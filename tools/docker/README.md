# Docker Cross-Compilation Build System

This directory contains Docker-based cross-compilation environments for building
AI Camera Trap binaries for all supported platforms.

## Why Docker?

- **No board required** — compile offline on your Mac
- **Fast** — native x86_64 cross-compilation (not emulated)
- **Consistent** — identical build environment for all developers
- **Git repository sharing** — the source is mounted directly, no git bundles

## Quick Start

### 1. Build the Docker images

```bash
# Build all platform images
docker compose -f tools/docker/docker-compose.yml build

# Or build a specific platform
docker compose -f tools/docker/docker-compose.yml build rock3c
```

### 2. Cross-compile

```bash
# Build everything for rock3c
docker compose -f tools/docker/docker-compose.yml run --rm rock3c

# Build only the toolkit for cubie-a7s
docker compose -f tools/docker/docker-compose.yml run --rm cubie-a7s cubie-a7s toolkit

# Build only the target binary for rock3c
docker compose -f tools/docker/docker-compose.yml run --rm rock3c rock3c target
```

### 3. Deploy to a board

```bash
# Copy the binary and config to the board
scp build-rock3c/target/rock3c-* root@rock-3c.local:/usr/local/bin/
scp traps/targets/config/rock3c.yaml root@rock-3c.local:/etc/ai-trap/config.yaml
```

## Platform Images

| Image | Platform | SDK Stubs | Model Tools |
|-------|----------|-----------|-------------|
| `ai-trap-builder-rock3c` | ROCK 3C (RK3566) | RKAIQ, librga, MPP | rknn-toolkit2 |
| `ai-trap-builder-cubie-a7s` | Cubie A7S (A527) | OpenVX, OpenCL | — |
| `ai-trap-builder-rdk-x5` | RDK X5 (TDA4VM) | Linux headers only | — |

## SDK Stubs

The Docker images include **stub headers** for proprietary SDKs. These provide
just enough type information for compilation to succeed. The actual shared
libraries are loaded at runtime on the target board.

### Stub locations

- `tools/docker/sdk-stubs/rock3c/rkaiq/` — Rockchip AIQ (ISP 3A control)
- `tools/docker/sdk-stubs/rock3c/rga/` — Rockchip RGA (hardware scaler)
- `tools/docker/sdk-stubs/rock3c/rockchip/` — Rockchip MPP (video encoder)
- `tools/docker/sdk-stubs/cubie-a7s/VX/` — OpenVX (NPU inference)

### Adding real SDK headers

If you have access to the actual SDK headers (e.g., from a board), you can
mount them into the container:

```bash
docker run --rm \
  -v $(pwd):/workspace \
  -v /path/to/rkaiq:/usr/aarch64-linux-gnu/include/rkaiq \
  ai-trap-builder-rock3c rock3c
```

## Manual Docker Usage

```bash
# Build the rock3c image
docker build -t ai-trap-builder-rock3c \
  -f tools/docker/rock3c.Dockerfile \
  tools/docker

# Run a build
docker run --rm \
  -v $(pwd):/workspace \
  ai-trap-builder-rock3c \
  rock3c all detection
```

## Build Output

Build artifacts are written to `build-{platform}/` in the repository root:

```
build-rock3c/
├── toolkit/     # libtoolkit.a
└── target/      # rock3c-* binary

build-cubie-a7s/
├── toolkit/     # libtoolkit.a
└── target/      # cubie-a7s-* binary
```

## Troubleshooting

### "Permission denied" when writing build output

Ensure the container runs with your user ID:

```bash
docker run --rm \
  -v $(pwd):/workspace \
  --user $(id -u):$(id -g) \
  ai-trap-builder-rock3c rock3c
```

### Missing SDK headers

If compilation fails due to missing headers, you may need to:

1. Copy headers from a running board: `scp -r root@board:/usr/include/rkaiq ./`
2. Mount them into the container as shown above
3. Or update the stub headers in `tools/docker/sdk-stubs/`
