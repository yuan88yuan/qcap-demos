# SC6F0 RX RTSP reconnect verification

`sc6f0-dante-rx --rtsp-url` enables automatic reconnect with a 1000 ms retry
delay and unlimited attempts. Connect failures, handshake failures
(DESCRIBE/SETUP/PLAY), and detected streaming disconnects retry the same URL
without restarting the application. The zstreamer source default remains off
for other applications.

## Build

Rebuild the changed SDK before the RX application. No QCAP or firmware rebuild
is required for these source changes.

```bash
cd ~/docker/zstreamer
./scripts/build.sh xlnk2_arm64
cd ~/docker/qcap-demos
./scripts/docker-run.sh qcap-build:xlnk2_arm64-base \
  env BUILD_SYSTEM=cmake CMAKE_ARGS_EXTRA="-DBUILD_SC6F0=ON" \
  ./build.sh xlnk2_arm64 sc6f0-dante-rx
```

## Hardware test (performed by the operator)

With no other RX application using the decoder/DRM output, run on the RX board:

```sh
sudo sh /mnt/dev/zzlee/docker/qcap-demos/src/tests/sc6f0-rx-rtsp-test.sh \
  rtsp://SERVER:PORT/PATH
```

The script uses the NFS binary and libraries, on eth1 in `dante_avh`.

1. With the server running, confirm HDMI video/audio and increasing
   `RTSP playback` video AU, decoded frame, and audio frame counts.
2. Stop the server for at least 10 seconds. Expect
   `session ended, reconnecting attempt ...` and connect failure/retry logs.
   Keep the RX process running.
3. Restart the server at the same URL. Expect `streaming started`, increasing
   decoded/audio frame counts, and restored HDMI video/audio. Check the PID
   stays unchanged with `pidof sc6f0-dante-rx`.
4. Repeat at least three times, including a longer server outage.
5. Start the RX application while the server is stopped, then start the server.
   Verify it recovers from initial connect failures too.
6. If possible, interrupt a handshake or temporarily return an RTSP error,
   then restore the server. Verify retries continue through handshake failures.
7. Press Ctrl+C both during playback and during retries; verify clean exit.

Record logs, the RX release ID, and the rebuilt binary/SDK versions alongside
the result. An unchanged PID or a successful connection alone is not proof
that decoding and HDMI playback recovered.

## Limitations

The retry delay is in addition to connection/handshake timeouts. A silent
network blackhole that does not close/error the socket is not covered by a
media-inactivity watchdog here; recovery may depend on TCP detecting the loss.
Changing stream codec during server restart is a separate test.
The operator reported that hardware reconnect testing passed on 2026-10-01
using the rebuilt xlnk2_arm64 SDK and RX application. Detailed test logs and
the individual scenarios exercised were not supplied; the coding agent did
not independently verify hardware recovery.

## RTSP EDID / VProcSS scaling

RTSP uses the same output setup as AVH: source dimensions and frame rate come
from h26xparse SPS/PPS caps, and HDMI timing is selected from the connected
display's EDID modes. An exact supported mode is preferred; otherwise the
highest-resolution, highest-refresh progressive mode within 3840x2160,
594 MHz and 60 Hz is selected, following the existing AVH policy.

The decoded framebuffer supplies the DRM plane source dimensions. The plane
destination rectangle configures VProcSS scaling, preserving aspect ratio
with centered letterboxing/pillarboxing and even-sized NV12 dimensions. The
black primary plane fills the unused output area. No software scaling is used.

For a non-exact EDID match the application reports, for example:

```text
WARN[sc6f0-dante-rx]: [RTSP] input 3840x2160@60.00 not in EDID; scaling to 1920x1080 (1920x1080@60.00)
```

`RTSP VProcSS scaler` additionally reports source dimensions, scaled destination
dimensions, destination position, and HDMI output timing.

When RTSP caps change after startup, decoder output callbacks are stopped,
the old decoder and display are released, and new caps are applied. The next
IDR rebuilds the shared EDID/VProcSS output and decoder. Unchanged caps during
a reconnect do not force a rebuild. This is limited to H.264 playback; HDMI
hotplug without a source caps change does not trigger an automatic modeset.

Operator verification should include 3840x2160 input on a 1920x1080-only
monitor, a matching input/output mode, non-16:9 input (aspect preservation),
and a server restart with changed H.264 resolution. Confirm the scaler log,
HDMI picture, and increasing decoded frame count for each case.

The operator reported that testing of the EDID/VProcSS changes passed on
2026-10-01 using the rebuilt xlnk2_arm64 RX application (SHA-256:
`39c2f46813441c05c20b32712ef5dc9b2b7fd5bb4ef0a637a4425782e092c091`).
Detailed logs and individual scenario coverage were not supplied; this report
does not establish that every scenario above was exercised.
