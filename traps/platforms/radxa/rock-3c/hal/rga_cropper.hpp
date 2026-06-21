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

#include <vector>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <rga/im2d.h>
#include <rga/rga.h>
#include <rga/RockchipRga.h>

#ifndef RK_FORMAT_NV12
#define RK_FORMAT_NV12 0x00000005
#endif

namespace ct {

// ─── RgaCropper ───────────────────────────────────────────────────────────────
// Hardware-accelerated NV12 crop using Rockchip RGA (2D accelerator).
// Uses importbuffer_fd() to import a DMA buffer, then imcrop() to crop.
class RgaCropper {
public:
    static bool ensure_rga_initialized();

    RgaCropper() = default;
    ~RgaCropper() { release(); }
    RgaCropper(const RgaCropper&) = delete;
    RgaCropper& operator=(const RgaCropper&) = delete;

    int setSource(int dma_fd, size_t dma_size, int src_w, int src_h, int src_stride) {
        release();
        if (dma_fd < 0 || dma_size == 0) return -1;

        src_handle_ = importbuffer_fd(dma_fd, dma_size);
        if (src_handle_ <= 0) {
            std::cerr << "[RgaCropper] importbuffer_fd failed for fd="
                      << dma_fd << "\n";
            return -1;
        }

        // Wrap as RGA buffer. wstride must be 16-byte aligned for RGA2.
        src_buf_ = wrapbuffer_handle(src_handle_, src_w, src_h, RK_FORMAT_NV12);
        src_buf_.wstride = (src_w + 15) & ~15;
        return 0;
    }

    std::vector<uint8_t> crop(int cx, int cy, int cw, int ch) {
        if (src_handle_ <= 0) return {};

        // Align to 2 pixels for NV12
        const int aw = (cw + 1) & ~1;
        const int ah = (ch + 1) & ~1;
        const int ax = cx & ~1;
        const int ay = cy & ~1;

        // Allocate CPU buffer for result
        const size_t dst_size = static_cast<size_t>(aw * ah * 3 / 2);
        std::vector<uint8_t> result(dst_size);

        // Wrap destination as virtual address buffer (16-byte aligned stride)
        const int dst_stride = (aw + 15) & ~15;
        rga_buffer_t dst = wrapbuffer_virtualaddr(result.data(), aw, ah,
                                                   dst_stride, ah, RK_FORMAT_NV12);

        im_rect src_rect = {ax, ay, aw, ah};
        IM_STATUS ret = imcrop_t(src_buf_, dst, src_rect, 1);
        if (ret != IM_STATUS_SUCCESS) {
            std::cerr << "[RgaCropper] imcrop failed: " << ret << "\n";
            return {};
        }
        return result;
    }

    void release() {
        if (src_handle_ > 0) {
            releasebuffer_handle(src_handle_);
            src_handle_ = 0;
        }
    }

private:
    inline static std::once_flag rga_init_flag_;
    inline static bool rga_init_ok_ = false;

    rga_buffer_handle_t src_handle_ = 0;
    rga_buffer_t src_buf_ = {};
};

inline bool RgaCropper::ensure_rga_initialized() {
    std::call_once(rga_init_flag_, []() {
        std::cout << "[RgaCropper] initialising RockchipRGA singleton...\n";
        auto& rga = RockchipRga::get();
        int ret = rga.RkRgaInit();
        if (ret != 0) {
            std::cerr << "[RgaCropper] RkRgaInit() failed: " << ret << "\n";
            rga_init_ok_ = false;
        } else {
            std::cout << "[RgaCropper] RkRgaInit() OK\n";
            rga_init_ok_ = true;
        }
    });
    return rga_init_ok_;
}

} // namespace ct