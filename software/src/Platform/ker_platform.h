/* Keraunos Platform-Specific Header
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * This header contains Keraunos-specific register definitions and
 * configurations. It is automatically included by platform.h when building for
 * Keraunos platforms.
 *
 * All Keraunos platform code should use this header to access registers.
 * This ensures the correct platform-specific register definitions are used.
 */

#ifndef KER_PLATFORM_H
#define KER_PLATFORM_H

/* Include Keraunos SoC register definitions
 * This is the authoritative source for Keraunos register addresses and layouts.
 * Generated from: keraunos_soc/meta/registers/c/keraunos_soc_reg.h
 */
#include "keraunos_soc_reg.h"

/* To include chippy naming convention for registers, include the following header */
//#include "chippy_keraunos_soc_reg.h"

/* Keraunos-specific platform constants */
#define PLATFORM_NAME "Keraunos"
#define PLATFORM_ID 0x02
#define KERAUNOS_SOC_VERSION_MAJOR 1
#define KERAUNOS_SOC_VERSION_MINOR 0

/* Platform identification macros (set by build system) */
#define PLATFORM_CHIP_KER 1
#define PLATFORM_CHIP "ker"

/* Component identification (set by build system via PLATFORM_KER_SMC or
 * PLATFORM_KER_CCE) */
#ifdef PLATFORM_KER_SMC
#define PLATFORM_COMPONENT_SMC 1
#define PLATFORM_COMPONENT "smc"
#endif

#ifdef PLATFORM_KER_CCE
#define PLATFORM_COMPONENT_CCE 1
#define PLATFORM_COMPONENT "cce"
#endif

/* Helper macros for platform-specific code */
#define IS_MIMIR_PLATFORM() 0
#define IS_KER_PLATFORM() 1

#ifdef PLATFORM_KER_SMC
#define IS_SMC_COMPONENT() 1
#define IS_CCE_COMPONENT() 0
#else
#define IS_SMC_COMPONENT() 0
#define IS_CCE_COMPONENT() 1
#endif

#endif /* KER_PLATFORM_H */
