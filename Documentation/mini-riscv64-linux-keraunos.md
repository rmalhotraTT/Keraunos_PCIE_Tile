# Mini RISC-V Linux for Keraunos PCIE Tile (host)

This document tracks bringing **host Linux** artifacts into `software/mini-riscv64-linux/output/` and pointing VP at them. The host `initial_image` uses **`fw_payload.elf` with empty load addresses** so the simulator applies **ELF program headers** (entry **0x80000000** for this OpenSBI build) — same pattern as `vpconfigs/default/default.vpcfg`.

> **Do not** copy Ascalon’s `{fw_payload,0x00000000,...}` line blindly: Keraunos `fw_payload.elf` is linked at **0x80000000**. Forcing load at **0x0** breaks execution (bogus PC, debugger “No connection”, crash).

## Goals

- Faster **host** Linux bring-up in simulation vs. pointing VP only at a monolithic path under `riscv-linux/`.
- Keep **device** firmware (`pcie_bringup.elf` on `Keraunos_PCIE_Chiplet`) unchanged unless you explicitly change it.

## Layout in this repo

| Path | Purpose |
|------|---------|
| `software/mini-riscv64-linux/` | Scripts, DTS, and (generated) `output/` |
| `software/mini-riscv64-linux/output/` | **`vmlinux`**, **`fw_payload.elf`**, `keraunos_host.dtb`, … — **not committed** (see `output/.gitignore`) |
| `vpconfigs/mini_riscv64_linux/` | VP config: host Rocket uses two-image mini-linux paths |
| `software/mini-riscv64-linux/vdk-linux-build-keraunos-host.sh` | Sync or full rebuild |

## Populate `output/` (recommended: reuse your `riscv-linux` tree)

If you already build Linux/OpenSBI under **`/localdev/rmalhotra/riscv-linux`** (or set **`RISCV_LINUX_ROOT`**), run:

```bash
cd software/mini-riscv64-linux
./vdk-linux-build-keraunos-host.sh --sync
# or simply (auto-detects RISCV_LINUX_ROOT on this machine):
./vdk-linux-build-keraunos-host.sh
```

This copies:

- `linux-*/vmlinux` → `output/vmlinux`
- `opensbi/build/platform/generic/firmware/fw_payload.elf` → `output/fw_payload.elf`
- `riscv-host-keraunos.dtb` → `output/keraunos_host.dtb`

**Source of truth for the device tree** (keep in sync when the VP memory map changes):

- `/localdev/rmalhotra/riscv-linux/riscv-host-keraunos.dts`  
- Copy in-repo: `software/mini-riscv64-linux/DTS/keraunos_host.dts`

## VP configuration

- **`snps.vpproject`** is set to **`activeConfig="mini_riscv64_linux/mini_riscv64_linux"`** so the host uses `output/vmlinux` + `output/fw_payload.elf`.
- To go back to the single-image host load from `../../../riscv-linux/opensbi/...`, set **`activeConfig`** to **`default/default`**.

## Full rebuild (`--full`)

Requires `dtc`, a RISC-V cross compiler, **`LINUX_SRC`**, **`OPENSBI_SRC`**, and **`ROOTFS_CPIO`** (uncompressed cpio path for `INITRAMFS_SOURCE`). Example:

```bash
export LINUX_SRC=/path/to/linux-6.6.30
export OPENSBI_SRC=/path/to/opensbi
export ROOTFS_CPIO=/path/to/rootfs.cpio
./vdk-linux-build-keraunos-host.sh --full
```

Default `CROSS_COMPILE` targets **`riscv64-unknown-linux-musl-`** and prepends `/localdev/rmalhotra/riscv-linux/rv64imac-toolchain/bin` to `PATH` when using this script.

## Verification checklist

- [ ] `output/fw_payload.elf` exists (after `--sync` or `--full`). (`vmlinux` in `output/` is optional for source debug; not required for the single-image VP line.)
- [ ] `readelf -l output/fw_payload.elf` shows **VirtAddr** loads at **0x8000…** — VPC must **not** override with `0x0`.
- [ ] `DTS/keraunos_host.dts` matches `riscv-host-keraunos.dts` when the host map changes.
- [ ] Simulation: OpenSBI → Linux → console on **UART @ 0xC000A000** (per DTS `bootargs`).

## If the host “crashes” or debugger shows nonsense PC

1. **Image load address** — confirm VPC uses **`fw_payload.elf,,,image+symbols`** (empty address), not `...,0x00000000,...`.
2. **Kernel / userspace ISA** — follow **`Documentation/keraunos-host-riscv-linux.md`** (version1): Host CPU is **rv64imac**; **`CONFIG_FPU=n`** in the kernel; initramfs built with **musl rv64imac** (`riscv64-unknown-linux-musl-`), not FPU userspace from the gnu `lp64d` toolchain.
3. Re-sync **`fw_payload.elf`** after any OpenSBI/kernel rebuild: `./vdk-linux-build-keraunos-host.sh --sync`.

## Reference (Ascalon pattern)

`/localdev/pdroy/Ascalon_Workspace/linux_extensible/vsws_TT/Ascalon_Chiplet_System/software/mini-riscv64-linux/`

## Related

- `Documentation/keraunos-host-riscv-linux.md` (if present)
