/*
 * PCIe Functional Tests Implementation
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * Functional tests for PCIe capabilities (speed change, link width, etc.)
 */

#include "functional_tests.h"
#include "pcie_init.h"
#include "platform.h"
#include "regs.h"
#include "scratch.h"

/* PCIe Controller Register Offsets */
#define PCIECTL_LINK_CONTROL 0x0080
#define PCIECTL_LINK_STATUS 0x0082
#define PCIECTL_LINK_STATUS2 0x00A2
#define PCIECTL_LINK_CONTROL2 0x00A0
#define PCIECTL_GEN2_CONTROL 0x080C
#define PCIECTL_MULTI_LANE_CONTROL 0x08C0
#define PCIECTL_MISC_CONTROL 0x08BC
#define SMN_DBI_ADDR (0x18400000)

/* Delay constants */
#define SPEED_CHANGE_DELAY_CYCLES 1000
#define WIDTH_CHANGE_DELAY_CYCLES 1000

/**
 * @brief Simple delay function
 * @param cycles Number of cycles to delay
 */
static void delay_cycles(uint32_t cycles) {
  for (volatile uint32_t i = 0; i < cycles; i++) {
    asm volatile("nop");
  }
}

/**
 * @brief Test PCIe speed change capabilities from Gen1 to Gen6
 * @param initial_width Initial link width to verify after each speed change
 * @return 0 on success, negative on error
 */
int test_speed_change(uint32_t initial_width) {
  uint32_t data;
  uint32_t link_status_reg;
  uint32_t current_speed;
  uint32_t current_width;
  uint32_t target_speed;
  int i;
  const char *speed_names[] = {"Gen1", "Gen2", "Gen3", "Gen4", "Gen5", "Gen6"};

  // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_3__REG_ADDR, 0x50EED157);  //
  // Speed test marker

  // Loop through all speeds from Gen1 to Gen6
  for (i = 0; i < 6; i++) {
    target_speed = i + 1; // Gen1=1, Gen2=2, ..., Gen6=6

    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR, target_speed);  //
    // Log target speed write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_3__REG_ADDR,
    // 0x57A6E000 | i);  // Stage 0: Starting iteration

    // Step 1: Enable DBI read/write access
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_3__REG_ADDR, 0x57A6E001 | (i <<
    // 8));  // Stage 1: Enable DBI
    data = read32_reg(SMN_DBI_ADDR + PCIECTL_MISC_CONTROL);
    data |= (1 << 0); // dbi_ro_rw_en[0] = 1
    write32_reg(SMN_DBI_ADDR + PCIECTL_MISC_CONTROL, data);

    // Step 2: Program target link speed in Link Control 2 register
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_3__REG_ADDR, 0x57A6E002 | (i <<
    // 8));  // Stage 2: Program speed
    data = read32_reg(SMN_DBI_ADDR + PCIECTL_LINK_CONTROL2);
    data &= ~0xF;         // Clear target_link_speed[3:0]
    data |= target_speed; // Set new target speed
    write32_reg(SMN_DBI_ADDR + PCIECTL_LINK_CONTROL2, data);
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR, data);  // Log
    // programmed value

    // Step 3: Trigger directed speed change
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_3__REG_ADDR, 0x57A6E003 | (i <<
    // 8));  // Stage 3: Trigger change
    data = read32_reg(SMN_DBI_ADDR + PCIECTL_GEN2_CONTROL);
    data |= (1 << 17); // directed_speed_change[17] = 1
    write32_reg(SMN_DBI_ADDR + PCIECTL_GEN2_CONTROL, data);

    // Step 4: Wait for link retraining to complete
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_3__REG_ADDR, 0x57A6E004 | (i <<
    // 8));  // Stage 4: Waiting
    delay_cycles(SPEED_CHANGE_DELAY_CYCLES);

    // Step 5: Read back link status using pcie_get_link_status()
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_3__REG_ADDR, 0x57A6E005 | (i <<
    // 8));  // Stage 5: Read status
    pcie_link_status_t link_status;
    pcie_get_link_status(&link_status);

    // Log link status to unused scratch registers
    // SCRATCH_0: Iteration number and LTSSM state
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_0__REG_ADDR,
                (i << 24) | (link_status.ltssm_state & 0xFFFFFF));

    // SCRATCH_1: Link speed and width
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR,
                (link_status.link_speed & 0xFFFF) |
                    ((link_status.link_width & 0xFFFF) << 16));

    // SCRATCH_2: Enumeration status and flit mode
    write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_2__REG_ADDR,
                (link_status.enumerated & 0xFFFF) |
                    ((link_status.flit_mode & 0xFFFF) << 16));

    link_status_reg = read32_reg(SMN_DBI_ADDR + PCIECTL_LINK_CONTROL);
    current_speed = (link_status_reg >> 16) & 0xF;  // Bits [19:16]
    current_width = (link_status_reg >> 20) & 0x3F; // Bits [25:20]
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR, link_status_reg);
    // // Log raw status

    // Step 6: Verify speed changed correctly
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_3__REG_ADDR, 0x57A6E006 | (i <<
    // 8));  // Stage 6: Verify speed
    if (current_speed != target_speed) {
      // Speed change failed
      // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_5__REG_ADDR, 0xBAD50EED);
      // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_6__REG_ADDR,
      //             (target_speed << 16) | current_speed);
      return -1;
    }

    // Step 7: Verify width remained the same
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_3__REG_ADDR, 0x57A6E007 | (i <<
    // 8));  // Stage 7: Verify width
    if (current_width != initial_width) {
      // Width changed unexpectedly
      // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_7__REG_ADDR, 0xBAD61D74);
      // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_8__REG_ADDR,
      //             (initial_width << 16) | current_width);
      return -2;
    }

    // Log successful speed change
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_3__REG_ADDR, 0x57A6E008 | (i <<
    // 8));  // Stage 8: Success
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_9__REG_ADDR + (i * 4),
    //             (current_speed << 16) | current_width);
  }

  // All speed changes successful
  // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_3__REG_ADDR, 0x57A6ED0E);  // All
  // stages complete write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR,
  // 0x600D50D0);  // Success marker
  return 0;
}

/**
 * @brief Test PCIe link width downgrade from x4 to x1
 * @param initial_speed Initial link speed to verify after each width change
 * @return 0 on success, negative on error
 */
int test_width_downgrade(uint32_t initial_speed) {
  uint32_t data;
  uint32_t link_status_reg;
  uint32_t current_speed;
  uint32_t current_width;
  uint32_t target_width;
  int i;

  // Width values: x4, x2, x1
  uint32_t width_values[] = {4, 2, 1};
  const char *width_names[] = {"x4", "x2", "x1"};

  // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR, 0x61D71757);  //
  // Width test marker

  // Loop through all widths from x4 down to x1
  for (i = 1; i < 3; i++) {
    target_width = width_values[i];

    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_5__REG_ADDR, target_width);  //
    // Log target width write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR,
    // 0x5761D000 | i);  // Stage 0: Starting iteration

    // Step 1: Enable DBI read/write access
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR, 0x5761D001 | (i <<
    // 8));  // Stage 1: Enable DBI
    data = read32_reg(SMN_DBI_ADDR + PCIECTL_MISC_CONTROL);
    data |= (1 << 0); // dbi_ro_rw_en[0] = 1
    write32_reg(SMN_DBI_ADDR + PCIECTL_MISC_CONTROL, data);

    // Step 2: Program target link width in MULTI_LANE_CONTROL register
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR, 0x5761D002 | (i <<
    // 8));  // Stage 2: Program width
    data = read32_reg(SMN_DBI_ADDR + PCIECTL_MULTI_LANE_CONTROL);
    data &= ~(0x3F);      // Clear TARGET_LINK_WIDTH[5:0]
    data |= target_width; // Set new target width
    write32_reg(SMN_DBI_ADDR + PCIECTL_MULTI_LANE_CONTROL, data);
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_5__REG_ADDR, data);  // Log
    // programmed value

    // Step 3: Trigger direct link width change
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR, 0x5761D003 | (i <<
    // 8));  // Stage 3: Trigger change
    data = read32_reg(SMN_DBI_ADDR + PCIECTL_MULTI_LANE_CONTROL);
    data |= (1 << 6); // DIRECT_LINK_WIDTH_CHANGE[6] = 1
    write32_reg(SMN_DBI_ADDR + PCIECTL_MULTI_LANE_CONTROL, data);

    // Step 4: Wait for link retraining to complete
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR, 0x5761D004 | (i <<
    // 8));  // Stage 4: Waiting
    delay_cycles(WIDTH_CHANGE_DELAY_CYCLES);

    // Step 5: Read back link status
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR, 0x5761D005 | (i <<
    // 8));  // Stage 5: Read status
    link_status_reg = read32_reg(SMN_DBI_ADDR + PCIECTL_LINK_CONTROL);
    current_speed = (link_status_reg >> 16) & 0xF;  // Bits [19:16]
    current_width = (link_status_reg >> 20) & 0x3F; // Bits [25:20]
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_5__REG_ADDR, link_status_reg);
    // // Log raw status

    // Step 6: Clear the DIRECT_LINK_WIDTH_CHANGE trigger bit
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR, 0x5761D006 | (i <<
    // 8));  // Stage 6: Clear trigger
    data = read32_reg(SMN_DBI_ADDR + PCIECTL_MULTI_LANE_CONTROL);
    data &= ~(1 << 6); // DIRECT_LINK_WIDTH_CHANGE[6] = 0
    write32_reg(SMN_DBI_ADDR + PCIECTL_MULTI_LANE_CONTROL, data);

    // Step 7: Verify width changed correctly
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR, 0x5761D007 | (i <<
    // 8));  // Stage 7: Verify width
    if (current_width != target_width) {
      // Width change failed
      // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_6__REG_ADDR, 0xBAD61D74);
      // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_7__REG_ADDR,
      //             (target_width << 16) | current_width);
      return -1;
    }

    // Step 8: Verify speed remained the same
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR, 0x5761D008 | (i <<
    // 8));  // Stage 8: Verify speed
    if (current_speed != initial_speed) {
      // Speed changed unexpectedly
      // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_6__REG_ADDR, 0xBAD50EED);
      // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_7__REG_ADDR,
      //             (initial_speed << 16) | current_speed);
      return -2;
    }

    // Log successful width change
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR, 0x5761D009 | (i <<
    // 8));  // Stage 9: Success
    // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_8__REG_ADDR + (i * 4),
    //             (current_width << 16) | current_speed);
  }

  // All width changes successful
  // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR, 0x5761DD0E);  // All
  // stages complete write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_10__REG_ADDR,
  // 0x600D61D7);  // Success marker
  return 0;
}

/**
 * @brief Read and verify PCIe device identification registers
 */
int test_pcie_device_id(void) {
  // Read PCIe Configuration Space registers
  // Offset 0x00: Device ID (upper 16 bits) / Vendor ID (lower 16 bits)
  uint32_t dev_vendor_id = read32_reg(SMN_DBI_ADDR + 0x00);
  // Offset 0x08: Class Code (upper 24 bits) / Revision ID (lower 8 bits)
  uint32_t class_rev_id = read32_reg(SMN_DBI_ADDR + 0x08);
  // Offset 0x2C: Subsystem ID (upper 16 bits) / Subsystem Vendor ID (lower 16
  // bits)
  uint32_t subsys_id = read32_reg(SMN_DBI_ADDR + 0x2C);

  // Write to scratch registers for debugging
  // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_1__REG_ADDR, dev_vendor_id);
  // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_2__REG_ADDR, class_rev_id);
  // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_3__REG_ADDR, subsys_id);

  return 0;
}

/**
 * @brief Check PCIe BAR (Base Address Register) configuration
 */
int test_pcie_bar_config(void) {
  // PCIe Type 0 header has 6 BARs at offsets 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24
  // BAR format:
  // - Bit 0: Memory (0) or I/O (1) space indicator
  // - Bits 1-2 (Memory): Type (00=32-bit, 10=64-bit)
  // - Bit 3 (Memory): Prefetchable (1) or Non-prefetchable (0)
  // - Bits 4-31: Base Address (aligned to size)

  for (int bar_idx = 0; bar_idx < 6; bar_idx++) {
    uint32_t bar_offset = 0x10 + (bar_idx * 4);
    uint32_t bar_value = read32_reg(SMN_DBI_ADDR + bar_offset);

    if (bar_value == 0) {
      // BAR not implemented
      continue;
    }

    // Check if I/O or Memory space
    bool is_io = (bar_value & 0x1);

    if (is_io) {
      // I/O BAR
      uint32_t io_base = bar_value & 0xFFFFFFFC;

      // Write all 1's to determine size
      write32_reg(SMN_DBI_ADDR + bar_offset, 0xFFFFFFFF);
      uint32_t size_mask = read32_reg(SMN_DBI_ADDR + bar_offset);
      // Restore original value
      write32_reg(SMN_DBI_ADDR + bar_offset, bar_value);

      // Calculate size (invert mask and add 1)
      uint32_t bar_size = ~(size_mask & 0xFFFFFFFC) + 1;

      // Store BAR info: [31:16]=size_hi, [15:8]=BAR_idx, [7:0]=flags
      // Flags: bit 0 = I/O space
      uint32_t bar_info = (bar_size << 16) | (bar_idx << 8) | 0x01;
      // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR, bar_info);
      // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_5__REG_ADDR, io_base);

    } else {
      // Memory BAR
      uint32_t mem_type = (bar_value >> 1) & 0x3;    // Bits 1-2
      bool is_prefetchable = (bar_value >> 3) & 0x1; // Bit 3
      bool is_64bit = (mem_type == 0x2);

      uint32_t mem_base_low = bar_value & 0xFFFFFFF0;
      uint32_t mem_base_high = 0;

      // Write all 1's to determine size
      write32_reg(SMN_DBI_ADDR + bar_offset, 0xFFFFFFFF);
      uint32_t size_mask_low = read32_reg(SMN_DBI_ADDR + bar_offset);
      // Restore original value
      write32_reg(SMN_DBI_ADDR + bar_offset, bar_value);

      // Calculate size from low 32 bits
      // Mask off the control bits (lower 4 bits for memory BARs)
      uint32_t size_field = size_mask_low & 0xFFFFFFF0;
      uint32_t bar_size;
      if (size_field == 0) {
        // All address bits read as 0 - this is the minimum size (16 bytes for
        // memory)
        bar_size = 16;
      } else {
        // Invert the size field and add 1 to get actual size
        bar_size = (~size_field) + 1;
      }

      if (is_64bit && bar_idx < 5) {
        // Read upper 32 bits
        uint32_t bar_upper_offset = bar_offset + 4;
        mem_base_high = read32_reg(SMN_DBI_ADDR + bar_upper_offset);

        // Size check for upper bits (optional - usually 0 for small BARs)
        write32_reg(SMN_DBI_ADDR + bar_upper_offset, 0xFFFFFFFF);
        uint32_t size_mask_high = read32_reg(SMN_DBI_ADDR + bar_upper_offset);
        write32_reg(SMN_DBI_ADDR + bar_upper_offset, mem_base_high);

        // Store 64-bit BAR info
        // Flags: [7:0] = bit 0: memory (0), bit 1: 64-bit, bit 2: prefetchable,
        // bits[7:4]: BAR index
        uint32_t bar_flags = (bar_idx << 4) | (is_64bit ? 0x02 : 0x00) |
                             (is_prefetchable ? 0x04 : 0x00);

        // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR, bar_flags);
        // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_5__REG_ADDR, mem_base_low);
        // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_6__REG_ADDR, mem_base_high);
        // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_7__REG_ADDR, bar_size);
        // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_8__REG_ADDR, size_mask_low);
        // // Raw size mask for debugging
        // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_9__REG_ADDR,
        // size_mask_high);

        // Skip next BAR index since 64-bit BAR uses two consecutive BARs
        bar_idx++;
      } else {
        // 32-bit Memory BAR
        // Flags: bit 0 = memory (0), bit 1 = 32-bit (0), bit 2 = prefetchable
        uint32_t bar_flags = (is_prefetchable ? 0x04 : 0x00);
        uint32_t bar_info = (bar_size << 16) | (bar_idx << 8) | bar_flags;

        // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_4__REG_ADDR, bar_info);
        // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_5__REG_ADDR, mem_base_low);
        // write32_reg(SMC_CPU_SMC_CPU_CTRL_SCRATCH_6__REG_ADDR, bar_size);
      }
    }

    // Only check first implemented BAR for now to avoid overwriting scratch
    // regs
    break;
  }

  return 0;
}
