/*
 * Stub header for Rockchip MPP - MPI (Media Process Interface)
 *
 * Compilation stub only. Real header on board: /usr/include/rockchip/rk_mpi.h
 * MPP is open source: https://github.com/rockchip-linux/mpp
 */

#pragma once

#include "rk_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Opaque types ──────────────────────────────────────────────────────────── */
typedef void* MppCtx;
typedef struct MppApi_t {
    RK_U32 size;
    RK_U32 version;
    /* MPI function pointers would be here */
} MppApi;

typedef void* MppEncCfg;
typedef void* MppBufferGroup;
typedef void* MppBuffer;
typedef void* MppFrame;
typedef void* MppPacket;
typedef void* MppTask;

/* ── Return codes ──────────────────────────────────────────────────────────── */
typedef enum {
    MPP_OK = 0,
    MPP_NOK = -1,
    MPP_ERR_UNKNOW = -2,
    MPP_ERR_NULL_PTR = -3,
    MPP_ERR_MALLOC = -4,
    MPP_ERR_OPEN_FILE = -5,
    MPP_ERR_VALUE = -6,
    MPP_ERR_SIZE = -7,
    MPP_ERR_TIMEOUT = -8,
    MPP_ERR_BUFFER_FULL = -9,
} MPP_RET;

/* ── Coding types ──────────────────────────────────────────────────────────── */
typedef enum {
    MPP_VIDEO_CodingNone = 0,
    MPP_VIDEO_CodingMJPEG = 1,
    MPP_VIDEO_CodingJPEG = 1,
} MppCodingType;

/* ── Frame types ───────────────────────────────────────────────────────────── */
typedef enum {
    MPP_FRAME_FMT_NV12 = 0,
} MppFrameFormat;

/* ── Buffer types ──────────────────────────────────────────────────────────── */
typedef enum {
    MPP_BUFFER_TYPE_NORMAL = 0,
    MPP_BUFFER_TYPE_DMA_HEAP = 1,
    MPP_BUFFER_TYPE_ION = 2,
    MPP_BUFFER_TYPE_DRM = 3,
} MppBufferType;

/* ── Encoder parameter types ───────────────────────────────────────────────── */
typedef enum {
    MPP_ENC_SET_CFG = 1,
    MPP_ENC_SET_PREP_CFG = 2,
    MPP_ENC_SET_RC_CFG = 3,
    MPP_ENC_SET_HEADER_MODE = 4,
} MppEncParamType;

/* ── API functions ─────────────────────────────────────────────────────────── */
MPP_RET mpp_create(MppCtx* ctx, MppApi** api);
MPP_RET mpp_init(MppCtx ctx, MppCodingType type);
MPP_RET mpp_destroy(MppCtx ctx);
MPP_RET mpp_enc_cfg_init(MppEncCfg* cfg);
MPP_RET mpp_enc_cfg_deinit(MppEncCfg cfg);
MPP_RET mpp_buffer_group_get(MppBufferGroup* group, MppBufferType type);
MPP_RET mpp_buffer_group_put(MppBufferGroup group);
MPP_RET mpp_buffer_get(MppBufferGroup group, MppBuffer* buffer, RK_U32 size);
MPP_RET mpp_buffer_put(MppBuffer buffer);
RK_VOID* mpp_buffer_get_ptr(MppBuffer buffer);
RK_S32 mpp_buffer_get_fd(MppBuffer buffer);

#ifdef __cplusplus
}
#endif
