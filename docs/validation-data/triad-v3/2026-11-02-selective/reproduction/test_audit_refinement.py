#!/usr/bin/env python3
"""Independent raw-row fixtures for complete twofold refinement auditing."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

import numpy as np

HERE=Path(__file__).resolve().parent
SPEC=importlib.util.spec_from_file_location('selective_refinement_audit',HERE/'audit_refinement.py')
audit=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(audit)
FIX=importlib.util.spec_from_file_location('accepted_raw_row_fixture',audit.HELPER_PATH.with_name('test_audit_compression.py'))
fixture=importlib.util.module_from_spec(FIX);FIX.loader.exec_module(fixture)


class RefinementTests(unittest.TestCase):
    def setUp(self):
        self.raw=fixture.CompressionAuditTests('test_raw_sample_sums_with_erasure_and_partial_last_block')
        self.raw.setUp();self.addCleanup(self.raw.doCleanups);self.root=self.raw.root

    def workspace(self,name,block,n=14):
        path=self.raw.workspace(name,block,n);manifest=json.loads((path/'manifest.json').read_text())
        for field in audit.TRUTH_FIELDS:manifest[field]=0.
        manifest['path_truth_simulation_only']={'selective_perturbation':{'enabled':False}}
        (path/'manifest.json').write_text(json.dumps(manifest));return path

    def test_twofold_raw_sums_cover_erasure_and_entire_partial_tail(self):
        refinement=self.workspace('fine',2);parent=self.workspace('parent',4)
        result=audit.audit_pair(refinement,parent)
        self.assertTrue(result['passed']);self.assertTrue(result['complete_capture_coverage'])
        self.assertEqual(result['last_parent_refinement_blocks'],1)
        self.assertEqual(result['refinement_samples'],14);self.assertEqual(result['parent_samples'],14)
        for name in audit.BASE.ARRAYS:
            self.assertTrue(result['statistics'][name]['passed'])
            self.assertEqual(result['statistics'][name]['coarse_blocks_compared'],4)
        self.assertEqual(len(result['file_provenance']['refinement']),8)
        self.assertEqual(len(result['file_provenance']['parent']),8)

    def test_changed_actual_noise_is_rejected(self):
        refinement=self.workspace('fine',2);parent=self.workspace('parent',4)
        value=np.fromfile(parent/'observation.c128',dtype='<c16');value[0]+=.01;value.tofile(parent/'observation.c128')
        result=audit.audit_pair(refinement,parent)
        self.assertFalse(result['passed']);self.assertFalse(result['statistics']['observation']['passed'])
        self.assertTrue(result['statistics']['mean']['passed'])

    def test_changed_seed_and_dam_truth_are_visible(self):
        refinement=self.workspace('fine',2);parent=self.workspace('parent',4)
        path=parent/'manifest.json';manifest=json.loads(path.read_text())
        manifest['random_seed_roots']['noise']+=1
        manifest['path_truth_simulation_only']['selective_perturbation']['enabled']=True
        path.write_text(json.dumps(manifest));result=audit.audit_pair(refinement,parent)
        self.assertFalse(result['passed']);self.assertFalse(result['metadata_parity'])
        self.assertIn('random_seed_roots.noise',result['unexpected_metadata_differences'])
        self.assertIn('path_truth_simulation_only.selective_perturbation.enabled',result['unexpected_metadata_differences'])

    def test_missing_capture_tail_never_compares_a_shorter_prefix(self):
        refinement=self.workspace('fine',2,n=14);parent=self.workspace('parent',4,n=16)
        result=audit.audit_pair(refinement,parent)
        self.assertFalse(result['passed']);self.assertFalse(result['complete_capture_coverage'])
        self.assertEqual(result['refinement_samples'],14);self.assertEqual(result['parent_samples'],16)
        self.assertTrue(all(x['state']=='not_compared' and not x['passed'] for x in result['statistics'].values()))
        self.assertEqual(len(result['file_provenance']['parent']),8)

    def test_real_plan_has_exact_eight_pairs_and_rejects_incomplete_matrix(self):
        plan=audit.BASE.read_json(HERE/'long-refinement-score-plan.json')
        self.assertEqual(len(audit.planned_pairs(self.root,plan)),8)
        bad=copy.deepcopy(plan);bad['paired_parent_inputs'][0]['parent'].pop()
        with self.assertRaisesRegex(ValueError,'four physical paths'):audit.planned_pairs(self.root,bad)
        bad=copy.deepcopy(plan);bad['jobs'][0]['argv'][2]=bad['jobs'][1]['argv'][2]
        with self.assertRaisesRegex(ValueError,'exactly the declared'):audit.planned_pairs(self.root,bad)

    def test_expected_physical_block_duration_is_enforced(self):
        refinement=self.workspace('fine',2);parent=self.workspace('parent',4)
        with self.assertRaisesRegex(ValueError,'durations differ from plan'):audit.audit_pair(refinement,parent,4096)


if __name__=='__main__':unittest.main()
