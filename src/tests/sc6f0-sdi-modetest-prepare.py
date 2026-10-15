#!/usr/bin/env python3
"""Prepare a named, idle SC6F0 SDI RX for local SDI modetest.

Work around modetest versions that process -a/-w before allocating the atomic
request. No MMIO, reboot, SDK change, source stream or firmware installation.
Only use --clear-failed-test-state for the operator-owned failed local test.
After this tool succeeds, use modetest -a WITHOUT -w for the actual modeset.
"""
import argparse
import ctypes as C
from pathlib import Path
import subprocess

U32 = C.c_uint32
U64 = C.c_uint64
OBJECT_CRTC = 0xCCCCCCCC
OBJECT_CONNECTOR = 0xC0C0C0C0
OBJECT_PLANE = 0xEEEEEEEE
ALLOW_MODESET = 0x400
PROPERTIES = [('sdi_mode', 5), ('sdi_data_stream', 8), ('is_frac', 0),
              ('sdi_420_in', 0), ('sdi_420_out', 0), ('sdi_444_out', 0),
              ('width_out', 3840), ('height_out', 2160),
              ('in_fmt', 4106), ('out_fmt', 8218), ('c_encoding', 0)]
HD_OVERRIDES = {'sdi_mode': 2, 'sdi_data_stream': 2,
                'width_out': 1920, 'height_out': 1080}
PROFILES = {'2160p60': PROPERTIES,
            '1080p60': [(name, HD_OVERRIDES.get(name, value))
                        for name, value in PROPERTIES]}


class ObjectProperties(C.Structure):
    _fields_ = [('count_props', U32), ('props', C.POINTER(U32)),
                ('prop_values', C.POINTER(U64))]


class PropertyPrefix(C.Structure):
    _fields_ = [('prop_id', U32), ('flags', U32), ('name', C.c_char * 32)]


class DRM:
    def __init__(self):
        self.lib = C.CDLL('libdrm.so.2', use_errno=True)
        for name, result, arguments in [
            ('drmOpen', C.c_int, [C.c_char_p, C.c_char_p]),
            ('drmClose', C.c_int, [C.c_int]),
            ('drmSetClientCap', C.c_int, [C.c_int, U64, U64]),
            ('drmSetMaster', C.c_int, [C.c_int]),
            ('drmModeObjectGetProperties', C.POINTER(ObjectProperties),
             [C.c_int, U32, U32]),
            ('drmModeFreeObjectProperties', None, [C.POINTER(ObjectProperties)]),
            ('drmModeGetProperty', C.POINTER(PropertyPrefix), [C.c_int, U32]),
            ('drmModeFreeProperty', None, [C.POINTER(PropertyPrefix)]),
            ('drmModeAtomicAlloc', C.c_void_p, []),
            ('drmModeAtomicFree', None, [C.c_void_p]),
            ('drmModeAtomicAddProperty', C.c_int, [C.c_void_p, U32, U32, U64]),
            ('drmModeAtomicCommit', C.c_int, [C.c_int, C.c_void_p, U32, C.c_void_p]),
        ]:
            function = getattr(self.lib, name)
            function.restype = result
            function.argtypes = arguments
        self.fd = self.lib.drmOpen(b'xlnx', None)
        if self.fd < 0:
            raise OSError(C.get_errno(), 'drmOpen')
        try:
            self.check(self.lib.drmSetClientCap(self.fd, 2, 1), 'universal planes')
            self.check(self.lib.drmSetClientCap(self.fd, 3, 1), 'atomic capability')
            self.check(self.lib.drmSetMaster(self.fd), 'DRM master')
        except Exception:
            self.close()
            raise

    @staticmethod
    def check(result, label):
        if result < 0:
            raise OSError(C.get_errno(), label + ': result ' + str(result))

    def close(self):
        if self.fd >= 0:
            self.lib.drmClose(self.fd)
            self.fd = -1

    def properties(self, obj, kind):
        pointer = self.lib.drmModeObjectGetProperties(self.fd, obj, kind)
        if not pointer:
            raise OSError(C.get_errno(), 'get properties for ' + str(obj))
        result = {}
        try:
            for i in range(pointer.contents.count_props):
                prop_id = pointer.contents.props[i]
                prop = self.lib.drmModeGetProperty(self.fd, prop_id)
                if not prop:
                    raise OSError(C.get_errno(), 'get property ' + str(prop_id))
                try:
                    name = prop.contents.name.decode()
                    result[name] = (prop_id, pointer.contents.prop_values[i])
                finally:
                    self.lib.drmModeFreeProperty(prop)
        finally:
            self.lib.drmModeFreeObjectProperties(pointer)
        return result

    def commit(self, updates, flags=0):
        # Allocate BEFORE adding even the first property (modetest workaround).
        request = self.lib.drmModeAtomicAlloc()
        if not request:
            raise MemoryError('drmModeAtomicAlloc')
        try:
            for obj, kind, name, value in updates:
                properties = self.properties(obj, kind)
                if name not in properties:
                    raise RuntimeError('Missing required property ' + name)
                self.check(self.lib.drmModeAtomicAddProperty(
                    request, obj, properties[name][0], value), 'add ' + name)
            self.check(self.lib.drmModeAtomicCommit(
                self.fd, request, flags, None), 'atomic commit')
        finally:
            self.lib.drmModeAtomicFree(request)


def prepare(drm, connector, crtc, primary, clear_failed, profile='2160p60'):
    expected = PROFILES[profile]
    connector_props = drm.properties(connector, OBJECT_CONNECTOR)
    if any(name not in connector_props for name, _ in PROPERTIES):
        raise RuntimeError('Connector is not the expected SDI connector')
    primary_props = drm.properties(primary, OBJECT_PLANE)
    if primary_props.get('type', (None, None))[1] != 1:
        raise RuntimeError('Selected plane is not primary')
    current_crtc = connector_props.get('CRTC_ID', (None, 0))[1]
    if current_crtc not in (0, crtc):
        raise RuntimeError('Connector attached to a different CRTC')
    if clear_failed:
        updates = [(primary, OBJECT_PLANE, 'FB_ID', 0),
                   (primary, OBJECT_PLANE, 'CRTC_ID', 0),
                   (connector, OBJECT_CONNECTOR, 'CRTC_ID', 0),
                   (crtc, OBJECT_CRTC, 'ACTIVE', 0),
                   (crtc, OBJECT_CRTC, 'MODE_ID', 0)]
        drm.commit(updates, ALLOW_MODESET)
        print('FAILED_TEST_STATE_DISABLED', flush=True)
    if drm.properties(crtc, OBJECT_CRTC).get('ACTIVE', (None, None))[1] != 0:
        raise RuntimeError('CRTC must be inactive before preparing properties')
    drm.commit([(connector, OBJECT_CONNECTOR, name, value)
                for name, value in expected])
    actual = drm.properties(connector, OBJECT_CONNECTOR)
    for name, value in expected:
        readback = actual.get(name, (None, None))[1]
        print('PROPERTY_READBACK', name, readback, flush=True)
        if readback != value:
            raise RuntimeError('Readback mismatch: ' + name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot-id', required=True)
    parser.add_argument('--connector', required=True, type=int)
    parser.add_argument('--crtc', required=True, type=int)
    parser.add_argument('--primary', required=True, type=int)
    parser.add_argument('--clear-failed-test-state', action='store_true')
    parser.add_argument('--profile', choices=sorted(PROFILES), default='2160p60')
    args = parser.parse_args()
    if Path('/proc/device-tree/vp_boot/product').read_bytes().rstrip(b'\0') != b'SC6F0 SDI Dante RX':
        raise SystemExit('REFUSE: wrong product')
    if Path('/proc/sys/kernel/random/boot_id').read_text().strip() != args.boot_id:
        raise SystemExit('REFUSE: boot changed; refresh identity/DRM IDs')
    if subprocess.run(['pidof', 'sc6f0-dante-rx', 'modetest'],
                      stdout=subprocess.DEVNULL).returncode == 0:
        raise SystemExit('REFUSE: competing App/modetest')
    owners = subprocess.run(['fuser', '/dev/dri/card0'], capture_output=True)
    if owners.returncode != 1:
        raise SystemExit('REFUSE: DRM is in use or fuser failed')
    drm = DRM()
    try:
        prepare(drm, args.connector, args.crtc, args.primary,
                args.clear_failed_test_state, args.profile)
    finally:
        drm.close()
    print('PREPARE_OK: run atomic modetest without -w', flush=True)


if __name__ == '__main__':
    main()
