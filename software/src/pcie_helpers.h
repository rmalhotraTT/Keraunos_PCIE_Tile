/*
 * PCIe Helper Functions Header
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * Utility functions for PCIe monitoring, diagnostics, and runtime operations
 */

#ifndef PCIE_HELPERS_H
#define PCIE_HELPERS_H

#include "pcie_init.h"
#include <stdint.h>

/* ========================================================================== */
/*                    PCIe SII Register Definitions                           */
/* ========================================================================== */

/* Core documented registers needed for FLR and interrupt handling
 * From: validation/firmware/metal/tools/registers/ker/keraunos_soc_reg.h
 */
#define PCIE_MGMT_MMR_KPCIE_SII_DMA_HARD_STOP_REG_ADDR (0x18104008)
#define PCIE_MGMT_MMR_KPCIE_SII_MSI_CSR_REG_ADDR (0x18104054)
#define PCIE_MGMT_MMR_KPCIE_SII_FLR_INT_REG_ADDR (0x18104064)
#define PCIE_MGMT_MMR_KPCIE_SII_INT_CSR_REG_ADDR (0x18104094)
#define PCIE_MGMT_MMR_KPCIE_SII_INT_CSR1_REG_ADDR (0x18104098)
#define PCIE_MGMT_MMR_KPCIE_SII_CII_CSR_REG_ADDR (0x181040AC)

/* Interrupt mask registers */
#define PCIE_MGMT_MMR_KPCIE_SII_INT_CSR_MASK_REG_ADDR (0x181040DC)
#define PCIE_MGMT_MMR_KPCIE_SII_INT_CSR1_MASK_REG_ADDR (0x181040E0)
#define PCIE_MGMT_MMR_KPCIE_SII_MISC_INT_MASK0_REG_ADDR (0x181040E4)
#define PCIE_MGMT_MMR_KPCIE_SII_MISC_INT_MASK1_REG_ADDR (0x181040E8)

/* PCIe Access Control Register - controls traffic enable/disable
 * Used during FLR sequence to isolate device during reset
 */
#define PCIE_MGMT_MMR_KPCIE_ACCESS_CTRL_REG_ADDR (0x1804FFF8)

/* ACCESS_CTRL Register Bit Masks */
#define ACCESS_CTRL_O_PCIE_OUTBOUND_APP_ENABLE_MASK                            \
  (0x1) /* Bit 0: Outbound traffic */
#define ACCESS_CTRL_O_PCIE_OUTBOUND_APP_ENABLE_SHIFT (0)
#define ACCESS_CTRL_O_PCIE_INBOUND_APP_ENABLE_MASK                             \
  (0x10000) /* Bit 16: Inbound traffic */
#define ACCESS_CTRL_O_PCIE_INBOUND_APP_ENABLE_SHIFT (16)

/* ========================================================================== */
/*                    Undocumented PCIe SII Registers                         */
/* ========================================================================== */

/* These registers were discovered through testing and are not in the official
 * register map. They appear to be status/control registers in the gap between
 * L0P_CSR (0xB0) and ERR_STATUS0_INT_MASK (0xD0).
 */

/* MISC Interrupt Status Register (discovered) */
#define PCIE_MGMT_MMR_KPCIE_SII_MISC_INT_STATUS0_REG_OFFSET (0x000000B4)
#define PCIE_MGMT_MMR_KPCIE_SII_MISC_INT_STATUS0_REG_ADDR (0x181040B4)

/* Additional undocumented registers in the 0xB4-0xCC range
 * Purpose unknown - candidates for MISC_INT_STATUS1 or other status registers
 */
#define PCIE_MGMT_MMR_KPCIE_SII_UNDOC_B8_REG_ADDR (0x181040B8)
#define PCIE_MGMT_MMR_KPCIE_SII_UNDOC_BC_REG_ADDR (0x181040BC)
#define PCIE_MGMT_MMR_KPCIE_SII_UNDOC_C0_REG_ADDR (0x181040C0)
#define PCIE_MGMT_MMR_KPCIE_SII_UNDOC_C4_REG_ADDR (0x181040C4)
#define PCIE_MGMT_MMR_KPCIE_SII_UNDOC_C8_REG_ADDR (0x181040C8)
#define PCIE_MGMT_MMR_KPCIE_SII_UNDOC_CC_REG_ADDR (0x181040CC)

/* ========================================================================== */
/*                           PCIe Link State Definitions                      */
/* ========================================================================== */

/* LTSSM States (bits [14:9] of POWER_MANAGEMENT register) */
#define LTSSM_DETECT_QUIET 0x00
#define LTSSM_DETECT_ACT 0x01
#define LTSSM_POLL_ACTIVE 0x02
#define LTSSM_POLL_COMPLIANCE 0x03
#define LTSSM_POLL_CONFIG 0x04
#define LTSSM_PRE_DETECT_QUIET 0x05
#define LTSSM_DETECT_WAIT 0x06
#define LTSSM_CFG_LINKWD_START 0x07
#define LTSSM_CFG_LINKWD_ACEPT 0x08
#define LTSSM_CFG_LANENUM_WAIT 0x09
#define LTSSM_CFG_LANENUM_ACEPT 0x0A
#define LTSSM_CFG_COMPLETE 0x0B
#define LTSSM_CFG_IDLE 0x0C
#define LTSSM_RCVRY_LOCK 0x0D
#define LTSSM_RCVRY_SPEED 0x0E
#define LTSSM_RCVRY_RCVRCFG 0x0F
#define LTSSM_RCVRY_IDLE 0x10
#define LTSSM_L0 0x11
#define LTSSM_L0S 0x12
#define LTSSM_L123_SEND_EIDLE 0x13
#define LTSSM_L1_IDLE 0x14
#define LTSSM_L2_IDLE 0x15
#define LTSSM_L2_WAKE 0x16
#define LTSSM_DISABLED_ENTRY 0x17
#define LTSSM_DISABLED_IDLE 0x18
#define LTSSM_DISABLED 0x19
#define LTSSM_LPBK_ENTRY 0x1A
#define LTSSM_LPBK_ACTIVE 0x1B
#define LTSSM_LPBK_EXIT 0x1C
#define LTSSM_LPBK_EXIT_TIMEOUT 0x1D
#define LTSSM_HOT_RESET_ENTRY 0x1E
#define LTSSM_HOT_RESET 0x1F

/* PCIe Link Speeds */
#define PCIE_SPEED_GEN1 1 /* 2.5 GT/s */
#define PCIE_SPEED_GEN2 2 /* 5.0 GT/s */
#define PCIE_SPEED_GEN3 3 /* 8.0 GT/s */
#define PCIE_SPEED_GEN4 4 /* 16.0 GT/s */
#define PCIE_SPEED_GEN5 5 /* 32.0 GT/s */
#define PCIE_SPEED_GEN6 6 /* 64.0 GT/s */

/* ========================================================================== */
/*                       Link Status Helper Functions                         */
/* ========================================================================== */

/**
 * @brief Read current PCIe link status
 * @param status Pointer to status structure to fill
 * @return 0 on success, negative on error
 *
 * Wrapper around pcie_get_link_status() for convenience.
 * Fills in: ltssm_state, link_speed, link_width, enumerated, flit_mode
 */
static inline int pcie_read_link_status(pcie_link_status_t *status) {
  return pcie_get_link_status(status);
}

/**
 * @brief Check if link is in L0 (active) state
 * @return 1 if in L0, 0 otherwise
 */
int pcie_is_link_up(void);

/**
 * @brief Check if link training is in progress
 * @return 1 if training, 0 otherwise
 */
int pcie_is_link_training(void);

/**
 * @brief Get LTSSM state as a readable string
 * @param ltssm_state LTSSM state value
 * @return String representation of the state
 */
const char *pcie_ltssm_state_to_string(uint32_t ltssm_state);

/**
 * @brief Get link speed as a readable string (e.g., "Gen3")
 * @param speed Speed value (1-6)
 * @return String representation of the speed
 */
const char *pcie_speed_to_string(uint32_t speed);

/**
 * @brief Get link speed in GT/s
 * @param speed Speed value (1-6)
 * @return Speed in GT/s (e.g., 8.0 for Gen3), or 0.0 for invalid
 */
float pcie_speed_to_gtps(uint32_t speed);

/**
 * @brief Check if device has been enumerated by the host
 * @return 1 if enumerated, 0 if not
 *
 * Checks if Memory Space Enable is set and BAR0 is programmed
 */
static inline int pcie_is_enumerated(void) { return pcie_check_enumeration(); }

/**
 * @brief Check if link is in Flit Mode (PCIe 6.0)
 * @return 1 if in Flit Mode, 0 if in non-Flit Mode or link down
 *
 * Flit Mode is only available in PCIe Gen6 (64 GT/s).
 * Gen1-5 links will always return 0.
 *
 * Reads Link Status 2 register bit 10 (Flit Mode Status).
 * Note: Flit Mode Disable is controlled via Link Control bit 13.
 * Note: Flit Mode Supported is indicated in CAP_EXP bit 31.
 */
int pcie_is_flit_mode(void);

/* ========================================================================== */
/*                      Register Access Helpers                               */
/* ========================================================================== */

/**
 * @brief Read a PCIe DBI (Device/Bridge Interface) register
 * @param offset Register offset from DBI base
 * @return Register value
 */
uint32_t pcie_read_dbi_reg(uint32_t offset);

/**
 * @brief Write a PCIe DBI register
 * @param offset Register offset from DBI base
 * @param value Value to write
 */
void pcie_write_dbi_reg(uint32_t offset, uint32_t value);

/**
 * @brief Read PCIe configuration space register
 * @param offset Configuration space offset (0-0xFFF)
 * @return Register value
 */
uint32_t pcie_read_config_reg(uint32_t offset);

/**
 * @brief Write PCIe configuration space register
 * @param offset Configuration space offset (0-0xFFF)
 * @param value Value to write
 */
void pcie_write_config_reg(uint32_t offset, uint32_t value);

/**
 * @brief Read a BAR value
 * @param bar_num BAR number (0-5)
 * @return BAR value
 */
uint32_t pcie_read_bar(uint8_t bar_num);

/**
 * @brief Write a BAR value
 * @param bar_num BAR number (0-5)
 * @param value BAR value to write
 */
void pcie_write_bar(uint8_t bar_num, uint32_t value);

/* ========================================================================== */
/*                      Interrupt Status Helpers                              */
/* ========================================================================== */

/**
 * @brief Read PCIe interrupt status
 * @param int_status Pointer to store CSR interrupt status
 * @param misc_status Pointer to store MISC interrupt status
 * @return 0 on success
 */
int pcie_read_interrupt_status(uint32_t *int_status, uint32_t *misc_status);

/**
 * @brief Clear PCIe interrupt status bits
 * @param int_status CSR interrupt bits to clear
 * @param misc_status MISC interrupt bits to clear
 */
void pcie_clear_interrupt_status(uint32_t int_status, uint32_t misc_status);

/**
 * @brief Check if LTSSM state change interrupt is pending
 * @return 1 if pending, 0 otherwise
 */
int pcie_is_ltssm_interrupt_pending(void);

/* ========================================================================== */
/*                         Diagnostic Helpers                                 */
/* ========================================================================== */

/**
 * @brief Print link status to scratch registers for debugging
 * @param status Pointer to link status structure
 * @param scratch_base Base scratch register number (10-15 recommended)
 *
 * Writes status information to consecutive scratch registers:
 * - scratch_base+0: LTSSM state
 * - scratch_base+1: Link speed and width
 * - scratch_base+2: Enumeration and flit mode
 */
void pcie_write_status_to_scratch(const pcie_link_status_t *status,
                                  uint32_t scratch_base);

/**
 * @brief Perform a link status check and write results to scratch registers
 * @param scratch_base Base scratch register number
 * @return 0 on success, negative on error
 */
int pcie_diagnostic_check(uint32_t scratch_base);

/**
 * @brief Read and return current LTSSM state only
 * @return LTSSM state value (0-31)
 */
uint32_t pcie_read_ltssm_state(void);

/**
 * @brief Check if link speed matches expected value
 * @param expected_speed Expected speed (1-6 for Gen1-Gen6)
 * @return 1 if match, 0 if mismatch
 */
int pcie_check_speed(uint32_t expected_speed);

/**
 * @brief Check if link width matches expected value
 * @param expected_width Expected width (1, 2, 4, 8, 16)
 * @return 1 if match, 0 if mismatch
 */
int pcie_check_width(uint32_t expected_width);

/* ========================================================================== */
/*                        Link Control Helpers                                */
/* ========================================================================== */

/**
 * @brief Initiate link retrain
 * @return 0 on success, negative on error
 *
 * Triggers PCIe link retraining by setting the Retrain Link bit
 */
int pcie_retrain_link(void);

/**
 * @brief Disable LTSSM
 * @return 0 on success
 */
int pcie_disable_ltssm(void);

/**
 * @brief Request link speed change
 * @param target_speed Target speed (1-6 for Gen1-Gen6)
 * @return 0 on success, negative on error
 */
int pcie_request_speed_change(uint32_t target_speed);

/**
 * @brief Request link width change (downgrade)
 * @param target_width Target width (1, 2, 4, 8, 16)
 * @return 0 on success, negative on error
 */
int pcie_request_width_change(uint32_t target_width);

/* ========================================================================== */
/*                         Reset Detection Helpers                            */
/* ========================================================================== */

/**
 * @brief Check if a hot reset has occurred
 * @return 1 if hot reset detected, 0 otherwise
 */
int pcie_check_hot_reset(void);

/**
 * @brief Check if link went down (not in L0)
 * @return 1 if link is down, 0 if link is up
 */
int pcie_check_link_down(void);

/**
 * @brief Monitor for link state changes
 * @param prev_state Pointer to previous LTSSM state (updated on change)
 * @return 1 if state changed, 0 if same
 */
int pcie_monitor_state_change(uint32_t *prev_state);

#endif /* PCIE_HELPERS_H */
