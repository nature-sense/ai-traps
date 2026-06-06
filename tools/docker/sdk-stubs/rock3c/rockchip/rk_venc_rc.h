/*
 * Stub header for Rockchip MPP - VENC rate control
 *
 * Compilation stub only. Real header on board: /usr/include/rockchip/rk_venc_rc.h
 */

#pragma once

#include "rk_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Rate control mode ─────────────────────────────────────────────────────── */
typedef enum {
    MPP_ENC_RC_MODE_VBR = 0,
    MPP_ENC_RC_MODE_CBR = 1,
    MPP_ENC_RC_MODE_FIXQP = 2,
    MPP_ENC_RC_MODE_AVBR = 3,
} MppEncRcMode;

/* ── Rate control configuration ────────────────────────────────────────────── */
typedef struct {
    RK_S32 rc_mode;
    RK_S32 quality;
    RK_S32 bit_rate;
    RK_S32 bit_rate_max;
    RK_S32 bit_rate_min;
} MppEncRcCfg;

/* ── Header mode ───────────────────────────────────────────────────────────── */
typedef enum {
    MPP_ENC_HEADER_MODE_EACH_IDR = 0,
    MPP_ENC_HEADER_MODE_DEFAULT = 1,
} MppEncHeaderMode;

#ifdef __cplusplus
}
#endif
