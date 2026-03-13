/*
 * PCIe Interrupt Quick Reference
 * Copyright (c) 2025 Tenstorrent
 *
 * Quick reference for understanding the interrupt implementation
 */

/*******************************************************************************
 * INITIALIZATION SEQUENCE
 ******************************************************************************/

// In main.c, after pcie_full_init():
pcie_init_interrupts(); // This does everything needed to enable interrupts

// Internally, this calls:
//   1. pcie_setup_interrupt_vector()      - Sets mtvec CSR
//   2. pcie_enable_machine_interrupts()   - Sets MEIE in mie CSR
//   3. pcie_enable_global_interrupts()    - Sets MIE in mstatus CSR

/*******************************************************************************
 * WHAT HAPPENS WHEN AN INTERRUPT OCCURS
 ******************************************************************************/

// 1. PCIe hardware detects LTSSM state change
// 2. Interrupt status bit is set in KPCIE_SII_MISC_INT_STATUS1 register
// 3. CPU receives external interrupt signal
// 4. Hardware saves mepc and mcause, disables interrupts
// 5. CPU jumps to vector table: __metal_vector_table + (4 * cause)
// 6. Vector table calls: metal_external_interrupt_vector_handler()
// 7. Our handler: pcie_external_interrupt_handler() executes
// 8. Handler returns with mret, restores mepc and re-enables interrupts
// 9. Execution continues at WFI loop

/*******************************************************************************
 * SCRATCH REGISTER MAP
 ******************************************************************************/

// SCRATCH_10: Event markers
//   0xD0700E57 - Hot reset detected
//   0x11DDD06D - Link went down
//   0x110C0000 - Link came up
//   0x61D70A55 - Width test passed
//   0xDDDDDDDD - Init successful
//   0xEEEEEEEE - Init failed

// SCRATCH_11: LTSSM transitions
//   [7:0]   - Previous LTSSM state
//   [15:8]  - Current LTSSM state
//   [31:16] - Interrupt count

// SCRATCH_12: Link parameters (when link is up)
//   [7:0]   - Link speed (1-6 for Gen1-Gen6)
//   [15:8]  - Link width (1, 2, 4, 8, 16)

// SCRATCH_13: CSR interrupt status
//   Raw value from KPCIE_SII_INT_CSR_STATUS

// SCRATCH_14: Combined interrupt status
//   [15:0]  - CSR interrupt status
//   [31:16] - MISC interrupt status

// SCRATCH_15: Interrupt system status
//   0x107Exxxx where xxxx = total LTSSM interrupt count

/*******************************************************************************
 * KEY LTSSM STATES (see pcie_helpers.h for full list)
 ******************************************************************************/

#define LTSSM_DETECT_QUIET 0x00     // Initial state - looking for receiver
#define LTSSM_DETECT_ACT 0x01       // Actively detecting
#define LTSSM_POLL_ACTIVE 0x02      // Polling
#define LTSSM_CFG_LINKWD_START 0x07 // Configuring link width
#define LTSSM_CFG_COMPLETE 0x0B     // Configuration complete
#define LTSSM_L0 0x11               // Link up and active!
#define LTSSM_L0S 0x12              // Low power L0s
#define LTSSM_L1_IDLE 0x14          // Low power L1
#define LTSSM_HOT_RESET_ENTRY 0x1E  // Entering hot reset
#define LTSSM_HOT_RESET 0x1F        // Hot reset active

/*******************************************************************************
 * RISC-V CSR BITS USED
 ******************************************************************************/

// mstatus (Machine Status Register)
#define MSTATUS_MIE (1 << 3) // Machine Interrupt Enable (global)

// mie (Machine Interrupt Enable)
#define MIE_MEIE (1 << 11) // Machine External Interrupt Enable

// mtvec (Machine Trap Vector)
//   [XLEN-1:2] - Base address of vector table
//   [1:0]      - Mode: 0=Direct, 1=Vectored

/*******************************************************************************
 * HELPER FUNCTIONS AVAILABLE
 ******************************************************************************/

// From pcie_helpers.c:
pcie_is_link_up()                    // Returns 1 if in L0
    pcie_read_ltssm_state()          // Returns current LTSSM state (0-31)
    pcie_read_link_status(&status)   // Gets full status structure
    pcie_read_interrupt_status(...)  // Reads interrupt status registers
    pcie_clear_interrupt_status(...) // Clears interrupt status
    pcie_check_hot_reset()           // Returns 1 if hot reset detected
    pcie_monitor_state_change(&prev) // Detects state changes

    // From pcie_interrupt.c:
    pcie_init_interrupts()        // Complete interrupt setup
    pcie_get_interrupt_stats(...) // Get event counters
    pcie_reset_interrupt_stats()  // Reset all counters

    /*******************************************************************************
     * DEBUGGING TIPS
     ******************************************************************************/

    // 1. Check if interrupts are enabled:
    //    - Read SCRATCH_15, should be 0x107Exxxx with xxxx incrementing

    // 2. Read CSRs directly:
    //    uint64_t mtvec = read_csr(mtvec);    // Should point to vector table
    //    uint64_t mie = read_csr(mie);        // Should have bit 11 set
    //    uint64_t mstatus = read_csr(mstatus);// Should have bit 3 set

    // 3. Monitor state transitions:
    //    - SCRATCH_11 shows old state → new state

    // 4. Verify interrupt handler is being called:
    //    - SCRATCH_15 counter should increment on LTSSM changes

    // 5. Trigger an interrupt manually:
    //    - Change link speed: pcie_request_speed_change(PCIE_SPEED_GEN2)
    //    - This will cause LTSSM state changes and generate interrupts

    /*******************************************************************************
     * ADDING CUSTOM INTERRUPT HANDLING
     ******************************************************************************/

    // To add re-initialization on hot reset, modify pcie_interrupt.c:
    //
    // if (current_state == LTSSM_HOT_RESET || current_state ==
    // LTSSM_HOT_RESET_ENTRY) {
    //     g_hot_reset_count++;
    //
    //     // Wait for reset to complete
    //     while (pcie_read_ltssm_state() == LTSSM_HOT_RESET) {
    //         // Wait...
    //     }
    //
    //     // Re-initialize PCIe
    //     pcie_config_t cfg;
    //     pcie_init_config(&cfg);
    //     pcie_full_init(&cfg);
    // }

    /*******************************************************************************
     * PERFORMANCE NOTES
     ******************************************************************************/

    // - WFI instruction puts CPU in low-power mode
    // - CPU wakes instantly on interrupt (<10 cycles typically)
    // - Interrupt handler overhead: ~50-100 cycles
    // - PCIe state changes are infrequent (milliseconds apart)
    // - Overall power consumption is minimal compared to polling
