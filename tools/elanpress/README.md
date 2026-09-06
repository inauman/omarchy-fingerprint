# elanpress — press-mode libfprint driver for the Elan 04f3:0c3d pad

A libfprint driver that treats the TEC / Elan `04f3:0c3d` dongle as what it
is: a **press sensor** with a 64 × 88 px (≈3.2 × 4.4 mm, ≈500 dpi) window.
It replaces the two things that made the stock `elan` driver fail:

| stock `elan` driver | this driver |
|---|---|
| swipe mode: stitches ~30 frames of a resting finger into a collage that differs on every press | press mode: each frame is a complete image |
| crops every frame to 50 rows (`ELAN_MAX_FRAME_HEIGHT`) — 43 % of the sensor thrown away | uses the full 64 × 88 frame |
| NBIS bozorth3 matcher, which returns a hard 0 for any print with < 10 minutiae (a 3 × 4 mm patch has 1–5) | correlation matcher: normalised cross-correlation of locally-normalised ridge images, searched over translation and ±20° rotation |
| device-side calibration loop that deadlocks on this firmware | no calibration commands; the background frame is validated against the device's own calibration mean instead |
| one image per swipe | keeps up to 3 distinct frames per touch (the finger settles and rolls during a press), 10 touches per enrolment |

Lineage: the USB state machine is from Filip Spanne's `elanpress` driver for
the sibling `04f3:0c6e` sensor; the matcher design draws on dragosol/fpmatch,
which was built against a 144 × 64 variant of this same product ID and
measured that on sensors this small the matcher is not the limit — **capture
overlap is** (see "How to enrol" below).

## Files

| file | purpose |
|---|---|
| `elanpress.c/.h` | the driver (FpDevice, host-side templates as `FPI_PRINT_RAW`) |
| `elanpress-match.c/.h` | preprocessing, quality gates, matcher; no libfprint dependency |
| `eval.c`, `Makefile` | offline tool sharing the matcher code: pair scores, pool statistics, self-test |
| `build.sh` | clone libfprint master into `build/`, add the driver, build; `install` copies it to `/opt/libfprint-0c3d` and points fprintd at it |
| `hw-test.sh` | first hardware run: enrol + verify through libfprint's example programs, frames dumped |
| `capture-pool.sh` | collect labelled captures for tuning |

## Build, test, install

```sh
tools/elanpress/build.sh                    # as your user, ~2 min
sudo tools/elanpress/hw-test.sh             # enrol right index, then verify in a loop
```

`hw-test.sh` stops fprintd, runs against the device directly, and writes
`hw-test/enroll.log`, `hw-test/verify.log` and every frame under
`hw-test/dump/`. What to look for in the output:

- `sensor dimensions, WxH: 64x88` and `FW ver 0x0165` — the device opened.
- `background mean ~8300 ... delta < 500` — clean background accepted.
- per frame: `mean` (expect ~1000–2000 with a finger on), `contrast`,
  `coverage`; per touch: `kept N images`.
- verify: `verify: best 0.xxx (threshold 0.55)` followed by `MATCH` / `NO MATCH`.

When enrol and verify work there:

```sh
sudo tools/elanpress/build.sh install       # /opt/libfprint-0c3d + fprintd drop-in
sudo fprintd-delete $USER                   # old swipe-mode templates are unreadable
fprintd-enroll                              # or: omarchy setup security fingerprint
fprintd-verify
journalctl -u fprintd -f                    # scores for every attempt
```

Uninstall: remove `/opt/libfprint-0c3d` and
`/etc/systemd/system/fprintd.service.d/allowlist.conf`, `systemctl daemon-reload`.

## How to enrol (this matters more than the matcher)

Each press images ~3 × 4 mm of finger. Verification succeeds only if the
probe overlaps one of the enrolled images by at least 40 % of the frame. So
during enrolment **move the finger between presses**: centre, then slightly
up, down, left, right, and the four diagonals, then repeat. Pressing the
same spot ten times enrols one patch ten times. Windows Hello's Elan driver
does exactly this with its "move your finger" prompts; libfprint has no
channel for that guidance, so it has to be a habit.

Hold each press flat for about a second (≈12 frames at 76 ms). Rolling the
finger slightly during the hold is fine and even useful: distinct frames from
one touch are all kept.

## Measured on this pad (2026-09-06)

Pool: one subject, right index 20 touches (49 images), right middle / ring /
thumb 12 touches each (91 impostor images), captured with
`capture-pool.sh`, deliberately varied placement for the index.

Leave-one-touch-out for the index (template = the other 19 touches, so a
20-touch enrolment), score = best correlation over the template:

| | per image | per touch (best of its frames) |
|---|---|---|
| highest impostor score | 0.757 | 0.757 |
| genuine median | 0.740 | |
| genuine accepted at zero false accepts (threshold 0.76) | 47 % (23/49) | **60 % (12/20)** |
| with only 10 enrolment touches | 24 % | |

So: **threshold 0.76**, 20 enrolment touches, three probe frames per
verification. With PAM's three tries per login that is roughly a 94 %
chance of getting in, and every one of 91 impostor images from three other
fingers is rejected. Natural presses land more consistently than the
varied-placement probes in the pool, and the first hardware run (10-touch
enrolment, natural presses) matched the index every time.

What did *not* help, all measured on the same pool: raising the overlap
floor to 50 % or 60 % (impostor ceiling stays at 0.72, genuine drops
faster), narrowing the rotation search to ±12°, peak-minus-sidelobe and
peak/sidelobe scores, block-consistency scores (min or mean correlation
over a 3 × 3 grid at the peak, alone or blended), an overlap-dependent
threshold, and mosaicking the enrolled images (only a third of them
overlap each other enough to register). The impostor ceiling comes from
chance alignment of parallel ridges over the smallest allowed overlap, and
none of those change it. What does move the genuine rate is coverage:
more enrolled touches and more frames per touch.

`FAR = 0` here means 0 of 91 images from one person's other fingers. It is
not a population statistic; expect the 92nd impostor image to eventually
cross 0.76, and treat this as a convenience login, not a vault.

## Tuning

To re-measure or retune:

```sh
sudo tools/elanpress/capture-pool.sh right-index 20
sudo tools/elanpress/capture-pool.sh right-thumb 20
sudo tools/elanpress/capture-pool.sh left-index 20        # impostors
make -C tools/elanpress
tools/elanpress/elanpress-eval pool pool 10
```

The last command prints genuine and impostor score distributions, the
acceptance rate at FAR = 0, and what the current threshold would do. All
parameters are runtime-overridable so they can be swept without rebuilding
(the driver reads the same variables; put them in the fprintd drop-in):

| variable | default | meaning |
|---|---|---|
| `FP_ELANPRESS_THRESHOLD` | 0.76 | accept at or above this score |
| `FP_ELANPRESS_ROT_RANGE` / `ROT_STEP` | 20 / 4 | rotation search, degrees |
| `FP_ELANPRESS_MIN_OVERLAP` | 0.40 | fraction of the frame that must overlap |
| `FP_ELANPRESS_NORM_WIN` | 11 | local-normalisation window, px |
| `FP_ELANPRESS_MIN_MEAN` / `MIN_CONTRAST` / `MIN_COVERAGE` | 150 / 40 / 0.35 | frame quality gates, raw units |
| `FP_ELANPRESS_ENROLL_STAGES` | 20 | touches per enrolment (1–40) |
| `FP_ELANPRESS_SCORE_MODE` / `BLOCK_WEIGHT` | 0 / 0 | experimental scores, measured worse; keep at 0 |
| `FP_ELANPRESS_DUMP_DIR` | unset | write every frame as PGM here |

Extra protocols, selected by environment variables on `pool`:
`EVAL_SKIP_PAIRWISE=1` skips the dense pairwise pass, `EVAL_LOO=1` runs
leave-one-touch-out (`EVAL_LOO_LABEL=right-index`), `EVAL_VARIANTS=out.csv`
writes every score variant per probe, `EVAL_CSV=out.csv` writes the
enrolled-protocol details, `EVAL_MOSAIC=1` tries mosaic templates.

`elanpress-eval synth IMG.pgm` runs the self-test: shifted and rotated copies
of an image must match, its mirror must not. `elanpress-eval pair A B`
scores two 8-bit images; `reprocess BG RAW OUT` rebuilds an 8-bit image from
dumped 16-bit frames with the current preprocessing.

## Privacy

A template is the enrolled images themselves: ~30 × 5.6 KB ≈ 170 KB of
fingerprint imagery per finger, stored by fprintd under `/var/lib/fprint`
readable by root only. A minutiae template cannot be inverted into an image;
this one can. That is the trade every correlation matcher makes, and it is
the same trade the Windows driver makes for this sensor.

## Known limits

- Roughly 60 % of touches are accepted; expect a second press sometimes.
- A cross-finger impostor from the same person is the strongest test
  available; two people's fingers would be better.
- Matching runs in threads across all CPUs; a rejection against a 20-touch
  template (≈45 images × 3 probes) is well under a second on 8 cores.
- Templates enrolled with fewer touches or a different frame size are
  rejected as unusable and must be re-enrolled.
