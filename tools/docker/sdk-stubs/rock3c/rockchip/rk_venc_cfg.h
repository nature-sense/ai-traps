/*
 * Stub header for Rockchip MPP - VENC configuration
 *
 * Compilation stub only. Real header on board: /usr/include/rockchip/rk_venc_cfg.h
 */

#pragma once

#include "rk_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Encoder prepare configuration ─────────────────────────────────────────── */
typedef struct {
    RK_S32 width;
    RK_S32 height;
    RK_S32 hor_stride;
    RK_S32 ver_stride;
    RK_S32 format;
} MppEncPrepCfg;

/* ── Encoder configuration ─────────────────────────────────────────────────── */
typedef struct {
    RK_S32 coding_type;
    RK_S32 max_pic_width;
    RK_S32 max_pic_height;
} MppEncCfgSet;

#ifdef __cplusplus
}
#endif
