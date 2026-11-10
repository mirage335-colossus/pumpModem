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
            workspace=dict(estimated_peak_bytes=1024,
                           memory_limit_bytes=self.arguments['memory_limit_bytes']))

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
        arguments.setdefault('continuous', False)
        arguments.setdefault('workers', 1)
        receiver = CompleteStreamReceiver(public_manifest(), source,
            source_identity=dict(public_key_fixture='bounded'),
            memory_limit_bytes=arguments.pop('memory_limit_bytes', 2**20),
            maximum_slots=arguments.pop('maximum_slots', 6),
            maximum_input_chunk_samples=arguments.pop('maximum_input_chunk_samples', 4),
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

    def test_continuous_noise_does_not_exhaust_lifetime_slots_or_diagnostics(self):
        receiver, source = self.receiver(continuous=True, maximum_pending_messages=2,
            recent_slot_receipts=5)
        result = self.feed(receiver, self.record([0]*500))
        self.assertEqual(result['status'], 'receiving')
        self.assertEqual(result['completed_slot_count'], 500)
        self.assertEqual(result['completed_message_count'], 0)
        self.assertEqual(result['pending_message_count'], 0)
        self.assertFalse(result['physical_complete'])
        self.assertTrue(result['complete_receiver_work_covered'])
        self.assertEqual(result['slot_classification_counts']['QUIET'], 500)
        self.assertEqual(result['slot_receipt_count'], 5)
        self.assertEqual([row['index'] for row in receiver.result()['slots']], list(range(495, 500)))
        self.assertEqual(source.addresses[-1]['symbol_ordinal'], 599)
        self.assertLessEqual(receiver._receipt_bytes, 5*16*1024)
        self.assertLessEqual(receiver._buffer_samples, source.window+4)
        self.decode.assert_not_called()

    def test_continuous_two_messages_retain_following_buffer_and_global_address(self):
        receiver, source = self.receiver(continuous=True, maximum_pending_messages=3,
            recent_slot_receipts=4, maximum_input_chunk_samples=64)
        values = self.record([1, 0, 0, 2, 0, 0])
        result = receiver.push(0, values)
        self.assertEqual(result['status'], 'receiving')
        self.assertEqual(result['completed_message_count'], 2)
        self.assertEqual(result['pending_message_count'], 2)
        self.assertEqual(result['completed_slot_count'], 6)
        self.assertFalse(result['physical_complete'])
        self.assertTrue(result['complete_receiver_work_covered'])
        self.assertEqual(result['represented_baseband_seconds'], 13.)
        self.assertEqual(result['ingested_samples'], 28)
        self.assertEqual(result['processed_physical_stop_sample'], 26)
        self.assertEqual(self.decode.call_args_list, [mock.call((0,)), mock.call((1,))])
        self.assertEqual([x['symbol_ordinal'] for x in source.addresses], list(range(100, 106)))
        self.assertGreater(receiver._buffer_samples, 0)
        self.assertEqual(result['messages'], [])
        with mock.patch('triad_stream_receiver.copy.deepcopy',
                        side_effect=AssertionError('cheap receipt must not copy stored snapshots')):
            cheap = receiver.result(include_slots=False)
        self.assertEqual(cheap['pending_message_count'], 2)
        detailed = receiver.result()
        detailed['messages'][0]['message']['received_bits'][0] = 1
        events = receiver.drain_messages()
        self.assertEqual([x['message']['received_bits'] for x in events], [[0], [1]])
        self.assertEqual([(x['first_public_slot'], x['stop_public_slot']) for x in events], [(0, 3), (3, 6)])
        self.assertEqual([x['decision_physical_stop_sample'] for x in events], [14, 26])
        self.assertEqual(receiver.drain_messages(), [])

    def test_continuous_backpressure_rejects_unconsumed_push_and_resumes_buffer(self):
        receiver, _ = self.receiver(continuous=True, maximum_pending_messages=1,
            recent_slot_receipts=4, maximum_input_chunk_samples=64)
        result = receiver.push(0, self.record([1, 0, 0, 2, 0, 0]))
        self.assertEqual(result['status'], 'message_backpressure')
        self.assertEqual(result['completed_slot_count'], 3)
        self.assertEqual(result['ingested_stop_sample'], 28)
        class UnreadInput:
            def __len__(self): raise AssertionError('backpressured input must remain untouched')
            def __array__(self): raise AssertionError('backpressured input must remain untouched')
        with self.assertRaisesRegex(RuntimeError, 'backpressure'):
            receiver.push(28, UnreadInput())
        self.assertEqual(receiver.result()['ingested_stop_sample'], 28)
        self.assertEqual(receiver.drain_messages()[0]['message']['received_bits'], [0])
        with self.assertRaisesRegex(RuntimeError, 'resumed'):
            receiver.push(28, UnreadInput())
        self.assertEqual(receiver.result()['ingested_stop_sample'], 28)
        result = receiver.resume()
        self.assertEqual(result['status'], 'message_backpressure')
        self.assertEqual(result['completed_slot_count'], 6)
        self.assertEqual(receiver.drain_messages()[0]['message']['received_bits'], [1])
        self.assertEqual(receiver.resume()['status'], 'receiving')
        result = receiver.push(28, np.zeros(2, complex))
        self.assertEqual(result['ingested_stop_sample'], 30)
        self.assertEqual(result['completed_slot_count'], 7)

    def test_continuous_resync_needs_scored_quiet_and_recovers_from_old_gap(self):
        receiver, _ = self.receiver(continuous=True, source=PublicSource(stride=10),
            maximum_pending_messages=2, recent_slot_receipts=4, maximum_input_chunk_samples=64)
        receiver.push(0, np.zeros(6, complex))
        result = receiver.push(10, self.record([0, 1, 0, 0], stride=10, extra=0))
        self.assertEqual(result['status'], 'receiving')
        self.assertEqual(result['timestamp_gap_samples'], 4)
        self.assertFalse(result['complete_receiver_work_covered'])
        event = receiver.drain_messages()[0]
        self.assertEqual(event['first_public_slot'], 2)
        self.assertEqual(event['message']['status'], 'complete')
        self.decode.assert_called_once_with((0,))

    def test_continuous_ambiguity_is_not_discarded_until_a_quiet_boundary(self):
        receiver, _ = self.receiver(continuous=True, maximum_pending_messages=3,
            recent_slot_receipts=4, maximum_input_chunk_samples=64)
        receiver.push(0, self.record([3, 1, 0, 0, 2, 0, 0]))
        events = receiver.drain_messages()
        self.assertEqual(events[0]['message']['status'], 'incomplete_message')
        self.assertEqual(events[0]['message']['erasure_slot_indices'], [0])
        self.assertEqual(events[1]['message']['status'], 'complete')
        self.decode.assert_called_once_with((1,))
        unresolved, _ = self.receiver(continuous=True, maximum_pending_messages=2,
            recent_slot_receipts=4)
        result = self.feed(unresolved, self.record([3]*6))
        self.assertEqual(result['status'], 'receiving')
        self.assertTrue(result['waiting_for_quiet_resynchronization'])
        self.assertFalse(result['physical_complete'])
        self.assertEqual(unresolved.drain_messages(), [])

    def test_continuous_rejected_episode_waits_for_quiet_before_later_message(self):
        examples = (
            ([3]*6+[1, 0, 0, 2, 0, 0], {}, 9, 'local_slot_limit_before_physical_completion'),
            ([1, 2, 1, 0, 1, 0, 0, 2, 0, 0], {'maximum_data_bits': 1}, 7,
             'local_data_bit_limit_exceeded'))
        for markers, options, fresh_slot, reason in examples:
            with self.subTest(reason=reason):
                self.decode.reset_mock()
                receiver, _ = self.receiver(continuous=True, maximum_pending_messages=2,
                    recent_slot_receipts=4, maximum_input_chunk_samples=64, **options)
                result = receiver.push(0, self.record(markers))
                self.assertEqual(result['status'], 'receiving')
                self.assertFalse(result['waiting_for_quiet_resynchronization'])
                self.assertEqual(result['rejected_episode_count'], 1)
                result = receiver.result()
                self.assertEqual(result['last_rejected_episode']['reason'], reason)
                self.assertFalse(result['last_rejected_episode']['physical_complete'])
                self.assertEqual(result['last_rejected_episode']['resynchronized_at_public_slot'], fresh_slot)
                events = receiver.drain_messages()
                self.assertEqual(len(events), 1)
                self.assertEqual(events[0]['first_public_slot'], fresh_slot)
                self.assertEqual(events[0]['message']['status'], 'complete')
                self.decode.assert_called_once_with((1,))

    def test_invalid_future_samples_do_not_poison_an_earlier_completed_message(self):
        receiver, _ = self.receiver(continuous=True, maximum_pending_messages=2,
            recent_slot_receipts=4, maximum_input_chunk_samples=64)
        values = self.record([1, 0, 0, 2, 0, 0])
        valid = np.ones(len(values), np.uint8)
        valid[14] = 0  # First physical completion stops at14, before this sample.
        result = receiver.push(0, values, valid)
        self.assertEqual(result['completed_message_count'], 1)
        self.assertEqual(receiver.drain_messages()[0]['message']['status'], 'complete')
        self.decode.assert_called_once_with((0,))
        self.assertFalse(result['complete_receiver_work_covered'])

    def test_continuous_eof_and_cancel_do_not_complete_the_current_message(self):
        receiver, _ = self.receiver(continuous=True, maximum_pending_messages=2,
            recent_slot_receipts=4)
        self.feed(receiver, self.record([1, 0]))
        self.assertFalse(receiver.finish_input()['physical_complete'])
        self.assertEqual(receiver.drain_messages(), [])
        other, _ = self.receiver(continuous=True, maximum_pending_messages=2,
            recent_slot_receipts=4)
        self.assertFalse(other.cancel()['physical_complete'])
        self.assertEqual(other.drain_messages(), [])

    def test_continuous_default_uses_the_full_visible_worker_budget(self):
        with mock.patch('triad_stream_receiver.resolve_cpu_budget', return_value={'threads': 12}):
            receiver = CompleteStreamReceiver(public_manifest(), PublicSource(),
                source_identity='public', memory_limit_bytes=4*2**20,
                maximum_buffer_samples=64, maximum_gap_samples=8,
                decode_short_message=self.decode, bank_builder=bank_builder,
                receiver_factory=BitReceiver)
        self.addCleanup(receiver.close)
        self.assertTrue(receiver.continuous)
        self.assertEqual(receiver.workers, 12)
        self.assertEqual(receiver.result()['worker_allocation'], 12)

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
            continuous=False, workers=1,
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
            continuous=False, workers=1,
            source_identity='public', memory_limit_bytes=2**20, maximum_slots=6,
            maximum_input_chunk_samples=4, maximum_buffer_samples=64,
            decode_short_message=self.decode, bank_builder=incomplete_builder,
            receiver_factory=BitReceiver)
        self.addCleanup(receiver.close)
        result = self.feed(receiver, self.record([1, 1, 2, 0, 0]))
        self.assertEqual(result['status'], 'complete')
        self.assertFalse(result['complete_receiver_work_covered'])
        self.assertIsNone(result['ratio_processing_seconds_per_baseband_second'])

    def test_recovery_requires_exact_integer_physical_geometry(self):
        original = BitReceiver.receive
        for field in ('sample_rate_hz', 'physical_first_sample', 'physical_stop_sample',
                      'expected_samples'):
            with self.subTest(field=field):
                def malformed(backend, values, valid):
                    row = original(backend, values, valid)
                    if backend.manifest['calendar']['physical_origin_sample'] >= 8:
                        target = row if field == 'sample_rate_hz' else row['coverage']
                        target[field] = float(target[field])
                    return row
                receiver, _ = self.receiver(continuous=True, maximum_data_bits=1,
                    maximum_pending_messages=2, recent_slot_receipts=5)
                with mock.patch.object(BitReceiver, 'receive', malformed):
                    result = self.feed(receiver, self.record([1, 2, 0, 0]))
                self.assertEqual(result['status'], 'receiving')
                self.assertTrue(result['waiting_for_quiet_resynchronization'])
                self.assertEqual(result['recovery_quiet_slot_count'], 0)
                self.assertFalse(result['complete_receiver_work_covered'])
                self.assertEqual(result['slot_classification_counts']['AMBIGUOUS'], 2)

    def test_recovery_workspace_receipts_cannot_silently_qualify_capacity(self):
        original = BitReceiver.receive
        cases = (None, {}, {'estimated_peak_bytes': -1}, {'estimated_peak_bytes': True},
                 {'estimated_peak_bytes': '1024'}, {'estimated_peak_bytes': 1024},
                 {'estimated_peak_bytes': 1024, 'memory_limit_bytes': False})
        for workspace in cases:
            with self.subTest(workspace=workspace):
                def malformed(backend, values, valid):
                    row = original(backend, values, valid)
                    if backend.manifest['calendar']['physical_origin_sample'] == 8:
                        if workspace is None:
                            row.pop('workspace')
                        else:
                            row['workspace'] = workspace
                    return row
                receiver, _ = self.receiver(continuous=True, maximum_data_bits=1,
                    maximum_pending_messages=2, recent_slot_receipts=5)
                with mock.patch.object(BitReceiver, 'receive', malformed):
                    result = self.feed(receiver, self.record([1, 2, 0, 0]))
                self.assertEqual(result['status'], 'receiving')
                self.assertFalse(result['waiting_for_quiet_resynchronization'])
                self.assertFalse(result['complete_receiver_work_covered'])
                self.assertFalse(result['workspace']['complete_preparation_receipts'])
                self.assertIsNone(result['ratio_processing_seconds_per_baseband_second'])

    def test_dsp_workspace_cannot_exceed_receiver_or_declared_budget(self):
        original = BitReceiver.receive
        for kind in ('peak_exceeds_receiver', 'peak_exceeds_declared', 'declared_exceeds_receiver'):
            with self.subTest(kind=kind):
                def over_budget(backend, values, valid):
                    row = original(backend, values, valid)
                    if kind == 'peak_exceeds_receiver':
                        row['workspace']['estimated_peak_bytes'] = backend.arguments['memory_limit_bytes']+1
                    elif kind == 'peak_exceeds_declared':
                        row['workspace']['memory_limit_bytes'] = 1023
                    else:
                        row['workspace']['memory_limit_bytes'] = backend.arguments['memory_limit_bytes']+1
                    return row
                receiver, _ = self.receiver()
                with mock.patch.object(BitReceiver, 'receive', over_budget):
                    result = self.feed(receiver, self.record([1, 0, 0]))
                self.assertEqual(result['status'], 'resource_limit')
                self.assertFalse(result['complete_receiver_work_covered'])
                self.assertFalse(result['physical_complete'])
                self.assertEqual(result['diagnostic']['exception_type'], 'MemoryError')

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
                continuous=False, workers=1,
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

    def test_measured_source_artifact_or_manual_edits_invalidate_an_otherwise_passing_gate(self):
        for change in ('none', 'source', 'artifact', 'manual'):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary)/'manual.md'
                path.write_text('current public fixture\n')
                case = self.case(9, 'fast')
                parsed = dict(source_path=str(path),
                    source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(), cases=[case])
                before = dict(sources={'tools/receiver.py': 'before'}, native_binary_sha256='binary',
                              native_artifacts={'/native-kernel': 'initial-kernel'})
                after = dict(sources={'tools/receiver.py': 'after' if change == 'source' else 'before'},
                             native_binary_sha256='binary',
                             native_artifacts={'/native-kernel': 'replaced-kernel' if change == 'artifact'
                                               else 'initial-kernel'})
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
                self.assertEqual(report['mode'], 'one-message')

    def test_continuous_gate_mode_is_explicit_and_boolean(self):
        parsed = dict(source_path='/manual', source_sha256='unused', cases=[])
        for invalid in (1, None, 'true'):
            with self.subTest(mode=invalid), \
                 mock.patch('triad_cases.load_cases', return_value=parsed), \
                 self.assertRaisesRegex(ValueError, 'boolean'):
                run_native_message_gate('/native', 2**20, continuous=invalid)


class ControlledSlotBackend:
    """No concurrent numerical work: exercise the real parent scheduling seam."""
    resident_bytes = 0
    native_workspace_bytes = 0

    def __init__(self, source, **options):
        self.source, self.total_workers = source, options['total_workers']
        self.pending, self.allocations, self.completed = {}, [], []
        self.release = False
        self.reverse = True
        self.blocking_polls = 0
        self.closed = False
        self.error = None
        source.backend = self

    @staticmethod
    def input_reservation_bytes(count):
        return 4*17*count+4*1024**2

    def submit(self, index, **job):
        assert len(self.pending) < 2
        assert sum(value['workers'] for value in self.pending.values())+job['workers'] <= self.total_workers
        self.pending[index] = job
        self.allocations.append((index, job['workers']))

    def poll(self, *, wait=False):
        if not self.pending or not (wait or self.release):
            return []
        self.blocking_polls += int(wait)
        from triad_slot_executor import score_public_slot
        import time
        index = (max if self.reverse else min)(self.pending)
        job = self.pending.pop(index)
        self.completed.append(index)
        if self.error:
            error, self.error = self.error, None
            now = time.perf_counter()
            return [dict(job_id=index, error_type=error, error_phase='fixture',
                service_start_monotonic=now, service_stop_monotonic=now)]
        result = score_public_slot(self.source, job.pop('manifest'),
            bank_builder=bank_builder, receiver_factory=BitReceiver, **job)
        return [dict(job_id=index, result=result,
            service_start_monotonic=result['service_start_monotonic'],
            service_stop_monotonic=result['service_stop_monotonic'])]

    def close(self):
        self.closed = True
        self.pending.clear()


class StreamPipelineTests(unittest.TestCase):
    setUp = CompleteStreamReceiverTests.setUp
    record = CompleteStreamReceiverTests.record

    def receiver(self, **options):
        source = PublicSource()
        source.command = source
        options.setdefault('maximum_pending_messages', 4)
        receiver = CompleteStreamReceiver(public_manifest(), source,
            source_identity={}, memory_limit_bytes=64*1024**2, workers=12,
            decode_short_message=self.decode, bank_builder=bank_builder,
            receiver_factory=BitReceiver, maximum_slots=8,
            maximum_input_chunk_samples=options.pop('maximum_input_chunk_samples', 32),
            maximum_buffer_samples=128,
            maximum_gap_samples=8, recent_slot_receipts=8,
            process_backend_factory=ControlledSlotBackend, **options)
        self.addCleanup(receiver.close)
        return receiver, source

    def test_async_ingress_lone_full_budget_then_pair_and_ordered_commit(self):
        receiver, source = self.receiver()
        values = self.record([1, 1, 2, 0, 0])
        first = receiver.push(0, values[:8])
        self.assertEqual(first['completed_slot_count'], 0)
        self.assertEqual(source.backend.allocations, [(0, 12)])
        later = receiver.push(8, values[8:])
        self.assertEqual(later['ingested_samples'], len(values))
        self.assertEqual(source.backend.allocations, [(0, 12)])
        self.assertFalse(later['complete_receiver_work_covered'])
        result = receiver.resume(wait=True)
        self.assertEqual(result['completed_slot_count'], 5)
        self.assertEqual(source.backend.allocations, [(0, 12), (1, 6), (2, 6), (3, 6), (4, 6)])
        self.assertEqual(source.backend.completed, [0, 2, 1, 4, 3])
        self.assertEqual(result['received_samples_through_decision'], 22)
        self.assertTrue(result['complete_receiver_work_covered'])
        self.assertEqual(receiver.drain_messages()[0]['message']['received_bits'], [0, 0, 1])

    def test_two_ready_windows_share_budget_immediately(self):
        receiver, source = self.receiver()
        receiver.push(0, np.zeros(10, complex))
        self.assertEqual(source.backend.allocations, [(0, 6), (1, 6)])
        self.assertEqual(receiver.resume(wait=False)['completed_slot_count'], 0)
        self.assertEqual(receiver.resume()['completed_slot_count'], 2)

    def test_shared_spawn_auxiliary_is_reserved_once_before_launch_and_after_fallback(self):
        receiver, source = self.receiver()
        before = receiver._reserved_bytes()
        reserve = 32*1024**2
        factory = receiver._backend_factory
        def launch(*args, **kwargs):
            self.assertEqual(receiver._reserved_bytes(), before+reserve)
            return factory(*args, **kwargs)
        receiver._backend_factory = launch
        with mock.patch('triad_stream_receiver.spawn_auxiliary_reservation_bytes', return_value=reserve):
            receiver._ensure_backend()
            self.assertEqual(receiver._reserved_bytes(), before+reserve)
            receiver._disable_pipeline('fixture_workspace_fallback')
            self.assertTrue(source.backend.closed)
            self.assertEqual(receiver._reserved_bytes(), before+reserve)
            receiver.close()
            self.assertEqual(receiver.result()['workspace']['spawn_auxiliary_reserved_bytes'], reserve)

    def test_tracker_prospective_reserve_can_prevent_launch_without_reducing_serial_budget(self):
        receiver, _ = self.receiver()
        receiver._custom_backend = False
        receiver._backend_factory = mock.Mock()
        before = receiver._reserved_bytes()
        # Two24MiB owners fit64MiB; their32MiB helper changes admission.
        owner, auxiliary = 24*1024**2, 32*1024**2
        self.assertLess(before+2*owner, receiver.memory_limit)
        self.assertLessEqual(receiver.memory_limit, before+2*owner+auxiliary)
        pages = owner // __import__('os').sysconf('SC_PAGE_SIZE')
        with mock.patch('triad_stream_receiver.Path.read_text', return_value=f'0 {pages}'), \
                mock.patch('triad_stream_receiver.spawn_auxiliary_reservation_bytes', return_value=32*1024**2):
            receiver._ensure_backend()
        receiver._backend_factory.assert_not_called()
        self.assertEqual(receiver._spawn_auxiliary_reserve, 0)
        self.assertEqual(receiver._reserved_bytes(), before)
        self.assertEqual(receiver._pipeline_fallback_reason, 'two_owner_startup_memory_admission')

    def test_ingress_waits_only_until_space_is_available_and_keeps_pair_running(self):
        receiver, source = self.receiver(maximum_input_chunk_samples=4)
        for first in range(0, 20, 4):
            receiver.push(first, np.zeros(4, complex))
        self.assertEqual(source.backend.allocations, [(0, 12)])
        result = receiver.push(20, np.zeros(4, complex))
        self.assertEqual(result['completed_slot_count'], 1)
        self.assertEqual(source.backend.allocations, [(0, 12), (1, 6), (2, 6)])
        self.assertEqual(set(source.backend.pending), {1, 2})
        self.assertEqual(result['ingested_samples'], 24)
        # Accepted subsequent input supplies the next pair while this pair is
        # still running; explicit resume remains a complete drain.
        receiver.push(24, np.zeros(4, complex))
        source.backend.reverse = False
        polls = source.backend.blocking_polls
        result = receiver.push(28, np.zeros(4, complex))
        self.assertEqual(source.backend.blocking_polls, polls+1)
        self.assertEqual(result['completed_slot_count'], 2)
        self.assertEqual(set(source.backend.pending), {2, 3})
        self.assertEqual(result['ingested_samples'], 32)
        result = receiver.resume()
        self.assertEqual(result['completed_slot_count'], 7)
        self.assertEqual(source.backend.allocations[:5], [(0, 12), (1, 6), (2, 6), (3, 6), (4, 6)])

    def test_workspace_rejection_closes_children_and_retries_once_full_budget(self):
        receiver, source = self.receiver()
        receiver.push(0, np.zeros(10, complex))
        source.backend.error = 'MemoryError'
        result = receiver.resume()
        self.assertEqual(result['status'], 'receiving')
        self.assertTrue(source.backend.closed)
        self.assertEqual(result['slot_pipeline']['workspace_retry_count'], 1)
        self.assertEqual(result['slot_pipeline']['serial_fallback_reason'], 'bounded_child_workspace_rejection')
        self.assertEqual([slot['slot_workers'] for slot in receiver.result()['slots']], [6, 12])
        receiver.push(10, np.zeros(4, complex))
        self.assertEqual(receiver.result()['slots'][-1]['slot_workers'], 12)

    def test_worker_failure_is_terminal_without_retry(self):
        receiver, source = self.receiver()
        receiver.push(0, np.zeros(6, complex))
        source.backend.error = 'RuntimeError'
        result = receiver.resume()
        self.assertEqual(result['status'], 'failed')
        self.assertEqual(result['slot_pipeline']['workspace_retry_count'], 0)
        self.assertTrue(source.backend.closed)
        self.assertFalse(result['physical_complete'])

    def test_eof_drains_captured_jobs_without_inventing_quiet(self):
        receiver, source = self.receiver()
        receiver.push(0, self.record([1, 2], extra=0))
        result = receiver.finish_input()
        self.assertEqual(result['status'], 'input_ended')
        self.assertEqual(result['completed_slot_count'], 2)
        self.assertFalse(result['physical_complete'])
        self.decode.assert_not_called()
        with self.assertRaises(RuntimeError):
            receiver.push(10, np.zeros(2, complex))

    def test_cancel_closes_inflight_owners_without_completion(self):
        receiver, source = self.receiver()
        receiver.push(0, np.zeros(10, complex))
        result = receiver.cancel()
        self.assertTrue(source.backend.closed)
        self.assertEqual(result['status'], 'cancelled')
        self.assertEqual(result['completed_slot_count'], 0)
        self.assertEqual(result['pending_message_count'], 0)

    def test_output_backpressure_preserves_unread_and_rejects_new_input(self):
        receiver, source = self.receiver(maximum_pending_messages=1)
        values = self.record([1, 0, 0, 2, 0, 0], extra=0)
        receiver.push(0, values)
        result = receiver.resume()
        self.assertEqual(result['status'], 'message_backpressure')
        before = result['ingested_samples']
        with self.assertRaisesRegex(RuntimeError, 'backpressure'):
            receiver.push(len(values), np.zeros(2, complex))
        self.assertEqual(receiver.result()['ingested_samples'], before)
        self.assertEqual(receiver.drain_messages()[0]['message']['received_bits'], [0])
        result = receiver.resume()
        self.assertEqual(receiver.drain_messages()[0]['message']['received_bits'], [1])
        receiver.resume()
        self.assertEqual(receiver.push(len(values), np.zeros(2, complex))['ingested_samples'], before+2)

    def test_future_gap_does_not_poison_prior_completed_episode(self):
        receiver, source = self.receiver()
        receiver.push(0, self.record([1, 0, 0], extra=0))
        receiver.push(16, np.zeros(2, complex))  # Gap14..16 lies after the first decision.
        result = receiver.resume()
        self.assertEqual(receiver.drain_messages()[0]['message']['decoded_content']['filtered_text'], 'e')
        self.assertEqual(result['timestamp_gap_samples'], 2)
        self.assertEqual(result['received_samples_through_decision'], 16)
        self.assertFalse(result['complete_receiver_work_covered'])

    def test_shutdown_failure_is_terminal_and_retains_owner_reservation(self):
        receiver, source = self.receiver()
        receiver.push(0, np.zeros(6, complex))
        close = source.backend.close
        source.backend.close = mock.Mock(side_effect=RuntimeError('owner did not exit'))
        result = receiver.cancel()
        self.assertEqual(result['status'], 'failed')
        self.assertEqual(result['reason'], 'slot_owner_shutdown_failed')
        self.assertIs(receiver._backend, source.backend)
        with self.assertRaises(RuntimeError):
            receiver.resume()
        with self.assertRaises(RuntimeError):
            receiver.push(6, np.zeros(2, complex))
        source.backend.close = close

    def test_interrupted_ordered_commit_cannot_resume_partial_parent_state(self):
        receiver, source = self.receiver()
        receiver.push(0, np.zeros(10, complex))
        original = receiver._commit_slot
        def interrupted(job, result):
            original(job, result)
            raise KeyboardInterrupt()
        receiver._commit_slot = interrupted
        with self.assertRaises(KeyboardInterrupt):
            receiver.resume()
        self.assertEqual(receiver.result()['status'], 'cancelled')
        self.assertTrue(source.backend.closed)
        with self.assertRaises(RuntimeError):
            receiver.resume()

    def test_eof_with_output_backpressure_drains_after_event_consumption(self):
        receiver, source = self.receiver(maximum_pending_messages=1)
        receiver.push(0, self.record([1, 0, 0, 2, 0, 0], extra=0))
        self.assertEqual(receiver.finish_input()['status'], 'message_backpressure')
        self.assertEqual(receiver.drain_messages()[0]['message']['received_bits'], [0])
        self.assertEqual(receiver.resume()['status'], 'message_backpressure')
        self.assertEqual(receiver.drain_messages()[0]['message']['received_bits'], [1])
        self.assertEqual(receiver.resume()['status'], 'input_ended')
        self.assertTrue(source.backend.closed)

    def test_service_union_excludes_caller_idle_and_does_not_double_charge_wait(self):
        receiver, _ = self.receiver()
        from triad_stream_receiver import _ServiceUnion
        receiver._service_clock, receiver._slot_clock = _ServiceUnion(), _ServiceUnion()
        receiver._jobs = {0: dict(submitted=1., finished=False)}
        with mock.patch('triad_stream_receiver.time.perf_counter', side_effect=[1., 2., 2.]):
            began = receiver._begin_service()
            receiver._account_parent(began)
        with mock.patch('triad_stream_receiver.time.perf_counter', side_effect=[5., 8., 8.]):
            began = receiver._begin_service()
            receiver._record_worker(dict(service_start_monotonic=1.5, service_stop_monotonic=7.))
            receiver._jobs.clear()
            receiver._account_parent(began)
        self.assertEqual(receiver._service, 7.)  # union[1,8], no repeated[5,7]
        self.assertEqual(receiver._slot_service, 5.5)
        with mock.patch('triad_stream_receiver.time.perf_counter', side_effect=[20., 21., 21.]):
            began = receiver._begin_service()
            receiver._account_parent(began)
        self.assertEqual(receiver._service, 8.)


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
    parser.add_argument('--continuous', action='store_true',
                        help='exercise default continuous mode through one extra native quiet slot')
    args = parser.parse_args(arguments)
    memory = args.memory_bytes
    if memory is None:
        memory = machine_resources()['memory_budget_bytes']
    report = run_native_message_gate(args.native_message_gate, memory, workers=args.workers,
        case_list=args.case_list, continuous=args.continuous,
        progress=lambda message: print(message, flush=True))
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
