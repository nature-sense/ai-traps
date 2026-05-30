#!/usr/bin/env bash

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

# ==============================================================================
# Convert YOLO26n ONNX → NBG for Cubie A7s (Vivante VIPLite NPU)
#
# Runs inside the ubuntu-npu-with-acuity Docker container which has the
# ACUITY Toolkit (pegasus.py) pre-installed.
#
# Usage:
#   # From the host (macOS):
#   docker run --rm -v $(pwd)/..:/models ubuntu-npu-with-acuity:latest \
#       bash /models/convert/convert_nbg_docker.sh
#
#   # Or run this script directly inside the container:
#   bash /models/convert/convert_nbg_docker.sh
#
# Output:
#   ../nbg_output/network_binary.nb  — the NBG model for Cubie A7s
#   ../nbg_output/generated/         — intermediate files (JSON, data, C source, etc.)
# ==============================================================================

# Note: We don't use set -e because the NBG packing step may fail
# under Rosetta emulation (Apple Silicon). We handle that gracefully.
set -uo pipefail

MODEL_DIR="/models"
CONVERT_DIR="${MODEL_DIR}/convert"
INPUT_ONNX="${MODEL_DIR}/best.onnx"
OUTPUT_DIR="${MODEL_DIR}/nbg_output"
GEN_DIR="${OUTPUT_DIR}/generated"

ACUITY_BIN="/root/acuity-toolkit-whl-6.30.22/bin/pegasus.py"

echo "=========================================="
echo "YOLO26n → NBG Conversion for Cubie A7s"
echo "=========================================="
echo "Input ONNX:  ${INPUT_ONNX}"
echo "Output dir:  ${OUTPUT_DIR}"
echo "Generated:   ${GEN_DIR}"
echo "ACUITY:      ${ACUITY_BIN}"
echo ""

# ── Validate ──────────────────────────────────────────────────────────────────
if [ ! -f "${INPUT_ONNX}" ]; then
    echo "[ERROR] ONNX model not found: ${INPUT_ONNX}"
    echo "        Mount the models directory with: -v \$(pwd):/models"
    exit 1
fi

if [ ! -f "${ACUITY_BIN}" ]; then
    echo "[ERROR] ACUITY Toolkit not found at: ${ACUITY_BIN}"
    echo "        Make sure you're using the ubuntu-npu-with-acuity image."
    exit 1
fi

mkdir -p "${GEN_DIR}"
cd "${GEN_DIR}"

# ── Step A: Import ONNX → JSON + data ────────────────────────────────────────
echo ""
echo "[Step A] Importing ONNX model..."
python3 "${ACUITY_BIN}" import onnx \
    --model "${INPUT_ONNX}" \
    --output-model yolo26n.json \
    --output-data yolo26n.data

echo "[Step A] Done: yolo26n.json + yolo26n.data"

# ── Step B: Generate input metadata ───────────────────────────────────────────
echo ""
echo "[Step B] Generating input metadata..."
python3 "${ACUITY_BIN}" generate inputmeta \
    --model yolo26n.json \
    --input-meta-output yolo26n_inputmeta.yml

echo "[Step B] Done: yolo26n_inputmeta.yml"

# ── Step C: Quantize (INT8) ───────────────────────────────────────────────────
echo ""
echo "[Step C] Quantizing model (INT8 asymmetric_affine)..."

# The ACUITY Toolkit needs a dataset.txt listing calibration images.
# We generate 10 random 320x320 images for calibration (the model input size).
CALIB_DIR="${MODEL_DIR}/calib_images"
mkdir -p "${CALIB_DIR}"

echo "[Step C] Generating 10 random 320x320 calibration images..."
python3 -c "
import numpy as np
from PIL import Image
import os
calib_dir = '${CALIB_DIR}'
for i in range(10):
    img = np.random.randint(0, 256, (320, 320, 3), dtype=np.uint8)
    Image.fromarray(img).save(os.path.join(calib_dir, f'calib_{i}.jpg'))
"

# Generate dataset.txt listing calibration images
ls "${CALIB_DIR}/"*.jpg "${CALIB_DIR}/"*.png 2>/dev/null > dataset.txt
NUM_CALIB=$(wc -l < dataset.txt)
echo "[Step C] Calibration dataset: ${NUM_CALIB} images"

python3 "${ACUITY_BIN}" quantize \
    --model yolo26n.json \
    --model-data yolo26n.data \
    --with-input-meta yolo26n_inputmeta.yml \
    --model-quantize yolo26n.quantize \
    --quantizer asymmetric_affine \
    --qtype int8

echo "[Step C] Done: yolo26n.quantize"

# ── Step D: Export ovxlib case for VIP9000 (Cubie A7s) ───────────────────────
echo ""
echo "[Step D] Exporting ovxlib case (VIP9000 / Cubie A7s)..."
echo "  (Generating C source files — NBG packing requires native ARM64 build)"
python3 "${ACUITY_BIN}" export ovxlib \
    --model yolo26n.json \
    --model-data yolo26n.data \
    --model-quantize yolo26n.quantize \
    --with-input-meta yolo26n_inputmeta.yml \
    --optimize VIP9000 \
    --output-path "${GEN_DIR}"

# The ACUITY export step may place C source files in the model root directory.
# Move them into the generated directory if they ended up there.
echo "[Step D] Moving any C source files from model root to ${GEN_DIR}..."
for f in "${MODEL_DIR}"/vnn_*.c "${MODEL_DIR}"/vnn_*.h \
         "${MODEL_DIR}"/main.c "${MODEL_DIR}"/makefile.linux \
         "${MODEL_DIR}"/vnn_global.h; do
    if [ -f "$f" ]; then
        mv "$f" "${GEN_DIR}/" 2>/dev/null && echo "  moved $(basename $f)"
    fi
done

echo "[Step D] Done"

# ── Step E: Pack NBG (requires native ARM64 — skip on x86_64 under Rosetta) ──
echo ""
echo "[Step E] Attempting NBG packing..."
echo "  (This step runs gen_nbg which links against VSI emulator libs)"
echo "  (On Apple Silicon (Rosetta), this may segfault — that's expected)"
echo "  (For native ARM64, run on the Cubie A7s board instead)"

# Try pack-nbg-unify, but don't fail if it segfaults under Rosetta
python3 "${ACUITY_BIN}" export ovxlib \
    --model yolo26n.json \
    --model-data yolo26n.data \
    --model-quantize yolo26n.quantize \
    --with-input-meta yolo26n_inputmeta.yml \
    --pack-nbg-unify \
    --optimize VIP9000 \
    --output-path "${GEN_DIR}" 2>&1 || {
    echo ""
    echo "[INFO] NBG packing failed (expected under Rosetta emulation)"
    echo "[INFO] To complete NBG packing, run on the Cubie A7s board:"
    echo ""
    echo "  # Copy the generated files to the board:"
    echo "  scp -r ${GEN_DIR}/* cubie-a7s.local:/tmp/nbg/"
    echo ""
    echo "  # On the board, compile and run gen_nbg:"
    echo "  cd /tmp/nbg && make -f makefile.linux && ./gen_nbg nbg_output.export.data images_392_0.tensor"
    echo ""
    echo "  # Copy the resulting NBG back:"
    echo "  scp cubie-a7s.local:/tmp/nbg/network_binary.nb ${OUTPUT_DIR}/"
}

# Move any NBG binary that was generated into the output directory
if [ -f "${GEN_DIR}/network_binary.nb" ]; then
    mv "${GEN_DIR}/network_binary.nb" "${OUTPUT_DIR}/network_binary.nb"
    echo "[INFO] Moved NBG binary to ${OUTPUT_DIR}/network_binary.nb"
fi
if [ -f "${GEN_DIR}/network_binary.nbg" ]; then
    mv "${GEN_DIR}/network_binary.nbg" "${OUTPUT_DIR}/network_binary.nbg"
    echo "[INFO] Moved NBG binary to ${OUTPUT_DIR}/network_binary.nbg"
fi

# ── Verify ────────────────────────────────────────────────────────────────────
echo ""
echo "=========================================="
echo "Conversion Complete!"
echo "=========================================="

if [ -f "${OUTPUT_DIR}/network_binary.nb" ]; then
    SIZE_KB=$(du -k "${OUTPUT_DIR}/network_binary.nb" | cut -f1)
    echo "NBG model: ${OUTPUT_DIR}/network_binary.nb (${SIZE_KB} KB)"
elif [ -f "${OUTPUT_DIR}/network_binary.nbg" ]; then
    SIZE_KB=$(du -k "${OUTPUT_DIR}/network_binary.nbg" | cut -f1)
    echo "NBG model: ${OUTPUT_DIR}/network_binary.nbg (${SIZE_KB} KB)"
else
    echo "[INFO] NBG binary not generated in this run."
    echo "  All intermediate files are in: ${GEN_DIR}/"
    echo ""
    echo "  To complete NBG packing, run on the Cubie A7s board (see above)."
fi

echo ""
echo "Generated files in: ${GEN_DIR}/"
ls -la "${GEN_DIR}/"
echo ""
echo "Output directory: ${OUTPUT_DIR}/"
ls -la "${OUTPUT_DIR}/"
