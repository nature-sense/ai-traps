#pragma once

#include "hal/api/video_scaler.hpp"

#include <memory>
#include <string>
#include <cstdint>

namespace ct {

// ─── VideoScalerVSE ───────────────────────────────────────────────────────────
// IVideoScaler implementation using the D-Robotics VSE (Video Scaling Engine).
//
// On the RDK X5, the VSE is a dedicated hardware block that provides 6
// independent scaling channels. Unlike GPU-based scaling (OpenCL) or
// RGA-based scaling (Rockchip), the VSE operates entirely in hardware
// with zero CPU overhead and no GPU dependency.
//
// Key characteristics:
//   - 6 independent scaling channels
//   - Max input: 3840×2160@60fps
//   - Min output: 64×64
//   - Arbitrary scaling factors per channel
//   - Hardware color space conversion (to NV12)
//   - No OpenCL/OpenGL required
//
// IMPORTANT: On the RDK X5, the VSE is typically configured and managed
// directly by the CameraHalRdkX5 implementation as part of the VIN→ISP→VSE
// pipeline. This VideoScalerVSE class provides a standalone interface for
// cases where additional scaling operations are needed outside the camera
// pipeline (e.g., for inference preprocessing).
//
// For the standard 3-stream pipeline (full/medium/lores), the VSE channels
// are configured once at camera init and run continuously in hardware.
// No additional scaling calls are needed.
//
// NOTE: This is a preparatory implementation. It has NOT been tested on
//       real hardware. TODO: Test and validate on D-Robotics RDK X5.
struct VideoScalerVSE : IVideoScaler {
    VideoScalerVSE();
    ~VideoScalerVSE() override;

    // Non-copyable, non-movable
    VideoScalerVSE(const VideoScalerVSE&) = delete;
    VideoScalerVSE& operator=(const VideoScalerVSE&) = delete;

    // ── IVideoScaler interface ────────────────────────────────────────────────
    bool scale_nv12(int src_fd, int src_w, int src_h,
                    int dst_fd, int dst_w, int dst_h) override;

    bool scale_nv12_to_bgr(int src_fd, int src_w, int src_h,
                           int dst_fd, int dst_w, int dst_h) override;

    // ── Query ─────────────────────────────────────────────────────────────────
    bool is_available() const { return available_; }

private:
    // VSE device file descriptor (for standalone scaling operations)
    int vse_fd_ = -1;

    bool available_ = false;
    bool initialised_ = false;
};

} // namespace ct
