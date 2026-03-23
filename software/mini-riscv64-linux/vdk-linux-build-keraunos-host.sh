#!/usr/bin/env bash
#
# Keraunos PCIE Tile — host mini-linux artifacts for VP (vmlinux + fw_payload.elf).
#
# Usage:
#   ./vdk-linux-build-keraunos-host.sh           # sync from RISCV_LINUX_ROOT if present; else compile DTB only
#   ./vdk-linux-build-keraunos-host.sh --sync    # only copy vmlinux/fw_payload/dtb from RISCV_LINUX_ROOT
#   ./vdk-linux-build-keraunos-host.sh --full    # rebuild kernel + OpenSBI (needs toolchain, rootfs cpio)
#
set -euo pipefail

MINI_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${MINI_ROOT}/output"
DTS_SRC="${MINI_ROOT}/DTS/keraunos_host.dts"
DTB_OUT="${OUT_DIR}/keraunos_host.dtb"

: "${CROSS_COMPILE:=riscv64-unknown-linux-musl-}"
: "${ROOTFS_CPIO:=}"

# Central workspace (adjust on other machines)
if [[ -z "${RISCV_LINUX_ROOT:-}" && -d "/localdev/rmalhotra/riscv-linux" ]]; then
  RISCV_LINUX_ROOT="/localdev/rmalhotra/riscv-linux"
fi

LINUX_SRC="${LINUX_SRC:-}"
OPENSBI_SRC="${OPENSBI_SRC:-}"

export PATH="/localdev/rmalhotra/riscv-linux/rv64imac-toolchain/bin:/opt/riscv/bin:${PATH}"
export CROSS_COMPILE

mkdir -p "${OUT_DIR}"

sync_from_riscv_linux() {
  local r="$1"
  if [[ ! -d "$r" ]]; then
    echo "ERROR: RISCV_LINUX_ROOT not a directory: $r"
    exit 1
  fi
  local linux_dir=""
  local d
  for d in "${r}"/linux-*; do
    if [[ -d "$d" && -f "$d/vmlinux" ]]; then
      linux_dir="$d"
      break
    fi
  done
  if [[ -z "$linux_dir" ]]; then
    echo "ERROR: No linux-*/vmlinux under ${r}"
    exit 1
  fi
  local fw="${r}/opensbi/build/platform/generic/firmware/fw_payload.elf"
  if [[ ! -f "$fw" ]]; then
    echo "ERROR: Missing ${fw}"
    exit 1
  fi
  local dtb="${r}/riscv-host-keraunos.dtb"
  if [[ ! -f "$dtb" ]]; then
    echo "WARN: Missing ${dtb} — skipping dtb copy"
  else
    cp -f "${dtb}" "${DTB_OUT}"
    echo "Copied $(basename "$dtb") -> ${DTB_OUT}"
  fi
  cp -f "${linux_dir}/vmlinux" "${OUT_DIR}/vmlinux"
  cp -f "${fw}" "${OUT_DIR}/fw_payload.elf"
  echo "Synced vmlinux from ${linux_dir}"
  echo "Synced fw_payload.elf from opensbi build"
  ls -lh "${OUT_DIR}/vmlinux" "${OUT_DIR}/fw_payload.elf" "${DTB_OUT}" 2>/dev/null || ls -lh "${OUT_DIR}/vmlinux" "${OUT_DIR}/fw_payload.elf"
}

compile_dtb() {
  if ! command -v dtc >/dev/null 2>&1; then
    echo "SKIP: dtc not in PATH (install device-tree-compiler to refresh ${DTB_OUT} from DTS)"
    return 0
  fi
  if [[ ! -f "${DTS_SRC}" ]]; then
    echo "ERROR: Missing ${DTS_SRC}"
    exit 1
  fi
  echo "== Device tree (dtc) =="
  dtc -I dts -O dtb -o "${DTB_OUT}" "${DTS_SRC}"
  echo "Wrote ${DTB_OUT}"
}

full_build() {
  if ! command -v "${CROSS_COMPILE}gcc" >/dev/null 2>&1; then
    echo "ERROR: Cross compiler not found: ${CROSS_COMPILE}gcc"
    exit 1
  fi
  LINUX_SRC="${LINUX_SRC:-${MINI_ROOT}/linux-6.12.1}"
  OPENSBI_SRC="${OPENSBI_SRC:-${MINI_ROOT}/opensbi-1.5.1}"
  ROOTFS_CPIO="${ROOTFS_CPIO:-${OUT_DIR}/rootfs.cpio}"

  compile_dtb

  if [[ ! -d "${LINUX_SRC}" ]]; then
    echo "ERROR: LINUX_SRC not found (${LINUX_SRC}). Use --sync or set LINUX_SRC."
    exit 1
  fi
  if [[ ! -f "${ROOTFS_CPIO}" ]]; then
    echo "ERROR: rootfs cpio not found (${ROOTFS_CPIO}). Set ROOTFS_CPIO or use --sync."
    exit 1
  fi
  if [[ ! -d "${OPENSBI_SRC}" ]]; then
    echo "ERROR: OPENSBI_SRC not found (${OPENSBI_SRC})"
    exit 1
  fi

  echo "== Linux kernel =="
  pushd "${LINUX_SRC}" >/dev/null
  make ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE}" defconfig
  ./scripts/config --file .config --set-str INITRAMFS_SOURCE "${ROOTFS_CPIO}"
  ./scripts/config --file .config --enable INITRAMFS_COMPRESSION_NONE
  yes "" | make ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE}" oldconfig
  make ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE}" -j"$(nproc)" -s Image
  cp -f arch/riscv/boot/Image "${OUT_DIR}/Image"
  cp -f vmlinux "${OUT_DIR}/vmlinux"
  popd >/dev/null

  echo "== OpenSBI =="
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

  echo "Done."
  ls -la "${OUT_DIR}/vmlinux" "${OUT_DIR}/fw_payload.elf" "${OUT_DIR}/Image" "${DTB_OUT}"
}

echo "== mini-riscv64-linux (Keraunos) =="
echo "MINI_ROOT=${MINI_ROOT}  OUT_DIR=${OUT_DIR}"

case "${1:-}" in
  --sync)
    if [[ -z "${RISCV_LINUX_ROOT:-}" ]]; then
      echo "ERROR: Set RISCV_LINUX_ROOT to your riscv-linux tree (contains linux-*/vmlinux and opensbi/)."
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
      echo "RISCV_LINUX_ROOT not set; skipping binary sync."
    fi
    compile_dtb
    if [[ -z "${RISCV_LINUX_ROOT:-}" ]]; then
      echo
      echo "Tip: export RISCV_LINUX_ROOT=/path/to/riscv-linux then re-run, or use:"
      echo "  $0 --sync"
    fi
    ;;
esac
