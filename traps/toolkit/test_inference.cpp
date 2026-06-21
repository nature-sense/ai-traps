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
// test_inference.cpp — Standalone C++ test for RKNN inference on a single image
//
// Loads a scene PNG, resizes to 320×320 RGB, runs RKNN inference on NPU,
// decodes both YOLOv8n [1,5,2100] and YOLO26n [1,300,6] output formats,
// and prints detections to stdout.
//
// Build (cross-compile for rock3c):
//   tools/native/build.sh rock3c toolkit
//
// Deploy + run:
//   scp build-rock3c/toolkit/test_inference radxa@rock-3c.local:/tmp/
//   ssh radxa@rock-3c.local "/tmp/test_inference /tmp/yolov8n_fp16.rknn /tmp/frame_0010.png"
//   ssh radxa@rock-3c.local "/tmp/test_inference /tmp/yolo26n_fp16.rknn /tmp/frame_0010.png"
//
// Usage:
//   test_inference <model.rknn> <image.png> [confidence_threshold]
// ──────────────────────────────────────────────────────────────────────────────

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <algorithm>
#include <vector>
#include <cstdint>

extern "C" {
#include <rknn_api.h>
#include <png.h>
}

// ─── Constants ────────────────────────────────────────────────────────────────
static constexpr int   MODEL_W      = 320;
static constexpr int   MODEL_H      = 320;
static constexpr float CONF_THRESH  = 0.5f;
static constexpr float NMS_THRESH   = 0.45f;

// ─── Detection struct ─────────────────────────────────────────────────────────
struct Detection {
    float x, y, w, h;       // normalized [0,1] relative to model input
    float confidence;
    int   class_id;
};

// ─── PNG loader (RGBA) ───────────────────────────────────────────────────────
static uint8_t* load_png(const char* path, int& out_w, int& out_h) {
    FILE* fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "[ERROR] Cannot open PNG: %s\n", path); return nullptr; }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                                  nullptr, nullptr, nullptr);
    if (!png_ptr) { fclose(fp); return nullptr; }
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) { png_destroy_read_struct(&png_ptr, nullptr, nullptr); fclose(fp); return nullptr; }
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        fclose(fp); return nullptr;
    }

    png_init_io(png_ptr, fp);
    png_read_info(png_ptr, info_ptr);
    png_uint_32 w, h;
    int bit_depth, color_type;
    png_get_IHDR(png_ptr, info_ptr, &w, &h, &bit_depth, &color_type,
                 nullptr, nullptr, nullptr);

    if (bit_depth == 16) png_set_strip_16(png_ptr);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png_ptr);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY)
        png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER);

    png_read_update_info(png_ptr, info_ptr);
    size_t row_bytes = png_get_rowbytes(png_ptr, info_ptr);
    uint8_t* image = new uint8_t[row_bytes * h];
    std::vector<png_bytep> row_ptrs(h);
    for (png_uint_32 y = 0; y < h; ++y)
        row_ptrs[y] = image + y * row_bytes;
    png_read_image(png_ptr, row_ptrs.data());
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    fclose(fp);
    out_w = static_cast<int>(w);
    out_h = static_cast<int>(h);
    return image;  // RGBA, 4 bytes per pixel
}

// ─── Bilinear resize RGBA → RGB ──────────────────────────────────────────────
static void resize_to_rgb(const uint8_t* src, int src_w, int src_h,
                           uint8_t* dst, int dst_w, int dst_h) {
    const float scale_x = static_cast<float>(src_w) / dst_w;
    const float scale_y = static_cast<float>(src_h) / dst_h;
    for (int dy = 0; dy < dst_h; ++dy) {
        for (int dx = 0; dx < dst_w; ++dx) {
            float sx = (dx + 0.5f) * scale_x - 0.5f;
            float sy = (dy + 0.5f) * scale_y - 0.5f;
            int ix = std::max(0, std::min(src_w - 2, static_cast<int>(std::floor(sx))));
            int iy = std::max(0, std::min(src_h - 2, static_cast<int>(std::floor(sy))));
            float fx = sx - ix, fy = sy - iy;
            for (int c = 0; c < 3; ++c) {
                float v = (1-fx)*(1-fy)*src[(iy*src_w+ix)*4+c] +
                          fx*(1-fy)*src[(iy*src_w+ix+1)*4+c] +
                          (1-fx)*fy*src[((iy+1)*src_w+ix)*4+c] +
                          fx*fy*src[((iy+1)*src_w+ix+1)*4+c];
                int vi = static_cast<int>(v + 0.5f);
                dst[(dy*dst_w+dx)*3+c] = static_cast<uint8_t>(std::max(0, std::min(255, vi)));
            }
        }
    }
}

// ─── NMS ──────────────────────────────────────────────────────────────────────
static void nms(std::vector<Detection>& dets, float iou_thresh) {
    if (dets.empty()) return;
    std::sort(dets.begin(), dets.end(),
              [](const Detection& a, const Detection& b) { return a.confidence > b.confidence; });
    std::vector<bool> keep(dets.size(), true);
    for (size_t i = 0; i < dets.size(); ++i) {
        if (!keep[i]) continue;
        for (size_t j = i + 1; j < dets.size(); ++j) {
            if (!keep[j]) continue;
            float ix1 = std::max(dets[i].x, dets[j].x);
            float iy1 = std::max(dets[i].y, dets[j].y);
            float ix2 = std::min(dets[i].x + dets[i].w, dets[j].x + dets[j].w);
            float iy2 = std::min(dets[i].y + dets[i].h, dets[j].y + dets[j].h);
            float inter = std::max(0.0f, ix2 - ix1) * std::max(0.0f, iy2 - iy1);
            float iou = inter / (dets[i].w*dets[i].h + dets[j].w*dets[j].h - inter + 1e-8f);
            if (iou > iou_thresh) keep[j] = false;
        }
    }
    size_t n = 0;
    for (size_t i = 0; i < dets.size(); ++i) if (keep[i]) dets[n++] = dets[i];
    dets.resize(n);
}

// ─── Post-process ─────────────────────────────────────────────────────────────
static std::vector<Detection> postprocess(const float* output,
                                           const uint32_t* dims, int n_dims,
                                           float conf_thresh) {
    std::vector<Detection> detections;
    if (n_dims < 2) return detections;
    int d1 = static_cast<int>(dims[1]);
    int d2 = static_cast<int>(dims[2]);
    const float inv_w = 1.0f / MODEL_W;
    const float inv_h = 1.0f / MODEL_H;

    // YOLOv8n: [1, 5, 2100] pre-decoded [x1,y1,x2,y2,conf]
    if (d1 == 5 && d2 == 2100) {
        printf("[INFO] Format: YOLOv8n pre-decoded [1,5,2100]\n");
        for (int i = 0; i < d2; ++i) {
            float conf = output[i + 4 * d2];
            if (conf < conf_thresh) continue;
            float x1 = output[i], y1 = output[i + d2];
            float x2 = output[i + 2*d2], y2 = output[i + 3*d2];
            detections.push_back({std::min(x1,x2)*inv_w, std::min(y1,y2)*inv_h,
                                  std::abs(x2-x1)*inv_w, std::abs(y2-y1)*inv_h,
                                  conf, 0});
        }
    }
    // YOLO26n: [1, N, 6] post-NMS [x1,y1,x2,y2,conf,class]
    else if (d2 == 6) {
        printf("[INFO] Format: YOLO26n post-NMS [1,%d,6]\n", d1);
        for (int i = 0; i < d1; ++i) {
            float conf = output[i*6 + 4];
            if (conf < conf_thresh) continue;
            float x1 = output[i*6], y1 = output[i*6+1];
            float x2 = output[i*6+2], y2 = output[i*6+3];
            int cls = static_cast<int>(output[i*6+5]);
            detections.push_back({std::min(x1,x2)*inv_w, std::min(y1,y2)*inv_h,
                                  std::abs(x2-x1)*inv_w, std::abs(y2-y1)*inv_h,
                                  conf, cls});
        }
    }
    // Raw DFL: [1, C, N] with C >= 65
    else if (d1 >= 65) {
        constexpr int REG_MAX = 16;
        int num_classes = d1 - 4 * REG_MAX;
        printf("[INFO] Format: raw DFL [1,%d,%d] (%d classes)\n", d1, d2, num_classes);
        for (int a = 0; a < d2; ++a) {
            float max_cls = -FLT_MAX; int class_id = 0;
            for (int c = 0; c < num_classes; ++c) {
                float s = output[a + (4*REG_MAX + c)*d2];
                if (s > max_cls) { max_cls = s; class_id = c; }
            }
            float cls_conf = 1.0f / (1.0f + std::exp(-max_cls));
            if (cls_conf < conf_thresh) continue;
            float dfl[4] = {0,0,0,0};
            for (int g = 0; g < 4; ++g) {
                float max_v = -FLT_MAX;
                for (int c = 0; c < REG_MAX; ++c)
                    max_v = std::max(max_v, output[a + (g*REG_MAX + c)*d2]);
                float sum = 0;
                for (int c = 0; c < REG_MAX; ++c) {
                    float e = std::exp(output[a + (g*REG_MAX + c)*d2] - max_v);
                    dfl[g] += c * e; sum += e;
                }
                dfl[g] /= sum;
            }
            float cx, cy, stride;
            if (a < 1600) { stride = 8; cx = (a%40+0.5f)*stride; cy = (a/40+0.5f)*stride; }
            else if (a < 2000) { stride = 16; int idx=a-1600; cx=(idx%20+0.5f)*stride; cy=(idx/20+0.5f)*stride; }
            else { stride = 32; int idx=a-2000; cx=(idx%10+0.5f)*stride; cy=(idx/10+0.5f)*stride; }
            float x1 = std::max(0.0f, std::min(1.0f, (cx - dfl[0]*stride)*inv_w));
            float y1 = std::max(0.0f, std::min(1.0f, (cy - dfl[1]*stride)*inv_h));
            float x2 = std::max(0.0f, std::min(1.0f, (cx + dfl[2]*stride)*inv_w));
            float y2 = std::max(0.0f, std::min(1.0f, (cy + dfl[3]*stride)*inv_h));
            detections.push_back({x1, y1, x2-x1, y2-y1, cls_conf, class_id});
        }
    } else {
        printf("[INFO] Unknown format: [1,%d,%d]\n", d1, d2);
    }
    printf("[INFO] Raw: %zu  After NMS: ", detections.size());
    nms(detections, NMS_THRESH);
    printf("%zu\n", detections.size());
    return detections;
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <model.rknn> <image.png> [conf_thresh]\n", argv[0]);
        return 1;
    }
    const char* model_path = argv[1];
    const char* image_path = argv[2];
    float conf_thresh = (argc >= 4) ? std::atof(argv[3]) : CONF_THRESH;
    printf("[INFO] Model: %s  Image: %s  Conf: %.2f\n", model_path, image_path, conf_thresh);

    // ── 1. Load PNG ────────────────────────────────────────────────────────────
    int src_w = 0, src_h = 0;
    uint8_t* rgba = load_png(image_path, src_w, src_h);
    if (!rgba) return 1;
    printf("[INFO] Loaded %dx%d RGBA\n", src_w, src_h);

    // ── 2. Load model ──────────────────────────────────────────────────────────
    FILE* fp = fopen(model_path, "rb");
    if (!fp) { fprintf(stderr, "[ERROR] Cannot open model\n"); delete[] rgba; return 1; }
    fseek(fp, 0, SEEK_END);
    size_t model_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    void* model_data = malloc(model_size);
    if (fread(model_data, 1, model_size, fp) != model_size) {
        fprintf(stderr, "[ERROR] Short read on model\n");
        fclose(fp); free(model_data); delete[] rgba; return 1;
    }
    fclose(fp);
    printf("[INFO] Model loaded: %zu bytes\n", model_size);

    // ── 3. Init RKNN ───────────────────────────────────────────────────────────
    rknn_context ctx = 0;
    int ret = rknn_init(&ctx, model_data, model_size, 0, nullptr);
    if (ret < 0) { fprintf(stderr, "[ERROR] rknn_init: %d\n", ret); free(model_data); delete[] rgba; return 1; }
    printf("[INFO] rknn_init OK\n");

    // ── 4. Query input/output info ─────────────────────────────────────────────
    rknn_input_output_num io_num;
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret < 0) { fprintf(stderr, "[ERROR] rknn_query IN_OUT_NUM: %d\n", ret); rknn_destroy(ctx); free(model_data); delete[] rgba; return 1; }
    printf("[INFO] Inputs: %d, Outputs: %d\n", io_num.n_input, io_num.n_output);

    // ── 5. Query input attributes ──────────────────────────────────────────────
    rknn_tensor_attr input_attr;
    memset(&input_attr, 0, sizeof(input_attr));
    input_attr.index = 0;
    ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &input_attr, sizeof(input_attr));
    if (ret < 0) { fprintf(stderr, "[ERROR] rknn_query INPUT_ATTR: %d\n", ret); rknn_destroy(ctx); free(model_data); delete[] rgba; return 1; }

    int model_in_h, model_in_w, model_in_c;
    if (input_attr.fmt == RKNN_TENSOR_NHWC) {
        model_in_h = input_attr.dims[1]; model_in_w = input_attr.dims[2]; model_in_c = input_attr.dims[3];
    } else {
        model_in_c = input_attr.dims[1]; model_in_h = input_attr.dims[2]; model_in_w = input_attr.dims[3];
    }
    printf("[INFO] Model input: %dx%dx%d fmt=%s attr.size=%u\n",
           model_in_w, model_in_h, model_in_c,
           input_attr.fmt == RKNN_TENSOR_NHWC ? "NHWC" : "NCHW", input_attr.size);

    // ── 6. Resize into a buffer matching the model's expected input size ───────
    // The RKNN runtime compares input_.size against attr.size. Use attr.size
    // (may be larger than W*H*3 for FP16 models). We write valid RGB into the
    // first W*H*3 bytes and zero-fill the rest.
    uint32_t input_buf_size = std::max(input_attr.size, static_cast<uint32_t>(model_in_w * model_in_h * model_in_c));
    std::vector<uint8_t> input_buf(input_buf_size, 0);
    resize_to_rgb(rgba, src_w, src_h, input_buf.data(), model_in_w, model_in_h);
    delete[] rgba;
    printf("[INFO] Resized to %dx%d RGB, input buf %u bytes\n", model_in_w, model_in_h, input_buf_size);

    // ── 7. Set input ───────────────────────────────────────────────────────────
    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    // pass_through=1: bypass RKNN's internal format conversion.
    // We already provide NHWC uint8 data, matching the model's expected layout.
    // This avoids a signed integer size comparison bug in rknn_inputs_set
    // where model input size wraps negative for certain model versions.
    inputs[0].index = 0;
    inputs[0].pass_through = 1;
    inputs[0].type  = RKNN_TENSOR_UINT8;
    inputs[0].fmt   = RKNN_TENSOR_NHWC;
    inputs[0].size  = static_cast<uint32_t>(model_in_w * model_in_h * model_in_c);
    inputs[0].buf   = input_buf.data();
    ret = rknn_inputs_set(ctx, 1, inputs);
    if (ret < 0) { fprintf(stderr, "[ERROR] rknn_inputs_set: %d\n", ret); rknn_destroy(ctx); free(model_data); return 1; }

    // ── 8. Run inference ───────────────────────────────────────────────────────
    printf("[INFO] Running inference...\n");
    ret = rknn_run(ctx, nullptr);
    if (ret < 0) { fprintf(stderr, "[ERROR] rknn_run: %d\n", ret); rknn_destroy(ctx); free(model_data); return 1; }
    printf("[INFO] Inference OK\n");

    // ── 9. Query output attributes ─────────────────────────────────────────────
    std::vector<rknn_tensor_attr> out_attrs(io_num.n_output);
    for (uint32_t i = 0; i < io_num.n_output; ++i) {
        memset(&out_attrs[i], 0, sizeof(rknn_tensor_attr));
        out_attrs[i].index = i;
        rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &out_attrs[i], sizeof(rknn_tensor_attr));
        printf("[INFO] Output[%d]: dims=[", i);
        for (int d = 0; d < out_attrs[i].n_dims; ++d)
            printf("%s%d", d ? "," : "", out_attrs[i].dims[d]);
        printf("] size=%u\n", out_attrs[i].size);
    }

    // ── 10. Get outputs ────────────────────────────────────────────────────────
    std::vector<rknn_output> outputs(io_num.n_output);
    for (uint32_t i = 0; i < io_num.n_output; ++i) {
        outputs[i].index = i;
        outputs[i].want_float = 1;
    }
    ret = rknn_outputs_get(ctx, io_num.n_output, outputs.data(), nullptr);
    if (ret < 0) { fprintf(stderr, "[ERROR] rknn_outputs_get: %d\n", ret); rknn_destroy(ctx); free(model_data); return 1; }

    // ── 11. Post-process ───────────────────────────────────────────────────────
    printf("\n=== Results ===\n");
    for (uint32_t i = 0; i < io_num.n_output; ++i) {
        float* out_data = static_cast<float*>(outputs[i].buf);
        uint32_t out_elems = outputs[i].size / sizeof(float);
        printf("[INFO] Output[%d]: %u floats  ", i, out_elems);
        for (int ei = 0; ei < std::min(5, (int)out_elems); ++ei)
            printf("%.4f ", out_data[ei]);
        printf("\n");

        if (out_attrs[i].n_dims >= 2) {
            auto dets = postprocess(out_data, out_attrs[i].dims, out_attrs[i].n_dims, conf_thresh);
            for (size_t di = 0; di < dets.size(); ++di) {
                auto& d = dets[di];
                int px = static_cast<int>(d.x * src_w);
                int py = static_cast<int>(d.y * src_h);
                int pw = static_cast<int>(d.w * src_w);
                int ph = static_cast<int>(d.h * src_h);
                printf("  [%zu] class=%d bbox=(%d,%d,%d,%d) rel=(%.4f,%.4f,%.4f,%.4f) conf=%.4f\n",
                       di, d.class_id, px, py, pw, ph, d.x, d.y, d.w, d.h, d.confidence);
            }
        }
    }

    // ── 12. Cleanup ────────────────────────────────────────────────────────────
    rknn_outputs_release(ctx, io_num.n_output, outputs.data());
    rknn_destroy(ctx);
    free(model_data);
    printf("\n[INFO] Done.\n");
    return 0;
}