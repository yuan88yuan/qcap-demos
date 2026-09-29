#!/bin/sh
# Run on the SC6F0 TX after boot. Stop with Ctrl+C.
set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "Run with sudo: sudo sh $0" >&2
	exit 1
fi
if ! grep -q '^SC6F0_PETALINUX_RELEASE=TX-' /etc/sc6f0-release-id; then
	echo "This script is for the SC6F0 TX only" >&2
	exit 1
fi
if systemctl is-active --quiet qcap-app; then
	echo "Stop the other TX app first: sudo systemctl stop qcap-app" >&2
	exit 1
fi
if pidof sc6f0-dante-tx >/dev/null 2>&1; then
	echo "A sc6f0-dante-tx process is already running; stop it before starting this test" >&2
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
binary="$base/qcap-demos/_objs/xlnk2_arm64/bin/sc6f0-dante-tx"
qcap_lib="$base/qcap-dev/qcap/build-qcap/_objs/xlnk2_arm64/lib"
zstreamer_lib="$base/zstreamer/build-xlnk2_arm64/src"
if [ ! -x "$binary" ] || [ ! -f "$qcap_lib/libqcap.so" ] ||
   [ ! -f "$zstreamer_lib/libzstreamer.so" ]; then
	echo "Missing TX binary or matching NFS libraries under $base; check the build and /mnt/dev" >&2
	exit 1
fi

export LD_LIBRARY_PATH="$qcap_lib:$zstreamer_lib:/opt/qcap/lib"
echo "TX release: $(grep '^SC6F0_PETALINUX_RELEASE=' /etc/sc6f0-release-id)"
echo "TX AVH + Ultra test: $binary"
echo "Press Ctrl+C to stop; Dante Controller selects the RX subscription."
# Match the production 4 MiB TX send buffer (qcap-app-run.sh): the 4K60 H.264
# IDR bursts overflow the default 212992-byte SO_SNDBUF and EAGAIN drops land
# on keyframes. Controlled through --tx-sndbuf, not the environment.
exec ip netns exec dante_avh "$binary" --tx-sndbuf 4194304
