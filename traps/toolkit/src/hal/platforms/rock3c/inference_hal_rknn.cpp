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
// inference_hal_rknn.cpp — RKNN inference wrapper for RK3566 NPU
// ──────────────────────────────────────────────────────────────────────────────

#include "inference_hal_rknn.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <vector>
#include <chrono>
#include <cfloat>

extern "C" {
#include <rknn_api.h>
}

namespace ct {

// ─── Constants ────────────────────────────────────────────────────────────────

static constexpr int   REG_MAX       = 16;  // DFL regression channels for YOLOv8/v11
static constexpr float NMS_THRESHOLD = 0.45f;
static constexpr int   MAX_OUTPUT_BOXES = 100;

// ─── Constructor / Destructor ────────────────────────────────────────────────

InferenceHalRKNN::InferenceHalRKNN()  = default;
InferenceHalRKNN::~InferenceHalRKNN() { shutdown(); }

// ─── init ─────────────────────────────────────────────────────────────────────

bool InferenceHalRKNN::init(const std::string& model_path, float conf_thresh) {
    if (initialised_) {
        std::cerr << "[InferenceHalRKNN] already initialised\n";
        return false;
    }

    conf_thresh_ = conf_thresh;

    // ── Load model ───────────────────────────────────────────────────────────
    FILE* fp = fopen(model_path.c_str(), "rb");
    if (!fp) {
        std::cerr << "[InferenceHalRKNN] cannot open model: " << model_path << "\n";
        return false;
    }
    fseek(fp, 0, SEEK_END);
    model_size_ = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    model_data_ = malloc(model_size_);
    if (!model_data_) {
        std::cerr << "[InferenceHalRKNN] malloc(" << model_size_ << ") failed\n";
        fclose(fp);
        return false;
    }
    size_t nread = fread(model_data_, 1, model_size_, fp);
    fclose(fp);
    if (nread != model_size_) {
        std::cerr << "[InferenceHalRKNN] short read: " << nread << " / " << model_size_ << "\n";
        free(model_data_);
        model_data_ = nullptr;
        return false;
    }

    // ── Initialise RKNN context ──────────────────────────────────────────────
    int ret = rknn_init(&ctx_, model_data_, model_size_, 0, nullptr);
    if (ret < 0) {
        std::cerr << "[InferenceHalRKNN] rknn_init failed: " << ret << "\n";
        free(model_data_);
        model_data_ = nullptr;
        return false;
    }
    std::cout << "[InferenceHalRKNN] rknn_init OK\n";

    // ── Query input/output info ──────────────────────────────────────────────
    ret = rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num_, sizeof(io_num_));
    if (ret < 0) {
        std::cerr << "[InferenceHalRKNN] rknn_query(IN_OUT_NUM) failed: " << ret << "\n";
        shutdown();
        return false;
    }
    std::cout << "[InferenceHalRKNN] n_input=" << io_num_.n_input
              << " n_output=" << io_num_.n_output << "\n";

    // ── Query input attributes ───────────────────────────────────────────────
    std::vector<rknn_tensor_attr> input_attrs(io_num_.n_input);
    for (uint32_t i = 0; i < io_num_.n_input; ++i) {
        input_attrs[i].index = i;
        ret = rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &input_attrs[i],
                         sizeof(rknn_tensor_attr));
        if (ret < 0) {
            std::cerr << "[InferenceHalRKNN] rknn_query(INPUT_ATTR)[" << i
                      << "] failed: " << ret << "\n";
            shutdown();
            return false;
        }
    }

    // ── Store model dimensions ───────────────────────────────────────────────
    // Assume input 0 is the image input
    if (input_attrs[0].fmt == RKNN_TENSOR_NHWC) {
        model_in_h_ = input_attrs[0].dims[1];
        model_in_w_ = input_attrs[0].dims[2];
        model_in_c_ = input_attrs[0].dims[3];
    } else {
        model_in_c_ = input_attrs[0].dims[1];
        model_in_h_ = input_attrs[0].dims[2];
        model_in_w_ = input_attrs[0].dims[3];
    }
    std::cout << "[InferenceHalRKNN] model input: " << model_in_w_ << "x"
              << model_in_h_ << "x" << model_in_c_ << "\n";

    // ── Set input tensor attributes ──────────────────────────────────────────
    input_.index = 0;
    input_.type  = RKNN_TENSOR_UINT8;
    input_.fmt   = RKNN_TENSOR_NHWC;
    input_.size  = model_in_w_ * model_in_h_ * model_in_c_ * sizeof(uint8_t);
    input_.buf   = nullptr;  // set per-inference

    initialised_ = true;
    std::cout << "[InferenceHalRKNN] ready (conf_thresh=" << conf_thresh_ << ")\n";
    return true;
}

// ─── detect ───────────────────────────────────────────────────────────────────

std::vector<Detection> InferenceHalRKNN::detect(const FrameBuffer& frame) {
    if (!initialised_) {
        std::cerr << "[InferenceHalRKNN] not initialised\n";
        return {};
    }

    auto t0 = std::chrono::steady_clock::now();

    // ── 1. Resize and convert NV12 → RGB (NHWC uint8) ───────────────────────
    std::vector<uint8_t> rgb_input(model_in_w_ * model_in_h_ * 3);
    nv12_to_rgb_resize(static_cast<const uint8_t*>(frame.data),
                       static_cast<int>(frame.width),
                       static_cast<int>(frame.height),
                       rgb_input.data(), model_in_w_, model_in_h_);

    auto t1 = std::chrono::steady_clock::now();

    // ── 2. Set input buffer ──────────────────────────────────────────────────
    input_.buf = rgb_input.data();
    rknn_inputs_set(ctx_, io_num_.n_input, &input_);

    // ── 3. Run inference ─────────────────────────────────────────────────────
    int ret = rknn_run(ctx_, nullptr);
    if (ret < 0) {
        std::cerr << "[InferenceHalRKNN] rknn_run failed: " << ret << "\n";
        return {};
    }

    auto t2 = std::chrono::steady_clock::now();

    // ── 4. Get outputs ───────────────────────────────────────────────────────
    std::vector<rknn_output> outputs(io_num_.n_output);
    for (uint32_t i = 0; i < io_num_.n_output; ++i) {
        outputs[i].index = i;
        outputs[i].want_float = 1;
    }

    ret = rknn_outputs_get(ctx_, io_num_.n_output, outputs.data(), nullptr);
    if (ret < 0) {
        std::cerr << "[InferenceHalRKNN] rknn_outputs_get failed: " << ret << "\n";
        return {};
    }

    auto t3 = std::chrono::steady_clock::now();

    // ── 5. Post-process ──────────────────────────────────────────────────────
    // Query output attributes to decide decoding strategy
    std::vector<rknn_tensor_attr> out_attrs(io_num_.n_output);
    for (uint32_t i = 0; i < io_num_.n_output; ++i) {
        out_attrs[i].index = i;
        rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &out_attrs[i], sizeof(rknn_tensor_attr));
    }

    std::vector<Detection> detections;
    detections.reserve(MAX_OUTPUT_BOXES);

    const auto& attr = out_attrs[0];
    float* output_data = static_cast<float*>(outputs[0].buf);
    uint32_t output_elements = outputs[0].size / sizeof(float);

    // Flexible decoding logic
    // Determine number of channels and anchors
    int n_channels = 0;
    int n_anchors  = 0;

    if (attr.n_dims == 3) {
        // [1, C, N] or [1, N, C]
        if (attr.dims[1] < attr.dims[2]) {
            n_channels = attr.dims[1];
            n_anchors  = attr.dims[2];
        } else {
            n_channels = attr.dims[2];
            n_anchors  = attr.dims[1];
        }
    } else if (attr.n_dims == 2) {
        // [1, N*C]
        // This is harder, assume YOLO11 default anchors if we can't tell
        // For YOLO11n 320x320, it's often 2100 anchors.
        // Let's try to infer from common YOLO channel counts.
        // 4*16 (DFL) + 1 (class) = 65
        if (output_elements % 65 == 0) {
            n_channels = 65;
            n_anchors = output_elements / 65;
        } else if (output_elements % 5 == 0) {
            n_channels = 5;
            n_anchors = output_elements / 5;
        }
    }

    if (n_channels == 5) {
        // Pre-decoded format: [x1, y1, x2, y2, confidence] (pixel coordinates)
        const float inv_w = 1.0f / model_in_w_;
        const float inv_h = 1.0f / model_in_h_;

        for (int i = 0; i < n_anchors; ++i) {
            float x1, y1, x2, y2, conf;
            if (attr.dims[1] == 5) { // [1, 5, N]
                x1 = output_data[i];
                y1 = output_data[i + n_anchors];
                x2 = output_data[i + 2 * n_anchors];
                y2 = output_data[i + 3 * n_anchors];
                conf = output_data[i + 4 * n_anchors];
            } else { // [1, N, 5]
                x1 = output_data[i * 5];
                y1 = output_data[i * 5 + 1];
                x2 = output_data[i * 5 + 2];
                y2 = output_data[i * 5 + 3];
                conf = output_data[i * 5 + 4];
            }

            if (conf < conf_thresh_) continue;

            float box_x = std::min(x1, x2);
            float box_y = std::min(y1, y2);
            float box_w = std::abs(x2 - x1);
            float box_h = std::abs(y2 - y1);

            Detection det;
            det.x = box_x * inv_w;
            det.y = box_y * inv_h;
            det.w = box_w * inv_w;
            det.h = box_h * inv_h;
            det.confidence = conf;
            det.class_id = 0;
            detections.push_back(det);
        }
    } else if (n_channels >= 65) {
        // Raw YOLOv8/v11 DFL format: [4*REG_MAX + num_classes, num_anchors]
        const int num_classes = n_channels - 4 * REG_MAX;
        
        for (int a = 0; a < n_anchors; ++a) {
            // Class scores (multi-class support)
            float max_cls_score = -FLT_MAX;
            int class_id = 0;
            
            for (int c = 0; c < num_classes; ++c) {
                float score = output_data[a + (4 * REG_MAX + c) * n_anchors];
                if (score > max_cls_score) {
                    max_cls_score = score;
                    class_id = c;
                }
            }
            
            float cls_conf = 1.0f / (1.0f + std::exp(-max_cls_score));
            if (cls_conf < conf_thresh_) continue;

            // DFL decode
            float dfl[4] = {0, 0, 0, 0};
            for (int g = 0; g < 4; ++g) {
                float max_val = -FLT_MAX;
                for (int c = 0; c < REG_MAX; ++c) {
                    float v = output_data[a + (g * REG_MAX + c) * n_anchors];
                    if (v > max_val) max_val = v;
                }
                float sum = 0;
                for (int c = 0; c < REG_MAX; ++c) {
                    float v = output_data[a + (g * REG_MAX + c) * n_anchors];
                    float e = std::exp(v - max_val);
                    dfl[g] += c * e;
                    sum += e;
                }
                dfl[g] /= sum;
            }

            // Anchors for YOLOv11 usually follow a grid. 
            // For simplicity, we assume the model output is flattened and 
            // the anchors match the grid scale (8, 16, 32).
            // This part is model-specific. For a generic HAL, it's better if 
            // the model includes decoding layers.
            // But if we know it's YOLOv11n 320x320 flattened:
            // 80x80/4 + 40x40/4 + 20x20/4 ? No, usually 3 scales.
            // 320/8 = 40. 40x40 = 1600.
            // 320/16 = 20. 20x20 = 400.
            // 320/32 = 10. 10x10 = 100.
            // Total = 1600 + 400 + 100 = 2100 anchors.
            
            float cx, cy, stride;
            if (a < 1600) {
                stride = 8.0f;
                cx = (a % 40 + 0.5f) * stride;
                cy = (a / 40 + 0.5f) * stride;
            } else if (a < 2000) {
                stride = 16.0f;
                int a_idx = a - 1600;
                cx = (a_idx % 20 + 0.5f) * stride;
                cy = (a_idx / 20 + 0.5f) * stride;
            } else {
                stride = 32.0f;
                int a_idx = a - 2000;
                cx = (a_idx % 10 + 0.5f) * stride;
                cy = (a_idx / 10 + 0.5f) * stride;
            }

            float x1 = (cx - dfl[0] * stride) / model_in_w_;
            float y1 = (cy - dfl[1] * stride) / model_in_h_;
            float x2 = (cx + dfl[2] * stride) / model_in_w_;
            float y2 = (cy + dfl[3] * stride) / model_in_h_;

            Detection det;
            det.x = std::max(0.0f, std::min(1.0f, x1));
            det.y = std::max(0.0f, std::min(1.0f, y1));
            det.w = std::max(0.0f, std::min(1.0f, x2 - x1));
            det.h = std::max(0.0f, std::min(1.0f, y2 - y1));
            det.confidence = cls_conf;
            det.class_id = class_id;
            detections.push_back(det);
        }
    }

    // ── 6. NMS ───────────────────────────────────────────────────────────────
    nms(detections, NMS_THRESHOLD);

    auto t4 = std::chrono::steady_clock::now();

    // ── 7. Release outputs ───────────────────────────────────────────────────
    rknn_outputs_release(ctx_, io_num_.n_output, outputs.data());

    // ── Timing ───────────────────────────────────────────────────────────────
    last_inference_us_ = std::chrono::duration_cast<std::chrono::microseconds>(
                             t4 - t0).count();

    static int log_cnt = 0;
    if (++log_cnt <= 10) {
        std::cout << "[InferenceHalRKNN] " << (last_inference_us_/1000.0f) << "ms, "
                  << "detections=" << detections.size() << "\n";
    }

    return detections;
}

// ─── shutdown ─────────────────────────────────────────────────────────────────

void InferenceHalRKNN::shutdown() {
    if (ctx_) {
        rknn_destroy(ctx_);
        ctx_ = 0;
    }
    if (model_data_) {
        free(model_data_);
        model_data_ = nullptr;
    }
    initialised_ = false;
    std::cout << "[InferenceHalRKNN] shutdown\n";
}

// ─── NV12 → RGB resize ───────────────────────────────────────────────────────

void InferenceHalRKNN::nv12_to_rgb_resize(const uint8_t* src, int src_w, int src_h,
                                           uint8_t* dst, int dst_w, int dst_h) {
    const float scale_x = static_cast<float>(src_w) / dst_w;
    const float scale_y = static_cast<float>(src_h) / dst_h;

    for (int dy = 0; dy < dst_h; ++dy) {
        for (int dx = 0; dx < dst_w; ++dx) {
            float sx = (dx + 0.5f) * scale_x - 0.5f;
            float sy = (dy + 0.5f) * scale_y - 0.5f;

            int ix = static_cast<int>(std::floor(sx));
            int iy = static_cast<int>(std::floor(sy));
            ix = std::max(0, std::min(src_w - 2, ix));
            iy = std::max(0, std::min(src_h - 2, iy));

            float fx = sx - ix;
            float fy = sy - iy;

            const uint8_t* y_plane = src;
            const uint8_t* uv_plane = src + src_w * src_h;

            auto get_y = [&](int x, int y) -> float {
                return y_plane[y * src_w + x];
            };
            auto get_uv = [&](int x, int y, int comp) -> float {
                int uv_idx = (y / 2) * src_w + (x / 2) * 2 + comp;
                return uv_plane[uv_idx];
            };

            float y_val =
                (1 - fx) * (1 - fy) * get_y(ix, iy) +
                fx       * (1 - fy) * get_y(ix + 1, iy) +
                (1 - fx) * fy       * get_y(ix, iy + 1) +
                fx       * fy       * get_y(ix + 1, iy + 1);

            float u_val = get_uv(ix, iy, 0) - 128.0f;
            float v_val = get_uv(ix, iy, 1) - 128.0f;

            int r = static_cast<int>(y_val + 1.402f * v_val);
            int g = static_cast<int>(y_val - 0.344f * u_val - 0.714f * v_val);
            int b = static_cast<int>(y_val + 1.772f * u_val);

            r = std::max(0, std::min(255, r));
            g = std::max(0, std::min(255, g));
            b = std::max(0, std::min(255, b));

            int dst_idx = (dy * dst_w + dx) * 3;
            dst[dst_idx + 0] = static_cast<uint8_t>(r);
            dst[dst_idx + 1] = static_cast<uint8_t>(g);
            dst[dst_idx + 2] = static_cast<uint8_t>(b);
        }
    }
}

// ─── NMS ──────────────────────────────────────────────────────────────────────

void InferenceHalRKNN::nms(std::vector<Detection>& dets, float iou_thresh) {
    if (dets.empty()) return;

    std::sort(dets.begin(), dets.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });

    std::vector<bool> keep(dets.size(), true);

    for (size_t i = 0; i < dets.size(); ++i) {
        if (!keep[i]) continue;
        for (size_t j = i + 1; j < dets.size(); ++j) {
            if (!keep[j]) continue;

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

    std::vector<Detection> result;
    result.reserve(dets.size());
    for (size_t i = 0; i < dets.size(); ++i) {
        if (keep[i]) result.push_back(dets[i]);
    }
    dets = std::move(result);
}

} // namespace ct
