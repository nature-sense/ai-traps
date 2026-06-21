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
MCP Server for NatureSense AI Camera Trap — WebSocket JSON-RPC + BLE GATT Interface.

Uses a single WebSocket connection (ws://trapHost:8080/ws) to communicate with the
trap's JSON-RPC over WebSocket endpoint (WsApiHandler on the C++ firmware side).

Also provides BLE GATT tools (via bleak) for local BLE discovery and provisioning.

Usage:
  This script is run by Cline as an MCP server via STDIN/STDOUT JSON-RPC.
  It is NOT meant to be run directly from the terminal.

Installation:
  cp scripts/trap-mcp-server.py /Users/steve/.local/bin/
  chmod +x /Users/steve/.local/bin/trap-mcp-server.py

Dependencies:
  pip install mcp websockets bleak

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
          "discover_traps",
          "ble_scan_traps",
          "ble_read_trap_id",
          "ble_read_wifi_state"
        ]
      }
    }
  }
"""

import asyncio
import json
import subprocess
import struct
import websockets
from mcp.server import Server
from mcp.server.models import InitializationOptions
import mcp.server.stdio
import mcp.types as types

# BLE GATT Service UUIDs (from docs/ble-gatt-service.md)
BLE_SERVICE_UUID = "A1C00001-0001-4A54-8000-0A4D4943414D"
BLE_CHAR_TRAP_ID = "A1C00002-0001-4A54-8000-0A4D4943414D"
BLE_CHAR_WIFI_STATE = "A1C00003-0001-4A54-8000-0A4D4943414D"
BLE_CHAR_PROVISION_STATION = "A1C00004-0001-4A54-8000-0A4D4943414D"
BLE_CHAR_PROVISION_AP = "A1C00005-0001-4A54-8000-0A4D4943414D"
BLE_CHAR_CMD_RESPONSE = "A1C00006-0001-4A54-8000-0A4D4943414D"

# Manufacturer data constants
MANUFACTURER_ID = 0xFFFF  # Development company ID
BLE_PROTOCOL_VERSION = 0x01
BLE_SERVICE_SHORT_UUID = 0x0001

# WiFi state enum (from docs/ble-gatt-service.md)
WIFI_STATE_NAMES = {
    0x00: "WIFI_OFF",
    0x01: "WIFI_STATION",
    0x02: "WIFI_AP",
    0x03: "WIFI_STATION_CONNECTING",
    0x04: "WIFI_STATION_FAILED",
}

# Command response status codes (from docs/ble-gatt-service.md)
CMD_STATUS_NAMES = {
    0x00: "SUCCESS",
    0x01: "ACCEPTED",
    0x02: "INVALID_FORMAT",
    0x03: "SSID_TOO_LONG",
    0x04: "PASSWORD_TOO_LONG",
    0x05: "CONFIG_FAILED",
    0x06: "CONNECTION_FAILED",
    0x07: "INTERNAL_ERROR",
}

server = Server("naturesense-trap-mcp")

# Default WebSocket port
WS_PORT = 8080


# Global JSON-RPC ID counter
_rpc_id = 0


def _next_id() -> int:
    global _rpc_id
    _rpc_id += 1
    return _rpc_id


# ---------------------------------------------------------------------------
# WebSocket JSON-RPC helper
# ---------------------------------------------------------------------------

async def _ws_request(trap_host: str, method: str, params: dict = None) -> dict:
    """
    Connect to the trap's WebSocket endpoint, send a JSON-RPC request,
    and wait for the matching response.

    Protocol (JSON-RPC 2.0-like):
      Request:  {"id": N, "method": "method_name", "params": {...}}
      Success:  {"id": N, "result": {...}}
      Error:    {"id": N, "error": {"code": C, "message": "..."}}

    trap_host example: "rock-3c.local" or "trap-singapore-01.local"
    """
    ws_url = f"ws://{trap_host}:{WS_PORT}/ws"
    request_id = _next_id()
    request = {
        "id": request_id,
        "method": method,
        "params": params or {},
    }

    async with websockets.connect(ws_url, max_size=2**20, open_timeout=10) as ws:
        # Send the request
        await ws.send(json.dumps(request))

        # Wait for the matching response
        while True:
            raw = await asyncio.wait_for(ws.recv(), timeout=30.0)
            if not isinstance(raw, str):
                continue  # Skip binary frames (e.g., JPEG images)

            try:
                response = json.loads(raw)
            except json.JSONDecodeError:
                continue

            # Match by ID
            if response.get("id") == request_id:
                if "result" in response:
                    return response["result"]
                elif "error" in response:
                    err = response["error"]
                    code = err.get("code", -1)
                    msg = err.get("message", "Unknown error")
                    raise WsRpcException(code, msg)
                else:
                    raise WsRpcException(-1, "Malformed response: missing result/error")

    # Should never reach here
    raise WsRpcException(-1, "WebSocket closed without response")


class WsRpcException(Exception):
    """Exception raised when a WebSocket JSON-RPC call returns an error."""
    def __init__(self, code: int, message: str):
        self.code = code
        self.message = message
        super().__init__(f"WS RPC error {code}: {message}")


# ---------------------------------------------------------------------------
# Parameter translation: MCP tool params (camelCase) -> WS method params (snake_case)
# ---------------------------------------------------------------------------

def _translate_params(args: dict, mapping: dict) -> dict:
    """
    Translate MCP tool parameter names (camelCase) to WS method parameter names
    (snake_case) using the given mapping.

    Example mapping: {"sessionId": "session_id", "detectionId": "detection_id"}

    All trapHost is stripped out (not sent over WS).
    """
    params = {}
    for mcp_key, ws_key in mapping.items():
        if mcp_key in args:
            params[ws_key] = args[mcp_key]
    return params


# ---------------------------------------------------------------------------
# BLE helper functions
# ---------------------------------------------------------------------------

def _parse_manufacturer_data(manu_data: bytes) -> dict | None:
    """
    Parse manufacturer-specific data from a BLE advertisement.
    Format (from docs/ble-gatt-service.md):
      Byte 0:       Protocol Version (0x01)
      Bytes 1-2:    Service UUID (0x0001)
      Bytes 3-18:   Trap ID (16 bytes, ASCII, null-padded)
      Byte 19:      WiFi State
    Returns a dict with trap_id and wifi_state, or None if not a trap.
    """
    if len(manu_data) < 20:
        return None

    proto_ver = manu_data[0]
    service_uuid = struct.unpack("<H", manu_data[1:3])[0]

    if proto_ver != BLE_PROTOCOL_VERSION or service_uuid != BLE_SERVICE_SHORT_UUID:
        return None

    # Extract trap ID (16 bytes, null-padded ASCII)
    trap_id_bytes = manu_data[3:19]
    trap_id = trap_id_bytes.rstrip(b"\x00").decode("ascii", errors="replace")

    wifi_state = manu_data[19]

    return {
        "trap_id": trap_id,
        "wifi_state": wifi_state,
        "wifi_state_name": WIFI_STATE_NAMES.get(wifi_state, f"UNKNOWN_{wifi_state}"),
    }


def _encode_provision_data(ssid: str, password: str) -> bytes:
    """
    Encode SSID + password for BLE provisioning write.
    Format (from docs/ble-gatt-service.md):
      Byte 0:       SSID Length (uint8)
      Bytes 1..N:   SSID (UTF-8)
      Byte N+1:     Password Length (uint8)
      Bytes N+2..M: Password (UTF-8)
    """
    ssid_bytes = ssid.encode("utf-8")
    password_bytes = password.encode("utf-8")

    if len(ssid_bytes) > 32:
        raise ValueError(f"SSID too long: {len(ssid_bytes)} bytes (max 32)")
    if len(password_bytes) > 32:
        raise ValueError(f"Password too long: {len(password_bytes)} bytes (max 32)")

    data = bytearray()
    data.append(len(ssid_bytes))
    data.extend(ssid_bytes)
    data.append(len(password_bytes))
    data.extend(password_bytes)
    return bytes(data)


async def _ble_connect_and_read(ble_address: str, char_uuid: str) -> bytes:
    """
    Connect to a BLE device, read a characteristic, and disconnect.
    """
    from bleak import BleakClient

    async with BleakClient(ble_address, timeout=20.0) as client:
        value = await client.read_gatt_char(char_uuid)
        return value


async def _ble_connect_and_write(
    ble_address: str,
    write_char_uuid: str,
    data: bytes,
    notify_char_uuid: str | None = None,
    notify_timeout: float = 15.0,
) -> dict:
    """
    Connect to a BLE device, write to a characteristic, optionally wait for
    a notification response, and disconnect.

    Returns a dict with:
      - "status": the notification status code (if notify_char_uuid provided)
      - "status_name": human-readable status name
      - "message": optional human-readable message from the notification
      - "notification_received": whether a notification was received
    """
    from bleak import BleakClient

    notification_result = {"data": None, "received": False}

    def notification_handler(sender, data):
        notification_result["data"] = data
        notification_result["received"] = True

    async with BleakClient(ble_address, timeout=20.0) as client:
        # Enable notifications if requested
        if notify_char_uuid:
            await client.start_notify(notify_char_uuid, notification_handler)

        # Write the provisioning data
        await client.write_gatt_char(write_char_uuid, data)

        # Wait for notification (e.g., Command Response)
        if notify_char_uuid:
            waited = 0.0
            step = 0.5
            while waited < notify_timeout:
                if notification_result["received"]:
                    break
                await asyncio.sleep(step)
                waited += step

            # Stop notifications
            await client.stop_notify(notify_char_uuid)

        if notification_result["received"] and notification_result["data"]:
            raw = notification_result["data"]
            status_code = raw[0]
            message = raw[1:].rstrip(b"\x00").decode("utf-8", errors="replace") if len(raw) > 1 else ""
            return {
                "status": status_code,
                "status_name": CMD_STATUS_NAMES.get(status_code, f"UNKNOWN_{status_code}"),
                "message": message,
                "notification_received": True,
            }
        elif notify_char_uuid:
            return {
                "status": None,
                "status_name": "TIMEOUT",
                "message": "No command response notification received within timeout",
                "notification_received": False,
            }
        else:
            return {
                "status": None,
                "status_name": "WRITTEN",
                "message": "Data written successfully (no notification requested)",
                "notification_received": False,
            }


# ---------------------------------------------------------------------------
# Tool definitions
# ---------------------------------------------------------------------------

@server.list_tools()
async def handle_list_tools() -> list[types.Tool]:
    """Register all MCP tools with trapHost parameter."""

    # Common properties for all WS API tools (trapHost is always required)
    base_properties = {
        "trapHost": {
            "type": "string",
            "description": "mDNS hostname of the target trap (e.g., 'rock-3c.local' or 'trap-singapore-01.local')"
        }
    }

    return [
        # ===== WebSocket JSON-RPC tools (replacing former REST API) =====

        # ----- Read-only tools (safe for auto-approve) -----
        types.Tool(
            name="get_trap_status",
            description="Get current status of a provisioned trap (via WebSocket JSON-RPC)",
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
            description="Get real-time system metrics (CPU, memory, NPU, pipeline FPS, power). Currently returns 501 stub.",
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
            description="Get basic server status (runs on trap device). Uses get_status WS method.",
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

        # ===== BLE GATT tools (unchanged) =====

        # ----- BLE read-only tools (safe for auto-approve) -----
        types.Tool(
            name="ble_scan_traps",
            description="Scan for AI Camera Traps via BLE advertisements. Returns discovered traps with their BLE address, trap ID, WiFi state, and RSSI.",
            inputSchema={
                "type": "object",
                "properties": {
                    "scan_duration": {
                        "type": "integer",
                        "description": "Duration of BLE scan in seconds (default: 10)",
                        "default": 10
                    }
                },
                "required": []
            }
        ),
        types.Tool(
            name="ble_read_trap_id",
            description="Connect to a trap via BLE and read its Trap Identity characteristic. Requires the trap's BLE address (obtained from ble_scan_traps).",
            inputSchema={
                "type": "object",
                "properties": {
                    "ble_address": {
                        "type": "string",
                        "description": "BLE MAC address of the trap (e.g., 'AA:BB:CC:DD:EE:FF')"
                    }
                },
                "required": ["ble_address"]
            }
        ),
        types.Tool(
            name="ble_read_wifi_state",
            description="Connect to a trap via BLE and read its WiFi State characteristic. Requires the trap's BLE address (obtained from ble_scan_traps).",
            inputSchema={
                "type": "object",
                "properties": {
                    "ble_address": {
                        "type": "string",
                        "description": "BLE MAC address of the trap (e.g., 'AA:BB:CC:DD:EE:FF')"
                    }
                },
                "required": ["ble_address"]
            }
        ),

        # ----- BLE state-changing tools (require manual approval) -----
        types.Tool(
            name="ble_provision_station",
            description="Provision a trap to connect to a WiFi network as a station via BLE GATT. Writes SSID + password to the WiFi Provision Station characteristic. Requires BLE pairing (Numeric Comparison).",
            inputSchema={
                "type": "object",
                "properties": {
                    "ble_address": {
                        "type": "string",
                        "description": "BLE MAC address of the trap (e.g., 'AA:BB:CC:DD:EE:FF')"
                    },
                    "ssid": {
                        "type": "string",
                        "description": "WiFi network SSID (1-32 characters)"
                    },
                    "password": {
                        "type": "string",
                        "description": "WiFi network password (0-32 characters; empty for open network)"
                    }
                },
                "required": ["ble_address", "ssid", "password"]
            }
        ),
        types.Tool(
            name="ble_provision_ap",
            description="Provision a trap to start a WiFi Access Point via BLE GATT. Writes SSID + password to the WiFi Provision AP characteristic. Requires BLE pairing (Numeric Comparison).",
            inputSchema={
                "type": "object",
                "properties": {
                    "ble_address": {
                        "type": "string",
                        "description": "BLE MAC address of the trap (e.g., 'AA:BB:CC:DD:EE:FF')"
                    },
                    "ssid": {
                        "type": "string",
                        "description": "AP network SSID (1-32 characters)"
                    },
                    "password": {
                        "type": "string",
                        "description": "AP network password (8-32 characters; empty for open AP)"
                    }
                },
                "required": ["ble_address", "ssid", "password"]
            }
        ),
    ]


# ---------------------------------------------------------------------------
# Tool execution
# ---------------------------------------------------------------------------

@server.call_tool()
async def handle_call_tool(
    name: str,
    arguments: dict | None
) -> list[types.TextContent]:
    """Execute the requested tool."""
    args = arguments or {}

    # ===== BLE GATT tools =====
    # These don't need trapHost — they use BLE directly

    if name == "ble_scan_traps":
        return await _handle_ble_scan(args)

    if name == "ble_read_trap_id":
        return await _handle_ble_read_trap_id(args)

    if name == "ble_read_wifi_state":
        return await _handle_ble_read_wifi_state(args)

    if name == "ble_provision_station":
        return await _handle_ble_provision(args, station_mode=True)

    if name == "ble_provision_ap":
        return await _handle_ble_provision(args, station_mode=False)

    # ===== WebSocket JSON-RPC tools and mDNS discovery =====

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
        # ----- URL construction tools (no WebSocket call needed) -----
        if name == "get_crop_image_url":
            url = f"http://{trap_host}:{WS_PORT}/v1/crops/{args['date']}/{args['filename']}"
            return [types.TextContent(type="text", text=json.dumps({"image_url": url}, indent=2))]

        if name == "get_stream_urls":
            result = {
                "standard_stream": f"http://{trap_host}:{WS_PORT}/stream.mjpg",
                "hires_stream": f"http://{trap_host}:{WS_PORT}/stream_hires.mjpg",
                "note": "Open these URLs in a browser. MJPEG streams cannot be viewed in Cline."
            }
            return [types.TextContent(type="text", text=json.dumps(result, indent=2))]

        # ----- Tools that need a WebSocket connection -----

        if name == "get_server_status":
            # get_status WS method doesn't need params
            result = await _ws_request(trap_host, "get_status", {})

        elif name == "get_trap_status":
            result = await _ws_request(trap_host, "get_status", {})

        elif name == "get_system_metrics":
            result = await _ws_request(trap_host, "get_system_metrics", {})

        elif name == "start_monitoring":
            result = await _ws_request(trap_host, "start_session", {})

        elif name == "stop_monitoring":
            params = _translate_params(args, {"sessionId": "session_id"})
            result = await _ws_request(trap_host, "stop_session", params)

        elif name == "list_sessions":
            params = _translate_params(args, {"limit": "limit", "offset": "offset"})
            result = await _ws_request(trap_host, "list_sessions", params)

        elif name == "get_active_session":
            result = await _ws_request(trap_host, "get_active_session", {})

        elif name == "get_session":
            params = _translate_params(args, {"sessionId": "session_id"})
            result = await _ws_request(trap_host, "get_session", params)

        elif name == "list_detections":
            params = _translate_params(args, {
                "sessionId": "session_id",
                "limit": "limit",
                "offset": "offset"
            })
            result = await _ws_request(trap_host, "list_detections", params)

        elif name == "get_detection":
            params = _translate_params(args, {"detectionId": "detection_id"})
            result = await _ws_request(trap_host, "get_detection", params)

        elif name == "provision_trap":
            params = {"trapId": args.get("trapId", "")}
            result = await _ws_request(trap_host, "provision", params)

        elif name == "get_recent_detections":
            # Composite call: get active session, then list detections
            try:
                active = await _ws_request(trap_host, "get_active_session", {})
            except WsRpcException as e:
                # If the server doesn't support get_active_session yet, fall back
                return [types.TextContent(
                    type="text",
                    text=json.dumps({
                        "error": f"get_active_session failed: {e.message}",
                        "message": "Ensure the trap is provisioned and running."
                    }, indent=2)
                )]

            if not active.get("active", False):
                result = {"message": "No active session", "detections": []}
            else:
                session_id = active.get("id") or active.get("sessionId")
                if session_id is None:
                    # Try to extract from nested structure
                    session_info = active.get("activeSession", {})
                    session_id = session_info.get("id")
                detections = await _ws_request(trap_host, "list_detections", {
                    "session_id": session_id,
                    "limit": 100,
                    "offset": 0,
                })
                result = detections

        else:
            raise ValueError(f"Unknown tool: {name}")

        return [types.TextContent(type="text", text=json.dumps(result, indent=2))]

    except WsRpcException as e:
        return [types.TextContent(
            type="text",
            text=json.dumps({
                "error": f"WS RPC error {e.code}",
                "message": e.message
            }, indent=2)
        )]
    except websockets.exceptions.WebSocketException as e:
        return [types.TextContent(
            type="text",
            text=json.dumps({
                "error": f"WebSocket connection failed: {str(e)}",
                "message": f"Ensure the trap at {trap_host}:{WS_PORT} is running and the /ws endpoint is available."
            }, indent=2)
        )]
    except asyncio.TimeoutError:
        return [types.TextContent(
            type="text",
            text=json.dumps({
                "error": "WebSocket request timed out",
                "message": "The trap did not respond within the timeout period. Check network connectivity."
            }, indent=2)
        )]
    except Exception as e:
        return [types.TextContent(type="text", text=f"Error: {str(e)}")]


# ---------------------------------------------------------------------------
# BLE tool handlers
# ---------------------------------------------------------------------------

async def _handle_ble_scan(args: dict) -> list[types.TextContent]:
    """Handle ble_scan_traps: scan for BLE advertisements from traps."""
    scan_duration = args.get("scan_duration", 10)

    try:
        from bleak import BleakScanner

        discovered_traps = []

        def detection_callback(device, advertisement_data):
            # Check for manufacturer data
            if MANUFACTURER_ID not in (advertisement_data.manufacturer_data or {}):
                return

            manu_data = advertisement_data.manufacturer_data[MANUFACTURER_ID]
            parsed = _parse_manufacturer_data(manu_data)
            if parsed is None:
                return

            discovered_traps.append({
                "ble_address": device.address,
                "name": device.name or "Unknown",
                "rssi": advertisement_data.rssi,
                "trap_id": parsed["trap_id"],
                "wifi_state": parsed["wifi_state"],
                "wifi_state_name": parsed["wifi_state_name"],
            })

        scanner = BleakScanner(detection_callback)
        await scanner.start()
        await asyncio.sleep(scan_duration)
        await scanner.stop()

        if not discovered_traps:
            return [types.TextContent(
                type="text",
                text=json.dumps({
                    "message": "No AI Camera Traps found via BLE",
                    "note": "Ensure the trap is powered on and advertising. Scan duration: {}s".format(scan_duration),
                    "discovered_traps": [],
                }, indent=2)
            )]

        return [types.TextContent(
            type="text",
            text=json.dumps({
                "scan_duration_seconds": scan_duration,
                "traps_found": len(discovered_traps),
                "discovered_traps": discovered_traps,
            }, indent=2)
        )]

    except ImportError:
        return [types.TextContent(
            type="text",
            text=json.dumps({
                "error": "bleak library not installed",
                "message": "Install bleak: pip install bleak"
            }, indent=2)
        )]
    except Exception as e:
        return [types.TextContent(
            type="text",
            text=json.dumps({
                "error": f"BLE scan failed: {str(e)}",
                "message": "Ensure Bluetooth is enabled on this machine. On macOS, grant Bluetooth permission."
            }, indent=2)
        )]


async def _handle_ble_read_trap_id(args: dict) -> list[types.TextContent]:
    """Handle ble_read_trap_id: read the Trap Identity characteristic."""
    ble_address = args.get("ble_address")
    if not ble_address:
        return [types.TextContent(
            type="text",
            text="Error: 'ble_address' parameter is required (e.g., 'AA:BB:CC:DD:EE:FF')"
        )]

    try:
        from bleak import BleakClient
        from bleak.exceptions import BleakError

        value = await _ble_connect_and_read(ble_address, BLE_CHAR_TRAP_ID)
        trap_id = value.rstrip(b"\x00").decode("utf-8", errors="replace")

        return [types.TextContent(
            type="text",
            text=json.dumps({
                "ble_address": ble_address,
                "trap_id": trap_id,
                "raw_hex": value.hex(),
            }, indent=2)
        )]

    except ImportError:
        return [types.TextContent(
            type="text",
            text=json.dumps({
                "error": "bleak library not installed",
                "message": "Install bleak: pip install bleak"
            }, indent=2)
        )]
    except BleakError as e:
        return [types.TextContent(
            type="text",
            text=json.dumps({
                "error": f"BLE connection failed: {str(e)}",
                "message": "Ensure the trap is powered on, advertising, and within range. The BLE address should be obtained from ble_scan_traps."
            }, indent=2)
        )]
    except Exception as e:
        return [types.TextContent(
            type="text",
            text=json.dumps({
                "error": f"Failed to read trap ID: {str(e)}"
            }, indent=2)
        )]


async def _handle_ble_read_wifi_state(args: dict) -> list[types.TextContent]:
    """Handle ble_read_wifi_state: read the WiFi State characteristic."""
    ble_address = args.get("ble_address")
    if not ble_address:
        return [types.TextContent(
            type="text",
            text="Error: 'ble_address' parameter is required (e.g., 'AA:BB:CC:DD:EE:FF')"
        )]

    try:
        from bleak.exceptions import BleakError

        value = await _ble_connect_and_read(ble_address, BLE_CHAR_WIFI_STATE)
        wifi_state = value[0] if value else 0xFF
        wifi_state_name = WIFI_STATE_NAMES.get(wifi_state, f"UNKNOWN_{wifi_state}")

        return [types.TextContent(
            type="text",
            text=json.dumps({
                "ble_address": ble_address,
                "wifi_state": wifi_state,
                "wifi_state_name": wifi_state_name,
                "raw_hex": value.hex(),
            }, indent=2)
        )]

    except ImportError:
        return [types.TextContent(
            type="text",
            text=json.dumps({
                "error": "bleak library not installed",
                "message": "Install bleak: pip install bleak"
            }, indent=2)
        )]
    except BleakError as e:
        return [types.TextContent(
            type="text",
            text=json.dumps({
                "error": f"BLE connection failed: {str(e)}",
                "message": "Ensure the trap is powered on, advertising, and within range. The BLE address should be obtained from ble_scan_traps."
            }, indent=2)
        )]
    except Exception as e:
        return [types.TextContent(
            type="text",
            text=json.dumps({
                "error": f"Failed to read WiFi state: {str(e)}"
            }, indent=2)
        )]


async def _handle_ble_provision(args: dict, station_mode: bool) -> list[types.TextContent]:
    """Handle ble_provision_station or ble_provision_ap."""
    ble_address = args.get("ble_address")
    ssid = args.get("ssid", "")
    password = args.get("password", "")

    if not ble_address:
        return [types.TextContent(
            type="text",
            text="Error: 'ble_address' parameter is required (e.g., 'AA:BB:CC:DD:EE:FF')"
        )]
    if not ssid:
        return [types.TextContent(
            type="text",
            text="Error: 'ssid' parameter is required"
        )]

    mode_name = "station" if station_mode else "access point"
    write_char = BLE_CHAR_PROVISION_STATION if station_mode else BLE_CHAR_PROVISION_AP

    try:
        from bleak.exceptions import BleakError

        # Encode the provisioning data
        provision_data = _encode_provision_data(ssid, password)

        # Write to the characteristic and wait for Command Response notification
        result = await _ble_connect_and_write(
            ble_address,
            write_char,
            provision_data,
            notify_char_uuid=BLE_CHAR_CMD_RESPONSE,
            notify_timeout=15.0,
        )

        return [types.TextContent(
            type="text",
            text=json.dumps({
                "ble_address": ble_address,
                "mode": mode_name,
                "ssid": ssid,
                "provision_data_hex": provision_data.hex(),
                "response": result,
            }, indent=2)
        )]

    except ValueError as e:
        return [types.TextContent(
            type="text",
            text=json.dumps({
                "error": f"Invalid provisioning data: {str(e)}",
                "message": "SSID must be 1-32 characters, password must be 0-32 characters."
            }, indent=2)
        )]
    except ImportError:
        return [types.TextContent(
            type="text",
            text=json.dumps({
                "error": "bleak library not installed",
                "message": "Install bleak: pip install bleak"
            }, indent=2)
        )]
    except BleakError as e:
        return [types.TextContent(
            type="text",
            text=json.dumps({
                "error": f"BLE connection failed: {str(e)}",
                "message": "Ensure the trap is powered on, advertising, and within range. BLE provisioning requires pairing (Numeric Comparison)."
            }, indent=2)
        )]
    except Exception as e:
        return [types.TextContent(
            type="text",
            text=json.dumps({
                "error": f"Failed to provision {mode_name} mode: {str(e)}"
            }, indent=2)
        )]


async def main():
    async with mcp.server.stdio.stdio_server() as (read_stream, write_stream):
        await server.run(
            read_stream,
            write_stream,
            InitializationOptions(
                server_name="naturesense-trap-mcp",
                server_version="2.0.0",
                capabilities={}
            )
        )


if __name__ == "__main__":
    asyncio.run(main())