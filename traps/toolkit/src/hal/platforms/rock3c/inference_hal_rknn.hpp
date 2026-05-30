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

extern "C" {
#include <rknn_api.h>
}

namespace ct {

// ─── InferenceHalRKNN ────────────────────────────────────────────────────────
// Concrete IInferenceHAL implementation for Rockchip RKNN NPU (RK3566, RV1106).
// Wraps the Rockchip RKNN API (librknnrt) for YOLO inference.
//
// Platform differences from RV1106:
//   - Uses librknnrt (full runtime) instead of librknnmrt (mini-runtime)
//   - RKNN API v3.x (vs v2.x on RV1106)
//   - No AIQ pause/resume needed (separate ISP/NPU DMA engines)
//   - Supports both INT8 and FP16 inference
//   - NPU can execute Softmax natively (no DFL stripping needed)
class InferenceHalRKNN : public IInferenceHAL {
public:
    InferenceHalRKNN();
    ~InferenceHalRKNN() override;

    // Non-copyable, non-movable
    InferenceHalRKNN(const InferenceHalRKNN&) = delete;
    InferenceHalRKNN& operator=(const InferenceHalRKNN&) = delete;

    // ── IInferenceHAL interface ────────────────────────────────────────────────
    bool init(const std::string& model_path, float confidence_threshold) override;
    std::vector<Detection> detect(const FrameBuffer& frame) override;

    int64_t last_inference_us() const override { return last_inference_us_; }
    uint32_t input_width() const override { return static_cast<uint32_t>(model_in_w_); }
    uint32_t input_height() const override { return static_cast<uint32_t>(model_in_h_); }

    void shutdown() override;

private:
    // NV12 → RGB bilinear resize
    static void nv12_to_rgb_resize(const uint8_t* src, int src_w, int src_h,
                                    uint8_t* dst, int dst_w, int dst_h);

    // Non-maximum suppression
    static void nms(std::vector<Detection>& dets, float iou_thresh);

    // ── State ─────────────────────────────────────────────────────────────────
    rknn_context ctx_ = 0;       // RKNN context handle
    void* model_data_ = nullptr; // Raw model file data
    size_t model_size_ = 0;      // Model file size

    int model_in_w_ = 0;
    int model_in_h_ = 0;
    int model_in_c_ = 0;

    float conf_thresh_ = 0.5f;

    // RKNN tensor I/O descriptors (set once in init)
    rknn_input_output_num io_num_{};
    rknn_input  input_{};
    rknn_output output_{};

    bool initialised_ = false;

    // Timing
    int64_t last_inference_us_ = 0;
};

} // namespace ct
