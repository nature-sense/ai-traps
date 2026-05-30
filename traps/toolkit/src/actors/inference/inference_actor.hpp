#pragma once

#include <ramen.hpp>
#include "hal/api/types.hpp"
#include "hal/api/inference_hal.hpp"
#include <string>
#include <vector>
#include <memory>

namespace ct {

// ─── InferenceActor ────────────────────────────────────────────────────────────
// Ramen actor that wraps an IInferenceHAL implementation, providing the standard
// inference Ramen port interface (in_frame, out_detections).
//
// This replaces the old InferNode struct. The platform-specific inference logic
// now lives in IInferenceHAL implementations under hal/platforms/<platform>/.
//
// The HAL is injected at construction time, selected at runtime via
// PipelineConfig::inference_backend and created by IInferenceHAL::create().
//
// Execution model — synchronous:
//   hal_->detect() runs inference and returns decoded detections.
//   This adds ~30–60ms per tick on RV1106. At 15fps (67ms budget) this is
//   acceptable.
struct InferenceActor {
    explicit InferenceActor(std::unique_ptr<IInferenceHAL> hal);

    // ── Input ─────────────────────────────────────────────────────────────────
    ramen::Pushable<FrameBuffer> in_frame = [this](const FrameBuffer& frame) {
        if (!enabled_ || !hal_ || frame.data == nullptr) return;
        auto dets = hal_->detect(frame);
        if (!dets.empty()) {
            out_detections(std::move(dets));
        }
    };

    // ── Output ────────────────────────────────────────────────────────────────
    ramen::Pusher<std::vector<Detection>> out_detections{};

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    bool init(const std::string& model_path, float confidence_threshold);
    void shutdown();

    // ── Runtime query (useful for logging / debug) ────────────────────────────
    int64_t last_inference_us() const;

    // ── Inference gating ──────────────────────────────────────────────────────
    // When disabled, in_frame callback returns immediately without running
    // inference. This saves NPU cycles when no session is active.
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool is_enabled() const { return enabled_; }

private:
    std::unique_ptr<IInferenceHAL> hal_;
    bool enabled_ = true;  // default: enabled (backward compatible)
};

} // namespace ct
