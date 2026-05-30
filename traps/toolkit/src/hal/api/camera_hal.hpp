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

#include "hal/api/types.hpp"
#include <memory>
#include <string>

namespace ct {

// ─── ICameraHAL ───────────────────────────────────────────────────────────────
// Abstract interface for camera hardware abstraction.
// Each platform (rock3c, etc.) provides a concrete implementation that
// encapsulates the platform-specific camera capture pipeline.
//
// The HAL is responsible for:
//   - Initialising the camera sensor (ISP, AIQ, V4L2, etc.)
//   - Acquiring frames at the configured resolution(s)
//   - Releasing frame buffers back to the pool
//   - Shutting down cleanly
//
// Frame acquisition produces three scaled outputs:
//   full   → CropperNode (for JPEG crop extraction)
//   medium → OverlayNode (for MJPEG preview stream)
//   lores  → InferNode   (for YOLO inference)
struct ICameraHAL {
    virtual ~ICameraHAL() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    // Initialise the camera hardware. Returns false on failure.
    // The PipelineConfig provides resolution, fps, device path, IQ dir, etc.
    virtual bool init(const PipelineConfig& cfg) = 0;

    // ── Frame capture ─────────────────────────────────────────────────────────
    // Acquire one frame from the camera, producing three scaled outputs.
    // Called once per pipeline tick. Returns true if frames were acquired.
    // The FrameBuffer pointers are valid only until release_frames() is called.
    virtual bool acquire_frames(FrameBuffer& full, FrameBuffer& medium,
                                FrameBuffer& lores) = 0;


    // Release all frame buffers acquired in the last acquire_frames() call.
    // Must be called after downstream nodes have consumed the frames.
    virtual void release_frames() = 0;

    // ── Teardown ──────────────────────────────────────────────────────────────
    virtual void shutdown() = 0;
};

} // namespace ct
