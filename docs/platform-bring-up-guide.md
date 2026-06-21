# AI Camera Trap — Platform Bring-Up Guide

## Overview

This guide documents the complete process of taking a bare-board ROCK 3C or Cubie A7S and turning it into a fully running AI Camera Trap detection platform. The same macOS cross-compilation toolchain supports both platforms, with platform-specific differences noted throughout.

### Supported Platforms

| Platform | SoC | NPU | Model Format | Camera HAL |
|----------|-----|-----|-------------|------------|
| Radxa ROCK 3C | RK3566 (ARM Cortex-A55) | Rockchip NPU (RKNN) | `.rknn` | `camera_hal_imx219` / `camera_hal_imx415` |
| Radxa Cubie A7S | Allwinner A527 (ARM Cortex-A55) | Vivante VIPLite (BPU) | `.nbg` | `camera_hal_a7s` (V4L2) |

### Development Workflow

```
macOS (Clang + Meson + Ninja)
  ├─ Cross-compile toolkit (libai-trap-toolkit.a)
  ├─ Cross-compile target (ai-trap-detection)
  ├─ Convert ONNX → NPU model format
  └─ SCP binary + config + model → board
```

---

## 1. Prerequisites

### macOS Development Machine

```bash
# Install toolchain
brew install llvm meson ninja pkg-config

# Add LLVM to PATH (add to ~/.zshrc)
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"

# Verify
clang --version    # Should show Homebrew clang version 22+
meson --version    # Should show 1.0+
ninja --version    # Should show 1.0+
```

### Target Boards

- Board with Radxa OS (Debian Bullseye-based) installed and booted
- SSH access configured (`ssh rock3c` / `ssh cubie-a7s`)
- Internet access for apt package installation
- For Cubie A7S: NPU kernel module must be active (`/dev/vipcore`)

---

## 2. Board Setup

Copy the platform-specific setup script and shared library to the board, then execute:

### ROCK 3C

```bash
scp tools/scripts/setup-rock3c.sh tools/scripts/lib/setup-common.sh rock3c:/tmp/
ssh rock3c "sudo bash /tmp/setup-rock3c.sh"
```

This installs:
- Runtime dependencies (libsqlite3, libjpeg, libpng, yaml-cpp, v4l-utils)
- RKNPU2 runtime (`rknpu2-rk356x`)
- Camera overlays (OV5647 / IMX219 / IMX415)
- Runtime directory structure (`/var/lib/ai-trap/`, `/etc/ai-trap/`, `/usr/share/ai-trap/models/`)
- Systemd service (disabled by default)
- Bluetooth

**Note:** On ROCK 3C (Bullseye), the package `libyaml-cpp0.6` is the correct name, not `libyaml-cpp0.8`.

### Cubie A7S

```bash
scp tools/scripts/setup-cubie-a7s.sh tools/scripts/lib/setup-common.sh cubie-a7s:/tmp/
ssh cubie-a7s "echo 'radxa' | sudo -S bash /tmp/setup-cubie-a7s.sh"
```

This installs:
- Runtime dependencies (libsqlite3, libjpeg, libpng, yaml-cpp, v4l-utils, bluez)
- OpenCL ICD loader (for Mali G57 GPU video scaling)
- Camera overlay (IMX415 via sunxi-vin)
- Runtime directory structure
- Systemd service (disabled)
- Bluetooth

**Note:** The `sudo` password is required. The default Radxa OS user is `radxa`.

#### NPU Kernel Module (Cubie A7S Only)

After the setup script, verify the NPU kernel module is loaded:

```bash
ssh cubie-a7s "ls -la /dev/vipcore"
```

Expected output:
```
crw-rw-rw- 1 root root 199, 0 ... /dev/vipcore
```

If missing, the NPU kernel module needs to be patched/loaded (this is a manual step on some kernel versions).

---

## 3. Sysroot Extraction

The sysroot provides the target's C/C++ headers and libraries to the macOS cross-compiler.

### Extract from Live Board

```bash
# ROCK 3C
./tools/native/setup-sysroot.sh rock3c root@rock3c

# Cubie A7S
./tools/native/setup-sysroot.sh cubie-a7s radxa@cubie-a7s
```

The script:
1. SSHes into the board
2. Archives and copies `/usr/include/`, `/usr/lib/aarch64-linux-gnu/`, `/lib/aarch64-linux-gnu/`
3. Creates a pkg-config wrapper (`aarch64-pkg-config`)
4. Verifies essential headers and libraries

**Common Issues:**

| Issue | Solution |
|-------|----------|
| SSH times out copying large libraries | Use `rsync` instead of `scp` for large files |
| C++ headers missing (`vector`, `array` not found) | Install `libstdc++-10-dev` on the board and recopy headers |
| `c++config.h` not found | Copy from `/usr/include/aarch64-linux-gnu/c++/10/` (multi-arch path) |
| Linker errors: `cannot find -lm`, `-lpthread`, `-lc` | Create `.so` symlinks from versioned `.so.6` files in the sysroot |
| Linker errors: `undefined symbol: __libc_csu_init` | Copy `libc_nonshared.a` from the board to the sysroot |
| Linker errors: `cannot open Scrt1.o` | Copy CRT objects (`crt1.o`, `crti.o`, `crtn.o`, `Scrt1.o`) from `/usr/lib/aarch64-linux-gnu/` and GCC runtime objects from `/usr/lib/gcc/aarch64-linux-gnu/10/` |

### Cross-Compilation Config Files

The Meson cross-compilation files are at:

- `tools/native/cross/aarch64-rock3c.ini`
- `tools/native/cross/aarch64-cubie-a7s.ini`

Key differences:
- ROCK 3C targets `armv8-a` (RK3566)
- Cubie A7S targets `armv8.2-a+fp16+rcpc` (A527 with FP16 and RCpc extensions)
- Cubie A7S requires `-isystem` flags to locate GCC C++ headers in the sysroot

---

## 4. Cross-Compilation

### Build the Toolkit and Target

```bash
# Build everything (toolkit + target)
./tools/native/build.sh rock3c all
./tools/native/build.sh cubie-a7s all

# Build individual components
./tools/native/build.sh rock3c toolkit    # libai-trap-toolkit.a only
./tools/native/build.sh rock3c target     # ai-trap-detection only

# Debug build
./tools/native/build.sh rock3c all --buildtype=debug
```

### Build Output

| File | Location | Size |
|------|----------|------|
| Toolkit library | `build-{platform}/toolkit/libai-trap-toolkit.a` | ~2.6 MB |
| Target binary | `build-{platform}/target/ai-trap-detection` | ~1 MB (AArch64 ELF) |

### Platform Compile-Time Defines

The build system (`meson.build`) sets platform-specific `#define` flags:

| Define | ROCK 3C | Cubie A7S |
|--------|---------|-----------|
| `HAVE_RKAIQ` | ✓ | ✗ |
| `HAVE_RKNN` | ✓ | ✗ |
| `HAVE_RGA` | ✓ | ✗ |
| `HAVE_A7S` | ✗ | ✓ |
| `HAVE_BPU` | ✗ | ✗ (future) |

These control which camera HAL implementations and inference backends are compiled in.

### Common Build Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `ERROR: Unknown linker(s): [['llvm-ar']]` | `llvm-ar` not in PATH | `export PATH="/opt/homebrew/opt/llvm/bin:$PATH"` |
| `fatal error: 'vector' file not found` | C++ headers missing from sysroot | Install `libstdc++-10-dev` on board, recopy headers |
| `fatal error: 'array' file not found` | Same (header not found) | Install `libstdc++-10-dev` on board, recopy — also add `-isystem` paths for multi-arch C++ headers |
| `fatal error: 'jpeglib.h' file not found` | libjpeg dev headers missing | `sudo apt install libjpeg62-turbo-dev` on board, recopy |
| `fatal error: 'png.h' file not found` | libpng dev headers missing | `sudo apt install libpng-dev` on board, recopy |
| `fatal error: 'systemd/sd-bus.h'` | libsystemd dev headers missing | `sudo apt install libsystemd-dev` on board, recopy |
| `fatal error: 'CL/cl.h'` | OpenCL headers missing | `sudo apt install ocl-icd-opencl-dev` on board, recopy |
| `ld.lld: error: unable to find library -lm` | No `.so` symlink for libm | `ln -sf libm.so.6 libm.so` in sysroot |
| `ld.lld: error: undefined symbol: __libc_csu_init` | Missing `libc_nonshared.a` | Copy from board `/usr/lib/aarch64-linux-gnu/libc_nonshared.a` to sysroot |
| `ld.lld: error: cannot open Scrt1.o` | Missing CRT startup objects | Copy `Scrt1.o`, `crti.o`, `crtn.o` from `/usr/lib/aarch64-linux-gnu/` to sysroot |
| `ld.lld: error: undefined symbol: sqlite3_prepare_v2` | SQLite library not linked | Add `libsqlite3.so` to link args (for cross-compilation, pkg-config may not work) |

---

## 5. Model Preparation

The model pipeline is: **PyTorch → ONNX → NPU-specific format**

### Training (for both platforms)

```bash
python models/detection/insects/yolo26n/train_yolo26n.py
```

This produces `yolo26n.pt` and `best.pt` in `models/detection/insects/yolo26n/`.

### Export to ONNX

```python
from ultralytics import YOLO
model = YOLO("yolo26n.pt")
model.export(format="onnx", imgsz=320, end2end=False)  # NMS-free for NPU
```

This produces `best.onnx` (9.7 MB, 320×320 input).

**Important:** For YOLO26n, `end2end=False` is the default and produces NMS-free output — ideal for NPU deployment. The model generates predictions directly without requiring Non-Maximum Suppression as a post-processing step.

### Convert to NPU Format

#### ROCK 3C: ONNX → RKNN

**Option A: During training (macOS, recommended)**
```bash
pip install rknn-toolkit2 ultralytics
python models/detection/insects/yolo26n/train_yolo26n.py --rknn --rknn-target rk3566
```

**Option B: On-board conversion**
```bash
scp models/detection/insects/yolo26n/best.onnx rock3c:/usr/share/ai-trap/
ssh rock3c "cd /usr/share/ai-trap && python3 convert_rknn_onboard.py"
```
This requires `rknpu2-rk356x` and `rknn-toolkit2` installed on the board.

**Option C: Using the conversion script**
```bash
python models/detection/insects/yolo26n/convert/convert_rknn.py \
  --input models/detection/insects/yolo26n/best.onnx \
  --output models/detection/insects/yolo26n/yolo26n.rknn
```

**Note:** The RKNN model file (`yolo26n.rknn`, ~1.4 MB) is gitignored.

#### Cubie A7S: ONNX → NBG

The Cubie A7S uses the Vivante VIPLite NPU which requires the `.nbg` (Network Binary Graph) format. Conversion requires the ACUITY Toolkit from Allwinner/VeriSilicon.

**Using the Docker container (on x86_64 macOS via Rosetta 2):**

```bash
# The Docker image is bundled in the repo
cd docker_images_v2.0.x
unzip ubuntu-npu_v2.0.10.2.tar.zip
docker load -i ubuntu-npu_v2.0.10.2.tar
# Image: ubuntu-npu:v2.0.10.2 (ACUITY v6.30.22, for A733 NPU)

# Run conversion
docker run --rm -v $(pwd)/models/detection/insects/yolo26n:/data \
  ubuntu-npu:v2.0.10.2 \
  bash -c "source /root/acuity-toolkit-whl-*/bin/activate && \
    python3 convert.py --model /data/best.onnx --target A733 --output /data/yolo26n.nbg"
```

**Note:** The ACUITY Docker image only runs on x86_64 hosts (will work on macOS via Rosetta 2 emulation). The ARM64 NPU runtime libraries (`libOpenVX.so`, `libGAL.so`, `libovxlib.so`, `libVSC.so`, `libCLC.so`) inside the container are in `/workspace/common/lib/` and can be extracted for the board:

```bash
# Extract ARM64 NPU libraries from the Docker image layers
python3 << 'EOF'
import tarfile, json, os, shutil

t = tarfile.open('docker_images_v2.0.x/ubuntu-npu_v2.0.10.2.tar')
manifest = json.load(t.extractfile([m for m in t.getmembers() if m.name == 'manifest.json'][0]))
blobs = manifest[0].get('Layers', [])

for blob_path in blobs:
    try:
        blob_tar = tarfile.open(fileobj=t.extractfile(t.getmember(blob_path)))
        for m in blob_tar.getmembers():
            if m.isfile() and 'lib' in m.name and (m.name.endswith('.so') or '.so.' in m.name):
                f = os.path.basename(m.name)
                if any(f.startswith(p) for p in ['libOpenVX', 'libGAL', 'libovx', 'libVSC', 'libCLC', 'libvsi']):
                    blob_tar.extract(m, '/tmp/npu-libs')
        blob_tar.close()
    except:
        pass
EOF

# Copy to board
scp /tmp/npu-libs/root/Vivante_IDE/VivanteIDE5.11.0/cmdtools/vsimulator/lib/*.so* \
  cubie-a7s:/usr/lib/
```

---

## 6. Deployment

### Step 1: Copy the Binary

```bash
# ROCK 3C
scp build-rock3c/target/ai-trap-detection rock3c:/usr/local/bin/

# Cubie A7S
scp build-cubie-a7s/target/ai-trap-detection cubie-a7s:/tmp/
ssh cubie-a7s "echo 'radxa' | sudo -S cp /tmp/ai-trap-detection /usr/local/bin/ && sudo chmod +x /usr/local/bin/ai-trap-detection"
```

### Step 2: Copy the Model

```bash
# ROCK 3C (RKNN)
scp models/detection/insects/yolo26n/yolo26n.rknn rock3c:/usr/share/ai-trap/

# Cubie A7S (NBG)
scp models/detection/insects/yolo26n/yolo26n.nbg cubie-a7s:/usr/share/ai-trap/
```

### Step 3: Copy Config

```bash
# ROCK 3C — select camera type:
scp traps/runtimes/rock-3c/config.ov5647.yaml rock3c:/etc/ai-trap/config.yaml  # OV5647 (RPi Cam v1.3)
scp traps/runtimes/rock-3c/config.imx219.yaml rock3c:/etc/ai-trap/config.yaml   # IMX219 (RPi Cam v2)
scp traps/runtimes/rock-3c/config.imx415.yaml rock3c:/etc/ai-trap/config.yaml   # IMX415 (Radxa Camera 4K)

# ROCK 3C — scene actor testing (no camera needed):
scp traps/runtimes/rock-3c/config.scene.yaml rock3c:/etc/ai-trap/config.yaml

# Cubie A7S — standard V4L2 camera config:
scp traps/targets/config/cubie-a7s.yaml cubie-a7s:/etc/ai-trap/config.yaml

# Cubie A7S — scene actor testing (no camera needed):
scp traps/runtimes/rock-3c/config.scene_cubiea7s.yaml cubie-a7s:/etc/ai-trap/config.yaml
```

### Step 4: Copy Scene Frames (for Scene Actor Testing)

```bash
# On the board:
mkdir -p /usr/share/ai-trap/scenes/insect_loop_4

# From macOS:
scp traps/runtimes/rock-3c/scenes/insect_loop_4/* cubie-a7s:/usr/share/ai-trap/scenes/insect_loop_4/
```

### Step 5: Run the Detection Pipeline

```bash
# Basic run (uses /etc/ai-trap/config.yaml by default)
ssh rock3c "/usr/local/bin/ai-trap-detection"

# Run with explicit config
ssh rock3c "/usr/local/bin/ai-trap-detection --config /etc/ai-trap/config.yaml"

# Run as systemd service (after enabling)
ssh rock3c "sudo systemctl enable ai-trap-detection && sudo systemctl start ai-trap-detection"
```

### Step 6: Verify

```bash
# Test HTTP endpoints
curl http://rock3c.local:8080/status
curl -N http://rock3c.local:8080/events

# View MJPEG stream (open in browser)
open http://rock3c.local:8080/stream.mjpg

# Check logs
ssh rock3c "journalctl -u ai-trap-detection -f"
```

---

## 7. Scene Actor Testing

The scene actor is a virtual camera that reads pre-generated PNG frames from a directory, enabling testing without physical camera hardware.

### When to Use

- Development and debugging
- CI/CD testing
- Reproducible test scenarios
- Verifying pipeline components (cropping, inference, storage, HTTP)

### How It Works

The scene actor (`SceneCameraActor` / `scene_camera_actor.cpp`) is selected when `camera.model: "scene"` in the config. It:
1. Reads PNG files from `scene_dir`
2. Loops through them at the configured `fps`
3. Presents them to the pipeline like a real camera

### Preparing Scene Frames

Use 1920×1080 PNG images. Frame filenames must be `frame_%04d.png` (e.g., `frame_0000.png`, `frame_0001.png`).

**Scene frame generation:**
```bash
# From test data directory
python3 -c "
import cv2, os
os.makedirs('/tmp/ai-trap-scenes', exist_ok=True)
for i in range(100):
    frame = cv2.imread(f'source_frames/{i:04d}.jpg')
    frame = cv2.resize(frame, (1920, 1080))
    cv2.imwrite(f'/tmp/ai-trap-scenes/frame_{i:04d}.png', frame)
"
```

300 pre-generated scene frames are provided at `traps/runtimes/rock-3c/scenes/insect_loop_4/`.

---

## 8. Platform Comparison Summary

| Step | ROCK 3C | Cubie A7S |
|------|---------|-----------|
| **SSH user** | `root` | `radxa` |
| **Board setup script** | `setup-rock3c.sh` | `setup-cubie-a7s.sh` |
| **Sysroot command** | `setup-sysroot.sh rock3c root@rock3c` | `setup-sysroot.sh cubie-a7s radxa@cubie-a7s` |
| **Build command** | `build.sh rock3c all` | `build.sh cubie-a7s all` |
| **NPU format** | RKNN (`.rknn`) | NBG (`.nbg`) |
| **NPU conversion** | Ultralytics `export(rknn)` or on-board `convert_rknn_onboard.py` | ACUITY Docker container (`ubuntu-npu:v2.0.10.2`) |
| **NPU runtime libs** | `rknpu2-rk356x` package (from Radxa APT) | Need to extract from ACUITY container or install BSP packages |
| **Camera HAL** | AIQ-based (imx219 / imx415) | V4L2-based (CameraHalA7s auto-detects `/dev/video*`) |
| **Video scaler** | RGA hardware | OpenCL on Mali G57 GPU |
| **C++ headers path** | Standard `/usr/include/c++/10/` | Multi-arch: `/usr/include/aarch64-linux-gnu/c++/10/bits/c++config.h` |

---

## 9. Troubleshooting

### Binary starts but fails with pipeline errors

```
[CameraHalA7s] No V4L2 capture device found
```

The camera is not connected or the camera overlay is not enabled. Either:
- Connect the camera and enable the overlay (reboot required)
- Switch to scene actor mode: use `config.scene.yaml` / `config.scene_cubiea7s.yaml`

```
[InferenceHalVIP] Failed to load NBG graph
```

The NBG model file is missing or incompatible. Verify the model exists and matches the A733 target:
```bash
ssh cubie-a7s "ls -la /usr/share/ai-trap/yolo26n.nbg"
# Expected: at least a few hundred KB, non-zero
```

```
ld.lld: error: undefined symbol: __libc_csu_init
```

`libc_nonshared.a` is missing from the sysroot. Copy from the board:
```bash
scp cubie-a7s:/usr/lib/aarch64-linux-gnu/libc_nonshared.a \
  tools/native/sysroots/cubie-a7s/usr/lib/aarch64-linux-gnu/
```

### Docker: "repository does not exist or may require 'docker login'"

The public Docker image `zifengzp/ubuntu-npu:v2.0.10.1` may be restricted. Use the bundled Docker tarball instead:
```bash
cd docker_images_v2.0.x
unzip ubuntu-npu_v2.0.10.2.tar.zip
docker load -i ubuntu-npu_v2.0.10.2.tar
```

### Arm64 NPU Libraries Missing on Cubie A7S

The NPU kernel module (`/dev/vipcore`) is loaded but userspace libraries are missing. The Vivante SDK libraries must be installed from Radxa BSP packages or extracted from the ACUITY container's `/workspace/common/lib/` directory. Required libraries:
- `libOpenVX.so` — OpenVX runtime
- `libGAL.so` — Graphics Abstraction Layer
- `libovxlib.so` — OpenVX extensions library
- `libVSC.so` — VSC runtime
- `libCLC.so` — CLC runtime

---

## 10. File Reference

| File | Purpose |
|------|---------|
| `tools/scripts/setup-rock3c.sh` | ROCK 3C board setup (run on board) |
| `tools/scripts/setup-cubie-a7s.sh` | Cubie A7S board setup (run on board) |
| `tools/scripts/lib/setup-common.sh` | Shared library for board setup scripts |
| `tools/native/setup-sysroot.sh` | Extract sysroot from live board |
| `tools/native/build.sh` | Cross-compilation entry point |
| `tools/native/cross/aarch64-rock3c.ini` | Meson cross-compilation config for ROCK 3C |
| `tools/native/cross/aarch64-cubie-a7s.ini` | Meson cross-compilation config for Cubie A7S |
| `models/detection/insects/yolo26n/train_yolo26n.py` | YOLO26n training script |
| `models/detection/insects/yolo26n/convert/convert_rknn.py` | ONNX → RKNN conversion script |
| `models/detection/insects/yolo26n/convert/convert_nbg.py` | ONNX → NBG conversion script (uses ACUITY) |
| `models/detection/insects/yolo26n/convert/convert_nbg_docker.sh` | Docker-based NBG conversion |
| `models/detection/insects/yolo26n/convert/Dockerfile.nbg` | Dockerfile for NBG conversion container |
| `traps/targets/config/cubie-a7s.yaml` | Cubie A7S runtime config (V4L2 camera + BPU) |
| `traps/runtimes/rock-3c/config.scene.yaml` | ROCK 3C scene-actor config |
| `traps/runtimes/rock-3c/config.scene_cubiea7s.yaml` | Cubie A7S scene-actor config |
| `docker_images_v2.0.x/ubuntu-npu_v2.0.10.2.tar.zip` | Bundled ACUITY NPU Docker image |
| `docker_images_v2.0.x/run_npu_docker.sh` | Helper to run NPU Docker container |