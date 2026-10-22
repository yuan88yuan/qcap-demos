# SDI RX transitional output resync checkpoint (2026-10-22)

Operator accepted the refactored App after repeated FS ON/OFF tests. The prior
test explicitly observed shifted picture followed by automatic recovery.
Refactored executable SHA-256:
`6c81dd1d48829bce4cb741b95ea45b5982884ddf6fb0164d5834df66186da6d1`.
Board rootfs RX-PL-2026.10.22.01-SDI, FPGA0x26102101/ABI3, video-only4K60.
This is a scoped engineering workaround checkpoint, not root-cause closure,
seamless switching, OFF NO SIGNAL resolution or endurance signoff.

The App has no FS ON/OFF event interface. Opt-in `SC6F0_SDI_AUTO_RESYNC=1`
monitors native SDI register0xB00C006C in the Ultra main loop. Sticky bit11 +
lag12 across five500ms samples/at least2s requests one guarded same-mode
ACTIVE0/1 cycle with unchanged framebuffers/rate/source.5s cooldown, max3
attempts per rolling60s; a DRM failure attempts original-state rollback after
a real commit error and disables further automatic attempts. The register
predicate is only a narrow history heuristic, not current-lock/picture proof.

Functions separate initialization, read-only MMIO load, policy/debounce,
polling, internal recovery callback, guarded DRM commits and cleanup. This is
explicitly marked TRANSITIONAL WORKAROUND for future replacement/removal.

Tests: `tools/tests/test-sc6f0-sdi-resync.py` covers policy and the actual App
DRM helper with mocked commits/failures; existing RX output/idempotence tests
pass. AArch64 target builds in qcap-build:xlnk2_arm64-base.

Evidence lives in the coordination workspace:
`evidence/probe-abi3-20261022/resync-app/refactor-accepted.log`,
`refactor-start.log`, `operator-toggle-results.log` and `operator-results.md`.
Test deployment is transient under /run, not a persistent full-image delivery.
Next delivery must explicitly enable the workaround and accept its newly
audited no-probe FPGA version; otherwise the compatibility guard fails closed.

No-probe delivery candidate0x26102201 is now explicitly allowed alongside the
accepted ABI3 FPGA0x26102101. Both retain the native SDI0xB00C006C register;
the workaround does not access the removed diagnostic0xB1020000 aperture.
Unknown IDs and non-SDIR identities still fail closed. Host tests cover both
allowed identities and rejected unknown/cross-product identities. This adds
compatibility for the new full-image candidate, not new hardware acceptance.
