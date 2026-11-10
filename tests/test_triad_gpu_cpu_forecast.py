"""Calibration prediction uses useful GPU demand and held-out target CPU time."""
from pathlib import Path
import copy
from contextlib import nullcontext, redirect_stderr, redirect_stdout
import hashlib
import io
import json
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import triad_gpu_cpu_forecast as forecast


def component(point, lo, hi):
    return {forecast.RATIO:point, forecast.INTERVAL:[lo,hi],
            'model_status':'abbreviated_holdout_supported'}


class ForecastTests(unittest.TestCase):
    def _applicability_fixture(self):
        before = dict(revision='revision', sources={name:'old' for name in forecast._APPLICABILITY_PATHS},
                      native_binary_sha256='exporter',native_artifacts={'kernel':'native'})
        before['sources']['tools/triad_receiver.py']='unchanged-dsp'
        after=copy.deepcopy(before)
        for name in forecast._APPLICABILITY_PATHS: after['sources'][name]='new'
        runtime,budget={'numerical_threads':1},{'available_cpus':12}
        case=dict(index=1,heading='first',configuration_hash='configuration')
        cpu=dict(schema='triad-manual-complete-capacity-v3',capacity_scope='configured-public-calendar',
            provenance=dict(before,unchanged_at_completion=True),runtime=dict(runtime,unchanged_at_completion=True),
            machine={'cpu':budget},input={'source_sha256':'manual','unchanged_at_completion':True},
            message_gate={'passed':True},workspace_limit_bytes=4096,worker_allocations=[1,6,12,24],
            cases=[dict(case=case,measurement=dict(inference_workers=24,inference_allocation_mode='automatic_receiver'))])
        raw=json.dumps(cpu).encode()
        identities=[{k:identity[k] for k in forecast._IDENTITY_FIELDS} for identity in (before,after)]
        receipt=dict(schema='triad-cpu-calibration-applicability-v1',status='independently_reviewed_applicable',
            scope=forecast._APPLICABILITY_SCOPE,cpu_calibration_reports=[{'sha256':hashlib.sha256(raw).hexdigest()}],
            measured_identity=identities[0],applicable_identity=identities[1],
            changed_source_paths=sorted(forecast._APPLICABILITY_PATHS),
            independent_audit=dict(status='completed',conclusion='unchanged_measured_work',independent=True,
                reviewer='independent source reviewer',
                source_pair_sha256=hashlib.sha256(json.dumps(identities,sort_keys=True,separators=(',',':')).encode()).hexdigest(),
                findings={key:'Reviewed exact unchanged finite measured dependency.' for key in forecast._APPLICABILITY_REVIEW}),
            calibration_invariants=dict(runtime=runtime,cpu_budget=budget,workspace_limit_bytes=4096,
                worker_allocations=cpu['worker_allocations'],manual_list_sha256='manual',
                inference_workers=24,inference_allocation_mode='automatic_receiver'))
        gpu=dict(schema='triad-gpu-workload-v2',status='bounded_mixed_precision_cost_regime_observed',
            unchanged_at_completion=True,device='cuda',coarse_device='cuda',cuda_peak_allocated_bytes=1,
            shared_sources=after['sources'],native_binary_sha256='exporter',mapped_kernel_sha256='native',
            memory_limit_bytes=4096,input={'source_sha256':'manual'},
            sources={forecast._GPU_RUNTIME_LEAVES[0]:'gpu-driver'},cases=[{'case':case}])
        return cpu,raw,receipt,before,after,gpu,runtime,budget,dict(source_sha256='manual',cases=[case])

    def test_applicability_cli_preserves_numbers_and_strict_trace_gates(self):
        cpu,raw,receipt,before,after,gpu,runtime,budget,parsed=self._applicability_fixture()
        predicted=dict(missing_or_mismatched={},components={},
            nominal_service=dict(component(.2,.15,.25),realtime_status='pass'),
            complete_capacity=dict(component(.4,.3,.5),realtime_status='pass',all_component_models_supported=True))
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            def run(*,source=after,proof=receipt,cpu_bytes=raw,gpu_report=gpu,holdout=None):
                (root/'cpu.json').write_bytes(cpu_bytes)
                (root/'gpu.json').write_text(json.dumps(gpu_report))
                args=['--cpu-report',str(root/'cpu.json'),'--gpu-report',str(root/'gpu.json'),'--output',str(root/'out.json')]
                if proof is not None:
                    (root/'proof.json').write_text(json.dumps(proof))
                    args+=['--cpu-calibration-applicability',str(root/'proof.json')]
                if holdout is not None:
                    (root/'holdout.json').write_text(json.dumps(holdout))
                    args+=['--local-holdout-report',str(root/'holdout.json'),'--holdout-case-index','1']
                with mock.patch.object(forecast,'load_cases',return_value=parsed), \
                     mock.patch.object(forecast,'identity',return_value=source), \
                     mock.patch('triad_cpu.detect_cpu_budget',return_value=budget), \
                     mock.patch('triad_cpu.configure_numerical_threads'), \
                     mock.patch('triad_abbreviated.environment',return_value=runtime), \
                     mock.patch.object(forecast,'_runtime_leaf_hashes',return_value=gpu['sources']), \
                     mock.patch.object(forecast,'_reference_coverage',return_value=[1]), \
                     mock.patch.object(forecast,'_gpu_work_executed',return_value=('mixed','mixed')), \
                     mock.patch.object(forecast,'_forecast_row',side_effect=lambda *a,**k:copy.deepcopy(predicted)), \
                     mock.patch.object(forecast,'_verification',return_value={'status':'accepted'}), \
                     redirect_stdout(io.StringIO()):
                    code=forecast.main(args)
                return code,json.loads((root/'out.json').read_text())
            old_gpu=dict(gpu,shared_sources=before['sources'])
            baseline_code,baseline=run(source=before,proof=None,gpu_report=old_gpu)
            code,bridged=run()
            self.assertEqual((baseline_code,code),(0,0))
            self.assertEqual(bridged['cases'],baseline['cases'])
            self.assertEqual(bridged['cpu_calibration_applicability']['cpu_calibration_report_sha256'],hashlib.sha256(raw).hexdigest())
            self.assertIn('historical measured-source',bridged['cpu_calibration_applicability']['limitation'])
            for proof,bytes_,trace in ((None,raw,gpu),(receipt,raw+b'\n',gpu),(receipt,raw,old_gpu)):
                with self.subTest(proof=proof is not None,changed_bytes=bytes_!=raw,old_gpu=trace==old_gpu):
                    code,result=run(proof=proof,cpu_bytes=bytes_,gpu_report=trace)
                    self.assertEqual(code,2)
                    self.assertEqual(result['cases'][0]['status'],'unresolved')
                    self.assertNotIn('nominal_service',result['cases'][0])
            for field,value in (('scope','automatic-pipeline'),
                                ('measured_identity',dict(receipt['measured_identity'],native_binary_sha256='other')),
                                ('applicable_identity',dict(receipt['applicable_identity'],native_artifacts={'kernel':'other'})),
                                ('independent_audit',dict(receipt['independent_audit'],status='pending'))):
                with self.subTest(bad_receipt_field=field):
                    bad=dict(receipt,**{field:value})
                    code,result=run(proof=bad)
                    self.assertEqual(code,2)
                    self.assertNotIn('nominal_service',result['cases'][0])
            holdout=dict(gpu,device='cpu',status='cpu_reference_only',runtime=runtime,cpu_budget=budget,workers=24)
            self.assertEqual(run(holdout=holdout)[0],0)
            holdout['shared_sources']=before['sources']
            code,result=run(holdout=holdout)
            self.assertEqual(code,2)
            self.assertIn('provenance/allocation differs',result['cases'][0]['independent_cpu_trace']['reason'])
            self.assertEqual(result['cases'][0]['nominal_service']['realtime_status'],'unresolved')

    def test_applicability_rejects_changed_identity_scope_configuration_and_unreviewed_audit(self):
        cpu,raw,receipt,before,after,*_=self._applicability_fixture()
        edits=[lambda r:r.update(scope='automatic-pipeline'),
               lambda r:r['measured_identity']['sources'].update({'tools/triad_receiver.py':'different'}),
               lambda r:r['applicable_identity'].update(native_binary_sha256='different'),
               lambda r:r['applicable_identity']['native_artifacts'].update(kernel='different'),
               lambda r:r['independent_audit'].update(status='pending'),
               lambda r:r['independent_audit'].update(source_pair_sha256='different'),
               lambda r:r['independent_audit']['findings'].pop('invoked_globals'),
               lambda r:r['calibration_invariants'].update(workspace_limit_bytes=8192),
               lambda r:r['calibration_invariants'].update(inference_workers=12),
               lambda r:r['cpu_calibration_reports'].clear()]
        for edit in edits:
            bad=copy.deepcopy(receipt);edit(bad)
            with self.subTest(edit=edits.index(edit)),self.assertRaises((ValueError,KeyError)):
                forecast._cpu_calibration_applicability(cpu,after,raw,json.dumps(bad).encode())
        changed=copy.deepcopy(after);changed['sources']['tools/triad_receiver.py']='changed-dsp'
        bad=copy.deepcopy(receipt);bad['applicable_identity']['sources']=changed['sources']
        bad['changed_source_paths'].append('tools/triad_receiver.py')
        with self.assertRaisesRegex(ValueError,'two reviewed pipeline'):
            forecast._cpu_calibration_applicability(cpu,changed,raw,json.dumps(bad).encode())

    def test_applicability_does_not_complete_interrupted_calibration_or_audit_union(self):
        cpu,raw,receipt,before,after,gpu,runtime,budget,parsed=self._applicability_fixture()
        for incomplete in (dict(cpu,schema='triad-audited-union-v1'),
                           dict(cpu,provenance=before),dict(cpu,message_gate={'passed':False})):
            result=forecast.estimate(incomplete,gpu,parsed,after,runtime,budget,
                cpu_report_bytes=raw,cpu_calibration_applicability=json.dumps(receipt).encode())
            self.assertEqual(result['cases'][0]['status'],'unresolved')
            self.assertNotIn('cpu_calibration_applicability',result)

    def test_applicability_is_excluded_from_measured_cpu_source_closure(self):
        import triad_case_suite
        self.assertIn('tools/triad_gpu_cpu_forecast.py',triad_case_suite.GPU_ONLY_SOURCES)
        self.assertNotIn('tools/triad_gpu_cpu_forecast.py',triad_case_suite.measured_source_hashes())

    def test_receiver_allocation_requires_exact_hardware_or_automatic_target(self):
        for hardware in (1, 3, 12, 32):
            for mode, workers in (('full_logical', hardware),
                                  ('automatic_receiver', 2*hardware)):
                with self.subTest(hardware=hardware, mode=mode):
                    measured = dict(inference_allocation_mode=mode, inference_workers=workers)
                    self.assertEqual(forecast._calibrated_workers(measured, hardware), workers)
                    for wrong in (workers+1, hardware if mode=='automatic_receiver' else 2*hardware, True, None):
                        with self.assertRaises(ValueError):
                            forecast._calibrated_workers(dict(measured, inference_workers=wrong), hardware)
        for mode in ('pilot_selected', 'explicit', 'unknown', None):
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                forecast._calibrated_workers(dict(inference_allocation_mode=mode, inference_workers=18), 12)

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
        cache.update(calls=30,interpolated_rows=100,cache_hits=200,row_tile_references=240)
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

    def test_fine_demand_uses_row_references_not_physical_cache_lookups(self):
        row = {'lanes':{'adaptive':{'work_counts':{'basis_rows':100}}}}
        cache = dict(calls=3,interpolated_rows=100,cache_hits=20,row_tile_references=120)
        expected = forecast._drivers(row,cache)
        self.assertEqual(expected['cache.row_tile_references'],120)
        self.assertNotIn('cache.cache_hits',expected)
        self.assertEqual(expected,forecast._drivers(row,dict(cache,cache_hits=200)))
        self.assertNotEqual(expected,forecast._drivers(row,dict(cache,row_tile_references=121)))
        del cache['row_tile_references']
        with self.assertRaises(KeyError):
            forecast._drivers(row,cache)

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

    def test_independent_broad_acquisition_requires_fresh_full_count_selection(self):
        demand = dict(status='partial_acquisition')
        plan = dict.fromkeys(forecast._TILE,1)
        plan.update(units=20,frequency_unit_points=4,frequency_points=80,raw_candidate_row_capacity=160)
        tiles = [dict(completed_units=n,completed_frequency_points=4*n,raw_candidate_rows=rows,
                      timing={'total':{'wall_seconds':float(n)}})
                 for n,rows in ((1,8),(1,16),(2,32))]
        trace = dict(status='partial_acquisition',acquisition_work_class=demand,coordinates=[],
                     full_plan=plan,bounded_tiles=tiles,elapsed_seconds=2.)
        self.assertIsNone(forecast._heldout_acquisition(trace,trace,10.))
        selection = dict(exact_candidate_rows=160,score_order_strata=['monotone','alternating','shuffled'],
                         samples=[.1,.2,.3])
        measured = dict(trace,full_selection_holdout=selection)
        self.assertAlmostEqual(forecast._heldout_acquisition(measured,trace,10.),2.5)
        selection['exact_candidate_rows'] = 159
        with self.assertRaisesRegex(ValueError,'full-count candidate-selection'):
            forecast._heldout_acquisition(measured,trace,10.)

    def test_independent_acquired_time_repeats_only_short_work(self):
        trace = dict(status='acquired',coordinates=[1],acquisition_work_class={'branch':'acquired'},
                     elapsed_seconds=.375,elapsed_observations_seconds=[.25,.375,.5])
        self.assertEqual(forecast._heldout_acquisition(trace,trace,10.),.0375)
        long = dict(trace,elapsed_seconds=.75,elapsed_observations_seconds=[.75])
        self.assertEqual(forecast._heldout_acquisition(long,long,10.),.075)
        trace['elapsed_observations_seconds'].pop()
        with self.assertRaisesRegex(ValueError,'declared matched timing starts'):
            forecast._heldout_acquisition(trace,trace,10.)

    def test_whole_service_budget_does_not_allow_cancellation(self):
        residuals = {name:dict(observed_s_per_s=.4,forecast_s_per_s=prediction)
                     for name,prediction in (('bank',.55),('fine',.25))}
        result = forecast._holdout_service(residuals,{},.2,(('bank',),('fine',)),
                                           {forecast.RATIO:1.},(.2,.16))
        self.assertEqual(result['relative_error'],0.)
        self.assertAlmostEqual(result['no_cancellation_relative_error'],.30)
        self.assertEqual(result['status'],'cost_drift')

    def test_ceiling_error_uses_maximum_within_alternatives_and_requires_each(self):
        residuals = {name:dict(observed_s_per_s=actual,forecast_s_per_s=prediction)
                     for name,actual,prediction in (('DATA',.015,.015),('H0',.001,.01),
                                                    ('active',.21,.22),('cap',.70,.68))}
        groups = (('DATA','H0'),('active','cap'))
        expected = {forecast.RATIO:.715}
        result = forecast._holdout_service(residuals,{},.02,groups,expected,(.20,.15))
        self.assertTrue(result['accepted'])
        self.assertAlmostEqual(result['no_cancellation_relative_error'],.029/.735)
        result = forecast._holdout_service(residuals,{'H0':'different work'},.02,groups,expected,(.20,.15))
        self.assertEqual(result['status'],'partial_coverage')
        self.assertEqual(result['missing'],['H0'])

    def test_forecast_maximum_preserves_unselected_uncertainty_and_missing_work(self):
        geometry=dict.fromkeys(forecast._GEOMETRY,1)
        def measured(value):
            return dict(component(value,value,value),calibrated_point_before_gpu_scaling=value,
                        independent_local_holdout={forecast.RATIO:value})
        measurement=dict(inference_allocation_mode='full_logical',inference_workers=12,workspace_margin={'status':'within_budget'},
            recurring_window=dict(covers_longest_public_geometry=True,noise_only_acquisition=measured(.04)),
            nominal_service={'components':{name:measured(.01) for name in forecast._FIXED}},
            nominal_acquisition=measured(.02),noise_only_acquisition=measured(.01),recurring_banks=measured(.01),
            fine_active=measured(.03),fine_terminal=measured(.06),noise_only_fine=measured(.05),
            fine_public_state_ceiling=measured(.07),erasure_branch_holdout=dict(supported_by_state_ceiling=True,
                sample={'physical_first_sample':0,forecast.RATIO:.01,'native_template_work':{'cache':{}}}))
        strata={name:{} for name in forecast._FINE_STRATA}
        strata['interior_erasure']={'records':[{'work':{'physical_first_sample':0}}]}
        cpu=dict(public_geometry=geometry,calibration_inputs={},measurement=measurement)
        gpu=dict(public_geometry=geometry,evaluator={},recurring_acquisition={},recurring_banks=[],
            devices={'cuda':dict(strata=strata,data_acquisition={},noise_acquisition={})})
        with mock.patch.object(forecast,'_acquisition',side_effect=lambda row,*a,**k:row), \
             mock.patch.object(forecast,'_banks',side_effect=lambda row,*a,**k:row), \
             mock.patch.object(forecast,'_nominal',side_effect=lambda row,*a,**k:row), \
             mock.patch.object(forecast,'_branch',side_effect=lambda row,*a,**k:row), \
             mock.patch.object(forecast,'_check_fine',return_value={}), \
             mock.patch.object(forecast,'_work_envelope',return_value={'relative_cost_scale_interval':[1.,1.]}):
            historical=forecast._forecast_row(cpu,gpu,hardware_threads=12)
            self.assertEqual(historical['complete_capacity']['realtime_status'],'pass')
            measurement.update(inference_allocation_mode='automatic_receiver', inference_workers=24)
            automatic=forecast._forecast_row(cpu,gpu,hardware_threads=12)
            self.assertEqual(automatic['complete_capacity'], historical['complete_capacity'])
            measurement['inference_workers']=12
            with self.assertRaisesRegex(ValueError, 'declared receiver allocation'):
                forecast._forecast_row(cpu,gpu,hardware_threads=12)
            measurement['inference_allocation_mode']='full_logical'
            self.assertEqual(historical['capacity_scope'],'historical-unspecified')
            self.assertEqual(historical['blind_fresh_start_capacity']['status'],'unqualified')
            self.assertIsNone(historical['blind_fresh_start_capacity'][forecast.RATIO])
            measurement.update(capacity_scope='configured-public-calendar',
                blind_fresh_start_capacity=forecast.unqualified_blind_capacity(90.))
            scoped=forecast._forecast_row(cpu,gpu,hardware_threads=12)
            self.assertEqual(scoped['capacity_scope'],'configured-public-calendar')
            self.assertEqual(scoped['nominal_service']['capacity_scope'],'configured-public-calendar')
            self.assertEqual(scoped['complete_capacity']['capacity_scope'],'configured-public-calendar')
            self.assertEqual(scoped['blind_fresh_start_capacity'],measurement['blind_fresh_start_capacity'])
            self.assertIsNot(scoped['blind_fresh_start_capacity'],measurement['blind_fresh_start_capacity'])
            # A smaller alternative is still required work: choosing a larger
            # supported point must not conceal its unknown or missing model.
            for model in ('future_unknown_status',None):
                measurement['noise_only_acquisition']=measured(.01)
                if model is None:
                    del measurement['noise_only_acquisition']['model_status']
                else:
                    measurement['noise_only_acquisition']['model_status']=model
                with self.subTest(model=model):
                    result=forecast._forecast_row(cpu,gpu,hardware_threads=12)['complete_capacity']
                    self.assertFalse(result['all_component_models_supported'])
                    self.assertEqual(result['realtime_status'],'unresolved')
            measurement['noise_only_acquisition']=dict(measured(.01),**{forecast.INTERVAL:None})
            uncertain=forecast._forecast_row(cpu,gpu,hardware_threads=12)['complete_capacity']
            self.assertIsNone(uncertain[forecast.INTERVAL])
            self.assertNotEqual(uncertain['realtime_status'],'pass')
            measurement['noise_only_acquisition']=dict(model_status='unresolved',
                measured_complete_component_lower_bound=1.2)
            missing=forecast._forecast_row(cpu,gpu,hardware_threads=12)['complete_capacity']
            self.assertFalse(missing['all_component_models_supported'])
            self.assertEqual(missing['components']['acquisition']['measured_complete_component_lower_bound'],1.2)
            self.assertEqual(missing['realtime_status'],'fail')

    def test_guard_preserves_point_and_withholds_pass_when_upper_reaches_realtime(self):
        service = dict(component(.7,.5,.9),realtime_status='pass')
        forecast._guarded_interval(service,.15)
        self.assertEqual(service[forecast.RATIO],.7)
        self.assertEqual(service['local_calibration_interval'],[.5,.9])
        self.assertAlmostEqual(service[forecast.INTERVAL][1],.9/.85)
        self.assertEqual(service['realtime_status'],'unresolved')

    def test_phase_refresh_seals_before_external_holdout_and_keeps_other_evidence(self):
        cap = 4608*2**20
        case = dict(index=2,configuration_hash='configuration',native_args=[])
        geometry = dict.fromkeys(forecast._GEOMETRY,1)
        data, valid = b'data',b'mask'
        sha = lambda value:hashlib.sha256(value).hexdigest()
        source = dict(native_binary='exporter')
        budget, runtime = {'available_cpus':12}, {'threads':1}
        local = dict(case=case,measurement={'fine_active':'original training',
            'inference_allocation_mode':'full_logical','inference_workers':12})
        other = dict(case={'index':1},measurement={'unselected':'unchanged'})
        remote = dict(case=case,public_geometry=geometry,input_sha256=sha(data),validity_sha256=sha(valid),
                      devices={'cuda':{'data_acquisition':{'coordinates':[[0]]}}})
        prior_trace = dict(input_sha256=sha(data),validity_sha256=sha(valid),devices={'cpu':{
            'strata':{'nominal_continuation':'original holdout','noise_only':'unchanged holdout'}}})
        baseline = dict(cases=[dict(case_index=2,missing_or_mismatched={},independent_cpu_trace=dict(
            input_relation={'status':'identical_input'},nominal_service={'observed_s_per_s':.3}))])
        arguments = dict(cpu=dict(cases=[other,local],workspace_limit_bytes=cap,policy={'family_size':72}),
            gpu=dict(cases=[remote],sources={'driver':'exact'}),holdout=dict(cases=[prior_trace]),
            parsed=dict(cases=[case],source_sha256='list'),source=source,runtime=runtime,cpu_budget=budget)
        receiver=object()
        prepared = SimpleNamespace(raw=data,valid=valid,public_geometry=geometry,
            receiver=lambda _:nullcontext(receiver),
            acquisition=lambda _:dict(result=dict(status='acquired',coordinates=((0,),))))
        training = dict(successful_outer_observations=3)
        predictions = dict(missing_or_mismatched={},
            nominal_service=dict(component(.2,.15,.25),realtime_status='pass'),
            complete_capacity=dict(component(.9,.7,1.2),realtime_status='unresolved'))
        new_trace = {'fresh':('external timing',),'coordinates':()}
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'new.json'
            def fresh(*_):
                sealed=json.loads(output.read_text())
                self.assertEqual(sealed['status'],'prediction_sealed')
                self.assertEqual(sealed['phase_refresh']['pre_external_holdout']['prediction']['nominal_service'][forecast.RATIO],.2)
                return new_trace
            def verify(**inputs):
                self.assertIs(inputs['cpu']['cases'][0],other)
                self.assertIs(inputs['cpu']['cases'][1]['measurement']['fine_active'],training)
                strata=inputs['holdout']['cases'][0]['devices']['cpu']['strata']
                self.assertEqual(strata['nominal_continuation'],{'fresh':['external timing'],'coordinates':[]})
                self.assertEqual(strata['noise_only'],'unchanged holdout')
                return dict(cases=[dict(predictions,components={'nominal_fine':component(.2,.15,.25)},
                    independent_cpu_trace={'nominal_service':{'accepted':True}})])
            operating={'platform_profile':'balanced','energy_preference':'performance','governor':'powersave','ac_online':'1'}
            with mock.patch('triad_case_capacity.PreparedCapacityCase',return_value=nullcontext(prepared)), \
                 mock.patch('triad_case_capacity.measure_sustained_fine',return_value=training) as fit, \
                 mock.patch('triad_case_metrics.machine_resources',return_value={'cpu':budget,'memory_budget_bytes':cap}), \
                 mock.patch('triad_case_suite.native_plan',return_value={}), \
                 mock.patch('triad_abbreviated.environment',return_value=runtime), \
                 mock.patch('triad_gpu_workload._continuation_sample',side_effect=fresh), \
                 mock.patch.object(forecast,'identity',return_value=source), \
                 mock.patch.object(forecast,'same_measured_source',return_value=True), \
                 mock.patch.object(forecast,'load_cases',return_value={'source_sha256':'list'}), \
                 mock.patch.object(forecast,'_runtime_leaf_hashes',return_value=arguments['gpu']['sources']), \
                 mock.patch.object(forecast,'_cpu_operating_conditions',return_value=operating), \
                 mock.patch.object(forecast,'_forecast_row',return_value=predictions), \
                 mock.patch.object(forecast,'estimate',side_effect=verify) as reverify:
                result=forecast._refresh_cpu_phase('nominal-fine',baseline,arguments,output,'cases.md',{})
                fit.assert_called_once_with(prepared,receiver,((0,),),family_size=72,maximum_look=9)
                output=Path(directory)/'changed-source.json'
                with mock.patch.object(forecast,'same_measured_source',return_value=False), \
                     self.assertRaisesRegex(ValueError,'changed during the phase refresh'):
                    forecast._refresh_cpu_phase('nominal-fine',baseline,arguments,output,'cases.md',{})
                reverify.assert_called_once()
                self.assertEqual(json.loads(output.read_text())['status'],'prediction_sealed')
        self.assertEqual(result['phase_refresh']['status'],'phase_timing_supported')
        self.assertEqual(local['measurement']['fine_active'],'original training')
        self.assertEqual(prior_trace['devices']['cpu']['strata']['nominal_continuation'],'original holdout')

    def test_phase_refresh_cli_rejects_overwriting_existing_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'original.json'; path.write_text('{}')
            args=['--refresh-cpu-phase','nominal-fine','--case-index','2','--holdout-case-index','2',
                  '--cpu-report',str(path),'--gpu-report',str(path),'--local-holdout-report',str(path),'--output',str(path)]
            with mock.patch.object(forecast,'load_cases',return_value={'cases':[{'index':2}]}), \
                 mock.patch.object(forecast,'identity'),mock.patch.object(forecast,'estimate') as estimate, \
                 mock.patch('triad_cpu.detect_cpu_budget',return_value={'available_cpus':12}), \
                 mock.patch('triad_cpu.configure_numerical_threads'), \
                 redirect_stderr(io.StringIO()),self.assertRaises(SystemExit) as exited:
                forecast.main(args)
            estimate.assert_not_called()
            self.assertEqual(exited.exception.code,2)
            self.assertEqual(path.read_text(),'{}')

    def test_cached_gpu_reference_binds_precision_and_actual_coarse_runtime(self):
        runtime = dict.fromkeys(forecast._CUDA_REPORT_MATCH,'matched')
        pools = [{'prefix':'libgomp','threads':1},{'prefix':'openblas','threads':1}]
        runtime['runtime'] = {'cuda_jit':dict(triton_version='3.4.0',driver_version='580.95.05',
                                              compute_capability=[12,0]),'numerical_thread_pools':pools}
        runtime['runtime_limits'] = {'pools':pools}
        coarse = dict.fromkeys(forecast._COARSE_RUNTIME,'matched')
        reference = dict(runtime,policy={'precision':'mixed'},cases=[{'case':{'index':1},'gpu_coarse':coarse}])
        target = dict(copy.deepcopy(runtime),policy={'precision':'mixed'},
                      cases=[{'case':{'index':7},'gpu_coarse':dict(coarse)}])
        target['runtime']['numerical_thread_pools'].reverse()
        target['runtime_limits']['pools'] = pools[::-1]
        forecast._same_cuda_reference_environment(target,reference,[1])
        duplicate = copy.deepcopy(target)
        duplicate['runtime_limits']['pools'].append(pools[0])
        with self.assertRaisesRegex(ValueError,'cached CUDA reference runtime'):
            forecast._same_cuda_reference_environment(duplicate,reference,[1])
        for field, value in [('driver_version','580.95.06'),('triton_version','3.4.1'),('compute_capability',[9,0])]:
            changed = copy.deepcopy(target)
            changed['runtime']['cuda_jit'][field] = value
            with self.subTest(field=field), self.assertRaisesRegex(ValueError,'cached CUDA reference runtime'):
                forecast._same_cuda_reference_environment(changed,reference,[1])
        unknown = copy.deepcopy(reference)
        unknown['runtime']['cuda_jit']['driver_version'] = None
        with self.assertRaisesRegex(ValueError,'observed Triton version, NVIDIA driver and GPU capability'):
            forecast._same_cuda_reference_environment(unknown,unknown,[1])
        target['cases'][0]['gpu_coarse']['cupy'] = 'different'
        with self.assertRaisesRegex(ValueError,'device or coarse library differs'):
            forecast._same_cuda_reference_environment(target,reference,[1])
        target['policy']['precision'] = 'different'
        with self.assertRaisesRegex(ValueError,'runtime, precision'):
            forecast._same_cuda_reference_environment(target,reference,[1])

    def test_standalone_noise_stage_receipt_binds_current_list_runtime_and_cap(self):
        source = dict(sources={'receiver':'revision'},native_binary_sha256='exporter',native_artifacts={'kernel':'binary'})
        runtime, cpus = {'threads':1},{'available_cpus':12}
        parsed = {'source_sha256':'edited'}
        report = dict(schema='triad-manual-stage-calibration-v1',calibration_stage='noise-only-fine',
                      status='stage_calibration_measured',provenance=dict(source,unchanged_at_completion=True),
                      input=dict(parsed,unchanged_at_completion=True),runtime=dict(runtime,unchanged_at_completion=True),
                      machine={'cpu':cpus},worker_allocations=[12],workspace_limit_bytes=4608*2**20,
                      cases=[dict(measurement=dict(inference_allocation_mode='full_logical', inference_workers=12))])
        forecast._validate_target_noise_report(report,parsed,source,runtime,cpus,4608*2**20)
        for edit in (dict(workspace_limit_bytes=4096*2**20),dict(worker_allocations=[6]),
                     dict(input={'source_sha256':'stale','unchanged_at_completion':True})):
            with self.subTest(edit=edit), self.assertRaisesRegex(ValueError,'calibration identity or allocation differs'):
                forecast._validate_target_noise_report(dict(report,**edit),parsed,source,runtime,cpus,4608*2**20)
        automatic = copy.deepcopy(report)
        automatic['worker_allocations']=[24]
        automatic['cases'][0]['measurement'].update(inference_allocation_mode='automatic_receiver', inference_workers=24)
        forecast._validate_target_noise_report(automatic,parsed,source,runtime,cpus,4608*2**20)
        automatic['cases'][0]['measurement']['inference_workers']=12
        with self.assertRaisesRegex(ValueError, 'declared receiver allocation'):
            forecast._validate_target_noise_report(automatic,parsed,source,runtime,cpus,4608*2**20)

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
        self.assertEqual(result['capacity_scope'],'historical-unspecified')
        self.assertEqual(result['blind_fresh_start_capacity']['status'],'unqualified')
        cpu.update(capacity_scope='configured-public-calendar',
                   blind_fresh_start_capacity=forecast.unqualified_blind_capacity())
        result=forecast.estimate(cpu,gpu,{'cases':[case],'source_sha256':'list'},source,runtime,budget)
        self.assertEqual(result['capacity_scope'],'configured-public-calendar')
        self.assertEqual(result['blind_fresh_start_capacity'],cpu['blind_fresh_start_capacity'])

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
        local5 = {'case':donor,'measurement':{'inference_workers':12,'inference_allocation_mode':'full_logical'}}
        local7 = {'case':target,'measurement':{'inference_workers':12,'inference_allocation_mode':'full_logical',
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
             mock.patch.object(forecast,'_reference_coverage',return_value=[5]), \
             mock.patch.object(forecast,'_gpu_work_executed',return_value=('fp32_bulk','fp32_bulk')), \
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
            holdout['workers']=18
            mismatched=forecast.estimate(cpu,gpu,{'cases':[donor,target],'source_sha256':'list'},
                source,runtime,budget,holdout=holdout,holdout_indices=[7],transfers=[(7,5)],
                target_noise_indices=[7])
            self.assertIn('holdout workers differ',mismatched['cases'][1]['reason'])
            holdout['workers']=24
            for local in (local5,local7):
                local['measurement'].update(inference_allocation_mode='automatic_receiver', inference_workers=24)
            calls=fit.call_count
            forecast.estimate(cpu,gpu,{'cases':[donor,target],'source_sha256':'list'},
                source,runtime,budget,holdout=holdout,holdout_indices=[7],transfers=[(7,5)],
                target_noise_indices=[7])
            self.assertEqual(fit.call_count,calls+2)  # Both current-list rows now use the automatic allocation.
            self.assertEqual(fit.call_args.kwargs['hardware_threads'],12)
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
