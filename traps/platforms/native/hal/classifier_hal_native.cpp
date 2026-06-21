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

#include "classifier_hal_native.hpp"
#include <iostream>
#include <chrono>

namespace ct {

bool ClassifierHalNative::init(const std::string& model_path, float confidence_threshold) {
    std::cout << "[ClassifierHalNative] init (model=" << model_path
              << ", threshold=" << confidence_threshold << ")\n";
    initialized_ = true;
    return true;
}

Classification ClassifierHalNative::classify(const uint8_t* rgb_data, int width, int height,
                                               int64_t timestamp_ms) {
    if (!initialized_) return {};

    auto start = std::chrono::high_resolution_clock::now();

    // Return a dummy classification
    Classification result;
    result.class_id = 0;
    result.confidence = 0.0f;
    result.timestamp_ms = timestamp_ms;

    auto end = std::chrono::high_resolution_clock::now();
    last_classification_us_ = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    return result;
}

void ClassifierHalNative::shutdown() {
    initialized_ = false;
    std::cout << "[ClassifierHalNative] shutdown\n";
}

} // namespace ct
