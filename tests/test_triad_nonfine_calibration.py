"""Unproven local calibration must not become a receiver-capacity claim."""
from copy import deepcopy
from pathlib import Path
from types import SimpleNamespace
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import triad_case_capacity as capacity
import triad_nonfine_calibration as nonfine


def identity_observation(start):
    return dict(status='available', identity=dict(workers=2, manual_list_sha256='a'*64, cpu=dict(budget=dict(
        available_cpus=2, receiver_worker_candidates=[1,2], topology=dict(physical_core_count=1)))),
        observation=dict(started_ns=start, ended_ns=start+2))


def measurement():
    row = dict(model_status='unresolved', ratio_processing_seconds_per_baseband_second=None)
    components = {stage: deepcopy(row) for stage in nonfine.ALL_STAGES}
    return dict(status='measured_complete_work_estimate', measurement_scope='full_capacity',
        inference_allocation_mode='pilot_selected', selected_workers=2,
        allocation=dict(selected_workers=2, inference_samples_reused=False,
            samples=[dict(workers=n, selection_ratio=.3/n) for n in (1,2)]),
        nominal_service=dict(components=deepcopy(components)),
        complete_capacity=dict(components=deepcopy(components)),
        workspace_margin=dict(status='within_budget', measurement_scope='full_capacity', estimated_peak_bytes=10,
            conservative_peak_bytes=11, budget_bytes=20), fine_active=dict(observations=[]),
        **{key:{} for key in ('fine_public_state_ceiling', 'fine_terminal',
            'erasure_branch_holdout', 'noise_only_fine', 'noise_only_acquisition',
            'nominal_acquisition', 'orchestration', 'recurring_banks')})


def receipt():
    return dict(schema=nonfine.SCHEMA, status='recorded_unqualified',
        identity_before=identity_observation(10), identity_after=identity_observation(30),
        work_started_ns=14, work_ended_ns=28, measurement=measurement(),
        public_geometry={}, public_geometry_sha256=nonfine._digest({}),
        resident_bank={}, resident_bank_sha256=nonfine._digest({}),
        policy={}, policy_sha256=nonfine._digest({}))


class NonfineTests(unittest.TestCase):
    def test_writer_checks_the_same_size_bound_as_the_reader_before_installing(self):
        with patch.object(nonfine, 'LOCAL_RECEIPT_LIMIT', 16), \
                patch('triad_replay_packet.write_new_file') as install:
            with self.assertRaisesRegex(ValueError, 'bounded read size'):
                nonfine.write_local_receipt('ignored', {'key': 'x'*16})
        install.assert_not_called()

    def test_match_returns_only_unqualified_nonfine_inputs(self):
        with patch.object(nonfine, 'compare_calibration_identities', return_value=dict(status='matched')):
            result = nonfine.compare_nonfine_calibration(receipt(), identity_observation(50),
                identity_observation(70), current_work_started_ns=54, current_work_ended_ns=68, current_geometry={}, current_policy={})
        self.assertEqual(result['status'], 'locally_matched_unqualified')
        self.assertEqual(set(result['components']['nominal_service']), nonfine.NONFINE_STAGES)
        self.assertEqual(result['components']['nominal_service']['acquisition']['model_status'], 'unresolved')
        self.assertNotIn('realtime_status', result)
        self.assertIn('No validated', result['scope'])

    def test_fixed_holdout_keeps_actual_inference_and_pilot_winner_distinct(self):
        r=receipt()
        pilot=r['measurement']['allocation']
        pilot['samples'][0]['selection_ratio']=.10
        pilot['selected_workers']=1
        r['measurement'].update(selected_workers=1,inference_workers=2,inference_allocation_mode='fixed_holdout')
        identity=r['identity_before']['identity']
        self.assertTrue(nonfine._allocation_covered(r['measurement'],identity))
        absent=deepcopy(r['measurement'])
        absent['allocation']['samples'].pop()
        self.assertFalse(nonfine._allocation_covered(absent,identity))
        for changed in (dict(inference_workers=1),dict(inference_allocation_mode='pilot_selected'),
                        dict(selected_workers=2)):
            altered=deepcopy(r['measurement'])
            altered.update(changed)
            self.assertFalse(nonfine._allocation_covered(altered,identity))

    def test_nominal_holdout_schema_requires_full_nominal_work_and_has_no_ceiling(self):
        holdout=receipt()
        holdout['schema']=nonfine.NOMINAL_HOLDOUT_RECEIPT_SCHEMA
        measured=holdout['measurement']
        measured.update(status='measured_nominal_holdout',measurement_scope='nominal_holdout',
            inference_allocation_mode='fixed_holdout',ceiling_only_receipts_omitted=list(capacity.CEILING_ONLY_RECEIPTS))
        for name in ('complete_capacity', *capacity.CEILING_ONLY_RECEIPTS):
            measured.pop(name)
        measured['workspace_margin']['measurement_scope']='nominal_holdout'
        with patch.object(nonfine,'compare_calibration_identities',return_value=dict(status='matched')):
            def compare(record, *, nominal_holdout=True):
                return nonfine.compare_nonfine_calibration(record,identity_observation(50),
                    identity_observation(70),current_work_started_ns=54,current_work_ended_ns=68,
                    current_geometry={},current_policy={},nominal_holdout=nominal_holdout)
            matched=compare(holdout)
            self.assertEqual(matched['status'],'locally_matched_unqualified')
            self.assertEqual(set(matched['components']),{'nominal_service'})
            self.assertNotIn('separate_ceiling',matched)
            self.assertEqual(compare(holdout,nominal_holdout=False)['status'],'unresolved')
            self.assertEqual(compare(receipt())['status'],'unresolved')
            for damage in (
                lambda r:r['nominal_service']['components'].pop('recurring_bank_preparation'),
                lambda r:r['nominal_service']['components'].pop('quiet_state_and_canonical_codec'),
                lambda r:r.pop('nominal_acquisition'),
                lambda r:r['workspace_margin'].update(measurement_scope='full_capacity'),
                lambda r:r.update(complete_capacity={}),
            ):
                bad=deepcopy(holdout)
                damage(bad['measurement'])
                self.assertEqual(compare(bad)['status'],'unresolved')

    def test_invalid_brackets_and_identity_are_unresolved(self):
        for start, end, identity_status in ((52,68,'matched'),(54,72,'matched'),(54,68,'unresolved')):
            with self.subTest(start=start,end=end,identity_status=identity_status), patch.object(
                    nonfine, 'compare_calibration_identities', return_value=dict(status=identity_status)):
                result = nonfine.compare_nonfine_calibration(receipt(), identity_observation(50),
                    identity_observation(70), current_work_started_ns=start, current_work_ended_ns=end, current_geometry={}, current_policy={})
                self.assertEqual(result['status'], 'unresolved')
        r = receipt(); r['work_started_ns']=10
        result = nonfine.compare_nonfine_calibration(r, identity_observation(50), identity_observation(70),
            current_work_started_ns=54,current_work_ended_ns=68, current_geometry={}, current_policy={})
        self.assertEqual(result['status'], 'unresolved')

    def test_changed_or_corrupt_public_geometry_is_unresolved(self):
        before, after = identity_observation(50), identity_observation(70)
        with patch.object(nonfine, 'compare_calibration_identities', return_value=dict(status='matched')):
            result = nonfine.compare_nonfine_calibration(receipt(), before, after,
                current_work_started_ns=54, current_work_ended_ns=68, current_geometry={'sample_rate':2}, current_policy={})
            self.assertEqual(result['reason'], 'public_geometry_mismatch')
            invalid = receipt(); invalid['public_geometry_sha256'] = '0'*64
            result = nonfine.compare_nonfine_calibration(invalid, before, after,
                current_work_started_ns=54, current_work_ended_ns=68, current_geometry={}, current_policy={})
            self.assertEqual(result['reason'], 'public_geometry_mismatch')

    def test_missing_stages_allocation_or_workspace_are_unresolved(self):
        mutations = [
            lambda r:r['measurement']['nominal_service']['components'].pop('acquisition'),
            lambda r:r['measurement']['complete_capacity']['components'].pop('complete_fine_search'),
            lambda r:r['measurement']['allocation']['samples'].pop(0),
            lambda r:r['measurement']['allocation'].update(inference_samples_reused=True),
            lambda r:r['measurement']['allocation']['samples'][0].update(selection_ratio=float('nan')),
            lambda r:r['measurement']['workspace_margin'].update(estimated_peak_bytes=float('nan')),
            lambda r:r['measurement'].pop('noise_only_acquisition'),
            lambda r:r['measurement'].update(selected_workers=1),
            lambda r:r['measurement'].update(selected_workers=True),
            lambda r:r['measurement']['allocation'].update(selected_workers=True),
        ]
        for mutation in mutations:
            r = receipt(); mutation(r)
            with self.subTest(mutation=mutation), patch.object(nonfine, 'compare_calibration_identities',
                    return_value=dict(status='matched')):
                result = nonfine.compare_nonfine_calibration(r, identity_observation(50),
                    identity_observation(70), current_work_started_ns=54,current_work_ended_ns=68, current_geometry={}, current_policy={})
                self.assertEqual(result['status'], 'unresolved')

    def test_full_candidate_set_argmin_and_policy_are_required(self):
        r = receipt()
        budget = r['identity_before']['identity']['cpu']['budget']
        budget.update(available_cpus=12, receiver_worker_candidates=[1,2,4,6,12])
        budget['topology']['physical_core_count']=6
        r['identity_after']['identity']['cpu']['budget'] = deepcopy(budget)
        r['measurement']['allocation']['samples'] = [dict(workers=w, selection_ratio=1/w)
                                                       for w in (2,6,12)]
        self.assertFalse(nonfine._allocation_covered(r['measurement'], r['identity_before']['identity']))
        r['measurement']['allocation']['samples'] = [dict(workers=w, selection_ratio=1/w)
                                                       for w in (1,2,4,6,12)]
        self.assertFalse(nonfine._allocation_covered(r['measurement'], r['identity_before']['identity']))
        r['measurement']['selected_workers']=12
        r['measurement']['allocation']['selected_workers']=12
        r['identity_before']['identity']['workers']=12
        self.assertTrue(nonfine._allocation_covered(r['measurement'], r['identity_before']['identity']))
        with patch.object(nonfine, 'compare_calibration_identities', return_value=dict(status='matched')):
            result = nonfine.compare_nonfine_calibration(receipt(), identity_observation(50),
                identity_observation(70), current_work_started_ns=54, current_work_ended_ns=68,
                current_geometry={}, current_policy={'family_size':9})
        self.assertEqual(result['status'], 'unresolved')

    def test_capture_rejects_wrong_family_before_measurement(self):
        class Prepared:
            case={'configuration_hash':'a'*64, 'index':1}
            basis_source=object()
            unit_samples=1; fs=1
        with patch('triad_cases.load_cases',return_value=dict(cases=[{}],source_sha256='a'*64)), \
                patch.object(capacity,'PreparedCapacityCase',Prepared), patch.object(capacity,
                'measure_complete_case') as measure:
            result=nonfine.capture_nonfine_calibration(Prepared(),[1,2],declared_workers=2,
                family_size=9,exporter='exporter')
        self.assertEqual(result['reason'],'family_size_does_not_match_full_manual_list')
        measure.assert_not_called()

    @staticmethod
    def _observed_measurement(*args, **kwargs):
        kwargs['on_inference_allocation'](2)
        return measurement()

    def test_capture_calls_canonical_measurement_between_identity_observations(self):
        class Prepared:
            case={'configuration_hash':'a'*64, 'index':1}
            basis_source=object()
            public_geometry={}
            bank=SimpleNamespace(public_bank_profile={}, public_bank_work={})
            fs=1; samples=100; unit_samples=1; slot_seconds=1
            memory_limit_bytes=100; noise_variance=1.
        prepared = Prepared()
        with patch('triad_cases.load_cases',return_value=dict(cases=[{}],source_sha256='a'*64)), \
                patch.object(capacity, 'PreparedCapacityCase', Prepared), patch.object(capacity,
                'measure_complete_case',side_effect=self._observed_measurement) as measure, patch.object(nonfine,
                'public_calibration_geometry',return_value={}), patch.object(nonfine,
                'capture_calibration_identity',side_effect=[identity_observation(10),identity_observation(30)]), \
                patch.object(nonfine.time, 'monotonic_ns',side_effect=[14,28]), patch.object(nonfine,
                'compare_calibration_identities',return_value=dict(status='matched')):
            result=nonfine.capture_nonfine_calibration(prepared,[1,2],declared_workers=2,
                family_size=8,exporter='exporter')
        self.assertEqual(result['status'],'recorded_unqualified')
        measure.assert_called_once()
        self.assertIs(measure.call_args.kwargs['record_signal_work'], True)
        self.assertEqual(result['case_index'],1)
        self.assertEqual(result['work_started_ns'],14)
        self.assertEqual(result['work_ended_ns'],28)
        self.assertNotIn('realtime_status',result)

    def test_automatic_worker_binding_precedes_fresh_inference(self):
        class Prepared:
            case={'configuration_hash':'a'*64, 'index':1}
            basis_source=object()
            bank=SimpleNamespace(public_bank_profile={}, public_bank_work={})
            fs=1; unit_samples=1

        def observed(start, workers):
            value=identity_observation(start)
            value['identity']['workers']=workers
            value['identity']['cpu']['budget'].update(available_cpus=4,
                receiver_worker_candidates=[1,2,4])
            value['observation'].update(process_pair='owned native and controller',
                host_maps_sha256='controller maps', native_maps_sha256='native maps')
            return value

        def measured(*args, **kwargs):
            kwargs['on_inference_allocation'](2)
            sample=measurement()
            sample['allocation']['samples'].append(dict(workers=4,selection_ratio=.2))
            return sample

        for drift in (False,True):
            selected=observed(20,2)
            if drift:
                selected['observation']['host_maps_sha256']='different maps'
            with self.subTest(drift=drift), patch('triad_cases.load_cases',
                    return_value=dict(cases=[{}],source_sha256='a'*64)), \
                    patch.object(capacity,'PreparedCapacityCase',Prepared), \
                    patch.object(capacity,'measure_complete_case',side_effect=measured), \
                    patch.object(nonfine,'public_calibration_geometry',return_value={}), \
                    patch.object(nonfine,'capture_calibration_identity',
                        side_effect=[observed(10,4),selected,observed(40,2)]) as captures, \
                    patch.object(nonfine.time,'monotonic_ns',side_effect=[24,38]), \
                    patch.object(nonfine,'compare_calibration_identities',return_value=dict(status='matched')):
                result=nonfine.capture_nonfine_calibration(Prepared(),[1,2,4],declared_workers=None,
                    family_size=8,exporter='exporter')
            if drift:
                self.assertEqual(result['reason'],'local_identity_changed_during_allocation_selection')
                self.assertEqual([call.kwargs['workers'] for call in captures.call_args_list],[4,2])
            else:
                self.assertEqual(result['status'],'recorded_unqualified')
                self.assertEqual([call.kwargs['workers'] for call in captures.call_args_list],[4,2,2])
                self.assertEqual(result['work_started_ns'],24)

    def test_unselected_worker_and_failed_capture_remain_unresolved(self):
        class Prepared:
            case={'configuration_hash':'a'*64, 'index':1}
            basis_source=object()
            public_geometry={}
            bank=SimpleNamespace(public_bank_profile={}, public_bank_work={})
            fs=1; samples=100; unit_samples=1; slot_seconds=1
            memory_limit_bytes=100; noise_variance=1.
        one_worker_before=identity_observation(10); one_worker_before['identity']['workers']=1
        one_worker_after=identity_observation(30); one_worker_after['identity']['workers']=1
        with patch('triad_cases.load_cases',return_value=dict(cases=[{}],source_sha256='a'*64)), \
                patch.object(capacity, 'PreparedCapacityCase', Prepared), patch.object(capacity,
                'measure_complete_case',side_effect=self._observed_measurement), patch.object(nonfine,
                'public_calibration_geometry',return_value={}), patch.object(nonfine,
                'capture_calibration_identity',side_effect=[one_worker_before,one_worker_after]), \
                patch.object(nonfine.time, 'monotonic_ns',side_effect=[14,28]), patch.object(nonfine,
                'compare_calibration_identities',return_value=dict(status='matched')):
            result=nonfine.capture_nonfine_calibration(Prepared(),[1,2],declared_workers=1,
                family_size=8,exporter='exporter')
        self.assertEqual(result['status'],'unresolved')
        self.assertEqual(result['reason'],'declared_worker_selection_changed')
        self.assertEqual(result['selected_workers'],2)
        with patch('triad_cases.load_cases',return_value=dict(cases=[{}],source_sha256='a'*64)), \
                patch.object(capacity,'PreparedCapacityCase',Prepared), patch.object(nonfine,
                'public_calibration_geometry',return_value={}), patch.object(nonfine,
                'capture_calibration_identity',return_value=dict(status='unresolved')):
            result=nonfine.capture_nonfine_calibration(Prepared(),[1,2],declared_workers=2,
                family_size=8,exporter='exporter')
        self.assertEqual(result['reason'],'pre_measurement_identity_unavailable')

    def test_suite_keeps_one_actual_measurement_when_identity_or_list_is_unavailable(self):
        class Prepared:
            case = {'configuration_hash': 'a'*64, 'index': 1}
            basis_source = object()
            bank = SimpleNamespace(public_bank_profile={}, public_bank_work={})
            fs = unit_samples = 6000

        for cause in ('preflight', 'binding', 'list'):
            with self.subTest(cause=cause):
                before = identity_observation(10)
                before['identity']['workers'] = 4
                missing = dict(status='unresolved')
                def observed(*args, **kwargs):
                    if cause == 'binding':
                        kwargs['on_inference_allocation'](2)
                    else:
                        self.assertNotIn('on_inference_allocation', kwargs)
                    return measurement()

                with patch('triad_cases.load_cases',
                        return_value=dict(cases=[{}],source_sha256='a'*64)), \
                        patch.object(capacity,'PreparedCapacityCase',Prepared), \
                        patch.object(capacity,'measure_complete_case',side_effect=observed) as measure, \
                        patch.object(nonfine,'public_calibration_geometry',return_value={}), \
                        patch.object(nonfine,'capture_calibration_identity',
                            side_effect=[before,missing] if cause=='binding' else [missing]) as identity:
                    result=nonfine.capture_nonfine_calibration(Prepared(),[1,2,4],declared_workers=None,
                        family_size=8,exporter='exporter',suite_list_sha256=('b' if cause=='list' else 'a')*64)
                measure.assert_called_once()
                self.assertEqual(identity.call_count, {'preflight':1, 'binding':2, 'list':0}[cause])
                self.assertEqual(result['status'],'unresolved')
                self.assertEqual(result['measurement'],measurement())
                self.assertEqual(result['reason'], {'preflight':'pre_measurement_identity_unavailable',
                    'binding':'local_identity_changed_during_allocation_selection',
                    'list':'manual_list_changed_before_calibration'}[cause])


if __name__ == '__main__':
    unittest.main()
