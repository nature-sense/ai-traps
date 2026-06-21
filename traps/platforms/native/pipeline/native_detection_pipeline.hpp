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

// ─── NativeDetectionPipeline ──────────────────────────────────────────────────
// Platform-specific pipeline for macOS/Linux (host) development.
// Uses CameraHalNative (synthetic frames) and InferenceHalNative (stub).
class NativeDetectionPipeline : public BaseDetectionPipeline {
protected:
    std::unique_ptr<CameraActor> createCamera() override;
    std::unique_ptr<IInferenceHAL> createInferenceHAL() override;
};

} // namespace ct
