#!/bin/sh
# Run on the SC6F0 RX after boot. Stop with Ctrl+C.
set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "Run with sudo: sudo sh $0" >&2
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
	echo "A sc6f0-dante-rx process is already running; stop it before starting this test" >&2
	exit 1
fi
if [ ! -e /var/run/netns/dante_avh ] || [ ! -S /var/run/dante/dvr ]; then
	echo "Dante AVH namespace or DVR socket missing; check qcap-start and Dante AV-X logs" >&2
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

# This firmware's shared plane 34 must be routed through the continuous
# dummy stream before the new app configures the VProcSS and DMA.
modetest -M xlnx -w 34:SC6F0_OVERLAY_SOURCE:0
export LD_LIBRARY_PATH="$qcap_lib:$zstreamer_lib:/opt/qcap/lib"
echo "RX release: $(grep '^SC6F0_PETALINUX_RELEASE=' /etc/sc6f0-release-id)"
echo "RX auto test: HDMI mode follows Ultra/AVH format and EDID; VProcSS converts to RGB and scales when needed."
echo "Press Ctrl+C to stop; wait about 10 seconds per source in Dante Controller."
exec ip netns exec dante_avh "$binary" --media-path auto --interval 3
