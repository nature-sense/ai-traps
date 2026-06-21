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

#include "inference_hal_bpu.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>

#include "dnn/hb_dnn.h"
#include "dnn/hb_sys.h"

namespace ct {

// ─── Constructor / Destructor ─────────────────────────────────────────────────
InferenceHalBPU::InferenceHalBPU()
    : input_tensor_{}
    , output_tensors_() {}

InferenceHalBPU::~InferenceHalBPU() {
    shutdown();
}

// ─── init ─────────────────────────────────────────────────────────────────────
bool InferenceHalBPU::init(const std::string& model_path, float confidence_threshold) {
    if (initialised_) {
        std::cerr << "[InferenceHalBPU] Already initialised\n";
        return true;
    }

    conf_thresh_ = confidence_threshold;

    // 1. Load the compiled BPU binary via hbDNNInitializeFromFiles
    const char* path_ptr = model_path.c_str();
    int ret = hbDNNInitializeFromFiles(&packed_dnn_handle_, &path_ptr, 1);
    if (ret != 0) {
        std::cerr << "[InferenceHalBPU] CRITICAL: hbDNNInitializeFromFiles failed with code: "
                  << ret << " for model: " << model_path << std::endl;
        return false;
    }

    // 2. Query the model name(s) inside the packed container
    const char** model_names = nullptr;
    int model_count = 0;
    hbDNNGetModelNameList(&model_names, &model_count, packed_dnn_handle_);
    if (model_count == 0 || model_names == nullptr) {
        std::cerr << "[InferenceHalBPU] No models found in packed container\n";
        hbDNNRelease(packed_dnn_handle_);
        packed_dnn_handle_ = nullptr;
        return false;
    }

    // 3. Extract the individual model handle
    ret = hbDNNGetModelHandle(&dnn_handle_, packed_dnn_handle_, model_names[0]);
    if (ret != 0 || dnn_handle_ == nullptr) {
        std::cerr << "[InferenceHalBPU] hbDNNGetModelHandle failed for: "
                  << model_names[0] << std::endl;
        hbDNNRelease(packed_dnn_handle_);
        packed_dnn_handle_ = nullptr;
        return false;
    }

    std::cout << "[InferenceHalBPU] Loaded BPU model: " << model_path
              << " | identity: " << model_names[0] << std::endl;

    // 4. Allocate the input tensor based on model properties
    hbDNNGetInputTensorProperties(&input_tensor_.properties, dnn_handle_, 0);

    // Determine model input dimensions from the tensor properties
    // For NV12 models, the aligned size includes both Y and UV planes
    model_in_w_ = input_tensor_.properties.validShape.dimensionSize[2];    // width
    model_in_h_ = input_tensor_.properties.validShape.dimensionSize[1];    // height
    std::cout << "[InferenceHalBPU] Model input: " << model_in_w_ << "x" << model_in_h_
              << " (aligned: " << input_tensor_.properties.alignedByteSize << " bytes)"
              << std::endl;

    // Allocate cached memory for the input tensor (accessible by both CPU and BPU)
    ret = hbSysAllocCachedMem(&input_tensor_.sysMem,
                              input_tensor_.properties.alignedByteSize);
    if (ret != 0) {
        std::cerr << "[InferenceHalBPU] hbSysAllocCachedMem (input) failed: " << ret << std::endl;
        hbDNNRelease(packed_dnn_handle_);
        packed_dnn_handle_ = nullptr;
        dnn_handle_ = nullptr;
        return false;
    }

    // 5. Query and allocate output tensors
    ret = hbDNNGetOutputCount(&output_count_, dnn_handle_);
    if (ret != 0 || output_count_ == 0) {
        std::cerr << "[InferenceHalBPU] No output tensors found\n";
        hbSysFreeMem(&input_tensor_.sysMem);
        hbDNNRelease(packed_dnn_handle_);
        packed_dnn_handle_ = nullptr;
        dnn_handle_ = nullptr;
        return false;
    }

    output_tensors_.resize(output_count_);
    for (int i = 0; i < output_count_; ++i) {
        hbDNNGetOutputTensorProperties(&output_tensors_[i].properties, dnn_handle_, i);
        ret = hbSysAllocCachedMem(&output_tensors_[i].sysMem,
                                   output_tensors_[i].properties.alignedByteSize);
        if (ret != 0) {
            std::cerr << "[InferenceHalBPU] hbSysAllocCachedMem (output " << i
                      << ") failed: " << ret << std::endl;
            // Free already-allocated outputs
            for (int j = 0; j < i; ++j) {
                hbSysFreeMem(&output_tensors_[j].sysMem);
            }
            hbSysFreeMem(&input_tensor_.sysMem);
            hbDNNRelease(packed_dnn_handle_);
            packed_dnn_handle_ = nullptr;
            dnn_handle_ = nullptr;
            return false;
        }
    }

    std::cout << "[InferenceHalBPU] API Initialised. Model: " << model_names[0]
              << " | Input: " << model_in_w_ << "x" << model_in_h_
              << " | Outputs: " << output_count_ << std::endl;

    initialised_ = true;
    return true;
}

// ─── detect ───────────────────────────────────────────────────────────────────
std::vector<Detection> InferenceHalBPU::detect(const FrameBuffer& frame) {
    if (!initialised_ || dnn_handle_ == nullptr) {
        std::cerr << "[InferenceHalBPU] detect() called but not initialised\n";
        return {};
    }

    auto t_start = std::chrono::high_resolution_clock::now();

    // 1. Resize the incoming NV12 frame to the model's expected input size
    std::vector<uint8_t> resized_input;
    if (!resize_nv12(frame, resized_input, model_in_w_, model_in_h_)) {
        std::cerr << "[InferenceHalBPU] Frame resize failed\n";
        return {};
    }

    // 2. Copy resized NV12 data into the BPU input tensor memory
    //    The BPU expects NV12 format in a single contiguous buffer:
    //    [Y plane: W*H bytes] [UV plane: W*H/2 bytes]
    size_t expected_size = input_tensor_.properties.alignedByteSize;
    size_t actual_size = static_cast<size_t>(model_in_w_ * model_in_h_ * 3 / 2);

    if (resized_input.size() < actual_size) {
        std::cerr << "[InferenceHalBPU] Resized frame too small: "
                  << resized_input.size() << " < " << actual_size << std::endl;
        return {};
    }

    // Copy to input tensor memory and flush cache so BPU can see it
    std::memcpy(input_tensor_.sysMem.virAddr, resized_input.data(),
                std::min(actual_size, expected_size));
    hbSysFlushMem(&input_tensor_.sysMem, HB_SYS_MEM_CACHE_CLEAN);

    // 3. Prepare inference task
    hbDNNTaskHandle_t task_handle = nullptr;
    hbDNNTensor* input_ptr = &input_tensor_;
    hbDNNTensor* output_ptrs = output_tensors_.data();

    int ret = hbDNNInfer(&task_handle,
                          &output_ptrs,
                          const_cast<const hbDNNTensor**>(&input_ptr),
                          dnn_handle_,
                          1);  // Inference with 1 task at a time
    if (ret != 0) {
        std::cerr << "[InferenceHalBPU] hbDNNInfer failed: " << ret << std::endl;
        return {};
    }

    // 4. Wait for inference to complete
    ret = hbDNNWaitTaskDone(task_handle, 0);  // 0 = block indefinitely
    if (ret != 0) {
        std::cerr << "[InferenceHalBPU] hbDNNWaitTaskDone failed: " << ret << std::endl;
        hbDNNReleaseTask(task_handle);
        return {};
    }

    // 5. Flush output cache so CPU can read results
    for (int i = 0; i < output_count_; ++i) {
        hbSysFlushMem(&output_tensors_[i].sysMem, HB_SYS_MEM_CACHE_INVALIDATE);
    }

    // 6. Release the task handle
    hbDNNReleaseTask(task_handle);

    // 7. Decode the output tensors into Detection structs
    auto detections = decode_yolo_output();

    // 8. Apply NMS
    detections = nms(detections);

    auto t_end = std::chrono::high_resolution_clock::now();
    last_inference_us_ = std::chrono::duration_cast<std::chrono::microseconds>(
                             t_end - t_start).count();

    if (!detections.empty()) {
        std::cout << "[InferenceHalBPU] Detected " << detections.size()
                  << " objects in " << last_inference_us_ / 1000 << " ms"
                  << std::endl;
    }

    return detections;
}

// ─── resize_nv12 ──────────────────────────────────────────────────────────────
bool InferenceHalBPU::resize_nv12(const FrameBuffer& src, std::vector<uint8_t>& dst,
                                   int dst_w, int dst_h) {
    const int src_w = static_cast<int>(src.width);
    const int src_h = static_cast<int>(src.height);

    if (!src.data || src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0) {
        return false;
    }

    // NV12 total size = Y plane + UV plane
    size_t dst_size = static_cast<size_t>(dst_w * dst_h * 3 / 2);
    dst.resize(dst_size);

    const uint8_t* src_y = static_cast<const uint8_t*>(src.data);
    const uint8_t* src_uv = src_y + src_w * src_h;

    uint8_t* dst_y = dst.data();
    uint8_t* dst_uv = dst_y + dst_w * dst_h;

    // Resize Y plane using nearest-neighbour
    for (int row = 0; row < dst_h; ++row) {
        int src_row = row * src_h / dst_h;
        src_row = std::min(src_row, src_h - 1);
        for (int col = 0; col < dst_w; ++col) {
            int src_col = col * src_w / dst_w;
            src_col = std::min(src_col, src_w - 1);
            dst_y[row * dst_w + col] = src_y[src_row * src_w + src_col];
        }
    }

    // Resize UV plane (half resolution in both directions)
    int uv_dst_h = dst_h / 2;
    int uv_dst_w = dst_w / 2;
    int uv_src_h = src_h / 2;
    int uv_src_w = src_w / 2;

    for (int row = 0; row < uv_dst_h; ++row) {
        int src_row = std::min(row * uv_src_h / uv_dst_h, uv_src_h - 1);
        for (int col = 0; col < uv_dst_w; ++col) {
            int src_col = std::min(col * uv_src_w / uv_dst_w, uv_src_w - 1);
            int src_idx = (src_row * uv_src_w + src_col) * 2;
            int dst_idx = (row * uv_dst_w + col) * 2;
            dst_uv[dst_idx]     = src_uv[src_idx];     // U
            dst_uv[dst_idx + 1] = src_uv[src_idx + 1]; // V
        }
    }

    return true;
}

// ─── decode_yolo_output ───────────────────────────────────────────────────────
std::vector<Detection> InferenceHalBPU::decode_yolo_output() {
    // YOLOv12n (and most YOLOv5/v8/v10/v12) detection models packaged for
    // the Horizon BPU produce output in [1, N, 6] format where each row is:
    //   [x_center, y_center, width, height, confidence, class_id]
    //
    // Coordinates are normalised [0, 1] relative to the model input size.
    //
    // The model may have multiple output tensors (e.g. three for FPN/PAN).
    // We concatenate all outputs and filter by confidence threshold.

    std::vector<Detection> detections;

    for (int o = 0; o < output_count_; ++o) {
        hbDNNTensor& out = output_tensors_[o];

        // Get output shape: typically [1, N, 6]
        int num_dims = out.properties.validShape.numDimensions;
        int num_elements = 1;
        for (int d = 0; d < num_dims; ++d) {
            num_elements *= out.properties.validShape.dimensionSize[d];
        }

        // Expected format: each detection is 6 floats
        constexpr int VALUES_PER_DET = 6;
        int num_detections = num_elements / VALUES_PER_DET;

        if (num_detections <= 0) continue;

        const float* out_data = static_cast<const float*>(out.sysMem.virAddr);

        for (int i = 0; i < num_detections; ++i) {
            int idx = i * VALUES_PER_DET;
            float x_center = out_data[idx + 0];
            float y_center = out_data[idx + 1];
            float width    = out_data[idx + 2];
            float height   = out_data[idx + 3];
            float conf     = out_data[idx + 4];
            int class_id   = static_cast<int>(out_data[idx + 5]);

            if (conf < conf_thresh_) continue;

            Detection det;
            det.class_id   = class_id;
            det.confidence = conf;
            // Convert [x_center, y_center, w, h] → [x, y, w, h] (top-left)
            det.x = std::max(0.0f, x_center - width / 2.0f);
            det.y = std::max(0.0f, y_center - height / 2.0f);
            det.w = std::min(1.0f - det.x, width);
            det.h = std::min(1.0f - det.y, height);

            detections.push_back(det);
        }
    }

    return detections;
}

// ─── nms ──────────────────────────────────────────────────────────────────────
std::vector<Detection> InferenceHalBPU::nms(const std::vector<Detection>& detections,
                                             float iou_threshold) {
    if (detections.empty()) return {};

    // Sort by confidence (descending)
    std::vector<Detection> sorted = detections;
    std::sort(sorted.begin(), sorted.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });

    std::vector<Detection> result;
    std::vector<bool> suppressed(sorted.size(), false);

    for (size_t i = 0; i < sorted.size(); ++i) {
        if (suppressed[i]) continue;

        result.push_back(sorted[i]);

        for (size_t j = i + 1; j < sorted.size(); ++j) {
            if (suppressed[j]) continue;
            if (sorted[i].class_id != sorted[j].class_id) continue;

            float xi1 = sorted[i].x;
            float yi1 = sorted[i].y;
            float xi2 = sorted[i].x + sorted[i].w;
            float yi2 = sorted[i].y + sorted[i].h;

            float xj1 = sorted[j].x;
            float yj1 = sorted[j].y;
            float xj2 = sorted[j].x + sorted[j].w;
            float yj2 = sorted[j].y + sorted[j].h;

            float inter_x1 = std::max(xi1, xj1);
            float inter_y1 = std::max(yi1, yj1);
            float inter_x2 = std::min(xi2, xj2);
            float inter_y2 = std::min(yi2, yj2);

            float inter_w = std::max(0.0f, inter_x2 - inter_x1);
            float inter_h = std::max(0.0f, inter_y2 - inter_y1);
            float inter_area = inter_w * inter_h;

            float area_i = sorted[i].w * sorted[i].h;
            float area_j = sorted[j].w * sorted[j].h;
            float union_area = area_i + area_j - inter_area;

            float iou = (union_area > 0.0f) ? inter_area / union_area : 0.0f;

            if (iou > iou_threshold) {
                suppressed[j] = true;
            }
        }
    }

    return result;
}

// ─── shutdown ─────────────────────────────────────────────────────────────────
void InferenceHalBPU::shutdown() {
    if (!initialised_) return;

    // Free output tensor memory
    for (auto& out : output_tensors_) {
        if (out.sysMem.virAddr != nullptr) {
            hbSysFreeMem(&out.sysMem);
        }
    }
    output_tensors_.clear();

    // Free input tensor memory
    if (input_tensor_.sysMem.virAddr != nullptr) {
        hbSysFreeMem(&input_tensor_.sysMem);
    }

    // Release model handles
    if (packed_dnn_handle_ != nullptr) {
        hbDNNRelease(packed_dnn_handle_);
        packed_dnn_handle_ = nullptr;
    }

    dnn_handle_ = nullptr;
    output_count_ = 0;
    initialised_ = false;

    std::cout << "[InferenceHalBPU] Shutdown complete\n";
}

} // namespace ct