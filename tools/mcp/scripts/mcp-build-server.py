#!/usr/bin/env python3
"""
MCP Server for AI Camera Trap Build System
Extended with run, debug, and process management tools for all platforms.

Uses the official `mcp` SDK (Model Context Protocol) for proper async MCP protocol handling.

Usage:
  This script is run by Cline as an MCP server via STDIN/STDOUT JSON-RPC.
  It is NOT meant to be run directly from the terminal.

Installation:
  cp scripts/mcp-build-server.py /Users/steve/.local/bin/
  chmod +x /Users/steve/.local/bin/mcp-build-server.py
  pip install mcp

MCP Config (cline_mcp_settings.json):
  {
    "mcpServers": {
      "ai-trap-build": {
        "command": "python3",
        "args": ["/Users/steve/.local/bin/mcp-build-server.py"],
        "env": {},
        "disabled": false,
        "autoApprove": [
          "build",
          "run", "stop", "status",
          "logs", "debug", "list_binaries"
        ]
      }
    }
  }

"""

import subprocess
import json
import sys
import os
import re
import signal
import anyio
from pathlib import Path
from typing import List, Dict, Any, Optional

from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import (
    Tool,
    TextContent,
    CallToolResult,
)

# ============================================================================
# Compiler Error Parser
# ============================================================================

class CompilerErrorParser:
    """Parse GCC/Clang compiler output into structured errors"""

    # GCC/Clang error pattern: file:line:column: error: message
    GCC_ERROR_PATTERN = re.compile(
        r'^(?P<file>[^:]+):(?P<line>\d+):(?P<col>\d+):\s+(?P<severity>error|warning|note):\s+(?P<message>.*)$'
    )

    # Linker error pattern (file:line: error: message)
    LINKER_ERROR_PATTERN = re.compile(
        r'^(?P<file>[^:]+):(?P<line>\d+):\s+(?P<severity>error|warning):\s+(?P<message>.*)$'
    )

    # Undefined reference (linker)
    UNDEFINED_PATTERN = re.compile(
        r'undefined reference to [`\'](?P<symbol>[^\'\"]+)'
    )

    # Missing include
    MISSING_INCLUDE_PATTERN = re.compile(
        r'[`\'](?P<header>[^\'\"]+\.h)[`\']: No such file or directory'
    )

    @classmethod
    def parse_line(cls, line: str) -> Optional[Dict[str, Any]]:
        """Parse a single compiler output line"""

        # Try GCC pattern
        match = cls.GCC_ERROR_PATTERN.match(line)
        if match:
            return {
                "type": "compiler",
                "file": match.group("file"),
                "line": int(match.group("line")),
                "column": int(match.group("col")),
                "severity": match.group("severity"),
                "message": match.group("message").strip()
            }

        # Try linker pattern
        match = cls.LINKER_ERROR_PATTERN.match(line)
        if match:
            error = {
                "type": "linker",
                "file": match.group("file"),
                "line": int(match.group("line")),
                "severity": match.group("severity"),
                "message": match.group("message").strip()
            }

            # Check for undefined reference
            undef_match = cls.UNDEFINED_PATTERN.search(line)
            if undef_match:
                error["symbol"] = undef_match.group("symbol")
                error["suggestion"] = f"Missing implementation or library for '{undef_match.group('symbol')}'"

            return error

        return None

    @classmethod
    def parse_output(cls, output: str) -> Dict[str, Any]:
        """Parse full compiler/linker output"""
        errors = []
        warnings = []
        linker_errors = []

        lines = output.split('\n')

        for line in lines:
            parsed = cls.parse_line(line)

            if parsed:
                if parsed["severity"] == "error":
                    if parsed["type"] == "linker":
                        linker_errors.append(parsed)
                    else:
                        errors.append(parsed)
                elif parsed["severity"] == "warning":
                    warnings.append(parsed)

        # Summary
        summary = {
            "total_errors": len(errors),
            "total_warnings": len(warnings),
            "total_linker_errors": len(linker_errors),
            "build_failed": len(errors) > 0 or len(linker_errors) > 0
        }

        # Generate AI-friendly suggestions
        suggestions = []
        for err in errors:
            if "implicit declaration" in err["message"]:
                suggestions.append(f"Missing #include in {err['file']}:{err['line']}")
            elif "unknown type name" in err["message"]:
                suggestions.append(f"Missing header or forward declaration in {err['file']}:{err['line']}")

        for err in linker_errors:
            if "undefined reference" in err["message"]:
                symbol = err.get("symbol", "unknown")
                suggestions.append(f"Linker error: '{symbol}' not defined. Check library linkage or implement missing function.")

        return {
            "errors": errors,
            "warnings": warnings,
            "linker_errors": linker_errors,
            "summary": summary,
            "suggestions": suggestions,
            "raw_output": output[-2000:]  # Last 2000 chars for context
        }


# ============================================================================
# Crash / GDB Output Parser
# ============================================================================

class CrashParser:
    """Parse GDB backtrace and crash output into structured data"""

    # Signal pattern: "Program received signal SIGSEGV, Segmentation fault."
    SIGNAL_PATTERN = re.compile(
        r'Program received signal\s+(?P<signal>SIG\w+),\s+(?P<signal_desc>.+)$'
    )

    # Backtrace frame: "#0  function (arg=...) at file:line"
    BT_FRAME_PATTERN = re.compile(
        r'^#(?P<frame>\d+)\s+(0x[0-9a-f]+\s+in\s+)?(?P<function>\S+)\s+'
        r'(\(.*\)\s+)?at\s+(?P<file>[^:]+):(?P<line>\d+)'
    )

    # Backtrace frame without file info: "#0  0x0000... in function_name"
    BT_FRAME_NO_FILE_PATTERN = re.compile(
        r'^#(?P<frame>\d+)\s+(0x[0-9a-f]+\s+in\s+)?(?P<function>\S+)'
    )

    # Fault address: "Address 0x1234 is not stack'd, malloc'd or (recently) free'd"
    FAULT_ADDR_PATTERN = re.compile(
        r'[Aa]ddress\s+(?P<address>0x[0-9a-f]+)'
    )

    @classmethod
    def parse_gdb_output(cls, output: str) -> Dict[str, Any]:
        """Parse GDB output into structured crash data"""
        result = {
            "crashed": False,
            "signal": None,
            "signal_description": None,
            "fault_address": None,
            "backtrace": [],
            "raw_output": output[-3000:]  # Last 3000 chars
        }

        lines = output.split('\n')

        for line in lines:
            # Check for signal
            signal_match = cls.SIGNAL_PATTERN.search(line)
            if signal_match:
                result["crashed"] = True
                result["signal"] = signal_match.group("signal")
                result["signal_description"] = signal_match.group("signal_desc").strip()

            # Check for fault address
            addr_match = cls.FAULT_ADDR_PATTERN.search(line)
            if addr_match:
                result["fault_address"] = addr_match.group("address")

            # Check for backtrace frame with file info
            bt_match = cls.BT_FRAME_PATTERN.match(line)
            if bt_match:
                result["backtrace"].append({
                    "frame": int(bt_match.group("frame")),
                    "function": bt_match.group("function"),
                    "file": bt_match.group("file"),
                    "line": int(bt_match.group("line"))
                })
                continue

            # Check for backtrace frame without file info
            bt_match = cls.BT_FRAME_NO_FILE_PATTERN.match(line)
            if bt_match:
                result["backtrace"].append({
                    "frame": int(bt_match.group("frame")),
                    "function": bt_match.group("function"),
                    "file": None,
                    "line": None
                })

        # Check for segfault in output even without GDB signal pattern
        if not result["signal"]:
            for line in lines:
                if "Segmentation fault" in line or "SIGSEGV" in line:
                    result["crashed"] = True
                    result["signal"] = "SIGSEGV"
                    result["signal_description"] = "Segmentation fault"
                    break
                elif "SIGABRT" in line or "Aborted" in line:
                    result["crashed"] = True
                    result["signal"] = "SIGABRT"
                    result["signal_description"] = "Aborted (assertion failure or abort())"
                    break

        return result


# ============================================================================
# Platform Configuration
# ============================================================================

PLATFORM_CONFIGS = {
    "rock3c": {
        "build_dir": "build-rock3c",
        "trap_path_template": "targets/build/ai-trap-detection",
        "default_host": "rock-3c.local",
        "default_user": "radxa",
        "default_trap": "rock3c-imx219",
        "remote_dir": "~/ai-traps",
        "model_type": "rknn",
        "model_path": "models/detection/insects/yolo26n",
        "model_output": "yolo26n.rknn",
        "model_convert_script": "models/detection/insects/yolo26n/convert/convert_rknn.py",
    },
    "cubie-a7s": {
        "build_dir": "build-cubie-a7s",
        "trap_path_template": "targets/build/ai-trap-detection",
        "default_host": "cubie-a7s.local",
        "default_user": "radxa",
        "default_trap": "cubie-a7s",
        "remote_dir": "~/ai-traps",
        "model_type": "nbg",
        "model_path": "models/detection/insects/yolo26n",
        "model_output": "nbg_output/network_binary.nb",
        "model_convert_script": "models/detection/insects/yolo26n/convert/convert_nbg_docker.sh",
    },
    "rdk-x5": {
        "build_dir": "build-rdk-x5",
        "trap_path_template": "targets/build/ai-trap-detection",
        "default_host": "rdk-x5.local",
        "default_user": "root",
        "default_trap": "rdk-x5",
        "remote_dir": "~/ai-traps",
        "model_type": None,
        "model_path": None,
        "model_output": None,
        "model_convert_script": None,
    }
}


def get_platform_config(platform: str) -> Optional[Dict[str, Any]]:
    """Get platform configuration, returns None if unknown"""
    return PLATFORM_CONFIGS.get(platform)


def resolve_config(platform: str, params: Dict[str, Any]) -> Dict[str, Any]:
    """Resolve platform config with user-provided overrides"""
    config = get_platform_config(platform)
    if not config:
        return {"error": f"Unknown platform: {platform}"}

    return {
        "host": params.get("host", config["default_host"]),
        "user": params.get("user", config["default_user"]),
        "trap_name": params.get("trap_name", config["default_trap"]),
        "build_dir": config["build_dir"],
        "trap_path_template": config["trap_path_template"],
        "remote_dir": config["remote_dir"],
        "project_root": params.get("project_root", str(Path.home() / "naturesense" / "ai-traps")),
        "model_type": config["model_type"],
        "model_path": config["model_path"],
        "model_output": config["model_output"],
        "model_convert_script": config["model_convert_script"],
    }


def get_binary_path(remote_dir: str, build_dir: str, trap_path_template: str, trap_name: str) -> str:
    """Get the full path to the built binary on the board"""
    trap_path = trap_path_template.format(trap=trap_name)
    return f"{remote_dir}/{build_dir}/{trap_path}"


def get_run_command(user: str, host: str, binary_path: str, sudo: bool = True, args: str = "") -> str:
    """Get the SSH command to run the binary"""
    prefix = "sudo " if sudo else ""
    cd_cmd = f"cd {os.path.dirname(binary_path)} && " if os.path.dirname(binary_path) else ""
    return f"ssh {user}@{host} '{cd_cmd}{prefix}{binary_path} {args}'".strip()


# ============================================================================
# SSH Helpers
# ============================================================================

def ssh_execute(user: str, host: str, command: str, timeout: int = 60) -> Dict[str, Any]:
    """Execute a command on the remote board via SSH and return structured result"""
    try:
        ssh_cmd = ["ssh", "-o", "ConnectTimeout=10", f"{user}@{host}", command]
        result = subprocess.run(ssh_cmd, capture_output=True, text=True, timeout=timeout)
        return {
            "success": result.returncode == 0,
            "returncode": result.returncode,
            "stdout": result.stdout,
            "stderr": result.stderr,
            "combined": result.stdout + result.stderr
        }
    except subprocess.TimeoutExpired:
        return {
            "success": False,
            "returncode": -1,
            "stdout": "",
            "stderr": "",
            "combined": f"SSH command timed out after {timeout} seconds"
        }
    except Exception as e:
        return {
            "success": False,
            "returncode": -1,
            "stdout": "",
            "stderr": "",
            "combined": f"SSH error: {str(e)}"
        }


def ssh_execute_background(user: str, host: str, command: str, pid_file: str) -> Dict[str, Any]:
    """Launch a command in the background on the remote board via SSH"""
    try:
        # Use nohup and store PID
        bg_script = f"nohup sh -c '{command}' > /tmp/{os.path.basename(pid_file)}.out 2>&1 & echo $!"
        ssh_cmd = ["ssh", "-o", "ConnectTimeout=10", f"{user}@{host}", bg_script]
        result = subprocess.run(ssh_cmd, capture_output=True, text=True, timeout=15)
        pid = result.stdout.strip()

        if pid and pid.isdigit():
            # Store PID on the board for later management
            store_cmd = f"echo {pid} > {pid_file}"
            subprocess.run(["ssh", f"{user}@{host}", store_cmd], capture_output=True, timeout=10)
            return {
                "success": True,
                "pid": int(pid),
                "pid_file": pid_file,
                "message": f"Process started with PID {pid}"
            }
        else:
            return {
                "success": False,
                "pid": None,
                "message": f"Failed to start background process. Output: {result.stdout.strip() or result.stderr.strip()}"
            }
    except Exception as e:
        return {
            "success": False,
            "pid": None,
            "message": f"Error starting background process: {str(e)}"
        }


# ============================================================================
# MCP Server Setup
# ============================================================================

server = Server("ai-trap-build", version="1.1.0")


# ============================================================================
# Tool Definitions
# ============================================================================

@server.list_tools()
async def list_tools() -> list[Tool]:
    """Return the list of available tools"""
    return [
        # ── Build Tool (unified: model/toolkit/target) ──
        Tool(
            name="build",
            description="Build AI Camera Trap components. Supports building model, toolkit, and/or target binary for any platform.",
            inputSchema={
                "type": "object",
                "properties": {
                    "platform": {
                        "type": "string",
                        "description": "Target platform: rock3c, cubie-a7s, or rdk-x5",
                        "enum": ["rock3c", "cubie-a7s", "rdk-x5"]
                    },
                    "component": {
                        "type": "string",
                        "description": "What to build: 'model' (compile ONNX→RKNN/NBG), 'toolkit' (libtoolkit.a), 'target' (trap binary), or 'all' (model→toolkit→target)",
                        "enum": ["model", "toolkit", "target", "all"],
                        "default": "target"
                    },
                    "trap_type": {
                        "type": "string",
                        "description": "Trap type (e.g., 'detection', 'classification'). Determines which model to compile and which binary to build.",
                        "default": "detection"
                    },
                    "project_root": {
                        "type": "string",
                        "description": "Path to project root (default: ~/naturesense/ai-traps)"
                    },
                    "host": {
                        "type": "string",
                        "description": "Board hostname (default: per-platform, e.g. rock-3c.local)"
                    },
                    "user": {
                        "type": "string",
                        "description": "SSH user (default: per-platform, radxa or root)"
                    },
                    "trap_name": {
                        "type": "string",
                        "description": "Specific trap to build (e.g., rock3c-imx219, cubie-a7s, rdk-x5)",
                        "default": ""
                    },
                    "rebuild": {
                        "type": "boolean",
                        "description": "If true, performs a full rebuild from scratch",
                        "default": False
                    }
                },
                "required": ["platform"]
            }
        ),

        # ── Run Tool (parameterized by platform) ──
        Tool(
            name="run",
            description="Run the built trap binary on a target board. Supports foreground (captures output) and background (returns PID) modes.",
            inputSchema={
                "type": "object",
                "properties": {
                    "platform": {
                        "type": "string",
                        "description": "Target platform: rock3c, cubie-a7s, or rdk-x5",
                        "enum": ["rock3c", "cubie-a7s", "rdk-x5"]
                    },
                    "host": {
                        "type": "string",
                        "description": "Board hostname (default: per-platform)"
                    },
                    "user": {
                        "type": "string",
                        "description": "SSH user (default: per-platform)"
                    },
                    "trap_name": {
                        "type": "string",
                        "description": "Trap to run (default: per-platform)"
                    },
                    "args": {
                        "type": "string",
                        "description": "Additional CLI arguments to pass to the binary (e.g., '--duration 30 --threshold 0.5')",
                        "default": ""
                    },
                    "timeout": {
                        "type": "number",
                        "description": "Max runtime in seconds for foreground mode (default: 60, max: 300)",
                        "default": 60
                    },
                    "background": {
                        "type": "boolean",
                        "description": "If true, runs in background via nohup and returns PID immediately",
                        "default": False
                    },
                    "sudo": {
                        "type": "boolean",
                        "description": "Whether to run with sudo (default: true, camera traps need hardware access)",
                        "default": True
                    }
                },
                "required": ["platform"]
            }
        ),
        Tool(
            name="stop",
            description="Stop a running trap on a target board by sending a signal.",
            inputSchema={
                "type": "object",
                "properties": {
                    "platform": {
                        "type": "string",
                        "description": "Target platform: rock3c, cubie-a7s, or rdk-x5",
                        "enum": ["rock3c", "cubie-a7s", "rdk-x5"]
                    },
                    "host": {
                        "type": "string",
                        "description": "Board hostname (default: per-platform)"
                    },
                    "user": {
                        "type": "string",
                        "description": "SSH user (default: per-platform)"
                    },
                    "trap_name": {
                        "type": "string",
                        "description": "Trap to stop (default: per-platform)"
                    },
                    "signal": {
                        "type": "string",
                        "description": "Signal to send: TERM (graceful) or KILL (force)",
                        "default": "TERM"
                    }
                },
                "required": ["platform"]
            }
        ),
        Tool(
            name="status",
            description="Check if a trap is running on a target board and get process info (PID, CPU, memory, uptime).",
            inputSchema={
                "type": "object",
                "properties": {
                    "platform": {
                        "type": "string",
                        "description": "Target platform: rock3c, cubie-a7s, or rdk-x5",
                        "enum": ["rock3c", "cubie-a7s", "rdk-x5"]
                    },
                    "host": {
                        "type": "string",
                        "description": "Board hostname (default: per-platform)"
                    },
                    "user": {
                        "type": "string",
                        "description": "SSH user (default: per-platform)"
                    },
                    "trap_name": {
                        "type": "string",
                        "description": "Trap to check (default: per-platform)"
                    }
                },
                "required": ["platform"]
            }
        ),
        Tool(
            name="logs",
            description="Fetch recent log output from a trap on a target board. Can fetch from journalctl or a log file.",
            inputSchema={
                "type": "object",
                "properties": {
                    "platform": {
                        "type": "string",
                        "description": "Target platform: rock3c, cubie-a7s, or rdk-x5",
                        "enum": ["rock3c", "cubie-a7s", "rdk-x5"]
                    },
                    "host": {
                        "type": "string",
                        "description": "Board hostname (default: per-platform)"
                    },
                    "user": {
                        "type": "string",
                        "description": "SSH user (default: per-platform)"
                    },
                    "trap_name": {
                        "type": "string",
                        "description": "Trap to fetch logs for (default: per-platform)"
                    },
                    "lines": {
                        "type": "number",
                        "description": "Number of recent log lines to fetch (default: 50)",
                        "default": 50
                    },
                    "source": {
                        "type": "string",
                        "description": "Log source: 'journalctl' (systemd journal) or 'file' with a log_path",
                        "default": "journalctl"
                    },
                    "log_path": {
                        "type": "string",
                        "description": "Path to log file on the board (only used if source='file')",
                        "default": ""
                    }
                },
                "required": ["platform"]
            }
        ),
        Tool(
            name="debug",
            description="Run a trap under GDB on a target board for crash analysis. Captures backtrace on segfault/abort.",
            inputSchema={
                "type": "object",
                "properties": {
                    "platform": {
                        "type": "string",
                        "description": "Target platform: rock3c, cubie-a7s, or rdk-x5",
                        "enum": ["rock3c", "cubie-a7s", "rdk-x5"]
                    },
                    "host": {
                        "type": "string",
                        "description": "Board hostname (default: per-platform)"
                    },
                    "user": {
                        "type": "string",
                        "description": "SSH user (default: per-platform)"
                    },
                    "trap_name": {
                        "type": "string",
                        "description": "Trap to debug (default: per-platform)"
                    },
                    "args": {
                        "type": "string",
                        "description": "CLI arguments to pass to the binary",
                        "default": ""
                    },
                    "timeout": {
                        "type": "number",
                        "description": "Max runtime in seconds (default: 30, max: 120)",
                        "default": 30
                    }
                },
                "required": ["platform"]
            }
        ),
        Tool(
            name="list_binaries",
            description="List built binaries on a target board with file sizes and modification times.",
            inputSchema={
                "type": "object",
                "properties": {
                    "platform": {
                        "type": "string",
                        "description": "Target platform: rock3c, cubie-a7s, or rdk-x5",
                        "enum": ["rock3c", "cubie-a7s", "rdk-x5"]
                    },
                    "host": {
                        "type": "string",
                        "description": "Board hostname (default: per-platform)"
                    },
                    "user": {
                        "type": "string",
                        "description": "SSH user (default: per-platform)"
                    }
                },
                "required": ["platform"]
            }
        ),
    ]


# ============================================================================
# Model Compilation Functions
# ============================================================================

def compile_rknn_model(project_root: str, trap_type: str) -> Dict[str, Any]:
    """Compile ONNX model to RKNN format for Rockchip NPU (ROCK 3C)"""
    result = {
        "component": "model",
        "platform": "rock3c",
        "model_type": "rknn",
        "success": False,
        "message": "",
        "output_path": None
    }

    # Determine model path based on trap_type
    if trap_type == "detection":
        model_dir = os.path.join(project_root, "models", "detection", "insects", "yolo26n")
        onnx_path = os.path.join(model_dir, "best.onnx")
        output_path = os.path.join(model_dir, "yolo26n.rknn")
        convert_script = os.path.join(model_dir, "convert", "convert_rknn.py")
    else:
        result["message"] = f"Unknown trap_type '{trap_type}' for RKNN compilation"
        return result

    # Validate inputs
    if not os.path.isfile(onnx_path):
        result["message"] = f"ONNX model not found: {onnx_path}"
        return result

    if not os.path.isfile(convert_script):
        result["message"] = f"Convert script not found: {convert_script}"
        return result

    # Run RKNN conversion (remote via SSH to ROCK 3C, or local if rknn-toolkit2 is available)
    try:
        # Try local conversion first (if rknn-toolkit2 is installed)
        import importlib.util
        rknn_spec = importlib.util.find_spec("rknn")
        if rknn_spec is not None:
            # Local conversion
            cmd = [
                sys.executable, convert_script,
                "--input", onnx_path,
                "--output", output_path,
                "--target", "rk3566",
                "--imgsz", "320",
                "--quantize"
            ]
            proc = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
            if proc.returncode == 0:
                result["success"] = True
                result["message"] = f"RKNN model compiled locally: {output_path}"
                result["output_path"] = output_path
            else:
                result["message"] = f"Local RKNN conversion failed:\n{proc.stderr[-1000:]}"
        else:
            # Remote conversion via SSH to ROCK 3C
            rock3c_host = PLATFORM_CONFIGS["rock3c"]["default_host"]
            rock3c_user = PLATFORM_CONFIGS["rock3c"]["default_user"]
            remote_dir = "/tmp/rknn_convert"
            remote_onnx = f"{remote_dir}/best.onnx"
            remote_output = f"{remote_dir}/yolo26n.rknn"

            # Create remote dir
            ssh_execute(rock3c_user, rock3c_host, f"mkdir -p {remote_dir}", timeout=10)

            # Copy ONNX and script to board
            subprocess.run(["scp", onnx_path, f"{rock3c_user}@{rock3c_host}:{remote_onnx}"],
                           capture_output=True, timeout=30)
            subprocess.run(["scp", convert_script, f"{rock3c_user}@{rock3c_host}:{remote_dir}/convert_rknn.py"],
                           capture_output=True, timeout=30)

            # Run conversion on board
            remote_cmd = (
                f"python3 {remote_dir}/convert_rknn.py"
                f" --input {remote_onnx}"
                f" --output {remote_output}"
                f" --target rk3566 --imgsz 320 --quantize"
            )
            ssh_result = ssh_execute(rock3c_user, rock3c_host, remote_cmd, timeout=600)

            if ssh_result["success"]:
                # Copy result back
                subprocess.run(["scp", f"{rock3c_user}@{rock3c_host}:{remote_output}", output_path],
                               capture_output=True, timeout=30)
                # Cleanup
                ssh_execute(rock3c_user, rock3c_host, f"rm -rf {remote_dir}", timeout=10)

                if os.path.isfile(output_path):
                    result["success"] = True
                    result["message"] = f"RKNN model compiled on {rock3c_host}: {output_path}"
                    result["output_path"] = output_path
                else:
                    result["message"] = f"Remote RKNN conversion completed but output not found at {output_path}"
            else:
                result["message"] = f"Remote RKNN conversion failed:\n{ssh_result['combined'][-1000:]}"

    except subprocess.TimeoutExpired:
        result["message"] = "RKNN conversion timed out after 600 seconds"
    except Exception as e:
        result["message"] = f"RKNN conversion error: {str(e)}"

    return result


def compile_nbg_model(project_root: str, trap_type: str) -> Dict[str, Any]:
    """Compile ONNX model to NBG format for Vivante VIPLite NPU (Cubie A7S)"""
    result = {
        "component": "model",
        "platform": "cubie-a7s",
        "model_type": "nbg",
        "success": False,
        "message": "",
        "output_path": None
    }

    # Determine model path based on trap_type
    if trap_type == "detection":
        model_dir = os.path.join(project_root, "models", "detection", "insects", "yolo26n")
        convert_dir = os.path.join(model_dir, "convert")
        nbg_output_dir = os.path.join(model_dir, "nbg_output")
        gen_dir = os.path.join(nbg_output_dir, "generated")
        onnx_path = os.path.join(model_dir, "best.onnx")
        output_path = os.path.join(nbg_output_dir, "network_binary.nb")
        docker_script = os.path.join(convert_dir, "convert_nbg_docker.sh")
    else:
        result["message"] = f"Unknown trap_type '{trap_type}' for NBG compilation"
        return result

    # Validate inputs
    if not os.path.isfile(onnx_path):
        result["message"] = f"ONNX model not found: {onnx_path}"
        return result

    if not os.path.isfile(docker_script):
        result["message"] = f"Docker script not found: {docker_script}"
        return result

    try:
        # Step 1: Run ACUITY Toolkit in Docker (Steps A-D)
        # This runs on macOS and generates C source files + intermediate data
        docker_cmd = [
            "docker", "run", "--rm",
            "-v", f"{model_dir}:/models",
            "ubuntu-npu-with-acuity:latest",
            "bash", f"/models/convert/convert_nbg_docker.sh"
        ]

        proc = subprocess.run(docker_cmd, capture_output=True, text=True, timeout=600)

        # Check if NBG was generated directly (Step E succeeded on x86_64)
        if os.path.isfile(output_path) and os.path.getsize(output_path) > 0:
            result["success"] = True
            result["message"] = f"NBG model compiled via Docker: {output_path}"
            result["output_path"] = output_path
            return result

        # Step 2: If NBG wasn't packed (expected under Rosetta), try on Cubie A7S board
        cubie_host = PLATFORM_CONFIGS["cubie-a7s"]["default_host"]
        cubie_user = PLATFORM_CONFIGS["cubie-a7s"]["default_user"]

        # Check if board is reachable
        reachable = ssh_execute(cubie_user, cubie_host, "echo ok", timeout=10)
        if not reachable["success"]:
            result["message"] = (
                "NBG packing requires native ARM64. "
                f"Generated files are in {gen_dir}/. "
                f"To complete, copy to Cubie A7S and run gen_nbg there. "
                f"Docker output: {proc.stdout[-500:]}"
            )
            return result

        # Rsync generated files to board
        remote_dir = "/tmp/nbg"
        ssh_execute(cubie_user, cubie_host, f"mkdir -p {remote_dir}", timeout=10)
        rsync_cmd = [
            "rsync", "-avz", "--delete",
            f"{gen_dir}/",
            f"{cubie_user}@{cubie_host}:{remote_dir}/"
        ]
        subprocess.run(rsync_cmd, capture_output=True, text=True, timeout=60)

        # Run gen_nbg on the board
        build_cmd = f"cd {remote_dir} && make -f makefile.linux && ./gen_nbg nbg_output.export.data images_392_0.tensor"
        build_result = ssh_execute(cubie_user, cubie_host, build_cmd, timeout=120)

        if build_result["success"]:
            # Copy NBG binary back
            subprocess.run(
                ["scp", f"{cubie_user}@{cubie_host}:{remote_dir}/network_binary.nb", output_path],
                capture_output=True, timeout=30
            )
            # Cleanup
            ssh_execute(cubie_user, cubie_host, f"rm -rf {remote_dir}", timeout=10)

            if os.path.isfile(output_path) and os.path.getsize(output_path) > 0:
                result["success"] = True
                result["message"] = f"NBG model compiled on {cubie_host}: {output_path}"
                result["output_path"] = output_path
            else:
                result["message"] = f"NBG compilation on {cubie_host} completed but output not found"
        else:
            result["message"] = f"NBG gen_nbg failed on {cubie_host}:\n{build_result['combined'][-1000:]}"

    except subprocess.TimeoutExpired:
        result["message"] = "NBG conversion timed out after 600 seconds"
    except Exception as e:
        result["message"] = f"NBG conversion error: {str(e)}"

    return result


# ============================================================================
# Build Functions
# ============================================================================

def build_toolkit(platform: str, params: Dict[str, Any]) -> Dict[str, Any]:
    """Build the toolkit static library (libtoolkit.a) on the target board"""
    cfg = resolve_config(platform, params)
    if "error" in cfg:
        return cfg

    host = cfg["host"]
    user = cfg["user"]
    build_dir = cfg["build_dir"]
    remote_dir = cfg["remote_dir"]
    project_root = cfg["project_root"]
    rebuild = params.get("rebuild", False)

    result = {
        "component": "toolkit",
        "platform": platform,
        "success": False,
        "message": "",
        "binary_path": None,
        "run_command": None
    }

    # Rsync toolkit source to board
    rsync_exclude = [
        "--exclude", f"{build_dir}",
        "--exclude", ".git",
        "--exclude", "*.o",
        "--exclude", "captures/",
        "--exclude", "build-*",
    ]

    toolkit_src = os.path.join(project_root, "traps", "toolkit")
    remote_toolkit = f"{remote_dir}/traps/toolkit"

    rsync_cmd = ["rsync", "-avz", "--delete"] + rsync_exclude + [
        f"{toolkit_src}/",
        f"{user}@{host}:{remote_toolkit}/"
    ]

    try:
        subprocess.run(rsync_cmd, capture_output=True, text=True, timeout=60)
    except Exception as e:
        result["message"] = f"rsync failed: {str(e)}"
        return result

    # Build toolkit on the board
    if rebuild:
        setup_cmd = f"cd {remote_toolkit} && rm -rf {build_dir} && meson setup {build_dir} -Dplatform={platform}"
    else:
        setup_cmd = f"cd {remote_toolkit} && (test -d {build_dir} || meson setup {build_dir} -Dplatform={platform})"

    setup_result = ssh_execute(user, host, setup_cmd, timeout=60)
    if not setup_result["success"]:
        result["message"] = f"meson setup failed:\n{setup_result['combined'][-1000:]}"
        return result

    compile_cmd = f"cd {remote_toolkit} && meson compile -C {build_dir}"
    compile_result = ssh_execute(user, host, compile_cmd, timeout=300)

    if compile_result["success"]:
        result["success"] = True
        result["message"] = f"Toolkit built successfully for {platform}"
    else:
        parsed = CompilerErrorParser.parse_output(compile_result["combined"])
        result["message"] = f"Toolkit build failed for {platform}"
        result["errors"] = parsed

    return result


def build_target(platform: str, params: Dict[str, Any]) -> Dict[str, Any]:
    """Build the trap target binary on the target board"""
    cfg = resolve_config(platform, params)
    if "error" in cfg:
        return cfg

    host = cfg["host"]
    user = cfg["user"]
    build_dir = cfg["build_dir"]
    remote_dir = cfg["remote_dir"]
    project_root = cfg["project_root"]
    trap_name = cfg["trap_name"]
    trap_type = params.get("trap_type", "detection")
    rebuild = params.get("rebuild", False)

    result = {
        "component": "target",
        "platform": platform,
        "trap_type": trap_type,
        "success": False,
        "message": "",
        "binary_path": None,
        "run_command": None
    }

    # Rsync targets source to board
    rsync_exclude = [
        "--exclude", f"{build_dir}",
        "--exclude", ".git",
        "--exclude", "*.o",
        "--exclude", "captures/",
        "--exclude", "build-*",
    ]

    targets_src = os.path.join(project_root, "traps", "targets")
    remote_targets = f"{remote_dir}/traps/targets"

    rsync_cmd = ["rsync", "-avz", "--delete"] + rsync_exclude + [
        f"{targets_src}/",
        f"{user}@{host}:{remote_targets}/"
    ]

    try:
        subprocess.run(rsync_cmd, capture_output=True, text=True, timeout=60)
    except Exception as e:
        result["message"] = f"rsync failed: {str(e)}"
        return result

    # Build targets on the board
    toolkit_build_dir = f"{remote_dir}/traps/toolkit/{build_dir}"

    if rebuild:
        setup_cmd = (
            f"cd {remote_targets} && rm -rf {build_dir} && "
            f"meson setup {build_dir} -Dplatform={platform} "
            f"-Dtoolkit_build_dir={toolkit_build_dir}"
        )
    else:
        setup_cmd = (
            f"cd {remote_targets} && (test -d {build_dir} || "
            f"meson setup {build_dir} -Dplatform={platform} "
            f"-Dtoolkit_build_dir={toolkit_build_dir})"
        )

    setup_result = ssh_execute(user, host, setup_cmd, timeout=60)
    if not setup_result["success"]:
        result["message"] = f"meson setup failed:\n{setup_result['combined'][-1000:]}"
        return result

    compile_cmd = f"cd {remote_targets} && meson compile -C {build_dir}"
    compile_result = ssh_execute(user, host, compile_cmd, timeout=300)

    if compile_result["success"]:
        binary_path = get_binary_path(remote_dir, build_dir, cfg["trap_path_template"], trap_name)
        run_cmd = get_run_command(user, host, binary_path, sudo=True)
        result["success"] = True
        result["message"] = f"Target built successfully for {platform}/{trap_type}"
        result["binary_path"] = binary_path
        result["run_command"] = run_cmd
    else:
        parsed = CompilerErrorParser.parse_output(compile_result["combined"])
        result["message"] = f"Target build failed for {platform}/{trap_type}"
        result["errors"] = parsed

    return result


# ============================================================================
# Tool Call Handler
# ============================================================================

@server.call_tool()
async def handle_call_tool(name: str, arguments: dict | None) -> list[TextContent]:
    """Execute the requested tool"""
    args = arguments or {}

    try:
        # ── build ──
        if name == "build":
            platform = args.get("platform")
            if not platform:
                return [TextContent(type="text", text="Error: 'platform' parameter is required")]

            component = args.get("component", "target")
            trap_type = args.get("trap_type", "detection")
            project_root = args.get("project_root", str(Path.home() / "naturesense" / "ai-traps"))

            results = []

            if component == "model":
                # Compile model for the platform
                if platform == "rock3c":
                    result = compile_rknn_model(project_root, trap_type)
                elif platform == "cubie-a7s":
                    result = compile_nbg_model(project_root, trap_type)
                else:
                    result = {
                        "component": "model",
                        "platform": platform,
                        "success": False,
                        "message": f"Model compilation not supported for platform '{platform}'"
                    }
                results.append(result)

            elif component == "toolkit":
                result = build_toolkit(platform, args)
                results.append(result)

            elif component == "target":
                result = build_target(platform, args)
                results.append(result)

            elif component == "all":
                # Model → Toolkit → Target
                if platform == "rock3c":
                    model_result = compile_rknn_model(project_root, trap_type)
                elif platform == "cubie-a7s":
                    model_result = compile_nbg_model(project_root, trap_type)
                else:
                    model_result = {
                        "component": "model",
                        "platform": platform,
                        "success": True,
                        "message": f"No model compilation needed for {platform}"
                    }
                results.append(model_result)

                if model_result.get("success", False):
                    toolkit_result = build_toolkit(platform, args)
                    results.append(toolkit_result)

                    if toolkit_result.get("success", False):
                        target_result = build_target(platform, args)
                        results.append(target_result)
                    else:
                        results.append({
                            "component": "target",
                            "platform": platform,
                            "success": False,
                            "message": "Skipped: toolkit build failed"
                        })
                else:
                    results.append({
                        "component": "toolkit",
                        "platform": platform,
                        "success": False,
                        "message": "Skipped: model compilation failed"
                    })
                    results.append({
                        "component": "target",
                        "platform": platform,
                        "success": False,
                        "message": "Skipped: model compilation failed"
                    })

            return [TextContent(type="text", text=json.dumps(results, indent=2))]

        # ── run ──
        elif name == "run":
            platform = args.get("platform")
            if not platform:
                return [TextContent(type="text", text="Error: 'platform' parameter is required")]

            cfg = resolve_config(platform, args)
            if "error" in cfg:
                return [TextContent(type="text", text=json.dumps(cfg, indent=2))]

            host = cfg["host"]
            user = cfg["user"]
            trap_name = cfg["trap_name"]
            binary_path = get_binary_path(cfg["remote_dir"], cfg["build_dir"], cfg["trap_path_template"], trap_name)
            sudo = args.get("sudo", True)
            cli_args = args.get("args", "")
            timeout = min(args.get("timeout", 60), 300)
            background = args.get("background", False)

            if background:
                pid_file = f"/tmp/ai-trap-{trap_name}.pid"
                run_cmd = f"{'sudo ' if sudo else ''}{binary_path} {cli_args}"
                bg_result = ssh_execute_background(user, host, run_cmd, pid_file)
                return [TextContent(type="text", text=json.dumps(bg_result, indent=2))]
            else:
                run_cmd = f"{'sudo ' if sudo else ''}{binary_path} {cli_args}"
                ssh_result = ssh_execute(user, host, run_cmd, timeout=timeout)
                crash_info = CrashParser.parse_gdb_output(ssh_result["combined"]) if not ssh_result["success"] else None
                response = {
                    "success": ssh_result["success"],
                    "returncode": ssh_result["returncode"],
                    "stdout": ssh_result["stdout"][-5000:],
                    "stderr": ssh_result["stderr"][-2000:],
                }
                if crash_info and crash_info["crashed"]:
                    response["crash_info"] = crash_info
                return [TextContent(type="text", text=json.dumps(response, indent=2))]

        # ── stop ──
        elif name == "stop":
            platform = args.get("platform")
            if not platform:
                return [TextContent(type="text", text="Error: 'platform' parameter is required")]

            cfg = resolve_config(platform, args)
            if "error" in cfg:
                return [TextContent(type="text", text=json.dumps(cfg, indent=2))]

            host = cfg["host"]
            user = cfg["user"]
            trap_name = cfg["trap_name"]
            signal_type = args.get("signal", "TERM")
            sig_num = signal.SIGTERM if signal_type == "TERM" else signal.SIGKILL
            sig_name = signal_type

            pid_file = f"/tmp/ai-trap-{trap_name}.pid"
            read_pid = f"cat {pid_file} 2>/dev/null || echo ''"
            pid_result = ssh_execute(user, host, read_pid, timeout=10)
            pid = pid_result["stdout"].strip()

            if pid and pid.isdigit():
                kill_cmd = f"kill -{sig_name} {pid} && rm -f {pid_file}"
                kill_result = ssh_execute(user, host, kill_cmd, timeout=10)
                return [TextContent(type="text", text=json.dumps({
                    "success": kill_result["success"],
                    "pid": int(pid),
                    "signal": sig_name,
                    "message": f"Sent {sig_name} to PID {pid}" if kill_result["success"] else f"Failed to stop PID {pid}"
                }, indent=2))]
            else:
                # Fallback: try pkill
                pkill_cmd = f"pkill -f {trap_name} 2>/dev/null; echo $?"
                pkill_result = ssh_execute(user, host, pkill_cmd, timeout=10)
                return [TextContent(type="text", text=json.dumps({
                    "success": pkill_result["success"],
                    "message": f"Attempted pkill for '{trap_name}'"
                }, indent=2))]

        # ── status ──
        elif name == "status":
            platform = args.get("platform")
            if not platform:
                return [TextContent(type="text", text="Error: 'platform' parameter is required")]

            cfg = resolve_config(platform, args)
            if "error" in cfg:
                return [TextContent(type="text", text=json.dumps(cfg, indent=2))]

            host = cfg["host"]
            user = cfg["user"]
            trap_name = cfg["trap_name"]

            pid_file = f"/tmp/ai-trap-{trap_name}.pid"
            read_pid = f"cat {pid_file} 2>/dev/null || echo ''"
            pid_result = ssh_execute(user, host, read_pid, timeout=10)
            pid = pid_result["stdout"].strip()

            if pid and pid.isdigit():
                ps_cmd = f"ps -p {pid} -o pid,pcpu,pmem,etime,args --no-headers 2>/dev/null || echo 'not running'"
                ps_result = ssh_execute(user, host, ps_cmd, timeout=10)
                if ps_result["success"] and "not running" not in ps_result["stdout"]:
                    return [TextContent(type="text", text=json.dumps({
                        "running": True,
                        "pid": int(pid),
                        "process_info": ps_result["stdout"].strip()
                    }, indent=2))]
                else:
                    # PID file stale, clean it up
                    ssh_execute(user, host, f"rm -f {pid_file}", timeout=5)
                    return [TextContent(type="text", text=json.dumps({
                        "running": False,
                        "message": f"No process found with PID {pid} (stale PID file cleaned up)"
                    }, indent=2))]
            else:
                return [TextContent(type="text", text=json.dumps({
                    "running": False,
                    "message": f"No PID file found for {trap_name}"
                }, indent=2))]

        # ── logs ──
        elif name == "logs":
            platform = args.get("platform")
            if not platform:
                return [TextContent(type="text", text="Error: 'platform' parameter is required")]

            cfg = resolve_config(platform, args)
            if "error" in cfg:
                return [TextContent(type="text", text=json.dumps(cfg, indent=2))]

            host = cfg["host"]
            user = cfg["user"]
            trap_name = cfg["trap_name"]
            lines = min(args.get("lines", 50), 500)
            source = args.get("source", "journalctl")

            if source == "journalctl":
                log_cmd = f"journalctl -u {trap_name} -n {lines} --no-pager 2>/dev/null || journalctl -n {lines} --no-pager 2>/dev/null | tail -{lines}"
            elif source == "file":
                log_path = args.get("log_path", f"/var/log/{trap_name}.log")
                log_cmd = f"tail -{lines} {log_path} 2>/dev/null || echo 'Log file not found: {log_path}'"
            else:
                return [TextContent(type="text", text=f"Error: Unknown log source '{source}'")]

            log_result = ssh_execute(user, host, log_cmd, timeout=15)
            return [TextContent(type="text", text=json.dumps({
                "success": log_result["success"],
                "source": source,
                "lines": log_result["stdout"].strip()
            }, indent=2))]

        # ── debug ──
        elif name == "debug":
            platform = args.get("platform")
            if not platform:
                return [TextContent(type="text", text="Error: 'platform' parameter is required")]

            cfg = resolve_config(platform, args)
            if "error" in cfg:
                return [TextContent(type="text", text=json.dumps(cfg, indent=2))]

            host = cfg["host"]
            user = cfg["user"]
            trap_name = cfg["trap_name"]
            binary_path = get_binary_path(cfg["remote_dir"], cfg["build_dir"], cfg["trap_path_template"], trap_name)
            cli_args = args.get("args", "")
            timeout = min(args.get("timeout", 30), 120)

            gdb_cmd = (
                f"sudo gdb -batch -ex run -ex 'bt full' -ex 'info registers' "
                f"--args {binary_path} {cli_args}"
            )
            gdb_result = ssh_execute(user, host, gdb_cmd, timeout=timeout)
            crash_info = CrashParser.parse_gdb_output(gdb_result["combined"])

            return [TextContent(type="text", text=json.dumps({
                "success": gdb_result["success"],
                "crash_info": crash_info,
                "gdb_output": gdb_result["combined"][-5000:]
            }, indent=2))]

        # ── list_binaries ──
        elif name == "list_binaries":
            platform = args.get("platform")
            if not platform:
                return [TextContent(type="text", text="Error: 'platform' parameter is required")]

            cfg = resolve_config(platform, args)
            if "error" in cfg:
                return [TextContent(type="text", text=json.dumps(cfg, indent=2))]

            host = cfg["host"]
            user = cfg["user"]
            remote_dir = cfg["remote_dir"]
            build_dir = cfg["build_dir"]

            ls_cmd = f"find {remote_dir}/{build_dir} -type f -executable -exec ls -lh {{}} \\; 2>/dev/null || echo 'No binaries found'"
            ls_result = ssh_execute(user, host, ls_cmd, timeout=15)
            return [TextContent(type="text", text=json.dumps({
                "success": ls_result["success"],
                "binaries": ls_result["stdout"].strip()
            }, indent=2))]

        else:
            return [TextContent(type="text", text=f"Error: Unknown tool '{name}'")]

    except Exception as e:
        return [TextContent(type="text", text=json.dumps({
            "error": str(e),
            "type": type(e).__name__
        }, indent=2))]


# ============================================================================
# Main Entry Point
# ============================================================================

async def main():
    async with stdio_server() as (read_stream, write_stream):
        await server.run(
            read_stream,
            write_stream,
            server.create_initialization_options()
        )


if __name__ == "__main__":
    anyio.run(main)

