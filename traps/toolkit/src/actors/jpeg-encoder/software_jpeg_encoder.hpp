#pragma once

#include <vector>
#include <cstdint>

namespace ct {

// ─── SoftwareJpegEncoder ──────────────────────────────────────────────────────
// Pure-software NV12→JPEG encoder using libjpeg-turbo.
// Used as fallback when hardware MPP JPEG encoding is unavailable.
//
// Thread safety: Not thread-safe. Create one per thread or protect with mutex.
class SoftwareJpegEncoder {
public:
    SoftwareJpegEncoder() = default;
    ~SoftwareJpegEncoder() = default;

    // Initialize encoder with given dimensions and quality (1-100)
    bool init(int width, int height, int quality = 85);

    // Encode NV12 frame to JPEG
    std::vector<uint8_t> encode(const void* nv12_data, int width, int height, int stride);

    // Check if encoder is initialized
    bool is_initialized() const { return initialized_; }

private:
    int width_ = 0;
    int height_ = 0;
    int quality_ = 85;
    bool initialized_ = false;
};

} // namespace ct
