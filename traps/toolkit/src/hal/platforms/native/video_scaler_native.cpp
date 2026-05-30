#include "video_scaler_native.hpp"
#include <iostream>
#include <cstring>

namespace ct {

bool VideoScalerNative::scale_nv12(int src_fd, int src_w, int src_h,
                                   int dst_fd, int dst_w, int dst_h) {
    std::cout << "[VideoScalerNative] scale_nv12 "
              << src_w << "x" << src_h << " -> " << dst_w << "x" << dst_h
              << " (stub)\n";
    return true;
}

bool VideoScalerNative::scale_nv12_to_bgr(int src_fd, int src_w, int src_h,
                                          int dst_fd, int dst_w, int dst_h) {
    std::cout << "[VideoScalerNative] scale_nv12_to_bgr "
              << src_w << "x" << src_h << " -> " << dst_w << "x" << dst_h
              << " (stub)\n";
    return true;
}

} // namespace ct
