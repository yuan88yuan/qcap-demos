#!/usr/bin/env python3
"""RX-only, operator diagnostic of the EXISTING shared output-reset path.

Read-only unless --pulse is supplied. Uses the primary TPG owner's sysfs reset,
not GPIO export or MMIO writes. This also resets the primary/dummy TPG and the
CSC/packer/SDI video input; it is NOT a seamless restart or an acceptance test.
Run only while an operator has reserved the board: guards are not DRM locks.
"""
import argparse
import json
import mmap
import os
from pathlib import Path
import re
import struct
import subprocess
import time

BOOT = Path('/proc/sys/kernel/random/boot_id')
TPG = Path('/sys/bus/platform/devices/a0000000.v_tpg')
CSC = 0xB00B0000
SDI = 0xB00C0000


def run(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=10)


def controls():
    values = {}
    # Separate pages: CSC and SDI are 64 KiB apart, NOT in one 12 KiB map.
    fd = os.open('/dev/mem', os.O_RDONLY | os.O_SYNC)
    try:
        for name, base in [('csc', CSC), ('sdi', SDI)]:
            with mmap.mmap(fd, 4096, access=mmap.ACCESS_READ, offset=base) as mm:
                values[name] = struct.unpack_from('<I', mm, 0)[0]
    finally:
        os.close(fd)
    return values


def check_snapshot(s, expected_boot, expected_release):
    """Pure fail-closed guard, also exercised by host tests."""
    if s['boot'] != expected_boot:
        raise RuntimeError('REFUSE boot ID mismatch')
    if s['product'] != 'SC6F0 SDI Dante RX':
        raise RuntimeError('REFUSE product mismatch')
    if s['release'] != expected_release or s['fpga_compatible'] != '0x26101203':
        raise RuntimeError('REFUSE release/FPGA contract mismatch')
    if s['tpg_compatible'] != 'yuan,v-tpg' or s['reset_gpio'][1:] != [79, 1]:
        raise RuntimeError('REFUSE unexpected reset binding')
    if s['gpio_compatible'] != 'xlnx,zynqmp-gpio-1.0':
        raise RuntimeError('REFUSE unexpected GPIO controller')
    if s['owners_rc'] != 1 or s['process_rc'] != 1:
        raise RuntimeError('REFUSE DRM owner, App/test process, or probe error')
    if not s['crtc_inactive'] or not s['audio_off'] or s['sdi'] != 0:
        raise RuntimeError('REFUSE active output or audio passthrough')


def snapshot():
    release = dict(re.findall(r'^(SC6F0_\w+)=(.*)$',
                             Path('/etc/sc6f0-release-id').read_text(), re.M))
    dt = TPG / 'of_node'
    gpio = list(struct.unpack('>III', (dt / 'reset-gpios').read_bytes()))
    gpio_node = Path('/sys/bus/platform/devices/ff0a0000.gpio/of_node')
    # Resolve the actual GPIO phandle, not just its numeric line/flags.
    phandle = struct.unpack('>I', (gpio_node / 'phandle').read_bytes())[0]
    if phandle != gpio[0]:
        raise RuntimeError('REFUSE reset controller phandle mismatch')
    state = Path('/sys/kernel/debug/dri/0/state').read_text()
    crtcs = re.findall(r'^crtc\[.*?(?=^\S|\Z)', state, re.M | re.S)
    crtc_inactive = bool(crtcs) and all(
        '\tenable=0\n' in c and '\tactive=0\n' in c for c in crtcs)
    audio = run('amixer', '-c', '1', 'cget', 'name=SDI FPGA Passthrough')
    return dict(boot=BOOT.read_text().strip(),
                product=Path('/proc/device-tree/vp_boot/product').read_bytes().rstrip(b'\0').decode(),
                release=release.get('SC6F0_PETALINUX_RELEASE'),
                fpga_compatible=release.get('SC6F0_FPGA_COMPATIBLE'),
                tpg_compatible=(dt / 'compatible').read_bytes().rstrip(b'\0').decode(),
                reset_gpio=gpio,
                gpio_compatible=(gpio_node / 'compatible').read_bytes().rstrip(b'\0').decode(),
                owners_rc=run('fuser', '/dev/dri/card0').returncode,
                process_rc=run('pidof', 'modetest', 'sc6f0-dante-rx').returncode,
                crtc_inactive=crtc_inactive,
                audio_off=audio.returncode == 0 and ': values=off' in audio.stdout,
                **controls())


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--boot-id', required=True)
    ap.add_argument('--release', required=True)
    ap.add_argument('--pulse', action='store_true', help='one shared reset; interrupts video state')
    args = ap.parse_args()
    before = snapshot()
    check_snapshot(before, args.boot_id, args.release)
    report = {'before': before, 'pulse': args.pulse}
    if args.pulse:
        # Recheck immediately before the only write; reserve board externally.
        check_snapshot(snapshot(), args.boot_id, args.release)
        (TPG / 'reset').write_text('1\n')
        time.sleep(0.2)
        after = snapshot()
        check_snapshot(after, args.boot_id, args.release)
        report['after'] = after
        report['idle_recovered'] = bool(after['csc'] & 4)
    print(json.dumps(report, indent=2), flush=True)
    return 0 if not args.pulse or report['idle_recovered'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
