/*
 * PCIe Link Control Implementation (P0)
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcie_ctrl.h"
#include "pcie_config.h"
#include "pcie_helpers.h"
#include "pcie_init.h"
#include "platform.h"
#include "regs.h"
#include <stdio.h>

/* ========================================================================== */
/*                    PCIe Configuration Space Registers                      */
/* ========================================================================== */

/* PCIe Device Control/Status register offset */
#define PCIECTL_DEVICE_CONTROL                                                 \
  0x0078 /* Device Control[15:0] | Device Status[31:16] */

/* ========================================================================== */
/*                          Internal Helper Functions                         */
/* ========================================================================== */

/**
 * @brief Delay function
 */
static void ctrl_delay_cycles(uint32_t cycles) {
  volatile uint32_t i;
  for (i = 0; i < cycles; i++) {
    /* Busy wait */
  }
}

/* ========================================================================== */
/*                          Link Control Implementation                       */
/* ========================================================================== */

int pcie_ctrl_enable_link(uint32_t timeout_iterations,
                          pcie_ctrl_enable_result_t *result) {
  uint32_t data;
  uint32_t ltssm_state;
  uint32_t i;
  pcie_link_status_t link_status;

  /* Initialize result structure if provided */
  if (result) {
    result->result = -1;
    result->final_ltssm_state = 0;
    result->negotiated_speed = 0;
    result->negotiated_width = 0;
    result->iterations = 0;
  }

  /* Step 1: Enable LTSSM state machine */
  printf("[PCIe CTRL] Reading CORE_CONTROL @ 0x%08x\n",
         (unsigned)PCIE_MGMT_MMR_KPCIE_SII_CORE_CONTROL_REG_ADDR);
  data = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_CORE_CONTROL_REG_ADDR);
  printf("[PCIe CTRL] CORE_CONTROL read: 0x%08x\n", data);

  /* Set LTSSM enable bit (bit 0) and CII enable (bit 9) */
  data |= (1 << 0) | (1 << 9);
  printf("[PCIe CTRL] Writing CORE_CONTROL: 0x%08x\n", data);
  write32_reg(PCIE_MGMT_MMR_KPCIE_SII_CORE_CONTROL_REG_ADDR, data);

  /* Verify LTSSM was enabled */
  data = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_CORE_CONTROL_REG_ADDR);
  printf("[PCIe CTRL] CORE_CONTROL verify: 0x%08x\n", data);
  if (!(data & (1 << 0))) {
    printf("[PCIe CTRL] ERROR: LTSSM enable bit not set!\n");
    if (result) {
      result->result = -2;
    }
    return -2;
  }

  printf("[PCIe CTRL] LTSSM enabled, polling for L0 (%u iters)...\n",
         timeout_iterations);

  /* Step 2: Poll for L0 state */
  uint32_t last_ltssm = 0xFF;
  for (i = 0; i < timeout_iterations; i++) {
    /* Read LTSSM state from Power Management register */
    data = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_POWER_MANAGEMENT_REG_ADDR);
    ltssm_state = (data >> 9) & 0x3F;

    if (ltssm_state != last_ltssm) {
      printf("[PCIe CTRL] LTSSM: 0x%02x (%s) at iter %u (raw=0x%08x)\n",
             ltssm_state, pcie_ltssm_state_to_string(ltssm_state),
             i, data);
      last_ltssm = ltssm_state;
    }

    if (result) {
      result->iterations = i + 1;
      result->final_ltssm_state = ltssm_state;
    }

    /* Check if we reached L0 */
    if (ltssm_state == LTSSM_L0) {
      printf("[PCIe CTRL] Link reached L0!\n");
      if (pcie_get_link_status(&link_status) == 0) {
        if (result) {
          result->result = 0;
          result->negotiated_speed = link_status.link_speed;
          result->negotiated_width = link_status.link_width;
        }
        return 0;
      }
    }

    /* Check for error states */
    if (ltssm_state == LTSSM_DISABLED || ltssm_state == LTSSM_DISABLED_IDLE ||
        ltssm_state == LTSSM_HOT_RESET) {
      printf("[PCIe CTRL] ERROR: LTSSM in error state 0x%02x\n", ltssm_state);
      if (result) {
        result->result = -3;
      }
      return -3;
    }

    ctrl_delay_cycles(PCIE_LINK_POLL_DELAY_CYCLES);
  }

  printf("[PCIe CTRL] TIMEOUT: LTSSM stuck at 0x%02x (%s) after %u iters\n",
         ltssm_state, pcie_ltssm_state_to_string(ltssm_state),
         timeout_iterations);
  if (result) {
    result->result = -1;
  }
  return -1;
}

int pcie_ctrl_get_link_status(pcie_ctrl_link_status_t *status) {
  pcie_link_status_t link_status;
  uint32_t device_status;

  if (!status) {
    return -1;
  }

  /* Get base link status */
  if (pcie_get_link_status(&link_status) != 0) {
    return -1;
  }

  /* Fill in ctrl status structure */
  status->link_up = (link_status.ltssm_state == LTSSM_L0) ? 1 : 0;
  status->ltssm_state = link_status.ltssm_state;
  status->negotiated_speed = link_status.link_speed;
  status->negotiated_width = link_status.link_width;
  status->enumerated = link_status.enumerated;
  status->flit_mode = link_status.flit_mode;

  /* Check for training errors in Device Status register */
  device_status = pcie_read_dbi_reg(PCIECTL_DEVICE_CONTROL);

  /* Extract error bits from Device Status (upper 16 bits) */
  /* Bit 0: Correctable Error Detected */
  /* Bit 1: Non-Fatal Error Detected */
  /* Bit 2: Fatal Error Detected */
  /* Bit 3: Unsupported Request Detected */
  status->training_errors = (device_status >> 16) & 0xF;

  return 0;
}

int pcie_ctrl_disable_link(void) {
  uint32_t data;

  /* Read current CORE_CONTROL register */
  data = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_CORE_CONTROL_REG_ADDR);

  /* Clear LTSSM enable bit (bit 0) */
  data &= ~(1 << 0);

  /* Write back */
  write32_reg(PCIE_MGMT_MMR_KPCIE_SII_CORE_CONTROL_REG_ADDR, data);

  return 0;
}

int pcie_ctrl_is_link_up(void) {
  uint32_t data;
  uint32_t ltssm_state;

  data = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_POWER_MANAGEMENT_REG_ADDR);
  ltssm_state = (data >> 9) & 0x3F;

  return (ltssm_state == LTSSM_L0) ? 1 : 0;
}

int pcie_ctrl_wait_for_l0(uint32_t timeout_iterations) {
  uint32_t data;
  uint32_t ltssm_state;
  uint32_t i;

  for (i = 0; i < timeout_iterations; i++) {
    data = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_POWER_MANAGEMENT_REG_ADDR);
    ltssm_state = (data >> 9) & 0x3F;

    if (ltssm_state == LTSSM_L0) {
      return 0; /* Success */
    }

    ctrl_delay_cycles(PCIE_LINK_POLL_DELAY_CYCLES);
  }

  return -1; /* Timeout */
}

int pcie_ctrl_get_negotiated_speed(uint32_t *speed) {
  pcie_ctrl_link_status_t status;

  if (!speed) {
    return -1;
  }

  if (pcie_ctrl_get_link_status(&status) != 0) {
    return -1;
  }

  if (!status.link_up) {
    return -1;
  }

  *speed = status.negotiated_speed;
  return 0;
}

int pcie_ctrl_get_negotiated_width(uint32_t *width) {
  pcie_ctrl_link_status_t status;

  if (!width) {
    return -1;
  }

  if (pcie_ctrl_get_link_status(&status) != 0) {
    return -1;
  }

  if (!status.link_up) {
    return -1;
  }

  *width = status.negotiated_width;
  return 0;
}

/* ========================================================================== */
/*                          Helper Functions                                  */
/* ========================================================================== */

const char *pcie_ctrl_ltssm_to_string(uint32_t ltssm_state) {
  /* Delegate to existing helper function */
  return pcie_ltssm_state_to_string(ltssm_state);
}

const char *pcie_ctrl_speed_to_string(uint32_t speed) {
  /* Delegate to existing helper function */
  return pcie_speed_to_string(speed);
}
