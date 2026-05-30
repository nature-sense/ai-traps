#include "camera_actor.hpp"
#include "camera_hal_actor.hpp"
#include "scene_camera_actor.hpp"
#include <iostream>
#include <algorithm>

// Platform-specific HAL includes are guarded by compile-time defines
// set by the build system for each target platform.
#if defined(HAVE_RKAIQ) || defined(HAVE_RKNN) || defined(HAVE_RGA)
#include "hal/platforms/rock3c/camera_hal_imx219.hpp"
#include "hal/platforms/rock3c/camera_hal_imx415.hpp"
#endif

// CameraHalA7s uses V4L2 directly (no libcamera dependency)
#include "hal/platforms/cubie-a7s/camera_hal_a7s.hpp"

#if defined(HAVE_BPU)
#include "hal/platforms/rdk-x5/camera_hal_rdkx5.hpp"
#endif

namespace ct {

std::unique_ptr<CameraActor> CameraActor::create(const std::string& camera_model) {
    // Normalise to lowercase for case-insensitive matching
    std::string model = camera_model;
    std::transform(model.begin(), model.end(), model.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Camera-specific models use the HAL abstraction layer.
    if (model == "imx219" || model == "imx219_actor") {
#if defined(HAVE_RKAIQ) || defined(HAVE_RKNN) || defined(HAVE_RGA)
        auto hal = std::make_unique<CameraHalImx219>();
        std::cout << "[CameraActor] Creating CameraHalActor wrapping IMX219\n";
        return std::make_unique<CameraHalActor>(std::move(hal));
#else
        std::cerr << "[CameraActor] IMX219 HAL not available on this platform\n";
        return nullptr;
#endif
    }

    if (model == "imx415" || model == "imx415_actor") {
#if defined(HAVE_RKAIQ) || defined(HAVE_RKNN) || defined(HAVE_RGA)
        auto hal = std::make_unique<CameraHalImx415>();
        std::cout << "[CameraActor] Creating CameraHalActor wrapping IMX415\n";
        return std::make_unique<CameraHalActor>(std::move(hal));
#else
        std::cerr << "[CameraActor] IMX415 HAL not available on this platform\n";
        return nullptr;
#endif
    }

    // Radxa Cubie A7S: Radxa Camera 4K (IMX415) via V4L2
    if (model == "a7s" || model == "cubie-a7s" || model == "os08a10") {
        auto hal = std::make_unique<CameraHalA7s>();
        std::cout << "[CameraActor] Creating CameraHalActor wrapping CameraHalA7s (V4L2)\n";
        return std::make_unique<CameraHalActor>(std::move(hal));
    }

    // D-Robotics RDK X5: V4L2 + VSE hardware multi-stream
    if (model == "rdkx5" || model == "rdk-x5") {
#if defined(HAVE_BPU)
        auto hal = std::make_unique<CameraHalRdkX5>();
        std::cout << "[CameraActor] Creating CameraHalActor wrapping CameraHalRdkX5 (V4L2+VSE)\n";
        return std::make_unique<CameraHalActor>(std::move(hal));
#else
        std::cerr << "[CameraActor] CameraHalRdkX5 not available on this platform\n";
        return nullptr;
#endif
    }

    if (model == "scene" || model == "scene_camera_actor") {
        std::cout << "[CameraActor] Creating SceneCameraActor\n";
        return std::make_unique<SceneCameraActor>();
    }

    // Unknown model — log warning and default to IMX219
    std::cerr << "[CameraActor] Unknown camera model '" << camera_model
              << "', defaulting to CameraHalImx219\n";
#if defined(HAVE_RKAIQ) || defined(HAVE_RKNN) || defined(HAVE_RGA)
    auto hal = std::make_unique<CameraHalImx219>();
    return std::make_unique<CameraHalActor>(std::move(hal));
#else
    std::cerr << "[CameraActor] No default HAL available on this platform\n";
    return nullptr;
#endif
}

} // namespace ct
