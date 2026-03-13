/*
 * PCIe End-to-End Transaction Test
 *
 * Tests the full data path:
 *   Host CPU -> RC (DBI/AXI_Slave) -> EP -> PCIE_Tile
 *
 * Topology: RC directly connected to EP (no PCIeSwitch).
 *   Bus 0: RC (Root Complex)
 *   Bus 1: EP (Endpoint, directly on RC downstream port)
 *
 * Memory map (Host_Chiplet):
 *   0x44000000  PCIe_RC DBI       (4 MB)
 *   0x44300000  PCIe_RC ATU       (via DBI CS2, 128 KB)
 *   0x70000000  PCIe_RC AXI_Slave (256 MB, config/mem window)
 *   0xC000A000  UART              (256 B)
 *   0x80000000  DRAM              (64 MB)
 *
 * Data path:
 *   RC.PCIMem ---> PCIe_EP.PCIMem_Slave
 *   PCIe_EP.BusMaster ---> PCIE_TILE.pcie_controller_target
 *   PCIe_EP.AXI_Slave  <--- PCIE_TILE.pcie_controller_initiator
 */

#include <stdio.h>
#include <stdint.h>

static volatile int trap_fault;
static volatile unsigned long trap_mcause;
static volatile unsigned long trap_mepc;
static volatile unsigned long trap_mtval;

__attribute__((interrupt("machine"), aligned(4)))
void trap_handler(void)
{
    asm volatile("csrr %0, mcause" : "=r"(trap_mcause));
    asm volatile("csrr %0, mepc"   : "=r"(trap_mepc));
    asm volatile("csrr %0, mtval"  : "=r"(trap_mtval));
    trap_fault = 1;
    trap_mepc += 4;
    asm volatile("csrw mepc, %0" :: "r"(trap_mepc));
}

static void install_trap_handler(void)
{
    unsigned long addr = (unsigned long)&trap_handler;
    asm volatile("csrw mtvec, %0" :: "r"(addr));
}

static inline uint32_t mmio_read32(uint64_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

static inline void mmio_write32(uint64_t addr, uint32_t val)
{
    *(volatile uint32_t *)(uintptr_t)addr = val;
}

static uint32_t safe_read32(uint64_t addr)
{
    trap_fault = 0;
    uint32_t val = mmio_read32(addr);
    if (trap_fault)
        return 0xDEAD0000 | (trap_mcause & 0xFFFF);
    return val;
}

static int safe_write32(uint64_t addr, uint32_t val)
{
    trap_fault = 0;
    mmio_write32(addr, val);
    return trap_fault ? -1 : 0;
}

static void delay(volatile int n)
{
    for (volatile int i = 0; i < n; i++)
        ;
}

#define RC_DBI_BASE     0x44000000UL
#define RC_ATU_BASE     0x44300000UL
#define RC_AXI_BASE     0x70000000UL

/*
 * Register offsets use VDK register view names from:
 *   /Keraunos_PCIE_Tile/Host_Chiplet/Misc/PCIE_RC/Registers/
 *
 * Fn0PCIConfigHeaderRegGrp_5_xx  (Type 1 Config Header, 0x00-0x3C)
 * Fn0PCIePCIeCapRegGrp_5_xx     (PCIe Capability, at CAP_PTR)
 * PF0_PORT_LOGIC                 (Port Logic, 0x700+)
 * PF0_ATU_CAP_OUTBOUND           (iATU Outbound, 0x300000+)
 */

/* Fn0PCIConfigHeaderRegGrp_5_xx register offsets */
#define CFG_DEVICE_VENDOR_ID    0x00   /* Device_Vendor_ID */
#define CFG_COMMAND_STATUS      0x04   /* Command_Status */
#define CFG_CLASSCODE_REVID     0x08   /* ClassCode_RevisionID */
#define CFG_CLHB                0x0C   /* CLHB (CacheLineSize/HeaderType/BIST) */
#define CFG_BAR0                0x10   /* BAR0 */
#define CFG_BAR1                0x14   /* BAR1 */
#define CFG_BUS_NUMBERS         0x18   /* BusNumbers */
#define CFG_IO_CONFIG_STATUS    0x1C   /* IO_Config_Status */
#define CFG_MEMORY_BASE_LIMIT   0x20   /* Memory_Base_Limit */
#define CFG_CAPABILITY_PTR      0x34   /* Capability_Ptr */
#define CFG_INT_BRIDGE_CTRL     0x3C   /* Int_Bridge_Ctrl */

/* Fn0PCIePCIeCapRegGrp_5_xx offsets (relative to CAP_PTR) */
#define PCIECAP_PCIECAP         0x00   /* PCIeCap */
#define PCIECAP_DEVICECAPS      0x04   /* DeviceCaps */
#define PCIECAP_DEVCTRLSTATUS   0x08   /* DeviceCtrlStatus */
#define PCIECAP_LINKCAP         0x0C   /* LinkCap */
#define PCIECAP_LINKCTRLSTATUS  0x10   /* LinkCtrlStatus */

/* PF0_PORT_LOGIC register offsets */
#define PL_ACK_LATENCY_TIMER_OFF  0x700  /* ACK_LATENCY_TIMER_OFF */
#define PL_PORT_LINK_CTRL_OFF     0x710  /* PORT_LINK_CTRL_OFF */
#define PL_LANE_SKEW_OFF          0x714  /* LANE_SKEW_OFF */
#define PL_GEN2_CTRL_OFF          0x80C  /* GEN2_CTRL_OFF */
#define PL_PHY_STATUS_OFF         0x810  /* PHY_STATUS_OFF */
#define PL_PHY_CONTROL_OFF        0x814  /* PHY_CONTROL_OFF */
#define PL_MISC_CONTROL_1_OFF     0x8BC  /* MISC_CONTROL_1_OFF */

/* ATU register offsets (from ATU base = DBI + 0x300000) */
#define ATU_REGION_CTRL_1       0x00
#define ATU_REGION_CTRL_2       0x04
#define ATU_LWR_BASE_ADDR       0x08
#define ATU_UPPER_BASE_ADDR     0x0C
#define ATU_LIMIT_ADDR          0x10
#define ATU_LWR_TARGET_ADDR     0x14
#define ATU_UPPER_TARGET_ADDR   0x18
#define ATU_REGION_STRIDE       0x200

/* ATU TLP types */
#define ATU_TYPE_MEM            0x0
#define ATU_TYPE_IO             0x2
#define ATU_TYPE_CFG0           0x4
#define ATU_TYPE_CFG1           0x5

/* ATU CTRL2 bits */
#define ATU_ENABLE              (1U << 31)
#define ATU_BAR_MODE            (1U << 28)

/* PCI Capability IDs */
#define PCI_CAP_ID_PM       0x01
#define PCI_CAP_ID_MSI      0x05
#define PCI_CAP_ID_PCIE     0x10

static int pass_count;
static int fail_count;
static int skip_count;

static void test_result(const char *name, int passed, const char *detail)
{
    if (passed > 0) {
        printf("  [PASS] %s", name);
        pass_count++;
    } else if (passed == 0) {
        printf("  [FAIL] %s", name);
        fail_count++;
    } else {
        printf("  [SKIP] %s", name);
        skip_count++;
    }
    if (detail)
        printf(" -- %s", detail);
    printf("\n");
}

static void print_reg(const char *name, uint64_t addr)
{
    uint32_t val = safe_read32(addr);
    if (trap_fault)
        printf("    %-28s @ 0x%08x => FAULT (mcause=0x%lx)\n",
               name, (unsigned)addr, trap_mcause);
    else
        printf("    %-28s @ 0x%08x => 0x%08x\n",
               name, (unsigned)addr, val);
}

static uint32_t find_pci_cap(uint64_t cfg_base, uint8_t cap_id)
{
    uint32_t cap_ptr_val = safe_read32(cfg_base + CFG_CAPABILITY_PTR) & 0xFF;
    int walk = 0;
    printf("    Walking capability list from 0x%02x:\n", cap_ptr_val);
    while (cap_ptr_val >= 0x40 && cap_ptr_val <= 0xFC && walk < 20) {
        uint32_t cap_hdr = safe_read32(cfg_base + cap_ptr_val);
        uint8_t id   = cap_hdr & 0xFF;
        uint8_t next = (cap_hdr >> 8) & 0xFF;
        printf("      [0x%02x] Cap ID=0x%02x Next=0x%02x\n",
               cap_ptr_val, id, next);
        if (id == cap_id)
            return cap_ptr_val;
        cap_ptr_val = next;
        walk++;
    }
    return 0;
}

static void program_atu_region(int region, uint32_t type,
                               uint64_t base, uint64_t limit,
                               uint64_t target, uint32_t extra_ctrl2)
{
    uint64_t atu = RC_ATU_BASE + (region * ATU_REGION_STRIDE);
    mmio_write32(atu + ATU_REGION_CTRL_1, type);
    mmio_write32(atu + ATU_LWR_BASE_ADDR, (uint32_t)base);
    mmio_write32(atu + ATU_UPPER_BASE_ADDR, (uint32_t)(base >> 32));
    mmio_write32(atu + ATU_LIMIT_ADDR, (uint32_t)limit);
    mmio_write32(atu + ATU_LWR_TARGET_ADDR, (uint32_t)target);
    mmio_write32(atu + ATU_UPPER_TARGET_ADDR, (uint32_t)(target >> 32));
    mmio_write32(atu + ATU_REGION_CTRL_2, ATU_ENABLE | extra_ctrl2);
}

int main(void)
{
    delay(2000000);

    install_trap_handler();

    printf("\n\n");
    printf("==============================================================\n");
    printf("  PCIe End-to-End Transaction Test\n");
    printf("  Path: Host CPU -> RC -> EP -> PCIE_Tile\n");
    printf("==============================================================\n\n");

    pass_count = fail_count = skip_count = 0;

    /* ================================================================
     * TEST 1: RC DBI Access - Port Logic Registers
     * Verifies: Host CPU -> RC DBI path
     * These registers are known to return correct values.
     * ================================================================ */
    printf("--- TEST 1: RC DBI Register Dump ---\n");
    printf("  VDK path: PCIE_RC/Registers/...\n\n");

    printf("  [Fn0PCIConfigHeaderRegGrp_5_xx]\n");
    print_reg("Device_Vendor_ID",      RC_DBI_BASE + CFG_DEVICE_VENDOR_ID);
    print_reg("Command_Status",        RC_DBI_BASE + CFG_COMMAND_STATUS);
    print_reg("ClassCode_RevisionID",  RC_DBI_BASE + CFG_CLASSCODE_REVID);
    print_reg("CLHB",                  RC_DBI_BASE + CFG_CLHB);
    print_reg("BusNumbers",            RC_DBI_BASE + CFG_BUS_NUMBERS);
    print_reg("IO_Config_Status",      RC_DBI_BASE + CFG_IO_CONFIG_STATUS);
    print_reg("Memory_Base_Limit",     RC_DBI_BASE + CFG_MEMORY_BASE_LIMIT);
    print_reg("Capability_Ptr",        RC_DBI_BASE + CFG_CAPABILITY_PTR);
    print_reg("Int_Bridge_Ctrl",       RC_DBI_BASE + CFG_INT_BRIDGE_CTRL);

    printf("\n  [PF0_PORT_LOGIC]\n");
    print_reg("ACK_LATENCY_TIMER_OFF", RC_DBI_BASE + PL_ACK_LATENCY_TIMER_OFF);
    print_reg("PORT_LINK_CTRL_OFF",    RC_DBI_BASE + PL_PORT_LINK_CTRL_OFF);
    print_reg("GEN2_CTRL_OFF",         RC_DBI_BASE + PL_GEN2_CTRL_OFF);
    print_reg("PHY_STATUS_OFF",        RC_DBI_BASE + PL_PHY_STATUS_OFF);
    print_reg("PHY_CONTROL_OFF",       RC_DBI_BASE + PL_PHY_CONTROL_OFF);
    print_reg("MISC_CONTROL_1_OFF",    RC_DBI_BASE + PL_MISC_CONTROL_1_OFF);

    uint32_t ack_freq = safe_read32(RC_DBI_BASE + PL_ACK_LATENCY_TIMER_OFF);
    test_result("DBI read ACK_LATENCY_TIMER_OFF",
                !trap_fault && ack_freq != 0, NULL);

    /* ================================================================
     * TEST 2: RC DBI Write + Readback (Bus Numbers)
     * Verifies: DBI write path works for certain registers.
     * ================================================================ */
    printf("\n--- TEST 2: DBI Write + Readback (BusNumbers) ---\n");
    printf("  Register: Fn0PCIConfigHeaderRegGrp_5_xx/BusNumbers\n");
    printf("  RC: Primary=0x00 Secondary=0x01 Subordinate=0x01\n\n");

    /* Primary=0 (RC itself), Secondary=1 (EP bus), Subordinate=1 (no bridges below EP) */
    mmio_write32(RC_DBI_BASE + CFG_BUS_NUMBERS, 0x00010100);
    uint32_t bus_num = safe_read32(RC_DBI_BASE + CFG_BUS_NUMBERS);
    printf("    wrote 0x00010100, read 0x%08x\n", bus_num);
    test_result("BusNumbers write+readback", bus_num == 0x00010100, NULL);

    /* ================================================================
     * RC Command_Status: enable Memory Space Enable (bit1) + Bus Master (bit2)
     * DWC PCIe treats Command.MSE as read-only unless DBI_RO_WR_EN (bit 0
     * of MISC_CONTROL_1_OFF at 0x8BC) is set first.
     * pcie_bringup does the same for the EP side.
     * ================================================================ */
    printf("\n--- RC Command_Status: enabling MSE + Bus Master ---\n");
    /* Step 1: enable DBI_RO_WR_EN so read-only config fields become writable */
    uint32_t misc_ctrl = safe_read32(RC_DBI_BASE + PL_MISC_CONTROL_1_OFF);
    mmio_write32(RC_DBI_BASE + PL_MISC_CONTROL_1_OFF, misc_ctrl | 0x1);
    printf("  MISC_CONTROL_1_OFF: 0x%08x -> 0x%08x (DBI_RO_WR_EN=%d)\n",
           misc_ctrl, misc_ctrl | 1, 1);
    /* Step 2: write MSE + Bus Master */
    uint32_t cmd_old = safe_read32(RC_DBI_BASE + CFG_COMMAND_STATUS);
    mmio_write32(RC_DBI_BASE + CFG_COMMAND_STATUS, cmd_old | 0x00000006);
    uint32_t cmd_new = safe_read32(RC_DBI_BASE + CFG_COMMAND_STATUS);
    printf("  wrote 0x%08x -> read 0x%08x (MSE=%d BME=%d)\n",
           cmd_old | 0x6, cmd_new, (cmd_new >> 1) & 1, (cmd_new >> 2) & 1);
    /* Step 3: restore DBI_RO_WR_EN=0 */
    mmio_write32(RC_DBI_BASE + PL_MISC_CONTROL_1_OFF, misc_ctrl);

    /* ================================================================
     * TEST 3: LTSSM State Monitoring
     * The RC's app_ltssm_en signal must be driven high for LTSSM to
     * start. On the EP side, this is done by pcie_bringup firmware
     * writing to PCIE_TILE CORE_CONTROL. On the RC side, app_ltssm_en
     * must be connected in the .vdksys topology.
     *
     * We poll both PHY_STATUS (0x810, Port Logic debug) and also the
     * PCIe Capability Link Status register (CAP_PTR + 0x12) for the
     * Data Link Layer Link Active (DLLLA) bit.
     *
     * Wait up to ~30s sim time for EP bringup to complete.
     * ================================================================ */
    printf("\n--- TEST 3: PCIe Link Status (LTSSM + Capabilities) ---\n");
    printf("  Walking RC capability list to find PCIe Cap (ID=0x10)...\n");

    uint32_t pcie_cap_off = find_pci_cap(RC_DBI_BASE, PCI_CAP_ID_PCIE);

    int link_up = 0;
    uint32_t ltssm_state = 0;

    if (pcie_cap_off) {
        printf("\n  [Fn0PCIePCIeCapRegGrp_5_xx] (at 0x%02x)\n", pcie_cap_off);
        print_reg("PCIeCap",          RC_DBI_BASE + pcie_cap_off + PCIECAP_PCIECAP);
        print_reg("DeviceCaps",       RC_DBI_BASE + pcie_cap_off + PCIECAP_DEVICECAPS);
        print_reg("DeviceCtrlStatus", RC_DBI_BASE + pcie_cap_off + PCIECAP_DEVCTRLSTATUS);
        print_reg("LinkCap",          RC_DBI_BASE + pcie_cap_off + PCIECAP_LINKCAP);
        print_reg("LinkCtrlStatus",   RC_DBI_BASE + pcie_cap_off + PCIECAP_LINKCTRLSTATUS);
        test_result("Found PCIe Capability (Cap ID 0x10)", 1, NULL);
    } else {
        printf("    PCIe Capability NOT found in linked list!\n");
        test_result("Found PCIe Capability (Cap ID 0x10)", 0,
                    "not found - cap list may be broken");
    }

    printf("\n  Polling PHY_STATUS_OFF for LTSSM state (5 attempts)...\n");
    for (int attempt = 0; attempt < 5; attempt++) {
        delay(5000000);

        uint32_t phy_debug = safe_read32(RC_DBI_BASE + PL_PHY_STATUS_OFF);
        ltssm_state = (phy_debug >> 0) & 0x3F;

        uint32_t link_status = 0;
        uint32_t dllla = 0;
        if (pcie_cap_off) {
            link_status = safe_read32(RC_DBI_BASE + pcie_cap_off +
                                       PCIECAP_LINKCTRLSTATUS);
            dllla = (link_status >> 29) & 1;
        }

        link_up = (ltssm_state == 0x11) || dllla;

        printf("    Poll %d: PHY_STATUS_OFF=0x%08x LTSSM=0x%02x"
               " LinkCtrlStatus=0x%08x DLLLA=%d (%s)\n",
               attempt, phy_debug, ltssm_state,
               link_status, dllla,
               link_up ? "LINK UP" :
               ltssm_state == 0x00 ? "DETECT_QUIET" :
               ltssm_state == 0x01 ? "DETECT_ACTIVE" :
               ltssm_state == 0x02 ? "POLLING_ACTIVE" :
               "other");

        if (link_up)
            break;
    }

    if (!link_up) {
        test_result("PCIe link trained (LTSSM=L0)", 0,
                    "link did not train");
        printf("    ROOT CAUSE: RC app_ltssm_en is NOT connected in\n");
        printf("    .vdksys. LTSSM stays in DETECT_QUIET.\n");
        printf("    FIX: Connect PCIE_RC.app_ltssm_en to constant-1.\n");
        printf("    NOTE: VDK may still pass transactions over PCIe wire\n");
        printf("    even without link training (VP model behavior).\n");
    } else {
        test_result("PCIe link trained (LTSSM=L0)", 1, "link is up");
    }


    /* ================================================================
     * TEST 4: ATU Programming
     * Verifies: DBI writes to ATU registers (via CS2 space).
     * Programs two ATU regions:
     *   Region 0: CfgRd0 to Bus 1, Dev 0 (PCIeSwitch DSP or EP)
     *   Region 1: MemRd/MemWr to address 0x01000000 (4 KB window)
     * ================================================================ */
    printf("\n--- TEST 4: ATU Programming ---\n");
    printf("  Registers: PF0_ATU_CAP_OUTBOUND/IATU_*_OFF_OUTBOUND\n\n");

    uint64_t cfg_window_base  = RC_AXI_BASE;
    uint64_t cfg_window_limit = RC_AXI_BASE + 0xFFF;
    uint64_t cfg_target       = (1UL << 24);

    uint64_t mem_window_base  = RC_AXI_BASE + 0x00100000;
    uint64_t mem_window_limit = RC_AXI_BASE + 0x00100FFF;
    /* mem_target will be set from EP BAR0 after EP is found in TEST 5.
     * Default 0x00000000 since pcie_bringup programs EP BAR0 to base=0. */
    uint64_t mem_target       = 0x00000000UL;

    int atu_ok = 1;

    if (safe_write32(RC_ATU_BASE + ATU_REGION_CTRL_1, ATU_TYPE_CFG0) < 0)
        atu_ok = 0;
    if (safe_write32(RC_ATU_BASE + ATU_LWR_BASE_ADDR, (uint32_t)cfg_window_base) < 0)
        atu_ok = 0;

    if (atu_ok) {
        program_atu_region(0, ATU_TYPE_CFG0,
                           cfg_window_base, cfg_window_limit,
                           cfg_target, 0);
        printf("    ATU Region 0: CfgRd0 -> Bus 1 Dev 0\n");
        printf("      Base=0x%08x Limit=0x%08x Target=0x%08x\n",
               (unsigned)cfg_window_base, (unsigned)cfg_window_limit,
               (unsigned)cfg_target);

        program_atu_region(1, ATU_TYPE_MEM,
                           mem_window_base, mem_window_limit,
                           mem_target, 0);
        printf("    ATU Region 1: MemRd/MemWr\n");
        printf("      Base=0x%08x Limit=0x%08x Target=0x%08x\n",
               (unsigned)mem_window_base, (unsigned)mem_window_limit,
               (unsigned)mem_target);
    }

    test_result("ATU region programming", atu_ok, NULL);

    if (atu_ok) {
        printf("\n  ATU readback (PF0_ATU_CAP_OUTBOUND):\n");
        for (int r = 0; r < 2; r++) {
            uint64_t atu = RC_ATU_BASE + (r * ATU_REGION_STRIDE);
            printf("    [Region %d: IATU_*_OFF_OUTBOUND%s]\n",
                   r, r == 0 ? "" : "_1");
            print_reg("IATU_REGION_CTRL_1_OFF_OUTBOUND", atu + ATU_REGION_CTRL_1);
            print_reg("IATU_REGION_CTRL_2_OFF_OUTBOUND", atu + ATU_REGION_CTRL_2);
            print_reg("IATU_LWR_BASE_ADDR_OFF_OUTBOUND", atu + ATU_LWR_BASE_ADDR);
            print_reg("IATU_LIMIT_ADDR_OFF_OUTBOUND",    atu + ATU_LIMIT_ADDR);
            print_reg("IATU_LWR_TARGET_ADDR_OFF_OUTBOUND", atu + ATU_LWR_TARGET_ADDR);
        }
    }

    /* ================================================================
     * TEST 5: Config Read via ATU (CfgRd0 to Bus 1 Dev 0)
     * Verifies: Host CPU -> RC AXI_Slave -> ATU -> PCIe Wire -> EP
     * EP is directly connected (no switch). ATU issues a Type 0 TLP.
     * ================================================================ */
    printf("\n--- TEST 5: Config Read via ATU (EP on Bus 1 Dev 0) ---\n");
    printf("  Path: Host CPU -> RC AXI_Slave -> ATU -> PCIe Wire -> EP\n\n");

    int ep_found = 0;
    if (!atu_ok) {
        test_result("CfgRd0 to EP (Bus 1 Dev 0)", -1, "ATU not programmed");
    } else {
        uint32_t cfg_id = safe_read32(cfg_window_base);

        if (trap_fault) {
            printf("    FAULT mcause=0x%lx (transaction blocked)\n", trap_mcause);
            test_result("CfgRd0 to EP (Bus 1 Dev 0)", 0, "FAULT");
        } else if (cfg_id == 0xFFFFFFFF) {
            test_result("CfgRd0 to EP (Bus 1 Dev 0)", 0,
                        "returned 0xFFFFFFFF (no device / UR)");
        } else if (cfg_id == 0x00000000) {
            test_result("CfgRd0 to EP (Bus 1 Dev 0)", 0,
                        "returned 0x00000000 (link down / UR mapped to OKAY)");
        } else {
            printf("    EP found! Vendor=0x%04x Device=0x%04x\n",
                   cfg_id & 0xFFFF, (cfg_id >> 16) & 0xFFFF);
            test_result("CfgRd0 to EP (Bus 1 Dev 0)", 1, "EP responded");
            ep_found = 1;

            printf("    Reading EP config space (Bus 1)...\n");
            print_reg("  EP Device_Vendor_ID",     cfg_window_base + 0x00);
            print_reg("  EP Command_Status",       cfg_window_base + 0x04);
            print_reg("  EP ClassCode_RevisionID", cfg_window_base + 0x08);
            print_reg("  EP CLHB",                 cfg_window_base + 0x0C);
            uint32_t ep_bar0_raw = safe_read32(cfg_window_base + 0x10);
            uint32_t ep_bar1_raw = safe_read32(cfg_window_base + 0x14);
            print_reg("  EP BAR0",                 cfg_window_base + 0x10);
            print_reg("  EP BAR1",                 cfg_window_base + 0x14);
            print_reg("  EP Capability_Ptr",       cfg_window_base + 0x34);

            /* Derive ATU memory target from EP BAR0 (set by pcie_bringup via DBI).
             * BAR0 bits[3:0] are type flags; base address is in bits[31:4].
             * For 64-bit BAR (bits[2:1]=10), BAR1 holds upper 32 bits.
             * DWC PCIe model only allows BAR programming via DBI (not from RC
             * config space), so we use whatever pcie_bringup set. */
            uint32_t bar0_type = ep_bar0_raw & 0xF;
            uint32_t bar0_base_lo = ep_bar0_raw & ~0xF;
            uint32_t bar0_base_hi = ((bar0_type >> 1) & 0x3) == 2 ? ep_bar1_raw : 0;
            mem_target = ((uint64_t)bar0_base_hi << 32) | bar0_base_lo;
            printf("    EP BAR0 base = 0x%08x%08x (%s%s)\n",
                   bar0_base_hi, bar0_base_lo,
                   (bar0_type & 1) ? "IO" : "MEM",
                   ((bar0_type >> 1) & 3) == 2 ? ", 64-bit" : ", 32-bit");
            printf("    ATU Region 1 target set to EP BAR0 base = 0x%08x\n",
                   (unsigned)mem_target);
        }
    }

    /* ================================================================
     * TEST 6: Configure EP for Memory Transactions
     * Enable Mem Space + Bus Master in EP Command register.
     * Program EP BAR0 to 0x01000000 so ATU Region 1 MemRd/MemWr
     * targets map into PCIE_TILE address space.
     * No switch to configure: EP is directly on Bus 1.
     * ================================================================ */
    printf("\n--- TEST 6: Configure EP (Bus 1) for Memory Access ---\n");
    printf("  Enable EP Command[MemSpace+BusMaster], program BAR0\n\n");

    if (!atu_ok || !ep_found) {
        test_result("EP Command_Status enable", -1,
                    ep_found ? "ATU not programmed" : "EP not found on Bus 1");
        test_result("EP BAR0 program", -1, "skipped");
    } else {
        /* Restore ATU Region 0 to CfgRd0 -> Bus 1 in case TEST 5 left it modified */
        program_atu_region(0, ATU_TYPE_CFG0,
                           cfg_window_base, cfg_window_limit,
                           cfg_target, 0);

        printf("    Step 1: Enable EP Mem Space + Bus Master...\n");
        uint32_t ep_cmd = safe_read32(cfg_window_base + CFG_COMMAND_STATUS);
        safe_write32(cfg_window_base + CFG_COMMAND_STATUS, ep_cmd | 0x06);
        ep_cmd = safe_read32(cfg_window_base + CFG_COMMAND_STATUS);
        printf("    EP Command_Status = 0x%08x\n", ep_cmd);
        test_result("EP Command_Status enable",
                    (ep_cmd & 0x06) == 0x06, NULL);

        /* NOTE: DWC PCIe model does NOT allow BAR base address programming
         * via PCIe config space from the RC side. BAR base is set by the
         * EP's own DBI (done by pcie_bringup firmware). BAR size mask bits
         * are fixed by the model parameter BAR0_MASK.
         * We accept whatever base pcie_bringup set and use it as ATU target. */
        uint32_t ep_bar0_cur = safe_read32(cfg_window_base + CFG_BAR0);
        uint32_t ep_bar1_cur = safe_read32(cfg_window_base + 0x14);
        uint32_t bar_base_lo = ep_bar0_cur & ~0xF;
        uint32_t bar_base_hi = (((ep_bar0_cur >> 1) & 3) == 2) ? ep_bar1_cur : 0;
        uint64_t ep_bar_base = ((uint64_t)bar_base_hi << 32) | bar_base_lo;
        /* BAR0_MASK=0xFFFFFF -> 16 MB BAR; size = ~mask+1 */
        uint32_t bar_mask = 0xFFFFFF;
        uint32_t bar_size = ~bar_mask + 1;
        printf("    EP BAR0 = 0x%08x (base=0x%08x, size=%u MB)\n",
               ep_bar0_cur, (unsigned)ep_bar_base, bar_size >> 20);
        printf("    ATU Region 1 target = 0x%08x (matches EP BAR0 base)\n",
               (unsigned)mem_target);
        test_result("EP BAR0 confirmed",
                    ep_bar0_cur != 0xFFFFFFFF ? 1 : 0,
                    ep_bar0_cur == 0xFFFFFFFF ? "EP not responding" : NULL);
    }

    /* ================================================================
     * TEST 7: Memory Transaction via ATU
     * Sends a MemRd through the ATU targeting address 0x01000000.
     * If ATU + link are working, this should reach the EP's BAR
     * and ultimately the PCIE_TILE.
     * ================================================================ */
    printf("\n--- TEST 7: Memory Transaction via ATU ---\n");
    printf("  Path: Host -> RC AXI_Slave -> ATU -> PCIe Wire -> EP -> PCIE_TILE\n\n");

    if (!atu_ok) {
        test_result("MemRd via ATU", -1, "ATU not programmed");
    } else {
        program_atu_region(1, ATU_TYPE_MEM,
                           mem_window_base, mem_window_limit,
                           mem_target, 0);
        /* Re-program ATU Region 1 with the dynamically derived EP BAR0 base */
        program_atu_region(1, ATU_TYPE_MEM,
                           mem_window_base, mem_window_limit,
                           mem_target, 0);
        printf("    ATU Region 1: MemRd -> EP BAR0 target 0x%08x\n",
               (unsigned)mem_target);

        uint32_t mem_val = safe_read32(mem_window_base);

        if (trap_fault) {
            printf("    FAULT mcause=0x%lx\n", trap_mcause);
            test_result("MemRd via ATU to EP BAR0", 0, "FAULT");
        } else if (mem_val == 0xFFFFFFFF) {
            printf("    returned 0xFFFFFFFF (UR)\n");
            printf("    EP BAR0 base=0x%08x; EP may not forward to PCIE_TILE,\n",
                   (unsigned)mem_target);
            printf("    or PCIE_TILE has no memory at offset 0x0.\n");
            test_result("MemRd via ATU to EP BAR0", 0, "UR (0xFFFFFFFF)");
        } else {
            printf("    returned 0x%08x (data from EP/PCIE_TILE)\n", mem_val);
            test_result("MemRd via ATU to EP BAR0", 1, NULL);
        }

        printf("    Attempting MemWr to same address...\n");
        int wr_ok = safe_write32(mem_window_base, 0xCAFEBEEF);
        test_result("MemWr via ATU to EP BAR0",
                    wr_ok == 0 ? 1 : 0,
                    wr_ok == 0 ? "write completed (no fault)" : "write faulted");

        if (wr_ok == 0) {
            uint32_t rb = safe_read32(mem_window_base);
            printf("    wrote 0xCAFEBEEF, read back 0x%08x\n", rb);
            test_result("MemWr+MemRd roundtrip",
                        rb == 0xCAFEBEEF ? 1 : 0,
                        rb == 0xFFFFFFFF ? "UR from PCIE_TILE (no mapped memory)" : NULL);
        }
    }

    /* ================================================================
     * TEST 8: Multiple ATU Memory Windows
     * Programs additional ATU regions to test different address ranges
     * that would map to different parts of the PCIE_TILE address space.
     * ================================================================ */
    printf("\n--- TEST 8: Multi-region ATU Memory Access ---\n");
    printf("  Tests multiple address windows into PCIE_TILE\n\n");

    if (!atu_ok || !ep_found) {
        test_result("Multi-region MemRd", -1,
                    !atu_ok ? "ATU not programmed" :
                    "EP not enumerated, skipping");
    } else {
        uint64_t targets[] = { 0x00000000UL, 0x01000000UL, 0x02000000UL };
        const char *names[] = { "TILE offset 0x0", "TILE offset 0x1000000",
                                "TILE offset 0x2000000" };
        for (int i = 0; i < 3; i++) {
            uint64_t win_base  = RC_AXI_BASE + 0x200000 + i * 0x1000;
            uint64_t win_limit = win_base + 0xFFF;
            program_atu_region(2 + i, ATU_TYPE_MEM,
                               win_base, win_limit, targets[i], 0);
            uint32_t rd = safe_read32(win_base);
            printf("    target=0x%08x data=0x%08x\n",
                   (unsigned)targets[i], rd);
            test_result(names[i], !trap_fault ? 1 : 0, NULL);
        }
    }

    /* ================================================================
     * SUMMARY
     * ================================================================ */
    printf("\n==============================================================\n");
    printf("  End-to-End Test Summary\n");
    printf("  PASS: %d   FAIL: %d   SKIP: %d\n", pass_count, fail_count, skip_count);
    printf("==============================================================\n\n");

    printf("  Topology: RC (Bus 0) -> EP (Bus 1), no switch.\n");
    printf("  Diagnostic Notes:\n");
    printf("  - If LTSSM stuck in DETECT_QUIET:\n");
    printf("    RC app_ltssm_en is NOT connected in .vdksys.\n");
    printf("    FIX: Connect PCIE_RC.app_ltssm_en to constant-1.\n");
    printf("    NOTE: VDK VP models may still route TLPs without LTSSM.\n");
    printf("  - If EP not found (TEST 5 fails):\n");
    printf("    Check RC<->EP PCIe wire connection in .vdksys.\n");
    printf("  - If MemRd returns 0xFFFFFFFF (TEST 7 fails):\n");
    printf("    EP BAR0 may not match ATU target 0x01000000.\n");
    printf("    Check EP model's default BAR0 address in .vdksys.\n");
    printf("  - Check simout.txt for:\n");
    printf("    * '[ATU][OutBound]' - ATU translation occurred\n");
    printf("    * '[PCIe_Wire] Sending' - TLP generated\n");
    printf("    * 'handle_write_IATU' - ATU register programming logged\n");
    printf("\n");

    while (1)
        asm volatile("wfi");

    return 0;
}
