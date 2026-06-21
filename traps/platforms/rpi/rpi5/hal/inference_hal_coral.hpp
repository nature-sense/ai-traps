/*
 * Copyright 2026 Nature Sense
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "hal/api/inference_hal.hpp"

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

// Forward-declare TFLite types to avoid header dependency in the .hpp
struct TfLiteTensor;
namespace tflite {
class FlatBufferModel;
class Interpreter;
}

// Forward-declare Edge TPU types
namespace edgetpu {
class EdgeTpuContext;
}

namespace ct {

// ─── InferenceHalCoral ────────────────────────────────────────────────────────
// IInferenceHAL implementation for the Coral Dual Edge TPU (PCIe).
//
// Loads a pre-compiled Edge TPU .tflite model, mmap's the ISP's RGB dmabuf
// directly as the input tensor (zero-copy), and runs inference via the Edge
// TPU delegate.
//
// Input format: RGB888 at 320×320 (from ISP Stream 3 hardware format conv).
//   - mmap dmabuf → raw tensor pointer (no memcpy to TFLite buffer)
// Output: YOLO detection tensors → decoded into Detection structs
//
// Key characteristics:
//   - Dual TPU: Two EdgeTpuContext instances for round-robin pipelining
//   - 4 TOPS per TPU (8 TOPS dual)
//   - PCIe DMA reads RGB from ISP dmabuf pages
//   - Zero CPU pixel processing
struct InferenceHalCoral : IInferenceHAL {
    InferenceHalCoral();
    ~InferenceHalCoral() override;

    // Non-copyable, non-movable
    InferenceHalCoral(const InferenceHalCoral&) = delete;
    InferenceHalCoral& operator=(const InferenceHalCoral&) = delete;

    // ── IInferenceHAL interface ────────────────────────────────────────────────
    bool init(const std::string& model_path, float confidence_threshold) override;
    std::vector<Detection> detect(const FrameBuffer& frame) override;

    int64_t last_inference_us() const override { return last_inference_us_; }
    uint32_t input_width() const override { return static_cast<uint32_t>(model_in_w_); }
    uint32_t input_height() const override { return static_cast<uint32_t>(model_in_h_); }

    void shutdown() override;

private:
    // ── Internal helpers ──────────────────────────────────────────────────────
    // Decode YOLO output tensors into Detection structs.
    // The model produces output tensors containing boxes, scores, and classes.
    std::vector<Detection> decode_yolo_output();

    // Non-maximum suppression
    std::vector<Detection> nms(const std::vector<Detection>& detections,
                                float iou_threshold = 0.45f);

    // ── TFLite + Edge TPU state ──────────────────────────────────────────────
    std::unique_ptr<tflite::FlatBufferModel> model_;
    std::unique_ptr<tflite::Interpreter> interpreter_;
    std::unique_ptr<edgetpu::EdgeTpuContext> edgetpu_context_;

    // TFLite tensor pointers (raw pointers into interpreter internals)
    TfLiteTensor* input_tensor_  = nullptr;  // RGB input (we set pointer to mmap)
    TfLiteTensor* output_boxes_  = nullptr;  // [N][4] bounding boxes
    TfLiteTensor* output_scores_ = nullptr;  // [N] confidence scores
    TfLiteTensor* output_classes_= nullptr;  // [N] class IDs
    TfLiteTensor* output_count_  = nullptr;  // [1] number of detections

    // Model dimensions
    int model_in_w_ = 320;
    int model_in_h_ = 320;
    int input_tensor_index_ = 0;

    // Config
    float conf_thresh_ = 0.5f;
    int num_classes_ = 80;  // COCO default, overridden from config

    volatile bool initialised_ = false;

    // Timing
    int64_t last_inference_us_ = 0;

    // For dual-TPU round-robin (future): which TPU to use next tick
    int tpu_toggle_ = 0;
};

} // namespace ct