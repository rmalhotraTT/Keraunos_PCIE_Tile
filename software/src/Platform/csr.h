/* RISC-V CSR (Control and Status Register) Utilities
 * Copyright (c) 2025 Tenstorrent
 * SPDX-License-Identifier: Apache-2.0
 *
 * Common utilities for accessing RISC-V Control and Status Registers (CSRs).
 * This header provides macros for reading and writing CSRs in a consistent way
 * across all platforms and applications.
 *
 * Usage:
 *   unsigned long hart_id = read_csr(mhartid);
 *   write_csr(mstatus, new_value);
 *
 * Common CSRs:
 *   mhartid   - Machine Hart ID (read-only)
 *   mstatus   - Machine Status Register
 *   mie       - Machine Interrupt Enable
 *   mip       - Machine Interrupt Pending
 *   mtvec     - Machine Trap Vector
 *   mepc      - Machine Exception Program Counter
 *   mcause    - Machine Trap Cause
 *   mtval     - Machine Trap Value
 */

#ifndef CSR_H
#define CSR_H

/*==============================================================================
 * CSR READ/WRITE MACROS
 *============================================================================*/

/**
 * Read a RISC-V CSR
 * @param reg: CSR name (e.g., mhartid, mstatus)
 * @return: Value of the CSR
 */
#define read_csr(reg)                                                          \
  ({                                                                           \
    unsigned long __tmp;                                                       \
    asm volatile("csrr %0, " #reg : "=r"(__tmp));                              \
    __tmp;                                                                     \
  })

/**
 * Write to a RISC-V CSR
 * @param reg: CSR name (e.g., mstatus, mtvec)
 * @param val: Value to write
 */
#define write_csr(reg, val) ({ asm volatile("csrw " #reg ", %0" ::"rK"(val)); })

/**
 * Set bits in a RISC-V CSR (atomic OR)
 * @param reg: CSR name
 * @param bit: Bits to set
 * @return: Original value before modification
 */
#define set_csr(reg, bit)                                                      \
  ({                                                                           \
    unsigned long __tmp;                                                       \
    asm volatile("csrrs %0, " #reg ", %1" : "=r"(__tmp) : "rK"(bit));          \
    __tmp;                                                                     \
  })

/**
 * Clear bits in a RISC-V CSR (atomic AND NOT)
 * @param reg: CSR name
 * @param bit: Bits to clear
 * @return: Original value before modification
 */
#define clear_csr(reg, bit)                                                    \
  ({                                                                           \
    unsigned long __tmp;                                                       \
    asm volatile("csrrc %0, " #reg ", %1" : "=r"(__tmp) : "rK"(bit));          \
    __tmp;                                                                     \
  })

/**
 * Read-modify-write a RISC-V CSR
 * @param reg: CSR name
 * @param val: New value
 * @return: Original value before modification
 */
#define swap_csr(reg, val)                                                     \
  ({                                                                           \
    unsigned long __tmp;                                                       \
    asm volatile("csrrw %0, " #reg ", %1" : "=r"(__tmp) : "rK"(val));          \
    __tmp;                                                                     \
  })

/*==============================================================================
 * COMMON CSR BIT DEFINITIONS
 *============================================================================*/

/* mstatus register bits */
#define MSTATUS_MIE 0x00000008UL  /* Machine Interrupt Enable */
#define MSTATUS_MPIE 0x00000080UL /* Machine Previous Interrupt Enable */
#define MSTATUS_MPP 0x00001800UL  /* Machine Previous Privilege */

/* mie register bits */
#define MIE_MSIE 0x00000008UL /* Machine Software Interrupt Enable */
#define MIE_MTIE 0x00000080UL /* Machine Timer Interrupt Enable */
#define MIE_MEIE 0x00000800UL /* Machine External Interrupt Enable */

/* mip register bits */
#define MIP_MSIP 0x00000008UL /* Machine Software Interrupt Pending */
#define MIP_MTIP 0x00000080UL /* Machine Timer Interrupt Pending */
#define MIP_MEIP 0x00000800UL /* Machine External Interrupt Pending */

#endif /* CSR_H */
