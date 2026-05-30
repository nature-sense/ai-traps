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
#include <string>
#include <atomic>
#include <thread>
#include <functional>

namespace ct {

// ─── MotionSensorActor ────────────────────────────────────────────────────────
// Monitors a PIR motion sensor via GPIO sysfs and emits a wake signal when
// motion is detected. Designed to wake the pipeline from idle/sleep state.
//
// GPIO interface: Uses sysfs (/sys/class/gpio) or libgpiod if available.
// The GPIO pin is configured as an input with falling/rising edge interrupt.
//
// In idle mode, the pipeline can be paused (CameraActor stops capturing).
// When motion is detected, this actor emits out_wake() which the Pipeline
// uses to resume capture.
//
// If no GPIO is configured (gpio_pin < 0), the actor is a no-op pass-through
// that immediately emits out_wake() on startup (always-on mode).
struct MotionSensorActor {
    // ── Output ────────────────────────────────────────────────────────────────
    // Emitted when motion is detected (or immediately if GPIO is not configured).
    ramen::Pusher<bool> out_wake{};

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    // gpio_pin: GPIO pin number (sysfs). Set to -1 to disable (always-on mode).
    // poll_ms:  Polling interval in ms (only used if edge interrupt not available).
    bool init(int gpio_pin = -1, int poll_ms = 100);
    void shutdown();

    // ── Runtime query ─────────────────────────────────────────────────────────
    bool motion_detected() const { return motion_detected_; }
    void clear_motion() { motion_detected_ = false; }

private:
    void poll_loop();
    bool setup_gpio(int pin);
    bool read_gpio(int pin);
    void cleanup_gpio(int pin);

    int  gpio_pin_ = -1;
    int  poll_ms_  = 100;
    bool initialized_ = false;

    std::atomic<bool> running_{false};
    std::atomic<bool> motion_detected_{false};
    std::thread       poll_thread_;
};

} // namespace ct
