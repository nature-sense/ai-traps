#!/usr/bin/env python3
"""
Convert a trained ONNX model to NBG format for Vivante VIPLite NPU (Allwinner A527).

This script can run in three modes:
  1. Local (x86_64 Linux with ACUITY Toolkit installed)
  2. Docker (x86_64 Linux container)
  3. Remote via SSH (on the Cubie A7s board itself)

Usage (local):
    python convert_nbg.py --input best.onnx --output yolo26n.nbg

Usage (remote via SSH on Cubie A7s):
    python convert_nbg.py --input best.onnx --output yolo26n.nbg \
        --remote radxa@cubie-a7s.local

Usage (Docker):
    docker run --rm -v $(pwd):/models nbg-converter \
        --input /models/best.onnx --output /models/yolo26n.nbg
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(
        description="Convert ONNX model to NBG for Vivante VIPLite NPU"
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
        default="yolo26n.nbg",
        help="Output NBG model path (default: yolo26n.nbg)",
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
        help="Disable INT8 quantization (FP32 inference)",
    )
    parser.add_argument(
        "--toolkit-dir",
        type=str,
        default="/opt/acuity-toolkit",
        help="ACUITY Toolkit installation directory (default: /opt/acuity-toolkit)",
    )
    parser.add_argument(
        "--remote",
        type=str,
        default=None,
        help="SSH host for remote conversion (e.g., radxa@cubie-a7s.local)",
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
        remote_dir = "/tmp/nbg_convert"
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
        script_remote = f"{remote_dir}/convert_nbg.py"
        subprocess.run(
            ["scp", __file__, f"{remote_host}:{script_remote}"],
            check=True, capture_output=True
        )

        # Build remote command
        remote_cmd = (
            f"python3 {script_remote}"
            f" --input {remote_input}"
            f" --output {remote_output}"
            f" --imgsz {args.imgsz}"
            f" --toolkit-dir {args.toolkit_dir}"
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
            print(f"[INFO] NBG model saved: {args.output} ({size_mb:.2f} MB)")

            # Verify NBG magic header
            with open(args.output, "rb") as f:
                header = f.read(4)
            if header[:3] == b"NBG":
                print(f"[INFO] NBG magic header verified: {header}")
            else:
                print(f"[WARN] Unexpected NBG header: {header.hex()}")
        else:
            print(f"[ERROR] Output file not found after remote copy")
            sys.exit(1)

        print(f"\n{'='*60}")
        print(f"Remote conversion complete!")
        print(f"  Board:   {remote_host}")
        print(f"  Input:   {args.input}")
        print(f"  Output:  {args.output}")
        print(f"  Quant:   {'INT8' if args.quantize else 'FP32'}")
        print(f"{'='*60}")
        return

    # ── Local mode ────────────────────────────────────────────────────────────
    acuity_cli = os.path.join(args.toolkit_dir, "bin", "acuity-cli")
    if not os.path.isfile(acuity_cli):
        print(f"[ERROR] ACUITY Toolkit not found at: {args.toolkit_dir}")
        print()
        print("  To run locally, install the ACUITY Toolkit on an x86_64 Linux machine.")
        print("  Or use --remote to convert on the Cubie A7s board via SSH.")
        print()
        print("  Examples:")
        print("    python convert_nbg.py --input best.onnx --remote radxa@cubie-a7s.local")
        sys.exit(1)

    # ── Create working directory ──────────────────────────────────────────────
    work_dir = tempfile.mkdtemp(prefix="nbg_convert_")
    print(f"[INFO] Working directory: {work_dir}")

    # ── Prepare ACUITY configuration ──────────────────────────────────────────
    config = {
        "model": {
            "format": "onnx",
            "file": os.path.abspath(args.input),
            "input_name": "images",
            "output_name": "output0",
            "input_size": [1, 3, args.imgsz, args.imgsz],
            "mean": [0, 0, 0],
            "std": [1.0, 1.0, 1.0],
            "input_type": "uint8",
            "output_type": "float32",
        },
        "target": {
            "platform": "VIPLite",
            "chip": "A527",
            "core": "NN",
            "quantize": {
                "enable": args.quantize,
                "type": "int8" if args.quantize else "float32",
                "calibration_dataset": "",
                "calibration_size": 100,
            },
            "optimization": {
                "fuse_bn": True,
                "fuse_relu": True,
                "remove_identity": True,
                "constant_folding": True,
            },
        },
        "output": {
            "file": os.path.abspath(args.output),
            "format": "nbg",
        },
    }

    config_path = os.path.join(work_dir, "config.json")
    with open(config_path, "w") as f:
        json.dump(config, f, indent=2)
    print(f"[INFO] Configuration written to: {config_path}")

    # ── Run ACUITY conversion ─────────────────────────────────────────────────
    print(f"[INFO] Running ACUITY conversion...")
    print(f"       Input:  {args.input}")
    print(f"       Output: {args.output}")
    print(f"       Quant:  {'INT8' if args.quantize else 'FP32'}")

    cmd = [
        acuity_cli,
        "--config", config_path,
        "--output", os.path.abspath(args.output),
    ]

    try:
        result = subprocess.run(
            cmd,
            cwd=work_dir,
            capture_output=True,
            text=True,
            timeout=600,
        )
    except subprocess.TimeoutExpired:
        print("[ERROR] ACUITY conversion timed out after 10 minutes")
        sys.exit(1)

    if result.stdout:
        print(result.stdout)
    if result.stderr:
        print(result.stderr, file=sys.stderr)

    if result.returncode != 0:
        print(f"[ERROR] ACUITY conversion failed with exit code: {result.returncode}")
        sys.exit(1)

    # ── Verify output ─────────────────────────────────────────────────────────
    output_path = os.path.abspath(args.output)
    if os.path.isfile(output_path):
        size_mb = os.path.getsize(output_path) / (1024 * 1024)
        print(f"[INFO] NBG model exported successfully: {output_path}")
        print(f"[INFO] Model size: {size_mb:.2f} MB")

        with open(output_path, "rb") as f:
            header = f.read(4)
        if header[:3] == b"NBG":
            print(f"[INFO] NBG magic header verified: {header}")
        else:
            print(f"[WARN] Unexpected NBG header: {header.hex()}")
    else:
        print(f"[ERROR] Output file not found after conversion: {output_path}")
        sys.exit(1)

    # ── Cleanup ───────────────────────────────────────────────────────────────
    import shutil
    shutil.rmtree(work_dir, ignore_errors=True)

    print(f"\n{'='*60}")
    print(f"Conversion complete!")
    print(f"  Input:  {args.input}")
    print(f"  Output: {output_path}")
    print(f"  Quant:  {'INT8' if args.quantize else 'FP32'}")
    print(f"{'='*60}")


if __name__ == "__main__":
    main()
