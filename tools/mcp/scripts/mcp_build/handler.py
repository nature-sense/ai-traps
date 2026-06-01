"""
MCP tool call handler.

Routes incoming tool calls to the Build Server REST API on target boards.
All tools communicate via HTTP to the build server running on port 8081.
"""

import json
from typing import Dict, Any

from mcp.types import TextContent

from mcp_build import server
from mcp_build.remote import BuildServerClient
from mcp_build.config import PLATFORM_CONFIGS


def _get_host(args: Dict[str, Any]) -> str:
    """Resolve host from args or platform default."""
    host = args.get("host")
    if host:
        return host
    platform = args.get("platform", "rock3c")
    config = PLATFORM_CONFIGS.get(platform, {})
    return config.get("default_host", f"{platform}.local")


def _get_client(args: Dict[str, Any]) -> BuildServerClient:
    """Create a BuildServerClient from tool arguments."""
    host = _get_host(args)
    return BuildServerClient(host=host)


@server.call_tool()
async def handle_call_tool(name: str, arguments: dict | None) -> list[TextContent]:
    """Execute the requested tool via the Build Server REST API."""
    args = arguments or {}

    try:
        client = _get_client(args)
        platform = args.get("platform", "rock3c")

        # ── Environment Tools ──
        if name == "check_environment":
            result = client.check_environment(platform=platform)

        elif name == "setup_environment":
            components = args.get("components", ["build", "runtime"])
            result = client.setup_environment(components=components, platform=platform)

        # ── Build Tools ──
        elif name == "build":
            project_root = args.get("project_root", None)
            if not project_root:
                # Auto-detect from the MCP server's working directory
                import os
                project_root = os.getcwd()

            # Normalize boolean parameters that might come as strings
            rebuild = args.get("rebuild", False)
            if isinstance(rebuild, str):
                rebuild = rebuild.lower() == "true"
            changed_files_only = args.get("changed_files_only", True)
            if isinstance(changed_files_only, str):
                changed_files_only = changed_files_only.lower() == "true"

            result = client.build(
                source_dir=project_root,
                platform=platform,
                component=args.get("component", "all"),
                trap_type=args.get("trap_type", "detection"),
                rebuild=rebuild,
                changed_files_only=changed_files_only,
            )

        elif name == "get_build_status":
            build_id = args.get("build_id")
            if not build_id:
                return [TextContent(type="text", text=json.dumps({
                    "success": False,
                    "message": "Error: 'build_id' parameter is required"
                }, indent=2))]
            result = client.get_build_status(build_id=build_id, platform=platform)

        elif name == "get_build_log":
            build_id = args.get("build_id")
            if not build_id:
                return [TextContent(type="text", text=json.dumps({
                    "success": False,
                    "message": "Error: 'build_id' parameter is required"
                }, indent=2))]
            lines = min(args.get("lines", 500), 2000)
            result = client.get_build_log(build_id=build_id, platform=platform, lines=lines)

        # ── Runtime Tool ──
        elif name == "create_runtime":
            build_id = args.get("build_id")
            if not build_id:
                return [TextContent(type="text", text=json.dumps({
                    "success": False,
                    "message": "Error: 'build_id' parameter is required"
                }, indent=2))]
            result = client.create_runtime(
                build_id=build_id,
                platform=platform,
                start_trap=args.get("start_trap", True),
            )

        # ── Trap Lifecycle Tools ──
        elif name == "start_trap":
            binary_path = args.get("binary_path")
            config_path = args.get("config_path")
            if not binary_path or not config_path:
                return [TextContent(type="text", text=json.dumps({
                    "success": False,
                    "message": "Error: 'binary_path' and 'config_path' parameters are required"
                }, indent=2))]
            result = client.start_trap(
                binary_path=binary_path,
                config_path=config_path,
                args=args.get("args", ""),
                platform=platform,
            )

        elif name == "stop_trap":
            result = client.stop_trap(
                signal=args.get("signal", "TERM"),
                platform=platform,
            )

        elif name == "get_trap_status":
            result = client.get_trap_status(platform=platform)

        elif name == "get_trap_log":
            lines = min(args.get("lines", 100), 1000)
            source = args.get("source", "file")
            result = client.get_trap_log(platform=platform, lines=lines, source=source)

        else:
            return [TextContent(type="text", text=json.dumps({
                "success": False,
                "message": f"Unknown tool: {name}"
            }, indent=2))]

        return [TextContent(type="text", text=json.dumps(result, indent=2, default=str))]

    except ValueError as e:
        return [TextContent(type="text", text=json.dumps({
            "success": False,
            "message": str(e)
        }, indent=2))]
    except Exception as e:
        return [TextContent(type="text", text=json.dumps({
            "success": False,
            "message": f"Error executing {name}: {str(e)}"
        }, indent=2))]
