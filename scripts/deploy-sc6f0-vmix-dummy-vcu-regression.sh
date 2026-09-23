#!/bin/sh
# Copy the focused DRM binary and target-side regression script to an SC6F0 RX.
# Usage: ./scripts/deploy-sc6f0-vmix-dummy-vcu-regression.sh petalinux@board-host

set -eu

if [ "$#" -ne 1 ]; then
	echo "Usage: $0 user@sc6f0-rx-host" >&2
	exit 2
fi

target=$1
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary="$root/_objs/xlnk2_arm64/bin/sc6f0-vmix-drm-test"
script="$root/src/tests/sc6f0-vmix-dummy-vcu-regression.sh"

if [ ! -x "$binary" ]; then
	echo "Missing binary: $binary" >&2
	echo "Build it with: ./build.sh xlnk2_arm64 sc6f0-vmix-drm-test" >&2
	exit 2
fi

scp "$binary" "$script" "$target:/tmp/"
ssh "$target" 'chmod 755 /tmp/sc6f0-vmix-drm-test /tmp/sc6f0-vmix-dummy-vcu-regression.sh && /tmp/sc6f0-vmix-dummy-vcu-regression.sh'
