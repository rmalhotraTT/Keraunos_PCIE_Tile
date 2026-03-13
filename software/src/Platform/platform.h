/* Platform-Agnostic Register Header
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * This header provides a unified interface to platform-specific register
 * definitions. Applications should ONLY include this file - never directly
 * include platform headers. The build system automatically selects the correct
 * platform based on PLATFORM parameter.
 *
 * Usage in application code:
 *   #include "platform.h"
 *
 * The build system will automatically:
 * - Include common definitions (registers/common/)
 * - Include platform-specific headers (registers/mimir/ or registers/ker/)
 * - Define PLATFORM_* macros for conditional compilation
 * - Set correct include paths (-I points to correct platform directory)
 *
 * Architecture:
 * - Common definitions in registers/common/ (types, macros, utilities)
 * - Platform-specific register definitions are authoritative (from *_soc_reg.h)
 * - No overlap or ambiguity about which registers to use
 */

#pragma once

#ifndef PLATFORM_H
#define PLATFORM_H

/* Common definitions for all platforms */
#include "common_defs.h"

/* Platform-specific headers - selected by build system
 * Each platform header includes its complete SoC register definitions
 * and provides all platform-specific macros.
 */
#if defined(PLATFORM_MIMIR_SMC) || defined(PLATFORM_MIMIR_CCE) ||              \
    defined(PLATFORM_MIMIR_D2D)
#include "mimir_platform.h"
#elif defined(PLATFORM_KER_SMC) || defined(PLATFORM_KER_CCE) ||                \
    defined(PLATFORM_KER_D2D)
#include "ker_platform.h"
#else
#error                                                                         \
    "No platform defined. Use CONFIG=mimir_smc, CONFIG=ker_smc, CONFIG=mimir_cce, or CONFIG=ker_cce"
#endif

#endif /* PLATFORM_H */
