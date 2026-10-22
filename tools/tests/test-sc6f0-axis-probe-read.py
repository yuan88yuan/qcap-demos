#!/usr/bin/env python3
"""Host-only fake MMIO tests. Never opens /dev/mem or connects to hardware."""
import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import struct
import tempfile
import unittest
from unittest.mock import patch

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('probe', root / 'src/tests/sc6f0-axis-probe-read.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class Fake:
    def __init__(self, mask=31):
        self.values = {0: probe.MAGIC, 4: probe.ABI, 8: 5, 0x10: 1, 0x14: 0, 0x18: mask, 0x30: 3, 0x34: 28}
        self.writes = []

    def read32(self, offset):
        return self.values.get(offset, 0)

    def write32(self, offset, value):
        self.writes.append((offset, value))
        self.values[0x10] = (self.values[0x10] + 1) & 0xFFFFFFFF


class ReaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='sc6f0-mmio32-test-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.native_path = Path(cls.temp.name) / 'libsc6f0-mmio32.so'
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-fPIC', '-shared', str(root / 'src/tests/sc6f0-mmio32.c'),
                        '-o', str(cls.native_path)], check=True)
        cls.native = probe.NativeMMIO(cls.native_path)

    def register_map(self, trace):
        r = probe.RegisterMap.__new__(probe.RegisterMap)
        r.base = probe.PROBE_BASE
        r.trace = trace
        r.memory = bytearray(4096)
        r.address = probe.ctypes.addressof(probe.ctypes.c_char.from_buffer(r.memory))
        r.io = self.native
        return r

    def test_mmio_trace_read_and_write(self):
        r = self.register_map(trace=True)
        out = io.StringIO()
        with contextlib.redirect_stderr(out):
            r.write32(0xC, 3)
            self.assertEqual(r.read32(0xC), 3)
        self.assertEqual(out.getvalue().splitlines(), [
            'MMIO write32 begin address=0xb102000c value=0x00000003',
            'MMIO write32 done address=0xb102000c value=0x00000003',
            'MMIO read32 begin address=0xb102000c',
            'MMIO read32 done address=0xb102000c value=0x00000003'])

    def test_mmio_trace_precedes_failed_access(self):
        r = self.register_map(trace=True)
        out = io.StringIO()
        def fail(*args):
            self.assertEqual(out.getvalue(), 'MMIO read32 begin address=0xb1020000\n')
            raise OSError('simulated bus failure')
        with contextlib.redirect_stderr(out), patch.object(r.io, 'read32', side_effect=fail):
            with self.assertRaises(OSError):
                r.read32(0)
        self.assertNotIn('done', out.getvalue())

    def test_mmio_trace_disabled_and_bounds_preserved(self):
        r = self.register_map(trace=False)
        out = io.StringIO()
        with contextlib.redirect_stderr(out):
            self.assertEqual(r.read32(0), 0)
            with self.assertRaises(ValueError):
                r.read32(1)
            with self.assertRaises(ValueError):
                r.read32(4096)
            with self.assertRaises(ValueError):
                r.write32(0, 3)
            with self.assertRaises(ValueError):
                r.write32(0xC, 4)
        self.assertEqual(out.getvalue(), '')

    def test_legacy_pack_into_zeroes_before_value_conversion(self):
        # Ordinary RAM only: demonstrates why buffer packing is not MMIO.
        # CPython clears the destination before converting/packing the value.
        memory = bytearray(b'\xa5' * 4)
        observed = []
        class Command:
            def __index__(self):
                observed.append(bytes(memory))
                return 3
        struct.pack_into('<I', memory, 0, Command())
        self.assertEqual(observed, [b'\x00' * 4])
        self.assertEqual(memory, b'\x03\x00\x00\x00')

    def test_native_store_preserves_adjacent_bytes(self):
        r = self.register_map(trace=False)
        r.memory[:] = b'\xa5' * 4096
        r.write32(0xC, 3)
        self.assertEqual(r.memory[0xC:0x10], b'\x03\x00\x00\x00')
        self.assertEqual(r.memory[:0xC], b'\xa5' * 0xC)
        self.assertEqual(r.memory[0x10:], b'\xa5' * (4096 - 0x10))
        self.assertEqual(r.read32(0xC), 3)

    def test_missing_native_helper_has_no_fallback(self):
        with self.assertRaises(OSError):
            probe.NativeMMIO(Path(self.temp.name) / 'missing.so')

    def test_no_python_buffer_pack_mmio(self):
        source = (root / 'src/tests/sc6f0-axis-probe-read.py').read_text()
        self.assertNotIn('struct.pack_into(', source)
        self.assertNotIn('struct.unpack_from(', source)

    def test_read_only(self):
        r = Fake()
        sample = probe.capture(r)
        self.assertEqual(len(sample['points']), 5)
        self.assertEqual(r.writes, [])

    def test_command_bounded(self):
        r = Fake()
        probe.capture(r, request=True, clear=True)
        self.assertEqual(r.writes, [(0xC, 3)])

    def test_busy_rejected(self):
        r = Fake(); r.values[0x14] = 3
        with self.assertRaises(RuntimeError):
            probe.capture(r, request=True)
        self.assertEqual(r.writes, [])

    def test_partial_masks_old_banks(self):
        r = Fake(mask=1); r.values[0x14] = 3
        sample = probe.capture(r)
        self.assertTrue(sample['partial'])
        self.assertIsNone(sample['points']['cdc_out'])

    def test_unknown_abi_no_command(self):
        r = Fake(); r.values[4] = 0
        with self.assertRaises(RuntimeError):
            probe.capture(r, request=True)
        self.assertEqual(r.writes, [])

    def test_downstream_fpga_and_layout(self):
        r = Fake(); r.values[0] = 0x26102101; r.values[4] = 0x53444952
        self.assertEqual(probe.validate_fpga(r), 0x26102101)
        r = Fake(); r.values[4] = 0x00030000
        sample = probe.capture(r, expected_fpga=0x26102101)
        self.assertEqual(sample['abi'], '3.0')
        self.assertEqual(sample['fpga'], '0x26102101')
        self.assertEqual(tuple(sample['points']), ('dante_out', 'mixer_overlay_in', 'mixer_out', 'csc_out', 'packer_out'))
        self.assertEqual(sample['point_data_width_bits']['packer_out'], 64)
        self.assertNotIn('vproc_in', sample['points'])

    def test_crossed_fpga_abi_no_command(self):
        for fpga, abi in ((0x26102101, 0x00020000), (0x26102005, 0x00030000)):
            r = Fake(); r.values[4] = abi
            with self.assertRaisesRegex(RuntimeError, 'mismatch'):
                probe.capture(r, request=True, expected_fpga=fpga)
            self.assertEqual(r.writes, [])

    def test_old_fpga_rejected(self):
        r = Fake(); r.values[0] = 0x26101501; r.values[4] = 0x53444952
        with self.assertRaises(RuntimeError):
            probe.validate_fpga(r)

    def test_fpga_identity(self):
        r = Fake(); r.values[0] = probe.FPGA; r.values[4] = 0x53444952
        self.assertEqual(probe.validate_fpga(r), probe.FPGA)

    def test_64bit_and_uninitialized_minimum(self):
        words = [0] * 64
        words[2], words[3] = 7, 2
        words[17] = 0xFFFFFFFF
        words[31] = words[32] = 0xFFFFFFFF
        words[46] = 9
        sample = probe.decode_point(words)
        self.assertEqual(sample['accepted_beats'], (2 << 32) + 7)
        self.assertIsNone(sample['line_beats']['min'])
        self.assertIsNone(sample['sof_period_ref_ticks']['min'])
        self.assertEqual(sample['period_sample_overrun'], 9)
        self.assertFalse(sample['compact_encoding_valid'])
        self.assertFalse(sample['measurement_complete'])

    def test_overflow_is_not_complete(self):
        r = Fake(); r.values[0x1bc] = 1 | (1 << 15)
        sample = probe.capture(r)
        self.assertFalse(sample['measurement_complete'])
        self.assertEqual(sample['points']['dante_out']['overflow_fields'], ['window_cycles', 'period_range'])

    def test_period_pending_not_complete(self):
        r = Fake(); r.values[0x100] = 64
        self.assertFalse(probe.capture(r)['measurement_complete'])

    def test_features_required(self):
        r = Fake(); r.values[0x30] = 0
        with self.assertRaises(RuntimeError):
            probe.capture(r, request=True)
        self.assertEqual(r.writes, [])

    def test_missing_statistics_are_not_zero(self):
        sample = probe.capture(Fake())['points']['dante_out']
        self.assertIsNone(sample['line_beats']['max'])
        self.assertIsNone(sample['stall_max'])
        self.assertIsNone(sample['invalid_run'])

    def test_clear_each_window(self):
        r = Fake()
        for _ in range(3):
            self.assertTrue(probe.capture(r, request=True, clear=True)['measurement_complete'])
        self.assertEqual(r.writes, [(0xC, 3)] * 3)

    def test_no_snapshot_rejected(self):
        r = Fake(); r.values[0x10] = 0
        with self.assertRaises(RuntimeError):
            probe.capture(r)


if __name__ == '__main__':
    unittest.main()
