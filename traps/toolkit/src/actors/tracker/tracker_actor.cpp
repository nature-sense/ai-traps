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

#include "tracker_actor.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <cmath>

namespace ct {

void TrackerActor::init(int lores_w, int lores_h, int full_w, int full_h) {
    lores_w_ = lores_w;
    lores_h_ = lores_h;
    full_w_  = full_w;
    full_h_  = full_h;

    // Compute letterbox parameters.
    // The lores frame is lores_w × lores_h (e.g. 320×320).
    // The actual camera image (full_w × full_h, e.g. 1920×1080) is scaled to
    // fit within the lores frame while maintaining aspect ratio.
    //
    // Since the camera is typically 16:9 and the lores frame is square,
    // the image fits to width: scaled_w = lores_w, scaled_h = lores_w * full_h / full_w
    // with black bars (letterbox) top/bottom.
    const float src_aspect = static_cast<float>(full_w) / static_cast<float>(full_h);
    const float dst_aspect = static_cast<float>(lores_w) / static_cast<float>(lores_h);

    int scaled_w, scaled_h;
    if (src_aspect > dst_aspect) {
        // Source is wider — fit to width, letterbox top/bottom
        scaled_w = lores_w;
        scaled_h = static_cast<int>(lores_w / src_aspect);
    } else {
        // Source is taller — fit to height, pillarbox left/right
        scaled_h = lores_h;
        scaled_w = static_cast<int>(lores_h * src_aspect);
    }

    // Ensure even alignment for NV12 subsampling
    scaled_w &= ~1;
    scaled_h &= ~1;

    pad_top_  = (lores_h - scaled_h) / 2;
    pad_top_ &= ~1;  // Ensure even
    actual_h_ = scaled_h;

    // Scale factors from actual image area (within letterbox) to full-res
    scale_x_ = static_cast<float>(full_w) / static_cast<float>(scaled_w);
    scale_y_ = static_cast<float>(full_h) / static_cast<float>(scaled_h);

    std::cout << "[TrackerActor] init (match_mode=" << match_mode
              << ", lores=" << lores_w << "x" << lores_h
              << " full=" << full_w << "x" << full_h
              << " letterbox: pad_top=" << pad_top_
              << " actual_h=" << actual_h_
              << " scale=" << scale_x_ << "x" << scale_y_ << ")\n";
}

// IoU in normalised [0,1] detection space
float TrackerActor::iou(const Detection& a, const Detection& b) {
    const float ix  = std::max(a.x, b.x);
    const float iy  = std::max(a.y, b.y);
    const float ix2 = std::min(a.x + a.w, b.x + b.w);
    const float iy2 = std::min(a.y + a.h, b.y + b.h);
    if (ix2 <= ix || iy2 <= iy) return 0.f;
    const float inter = (ix2 - ix) * (iy2 - iy);
    const float uni   = a.w * a.h + b.w * b.h - inter;
    return (uni > 0.f) ? inter / uni : 0.f;
}

// Centroid distance in normalised [0,1] detection space.
// Returns Euclidean distance between the centers of two detections.
float TrackerActor::centroid_dist(const Detection& a, const Detection& b) {
    const float cx1 = a.x + a.w * 0.5f;
    const float cy1 = a.y + a.h * 0.5f;
    const float cx2 = b.x + b.w * 0.5f;
    const float cy2 = b.y + b.h * 0.5f;
    const float dx = cx1 - cx2;
    const float dy = cy1 - cy2;
    return std::sqrt(dx * dx + dy * dy);
}

void TrackerActor::lores_to_full(const Detection& det,
                                 int& out_x, int& out_y,
                                 int& out_w, int& out_h) const {
    // Step 1: Convert normalized [0,1] lores coords to letterboxed pixel coords
    float lx = det.x * static_cast<float>(lores_w_);
    float ly = det.y * static_cast<float>(lores_h_);
    float lw = det.w * static_cast<float>(lores_w_);
    float lh = det.h * static_cast<float>(lores_h_);

    // Step 2: Remove letterbox padding (top black bar)
    // The actual image starts at pad_top_ pixels from the top of the lores frame.
    float ax = lx;
    float ay = ly - static_cast<float>(pad_top_);
    float aw = lw;
    float ah = lh;

    // Clamp to actual image area (no negative coords, no exceeding actual bounds)
    if (ay < 0.f) {
        // Detection extends into top black bar — trim it
        ah += ay;  // Reduce height by the amount it extends into padding
        ay = 0.f;
    }
    if (ay + ah > static_cast<float>(actual_h_)) {
        ah = static_cast<float>(actual_h_) - ay;
    }

    // Step 3: Scale from actual image pixel coords to full-res pixel coords
    if (aw <= 0.f || ah <= 0.f) {
        // Detection is entirely in the letterbox area — invalid
        out_x = out_y = out_w = out_h = 0;
        return;
    }

    out_x = static_cast<int>(std::round(ax * scale_x_));
    out_y = static_cast<int>(std::round(ay * scale_y_));
    out_w = static_cast<int>(std::round(aw * scale_x_));
    out_h = static_cast<int>(std::round(ah * scale_y_));

    // Clamp to full-res bounds
    out_x = std::max(0, std::min(out_x, full_w_ - 1));
    out_y = std::max(0, std::min(out_y, full_h_ - 1));
    out_w = std::max(1, std::min(out_w, full_w_ - out_x));
    out_h = std::max(1, std::min(out_h, full_h_ - out_y));
}

std::vector<TrackedObject> TrackerActor::update(const std::vector<Detection>& dets) {
    // Greedy matching: for each detection find the best previous track.
    // Uses either IoU or centroid distance depending on match_mode.

    // ── Step 1: Age all existing tracks ───────────────────────────────────────
    // Increment age counter for all tracks. Tracks that are matched in this
    // frame will have their age reset to 0.
    for (auto& t : prev_tracks_) {
        t.age++;
    }

    // ── Step 2: Apply NMS to detections before tracking ───────────────────────
    // The YOLO model may produce multiple overlapping detections for the same
    // insect (from different anchor points). Without NMS here, the tracker would
    // create separate tracks for each overlapping detection in the same frame,
    // causing track fragmentation.
    //
    // We apply a strict NMS (IoU=0.2) to aggressively suppress duplicates.
    std::vector<Detection> nms_dets;
    {
        // Sort by confidence descending
        std::vector<Detection> sorted = dets;
        std::sort(sorted.begin(), sorted.end(),
                  [](const Detection& a, const Detection& b) {
                      return a.confidence > b.confidence;
                  });
        std::vector<bool> suppressed(sorted.size(), false);
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            if (suppressed[i]) continue;
            nms_dets.push_back(sorted[i]);
            for (std::size_t j = i + 1; j < sorted.size(); ++j) {
                if (!suppressed[j] && iou(sorted[i], sorted[j]) > 0.2f) {
                    suppressed[j] = true;
                }
            }
        }
    }

    std::vector<bool> prev_used(prev_tracks_.size(), false);
    std::vector<Track> next_tracks;
    next_tracks.reserve(nms_dets.size() + prev_tracks_.size());

    std::vector<TrackedObject> result;
    result.reserve(nms_dets.size());

    for (const auto& d : nms_dets) {
        // Filter out low-confidence detections — they should not start new tracks
        // or match to existing ones.
        if (min_confidence > 0.f && d.confidence < min_confidence) {
            continue;
        }

        // Filter out detections entirely in the letterbox padding area.
        // The actual image starts at pad_top_ pixels from the top of the lores frame.
        // In normalized coords, this is pad_top_ / lores_h_.
        const float pad_top_norm = static_cast<float>(pad_top_) / static_cast<float>(lores_h_);
        const float pad_bottom_norm = static_cast<float>(pad_top_ + actual_h_) / static_cast<float>(lores_h_);
        
        // Check if detection has ANY overlap with the actual image area.
        // We use the detection bottom edge (d.y + d.h) to check if it extends
        // into the actual image, and the top edge (d.y) to check if it extends
        // below the top padding. This allows detections of insects that are
        // partially off-screen at the top edge to still be tracked.
        const float det_bottom = d.y + d.h;
        const float det_top = d.y;
        
        // Skip only if the detection is entirely above the actual image
        // (completely in the top letterbox) or entirely below it.
        if (det_bottom <= pad_top_norm || det_top >= pad_bottom_norm) {
            continue;
        }
        
        int   best_idx = -1;
        float best_score;

        if (match_mode == "centroid") {
            // Centroid-based matching: find the closest track by center distance.
            // A detection matches a track if the distance is below centroid_threshold.
            best_score = centroid_threshold;
            for (std::size_t p = 0; p < prev_tracks_.size(); ++p) {
                if (prev_used[p]) continue;
                const float dist = TrackerActor::centroid_dist(d, prev_tracks_[p].det);
                if (dist < best_score) {
                    best_score = dist;
                    best_idx = (int)p;
                }
            }
        } else {
            // IoU-based matching: find the best overlapping track.
            best_score = iou_threshold;
            for (std::size_t p = 0; p < prev_tracks_.size(); ++p) {
                if (prev_used[p]) continue;
                const float score = iou(d, prev_tracks_[p].det);
                if (score > best_score) {
                    best_score = score;
                    best_idx = (int)p;
                }
            }
        }

        int tid;
        if (best_idx >= 0) {
            tid = prev_tracks_[best_idx].id;
            prev_used[best_idx] = true;
        } else {
            // Only start a new track if the detection confidence is high enough.
            // Low-confidence detections that don't match any existing track are
            // likely false positives — skip them to prevent track ID pollution.
            if (d.confidence < new_track_min_confidence) {
                continue;
            }
            tid = next_id_++;
        }

        // Determine detection count for this track.
        // If matched to an existing track, inherit its detection count + 1.
        // If new, start at 1.
        int det_count = 1;
        if (best_idx >= 0) {
            det_count = prev_tracks_[best_idx].detection_count + 1;
        }

        // Add matched/new track with age=0 (freshly detected)
        next_tracks.push_back({tid, d, 0, det_count});

        // Only emit tracks that have been detected enough times to be considered real.
        // This prevents brief, low-confidence detections from creating visible tracks.
        if (det_count >= min_detections_for_track) {
            TrackedObject t;
            t.track_id  = tid;
            t.detection = d;
            
            lores_to_full(d, t.full_x, t.full_y, t.full_w, t.full_h);
            
            std::cout << "[TrackerActor] track_id=" << tid
                      << " conf=" << d.confidence
                      << " det=" << d.x << "," << d.y << "," << d.w << "," << d.h
                      << " full=" << t.full_x << "," << t.full_y << ","
                      << t.full_w << "," << t.full_h << "\n";
            
            result.push_back(t);
        }
    }

    // ── Step 3: Carry over unmatched tracks that haven't aged out ─────────────
    // This prevents track ID churn when an insect is briefly missed (e.g.,
    // confidence dips below threshold for 1-2 frames).
    for (std::size_t p = 0; p < prev_tracks_.size(); ++p) {
        if (prev_used[p]) continue;
        if (prev_tracks_[p].age < track_hold_frames) {
            // Keep the track alive but don't emit it (no detection this frame)
            next_tracks.push_back(prev_tracks_[p]);
        }
    }

    prev_tracks_ = std::move(next_tracks);
    return result;
}

} // namespace ct
