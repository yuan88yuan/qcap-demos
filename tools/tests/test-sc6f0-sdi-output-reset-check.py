#!/usr/bin/env python3
"""Host guard tests only; no board/MMIO access."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('reset_check', ROOT / 'src/tests/sc6f0-sdi-output-reset-check.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class ResetCheckTests(unittest.TestCase):
    def setUp(self):
        self.s = dict(boot='expected-boot', product='SC6F0 SDI Dante RX',
                      release='RX-PL-2026.10.15.01-SDI', fpga_compatible='0x26101203',
                      tpg_compatible='yuan,v-tpg', reset_gpio=[43, 79, 1],
                      gpio_compatible='xlnx,zynqmp-gpio-1.0', owners_rc=1,
                      process_rc=1, crtc_inactive=True, audio_off=True, csc=0, sdi=0)

    def check(self, s):
        m.check_snapshot(s, 'expected-boot', 'RX-PL-2026.10.15.01-SDI')

    def test_stalled_csc_allowed_but_no_output(self):
        self.check(self.s)

    def test_all_unsafe_states_refused(self):
        for field, bad in [('boot', 'other'), ('product', 'SC6F0 Dante TX'),
                           ('release', 'other'), ('fpga_compatible', 'other'),
                           ('tpg_compatible', 'other'), ('reset_gpio', [43, 80, 1]),
                           ('reset_gpio', [43, 79, 0]), ('gpio_compatible', 'other'),
                           ('owners_rc', 0), ('owners_rc', 2), ('process_rc', 0),
                           ('process_rc', 2), ('crtc_inactive', False),
                           ('audio_off', False), ('sdi', 0x301)]:
            with self.subTest(field=field, bad=bad), self.assertRaises(RuntimeError):
                self.check(dict(self.s, **{field: bad}))

    def invoke(self, *args):
        with patch('sys.argv', ['reset-check', '--boot-id', 'expected-boot',
                               '--release', self.s['release'], *args]):
            return m.main()

    def test_default_never_writes(self):
        with patch.object(m, 'snapshot', return_value=self.s), patch.object(Path, 'write_text') as write:
            self.assertEqual(self.invoke(), 0)
            write.assert_not_called()

    def test_pulse_requires_second_preflight(self):
        unsafe = dict(self.s, owners_rc=0)
        with patch.object(m, 'snapshot', side_effect=[self.s, unsafe]), patch.object(Path, 'write_text') as write:
            with self.assertRaises(RuntimeError):
                self.invoke('--pulse')
            write.assert_not_called()

    def test_pulse_once_and_verify_idle(self):
        after = dict(self.s, csc=4)
        with patch.object(m, 'snapshot', side_effect=[self.s, self.s, after]), \
             patch.object(m.time, 'sleep'), patch.object(Path, 'write_text') as write:
            self.assertEqual(self.invoke('--pulse'), 0)
            write.assert_called_once_with('1\n')

    def test_pulse_failure_is_failure_not_retried(self):
        with patch.object(m, 'snapshot', return_value=self.s), \
             patch.object(m.time, 'sleep'), patch.object(Path, 'write_text') as write:
            self.assertEqual(self.invoke('--pulse'), 1)
            write.assert_called_once()

    def test_boot_change_after_pulse_refused(self):
        with patch.object(m, 'snapshot', side_effect=[self.s, self.s, dict(self.s, boot='changed')]), \
             patch.object(m.time, 'sleep'), patch.object(Path, 'write_text'):
            with self.assertRaises(RuntimeError):
                self.invoke('--pulse')


if __name__ == '__main__':
    unittest.main()
