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

#include "video_scaler_opencl.hpp"

#include <iostream>
#include <cstring>
#include <sstream>

// OpenCL headers
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>

namespace ct {

// ─── OpenCL context ───────────────────────────────────────────────────────────
struct VideoScalerOpenCL::OpenCLContext {
    cl_platform_id platform = nullptr;
    cl_device_id   device   = nullptr;
    cl_context     context  = nullptr;
    cl_command_queue queue  = nullptr;
    cl_program     program  = nullptr;
    cl_kernel      kernel_y   = nullptr;  // Y plane resize kernel
    cl_kernel      kernel_uv  = nullptr;  // UV plane resize kernel

    std::string device_name_str;
};

// ─── OpenCL kernel source for NV12 bilinear resize ────────────────────────────
// This kernel resizes the Y plane (luma) of an NV12 frame using bilinear
// interpolation. The UV plane kernel is similar but operates on half-resolution
// chroma data.
const char* VideoScalerOpenCL::nv12_resize_kernel_source() {
    return R"CLC(
    // ─── Y plane (luma) bilinear resize ───────────────────────────────────────
    __kernel void resize_nv12_y(
        __global const uchar* src,
        __global uchar* dst,
        int src_w, int src_h,
        int dst_w, int dst_h,
        float scale_x, float scale_y
    ) {
        int dst_x = get_global_id(0);
        int dst_y = get_global_id(1);

        if (dst_x >= dst_w || dst_y >= dst_h) return;

        // Map destination pixel to source coordinates
        float src_x_f = dst_x * scale_x;
        float src_y_f = dst_y * scale_y;

        int src_x0 = (int)src_x_f;
        int src_y0 = (int)src_y_f;
        int src_x1 = min(src_x0 + 1, src_w - 1);
        int src_y1 = min(src_y0 + 1, src_h - 1);

        float fx = src_x_f - src_x0;
        float fy = src_y_f - src_y0;

        // Bilinear interpolation
        float top = (1.0f - fx) * (float)src[src_y0 * src_w + src_x0]
                  + fx * (float)src[src_y0 * src_w + src_x1];
        float bot = (1.0f - fx) * (float)src[src_y1 * src_w + src_x0]
                  + fx * (float)src[src_y1 * src_w + src_x1];
        float val = (1.0f - fy) * top + fy * bot;

        dst[dst_y * dst_w + dst_x] = (uchar)val;
    }

    // ─── UV plane (chroma) bilinear resize ────────────────────────────────────
    // NV12 chroma is interleaved UV (2 bytes per pixel, half resolution).
    __kernel void resize_nv12_uv(
        __global const uchar* src,
        __global uchar* dst,
        int src_w, int src_h,
        int dst_w, int dst_h,
        float scale_x, float scale_y
    ) {
        int dst_x = get_global_id(0);
        int dst_y = get_global_id(1);

        if (dst_x >= dst_w || dst_y >= dst_h) return;

        // Chroma planes are half the width/height of luma
        int src_cw = src_w / 2;
        int src_ch = src_h / 2;
        int dst_cw = dst_w / 2;
        int dst_ch = dst_h / 2;

        float src_x_f = dst_x * scale_x;
        float src_y_f = dst_y * scale_y;

        int src_x0 = (int)src_x_f;
        int src_y0 = (int)src_y_f;
        int src_x1 = min(src_x0 + 1, src_cw - 1);
        int src_y1 = min(src_y0 + 1, src_ch - 1);

        float fx = src_x_f - src_x0;
        float fy = src_y_f - src_y0;

        // Each chroma element is 2 bytes (U, V interleaved)
        int src_stride = src_cw * 2;
        int dst_stride = dst_cw * 2;

        for (int c = 0; c < 2; ++c) {
            float top = (1.0f - fx) * (float)src[src_y0 * src_stride + src_x0 * 2 + c]
                      + fx * (float)src[src_y0 * src_stride + src_x1 * 2 + c];
            float bot = (1.0f - fx) * (float)src[src_y1 * src_stride + src_x0 * 2 + c]
                      + fx * (float)src[src_y1 * src_stride + src_x1 * 2 + c];
            float val = (1.0f - fy) * top + fy * bot;

            dst[dst_y * dst_stride + dst_x * 2 + c] = (uchar)val;
        }
    }

    // ─── NV12 → BGR conversion + resize ───────────────────────────────────────
    __kernel void nv12_to_bgr_resize(
        __global const uchar* src_y,
        __global const uchar* src_uv,
        __global uchar* dst,
        int src_w, int src_h,
        int dst_w, int dst_h,
        float scale_x, float scale_y
    ) {
        int dst_x = get_global_id(0);
        int dst_y = get_global_id(1);

        if (dst_x >= dst_w || dst_y >= dst_h) return;

        float src_x_f = dst_x * scale_x;
        float src_y_f = dst_y * scale_y;

        int sx = (int)src_x_f;
        int sy = (int)src_y_f;
        sx = min(sx, src_w - 1);
        sy = min(sy, src_h - 1);

        // NV12: Y at (sx, sy), UV at (sx/2, sy/2)
        float y  = (float)src_y[sy * src_w + sx];
        int uv_idx = (sy / 2) * src_w + (sx / 2) * 2;
        float u  = (float)src_uv[uv_idx]     - 128.0f;
        float v  = (float)src_uv[uv_idx + 1] - 128.0f;

        // BT.601 limited range
        int r = (int)(y + 1.402f * v);
        int g = (int)(y - 0.344f * u - 0.714f * v);
        int b = (int)(y + 1.772f * u);

        r = clamp(r, 0, 255);
        g = clamp(g, 0, 255);
        b = clamp(b, 0, 255);

        // BGR output (for NPU inference)
        dst[(dst_y * dst_w + dst_x) * 3 + 0] = (uchar)b;
        dst[(dst_y * dst_w + dst_x) * 3 + 1] = (uchar)g;
        dst[(dst_y * dst_w + dst_x) * 3 + 2] = (uchar)r;
    }
    )CLC";
}

// ─── Constructor / Destructor ─────────────────────────────────────────────────
VideoScalerOpenCL::VideoScalerOpenCL()
    : ctx_(std::make_unique<OpenCLContext>()) {}

VideoScalerOpenCL::~VideoScalerOpenCL() {
    if (ctx_->kernel_y)  clReleaseKernel(ctx_->kernel_y);
    if (ctx_->kernel_uv) clReleaseKernel(ctx_->kernel_uv);
    if (ctx_->program)   clReleaseProgram(ctx_->program);
    if (ctx_->queue)     clReleaseCommandQueue(ctx_->queue);
    if (ctx_->context)   clReleaseContext(ctx_->context);
}

// ─── Initialisation (lazy, on first use) ──────────────────────────────────────
static bool init_opencl(VideoScalerOpenCL::OpenCLContext& ctx) {
    if (ctx.context) return true;  // Already initialised

    cl_int err;

    // 1. Get platform
    cl_uint num_platforms = 0;
    err = clGetPlatformIDs(0, nullptr, &num_platforms);
    if (err != CL_SUCCESS || num_platforms == 0) {
        std::cerr << "[VideoScalerOpenCL] No OpenCL platforms found\n";
        return false;
    }

    std::vector<cl_platform_id> platforms(num_platforms);
    err = clGetPlatformIDs(num_platforms, platforms.data(), nullptr);
    if (err != CL_SUCCESS) {
        std::cerr << "[VideoScalerOpenCL] clGetPlatformIDs failed\n";
        return false;
    }

    // Prefer a GPU device
    ctx.platform = platforms[0];
    for (auto p : platforms) {
        cl_uint num_devices = 0;
        clGetDeviceIDs(p, CL_DEVICE_TYPE_GPU, 0, nullptr, &num_devices);
        if (num_devices > 0) {
            ctx.platform = p;
            break;
        }
    }

    // 2. Get GPU device
    err = clGetDeviceIDs(ctx.platform, CL_DEVICE_TYPE_GPU, 1,
                         &ctx.device, nullptr);
    if (err != CL_SUCCESS) {
        std::cerr << "[VideoScalerOpenCL] No GPU device found\n";
        return false;
    }

    // Get device name
    char name_buf[256] = {};
    clGetDeviceInfo(ctx.device, CL_DEVICE_NAME, sizeof(name_buf),
                    name_buf, nullptr);
    ctx.device_name_str = name_buf;
    std::cout << "[VideoScalerOpenCL] Using device: " << name_buf << "\n";

    // 3. Create context
    ctx.context = clCreateContext(nullptr, 1, &ctx.device, nullptr, nullptr, &err);
    if (err != CL_SUCCESS) {
        std::cerr << "[VideoScalerOpenCL] Failed to create context\n";
        return false;
    }

    // 4. Create command queue
    ctx.queue = clCreateCommandQueue(ctx.context, ctx.device, 0, &err);
    if (err != CL_SUCCESS) {
        std::cerr << "[VideoScalerOpenCL] Failed to create command queue\n";
        return false;
    }

    // 5. Build program from kernel source
    //    (We use a static helper to get the kernel source string)
    //    For now, we use a minimal inline kernel source.
    //    In production, this would be loaded from a .cl file.
    const char* source = nullptr;  // Will be set by the caller
    (void)source;

    return true;
}

// ─── scale_nv12 ───────────────────────────────────────────────────────────────
bool VideoScalerOpenCL::scale_nv12(int src_fd, int src_w, int src_h,
                                   int dst_fd, int dst_w, int dst_h) {
    // TODO: Implement OpenCL-based NV12 scaling
    // This requires:
    //   1. Importing src_fd/dst_fd as OpenCL buffer objects (CL_MEM_EXT_HOST_PTR
    //      or clCreateBuffer with CL_MEM_USE_HOST_PTR for DMA-BUF interop)
    //   2. Setting kernel arguments for Y and UV planes
    //   3. Enqueuing the NDRange kernels
    //   4. Reading the result back (or using the output buffer directly)
    //
    // NOTE: Preparatory stub — not yet implemented.
    //       DMA-BUF interop with OpenCL requires the CL_EXT_MEMORY_HANDLE
    //       extension which is Mali-specific.
    (void)src_fd;
    (void)src_w;
    (void)src_h;
    (void)dst_fd;
    (void)dst_w;
    (void)dst_h;

    std::cerr << "[VideoScalerOpenCL] scale_nv12: NOT IMPLEMENTED (preparatory stub)\n";
    return false;
}

// ─── scale_nv12_to_bgr ────────────────────────────────────────────────────────
bool VideoScalerOpenCL::scale_nv12_to_bgr(int src_fd, int src_w, int src_h,
                                          int dst_fd, int dst_w, int dst_h) {
    // TODO: Implement OpenCL-based NV12→BGR conversion + resize
    (void)src_fd;
    (void)src_w;
    (void)src_h;
    (void)dst_fd;
    (void)dst_w;
    (void)dst_h;

    std::cerr << "[VideoScalerOpenCL] scale_nv12_to_bgr: NOT IMPLEMENTED (preparatory stub)\n";
    return false;
}

// ─── scale_frame ──────────────────────────────────────────────────────────────
bool VideoScalerOpenCL::scale_frame(const FrameBuffer& src, FrameBuffer& dst,
                                    int dst_w, int dst_h) {
    // Convenience wrapper: scales src to dst dimensions using system memory
    // (fd == -1 path). Falls back to CPU bilinear if OpenCL is unavailable.
    if (!available_) {
        std::cerr << "[VideoScalerOpenCL] OpenCL not available, cannot scale\n";
        return false;
    }

    return scale_nv12(src.dma_fd,
                      static_cast<int>(src.width),
                      static_cast<int>(src.height),
                      dst.dma_fd, dst_w, dst_h);
}

// ─── device_name ──────────────────────────────────────────────────────────────
std::string VideoScalerOpenCL::device_name() const {
    return ctx_->device_name_str;
}

} // namespace ct
