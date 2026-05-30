#include "cubie_a7s_detection_pipeline.hpp"

#include "camera/camera_hal_actor.hpp"
#include "hal/platforms/cubie-a7s/camera_hal_a7s.hpp"
#include "hal/platforms/cubie-a7s/inference_hal_vip.hpp"

#include <memory>

namespace ct {

std::unique_ptr<CameraActor> CubieA7sDetectionPipeline::createCamera() {
    auto hal = std::make_unique<CameraHalA7s>();
    auto actor = std::make_unique<CameraHalActor>(std::move(hal));
    return actor;
}

std::unique_ptr<IInferenceHAL> CubieA7sDetectionPipeline::createInferenceHAL() {
    return std::make_unique<InferenceHalVIP>();
}

} // namespace ct
