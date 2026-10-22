#!/usr/bin/env python3
"""Host-only ON/OFF session summarizer tests; no device access or compiler."""
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('session', ROOT / 'src/tests/sc6f0-axis-probe-session.py')
session = importlib.util.module_from_spec(spec)
spec.loader.exec_module(session)


def sample(sequence):
    point = dict(measurement_complete=True, compact_encoding_valid=True, overflow_flags=0,
                 period_sample_overrun=0, period_update_pending=False, accepted_beats=62000000,
                 local_cycles=75000000, stall_cycles=12000000, sof=15, eol=32000,
                 geometry_error_beats=0, stall_violation_cycles=0, observed_reset_assertions=0,
                 expected_line_beats=1920, expected_frame_lines=2160,
                 line_beats={'last': 1920}, frame_lines={'last': 2160},
                 frame_beats={'last': 4147200}, sof_period_ref_ticks={'last': 1666667})
    return dict(sequence=sequence, busy=False, timed_out=False, partial=False, valid_mask=31,
                fpga='0x26102005', abi='2.0', clear_after_capture_requested=True,
                measurement_complete=True, points={n: copy.deepcopy(point) for n in session.POINTS})


class Tests(unittest.TestCase):
    def records(self):
        return [sample(i) for i in (1, 2, 3)]

    def test_healthy_and_backpressure_not_error(self):
        result = session.summarize(self.records())
        self.assertEqual(result['status'], 'NO_COUNTER_ANOMALY')
        self.assertEqual(result['fresh_windows'], 2)
        self.assertEqual(result['points']['dante_out']['backpressure_fraction_across_windows']['median'], .16)

    def test_discard_old_overflow(self):
        records = self.records()
        records[0]['measurement_complete'] = False
        records[0]['points']['dante_out']['overflow_flags'] = 1
        self.assertEqual(session.summarize(records)['status'], 'NO_COUNTER_ANOMALY')

    def test_incomplete_and_missing_not_healthy(self):
        for value in (None, dict(sample(1)['points']['cdc_out'], measurement_complete=False),
                      dict(sample(1)['points']['cdc_out'], overflow_flags=1),
                      dict(sample(1)['points']['cdc_out'], period_update_pending=True)):
            with self.subTest(value=value):
                records = self.records()
                records[1]['points']['cdc_out'] = value
                self.assertEqual(session.summarize(records)['points']['cdc_out']['missing_or_incomplete_windows'], 1)
                self.assertNotEqual(session.summarize(records)['status'], 'NO_COUNTER_ANOMALY')

    def test_errors_stop_healthy_result(self):
        for field in ('geometry_error_beats', 'stall_violation_cycles', 'observed_reset_assertions'):
            records = self.records()
            records[1]['points']['vproc_out'][field] = 1
            self.assertNotEqual(session.summarize(records)['status'], 'NO_COUNTER_ANOMALY')

    def test_no_traffic_and_sof(self):
        records = self.records()
        records[1]['points']['dante_out'].update(accepted_beats=0, sof=0)
        point = session.summarize(records)['points']['dante_out']
        self.assertEqual(point['no_transfer_windows'], 1)
        self.assertEqual(point['no_sof_windows'], 1)

    def test_geometry(self):
        records = self.records()
        records[1]['points']['mixer_overlay_in']['frame_lines']['last'] = 1080
        self.assertEqual(session.summarize(records)['points']['mixer_overlay_in']['geometry_mismatch_windows'], 1)

    def test_fractional_not_assumed_error(self):
        records = self.records()
        for record in records:
            for point in record['points'].values():
                point['sof_period_ref_ticks']['last'] = 1668333
        self.assertEqual(session.summarize(records)['status'], 'NO_COUNTER_ANOMALY')

    def test_capture_integrity(self):
        for field, value in [('busy', True), ('timed_out', True), ('partial', True),
                             ('valid_mask', 1), ('abi', '1.0'), ('sequence', 5),
                             ('clear_after_capture_requested', False)]:
            records = self.records()
            records[1][field] = value
            self.assertNotEqual(session.summarize(records)['status'], 'NO_COUNTER_ANOMALY')

    def test_sequence_wrap(self):
        records = [sample(n) for n in (0xffffffff, 0, 1)]
        self.assertEqual(session.summarize(records)['status'], 'NO_COUNTER_ANOMALY')

    def test_short_or_empty_incomplete(self):
        for records in ([], [sample(1)], [sample(1), sample(2)]):
            self.assertNotEqual(session.summarize(records)['status'], 'NO_COUNTER_ANOMALY')

    def test_collect_command_and_output_no_hardware(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            (base / 'sc6f0-axis-probe-read.py').touch()
            (base / 'libsc6f0-mmio32.so').touch()
            out = base / 'evidence'
            def run(command, stdout, stderr, timeout):
                self.assertEqual(command[2:], ['--snapshot', '--clear', '--count', '3', '--interval', '0.25'])
                for record in self.records():
                    stdout.write(json.dumps(record) + '\n')
                return type('Result', (), {'returncode': 2})()  # Warmup overflow allowed.
            argv = ['session', '--state', 'off', '--output', str(out), '--count', '2', '--reader-dir', str(base)]
            with patch.object(sys, 'argv', argv), patch.object(session.subprocess, 'run', side_effect=run), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(session.main(), 0)
            result = json.loads((out / 'summary.json').read_text())
            self.assertEqual(result['operator_declared_fast_switching'], 'off')
            self.assertEqual(result['reader_exit_code'], 2)
            self.assertFalse(json.loads((out / 'metadata.json').read_text())['setting_independently_verified'])
            with self.assertRaises(FileExistsError), patch.object(sys, 'argv', argv):
                session.main()

    def test_offline_no_subprocess(self):
        with tempfile.TemporaryDirectory() as directory:
            raw = Path(directory) / 'raw.jsonl'
            raw.write_text(''.join(json.dumps(s) + '\n' for s in self.records()))
            with patch.object(sys, 'argv', ['session', '--analyze', str(raw)]), patch.object(session.subprocess, 'run') as run, contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(session.main(), 0)
                run.assert_not_called()

    def downstream_records(self):
        records = self.records()
        names = session.LAYOUTS[('0x26102101', '3.0')]
        for record in records:
            record.update(fpga='0x26102101', abi='3.0')
            point = record['points']['dante_out']
            record['points'] = {name: copy.deepcopy(point) for name in names}
            for i, p in enumerate(record['points'].values()):
                p['snapshot_ref_ticks'] = 9000000
                p['sof_age_ref_ticks'] = 100000 - i * 10
        return records

    def test_downstream_layout_and_phase(self):
        result = session.summarize(self.downstream_records())
        self.assertEqual(result['status'], 'NO_COUNTER_ANOMALY')
        self.assertEqual(result['abi'], '3.0')
        self.assertNotIn('vproc_out', result['points'])
        for pair in result['adjacent_pipeline_sof_phase_ref_ticks'].values():
            self.assertEqual(pair['samples'], 2)
            self.assertEqual(pair['signed_modulo_upstream_period']['median'], 10)

    def test_downstream_boundary_missing_data_is_not_healthy(self):
        records = self.downstream_records()
        records[1]['points']['mixer_out']['accepted_beats'] = 0
        self.assertNotEqual(session.summarize(records)['status'], 'NO_COUNTER_ANOMALY')

    def test_mixed_layouts_rejected(self):
        records = self.downstream_records()
        records[1].update(fpga='0x26102005', abi='2.0')
        self.assertNotEqual(session.summarize(records)['status'], 'NO_COUNTER_ANOMALY')
        records[0]['abi'] = '9.0'
        with self.assertRaises(ValueError):
            session.summarize(records)

    def test_phase_wrap_not_full_frame_latency(self):
        records = self.downstream_records()
        for record in records:
            record['points']['mixer_out']['sof_age_ref_ticks'] += 1666667
        result = session.summarize(records)
        self.assertEqual(result['adjacent_pipeline_sof_phase_ref_ticks']['mixer_overlay_in->mixer_out']['signed_modulo_upstream_period']['median'], 10)

    def test_nonfinite_interval_rejected_before_output(self):
        with patch.object(sys, 'argv', ['session', '--state', 'on', '--output', '/unused', '--interval', 'nan']), contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                session.main()


if __name__ == '__main__':
    unittest.main()
