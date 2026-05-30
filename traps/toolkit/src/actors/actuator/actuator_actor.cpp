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

#include "actuator_actor.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <thread>

namespace ct {

// ─── GPIO sysfs core ───────────────────────────────────────────────────────

static constexpr const char* GPIO_SYSFS = "/sys/class/gpio";

static bool gpio_export(int pin) {
    std::ofstream f(std::string(GPIO_SYSFS) + "/export");
    if (!f) return false;
    f << pin;
    return f.good();
}

static bool gpio_unexport(int pin) {
    std::ofstream f(std::string(GPIO_SYSFS) + "/unexport");
    if (!f) return false;
    f << pin;
    return f.good();
}

static bool gpio_set_dir(int pin, const char* dir) {
    std::ostringstream path;
    path << GPIO_SYSFS << "/gpio" << pin << "/direction";
    std::ofstream f(path.str());
    if (!f) return false;
    f << dir;
    return f.good();
}

static bool gpio_write(int pin, int value) {
    std::ostringstream path;
    path << GPIO_SYSFS << "/gpio" << pin << "/value";
    std::ofstream f(path.str());
    if (!f) return false;
    f << value;
    return f.good();
}

// ─── ActuatorActor implementation ─────────────────────────────────────────────

bool ActuatorActor::setup_gpio(int pin) {
    if (!gpio_export(pin)) {
        std::cerr << "[ActuatorActor] gpio_export(" << pin
                  << ") failed — may already be exported\n";
    }

    if (!gpio_set_dir(pin, "out")) {
        std::cerr << "[ActuatorActor] gpio_set_dir(" << pin
                  << ", out) failed: " << std::strerror(errno) << "\n";
        return false;
    }

    // Ensure initial state is LOW
    gpio_write(pin, 0);

    std::cout << "[ActuatorActor] GPIO " << pin << " configured as output\n";
    return true;
}

void ActuatorActor::cleanup_gpio(int pin) {
    gpio_write(pin, 0);  // Ensure LOW before unexport
    gpio_unexport(pin);
}

bool ActuatorActor::init(const std::string& type, int gpio_pin, int duration_ms) {
    type_       = type;
    gpio_pin_   = gpio_pin;
    duration_ms_ = duration_ms;

    std::cout << "[ActuatorActor] init (type=" << type_
              << ", gpio=" << gpio_pin_
              << ", duration=" << duration_ms_ << "ms)\n";

    if (type_ == "none") {
        std::cout << "[ActuatorActor] no-op mode — triggers will be logged only\n";
        return true;
    }

    if (gpio_pin_ < 0) {
        std::cerr << "[ActuatorActor] no GPIO pin configured for type="
                  << type_ << "\n";
        return false;
    }

    if (type_ == "relay") {
        if (!setup_gpio(gpio_pin_)) {
            std::cerr << "[ActuatorActor] GPIO setup failed\n";
            return false;
        }
    } else if (type_ == "servo") {
        // Servo mode: same GPIO setup but with PWM timing
        if (!setup_gpio(gpio_pin_)) {
            std::cerr << "[ActuatorActor] GPIO setup failed for servo\n";
            return false;
        }
        std::cout << "[ActuatorActor] servo mode — software PWM on GPIO "
                  << gpio_pin_ << "\n";
    } else {
        std::cerr << "[ActuatorActor] unknown actuator type: " << type_ << "\n";
        return false;
    }

    std::cout << "[ActuatorActor] ready\n";
    return true;
}

void ActuatorActor::shutdown() {
    running_ = false;
    if (timer_thread_.joinable()) {
        timer_thread_.join();
    }

    if (gpio_pin_ >= 0 && type_ != "none") {
        deactivate();
        cleanup_gpio(gpio_pin_);
    }

    std::cout << "[ActuatorActor] shutdown\n";
}

void ActuatorActor::activate() {
    active_ = true;

    if (type_ == "relay") {
        // Set GPIO HIGH to energize relay
        if (gpio_write(gpio_pin_, 1)) {
            std::cout << "[ActuatorActor] relay ON (GPIO " << gpio_pin_ << " HIGH)\n";
        } else {
            std::cerr << "[ActuatorActor] failed to set GPIO HIGH\n";
        }
    } else if (type_ == "servo") {
        // For servo: set GPIO HIGH (software PWM handled in timer)
        if (gpio_write(gpio_pin_, 1)) {
            std::cout << "[ActuatorActor] servo ON (GPIO " << gpio_pin_ << " HIGH)\n";
        }
    } else {
        // "none" type — just log
        std::cout << "[ActuatorActor] TRIGGER (no-op)\n";
    }

    // Join any previous timer thread before starting a new one
    if (timer_thread_.joinable()) {
        running_ = false;
        timer_thread_.join();
    }

    // Start timer to deactivate after duration
    running_ = true;
    timer_thread_ = std::thread([this] { activation_timer(); });
}

void ActuatorActor::deactivate() {
    if (type_ == "relay") {
        if (gpio_write(gpio_pin_, 0)) {
            std::cout << "[ActuatorActor] relay OFF (GPIO " << gpio_pin_ << " LOW)\n";
        }
    } else if (type_ == "servo") {
        if (gpio_write(gpio_pin_, 0)) {
            std::cout << "[ActuatorActor] servo OFF (GPIO " << gpio_pin_ << " LOW)\n";
        }
    } else {
        std::cout << "[ActuatorActor] deactivate (no-op)\n";
    }

    active_ = false;
}

void ActuatorActor::activation_timer() {
    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms_));
    if (running_) {
        deactivate();
    }
}

} // namespace ct
