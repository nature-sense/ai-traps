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

#ifdef HAVE_MPP

#include "mpp_h264_encoder.hpp"

#include <cstring>
#include <iostream>
#include <sys/mman.h>

#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/rk_type.h>
#include <rockchip/rk_venc_cmd.h>

#ifndef MPP_ENC_RC_CFG_CHANGE_RC_MODE
#define MPP_ENC_RC_CFG_CHANGE_RC_MODE (1 << 0)
#endif

#ifndef MPP_ENC_RC_QUALITY_CQP
#define MPP_ENC_RC_QUALITY_CQP ((MppEncRcQuality)4)
#endif

namespace ct {

MppH264Encoder::MppH264Encoder() = default;

MppH264Encoder::~MppH264Encoder() {
    deinit();
}

int MppH264Encoder::init(int w, int h, int qp) {
    deinit();
    if (w <= 0 || h <= 0) return -1;
    width_ = w; height_ = h;

    MPP_RET ret = mpp_create(&mpp_ctx_, reinterpret_cast<MppApi**>(&mpp_api_));
    if (ret != MPP_OK) { std::cerr << "[H264] mpp_create fail\n"; return -1; }

    ret = mpp_init(mpp_ctx_, MPP_CTX_ENC, MPP_VIDEO_CodingAVC);
    if (ret != MPP_OK) {
        std::cerr << "[H264] mpp_init AVC fail: " << ret << "\n";
        mpp_destroy(mpp_ctx_); mpp_ctx_ = nullptr; return -1;
    }
    std::cout << "[H264] mpp_init AVC OK\n";

    // Prep config
    MppEncPrepCfg prep;
    memset(&prep, 0, sizeof(prep));
    prep.change  = MPP_ENC_PREP_CFG_CHANGE_INPUT | MPP_ENC_PREP_CFG_CHANGE_FORMAT;
    prep.width   = w;
    prep.height  = h;
    prep.format  = MPP_FMT_YUV420SP;
    prep.hor_stride = (w + 15) & ~15;
    prep.ver_stride = h;
    reinterpret_cast<MppApi*>(mpp_api_)->control(mpp_ctx_, MPP_ENC_SET_PREP_CFG, &prep);

    // RC config — CQP mode for constant quality
    MppEncRcCfg rc;
    memset(&rc, 0, sizeof(rc));
    rc.change  = MPP_ENC_RC_CFG_CHANGE_RC_MODE |
                 MPP_ENC_RC_CFG_CHANGE_QP_INIT |
                 MPP_ENC_RC_CFG_CHANGE_QP_RANGE;
    rc.rc_mode = MPP_ENC_RC_MODE_CBR;
    rc.quality = MPP_ENC_RC_QUALITY_CQP;
    rc.qp_init = qp;
    rc.qp_min  = qp;
    rc.qp_max  = qp;
    reinterpret_cast<MppApi*>(mpp_api_)->control(mpp_ctx_, MPP_ENC_SET_RC_CFG, &rc);

    // Codec config
    MppEncCodecCfg codec;
    memset(&codec, 0, sizeof(codec));
    codec.coding = MPP_VIDEO_CodingAVC;
    codec.h264.change = MPP_ENC_H264_CFG_CHANGE_PROFILE |
                        MPP_ENC_H264_CFG_CHANGE_ENTROPY |
                        MPP_ENC_H264_CFG_CHANGE_TRANS_8x8;
    codec.h264.profile  = 100;         // High
    codec.h264.level    = 40;           // 4.0
    codec.h264.entropy_coding_mode = 1; // CABAC
    codec.h264.transform8x8_mode   = 1; // 8x8
    reinterpret_cast<MppApi*>(mpp_api_)->control(mpp_ctx_, MPP_ENC_SET_CODEC_CFG, &codec);

    // Frame
    ret = mpp_frame_init(&mpp_frame_);
    if (ret != MPP_OK) { deinit(); return -1; }
    mpp_frame_set_width(mpp_frame_, w);
    mpp_frame_set_height(mpp_frame_, h);
    mpp_frame_set_fmt(mpp_frame_, MPP_FMT_YUV420SP);
    mpp_frame_set_hor_stride(mpp_frame_, (w + 15) & ~15);
    mpp_frame_set_ver_stride(mpp_frame_, h);

    // Buffer group
    mpp_buffer_group_get_internal(&buf_group_, MPP_BUFFER_TYPE_ION);

    std::cout << "[H264] encoder ready\n";
    return 0;
}

std::vector<uint8_t> MppH264Encoder::encode(int dma_fd, uint32_t size) {
    if (!mpp_ctx_ || !mpp_frame_ || dma_fd < 0) return {};
    size_t frame_size = static_cast<size_t>(size > 0 ? size : width_ * height_ * 3 / 2);

    // mmap the dmabuf to get a CPU pointer for MPP
    void* nv12_data = mmap(nullptr, frame_size, PROT_READ, MAP_SHARED, dma_fd, 0);
    if (nv12_data == MAP_FAILED) {
        std::cerr << "[H264] mmap dmabuf failed\n";
        return {};
    }

    MppBuffer src_buf = nullptr;
    MPP_RET ret = mpp_buffer_get(buf_group_, &src_buf, frame_size);
    if (ret != MPP_OK || !src_buf) {
        munmap(nv12_data, frame_size);
        return {};
    }

    void* ptr = mpp_buffer_get_ptr(src_buf);
    if (ptr) memcpy(ptr, nv12_data, frame_size);
    munmap(nv12_data, frame_size);

    mpp_frame_set_buffer(mpp_frame_, src_buf);
    mpp_frame_set_eos(mpp_frame_, 0);

    ret = reinterpret_cast<MppApi*>(mpp_api_)->encode_put_frame(mpp_ctx_, mpp_frame_);
    if (ret != MPP_OK) { mpp_buffer_put(src_buf); return {}; }

    // Drain all packets
    std::vector<uint8_t> result;
    while (true) {
        MppPacket packet = nullptr;
        ret = reinterpret_cast<MppApi*>(mpp_api_)->encode_get_packet(mpp_ctx_, &packet);
        if (ret != MPP_OK || !packet) break;
        if (mpp_packet_get_eos(packet)) { mpp_packet_deinit(&packet); break; }

        void* data = mpp_packet_get_data(packet);
        size_t len = mpp_packet_get_length(packet);
        if (data && len > 0)
            result.insert(result.end(), (const uint8_t*)data, (const uint8_t*)data + len);
        mpp_packet_deinit(&packet);
    }
    mpp_buffer_put(src_buf);
    return result;
}

void MppH264Encoder::deinit() {
    if (mpp_frame_) { mpp_frame_deinit(&mpp_frame_); mpp_frame_ = nullptr; }
    if (buf_group_) { mpp_buffer_group_put(buf_group_); buf_group_ = nullptr; }
    if (mpp_ctx_) { mpp_destroy(mpp_ctx_); mpp_ctx_ = nullptr; mpp_api_ = nullptr; }
}

} // namespace ct

#endif // HAVE_MPP