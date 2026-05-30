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

#include <ramen.hpp>
#include <atomic>
#include <mutex>
#include <optional>
#include <vector>
#include <cstdint>

namespace ct {

// ─── HiresMjpegBridge ─────────────────────────────────────────────────────────
// Buffers the latest raw MJPEG frame from the camera for the hi-res stream.
// Unlike MjpegBridge (which encodes NV12→JPEG), this bridge stores the raw
// MJPEG data directly from the UVC camera, avoiding re-encoding.
//
// Pipeline thread:  pushes raw MJPEG → in_frame → latest_
// HTTP handler thread: calls try_get() to drain latest_
//
// Single-slot design: if the HTTP client is slow the frame is overwritten.
struct HiresMjpegBridge {
    // ── Input (wired inside Ramen graph) ──────────────────────────────────────
    ramen::Pushable<std::vector<uint8_t>> in_frame = [this](const std::vector<uint8_t>& f) {
        if (!active_) return;   // skip when no clients connected
        std::lock_guard lock(mutex_);
        latest_ = f;
    };

    // ── Called by HTTP handler thread ─────────────────────────────────────────
    std::optional<std::vector<uint8_t>> try_get() {
        std::lock_guard lock(mutex_);
        if (latest_.empty()) return std::nullopt;
        return latest_;
    }

    void client_connected()    { ++active_; }
    void client_disconnected() { if (active_ > 0) --active_; }

private:
    std::mutex                     mutex_;
    std::vector<uint8_t>           latest_;
    std::atomic<int>               active_{0};
};

} // namespace ct
