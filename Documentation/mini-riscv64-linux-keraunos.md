# Mini RISC-V Linux for Keraunos PCIE Tile (host)

This document tracks bringing an **Ascalon-style mini Linux** flow into this workspace: Linux + OpenSBI `fw_payload.elf`, loaded in the Virtualizer with a **two-image** `initial_image` (`vmlinux` for symbols + `fw_payload.elf` for execution).

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

- [ ] `output/vmlinux` and `output/fw_payload.elf` exist (after `--sync` or `--full`).
- [ ] `DTS/keraunos_host.dts` matches `riscv-host-keraunos.dts` when the host map changes.
- [ ] Simulation: OpenSBI → Linux → console on **UART @ 0xC000A000** (per DTS `bootargs`).

## Reference (Ascalon pattern)

`/localdev/pdroy/Ascalon_Workspace/linux_extensible/vsws_TT/Ascalon_Chiplet_System/software/mini-riscv64-linux/`

## Related

- `Documentation/keraunos-host-riscv-linux.md` (if present)
