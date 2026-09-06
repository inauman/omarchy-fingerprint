#!/usr/bin/env python3
"""Capture libfprint's calibration debug output for the Elan 04f3:0c3d reader.

Calibration runs at device activation, before any finger is needed, so this
reaches the failure without a single swipe. The elan driver logs its firmware
version, the background/calibration means and delta, and every calibration
status byte -- which is what distinguishes "needs more attempts" from
"never reports 0x01" as the cause of "Calibration failed!".
"""
import os, sys

os.environ["FP_DRIVERS_ALLOWLIST"] = "elan"
os.environ["G_MESSAGES_DEBUG"] = "all"

import gi
gi.require_version("FPrint", "2.0")
from gi.repository import FPrint, GLib

ctx = FPrint.Context()
ctx.enumerate()
devices = ctx.get_devices()
print(f"devices: {len(devices)}", flush=True)
if not devices:
    sys.exit("no elan device found (is fprintd holding it?)")

dev = devices[0]
print(f"driver={dev.get_driver()} name={dev.get_name()} "
      f"stages={dev.get_nr_enroll_stages()}", flush=True)

try:
    dev.open_sync()
    print("open_sync OK", flush=True)
except GLib.Error as e:
    sys.exit(f"open failed: {e.message}")

template = FPrint.Print.new(dev)
template.props.finger = FPrint.Finger.RIGHT_INDEX
template.props.username = "probe"

def progress_cb(device, completed, print_, error, user_data):
    print(f"  stage {completed}: err={error.message if error else None}", flush=True)

try:
    dev.enroll_sync(template, None, progress_cb, None)
    print("ENROLL SUCCEEDED", flush=True)
except GLib.Error as e:
    print(f"ENROLL FAILED: {e.message}", flush=True)
finally:
    try:
        dev.close_sync()
    except Exception:
        pass
