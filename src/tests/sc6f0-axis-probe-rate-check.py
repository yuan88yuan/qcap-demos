#!/usr/bin/env python3
"""Read-only ENV DCO/reference-rate check. No snapshot, clear or video writes.

Clock pin mapping is candidate05-specific: ENV ticks1=Dante video DCO,
ticks2=fixed audio clock. Host monotonic timing is an estimate, not a calibrated
frequency instrument; ratios distinguish the integer/fractional clock families.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import time

spec = importlib.util.spec_from_file_location('probe', Path(__file__).with_name('sc6f0-axis-probe-read.py'))
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def read_reference(regs):
    for _ in range(5):
        hi = regs.read32(0x24)
        lo = regs.read32(0x20)
        if regs.read32(0x24) == hi:
            return (hi << 32) | lo
    raise RuntimeError('Reference changed repeatedly; no coherent read')


def observe(env, regs):
    readings = {}
    for index in range(4):
        before = time.monotonic_ns()
        ref0 = read_reference(regs)
        value = env.read32(0xC + index * 4)
        ref1 = read_reference(regs)
        after = time.monotonic_ns()
        readings[str(index)] = {'value': value, 'reference_midpoint': (ref0 + ref1) / 2,
                                'monotonic_midpoint_ns': (before + after) / 2,
                                'read_duration_ns': after - before}
    return readings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--interval', type=float, default=3.0)
    parser.add_argument('--count', type=int, default=3)
    args = parser.parse_args()
    if not 1 <= args.interval <= 10 or not 1 <= args.count <= 10:
        parser.error('Use interval 1..10 seconds and count 1..10')
    fd = os.open('/dev/mem', os.O_RDWR | os.O_SYNC)
    env = regs = None
    try:
        env = probe.RegisterMap(fd, probe.ENV_BASE)
        probe.validate_fpga(env)
        regs = probe.RegisterMap(fd, probe.PROBE_BASE)
        probe.validate_probe(regs)
        for trial in range(args.count):
            start = observe(env, regs)
            time.sleep(args.interval)
            end = observe(env, regs)
            rates = {}
            for i in start:
                delta = (end[i]['value'] - start[i]['value']) & 0xFFFFFFFF
                elapsed = (end[i]['monotonic_midpoint_ns'] - start[i]['monotonic_midpoint_ns']) / 1e9
                ref = end[i]['reference_midpoint'] - start[i]['reference_midpoint']
                rates[i] = {'estimated_hz_monotonic': delta / elapsed,
                            'reference_hz_monotonic': ref / elapsed,
                            'clock_to_reference_ratio': delta / ref,
                            'elapsed_seconds': elapsed, 'counter_delta': delta}
            ratio = rates['1']['clock_to_reference_ratio'] / rates['2']['clock_to_reference_ratio']
            print(json.dumps({'trial': trial, 'rates': rates,
                              'video_dco_to_fixed_audio_ratio': ratio,
                              'nominal_integer_ratio': 148500000 / 24576000,
                              'nominal_fractional_ratio': (148500000 / 1.001) / 24576000,
                              'start': start, 'end': end}), flush=True)
    finally:
        if regs is not None:
            regs.close()
        if env is not None:
            env.close()
        os.close(fd)


if __name__ == '__main__':
    main()
