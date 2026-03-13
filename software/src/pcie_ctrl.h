/*
 * PCIe Link Control API (P0)
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * High-level link control wrapper functions for PCIe bringup:
 * - Link enable and training
 * - Link status query
 * - Negotiated parameters
 */

#ifndef PCIE_CTRL_H
#define PCIE_CTRL_H

#include <stdint.h>

/* ========================================================================== */
/*                          Data Structures                                   */
/* ========================================================================== */

/**
 * @brief Link control status structure
 *
 * Contains negotiated link parameters and current state.
 * Returned by pcie_ctrl_get_link_status().
 */
typedef struct {
  uint32_t link_up;          /**< 1 if link is in L0 state, 0 otherwise */
  uint32_t ltssm_state;      /**< Current LTSSM state (0-31) */
  uint32_t negotiated_speed; /**< Negotiated link speed (1-6 for Gen1-Gen6) */
  uint32_t negotiated_width; /**< Negotiated link width (1, 2, 4, 8, 16) */
  uint32_t enumerated;       /**< 1 if enumerated by host, 0 otherwise */
  uint32_t flit_mode;        /**< 1 if in Flit Mode (Gen6), 0 otherwise */
  uint32_t training_errors;  /**< Count of training errors detected */
} pcie_ctrl_link_status_t;

/**
 * @brief Link enable result structure
 *
 * Contains result of pcie_ctrl_enable_link() operation.
 */
typedef struct {
  int32_t result;             /**< 0 on success, negative on error */
  uint32_t final_ltssm_state; /**< Final LTSSM state reached */
  uint32_t negotiated_speed;  /**< Negotiated speed (valid if result == 0) */
  uint32_t negotiated_width;  /**< Negotiated width (valid if result == 0) */
  uint32_t iterations;        /**< Number of poll iterations used */
} pcie_ctrl_enable_result_t;

/* ========================================================================== */
/*                          Link Control API                                  */
/* ========================================================================== */

/**
 * @brief Enable PCIe link and wait for L0 state
 * @param timeout_iterations Maximum number of polling iterations
 * @param result Pointer to result structure (can be NULL)
 * @return 0 on success (L0 reached), negative on error/timeout
 *
 * P0 Requirement: Link bring-up wrapper
 *
 * This function:
 * 1. Asserts LTSSM enable
 * 2. Polls for L0 state with configurable timeout
 * 3. Records negotiated Gen and width
 * 4. Returns detailed status in result structure
 *
 * Example:
 * @code
 *   pcie_ctrl_enable_result_t result;
 *   if (pcie_ctrl_enable_link(200, &result) == 0) {
 *       // Success - link is in L0
 *       printf("Link up: Gen%d x%d\n",
 *              result.negotiated_speed,
 *              result.negotiated_width);
 *   }
 * @endcode
 *
 * Error codes:
 * - 0: Success (L0 reached)
 * - -1: Timeout (link did not reach L0)
 * - -2: LTSSM enable failed
 * - -3: Link stuck in error state
 */
int pcie_ctrl_enable_link(uint32_t timeout_iterations,
                          pcie_ctrl_enable_result_t *result);

/**
 * @brief Get current link status
 * @param status Pointer to status structure to fill
 * @return 0 on success, negative on error
 *
 * P0 Requirement: Status query returning LTSSM state, speed/width, error flags
 *
 * This function retrieves:
 * - Current LTSSM state
 * - Negotiated link speed and width
 * - Enumeration status
 * - Link-level error flags
 * - Flit mode status
 *
 * Example:
 * @code
 *   pcie_ctrl_link_status_t status;
 *   if (pcie_ctrl_get_link_status(&status) == 0) {
 *       if (status.link_up) {
 *           printf("Link: Gen%d x%d\n",
 *                  status.negotiated_speed,
 *                  status.negotiated_width);
 *       }
 *   }
 * @endcode
 */
int pcie_ctrl_get_link_status(pcie_ctrl_link_status_t *status);

/**
 * @brief Disable LTSSM and bring link down
 * @return 0 on success
 *
 * Cleanly disables the LTSSM state machine and brings the link down.
 * Use this before link reset or reconfiguration.
 */
int pcie_ctrl_disable_link(void);

/**
 * @brief Check if link is currently in L0 state
 * @return 1 if in L0, 0 otherwise
 *
 * Quick check for link up state without full status query.
 * Convenience wrapper around pcie_ctrl_get_link_status().
 */
int pcie_ctrl_is_link_up(void);

/**
 * @brief Wait for link to reach L0 state
 * @param timeout_iterations Maximum number of polling iterations
 * @return 0 if L0 reached, negative on timeout
 *
 * Passive waiting function - does not enable LTSSM, just polls.
 * Use this if LTSSM is already enabled elsewhere.
 */
int pcie_ctrl_wait_for_l0(uint32_t timeout_iterations);

/**
 * @brief Get negotiated link speed
 * @param speed Pointer to store speed (1-6 for Gen1-Gen6)
 * @return 0 on success, negative if link is down
 *
 * Convenience function to query just the negotiated speed.
 */
int pcie_ctrl_get_negotiated_speed(uint32_t *speed);

/**
 * @brief Get negotiated link width
 * @param width Pointer to store width (1, 2, 4, 8, 16)
 * @return 0 on success, negative if link is down
 *
 * Convenience function to query just the negotiated width.
 */
int pcie_ctrl_get_negotiated_width(uint32_t *width);

/* ========================================================================== */
/*                          Helper Functions                                  */
/* ========================================================================== */

/**
 * @brief Get human-readable string for LTSSM state
 * @param ltssm_state LTSSM state value
 * @return String representation
 */
const char *pcie_ctrl_ltssm_to_string(uint32_t ltssm_state);

/**
 * @brief Get human-readable string for link speed
 * @param speed Speed value (1-6)
 * @return String representation ("Gen1" through "Gen6")
 */
const char *pcie_ctrl_speed_to_string(uint32_t speed);

#endif /* PCIE_CTRL_H */
