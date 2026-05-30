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

#include "native_detection_pipeline.hpp"

#include "camera/camera_hal_actor.hpp"
#include "hal/platforms/native/camera_hal_native.hpp"
#include "hal/platforms/native/inference_hal_native.hpp"

#include <memory>

namespace ct {

std::unique_ptr<CameraActor> NativeDetectionPipeline::createCamera() {
    // Use CameraHalActor wrapping CameraHalNative for synthetic frames
    auto hal = std::make_unique<CameraHalNative>();
    auto actor = std::make_unique<CameraHalActor>(std::move(hal));
    return actor;
}

std::unique_ptr<IInferenceHAL> NativeDetectionPipeline::createInferenceHAL() {
    return std::make_unique<InferenceHalNative>();
}

} // namespace ct
