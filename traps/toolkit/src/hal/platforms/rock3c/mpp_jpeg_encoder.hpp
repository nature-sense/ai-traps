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
#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/rk_type.h>
#include <rockchip/rk_venc_cmd.h>

namespace ct {

// ─── MppJpegEncoder ───────────────────────────────────────────────────────────
// Hardware-accelerated NV12 → JPEG encoding using Rockchip MPP (MJPEG encoder).
class MppJpegEncoder {
public:
    MppJpegEncoder() = default;
    ~MppJpegEncoder() { deinit(); }

    MppJpegEncoder(const MppJpegEncoder&) = delete;
    MppJpegEncoder& operator=(const MppJpegEncoder&) = delete;

    int init(int enc_width, int enc_height, int quality = 85) {
        deinit();
        if (enc_width <= 0 || enc_height <= 0) return -1;
        width_ = enc_width;
        height_ = enc_height;

        MPP_RET ret = mpp_create(&mpp_ctx_, &mpp_api_);
        if (ret != MPP_OK) return -1;

        ret = mpp_init(mpp_ctx_, MPP_CTX_ENC, MPP_VIDEO_CodingMJPEG);
        if (ret != MPP_OK) { mpp_destroy(mpp_ctx_); mpp_ctx_ = nullptr; return -1; }

        MppEncPrepCfg prep;
        std::memset(&prep, 0, sizeof(prep));
        prep.change = MPP_ENC_PREP_CFG_CHANGE_INPUT;
        prep.width  = width_;
        prep.height = height_;
        prep.format = MPP_FMT_YUV420SP;
        mpp_api_->control(mpp_ctx_, MPP_ENC_SET_PREP_CFG, &prep);

        MppEncCodecCfg codec;
        std::memset(&codec, 0, sizeof(codec));
        codec.coding = MPP_VIDEO_CodingMJPEG;
        codec.jpeg.change = MPP_ENC_JPEG_CFG_CHANGE_QP;
        codec.jpeg.quant  = (100 - quality) * 2;
        mpp_api_->control(mpp_ctx_, MPP_ENC_SET_CODEC_CFG, &codec);

        ret = mpp_frame_init(&mpp_frame_);
        if (ret != MPP_OK) { deinit(); return -1; }

        mpp_frame_set_width(mpp_frame_, width_);
        mpp_frame_set_height(mpp_frame_, height_);
        mpp_frame_set_fmt(mpp_frame_, MPP_FMT_YUV420SP);
        mpp_frame_set_hor_stride(mpp_frame_, width_);
        mpp_frame_set_ver_stride(mpp_frame_, height_);

        // Create buffer group for frame allocation
        mpp_buffer_group_get_internal(&buf_group_, MPP_BUFFER_TYPE_ION);
        return 0;
    }

    std::vector<uint8_t> encode(const uint8_t* nv12_data) {
        if (!mpp_ctx_ || !mpp_frame_ || !nv12_data) return {};
        size_t frame_size = static_cast<size_t>(width_ * height_ * 3 / 2);

        MppBuffer src_buf = nullptr;
        MPP_RET ret = mpp_buffer_get(buf_group_, &src_buf, frame_size);
        if (ret != MPP_OK || !src_buf) return {};

        void* ptr = mpp_buffer_get_ptr(src_buf);
        if (ptr) std::memcpy(ptr, nv12_data, frame_size);

        mpp_frame_set_buffer(mpp_frame_, src_buf);

        ret = mpp_api_->encode_put_frame(mpp_ctx_, mpp_frame_);
        if (ret != MPP_OK) { mpp_buffer_put(src_buf); return {}; }

        MppPacket packet = nullptr;
        ret = mpp_api_->encode_get_packet(mpp_ctx_, &packet);
        if (ret != MPP_OK || !packet) { mpp_buffer_put(src_buf); return {}; }

        void* jpeg_ptr = mpp_packet_get_data(packet);
        size_t jpeg_size = mpp_packet_get_length(packet);

        std::vector<uint8_t> result;
        if (jpeg_ptr && jpeg_size > 0)
            result.assign((const uint8_t*)jpeg_ptr, (const uint8_t*)jpeg_ptr + jpeg_size);

        mpp_packet_deinit(&packet);
        mpp_buffer_put(src_buf);
        return result;
    }

    void deinit() {
        if (mpp_frame_) { mpp_frame_deinit(&mpp_frame_); mpp_frame_ = nullptr; }
        if (buf_group_) { mpp_buffer_group_put(buf_group_); buf_group_ = nullptr; }
        if (mpp_ctx_) { mpp_destroy(mpp_ctx_); mpp_ctx_ = nullptr; mpp_api_ = nullptr; }
    }

private:
    MppCtx          mpp_ctx_     = nullptr;
    MppApi*         mpp_api_     = nullptr;
    MppFrame        mpp_frame_   = nullptr;
    MppBufferGroup  buf_group_   = nullptr;
    int width_  = 0;
    int height_ = 0;
};

} // namespace ct