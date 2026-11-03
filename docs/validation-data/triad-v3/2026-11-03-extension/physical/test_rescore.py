from __future__ import annotations
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

HERE=Path(__file__).resolve().parent
SPEC=importlib.util.spec_from_file_location('extension_rescore_test',HERE/'rescore.py')
rescore=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(rescore)


def fixtures():
    plan=dict(jobs=[],expected_binary_sha256='old-binary',source_file_sha256={'old-native':'old-hash'},
              scorer_source_file_sha256={'tools/triad_group_acquire.py':'old-scorer'})
    prior={}
    for case in ('zero-shift-vlf-am65','shared-microwave-am65'):
        export='export-'+case;configure='configure-'+case
        plan['jobs'].append(dict(id=export,stage='export',workspace='{OUT}/workspaces/'+case,definition_sha256='old-export-'+case))
        plan['jobs'].append(dict(id=configure,stage='configure'))
        prior[export]=dict(status='completed',output_files=[dict(path='workspaces/'+case+'/'+name,sha256='hash',size_bytes=1)
            for name in ('manifest.json','group_observation.c128','received_energy.f64')])
        prior[configure]=dict(status='completed',native_configuration={'configuration':{'deterministic_oscillator_cfo_bound_hz':5e-7}})
        for method in ('window','global','global-frequency'):
            ident=method+'-'+case
            plan['jobs'].append(dict(id=ident,stage='acquire',case=case,control='signal',bit=0,expected_groups=7,
                method=method,timeout_seconds=300,depends_on=[export,configure],definition_sha256='original-'+ident))
            prior[ident]=dict(id=ident,status='execution_failed',log_sha256='original-error-log-'+ident,
                             bounded_status={'child_exit_code':2})
    return plan,prior


class RescoreTests(unittest.TestCase):
    def test_all_six_original_failures_are_preserved(self):
        plan,prior=fixtures();before=json.dumps(prior,sort_keys=True)
        value=rescore.build(plan,'original-plan',prior,[dict(path='/original/results.json',sha256='receipt-hash')],HERE.parents[4])
        self.assertEqual(value['baseline_status_counts'],{'execution_failed':6})
        self.assertEqual(value['planned_acquisitions'],6)
        self.assertEqual(len(value['jobs']),6)
        self.assertEqual(json.dumps(prior,sort_keys=True),before)
        for old in value['baseline_acquisitions']:
            self.assertEqual(old['record'],prior[old['id']])
            self.assertEqual(old['record_sha256'],rescore.common.digest(prior[old['id']]))
        for job in value['jobs']:
            rescore.common.check_job(job)
            self.assertEqual(job['maximum_block_phase_loss_db'],.02 if job['case']=='shared-microwave-am65' else .01)
        self.assertIn('tools/triad_gaussian_state.py',value['scorer_source_file_sha256'])
        self.assertNotEqual(value['scorer_source_file_sha256'],plan['scorer_source_file_sha256'])

    def test_missing_export_keeps_receiver_denominator(self):
        plan,prior=fixtures();prior['export-zero-shift-vlf-am65']['status']='execution_failed'
        value=rescore.build(plan,'p',prior,[],HERE.parents[4])
        self.assertEqual(value['planned_acquisitions'],6)
        self.assertEqual(value['input_capture_count'],1)
        unavailable=[j for j in value['jobs'] if j['unavailable_original_dependencies']]
        self.assertEqual(len(unavailable),3)
        for job in unavailable:
            result=rescore.execute(job,SimpleNamespace(),{},value)
            self.assertEqual(result['status'],'not_started_dependency')

    def test_workspace_hash_mutation_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);path=root/'input.bin';path.write_bytes(b'abc')
            artifact=dict(path='input.bin',size_bytes=3,sha256=hashlib.sha256(b'abc').hexdigest())
            manifest={'inputs':{'one':{'output_files':[artifact]},'two':{'output_files':[artifact]}}}
            self.assertEqual(rescore.verify_inputs(manifest,root),1)
            path.write_bytes(b'abd')
            with self.assertRaisesRegex(ValueError,'input bytes changed'):rescore.verify_inputs(manifest,root)

    def test_explicit_budget_only_changes_exact_case(self):
        self.assertEqual(rescore.phase_budget('shared-microwave-am65'),.02)
        self.assertEqual(rescore.phase_budget('other-microwave-case'),.01)
        self.assertEqual(rescore.phase_budget('zero-shift-vlf-am65'),.01)

    def test_command_keeps_original_inputs_and_new_outputs_separate(self):
        plan,prior=fixtures();value=rescore.build(plan,'p',prior,[],HERE.parents[4])
        with tempfile.TemporaryDirectory() as directory:
            base=Path(directory);inputs=base/'inputs';outputs=base/'new';outputs.mkdir()
            for item in value['inputs'].values():
                workspace=inputs/item['workspace'];workspace.mkdir(parents=True)
                (workspace/'manifest.json').write_text(json.dumps(dict(block_count=5,sample_count=160,sample_rate=10)))
            bindings=dict(INPUT=str(inputs),OUT=str(outputs))
            args=SimpleNamespace(python=Path('/python'),root=Path('/root'))
            for job in value['jobs']:
                argv=rescore.command(job,args,bindings,value)
                self.assertTrue(argv[2].startswith(str(inputs)))
                self.assertTrue(argv[argv.index('--output')+1].startswith(str(outputs)))
                self.assertEqual(float(argv[argv.index('--maximum-block-phase-loss-db')+1]),job['maximum_block_phase_loss_db'])
                if job['method']=='global-frequency':
                    idx=argv.index('--frequency-offset-grid')
                    self.assertEqual(list(map(float,argv[idx+1:idx+4])),[-5e-7,5e-7,1/64])

    def test_fresh_json_never_overwrites_original_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'receipt.json'
            rescore.fresh_json(path,{'original_failure':True})
            with self.assertRaises(FileExistsError):rescore.fresh_json(path,{'replacement':True})
            self.assertEqual(json.loads(path.read_text()),{'original_failure':True})


if __name__=='__main__':unittest.main()
