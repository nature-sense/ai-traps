# Remote Build Tools for AI Camera Trap

Multi-platform build tools for the AI Camera Trap project, enabling Cline/DeepSeek to build, run, and debug code on remote SBCs (Single Board Computers) via SSH + rsync + meson.

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         YOUR MAC (Development)                          │
│                                                                         │
│   IntelliJ IDEA + Cline + DeepSeek                                      │
│   Source Code: ~/naturesense/ai_trap/ai-camera-trap                     │
│                                                                         │
│   MCP Servers:                                                           │
│   ├── ai-trap-build (build/deploy):                                     │
│   │   ├── build_rock3c  (active)  - ROCK 3C                             │
│   │   ├── build_cubie   (active)  - Cubie A7S                           │
│   │   ├── build_rdkx5   (active)  - RDK X5                              │
│   │   ├── run_rock3c    (active)  - Run on ROCK 3C                      │
│   │   ├── stop_rock3c   (active)  - Stop on ROCK 3C                     │
│   │   ├── status_rock3c (active)  - Status on ROCK 3C                   │
│   │   ├── logs_rock3c   (active)  - Logs from ROCK 3C                   │
│   │   ├── debug_rock3c  (active)  - Debug on ROCK 3C                    │
│   │   └── list_binaries_rock3c (active) - List binaries on ROCK 3C      │
│   │                                                                     │
│   ├── trap-ops (monitor/control):                                       │
│   │   ├── get_trap_status      - Trap status                            │
│   │   ├── list_sessions        - List monitoring sessions               │
│   │   ├── get_active_session   - Active session details                 │
│   │   ├── get_session          - Session details                        │
│   │   ├── list_detections      - Detections from session                │
│   │   ├── get_detection        - Single detection                       │
│   │   ├── get_crop_image_url   - Crop image URL                         │
│   │   ├── get_system_metrics   - CPU, memory, NPU, FPS                 │
│   │   ├── get_server_status    - Server status                          │
│   │   ├── get_recent_detections - Recent detections (poll)              │
│   │   ├── get_stream_urls      - MJPEG stream URLs                     │
│   │   ├── start_monitoring     - Start monitoring session               │
│   │   ├── stop_monitoring      - Stop monitoring session                │
│   │   └── provision_trap       - ONE-TIME: Provision new trap           │
│                                                                         │
│   Shell Scripts (terminal/IntelliJ):                                    │
│   ├── remote-build.sh          - Unified build script                   │
│   ├── remote-build-rock3c.sh   - ROCK 3C (legacy)                       │
│   ├── remote-build-cubie.sh    - Cubie A7S (legacy)                     │
│   └── remote-build-rdkx5.sh    - RDK X5 (legacy)                        │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                    ┌───────────────┼───────────────┐
                    ▼               ▼               ▼
┌───────────────────────┐ ┌───────────────────────┐ ┌───────────────────────┐
│   ROCK 3C (Board #1)  │ │  Cubie A7S (Board #2) │ │   RDK X5 (Board #3)   │
│                       │ │                       │ │                       │
│   IP: rock-3c.local   │ │   IP: cubie-a7s.local │ │   IP: rdk-x5.local    │
│   Platform: rock3c    │ │   Platform: cubie-a7s  │ │   Platform: rdk-x5    │
│   Build dir: build-   │ │   Build dir: build-   │ │   Build dir: build-   │
│            rock3c     │ │            cubie-a7s  │ │            rdk-x5     │
│                       │ │                       │ │                       │
│   Binary runs here    │ │   Binary runs here    │ │   Binary runs here    │
└───────────────────────┘ └───────────────────────┘ └───────────────────────┘
```

**Key Principle:** Each board is both the **build machine** and the **deployment target**. No binaries are copied back to the Mac. Build once, run immediately on the same board.

## Prerequisites

- **rsync** on Mac: `brew install rsync`
- **SSH key-based auth** to each board (see Setup below)
- **meson + ninja** on each board: `sudo apt install meson ninja-build`
- **gdb** on ROCK 3C (for debug tool): `sudo apt install gdb`

## Quick Start

### 1. Install Build Scripts & MCP Server

```bash
# Copy scripts to ~/.local/bin
cp scripts/remote-build*.sh scripts/mcp-build-server.py /Users/steve/.local/bin/
chmod +x /Users/steve/.local/bin/remote-build*.sh /Users/steve/.local/bin/mcp-build-server.py
```

### 2. Set Up SSH Keys

```bash
# Generate SSH key (if not already done)
ssh-keygen -t ed25519 -C "ai-trap-build"

# Copy to ROCK 3C
ssh-copy-id radxa@rock-3c.local

# Test connection
ssh radxa@rock-3c.local "echo Connected to ROCK 3C"
```

### 3. Install Cline MCP Configuration

Copy `config/cline_mcp_settings.json` to `~/.cline/data/settings/cline_mcp_settings.json`
(or merge with your existing config).

### 4. Build!

```bash
# From command line (unified script)
./scripts/remote-build.sh rock3c

# Or with --rebuild for a clean build
./scripts/remote-build.sh rock3c ~/naturesense/ai_trap/ai-camera-trap --rebuild

# Other platforms
./scripts/remote-build.sh cubie-a7s
./scripts/remote-build.sh rdk-x5
```

## Build Scripts

### Unified Script (Recommended)

| Script | Usage | Description |
|--------|-------|-------------|
| `remote-build.sh` | `./remote-build.sh <platform> [project-root] [--rebuild]` | Unified build for all platforms |

**Platforms:**
- `rock3c` - Radxa ROCK 3C (host: rock-3c.local, user: radxa)
- `cubie-a7s` - Radxa Cubie A7S (host: cubie-a7s.local, user: radxa)
- `rdk-x5` - D-Robotics RDK X5 (host: rdk-x5.local, user: root)

**Environment variables:**
- `<PLATFORM>_HOST` - Board hostname/IP (e.g., `ROCK3C_HOST`, `CUBIE_HOST`, `RDKX5_HOST`)
- `<PLATFORM>_USER` - SSH user (e.g., `ROCK3C_USER`, `CUBIE_USER`, `RDKX5_USER`)

**Options:**
- `--rebuild` - Delete existing build directory and do a clean meson setup

### Legacy Scripts (Per-Platform)

| Script | Board | Host | User |
|--------|-------|------|------|
| `remote-build-rock3c.sh` | Radxa ROCK 3C | rock-3c.local | radxa |
| `remote-build-cubie.sh` | Radxa Cubie A7S | cubie-a7s.local | radxa |
| `remote-build-rdkx5.sh` | D-Robotics RDK X5 | rdk-x5.local | root |

## Cline MCP Integration

Two MCP servers are available:

### `ai-trap-build` — Build & Deploy

Server: `scripts/mcp-build-server.py` — SSH + rsync + meson for building, running, and debugging on target boards.

### `trap-ops` — Monitor & Control Deployed Traps

Server: `scripts/trap-mcp-server.py` — HTTP client to the trap's REST API (LAN, port 8080, no auth).

Each tool accepts a `trapHost` parameter (e.g., `rock-3c.local`) to address a specific trap via mDNS. No environment variables or IP configuration needed.

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

**All tools** (except `discover_traps`) require a `trapHost` parameter — the mDNS hostname of the target trap (e.g., `rock-3c.local`).

**Dependencies:**
```bash
pip install mcp httpx
```

**Installation:**
```bash
cp scripts/trap-mcp-server.py /Users/steve/.local/bin/
chmod +x /Users/steve/.local/bin/trap-mcp-server.py
```

**Example Cline prompts:**
- "Discover all traps on the network" → `discover_traps()`
- "Check status of trap at rock-3c.local" → `get_trap_status(trapHost="rock-3c.local", trapId="...")`
- "Start monitoring on rock-3c" → `start_monitoring(trapHost="rock-3c.local", trapId="...")`

### Build Tools (ai-trap-build)


### Build Tools

| Tool | Description |
|------|-------------|
| `build_rock3c` | Build AI Camera Trap for Radxa ROCK 3C |
| `build_cubie` | Build AI Camera Trap for Radxa Cubie A7S |
| `build_rdkx5` | Build AI Camera Trap for D-Robotics RDK X5 |

Each build tool:
- Syncs source code incrementally via rsync (fast after first run)
- Runs `meson compile` on the target board
- **Parses compiler/linker errors into structured JSON** for AI consumption
- Returns AI-friendly suggestions for fixing errors
- Includes `binary_path` and `run_command` on success for seamless chaining with run tools

### Run & Debug Tools (ROCK 3C)

| Tool | Description |
|------|-------------|
| `run_rock3c` | Run the built trap binary. Supports foreground (captures output) and background (returns PID) modes. |
| `stop_rock3c` | Stop a running trap by sending TERM or KILL signal. |
| `status_rock3c` | Check if a trap is running and get process info (PID, CPU, memory, uptime). |
| `logs_rock3c` | Fetch recent log output from journalctl or a log file. |
| `debug_rock3c` | Run a trap under GDB for crash analysis. Captures backtrace on segfault/abort. |
| `list_binaries_rock3c` | List built binaries with file sizes and modification times. |

### Structured Error Response

When a build fails, Cline/DeepSeek receives structured data:

```json
{
  "platform": "rock3c",
  "host": "rock-3c.local",
  "build_success": false,
  "compile_errors": [
    {
      "type": "compiler",
      "file": "src/hal/rock3c/camera.c",
      "line": 47,
      "column": 12,
      "severity": "error",
      "message": "implicit declaration of function 'v4l2_open'"
    }
  ],
  "linker_errors": [
    {
      "type": "linker",
      "file": "src/hal/rock3c/camera.c",
      "line": 47,
      "severity": "error",
      "message": "undefined reference to 'v4l2_open'",
      "symbol": "v4l2_open",
      "suggestion": "Missing implementation or library for 'v4l2_open'"
    }
  ],
  "warnings": [],
  "suggestions": [
    "Missing #include in src/hal/rock3c/camera.c:47",
    "Linker error: 'v4l2_open' not defined. Check library linkage or implement missing function."
  ],
  "binary_path": null,
  "run_command": null,
  "raw_output_preview": "... compiler output ..."
}
```

### Structured Crash Response

When `debug_rock3c` captures a crash, Cline/DeepSeek receives structured data:

```json
{
  "platform": "rock3c",
  "host": "rock-3c.local",
  "trap_name": "rock3c-imx219",
  "binary_path": "~/ai-camera-trap/build-rock3c/src/traps/rock3c-imx219/rock3c-imx219",
  "crashed": true,
  "crash_info": {
    "signal": "SIGSEGV",
    "signal_description": "Segmentation fault",
    "fault_address": "0x1234",
    "backtrace": [
      {
        "frame": 0,
        "function": "process_frame",
        "file": "src/traps/rock3c-imx219/capture.c",
        "line": 142
      },
      {
        "frame": 1,
        "function": "main",
        "file": "src/traps/rock3c-imx219/main.c",
        "line": 67
      }
    ]
  },
  "message": "Process crashed with SIGSEGV. Captured 2 backtrace frames."
}
```

### Typical Workflow

1. **Build**: `build_rock3c(trap_name="rock3c-imx219")` → returns binary_path and run_command
2. **Run**: `run_rock3c(trap_name="rock3c-imx219", background=true)` → returns PID
3. **Monitor**: `status_rock3c(trap_name="rock3c-imx219")` → returns running state, memory, uptime
4. **Logs**: `logs_rock3c(trap_name="rock3c-imx219", lines=100)` → returns recent output
5. **Debug**: `debug_rock3c(trap_name="rock3c-imx219")` → runs under GDB, captures crash info
6. **Stop**: `stop_rock3c(trap_name="rock3c-imx219")` → kills the process

## IntelliJ External Tools (Optional)

Add external tools in IntelliJ for one-click builds:

1. `Settings → Tools → External Tools → Add`

| Field | Value |
|-------|-------|
| Name | Build ROCK 3C |
| Program | `/Users/steve/.local/bin/remote-build.sh` |
| Arguments | `rock3c` |
| Working directory | `$ProjectFileDir$` |

## Running on Board (Manual)

After a successful build, run the trap on the board:

```bash
ssh radxa@rock-3c.local 'cd ~/ai-camera-trap && sudo ./build-rock3c/src/traps/rock3c-imx219/rock3c-imx219'
```

Or use the MCP `run_rock3c` tool for structured output and crash detection.

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `rsync: command not found` | `brew install rsync` |
| Permission denied (SSH) | Run `ssh-copy-id` again, check board IP |
| Meson not found on board | `sudo apt install meson ninja-build` |
| GDB not found on board | `sudo apt install gdb` |
| Build directory platform mismatch | Use `--rebuild` flag |
| Cline can't find tool | Check path in `cline_mcp_settings.json`, restart Cline |
| Binary not found when running | Build first with `build_rock3c` |

## Future: CI/CD Migration

These scripts are designed to be easily adapted for GitHub Actions self-hosted runners:

```yaml
# .github/workflows/build-rock3c.yml (future)
name: Build ROCK 3C
on:
  push:
    tags: ['v*']
jobs:
  build:
    runs-on: [self-hosted, rock3c]
    steps:
      - uses: actions/checkout@v4
      - name: Build
        run: ./scripts/remote-build.sh rock3c
```
