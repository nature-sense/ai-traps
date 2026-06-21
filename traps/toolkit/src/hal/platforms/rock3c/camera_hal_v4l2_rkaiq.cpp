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

#include "camera_hal_v4l2_rkaiq.hpp"

#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>

namespace ct {

// ─── Callbacks ────────────────────────────────────────────────────────────────

static XCamReturn aiq_err_cb(rk_aiq_err_msg_t* msg) {
    if (msg->err_code == XCAM_RETURN_BYPASS) {
        std::cerr << "[CameraHalV4L2Rkaiq] AIQ fatal error, should quit\n";
    }
    return XCAM_RETURN_NO_ERROR;
}

static XCamReturn aiq_sof_cb(rk_aiq_metas_t* meta) {
    static int cnt = 0;
    if (++cnt <= 2)
        std::cout << "[CameraHalV4L2Rkaiq] AIQ SOF frame_id=" << meta->frame_id << "\n";
    return XCAM_RETURN_NO_ERROR;
}

// ─── Constructor / Destructor ─────────────────────────────────────────────────

CameraHalV4L2Rkaiq::CameraHalV4L2Rkaiq() = default;

CameraHalV4L2Rkaiq::~CameraHalV4L2Rkaiq() {
    shutdown();
}

// ─── init_aiq ─────────────────────────────────────────────────────────────────
// Minimal RKAIQ bootstrap: initializes the ISP pipeline just enough for
// raw V4L2 capture to work. No per-sensor IQ tuning files are used beyond
// what RKAIQ discovers automatically.
bool CameraHalV4L2Rkaiq::init_aiq(const std::string& iq_dir) {
    // Must set HDR_MODE before AIQ init (required by Rockchip AIQ)
    setenv("HDR_MODE", "0", 1);

    rk_aiq_static_info_t static_info{};
    XCamReturn xret = rk_aiq_uapi2_sysctl_enumStaticMetasByPhyId(0, &static_info);
    if (xret != XCAM_RETURN_NO_ERROR) {
        std::cerr << "[CameraHalV4L2Rkaiq] enumStaticMetasByPhyId failed: " << xret << "\n";
        return false;
    }
    const char* sns_name = static_info.sensor_info.sensor_name;
    std::cout << "[CameraHalV4L2Rkaiq] AIQ sensor: " << sns_name << "\n";

    rk_aiq_uapi2_sysctl_preInit_devBufCnt(sns_name, "rkraw_rx", 2);

    // Try scene preinit — non-fatal if unsupported
    int scene_ret = rk_aiq_uapi2_sysctl_preInit_scene(sns_name, "normal", "");
    if (scene_ret < 0) {
        std::cerr << "[CameraHalV4L2Rkaiq] preInit_scene failed: "
                  << scene_ret << " (continuing anyway)\n";
    }

    aiq_ctx_ = rk_aiq_uapi2_sysctl_init(sns_name, iq_dir.c_str(), aiq_err_cb, aiq_sof_cb);
    if (!aiq_ctx_) {
        std::cerr << "[CameraHalV4L2Rkaiq] rk_aiq_uapi2_sysctl_init failed\n";
        return false;
    }

    xret = rk_aiq_uapi2_sysctl_prepare(aiq_ctx_, 0, 0, RK_AIQ_WORKING_MODE_NORMAL);
    if (xret != XCAM_RETURN_NO_ERROR) {
        std::cerr << "[CameraHalV4L2Rkaiq] rk_aiq_uapi2_sysctl_prepare failed: " << xret << "\n";
        return false;
    }
    std::cout << "[CameraHalV4L2Rkaiq] rk_aiq_uapi2_sysctl_prepare succeeded\n";

    xret = rk_aiq_uapi2_sysctl_start(aiq_ctx_);
    if (xret != XCAM_RETURN_NO_ERROR) {
        std::cerr << "[CameraHalV4L2Rkaiq] rk_aiq_uapi2_sysctl_start failed: " << xret << "\n";
        return false;
    }

    std::cout << "[CameraHalV4L2Rkaiq] RK_AIQ started, ISP pipeline live\n";

    // Wait for ISP to stabilize before V4L2 capture
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Lock AE/AWB to prevent RKAIQ from competing with V4L2 controls
    rk_aiq_uapi2_setAeLock(aiq_ctx_, true);
    rk_aiq_uapi2_lockAWB(aiq_ctx_);
    std::cout << "[CameraHalV4L2Rkaiq] AE/AWB locked\n";

    return true;
}

// ─── init ─────────────────────────────────────────────────────────────────────
// 1. Bootstrap RKAIQ to wake the sensor and enable the ISP pipeline
// 2. Delegate to CameraHalV4L2 for all V4L2 capture setup
bool CameraHalV4L2Rkaiq::init(const PipelineConfig& cfg) {
    // Step 1: Bootstrap RKAIQ
    if (!init_aiq(cfg.camera.iq_dir)) {
        std::cerr << "[CameraHalV4L2Rkaiq] RKAIQ bootstrap failed\n";
        return false;
    }

    // Step 2: Delegate to base V4L2 HAL (format negotiation, buffer setup,
    // VIDIOC_STREAMON). The ISP is already streaming from step 1, so
    // the V4L2 video node should respond correctly now.
    if (!CameraHalV4L2::init(cfg)) {
        std::cerr << "[CameraHalV4L2Rkaiq] V4L2 init failed (after AIQ bootstrap)\n";
        return false;
    }

    std::cout << "[CameraHalV4L2Rkaiq] ready\n";
    return true;
}

// ─── shutdown ─────────────────────────────────────────────────────────────────
void CameraHalV4L2Rkaiq::shutdown() {
    std::cout << "[CameraHalV4L2Rkaiq] shutdown\n";

    // Stop RKAIQ first
    if (aiq_ctx_) {
        rk_aiq_uapi2_sysctl_stop(aiq_ctx_, false);
        rk_aiq_uapi2_sysctl_deinit(aiq_ctx_);
        aiq_ctx_ = nullptr;
        std::cout << "[CameraHalV4L2Rkaiq] RKAIQ stopped\n";
    }

    // Parent V4L2 shutdown (STREAMOFF, unmap, close)
    CameraHalV4L2::shutdown();
}

} // namespace ct