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

#include <ramen.hpp>
#include "hal/api/types.hpp"
#include <string>
#include <memory>

namespace ct {

// ─── CameraActor ──────────────────────────────────────────────────────────────
// Abstract base class for camera sensor actors in the Ramen pipeline.
// Concrete implementations include:
//   - CameraHalActor: wraps an ICameraHAL (platform-specific camera HAL)
//   - SceneCameraActor: PNG-based test scenes (platform-independent)
//
// The actor is selected at runtime via PipelineConfig::camera_model and
// created by CameraActor::create().
//
// Output ports (identical for all camera models):
//   out_frame_full   → CropperNode::in_frame_full
//   out_frame_medium → OverlayNode::in_frame
//   out_frame_lores  → InferNode::in_frame
//   out_mjpeg_frame  → HiresMjpegBridge::in_frame (UVC cameras only)
//
// Platform differences from RV1106:
//   - V4L2 device: /dev/video0 (RK3566/Armbian) vs /dev/video11 (RV1106/Luckfox)
//   - MB pool: DMA_HEAP allocator (RK3566) vs CMA (RV1106)
//   - AIQ: No pause/resume needed around NPU inference (RK3566 has separate
//     ISP and NPU DMA engines that don't conflict)
struct CameraActor {
    virtual ~CameraActor() = default;

    // ── Ramen output ports (wired by Pipeline::wire()) ────────────────────────
    ramen::Pusher<FrameBuffer> out_frame_full{};    // → CropperNode::in_frame_full
    ramen::Pusher<FrameBuffer> out_frame_medium{};  // → OverlayNode::in_frame
    ramen::Pusher<FrameBuffer> out_frame_lores{};   // → InferNode::in_frame

    // Raw MJPEG frame from USB camera (for hi-res streaming, UVC only)
    ramen::Pusher<std::vector<uint8_t>> out_mjpeg_frame{};

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    virtual bool init(const PipelineConfig& cfg) = 0;
    virtual void shutdown() = 0;

    // ── Called once per frame by Pipeline::run_loop() ─────────────────────────
    virtual void tick() = 0;

    // ── Accessors ─────────────────────────────────────────────────────────────
    virtual const FrameBuffer& last_lores() const = 0;

    virtual const PipelineConfig& cfg() const = 0;

    // Pause/resume AIQ ISP processing. Used by Pipeline to bracket RKNN inference
    // so AIQ ISP register writes don't interfere with NPU DMA on RV1106.
    // On RK3566 these are no-ops (separate DMA engines).
    virtual void aiq_pause() = 0;
    virtual void aiq_resume() = 0;

    // ── Factory ───────────────────────────────────────────────────────────────
    // Creates the appropriate CameraActor subclass based on cfg.camera_model.
    // Returns nullptr if the model is unknown.
    static std::unique_ptr<CameraActor> create(const std::string& camera_model);
};

} // namespace ct
