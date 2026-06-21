#!/usr/bin/env python3
#
# convert_rknn_onboard.py — Convert ONNX model to RKNN on the ROCK 3C board
#
# For YOLO26n models (which include NMS post-processing ops the NPU cannot
# execute), this script strips the NMS subgraph and inserts a Reshape to
# produce a 4D output tensor [1, N, 1, 2100], matching the format that the
# C++ inference HAL expects and that the RKNN runtime handles correctly.
#
# Usage:
#   python3 convert_rknn_onboard.py --input yolo26n.onnx --output yolo26n.rknn
#
# Options:
#   --input   Path to input ONNX model (default: yolo26n.onnx)
#   --output  Path to output RKNN model (default: yolo26n.rknn)
#   --target  Rockchip target platform (default: rk3566)
#   --imgsz   Model input size (default: 320)
#   --no-quantize  Disable INT8 quantization (FP16 inference)
#

import argparse
import os
import sys


def strip_nms_and_reshape(onnx_path: str) -> str:
    """
    For YOLO26n ONNX models:
    1. Find the last Concat_3 node (raw feature map before NMS subgraph)
    2. Remove all descendant nodes (NMS post-processing: TopK, Gather, Tile, etc.)
    3. Insert a Reshape node [1,N,2100] -> [1,N,1,2100]
    4. Save as stripped ONNX and return its path.

    Returns the path to the stripped ONNX file.
    Returns the original path if Concat_3 is not found (no stripping needed).
    """
    import onnx
    import numpy as np

    model = onnx.load(onnx_path)
    graph = model.graph

    # ── Print output info ──────────────────────────────────────────────────────
    print('[rknn] ONNX output tensors:')
    for o in graph.output:
        if o.type.HasField('tensor_type') and o.type.tensor_type.HasField('shape'):
            shape = []
            for d in o.type.tensor_type.shape.dim:
                shape.append(d.dim_value if d.HasField('dim_value') else '?')
        else:
            shape = ['unknown']
        print(f'  {o.name}: {shape}')

    # ── Find Concat_3 ──────────────────────────────────────────────────────────
    concat_3_output = None
    for n in graph.node:
        if n.name.endswith('/Concat_3'):
            concat_3_output = n.output[0]
            print(f'[rknn] found Concat_3: {n.name} -> output {concat_3_output}')
            break

    if concat_3_output is None:
        print('[rknn] Concat_3 not found — no NMS subgraph to strip')
        return onnx_path  # vanilla ONNX, no stripping needed

    # ── Build consumer map ─────────────────────────────────────────────────────
    consumers = {}
    for n in graph.node:
        for inp in n.input:
            if inp:
                consumers.setdefault(inp, []).append(n)

    # ── BFS from Concat_3 output to find NMS nodes ─────────────────────────────
    to_remove_ids = set()
    queue = list(consumers.get(concat_3_output, []))
    visited = set()
    while queue:
        n = queue.pop()
        nid = id(n)
        if nid in visited:
            continue
        visited.add(nid)
        to_remove_ids.add(nid)
        for out in n.output:
            if out:
                queue.extend(consumers.get(out, []))

    print(f'[rknn] removing {len(to_remove_ids)} NMS post-processing node(s)')

    # ── Rebuild graph without NMS nodes ────────────────────────────────────────
    new_nodes = [n for n in graph.node if id(n) not in to_remove_ids]
    del graph.node[:]
    graph.node.extend(new_nodes)

    # ── Determine output channels (N) from Concat_3 output shape ───────────────
    n_channels = None
    for v in graph.value_info:
        if v.name == concat_3_output:
            if v.type.HasField('tensor_type') and v.type.tensor_type.HasField('shape'):
                dims = [d.dim_value for d in v.type.tensor_type.shape.dim]
                if len(dims) >= 2:
                    n_channels = dims[1]
                    print(f'[rknn] Concat_3 shape from value_info: {dims} -> {n_channels} ch')
                break

    if n_channels is None:
        # Fallback: infer from the initializers/shapes connected to Concat_3
        for n in new_nodes:
            for o in n.output:
                if o == concat_3_output:
                    for v in graph.value_info:
                        if v.name == o:
                            if v.type.HasField('tensor_type') and v.type.tensor_type.HasField('shape'):
                                dims = [d.dim_value for d in v.type.tensor_type.shape.dim]
                                if len(dims) >= 2:
                                    n_channels = dims[1]
                                    print(f'[rknn] Concat_3 shape (fallback): {dims} -> {n_channels} ch')
                            break
                    break

    if n_channels is None:
        raise RuntimeError('Could not determine Concat_3 output channels')

    # ── Add Reshape to make output 4D ──────────────────────────────────────────
    print(f'[rknn] adding Reshape: [1,{n_channels},2100] -> [1,{n_channels},1,2100]')

    reshape_shape = onnx.helper.make_tensor(
        name='reshape_shape_4d',
        data_type=onnx.TensorProto.INT64,
        dims=[4],
        vals=[1, n_channels, 1, 2100])
    graph.initializer.append(reshape_shape)

    reshape_node = onnx.helper.make_node(
        'Reshape',
        name='reshape_to_4d',
        inputs=[concat_3_output, 'reshape_shape_4d'],
        outputs=['output_4d'])
    graph.node.append(reshape_node)

    # ── Set reshaped output as sole output ─────────────────────────────────────
    del graph.output[:]
    graph.output.append(
        onnx.helper.make_tensor_value_info(
            'output_4d', onnx.TensorProto.FLOAT, [1, n_channels, 1, 2100]))

    # ── Save stripped ONNX ─────────────────────────────────────────────────────
    stripped_path = onnx_path + '.stripped.onnx'
    onnx.save(model, stripped_path)
    print(f'[rknn] stripped ONNX saved: {stripped_path}')
    return stripped_path


def main():
    parser = argparse.ArgumentParser(
        description="Convert ONNX model to RKNN on ROCK 3C board"
    )
    parser.add_argument(
        "--input", "-i",
        type=str,
        default="yolo26n.onnx",
        help="Path to input ONNX model (default: yolo26n.onnx)",
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

    # ── Check onnx / rknn-toolkit2 availability ────────────────────────────────
    try:
        import onnx
    except ImportError:
        print("[ERROR] onnx is not installed. Install with: pip3 install onnx")
        sys.exit(1)

    try:
        from rknn.api import RKNN
    except ImportError:
        print("[ERROR] rknn-toolkit2 is not installed.")
        print("  Install with: pip3 install rknn-toolkit2")
        sys.exit(1)

    # ── Strip NMS subgraph + reshape to 4D (for YOLO26n models) ────────────────
    onnx_input = strip_nms_and_reshape(args.input)

    # ── Create RKNN object ────────────────────────────────────────────────────
    print(f"[INFO] Creating RKNN object for target: {args.target}")
    rknn = RKNN(verbose=True)

    # ── Configure model ───────────────────────────────────────────────────────
    print(f"[INFO] Configuring model...")
    config_kwargs = dict(
        mean_values=[[0, 0, 0]],
        std_values=[[255, 255, 255]],
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
    print(f"[INFO] Loading ONNX model: {onnx_input}")
    ret = rknn.load_onnx(model=onnx_input)
    if ret != 0:
        print(f"[ERROR] rknn.load_onnx() failed with error code: {ret}")
        sys.exit(1)

    # ── Build RKNN model ──────────────────────────────────────────────────────
    if args.quantize and not args.dataset:
        print("[WARN] No dataset provided for INT8 quantization calibration.")
        print("[WARN] Falling back to FP16 inference (no quantization).")
        print("[WARN] To enable INT8 quantization, provide a dataset.txt with:")
        print("[WARN]   python3 convert_rknn_onboard.py --dataset dataset.txt")
        args.quantize = False
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