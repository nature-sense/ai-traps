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

#include "inference_hal_python.hpp"
#include <iostream>
#include <cstring>
#include <chrono>

namespace ct {

// ─── Embedded Python inference script ─────────────────────────────────────────
// This script is executed in the CPython interpreter at init time.
// It loads the RKNN model and exposes a detect() function that the C++ code
// calls for each frame.
static const char* PYTHON_SCRIPT = R"(
import sys, numpy as np
from rknn.api import RKNN

MODEL_PATH = None
RKNN_INST = None
CONF_THRESH = 0.5
MODEL_W = 320
MODEL_H = 320

def init_inference(model_path, conf_thresh):
    global MODEL_PATH, RKNN_INST, CONF_THRESH
    MODEL_PATH = model_path
    CONF_THRESH = conf_thresh
    print(f"[RKNN-Python] Loading model: {model_path}")
    rknn = RKNN(verbose=False)
    ret = rknn.load_rknn(path=model_path)
    if ret != 0:
        raise RuntimeError(f"load_rknn failed: {ret}")
    print("[RKNN-Python] Initializing runtime...")
    ret = rknn.init_runtime(target='rk3566')
    if ret != 0:
        raise RuntimeError(f"init_runtime failed: {ret}")
    RKNN_INST = rknn
    print("[RKNN-Python] Ready")

def _nv12_sized(w, h):
    y_sz = w * h
    uv_sz = w * (h // 2)
    return y_sz + uv_sz

def _nv12_to_rgb(nv12, src_w, src_h):
    h, w = src_h, src_w
    y = nv12[:w*h].reshape(h, w).astype(np.float32)
    uv = nv12[w*h:].reshape(h//2, w, 2).astype(np.float32)

    u = uv[:,:,0]
    v = uv[:,:,1]

    # Bilinear resize Y and UV
    dst_w, dst_h = MODEL_W, MODEL_H

    # Use OpenCV-like resize via numpy (keep it simple - nearest neighbour)
    # For Y plane
    y_resized = y[::h//dst_h, ::w//dst_w] if h >= dst_h else y
    # Proper resize using numpy + interpolation
    from numpy.lib.stride_tricks import as_strided
    row_rat = h / dst_h
    col_rat = w / dst_w
    rows = np.floor(np.arange(dst_h) * row_rat).astype(int)
    cols = np.floor(np.arange(dst_w) * col_rat).astype(int)
    y_resized = y[rows][:, cols]

    u_resized = u[rows//2][:, cols//2]
    v_resized = v[rows//2][:, cols//2]

    u_up = np.repeat(np.repeat(u_resized, 2, axis=0), 2, axis=1)[:dst_h, :dst_w]
    v_up = np.repeat(np.repeat(v_resized, 2, axis=0), 2, axis=1)[:dst_h, :dst_w]

    r = y_resized + 1.402 * (v_up - 128)
    g = y_resized - 0.344 * (u_up - 128) - 0.714 * (v_up - 128)
    b = y_resized + 1.772 * (u_up - 128)

    rgb = np.stack((r, g, b), axis=-1)
    rgb = np.clip(rgb, 0, 255).astype(np.uint8)
    return rgb

def detect(frame_bytes, src_w, src_h):
    global RKNN_INST, CONF_THRESH, MODEL_W, MODEL_H
    if RKNN_INST is None:
        return []

    # Convert NV12 bytes → RGB 320x320
    nv12 = np.frombuffer(frame_bytes, dtype=np.uint8)
    rgb = _nv12_to_rgb(nv12, src_w, src_h)

    # Run inference
    input_data = rgb[np.newaxis, :, :, :].astype(np.uint8)
    outputs = RKNN_INST.inference(inputs=[input_data])

    out = outputs[0]
    if out.ndim == 3:
        data = out[0]
        c0, c1 = data.shape[0], data.shape[1]
        inv_w = 1.0 / MODEL_W
        inv_h = 1.0 / MODEL_H

        detections = []
        if c0 == 5 and c1 == 2100:
            for i in range(c1):
                x1, y1, x2, y2, conf = data[0,i], data[1,i], data[2,i], data[3,i], data[4,i]
                if conf < CONF_THRESH:
                    continue
                detections.append([float(x1*inv_w), float(y1*inv_h),
                                    float(abs(x2-x1)*inv_w), float(abs(y2-y1)*inv_h),
                                    float(conf), 0])
        elif c1 == 6:
            for i in range(c0):
                x1, y1, x2, y2, conf, cls = data[i,0], data[i,1], data[i,2], data[i,3], data[i,4], data[i,5]
                if conf < CONF_THRESH:
                    continue
                detections.append([float(x1*inv_w), float(y1*inv_h),
                                    float(abs(x2-x1)*inv_w), float(abs(y2-y1)*inv_h),
                                    float(conf), int(cls)])
        return detections
    return []
)";

// ─── Constructor / Destructor ────────────────────────────────────────────────

InferenceHalPython::InferenceHalPython() = default;

InferenceHalPython::~InferenceHalPython() {
    shutdown();
}

// ─── init ─────────────────────────────────────────────────────────────────────

bool InferenceHalPython::init(const std::string& model_path, float conf_thresh) {
    if (python_initialised_) {
        std::cerr << "[InferenceHalPython] already initialised\n";
        return false;
    }

    conf_thresh_ = conf_thresh;
    std::cout << "[InferenceHalPython] initialising CPython...\n";

    // ── 1. Initialise CPython ─────────────────────────────────────────────────
    Py_Initialize();
    if (!Py_IsInitialized()) {
        std::cerr << "[InferenceHalPython] Py_Initialize failed\n";
        return false;
    }

    main_module_ = PyImport_ImportModule("__main__");
    if (!main_module_) {
        std::cerr << "[InferenceHalPython] failed to get __main__\n";
        Py_Finalize();
        return false;
    }
    main_dict_ = PyModule_GetDict(main_module_);

    // ── 2. Execute the embedded Python script ─────────────────────────────────
    PyObject* script_result = PyRun_String(PYTHON_SCRIPT, Py_file_input,
                                           main_dict_, main_dict_);
    if (!script_result) {
        std::cerr << "[InferenceHalPython] script execution failed\n";
        PyErr_Print();
        Py_Finalize();
        return false;
    }
    Py_DECREF(script_result);

    // ── 3. Get the init_inference function and call it ────────────────────────
    PyObject* init_func = PyDict_GetItemString(main_dict_, "init_inference");
    if (!init_func || !PyCallable_Check(init_func)) {
        std::cerr << "[InferenceHalPython] init_inference not found\n";
        Py_Finalize();
        return false;
    }

    PyObject* args = Py_BuildValue("(sf)", model_path.c_str(), conf_thresh_);
    PyObject* result = PyObject_CallObject(init_func, args);
    Py_DECREF(args);

    if (!result) {
        std::cerr << "[InferenceHalPython] init_inference() failed\n";
        PyErr_Print();
        Py_Finalize();
        return false;
    }
    Py_DECREF(result);

    // ── 4. Get the detect function reference ──────────────────────────────────
    detect_func_ = PyDict_GetItemString(main_dict_, "detect");
    if (!detect_func_ || !PyCallable_Check(detect_func_)) {
        std::cerr << "[InferenceHalPython] detect function not found\n";
        Py_Finalize();
        return false;
    }

    python_initialised_ = true;
    model_loaded_ = true;

    std::cout << "[InferenceHalPython] ready (conf_thresh=" << conf_thresh_
              << ", model=" << model_path << ")\n";
    return true;
}

// ─── detect ───────────────────────────────────────────────────────────────────

std::vector<Detection> InferenceHalPython::detect(const FrameBuffer& frame) {
    if (!python_initialised_ || !model_loaded_) {
        std::cerr << "[InferenceHalPython] not initialised\n";
        return {};
    }

    auto t0 = std::chrono::steady_clock::now();

    // Call Python detect(frame_bytes, src_w, src_h)
    // frame.data is NV12 data
    PyObject* py_bytes = PyBytes_FromStringAndSize(
        static_cast<const char*>(frame.data),
        frame.size);
    PyObject* py_w = PyLong_FromUnsignedLong(frame.width);
    PyObject* py_h = PyLong_FromUnsignedLong(frame.height);

    PyObject* args = PyTuple_Pack(3, py_bytes, py_w, py_h);
    PyObject* result = PyObject_CallObject(detect_func_, args);

    Py_DECREF(py_bytes);
    Py_DECREF(py_w);
    Py_DECREF(py_h);
    Py_DECREF(args);

    std::vector<Detection> detections;

    if (result) {
        detections = py_list_to_detections(result);
        Py_DECREF(result);
    } else {
        std::cerr << "[InferenceHalPython] detect() failed\n";
        PyErr_Print();
    }

    auto t1 = std::chrono::steady_clock::now();
    last_inference_us_ = std::chrono::duration_cast<std::chrono::microseconds>(
                             t1 - t0).count();

    static int log_cnt = 0;
    if (++log_cnt <= 10) {
        std::cout << "[InferenceHalPython] " << (last_inference_us_ / 1000.0f)
                  << "ms, detections=" << detections.size() << "\n";
    }

    return detections;
}

// ─── py_list_to_detections ───────────────────────────────────────────────────

std::vector<Detection> InferenceHalPython::py_list_to_detections(PyObject* py_list) {
    std::vector<Detection> detections;

    if (!PyList_Check(py_list)) {
        return detections;
    }

    Py_ssize_t n = PyList_Size(py_list);
    detections.reserve(static_cast<size_t>(n));

    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject* item = PyList_GetItem(py_list, i);
        if (!item || !PyList_Check(item) || PyList_Size(item) < 6) {
            continue;
        }

        PyObject* py_x = PyList_GetItem(item, 0);
        PyObject* py_y = PyList_GetItem(item, 1);
        PyObject* py_w = PyList_GetItem(item, 2);
        PyObject* py_h = PyList_GetItem(item, 3);
        PyObject* py_conf = PyList_GetItem(item, 4);
        PyObject* py_cls = PyList_GetItem(item, 5);

        Detection det;
        det.x = static_cast<float>(PyFloat_AsDouble(py_x));
        det.y = static_cast<float>(PyFloat_AsDouble(py_y));
        det.w = static_cast<float>(PyFloat_AsDouble(py_w));
        det.h = static_cast<float>(PyFloat_AsDouble(py_h));
        det.confidence = static_cast<float>(PyFloat_AsDouble(py_conf));
        det.class_id = static_cast<int>(PyLong_AsLong(py_cls));

        detections.push_back(det);
    }

    return detections;
}

// ─── shutdown ─────────────────────────────────────────────────────────────────

void InferenceHalPython::shutdown() {
    if (python_initialised_) {
        // Call Python's RKNN release via the __main__ scope
        if (main_dict_) {
            PyObject* rknn_inst = PyDict_GetItemString(main_dict_, "RKNN_INST");
            if (rknn_inst && rknn_inst != Py_None) {
                PyObject* release_method = PyObject_GetAttrString(rknn_inst, "release");
                if (release_method && PyCallable_Check(release_method)) {
                    PyObject* rel_result = PyObject_CallObject(release_method, nullptr);
                    Py_XDECREF(rel_result);
                }
                Py_XDECREF(release_method);
            }
        }
        detect_func_ = nullptr;
        Py_Finalize();
        python_initialised_ = false;
        std::cout << "[InferenceHalPython] shutdown\n";
    }
}

} // namespace ct