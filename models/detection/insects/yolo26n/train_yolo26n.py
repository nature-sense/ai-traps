#!/usr/bin/env python3
"""
Train a YOLO26n insect detection model.

Uses yolo26n.pt from Ultralytics as the pretrained checkpoint and trains on
the Insect_Detect dataset (320×320, 1 class: "insect").

Usage:
    python train_yolo26n.py                          # default training
    python train_yolo26n.py --epochs 150 --batch 16  # custom params
    python train_yolo26n.py --device cpu             # CPU training
"""

import argparse
import os
import sys
from pathlib import Path

import torch
from ultralytics import YOLO


# ── Default dataset path ──────────────────────────────────────────────────────
DEFAULT_DATASET = (
    "/Users/steve/datasets/"
    "Insect_Detect_detection.v7-insect_detect_320_1class.yolov11"
)


def resolve_data_yaml(dataset_dir: str) -> str:
    """
    Resolve the data.yaml path and rewrite its train/val/test paths to absolute.

    The dataset's data.yaml uses relative paths (e.g. ``../train/images``) which
    only work when the working directory is the dataset folder.  This function
    rewrites them to absolute paths so the script can be run from anywhere.
    """
    dataset_dir = os.path.abspath(dataset_dir)
    yaml_path = os.path.join(dataset_dir, "data.yaml")

    if not os.path.isfile(yaml_path):
        print(f"[ERROR] data.yaml not found at: {yaml_path}")
        sys.exit(1)

    # Read original content
    with open(yaml_path, "r") as f:
        lines = f.readlines()

    # Rewrite relative paths to absolute.
    # The original data.yaml uses paths like "../train/images" which assume
    # the working directory is one level above the dataset folder.  Since we
    # resolve relative to the dataset directory itself, we strip any leading
    # "../" segments.
    out_lines = []
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("train:") or stripped.startswith("val:") or stripped.startswith("test:"):
            key, _, value = stripped.partition(":")
            value = value.strip()
            # Only rewrite relative paths
            if not value.startswith("/"):
                # Strip leading "../" segments — they assume a different CWD
                while value.startswith("../"):
                    value = value[3:]
                abs_path = os.path.normpath(os.path.join(dataset_dir, value))
                out_lines.append(f"{key}: {abs_path}\n")
            else:
                out_lines.append(line)
        else:
            out_lines.append(line)

    # Write a temporary resolved copy next to the original
    resolved_path = os.path.join(dataset_dir, "data_resolved.yaml")
    with open(resolved_path, "w") as f:
        f.writelines(out_lines)

    print(f"[INFO] Resolved data.yaml -> {resolved_path}")
    return resolved_path


def main():
    parser = argparse.ArgumentParser(
        description="Train YOLO26n insect detection model"
    )
    parser.add_argument(
        "--data",
        type=str,
        default=DEFAULT_DATASET,
        help=f"Path to dataset directory (default: {DEFAULT_DATASET})",
    )
    parser.add_argument(
        "--epochs",
        type=int,
        default=100,
        help="Number of training epochs (default: 100)",
    )
    parser.add_argument(
        "--imgsz",
        type=int,
        default=320,
        help="Input image size (default: 320)",
    )
    parser.add_argument(
        "--batch",
        type=int,
        default=-1,
        help="Batch size (-1 for auto, default: -1)",
    )
    parser.add_argument(
        "--device",
        type=str,
        default="",
        help='Device: "0" for GPU, "cpu" for CPU (default: auto-detect)',
    )
    parser.add_argument(
        "--project",
        type=str,
        default="/Users/steve/naturesense/models/runs/detect",
        help="Output project directory (default: /Users/steve/naturesense/models/runs/detect)",
    )
    parser.add_argument(
        "--name",
        type=str,
        default="yolo26n_insect",
        help="Run name (default: yolo26n_insect)",
    )
    parser.add_argument(
        "--resume",
        type=str,
        default=None,
        help="Path to checkpoint to resume from (default: None)",
    )
    parser.add_argument(
        "--workers",
        type=int,
        default=8,
        help="Number of data loading workers (default: 8)",
    )
    parser.add_argument(
        "--patience",
        type=int,
        default=20,
        help="Early stopping patience (default: 20)",
    )
    args = parser.parse_args()

    # ── Auto-detect device ──────────────────────────────────────────────────
    if not args.device:
        if torch.cuda.is_available():
            args.device = "0"
            print(f"[INFO] CUDA detected — using GPU device=0")
        else:
            args.device = "cpu"
            print(f"[INFO] No CUDA detected — falling back to CPU")

    # ── Resolve dataset paths ────────────────────────────────────────────────
    data_yaml = resolve_data_yaml(args.data)

    # ── Load model ───────────────────────────────────────────────────────────
    print(f"[INFO] Loading yolo26n.pt pretrained model")
    model = YOLO("yolo26n.pt")

    # ── Training arguments ───────────────────────────────────────────────────
    train_kwargs = {
        "data": data_yaml,
        "epochs": args.epochs,
        "imgsz": args.imgsz,
        "batch": args.batch,
        "device": args.device,
        "project": args.project,
        "name": args.name,
        "workers": args.workers,
        "patience": args.patience,
        "optimizer": "auto",
        "cos_lr": True,
        "close_mosaic": 10,
        "augment": True,
        "val": True,
        "plots": True,
        "save": True,
        "exist_ok": True,
        "verbose": True,
    }

    if args.resume:
        train_kwargs["resume"] = args.resume

    # ── Train ────────────────────────────────────────────────────────────────
    print(f"[INFO] Starting training")
    print(f"       Model:     yolo26n.pt")
    print(f"       Dataset:   {data_yaml}")
    print(f"       Epochs:    {args.epochs}")
    print(f"       Img size:  {args.imgsz}")
    print(f"       Batch:     {args.batch}")
    print(f"       Device:    {args.device}")
    print(f"       Project:   {args.project}/{args.name}")
    print()

    results = model.train(**train_kwargs)

    # ── Export to ONNX ───────────────────────────────────────────────────────
    print(f"\n[INFO] Training complete. Exporting best model to ONNX...")

    best_pt = Path(args.project) / args.name / "weights" / "best.pt"
    if best_pt.exists():
        best_model = YOLO(str(best_pt))
        onnx_path = best_model.export(format="onnx", imgsz=args.imgsz)
        print(f"[INFO] ONNX model exported to: {onnx_path}")
    else:
        print(f"[WARN] best.pt not found at {best_pt}, skipping ONNX export")

    # ── Summary ──────────────────────────────────────────────────────────────
    print(f"\n{'='*60}")
    print(f"Training complete!")
    print(f"  Results:  {args.project}/{args.name}/")
    print(f"  Best PT:  {best_pt}")
    if best_pt.exists():
        print(f"  Best ONNX: {onnx_path}")
    print(f"{'='*60}")


if __name__ == "__main__":
    main()
