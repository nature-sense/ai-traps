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

#include "rdk_x5_detection_pipeline.hpp"

#include "camera/camera_actor.hpp"
#include "camera/camera_hal_actor.hpp"
#include "hal/platforms/rdk-x5/inference_hal_bpu.hpp"

#include <iostream>
#include <memory>

namespace ct {

// ─── Destructor ───────────────────────────────────────────────────────────────

RdkX5DetectionPipeline::~RdkX5DetectionPipeline() {
    shutdown();
}

// ─── Init ─────────────────────────────────────────────────────────────────────

bool RdkX5DetectionPipeline::init(const PipelineConfig& cfg) {
    // Initialise the base pipeline (camera, inference, actors, wiring)
    if (!BaseDetectionPipeline::init(cfg)) {
        return false;
    }

    std::cout << "[RdkX5DetectionPipeline] RDK X5 pipeline initialised\n";
    return true;
}

// ─── Shutdown ─────────────────────────────────────────────────────────────────

void RdkX5DetectionPipeline::shutdown() {
    std::cout << "[RdkX5DetectionPipeline] shutdown\n";
    BaseDetectionPipeline::shutdown();
}

// ─── Factory methods ──────────────────────────────────────────────────────────

std::unique_ptr<CameraActor> RdkX5DetectionPipeline::createCamera() {
    std::cout << "[RdkX5DetectionPipeline] creating camera for model=\""
              << cfg_.camera.model << "\"\n";
    return CameraActor::create(cfg_.camera.model);
}

std::unique_ptr<IInferenceHAL> RdkX5DetectionPipeline::createInferenceHAL() {
    // Uses the BPU inference HAL for the D-Robotics X5
    return std::make_unique<InferenceHalBPU>();
}

} // namespace ct