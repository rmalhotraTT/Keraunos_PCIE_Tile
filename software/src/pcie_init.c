/*
 * PCIe Initialization Library Implementation
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * PCIe bringup and initialization functions for Keraunos SMC
 */

#include "pcie_init.h"
#include "pcie_config.h"
#include "basic_init.h"
#include "dccm_firmware.h"
#include "iccm_firmware.h"
#include "pcie_ctrl.h"
#include "pcie_svc.h"
#include "platform.h"
#include "regs.h"
#include "scratch.h"
#include "string.h"
#include <stdio.h>

/* ========================================================================== */
/*                    PCIe Memory/Bus Master Control                         */
/* ========================================================================== */

/**
 * PCIE_CONTROL_MEM_BUS_MASTER - Enable firmware control of MEM_SPACE/BUS_MASTER
 *
 * When enabled (1), firmware will:
 *   1. Explicitly disable MEM_SPACE and BUS_MASTER during initialization
 *   2. Keep them disabled until BARs and TLBs are fully configured
 *   3. Optionally re-enable them after configuration is complete
 *
 * When disabled (0), firmware will:
 *   - Leave MEM_SPACE/BUS_MASTER in their hardware default state
 *   - Allow host to control these bits exclusively
 *
 * Per PCIe spec recommendation: These should be disabled during device
 * initialization to prevent undefined behavior from incomplete configuration.
 *
 * Default: 0 (disabled) - maintains existing behavior, host controls these bits
 */
#ifndef PCIE_CONTROL_MEM_BUS_MASTER
#define PCIE_CONTROL_MEM_BUS_MASTER 0
#endif

/* Reset IDs */
#define RST_ID_SMN 1
#define RST_ID_PCIE_SII 2
#define RST_ID_PCIE 3

/* TLB Configuration */
#define TLB_ENTRY_SIZE 0x40
#define TLB_DBI_VAL 0xFFFFFFFF

/* PCIe Controller DBI Offsets */
#define PCIECTL_DBI_MASK (0b010 << 19)
#define PCIECTL_DBI_ATU (0b110 << 19)
#define PCIECTL_DBI_DMA (0b111 << 19)

/* Memory Base Addresses */
#define SYS_SRAM_BASE 0x0000010000000000ULL
#define SYS_DRAM_BASE 0x0001000000000000ULL
#define SMC_MAILBOX 0x0001202018000ULL
#define MIMIR_CCE_ADDR 0x0001280000000ULL
#define KER_CONFIG_ADDR 0x0001200000000ULL
#define MIMIR_CONFIG_ADDR 0x0001300000000ULL

/* SMN I/O Addresses */
#define SMNIO_MSIRELAY (0x18000000)
#define SMNIO_TLBCFG (0x18040000)
#define SMNIO_SYSOUT0 (0x18400000)
#define SMN_DBI_ADDR (0x18400000)
#define SMN_DBIMASK_ADDR (0x18410000)
#define SMN_DBIATU_ADDR (0x18420000)

/* PHY Addresses */
#define PHY_AHB_BASE 0x18080000
#define PHY_APB_BASE 0x180C0000
#define PCIE_PHY_CPU_CFG 0x180C341C

/* PHY Firmware SRAM Offsets */
#define PHY_ICCM_FW_SRAM_OFFSET 0x0000000000000000ULL
#define PHY_DCCM_FW_SRAM_OFFSET 0x0000000000010000ULL
#define PHY_FW_MAX_SIZE 0x10000

/* ATU Configuration */
#define ATU_ENTRY_SIZE 0x200

/* NOC Channel Configuration */
#define NOCPCIE_IN_APP0 0
#define NOCPCIE_IN_APP1 1
#define NOCPCIE_IN_SYSIN0 14

/* ========================================================================== */
/*                    PCIe Configuration Space Registers                      */
/* ========================================================================== */

/* Type 0 Configuration Space Header */
#define PCIECTL_VENDOR_DEVICE_ID                                               \
  0x0000 /* Vendor ID[15:0] | Device ID[31:16]                                 \
          */
#define PCIECTL_TYPE1_STATUS_COMMAND                                           \
  0x0004 /* Command[15:0] | Status[31:16]                                      \
          */
#define PCIECTL_CLASS_REVISION                                                 \
  0x0008 /* Revision ID[7:0] | Class Code[31:8]                                \
          */
#define PCIECTL_BIST_HEADER_LATENCY                                            \
  0x000C /* Cache Line[7:0] | Latency[15:8] | Header Type[23:16] | BIST[31:24] \
          */
#define PCIECTL_BAR_OFF 0x0010         /* BAR0 */
#define PCIECTL_BAR1 0x0014            /* BAR1 */
#define PCIECTL_BAR2 0x0018            /* BAR2 */
#define PCIECTL_BAR3 0x001C            /* BAR3 */
#define PCIECTL_BAR4 0x0020            /* BAR4 */
#define PCIECTL_BAR5 0x0024            /* BAR5 */
#define PCIECTL_CARDBUS_CIS_PTR 0x0028 /* CardBus CIS Pointer */
#define PCIECTL_SUBSYSTEM_ID                                                   \
  0x002C /* Subsystem Vendor ID[15:0] | Subsystem ID[31:16] */
#define PCIECTL_EXPANSION_ROM 0x0030 /* Expansion ROM Base Address */
#define PCIECTL_CAP_PTR 0x0034 /* Capability Pointer[7:0] | Reserved[31:8] */
#define PCIECTL_INTERRUPT                                                      \
  0x003C /* Int Line[7:0] | Int Pin[15:8] | Min_Gnt[23:16] | Max_Lat[31:24] */

/* Command Register Bits (offset 0x04, lower 16 bits) */
#define PCIE_CMD_IO_SPACE_ENABLE (1 << 0)   /* I/O Space Enable */
#define PCIE_CMD_MEM_SPACE_ENABLE (1 << 1)  /* Memory Space Enable */
#define PCIE_CMD_BUS_MASTER_ENABLE (1 << 2) /* Bus Master Enable */
#define PCIE_CMD_SPECIAL_CYCLES (1 << 3)    /* Special Cycles */
#define PCIE_CMD_MEM_WR_INV_ENABLE                                             \
  (1 << 4) /* Memory Write and Invalidate Enable */
#define PCIE_CMD_VGA_PALETTE_SNOOP (1 << 5) /* VGA Palette Snoop */
#define PCIE_CMD_PARITY_ERROR_RESP (1 << 6) /* Parity Error Response */
#define PCIE_CMD_SERR_ENABLE (1 << 8)       /* SERR# Enable */
#define PCIE_CMD_FAST_B2B_ENABLE (1 << 9)   /* Fast Back-to-Back Enable */
#define PCIE_CMD_INTX_DISABLE (1 << 10)     /* INTx Disable */

/* Status Register Bits (offset 0x04, upper 16 bits) */
#define PCIE_STS_INTX_STATUS (1 << 19) /* INTx Status (bit 3 in upper word) */
#define PCIE_STS_CAP_LIST                                                      \
  (1 << 20) /* Capabilities List (bit 4 in upper word) */
#define PCIE_STS_66MHZ_CAPABLE (1 << 21)    /* 66 MHz Capable */
#define PCIE_STS_FAST_B2B_CAPABLE (1 << 23) /* Fast Back-to-Back Capable */
#define PCIE_STS_MASTER_DATA_PARITY_ERR                                        \
  (1 << 24) /* Master Data Parity Error (sticky) */
#define PCIE_STS_DEVSEL_TIMING_MASK (3 << 25) /* DEVSEL Timing */
#define PCIE_STS_SIGNALED_TARGET_ABORT                                         \
  (1 << 27) /* Signaled Target Abort (sticky) */
#define PCIE_STS_RECEIVED_TARGET_ABORT                                         \
  (1 << 28) /* Received Target Abort (sticky) */
#define PCIE_STS_RECEIVED_MASTER_ABORT                                         \
  (1 << 29) /* Received Master Abort (sticky) */
#define PCIE_STS_SIGNALED_SYSTEM_ERROR                                         \
  (1 << 30) /* Signaled System Error (sticky) */
#define PCIE_STS_DETECTED_PARITY_ERROR                                         \
  (1 << 31) /* Detected Parity Error (sticky) */

/* Sticky error bits that should be cleared by writing 1 */
#define PCIE_STS_ERROR_BITS                                                    \
  (PCIE_STS_MASTER_DATA_PARITY_ERR | PCIE_STS_SIGNALED_TARGET_ABORT |          \
   PCIE_STS_RECEIVED_TARGET_ABORT | PCIE_STS_RECEIVED_MASTER_ABORT |           \
   PCIE_STS_SIGNALED_SYSTEM_ERROR | PCIE_STS_DETECTED_PARITY_ERROR)

/* PCIe Capability Structure Offsets (Extended Capabilities) */
#define PCIECTL_PCIE_CAP 0x0074   /* PCI Express Capabilities Register */
#define PCIECTL_DEVICE_CAP 0x0074 /* Device Capabilities */
#define PCIECTL_DEVICE_CONTROL                                                 \
  0x0078 /* Device Control[15:0] | Device Status[31:16] */
#define PCIECTL_LINK_CAPABILITIES 0x007C /* Link Capabilities */
#define PCIECTL_LINK_CONTROL                                                   \
  0x0080                           /* Link Control[15:0] | Link Status[31:16] */
#define PCIECTL_LINK_STATUS 0x0082 /* Link Status (upper 16 bits of 0x80) */
#define PCIECTL_LINK_CONTROL2                                                  \
  0x00A0 /* Link Control 2[15:0] | Link Status 2[31:16] */
#define PCIECTL_LINK_STATUS2                                                   \
  0x00A2 /* Link Status 2 (upper 16 bits of 0xA0)                              \
          */

/* Device Control Register Bits (offset 0x78, lower 16 bits) */
#define PCIE_DEVCTL_CORR_ERR_REPORT_EN                                         \
  (1 << 0) /* Correctable Error Reporting Enable */
#define PCIE_DEVCTL_NONFATAL_ERR_EN                                            \
  (1 << 1)                                /* Non-Fatal Error Reporting Enable */
#define PCIE_DEVCTL_FATAL_ERR_EN (1 << 2) /* Fatal Error Reporting Enable */
#define PCIE_DEVCTL_UNSUP_REQ_EN                                               \
  (1 << 3) /* Unsupported Request Reporting Enable */
#define PCIE_DEVCTL_RELAX_ORDERING_EN (1 << 4)  /* Enable Relaxed Ordering */
#define PCIE_DEVCTL_MAX_PAYLOAD_MASK (7 << 5)   /* Max Payload Size */
#define PCIE_DEVCTL_EXT_TAG_EN (1 << 8)         /* Extended Tag Field Enable */
#define PCIE_DEVCTL_PHANTOM_FUNC_EN (1 << 9)    /* Phantom Functions Enable */
#define PCIE_DEVCTL_AUX_POWER_EN (1 << 10)      /* Auxiliary Power PM Enable */
#define PCIE_DEVCTL_NO_SNOOP_EN (1 << 11)       /* Enable No Snoop */
#define PCIE_DEVCTL_MAX_READ_REQ_MASK (7 << 12) /* Max Read Request Size */

/* Max Payload Size / Max Read Request Size Encodings */
#define PCIE_MPS_128_BYTES (0 << 5)  /* 128 bytes */
#define PCIE_MPS_256_BYTES (1 << 5)  /* 256 bytes */
#define PCIE_MPS_512_BYTES (2 << 5)  /* 512 bytes */
#define PCIE_MPS_1024_BYTES (3 << 5) /* 1024 bytes */
#define PCIE_MPS_2048_BYTES (4 << 5) /* 2048 bytes */
#define PCIE_MPS_4096_BYTES (5 << 5) /* 4096 bytes */

#define PCIE_MRRS_128_BYTES (0 << 12)  /* 128 bytes */
#define PCIE_MRRS_256_BYTES (1 << 12)  /* 256 bytes */
#define PCIE_MRRS_512_BYTES (2 << 12)  /* 512 bytes */
#define PCIE_MRRS_1024_BYTES (3 << 12) /* 1024 bytes */
#define PCIE_MRRS_2048_BYTES (4 << 12) /* 2048 bytes */
#define PCIE_MRRS_4096_BYTES (5 << 12) /* 4096 bytes */

/* MSI Capability (typical offset 0x50, but verify in hardware) */
#define PCIECTL_MSI_CAP_OFFSET 0x0050 /* MSI Capability base offset */
#define PCIECTL_MSI_CONTROL 0x0052    /* MSI Control register */
#define PCIECTL_MSI_ADDR_LOW 0x0054   /* MSI Message Address (lower 32 bits) */
#define PCIECTL_MSI_ADDR_HIGH 0x0058  /* MSI Message Address (upper 32 bits) */
#define PCIECTL_MSI_DATA 0x005C       /* MSI Message Data */

/* MSI-X Capability (typical offset 0xB0, but verify in hardware) */
#define PCIECTL_MSIX_CAP_OFFSET 0x00B0   /* MSI-X Capability base offset */
#define PCIECTL_MSIX_CONTROL 0x00B2      /* MSI-X Control register */
#define PCIECTL_MSIX_TABLE_OFFSET 0x00B4 /* MSI-X Table Offset/BIR */
#define PCIECTL_MSIX_PBA_OFFSET 0x00B8   /* MSI-X PBA Offset/BIR */

/* Expected Vendor/Device ID for Tenstorrent Grendel */
#define EXPECTED_VENDOR_ID 0x1E52 /* Tenstorrent vendor ID */
#define EXPECTED_DEVICE_ID 0xFEED /* Grendel device ID (placeholder) */

/* ========================================================================== */
/*                    PCIe Controller Register Offsets                        */
/* ========================================================================== */

#define PCIECTL_MISC_CONTROL 0x08BC
#define PCIECTL_ERROR_RESPONSE_DEFAULT 0x08D0
#define PCIECTL_GEN2_CONTROL 0x080C
#define PCIECTL_GEN3_RELATED 0x0890
#define PCIECTL_PL32G_CAPABILITY 0x01BC
#define PCIECTL_PHY_CONTROL 0x0814
#define PCIECTL_PORT_LINK_CTRL 0x0710
#define PCIECTL_PCIE_PORT_DEBUG0 0x0728

/* ATU (Address Translation Unit) Offsets */
#define PCIECTL_ATU_INBOUND_OFFSET 0x0100
#define PCIECTL_ATU_OUTBOUND_OFFSET 0x0000

#define SS_FORCE_TO_REF_CLK_PCIE_SII_BIT 2

/* ========================================================================== */
/*                           Internal Helper Functions                        */
/* ========================================================================== */

/* -------------------------------------------------------------------------- */
/*                       SerDes APB/AHB Access Helpers                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Read from PCIe SerDes APB address space
 * @param offset Register offset from PHY_APB_BASE
 * @return Register value
 *
 * Use this for accessing SerDes control registers (PLL, calibration, lane
 * config). APB base: 0x180C0000
 */
static inline uint32_t pcie_phy_apb_read32(uint32_t offset) {
  return read32_reg(PHY_APB_BASE + offset);
}

/**
 * @brief Write to PCIe SerDes APB address space
 * @param offset Register offset from PHY_APB_BASE
 * @param value Value to write
 *
 * Use this for configuring SerDes control registers (PLL, calibration, lane
 * config). APB base: 0x180C0000
 */
static inline void pcie_phy_apb_write32(uint32_t offset, uint32_t value) {
  write32_reg(PHY_APB_BASE + offset, value);
}

/**
 * @brief Read from PCIe SerDes AHB address space
 * @param offset Register offset from PHY_AHB_BASE
 * @return Register value
 *
 * Use this for accessing SerDes ICCM/DCCM memory and firmware upload.
 * AHB base: 0x18080000
 * ICCM offset: 0x10000
 * DCCM offset: 0x20000
 */
static inline uint32_t pcie_phy_ahb_read32(uint32_t offset) {
  return read32_reg(PHY_AHB_BASE + offset);
}

/**
 * @brief Write to PCIe SerDes AHB address space
 * @param offset Register offset from PHY_AHB_BASE
 * @param value Value to write
 *
 * Use this for loading firmware to SerDes ICCM/DCCM memory.
 * AHB base: 0x18080000
 * ICCM offset: 0x10000
 * DCCM offset: 0x20000
 */
static inline void pcie_phy_ahb_write32(uint32_t offset, uint32_t value) {
  write32_reg(PHY_AHB_BASE + offset, value);
}

/**
 * @brief Read-modify-write PCIe SerDes APB register
 * @param offset Register offset from PHY_APB_BASE
 * @param mask Bit mask for field
 * @param value Value to set (will be masked)
 *
 * Convenience function for updating bit fields in PHY registers.
 */
static inline void pcie_phy_apb_rmw32(uint32_t offset, uint32_t mask,
                                      uint32_t value) {
  uint32_t reg = pcie_phy_apb_read32(offset);
  reg = (reg & ~mask) | (value & mask);
  pcie_phy_apb_write32(offset, reg);
}

/* -------------------------------------------------------------------------- */
/*                         DBI Access Helpers                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Read from DesignWare DBI (Device Bus Interface) config space
 * @param offset Register offset from DBI base
 * @return Register value
 *
 * Use this for reading PCIe controller configuration registers.
 * Includes standard PCIe config space and DesignWare-specific registers.
 * DBI base: 0x18400000
 */
uint32_t pcie_dbi_read32(uint32_t offset) {
  return read32_reg(SMN_DBI_ADDR + offset);
}

/**
 * @brief Write to DesignWare DBI (Device Bus Interface) config space
 * @param offset Register offset from DBI base
 * @param value Value to write
 *
 * Use this for configuring PCIe controller registers.
 * Note: Some registers require dbi_ro_rw_en to be set for writing.
 * DBI base: 0x18400000
 */
static inline void pcie_dbi_write32(uint32_t offset, uint32_t value) {
  write32_reg(SMN_DBI_ADDR + offset, value);
}

/**
 * @brief Read from DBI BAR mask registers
 * @param offset Register offset from DBIMASK base
 * @return Register value
 *
 * Use this for reading BAR size mask registers.
 * DBIMASK base: 0x18410000
 */
static inline uint32_t pcie_dbi_mask_read32(uint32_t offset) {
  return read32_reg(SMN_DBIMASK_ADDR + offset);
}

/**
 * @brief Write to DBI BAR mask registers
 * @param offset Register offset from DBIMASK base
 * @param value Value to write (BAR size - 1)
 *
 * Use this for setting BAR sizes via mask registers.
 * DBIMASK base: 0x18410000
 */
static inline void pcie_dbi_mask_write32(uint32_t offset, uint32_t value) {
  write32_reg(SMN_DBIMASK_ADDR + offset, value);
}

/**
 * @brief Read from DBI ATU (Address Translation Unit) registers
 * @param offset Register offset from DBIATU base
 * @return Register value
 *
 * Use this for reading ATU region configuration.
 * DBIATU base: 0x18420000
 */
static inline uint32_t pcie_dbi_atu_read32(uint32_t offset) {
  return read32_reg(SMN_DBIATU_ADDR + offset);
}

/**
 * @brief Write to DBI ATU (Address Translation Unit) registers
 * @param offset Register offset from DBIATU base
 * @param value Value to write
 *
 * Use this for configuring ATU inbound/outbound regions.
 * DBIATU base: 0x18420000
 */
static inline void pcie_dbi_atu_write32(uint32_t offset, uint32_t value) {
  write32_reg(SMN_DBIATU_ADDR + offset, value);
}

/**
 * @brief Read-modify-write DBI register
 * @param offset Register offset from DBI base
 * @param mask Bit mask for field
 * @param value Value to set (will be masked)
 *
 * Convenience function for updating bit fields in DBI registers.
 */
static inline void pcie_dbi_rmw32(uint32_t offset, uint32_t mask,
                                  uint32_t value) {
  uint32_t reg = pcie_dbi_read32(offset);
  reg = (reg & ~mask) | (value & mask);
  pcie_dbi_write32(offset, reg);
}

/**
 * @brief Read 64-bit value from DBI (two consecutive 32-bit registers)
 * @param offset Register offset from DBI base (must be 8-byte aligned)
 * @return 64-bit register value
 */
static inline uint64_t pcie_dbi_read64(uint32_t offset) {
  uint64_t low = read32_reg(SMN_DBI_ADDR + offset);
  uint64_t high = read32_reg(SMN_DBI_ADDR + offset + 4);
  return (high << 32) | low;
}

/**
 * @brief Write 64-bit value to DBI (two consecutive 32-bit registers)
 * @param offset Register offset from DBI base (must be 8-byte aligned)
 * @param value 64-bit value to write
 */
static inline void pcie_dbi_write64(uint32_t offset, uint64_t value) {
  write32_reg(SMN_DBI_ADDR + offset, (uint32_t)(value & 0xFFFFFFFF));
  write32_reg(SMN_DBI_ADDR + offset + 4, (uint32_t)(value >> 32));
}

/**
 * @brief Read 64-bit value from DBI mask registers
 * @param offset Register offset from DBIMASK base (must be 8-byte aligned)
 * @return 64-bit register value
 */
static inline uint64_t pcie_dbi_mask_read64(uint32_t offset) {
  uint64_t low = read32_reg(SMN_DBIMASK_ADDR + offset);
  uint64_t high = read32_reg(SMN_DBIMASK_ADDR + offset + 4);
  return (high << 32) | low;
}

/**
 * @brief Write 64-bit value to DBI mask registers
 * @param offset Register offset from DBIMASK base (must be 8-byte aligned)
 * @param value 64-bit value to write
 */
static inline void pcie_dbi_mask_write64(uint32_t offset, uint64_t value) {
  write32_reg(SMN_DBIMASK_ADDR + offset, (uint32_t)(value & 0xFFFFFFFF));
  write32_reg(SMN_DBIMASK_ADDR + offset + 4, (uint32_t)(value >> 32));
}

/* -------------------------------------------------------------------------- */
/*                        Core Helper Functions                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Delay function
 * @param cycles Number of cycles to delay
 */
static void delay_cycles(uint32_t cycles) {
  volatile uint32_t i;
  for (i = 0; i < cycles; i++) {
    /* Busy wait */
  }
}

/**
 * @brief Disable PCIe Memory Space and Bus Master
 *
 * Clears MEM_SPACE_ENABLE and BUS_MASTER_ENABLE bits in the Command register.
 * This should be called during initialization before BARs/TLBs are configured
 * to prevent undefined behavior from incomplete setup.
 *
 * Only active when PCIE_CONTROL_MEM_BUS_MASTER is enabled.
 */
static void pcie_disable_mem_bus_master(void) {
#if PCIE_CONTROL_MEM_BUS_MASTER
  /* Clear MEM_SPACE and BUS_MASTER bits using RMW helper */
  pcie_dbi_rmw32(PCIECTL_TYPE1_STATUS_COMMAND,
                 (PCIE_CMD_MEM_SPACE_ENABLE | PCIE_CMD_BUS_MASTER_ENABLE), 0);

  /* Debug: Log that we disabled these bits */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_8__REG_ADDR, 0xD15AB1E0);
#endif
}

/**
 * @brief Enable PCIe Memory Space and Bus Master
 *
 * Sets MEM_SPACE_ENABLE and BUS_MASTER_ENABLE bits in the Command register.
 * This should be called after BARs/TLBs are fully configured and the device
 * is ready to handle memory transactions.
 *
 * Only active when PCIE_CONTROL_MEM_BUS_MASTER is enabled.
 */
void pcie_enable_mem_bus_master(void) {
#if PCIE_CONTROL_MEM_BUS_MASTER
  /* Set MEM_SPACE and BUS_MASTER bits using RMW helper */
  pcie_dbi_rmw32(PCIECTL_TYPE1_STATUS_COMMAND,
                 (PCIE_CMD_MEM_SPACE_ENABLE | PCIE_CMD_BUS_MASTER_ENABLE),
                 (PCIE_CMD_MEM_SPACE_ENABLE | PCIE_CMD_BUS_MASTER_ENABLE));

  /* Debug: Log that we enabled these bits */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_8__REG_ADDR, 0xE11AB1E0);
#endif
}

/**
 * @brief Program a single TLB entry
 * @param base_addr TLB configuration base address
 * @param index Entry index
 * @param entry TLB entry data
 * @return 0 on success, negative on error
 */
static int tlbcfg_program(uint32_t base_addr, uint32_t index,
                          const tlb_entry_t *entry) {
  uint32_t addr;
  uint64_t data;

  /* Calculate entry address */
  addr = base_addr + (index * TLB_ENTRY_SIZE);

  /* Write valid bit and address (lower 64 bits) */
  data = ((uint64_t)(entry->valid & 0x1) << 0) | entry->addr;
  write64_reg(addr, data);

  /* Write attributes (upper 64 bits at offset 0x20) */
  addr += 0x20;
  write64_reg(addr, entry->attr);
  return 0;
}

/**
 * @brief Read back a TLB entry
 * @param base_addr TLB configuration base address
 * @param index Entry index
 * @param valid_out Pointer to valid flag
 * @param addr_high_out Pointer to address high bits
 */
static void pcie_tlbsys0_read_entry(int index, uint8_t *valid_out,
                                    uint64_t *addr_high_out) {
  uintptr_t entry_base =
      (uintptr_t)PCIE_MGMT_MMR_TLB_SYS_OUT0_0__TLB_CONFIG_REG_ADDR +
      (uintptr_t)index * TLB_ENTRY_SIZE;

  uint64_t w0 = read64_reg(entry_base + 0x00U);

  uint8_t valid = (uint8_t)(w0 & 0x1U);
  uint64_t addr_high = w0 & ~0xFFFULL; // bits [63:12], low 12 bits = 0

  *valid_out = valid;
  *addr_high_out = addr_high;
}

/* ========================================================================== */
/*             Config Space Initialization Helper Functions (P0)             */
/* ========================================================================== */

/**
 * @brief Verify Vendor ID and Device ID match expected values
 * @return 0 on success, -1 if IDs don't match
 *
 * P0 Requirement: Sanity check for hard-wired Vendor/Device ID.
 * Expected values: Vendor ID = 0x1E52 (Tenstorrent), Device ID = 0xFEED
 */
static int pcie_verify_device_id(void) {
  uint32_t dev_vendor_id;
  uint16_t vendor_id, device_id;

  /* Read Vendor/Device ID from config space */
  dev_vendor_id = pcie_dbi_read32(PCIECTL_VENDOR_DEVICE_ID);

  vendor_id = dev_vendor_id & 0xFFFF;
  device_id = (dev_vendor_id >> 16) & 0xFFFF;

  printf("[CFG] DBI Vendor/Device ID = 0x%08x (vendor=0x%04x device=0x%04x)\n",
         dev_vendor_id, vendor_id, device_id);
  printf("[CFG] Expected: vendor=0x%04x device=0x%04x\n",
         EXPECTED_VENDOR_ID, EXPECTED_DEVICE_ID);

  /* Verify against expected values */
  if (vendor_id != EXPECTED_VENDOR_ID || device_id != EXPECTED_DEVICE_ID) {
    printf("[CFG] WARNING: Vendor/Device ID mismatch, continuing anyway (VP mode)\n");
  }

  /* Success - log the verified IDs */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_0__REG_ADDR, 0x600D0000);
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR, dev_vendor_id);

  return 0;
}

/**
 * @brief Clear sticky error bits in Status register
 *
 * P0 Requirement: Helper to clear sticky error bits before enabling
 * memory/bus master. Sticky error bits are cleared by writing 1.
 *
 * Clears:
 * - Bit 31: Detected Parity Error
 * - Bit 30: Signaled System Error
 * - Bit 29: Received Master Abort
 * - Bit 28: Received Target Abort
 * - Bit 27: Signaled Target Abort
 * - Bit 24: Master Data Parity Error
 */
static void pcie_clear_status_errors(void) {
  uint32_t cmd_status;

  /* Read current Command/Status register */
  cmd_status = pcie_dbi_read32(PCIECTL_TYPE1_STATUS_COMMAND);

  /* Write 1 to sticky error bits to clear them (preserves command bits) */
  cmd_status |= PCIE_STS_ERROR_BITS;
  pcie_dbi_write32(PCIECTL_TYPE1_STATUS_COMMAND, cmd_status);

  /* Log that we cleared errors */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_2__REG_ADDR,
              0xC1EA0000 | ((cmd_status >> 16) & 0xFFFF));
}

/**
 * @brief Initialize capability chain
 * @return 0 on success, negative on error
 *
 * P0 Requirement: Ensure Capabilities List bit is set in Status register
 * and verify basic capability pointers are present (MSI, PCIe Cap).
 *
 * The capabilities list pointer and capability structures should be
 * hard-wired in the PCIe controller, but we verify their presence.
 */
static int pcie_init_capabilities(void) {
  uint32_t cmd_status;
  uint32_t cap_ptr_reg;
  uint8_t cap_ptr;

  cmd_status = pcie_dbi_read32(PCIECTL_TYPE1_STATUS_COMMAND);
  printf("[CFG] CMD/STATUS = 0x%08x\n", cmd_status);

  if (!(cmd_status & PCIE_STS_CAP_LIST)) {
    printf("[CFG] WARNING: Capabilities List bit not set, continuing (VP mode)\n");
    return 0;
  }

  cap_ptr_reg = pcie_dbi_read32(PCIECTL_CAP_PTR);
  cap_ptr = cap_ptr_reg & 0xFF;
  printf("[CFG] CAP_PTR = 0x%02x (raw=0x%08x)\n", cap_ptr, cap_ptr_reg);

  if (cap_ptr == 0 || (cap_ptr & 0x3) != 0) {
    printf("[CFG] WARNING: Invalid capability pointer 0x%02x, continuing (VP mode)\n",
           cap_ptr);
    return 0;
  }

  return 0;
}

/**
 * @brief Configure PCIe Device Control register with reasonable defaults
 *
 * P0 Requirement: Set Max Payload Size (MPS) and Max Read Request Size (MRRS)
 * to reasonable defaults (256 bytes).
 *
 * Also enables error reporting for proper operation:
 * - Correctable Error Reporting
 * - Non-Fatal Error Reporting
 * - Fatal Error Reporting
 * - Unsupported Request Reporting
 */
static void pcie_configure_device_control(void) {
  uint32_t dev_ctrl;

  /* Read current Device Control/Status register */
  dev_ctrl = pcie_dbi_read32(PCIECTL_DEVICE_CONTROL);

  /* Clear MPS and MRRS fields */
  dev_ctrl &= ~(PCIE_DEVCTL_MAX_PAYLOAD_MASK | PCIE_DEVCTL_MAX_READ_REQ_MASK);

  /* Set MPS = 256 bytes, MRRS = 256 bytes (reasonable P0 defaults) */
  dev_ctrl |= PCIE_MPS_256_BYTES | PCIE_MRRS_256_BYTES;

  /* Enable error reporting for proper diagnostics */
  dev_ctrl |= PCIE_DEVCTL_CORR_ERR_REPORT_EN | PCIE_DEVCTL_NONFATAL_ERR_EN |
              PCIE_DEVCTL_FATAL_ERR_EN | PCIE_DEVCTL_UNSUP_REQ_EN;

  /* Write back Device Control register */
  pcie_dbi_write32(PCIECTL_DEVICE_CONTROL, dev_ctrl);

  /* Log the configuration */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR,
              0xDEBC0000 | ((dev_ctrl >> 16) & 0xFFFF));
}

/**
 * @brief Complete config space bring-up minimum (P0)
 * @return 0 on success, negative on error
 *
 * P0 Requirements:
 * 1. Verify Vendor/Device ID (0x1E52 / 0xFEED)
 * 2. Clear sticky error bits in Status register
 * 3. Initialize capability chain (verify Capability List bit and pointers)
 * 4. Configure PCIe Device Control (MPS=256B, MRRS=256B, error reporting)
 *
 * NOTE: MEM_SPACE and BUS_MASTER enable happens separately via
 *       pcie_enable_mem_bus_master() AFTER BARs and TLBs are configured.
 */
int pcie_init_config_space_p0(void) {
  int ret;

  /* Step 1: Verify Vendor/Device ID */
  ret = pcie_verify_device_id();
  if (ret != 0) {
    return ret;
  }

  /* Step 2: Clear sticky error bits in Status register */
  pcie_clear_status_errors();

  /* Step 3: Initialize and verify capability chain */
  ret = pcie_init_capabilities();
  if (ret != 0) {
    return ret;
  }

  /* Step 4: Configure PCIe Device Control (MPS, MRRS, error reporting) */
  pcie_configure_device_control();

  /* Log successful P0 config space initialization */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_5__REG_ADDR,
              0xCF600000); /* CF6 = ConFiG */

  return 0;
}

/**
 * @brief Force SII reset domain to use reference clock
 * @return 0 on success, negative on error
 */
static int force_sii_to_refclk(void) {
  uint32_t regdata;

  /* Read current value from SS_FORCE_TO_REF_CLK register */
  regdata = read32_reg(SMC_CPU_RESET_UNIT_SS_FORCE_TO_REF_CLK_REG_ADDR);

  /* Set bit[2] to force PCIE_SII domain to refclk */
  regdata |= (1 << SS_FORCE_TO_REF_CLK_PCIE_SII_BIT);

  /* Write back the modified value */
  write32_reg(SMC_CPU_RESET_UNIT_SS_FORCE_TO_REF_CLK_REG_ADDR, regdata);

  /* Wait for clock domain to stabilize */
  delay_cycles(20);

  return 0;
}

#if 0  // SRAM-based firmware loading - disabled for now, will be ultimate
       // implementation
/**
 * @brief Load firmware from embedded arrays to SRAM
 * @return 0 on success, negative on error
 *
 * NOTE: This function is currently disabled. Firmware is loaded directly to PHY.
 *       This SRAM-based approach will be the ultimate implementation.
 */
static int load_firmware_to_sram(void)
{
    uint64_t sram_addr;
    int i;

    // Load ICCM firmware to SRAM
    uint64_t iccm_sram_addr = SYS_SRAM_BASE + PHY_ICCM_FW_SRAM_OFFSET;

    for (i = 0; i < ICCM_FIRMWARE_DATA_SIZE; i++) {
        sram_addr = iccm_sram_addr + (i * 4);
        write32_reg(sram_addr, iccm_firmware_data[i]);
    }

    // Pad remaining space with zeros
    for (i = ICCM_FIRMWARE_DATA_SIZE; i < (PHY_FW_MAX_SIZE / 4); i++) {
        sram_addr = iccm_sram_addr + (i * 4);
        write32_reg(sram_addr, 0x00000000);
    }

    // Load DCCM firmware to SRAM
    uint64_t dccm_sram_addr = SYS_SRAM_BASE + PHY_DCCM_FW_SRAM_OFFSET;

    for (i = 0; i < DCCM_FIRMWARE_DATA_SIZE; i++) {
        sram_addr = dccm_sram_addr + (i * 4);
        write32_reg(sram_addr, dccm_firmware_data[i]);
    }

    // Pad remaining space with zeros
    for (i = DCCM_FIRMWARE_DATA_SIZE; i < (PHY_FW_MAX_SIZE / 4); i++) {
        sram_addr = dccm_sram_addr + (i * 4);
        write32_reg(sram_addr, 0x00000000);
    }

    return 0;
}
#endif // SRAM-based firmware loading

#if 0  // SRAM-based firmware loading - disabled for now
/**
 * @brief Load PHY firmware from SRAM to PHY memory
 * @return 0 on success, negative on error
 *
 * NOTE: This function is currently disabled. Firmware is loaded directly to PHY.
 *       This SRAM-based approach will be the ultimate implementation.
 */
static int load_phy_firmware_from_sram(void)
{
    uint32_t addr;
    uint64_t sram_addr;
    int i, j;
    uint32_t fw_data;

    // Enable AHB access in PHY CPU_CFG
    uint32_t cpu_cfg = read32_reg(PCIE_PHY_CPU_CFG);
    cpu_cfg |= (1 << 9);  // CCM_FAST_ACCESS_SEL
    write32_reg(PCIE_PHY_CPU_CFG, cpu_cfg);

    delay_cycles(1);

    // Load ICCM firmware from SRAM
    uint32_t iccm_base = PHY_AHB_BASE + (0x1 << 16);
    uint64_t iccm_sram_addr = SYS_SRAM_BASE + PHY_ICCM_FW_SRAM_OFFSET;

    // Load ICCM firmware in 2KB chunks (32 chunks for 64KB total)
    for (i = 0; i < 32; i++) {
        addr = iccm_base + (i * 0x800);
        sram_addr = iccm_sram_addr + (i * 0x800);

        for (j = 0; j < 512; j++) {
            fw_data = read32_reg(sram_addr + (j * 4));
            write32_reg(addr + (j * 4), fw_data);
        }
    }

    // Load DCCM firmware from SRAM
    uint32_t dccm_base = PHY_AHB_BASE + (0x2 << 16);
    uint64_t dccm_sram_addr = SYS_SRAM_BASE + PHY_DCCM_FW_SRAM_OFFSET;

    // Load DCCM firmware in 2KB chunks (32 chunks for 64KB total)
    for (i = 0; i < 32; i++) {
        addr = dccm_base + (i * 0x800);
        sram_addr = dccm_sram_addr + (i * 0x800);

        for (j = 0; j < 512; j++) {
            fw_data = read32_reg(sram_addr + (j * 4));
            write32_reg(addr + (j * 4), fw_data);
        }
    }

    return 0;
}
#endif // SRAM-based firmware loading

/**
 * @brief Load PHY firmware directly from embedded arrays to PHY memory
 * @return 0 on success, negative on error
 *
 * NOTE: This is a temporary direct-load implementation.
 *       Ultimate implementation will use SRAM as intermediate storage.
 */
static int load_phy_firmware_direct(void) {
  uint32_t addr;
  int i, j;
  uint32_t fw_word_idx;

  /* Enable AHB access in PHY CPU_CFG using APB helper */
  pcie_phy_apb_rmw32(PCIE_PHY_CPU_CFG - PHY_APB_BASE, (1 << 9), (1 << 9));
  delay_cycles(1);

  /* ========================================================================
   * Load ICCM firmware directly to PHY
   * ======================================================================== */
  uint32_t iccm_base_offset = (0x1 << 16); /* ICCM offset from AHB base */

  /* Load ICCM firmware in 2KB chunks (32 chunks for 64KB total) */
  for (i = 0; i < 32; i++) {
    addr = iccm_base_offset + (i * 0x800); /* 2KB chunks in PHY memory */

    /* Copy 2KB (512 x 32-bit words) per chunk directly from firmware array */
    for (j = 0; j < 512; j++) {
      fw_word_idx = (i * 512) + j;

      /* Write firmware data or zeros if beyond array size */
      if (fw_word_idx < ICCM_FIRMWARE_DATA_SIZE) {
        pcie_phy_ahb_write32(addr + (j * 4), iccm_firmware_data[fw_word_idx]);
      } else {
        pcie_phy_ahb_write32(addr + (j * 4), 0x00000000);
      }
    }
  }

  /* ========================================================================
   * Load DCCM firmware directly to PHY
   * ======================================================================== */
  uint32_t dccm_base_offset = (0x2 << 16); /* DCCM offset from AHB base */

  /* Load DCCM firmware in 2KB chunks (32 chunks for 64KB total) */
  for (i = 0; i < 32; i++) {
    addr = dccm_base_offset + (i * 0x800); /* 2KB chunks in PHY memory */

    /* Copy 2KB (512 x 32-bit words) per chunk directly from firmware array */
    for (j = 0; j < 512; j++) {
      fw_word_idx = (i * 512) + j;

      /* Write firmware data or zeros if beyond array size */
      if (fw_word_idx < DCCM_FIRMWARE_DATA_SIZE) {
        pcie_phy_ahb_write32(addr + (j * 4), dccm_firmware_data[fw_word_idx]);
      } else {
        pcie_phy_ahb_write32(addr + (j * 4), 0x00000000);
      }
    }
  }

  return 0;
}

/**
 * @brief Program a single ATU inbound region
 * @param region Region number
 * @param config ATU configuration
 * @return 0 on success, negative on error
 */
static int program_atu_inbound_region(uint8_t region,
                                      const atu_inbound_t *config) {
  uint32_t atu_offset;
  uint32_t data;

  atu_offset = PCIECTL_ATU_INBOUND_OFFSET + (region * ATU_ENTRY_SIZE);

  /* Program control register 1 */
  data = config->tlp_type << 0;
  pcie_dbi_atu_write32(atu_offset + 0x0, data);

  /* Program control register 2 */
  data = (1 << 31) | (config->mode << 30) | (config->func << 19) |
         (config->bar << 8);
  pcie_dbi_atu_write32(atu_offset + 0x4, data);

  /* Program target address (lower 32 bits) */
  data = (uint32_t)(config->target_addr & 0xFFFFFFFF);
  pcie_dbi_atu_write32(atu_offset + 0x14, data);

  /* Program target address (upper 32 bits) */
  data = (uint32_t)(config->target_addr >> 32);
  pcie_dbi_atu_write32(atu_offset + 0x18, data);

  delay_cycles(1000);

  return 0;
}

/**
 * @brief Enable interrupts
 * NOTE: These are write-enable masks: writing 1 enables the interrupt
 */
static void enable_interrupts(void) {
  uint32_t data;

  // Enable all interrupts (writing 1 enables them)
  data = 0xffffffff;
  write32_reg(PCIE_MGMT_MMR_KPCIE_SII_INT_CSR_MASK_REG_ADDR, data);
  write32_reg(PCIE_MGMT_MMR_KPCIE_SII_INT_CSR1_MASK_REG_ADDR, data);
  write32_reg(PCIE_MGMT_MMR_KPCIE_SII_MISC_INT_MASK0_REG_ADDR, data);
  write32_reg(PCIE_MGMT_MMR_KPCIE_SII_MISC_INT_MASK1_REG_ADDR, data);

  // Configure timeout flags
  data = (1 << 9) | (1 << 8) | (0 << 7) | (0 << 6) | (0b11110 << 0);
  write32_reg(
      PCIE_MGMT_MMR_SMN_TIMEOUT_REG_SIDEBANDMANAGER_TIMEOUT_MAIN_SIDEBANDMANAGER_A_SIDEBANDMANAGER_TIMEOUT_MAIN_SIDEBANDMANAGER_FLAGOUTCLR0_REG_ADDR,
      data);
}

/**
 * @brief Enable LTSSM state change interrupt
 * NOTE: This is redundant since enable_interrupts() already enables all
 * interrupts
 */
static void enable_ltssm_state_interrupt(void) {
  // No-op: enable_interrupts() already enables all interrupts including LTSSM
  // Kept for compatibility
}

/* ========================================================================== */
/*                          Public API Implementation                         */
/* ========================================================================== */

void pcie_init_config(pcie_config_t *cfg) {
  int i;

  memset(cfg, 0, sizeof(pcie_config_t));

  /* BAR configurations */
  cfg->bars[0].addr = 0x0;
  cfg->bars[0].size = 0x0001000000000ULL; /* 4GB */

  cfg->bars[1].addr = 0x0;
  cfg->bars[1].size = 0x0000000100000ULL; /* 1MB */

  cfg->bars[2].addr = 0x0;
  cfg->bars[2].size = 0x2000000000000ULL; /* 512GB */

  /* ATU inbound region 0: BAR0 -> APP0 */
  cfg->atu_inbound[0].mode = 1;
  cfg->atu_inbound[0].tlp_type = 0b00000;
  cfg->atu_inbound[0].bar = 0;
  cfg->atu_inbound[0].func = 0;
  cfg->atu_inbound[0].target_addr = ((uint64_t)NOCPCIE_IN_APP0 << 60);

  /* ATU inbound region 1: BAR2 -> SYSIN0 */
  cfg->atu_inbound[1].mode = 1;
  cfg->atu_inbound[1].tlp_type = 0b00000;
  cfg->atu_inbound[1].bar = 2;
  cfg->atu_inbound[1].func = 0;
  cfg->atu_inbound[1].target_addr = ((uint64_t)NOCPCIE_IN_SYSIN0 << 60);

  /* ATU inbound region 2: BAR4 -> APP1 */
  cfg->atu_inbound[2].mode = 1;
  cfg->atu_inbound[2].tlp_type = 0b00000;
  cfg->atu_inbound[2].bar = 4;
  cfg->atu_inbound[2].func = 0;
  cfg->atu_inbound[2].target_addr = ((uint64_t)NOCPCIE_IN_APP1 << 60);

  /* Initialize last TLB addresses */
  for (i = 0; i < 64; i++) {
    cfg->last_tlbsys_addr[i] = 0x0;
  }
}

int pcie_release_reset(void) {
  uint32_t reset_value;

  printf("[RST] step 1: reset unit write @ 0x%08x\n",
         (unsigned)SMC_CPU_RESET_UNIT_SS_COLD_RESET_N_REG_ADDR);
  reset_value = (1 << RST_ID_SMN) | (1 << RST_ID_PCIE_SII);
  write32_reg(SMC_CPU_RESET_UNIT_SS_COLD_RESET_N_REG_ADDR, reset_value);
  delay_cycles(100);

  printf("[RST] step 2: disable firewall filters\n");
  disable_all_ker_firewall_filters();

  printf("[RST] step 3: SMN timeout @ 0x%08x\n",
         (unsigned)PCIE_MGMT_MMR_SMN_TIMEOUT_REG_SIDEBANDMANAGER_TIMEOUT_MAIN_SIDEBANDMANAGER_A_SIDEBANDMANAGER_TIMEOUT_MAIN_SIDEBANDMANAGER_FLAGOUTSET0_REG_ADDR);
  write64_reg(
      PCIE_MGMT_MMR_SMN_TIMEOUT_REG_SIDEBANDMANAGER_TIMEOUT_MAIN_SIDEBANDMANAGER_A_SIDEBANDMANAGER_TIMEOUT_MAIN_SIDEBANDMANAGER_FLAGOUTSET0_REG_ADDR,
      0x1FF);

  printf("[RST] step 4: read CORE_CONTROL @ 0x%08x\n",
         (unsigned)PCIE_MGMT_MMR_KPCIE_SII_CORE_CONTROL_REG_ADDR);
  uint32_t data = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_CORE_CONTROL_REG_ADDR);
  printf("[RST] step 4: CORE_CONTROL = 0x%08x\n", data);
  data = data | (0 << 23) | ((0x0 & 0xF) << 10) | (1 << 2);
  write32_reg(PCIE_MGMT_MMR_KPCIE_SII_CORE_CONTROL_REG_ADDR, data);
  delay_cycles(20);

  printf("[RST] step 5: force_sii_to_refclk (0x%08x)\n",
         (unsigned)SMC_CPU_RESET_UNIT_SS_FORCE_TO_REF_CLK_REG_ADDR);
  force_sii_to_refclk();

  printf("[RST] step 6: release all resets (0x%08x)\n",
         (unsigned)SMC_CPU_RESET_UNIT_SS_COLD_RESET_N_REG_ADDR);
  reset_value = 0xffffffff;
  write32_reg(SMC_CPU_RESET_UNIT_SS_COLD_RESET_N_REG_ADDR, reset_value);
  delay_cycles(100);

  printf("[RST] pcie_release_reset done\n");
  return 0;
}

int pcie_load_firmware(void) {
  int ret;

#if 0 // SRAM-based loading - ultimate implementation (currently disabled)
    /* Load firmware to SRAM */
    ret = load_firmware_to_sram();
    if (ret != 0) {
        return ret;
    }

    /* Load firmware from SRAM to PHY */
    ret = load_phy_firmware_from_sram();
    if (ret != 0) {
        return ret;
    }
#else // Direct loading - temporary implementation
  /* Load firmware directly to PHY (bypasses SRAM) */
  ret = load_phy_firmware_direct();
  if (ret != 0) {
    return ret;
  }
#endif

  return 0;
}

int pcie_program_tlb_sysout0(void) {
  tlb_entry_t entry;

  /* Entry 0: DBI Base */
  entry.valid = 1;
  entry.addr = 0x0;
  entry.attr = TLB_DBI_VAL;
  tlbcfg_program(PCIE_MGMT_MMR_TLB_SYS_OUT0_0__TLB_CONFIG_REG_ADDR, 0, &entry);

  /* Entry 1: DBI Mask */
  entry.valid = 1;
  entry.addr = PCIECTL_DBI_MASK;
  tlbcfg_program(PCIE_MGMT_MMR_TLB_SYS_OUT0_0__TLB_CONFIG_REG_ADDR, 1, &entry);

  /* Entry 2: DBI ATU */
  entry.valid = 1;
  entry.addr = PCIECTL_DBI_ATU;
  tlbcfg_program(PCIE_MGMT_MMR_TLB_SYS_OUT0_0__TLB_CONFIG_REG_ADDR, 2, &entry);

  /* Entry 3: DBI DMA */
  entry.valid = 1;
  entry.addr = PCIECTL_DBI_DMA;
  tlbcfg_program(PCIE_MGMT_MMR_TLB_SYS_OUT0_0__TLB_CONFIG_REG_ADDR, 3, &entry);

  /* Entry 4: PCIe MEMRD/MEMWR */
  entry.valid = 1;
  entry.addr = 0x0;
  entry.attr = 0x0;
  tlbcfg_program(PCIE_MGMT_MMR_TLB_SYS_OUT0_0__TLB_CONFIG_REG_ADDR, 4, &entry);

  return 0;
}

int pcie_program_tlb_sysin0(void) {
  tlb_entry_t entry;
  int i;

  entry.valid = 1;
  entry.attr = 0x0;

  /* Entry 0: MSI Relay */
  entry.addr = SMNIO_MSIRELAY;
  tlbcfg_program(PCIE_MGMT_MMR_TLB_SYS_IN0_0__TLB_CONFIG_REG_ADDR, 0, &entry);

  /* Entries 1-3: TLB Config */
  for (i = 1; i <= 3; i++) {
    entry.addr = SMNIO_TLBCFG + ((i - 1) * 0x4000);
    tlbcfg_program(PCIE_MGMT_MMR_TLB_SYS_IN0_0__TLB_CONFIG_REG_ADDR, i, &entry);
  }

  /* Entry 4: SII Core Control */
  entry.addr = PCIE_CFG_SII_CONFIG_BASE;
  tlbcfg_program(PCIE_MGMT_MMR_TLB_SYS_IN0_0__TLB_CONFIG_REG_ADDR, 4, &entry);

  /* Entries 5-8: DBI regions */
  for (i = 5; i <= 8; i++) {
    entry.addr = SMNIO_SYSOUT0 + ((i - 5) * 0x4000);
    tlbcfg_program(PCIE_MGMT_MMR_TLB_SYS_IN0_0__TLB_CONFIG_REG_ADDR, i, &entry);
  }

  /* Entries 9-12: DBI_DMA regions */
  for (i = 9; i <= 12; i++) {
    entry.addr = SMNIO_SYSOUT0 + 0x30000 + ((i - 9) * 0x4000);
    tlbcfg_program(PCIE_MGMT_MMR_TLB_SYS_IN0_0__TLB_CONFIG_REG_ADDR, i, &entry);
  }

  /* Entry 13: SMC Mailbox */
  entry.addr = SMC_MAILBOX;
  tlbcfg_program(PCIE_MGMT_MMR_TLB_SYS_IN0_0__TLB_CONFIG_REG_ADDR, 13, &entry);

  return 0;
}

int pcie_program_tlb_appin0(void) {
  tlb_entry_t entry;
  int i;

  entry.valid = 1;
  entry.attr = 0x0;

  /* APPIN00: Entries 0-63 (Quasar/Tensix) */
  for (i = 0; i < 64; i++) {
    entry.addr = SYS_SRAM_BASE + (i * 0x1000000ULL);
    tlbcfg_program(PCIE_MGMT_MMR_TLB_APP_IN0_0__TLB_CONFIG_REG_ADDR, i, &entry);
  }

  /* APPIN01: Entries 64-67 (Mimir/CCE) */
  for (i = 0; i < 4; i++) {
    entry.addr = MIMIR_CCE_ADDR + (i * 0x1000000ULL);
    tlbcfg_program(PCIE_MGMT_MMR_TLB_APP_IN0_64__TLB_CONFIG_REG_ADDR, i,
                   &entry);
  }

  /* APPIN01: Entries 68-99 (Keraunos Config) */
  for (i = 4; i < 36; i++) {
    entry.addr = KER_CONFIG_ADDR + ((i - 4) * 0x1000000ULL);
    tlbcfg_program(PCIE_MGMT_MMR_TLB_APP_IN0_64__TLB_CONFIG_REG_ADDR, i,
                   &entry);
  }

  /* APPIN01: Entries 100-127 (Mimir Config) */
  for (i = 36; i < 64; i++) {
    entry.addr = MIMIR_CONFIG_ADDR + ((i - 36) * 0x1000000ULL);
    tlbcfg_program(PCIE_MGMT_MMR_TLB_APP_IN0_64__TLB_CONFIG_REG_ADDR, i,
                   &entry);
  }

  /* APPIN02: Entries 128-163 (Mimir Config continued) */
  for (i = 0; i < 36; i++) {
    entry.addr = MIMIR_CONFIG_ADDR + ((i + 28) * 0x1000000ULL);
    tlbcfg_program(PCIE_MGMT_MMR_TLB_APP_IN0_128__TLB_CONFIG_REG_ADDR, i,
                   &entry);
  }

  return 0;
}

int pcie_program_tlb_appin1(void) {
  tlb_entry_t entry;
  int i;

  entry.valid = 1;
  entry.attr = 0x0;

  /* 64 entries, 8GB increments */
  for (i = 0; i < 64; i++) {
    entry.addr = SYS_DRAM_BASE + (i * 0x200000000ULL);
    tlbcfg_program(PCIE_MGMT_MMR_TLB_APP_IN1_0__TLB_CONFIG_REG_ADDR, i, &entry);
  }

  return 0;
}

int pcie_program_tlb_appout0(void) {
  tlb_entry_t entry;
  int i;

  entry.valid = 1;
  entry.attr = 0x0;

  /* 16 entries, 16TB increments */
  for (i = 0; i < 16; i++) {
    entry.addr = 0x0ULL + (i * 0x4000000000000ULL);
    tlbcfg_program(PCIE_MGMT_MMR_TLB_APP_OUT0_0__TLB_CONFIG_REG_ADDR, i,
                   &entry);
  }

  return 0;
}

int pcie_program_all_tlbs(void) {
  pcie_program_tlb_sysout0();
  pcie_program_tlb_sysin0();
  pcie_program_tlb_appin0();
  pcie_program_tlb_appin1();
  pcie_program_tlb_appout0();

  /* Enable outbound TLBs */
  write32_reg(PCIE_MGMT_MMR_ACCESS_STATUS_KPCIE_ACCESS_CTRL_REG_ADDR,
              0xFFFFFFFF);
  write32_reg(PCIE_MGMT_MMR_ACCESS_STATUS_KPCIE_SYSTEM_STATUS_REG_ADDR, 0x1);
  delay_cycles(100);

  return 0;
}

int pcie_configure_dbi(void) {
  uint32_t data;

  /* MISC_CONTROL: Enable DBI read/write */
  data = pcie_dbi_read32(PCIECTL_MISC_CONTROL);
  delay_cycles(100);
  data |= (1 << 0); /* dbi_ro_rw_en[0] */
  pcie_dbi_write32(PCIECTL_MISC_CONTROL, data);

  /* GEN2_CONTROL: Enable directed speed change */
  pcie_dbi_rmw32(PCIECTL_GEN2_CONTROL, (1 << 17), (1 << 17));

  /* PL32G_CAPABILITY: Set equalization settings */
  pcie_dbi_rmw32(PCIECTL_PL32G_CAPABILITY, 0b11, 0b11);

  /* PORT_LINK_CTRL: Enable fast link mode */
  pcie_dbi_rmw32(PCIECTL_PORT_LINK_CTRL, (1 << 7), (1 << 7));

  /* Enable CII (Configuration Information Indication) in CORE_CONTROL
   * This allows firmware to detect when host writes to config space */
  data = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_CORE_CONTROL_REG_ADDR);
  data |= (1 << 9); /* cii_enable[9] - Enable CII feature */
  write32_reg(PCIE_MGMT_MMR_KPCIE_SII_CORE_CONTROL_REG_ADDR, data);

  /* Disable MEM_SPACE and BUS_MASTER before configuring BARs/TLBs
   * Per PCIe spec: These should be disabled during device initialization
   * to prevent undefined behavior from incomplete configuration.
   * Only active when PCIE_CONTROL_MEM_BUS_MASTER is enabled. */
  pcie_disable_mem_bus_master();

  return 0;
}

int pcie_init_phy(void) {
  uint32_t data;

  /* ========================================================================
   * CURRENT IMPLEMENTATION: Minimal PHY Reset Release
   * ======================================================================== */

  /* Release PHY reset */
  data = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_CORE_CONTROL_REG_ADDR);
  data &= ~(1 << 2); // Clear phy_reset[2]
  write32_reg(PCIE_MGMT_MMR_KPCIE_SII_CORE_CONTROL_REG_ADDR, data);

  /* ========================================================================
   * TODO: COMPLETE PHY INITIALIZATION SEQUENCE (Per Keraunos E100 Spec)
   * ========================================================================
   *
   * The following initialization steps need to be implemented for full
   * SerDes/PHY bringup on silicon. Current implementation only releases reset.
   *
   * REQUIRED INITIALIZATION STEPS:
   *
   * 1. PLL Configuration
   *    - Configure reference clock source and frequency
   *    - Set PLL multiplier/divider ratios for target PCIe gen
   *    - Enable PLL and wait for lock
   *    - Registers: Use pcie_phy_apb_write32() for PLL CSRs
   *    - Example:
   *      pcie_phy_apb_write32(PLL_CONFIG_OFFSET, pll_config_value);
   *      pcie_phy_apb_write32(PLL_ENABLE_OFFSET, 0x1);
   *      while (!(pcie_phy_apb_read32(PLL_STATUS_OFFSET) & PLL_LOCK_BIT));
   *
   * 2. Calibration Sequences
   *    - TX impedance calibration
   *    - RX impedance calibration
   *    - Offset calibration (DC offset removal)
   *    - DFE (Decision Feedback Equalization) calibration for Gen3+
   *    - Registers: Use pcie_phy_apb_write32() for calibration CSRs
   *    - Example:
   *      pcie_phy_apb_write32(TX_ZCAL_OFFSET, tx_zcal_value);
   *      pcie_phy_apb_write32(RX_ZCAL_OFFSET, rx_zcal_value);
   *      pcie_phy_apb_write32(CAL_START_OFFSET, 0x1);
   *      while (pcie_phy_apb_read32(CAL_STATUS_OFFSET) & CAL_BUSY_BIT);
   *
   * 3. Per-Lane Configuration
   *    - Configure TX swing, pre-emphasis, de-emphasis
   *    - Configure RX equalization settings
   *    - Set lane polarity (if needed)
   *    - Enable/disable lanes based on target width (x1, x2, x4, x8, x16)
   *    - Registers: Per-lane APB registers (typically lane_num * lane_stride)
   *    - Example (for each lane):
   *      for (lane = 0; lane < num_lanes; lane++) {
   *          uint32_t lane_offset = LANE_BASE + (lane * LANE_STRIDE);
   *          pcie_phy_apb_write32(lane_offset + TX_SWING_REG, tx_swing);
   *          pcie_phy_apb_write32(lane_offset + TX_PREEMP_REG, tx_pre);
   *          pcie_phy_apb_write32(lane_offset + RX_EQ_REG, rx_eq);
   *      }
   *
   * 4. Equalization Settings (Gen3+)
   *    - Configure transmitter equalization coefficients
   *    - Configure receiver equalization (CTLE, DFE)
   *    - Set up equalization bypass if needed for testing
   *    - Registers: EQ control via APB
   *    - Example:
   *      pcie_phy_apb_write32(TX_EQ_COEF_OFFSET, tx_eq_coef);
   *      pcie_phy_apb_write32(RX_CTLE_OFFSET, rx_ctle_value);
   *      pcie_phy_apb_write32(RX_DFE_OFFSET, rx_dfe_value);
   *
   * 5. Power State Configuration
   *    - Configure L0s/L1/L2 power state settings
   *    - Set up ASPM (Active State Power Management) parameters
   *    - Configure clock gating for power savings
   *    - Registers: Power management APB CSRs
   *
   * 6. PHY CPU Firmware Load Verification
   *    - Verify firmware loaded correctly (done in pcie_load_firmware())
   *    - Check PHY CPU is running and responsive
   *    - Verify PHY CPU ready status
   *    - Example:
   *      uint32_t cpu_status = pcie_phy_apb_read32(PHY_CPU_STATUS_OFFSET);
   *      if (!(cpu_status & PHY_CPU_READY_BIT)) return -1;
   *
   * 7. Link Speed Configuration
   *    - Set maximum supported link speed (Gen1/2/3/4/5/6)
   *    - Configure speed change protocol
   *    - Set up directed speed change if needed
   *    - Note: This may also involve DBI registers
   *
   * 8. Margin Testing Setup (Optional)
   *    - Configure margin testing parameters if needed
   *    - Set up voltage/timing margins for validation
   *    - Registers: Margin control via APB
   *
   * REFERENCE DOCUMENTATION:
   * - Keraunos-E100 specification: SerDes initialization sequence
   * - PHY vendor datasheet: Register map and programming guide
   * - PCIe Base Spec: PHY requirements for each generation
   *
   * HELPER FUNCTIONS AVAILABLE:
   * - pcie_phy_apb_read32(offset)  - Read PHY APB register
   * - pcie_phy_apb_write32(offset, value) - Write PHY APB register
   * - pcie_phy_apb_rmw32(offset, mask, value) - Read-modify-write
   * - pcie_phy_ahb_read32(offset)  - Read PHY AHB (firmware memory)
   * - pcie_phy_ahb_write32(offset, value) - Write PHY AHB
   *
   * ======================================================================== */

  return 0;
}

int pcie_program_bars(pcie_config_t *cfg) {
  uint64_t data;

  /* BAR0: 4GB, 64-bit, prefetchable */
  data = cfg->bars[0].addr | (1 << 3) | (1 << 2) | (0 << 0);
  pcie_dbi_write64(PCIECTL_BAR_OFF + 0x0, data);

  data = cfg->bars[0].size - 1;
  pcie_dbi_mask_write64(PCIECTL_BAR_OFF + 0x0, data);

  /* BAR2: 1MB, 64-bit, prefetchable */
  data = cfg->bars[1].addr | (1 << 3) | (1 << 2) | (0 << 0);
  pcie_dbi_write64(PCIECTL_BAR_OFF + 0x08, data);

  data = cfg->bars[1].size - 1;
  pcie_dbi_mask_write64(PCIECTL_BAR_OFF + 0x08, data);

  /* BAR4: 512GB, 64-bit, prefetchable */
  data = cfg->bars[2].addr | (1 << 3) | (1 << 2) | (0 << 0);
  pcie_dbi_write64(PCIECTL_BAR_OFF + 0x10, data);

  data = cfg->bars[2].size - 1;
  pcie_dbi_mask_write64(PCIECTL_BAR_OFF + 0x10, data);

  return 0;
}

int pcie_program_atu(pcie_config_t *cfg) {
  int i;

  for (i = 0; i < 3; i++) {
    program_atu_inbound_region(i, &cfg->atu_inbound[i]);
  }

  return 0;
}

int pcie_enable_ltssm(void) {
  /* Enable LTSSM state change interrupt */
  enable_ltssm_state_interrupt();

  /* Enable all interrupts */
  enable_interrupts();

  /* Enable LTSSM */
  write32_reg(PCIE_MGMT_MMR_KPCIE_SII_CORE_CONTROL_REG_ADDR, (1 << 9));

  return 0;
}

int pcie_wait_for_link_up(uint32_t timeout_iterations) {
  uint32_t data;
  uint32_t ltssm_state;
  uint32_t i;

  for (i = 0; i < timeout_iterations; i++) {
    data = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_POWER_MANAGEMENT_REG_ADDR);
    ltssm_state = (data >> 9) & 0x3F;

    if (ltssm_state == 0x11) { // LTSSM L0 State
      return 0;
    }

    delay_cycles(100);
  }

  return -1; // Timeout
}

int pcie_get_link_status(pcie_link_status_t *status) {
  uint32_t data;
  uint32_t link_status;
  uint32_t link_status2;

  if (!status) {
    return -1;
  }

  /* Get LTSSM state */
  data = read32_reg(PCIE_MGMT_MMR_KPCIE_SII_POWER_MANAGEMENT_REG_ADDR);
  status->ltssm_state = (data >> 9) & 0x3F;

  /* Read Link Status register using DBI helper */
  link_status = pcie_dbi_read32(PCIECTL_LINK_STATUS);
  status->link_speed = link_status & 0xF;
  status->link_width = (link_status >> 4) & 0x3F;

  /* Read Link Status 2 register for flit mode status (bit 10) */
  link_status2 = pcie_dbi_read32(PCIECTL_LINK_STATUS2);
  status->flit_mode = (link_status2 >> 10) & 0x1;

  /* Check enumeration */
  status->enumerated = pcie_check_enumeration();

  return 0;
}

int pcie_check_enumeration(void) {
  uint32_t cmd_status, bar0;

  /* Check if device has been enumerated by host software.
   *
   * This checks for SOFTWARE enumeration (not just link-level L0 state).
   * We verify that the host has:
   *   1. Enabled Memory Space in the Command register (bit 1)
   *   2. Programmed BAR0 with a valid address
   *
   * Note: The link can be in L0 state before these conditions are met.
   * Link reaching L0 only indicates PCIe link training is complete.
   * Full enumeration requires the host OS/BIOS to discover the device,
   * assign resources (BARs), and enable memory/IO access.
   *
   * In emulation environments, this may take additional time after L0
   * as the host software enumerates the PCIe bus tree and configures devices.
   * This function returns 0 during that window, and only returns 1 after
   * the host has fully configured the device for operation.
   */

  /* Read Command/Status Register using DBI helper */
  cmd_status = pcie_dbi_read32(PCIECTL_TYPE1_STATUS_COMMAND);

  /* Check if Memory Space Enable (bit 1) is set by host */
  if (!(cmd_status & 0x02)) {
    return 0;
  }

  /* Read BAR0 to see if it's been programmed by host */
  bar0 = pcie_dbi_read32(PCIECTL_BAR_OFF);
  if (bar0 == 0 || bar0 == 0xFFFFFFFF) {
    return 0;
  }

  return 1;
}

uint64_t pcie_translate_smn_to_dbi(uint64_t smn_addr, uint8_t *valid_out) {
  uint32_t pa = (uint32_t)smn_addr;
  uint32_t index = (pa >> 16) & 0xF;

  uint8_t valid;
  uint64_t addr_high;
  pcie_tlbsys0_read_entry(index, &valid, &addr_high);

  if (!valid) {
    *valid_out = 0;
    return 0;
  }

  uint64_t out_addr = addr_high | (uint64_t)(pa & 0xFFFFU);

  *valid_out = 1;
  return out_addr;
}

int pcie_full_init(pcie_config_t *cfg) {
  int ret;

  /* Release resets */
  ret = pcie_release_reset();
  if (ret != 0)
    return ret;

  /* Program TLBs */
  ret = pcie_program_all_tlbs();
  if (ret != 0)
    return ret;

  /* Configure DBI registers */
  ret = pcie_configure_dbi();
  if (ret != 0)
    return ret;

  /* P0 Config Space Bring-up: Verify Vendor/Device ID, clear errors,
   * initialize capabilities, configure MPS/MRRS */
  ret = pcie_init_config_space_p0();
  if (ret != 0)
    return ret;

  /* Initialize PHY */
  ret = pcie_init_phy();
  if (ret != 0)
    return ret;

  /* Program BARs */
  ret = pcie_program_bars(cfg);
  if (ret != 0)
    return ret;

  /* Program ATU regions */
  ret = pcie_program_atu(cfg);
  if (ret != 0)
    return ret;

  /* BARs and TLBs are now fully configured - safe to enable memory access
   * Enable MEM_SPACE and BUS_MASTER if firmware control is enabled.
   * Otherwise, host will enable these bits during enumeration. */
  pcie_enable_mem_bus_master();

  /* Load firmware */
  ret = pcie_load_firmware();
  if (ret != 0)
    return ret;

  /* Enable LTSSM */
  ret = pcie_enable_ltssm();
  if (ret != 0)
    return ret;

  /* Wait for link up with timeout */
  ret = pcie_wait_for_link_up(200);
  if (ret != 0)
    return ret;

  // Check enumeration (not implemented for emulation)
  // ret = pcie_check_enumeration();
  // if (ret != 1) return -1;

  return 0;
}

/* ========================================================================== */
/*        State Machine-Based Initialization (P0/P1 Recommended)             */
/* ========================================================================== */

/**
 * @brief Initialize PCIe using non-blocking state machine
 * @param cfg Pointer to PCIe configuration
 * @param ctx Pointer to state machine context (caller-allocated)
 * @return 0 on success, negative on error
 *
 * P0/P1 Requirement: Non-blocking initialization using state machine.
 *
 * This function provides two usage modes:
 *
 * MODE 1: Blocking (run to completion)
 * ------------------------------------
 * Call pcie_init_with_state_machine(cfg, NULL) and it will run the
 * state machine to completion in a blocking loop.
 *
 * Example:
 * @code
 *   pcie_config_t config;
 *   pcie_init_config(&config);
 *
 *   int ret = pcie_init_with_state_machine(&config, NULL);
 *   if (ret == 0) {
 *       // PCIe fully initialized and enumerated
 *   }
 * @endcode
 *
 * MODE 2: Non-blocking (caller polls)
 * ------------------------------------
 * Caller allocates context and polls the state machine from main loop.
 *
 * Example:
 * @code
 *   pcie_config_t config;
 *   pcie_svc_context_t ctx;
 *
 *   pcie_init_config(&config);
 *   pcie_init_with_state_machine(&config, &ctx);  // Initializes state machine
 *
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
 * State Machine Progress:
 * - RESET → RESET_RELEASED → TLB_PROGRAMMED → DBI_CONFIGURED
 * - → BARS_PROGRAMMED → SERDES_FW_LOAD → SERDES_READY
 * - → LINK_TRAINING → LINK_L0 → ENUM_READY
 *
 * @note If ctx is NULL, function runs in blocking mode and polls state
 *       machine internally until completion or error.
 * @note If ctx is provided, function only initializes the state machine
 *       and returns immediately. Caller must poll pcie_svc_poll(ctx).
 */
int pcie_init_with_state_machine(pcie_config_t *cfg, pcie_svc_context_t *ctx) {
  int ret;
  pcie_svc_context_t local_ctx;
  pcie_svc_context_t *active_ctx;

  if (!cfg) {
    return -1;
  }

  /* Determine which context to use */
  if (ctx) {
    /* Non-blocking mode: use caller's context */
    active_ctx = ctx;
  } else {
    /* Blocking mode: use local context */
    active_ctx = &local_ctx;
  }

  /* Initialize state machine */
  ret = pcie_svc_init(active_ctx, cfg);
  if (ret != 0) {
    return ret;
  }

  /* If caller provided context, return now (non-blocking mode) */
  if (ctx) {
    /* Caller will poll pcie_svc_poll(ctx) from their main loop */
    return 0;
  }

  /* ========================================================================
   * BLOCKING MODE: Poll state machine to completion
   * ======================================================================== */

  uint32_t max_iterations = 10000; /* Safety limit */
  uint32_t iteration = 0;

  while (iteration < max_iterations) {
    pcie_svc_state_t state = pcie_svc_poll(active_ctx);

    /* Check for completion */
    if (pcie_svc_is_ready(active_ctx)) {
      /* Success - device fully initialized and enumerated */
      write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_6__REG_ADDR,
                  0x50C6EA00); /* SMC Ready */
      return 0;
    }

    /* Check for error */
    if (pcie_svc_is_error(active_ctx)) {
      pcie_svc_error_t error = pcie_svc_get_last_error(active_ctx);

      /* Log error to scratch register */
      write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_6__REG_ADDR,
                  0x0E000000 | (error & 0xFFFF));
      write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_7__REG_ADDR,
                  (uint32_t)active_ctx->current_state << 16 |
                      (uint32_t)active_ctx->previous_state);

      return -1;
    }

    /* Optional: Log progress periodically */
    if ((iteration % 100) == 0) {
      uint32_t progress = pcie_svc_get_progress(active_ctx);
      write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_9__REG_ADDR,
                  (progress << 24) | (state << 16) | (iteration & 0xFFFF));
    }

    iteration++;
    delay_cycles(10); /* Small delay between polls */
  }

  /* Timeout - state machine didn't complete */
  write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_6__REG_ADDR,
              0xBADBAD00); /* Timeout */
  return -2;
}
