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

// ─── CameraHalRdkX5 ───────────────────────────────────────────────────────────
// ICameraHAL implementation for the D-Robotics RDK X5 (X5 SoC).
//
// Uses the D-Robotics SP (Smart Platform) camera API (sp_vio.h) for camera
// capture and the VPS (Video Processing Subsystem) for multi-stream scaling.
//
// The camera pipeline on X5 is:
//   VIN (MIPI CSI) → ISP (3A auto-control) → VSE/VPS (scaling channels) → DMA-BUF
//
// Unlike Rockchip platforms, the X5 does NOT expose raw V4L2 capture devices
// at /dev/video*. Instead, the pipeline is managed through the SP API which
// initialises VIN+ISP+VSE, auto-detects the connected sensor, and provides
// buffer management.
//
// Multi-stream scaling (VPS):
//   Channel 0: Full-res pass-through (1920×1080 or sensor native)
//   VPS Instance 1: Medium-res for MJPEG streaming  (640×480)
//   VPS Instance 2: Lores for BPU inference           (320×320)
//
// Platform characteristics:
//   - SoC: D-Robotics X5 (Cortex-A55 ×5)
//   - AI:  10 TOPS BPU (Brain Processing Unit)
//   - Camera: SP framework via libcamd (VIN→ISP→VSE pipeline)
//   - Scaling: VSE hardware with VPS abstraction
//   - No GPU, no RKAIQ, no Rockchip RGA
struct CameraHalRdkX5 : ICameraHAL {
    CameraHalRdkX5();
    ~CameraHalRdkX5() override;

    // Non-copyable, non-movable
    CameraHalRdkX5(const CameraHalRdkX5&) = delete;
    CameraHalRdkX5& operator=(const CameraHalRdkX5&) = delete;

    // ── ICameraHAL interface ──────────────────────────────────────────────────
    bool init(const PipelineConfig& cfg) override;
    bool acquire_frames(FrameBuffer& full, FrameBuffer& medium,
                        FrameBuffer& lores) override;
    void release_frames() override;
    void shutdown() override;

private:
    // ── Internal types ────────────────────────────────────────────────────────
    // Represents a VPS output channel buffer
    struct VpsBuffer {
        void*    data    = nullptr;  // mapped buffer pointer
        uint32_t width   = 0;
        uint32_t height  = 0;
        uint32_t size    = 0;
    };

    // ── Private methods ───────────────────────────────────────────────────────
    bool init_camera_pipeline();
    bool init_vps_channels();
    bool alloc_vps_buffer(VpsBuffer& buf, int width, int height);
    void free_vps_buffer(VpsBuffer& buf);

    // ── State ─────────────────────────────────────────────────────────────────
    PipelineConfig cfg_{};

    // SP module handle (opaque pointer from sp_init_vio_module)
    void* vio_module_ = nullptr;

    // VPS output buffers (medium and lores are scaled via VPS channels)
    VpsBuffer vps_full_;
    VpsBuffer vps_medium_;
    VpsBuffer vps_lores_;

    // Per-tick frame buffers (point to VPS output buffers)
    FrameBuffer frame_full_{};
    FrameBuffer frame_medium_{};
    FrameBuffer frame_lores_{};

    bool initialised_ = false;
};

} // namespace ct