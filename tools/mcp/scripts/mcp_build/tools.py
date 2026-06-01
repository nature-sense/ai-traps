"""
MCP tool definitions.

Defines the schemas for all tools exposed by the ai-trap-build MCP server.
All tools communicate with the Build Server REST API running on target boards.
"""

from mcp.types import Tool

from mcp_build import server


@server.list_tools()
async def list_tools() -> list[Tool]:
    """Return the list of available tools"""
    return [
        # ── Environment Tools ──
        Tool(
            name="check_environment",
            description="Check all dependencies on a target board (build tools, runtime deps, model tools).",
            inputSchema={
                "type": "object",
                "properties": {
                    "host": {
                        "type": "string",
                        "description": "Board hostname (e.g., 'rock-3c.local'). Defaults to per-platform default.",
                    },
                    "platform": {
                        "type": "string",
                        "description": "Target platform: rock3c, cubie-a7s, or rdk-x5",
                        "enum": ["rock3c", "cubie-a7s", "rdk-x5"],
                        "default": "rock3c"
                    },
                },
                "required": []
            }
        ),
        Tool(
            name="setup_environment",
            description="Install missing dependencies on a target board.",
            inputSchema={
                "type": "object",
                "properties": {
                    "host": {
                        "type": "string",
                        "description": "Board hostname (e.g., 'rock-3c.local'). Defaults to per-platform default.",
                    },
                    "platform": {
                        "type": "string",
                        "description": "Target platform: rock3c, cubie-a7s, or rdk-x5",
                        "enum": ["rock3c", "cubie-a7s", "rdk-x5"],
                        "default": "rock3c"
                    },
                    "components": {
                        "type": "array",
                        "items": {"type": "string", "enum": ["build", "runtime", "model"]},
                        "description": "Which dependency groups to install: 'build', 'runtime', 'model'",
                        "default": ["build", "runtime"]
                    },
                },
                "required": []
            }
        ),

        # ── Build Tool ──
        Tool(
            name="build",
            description="Build AI Camera Trap components on a target board. Creates a git bundle of committed source files and sends it to the build server for a meson build. Uncommitted changes must be committed before building.",
            inputSchema={
                "type": "object",
                "properties": {
                    "host": {
                        "type": "string",
                        "description": "Board hostname (e.g., 'rock-3c.local'). Defaults to per-platform default.",
                    },
                    "platform": {
                        "type": "string",
                        "description": "Target platform: rock3c, cubie-a7s, or rdk-x5",
                        "enum": ["rock3c", "cubie-a7s", "rdk-x5"],
                        "default": "rock3c"
                    },
                    "component": {
                        "type": "string",
                        "description": "What to build: 'model' (compile ONNX→RKNN/NBG), 'toolkit' (libtoolkit.a), 'target' (trap binary), or 'all' (model→toolkit→target)",
                        "enum": ["model", "toolkit", "target", "all"],
                        "default": "all"
                    },
                    "trap_type": {
                        "type": "string",
                        "description": "Trap type (e.g., 'detection', 'classification'). Determines which model to compile.",
                        "default": "detection"
                    },
                    "rebuild": {
                        "type": "boolean",
                        "description": "If true, performs a full rebuild from scratch",
                        "default": False
                    },
                    "changed_files_only": {
                        "type": "boolean",
                        "description": "If true, only send files changed since last commit (faster). If false, send entire project.",
                        "default": True
                    },
                },
                "required": []
            }
        ),
        Tool(
            name="get_build_status",
            description="Get the status of a build on a target board.",
            inputSchema={
                "type": "object",
                "properties": {
                    "host": {
                        "type": "string",
                        "description": "Board hostname (e.g., 'rock-3c.local'). Defaults to per-platform default.",
                    },
                    "build_id": {
                        "type": "string",
                        "description": "Build ID returned from the 'build' tool",
                    },
                    "platform": {
                        "type": "string",
                        "description": "Target platform: rock3c, cubie-a7s, or rdk-x5",
                        "enum": ["rock3c", "cubie-a7s", "rdk-x5"],
                        "default": "rock3c"
                    },
                },
                "required": ["build_id"]
            }
        ),
        Tool(
            name="get_build_log",
            description="Get the build log output from a target board.",
            inputSchema={
                "type": "object",
                "properties": {
                    "host": {
                        "type": "string",
                        "description": "Board hostname (e.g., 'rock-3c.local'). Defaults to per-platform default.",
                    },
                    "build_id": {
                        "type": "string",
                        "description": "Build ID returned from the 'build' tool",
                    },
                    "platform": {
                        "type": "string",
                        "description": "Target platform: rock3c, cubie-a7s, or rdk-x5",
                        "enum": ["rock3c", "cubie-a7s", "rdk-x5"],
                        "default": "rock3c"
                    },
                    "lines": {
                        "type": "number",
                        "description": "Number of recent log lines to fetch (default: 500, max: 2000)",
                        "default": 500
                    },
                },
                "required": ["build_id"]
            }
        ),

        # ── Runtime Tool ──
        Tool(
            name="create_runtime",
            description="Create a runtime from a completed build: install binary, deploy model/config, and optionally start the trap.",
            inputSchema={
                "type": "object",
                "properties": {
                    "host": {
                        "type": "string",
                        "description": "Board hostname (e.g., 'rock-3c.local'). Defaults to per-platform default.",
                    },
                    "build_id": {
                        "type": "string",
                        "description": "Build ID from a successful build",
                    },
                    "platform": {
                        "type": "string",
                        "description": "Target platform: rock3c, cubie-a7s, or rdk-x5",
                        "enum": ["rock3c", "cubie-a7s", "rdk-x5"],
                        "default": "rock3c"
                    },
                    "start_trap": {
                        "type": "boolean",
                        "description": "Whether to start the trap after creating the runtime",
                        "default": True
                    },
                },
                "required": ["build_id"]
            }
        ),

        # ── Trap Lifecycle Tools ──
        Tool(
            name="start_trap",
            description="Start the trap binary on a target board via the build server.",
            inputSchema={
                "type": "object",
                "properties": {
                    "host": {
                        "type": "string",
                        "description": "Board hostname (e.g., 'rock-3c.local'). Defaults to per-platform default.",
                    },
                    "platform": {
                        "type": "string",
                        "description": "Target platform: rock3c, cubie-a7s, or rdk-x5",
                        "enum": ["rock3c", "cubie-a7s", "rdk-x5"],
                        "default": "rock3c"
                    },
                    "binary_path": {
                        "type": "string",
                        "description": "Path to the trap binary on the board (e.g., '/usr/local/bin/rock3c-imx219')",
                    },
                    "config_path": {
                        "type": "string",
                        "description": "Path to the YAML config file on the board (e.g., '/etc/ai-trap/config.yaml')",
                    },
                    "args": {
                        "type": "string",
                        "description": "Additional CLI arguments to pass to the binary",
                        "default": ""
                    },
                },
                "required": ["binary_path", "config_path"]
            }
        ),
        Tool(
            name="stop_trap",
            description="Stop the trap binary on a target board via the build server.",
            inputSchema={
                "type": "object",
                "properties": {
                    "host": {
                        "type": "string",
                        "description": "Board hostname (e.g., 'rock-3c.local'). Defaults to per-platform default.",
                    },
                    "platform": {
                        "type": "string",
                        "description": "Target platform: rock3c, cubie-a7s, or rdk-x5",
                        "enum": ["rock3c", "cubie-a7s", "rdk-x5"],
                        "default": "rock3c"
                    },
                    "signal": {
                        "type": "string",
                        "description": "Signal to send: TERM (graceful) or KILL (force)",
                        "default": "TERM"
                    },
                },
                "required": []
            }
        ),
        Tool(
            name="get_trap_status",
            description="Get the status of the running trap on a target board (PID, CPU, memory, uptime).",
            inputSchema={
                "type": "object",
                "properties": {
                    "host": {
                        "type": "string",
                        "description": "Board hostname (e.g., 'rock-3c.local'). Defaults to per-platform default.",
                    },
                    "platform": {
                        "type": "string",
                        "description": "Target platform: rock3c, cubie-a7s, or rdk-x5",
                        "enum": ["rock3c", "cubie-a7s", "rdk-x5"],
                        "default": "rock3c"
                    },
                },
                "required": []
            }
        ),
        Tool(
            name="get_trap_log",
            description="Fetch recent log output from the trap on a target board.",
            inputSchema={
                "type": "object",
                "properties": {
                    "host": {
                        "type": "string",
                        "description": "Board hostname (e.g., 'rock-3c.local'). Defaults to per-platform default.",
                    },
                    "platform": {
                        "type": "string",
                        "description": "Target platform: rock3c, cubie-a7s, or rdk-x5",
                        "enum": ["rock3c", "cubie-a7s", "rdk-x5"],
                        "default": "rock3c"
                    },
                    "lines": {
                        "type": "number",
                        "description": "Number of recent log lines to fetch (default: 100, max: 1000)",
                        "default": 100
                    },
                    "source": {
                        "type": "string",
                        "description": "Log source: 'file' (log file) or 'journalctl' (systemd journal)",
                        "default": "file"
                    },
                },
                "required": []
            }
        ),
    ]
