#!/usr/bin/env python3
"""Host-only fake MMIO tests. Never opens /dev/mem or connects to hardware."""
import importlib.util
from pathlib import Path
import unittest

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
