# mini-riscv64-linux (Keraunos PCIE Tile host)

**Host load:** `fw_payload.elf` with **empty** load address in VPC (ELF segments decide placement; OpenSBI entry **0x80000000**). Same idea as `vpconfigs/default` — **not** Ascalon’s forced `@0x0` load.

The sync script still copies **`vmlinux`** into `output/` if you want it for offline debug / gdb symbol files; VP uses **`fw_payload.elf`** only.

## Fast path: copy from existing `riscv-linux` workspace

On machines where `/localdev/rmalhotra/riscv-linux` exists:

```bash
cd software/mini-riscv64-linux
./vdk-linux-build-keraunos-host.sh
```

Or explicitly:

```bash
export RISCV_LINUX_ROOT=/localdev/rmalhotra/riscv-linux
./vdk-linux-build-keraunos-host.sh --sync
```

## Other machines

```bash
export RISCV_LINUX_ROOT=/path/to/your/riscv-linux
./vdk-linux-build-keraunos-host.sh --sync
```

## Full kernel + OpenSBI rebuild

Needs `dtc`, toolchain, kernel tree, OpenSBI tree, and `ROOTFS_CPIO`. See `Documentation/mini-riscv64-linux-keraunos.md`.

```bash
./vdk-linux-build-keraunos-host.sh --full
```

## DTS

- **`DTS/keraunos_host.dts`** — Keraunos host (UART `0xC000A000`, PLIC, PCIe RC). Sync with `/localdev/rmalhotra/riscv-linux/riscv-host-keraunos.dts` when the map changes.

## VP

Project **`snps.vpproject`** uses **`mini_riscv64_linux/mini_riscv64_linux`**. Switch to **`default/default`** to use the single `fw_payload` path under `riscv-linux/opensbi/...` again.
