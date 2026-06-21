# ROCK 3C Deployment Guide

## Overview

Deploy the AI Trap detection pipeline to a Radxa ROCK 3C board. The pipeline uses:
- **Camera**: OV5647 (RPi Camera v1.3), IMX219 (RPi Camera v2), or IMX415 (Radxa Camera 4K)
- **Inference**: YOLO26n detection model running on RK3566 NPU via RKNN
- **Streaming**: MJPEG over HTTP
- **Management**: REST API + SSE events

## Prerequisites

- ROCK 3C board with Radxa OS installed
- SSH access to the board
- macOS developer machine with Clang cross-compilation setup

## Quick Deploy (Build + SCP)

```bash
# 1. Build and populate runtime bundle
./tools/native/build.sh rock3c all --populate-runtime

# 2. Copy to board
scp -r traps/runtimes/rock-3c/* root@rock-3c.local:/usr/share/ai-trap/

# 3. On the board, select config and run
ssh root@rock-3c.local
cp /usr/share/ai-trap/config.ov5647.yaml /etc/ai-trap/config.yaml
sudo /usr/share/ai-trap/ai-trap-detection --config /etc/ai-trap/config.yaml
```

## Model Export

### Option A: Export during training (macOS with GPU, no board needed)

```bash
# After training, export ONNX + RKNN in one step:
python models/detection/insects/yolo26n/train_yolo26n.py --rknn --rknn-target rk3566
```

This requires `pip install rknn-toolkit2 ultralytics` on the machine. Ultralytics handles
the RKNN conversion internally, including pruning the final detection layer, INT8
quantization, and target platform selection.

### Option B: On-board conversion (requires SSH + rknn-toolkit2 on board)

```bash
python3 /usr/share/ai-trap/convert_rknn_onboard.py
```

## Usage

```
ai-trap-detection [--config /etc/ai-trap/config.yaml]
```

If `--config` is not provided, built-in defaults are used (OV5647 camera, yolo26n model).

## Config Files

| Camera | Config File | Notes |
|--------|-------------|-------|
| OV5647 (RPi Camera v1.3) | `config.ov5647.yaml` | Default |
| IMX219 (RPi Camera v2) | `config.imx219.yaml` | Switch device |
| IMX415 (Radxa Camera 4K) | `config.imx415.yaml` | Switch device |
| Scene actor (testing) | `config.scene.yaml` | Pre-generated frames, no camera needed |

## Testing

```bash
# Check status
curl http://rock-3c.local:8080/status

# View stream
open http://rock-3c.local:8080/stream.mjpg

# Subscribe to events
curl -N http://rock-3c.local:8080/events
```

## Model Notes

### YOLO26n (Primary)
- Trained via `models/detection/insects/yolo26n/train_yolo26n.py`
- RKNN ~1.4MB, 320×320 input, single class (insect)
- Ultralytics native `export(format='rknn')` handles:
  - Pruning the final detection layer → raw DFL output (65 channels)
  - INT8 quantization
  - Target platform (rk3566) optimization
- C++ inference HAL (`inference_hal_rknn.cpp`) decodes the raw DFL output:
  - Softmax over 16 regression channels per bounding box edge
  - Sigmoid over class scores
  - Hardcoded strides for 320×320 (8/16/32) with anchor grid layout (40×40, 20×20, 10×10)

### YOLOv8n (Alternative)
- Smaller model, ~0.90MB RKNN
- Same 320×320 input, 4 classes
- Also exported via Ultralytics `export(format='rknn')`
- Switch config `model_path` to use instead of yolo26n