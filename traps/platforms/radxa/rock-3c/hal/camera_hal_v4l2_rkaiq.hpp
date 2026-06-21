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

#include "hal/platforms/common/camera_hal_v4l2.hpp"

// RK_AIQ: ISP 3A control — minimal bootstrap for sensor pipeline
extern "C" {
#include "uAPI2/rk_aiq_user_api2_sysctl.h"
#include "uAPI2/rk_aiq_user_api2_imgproc.h"
#include "uAPI2/rk_aiq_user_api2_ae.h"
}

namespace ct {

// ─── CameraHalV4L2Rkaiq ───────────────────────────────────────────────────────
// RKAIQ-bootstrapped V4L2 capture for Rockchip platforms (ROCK 3C).
//
// The rkisp_v5 ISP driver on RK3566/68 requires the RKAIQ userspace library
// to initialize the CSI-2/ISP media pipeline before raw V4L2 streaming works.
// This wrapper:
//   1. Calls rk_aiq_uapi2_sysctl_init/prepare/start to wake the sensor
//   2. Delegates the rest to CameraHalV4L2 (V4L2 format negotiation, capture)
//
// No per-sensor IQ tuning files are loaded — RKAIQ is used minimally just to
// enable the ISP pipeline. Sensor controls (AE, AWB, gain) are managed via
// standard V4L2 controls, not via RKAIQ's 3A algorithms.
//
// Platform: ROCK 3C (RK3566/RK3568) with rkisp_v5 driver
struct CameraHalV4L2Rkaiq : CameraHalV4L2 {
    CameraHalV4L2Rkaiq();
    ~CameraHalV4L2Rkaiq() override;

    // Non-copyable, non-movable
    CameraHalV4L2Rkaiq(const CameraHalV4L2Rkaiq&) = delete;
    CameraHalV4L2Rkaiq& operator=(const CameraHalV4L2Rkaiq&) = delete;

    // ── ICameraHAL interface ──────────────────────────────────────────────────
    bool init(const PipelineConfig& cfg) override;
    void shutdown() override;

private:
    // ── RKAIQ bootstrap ───────────────────────────────────────────────────────
    bool init_aiq(const std::string& iq_dir);

    // RKAIQ context handle
    rk_aiq_sys_ctx_t* aiq_ctx_ = nullptr;
};

} // namespace ct