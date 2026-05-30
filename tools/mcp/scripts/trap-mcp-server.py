#!/usr/bin/env python3

# Copyright 2026 Nature Sense
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


"""
MCP Server for NatureSense AI Camera Trap REST API.
Uses mDNS hostnames (e.g., rock-3c.local) to address traps.

Usage:
  This script is run by Cline as an MCP server via STDIN/STDOUT JSON-RPC.
  It is NOT meant to be run directly from the terminal.

Installation:
  cp scripts/trap-mcp-server.py /Users/steve/.local/bin/
  chmod +x /Users/steve/.local/bin/trap-mcp-server.py

Dependencies:
  pip install mcp httpx

MCP Config (cline_mcp_settings.json):
  {
    "mcpServers": {
      "trap-ops": {
        "command": "python3",
        "args": ["/Users/steve/.local/bin/trap-mcp-server.py"],
        "disabled": false,
        "autoApprove": [
          "get_trap_status",
          "list_sessions",
          "get_active_session",
          "get_session",
          "list_detections",
          "get_detection",
          "get_crop_image_url",
          "get_system_metrics",
          "get_server_status",
          "get_recent_detections",
          "get_stream_urls",
          "discover_traps"
        ]
      }
    }
  }
"""

import json
import subprocess
import httpx
from mcp.server import Server
from mcp.server.models import InitializationOptions
import mcp.server.stdio
import mcp.types as types

server = Server("naturesense-trap-mcp")


async def _request(trap_host: str, method: str, path: str, json_data: dict = None) -> dict:
    """
    Make HTTP request to a specific trap using its mDNS hostname.
    trap_host example: "rock-3c.local" or "trap-singapore-01.local"
    """
    # Python's httpx and the system resolver handle .local mDNS automatically
    base_url = f"http://{trap_host}:8080"
    async with httpx.AsyncClient(base_url=base_url, timeout=30.0) as client:
        resp = await client.request(method, path, json=json_data)
        resp.raise_for_status()
        return resp.json() if resp.content else {}


@server.list_tools()
async def handle_list_tools() -> list[types.Tool]:
    """Register all MCP tools with trapHost parameter."""

    # Common properties for all tools (trapHost is always required)
    base_properties = {
        "trapHost": {
            "type": "string",
            "description": "mDNS hostname of the target trap (e.g., 'rock-3c.local' or 'trap-singapore-01.local')"
        }
    }

    return [
        # ----- Read-only tools (safe for auto-approve) -----
        types.Tool(
            name="get_trap_status",
            description="Get current status of a provisioned trap",
            inputSchema={
                "type": "object",
                "properties": {
                    **base_properties,
                    "trapId": {"type": "string", "description": "Trap identifier"}
                },
                "required": ["trapHost", "trapId"]
            }
        ),
        types.Tool(
            name="list_sessions",
            description="List all monitoring sessions for a trap (most recent first)",
            inputSchema={
                "type": "object",
                "properties": {
                    **base_properties,
                    "trapId": {"type": "string"},
                    "limit": {"type": "integer", "default": 50},
                    "offset": {"type": "integer", "default": 0}
                },
                "required": ["trapHost", "trapId"]
            }
        ),
        types.Tool(
            name="get_active_session",
            description="Get the currently active monitoring session (if any)",
            inputSchema={
                "type": "object",
                "properties": {
                    **base_properties,
                    "trapId": {"type": "string"}
                },
                "required": ["trapHost", "trapId"]
            }
        ),
        types.Tool(
            name="get_session",
            description="Get detailed information about a specific session",
            inputSchema={
                "type": "object",
                "properties": {
                    **base_properties,
                    "trapId": {"type": "string"},
                    "sessionId": {"type": "integer"}
                },
                "required": ["trapHost", "trapId", "sessionId"]
            }
        ),
        types.Tool(
            name="list_detections",
            description="List all detections (crops) from a session",
            inputSchema={
                "type": "object",
                "properties": {
                    **base_properties,
                    "trapId": {"type": "string"},
                    "sessionId": {"type": "integer"},
                    "limit": {"type": "integer", "default": 50},
                    "offset": {"type": "integer", "default": 0}
                },
                "required": ["trapHost", "trapId", "sessionId"]
            }
        ),
        types.Tool(
            name="get_detection",
            description="Get a single detection by ID",
            inputSchema={
                "type": "object",
                "properties": {
                    **base_properties,
                    "trapId": {"type": "string"},
                    "detectionId": {"type": "integer"}
                },
                "required": ["trapHost", "trapId", "detectionId"]
            }
        ),
        types.Tool(
            name="get_crop_image_url",
            description="Get the URL for a crop image (returns URL, not the image binary)",
            inputSchema={
                "type": "object",
                "properties": {
                    **base_properties,
                    "date": {"type": "string", "pattern": "^\\d{4}-\\d{2}-\\d{2}$"},
                    "filename": {"type": "string", "pattern": "^\\d+_\\d+\\.jpg$"}
                },
                "required": ["trapHost", "date", "filename"]
            }
        ),
        types.Tool(
            name="get_system_metrics",
            description="Get real-time system metrics (CPU, memory, NPU, pipeline FPS, power)",
            inputSchema={
                "type": "object",
                "properties": {
                    **base_properties,
                    "trapId": {"type": "string"}
                },
                "required": ["trapHost", "trapId"]
            }
        ),
        types.Tool(
            name="get_server_status",
            description="Get basic server status (runs on trap device)",
            inputSchema={
                "type": "object",
                "properties": base_properties,
                "required": ["trapHost"]
            }
        ),
        types.Tool(
            name="get_recent_detections",
            description="Poll for detections in the last N minutes (alternative to SSE)",
            inputSchema={
                "type": "object",
                "properties": {
                    **base_properties,
                    "trapId": {"type": "string"},
                    "minutes": {"type": "integer", "default": 5}
                },
                "required": ["trapHost", "trapId"]
            }
        ),
        types.Tool(
            name="get_stream_urls",
            description="Get the MJPEG stream URLs for viewing in a browser",
            inputSchema={
                "type": "object",
                "properties": base_properties,
                "required": ["trapHost"]
            }
        ),

        # ----- State-changing tools (require manual approval) -----
        types.Tool(
            name="start_monitoring",
            description="Start a new monitoring session (inference, tracking, cropping, storage begin)",
            inputSchema={
                "type": "object",
                "properties": {
                    **base_properties,
                    "trapId": {"type": "string"}
                },
                "required": ["trapHost", "trapId"]
            }
        ),
        types.Tool(
            name="stop_monitoring",
            description="Stop an active monitoring session (inference/tracking/cropping/storage stop, MJPEG stays alive)",
            inputSchema={
                "type": "object",
                "properties": {
                    **base_properties,
                    "trapId": {"type": "string"},
                    "sessionId": {"type": "integer"}
                },
                "required": ["trapHost", "trapId", "sessionId"]
            }
        ),

        # ----- Destructive tool (never auto-approve) -----
        types.Tool(
            name="provision_trap",
            description="ONE-TIME OPERATION: Provision a new trap with a unique ID. Use only during initial setup.",
            inputSchema={
                "type": "object",
                "properties": {
                    **base_properties,
                    "trapId": {"type": "string", "description": "Unique serial number"}
                },
                "required": ["trapHost", "trapId"]
            }
        ),

        # ----- Discovery tool (optional) -----
        types.Tool(
            name="discover_traps",
            description="Discover available traps on the local network via mDNS (avahi)",
            inputSchema={
                "type": "object",
                "properties": {},
                "required": []
            }
        ),
    ]


@server.call_tool()
async def handle_call_tool(
    name: str,
    arguments: dict | None
) -> list[types.TextContent]:
    """Execute the requested tool."""
    args = arguments or {}

    # Special case: discover_traps doesn't need trapHost
    if name == "discover_traps":
        try:
            # Run avahi-browse to find _http._tcp services
            result = subprocess.run(
                ["avahi-browse", "-rp", "_http._tcp"],
                capture_output=True,
                text=True,
                timeout=10
            )
            # Parse output to extract hostnames
            hosts = set()
            for line in result.stdout.splitlines():
                if line.startswith("="):
                    parts = line.split(";")
                    if len(parts) >= 8:
                        hostname = parts[6]  # hostname field
                        if hostname.endswith(".local"):
                            hosts.add(hostname)
            return [types.TextContent(
                type="text",
                text=json.dumps({"discovered_traps": list(hosts)}, indent=2)
            )]
        except FileNotFoundError:
            return [types.TextContent(
                type="text",
                text=json.dumps({
                    "error": "avahi-browse not found",
                    "message": "Install avahi-utils on Linux, or use macOS built-in mDNS. On macOS, try: dns-sd -B _http._tcp local"
                }, indent=2)
            )]
        except Exception as e:
            return [types.TextContent(
                type="text",
                text=json.dumps({
                    "error": f"Error discovering traps: {str(e)}",
                    "message": "Ensure avahi-daemon is running on Linux, or mDNS is enabled on macOS."
                }, indent=2)
            )]

    # All other tools require trapHost
    trap_host = args.get("trapHost")
    if not trap_host:
        return [types.TextContent(
            type="text",
            text="Error: 'trapHost' parameter is required for this tool (e.g., 'rock-3c.local')"
        )]

    try:
        # ----- Read-only endpoints -----
        if name == "get_trap_status":
            result = await _request(trap_host, "GET", f"/v1/traps/{args['trapId']}")

        elif name == "list_sessions":
            params = {k: v for k, v in args.items() if k not in ["trapHost", "trapId"]}
            query = "&".join([f"{k}={v}" for k, v in params.items()]) if params else ""
            path = f"/v1/traps/{args['trapId']}/sessions"
            if query:
                path += f"?{query}"
            result = await _request(trap_host, "GET", path)

        elif name == "get_active_session":
            result = await _request(trap_host, "GET", f"/v1/traps/{args['trapId']}/sessions/active")

        elif name == "get_session":
            result = await _request(trap_host, "GET", f"/v1/traps/{args['trapId']}/sessions/{args['sessionId']}")

        elif name == "list_detections":
            params = {k: v for k, v in args.items() if k not in ["trapHost", "trapId", "sessionId"]}
            query = "&".join([f"{k}={v}" for k, v in params.items()]) if params else ""
            path = f"/v1/traps/{args['trapId']}/sessions/{args['sessionId']}/detections"
            if query:
                path += f"?{query}"
            result = await _request(trap_host, "GET", path)

        elif name == "get_detection":
            result = await _request(trap_host, "GET", f"/v1/traps/{args['trapId']}/detections/{args['detectionId']}")

        elif name == "get_crop_image_url":
            url = f"http://{trap_host}:8080/v1/crops/{args['date']}/{args['filename']}"
            result = {"image_url": url}

        elif name == "get_system_metrics":
            result = await _request(trap_host, "GET", f"/v1/traps/{args['trapId']}/system")

        elif name == "get_server_status":
            result = await _request(trap_host, "GET", "/status")

        elif name == "get_recent_detections":
            active = await _request(trap_host, "GET", f"/v1/traps/{args['trapId']}/sessions/active")
            if not active.get("active"):
                result = {"message": "No active session", "detections": []}
            else:
                detections = await _request(
                    trap_host,
                    "GET",
                    f"/v1/traps/{args['trapId']}/sessions/{active['sessionId']}/detections?limit=100"
                )
                result = detections

        elif name == "get_stream_urls":
            result = {
                "standard_stream": f"http://{trap_host}:8080/stream.mjpg",
                "hires_stream": f"http://{trap_host}:8080/stream_hires.mjpg",
                "note": "Open these URLs in a browser. MJPEG streams cannot be viewed in Cline."
            }

        # ----- State-changing endpoints -----
        elif name == "start_monitoring":
            result = await _request(trap_host, "POST", f"/v1/traps/{args['trapId']}/sessions")

        elif name == "stop_monitoring":
            result = await _request(trap_host, "PUT", f"/v1/traps/{args['trapId']}/sessions/{args['sessionId']}/stop")

        # ----- Destructive endpoint -----
        elif name == "provision_trap":
            result = await _request(trap_host, "POST", "/v1/provision", json_data={"trapId": args["trapId"]})

        else:
            raise ValueError(f"Unknown tool: {name}")

        return [types.TextContent(type="text", text=json.dumps(result, indent=2))]

    except httpx.HTTPStatusError as e:
        return [types.TextContent(
            type="text",
            text=f"HTTP Error {e.response.status_code}: {e.response.text}"
        )]
    except Exception as e:
        return [types.TextContent(type="text", text=f"Error: {str(e)}")]


async def main():
    async with mcp.server.stdio.stdio_server() as (read_stream, write_stream):
        await server.run(
            read_stream,
            write_stream,
            InitializationOptions(
                server_name="naturesense-trap-mcp",
                server_version="1.0.0"
            )
        )


if __name__ == "__main__":
    import asyncio
    asyncio.run(main())
