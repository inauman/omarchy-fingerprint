#!/bin/bash
# Capture libfprint elan debug output during an enroll WITH finger swipes.
# The post-swipe recalibration is the only path that reaches the status-polling
# loop, so this is what reveals the 'calibration status: 0xNN' bytes.
# Run as root: sudo bash tools/capture-calibration-log.sh
LOG=$(cd "$(dirname "$0")" && pwd)/elan-0c3d-swipe-debug.log

echo "Stopping fprintd so libfprint can claim the device..."
systemctl stop fprintd
sleep 1

echo
echo "=== SWIPE YOUR FINGER REPEATEDLY when it starts ==="
echo "Slow, full-length drags (needs >= 7 frames). Keep going until it stops."
echo

{
  echo "### $(date -Is)"
  pacman -Q libfprint-git 2>/dev/null || pacman -Q libfprint
  lsusb -d 04f3:0c3d
  timeout 120 python3 "$(dirname "$0")/elan-debug-probe.py" 2>&1
  echo "### exit: $?"
} | tee "$LOG"

chown nauman:nauman "$LOG"
systemctl start fprintd
echo
echo "Log saved to $LOG"
