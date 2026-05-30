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

#include "hal/api/types.hpp"
#include <memory>
#include <string>
#include <vector>

namespace ct {

// ─── IInferenceHAL ─────────────────────────────────────────────────────────────
// Abstract interface for NPU inference hardware abstraction.
// Each platform (rock3c, etc.) provides a concrete implementation that
// encapsulates the platform-specific inference backend (RKNN, BPU, TFLite).
//
// The HAL is responsible for:
//   - Loading the model and allocating tensor memory
//   - Running inference on NV12 frames
//   - Decoding raw model outputs into Detection structs
//   - Shutting down cleanly
//
struct IInferenceHAL {
    virtual ~IInferenceHAL() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    // Load model from path, allocate tensor memory, prepare for inference.
    // Returns false if the model cannot be loaded or the backend is unavailable.
    virtual bool init(const std::string& model_path, float confidence_threshold) = 0;

    // ── Inference ─────────────────────────────────────────────────────────────
    // Run inference on the given NV12 frame and return detections.
    // The frame format is NV12 (as produced by VPSS lores channel).
    // Detection coordinates are normalised [0, 1] in the input frame space.
    // Returns empty vector if no objects detected or on error.
    virtual std::vector<Detection> detect(const FrameBuffer& frame) = 0;

    // ── Query ─────────────────────────────────────────────────────────────────
    // Returns the inference time for the last detect() call in microseconds.
    virtual int64_t last_inference_us() const = 0;

    // Model input dimensions (the size the model expects).
    virtual uint32_t input_width() const = 0;
    virtual uint32_t input_height() const = 0;

    // ── Teardown ──────────────────────────────────────────────────────────────
    // Release all resources (model context, tensor memory, etc.).
    virtual void shutdown() = 0;

};

} // namespace ct
