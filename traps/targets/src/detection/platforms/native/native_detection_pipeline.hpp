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
