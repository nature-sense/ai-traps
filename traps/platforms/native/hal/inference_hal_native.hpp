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
#include <cstdint>
#include <string>
#include <vector>

namespace ct {

// ─── InferenceHalNative ──────────────────────────────────────────────────────
// Stub inference HAL for host (macOS/Linux) development.
// Returns empty detections for testing the pipeline without real hardware.
struct InferenceHalNative : IInferenceHAL {
    bool init(const std::string& model_path, float confidence_threshold) override;
    std::vector<Detection> detect(const FrameBuffer& frame) override;
    int64_t last_inference_us() const override;
    uint32_t input_width() const override;
    uint32_t input_height() const override;
    void shutdown() override;

private:
    bool     initialized_ = false;
    uint32_t input_width_  = 320;
    uint32_t input_height_ = 320;
    int64_t  last_us_ = 0;
};

} // namespace ct
