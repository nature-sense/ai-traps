#include "inference_hal_bpu.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>

namespace ct {

// ─── Constructor / Destructor ─────────────────────────────────────────────────
InferenceHalBPU::InferenceHalBPU() = default;

InferenceHalBPU::~InferenceHalBPU() {
    shutdown();
}

// ─── init ─────────────────────────────────────────────────────────────────────
bool InferenceHalBPU::init(const std::string& model_path, float confidence_threshold) {
    if (initialised_) {
        std::cerr << "[InferenceHalBPU] Already initialised\n";
        return true;
    }

    conf_thresh_ = confidence_threshold;

    // 1. Load the BPU model file (.bin format)
    std::ifstream file(model_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[InferenceHalBPU] Cannot open model file: " << model_path << "\n";
        std::cerr << "[InferenceHalBPU] NOTE: This is a preparatory stub. "
                  << "A valid .bin model file is required.\n";
        std::cerr << "[InferenceHalBPU] Use the D-Robotics Model Zoo to obtain "
                  << "pre-optimized .bin models.\n";
        std::cerr << "[InferenceHalBPU] Installation: pip install bpu_infer_lib_x5 "
                  << "-i http://sdk.d-robotics.cc:8080/simple/ "
                  << "--trusted-host sdk.d-robotics.cc\n";
        return false;
    }

    model_size_ = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    model_data_ = std::malloc(model_size_);
    if (!model_data_) {
        std::cerr << "[InferenceHalBPU] Failed to allocate " << model_size_
                  << " bytes for model\n";
        return false;
    }

    file.read(static_cast<char*>(model_data_), static_cast<std::streamsize>(model_size_));
    file.close();

    std::cout << "[InferenceHalBPU] Loaded BPU model: " << model_path
              << " (" << model_size_ << " bytes)\n";

    // 2. Initialise BPU inference context
    //    TODO: Implement bpu_infer_lib integration.
    //    This requires:
    //      - Including the bpu_infer_lib headers (C++ API)
    //      - Creating an Infer object:
    //          bpu_infer_lib::Infer inf;
    //          inf.load_model(model_data_, model_size_);
    //      - Querying model input/output tensor shapes
    //      - Pre-allocating input/output buffers
    //
    //    Python reference:
    //      import bpu_infer_lib
    //      inf = bpu_infer_lib.Infer()
    //      inf.load_model("insect_detector.bin")
    //      inf.set_input(frame_320x320)
    //      inf.forward()
    //      results = inf.get_output()
    //
    //    The BPU accepts NV12 input directly for many models,
    //    eliminating the need for NV12→RGB conversion.
    //
    std::cerr << "[InferenceHalBPU] BPU integration is a PREPARATORY STUB.\n";
    std::cerr << "[InferenceHalBPU] TODO: Implement bpu_infer_lib::Infer integration.\n";

    // Free the model data since we can't use it yet
    std::free(model_data_);
    model_data_ = nullptr;
    model_size_ = 0;

    return false;
}

// ─── detect ───────────────────────────────────────────────────────────────────
std::vector<Detection> InferenceHalBPU::detect(const FrameBuffer& frame) {
    // TODO: Implement BPU inference
    // This requires:
    //   1. Preprocessing: NV12 → model input format
    //      (BPU may accept NV12 directly, or need RGB/BGR)
    //   2. Set input tensor: inf.set_input(frame_data)
    //   3. Run inference: inf.forward()
    //   4. Read output tensors: inf.get_output()
    //   5. Decode detections based on model architecture
    //   6. Apply NMS and confidence thresholding
    //
    // NOTE: Preparatory stub — returns empty detections.
    (void)frame;

    std::cerr << "[InferenceHalBPU] detect() called but NOT IMPLEMENTED "
              << "(preparatory stub)\n";
    return {};
}

// ─── nv12_to_rgb ──────────────────────────────────────────────────────────────
bool InferenceHalBPU::nv12_to_rgb(const FrameBuffer& frame, std::vector<uint8_t>& rgb_out) {
    // Convert NV12 frame to RGB planar (for BPU models that don't accept NV12)
    // NV12 layout:
    //   Y plane: width * height bytes
    //   UV plane: width * height / 2 bytes (interleaved U, V at half resolution)
    //
    // RGB output: width * height * 3 bytes (planar or interleaved)

    const uint32_t w = frame.width;
    const uint32_t h = frame.height;

    if (!frame.data || w == 0 || h == 0) {
        return false;
    }

    rgb_out.resize(w * h * 3);

    const uint8_t* y_plane = static_cast<const uint8_t*>(frame.data);
    const uint8_t* uv_plane = y_plane + w * h;

    for (uint32_t row = 0; row < h; ++row) {
        for (uint32_t col = 0; col < w; ++col) {
            uint32_t y_idx = row * w + col;
            uint32_t uv_idx = (row / 2) * w + (col / 2) * 2;

            float y  = static_cast<float>(y_plane[y_idx]);
            float u  = static_cast<float>(uv_plane[uv_idx])     - 128.0f;
            float v  = static_cast<float>(uv_plane[uv_idx + 1]) - 128.0f;

            // BT.601 limited range
            int r = static_cast<int>(y + 1.402f * v);
            int g = static_cast<int>(y - 0.344f * u - 0.714f * v);
            int b = static_cast<int>(y + 1.772f * u);

            r = std::clamp(r, 0, 255);
            g = std::clamp(g, 0, 255);
            b = std::clamp(b, 0, 255);

            // RGB interleaved output
            uint32_t rgb_idx = (row * w + col) * 3;
            rgb_out[rgb_idx + 0] = static_cast<uint8_t>(r);
            rgb_out[rgb_idx + 1] = static_cast<uint8_t>(g);
            rgb_out[rgb_idx + 2] = static_cast<uint8_t>(b);
        }
    }

    return true;
}

// ─── decode_outputs ───────────────────────────────────────────────────────────
std::vector<Detection> InferenceHalBPU::decode_outputs(const float* raw_output,
                                                        int num_outputs,
                                                        int num_classes) {
    // TODO: Implement YOLO output decoding for BPU models.
    // The output format depends on the model architecture:
    //
    // YOLOv5: [batch, num_detections, 6] where 6 = [x, y, w, h, conf, class_id]
    // YOLOv8/v11: [batch, num_classes + 4, grid_h, grid_w] with DFL (Distribution Focal Loss)
    //
    // BPU models may have a custom output format. This will need to be
    // determined experimentally once hardware is available.
    //
    // NOTE: Preparatory stub — returns empty detections.
    (void)raw_output;
    (void)num_outputs;
    (void)num_classes;

    return {};
}

// ─── nms ──────────────────────────────────────────────────────────────────────
std::vector<Detection> InferenceHalBPU::nms(const std::vector<Detection>& detections,
                                             float iou_threshold) {
    // Standard non-maximum suppression
    if (detections.empty()) return {};

    // Sort by confidence (descending)
    std::vector<Detection> sorted = detections;
    std::sort(sorted.begin(), sorted.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });

    std::vector<Detection> result;
    std::vector<bool> suppressed(sorted.size(), false);

    for (size_t i = 0; i < sorted.size(); ++i) {
        if (suppressed[i]) continue;

        result.push_back(sorted[i]);

        for (size_t j = i + 1; j < sorted.size(); ++j) {
            if (suppressed[j]) continue;
            if (sorted[i].class_id != sorted[j].class_id) continue;

            // Calculate IoU
            float xi1 = sorted[i].x;
            float yi1 = sorted[i].y;
            float xi2 = sorted[i].x + sorted[i].w;
            float yi2 = sorted[i].y + sorted[i].h;

            float xj1 = sorted[j].x;
            float yj1 = sorted[j].y;
            float xj2 = sorted[j].x + sorted[j].w;
            float yj2 = sorted[j].y + sorted[j].h;

            float inter_x1 = std::max(xi1, xj1);
            float inter_y1 = std::max(yi1, yj1);
            float inter_x2 = std::min(xi2, xj2);
            float inter_y2 = std::min(yi2, yj2);

            float inter_w = std::max(0.0f, inter_x2 - inter_x1);
            float inter_h = std::max(0.0f, inter_y2 - inter_y1);
            float inter_area = inter_w * inter_h;

            float area_i = sorted[i].w * sorted[i].h;
            float area_j = sorted[j].w * sorted[j].h;
            float union_area = area_i + area_j - inter_area;

            float iou = (union_area > 0.0f) ? inter_area / union_area : 0.0f;

            if (iou > iou_threshold) {
                suppressed[j] = true;
            }
        }
    }

    return result;
}

// ─── shutdown ─────────────────────────────────────────────────────────────────
void InferenceHalBPU::shutdown() {
    if (!initialised_) return;

    // TODO: Release BPU inference resources
    //   inf.release();
    //   bpu_ctx_ = nullptr;

    if (model_data_) {
        std::free(model_data_);
        model_data_ = nullptr;
        model_size_ = 0;
    }

    bpu_ctx_ = nullptr;
    initialised_ = false;

    std::cout << "[InferenceHalBPU] Shutdown complete\n";
}

} // namespace ct
