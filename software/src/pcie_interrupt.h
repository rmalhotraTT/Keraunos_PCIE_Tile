/*
 * PCIe Interrupt Handler Header
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * Interrupt handling for PCIe events including LTSSM state changes and resets
 */

#ifndef PCIE_INTERRUPT_H
#define PCIE_INTERRUPT_H

#include <stdint.h>

/* ========================================================================== */
/*                         PCIe PLIC Interrupt IDs                            */
/* ========================================================================== */
/* From Keraunos-E100 Architecture Spec Table 3: Wired interrupts to SMC PLIC
 * Based on keraunos_pkg.sv and PCIe Interrupt Implementation Guide
 *
 * IMPORTANT: LTSSM interrupt is mapped separately at bit 112 (GREN-1096)
 *            via hsio_intreq[4].rsvd[2] = pcie_interrupt_i[0].legacy_intx
 */

#define PCIE_PLIC_LTSSM_INTERRUPT_ID 112     /* LTSSM State Change (SPECIAL) */
#define PCIE_PLIC_FLR_INTERRUPT_ID 115       /* Function Level Reset */
#define PCIE_PLIC_HOT_RESET_INTERRUPT_ID 116 /* Hot Reset / Link Down */
#define PCIE_PLIC_CONFIG_UPDATE_INTERRUPT_ID 117  /* Configuration Update */
#define PCIE_PLIC_RAS_ERROR_INTERRUPT_ID 118      /* RAS Error */
#define PCIE_PLIC_DMA_COMPLETION_INTERRUPT_ID 119 /* DMA Completion */
#define PCIE_PLIC_CONTROLLER_MISC_INTERRUPT_ID                                 \
  120                                             /* Misc Controller Events    \
                                                   */
#define PCIE_PLIC_NOC_RD_TIMEOUT_INTERRUPT_ID 121 /* NOC Read Timeout */
#define PCIE_PLIC_NOC_WR_TIMEOUT_INTERRUPT_ID 122 /* NOC Write Timeout */
#define PCIE_PLIC_SMN_TIMEOUT_INTERRUPT_ID 123    /* SMN Timeout */

/* ========================================================================== */
/*                         Interrupt Handler Functions                        */
/* ========================================================================== */

/**
 * @brief Setup interrupt vector table
 *
 * Configures the mtvec CSR to point to the interrupt vector table
 * in vectored mode.
 */
void pcie_setup_interrupt_vector(void);

/**
 * @brief Enable machine-level interrupts
 *
 * Enables Machine External Interrupts (MEIE) in the mie CSR.
 */
void pcie_enable_machine_interrupts(void);

/**
 * @brief Enable global interrupts
 *
 * Sets the MIE bit in mstatus to enable interrupt processing globally.
 */
void pcie_enable_global_interrupts(void);

/**
 * @brief Initialize complete interrupt system
 *
 * Performs all steps needed to enable interrupts:
 * 1. Setup interrupt vector
 * 2. Enable machine interrupts
 * 3. Enable global interrupts
 *
 * Call this after pcie_full_init() completes.
 */
void pcie_init_interrupts(void);

/**
 * @brief PCIe external interrupt handler
 *
 * This function is called when a PCIe interrupt occurs.
 * It checks interrupt status and handles:
 * - LTSSM state changes
 * - Link down events
 * - Hot resets
 * - Function Level Reset (FLR)
 * - Configuration updates
 * - RAS errors
 * - DMA completion
 * - NOC/SMN timeouts
 *
 * FLR Handler performs the following sequence:
 * 1. Stop all active DMA operations
 * 2. Reset function-specific state
 * 3. Clear pending interrupts
 * 4. Acknowledge FLR completion to host
 */
void pcie_external_interrupt_handler(void);

/**
 * @brief Get interrupt statistics
 * @param ltssm_count Pointer to store LTSSM interrupt count
 * @param link_down_count Pointer to store link down event count
 * @param hot_reset_count Pointer to store hot reset count
 * @param config_update_count Pointer to store config update interrupt count
 */
void pcie_get_interrupt_stats(uint32_t *ltssm_count, uint32_t *link_down_count,
                              uint32_t *hot_reset_count,
                              uint32_t *config_update_count);

/**
 * @brief Reset interrupt statistics
 */
void pcie_reset_interrupt_stats(void);

#endif /* PCIE_INTERRUPT_H */
