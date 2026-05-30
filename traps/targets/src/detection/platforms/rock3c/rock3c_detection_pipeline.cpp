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

#include "rock3c_detection_pipeline.hpp"

#include "camera/camera_hal_actor.hpp"
#include "hal/platforms/rock3c/camera_hal_imx415.hpp"
#include "hal/platforms/rock3c/inference_hal_rknn.hpp"

#include <memory>

namespace ct {

std::unique_ptr<CameraActor> Rock3cDetectionPipeline::createCamera() {
    auto hal = std::make_unique<CameraHalImx415>();
    auto actor = std::make_unique<CameraHalActor>(std::move(hal));
    return actor;
}

std::unique_ptr<IInferenceHAL> Rock3cDetectionPipeline::createInferenceHAL() {
    return std::make_unique<InferenceHalRKNN>();
}

} // namespace ct
