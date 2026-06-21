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

#include <iostream>
#include <mutex>
#include <exception>

// Platform-specific H.264 encoder includes.
// Each platform defines its own encoder through the HAL interface.
#ifdef HAVE_MPP
#include "hal/platforms/rock3c/mpp_h264_encoder.hpp"
#elif defined(HAVE_SOFTENC)
#include "hal/platforms/rdk-x5/h264_encoder_hw.hpp"
#else
#error "No H.264 encoder defined for this platform. Set HAVE_MPP or HAVE_SOFTENC."
#endif

namespace ct {

// Static H.264 encoder instance (lazy-initialized, shared across all bridge instances)
std::unique_ptr<H264Encoder> MjpegBridgeActor::h264_encoder_;
std::once_flag MjpegBridgeActor::h264_init_flag_;

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

        auto h264 = encode_h264(f);
        if (h264.empty()) return;

        // Build MjpegFrame message (reusing same struct, now carrying H.264 data)
        MjpegFrame mjpeg_frame;
        mjpeg_frame.data = h264;
        mjpeg_frame.timestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count());
        mjpeg_frame.width = f.width;
        mjpeg_frame.height = f.height;
        mjpeg_frame.quality = 85;

        // Send to HTTP server for streaming
        ramen::send(server_actor_name_, std::move(mjpeg_frame));

        // Push into circular buffer (overwrites oldest if full)
        push_frame(std::move(h264), f.width, f.height);
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
    if (type == typeid(MjpegFrame)) {
        auto latest = pop_newest();
        if (latest.has_value()) {
            ramen::send(server_actor_name_, std::move(latest.value()));
        }
    }
}

// ─── Circular Buffer ───────────────────────────────────────────────────────────

void MjpegBridgeActor::push_frame(std::vector<uint8_t>&& h264_data, int width, int height) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    auto now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count());

    Slot& slot = buffer_[write_index_];
    slot.data = std::move(h264_data);
    slot.width = width;
    slot.height = height;
    slot.timestamp = now;
    slot.valid = true;

    write_index_ = (write_index_ + 1) % CIRCULAR_BUFFER_SIZE;
    if (count_ < CIRCULAR_BUFFER_SIZE) {
        ++count_;
    } else {
        read_index_ = (read_index_ + 1) % CIRCULAR_BUFFER_SIZE;
    }
}

std::optional<MjpegFrame> MjpegBridgeActor::pop_newest() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    if (count_ == 0) return std::nullopt;

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

// ─── H.264 Encoding ────────────────────────────────────────────────────────────

std::vector<uint8_t> MjpegBridgeActor::encode_h264(const FrameBuffer& f) {

    if (!f.data || f.width == 0 || f.height == 0) {
        std::cerr << "[MjpegBridgeActor] encode_h264: invalid frame (data="
                  << (void*)f.data << " w=" << f.width << " h=" << f.height << ")\n";
        return {};
    }

    // Lazy-initialize the shared H.264 encoder (platform HAL implementation)
    std::call_once(h264_init_flag_, [&]() {
#ifdef HAVE_MPP
        auto enc = std::make_unique<MppH264Encoder>();
#elif defined(HAVE_SOFTENC)
        auto enc = std::make_unique<SpH264Encoder>();
#endif
        if (enc->init(f.width, f.height, 26) == 0) {
            h264_encoder_ = std::move(enc);
            std::cout << "[MjpegBridgeActor] H.264 encoder initialized for "
                      << f.width << "x" << f.height << "\n";
        } else {
            std::cerr << "[MjpegBridgeActor] H.264 encoder init failed\n";
        }
    });

    if (!h264_encoder_) {
        std::cerr << "[MjpegBridgeActor] encode_h264: no encoder available\n";
        return {};
    }

    auto result = h264_encoder_->encode(f.dma_fd, f.size);
    if (result.empty()) {
        std::cerr << "[MjpegBridgeActor] encode_h264: encoder returned empty result\n";
    }
    return result;
}

} // namespace ct