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
