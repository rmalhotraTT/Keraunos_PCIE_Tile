#!/bin/bash
# Launch Virtualizer Studio with the Keraunos PCIe Tile project

VIRTUALIZER=/tools_vendor/synopsys/virtualizer-tool-elite/V-2024.03/SLS/linux/virtualizerstudio/vs

# Required SMC IP path variables (ESA0147 fix)
export SYNOPSYS_EFUSE_EFUSE=/localdev/pdroy/PCIE_Tile_Keraunos/linux_extensible/Tenstorrent_IPDIR/linux/IP/Tenstorrent/eFUSE/1.0
export SYNOPSYS_MAILBOX_MAILBOX=/localdev/pdroy/PCIE_Tile_Keraunos/linux_extensible/Tenstorrent_IPDIR/linux/IP/Tenstorrent/MailBox/2.0
export SYNOPSYS_DWC_I2C_DWC_I2C=/tools_vendor/synopsys/virtualizer-tool-elite/V-2024.03/IP/DWC_i2c

echo "Environment set:"
echo "  SYNOPSYS_EFUSE_EFUSE=$SYNOPSYS_EFUSE_EFUSE"
echo "  SYNOPSYS_MAILBOX_MAILBOX=$SYNOPSYS_MAILBOX_MAILBOX"
echo "  SYNOPSYS_DWC_I2C_DWC_I2C=$SYNOPSYS_DWC_I2C_DWC_I2C"

if [ ! -f "$VIRTUALIZER" ]; then
    echo "ERROR: Virtualizer not found at $VIRTUALIZER"
    exit 1
fi

if [ -z "$DISPLAY" ]; then
    echo "ERROR: No DISPLAY set. Run this from a VNC or X11 session."
    exit 1
fi

echo "Launching Virtualizer Studio..."
exec "$VIRTUALIZER"
