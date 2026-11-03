#!/usr/bin/env python3
"""Small contract checks; no sampled long-duration execution."""
import json
from pathlib import Path
import tempfile
import unittest
import prepare

ROOT=prepare.HERE.parents[4]

def value(argv,key):return argv[argv.index(key)+1]
def strip(argv,keys):
    out=[];i=0
    while i<len(argv):
        if argv[i] in keys:i+=2
        else:out.append(argv[i]);i+=1
    return out

class FollowupChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.core=json.loads((ROOT/'local-evidence/triad-broad-coverage/plans/long-frozen.json').read_text())
        cls.fixed={'jobs':prepare.fixed_prepare.variant_jobs(cls.core,'/immutable/long')}
        cls.jobs,cls.pilot=prepare.build_jobs(cls.core,cls.fixed)

    def test_independent_paths_preserve_physics_and_balance_labels(self):
        original={j['id']:j for j in self.core['jobs']};exports=[j for j in self.jobs if j['stage']=='export']
        self.assertEqual(len(exports),136);self.assertEqual(len(self.pilot),4)
        seeds=[];changed={'--seed','--key-seed','--channel-seed','--oscillator-seed','--noise-seed','--frontend-seed','--path-seed','--signal-bit','--control','--output'}
        for job in exports:
            self.assertEqual(strip(job['argv'],changed),strip(original[job['parent_export_id']]['argv'],changed))
            self.assertEqual(value(job['argv'],'--seconds'),'73440000')
            self.assertEqual(value(job['argv'],'--signal-bit'),str(job['bit']))
            self.assertEqual(value(job['argv'],'--noise-seed'),str(5100000+job['seed_address_index']))
            seeds.append(job['seed_address_index'])
        self.assertEqual(len(set(seeds)),136)
        for case in {j['case'] for j in exports}:
            signals=[j for j in exports if j['case']==case and j['control']=='signal']
            self.assertEqual([sum(j['bit']==b for j in signals) for b in (0,1)],[16,16])
        for job in self.jobs:prepare.runner.check_job(job)

    def test_resource_arm_only_changes_cap_output_and_bound(self):
        fixed={j['id']:j for j in self.fixed['jobs']}
        resource=[j for j in self.jobs if j['stage']=='resource']
        self.assertEqual(len(resource),8)
        for job in resource:
            old=fixed[job['fixed_parent_id']]
            self.assertEqual(strip(job['argv'],{'--memory-gib','--output'}),strip(old['argv'],{'--memory-gib','--output'}))
            self.assertEqual(value(job['argv'],'--memory-gib'),'2')
            self.assertEqual(job['workspace'],old['workspace'])
        benchmarks=[j for j in self.jobs if j['stage']=='benchmark']
        self.assertEqual({(j['backend'],j['precision']) for j in benchmarks},{(b,p) for b in ('cpu','cuda:0') for p in ('fp32','fp64')})
        self.assertEqual(len({j['workspace'] for j in benchmarks}),1)
        for job in benchmarks:
            self.assertIn('--cn0-grid=-66,-63,-60,-57',job['argv'])
            self.assertEqual(value(job['argv'],'--trial-batch'),'32')
            self.assertEqual(value(job['argv'],'--memory-gib'),'8')

    def test_atomic_receipt_refuses_overwrite_and_leaves_complete_json(self):
        scratch=ROOT/'local-evidence/triad-broad-coverage/long-followup'
        scratch.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='receipt-fixture-',dir=scratch) as temp:
            path=Path(temp)/'receipt.json';prepare.atomic_json(path,{'completed':False,'failures':3})
            self.assertEqual(json.loads(path.read_text()),{'completed':False,'failures':3})
            self.assertFalse(path.with_name('receipt.json.pending').exists())
            with self.assertRaises(FileExistsError):prepare.atomic_json(path,{'completed':True})

if __name__=='__main__':unittest.main()
