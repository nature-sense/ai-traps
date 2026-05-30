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

#include "mjpeg_bridge_actor.hpp"

#include "jpeg-encoder/software_jpeg_encoder.hpp"
#include <iostream>
#include <mutex>
#include <exception>

namespace ct {

// Static encoder instance (lazy-initialized, shared across all bridge instances)
std::unique_ptr<HardwareJpegEncoder> MjpegBridgeActor::hw_encoder_;
std::once_flag MjpegBridgeActor::hw_init_flag_;

std::vector<uint8_t> MjpegBridgeActor::encode_jpeg(const FrameBuffer& f) {

    if (!f.data || f.width == 0 || f.height == 0) {
        std::cerr << "[MjpegBridgeActor] encode_jpeg: invalid frame (data="
                  << (void*)f.data << " w=" << f.width << " h=" << f.height << ")\n";
        return {};
    }

    // Try hardware MPP encoder first
    std::call_once(hw_init_flag_, [&]() {
        auto enc = std::make_unique<HardwareJpegEncoder>();
        if (enc->init(f.width, f.height, 85)) {
            hw_encoder_ = std::move(enc);
            std::cout << "[MjpegBridgeActor] Hardware MPP JPEG encoder initialized for "
                      << f.width << "x" << f.height << "\n";
        } else {
            std::cerr << "[MjpegBridgeActor] Hardware MPP JPEG encoder init failed, "
                      << "will fall back to software encoder\n";
        }
    });

    if (hw_encoder_) {
        auto result = hw_encoder_->encode(f.data, f.width, f.height, f.stride);
        if (!result.empty()) {
            return result;
        }
        std::cerr << "[MjpegBridgeActor] Hardware encode returned empty, falling back to software\n";
    }

    // Fallback: software encoder (lazy-initialized)
    static std::unique_ptr<SoftwareJpegEncoder> sw_encoder_;
    static std::once_flag sw_init_flag_;
    std::call_once(sw_init_flag_, [&]() {
        auto enc = std::make_unique<SoftwareJpegEncoder>();
        if (enc->init(f.width, f.height, 85)) {
            sw_encoder_ = std::move(enc);
            std::cout << "[MjpegBridgeActor] Software JPEG encoder initialized for "
                      << f.width << "x" << f.height << " (fallback)\n";
        } else {
            std::cerr << "[MjpegBridgeActor] Software JPEG encoder init failed\n";
        }
    });

    if (!sw_encoder_) {
        std::cerr << "[MjpegBridgeActor] encode_jpeg: no encoder available\n";
        return {};
    }

    auto result = sw_encoder_->encode(f.data, f.width, f.height, f.stride);
    if (result.empty()) {
        std::cerr << "[MjpegBridgeActor] encode_jpeg: encoder returned empty result\n";
    }
    return result;
}


} // namespace ct
