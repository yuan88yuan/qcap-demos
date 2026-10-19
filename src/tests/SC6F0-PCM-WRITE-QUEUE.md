# RX PCM delivery investigation (2026-10-19)

The original receiver discarded 960 frames after every nonblocking write,
including EAGAIN, XRUN and short-write results. The shared queue helper now
removes only accepted frames and retries unaccepted data on a later callback.
It never replays accepted frames, sleeps or spins on EAGAIN. Short-write tails
can finish below 960 frames. Recovery is limited to one prepare per error/callback
and at most 16 write attempts per drain. At 7680 queued frames, explicitly
counted overflow drops the oldest pending data to bound memory/latency.

EAGAIN/short-write counts are observations, not playback errors when pending
data is retained. Inspect `[RX PCM]` accepted frames, xruns, recovery errors and
queue-dropped counters. The existing `audio playback errors` counts hard write,
recovery and overflow errors, not ordinary backpressure.

## Controlled native DEP comparison

Both runs use the same candidate App, SDK, zstreamer, source boot and SDI/ALSA
parameters. First run normal `--media-path linux-decode --mode 3840x2160@60`.
Then add `--dep-audio-batched` to request zstreamer `queue-time-ms=100` and
`block-time-ms=20`. The library resolves these from the mapped DEP format;
the App no longer hardcodes producer period counts or output frame counts.
At 48kHz this gives a 4800-frame queue and 960-frame reads, replacing the
legacy library's 8-period / 2.67ms queue and 16-frame reads on the tested source.
The experiment changes both queue headroom and batching, not one isolated cause.
It does not change ALSA's 160ms buffer or HDMI-specific output settings.

The option is deliberately not the default and is rejected with RTSP, QCAP,
Ultra, direct RTP, non-48kHz and measurement-only modes. No library rebuild is
needed for the original frame/period A/B. The time-based API migration does
require an updated zstreamer library; setter failures are fatal rather than a
silent fallback. `[RX PCM]` reports resolved dep-block/dep-capacity. API details:
`zstreamer/wiki/dante-dep-audio-batching.md` in the sibling repository.

Compare steady-state deltas after video and audio actually start. Record board
boot IDs, source fps, App hash, errors, queue loss and analyzer counts per run.
A cold TX boot restored ~60fps after an earlier ~23fps run, so comparing across
those boots cannot establish that the App improved video performance.

RTSP is useful as an output-path control, but replaces native DEP PCM with AAC
decode and different delivery timing. HDMI playback was reportedly not audibly
faulty on a screen; no analyzer measurement was made, so that is not a sample-
continuity or protocol acceptance claim.

## Tests/build

Inside the builder, run `python3 src/tests/test-sc6f0-pcm-write-queue.py`.
This compiles the actual production helper with fake writes, covering frame
ordering/preservation, short/EAGAIN/XRUN/error paths, bounded retries and overflow.
The separate preload observer has its own mock transparency test.

Build only `sc6f0-dante-rx` with the standard xlnk2_arm64 CMake command. Preserve
the old executable outside generated outputs and stop the RX process before
relinking an NFS-backed executable. Cross-build success is not hardware signoff.
Do not enable the batched option globally or close the audio issue without
repeat physical/analyzer acceptance.

## Engineering checkpoint acceptance

On 2026-10-19, SDI RX App candidate SHA-256
`766e8b301949133039428db8879edb4d0538126a7259da5e334c5a80d66fa492`
was tested with `--dep-audio-batched` on rootfs `RX-PL-2026.10.16.01-SDI`.
Three additional App restarts, 90 seconds observation per run, had zero sampled
XRUN, DEP overrun, recovery errors, pending-queue drops, EAGAIN or short writes.
User confirmed normal picture, no TRS Position/Line Number errors and no Audio
Sample counter increase during playback. One or two analyzer increments when
opening/closing the App remain accepted acquisition transients, not zero-error
startup signoff. The unbatched control still reproduced continuous audio errors.

This accepts the steady-state engineering checkpoint, not global HDMI defaults,
production image integration, all cold/warm boots, endurance or A/V sync/latency.
The test service override is volatile; deployments must explicitly preserve the
batched option. Detailed A/B and restart evidence is recorded in sibling
`sc6f0-dante/wiki/SDI-RX-INTERMITTENT-OFFSET.md`.
