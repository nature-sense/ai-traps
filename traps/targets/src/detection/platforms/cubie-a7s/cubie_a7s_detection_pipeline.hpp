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

#include "pipeline/base_detection_pipeline.hpp"

namespace ct {

// ─── CubieA7sDetectionPipeline ────────────────────────────────────────────────
// Platform-specific pipeline for Radxa Cubie A7S (Allwinner A527).
// Uses CameraHalA7s (IMX415 via V4L2) and InferenceHalVIP (Vivante VIPLite NPU).
class CubieA7sDetectionPipeline : public BaseDetectionPipeline {
protected:
    std::unique_ptr<CameraActor> createCamera() override;
    std::unique_ptr<IInferenceHAL> createInferenceHAL() override;
};

} // namespace ct
