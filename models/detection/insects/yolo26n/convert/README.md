# Model Conversion: YOLO26n → RKNN & NBG

This directory contains scripts to convert a trained YOLO26n ONNX model into
target-specific formats for the camera trap hardware:

| Format | Target | NPU | Tool |
|--------|--------|-----|------|
| `.rknn` | ROCK 3C (RK3566) | Rockchip NPU | `rknn-toolkit2` |
| `.nbg`  | Cubie A7s (A527)  | Vivante VIPLite | ACUITY Toolkit |

Both conversion tools require the respective SDKs which are only available on
Linux. You have two options:

1. **Remote via SSH** (recommended) — run the conversion on the target board
   itself (ROCK 3C or Cubie A7s) via SSH. The boards already have the NPU SDKs.
2. **Docker** — run in an x86_64 Linux Docker container (requires x86 host).

---

## Option 1: Remote Conversion via SSH (Recommended)

This is the simplest approach. The script copies the ONNX model to the board,
runs the conversion there, and copies the result back.

### RKNN (ROCK 3C)

```bash
python convert_rknn.py --input ../best.onnx --output ../yolo26n.rknn \
    --remote radxa@rock-3c.local
```

### NBG (Cubie A7s)

```bash
python convert_nbg.py --input ../best.onnx --output ../yolo26n.nbg \
    --remote radxa@cubie-a7s.local
```

**Requirements:**
- SSH access to the board (passwordless key-based auth recommended)
- The board must have the conversion SDK installed:
  - ROCK 3C: `rknn-toolkit2` (Python package)
  - Cubie A7s: ACUITY Toolkit (proprietary, from VeriSilicon/Allwinner)

---

## Option 2: Docker (x86_64 Linux Host)

If you have an x86_64 Linux machine, you can build and run the Docker containers.

### RKNN

```bash
docker build -f Dockerfile.rknn -t rknn-converter .
docker run --rm -v $(pwd)/..:/models rknn-converter \
    --input /models/best.onnx --output /models/yolo26n.rknn
```

### NBG (macOS — using pre-built Docker image)

You have a pre-built Docker image `ubuntu-npu-with-acuity:latest` that has the
ACUITY Toolkit installed. Use the helper script to run the full 4-step pipeline:

```bash
# From the convert/ directory:
docker run --rm -v $(pwd)/..:/models ubuntu-npu-with-acuity:latest \
    bash /models/convert/convert_nbg_docker.sh
```

The script performs all four steps automatically:
1. **Import** ONNX → `yolo26n.json` + `yolo26n.data`
2. **Generate input metadata** → `yolo26n_inputmeta.yml`
3. **Quantize** (INT8 asymmetric_affine) → `yolo26n.quantize`
4. **Export NBG** for VIP9000 (Cubie A7s) → `nbg_output/network_binary.nb`

Output is written to `../nbg_output/` (i.e., `models/detection/insects/yolo26n/nbg_output/`).

> **Note:** The `convert_nbg_docker.sh` script is designed to run *inside* the
> container. It validates that the ONNX model and ACUITY Toolkit exist before
> proceeding.

---

## Full Pipeline

```bash
# 1. Train (produces best.pt + best.onnx)
cd ..
python train_yolo26n.py

# 2. Convert to RKNN (on ROCK 3C)
cd convert
python convert_rknn.py --input ../best.onnx --output ../yolo26n.rknn \
    --remote radxa@rock-3c.local

# 3. Convert to NBG (on Cubie A7s)
python convert_nbg.py --input ../best.onnx --output ../yolo26n.nbg \
    --remote radxa@cubie-a7s.local

# 4. Deploy to targets (if not already there)
scp ../yolo26n.rknn radxa@rock-3c.local:/usr/share/ai-trap/models/
scp ../yolo26n.nbg radxa@cubie-a7s.local:/usr/share/ai-trap/models/
```

---

## Options Reference

### `convert_rknn.py`

| Flag | Default | Description |
|------|---------|-------------|
| `--input` / `-i` | (required) | Path to ONNX model |
| `--output` / `-o` | `yolo26n.rknn` | Output RKNN path |
| `--target` | `rk3566` | Rockchip platform (`rk3566`, `rk3568`, `rk3588`, `rv1106`) |
| `--imgsz` | `320` | Model input size |
| `--no-quantize` | (off) | Disable INT8 quantization (use FP16) |
| `--remote` | (none) | SSH host for remote conversion (e.g., `radxa@rock-3c.local`) |

### `convert_nbg.py`

| Flag | Default | Description |
|------|---------|-------------|
| `--input` / `-i` | (required) | Path to ONNX model |
| `--output` / `-o` | `yolo26n.nbg` | Output NBG path |
| `--imgsz` | `320` | Model input size |
| `--no-quantize` | (off) | Disable INT8 quantization (use FP32) |
| `--toolkit-dir` | `/opt/acuity-toolkit` | ACUITY Toolkit installation path |
| `--remote` | (none) | SSH host for remote conversion (e.g., `radxa@cubie-a7s.local`) |

---

## Troubleshooting

### "rknn-toolkit2 is not installed"
The script needs the `rknn.api` Python module. Install it on the ROCK 3C:
```bash
pip install rknn-toolkit2
```
Or use `--remote` to run on the board.

### "ACUITY Toolkit not found"
The ACUITY Toolkit is proprietary. Install it on the Cubie A7s or use `--remote`.

### SSH connection refused
Ensure the board is reachable and SSH is running:
```bash
ping rock-3c.local
ssh radxa@rock-3c.local
```
