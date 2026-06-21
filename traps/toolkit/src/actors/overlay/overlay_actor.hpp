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
#include <vector>
#include <cstdint>

namespace ct {

// ─── OverlayActor ──────────────────────────────────────────────────────────────
// Receives the medium-res NV12 frame and tracked objects, draws bounding boxes
// and labels directly onto the NV12 data, then pushes the modified frame onward
// to MjpegBridge for JPEG encoding and HTTP streaming.
//
// Drawing is done in-place on the VpssFrame data buffer (which is valid for the
// duration of the pipeline tick).  No extra allocations.
//
// Colour format: NV12 (Y plane + interleaved UV plane).
// Bbox coords arrive in full-res pixel space; we scale them down to medium-res.
struct OverlayActor {
    // ── Inputs ────────────────────────────────────────────────────────────────
    // in_frame receives the medium-res NV12 frame and stores it as pending.
    // The frame is NOT pushed immediately — we wait for tracked objects to
    // arrive so overlays can be drawn before the frame reaches the MJPEG bridge.
    // flush() must be called at the end of each tick to push any pending frame
    // that didn't receive tracked objects.
    ramen::Pushable<FrameBuffer> in_frame = [this](const FrameBuffer& f) {

        pending_frame_ = f;
        pending_has_frame_ = true;
    };

    // in_tracked receives tracked objects, draws overlays on the pending frame,
    // then pushes the modified frame to the MJPEG bridge.
    ramen::Pushable<std::vector<TrackedObject>> in_tracked =
        [this](const std::vector<TrackedObject>& tracked) {
            if (tracked.empty() || !pending_has_frame_ || pending_frame_.data == nullptr) {
                return;
            }
            draw_overlays(pending_frame_, tracked);
            out_frame(pending_frame_);
            pending_has_frame_ = false;
        };

    // ── Flush ─────────────────────────────────────────────────────────────────
    // Must be called at the end of each pipeline tick to push any pending frame
    // that didn't receive tracked objects (i.e. no detections this frame).
    void flush() {
        if (pending_has_frame_ && pending_frame_.data) {
            out_frame(pending_frame_);
            pending_has_frame_ = false;
        }
    }

    // ── Clear ─────────────────────────────────────────────────────────────────
    // Called when the session is stopped. Discards any pending frame so that
    // no stale bounding boxes (drawn in a previous tick) are flushed to the
    // MJPEG stream. The next camera tick will push a clean frame.
    void clear_pending() {
        pending_has_frame_ = false;
    }

    // ── Clear and push clean ───────────────────────────────────────────────────
    // Called when the session is stopped. Pushes the pending frame WITHOUT
    // drawing any overlays, so the MJPEG stream immediately shows a clean frame.
    // If there's no pending frame, does nothing.
    void clear_and_push_clean() {
        if (pending_has_frame_ && pending_frame_.data) {
            out_frame(pending_frame_);
        }
        pending_has_frame_ = false;
    }

    // ── Output ────────────────────────────────────────────────────────────────
    ramen::Pusher<FrameBuffer> out_frame{};


    // ── Config ────────────────────────────────────────────────────────────────
    // Full-res dimensions (source of bbox coords)
    int full_w = 3840;
    int full_h = 2160;

private:
    void draw_overlays(FrameBuffer& frame, const std::vector<TrackedObject>& tracked);


    // Pixel-level drawing core (NV12)
    void draw_rect(uint8_t* y_plane, uint8_t* uv_plane,
                   int stride, int w, int h,
                   int rx, int ry, int rw, int rh,
                   uint8_t r, uint8_t g, uint8_t b);
    void draw_text(uint8_t* y_plane, int stride, int w, int h,
                   int x, int y, const char* text, uint8_t brightness = 200);

    bool pending_has_frame_ = false;
    FrameBuffer pending_frame_{};

};

} // namespace ct
