# libfprint merge request — elanpress: Add driver for the Elan 04f3:0c3d press sensor

Branch: `elanpress-0c3d` in `build/libfprint/` (one commit, also exported as
`0001-elanpress-Add-driver-for-the-Elan-04f3-0c3d-press-se.patch` here).

## To open it

libfprint lives on GitLab, so this needs your account:

```sh
# once: fork https://gitlab.freedesktop.org/libfprint/libfprint in the web UI
cd build/libfprint
git remote add fork git@gitlab.freedesktop.org:inauman/libfprint.git
git push -u fork elanpress-0c3d
# then "Create merge request" from the link GitLab prints
```

## Before it can merge: a umockdev recording

libfprint's CI runs every USB driver against a recorded session
(`tests/<driver>/`). The recording needs the device and root:

```sh
paru -S umockdev                                   # AUR
meson setup build-test build/libfprint -Dintrospection=true -Ddoc=false
ninja -C build-test
sudo build-test/tests/create-driver-test.py elanpress   # follow the prompts: enrol, verify
```

That writes `tests/elanpress/{device,custom.pcapng}` and expects a
`tests/elanpress/custom.py`; `tests/elanpress-custom.py` here is a start
(host-stored prints: no STORAGE features, VERIFY and IDENTIFY yes). Add
`'elanpress': {}` to `tests/meson.build` and commit alongside.

## Merge request description (paste)

**Device.** `04f3:0c3d` Elan, a 64×88 px (~500 dpi, ~3.2×4.4 mm) press
sensor sold as a "Windows Hello" USB dongle (TEC TE-FPA2 and others; 162
machines on linux-hardware.org, all "failed"). It is in the `elan` id table
today, and enrolment through that driver never verifies.

**Why the `elan` driver cannot work for it.** That driver is swipe-only:
it crops every frame to 50 rows (`ELAN_MAX_FRAME_HEIGHT`), films ~30
frames of a *resting* finger and stitches them into a strip that differs
on every press, then hands the result to NBIS. `bozorth3` returns a
constant zero when either print has fewer than
`MIN_COMPUTABLE_BOZORTH_MINUTIAE` (10) minutiae, and a patch this size
yields 1–5. So the failure is structural, not a threshold; it also affects
`0c6e` (Filip Spanne's fork) and, presumably, the other press sensors in
the table.

**What this driver does.** Same bulk protocol, treated as a press sensor:
full frames; no device-side calibration loop (it deadlocks on fw 0x0165 —
status stays 0x01), the background frame is checked against the device's
own calibration mean instead, which also detects a finger that has not
lifted; up to three distinct frames per touch; host matching by zero-mean
NCC of locally normalised ridge images over translation and ±20°,
coarse-to-fine, threaded. Templates are `FPI_PRINT_RAW`, the images.

**Measured.** 140-image pool from one pad (index 20 touches; middle, ring,
thumb 12 each). Threshold 0.76 sits above every impostor (0/91) and
accepts ~60 % of genuine touches with a 20-touch enrolment; a genuine
match returns in ~0.2 s, a rejection in ~1.9 s on 4 cores. Higher overlap
floors, narrower rotation, sidelobe- and block-consistency scores,
overlap-adaptive thresholds and mosaic templates were all measured and did
not move the impostor ceiling; coverage did. Verified end to end through
fprintd and PAM (sudo, polkit, lock screen).

**Trade-off to decide on.** The template is the enrolled imagery, ~170 KB
per finger, invertible into a picture of the finger; NBIS templates are
not. That is what the vendor's Windows driver stores too, and it is the
only kind of template a 3×4 mm patch supports. If that is unacceptable
upstream, say so and this stays out of tree.

**Not included.** `0c6e` (needs testing with these parameters; Filip's
fork has its own tuning); the umockdev test (recording in progress);
`uncrustify` was not available on the build host — happy to reformat.
