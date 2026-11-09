"""Check observable estimator gates without a new sustained simulation."""
from copy import deepcopy
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))

from triad_cases import DEFAULT_CASES
from triad_nonfine_calibration import (NONFINE_STAGES, NOMINAL_HOLDOUT_RECEIPT_SCHEMA,
    SCHEMA as FIT_SCHEMA, declare_fixed_holdout)
from triad_replay_cpu import SCHEMA as REPLAY_SCHEMA
from triad_replay_producer import canonical_digest
from triad_replay_estimate import (_independent_work, _qualified_nominal, _read_local_receipt, assess_nominal_replay,
    estimate_current_manual_list)


class ReplayEstimateTests(unittest.TestCase):
    def nominal(self, ratio, interval, supported=True):
        return dict(complete_work_covered=True, all_component_models_supported=supported,
            ratio_processing_seconds_per_baseband_second=ratio,
            ratio_processing_seconds_per_baseband_second_interval=interval)

    def test_matched_point_does_not_pass_overlapping_interval(self):
        predicted = self.nominal(.90, [.8, .98])
        actual = self.nominal(.94, [.87, 1.02])
        result = _qualified_nominal(predicted, actual)
        self.assertEqual(result['status'], 'validated_conditional_nominal_service')
        self.assertEqual(result['nominal_service']['realtime_status'], 'unresolved')
        self.assertFalse(result['complete_receiver_capacity_established'])
        self.assertEqual(result['separate_worst_state_capacity'], 'unresolved_from_replay')

    def test_actual_point_failure_cannot_be_reported_below_one(self):
        result = _qualified_nominal(self.nominal(.99, [.9, 1.05]), self.nominal(1.01, [.94, 1.08]))
        self.assertEqual(result['status'], 'unresolved')
        self.assertNotIn('nominal_service', result)
        self.assertEqual(result['numeric_point_criterion'], 'fail')
        self.assertEqual(result['failed_nominal_points_processing_seconds_per_baseband_second'],
            {'independent_resident_nominal':1.01})

    def test_model_failure_still_preserves_complete_numeric_point_failure(self):
        result = _qualified_nominal(self.nominal(1.2, [.8, 1.6], supported=False),
            self.nominal(1.1, [.9, 1.3]))
        self.assertEqual(result['status'], 'unresolved')
        self.assertEqual(result['numeric_point_criterion'], 'fail')
        self.assertEqual(result['failed_nominal_points_processing_seconds_per_baseband_second'],
            {'candidate_replay_nominal':1.2, 'independent_resident_nominal':1.1})

    def test_receipt_rejects_float_overflow(self):
        with tempfile.TemporaryDirectory() as location:
            path = Path(location)/'overflow.json'
            path.write_text('{"finite": 1e999}')
            with self.assertRaisesRegex(ValueError, 'nonfinite'):
                _read_local_receipt(path)

    def test_large_holdout_error_withholds_prediction(self):
        result = _qualified_nominal(self.nominal(.70, [.6, .8]), self.nominal(.90, [.84, .98]))
        self.assertEqual(result['status'], 'unresolved')
        self.assertNotIn('nominal_service', result)

    def test_absent_holdout_never_emits_a_ratio(self):
        result = assess_nominal_replay(case={}, parsed={}, exporter='unused',
            replay={}, calibration={}, holdout=None)
        self.assertEqual(result['reason'], 'missing_replay_calibration_or_holdout')
        self.assertNotIn('nominal_service', result)

    def test_current_manual_list_keeps_duplicate_rows_and_rereads_edits(self):
        chunks = DEFAULT_CASES.read_text().split('\n- ', 2)
        first = '- '+chunks[1]
        with tempfile.TemporaryDirectory() as location:
            path = Path(location)
            cases, exporter = path/'cases.md', path/'exporter'
            exporter.write_bytes(b'identity fixture')
            cases.write_text(first+'\n'+first)
            kwargs = dict(case_list=cases, exporter=exporter, replays={}, calibrations={}, holdouts={})
            duplicate = estimate_current_manual_list(**kwargs)
            self.assertEqual([row['index'] for row in duplicate['cases']], [1, 2])
            self.assertEqual(duplicate['cases'][0]['configuration_sha256'],
                duplicate['cases'][1]['configuration_sha256'])
            self.assertTrue(all(row['status'] == 'unresolved' and 'nominal_service' not in row
                for row in duplicate['cases']))
            cases.write_text(first)
            edited = estimate_current_manual_list(**kwargs)
            self.assertEqual(edited['current_case_count'], 1)
            self.assertNotEqual(duplicate['manual_list_sha256'], edited['manual_list_sha256'])

    def test_assessment_matches_actual_work_and_rejects_material_changes(self):
        digest = 'a'*64
        coords = [[0, .5, .01, 0.]]
        signal = dict(schema='triad-complete-public-signal-work-v1', sample_count=60000,
            full_iq_sha256=digest, full_validity_sha256=digest, fine_slice_sha256=digest,
            acquisition=dict(status='acquired', complete_public_search=True, work_only=False,
                receiver_workers=2, helper_status='fallback', helper_reason='helper_disabled',
                helper_result_sha256=digest, coarse_reason='complete_public_workspace_search',
                coarse_candidates=[dict(bit=bit, epoch=0, arrival=.5, frequency=.01, clock_ppm=0., rank=1)
                    for bit in (0,1)], coarse_work=dict(workspaces=26, workers=2,
                    frequency_points=2, arrival_points=10, fft_products=52, correlation_method='frequency'),
                searched_frequency_intervals_hz=[[-.01,.01]], refinement_candidates=1, coordinates=coords))
        def component(ratio):
            key = 'ratio_processing_seconds_per_baseband_second'
            return {key:ratio, key+'_interval':[ratio*.9,ratio*1.1],
                'model_status':'abbreviated_holdout_supported'}

        def receipt(pid):
            pair = dict(boot_id_sha256=digest, controller=[pid,1], native=[pid+1,1])
            return dict(case_index=1, identity_before=dict(identity=dict(workers=2),
                observation=dict(process_pair=pair)),
                identity_after={}, work_started_ns=100*pid, work_ended_ns=100*pid+90)

        def checkpoint(rows):
            counts = {name: rows for name in ('transition_edges','hypothesis_update_tasks',
                'candidate_block_updates','merged_channel_components','projection_mode_updates','basis_rows')}
            return dict(counters=dict(processed_rows=rows,consumed_samples=rows,work_counts=counts,
                lanes={name:dict(bit_scores=[.5,.3]) for name in ('adaptive','initial_affine')},
                retained_states={name:[[0,0,0]] for name in ('adaptive','initial_affine')},
                native_templates=dict(energy=1.,calls=1,returned_elements=rows)))
        observation = dict(status='complete_work_observation', geometry=dict(supported=True),
            continued_cache_boundary_work=dict(supported=True),
            finish_classify_close=dict(complete_six_second_probe=True,endpoint='AMBIGUOUS'),
            prefix=checkpoint(12000), checkpoints=[checkpoint(rows) for rows in (18000,24000,36000)])
        fine = dict(status='measured_continuation', fine_component=component(.4),
            continuation_checkpoints_seconds=[1,2,4], prefix_baseband_seconds=2.,
            public_probe_samples=36000, observations=[deepcopy(observation) for _ in range(3)],
            successful_outer_observations=3, public_origin_sample=100,
            requested_center_sample=18100,workers=2)
        stages = {stage:component(.05) for stage in NONFINE_STAGES}
        geometry = dict(unit_samples=6000, sample_rate_hz=6000, search_samples=60000,
            memory_limit_bytes=5000000)
        measurement = dict(nominal_service=dict(components=dict(stages,complete_fine_search=component(.42))),
            nominal_acquisition=dict(samples=[{}, {}, {}], actual_work_match_count=3), signal_work=signal,
            selected_workers=2,inference_workers=2,inference_allocation_mode='pilot_selected',measurement_scope='full_capacity')
        runtime_match = dict(status='locally_matched_unqualified', resident_fine=fine,
            workspace=dict(status='within_budget', observed_peak_bytes=2000000, conservative_peak_bytes=2500000),
            resident_bank=dict(work=dict(fft_workspace_bytes=500000)),
            components=dict(nominal_service=stages))
        with tempfile.TemporaryDirectory() as location:
            exporter = Path(location)/'exporter'
            exporter.write_bytes(b'native exporter identity')
            case = dict(index=1, configuration_hash=digest)
            parsed = dict(source_sha256=digest,cases=[case])
            replay = dict(receipt(20),schema=REPLAY_SCHEMA,status='measured_detached_fine',
                workers=2,configuration_sha256=digest,manual_list_sha256=digest,
                source_identity_sha256=digest,native_exporter_sha256=hashlib.sha256(exporter.read_bytes()).hexdigest(),
                public_calibration_geometry=geometry,signal_work=signal,signal_work_sha256=canonical_digest(signal),
                acquisition=dict(input_sha256=digest,coordinates=coords),fine=fine,bank_reservation_bytes=500000)
            calibration, holdout = (dict(receipt(pid),measurement=deepcopy(measurement),
                schema=FIT_SCHEMA,status='recorded_unqualified') for pid in (10,30))
            with patch('triad_replay_estimate.source_identity_sha256',return_value=digest), \
                    patch('triad_replay_estimate.machine_resources',return_value={'memory_budget_bytes':5000000}), \
                    patch('triad_replay_estimate._current_runtime_matches',return_value=True) as current, \
                    patch('triad_replay_estimate.compare_nonfine_calibration',return_value=runtime_match) as compare:
                def assess(r=replay, c=calibration, h=holdout):
                    return assess_nominal_replay(case=case, parsed=parsed, exporter=exporter,
                        replay=r, calibration=c, holdout=h)
                holdout['schema']=NOMINAL_HOLDOUT_RECEIPT_SCHEMA
                holdout['measurement'].update(selected_workers=4,inference_workers=2,
                    inference_allocation_mode='fixed_holdout',measurement_scope='nominal_holdout')
                def bind(r=replay, h=holdout):
                    h['holdout_declaration'] = declare_fixed_holdout(calibration,r,
                        declared_ns=2500,boot_id_sha256=digest)
                    h['pilot_started_ns']=2800
                    return h
                bind()
                valid = assess()
                self.assertEqual(valid['status'],'validated_conditional_nominal_service')
                self.assertAlmostEqual(valid['nominal_service']['ratio_processing_seconds_per_baseband_second'], .7)
                self.assertFalse(valid['complete_receiver_capacity_established'])
                self.assertEqual([call.kwargs['nominal_holdout'] for call in compare.call_args_list],
                    [True,False])
                self.assertEqual(valid['holdout']['exploratory_selected_workers'],4)
                self.assertEqual(valid['holdout']['actual_inference_workers'],2)
                self.assertEqual(assess(c=holdout)['status'],'unresolved')
                fixed_fit = deepcopy(calibration)
                fixed_fit['measurement']['inference_allocation_mode']='fixed_holdout'
                self.assertEqual(assess(c=fixed_fit)['reason'],'holdout_was_not_predeclared_for_exact_fit_and_replay')
                for change in (dict(fit_sha256='c'*64), dict(replay_sha256='c'*64), dict(workers=4),
                               dict(declared_ns=2001), dict(declared_ns=2800)):
                    mismatched=deepcopy(holdout)
                    mismatched['holdout_declaration'].update(change)
                    self.assertEqual(assess(h=mismatched)['reason'],
                        'holdout_was_not_predeclared_for_exact_fit_and_replay')
                for change in (
                    lambda x:x['identity_before']['observation']['process_pair'].update(boot_id_sha256='c'*64),
                    lambda x:x['identity_before']['identity'].update(workers=4),
                    lambda x:x.update(pilot_started_ns=3000),
                ):
                    mismatched=deepcopy(holdout)
                    change(mismatched)
                    self.assertEqual(assess(h=mismatched)['reason'],
                        'holdout_was_not_predeclared_for_exact_fit_and_replay')
                late_fit = deepcopy(calibration)
                late_fit.update(work_started_ns=2200,work_ended_ns=2300)
                self.assertEqual(assess(c=late_fit)['reason'],
                    'holdout_was_not_predeclared_for_exact_fit_and_replay')
                for change in (
                    lambda x:x['signal_work'].update(full_iq_sha256='b'*64),
                    lambda x:x['signal_work']['acquisition'].update(helper_reason='changed_path'),
                    lambda x:x.update(workers=3),
                    lambda x:x.update(bank_reservation_bytes=499999),
                    lambda x:x['fine']['observations'][0]['prefix']['counters']['work_counts'].update(basis_rows=1),
                    lambda x:x['fine']['observations'][0]['prefix']['counters']['retained_states'].update(adaptive=[[1,0,0]]),
                ):
                    malformed = deepcopy(replay)
                    change(malformed)
                    malformed['signal_work_sha256'] = canonical_digest(malformed['signal_work'])
                    try:
                        attached = bind(r=malformed,h=deepcopy(holdout))
                    except ValueError:
                        attached = holdout
                    self.assertEqual(assess(r=malformed,h=attached)['status'],'unresolved')
                changed = deepcopy(holdout)
                changed['measurement']['nominal_acquisition']['actual_work_match_count']=2
                self.assertEqual(assess(h=changed)['status'],'unresolved')
                changed['measurement']['nominal_service']['components']['complete_fine_search']=component(.85)
                failure = assess(h=changed)
                self.assertEqual(failure['status'],'unresolved')
                self.assertEqual(failure['numeric_point_criterion'],'fail')
                self.assertAlmostEqual(failure['failed_nominal_points_processing_seconds_per_baseband_second'][
                    'independent_resident_nominal'], 1.15)
                compare.return_value = dict(runtime_match,workspace=dict(runtime_match['workspace'],status='failure'))
                self.assertEqual(assess()['reason'],'calibration_workspace_unresolved')
                current.return_value=False
                self.assertEqual(assess()['reason'],'current_local_runtime_identity_differs')

    def test_distinct_processes_cannot_time_overlapping_work(self):
        def receipt(pid, first, stop):
            pair = dict(boot_id_sha256='a'*64, controller=[pid, 10], native=[pid+1, 10])
            return dict(identity_before=dict(observation=dict(process_pair=pair)),
                work_started_ns=first, work_ended_ns=stop)
        self.assertFalse(_independent_work(receipt(10, 10, 20), receipt(12, 15, 25)))
        self.assertFalse(_independent_work(receipt(10, 10, 20), receipt(10, 21, 30)))
        for role in ('controller','native'):
            first,second=receipt(10,10,20),receipt(12,21,30)
            first_pair=first['identity_before']['observation']['process_pair']
            second['identity_before']['observation']['process_pair'][role]=first_pair[role]
            self.assertFalse(_independent_work(first,second))
        self.assertTrue(_independent_work(receipt(10, 10, 20), receipt(12, 21, 30)))


if __name__ == '__main__':
    unittest.main()
