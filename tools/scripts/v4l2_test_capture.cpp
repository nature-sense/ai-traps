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

// ─── v4l2_test_capture.cpp ─────────────────────────────────────────────────────
// Standalone V4L2 camera capture test utility for the Radxa Cubie A7S.
//
// This tool validates that the camera pipeline works WITHOUT needing the full
// AI Trap build system. It:
//   1. Scans /dev/video* devices to find the ISP-processed node
//   2. Captures a single NV12 frame
//   3. Saves it to a raw file for inspection
//
// Compile on the Cubie A7S board:
//   g++ -o v4l2_test_capture v4l2_test_capture.cpp -lv4l2
//
// Run:
//   ./v4l2_test_capture [--device /dev/videoN] [--output test.raw]
//                       [--width 1920] [--height 1080] [--count 1]
//
// Quick validation (capture one frame and convert with ffmpeg):
//   ./v4l2_test_capture --output test.raw
//   ffmpeg -f rawvideo -pixel_format nv12 -video_size 1920x1080 \
//          -i test.raw -frames:v 1 test.jpg
//
// List all video devices:
//   ./v4l2_test_capture --list

#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <string>
#include <algorithm>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <poll.h>
#include <linux/videodev2.h>
#include <linux/media.h>
#include <linux/v4l2-subdev.h>
#include <errno.h>

// ─── Helpers ──────────────────────────────────────────────────────────────────

static const char* v4l2_fmt_name(uint32_t fourcc) {
    static char buf[5];
    buf[0] = (fourcc >>  0) & 0xFF;
    buf[1] = (fourcc >>  8) & 0xFF;
    buf[2] = (fourcc >> 16) & 0xFF;
    buf[3] = (fourcc >> 24) & 0xFF;
    buf[4] = '\0';
    return buf;
}

static void print_caps(uint32_t caps) {
    if (caps & V4L2_CAP_VIDEO_CAPTURE)       std::cout << " VIDEO_CAPTURE";
    if (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) std::cout << " VIDEO_CAPTURE_MPLANE";
    if (caps & V4L2_CAP_VIDEO_OUTPUT)         std::cout << " VIDEO_OUTPUT";
    if (caps & V4L2_CAP_VIDEO_M2M)            std::cout << " VIDEO_M2M";
    if (caps & V4L2_CAP_STREAMING)            std::cout << " STREAMING";
    if (caps & V4L2_CAP_EXT_PIX_FORMAT)       std::cout << " EXT_PIX_FMT";
}

// ─── Device listing ───────────────────────────────────────────────────────────

static void list_devices() {
    std::cout << "\n=== V4L2 Video Devices ===\n\n";

    for (int i = 0; i <= 31; ++i) {
        char dev_path[32];
        snprintf(dev_path, sizeof(dev_path), "/dev/video%d", i);

        struct stat st;
        if (stat(dev_path, &st) != 0) continue;

        int fd = open(dev_path, O_RDWR, 0);
        if (fd < 0) {
            std::cout << dev_path << " (stat ok, open failed: "
                      << strerror(errno) << ")\n";
            continue;
        }

        struct v4l2_capability cap{};
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
            std::cout << dev_path << " (VIDIOC_QUERYCAP failed: "
                      << strerror(errno) << ")\n";
            close(fd);
            continue;
        }

        std::cout << dev_path << ":\n";
        std::cout << "  Driver:     " << cap.driver << "\n";
        std::cout << "  Card:       " << cap.card << "\n";
        std::cout << "  Bus info:   " << cap.bus_info << "\n";
        std::cout << "  Version:    " << ((cap.version >> 16) & 0xFF) << "."
                  << ((cap.version >> 8) & 0xFF) << "."
                  << (cap.version & 0xFF) << "\n";
        std::cout << "  Capabilities:";
        print_caps(cap.capabilities);
        std::cout << "\n";

        // Read sysfs name
        char sysfs_path[64];
        snprintf(sysfs_path, sizeof(sysfs_path),
                 "/sys/class/video4linux/video%d/name", i);
        FILE* f = fopen(sysfs_path, "r");
        if (f) {
            char name[128] = {};
            if (fgets(name, sizeof(name), f)) {
                size_t len = strlen(name);
                if (len > 0 && name[len - 1] == '\n') name[len - 1] = '\0';
                std::cout << "  Sysfs name: " << name << "\n";
            }
            fclose(f);
        }

        // Try to enumerate formats
        struct v4l2_fmtdesc fmt_desc{};
        fmt_desc.type = (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
                        ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                        : V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt_desc.index = 0;
        std::cout << "  Formats:";
        while (ioctl(fd, VIDIOC_ENUM_FMT, &fmt_desc) == 0) {
            std::cout << " " << v4l2_fmt_name(fmt_desc.pixelformat);
            fmt_desc.index++;
        }
        std::cout << "\n\n";

        close(fd);
    }
}

// ─── Media controller pipeline configuration ──────────────────────────────────
// The sunxi-vin driver on Allwinner A527 requires the sensor subdev to be
// configured via the media controller API before VIDIOC_S_FMT will succeed.
// This function finds the media device, locates the sensor subdev, and
// configures its format.

static bool configure_media_pipeline(int video_fd, int width, int height) {
    // Find the media device
    for (int med_idx = 0; med_idx <= 3; ++med_idx) {
        char media_path[32];
        snprintf(media_path, sizeof(media_path), "/dev/media%d", med_idx);

        int media_fd = ::open(media_path, O_RDWR, 0);
        if (media_fd < 0) continue;

        // Enumerate entities to find sensor subdev
        int sensor_entity_id = -1;
        struct media_entity_desc entity{};
        entity.id = MEDIA_ENT_ID_FLAG_NEXT;
        while (ioctl(media_fd, MEDIA_IOC_ENUM_ENTITIES, &entity) == 0) {
            if (entity.type == MEDIA_ENT_T_V4L2_SUBDEV_SENSOR ||
                (entity.type == MEDIA_ENT_T_V4L2_SUBDEV &&
                 strstr(entity.name, "imx415") != nullptr)) {
                sensor_entity_id = entity.id;
                std::cout << "  Found sensor subdev: " << entity.name << "\n";
            }
            entity.id |= MEDIA_ENT_ID_FLAG_NEXT;
        }

        if (sensor_entity_id < 0) {
            ::close(media_fd);
            continue;
        }

        // Find the sensor subdev device node
        struct media_entity_desc sensor_entity{};
        sensor_entity.id = static_cast<__u32>(sensor_entity_id);
        if (ioctl(media_fd, MEDIA_IOC_ENUM_ENTITIES, &sensor_entity) < 0 ||
            sensor_entity.dev.major == 0) {
            ::close(media_fd);
            continue;
        }

        char sensor_dev_path[64] = {};
        bool found = false;
        for (int sd = 0; sd <= 31; ++sd) {
            char sd_path[32];
            snprintf(sd_path, sizeof(sd_path), "/dev/v4l-subdev%d", sd);
            struct stat st{};
            if (stat(sd_path, &st) == 0 &&
                major(st.st_rdev) == sensor_entity.dev.major &&
                minor(st.st_rdev) == sensor_entity.dev.minor) {
                strncpy(sensor_dev_path, sd_path, sizeof(sensor_dev_path) - 1);
                found = true;
                break;
            }
        }

        if (!found) {
            ::close(media_fd);
            continue;
        }

        // Open and configure the sensor subdev
        int subdev_fd = ::open(sensor_dev_path, O_RDWR, 0);
        if (subdev_fd < 0) {
            ::close(media_fd);
            continue;
        }

        struct v4l2_subdev_format sd_fmt{};
        sd_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
        sd_fmt.pad = 0;
        sd_fmt.format.width = static_cast<__u32>(width);
        sd_fmt.format.height = static_cast<__u32>(height);

        // Try common mbus codes for IMX415
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

        bool ok = false;
        for (auto code : mbus_codes) {
            sd_fmt.format.code = code;
            if (ioctl(subdev_fd, VIDIOC_SUBDEV_S_FMT, &sd_fmt) == 0) {
                std::cout << "  Sensor subdev format: "
                          << sd_fmt.format.width << "x" << sd_fmt.format.height
                          << " code=0x" << std::hex << sd_fmt.format.code
                          << std::dec << "\n";
                ok = true;
                break;
            }
        }

        if (!ok) {
            std::cerr << "  Sensor subdev configuration failed\n";
            ::close(subdev_fd);
            ::close(media_fd);
            continue;
        }
        std::cout << "  Sensor subdev configured\n";

        // 2. Configure ISP subdev (sunxi_isp.0)
        // The ISP subdev is typically v4l-subdev13 on this platform
        // We need to set both input (pad0) and output (pad2) formats
        int isp_subdev_fd = -1;
        for (int sd = 0; sd <= 31; ++sd) {
            char sd_path[32];
            snprintf(sd_path, sizeof(sd_path), "/dev/v4l-subdev%d", sd);
            int tfd = ::open(sd_path, O_RDWR, 0);
            if (tfd >= 0) {
                struct media_entity_desc ent{};
                ent.id = MEDIA_ENT_ID_FLAG_NEXT;
                while (ioctl(media_fd, MEDIA_IOC_ENUM_ENTITIES, &ent) == 0) {
                    struct stat st{};
                    if (fstat(tfd, &st) == 0 &&
                        ent.dev.major == major(st.st_rdev) &&
                        ent.dev.minor == minor(st.st_rdev)) {
                        if (strstr(ent.name, "sunxi_isp") != nullptr) {
                            isp_subdev_fd = tfd;
                            std::cout << "  Found ISP subdev: " << ent.name
                                      << " (" << sd_path << ")\n";
                            break;
                        }
                    }
                    ent.id |= MEDIA_ENT_ID_FLAG_NEXT;
                }
                if (isp_subdev_fd < 0) {
                    ::close(tfd);
                } else {
                    break;
                }
            }
        }

        if (isp_subdev_fd >= 0) {
            // Set ISP input format (pad0) - same as sensor output
            struct v4l2_subdev_format isp_fmt{};
            isp_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
            isp_fmt.pad = 0;
            isp_fmt.format.width = static_cast<__u32>(width);
            isp_fmt.format.height = static_cast<__u32>(height);
            isp_fmt.format.code = sd_fmt.format.code;  // Same mbus code as sensor

            if (ioctl(isp_subdev_fd, VIDIOC_SUBDEV_S_FMT, &isp_fmt) == 0) {
                std::cout << "  ISP pad0 format: " << isp_fmt.format.width << "x"
                          << isp_fmt.format.height << " code=0x"
                          << std::hex << isp_fmt.format.code << std::dec << "\n";
            }

            // Set ISP output format (pad2) - try YUYV
            isp_fmt.pad = 2;
            isp_fmt.format.code = MEDIA_BUS_FMT_YUYV8_1X16;
            if (ioctl(isp_subdev_fd, VIDIOC_SUBDEV_S_FMT, &isp_fmt) == 0) {
                std::cout << "  ISP pad2 format: " << isp_fmt.format.width << "x"
                          << isp_fmt.format.height << " code=0x"
                          << std::hex << isp_fmt.format.code << std::dec << "\n";
            }

            ::close(isp_subdev_fd);
        }

        // 3. Configure scaler subdev (sunxi_scaler.0)
        int scaler_subdev_fd = -1;
        for (int sd = 0; sd <= 31; ++sd) {
            char sd_path[32];
            snprintf(sd_path, sizeof(sd_path), "/dev/v4l-subdev%d", sd);
            int tfd = ::open(sd_path, O_RDWR, 0);
            if (tfd >= 0) {
                struct media_entity_desc ent{};
                ent.id = MEDIA_ENT_ID_FLAG_NEXT;
                while (ioctl(media_fd, MEDIA_IOC_ENUM_ENTITIES, &ent) == 0) {
                    struct stat st{};
                    if (fstat(tfd, &st) == 0 &&
                        ent.dev.major == major(st.st_rdev) &&
                        ent.dev.minor == minor(st.st_rdev)) {
                        if (strstr(ent.name, "sunxi_scaler") != nullptr) {
                            scaler_subdev_fd = tfd;
                            std::cout << "  Found scaler subdev: " << ent.name
                                      << " (" << sd_path << ")\n";
                            break;
                        }
                    }
                    ent.id |= MEDIA_ENT_ID_FLAG_NEXT;
                }
                if (scaler_subdev_fd < 0) {
                    ::close(tfd);
                } else {
                    break;
                }
            }
        }

        if (scaler_subdev_fd >= 0) {
            // Set scaler input format (pad0)
            struct v4l2_subdev_format sc_fmt{};
            sc_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
            sc_fmt.pad = 0;
            sc_fmt.format.width = static_cast<__u32>(width);
            sc_fmt.format.height = static_cast<__u32>(height);
            sc_fmt.format.code = MEDIA_BUS_FMT_YUYV8_1X16;

            if (ioctl(scaler_subdev_fd, VIDIOC_SUBDEV_S_FMT, &sc_fmt) == 0) {
                std::cout << "  Scaler pad0 format: " << sc_fmt.format.width << "x"
                          << sc_fmt.format.height << " code=0x"
                          << std::hex << sc_fmt.format.code << std::dec << "\n";
            }

            // Set scaler output format (pad1)
            sc_fmt.pad = 1;
            if (ioctl(scaler_subdev_fd, VIDIOC_SUBDEV_S_FMT, &sc_fmt) == 0) {
                std::cout << "  Scaler pad1 format: " << sc_fmt.format.width << "x"
                          << sc_fmt.format.height << " code=0x"
                          << std::hex << sc_fmt.format.code << std::dec << "\n";
            }

            ::close(scaler_subdev_fd);
        }

        ::close(subdev_fd);
        ::close(media_fd);
        std::cout << "  Media pipeline configured\n";
        return true;
    }

    std::cerr << "  Media pipeline configuration failed\n";
    return false;
}

// ─── Frame capture ────────────────────────────────────────────────────────────

struct CaptureContext {
    int fd = -1;
    bool mplane = false;
    uint32_t buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    struct Buffer {
        void*    data   = nullptr;
        uint32_t length = 0;
    };
    std::vector<Buffer> buffers;
};

static bool capture_frame(CaptureContext& ctx,
                          const char* output_path,
                          int width, int height,
                          int pixelformat) {
    // 1. Configure media pipeline (required by sunxi-vin driver)
    std::cout << "Configuring media pipeline...\n";
    configure_media_pipeline(ctx.fd, width, height);

    // 2. Set format - try multiplanar first, fall back to single-planar
    if (ctx.mplane) {
        struct v4l2_format fmt{};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        fmt.fmt.pix_mp.width       = static_cast<__u32>(width);
        fmt.fmt.pix_mp.height      = static_cast<__u32>(height);
        fmt.fmt.pix_mp.pixelformat = static_cast<__u32>(pixelformat);
        fmt.fmt.pix_mp.field       = V4L2_FIELD_NONE;
        fmt.fmt.pix_mp.num_planes  = 1;

        if (ioctl(ctx.fd, VIDIOC_S_FMT, &fmt) < 0) {
            std::cerr << "VIDIOC_S_FMT (MPLANE) failed: " << strerror(errno) << "\n";
            return false;
        }
        std::cout << "Format set: " << fmt.fmt.pix_mp.width << "x"
                  << fmt.fmt.pix_mp.height << " "
                  << v4l2_fmt_name(fmt.fmt.pix_mp.pixelformat)
                  << " stride=" << fmt.fmt.pix_mp.plane_fmt[0].bytesperline << "\n";
    } else {
        struct v4l2_format fmt{};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width       = static_cast<__u32>(width);
        fmt.fmt.pix.height      = static_cast<__u32>(height);
        fmt.fmt.pix.pixelformat = static_cast<__u32>(pixelformat);
        fmt.fmt.pix.field       = V4L2_FIELD_NONE;

        if (ioctl(ctx.fd, VIDIOC_S_FMT, &fmt) < 0) {
            std::cerr << "VIDIOC_S_FMT failed: " << strerror(errno) << "\n";
            return false;
        }
        std::cout << "Format set: " << fmt.fmt.pix.width << "x"
                  << fmt.fmt.pix.height << " "
                  << v4l2_fmt_name(fmt.fmt.pix.pixelformat)
                  << " stride=" << fmt.fmt.pix.bytesperline << "\n";
    }

    // 2. Request buffers
    struct v4l2_requestbuffers req{};
    req.count  = 4;
    req.type   = ctx.buf_type;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(ctx.fd, VIDIOC_REQBUFS, &req) < 0) {
        std::cerr << "VIDIOC_REQBUFS failed: " << strerror(errno) << "\n";
        return false;
    }
    std::cout << "Requested " << req.count << " buffers\n";

    ctx.buffers.resize(req.count);

    // 3. Query and mmap buffers
    for (unsigned int i = 0; i < req.count; ++i) {
        if (ctx.mplane) {
            struct v4l2_buffer buf{};
            struct v4l2_plane plane{};
            buf.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory  = V4L2_MEMORY_MMAP;
            buf.index   = i;
            buf.m.planes = &plane;
            buf.length   = 1;

            if (ioctl(ctx.fd, VIDIOC_QUERYBUF, &buf) < 0) {
                std::cerr << "VIDIOC_QUERYBUF[" << i << "] failed: "
                          << strerror(errno) << "\n";
                return false;
            }

            ctx.buffers[i].length = plane.length;
            ctx.buffers[i].data = mmap(nullptr, plane.length,
                                       PROT_READ | PROT_WRITE, MAP_SHARED,
                                       ctx.fd, plane.m.mem_offset);
        } else {
            struct v4l2_buffer buf{};
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index  = i;

            if (ioctl(ctx.fd, VIDIOC_QUERYBUF, &buf) < 0) {
                std::cerr << "VIDIOC_QUERYBUF[" << i << "] failed: "
                          << strerror(errno) << "\n";
                return false;
            }

            ctx.buffers[i].length = buf.length;
            ctx.buffers[i].data = mmap(nullptr, buf.length,
                                       PROT_READ | PROT_WRITE, MAP_SHARED,
                                       ctx.fd, buf.m.offset);
        }

        if (ctx.buffers[i].data == MAP_FAILED) {
            std::cerr << "mmap[" << i << "] failed: " << strerror(errno) << "\n";
            return false;
        }
        std::cout << "Buffer " << i << ": " << ctx.buffers[i].length
                  << " bytes @ " << ctx.buffers[i].data << "\n";
    }

    // 4. Queue all buffers
    for (unsigned int i = 0; i < req.count; ++i) {
        if (ctx.mplane) {
            struct v4l2_buffer buf{};
            struct v4l2_plane plane{};
            buf.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory  = V4L2_MEMORY_MMAP;
            buf.index   = i;
            buf.m.planes = &plane;
            buf.length   = 1;

            if (ioctl(ctx.fd, VIDIOC_QBUF, &buf) < 0) {
                std::cerr << "VIDIOC_QBUF[" << i << "] failed: "
                          << strerror(errno) << "\n";
                return false;
            }
        } else {
            struct v4l2_buffer buf{};
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index  = i;

            if (ioctl(ctx.fd, VIDIOC_QBUF, &buf) < 0) {
                std::cerr << "VIDIOC_QBUF[" << i << "] failed: "
                          << strerror(errno) << "\n";
                return false;
            }
        }
    }

    // 5. Start streaming
    {
        int type = static_cast<int>(ctx.buf_type);
        if (ioctl(ctx.fd, VIDIOC_STREAMON, &type) < 0) {
            std::cerr << "VIDIOC_STREAMON failed: " << strerror(errno) << "\n";
            return false;
        }
    }
    std::cout << "Streaming started\n";

    // 6. Capture one frame
    {
        struct v4l2_buffer buf{};
        struct v4l2_plane plane{};
        buf.type   = ctx.buf_type;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ctx.mplane) {
            buf.m.planes = &plane;
            buf.length   = 1;
        }

        struct pollfd pfd;
        pfd.fd     = ctx.fd;
        pfd.events = POLLIN;

        std::cout << "Waiting for frame...\n";
        int poll_ret = poll(&pfd, 1, 5000);  // 5 second timeout
        if (poll_ret <= 0) {
            std::cerr << "poll() timeout or error: " << strerror(errno) << "\n";
            return false;
        }

        if (ioctl(ctx.fd, VIDIOC_DQBUF, &buf) < 0) {
            std::cerr << "VIDIOC_DQBUF failed: " << strerror(errno) << "\n";
            return false;
        }

        uint32_t bytesused = ctx.mplane ? plane.bytesused : buf.bytesused;
        uint32_t buf_length = ctx.mplane ? plane.length : buf.length;
        uint32_t data_size = (bytesused > 0) ? bytesused : buf_length;

        std::cout << "Frame captured: index=" << buf.index
                  << " bytesused=" << bytesused
                  << " length=" << buf_length << "\n";

        // 7. Write to file
        std::ofstream out(output_path, std::ios::binary);
        if (!out) {
            std::cerr << "Failed to open output file: " << output_path << "\n";
            return false;
        }
        out.write(static_cast<const char*>(ctx.buffers[buf.index].data),
                  data_size);
        out.close();

        std::cout << "Saved " << data_size << " bytes to " << output_path << "\n";
    }

    // 8. Stop streaming
    {
        int type = static_cast<int>(ctx.buf_type);
        ioctl(ctx.fd, VIDIOC_STREAMOFF, &type);
    }

    // 9. Cleanup
    for (auto& b : ctx.buffers) {
        if (b.data && b.data != MAP_FAILED) {
            munmap(b.data, b.length);
        }
    }
    ctx.buffers.clear();

    return true;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::string device_path;
    std::string output_path = "test.raw";
    int width  = 1920;
    int height = 1080;
    int count  = 1;
    bool list_only = false;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--list") == 0 || strcmp(argv[i], "-l") == 0) {
            list_only = true;
        } else if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
            device_path = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            width = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            height = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            count = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            std::cout << "Usage: v4l2_test_capture [options]\n"
                      << "  --list, -l           List all V4L2 video devices\n"
                      << "  --device /dev/videoN  Video device (auto-detect if omitted)\n"
                      << "  --output FILE         Output raw file (default: test.raw)\n"
                      << "  --width W             Capture width (default: 1920)\n"
                      << "  --height H            Capture height (default: 1080)\n"
                      << "  --count N             Number of frames (default: 1)\n"
                      << "  --help, -h            Show this help\n";
            return 0;
        }
    }

    if (list_only) {
        list_devices();
        return 0;
    }

    // Auto-detect device if not specified
    if (device_path.empty()) {
        std::cout << "Auto-detecting V4L2 capture device...\n\n";

        // First, list all devices for diagnostics
        list_devices();

        // Try to find ISP device by sysfs name
        for (int i = 0; i <= 31; ++i) {
            char sysfs_path[64];
            snprintf(sysfs_path, sizeof(sysfs_path),
                     "/sys/class/video4linux/video%d/name", i);
            FILE* f = fopen(sysfs_path, "r");
            if (f) {
                char name[128] = {};
                if (fgets(name, sizeof(name), f)) {
                    if (strstr(name, "rkisp_mainpath") ||
                        strstr(name, "isp_mainpath") ||
                        strstr(name, "isp")) {
                        char dev[32];
                        snprintf(dev, sizeof(dev), "/dev/video%d", i);
                        device_path = dev;
                        fclose(f);
                        break;
                    }
                }
                fclose(f);
            }
        }

        // Fallback: try common ISP device numbers
        if (device_path.empty()) {
            const int candidates[] = {11, 21, 31, 0, 1, 2, 3};
            for (int dev_num : candidates) {
                char dev[32];
                snprintf(dev, sizeof(dev), "/dev/video%d", dev_num);
                int fd = open(dev, O_RDWR, 0);
                if (fd >= 0) {
                    struct v4l2_capability cap{};
                    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0 &&
                        (cap.capabilities & (V4L2_CAP_VIDEO_CAPTURE |
                                             V4L2_CAP_VIDEO_CAPTURE_MPLANE))) {
                        device_path = dev;
                        close(fd);
                        break;
                    }
                    close(fd);
                }
            }
        }

        if (device_path.empty()) {
            std::cerr << "No V4L2 capture device found!\n"
                      << "Run with --list to see available devices.\n";
            return 1;
        }

        std::cout << "\nSelected device: " << device_path << "\n\n";
    }

    // Open device
    CaptureContext ctx;
    ctx.fd = open(device_path.c_str(), O_RDWR, 0);
    if (ctx.fd < 0) {
        std::cerr << "Failed to open " << device_path << ": "
                  << strerror(errno) << "\n";
        return 1;
    }

    // Detect multiplanar
    struct v4l2_capability cap{};
    if (ioctl(ctx.fd, VIDIOC_QUERYCAP, &cap) < 0) {
        std::cerr << "VIDIOC_QUERYCAP failed: " << strerror(errno) << "\n";
        close(ctx.fd);
        return 1;
    }

    ctx.mplane = (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0;
    ctx.buf_type = ctx.mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                              : V4L2_BUF_TYPE_VIDEO_CAPTURE;

    std::cout << "Device: " << device_path << "\n"
              << "Card:   " << cap.card << "\n"
              << "Driver: " << cap.driver << "\n"
              << "API:    " << (ctx.mplane ? "multiplanar" : "single-planar") << "\n"
              << "Format: NV12 " << width << "x" << height << "\n"
              << "Output: " << output_path << "\n\n";

    // Capture frame(s)
    bool ok = capture_frame(ctx, output_path.c_str(), width, height,
                            V4L2_PIX_FMT_NV12);

    close(ctx.fd);

    if (ok) {
        std::cout << "\n✓ Frame captured successfully!\n"
                  << "  Convert to JPEG: ffmpeg -f rawvideo -pixel_format nv12"
                  << " -video_size " << width << "x" << height
                  << " -i " << output_path << " -frames:v 1 test.jpg\n";
        return 0;
    } else {
        std::cerr << "\n✗ Frame capture failed\n";
        return 1;
    }
}
