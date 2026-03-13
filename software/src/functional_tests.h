/*
 * PCIe Functional Tests Header
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * Functional tests for PCIe capabilities (speed change, link width, etc.)
 */

#ifndef FUNCTIONAL_TESTS_H
#define FUNCTIONAL_TESTS_H

#include <stdint.h>

/**
 * @brief Test PCIe speed change capabilities from Gen1 to Gen6
 * @param initial_width Initial link width to verify after each speed change
 * @return 0 on success, negative on error
 *
 * This function:
 * 1. Loops through all speeds from Gen1 to Gen6
 * 2. For each speed:
 *    - Programs the target link speed in Link Control 2 register
 *    - Triggers directed speed change via GEN2_CONTROL register
 *    - Waits for link to retrain
 *    - Verifies the new speed and that width remained the same
 * 3. Logs progress and results to scratch registers
 *
 * Return codes:
 *   0: All speed changes successful
 *  -1: Speed verification failed (speed didn't change as expected)
 *  -2: Width verification failed (width changed unexpectedly)
 */
int test_speed_change(uint32_t initial_width);

/**
 * @brief Test PCIe link width downgrade from x4 to x1
 * @param initial_speed Initial link speed to verify after each width change
 * @return 0 on success, negative on error
 *
 * This function:
 * 1. Loops through all widths from x4 down to x1 (x4, x2, x1)
 * 2. For each width:
 *    - Programs the target link width in MULTI_LANE_CONTROL register
 *    - Triggers directed link width change via DIRECT_LINK_WIDTH_CHANGE bit
 *    - Waits for link to retrain
 *    - Verifies the new width and that speed remained the same
 * 3. Logs progress and results to scratch registers
 *
 * Return codes:
 *   0: All width changes successful
 *  -1: Width verification failed (width didn't change as expected)
 *  -2: Speed verification failed (speed changed unexpectedly)
 */
int test_width_downgrade(uint32_t initial_speed);

/**
 * @brief Read and verify PCIe device identification registers
 * @return 0 on success, negative on error
 *
 * This function reads and stores to scratch registers:
 * - Device ID / Vendor ID (offset 0x00)
 * - Class Code / Revision ID (offset 0x08)
 * - Subsystem ID / Subsystem Vendor ID (offset 0x2C)
 *
 * Scratch register mapping:
 *   SCRATCH_1: Device ID (upper 16) / Vendor ID (lower 16)
 *   SCRATCH_2: Class Code (upper 24) / Revision ID (lower 8)
 *   SCRATCH_3: Subsystem ID (upper 16) / Subsystem Vendor ID (lower 16)
 */
int test_pcie_device_id(void);

/**
 * @brief Check PCIe BAR (Base Address Register) configuration
 * @return 0 on success, negative on error
 *
 * This function scans all 6 BARs in the PCIe Type 0 header and for the
 * first implemented BAR, determines:
 * - I/O vs Memory space
 * - 32-bit vs 64-bit addressing
 * - Prefetchable vs Non-prefetchable
 * - BAR size (via standard sizing mechanism)
 * - Base address assignment
 *
 * Scratch register mapping (for first BAR found):
 *   SCRATCH_4: BAR flags [7:4]=BAR index, [2]=prefetchable, [1]=64-bit, [0]=I/O
 *   SCRATCH_5: Base address low 32 bits
 *   SCRATCH_6: Base address high 32 bits (64-bit BARs) or size (32-bit BARs)
 *   SCRATCH_7: Full BAR size
 *   SCRATCH_8: Raw size mask low (for debugging)
 *   SCRATCH_9: Raw size mask high (for debugging)
 */
int test_pcie_bar_config(void);

#endif /* FUNCTIONAL_TESTS_H */
