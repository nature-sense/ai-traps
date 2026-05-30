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
#include <chrono>


namespace ct {

// ─── TrackerActor ──────────────────────────────────────────────────────────────
// Wraps ByteTracker multi-object tracking.  Receives raw detections from
// InferNode, maintains track state across frames, and emits TrackedObjects
// with stable track_ids and bboxes scaled to full-res coordinates.
//
// This is the only stateful node in the pipeline (Kalman filter state, lost
// track buffer, track ID counter).
//
// ─── Letterbox-aware coordinate conversion ────────────────────────────────────
// The lores frame (320×320) uses letterboxing: the actual 16:9 camera image
// (320×180) is centered in a 320×320 buffer with 70px black bars top/bottom.
// YOLO detects objects in this letterboxed 320×320 space, so we must:
//   1. Convert from letterboxed normalized coords → letterboxed pixel coords
//   2. Remove the top padding (70px) to get actual image pixel coords
//   3. Scale from actual image size (320×180) to full-res (1920×1080)
//
// The letterbox parameters are computed from the lores and full-res dimensions:
//   - scaled_h = lores_w * (full_h / full_w)  [maintains aspect ratio]
//   - pad_top  = (lores_h - scaled_h) / 2
struct TrackerActor {
    // ── Input ─────────────────────────────────────────────────────────────────
    ramen::Pushable<std::vector<Detection>> in_detections =
        [this](const std::vector<Detection>& dets) {
            auto t0 = std::chrono::steady_clock::now();
            auto tracked = update(dets);
            last_tracking_us_ = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t0).count();
            if (!tracked.empty()) {
                out_tracked(std::move(tracked));
            }
        };


    // ── Output ────────────────────────────────────────────────────────────────
    ramen::Pusher<std::vector<TrackedObject>> out_tracked{};

    // ── Config ────────────────────────────────────────────────────────────────
    // Minimum confidence to track. Detections below this threshold are discarded
    // entirely — they won't start new tracks or match to existing ones.
    float min_confidence = 0.0f;  // 0 = no filter (use InferNode's threshold)

    // Minimum confidence for starting a NEW track.
    // Detections below this threshold can still match to EXISTING tracks (to keep
    // them alive), but they won't create new track IDs. This prevents false
    // positive detections from getting their own track IDs.
    // Default 0.6 — only high-confidence detections can start new tracks.
    // The real insects have confidence 0.82-0.87, while false positives at the
    // top edge (model noise) have confidence 0.45-0.52. 0.6 cleanly separates them.
    float new_track_min_confidence = 0.6f;


    // Minimum number of detections before a track is emitted as a result.
    // Tracks with fewer than this many detections are kept alive internally
    // (for matching purposes) but not reported to downstream nodes. This
    // prevents brief, low-confidence detections from creating visible tracks.
    // Default 3 — a track must be detected in at least 3 frames to be real.
    int min_detections_for_track = 3;

    // Matching mode: "iou" or "centroid"
    // "iou"      — match by bounding box overlap (good for slow-moving objects)
    // "centroid" — match by center-point distance (good for fast-moving objects)
    // Default "centroid" for insects that may move significantly between frames.
    std::string match_mode = "centroid";

    // IoU threshold for matching detections to existing tracks (match_mode="iou").
    // Lower values are more generous for fast-moving objects.
    // Default 0.1 is generous for insects that may move significantly between
    // frames when inference is slower than the camera frame rate.
    float iou_threshold = 0.1f;

    // Centroid distance threshold (normalized [0,1]) for matching detections
    // to existing tracks (match_mode="centroid").
    // A detection matches a track if the Euclidean distance between their
    // centers is below this threshold.
    // Default 0.3 — at 320x320 inference, this is ~96px, generous for insects
    // that may move significantly between frames or when the scene loops.
    float centroid_threshold = 0.3f;

    // Number of frames to keep a track alive after its last detection.
    // This prevents track ID churn when an insect is briefly missed (e.g.,
    // confidence dips below threshold for 1-2 frames, or insect moves out
    // of frame and back in).
    // Default 30 frames at 15fps = ~2 seconds of missed detections tolerated.
    // This is generous enough to handle insects that briefly leave the frame
    // (e.g., at the top edge where they're partially off-screen) and return.
    int track_hold_frames = 30;


    // ── Lifecycle ─────────────────────────────────────────────────────────────
    // lores_w/h: inference frame size (320×320, the letterboxed size)
    // full_w/h:  full-res frame size (for bbox coordinate scaling)
    void init(int lores_w, int lores_h, int full_w, int full_h);

    // Reset tracker state — clears all tracks and resets the track ID counter.
    // Called when a new session starts so track IDs begin from 1 again.
    void reset() {
        next_id_ = 1;
        prev_tracks_.clear();
    }

    // ── Runtime query ─────────────────────────────────────────────────────────
    // Returns the tracking time for the last frame in microseconds.
    int64_t last_tracking_us() const { return last_tracking_us_; }

private:

    std::vector<TrackedObject> update(const std::vector<Detection>& dets);
    // Implements greedy IoU matching with 0.3 threshold.
    // New detections get a fresh track_id; matched ones inherit the previous id.
    // Bboxes are scaled from lores normalised coords to full-res pixel coords,
    // accounting for the letterbox padding in the lores frame.

    // Convert a detection from letterboxed lores normalized coords to full-res
    // pixel coords. Returns {x, y, w, h} in full-res pixels.
    void lores_to_full(const Detection& det,
                       int& out_x, int& out_y, int& out_w, int& out_h) const;

    static float iou(const Detection& a, const Detection& b);
    static float centroid_dist(const Detection& a, const Detection& b);

    int lores_w_ = 320, lores_h_ = 320;
    int full_w_  = 0,   full_h_  = 0;

    // Letterbox parameters (computed in init())
    int pad_top_    = 0;   // pixels of black bar at top of lores frame
    int actual_h_   = 0;   // actual image height within lores frame (after removing letterbox)
    float scale_x_  = 1.f; // lores-actual → full-res scale factor (X)
    float scale_y_  = 1.f; // lores-actual → full-res scale factor (Y)

    int next_id_ = 1;

    // Tracking time for the last frame (microseconds)
    int64_t last_tracking_us_ = 0;

    // Tracks alive in the previous frame — used for IoU-based ID assignment.

    // All coords are normalised [0,1] in lores space.
    struct Track {
        int       id;
        Detection det;
        int       age = 0;  // frames since last matched detection
        int       detection_count = 0;  // total detections matched to this track
    };
    std::vector<Track> prev_tracks_;
};

} // namespace ct
