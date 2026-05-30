#pragma once

#include "camera_actor.hpp"
#include "hal/api/camera_hal.hpp"

#include <memory>
#include <vector>
#include <cstdint>

namespace ct {

// ─── CameraHalActor ────────────────────────────────────────────────────────────
// Ramen actor that wraps an ICameraHAL implementation, providing the standard
// CameraActor Ramen port interface (out_frame_full, out_frame_medium,
// out_frame_lores, out_mjpeg_frame).
//
// This replaces the old Imx219Actor, Imx415Actor, and UvcCameraActor classes.
// The platform-specific camera logic now lives in ICameraHAL implementations
// under platform/<platform>/.
//
// The actor is selected at runtime via PipelineConfig::camera_model and
// created by CameraActor::create().
struct CameraHalActor : CameraActor {
    explicit CameraHalActor(std::unique_ptr<ICameraHAL> hal);

    bool init(const PipelineConfig& cfg) override;
    void shutdown() override;
    void tick() override;

    const FrameBuffer& last_lores() const override { return frame_lores_; }

    const PipelineConfig& cfg() const override { return cfg_; }

    void aiq_pause() override {}   // No-op on RK3566 (separate ISP/NPU DMA engines)
    void aiq_resume() override {}  // No-op on RK3566

private:
    std::unique_ptr<ICameraHAL> hal_;
    PipelineConfig cfg_{};

    // Per-tick frame storage (valid between tick() and the next tick())
    FrameBuffer frame_full_{};
    FrameBuffer frame_medium_{};
    FrameBuffer frame_lores_{};


    // Raw MJPEG buffer for hi-res streaming (UVC cameras only)
    std::vector<uint8_t> mjpeg_buffer_;
};

} // namespace ct
