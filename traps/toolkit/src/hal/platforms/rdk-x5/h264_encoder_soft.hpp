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

// ─── SoftH264Encoder ──────────────────────────────────────────────────────────
// Software NV12 → H.264 encoding via FFmpeg libavcodec.
//
// Uses the built-in H.264 encoder from FFmpeg (libx264 or the built-in
// mpeg4/h264 encoder). Produces standalone IDR keyframes.
//
// This is the fallback for platforms without hardware H.264 encoding,
// such as the D-Robotics RDK X5.
class SoftH264Encoder : public H264Encoder {
public:
    SoftH264Encoder();
    ~SoftH264Encoder() noexcept override;

    SoftH264Encoder(const SoftH264Encoder&) = delete;
    SoftH264Encoder& operator=(const SoftH264Encoder&) = delete;

    int init(int width, int height, int qp = 26) override;

    std::vector<uint8_t> encode(int dma_fd, uint32_t size) override;

    void deinit();

private:
    void* codec_ctx_  = nullptr;   // AVCodecContext*
    void* frame_      = nullptr;   // AVFrame*
    void* pkt_        = nullptr;   // AVPacket*
    int   width_      = 0;
    int   height_     = 0;
    int64_t frame_count_ = 0;
};

} // namespace ct