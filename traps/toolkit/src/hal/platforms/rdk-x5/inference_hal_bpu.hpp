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

#include "dnn/hb_dnn.h"

namespace ct {

// ─── InferenceHalBPU ──────────────────────────────────────────────────────────
// IInferenceHAL implementation for the D-Robotics BPU (Brain Processing Unit)
// on the RDK X5 (X5 SoC, 10 TOPS).
//
// Uses the native libdnn.so C API (hbDNN*) for model loading and inference.
// Model format: Pre-optimized .bin files from D-Robotics Model Zoo.
//
// Key characteristics:
//   - 10 TOPS INT8 inference performance
//   - Native support for YOLO models (YOLOv5, YOLOv8, YOLOv11, YOLOv12n)
//   - Input format: NV12 at 640x640 (model-specific)
//   - Output: Raw tensor data decoded into Detection structs
//
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
    // Resize an NV12 frame to the model's expected input size
    // Simple nearest-neighbour downscale for NV12 format.
    bool resize_nv12(const FrameBuffer& src, std::vector<uint8_t>& dst,
                     int dst_w, int dst_h);

    // Decode YOLOv12n output tensors into Detection structs
    // The model produces [1, N, 6] format: [x, y, w, h, conf, class_id]
    std::vector<Detection> decode_yolo_output();

    // Non-maximum suppression
    std::vector<Detection> nms(const std::vector<Detection>& detections,
                               float iou_threshold = 0.45f);

    // ── hbDNN State ────────────────────────────────────────────────────────────
    // Packed handle returned by hbDNNInitializeFromFiles
    hbPackedDNNHandle_t packed_dnn_handle_ = nullptr;

    // Individual model handle extracted from the packed container
    hbDNNHandle_t dnn_handle_ = nullptr;

    // Input tensor (NV12 frame data mapped to BPU memory)
    hbDNNTensor input_tensor_;

    // Output tensors (allocated based on model output count)
    std::vector<hbDNNTensor> output_tensors_;

    // Number of output tensors this model produces
    int output_count_ = 0;

    // Model input dimensions (derived from model properties)
    int model_in_w_ = 640;
    int model_in_h_ = 640;

    // Model output configuration
    int num_classes_ = 80;  // COCO default, override from model metadata

    float conf_thresh_ = 0.5f;

    bool initialised_ = false;

    // Timing
    int64_t last_inference_us_ = 0;
};

} // namespace ct