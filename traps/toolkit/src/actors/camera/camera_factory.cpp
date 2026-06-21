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

#include "camera_actor.hpp"
#include "camera_hal_actor.hpp"
#include "scene_camera_actor.hpp"
#include "../platforms/common/camera_hal_v4l2.hpp"
#include <iostream>
#include <algorithm>

// Platform-specific HAL includes are guarded by compile-time defines
// set by the build system for each target platform.
#if defined(HAVE_RKAIQ) || defined(HAVE_RKNN) || defined(HAVE_RGA)
#include "../platforms/radxa/rock-3c/hal/camera_hal_imx219.hpp"
#include "../platforms/radxa/rock-3c/hal/camera_hal_imx415.hpp"
#include "../platforms/radxa/rock-3c/hal/camera_hal_ov5647.hpp"
#include "../platforms/radxa/rock-3c/hal/camera_hal_v4l2_rkaiq.hpp"
#endif

#if defined(HAVE_A7S)
#include "../platforms/radxa/cubie-a7s/hal/camera_hal_a7s.hpp"
#endif
#if defined(HAVE_SOFTENC)
#include "../platforms/d-robotics/rdk-x5/hal/camera_hal_rdkx5.hpp"
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
#if defined(HAVE_A7S)
        auto hal = std::make_unique<CameraHalA7s>();
        std::cout << "[CameraActor] Creating CameraHalActor wrapping CameraHalA7s (V4L2)\n";
        return std::make_unique<CameraHalActor>(std::move(hal));
#else
        std::cerr << "[CameraActor] A7S HAL not available on this platform\n";
        return nullptr;
#endif
    }


    // D-Robotics RDK X5: SP camera API (VIN→ISP→VSE pipeline via libcamd)
    if (model == "rdkx5" || model == "rdk-x5") {
#if defined(HAVE_SOFTENC)
        auto hal = std::make_unique<CameraHalRdkX5>();
        std::cout << "[CameraActor] Creating CameraHalActor wrapping CameraHalRdkX5 (SP API)\n";
        return std::make_unique<CameraHalActor>(std::move(hal));
#else
        std::cerr << "[CameraActor] RDK X5 HAL not available on this platform\n";
        return nullptr;
#endif
    }

    if (model == "ov5647" || model == "ov5647_actor") {
#if defined(HAVE_RKAIQ) || defined(HAVE_RKNN) || defined(HAVE_RGA)
        auto hal = std::make_unique<CameraHalOv5647>();
        std::cout << "[CameraActor] Creating CameraHalActor wrapping OV5647\n";
        return std::make_unique<CameraHalActor>(std::move(hal));
#else
        std::cerr << "[CameraActor] OV5647 HAL not available on this platform\n";
        return nullptr;
#endif
    }

    // Generic V4L2 HAL — works on any board with any V4L2 capture device.
    // No compile-time guards, no platform-specific code paths.
    // Auto-discovers the capture device, negotiates format, and manages
    // MMAP buffers. Camera controls (exposure, gain, WB) use V4L2 ioctls.
    //
    // On Rockchip platforms (HAVE_RKAIQ defined), we use CameraHalV4L2Rkaiq
    // which bootstraps the ISP via RKAIQ before handing off to raw V4L2.
    // On other platforms (Cubie A7S, RDK X5, native), we use the base
    // CameraHalV4L2 which works with kernel sensors that expose full V4L2.
    if (model == "v4l2" || model == "v4l2_generic") {
#if defined(HAVE_RKAIQ) || defined(HAVE_RKNN) || defined(HAVE_RGA)
        auto hal = std::make_unique<CameraHalV4L2Rkaiq>();
        std::cout << "[CameraActor] Creating CameraHalActor wrapping CameraHalV4L2Rkaiq (V4L2+RKAIQ)\n";
#else
        auto hal = std::make_unique<CameraHalV4L2>();
        std::cout << "[CameraActor] Creating CameraHalActor wrapping CameraHalV4L2 (generic)\n";
#endif
        return std::make_unique<CameraHalActor>(std::move(hal));
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
