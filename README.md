# omarchy-fingerprint

Getting fingerprint authentication working on an Omarchy (Arch) laptop, so
`sudo` and the lock screen accept a fingerprint instead of a typed password.

Two sensors were investigated. **The external one now works end to end**
with a press-mode driver written for it (`tools/elanpress/`); the built-in
one is still unusable. The technical trail is in [FINDINGS.md](FINDINGS.md);
this file covers the goal, the hardware, and where things stand.

## Goal

Replace password entry for `sudo`, polkit and the lock screen with a
fingerprint, using Omarchy's built-in support:

```
omarchy setup security fingerprint    # configures PAM for sudo, polkit, lock screen
omarchy remove security fingerprint   # clean undo
```

That command is not the problem — it does the right thing. The problem was
that neither sensor produced a fingerprint template that verifies; the
external one does now.

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

**Status: works with the new press-mode driver; ~60 % of touches accepted, zero false accepts measured.**
With the stock `elan` driver it originally could not enroll at all, and after
four quirk fixes it enrolled but never verified: the driver treats this press
sensor as a swipe sensor, throws away 43 % of every frame, and hands a 3 × 4 mm
patch to a minutiae matcher that refuses to score it. The replacement is
[`tools/elanpress/`](tools/elanpress/README.md): a press-mode driver with a
correlation matcher, the approach the Windows driver and two independent
Linux projects for sibling Elan pads use. See FINDINGS.md for the trail.

## System

Omarchy / Arch, kernel 7.1.9-arch1, fprintd 1.94.5, libfprint master (1.94.100)
built from source with the patch in `tools/`.

## Where it stands

| | Enrolls | Verifies | Blocker |
|---|---|---|---|
| Built-in ELAN7001 | once, unreliably | no | Sensor never initialises; `elanspi` unusable on this machine. A [SIGFM press-mode patch](https://github.com/Ajaneeshwar/x571gt-fingerprint) exists for the same die in another ASUS model and is the thing to try next |
| TEC 04f3:0c3d, stock `elan` driver | yes, reliably | no | Press sensor driven as a swipe sensor; NBIS can't match the assembled collage |
| TEC 04f3:0c3d, `elanpress` driver | yes, 20 touches | **yes**, ~60 % of touches at threshold 0.76, 0/91 impostor images accepted | Coverage: a 3 × 4 mm patch must overlap an enrolled one; expect an occasional second press |

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

## Currently installed on this machine (2026-09-06)

- libfprint master + `elanpress` at `/opt/libfprint-0c3d`, with
  `/etc/systemd/system/fprintd.service.d/allowlist.conf` pointing fprintd at
  it and restricting drivers to `elanpress`.
- Right index enrolled with 20 touches; `omarchy setup security fingerprint`
  has configured PAM for sudo, polkit and the lock screen.
- Source and build in `build/libfprint/` (gitignored, survives reboots);
  `tools/elanpress/build.sh` rebuilds, `sudo tools/elanpress/build.sh install`
  reinstalls.

The packaged libfprint is untouched; only fprintd's environment differs. To
remove: `omarchy remove security fingerprint`, delete `/opt/libfprint-0c3d`
and the drop-in, then `systemctl daemon-reload`.

## Next steps

1. **Live with it for a few days.** Scores for every attempt are in
   `journalctl -u fprintd`. If misses are too frequent, raise coverage:
   `Environment=FP_ELANPRESS_ENROLL_STAGES=30` in the drop-in and re-enrol
   (a miss then takes longer to reject; speeding up the matcher is the
   companion change).
2. **Enrolment UI for Omarchy**: a shell plugin that fills a fingerprint
   glyph stage by stage and tells the user to shift the finger between
   presses. Omarchy has no enrolment UI today, only the terminal script.
3. **More impostor data** would make the threshold trustworthy: another
   person's fingers through `capture-pool.sh`, then
   `EVAL_LOO=1 elanpress-eval pool pool 0`.
4. **Built-in ELAN7001**: try the SIGFM press-mode patch linked above; it is
   the same die (`eFSA80SC`) in another ASUS model, and the same
   swipe-versus-press diagnosis.
5. **Upstream**: the driver to libfprint, ideally merged with Filip Spanne's
   `elanpress` fork for 04f3:0c6e that it derives from, with the raw-image
   template trade-off stated up front; the `ppmm` fix in
   `tools/0001-elan-0c3d-quirks.patch` stands on its own. [iafilatov/libfprint#53](https://github.com/iafilatov/libfprint/issues/53)
   reports this device with no data attached.

## Repo layout

```
FINDINGS.md        technical write-up of the 04f3:0c3d investigation
tools/elanpress/   the press-mode driver, matcher, eval tool, build/test scripts
tools/             the earlier swipe-driver patch, debug scripts, and logs
build/             libfprint source + build (gitignored)
pool/, hw-test/    captures and logs from hardware runs (gitignored, biometric)
```

Captured fingerprint images are gitignored. They are real biometric data, and
FINDINGS.md describes what they showed.
