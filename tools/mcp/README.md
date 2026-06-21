# Trap Operations MCP Server

Query trap REST API for status, sessions, detections, and metrics.

This MCP server talks directly to the trap binary's **embedded HTTP server** (CivetWeb, port 8080 on the board). It only works when the trap binary is running on the target board.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    YOUR MAC (Development)                 │
│                                                          │
│   VS Code + Cline                                        │
│                                                          │
│   MCP Server: trap-ops                                   │
│   ├── get_trap_status      - Trap status                 │
│   ├── list_sessions        - List monitoring sessions    │
│   ├── get_active_session   - Active session details      │
│   ├── get_session          - Session details             │
│   ├── list_detections      - Detections from session     │
│   ├── get_detection        - Single detection            │
│   ├── get_crop_image_url   - Crop image URL              │
│   ├── get_system_metrics   - CPU, memory, NPU, FPS      │
│   ├── get_server_status    - Server status               │
│   ├── get_recent_detections - Recent detections (poll)   │
│   ├── get_stream_urls      - MJPEG stream URLs           │
│   ├── start_monitoring     - Start monitoring session    │
│   ├── stop_monitoring      - Stop monitoring session     │
│   ├── provision_trap       - ONE-TIME: Provision trap    │
│   ├── discover_traps       - mDNS trap discovery         │
│   ├── ble_scan_traps       - Scan for traps via BLE      │
│   ├── ble_read_trap_id     - Read trap identity via BLE  │
│   ├── ble_read_wifi_state  - Read WiFi state via BLE     │
│   ├── ble_provision_station - Provision WiFi via BLE     │
│   └── ble_provision_ap     - Provision WiFi AP via BLE   │
└─────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────┐
│              TARGET BOARD (e.g. ROCK 3C)                 │
│                                                          │
│   Trap binary running (port 8080)                        │
│   REST API: /status, /v1/traps/*, /stream.mjpg, /events │
└─────────────────────────────────────────────────────────┘
```

## Prerequisites

```bash
pip install mcp httpx
```

For BLE tools:
```bash
pip install bleak
```

## Installation

```bash
cp scripts/trap-mcp-server.py /Users/steve/.local/bin/
chmod +x /Users/steve/.local/bin/trap-mcp-server.py
```

## MCP Configuration

Add to your `cline_mcp_settings.json`:

```json
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
```

## Tools

| Tool | REST Endpoint | Description | Auto-Approve |
|------|--------------|-------------|:---:|
| `discover_traps` | mDNS (avahi) | Discover traps on local network | ✅ |
| `get_trap_status` | `GET /v1/traps/{trapId}` | Current trap status | ✅ |
| `list_sessions` | `GET /v1/traps/{trapId}/sessions` | List monitoring sessions | ✅ |
| `get_active_session` | `GET /v1/traps/{trapId}/sessions/active` | Currently active session | ✅ |
| `get_session` | `GET /v1/traps/{trapId}/sessions/{sessionId}` | Session details | ✅ |
| `list_detections` | `GET /v1/traps/{trapId}/sessions/{sessionId}/detections` | Detections from session | ✅ |
| `get_detection` | `GET /v1/traps/{trapId}/detections/{detectionId}` | Single detection | ✅ |
| `get_crop_image_url` | Constructs URL from date/filename | Crop image URL | ✅ |
| `get_system_metrics` | `GET /v1/traps/{trapId}/system` | CPU, memory, NPU, FPS, power | ✅ |
| `get_server_status` | `GET /status` | Server status | ✅ |
| `get_recent_detections` | Polls active session | Recent detections (poll) | ✅ |
| `get_stream_urls` | Returns MJPEG URLs | Stream URLs for browser | ✅ |
| `start_monitoring` | `POST /v1/traps/{trapId}/sessions` | Start monitoring | ❌ (approval) |
| `stop_monitoring` | `PUT /v1/traps/{trapId}/sessions/{sessionId}/stop` | Stop monitoring | ❌ (approval) |
| `provision_trap` | `POST /v1/provision` | ONE-TIME: Provision trap | ❌ (never) |
| `ble_scan_traps` | BLE advertisement scan | Scan for traps via BLE | ✅ |
| `ble_read_trap_id` | BLE GATT read | Read trap identity | ✅ |
| `ble_read_wifi_state` | BLE GATT read | Read WiFi state | ✅ |
| `ble_provision_station` | BLE GATT write | Provision WiFi station mode | ❌ (approval) |
| `ble_provision_ap` | BLE GATT write | Provision WiFi AP mode | ❌ (approval) |

**All tools** (except `discover_traps` and BLE tools) require a `trapHost` parameter — the mDNS hostname of the target trap (e.g., `rock-3c.local`).

## Example Cline Prompts

- "Discover all traps on the network" → `discover_traps()`
- "Check status of trap at rock-3c.local" → `get_trap_status(trapHost="rock-3c.local", trapId="...")`
- "Start monitoring on rock-3c" → `start_monitoring(trapHost="rock-3c.local", trapId="...")`
- "Scan for BLE traps" → `ble_scan_traps()`
