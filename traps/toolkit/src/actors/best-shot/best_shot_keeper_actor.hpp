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
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace ct {

// ─── BestShotKeeperActor ───────────────────────────────────────────────────────────
// Per-track best-confidence tracking for the classification pipeline.
//
// Receives full-res frames and tracked objects. For each track_id, retains the
// highest-confidence detection's frame + bbox. When a track disappears (not seen
// for track_hold_frames), emits the best shot as a BestShot struct.
//
// This replaces the CropperActor's "best crop" logic in the classification
// pipeline. It does NOT encode JPEG — it just holds frame buffer references.
//
// Wiring:
//   camera.out_frame_full >> best_shot_keeper.in_frame_full
//   tracker.out_tracked   >> best_shot_keeper.in_tracked
//   best_shot_keeper.out_best >> classifier.in_best_shot
//
struct BestShotKeeperActor {
    // ── Inputs ────────────────────────────────────────────────────────────────
    ramen::Pushable<FrameBuffer> in_frame_full =
        [this](const FrameBuffer& frame) {
            latest_frame_ = frame;
        };

    ramen::Pushable<std::vector<TrackedObject>> in_tracked =
        [this](const std::vector<TrackedObject>& tracked) {
            process_tracked(tracked);
        };

    // ── Outputs ───────────────────────────────────────────────────────────────
    ramen::Pusher<BestShot> out_best{};

    // ── Config ────────────────────────────────────────────────────────────────
    // Number of frames a track can be absent before we consider it closed.
    int track_hold_frames = 5;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    void init() {
        latest_frame_ = {};
        best_per_track_.clear();
        absent_frames_.clear();
    }

    // PipelineConfig-based init (no-op — config not needed for best shot keeper).
    bool init(const PipelineConfig& cfg) {
        (void)cfg;
        init();
        return true;
    }

    void reset() {
        // Emit any pending best shots before clearing
        for (auto& [track_id, best] : best_per_track_) {
            if (best.confidence > 0.f) {
                out_best.push(best);
            }
        }
        best_per_track_.clear();
        absent_frames_.clear();
        latest_frame_ = {};
    }

private:
    void process_tracked(const std::vector<TrackedObject>& tracked) {
        // Mark all currently tracked IDs as seen this frame
        std::unordered_set<int> seen_this_frame;

        for (const auto& obj : tracked) {
            seen_this_frame.insert(obj.track_id);

            // Update best shot for this track
            auto it = best_per_track_.find(obj.track_id);
            if (it == best_per_track_.end() || obj.detection.confidence > it->second.confidence) {
                BestShot shot;
                shot.track_id = obj.track_id;
                shot.class_id = obj.detection.class_id;
                shot.confidence = obj.detection.confidence;
                shot.timestamp_ms = obj.detection.x * 1000; // approximate — real timestamp from frame
                shot.bbox_x = obj.full_x;
                shot.bbox_y = obj.full_y;
                shot.bbox_w = obj.full_w;
                shot.bbox_h = obj.full_h;
                shot.frame = latest_frame_;
                best_per_track_[obj.track_id] = shot;
            }

            // Reset absent counter for this track
            absent_frames_[obj.track_id] = 0;
        }

        // Increment absent counters for tracks not seen this frame
        for (auto& [track_id, absent] : absent_frames_) {
            if (seen_this_frame.find(track_id) == seen_this_frame.end()) {
                absent++;
            }
        }

        // Emit best shots for tracks that have exceeded the hold threshold
        std::vector<int> closed_tracks;
        for (auto& [track_id, absent] : absent_frames_) {
            if (absent >= track_hold_frames) {
                auto it = best_per_track_.find(track_id);
                if (it != best_per_track_.end() && it->second.confidence > 0.f) {
                    out_best.push(it->second);
                }
                closed_tracks.push_back(track_id);
            }
        }

        // Clean up closed tracks
        for (int tid : closed_tracks) {
            best_per_track_.erase(tid);
            absent_frames_.erase(tid);
        }
    }

    FrameBuffer latest_frame_;

    // Per-track state
    std::unordered_map<int, BestShot> best_per_track_;
    std::unordered_map<int, int>      absent_frames_;  // frames since track last seen
};

} // namespace ct
