#!/bin/bash
# Swap stock libfprint for Omarchy's master snapshot and point fprintd at the USB elan driver.
set -u

echo "== 1. Resetting driver allowlist to 'elan' (was 'elanmoc' from the test) =="
mkdir -p /etc/systemd/system/fprintd.service.d
printf '[Service]\nEnvironment=FP_DRIVERS_ALLOWLIST=elan\n' > /etc/systemd/system/fprintd.service.d/allowlist.conf

echo
echo "== 2. Installing omarchy/libfprint-git =="
# libfprint-git provides+conflicts libfprint, and --noconfirm answers the
# conflict prompt with N and aborts. Pre-remove stock libfprint (deps-only, so
# fprintd stays installed) -- the same pattern omarchy's own script uses in reverse.
if /usr/bin/pacman -Q libfprint &>/dev/null; then
  /usr/bin/pacman -Rdd --noconfirm libfprint || { echo "FAILED to remove stock libfprint"; exit 1; }
fi

if ! /usr/bin/pacman -S --noconfirm omarchy/libfprint-git; then
  echo "!! install FAILED - restoring stock libfprint"
  /usr/bin/pacman -S --noconfirm extra/libfprint
  exit 1
fi

echo
echo "== 3. Restarting fprintd =="
/usr/bin/systemctl daemon-reload
/usr/bin/systemctl restart fprintd
sleep 3

echo
echo "== 4. Result =="
/usr/bin/pacman -Q libfprint-git 2>/dev/null || /usr/bin/pacman -Q libfprint
echo -n "devices seen by fprintd: "
/usr/bin/busctl --system call net.reactivated.Fprint /net/reactivated/Fprint/Manager net.reactivated.Fprint.Manager GetDevices 2>&1
