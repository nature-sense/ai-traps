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

// ─── SpH264Encoder ────────────────────────────────────────────────────────────
// Hardware NV12 → H.264 encoding via the D-Robotics SP media codec API.
//
// Uses the HBN hardware codec (hobot_codec_vnode) exposed through the
// SP user-space API in libspcdev.so. Loaded at runtime via dlopen.
//
// API from sp_codec.h:
//   sp_init_encoder_module()
//   sp_start_encode(obj, chn, SP_ENCODER_H264, width, height, bitrate)
//   sp_encoder_set_frame(obj, nv12_buffer, size)
//   sp_encoder_get_stream(obj, h264_buffer)
//   sp_stop_encode(obj)
//   sp_release_encoder_module(obj)
class SpH264Encoder : public H264Encoder {
public:
    SpH264Encoder();
    ~SpH264Encoder() noexcept override;

    SpH264Encoder(const SpH264Encoder&) = delete;
    SpH264Encoder& operator=(const SpH264Encoder&) = delete;

    int init(int width, int height, int qp = 26) override;

    std::vector<uint8_t> encode(int dma_fd, uint32_t size) override;

    void deinit();

private:
    void* module_ = nullptr;  // Opaque SP encoder module handle
    int width_  = 0;
    int height_ = 0;
    bool initialised_ = false;
};

} // namespace ct