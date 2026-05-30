#pragma once

#include <ramen.hpp>
#include "hal/api/types.hpp"
#include "jpeg-encoder/hardware_jpeg_encoder.hpp"
#include <atomic>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>
#include <memory>
#include <iostream>

namespace ct {

// ─── MjpegBridgeActor ──────────────────────────────────────────────────────────────
// Bridges the single-threaded Ramen pipeline to the multi-threaded HTTP server.
//
// Pipeline thread:  pushes frame_medium → in_frame → JPEG encode → latest_
// HTTP handler thread: calls try_get() to drain latest_ and write a MIME part
//
// Single-slot design: if the HTTP client is slow the encoder simply overwrites
// the previous frame (drop policy).  No queue buildup, no back-pressure.
//
// JPEG encoding happens here (not in CaptureNode) so frames are only encoded
// when at least one HTTP client is connected (active_ guard).
//
// Uses Rockchip MPP hardware JPEG encoder (VEPU2 on RK3566) for NV12→JPEG
// conversion. Falls back to software libjpeg-turbo if MPP is unavailable.
// Hardware encoding takes ~4ms per 640×480 frame vs ~15ms for software.
struct MjpegBridgeActor {
    // ── Input (wired inside Ramen graph) ──────────────────────────────────────
    ramen::Pushable<FrameBuffer> in_frame = [this](const FrameBuffer& f) {

        if (!active_) {
            static int skipped = 0;
            if (++skipped % 300 == 0)
                std::cout << "[MjpegBridgeActor] skipped frame #" << skipped << " (active_=" << active_.load() << ")\n";
            return;   // skip encode when no clients connected
        }
        auto jpeg = encode_jpeg(f);
        std::lock_guard lock(mutex_);
        latest_ = std::move(jpeg);
    };

    // ── Called by HTTP handler thread ─────────────────────────────────────────
    // Returns the latest frame and clears the slot, or nullopt if none ready.
    std::optional<std::vector<uint8_t>> try_get() {
        std::lock_guard lock(mutex_);
        return std::exchange(latest_, std::nullopt);
    }

    // Track connected client count so we can skip encoding when idle.
    void client_connected()    { ++active_; }
    void client_disconnected() { if (active_ > 0) --active_; }

private:
    std::vector<uint8_t> encode_jpeg(const FrameBuffer& f);


    // Hardware MPP encoder (lazy-initialized, shared across all bridge instances)
    static std::unique_ptr<HardwareJpegEncoder> hw_encoder_;
    static std::once_flag hw_init_flag_;

    std::mutex                           mutex_;
    std::optional<std::vector<uint8_t>> latest_;
    std::atomic<int>                     active_{0};
};


} // namespace ct

