#pragma once

#include "hal/api/inference_hal.hpp"

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

namespace ct {

// ─── InferenceHalBPU ──────────────────────────────────────────────────────────
// IInferenceHAL implementation for the D-Robotics BPU (Brain Processing Unit)
// on the RDK X5 (X5 SoC, 10 TOPS).
//
// Uses the bpu_infer_lib Python/C++ library for model loading and inference.
// Model format: Pre-optimized .bin files from D-Robotics Model Zoo.
//
// Key characteristics:
//   - 10 TOPS INT8 inference performance
//   - Native support for YOLO models (YOLOv5, YOLOv8, YOLOv11)
//   - Input format: NV12 or BGR (model-dependent)
//   - Model path: Pre-optimized .bin files
//
// Installation:
//   pip install bpu_infer_lib_x5 -i http://sdk.d-robotics.cc:8080/simple/ \
//     --trusted-host sdk.d-robotics.cc
//
// NOTE: This is a PREPARATORY STUB. It has NOT been tested on real hardware
//       and requires a valid .bin model file to function. The init() method
//       will return false until a valid model is provided.
//       TODO: Implement full BPU integration once hardware is available.
struct InferenceHalBPU : IInferenceHAL {
    InferenceHalBPU();
    ~InferenceHalBPU() override;

    // Non-copyable, non-movable
    InferenceHalBPU(const InferenceHalBPU&) = delete;
    InferenceHalBPU& operator=(const InferenceHalBPU&) = delete;

    // ── IInferenceHAL interface ────────────────────────────────────────────────
    bool init(const std::string& model_path, float confidence_threshold) override;
    std::vector<Detection> detect(const FrameBuffer& frame) override;

    int64_t last_inference_us() const override { return last_inference_us_; }
    uint32_t input_width() const override { return static_cast<uint32_t>(model_in_w_); }
    uint32_t input_height() const override { return static_cast<uint32_t>(model_in_h_); }

    void shutdown() override;

private:
    // ── Internal helpers ──────────────────────────────────────────────────────
    // Convert NV12 frame to RGB planar (for BPU input if needed)
    // BPU may accept NV12 directly depending on the model configuration.
    bool nv12_to_rgb(const FrameBuffer& frame, std::vector<uint8_t>& rgb_out);

    // Decode raw BPU output tensors into Detection structs
    // The output format depends on the model architecture (YOLOv5/v8/v11).
    std::vector<Detection> decode_outputs(const float* raw_output,
                                          int num_outputs,
                                          int num_classes);

    // Non-maximum suppression
    std::vector<Detection> nms(const std::vector<Detection>& detections,
                               float iou_threshold = 0.45f);

    // ── State ─────────────────────────────────────────────────────────────────
    // BPU inference context (opaque handle to bpu_infer_lib::Infer)
    void* bpu_ctx_ = nullptr;

    // Model data (loaded from .bin file)
    void*  model_data_ = nullptr;
    size_t model_size_ = 0;

    int model_in_w_ = 320;  // Default YOLO input size
    int model_in_h_ = 320;
    int model_in_c_ = 3;

    // Model output configuration
    int num_outputs_  = 0;   // Number of output tensors
    int num_classes_  = 80;  // COCO default, override from model metadata
    int output_size_  = 0;   // Total output elements

    float conf_thresh_ = 0.5f;

    bool initialised_ = false;

    // Timing
    int64_t last_inference_us_ = 0;
};

} // namespace ct
