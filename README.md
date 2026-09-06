# omarchy-fingerprint

Getting fingerprint authentication working on an Omarchy (Arch) laptop, so
`sudo` and the lock screen accept a fingerprint instead of a typed password.

Two sensors were investigated. **Neither works end to end yet.** The technical
trail is in [FINDINGS.md](FINDINGS.md); this file covers the goal, the hardware,
and where things stand.

## Goal

Replace password entry for `sudo`, polkit and the lock screen with a
fingerprint, using Omarchy's built-in support:

```
omarchy setup security fingerprint    # configures PAM for sudo, polkit, lock screen
omarchy remove security fingerprint   # clean undo
```

That command is not the problem — it does the right thing. The problem is that
neither sensor produces a fingerprint template that verifies.

## Hardware

### 1. Built-in — ASUS laptop, square pad in the touchpad corner

| | |
|---|---|
| Chip | ELAN7001, SPI (not USB) |
| sysfs | `spi-ELAN7001:00`, bound to `spidev`, `/dev/spidev2.0` |
| libfprint driver | `elanspi` |
| fprintd name | ElanTech Embedded Fingerprint Sensor |
| Scan type | swipe, 8 enroll stages |

Present from the start but invisible to `omarchy hw fingerprint`, which only
scans USB sysfs. libfprint's own udev rule binds it correctly, so the plumbing
is fine — the sensor itself never initialises reliably.

**Status: unusable.** Enrolls all 8 stages once, then `verify-no-match`. The
journal shows repeated `<init/otp> timed out waiting for vcom detection`,
`Device disabled to prevent overheating` (libfprint's thermal model tripping
because the driver burns seconds retrying), and assertion failures inside
libfprint. Excluded from fprintd via `FP_DRIVERS_ALLOWLIST=elan`.

### 2. External — TEC USB fingerprint reader

[amazon.com/dp/B08DCFMSLG](https://www.amazon.com/TEC-Fingerprint-Bio-Metric-Password-Free-Encryption/dp/B08DCFMSLG)
— sold for Windows Hello; the listing says to *touch the sensor for 1–2 seconds*.

| | |
|---|---|
| USB ID | `04f3:0c3d` Elan Microelectronics, `ELAN:Fingerprint` |
| Firmware | `0x0165` (`bcdDevice 1.65`) |
| libfprint driver | `elan` |
| fprintd name | ElanTech Fingerprint Sensor |
| Scan type | swipe (hardcoded), 5–6 enroll stages, 64 × 50 frames |

**Status: enrolls reliably, does not verify.** Originally could not enroll at
all — `Calibration failed!` on every attempt. Three driver quirks and one
general libfprint bug were found and fixed; see
[FINDINGS.md](FINDINGS.md). Enrollment now completes cleanly every time.
Verification still returns `verify-no-match`, because the driver treats this
press sensor as a swipe sensor and assembles 30 views of a settling finger into
a collage that differs on every capture.

## System

Omarchy / Arch, kernel 7.1.9-arch1, fprintd 1.94.5, libfprint master (1.94.100)
built from source with the patch in `tools/`.

## Where it stands

| | Enrolls | Verifies | Blocker |
|---|---|---|---|
| Built-in ELAN7001 | once, unreliably | no | Sensor never initialises; `elanspi` unusable on this machine |
| TEC 04f3:0c3d | yes, reliably | no | Press sensor driven as a swipe sensor; NBIS can't match the assembled collage |

PAM was never modified — login is untouched and there is nothing to undo on that
front. `tools/setup-pam-fingerprint.sh` is written and ready but deliberately
unrun, since wiring fingerprint into `sudo` against a template that never
matches would only add a delay before the password prompt.

## What was tried

Roughly in order:

1. **`omarchy setup security fingerprint`** — enrolled against the *built-in*
   sensor without saying so, and silently did nothing. fprintd picks a default
   device, and the built-in one sorted first.
2. **Isolating each sensor** with `FP_DRIVERS_ALLOWLIST` (`elanspi` vs `elan`)
   in a fprintd systemd drop-in. This is what made the two devices separable and
   is still in place.
3. **Built-in sensor** — enrolled 8 stages after several attempts; verify failed.
   Repeated init timeouts and thermal shutdowns. Abandoned.
4. **Newer libfprint** — `omarchy/libfprint-git` (master snapshot). No change;
   identical `Calibration failed!`.
5. **Reading the driver source** rather than guessing — `elan.c` / `elan.h` from
   libfprint master, with debug builds and full driver logging.
6. **Four patch iterations** on the dongle, two of which were wrong and were
   killed by measurement rather than argument. Details in FINDINGS.md.
7. **Dumping the actual image** with libfprint's `img-capture` example — the
   step that turned inference into evidence and identified the press/swipe
   mismatch.

## Currently installed on this machine

- Patched libfprint at `/opt/libfprint-0c3d`
- `/etc/systemd/system/fprintd.service.d/allowlist.conf` pointing fprintd at it
  and restricting drivers to `elan`

The packaged libfprint is untouched; only fprintd's environment differs. To
remove, delete both and `systemctl daemon-reload`.

The source tree used to build it lives in `/tmp` and does not survive a reboot.
Rebuild with `tools/0001-elan-0c3d-quirks.patch` against libfprint master.

## Next steps

1. **Submit the `ppmm` fix upstream** — independent of this hardware and the
   easiest review. `elan` never sets `FpImage.ppmm`, so NBIS receives 0. Affects
   every Elan sensor.
2. **Post FINDINGS.md to
   [iafilatov/libfprint#53](https://github.com/iafilatov/libfprint/issues/53)**,
   which reports this exact device with no diagnostic data attached.
3. **Per-device `scan_type`** — `elan.c` hardcodes `FP_SCAN_TYPE_SWIPE` for every
   device it supports. Making it per-device is the architecturally correct fix,
   though a 64 × 50 frame already defeated NBIS minutiae detection twice.
4. **`omarchy setup security fido2`** — if the practical goal is not typing a
   password, a hardware security key is the path that works today on Arch.

## Repo layout

```
FINDINGS.md   technical write-up of the 04f3:0c3d investigation
tools/        patch, debug scripts, and logs from each stage
```

Captured fingerprint images are gitignored. They are real biometric data, and
FINDINGS.md describes what they showed.
