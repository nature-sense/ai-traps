/*
 * Stub header for Rockchip librga - im2d API
 *
 * Compilation stub only. Real header on board: /usr/include/rga/im2d.h
 * librga is open source: https://github.com/rockchip-linux/linux-librga
 */

#pragma once

#include "im2d_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Image resize ──────────────────────────────────────────────────────────── */
IM_STATUS imresize(const rga_buffer_t src, rga_buffer_t dst);

/* ── Buffer wrapping ───────────────────────────────────────────────────────── */
rga_buffer_t wrapbuffer_handle(int fd, int width, int height, int wstride, int hstride, int format);

#ifdef __cplusplus
}
#endif
