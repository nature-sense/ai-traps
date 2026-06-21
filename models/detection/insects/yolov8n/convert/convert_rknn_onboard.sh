#!/usr/bin/env bash
#
# convert_rknn_onboard.sh — Convert YOLOv8n ONNX model to RKNN on ROCK 3C board
#
# This script copies the ONNX model and conversion script to the ROCK 3C board
# via SSH, runs the conversion there, and copies the resulting RKNN model back.
#
# Prerequisites:
#   1. ROCK 3C board with rknn-toolkit2 installed
#   2. SSH access to the board (passwordless key recommended)
#   3. ONNX model exported from best.pt (run train_yolov8n.py first)
#
# Usage:
#   ./convert_rknn_onboard.sh [options]
#
# Options:
#   --host       SSH host (default: radxa@rock-3c.local)
#   --input      ONNX model path (default: ../best.onnx)
#   --output     Output RKNN path (default: ../yolov8n.rknn)
#   --target     Rockchip target (default: rk3566)
#   --imgsz      Model input size (default: 320)
#   --no-quantize  Disable INT8 quantization
#   --dataset    Dataset.txt for quantization calibration
#   --help       Show this help
#

set -euo pipefail

# ── Defaults ──────────────────────────────────────────────────────────────────
HOST="radxa@rock-3c.local"
INPUT="../best.onnx"
OUTPUT="../yolov8n.rknn"
TARGET="rk3566"
IMGSZ=320
QUANTIZE="--quantize"
DATASET=""

# ── Parse arguments ───────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --host)       HOST="$2";       shift 2 ;;
        --input)      INPUT="$2";      shift 2 ;;
        --output)     OUTPUT="$2";     shift 2 ;;
        --target)     TARGET="$2";     shift 2 ;;
        --imgsz)      IMGSZ="$2";      shift 2 ;;
        --no-quantize) QUANTIZE="--no-quantize"; shift ;;
        --dataset)    DATASET="$2";    shift 2 ;;
        --help)
            sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# //'
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--host HOST] [--input INPUT] [--output OUTPUT] [options]"
            exit 1
            ;;
    esac
done

# ── Validate input ────────────────────────────────────────────────────────────
if [ ! -f "$INPUT" ]; then
    echo "[ERROR] Input file not found: $INPUT"
    echo "  Export ONNX first with: python3 ../train_yolov8n.py --export-onnx"
    exit 1
fi

if [[ "$INPUT" != *.onnx ]]; then
    echo "[WARN] Expected .onnx file, got: $INPUT"
fi

# ── Resolve absolute paths ────────────────────────────────────────────────────
INPUT_ABS="$(cd "$(dirname "$INPUT")" && pwd)/$(basename "$INPUT")"
OUTPUT_ABS="$(cd "$(dirname "$OUTPUT")" && pwd)/$(basename "$OUTPUT")"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "═══════════════════════════════════════════════════════════════"
echo "  YOLOv8n → RKNN Converter (on-board)"
echo "═══════════════════════════════════════════════════════════════"
echo "  Host:      $HOST"
echo "  Input:     $INPUT_ABS"
echo "  Output:    $OUTPUT_ABS"
echo "  Target:    $TARGET"
echo "  Img size:  $IMGSZ"
echo "  Quantize:  $([ "$QUANTIZE" = "--quantize" ] && echo "INT8" || echo "FP16")"
echo "───────────────────────────────────────────────────────────────"

# ── Create remote temp directory ──────────────────────────────────────────────
REMOTE_DIR="/tmp/yolov8n_convert_$$"
echo "[1/5] Creating remote temp directory..."
ssh "$HOST" "mkdir -p $REMOTE_DIR"

# ── Copy ONNX model ───────────────────────────────────────────────────────────
echo "[2/5] Copying ONNX model to board..."
scp "$INPUT_ABS" "$HOST:$REMOTE_DIR/best.onnx"

# ── Copy conversion script ────────────────────────────────────────────────────
echo "[3/5] Copying conversion script..."
scp "$SCRIPT_DIR/convert_rknn_onboard.py" "$HOST:$REMOTE_DIR/"

# ── Run conversion on board ───────────────────────────────────────────────────
echo "[4/5] Running RKNN conversion on board (this may take a while)..."
REMOTE_CMD="cd $REMOTE_DIR && python3 convert_rknn_onboard.py \
    --input best.onnx \
    --output yolov8n.rknn \
    --target $TARGET \
    --imgsz $IMGSZ \
    $QUANTIZE"

if [ -n "$DATASET" ]; then
    scp "$DATASET" "$HOST:$REMOTE_DIR/dataset.txt"
    REMOTE_CMD="$REMOTE_CMD --dataset dataset.txt"
fi

ssh "$HOST" "$REMOTE_CMD"

# ── Copy RKNN model back ──────────────────────────────────────────────────────
echo "[5/5] Copying RKNN model back..."
scp "$HOST:$REMOTE_DIR/yolov8n.rknn" "$OUTPUT_ABS"

# ── Cleanup remote temp directory ─────────────────────────────────────────────
echo "[INFO] Cleaning up remote temp directory..."
ssh "$HOST" "rm -rf $REMOTE_DIR"

# ── Verify ────────────────────────────────────────────────────────────────────
if [ -f "$OUTPUT_ABS" ]; then
    SIZE_MB=$(du -h "$OUTPUT_ABS" | cut -f1)
    echo "───────────────────────────────────────────────────────────────"
    echo "[SUCCESS] RKNN model saved: $OUTPUT_ABS ($SIZE_MB)"
    echo "───────────────────────────────────────────────────────────────"
else
    echo "[ERROR] Output file not found after remote copy"
    exit 1
fi

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  Conversion complete!"
echo "  Model: $OUTPUT_ABS"
echo "  Deploy with:"
echo "    scp $OUTPUT_ABS radxa@rock-3c.local:/opt/ai-traps/models/"
echo "═══════════════════════════════════════════════════════════════"
