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
// Uses V4L2 with the HBN (Horizon Brain) framework for camera capture and
// the VSE (Video Scaling Engine) for hardware multi-stream scaling.
//
// The VSE provides 6 independent scaling channels, allowing us to produce
// all three output streams (full, medium, lores) simultaneously in hardware
// with zero CPU overhead.
//
// Stream mapping:
//   VSE Channel 0: Full-res pass-through (3840×2160 or 1920×1080)
//   VSE Channel 1: Medium-res for MJPEG streaming (640×480)
//   VSE Channel 3: Lores for BPU inference (320×320)
//
// Platform characteristics:
//   - SoC: D-Robotics X5 (Cortex-A55)
//   - AI: 10 TOPS BPU (Brain Processing Unit)
//   - Camera: V4L2 with HBN framework
//   - Scaling: VSE hardware (6 independent channels)
//   - No GPU scaling required — VSE is hardware-native
//   - No Rockchip AIQ, no RGA, no VPSS, no libcamera
//
// NOTE: This is a preparatory implementation. It has NOT been tested on
//       real hardware. TODO: Test and validate on D-Robotics RDK X5.
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
    // Represents a VSE channel configuration
    struct VseChannel {
        int  fd          = -1;       // VSE device file descriptor
        int  output_w    = 0;
        int  output_h    = 0;
        int  dma_fd      = -1;       // DMA-BUF for VSE output
        void* mapped     = nullptr;  // mmap'd buffer
        uint32_t size    = 0;
    };

    // V4L2 buffer slot (for camera capture)
    struct V4l2BufSlot {
        uint32_t index = 0;
        void*    data  = nullptr;
        uint32_t length = 0;
        int      dma_fd = -1;
        bool     queued = false;
    };

    // ── Private methods ───────────────────────────────────────────────────────
    bool init_v4l2();
    bool init_vse_channels();
    bool alloc_vse_buffer(VseChannel& ch, int width, int height);
    void free_vse_buffer(VseChannel& ch);

    // ── State ─────────────────────────────────────────────────────────────────
    PipelineConfig cfg_{};

    // V4L2 capture device
    int v4l2_fd_ = -1;
    std::vector<V4l2BufSlot> v4l2_buf_pool_;
    bool use_dmabuf_ = false;

    // VSE channels for the three output streams
    VseChannel vse_full_;    // Channel 0: full-res
    VseChannel vse_medium_;  // Channel 1: medium-res
    VseChannel vse_lores_;   // Channel 3: lores

    // Per-tick frame buffers (point to VSE output buffers)
    FrameBuffer frame_full_{};
    FrameBuffer frame_medium_{};
    FrameBuffer frame_lores_{};

    bool initialised_ = false;
};

} // namespace ct
