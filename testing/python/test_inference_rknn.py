#!/usr/bin/env python3
"""
Test inference on a single scene image using YOLOv8n RKNN model on ROCK 3C.

Usage (on ROCK 3C board):
    python3 test_inference_rknn.py <image.png> <model.rknn>

Example:
    python3 test_inference_rknn.py /tmp/frame_0050.png /usr/share/ai-trap/yolov8n.rknn
    
To run on macOS for comparison (uses ONNX instead):
    python3 test_inference_rknn.py <image.png> <model.onnx> --onnx
"""

import sys
import os
import cv2
import numpy as np

# ─── Constants ────────────────────────────────────────────────────────────────
CONF_THRESH = 0.5
NMS_THRESH  = 0.45
MODEL_W     = 320
MODEL_H     = 320
MAX_BOXES   = 100


# ─── Preprocessing ────────────────────────────────────────────────────────────
def preprocess(image_path: str):
    """
    Load a scene PNG (1920x1080) and convert to model input format:
    RGB 320x320 uint8 NHWC — matching inference_hal_rknn.cpp behavior.
    """
    img = cv2.imread(image_path, cv2.IMREAD_COLOR)
    if img is None:
        print(f"[ERROR] Cannot load image: {image_path}")
        sys.exit(1)

    src_h, src_w = img.shape[:2]
    print(f"[INFO] Loaded {image_path}: {src_w}x{src_h}")

    # BGR → RGB (matching nv12_to_rgb_resize output order)
    img_rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)

    # Resize to model input size (bilinear — matching C++ code)
    input_rgb = cv2.resize(img_rgb, (MODEL_W, MODEL_H), interpolation=cv2.INTER_LINEAR)
    print(f"[INFO] Resized to {MODEL_W}x{MODEL_H} RGB")

    # Return as uint8 NHWC (HWC shape)
    return img_rgb, input_rgb, src_w, src_h


# ─── Post-processing ──────────────────────────────────────────────────────────
def postprocess(output_data: np.ndarray, src_w: int, src_h: int):
    """
    Decode model output. Handles multiple formats:
    - [1, 5, N]     — pre-decoded [x1,y1,x2,y2,conf], N=2100 (YOLOv8n)
    - [1, N, 6]     — fully post-processed [x1,y1,x2,y2,conf,class], N=300 (YOLO26n)
    - [1, C, N]     — raw DFL format for C >= 65 (future)
    """
    assert output_data.ndim == 3, f"Expected 3D output, got shape {output_data.shape}"
    data = output_data[0]  # remove batch dim → [C, N] or [N, C]
    c0, c1 = data.shape[0], data.shape[1]

    inv_w = 1.0 / MODEL_W
    inv_h = 1.0 / MODEL_H

    if c0 == 5 and c1 == 2100:
        # YOLOv8n: [C=5, N=2100] pre-decoded [x1,y1,x2,y2,conf]
        print(f"[INFO] YOLOv8n pre-decoded format: 5 channels, 2100 anchors")
        raw = []
        for i in range(c1):
            x1, y1, x2, y2, conf = data[0,i], data[1,i], data[2,i], data[3,i], data[4,i]
            if conf < CONF_THRESH:
                continue
            raw.append({
                'x': min(x1,x2) * inv_w, 'y': min(y1,y2) * inv_h,
                'w': abs(x2-x1) * inv_w, 'h': abs(y2-y1) * inv_h,
                'confidence': float(conf), 'class_id': 0,
            })
        print(f"[INFO] Before NMS: {len(raw)} detections")
        detections = nms(raw, NMS_THRESH)
        print(f"[INFO] After NMS:  {len(detections)} detections")
        return detections

    elif c1 == 6:
        # YOLO26n: [N=300, C=6] fully post-processed [x1,y1,x2,y2,conf,class]
        print(f"[INFO] YOLO26n post-processed format: {c0} boxes, 6 channels")
        detections = []
        for i in range(c0):
            x1, y1, x2, y2, conf, cls_id = data[i,0], data[i,1], data[i,2], data[i,3], data[i,4], data[i,5]
            if conf < CONF_THRESH:
                continue
            detections.append({
                'x': min(x1,x2) * inv_w, 'y': min(y1,y2) * inv_h,
                'w': abs(x2-x1) * inv_w, 'h': abs(y2-y1) * inv_h,
                'confidence': float(conf), 'class_id': int(cls_id),
            })
        # Already NMS'd by model, but apply a second pass for safety
        print(f"[INFO] Model returned {len(detections)} detections (post-NMS)")
        detections = nms(detections, NMS_THRESH)
        print(f"[INFO] After NMS:  {len(detections)} detections")
        return detections

    else:
        print(f"[ERROR] Unknown output shape: {output_data.shape}")
        return []


# ─── NMS ──────────────────────────────────────────────────────────────────────
def nms(dets, iou_thresh):
    if not dets:
        return []

    dets = sorted(dets, key=lambda d: d['confidence'], reverse=True)
    keep = [True] * len(dets)

    for i in range(len(dets)):
        if not keep[i]:
            continue
        for j in range(i + 1, len(dets)):
            if not keep[j]:
                continue

            ix1 = max(dets[i]['x'], dets[j]['x'])
            iy1 = max(dets[i]['y'], dets[j]['y'])
            ix2 = min(dets[i]['x'] + dets[i]['w'], dets[j]['x'] + dets[j]['w'])
            iy2 = min(dets[i]['y'] + dets[i]['h'], dets[j]['y'] + dets[j]['h'])

            inter = max(0, ix2 - ix1) * max(0, iy2 - iy1)
            area_i = dets[i]['w'] * dets[i]['h']
            area_j = dets[j]['w'] * dets[j]['h']
            union = area_i + area_j - inter

            iou = inter / union if union > 0 else 0
            if iou > iou_thresh:
                keep[j] = False

    return [dets[i] for i in range(len(dets)) if keep[i]]


# ─── RKNN Inference (on ROCK 3C) ─────────────────────────────────────────────
def run_rknn(model_path: str, input_rgb: np.ndarray):
    """
    Run inference using rknn-toolkit2 on the ROCK 3C board.
    input_rgb: uint8 HWC RGB image at MODEL_W x MODEL_H
    """
    from rknn.api import RKNN

    print(f"[INFO] Loading RKNN model: {model_path}")
    rknn = RKNN(verbose=False)
    ret = rknn.load_rknn(path=model_path)
    if ret != 0:
        print(f"[ERROR] Failed to load RKNN model: {ret}")
        sys.exit(1)

    print("[INFO] Initializing runtime (NPU)...")
    ret = rknn.init_runtime(target='rk3566')
    if ret != 0:
        print(f"[ERROR] Failed to init runtime: {ret}")
        sys.exit(1)

    # Prepare input: NHWC uint8 [1, H, W, 3]
    input_data = input_rgb[np.newaxis, :, :, :].astype(np.uint8)
    print(f"[INFO] Input shape: {input_data.shape}, dtype={input_data.dtype}")

    print("[INFO] Running inference...")
    outputs = rknn.inference(inputs=[input_data])
    print(f"[INFO] Inference complete. {len(outputs)} output tensor(s)")

    for i, out in enumerate(outputs):
        print(f"[INFO] Output[{i}]: shape={out.shape}, dtype={out.dtype}, "
              f"min={out.min():.4f}, max={out.max():.4f}")

    rknn.release()

    # Output should be [1, 5, 2100] float32 — pre-decoded
    return outputs[0]


# ─── ONNX Inference (on macOS for comparison) ────────────────────────────────
def run_onnx(model_path: str, input_rgb: np.ndarray):
    """
    Run inference using onnxruntime on macOS.
    input_rgb: uint8 HWC RGB image at MODEL_W x MODEL_H
    """
    import onnxruntime as ort

    print(f"[INFO] Loading ONNX model: {model_path}")
    providers = ort.get_available_providers()
    session = ort.InferenceSession(model_path, providers=providers)

    # ONNX expects NCHW float32 [0,1]
    input_data = input_rgb.astype(np.float32) / 255.0
    input_data = np.transpose(input_data, (2, 0, 1))  # HWC → CHW
    input_data = input_data[np.newaxis, :, :, :]       # → NCHW
    print(f"[INFO] Input shape: {input_data.shape}, dtype={input_data.dtype}")

    input_name = session.get_inputs()[0].name
    print(f"[INFO] Running inference (providers={providers})...")
    outputs = session.run(None, {input_name: input_data})
    print(f"[INFO] Inference complete. {len(outputs)} output tensor(s)")

    for i, out in enumerate(outputs):
        print(f"[INFO] Output[{i}]: shape={out.shape}, dtype={out.dtype}, "
              f"min={out.min():.4f}, max={out.max():.4f}")

    return outputs[0]


# ─── Visualization ────────────────────────────────────────────────────────────
def draw_detections(img_bgr, detections, output_path="inference_result.jpg"):
    """Draw bounding boxes on the original image and save."""
    img_out = img_bgr.copy()
    h, w = img_out.shape[:2]

    for det in detections:
        x = int(det['x'] * w)
        y = int(det['y'] * h)
        bw = int(det['w'] * w)
        bh = int(det['h'] * h)
        conf = det['confidence']

        cv2.rectangle(img_out, (x, y), (x + bw, y + bh), (0, 255, 0), 3)
        label = f"insect {conf:.2f}"
        cv2.putText(img_out, label, (x, y - 5),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)

    cv2.imwrite(output_path, img_out)
    print(f"[INFO] Result saved to: {output_path}")
    return img_out


# ─── Main ─────────────────────────────────────────────────────────────────────
def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    image_path = sys.argv[1]
    model_path = sys.argv[2]
    use_onnx = '--onnx' in sys.argv or model_path.endswith('.onnx')

    # ── 1. Load and preprocess ────────────────────────────────────────────────
    img_bgr, input_rgb, src_w, src_h = preprocess(image_path)

    # ── 2. Run inference ──────────────────────────────────────────────────────
    if use_onnx:
        print("[INFO] Using ONNX runtime")
        output = run_onnx(model_path, input_rgb)
    else:
        print("[INFO] Using RKNN runtime (NPU)")
        output = run_rknn(model_path, input_rgb)

    # ── 3. Post-process ───────────────────────────────────────────────────────
    detections = postprocess(output, src_w, src_h)

    # ── 4. Print results ──────────────────────────────────────────────────────
    print(f"\n{'='*60}")
    print(f"RESULTS: {len(detections)} detection(s)")
    print(f"{'='*60}")
    if detections:
        for i, det in enumerate(detections):
            x_px = int(det['x'] * src_w)
            y_px = int(det['y'] * src_h)
            w_px = int(det['w'] * src_w)
            h_px = int(det['h'] * src_h)
            print(f"  [{i}] class={det['class_id']} "
                  f"bbox=({x_px},{y_px},{w_px},{h_px}) "
                  f"rel=({det['x']:.4f},{det['y']:.4f},{det['w']:.4f},{det['h']:.4f}) "
                  f"conf={det['confidence']:.4f}")
    else:
        print("  No detections above threshold.")

    # ── 5. Save visualization ─────────────────────────────────────────────────
    if not use_onnx:
        output_path = os.path.join(os.path.dirname(image_path) or '.',
                                   "inference_result.jpg")
        draw_detections(img_bgr, detections, output_path)


if __name__ == "__main__":
    main()