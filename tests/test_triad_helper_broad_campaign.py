#!/usr/bin/env python3
"""Cheap frozen broad-study orchestration/mask checks; no actual campaign."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import triad_helper_broad_campaign as broad


def manifest():
    return dict(sample_rate_hz=1000, format=dict(dwell_seconds=.4, bit_seconds=4., tile_seconds=2., tile_count=2),
        calendar=dict(capture_samples=45000, capture_lead_seconds=.2, data_calendar_start_seconds=38),
        observation=dict(file='observation.c128', shape=[45000]), validity=dict(file='validity.u8', shape=[45000]),
        channel=dict(rf_hz=30e6, nominal_conversion_if_hz=1725.), templates=dict(file='templates.c128', enabled=True),
        helper_enabled=True, cost=dict(wall_seconds=.01))


def case(name='first', **extra):
    return dict(id=name, export_args=['--case', 'eme'], seconds=4, seed_base=100,
        calibration_count=2, signal_count=2, null_count=1, lag_steps=[1,4], **extra)


class BroadCampaignTests(unittest.TestCase):
    def test_protocol_rejects_unsafe_duplicate_and_managed_inputs(self):
        self.assertEqual(len(broad.protocol_cases([case()])['cohorts']), 1)
        for invalid in ([case('../bad')], [case(),case()], [dict(case(), lag_steps=[1,1])],
                        [dict(case(), arms=[])], [dict(case(), arms=['baseline','baseline'])],
                        [dict(case(), arms=['other'])], [dict(case(), arms=['baseline'])],
                        [dict(case(), arms=['assisted','baseline'])],
                        [dict(case(), export_args=['--output','elsewhere'])],
                        [dict(case(), templates_from='not-yet-created')], [dict(case(), erasure='guess')]):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                broad.protocol_cases(invalid)

    def test_all_public_erasure_recipes_are_fixed_and_preserve_helper_lead(self):
        m = manifest()
        for recipe in broad.ERASURES:
            a, info = broad.erasure_mask(m, recipe, 112)
            b, _ = broad.erasure_mask(m, recipe, 112)
            np.testing.assert_array_equal(a,b)
            self.assertTrue(np.all(a[:38200] == 1))
            self.assertEqual(int((a == 0).sum()), info['missing_rows'])
            expected = {'none':0,'every-dwell-10ms':100,'beginning-dwell':400,
                        'end-dwell':400,'boundary-two':800,'random-three':1200}[recipe]
            self.assertEqual(info['missing_rows'], expected)

    def test_burst_masks_straddle_gap_and_leave_backlog_time_valid(self):
        m=manifest();m['format'].update(tile_seconds=2.,tile_count=2,bit_seconds=10.)
        m['calendar']['capture_samples']=52000
        for recipe in broad.ERASURES:
            valid,info=broad.erasure_mask(m,recipe,112)
            self.assertEqual(info['inferred_burst_gap_seconds'],6.)
            self.assertTrue(np.all(valid[40200:46200]==1))
            if recipe=='boundary-two':
                np.testing.assert_allclose(info['intervals_seconds'],[(39.8,40.2),(46.2,46.6)],rtol=0,atol=1e-12)
                self.assertEqual(info['missing_rows'],800)
            if recipe=='every-dwell-10ms':self.assertEqual(info['missing_rows'],100)

    def test_mask_application_preserves_manifest_and_receipts(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary);m=manifest();(folder/'manifest.json').write_text(json.dumps(m))
            np.ones(45000,np.complex128).tofile(folder/'observation.c128')
            np.ones(45000,np.uint8).tofile(folder/'validity.u8')
            mask, info=broad.erasure_mask(m,'beginning-dwell')
            receipt=broad.apply_erasure(folder,m,mask,info)
            raw=np.fromfile(folder/'observation.c128',np.complex128)
            np.testing.assert_array_equal(raw[mask==0],0);np.testing.assert_array_equal(raw[mask==1],1)
            self.assertNotEqual(receipt['native']['observation_sha256'],receipt['observation_sha256'])
            self.assertEqual(json.loads((folder/'manifest.json').read_text()),m)
            with self.assertRaises(ValueError):broad.apply_erasure(folder,m,mask,info)

    def test_generate_once_share_prepare_separate_gates_and_continue_failed_cohort(self):
        exports=[];prepares=[];scores=[];evaluations=[]
        class FakeComparison:
            def __init__(self,fixture,templates,**options):
                self.step=options['lag_step'];self.procedure_id=f'policy-{self.step}'
                self.template_hash='template';self.sources={};self.preparation_seconds=.02
                self.options=options
            def prepare_capture(self,folder):
                prepares.append(folder)
                return dict(folder=folder,shared_preparation_seconds=.1)
            def score_capture(self,prepared,*,include_baseline=True):
                folder=prepared['folder'];scores.append((folder,self.step))
                m=json.loads((folder/'manifest.json').read_text())
                source=np.fromfile(folder/'observation.c128',np.complex128)
                bit=max(0,int(source[0].real)-1);present=bool(source[0])
                outcome=dict(receiver_id=self.procedure_id,bit=bit,working_presence=10. if present else 1.,
                    bit_log_likelihoods=[10.,1.],opposite_bit_margin=9.,best_epoch_offset_seconds=0,
                    best_group_public_parameters={'frequency_hz':0.},timing={'total_score_prepared_seconds':.2},
                    arithmetic={},resources={},geometry={'epoch_offsets':[0],'lag_samples':[0,1],
                        'frequency_offsets_hz':[0.],'samples':100,'effective_group_count':2})
                executed_arms=['baseline','assisted'] if include_baseline else ['assisted']
                return dict(schema='test',procedure_id=self.procedure_id,executed_arms=executed_arms,
                    input_sha256=broad.digest(folder/'observation.c128'),
                    template_sha256='template',source_hashes={},assistance=dict(status='narrowed',selected_frequencies=[0.],
                        expanded_intervals_hz=[[-1.,1.]]),timing=dict(shared_preparation_seconds=.1,score_only_seconds=.2,
                        command_score_seconds=.3),limits=[],
                    baseline=outcome if include_baseline else None,assisted=outcome.copy())
        def export(command,**kwargs):
            exports.append(command)
            if '--bad' in command:raise subprocess.CalledProcessError(1,command)
            folder=Path(command[command.index('--output')+1]);folder.mkdir();m=manifest()
            (folder/'manifest.json').write_text(json.dumps(m))
            raw=np.zeros(45000,np.complex128)
            if command[command.index('--data')+1]=='on':raw[0]=1+int(command[command.index('--bit')+1])
            raw.tofile(folder/'observation.c128');np.ones(45000,np.uint8).tofile(folder/'validity.u8')
            if '--templates' in command:(folder/'templates.c128').write_bytes(b'template')
            (folder/'evaluator-truth.json').write_text(json.dumps(dict(source_bit=int(command[command.index('--bit')+1]),
                arrival_seconds=0.,tx=dict(audio_ppm=0.,rf_ppm=0.),rx=dict(audio_ppm=0.,rf_ppm=0.),
                source_energy={},channel_report={})))
            return subprocess.CompletedProcess(command,0)
        original=broad.evaluator_metrics
        def evaluated(truth,m,result):
            # Every trial is scored for both lag policies before evaluator access.
            self.assertEqual(len(scores)%2,0);evaluations.append(result['procedure_id'])
            return original(truth,m,result)
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);exe=root/'exporter';exe.write_text('frozen');protocol=root/'input.json'
            cases=[dict(case('bad'),export_args=['--bad']),case('good'),
                   case('reuse',templates_from='good'),case('assisted',arms=['assisted'])]
            protocol.write_text(json.dumps(dict(alpha=.5,cohorts=cases)));output=root/'out'
            with mock.patch.object(broad,'public_clock_grids',return_value=((0.,),(1.,))), \
                 mock.patch.object(broad,'evaluator_metrics',side_effect=evaluated):
                status=broad.main(['--exporter',str(exe),'--protocol',str(protocol),'--output',str(output)],
                    comparison_factory=FakeComparison,export_runner=export)
            self.assertEqual(status,2);summary=json.loads((output/'summary.json').read_text())
            self.assertEqual(summary['cohorts']['bad']['status'],'failed')
            self.assertEqual(summary['cohorts']['good']['status'],'completed')
            self.assertEqual(summary['cohorts']['reuse']['status'],'completed')
            self.assertEqual(len(exports),16);self.assertEqual(len(prepares),15);self.assertEqual(len(scores),30)
            self.assertEqual(sum('--templates' in cmd for cmd in exports),3) # Each independent template bank starts explicitly.
            self.assertFalse(any(output.rglob('*.c128')));self.assertFalse(any(output.rglob('*.u8')))
            for lag in (1,4):
                gate=json.loads((output/'good'/f'gate-lag-{lag}.json').read_text())
                self.assertEqual(gate['procedure_id'],f'policy-{lag}')
                self.assertEqual(summary['cohorts']['good']['outcomes'][str(lag)]['baseline']['counts']['correct_admitted'],2)
            self.assertAlmostEqual(summary['cohorts']['good']['timing']['shared_capture_preparation_seconds'],.5)
            self.assertAlmostEqual(summary['cohorts']['good']['timing']['all_lag_score_only_seconds'],2.)
            self.assertEqual(len((output/'good'/'trials.jsonl').read_text().splitlines()),5)
            assisted_summary=summary['cohorts']['assisted']
            self.assertEqual(assisted_summary['executed_arms'],['assisted'])
            self.assertEqual(set(assisted_summary['outcomes']['1']),{'assisted'})
            self.assertEqual(assisted_summary['outcomes']['1']['assisted']['counts']['correct_admitted'],2)
            assisted_protocol=json.loads((output/'assisted'/'protocol.json').read_text())
            self.assertEqual(assisted_protocol['executed_arms'],['assisted'])
            assisted_gate=json.loads((output/'assisted'/'gate-lag-1.json').read_text())
            self.assertEqual(assisted_gate['arms'],['assisted'])
            self.assertNotIn('executed_arms',summary['cohorts']['good'])
            assisted_rows=[json.loads(line) for line in (output/'assisted'/'trials.jsonl').read_text().splitlines()]
            self.assertTrue(all('baseline' not in row['variants']['1'] for row in assisted_rows))
            self.assertTrue(all(set(row['variants']['1'].get('decisions',{})) <= {'assisted'}
                                for row in assisted_rows))
            with self.assertRaises(SystemExit):
                broad.main(['--exporter',str(exe),'--protocol',str(protocol),'--output',str(output)])


if __name__=='__main__':unittest.main()
