#include "inference_hal_native.hpp"
#include <iostream>

namespace ct {

bool InferenceHalNative::init(const std::string& model_path, float confidence_threshold) {
    std::cout << "[InferenceHalNative] init model=" << model_path
              << " conf=" << confidence_threshold << " (stub)\n";
    initialized_ = true;
    return true;
}

std::vector<Detection> InferenceHalNative::detect(const FrameBuffer& frame) {
    // Stub: return empty detections
    last_us_ = 1000; // pretend 1ms inference
    return {};
}

int64_t InferenceHalNative::last_inference_us() const {
    return last_us_;
}

uint32_t InferenceHalNative::input_width() const {
    return input_width_;
}

uint32_t InferenceHalNative::input_height() const {
    return input_height_;
}

void InferenceHalNative::shutdown() {
    std::cout << "[InferenceHalNative] shutdown (stub)\n";
    initialized_ = false;
}

} // namespace ct
