# SC6F0 SDI RX diagnostic checkpoint — 2026-10-21

Target: xlnk2_arm64 / FPGA0x26102005 SDIR / passive probe ABI2.0.
Running firmware checkpoint: RX-PL-2026.10.21.02-SDI. App/SDK runtime
20261021T015624Z is preserved across kernel/rootfs updates, not rebuilt by them.
The committed App source includes the single configured RGB8 software timing,
SDI video-only option, and unchanged-format idempotence used by that runtime.
Software EDID/DAU60Hz metadata does NOT prove physical integer source cadence.

## Native-width MMIO — mandatory

`src/tests/sc6f0-mmio32.c` supplies helper ABI1 with explicit volatile32-bit
loads/stores and barriers. AArch64 disassembly was checked for one `ldr w` /
`str w` operation. Do not replace it with Python buffer serialization:
CPython struct.pack_into can zero the destination before packing, making an
extra invalid command write. Prior board unresponsiveness followed that writer;
without UART, a specific SError/panic cause was not established.

Standalone optional CMake target `sc6f0-mmio32` is enabled by BUILD_SC6F0 and
EXCLUDE_FROM_ALL. Build only when requested, in qcap-build:xlnk2_arm64-base,
using the normal sibling-prebuilt-SDK build configuration. It does not rebuild
QCAP/zstreamer. Install libsc6f0-mmio32.so adjacent to the Python probe reader.
Missing library/unknown helper ABI fails closed; there is no struct fallback.

## Tools (not services; never auto-run)

- `sc6f0-axis-probe-read.py`: default reads the published bank. Explicit
  `--snapshot --clear --count 24 --interval .25` requests bounded captures and
  clears ONLY diagnostic counters, not video hardware. First old window often
  saturates; discard it and require complete valid fresh windows. Exit2 can
  indicate incomplete measurement, not necessarily failed subsequent captures.
- `sc6f0-axis-probe-rate-check.py`: read-only ENV/reference clock ratios; no
  snapshot or clear command. Host monotonic frequency is not calibrated.
- `sc6f0-sdi-control-read.py`: read-only4K60 SDR control-state contract check:
  CSC/start, four ST352 payloads, interrupt masks, enables and raw Video-Out
  sticky transition history. Correct controls are NOT media acceptance or proof
  of current lock. This tool imports the adjacent probe reader/native helper.
- `sc6f0-sdi-rate-modeset.py`: state-changing named-board diagnostic, explicit
  integer/fractional or DUMMY/DANTE comparison, with same-mode `--reinitialize`
  using CRTC ACTIVE. Preserves existing framebuffer geometry/IDs and uses DRM
  TEST_ONLY before real commits. Changes can invalidate App cached state; heed
  its warnings. Use only for a planned test, not from an automatic restart loop.
- `sc6f0-sdi-local-modeset.py`: historical local-bars/ANC diagnostic, explicitly
  guarded to older FPGA0x26101501. It intentionally rejects candidate05. It
  changes video/audio/overlay state; do not use as a generic recovery tool.

## What the current five points can establish

P0 dante_out -> P1 cdc_out -> P2 vproc_in -> P3 vproc_out ->
P4 mixer_overlay_in. P4 is BEFORE mixer. No mixer-out/output-CSC/packer-out
probe exists in this ABI. Counters check handshake stability, accepted geometry,
SOF/EOL and cadence, not image content or visible screen position. During the
operator's shifted picture all five points passed these checks. Consequently
one cannot infer that displacement already happened upstream of the mixer.

Operator confirmed steady ON normal, but OFF is NO SIGNAL and OFF->ON can leave
persistent shift/TRS accumulation. Correct CSC/ST352/IRQ controls coexist with
that fault. One same-mode ACTIVE reinitialize restored correct external picture
and stopped further TRS/Line Number errors. An App-only restart for this exact
fault was NOT tested; no automatic workaround has been implemented. OFF cadence,
automatic recovery and coldboot/endurance remain open. Firmware-side source and
bounded evidence are in sc6f0-dante/wiki/SDI-RX-FAST-SWITCH-TRANSITION-20261021.md.

## Host checks (no hardware)

Run Python scripts under tools/tests:
- test-sc6f0-axis-probe-read.py:21 PASS (also builds a host native helper).
- test-sc6f0-sdi-rate-diagnostics.py:11 PASS.
- test-sc6f0-sdi-control-state.py:9 PASS.
- test-sc6f0-rx-output.py: PASS compiled output/capability policy fixture.
- test-sc6f0-rx-idempotent.py: PASS actual unchanged-format guard fixture.

These are logic/compile evidence, not cross-compiled or external media signoff.
No new lengthy SDK/App build or board modification was performed for Git commit.
