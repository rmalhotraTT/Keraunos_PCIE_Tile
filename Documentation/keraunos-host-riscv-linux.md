# Booting Linux on a RISC-V Host CPU to Drive Keraunos PCIe Endpoint

> **Target environment: Keraunos VDK (WIP)**
> This guide targets the Synopsys Virtualizer-based VDK at
> `/localdev/rmalhotra/Keraunos_PCIE_Tile`.
> No U-Boot, QEMU, or physical hardware is needed.
> OpenSBI is built from source and loaded as M-mode firmware via `fw_payload.elf`.

## System Architecture

### VDK Topology

```
┌─────────────────────────────────────────────────────────────────┐
│              BUILD MACHINE (x86_64)                             │
│  Cross-compiler: riscv64-unknown-linux-gnu-                     │
│  Produces: Image (kernel), initramfs.cpio.gz, DTB               │
└──────────────────────────────┬──────────────────────────────────┘
                               │ (load ELFs into VDK)
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│   VDK: Keraunos_PCIE_Tile  (Synopsys Virtualizer Elite)         │
│   Path: /localdev/rmalhotra/Keraunos_PCIE_Tile                   │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  Host_Chiplet  (PCIe Root Complex side)                 │   │
│  │                                                         │   │
│  │   TT_Rocket_LT RISC-V CPU  ← runs Linux                │   │
│  │     DATA/INSTRUCTION bus → SharedMemoryMap              │   │
│  │                                                         │   │
│  │   Memory map (CPU view):                                │   │
│  │     0x44000000  PCIE_RC DBI        (4 MB)               │   │
│  │     0x44300000  PCIE_RC ATU        (via DBI CS2)        │   │
│  │     0x70000000  PCIE_RC AXI_Slave  (256 MB, PCIe win)  │   │
│  │     0x80000000  DRAM               (1 GB)              │   │
│  │     0xC000A000  UART               (256 B)              │   │
│  │     0xC4000000  PLIC                                    │   │
│  │                                                         │   │
│  │   PCIE_RC ──────────── PCIe wire ──────────────────┐   │   │
│  └─────────────────────────────────────────────────────┼───┘   │
│                                                        │        │
│  ┌─────────────────────────────────────────────────────┼───┐   │
│  │  Keraunos_PCIE_Chiplet  (PCIe Endpoint side)        │   │   │
│  │                                                     │   │   │
│  │   TT_Rocket_LT RISC-V CPU  ← pcie_bringup firmware │   │   │
│  │   (SMC_Configure CPU — NOT the Linux host)          │   │   │
│  │                                                     ▼   │   │
│  │   PCIe_EP ←────────────────────────────────── PCIe_RC  │   │
│  │      ↓                                               │   │   │
│  │   PCIE_TILE (smn_n_target: 0x18000000–0x187FFFFF)   │   │   │
│  │      ├── TLBAppIn0/1 → NOC-N → Quasar               │   │   │
│  │      └── TLBSysIn0   → SMN   → SMC / SEP            │   │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

**Key points:**
- Linux runs on the **Host_Chiplet TT_Rocket_LT** CPU (RC side).
- The **Keraunos_PCIE_Chiplet SMC_Configure CPU** runs `pcie_bringup` bare-metal
  firmware to initialize the PCIe Tile (set `system_ready`, configure Inbound TLBs,
  program BAR sizes via DBI) before the host attempts enumeration.
- **OpenSBI (`fw_payload.elf`)** is required as M-mode firmware on the Host_Chiplet.
  It initializes the CPU, sets up S-mode, and jumps to the Linux kernel. There is
  **no U-Boot** — the simulator loads `fw_payload.elf` (which embeds OpenSBI + Linux
  kernel + DTB) directly into the Host CPU memory.
- PCIe config space is accessed via **iATU-programmed outbound window** at
  `0x70000000`, not via a dedicated ECAM region.

> **Current VDK status — why this guide exists:**
> The VDK today boots the Host_Chiplet CPU into a **bare-metal test firmware**
> (`pcie_e2e_test.elf`) rather than a real OS.  That firmware is sufficient for
> basic PCIe link-up checks but cannot run a Linux PCIe stack, kernel drivers,
> or userspace tools.
>
> **The goal of this guide is to replace that firmware with a full Linux kernel**
> on the Host_Chiplet, so that all future Keraunos PCIe bring-up, driver
> development, and validation work runs on a production-representative OS
> environment — while the Keraunos_PCIE_Chiplet continues to run its own
> bare-metal `pcie_bringup` firmware unchanged.

---

## Phase 1: Set Up the Cross-Compiler (on x86_64 Build Machine)

Even though the target is RISC-V, you cross-compile on a faster x86_64 machine.

### 1.1 Install Build Dependencies

No `sudo` required — the following packages are assumed to be available on the shared
build machine. Verify they exist before proceeding:

```bash
# Check required build tools are available (no install needed if present)
for tool in bc bison flex gcc make git wget python3 rsync dtc; do
    which $tool && $tool --version 2>&1 | head -1 || echo "MISSING: $tool"
done
```

If any are missing, request installation from your sysadmin, or use the project
environment (e.g. `module load` / `source` a project env script if available on
your cluster).

### 1.2 Use the Pre-installed RISC-V Toolchain

A fully pre-built RISC-V GCC 14 cross-compilation toolchain is already available at:

```
/proj_perf/asc/tools/asc-toolchain/tt-riscv-toolchain-20240125/
```

No download or installation required — just point your `PATH` at it.

```bash
export RISCV_TC=/proj_perf/asc/tools/asc-toolchain/tt-riscv-toolchain-20240125
export PATH=$RISCV_TC/bin:$PATH
```

### 1.3 Verify

```bash
riscv64-unknown-linux-gnu-gcc --version
# riscv64-unknown-linux-gnu-gcc () 14.0.1 20240124 (experimental)

riscv64-unknown-linux-gnu-gcc -print-multi-lib
# Should show: . (root = rv64gc/lp64d)
```

### 1.3a Fix Missing `libmpfr.so.6` (Required for GCC 14 from ASC Toolchain)

The GCC 14 internal compiler (`cc1`) in the ASC toolchain requires `libmpfr.so.6`,
but RHEL 8 ships only `libmpfr.so.4`. Without this fix, compilation fails with:

```
cc1: error while loading shared libraries: libmpfr.so.6: cannot open shared object file
```

This also causes OpenSBI's PIE linker check to fail:
```
Your linker does not support creating PIEs, opensbi requires this.
```

**Fix: create a local symlink shim (no internet access or `sudo` needed).**

```bash
mkdir -p /localdev/rmalhotra/riscv-linux/lib_shim
ln -sf /usr/lib64/libmpfr.so.4 /localdev/rmalhotra/riscv-linux/lib_shim/libmpfr.so.6

# Verify
ls -la /localdev/rmalhotra/riscv-linux/lib_shim/libmpfr.so.6
# libmpfr.so.6 -> /usr/lib64/libmpfr.so.4
```

**Expose the shim to the toolchain** (add to `~/.bashrc` or set at the start of each build session):

```bash
export LD_LIBRARY_PATH=/localdev/rmalhotra/riscv-linux/lib_shim${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
```

Confirm the cross-compiler works:

```bash
echo "int main(){}" > /tmp/t.c && riscv64-unknown-linux-gnu-gcc /tmp/t.c -o /tmp/t && echo "OK"
# Expected: OK
```

### 1.4 Set Cross-Compilation Environment Variables

Add to your `~/.bashrc` or set at the start of every build session:

```bash
export RISCV_TC=/proj_perf/asc/tools/asc-toolchain/tt-riscv-toolchain-20240125
export PATH=$RISCV_TC/bin:$PATH
export ARCH=riscv
export CROSS_COMPILE=riscv64-unknown-linux-gnu-

# libmpfr.so.6 shim (see §1.3a) — required by GCC 14 cc1
export LD_LIBRARY_PATH=/localdev/rmalhotra/riscv-linux/lib_shim${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}

# Working directory for all RISC-V Linux artifacts
export RISCV_WS=/localdev/rmalhotra/riscv-linux
mkdir -p $RISCV_WS
```

### 1.5 Build the Soft-Float Musl Toolchain (`rv64imac / lp64`)

The ASC toolchain (§1.2) only ships `lp64d` (double-float) libraries. Any userspace
binary linked with it — including BusyBox — will contain FPU instructions that crash
on the VDK's `rv64imac` CPU (no F/D extensions).

**You must build a second toolchain** targeting `rv64imac / lp64` (soft-float) with
musl as the C library. Musl produces small static binaries ideal for the initramfs.

> **Why not just rebuild the ASC toolchain?** The ASC toolchain is pre-built with a
> single multilib (`lp64d`). Rebuilding it for `lp64` would require modifying its
> build scripts. It is faster to build a fresh musl-based toolchain from source.

#### Step 1: Clone the riscv-gnu-toolchain

```bash
cd $RISCV_WS
git clone --depth 1 https://github.com/riscv-collab/riscv-gnu-toolchain.git rv64imac-toolchain-src
```

#### Step 2: Fix missing `makeinfo`

RHEL 8 may not have `makeinfo` (texinfo). Create a stub to prevent build failures:

```bash
mkdir -p $RISCV_WS/bin_shim
cat > $RISCV_WS/bin_shim/makeinfo << 'SHIM'
#!/bin/sh
echo "makeinfo (stub)"
SHIM
chmod +x $RISCV_WS/bin_shim/makeinfo
export PATH=$RISCV_WS/bin_shim:$PATH
```

#### Step 3: Configure and build

```bash
cd $RISCV_WS/rv64imac-toolchain-src

./configure \
    --prefix=$RISCV_WS/rv64imac-toolchain \
    --with-arch=rv64imac \
    --with-abi=lp64

export CCACHE_DISABLE=1   # ccache fills ~/.ccache on small /home partitions
make musl -j$(nproc)
```

> **Build notes:**
> - Takes ~10–15 minutes on a 32-core machine.
> - The build exits with code 2 after each stage due to a harmless `/lib` symlink
>   permission error (`|| true`). Re-run `make musl` until all stamps are created.
> - Check progress: `ls $RISCV_WS/rv64imac-toolchain-src/stamps/`
> - Required stamps (in order): `build-binutils-musl`, `build-gcc-musl-stage1`,
>   `build-linux-headers`, `build-musl-linux-headers`, `build-musl-linux`,
>   `build-gcc-musl-stage2`

#### Step 4: Verify

```bash
export PATH=$RISCV_WS/rv64imac-toolchain/bin:$PATH

riscv64-unknown-linux-musl-gcc --version
# riscv64-unknown-linux-musl-gcc (GCC) 15.x.x

echo "int main(){return 0;}" > /tmp/t.c
riscv64-unknown-linux-musl-gcc -static -o /tmp/t /tmp/t.c
riscv64-unknown-linux-musl-readelf -h /tmp/t | grep Flags
# Flags: 0x1, RVC, soft-float ABI
```

#### Toolchain usage summary

| Toolchain | Prefix | ABI | Use for |
|---|---|---|---|
| ASC (§1.2) | `riscv64-unknown-linux-gnu-` | `lp64d` (double-float) | **Kernel** and **OpenSBI** (both handle FPU config internally) |
| Musl (§1.5) | `riscv64-unknown-linux-musl-` | `lp64` (soft-float) | **BusyBox** and all **userspace** binaries |

---

## Phase 2: Download and Configure the Linux Kernel

### 2.1 Download Kernel Source

```bash
cd $RISCV_WS

# Linux 6.6 LTS — stable, well-tested RISC-V + PCIe RC support
wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.6.30.tar.xz
tar -xf linux-6.6.30.tar.xz
cd linux-6.6.30
```

### 2.2 Start from Default RISC-V Config

```bash
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- defconfig
```

This enables the standard 64-bit RISC-V config. Now apply targeted changes for PCIe RC support.

### 2.3 Enable PCIe Root Complex + Keraunos-relevant Options

```bash
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- menuconfig
```

Navigate and set the following. The table below maps the config path → the option:

| Category | Config Symbol | Value | Why |
|---|---|---|---|
| **FPU** | **`CONFIG_FPU`** | **`n`** | **VDK CPU is `rv64imac` (no F/D extensions). `CONFIG_FPU=y` compiles the kernel with FPU instructions that cause illegal-instruction traps → crash at boot** |
| Bus support | `CONFIG_PCI` | `y` | Core PCIe subsystem |
| Bus support | `CONFIG_PCI_MSI` | `y` | MSI/MSI-X interrupt support |
| Bus support | `CONFIG_PCIEPORTBUS` | `y` | PCIe port services |
| Bus support | `CONFIG_PCIE_DW` | `y` | Synopsys DesignWare PCIe core |
| Bus support | `CONFIG_PCIE_DW_HOST` | `y` | DW PCIe as Root Complex |
| Bus support | `CONFIG_PCI_HOST_GENERIC` | `y` | Generic OF-based PCIe host (fallback) |
| Bus support | `CONFIG_PCI_DOMAINS` | `y` | Multiple PCIe domains |
| DMA | `CONFIG_IOMMU_SUPPORT` | `y` | IOMMU for DMA safety |
| Memory | `CONFIG_ZONE_DMA32` | `y` | 32-bit DMA zone |
| Init | `CONFIG_BLK_DEV_INITRD` | `y` | initramfs boot |
| Filesystems | `CONFIG_TMPFS` | `y` | tmpfs for /tmp |
| Filesystems | `CONFIG_DEVTMPFS` | `y` | auto-populate /dev |
| Filesystems | `CONFIG_PROC_FS` | `y` | /proc |
| Filesystems | `CONFIG_SYSFS` | `y` | /sys |
| Serial | `CONFIG_SERIAL_8250` | `y` | UART console |
| Serial | `CONFIG_SERIAL_OF_PLATFORM` | `y` | DT-driven UART |
| Debug | `CONFIG_EARLY_PRINTK` | `y` | Early boot console |
| Debug | `CONFIG_PCI_DEBUG` | `y` | Verbose PCIe debug (remove for production) |
| SMP | `CONFIG_SMP` | `y` | Multi-core host CPU |

> **Critical — FPU must be disabled:**
> The VDK Host CPU (`TT_Rocket_LT_PSP_1`, variant `Rocket-Core-CCE`) implements
> **`rv64imac`** only — no F (single-float) or D (double-float) extensions.
> `Properties.xml` confirms `/Floating_Point/mstatus_FS = 0` (FPU absent).
>
> The default `riscv_defconfig` enables `CONFIG_FPU=y`, which compiles the kernel
> with `-march=rv64gc` (includes F+D). Any FPU instruction in the kernel binary
> triggers an **illegal instruction trap** on this CPU, crashing the boot before
> any UART output appears.
>
> Always verify after `olddefconfig`:
> ```bash
> grep CONFIG_FPU .config
> # Must show: # CONFIG_FPU is not set
> ```
>
> The rebuilt kernel ELF should show `soft-float ABI`:
> ```bash
> riscv64-unknown-linux-gnu-readelf -h vmlinux | grep Flags
> # Flags: 0x1, RVC, soft-float ABI
> ```

**Script-based config (instead of menuconfig):**
```bash
cd $RISCV_WS/linux-6.6.30

# VDK CPU is rv64imac — disable FPU to avoid illegal-instruction crash
./scripts/config --disable CONFIG_FPU

./scripts/config --enable CONFIG_PCI
./scripts/config --enable CONFIG_PCI_MSI
./scripts/config --enable CONFIG_PCIEPORTBUS
./scripts/config --enable CONFIG_PCIE_DW
./scripts/config --enable CONFIG_PCIE_DW_HOST
./scripts/config --enable CONFIG_PCI_HOST_GENERIC
./scripts/config --enable CONFIG_PCI_DOMAINS
./scripts/config --enable CONFIG_BLK_DEV_INITRD
./scripts/config --enable CONFIG_TMPFS
./scripts/config --enable CONFIG_DEVTMPFS
./scripts/config --enable CONFIG_DEVTMPFS_MOUNT
./scripts/config --enable CONFIG_SERIAL_8250
./scripts/config --enable CONFIG_SERIAL_8250_CONSOLE
./scripts/config --enable CONFIG_SERIAL_OF_PLATFORM
./scripts/config --enable CONFIG_PCI_DEBUG
./scripts/config --enable CONFIG_IOMMU_SUPPORT

# Resolve any new dependency prompts automatically
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- olddefconfig
```

### 2.4 Compile the Kernel

```bash
cd $RISCV_WS/linux-6.6.30

make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- -j$(nproc)

# Check outputs
ls -lh arch/riscv/boot/Image        # Raw kernel binary (~20 MB)
ls -lh arch/riscv/boot/Image.gz     # Compressed
ls -lh vmlinux                       # ELF (for GDB/JTAG debugging)
```

---

## Phase 3: Build the Device Tree

The Device Tree describes the RISC-V host board hardware to Linux: CPUs, memory,
UART, interrupt controllers, and critically — the PCIe Root Complex that connects to
Keraunos.

### 3.0 Install `dtc` (Device Tree Compiler) — No `sudo` Required

The system does not ship `dtc`. Build it from source directly into `$RISCV_WS`:

```bash
cd $RISCV_WS

git clone https://git.kernel.org/pub/scm/utils/dtc/dtc.git
cd dtc
make -j$(nproc)
```

Add `dtc` to `PATH` (also add to `~/.bashrc` and `riscv-linux-build.sh`):

```bash
export PATH=$RISCV_WS/dtc:$PATH
```

Verify:

```bash
dtc --version
# Version: DTC 1.7.x
```

> **Note:** If you accidentally cloned `dtc` inside the kernel source tree
> (`linux-6.6.30/dtc/`), move it out first:
> ```bash
> mv $RISCV_WS/linux-6.6.30/dtc $RISCV_WS/dtc
> ```

### 3.1 Understand the PCIe Address Layout

All addresses below are sourced directly from the VDK files.

#### Host_Chiplet CPU Address Map
> Source: `software/host_pcie_test/pcie_e2e_test.c` lines 12–16, `bin/artefacts/mem_map_top`

```
Host_Chiplet CPU (TT_Rocket_LT) physical address space:

  0x44000000 – 0x443FFFFF   PCIe RC DBI        (4 MB)   — RC controller config regs
  0x44300000                PCIe RC ATU        (via DBI CS2, offset +0x300000)
  0x70000000 – 0x7FFFFFFF   PCIe RC AXI_Slave  (256 MB) — all PCIe outbound traffic goes here
  0x80000000 – 0x83FFFFFF   Host DRAM          (1GB )  — Linux loads here
  0xC000A000                UART               (256 B)
  0xC4000000                PLIC
```

#### How the Host Reaches EP Config Space and BARs
> Source: `pcie_e2e_test.c` (TEST 4, TEST 5) — the iATU is the only outbound path

The host has **no dedicated ECAM window**. All PCIe traffic — config reads, memory
reads, memory writes — exits via the `AXI_Slave` window at `0x70000000` after the
RC's iATU translates it to a PCIe TLP:

```
Config space access (CfgRd0 to Bus 1 Dev 0):
  Host writes to 0x70000000  →  ATU Region 0 (type=CFG0, target=Bus1Dev0)
                             →  PCIe wire  →  EP PCIMem_Slave  →  EP config space

BAR memory access (MemRd/MemWr):
  Host writes to 0x70100000  →  ATU Region 1 (type=MEM, target=EP BAR0 base)
                             →  PCIe wire  →  EP PCIMem_Slave  →  PCIE_TILE
```

ATU register base: `RC_DBI_BASE + 0x300000` = `0x44300000`
(offsets: `IATU_*_OFF_OUTBOUND`, stride = `0x200` per region)

#### EP Inbound Routing — BAR → NOC Channel
> Source: `software/src/pcie_init.c` (~line 1012), `software/src/pcie_config.h` (NOC Channel IDs)

The EP's iATU maps each BAR to a NOC channel by encoding the channel ID in
**bits [63:60] of the ATU inbound target address**:

```c
/* From pcie_init.c — ATU inbound setup: */
cfg->atu_inbound[0].bar = 0;  target = (uint64_t)NOCPCIE_IN_APP0   << 60; // 0  << 60
cfg->atu_inbound[1].bar = 2;  target = (uint64_t)NOCPCIE_IN_SYSIN0 << 60; // 14 << 60
cfg->atu_inbound[2].bar = 4;  target = (uint64_t)NOCPCIE_IN_APP1   << 60; // 1  << 60

/* From pcie_init.c — channel IDs: */
#define NOCPCIE_IN_APP0   0    // bits[63:60] = 0x0
#define NOCPCIE_IN_APP1   1    // bits[63:60] = 0x1
#define NOCPCIE_IN_SYSIN0 14   // bits[63:60] = 0xE
```

Resulting BAR → channel mapping:

| BAR | Size | NOC channel ID | bits[63:60] | Destination |
|-----|------|----------------|-------------|-------------|
| BAR0/1 (64-bit) | 4 GB   | APP0 = 0  | `0x0` | TLBAppIn0 → NOC-N (Quasar data memory) |
| BAR2/3 (64-bit) | 1 MB   | SYSIN0 = 14 | `0xE` | TLBSysIn0 → SMN (SMC / SEP config regs) |
| BAR4/5 (64-bit) | 512 GB | APP1 = 1  | `0x1` | TLBAppIn1 → NOC-N (large DRAM window) |

> Source for BAR sizes: `pcie_config.h` `PCIE_BAR0_SIZE` / `PCIE_BAR2_SIZE` / `PCIE_BAR4_SIZE`

#### TLB Target Addresses (real SoC — NOT connected in VDK)
> Source: `pcie_config.h` comment block "TLB Target Addresses (Real SoC, NOT VDK-accessible)"

The EP firmware writes these into TLB config registers (`0x18040000`) to define
where inbound traffic lands. The VDK accepts the writes but the destinations are
not wired up — expected for initial bring-up:

```
PCIE_CFG_SYS_SRAM_BASE          = 0x0000_0100_0000_0000  (Tensix SRAM)
PCIE_CFG_SYS_DRAM_BASE          = 0x0001_0000_0000_0000  (System DRAM)
PCIE_CFG_SMC_MAILBOX_BASE       = 0x0001_2020_1800_0     (SMC Mailbox)
PCIE_CFG_KERAUNOS_CONFIG_BASE   = 0x0001_2000_0000_0     (Keraunos Config)
```

#### EP Boot Prerequisite
> Source: `pcie_config.h` `PCIE_CFG_SYSTEM_READY_REG` / `PCIE_CFG_ACCESS_CTRL_REG`,
> `keraunos_soc_reg.h` `o_system_ready` / `o_pcie_inbound_app_enable`

Before Linux can enumerate the EP, the `pcie_bringup` bare-metal firmware on the
Keraunos_PCIE_Chiplet must complete its init sequence:

1. Deassert `cold_reset_n` (reset unit at `SMC_CPU_RESET_UNIT_SS_COLD_RESET_N`)
2. Configure PHY / SerDes (via `0x18080000` PHY AHB)
3. Write `system_ready = 1` at `0x1804FFFC`
4. Configure Inbound TLBs (at `0x18040000`)
5. Write `pcie_inbound_app_enable = 1` at `0x1804FFF8`

Without step 3–5, all inbound PCIe transactions from the host return errors.

### 3.2 Device Tree Source (DTS) — VDK (Host_Chiplet Memory Map)

Addresses are taken directly from the VDK memory map
(`Keraunos_PCIE_Tile/bin/artefacts/mem_map_top`) and
`software/host_pcie_test/pcie_e2e_test.c`.

> **VDK note:** There is no dedicated ECAM window. Config space TLPs are
> issued via an iATU outbound region programmed inside the `0x70000000`
> AXI_Slave window. The `reg-names = "dbi", "config"` pattern below
> matches how the DW PCIe Linux driver uses those two regions.

```bash
cat > $RISCV_WS/smc_pcie_tile.dts << 'EOF'
/dts-v1/;

/ {
    #address-cells = <2>;
    #size-cells = <2>;
    compatible = "tenstorrent,keraunos-vdk";
    model = "Keraunos VDK Host_Chiplet — TT_Rocket_LT RISC-V";

    chosen {
        /* UART at 0xC000A000 — Synopsys DesignWare UART (DW_apb_uart) */
        bootargs = "console=ttyS0,115200 earlycon=uart8250,mmio32,0xC000A000 \
rdinit=/init pci=realloc pci=assign-busses";
        linux,initrd-start = <0x0 0x84000000>;
        linux,initrd-end   = <0x0 0x86000000>;
    };

    /* ------------------------------------------------------------------ */
    /*  CPU — TT_Rocket_LT RISC-V (single core in VDK Host_Chiplet)      */
    /* ------------------------------------------------------------------ */
    cpus {
        #address-cells = <1>;
        #size-cells = <0>;
        timebase-frequency = <1000000>;

        cpu@0 {
            compatible = "riscv";
            device_type = "cpu";
            reg = <0>;
            riscv,isa = "rv64imafdc";
            mmu-type = "riscv,sv39";
            cpu0_intc: interrupt-controller {
                compatible = "riscv,cpu-intc";
                interrupt-controller;
                #interrupt-cells = <1>;
            };
        };
    };

    /* ------------------------------------------------------------------ */
    /*  Host DRAM — 0x8000_0000, 1GB                                   */
    /*  Source: mem_map_top / pcie_e2e_test.c comment                    */
    /* ------------------------------------------------------------------ */
    memory@80000000 {
        device_type = "memory";
        reg = <0x0 0x80000000 0x0 0x10000000>;  /* 256 MB (VDK has 1 GB, see §5.4) */
    };

    /* ------------------------------------------------------------------ */
    /*  PLIC — 0xC400_0000                                               */
    /*  Source: software/src/main.c PLIC base                            */
    /* ------------------------------------------------------------------ */
    plic: interrupt-controller@C4000000 {
        compatible = "riscv,plic0";
        interrupt-controller;
        #interrupt-cells = <1>;
        #address-cells = <0>;
        reg = <0x0 0xC4000000 0x0 0x04000000>;
        interrupts-extended = <&cpu0_intc 11 &cpu0_intc 9>;
        riscv,ndev = <64>;
    };

    /* ------------------------------------------------------------------ */
    /*  UART — 0xC000_A000 (Synopsys DW_apb_uart / NS16550-compatible)  */
    /*  Source: pcie_e2e_test.c memory map comment                       */
    /*                                                                   */
    /*  CRITICAL: Do NOT add interrupts / interrupt-parent here.         */
    /*  The VDK PLIC does not wire the UART IRQ output. If the DTS      */
    /*  declares an interrupt, the 8250 tty driver uses interrupt-driven */
    /*  TX and hangs forever waiting for the THRI interrupt. Without     */
    /*  interrupts the driver falls back to timer-based polling, which   */
    /*  works for both kernel printk and userspace tty writes.           */
    /* ------------------------------------------------------------------ */
    uart0: serial@C000A000 {
        compatible = "snps,dw-apb-uart";
        reg = <0x0 0xC000A000 0x0 0x100>;
        clock-frequency = <0x5f5e100>;
        current-speed = <0x1c200>;
        reg-io-width = <4>;
        reg-shift = <2>;
    };

    /* ------------------------------------------------------------------ */
    /*  PCIe Root Complex (Synopsys DesignWare)                          */
    /*  VDK path: Host_Chiplet > Misc > PCIE_RC                         */
    /*                                                                   */
    /*  VDK memory map (from mem_map_top + pcie_e2e_test.c):            */
    /*    DBI:       0x4400_0000 – 0x443F_FFFF  (4 MB)                  */
    /*    ATU (CS2): 0x4430_0000               (via DBI + 0x300000)     */
    /*    AXI_Slave: 0x7000_0000 – 0x7FFF_FFFF (256 MB, PCIe window)   */
    /*                                                                   */
    /*  The AXI_Slave window is the single outbound PCIe MMIO window.   */
    /*  Linux uses it for both config TLPs (via iATU CFG0/CFG1) and     */
    /*  memory TLPs (via iATU MEM) to reach Keraunos BARs.             */
    /*                                                                   */
    /*  Keraunos BAR layout (from pcie_config.h):                       */
    /*    BAR0: 4 GB   — APP0  → TLBAppIn0 → NOC-N (Quasar)            */
    /*    BAR2: 1 MB   — SYSIN0 → TLBSysIn0 → SMN (SMC/SEP)           */
    /*    BAR4: 512 GB — APP1  → TLBAppIn1 → NOC-N (large DRAM)        */
    /* ------------------------------------------------------------------ */
    pcie: pcie@44000000 {
        compatible = "snps,dw-pcie";
        reg = <0x0 0x44000000 0x0 0x400000>,   /* DBI: 4 MB            */
              <0x0 0x70000000 0x0 0x10000000>; /* config/mem window    */
        reg-names = "dbi", "config";

        #address-cells = <3>;
        #size-cells = <2>;
        device_type = "pci";

        bus-range = <0x0 0xff>;

        /*
         * ranges: outbound PCIe address windows.
         *
         * Single 256 MB non-prefetchable window backed by AXI_Slave
         * at 0x7000_0000. The Linux DW PCIe driver programs the iATU
         * to generate CFG0 / MEM TLPs into this window.
         *
         * Keraunos BAR bits[63:60] routing:
         *   0x0 → TLBAppIn0 → NOC-N  (BAR0, Quasar data memory, 4 GB)
         *   0x1 → TLBAppIn1 → NOC-N  (BAR4, large DRAM window, 512 GB)
         *   0x4 → TLBSysIn0 → SMN    (BAR2, SMC/SEP config, 1 MB)
         */
        ranges =
            <0x02000000 0x0 0x70000000  0x0 0x70000000  0x0 0x10000000>;

        interrupt-parent = <&plic>;
        /* Index 0 = MSI (dw_pcie_msi_host_init),
         * Index 1 = INTx (pp->irq in pcie-designware-plat.c) */
        interrupts = <32>, <33>;

        clocks = <&pcie_clk>;
        clock-names = "pcie";

        num-lanes = <4>;   /* VDK: x4 (confirmed from smc_pcie_tile.dts) */

        status = "okay";
    };

    pcie_clk: pcie-clock {
        compatible = "fixed-clock";
        #clock-cells = <0>;
        clock-frequency = <250000000>;
    };
};
EOF
```

### 3.3 Compile DTS → DTB

```bash
dtc -I dts -O dtb \
    -o $RISCV_WS/smc_pcie_tile.dtb \
       $RISCV_WS/smc_pcie_tile.dts

echo "DTB size: $(ls -lh $RISCV_WS/smc_pcie_tile.dtb)"

# Decompile back to verify correctness
dtc -I dtb -O dts $RISCV_WS/smc_pcie_tile.dtb | head -60
```

---

## Phase 4: Create Root Filesystem

Since the RISC-V host may not have persistent storage initially, use an **initramfs**
with BusyBox. This is enough to get a shell, run `lspci`, and load kernel modules.

### 4.1 Build BusyBox (statically linked, soft-float musl)

BusyBox **must** be compiled with the soft-float musl toolchain (§1.5). The ASC
toolchain produces `lp64d` binaries that crash on the VDK's `rv64imac` CPU.

```bash
cd $RISCV_WS
export PATH=$RISCV_WS/rv64imac-toolchain/bin:$PATH

wget https://busybox.net/downloads/busybox-1.36.1.tar.bz2
tar -xf busybox-1.36.1.tar.bz2
cd busybox-1.36.1

make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-musl- defconfig

# Enable static linking
sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config

# Disable tc applet (needs CBQ kernel headers not present in musl)
sed -i 's/CONFIG_TC=y/# CONFIG_TC is not set/' .config

make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-musl- -j$(nproc)
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-musl- install
# Installs to busybox-1.36.1/_install/
```

Verify the binary is soft-float:

```bash
riscv64-unknown-linux-musl-readelf -h busybox | grep Flags
# Flags: 0x1, RVC, soft-float ABI
```

### 4.2 Build the initramfs Directory

```bash
cd $RISCV_WS
mkdir -p initramfs/{bin,sbin,etc,proc,sys,dev,tmp,lib,lib64,usr/bin,usr/sbin,mnt,root}

# Copy BusyBox tree
cp -a busybox-1.36.1/_install/* initramfs/
```

> **Note:** Skip `modules_install` for the initial boot -- kernel modules add size
> and the VDK simulation is slow. Add them later once the shell prompt works.

### 4.3 Write the init Script

The init script uses plain `echo` (writes to inherited stdout, which the kernel
connected to `/dev/console`). Do **not** redirect explicitly to `/dev/console` or
`/dev/ttyS0` — re-opening a tty device can block on carrier detect.

> **Important:** The UART DTS node must **not** declare interrupts (see §3.2).
> Without interrupts the 8250 driver uses timer-based polling for TX/RX. If
> interrupts are declared but not wired in the VDK PLIC, userspace writes to
> the tty will hang forever waiting for the TX-empty interrupt.

```bash
cat > $RISCV_WS/initramfs/init << 'INIT_EOF'
#!/bin/sh
echo "[init] ALIVE"
mount -t proc  none /proc
mount -t sysfs none /sys

echo "=DW="
dmesg | grep dw-pcie

echo "=LSPCI="
lspci 2>&1

echo "=LSPCI-V="
lspci -v 2>&1

echo "=RC-CONFIG="
cat /sys/bus/pci/devices/0000\:00\:00.0/config | xxd | head -4

echo "=DMESG-PCI="
dmesg | grep -i "pci\|bus\|scan\|enum\|iatu\|atu"

echo "=END="
exec /bin/sh
INIT_EOF

chmod +x $RISCV_WS/initramfs/init
```

### 4.4 Pack into cpio.gz (with device nodes via `fakeroot`)

The kernel opens `/dev/console` **before** running init. If the device node is
missing from the initramfs, the "Warning: unable to open an initial console"
message appears and init's stdout goes nowhere.

Creating device nodes in a cpio archive requires root. Use `fakeroot`:

```bash
cd $RISCV_WS
fakeroot sh -c '
cd initramfs &&
mknod -m 622 dev/console c 5 1 &&
mknod -m 666 dev/null    c 1 3 &&
mknod -m 666 dev/zero    c 1 5 &&
mknod -m 666 dev/ttyS0   c 4 64 &&
find . | cpio -H newc -o 2>/dev/null
' | gzip > initramfs.cpio.gz

echo "initramfs size: $(du -sh initramfs.cpio.gz)"
# Expected: ~750 KB
```

### 4.5 Embed initramfs in the kernel

Embed the cpio.gz directly in the kernel Image so the boot chain stays as a
single `fw_payload.elf`:

```bash
cd $RISCV_WS/linux-6.6.30
./scripts/config --set-str CONFIG_INITRAMFS_SOURCE "$RISCV_WS/initramfs.cpio.gz"
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- olddefconfig
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- -j$(nproc)
```

---

## Phase 5: Launch the VDK and Boot Linux

In the VDK there is no OpenSBI, U-Boot, or physical boot media. The Synopsys
Virtualizer simulator loads ELF images directly into each CPU's memory and
starts simulation. The two CPUs boot in parallel:

| CPU | Chiplet | Firmware loaded | Role |
|---|---|---|---|
| `SMC_Configure` | `Keraunos_PCIE_Chiplet` | `pcie_bringup.elf` | Initializes PCIe Tile, programs BARs via DBI, sets `system_ready=1` |
| `SMC` | `Host_Chiplet` | `fw_payload.elf` (OpenSBI + Linux `Image` + DTB) | OpenSBI (M-mode) → Linux kernel (S-mode) → enumerates EP, drives Keraunos |

### 5.1 Prerequisites — VDK .vdksys Bus Connections

> **WIP:** The `.vdksys` file is currently missing 4 bus connections.
> Without them the CPU buses are disconnected from `SharedMemoryMap` and
> all external accesses (DBI, SMN, UART) silently hit undecoded stubs.
> See `VDKSYS_CHANGES_REQUIRED.md` in the VDK for full details.

Four connections must be added in `Keraunos_PCIE_Tile.vdksys`
(Virtualizer Studio GUI → drag DATA/INSTRUCTION → SharedMemoryMap.intf):

| Endpoint A (bus master) | Endpoint B (shared bus) | `start` |
|---|---|---|
| `Host_Chiplet > SMC` DATA | `Host_Chiplet > SharedMemoryMap` intf | `0x0` |
| `Host_Chiplet > SMC` INSTRUCTION | `Host_Chiplet > SharedMemoryMap` intf | `0x0` |
| `Keraunos_PCIE_Chiplet > SMC_Configure` DATA | `Keraunos_PCIE_Chiplet > SharedMemoryMap` intf | `0x0` |
| `Keraunos_PCIE_Chiplet > SMC_Configure` INSTRUCTION | `Keraunos_PCIE_Chiplet > SharedMemoryMap` intf | `0x0` |

Verify the fix worked (should return `0` after regenerating `Properties.xml`):

```bash
grep -c "TT_Rocket_LT_PSP_1_DATA_Undecoded\|TT_Rocket_LT_PSP_1_INSTRUCTION_Undecoded" \
    /localdev/rmalhotra/Keraunos_PCIE_Tile/generated/default/Properties.xml
# Expected: 0
```

### 5.2 Why OpenSBI Is Required

RISC-V CPUs reset into **M-mode** (Machine mode). Linux runs in **S-mode** (Supervisor mode).
OpenSBI is the M-mode firmware that:
1. Initializes CPU CSRs (Control and Status Registers)
2. Drops privilege level from M-mode to S-mode
3. Provides runtime SBI services to Linux (timer, IPI, console)
4. Jumps to the Linux kernel entry point at `0x80200000`

The `fw_payload` variant embeds the Linux `Image` and DTB directly inside the OpenSBI ELF,
creating a single file the Imperas simulator can load. **Do not use `vmlinux` directly** — it
uses virtual addresses and will crash without OpenSBI setting up the MMU.

### 5.3 Build the DTB and OpenSBI (`fw_payload.elf`)

First set up the toolchain (same as Phase 1):

```bash
export RISCV_TC=/proj_perf/asc/tools/asc-toolchain/tt-riscv-toolchain-20240125
export PATH=$RISCV_TC/bin:$PATH
export LD_LIBRARY_PATH=/localdev/rmalhotra/riscv-linux/lib_shim${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
```

#### Step 1: Compile the DTB

```bash
DTC=/localdev/rmalhotra/riscv-linux/dtc/dtc
$DTC -I dts -O dtb \
    -o /localdev/rmalhotra/riscv-linux/smc_pcie_tile.dtb \
       /localdev/rmalhotra/riscv-linux/smc_pcie_tile.dts
ls -lh /localdev/rmalhotra/riscv-linux/smc_pcie_tile.dtb   # expect ~2 KB
```

#### Step 2: Build OpenSBI with embedded kernel and DTB

```bash
cd /localdev/rmalhotra/riscv-linux/opensbi

make clean && \
make -j$(nproc) \
    CROSS_COMPILE=riscv64-unknown-linux-gnu- \
    PLATFORM=generic \
    PLATFORM_RISCV_XLEN=64 \
    PLATFORM_RISCV_ISA=rv64imac_zicsr_zifencei \
    PLATFORM_RISCV_ABI=lp64 \
    FW_PAYLOAD=y \
    FW_TEXT_START=0x80000000 \
    FW_FDT_PATH=/localdev/rmalhotra/riscv-linux/smc_pcie_tile.dtb \
    FW_PAYLOAD_PATH=/localdev/rmalhotra/riscv-linux/linux-6.6.30/arch/riscv/boot/Image
```

> **Critical flags:**
> - `PLATFORM_RISCV_ISA=rv64imac_zicsr_zifencei` — matches the Imperas RISC-V CPU ISA. The `_zicsr` suffix is required by binutils 2.38+ to allow CSR instructions.
> - `FW_FDT_PATH` — embeds the compiled DTB into `fw_payload.elf` (not `FW_PAYLOAD_FDT_PATH`).
> - `FW_PAYLOAD_PATH` — embeds the Linux kernel `Image` directly. If omitted, OpenSBI silently uses an internal test payload and produces no Linux output.
> - No `FW_PAYLOAD_OFFSET` — let OpenSBI place the kernel at its default offset (`0x200000`).

Expected output ELF (≈30 MB = OpenSBI + kernel + initramfs):
```
/localdev/rmalhotra/riscv-linux/opensbi/build/platform/generic/firmware/fw_payload.elf
```

Memory layout after VDK loads the ELF:
```
0x80000000  OpenSBI (M-mode entry point — CPU reset vector)
0x80200000  Linux kernel Image (S-mode, jumped to by OpenSBI)
            DTB passed in a1 register by OpenSBI
```

### 5.4 Configure the VDK to Load OpenSBI + Linux

The VDK is at `/localdev/rmalhotra/Keraunos_PCIE_Tile`. Two files must be correct
before launching.

#### 5.4.1 Set the active configuration in `snps.vpproject`

The project file must point to the `default` config (the `PCIEPlatformTest` config
file is not present in this copy of the VDK):

```bash
grep "activeConfig" /localdev/rmalhotra/Keraunos_PCIE_Tile/snps.vpproject
# Must show: activeConfig="default/default"
```

If it shows `PCIEPlatformTest`, fix it:
```bash
sed -i 's/activeConfig="PCIEPlatformTest\/PCIEPlatformTest"/activeConfig="default\/default"/' \
    /localdev/rmalhotra/Keraunos_PCIE_Tile/snps.vpproject
```

#### 5.4.2 Set simulation properties in `default.vpcfg`

The VP Config file at `vpconfigs/default/default.vpcfg` controls simulation-critical
properties that persist across Virtualizer rebuilds. Without these, OpenSBI/Linux
either fails to boot or floods `simout` with stub warnings.

| Property | Value | Why |
|---|---|---|
| Host CPU `defaultsemihost` | `false` | OpenSBI ecalls NOT intercepted by PK — OpenSBI boots cleanly |
| EP CPU `defaultsemihost` | `false` | `pcie_bringup.elf` `exit(0)` NOT intercepted — simulation keeps running for Linux |
| SharedMemoryMap `enable_warnings` (Host + EP) | `false` | No undecoded-address stub noise in simout |
| TLM_OK_RESPONSE (4 CPU bus stubs) | active | Unmapped accesses return 0 instead of bus errors |
| RST_GEN `active_at_start` (both chiplets) | `true` | Proper reset sequencing |
| PCIE_RC / PCIe_EP clocks | 100 / 250 MHz | Correct PCIe timing (`pipe_clk` = 250, all others = 100) |
| `SHARED_DBI_ENABLED` (RC + EP) | `false` | Single DBI port mode |
| `autoContinueInitialCrunch` | `false` | Simulation pauses at elaboration for GDB attach |

The full working `default.vpcfg`:

```xml
<?xml version="1.0" encoding="ASCII"?>
<com.synopsys.vp.base.vpcfg.core:Configuration xmi:version="2.0" xmlns:xmi="http://www.omg.org/XMI" xmlns:com.synopsys.vp.base.vpcfg.core="com.synopsys.vp.base.vpcfg.core.model" description="empty configuration" comment="gen_tool_VPX">
  <debugInfo traceSimOutput="true" generateUniqueSimOutFile="true" autoContinueInitialCrunch="false"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Host_Chiplet.Infra.RST_GEN.#SCML_PROPERTIES#active_at_start" value="true"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Host_Chiplet.Misc.PCIE_RC.#SCML_PROPERTIES#SHARED_DBI_ENABLED" value="false"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Host_Chiplet.Misc.PCIE_RC.#SCML_PROPERTIES#cc_aclkMstr_Override" value="100"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Host_Chiplet.Misc.PCIE_RC.#SCML_PROPERTIES#cc_aclkSlv_Override" value="100"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Host_Chiplet.Misc.PCIE_RC.#SCML_PROPERTIES#cc_aux_clk_Override" value="100"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Host_Chiplet.Misc.PCIE_RC.#SCML_PROPERTIES#cc_dbi_aclk_Override" value="100"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Host_Chiplet.Misc.PCIE_RC.#SCML_PROPERTIES#cc_pipe_clk_Override" value="250"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Host_Chiplet.Misc.PCIE_RC.#SCML_PROPERTIES#refclk_Override" value="100"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Host_Chiplet.SMC.CPU.TT_Rocket_LT_PSP_1.#SCML_PROPERTIES#/Simulator/defaultsemihost" value="false"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Host_Chiplet.SMC.CPU.TT_Rocket_LT_PSP_1.#EXTRA_PROPERTIES#/ImageInfo/cpu0/initial_image" value="{../../../riscv-linux/opensbi/build/platform/generic/firmware/fw_payload.elf,,,image+symbols,} "/>
  <paramOverrides key="Keraunos_PCIE_Tile.Host_Chiplet.SMC.CPU.iStub|iBus_TT_Rocket_LT_PSP_1_DATA_Undecoded.#EXTRA_PROPERTIES#/all_encaps/response" value="TLM_OK_RESPONSE"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Host_Chiplet.SMC.CPU.iStub|iBus_TT_Rocket_LT_PSP_1_INSTRUCTION_Undecoded.#EXTRA_PROPERTIES#/all_encaps/response" value="TLM_OK_RESPONSE"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Host_Chiplet.iStub|iBus_SharedMemoryMap_intf_Undecoded.#EXTRA_PROPERTIES#/all_encaps/enable_warnings" value="false"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Keraunos_PCIE_Chiplet.Infra.RST_GEN.#SCML_PROPERTIES#active_at_start" value="true"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Keraunos_PCIE_Chiplet.Misc.PCIe_EP.#SCML_PROPERTIES#SHARED_DBI_ENABLED" value="false"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Keraunos_PCIE_Chiplet.Misc.PCIe_EP.#SCML_PROPERTIES#cc_aclkMstr_Override" value="100"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Keraunos_PCIE_Chiplet.Misc.PCIe_EP.#SCML_PROPERTIES#cc_aclkSlv_Override" value="100"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Keraunos_PCIE_Chiplet.Misc.PCIe_EP.#SCML_PROPERTIES#cc_aux_clk_Override" value="100"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Keraunos_PCIE_Chiplet.Misc.PCIe_EP.#SCML_PROPERTIES#cc_dbi_aclk_Override" value="100"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Keraunos_PCIE_Chiplet.Misc.PCIe_EP.#SCML_PROPERTIES#cc_pipe_clk_Override" value="250"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Keraunos_PCIE_Chiplet.Misc.PCIe_EP.#SCML_PROPERTIES#refclk_Override" value="100"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Keraunos_PCIE_Chiplet.SMC_Configure.CPU.TT_Rocket_LT_PSP_1.#SCML_PROPERTIES#/Simulator/defaultsemihost" value="false"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Keraunos_PCIE_Chiplet.SMC_Configure.CPU.TT_Rocket_LT_PSP_1.#EXTRA_PROPERTIES#/ImageInfo/cpu0/initial_image" value="{../../software/pcie_bringup/pcie_bringup.elf,,,image+symbols,} "/>
  <paramOverrides key="Keraunos_PCIE_Tile.Keraunos_PCIE_Chiplet.SMC_Configure.CPU.iStub|iBus_TT_Rocket_LT_PSP_1_DATA_Undecoded.#EXTRA_PROPERTIES#/all_encaps/response" value="TLM_OK_RESPONSE"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Keraunos_PCIE_Chiplet.SMC_Configure.CPU.iStub|iBus_TT_Rocket_LT_PSP_1_INSTRUCTION_Undecoded.#EXTRA_PROPERTIES#/all_encaps/response" value="TLM_OK_RESPONSE"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Keraunos_PCIE_Chiplet.iStub|iBus_SharedMemoryMap_intf_Undecoded.#EXTRA_PROPERTIES#/all_encaps/enable_warnings" value="false"/>
  <paramOverrides key="Keraunos_PCIE_Tile.Keraunos_PCIE_Chiplet.iBus|SharedMemoryMap_intf.range_mappings" value="0x44000000:0x00400000:s;0x18000000:0x00800000"/>
  <monitorSets name="Default" path="${vpconfigDir}/Default.monitors" chartDisplayScript="">
    <startTrigger type="TIME" value="0"/>
  </monitorSets>
</com.synopsys.vp.base.vpcfg.core:Configuration>
```

> **Common mistake:** Using `FW_PAYLOAD_FDT_PATH` instead of `FW_FDT_PATH` when
> building OpenSBI. The wrong flag silently embeds no DTB, and Linux cannot find
> the device tree at boot. If OpenSBI boots but Linux is silent, rebuild OpenSBI
> with `FW_FDT_PATH` (see Section 5.3 Step 2).

#### 5.4.3 Set firmware paths in `Properties.xml`

Edit `/localdev/rmalhotra/Keraunos_PCIE_Tile/generated/default/Properties.xml`.

> **Warning:** Virtualizer regenerates this file when it rebuilds the project,
> wiping your firmware settings. Always verify these values before pressing Run.

Two entries must be set. Search for `/ImageInfo/cpu0/initial_image` in the file to find both:

| Approx line | Component | Purpose |
|---|---|---|
| ~8892 | `Host_Chiplet.SMC.CPU.TT_Rocket_LT_PSP_1` | Load vmlinux (symbols) + fw_payload.elf (execution) |
| ~33919 | `Keraunos_PCIE_Chiplet.SMC_Configure.CPU.TT_Rocket_LT_PSP_1` | Load pcie_bringup.elf |

**Critical format:** Values must use curly-brace syntax — plain paths fail with
`Unable to load image`. Use **absolute paths** to avoid working-directory ambiguity.

The Host_Chiplet uses a **dual-file** format: `vmlinux` provides kernel debug symbols
to the GDB debugger, while `fw_payload.elf` is the actual binary executed by the CPU:

```xml
<!-- Host_Chiplet (OpenSBI + Linux) — dual file: symbols + execution image -->
<name>/ImageInfo/cpu0/initial_image</name>
<string>
  <value>{/localdev/rmalhotra/riscv-linux/linux-6.6.30/vmlinux,,,symbols,} {/localdev/rmalhotra/riscv-linux/opensbi/build/platform/generic/firmware/fw_payload.elf,,,image+symbols,}</value>
</string>

<!-- Keraunos EP (PCIe bringup firmware) -->
<name>/ImageInfo/cpu0/initial_image</name>
<string>
  <value>{/localdev/rmalhotra/Keraunos_PCIE_Tile/software/pcie_bringup/pcie_bringup.elf,,,image+symbols,}</value>
</string>
```

> **Why two files for Host_Chiplet?**
> `fw_payload.elf` embeds OpenSBI + Linux kernel and is what the CPU actually executes.
> `vmlinux` is the unstripped kernel ELF with full debug symbols — loading it with
> `symbols` mode (no image load) lets GDB resolve function names during debugging without
> altering what runs.

Use this Python snippet to patch both entries at once (re-run if Virtualizer regenerates the file):

```python
python3 << 'EOF'
import re

PROPS = '/localdev/rmalhotra/Keraunos_PCIE_Tile/generated/default/Properties.xml'

HOST_VAL  = ('{/localdev/rmalhotra/riscv-linux/linux-6.6.30/vmlinux,,,symbols,} '
             '{/localdev/rmalhotra/riscv-linux/opensbi/build/platform/generic/firmware/fw_payload.elf,,,image+symbols,}')
EP_VAL    = '{/localdev/rmalhotra/Keraunos_PCIE_Tile/software/pcie_bringup/pcie_bringup.elf,,,image+symbols,}'

with open(PROPS) as f:
    xml = f.read()

# Find and patch the two /ImageInfo/cpu0/initial_image blocks
blocks = xml.split('/ImageInfo/cpu0/initial_image')
assert len(blocks) == 3, f"Expected 2 occurrences, got {len(blocks)-1}"

def patch_value(block, new_val):
    return re.sub(r'(<value>).*?(</value>)', rf'\g<1>{new_val}\2', block, count=1, flags=re.DOTALL)

blocks[1] = patch_value(blocks[1], HOST_VAL)
blocks[2] = patch_value(blocks[2], EP_VAL)

with open(PROPS, 'w') as f:
    f.write('/ImageInfo/cpu0/initial_image'.join(blocks))
print("Properties.xml patched successfully.")
EOF
```

Verify:
```bash
grep -A2 "initial_image" /localdev/rmalhotra/Keraunos_PCIE_Tile/generated/default/Properties.xml \
    | grep "<value>"
# Expected:
#   vmlinux,,,symbols,} {... fw_payload.elf,,,image+symbols,}
#   pcie_bringup.elf,,,image+symbols,}
```

### 5.5 Launch Virtualizer

Use the provided launch script from a **VNC or X11 session** (GUI required):

```bash
bash /localdev/rmalhotra/Keraunos_PCIE_Tile/launch_virtualizer.sh
```

Or manually:
```bash
/tools_vendor/synopsys/virtualizer-tool-elite/V-2024.03/SLS/linux/virtualizerstudio/vs \
    -data /localdev/rmalhotra/Keraunos_PCIE_Tile
```

### 5.6 Start the Simulation and Release CPUs from Reset

1. In Virtualizer, press the **green Run (▶) button**
2. The simulation starts but the Host CPU is held in reset waiting for GDB. Look at the
   **GDB console tab** at the bottom (labeled `cpu0>`) and type:

   ```
   continue_simulation
   ```

3. Both CPUs are now running. The Keraunos EP CPU will complete PCIe link training
   quickly (~1-2 minutes real time). Watch the EP UART tab for:
   ```
   [PCIe FW] === PCIe Bringup PASSED ===
   ```

4. The Host CPU runs OpenSBI and then Linux very slowly due to the cycle-accurate
   PCIe model (~17–28 ns simulated per real second). **Leave the simulation running
   overnight** — expect 30–60 minutes of real time before the first OpenSBI line appears.

### 5.7 Monitor Boot Progress

**Keraunos EP UART** (appears quickly, confirms firmware is loaded):
- Tab: `Keraunos_PCIE_Tile.Keraunos_PCIE_Chiplet.SMC_Configure.Peripherals.UART_PHY`

**Host UART** (appears after ~30–60 min real time):
- Tab: `Keraunos_PCIE_Tile.Host_Chiplet.SMC.Peripherals.UART_PHY`
- Expected output sequence:
  ```
  OpenSBI v1.x
  ...
  Linux version 6.6.30 ...
  [    0.000000] Machine model: Keraunos VDK Host_Chiplet
  ...
  =============================================
    RISC-V Host Linux — Keraunos PCIe Bringup
  =============================================
  / #
  ```

Check simulation time from the GDB console to confirm progress:
```
cpu0> get_current_time
```

> **Note:** The `simout_*.txt` file stops updating once the EP bringup completes
> (~474 ns). This is normal — the Host CPU executes silently inside the Imperas ISS
> with no SystemC-level output. Use the UART tab and GDB console to monitor progress.

### 5.8 Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `Unable to load image` — sim aborts | Wrong format (missing `{ }`) | Add curly braces: `{/path/to.elf,,,image+symbols,}` |
| Both UARTs completely silent | `initial_image` values are empty | Check/repatch `Properties.xml` |
| Host UART silent, EP UART works | `fw_payload.elf` built without Linux | Rebuild with `FW_PAYLOAD_PATH` set; verify with `grep FW_PAYLOAD_PATH autoconf.h` |
| "could not find vp config" | `snps.vpproject` points to missing config | Set `activeConfig="default/default"` |
| CPU shows "in reset state" | GDB stub waiting for debugger | In GDB console tab type: `continue_simulation` |
| Simulation exits immediately | No VNC/display for GUI | Run `vs` from VNC terminal, not plain SSH |
| OpenSBI boots but Linux never prints | DTB not embedded — wrong `make` flag | Use `FW_FDT_PATH` (not `FW_PAYLOAD_FDT_PATH`) in the OpenSBI build |
| EP firmware `exit(0)` kills simulation | Semihosting intercepts the exit ecall | Set `defaultsemihost=false` for EP CPU in `default.vpcfg` (§5.4.2) |
| simout flooded with undecoded-address warnings | SharedMemoryMap stub warnings | Set `enable_warnings=false` for both SharedMemoryMap stubs in `default.vpcfg` (§5.4.2) |
| Kernel crashes immediately after OpenSBI handoff (no Linux output) | Kernel compiled with `CONFIG_FPU=y` but VDK CPU is `rv64imac` (no F/D) | Disable FPU: `./scripts/config --disable CONFIG_FPU`, rebuild kernel + OpenSBI (§2.3) |
| "System is deadlocked on memory" panic during initramfs unpack | DTB declares too little DRAM (e.g. 64 MB) for kernel + initramfs | Increase DTB `memory` `reg` size to `0x10000000` (256 MB); rebuild DTB + OpenSBI (§3.2) |
| "Attempted to kill init! exitcode=0x00000004" (SIGILL in init) | BusyBox compiled with `lp64d` (hard-float ABI) but CPU is `rv64imac` | Rebuild BusyBox with the musl soft-float toolchain `riscv64-unknown-linux-musl-` (§1.5, §4.1) |
| "Warning: unable to open an initial console" | `/dev/console` device node missing from initramfs | Use `fakeroot` to bake `/dev/console` (c 5 1), `/dev/null`, `/dev/zero`, `/dev/ttyS0` into `initramfs.cpio.gz` (§4.4) |
| "Attempted to kill init! exitcode=0x00000000" (init exits cleanly) | `/bin/sh` receives EOF on stdin (no console attached) | Use `exec /bin/sh` (inherits kernel-opened fds) in init; do NOT redirect to `/dev/ttyS0` explicitly (§4.3) |
| Kernel boots, init runs, but no shell output on UART | DTS UART `compatible` string does not match VDK hardware (DesignWare) | Set `compatible = "snps,dw-apb-uart"` in DTS `uart0` node; rebuild DTB + OpenSBI (§3.2) |
| Init runs (`Run /init` appears) but no userspace output, no panic | UART DTS node declares `interrupts` but VDK PLIC doesn't wire the UART IRQ; tty TX hangs waiting for THRI interrupt | **Remove** `interrupts` and `interrupt-parent` from UART DTS node — 8250 driver falls back to timer-based polling (§3.2, §4.3) |
| `makeinfo: command not found` during toolchain build | RHEL 8 lacks texinfo | Create a stub `makeinfo` script (§1.5 Step 2) |
| `ccache: error: No space left on device` during build | `~/.ccache` fills small `/home` partition | Run `ccache -C` and `export CCACHE_DISABLE=1` before building |


---

## Phase 6: Verify PCIe Enumeration (lspci)

Once Linux is running on the Host_Chiplet in the VDK, check that Keraunos
is discovered by the PCIe subsystem.

### 6.1 Basic Enumeration

```bash
# From the Linux shell running on Host_Chiplet:
lspci
# Expected output — Tenstorrent Grendel (from pcie_config.h):
# 0000:00:00.0 PCI bridge: Synopsys, Inc. DWC PCIe Root Complex
# 0000:01:00.0 Processing accelerators: Tenstorrent Device 1e52:feed

lspci -v   # verbose: shows BARs, capabilities, interrupt pin
lspci -vvv # very verbose: full PCIe capability structures
lspci -xxx # raw hex config space dump
```

### 6.2 What Keraunos Should Expose

Vendor and Device IDs come from `pcie_config.h`:

```c
PCIE_VENDOR_ID_TENSTORRENT = 0x1E52
PCIE_DEVICE_ID_GRENDEL     = 0xFEED
PCIE_CLASS_CODE_PROCESSING = 0x0B4000  /* Processing accelerator */
```

After enumeration, Linux should see these BARs (sizes from `pcie_config.h`):

| BAR | Index | Size | VDK path | Post-TLB route | Purpose |
|---|---|---|---|---|---|
| BAR0/1 (64-bit) | 0 | 4 GB | APP0 channel | TLBAppIn0 → NOC-N | Quasar data memory |
| BAR2/3 (64-bit) | 2 | 1 MB | SYSIN0 channel | TLBSysIn0 → SMN | SMC / SEP config registers |
| BAR4/5 (64-bit) | 4 | 512 GB | APP1 channel | TLBAppIn1 → NOC-N | Large DRAM window |

> **VDK-specific:** The `pcie_bringup` firmware on `SMC_Configure` programs BAR
> sizes via DBI **before** the host runs. The DWC PCIe VP model does not allow
> BAR base address programming from the RC config space — `pcie_bringup` sets
> the base. Confirm `system_ready` (`0x1804FFFC`) = `1` and
> `pcie_inbound_app_enable` (`0x1804FFF8`) is set before host enumeration.

> **VDK LTSSM note:** `app_ltssm_en` on the RC is not connected in the current
> `.vdksys`, so LTSSM stays in `DETECT_QUIET`. However, the VP model routes TLPs
> over the PCIe wire without a trained link — enumeration and memory transactions
> can still succeed. See `pcie_e2e_test.c` TEST 3 comments.

```bash
# Expected lspci -v output for Keraunos:
# 0000:01:00.0 Processing accelerators: Tenstorrent Device feed (rev 00)
#         Subsystem: ...
#         Flags: bus master, fast devsel, latency 0
#         Memory at <addr> (64-bit, non-prefetchable) [size=4G]    ← BAR0 APP0
#         Memory at <addr> (64-bit, non-prefetchable) [size=1M]    ← BAR2 SYSIN0
#         Memory at <addr> (64-bit, prefetchable) [size=512G]      ← BAR4 APP1

# Check kernel messages for PCIe activity:
dmesg | grep -i pci
dmesg | grep -i "1e52\|feed\|keraunos\|tenstorrent"
```

### 6.3 Forcing Re-enumeration

```bash
# Rescan PCIe bus (useful during bringup iterations)
echo 1 > /sys/bus/pci/rescan

# Check PCIe link status
cat /sys/bus/pci/devices/0000:01:00.0/current_link_speed
cat /sys/bus/pci/devices/0000:01:00.0/current_link_width
```

### 6.4 Checking VDK Simulation Log

The VDK simulator logs ATU programming and TLP activity to `simout_6.txt`:

```bash
# On the build machine, monitor the VDK output:
tail -f /localdev/rmalhotra/Keraunos_PCIE_Tile/bin/export/simout_<latest>.txt \
    grep -E "ATU|TLP|PCIe_Wire|handle_write"

# Key strings to look for:
# '[ATU][OutBound]'     — iATU translation fired
# '[PCIe_Wire] Sending' — TLP sent over simulated PCIe link
# 'handle_write_IATU'   — ATU register programmed by Linux DW PCIe driver
# 'system_ready'        — EP firmware set system_ready flag
```

---

## Phase 7: Write Driver and Application

### 7.1 Keraunos PCIe Kernel Driver

Create an out-of-tree kernel module on the RISC-V host:

```bash
mkdir -p $RISCV_WS/keraunos-driver
cd $RISCV_WS/keraunos-driver
```

**`keraunos_pcie.c`:**

```c
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/io.h>

/* Vendor/Device IDs from pcie_config.h (Keraunos VDK) */
#define KERAUNOS_VENDOR_ID   0x1E52   /* Tenstorrent */
#define KERAUNOS_DEVICE_ID   0xFEED   /* Grendel */
#define DRIVER_NAME          "keraunos_pcie"

/*
 * Keraunos PCIe BAR layout (from pcie_config.h — Grendel package):
 *
 *   BAR0/1 (64-bit, 4 GB):   APP0 channel → TLBAppIn0 → NOC-N → Quasar data
 *   BAR2/3 (64-bit, 1 MB):   SYSIN0 channel → TLBSysIn0 → SMN → SMC/SEP regs
 *   BAR4/5 (64-bit, 512 GB): APP1 channel → TLBAppIn1 → NOC-N → large DRAM
 *
 * In the VDK, BAR base addresses are programmed by the pcie_bringup firmware
 * running on the Keraunos_PCIE_Chiplet SMC_Configure CPU via DBI before the
 * host enumerates. The DWC PCIe VP model does not allow RC-side BAR writes.
 *
 * system_ready register:          SMN offset 0x1804FFFC
 * pcie_inbound_app_enable:        SMN offset 0x1804FFF8
 * Both must be set by SMC firmware before any inbound TLPs are forwarded.
 */
#define NOC_N_QUASAR_OFFSET  0x00000000ULL   /* BAR0: start of Quasar memory */
#define SMN_SMC_OFFSET       0x00000000ULL   /* BAR2: SMC register base via SMN */
#define SMN_SEP_OFFSET       0x00040000ULL   /* BAR2: SEP register base (TLB_CFG) */
#define SYSTEM_READY_SMN_OFF 0x1804FFFCULL   /* system_ready flag (SMN-IO) */
#define ACCESS_CTRL_SMN_OFF  0x1804FFF8ULL   /* pcie_inbound_app_enable */

struct keraunos_dev {
    struct pci_dev  *pdev;
    void __iomem    *bar0;   /* APP0  / NOC-N / Quasar data path (BAR0, 4 GB) */
    void __iomem    *bar2;   /* SYSIN0 / SMN  / SMC+SEP config (BAR2, 1 MB) */
    resource_size_t  bar0_len;
    resource_size_t  bar2_len;
    struct cdev      cdev;
    dev_t            devno;
};

static struct keraunos_dev *kdev;
static struct class *keraunos_class;

/* ---------- PCIe probe / remove ---------- */

static int keraunos_probe(struct pci_dev *pdev,
                          const struct pci_device_id *id)
{
    int rc;

    kdev = devm_kzalloc(&pdev->dev, sizeof(*kdev), GFP_KERNEL);
    if (!kdev) return -ENOMEM;
    kdev->pdev = pdev;

    rc = pci_enable_device(pdev);
    if (rc) { dev_err(&pdev->dev, "pci_enable_device failed\n"); return rc; }

    /* Enable Bus Master — required for Keraunos NOC→PCIe outbound TLPs.
     * Without BME set, outbound writes from Quasar/NOC are blocked.
     * Controlled by PCIE_CFG_ACCESS_CTRL_REG (0x1804FFF8) on the EP side
     * alongside o_pcie_inbound_app_enable (keraunos_soc_reg.h). */
    pci_set_master(pdev);

    rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
    if (rc) {
        rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
        if (rc) { dev_err(&pdev->dev, "No suitable DMA mask\n"); goto err_disable; }
    }

    rc = pci_request_regions(pdev, DRIVER_NAME);
    if (rc) { dev_err(&pdev->dev, "pci_request_regions failed\n"); goto err_disable; }

    /* Map BAR0 (APP0 / NOC-N / Quasar data path, 4 GB).
     * pcie_bringup firmware programs BAR0 base via DBI on the EP side.
     * TLBAppIn0 translates inbound PCIe addr → internal NOC-N address. */
    kdev->bar0_len = pci_resource_len(pdev, 0);
    kdev->bar0     = pci_iomap(pdev, 0, kdev->bar0_len);
    if (!kdev->bar0) {
        dev_err(&pdev->dev, "Failed to map BAR0 (APP0/NOC-N)\n");
        rc = -ENOMEM;
        goto err_release;
    }

    /* Map BAR2 (SYSIN0 / SMN / SMC+SEP config path, 1 MB).
     * BAR2 index = 2 because BAR0/1 form a 64-bit pair.
     * TLBSysIn0 translates inbound PCIe addr → SMN register space.
     * system_ready and pcie_inbound_app_enable must be set by SMC
     * firmware (pcie_bringup) before this BAR is accessible. */
    kdev->bar2_len = pci_resource_len(pdev, 2);
    kdev->bar2     = pci_iomap(pdev, 2, kdev->bar2_len);
    if (!kdev->bar2) {
        dev_err(&pdev->dev, "Failed to map BAR2 (SYSIN0/SMN)\n");
        rc = -ENOMEM;
        goto err_unmap_bar0;
    }

    pci_set_drvdata(pdev, kdev);

    dev_info(&pdev->dev, "Keraunos (Tenstorrent Grendel) PCIe EP probed\n");
    dev_info(&pdev->dev, "  BAR0 APP0/NOC-N: phys=0x%llx len=0x%llx\n",
             (u64)pci_resource_start(pdev, 0), (u64)kdev->bar0_len);
    dev_info(&pdev->dev, "  BAR2 SYSIN0/SMN: phys=0x%llx len=0x%llx\n",
             (u64)pci_resource_start(pdev, 2), (u64)kdev->bar2_len);

    /* Sanity read: SMC register base via SMN (BAR2 offset 0x0) */
    dev_info(&pdev->dev, "  SMN[0x0] = 0x%08x\n",
             ioread32(kdev->bar2 + SMN_SMC_OFFSET));

    return 0;

err_unmap_bar0:
    pci_iounmap(pdev, kdev->bar0);
err_release:
    pci_release_regions(pdev);
err_disable:
    pci_disable_device(pdev);
    return rc;
}

static void keraunos_remove(struct pci_dev *pdev)
{
    struct keraunos_dev *k = pci_get_drvdata(pdev);
    pci_iounmap(pdev, k->bar2);
    pci_iounmap(pdev, k->bar0);
    pci_release_regions(pdev);
    pci_disable_device(pdev);
    dev_info(&pdev->dev, "Keraunos driver removed\n");
}

/* ---------- PCIe device table ---------- */

static const struct pci_device_id keraunos_ids[] = {
    { PCI_DEVICE(KERAUNOS_VENDOR_ID, KERAUNOS_DEVICE_ID) },
    { 0 }
};
MODULE_DEVICE_TABLE(pci, keraunos_ids);

static struct pci_driver keraunos_driver = {
    .name     = DRIVER_NAME,
    .id_table = keraunos_ids,
    .probe    = keraunos_probe,
    .remove   = keraunos_remove,
};

module_pci_driver(keraunos_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Keraunos Bringup Team");
MODULE_DESCRIPTION("Keraunos Chiplet PCIe Endpoint Driver");
MODULE_VERSION("0.1");
```

**`Makefile`:**

```makefile
KDIR   ?= /localdev/rmalhotra/riscv-linux/linux-6.6.30
ARCH   := riscv
CROSS  := riscv64-unknown-linux-gnu-

obj-m := keraunos_pcie.o

all:
	$(MAKE) -C $(KDIR) M=$(PWD) \
	    ARCH=$(ARCH) CROSS_COMPILE=$(CROSS) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) \
	    ARCH=$(ARCH) CROSS_COMPILE=$(CROSS) clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) \
	    ARCH=$(ARCH) CROSS_COMPILE=$(CROSS) \
	    INSTALL_MOD_PATH=/localdev/rmalhotra/riscv-linux/initramfs \
	    modules_install
```

**Build and install the module:**

```bash
cd $RISCV_WS/keraunos-driver
make
make install   # copies .ko into initramfs/lib/modules/
```

### 7.2 Userspace Test Application

A simple userspace program (no kernel module needed) using `/dev/mem` or a UIO
driver to send transactions to Keraunos:

**`keraunos_test.c`:**

```c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>

/*
 * BAR physical addresses are dynamically assigned by Linux during PCIe
 * enumeration from within the AXI_Slave outbound window (0x70000000–0x7FFFFFFF,
 * confirmed from mem_map_top). Run 'lspci -v' after boot to get the actual
 * addresses assigned by your VDK run, then patch these defines.
 *
 * BAR layout (from pcie_config.h / pcie_init.c):
 *   BAR0/1 (64-bit, 4 GB)   — APP0  / NOC-N → Quasar
 *   BAR2/3 (64-bit, 1 MB)   — SYSIN0 / SMN  → SMC+SEP config regs
 *   BAR4/5 (64-bit, 512 GB) — APP1  / NOC-N → large DRAM
 */
#define BAR0_PHYS  0x0UL          /* set from lspci output after VDK boot */
#define BAR2_PHYS  0x0UL          /* set from lspci output after VDK boot */
#define BAR0_SIZE  0x100000000UL  /* 4 GB  — PCIE_BAR0_SIZE (pcie_config.h) */
#define BAR2_SIZE  0x00100000UL   /* 1 MB  — PCIE_BAR2_SIZE (pcie_config.h) */

/* Keraunos internal address offsets (within BAR windows) */
#define QUASAR_MEM_OFFSET  0x00000000  /* BAR0: start of Quasar memory via NOC-N */
#define SMN_BASE_OFFSET    0x00000000  /* BAR2: SMC register base via SMN */
#define SMN_TLB_CFG_OFF    0x00040000  /* BAR2: TLB config registers (PCIE_CFG_SMNIO_TLB_CFG - base) */

int main(void)
{
    int fd;
    void *bar0, *bar1;
    uint32_t val;

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "open /dev/mem: %s\n"
                        "Hint: run as root, or use a UIO/keraunos driver\n",
                strerror(errno));
        return 1;
    }

    /* Map BAR0: NOC-N → Quasar memory */
    bar0 = mmap(NULL, BAR0_SIZE, PROT_READ | PROT_WRITE,
                MAP_SHARED, fd, BAR0_PHYS);
    if (bar0 == MAP_FAILED) {
        perror("mmap BAR0"); close(fd); return 1;
    }

    /* Map BAR1: SMN → SMC registers */
    bar1 = mmap(NULL, BAR1_SIZE, PROT_READ | PROT_WRITE,
                MAP_SHARED, fd, BAR1_PHYS);
    if (bar1 == MAP_FAILED) {
        perror("mmap BAR1"); munmap(bar0, BAR0_SIZE); close(fd); return 1;
    }

    printf("=== Keraunos PCIe Transaction Test ===\n\n");

    /* 1. Read SMC version register (SMN path) */
    val = *(volatile uint32_t *)((uint8_t *)bar1 + SMC_VER_REG);
    printf("[SMN] SMC version register:  0x%08x\n", val);

    val = *(volatile uint32_t *)((uint8_t *)bar1 + SMC_STATUS_REG);
    printf("[SMN] SMC status register:   0x%08x\n", val);

    /* 2. Write a test pattern to Quasar memory (NOC-N path) */
    printf("\n[NOC-N] Writing test pattern to Quasar memory at offset 0x0...\n");
    *(volatile uint64_t *)((uint8_t *)bar0 + QUASAR_MEM_OFFSET) = 0xDEADBEEFCAFEBABEULL;

    /* 3. Read it back */
    uint64_t readback = *(volatile uint64_t *)((uint8_t *)bar0 + QUASAR_MEM_OFFSET);
    printf("[NOC-N] Readback: 0x%016llx  %s\n",
           (unsigned long long)readback,
           (readback == 0xDEADBEEFCAFEBABEULL) ? "PASS" : "FAIL");

    munmap(bar1, BAR1_SIZE);
    munmap(bar0, BAR0_SIZE);
    close(fd);
    return 0;
}
```

**Cross-compile the test app:**
```bash
riscv64-unknown-linux-gnu-gcc -O2 -static \
    -o $RISCV_WS/initramfs/bin/keraunos_test \
    keraunos_test.c

# Run on RISC-V host after boot:
# / # keraunos_test
```

---

## Phase 8: VDK Simulation Verification

In the VDK the **Keraunos_PCIE_Chiplet is already the SystemC model** — there
is no separate simulation process or socket bridge needed.  The entire system
(Host_Chiplet running Linux + Keraunos_PCIE_Chiplet) runs together inside a
single Virtualizer Elite simulator process.

### 8.1 Full-System Simulation Architecture

```
x86_64 Development Machine — single scrun process
┌──────────────────────────────────────────────────────────────┐
│  Virtualizer Elite (scrun)                                   │
│                                                              │
│  ┌───────────────────────┐   PCIe TLM-2.0   ┌────────────┐  │
│  │  Host_Chiplet         │  ◄─────────────►  │  Keraunos  │  │
│  │  TT_Rocket_LT (RISC-V)│                   │  PCIE      │  │
│  │  Linux OS             │   (internal VP    │  Chiplet   │  │
│  │  PCIe RC driver       │    bus)           │  (SystemC) │  │
│  │  keraunos_pcie.ko     │                   │  EP model  │  │
│  └───────────────────────┘                   └────────────┘  │
│                                                              │
│  simout_6.txt  ← console UART output                         │
└──────────────────────────────────────────────────────────────┘
```

### 8.2 Verifying the Simulation

Once the VDK is running (see Phase 5), verify each layer:

**1. Kernel boot — UART console (`simout_6.txt`):**

```
[    0.000000] Linux version 6.6.30 ...
[    0.000000] OF: fdt: Machine model: Keraunos VDK Host_Chiplet — TT_Rocket_LT RISC-V
[    0.xxx] PCI: Probing PCI hardware
[    0.xxx] pci 0000:00:00.0: [1e52:feed] type 00 class 0x020000
```

**2. PCIe enumeration (from Linux shell via VDK console):**

```bash
lspci -vv
# Expected: 1e52:feed Tenstorrent Keraunos, BAR0 64-bit mem 4GB, BAR2 1MB
```

**3. Driver load:**

```bash
insmod keraunos_pcie.ko
dmesg | grep keraunos
# Expected: keraunos_pcie 0000:00:00.0: Keraunos PCIe device found
```

**4. TLP traffic — Virtualizer transcript:**

```bash
# In the scrun output look for SystemC model prints:
grep -i "pcie\|tlp\|bar" simout_6.txt
```

### 8.3 VDK Debug Tips

| Issue | Where to look |
|-------|---------------|
| Linux does not boot | `simout_6.txt` — check early UART output at `0xC000A000` |
| PCIe link not up | VDK transcript — search for `LTSSM` state transitions |
| Driver probe fails | `dmesg` — check BAR mapping, VID/DID mismatch |
| TLPs not reaching EP | Keraunos_PCIE_Chiplet SystemC trace output |
| VDK bus errors | `VDKSYS_CHANGES_REQUIRED.md` — bus connections may be missing |

---

## Summary: Build Sequence

```bash
# ============================================================
# All steps, in order, from a clean workspace
# ============================================================

export RISCV_TC=/proj_perf/asc/tools/asc-toolchain/tt-riscv-toolchain-20240125
export PATH=$RISCV_TC/bin:$PATH
export RISCV_WS=/localdev/rmalhotra/riscv-linux
export ARCH=riscv
export CROSS_COMPILE=riscv64-unknown-linux-gnu-
export LOCAL_LIBS=/localdev/rmalhotra/local-libs/install
export LD_LIBRARY_PATH=$LOCAL_LIBS/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export CCACHE_DISABLE=1

VDK=/localdev/rmalhotra/Keraunos_PCIE_Tile

# ----- 0. Build the soft-float musl toolchain (one-time) -----
cd $RISCV_WS
git clone --depth 1 https://github.com/riscv-collab/riscv-gnu-toolchain.git rv64imac-toolchain-src
mkdir -p $RISCV_WS/bin_shim
printf '#!/bin/sh\necho "makeinfo (stub)"' > $RISCV_WS/bin_shim/makeinfo
chmod +x $RISCV_WS/bin_shim/makeinfo
export PATH=$RISCV_WS/bin_shim:$PATH

cd rv64imac-toolchain-src
./configure --prefix=$RISCV_WS/rv64imac-toolchain --with-arch=rv64imac --with-abi=lp64
make musl -j$(nproc)
export PATH=$RISCV_WS/rv64imac-toolchain/bin:$PATH

# ----- 1. BusyBox (must use musl soft-float toolchain) -----
cd $RISCV_WS/busybox-1.36.1
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-musl- defconfig
sed -i 's/# CONFIG_STATIC.*/CONFIG_STATIC=y/' .config
sed -i 's/CONFIG_TC=y/# CONFIG_TC is not set/' .config
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-musl- -j$(nproc)
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-musl- install

# ----- 2. initramfs (with device nodes via fakeroot) -----
cd $RISCV_WS
mkdir -p initramfs/{bin,sbin,etc,proc,sys,dev,tmp}
cp -a busybox-1.36.1/_install/* initramfs/
# (write initramfs/init as shown in Phase 4.3)
fakeroot sh -c '
cd initramfs &&
mknod -m 622 dev/console c 5 1 &&
mknod -m 666 dev/null    c 1 3 &&
mknod -m 666 dev/zero    c 1 5 &&
mknod -m 666 dev/ttyS0   c 4 64 &&
find . | cpio -H newc -o 2>/dev/null
' | gzip > initramfs.cpio.gz

# ----- 3. Kernel (CONFIG_FPU=n — VDK CPU is rv64imac, no F/D) -----
cd $RISCV_WS/linux-6.6.30
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- defconfig
./scripts/config --disable CONFIG_FPU
./scripts/config --enable CONFIG_PCI --enable CONFIG_PCIE_DW \
    --enable CONFIG_PCIE_DW_HOST --enable CONFIG_BLK_DEV_INITRD \
    --enable CONFIG_DEVTMPFS --enable CONFIG_DEVTMPFS_MOUNT
./scripts/config --set-str CONFIG_INITRAMFS_SOURCE "$RISCV_WS/initramfs.cpio.gz"
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- olddefconfig
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- -j$(nproc)

# ----- 4. DTB -----
$RISCV_WS/dtc/dtc -I dts -O dtb \
    -o $RISCV_WS/smc_pcie_tile.dtb \
       $RISCV_WS/smc_pcie_tile.dts

# ----- 5. OpenSBI (fw_payload.elf — embeds kernel + DTB) -----
cd $RISCV_WS/opensbi
make clean && \
make -j$(nproc) \
    CROSS_COMPILE=riscv64-unknown-linux-gnu- \
    PLATFORM=generic \
    PLATFORM_RISCV_XLEN=64 \
    PLATFORM_RISCV_ISA=rv64imac_zicsr_zifencei \
    PLATFORM_RISCV_ABI=lp64 \
    FW_PAYLOAD=y \
    FW_TEXT_START=0x80000000 \
    FW_FDT_PATH=$RISCV_WS/smc_pcie_tile.dtb \
    FW_PAYLOAD_PATH=$RISCV_WS/linux-6.6.30/arch/riscv/boot/Image

# ----- 6. VDK: verify vpcfg, patch Properties.xml, launch -----
cd $VDK
# Patch Properties.xml with Python snippet from §5.4.3, then launch:
bash $VDK/launch_virtualizer.sh

# ----- 7. Watch boot on simulated UART (Host_Chiplet UART tab) -----
```

---

## VDK Address Reference

All values confirmed from the VDK source at
`/localdev/rmalhotra/Keraunos_PCIE_Tile`.

### Host_Chiplet Memory Map (CPU view)

| Address | Size | VDK Component | Source |
|---|---|---|---|
| `0x4400_0000` | 4 MB | PCIE_RC DBI registers | `mem_map_top`, `pcie_e2e_test.c` |
| `0x4430_0000` | 128 KB | PCIE_RC iATU (DBI CS2) | `pcie_e2e_test.c` |
| `0x7000_0000` | 256 MB | PCIE_RC AXI_Slave (outbound PCIe window) | `mem_map_top`, `pcie_e2e_test.c` |
| `0x8000_0000` | 1 GB (bus), 256 MB (DTB) | Host DRAM | `Properties.xml` range_mappings, DTB |
| `0xC000_A000` | 256 B | UART (DW_apb_uart) | `pcie_e2e_test.c` |
| `0xC400_0000` | — | PLIC | `software/src/main.c` |

### Keraunos_PCIE_Chiplet Memory Map (SMC_Configure CPU view)

| Address | Size | Region | Source |
|---|---|---|---|
| `0x0000_0000` | 32 MB | Internal RAM (firmware code) | `pcie_config.h` |
| `0x1800_0000` | 8 MB | PCIE_TILE smn_n_target (config regs) | `mem_map_top`, `pcie_config.h` |
| `0x1800_0000` | 256 KB | SMN-IO MSI Relay config | `pcie_config.h` |
| `0x1804_0000` | 64 KB | SMN-IO TLB config registers | `pcie_config.h` |
| `0x1804_FFF8` | 4 B | `pcie_inbound_app_enable` / access ctrl | `pcie_config.h` |
| `0x1804_FFFC` | 4 B | `system_ready` flag | `pcie_config.h` |
| `0x1805_0000` | 64 KB | SMN-IO Fabric CSR | `pcie_config.h` |
| `0x1808_0000` | 256 KB | PHY AHB (firmware download) | `pcie_config.h` |
| `0x180C_0000` | 256 KB | PHY APB (control regs) | `pcie_config.h` |
| `0x1810_0000` | 1 MB | SII APB Demux | `pcie_config.h` |
| `0x1840_0000` | — | DBI via TLBSysOut0 | `pcie_config.h` |
| `0x4400_0000` | 4 MB | PCIe_EP AXI_DBI | `mem_map_top` |
| `0x8000_0000` | 64 MB | Internal DRAM | `pcie_config.h` |

### Keraunos PCIe EP BAR Layout

| BAR | Index | Size | VDK Channel | TLB path | Purpose |
|---|---|---|---|---|---|
| BAR0/1 (64-bit) | 0 | **4 GB** | APP0 | TLBAppIn0 → NOC-N | Quasar data memory |
| BAR2/3 (64-bit) | 2 | **1 MB** | SYSIN0 | TLBSysIn0 → SMN | SMC / SEP config regs |
| BAR4/5 (64-bit) | 4 | **512 GB** | APP1 | TLBAppIn1 → NOC-N | Large DRAM window |

### Identification

| Parameter | Value | Source |
|---|---|---|
| Vendor ID | `0x1E52` | `pcie_config.h` `PCIE_VENDOR_ID_TENSTORRENT` |
| Device ID | `0xFEED` | `pcie_config.h` `PCIE_DEVICE_ID_GRENDEL` |
| Class code | `0x0B4000` | `pcie_config.h` `PCIE_CLASS_CODE_PROCESSING` |
| Default link width | x16 | `pcie_config.h` `PCIE_DEFAULT_MAX_WIDTH` |
| Default link speed | Gen6 | `pcie_config.h` `PCIE_DEFAULT_MAX_SPEED` |

---

## References

- [Linux RISC-V docs](https://www.kernel.org/doc/html/latest/riscv/index.html)
- [Linux PCI driver guide](https://www.kernel.org/doc/html/latest/PCI/pci.html)
- [Synopsys DW PCIe Linux driver](https://elixir.bootlin.com/linux/latest/source/drivers/pci/controller/dwc)
- [RISC-V Device Tree bindings](https://www.kernel.org/doc/Documentation/devicetree/bindings/riscv/)
- [riscv-gnu-toolchain releases](https://github.com/riscv-collab/riscv-gnu-toolchain/releases)
- VDK platform config: `Keraunos_PCIE_Tile/vpconfigs/default/default.vpcfg`
- VDK bus prerequisites: `Keraunos_PCIE_Tile/VDKSYS_CHANGES_REQUIRED.md`
- VDK memory map: `Keraunos_PCIE_Tile/bin/artefacts/mem_map_top`
