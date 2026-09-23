#!/bin/sh
# Run this on SC6F0 RX with the matching sc6f0-vmix-drm-test binary present.
# It exercises dynamic DUMMY <-> VCU (DDR/NV12) selection and primary-plane
# disable/enable transitions without disabling the CRTC.

set -eu

TEST_BIN=${TEST_BIN:-/tmp/sc6f0-vmix-drm-test}
SOURCE_CYCLES=${SOURCE_CYCLES:-20}
SOURCE_HOLD_SECONDS=${SOURCE_HOLD_SECONDS:-0}
PRIMARY_CYCLES=${PRIMARY_CYCLES:-20}
FRAMES=${FRAMES:-120}
VISUAL_SECONDS=${VISUAL_SECONDS:-15}
HOLD_SECONDS=${HOLD_SECONDS:-15}

if [ "$(id -u)" -ne 0 ]; then
	exec sudo "$0" "$@"
fi

if [ ! -x "$TEST_BIN" ]; then
	echo "Test binary is not executable: $TEST_BIN" >&2
	echo "Copy sc6f0-vmix-drm-test to /tmp, or set TEST_BIN=/path/to/binary." >&2
	exit 2
fi

if [ ! -c /dev/dri/card0 ]; then
	echo "DRM card0 is unavailable." >&2
	exit 2
fi

echo '=== SC6F0 release ==='
cat /etc/sc6f0-release-id 2>/dev/null || true
if [ "$VISUAL_SECONDS" -gt 0 ]; then
	echo '=== HDMI visual preflight ==='
	echo "Showing primary-plane TPG color bars for $VISUAL_SECONDS seconds."
	"$TEST_BIN" --seconds "$VISUAL_SECONDS"
fi
echo '=== Dynamic overlay regression ==='
echo "DUMMY <-> VCU cycles: $SOURCE_CYCLES; each source hold: $SOURCE_HOLD_SECONDS s; primary cycles: $PRIMARY_CYCLES; NV12 flips: $FRAMES; active HDMI hold: $HOLD_SECONDS s"

"$TEST_BIN" --layer2 \
	--source-cycles "$SOURCE_CYCLES" \
	--source-hold-seconds "$SOURCE_HOLD_SECONDS" \
	--primary-cycles "$PRIMARY_CYCLES" \
	--hold-seconds "$HOLD_SECONDS" \
	--frames "$FRAMES"

echo '=== Relevant kernel errors ==='
if dmesg | grep -Ei \
	'timed out|drm.*timeout|Csc error|XVprocSs_CfgInitialize.*failed|failed to configure VProcSS|vproc.*error|v_proc.*error|axis.*error|drm.*error'; then
	echo 'FAIL: relevant kernel error found.' >&2
	exit 1
fi

echo 'PASS: dummy <-> VCU and primary-plane transitions completed without relevant kernel errors.'
