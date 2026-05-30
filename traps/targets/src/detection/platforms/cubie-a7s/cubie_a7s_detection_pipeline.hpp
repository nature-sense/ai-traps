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
