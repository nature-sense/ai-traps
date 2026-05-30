#pragma once

#include "hal/api/classifier_hal.hpp"
#include <string>

namespace ct {

// ─── ClassifierHalNative ─────────────────────────────────────────────────────
// Native (host) stub classifier HAL.
// Returns a dummy classification (class_id=0, confidence=0).
// Used for testing the classification pipeline on the development machine.
class ClassifierHalNative : public IClassifierHAL {
public:
    ClassifierHalNative() = default;
    ~ClassifierHalNative() override = default;

    bool init(const std::string& model_path, float confidence_threshold) override;
    Classification classify(const uint8_t* rgb_data, int width, int height,
                             int64_t timestamp_ms) override;
    void shutdown() override;
    int64_t last_classification_us() const override { return last_classification_us_; }

private:
    bool initialized_ = false;
    int64_t last_classification_us_ = 0;
};

} // namespace ct
