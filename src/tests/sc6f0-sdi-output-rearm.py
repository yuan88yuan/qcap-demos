#!/usr/bin/env python3
"""Operator-only SDI output diagnostic via the existing App's DRM descriptor.

Default: read/TEST_ONLY. --apply is blocked: the tested driver disables CSC
without restarting it on an ACTIVE-only enable. Never writes GPIO/MMIO or restarts the
App/decoder. Reserve the board externally: guards are not a process/DRM lock.
Requires Linux pidfd_getfd permissions; never relaxes ptrace/security policy.
"""
import argparse
import ctypes as C
import hashlib
import json
import mmap
import os
from pathlib import Path
import re
import struct
import time

CRTC = 0xCCCCCCCC
CONNECTOR = 0xC0C0C0C0
PLANE = 0xEEEEEEEE
ALLOW_MODESET = 0x400
TEST_ONLY = 0x100


class Props(C.Structure):
    _fields_ = [('count', C.c_uint32), ('ids', C.POINTER(C.c_uint32)),
                ('values', C.POINTER(C.c_uint64))]


class Property(C.Structure):
    _fields_ = [('id', C.c_uint32), ('flags', C.c_uint32), ('name', C.c_char * 32)]


def check_snapshot(s, args):
    if s['boot'] != args.boot_id or s['product'] != 'SC6F0 SDI Dante RX':
        raise RuntimeError('REFUSE boot/product mismatch')
    if s['release'] != args.release or s['app_sha256'] != args.app_sha256:
        raise RuntimeError('REFUSE release/App mismatch')
    if not s['master'] or s['active'] != 1 or not s['mode_id']:
        raise RuntimeError('REFUSE missing DRM master or active mode')
    if s['primary_crtc'] != args.crtc or s['video_crtc'] != args.crtc or not s['video_fb']:
        raise RuntimeError('REFUSE unexpected plane/CRTC binding')
    if s['connector_crtc'] != args.crtc or s['sdi_mode'] != 5:
        raise RuntimeError('REFUSE not the expected active 12G SDI connector')


class DRM:
    def __init__(self, pid, source_fd):
        self.fd = -1
        self.lib = C.CDLL('libdrm.so.2', use_errno=True)
        for name, result, args in [
            ('drmIsMaster', C.c_int, [C.c_int]),
            ('drmModeObjectGetProperties', C.POINTER(Props), [C.c_int, C.c_uint32, C.c_uint32]),
            ('drmModeFreeObjectProperties', None, [C.POINTER(Props)]),
            ('drmModeGetProperty', C.POINTER(Property), [C.c_int, C.c_uint32]),
            ('drmModeFreeProperty', None, [C.POINTER(Property)]),
            ('drmModeAtomicAlloc', C.c_void_p, []),
            ('drmModeAtomicFree', None, [C.c_void_p]),
            ('drmModeAtomicAddProperty', C.c_int, [C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint64]),
            ('drmModeAtomicCommit', C.c_int, [C.c_int, C.c_void_p, C.c_uint32, C.c_void_p]),
        ]:
            f = getattr(self.lib, name); f.restype = result; f.argtypes = args
        if os.readlink(f'/proc/{pid}/fd/{source_fd}') != '/dev/dri/card0':
            raise RuntimeError('REFUSE non-DRM source descriptor')
        libc = C.CDLL(None, use_errno=True)
        with_pid = os.pidfd_open(pid)
        try:
            # asm-generic syscall number on the supported AArch64 board.
            self.fd = libc.syscall(C.c_long(438), C.c_int(with_pid), C.c_int(source_fd), C.c_uint(0))
            if self.fd < 0: raise OSError(C.get_errno(), 'pidfd_getfd')
        finally:
            os.close(with_pid)

    def properties(self, obj, kind):
        p = self.lib.drmModeObjectGetProperties(self.fd, obj, kind)
        if not p: raise OSError(C.get_errno(), 'DRM properties')
        result = {}
        try:
            for i in range(p.contents.count):
                prop = self.lib.drmModeGetProperty(self.fd, p.contents.ids[i])
                if not prop: raise OSError(C.get_errno(), 'DRM property metadata')
                try: result[prop.contents.name.decode()] = (prop.contents.id, p.contents.values[i])
                finally: self.lib.drmModeFreeProperty(prop)
        finally: self.lib.drmModeFreeObjectProperties(p)
        return result

    def active(self, crtc, value, flags):
        prop = self.properties(crtc, CRTC)['ACTIVE'][0]
        request = self.lib.drmModeAtomicAlloc()
        if not request: raise MemoryError('atomic request')
        try:
            if self.lib.drmModeAtomicAddProperty(request, crtc, prop, value) < 0:
                raise OSError(C.get_errno(), 'atomic add ACTIVE')
            if self.lib.drmModeAtomicCommit(self.fd, request, flags, None) != 0:
                raise OSError(C.get_errno(), 'atomic ACTIVE commit')
        finally: self.lib.drmModeAtomicFree(request)


def video_status():
    fd = os.open('/dev/mem', os.O_RDONLY | os.O_SYNC)
    try:
        with mmap.mmap(fd, 4096, access=mmap.ACCESS_READ, offset=0xB00C0000) as mm:
            return hex(struct.unpack_from('<I', mm, 0x6C)[0])
    finally: os.close(fd)


def snapshot(drm, a):
    values = dict(re.findall(r'^(SC6F0_\w+)=(.*)$', Path('/etc/sc6f0-release-id').read_text(), re.M))
    c = drm.properties(a.crtc, CRTC); p = drm.properties(a.primary, PLANE)
    v = drm.properties(a.plane, PLANE); out = drm.properties(a.connector, CONNECTOR)
    return dict(boot=Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
                product=Path('/proc/device-tree/vp_boot/product').read_bytes().rstrip(b'\0').decode(),
                release=values.get('SC6F0_PETALINUX_RELEASE'),
                app_sha256=hashlib.sha256(Path(f'/proc/{a.pid}/exe').read_bytes()).hexdigest(),
                master=bool(drm.lib.drmIsMaster(drm.fd)), active=c['ACTIVE'][1],
                mode_id=c['MODE_ID'][1], primary_crtc=p['CRTC_ID'][1],
                video_crtc=v['CRTC_ID'][1], video_fb=v['FB_ID'][1],
                connector_crtc=out['CRTC_ID'][1], sdi_mode=out.get('sdi_mode', (0, -1))[1],
                video_status=video_status())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ['pid', 'source-fd', 'crtc', 'primary', 'plane', 'connector']:
        p.add_argument('--' + name, required=True, type=int)
    for name in ['boot-id', 'release', 'app-sha256']:
        p.add_argument('--' + name, required=True)
    p.add_argument('--apply', action='store_true')
    a = p.parse_args()
    if a.apply:
        raise SystemExit('REFUSE: ACTIVE-only rearm caused CSC stop/no signal and '
                         'vblank timeout on RX-PL-2026.10.16.01-SDI; '
                         'a successful TEST_ONLY/commit is not output-health proof')
    if os.uname().machine != 'aarch64' or os.geteuid() != 0:
        raise SystemExit('REFUSE: requires root on the AArch64 target')
    drm = DRM(a.pid, a.source_fd)
    try:
        before = snapshot(drm, a); check_snapshot(before, a)
        drm.active(a.crtc, 0, ALLOW_MODESET | TEST_ONLY)
        print(json.dumps({'before': before, 'disable_test_only': 'PASS', 'apply': a.apply}), flush=True)
        if a.apply:
            check_snapshot(snapshot(drm, a), a)
            disabled = False
            try:
                drm.active(a.crtc, 0, ALLOW_MODESET); disabled = True
                time.sleep(0.2)
            finally:
                if disabled: drm.active(a.crtc, 1, ALLOW_MODESET)
            time.sleep(0.2)
            after = snapshot(drm, a); check_snapshot(after, a)
            if after['mode_id'] != before['mode_id']:
                raise RuntimeError('Unexpected mode blob change')
            print(json.dumps({'after': after, 'output_rearmed': True}), flush=True)
    finally: os.close(drm.fd)


if __name__ == '__main__':
    main()
