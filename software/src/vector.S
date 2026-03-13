/*
 * Boot entry + minimal interrupt vector table for VP bringup.
 *
 * With -nostartfiles, this replaces the toolchain's crt0.
 * _entry initialises SP, GP, mtvec, clears BSS, then calls main().
 */

  .section .text.entry, "ax"
  .globl _entry
  .globl _start
_entry:
_start:
  /* 1. Set stack pointer (la = auipc+addi, works with medany) */
  la      sp, __stack_top

  /* 2. Set global pointer */
  .option push
  .option norelax
  la      gp, __global_pointer$
  .option pop

  /* 3. Set mtvec so traps don't land at 0x0 */
  la      t0, __metal_vector_table
  csrw    mtvec, t0

  /* 4. Clear BSS */
  la      a0, __bss_start
  la      a1, __bss_end
  bgeu    a0, a1, 2f
1:
  sd      zero, 0(a0)
  addi    a0, a0, 8
  bltu    a0, a1, 1b
2:

  /* 5. Call main(0, 0, 0) */
  li      a0, 0
  li      a1, 0
  li      a2, 0
  jal     ra, main

  /* 6. After main returns, loop on WFI */
  li      a0, 0
3:
  wfi
  j       3b

/* ------------------------------------------------------------------ */

  .section .text.metal_vector_table, "ax"
  .balign 256
  .globl __metal_vector_table
__metal_vector_table:
  j _default_trap      /* exception (cause 0)  */
  j _default_trap      /* S-mode SW interrupt   */
  j _default_trap      /* reserved              */
  j _default_trap      /* M-mode SW interrupt   */
  j _default_trap      /* reserved              */
  j _default_trap      /* S-mode timer          */
  j _default_trap      /* reserved              */
  j _default_trap      /* M-mode timer          */
  j _default_trap      /* reserved              */
  j _default_trap      /* S-mode external       */
  j _default_trap      /* reserved              */
  j _default_trap      /* M-mode external (PLIC)*/
  j _default_trap
  j _default_trap
  j _default_trap
  j _default_trap

  .balign 4
  .globl _default_trap
  .weak  _default_trap
_default_trap:
  mret
