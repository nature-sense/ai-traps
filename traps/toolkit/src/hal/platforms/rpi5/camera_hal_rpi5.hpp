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

// ─── CameraHalRpi5 ───────────────────────────────────────────────────────────
// ICameraHAL implementation for Raspberry Pi 5 (BCM2712, VideoCore VII).
//
// Uses libcamera for the Pi Camera 3 (IMX708) with 4 hardware streams:
//   Stream 0: 1920×1080 NV12  — full-res for JPEG crops + H.264 stream
//   Stream 1: 640×480  NV12  — medium-res for overlay (bounding boxes)
//   Stream 2: 320×320  NV12  — lores for... (reserved)
//   Stream 3: 320×320  RGB   — direct Coral Edge TPU input (no CPU format conv)
//
// All scaling and format conversion is done by the ISP in hardware.
// Zero CPU pixel processing. All buffers are dmabuf-backed.
//
// Platform characteristics:
//   - SoC: Broadcom BCM2712 (4x Cortex-A76)
//   - Camera: libcamera + Pi Camera 3 module (IMX708)
//   - ISP: VideoCore VII (on-chip, 4-stream pipeline)
//   - H.264: V4L2 M2M /dev/video11
//   - JPEG: V4L2 M2M /dev/video31 (with hardware crop)
//   - AI: Coral Dual Edge TPU (PCIe)
//   - GPU: VideoCore VII (optional OpenGL ES 3.1)
struct CameraHalRpi5 : ICameraHAL {
    CameraHalRpi5();
    ~CameraHalRpi5() override;

    // Non-copyable, non-movable
    CameraHalRpi5(const CameraHalRpi5&) = delete;
    CameraHalRpi5& operator=(const CameraHalRpi5&) = delete;

    // ── ICameraHAL interface ──────────────────────────────────────────────────
    bool init(const PipelineConfig& cfg) override;
    bool acquire_frames(FrameBuffer& full, FrameBuffer& medium,
                        FrameBuffer& lores) override;
    void release_frames() override;
    void shutdown() override;

private:
    // Opaque libcamera handles (forward-declared to avoid libcamera headers in header)
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Configuration
    PipelineConfig cfg_{};
    bool initialised_ = false;
};

} // namespace ct