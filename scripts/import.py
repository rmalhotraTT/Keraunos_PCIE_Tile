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
try:
    _ = vdkcreator
except:
    raise RuntimeError("This script needs to be run in Virtualizer Studio!")

import os, inspect
script_dir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))
vdk_dir = os.path.normpath(os.path.join(script_dir, ".."))

try:
    copy_project
except NameError:
    copy_project = False

if copy_project:
    print("Creating new project by copying '" + vdk_dir + "'")
else:
    print("Importing project from '" + vdk_dir + "'")

try:
    build_disabled
except NameError:
    build_disabled = False

vdk = vdkcreator.import_project(vdk_dir, copy=copy_project, build_disabled=build_disabled)
print("Import done!")
