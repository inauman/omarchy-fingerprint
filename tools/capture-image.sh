#!/bin/bash
# Capture the assembled fingerprint image to a file so it can be inspected.
# The matcher's verdict is only as good as this image -- if it looks like noise
# or a smeared strip, the problem is assembly, not the template.
#
# Run as root:  sudo bash tools/capture-image.sh
OUT=$(cd "$(dirname "$0")" && pwd)/finger.pgm

systemctl stop fprintd
sleep 1

echo "=== SWIPE REPEATEDLY (slow, full-length) until this exits ==="
echo "A short swipe is silently ignored here, so keep going."

LD_LIBRARY_PATH=/opt/libfprint-0c3d \
  FP_DRIVERS_ALLOWLIST=elan \
  timeout 120 /tmp/lfp/build/examples/img-capture "$OUT"

echo "exit: $?"
chown nauman:nauman "$OUT" 2>/dev/null
systemctl start fprintd
ls -la "$OUT" 2>/dev/null && echo "saved: $OUT"
