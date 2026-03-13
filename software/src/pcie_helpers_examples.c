/*
 * PCIe Helpers Usage Examples
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * Example code showing how to use the pcie_helpers library
 */

#include "pcie_helpers.h"
#include "platform.h"
#include "regs.h"
#include "scratch.h"

/* Example 1: Simple link status check */
void example_check_link_status(void) {
  pcie_link_status_t status;

  /* Read current link status */
  if (pcie_read_link_status(&status) == 0) {
    /* Link is up and running at some speed/width */
    if (status.link_speed > 0 && status.link_width > 0) {
      /* Write status info to scratch registers for debugging */
      write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR,
                  status.ltssm_state);
      write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_11__REG_ADDR, status.link_speed);
      write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_12__REG_ADDR, status.link_width);
    }
  }
}

/* Example 2: Wait for link to come up */
int example_wait_for_link(void) {
  uint32_t timeout = 100000;
  uint32_t count = 0;

  while (count < timeout) {
    if (pcie_is_link_up()) {
      /* Link is in L0 state */
      return 0;
    }
    count++;
  }

  /* Timeout */
  return -1;
}

/* Example 3: Monitor LTSSM state changes */
void example_monitor_ltssm(void) {
  uint32_t prev_state = 0xFF; /* Invalid initial state */
  uint32_t current_state;

  for (int i = 0; i < 1000; i++) {
    current_state = pcie_read_ltssm_state();

    if (current_state != prev_state) {
      /* State changed - log it */
      write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR, current_state);
      prev_state = current_state;
    }

    /* Small delay */
    for (volatile int j = 0; j < 1000; j++)
      ;
  }
}

/* Example 4: Check if enumeration completed */
int example_check_enumeration(void) {
  if (pcie_is_enumerated()) {
    /* Host has enumerated the device */
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR, 0xE00DA550);
    return 1;
  } else {
    /* Not yet enumerated */
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR, 0xBADC0DE0);
    return 0;
  }
}

/* Example 5: Request speed change */
int example_change_speed_to_gen3(void) {
  int ret;

  /* Request Gen3 speed */
  ret = pcie_request_speed_change(PCIE_SPEED_GEN3);
  if (ret != 0) {
    return ret;
  }

  /* Wait for link to retrain */
  ret = example_wait_for_link();
  if (ret != 0) {
    return ret;
  }

  /* Verify we're at Gen3 */
  if (pcie_check_speed(PCIE_SPEED_GEN3)) {
    return 0; /* Success */
  } else {
    return -1; /* Speed change failed */
  }
}

/* Example 6: Handle hot reset detection */
int example_detect_hot_reset(void) {
  if (pcie_check_hot_reset()) {
    /* Hot reset detected - may need to reinitialize */
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR, 0xD0700E57);
    return 1;
  }
  return 0;
}

/* Example 7: Read and clear interrupts */
void example_handle_interrupts(void) {
  uint32_t int_status, misc_status;

  /* Read interrupt status */
  pcie_read_interrupt_status(&int_status, &misc_status);

  /* Check for LTSSM state change interrupt */
  if (misc_status & (1 << 3)) {
    /* LTSSM state changed */
    uint32_t new_state = pcie_read_ltssm_state();
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_13__REG_ADDR, new_state);
  }

  /* Clear interrupts */
  pcie_clear_interrupt_status(int_status, misc_status);
}

/* Example 8: Comprehensive diagnostic check */
void example_full_diagnostic(void) {
  pcie_link_status_t status;

  /* Get full status */
  if (pcie_read_link_status(&status) == 0) {
    /* Write detailed status to scratch registers */
    pcie_write_status_to_scratch(&status, 10);

    /* Additional checks */
    uint32_t bar0 = pcie_read_bar(0);
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_13__REG_ADDR, bar0);

    uint32_t cmd_reg = pcie_read_config_reg(0x04);
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_14__REG_ADDR, cmd_reg);
  }
}

/* Example 9: Polling loop with state monitoring */
void example_polling_loop(void) {
  uint32_t prev_state = 0xFF;

  while (1) {
    /* Check for state changes */
    if (pcie_monitor_state_change(&prev_state)) {
      /* State changed */
      write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR, prev_state);

      /* Check if we reached L0 */
      if (prev_state == LTSSM_L0) {
        write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_11__REG_ADDR, 0x10000000);
        break;
      }
    }

    /* WFI to save power */
    asm volatile("wfi");
  }
}

/* Example 10: Link recovery after reset */
int example_link_recovery(void) {
  /* Disable LTSSM */
  pcie_disable_ltssm();

  /* Small delay */
  for (volatile int i = 0; i < 10000; i++)
    ;

  /* Re-enable LTSSM (call from pcie_init.c) */
  pcie_enable_ltssm();

  /* Wait for link */
  return example_wait_for_link();
}
