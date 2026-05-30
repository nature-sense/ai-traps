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
