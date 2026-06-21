#!/usr/bin/env python3
"""
trapd — AI Trap Management Daemon

Runs as root/systemd on the target board. Manages the ai-trap-detection binary
process lifecycle and provides an HTTP JSON-RPC API for remote management.

Socket: TCP port or Unix domain socket at /run/ai-trap/trapd.sock
"""

import argparse
import atexit
import json
import os
import signal
import subprocess
import sys
import tempfile
import threading
import time
import traceback
from http.server import HTTPServer, BaseHTTPRequestHandler
from pathlib import Path


# ─── Camera Presets ───────────────────────────────────────────────────────────
CAMERA_PRESETS = {
    "scene": {
        "camera.model": "scene",
        "camera.device": "",
        "camera.iq_dir": "",
        "camera.scene_dir": "/usr/share/ai-trap/scenes/insect_loop_4",
    },
    "imx415": {
        "camera.model": "imx415",
        "camera.device": "/dev/video0",
        "camera.iq_dir": "/etc/iqfiles",
        "camera.scene_dir": "",
    },
    "imx219": {
        "camera.model": "imx219",
        "camera.device": "/dev/video0",
        "camera.iq_dir": "/etc/iqfiles",
        "camera.scene_dir": "",
    },
    "ov5647": {
        "camera.model": "ov5647",
        "camera.device": "/dev/video0",
        "camera.iq_dir": "/etc/iqfiles",
        "camera.scene_dir": "",
    },
}

# ══════════════════════════════════════════════════════════════════════════════
# Trap Process Manager
# ══════════════════════════════════════════════════════════════════════════════

class TrapManager:
    """Manages the ai-trap-detection subprocess."""

    def __init__(self, config_path: str, log_file: str, pid_file: str, binary: str):
        self.config_path = Path(config_path)
        self.log_file = Path(log_file)
        self.pid_file = Path(pid_file)
        self.binary = binary
        self._process: subprocess.Popen | None = None
        self._lock = threading.Lock()
        self._start_time: float | None = None

    @property
    def is_running(self) -> bool:
        if self._process is None:
            return False
        ret = self._process.poll()
        return ret is None

    @property
    def pid(self) -> int | None:
        if self._process is not None and self.is_running:
            return self._process.pid
        return None

    @property
    def uptime(self) -> float:
        if self._start_time is None or not self.is_running:
            return 0.0
        return time.time() - self._start_time

    def start(self, binary: str | None = None) -> tuple[bool, str]:
        with self._lock:
            if self.is_running:
                return False, "Trap is already running (pid %d)" % self._process.pid

            exe = binary or self.binary
            if not os.path.isfile(exe):
                return False, "Binary not found: %s" % exe
            if not os.access(exe, os.X_OK):
                return False, "Binary not executable: %s" % exe

            # Ensure config exists
            if not self.config_path.is_file():
                return False, "Config not found: %s" % self.config_path

            # Ensure log dir exists
            self.log_file.parent.mkdir(parents=True, exist_ok=True)

            try:
                log_fh = open(str(self.log_file), "ab")
                self._process = subprocess.Popen(
                    [exe, "--config", str(self.config_path)],
                    stdout=log_fh,
                    stderr=subprocess.STDOUT,
                    stdin=subprocess.DEVNULL,
                    preexec_fn=os.setsid,  # isolate process group
                )
                self._start_time = time.time()
                self.pid_file.write_text(str(self._process.pid))
                return True, "Started (pid %d)" % self._process.pid
            except Exception as e:
                return False, "Failed to start: %s" % str(e)

    def stop(self, timeout: float = 5.0) -> tuple[bool, str]:
        pid = None
        with self._lock:
            if self._process is None:
                return False, "No trap process to stop"
            pid = self._process.pid
            self._process.terminate()

        # Wait outside lock for timeout
        try:
            self._process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            # Force kill
            with self._lock:
                try:
                    os.killpg(os.getpgid(pid), signal.SIGKILL)
                except (ProcessLookupError, PermissionError):
                    pass
                self._process.kill()
                self._process.wait(timeout=2)
                self._process = None
                self._start_time = None
            return True, "Killed (pid %d, timeout)" % pid

        with self._lock:
            self._process = None
            self._start_time = None
        return True, "Stopped (pid %d)" % pid

    def cleanup(self):
        """Ensure trap is stopped on daemon exit."""
        if self._process is not None:
            self.stop(timeout=3.0)
            # Kill any orphaned trap processes in our session
            if self.pid_file.exists():
                try:
                    pid = int(self.pid_file.read_text().strip())
                    os.killpg(os.getpgid(pid), signal.SIGKILL)
                except (ValueError, ProcessLookupError, PermissionError, OSError):
                    pass
                self.pid_file.unlink(missing_ok=True)

    def get_logs(self, lines: int = 100) -> list:
        """Return last N lines of the trap log."""
        if not self.log_file.is_file():
            return []
        try:
            text = self.log_file.read_text(encoding="utf-8", errors="replace")
            all_lines = text.rstrip("\n").split("\n")
            return all_lines[-lines:]
        except Exception:
            return []


# ══════════════════════════════════════════════════════════════════════════════
# Config Manager
# ══════════════════════════════════════════════════════════════════════════════

class ConfigManager:
    """Reads/writes the YAML config file with field-level access."""

    # Known scalar types in the config schema — used for validation
    SCHEMA: dict = {
        "camera": {
            "model": str,
            "device": str,
            "iq_dir": str,
            "scene_dir": str,
            "full_w": int,
            "full_h": int,
            "med_w": int,
            "med_h": int,
            "lores_w": int,
            "lores_h": int,
            "fps": int,
        },
        "inference": {
            "backend": str,
            "model_path": str,
            "confidence_threshold": float,
        },
        "storage": {
            "output_dir": str,
            "db_path": str,
        },
        "cropper": {
            "padding_px": int,
            "min_confidence": float,
        },
        "classifier": {
            "model_path": str,
            "confidence_threshold": float,
            "input_width": int,
            "input_height": int,
        },
        "actuator": {
            "type": str,
            "gpio": int,
            "duration_ms": int,
        },
        "motion_sensor": {
            "gpio": int,
        },
        "decision": {
            "trigger_confidence": float,
            "cooldown_ms": int,
        },
    }

    def __init__(self, config_path: str):
        self.config_path = Path(config_path)

    def load(self) -> dict:
        """Load current config from YAML file. Returns empty dict if not found."""
        import yaml
        if not self.config_path.is_file():
            return {}
        with open(self.config_path) as f:
            return yaml.safe_load(f) or {}

    def save(self, data: dict) -> tuple[bool, str]:
        """Write config atomically. Validates basic structure."""
        import yaml
        if not isinstance(data, dict):
            return False, "Config must be a dictionary"

        # Write to temp then rename
        try:
            self.config_path.parent.mkdir(parents=True, exist_ok=True)
            fd, tmp = tempfile.mkstemp(
                dir=str(self.config_path.parent),
                prefix=".config-",
                suffix=".yaml",
            )
            with os.fdopen(fd, "w") as f:
                yaml.dump(data, f, default_flow_style=False)
            os.rename(tmp, str(self.config_path))
            return True, "Config written"
        except Exception as e:
            return False, "Write failed: %s" % str(e)

    def get_field(self, path: str) -> tuple[bool, object]:
        """Get a single field by dot-path (e.g. 'camera.model')."""
        keys = path.split(".")
        data = self.load()
        for k in keys:
            if isinstance(data, dict) and k in data:
                data = data[k]
            else:
                return False, None
        return True, data

    def set_fields(self, updates: dict) -> tuple[bool, str, dict]:
        """Set multiple fields by dot-path. Returns (ok, msg, new_config)."""
        import yaml
        config = self.load()

        for path, value in updates.items():
            keys = path.split(".")
            d = config
            for k in keys[:-1]:
                if k not in d or not isinstance(d[k], dict):
                    d[k] = {}
                d = d[k]
            d[keys[-1]] = value

        # Validate: check known types
        for section, fields in self.SCHEMA.items():
            if section in config:
                for field, ftype in fields.items():
                    if field in config[section]:
                        val = config[section][field]
                        if not isinstance(val, ftype):
                            return False, "Field '%s.%s' type error: expected %s, got %s" % (
                                section, field, ftype.__name__, type(val).__name__,
                            ), config

        return True, "Fields updated", config

    def apply_preset(self, preset: str) -> tuple[bool, str, dict]:
        """Apply a camera preset by setting all preset fields."""
        if preset not in CAMERA_PRESETS:
            valid = list(CAMERA_PRESETS.keys())
            return False, "Unknown preset '%s'. Valid: %s" % (preset, valid), {}
        return self.set_fields(CAMERA_PRESETS[preset])


# ══════════════════════════════════════════════════════════════════════════════
# HTTP Handler
# ══════════════════════════════════════════════════════════════════════════════

class TrapdHandler(BaseHTTPRequestHandler):
    """HTTP JSON-RPC handler for the trap management API."""

    # Shared by all handler instances via the server
    trap_manager: TrapManager | None = None
    config_manager: ConfigManager | None = None
    quiet: bool = False

    def log_message(self, format, *args):
        if not self.quiet:
            super().log_message(format, *args)

    def _send_json(self, code: int, data: dict):
        body = json.dumps(data).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_body(self) -> dict:
        try:
            length = int(self.headers.get("Content-Length", 0))
            if length == 0:
                return {}
            raw = self.rfile.read(length)
            return json.loads(raw)
        except (json.JSONDecodeError, ValueError):
            return {}

    # ── Routes ──────────────────────────────────────────────────────────────

    def do_GET(self):
        if self.path == "/status":
            self._handle_status()
        elif self.path.startswith("/config"):
            self._handle_get_config()
        elif self.path.startswith("/logs"):
            self._handle_get_logs()
        elif self.path == "/presets":
            self._send_json(200, {"presets": CAMERA_PRESETS})
        else:
            self._send_json(404, {"error": "Not found"})

    def do_POST(self):
        if self.path == "/start":
            self._handle_start()
        elif self.path == "/stop":
            self._handle_stop()
        elif self.path == "/restart":
            self._handle_restart()
        elif self.path == "/config":
            self._handle_set_config()
        elif self.path == "/apply_preset":
            self._handle_apply_preset()
        else:
            self._send_json(404, {"error": "Not found"})

    def do_PATCH(self):
        if self.path == "/config":
            self._handle_patch_config()
        else:
            self._send_json(404, {"error": "Not found"})

    # ── Handlers ────────────────────────────────────────────────────────────

    def _handle_status(self):
        tm = self.trap_manager
        self._send_json(200, {
            "running": tm.is_running,
            "pid": tm.pid,
            "uptime_sec": tm.uptime,
            "config": str(tm.config_path),
            "binary": tm.binary,
        })

    def _handle_get_config(self):
        cm = self.config_manager
        # Support ?path=camera.model for single field
        path = self._get_query_param("path")
        if path:
            ok, val = cm.get_field(path)
            if ok:
                self._send_json(200, {"path": path, "value": val})
            else:
                self._send_json(404, {"error": "Path not found: " + path})
        else:
            self._send_json(200, cm.load())

    def _handle_get_logs(self):
        lines = int(self._get_query_param("lines") or 100)
        logs = self.trap_manager.get_logs(lines)
        self._send_json(200, {"lines": logs})

    def _handle_start(self):
        body = self._read_body()
        binary = body.get("binary") or self.trap_manager.binary
        ok, msg = self.trap_manager.start(binary)
        self._send_json(200 if ok else 400, {"ok": ok, "message": msg})

    def _handle_stop(self):
        ok, msg = self.trap_manager.stop()
        self._send_json(200 if ok else 400, {"ok": ok, "message": msg})

    def _handle_restart(self):
        self.trap_manager.stop()
        body = self._read_body()
        binary = body.get("binary") or self.trap_manager.binary
        ok, msg = self.trap_manager.start(binary)
        self._send_json(200 if ok else 400, {"ok": ok, "message": msg})

    def _handle_set_config(self):
        """PUT /config — replace entire config."""
        body = self._read_body()
        if "yaml" in body:
            import yaml
            try:
                data = yaml.safe_load(body["yaml"])
            except Exception as e:
                self._send_json(400, {"ok": False, "error": "Invalid YAML: %s" % e})
                return
        else:
            data = body.get("config", body)
        ok, msg = self.config_manager.save(data)
        self._send_json(200 if ok else 400, {"ok": ok, "message": msg})

    def _handle_patch_config(self):
        """PATCH /config — modify specific fields by dot-path."""
        body = self._read_body()
        updates = {}
        for k, v in body.items():
            if k.startswith("camera.") or k.startswith("inference.") or \
               k.startswith("storage.") or k.startswith("cropper.") or \
               k.startswith("classifier.") or k.startswith("actuator.") or \
               k.startswith("motion_sensor.") or k.startswith("decision."):
                updates[k] = v
        if not updates:
            self._send_json(400, {"ok": False, "error": "No valid config fields"})
            return
        ok, msg, new_config = self.config_manager.set_fields(updates)
        if ok:
            self.config_manager.save(new_config)
        self._send_json(200 if ok else 400, {"ok": ok, "message": msg})

    def _handle_apply_preset(self):
        body = self._read_body()
        preset = body.get("preset", "")
        ok, msg, new_config = self.config_manager.apply_preset(preset)
        if ok:
            self.config_manager.save(new_config)
        self._send_json(200 if ok else 400, {"ok": ok, "message": msg})

    def _get_query_param(self, name: str) -> str | None:
        from urllib.parse import urlparse, parse_qs
        parsed = urlparse(self.path)
        params = parse_qs(parsed.query)
        vals = params.get(name)
        return vals[0] if vals else None

    # Suppress default logging noise
    def log_request(self, code="-", size="-"):
        pass


# ══════════════════════════════════════════════════════════════════════════════
# Main
# ══════════════════════════════════════════════════════════════════════════════

def parse_args(argv=None):
    p = argparse.ArgumentParser(description="AI Trap Management Daemon")
    p.add_argument("--port", type=int, default=0, help="TCP port (0=Unix socket only)")
    p.add_argument("--socket", default="/run/ai-trap/trapd.sock",
                   help="Unix domain socket path")
    p.add_argument("--binary", default="/usr/share/ai-trap/ai-trap-detection",
                   help="Path to trap binary")
    p.add_argument("--config-path", default="/etc/ai-trap/config.yaml",
                   help="Path to trap config YAML")
    p.add_argument("--log-file", default="/var/log/ai-trap/trap.log",
                   help="Path to trap log file")
    p.add_argument("--pid-file", default="/run/ai-trap/trap.pid",
                   help="Path to trap pid file")
    p.add_argument("--no-fork", action="store_true", help="Run in foreground")
    p.add_argument("--quiet", action="store_true", help="Suppress log messages")
    return p.parse_args(argv)


def main():
    args = parse_args()

    # Ensure directories exist
    for path in [args.socket, args.config_path, args.log_file, args.pid_file]:
        p = Path(path).parent
        p.mkdir(parents=True, exist_ok=True)

    # Init managers
    tm = TrapManager(
        config_path=args.config_path,
        log_file=args.log_file,
        pid_file=args.pid_file,
        binary=args.binary,
    )
    cm = ConfigManager(config_path=args.config_path)

    # Wire up handler
    TrapdHandler.trap_manager = tm
    TrapdHandler.config_manager = cm
    TrapdHandler.quiet = args.quiet

    # Create initial config if missing
    if not Path(args.config_path).is_file():
        cm.save(cm.load())  # saves empty {}

    # Start HTTP server on TCP if port specified
    if args.port:
        tcp_server = HTTPServer(("0.0.0.0", args.port), TrapdHandler)
        t = threading.Thread(target=tcp_server.serve_forever, daemon=True)
        t.start()
        print("trapd: TCP server on port %d" % args.port, file=sys.stderr)

    import socket as _socket
    from socketserver import UnixStreamServer as _UnixStreamServer
    sock_path = args.socket
    if os.path.exists(sock_path):
        os.unlink(sock_path)
    class _UnixServer(_UnixStreamServer):
        allow_reuse_address = True
        address_family = _socket.AF_UNIX
    unix_server = _UnixServer(sock_path, TrapdHandler)
    os.chmod(sock_path, 0o666)
    ut = threading.Thread(target=unix_server.serve_forever, daemon=True)
    ut.start()
    print("trapd: Unix socket at %s" % sock_path, file=sys.stderr)

    # Cleanup on exit
    atexit.register(tm.cleanup)

    # Block
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        print("\ntrapd: shutting down", file=sys.stderr)
        tm.cleanup()
        sys.exit(0)


if __name__ == "__main__":
    main()