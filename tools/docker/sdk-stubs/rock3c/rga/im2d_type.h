/*
 * Stub header for Rockchip librga - im2d types
 *
 * Compilation stub only. Real header on board: /usr/include/rga/im2d_type.h
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Status codes ──────────────────────────────────────────────────────────── */
typedef enum {
    IM_STATUS_SUCCESS       = 0,
    IM_STATUS_FAILED        = -1,
    IM_STATUS_INVALID_PARAM = -2,
    IM_STATUS_NOT_SUPPORTED = -3,
    IM_STATUS_OUT_OF_MEMORY = -4,
    IM_STATUS_NO_DEVICE     = -5,
    IM_STATUS_TIMEOUT       = -6,
} IM_STATUS;

/* ── Buffer descriptor ─────────────────────────────────────────────────────── */
typedef struct {
    int  fd;          /* dma-buf fd */
    void* virt_addr;  /* virtual address */
    int  width;
    int  height;
    int  wstride;
    int  hstride;
    int  format;
    int  color_space;
    int  global_alpha;
} rga_buffer_t;

/* ── Color formats ─────────────────────────────────────────────────────────── */
#define RK_FORMAT_YCbCr_420_SP  0x0  /* NV12 */

/* ── Buffer info helper ────────────────────────────────────────────────────── */
typedef struct {
    int fd;
    void* ptr;
    uint32_t size;
    int width;
    int height;
    int format;
} rga_buffer_info_t;

#ifdef __cplusplus
}
#endif
