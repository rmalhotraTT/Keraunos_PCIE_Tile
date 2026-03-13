/*
 * PCIe Interrupt Testing Guide
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * Simple methods to verify interrupt system is working
 */

#include "csr.h"
#include "pcie_helpers.h"
#include "pcie_interrupt.h"
#include "platform.h"
#include "regs.h"
#include "scratch.h"

/* ========================================================================== */
/*                    Test 1: Check CSR Configuration                         */
/* ========================================================================== */

/**
 * @brief Verify RISC-V CSRs are configured for interrupts
 *
 * This writes CSR values to scratch registers so you can verify:
 * - mtvec points to vector table (should have LSB=1 for vectored mode)
 * - mie has MEIE bit set (bit 11 = 0x800)
 * - mstatus has MIE bit set (bit 3 = 0x8)
 */
void test_check_csr_config(void) {
  uint64_t mtvec_val = read_csr(mtvec);
  uint64_t mie_val = read_csr(mie);
  uint64_t mstatus_val = read_csr(mstatus);

  /* Write to scratch registers for inspection */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR,
              (uint32_t)(mtvec_val & 0xFFFFFFFF));
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_11__REG_ADDR,
              (uint32_t)(mtvec_val >> 32));
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_12__REG_ADDR, (uint32_t)mie_val);
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_13__REG_ADDR, (uint32_t)mstatus_val);

  /* Write verification marker */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_14__REG_ADDR,
              0xC520C0D5); // "CSR CODES"
}

/* Expected results:
 * SCRATCH_10: Lower 32 bits of vector table address, should end in 0x1
 * (vectored mode) SCRATCH_11: Upper 32 bits of vector table address SCRATCH_12:
 * Should have bit 11 set (0x800 or higher) SCRATCH_13: Should have bit 3 set
 * (0x8 or higher)
 */

/* ========================================================================== */
/*                    Test 2: Monitor Interrupt Counter                       */
/* ========================================================================== */

/**
 * @brief Check if interrupt counter is incrementing
 *
 * The interrupt system writes to SCRATCH_15 with format: 0x107Exxxx
 * where xxxx is the interrupt count. Read this register multiple times
 * during link training to see it increment.
 *
 * @return Current interrupt count
 */
uint32_t test_get_interrupt_count(void) {
  uint32_t scratch15 = read32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_15__REG_ADDR);

  /* Extract count from lower 16 bits */
  return scratch15 & 0xFFFF;
}

/**
 * @brief Simple test that monitors for interrupt count changes
 *
 * This function reads the interrupt counter multiple times and
 * reports if it changed (indicating interrupts are firing).
 *
 * @return 1 if interrupts detected, 0 if not
 */
int test_wait_for_interrupt_activity(void) {
  uint32_t initial_count = test_get_interrupt_count();
  uint32_t current_count;
  int iterations = 0;
  const int max_iterations = 10000;

  /* Wait for counter to change or timeout */
  while (iterations < max_iterations) {
    current_count = test_get_interrupt_count();

    if (current_count != initial_count) {
      /* Interrupt counter changed! */
      write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR,
                  0x107600D); // "INT GOOD"
      write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_11__REG_ADDR, initial_count);
      write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_12__REG_ADDR, current_count);
      return 1;
    }

    iterations++;
  }

  /* No interrupt activity detected */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR,
              0x107DEAD); // "INT DEAD"
  return 0;
}

/* ========================================================================== */
/*                    Test 3: Trigger Interrupts Manually                     */
/* ========================================================================== */

/**
 * @brief Trigger interrupts by changing link speed
 *
 * Requesting a speed change causes the link to retrain, which generates
 * LTSSM state change interrupts as the link goes through:
 * L0 -> RECOVERY -> L0
 *
 * @return Number of interrupts that occurred during speed change
 */
uint32_t test_trigger_interrupts_via_speed_change(void) {
  uint32_t count_before, count_after;
  pcie_link_status_t status;

  /* Get initial count */
  count_before = test_get_interrupt_count();

  /* Read current speed */
  if (pcie_read_link_status(&status) != 0) {
    return 0;
  }

  /* Request a different speed to trigger retraining */
  uint32_t target_speed = (status.link_speed == PCIE_SPEED_GEN3)
                              ? PCIE_SPEED_GEN2
                              : PCIE_SPEED_GEN3;

  /* This will cause LTSSM transitions and generate interrupts */
  pcie_request_speed_change(target_speed);

  /* Wait a bit for retraining */
  for (volatile int i = 0; i < 100000; i++)
    ;

  /* Get count after */
  count_after = test_get_interrupt_count();

  /* Log results */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR,
              0x5PDE7E57); // "SPEED TEST"
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_11__REG_ADDR, count_before);
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_12__REG_ADDR, count_after);
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_13__REG_ADDR,
              count_after - count_before);

  return count_after - count_before;
}

/* ========================================================================== */
/*                    Test 4: Monitor LTSSM State Changes                     */
/* ========================================================================== */

/**
 * @brief Monitor and log LTSSM state changes
 *
 * Reads SCRATCH_11 which contains state transition information:
 * [7:0]   = Previous state
 * [15:8]  = Current state
 * [31:16] = Total state change count
 */
void test_read_ltssm_transitions(void) {
  uint32_t scratch11 = read32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_11__REG_ADDR);

  uint8_t prev_state = scratch11 & 0xFF;
  uint8_t curr_state = (scratch11 >> 8) & 0xFF;
  uint16_t count = (scratch11 >> 16) & 0xFFFF;

  /* Write decoded values for easier reading */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_14__REG_ADDR, (uint32_t)prev_state);
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_15__REG_ADDR,
              (uint32_t)curr_state | ((uint32_t)count << 16));
}

/* ========================================================================== */
/*                    Test 5: Comprehensive Interrupt Test                    */
/* ========================================================================== */

/**
 * @brief Run a comprehensive interrupt test
 *
 * Performs multiple checks and reports results to scratch registers.
 *
 * Test sequence:
 * 1. Check CSR configuration
 * 2. Check initial interrupt count
 * 3. Wait for interrupt activity
 * 4. Trigger interrupts via speed change
 * 5. Report final results
 *
 * @return Test result code
 */
int test_interrupt_system_comprehensive(void) {
  uint32_t initial_count, final_count;
  int activity_detected;

  /* Step 1: Check CSRs */
  test_check_csr_config();

  uint64_t mie_val = read_csr(mie);
  uint64_t mstatus_val = read_csr(mstatus);

  /* Verify CSRs are correct */
  if (!(mie_val & (1 << 11))) {
    /* MEIE not set */
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR, 0xBAD1E00);
    return -1;
  }

  if (!(mstatus_val & (1 << 3))) {
    /* MIE not set */
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR, 0xBAD57A7);
    return -2;
  }

  /* Step 2: Get initial count */
  initial_count = test_get_interrupt_count();

  /* Step 3: Wait for natural interrupt activity */
  activity_detected = test_wait_for_interrupt_activity();

  /* Step 4: Trigger interrupts manually */
  uint32_t triggered_count = test_trigger_interrupts_via_speed_change();

  /* Step 5: Get final count */
  final_count = test_get_interrupt_count();

  /* Report results */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR,
              0x7E570000 | activity_detected);
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_11__REG_ADDR, initial_count);
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_12__REG_ADDR, final_count);
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_13__REG_ADDR, triggered_count);

  /* Success if we got any interrupts */
  if (final_count > initial_count) {
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_14__REG_ADDR,
                0x1170A55); // "INT PASS"
    return 0;
  } else {
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_14__REG_ADDR,
                0x107FA11); // "INT FAIL"
    return -3;
  }
}

/* ========================================================================== */
/*                    Quick Tests You Can Run from Main                       */
/* ========================================================================== */

/*
 * Add to main.c after pcie_init_interrupts():
 *
 * // Quick Test 1: Just check if interrupt count increments
 * uint32_t count1 = test_get_interrupt_count();
 * for (volatile int i = 0; i < 100000; i++);  // Small delay
 * uint32_t count2 = test_get_interrupt_count();
 * if (count2 > count1) {
 *     write32_reg(SMC_CPU_SCRATCH_10, 0x1170A55);  // Interrupts working!
 * }
 *
 * // Quick Test 2: Trigger interrupts and count them
 * test_trigger_interrupts_via_speed_change();
 *
 * // Quick Test 3: Full test suite
 * test_interrupt_system_comprehensive();
 */
