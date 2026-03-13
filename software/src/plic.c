/*
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * PLIC driver implementation for VP bringup.
 * Uses the PLIC base address (0xC4000000) via guarded register accessors.
 */

#include "plic.h"

#define PLIC_BASE_ADDR (0xC4000000)

void set_interrupt_priority(uint32_t interrupt_id, uint32_t priority) {
  write32_reg(PLIC_BASE_ADDR + 4 * interrupt_id, priority);
}

void set_interrupt_threshold(uint32_t core_id, uint32_t threshold) {
  write32_reg(PLIC_BASE_ADDR + 0x00200000 + core_id * 0x1000, threshold);
}

void enable_interrupt_meip(uint32_t core_id, uint32_t interrupt_id) {
  uint32_t reg_offset = (interrupt_id / 32) * 4;
  uint32_t bit_offset = interrupt_id % 32;
  uint64_t addr = PLIC_BASE_ADDR + 0x00002000 + core_id * 0x80 + reg_offset;
  uint32_t val = read32_reg(addr);
  write32_reg(addr, val | (1u << bit_offset));
}

void disable_interrupt_meip(uint32_t core_id, uint32_t interrupt_id) {
  uint32_t reg_offset = (interrupt_id / 32) * 4;
  uint32_t bit_offset = interrupt_id % 32;
  uint64_t addr = PLIC_BASE_ADDR + 0x00002000 + core_id * 0x80 + reg_offset;
  uint32_t val = read32_reg(addr);
  write32_reg(addr, val & ~(1u << bit_offset));
}

void enable_interrupt_seip(uint32_t core_id, uint32_t interrupt_id) {
  uint32_t reg_offset = (interrupt_id / 32) * 4;
  uint32_t bit_offset = interrupt_id % 32;
  uint64_t addr = PLIC_BASE_ADDR + 0x00002200 + core_id * 0x80 + reg_offset;
  uint32_t val = read32_reg(addr);
  write32_reg(addr, val | (1u << bit_offset));
}

uint32_t claim_interrupt_meip(uint32_t core_id) {
  return read32_reg(PLIC_BASE_ADDR + 0x00200004 + core_id * 0x1000);
}

uint32_t claim_interrupt_seip(uint32_t core_id) {
  return read32_reg(PLIC_BASE_ADDR + 0x00204004 + core_id * 0x1000);
}

void complete_interrupt_meip(uint32_t core_id, uint32_t interrupt_id) {
  write32_reg(PLIC_BASE_ADDR + 0x00200004 + core_id * 0x1000, interrupt_id);
}

void complete_interrupt_seip(uint32_t core_id, uint32_t interrupt_id) {
  write32_reg(PLIC_BASE_ADDR + 0x00204004 + core_id * 0x1000, interrupt_id);
}

uint32_t read_pending_interrupt(uint32_t interrupt_id) {
  uint32_t reg_offset = (interrupt_id / 32) * 4;
  uint32_t bit_offset = interrupt_id % 32;
  uint32_t val = read32_reg(PLIC_BASE_ADDR + 0x00001000 + reg_offset);
  return (val >> bit_offset) & 1;
}

void reset_interrupt_registers(void) {
  for (uint32_t i = 1; i <= 193; ++i)
    write32_reg(PLIC_BASE_ADDR + 4 * i, 0);
  for (uint32_t core = 0; core < 4; ++core)
    for (uint32_t en = 0; en < 7; ++en)
      write32_reg(PLIC_BASE_ADDR + 0x00002000 + core * 0x80 + en * 4, 0);
  for (uint32_t core = 0; core < 4; ++core)
    write32_reg(PLIC_BASE_ADDR + 0x00200000 + core * 0x1000, 1);
}
