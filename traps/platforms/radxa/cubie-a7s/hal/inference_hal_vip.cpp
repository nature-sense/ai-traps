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

// ──────────────────────────────────────────────────────────────────────────────
// inference_hal_vip.cpp — Vivante VIPLite NPU inference wrapper for Allwinner A527
// ──────────────────────────────────────────────────────────────────────────────

#include "inference_hal_vip.hpp"

#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <vector>
#include <chrono>
#include <cfloat>

// Vivante VIPLite / OpenVX headers
// These are provided by the Allwinner A527 SDK (libOpenVX, libOpenVXU, etc.)
// On the target system, they are installed under /usr/include/VX/ or similar.
//
// NOTE: The exact API may vary between SDK versions. The implementation below
//       uses the standard OpenVX 1.3 API with Vivante extensions for NBG
//       graph loading. If the SDK uses a different API, adjust accordingly.
//
// Reference headers:
//   #include <VX/vx.h>
//   #include <VX/vx_vendors.h>   // Vivante-specific extensions
//   #include <VX/vx_khr_nn.h>    // Neural network extensions
//
// For now, we forward-declare the types we need to avoid requiring the SDK
// headers at compile time on non-target platforms. When building for cubie-a7s,
// the SDK headers should be available.

// Forward declarations for Vivante VIPLite types
// These match the OpenVX 1.3 API used by the Vivante driver stack.
// When building on the actual target, include the real headers instead.
#ifndef VX_API_CALL
#define VX_API_CALL
#endif

// Opaque handle types (matching OpenVX 1.3)
typedef struct _vx_context*     vx_context;
typedef struct _vx_graph*       vx_graph;
typedef struct _vx_tensor*      vx_tensor;
typedef struct _vx_image*       vx_image;
typedef struct _vx_node*        vx_node;
typedef struct _vx_uint8*       vx_uint8;

// Error codes
typedef int vx_status;
#define VX_SUCCESS   0
#define VX_FAILURE   (-1)

// Tensor data types
typedef int vx_enum;
#define VX_TYPE_UINT8     0x0B
#define VX_TYPE_FLOAT32   0x0D
#define VX_DF_IMAGE_NV12  0x14

// Tensor attributes
#define VX_TENSOR_DATA_TYPE  0x30
#define VX_TENSOR_FIXED_POINT 0x31
#define VX_TENSOR_NUM_OF_DIMS 0x32
#define VX_TENSOR_DIMS       0x33
#define VX_TENSOR_DATA_SIZE  0x34

// Vivante-specific NBG graph loading extension
// This is a Vivante extension to OpenVX for loading pre-compiled NBG graphs.
// The exact function signature may vary — adjust based on SDK documentation.
typedef vx_status (VX_API_CALL *vxLoadGraphFromMemory_t)(vx_context context,
                                                          vx_graph* graph,
                                                          const void* model_data,
                                                          size_t model_size);

namespace ct {

// ─── Constants ────────────────────────────────────────────────────────────────

static constexpr int   REG_MAX       = 16;   // DFL regression channels for YOLOv8/v11
static constexpr float NMS_THRESHOLD = 0.45f;
static constexpr int   MAX_OUTPUT_BOXES = 100;

// ─── Constructor / Destructor ─────────────────────────────────────────────────

InferenceHalVIP::InferenceHalVIP() = default;

InferenceHalVIP::~InferenceHalVIP() {
    shutdown();
}

// ─── init ─────────────────────────────────────────────────────────────────────

bool InferenceHalVIP::init(const std::string& model_path, float confidence_threshold) {
    if (initialised_) {
        std::cerr << "[InferenceHalVIP] Already initialised\n";
        return true;
    }

    conf_thresh_ = confidence_threshold;

    // 1. Load the NBG model file
    std::ifstream file(model_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[InferenceHalVIP] Cannot open model file: " << model_path << "\n";
        return false;
    }

    model_size_ = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    model_data_ = std::malloc(model_size_);
    if (!model_data_) {
        std::cerr << "[InferenceHalVIP] Failed to allocate " << model_size_
                  << " bytes for model\n";
        return false;
    }

    file.read(static_cast<char*>(model_data_), static_cast<std::streamsize>(model_size_));
    file.close();

    std::cout << "[InferenceHalVIP] Loaded NBG model: " << model_path
              << " (" << model_size_ << " bytes)\n";

    // 2. Parse NBG header to determine model input dimensions
    //    NBG files have a header that contains metadata including input tensor
    //    dimensions. The exact header format is Vivante-specific.
    //
    //    Typical NBG header structure (may vary by SDK version):
    //      Offset 0: magic number (4 bytes, e.g. "NBG\0" or 0x4E424700)
    //      Offset 4: version (4 bytes)
    //      Offset 8: input width  (4 bytes, uint32)
    //      Offset 12: input height (4 bytes, uint32)
    //      Offset 16: input channels (4 bytes, uint32)
    //      Offset 20: output elements (4 bytes, uint32)
    //      ...
    //
    //    If the header format is different, adjust the offsets below.
    //    As a fallback, we default to 320x320x3 (YOLO11n input size).

    if (model_size_ >= 24) {
        const uint8_t* header = static_cast<const uint8_t*>(model_data_);

        // Check for NBG magic: "NBG\0" or "NBG1" etc.
        bool has_nbg_magic = (header[0] == 'N' && header[1] == 'B' && header[2] == 'G');

        if (has_nbg_magic) {
            // Try to read dimensions from header (Vivante NBG format)
            // These offsets are typical but may need adjustment per SDK version
            uint32_t hdr_w = (static_cast<uint32_t>(header[8])  << 0) |
                             (static_cast<uint32_t>(header[9])  << 8) |
                             (static_cast<uint32_t>(header[10]) << 16) |
                             (static_cast<uint32_t>(header[11]) << 24);
            uint32_t hdr_h = (static_cast<uint32_t>(header[12]) << 0) |
                             (static_cast<uint32_t>(header[13]) << 8) |
                             (static_cast<uint32_t>(header[14]) << 16) |
                             (static_cast<uint32_t>(header[15]) << 24);
            uint32_t hdr_c = (static_cast<uint32_t>(header[16]) << 0) |
                             (static_cast<uint32_t>(header[17]) << 8) |
                             (static_cast<uint32_t>(header[18]) << 16) |
                             (static_cast<uint32_t>(header[19]) << 24);
            uint32_t hdr_out = (static_cast<uint32_t>(header[20]) << 0) |
                               (static_cast<uint32_t>(header[21]) << 8) |
                               (static_cast<uint32_t>(header[22]) << 16) |
                               (static_cast<uint32_t>(header[23]) << 24);

            if (hdr_w > 0 && hdr_h > 0 && hdr_w <= 4096 && hdr_h <= 4096) {
                model_in_w_ = static_cast<int>(hdr_w);
                model_in_h_ = static_cast<int>(hdr_h);
                model_in_c_ = static_cast<int>(hdr_c > 0 ? hdr_c : 3);
                output_elements_ = hdr_out;
                std::cout << "[InferenceHalVIP] NBG header: "
                          << model_in_w_ << "x" << model_in_h_ << "x" << model_in_c_
                          << ", output elements=" << output_elements_ << "\n";
            }
        } else {
            std::cout << "[InferenceHalVIP] No NBG magic found, using default "
                      << "input dimensions: " << model_in_w_ << "x" << model_in_h_ << "\n";
        }
    }

    // 3. Create OpenVX context
    //    vx_context ctx = vxCreateContext();
    //    This initialises the Vivante VIPLite driver stack.
    //
    //    NOTE: The vxCreateContext() function is provided by libOpenVX.so.
    //    On the Allwinner A527, this requires the galcore kernel module to be
    //    loaded first:
    //      sudo modprobe galcore powerManagement=1
    //
    //    For now, we simulate context creation. On real hardware, replace with:
    //      vx_context_ = vxCreateContext();
    //      if (!vx_context_) { ... error ... }

    std::cout << "[InferenceHalVIP] Creating OpenVX context...\n";

    // TODO: Replace with real vxCreateContext() call when building on target
    // vx_context_ = vxCreateContext();
    // if (!vx_context_) {
    //     std::cerr << "[InferenceHalVIP] vxCreateContext() failed\n";
    //     std::cerr << "[InferenceHalVIP] Ensure galcore kernel module is loaded:\n";
    //     std::cerr << "[InferenceHalVIP]   sudo modprobe galcore powerManagement=1\n";
    //     return false;
    // }

    // 4. Load NBG graph into OpenVX
    //    Vivante provides a vendor extension to load NBG graphs from memory:
    //      vxLoadGraphFromMemory(vx_context_, &vx_graph_, model_data_, model_size_);
    //
    //    This creates a vx_graph object with all the nodes and tensors defined
    //    in the NBG model. The graph is ready for inference once loaded.
    //
    //    Alternative API (SDK-dependent):
    //      vx_graph graph = vxCreateGraph(vx_context_);
    //      vxLoadGraph(graph, model_data_, model_size_);

    std::cout << "[InferenceHalVIP] Loading NBG graph...\n";

    // TODO: Replace with real graph loading when building on target
    // vx_status status = vxLoadGraphFromMemory(vx_context_, &vx_graph_,
    //                                           model_data_, model_size_);
    // if (status != VX_SUCCESS) {
    //     std::cerr << "[InferenceHalVIP] vxLoadGraphFromMemory() failed: "
    //               << status << "\n";
    //     vxReleaseContext(vx_context_);
    //     vx_context_ = nullptr;
    //     return false;
    // }

    // 5. Query input/output tensor handles
    //    After loading the graph, we need to get handles to the input and output
    //    tensors so we can feed data and read results.
    //
    //    The exact API depends on the SDK version. Common approaches:
    //      a) Get tensor by name:
    //           vx_tensor input = vxGetTensorByName(vx_graph_, "input");
    //           vx_tensor output = vxGetTensorByName(vx_graph_, "output");
    //
    //      b) Get tensor by index:
    //           vx_tensor input = vxGetGraphInput(vx_graph_, 0);
    //           vx_tensor output = vxGetGraphOutput(vx_graph_, 0);
    //
    //      c) Query tensor attributes to determine dimensions:
    //           vx_size num_dims = 0;
    //           vxQueryTensor(output, VX_TENSOR_NUM_OF_DIMS, &num_dims, sizeof(num_dims));
    //           vx_size dims[4] = {};
    //           vxQueryTensor(output, VX_TENSOR_DIMS, dims, sizeof(dims));
    //           vx_size data_size = 0;
    //           vxQueryTensor(output, VX_TENSOR_DATA_SIZE, &data_size, sizeof(data_size));

    std::cout << "[InferenceHalVIP] Querying tensor handles...\n";

    // TODO: Replace with real tensor queries when building on target
    // vx_input_ = vxGetGraphInput(vx_graph_, 0);
    // vx_output_ = vxGetGraphOutput(vx_graph_, 0);
    // if (!vx_input_ || !vx_output_) {
    //     std::cerr << "[InferenceHalVIP] Failed to get tensor handles\n";
    //     shutdown();
    //     return false;
    // }
    //
    // // Query output tensor size
    // vx_size output_size = 0;
    // vxQueryTensor(vx_output_, VX_TENSOR_DATA_SIZE, &output_size, sizeof(output_size));
    // output_elements_ = static_cast<uint32_t>(output_size / sizeof(float));

    // If we couldn't determine output elements from the header, use a default
    if (output_elements_ == 0) {
        // Default for YOLO11n 320x320: 2100 anchors × (4*16 + 1) = 136500
        // But typically the model output is already post-processed to [N, 5] or [N, 6]
        // where N is the number of detections. We'll use a generous default.
        output_elements_ = 2100 * 65;  // Max possible for YOLO11n 320x320
        std::cout << "[InferenceHalVIP] Using default output elements: "
                  << output_elements_ << "\n";
    }

    initialised_ = true;
    std::cout << "[InferenceHalVIP] Initialised successfully ("
              << model_in_w_ << "x" << model_in_h_ << "x" << model_in_c_
              << ", conf_thresh=" << conf_thresh_ << ")\n";
    return true;
}

// ─── detect ───────────────────────────────────────────────────────────────────

std::vector<Detection> InferenceHalVIP::detect(const FrameBuffer& frame) {
    if (!initialised_) {
        std::cerr << "[InferenceHalVIP] Not initialised\n";
        return {};
    }

    auto t0 = std::chrono::steady_clock::now();

    // ── 1. Pre-process: NV12 → RGB888 resize to model input dimensions ────────
    //      The camera HAL produces NV12 frames. The NPU expects RGB888
    //      (or BGR888, depending on the model) at the model's input resolution.
    //
    //      We use CPU-based bilinear interpolation for the resize + colour
    //      conversion. If the OpenCL scaler is available, it could be used
    //      instead for GPU-accelerated preprocessing.

    std::vector<uint8_t> rgb_input(static_cast<size_t>(model_in_w_ * model_in_h_ * 3));
    nv12_to_rgb_resize(static_cast<const uint8_t*>(frame.data),
                       static_cast<int>(frame.width),
                       static_cast<int>(frame.height),
                       rgb_input.data(), model_in_w_, model_in_h_);

    auto t1 = std::chrono::steady_clock::now();

    // ── 2. Copy preprocessed data to NPU input tensor ─────────────────────────
    //      The input tensor was created during init(). We need to copy the
    //      RGB data into the tensor's memory.
    //
    //      OpenVX approach:
    //        vx_status status = vxCopyTensorData(vx_input_, rgb_input.data(),
    //                                             rgb_input.size(),
    //                                             VX_WRITE_ONLY, VX_MEMORY_TYPE_HOST);
    //
    //      Alternative (direct memory access):
    //        void* tensor_mem = nullptr;
    //        vxMapTensorData(vx_input_, &tensor_mem, VX_WRITE_ONLY,
    //                        VX_MEMORY_TYPE_HOST, 0);
    //        memcpy(tensor_mem, rgb_input.data(), rgb_input.size());
    //        vxUnmapTensorData(vx_input_, tensor_mem);

    // TODO: Replace with real tensor copy when building on target
    // vxCopyTensorData(vx_input_, rgb_input.data(), rgb_input.size(),
    //                  VX_WRITE_ONLY, VX_MEMORY_TYPE_HOST);

    auto t2 = std::chrono::steady_clock::now();

    // ── 3. Run inference ──────────────────────────────────────────────────────
    //      vxProcessGraph(vx_graph_) executes the entire NBG graph on the NPU.
    //      This is a blocking call — it returns when inference is complete.
    //
    //      For asynchronous execution (pipelining), use:
    //        vxScheduleGraph(vx_graph_);
    //        vxWaitGraph(vx_graph_);

    // TODO: Replace with real graph execution when building on target
    // vx_status status = vxProcessGraph(vx_graph_);
    // if (status != VX_SUCCESS) {
    //     std::cerr << "[InferenceHalVIP] vxProcessGraph() failed: " << status << "\n";
    //     return {};
    // }

    auto t3 = std::chrono::steady_clock::now();

    // ── 4. Read output tensor ─────────────────────────────────────────────────
    //      After inference, the output tensor contains the raw model predictions.
    //      We copy it to a host buffer for post-processing.
    //
    //      OpenVX approach:
    //        std::vector<float> output_data(output_elements_);
    //        vxCopyTensorData(vx_output_, output_data.data(),
    //                         output_elements_ * sizeof(float),
    //                         VX_READ_ONLY, VX_MEMORY_TYPE_HOST);

    // TODO: Replace with real tensor read when building on target
    // For now, simulate with empty output (no detections)
    std::vector<float> output_data(output_elements_, 0.0f);

    auto t4 = std::chrono::steady_clock::now();

    // ── 5. Post-process: decode YOLO detections ───────────────────────────────
    std::vector<Detection> detections = decode_yolo_dfl(
        output_data.data(), output_elements_,
        model_in_w_, model_in_h_, conf_thresh_);

    // ── 6. NMS ────────────────────────────────────────────────────────────────
    nms(detections, NMS_THRESHOLD);

    auto t5 = std::chrono::steady_clock::now();

    // ── Timing ────────────────────────────────────────────────────────────────
    last_inference_us_ = std::chrono::duration_cast<std::chrono::microseconds>(
                             t5 - t0).count();

    static int log_cnt = 0;
    if (++log_cnt <= 10) {
        auto preproc_us  = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        auto copy_us     = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
        auto infer_us    = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
        auto read_us     = std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count();
        auto postproc_us = std::chrono::duration_cast<std::chrono::microseconds>(t5 - t4).count();

        std::cout << "[InferenceHalVIP] " << (last_inference_us_ / 1000.0f) << "ms total"
                  << " (preproc=" << (preproc_us / 1000.0f) << "ms"
                  << ", copy=" << (copy_us / 1000.0f) << "ms"
                  << ", infer=" << (infer_us / 1000.0f) << "ms"
                  << ", read=" << (read_us / 1000.0f) << "ms"
                  << ", postproc=" << (postproc_us / 1000.0f) << "ms)"
                  << ", detections=" << detections.size() << "\n";
    }

    return detections;
}

// ─── shutdown ─────────────────────────────────────────────────────────────────

void InferenceHalVIP::shutdown() {
    if (!initialised_ && !vx_context_) return;

    // Release VIPLite / OpenVX resources
    // Order matters: release tensors → graph → context

    // TODO: Replace with real OpenVX release calls when building on target
    // if (vx_output_) {
    //     vxReleaseTensor(&vx_output_);
    //     vx_output_ = nullptr;
    // }
    // if (vx_input_) {
    //     vxReleaseTensor(&vx_input_);
    //     vx_input_ = nullptr;
    // }
    // if (vx_graph_) {
    //     vxReleaseGraph(&vx_graph_);
    //     vx_graph_ = nullptr;
    // }
    // if (vx_context_) {
    //     vxReleaseContext(&vx_context_);
    //     vx_context_ = nullptr;
    // }

    vx_context_ = nullptr;
    vx_graph_   = nullptr;
    vx_input_   = nullptr;
    vx_output_  = nullptr;

    if (model_data_) {
        std::free(model_data_);
        model_data_ = nullptr;
        model_size_ = 0;
    }

    initialised_ = false;
    std::cout << "[InferenceHalVIP] Shutdown complete\n";
}

// ─── NV12 → RGB888 bilinear resize (static) ───────────────────────────────────
// Converts an NV12 frame to RGB888 at the target resolution using bilinear
// interpolation. This is the CPU fallback when the OpenCL scaler is unavailable.
//
// NV12 layout:
//   - Y plane: src_w × src_h bytes (luma)
//   - UV plane: (src_w/2) × (src_h/2) × 2 bytes (interleaved chroma)
//
// RGB888 output: dst_w × dst_h × 3 bytes (BGR order for NPU compatibility)
void InferenceHalVIP::nv12_to_rgb_resize(const uint8_t* src, int src_w, int src_h,
                                          uint8_t* dst, int dst_w, int dst_h) {
    const float scale_x = static_cast<float>(src_w) / static_cast<float>(dst_w);
    const float scale_y = static_cast<float>(src_h) / static_cast<float>(dst_h);

    const uint8_t* y_plane  = src;
    const uint8_t* uv_plane = src + src_w * src_h;

    for (int dy = 0; dy < dst_h; ++dy) {
        for (int dx = 0; dx < dst_w; ++dx) {
            // Map destination pixel to source coordinates (centre-aligned)
            float sx = (dx + 0.5f) * scale_x - 0.5f;
            float sy = (dy + 0.5f) * scale_y - 0.5f;

            int ix = static_cast<int>(std::floor(sx));
            int iy = static_cast<int>(std::floor(sy));
            ix = std::max(0, std::min(src_w - 2, ix));
            iy = std::max(0, std::min(src_h - 2, iy));

            float fx = sx - ix;
            float fy = sy - iy;

            // Bilinear Y
            float y_val =
                (1 - fx) * (1 - fy) * y_plane[iy * src_w + ix] +
                fx       * (1 - fy) * y_plane[iy * src_w + ix + 1] +
                (1 - fx) * fy       * y_plane[(iy + 1) * src_w + ix] +
                fx       * fy       * y_plane[(iy + 1) * src_w + ix + 1];

            // Nearest-neighbour UV (bilinear UV is expensive and rarely needed)
            int uv_x = ix / 2;
            int uv_y = iy / 2;
            uv_x = std::min(uv_x, src_w / 2 - 1);
            uv_y = std::min(uv_y, src_h / 2 - 1);

            float u_val = static_cast<float>(uv_plane[uv_y * src_w + uv_x * 2]) - 128.0f;
            float v_val = static_cast<float>(uv_plane[uv_y * src_w + uv_x * 2 + 1]) - 128.0f;

            // BT.601 limited range YUV → RGB
            int r = static_cast<int>(y_val + 1.402f * v_val);
            int g = static_cast<int>(y_val - 0.344f * u_val - 0.714f * v_val);
            int b = static_cast<int>(y_val + 1.772f * u_val);

            r = std::max(0, std::min(255, r));
            g = std::max(0, std::min(255, g));
            b = std::max(0, std::min(255, b));

            // BGR output (common for NPU models)
            int dst_idx = (dy * dst_w + dx) * 3;
            dst[dst_idx + 0] = static_cast<uint8_t>(b);
            dst[dst_idx + 1] = static_cast<uint8_t>(g);
            dst[dst_idx + 2] = static_cast<uint8_t>(r);
        }
    }
}

// ─── decode_yolo_dfl (static) ─────────────────────────────────────────────────
// Decodes YOLOv8/v11 DFL (Distribution Focal Loss) output format into
// Detection structs.
//
// The model output is expected to be a flattened tensor with shape:
//   [1, num_anchors, 4*REG_MAX + num_classes]
//
// Where:
//   - REG_MAX = 16 (DFL regression channels per bbox edge)
//   - num_classes = number of detection classes (e.g., 1 for person-only)
//   - num_anchors = total grid anchors across all scales
//
// For YOLO11n 320×320:
//   - 3 scales: 40×40 (1600), 20×20 (400), 10×10 (100) = 2100 anchors
//   - 65 channels per anchor (4×16 DFL + 1 class)
//   - Total output elements: 2100 × 65 = 136500
//
// If the model output is already post-processed (e.g., [N, 5] format with
// x1, y1, x2, y2, confidence), this function handles that format too.
std::vector<Detection> InferenceHalVIP::decode_yolo_dfl(const float* output_data,
                                                         uint32_t output_elements,
                                                         int model_w, int model_h,
                                                         float conf_thresh) {
    std::vector<Detection> detections;
    detections.reserve(MAX_OUTPUT_BOXES);

    if (!output_data || output_elements == 0) {
        return detections;
    }

    // Try to determine the output format from the number of elements
    // Common formats:
    //   [N, 5]  — pre-decoded: x1, y1, x2, y2, confidence (N detections)
    //   [N, 6]  — pre-decoded: x1, y1, x2, y2, confidence, class_id
    //   [A, C]  — raw DFL: A anchors × C channels (C = 4*16 + num_classes)

    // Check for pre-decoded format first
    if (output_elements % 5 == 0 && output_elements / 5 <= MAX_OUTPUT_BOXES) {
        // [N, 5] format: x1, y1, x2, y2, confidence
        uint32_t n_dets = output_elements / 5;
        const float inv_w = 1.0f / model_w;
        const float inv_h = 1.0f / model_h;

        for (uint32_t i = 0; i < n_dets; ++i) {
            float x1 = output_data[i * 5];
            float y1 = output_data[i * 5 + 1];
            float x2 = output_data[i * 5 + 2];
            float y2 = output_data[i * 5 + 3];
            float conf = output_data[i * 5 + 4];

            if (conf < conf_thresh) continue;

            Detection det;
            det.x = std::min(x1, x2) * inv_w;
            det.y = std::min(y1, y2) * inv_h;
            det.w = std::abs(x2 - x1) * inv_w;
            det.h = std::abs(y2 - y1) * inv_h;
            det.confidence = conf;
            det.class_id = 0;
            detections.push_back(det);
        }
        return detections;
    }

    if (output_elements % 6 == 0 && output_elements / 6 <= MAX_OUTPUT_BOXES) {
        // [N, 6] format: x1, y1, x2, y2, confidence, class_id
        uint32_t n_dets = output_elements / 6;
        const float inv_w = 1.0f / model_w;
        const float inv_h = 1.0f / model_h;

        for (uint32_t i = 0; i < n_dets; ++i) {
            float x1 = output_data[i * 6];
            float y1 = output_data[i * 6 + 1];
            float x2 = output_data[i * 6 + 2];
            float y2 = output_data[i * 6 + 3];
            float conf = output_data[i * 6 + 4];
            int cls = static_cast<int>(output_data[i * 6 + 5]);

            if (conf < conf_thresh) continue;

            Detection det;
            det.x = std::min(x1, x2) * inv_w;
            det.y = std::min(y1, y2) * inv_h;
            det.w = std::abs(x2 - x1) * inv_w;
            det.h = std::abs(y2 - y1) * inv_h;
            det.confidence = conf;
            det.class_id = cls;
            detections.push_back(det);
        }
        return detections;
    }

    // Raw DFL format: [A, C] where C = 4*REG_MAX + num_classes
    // Try to determine num_classes from the channel count
    // Common values: 65 (4*16 + 1), 84 (4*16 + 20), 85 (4*16 + 21)
    int n_channels = 0;
    int n_anchors  = 0;

    // Try common YOLO channel configurations
    for (int channels : {65, 84, 85, 117, 117}) {
        if (output_elements % channels == 0) {
            n_channels = channels;
            n_anchors  = static_cast<int>(output_elements) / channels;
            break;
        }
    }

    if (n_channels == 0) {
        // Unknown format — try to infer from common anchor counts
        // YOLO11n 320×320: 2100 anchors
        // YOLO11n 640×640: 8400 anchors
        for (int anchors : {2100, 8400, 3549, 6300}) {
            if (output_elements % anchors == 0) {
                n_anchors  = anchors;
                n_channels = static_cast<int>(output_elements) / anchors;
                break;
            }
        }
    }

    if (n_channels == 0 || n_anchors == 0) {
        std::cerr << "[InferenceHalVIP] Unknown output format: "
                  << output_elements << " elements\n";
        return detections;
    }

    const int num_classes = n_channels - 4 * REG_MAX;
    if (num_classes <= 0) {
        std::cerr << "[InferenceHalVIP] Invalid channel count: " << n_channels
                  << " (num_classes=" << num_classes << ")\n";
        return detections;
    }

    // Decode DFL for each anchor
    for (int a = 0; a < n_anchors; ++a) {
        // Find the best class score (sigmoid of class logits)
        float max_cls_score = -FLT_MAX;
        int class_id = 0;

        for (int c = 0; c < num_classes; ++c) {
            float score = output_data[a + (4 * REG_MAX + c) * n_anchors];
            if (score > max_cls_score) {
                max_cls_score = score;
                class_id = c;
            }
        }

        // Sigmoid activation for class confidence
        float cls_conf = 1.0f / (1.0f + std::exp(-max_cls_score));
        if (cls_conf < conf_thresh) continue;

        // DFL decode: softmax over each of the 4 regression groups
        float dfl[4] = {0, 0, 0, 0};
        for (int g = 0; g < 4; ++g) {
            // Find max for numerical stability (softmax)
            float max_val = -FLT_MAX;
            for (int c = 0; c < REG_MAX; ++c) {
                float v = output_data[a + (g * REG_MAX + c) * n_anchors];
                if (v > max_val) max_val = v;
            }

            // Softmax + weighted sum
            float sum = 0;
            for (int c = 0; c < REG_MAX; ++c) {
                float v = output_data[a + (g * REG_MAX + c) * n_anchors];
                float e = std::exp(v - max_val);
                dfl[g] += c * e;
                sum += e;
            }
            dfl[g] /= sum;
        }

        // Determine anchor centre and stride based on grid position
        // YOLO11 uses 3 scales: stride 8, 16, 32
        float cx, cy, stride;
        if (a < 1600) {
            // Stride 8: 40×40 grid (320/8 = 40)
            stride = 8.0f;
            cx = (a % 40 + 0.5f) * stride;
            cy = (a / 40 + 0.5f) * stride;
        } else if (a < 2000) {
            // Stride 16: 20×20 grid (320/16 = 20)
            stride = 16.0f;
            int a_idx = a - 1600;
            cx = (a_idx % 20 + 0.5f) * stride;
            cy = (a_idx / 20 + 0.5f) * stride;
        } else {
            // Stride 32: 10×10 grid (320/32 = 10)
            stride = 32.0f;
            int a_idx = a - 2000;
            cx = (a_idx % 10 + 0.5f) * stride;
            cy = (a_idx / 10 + 0.5f) * stride;
        }

        // Convert DFL offsets to bounding box in normalised coordinates
        float x1 = (cx - dfl[0] * stride) / model_w;
        float y1 = (cy - dfl[1] * stride) / model_h;
        float x2 = (cx + dfl[2] * stride) / model_w;
        float y2 = (cy + dfl[3] * stride) / model_h;

        Detection det;
        det.x = std::max(0.0f, std::min(1.0f, x1));
        det.y = std::max(0.0f, std::min(1.0f, y1));
        det.w = std::max(0.0f, std::min(1.0f, x2 - x1));
        det.h = std::max(0.0f, std::min(1.0f, y2 - y1));
        det.confidence = cls_conf;
        det.class_id = class_id;
        detections.push_back(det);
    }

    return detections;
}

// ─── NMS (static) ─────────────────────────────────────────────────────────────
// Non-maximum suppression using IoU threshold.
// Sorts detections by confidence descending, then greedily selects boxes
// that don't overlap more than iou_thresh with any higher-confidence box.
void InferenceHalVIP::nms(std::vector<Detection>& dets, float iou_thresh) {
    if (dets.empty()) return;

    // Sort by confidence descending
    std::sort(dets.begin(), dets.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });

    std::vector<bool> keep(dets.size(), true);

    for (size_t i = 0; i < dets.size(); ++i) {
        if (!keep[i]) continue;
        for (size_t j = i + 1; j < dets.size(); ++j) {
            if (!keep[j]) continue;

            // Compute IoU
            float ix1 = std::max(dets[i].x, dets[j].x);
            float iy1 = std::max(dets[i].y, dets[j].y);
            float ix2 = std::min(dets[i].x + dets[i].w, dets[j].x + dets[j].w);
            float iy2 = std::min(dets[i].y + dets[i].h, dets[j].y + dets[j].h);

            float iw = std::max(0.0f, ix2 - ix1);
            float ih = std::max(0.0f, iy2 - iy1);
            float inter = iw * ih;

            float area_i = dets[i].w * dets[i].h;
            float area_j = dets[j].w * dets[j].h;
            float union_area = area_i + area_j - inter;

            float iou = (union_area > 0) ? inter / union_area : 0;

            if (iou > iou_thresh) {
                keep[j] = false;
            }
        }
    }

    // Collect kept detections
    std::vector<Detection> result;
    result.reserve(dets.size());
    for (size_t i = 0; i < dets.size(); ++i) {
        if (keep[i]) result.push_back(dets[i]);
    }
    dets = std::move(result);
}

} // namespace ct
