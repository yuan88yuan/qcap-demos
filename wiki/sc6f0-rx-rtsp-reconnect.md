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
Changing stream codec/resolution during server restart is a separate test.
The operator reported that hardware reconnect testing passed on 2026-10-01
using the rebuilt xlnk2_arm64 SDK and RX application. Detailed test logs and
the individual scenarios exercised were not supplied; the coding agent did
not independently verify hardware recovery.
