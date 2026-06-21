#!/usr/bin/env python3
#
# convert_rknn_onboard.py — Convert YOLOv8n ONNX model to RKNN on the ROCK 3C board
#
# This script runs ON THE BOARD ITSELF. It uses the rknn-toolkit2 Python
# package (installed alongside the rknpu2 runtime) to convert an ONNX model
# to RKNN format. This ensures the model format matches the librknnrt.so
# runtime version on the board.
#
# Usage:
#   python3 convert_rknn_onboard.py --input best.onnx --output yolov8n.rknn
#
# Options:
#   --input   Path to input ONNX model (default: best.onnx)
#   --output  Path to output RKNN model (default: yolov8n.rknn)
#   --target  Rockchip target platform (default: rk3566)
#   --imgsz   Model input size (default: 320)
#   --no-quantize  Disable INT8 quantization (FP16 inference)
#

import argparse
import os
import sys


def main():
    parser = argparse.ArgumentParser(
        description="Convert YOLOv8n ONNX model to RKNN on ROCK 3C board"
    )
    parser.add_argument(
        "--input", "-i",
        type=str,
        default="best.onnx",
        help="Path to input ONNX model (default: best.onnx)",
    )
    parser.add_argument(
        "--output", "-o",
        type=str,
        default="yolov8n.rknn",
        help="Output RKNN model path (default: yolov8n.rknn)",
    )
    parser.add_argument(
        "--target",
        type=str,
        default="rk3566",
        choices=["rk3566", "rk3568", "rk3588", "rv1106", "rv1103"],
        help="Rockchip target platform (default: rk3566)",
    )
    parser.add_argument(
        "--imgsz",
        type=int,
        default=320,
        help="Model input size (default: 320)",
    )
    parser.add_argument(
        "--quantize",
        action="store_true",
        default=True,
        help="Enable INT8 quantization (default: True)",
    )
    parser.add_argument(
        "--no-quantize",
        action="store_false",
        dest="quantize",
        help="Disable INT8 quantization (FP16 inference)",
    )
    parser.add_argument(
        "--dataset",
        type=str,
        default=None,
        help="Path to dataset.txt file for quantization calibration images",
    )
    args = parser.parse_args()

    # ── Validate input ─────────────────────────────────────────────────────────
    if not os.path.isfile(args.input):
        print(f"[ERROR] Input file not found: {args.input}")
        sys.exit(1)

    if not args.input.endswith(".onnx"):
        print(f"[WARN] Expected .onnx file, got: {args.input}")

    # ── Check rknn-toolkit2 availability ───────────────────────────────────────
    try:
        from rknn.api import RKNN
    except ImportError:
        print("[ERROR] rknn-toolkit2 is not installed.")
        print()
        print("  Install it with:")
        print("    pip3 install rknn-toolkit2")
        print()
        print("  On ROCK 3C with Radxa OS, it should be available after:")
        print("    sudo apt-get install rknpu2-rk356x python3-pip")
        print("    pip3 install rknn-toolkit2")
        sys.exit(1)

    # ── Create RKNN object ────────────────────────────────────────────────────
    print(f"[INFO] Creating RKNN object for target: {args.target}")
    rknn = RKNN(verbose=True)

    # ── Configure model ───────────────────────────────────────────────────────
    print(f"[INFO] Configuring model...")
    config_kwargs = dict(
        target_platform=args.target,
        quantized_method="channel",
        model_pruning=False,
        remove_weight=True,
    )
    if args.quantize:
        config_kwargs["quantized_dtype"] = "w8a8"
    else:
        config_kwargs["quantized_dtype"] = "w16a16i"
    ret = rknn.config(**config_kwargs)
    if ret != 0:
        print(f"[ERROR] rknn.config() failed with error code: {ret}")
        sys.exit(1)

    # ── Load ONNX model ───────────────────────────────────────────────────────
    print(f"[INFO] Loading ONNX model: {args.input}")
    ret = rknn.load_onnx(model=args.input)
    if ret != 0:
        print(f"[ERROR] rknn.load_onnx() failed with error code: {ret}")
        sys.exit(1)

    # ── Build RKNN model ──────────────────────────────────────────────────────
    # If quantization is requested but no dataset is provided, fall back to FP16
    if args.quantize and not args.dataset:
        print("[WARN] No dataset provided for INT8 quantization calibration.")
        print("[WARN] Falling back to FP16 inference (no quantization).")
        print("[WARN] To enable INT8 quantization, provide a dataset.txt with:")
        print("[WARN]   python3 convert_rknn_onboard.py --dataset dataset.txt")
        print("[WARN]   where dataset.txt lists paths to representative images.")
        args.quantize = False
        # Re-configure with FP16 dtype since config was already called with w8a8
        config_kwargs["quantized_dtype"] = "w16a16i"
        ret = rknn.config(**config_kwargs)
        if ret != 0:
            print(f"[ERROR] rknn.config() failed with error code: {ret}")
            sys.exit(1)

    dataset_path = args.dataset if args.dataset else ""
    print(f"[INFO] Building RKNN model (quantize={args.quantize}, dataset={dataset_path})...")
    ret = rknn.build(do_quantization=args.quantize, dataset=dataset_path)
    if ret != 0:
        print(f"[ERROR] rknn.build() failed with error code: {ret}")
        sys.exit(1)

    # ── Export RKNN model ─────────────────────────────────────────────────────
    output_path = os.path.abspath(args.output)
    print(f"[INFO] Exporting RKNN model to: {output_path}")
    ret = rknn.export_rknn(export_path=output_path)
    if ret != 0:
        print(f"[ERROR] rknn.export_rknn() failed with error code: {ret}")
        sys.exit(1)

    # ── Verify output ─────────────────────────────────────────────────────────
    if os.path.isfile(output_path):
        size_mb = os.path.getsize(output_path) / (1024 * 1024)
        print(f"[INFO] RKNN model exported successfully: {output_path}")
        print(f"[INFO] Model size: {size_mb:.2f} MB")
    else:
        print(f"[ERROR] Output file not found after export: {output_path}")
        sys.exit(1)

    # ── Cleanup ───────────────────────────────────────────────────────────────
    rknn.release()

    print(f"\n{'='*60}")
    print(f"Conversion complete!")
    print(f"  Input:  {args.input}")
    print(f"  Output: {output_path}")
    print(f"  Target: {args.target}")
    print(f"  Quant:  {'INT8' if args.quantize else 'FP16'}")
    print(f"{'='*60}")


if __name__ == "__main__":
    main()
