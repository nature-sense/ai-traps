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
