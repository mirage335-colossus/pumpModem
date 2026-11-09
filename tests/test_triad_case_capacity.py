"""Absolute capacity cannot pass missing stages or duplicate physical exposure."""
from contextlib import nullcontext
from pathlib import Path
import sys
import numpy as np
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import triad_case_capacity as capacity
from triad_case_capacity import (capacity_status, cap_transition_coverage,
                                 combine_components, public_calendar, _larger_component)


def component(point, low=None, high=None):
    return dict(ratio_processing_seconds_per_baseband_second=point,
        ratio_processing_seconds_per_baseband_second_interval=[point if low is None else low, point if high is None else high],
        model_status='abbreviated_holdout_supported')


class CapacityTests(unittest.TestCase):
    def test_noise_stage_calibration_never_drains_partial_coarse_search(self):
        receiver, session = object(), Mock()
        coordinates = ((1,0.,0.,0.),)
        prepared = SimpleNamespace(observation_work_stratum='proper_complex_noise_only',
            receiver=Mock(return_value=nullcontext(receiver)), memory_limit_bytes=512,
            stream_workspace={'simultaneous_outer_reserve_bytes':100},
            acquisition=Mock(return_value={'result':dict(status='partial_acquisition',coarse_session=session)}))
        summary = dict(all_observations_complete=True,nonzero_templates_in_every_probe=True,
                       work_samples=[{'workspace_bytes':370}])
        with patch.object(capacity,'public_work_coordinates',return_value=coordinates), \
             patch.object(capacity,'measure_fine_stratum',return_value=summary) as measure:
            result = capacity.measure_noise_only_fine_calibration(prepared,12,family_size=8)
        prepared.receiver.assert_called_once_with(12)
        prepared.acquisition.assert_called_once_with(receiver)
        session.close.assert_called_once_with()
        session.step.assert_not_called()
        measure.assert_called_once_with(prepared,receiver,coordinates,stratum='active',family_size=8)
        self.assertEqual(result['status'],'stage_calibration_measured')
        self.assertEqual(result['workspace_margin']['estimated_peak_bytes'],470)
        self.assertIs(result['noise_only_fine'],summary)
        self.assertNotIn('complete_capacity',result)

    def test_acquisition_work_drift_withholds_timing_model(self):
        prepared = SimpleNamespace(acquisition=Mock(side_effect=[
            dict(result={'status': 'acquired', 'coordinates': ((0, 0., 0., 0.),)},
                 ratio_processing_seconds_per_baseband_second=.2),
            dict(result={'status': 'acquired', 'coordinates': ((1, 0., 0., 0.),)},
                 ratio_processing_seconds_per_baseband_second=.2)]))
        actual = capacity.measure_acquisition(prepared, object(), family_size=8)
        self.assertEqual(prepared.acquisition.call_count, 2)
        self.assertEqual(actual['model_status'], 'unresolved')
        self.assertEqual(actual['reason'], 'acquisition_work_changed_between_looks')

    def test_continuation_stopping_uses_precision_without_raw_realtime_decision(self):
        from triad_case_metrics import summarize_service
        # These per-window rates would stop the generic service sampler on
        # either side of 1 s/s before charging real slot cadence and stages.
        high = summarize_service([[1.1, 1.2, 1.15], [1.3, 1.2, 1.25], [1.2, 1.3, 1.2]])
        self.assertEqual(high['stop_reason'], 'point_at_or_above_realtime_limit')
        self.assertIsNone(capacity.continuation_look(high))
        self.assertEqual(capacity.continuation_look(high,maximum_look=3),'bounded_limit')
        low = dict(model_status='abbreviated_holdout_supported', relative_precision_fraction=.25,
            outer_trials=5, stop=True, stop_reason='upper_interval_below_limit_and_holdout_supported')
        self.assertIsNone(capacity.continuation_look(low))
        self.assertEqual(capacity.continuation_look(dict(low, relative_precision_fraction=.1)),
            'relative_precision')
        self.assertEqual(capacity.continuation_look(dict(low, outer_trials=9)), 'bounded_limit')

    def test_full_logical_inference_requires_a_valid_pilot_observation(self):
        pilot = dict(selected_workers=6, samples=[dict(workers=6, selection_ratio=.8),
                                                  dict(workers=12)])
        prepared = SimpleNamespace(receiver=Mock())
        with patch.object(capacity, 'choose_allocation', return_value=pilot):
            actual = capacity.measure_complete_case(prepared, [1, 6, 12], family_size=8,
                inference_workers=12, progress=lambda message:None)
        prepared.receiver.assert_not_called()
        self.assertEqual(actual['reason'], 'declared_full_logical_allocation_not_measured')

    def test_sustained_allocation_probe_deduplicates_and_bounds_alternatives(self):
        receiver=object()
        coordinates=((0,0.,0.,0.),)
        acquired=dict(status='acquired',coordinates=coordinates)
        prepared=SimpleNamespace(receiver=Mock(side_effect=lambda count:nullcontext(receiver)),
            acquisition=Mock(return_value=dict(result=acquired)))
        selected={}
        alternate=dict(observations=[{}, {}, {}])
        match=(dict(observations=3), {'public_input_and_dsp_work':'same'}, np.zeros(2))
        targets=dict(physical_workers=6,logical_workers=12)
        with patch.object(capacity,'measure_sustained_fine',return_value=alternate) as fine, patch.object(
                capacity,'_allocation_fine_sample',side_effect=[match,match]):
            report=capacity.measure_sustained_allocation_probe(prepared,[1,2,4,6,12],6,coordinates,selected,
                targets=targets,family_size=192)
        self.assertEqual(report['status'],'matched_exploratory_measurements')
        self.assertEqual([row['workers'] for row in report['comparisons']],[6,12])
        self.assertEqual(report['extra_repeated_baseband_probe_seconds_maximum'],18)
        self.assertEqual(report['extra_observations_attempted'],3)
        self.assertEqual(report['extra_acquisition_attempts'],1)
        prepared.receiver.assert_called_once_with(12)
        fine.assert_called_once_with(prepared,receiver,coordinates,family_size=192,maximum_look=3)

    def test_sustained_allocation_probe_refuses_partial_search_and_changed_work(self):
        receiver=object()
        coordinates=((0,0.,0.,0.),)
        acquired=dict(status='acquired',coordinates=coordinates)
        partial=Mock()
        prepared=SimpleNamespace(receiver=lambda count:nullcontext(receiver),
            acquisition=Mock(side_effect=[dict(result=dict(status='partial_acquisition',coarse_session=partial)),
                dict(result=acquired)]))
        selected={}
        alternate=dict(observations=[{}, {}, {}])
        with patch.object(capacity,'measure_sustained_fine',return_value=alternate) as fine, patch.object(
                capacity,'_allocation_fine_sample',side_effect=[
                    ({},{'work':'same'},np.zeros(2)), ({},{'work':'different'},np.zeros(2))]):
            report=capacity.measure_sustained_allocation_probe(prepared,[1,2,4,6,12],4,coordinates,selected,
                targets=dict(physical_workers=6,logical_workers=12),family_size=192)
        self.assertEqual(report['status'],'unresolved')
        self.assertEqual(report['extra_repeated_baseband_probe_seconds_maximum'],36)
        self.assertEqual([row.get('reason') for row in report['comparisons']],[
            None,'actual_complete_acquisition_or_proposals_differ','continued_state_cache_or_dsp_work_differs'])
        partial.close.assert_called_once()
        fine.assert_called_once_with(prepared,receiver,coordinates,family_size=192,maximum_look=3)

    def test_global_pass_requires_current_sources_full_list_and_worker_comparison(self):
        from copy import deepcopy
        report=dict(provenance=dict(unchanged_at_completion=True),
            input=dict(unchanged_at_completion=True),message_gate=dict(passed=True),
            selection=dict(complete_current_list=True,complete_worker_comparison=True),
            cases=[dict(measurement=dict(complete_capacity=dict(realtime_status='pass')))])
        self.assertEqual(capacity_status(report),'conditional_capacity_pass')
        for section,key in (('selection','complete_current_list'),
                ('selection','complete_worker_comparison'),('message_gate','passed'),
                ('provenance','unchanged_at_completion'),('input','unchanged_at_completion')):
            changed=deepcopy(report);changed[section][key]=False
            self.assertNotEqual(capacity_status(changed),'conditional_capacity_pass')
        report['cases'][0]['measurement']['complete_capacity']['realtime_status']='fail'
        self.assertEqual(capacity_status(report),'capacity_failures')

    def test_missing_path_stratum_cannot_disappear_behind_measured_maximum(self):
        merged=_larger_component([component(.1),dict(model_status='unresolved')])
        self.assertEqual(merged['model_status'],'unresolved')
        self.assertEqual(combine_components({'fine':merged},complete_work_covered=True)['realtime_status'],
                         'unresolved')

    def test_state_ceiling_requires_real_boundary_work_in_both_lanes(self):
        from copy import deepcopy
        adaptive=dict(timing=dict(observations=2,predictions=1),
            state_work_regime=dict(initial_states=256,peak_post_observation_states=64),
            work_counts=dict(transition_edges=128,merged_channel_components=0))
        nominal=dict(timing=dict(predictions=1),
            state_work_regime=dict(initial_states=256,terminal_post_observation_states=256),
            work_counts=dict(transition_edges=256))
        result=dict(segments=2,lanes=dict(adaptive=adaptive,initial_affine=nominal))
        self.assertTrue(cap_transition_coverage(result)['supported'])
        for lane in ('adaptive','initial_affine'):
            changed=deepcopy(result)
            changed['lanes'][lane]['timing']['predictions']=0
            self.assertFalse(cap_transition_coverage(changed)['supported'])
        result['lanes']['adaptive']['state_work_regime']['peak_post_observation_states']=12
        self.assertFalse(cap_transition_coverage(result)['supported'])

    def test_elapsed_component_costs_are_summed_without_core_division(self):
        result=combine_components({'coarse':component(.06),'fine':component(.84),
            'bank':component(.1)},complete_work_covered=True)
        self.assertEqual(result['realtime_status'],'fail')
        self.assertAlmostEqual(result['ratio_processing_seconds_per_baseband_second'],1.)
        self.assertAlmostEqual(result['margin_fraction'],0.)

    def test_point_one_has_ninety_percent_margin_only_with_complete_work(self):
        for complete in (False,True):
            result=combine_components({'receiver':component(.1,.09,.11)},complete_work_covered=complete)
            self.assertAlmostEqual(result['margin_fraction'],.9)
            self.assertEqual(result['realtime_status'],'pass' if complete else 'unresolved')
        self.assertEqual(combine_components({},complete_work_covered=True)['realtime_status'],'unresolved')

    def test_missing_stage_or_failed_holdout_cannot_pass(self):
        result=combine_components({'fine':component(.1),'coarse':{}},complete_work_covered=True)
        self.assertEqual(result['realtime_status'],'unresolved')
        bad=component(.1);bad['model_status']='unresolved'
        self.assertEqual(combine_components({'fine':bad},complete_work_covered=True)['realtime_status'],'unresolved')

    def test_measured_complete_component_lower_bound_can_fail_partial_coverage(self):
        row=component(1.2);row['measured_complete_component_lower_bound']=1.1
        result=combine_components({'coarse':row,'fine':{}},complete_work_covered=False)
        self.assertEqual(result['realtime_status'],'fail')
        self.assertFalse(result['complete_work_covered'])
        self.assertEqual(result['covered_stage_subtotal_processing_seconds_per_baseband_second'],1.2)
        self.assertIsNone(result['ratio_processing_seconds_per_baseband_second'])
        self.assertIsNone(result['ratio_processing_seconds_per_baseband_second_interval'])
        self.assertIsNone(result['margin_fraction'])
        self.assertEqual(result['numeric_point_criterion'],'unresolved')

    def test_default_capacity_uses_sustained_fine_only_after_acquisition(self):
        observed = ((7, .01, .2, 0.),)
        cost_only = ((8, .02, .3, 0.),)
        receiver = object()
        sample = dict(workspace_bytes=100, rss={'sum_process_bytes':110},
            observed_cpu_utilization_fraction=.25, requested_invalid_rows=2,
            additional_discarded_rows=4, ratio_processing_seconds_per_baseband_second=.04)
        orchestration = dict(full_window_assembly=dict(
            nonoverlap_stream_orchestration_seconds_per_baseband_second=.01,
            represented_baseband_seconds=80., estimated_peak_bytes=100),
            message_state_and_codec=dict(component(.01), public_slot_plan_service=component(.01)))
        cases = (
            ('acquired', .2, True, True, 'below_one', 'pass'),
            ('acquired', .2, True, False, 'below_one', 'unresolved'),
            ('partial_acquisition', None, True, True, 'unresolved', 'unresolved'),
            ('acquired', 1.05, True, True, 'fail', 'fail'),
            ('acquired', 1.05, False, True, 'fail', 'unresolved'),
        )
        for status, point, model_supported, recurring_measured, point_status, capacity_status_expected in cases:
            with self.subTest(status=status, point=point, model_supported=model_supported,
                              recurring_measured=recurring_measured):
                acquisition = dict(status=status, coordinates=observed)
                banks = dict(component(.01), samples=[dict(preparation_peak_estimate_bytes=50)],
                    recurring_window=(dict(noise_only_acquisition=component(.015),
                        full_window_assembly=orchestration['full_window_assembly'],
                        sample_count=900, ingress_samples=1000) if recurring_measured else {}))
                prepared = SimpleNamespace(slot_seconds=100., window_seconds=80.,
                    memory_limit_bytes=1000, raw=SimpleNamespace(nbytes=40), valid=SimpleNamespace(nbytes=20),
                    receiver=Mock(side_effect=lambda workers:nullcontext(receiver)),
                    recurring_window=dict(window_samples=900, maximum_ingress_samples=1000),
                    stream_workspace=dict(simultaneous_outer_reserve_bytes=120),
                    fine_probe=Mock(return_value=sample))
                fine = component(point) if point is not None else {}
                if not model_supported:
                    fine['model_status'] = 'unresolved'
                continuation = Mock(return_value=dict(status='measured_continuation', fine_component=fine,
                    observations=[dict(workspace=dict(estimated_peak_bytes=400, rss={'sum_process_bytes':500}),
                        prefix={'observed_allocated_cpu_utilization_fraction':.6},
                        checkpoints=[{'observed_allocated_cpu_utilization_fraction':.8}])]))
                cap = dict(component(.15), full_window_forecast_multiplier=1.6, work_samples=[sample])
                if point is not None and point >= 1:
                    cap['model_status'] = 'unresolved'
                other = dict(component(.05), full_window_forecast_multiplier=.8, work_samples=[sample])
                cold = Mock(side_effect=[cap, other, other])
                with patch.multiple(capacity,
                        choose_allocation=Mock(return_value=dict(selected_workers=6,
                            samples=[dict(workers=12, selection_ratio=.9)])),
                        public_work_coordinates=Mock(return_value=cost_only),
                        measure_acquisition=Mock(side_effect=[dict(component(.01), coverage=acquisition),
                            dict(component(.01), coverage=dict(coordinates=cost_only))]),
                        measure_sustained_fine=continuation, measure_fine_stratum=cold,
                        _probe_work=lambda row:row, noise_only_observation=Mock(),
                        measure_recurring_banks=Mock(return_value=banks)), patch(
                            'triad_orchestration_cost.measure_orchestration', return_value=orchestration):
                    measured = capacity.measure_complete_case(prepared, [1,6,12], family_size=64,
                        inference_workers=12, progress=lambda message:None)
                self.assertEqual(measured['selected_workers'], 6)
                self.assertEqual(measured['inference_workers'], 12)
                self.assertTrue(all(call.args == (12,) for call in prepared.receiver.call_args_list))
                self.assertEqual(measured['recurring_window']['covers_longest_public_geometry'], recurring_measured)
                nominal = measured['nominal_service']
                self.assertEqual(nominal['numeric_point_criterion'], point_status)
                self.assertEqual(measured['complete_capacity']['realtime_status'], capacity_status_expected)
                self.assertEqual([(call.kwargs['stratum'],call.kwargs.get('work_probe',False))
                    for call in cold.call_args_list], [('active',True),('terminal',False),('active',False)])
                if status == 'acquired':
                    continuation.assert_called_once_with(prepared, receiver, observed, family_size=64)
                    self.assertEqual(nominal['components']['complete_fine_search'][
                        'ratio_processing_seconds_per_baseband_second'], point)
                    self.assertEqual(measured['workspace_margin']['estimated_peak_bytes'],520)
                    self.assertEqual(measured['workspace_margin']['observed_peak_bytes'],500)
                    self.assertEqual(measured['observed_allocated_cpu_utilization_fraction_range'],[.25,.8])
                else:
                    continuation.assert_not_called()
                    self.assertIsNone(nominal['ratio_processing_seconds_per_baseband_second'])
                    self.assertEqual(measured['fine_active']['reason'],'complete_public_acquisition_required')

    def test_invalid_ratio_is_not_a_margin(self):
        for value in (float('nan'),float('inf'),-1,True):
            with self.subTest(value=value),self.assertRaises(ValueError):
                combine_components({'fine':component(value)},complete_work_covered=True)

    def test_variable_public_slot_intervals_are_used_exactly(self):
        class Source:
            def slot_plan(self,index):
                starts=[0,600,1500,2100]
                return dict(index=index,sample_rate_hz=10,
                    source_begin_samples=starts[index],next_source_begin_samples=starts[index+1])
        result=public_calendar(Source(),slots=3)
        self.assertEqual(result['intervals_seconds'],[60.,90.,60.])
        self.assertEqual(result['minimum_interval_seconds'],60.)
        self.assertEqual(result['maximum_interval_seconds'],90.)

    def test_stream_workspace_reserves_overlap_receipts_and_assembled_input_together(self):
        calendar = {'plans': [{'receiver_window_samples': n} for n in (800000, 950000, 900000)]}
        with self.assertRaisesRegex(MemoryError, 'production stream input buffer'):
            capacity._production_stream_workspace(calendar, 1000000, 950000*68-1)
        budget = 96*1024**2
        bound = capacity._production_stream_workspace(calendar, 1000000, budget)
        self.assertGreater(bound['retained_input_bound_bytes'], 950000*17)
        self.assertEqual(bound['assembled_input_bound_bytes'], 1000000*17)
        self.assertEqual(bound['bank_limit_bytes']+bound['assembled_input_bound_bytes']+
                         bound['simultaneous_outer_reserve_bytes'], budget)

    def test_calendar_does_not_hide_a_discontinuity(self):
        class Source:
            def slot_plan(self,index):
                return dict(index=index,sample_rate_hz=10,
                    source_begin_samples=index*10,next_source_begin_samples=index*10+11)
        with self.assertRaises(ValueError):public_calendar(Source(),slots=2)


if __name__=='__main__':unittest.main()
