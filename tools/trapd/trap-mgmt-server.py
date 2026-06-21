#!/usr/bin/env python3
"""
trap-mgmt-server.py — MCP Server for Trap Management

Supports multiple target boards. Each tool accepts a `host` parameter
(pointing to the trapd daemon's HTTP API on that board). Defaults to
environment variable TRAPD_HOST if not specified per-call.

Usage with Cline MCP settings:
  {
    "mcpServers": {
      "trap-mgmt": {
        "command": "python3",
        "args": ["tools/trapd/trap-mgmt-server.py"]
      }
    }
  }

Then in any tool call: {"host": "http://10.0.10.226:9090"}
"""

import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

DEFAULT_HOST = os.environ.get("TRAPD_HOST", "http://localhost:9090")
DEFAULT_BINARY = os.environ.get("TRAPD_BINARY", "/usr/share/ai-trap/ai-trap-detection")


def _resolve_host(args: dict) -> str:
    """Use per-request host, or fall back to env var / default."""
    return args.get("host") or DEFAULT_HOST


def _trapd_req(method: str, path: str, host: str, body: dict | None = None) -> dict:
    """Send HTTP request to a specific target's trapd daemon."""
    url = f"{host.rstrip('/')}{path}"
    data = json.dumps(body).encode("utf-8") if body else None
    req = urllib.request.Request(
        url,
        data=data,
        method=method,
        headers={"Content-Type": "application/json"} if data else {},
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        try:
            return json.loads(e.read().decode("utf-8"))
        except Exception:
            return {"ok": False, "error": f"HTTP {e.code}: {e.reason}"}
    except urllib.error.URLError as e:
        return {"ok": False, "error": f"Connection to {host}: {e.reason}"}
    except Exception as e:
        return {"ok": False, "error": str(e)}


# ── Tool Handlers ──────────────────────────────────────────────────────────────

def handle_status(args: dict) -> dict:
    return _trapd_req("GET", "/status", _resolve_host(args))

def handle_start(args: dict) -> dict:
    binary = args.get("binary") or DEFAULT_BINARY
    return _trapd_req("POST", "/start", _resolve_host(args), {"binary": binary})

def handle_stop(args: dict) -> dict:
    return _trapd_req("POST", "/stop", _resolve_host(args))

def handle_restart(args: dict) -> dict:
    binary = args.get("binary") or DEFAULT_BINARY
    h = _resolve_host(args)
    _trapd_req("POST", "/stop", h)
    return _trapd_req("POST", "/start", h, {"binary": binary})

def handle_get_config(args: dict) -> dict:
    path = args.get("path", "")
    h = _resolve_host(args)
    if path:
        return _trapd_req("GET", f"/config?path={path}", h)
    return _trapd_req("GET", "/config", h)

def handle_set_config(args: dict) -> dict:
    config = args.get("config", {})
    return _trapd_req("POST", "/config", _resolve_host(args), {"config": config})

def handle_patch_config(args: dict) -> dict:
    return _trapd_req("PATCH", "/config", _resolve_host(args),
                      args.get("fields", args))

def handle_apply_preset(args: dict) -> dict:
    preset = args.get("preset", "")
    return _trapd_req("POST", "/apply_preset", _resolve_host(args),
                      {"preset": preset})

def handle_get_logs(args: dict) -> dict:
    lines = args.get("lines", 100)
    return _trapd_req("GET", f"/logs?lines={lines}", _resolve_host(args))

def handle_list_presets(args: dict) -> dict:
    return _trapd_req("GET", "/presets", _resolve_host(args))


# ── Base schema for tools that accept a target host ────────────────────────────
HOST_SCHEMA = {
    "host": {
        "type": "string",
        "description": "Target trapd URL (e.g. http://10.0.10.226:9090). Defaults to TRAPD_HOST env.",
    }
}

TOOLS = [
    {
        "name": "trapd_status",
        "description": "Get trap daemon and process status on a target board",
        "inputSchema": {"type": "object", "properties": dict(HOST_SCHEMA)},
        "handler": handle_status,
    },
    {
        "name": "trapd_start",
        "description": "Start the ai-trap-detection binary on a target board",
        "inputSchema": {
            "type": "object",
            "properties": {
                **HOST_SCHEMA,
                "binary": {
                    "type": "string",
                    "description": "Path to binary (default: /usr/share/ai-trap/ai-trap-detection)",
                },
            },
        },
        "handler": handle_start,
    },
    {
        "name": "trapd_stop",
        "description": "Stop the running trap process on a target board",
        "inputSchema": {"type": "object", "properties": dict(HOST_SCHEMA)},
        "handler": handle_stop,
    },
    {
        "name": "trapd_restart",
        "description": "Restart the trap process on a target board",
        "inputSchema": {
            "type": "object",
            "properties": {
                **HOST_SCHEMA,
                "binary": {
                    "type": "string",
                    "description": "Path to binary (default: /usr/share/ai-trap/ai-trap-detection)",
                },
            },
        },
        "handler": handle_restart,
    },
    {
        "name": "trapd_get_config",
        "description": "Get trap configuration (full or single field) from a target board",
        "inputSchema": {
            "type": "object",
            "properties": {
                **HOST_SCHEMA,
                "path": {
                    "type": "string",
                    "description": "Dot-path for single field, e.g. 'camera.model'. Omit for full config.",
                },
            },
        },
        "handler": handle_get_config,
    },
    {
        "name": "trapd_set_config",
        "description": "Replace entire config on a target board",
        "inputSchema": {
            "type": "object",
            "properties": {
                **HOST_SCHEMA,
                "config": {"type": "object", "description": "Full config dictionary"},
            },
            "required": ["config"],
        },
        "handler": handle_set_config,
    },
    {
        "name": "trapd_patch_config",
        "description": "Modify specific config fields by dot-path on a target board",
        "inputSchema": {
            "type": "object",
            "properties": {
                **HOST_SCHEMA,
                "fields": {
                    "type": "object",
                    "description": "Field path → value, e.g. {'camera.model': 'imx415'}",
                },
            },
            "required": ["fields"],
        },
        "handler": handle_patch_config,
    },
    {
        "name": "trapd_apply_preset",
        "description": "Apply a camera preset (scene/imx415/imx219/ov5647) on a target board",
        "inputSchema": {
            "type": "object",
            "properties": {
                **HOST_SCHEMA,
                "preset": {
                    "type": "string",
                    "enum": ["scene", "imx415", "imx219", "ov5647"],
                    "description": "Camera preset name",
                },
            },
            "required": ["preset"],
        },
        "handler": handle_apply_preset,
    },
    {
        "name": "trapd_list_presets",
        "description": "List available camera presets",
        "inputSchema": {"type": "object", "properties": dict(HOST_SCHEMA)},
        "handler": handle_list_presets,
    },
    {
        "name": "trapd_get_logs",
        "description": "Get recent trap log lines from a target board",
        "inputSchema": {
            "type": "object",
            "properties": {
                **HOST_SCHEMA,
                "lines": {
                    "type": "integer",
                    "description": "Number of lines (default: 100)"
                },
            },
        },
        "handler": handle_get_logs,
    },
]

TOOL_MAP = {t["name"]: t for t in TOOLS}


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except json.JSONDecodeError:
            continue

        req_id = req.get("id")
        method = req.get("method", "")

        if method == "initialize":
            response = {
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {"tools": {}},
                    "serverInfo": {"name": "trap-mgmt", "version": "1.0.0"},
                },
            }
        elif method == "notifications/initialized":
            continue
        elif method == "tools/list":
            response = {
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {
                    "tools": [{"name": t["name"],
                               "description": t["description"],
                               "inputSchema": t["inputSchema"]}
                              for t in TOOLS],
                },
            }
        elif method == "tools/call":
            tool_name = req.get("params", {}).get("name", "")
            tool_args = req.get("params", {}).get("arguments", {})
            if tool_name in TOOL_MAP:
                try:
                    result = TOOL_MAP[tool_name]["handler"](tool_args)
                    response = {"jsonrpc": "2.0", "id": req_id, "result": {"content": [{"type": "text", "text": json.dumps(result)}]}}
                except Exception as e:
                    response = {
                        "jsonrpc": "2.0", "id": req_id,
                        "error": {"code": -32000, "message": str(e)},
                    }
            else:
                response = {
                    "jsonrpc": "2.0", "id": req_id,
                    "error": {"code": -32601, "message": f"Tool not found: {tool_name}"},
                }
        elif method == "shutdown":
            break
        else:
            response = {
                "jsonrpc": "2.0", "id": req_id,
                "error": {"code": -32601, "message": f"Method not found: {method}"},
            }

        sys.stdout.write(json.dumps(response) + "\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()