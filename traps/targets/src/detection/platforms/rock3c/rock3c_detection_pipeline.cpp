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
