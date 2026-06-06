/*
 * Stub header for Rockchip MPP - type definitions
 *
 * Compilation stub only. Real header on board: /usr/include/rockchip/rk_type.h
 * MPP is open source: https://github.com/rockchip-linux/mpp
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t RK_S32;
typedef uint32_t RK_U32;
typedef int64_t RK_S64;
typedef uint64_t RK_U64;
typedef float RK_FLOAT;
typedef double RK_DOUBLE;
typedef void RK_VOID;
typedef char RK_CHAR;
typedef size_t RK_SIZE;
typedef int RK_BOOL;

#ifndef NULL
#define NULL 0
#endif

#ifndef RK_TRUE
#define RK_TRUE 1
#endif

#ifndef RK_FALSE
#define RK_FALSE 0
#endif

#ifdef __cplusplus
}
#endif
