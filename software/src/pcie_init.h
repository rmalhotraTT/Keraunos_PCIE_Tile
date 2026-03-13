/*
 * PCIe Initialization Library Header
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * PCIe bringup and initialization functions for Keraunos SMC
 *
 * ============================================================================
 * CONFIGURATION OPTIONS
 * ============================================================================
 *
 * PCIE_CONTROL_MEM_BUS_MASTER (default: 0)
 * -----------------------------------------
 * Controls whether firmware manages MEM_SPACE and BUS_MASTER enable bits
 * in the PCIe Command register (offset 0x04, bits 1-2).
 *
 * When enabled (set to 1):
 *   - Firmware explicitly DISABLES MEM_SPACE/BUS_MASTER during init
 *   - Keeps them disabled until BARs and TLBs are fully configured
 *   - Re-enables them after configuration is complete
 *   - Prevents transactions during incomplete device setup
 *
 * When disabled (set to 0, default):
 *   - Firmware leaves Command register in hardware default state
 *   - Host has full control over MEM_SPACE/BUS_MASTER bits
 *   - Standard PCIe enumeration behavior
 *
 * Usage:
 *   Add to CMakeLists.txt:
 *     COMPILE_DEFINITIONS PCIE_CONTROL_MEM_BUS_MASTER=1
 *
 *   Or define before including this header:
 *     #define PCIE_CONTROL_MEM_BUS_MASTER 1
 *     #include "pcie_init.h"
 *
 * Debug markers (when enabled):
 *   SCRATCH_8 = 0xD15AB1E0 when disabled
 *   SCRATCH_8 = 0xE11AB1E0 when enabled
 *
 * ============================================================================
 */

#ifndef PCIE_INIT_H
#define PCIE_INIT_H

#include <stdint.h>

/* ========================================================================== */
/*                              Type Definitions                              */
/* ========================================================================== */

/**
 * @brief TLB Entry Structure
 */
typedef struct {
  uint8_t valid; /* Valid bit */
  uint64_t addr; /* Physical address */
  uint64_t attr; /* Attributes */
} tlb_entry_t;

/**
 * @brief BAR Configuration Structure
 */
typedef struct {
  uint64_t addr; /* BAR address */
  uint64_t size; /* BAR size */
} bar_config_t;

/**
 * @brief ATU Inbound Region Configuration
 */
typedef struct {
  uint8_t mode;         /* Mode (0=address, 1=BAR match) */
  uint8_t tlp_type;     /* TLP type */
  uint8_t bar;          /* BAR number */
  uint8_t func;         /* Function number */
  uint64_t target_addr; /* Target address */
} atu_inbound_t;

/**
 * @brief PCIe Configuration Context
 */
typedef struct {
  bar_config_t bars[3];          /* BAR configurations */
  atu_inbound_t atu_inbound[3];  /* ATU inbound regions */
  uint64_t last_tlbsys_addr[64]; /* Last programmed SYSIN0 TLB addresses */
} pcie_config_t;

/**
 * @brief PCIe Link Status Information
 */
typedef struct {
  uint32_t ltssm_state; /* LTSSM state */
  uint32_t link_speed;  /* Current link speed (Gen1-6) */
  uint32_t link_width;  /* Current link width (x1, x2, x4, etc.) */
  uint32_t enumerated;  /* 1 if enumerated by host, 0 otherwise */
  uint32_t flit_mode;   /* 1 if in Flit Mode (Gen6), 0 otherwise */
} pcie_link_status_t;

/* ========================================================================== */
/*                         Function Prototypes                                */
/* ========================================================================== */

/**
 * @brief Initialize PCIe configuration structure with default values
 * @param cfg PCIe configuration context to initialize
 */
void pcie_init_config(pcie_config_t *cfg);

/**
 * @brief Complete PCIe initialization sequence
 * @param cfg PCIe configuration context
 * @return 0 on success, negative on error
 *
 * This function performs the complete PCIe initialization sequence:
 * 1. Release resets (SMN, PCIe SII)
 * 2. Configure clock domains
 * 3. Load PHY firmware
 * 4. Program TLB entries
 * 5. Configure DBI registers
 * 6. Program BARs
 * 7. Configure ATU regions
 * 8. Enable LTSSM
 */
int pcie_full_init(pcie_config_t *cfg);

/**
 * @brief Release PCIe and SMN from reset
 * @return 0 on success, negative on error
 */
int pcie_release_reset(void);

/**
 * @brief Load PHY firmware from embedded arrays to SRAM and then to PHY
 * @return 0 on success, negative on error
 */
int pcie_load_firmware(void);

/**
 * @brief Program all TLB entries (SYSOUT0, SYSIN0, APPIN0/1, APPOUT0)
 * @return 0 on success, negative on error
 */
int pcie_program_all_tlbs(void);

/**
 * @brief Program PCIe DBI configuration registers
 * @return 0 on success, negative on error
 */
int pcie_configure_dbi(void);

/**
 * @brief Initialize PHY and release from reset
 * @return 0 on success, negative on error
 */
int pcie_init_phy(void);

/**
 * @brief Program BAR registers
 * @param cfg PCIe configuration context
 * @return 0 on success, negative on error
 */
int pcie_program_bars(pcie_config_t *cfg);

/**
 * @brief Program all ATU inbound regions
 * @param cfg PCIe configuration context
 * @return 0 on success, negative on error
 */
int pcie_program_atu(pcie_config_t *cfg);

/**
 * @brief Enable LTSSM and start link training
 * @return 0 on success, negative on error
 */
int pcie_enable_ltssm(void);

/**
 * @brief Wait for link to reach L0 state
 * @param timeout_iterations Maximum iterations to wait
 * @return 0 on success (L0 reached), negative on timeout
 */
int pcie_wait_for_link_up(uint32_t timeout_iterations);

/**
 * @brief Get current PCIe link status
 * @param status Pointer to status structure to fill
 * @return 0 on success, negative on error
 */
int pcie_get_link_status(pcie_link_status_t *status);

/**
 * @brief Check if device has been enumerated by host
 * @return 1 if enumerated, 0 if not
 *
 * Checks for SOFTWARE enumeration completion by verifying:
 *   - Memory Space Enable bit is set in Command register
 *   - BAR0 has been programmed with a valid address
 *
 * Note: This is stricter than just checking if link is in L0 state.
 * The link can reach L0 before the host OS/BIOS completes enumeration
 * and resource assignment. This function returns 1 only after the host
 * has fully configured the device.
 *
 * In emulation, there may be a delay between link L0 and this returning 1
 * while the host software enumerates the bus and assigns resources.
 */
int pcie_check_enumeration(void);

/**
 * @brief Translate SMN address to DBI address using TLB
 * @param smn_addr SMN address to translate
 * @param valid_out Pointer to valid flag (set to 1 if translation valid)
 * @return Translated address, or 0 if invalid
 */
uint64_t pcie_translate_smn_to_dbi(uint64_t smn_addr, uint8_t *valid_out);

/* ========================================================================== */
/*                    Individual TLB Programming Functions                    */
/* ========================================================================== */

/**
 * @brief Program SYSOUT0 TLB entries
 * @return 0 on success, negative on error
 */
int pcie_program_tlb_sysout0(void);

/**
 * @brief Program SYSIN0 TLB entries
 * @return 0 on success, negative on error
 */
int pcie_program_tlb_sysin0(void);

/**
 * @brief Program APPIN0 TLB entries
 * @return 0 on success, negative on error
 */
int pcie_program_tlb_appin0(void);

/**
 * @brief Program APPIN1 TLB entries
 * @return 0 on success, negative on error
 */
int pcie_program_tlb_appin1(void);

/**
 * @brief Program APPOUT0 TLB entries
 * @return 0 on success, negative on error
 */
int pcie_program_tlb_appout0(void);

/* ========================================================================== */
/*                         Low-Level Helper Functions                         */
/* ========================================================================== */

/**
 * @brief Read from DesignWare DBI (Device Bus Interface) config space
 * @param offset Register offset from DBI base (0x18400000)
 * @return 32-bit register value
 */
uint32_t pcie_dbi_read32(uint32_t offset);

/**
 * @brief Initialize P0 (Phase 0) PCIe config space
 * @return 0 on success, negative on error
 *
 * Verifies device ID, configures command register, capabilities,
 * and device control settings for P0 initialization.
 */
int pcie_init_config_space_p0(void);

/**
 * @brief Enable Memory Space and Bus Master in PCIe Command register
 *
 * Only active when PCIE_CONTROL_MEM_BUS_MASTER compile option is enabled.
 * Should be called after BARs and TLBs are fully configured.
 */
void pcie_enable_mem_bus_master(void);

/* ========================================================================== */
/*              State Machine-Based Initialization (P0/P1)                    */
/* ========================================================================== */

/* Forward declaration - full definition in pcie_svc.h */
struct pcie_svc_context;
typedef struct pcie_svc_context pcie_svc_context_t;

/**
 * @brief Initialize PCIe using non-blocking state machine
 * @param cfg Pointer to PCIe configuration
 * @param ctx Pointer to state machine context (NULL for blocking mode)
 * @return 0 on success, negative on error
 *
 * P0/P1 Recommended: Non-blocking state machine initialization.
 *
 * USAGE MODE 1 - Blocking (run to completion):
 * @code
 *   pcie_config_t config;
 *   pcie_init_config(&config);
 *
 *   // NULL context = blocking mode
 *   int ret = pcie_init_with_state_machine(&config, NULL);
 *   if (ret == 0) {
 *       // PCIe fully initialized
 *   }
 * @endcode
 *
 * USAGE MODE 2 - Non-blocking (caller polls):
 * @code
 *   #include "pcie_svc.h"  // For pcie_svc_context_t and polling functions
 *
 *   pcie_config_t config;
 *   pcie_svc_context_t ctx;
 *
 *   pcie_init_config(&config);
 *
 *   // Initialize state machine
 *   pcie_init_with_state_machine(&config, &ctx);
 *
 *   // Poll from main loop
 *   while (1) {
 *       pcie_svc_state_t state = pcie_svc_poll(&ctx);
 *
 *       if (pcie_svc_is_ready(&ctx)) {
 *           // Initialization complete
 *           break;
 *       } else if (pcie_svc_is_error(&ctx)) {
 *           // Handle error
 *           pcie_svc_error_t err = pcie_svc_get_last_error(&ctx);
 *           break;
 *       }
 *
 *       // Do other work while PCIe initializes...
 *   }
 * @endcode
 *
 * State Machine Sequence:
 * RESET → RESET_RELEASED → TLB_PROGRAMMED → DBI_CONFIGURED
 * → BARS_PROGRAMMED → SERDES_FW_LOAD → SERDES_READY
 * → LINK_TRAINING → LINK_L0 → ENUM_READY
 *
 * @note If ctx is NULL, runs in blocking mode until completion/error
 * @note If ctx is provided, initializes state machine and returns immediately.
 *       Caller must poll pcie_svc_poll(ctx) to advance state machine.
 * @note For non-blocking mode, include "pcie_svc.h" for full context definition
 */
int pcie_init_with_state_machine(pcie_config_t *cfg, pcie_svc_context_t *ctx);

#endif /* PCIE_INIT_H */
