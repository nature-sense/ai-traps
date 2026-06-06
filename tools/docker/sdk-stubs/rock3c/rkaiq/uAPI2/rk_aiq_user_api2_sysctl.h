/*
 * Stub header for Rockchip AIQ (RKAIQ) user API v2 - sysctl
 *
 * This is a compilation stub only. The actual library is loaded at runtime
 * on the target board. These stubs provide just enough type information
 * for the cross-compiler to succeed.
 *
 * Real header location on board: /usr/include/rkaiq/uAPI2/rk_aiq_user_api2_sysctl.h
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Return codes ──────────────────────────────────────────────────────────── */
typedef int XCamReturn;

#define XCAM_RETURN_NO_ERROR        ((XCamReturn)0)
#define XCAM_RETURN_BYPASS          ((XCamReturn)1)
#define XCAM_RETURN_ERROR_FAILED    ((XCamReturn)-1)
#define XCAM_RETURN_ERROR_PARAM     ((XCamReturn)-2)
#define XCAM_RETURN_ERROR_TIMEOUT   ((XCamReturn)-3)
#define XCAM_RETURN_ERROR_MEM       ((XCamReturn)-4)
#define XCAM_RETURN_ERROR_IOCTL     ((XCamReturn)-5)
#define XCAM_RETURN_ERROR_ORDER     ((XCamReturn)-6)

/* ── Opaque context ────────────────────────────────────────────────────────── */
typedef struct rk_aiq_sys_ctx_s rk_aiq_sys_ctx_t;

/* ── Working mode ──────────────────────────────────────────────────────────── */
typedef enum {
    RK_AIQ_WORKING_MODE_NORMAL          = 0,
    RK_AIQ_WORKING_MODE_ISP_HDR2        = 1,
    RK_AIQ_WORKING_MODE_ISP_HDR3        = 2,
} rk_aiq_working_mode_t;

/* ── Error message ─────────────────────────────────────────────────────────── */
typedef struct {
    int err_code;
    const char* err_msg;
} rk_aiq_err_msg_t;

/* ── Metadata ──────────────────────────────────────────────────────────────── */
typedef struct {
    uint32_t frame_id;
    uint64_t timestamp;
} rk_aiq_metas_t;

/* ── Sensor info ───────────────────────────────────────────────────────────── */
typedef struct {
    char sensor_name[64];
    int phy_id;
} rk_aiq_sensor_info_t;

typedef struct {
    rk_aiq_sensor_info_t sensor_info;
    int reserved[8];
} rk_aiq_static_info_t;

/* ── Callback types ────────────────────────────────────────────────────────── */
typedef XCamReturn (*rk_aiq_error_cb)(rk_aiq_err_msg_t* msg);
typedef XCamReturn (*rk_aiq_metas_cb)(rk_aiq_metas_t* meta);

/* ── API functions ─────────────────────────────────────────────────────────── */

XCamReturn rk_aiq_uapi2_sysctl_enumStaticMetasByPhyId(int phy_id, rk_aiq_static_info_t* info);

void rk_aiq_uapi2_sysctl_preInit_devBufCnt(const char* sensor_name, const char* buf_type, int count);

int rk_aiq_uapi2_sysctl_preInit_scene(const char* sensor_name, const char* scene, const char* param);

rk_aiq_sys_ctx_t* rk_aiq_uapi2_sysctl_init(const char* sensor_name,
                                            const char* iq_file_dir,
                                            rk_aiq_error_cb err_cb,
                                            rk_aiq_metas_cb metas_cb);

XCamReturn rk_aiq_uapi2_sysctl_prepare(rk_aiq_sys_ctx_t* ctx,
                                        uint32_t width, uint32_t height,
                                        rk_aiq_working_mode_t mode);

XCamReturn rk_aiq_uapi2_sysctl_start(rk_aiq_sys_ctx_t* ctx);

XCamReturn rk_aiq_uapi2_sysctl_stop(rk_aiq_sys_ctx_t* ctx, bool keep_ext_mem);

XCamReturn rk_aiq_uapi2_sysctl_deinit(rk_aiq_sys_ctx_t* ctx);

#ifdef __cplusplus
}
#endif
