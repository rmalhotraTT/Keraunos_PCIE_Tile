#!/usr/bin/env bash
#
# Bootstrap: build mini Linux + OpenSBI for Keraunos PCIE Tile (host Rocket).
# Extend this script as you vendor kernel/opensbi trees (see Ascalon reference).
#
set -euo pipefail

MINI_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${MINI_ROOT}/output"
DTS_SRC="${MINI_ROOT}/DTS/keraunos_host_template.dts"
DTB_OUT="${OUT_DIR}/keraunos_host.dtb"

# --- User / CI: point these at your trees (or place symlinks next to this script) ---
: "${CROSS_COMPILE:=riscv64-unknown-linux-gnu-}"
: "${LINUX_SRC:=${MINI_ROOT}/linux-6.12.1}"
: "${OPENSBI_SRC:=${MINI_ROOT}/opensbi-1.5.1}"
: "${ROOTFS_CPIO:=${OUT_DIR}/rootfs.cpio}"

export PATH="/opt/riscv/bin:${PATH}"
export CROSS_COMPILE

echo "== mini-riscv64-linux (Keraunos) =="
echo "MINI_ROOT=${MINI_ROOT}"
echo "OUT_DIR=${OUT_DIR}"
echo "LINUX_SRC=${LINUX_SRC}"
echo "OPENSBI_SRC=${OPENSBI_SRC}"
echo

mkdir -p "${OUT_DIR}"

if ! command -v "${CROSS_COMPILE}gcc" >/dev/null 2>&1; then
  echo "ERROR: Cross compiler not found: ${CROSS_COMPILE}gcc"
  echo "Set CROSS_COMPILE or add toolchain to PATH (e.g. /opt/riscv/bin)."
  exit 1
fi

if ! command -v dtc >/dev/null 2>&1; then
  echo "ERROR: dtc (device-tree-compiler) not found. Install device-tree-compiler."
  exit 1
fi

echo "== Device tree =="
dtc -I dts -O dtb -o "${DTB_OUT}" "${DTS_SRC}"
echo "Wrote ${DTB_OUT}"

if [[ ! -d "${LINUX_SRC}" ]]; then
  echo
  echo "SKIP kernel build: LINUX_SRC not found (${LINUX_SRC})."
  echo "  Fetch and extract Linux (e.g. 6.12.x), or set LINUX_SRC."
  echo "  Follow: Documentation/mini-riscv64-linux-keraunos.md"
  echo "  Ascalon reference: .../Ascalon_Chiplet_System/software/mini-riscv64-linux/vdk-linux-build-tt-pcirc.sh"
  exit 0
fi

if [[ ! -f "${ROOTFS_CPIO}" ]]; then
  echo
  echo "SKIP kernel build: rootfs cpio not found (${ROOTFS_CPIO})."
  echo "  Create a minimal initramfs and save as output/rootfs.cpio (BusyBox, etc.)."
  exit 0
fi

echo
echo "== Linux kernel (minimal) =="
pushd "${LINUX_SRC}" >/dev/null
make ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE}" defconfig
./scripts/config --file .config --set-str INITRAMFS_SOURCE "${ROOTFS_CPIO}"
./scripts/config --file .config --enable INITRAMFS_COMPRESSION_NONE
yes "" | make ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE}" oldconfig
make ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE}" -j"$(nproc)" -s Image
cp -f arch/riscv/boot/Image "${OUT_DIR}/Image"
cp -f vmlinux "${OUT_DIR}/vmlinux"
popd >/dev/null

if [[ ! -d "${OPENSBI_SRC}" ]]; then
  echo "ERROR: OPENSBI_SRC not found (${OPENSBI_SRC})."
  exit 1
fi

echo
echo "== OpenSBI (fw_payload) =="
pushd "${OPENSBI_SRC}" >/dev/null
export PLATFORM_RISCV_XLEN=64
export PLATFORM=generic
export PLATFORM_RISCV_ISA=rv64imafdc_zifencei
export PLATFORM_RISCV_ABI=lp64d
export FW_PAYLOAD=y
export FW_TEXT_START=0x80000000
export FW_FDT_PATH="${DTB_OUT}"
export FW_PAYLOAD_PATH="${OUT_DIR}/Image"
make clean
make -s -j"$(nproc)"
cp -f build/platform/generic/firmware/fw_payload.elf "${OUT_DIR}/fw_payload.elf"
popd >/dev/null

echo
echo "Done. Artifacts:"
ls -la "${OUT_DIR}/vmlinux" "${OUT_DIR}/fw_payload.elf" "${OUT_DIR}/Image" "${DTB_OUT}"
