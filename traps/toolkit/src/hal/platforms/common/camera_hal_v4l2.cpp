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

#include "camera_hal_v4l2.hpp"

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <chrono>

// V4L2 headers
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>  // major(), minor()
#include <poll.h>
#include <linux/videodev2.h>
#include <linux/media.h>
#include <linux/v4l2-subdev.h>
#include <errno.h>

// Compatibility defines for older kernel headers
#ifndef MEDIA_ENT_T_V4L2_SUBDEV_SENSOR
#define MEDIA_ENT_T_V4L2_SUBDEV_SENSOR MEDIA_ENT_T_V4L2_SUBDEV
#endif

// The v4l2_subdev_capability struct on older BSP kernels (e.g. Allwinner A527
// kernel 5.10) does NOT have a 'name' field. We use the media entity name
// from the media controller API instead, which is more reliable anyway.
// If VIDIOC_SUBDEV_QUERYCAP is not defined, we define a stub.
#ifndef VIDIOC_SUBDEV_QUERYCAP
#define VIDIOC_SUBDEV_QUERYCAP 0
#endif

namespace ct {

// ─── Static Helpers ──────────────────────────────────────────────────────────

const char* CameraHalV4L2::v4l2_fmt_name(uint32_t fourcc) {
    static char buf[5];
    buf[0] = (fourcc >>  0) & 0xFF;
    buf[1] = (fourcc >>  8) & 0xFF;
    buf[2] = (fourcc >> 16) & 0xFF;
    buf[3] = (fourcc >> 24) & 0xFF;
    buf[4] = '\0';
    return buf;
}

/// Try to open a V4L2 video device. Returns fd or -1.
static int try_open_v4l2(const char* device) {
    int fd = ::open(device, O_RDWR, 0);
    if (fd < 0) return -1;

    struct v4l2_capability cap{};
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        ::close(fd);
        return -1;
    }

    // Must be a video capture device (single or multi-planar)
    if (!(cap.capabilities & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE))) {
        ::close(fd);
        return -1;
    }

    std::cout << "[CameraHalV4L2] Opened " << device
              << ": " << reinterpret_cast<const char*>(cap.card)
              << " (driver: " << reinterpret_cast<const char*>(cap.driver) << ")\n";
    return fd;
}

/// Determine if the device uses multiplanar API based on capabilities.
static bool is_mplane(int fd) {
    struct v4l2_capability cap{};
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) return false;
    return (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0;
}

/// Return the appropriate buffer type for capture (single or multi-planar).
static uint32_t capture_buf_type(bool mplane) {
    return mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
}

// ─── Constructor / Destructor ─────────────────────────────────────────────────
CameraHalV4L2::CameraHalV4L2() = default;

CameraHalV4L2::~CameraHalV4L2() {
    shutdown();
}

// ─── init ─────────────────────────────────────────────────────────────────────
bool CameraHalV4L2::init(const PipelineConfig& cfg) {
    if (initialised_) {
        std::cerr << "[CameraHalV4L2] Already initialised\n";
        return true;
    }

    cfg_ = cfg;

    std::cout << "[CameraHalV4L2] Initialising generic V4L2 camera pipeline\n";
    std::cout << "[CameraHalV4L2]   Full:   " << cfg.camera.full_w << "x" << cfg.camera.full_h << "\n";
    std::cout << "[CameraHalV4L2]   Medium: " << cfg.camera.med_w << "x" << cfg.camera.med_h << "\n";
    std::cout << "[CameraHalV4L2]   Lores:  " << cfg.camera.lores_w << "x" << cfg.camera.lores_h << "\n";
    std::cout << "[CameraHalV4L2]   FPS:    " << cfg.camera.fps << "\n";
    std::cout << "[CameraHalV4L2]   Device: " << cfg.camera.device << "\n";

    // 1. Discover and open V4L2 device
    if (!init_v4l2()) {
        std::cerr << "[CameraHalV4L2] V4L2 initialisation failed\n";
        return false;
    }

    // 2. Pre-allocate scaler output buffers for medium and lores
    uint32_t med_size  = static_cast<uint32_t>(cfg.camera.med_w * cfg.camera.med_h * 3 / 2);
    uint32_t lores_size = static_cast<uint32_t>(cfg.camera.lores_w * cfg.camera.lores_h * 3 / 2);

    scaler_medium_.addr = std::malloc(med_size);
    scaler_medium_.size = med_size;
    scaler_lores_.addr  = std::malloc(lores_size);
    scaler_lores_.size  = lores_size;

    if (!scaler_medium_.addr || !scaler_lores_.addr) {
        std::cerr << "[CameraHalV4L2] Failed to allocate scaler buffers\n";
        shutdown();
        return false;
    }

    initialised_ = true;
    std::cout << "[CameraHalV4L2] Initialised successfully ("
              << cfg.camera.full_w << "x" << cfg.camera.full_h << " NV12 via V4L2)\n";
    return true;
}

// ─── discover_device ──────────────────────────────────────────────────────────
int CameraHalV4L2::discover_device() {
    const char* configured = cfg_.camera.device.c_str();

    // 1. Try the configured device path first
    int fd = try_open_v4l2(configured);
    if (fd >= 0) {
        std::cout << "[CameraHalV4L2] Using configured device: " << configured << "\n";
        return fd;
    }

    // 2. Scan sysfs for ISP/streaming capture devices
    std::cout << "[CameraHalV4L2] Scanning for V4L2 capture devices...\n";
    for (int i = 0; i <= 31; ++i) {
        char sysfs_path[64];
        snprintf(sysfs_path, sizeof(sysfs_path),
                 "/sys/class/video4linux/video%d/name", i);
        FILE* f = fopen(sysfs_path, "r");
        if (f) {
            char name[128] = {};
            if (fgets(name, sizeof(name), f)) {
                size_t len = strlen(name);
                if (len > 0 && name[len - 1] == '\n') name[len - 1] = '\0';

                // Look for ISP mainpath, streaming, or capture devices
                // This covers: rkisp_mainpath, isp_mainpath, isp, stream,
                //             sunxi-vin, hbn, uvcvideo, etc.
                if (strstr(name, "rkisp_mainpath") ||
                    strstr(name, "isp_mainpath") ||
                    strstr(name, "sunxi-vin") ||
                    strstr(name, "hbn") ||
                    strstr(name, "stream")) {
                    char dev_path[32];
                    snprintf(dev_path, sizeof(dev_path), "/dev/video%d", i);
                    fd = try_open_v4l2(dev_path);
                    if (fd >= 0) {
                        std::cout << "[CameraHalV4L2] Found capture device: "
                                  << dev_path << " (" << name << ")\n";
                        fclose(f);
                        return fd;
                    }
                }
            }
            fclose(f);
        }
    }

    // 3. Fallback: try common device numbers
    std::cout << "[CameraHalV4L2] Trying common device numbers...\n";
    const int common_devices[] = {0, 1, 2, 3, 4, 5, 11, 21, 31};
    for (int dev_num : common_devices) {
        char alt_dev[32];
        snprintf(alt_dev, sizeof(alt_dev), "/dev/video%d", dev_num);
        fd = try_open_v4l2(alt_dev);
        if (fd >= 0) {
            std::cout << "[CameraHalV4L2] Found working device: " << alt_dev << "\n";
            return fd;
        }
    }

    std::cerr << "[CameraHalV4L2] No V4L2 capture device found\n";
    return -1;
}

// ─── Media controller pipeline ────────────────────────────────────────────────
// Some drivers (sunxi-vin on Allwinner A733) use the media controller API and
// require the sensor subdev to be configured before VIDIOC_S_FMT will succeed.
// This function attempts that; failures are non-fatal.
/* static */
bool CameraHalV4L2::try_configure_media_pipeline(int video_fd, int width, int height, int fps) {
    // Get video node bus_info to find the matching media device
    struct v4l2_capability cap{};
    if (ioctl(video_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        return false;
    }

    // Try each media device
    for (int med_idx = 0; med_idx <= 3; ++med_idx) {
        char media_path[32];
        snprintf(media_path, sizeof(media_path), "/dev/media%d", med_idx);

        int media_fd = ::open(media_path, O_RDWR, 0);
        if (media_fd < 0) continue;

        struct media_device_info med_info{};
        if (ioctl(media_fd, MEDIA_IOC_DEVICE_INFO, &med_info) < 0) {
            ::close(media_fd);
            continue;
        }

        // Find sensor subdev and video capture entities
        int sensor_subdev_fd = -1;
        int sensor_entity_id = -1;
        char sensor_dev_path[64] = {};

        struct media_entity_desc entity{};
        entity.id = MEDIA_ENT_ID_FLAG_NEXT;
        while (ioctl(media_fd, MEDIA_IOC_ENUM_ENTITIES, &entity) == 0) {
            // Check for sensor subdev
            if (entity.type == MEDIA_ENT_T_V4L2_SUBDEV_SENSOR ||
                entity.type == MEDIA_ENT_T_V4L2_SUBDEV) {
                sensor_entity_id = entity.id;
            }
            entity.id |= MEDIA_ENT_ID_FLAG_NEXT;
        }

        if (sensor_entity_id < 0) {
            ::close(media_fd);
            continue;
        }

        // Get sensor entity info with devnode
        struct media_entity_desc sensor_entity{};
        sensor_entity.id = static_cast<__u32>(sensor_entity_id);
        if (ioctl(media_fd, MEDIA_IOC_ENUM_ENTITIES, &sensor_entity) < 0 ||
            sensor_entity.dev.major <= 0) {
            ::close(media_fd);
            continue;
        }

        // Find subdev device by major/minor
        for (int sd = 0; sd <= 31; ++sd) {
            char sd_path[32];
            snprintf(sd_path, sizeof(sd_path), "/dev/v4l-subdev%d", sd);
            struct stat st{};
            if (stat(sd_path, &st) == 0 &&
                major(st.st_rdev) == sensor_entity.dev.major &&
                minor(st.st_rdev) == sensor_entity.dev.minor) {
                strncpy(sensor_dev_path, sd_path, sizeof(sensor_dev_path) - 1);
                break;
            }
        }

        if (sensor_dev_path[0] == '\0') {
            ::close(media_fd);
            continue;
        }

        // Open sensor subdev
        sensor_subdev_fd = ::open(sensor_dev_path, O_RDWR, 0);
        if (sensor_subdev_fd < 0) {
            ::close(media_fd);
            continue;
        }

        // Configure sensor subdev format
        struct v4l2_subdev_format sd_fmt{};
        sd_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
        sd_fmt.pad = 0;
        sd_fmt.format.width  = static_cast<__u32>(width);
        sd_fmt.format.height = static_cast<__u32>(height);

        // Try common mbus codes
        uint32_t mbus_codes[] = {
            MEDIA_BUS_FMT_SBGGR10_1X10,
            MEDIA_BUS_FMT_SRGGB10_1X10,
            MEDIA_BUS_FMT_SGRBG10_1X10,
            MEDIA_BUS_FMT_SGBRG10_1X10,
            MEDIA_BUS_FMT_SBGGR12_1X12,
            MEDIA_BUS_FMT_SRGGB12_1X12,
            MEDIA_BUS_FMT_SBGGR8_1X8,
            MEDIA_BUS_FMT_SRGGB8_1X8,
            MEDIA_BUS_FMT_UYVY8_1X16,
            MEDIA_BUS_FMT_YUYV8_1X16,
        };

        bool subdev_fmt_ok = false;
        for (auto mbus_code : mbus_codes) {
            sd_fmt.format.code = mbus_code;
            if (ioctl(sensor_subdev_fd, VIDIOC_SUBDEV_S_FMT, &sd_fmt) == 0) {
                subdev_fmt_ok = true;
                break;
            }
        }

        if (!subdev_fmt_ok) {
            // Enumerate sensor formats
            struct v4l2_subdev_mbus_code_enum mbus_enum{};
            mbus_enum.pad = 0;
            mbus_enum.which = V4L2_SUBDEV_FORMAT_ACTIVE;
            mbus_enum.index = 0;
            while (ioctl(sensor_subdev_fd, VIDIOC_SUBDEV_ENUM_MBUS_CODE, &mbus_enum) == 0) {
                sd_fmt.format.code = mbus_enum.code;
                if (ioctl(sensor_subdev_fd, VIDIOC_SUBDEV_S_FMT, &sd_fmt) == 0) {
                    subdev_fmt_ok = true;
                    break;
                }
                mbus_enum.index++;
            }
        }

        // Set frame interval
        if (subdev_fmt_ok) {
            struct v4l2_subdev_frame_interval fi{};
            fi.pad = 0;
            fi.interval.numerator = 1;
            fi.interval.denominator = static_cast<__u32>(fps);
            ioctl(sensor_subdev_fd, VIDIOC_SUBDEV_S_FRAME_INTERVAL, &fi);
        }

        ::close(sensor_subdev_fd);
        ::close(media_fd);

        if (subdev_fmt_ok) {
            std::cout << "[CameraHalV4L2] Media pipeline configured successfully\n";
            return true;
        }
    }

    std::cout << "[CameraHalV4L2] Media pipeline not required or not supported\n";
    return false;
}

// ─── init_v4l2 ────────────────────────────────────────────────────────────────
bool CameraHalV4L2::init_v4l2() {
    // 1. Discover and open the capture device
    v4l2_fd_ = discover_device();
    if (v4l2_fd_ < 0) return false;

    // Detect multiplanar vs single-planar API
    mplane_ = is_mplane(v4l2_fd_);
    buf_type_ = capture_buf_type(mplane_);
    std::cout << "[CameraHalV4L2] Detected " << (mplane_ ? "multiplanar" : "single-planar")
              << " V4L2 API\n";

    // 2. Attempt media controller pipeline configuration
    //    Non-fatal — only needed by certain drivers (sunxi-vin)
    try_configure_media_pipeline(v4l2_fd_,
                                 static_cast<int>(cfg_.camera.full_w),
                                 static_cast<int>(cfg_.camera.full_h),
                                 static_cast<int>(cfg_.camera.fps));

    // 3. Enumerate supported pixel formats
    std::vector<uint32_t> supported_formats;
    {
        struct v4l2_fmtdesc fmtdesc{};
        fmtdesc.type = buf_type_;
        fmtdesc.index = 0;
        while (ioctl(v4l2_fd_, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
            supported_formats.push_back(fmtdesc.pixelformat);
            std::cout << "[CameraHalV4L2]   Supports: "
                      << v4l2_fmt_name(fmtdesc.pixelformat) << "\n";
            fmtdesc.index++;
        }
    }

    // 4. Set format: NV12 preferred, fall back to first supported format
    std::vector<uint32_t> format_prefs;
    format_prefs.push_back(V4L2_PIX_FMT_NV12);
    // YUYV is a good fallback — almost every UVC webcam supports it
    format_prefs.push_back(V4L2_PIX_FMT_YUYV);
    format_prefs.push_back(V4L2_PIX_FMT_UYVY);
    // Add all driver-supported formats after our preferences
    for (auto fmt : supported_formats) {
        if (fmt != V4L2_PIX_FMT_NV12 &&
            fmt != V4L2_PIX_FMT_YUYV &&
            fmt != V4L2_PIX_FMT_UYVY) {
            format_prefs.push_back(fmt);
        }
    }

    uint32_t selected_fmt = 0;
    bool fmt_ok = false;

    for (auto try_fmt : format_prefs) {
        if (mplane_) {
            struct v4l2_format fmt{};
            fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            fmt.fmt.pix_mp.width       = static_cast<__u32>(cfg_.camera.full_w);
            fmt.fmt.pix_mp.height      = static_cast<__u32>(cfg_.camera.full_h);
            fmt.fmt.pix_mp.pixelformat = try_fmt;
            fmt.fmt.pix_mp.field       = V4L2_FIELD_NONE;
            fmt.fmt.pix_mp.num_planes  = 1;

            if (ioctl(v4l2_fd_, VIDIOC_S_FMT, &fmt) == 0) {
                std::cout << "[CameraHalV4L2] Format set (MPLANE): "
                          << fmt.fmt.pix_mp.width << "x"
                          << fmt.fmt.pix_mp.height << " "
                          << v4l2_fmt_name(fmt.fmt.pix_mp.pixelformat)
                          << " stride=" << fmt.fmt.pix_mp.plane_fmt[0].bytesperline << "\n";
                selected_fmt = fmt.fmt.pix_mp.pixelformat;
                v4l2_stride_ = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
                fmt_ok = true;
                break;
            }
        } else {
            struct v4l2_format fmt{};
            fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            fmt.fmt.pix.width       = static_cast<__u32>(cfg_.camera.full_w);
            fmt.fmt.pix.height      = static_cast<__u32>(cfg_.camera.full_h);
            fmt.fmt.pix.pixelformat = try_fmt;
            fmt.fmt.pix.field       = V4L2_FIELD_NONE;

            if (ioctl(v4l2_fd_, VIDIOC_S_FMT, &fmt) == 0) {
                std::cout << "[CameraHalV4L2] Format set (single-planar): "
                          << fmt.fmt.pix.width << "x"
                          << fmt.fmt.pix.height << " "
                          << v4l2_fmt_name(fmt.fmt.pix.pixelformat)
                          << " stride=" << fmt.fmt.pix.bytesperline << "\n";
                selected_fmt = fmt.fmt.pix.pixelformat;
                v4l2_stride_ = fmt.fmt.pix.bytesperline;
                fmt_ok = true;
                break;
            }

            // If MPLANE failed, try single-planar
            if (mplane_) {
                mplane_ = false;
                buf_type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE;

                struct v4l2_format sfmt{};
                sfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                sfmt.fmt.pix.width       = static_cast<__u32>(cfg_.camera.full_w);
                sfmt.fmt.pix.height      = static_cast<__u32>(cfg_.camera.full_h);
                sfmt.fmt.pix.pixelformat = try_fmt;
                sfmt.fmt.pix.field       = V4L2_FIELD_NONE;

                if (ioctl(v4l2_fd_, VIDIOC_S_FMT, &sfmt) == 0) {
                    std::cout << "[CameraHalV4L2] Format set (single-planar fallback): "
                              << sfmt.fmt.pix.width << "x"
                              << sfmt.fmt.pix.height << " "
                              << v4l2_fmt_name(sfmt.fmt.pix.pixelformat)
                              << " stride=" << sfmt.fmt.pix.bytesperline << "\n";
                    selected_fmt = sfmt.fmt.pix.pixelformat;
                    v4l2_stride_ = sfmt.fmt.pix.bytesperline;
                    fmt_ok = true;
                    break;
                }
            }
        }
    }

    if (!fmt_ok) {
        std::cerr << "[CameraHalV4L2] Failed to set any V4L2 format\n";
        return false;
    }

    v4l2_pix_fmt_ = selected_fmt;
    std::cout << "[CameraHalV4L2] Using pixel format: "
              << v4l2_fmt_name(selected_fmt) << "\n";

    // 5. Set framerate
    struct v4l2_streamparm parm{};
    parm.type = buf_type_;
    parm.parm.capture.timeperframe.numerator   = 1;
    parm.parm.capture.timeperframe.denominator = static_cast<__u32>(cfg_.camera.fps);
    if (ioctl(v4l2_fd_, VIDIOC_S_PARM, &parm) < 0) {
        std::cerr << "[CameraHalV4L2] VIDIOC_S_PARM (fps) failed: " << strerror(errno) << "\n";
        // Non-fatal
    }

    // 6. Request MMAP buffers and start streaming
    uint32_t frame_size = cfg_.camera.full_w * cfg_.camera.full_h * 3 / 2;
    if (!init_v4l2_mmap(frame_size)) {
        return false;
    }

    // 7. Set V4L2 controls (exposure, gain, white balance)
    if (!set_v4l2_controls()) {
        std::cerr << "[CameraHalV4L2] Failed to set V4L2 controls (non-fatal)\n";
    }

    return true;
}

// ─── init_v4l2_mmap ───────────────────────────────────────────────────────────
bool CameraHalV4L2::init_v4l2_mmap(uint32_t frame_size) {
    (void)frame_size;  // Buffer sizes determined by the driver

    struct v4l2_requestbuffers req{};
    req.count  = 4;
    req.type   = buf_type_;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(v4l2_fd_, VIDIOC_REQBUFS, &req) < 0) {
        std::cerr << "[CameraHalV4L2] VIDIOC_REQBUFS failed: " << strerror(errno) << "\n";
        return false;
    }

    if (req.count < 2) {
        std::cerr << "[CameraHalV4L2] Insufficient buffers (" << req.count << ")\n";
        return false;
    }

    v4l2_buf_pool_.resize(req.count);

    for (unsigned int i = 0; i < req.count; ++i) {
        if (mplane_) {
            struct v4l2_buffer buf{};
            struct v4l2_plane plane{};
            buf.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory  = V4L2_MEMORY_MMAP;
            buf.index   = i;
            buf.m.planes = &plane;
            buf.length   = 1;

            if (ioctl(v4l2_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
                std::cerr << "[CameraHalV4L2] VIDIOC_QUERYBUF[" << i << "] failed: "
                          << strerror(errno) << "\n";
                return false;
            }

            v4l2_buf_pool_[i].index  = i;
            v4l2_buf_pool_[i].length = plane.length;
            v4l2_buf_pool_[i].mapped = mmap(nullptr, plane.length,
                                            PROT_READ | PROT_WRITE,
                                            MAP_SHARED,
                                            v4l2_fd_, plane.m.mem_offset);
            if (v4l2_buf_pool_[i].mapped == MAP_FAILED) {
                std::cerr << "[CameraHalV4L2] mmap[" << i << "] failed: "
                          << strerror(errno) << "\n";
                return false;
            }
        } else {
            struct v4l2_buffer buf{};
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index  = i;

            if (ioctl(v4l2_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
                std::cerr << "[CameraHalV4L2] VIDIOC_QUERYBUF[" << i << "] failed: "
                          << strerror(errno) << "\n";
                return false;
            }

            v4l2_buf_pool_[i].index  = i;
            v4l2_buf_pool_[i].length = buf.length;
            v4l2_buf_pool_[i].mapped = mmap(nullptr, buf.length,
                                            PROT_READ | PROT_WRITE,
                                            MAP_SHARED,
                                            v4l2_fd_, buf.m.offset);
            if (v4l2_buf_pool_[i].mapped == MAP_FAILED) {
                std::cerr << "[CameraHalV4L2] mmap[" << i << "] failed: "
                          << strerror(errno) << "\n";
                return false;
            }
        }
    }

    // Pre-queue all buffers
    for (auto& slot : v4l2_buf_pool_) {
        if (mplane_) {
            struct v4l2_buffer buf{};
            struct v4l2_plane plane{};
            buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory   = V4L2_MEMORY_MMAP;
            buf.index    = slot.index;
            buf.m.planes = &plane;
            buf.length   = 1;

            if (ioctl(v4l2_fd_, VIDIOC_QBUF, &buf) < 0) {
                std::cerr << "[CameraHalV4L2] VIDIOC_QBUF[" << slot.index << "] failed: "
                          << strerror(errno) << "\n";
                return false;
            }
        } else {
            struct v4l2_buffer buf{};
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index  = slot.index;

            if (ioctl(v4l2_fd_, VIDIOC_QBUF, &buf) < 0) {
                std::cerr << "[CameraHalV4L2] VIDIOC_QBUF[" << slot.index << "] failed: "
                          << strerror(errno) << "\n";
                return false;
            }
        }
        slot.queued = true;
    }

    // Start streaming
    int type = static_cast<int>(buf_type_);
    if (ioctl(v4l2_fd_, VIDIOC_STREAMON, &type) < 0) {
        std::cerr << "[CameraHalV4L2] VIDIOC_STREAMON failed: " << strerror(errno) << "\n";
        return false;
    }

    std::cout << "[CameraHalV4L2] Streaming started (" << req.count << " MMAP buffers)\n";
    return true;
}

// ─── set_v4l2_controls ────────────────────────────────────────────────────────
bool CameraHalV4L2::set_v4l2_controls() {
    struct v4l2_control ctrl{};

    // Auto exposure (enable AE)
    ctrl.id    = V4L2_CID_EXPOSURE_AUTO;
    ctrl.value = V4L2_EXPOSURE_AUTO;
    if (ioctl(v4l2_fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
        std::cerr << "[CameraHalV4L2] V4L2_CID_EXPOSURE_AUTO not supported: "
                  << strerror(errno) << "\n";
    }

    // Exposure time (in 100µs units for V4L2) — 20ms default
    ctrl.id    = V4L2_CID_EXPOSURE_ABSOLUTE;
    ctrl.value = 200;  // 200 × 100µs = 20ms
    if (ioctl(v4l2_fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
        std::cerr << "[CameraHalV4L2] V4L2_CID_EXPOSURE_ABSOLUTE not supported: "
                  << strerror(errno) << "\n";
    }

    // Analogue gain
    ctrl.id    = V4L2_CID_ANALOGUE_GAIN;
    ctrl.value = 10;
    if (ioctl(v4l2_fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
        std::cerr << "[CameraHalV4L2] V4L2_CID_ANALOGUE_GAIN not supported: "
                  << strerror(errno) << "\n";
    }

    // Auto white balance
    ctrl.id    = V4L2_CID_AUTO_WHITE_BALANCE;
    ctrl.value = 1;  // Enable
    if (ioctl(v4l2_fd_, VIDIOC_S_CTRL, &ctrl) < 0) {
        std::cerr << "[CameraHalV4L2] V4L2_CID_AUTO_WHITE_BALANCE not supported: "
                  << strerror(errno) << "\n";
    }

    return true;
}

// ─── acquire_frames ───────────────────────────────────────────────────────────
bool CameraHalV4L2::acquire_frames(FrameBuffer& full, FrameBuffer& medium,
                                   FrameBuffer& lores) {
    if (!initialised_) return false;

    // 1. Dequeue a frame (blocking, with poll timeout)
    struct v4l2_buffer buf{};
    struct v4l2_plane plane{};
    buf.type    = buf_type_;
    buf.memory  = V4L2_MEMORY_MMAP;
    if (mplane_) {
        buf.m.planes = &plane;
        buf.length   = 1;
    }

    // Use poll() to wait for data with timeout
    struct pollfd pfd;
    pfd.fd     = v4l2_fd_;
    pfd.events = POLLIN;

    int poll_ret = poll(&pfd, 1, 3000);  // 3 second timeout
    if (poll_ret <= 0) {
        if (poll_ret == 0) {
            std::cerr << "[CameraHalV4L2] Frame capture timeout\n";
        } else {
            std::cerr << "[CameraHalV4L2] poll() error: " << strerror(errno) << "\n";
        }
        return false;
    }

    if (ioctl(v4l2_fd_, VIDIOC_DQBUF, &buf) < 0) {
        std::cerr << "[CameraHalV4L2] VIDIOC_DQBUF failed: " << strerror(errno) << "\n";
        return false;
    }

    // Find the buffer slot
    V4l2BufSlot& slot = v4l2_buf_pool_[buf.index];
    slot.queued = false;

    // 2. Get timestamp
    int64_t timestamp_ms = 0;
    if (buf.flags & V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC) {
        timestamp_ms = static_cast<int64_t>(buf.timestamp.tv_sec) * 1000
                     + static_cast<int64_t>(buf.timestamp.tv_usec) / 1000;
    } else {
        timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // 3. Populate full frame buffer (points directly to MMAP buffer)
    uint32_t bytesused = mplane_ ? plane.bytesused : buf.bytesused;
    uint32_t buf_length = mplane_ ? plane.length : buf.length;

    frame_full_.data         = static_cast<uint8_t*>(slot.mapped);
    frame_full_.width        = static_cast<uint32_t>(cfg_.camera.full_w);
    frame_full_.height       = static_cast<uint32_t>(cfg_.camera.full_h);
    frame_full_.stride       = v4l2_stride_ > 0 ? v4l2_stride_ : static_cast<uint32_t>(cfg_.camera.full_w);
    frame_full_.size         = bytesused > 0 ? bytesused : buf_length;
    frame_full_.timestamp_ms = timestamp_ms;
    frame_full_.dma_fd       = -1;

    // 4. Scale to medium and lores using CPU bilinear scaler
    const uint8_t* src = static_cast<const uint8_t*>(slot.mapped);

    // Medium
    scale_nv12_cpu(src,
                   static_cast<uint8_t*>(scaler_medium_.addr),
                   cfg_.camera.full_w, cfg_.camera.full_h,
                   cfg_.camera.med_w, cfg_.camera.med_h);

    frame_medium_.data         = static_cast<uint8_t*>(scaler_medium_.addr);
    frame_medium_.width        = static_cast<uint32_t>(cfg_.camera.med_w);
    frame_medium_.height       = static_cast<uint32_t>(cfg_.camera.med_h);
    frame_medium_.stride       = static_cast<uint32_t>(cfg_.camera.med_w);
    frame_medium_.size         = scaler_medium_.size;
    frame_medium_.timestamp_ms = timestamp_ms;
    frame_medium_.dma_fd       = -1;

    // Lores
    scale_nv12_cpu(src,
                   static_cast<uint8_t*>(scaler_lores_.addr),
                   cfg_.camera.full_w, cfg_.camera.full_h,
                   cfg_.camera.lores_w, cfg_.camera.lores_h);

    frame_lores_.data         = static_cast<uint8_t*>(scaler_lores_.addr);
    frame_lores_.width        = static_cast<uint32_t>(cfg_.camera.lores_w);
    frame_lores_.height       = static_cast<uint32_t>(cfg_.camera.lores_h);
    frame_lores_.stride       = static_cast<uint32_t>(cfg_.camera.lores_w);
    frame_lores_.size         = scaler_lores_.size;
    frame_lores_.timestamp_ms = timestamp_ms;
    frame_lores_.dma_fd       = -1;

    // 5. Re-queue the buffer for the next frame
    if (mplane_) {
        struct v4l2_buffer qbuf{};
        struct v4l2_plane qplane{};
        qbuf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        qbuf.memory   = V4L2_MEMORY_MMAP;
        qbuf.index    = buf.index;
        qbuf.m.planes = &qplane;
        qbuf.length   = 1;

        if (ioctl(v4l2_fd_, VIDIOC_QBUF, &qbuf) < 0) {
            std::cerr << "[CameraHalV4L2] VIDIOC_QBUF (re-queue) failed: "
                      << strerror(errno) << "\n";
            return false;
        }
    } else {
        struct v4l2_buffer qbuf{};
        qbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        qbuf.memory = V4L2_MEMORY_MMAP;
        qbuf.index  = buf.index;

        if (ioctl(v4l2_fd_, VIDIOC_QBUF, &qbuf) < 0) {
            std::cerr << "[CameraHalV4L2] VIDIOC_QBUF (re-queue) failed: "
                      << strerror(errno) << "\n";
            return false;
        }
    }
    slot.queued = true;

    // 6. Assign output references
    full   = frame_full_;
    medium = frame_medium_;
    lores  = frame_lores_;

    return true;
}

// ─── release_frames ───────────────────────────────────────────────────────────
void CameraHalV4L2::release_frames() {
    // No-op: V4L2 MMAP buffers are managed by the kernel.
    // Scaler output buffers persist until the next acquire_frames() call.
}

// ─── shutdown ─────────────────────────────────────────────────────────────────
void CameraHalV4L2::shutdown() {
    if (!initialised_ && v4l2_fd_ < 0) return;

    if (initialised_) {
        std::cout << "[CameraHalV4L2] Shutting down\n";
    }

    // Stop streaming
    if (v4l2_fd_ >= 0) {
        int type = static_cast<int>(buf_type_);
        ioctl(v4l2_fd_, VIDIOC_STREAMOFF, &type);
    }

    // Unmap V4L2 buffers
    for (auto& slot : v4l2_buf_pool_) {
        if (slot.mapped && slot.mapped != MAP_FAILED) {
            munmap(slot.mapped, slot.length);
        }
    }
    v4l2_buf_pool_.clear();

    // Close V4L2 device
    if (v4l2_fd_ >= 0) {
        ::close(v4l2_fd_);
        v4l2_fd_ = -1;
    }

    // Free scaler buffers
    if (scaler_medium_.addr) {
        std::free(scaler_medium_.addr);
        scaler_medium_.addr = nullptr;
        scaler_medium_.size = 0;
    }
    if (scaler_lores_.addr) {
        std::free(scaler_lores_.addr);
        scaler_lores_.addr = nullptr;
        scaler_lores_.size = 0;
    }

    initialised_ = false;
    if (v4l2_fd_ < 0) return;  // Already cleaned up
    std::cout << "[CameraHalV4L2] Shutdown complete\n";
}

// ─── scale_nv12_cpu ──────────────────────────────────────────────────────────
// CPU-based bilinear scaler for NV12 frames.
// Scales the Y plane (luma) and UV plane (chroma) separately.
/* static */
void CameraHalV4L2::scale_nv12_cpu(const uint8_t* src, uint8_t* dst,
                                   int src_w, int src_h,
                                   int dst_w, int dst_h) {
    if (!src || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return;

    // Scale Y plane (luma)
    float scale_x = static_cast<float>(src_w) / static_cast<float>(dst_w);
    float scale_y = static_cast<float>(src_h) / static_cast<float>(dst_h);

    for (int dy = 0; dy < dst_h; ++dy) {
        float src_y_f = dy * scale_y;
        int sy0 = static_cast<int>(src_y_f);
        int sy1 = std::min(sy0 + 1, src_h - 1);
        float fy = src_y_f - sy0;

        for (int dx = 0; dx < dst_w; ++dx) {
            float src_x_f = dx * scale_x;
            int sx0 = static_cast<int>(src_x_f);
            int sx1 = std::min(sx0 + 1, src_w - 1);
            float fx = src_x_f - sx0;

            // Bilinear interpolation
            float top = (1.0f - fx) * src[sy0 * src_w + sx0]
                      + fx * src[sy0 * src_w + sx1];
            float bottom = (1.0f - fx) * src[sy1 * src_w + sx0]
                         + fx * src[sy1 * src_w + sx1];
            float val = (1.0f - fy) * top + fy * bottom;
            dst[dy * dst_w + dx] = static_cast<uint8_t>(val + 0.5f);
        }
    }

    // Scale UV plane (chroma)
    // NV12 UV plane is at half resolution: (src_w/2) × (src_h/2)
    int src_w_uv = src_w / 2;
    int src_h_uv = src_h / 2;
    int dst_w_uv = dst_w / 2;
    int dst_h_uv = dst_h / 2;

    const uint8_t* src_uv = src + src_w * src_h;
    uint8_t* dst_uv = dst + dst_w * dst_h;

    float scale_x_uv = static_cast<float>(src_w_uv) / static_cast<float>(dst_w_uv);
    float scale_y_uv = static_cast<float>(src_h_uv) / static_cast<float>(dst_h_uv);

    for (int dy = 0; dy < dst_h_uv; ++dy) {
        float src_y_f = dy * scale_y_uv;
        int sy0 = static_cast<int>(src_y_f);
        int sy1 = std::min(sy0 + 1, src_h_uv - 1);
        float fy = src_y_f - sy0;

        for (int dx = 0; dx < dst_w_uv; ++dx) {
            float src_x_f = dx * scale_x_uv;
            int sx0 = static_cast<int>(src_x_f);
            int sx1 = std::min(sx0 + 1, src_w_uv - 1);
            float fx = src_x_f - sx0;

            // Each UV sample is 2 bytes (U, V interleaved)
            int src_idx0 = (sy0 * src_w_uv + sx0) * 2;
            int src_idx1 = (sy0 * src_w_uv + sx1) * 2;
            int src_idx2 = (sy1 * src_w_uv + sx0) * 2;
            int src_idx3 = (sy1 * src_w_uv + sx1) * 2;
            int dst_idx = (dy * dst_w_uv + dx) * 2;

            for (int c = 0; c < 2; ++c) {
                float top = (1.0f - fx) * src_uv[src_idx0 + c]
                          + fx * src_uv[src_idx1 + c];
                float bottom = (1.0f - fx) * src_uv[src_idx2 + c]
                             + fx * src_uv[src_idx3 + c];
                float val = (1.0f - fy) * top + fy * bottom;
                dst_uv[dst_idx + c] = static_cast<uint8_t>(val + 0.5f);
            }
        }
    }
}

} // namespace ct