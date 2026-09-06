# Elan 04f3:0c3d on Linux — findings

Investigation of a "Windows Hello" USB fingerprint dongle reporting
`04f3:0c3d ELAN:Fingerprint` (firmware `0x0165`, `bcdDevice 1.65`) under
libfprint's `elan` driver.

Intended for [iafilatov/libfprint#53](https://github.com/iafilatov/libfprint/issues/53),
which reports this device as non-functional with no diagnostic data attached.
Everything above the appendix can be pasted there as-is.

See [README.md](README.md) for the goal, the hardware inventory, and what else
was tried. The laptop's second, built-in sensor is covered in the
[appendix](#appendix-the-laptops-built-in-sensor-not-part-of-issue-53).

**Result:** enrollment now works. Verification does not, for a reason that
appears to be architectural rather than a bug — see [Unresolved](#unresolved).

## Environment

| | |
|---|---|
| Device | `04f3:0c3d` Elan Microelectronics, `ELAN:Fingerprint` |
| Firmware | `0x0165` (driver reads via `get_fw_ver_cmd`) |
| Frame geometry | 64 × 50 |
| libfprint | master (1.94.100) and packaged 1.94.10 — identical behaviour |
| fprintd | 1.94.5 |
| Kernel | 7.1.9-arch1 |

`0x0c3d` is already in `elan_id_table` with `driver_data = ELAN_ALL_DEV`, so the
driver binds and names the device. Being in the table is not the same as
working.

## Symptom

```
Enroll result: enroll-unknown-error
fprintd: Device reported an error during enroll: Calibration failed!
```

Intermittent in a way that was initially misleading: a **cold** sensor enrolls a
stage or two, a **warm** one fails immediately. Cause below.

## 1. Calibration deadlock

`elan_need_calibration()` compares a device-reported calibration mean against a
host-computed mean of a background frame:

| Sensor state | bg mean | calib mean | delta | vs `ELAN_CALIBRATION_MAX_DELTA` (500) |
|---|---|---|---|---|
| Cold / idle | 8327 | 8302 | **25** | under — calibration skipped, device works |
| After finger contact | 9909 | 8274 | **1635** | over — calibration runs, then deadlocks |

`CALIBRATE_GET_BACKGROUND` issues `get_image_cmd` with no wait for the finger to
lift, so the "background" reference is captured with the finger still present.
That inflates it by ~1600 and spuriously triggers calibration.

The loop then never terminates. `CALIBRATE_CHECK_STATUS` requires status `0x01`
followed by `0x03`; this device returns `0x01` indefinitely:

```
calibration status: 0x01     ×199 consecutive, ~10 seconds
```

Raising `ELAN_CALIBRATION_ATTEMPTS` from 10 to 200 was tried first and **did not
help** — the 199 polls above are from that build. It is not a timing problem.

`0x01` is documented in the driver's own comment as *"retry"*. The device is
asking to be retried; the driver keeps asking whether it is finished. Honouring
the retry by jumping back to `CALIBRATE_GET_BACKGROUND` re-reads a clean
background once the finger is off, the delta falls back under threshold, and
calibration completes.

Confirmed on hardware — deltas of 1440 / 1543 / 1168 recovering to 42 / 46 / 126
within 1–2 polls, where the previous code failed 100% of the time.

## 2. Capture protocol error

With calibration fixed, the next stage failed:

```
SSM CAPTURE_NUM_STATES failed in state 2 (CAPTURE_READ_DATA)
FP_DEVICE_ERROR_PROTO
```

`CAPTURE_READ_DATA` treats a pre-scan response of `0x00`/`0xaf` as
retryable — but only when `dev_type == ELAN_0C58`. Any other device gets a hard
protocol error. `0c3d` returns those same bytes and needs the same tolerance.

Extending that branch eliminated the error entirely: five clean enrollment
stages, zero protocol errors since.

## 3. Frame assembly — unusable horizontal offsets

Enrollment succeeded but verification always returned `verify-no-match`. Dumping
the assembled image with `examples/img-capture` showed why: sharp,
high-contrast ridge data assembled into a **staircase**, each frame displaced
tens of pixels laterally from its neighbour.

`fpi_do_movement_estimation()` produces unusable `delta_x` values for this
sensor. A finger crossing a 64 px sensor does not travel that far sideways
between consecutive frames. Zeroing `delta_x` while keeping `delta_y` produced a
straight, correctly stacked column.

Also tried and rejected: switching to `elan_process_frame_linear` on the theory
that `elan_process_frame_thirds` rebuilds its tone curve per frame and thereby
misleads the correlation. It made contrast slightly worse and did not change the
offsets.

## 4. `ppmm` is never set — affects all Elan devices

`fp_image_init()` is empty, so `FpImage.ppmm` defaults to **0**, and the `elan`
driver never assigns it. That zero is passed straight to NBIS:

```c
get_minutiae (..., image, self->width, self->height, 8, self->ppmm, lfsparms);
```

NBIS ridge thresholds are all pixels-per-millimetre relative. Compare
`secugen.c`, which sets `SECUGEN_PPMM 19.685` (500 DPI). This is not specific to
`0c3d` — it applies to every device the `elan` driver handles.

Setting it did not by itself fix matching here, but the current value is
plainly wrong.

## Unresolved: press sensor driven as a swipe sensor

`elan.c` hardcodes `dev_class->scan_type = FP_SCAN_TYPE_SWIPE` for every device,
with no per-device override.

This device is a **press/area sensor**. The vendor's own instructions are "just
touch the fingerprint sensor around 1-2 seconds". Its captures bear that out:
30 frames of the same patch of finger as it settles, not consecutive slices of a
moving one. With `delta_x` corrected the strip stacks straight, but the frames
are visibly *unrelated views* — different ridge orientations block to block.

That explains the matching failure completely. Enrollment succeeds because five
collages pass the frame-count check; verification fails because each collage is
assembled from a different random settling of the finger.

Submitting the single sharpest frame instead of assembling was tried:

```
Error capturing data: Minutiae detection failed, please retry
```

NBIS could not extract minutiae from a 64 × 50 frame (~3 mm square), with
correct `ppmm` set, nor from the same frame bilinearly upscaled 4× to 256 × 200
with `ppmm` scaled to match. This matches the original driver author's
expectation that square Elan sensors would be "less reliable because the
resulting image is even smaller", and that in touch mode "even enrolling is
impossible".

So the remaining gap looks like a genuine limit of this sensor against NBIS,
not a further quirk to patch.

## Patch

`tools/0001-elan-0c3d-quirks.patch` — against libfprint master, four changes:

1. `ELAN_0C3D` quirk flag; `0x0c3d` tagged with it instead of `ELAN_ALL_DEV`
2. Calibration: honour `0x01` by re-reading the background
3. Capture: extend the `0x00`/`0xaf` pre-scan retry to `0c3d`
4. `delta_x = 0` after movement estimation; set `ELAN_PPMM`

Verified safe against every `dev_type` comparison in `elan.c` — the new bit
matches no existing command flag, `ELAN_NOT_ROTATED` is bit 1, and the
`dev_type` switch has no `default`.

**Before:** enrollment impossible, `Calibration failed!` on every warm attempt.
**After:** enrollment completes reliably, five stages, no retries.
**Still:** `verify-no-match`, for the reason above.

## Reproducing

```
tools/elan-debug-probe.py          # drive the device directly, full driver debug
tools/capture-calibration-log.sh   # capture the calibration status bytes
tools/capture-image.sh             # dump the assembled image to a PGM
tools/install-patched-driver.sh    # install patched libfprint, scoped to fprintd
```

Logs from each stage are archived alongside them.

---

## Appendix: the laptop's built-in sensor (not part of issue #53)

The same machine has a second, unrelated fingerprint sensor — the square pad in
the corner of the ASUS touchpad. It is recorded here because it caused the first
few hours of confusion, not because it bears on `04f3:0c3d`.

| | |
|---|---|
| Chip | ELAN7001, **SPI** (not USB) |
| sysfs | `spi-ELAN7001:00` → `spidev`, `/dev/spidev2.0` |
| Driver | `elanspi` |
| fprintd name | ElanTech Embedded Fingerprint Sensor |
| Scan type | swipe, 8 enroll stages |

### It hid the real problem

`omarchy hw fingerprint` only scans USB sysfs, so it reported *no reader* while
this SPI sensor was present. Meanwhile fprintd enumerated **both** devices and
`fprintd-enroll` used the default — the built-in one. Early enrollment attempts
therefore appeared to do nothing while the user touched the USB dongle, because
the driver was talking to a different sensor entirely.

Separating them with `FP_DRIVERS_ALLOWLIST` (`elanspi` vs `elan`) in a fprintd
systemd drop-in was the step that made everything afterwards diagnosable.

### Why it was abandoned

libfprint's udev rule binds it to `spidev` correctly, so the plumbing is right,
but the sensor never initialises reliably:

```
<init/otp> timed out waiting for vcom detection        (repeatedly, ~5s each)
Device reported an error during enroll: Device disabled to prevent overheating.
Deactivating image device while it is not idle, this should not happen.
fpi_device_action_error: assertion 'priv->current_action != FPI_DEVICE_ACTION_NONE' failed
```

"Overheating" is libfprint's *temperature estimation model*, not real heat — the
driver spends so long retrying failed initialisations that its own safety model
shuts the device down. Restarting fprintd resets that model, which buys one more
attempt.

With a fresh daemon it did once complete all 8 enroll stages, but the resulting
template failed `fprintd-verify` — captures taken while the device was only
partially initialised. The assertion failure is libfprint reaching a state its
authors did not anticipate.

Conclusion: `elanspi` support for ELAN7001 is real in principle but does not work
on this machine. Excluded from fprintd via the driver allowlist. Worth retesting
after a future kernel or libfprint update — it is a one-word change back.
