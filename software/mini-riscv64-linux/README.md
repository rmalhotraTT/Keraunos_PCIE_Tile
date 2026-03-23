# mini-riscv64-linux (Keraunos PCIE Tile host)

Ascalon-style **minimal Linux + OpenSBI** artifacts for the **host** Rocket in `Keraunos_PCIE_Tile`.

## Prerequisites

- RISC-V cross compiler (e.g. `riscv64-unknown-linux-gnu-gcc`) on `PATH`
- `dtc` (device-tree-compiler)
- Linux kernel source tree and OpenSBI source (versions per your policy; Ascalon uses 6.12.x + OpenSBI 1.5.x)

See `Documentation/mini-riscv64-linux-keraunos.md` for the full workflow.

## Quick start

```bash
cd software/mini-riscv64-linux
# Edit vdk-linux-build-keraunos-host.sh: set LINUX_SRC, OPENSBI_SRC, CROSS_COMPILE
./vdk-linux-build-keraunos-host.sh
```

Artifacts should land in `output/`:

- `vmlinux` — kernel with symbols (for VP `symbols` load)
- `fw_payload.elf` — OpenSBI + payload (for VP `image+symbols` at `0x0` in mini vpcfg)

## DTS

- `DTS/keraunos_host_template.dts` — **template**; validate against **Keraunos** VP map before relying on it.

## VP

Use configuration `mini_riscv64_linux/mini_riscv64_linux` (see project `snps.vpproject` → active configuration).
