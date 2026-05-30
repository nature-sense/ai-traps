#pragma once

#include "pipeline/base_detection_pipeline.hpp"

namespace ct {

// ─── Rock3cDetectionPipeline ──────────────────────────────────────────────────
// Platform-specific pipeline for ROCK 3C (RK3566).
// Uses CameraHalImx219 (IMX219 sensor via V4L2 + AIQ) and
// InferenceHalRKNN (RKNN NPU).
class Rock3cDetectionPipeline : public BaseDetectionPipeline {
protected:
    std::unique_ptr<CameraActor> createCamera() override;
    std::unique_ptr<IInferenceHAL> createInferenceHAL() override;
};

} // namespace ct
