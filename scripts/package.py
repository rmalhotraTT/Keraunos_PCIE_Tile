#############################################################################
# Copyright 1996-2025 Synopsys, Inc.                                        #
#                                                                           #
# This Synopsys software and all associated documentation are proprietary   #
# to Synopsys, Inc. and may only be used pursuant to the terms and          #
# conditions of a written license agreement with Synopsys, Inc.             #
# All other use, reproduction, modification, or distribution of the         #
# Synopsys software or the associated documentation is strictly prohibited. #
#############################################################################

from __future__ import print_function
from builtins import str
try:
    _ = vdkcreator
except:
    raise RuntimeError("This script needs to be run in Virtualizer Studio!")

import os, inspect
script_dir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
vdk_dir = os.path.normpath(os.path.join(script_dir, ".."))

try:
    _ = vdk
    if not vdk.get_filename() or not os.path.realpath(vdk.get_filename()).startswith(os.path.realpath(vdk_dir) + os.sep):
        raise
except:
    print("Importing project from '" + vdk_dir)
    vdk = vdkcreator.import_project(vdk_dir)

try:
    config_names = packaging_configs
except:
    config_names = [x.get_name() for x in vdk.get_packaging_configurations()]
    if not config_names:
        raise RuntimeError("The VDK project in '" + vdk.get_filename() + "' does not have any packaging configurations.")

markers = vdk.get_markers()
if markers:
    print("Warning! The following problems are present in the design which may lead to build issues:")
    print("\n".join(sorted(str(x) for x in markers)))
    print("")

packaging_messages = []
for config_name in config_names:
    config = vdk.get_packaging_configuration(config_name)
    if not config:
        raise RuntimeError("The VDK project in '" + vdk.get_filename() + "' does not have a packaging configuration of name '" + config_name + "'")
    print("Packaging configuration '" + config_name + "' of project '" + vdk.get_filename() + "'")
    vdk.create_package(config)
    packaging_messages.extend(vdk.get_packaging_messages())
    print("")
print("Packaging done!")
if packaging_messages:
    print("Packaging messages are available in variable 'packaging_messages'")
