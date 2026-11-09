"""Stream chronology, completion and accounting; bounded injected DSP fixtures."""
from pathlib import Path
import hashlib
import sys
import tempfile
import unittest
from unittest import mock

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_stream_receiver import CompleteStreamReceiver, native_short_decoder
from triad_message_gate import _choose_case, _ordered_cases, run_native_message_gate


class PublicSource:
    """Public schedules remain queryable beyond any emitted payload endpoint."""
    def __init__(self, stride=4, window=6):
        self.stride, self.window = stride, window
        self.plans, self.addresses = [], []
        self.open_receivers = 0
        self.descriptor = dict(source_samples=4, total_samples=1, emitted_samples=1,
                               fixed_workspace_bytes=0, workspace_budget_bytes=0)

    def slot_plan(self, index):
        self.plans.append(index)
        first = self.stride*index
        return dict(schema='triad-public-slot-plan-v1', index=index,
            sample_rate_hz=2, pattern_epoch=1000+2*index, symbol_ordinal=100+index,
            source_begin_samples=first, next_source_begin_samples=first+self.stride,
            source_samples=4, source_calendar_origin_sample=1,
            helper_begin_samples=first, pattern_begin_samples=first+1,
            pattern_end_samples=first+3, source_end_samples=first+4,
            helper_end_samples=first+4, minimum_source_step_samples=self.stride,
            receiver_window_first_sample=first, receiver_window_stop_sample=first+self.window,
            receiver_window_physical_origin_sample=first, receiver_window_samples=self.window,
            receiver_data_first_sample=first+1, receiver_data_stop_sample=first+self.window-1,
            receiver_previous_signal_stop_sample=first, receiver_next_signal_first_sample=first+self.window,
            quiet_aperture_first_sample=first+1, quiet_aperture_stop_sample=first+self.window-1,
            capture_lead_seconds=1., window_capture_lead_seconds=1.-first/2)


def public_manifest():
    # Deliberately wrong original capture extent: the PUBLIC per-slot planner
    # replaces it, rather than an emitter's one-sample total choosing geometry.
    return dict(sample_rate_hz=2, format={}, channel={}, power={}, helper_enabled=False,
                calendar=dict(capture_samples=1, data_sample_offset=999,
                              capture_lead_seconds=123., arrival_prior_seconds=[0., 0.]))


def bank_builder(source, manifest, **arguments):
    assert source.open_receivers == 0, 'address changed while a receiver remained alive'
    address = dict(arguments['public_address'])
    source.addresses.append(address)
    source.current_address = address
    return dict(screen=object(), manifest=manifest,
                work_receipt=dict(preparation_peak_estimate_bytes=1024),
                template_identity='public-bank-'+str(address['symbol_ordinal']))


class BitReceiver:
    seen = []
    geometry_error = False

    def __init__(self, manifest, screen, source, **arguments):
        self.manifest, self.source = manifest, source
        self.source.open_receivers += 1
        self.closed = False
        self.arguments = arguments

    def receive(self, values, valid):
        cal = self.manifest['calendar']
        self.seen.append((dict(cal), values.copy(), valid.copy(), dict(self.source.current_address)))
        n, fs = len(values), self.manifest['sample_rate_hz']
        first = cal['physical_origin_sample']
        marker = round(float(values[0].real))
        kind = ('AMBIGUOUS' if not bool(valid.all()) or marker == 3 else
                'QUIET' if marker == 0 else 'DATA')
        complete = kind != 'AMBIGUOUS'
        duration = n/fs
        stages = {name: dict(included_in_service=True,
            represented_baseband_seconds=duration,
            ratio_processing_seconds_per_baseband_second=.001) for name in (
                'helper', 'coarse', 'refinement_selection', 'fine_preparation', 'tracking', 'decision')}
        return dict(schema='fixture-complete-bit-v1', status='scored' if complete else 'unresolved',
            slot_classification=kind, decision_bit=marker-1 if kind == 'DATA' else None,
            sample_rate_hz=fs, represented_baseband_seconds=duration,
            ratio_processing_seconds_per_baseband_second=.006, stages=stages,
            operational=dict(passes_conditional_bound=kind == 'DATA'),
            coverage=dict(complete_observation=complete, all_input_valid=bool(valid.all()),
                consumed_samples=n if complete else 0, expected_samples=n,
                physical_first_sample=first+(1 if self.geometry_error else 0),
                physical_stop_sample=first+n+(1 if self.geometry_error else 0),
                zero_h0_verified=True, valid_noise_statistics=True),
            quiet=dict(noise_consistent=kind == 'QUIET', projected_energy=0., threshold=1.,
                observed_projection_modes=n, noise_upper_tail_probability=.001),
            workspace=dict(estimated_peak_bytes=1024))

    def close(self):
        assert not self.closed
        self.closed = True
        self.source.open_receivers -= 1


class CompleteStreamReceiverTests(unittest.TestCase):
    def setUp(self):
        BitReceiver.seen = []
        BitReceiver.geometry_error = False
        self.decode = mock.Mock(return_value=dict(decoded_bytes=1, filtered_text='e'))

    def receiver(self, *, source=None, **arguments):
        source = PublicSource() if source is None else source
        receiver = CompleteStreamReceiver(public_manifest(), source,
            source_identity=dict(public_key_fixture='bounded'), memory_limit_bytes=2**20,
            maximum_slots=6, maximum_input_chunk_samples=4,
            maximum_buffer_samples=64, maximum_gap_samples=8,
            decode_short_message=self.decode, bank_builder=bank_builder,
            receiver_factory=BitReceiver, **arguments)
        self.addCleanup(receiver.close)
        return receiver, source

    def record(self, markers, *, stride=4, extra=2):
        samples = np.zeros((len(markers)-1)*stride+6+extra, complex)
        for index, marker in enumerate(markers):
            samples[index*stride] = marker
        return samples

    def feed(self, receiver, values, first=0):
        result = None
        for offset in range(0, len(values), 4):
            result = receiver.push(first+offset, values[offset:offset+4])
            if result['status'] != 'receiving':
                break
        return result

    def test_actual_chunks_drive_three_bits_then_quiet_with_overlap_counted_once(self):
        receiver, source = self.receiver()
        result = self.feed(receiver, self.record([1, 1, 2, 0, 0]))
        self.assertEqual(result['status'], 'complete')
        self.assertTrue(result['physical_complete'])
        self.assertTrue(result['complete_receiver_work_covered'])
        self.assertEqual(result['message']['received_bits'], [0, 0, 1])
        self.decode.assert_called_once_with((0, 0, 1))
        self.assertEqual(source.plans, list(range(5)))
        self.assertEqual(len(source.addresses), 5)
        self.assertEqual(source.open_receivers, 0)
        self.assertEqual(result['ingested_samples'], 24)
        self.assertEqual(result['decision_physical_stop_sample'], 22)
        self.assertEqual(result['received_samples_through_decision'], 22)
        self.assertEqual(result['represented_baseband_seconds'], 11.)
        self.assertGreater(result['ratio_processing_seconds_per_baseband_second'], 0)
        self.assertEqual(sum(row['coverage']['expected_samples'] for row in result['slots']), 30)
        self.assertLess(result['workspace']['peak_overlap_buffer_bytes'], 17*20)
        for index, row in enumerate(result['slots']):
            cal = BitReceiver.seen[index][0]
            self.assertEqual(cal['physical_origin_sample'], 4*index)
            self.assertEqual(cal['capture_lead_seconds'], 1.-2*index)
            self.assertEqual(cal['data_sample_offset'], 4*index)
            self.assertEqual(cal['quiet_aperture_first_sample'], 4*index+1)
            self.assertEqual(cal['quiet_aperture_stop_sample'], 4*index+source.window-1)
            self.assertEqual(cal['receiver_previous_signal_stop_sample'], 4*index)
            self.assertEqual(row['public_address'], dict(pattern_epoch=1000+2*index,
                                                       symbol_ordinal=100+index))
            self.assertTrue(row['stages']['recurring_bank_preparation']['included_in_service'])
        with self.assertRaisesRegex(RuntimeError, 'terminal'):
            receiver.push(24, np.zeros(1, complex))

    def test_eof_cancel_and_noise_only_never_substitute_for_physical_end(self):
        receiver, _ = self.receiver()
        self.feed(receiver, self.record([1, 1, 2, 0], extra=0))
        result = receiver.finish_input()
        self.assertEqual(result['status'], 'input_ended')
        self.assertFalse(result['physical_complete'])
        self.assertIsNone(result['ratio_processing_seconds_per_baseband_second'])
        self.decode.assert_not_called()
        second, _ = self.receiver()
        result = second.cancel()
        self.assertEqual(result['status'], 'cancelled')
        self.assertFalse(result['physical_complete'])
        third, _ = self.receiver()
        result = self.feed(third, self.record([0]*6))
        self.assertEqual(result['status'], 'resource_limit')
        self.assertFalse(result['physical_complete'])

    def test_interior_and_leading_ambiguity_prevent_decoding_a_suffix(self):
        for markers, erased in (([1, 3, 2, 0, 0], 1), ([3, 1, 0, 0], 0)):
            with self.subTest(markers=markers):
                receiver, _ = self.receiver()
                result = self.feed(receiver, self.record(markers))
                self.assertEqual(result['status'], 'incomplete_message')
                self.assertTrue(result['physical_complete'])
                self.assertIn(erased, result['message']['erasure_slot_indices'])
                self.assertIsNone(result['ratio_processing_seconds_per_baseband_second'])
        self.decode.assert_not_called()

    def test_timestamp_gap_outside_windows_is_not_denominator_or_decode_evidence(self):
        receiver, _ = self.receiver(source=PublicSource(stride=10))
        # Consume one DATA window, then leave a timestamp gap in the public
        # inter-slot interval. Later valid QUIET windows cannot erase that gap.
        receiver.push(0, np.array([1, 0, 0, 0], complex))
        receiver.push(4, np.zeros(2, complex))
        result = self.feed(receiver, self.record([0, 0], stride=10, extra=0), first=10)
        self.assertTrue(result['physical_complete'])
        self.assertEqual(result['timestamp_gap_samples'], 4)
        self.assertFalse(result['complete_receiver_work_covered'])
        self.assertIsNone(result['ratio_processing_seconds_per_baseband_second'])
        self.assertEqual(result['received_samples_through_decision'],
                         result['decision_physical_stop_sample']-4)
        self.decode.assert_not_called()

    def test_local_limits_and_backend_geometry_failures_never_become_quiet(self):
        receiver, _ = self.receiver()
        BitReceiver.geometry_error = True
        result = self.feed(receiver, self.record([0]*6))
        self.assertEqual(result['status'], 'resource_limit')
        self.assertFalse(result['physical_complete'])
        self.assertTrue(all(row['slot_classification'] == 'AMBIGUOUS' for row in result['slots']))
        self.decode.assert_not_called()
        with self.assertRaises(MemoryError):
            self.receiver(source=PublicSource(window=65))

    def test_input_bounds_are_checked_before_array_materialization_and_chronology(self):
        receiver, _ = self.receiver()
        class TooLong:
            def __len__(self): return 1000
            def __array__(self): raise AssertionError('must not materialize oversized input')
        with self.assertRaisesRegex(ValueError, 'count bound'):
            receiver.push(0, TooLong())
        for first, values, valid in ((True, np.zeros(1, complex), None),
                (0, np.zeros(1), None), (0, np.array([complex(float('nan'), 0)]), None),
                (0, np.zeros(1, complex), [2]), (9, np.zeros(1, complex), None)):
            with self.subTest(first=first), self.assertRaises(ValueError):
                receiver.push(first, values, valid)
        receiver.push(0, np.zeros(2, complex))
        with self.assertRaisesRegex(ValueError, 'backwards'):
            receiver.push(1, np.zeros(2, complex))

    def test_bank_failure_closes_stream_without_decoding_or_skipping_stage_cost(self):
        def failed_builder(*args, **kwargs):
            raise MemoryError('locally bounded preparation')
        receiver = CompleteStreamReceiver(public_manifest(), PublicSource(),
            source_identity='public', memory_limit_bytes=2**20, maximum_slots=6,
            maximum_input_chunk_samples=4, maximum_buffer_samples=64,
            decode_short_message=self.decode, bank_builder=failed_builder,
            receiver_factory=BitReceiver)
        self.addCleanup(receiver.close)
        result = self.feed(receiver, np.zeros(8, complex))
        self.assertEqual(result['status'], 'resource_limit')
        self.assertFalse(result['physical_complete'])
        self.assertEqual(result['diagnostic']['exception_type'], 'MemoryError')
        self.assertEqual(result['diagnostic']['error_phase'], 'recurring_public_bank_preparation')
        self.assertGreater(result['observed_ratio_processing_seconds_per_baseband_second'], 0)
        self.decode.assert_not_called()

    def test_missing_preparation_workspace_receipt_keeps_capacity_unresolved(self):
        def incomplete_builder(*args, **kwargs):
            row = bank_builder(*args, **kwargs)
            row.pop('work_receipt')
            return row
        receiver = CompleteStreamReceiver(public_manifest(), PublicSource(),
            source_identity='public', memory_limit_bytes=2**20, maximum_slots=6,
            maximum_input_chunk_samples=4, maximum_buffer_samples=64,
            decode_short_message=self.decode, bank_builder=incomplete_builder,
            receiver_factory=BitReceiver)
        self.addCleanup(receiver.close)
        result = self.feed(receiver, self.record([1, 1, 2, 0, 0]))
        self.assertEqual(result['status'], 'complete')
        self.assertFalse(result['complete_receiver_work_covered'])
        self.assertIsNone(result['ratio_processing_seconds_per_baseband_second'])

    def test_recurring_build_and_decode_are_timed_but_generator_waits_are_excluded(self):
        class Clock:
            now = 0.
            def __call__(self):
                self.now += .001
                return self.now
            def advance(self, seconds):
                self.now += seconds
        clock = Clock()
        def timed_builder(*args, **kwargs):
            clock.advance(2.)
            return bank_builder(*args, **kwargs)
        def timed_decoder(bits):
            clock.advance(3.)
            return dict(decoded_bytes=1, filtered_text='e')
        with mock.patch('triad_stream_receiver.time.perf_counter', side_effect=clock):
            receiver = CompleteStreamReceiver(public_manifest(), PublicSource(),
                source_identity='public', memory_limit_bytes=2**20, maximum_slots=6,
                maximum_input_chunk_samples=4, maximum_buffer_samples=64,
                decode_short_message=timed_decoder, bank_builder=timed_builder,
                receiver_factory=BitReceiver)
            self.addCleanup(receiver.close)
            samples = self.record([1, 1, 2, 0, 0])
            for first in range(0, len(samples), 4):
                clock.advance(100.)  # Outside receiver service: a slow simulator.
                result = receiver.push(first, samples[first:first+4])
                if result['status'] != 'receiving':
                    break
        ratio = result['ratio_processing_seconds_per_baseband_second']
        self.assertTrue(result['complete_receiver_work_covered'])
        self.assertGreaterEqual(ratio, 13./11.)  # Five2s builds plus3s decode.
        self.assertLess(ratio, 14./11.)
        self.assertGreaterEqual(result['service_components']['complete_slot_pipeline_ratio'], 10./11.)
        self.assertGreaterEqual(result['service_components']['stream_orchestration_ratio'], 3./11.)

    @mock.patch('triad_stream_receiver.subprocess.run')
    def test_canonical_decoder_adapter_checks_exact_bits_and_filtered_native_receipt(self, run):
        run.return_value.stdout = b'{"schema":"triad-short-codec-v1","operation":"decode","codec":"short-prefix","received_bits":3,"decoded_bytes":1,"text":"e"}'
        decode = native_short_decoder('/bounded/exporter')
        self.assertEqual(decode((0, 0, 1)), dict(decoded_bytes=1, filtered_text='e'))
        self.assertEqual(run.call_args.args[0], ['/bounded/exporter', '--short-decode', '001'])
        for bits in ([0], (True,), (2,), (0,)*209):
            with self.subTest(bits=bits), self.assertRaises(ValueError):
                decode(bits)
        run.return_value.stdout = b'{"schema":"triad-short-codec-v1","operation":"decode","codec":"short-prefix","received_bits":4,"decoded_bytes":1,"text":"e"}'
        with self.assertRaisesRegex(ValueError, 'acknowledgement'):
            decode((0, 0, 1))


class NativeMessageGateHarnessTests(unittest.TestCase):
    @staticmethod
    def case(index, name, *, helper='off', rate='36', encrypt='off'):
        return dict(index=index, heading=name, configuration_hash=name, native_args=[
            '--case', 'hf', '--helper', helper, '--fhss', 'off', '--encrypt', encrypt,
            '--rate', rate])

    def test_current_configuration_selects_gate_without_a_frozen_row_number(self):
        helper = self.case(1, 'helper', helper='on')
        slow = self.case(7, 'slow', rate='.1')
        fast = self.case(9, 'fast')
        duplicate = dict(fast, index=12)
        self.assertEqual([row['index'] for row in _ordered_cases([helper, slow, fast, duplicate])], [9, 7, 1])
        with mock.patch('triad_case_suite.native_plan', return_value=dict(
                geometry_feasible=True, geometry=dict(padded_source_samples=48000))) as plan:
            chosen, attempts = _choose_case(Path('/native'), [helper, slow, fast])
        self.assertEqual(chosen['index'], 9)
        self.assertEqual(len(attempts), 1)
        plan.assert_called_once_with(Path('/native'), fast['native_args'])

    def test_ineligible_current_profiles_stop_at_the_fixed_planning_budget(self):
        rows = [self.case(index, str(index)) for index in range(20)]
        with mock.patch('triad_case_suite.native_plan', return_value=dict(
                geometry_feasible=True, geometry=dict(padded_source_samples=500001))) as plan:
            chosen, attempts = _choose_case(Path('/native'), rows)
        self.assertIsNone(chosen)
        self.assertEqual(len(attempts), 8)
        self.assertEqual(plan.call_count, 8)

    def test_source_or_manual_edits_invalidate_an_otherwise_passing_gate(self):
        for change in ('none', 'source', 'manual'):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary)/'manual.md'
                path.write_text('current public fixture\n')
                case = self.case(9, 'fast')
                parsed = dict(source_path=str(path),
                    source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(), cases=[case])
                before = dict(sources={'tools/receiver.py': 'before'}, native_binary_sha256='binary')
                after = dict(sources={'tools/receiver.py': 'after' if change == 'source' else 'before'},
                             native_binary_sha256='binary')
                def functional(*args, **kwargs):
                    if change == 'manual':
                        path.write_text('changed public fixture\n')
                    return dict(functional_passed=True)
                with mock.patch('triad_cases.load_cases', return_value=parsed) as load, \
                     mock.patch('triad_case_suite.identity', side_effect=[before, after]), \
                     mock.patch('triad_message_gate._choose_case', return_value=(case, [])), \
                     mock.patch('triad_message_gate._run_selected_case', side_effect=functional), \
                     mock.patch('triad_cpu.resolve_cpu_budget', return_value=dict(explicit_overcommit=False)), \
                     mock.patch('triad_cpu.configure_numerical_threads', return_value={}):
                    report = run_native_message_gate('/native', 2**20, case_list=path)
                load.assert_called_once_with(path)
                self.assertEqual(report['status'], 'passed' if change == 'none' else 'invalidated')
                self.assertEqual(report['passed'], change == 'none')


def native_gate_main(arguments):
    """Explicit opt-in integration entry; normal discovery runs fast fixtures."""
    import argparse
    import json
    from triad_case_metrics import machine_resources
    parser = argparse.ArgumentParser(allow_abbrev=False,
        description='Optional bounded native complete-message functional gate; no throughput campaign.')
    parser.add_argument('--native-message-gate', type=Path, required=True,
                        help='canonical native exporter to exercise')
    parser.add_argument('--output', type=Path,
                        help='optional temporary JSON evidence path')
    parser.add_argument('--case-list', type=Path,
                        help='current manual list; defaults to the maintained source')
    parser.add_argument('--memory-bytes', type=int)
    parser.add_argument('--workers', type=int, default=1)
    args = parser.parse_args(arguments)
    memory = args.memory_bytes
    if memory is None:
        memory = machine_resources()['memory_budget_bytes']
    report = run_native_message_gate(args.native_message_gate, memory, workers=args.workers,
        case_list=args.case_list, progress=lambda message: print(message, flush=True))
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, allow_nan=False)+'\n')
    print(json.dumps(dict(status=report['status'], passed=report['passed'],
        case_index=report.get('case', {}).get('index'),
        output=None if args.output is None else str(args.output))), flush=True)
    return 0 if report['passed'] else 2


if __name__ == '__main__':
    if any(argument.split('=', 1)[0] == '--native-message-gate' for argument in sys.argv[1:]):
        raise SystemExit(native_gate_main(sys.argv[1:]))
    unittest.main()
