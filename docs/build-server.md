# AI Camera Trap Build Server

## 1. Aims & Motivation

### Problem
The original build/deploy system used SSH-based MCP tools that executed commands directly on target boards via `ssh`. This approach had several limitations:

- **SSH key management**: Every developer needed SSH keys configured for every board
- **No parallelism**: Each tool call was a separate SSH session with no shared state
- **No build history**: No way to track what was built, when, or what the result was
- **No API**: The Flutter app and other tools couldn't programmatically trigger builds
- **Fragile**: SSH session timeouts, network interruptions, and TTY issues caused failures
- **No authentication**: Any process on the dev machine could SSH into boards

### Solution: Build Server REST API
A lightweight FastAPI-based HTTP server that runs directly on each target board (port 8081), providing:

1. **REST API for all build/deploy operations** — clean, documented, testable
2. **Source upload via HTTP** — send changed files as a tarball, build server extracts and builds
3. **Build state management** — track build IDs, status, logs
4. **Trap lifecycle management** — start/stop/status/logs via HTTP
5. **Environment verification** — check all dependencies before building
6. **Platform abstraction** — same API for rock3c, cubie-a7s, rdk-x5

### Design Goals
- **Replace SSH entirely** — all communication over HTTP
- **Stateless where possible** — each request is self-contained
- **Idempotent operations** — safe to retry builds
- **Minimal dependencies** — only FastAPI + uvicorn + standard library
- **Platform-agnostic API** — same endpoints for all boards
- **Secure by default** — runs on isolated port, no auth (for now)

---

## 2. Architecture

### Two-Layer Design

```
  DEVELOPER MACHINE
  ┌──────────────────────────────────────────────────────────────┐
  │                                                              │
  │  ┌──────────────┐  ┌──────────────────┐  ┌───────────────┐  │
  │  │    Cline      │  │   MCP Server     │  │ BuildServer   │  │
  │  │  (VS Code)    │─▶│ (mcp-build-      │─▶│ Client        │  │
  │  │               │  │  server.py)      │  │ (remote.py)   │  │
  │  └──────────────┘  └──────────────────┘  └───────┬───────┘  │
  │                                                    │         │
  │                                           HTTP (httpx)       │
  │                                           port 8081          │
  └──────────────────────────────────────────────────────┬───────┘
                                                         │
                                                         ▼
  TARGET BOARD (rock-3c.local)
  ┌──────────────────────────────────────────────────────────────┐
  │                                                              │
  │  ┌──────────────────────────────────────────────────────┐   │
  │  │              Build Server (FastAPI)                   │   │
  │  │                                                      │   │
  │  │  ┌──────────────┐  ┌──────────────┐  ┌────────────┐ │   │
  │  │  │  /api/v1/    │  │  Platform    │  │  Build     │ │   │
  │  │  │  status      │  │  (rock3c.py) │  │  Manager   │ │   │
  │  │  │  environment │  │              │  │  (build_   │ │   │
  │  │  │  build       │  │  - meson     │  │  manager)  │ │   │
  │  │  │  runtime     │  │  - ninja     │  │  - tarball │ │   │
  │  │  │  trap/*      │  │  - systemctl │  │  - build   │ │   │
  │  │  └──────────────┘  └──────────────┘  │  dir mgmt  │ │   │
  │  │                                       └────────────┘ │   │
  │  └──────────────────────────────────────────────────────┘   │
  │                                                              │
  │  ┌──────────────────────────────────────────────────────┐   │
  │  │              Trap Binary (C++ pipeline)               │   │
  │  │  Camera → Inference → Tracker → Overlay → MJPEG/SSE  │   │
  │  └──────────────────────────────────────────────────────┘   │
  └──────────────────────────────────────────────────────────────┘
```

### Component Responsibilities

#### 1. Build Server (`build_server/`)
- **`main.py`** — FastAPI application with all route handlers
- **`models.py`** — Pydantic models for request/response validation
- **`base_platform.py`** — Abstract base class defining the platform interface
- **`rock3c.py`** — ROCK 3C-specific implementation
- **`build_manager.py`** — Build orchestration (tarball extraction, meson setup/compile)

#### 2. MCP Client (`mcp_build/remote.py`)
- **`BuildServerClient`** — httpx-based HTTP client
- Handles tarball creation (changed files only or full project)
- Maps MCP tool calls to REST API calls

#### 3. MCP Server (`mcp_build/`)
- **`tools.py`** — Tool definitions (schemas for Cline)
- **`handler.py`** — Routes tool calls to `BuildServerClient`

### Data Flow: Build Cycle

```
1. Developer calls "build" tool in Cline
2. MCP Server receives tool call
3. handler.py creates BuildServerClient(host="rock-3c.local")
4. remote.py creates tarball of changed files (git diff HEAD~1)
5. remote.py POSTs tarball to http://rock-3c.local:8081/api/v1/build
6. Build Server extracts tarball to ~/ai-traps/
7. Build Server runs: meson setup build-dir → ninja -C build-dir
8. Build Server returns build_id + status
9. Developer polls get_build_status(build_id) for completion
10. On success, calls create_runtime(build_id) to install binary
11. create_runtime copies binary to /usr/local/bin, creates config, starts trap
```

---

## 3. REST API Reference

### Base URL
```
http://<board-hostname>:8081/api/v1
```

### 3.1 Server Status

#### `GET /api/v1/status`
Returns server health and platform information.

**Response:**
```json
{
  "status": "ok",
  "version": "1.0.0",
  "platform": "rock3c",
  "hostname": "rock-3c",
  "uptime_seconds": 7.83,
  "trap_running": true,
  "trap_pid": 3070,
  "disk_free_gb": 212.18,
  "memory_free_mb": 7330.0
}
```

### 3.2 Environment

#### `GET /api/v1/environment`
Checks all required and optional dependencies on the board.

**Response:**
```json
{
  "platform": "rock3c",
  "all_satisfied": true,
  "items": [
    {
      "name": "meson",
      "installed": true,
      "version": "1.0.1",
      "required": true,
      "message": null
    },
    {
      "name": "ninja",
      "installed": true,
      "version": "1.11.1",
      "required": true,
      "message": null
    },
    {
      "name": "g++",
      "installed": true,
      "version": "g++ (Debian 12.2.0-14+deb12u1) 12.2.0",
      "required": true,
      "message": null
    },
    {
      "name": "pkg-config",
      "installed": true,
      "version": "1.8.1",
      "required": true,
      "message": null
    },
    {
      "name": "python3",
      "installed": true,
      "version": "Python 3.11.2",
      "required": true,
      "message": null
    },
    {
      "name": "cmake",
      "installed": true,
      "version": "cmake version 3.25.1",
      "required": true,
      "message": null
    },
    {
      "name": "v4l2-ctl",
      "installed": true,
      "version": "v4l2-ctl 1.22.1-5414",
      "required": false,
      "message": null
    },
    {
      "name": "RKAIQ IQ files",
      "installed": true,
      "version": null,
      "required": true,
      "message": "Found: /usr/share/iqfiles"
    },
    {
      "name": "V4L2 device",
      "installed": true,
      "version": null,
      "required": true,
      "message": "Found: /dev/video0"
    },
    {
      "name": "rknn-toolkit2",
      "installed": false,
      "version": null,
      "required": false,
      "message": "Python package 'rknn' not installed"
    },
    {
      "name": "Project directory",
      "installed": true,
      "version": null,
      "required": false,
      "message": "Found: /home/radxa/ai-traps"
    },
    {
      "name": "Toolkit source",
      "installed": true,
      "version": null,
      "required": false,
      "message": "Found: /home/radxa/ai-traps/traps/toolkit"
    },
    {
      "name": "Targets source",
      "installed": true,
      "version": null,
      "required": false,
      "message": "Found: /home/radxa/ai-traps/traps/targets"
    },
    {
      "name": "Model directory",
      "installed": true,
      "version": null,
      "required": false,
      "message": "Found: /home/radxa/ai-traps/models/detection/insects/yolo26n"
    },
    {
      "name": "ONNX model",
      "installed": true,
      "version": null,
      "required": false,
      "message": "Found: /home/radxa/ai-traps/models/detection/insects/yolo26n/best.onnx"
    }
  ]
}
```

#### `POST /api/v1/setup`
Installs missing dependencies on the board.

**Request:**
```json
{
  "components": ["build", "runtime"]
}
```

**Response:**
```json
{
  "success": true,
  "platform": "rock3c",
  "installed": ["meson", "ninja", "g++"],
  "already_present": ["cmake", "pkg-config"],
  "failed": []
}
```

### 3.3 Build

#### `POST /api/v1/build`
Uploads a source tarball and triggers a meson build.

**Request:** `multipart/form-data`
- `file`: gzipped tarball of source files
- `platform`: `"rock3c"`
- `component`: `"all"` | `"model"` | `"toolkit"` | `"target"`
- `trap_type`: `"detection"` | `"classification"`
- `rebuild`: `"true"` | `"false"`

**Response:**
```json
{
  "build_id": "build_20260601_084212_abc123",
  "status": "running",
  "platform": "rock3c",
  "component": "all",
  "message": "Build started"
}
```

#### `GET /api/v1/build/{build_id}`
Returns the current status of a build.

**Response:**
```json
{
  "build_id": "build_20260601_084212_abc123",
  "status": "completed",
  "platform": "rock3c",
  "component": "all",
  "success": true,
  "duration_seconds": 45.2,
  "output_path": "/home/radxa/ai-traps/build-rock3c/traps/targets/build/ai-trap-detection"
}
```

#### `GET /api/v1/build/{build_id}/log`
Returns the build log output.

**Query Parameters:**
- `lines`: Number of lines to return (default: 500, max: 2000)

**Response:**
```json
{
  "build_id": "build_20260601_084212_abc123",
  "status": "completed",
  "log": "The meson build system\nVersion: 1.0.1\nSource dir: /home/radxa/ai-traps\nBuild dir: /home/radxa/ai-traps/build-rock3c\n..."
}
```

### 3.4 Runtime

#### `POST /api/v1/runtime`
Creates a runtime from a completed build: installs the binary, deploys model/config, and optionally starts the trap.

**Request:**
```json
{
  "build_id": "build_20260601_084212_abc123",
  "platform": "rock3c",
  "install_path": "/usr/local/bin",
  "start_trap": true
}
```

**Response:**
```json
{
  "success": true,
  "build_id": "build_20260601_084212_abc123",
  "binary_installed": "/usr/local/bin/rock3c-imx219",
  "config_deployed": "/etc/ai-trap/config.yaml",
  "model_deployed": "/usr/local/share/ai-trap/models/yolo26n.rknn",
  "trap_started": true,
  "trap_pid": 3070
}
```

### 3.5 Trap Lifecycle

#### `POST /api/v1/trap/start`
Starts the trap binary.

**Request:**
```json
{
  "binary_path": "/usr/local/bin/rock3c-imx219",
  "config_path": "/etc/ai-trap/config.yaml",
  "args": ""
}
```

**Response:**
```json
{
  "success": true,
  "pid": 3070,
  "message": "Trap started with PID 3070"
}
```

#### `POST /api/v1/trap/stop`
Stops the running trap.

**Request:**
```json
{
  "signal": "TERM"
}
```

**Response:**
```json
{
  "success": true,
  "message": "Sent signal TERM to PID 3070"
}
```

#### `GET /api/v1/trap/status`
Returns the status of the running trap.

**Response:**
```json
{
  "running": true,
  "pid": 3070,
  "cpu_percent": 0.0,
  "memory_percent": 0.0,
  "uptime_seconds": null,
  "binary_path": null
}
```

#### `GET /api/v1/trap/log`
Returns recent log output from the trap.

**Query Parameters:**
- `lines`: Number of lines (default: 100, max: 1000)
- `source`: `"file"` | `"journalctl"`

**Response:**
```json
{
  "success": true,
  "source": "file",
  "log": "[2026-06-01 08:42:10] Camera initialized\n[2026-06-01 08:42:11] Inference model loaded\n[2026-06-01 08:42:12] Pipeline started\n"
}
```

---

## 4. ROCK 3C Implementation

### Platform Overview
The ROCK 3C (Radxa ROCK 3C) is an ARM64 single-board computer based on the Rockchip RK3566 SoC:
- **CPU**: 4x Cortex-A55 @ 1.8 GHz
- **NPU**: 1.5 TOPS (RKNN)
- **RAM**: 4 GB LPDDR4
- **Camera**: IMX219 (8 MP) or IMX415 via CSI interface
- **Storage**: eMMC + microSD

### Implementation: `rock3c.py`

The `Rock3cPlatform` class extends `BasePlatform` and implements all platform-specific operations:

```python
class Rock3cPlatform(BasePlatform):
    """ROCK 3C platform implementation."""

    @property
    def name(self) -> str:
        return "rock3c"

    @property
    def default_host(self) -> str:
        return "rock-3c.local"

    @property
    def default_user(self) -> str:
        return "radxa"

    @property
    def default_trap_name(self) -> str:
        return "rock3c-imx219"

    @property
    def remote_dir(self) -> str:
        return os.path.expanduser("~/ai-traps")

    @property
    def build_dir(self) -> str:
        return "build-rock3c"
```

#### Key Methods

**`check_environment()`**
Verifies the following dependencies are present:

| Dependency       | Check Method              | Required |
|------------------|---------------------------|----------|
| meson            | `meson --version`         | Yes      |
| ninja            | `ninja --version`         | Yes      |
| g++              | `g++ --version`           | Yes      |
| pkg-config       | `pkg-config --version`    | Yes      |
| python3          | `python3 --version`       | Yes      |
| cmake            | `cmake --version`         | Yes      |
| v4l2-ctl         | `v4l2-ctl --version`      | No       |
| RKAIQ IQ files   | `/usr/share/iqfiles`      | Yes      |
| V4L2 device      | `/dev/video0`             | Yes      |
| rknn-toolkit2    | Python `rknn` module      | No       |
| Project dir      | `~/ai-traps`              | No       |
| Source dirs      | `traps/toolkit + targets` | No       |
| Model dir        | `models/.../yolo26n`      | No       |
| ONNX model       | `best.onnx` in model dir  | No       |

**`setup_environment(components)`**
Installs missing dependencies using `apt-get`:
- `build` component: meson, ninja, g++, cmake, pkg-config, python3-pip
- `runtime` component: v4l-utils, sqlite3, libjpeg-turbo, libyaml-cpp
- `model` component: rknn-toolkit2 (Python package)

**`build_source(component, trap_type, rebuild)`**
Orchestrates the meson build:
1. Creates build directory: `~/ai-traps/build-rock3c/`
2. Runs `meson setup build-rock3c -Dplatform=rock3c -Dtrap_type=detection`
3. Runs `ninja -C build-rock3c`
4. Returns the path to the built binary

**`start_trap(binary_path, config_path, args)`**
Starts the trap binary with sudo:
1. Creates config directory: `/etc/ai-trap/`
2. Copies config YAML if provided
3. Runs: `sudo nohup {binary_path} --config {config_path} {args} > /var/log/ai-trap/trap.log 2>&1 &`
4. Stores PID in `/tmp/ai-trap-{trap_name}.pid`

**`stop_trap(signal)`**
Stops the trap by reading the PID file and sending a signal.

**`get_trap_status()`**
Checks if the trap is running via `/proc/{pid}/status`.

**`get_trap_log(lines, source)`**
Reads the trap log file or queries journalctl.

### Build Configuration

The meson build for rock3c uses:
```
-Dplatform=rock3c
-Dtrap_type=detection
```

This selects:
- `traps/toolkit/src/hal/platforms/rock3c/` — HAL implementations
- `traps/targets/src/detection/platforms/rock3c/` — Platform pipeline
- RKNN model format (`.rknn`)
- Rockchip MPP for hardware JPEG encoding
- RKRGA for hardware video scaling

### Deployment Paths

| Artifact | Source | Destination |
|----------|--------|-------------|
| Binary | `build-rock3c/traps/targets/build/ai-trap-detection` | `/usr/local/bin/rock3c-imx219` |
| Config | `traps/targets/config/rock3c.yaml` | `/etc/ai-trap/config.yaml` |
| Model | `models/detection/insects/yolo26n/yolo26n.rknn` | `/usr/local/share/ai-trap/models/` |
| Logs | — | `/var/log/ai-trap/trap.log` |

---

## 5. Deployment

### Prerequisites
- Target board with network access (mDNS hostname or IP)
- SSH access configured (for initial deployment only)
- Python 3.9+ on target board

### Deploy Script
```bash
# From the project root:
bash tools/mcp/scripts/deploy_build_server.sh rock-3c.local radxa
```

This script:
1. Creates directories on the board
2. Copies the `build_server/` package via rsync
3. Installs Python dependencies (FastAPI, uvicorn, pydantic)
4. Installs the entry point script to `/usr/local/bin/ai-trap-build-server`
5. Installs and enables the systemd service (auto-start on boot)
6. Starts (or restarts) the build server service

### Service Management (systemd)

The build server runs as a **systemd service** (`ai-trap-build-server.service`), which means:
- **Auto-start on boot**: Enabled via `systemctl enable`
- **Auto-restart on crash**: Configured with `Restart=always`
- **Logs to journald**: Accessible via `journalctl`

#### Service Commands
```bash
# Check service status
ssh radxa@rock-3c.local "sudo systemctl status ai-trap-build-server"

# Restart (e.g., after updating files)
ssh radxa@rock-3c.local "sudo systemctl restart ai-trap-build-server"

# Stop
ssh radxa@rock-3c.local "sudo systemctl stop ai-trap-build-server"

# Start
ssh radxa@rock-3c.local "sudo systemctl start ai-trap-build-server"

# View live logs
ssh radxa@rock-3c.local "sudo journalctl -u ai-trap-build-server -f"

# View recent logs
ssh radxa@rock-3c.local "sudo journalctl -u ai-trap-build-server -n 100 --no-pager"
```

#### Re-deploying (Updating the Build Server)
When you need to update the build server (e.g., after code changes), simply re-run the deploy script. The `systemctl restart` command will gracefully stop the old process and start the new one:

```bash
bash tools/mcp/scripts/deploy_build_server.sh rock-3c.local radxa
```

#### Service Unit File
The systemd unit file is located at:
- **Source**: `tools/mcp/scripts/build_server/ai-trap-build-server.service`
- **Installed to**: `/etc/systemd/system/ai-trap-build-server.service` on the board

```ini
[Unit]
Description=AI Camera Trap Build Server
After=network.target network-online.target
Wants=network-online.target

[Service]
Type=simple
User=root
Group=root
ExecStart=/usr/local/bin/ai-trap-build-server
WorkingDirectory=/home/radxa/ai-traps
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
```

### Verification
```bash
curl -s http://rock-3c.local:8081/api/v1/status | python3 -m json.tool
```

Expected output:
```json
{
  "status": "ok",
  "version": "1.0.0",
  "platform": "rock3c",
  "hostname": "rock-3c",
  "trap_running": true,
  "trap_pid": 3070
}
```

---

## 6. File Reference

### Build Server Package
```
tools/mcp/scripts/
  build_server/
    __init__.py              Package init
    main.py                  FastAPI app, route handlers
    models.py                Pydantic request/response models
    base_platform.py         Abstract base class
    rock3c.py                ROCK 3C implementation
    build_manager.py         Build orchestration
    requirements.txt         Python dependencies
    ai-trap-build-server.service  systemd unit file
  build_server.py            Entry point
  deploy_build_server.sh     Deploy script
  mcp_build/
    remote.py                BuildServerClient (httpx)
    tools.py                 MCP tool definitions
    handler.py               MCP tool handler
    config.py                Platform config constants
```

---

## 7. Future Work

### Short-term
- [ ] Add cubie-a7s platform implementation
- [ ] Add rdk-x5 platform implementation
- [ ] Add build history endpoint (list past builds)

### Medium-term
- [ ] Add API key authentication
- [ ] Add WebSocket endpoint for real-time build log streaming
- [ ] Add build cancellation endpoint
- [ ] Add incremental builds (only rebuild changed targets)

### Long-term
- [ ] CI/CD integration (GitHub Actions -> Build Server)
- [ ] OTA firmware updates via build server
- [ ] Multi-board build orchestration
- [ ] Build artifact caching
