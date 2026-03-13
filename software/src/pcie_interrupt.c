/*
 * PCIe Interrupt Handler Implementation
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * Interrupt handling for PCIe events including LTSSM state changes and resets
 *
 * Implementation based on:
 * -
 * /proj_syseng/user_dev/wayneng/keraunos_soc/docs/pcie_interrupt_implementation_guide.md
 * - DV test patterns from pcie_int_test.py, fabrictimeout_test.py, ttpcie.py
 * - keraunos_pkg.sv interrupt structure definitions
 *
 * Key Architecture Notes:
 * 1. LTSSM interrupt is at PLIC bit 112 (hsio_intreq[4].rsvd[2]) - GREN-1096
 * workaround
 * 2. Other PCIe interrupts are at bits 115-123 (pcie_intreq structure)
 * 3. Interrupt flow: PCIe → Interrupt Consolidator → SMU → PLIC → SMC CPU
 * 4. Two-level masking: PCIe controller masks + PLIC enables
 * 5. Write-1-to-Clear (RW1C) for all interrupt status registers
 */

#include "pcie_interrupt.h"
#include "csr.h"
#include "pcie_helpers.h"
#include "pcie_init.h"
#include "platform.h"
#include "plic.h"
#include "regs.h"
#include "scratch.h"
#include <stdint.h>

/* PLIC base address for Keraunos SMC - from mimir_soc_smc_view_alt_reg.h */
#define SMC_CPU_SMC_CLUSTER_PLIC_REG_MAP_BASE_ADDR (0xC4000000)

extern "C" void __metal_vector_table(void);

/* ========================================================================== */
/*                         Interrupt Statistics                               */
/* ========================================================================== */

static volatile uint32_t g_ltssm_interrupt_count = 0;
static volatile uint32_t g_link_down_count = 0;
static volatile uint32_t g_hot_reset_count = 0;
static volatile uint32_t g_config_update_count = 0;
static volatile uint32_t g_flr_count = 0;
static volatile uint32_t g_last_ltssm_state = 0xFF;

/* ========================================================================== */
/*                    Interrupt Setup Functions                               */
/* ========================================================================== */

void pcie_setup_interrupt_vector(void) {
  /* Debug: Entering vector setup */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_7__REG_ADDR, 0x6EC00001);

  /* Debug: Got vector table address */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_7__REG_ADDR, 0x6EC00002);

  /* Get the full 64-bit address */
  uintptr_t vector_table_addr = (uintptr_t)__metal_vector_table;

  /* Debug: Cast complete */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_7__REG_ADDR, 0x6EC00003);

  /* Set mtvec to vectored mode (LSB = 1)
   * Vectored mode: interrupts jump to BASE + 4*cause */
  vector_table_addr |= 1;

  /* Debug: About to write mtvec */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_7__REG_ADDR, 0x6EC00004);

  write_csr(mtvec, vector_table_addr);

  /* Debug: Vector setup complete */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_7__REG_ADDR, 0x6EC000FF);
}

void pcie_enable_machine_interrupts(void) {
  /* Debug: Entering machine interrupt enable */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_7__REG_ADDR, 0x6EC00001);

  /* Enable Machine External Interrupt Enable (MEIE - bit 11) */
  set_csr(mie, MIE_MEIE);

  /* Debug: Machine interrupts enabled */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_7__REG_ADDR, 0x6EC000FF);
}

void pcie_enable_global_interrupts(void) {
  /* Debug: Entering global interrupt enable */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_7__REG_ADDR, 0x61E00001);

  /* Enable MIE bit (bit 3) in mstatus to enable global interrupts */
  set_csr(mstatus, MSTATUS_MIE);

  /* Debug: Global interrupts enabled */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_7__REG_ADDR, 0x61E000FF);
}

void pcie_init_interrupts(void) {
  /* === INITIALIZATION PROGRESS TRACKING ===
   * Use SCRATCH_1 for initialization progress (gets overwritten later with mask
   * data) This prevents conflicts with POST-INIT diagnostics */

  /* Debug marker: Entered function */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR, 0xDEB60001);

  /* Read the current hart ID from mhartid CSR */
  int hartid = read_csr(mhartid);

  /* Debug marker: Read hartid successfully */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR,
              0xDEB60002 | (hartid << 16));

  /* Setup interrupt vector table */
  pcie_setup_interrupt_vector();

  /* Debug marker: Vector table setup complete */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR, 0xDEB60003);

  /* NOTE: PCIe controller interrupt masks are already enabled by
   * pcie_full_init() */
  /* via the enable_interrupts() function called from pcie_enable_ltssm() */

  /* ===== Configure PLIC for PCIe interrupts ===== */

  /* Debug marker: Starting PLIC configuration */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR, 0xDEB60004);

  /* Set priority for all PCIe interrupts (7 = highest priority)
   * Based on PCIe Interrupt Implementation Guide:
   * - LTSSM is critical for link initialization (bit 112)
   * - Hot Reset, FLR, RAS errors are high priority
   * - DMA completion is medium priority
   * - Config updates and misc are lower priority
   */
  set_interrupt_priority(PCIE_PLIC_LTSSM_INTERRUPT_ID,
                         7); /* Critical for link init */
  set_interrupt_priority(PCIE_PLIC_FLR_INTERRUPT_ID, 5);
  set_interrupt_priority(PCIE_PLIC_HOT_RESET_INTERRUPT_ID, 5);
  set_interrupt_priority(PCIE_PLIC_CONFIG_UPDATE_INTERRUPT_ID, 4);
  set_interrupt_priority(PCIE_PLIC_RAS_ERROR_INTERRUPT_ID, 6);
  set_interrupt_priority(PCIE_PLIC_DMA_COMPLETION_INTERRUPT_ID, 4);
  set_interrupt_priority(PCIE_PLIC_CONTROLLER_MISC_INTERRUPT_ID, 4);
  set_interrupt_priority(PCIE_PLIC_NOC_RD_TIMEOUT_INTERRUPT_ID, 6);
  set_interrupt_priority(PCIE_PLIC_NOC_WR_TIMEOUT_INTERRUPT_ID, 6);
  set_interrupt_priority(PCIE_PLIC_SMN_TIMEOUT_INTERRUPT_ID, 6);

  /* Debug marker: Priorities set */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR, 0xDEB60005);

  /* Set interrupt threshold for this core (0 = allow all priorities) */
  set_interrupt_threshold(hartid, 0);

  /* Debug marker: Threshold set */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR, 0xDEB60006);

  /* Enable PCIe interrupts in PLIC for this core
   * NOTE: LTSSM interrupt is at bit 112, NOT in the pcie_intreq structure (bits
   * 115-123) This is due to GREN-1096 workaround where LTSSM is mapped to
   * hsio_intreq[4].rsvd[2]
   */
  enable_interrupt_meip(hartid,
                        PCIE_PLIC_LTSSM_INTERRUPT_ID); /* Bit 112 - CRITICAL */
  enable_interrupt_meip(hartid, PCIE_PLIC_FLR_INTERRUPT_ID);
  enable_interrupt_meip(hartid, PCIE_PLIC_HOT_RESET_INTERRUPT_ID);
  enable_interrupt_meip(hartid, PCIE_PLIC_CONFIG_UPDATE_INTERRUPT_ID);
  enable_interrupt_meip(hartid, PCIE_PLIC_RAS_ERROR_INTERRUPT_ID);
  enable_interrupt_meip(hartid, PCIE_PLIC_DMA_COMPLETION_INTERRUPT_ID);
  enable_interrupt_meip(hartid, PCIE_PLIC_CONTROLLER_MISC_INTERRUPT_ID);
  enable_interrupt_meip(hartid, PCIE_PLIC_NOC_RD_TIMEOUT_INTERRUPT_ID);
  enable_interrupt_meip(hartid, PCIE_PLIC_NOC_WR_TIMEOUT_INTERRUPT_ID);
  enable_interrupt_meip(hartid, PCIE_PLIC_SMN_TIMEOUT_INTERRUPT_ID);

  /* Debug marker: PLIC enables complete */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR, 0xDEB60007);

  /* Initialize statistics */
  g_ltssm_interrupt_count = 0;
  g_link_down_count = 0;
  g_hot_reset_count = 0;
  g_config_update_count = 0;
  g_flr_count = 0;
  g_last_ltssm_state = 0xFF;

  /* Debug marker: Stats initialized */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR, 0xDEB6000A);

  /* Write marker to indicate interrupt system initialized */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_15__REG_ADDR, 0x107E0000);

  /* Debug marker: About to clear pending interrupts */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR, 0xDEB6000D);

  /* Read and verify interrupt masks are enabled */
  uint32_t int_csr_mask =
      read32_reg(PCIE_MGMT_MMR_KPCIE_SII_INT_CSR_MASK_REG_ADDR);
  uint32_t int_csr1_mask =
      read32_reg(PCIE_MGMT_MMR_KPCIE_SII_INT_CSR1_MASK_REG_ADDR);
  uint32_t misc_int_mask0 =
      read32_reg(PCIE_MGMT_MMR_KPCIE_SII_MISC_INT_MASK0_REG_ADDR);
  uint32_t misc_int_mask1 =
      read32_reg(PCIE_MGMT_MMR_KPCIE_SII_MISC_INT_MASK1_REG_ADDR);

  /* Log mask values to verify they're 0xFFFFFFFF (all enabled) */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_2__REG_ADDR,
              (int_csr_mask & 0xFFFF) | ((misc_int_mask1 & 0xFFFF) << 16));
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR,
              (int_csr1_mask & 0xFFFF) | ((misc_int_mask0 & 0xFFFF) << 16));

  /* Clear any pending PCIe interrupts that occurred during initialization
   * This is critical: LTSSM state changes during link training will have set
   * interrupt bits before we configured the interrupt system. We need to clear
   * these stale interrupts so we can detect new state changes. */
  uint32_t init_int_status = 0;
  uint32_t init_misc_status = 0;
  pcie_read_interrupt_status(&init_int_status, &init_misc_status);

  /* Log initial interrupt status to see what was pending from initialization */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_0__REG_ADDR,
              (init_int_status & 0xFFFF) | ((init_misc_status & 0xFFFF) << 16));

  /* Clear all pending interrupts so we start with a clean slate */
  if (init_int_status || init_misc_status) {
    pcie_clear_interrupt_status(init_int_status, init_misc_status);

    /* Also clear any CII (Config Information Indication) status */
    uint32_t cii_status = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_CII_CSR_REG_ADDR);
    if (cii_status) {
      write32_reg(PCIE_MGMT_MMR_KPCIE_SII_CII_CSR_REG_ADDR, cii_status);
    }

    /* Debug: Log that we cleared stale interrupts */
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR,
                0xC1EA5ED0 | ((init_misc_status >> 8) & 0xFF));
  }

  /* Debug marker: Cleared pending interrupts, also claim/complete any pending
   * PLIC interrupts */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR, 0xDEB6000E);

  /* Claim and immediately complete any pending PLIC interrupts */
  uint32_t pending_id;
  int claim_attempts = 0;
  while ((pending_id = claim_interrupt_meip(hartid)) != 0 &&
         claim_attempts < 10) {
    complete_interrupt_meip(hartid, pending_id);
    claim_attempts++;
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR,
                0xDEB6000E | (claim_attempts << 8));
  }

  /* Debug marker: About to enable machine interrupts */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR, 0xDEB6000B);

  /* Enable machine external interrupts in CPU */
  pcie_enable_machine_interrupts();

  /* Debug marker: Machine interrupts enabled */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR, 0xDEB60008);

  /* Debug marker: About to enable global interrupts - POINT OF NO RETURN */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR, 0xDEB6000C);

  /* Write FINAL completion marker BEFORE enabling global interrupts
   * because enabling global interrupts may immediately jump to an interrupt
   * handler */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR, 0xDEB600FF);

  /* ENABLE global interrupts - hardware wiring has been fixed!
   * This enables interrupt-driven PCIe event handling per the implementation
   * guide. May immediately trigger pending interrupts. */

  /* Enable global interrupts LAST - this may immediately trigger pending
   * interrupts and we may never return from this call if an interrupt is
   * pending */
  pcie_enable_global_interrupts();

  /* NOTE: We may never reach this point if an interrupt fires immediately.
   * That's OK - the 0xDEB600FF marker above indicates successful completion. */

  /* Optional marker if we do return (no pending interrupts) */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR, 0xDEB60FFF);

  /* ===== POST-INIT DIAGNOSTICS ===== */
  /* Write comprehensive diagnostic information to SCRATCH registers
   * to verify interrupt system is properly configured and operational */

  /* Read PLIC pending register to see if any interrupts are currently pending
   */
  volatile uint32_t *plic_pending =
      (volatile uint32_t
           *)(uintptr_t)(SMC_CPU_SMC_CLUSTER_PLIC_REG_MAP_BASE_ADDR + 0x001000);
  uint32_t pending_bits_96_127 =
      plic_pending[3]; /* Bits 96-127 (includes PCIe 112, 115-123) */

  /* Read PLIC enable register for this hart */
  volatile uint32_t *plic_enable =
      (volatile uint32_t
           *)(uintptr_t)(SMC_CPU_SMC_CLUSTER_PLIC_REG_MAP_BASE_ADDR + 0x002000 +
                         (hartid * 0x80));
  uint32_t enable_bits_96_127 = plic_enable[3]; /* Our PCIe interrupt enables */

  /* Read CPU interrupt state */
  uint32_t mie_val = read_csr(mie);
  uint32_t mstatus_val = read_csr(mstatus);
  uint32_t mip_val = read_csr(mip);

  /* Read current PCIe interrupt status */
  uint32_t current_int_status = 0;
  uint32_t current_misc_status = 0;
  pcie_read_interrupt_status(&current_int_status, &current_misc_status);

  /* SCRATCH_3: PLIC Pending bits for PCIe interrupts
   * [31:16] = bits 127-112 (upper half)
   * [15:0]  = bits 111-96 (lower half)
   * Should see bit 0 set if interrupt 112 (LTSSM) is pending
   * Should see bits 19-27 set if interrupts 115-123 are pending */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_3__REG_ADDR, pending_bits_96_127);

  /* SCRATCH_4: PLIC Enable bits for PCIe interrupts
   * [31:16] = bits 127-112 (upper half)
   * [15:0]  = bits 111-96 (lower half)
   * Expected: 0x0FF81000 if all PCIe interrupts enabled
   *   Bit 16 = interrupt 112 (LTSSM) - SPECIAL per GREN-1096
   *   Bits 19-27 = interrupts 115-123 (pcie_intreq)
   *
   * Breakdown for verification:
   *   0x0FF81000 = 0000 1111 1111 1000 0001 0000 0000 0000
   *                     │    └──┬──┘ │    │
   *                     │       │     │    └─ Bit 16 (int 112 LTSSM)
   *                     │       │     └────── Reserved
   *                     └───────┴──────────── Bits 19-27 (int 115-123)
   */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR, enable_bits_96_127);

  /* SCRATCH_6: LTSSM diagnostic and runtime status
   * [31:24] = Last claimed interrupt ID (updated by handler, 0x70=LTSSM,
   * 0x73-0x7B=others) [23:16] = LTSSM enable bit status (0xFF if enabled, 0x00
   * if not) [15:8]  = LTSSM pending bit status at init (0xFF if pending, 0x00
   * if not) [7:0]   = Handler call count (increments each interrupt, wraps at
   * 255) Expected after init: 0x00FF0x00 (no interrupts yet, LTSSM enabled, not
   * pending initially) During operation: upper byte shows last interrupt ID,
   * lower byte increments */

  /* Read LTSSM interrupt priority */
  volatile uint32_t *plic_priority =
      (volatile uint32_t
           *)(uintptr_t)(SMC_CPU_SMC_CLUSTER_PLIC_REG_MAP_BASE_ADDR + 0x000000);
  uint32_t ltssm_priority = plic_priority[PCIE_PLIC_LTSSM_INTERRUPT_ID];

  /* Check if LTSSM bit is enabled (bit 16 of register 3) */
  uint8_t ltssm_enabled =
      (enable_bits_96_127 & (1 << (112 - 96))) ? 0xFF : 0x00;

  /* Check if LTSSM bit is pending (bit 16 of register 3) */
  uint8_t ltssm_pending =
      (pending_bits_96_127 & (1 << (112 - 96))) ? 0xFF : 0x00;

  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_6__REG_ADDR,
              (0x00 << 24) | (ltssm_enabled << 16) | (ltssm_pending << 8) |
                  0x00);

  /* SCRATCH_5: CPU Interrupt State
   * [31:24] = mip (pending interrupts from CPU perspective)
   * [23:16] = mstatus (MIE bit should be set - bit 3)
   * [15:0]  = mie (MEIE bit should be set - bit 11)
   * Expected: 0x00xx08xx if interrupts properly enabled */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_5__REG_ADDR,
              ((mip_val & 0xFF) << 24) | ((mstatus_val & 0xFF) << 16) |
                  (mie_val & 0xFFFF));

  /* SCRATCH_7: Current PCIe Interrupt Status (after init)
   * [31:16] = MISC_INT_STATUS0 (LTSSM state info)
   * [15:0]  = INT_CSR status
   * If non-zero, PCIe controller has pending interrupts */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_7__REG_ADDR,
              ((current_misc_status & 0xFFFF) << 16) |
                  (current_int_status & 0xFFFF));

  /* SCRATCH_9: Initialize to show we're ready for config updates
   * Will be overwritten when first config update interrupt fires */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_9__REG_ADDR,
              0x5EAD0000); /* "READY" */

  /* SCRATCH_10: Initialize to show waiting for link events
   * Will show link up/down/hot reset events */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR,
              0x6A170000); /* "WAIT" */

  /* SCRATCH_11: Initialize LTSSM tracking
   * Will show state transitions when they occur */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_11__REG_ADDR, 0x00000000);

  /* SCRATCH_12: Initialize link status
   * Will show speed/width when link comes up */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_12__REG_ADDR, 0x00000000);

  /* SCRATCH_13: Initialize to show LTSSM interrupt configuration details
   * This verifies the GREN-1096 special case mapping is working
   * [31:24] = LTSSM interrupt ID (should be 112 = 0x70)
   * [23:16] = LTSSM priority (should be 7)
   * [15:8]  = LTSSM enable check (0xEE if enabled, 0x00 if not)
   * [7:0]   = LTSSM pending check (0xFF if pending, 0x00 if not)
   * Expected after init: 0x7007EE00 or 0x7007EEFF (if LTSSM already pending) */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_13__REG_ADDR,
              (PCIE_PLIC_LTSSM_INTERRUPT_ID << 24) | (ltssm_priority << 16) |
                  (ltssm_enabled << 8) | ltssm_pending);

  /* SCRATCH_14: Diagnostic summary status
   * [31:24] = Expected enables matched? (0xAA = yes, 0x00 = no)
   * [23:16] = Global interrupts enabled? (0xEE = yes)
   * [15:8]  = PCIe has pending? (0xFF = yes, 0x00 = no)
   * [7:0]   = PLIC sees pending? (0xFF = yes, 0x00 = no) */
  uint8_t enables_ok =
      (enable_bits_96_127 & 0x0FF81000) == 0x0FF81000 ? 0xAA : 0x00;
  uint8_t global_enabled = (mstatus_val & 0x08) ? 0xEE : 0x00;
  uint8_t pcie_pending =
      (current_int_status | current_misc_status) ? 0xFF : 0x00;
  uint8_t plic_has_pending = pending_bits_96_127 ? 0xFF : 0x00;

  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_14__REG_ADDR,
              (enables_ok << 24) | (global_enabled << 16) |
                  (pcie_pending << 8) | plic_has_pending);

  /* SCRATCH_15: Final status and handler readiness
   * [31:16] = 0x107E (init marker)
   * [15:0]  = LTSSM interrupt count (should increment if working) */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_15__REG_ADDR,
              0x107E0000 | (g_ltssm_interrupt_count & 0xFFFF));
}

/* ========================================================================== */
/*                    Interrupt Handler Implementation                        */
/* ========================================================================== */

/**
 * @brief Main PCIe interrupt handler
 *
 * This function handles PCIe interrupts from the PLIC.
 * Called from metal_external_interrupt_vector_handler() which has
 * the interrupt attribute, so this should NOT have it to avoid
 * nested interrupt epilogues and multiple mret instructions.
 */
void pcie_external_interrupt_handler(void) {
  /* CRITICAL: First thing - prove we entered the handler */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_8__REG_ADDR, 0xBADD1E00);

  /* Debug: Interrupt handler entered - update handler call count in SCRATCH_6
   */
  static volatile uint32_t handler_call_count = 0;
  handler_call_count++;

  /* Update only the lower 8 bits of SCRATCH_6 (handler count)
   * Upper 24 bits contain LTSSM diagnostic info from init */
  uint32_t scratch6_val = read32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_6__REG_ADDR);
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_6__REG_ADDR,
              (scratch6_val & 0xFFFFFF00) | (handler_call_count & 0xFF));

  /* Read the current hart ID from mhartid CSR */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_8__REG_ADDR, 0xBADD1E01);
  int hartid = read_csr(mhartid);

  /* Claim the interrupt from PLIC to determine which PCIe interrupt fired */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_8__REG_ADDR, 0xBADD1E02);
  uint32_t claimed_id = claim_interrupt_meip(hartid);

  /* Log claimed ID immediately */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_8__REG_ADDR,
              0xBADD1E03 | (claimed_id << 16));

  /* Log which interrupt was claimed for debugging
   * Update upper 8 bits of SCRATCH_6 with last claimed ID */
  scratch6_val = read32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_6__REG_ADDR);
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_6__REG_ADDR,
              (scratch6_val & 0x00FFFFFF) | ((claimed_id & 0xFF) << 24));

  /* Verify it's a PCIe interrupt (ID 112 or 115-123) */
  if (claimed_id != PCIE_PLIC_LTSSM_INTERRUPT_ID &&
      (claimed_id < PCIE_PLIC_FLR_INTERRUPT_ID ||
       claimed_id > PCIE_PLIC_SMN_TIMEOUT_INTERRUPT_ID)) {
    /* Not a PCIe interrupt - complete it and return */
    complete_interrupt_meip(hartid, claimed_id);

    /* Log unexpected interrupt in SCRATCH_14 */
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_14__REG_ADDR,
                0xBAD00000 | claimed_id);
    return;
  }

  /* ===== Handle interrupts based on claimed ID ===== */

  /* LTSSM State Change Interrupt (bit 112 - SPECIAL CASE per GREN-1096) */
  if (claimed_id == PCIE_PLIC_LTSSM_INTERRUPT_ID) {
    uint32_t ltssm_state_change;
    uint8_t new_state, old_state;

    g_ltssm_interrupt_count++;

    /* Read LTSSM_STATE_CHANGE register (0x181040B4 per guide)
     * This is MISC_INT_STATUS0 - discovered register with LTSSM state info
     * Format:
     * [31:24] = new LTSSM state
     * [23:16] = old LTSSM state
     * [7:0]   = link speed/width
     */
    ltssm_state_change =
        read32_reg(PCIE_MGMT_MMR_KPCIE_SII_MISC_INT_STATUS0_REG_ADDR);
    new_state = (ltssm_state_change >> 24) & 0xFF;
    old_state = (ltssm_state_change >> 16) & 0xFF;

    /* Log state transition */
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_11__REG_ADDR,
                (old_state & 0xFF) | ((new_state & 0xFF) << 8) |
                    ((g_ltssm_interrupt_count & 0xFFFF) << 16));

    /* Check for hot reset states */
    if (new_state == LTSSM_HOT_RESET || new_state == LTSSM_HOT_RESET_ENTRY) {
      g_hot_reset_count++;
      write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR, 0xD0700E57);
    }

    /* Check if link went down (was in L0, now not) */
    if (old_state == LTSSM_L0 && new_state != LTSSM_L0) {
      g_link_down_count++;
      write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR, 0x11DDD06D);
    }

    /* Check if link came up (wasn't in L0, now is) */
    if (old_state != LTSSM_L0 && new_state == LTSSM_L0) {
      pcie_link_status_t status;
      if (pcie_read_link_status(&status) == 0) {
        write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_12__REG_ADDR,
                    (status.link_speed & 0xFF) |
                        ((status.link_width & 0xFF) << 8));
      }
      write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR, 0x110C0000);
    }

    /* Clear LTSSM interrupt in INT_CSR (bit 16 per guide) */
    write32_reg(PCIE_MGMT_MMR_KPCIE_SII_INT_CSR_REG_ADDR, (1 << 16));

    /* Update last state */
    g_last_ltssm_state = new_state;

    /* Complete the interrupt */
    complete_interrupt_meip(hartid, claimed_id);
    return;
  }

  /* ===== Handle CONFIG_UPDATE interrupt explicitly ===== */
  if (claimed_id == PCIE_PLIC_CONFIG_UPDATE_INTERRUPT_ID) {
    uint32_t cii_status;
    uint32_t link_ctrl2;
    uint32_t target_speed;
    uint32_t gen2_ctrl;

    g_config_update_count++;

    /* Read the CII CSR (Configuration Information Indication) register
     * This is an RW1C (Read/Write-1-to-Clear) bitmap that indicates
     * which config DWORDs have been modified by the host */
    cii_status = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_CII_CSR_REG_ADDR);

    /* Log the CII status for debugging */
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_9__REG_ADDR,
                (g_config_update_count & 0xFFFF) |
                    ((cii_status & 0xFFFF) << 16));

    /* Check if host modified Link Control 2 (speed change request)
     * Link Control 2 is at offset 0xA0, which is in DWORD 0x28 (40 decimal)
     * CII bits correspond to modified DWORDs */
    if (cii_status) {
      /* Read Link Control 2 to get target speed */
      link_ctrl2 = read32_reg(0x18400000 +
                              0x00A0); // SMN_DBI_ADDR + PCIECTL_LINK_CONTROL2
      target_speed = link_ctrl2 & 0xF;

      /* Log target speed for debugging */
      write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_8__REG_ADDR,
                  (target_speed << 24) | (link_ctrl2 & 0xFFFFFF));

      /* Trigger directed speed change */
      gen2_ctrl = read32_reg(0x18400000 +
                             0x080C); // SMN_DBI_ADDR + PCIECTL_GEN2_CONTROL
      gen2_ctrl |= (1 << 17);         // directed_speed_change[17] = 1
      write32_reg(0x18400000 + 0x080C, gen2_ctrl);

      /* Log completion */
      write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_6__REG_ADDR, 0xC0115EED);
    }

    /* Clear all set bits in CII CSR by writing them back (RW1C) */
    if (cii_status) {
      write32_reg(PCIE_MGMT_MMR_KPCIE_SII_CII_CSR_REG_ADDR, cii_status);
    }

    /* Clear CONFIG_UPDATE interrupt in INT_CSR (bit 2 per guide) */
    write32_reg(PCIE_MGMT_MMR_KPCIE_SII_INT_CSR_REG_ADDR, (1 << 2));

    /* Complete the interrupt in PLIC */
    complete_interrupt_meip(hartid, claimed_id);

    /* Return early - CONFIG_UPDATE is fully handled */
    return;
  }

  /* ===== Function Level Reset (FLR) ===== */
  if (claimed_id == PCIE_PLIC_FLR_INTERRUPT_ID) {
    uint32_t flr_status;
    uint32_t function_mask;
    uint32_t access_ctrl;

    /* Increment FLR counter */
    g_flr_count++;

    /* Read FLR status register
     * Bits [7:0]:   app_flr_pf_done - Write 1 to signal FLR completion to
     * hardware Bits [15:8]:  cfg_flr_pf_active - Shows which functions have
     * active FLR (read-only)
     */
    flr_status = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_FLR_INT_REG_ADDR);

    /* Extract which function(s) have active FLR (bits [15:8]) */
    function_mask = (flr_status >> 8) & 0xFF;

    /* Log FLR event with function mask */
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_14__REG_ADDR,
                0xF17F0000 | (flr_status & 0xFFFF));

    /* ========== Complete PCIe FLR Sequence ========== */
    /* Per PCIe spec Section 6.6.2: Function-level reset sequence
     * 1. Disable inbound/outbound PCIe traffic
     * 2. Stop all DMA operations
     * 3. Reset internal state (MSI, interrupts, etc.)
     * 4. Re-initialize controller if needed
     * 5. Re-enable traffic
     * 6. Signal completion via app_flr_pf_done
     */

    /* Step 1: Disable PCIe inbound/outbound traffic
     * This isolates the device during reset to prevent data corruption
     */
    access_ctrl =
        read32_reg(PCIE_MGMT_MMR_ACCESS_STATUS_KPCIE_ACCESS_CTRL_REG_ADDR);
    access_ctrl &= ~(ACCESS_CTRL_O_PCIE_OUTBOUND_APP_ENABLE_MASK |
                     ACCESS_CTRL_O_PCIE_INBOUND_APP_ENABLE_MASK);
    write32_reg(PCIE_MGMT_MMR_ACCESS_STATUS_KPCIE_ACCESS_CTRL_REG_ADDR,
                access_ctrl);

    /* Step 2: Stop all active DMA operations
     * Read DMA status and abort any in-progress transfers
     * This prevents data corruption during reset
     */
    uint32_t dma_status =
        read32_reg(PCIE_MGMT_MMR_KPCIE_SII_DMA_HARD_STOP_REG_ADDR);
    if (dma_status & 0xFF) { /* Check if any DMA channels are active */
      /* Issue hard stop to all active DMA channels */
      write32_reg(PCIE_MGMT_MMR_KPCIE_SII_DMA_HARD_STOP_REG_ADDR, 0xFF);

      /* Wait for DMA to quiesce (simple polling with timeout) */
      uint32_t timeout = 1000;
      while (timeout-- > 0) {
        dma_status = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_DMA_HARD_STOP_REG_ADDR);
        if ((dma_status & 0xFF) == 0) {
          break; /* All DMA channels stopped */
        }
        /* Small delay */
        for (volatile int i = 0; i < 100; i++)
          ;
      }

      /* Log DMA stop status */
      write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_13__REG_ADDR,
                  0xD3A50000 | (dma_status & 0xFFFF));
    }

    /* Step 3: Reset function-specific state
     * Clear any function-specific configuration that should be reset
     * Note: Some registers are automatically reset by hardware during FLR
     */

    /* Clear pending MSI/MSI-X interrupts for the function(s) */
    uint32_t msi_csr = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_MSI_CSR_REG_ADDR);
    write32_reg(PCIE_MGMT_MMR_KPCIE_SII_MSI_CSR_REG_ADDR, 0x0);
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_12__REG_ADDR,
                0x35100000 | (msi_csr & 0xFFFF));

    /* Reset any application-specific state tied to the function
     * This would include clearing buffers, resetting state machines, etc.
     * Application-specific code goes here
     */

    /* Clear all pending interrupt status
     * Ensure clean state after reset
     */
    uint32_t int_csr = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_INT_CSR_REG_ADDR);
    uint32_t int_csr1 = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_INT_CSR1_REG_ADDR);

    /* Clear all pending interrupts (RW1C) */
    if (int_csr) {
      write32_reg(PCIE_MGMT_MMR_KPCIE_SII_INT_CSR_REG_ADDR, int_csr);
    }
    if (int_csr1) {
      write32_reg(PCIE_MGMT_MMR_KPCIE_SII_INT_CSR1_REG_ADDR, int_csr1);
    }

    /* Step 4: Re-initialize controller if needed
     * For basic FLR, controller state persists
     * For deep reset, would call pcie_tlbsys0_* functions here
     */

    /* Step 5: Re-enable PCIe traffic
     * Device is now ready to accept transactions again
     */
    access_ctrl |= (ACCESS_CTRL_O_PCIE_OUTBOUND_APP_ENABLE_MASK |
                    ACCESS_CTRL_O_PCIE_INBOUND_APP_ENABLE_MASK);
    write32_reg(PCIE_MGMT_MMR_ACCESS_STATUS_KPCIE_ACCESS_CTRL_REG_ADDR,
                access_ctrl);

    /* Step 6: Signal FLR completion to hardware
     * CRITICAL: Write app_flr_pf_done bits (bits [7:0]) for functions that
     * requested FLR This signals to the hardware that FLR is complete and the
     * device can respond
     *
     * FLR_INT Register Format:
     * Bits [7:0]:  app_flr_pf_done (write 1 to signal completion)
     * Bits [15:8]: cfg_flr_pf_active (read-only, shows active FLR requests)
     */
    if (function_mask) {
      /* Set app_flr_pf_done bits for functions that had active FLR
       * This MUST be done or the device will hang waiting for completion!
       */
      uint32_t flr_done =
          function_mask; /* Set done bits for active functions */
      write32_reg(PCIE_MGMT_MMR_KPCIE_SII_FLR_INT_REG_ADDR, flr_done);
    }

    /* Verify FLR completion
     * Read back to ensure the interrupt has been cleared
     */
    flr_status = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_FLR_INT_REG_ADDR);
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_11__REG_ADDR,
                0xF17C0000 | (flr_status & 0xFFFF));

    /* ========== Post-Reset Notes ========== */
    /* NOTE: FLR does NOT require link re-initialization!
     * - FLR is function-level only (per PCIe spec Section 6.6.2)
     * - The PCIe link remains up and operational in L0
     * - Only the function's configuration space and state are reset
     * - Link training, speed, and width are unaffected
     * - For link-level reset, see Hot Reset handler (ID 116)
     */

    /* Acknowledge FLR completion to PCIe controller
     * The hardware automatically sends FLR completion after we clear the status
     * but we log the completion for debugging
     */
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR,
                0xF17D0000 | (function_mask & 0xFF));

    /* Complete interrupt in PLIC */
    complete_interrupt_meip(hartid, claimed_id);
    return;
  }

  /* ===== Hot Reset / Link Down ===== */
  if (claimed_id == PCIE_PLIC_HOT_RESET_INTERRUPT_ID) {
    g_hot_reset_count++;

    /* Log hot reset event */
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_14__REG_ADDR,
                0xD0700000 | g_hot_reset_count);

    /* Hot reset means link may go down and need re-initialization
     * TODO: Trigger re-initialization if needed
     */

    /* Clear hot reset bit in INT_CSR (bit 1) */
    write32_reg(PCIE_MGMT_MMR_KPCIE_SII_INT_CSR_REG_ADDR, (1 << 1));

    /* Complete interrupt */
    complete_interrupt_meip(hartid, claimed_id);
    return;
  }

  /* ===== RAS Error ===== */
  if (claimed_id == PCIE_PLIC_RAS_ERROR_INTERRUPT_ID) {
    /* Log RAS error */
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_14__REG_ADDR, 0x5A5E5500);

    /* TODO: Read RAS error details from controller registers
     * Log for debugging, potentially trigger recovery
     */

    /* Clear RAS error bit in INT_CSR (bit 3) */
    write32_reg(PCIE_MGMT_MMR_KPCIE_SII_INT_CSR_REG_ADDR, (1 << 3));

    /* Complete interrupt */
    complete_interrupt_meip(hartid, claimed_id);
    return;
  }

  /* ===== DMA Completion ===== */
  if (claimed_id == PCIE_PLIC_DMA_COMPLETION_INTERRUPT_ID) {
    /* Log DMA completion */
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_14__REG_ADDR, 0xD3A00000);

    /* TODO: Read DMA status to identify which channel completed
     * Wake up waiting DMA driver
     */

    /* Clear DMA complete bit in INT_CSR (bit 4) */
    write32_reg(PCIE_MGMT_MMR_KPCIE_SII_INT_CSR_REG_ADDR, (1 << 4));

    /* Complete interrupt */
    complete_interrupt_meip(hartid, claimed_id);
    return;
  }

  /* ===== Controller Miscellaneous ===== */
  if (claimed_id == PCIE_PLIC_CONTROLLER_MISC_INTERRUPT_ID) {
    /* Log misc interrupt */
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_14__REG_ADDR, 0x31500000);

    /* TODO: Read misc interrupt details
     * Could be various controller events
     */

    /* Clear misc bit in INT_CSR (bit 5) */
    write32_reg(PCIE_MGMT_MMR_KPCIE_SII_INT_CSR_REG_ADDR, (1 << 5));

    /* Complete interrupt */
    complete_interrupt_meip(hartid, claimed_id);
    return;
  }

  /* ===== Timeout Interrupts (NOC Read/Write, SMN) ===== */
  if (claimed_id == PCIE_PLIC_NOC_RD_TIMEOUT_INTERRUPT_ID ||
      claimed_id == PCIE_PLIC_NOC_WR_TIMEOUT_INTERRUPT_ID ||
      claimed_id == PCIE_PLIC_SMN_TIMEOUT_INTERRUPT_ID) {

    /* Log timeout type */
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_14__REG_ADDR,
                0x71300000 | (claimed_id & 0xFF));

    /* Timeout indicates transaction stuck or target not responding
     * Log for debugging, may need system recovery
     * TODO: Read timeout status registers for details
     */

    /* Clear timeout bits in INT_CSR
     * Bits may vary - clear corresponding bit based on interrupt ID
     */
    uint32_t timeout_bit = 0;
    if (claimed_id == PCIE_PLIC_NOC_RD_TIMEOUT_INTERRUPT_ID) {
      timeout_bit = (1 << 6); /* Example - verify in spec */
    } else if (claimed_id == PCIE_PLIC_NOC_WR_TIMEOUT_INTERRUPT_ID) {
      timeout_bit = (1 << 7); /* Example - verify in spec */
    } else if (claimed_id == PCIE_PLIC_SMN_TIMEOUT_INTERRUPT_ID) {
      timeout_bit = (1 << 8); /* Example - verify in spec */
    }

    write32_reg(PCIE_MGMT_MMR_KPCIE_SII_INT_CSR_REG_ADDR, timeout_bit);

    /* Complete interrupt */
    complete_interrupt_meip(hartid, claimed_id);
    return;
  }

  /* If we get here, unknown interrupt ID */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_14__REG_ADDR,
              0xBADC1A10 | (claimed_id & 0xFF));
  complete_interrupt_meip(hartid, claimed_id);
}

/* ========================================================================== */
/*                    Weak Symbol for Vector Table                           */
/* ========================================================================== */

/**
 * @brief External interrupt handler for vector table
 *
 * This function is called from the vector table when an external
 * interrupt occurs. It's defined as a weak symbol in vector.S and
 * we override it here to call our PCIe handler.
 */
void metal_external_interrupt_vector_handler(void) __attribute__((interrupt));
void metal_external_interrupt_vector_handler(void) {
  /* Call our PCIe interrupt handler */
  pcie_external_interrupt_handler();
}

/* ========================================================================== */
/*                    Interrupt Statistics Functions                          */
/* ========================================================================== */

void pcie_get_interrupt_stats(uint32_t *ltssm_count, uint32_t *link_down_count,
                              uint32_t *hot_reset_count,
                              uint32_t *config_update_count) {
  if (ltssm_count) {
    *ltssm_count = g_ltssm_interrupt_count;
  }

  if (link_down_count) {
    *link_down_count = g_link_down_count;
  }

  if (hot_reset_count) {
    *hot_reset_count = g_hot_reset_count;
  }

  if (config_update_count) {
    *config_update_count = g_config_update_count;
  }
}

void pcie_reset_interrupt_stats(void) {
  g_ltssm_interrupt_count = 0;
  g_link_down_count = 0;
  g_hot_reset_count = 0;
  g_config_update_count = 0;
  g_flr_count = 0;
  g_last_ltssm_state = 0xFF;
}
