#!/usr/bin/env python3
"""Read-only SDIR candidate05 4K60 control-state check; NOT media acceptance.

Contract: 12G/8 data streams, integer60, YUV42210 output, SDR ST3520x0100CBCE.
No commands, reset, modeset or control writes. A good snapshot does not prove
persistent resume/coldboot behavior or validate the external picture.
"""
import importlib.util
import json
import os
from pathlib import Path

spec = importlib.util.spec_from_file_location('probe', Path(__file__).with_name('sc6f0-axis-probe-read.py'))
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def inspect(sdi, csc):
    raw = {
        'reset_control': sdi.read32(0),
        'module_control': sdi.read32(4),
        'global_irq_enable': sdi.read32(0xC),
        'irq_mask': sdi.read32(0x14),
        'st352': [sdi.read32(0x1C + 4 * i) for i in range(4)],
        'videoout_sticky_history': sdi.read32(0x6C),
        'csc_control': csc.read32(0),
    }
    module = raw['module_control']
    decoded = {'sdi_mode': (module >> 4) & 7,
               'is_frac': (module >> 7) & 1,
               'mux_pattern': (module >> 8) & 7}
    errors = []
    if (decoded['sdi_mode'], decoded['is_frac'], decoded['mux_pattern']) != (5, 0, 2):
        errors.append('Not configured for integer60 12G/8-stream mux')
    if module & ((1 << 21) | (1 << 22)):
        errors.append('Unexpected 420/444 output flag')
    if raw['reset_control'] & 0x301 != 0x301:
        errors.append('SDI/bridge/AXI4S video enable missing')
    if raw['global_irq_enable'] != 1 or raw['irq_mask'] != 0x607:
        errors.append('Interrupt enable/mask not restored')
    if raw['st352'] != [0x0100CBCE] * 4:
        errors.append('Four active ST352 payloads do not match 4K60 SDR contract')
    if raw['csc_control'] & 0x81 != 0x81:
        errors.append('Output CSC AP_START/auto-restart not set')
    return {'control_state_ok': not errors, 'errors': errors, 'raw': raw,
            'decoded': decoded,
            'videoout_history_note': 'Sticky transitions, not current-lock proof',
            'media_acceptance': 'Requires external analyzer and separate stream checks'}


def main():
    fd = os.open('/dev/mem', os.O_RDWR | os.O_SYNC)
    maps = []
    try:
        env = probe.RegisterMap(fd, probe.ENV_BASE)
        maps.append(env)
        probe.validate_fpga(env)  # Guard before touching SDI/CSC mapped blocks.
        sdi = probe.RegisterMap(fd, 0xB00C0000)
        maps.append(sdi)
        csc = probe.RegisterMap(fd, 0xB00B0000)
        maps.append(csc)
        result = inspect(sdi, csc)
        result['fpga'] = '0x26102005/SDIR'
        print(json.dumps(result, sort_keys=True), flush=True)
        return 0 if result['control_state_ok'] else 2
    finally:
        for item in reversed(maps):
            item.close()
        os.close(fd)


if __name__ == '__main__':
    raise SystemExit(main())
