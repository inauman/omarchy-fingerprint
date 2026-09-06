#!/bin/bash
# First hardware test of the elanpress driver, without touching fprintd's
# configuration. Enrolls one finger with libfprint's example program, then
# runs verification attempts. Every frame is dumped so the matcher can be
# tuned offline afterwards.
#
#   sudo tools/elanpress/hw-test.sh              enroll + verify, right index
#   sudo tools/elanpress/hw-test.sh verify       verify only (reuses template)
#
# Finger numbers for the example programs: 5 right thumb, 6 right index,
# 7 right middle (FINGER=n to change).
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
PROJECT=$(cd "$HERE/../.." && pwd)
BUILD=$PROJECT/build/libfprint/build
LIB=$BUILD/libfprint
WORK=$PROJECT/hw-test
FINGER=${FINGER:-6}
OWNER=${SUDO_USER:-$USER}

if [[ $EUID -ne 0 ]]; then
  echo "needs root to open the USB device: sudo $0 $*" >&2
  exit 1
fi
if [[ ! -x $BUILD/examples/enroll ]]; then
  echo "build first (as your user): tools/elanpress/build.sh" >&2
  exit 1
fi
if ! lsusb -d 04f3:0c3d >/dev/null; then
  echo "04f3:0c3d is not plugged in" >&2
  exit 1
fi

mkdir -p "$WORK/dump"
cd "$WORK"
export LD_LIBRARY_PATH=$LIB
export FP_DRIVERS_ALLOWLIST=elanpress
export G_MESSAGES_DEBUG=${G_MESSAGES_DEBUG:-libfprint-elanpress}
export FP_ELANPRESS_DUMP_DIR=$WORK/dump

systemctl stop fprintd 2>/dev/null

if [[ ${1:-all} == all ]]; then
  echo "=================================================================="
  echo " ENROLL: press the finger flat on the pad, hold about one second,"
  echo " lift. Repeat for every stage, and SHIFT THE FINGER A LITTLE"
  echo " between presses (centre, up, down, left, right, corners) so the"
  echo " template covers more of the finger than a 3x4 mm patch."
  echo "=================================================================="
  printf '%s\n' "$FINGER" | "$BUILD/examples/enroll" 2>&1 | tee enroll.log
  echo
fi

echo "=================================================================="
echo " VERIFY: press the same finger naturally. Ctrl-C to stop."
echo " Then try a DIFFERENT finger a few times: it must be rejected."
echo "=================================================================="
trap 'echo; echo "stopped"; exit 0' INT TERM
n=0
while :; do
  n=$((n + 1))
  echo "--- attempt $n"
  printf '%s\n' "$FINGER" | "$BUILD/examples/verify" 2>&1 | tee -a verify.log | \
    grep -E "MATCH|verify:|Verify result|frame [0-9]+:|touch [0-9]+:|kept|WARNING|CRITICAL|rror"
  [[ ${PIPESTATUS[1]} -gt 128 ]] && break
done
