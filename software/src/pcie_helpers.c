/*
 * PCIe Helper Functions Implementation
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * Utility functions for PCIe monitoring, diagnostics, and runtime operations
 */

#include "pcie_helpers.h"
#include "platform.h"
#include "regs.h"
#include "scratch.h"

/* PCIe Controller DBI Addresses */
#define SMN_DBI_ADDR (0x18400000)

/* PCIe Controller Register Offsets */
#define PCIECTL_LINK_CONTROL 0x0080
#define PCIECTL_LINK_STATUS 0x0082
#define PCIECTL_LINK_CONTROL2 0x00A0
#define PCIECTL_LINK_STATUS2 0x00A2
#define PCIECTL_TYPE1_STATUS_COMMAND 0x0004
#define PCIECTL_BAR_OFF 0x0010
#define PCIECTL_PORT_LINK_CTRL 0x0710

/* ========================================================================== */
/*                       Link Status Helper Functions                         */
/* ========================================================================== */

int pcie_is_link_up(void) {
  uint32_t ltssm_state;
  uint32_t data;

  data = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_POWER_MANAGEMENT_REG_ADDR);
  ltssm_state = (data >> 9) & 0x3F;

  return (ltssm_state == LTSSM_L0) ? 1 : 0;
}

int pcie_is_link_training(void) {
  uint32_t ltssm_state;
  uint32_t data;

  data = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_POWER_MANAGEMENT_REG_ADDR);
  ltssm_state = (data >> 9) & 0x3F;

  /* Link is training if in any of the training states */
  return (ltssm_state > LTSSM_DETECT_QUIET && ltssm_state <= LTSSM_CFG_IDLE)
             ? 1
             : 0;
}

int pcie_is_flit_mode(void) {
  uint32_t link_status2;

  /* Read Link Status 2 register */
  link_status2 = read32_reg(SMN_DBI_ADDR + PCIECTL_LINK_STATUS2);

  /* Return bit 10: Flit Mode Status
   * This bit is 1 when the link is operating in flit mode (Gen6).
   * Gen1-5 links will always have this bit as 0.
   */
  return (link_status2 >> 10) & 0x1;
}

const char *pcie_ltssm_state_to_string(uint32_t ltssm_state) {
  switch (ltssm_state) {
  case LTSSM_DETECT_QUIET:
    return "DETECT.QUIET";
  case LTSSM_DETECT_ACT:
    return "DETECT.ACTIVE";
  case LTSSM_POLL_ACTIVE:
    return "POLL.ACTIVE";
  case LTSSM_POLL_COMPLIANCE:
    return "POLL.COMPLIANCE";
  case LTSSM_POLL_CONFIG:
    return "POLL.CONFIG";
  case LTSSM_PRE_DETECT_QUIET:
    return "PRE_DETECT.QUIET";
  case LTSSM_DETECT_WAIT:
    return "DETECT.WAIT";
  case LTSSM_CFG_LINKWD_START:
    return "CFG.LINKWD.START";
  case LTSSM_CFG_LINKWD_ACEPT:
    return "CFG.LINKWD.ACCEPT";
  case LTSSM_CFG_LANENUM_WAIT:
    return "CFG.LANENUM.WAIT";
  case LTSSM_CFG_LANENUM_ACEPT:
    return "CFG.LANENUM.ACCEPT";
  case LTSSM_CFG_COMPLETE:
    return "CFG.COMPLETE";
  case LTSSM_CFG_IDLE:
    return "CFG.IDLE";
  case LTSSM_RCVRY_LOCK:
    return "RECOVERY.LOCK";
  case LTSSM_RCVRY_SPEED:
    return "RECOVERY.SPEED";
  case LTSSM_RCVRY_RCVRCFG:
    return "RECOVERY.RCVRCFG";
  case LTSSM_RCVRY_IDLE:
    return "RECOVERY.IDLE";
  case LTSSM_L0:
    return "L0";
  case LTSSM_L0S:
    return "L0s";
  case LTSSM_L123_SEND_EIDLE:
    return "L123.SEND_EIDLE";
  case LTSSM_L1_IDLE:
    return "L1.IDLE";
  case LTSSM_L2_IDLE:
    return "L2.IDLE";
  case LTSSM_L2_WAKE:
    return "L2.WAKE";
  case LTSSM_DISABLED_ENTRY:
    return "DISABLED.ENTRY";
  case LTSSM_DISABLED_IDLE:
    return "DISABLED.IDLE";
  case LTSSM_DISABLED:
    return "DISABLED";
  case LTSSM_LPBK_ENTRY:
    return "LOOPBACK.ENTRY";
  case LTSSM_LPBK_ACTIVE:
    return "LOOPBACK.ACTIVE";
  case LTSSM_LPBK_EXIT:
    return "LOOPBACK.EXIT";
  case LTSSM_LPBK_EXIT_TIMEOUT:
    return "LOOPBACK.EXIT_TIMEOUT";
  case LTSSM_HOT_RESET_ENTRY:
    return "HOT_RESET.ENTRY";
  case LTSSM_HOT_RESET:
    return "HOT_RESET";
  default:
    return "UNKNOWN";
  }
}

const char *pcie_speed_to_string(uint32_t speed) {
  switch (speed) {
  case PCIE_SPEED_GEN1:
    return "Gen1";
  case PCIE_SPEED_GEN2:
    return "Gen2";
  case PCIE_SPEED_GEN3:
    return "Gen3";
  case PCIE_SPEED_GEN4:
    return "Gen4";
  case PCIE_SPEED_GEN5:
    return "Gen5";
  case PCIE_SPEED_GEN6:
    return "Gen6";
  default:
    return "Unknown";
  }
}

float pcie_speed_to_gtps(uint32_t speed) {
  switch (speed) {
  case PCIE_SPEED_GEN1:
    return 2.5f;
  case PCIE_SPEED_GEN2:
    return 5.0f;
  case PCIE_SPEED_GEN3:
    return 8.0f;
  case PCIE_SPEED_GEN4:
    return 16.0f;
  case PCIE_SPEED_GEN5:
    return 32.0f;
  case PCIE_SPEED_GEN6:
    return 64.0f;
  default:
    return 0.0f;
  }
}

/* ========================================================================== */
/*                      Register Access Helpers                               */
/* ========================================================================== */

uint32_t pcie_read_dbi_reg(uint32_t offset) {
  return read32_reg(SMN_DBI_ADDR + offset);
}

void pcie_write_dbi_reg(uint32_t offset, uint32_t value) {
  write32_reg(SMN_DBI_ADDR + offset, value);
}

uint32_t pcie_read_config_reg(uint32_t offset) {
  /* Configuration space is accessed via DBI */
  return pcie_read_dbi_reg(offset);
}

void pcie_write_config_reg(uint32_t offset, uint32_t value) {
  /* Configuration space is accessed via DBI */
  pcie_write_dbi_reg(offset, value);
}

uint32_t pcie_read_bar(uint8_t bar_num) {
  if (bar_num > 5) {
    return 0xFFFFFFFF;
  }
  return pcie_read_dbi_reg(PCIECTL_BAR_OFF + (bar_num * 4));
}

void pcie_write_bar(uint8_t bar_num, uint32_t value) {
  if (bar_num > 5) {
    return;
  }
  pcie_write_dbi_reg(PCIECTL_BAR_OFF + (bar_num * 4), value);
}

/* ========================================================================== */
/*                      Interrupt Status Helpers                              */
/* ========================================================================== */

int pcie_read_interrupt_status(uint32_t *int_status, uint32_t *misc_status) {
  if (int_status) {
    *int_status = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_INT_CSR_REG_ADDR);
  }

  if (misc_status) {
    /* MISC interrupt STATUS register (discovered through testing)
     * This undocumented register at 0x181040B4 shows pending MISC interrupts
     * including LTSSM state changes, hot resets, and other events.
     * Confirmed by observing value changes during link speed/width tests.
     */
    *misc_status =
        read32_reg(PCIE_MGMT_MMR_KPCIE_SII_MISC_INT_STATUS0_REG_ADDR);
  }

  return 0;
}

void pcie_clear_interrupt_status(uint32_t int_status, uint32_t misc_status) {
  /* Write 1 to clear INT_CSR interrupt status bits (RW1C) */
  if (int_status) {
    write32_reg(PCIE_MGMT_MMR_KPCIE_SII_INT_CSR_REG_ADDR, int_status);
  }

  /* Clear MISC interrupt status bits (RW1C)
   * Write to the MISC_INT_STATUS0 register (discovered at 0x181040B4)
   * This register is RW1C - writing 1 to a bit clears that interrupt.
   */
  if (misc_status) {
    write32_reg(PCIE_MGMT_MMR_KPCIE_SII_MISC_INT_STATUS0_REG_ADDR, misc_status);
  }
}

int pcie_is_ltssm_interrupt_pending(void) {
  uint32_t misc_status;

  misc_status = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_MISC_INT_MASK1_REG_ADDR);

  /* LTSSM state change interrupt is bit 3 */
  return (misc_status & (1 << 3)) ? 1 : 0;
}

/* ========================================================================== */
/*                         Diagnostic Helpers                                 */
/* ========================================================================== */

void pcie_write_status_to_scratch(const pcie_link_status_t *status,
                                  uint32_t scratch_base) {
  if (!status || scratch_base > 13) {
    return;
  }

  uint32_t scratch_addr_base =
      SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR + ((scratch_base - 10) * 4);

  /* Write LTSSM state */
  write32_reg(scratch_addr_base, status->ltssm_state);

  /* Write speed and width combined */
  write32_reg(scratch_addr_base + 4, (status->link_speed & 0xFFFF) |
                                         ((status->link_width & 0xFFFF) << 16));

  /* Write enumeration and flit mode */
  write32_reg(scratch_addr_base + 8, (status->enumerated & 0xFFFF) |
                                         ((status->flit_mode & 0xFFFF) << 16));
}

int pcie_diagnostic_check(uint32_t scratch_base) {
  pcie_link_status_t status;
  int ret;

  ret = pcie_get_link_status(&status);
  if (ret != 0) {
    return ret;
  }

  pcie_write_status_to_scratch(&status, scratch_base);

  return 0;
}

uint32_t pcie_read_ltssm_state(void) {
  uint32_t data;

  data = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_POWER_MANAGEMENT_REG_ADDR);
  return (data >> 9) & 0x3F;
}

int pcie_check_speed(uint32_t expected_speed) {
  pcie_link_status_t status;

  if (pcie_get_link_status(&status) != 0) {
    return 0;
  }

  return (status.link_speed == expected_speed) ? 1 : 0;
}

int pcie_check_width(uint32_t expected_width) {
  pcie_link_status_t status;

  if (pcie_get_link_status(&status) != 0) {
    return 0;
  }

  return (status.link_width == expected_width) ? 1 : 0;
}

/* ========================================================================== */
/*                        Link Control Helpers                                */
/* ========================================================================== */

int pcie_retrain_link(void) {
  uint32_t link_ctrl;

  /* Read Link Control register */
  link_ctrl = pcie_read_dbi_reg(PCIECTL_LINK_CONTROL);

  /* Set Retrain Link bit (bit 5) */
  link_ctrl |= (1 << 5);

  /* Write back */
  pcie_write_dbi_reg(PCIECTL_LINK_CONTROL, link_ctrl);

  return 0;
}

int pcie_disable_ltssm(void) {
  uint32_t data;

  /* Read current LTSSM enable register */
  data = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_CORE_CONTROL_REG_ADDR);

  /* Clear LTSSM enable bit (bit 0) */
  data &= ~(1 << 0);

  /* Write back */
  write32_reg(PCIE_MGMT_MMR_KPCIE_SII_CORE_CONTROL_REG_ADDR, data);

  return 0;
}

int pcie_request_speed_change(uint32_t target_speed) {
  uint32_t link_ctrl2;

  if (target_speed < 1 || target_speed > 6) {
    return -1;
  }

  /* Read Link Control 2 register */
  link_ctrl2 = pcie_read_dbi_reg(PCIECTL_LINK_CONTROL2);

  /* Clear current target link speed (bits [3:0]) */
  link_ctrl2 &= ~0xF;

  /* Set new target speed */
  link_ctrl2 |= (target_speed & 0xF);

  /* Write back */
  pcie_write_dbi_reg(PCIECTL_LINK_CONTROL2, link_ctrl2);

  /* Retrain link to apply the change */
  return pcie_retrain_link();
}

int pcie_request_width_change(uint32_t target_width) {
  uint32_t port_link_ctrl;
  uint32_t encoded_width;

  /* Encode width to PCIe standard encoding */
  switch (target_width) {
  case 1:
    encoded_width = 0x01;
    break;
  case 2:
    encoded_width = 0x02;
    break;
  case 4:
    encoded_width = 0x04;
    break;
  case 8:
    encoded_width = 0x08;
    break;
  case 16:
    encoded_width = 0x10;
    break;
  default:
    return -1;
  }

  /* Read Port Link Control register */
  port_link_ctrl = pcie_read_dbi_reg(PCIECTL_PORT_LINK_CTRL);

  /* Clear link mode enable bits [22:16] */
  port_link_ctrl &= ~(0x7F << 16);

  /* Set new width */
  port_link_ctrl |= (encoded_width << 16);

  /* Write back */
  pcie_write_dbi_reg(PCIECTL_PORT_LINK_CTRL, port_link_ctrl);

  /* Retrain link to apply the change */
  return pcie_retrain_link();
}

/* ========================================================================== */
/*                         Reset Detection Helpers                            */
/* ========================================================================== */

int pcie_check_hot_reset(void) {
  uint32_t ltssm_state = pcie_read_ltssm_state();

  return ((ltssm_state == LTSSM_HOT_RESET_ENTRY) ||
          (ltssm_state == LTSSM_HOT_RESET))
             ? 1
             : 0;
}

int pcie_check_link_down(void) { return pcie_is_link_up() ? 0 : 1; }

int pcie_monitor_state_change(uint32_t *prev_state) {
  uint32_t current_state;

  if (!prev_state) {
    return 0;
  }

  current_state = pcie_read_ltssm_state();

  if (current_state != *prev_state) {
    *prev_state = current_state;
    return 1;
  }

  return 0;
}
