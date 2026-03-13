#!/usr/bin/env python3
"""
patch_pcie_switch_params.py
----------------------------
After every VDK rebuild (vpx generate / Build in Virtualizer Studio), run this
script to restore the PCIeSwitch USP/DSP0 sub-module scml_property_registry entries.

Problem
-------
The Synopsys Virtualizer V-2024.03 scml_property_registry is populated exclusively
from the --cwr_properties_xml file (generated/PCIEPlatformTest/Properties.xml).
The .vpcfg paramOverrides can only OVERRIDE existing registered properties, not
inject new ones.  The .vdksys sub-module path overrides (/USP/..., /DSP[0]/...)
are NOT propagated to Properties.xml by the VDK build generator.

This means the 59 parameters that PCIeSwitch.USP and PCIeSwitch.DSP0 look up in the
scml_property_registry (PL128G_CAP_OFFSET, CX_IS_SW, etc.) are never found, causing:
  - DSP0.DWC_PCIe_DM_impl: device_type input pin = 0 (wrong, fallback = 255)
  - DSP0 link does not train as a switch downstream port
  - CfgRd1 to Bus 2 (EP) fails / returns 0xffffffff

Solution
--------
This script does TWO things after each VDK rebuild:

  1. Patches generated/PCIEPlatformTest/launch.conf to add a second
     --cwr_properties_xml argument pointing to a persistent supplemental file:
       vpconfigs/PCIEPlatformTest/PCIeSwitch_ext_properties.xml
     This supplemental file lives outside generated/ and survives VDK rebuilds.
     The launch.conf is regenerated on each VDK build, so this patch is needed
     once per build.

  2. (Fallback) If the VPX binary does not accept multiple --cwr_properties_xml,
     also patches Properties.xml directly to add the USP/DSP0 module entries.

Usage
-----
    python3 scripts/patch_pcie_switch_params.py [--dry-run] [--fallback-props-only]

Options
    --dry-run             Show what would change without writing any files.
    --fallback-props-only Skip the launch.conf patch; only patch Properties.xml.

The script is idempotent: safe to run multiple times.
"""

import json
import os
import re
import sys
import shutil
import xml.etree.ElementTree as ET
from datetime import datetime

# ── Paths ──────────────────────────────────────────────────────────────────────
SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
LAUNCH_CONF  = os.path.join(PROJECT_ROOT, 'generated', 'PCIEPlatformTest', 'launch.conf')
PROPS_XML    = os.path.join(PROJECT_ROOT, 'generated', 'PCIEPlatformTest', 'Properties.xml')
EXT_PROPS    = os.path.join(PROJECT_ROOT, 'vpconfigs', 'PCIEPlatformTest',
                            'PCIeSwitch_ext_properties.xml')

# ── Complete parameter list for PCIeSwitch.USP and PCIeSwitch.DSP0 ─────────────
# Values taken from the PCIE_RC module in Properties.xml (same DW controller core).
# CX_IS_SW = 1 for switch ports (RC has 0).
USP_DSP_PARAMS = {
    'ARC_512': 0,
    'CD_CAP_NEXT_PTR': 0,
    'CD_CAP_OFFSET': 0,
    'CLIENT2_POPULATED': 1,
    'CX_CCIX_VDM_VID': 0,
    'CX_CXL_CACHE_EN': 0,
    'CX_CXL_CHI_DATACHK_EN': 0,
    'CX_CXL_MEM_EN': 0,
    'CX_DEFAULT_LANE_UNDER_TEST': 0,
    'CX_DSP_PCIPM_L1_ENTER_DELAY': 0,
    'CX_FLT_Q_ADDR_GT_32': 0,
    'CX_INTERFACE_TIMER_EN': 0,
    'CX_IS_SW': 1,                          # Switch port — must be 1
    'CX_MASK_UR_CA_4_TRGT1': 0,
    'CX_MODIFIED_TS_FORMAT_SUPPORT': 0,
    'CX_MOD_TS_FMT_SUPPORT_VALUE': 0,
    'CX_MSI_CTRL_EN': 0,
    'CX_PMA_PIPE_RST_DELAY_TIMER': 0,
    'CX_RADMQ_MODE': 0,
    'CX_RAS_EN': 0,
    'DEFAULT_GEN5_RXMARGIN_IND_ERROR_SAMPLER': 1,
    'DEFAULT_GEN5_RXMARGIN_IND_LEFT_RIGHT_TIMING': 1,
    'DEFAULT_GEN5_RXMARGIN_IND_UP_DOWN_VOLTAGE': 1,
    'DEFAULT_GEN5_RXMARGIN_MAXLANES': 15,
    'DEFAULT_GEN5_RXMARGIN_MAX_TIMING_OFFSET': 50,
    'DEFAULT_GEN5_RXMARGIN_MAX_VOLTAGE_OFFSET': 19,
    'DEFAULT_GEN5_RXMARGIN_NUM_TIMING_STEPS': 25,
    'DEFAULT_GEN5_RXMARGIN_NUM_VOLTAGE_STEPS': 127,
    'DEFAULT_GEN5_RXMARGIN_SAMPLE_RATE_TIMING': 0,
    'DEFAULT_GEN5_RXMARGIN_SAMPLE_RATE_VOLTAGE': 0,
    'DEFAULT_GEN5_RXMARGIN_SAMPLE_REPORTING_METHOD': 1,
    'DEFAULT_GEN5_RXMARGIN_VOLTAGE_SUPPORTED': 1,
    'DEFAULT_GEN6_RXMARGIN_MAXLANES': 15,
    'DEFAULT_GEN6_RXMARGIN_SAMPLE_RATE_TIMING': 0,
    'DEFAULT_GEN6_RXMARGIN_SAMPLE_RATE_VOLTAGE': 0,
    'DEFAULT_GEN6_RXMARGIN_SAMPLE_REPORTING_METHOD': 1,
    'DEFAULT_LANE_SKEW_OFF_26': 0,
    'DEFAULT_MAX_COEF_FOM_VECTOR_DEPTH': 0,
    'DEFAULT_PHY_CONTROL': 0,
    'DEFAULT_PIPE_GARBAGE_DATA_MODE': 0,
    'DEFAULT_RX_SERIALIZATION_Q_ALMOST_FULL_THRESHOLD': 0,
    'DEFAULT_TARGET': 0,
    'DEFAULT_TX_MESSAGE_BUS_MIN_WRITE_BUFFER_DEPTH': 0,
    'DEFAULT_UPCONFIGURE_SUPPORT': 1,
    'MEM_FUNC0_BAR0_TARGET_MAP': 1,
    'MEM_FUNC0_BAR1_TARGET_MAP': 1,
    'MEM_FUNC0_BAR2_TARGET_MAP': 1,
    'MEM_FUNC0_BAR3_TARGET_MAP': 1,
    'MEM_FUNC0_BAR4_TARGET_MAP': 1,
    'MEM_FUNC0_BAR5_TARGET_MAP': 1,
    'PL128G_CAP_NEXT_PTR': 0,
    'PL128G_CAP_OFFSET': 0,
    'ROM_FUNC0_TARGET_MAP': 1,
    'VF_MEM_FUNC0_BAR0_TARGET_MAP': 1,
    'VF_MEM_FUNC0_BAR1_TARGET_MAP': 1,
    'VF_MEM_FUNC0_BAR2_TARGET_MAP': 1,
    'VF_MEM_FUNC0_BAR3_TARGET_MAP': 1,
    'VF_MEM_FUNC0_BAR4_TARGET_MAP': 1,
    'VF_MEM_FUNC0_BAR5_TARGET_MAP': 1,
}

# CC_DEVICE_TYPE drives the device_type input pin in DWC_PCIe_DM_impl.
# DW encoding: 0=EP, 1=RC/Root Port, 2=USP, 3=DSP.
# This MUST differ between USP and DSP — stored per-module.
MODULE_PARAMS = {
    'Keraunos_PCIE_Tile.Misc.PCIeSwitch.USP':  dict(USP_DSP_PARAMS, CC_DEVICE_TYPE=2),
    'Keraunos_PCIE_Tile.Misc.PCIeSwitch.DSP0': dict(USP_DSP_PARAMS, CC_DEVICE_TYPE=3),
}
MODULE_NAMES = list(MODULE_PARAMS.keys())

EXT_PROPS_ARG = '--cwr_properties_xml "@THIS_FILE_DIR@/../../vpconfigs/PCIEPlatformTest/PCIeSwitch_ext_properties.xml"'


def backup(path):
    ts = datetime.now().strftime('%Y%m%d_%H%M%S')
    dst = path + f'.bak.{ts}'
    shutil.copy2(path, dst)
    print(f'  backup → {dst}')
    return dst


def write_ext_properties(dry_run):
    """Create/refresh the supplemental PCIeSwitch_ext_properties.xml."""
    lines = [
        '<?xml version="1.0" encoding="ASCII"?>',
        '<!-- Supplemental scml_property_registry entries for PCIeSwitch.USP and .DSP0 -->',
        '<!-- Generated by scripts/patch_pcie_switch_params.py — do not edit manually. -->',
        '<system_parameters>',
        '  <modules>',
    ]
    for module_name in MODULE_NAMES:
        params = MODULE_PARAMS[module_name]
        lines.append('    <module>')
        lines.append(f'      <name>{module_name}</name>')
        lines.append('      <parameters>')
        for pname in sorted(params.keys()):
            val = params[pname]
            lines += [
                '        <parameter>',
                f'          <name>{pname}</name>',
                '          <integer>',
                f'            <value>{val}</value>',
                '          </integer>',
                '        </parameter>',
            ]
        lines.append('      </parameters>')
        lines.append('    </module>')
    lines += ['  </modules>', '</system_parameters>', '']

    content = '\n'.join(lines)
    total = sum(len(MODULE_PARAMS[m]) for m in MODULE_NAMES)
    if dry_run:
        print(f'[DRY-RUN] Would write {total} params total to {EXT_PROPS}')
    else:
        os.makedirs(os.path.dirname(EXT_PROPS), exist_ok=True)
        with open(EXT_PROPS, 'w') as f:
            f.write(content)
        print(f'  wrote {EXT_PROPS} ({len(MODULE_NAMES)} modules)')


def patch_launch_conf(dry_run):
    """Add second --cwr_properties_xml to launch.conf ARGS line if not already present."""
    if not os.path.exists(LAUNCH_CONF):
        print(f'  SKIP: launch.conf not found at {LAUNCH_CONF}')
        return False

    with open(LAUNCH_CONF) as f:
        content = f.read()

    if 'PCIeSwitch_ext_properties.xml' in content:
        print('  launch.conf already has PCIeSwitch_ext_properties.xml — no change needed.')
        return True

    # Insert after the first --cwr_properties_xml argument
    old_arg = '--cwr_properties_xml "@THIS_FILE_DIR@/Properties.xml"'
    new_arg = old_arg + ' ' + EXT_PROPS_ARG
    if old_arg not in content:
        print('  WARNING: Expected --cwr_properties_xml pattern not found in launch.conf.')
        print('           Please manually add:')
        print(f'           {EXT_PROPS_ARG}')
        return False

    new_content = content.replace(old_arg, new_arg, 1)
    if dry_run:
        print(f'[DRY-RUN] Would add second --cwr_properties_xml to {LAUNCH_CONF}')
    else:
        backup(LAUNCH_CONF)
        with open(LAUNCH_CONF, 'w') as f:
            f.write(new_content)
        print(f'  patched {LAUNCH_CONF}')
    return True


def patch_properties_xml(dry_run):
    """Fallback: inject USP/DSP0 module entries directly into Properties.xml."""
    if not os.path.exists(PROPS_XML):
        print(f'  SKIP: Properties.xml not found at {PROPS_XML}')
        return

    tree = ET.parse(PROPS_XML)
    root = tree.getroot()
    modules_elem = root.find('modules')

    existing = set()
    for mod in modules_elem:
        name_e = mod.find('name')
        if name_e is not None:
            existing.add(name_e.text)

    added = []
    for module_name in MODULE_NAMES:
        params = MODULE_PARAMS[module_name]
        if module_name in existing:
            # Module already exists — ensure CC_DEVICE_TYPE is present/correct
            for mod in modules_elem:
                name_e = mod.find('name')
                if name_e is not None and name_e.text == module_name:
                    params_e = mod.find('parameters')
                    has_cdt = any(
                        p.find('name') is not None and p.find('name').text == 'CC_DEVICE_TYPE'
                        for p in params_e
                    )
                    if not has_cdt:
                        param_elem = ET.SubElement(params_e, 'parameter')
                        n = ET.SubElement(param_elem, 'name'); n.text = 'CC_DEVICE_TYPE'
                        t = ET.SubElement(param_elem, 'integer')
                        v = ET.SubElement(t, 'value'); v.text = str(params['CC_DEVICE_TYPE'])
                        added.append(module_name + ' (CC_DEVICE_TYPE only)')
                        print(f'  {module_name}: added missing CC_DEVICE_TYPE = {params["CC_DEVICE_TYPE"]}')
                    else:
                        print(f'  {module_name} already complete — skipping.')
                    break
            continue

        mod_elem = ET.SubElement(modules_elem, 'module')
        name_sub = ET.SubElement(mod_elem, 'name')
        name_sub.text = module_name
        params_sub = ET.SubElement(mod_elem, 'parameters')

        for pname in sorted(params.keys()):
            val = params[pname]
            param_elem = ET.SubElement(params_sub, 'parameter')
            n = ET.SubElement(param_elem, 'name')
            n.text = pname
            t = ET.SubElement(param_elem, 'integer')
            v = ET.SubElement(t, 'value')
            v.text = str(val)

        added.append(module_name)
        print(f'  added {module_name} ({len(params)} params)')

    if not added:
        return

    if not dry_run:
        backup(PROPS_XML)
        tree.write(PROPS_XML, encoding='ASCII', xml_declaration=True)
        print(f'  updated {PROPS_XML}')
    else:
        print(f'[DRY-RUN] Would add {len(added)} module(s) to Properties.xml')


def main():
    dry_run = '--dry-run' in sys.argv
    fallback_only = '--fallback-props-only' in sys.argv

    print('=== patch_pcie_switch_params.py ===')
    if dry_run:
        print('(DRY-RUN mode — no files will be written)')

    print()
    print('Step 1: Write/refresh supplemental PCIeSwitch_ext_properties.xml')
    write_ext_properties(dry_run)

    if not fallback_only:
        print()
        print('Step 2: Patch launch.conf to load supplemental properties XML')
        ok = patch_launch_conf(dry_run)

        if not ok:
            print()
            print('Step 3: Fallback — patching Properties.xml directly')
            patch_properties_xml(dry_run)
    else:
        print()
        print('Step 2: Patching Properties.xml directly (--fallback-props-only)')
        patch_properties_xml(dry_run)

    print()
    print('Done. After a VDK rebuild (vpx generate), re-run this script to restore')
    print('the launch.conf patch. The PCIeSwitch_ext_properties.xml is persistent.')


if __name__ == '__main__':
    main()
