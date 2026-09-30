#!/bin/sh
# Standalone RTSP H.264 (and optional AAC) playback to HDMI on SC6F0 RX.
# Usage: sudo sh sc6f0-rx-rtsp-test.sh rtsp://server/path [--netns NAME]
# Default network: eth1 in dante_avh.
# Stop with Ctrl+C. No Dante DVR socket or AV-X license is required.
set -eu

usage() {
	echo "Usage: sudo sh $0 rtsp://server/path [--netns NAME]"
	echo "Default: RX eth1 (network namespace dante_avh)."
}
if [ "$#" -eq 1 ] && { [ "$1" = "--help" ] || [ "$1" = "-h" ]; }; then
	usage
	exit 0
fi
if [ "$#" -ne 1 ] && [ "$#" -ne 3 ]; then
	usage >&2
	exit 1
fi
url=$1
case "$url" in
	rtsp://?*) ;;
	*) echo "Expected an RTSP URL: rtsp://server/path" >&2; exit 1 ;;
esac
netns=dante_avh
if [ "$#" -eq 3 ]; then
	if [ "$2" != "--netns" ] || [ -z "$3" ]; then
		usage >&2
		exit 1
	fi
	netns=$3
fi

if [ "$(id -u)" -ne 0 ]; then
	echo "Run this script with sudo" >&2
	exit 1
fi
if ! grep -q '^SC6F0_PETALINUX_RELEASE=RX-' /etc/sc6f0-release-id; then
	echo "This script is for the SC6F0 RX only" >&2
	exit 1
fi
if systemctl is-active --quiet qcap-app; then
	echo "Stop the other RX app first: sudo systemctl stop qcap-app" >&2
	exit 1
fi
if pidof sc6f0-dante-rx >/dev/null 2>&1; then
	echo "A sc6f0-dante-rx process is already running; stop it first" >&2
	exit 1
fi
if [ -n "$netns" ] && ! ip netns exec "$netns" true; then
	echo "Network namespace unavailable: $netns" >&2
	exit 1
fi

if ! mountpoint -q /mnt/dev; then
	/opt/qcap/bin/mount-dev.sh
fi
base=/mnt/dev/zzlee/docker
binary="$base/qcap-demos/_objs/xlnk2_arm64/bin/sc6f0-dante-rx"
qcap_lib="$base/qcap-dev/qcap/build-qcap/_objs/xlnk2_arm64/lib"
zstreamer_lib="$base/zstreamer/build-xlnk2_arm64/src"
if [ ! -x "$binary" ] || [ ! -f "$qcap_lib/libqcap.so" ] ||
   [ ! -f "$zstreamer_lib/libzstreamer.so" ]; then
	echo "Missing RX binary or matching NFS libraries under $base; check the build and /mnt/dev" >&2
	exit 1
fi

# Route shared plane 34 through the continuous dummy stream before the
# application configures VProcSS and DMA, as in the RX auto-test launcher.
modetest -M xlnx -w 34:SC6F0_OVERLAY_SOURCE:0
export LD_LIBRARY_PATH="$qcap_lib:$zstreamer_lib:/opt/qcap/lib"
echo "RX release: $(grep '^SC6F0_PETALINUX_RELEASE=' /etc/sc6f0-release-id)"
echo "Standalone RTSP -> decoder -> HDMI (H.264 video, optional AAC audio)"
echo "Network namespace: $netns (default dante_avh uses eth1)"
echo "Press Ctrl+C to stop and print video/audio frame counts."
exec ip netns exec "$netns" "$binary" --rtsp-url "$url"
