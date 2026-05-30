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
#include <chrono>

namespace ct {

// ─── ActuatorActor ────────────────────────────────────────────────────────────
// Controls a trap mechanism (servo, relay, solenoid) via GPIO.
// Receives trigger signals from DecisionActor and activates the mechanism
// for a configurable duration.
//
// Supported actuator types:
//   "relay"   — Simple GPIO high/low (e.g., electronic strike, solenoid)
//   "servo"   — PWM servo control (via GPIO bit-banging or hardware PWM)
//   "none"    — No-op (log only, for testing)
//
// GPIO interface: Uses sysfs (/sys/class/gpio) for relay mode.
// Servo mode uses software PWM via a separate thread.
struct ActuatorActor {
    // ── Input ─────────────────────────────────────────────────────────────────
    ramen::Pushable<bool> in_trigger = [this](bool triggered) {
        if (triggered && !active_) {
            activate();
        }
    };

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    // type:     "relay", "servo", "none" (default: "none")
    // gpio_pin: GPIO pin for relay/servo signal
    // duration_ms: How long to keep the mechanism active (ms)
    bool init(const std::string& type = "none",
              int gpio_pin = -1,
              int duration_ms = 3000);
    void shutdown();

    // ── Destructor ────────────────────────────────────────────────────────────
    ~ActuatorActor() { shutdown(); }

    // ── Runtime query ─────────────────────────────────────────────────────────
    bool is_active() const { return active_; }

private:
    void activate();
    void deactivate();
    void activation_timer();
    bool setup_gpio(int pin);
    void cleanup_gpio(int pin);

    std::string type_ = "none";
    int  gpio_pin_    = -1;
    int  duration_ms_ = 3000;

    std::atomic<bool> active_{false};
    std::atomic<bool> running_{false};
    std::thread       timer_thread_;
};

} // namespace ct
