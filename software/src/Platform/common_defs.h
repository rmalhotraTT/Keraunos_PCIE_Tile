/* Common Definitions for All Platforms
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * This header contains definitions that are common across all platforms.
 * Platform-specific code should NOT include this directly - it's automatically
 * included via platform.h
 *
 * Use this file for:
 * - Common type definitions
 * - Common constants
 * - Common utility macros
 * - Common helper functions
 *
 * DO NOT put platform-specific register definitions here.
 */

#ifndef COMMON_DEFS_H
#define COMMON_DEFS_H

/* Standard C library headers needed by all platforms */
#include <stdbool.h>
#include <stdint.h>

/* RISC-V CSR utilities */
#include "csr.h"

/*==============================================================================
 * COMMON TYPE DEFINITIONS
 *============================================================================*/

/* Common status/error codes */
typedef enum {
  STATUS_OK = 0,
  STATUS_ERROR = -1,
  STATUS_TIMEOUT = -2,
  STATUS_BUSY = -3,
  STATUS_INVALID_PARAM = -4,
  STATUS_NOT_SUPPORTED = -5
} status_t;

/*==============================================================================
 * COMMON CONSTANTS
 *============================================================================*/

/* Common boolean values (if not using stdbool.h) */
#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

/* Common bit manipulation macros */
#define BIT(n) (1UL << (n))
#define BIT_MASK(n) (BIT(n) - 1)
#define GET_BIT(val, n) (((val) >> (n)) & 1)
#define SET_BIT(val, n) ((val) | BIT(n))
#define CLEAR_BIT(val, n) ((val) & ~BIT(n))
#define TOGGLE_BIT(val, n) ((val) ^ BIT(n))

/* Field manipulation macros */
#define FIELD_PREP(mask, val) (((val) << (__builtin_ctzl(mask))) & (mask))
#define FIELD_GET(mask, reg) (((reg) & (mask)) >> (__builtin_ctzl(mask)))

/* Common size definitions */
#define KB(x) ((x) * 1024UL)
#define MB(x) ((x) * 1024UL * 1024UL)
#define GB(x) ((x) * 1024UL * 1024UL * 1024UL)

/* Alignment macros */
#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(x, align) ((x) & ~((align) - 1))
#define IS_ALIGNED(x, align) (((x) & ((align) - 1)) == 0)

/* Min/Max macros */
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

/* Array size macro */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/*==============================================================================
 * COMMON COMPILER ATTRIBUTES
 *============================================================================*/

/* Function attributes */
#define WEAK __attribute__((weak))
#define NORETURN __attribute__((noreturn))
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#define NEVER_INLINE __attribute__((noinline))
#define PACKED __attribute__((packed))
#define ALIGNED(x) __attribute__((aligned(x)))
#define SECTION(x) __attribute__((section(x)))
#define USED __attribute__((used))
#define UNUSED __attribute__((unused))

/*==============================================================================
 * COMMON UTILITY MACROS
 *============================================================================*/

/* Barrier macros */
#define COMPILER_BARRIER() __asm__ volatile("" ::: "memory")
#define MEMORY_BARRIER() __asm__ volatile("fence" ::: "memory")

/* Read/Write barrier macros for MMIO */
#define READ_ONCE(x) (*(volatile typeof(x) *)&(x))
#define WRITE_ONCE(x, val) ((*(volatile typeof(x) *)&(x)) = (val))

/*==============================================================================
 * PLACEHOLDER FOR FUTURE COMMON DEFINITIONS
 *============================================================================*/

/* Add more common definitions here as needed:
 * - Common peripheral access functions
 * - Common timing/delay functions
 * - Common debugging macros
 * - Common logging macros
 */

#endif /* COMMON_DEFS_H */
