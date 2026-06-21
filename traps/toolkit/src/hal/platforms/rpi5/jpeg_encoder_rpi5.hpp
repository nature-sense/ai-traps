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

#include "hal/api/jpeg_encoder.hpp"

#include <cstdint>

namespace ct {

// ─── Rpi5JpegEncoder ─────────────────────────────────────────────────────────
// Hardware-accelerated NV12 → JPEG encoding via V4L2 M2M on Pi 5.
//
// Uses the VideoCore VII JPEG encoder exposed as /dev/video31 (V4L2 M2M).
// Hardware crop rectangle is set via VIDIOC_S_SELECTION before encode.
//
// Zero-copy: source frame is imported as a dmabuf fd. The hardware crops
// and encodes the sub-region directly from the shared physical pages —
// no CPU memcpy, no ROI extraction.
struct Rpi5JpegEncoder : IJpegEncoder {
    Rpi5JpegEncoder();
    ~Rpi5JpegEncoder() override;

    Rpi5JpegEncoder(const Rpi5JpegEncoder&) = delete;
    Rpi5JpegEncoder& operator=(const Rpi5JpegEncoder&) = delete;

    std::vector<uint8_t> encode_crop(int src_dma_fd,
                                      uint32_t src_w,
                                      uint32_t src_h,
                                      uint32_t src_stride,
                                      uint32_t crop_x,
                                      uint32_t crop_y,
                                      uint32_t crop_w,
                                      uint32_t crop_h,
                                      int quality = 85) override;

private:
    int m2m_fd_      = -1;   // V4L2 M2M device fd (/dev/video31)
    bool initialised_ = false;

    bool ensure_initialised();
};

} // namespace ct