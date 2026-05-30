#pragma once

#include "hal/api/camera_hal.hpp"
#include "hal/api/frame_buffer.hpp"
#include <cstdint>

namespace ct {

// ─── CameraHalNative ─────────────────────────────────────────────────────────
// Stub camera HAL for host (macOS/Linux) development.
// Returns synthetic frames for testing the pipeline without real hardware.
struct CameraHalNative : ICameraHAL {
    bool init(const PipelineConfig& cfg) override;
    bool acquire_frames(FrameBuffer& full, FrameBuffer& medium,
                        FrameBuffer& lores) override;
    void release_frames() override;
    void shutdown() override;

    // Config
    int width_  = 640;
    int height_ = 480;
    int fps_    = 30;
};

} // namespace ct
