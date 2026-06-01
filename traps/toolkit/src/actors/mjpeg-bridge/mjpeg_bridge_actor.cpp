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

// ─── Constructor ───────────────────────────────────────────────────────────────
// Initializes the in_frame pushable with a lambda that captures `this`.
MjpegBridgeActor::MjpegBridgeActor()
    : in_frame([this](const FrameBuffer& f) {
        if (!active_) {
            static int skipped = 0;
            if (++skipped % 300 == 0)
                std::cout << "[MjpegBridgeActor] skipped frame #" << skipped
                          << " (active_=" << active_.load() << ")\n";
            return;   // skip encode when no clients connected
        }

        auto jpeg = encode_jpeg(f);
        if (jpeg.empty()) return;

        // Build MjpegFrame message
        MjpegFrame mjpeg_frame;
        mjpeg_frame.data = jpeg;
        mjpeg_frame.timestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count());
        mjpeg_frame.width = f.width;
        mjpeg_frame.height = f.height;
        mjpeg_frame.quality = 85;

        // Send to HTTP server for MJPEG streaming
        ramen::send(server_actor_name_, std::move(mjpeg_frame));

        // Push into circular buffer (overwrites oldest if full)
        push_frame(std::move(jpeg), f.width, f.height);
    })
{}

// ─── Lifecycle ─────────────────────────────────────────────────────────────────

void MjpegBridgeActor::onStart() {
    std::cout << "[MjpegBridgeActor] started\n";
}

void MjpegBridgeActor::onStop() {
    std::cout << "[MjpegBridgeActor] stopped\n";
}

// ─── Message Dispatch ──────────────────────────────────────────────────────────

void MjpegBridgeActor::onMessageAny(const std::type_info& type, void* msg) {
    // Handle requests from the HTTP server for the latest frame.
    // The HTTP server sends a request message when a new MJPEG client connects
    // and needs the latest frame.
    if (type == typeid(MjpegFrame)) {
        // A request for the latest frame — pop from circular buffer and send back
        auto latest = pop_newest();
        if (latest.has_value()) {
            ramen::send(server_actor_name_, std::move(latest.value()));
        }
    }
}

// ─── Circular Buffer ───────────────────────────────────────────────────────────

void MjpegBridgeActor::push_frame(std::vector<uint8_t>&& jpeg_data, int width, int height) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    auto now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count());

    Slot& slot = buffer_[write_index_];
    slot.data = std::move(jpeg_data);
    slot.width = width;
    slot.height = height;
    slot.timestamp = now;
    slot.valid = true;

    write_index_ = (write_index_ + 1) % CIRCULAR_BUFFER_SIZE;
    if (count_ < CIRCULAR_BUFFER_SIZE) {
        ++count_;
    } else {
        // Buffer full — advance read index to drop oldest
        read_index_ = (read_index_ + 1) % CIRCULAR_BUFFER_SIZE;
    }
}

std::optional<MjpegFrame> MjpegBridgeActor::pop_newest() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    if (count_ == 0) return std::nullopt;

    // Read the newest valid slot (the one just before write_index_)
    size_t newest = (write_index_ == 0) ? CIRCULAR_BUFFER_SIZE - 1 : write_index_ - 1;
    Slot& slot = buffer_[newest];

    if (!slot.valid) return std::nullopt;

    MjpegFrame frame;
    frame.data = slot.data;
    frame.width = slot.width;
    frame.height = slot.height;
    frame.timestamp = slot.timestamp;
    frame.quality = 85;

    return frame;
}

std::optional<MjpegFrame> MjpegBridgeActor::try_get() {
    return pop_newest();
}

// ─── JPEG Encoding ─────────────────────────────────────────────────────────────

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
