#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <poll.h>
#include <linux/videodev2.h>
#include <linux/media.h>
#include <linux/v4l2-subdev.h>
#include <errno.h>

int main() {
    // Step 1: Configure media pipeline FIRST (before opening video0)
    int media_fd = open("/dev/media0", O_RDWR);
    if (media_fd < 0) { perror("open media0"); return 1; }
    
    // Enable links
    struct media_link_desc link = {0};
    link.source.entity = 1; link.source.index = 0;
    link.sink.entity = 49; link.sink.index = 0;
    link.flags = MEDIA_LNK_FL_ENABLED;
    ioctl(media_fd, MEDIA_IOC_SETUP_LINK, &link);
    
    link.source.entity = 31; link.source.index = 1;
    link.sink.entity = 34; link.sink.index = 0;
    link.flags = MEDIA_LNK_FL_ENABLED;
    ioctl(media_fd, MEDIA_IOC_SETUP_LINK, &link);
    close(media_fd);
    
    // Configure sensor
    int sd = open("/dev/v4l-subdev0", O_RDWR);
    struct v4l2_subdev_format sf = {0};
    sf.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    sf.pad = 0;
    sf.format.width = 1920; sf.format.height = 1080;
    sf.format.code = MEDIA_BUS_FMT_SGBRG10_1X10;
    ioctl(sd, VIDIOC_SUBDEV_S_FMT, &sf);
    close(sd);
    
    // Configure ISP
    sd = open("/dev/v4l-subdev13", O_RDWR);
    sf.pad = 0; sf.format.code = MEDIA_BUS_FMT_SGBRG10_1X10;
    ioctl(sd, VIDIOC_SUBDEV_S_FMT, &sf);
    sf.pad = 2; sf.format.code = MEDIA_BUS_FMT_YUYV8_1X16;
    ioctl(sd, VIDIOC_SUBDEV_S_FMT, &sf);
    close(sd);
    
    // Configure scaler
    sd = open("/dev/v4l-subdev27", O_RDWR);
    sf.pad = 0; sf.format.code = MEDIA_BUS_FMT_YUYV8_1X16;
    ioctl(sd, VIDIOC_SUBDEV_S_FMT, &sf);
    sf.pad = 1; sf.format.code = MEDIA_BUS_FMT_YUYV8_1X16;
    ioctl(sd, VIDIOC_SUBDEV_S_FMT, &sf);
    close(sd);
    
    printf("Pipeline configured. Now opening video0...\n");
    
    // Step 2: NOW open video0
    int fd = open("/dev/video0", O_RDWR);
    if (fd < 0) { perror("open video0"); return 1; }
    
    // Check format
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctl(fd, VIDIOC_G_FMT, &fmt);
    printf("G_FMT: %dx%d fourcc=0x%x planes=%d\n", 
           fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
           fmt.fmt.pix_mp.pixelformat, fmt.fmt.pix_mp.num_planes);
    
    // Try S_FMT
    struct v4l2_format sf2 = {0};
    sf2.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    sf2.fmt.pix_mp.width = 1920;
    sf2.fmt.pix_mp.height = 1080;
    sf2.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    sf2.fmt.pix_mp.field = V4L2_FIELD_NONE;
    sf2.fmt.pix_mp.num_planes = 1;
    int ret = ioctl(fd, VIDIOC_S_FMT, &sf2);
    printf("S_FMT: ret=%d errno=%d\n", ret, errno);
    if (ret == 0) {
        printf("  %dx%d fourcc=0x%x\n", sf2.fmt.pix_mp.width, sf2.fmt.pix_mp.height, sf2.fmt.pix_mp.pixelformat);
    }
    
    // Try REQBUFS
    struct v4l2_requestbuffers req = {0};
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    ret = ioctl(fd, VIDIOC_REQBUFS, &req);
    printf("REQBUFS: ret=%d errno=%d count=%d\n", ret, errno, req.count);
    
    if (ret == 0 && req.count > 0) {
        void* bufs[4] = {0};
        unsigned int sizes[4] = {0};
        for (int i = 0; i < req.count; i++) {
            struct v4l2_buffer buf = {0};
            struct v4l2_plane plane = {0};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            buf.m.planes = &plane;
            buf.length = 1;
            if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == 0) {
                sizes[i] = plane.length;
                bufs[i] = mmap(NULL, plane.length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, plane.m.mem_offset);
                printf("Buf %d: %u bytes @ %p\n", i, sizes[i], bufs[i]);
            }
        }
        
        for (int i = 0; i < req.count; i++) {
            struct v4l2_buffer buf = {0};
            struct v4l2_plane plane = {0};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            buf.m.planes = &plane;
            buf.length = 1;
            ioctl(fd, VIDIOC_QBUF, &buf);
        }
        
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        ret = ioctl(fd, VIDIOC_STREAMON, &type);
        printf("STREAMON: ret=%d errno=%d\n", ret, errno);
        
        if (ret == 0) {
            struct pollfd pfd = {fd, POLLIN, 0};
            printf("Waiting for frame...\n");
            int pret = poll(&pfd, 1, 5000);
            if (pret > 0) {
                struct v4l2_buffer buf = {0};
                struct v4l2_plane plane = {0};
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
                buf.memory = V4L2_MEMORY_MMAP;
                buf.m.planes = &plane;
                buf.length = 1;
                if (ioctl(fd, VIDIOC_DQBUF, &buf) == 0) {
                    printf("Frame! index=%d bytesused=%d\n", buf.index, plane.bytesused);
                    FILE* f = fopen("/tmp/test_capture.raw", "wb");
                    if (f) {
                        fwrite(bufs[buf.index], 1, plane.bytesused > 0 ? plane.bytesused : sizes[buf.index], f);
                        fclose(f);
                        printf("Saved!\n");
                    }
                }
            } else {
                printf("poll timeout/error: %d\n", pret);
            }
            ioctl(fd, VIDIOC_STREAMOFF, &type);
        }
        
        for (int i = 0; i < req.count; i++) {
            if (bufs[i]) munmap(bufs[i], sizes[i]);
        }
    }
    
    close(fd);
    return 0;
}
