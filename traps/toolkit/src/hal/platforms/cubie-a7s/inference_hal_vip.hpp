#pragma once

#include "hal/api/inference_hal.hpp"

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

namespace ct {

// ─── InferenceHalVIP ──────────────────────────────────────────────────────────
// IInferenceHAL implementation for the Vivante VIPLite NPU (Allwinner A527).
//
// Uses the Vivante VIPLite driver stack:
//   - OpenVX (libOpenVX) for graph-based vision processing
//   - OpenVXU (libOpenVXU) for utility extensions
//   - CLC (libCLC) for OpenCL C kernel compilation
//   - VSC (libVSC) for shader compilation
//   - GAL (libGAL) for GPU abstraction layer
//
// Model format: NBG (Neural Network Binary Graph), converted from ONNX/PyTorch
// via the ACUITY Toolkit. INT8 quantization is strongly recommended for
// memory efficiency (4x reduction vs FP32).
//
// Power management: The galcore kernel module should be loaded with
// `powerManagement=1` to allow the NPU to sleep between detections.
//
// Inference pipeline:
//   1. NV12 frame → RGB888 resize to model input dimensions (CPU bilinear)
//   2. Copy RGB data to NPU input tensor
//   3. vxProcessGraph() to run inference
//   4. Read output tensor and decode YOLO detections (DFL + NMS)
//
// NOTE: This implementation requires the Vivante VIPLite userspace libraries
//       (libOpenVX.so, libOpenVXU.so, libCLC.so, libVSC.so, libGAL.so) and
//       the galcore kernel module to be loaded on the target system.
struct InferenceHalVIP : IInferenceHAL {
    InferenceHalVIP();
    ~InferenceHalVIP() override;

    // Non-copyable, non-movable
    InferenceHalVIP(const InferenceHalVIP&) = delete;
    InferenceHalVIP& operator=(const InferenceHalVIP&) = delete;

    // ── IInferenceHAL interface ────────────────────────────────────────────────
    bool init(const std::string& model_path, float confidence_threshold) override;
    std::vector<Detection> detect(const FrameBuffer& frame) override;

    int64_t last_inference_us() const override { return last_inference_us_; }
    uint32_t input_width() const override { return static_cast<uint32_t>(model_in_w_); }
    uint32_t input_height() const override { return static_cast<uint32_t>(model_in_h_); }

    void shutdown() override;

private:
    // ── Pre-processing ─────────────────────────────────────────────────────────
    // NV12 → RGB888 bilinear resize (CPU, used when OpenCL scaler unavailable)
    static void nv12_to_rgb_resize(const uint8_t* src, int src_w, int src_h,
                                    uint8_t* dst, int dst_w, int dst_h);

    // ── Post-processing ────────────────────────────────────────────────────────
    // Decode YOLOv8/v11 DFL output format into Detection structs
    static std::vector<Detection> decode_yolo_dfl(const float* output_data,
                                                   uint32_t output_elements,
                                                   int model_w, int model_h,
                                                   float conf_thresh);

    // Non-maximum suppression
    static void nms(std::vector<Detection>& dets, float iou_thresh);

    // ── State ─────────────────────────────────────────────────────────────────
    // VIPLite / OpenVX handles (opaque pointers)
    void* vx_context_ = nullptr;   // vx_context
    void* vx_graph_   = nullptr;   // vx_graph
    void* vx_input_   = nullptr;   // vx_tensor (input)
    void* vx_output_  = nullptr;   // vx_tensor (output)

    // Model data (loaded from .nbg file)
    void*  model_data_ = nullptr;
    size_t model_size_ = 0;

    // Model input dimensions (from NBG header or config)
    int model_in_w_ = 320;
    int model_in_h_ = 320;
    int model_in_c_ = 3;

    // Output dimensions (queried from graph)
    uint32_t output_elements_ = 0;

    float conf_thresh_ = 0.5f;

    bool initialised_ = false;

    // Timing
    int64_t last_inference_us_ = 0;
};

} // namespace ct
