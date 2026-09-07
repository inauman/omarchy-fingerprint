#!/bin/bash
# Build libfprint master with the elanpress driver for the Elan 04f3:0c3d pad.
#
#   tools/elanpress/build.sh            build into build/libfprint (no root)
#   sudo tools/elanpress/build.sh install
#                                       copy the library to /opt/libfprint-0c3d,
#                                       point fprintd at it, restart fprintd
#
# The packaged libfprint is never touched. Uninstall = delete
# /opt/libfprint-0c3d and /etc/systemd/system/fprintd.service.d/allowlist.conf,
# then `systemctl daemon-reload`.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
PROJECT=$(cd "$HERE/../.." && pwd)
SRC=$PROJECT/build/libfprint
BUILD=$SRC/build
DEST=/opt/libfprint-0c3d
DROPIN=/etc/systemd/system/fprintd.service.d/allowlist.conf
PIN=${LIBFPRINT_REF:-master}

do_build() {
  if [[ ! -d $SRC/.git ]]; then
    mkdir -p "$PROJECT/build"
    git clone https://gitlab.freedesktop.org/libfprint/libfprint.git "$SRC"
  fi
  git -C "$SRC" checkout -q "$PIN"
  git -C "$SRC" checkout -q -- libfprint/drivers/elan.h meson.build libfprint/meson.build

  # 1. the driver
  cp "$HERE"/elanpress.c "$HERE"/elanpress.h "$HERE"/elanpress-match.c \
     "$HERE"/elanpress-match.h "$SRC/libfprint/drivers/"

  # 2. register it; the elan driver must no longer claim 0c3d
  sed -i "s|    'elan': { 'endian_dependent': true },|    'elan': { 'endian_dependent': true },\n    'elanpress': { 'endian_dependent': true },|" "$SRC/meson.build"
  sed -i "s|    'elan' : files('drivers/elan.c'),|    'elan' : files('drivers/elan.c'),\n    'elanpress' : files('drivers/elanpress.c', 'drivers/elanpress-match.c'),|" "$SRC/libfprint/meson.build"
  sed -i "/pid = 0x0c3d, .driver_data = ELAN_ALL_DEV/d" "$SRC/libfprint/drivers/elan.h"
  grep -q "'elanpress'" "$SRC/meson.build"
  grep -q "'elanpress'" "$SRC/libfprint/meson.build"

  # 3. build
  if [[ ! -f $BUILD/build.ninja ]]; then
    meson setup "$BUILD" "$SRC" -Ddoc=false -Dintrospection=false \
      -Dinstalled-tests=false -Dgtk-examples=false -Dudev_rules=disabled \
      -Dudev_hwdb=disabled
  fi
  ninja -C "$BUILD"
  echo
  echo "built: $BUILD/libfprint/libfprint-2.so.2.0.0"
  echo -n "drivers in this build: "
  strings "$BUILD/libfprint/libfprint-2.so.2.0.0" | grep -c "press-type fingerprint sensor" | sed "s/^1$/elanpress present/"
  echo
}

do_install() {
  if [[ $EUID -ne 0 ]]; then
    echo "install needs root: sudo $0 install" >&2
    exit 1
  fi
  if [[ ! -f $BUILD/libfprint/libfprint-2.so.2.0.0 ]]; then
    echo "no build found, run $0 first (as your user)" >&2
    exit 1
  fi
  mkdir -p "$DEST"
  cp -a "$BUILD"/libfprint/libfprint-2.so* "$DEST"/
  mkdir -p "$(dirname "$DROPIN")"
  cat > "$DROPIN" <<CONF
[Service]
# libfprint master + elanpress driver for the Elan 04f3:0c3d press sensor.
Environment=LD_LIBRARY_PATH=$DEST
# Only the press driver: keeps the swipe-mode elan driver and the broken
# built-in ELAN7001 (elanspi) out of fprintd's device list.
Environment=FP_DRIVERS_ALLOWLIST=elanpress
# Driver decisions and scores in the journal (journalctl -u fprintd).
Environment=G_MESSAGES_DEBUG=libfprint-elanpress
CONF
  systemctl daemon-reload
  systemctl restart fprintd || true
  sleep 2
  echo "installed to $DEST; fprintd devices:"
  busctl --system call net.reactivated.Fprint /net/reactivated/Fprint/Manager \
    net.reactivated.Fprint.Manager GetDevices || true
  echo
  echo "Enrolled fingers are kept. Only templates from the old swipe-mode elan"
  echo "driver are unreadable; if verification says the print is unusable,"
  echo "re-enrol through Setup > Security > Fingerprint."
}

case "${1:-build}" in
  build) do_build ;;
  install) do_install ;;
  *) echo "usage: $0 [build|install]" >&2; exit 2 ;;
esac
