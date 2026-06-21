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

// V4L2 types needed for the interface (buf_type_ member)
#include <linux/videodev2.h>
#include <linux/media.h>

namespace ct {

// ─── CameraHalV4L2 ─────────────────────────────────────────────────────────────
// ICameraHAL implementation using generic V4L2 capture, shared across all
// platforms (ROCK 3C, Cubie A7S, RDK X5, host/native).
//
// This HAL works with any sensor, webcam, or capture device that exposes
// a V4L2 interface. It auto-discovers the capture device, negotiates the
// best pixel format (NV12 preferred), and handles all buffer management
// via MMAP. Scaling to medium/lores outputs is done via CPU bilinear
// interpolation (or an optional hardware scaler if the platform provides one).
//
// Camera controls (exposure, gain, white balance) are set via standard
// V4L2 ioctls (VIDIOC_S_CTRL) — no ISP tuning or proprietary SDK required.
//
// Works on:
//   - ROCK 3C (rkisp ISP driver)
//   - Cubie A7S (sunxi-vin ISP driver)
//   - RDK X5 (HBN framework V4L2)
//   - macOS host (USB UVC webcams via VirtualHere or native)
//   - Any Linux board with V4L2 capture devices
//
// No compile-time guards or per-platform code paths — all adaptation is
// done at runtime via V4L2 capability queries and device discovery.
struct CameraHalV4L2 : ICameraHAL {
    CameraHalV4L2();
    ~CameraHalV4L2() override;

    // Non-copyable, non-movable
    CameraHalV4L2(const CameraHalV4L2&) = delete;
    CameraHalV4L2& operator=(const CameraHalV4L2&) = delete;

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

    // ── Device discovery ──────────────────────────────────────────────────────
    // Tries the configured device path first, then scans /sys/class/video4linux/
    // for ISP capture devices, finally tries common device numbers.
    int discover_device();

    // ── Media controller pipeline ─────────────────────────────────────────────
    // Some drivers (sunxi-vin on Allwinner) require the sensor subdev to be
    // configured before VIDIOC_S_FMT succeeds. This attempts that; failures
    // are non-fatal since most drivers don't need it.
    static bool try_configure_media_pipeline(int video_fd, int width, int height, int fps);

    // ── CPU-based NV12 bilinear scaler ────────────────────────────────────────
    // Scales an NV12 frame from src_w×src_h to dst_w×dst_h.
    // dst buffer must be pre-allocated with size dst_w * dst_h * 3/2.
    static void scale_nv12_cpu(const uint8_t* src, uint8_t* dst,
                               int src_w, int src_h,
                               int dst_w, int dst_h);

    // ── Helpers ───────────────────────────────────────────────────────────────
    /// Return a human-readable name for a V4L2 pixel format.
    static const char* v4l2_fmt_name(uint32_t fourcc);

    // ── State ─────────────────────────────────────────────────────────────────
    PipelineConfig cfg_{};

    // V4L2 device
    int v4l2_fd_ = -1;

    // Multiplanar API flag and buffer type
    bool     mplane_    = false;
    uint32_t buf_type_  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    uint32_t v4l2_stride_ = 0;
    uint32_t v4l2_pix_fmt_ = 0;  // Actual pixel format from driver

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