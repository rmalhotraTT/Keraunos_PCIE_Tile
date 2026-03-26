#!/usr/bin/env bash
#
# Keraunos PCIE Tile — default (full) host Linux artifacts for VP.
#
# Usage:
#   ./vdk-linux-build-keraunos-host.sh           # sync from RISCV_LINUX_ROOT
#   ./vdk-linux-build-keraunos-host.sh --sync    # same as above (explicit)
#   ./vdk-linux-build-keraunos-host.sh --full    # rebuild kernel + OpenSBI from RISCV_LINUX_ROOT sources
#
# Sources live in the external RISCV_LINUX_ROOT tree (default: /localdev/rmalhotra/riscv-linux).
# This script builds or syncs into software/riscv64-linux/output/.
#
set -euo pipefail

SCRIPT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${SCRIPT_ROOT}/output"
DTS_SRC="${SCRIPT_ROOT}/DTS/keraunos_host.dts"
DTB_OUT="${OUT_DIR}/keraunos_host.dtb"

MUSL_TC="/localdev/rmalhotra/riscv-linux/rv64imac-toolchain/bin"
: "${CROSS_COMPILE:=riscv64-unknown-linux-musl-}"

if [[ -z "${RISCV_LINUX_ROOT:-}" && -d "/localdev/rmalhotra/riscv-linux" ]]; then
  RISCV_LINUX_ROOT="/localdev/rmalhotra/riscv-linux"
fi

export PATH="${MUSL_TC}:/opt/riscv/bin:${PATH}"
export CROSS_COMPILE

mkdir -p "${OUT_DIR}"

find_linux_dir() {
  local r="$1"
  for d in "${r}"/linux-*; do
    if [[ -d "$d" && -f "$d/Makefile" ]]; then
      echo "$d"
      return 0
    fi
  done
  return 1
}

compile_dtb() {
  local dtc_bin="dtc"
  if ! command -v dtc >/dev/null 2>&1; then
    local linux_dir
    linux_dir="$(find_linux_dir "${RISCV_LINUX_ROOT}")" || true
    local kern_dtc="${linux_dir}/scripts/dtc/dtc"
    if [[ -x "${kern_dtc}" ]]; then
      dtc_bin="${kern_dtc}"
    else
      echo "SKIP: dtc not in PATH and kernel dtc not found"
      return 0
    fi
  fi
  if [[ ! -f "${DTS_SRC}" ]]; then
    echo "WARN: Missing ${DTS_SRC} — skipping DTB compile"
    return 0
  fi
  echo "== Device tree (dtc) =="
  "${dtc_bin}" -I dts -O dtb -o "${DTB_OUT}" "${DTS_SRC}"
  echo "Wrote ${DTB_OUT}"
}

sync_from_riscv_linux() {
  local r="$1"
  if [[ ! -d "$r" ]]; then
    echo "ERROR: RISCV_LINUX_ROOT not a directory: $r"
    exit 1
  fi
  local linux_dir
  linux_dir="$(find_linux_dir "$r")" || true
  if [[ -z "$linux_dir" || ! -f "$linux_dir/vmlinux" ]]; then
    echo "ERROR: No linux-*/vmlinux under ${r}"
    exit 1
  fi
  local fw="${r}/opensbi/build/platform/generic/firmware/fw_payload.elf"
  if [[ ! -f "$fw" ]]; then
    echo "ERROR: Missing ${fw}"
    exit 1
  fi
  local dtb="${r}/riscv-host-keraunos.dtb"
  if [[ -f "$dtb" ]]; then
    cp -f "${dtb}" "${DTB_OUT}"
    echo "Copied $(basename "$dtb") -> ${DTB_OUT}"
  fi
  cp -f "${linux_dir}/vmlinux" "${OUT_DIR}/vmlinux"
  cp -f "${fw}" "${OUT_DIR}/fw_payload.elf"
  echo "Synced vmlinux from ${linux_dir}"
  echo "Synced fw_payload.elf from opensbi build"
  ls -lh "${OUT_DIR}/fw_payload.elf" "${OUT_DIR}/vmlinux" "${DTB_OUT}" 2>/dev/null
}

full_build() {
  if [[ -z "${RISCV_LINUX_ROOT:-}" ]]; then
    echo "ERROR: Set RISCV_LINUX_ROOT (contains linux-6.6.30/ and opensbi/)"
    exit 1
  fi
  if ! command -v "${CROSS_COMPILE}gcc" >/dev/null 2>&1; then
    echo "ERROR: Cross compiler not found: ${CROSS_COMPILE}gcc"
    exit 1
  fi

  local r="${RISCV_LINUX_ROOT}"
  local linux_dir
  linux_dir="$(find_linux_dir "$r")" || { echo "ERROR: No linux-* under ${r}"; exit 1; }
  local opensbi_dir="${r}/opensbi"
  local rootfs_gz="${r}/initramfs.cpio.gz"

  if [[ ! -d "$opensbi_dir" ]]; then
    echo "ERROR: Missing ${opensbi_dir}"
    exit 1
  fi
  if [[ ! -f "$rootfs_gz" ]]; then
    echo "ERROR: Missing ${rootfs_gz} — rebuild initramfs first"
    exit 1
  fi

  echo "== Linux kernel (default — rv64imac, no FPU) =="
  echo "Using: ${linux_dir}"
  pushd "${linux_dir}" >/dev/null
  ###### 3 lines addedded to enable earlycon for UART console ######
  ./scripts/config --file .config --enable CONFIG_RISCV_SBI_V01
  ./scripts/config --file .config --enable CONFIG_SERIAL_EARLYCON_RISCV_SBI
  make ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE}" olddefconfig
  
  make ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE}" -j"$(nproc)" Image
  cp -f arch/riscv/boot/Image "${OUT_DIR}/Image"
  cp -f vmlinux "${OUT_DIR}/vmlinux"
  popd >/dev/null

  compile_dtb

  echo "== OpenSBI (rv64imac, lp64) =="
  pushd "${opensbi_dir}" >/dev/null
  make clean
  make -j"$(nproc)" \
    CROSS_COMPILE="${CROSS_COMPILE}" \
    PLATFORM=generic \
    PLATFORM_RISCV_XLEN=64 \
    PLATFORM_RISCV_ISA=rv64imac_zicsr_zifencei \
    PLATFORM_RISCV_ABI=lp64 \
    FW_PAYLOAD=y \
    FW_TEXT_START=0x80000000 \
    FW_FDT_PATH="${DTB_OUT}" \
    FW_PAYLOAD_PATH="${OUT_DIR}/Image"
  cp -f build/platform/generic/firmware/fw_payload.elf "${OUT_DIR}/fw_payload.elf"
  popd >/dev/null

  echo "Done."
  ls -lh "${OUT_DIR}/fw_payload.elf" "${OUT_DIR}/vmlinux" "${OUT_DIR}/Image" "${DTB_OUT}"
}

echo "== riscv64-linux default (Keraunos) =="
echo "SCRIPT_ROOT=${SCRIPT_ROOT}  OUT_DIR=${OUT_DIR}"

case "${1:-}" in
  --sync)
    if [[ -z "${RISCV_LINUX_ROOT:-}" ]]; then
      echo "ERROR: Set RISCV_LINUX_ROOT to your riscv-linux tree."
      exit 1
    fi
    sync_from_riscv_linux "${RISCV_LINUX_ROOT}"
    ;;
  --full)
    full_build
    ;;
  *)
    if [[ -n "${RISCV_LINUX_ROOT:-}" ]]; then
      sync_from_riscv_linux "${RISCV_LINUX_ROOT}"
    else
      echo "RISCV_LINUX_ROOT not set."
      echo "  export RISCV_LINUX_ROOT=/localdev/rmalhotra/riscv-linux"
      echo "  $0 --sync   # copy pre-built artifacts"
      echo "  $0 --full   # rebuild kernel + OpenSBI"
      exit 1
    fi
    ;;
esac
