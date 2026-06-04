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

#include "hal/api/video_scaler.hpp"
#include "hal/api/frame_buffer.hpp"

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace ct {

// ─── VideoScalerOpenCL ────────────────────────────────────────────────────────
// IVideoScaler implementation using GPU-accelerated OpenCL for NV12 scaling.
//
// Uses the Mali GPU (G57 or similar on Allwinner A527) via OpenCL to perform
// bilinear interpolation on NV12 frames. This is significantly more efficient
// than CPU-based scaling and frees the CPU for other tasks.
//
// The OpenCL kernel handles Y and UV planes separately:
//   - Y plane: full-resolution luma, bilinear scaled to target
//   - UV plane: half-resolution chroma, bilinear scaled to target
//
// NOTE: This is a preparatory implementation. It has NOT been tested on
//       real hardware. TODO: Test and validate on Radxa Cubie A7S.
struct VideoScalerOpenCL : IVideoScaler {
    VideoScalerOpenCL();
    ~VideoScalerOpenCL() override;

    // Non-copyable, non-movable
    VideoScalerOpenCL(const VideoScalerOpenCL&) = delete;
    VideoScalerOpenCL& operator=(const VideoScalerOpenCL&) = delete;

    // ── IVideoScaler interface ────────────────────────────────────────────────
    bool scale_nv12(int src_fd, int src_w, int src_h,
                    int dst_fd, int dst_w, int dst_h) override;

    bool scale_nv12_to_bgr(int src_fd, int src_w, int src_h,
                           int dst_fd, int dst_w, int dst_h) override;

    // ── Convenience: scale a FrameBuffer in-place ─────────────────────────────
    // Scales src to dst dimensions. Both buffers must be pre-allocated.
    bool scale_frame(const FrameBuffer& src, FrameBuffer& dst,
                     int dst_w, int dst_h);

    // ── Query ─────────────────────────────────────────────────────────────────
    bool is_available() const { return available_; }
    std::string device_name() const;

    struct OpenCLContext;
    friend bool init_opencl(OpenCLContext& ctx);

private:
    std::unique_ptr<OpenCLContext> ctx_;

    // OpenCL kernel source for NV12 bilinear resize
    static const char* nv12_resize_kernel_source();

    bool available_ = false;
    bool initialised_ = false;
};

} // namespace ct
