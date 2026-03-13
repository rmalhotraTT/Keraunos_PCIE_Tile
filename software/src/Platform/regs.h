#ifndef REGS_H
#define REGS_H

#include <stdint.h>

static inline void write32_reg(uint64_t addr, uint32_t value) {
  volatile uint32_t *p_addr = (volatile uint32_t *)(uintptr_t)addr;
  *p_addr = value;
}

static inline uint32_t read32_reg(uint64_t addr) {
  volatile uint32_t *p_addr = (volatile uint32_t *)(uintptr_t)addr;
  return *p_addr;
}

static inline void write64_reg(uint64_t addr, uint64_t value) {
  volatile uint64_t *p_addr = (volatile uint64_t *)(uintptr_t)addr;
  *p_addr = value;
}

static inline uint64_t read64_reg(uint64_t addr) {
  volatile uint64_t *p_addr = (volatile uint64_t *)(uintptr_t)addr;
  return *p_addr;
}

static inline void write16_reg(uint64_t addr, uint16_t value) {
  volatile uint16_t *p_addr = (volatile uint16_t *)(uintptr_t)addr;
  *p_addr = value;
}

static inline uint16_t read16_reg(uint64_t addr) {
  volatile uint16_t *p_addr = (volatile uint16_t *)(uintptr_t)addr;
  return *p_addr;
}

#endif // REGS_H
