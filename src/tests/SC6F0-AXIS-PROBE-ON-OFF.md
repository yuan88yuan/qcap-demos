# SC6F0 SDI RX five-point Fast Switching comparison

## Scope and safety

`sc6f0-axis-probe-session.py` is a bounded diagnostic wrapper around the existing
`sc6f0-axis-probe-read.py` and its native32 MMIO library. It does NOT change Fast
Switching, subscription, EDID, DRM/CRTC, pixel clock, audio, App services or reset
signals. Its only hardware writes are the existing reader's probe diagnostic
SNAPSHOT/CLEAR commands, serialized with that reader's `/run/lock` lock.
Do not run other probe snapshot tools concurrently. It is not entirely read-only:
probe statistics are cleared after each snapshot, NOT video hardware.

Supported exact pairs: SDI RX `0x26102005/SDIR` + compact ABI2.0 (legacy
upstream map), or candidate `0x26102101/SDIR` + compact ABI3.0 (downstream map).
The reader rejects unknown hardware before touching probe CSRs, and crossed
FPGA/ABI pairs before any snapshot/clear command. No Python buffer
packing is used for MMIO and there is no fallback if the native32 library is absent.
A subprocess timeout is not a hardware bus timeout and cannot guarantee recovery
from a CPU blocked on a nonresponding AXI slave.

ABI2 points: Dante output -> CDC output -> vproc input -> vproc output -> mixer
overlay INPUT. ABI3 points: Dante output -> mixer overlay INPUT -> mixer OUTPUT
-> output CSC OUTPUT -> packer OUTPUT/SDI VIDEO_IN. Packer tap observes all64
bits, two pixels/beat. Neither map observes physical serialized SDI output.
Counter health must not be interpreted as external picture/SDI acceptance.
ABI3 is a new source/build candidate, NOT yet programmed on the board.

## Deployment (no firmware rebuild)

For the currently installed ABI2 image, the wrapper alone can use its existing
reader. Before any ABI3 target test, deploy the UPDATED dual-ABI reader and
wrapper together, verify their hashes/FPGA identity, and retain the existing
AArch64 native32 library. An old ABI2 reader must reject the ABI3 FPGA rather
than mislabel banks. No updated reader has been deployed by the source build.
Use the guarded reader and adjacent native32 library via
`--reader-dir /opt/qcap-runtime/sdk/current/lib`. Do not replace the installed
library or deploy a host-architecture .so. If prerequisites are absent, stop.

## Operator-controlled sequence

Keep the same TX source/subscription and 3840x2160 input. Operator changes FS;
the wrapper's `--state` is only an operator label, not a setter or independent
verification. Log LV7300 signal/picture and whether TRS/Line Number are increasing
for EACH stage. Use a new output directory for each run.

1. FS ON, normal picture after the already-completed CRTC recovery: capture baseline.
2. After capture finishes, operator switches OFF; keep it OFF throughout capture,
   even if the analyzer reports NO SIGNAL.
3. After OFF capture finishes, operator switches ON; capture whether shifted or
   normal WITHOUT another CRTC recovery first.
4. Discuss results before any output reset/recovery. No automatic recovery here.

Run as root on RX (default 40 fresh windows, ~10 seconds plus read overhead):

```sh
sudo python3 /home/petalinux/sc6f0-axis-probe-session.py \
  --reader-dir /opt/qcap-runtime/sdk/current/lib \
  --state on --output /home/petalinux/probe-on-baseline-01

sudo python3 /home/petalinux/sc6f0-axis-probe-session.py \
  --reader-dir /opt/qcap-runtime/sdk/current/lib \
  --state off --output /home/petalinux/probe-off-01

sudo python3 /home/petalinux/sc6f0-axis-probe-session.py \
  --reader-dir /opt/qcap-runtime/sdk/current/lib \
  --state on --output /home/petalinux/probe-on-return-01
```

Do NOT run the three commands in a batch: each is separated by operator state
confirmation. For an intentional 1080p test add `--width 1920 --height 1080`;
otherwise the expected geometry is 1920 beats/line, 2160 lines/frame,
4147200 beats/frame (two pixels per beat).

## Artifacts and interpretation

- `metadata.json`: operator label, UTC start, release/boot ID, exact reader command.
- `raw.jsonl`: unmodified reader samples, including the initial old window.
- `reader.stderr`: reader errors.
- `summary.json`: exact FPGA/ABI point map, fresh-window integrity, totals,
  geometry, per-window ranges, SOF counts, last SOF period in reference ticks,
  backpressure/transfer fractions and sampled VALID/READY/reset levels. ABI3
  also reconstructs adjacent pipeline last-SOF phase, signed modulo the upstream
  period, from existing reference timestamp/age fields. This needs no new RTL
  subtractor and is NOT a content/frame-match or absolute-latency measurement.

First capture is always excluded: it closes an unbounded pre-test interval and
may contain saturated counters. All subsequent windows must have complete data,
all five points, consecutive sequences and no timeout/overflow/pending period
sample. Missing/invalid measurements never count as a healthy result.

Exit0 = no evaluated counter anomaly; exit2 = attention/incomplete or observed
counter anomaly; other nonzero = setup/analysis failure. None means media PASS.
The underlying reader may exit2 just for the discarded initial window; the
wrapper evaluates fresh windows separately. Nonzero backpressure/stall cycles
are normal AXIS flow control; `stall_violation_cycles` is a separate metric.
SOF period values are reported without requiring integer60Hz: fractional cadence
must be compared rather than automatically called an error. ABI2/3 has no true
within-window minima/maxima or maximum-stall/no-transfer run metrics. Summary
ranges describe observations ACROSS snapshots only, not hidden measurements.

Offline reanalysis (no root, MMIO import or target access):

```sh
python3 src/tests/sc6f0-axis-probe-session.py --analyze /path/to/raw.jsonl
python3 tools/tests/test-sc6f0-axis-probe-session.py
```

A healthy OFF result says only that the selected AXIS checks did not expose an
anomaly; it does not exclude faults after the final tap or brief unmeasured gaps.
