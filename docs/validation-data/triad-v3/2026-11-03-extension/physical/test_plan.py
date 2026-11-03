from __future__ import annotations
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

HERE=Path(__file__).resolve().parent

def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value

prepare=module('extension_physical_prepare_test',HERE/'prepare.py')
runner=module('extension_physical_runner_test',HERE/'run.py')


def parent():
    # Minimal external-format fixture, independent of ignored campaign captures.
    template=['{BINARY}','--mode','export-group-stats','--seconds','40','--rate','360',
        '--added','10','--sample-rate','4096','--rf-hz','30000000','--nominal-if-hz','1725',
        '--base-oscillator','gpsdo-ocxo','--shift-oscillator','gpsdo-ocxo',
        '--spread-preset','hf-best','--selective-perturbation','each-65','--tap-delay-ms','.5',
        '--tap-doppler-hz','.1','--block','128','--cn0','20','--fhss','--stats-noisy',
        '--start','-.090500000001','.090500000001','.0003986784141013216']
    jobs=[]
    for bit in (0,1):
        argv=template+['--signal-bit',str(bit),'--control','signal','--output','{OUT}/old']
        for domain,flag in enumerate(prepare.SEED_FLAGS,1):argv += [flag,str(domain*1_000_000+105000+bit)]
        jobs.append(dict(stage='export',case='erasures-none',control='signal',bit=bit,argv=argv))
    return {'jobs':jobs}


class PhysicalPlanTests(unittest.TestCase):
    def test_original_default_job_hashes_remain_unchanged(self):
        # Recorded before adding the opt-in maximum-spreading cell. This binds
        # the original argv, seed schedules, job metadata and denominators.
        expected={'paired':'e4c2a95b0435f08784a4f0a3c824fc4fa9059eef8bfe3e9ab7a89c72dce26b0d',
                  'validation':'31fc48cb904df38712f017da632c06736e1effbf1306a4dbd20d1f520a2b7ef3'}
        for suite,digest in expected.items():
            self.assertEqual(prepare.digest(prepare.build(parent(),suite)['jobs']),digest)

    def test_maximum_spreading_requires_explicit_selection(self):
        self.assertNotIn('max-spreading-rate0p1',[c['name'] for c in prepare.build(parent())['cells']])
        for suite,count in (('paired',3),('validation',18)):
            plan=prepare.build(parent(),suite,['max-spreading-rate0p1'],cn0=-16)
            self.assertEqual(len(plan['cells']),1)
            self.assertEqual(plan['cells'][0]['group_count'],15)
            self.assertEqual(plan['cells'][0]['requested_seconds'],8)
            exports=[j for j in plan['jobs'] if j['stage']=='export']
            self.assertEqual(len(exports),count)
            for job in exports:
                args=job['argv']
                self.assertNotIn('--fhss',args)
                for flag,value in (('--rate','.1'),('--added','1000'),('--sample-rate','128'),
                    ('--seconds','8'),('--block','128'),('--rf-hz','30000000'),('--nominal-if-hz','1725'),
                    ('--margin','1'),('--tap-delay-ms','.5'),('--tap-doppler-hz','.1'),('--cn0','-16')):
                    self.assertEqual(float(prepare.option(args,flag)),float(value))
                self.assertEqual(prepare.option(args,'--spread-preset'),'hf-best')
                self.assertEqual(prepare.option(args,'--selective-perturbation'),'each-65')
                self.assertEqual(prepare.option(args,'--basis'),'two-ray')
                self.assertEqual(prepare.option(args,'--base-oscillator'),'gpsdo-ocxo')
                self.assertEqual(prepare.option(args,'--shift-oscillator'),'gpsdo-ocxo')
            if suite=='validation':
                signal=[j for j in exports if j['control']=='signal']
                self.assertEqual([sum(j['bit']==b for j in signal) for b in (0,1)],[8,8])
                self.assertEqual({j['control'] for j in exports},{'signal','null','wrong-key'})
                self.assertEqual(prepare.option(signal[0]['argv'],'--seed'),'1051000')

    def test_declared_denominators_and_actual_gap_control(self):
        plan=prepare.build(parent())
        self.assertEqual(len(plan['cells']),10)
        self.assertEqual(len(plan['jobs']),150)
        self.assertEqual(plan['limits']['maximum_native_groups'],10000)
        self.assertEqual(plan['limits']['maximum_receiver_base_groups'],5000)
        self.assertEqual(sum(j['stage']=='export' for j in plan['jobs']),30)
        internal=next(j for j in plan['jobs'] if j['stage']=='export' and j['case']=='internal-wait3')
        self.assertIn('--burst-wait-tiles',internal['argv'])
        self.assertNotIn('--symbol-burst',internal['argv'])
        for job in plan['jobs']:runner.common.check_job(job)

    def test_scorer_identity_includes_imported_gaussian_state(self):
        self.assertIn('tools/triad_gaussian_state.py',prepare.SCORER)
        self.assertEqual(len(set(prepare.SCORER)),len(prepare.SCORER))

    def test_configure_does_not_release_unsupported_receiver_bank(self):
        original=next(j for j in prepare.build(parent())['jobs'] if j['stage']=='configure')
        for count,expected in ((5000,'completed'),(5001,'invalid_output'),(10001,'invalid_output')):
            job={**original,'expected_groups':count}
            native=dict(status='completed',native_configuration={'configuration':{
                'public_search':{'covers_initial_timing_prior':True}}})
            with mock.patch.object(runner.common,'execute',return_value=native):
                result=runner.execute(job,SimpleNamespace(),{}, {},{})
            self.assertEqual(result['status'],expected)
            if count==5001:self.assertIn('receiver base-group',result['reason'])
            if count==10001:self.assertIn('native group',result['reason'])

    def test_timing_grids_cover_endpoints_and_zero_with_declared_loss(self):
        plan=prepare.build(parent())
        for cell in plan['cells']:
            low,high,step=cell['timing_grid'];groups=cell['group_count']
            self.assertAlmostEqual(low+step*(groups-1),high)
            self.assertAlmostEqual(low+step*((groups-1)//2),0.)
            self.assertLessEqual(cell['timing_loss']['worst_nearest_grid_loss_db'],3.)
            self.assertFalse(cell['timing_loss']['nearest_grid_cell_contains_autocorrelation_zero'])
        self.assertEqual(plan['cells'][0]['group_count'],455)
        self.assertEqual(next(c for c in plan['cells'] if c['name']=='rate1200-added1')['group_count'],153)

    def test_paired_schedule_seeds_unchanged_and_null_is_control_only(self):
        plan=prepare.build(parent(),selected=['loss-middle'])
        exports=[j for j in plan['jobs'] if j['stage']=='export']
        zero=next(j for j in exports if j['bit']==0 and j['control']=='signal')
        null=next(j for j in exports if j['control']=='null')
        old=parent()['jobs'][0]['argv']
        for flag in prepare.SEED_FLAGS:
            self.assertEqual(prepare.option(zero['argv'],flag),prepare.option(old,flag))
            self.assertEqual(prepare.option(zero['argv'],flag),prepare.option(null['argv'],flag))
        self.assertEqual(prepare.option(zero['argv'],'--missed-dwells'),'11,12')

    def test_validation_is_balanced_and_domains_independent(self):
        plan=prepare.build(parent(),'validation',['loss-middle','internal-wait3'])
        exports=[j for j in plan['jobs'] if j['stage']=='export']
        self.assertEqual(len(exports),36)
        values=[]
        for cell in plan['cells']:
            signal=[j for j in exports if j['case']==cell['name'] and j['control']=='signal']
            self.assertEqual([sum(j['bit']==b for j in signal) for b in (0,1)],[8,8])
        for job in exports:
            seeds=tuple(int(prepare.option(job['argv'],flag)) for flag in prepare.SEED_FLAGS)
            self.assertEqual(len(set(seeds)),7);values.append(seeds)
        self.assertEqual(len(set(values)),len(values))

    def test_none_modulation_override_is_explicit_and_rf_zero_retained(self):
        plan=prepare.build(parent())
        exported={j['case']:j for j in plan['jobs'] if j['stage']=='export' and j['control']=='signal' and j['bit']==0}
        self.assertEqual(prepare.option(exported['none-am-off']['argv'],'--selective-modulation-rate-hz'),'0')
        self.assertEqual(prepare.option(exported['none-am65']['argv'],'--selective-modulation-rate-hz'),'0.1')
        self.assertEqual(prepare.option(exported['zero-shift-vlf-am65']['argv'],'--rf-hz'),'0')
        self.assertEqual(prepare.option(exported['shared-microwave-am65']['argv'],'--shift-oscillator'),'baseband-clock')

    def test_admission_recount_separates_wrong_and_null(self):
        self.assertEqual(runner.outcome([1,4],3,0,'signal'),dict(correct_admitted=0,wrong_admitted=1,missed=0))
        self.assertEqual(runner.outcome([1,2],3,0,'signal'),dict(correct_admitted=0,wrong_admitted=0,missed=1))
        self.assertEqual(runner.outcome([4,1],3,0,'null'),dict(false_admitted=1,not_admitted=0))
        with self.assertRaises(ValueError):runner.outcome([float('nan'),1],3,0,'signal')

    def test_frequency_bank_uses_public_bound_and_full_calendar_only(self):
        job=next(j for j in prepare.build(parent(),selected=['internal-wait3'])['jobs']
                 if j['stage']=='acquire' and j['method']=='global-frequency')
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory);workspace=output/'workspaces'/job['workspace'].split('/')[-1]
            workspace.mkdir(parents=True)
            (workspace/'manifest.json').write_text(json.dumps(dict(block_count=5000,sample_count=640000,sample_rate=4096,
                endpoint_truth_simulation_only={'frequency_hz':99999})))
            prior={job['depends_on'][1]:{'native_configuration':{'configuration':{'deterministic_oscillator_cfo_bound_hz':.003}}}}
            args=SimpleNamespace(python=Path('/python'),root=Path('/root'))
            command=runner.acquire_argv(job,args,{'OUT':str(output)},prior)
            index=command.index('--frequency-offset-grid')
            self.assertEqual(list(map(float,command[index+1:index+4])),[-.003,.003,1/(4*640000/4096)])
            self.assertEqual(prepare.option(command,'--reblock'),'5000')
            self.assertNotIn('99999',command)


if __name__=='__main__':unittest.main()
