#include "classifier_hal_native.hpp"
#include <iostream>
#include <chrono>

namespace ct {

bool ClassifierHalNative::init(const std::string& model_path, float confidence_threshold) {
    std::cout << "[ClassifierHalNative] init (model=" << model_path
              << ", threshold=" << confidence_threshold << ")\n";
    initialized_ = true;
    return true;
}

Classification ClassifierHalNative::classify(const uint8_t* rgb_data, int width, int height,
                                               int64_t timestamp_ms) {
    if (!initialized_) return {};

    auto start = std::chrono::high_resolution_clock::now();

    // Return a dummy classification
    Classification result;
    result.class_id = 0;
    result.confidence = 0.0f;
    result.timestamp_ms = timestamp_ms;

    auto end = std::chrono::high_resolution_clock::now();
    last_classification_us_ = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    return result;
}

void ClassifierHalNative::shutdown() {
    initialized_ = false;
    std::cout << "[ClassifierHalNative] shutdown\n";
}

} // namespace ct
