#!/bin/bash
# Install the patched libfprint for the Elan 04f3:0c3d reader, scoped to fprintd.
#
# The packaged libfprint is left alone; only fprintd gets LD_LIBRARY_PATH
# pointing at the patched copy, so nothing else on the system is affected and
# uninstalling is deleting one drop-in file plus /opt/libfprint-0c3d.
set -e

BUILD=/tmp/lfp/build/libfprint
DEST=/opt/libfprint-0c3d
DROPIN=/etc/systemd/system/fprintd.service.d/allowlist.conf

if [[ ! -f $BUILD/libfprint-2.so.2.0.0 ]]; then
  echo "Patched build not found at $BUILD."
  echo "Rebuild first: see tools/rebuild-patched-driver.sh"
  exit 1
fi

echo "Installing patched libfprint to $DEST ..."
mkdir -p "$DEST"
cp -a "$BUILD"/libfprint-2.so* "$DEST"/

echo "Pointing fprintd at it ..."
mkdir -p "$(dirname "$DROPIN")"
cat > "$DROPIN" <<EOF
[Service]
# Patched libfprint with the 04f3:0c3d calibration + capture quirks.
Environment=LD_LIBRARY_PATH=$DEST
# Keep the broken built-in ELAN7001 SPI sensor out of fprintd's device list.
Environment=FP_DRIVERS_ALLOWLIST=elan
EOF

systemctl daemon-reload
systemctl restart fprintd
sleep 3

echo
echo "=== verification ==="
PID=$(systemctl show -p MainPID --value fprintd)
if [[ -n $PID && $PID != 0 ]]; then
  echo -n "libfprint loaded by fprintd: "
  tr '\0' '\n' < /proc/"$PID"/maps 2>/dev/null | grep -o '[^ ]*libfprint-2\.so[^ ]*' | sort -u | head -2
fi
echo -n "devices seen by fprintd: "
busctl --system call net.reactivated.Fprint /net/reactivated/Fprint/Manager \
  net.reactivated.Fprint.Manager GetDevices
