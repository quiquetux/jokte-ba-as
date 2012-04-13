#!/bin/bash
#
# VirtualBox VNIC terminate script for Solaris hosts with Crossbow.
#
# Copyright (C) 2007-2009 Oracle Corporation
#
# This file is part of VirtualBox Open Source Edition (OSE), as
# available from http://www.virtualbox.org. This file is free software;
# you can redistribute it and/or modify it under the terms of the GNU
# General Public License (GPL) as published by the Free Software
# Foundation, in version 2 as it comes in the "COPYING" file of the
# VirtualBox OSE distribution. VirtualBox OSE is distributed in the
# hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
#

if [ -z "$1" ]; then
    echo "Missing VNIC interface name."
    echo
    echo "Usage: $0 vnicname"
    exit 1
fi

/sbin/ifconfig $1 unplumb
vnic_id=${1##*[a-z]}
/usr/lib/vna ${vnic_id}
exit $?

