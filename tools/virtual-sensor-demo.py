#!/usr/bin/env python3
"""Drive a libfprint virtual fingerprint sensor -- no hardware, no root.

libfprint ships virtual drivers for its own test suite. Point FP_VIRTUAL_DEVICE
at a socket path and the driver creates a device that takes scripted commands,
so an enrollment UI can be developed and tested against deterministic input.
"""
import os, socket, tempfile, sys

sock_path = os.path.join(tempfile.mkdtemp(), "virt.sock")
os.environ["FP_VIRTUAL_DEVICE"] = sock_path
os.environ["FP_DRIVERS_ALLOWLIST"] = "virtual_device"

import gi
gi.require_version("FPrint", "2.0")
from gi.repository import FPrint

ctx = FPrint.Context()
ctx.enumerate()
devices = ctx.get_devices()

print(f"socket: {sock_path}")
print(f"devices found: {len(devices)}")
for d in devices:
    print(f"  driver          : {d.get_driver()}")
    print(f"  name            : {d.get_name()}")
    print(f"  enroll stages   : {d.get_nr_enroll_stages()}")
    print(f"  scan type       : {d.get_scan_type().value_nick}")
