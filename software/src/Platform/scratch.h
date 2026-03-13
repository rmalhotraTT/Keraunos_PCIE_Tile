/* Scratch/Postcode Register Utilities
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * Platform-agnostic utilities for writing test results to scratch/postcode
 * registers. Provides a unified interface for writing test status codes across
 * platforms.
 *
 * Usage:
 *   #include "scratch.h"
 *
 *   // Write test result (works on all platforms)
 *   write_test_result(0, TEST_PASS);
 *
 *   // Or use convenience macros
 *   WRITE_TEST_PASS();
 *   WRITE_TEST_FAIL();
 *
 * Platform Behavior:
 * - SMC platforms: Writes to scratch registers (with local address translation)
 * - CCE platforms: Writes to per-hart postcode registers
 */

#ifndef SCRATCH_H
#define SCRATCH_H

#include "platform.h"
#include "regs.h"
#include <stdint.h>

/*==============================================================================
 * TEST STATUS CODES
 *============================================================================*/

/* Common test status codes following testbench conventions */
#if defined(PLATFORM_MIMIR_SMC) || defined(PLATFORM_KER_SMC)
/* SMC: test_smc_plusargs convention */
#define TEST_PASS 0xACAFACA1    /* PASS code for SMC testbench */
#define TEST_FAIL 0xDEADBEEF    /* FAIL code for SMC testbench */
#define TEST_RUNNING 0x12345678 /* Test in progress indicator */
#elif defined(PLATFORM_MIMIR_CCE) || defined(PLATFORM_KER_CCE)
/* CCE: cce-dv postcode convention */
#define TEST_PASS 0xfaaccaa5    /* PASS code for CCE testbench */
#define TEST_FAIL 0xdeadbeef    /* FAIL code for CCE testbench */
#define TEST_RUNNING 0x12345678 /* Test in progress indicator */
#elif defined(PLATFORM_MIMIR_D2D) || defined(PLATFORM_KER_D2D)
#define TEST_PASS 0xcaa5faac    /* PASS code for CCE testbench */
#define TEST_FAIL 0xdeadbeef    /* FAIL code for CCE testbench */
#define TEST_RUNNING 0x12345678 /* Test in progress indicator */
#else
#error "Unknown platform - cannot determine test status codes"
#endif

/*==============================================================================
 * SMC-SPECIFIC DEFINITIONS
 *============================================================================*/

#if defined(PLATFORM_MIMIR_SMC) || defined(PLATFORM_KER_SMC)
/* SMC CPU local base address
 * When firmware runs on the SMC CPU, it accesses its own registers via
 * the local address space (0xC0000000 base) rather than the global/SoC
 * address space (0x08000000 for Keraunos, 0x00000000 for Mimir).
 *
 * This is configured via the SMC_CPU_CTRL_LOCAL_BASE_REG hardware register.
 */
#ifndef SMC_CPU_CTRL_BASE_ADDR
#define SMC_CPU_CTRL_BASE_ADDR 0xC0010000
#endif

#ifndef SMC_SCRATCH_0_REG_OFFSET
#define SMC_SCRATCH_0_REG_OFFSET SMC_CPU_SMC_CPU_CTRL_SCRATCH_0__REG_OFFSET
#endif

#endif
/*==============================================================================
 * CCE-SPECIFIC DEFINITIONS
 *============================================================================*/

#if defined(PLATFORM_MIMIR_CCE) || defined(PLATFORM_KER_CCE)
/* CCE peripheral base address */
#ifndef CCE_CLUSTER_BASE_ADDR
#define CCE_CLUSTER_BASE_ADDR 0x03000000
// CCE core local base address is 0x03000000; refer cce_reg.h
#endif

#ifndef CCE_SCRATCH_0_REG_OFFSET
#define CCE_SCRATCH_0_REG_OFFSET 0x40 // refer to cce_reg.h
#endif
#endif

/*==============================================================================
 * D2D-SPECIFIC DEFINITIONS
 *============================================================================*/
#if defined(PLATFORM_MIMIR_D2D) || defined(PLATFORM_KER_D2D)
/* D2D MMIO space address */
#ifndef D2D_MSIO_BASE_ADDR
#define D2D_MSIO_BASE_ADDR 0x1000000000 /// MMIO space for the RISCV core is
/// configured to this value so only address
/// that writes to this space are outputs
/// from RISCV core and outputs as MMIO
#endif

#ifndef D2D_SS_SCRATCH_REG_OFFSET
#define D2D_SS_SCRATCH_REG_ADDR 0x00001000
#define D2D_SS_SCRATCH_REG_OFFSET 0x0008
#endif
#endif

/*==============================================================================
 * SCRATCH/POSTCODE REGISTER ACCESS
 *============================================================================*/

/**
 * Write test result to scratch/postcode register
 *
 * @param[in] offset Register offset (from
 * mimir_soc_reg.h/keraunos_soc_reg.h)
 * @param[in] value Value to write
 *
 *
 * Its scratch register common for the cluster in SMC for 4 cores and CCE for
 * the 8 cores
 *
 *
 * Its scratch register common for the cluster in SMC for 4 cores and CCE for
 * the 8 cores
 *
 *
 * SUBHA : why the per hart postcode for CCE is done differntly than SMC?
 * Its scratch register common for the cluster in SMC for 4 cores and CCE for
 * the 8 cores
 *
 *
 * This function is specifically for writing test status codes and handles
 * platform-specific behavior automatically.
 */
static inline void write_scratch_reg(uint32_t offset, uint32_t value) {
#if defined(PLATFORM_MIMIR_SMC) || defined(PLATFORM_KER_SMC)
  /* SMC: Write to scratch registers with local address view
   * Convert the global register address to SMC-local address space.
   * The SMC CPU accesses its own registers at 0xC0000000 base, not the
   * global SoC base address.
   * SMC scratch registers are 64-bit wide.
   */

  write32_reg(SMC_CPU_CTRL_BASE_ADDR + offset, value);

#elif defined(PLATFORM_MIMIR_CCE) || defined(PLATFORM_KER_CCE)
  /* CCE: Write to scratch register
   * CCE scratch registers are 32-bit wide (4 bytes apart).
   * Each register is at CCE_CLUSTER_BASE_ADDR + offset.
   */
  write32_reg(CCE_CLUSTER_BASE_ADDR + offset, value);

#elif defined(PLATFORM_MIMIR_D2D) || defined(PLATFORM_KER_D2D)
  /* D2D: Write to scratch register */
  write32_reg(D2D_MSIO_BASE_ADDR + D2D_SS_SCRATCH_REG_ADDR + offset, value);
#else
#error "Unknown platform"
#endif
}

/**
 * Read value from scratch/postcode register
 *
 * @param offset Register offset (from mimir_soc_reg.h/keraunos_soc_reg.h)
 * @return       Value read from register
 *
 * Platform behavior:
 * - SMC: Reads from SCRATCH_N register at offset N*8 (64-bit offset but 32 bit
 * registers)
 * - CCE: Reads from SCRATCH_N register at offset N*4 (32-bit offset and 32 bit
 * registers)
 */
static inline uint32_t read_scratch(uint32_t offset) {
#if defined(PLATFORM_MIMIR_SMC) || defined(PLATFORM_KER_SMC)
  /* SMC: Read from scratch registers with local address view
   * SMC scratch registers are 64-bit wide (8 bytes apart).
   */
  return (read32_reg(SMC_CPU_CTRL_BASE_ADDR + offset));

#elif defined(PLATFORM_MIMIR_CCE) || defined(PLATFORM_KER_CCE)
  /* CCE: Read from scratch register
   * CCE scratch registers are 32-bit wide (4 bytes apart).
   */
  return (read32_reg((CCE_CLUSTER_BASE_ADDR + offset)));
#elif defined(PLATFORM_MIMIR_D2D) || defined(PLATFORM_KER_D2D)
  /* D2D: Read from scratch register */
  return (read32_reg(D2D_MSIO_BASE_ADDR + D2D_SS_SCRATCH_REG_ADDR + offset));
#else
#error "Unknown platform"
  return 0;
#endif
}

/*==============================================================================
 * CONVENIENCE MACROS FOR TEST RESULTS
 *============================================================================*/

/**
 * Write PASS status to test result register
 */
#if defined(PLATFORM_MIMIR_SMC) || defined(PLATFORM_KER_SMC)
#define WRITE_TEST_PASS() write_scratch_reg(SMC_SCRATCH_0_REG_OFFSET, TEST_PASS)
#elif defined(PLATFORM_MIMIR_CCE) || defined(PLATFORM_KER_CCE)
#define WRITE_TEST_PASS() write_scratch_reg(CCE_SCRATCH_0_REG_OFFSET, TEST_PASS)
#elif defined(PLATFORM_MIMIR_D2D) || defined(PLATFORM_KER_D2D)
#define WRITE_TEST_PASS()                                                      \
  write_scratch_reg(D2D_SS_SCRATCH_REG_OFFSET, TEST_PASS)
#else
#error "Unknown platform"
#endif

/**
 * Write FAIL status to test result register
 */
#if defined(PLATFORM_MIMIR_SMC) || defined(PLATFORM_KER_SMC)
#define WRITE_TEST_FAIL() write_scratch_reg(SMC_SCRATCH_0_REG_OFFSET, TEST_FAIL)
#elif defined(PLATFORM_MIMIR_CCE) || defined(PLATFORM_KER_CCE)
#define WRITE_TEST_FAIL() write_scratch_reg(CCE_SCRATCH_0_REG_OFFSET, TEST_FAIL)
#elif defined(PLATFORM_MIMIR_D2D) || defined(PLATFORM_KER_D2D)
#define WRITE_TEST_FAIL()                                                      \
  write_scratch_reg(D2D_SS_SCRATCH_REG_OFFSET, TEST_FAIL)
#else
#error "Unknown platform"
#endif

/**
 * Write RUNNING status to test result register
 */
#if defined(PLATFORM_MIMIR_SMC) || defined(PLATFORM_KER_SMC)
#define WRITE_TEST_RUNNING()                                                   \
  write_scratch_reg(SMC_SCRATCH_0_REG_OFFSET, TEST_RUNNING)
#elif defined(PLATFORM_MIMIR_CCE) || defined(PLATFORM_KER_CCE)
#define WRITE_TEST_RUNNING()                                                   \
  write_scratch_reg(CCE_SCRATCH_0_REG_OFFSET, TEST_RUNNING)
#elif defined(PLATFORM_MIMIR_D2D) || defined(PLATFORM_KER_D2D)
#define WRITE_TEST_RUNNING()                                                   \
  write_scratch_reg(D2D_SS_SCRATCH_REG_OFFSET, TEST_RUNNING)
#else
#error "Unknown platform"
#endif

#endif /* SCRATCH_H */
