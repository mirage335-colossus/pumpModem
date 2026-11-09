"""Calibration prediction uses useful GPU demand and held-out target CPU time."""
from pathlib import Path
import copy
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import triad_gpu_cpu_forecast as forecast


def component(point, lo, hi):
    return {forecast.RATIO:point, forecast.INTERVAL:[lo,hi],
            'model_status':'abbreviated_holdout_supported'}


class ForecastTests(unittest.TestCase):
    def test_acquisition_uses_cpu_fit_and_keeps_last_local_sample_outside_fit(self):
        demand = dict(status='acquired', coarse={'fft_products':48}, helper_reason='helper_disabled')
        cpu = dict(component(.45,.1,.9), coverage={'status':'acquired','coordinates':[[0,0,0,0]]}, acquisition_work_class=demand,
                   samples=[{forecast.RATIO:x} for x in (.2,.4,.8)])
        gpu = dict(status='acquired', coordinates=[[0,0,0,0]], acquisition_work_class=demand, elapsed_seconds=1.e-6)
        result = forecast._acquisition(cpu,gpu)
        self.assertAlmostEqual(result[forecast.RATIO],.3)
        self.assertEqual(result['independent_local_holdout'][forecast.RATIO],.8)
        self.assertFalse(result['independent_local_holdout']['accepted'])
        self.assertEqual(result['model_status'],'unresolved')
        gpu['acquisition_work_class'] = dict(demand,coarse={'fft_products':49})
        with self.assertRaisesRegex(ValueError,'coarse demand differs'):
            forecast._acquisition(cpu,gpu)

    def test_fine_estimate_charges_gpu_work_through_actual_public_slot(self):
        fs, samples, slot = 10, 100, 20.
        cache = {key:0 for key in forecast._CACHE}
        cache.update(calls=30,interpolated_rows=100,cache_hits=200)
        records, counters = [], []
        for count in (20,30,40,60):
            regime = {key:1 for key in forecast._ADAPTIVE_REGIME}
            lanes = {name:dict(work_counts={'basis_rows':100*count},state_work_regime=regime,
                pruning_events=0,states_dropped=0) for name in ('initial_affine','adaptive')}
            work = dict(consumed_samples=count,processed_rows=count,blocks=count,segments=count,
                additional_discarded_rows=0,requested_invalid_rows=0,
                retained_states={name:[[0,0,0]] for name in lanes},lanes=lanes,native_cache=cache)
            records.append(dict(work=work,lane_scores={name:[0.,1.] for name in lanes}))
            cpu_work = copy.deepcopy(work)
            for name in lanes:
                cpu_work['lanes'][name]['bit_scores'] = [0.,1.]
            counters.append(dict(cpu_work,native_templates={'cache':cache}))
        records.append(dict(work=dict(records[-1]['work'],physical_first_sample=4,
                                      probe_endpoint={'classification':'AMBIGUOUS'})))
        row = dict(status='complete_work_observation',geometry={'supported':True},
            continued_cache_boundary_work={'supported':True},
            prefix={'counters':counters[0],forecast.RATIO:.5},
            checkpoints=[{'counters':c,forecast.RATIO:v} for c,v in zip(counters[1:],(.25,.25,.3))],
            finish_classify_close={'endpoint':'AMBIGUOUS',forecast.RATIO:.1})
        cpu = dict(observations=[row,row,row],public_origin_sample=4,public_probe_samples=60,
            fine_component=component(.26,.22,.35))
        geometry = dict(sample_rate_hz=fs,search_window_samples=samples,
                        conservative_public_slot_interval_seconds=slot)
        result = forecast._nominal(cpu,{'records':records},geometry)
        self.assertAlmostEqual(result[forecast.RATIO],.25)
        self.assertAlmostEqual(result['independent_local_holdout'][forecast.RATIO],.27)
        self.assertEqual(result['gpu_demand'],{'prefix_rows':20,'continuation_rows':80})
        self.assertTrue(result['independent_local_holdout']['accepted'])
        for record in records[:4]:
            record['work']['lanes']['adaptive']['work_counts']['basis_rows'] = 103*record['work']['processed_rows']
        with self.assertRaisesRegex(ValueError,'lacks a supported precision/work reference'):
            forecast._nominal(cpu,{'records':records},geometry)
        approximate = forecast._nominal(cpu,{'records':records},geometry,allow_approximation=True)
        discrepancy = approximate['gpu_work_discrepancy']
        self.assertFalse(discrepancy['exact_work_match'])
        self.assertEqual(discrepancy['relative_cost_scale_interval'],[1.,1.03])
        self.assertGreater(approximate[forecast.INTERVAL][1],result[forecast.INTERVAL][1])
        records[3]['work']['retained_states']['initial_affine'][0][0]=1
        with self.assertRaisesRegex(ValueError,'fixed-affine retained states'):
            forecast._nominal(cpu,{'records':records},geometry,allow_approximation=True)

    def test_partial_gpu_frequency_work_cannot_supply_a_complete_estimate(self):
        demand = dict(status='partial_acquisition', coarse={}, helper_reason='no_coarse_pilot')
        plan = {key:1 for key in forecast._TILE}
        plan.update(units=50,frequency_unit_points=4,frequency_points=200)
        cpu = dict(acquisition_work_class=demand,work_plan=plan)
        gpu = dict(acquisition_work_class=demand,status='partial_acquisition',full_plan=plan,
                   bounded_tiles=[{'cumulative_frequency_points':4}])
        with self.assertRaisesRegex(ValueError,'1/2/4 frequency tiles'):
            forecast._acquisition(cpu,gpu)

    def test_lag_acquisition_preserves_exact_cpu_partition_kernel_charge(self):
        def acquisition(workers,products):
            return dict(acquisition_work_class={'coarse':dict(correlation_method='lag',
                workspaces=78,arrival_points=71,frequency_points=1474,fft_products=products)},
                frequency_intervals=[[-8.,8.]], work={'workers':workers})
        local = acquisition(12,22164)
        result = forecast._acquisition_demand_match(local,acquisition(1,22153))
        self.assertEqual(result['replicated_cached_kernel_ffts'],11)
        for products in (22152,22154,33229):
            with self.subTest(products=products), self.assertRaisesRegex(ValueError,'cached-kernel replication'):
                forecast._acquisition_demand_match(local,acquisition(1,products))
        with self.assertRaisesRegex(ValueError,'coarse demand differs'):
            forecast._acquisition_demand_match(local,dict(acquisition(1,22153),frequency_intervals=[[-8.,7.]]))

    def test_declared_reference_without_executed_row_is_not_coverage(self):
        policy = dict(paired_reference_case_indices=[3,4,6],score_atol=2e-3,score_rtol=2e-4,
                      adaptive_cost_driver_relative_limit=.05)
        gpu = {'policy':policy,'cases':[]}
        parsed = {'cases':[{'index':n,'configuration_hash':str(n)} for n in (3,4,6)]}
        with self.assertRaisesRegex(ValueError,'reference coverage'):
            forecast._reference_coverage(gpu,parsed,expected=[3,4])
        with self.assertRaises(KeyError):
            forecast._reference_coverage(gpu,parsed,expected=[3,4,6])

    def test_unresolved_gpu_execution_cannot_publish_a_calibrated_prediction(self):
        source = dict(revision='rev', sources={'receiver.py':'source'}, native_binary_sha256='binary',
                      native_artifacts={'kernel':'artifact'})
        runtime, budget = {'torch_threads':1}, {'available_cpus':12}
        case = dict(index=1, heading='case', configuration_hash='config')
        cpu = dict(schema='triad-manual-complete-capacity-v3',
            provenance=dict(source,unchanged_at_completion=True),
            runtime=dict(runtime,unchanged_at_completion=True),
            input={'source_sha256':'list','unchanged_at_completion':True},
            message_gate={'passed':True}, machine={'cpu':budget}, cases=[{'case':case}])
        gpu = dict(schema='triad-gpu-workload-v2',status='unresolved',unchanged_at_completion=True,
                   device='cuda',coarse_device='cuda',cuda_peak_allocated_bytes=100,cases=[{'case':case}])
        result = forecast.estimate(cpu,gpu,{'cases':[case],'source_sha256':'list'},source,runtime,budget)
        self.assertEqual(result['status'],'unresolved')
        self.assertEqual(result['nominal_point_estimates'],0)
        self.assertIn('no qualified mixed-precision work reference',result['cases'][0]['reason'])

    def test_transfer_requires_same_native_domain_and_only_removes_encryption(self):
        old = dict(fields={'Encryption':True,'FHSS':'off'},native_args=['--case','hf','--encrypt','on'])
        new = dict(fields={'Encryption':False,'FHSS':'off'},native_args=['--case','hf','--encrypt','off'])
        forecast._transfer_domain(old,new)
        with self.assertRaisesRegex(ValueError,'configuration edits'):
            forecast._transfer_domain(new,old)
        with self.assertRaisesRegex(ValueError,'native argument domain'):
            forecast._transfer_domain(old,dict(new,native_args=[*new['native_args'],'--rate','1']))
        limits = forecast._work_envelope([dict(exact_work=False,ratios={
            'native.full_two_bit_tile_fills':.5,'adaptive.basis_rows':1.01})],True,transfer=True)
        self.assertEqual(limits['relative_cost_scale_interval'],[0.,1.01])
        self.assertAlmostEqual(limits['relative_point_scale'],1.01**.5)
        with self.assertRaisesRegex(ValueError,'outside declared approximation envelope'):
            forecast._driver_ratios({'native.full_two_bit_tile_fills':4},
                                    {'native.full_two_bit_tile_fills':1})

    def test_transfer_withholds_all_target_cpu_timings_from_prediction(self):
        donor = dict(index=5,heading='encrypted',configuration_hash='5',
            fields={'Encryption':True},native_args=['--encrypt','on'])
        target = dict(index=7,heading='clear',configuration_hash='7',
            fields={'Encryption':False},native_args=['--encrypt','off'])
        source = dict(revision='rev',sources={},native_binary_sha256='binary',native_artifacts={})
        runtime, budget = {'torch_threads':1}, {'available_cpus':12}
        local5 = {'case':donor,'measurement':{'inference_workers':12}}
        local7 = {'case':target,'measurement':{'inference_workers':12,
            'nominal_service':{forecast.RATIO:9.}, 'complete_capacity':{forecast.RATIO:10.}}}
        cpu = dict(schema='triad-manual-complete-capacity-v3',
            provenance=dict(source,unchanged_at_completion=True),
            runtime=dict(runtime,unchanged_at_completion=True),workspace_limit_bytes=4096,
            input={'source_sha256':'old-list','unchanged_at_completion':True},
            message_gate={'passed':True},machine={'cpu':budget},cases=[local5,local7])
        gpu7 = {'case':target}
        gpu = dict(schema='triad-gpu-workload-v2',status='bounded_mixed_precision_cost_regime_observed',
            unchanged_at_completion=True,device='cuda',coarse_device='cuda',cuda_peak_allocated_bytes=100,
            memory_limit_bytes=4096,input={'source_sha256':'list'},sources={'triad_gpu_workload.py':'driver'},
            cases=[{'case':donor},gpu7])
        holdout = dict(gpu,device='cpu',status='cpu_reference_only',runtime=runtime,cpu_budget=budget,
            workers=12,cases=[{'case':target}])
        predicted = dict(missing_or_mismatched={},nominal_service={forecast.RATIO:.25,'realtime_status':'pass'},
            complete_capacity={forecast.RATIO:.9,'all_component_models_supported':True,'realtime_status':'pass'},
            components={})
        with mock.patch.object(forecast,'_trace_source_matches',return_value=True), \
             mock.patch.object(forecast,'_runtime_leaf_hashes',return_value=gpu['sources']), \
             mock.patch.object(forecast,'_reference_coverage',return_value=[3,4,6]), \
             mock.patch.object(forecast,'_gpu_work_executed'), \
             mock.patch.object(forecast,'_forecast_row',side_effect=lambda *_,**kw: copy.deepcopy(predicted)) as fit, \
             mock.patch.object(forecast,'_verification',return_value={
                 'status':'accepted','actual_nominal_s_per_s':.24}) as verify:
            result = forecast.estimate(cpu,gpu,{'cases':[donor,target],'source_sha256':'list'},
                source,runtime,budget,holdout=holdout,holdout_indices=[7],transfers=[(7,5)])
            self.assertIs(fit.call_args.args[0],local5)
            self.assertIs(fit.call_args.args[1],gpu7)
            self.assertIsNone(fit.call_args.kwargs['target_noise'])
            self.assertIs(verify.call_args.args[1],local5)
            noise = {'limited_to_noise_stage':True}
            geometry = dict.fromkeys(forecast._GEOMETRY,1)
            evaluator = {'seed':8}
            local7.update(calibration_inputs=evaluator, public_geometry=geometry)
            local7['measurement'].update(inference_allocation_mode='full_logical',
                workspace_margin={'status':'within_budget'},noise_only_fine=noise)
            gpu7.update(evaluator=evaluator,public_geometry=geometry)
            cpu['input']['source_sha256'] = 'list'
            calibrated = forecast.estimate(cpu,gpu,{'cases':[donor,target],'source_sha256':'list'},
                source,runtime,budget,holdout=holdout,holdout_indices=[7],transfers=[(7,5)],
                target_noise_indices=[7])
            self.assertIs(fit.call_args.args[0],local5)
            self.assertIs(fit.call_args.kwargs['target_noise'],noise)
            self.assertIs(verify.call_args.kwargs['target_noise'],noise)
        transfer = result['cases'][1]['cross_case_calibration']
        self.assertTrue(transfer['target_cpu_timing_excluded_from_fit'])
        self.assertEqual(transfer['donor_manual_list_sha256'],'old-list')
        self.assertEqual(transfer['target_manual_list_sha256'],'list')
        self.assertIn('explicit validated donor',result['cases'][0]['reason'])
        self.assertEqual(transfer['withheld_complete_target_comparison']['nominal_service']['direct_target_s_per_s'],9.)
        target_result = result['cases'][1]
        self.assertEqual(target_result['nominal_service'][forecast.RATIO],.25)
        self.assertEqual(target_result['nominal_service']['realtime_status'],'unresolved')
        self.assertEqual(target_result['status'],'unresolved')
        self.assertEqual(target_result['held_out_target_contradictions']['nominal_service']['forecast_verdict'],'pass')
        target_noise_transfer = calibrated['cases'][1]['cross_case_calibration']
        self.assertFalse(target_noise_transfer['target_cpu_timing_excluded_from_fit'])
        self.assertTrue(target_noise_transfer['target_cpu_timing_excluded_from_nominal_fit'])
        self.assertEqual(target_noise_transfer['target_cpu_calibrated_stages'],['noise_only'])
        self.assertIn('target_comparison_with_noise_calibration',target_noise_transfer)

    def test_holdout_digests_distinguish_same_seed_from_identical_input(self):
        cpu = dict(evaluator={'seed':2},input_sha256='local',validity_sha256='mask')
        self.assertEqual(forecast._holdout_input_relation(cpu,cpu)['status'],'identical_input')
        relation = forecast._holdout_input_relation(cpu,dict(cpu,input_sha256='remote'))
        self.assertEqual(relation['status'],'same_evaluator_distinct_input_bytes')
        self.assertEqual(relation['cpu_input_sha256'],'local')
        self.assertEqual(relation['gpu_input_sha256'],'remote')
        with self.assertRaisesRegex(ValueError,'target validity differs'):
            forecast._holdout_input_relation(cpu,dict(cpu,validity_sha256='other-mask'))

    def test_target_timing_may_contradict_a_fail_even_with_small_residual(self):
        row = dict(status='conditional_estimate',
            nominal_service={forecast.RATIO:1.01,'realtime_status':'fail'},
            complete_capacity={forecast.RATIO:1.05,'realtime_status':'fail'},
            independent_cpu_trace={'actual_nominal_s_per_s':.99})
        forecast._gate_target_verdicts(row,{'complete_capacity':{'realtime_status':'pass'}})
        self.assertEqual(row['status'],'unresolved')
        self.assertEqual(row['nominal_service'][forecast.RATIO],1.01)
        self.assertEqual(set(row['held_out_target_contradictions']),{'nominal_service','complete_capacity'})


if __name__ == '__main__':
    unittest.main()
