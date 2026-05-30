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

#include "hal/api/camera_hal.hpp"

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace ct {

// ─── CameraHalA7s ─────────────────────────────────────────────────────────────
// ICameraHAL implementation for the Radxa Cubie A7S (Allwinner A527 SoC).
//
// Uses direct V4L2 for camera capture on the Radxa Camera 4K (IMX415 sensor).
// The Allwinner ISP is configured via standard V4L2 controls (exposure, gain,
// white balance) rather than a proprietary userspace library like Rockchip AIQ.
//
// This HAL captures a single full-resolution NV12 frame per tick and performs
// CPU-based bilinear scaling to produce the medium and lores streams (since
// the Allwinner A527 has no RGA-like hardware scaler).
//
// Platform characteristics:
//   - SoC: Allwinner A527 (Cortex-A55)
//   - Camera: Radxa Camera 4K (IMX415, 8MP, 3840×2160 max)
//   - ISP: Allwinner ISP (configured via V4L2 controls)
//   - No Rockchip AIQ, no RGA, no VPSS
//   - NPU: Vivante VIPLite (OpenVX-based, for inference)
//   - GPU: Mali G57 (OpenCL-capable, for optional video scaling)
//
// NOTE: This implementation has NOT been tested on real hardware.
//       TODO: Test and validate on Radxa Cubie A7S.
struct CameraHalA7s : ICameraHAL {
    CameraHalA7s();
    ~CameraHalA7s() override;

    // Non-copyable, non-movable
    CameraHalA7s(const CameraHalA7s&) = delete;
    CameraHalA7s& operator=(const CameraHalA7s&) = delete;

    // ── ICameraHAL interface ──────────────────────────────────────────────────
    bool init(const PipelineConfig& cfg) override;
    bool acquire_frames(FrameBuffer& full, FrameBuffer& medium,
                        FrameBuffer& lores) override;
    void release_frames() override;
    void shutdown() override;

private:
    // ── V4L2 buffer slot ──────────────────────────────────────────────────────
    struct V4l2BufSlot {
        unsigned int index  = 0;
        void*        mapped = nullptr;
        uint32_t     length = 0;
        bool         queued = false;
    };

    // ── Scaler output buffer ──────────────────────────────────────────────────
    struct ScalerBuf {
        void*    addr = nullptr;
        uint32_t size = 0;
    };

    // ── V4L2 initialisation ───────────────────────────────────────────────────
    bool init_v4l2();
    bool init_v4l2_mmap(uint32_t frame_size);

    // ── ISP control via V4L2 ──────────────────────────────────────────────────
    bool set_v4l2_controls();

    // ── CPU-based NV12 bilinear scaler ────────────────────────────────────────
    // Scales an NV12 frame from src_w×src_h to dst_w×dst_h.
    // dst buffer must be pre-allocated with size dst_w * dst_h * 3/2.
    static void scale_nv12_cpu(const uint8_t* src, uint8_t* dst,
                               int src_w, int src_h,
                               int dst_w, int dst_h);

    // ── State ─────────────────────────────────────────────────────────────────
    PipelineConfig cfg_{};

    // V4L2 device
    int v4l2_fd_ = -1;

    // V4L2 buffer pool (MMAP buffers)
    std::vector<V4l2BufSlot> v4l2_buf_pool_;

    // Scaler output buffers (pre-allocated for medium and lores)
    ScalerBuf scaler_medium_{};
    ScalerBuf scaler_lores_{};

    // Per-tick frame buffers
    FrameBuffer frame_full_{};
    FrameBuffer frame_medium_{};
    FrameBuffer frame_lores_{};

    bool initialised_ = false;
};

} // namespace ct
