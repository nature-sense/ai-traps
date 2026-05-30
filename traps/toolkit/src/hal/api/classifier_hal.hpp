#pragma once

#include "hal/api/types.hpp"
#include <memory>
#include <string>
#include <cstdint>

namespace ct {

// ─── IClassifierHAL ────────────────────────────────────────────────────────────
// Abstract interface for insect classification hardware abstraction.
// Separate from IInferenceHAL because the input is different — it takes a
// cropped, resized RGB image rather than a full NV12 frame.
//
// The classifier is responsible for:
//   - Loading the classifier model (e.g. 224×224 RGB input)
//   - Running classification on pre-processed RGB data
//   - Returning a Classification result with class_id, confidence, timestamp
//   - Shutting down cleanly
//
struct IClassifierHAL {
    virtual ~IClassifierHAL() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    // Load classifier model from path, allocate tensor memory, prepare for inference.
    // Returns false if the model cannot be loaded or the backend is unavailable.
    virtual bool init(const std::string& model_path, float confidence_threshold) = 0;

    // ── Classification ─────────────────────────────────────────────────────────
    // Classify a pre-processed RGB image.
    // The input is a contiguous RGB buffer of input_w × input_h × 3 bytes.
    // Returns the classification result with class_id, confidence, timestamp.
    virtual Classification classify(const uint8_t* rgb_data, int width, int height, int64_t timestamp_ms) = 0;

    // ── Query ──────────────────────────────────────────────────────────────────
    // Returns the classification time for the last classify() call in microseconds.
    virtual int64_t last_classification_us() const = 0;

    // Model input dimensions (the size the model expects, e.g. 224×224).
    virtual uint32_t input_width() const = 0;
    virtual uint32_t input_height() const = 0;

    // ── Teardown ───────────────────────────────────────────────────────────────
    // Release all resources (model context, tensor memory, etc.).
    virtual void shutdown() = 0;
};

} // namespace ct
