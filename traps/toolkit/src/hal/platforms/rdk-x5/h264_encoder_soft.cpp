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

#include "h264_encoder_soft.hpp"

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <sys/mman.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace ct {

SoftH264Encoder::SoftH264Encoder() = default;

SoftH264Encoder::~SoftH264Encoder() {
    deinit();
}

int SoftH264Encoder::init(int width, int height, int qp) {
    deinit();

    if (width <= 0 || height <= 0) return -1;
    width_ = width;
    height_ = height;

    // Find the H.264 encoder
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cerr << "[SoftH264] avcodec_find_encoder(H264) failed\n";
        return -1;
    }

    // Allocate codec context
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        std::cerr << "[SoftH264] avcodec_alloc_context3 failed\n";
        return -1;
    }

    ctx->width     = width;
    ctx->height    = height;
    ctx->time_base = AVRational{1, 30};        // 30 fps
    ctx->framerate = AVRational{30, 1};
    ctx->pix_fmt   = AV_PIX_FMT_NV12;
    ctx->gop_size  = 1;                         // Every frame is a keyframe
    ctx->max_b_frames = 0;                      // No B-frames (low latency)
    ctx->thread_count = 1;

    // Map QP (0-51) to FFmpeg's global_quality range
    ctx->global_quality = qp;
    ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    ctx->flags |= AV_CODEC_FLAG_QSCALE;

    // Set profile to High, level 4.0
    ctx->profile = FF_PROFILE_H264_HIGH;
    ctx->level   = 40;  // 4.0

    // Open the codec
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "preset", "ultrafast", 0);
    av_dict_set(&opts, "tune", "zerolatency", 0);

    int ret = avcodec_open2(ctx, codec, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[SoftH264] avcodec_open2 failed: " << errbuf << "\n";
        avcodec_free_context(&ctx);
        return -1;
    }

    codec_ctx_ = ctx;

    // Allocate AVFrame (NV12)
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        std::cerr << "[SoftH264] av_frame_alloc failed\n";
        deinit();
        return -1;
    }
    frame->width  = width;
    frame->height = height;
    frame->format = AV_PIX_FMT_NV12;

    ret = av_image_alloc(frame->data, frame->linesize,
                          width, height, AV_PIX_FMT_NV12, 32);
    if (ret < 0) {
        std::cerr << "[SoftH264] av_image_alloc failed\n";
        av_frame_free(&frame);
        deinit();
        return -1;
    }
    frame_ = frame;

    // Allocate AVPacket
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        std::cerr << "[SoftH264] av_packet_alloc failed\n";
        deinit();
        return -1;
    }
    pkt_ = pkt;

    std::cout << "[SoftH264] encoder initialized for " << width << "x" << height
              << " qp=" << qp << "\n";
    return 0;
}

std::vector<uint8_t> SoftH264Encoder::encode(int dma_fd, uint32_t size) {
    if (!codec_ctx_ || !frame_ || !pkt_ || dma_fd < 0) return {};

    AVFrame* frame = static_cast<AVFrame*>(frame_);
    AVPacket* pkt  = static_cast<AVPacket*>(pkt_);
    AVCodecContext* ctx = static_cast<AVCodecContext*>(codec_ctx_);

    // mmap the dmabuf — zero-copy MMU mapping, no memcpy from shared memory
    size_t y_size   = static_cast<size_t>(width_ * height_);
    size_t uv_size  = static_cast<size_t>(width_ * height_ / 2);
    size_t frame_size = size > 0 ? size : y_size + uv_size;

    void* mapped = mmap(nullptr, frame_size, PROT_READ, MAP_SHARED, dma_fd, 0);
    if (mapped == MAP_FAILED) {
        std::cerr << "[SoftH264] mmap dmabuf failed\n";
        return {};
    }

    const uint8_t* nv12_data = static_cast<const uint8_t*>(mapped);
    memcpy(frame->data[0], nv12_data, y_size);
    memcpy(frame->data[1], nv12_data + y_size, uv_size);
    munmap(mapped, frame_size);

    frame->pts = frame_count_++;

    // Send the frame to the encoder
    int ret = avcodec_send_frame(ctx, frame);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[SoftH264] avcodec_send_frame error: " << errbuf << "\n";
        return {};
    }

    // Receive the encoded packet
    std::vector<uint8_t> result;
    while (true) {
        av_packet_unref(pkt);
        ret = avcodec_receive_packet(ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) break;

        if (pkt->size > 0 && pkt->data) {
            result.insert(result.end(),
                           pkt->data, pkt->data + pkt->size);
        }
    }

    return result;
}

void SoftH264Encoder::deinit() {
    if (codec_ctx_) {
        AVCodecContext* ctx = static_cast<AVCodecContext*>(codec_ctx_);
        avcodec_close(ctx);
        avcodec_free_context(&ctx);
        codec_ctx_ = nullptr;
    }
    if (frame_) {
        AVFrame* f = static_cast<AVFrame*>(frame_);
        av_freep(&f->data[0]);
        av_frame_free(&f);
        frame_ = nullptr;
    }
    if (pkt_) {
        AVPacket* p = static_cast<AVPacket*>(pkt_);
        av_packet_free(&p);
        pkt_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
}

} // namespace ct