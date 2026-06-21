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
#include "hal/api/h264_encoder.hpp"
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
// Pipeline thread:  pushes frame_medium → in_frame → H.264 encode → circular buffer
//                   → sends H264Frame to HttpSseActor via ramen::send()
// HTTP handler thread: calls try_get() to drain latest frame from circular buffer
//
// Circular buffer design:
//   - Fixed-size ring buffer (CIRCULAR_BUFFER_SIZE slots)
//   - Writer (pipeline thread) always overwrites the oldest slot if full (drop policy)
//   - Reader (HTTP handler thread) reads the newest available slot
//   - No heap allocations during steady-state operation
//
// H.264 encoding uses a platform-specific HAL implementation selected at compile time:
//   - rock3c: MppH264Encoder (Rockchip MPP hardware VEPU)
//   - rdk-x5: SoftH264Encoder (FFmpeg libavcodec software encoder)
class MjpegBridgeActor : public ramen::Actor {
public:
    MjpegBridgeActor();
    ~MjpegBridgeActor() noexcept override = default;

    // ── Ramen Actor Lifecycle ──────────────────────────────────────────────────
    void onStart() override;
    void onStop() override;

    // ── Type-erased message dispatch ───────────────────────────────────────────
    void onMessageAny(const std::type_info& type, void* msg) override;

    // ── Input (wired inside Ramen graph) ──────────────────────────────────────
    ramen::Pushable<FrameBuffer> in_frame;

    // ── Called by HTTP handler thread ─────────────────────────────────────────
    std::optional<MjpegFrame> try_get();

    // Track connected client count so we can skip encoding when idle.
    void client_connected()    { ++active_; }
    void client_disconnected() { if (active_ > 0) --active_; }

    // Name of the HTTP server actor to send H.264 frames to.
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

    void push_frame(std::vector<uint8_t>&& h264_data, int width, int height);
    std::optional<MjpegFrame> pop_newest();

    std::array<Slot, CIRCULAR_BUFFER_SIZE> buffer_;
    std::mutex buffer_mutex_;
    size_t write_index_{0};
    size_t read_index_{0};
    size_t count_{0};

    // ── H.264 Encoding ────────────────────────────────────────────────────────
    std::vector<uint8_t> encode_h264(const FrameBuffer& f);

    // Hardware H.264 encoder (lazy-initialized, shared across all bridge instances)
    static std::unique_ptr<H264Encoder> h264_encoder_;
    static std::once_flag h264_init_flag_;

    // ── State ──────────────────────────────────────────────────────────────────
    std::atomic<int> active_{0};
};

} // namespace ct