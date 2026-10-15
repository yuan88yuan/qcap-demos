# SDI RX App-only decoder lock validation

## Scope

Target: SC6F0 SDI Dante RX, `xlnk2_arm64`, 2160p60 AVH input to 1080p60
SDI output. User explicitly approved this App-only synchronization change.
Do not change QCAP decoder surface ownership, SDK libraries, HDMI lock
behavior, TX runtime, firmware or physical clock configuration for this test.

GDB on the frozen old App showed input LWP947 holding `decoder_lock` while
blocked in `qcap2_video_decoder_push()`, and output LWP953 waiting on the
same mutex in `on_vdec_event_callback()`. Mutex owner was verified as947.
This is an observed wait chain; it does not prove the root cause of the
lower SDI display throughput or resolve the independent CSC drain problem.

## Change

SDI output callback uses a separate `decoder_output_lock` for decoder pop.
Initialization takes both input and output lifetime locks when publishing
decoder state. The output lock is released before initialization-failure
cleanup joins handlers. Shutdown joins handlers before taking locks and
deleting the decoder. HDMI callback still uses its original `decoder_lock`.
No buffer sizes, clock rates, packet mode or SDK ownership changed.

## Verification

- `python3 tools/tests/test-sc6f0-sdi-decoder-lock.py`: passes actual callback
  host contention test, SDI output lifecycle gate, stopped decoder, and
  unchanged HDMI contention behavior. Initialization/shutdown ordering has
  source assertions; this is not a vendor decoder or hardware stress test.
- `python3 tools/tests/test-sc6f0-rx-output.py`: passes.
- Focused Docker/CMake build: image `qcap-build:xlnk2_arm64-base`,
  `BUILD_SC6F0=ON`, target `sc6f0-dante-rx`: compile/link passes.

Candidate executable SHA-256:
`6929f7dea1b97d2e5c8e42cacafdc2d09459cb5d65429e2a019d683a5d715093`.
Staged RX path: `/home/petalinux/sdi-rx-lock-test-Bdv794/sc6f0-dante-rx`.
Original executable/runtime retained. Existing SDK/library directory:
`/home/petalinux/sdi-rtp-test-a0IHAy/lib`; libqcap SHA-256 remains
`cb6390567615263e5681db93e7ed93c03f6ab5b830c6226bbaf288b762887d0d`.

Temporary service `sdi-rx-avh-lockfix-test.service` uses fixed linux-decode,
1080p60 output, 3s stats, Restart=no, maximum15 minutes. Controller routing
is user-owned. Startup AVH flow, DRM and ALLEGRO2 initialize without new
bridge/vblank errors. Monitor smoothness, sound, sustained counters, shutdown
and repeated-start acceptance are pending. No firmware delivery or source
commit/push performed.

## First board result: failed sustained playback

User observed picture briefly, then frozen. Candidate is **not accepted**.
60s frozen-state sample: SDI IRQ delta2423, mixer/VCU IRQ deltas0. Before
freeze output counts reached roughly46-57fps, but AVH UDP queue rose to4096,
queue overflow518, RTP lost518, dep-drop21, recovery-skipped88. Stats then
blocked after dep-getter-done. DEP audio overrun also grew rapidly.

Authorized GDB diagnosis of candidate PID1277: output event thread LWP1297
is idle in epoll, no longer blocked on App decoder_lock. Input LWP1290
still blocks inside decoder push in vendor/QCAP condition waits; coordinator
stats and watchdog remain blocked behind upstream locks. Decoder feeder and
SCD threads idle. GDB detached; TracerPid0 verified. The split lock removes
the original callback lock wait but is not sufficient to prevent this new
freeze. Do not attribute this result solely to CSC, increase buffers blindly,
or change shared SDK ownership against the user's instruction.

Logs: /tmp/opencode/sdi-rx-app-lockfix-60s-stats.log,
/tmp/opencode/sdi-rx-lockfix-frozen-state.log,
/tmp/opencode/sdi-rx-lockfix-frozen-gdb.log.
