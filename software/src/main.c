/*
 * PCIe Bringup Application - Minimal Test
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal PCIe bringup test for Keraunos SMC
 *
 * This file now uses the pcie_init library for all initialization.
 */

#include <stdio.h>
#include "basic_init.h"
#include "functional_tests.h"
#include "pcie_helpers.h"
#include "pcie_init.h"
#include "pcie_interrupt.h"
#include "pcie_svc.h"
#include "platform.h"
#include "plic.h"
#include "regs.h"
#include "scratch.h"

/* PCIe Controller Register Offsets - for local use */
#define PCIECTL_LINK_STATUS 0x0082
#define PCIECTL_LINK_STATUS2 0x00A2
#define PCIECTL_LINK_CONTROL2 0x00A0
#define PCIECTL_GEN2_CONTROL 0x080C
#define SMN_DBI_ADDR (0x18400000)

#ifndef BOOT_HART
#define BOOT_HART 0
#endif

/* PLIC Register Base Addresses */
#define PLIC_BASE (0xC4000000) // SMC_CPU_SMC_CLUSTER_PLIC_REG_MAP_BASE_ADDR
#define PLIC_PENDING_BASE (PLIC_BASE + 0x00001000)
#define PLIC_ENABLE_BASE (PLIC_BASE + 0x00002000)
#define PLIC_THRESHOLD_BASE (PLIC_BASE + 0x00200000)

/*
 * VDK scratch register helper: writes to SMC-local address space (0xC001xxxx).
 * The global addresses (0x0801xxxx) are unmapped in the VDK bus topology,
 * so we skip scratch writes entirely in the VP to avoid store access faults.
 */
#define VP_SCRATCH_WRITE(addr, val) /* disabled for VDK - address unmapped */

/**
 * Main entry point
 */
int main(void) {
  if (read_csr(mhartid) != BOOT_HART) {
    while (1) {
      asm volatile("wfi");
    }
  }

  pcie_config_t cfg;
  pcie_svc_context_t svc_ctx;
  pcie_link_status_t link_status;
  int ret;
  uint32_t iteration = 0;
  uint32_t last_progress = 0;
  uint32_t flr_check = 0;
  uint32_t flr_pending = 0;
  uint32_t link_status_reg = 0;

  /* Bare UART write — if '!' appears, the CPU is alive and UART works */
  *(volatile uint32_t *)0xC000A000 = (uint32_t)'!';
  *(volatile uint32_t *)0xC000A000 = (uint32_t)'\r';
  *(volatile uint32_t *)0xC000A000 = (uint32_t)'\n';

  printf("[PCIe FW] === PCIe Bringup Firmware Starting ===\n");
  printf("[PCIe FW] Hart ID: %lu\n", (unsigned long)read_csr(mhartid));

  printf("[PCIe FW] Disabling firewall filters...\n");
  grendel_err_t err = disable_ker_smc_filters();
  if (err != GRENDEL_ERR_OK) {
    printf("[PCIe FW] ERROR: firewall disable failed: %d\n", err);
    goto error;
  }
  printf("[PCIe FW] Firewall filters disabled OK\n");

  printf("[PCIe FW] Initializing PCIe config...\n");
  pcie_init_config(&cfg);

  printf("[PCIe FW] Starting PCIe state machine init...\n");
  ret = pcie_init_with_state_machine(&cfg, &svc_ctx);
  if (ret != 0) {
    printf("[PCIe FW] ERROR: state machine init failed: %d\n", ret);
    goto error;
  }
  printf("[PCIe FW] State machine initialized, polling...\n");
  printf("[PCIe FW] Initial state: %u (%s)\n",
         (unsigned)svc_ctx.current_state,
         pcie_svc_state_to_string(svc_ctx.current_state));

  while (1) {
    pcie_svc_state_t prev = svc_ctx.current_state;
    pcie_svc_state_t state = pcie_svc_poll(&svc_ctx);
    iteration++;

    if (state != prev) {
      printf("[PCIe FW] State %u(%s) -> %u(%s) at iter %u\n",
             (unsigned)prev, pcie_svc_state_to_string(prev),
             (unsigned)state, pcie_svc_state_to_string(state),
             iteration);
    }

    if ((iteration % 500) == 0) {
      printf("[PCIe FW] Iter %u  State: %u(%s)  Progress: %u%%\n",
             iteration, (unsigned)state,
             pcie_svc_state_to_string(state),
             pcie_svc_get_progress(&svc_ctx));
    }

    if (pcie_svc_is_ready(&svc_ctx)) {
      printf("[PCIe FW] SVC ready!\n");
      break;
    }

    if (pcie_svc_is_error(&svc_ctx)) {
      pcie_svc_error_t error = pcie_svc_get_last_error(&svc_ctx);
      printf("[PCIe FW] ERROR: SVC error %d (%s), state %d(%s)->%d(%s)\n",
             error, pcie_svc_error_to_string(error),
             svc_ctx.previous_state,
             pcie_svc_state_to_string(svc_ctx.previous_state),
             svc_ctx.current_state,
             pcie_svc_state_to_string(svc_ctx.current_state));
      goto error;
    }

    if (iteration > 10000) {
      printf("[PCIe FW] ERROR: timeout after %u iterations, state=%u(%s)\n",
             iteration, (unsigned)state,
             pcie_svc_state_to_string(state));
      goto error;
    }
  }

  printf("[PCIe FW] PCIe init complete! Setting up interrupts...\n");

  pcie_init_interrupts();

  flr_check = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_FLR_INT_REG_ADDR);
  flr_pending = (flr_check >> 8) & 0xFF;
  if (flr_pending) {
    printf("[PCIe FW] FLR pending: 0x%x, handling...\n", flr_check);
    write32_reg(PCIE_MGMT_MMR_KPCIE_SII_FLR_INT_REG_ADDR, flr_pending);
  }

  pcie_get_link_status(&link_status);

  link_status_reg = read32_reg(SMN_DBI_ADDR + PCIECTL_LINK_STATUS);

  printf("[PCIe FW] === PCIe Bringup PASSED ===\n");
  printf("[PCIe FW] Link Speed: %u, Width: %u, LTSSM: 0x%x\n",
         link_status.link_speed, link_status.link_width,
         link_status.ltssm_state);
  printf("[PCIe FW] Raw link status reg: 0x%08x\n", link_status_reg);

  while (1) {
    asm volatile("wfi");
  }

  return 0;

error:
  printf("[PCIe FW] === PCIe Bringup FAILED ===\n");
  while (1) {
    asm volatile("wfi");
  }
  return -1;
}
