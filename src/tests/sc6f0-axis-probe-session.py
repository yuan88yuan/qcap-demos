#!/usr/bin/env python3
"""Bounded ON/OFF-labelled AXIS capture; NEVER toggles FS or resets video.

Uses the existing guarded native32 reader. SNAPSHOT/CLEAR writes affect only
probe statistics. Keep the operator's FS setting fixed throughout each run.
Labels are operator declarations, NOT independently verified FS configuration.
Offline --analyze never imports the MMIO reader or accesses hardware.
"""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys

POINTS = ('dante_out', 'cdc_out', 'vproc_in', 'vproc_out', 'mixer_overlay_in')
LAYOUTS = {
    ('0x26102005', '2.0'): POINTS,
    ('0x26102101', '3.0'): ('dante_out', 'mixer_overlay_in', 'mixer_out', 'csc_out', 'packer_out'),
}


def bounds(values):
    return {'min': min(values), 'median': statistics.median(values), 'max': max(values)} if values else None


def summarize(samples, width=3840, height=2160):
    # The first capture ends an unbounded OLD statistics window; never use it.
    fresh = samples[1:]
    identity = (samples[0].get('fpga'), samples[0].get('abi')) if samples else ('0x26102005', '2.0')
    if identity not in LAYOUTS:
        raise ValueError(f'Unknown FPGA/ABI layout: {identity}')
    names = LAYOUTS[identity]
    downstream = identity[1] == '3.0'
    expected = {'line_beats': width // 2, 'frame_lines': height,
                'frame_beats': width // 2 * height}
    result = {'captures': len(samples), 'discarded_initial_captures': min(1, len(samples)),
              'fresh_windows': len(fresh), 'expected_geometry': expected,
              'fpga': identity[0], 'abi': identity[1], 'point_order': names,
              'invalid_capture_windows': 0, 'points': {},
              'limitations': [('ABI3 taps end at SDI VIDEO_IN; no direct serializer/GT observation.' if downstream
                               else 'All five ABI2 taps are upstream of the mixer output.'),
                             'Healthy counters do not prove correct picture or valid SDI output.',
                             'Nonzero stall cycles are backpressure, not a protocol violation.',
                             'SOF period is in reference ticks; no assumed 60Hz pass criterion.',
                             'Compact ABI2/3 does not measure within-window minima/maxima or maximum gap lengths.']}
    prior = samples[0]['sequence'] if samples else None
    eligibility = []
    for sample in fresh:
        invalid = (prior is None or sample['sequence'] != ((prior + 1) & 0xffffffff)
                or sample.get('busy') or sample.get('timed_out') or sample.get('partial')
                or sample.get('valid_mask') != 31 or not sample.get('measurement_complete')
                or (sample.get('fpga'), sample.get('abi')) != identity
                or set(sample.get('points', {})) != set(names)
                or not sample.get('clear_after_capture_requested'))
        eligibility.append(not invalid)
        if invalid:
            result['invalid_capture_windows'] += 1
        prior = sample['sequence']
    for name in names:
        available = [s.get('points', {}).get(name) if eligible else None
                     for s, eligible in zip(fresh, eligibility)]
        valid = [p for p in available if p and p.get('measurement_complete')
                 and p.get('compact_encoding_valid') and not p.get('overflow_flags')
                 and not p.get('period_sample_overrun') and not p.get('period_update_pending')]
        counts = {'missing_or_incomplete_windows': len(fresh) - len(valid),
                  'no_transfer_windows': sum(p['accepted_beats'] == 0 for p in valid),
                  'no_sof_windows': sum(p['sof'] == 0 for p in valid),
                  'geometry_mismatch_windows': sum(any(p[k]['last'] != v for k, v in expected.items()) for p in valid),
                  'configured_geometry_mismatch_windows': sum(
                      p['expected_line_beats'] != expected['line_beats'] or
                      p['expected_frame_lines'] != height for p in valid)}
        totals = {k: sum(p[k] for p in valid) for k in
                  ('accepted_beats', 'sof', 'eol', 'geometry_error_beats',
                   'stall_violation_cycles', 'observed_reset_assertions')}
        issue = (not valid or any(counts.values()) or any(totals[k] for k in
                 ('geometry_error_beats', 'stall_violation_cycles', 'observed_reset_assertions')))
        result['points'][name] = {
            'status': 'ATTENTION' if issue else 'NO_COUNTER_ANOMALY',
            'complete_windows': len(valid), **counts, 'totals': totals,
            'accepted_beats_per_window': bounds([p['accepted_beats'] for p in valid]),
            'sof_per_window': bounds([p['sof'] for p in valid]),
            'last_sof_period_ref_ticks_across_windows': bounds(
                [p['sof_period_ref_ticks']['last'] for p in valid if p['sof'] and p['sof_period_ref_ticks']['last'] > 0]),
            'backpressure_fraction_across_windows': bounds(
                [p['stall_cycles'] / p['local_cycles'] for p in valid if p['local_cycles']]),
            'transfer_fraction_across_windows': bounds(
                [p['accepted_beats'] / p['local_cycles'] for p in valid if p['local_cycles']]),
            'snapshot_level_counts': {
                'samples': sum('flags' in p for p in valid),
                'valid_low': sum(not (p['flags'] & 1) for p in valid if 'flags' in p),
                'valid_high_ready_low': sum((p['flags'] & 3) == 1 for p in valid if 'flags' in p),
                'valid_high_ready_high': sum((p['flags'] & 3) == 3 for p in valid if 'flags' in p),
                'stream_reset_asserted': sum(not (p['flags'] & 4) for p in valid if 'flags' in p),
            },
            'observed_line_beats': sorted({p['line_beats']['last'] for p in valid}),
            'observed_frame_lines': sorted({p['frame_lines']['last'] for p in valid}),
            'observed_frame_beats': sorted({p['frame_beats']['last'] for p in valid}),
        }
    # Reconstruct each accepted last-SOF timestamp from existing common-reference
    # snapshot/age fields. All pipeline taps share the same receiver/clock, so
    # no new FPGA subtractors/counters are needed. This is a modulo-period phase
    # observation, not image-content matching or absolute latency proof.
    if downstream:
        result['adjacent_pipeline_sof_phase_ref_ticks'] = {}
        for upstream, downstream_name in zip(names[1:-1], names[2:]):
            deltas = []
            for sample, eligible in zip(fresh, eligibility):
                if not eligible:
                    continue
                a = sample.get('points', {}).get(upstream)
                b = sample.get('points', {}).get(downstream_name)
                if not (sample.get('measurement_complete') and a and b and
                        all(p.get('measurement_complete') and p.get('compact_encoding_valid') and
                            not p.get('overflow_flags') and not p.get('period_sample_overrun') and
                            not p.get('period_update_pending') and p.get('sof') for p in (a, b))):
                    continue
                fields = ('snapshot_ref_ticks', 'sof_age_ref_ticks')
                if not all(k in p for p in (a, b) for k in fields):
                    continue
                period = a['sof_period_ref_ticks']['last']
                if not period or not b['sof_period_ref_ticks']['last']:
                    continue
                a_time = a['snapshot_ref_ticks'] - a['sof_age_ref_ticks']
                b_time = b['snapshot_ref_ticks'] - b['sof_age_ref_ticks']
                delta = (b_time - a_time) % period
                if delta > period / 2:
                    delta -= period
                deltas.append(delta)
            result['adjacent_pipeline_sof_phase_ref_ticks'][upstream + '->' + downstream_name] = {
                'samples': len(deltas), 'signed_modulo_upstream_period': bounds(deltas)}
    result['status'] = ('NO_COUNTER_ANOMALY' if len(fresh) >= 2 and
                        not result['invalid_capture_windows'] and
                        all(p['status'] == 'NO_COUNTER_ANOMALY' for p in result['points'].values())
                        else 'ATTENTION_OR_INCOMPLETE')
    return result


def read_samples(path):
    with Path(path).open() as source:
        return [json.loads(line) for line in source if line.strip()]


def write_json(path, value):
    with path.open('x') as output:
        json.dump(value, output, indent=2, allow_nan=False)
        output.write('\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--state', choices=('on', 'off'), help='operator-declared setting (not a setter)')
    ap.add_argument('--output', type=Path, help='NEW evidence directory; never overwrites')
    ap.add_argument('--reader-dir', type=Path, default=Path(__file__).resolve().parent,
                    help='directory containing reader and adjacent native32 .so')
    ap.add_argument('--count', type=int, default=40, help='fresh windows; one extra warmup capture')
    ap.add_argument('--interval', type=float, default=.25)
    ap.add_argument('--width', type=int, choices=(1920, 3840), default=3840)
    ap.add_argument('--height', type=int, choices=(1080, 2160), default=2160)
    ap.add_argument('--analyze', type=Path, help='offline raw.jsonl analysis only')
    args = ap.parse_args()
    if (args.width, args.height) not in ((1920, 1080), (3840, 2160)):
        ap.error('Use 1920x1080 or 3840x2160')
    if args.analyze:
        result = summarize(read_samples(args.analyze), args.width, args.height)
        print(json.dumps(result, indent=2, allow_nan=False))
        return 0 if result['status'] == 'NO_COUNTER_ANOMALY' else 2
    if not args.state or not args.output:
        ap.error('--state and --output are required for collection')
    if not 2 <= args.count <= 120 or not math.isfinite(args.interval) or not .1 <= args.interval <= .4:
        ap.error('Use count 2..120 and interval 0.1..0.4 seconds for bounded compact-counter windows')
    reader = args.reader_dir.resolve() / 'sc6f0-axis-probe-read.py'
    if not reader.is_file() or not reader.with_name('libsc6f0-mmio32.so').is_file():
        ap.error('Reader/native32 helper missing; specify --reader-dir (no MMIO fallback)')
    args.output.mkdir(parents=True, exist_ok=False)
    command = [sys.executable, str(reader), '--snapshot', '--clear', '--count',
               str(args.count + 1), '--interval', str(args.interval)]
    metadata = {'start_utc': datetime.now(timezone.utc).isoformat(),
                'operator_declared_fast_switching': args.state,
                'setting_independently_verified': False, 'command': command,
                'width': args.width, 'height': args.height,
                'policy': 'No FS, DRM, video/audio/reset writes; diagnostic snapshot/clear only.'}
    for name, path in [('release_id', '/etc/sc6f0-release-id'),
                       ('boot_id', '/proc/sys/kernel/random/boot_id')]:
        try:
            metadata[name] = Path(path).read_text().strip()
        except OSError as exc:
            metadata[name] = str(exc)
    write_json(args.output / 'metadata.json', metadata)
    print(f'Collecting FS={args.state} (operator label), {args.count} fresh windows. '
          'Keep FS/source unchanged; no CRTC recovery during capture.', flush=True)
    error = None
    rc = None
    with (args.output / 'raw.jsonl').open('x') as raw, (args.output / 'reader.stderr').open('x') as stderr:
        try:
            rc = subprocess.run(command, stdout=raw, stderr=stderr,
                                timeout=(args.count + 1) * (args.interval + 1) + 5).returncode
        except (OSError, subprocess.TimeoutExpired) as exc:
            error = str(exc)
    # A process timeout cannot guarantee cancellation of a CPU blocked in AXI MMIO.
    result = summarize(read_samples(args.output / 'raw.jsonl'), args.width, args.height)
    result.update({'operator_declared_fast_switching': args.state, 'reader_exit_code': rc,
                   'capture_error': error, 'end_utc': datetime.now(timezone.utc).isoformat()})
    if error or rc not in (0, 2) or result['fresh_windows'] != args.count:
        result['status'] = 'ATTENTION_OR_INCOMPLETE'
    write_json(args.output / 'summary.json', result)
    print(f"{result['status']}: {args.output / 'summary.json'}")
    for name, point in result['points'].items():
        print(f"  {name}: {point['status']}, complete={point['complete_windows']}, "
              f"beats={point['totals']['accepted_beats']}, SOF={point['totals']['sof']}, "
              f"geometry_errors={point['totals']['geometry_error_beats']}, "
              f"stall_violations={point['totals']['stall_violation_cycles']}, "
              f"resets={point['totals']['observed_reset_assertions']}")
    return 0 if result['status'] == 'NO_COUNTER_ANOMALY' else 2


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise SystemExit(f'Capture/analysis failed; retain raw evidence: {exc}')
