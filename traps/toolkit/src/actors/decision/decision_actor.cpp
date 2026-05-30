#include "decision_actor.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>

namespace ct {

bool DecisionActor::should_trigger(float confidence, int class_id) {
    // Check confidence threshold
    const float min_conf = (trigger_confidence > 0.0f)
                           ? trigger_confidence
                           : 0.0f;
    if (confidence < min_conf) return false;

    // Check class whitelist
    if (!class_whitelist.empty() &&
        class_whitelist.find(class_id) == class_whitelist.end()) {
        return false;
    }

    // Check cooldown
    if (cooldown_ms > 0) {
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now - last_trigger_ms_ < cooldown_ms) {
            return false;  // Still in cooldown
        }
        last_trigger_ms_ = now;
    }

    return true;
}

void DecisionActor::evaluate_tracked(const std::vector<TrackedObject>& tracked) {
    if (tracked.empty()) return;

    for (const auto& obj : tracked) {
        if (should_trigger(obj.detection.confidence, obj.detection.class_id)) {
            std::cout << "[DecisionActor] TRIGGER — track_id=" << obj.track_id
                      << " class=" << obj.detection.class_id
                      << " conf=" << obj.detection.confidence << "\n";
            out_trigger(true);
            return;  // One trigger per tick
        }
    }
}

void DecisionActor::evaluate_detections(const std::vector<Detection>& dets) {
    if (dets.empty()) return;

    for (const auto& det : dets) {
        if (should_trigger(det.confidence, det.class_id)) {
            std::cout << "[DecisionActor] TRIGGER — class=" << det.class_id
                      << " conf=" << det.confidence << "\n";
            out_trigger(true);
            return;  // One trigger per tick
        }
    }
}

} // namespace ct
