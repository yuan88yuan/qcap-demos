#!/usr/bin/env python3
"""Host-only rate-plan/ABI/coherent-read checks; no target or /dev/mem access."""
import ast
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'src/tests' / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


mode = load('rate_modeset', 'sc6f0-sdi-rate-modeset.py')
rate = load('rate_check', 'sc6f0-axis-probe-rate-check.py')


class Reference:
    def __init__(self, values):
        self.values = iter(values)
        self.reads = []

    def read32(self, offset):
        self.reads.append(offset)
        return next(self.values)


class RateDiagnosticsTests(unittest.TestCase):
    def test_supported_fpga_guards(self):
        for version in ('0x26102005 SDIR 0x00000100', '0x26102101 SDIR 0x00000100'):
            mode.validate_version(version)
        for version in ('0x26102102 SDIR', '0x26102101 SDIT', '', '0x26102005SDIR'):
            with self.assertRaises(RuntimeError):
                mode.validate_version(version)

    def test_supported_rates(self):
        for width, height, integer, fractional in (
                (1920, 1080, 148500, 148350), (3840, 2160, 594000, 593406)):
            self.assertEqual(mode.rate_for_mode(width, height, 0), integer)
            self.assertEqual(mode.rate_for_mode(width, height, 1), fractional)

    def test_unsupported_plans_rejected(self):
        for args in ((1280, 720, 0), (3840, 1080, 1), (3840, 2160, 2), (1920, 1080, -1)):
            with self.assertRaises(ValueError):
                mode.rate_for_mode(*args)

    def test_libdrm_mode_abi(self):
        self.assertEqual(mode.c.sizeof(mode.Mode), 68)
        self.assertEqual(mode.Mode.clock.offset, 0)
        self.assertEqual(mode.Mode.vrefresh.offset, 24)
        self.assertEqual(mode.Mode.name.offset, 36)
        self.assertEqual(mode.c.sizeof(mode.Blob), 16)

    def apply_fixture(self, disable=None, reject=None):
        calls = []
        def commit(fd, request, flags, data):
            calls.append((request, flags))
            return -1 if (request, flags) == reject else 0
        def checked(rc, operation):
            if rc < 0:
                raise OSError(operation)
        try:
            mode.apply_requests(3, 'enable', disable, commit, checked)
        except OSError:
            return calls, False
        return calls, True

    def test_reinitialize_validates_both_before_hardware_commit(self):
        calls, success = self.apply_fixture(disable='disable')
        self.assertTrue(success)
        self.assertEqual(calls, [('enable', 0x500), ('disable', 0x500),
                                 ('disable', 0x400), ('enable', 0x400)])

    def test_normal_modeset_has_no_disable(self):
        calls, success = self.apply_fixture()
        self.assertTrue(success)
        self.assertEqual(calls, [('enable', 0x500), ('enable', 0x400)])

    def test_rejected_enable_validation_has_no_hardware_commit(self):
        calls, success = self.apply_fixture(disable='disable', reject=('enable', 0x500))
        self.assertFalse(success)
        self.assertEqual(calls, [('enable', 0x500)])

    def test_rejected_disable_validation_has_no_hardware_commit(self):
        calls, success = self.apply_fixture(disable='disable', reject=('disable', 0x500))
        self.assertFalse(success)
        self.assertEqual(calls, [('enable', 0x500), ('disable', 0x500)])

    def test_coherent_reference(self):
        r = Reference([3, 9, 3])
        self.assertEqual(rate.read_reference(r), (3 << 32) | 9)
        self.assertEqual(r.reads, [0x24, 0x20, 0x24])

    def test_reference_rollover_retry(self):
        r = Reference([0, 0xFFFFFFFF, 1, 1, 15, 1])
        self.assertEqual(rate.read_reference(r), (1 << 32) | 15)
        self.assertEqual(len(r.reads), 6)

    def test_reference_retry_is_bounded(self):
        r = Reference([0, 0, 1] * 5)
        with self.assertRaises(RuntimeError):
            rate.read_reference(r)
        self.assertEqual(len(r.reads), 15)

    def test_rate_check_never_issues_mmio_write(self):
        source = (ROOT / 'src/tests/sc6f0-axis-probe-rate-check.py').read_text()
        tree = ast.parse(source)
        self.assertFalse(any(isinstance(n, ast.Attribute) and n.attr == 'write32'
                             for n in ast.walk(tree)))


if __name__ == '__main__':
    unittest.main()
