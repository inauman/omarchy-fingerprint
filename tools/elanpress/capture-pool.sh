#!/bin/bash
# Collect a capture pool for offline tuning of the matcher.
#
#   sudo tools/elanpress/capture-pool.sh right-index 20
#   sudo tools/elanpress/capture-pool.sh right-thumb 20
#   sudo tools/elanpress/capture-pool.sh left-index 20      # impostor finger
#
# Each run asks for N touches of one finger and writes the processed
# images to pool/<label>-tNNN-fNN.pgm (plus the raw 16-bit frames and the
# background under pool/raw/). Then:
#
#   tools/elanpress/elanpress-eval pool pool 10
#
# scores every genuine and impostor pair, and reports the acceptance rate
# at FAR=0 and at the driver's current threshold, using touches 1..10 of
# each finger as the enrolled template.
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
PROJECT=$(cd "$HERE/../.." && pwd)
BUILD=$PROJECT/build/libfprint/build
LABEL=${1:?label, e.g. right-index}
TOUCHES=${2:-20}
POOL=$PROJECT/pool
OWNER=${SUDO_USER:-$USER}

if [[ $EUID -ne 0 ]]; then
  echo "needs root to open the USB device: sudo $0 $*" >&2
  exit 1
fi
if [[ $LABEL =~ -t[0-9] ]]; then
  echo "label must not contain '-t' (it separates label from touch number)" >&2
  exit 1
fi

TMP=$(mktemp -d)
mkdir -p "$POOL/raw"
systemctl stop fprintd 2>/dev/null
cd "$TMP"

echo "=================================================================="
echo " $LABEL: $TOUCHES touches. Vary the placement between presses."
echo "=================================================================="
LD_LIBRARY_PATH=$BUILD/libfprint FP_DRIVERS_ALLOWLIST=elanpress \
  G_MESSAGES_DEBUG=libfprint-elanpress FP_ELANPRESS_DUMP_DIR=$TMP \
  FP_ELANPRESS_ENROLL_STAGES=$TOUCHES \
  bash -c "printf '6\n' | '$BUILD/examples/enroll'" 2>&1 | \
  grep -E "frame [0-9]+:|touch [0-9]+:|background|Enroll|WARNING|rror"

# offset touch numbers past anything already in the pool for this label
last=$(ls "$POOL"/"$LABEL"-t*.pgm 2>/dev/null | sed -E 's/.*-t([0-9]+)-.*/\1/' | sort -n | tail -1)
last=${last:-0}
count=0
for f in "$TMP"/img-*-t*.pgm; do
  [[ -e $f ]] || continue
  b=$(basename "$f" .pgm)          # img-112233-t003-f07
  tf=${b##*-t}                     # 003-f07
  t=$((10#${tf%%-*} + 1 + last))
  fr=${tf##*-f}
  cp "$f" "$POOL/$LABEL-t$(printf %03d "$t")-f$fr.pgm"
  count=$((count + 1))
done
for f in "$TMP"/raw-*-t*.pgm "$TMP"/bg-*-t*.pgm; do
  [[ -e $f ]] || continue
  b=$(basename "$f" .pgm)
  tag=${b%%-*}
  tf=${b##*-t}
  t=$((10#${tf%%-*} + 1 + last))
  cp "$f" "$POOL/raw/$LABEL-$tag-t$(printf %03d "$t")-f${tf##*-f}.pgm"
done
chown -R "$OWNER": "$POOL"
rm -rf "$TMP"
echo
echo "$count images added for $LABEL; pool now has $(ls "$POOL"/*.pgm | wc -l) images"
