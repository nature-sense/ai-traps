#pragma once

#include <ramen.hpp>
#include "hal/api/types.hpp"
#include <vector>
#include <string>
#include <unordered_set>
#include <iostream>

namespace ct {

// ─── DecisionActor ────────────────────────────────────────────────────────────
// Applies trigger logic to detection results. Filters detections based on:
//   - Confidence threshold (minimum confidence to trigger)
//   - Class whitelist (only trigger for specific species/classes)
//   - Cooldown period (minimum time between triggers to avoid spam)
//
// Receives detections from TrackerNode (or directly from InferNode if no
// tracking is used) and emits a trigger signal when conditions are met.
//
// Wiring options:
//   tracker.out_tracked >> decision.in_tracked   (with tracking)
//   infer.out_detections >> decision.in_detections (without tracking)
//
// The decision actor can be configured to:
//   - Only trigger for specific class IDs (e.g., only "deer", not "squirrel")
//   - Enforce a cooldown between triggers (e.g., 5 seconds minimum)
//   - Require minimum confidence (overrides the global threshold)
struct DecisionActor {
    // ── Inputs ────────────────────────────────────────────────────────────────
    // Accept tracked objects (from TrackerNode)
    ramen::Pushable<std::vector<TrackedObject>> in_tracked =
        [this](const std::vector<TrackedObject>& tracked) {
            evaluate_tracked(tracked);
        };

    // Accept raw detections (from InferNode, no tracking)
    ramen::Pushable<std::vector<Detection>> in_detections =
        [this](const std::vector<Detection>& dets) {
            evaluate_detections(dets);
        };

    // ── Outputs ───────────────────────────────────────────────────────────────
    // Emitted when trigger conditions are met
    ramen::Pusher<bool> out_trigger{};

    // ── Config ────────────────────────────────────────────────────────────────
    // Minimum confidence to trigger (overrides global threshold if > 0)
    float trigger_confidence = 0.0f;

    // Cooldown between triggers in milliseconds (0 = no cooldown)
    int64_t cooldown_ms = 5000;

    // Class whitelist: only trigger for these class IDs.
    // Empty = trigger for all classes.
    std::unordered_set<int> class_whitelist{};

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    void init() {
        last_trigger_ms_ = 0;
        std::cout << "[DecisionActor] ready (conf=" << trigger_confidence
                  << ", cooldown=" << cooldown_ms << "ms"
                  << ", classes=" << (class_whitelist.empty() ? "all" : "filtered")
                  << ")\n";
    }

private:
    void evaluate_tracked(const std::vector<TrackedObject>& tracked);
    void evaluate_detections(const std::vector<Detection>& dets);
    bool should_trigger(float confidence, int class_id);

    int64_t last_trigger_ms_ = 0;
};

} // namespace ct
