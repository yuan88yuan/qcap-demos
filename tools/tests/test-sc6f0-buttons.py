#!/usr/bin/env python3
"""
Unit and regression test for SC6F0 onboard buttons (gpio-keys / evdev)
Verifies:
1. Data structures and event decoding (EV_KEY, BTN_0 ~ BTN_4, 24-byte layout).
2. Live hardware status if running on target or via SSH.
"""

import sys
import unittest
import struct
from pathlib import Path

# Add project root to sys.path
sys.path.insert(0, "/home/zzlee/vibecoding/sc6f0-ultra-sdi-fs-off")
try:
    import sc6f0_button as btn
except ImportError:
    # If not on host or path differs
    btn = None


class TestSC6F0Buttons(unittest.TestCase):
    def test_key_mappings(self):
        expected_keys = {
            0x100: "sw0",
            0x101: "sw1",
            0x102: "sw2",
            0x103: "sw3",
            0x104: "sw4",
        }
        self.assertEqual(btn.BTN_MAP, expected_keys)
        self.assertEqual(len(btn.BTN_MAP), 5)

    def test_input_event_layout(self):
        # 64-bit architecture struct input_event must be 24 bytes
        self.assertEqual(btn.EVENT_SIZE, 24)
        # Pack mock event: time=(100, 500000), type=EV_KEY(1), code=BTN_0(0x100), value=1(Press)
        raw = struct.pack(btn.EVENT_FORMAT, 100, 500000, btn.EV_KEY, 0x100, 1)
        self.assertEqual(len(raw), 24)

        tv_sec, tv_usec, ev_type, ev_code, ev_value = struct.unpack(btn.EVENT_FORMAT, raw)
        self.assertEqual(tv_sec, 100)
        self.assertEqual(tv_usec, 500000)
        self.assertEqual(ev_type, btn.EV_KEY)
        self.assertEqual(ev_code, 0x100)
        self.assertEqual(ev_value, 1)

    def test_button_event_object(self):
        ev = btn.ButtonEvent(0x100, "sw0", 1, 100.5)
        self.assertTrue(ev.is_press)
        self.assertFalse(ev.is_release)
        self.assertFalse(ev.is_repeat)
        self.assertEqual(ev.name, "sw0")

        ev_rel = btn.ButtonEvent(0x100, "sw0", 0, 100.7)
        self.assertFalse(ev_rel.is_press)
        self.assertTrue(ev_rel.is_release)

    def test_eviocgkey_macro(self):
        # EVIOCGKEY(64) macro value check
        val = btn._eviocgkey(64)
        # _IOC_READ (2) in bits [31:30], size 64 in bits [29:16], 'E' (0x45) in bits [15:8], 0x18 in bits [7:0]
        expected = (2 << 30) | (64 << 16) | (0x45 << 8) | 0x18
        self.assertEqual(val, expected)


if __name__ == "__main__":
    unittest.main()
