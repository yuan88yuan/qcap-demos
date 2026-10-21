#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('control',Path(__file__).resolve().parents[2]/'src/tests/sc6f0-sdi-control-read.py')
control=importlib.util.module_from_spec(spec)
spec.loader.exec_module(control)

class Registers:
    def __init__(self,values): self.values=values
    def read32(self,offset): return self.values[offset]
    def write32(self,*args): raise AssertionError('Control audit must never write MMIO')

class TestControlState(unittest.TestCase):
    def setUp(self):
        self.sdi=Registers({0:0x301,4:0x15f250,12:1,20:0x607,108:0xc0186,
                            **{28+4*i:0x0100cbce for i in range(4)}})
        self.csc=Registers({0:0x81})
    def check(self): return control.inspect(self.sdi,self.csc)['control_state_ok']
    def test_good(self): self.assertTrue(self.check())
    def test_zero_payload(self):
        self.sdi.values[28]=0; self.assertFalse(self.check())
    def test_masked_global_irq(self):
        self.sdi.values[12]=0; self.assertFalse(self.check())
    def test_wrong_irq_mask(self):
        self.sdi.values[20]=0; self.assertFalse(self.check())
    def test_fractional_not_this_contract(self):
        self.sdi.values[4]|=128; self.assertFalse(self.check())
    def test_wrong_mux(self):
        self.sdi.values[4]^=256; self.assertFalse(self.check())
    def test_stopped_csc(self):
        self.csc.values[0]=4; self.assertFalse(self.check())
    def test_video_not_enabled(self):
        self.sdi.values[0]&=~256; self.assertFalse(self.check())
    def test_history_is_not_acceptance(self):
        self.sdi.values[108]=0xb00c006c
        result=control.inspect(self.sdi,self.csc)
        self.assertTrue(result['control_state_ok'])
        self.assertIn('external analyzer',result['media_acceptance'])

if __name__=='__main__': unittest.main()
