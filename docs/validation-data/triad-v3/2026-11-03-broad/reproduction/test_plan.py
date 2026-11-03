#!/usr/bin/env python3
"""Small structural checks for the frozen broad-study recipes."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[4]
def load(name):
    spec=importlib.util.spec_from_file_location(name,HERE/(name+'.py'))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
make=load('make_plan');runner=load('run_plan')

class PlanChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.inputs=json.loads((HERE/'inputs/physical-cases.json').read_text())
        cls.plan=make.build_plan(cls.inputs,ROOT)

    def test_stage_counts_and_id_bindings(self):
        self.assertEqual(self.plan['stage_counts'],{'configure':81,'export':178,'actual':356,'conditional':912,'benchmark':16})
        ids={job['id'] for job in self.plan['jobs']}
        self.assertEqual(len(ids),len(self.plan['jobs']))
        for job in self.plan['jobs']:
            runner.check_job(job)
            self.assertTrue(set(job['depends_on'])<=ids)
        changed=copy.deepcopy(self.plan['jobs'][0]);changed['argv'].append('--bogus')
        with self.assertRaisesRegex(ValueError,'hash mismatch'):runner.check_job(changed)

    def test_requested_topologies_are_not_substituted(self):
        cases={r['case']['name']:r for r in self.plan['configuration_cases']}
        for name in ('shift-0','shiftosc-baseband-clock'):
            self.assertEqual(cases[name]['status'],'unsupported_topology')
            self.assertIsNone(cases[name]['physical_argv'])
            self.assertFalse(any(job['case']==name for job in self.plan['jobs']))
        self.assertEqual(make.option(cases['rate-0.001']['physical_argv'],'--rf-hz'),'500000')

    def test_wide_arrival_and_doppler_are_explicit(self):
        cases={r['case']['name']:r for r in self.plan['configuration_cases']}
        argv=cases['timing-margin-default-1']['physical_argv']
        i=argv.index('--arrival-prior-seconds')
        self.assertEqual(argv[i+1:i+3],['-7.035','7.035'])
        self.assertNotIn('--start',argv)
        stable=cases['spread-hf-worst'];renewing=cases['renewing-hf-worst']
        self.assertEqual(stable['case']['width'],0)
        self.assertEqual(renewing['case']['width'],30)
        self.assertGreaterEqual(stable['sample_rate'],64*30)
        self.assertEqual(stable['block_samples'],16)

    def test_conditional_seed_pairing_and_independent_paths(self):
        self.assertEqual(len(self.plan['conditional_paths']),24)
        self.assertEqual(len(set(self.plan['conditional_paths'])),24)
        rows=[j for j in self.plan['jobs'] if j['stage']=='conditional']
        for path in self.plan['conditional_paths']:
            selected=[j for j in rows if j['depends_on']==[path]]
            self.assertEqual({j['cn0_db_hz'] for j in selected},set(make.CN0))
            self.assertEqual({j['method'] for j in selected},set(make.METHODS))
            self.assertEqual(len({make.option(j['argv'],'--seed') for j in selected}),1)
        exports=[j for j in self.plan['jobs'] if j['stage']=='export' and not j['case'].startswith('erasures-')]
        addresses=[j['seed_address_index'] for j in exports]
        self.assertEqual(len(addresses),len(set(addresses)))

    def test_erasures_use_same_seed_and_native_tile_edge(self):
        edge=self.plan['erasure_schedules']['cycle-edge'];cycle=self.plan['erasure_private_tile_dwells']
        self.assertEqual(edge,[cycle-1,cycle])
        for bit in (0,1):
            jobs=[j for j in self.plan['jobs'] if j['stage']=='export' and j['case'].startswith('erasures-') and j['bit']==bit]
            self.assertEqual(len(jobs),5)
            self.assertEqual(len({j['seed_address_index'] for j in jobs}),1)
        for ids in self.plan['erasure_schedules'].values():self.assertEqual(ids,sorted(set(ids)))

    def test_long_tier_is_separate_and_has_real_span(self):
        plan=make.build_long_plan(self.inputs,ROOT)
        self.assertEqual(plan['stage_counts'],{'configure':4,'export':8,'actual':16,'conditional':0,'benchmark':0})
        self.assertEqual(plan['conditional_paths'],[])
        for row in plan['configuration_cases']:
            self.assertEqual(row['case']['seconds'],850*86400)
            self.assertEqual(row['case']['width'],0)
            self.assertEqual(make.option(row['physical_argv'],'--cn0'),'-60')
            self.assertIn(row['sample_rate'],(.25,.5))
        for job in plan['jobs']:runner.check_job(job)

    def test_owned_runner_records_success_failure_timeout_without_retry(self):
        scratch=ROOT/'local-evidence/triad-broad-coverage/plans'
        scratch.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='runner-fixture-',dir=scratch) as temp:
            base=Path(temp)
            good="import sys;assert sys.prefix == "+repr(sys.prefix)+";import json;print(json.dumps(dict(mode='configure',configuration=dict(capture=dict(feasible_for_deterministic_prior=True),public_search=dict()))))"
            jobs=[make.make_job('fixture-'+name,'configure',['{PYTHON}','-c',code],timeout,
                case='synthetic-runner-fixture',depends_on=[]) for name,code,timeout in
                [('ok',good,2),('fail','raise SystemExit(3)',2),('timeout','import time;time.sleep(10)',.1)]]
            (base/'source.tar').write_bytes(b'synthetic owned runner fixture archive')
            bounded=dict(outcome='completed',exit_code=0,child_exit_code=0,timed_out=False,cancelled=False,termination=dict(group_empty=True))
            provenance=dict(all_required_checks_passed=True,binary_sha256=runner.sha(sys.executable),
                source_archive_sha256=runner.sha(base/'source.tar'),bounded_check_status=dict(fixture=bounded))
            (base/'build.json').write_text(json.dumps(provenance))
            plan={'jobs':jobs,'source_file_sha256':{},'build_provenance_sha256':runner.sha(base/'build.json'),
                'source_archive_sha256':provenance['source_archive_sha256']}
            (base/'plan.json').write_text(json.dumps(plan))
            command=[sys.executable,str(HERE/'run_plan.py'),'--plan',str(base/'plan.json'),
                '--stage','configure','--root',str(ROOT),'--output-root',str(base/'out'),
                '--binary',sys.executable,'--run-dir',str(base/'run'),'--jobs','2','--global-seconds','30',
                '--build-provenance',str(base/'build.json'),'--source-archive',str(base/'source.tar')]
            result=subprocess.run(command,capture_output=True,text=True,timeout=20)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            receipt=json.loads((base/'run/results.json').read_text())
            rows={r['id']:r for r in receipt['jobs']}
            self.assertEqual(rows['fixture-ok']['status'],'completed')
            self.assertEqual(rows['fixture-fail']['status'],'execution_failed')
            self.assertEqual(rows['fixture-timeout']['status'],'execution_failed')
            self.assertEqual(rows['fixture-timeout']['wrapper_returncode'],124)
            self.assertTrue(receipt['all_selected_jobs_accounted'])
            # A fresh stage cannot implicitly repeat an already accounted job.
            command[command.index('--run-dir')+1]=str(base/'second-run')
            command += ['--prior-results',str(base/'run/results.json')]
            repeated=subprocess.run(command,capture_output=True,text=True,timeout=10)
            self.assertNotEqual(repeated.returncode,0)
            self.assertIn('no implicit rerun',repeated.stderr)

    def test_independent_recount_preserves_wrong_accepts_and_controls(self):
        analyze=load('analyze')
        scores=[[30.,0.],[0.,30.],[1.,2.]]
        self.assertEqual(analyze.recount(scores,0,20,'signal'),
            {'trials':3,'correct_accepted':1,'wrong_accepted':1,'missed':1,'raw_correct':1})
        self.assertEqual(analyze.recount(scores,0,20,'wrong-key'),
            {'trials':3,'false_admitted':2,'not_admitted':1})
        with self.assertRaisesRegex(ValueError,'finite'):
            analyze.recount([[float('nan'),0]],0,20,'signal')

if __name__=='__main__':unittest.main()
