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
#include "hal/api/types.hpp"
#include "jpeg-encoder/hardware_jpeg_encoder.hpp"
#include "http_sse_actor.h"
#include <atomic>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>
#include <memory>
#include <iostream>
#include <cstdint>
#include <array>
#include <chrono>

namespace ct {

// ─── MjpegBridgeActor ──────────────────────────────────────────────────────────────
// Bridges the single-threaded Ramen pipeline to the multi-threaded HTTP server.
//
// This is a proper Ramen actor (extends ramen::Actor) with a mutex-protected
// circular buffer that handles the speed disparity between the pipeline writer
// thread and the HTTP handler thread.
//
// Pipeline thread:  pushes frame_medium → in_frame → JPEG encode → circular buffer
//                   → sends MjpegFrame to HttpSseActor via ramen::send()
// HTTP handler thread: calls try_get() to drain latest frame from circular buffer
//
// Circular buffer design:
//   - Fixed-size ring buffer (CIRCULAR_BUFFER_SIZE slots)
//   - Writer (pipeline thread) always overwrites the oldest slot if full (drop policy)
//   - Reader (HTTP handler thread) reads the newest available slot
//   - No heap allocations during steady-state operation
//
// JPEG encoding happens here (not in CaptureNode) so frames are only encoded
// when at least one HTTP client is connected (active_ guard).
//
// Uses Rockchip MPP hardware JPEG encoder (VEPU2 on RK3566) for NV12→JPEG
// conversion. Falls back to software libjpeg-turbo if MPP is unavailable.
// Hardware encoding takes ~4ms per 640×480 frame vs ~15ms for software.
class MjpegBridgeActor : public ramen::Actor {
public:
    MjpegBridgeActor();
    ~MjpegBridgeActor() noexcept override = default;

    // ── Ramen Actor Lifecycle ──────────────────────────────────────────────────
    void onStart() override;
    void onStop() override;

    // ── Type-erased message dispatch ───────────────────────────────────────────
    // Handles requests from the HTTP server for the latest frame.
    void onMessageAny(const std::type_info& type, void* msg) override;

    // ── Input (wired inside Ramen graph) ──────────────────────────────────────
    // Receives NV12 frames from the overlay, encodes to JPEG, and pushes into
    // the circular buffer. Initialized in constructor.
    ramen::Pushable<FrameBuffer> in_frame;

    // ── Called by HTTP handler thread ─────────────────────────────────────────
    // Returns the latest frame from the circular buffer, or nullopt if empty.
    std::optional<MjpegFrame> try_get();

    // Track connected client count so we can skip encoding when idle.
    void client_connected()    { ++active_; }
    void client_disconnected() { if (active_ > 0) --active_; }

    // Name of the HTTP server actor to send MJPEG frames to.
    std::string server_actor_name_{"http_server"};

private:
    // ── Circular Buffer ────────────────────────────────────────────────────────
    static constexpr size_t CIRCULAR_BUFFER_SIZE = 4;

    struct Slot {
        std::vector<uint8_t> data;
        int width = 0;
        int height = 0;
        uint64_t timestamp = 0;
        bool valid = false;
    };

    // Push a new frame into the circular buffer (writer side, pipeline thread).
    void push_frame(std::vector<uint8_t>&& jpeg_data, int width, int height);

    // Pop the newest valid frame from the circular buffer (reader side, HTTP thread).
    std::optional<MjpegFrame> pop_newest();

    std::array<Slot, CIRCULAR_BUFFER_SIZE> buffer_;
    std::mutex buffer_mutex_;
    size_t write_index_{0};
    size_t read_index_{0};
    size_t count_{0};

    // ── JPEG Encoding ──────────────────────────────────────────────────────────
    std::vector<uint8_t> encode_jpeg(const FrameBuffer& f);

    // Hardware MPP encoder (lazy-initialized, shared across all bridge instances)
    static std::unique_ptr<HardwareJpegEncoder> hw_encoder_;
    static std::once_flag hw_init_flag_;

    // ── State ──────────────────────────────────────────────────────────────────
    std::atomic<int> active_{0};
};


} // namespace ct
