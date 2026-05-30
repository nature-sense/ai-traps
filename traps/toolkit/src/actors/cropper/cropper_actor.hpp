#pragma once

#include <ramen.hpp>
#include "hal/api/types.hpp"
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <cstdint>

namespace ct {

// ─── TrackCropState ───────────────────────────────────────────────────────────
// Tracks the best (highest-confidence) detection for a given track_id.
// The JPEG data is encoded immediately (while the DMA buffer is valid) and
// stored here. When a new track appears, the crop is emitted immediately.
// When confidence improves, an update crop is emitted (replaces the old one).
struct TrackCropState {
    float       best_confidence = 0.f;
    int64_t     best_timestamp  = 0;
    int         class_id        = 0;
    int         track_id        = 0;
    bool        has_emitted     = false; // true after first crop emission
    float       last_emitted_confidence = 0.f; // confidence at last emission (for detecting improvements)
    std::vector<uint8_t> jpeg_data{};  // encoded JPEG of the best crop
};

// ─── CropperActor ──────────────────────────────────────────────────────────────
// Receives frame_full and tracked_objects in the same pipeline tick.
//
// Detection reporting logic:
//   1. New track detected → emit crop immediately (first detection for that track)
//   2. Track still active  → do NOT emit any more crops for that track
//   3. Higher confidence   → emit an UPDATE crop (replaces the old image + metadata)
//
// This prevents saving hundreds of near-identical crops for a stationary
// insect while still capturing the best-quality image.
//
// Push ordering contract (enforced by Pipeline::wire() and CaptureNode::tick()):
//   in_frame_full fires FIRST  → latches the raw frame pointer
//   in_tracked    fires SECOND → encodes crops using the latched frame
//
// The DMA buffer pointed to by pending_frame_ is valid until CaptureNode calls
// release_frames() at the end of tick().  All encoding must complete before then.
// Therefore, JPEG encoding happens immediately when a new best is detected.
//
// Crop padding (cfg.crop_padding_px) adds context pixels around each bbox,
// clamped to frame bounds.
struct CropperActor {
    // ── Inputs ────────────────────────────────────────────────────────────────
    ramen::Pushable<FrameBuffer> in_frame_full = [this](const FrameBuffer& f) {

        pending_frame_ = f;
    };

    ramen::Pushable<std::vector<TrackedObject>> in_tracked =
        [this](const std::vector<TrackedObject>& tracked) {
            if (pending_frame_.data == nullptr) return;

            // ── Step 1: Update best detections for tracks seen this frame ─────
            // For each tracked object, if confidence is higher than the previous
            // best for that track_id, encode the crop immediately (while the DMA
            // buffer is still valid) and store the JPEG data.
            // Skip detections below min_confidence threshold.
            std::unordered_map<int, bool> seen_this_frame;
            for (const auto& obj : tracked) {
                seen_this_frame[obj.track_id] = true;

                // Skip low-confidence detections
                if (min_confidence > 0.f && obj.detection.confidence < min_confidence) {
                    continue;
                }

                auto& state = best_crops_[obj.track_id];
                if (!state.jpeg_data.empty() &&
                    obj.detection.confidence <= state.best_confidence) {
                    continue;  // Not better than what we already have
                }

                // New best — encode immediately while DMA buffer is valid
                auto crop = encode_crop(obj);
                if (!crop.data.empty()) {
                    state.best_confidence = obj.detection.confidence;
                    state.best_timestamp  = crop.timestamp_ms;
                    state.class_id        = obj.detection.class_id;
                    state.track_id        = obj.track_id;
                    state.jpeg_data       = std::move(crop.data);
                }
            }

            // ── Step 2: Emit crops ────────────────────────────────────────────
            // Emit crops for:
            //   a) Tracks that disappeared from frame (final best crop)
            //   b) New tracks (first detection)
            //   c) Active tracks with improved confidence (update)
            //
            // IMPORTANT: After emitting, we keep the jpeg_data in state so that
            // on the next frame Step 1 can compare confidence against the stored
            // best. If we moved jpeg_data out here, Step 1 would see an empty
            // buffer and re-encode on every frame.
            //
            // IMPORTANT: When a track disappears from frame, we emit the final
            // best crop but do NOT erase the state. This prevents duplicate
            // INSERTs if the same track_id reappears later (e.g. in scene loops
            // where objects leave and re-enter the frame). The state is only
            // cleaned up when reset() is called (on session start).
            std::vector<JpegCrop> crops;
            for (auto& [track_id, state] : best_crops_) {
                if (seen_this_frame.find(track_id) == seen_this_frame.end()) {
                    // Track disappeared — emit the best crop we stored
                    // but KEEP the state so reappearing tracks get updates.
                    // Only emit if confidence has improved since the last
                    // emission, to avoid duplicate crop_saved events when
                    // a track leaves and re-enters the frame.
                    if (!state.jpeg_data.empty() &&
                        state.best_confidence > state.last_emitted_confidence) {
                        JpegCrop crop;
                        crop.track_id     = state.track_id;
                        crop.class_id     = state.class_id;
                        crop.confidence   = state.best_confidence;
                        crop.timestamp_ms = state.best_timestamp;
                        crop.data         = state.jpeg_data;  // copy, not move
                        crop.is_update    = state.has_emitted;
                        crops.push_back(std::move(crop));
                        state.last_emitted_confidence = state.best_confidence;
                    }
                } else {
                    // Track still active
                    if (!state.jpeg_data.empty()) {
                        if (!state.has_emitted) {
                            // New track — emit first detection
                            JpegCrop crop;
                            crop.track_id     = state.track_id;
                            crop.class_id     = state.class_id;
                            crop.confidence   = state.best_confidence;
                            crop.timestamp_ms = state.best_timestamp;
                            crop.data         = state.jpeg_data;  // copy, not move
                            crop.is_update    = false;
                            crops.push_back(std::move(crop));
                            state.has_emitted = true;
                            state.last_emitted_confidence = state.best_confidence;
                        } else if (state.best_confidence > state.last_emitted_confidence) {
                            // Confidence improved — emit update crop
                            JpegCrop crop;
                            crop.track_id     = state.track_id;
                            crop.class_id     = state.class_id;
                            crop.confidence   = state.best_confidence;
                            crop.timestamp_ms = state.best_timestamp;
                            crop.data         = state.jpeg_data;  // copy, not move
                            crop.is_update    = true;
                            crops.push_back(std::move(crop));
                            state.last_emitted_confidence = state.best_confidence;
                        }
                        // else: confidence unchanged — skip emission
                    }
                }
            }

            if (!crops.empty()) {
                out_crops(std::move(crops));
            }
        };

    // ── Output ────────────────────────────────────────────────────────────────
    ramen::Pusher<std::vector<JpegCrop>> out_crops{};

    // ── Config ────────────────────────────────────────────────────────────────
    int padding_px = 16;
    float min_confidence = 0.0f;  // Skip detections below this confidence (0 = no filter)

    // Reset cropper state — clears all cached crop data and track states.
    // Called when a new session starts so that previously seen tracks are
    // treated as new detections (fresh track IDs, new first-emission).
    void reset() {
        best_crops_.clear();
    }

private:
    JpegCrop encode_crop(const TrackedObject& obj);
    // Extracts NV12 ROI from pending_frame_ (clamped + padded), then encodes
    // as JPEG using libjpeg-turbo (tjCompressFromYUVPlanes). Falls back to
    // empty data if libjpeg-turbo is not available.

    FrameBuffer pending_frame_{};

    std::unordered_map<int, TrackCropState> best_crops_{};
};

} // namespace ct
