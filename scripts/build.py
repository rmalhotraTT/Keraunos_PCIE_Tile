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
    snps_build_blackbox_subsystem
except NameError:
    snps_build_blackbox_subsystem = False

try:
    _ = vdk
    if not vdk.get_filename() or not os.path.realpath(vdk.get_filename()).startswith(os.path.realpath(vdk_dir) + os.sep):
        raise
except:
    try:
        copy_project
    except NameError:
        copy_project = False
    if copy_project:
        print("Creating new project by copying '" + vdk_dir + "'")
    else:
        print("Importing project from '" + vdk_dir + "'")
    vdk = vdkcreator.import_project(vdk_dir, copy=copy_project)

markers = vdk.get_markers()
if markers:
    print("Warning! The following problems are present in the design which may lead to build issues:")
    print("\n".join(sorted(str(x) for x in markers)))
    print("")

print("Building project '" + vdk.get_filename() + "'")
vdk.build(blackbox_subsystem=snps_build_blackbox_subsystem)
build_messages = vdk.get_build_messages()
print("Build done!")
if build_messages:
    print("Build messages are available in variable 'build_messages'")
