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

// ─── H264Encoder ──────────────────────────────────────────────────────────────
// Abstract HAL interface for NV12 → H.264 encoding.
//
// Each platform provides its own implementation:
//   - rock3c: MppH264Encoder — hardware-accelerated via Rockchip MPP (VEPU)
//   - rdk-x5: SoftH264Encoder — software encoding via libavcodec
class H264Encoder {
public:
    virtual ~H264Encoder() = default;

    // Initialize the encoder for given frame dimensions and quality.
    // @param width  Frame width in pixels
    // @param height Frame height in pixels
    // @param qp     Quantization parameter (0-51, lower = better quality)
    // @return 0 on success, nonzero on failure
    virtual int init(int width, int height, int qp = 26) = 0;

    // Encode a single NV12 frame as an H.264 IDR keyframe.
    //
    // Zero-copy: the frame is passed as a DMA-BUF file descriptor.
    // The encoder imports the dmabuf (no memcpy) and produces
    // an H.264 Annex B byte stream.
    //
    // @param dma_fd  DMA-BUF file descriptor for the NV12 frame data
    // @param size    Total size of the frame data in bytes (for mmap)
    // @return Encoded H.264 Annex B byte stream, or empty vector on failure
    virtual std::vector<uint8_t> encode(int dma_fd, uint32_t size) = 0;
};

} // namespace ct