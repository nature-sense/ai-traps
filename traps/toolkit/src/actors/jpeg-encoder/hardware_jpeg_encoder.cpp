#include "hardware_jpeg_encoder.hpp"
#include <iostream>
#include <chrono>
#include <cstring>

#ifdef HAVE_MPP
#include <rockchip/rk_venc_cfg.h>
#include <rockchip/rk_venc_cmd.h>
#endif

namespace ct {

HardwareJpegEncoder::HardwareJpegEncoder() = default;

HardwareJpegEncoder::~HardwareJpegEncoder() {
    shutdown();
}

bool HardwareJpegEncoder::init(int width, int height, int quality) {
    if (initialized_) {
        std::cerr << "[HardwareJpegEncoder] Already initialized\n";
        return false;
    }
    
    if (width <= 0 || height <= 0) {
        std::cerr << "[HardwareJpegEncoder] Invalid dimensions: " 
                  << width << "x" << height << "\n";
        return false;
    }
    
    if (quality < 1 || quality > 100) {
        std::cerr << "[HardwareJpegEncoder] Invalid quality: " << quality 
                  << " (must be 1-100)\n";
        return false;
    }
    
    width_ = width;
    height_ = height;
    quality_ = quality;
    
#ifdef HAVE_MPP
    if (!create_mpp_encoder()) {
        cleanup();
        return false;
    }
    
    if (!configure_encoder(width, height, quality)) {
        cleanup();
        return false;
    }
    
    initialized_ = true;
    std::cout << "[HardwareJpegEncoder] Initialized for " 
              << width << "x" << height << " JPEG, quality=" << quality << "\n";
    return true;
#else
    std::cerr << "[HardwareJpegEncoder] MPP not available (HAVE_MPP not defined)\n";
    return false;
#endif
}

void HardwareJpegEncoder::shutdown() {
    if (!initialized_) return;
    
    cleanup();
    initialized_ = false;
    
    std::cout << "[HardwareJpegEncoder] Shutdown. Encoded " << frames_encoded_ 
              << " frames, avg time=" 
              << (frames_encoded_ > 0 ? total_encode_time_us_ / frames_encoded_ : 0)
              << "us\n";
}

#ifdef HAVE_MPP

bool HardwareJpegEncoder::create_mpp_encoder() {
    MPP_RET ret = MPP_OK;
    
    // Create MPP context
    ret = mpp_create(&mpp_ctx_, &mpp_api_);
    if (ret != MPP_OK) {
        std::cerr << "[HardwareJpegEncoder] mpp_create failed: " << ret << "\n";
        return false;
    }
    
    // Initialize for JPEG encoding
    ret = mpp_init(mpp_ctx_, MPP_CTX_ENC, MPP_VIDEO_CodingMJPEG);
    if (ret != MPP_OK) {
        std::cerr << "[HardwareJpegEncoder] mpp_init failed: " << ret << "\n";
        return false;
    }
    
    // Create encoder configuration
    ret = mpp_enc_cfg_init(&mpp_cfg_);
    if (ret != MPP_OK) {
        std::cerr << "[HardwareJpegEncoder] mpp_enc_cfg_init failed: " << ret << "\n";
        return false;
    }
    
    // Create buffer group for output
    ret = mpp_buffer_group_get_internal(&buf_group_, MPP_BUFFER_TYPE_DRM);
    if (ret != MPP_OK) {
        std::cerr << "[HardwareJpegEncoder] mpp_buffer_group_get_internal failed: " << ret << "\n";
        return false;
    }
    
    return true;
}

bool HardwareJpegEncoder::configure_encoder(int width, int height, int quality) {
    MPP_RET ret = MPP_OK;
    
    // Set basic configuration (check each call individually)
    ret = mpp_enc_cfg_set_s32(mpp_cfg_, "prep:width", width);
    if (ret != MPP_OK) goto fail;
    ret = mpp_enc_cfg_set_s32(mpp_cfg_, "prep:height", height);
    if (ret != MPP_OK) goto fail;
    ret = mpp_enc_cfg_set_s32(mpp_cfg_, "prep:hor_stride", width);
    if (ret != MPP_OK) goto fail;
    ret = mpp_enc_cfg_set_s32(mpp_cfg_, "prep:ver_stride", height);
    if (ret != MPP_OK) goto fail;
    ret = mpp_enc_cfg_set_s32(mpp_cfg_, "prep:format", MPP_FMT_YUV420SP);
    if (ret != MPP_OK) goto fail;
    
    // Set JPEG quality (1-100, higher = better quality)
    // NOTE: On VEPU2 (RK3566), only jpeg:q_factor is supported.
    // jpeg:qf_max, jpeg:qf_min, rc:mode, and rc:bps_target are NOT supported
    // by the VEPU2 JPEG encoder and will cause MPP_ERR_VALUE (-6).
    ret = mpp_enc_cfg_set_s32(mpp_cfg_, "jpeg:q_factor", quality);
    if (ret != MPP_OK) goto fail;
    
    // Apply configuration
    ret = mpp_api_->control(mpp_ctx_, MPP_ENC_SET_CFG, mpp_cfg_);
    if (ret != MPP_OK) goto fail;
    
    return true;

fail:
    std::cerr << "[HardwareJpegEncoder] configure_encoder failed: " << ret << "\n";
    return false;
}


std::vector<uint8_t> HardwareJpegEncoder::encode(const void* nv12_data, int width, int height, int stride) {
    if (!initialized_) {
        std::cerr << "[HardwareJpegEncoder] Not initialized\n";
        return {};
    }
    
    if (width != width_ || height != height_) {
        std::cerr << "[HardwareJpegEncoder] Frame size mismatch: expected " 
                  << width_ << "x" << height_ << ", got " << width << "x" << height << "\n";
        return {};
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    MPP_RET ret = MPP_OK;
    
    // Create input MPP buffer
    MppBuffer input_buf = nullptr;
    MppFrame input_frame = nullptr;
    MppPacket output_packet = nullptr;
    
    // Allocate input buffer
    size_t nv12_size = stride * height * 3 / 2;  // NV12: Y plane + UV interleaved
    ret = mpp_buffer_get(buf_group_, &input_buf, nv12_size);
    if (ret != MPP_OK) {
        std::cerr << "[HardwareJpegEncoder] mpp_buffer_get failed: " << ret << "\n";
        return {};
    }
    
    // Copy NV12 data to MPP buffer
    void* buf_ptr = mpp_buffer_get_ptr(input_buf);
    std::memcpy(buf_ptr, nv12_data, nv12_size);
    
    // Create input frame
    ret = mpp_frame_init(&input_frame);
    if (ret != MPP_OK) {
        std::cerr << "[HardwareJpegEncoder] mpp_frame_init failed: " << ret << "\n";
        mpp_buffer_put(input_buf);
        return {};
    }
    
    mpp_frame_set_width(input_frame, width);
    mpp_frame_set_height(input_frame, height);
    mpp_frame_set_hor_stride(input_frame, stride);
    mpp_frame_set_ver_stride(input_frame, height);
    mpp_frame_set_fmt(input_frame, MPP_FMT_YUV420SP);
    mpp_frame_set_buffer(input_frame, input_buf);
    mpp_frame_set_eos(input_frame, 1);  // End of stream for single frame
    
    // Encode frame
    ret = mpp_api_->encode_put_frame(mpp_ctx_, input_frame);
    if (ret != MPP_OK) {
        std::cerr << "[HardwareJpegEncoder] encode_put_frame failed: " << ret << "\n";
        mpp_frame_deinit(&input_frame);
        mpp_buffer_put(input_buf);
        return {};
    }
    
    // Get encoded packet
    ret = mpp_api_->encode_get_packet(mpp_ctx_, &output_packet);
    if (ret != MPP_OK) {
        std::cerr << "[HardwareJpegEncoder] encode_get_packet failed: " << ret << "\n";
        mpp_frame_deinit(&input_frame);
        mpp_buffer_put(input_buf);
        return {};
    }
    
    // Extract JPEG data from packet
    std::vector<uint8_t> jpeg_data;
    void* packet_data = mpp_packet_get_data(output_packet);
    size_t packet_size = mpp_packet_get_length(output_packet);
    
    if (packet_data && packet_size > 0) {
        jpeg_data.resize(packet_size);
        std::memcpy(jpeg_data.data(), packet_data, packet_size);
    }
    
    // Cleanup
    mpp_packet_deinit(&output_packet);
    mpp_frame_deinit(&input_frame);
    mpp_buffer_put(input_buf);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto encode_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    frames_encoded_++;
    total_encode_time_us_ += encode_time.count();
    
    if (jpeg_data.empty()) {
        std::cerr << "[HardwareJpegEncoder] Empty JPEG output\n";
    }
    
    return jpeg_data;
}

void HardwareJpegEncoder::cleanup() {
#ifdef HAVE_MPP
    if (mpp_cfg_) {
        mpp_enc_cfg_deinit(mpp_cfg_);
        mpp_cfg_ = nullptr;
    }
    
    if (mpp_ctx_) {
        mpp_destroy(mpp_ctx_);
        mpp_ctx_ = nullptr;
        mpp_api_ = nullptr;
    }
    
    if (buf_group_) {
        mpp_buffer_group_put(buf_group_);
        buf_group_ = nullptr;
    }
#endif
}

#else // HAVE_MPP not defined

std::vector<uint8_t> HardwareJpegEncoder::encode(const void* nv12_data, int width, int height, int stride) {
    (void)nv12_data; (void)width; (void)height; (void)stride;
    std::cerr << "[HardwareJpegEncoder] Hardware encoding not available (HAVE_MPP not defined)\n";
    return {};
}

void HardwareJpegEncoder::cleanup() {
    // Nothing to clean up without MPP
}

#endif // HAVE_MPP

} // namespace ct