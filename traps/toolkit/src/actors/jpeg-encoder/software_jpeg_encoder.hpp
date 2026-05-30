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
