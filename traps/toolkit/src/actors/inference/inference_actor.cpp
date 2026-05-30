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

#include "inference_actor.hpp"
#include <iostream>

namespace ct {

InferenceActor::InferenceActor(std::unique_ptr<IInferenceHAL> hal)
    : hal_(std::move(hal))
{
}

bool InferenceActor::init(const std::string& model_path,
                          float confidence_threshold) {
    if (!hal_) {
        std::cerr << "[InferenceActor] no HAL provided\n";
        return false;
    }

    if (!hal_->init(model_path, confidence_threshold)) {
        std::cerr << "[InferenceActor] HAL init failed (model: "
                  << model_path << ")\n";
        return false;
    }

    std::cout << "[InferenceActor] ready (conf_thr=" << confidence_threshold
              << ", input=" << hal_->input_width() << "x" << hal_->input_height()
              << ")\n";
    return true;
}

void InferenceActor::shutdown() {
    if (hal_) {
        hal_->shutdown();
    }
    std::cout << "[InferenceActor] shutdown\n";
}

int64_t InferenceActor::last_inference_us() const {
    return hal_ ? hal_->last_inference_us() : 0;
}

} // namespace ct
