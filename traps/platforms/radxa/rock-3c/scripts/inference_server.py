#!/usr/bin/env python3
"""
Inference server for AI Camera Trap — subprocess mode (Plan B).

Reads NV12 frames from stdin, runs RKNN inference, writes JSON detections
to stdout. Intended to be spawned as a child process by InferenceHalSubprocess.

Protocol:
  C++ → Python (stdin):  <4-byte width><4-byte height><NV12 frame bytes>
  Python → C++ (stdout): <JSON line with detections>\n

Usage (standalone test):
  python3 inference_server.py /path/to/model.rknn [conf_thresh]
"""

import sys
import json
import struct
import numpy as np
from rknn.api import RKNN

CONF_THRESH = 0.5
MODEL_W = 320
MODEL_H = 320


def nv12_to_rgb(nv12: np.ndarray, src_w: int, src_h: int) -> np.ndarray:
    """Convert NV12 frame to RGB 320x320 uint8 NHWC."""
    y = nv12[:src_w * src_h].reshape(src_h, src_w).astype(np.float32)
    uv = nv12[src_w * src_h:].reshape(src_h // 2, src_w, 2).astype(np.float32)
    u = uv[:, :, 0]
    v = uv[:, :, 1]

    row_rat = src_h / MODEL_H
    col_rat = src_w / MODEL_W
    rows = np.floor(np.arange(MODEL_H) * row_rat).astype(int)
    cols = np.floor(np.arange(MODEL_W) * col_rat).astype(int)
    y_resized = y[rows][:, cols]

    u_resized = u[rows // 2][:, cols // 2]
    v_resized = v[rows // 2][:, cols // 2]

    u_up = np.repeat(np.repeat(u_resized, 2, axis=0), 2, axis=1)[:MODEL_H, :MODEL_W]
    v_up = np.repeat(np.repeat(v_resized, 2, axis=0), 2, axis=1)[:MODEL_H, :MODEL_W]

    r = y_resized + 1.402 * (v_up - 128)
    g = y_resized - 0.344 * (u_up - 128) - 0.714 * (v_up - 128)
    b = y_resized + 1.772 * (u_up - 128)

    rgb = np.clip(np.stack([r, g, b], axis=-1), 0, 255).astype(np.uint8)
    return rgb


def decode_output(out: np.ndarray) -> list:
    """Decode model output to list of [x,y,w,h,conf,class_id]."""
    if out.ndim != 3:
        return []
    data = out[0]
    c0, c1 = data.shape[0], data.shape[1]
    inv_w = 1.0 / MODEL_W
    inv_h = 1.0 / MODEL_H

    dets = []
    if c0 == 5 and c1 == 2100:
        # YOLOv8n pre-decoded [x1,y1,x2,y2,conf]
        for i in range(c1):
            conf = float(data[4, i])
            if conf < CONF_THRESH:
                continue
            x1, y1, x2, y2 = float(data[0, i]), float(data[1, i]), float(data[2, i]), float(data[3, i])
            dets.append([x1 * inv_w, y1 * inv_h, abs(x2 - x1) * inv_w, abs(y2 - y1) * inv_h, conf, 0])
    elif c1 == 6:
        # YOLO26n post-processed [x1,y1,x2,y2,conf,class]
        for i in range(c0):
            conf = float(data[i, 4])
            if conf < CONF_THRESH:
                continue
            x1, y1, x2, y2 = float(data[i, 0]), float(data[i, 1]), float(data[i, 2]), float(data[i, 3])
            cls = int(data[i, 5])
            dets.append([x1 * inv_w, y1 * inv_h, abs(x2 - x1) * inv_w, abs(y2 - y1) * inv_h, conf, cls])
    return dets


def main():
    if len(sys.argv) < 2:
        print(json.dumps({"error": "Usage: inference_server.py <model.rknn> [conf_thresh]"}))
        sys.exit(1)

    model_path = sys.argv[1]
    global CONF_THRESH
    if len(sys.argv) >= 3:
        CONF_THRESH = float(sys.argv[2])

    print(f"[RKNN-Server] Loading model: {model_path}", file=sys.stderr)
    sys.stderr.flush()

    rknn = RKNN(verbose=False)
    ret = rknn.load_rknn(path=model_path)
    if ret != 0:
        print(json.dumps({"error": f"load_rknn failed: {ret}"}))
        sys.exit(1)

    print("[RKNN-Server] Initializing runtime...", file=sys.stderr)
    sys.stderr.flush()
    ret = rknn.init_runtime(target='rk3566')
    if ret != 0:
        print(json.dumps({"error": f"init_runtime failed: {ret}"}))
        sys.exit(1)

    print("[RKNN-Server] Ready", file=sys.stderr)
    sys.stderr.flush()

    stdin = sys.stdin.buffer
    stdout = sys.stdout.buffer

    while True:
        # Read 8-byte header: width (u32) + height (u32)
        header = stdin.read(8)
        if not header or len(header) < 8:
            break

        w, h = struct.unpack('<II', header)
        frame_size = w * h * 3 // 2  # NV12

        frame_data = stdin.read(frame_size)
        if not frame_data or len(frame_data) < frame_size:
            break

        # Convert NV12 → RGB
        nv12 = np.frombuffer(frame_data, dtype=np.uint8)
        rgb = nv12_to_rgb(nv12, w, h)

        # Infer
        input_data = rgb[np.newaxis, :, :, :].astype(np.uint8)
        outputs = rknn.inference(inputs=[input_data])

        # Decode
        dets = decode_output(outputs[0]) if outputs else []

        # Write JSON response
        response = json.dumps(dets) + "\n"
        stdout.write(response.encode('utf-8'))
        stdout.flush()


if __name__ == "__main__":
    main()