import copy
import importlib.util
import json
import math
from pathlib import Path
import tempfile
import unittest

import numpy as np

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('selective_screen_analyzer_test',HERE/'analyze_screen.py')
a=importlib.util.module_from_spec(spec);spec.loader.exec_module(a)


def make_fixture(root,actual=False):
    evidence=root/'evidence';(evidence/'scores').mkdir(parents=True)
    original=Path('/remote/selective');output=original/'scores/case.json'
    inputs=[str(original/'screen/case'/f'path-{i}') for i in (0,1)]
    config={'false_probability':1e-4,'horizon_seconds':86400,'attempt_spacing':60.,
            'selection_methods':16,'duration_alternatives':8,'receiver_methods':['gaussian-window-ucb'],
            'cn0_grid':[] if actual else [-30.,-25.],'controls':['signal'] if actual else ['signal','null'],
            'noise_trials':1 if actual else 2,'seed':7,'mode':'actual-noisy' if actual else 'conditional'}
    gate=a.REFERENCE.gates(config)['strict'];records=[];arrays={}
    for bit,source in enumerate(inputs):
        path=evidence/'screen/case'/f'path-{bit}';path.mkdir(parents=True)
        m={'format':'triad-block-stats-v1','sample_rate':2.,'sample_count':32,'cn0_db_hz':-25.,'control':'signal',
           'signal_bit':bit,'key_seed':10+bit,'random_seed_roots':{k:100*j+bit for j,k in enumerate(('channel','oscillator','noise','frontend','path'),1)},
           'source_format':{'inner_rate_hz':.036},'channel_model':{'spread_doppler_hz':.001,
           'perturbation_slow_depth':.65,'perturbation_fast_depth':.65},'requested_active_seconds':8.,'actual_elapsed_seconds':16.}
        (path/'manifest.json').write_text(json.dumps(m))
        record={'input':source,'physical_path_index':bit,'status':'completed','truth_bit_simulation_only':bit,
                'input_hashes':{'manifest.json':a.sha256(path/'manifest.json'),'mean.c128':'a'*64},'conditions':[]}
        for cn0 in ([-25.] if actual else [-30.,-25.]):
            for control in (['signal'] if actual else ['signal','null']):
                key=f'p{bit}_c{cn0}_{control}'
                if actual:
                    scores=np.zeros((1,2));scores[0,bit]=gate+2
                    counts={'trials':1,'correct_accepted':1,'wrong_accepted':0,'missed':0,'raw_correct':1}
                elif control=='signal':
                    scores=np.zeros((2,2));scores[:,bit]=[gate+1,gate-1]
                    counts={'trials':2,'correct_accepted':1,'wrong_accepted':0,'missed':1,'raw_correct':2}
                else:
                    scores=np.array([[0.,0.],[0.,gate+1]])
                    counts={'trials':2,'false_admitted':1,'not_admitted':1}
                arrays[key]=scores
                record['conditions'].append({'cn0_db_hz':cn0,'control':control,'noise_trials':len(scores),
                    'methods':{'gaussian-window-ucb':{'score_array':key,'threshold':gate,'counts':counts}}})
        records.append(record)
    scorefile=evidence/'scores/case.scores.npz';np.savez(scorefile,**arrays)
    report={'schema':'triad-conditional-sweep-v1','configuration':config,'observation_mode':config['mode'],
            'paths':records,'source_sha256':'b'*64,'dependency_sha256':{},'scores_file':scorefile.name,
            'scores_sha256':a.sha256(scorefile),'failed_physical_path_records':0,'sampled_physical_paths':2}
    reportfile=evidence/'scores/case.json';reportfile.write_text(json.dumps(report))
    argv=['python','/remote/triad-conditional-sweep.py',*inputs,'--output',str(output),'--methods','gaussian-window-ucb','--seed','7']
    argv+=['--mode','actual-noisy'] if actual else ['--cn0-grid=-30,-25','--controls','signal','null','--noise-trials','2']
    plan={'jobs':[{'name':'case','argv':argv}]};planfile=root/'plan.json';planfile.write_text(json.dumps(plan))
    return evidence,planfile,reportfile,report,arrays


def rewrite(reportfile,report,arrays):
    np.savez(reportfile.parent/report['scores_file'],**arrays)
    report['scores_sha256']=a.sha256(reportfile.parent/report['scores_file'])
    reportfile.write_text(json.dumps(report))


class AnalyzeScreenTests(unittest.TestCase):
    def test_conditional_recount_separates_paths_gates_and_nulls_without_pooled_cp(self):
        with tempfile.TemporaryDirectory() as tmp:
            evidence,plan,*_=make_fixture(Path(tmp));d=a.analyze(evidence,plan)
            self.assertTrue(d['passed_integrity_and_complete_execution'])
            case=d['cases'][0];self.assertEqual(case['observation_mode'],'conditional')
            self.assertEqual(len(case['groups']),4)
            for g in case['groups']:
                self.assertEqual(g['completed_physical_paths'],2)
                self.assertEqual(len(g['physical_path_clusters']),2)
                self.assertTrue(g['clustered_by_physical_path'])
                self.assertIsNone(g['gates']['strict']['conditional_pooled_cp'])
                self.assertIsNone(g['gates']['strict']['actual_balanced_mean_confidence'])
                if g['control']=='signal':
                    self.assertEqual(g['gates']['strict']['counts']['correct_accepted'],2)
                    self.assertEqual(g['gates']['strict']['counts']['missed'],2)
                    self.assertEqual(g['gates']['relaxed']['counts']['correct_accepted'],4)
                else:self.assertEqual(g['gates']['strict']['counts']['false_admitted'],2)
            checks=case['input_file_checks']
            self.assertTrue(any(x['state']=='recorded_receipt_not_rehashed' for x in checks))
            self.assertEqual(d['path_mapping']['source_root'],'/remote/selective')

    def test_actual_balanced_mean_all_success_and_known_failure_counts_miss(self):
        with tempfile.TemporaryDirectory() as tmp:
            evidence,plan,path,report,arrays=make_fixture(Path(tmp),True)
            d=a.analyze(evidence,plan);g=d['cases'][0]['groups'][0]
            confidence=g['gates']['strict']['actual_balanced_mean_confidence']
            self.assertAlmostEqual(confidence['balanced_mean_correct_probability_lower_95'],math.sqrt(.05))
            self.assertEqual(g['gates']['strict']['per_bit_counts']['1']['trials'],1)
            bad=report['paths'][1];bad['status']='error';bad['error']='sampled export failed';
            for c in bad['conditions']:arrays.pop(c['methods']['gaussian-window-ucb']['score_array'])
            bad['conditions']=[];report['failed_physical_path_records']=1;rewrite(path,report,arrays)
            d=a.analyze(evidence,plan);self.assertFalse(d['passed_integrity_and_complete_execution'])
            g=d['cases'][0]['groups'][0]
            self.assertEqual(g['gates']['strict']['counts']['trials'],2)
            self.assertEqual(g['gates']['strict']['counts']['missed'],1)
            self.assertEqual(g['gates']['strict']['per_bit_counts']['1']['missed'],1)
            self.assertIsNotNone(g['gates']['strict']['actual_balanced_mean_confidence'])

    def test_unknown_actual_failure_does_not_create_a_confidence_bound(self):
        with tempfile.TemporaryDirectory() as tmp:
            evidence,plan,path,report,arrays=make_fixture(Path(tmp),True)
            bad=report['paths'][1]
            for c in bad['conditions']:arrays.pop(c['methods']['gaussian-window-ucb']['score_array'])
            bad.update(status='error',conditions=[],input_hashes={});bad.pop('truth_bit_simulation_only')
            (evidence/'screen/case/path-1/manifest.json').unlink()
            report['failed_physical_path_records']=1;rewrite(path,report,arrays)
            d=a.analyze(evidence,plan);g=d['cases'][0]['groups'][0]
            self.assertFalse(g['attempt_denominator_established'])
            self.assertEqual(g['gates']['strict']['counts']['missed'],1)
            self.assertIsNone(g['gates']['strict']['actual_balanced_mean_confidence'])

    def test_partial_score_rows_are_validated_and_excluded(self):
        with tempfile.TemporaryDirectory() as tmp:
            evidence,plan,path,report,arrays=make_fixture(Path(tmp))
            report['paths'][1]['status']='error';report['failed_physical_path_records']=1
            rewrite(path,report,arrays);d=a.analyze(evidence,plan);case=d['cases'][0]
            self.assertEqual(case['status'],'incomplete')
            self.assertEqual(case['failures'][0]['partial_conditions_excluded'],4)
            for g in case['groups']:
                self.assertEqual(g['completed_physical_paths'],1)
                self.assertEqual(g['failed_or_missing_physical_paths'],1)
                self.assertEqual(g['gates']['strict']['counts']['trials'],2)

    def test_missing_planned_records_and_reports_remain_incomplete(self):
        with tempfile.TemporaryDirectory() as tmp:
            evidence,plan,path,report,arrays=make_fixture(Path(tmp));bad=report['paths'].pop()
            for c in bad['conditions']:arrays.pop(c['methods']['gaussian-window-ucb']['score_array'])
            report['sampled_physical_paths']=1;rewrite(path,report,arrays)
            d=a.analyze(evidence,plan);case=d['cases'][0]
            self.assertEqual(case['status'],'incomplete');self.assertFalse(case['coverage_complete'])
            self.assertEqual(case['failures'][0]['status'],'missing_report_record')
            path.unlink();d=a.analyze(evidence,plan)
            self.assertEqual(d['summary']['error_jobs'],1)

    def test_corrupt_counts_hashes_gates_options_and_unscored_keys_fail_closed(self):
        for mutation in ('counts','hash','gate','config','extra','condition','trials','mode'):
            with self.subTest(mutation=mutation),tempfile.TemporaryDirectory() as tmp:
                evidence,plan,path,report,arrays=make_fixture(Path(tmp))
                item=report['paths'][0]['conditions'][0]['methods']['gaussian-window-ucb']
                if mutation=='counts':item['counts']['correct_accepted']=2
                if mutation=='gate':item['threshold']+=1
                if mutation=='config':report['configuration']['seed']=8
                if mutation=='trials':
                    condition=report['paths'][0]['conditions'][0];condition['noise_trials']=1
                    key=condition['methods']['gaussian-window-ucb']['score_array'];arrays[key]=arrays[key][:1]
                    item['counts']={'trials':1,'correct_accepted':1,'wrong_accepted':0,'missed':0,'raw_correct':1}
                if mutation=='mode':report['observation_mode']='actual-noisy'
                if mutation=='extra':arrays['unscored']=np.zeros((2,2))
                if mutation=='condition':
                    condition=report['paths'][0]['conditions'].pop()
                    arrays.pop(condition['methods']['gaussian-window-ucb']['score_array'])
                rewrite(path,report,arrays)
                if mutation=='hash':report['scores_sha256']='0'*64;path.write_text(json.dumps(report))
                d=a.analyze(evidence,plan)
                self.assertEqual(d['summary']['error_jobs'],1)
                self.assertFalse(d['passed_integrity_and_complete_execution'])

    def test_requested_raw_verification_fails_if_input_arrays_unavailable(self):
        with tempfile.TemporaryDirectory() as tmp:
            evidence,plan,*_=make_fixture(Path(tmp))
            self.assertTrue(a.analyze(evidence,plan)['passed_integrity_and_complete_execution'])
            self.assertFalse(a.analyze(evidence,plan,verify_arrays=True)['passed_integrity_and_complete_execution'])

    def test_retention_is_bound_to_manifest_and_does_not_replace_nominal_cn0(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);evidence,plan,path,report,_=make_fixture(root)
            record=report['paths'][0]
            retention={'schema':'triad-selective-retention-v1','passed':True,'workspaces':[{
                'status':'completed','input':record['input'],'input_sha256':{'manifest.json':record['input_hashes']['manifest.json']},
                'signal_energy_samples':1000000,'correct_label_retained_fraction':.7}]}
            rp=root/'retention.json';rp.write_text(json.dumps(retention))
            d=a.analyze(evidence,plan,[rp]);case=d['cases'][0]
            self.assertEqual({g['cn0_db_hz'] for g in case['groups']},{-30.,-25.})
            self.assertEqual(case['physical_paths'][0]['projection_retention_simulation_only']['correct_label_retained_fraction'],.7)
            retention['workspaces'][0]['input_sha256']['manifest.json']='0'*64;rp.write_text(json.dumps(retention))
            self.assertFalse(a.analyze(evidence,plan,[rp])['passed_integrity_and_complete_execution'])

    def test_retention_overlapping_raw_hash_mismatch_reproduces_original_gap(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);evidence,plan,path,report,_=make_fixture(root)
            record=report['paths'][0]
            retention={'schema':'triad-selective-retention-v1','passed':True,'workspaces':[{
                'status':'completed','input':record['input'],
                'input_sha256':{'manifest.json':record['input_hashes']['manifest.json'],'mean.c128':'c'*64},
                'correct_label_retained_fraction':.7}]}
            rp=root/'retention.json';rp.write_text(json.dumps(retention))
            self.assertEqual(record['input_hashes']['mean.c128'],'a'*64)
            bad=a.analyze(evidence,plan,[rp])
            self.assertFalse(bad['passed_integrity_and_complete_execution'])
            self.assertIn('hash receipts disagree: mean.c128',bad['cases'][0]['error'])
            retention['workspaces'][0]['input_sha256']['mean.c128']='a'*64;rp.write_text(json.dumps(retention))
            good=a.analyze(evidence,plan,[rp]);self.assertTrue(good['passed_integrity_and_complete_execution'])
            checks=good['cases'][0]['physical_paths'][0]['projection_retention_binding']['input_file_checks']
            mean=next(x for x in checks if x['file']=='mean.c128')
            self.assertEqual(mean['role'],'receiver_and_retention')
            self.assertEqual(mean['state'],'recorded_receipt_not_rehashed')
            retention['workspaces'][0]['input_sha256']['../mean.c128']='a'*64;rp.write_text(json.dumps(retention))
            self.assertFalse(a.analyze(evidence,plan,[rp])['passed_integrity_and_complete_execution'])

    def test_actual_retention_only_truth_arrays_are_explicit_and_optionally_rehashed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);evidence,plan,path,report,arrays=make_fixture(root,True)
            for record in report['paths']:
                # The actual receiver consumes neither noiseless diagnostic file.
                record['input_hashes'].pop('mean.c128')
            rewrite(path,report,arrays);record=report['paths'][0]
            directory=evidence/'screen/case/path-0'
            for name in ('mean.c128','signal_energy.f64'):(directory/name).write_bytes(b'known diagnostic bytes')
            hashes={name:a.sha256(directory/name) for name in ('mean.c128','signal_energy.f64')}
            hashes['manifest.json']=record['input_hashes']['manifest.json']
            retention={'schema':'triad-selective-retention-v1','passed':True,'workspaces':[{
                'status':'completed','input':record['input'],'input_sha256':hashes,'correct_label_retained_fraction':.7}]}
            rp=root/'retention.json';rp.write_text(json.dumps(retention))
            d=a.analyze(evidence,plan,[rp]);self.assertTrue(d['passed_integrity_and_complete_execution'])
            binding=d['cases'][0]['physical_paths'][0]['projection_retention_binding']
            self.assertEqual(binding['retention_only_input_files'],['mean.c128','signal_energy.f64'])
            checks={x['file']:x for x in binding['input_file_checks']}
            self.assertEqual(checks['mean.c128']['role'],'retention_only_simulation_diagnostic')
            self.assertEqual(checks['mean.c128']['state'],'recorded_retention_receipt_not_rehashed')
            verified=a.analyze(evidence,plan,[rp],verify_arrays=True)
            self.assertTrue(verified['passed_integrity_and_complete_execution'])
            checks=verified['cases'][0]['physical_paths'][0]['projection_retention_binding']['input_file_checks']
            self.assertTrue(all(x['state']=='verified' for x in checks))
            (directory/'mean.c128').write_bytes(b'changed diagnostic bytes')
            # Metadata-only mode honestly preserves its unverified status.
            self.assertTrue(a.analyze(evidence,plan,[rp])['passed_integrity_and_complete_execution'])
            self.assertFalse(a.analyze(evidence,plan,[rp],verify_arrays=True)['passed_integrity_and_complete_execution'])

    def test_confidence_allows_distinct_bit_probabilities_and_refuses_duplicate_roots(self):
        bybit={str(b):CounterLike(trials=2,correct_accepted=1) for b in (0,1)}
        c=a._confidence(bybit)
        self.assertAlmostEqual(c['balanced_mean_correct_probability_lower_95'],1-math.sqrt(.975))
        with tempfile.TemporaryDirectory() as tmp:
            evidence,plan,path,report,arrays=make_fixture(Path(tmp),True)
            m0=json.loads((evidence/'screen/case/path-0/manifest.json').read_text())
            p=evidence/'screen/case/path-1/manifest.json';m=json.loads(p.read_text());m['random_seed_roots']=m0['random_seed_roots'];p.write_text(json.dumps(m))
            report['paths'][1]['input_hashes']['manifest.json']=a.sha256(p);rewrite(path,report,arrays)
            d=a.analyze(evidence,plan)
            self.assertIsNone(d['cases'][0]['groups'][0]['gates']['strict']['actual_balanced_mean_confidence'])


class CounterLike(dict):
    def __missing__(self,key):return 0


if __name__=='__main__':unittest.main()
