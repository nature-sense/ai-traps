# AI Camera Trap

Real-time insect detection and classification on edge hardware.

AI-powered camera traps for ecological monitoring of insect populations. Captures video, detects insects using YOLO models on embedded NPUs, classifies species, and provides remote monitoring via a mobile app.

## Architecture

```
Camera → MotionSensor → Inference → Tracker → Decision → Cropper → JPEG Encoder → Storage
                                                               ↓
                                                          Classifier → Storage (classified)
```

The system uses an **actor-based pipeline architecture** built on the [Ramen framework](traps/toolkit/src/actors/ramen.hpp). Each pipeline stage is an independent actor communicating via typed push/pull ports. Platform-specific hardware is abstracted through a Hardware Abstraction Layer (HAL).

### Monorepo Structure

```
ai-traps/
├── traps/
│   ├── toolkit/              # C++ actor framework + HAL abstractions
│   │   ├── src/actors/       # 20+ pipeline actors
│   │   ├── src/hal/api/      # Abstract HAL interfaces
│   │   └── src/hal/platforms/ # Platform implementations (rock3c, cubie-a7s, host)
│   └── targets/              # Platform-specific detection binaries
│       └── src/detection/
│           ├── actors/       # Detection-specific actors (HTTP, session)
│           ├── pipeline/     # Base + platform-specific pipelines
│           └── platforms/    # Entry points (rock3c, cubie-a7s, native)
├── apps/
│   └── ai_trap_manager/      # Flutter mobile app (iOS/macOS)
├── models/
│   └── detection/insects/yolo26n/  # YOLO training + model conversion
└── tools/
    └── mcp/                  # MCP servers for AI-assisted development
```

## Supported Platforms

| Platform | Hardware | NPU | Camera | Status |
|----------|----------|-----|--------|--------|
| **ROCK 3C** | Radxa ROCK 3C (RK3566) | Rockchip RKNN | IMX415 / IMX219 | ✅ Working |
| **Cubie A7S** | Allwinner A527 | Vivante VIPLite | V4L2 ISP | 🚧 In progress |
| **RDK X5** | D-Robotics RDK X5 | — | — | 📋 Planned |
| **Host** | macOS / Linux | Stub | Stub | ✅ Development |

## Quick Start

### Prerequisites

```bash
# macOS
brew install meson ninja pkg-config ccache sqlite3 jpeg-turbo libpng

# Linux (Debian/Ubuntu)
sudo apt install meson ninja-build pkg-config ccache libsqlite3-dev libjpeg-turbo8-dev libpng-dev
```

### Build for Host (macOS/Linux)

```bash
# Build the toolkit static library
cd traps/toolkit
meson setup build -Dplatform=host
meson compile -C build

# Build the detection target
cd ../targets
meson setup build -Dplatform=host
meson compile -C build
```

### Build for ROCK 3C (via SSH)

```bash
# Rsync source to board, then SSH in
ssh radxa@rock-3c.local
cd ~/ai-traps/traps/toolkit
meson setup build-rock3c -Dplatform=rock3c
meson compile -C build-rock3c

cd ~/ai-traps/traps/targets
meson setup build-rock3c -Dplatform=rock3c
meson compile -C build-rock3c
```

Or use the MCP build tools (see [tools/mcp/](tools/mcp/README.md)).

## Pipeline Actors

| Actor | Description |
|-------|-------------|
| **CameraActor** | Captures frames from camera hardware via HAL |
| **MotionSensorActor** | PIR motion sensor integration |
| **InferenceActor** | YOLO object detection via NPU (HAL abstraction) |
| **TrackerActor** | ByteTracker-based multi-object tracking |
| **DecisionActor** | Trigger logic for capture decisions |
| **CropperActor** | Extracts JPEG crops of detected objects |
| **JPEGEncoderActor** | Hardware/software JPEG encoding |
| **OverlayActor** | Draws bounding boxes on preview stream |
| **MJPEGBridgeActor** | Serves MJPEG stream over HTTP |
| **ClassifierActor** | Optional species classification (HAL) |
| **BestShotKeeperActor** | Selects best frame per track |
| **ActuatorActor** | Controls relays/servos for physical intervention |
| **HttpSseActor** | CivetWeb-based HTTP/SSE/MJPEG server |
| **HttpHandlerActor** | REST API handler (OpenAPI interface) |
| **SessionActor** | Session lifecycle + classification storage |
| **EventPublisherActor** | Generic event dispatch (SSE, MQTT, etc.) |

## Model Pipeline

The project uses YOLO26n for insect detection. The model training and conversion pipeline is in [models/detection/insects/yolo26n/](models/detection/insects/yolo26n/).

```
Training (PyTorch) → ONNX export → Platform conversion → Deploy to target
                                        ├── RKNN (ROCK 3C)
                                        └── NBG  (Cubie A7S)
```

See [models/detection/insects/yolo26n/convert/README.md](models/detection/insects/yolo26n/convert/README.md) for conversion details.

## Mobile App

The [Flutter mobile app](apps/ai_trap_manager/) provides:
- Real-time monitoring via MJPEG stream
- Session management (start/stop monitoring)
- Detection history with crop images
- System metrics (CPU, memory, FPS)
- BLE and WebSocket connectivity
- Trap provisioning and configuration

## Development Tools

### MCP Servers

Two MCP servers enable AI-assisted development:

- **[ai-trap-build](tools/mcp/README.md)** — Build, run, debug, and monitor traps on remote boards via SSH
- **[trap-ops](tools/mcp/README.md)** — Query trap REST API for status, sessions, detections, and metrics

### Actor Diagram

Generate a visual diagram of the actor pipeline:

```bash
python tools/mcp/scripts/generate_actor_diagram.py
```

Output: `tools/mcp/scripts/actor_diagram.pdf`

## Build System

The project uses **Meson** with platform selection via `-Dplatform=`:

```bash
# Available platforms: host, rock3c, cubie-a7s, rdk-x5
meson setup build -Dplatform=<platform>
meson compile -C build
```

The toolkit builds as a static library (`libai-trap-toolkit.a`) that the detection target links against. Both must be built with the same platform flag.

## Status

The toolkit and detection target build cleanly on macOS, ROCK 3C, and Cubie A7S with zero warnings and zero errors. The IMX415 camera HAL is verified working on ROCK 3C hardware. The HTTP server infrastructure (REST, SSE, MJPEG) is fully functional. The Cubie A7S platform has a complete V4L2-based camera HAL and Vivante VIPLite inference HAL (untested on real hardware).

See [memory-bank/progress.md](memory-bank/progress.md) for detailed status and known issues.

## License

Copyright 2026 Nature Sense

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

See the [LICENSE](LICENSE) file for details.
