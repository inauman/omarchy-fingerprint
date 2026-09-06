#!/usr/bin/env python3
"""Run a full scripted enrollment against libfprint's virtual sensor.

Feeds the driver a deliberate sequence -- two good scans, a retry, then the
rest good -- so the per-stage callbacks an enrollment UI listens to can be
exercised without touching a real reader.
"""
import os, socket, tempfile, threading, time

sock_path = os.path.join(tempfile.mkdtemp(), "virt.sock")
os.environ["FP_VIRTUAL_DEVICE"] = sock_path
os.environ["FP_DRIVERS_ALLOWLIST"] = "virtual_device"

import gi
gi.require_version("FPrint", "2.0")
from gi.repository import FPrint

ctx = FPrint.Context()
ctx.enumerate()
dev = ctx.get_devices()[0]
dev.open_sync()

stages = dev.get_nr_enroll_stages()
print(f"device: {dev.get_name()}  stages: {stages}\n")

def send(cmd):
    """One command per connection -- the virtual driver expects it that way."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock_path)
    s.sendall(cmd.encode("utf-8"))
    s.close()

# RETRY 1 == FP_DEVICE_RETRY_TOO_SHORT, i.e. the "swipe too short" you kept hitting.
script = ["SCAN good", "SCAN good", "RETRY 1"] + ["SCAN good"] * stages

def feeder():
    time.sleep(0.4)
    for cmd in script:
        send(cmd)
        time.sleep(0.25)

threading.Thread(target=feeder, daemon=True).start()

seen = {"n": 0}
def progress_cb(device, completed, print_, error, user_data):
    seen["n"] = completed
    if error:
        print(f"  stage {completed}/{stages}  RETRY -> {error.message}")
    else:
        print(f"  stage {completed}/{stages}  passed")

template = FPrint.Print.new(dev)
template.props.finger = FPrint.Finger.RIGHT_INDEX
template.props.username = "demo"

result = dev.enroll_sync(template, None, progress_cb, None)
print(f"\nenrollment complete: {result.props.finger.value_nick} for {result.props.username}")
dev.close_sync()
