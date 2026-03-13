# Proposed Changes Report: DBI Access Fault Fix

## Problem Statement

The `host_pcie_test` firmware running on the Host_Chiplet CPU faults when reading the
PCIE_RC DBI register space at `0x44000000`:

```
[TRAP] DBI read 0x44000000: mcause=0x5  mepc=0x800005a2  mtval=0x44000000
[TRAP] cause: load access fault
```

## Investigation Summary

Reviewed against:
- **DWC_PCIe TLM Model Documentation** (`IP_DWC_PCIE_LCA.pdf`, V-2024.03-1)
  - Section 2.2: External Signals and Pins (Reset, Clock, Bus, PCIe, Core Control)
  - Section 3.1.1: RC Integration - Key interfaces table (Table 3-1)
  - Section 3.1.2: RC Configuration - Key parameters (Table 3-2)
  - Section 3.1.3: EP Integration - Key interfaces table (Table 3-3)
- **Generated Properties.xml** for PCIEPlatformTest
- **Current Keraunos_PCIE_Tile.vdksys** connections section

---

## Issue 1 (ROOT CAUSE - CRITICAL): AXI_DBI Missing Address Offset

### What the PDF says

Per Section 3.1.1.1: "The AXI_Slave offset has to be set to the base address."
The AXI_Slave connection correctly uses `"offset": "@start"`, which causes the bus
router to subtract the base address before forwarding to the PCIE_RC port. This
generates the `:s` suffix in range_mappings.

### The problem

The AXI_DBI connection does NOT have `"offset": "@start"`. Current decoded_parameters:

```json
{
  "start": "0x44000000",
  "end": "",
  "size": "0x00400000",
  "offset": "",
  "decoded": true
}
```

This generates range_mappings: `0x44000000:0x00400000` (no `:s` suffix).

Without address subtraction, when the CPU reads `0x44000000`, the bus router forwards
the FULL address `0x44000000` to the PCIE_RC's AXI_DBI port. The DBI register space
starts at offset `0x0` within that port, so `0x44000000` is far outside the valid
register range. The RC model returns TLM_GENERIC_ERROR_RESPONSE, which triggers the
RISC-V load access fault (mcause=0x5).

### Proposed fix

Change the AXI_DBI decoded_parameters to include `"offset": "@start"`:

**File:** `Keraunos_PCIE_Tile.vdksys`, connections section

**Host_Chiplet PCIE_RC AXI_DBI (line ~12215):**

Change `"offset" : ""` to `"offset" : "@start"`

**Keraunos_PCIE_Chiplet PCIe_EP AXI_DBI (line ~12430):**

Change `"offset" : ""` to `"offset" : "@start"`

This will generate range_mappings with `:s` suffix (`0x44000000:0x00400000:s`),
ensuring the bus subtracts `0x44000000` before forwarding to the DBI port (so the RC
sees address `0x0` for register offset 0, `0x4` for register offset 4, etc.).

### Verification

After regeneration, check Properties.xml for:
```
0x44000000:0x00400000:s
```
The `:s` suffix must be present on the DBI entry.

---

## Issue 2 (SIGNIFICANT): Missing PCIE_RC.PCIMem_Slave Connection

### What the PDF says

Per Table 3-1 (RC Key Interfaces):
- PCIMem: "Master of PCIe link. PCIMem interface of RC is connected to
  PCIMem_Slave interface of PCIe endpoint/switch."
- PCIMem_Slave: "Slave of PCIe link. PCIMem_Slave interface of RC is connected
  to PCIMem interface of PCIe endpoint/switch."

Both directions of the PCIe link must be connected.

### The problem

The Keraunos workspace connects:
- `PCIE_RC.PCIMem -> PCIeSwitch.USP_PCIMem_Slave` (RC-to-Switch) - PRESENT
- `PCIE_RC.PCIMem_Slave -> PCIeSwitch.USP_PCIMem` (Switch-to-RC) - MISSING

Without the reverse direction, the RC cannot receive incoming TLPs from the
switch/EP (configuration completions, DMA completions, MSI interrupts, etc.).

### Proposed fix

Add a new connection to `Keraunos_PCIE_Tile.vdksys`:

```json
{
  "endpoints" : [ {
    "instance" : [ "Host_Chiplet", "Misc", "PCIE_RC" ],
    "interface" : "PCIMem_Slave",
    "side" : "external"
  }, {
    "instance" : [ "Misc", "PCIeSwitch" ],
    "interface" : "USP_PCIMem",
    "side" : "external"
  } ],
  "decoded_parameters" : {
    "start" : "0x0",
    "end" : "",
    "size" : "",
    "offset" : "",
    "decoded" : true
  }
}
```

### Impact

Required for PCIe endpoint enumeration, config space reads, DMA, and MSI/MSI-X
from EP to RC. Not directly related to the DBI fault, but will cause failures in
later test steps.

---

## Issue 3 (MODERATE): Missing PCIe_EP Clock Connections

### What the PDF says

Per Table 3-3 (EP Key Interfaces) and Section 3.1.4 (EP Configuration):
- cc_pipe_clk and cc_aclkSlv are listed as key EP clock interfaces
- Clock overrides ensure "model functionality continues even though the clock may
  not be driven"

### The problem

The PCIe_EP instance (Keraunos_PCIE_Chiplet.Misc.PCIe_EP) has NO clock connections.
All clock ports are auto-stubbed (undriven). The corresponding override parameters
(cc_dbi_aclk_Override, etc.) are all 0 in Properties.xml.

### Proposed fix

At minimum, connect cc_dbi_aclk and cc_core_clk to the Keraunos_PCIE_Chiplet CLK_GEN:

```json
{
  "endpoints" : [ {
    "instance" : [ "Keraunos_PCIE_Chiplet", "Misc", "PCIe_EP" ],
    "interface" : "cc_dbi_aclk",
    "side" : "external"
  }, {
    "instance" : [ "Keraunos_PCIE_Chiplet", "Infra", "CLK_GEN" ],
    "interface" : "CLK",
    "side" : "external"
  } ]
}
```

```json
{
  "endpoints" : [ {
    "instance" : [ "Keraunos_PCIE_Chiplet", "Misc", "PCIe_EP" ],
    "interface" : "cc_core_clk",
    "side" : "external"
  }, {
    "instance" : [ "Keraunos_PCIE_Chiplet", "Infra", "CLK_GEN" ],
    "interface" : "CLK",
    "side" : "external"
  } ]
}
```

### Risk assessment

Low risk. Link training already succeeds without these clocks, suggesting the EP
model tolerates missing clocks for basic operation. However, they may be needed for
correct DBI register access and internal state machine timing on the EP side.

---

## Issue 4 (INFORMATIONAL): Redundant .vpcfg Overrides

The following Properties.xml values are already correct (baked in during generation):

| Parameter | Current | Expected | Status |
|-----------|---------|----------|--------|
| PCIE_RC.cc_dbi_aclk_Override | 100 | 100 | Already correct |
| PCIE_RC.AXIBaseAddr | 0x20000000 | 0x20000000 | Already correct |
| CX_ATU_NUM_OUTBOUND_REGIONS | 8 | 8 | Already correct |
| CX_ATU_NUM_INBOUND_REGIONS | 16 | 16 | Already correct |

The current .vpcfg overrides for range_mappings (added to fix the `:s` issue) did
not work via paramOverrides. These should be removed after the .vdksys fix is applied.

---

## Existing Connections Audit: Host_Chiplet PCIE_RC

### Bus Interfaces

| Interface | Connected To | Status |
|-----------|-------------|--------|
| BusMaster | SharedMemoryMap.intf (left) @ 0x0 | OK |
| AXI_Slave | SharedMemoryMap.intf (right) @ 0x70000000, offset=@start | OK |
| AXI_DBI | SharedMemoryMap.intf (right) @ 0x44000000, offset="" | NEEDS FIX (Issue 1) |
| PCIMem | PCIeSwitch.USP_PCIMem_Slave @ 0x0 | OK |
| PCIMem_Slave | NOT CONNECTED | NEEDS FIX (Issue 2) |
| ELBIMaster | NOT CONNECTED (auto-stubbed) | Optional per PDF |

### Reset Interfaces (all connected to RST_GEN.RST)

| Interface | Status |
|-----------|--------|
| pcie_axi_ares | OK |
| cc_dbi_ares | OK |
| cc_core_ares | OK |
| cc_pwr_ares | OK |
| cc_phy_ares | OK |
| perst_n | OK |

### Clock Interfaces (all connected to CLK_GEN.CLK)

| Interface | Status |
|-----------|--------|
| cc_aclkSlv | OK |
| cc_aclkMstr | OK |
| cc_dbi_aclk | OK |
| cc_pipe_clk | OK |
| cc_aux_clk | OK |
| refclk | OK |
| cc_core_clk | OK |

### Control Interfaces

| Interface | Status | Notes |
|-----------|--------|-------|
| device_type | Auto-stubbed | CC_DEVICE_TYPE=1 (RC) set via parameter |
| app_ltssm_en | Auto-stubbed | Per PDF: can be left unconnected |

---

## Existing Connections Audit: PCIeSwitch

| Interface | Connected To | Status |
|-----------|-------------|--------|
| USP_PCIMem_Slave | PCIE_RC.PCIMem | OK |
| USP_PCIMem | NOT CONNECTED | NEEDS FIX (Issue 2) |
| DSP_PCIMem_Slave[0] | PCIe_EP.PCIMem | OK |
| DSP_PCIMem[0] | PCIe_EP.PCIMem_Slave | OK |
| reset_n | Host_Chiplet.RST_GEN.RST | OK |

---

## Summary of All Proposed Changes

| # | File | Change | Priority | Risk |
|---|------|--------|----------|------|
| 1 | vdksys | AXI_DBI offset: "" to "@start" (2 places) | CRITICAL | Low |
| 2 | vdksys | Add PCIE_RC.PCIMem_Slave to PCIeSwitch.USP_PCIMem | HIGH | Low |
| 3 | vdksys | Add PCIe_EP cc_dbi_aclk + cc_core_clk to CLK_GEN | MEDIUM | Low |
| 4 | vpcfg | Remove redundant/non-working overrides | LOW | None |

### Recommended approach

1. Apply Issue 1 first (AXI_DBI offset fix) - this is the root cause of the DBI fault
2. Apply Issue 2 (PCIMem_Slave) - required for later test steps per PDF
3. Optionally apply Issue 3 (EP clocks) - may not be immediately needed
4. Clean up Issue 4 (.vpcfg) after verifying the fixes work
