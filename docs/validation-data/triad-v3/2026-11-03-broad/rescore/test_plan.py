#!/usr/bin/env python3
"""Cheap rescore mapping, denominator and paired-score checks; no receiver run."""
import copy
import json
import math
from pathlib import Path
import tempfile
import unittest
import numpy as np
import prepare
import run
import analyze

class RescoreChecks(unittest.TestCase):
    def setUp(self):
        self.job=dict(id='actual-fixture',stage='actual',argv=['{PYTHON}','{ROOT}/tools/triad-conditional-sweep.py',
            '{OUT}/workspaces/fixture','--methods','static-fft-ucb','--seed','42','--output','{OUT}/scores/fixture.json'],
            workspace='{OUT}/workspaces/fixture',report='{OUT}/scores/fixture.json',depends_on=['fixture'],
            method='static-fft-ucb',control='signal',bit=0,cn0_db_hz=20,case='fixture',timeout_seconds=120)
        self.job['definition_sha256']=prepare.digest(self.job)

    def test_identical_science_except_paths_and_variant_binding(self):
        original=copy.deepcopy(self.job)
        new=prepare.variant_jobs({'jobs':[self.job]},'/old/core')[0]
        self.assertEqual(self.job,original)
        self.assertEqual(new['id'],self.job['id'])
        self.assertEqual(new['original_definition_sha256'],self.job['definition_sha256'])
        self.assertEqual(new['argv'],[s.replace('{OUT}/workspaces/fixture','/old/core/workspaces/fixture') for s in self.job['argv']])
        prepare.runner.check_job(new)
        self.assertEqual(new['report'],self.job['report'])

    def test_failure_denominators_not_success_only(self):
        failed=analyze.opportunity(self.job,{'status':'execution_failed'})[0]
        self.assertEqual(failed['attempted_denominator'],1)
        self.assertEqual(failed['failed_signal_opportunities_as_misses'],1)
        self.assertEqual(analyze.opportunity(self.job,None)[0]['attempted_denominator'],0)
        job=copy.deepcopy(self.job);job['stage']='conditional'
        rows=analyze.opportunity(job,{'status':'invalid_output'})
        self.assertEqual(rows[0]['failed_signal_opportunities_as_misses'],4)
        self.assertEqual(rows[1]['unresolved_control_trials'],4)

    def test_precision_comparison_checks_winner_and_both_gates(self):
        result=analyze.compare_scores([[23.,0.],[0.,17.]],[[0.,23.],[0.,18.]],22.)
        self.assertEqual(result['winner_changes'],1)
        self.assertEqual(result['strict_admission_changes'],0)
        self.assertEqual(result['relaxed_admission_changes'],0)
        with self.assertRaisesRegex(ValueError,'invalid'):
            analyze.compare_scores([[float('inf'),0]],[[1,0]],22)

    def test_variant_receipt_never_migrates_baseline_identity(self):
        root=prepare.HERE.parents[4]/'local-evidence/triad-broad-coverage/rescore'
        root.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=root,prefix='fixture-') as temp:
            path=Path(temp)/'receipt.json'
            value=dict(manifest_sha256='fixed',variant='numerical-fix',inputs_unchanged_at_end=True,
                all_selected_jobs_accounted=True,jobs=[])
            path.write_text(json.dumps(value))
            self.assertEqual(run.read_variant_receipts([path],'fixed',{},Path(temp))[0],{})
            with self.assertRaisesRegex(ValueError,'identity differs'):
                run.read_variant_receipts([path],'original',{},Path(temp))
            value['inputs_unchanged_at_end']=False;path.write_text(json.dumps(value))
            with self.assertRaisesRegex(ValueError,'incomplete'):
                run.read_variant_receipts([path],'fixed',{},Path(temp))

    def test_partial_signal_scores_survive_later_null_failure_with_full_validation(self):
        # Mirrors the retained C/N0=120 failure: signal scores completed 4/4,
        # then null whitening failed. The job must remain failed, while those
        # signal arrays are available for paired numerical diagnosis.
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);workspace=root/'workspace';workspace.mkdir()
            (workspace/'manifest.json').write_text(json.dumps({'signal_bit':0}))
            for name in ('gram.c128','mean.c128','signal_energy.f64','valid_rows.u64','intervals.u64'):
                (workspace/name).write_bytes(b'fixture')
            hashes={p.name:prepare.sha(p) for p in workspace.iterdir()}
            source={'source_file_sha256':{'tools/'+name:'sha-'+name for name in prepare.SCORER}}
            job=copy.deepcopy(self.job)
            job.update(stage='conditional',control='conditional-signal-and-null',cn0_db_hz=120,
                       workspace=str(workspace),report='{OUT}/partial.json')
            job['argv']+=['--horizon-seconds','86400','--attempt-spacing','60','--false-probability','0.0001',
                '--duration-alternatives','8','--selection-methods','16','--calibration-alternatives','2']
            gate=math.log(1440*8*16*2/(.5e-4));signals=np.tile([gate+2,0.],(4,1));nulls=np.zeros((4,2))
            archive=root/'partial.scores.npz';np.savez(archive,signal=signals,null=nulls)
            def condition(control,scores):
                return dict(control=control,cn0_db_hz=120,methods={'static-fft-ucb':dict(score_array=control,
                    threshold=gate,receiver_seconds=.01,counts=analyze.evaluate.recount(scores,0,gate,control))})
            payload=dict(source_sha256=source['source_file_sha256']['tools/triad-conditional-sweep.py'],
                dependency_sha256={name:source['source_file_sha256']['tools/'+name] for name in prepare.SCORER
                    if name not in ('triad-conditional-sweep.py','triad_phase_frequency.py')},
                scores_file=archive.name,scores_sha256=prepare.sha(archive),observation_mode='conditional',wall_seconds=.1,
                family=dict(fixed_window_log_threshold=gate,score_probability=.5e-4,calibration_failure_probability=.5e-4,
                    total_false_admission_bound=1e-4,delta_per_segment=.5e-4/(1440*8*2)),
                paths=[dict(input=str(workspace),status='error',error='later null Cholesky failure',
                    sampled_input_hashes=hashes,conditions=[condition('signal',signals)])])
            report=root/'partial.json';report.write_text(json.dumps(payload));record={'status':'execution_failed','elapsed_seconds':.2}
            partial=analyze.completed_report(job,record,source,root,hashes)
            self.assertTrue(partial['partial']);self.assertEqual(record['status'],'execution_failed')
            self.assertEqual(partial['completed_controls'],{'signal'})
            self.assertEqual(partial['outcomes'][0]['counts']['correct_accepted'],4)
            opportunities=analyze.opportunity(job,record,partial['completed_controls'])
            self.assertEqual(opportunities[0]['completed_trials'],4)
            self.assertEqual(opportunities[0]['failed_signal_opportunities_as_misses'],0)
            self.assertEqual(opportunities[1]['unresolved_control_trials'],4)
            full=copy.deepcopy(payload);full['paths'][0].update(status='completed',conditions=[condition('signal',signals),condition('null',nulls)])
            report.write_text(json.dumps(full))
            fixed=analyze.completed_report(job,{'status':'completed'},source,root,hashes)
            paired=analyze.compare_jobs(partial,fixed)
            self.assertEqual(len(paired),1);self.assertEqual(paired[0]['control'],'signal')
            self.assertEqual(paired[0]['maximum_absolute_score_change'],0)
            # Every retained partial condition still obeys the same immutable
            # source, input, raw-array, gate, count and timing checks.
            mutations=[lambda p:p.update(source_sha256='wrong'),lambda p:p.update(scores_sha256='wrong'),
                lambda p:p['paths'][0]['sampled_input_hashes'].update({'mean.c128':'wrong'}),
                lambda p:p['family'].update(fixed_window_log_threshold=gate-1),
                lambda p:p['paths'][0]['conditions'][0]['methods']['static-fft-ucb'].update(receiver_seconds=-1),
                lambda p:p['paths'][0]['conditions'][0]['methods']['static-fft-ucb']['counts'].update(correct_accepted=0)]
            for mutate in mutations:
                changed=copy.deepcopy(payload);mutate(changed);report.write_text(json.dumps(changed))
                with self.assertRaises(ValueError):analyze.completed_report(job,record,source,root,hashes)

if __name__=='__main__':unittest.main()
