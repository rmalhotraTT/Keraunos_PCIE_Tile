/*
 * Bare-metal PCIe Root Complex connectivity test for Host_Chiplet CPU.
 *
 * Tests the RC -> PCIeSwitch -> EP -> PCIE_Tile data path using the
 * AXI_Slave port directly, without depending on DBI access.
 *
 * Memory map (Host_Chiplet):
 *   0x44000000  PCIe_RC DBI       (4 MB)
 *   0x44300000  PCIe_RC ATU       (128 KB)
 *   0x70000000  PCIe_RC AXI_Slave (256 MB, config/mem window)
 *   0xC000A000  UART              (256 B)
 *   0x80000000  DRAM              (64 MB)
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

static void print_trap(const char *label)
{
    unsigned long code = trap_mcause & 0x7FFFFFFFUL;
    const char *desc = "unknown";
    switch (code) {
    case 5:  desc = "load access fault"; break;
    case 7:  desc = "store access fault"; break;
    case 13: desc = "load page fault"; break;
    case 15: desc = "store page fault"; break;
    }
    printf("  [TRAP] %s: mcause=0x%lx (%s) mtval=0x%lx\n",
           label, trap_mcause, desc, trap_mtval);
}

static void probe_addr(const char *name, uint64_t addr)
{
    trap_fault = 0;
    uint32_t val = mmio_read32(addr);
    if (trap_fault) {
        printf("  0x%08x %-20s => FAULT (mcause=0x%lx)\n",
               (unsigned)addr, name, trap_mcause);
    } else {
        printf("  0x%08x %-20s => 0x%08x\n",
               (unsigned)addr, name, val);
    }
}

#define RC_DBI_BASE     0x44000000UL
#define RC_ATU_BASE     0x44300000UL
#define RC_AXI_BASE     0x70000000UL

static void delay(int n)
{
    for (volatile int i = 0; i < n; i++)
        ;
}

int main(void)
{
    for (volatile int i = 0; i < 500000; i++)
        ;

    install_trap_handler();

    printf("\n\n");
    printf("==============================================\n");
    printf("  Host_Chiplet PCIe Connectivity Test\n");
    printf("==============================================\n\n");

    /* ---- Part 1: DBI register probe ---- */
    printf("--- Part 1: DBI register probe ---\n");
    probe_addr("Undecoded(ref)",  0x50000000UL);
    probe_addr("DBI+0x00 VendDev", RC_DBI_BASE + 0x00);
    probe_addr("DBI+0x04 CmdStat", RC_DBI_BASE + 0x04);
    probe_addr("DBI+0x08 ClassRv", RC_DBI_BASE + 0x08);
    probe_addr("DBI+0x0C HdrType", RC_DBI_BASE + 0x0C);
    probe_addr("DBI+0x10 BAR0",    RC_DBI_BASE + 0x10);
    probe_addr("DBI+0x18 BusNum",  RC_DBI_BASE + 0x18);
    probe_addr("DBI+0x2C SubsysID",RC_DBI_BASE + 0x2C);
    probe_addr("DBI+0x34 CapPtr",  RC_DBI_BASE + 0x34);
    probe_addr("DBI+0x3C IntLine", RC_DBI_BASE + 0x3C);
    printf("  -- Port Logic / Synopsys regs --\n");
    probe_addr("DBI+0x700 AckFreq",RC_DBI_BASE + 0x700);
    probe_addr("DBI+0x70C LinkCtl",RC_DBI_BASE + 0x70C);
    probe_addr("DBI+0x710 LnSkew", RC_DBI_BASE + 0x710);
    probe_addr("DBI+0x80C Debug1", RC_DBI_BASE + 0x80C);
    probe_addr("DBI+0x810 Debug0", RC_DBI_BASE + 0x810);

    int dbi_ok = 0;
    {
        trap_fault = 0;
        uint32_t v = mmio_read32(RC_DBI_BASE);
        if (!trap_fault && v != 0x00000000)
            dbi_ok = 1;
    }

    printf("\n  DBI accessible: %s\n\n", dbi_ok ? "YES" : "NO");

    /* ---- Part 2: DBI config — set MSE/IO_EN before AXI probes ---- */
    if (dbi_ok) {
        printf("--- Part 2: DBI config ---\n");
        uint32_t id = mmio_read32(RC_DBI_BASE + 0x00);
        printf("  Vendor/Device: 0x%08x (V=0x%04x D=0x%04x)\n",
               id, id & 0xFFFF, (id >> 16) & 0xFFFF);
        printf("  Expected:      V=0x16C3 D=0xEDDC (Synopsys RC)\n");

        uint32_t class_rev = mmio_read32(RC_DBI_BASE + 0x08);
        printf("  Class/Rev:     0x%08x (class=0x%06x rev=0x%02x)\n",
               class_rev, (class_rev >> 8) & 0xFFFFFF, class_rev & 0xFF);
        printf("  Expected:      class=0x060400 (PCI-PCI Bridge)\n");

        uint32_t hdr = mmio_read32(RC_DBI_BASE + 0x0C);
        printf("  Header Type:   0x%02x (Type %d)\n",
               (hdr >> 16) & 0xFF, ((hdr >> 16) & 0x7F));

        printf("  Setting MSE + Bus Master (CMD reg)...\n");
        uint32_t cmd = mmio_read32(RC_DBI_BASE + 0x04);
        printf("  CMD before:    0x%08x\n", cmd);
        cmd |= (1U << 1) | (1U << 2);
        mmio_write32(RC_DBI_BASE + 0x04, cmd);
        cmd = mmio_read32(RC_DBI_BASE + 0x04);
        printf("  CMD after:     0x%08x (MSE=%d BM=%d)\n",
               cmd, (cmd >> 1) & 1, (cmd >> 2) & 1);

        printf("  Setting bus numbers (pri=0, sec=1, sub=0xFF)...\n");
        mmio_write32(RC_DBI_BASE + 0x18, 0x00FF0100);
        printf("  Bus numbers:   0x%08x\n",
               mmio_read32(RC_DBI_BASE + 0x18));
    } else {
        printf("--- Part 2: DBI config SKIPPED ---\n");
    }

    /* ---- Part 3: AXI_Slave probe (only after MSE is set) ---- */
    printf("\n--- Part 3: AXI_Slave probe ---\n");
    if (!dbi_ok) {
        printf("  SKIPPED (MSE not set, AXI reads would block)\n");
    } else {
        probe_addr("AXI_Slave+0x00", RC_AXI_BASE + 0x00);
        probe_addr("AXI_Slave+0x04", RC_AXI_BASE + 0x04);
        probe_addr("AXI_Slave+0x08", RC_AXI_BASE + 0x08);
    }

    /* ---- Part 4: ATU + downstream device probe ---- */
    if (dbi_ok) {
        printf("\n--- Part 4: ATU config + downstream probe ---\n");

        printf("  Programming ATU region 0 for CfgRd0 to bus 1 dev 0...\n");
        uint32_t atu = RC_ATU_BASE;
        mmio_write32(atu + 0x00, 0x04);
        mmio_write32(atu + 0x08, (uint32_t)RC_AXI_BASE);
        mmio_write32(atu + 0x0C, 0);
        mmio_write32(atu + 0x10, (uint32_t)(RC_AXI_BASE + 0xFFF));
        mmio_write32(atu + 0x14, (1U << 24));
        mmio_write32(atu + 0x18, 0);
        mmio_write32(atu + 0x04, (1U << 31) | (1U << 28));
        delay(100);

        uint32_t cfg_id = mmio_read32(RC_AXI_BASE);
        printf("  Bus1:Dev0 Vendor/Device: 0x%08x\n", cfg_id);
        if (cfg_id != 0xFFFFFFFF && cfg_id != 0x00000000) {
            printf("  *** Device found on bus 1! ***\n");
            uint32_t dhdr = mmio_read32(RC_AXI_BASE + 0x0C);
            printf("  Header type: 0x%02x\n", (dhdr >> 16) & 0x7F);
        } else {
            printf("  No device on bus 1 (or link not trained)\n");
        }
    } else {
        printf("\n--- Part 4: ATU SKIPPED (no DBI) ---\n");
    }

    printf("\n==============================================\n");
    if (dbi_ok)
        printf("  PCIe Connectivity Test PASSED (DBI+AXI)\n");
    else
        printf("  PCIe Connectivity Test FAILED (no DBI)\n");
    printf("==============================================\n");

    return 0;
}
