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

#include "inference_hal_native.hpp"
#include <iostream>

namespace ct {

bool InferenceHalNative::init(const std::string& model_path, float confidence_threshold) {
    std::cout << "[InferenceHalNative] init model=" << model_path
              << " conf=" << confidence_threshold << " (stub)\n";
    initialized_ = true;
    return true;
}

std::vector<Detection> InferenceHalNative::detect(const FrameBuffer& frame) {
    // Stub: return empty detections
    last_us_ = 1000; // pretend 1ms inference
    return {};
}

int64_t InferenceHalNative::last_inference_us() const {
    return last_us_;
}

uint32_t InferenceHalNative::input_width() const {
    return input_width_;
}

uint32_t InferenceHalNative::input_height() const {
    return input_height_;
}

void InferenceHalNative::shutdown() {
    std::cout << "[InferenceHalNative] shutdown (stub)\n";
    initialized_ = false;
}

} // namespace ct
