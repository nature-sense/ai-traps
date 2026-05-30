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

#include "hal/api/classifier_hal.hpp"
#include <string>

namespace ct {

// ─── ClassifierHalNative ─────────────────────────────────────────────────────
// Native (host) stub classifier HAL.
// Returns a dummy classification (class_id=0, confidence=0).
// Used for testing the classification pipeline on the development machine.
class ClassifierHalNative : public IClassifierHAL {
public:
    ClassifierHalNative() = default;
    ~ClassifierHalNative() override = default;

    bool init(const std::string& model_path, float confidence_threshold) override;
    Classification classify(const uint8_t* rgb_data, int width, int height,
                             int64_t timestamp_ms) override;
    void shutdown() override;
    int64_t last_classification_us() const override { return last_classification_us_; }

private:
    bool initialized_ = false;
    int64_t last_classification_us_ = 0;
};

} // namespace ct
