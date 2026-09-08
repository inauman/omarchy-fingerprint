#!/bin/bash
# Test the patched libfprint WITHOUT installing it system-wide.
# LD_LIBRARY_PATH/GI_TYPELIB_PATH point at the build tree, so the system
# libfprint is untouched and reverting means deleting /tmp/lfp.
#
# Run as root:  sudo bash tools/test-patched-driver.sh
BUILD=/tmp/lfp/build/libfprint
LOG=$(cd "$(dirname "$0")" && pwd)/elan-0c3d-patched.log

if [[ ! -f $BUILD/libfprint-2.so.2.0.0 ]]; then
  echo "Patched build not found at $BUILD -- rebuild with: ninja -C /tmp/lfp/build"
  exit 1
fi

echo "Stopping fprintd so libfprint can claim the device..."
systemctl stop fprintd
sleep 1

echo
echo "=== SWIPE REPEATEDLY once it starts ==="
echo "The sensor must get WARM -- a cold sensor skips calibration entirely"
echo "and won't exercise the patch. Slow full-length drags, needs >= 7 frames."
echo

{
  echo "### $(date -Is)  PATCHED BUILD"
  echo "### $(git -C /tmp/lfp log --oneline -1)"
  LD_LIBRARY_PATH="$BUILD" \
  GI_TYPELIB_PATH="$BUILD" \
  timeout 180 python3 "$(dirname "$0")/elan-debug-probe.py" 2>&1
  echo "### exit: $?"
} | tee "$LOG"

chown nauman:nauman "$LOG"
systemctl start fprintd
echo
echo "Log saved to $LOG"
