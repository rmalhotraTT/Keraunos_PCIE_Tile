#############################################################################
# Copyright 1996-2025 Synopsys, Inc.                                        #
#                                                                           #
# This Synopsys software and all associated documentation are proprietary   #
# to Synopsys, Inc. and may only be used pursuant to the terms and          #
# conditions of a written license agreement with Synopsys, Inc.             #
# All other use, reproduction, modification, or distribution of the         #
# Synopsys software or the associated documentation is strictly prohibited. #
#############################################################################

import os, inspect
script_dir = os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe())))

copy_project = True
exec(open(os.path.join(script_dir, "build.py")).read())
