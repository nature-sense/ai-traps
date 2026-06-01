#!/usr/bin/env python3

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


"""
Convert a trained ONNX model to RKNN format for Rockchip NPU (RK3566 / ROCK 3C).

This script can run in three modes:
  1. Local (x86_64 Linux with rknn-toolkit2 installed)
  2. Docker (x86_64 Linux container)
  3. Remote via SSH (on the ROCK 3C board itself)

Usage (local):
    python convert_rknn.py --input best.onnx --output yolo26n.rknn

Usage (remote via SSH on ROCK 3C):
    python convert_rknn.py --input best.onnx --output yolo26n.rknn \
        --remote radxa@rock-3c.local

Usage (Docker):
    docker run --rm -v $(pwd):/models rknn-converter \
        --input /models/best.onnx --output /models/yolo26n.rknn
"""

import argparse
import os
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(
        description="Convert ONNX model to RKNN for Rockchip NPU"
    )
    parser.add_argument(
        "--input", "-i",
        type=str,
        required=True,
        help="Path to input ONNX model",
    )
    parser.add_argument(
        "--output", "-o",
        type=str,
        default="yolo26n.rknn",
        help="Output RKNN model path (default: yolo26n.rknn)",
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
        "--remote",
        type=str,
        default=None,
        help="SSH host for remote conversion (e.g., radxa@rock-3c.local)",
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

    # ── Remote mode: scp + ssh + scp back ─────────────────────────────────────
    if args.remote:
        remote_host = args.remote
        remote_dir = "/tmp/rknn_convert"
        remote_input = f"{remote_dir}/{os.path.basename(args.input)}"
        remote_output = f"{remote_dir}/{os.path.basename(args.output)}"

        print(f"[INFO] Remote mode: converting on {remote_host}")

        # Create remote temp dir
        subprocess.run(
            ["ssh", remote_host, f"mkdir -p {remote_dir}"],
            check=True, capture_output=True
        )

        # Copy input ONNX to board
        print(f"[INFO] Copying {args.input} -> {remote_host}:{remote_input}")
        subprocess.run(
            ["scp", args.input, f"{remote_host}:{remote_input}"],
            check=True, capture_output=True
        )

        # Copy this script to board
        script_remote = f"{remote_dir}/convert_rknn.py"
        subprocess.run(
            ["scp", __file__, f"{remote_host}:{script_remote}"],
            check=True, capture_output=True
        )

        # Build remote command
        remote_cmd = (
            f"python3 {script_remote}"
            f" --input {remote_input}"
            f" --output {remote_output}"
            f" --target {args.target}"
            f" --imgsz {args.imgsz}"
        )
        if args.quantize:
            remote_cmd += " --quantize"
        else:
            remote_cmd += " --no-quantize"

        print(f"[INFO] Running conversion on {remote_host}...")
        result = subprocess.run(
            ["ssh", remote_host, remote_cmd],
            capture_output=True, text=True, timeout=600
        )
        print(result.stdout)
        if result.stderr:
            print(result.stderr, file=sys.stderr)

        if result.returncode != 0:
            print(f"[ERROR] Remote conversion failed with exit code {result.returncode}")
            sys.exit(result.returncode)

        # Copy output back
        print(f"[INFO] Copying {remote_host}:{remote_output} -> {args.output}")
        subprocess.run(
            ["scp", f"{remote_host}:{remote_output}", args.output],
            check=True, capture_output=True
        )

        # Cleanup remote temp dir
        subprocess.run(
            ["ssh", remote_host, f"rm -rf {remote_dir}"],
            capture_output=True
        )

        # Verify
        if os.path.isfile(args.output):
            size_mb = os.path.getsize(args.output) / (1024 * 1024)
            print(f"[INFO] RKNN model saved: {args.output} ({size_mb:.2f} MB)")
        else:
            print(f"[ERROR] Output file not found after remote copy")
            sys.exit(1)

        print(f"\n{'='*60}")
        print(f"Remote conversion complete!")
        print(f"  Board:   {remote_host}")
        print(f"  Input:   {args.input}")
        print(f"  Output:  {args.output}")
        print(f"  Target:  {args.target}")
        print(f"  Quant:   {'INT8' if args.quantize else 'FP16'}")
        print(f"{'='*60}")
        return

    # ── Local mode ────────────────────────────────────────────────────────────
    try:
        from rknn.api import RKNN
    except ImportError:
        print("[ERROR] rknn-toolkit2 is not installed.")
        print()
        print("  To run locally, install rknn-toolkit2 on an x86_64 Linux machine.")
        print("  Or use --remote to convert on the ROCK 3C board via SSH.")
        print()
        print("  Examples:")
        print("    python convert_rknn.py --input best.onnx --remote radxa@rock-3c.local")
        sys.exit(1)

    # ── Create RKNN object ────────────────────────────────────────────────────
    print(f"[INFO] Creating RKNN object for target: {args.target}")
    rknn = RKNN(verbose=True)

    # ── Configure model ───────────────────────────────────────────────────────
    print(f"[INFO] Configuring model...")
    ret = rknn.config(
        target_platform=args.target,
        quantized_method="channel",
        quantized_dtype="asymmetric_quantized-8" if args.quantize else "float16",
        model_pruning=False,
        remove_weight=True,
    )
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
