#include "motion_sensor_actor.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <thread>
#include <unistd.h>

namespace ct {

// ─── GPIO sysfs core ───────────────────────────────────────────────────────
// These use the legacy sysfs GPIO interface (/sys/class/gpio).
// For production, consider migrating to libgpiod (gpiod_line_* API).

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

static bool gpio_set_edge(int pin, const char* edge) {
    std::ostringstream path;
    path << GPIO_SYSFS << "/gpio" << pin << "/edge";
    std::ofstream f(path.str());
    if (!f) return false;
    f << edge;
    return f.good();
}

static int gpio_read(int pin) {
    std::ostringstream path;
    path << GPIO_SYSFS << "/gpio" << pin << "/value";
    std::ifstream f(path.str());
    if (!f) return -1;
    int val = 0;
    f >> val;
    return val;
}

// ─── MotionSensorActor implementation ─────────────────────────────────────────

bool MotionSensorActor::setup_gpio(int pin) {
    // Try to export the GPIO pin (may fail if already exported)
    if (!gpio_export(pin)) {
        // EINVAL or EEXIST — pin may already be exported
        std::cerr << "[MotionSensorActor] gpio_export(" << pin
                  << ") failed — may already be exported\n";
    }

    // Set direction to input
    if (!gpio_set_dir(pin, "in")) {
        std::cerr << "[MotionSensorActor] gpio_set_dir(" << pin
                  << ", in) failed: " << std::strerror(errno) << "\n";
        return false;
    }

    // Try to set edge interrupt (both edges for PIR: rising=detect, falling=clear)
    if (!gpio_set_edge(pin, "both")) {
        std::cerr << "[MotionSensorActor] gpio_set_edge(" << pin
                  << ", both) not supported — falling back to polling\n";
        // Non-fatal: we'll poll instead
    }

    std::cout << "[MotionSensorActor] GPIO " << pin << " configured as input\n";
    return true;
}

bool MotionSensorActor::read_gpio(int pin) {
    int val = gpio_read(pin);
    return val == 1;
}

void MotionSensorActor::cleanup_gpio(int pin) {
    gpio_unexport(pin);
}

bool MotionSensorActor::init(int gpio_pin, int poll_ms) {
    gpio_pin_ = gpio_pin;
    poll_ms_  = poll_ms;

    if (gpio_pin_ < 0) {
        // No GPIO configured — always-on mode: emit wake immediately
        std::cout << "[MotionSensorActor] no GPIO configured — always-on mode\n";
        motion_detected_ = true;
        out_wake(true);
        initialized_ = true;
        return true;
    }

    // Set up GPIO
    if (!setup_gpio(gpio_pin_)) {
        std::cerr << "[MotionSensorActor] GPIO setup failed\n";
        // Continue in always-on mode as fallback
        motion_detected_ = true;
        out_wake(true);
        initialized_ = true;
        return true;
    }

    // Start polling thread
    running_ = true;
    poll_thread_ = std::thread([this] { poll_loop(); });

    initialized_ = true;
    std::cout << "[MotionSensorActor] ready (GPIO " << gpio_pin_
              << ", poll=" << poll_ms_ << "ms)\n";
    return true;
}

void MotionSensorActor::shutdown() {
    running_ = false;
    if (poll_thread_.joinable()) {
        poll_thread_.join();
    }

    if (gpio_pin_ >= 0) {
        cleanup_gpio(gpio_pin_);
    }

    initialized_ = false;
    std::cout << "[MotionSensorActor] shutdown\n";
}

void MotionSensorActor::poll_loop() {
    int prev_value = -1;  // unknown

    while (running_) {
        int value = gpio_read(gpio_pin_);
        if (value >= 0 && value != prev_value) {
            prev_value = value;
            if (value == 1) {
                // Motion detected (rising edge)
                motion_detected_ = true;
                out_wake(true);
                std::cout << "[MotionSensorActor] motion detected\n";
            } else {
                // Motion cleared (falling edge)
                motion_detected_ = false;
                std::cout << "[MotionSensorActor] motion cleared\n";
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms_));
    }
}

} // namespace ct
