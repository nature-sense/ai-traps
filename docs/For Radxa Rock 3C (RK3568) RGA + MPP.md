------

## For Radxa Rock 3C (RK3568): RGA + MPP

Your pipeline uses **RGA (Raster Graphic Acceleration)** for cropping the NV12 buffer, then **MPP (Media Process Platform)** for hardware JPEG encoding.

### Step 1: RGA Cropping from DMA Buffer

You'll use `librga` to crop the NV12 buffer. The key functions are `importbuffer_fd()` (to import your DMA buffer) and `imcrop()` (to perform the hardware crop).

cpp

```
#include <rga/im2d.h>
#include <rga/rga.h>
#include <fcntl.h>

class RGACropper {
private:
    rga_buffer_handle_t src_handle;
    rga_buffer_t src_buf;
    
public:
    // Import your existing DMA buffer from inference frame
    int importSourceBuffer(int dma_fd, int width, int height) {
        im_handle_param_t src_param = {
            .width = width,
            .height = height,
            .format = RK_FORMAT_NV12,  // Your source format
            .fd = dma_fd,
            .size = (width * height * 3) / 2  // NV12 size
        };
        src_handle = importbuffer_fd(&src_param);
        if (src_handle <= 0) return -1;
        
        src_buf = wrapbuffer_handle(src_handle, width, height, RK_FORMAT_NV12);
        return 0;
    }
    
    // Crop a detection region to destination buffer
    int cropToBuffer(int dst_dma_fd, int crop_x, int crop_y, 
                     int crop_w, int crop_h, int dst_width, int dst_height) {
        // Import destination buffer
        im_handle_param_t dst_param = {
            .width = dst_width,
            .height = dst_height,
            .format = RK_FORMAT_NV12,
            .fd = dst_dma_fd,
            .size = (dst_width * dst_height * 3) / 2
        };
        rga_buffer_handle_t dst_handle = importbuffer_fd(&dst_param);
        rga_buffer_t dst_buf = wrapbuffer_handle(dst_handle, dst_width, dst_height, RK_FORMAT_NV12);
        
        // Define crop rectangle
        im_rect src_rect = {crop_x, crop_y, crop_w, crop_h};
        
        // Execute crop on RGA hardware - non-blocking
        int ret = imcrop(src_buf, dst_buf, src_rect);
        
        // Wait for completion
        imsync(dst_buf);
        
        releasebuffer_handle(dst_handle);
        return ret;
    }
    
    ~RGACropper() {
        if (src_handle > 0) releasebuffer_handle(src_handle);
    }
};
```



**Important**: When calling RGA functions, always check return values against `IM_STATUS_SUCCESS`. Some RGA implementations may crash if improper parameters are passed, especially with certain buffer sizes and memory alignments.

### Step 2: MPP JPEG Encoding

After cropping, use MPP to encode the cropped NV12 buffer to JPEG. The key interfaces are `mpp_create()`, `mpp_init()` with `MPP_CTX_ENC` and `MPP_ENC_MJPEG`.

cpp

```
#include <mpp/mpp.h>
#include <mpp/mpp_enc.h>

class MPPJPEGEncoder {
private:
    MppCtx mpp_ctx;
    MppApi *mpp_api;
    MppEncPrepCfg prep_cfg;
    MppEncCodecCfg codec_cfg;
    int width, height;
    
public:
    int init(int enc_width, int enc_height, int quality) {
        width = enc_width;
        height = enc_height;
        
        // Create MPP context
        mpp_create(&mpp_ctx, &mpp_api);
        
        // Initialize encoder for MJPEG
        mpp_init(mpp_ctx, MPP_CTX_ENC, MPP_ENC_MJPEG);
        
        // Configure input format (NV12)
        prep_cfg.change = MPP_ENC_PREP_CFG_CHANGE_INPUT;
        prep_cfg.width = width;
        prep_cfg.height = height;
        prep_cfg.format = MPP_FMT_NV12;
        mpp_api->control(mpp_ctx, MPP_ENC_SET_PREP_CFG, &prep_cfg);
        
        // Configure JPEG quality
        codec_cfg.jpeg_enc.change = MPP_ENC_JPEG_CFG_CHANGE_QP;
        // Convert quality (1-100) to quantization parameter
        codec_cfg.jpeg_enc.quant = 75;  // Good default
        mpp_api->control(mpp_ctx, MPP_ENC_SET_CODEC_CFG, &codec_cfg);
        
        return 0;
    }
    
    // Encode cropped DMA buffer to JPEG
    std::vector<uint8_t> encode(int dma_fd) {
        // Import DMA buffer as MPP buffer
        MppBuffer mpp_buf;
        mpp_buffer_import_with_tag(dma_fd, (width * height * 3) / 2, &mpp_buf, "cropped_frame");
        
        // Encode frame
        MppFrame frame = mpp_frame_init();
        mpp_frame_set_buffer(frame, mpp_buf);
        mpp_frame_set_width(frame, width);
        mpp_frame_set_height(frame, height);
        mpp_frame_set_fmt(frame, MPP_FMT_NV12);
        
        // Get encoded JPEG
        MppPacket packet;
        mpp_api->encode_put_frame(mpp_ctx, frame);
        mpp_api->encode_get_packet(mpp_ctx, &packet);
        
        // Extract JPEG data
        void *jpeg_data = mpp_packet_get_data(packet);
        size_t jpeg_size = mpp_packet_get_length(packet);
        
        std::vector<uint8_t> result((uint8_t*)jpeg_data, (uint8_t*)jpeg_data + jpeg_size);
        
        mpp_frame_deinit(&frame);
        mpp_packet_deinit(&packet);
        mpp_buffer_put(mpp_buf);
        
        return result;
    }
    
    ~MPPJPEGEncoder() {
        mpp_destroy(mpp_ctx);
    }
};
```



### Complete Pipeline for Rockchip

cpp

```
// Full pipeline for one detection
int processDetection(int source_dma_fd, int src_width, int src_height,
                     int crop_x, int crop_y, int crop_w, int crop_h,
                     std::vector<uint8_t>& jpeg_output) {
    
    // Step 1: Allocate destination DMA buffer for cropped NV12
    int crop_size = (crop_w * crop_h * 3) / 2;
    int dst_dma_fd = allocate_dmabuf(crop_size);  // Your DMA allocation function
    
    // Step 2: Crop using RGA
    RGACropper cropper;
    if (cropper.importSourceBuffer(source_dma_fd, src_width, src_height) != 0) 
        return -1;
    if (cropper.cropToBuffer(dst_dma_fd, crop_x, crop_y, crop_w, crop_h, crop_w, crop_h) != 0)
        return -1;
    
    // Step 3: Encode to JPEG using MPP
    MPPJPEGEncoder encoder;
    if (encoder.init(crop_w, crop_h, 85) != 0) return -1;
    jpeg_output = encoder.encode(dst_dma_fd);
    
    // Step 4: Cleanup
    close(dst_dma_fd);
    
    return 0;
}
```



The `ffmpeg-rockchip` project provides working examples of this RGA+MPP integration pattern. The key is ensuring the DMA buffers are properly aligned - RGA works best with 16-byte alignment for width and stride.

------

## For D-Robotics RDK X5: VSE + Encoder API

The RDK X5 has a cleaner integrated API. The **VSE (Video Scaling Engine)** handles cropping, and the **Encoder API** handles JPEG encoding.

### Step 1: VSE Alone Mode for Cropping

The VSE supports cropping up to 4K resolution with 6 independent channels. Use V4L2 `VIDIOC_S_SELECTION` to configure the crop region.

cpp

```
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <fcntl.h>

class VSECropper {
private:
    int vse_fd;
    int crop_w, crop_h;
    
public:
    int init(const char* vse_device = "/dev/video14") {
        // Open VSE channel (channel 0 for 4K support)
        vse_fd = open(vse_device, O_RDWR);
        if (vse_fd < 0) return -1;
        return 0;
    }
    
    int configureCrop(int x, int y, int width, int height) {
        crop_w = width;
        crop_h = height;
        
        // Configure crop region on VSE
        struct v4l2_selection sel = {0};
        sel.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        sel.target = V4L2_SEL_TGT_CROP;
        sel.r.left = x;
        sel.r.top = y;
        sel.r.width = width;
        sel.r.height = height;
        
        if (ioctl(vse_fd, VIDIOC_S_SELECTION, &sel) < 0) return -1;
        
        // Set output format to cropped dimensions
        struct v4l2_format fmt = {0};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = width;
        fmt.fmt.pix.height = height;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
        
        if (ioctl(vse_fd, VIDIOC_S_FMT, &fmt) < 0) return -1;
        
        return 0;
    }
    
    // Crop from source DMA buffer to destination DMA buffer
    int crop(int src_dma_fd, int dst_dma_fd, size_t src_size, size_t dst_size) {
        // Queue source buffer
        struct v4l2_buffer src_buf = {0};
        src_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        src_buf.memory = V4L2_MEMORY_DMABUF;
        src_buf.m.fd = src_dma_fd;
        src_buf.length = src_size;
        ioctl(vse_fd, VIDIOC_QBUF, &src_buf);
        
        // Queue destination buffer
        struct v4l2_buffer dst_buf = {0};
        dst_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        dst_buf.memory = V4L2_MEMORY_DMABUF;
        dst_buf.m.fd = dst_dma_fd;
        dst_buf.length = dst_size;
        ioctl(vse_fd, VIDIOC_QBUF, &dst_buf);
        
        // Start streaming
        int type_out = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        int type_cap = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(vse_fd, VIDIOC_STREAMON, &type_out);
        ioctl(vse_fd, VIDIOC_STREAMON, &type_cap);
        
        // Wait for completion
        ioctl(vse_fd, VIDIOC_DQBUF, &src_buf);
        ioctl(vse_fd, VIDIOC_DQBUF, &dst_buf);
        
        return 0;
    }
    
    ~VSECropper() {
        if (vse_fd >= 0) close(vse_fd);
    }
};
```



### Step 2: RDK X5 Encoder API for JPEG

The Encoder API is straightforward - initialize, start an MJPEG channel, feed NV12 frames, and retrieve JPEG streams.

**Key requirements**:

- Input format must be **NV12**
- RDK X5 requires **16-bit alignment** for encoded images
- Supports up to **32 concurrent encoding channels**

cpp

```
#include "encoder_module.h"  // RDK X5 encoder header

class RDKJPEGEncoder {
private:
    void* encoder_obj;
    int channel_id;
    int width, height;
    
public:
    int init(int enc_width, int enc_height, int bitrate_kbps = 4096) {
        width = enc_width;
        height = enc_height;
        
        // Initialize encoder module
        encoder_obj = sp_init_encoder_module();
        if (!encoder_obj) return -1;
        
        // Create MJPEG encoding channel
        // Channel IDs: 0-31 available
        channel_id = 0;
        int ret = sp_start_encode(encoder_obj, channel_id, 
                                   SP_ENCODER_MJPEG, 
                                   width, height, 
                                   bitrate_kbps);
        if (ret != 0) return -1;
        
        return 0;
    }
    
    // Encode cropped NV12 buffer (provided as DMA fd or pointer)
    std::vector<uint8_t> encode(void* nv12_buffer, size_t buffer_size) {
        // Feed NV12 frame to encoder
        // Note: RDK X5 requires 16-bit alignment
        sp_encoder_set_frame(encoder_obj, (char*)nv12_buffer, buffer_size);
        
        // Retrieve JPEG stream
        // Size should be sufficient for worst-case JPEG
        std::vector<uint8_t> jpeg_buffer(width * height);
        int jpeg_size = sp_encoder_get_stream(encoder_obj, (char*)jpeg_buffer.data());
        
        if (jpeg_size > 0) {
            jpeg_buffer.resize(jpeg_size);
            return jpeg_buffer;
        }
        
        return std::vector<uint8_t>();
    }
    
    // Overload for DMA buffer input
    std::vector<uint8_t> encode_from_dma(int dma_fd, size_t nv12_size) {
        // Map DMA buffer to userspace (implementation depends on your DMA allocator)
        void* mapped = mmap(NULL, nv12_size, PROT_READ, MAP_SHARED, dma_fd, 0);
        if (mapped == MAP_FAILED) return std::vector<uint8_t>();
        
        auto result = encode(mapped, nv12_size);
        munmap(mapped, nv12_size);
        
        return result;
    }
    
    ~RDKJPEGEncoder() {
        if (encoder_obj) {
            sp_stop_encode(encoder_obj, channel_id);
            sp_release_encoder_module(encoder_obj);
        }
    }
};
```



### Complete Pipeline for RDK X5

cpp

```
int processDetection(int source_dma_fd, int src_width, int src_height,
                     int crop_x, int crop_y, int crop_w, int crop_h,
                     std::vector<uint8_t>& jpeg_output) {
    
    // Step 1: Verify alignment (RDK X5 requires 16-bit alignment)[citation:1]
    if ((crop_w % 16) != 0 || (crop_h % 16) != 0) {
        // Adjust crop dimensions or handle accordingly
        fprintf(stderr, "Warning: Crop dimensions not 16-aligned\n");
    }
    
    // Step 2: Allocate destination DMA buffer for cropped NV12
    size_t crop_size = (crop_w * crop_h * 3) / 2;
    int dst_dma_fd = allocate_dmabuf(crop_size);
    
    // Step 3: Crop using VSE
    VSECropper vse;
    if (vse.init("/dev/video14") != 0) return -1;  // Channel 0 for 4K[citation:2]
    if (vse.configureCrop(crop_x, crop_y, crop_w, crop_h) != 0) return -1;
    if (vse.crop(source_dma_fd, dst_dma_fd, 
                 (src_width * src_height * 3) / 2, crop_size) != 0) return -1;
    
    // Step 4: Encode to JPEG using hardware encoder
    RDKJPEGEncoder encoder;
    if (encoder.init(crop_w, crop_h, 4096) != 0) return -1;
    jpeg_output = encoder.encode_from_dma(dst_dma_fd, crop_size);
    
    close(dst_dma_fd);
    return 0;
}
```



The VSE supports up to 6 channels with different capabilities:

- **Channel 0**: 4K downscale (best for large crops)
- **Channels 1-4**: 1080P/720P downscale
- **Channel 5**: 4K upscale (4x max)

Choose the appropriate VSE device node (`/dev/video14` for downscale channel 0) based on your crop dimensions.

------

## Performance Considerations

For both platforms, the key to low CPU usage is:

1. **Keep buffers in DMA memory** - avoid mapping to userspace when possible
2. **Batch operations** - process multiple detections per frame
3. **Use asynchronous APIs** - submit jobs and continue inference while hardware processes

The hardware JPEG encoder on RDK X5 supports up to 32 simultaneous channels, so you can encode multiple detection crops concurrently. For Rockchip, MPP also supports async encoding through frame-parallel operation.