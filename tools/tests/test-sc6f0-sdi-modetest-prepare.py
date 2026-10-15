#!/usr/bin/env python3
"""Host-only guard/ordering tests; not a hardware acceptance test."""
from pathlib import Path
import contextlib
import io
import runpy
import unittest

M = runpy.run_path(str(Path(__file__).resolve().parents[2] /
                      'src/tests/sc6f0-sdi-modetest-prepare.py'))


class FakeDRM:
    def __init__(self, active=0, primary_type=1, attached=37):
        self.active = active
        self.primary_type = primary_type
        self.attached = attached
        self.values = {n: 0 for n, _ in M['PROPERTIES']}
        self.commits = []

    def properties(self, obj, kind):
        if kind == M['OBJECT_CONNECTOR']:
            return {**{n: (i + 100, v) for i, (n, v) in enumerate(self.values.items())},
                    'CRTC_ID': (3, self.attached)}
        if kind == M['OBJECT_PLANE']:
            return {'type': (2, self.primary_type)}
        return {'ACTIVE': (4, self.active)}

    def commit(self, updates, flags=0):
        self.commits.append((updates, flags))
        for _, kind, name, value in updates:
            if kind == M['OBJECT_CONNECTOR']:
                self.values[name] = value
            elif kind == M['OBJECT_CRTC'] and name == 'ACTIVE':
                self.active = value


class PrepareTest(unittest.TestCase):
    def prepare(self, drm, clear=False):
        with contextlib.redirect_stdout(io.StringIO()):
            M['prepare'](drm, 39, 37, 32, clear)

    def test_active_requires_explicit_clear(self):
        drm = FakeDRM(active=1)
        with self.assertRaises(RuntimeError):
            self.prepare(drm)
        self.assertEqual(drm.commits, [])

    def test_clear_then_single_property_batch(self):
        drm = FakeDRM(active=1)
        self.prepare(drm, True)
        self.assertEqual(len(drm.commits), 2)
        self.assertEqual(drm.commits[0][1], M['ALLOW_MODESET'])
        self.assertEqual(len(drm.commits[1][0]), len(M['PROPERTIES']))
        self.assertEqual(drm.active, 0)
        self.assertEqual(drm.values['sdi_mode'], 5)
        self.assertEqual(drm.values['width_out'], 3840)

    def test_wrong_primary_refused_before_commit(self):
        drm = FakeDRM(primary_type=0)
        with self.assertRaises(RuntimeError):
            self.prepare(drm, True)
        self.assertEqual(drm.commits, [])

    def test_other_crtc_refused_before_commit(self):
        drm = FakeDRM(attached=99)
        with self.assertRaises(RuntimeError):
            self.prepare(drm, True)
        self.assertEqual(drm.commits, [])

    def test_no_clear_needed_when_inactive(self):
        drm = FakeDRM(active=0)
        self.prepare(drm)
        self.assertEqual(len(drm.commits), 1)

    def test_1080p60_is_3ga_two_stream_integer(self):
        drm = FakeDRM(active=0)
        with contextlib.redirect_stdout(io.StringIO()):
            M['prepare'](drm, 39, 37, 32, False, '1080p60')
        self.assertEqual(drm.values['sdi_mode'], 2)
        self.assertEqual(drm.values['sdi_data_stream'], 2)
        self.assertEqual(drm.values['width_out'], 1920)
        self.assertEqual(drm.values['height_out'], 1080)
        self.assertEqual(drm.values['is_frac'], 0)
        self.assertEqual(drm.values['out_fmt'], 8218)

    def test_request_allocated_before_properties_added(self):
        events = []

        class Lib:
            def drmModeAtomicAlloc(self):
                events.append('alloc')
                return 123

            def drmModeAtomicAddProperty(self, request, obj, prop, value):
                self.assert_request(request)
                events.append('add')
                return 1  # Positive index is success, not an error.

            def assert_request(self, request):
                if request != 123:
                    raise AssertionError('invalid atomic request')

            def drmModeAtomicCommit(self, *args):
                events.append('commit')
                return 0

            def drmModeAtomicFree(self, request):
                events.append('free')

        drm = M['DRM'].__new__(M['DRM'])
        drm.fd = 7
        drm.lib = Lib()
        drm.properties = lambda obj, kind: {'sdi_mode': (41, 0)}
        drm.commit([(39, M['OBJECT_CONNECTOR'], 'sdi_mode', 5)])
        self.assertEqual(events, ['alloc', 'add', 'commit', 'free'])


if __name__ == '__main__':
    unittest.main()
