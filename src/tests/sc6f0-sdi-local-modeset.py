#!/usr/bin/env python3
"""Named SC6F0 SDI RX local-output diagnostic (run as root on target).

Reuses the running RX app's primary framebuffer. Disables its overlay and
atomically programs an integer SDI mode; selects master TPG color bars.
This deliberately overrides the app's cached display state, not its EDID or
Controller subscriptions. Do not run during DAU format changes. Restart
qcap-app.service afterward to restore normal Ultra operation.
"""
import argparse
import ctypes as c
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--mode', required=True,
                    choices=['1920x1080@60', '3840x2160@60'])
parser.add_argument('--bypass-anc', action='store_true')
args = parser.parse_args()
assert os.geteuid() == 0, 'root required'
product = Path('/proc/device-tree/vp_boot/product').read_bytes().rstrip(b'\0')
assert product.startswith(b'SC6F0 SDI Dante RX'), product
version = Path('/sys/devices/platform/amba_pl@0/b1010000.zzlab_env_ctrl/zdev_ver').read_text()
assert version.startswith('0x26101501 SDIR '), version


class Mode(c.Structure):
    _fields_ = [('clock', c.c_uint32)] + [
        (n, c.c_uint16) for n in ('hdisplay', 'hsync_start', 'hsync_end', 'htotal',
                                  'hskew', 'vdisplay', 'vsync_start', 'vsync_end',
                                  'vtotal', 'vscan')] + [
        ('vrefresh', c.c_uint32), ('flags', c.c_uint32), ('type', c.c_uint32),
        ('name', c.c_char * 32)]


class Properties(c.Structure):
    _fields_ = [('count', c.c_uint32), ('ids', c.POINTER(c.c_uint32)),
                ('values', c.POINTER(c.c_uint64))]


class Property(c.Structure):
    # Only read the fixed prefix; free the full libdrm allocation afterward.
    _fields_ = [('id', c.c_uint32), ('flags', c.c_uint32), ('name', c.c_char * 32)]


lib = c.CDLL('libdrm.so.2', use_errno=True)


def bind(name, result, *types):
    fn = getattr(lib, name)
    fn.restype, fn.argtypes = result, list(types)
    return fn


open_drm = bind('drmOpen', c.c_int, c.c_char_p, c.c_char_p)
cap = bind('drmSetClientCap', c.c_int, c.c_int, c.c_uint64, c.c_uint64)
get_props = bind('drmModeObjectGetProperties', c.POINTER(Properties),
                 c.c_int, c.c_uint32, c.c_uint32)
free_props = bind('drmModeFreeObjectProperties', None, c.POINTER(Properties))
get_prop = bind('drmModeGetProperty', c.POINTER(Property), c.c_int, c.c_uint32)
free_prop = bind('drmModeFreeProperty', None, c.POINTER(Property))
alloc = bind('drmModeAtomicAlloc', c.c_void_p)
free = bind('drmModeAtomicFree', None, c.c_void_p)
add = bind('drmModeAtomicAddProperty', c.c_int, c.c_void_p, c.c_uint32,
           c.c_uint32, c.c_uint64)
commit = bind('drmModeAtomicCommit', c.c_int, c.c_int, c.c_void_p,
              c.c_uint32, c.c_void_p)
blob_create = bind('drmModeCreatePropertyBlob', c.c_int, c.c_int, c.c_void_p,
                   c.c_size_t, c.POINTER(c.c_uint32))
blob_destroy = bind('drmModeDestroyPropertyBlob', c.c_int, c.c_int, c.c_uint32)


def checked(rc, operation):
    if rc < 0:
        raise OSError(c.get_errno(), operation)
    return rc


fd = checked(open_drm(b'xlnx', None), 'drmOpen')
request, blob = None, c.c_uint32()
try:
    checked(cap(fd, 2, 1), 'universal planes')
    checked(cap(fd, 3, 1), 'atomic mode')

    def properties(obj, kind):
        ptr = get_props(fd, obj, kind)
        if not ptr:
            raise OSError(c.get_errno(), 'get properties')
        result = {}
        try:
            for i in range(ptr.contents.count):
                p = get_prop(fd, ptr.contents.ids[i])
                if not p:
                    raise OSError(c.get_errno(), 'get property')
                try:
                    result[p.contents.name.decode()] = (
                        p.contents.id, ptr.contents.values[i])
                finally:
                    free_prop(p)
        finally:
            free_props(ptr)
        return result

    connector = properties(39, 0xc0c0c0c0)
    crtc = properties(37, 0xcccccccc)
    primary = properties(32, 0xeeeeeeee)
    overlay = properties(34, 0xeeeeeeee)
    assert 'sdi_mode' in connector and 'SC6F0_OVERLAY_SOURCE' in overlay
    assert primary['FB_ID'][1] and crtc['ACTIVE'][1], 'running RX framebuffer required'
    width, height = (1920, 1080) if args.mode.startswith('1920') else (3840, 2160)
    mode = Mode(clock=148500 if width == 1920 else 594000,
                hdisplay=width, hsync_start=2008 if width == 1920 else 4016,
                hsync_end=2052 if width == 1920 else 4104,
                htotal=2200 if width == 1920 else 4400,
                vdisplay=height, vsync_start=1084 if height == 1080 else 2168,
                vsync_end=1089 if height == 1080 else 2178,
                vtotal=1125 if height == 1080 else 2250,
                vrefresh=60, flags=5, name=f'{width}x{height}'.encode())
    assert c.sizeof(mode) == 68
    checked(blob_create(fd, c.byref(mode), c.sizeof(mode), c.byref(blob)), 'create mode blob')
    request = alloc()
    if not request:
        raise MemoryError('atomic request')

    def put(obj, props, name, value):
        checked(add(request, obj, props[name][0], value), f'add {name}')

    put(39, connector, 'CRTC_ID', 37)
    for name, value in [('sdi_mode', 2 if width == 1920 else 5),
                        ('sdi_data_stream', 2 if width == 1920 else 8),
                        ('is_frac', 0), ('sdi_420_in', 0), ('sdi_420_out', 0),
                        ('sdi_444_out', 0), ('width_out', width), ('height_out', height),
                        ('in_fmt', 4106), ('out_fmt', 8218), ('c_encoding', 0)]:
        put(39, connector, name, value)
    put(37, crtc, 'MODE_ID', blob.value)
    put(37, crtc, 'ACTIVE', 1)
    for name, value in [('CRTC_ID', 37), ('FB_ID', primary['FB_ID'][1]),
                        ('CRTC_X', 0), ('CRTC_Y', 0), ('CRTC_W', width),
                        ('CRTC_H', height), ('SRC_X', 0), ('SRC_Y', 0),
                        ('SRC_W', width << 16), ('SRC_H', height << 16)]:
        put(32, primary, name, value)
    put(34, overlay, 'FB_ID', 0)
    put(34, overlay, 'CRTC_ID', 0)
    put(34, overlay, 'SC6F0_OVERLAY_SOURCE', 0)
    # Validate the complete transaction before any hardware change.
    checked(commit(fd, request, 0x100 | 0x400, None), 'atomic TEST_ONLY')
    subprocess.run(['amixer', '-c', '1', 'cset', 'numid=1', 'off'], check=True)
    checked(commit(fd, request, 0x400, None), 'atomic modeset')
    Path('/sys/bus/platform/drivers/zzlab-embeddedsw/a0040000.v_tpg/bckgnd_id').write_text('9\n')
    if args.bypass_anc:
        before = int(subprocess.check_output(['devmem', '0xb00c0004', '32'], text=True), 0)
        subprocess.run(['devmem', '0xb00c0004', '32', hex(before & ~(1 << 18))], check=True)
    print(f'LOCAL master color bars: {args.mode}, overlay disabled, audio off; '
          f'ANC bypass={args.bypass_anc}. App/EDID cached mode is not changed.')
finally:
    if request:
        free(request)
    if blob.value:
        blob_destroy(fd, blob.value)
    os.close(fd)
