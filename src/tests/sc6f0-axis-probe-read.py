#!/usr/bin/env python3
"""SC6F0 SDIR passive AXIS probe reader. No video/audio/reset control writes.

Default reads the published snapshot. --snapshot issues a diagnostic capture;
--clear clears ONLY probe statistics after every capture (use 250ms windows).
Requires FPGA 0x26102005 and compact probe ABI 2.0. Overflow is never valid data.
Requires adjacent libsc6f0-mmio32.so: device access uses single volatile 32-bit
loads/stores, never struct.pack_into (which may zero/fill a buffer first).
"""
import argparse
import ctypes
import fcntl
import json
import mmap
import os
from pathlib import Path
import sys
import time

FPGA = 0x26102005
ENV_BASE = 0xB1010000
PROBE_BASE = 0xB1020000
MAGIC = 0x41585052
ABI = 0x00020000
OVERFLOW_NAMES = ('window_cycles', 'reserved1', 'reserved2', 'reserved3', 'reserved4', 'reserved5', 'reserved6',
                  'reserved7', 'reserved8', 'reserved9', 'epoch',
                  'line_beats', 'frame_lines', 'frame_beats', 'runs',
                  'period_range', 'period_overrun_count')
POINT_NAMES = ('dante_out', 'cdc_out', 'vproc_in', 'vproc_out', 'mixer_overlay_in')


class NativeMMIO:
    def __init__(self, path=None):
        path = path or Path(__file__).resolve().with_name('libsc6f0-mmio32.so')
        self.library = ctypes.CDLL(str(path))  # Fail closed; no buffer-packing fallback.
        self.library.sc6f0_mmio32_abi.argtypes = []
        self.library.sc6f0_mmio32_abi.restype = ctypes.c_uint32
        if self.library.sc6f0_mmio32_abi() != 1:
            raise RuntimeError('Unknown native MMIO helper ABI')
        self.library.sc6f0_mmio32_read.argtypes = [ctypes.c_void_p]
        self.library.sc6f0_mmio32_read.restype = ctypes.c_uint32
        self.library.sc6f0_mmio32_write.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        self.library.sc6f0_mmio32_write.restype = None

    def read32(self, address):
        return self.library.sc6f0_mmio32_read(address)

    def write32(self, address, value):
        self.library.sc6f0_mmio32_write(address, value)


class RegisterMap:
    def __init__(self, fd, base, trace=False):
        self.base = base
        self.trace = trace
        self.io = NativeMMIO()
        self.memory = mmap.mmap(fd, 4096, mmap.MAP_SHARED,
                                mmap.PROT_READ | mmap.PROT_WRITE, offset=base)
        self.address = ctypes.addressof(ctypes.c_char.from_buffer(self.memory))

    def read32(self, offset):
        if offset % 4 or not 0 <= offset < 4096:
            raise ValueError('Invalid probe register offset')
        self.trace_access('read32 begin', offset)
        value = self.io.read32(self.address + offset)
        self.trace_access('read32 done', offset, value)
        return value

    def trace_access(self, operation, offset, value=None):
        # Flush BEFORE the potentially blocking bus access. A Python timeout
        # cannot cancel a CPU load/store stalled by a nonresponding AXI slave.
        if self.trace:
            detail = '' if value is None else f' value={value:#010x}'
            print(f'MMIO {operation} address={self.base + offset:#010x}{detail}',
                  file=sys.stderr, flush=True)

    def write32(self, offset, value):
        if offset != 0xC or value not in (1, 3):
            raise ValueError('Only probe SNAPSHOT/CLEAR_AFTER_SNAPSHOT may be written')
        self.trace_access('write32 begin', offset, value)
        self.io.write32(self.address + offset, value)
        self.trace_access('write32 done', offset, value)

    def close(self):
        self.memory.close()


def validate_fpga(env):
    version, platform = env.read32(0), env.read32(4)
    if version != FPGA or platform != 0x53444952:
        raise RuntimeError(f'Refusing probe access: FPGA={version:#010x}, platform={platform:#010x}; '
                           f'requires {FPGA:#010x}/SDIR')
    return version


def validate_probe(regs):
    if (regs.read32(0), regs.read32(4), regs.read32(8)) != (MAGIC, ABI, 5):
        raise RuntimeError('Unknown probe identity/ABI/point count; no command issued')
    if (regs.read32(0x30), regs.read32(0x34)) != (3, 28):
        raise RuntimeError('Unknown compact probe features; no command issued')


def decode_point(words):
    def u64(index):
        return words[index] | words[index + 1] << 32

    overflow = words[47]
    encoding_valid = all(words[i] == 0 for i in (3, 5, 7, 9, 11, 13, 30, 34, 36))
    encoding_valid &= all(words[i] < (1 << 28) for i in (2, 4, 6, 8, 10, 12, 14, 15, 35, 37, 46))
    return {
        'overflow_flags': overflow,
        'overflow_fields': [name for bit, name in enumerate(OVERFLOW_NAMES) if overflow & (1 << bit)],
        'compact_encoding_valid': encoding_valid,
        'measurement_complete': encoding_valid and overflow == 0 and words[46] == 0 and not words[0] & 64,
        'flags': words[0], 'epoch': words[1],
        'accepted_beats': u64(2), 'valid_cycles': u64(4),
        'stall_cycles': u64(6), 'invalid_cycles': u64(8),
        'sof': u64(10), 'eol': u64(12), 'geometry_error_beats': words[14],
        'stall_violation_cycles': words[15],
        'line_beats': {'last': words[16], 'min': None, 'max': None},
        'frame_lines': {'last': words[19], 'min': None, 'max': None},
        'frame_beats': {'last': words[22], 'min': None, 'max': None},
        'stall_run': None, 'stall_max': None,
        'no_transfer_run': None, 'no_transfer_max': None,
        'sof_period_ref_ticks': {'last': u64(29), 'min': None, 'max': None},
        'local_cycles': u64(35), 'observed_reset_assertions': words[37],
        'snapshot_ref_ticks': u64(38),
        'expected_line_beats': words[40], 'expected_frame_lines': words[41],
        'invalid_run': None, 'invalid_max': None,
        'unsupported_statistics': ['minima', 'maxima', 'run_lengths'],
        'sof_age_ref_ticks': u64(44), 'period_sample_overrun': words[46],
        'period_update_pending': bool(words[0] & 64),
    }


def capture(regs, request=False, clear=False, timeout=1.0):
    validate_probe(regs)
    if clear and not request:
        raise ValueError('--clear requires --snapshot')
    prior_seq = regs.read32(0x10)
    if request:
        if regs.read32(0x14) & 1:
            raise RuntimeError('Previous capture still BUSY; refusing to cancel/re-toggle it')
        regs.write32(0xC, 3 if clear else 1)
        # A CSR read after command also orders the MMIO transaction.
        deadline = time.monotonic() + timeout
        while True:
            seq = regs.read32(0x10)
            status = regs.read32(0x14)
            if seq != prior_seq and (not status & 1 or status & 2):
                break
            if time.monotonic() >= deadline:
                raise TimeoutError('Software capture deadline; no video/reset action taken')
            time.sleep(0.001)
        if seq != (prior_seq + 1) & 0xFFFFFFFF:
            raise RuntimeError('Concurrent capture detected')
    sequence = regs.read32(0x10)
    status = regs.read32(0x14)
    mask = regs.read32(0x18)
    if sequence == 0:
        raise RuntimeError('No published snapshot yet; explicitly request --snapshot')
    points = {}
    for point, name in enumerate(POINT_NAMES):
        if mask & (1 << point):
            words = [regs.read32(0x100 + point * 0x100 + i * 4) for i in range(64)]
            points[name] = decode_point(words)
        else:
            points[name] = None  # Old bank contents must not be reported as current.
    if regs.read32(0x10) != sequence:
        raise RuntimeError('Snapshot sequence changed during read; discard torn sample')
    complete = mask == 31 and all(point['measurement_complete'] for point in points.values() if point is not None)
    return {'fpga': f'{FPGA:#010x}', 'abi': '2.0', 'sequence': sequence,
            'busy': bool(status & 1), 'timed_out': bool(status & 2),
            'valid_mask': mask, 'partial': mask != 31,
            'measurement_complete': complete,
            'points': points, 'clear_after_capture_requested': clear}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--trace-mmio', action='store_true',
                        help='flush each MMIO operation to stderr before/after access; not a bus timeout')
    parser.add_argument('--snapshot', action='store_true')
    parser.add_argument('--clear', action='store_true')
    parser.add_argument('--count', type=int, default=1)
    parser.add_argument('--interval', type=float, default=0.25)
    args = parser.parse_args()
    if args.count < 1 or args.interval < 0 or (args.clear and not args.snapshot):
        parser.error('Invalid count/interval; --clear requires --snapshot')
    # Serialize this reader. External manual register writers are still detected
    # by sequence checks, but must not be run concurrently.
    lock_path = Path('/run/lock/sc6f0-axis-probe.lock')
    with lock_path.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        fd = os.open('/dev/mem', os.O_RDWR | os.O_SYNC)
        env = None
        regs = None
        try:
            env = RegisterMap(fd, ENV_BASE, trace=args.trace_mmio)
            validate_fpga(env)  # BEFORE mapping/touching B1020000 on old hardware.
            regs = RegisterMap(fd, PROBE_BASE, trace=args.trace_mmio)
            result = 0
            for i in range(args.count):
                sample = capture(regs, args.snapshot, args.clear)
                print(json.dumps(sample, sort_keys=True), flush=True)
                if not sample['measurement_complete']:
                    result = 2
                if i + 1 < args.count:
                    time.sleep(args.interval)
        finally:
            if regs is not None:
                regs.close()
            if env is not None:
                env.close()
            os.close(fd)
    return result


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, TimeoutError) as exc:
        raise SystemExit(str(exc))
