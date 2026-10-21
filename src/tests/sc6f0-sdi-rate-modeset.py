#!/usr/bin/env python3
"""Controlled SDIR candidate05 output-rate A/B; preserves all framebuffer planes.

Only connector is_frac and CRTC pixel-clock mode change. Controller/EDID,
Fast Switching, overlay routing, audio and App service are NOT modified.
The running App's cached mode is deliberately not authoritative during this
experiment. Use --fractional 0 to restore the integer output afterward.
--reinitialize explicitly cycles CRTC ACTIVE off/on at the selected rate;
framebuffer attachments are retained and failures attempt original-state rollback.
"""
import argparse
import ctypes as c
import json
import os
from pathlib import Path


class Mode(c.Structure):
    _fields_ = [('clock', c.c_uint32)] + [(n, c.c_uint16) for n in (
        'hdisplay', 'hsync_start', 'hsync_end', 'htotal', 'hskew', 'vdisplay',
        'vsync_start', 'vsync_end', 'vtotal', 'vscan')] + [
        ('vrefresh', c.c_uint32), ('flags', c.c_uint32), ('type', c.c_uint32),
        ('name', c.c_char * 32)]


class Properties(c.Structure):
    _fields_ = [('count', c.c_uint32), ('ids', c.POINTER(c.c_uint32)),
                ('values', c.POINTER(c.c_uint64))]


class Property(c.Structure):
    _fields_ = [('id', c.c_uint32), ('flags', c.c_uint32), ('name', c.c_char * 32)]


class Blob(c.Structure):
    _fields_ = [('id', c.c_uint32), ('length', c.c_uint32), ('data', c.c_void_p)]


def rate_for_mode(width, height, fractional):
    if (width, height) not in ((1920, 1080), (3840, 2160)) or fractional not in (0, 1):
        raise ValueError('Only 1080p/2160p integer/fractional 60 family is supported')
    return (148350 if fractional else 148500) if width == 1920 else (593406 if fractional else 594000)


def apply_requests(fd, request, disable_request, commit, checked):
    # Validate BOTH transactions before the first hardware-changing commit.
    checked(commit(fd, request, 0x100 | 0x400, None), 'atomic TEST_ONLY')
    if disable_request:
        checked(commit(fd, disable_request, 0x100 | 0x400, None), 'disable TEST_ONLY')
        checked(commit(fd, disable_request, 0x400, None), 'atomic output disable')
    checked(commit(fd, request, 0x400, None), 'atomic rate modeset')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--fractional', type=int, choices=(0, 1), required=True)
    ap.add_argument('--reinitialize', action='store_true',
                    help='explicitly cycle output ACTIVE off/on, preserving framebuffer attachments')
    args = ap.parse_args()
    if os.geteuid() != 0:
        raise RuntimeError('root required')
    product = Path('/proc/device-tree/vp_boot/product').read_bytes().rstrip(b'\0')
    if not product.startswith(b'SC6F0 SDI Dante RX'):
        raise RuntimeError('Not the named SDI RX product')
    version = Path('/sys/devices/platform/amba_pl@0/b1010000.zzlab_env_ctrl/zdev_ver').read_text()
    if not ''.join(version.split()).startswith('0x26102005SDIR'):
        raise RuntimeError('Requires candidate05/SDIR')
    lib = c.CDLL('libdrm.so.2', use_errno=True)

    def bind(name, result, *types):
        fn = getattr(lib, name)
        fn.restype, fn.argtypes = result, list(types)
        return fn

    open_drm = bind('drmOpen', c.c_int, c.c_char_p, c.c_char_p)
    cap = bind('drmSetClientCap', c.c_int, c.c_int, c.c_uint64, c.c_uint64)
    get_props = bind('drmModeObjectGetProperties', c.POINTER(Properties), c.c_int, c.c_uint32, c.c_uint32)
    free_props = bind('drmModeFreeObjectProperties', None, c.POINTER(Properties))
    get_prop = bind('drmModeGetProperty', c.POINTER(Property), c.c_int, c.c_uint32)
    free_prop = bind('drmModeFreeProperty', None, c.POINTER(Property))
    get_blob = bind('drmModeGetPropertyBlob', c.POINTER(Blob), c.c_int, c.c_uint32)
    free_blob = bind('drmModeFreePropertyBlob', None, c.POINTER(Blob))
    create_blob = bind('drmModeCreatePropertyBlob', c.c_int, c.c_int, c.c_void_p, c.c_size_t, c.POINTER(c.c_uint32))
    destroy_blob = bind('drmModeDestroyPropertyBlob', c.c_int, c.c_int, c.c_uint32)
    alloc = bind('drmModeAtomicAlloc', c.c_void_p)
    free = bind('drmModeAtomicFree', None, c.c_void_p)
    add = bind('drmModeAtomicAddProperty', c.c_int, c.c_void_p, c.c_uint32, c.c_uint32, c.c_uint64)
    commit = bind('drmModeAtomicCommit', c.c_int, c.c_int, c.c_void_p, c.c_uint32, c.c_void_p)

    def checked(rc, operation):
        if rc < 0:
            raise OSError(c.get_errno(), operation)
        return rc

    fd = checked(open_drm(b'xlnx', None), 'drmOpen')
    new_blob = c.c_uint32()
    request = disable_request = None
    connector = crtc = None
    try:
        checked(cap(fd, 2, 1), 'universal planes')
        checked(cap(fd, 3, 1), 'atomic')

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
                        result[p.contents.name.decode()] = (p.contents.id, ptr.contents.values[i])
                    finally:
                        free_prop(p)
            finally:
                free_props(ptr)
            return result

        connector = properties(39, 0xc0c0c0c0)
        crtc = properties(37, 0xcccccccc)
        primary = properties(32, 0xeeeeeeee)
        overlay = properties(34, 0xeeeeeeee)
        if not (connector['CRTC_ID'][1] == 37 and crtc['ACTIVE'][1] and
                primary['FB_ID'][1] and overlay['FB_ID'][1] and
                overlay['SC6F0_OVERLAY_SOURCE'][1] == 1):
            raise RuntimeError('Requires active primary + Dante overlay; no changes issued')
        old_blob = get_blob(fd, crtc['MODE_ID'][1])
        if not old_blob:
            raise OSError(c.get_errno(), 'get mode blob')
        try:
            if c.sizeof(Mode) != 68 or old_blob.contents.length != c.sizeof(Mode):
                raise RuntimeError('Unexpected DRM mode ABI')
            mode = Mode.from_buffer_copy(c.string_at(old_blob.contents.data, c.sizeof(Mode)))
        finally:
            free_blob(old_blob)
        if mode.flags & (1 << 4) or mode.clock not in (148500, 148350, 594000, 593406):
            raise RuntimeError('Unexpected interlace/rate')
        before = {'pixel_clock_khz': mode.clock, 'is_frac': connector['is_frac'][1],
                  'primary_fb': primary['FB_ID'][1], 'overlay_fb': overlay['FB_ID'][1],
                  'overlay_source': overlay['SC6F0_OVERLAY_SOURCE'][1],
                  'hdisplay': mode.hdisplay, 'vdisplay': mode.vdisplay,
                  'htotal': mode.htotal, 'vtotal': mode.vtotal}
        mode.clock = rate_for_mode(mode.hdisplay, mode.vdisplay, args.fractional)
        checked(create_blob(fd, c.byref(mode), c.sizeof(mode), c.byref(new_blob)), 'create mode blob')
        request = alloc()
        if not request:
            raise MemoryError('atomic request')
        checked(add(request, 39, connector['is_frac'][0], args.fractional), 'add is_frac')
        checked(add(request, 37, crtc['MODE_ID'][0], new_blob.value), 'add MODE_ID')
        checked(add(request, 37, crtc['ACTIVE'][0], 1), 'add ACTIVE')
        if args.reinitialize:
            disable_request = alloc()
            if not disable_request:
                raise MemoryError('disable request')
            checked(add(disable_request, 37, crtc['ACTIVE'][0], 0), 'add inactive')
        print(json.dumps({'phase': 'plan', 'before': before, 'pixel_clock_khz': mode.clock,
                          'is_frac': args.fractional, 'planes_unchanged': True,
                          'reinitialize': args.reinitialize}), flush=True)
        apply_requests(fd, request, disable_request, commit, checked)
        print(json.dumps({'phase': 'committed', 'pixel_clock_khz': mode.clock,
                          'is_frac': args.fractional, 'warning': 'App cache unchanged; external acceptance pending'}), flush=True)
    except Exception:
        # This legacy driver stores some connector properties outside atomic
        # state; even a failed TEST_ONLY can change its software property cache.
        if connector and crtc and request:
            rollback = alloc()
            if rollback:
                try:
                    checked(add(rollback, 39, connector['is_frac'][0], connector['is_frac'][1]), 'rollback is_frac')
                    checked(add(rollback, 37, crtc['MODE_ID'][0], crtc['MODE_ID'][1]), 'rollback mode')
                    checked(add(rollback, 37, crtc['ACTIVE'][0], crtc['ACTIVE'][1]), 'rollback ACTIVE')
                    checked(commit(fd, rollback, 0x400, None), 'rollback commit')
                finally:
                    free(rollback)
        raise
    finally:
        if disable_request:
            free(disable_request)
        if request:
            free(request)
        if new_blob.value:
            destroy_blob(fd, new_blob.value)
        os.close(fd)


if __name__ == '__main__':
    main()
