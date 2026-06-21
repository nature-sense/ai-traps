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

#include "hal/api/inference_hal.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <Python.h>

namespace ct {

// ─── InferenceHalPython ─────────────────────────────────────────────────────
// Concrete IInferenceHAL implementation that embeds CPython and uses the
// Rockchip rknn.api Python module for NPU inference.
//
// This avoids the C ABI incompatibility between librknnrt.so v2.3.2 and the
// struct-based C API. The Python rknn-toolkit2 module (v2.3.2) works correctly
// with the NPU on the ROCK 3C (RK3566).
//
// The Python inference module is loaded as a string literal embedded in the
// binary (inference_hal_python_script.cpp), or from a file at init time.
class InferenceHalPython : public IInferenceHAL {
public:
    InferenceHalPython();
    ~InferenceHalPython() override;

    InferenceHalPython(const InferenceHalPython&) = delete;
    InferenceHalPython& operator=(const InferenceHalPython&) = delete;

    // ── IInferenceHAL interface ────────────────────────────────────────────────
    bool init(const std::string& model_path, float confidence_threshold) override;
    std::vector<Detection> detect(const FrameBuffer& frame) override;

    int64_t last_inference_us() const override { return last_inference_us_; }
    uint32_t input_width() const override { return static_cast<uint32_t>(model_in_w_); }
    uint32_t input_height() const override { return static_cast<uint32_t>(model_in_h_); }

    void shutdown() override;

private:
    // Convert a Python list of detections to C++ Detection vector
    static std::vector<Detection> py_list_to_detections(PyObject* py_list);

    // ── CPython state ─────────────────────────────────────────────────────────
    PyObject* main_module_   = nullptr;  // "__main__"
    PyObject* main_dict_     = nullptr;  // "__main__" __dict__
    PyObject* detect_func_   = nullptr;  // callable: detect(frame_bytes, w, h)

    bool python_initialised_ = false;
    bool model_loaded_       = false;

    // ── Model info ─────────────────────────────────────────────────────────────
    int model_in_w_ = 320;
    int model_in_h_ = 320;
    float conf_thresh_ = 0.5f;

    // ── Timing ─────────────────────────────────────────────────────────────────
    int64_t last_inference_us_ = 0;
};

} // namespace ct