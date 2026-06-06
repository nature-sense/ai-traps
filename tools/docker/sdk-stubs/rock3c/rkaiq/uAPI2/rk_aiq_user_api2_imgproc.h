/*
 * Stub header for Rockchip AIQ (RKAIQ) user API v2 - image processing
 *
 * Compilation stub only. Real header on board: /usr/include/rkaiq/uAPI2/rk_aiq_user_api2_imgproc.h
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Opaque context (forward decl) ─────────────────────────────────────────── */
struct rk_aiq_sys_ctx_s;
typedef struct rk_aiq_sys_ctx_s rk_aiq_sys_ctx_t;

/* ── AE Lock ───────────────────────────────────────────────────────────────── */
int rk_aiq_uapi2_setAeLock(rk_aiq_sys_ctx_t* ctx, bool lock);

/* ── AWB Lock ──────────────────────────────────────────────────────────────── */
int rk_aiq_uapi2_lockAWB(rk_aiq_sys_ctx_t* ctx);

#ifdef __cplusplus
}
#endif
