# Independent SC6F0 VCU RTSP diagnostics

Purpose: test encoder/decoder without AVH subscriptions or DVR trial lifetime
gating the App. RTSP is a transport/isolation control, **not a decoder fix**.
Normal Dante modes remain the defaults. No licensed service is patched/restarted.

## Entry points

TX (same SDI source and ALLEGRO2 encoder):

```sh
sc6f0-dante-tx --rtsp-only --tx-sndbuf 4194304
```

Creates RTSP H.264/AAC output `rtsp://<tx-ip>:8554/session0`, but no Dante DVR
session, video coordinator, DEP sink or mirror RTP path. It rejects combinations
with QCAP Dante/direct routes. `--tx-sndbuf` and optional `--dump-stream` remain
usable. No raw-stream dump is enabled by default (storage/output load changes
timing). TX's existing `[DANTE TX STATS]` label is retained for parser compatibility;
`flows=0` is expected here, and its audio counters refer to DEP, not AAC RTSP.

RX (same ALLEGRO2 decoder; pop/release frames, no DRM display):

```sh
sc6f0-dante-rx --rtsp-url rtsp://<tx-ip>:8554/session0 --no-display
```

Omit `--no-display` for a subsequent decoder+display comparison, after verifying
clean output state. Audio remains the existing AAC -> PCM route; this is video
decode-only, not an audio-disabled test. RTSP does not accept DEP batching flags.
Progress is flushed every stats interval for reliable journald observations.

## Launch independently of production prerequisites

Do not use the production `qcap-app-run.sh`, which requires the Dante DVR and
namespace. Stop the existing App, verify no other codec owners, then launch
named transient units with Restart=no and the prebuilt SDK/zstreamer library
paths. Do not run two capture/decoder owners concurrently. Do not rebuild or
replace NFS-mapped binaries/libraries while either test is running.

Recommended diagnostic environment (read at process/library initialization):

```sh
QCAP_LOG_LEVEL=3 QCAP_FORCE_SHOW_FPS=1 QCAP_VSRC_FPS=1 QCAP_VENC_FPS=1 QCAP_VDEC_FPS=1
```

Use `systemd-run --unit=<reserved-name> --property=Restart=no ...` with no
qcap-start/DVR dependency. Ensure the NFS mount is visible in PID1's mount
namespace, not only the SSH shell's private namespace. Capture artifact hashes,
boot IDs, PIDs, unit ExecStart/dependencies and actual process network namespaces.

On the tested TX, the only Ethernet NIC is in `dante_avh` and PID1's network
namespace has only loopback. A transient unit can pin that existing networking
with `--property=NetworkNamespacePath=/run/netns/dante_avh`; this does not create
AVH flows or consume DVR data. Do not restart/delete the namespace or move the
NIC during the test. RX can use its host eth0 network. Verify network behavior
across actual trial expiry instead of claiming independence solely from flags.

## Measurements / failure policy

- Confirm TX capture, encoder input/output and pushed-frame counters separately.
  App TX FPS is incremented after synchronous RTSP delivery, so it includes
  consumer backpressure; it is not isolated hardware encoder throughput.
- RX decoded frames must advance; a live service or repeating display is not
  enough. Confirm `/proc/<pid>/fd` has no `/dev/dri/*` in decode-only mode.
- Record TCP queues, timeout diagnostics, codec IRQ deltas and actual DVR/trial
  state. Record zero-frame connection/setup failures as inconclusive.
- If stalled, preserve both sides and collect guarded GDB backtraces, then
  detach/verify TracerPid=0. Do not automatically restart/reboot or reset MMIO.
- A previous blocked decoder may require same-version codec module reload
  after all owners exit; verify descriptor ownership first. Do not unload DRM
  or reset neighboring output blocks as part of this codec test.

## 2026-10-19 initial deployment

AArch64 builds of both targets passed. TX `--rtsp-only` and RX RTSP
`--no-display` were started in separate units, TX PID15634 and RX PID1359.
RX began decoding 3840x2160@59.94 with no DRM fd. Original frozen AVH process
evidence was preserved before stopping it; codec modules were reloaded only
after no allegro descriptors remained open. No FPGA/rootfs/SDK rebuild occurred.

The initial unit launch included VSRC/VDEC but omitted QCAP_VENC_FPS; enable it
in the next run for split encoder input/output timing, rather than silently
restarting this baseline. A 75-minute observation was scheduled to detect
stalls/rate reduction and, if playback survives, cross the trial-expiry boundary.
This records a started test, **not a completed pass**. Results belong in sibling
`sc6f0-dante/wiki/VCU-CODEC-INVESTIGATION.md`.

The initial RTSP decode-only test subsequently stalled at 238 decoded frames;
GDB confirmed the same libqcap decoder-push wait without AVH or DRM. Closing the
failed RX connection restored TX to ~60 fps (backpressure confirmed). After
preserving evidence, RX was stopped and TX restarted with QCAP_VENC_FPS=1 for
a separate fast-host-RTSP-drain encoder observation. That second result remains
pending. Do not confuse it with a successful combined encoder/decoder run.

## First encoder-only results (preliminary)

With RX stopped and a workstation `ffmpeg` stream-copy consumer draining at
exactly 1.00x realtime (Send-Q 0, no backlog), TX was steady at 59.92 fps until a
single `ZzVideoEncoder2_allegro2::ProcessFrame(1919)` metadata-map miss, after
which fps stepped permanently to ~53.6. One event, sustained ~10% loss. See D3/D4
in `sc6f0-dante/wiki/VCU-CODEC-INVESTIGATION.md` for numbers, mechanism and
caveats. Note: keying rate/stall detection on `ffmpeg` `frame=` is wrong for
`-c:v copy`; use `out_time_us`. The interactive Dante AV-X trial expired mid-run
and the independent TX unit kept streaming unchanged (D4).
