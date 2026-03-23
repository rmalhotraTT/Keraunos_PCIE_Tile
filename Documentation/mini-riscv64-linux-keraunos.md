# Mini RISC-V Linux for Keraunos PCIE Tile (host)

This document tracks bringing an **Ascalon-style mini Linux** flow into this workspace: small initramfs kernel + OpenSBI `fw_payload.elf`, loaded in the Virtualizer with a **two-image** `initial_image` entry (see Ascalon `mini_riscv64_linux.vpcfg`).

## Goals

- Faster **host** Linux bring-up in simulation vs. a full `riscv-linux/opensbi` tree.
- Keep **device** firmware (`pcie_bringup.elf` on `Keraunos_PCIE_Chiplet`) unchanged unless you explicitly want to change it.

## Layout in this repo

| Path | Purpose |
|------|---------|
| `software/mini-riscv64-linux/` | Build scripts, DTS templates, and (generated) `output/` |
| `software/mini-riscv64-linux/output/` | **`vmlinux`**, **`fw_payload.elf`**, `Image`, `rootfs.cpio`, `*.dtb` — **not committed** |
| `vpconfigs/mini_riscv64_linux/` | VP configuration that points the **host** Rocket at mini-linux artifacts |
| `software/mini-riscv64-linux/vdk-linux-build-keraunos-host.sh` | Bootstrap build script (extend as you wire kernel/opensbi) |

## VP configuration switch

1. Build artifacts into `software/mini-riscv64-linux/output/` (see script README).
2. In Virtualizer Studio, set the project **active configuration** to **`mini_riscv64_linux/mini_riscv64_linux`** (folder `vpconfigs/mini_riscv64_linux/`).
3. Or edit `snps.vpproject` attribute `activeConfig` from `default/default` to `mini_riscv64_linux/mini_riscv64_linux` once images exist.
4. Or duplicate the host `initial_image` override from `mini_riscv64_linux.vpcfg` into `default/default` if you prefer a single config.

The mini config is a copy of `default/default` with **only** the host CPU image line changed to the two-entry mini-linux pattern.

## Build approach (high level)

1. **Device tree** — `DTS/keraunos_host_template.dts` is a **starting point** copied from Ascalon’s PC RC DTS. You **must** align memory, UART, interrupt controllers, and PCIe nodes with the **Keraunos_PCIE_Tile** platform as modeled in VP (see also `vpconfigs/default/default.vpcfg` and platform docs).
2. **Cross toolchain** — same class as Ascalon: `riscv64-unknown-linux-gnu-` or `riscv64-linux-gnu-` in `PATH`.
3. **Linux** — small `defconfig` + `INITRAMFS_SOURCE` pointing at a minimal `rootfs.cpio` (BusyBox-style).
4. **OpenSBI** — generic platform build with `FW_PAYLOAD=y`, `FW_FDT_PATH` to your `.dtb`, `FW_PAYLOAD_PATH` to kernel `Image`, `FW_TEXT_START` matching your firmware link address (often `0x80000000`; confirm with your OpenSBI/platform docs).

Reference implementation (external path may vary on your machine):

`/localdev/pdroy/Ascalon_Workspace/linux_extensible/vsws_TT/Ascalon_Chiplet_System/software/mini-riscv64-linux/`

## Verification checklist

- [ ] `dtc` compiles `DTS/keraunos_host_template.dts` → `.dtb` without errors (after edits).
- [ ] OpenSBI build produces `build/platform/generic/firmware/fw_payload.elf`.
- [ ] `vmlinux` and `fw_payload.elf` copied or linked into `output/`.
- [ ] Simulation: OpenSBI banner → Linux boot → console on expected UART.

## Related

- `Documentation/keraunos-host-riscv-linux.md` (if present) — host Linux notes for this platform.
