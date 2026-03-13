/*
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * Platform-Level Interrupt Controller (PLIC) driver header
 */

#ifndef PLIC_H
#define PLIC_H

#include "regs.h"
#include <stdint.h>

void set_interrupt_priority(uint32_t interrupt_id, uint32_t priority);
void set_interrupt_threshold(uint32_t core_id, uint32_t threshold);
void enable_interrupt_meip(uint32_t core_id, uint32_t interrupt_id);
void disable_interrupt_meip(uint32_t core_id, uint32_t interrupt_id);
void enable_interrupt_seip(uint32_t core_id, uint32_t interrupt_id);
uint32_t claim_interrupt_meip(uint32_t core_id);
uint32_t claim_interrupt_seip(uint32_t core_id);
void complete_interrupt_meip(uint32_t core_id, uint32_t interrupt_id);
void complete_interrupt_seip(uint32_t core_id, uint32_t interrupt_id);
uint32_t read_pending_interrupt(uint32_t interrupt_id);
void reset_interrupt_registers(void);

#endif // PLIC_H
