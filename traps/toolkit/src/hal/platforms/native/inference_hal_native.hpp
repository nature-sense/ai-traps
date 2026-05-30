#pragma once

#include "hal/api/inference_hal.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace ct {

// ─── InferenceHalNative ──────────────────────────────────────────────────────
// Stub inference HAL for host (macOS/Linux) development.
// Returns empty detections for testing the pipeline without real hardware.
struct InferenceHalNative : IInferenceHAL {
    bool init(const std::string& model_path, float confidence_threshold) override;
    std::vector<Detection> detect(const FrameBuffer& frame) override;
    int64_t last_inference_us() const override;
    uint32_t input_width() const override;
    uint32_t input_height() const override;
    void shutdown() override;

private:
    bool     initialized_ = false;
    uint32_t input_width_  = 320;
    uint32_t input_height_ = 320;
    int64_t  last_us_ = 0;
};

} // namespace ct
