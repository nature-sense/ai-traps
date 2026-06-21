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

// ─── IJpegEncoder ──────────────────────────────────────────────────────────────
// Abstract interface for NV12 → JPEG encoding.
//
// Each platform provides its own implementation:
//   - rock3c, cubie-a7s, host: LibJpegEncoder (software via libjpeg-turbo,
//     mmap'd dmabuf input)
//   - rpi5: Rpi5JpegEncoder (hardware via V4L2 M2M /dev/video31,
//     dmabuf import + HW crop rectangle)
//
// All inputs and outputs operate via DMA-BUF for zero-copy:
//   - src_dma_fd: source NV12 frame as a dmabuf file descriptor
//   - The encoder imports the dmabuf (no memcpy), encodes a rectangular crop,
//     and returns the JPEG byte stream (mmap'd from the output dmabuf)
struct IJpegEncoder {
    virtual ~IJpegEncoder() = default;

    // Encode a rectangular crop from an NV12 frame to JPEG.
    //
    // @param src_dma_fd  DMA-BUF fd of the source NV12 frame
    // @param src_w       Source frame width in pixels
    // @param src_h       Source frame height in pixels
    // @param src_stride  Source frame stride (bytes per row)
    // @param crop_x      Crop rectangle X origin
    // @param crop_y      Crop rectangle Y origin
    // @param crop_w      Crop rectangle width
    // @param crop_h      Crop rectangle height
    // @param quality     JPEG quality (1-100, default 85)
    // @return Encoded JPEG byte stream, or empty vector on failure
    virtual std::vector<uint8_t> encode_crop(int src_dma_fd,
                                              uint32_t src_w,
                                              uint32_t src_h,
                                              uint32_t src_stride,
                                              uint32_t crop_x,
                                              uint32_t crop_y,
                                              uint32_t crop_w,
                                              uint32_t crop_h,
                                              int quality = 85) = 0;
};

} // namespace ct