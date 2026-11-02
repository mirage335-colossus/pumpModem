#!/usr/bin/env python3
import importlib.util
import math
from pathlib import Path
import unittest


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("audit_holdout", HERE / "audit_holdout.py")
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


def outcome(correct=False, wrong=False, accepted=None):
    if accepted is None:
        accepted = correct or wrong
    return {"correct_accepted": int(correct), "wrong_accepted": int(wrong),
            "false_admitted": int(accepted), "not_admitted": int(not accepted), "accepted": accepted}


class HoldoutAuditTests(unittest.TestCase):
    def test_frozen_export_score_plans_are_path_and_seed_bound(self):
        export = AUDIT.read_json(HERE / "holdout-export-plan.json")
        score = AUDIT.read_json(HERE / "holdout-score-plan.json")
        binding = AUDIT._validate_plan_pair(export, score)
        self.assertEqual(binding["planned_path_count"], 2892)
        self.assertTrue(all(cell["all_planned_vectors_distinct_within_bit"]
                            for cell in binding["expected_seed_distinctness_by_cell"].values()))
        self.assertEqual(binding["expected_seed_distinctness_by_cell"]["g001-each-65-cn0m26-signal"]
                         ["planned_vector_counts_by_source_bit"], {"0": 150, "1": 150})

    def test_all_correct_balanced_cell_has_documented_pointwise_bound(self):
        expected = [{"index": i, "source_bit": i % 2} for i in range(300)]
        scored = {i: {"strict": outcome(correct=True), "relaxed": outcome(correct=True)} for i in range(300)}
        result = AUDIT.cell_counts(expected, scored, "signal")
        strict = result["gates"]["strict"]
        self.assertEqual(strict["correct_accepted"], 300)
        self.assertEqual(strict["per_bit"]["0"]["trials"], 150)
        self.assertEqual(strict["per_bit"]["1"]["trials"], 150)
        self.assertAlmostEqual(strict["balanced_mean_correct_probability_lower_95"], .05 ** (1 / 300))

    def test_missing_signal_is_a_miss_in_fixed_denominator_and_wrong_is_not_correct(self):
        expected = [{"index": i, "source_bit": i % 2} for i in range(4)]
        scored = {
            0: {"strict": outcome(correct=True), "relaxed": outcome(correct=True)},
            1: {"strict": outcome(wrong=True), "relaxed": outcome(correct=True)},
            2: {"strict": outcome(), "relaxed": outcome(correct=True)},
        }
        result = AUDIT.cell_counts(expected, scored, "signal")["gates"]["strict"]
        self.assertEqual(result["attempted"], 4)
        self.assertEqual(result["correct_accepted"], 1)
        self.assertEqual(result["wrong_accepted"], 1)
        self.assertEqual(result["missed"], 2)
        self.assertEqual(result["unscored"], 1)
        self.assertEqual(result["per_bit"]["1"]["missed"], 1)

    def test_missing_control_is_worst_case_false_admission(self):
        expected = [{"index": i, "source_bit": i % 2} for i in range(4)]
        scored = {0: {"strict": outcome(), "relaxed": outcome()},
                  1: {"strict": outcome(accepted=True), "relaxed": outcome(accepted=True)}}
        strict = AUDIT.cell_counts(expected, scored, "null")["gates"]["strict"]
        self.assertEqual(strict["false_admitted"], 3)
        self.assertEqual(strict["not_admitted"], 1)
        self.assertEqual(strict["unscored"], 2)

    def test_equal_bit_stratum_bound_uses_bonferroni_when_not_all_successes(self):
        expected = [{"index": i, "source_bit": i % 2} for i in range(20)]
        scored = {i: {"strict": outcome(correct=(i != 0)), "relaxed": outcome(correct=(i != 0))}
                  for i in range(20)}
        gate = AUDIT.cell_counts(expected, scored, "signal")["gates"]["strict"]
        self.assertIn("Bonferroni", gate["balanced_mean_bound_method"])
        self.assertLess(gate["balanced_mean_correct_probability_lower_95"], 1)
        self.assertGreater(gate["balanced_mean_correct_probability_lower_95"], 0)

    def test_path_mapping_rejects_escape_and_maps_exact_prefix(self):
        mapping = AUDIT.PathMap(Path("/tmp/audit-root"), Path("/workspace/triad"))
        self.assertEqual(mapping.resolve("/workspace/triad/local-evidence/x"), Path("/tmp/audit-root/local-evidence/x"))
        with self.assertRaises(ValueError):
            mapping.resolve("/workspace/triad/../outside")
        with self.assertRaises(ValueError):
            mapping.resolve("/other/evidence")

    def test_manifest_seed_identity_requires_all_six_roots_and_key_seed(self):
        manifest = {"key_seed": 1, "random_seed_roots": {
            "channel": 2, "oscillator": 3, "noise": 4, "frontend": 5, "path": 6}}
        self.assertEqual(AUDIT._seed_roots(manifest), {
            "key_seed": 1, "channel_seed": 2, "oscillator_seed": 3,
            "noise_seed": 4, "frontend_seed": 5, "path_seed": 6})
        del manifest["random_seed_roots"]["frontend"]
        self.assertIsNone(AUDIT._seed_roots(manifest))

    def test_zero_trial_stratum_has_no_bound(self):
        self.assertEqual(AUDIT.cp_lower(0, 0, .05), 0)
        self.assertTrue(math.isfinite(AUDIT.cp_lower(8, 10, .05)))



# Full-flow fixtures are generated by the real sampled exporter and real actual
# receiver, once per test class. They require an already built isolated binary;
# no test triggers a build or uses an external provider. Metadata is copied into
# temporary workspaces before negative mutations; source evidence stays intact.
import copy
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile


def actual_fixture(root):
    original_export=AUDIT.read_json(HERE/'holdout-export-plan.json')
    original_score=AUDIT.read_json(HERE/'holdout-score-plan.json')
    binary=next((p for p in (AUDIT.CAMPAIGN.ROOT/'build/dev/triad_long',
                AUDIT.CAMPAIGN.ROOT/'build/dev-cli/triad_long') if p.is_file()),None)
    if binary is None:raise unittest.SkipTest('actual-schema audit fixture requires a prebuilt isolated triad_long binary')
    job=copy.deepcopy(original_export['jobs'][0]);argv=job['argv']
    argv[0]=sys.executable;argv[1]=str(AUDIT.CAMPAIGN_PATH)
    values={'--binary':str(binary),'--output':str(root/'holdout/fixture/batch00'),
            '--paths':'2','--start-index':'90000','--jobs':'2','--timeout':'30','--seconds':'8','--cn0':'8'}
    for flag,value in values.items():argv[argv.index(flag)+1]=value
    subprocess.run(argv,check=True,capture_output=True,text=True,timeout=90)
    args=AUDIT._options(argv);campaign_path=Path(args['output'])/'campaign.json'
    campaign=AUDIT.read_json(campaign_path)
    name='fixture-batch00';cell=copy.deepcopy(original_export['cells'][0]);cell.update(
        name='fixture',sampled_cn0_db_hz=8.,requested_active_seconds=8.,path_count=2,
        source_bit_counts={'0':1,'1':1},path_index_first=90000,path_index_last=90001,batch_jobs=[name])
    job.update(name=name,cell='fixture',control='signal',path_count=2,start_index=90000,
        simulation_only_expected=[{'index':i,**AUDIT._expected_child(args,i,Path(args['output'])/f'path-{i:05d}')[1]}
                                   for i in (90000,90001)])
    common={k:copy.deepcopy(v) for k,v in original_export.items() if k not in ('jobs','operation')}
    common.update(signal_cell_count=1,control_cell_count=0,cell_count=1,signal_export_count=2,
                  control_export_count=0,sampled_export_count=2,job_count=1,
                  distinct_seed_vectors_across_all_cells=2,cells=[cell])
    common['receiver'].update(device='cpu',precision='fp64')
    common['source_defaults']['start_index']=90000
    common['seed_generation_source']['sha256']=AUDIT.sha256(AUDIT.CAMPAIGN_PATH)
    eplan={**copy.deepcopy(common),'operation':'actual sampled-waveform export','jobs':[job]}
    sj=copy.deepcopy(original_score['jobs'][0]);old=sj['argv'];tail=old[old.index('--output'):]
    tail[tail.index('--output')+1]=str(root/'scores/fixture.json')
    tail[tail.index('--device')+1]='cpu';tail[tail.index('--precision')+1]='fp64'
    inputs=[str(Path(args['output'])/f'path-{i:05d}') for i in (90000,90001)]
    sj.update(name=name,cell='fixture',control='signal',path_count=2,start_index=90000,
              argv=[sys.executable,str(AUDIT.CAMPAIGN_PATH.with_name('triad-conditional-sweep.py')),*inputs,*tail])
    splan={**copy.deepcopy(common),'operation':'actual sampled-noise receiver validation','jobs':[sj]}
    subprocess.run(sj['argv'],check=True,capture_output=True,text=True,timeout=90)
    report=AUDIT.read_json(root/'scores/fixture.json')
    files=dict(campaign['source_inputs']['files'])
    for filename in ['triad-conditional-sweep.py',*sorted(AUDIT.SCORER_DEPENDENCIES)]:
        files['tools/'+filename]=AUDIT.sha256(AUDIT.CAMPAIGN_PATH.with_name(filename))
    provenance={'schema':'triad-selective-build-provenance-v1','all_extracted_source_files_match_bundle':True,
        'source_bundle_sha256':hashlib.sha256(b'test-only trusted provenance declaration').hexdigest(),
        'binary_sha256':campaign['binary_sha256'],'source_file_sha256':files,
        'scope':'Test-only provenance declaration for the actual local exporter/receiver fixture; no cloud build assertion.'}
    for name,value in [('export-plan.json',eplan),('score-plan.json',splan),('provenance.json',provenance)]:
        (root/name).write_text(json.dumps(value,indent=2)+'\n')
    return root


class ActualSchemaHoldoutAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.original=actual_fixture(Path(cls.temp.name))

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def setUp(self):
        self.temp_case=tempfile.TemporaryDirectory();self.local=Path(self.temp_case.name)/'copy'
        shutil.copytree(self.original,self.local)

    def tearDown(self):self.temp_case.cleanup()

    def run_audit(self,verify=False):
        return AUDIT.audit(self.local/'export-plan.json',self.local/'score-plan.json',
            self.local,self.original,source_provenance_path=self.local/'provenance.json',verify_arrays=verify)

    def campaign(self):return self.local/'holdout/fixture/batch00/campaign.json'
    def report(self):return self.local/'scores/fixture.json'

    def change_manifest(self,change,mirror=True):
        campaign=AUDIT.read_json(self.campaign());record=campaign['results'][0]
        path=self.local/'holdout/fixture/batch00'/record['directory']/'manifest.json'
        manifest=AUDIT.read_json(path);change(manifest);path.write_text(json.dumps(manifest))
        if mirror:record['export_manifest']=copy.deepcopy(manifest)
        self.campaign().write_text(json.dumps(campaign))
        report=AUDIT.read_json(self.report());report['paths'][0]['input_hashes']['manifest.json']=AUDIT.sha256(path)
        self.report().write_text(json.dumps(report))

    def test_actual_two_path_end_to_end_audit_and_raw_verification(self):
        result=self.run_audit(True)
        self.assertTrue(result['integrity_complete'])
        self.assertEqual(result['cells'][0]['integrity']['scored_paths'],2)
        self.assertTrue(all(r['raw_bytes_rehashed'] for r in result['evidence']['scorer_manifest_inventory_receipts']))
        args=['--root',str(self.local),'--source-root',str(self.original),'--export-plan',str(self.local/'export-plan.json'),
              '--score-plan',str(self.local/'score-plan.json'),'--source-provenance',str(self.local/'provenance.json'),
              '--output',str(self.local/'positive-cli-audit.json')]
        self.assertEqual(AUDIT.main(args),0)

    def test_generation_job_source_errors_gate_complete_and_cli_exit(self):
        c=AUDIT.read_json(self.campaign());c['arguments']['rate']=123;self.campaign().write_text(json.dumps(c))
        result=self.run_audit();self.assertFalse(result['integrity_complete'])
        cell=result['cells'][0];self.assertFalse(cell['integrity']['export_complete'])
        self.assertEqual(cell['gates']['strict']['unscored'],2)
        self.assertEqual(cell['gates']['strict']['missed'],2)
        self.assertIsNone(cell['gates']['strict']['balanced_mean_correct_probability_lower_95'])
        self.assertEqual(cell['observed_scoring_diagnostic_counts']['gates']['strict']['unscored'],0)
        args=['--root',str(self.local),'--source-root',str(self.original),'--export-plan',str(self.local/'export-plan.json'),
              '--score-plan',str(self.local/'score-plan.json'),'--source-provenance',str(self.local/'provenance.json'),
              '--output',str(self.local/'negative-cli-audit.json')]
        self.assertEqual(AUDIT.main(args),1)

    def test_empty_or_mismatched_generation_sources_fail_closed(self):
        for field in ('empty','digest','binary'):
            with self.subTest(field=field):
                source=self.original/'holdout/fixture/batch00/campaign.json';c=AUDIT.read_json(source)
                if field=='empty':c['source_inputs']['files']={}
                if field=='digest':c['source_inputs']['sha256']='0'*64
                if field=='binary':c['binary_sha256']='0'*64
                self.campaign().write_text(json.dumps(c));result=self.run_audit()
                self.assertFalse(result['integrity_complete'])
                self.assertFalse(result['cells'][0]['integrity']['export_complete'])

    def test_empty_bundle_provenance_and_empty_inventory_are_rejected(self):
        original=AUDIT.read_json(self.local/'provenance.json')
        for kind in ('bundle','inventory','source'):
            p=copy.deepcopy(original)
            if kind=='bundle':p.pop('source_bundle_sha256')
            elif kind=='inventory':p['source_file_sha256']={}
            else:p['source_file_sha256']['tools/triad-conditional-sweep.py']='0'*64
            (self.local/'provenance.json').write_text(json.dumps(p))
            with self.assertRaises(ValueError):self.run_audit()

    def test_embedded_manifest_noise_gaussian_width_and_effective_am_are_bound(self):
        mutations=[('noise_variance',lambda m:m.__setitem__('noise_variance',m['noise_variance']*100),'noise variance'),
                   ('width',lambda m:m['channel_model']['path_evolution'].__setitem__('gaussian_doppler_width_hz',.123),'gaussian_doppler_width_hz'),
                   ('AM',lambda m:m['channel_model'].__setitem__('perturbation_slow_depth',.1),'perturbation_slow_depth'),
                   ('size',lambda m:m['file_sizes'].__setitem__('received_energy.f64',8),'shape/byte inventory')]
        for _,change,fragment in mutations:
            with self.subTest(fragment=fragment):
                # Restore a fresh true schema before each independent mutation.
                shutil.rmtree(self.local);shutil.copytree(self.original,self.local)
                self.change_manifest(change)
                result=self.run_audit();self.assertFalse(result['integrity_complete'])
                errors=result['export_jobs'][0]['errors']
                expected_paths,_,_,_=AUDIT._actual_exports(AUDIT.read_json(self.local/'export-plan.json'),AUDIT.PathMap(self.local,self.original),AUDIT.read_json(self.local/'provenance.json'),False)
                self.assertTrue(any(fragment in text for p in expected_paths.values() for text in p['export_errors']))
        shutil.rmtree(self.local);shutil.copytree(self.original,self.local)
        self.change_manifest(lambda m:m.__setitem__('receiver_metadata_warning','changed'),mirror=False)
        self.assertFalse(self.run_audit()['integrity_complete'])

    def test_frozen_expected_seed_is_reconstructed_not_merely_distinct(self):
        p=self.local/'export-plan.json';plan=AUDIT.read_json(p)
        plan['jobs'][0]['simulation_only_expected'][0]['key_seed']+=1;p.write_text(json.dumps(plan))
        with self.assertRaisesRegex(ValueError,'reconstructed generator'):self.run_audit()

    def test_exact_receiver_dependencies_inputs_and_implicit_defaults_are_bound(self):
        for kind in ('dependencies','receipts','defaults','source'):
            with self.subTest(kind=kind):
                shutil.rmtree(self.local);shutil.copytree(self.original,self.local)
                report=AUDIT.read_json(self.report())
                if kind=='dependencies':report['dependency_sha256']={}
                if kind=='receipts':
                    for row in report['paths']:row['input_hashes']={'manifest.json':row['input_hashes']['manifest.json']}
                if kind=='defaults':report['configuration']['tap_powers']=[.9,.1]
                if kind=='source':report['source_sha256']='0'*64
                self.report().write_text(json.dumps(report));result=self.run_audit()
                self.assertFalse(result['integrity_complete'])
                self.assertFalse(result['cells'][0]['integrity']['score_complete'])
                self.assertIsNone(result['cells'][0]['gates']['strict']['balanced_mean_correct_probability_lower_95'])

if __name__ == "__main__":
    unittest.main()
