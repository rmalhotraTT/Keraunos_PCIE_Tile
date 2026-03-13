/*
 * PCIe Service Layer - State Machine (P0)
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * Non-blocking state machine for PCIe initialization:
 * RESET → SERDES_FW_LOAD → SERDES_READY → LINK_L0 → ENUM_READY
 */

#ifndef PCIE_SVC_H
#define PCIE_SVC_H

#include "pcie_init.h"
#include <stdint.h>

/* ========================================================================== */
/*                          State Machine States                              */
/* ========================================================================== */

/**
 * @brief PCIe initialization state machine states
 */
typedef enum {
  PCIE_SVC_STATE_RESET = 0,       /**< Initial state - reset asserted */
  PCIE_SVC_STATE_RESET_RELEASED,  /**< Resets released, clocks stable */
  PCIE_SVC_STATE_TLB_PROGRAMMED,  /**< TLBs configured */
  PCIE_SVC_STATE_DBI_CONFIGURED,  /**< DBI registers configured */
  PCIE_SVC_STATE_BARS_PROGRAMMED, /**< BARs programmed */
  PCIE_SVC_STATE_SERDES_FW_LOAD,  /**< Loading SerDes firmware */
  PCIE_SVC_STATE_SERDES_READY,    /**< SerDes firmware loaded, PHY ready */
  PCIE_SVC_STATE_LINK_TRAINING,   /**< LTSSM enabled, training in progress */
  PCIE_SVC_STATE_LINK_L0,         /**< Link reached L0 state */
  PCIE_SVC_STATE_ENUM_READY,      /**< Enumerated by host, fully operational */
  PCIE_SVC_STATE_ERROR,           /**< Error state */
  PCIE_SVC_STATE_MAX              /**< Sentinel value */
} pcie_svc_state_t;

/**
 * @brief State machine error codes
 */
typedef enum {
  PCIE_SVC_ERR_NONE = 0,           /**< No error */
  PCIE_SVC_ERR_RESET_FAILED,       /**< Reset release failed */
  PCIE_SVC_ERR_TLB_PROGRAM_FAILED, /**< TLB programming failed */
  PCIE_SVC_ERR_DBI_CONFIG_FAILED,  /**< DBI configuration failed */
  PCIE_SVC_ERR_BAR_PROGRAM_FAILED, /**< BAR programming failed */
  PCIE_SVC_ERR_FW_LOAD_FAILED,     /**< Firmware load failed */
  PCIE_SVC_ERR_PHY_INIT_FAILED,    /**< PHY initialization failed */
  PCIE_SVC_ERR_LINK_TIMEOUT,       /**< Link training timeout */
  PCIE_SVC_ERR_ENUM_TIMEOUT,       /**< Enumeration timeout */
  PCIE_SVC_ERR_INVALID_STATE,      /**< Invalid state transition */
} pcie_svc_error_t;

/**
 * @brief State machine context structure
 */
typedef struct pcie_svc_context {
  pcie_svc_state_t current_state;  /**< Current state */
  pcie_svc_state_t previous_state; /**< Previous state (for debugging) */
  pcie_svc_error_t last_error;     /**< Last error code */
  uint32_t state_entry_time;       /**< Time when current state was entered */
  uint32_t retry_count;            /**< Retry attempts for current state */
  uint32_t poll_count;             /**< Total poll iterations */

  /* Link status */
  uint32_t link_speed; /**< Negotiated link speed */
  uint32_t link_width; /**< Negotiated link width */
  uint32_t enumerated; /**< Enumeration status */

  /* Configuration */
  pcie_config_t *config; /**< Pointer to PCIe configuration */
} pcie_svc_context_t;

/**
 * @brief State transition callback function type
 *
 * Called when state machine transitions to a new state.
 * Can be used for logging, debugging, or custom actions.
 *
 * @param old_state Previous state
 * @param new_state New state
 * @param ctx Pointer to state machine context
 */
typedef void (*pcie_svc_state_callback_t)(pcie_svc_state_t old_state,
                                          pcie_svc_state_t new_state,
                                          pcie_svc_context_t *ctx);

/* ========================================================================== */
/*                          State Machine API                                 */
/* ========================================================================== */

/**
 * @brief Initialize the PCIe service state machine
 * @param ctx Pointer to state machine context
 * @param config Pointer to PCIe configuration
 * @return 0 on success, negative on error
 *
 * Sets up the state machine context and initializes to RESET state.
 * Must be called before pcie_svc_poll().
 */
int pcie_svc_init(pcie_svc_context_t *ctx, pcie_config_t *config);

/**
 * @brief Poll the state machine (non-blocking)
 * @param ctx Pointer to state machine context
 * @return Current state
 *
 * P0 Requirement: Non-blocking state machine
 *
 * This function should be called periodically from main loop.
 * It executes one state machine iteration and returns immediately.
 *
 * State transitions:
 * 1. RESET → RESET_RELEASED: Release resets, wait for stability
 * 2. RESET_RELEASED → TLB_PROGRAMMED: Program all TLBs
 * 3. TLB_PROGRAMMED → DBI_CONFIGURED: Configure DBI registers
 * 4. DBI_CONFIGURED → BARS_PROGRAMMED: Program BARs and ATU
 * 5. BARS_PROGRAMMED → SERDES_FW_LOAD: Start firmware loading
 * 6. SERDES_FW_LOAD → SERDES_READY: Firmware loaded successfully
 * 7. SERDES_READY → LINK_TRAINING: Enable LTSSM
 * 8. LINK_TRAINING → LINK_L0: Link reaches L0 state
 * 9. LINK_L0 → ENUM_READY: Device enumerated by host
 *
 * On error, transitions to ERROR state.
 *
 * Example usage:
 * @code
 *   pcie_svc_context_t ctx;
 *   pcie_config_t config;
 *
 *   pcie_init_config(&config);
 *   pcie_svc_init(&ctx, &config);
 *
 *   while (1) {
 *       pcie_svc_state_t state = pcie_svc_poll(&ctx);
 *
 *       if (state == PCIE_SVC_STATE_ENUM_READY) {
 *           // Initialization complete
 *           break;
 *       } else if (state == PCIE_SVC_STATE_ERROR) {
 *           // Handle error
 *           break;
 *       }
 *
 *       // Do other work...
 *   }
 * @endcode
 */
pcie_svc_state_t pcie_svc_poll(pcie_svc_context_t *ctx);

/**
 * @brief Register a state transition callback
 * @param callback Callback function pointer
 *
 * The callback will be invoked whenever the state machine transitions
 * to a new state. Only one callback can be registered at a time.
 * Pass NULL to unregister.
 */
void pcie_svc_register_callback(pcie_svc_state_callback_t callback);

/**
 * @brief Get current state of the state machine
 * @param ctx Pointer to state machine context
 * @return Current state
 */
pcie_svc_state_t pcie_svc_get_state(const pcie_svc_context_t *ctx);

/**
 * @brief Get last error code
 * @param ctx Pointer to state machine context
 * @return Last error code
 */
pcie_svc_error_t pcie_svc_get_last_error(const pcie_svc_context_t *ctx);

/**
 * @brief Reset the state machine to initial state
 * @param ctx Pointer to state machine context
 * @return 0 on success
 *
 * Resets the state machine back to RESET state.
 * Use this to reinitialize after an error or for testing.
 */
int pcie_svc_reset(pcie_svc_context_t *ctx);

/**
 * @brief Check if state machine has completed initialization
 * @param ctx Pointer to state machine context
 * @return 1 if in ENUM_READY state, 0 otherwise
 */
int pcie_svc_is_ready(const pcie_svc_context_t *ctx);

/**
 * @brief Check if state machine is in error state
 * @param ctx Pointer to state machine context
 * @return 1 if in ERROR state, 0 otherwise
 */
int pcie_svc_is_error(const pcie_svc_context_t *ctx);

/* ========================================================================== */
/*                          Helper Functions                                  */
/* ========================================================================== */

/**
 * @brief Get human-readable string for state
 * @param state State value
 * @return String representation
 */
const char *pcie_svc_state_to_string(pcie_svc_state_t state);

/**
 * @brief Get human-readable string for error code
 * @param error Error code
 * @return String representation
 */
const char *pcie_svc_error_to_string(pcie_svc_error_t error);

/**
 * @brief Get progress percentage
 * @param ctx Pointer to state machine context
 * @return Progress percentage (0-100)
 *
 * Returns approximate progress through initialization sequence.
 * Useful for displaying progress bars or status updates.
 */
uint32_t pcie_svc_get_progress(const pcie_svc_context_t *ctx);

#endif /* PCIE_SVC_H */
