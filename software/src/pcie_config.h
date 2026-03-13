/*
 * PCIe Configuration Constants (P1)
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * Centralized configuration constants for PCIe subsystem:
 * - Memory base addresses (tile addresses)
 * - TLB indices and configuration
 * - BAR layout and sizes
 * - Timeout values
 * - Default parameters
 */

#ifndef PCIE_CONFIG_H
#define PCIE_CONFIG_H

#include <stdint.h>

/* ========================================================================== */
/*                   VDK Memory Map (SMC_Configure CPU View)                  */
/* ========================================================================== */
/*
 * The SMC_Configure CPU in Keraunos_Chiplet has this address space:
 *
 *   0x00000000 - 0x01FFFFFF  Internal RAM (32MB, firmware code)
 *   0x18000000 - 0x187FFFFF  PCIE_TILE via smn_n_target (8MB, config regs)
 *   0x44000000 - 0x443FFFFF  PCIe_EP0 AXI_DBI (4MB, EP controller config)
 *   0x80000000 - 0x83FFFFFF  Internal DRAM (64MB, data storage)
 *   0xC0000000+              SMC-local registers (PLIC, scratch, UART, etc.)
 *
 * All addresses below are absolute (no translation by SharedMemoryMap).
 */

/* ========================================================================== */
/*                   SMN-IO Register Blocks (via smn_n_target)                */
/* ========================================================================== */
/*
 * These are the PCIe Tile's internal register blocks, accessed by the
 * SMC_Configure CPU through SharedMemoryMap → PCIE_TILE.smn_n_target.
 * VDK decode range: 0x18000000 - 0x187FFFFF (offset: none).
 *
 * From keraunos_pcie_tile.md Table 28 (SMN-IO Routing Table):
 */
#define PCIE_CFG_SMNIO_MSI_RELAY 0x18000000  /* MSI Relay Config (256KB) */
#define PCIE_CFG_SMNIO_TLB_CFG 0x18040000    /* TLB Config Registers (64KB) */
#define PCIE_CFG_SMNIO_CSR 0x18050000        /* SMN-IO Fabric CSR (64KB) */
#define PCIE_CFG_SMNIO_SYSOUT0 0x18400000    /* TLBSys0 Outbound (1MB) */

/* SII Config Block (at 0x18100000, 1MB) */
#define PCIE_CFG_SII_CONFIG_BASE 0x18100000  /* SII APB Demux base */

/* Access Control & Status (within TLB Config region) */
#define PCIE_CFG_ACCESS_CTRL_REG 0x1804FFF8  /* Outbound/Inbound enable */
#define PCIE_CFG_SYSTEM_READY_REG 0x1804FFFC /* System ready flag */

/* DBI access (through TLBSysOut0 outbound path) */
#define PCIE_CFG_SMN_DBI_ADDR 0x18400000     /* DBI base */
#define PCIE_CFG_SMN_DBIMASK_ADDR 0x18410000 /* DBI Mask */
#define PCIE_CFG_SMN_DBIATU_ADDR 0x18420000  /* DBI ATU */

/* PHY/SerDes Addresses */
#define PCIE_CFG_PHY_AHB_BASE 0x18080000    /* PHY AHB - firmware download (256KB) */
#define PCIE_CFG_PHY_APB_BASE 0x180C0000    /* PHY APB - control regs (256KB) */
#define PCIE_CFG_PHY_CPU_CFG_REG 0x180C341C /* PHY CPU Config register */

/* ========================================================================== */
/*                   TLB Target Addresses (Real SoC, NOT VDK-accessible)      */
/* ========================================================================== */
/*
 * These are real Grendel/Keraunos SoC addresses used as TLB *target* values.
 * The firmware writes them into TLB configuration registers (at 0x18040000)
 * to define where inbound PCIe traffic is routed in the real chip.
 *
 * In the VDK, these writes succeed (the TLB config registers accept them),
 * but the resulting data paths don't have actual targets connected (no
 * Tensix SRAM, no system DRAM, etc.). This is expected for initial bringup.
 */
#define PCIE_CFG_SYS_SRAM_BASE 0x0000010000000000ULL /* System SRAM (Tensix) */
#define PCIE_CFG_SYS_DRAM_BASE 0x0001000000000000ULL /* System DRAM */
#define PCIE_CFG_SMC_MAILBOX_BASE 0x0001202018000ULL /* SMC Mailbox */
#define PCIE_CFG_KERAUNOS_CONFIG_BASE 0x0001200000000ULL /* Keraunos Config */
#define PCIE_CFG_MIMIR_CCE_BASE 0x0001280000000ULL       /* Mimir CCE */
#define PCIE_CFG_MIMIR_CONFIG_BASE 0x0001300000000ULL    /* Mimir Config */

/* ========================================================================== */
/*                          TLB Configuration                                 */
/* ========================================================================== */

/* TLB Entry Size */
#define PCIE_CFG_TLB_ENTRY_SIZE 0x40 /* 64 bytes per entry */

/* TLB Indices for Key Mappings */
#define PCIE_TLB_SYSOUT0_DBI 0      /* DBI Base */
#define PCIE_TLB_SYSOUT0_DBI_MASK 1 /* DBI Mask */
#define PCIE_TLB_SYSOUT0_DBI_ATU 2  /* DBI ATU */
#define PCIE_TLB_SYSOUT0_DBI_DMA 3  /* DBI DMA */
#define PCIE_TLB_SYSOUT0_MEMRW 4    /* PCIe MEMRD/MEMWR */

#define PCIE_TLB_SYSIN0_MSI_RELAY 0     /* MSI Relay */
#define PCIE_TLB_SYSIN0_TLB_CFG_START 1 /* TLB Config entries 1-3 */
#define PCIE_TLB_SYSIN0_SII_CORE 4      /* SII Core Control */
#define PCIE_TLB_SYSIN0_DBI_START 5     /* DBI regions 5-8 */
#define PCIE_TLB_SYSIN0_DBI_DMA_START 9 /* DBI_DMA regions 9-12 */
#define PCIE_TLB_SYSIN0_SMC_MAILBOX 13  /* SMC Mailbox */

/* TLB Special Values */
#define PCIE_TLB_DBI_SPECIAL_VAL 0xFFFFFFFF /* Special DBI value */

/* ========================================================================== */
/*                          BAR Configuration                                 */
/* ========================================================================== */

/* BAR Sizes (Grendel Package) */
#define PCIE_BAR0_SIZE 0x0001000000000ULL /* 4GB - APP0 */
#define PCIE_BAR2_SIZE 0x0000000100000ULL /* 1MB - SYSIN0 */
#define PCIE_BAR4_SIZE 0x2000000000000ULL /* 512GB - APP1 */

/* BAR Base Addresses (initialized to 0, set by host during enumeration) */
#define PCIE_BAR0_BASE 0x0ULL
#define PCIE_BAR2_BASE 0x0ULL
#define PCIE_BAR4_BASE 0x0ULL

/* BAR Indices */
#define PCIE_BAR0_INDEX 0
#define PCIE_BAR2_INDEX 2
#define PCIE_BAR4_INDEX 4

/* BAR Attributes */
#define PCIE_BAR_TYPE_64BIT (1 << 2)   /* 64-bit BAR */
#define PCIE_BAR_PREFETCHABLE (1 << 3) /* Prefetchable */
#define PCIE_BAR_MEMORY_SPACE (0 << 0) /* Memory space (not I/O) */

/* ========================================================================== */
/*                          ATU Configuration                                 */
/* ========================================================================== */

/* ATU Entry Size */
#define PCIE_CFG_ATU_ENTRY_SIZE 0x200 /* 512 bytes per region */

/* ATU Inbound Region Indices */
#define PCIE_ATU_INBOUND_BAR0 0 /* BAR0 -> APP0 */
#define PCIE_ATU_INBOUND_BAR2 1 /* BAR2 -> SYSIN0 */
#define PCIE_ATU_INBOUND_BAR4 2 /* BAR4 -> APP1 */

/* NOC Channel IDs */
#define PCIE_NOC_CHANNEL_APP0 0    /* APP0 channel */
#define PCIE_NOC_CHANNEL_APP1 1    /* APP1 channel */
#define PCIE_NOC_CHANNEL_SYSIN0 14 /* SYSIN0 channel */

/* ========================================================================== */
/*                          Timing and Timeouts                               */
/* ========================================================================== */

/* Link Training Timeouts */
#define PCIE_LINK_TRAIN_TIMEOUT_MS 100   /* Link training timeout (ms) */
#define PCIE_LINK_TRAIN_TIMEOUT_ITER 200 /* Link training iterations */
#define PCIE_LINK_POLL_DELAY_CYCLES 100  /* Delay between polls (cycles) */

/* Firmware Loading Timeouts */
#define PCIE_FW_LOAD_TIMEOUT_MS 50    /* Firmware load timeout (ms) */
#define PCIE_PHY_INIT_DELAY_CYCLES 20 /* PHY init stabilization */

/* Reset Delays */
#define PCIE_RESET_DELAY_CYCLES 100      /* Reset stabilization delay */
#define PCIE_DBI_CONFIG_DELAY_CYCLES 100 /* DBI config delay */

/* Enumeration Timeout */
#define PCIE_ENUM_TIMEOUT_MS 500         /* Enumeration timeout (ms) */
#define PCIE_ENUM_POLL_DELAY_CYCLES 1000 /* Enumeration poll delay */

/* FLR Timeout */
#define PCIE_FLR_COMPLETION_TIMEOUT_MS 100 /* FLR completion timeout */
#define PCIE_FLR_POLL_INTERVAL_CYCLES 50   /* FLR polling interval */

/* ========================================================================== */
/*                          Default Link Parameters                           */
/* ========================================================================== */

/* Default Target Link Configuration */
#define PCIE_DEFAULT_MAX_SPEED 6  /* Gen6 (64 GT/s) */
#define PCIE_DEFAULT_MAX_WIDTH 16 /* x16 */
#define PCIE_DEFAULT_MPS 256      /* Max Payload Size: 256 bytes */
#define PCIE_DEFAULT_MRRS 256     /* Max Read Request Size: 256 bytes */

/* Supported Speeds */
#define PCIE_MIN_SPEED 1 /* Gen1 (2.5 GT/s) */
#define PCIE_MAX_SPEED 6 /* Gen6 (64 GT/s) */

/* Supported Widths */
#define PCIE_VALID_WIDTH_X1 1
#define PCIE_VALID_WIDTH_X2 2
#define PCIE_VALID_WIDTH_X4 4
#define PCIE_VALID_WIDTH_X8 8
#define PCIE_VALID_WIDTH_X16 16

/* ========================================================================== */
/*                          Vendor/Device Identification                      */
/* ========================================================================== */

#define PCIE_VENDOR_ID_TENSTORRENT 0x1E52 /* Tenstorrent vendor ID */
#define PCIE_DEVICE_ID_GRENDEL 0xFEED     /* Grendel device ID */

/* Class Codes */
#define PCIE_CLASS_CODE_PROCESSING 0x0B4000 /* Processing accelerator */
#define PCIE_REVISION_ID 0x00               /* Revision ID */

/* ========================================================================== */
/*                          PHY/SerDes Configuration                          */
/* ========================================================================== */

/* Firmware Memory Offsets (from PHY_AHB_BASE) */
#define PCIE_PHY_ICCM_OFFSET 0x10000 /* ICCM offset */
#define PCIE_PHY_DCCM_OFFSET 0x20000 /* DCCM offset */
#define PCIE_PHY_FW_MAX_SIZE 0x10000 /* 64KB firmware max */

/* Firmware SRAM Offsets (for future SRAM-based loading) */
#define PCIE_PHY_ICCM_SRAM_OFFSET 0x0000000000000000ULL /* ICCM in SRAM */
#define PCIE_PHY_DCCM_SRAM_OFFSET 0x0000000000010000ULL /* DCCM in SRAM */

/* Firmware Loading */
#define PCIE_FW_CHUNK_SIZE 0x800 /* 2KB chunks */
#define PCIE_FW_CHUNK_WORDS 512  /* 512 words per chunk */
#define PCIE_FW_NUM_CHUNKS 32    /* 32 chunks = 64KB */

/* ========================================================================== */
/*                          DBI Register Offsets                              */
/* ========================================================================== */

/* DBI Access Modifiers */
#define PCIE_DBI_OFFSET_MASK (0b010 << 19) /* DBI Mask offset */
#define PCIE_DBI_OFFSET_ATU (0b110 << 19)  /* DBI ATU offset */
#define PCIE_DBI_OFFSET_DMA (0b111 << 19)  /* DBI DMA offset */

/* ========================================================================== */
/*                          State Machine Configuration                       */
/* ========================================================================== */

/* State Machine Timing */
#define PCIE_SM_POLL_INTERVAL_MS 10 /* State machine poll interval */
#define PCIE_SM_MAX_RETRIES 3       /* Max retries per state */

/* ========================================================================== */
/*                          Feature Flags                                     */
/* ========================================================================== */

/* Firmware Control Flags */
#ifndef PCIE_CONTROL_MEM_BUS_MASTER
#define PCIE_CONTROL_MEM_BUS_MASTER 0 /* Firmware controls MEM/BUS_MASTER */
#endif

/* Debug Features */
#define PCIE_DEBUG_SCRATCH_LOGGING 1 /* Enable scratch register logging */
#define PCIE_DEBUG_VERBOSE 0         /* Verbose debug output */

#endif /* PCIE_CONFIG_H */
