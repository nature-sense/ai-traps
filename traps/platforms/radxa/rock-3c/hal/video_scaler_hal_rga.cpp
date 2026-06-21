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

#include "video_scaler_hal_rga.hpp"
#include <iostream>
#include <cstring>

namespace ct {

VideoScalerRGA::VideoScalerRGA() {
    // librga initialization if necessary
    // In many versions of librga, we just use the c_RkRga global or create an instance.
}

VideoScalerRGA::~VideoScalerRGA() {
}

bool VideoScalerRGA::scale_nv12(int src_fd, int src_w, int src_h,
                                int dst_fd, int dst_w, int dst_h) {
    return blit(src_fd, src_w, src_h, RK_FORMAT_YCbCr_420_SP,
                dst_fd, dst_w, dst_h, RK_FORMAT_YCbCr_420_SP);
}

bool VideoScalerRGA::scale_nv12_to_bgr(int src_fd, int src_w, int src_h,
                                       int dst_fd, int dst_w, int dst_h) {
    return blit(src_fd, src_w, src_h, RK_FORMAT_YCbCr_420_SP,
                dst_fd, dst_w, dst_h, RK_FORMAT_BGR_888);
}

bool VideoScalerRGA::blit(int src_fd, int src_w, int src_h, int src_fmt,
                          int dst_fd, int dst_w, int dst_h, int dst_fmt) {
    rga_info_t src_info;
    rga_info_t dst_info;

    std::memset(&src_info, 0, sizeof(rga_info_t));
    std::memset(&dst_info, 0, sizeof(rga_info_t));

    src_info.fd = src_fd;
    src_info.mmuFlag = 1;
    rga_set_rect(&src_info.rect, 0, 0, src_w, src_h, src_w, src_h, src_fmt);

    dst_info.fd = dst_fd;
    dst_info.mmuFlag = 1;
    rga_set_rect(&dst_info.rect, 0, 0, dst_w, dst_h, dst_w, dst_h, dst_fmt);

    // Perform the blit (synchronous)
    int ret = RgaBlit(&src_info, &dst_info, nullptr);
    if (ret < 0) {
        std::cerr << "[VideoScalerRGA] RgaBlit failed: " << ret << "\n";
        return false;
    }

    return true;
}

} // namespace ct
