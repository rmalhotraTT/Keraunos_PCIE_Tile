# Required Changes to Keraunos_PCIE_Tile.vdksys

## Problem

The CPU inside each SMC model cannot reach any external address (SMN registers,
DBI space, reset_unit). All accesses to addresses like `0x18104000` (CORE_CONTROL),
`0x08002040` (reset_unit), and `0x44000000` (DBI) silently hit the CPU-internal
undecoded stub and are lost. Writes are discarded; reads return zero.

This is because the CPU's DATA and INSTRUCTION bus ports are not connected to
the Chiplet-level SharedMemoryMap. Without this connection, VDK generates
undecoded stubs (`iStub|iBus_TT_Rocket_LT_PSP_1_DATA_Undecoded`, etc.) instead
of routing undecoded addresses through the SharedMemoryMap to external targets
like the PCIE_TILE.

## Evidence

The reference workspace (`SMC_PCIE_Tile.vdksys`) has these 4 connections.
Its generated `Properties.xml` has **zero** `iStub|iBus_TT_Rocket_LT_PSP_1_*_Undecoded`
entries. The Keraunos workspace is missing these connections and has **4** such stubs.

## Connections to Add

Add the following **4 connections** to `Keraunos_PCIE_Tile.vdksys`.

All 4 use identical decoded parameters:
- **start**: `0x0`
- **decoded**: `true`
- **end, size, offset**: empty (leave blank)

### Connection 1: Host_Chiplet SMC DATA bus

```
Endpoint A:
  Instance:  Host_Chiplet > SMC
  Interface: DATA
  Side:      external

Endpoint B:
  Instance:  Host_Chiplet > SharedMemoryMap
  Interface: intf
  Side:      left
```

### Connection 2: Host_Chiplet SMC INSTRUCTION bus

```
Endpoint A:
  Instance:  Host_Chiplet > SMC
  Interface: INSTRUCTION
  Side:      external

Endpoint B:
  Instance:  Host_Chiplet > SharedMemoryMap
  Interface: intf
  Side:      left
```

### Connection 3: Keraunos_PCIE_Chiplet SMC_Configure DATA bus

```
Endpoint A:
  Instance:  Keraunos_PCIE_Chiplet > SMC_Configure
  Interface: DATA
  Side:      external

Endpoint B:
  Instance:  Keraunos_PCIE_Chiplet > SharedMemoryMap
  Interface: intf
  Side:      left
```

### Connection 4: Keraunos_PCIE_Chiplet SMC_Configure INSTRUCTION bus

```
Endpoint A:
  Instance:  Keraunos_PCIE_Chiplet > SMC_Configure
  Interface: INSTRUCTION
  Side:      external

Endpoint B:
  Instance:  Keraunos_PCIE_Chiplet > SharedMemoryMap
  Interface: intf
  Side:      left
```

## Reference JSON (for manual editing)

If editing the `.vdksys` JSON directly, add these 4 objects to the `"connections"` array:

```json
{
  "endpoints": [
    {
      "instance": ["Host_Chiplet", "SMC"],
      "interface": "DATA",
      "side": "external"
    },
    {
      "instance": ["Host_Chiplet", "SharedMemoryMap"],
      "interface": "intf",
      "side": "left"
    }
  ],
  "decoded_parameters": {
    "start": "0x0",
    "end": "",
    "size": "",
    "offset": "",
    "decoded": true
  }
}
```

```json
{
  "endpoints": [
    {
      "instance": ["Host_Chiplet", "SMC"],
      "interface": "INSTRUCTION",
      "side": "external"
    },
    {
      "instance": ["Host_Chiplet", "SharedMemoryMap"],
      "interface": "intf",
      "side": "left"
    }
  ],
  "decoded_parameters": {
    "start": "0x0",
    "end": "",
    "size": "",
    "offset": "",
    "decoded": true
  }
}
```

```json
{
  "endpoints": [
    {
      "instance": ["Keraunos_PCIE_Chiplet", "SMC_Configure"],
      "interface": "DATA",
      "side": "external"
    },
    {
      "instance": ["Keraunos_PCIE_Chiplet", "SharedMemoryMap"],
      "interface": "intf",
      "side": "left"
    }
  ],
  "decoded_parameters": {
    "start": "0x0",
    "end": "",
    "size": "",
    "offset": "",
    "decoded": true
  }
}
```

```json
{
  "endpoints": [
    {
      "instance": ["Keraunos_PCIE_Chiplet", "SMC_Configure"],
      "interface": "INSTRUCTION",
      "side": "external"
    },
    {
      "instance": ["Keraunos_PCIE_Chiplet", "SharedMemoryMap"],
      "interface": "intf",
      "side": "left"
    }
  ],
  "decoded_parameters": {
    "start": "0x0",
    "end": "",
    "size": "",
    "offset": "",
    "decoded": true
  }
}
```

## How to Verify After Regeneration

After adding the connections and regenerating `Properties.xml`:

1. The 4 `iStub|iBus_TT_Rocket_LT_PSP_1_*_Undecoded` modules should disappear
   from `Properties.xml`. Verify with:

   ```bash
   grep -c "TT_Rocket_LT_PSP_1_DATA_Undecoded\|TT_Rocket_LT_PSP_1_INSTRUCTION_Undecoded" \
     generated/PCIEPlatformTest/Properties.xml
   ```

   Expected result: `0` (was `8` before the fix).

2. The Chiplet-level SharedMemoryMap bus should show increased `num_target_ports`:
   - `Keraunos_PCIE_Chiplet.iBus|SharedMemoryMap_intf`: `num_target_ports` should
     increase from `2` to `4`
   - `Host_Chiplet.iBus|SharedMemoryMap_intf`: `num_target_ports` should
     increase from its current value by `2`

3. New adapter modules should appear, e.g.:
   - `iAdapt|SMC_Configure_DATA|iBus_SharedMemoryMap_intf_BusTarget_*`
   - `iAdapt|SMC_DATA|iBus_SharedMemoryMap_intf_BusTarget_*`

## Notes

- The `.vpcfg` paramOverrides for the undecoded stubs (`TLM_OK_RESPONSE`) will
  become harmless no-ops after this fix since the stubs will no longer exist.
  You can remove them from `PCIEPlatformTest.vpcfg` once confirmed working.

- These connections are present in the reference workspace at
  `/localdev/pdroy/SMC_Keraunos_ws/linux_extensible/vsws_TT/SMC_PCIE_Tile/SMC_PCIE_Tile.vdksys`
  and can be used as a reference.

- If using the VDK GUI (Virtualizer Studio), these connections should be made by
  dragging from the SMC's DATA/INSTRUCTION ports to the SharedMemoryMap's intf
  port within each Chiplet subsystem.
