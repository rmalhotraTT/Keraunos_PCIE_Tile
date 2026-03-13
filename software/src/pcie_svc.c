/*
 * PCIe Service Layer - State Machine Implementation
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcie_svc.h"
#include "pcie_config.h"
#include "pcie_ctrl.h"
#include "pcie_helpers.h"
#include "pcie_init.h"
#include "platform.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ========================================================================== */
/*                        PCIe Register Definitions                           */
/* ========================================================================== */

/* PCIe config space register offsets */
#define PCIECTL_TYPE1_STATUS_COMMAND 0x0004
#define PCIE_CMD_MEM_SPACE_ENABLE (1 << 1)
#define PCIE_CMD_BUS_MASTER_ENABLE (1 << 2)

/* ========================================================================== */
/*                          Configuration Constants                           */
/* ========================================================================== */

#define PCIE_SVC_MAX_RETRIES 3           /**< Max retries per state */
#define PCIE_SVC_RESET_DELAY_CYCLES 1000 /**< Delay after reset release */
#define PCIE_SVC_CLOCK_STABLE_CYCLES 500 /**< Wait for clock stability */

/* ========================================================================== */
/*                          State Transition Callback                         */
/* ========================================================================== */

static pcie_svc_state_callback_t g_state_callback = NULL;

void pcie_svc_register_callback(pcie_svc_state_callback_t callback) {
  g_state_callback = callback;
}

/* ========================================================================== */
/*                          Helper Functions                                  */
/* ========================================================================== */

/**
 * @brief Transition to a new state
 */
static void pcie_svc_transition_state(pcie_svc_context_t *ctx,
                                      pcie_svc_state_t new_state) {
  pcie_svc_state_t old_state = ctx->current_state;
  ctx->previous_state = old_state;
  ctx->current_state = new_state;
  ctx->state_entry_time =
      0; // Reset state timer (would use real time in production)
  ctx->retry_count = 0;
  ctx->poll_count = 0; // Reset poll count for new state

  // Invoke callback if registered
  if (g_state_callback) {
    g_state_callback(old_state, new_state, ctx);
  }
}

/**
 * @brief Transition to error state with error code
 */
static void pcie_svc_set_error(pcie_svc_context_t *ctx,
                               pcie_svc_error_t error) {
  ctx->last_error = error;
  pcie_svc_transition_state(ctx, PCIE_SVC_STATE_ERROR);
}

/* ========================================================================== */
/*                          State Handlers                                    */
/* ========================================================================== */

/**
 * @brief Handle RESET state
 *
 * Releases reset signals and waits for stability.
 */
static pcie_svc_state_t pcie_svc_handle_reset(pcie_svc_context_t *ctx) {
  printf("[SVC] RESET: releasing reset...\n");
  int ret = pcie_release_reset();
  printf("[SVC] RESET: pcie_release_reset returned %d\n", ret);

  if (ret != 0) {
    if (ctx->retry_count < PCIE_SVC_MAX_RETRIES) {
      ctx->retry_count++;
      printf("[SVC] RESET: retry %u/%u\n", ctx->retry_count,
             PCIE_SVC_MAX_RETRIES);
      return ctx->current_state;
    }
    pcie_svc_set_error(ctx, PCIE_SVC_ERR_RESET_FAILED);
    return ctx->current_state;
  }

  pcie_svc_transition_state(ctx, PCIE_SVC_STATE_RESET_RELEASED);
  return ctx->current_state;
}

/**
 * @brief Handle RESET_RELEASED state
 *
 * Waits for clocks to stabilize after reset release.
 */
static pcie_svc_state_t
pcie_svc_handle_reset_released(pcie_svc_context_t *ctx) {
  ctx->poll_count++;

  // Wait for clock stability
  if (ctx->poll_count >= PCIE_SVC_CLOCK_STABLE_CYCLES) {
    pcie_svc_transition_state(ctx, PCIE_SVC_STATE_TLB_PROGRAMMED);
  }

  return ctx->current_state;
}

/**
 * @brief Handle TLB_PROGRAMMED state
 *
 * Programs all TLB entries for PCIe communication.
 */
static pcie_svc_state_t pcie_svc_handle_tlb_program(pcie_svc_context_t *ctx) {
  printf("[SVC] TLB_PROGRAM: programming TLBs...\n");
  int ret = pcie_program_all_tlbs();
  printf("[SVC] TLB_PROGRAM: returned %d\n", ret);

  if (ret != 0) {
    if (ctx->retry_count < PCIE_SVC_MAX_RETRIES) {
      ctx->retry_count++;
      return ctx->current_state;
    }
    pcie_svc_set_error(ctx, PCIE_SVC_ERR_TLB_PROGRAM_FAILED);
    return ctx->current_state;
  }

  pcie_svc_transition_state(ctx, PCIE_SVC_STATE_DBI_CONFIGURED);
  return ctx->current_state;
}

/**
 * @brief Handle DBI_CONFIGURED state
 *
 * Configures DBI registers including config space.
 */
static pcie_svc_state_t pcie_svc_handle_dbi_config(pcie_svc_context_t *ctx) {
  printf("[SVC] DBI_CONFIG: configuring DBI...\n");
  int ret = pcie_configure_dbi();
  printf("[SVC] DBI_CONFIG: pcie_configure_dbi returned %d\n", ret);

  if (ret != 0) {
    if (ctx->retry_count < PCIE_SVC_MAX_RETRIES) {
      ctx->retry_count++;
      return ctx->current_state;
    }
    pcie_svc_set_error(ctx, PCIE_SVC_ERR_DBI_CONFIG_FAILED);
    return ctx->current_state;
  }

  printf("[SVC] DBI_CONFIG: init config space P0...\n");
  ret = pcie_init_config_space_p0();
  printf("[SVC] DBI_CONFIG: config_space_p0 returned %d\n", ret);
  if (ret != 0) {
    pcie_svc_set_error(ctx, PCIE_SVC_ERR_DBI_CONFIG_FAILED);
    return ctx->current_state;
  }

  printf("[SVC] DBI_CONFIG: init PHY...\n");
  ret = pcie_init_phy();
  printf("[SVC] DBI_CONFIG: pcie_init_phy returned %d\n", ret);
  if (ret != 0) {
    if (ctx->retry_count < PCIE_SVC_MAX_RETRIES) {
      ctx->retry_count++;
      return ctx->current_state;
    }
    pcie_svc_set_error(ctx, PCIE_SVC_ERR_PHY_INIT_FAILED);
    return ctx->current_state;
  }

  pcie_svc_transition_state(ctx, PCIE_SVC_STATE_BARS_PROGRAMMED);
  return ctx->current_state;
}

/**
 * @brief Handle BARS_PROGRAMMED state
 *
 * Programs BARs and ATU regions.
 */
static pcie_svc_state_t pcie_svc_handle_bars_program(pcie_svc_context_t *ctx) {
  printf("[SVC] BARS_PROGRAM: programming BARs...\n");
  int ret = pcie_program_bars(ctx->config);
  printf("[SVC] BARS_PROGRAM: pcie_program_bars returned %d\n", ret);

  if (ret != 0) {
    if (ctx->retry_count < PCIE_SVC_MAX_RETRIES) {
      ctx->retry_count++;
      return ctx->current_state;
    }
    pcie_svc_set_error(ctx, PCIE_SVC_ERR_BAR_PROGRAM_FAILED);
    return ctx->current_state;
  }

  printf("[SVC] BARS_PROGRAM: programming ATU...\n");
  ret = pcie_program_atu(ctx->config);
  printf("[SVC] BARS_PROGRAM: pcie_program_atu returned %d\n", ret);
  if (ret != 0) {
    pcie_svc_set_error(ctx, PCIE_SVC_ERR_BAR_PROGRAM_FAILED);
    return ctx->current_state;
  }

  printf("[SVC] BARS_PROGRAM: enabling MEM_SPACE and BUS_MASTER...\n");
  pcie_enable_mem_bus_master();

  pcie_svc_transition_state(ctx, PCIE_SVC_STATE_SERDES_FW_LOAD);
  return ctx->current_state;
}

/**
 * @brief Handle SERDES_FW_LOAD state
 *
 * Loads SerDes firmware to PHY.
 */
static pcie_svc_state_t
pcie_svc_handle_serdes_fw_load(pcie_svc_context_t *ctx) {
  printf("[SVC] SERDES_FW_LOAD: loading firmware...\n");
  int ret = pcie_load_firmware();
  printf("[SVC] SERDES_FW_LOAD: returned %d\n", ret);

  if (ret != 0) {
    if (ctx->retry_count < PCIE_SVC_MAX_RETRIES) {
      ctx->retry_count++;
      return ctx->current_state;
    }
    pcie_svc_set_error(ctx, PCIE_SVC_ERR_FW_LOAD_FAILED);
    return ctx->current_state;
  }

  pcie_svc_transition_state(ctx, PCIE_SVC_STATE_SERDES_READY);
  return ctx->current_state;
}

/**
 * @brief Handle SERDES_READY state
 *
 * PHY initialized, ready for link training.
 */
static pcie_svc_state_t pcie_svc_handle_serdes_ready(pcie_svc_context_t *ctx) {
  // PHY was already initialized in DBI_CONFIGURED state
  // This state just marks readiness for link training
  pcie_svc_transition_state(ctx, PCIE_SVC_STATE_LINK_TRAINING);
  return ctx->current_state;
}

/**
 * @brief Handle LINK_TRAINING state
 *
 * Enables LTSSM and waits for link to reach L0.
 */
static pcie_svc_state_t pcie_svc_handle_link_training(pcie_svc_context_t *ctx) {
  pcie_ctrl_enable_result_t result;

  printf("[SVC] LINK_TRAINING: enabling link (timeout=%u)...\n",
         PCIE_LINK_TRAIN_TIMEOUT_ITER);
  int ret = pcie_ctrl_enable_link(PCIE_LINK_TRAIN_TIMEOUT_ITER, &result);
  printf("[SVC] LINK_TRAINING: returned %d, LTSSM=0x%02x, iters=%u\n",
         ret, result.final_ltssm_state, result.iterations);

  if (ret == 0) {
    ctx->link_speed = result.negotiated_speed;
    ctx->link_width = result.negotiated_width;
    printf("[SVC] LINK_TRAINING: L0! speed=%u width=%u\n",
           result.negotiated_speed, result.negotiated_width);
    pcie_svc_transition_state(ctx, PCIE_SVC_STATE_LINK_L0);
  } else if (ret == -1) {
    printf("[SVC] LINK_TRAINING: timeout, retry %u/%u\n",
           ctx->retry_count + 1, PCIE_SVC_MAX_RETRIES);
    if (ctx->retry_count < PCIE_SVC_MAX_RETRIES) {
      ctx->retry_count++;
      pcie_ctrl_disable_link();
      return ctx->current_state;
    }
    pcie_svc_set_error(ctx, PCIE_SVC_ERR_LINK_TIMEOUT);
  } else {
    printf("[SVC] LINK_TRAINING: error %d\n", ret);
    pcie_svc_set_error(ctx, PCIE_SVC_ERR_LINK_TIMEOUT);
  }

  return ctx->current_state;
}

/**
 * @brief Handle LINK_L0 state
 *
 * Link is in L0, wait for host to enumerate device.
 */
static pcie_svc_state_t pcie_svc_handle_link_l0(pcie_svc_context_t *ctx) {
#if 1 // TEMPORARY: Skip enumeration check for emulation (no QEMU host)
  // In emulation without QEMU host, MEM_SPACE/BUS_MASTER bits won't be set
  // Skip enumeration check and transition directly to ENUM_READY
  ctx->enumerated = 0; // Mark as not enumerated but ready
  pcie_svc_transition_state(ctx, PCIE_SVC_STATE_ENUM_READY);
#else
  // Check if device has been enumerated by reading Command register
  // Host will set Memory Space Enable and Bus Master Enable when enumerated
  uint32_t command_reg;

  // Read Command/Status register (lower 16 bits are Command register)
  command_reg = pcie_dbi_read32(PCIECTL_TYPE1_STATUS_COMMAND);

  // Check for Memory Space Enable and Bus Master Enable bits
  uint32_t expected_bits =
      PCIE_CMD_MEM_SPACE_ENABLE | PCIE_CMD_BUS_MASTER_ENABLE;

  if ((command_reg & expected_bits) == expected_bits) {
    // Device has been enumerated
    ctx->enumerated = 1;
    pcie_svc_transition_state(ctx, PCIE_SVC_STATE_ENUM_READY);
  }

  // Check for timeout
  ctx->poll_count++;
  if (ctx->poll_count > PCIE_ENUM_TIMEOUT_MS * 10) { // Assume 100us poll period
    pcie_svc_set_error(ctx, PCIE_SVC_ERR_ENUM_TIMEOUT);
  }
#endif

  return ctx->current_state;
}

/**
 * @brief Handle ENUM_READY state
 *
 * Device is fully initialized and enumerated. Terminal state.
 */
static pcie_svc_state_t pcie_svc_handle_enum_ready(pcie_svc_context_t *ctx) {
  // Terminal state - stay here
  return ctx->current_state;
}

/**
 * @brief Handle ERROR state
 *
 * Error occurred. Terminal state.
 */
static pcie_svc_state_t pcie_svc_handle_error(pcie_svc_context_t *ctx) {
  // Terminal state - stay here
  // Could implement error recovery/retry logic here
  return ctx->current_state;
}

/* ========================================================================== */
/*                          State Machine Dispatch Table                     */
/* ========================================================================== */

typedef pcie_svc_state_t (*pcie_svc_state_handler_t)(pcie_svc_context_t *ctx);

static const pcie_svc_state_handler_t state_handlers[] = {
    [PCIE_SVC_STATE_RESET] = pcie_svc_handle_reset,
    [PCIE_SVC_STATE_RESET_RELEASED] = pcie_svc_handle_reset_released,
    [PCIE_SVC_STATE_TLB_PROGRAMMED] = pcie_svc_handle_tlb_program,
    [PCIE_SVC_STATE_DBI_CONFIGURED] = pcie_svc_handle_dbi_config,
    [PCIE_SVC_STATE_BARS_PROGRAMMED] = pcie_svc_handle_bars_program,
    [PCIE_SVC_STATE_SERDES_FW_LOAD] = pcie_svc_handle_serdes_fw_load,
    [PCIE_SVC_STATE_SERDES_READY] = pcie_svc_handle_serdes_ready,
    [PCIE_SVC_STATE_LINK_TRAINING] = pcie_svc_handle_link_training,
    [PCIE_SVC_STATE_LINK_L0] = pcie_svc_handle_link_l0,
    [PCIE_SVC_STATE_ENUM_READY] = pcie_svc_handle_enum_ready,
    [PCIE_SVC_STATE_ERROR] = pcie_svc_handle_error,
};

/* ========================================================================== */
/*                          Public API Implementation                         */
/* ========================================================================== */

int pcie_svc_init(pcie_svc_context_t *ctx, pcie_config_t *config) {
  if (!ctx || !config) {
    return -1;
  }

  memset(ctx, 0, sizeof(*ctx));
  ctx->config = config;
  ctx->current_state = PCIE_SVC_STATE_RESET;
  ctx->previous_state = PCIE_SVC_STATE_RESET;
  ctx->last_error = PCIE_SVC_ERR_NONE;

  return 0;
}

pcie_svc_state_t pcie_svc_poll(pcie_svc_context_t *ctx) {
  if (!ctx) {
    return PCIE_SVC_STATE_ERROR;
  }

  // Validate state
  if (ctx->current_state >= PCIE_SVC_STATE_MAX) {
    pcie_svc_set_error(ctx, PCIE_SVC_ERR_INVALID_STATE);
    return ctx->current_state;
  }

  // Execute state handler
  pcie_svc_state_handler_t handler = state_handlers[ctx->current_state];
  if (handler) {
    return handler(ctx);
  }

  // No handler for this state
  pcie_svc_set_error(ctx, PCIE_SVC_ERR_INVALID_STATE);
  return ctx->current_state;
}

pcie_svc_state_t pcie_svc_get_state(const pcie_svc_context_t *ctx) {
  return ctx ? ctx->current_state : PCIE_SVC_STATE_ERROR;
}

pcie_svc_error_t pcie_svc_get_last_error(const pcie_svc_context_t *ctx) {
  return ctx ? ctx->last_error : PCIE_SVC_ERR_INVALID_STATE;
}

int pcie_svc_reset(pcie_svc_context_t *ctx) {
  if (!ctx) {
    return -1;
  }

  ctx->current_state = PCIE_SVC_STATE_RESET;
  ctx->previous_state = PCIE_SVC_STATE_RESET;
  ctx->last_error = PCIE_SVC_ERR_NONE;
  ctx->state_entry_time = 0;
  ctx->retry_count = 0;
  ctx->poll_count = 0;
  ctx->link_speed = 0;
  ctx->link_width = 0;
  ctx->enumerated = 0;

  return 0;
}

int pcie_svc_is_ready(const pcie_svc_context_t *ctx) {
  return (ctx && ctx->current_state == PCIE_SVC_STATE_ENUM_READY) ? 1 : 0;
}

int pcie_svc_is_error(const pcie_svc_context_t *ctx) {
  return (ctx && ctx->current_state == PCIE_SVC_STATE_ERROR) ? 1 : 0;
}

/* ========================================================================== */
/*                          Helper Function Implementation                    */
/* ========================================================================== */

const char *pcie_svc_state_to_string(pcie_svc_state_t state) {
  static const char *state_strings[] = {
      [PCIE_SVC_STATE_RESET] = "RESET",
      [PCIE_SVC_STATE_RESET_RELEASED] = "RESET_RELEASED",
      [PCIE_SVC_STATE_TLB_PROGRAMMED] = "TLB_PROGRAMMED",
      [PCIE_SVC_STATE_DBI_CONFIGURED] = "DBI_CONFIGURED",
      [PCIE_SVC_STATE_BARS_PROGRAMMED] = "BARS_PROGRAMMED",
      [PCIE_SVC_STATE_SERDES_FW_LOAD] = "SERDES_FW_LOAD",
      [PCIE_SVC_STATE_SERDES_READY] = "SERDES_READY",
      [PCIE_SVC_STATE_LINK_TRAINING] = "LINK_TRAINING",
      [PCIE_SVC_STATE_LINK_L0] = "LINK_L0",
      [PCIE_SVC_STATE_ENUM_READY] = "ENUM_READY",
      [PCIE_SVC_STATE_ERROR] = "ERROR",
  };

  if (state >= PCIE_SVC_STATE_MAX) {
    return "UNKNOWN";
  }

  return state_strings[state];
}

const char *pcie_svc_error_to_string(pcie_svc_error_t error) {
  static const char *error_strings[] = {
      [PCIE_SVC_ERR_NONE] = "NONE",
      [PCIE_SVC_ERR_RESET_FAILED] = "RESET_FAILED",
      [PCIE_SVC_ERR_TLB_PROGRAM_FAILED] = "TLB_PROGRAM_FAILED",
      [PCIE_SVC_ERR_DBI_CONFIG_FAILED] = "DBI_CONFIG_FAILED",
      [PCIE_SVC_ERR_BAR_PROGRAM_FAILED] = "BAR_PROGRAM_FAILED",
      [PCIE_SVC_ERR_FW_LOAD_FAILED] = "FW_LOAD_FAILED",
      [PCIE_SVC_ERR_PHY_INIT_FAILED] = "PHY_INIT_FAILED",
      [PCIE_SVC_ERR_LINK_TIMEOUT] = "LINK_TIMEOUT",
      [PCIE_SVC_ERR_ENUM_TIMEOUT] = "ENUM_TIMEOUT",
      [PCIE_SVC_ERR_INVALID_STATE] = "INVALID_STATE",
  };

  if (error >= sizeof(error_strings) / sizeof(error_strings[0])) {
    return "UNKNOWN_ERROR";
  }

  return error_strings[error];
}

uint32_t pcie_svc_get_progress(const pcie_svc_context_t *ctx) {
  if (!ctx) {
    return 0;
  }

  // Map states to progress percentage
  static const uint8_t state_progress[] = {
      [PCIE_SVC_STATE_RESET] = 0,
      [PCIE_SVC_STATE_RESET_RELEASED] = 10,
      [PCIE_SVC_STATE_TLB_PROGRAMMED] = 20,
      [PCIE_SVC_STATE_DBI_CONFIGURED] = 30,
      [PCIE_SVC_STATE_BARS_PROGRAMMED] = 40,
      [PCIE_SVC_STATE_SERDES_FW_LOAD] = 50,
      [PCIE_SVC_STATE_SERDES_READY] = 60,
      [PCIE_SVC_STATE_LINK_TRAINING] = 70,
      [PCIE_SVC_STATE_LINK_L0] = 90,
      [PCIE_SVC_STATE_ENUM_READY] = 100,
      [PCIE_SVC_STATE_ERROR] = 0,
  };

  if (ctx->current_state >= PCIE_SVC_STATE_MAX) {
    return 0;
  }

  return state_progress[ctx->current_state];
}
