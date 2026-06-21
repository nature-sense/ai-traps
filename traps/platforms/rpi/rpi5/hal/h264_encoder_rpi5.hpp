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

#include "hal/api/h264_encoder.hpp"

namespace ct {

// ─── Rpi5H264Encoder ─────────────────────────────────────────────────────────
// Hardware-accelerated NV12 → H.264 encoding via V4L2 M2M on Pi 5.
//
// Uses the VideoCore VII H.264 encoder exposed as /dev/video11 (V4L2 M2M).
// Produces standalone IDR keyframes.
//
// Pipeline:
//   dmabuf_fd → VIDIOC_QBUF(OUTPUT, dmabuf import) → hardware encode
//           → VIDIOC_DQBUF(CAPTURE) → dmabuf → mmap → vector<uint8_t>
//
// Zero-copy: source frame is imported as a dmabuf fd, no memcpy.
class Rpi5H264Encoder : public H264Encoder {
public:
    Rpi5H264Encoder();
    ~Rpi5H264Encoder() noexcept override;

    Rpi5H264Encoder(const Rpi5H264Encoder&) = delete;
    Rpi5H264Encoder& operator=(const Rpi5H264Encoder&) = delete;

    int init(int width, int height, int qp = 26) override;

    std::vector<uint8_t> encode(int dma_fd, uint32_t size) override;

    void deinit();

private:
    int m2m_fd_          = -1;   // V4L2 M2M device fd (/dev/video11)
    int width_           = 0;
    int height_          = 0;
    bool initialised_    = false;
};

} // namespace ct