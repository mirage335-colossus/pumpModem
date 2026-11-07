"""Cheap public-coordinate and admission guards; no hardware is opened."""
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch, Mock

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from triad_continuous import parser, execute, carrier_phase, NativeStream
from triad_physical_run import (observation_geometry, validate_receipt, read_receipt,
    PreparedPhysical, make_basis, settings)


def receipt(frames=400):
    return dict(schema='triad-audio-stream-v1', mode='capture', complete=True,
        capture_format='adc-v1', capture_observation='pre-asrc-native-mono',
        requested_frames=frames, completed_frames=frames, clipped_samples=0,
        sample_rate_hz=48000, observed_native_frames=frames+50,
        discarded_post_asrc_frames=390,
        capture_stream_format=dict(logical_rate=48000, hardware_rate=48000,
            logical_channels=1, hardware_channels=2, selected_capture_channel=0),
        clock_discipline=dict(enabled=True, reports=2))


class PhysicalRunTests(unittest.TestCase):
    def test_physical_preparation_preserves_separate_linear_algebra_budget(self):
        cases = ((None, 3, 3),
                 (dict(threads=12, linear_algebra_threads=2), 12, 2),
                 (dict(threads=6, linear_algebra_threads=6), 6, 6))
        for allocation, requested, expected in cases:
            with self.subTest(allocation=allocation), tempfile.TemporaryDirectory() as td:
                profile = Path(td)/'profile.json'; profile.write_text('{}')
                a = parser().parse_args(['--mode', 'receive', '--report', str(Path(td)/'report'),
                    '--bank', 'unused-bank', '--physical-noise-profile', str(profile),
                    '--follow-system-clock', 'continuous', '--threads', str(requested)])
                if allocation is not None:
                    a.cpu_allocation = allocation
                source = Mock(descriptor=dict(helper_enabled=False))
                geometry = dict(sample_rate=48000, samples=100, block_samples=20,
                                required_bands_hz=[[100, 200]])
                with patch('triad_continuous.load_bank', return_value=({}, [[0,0,0,0]], [[0,0]])), \
                     patch('triad_continuous.NativeStream', return_value=source), \
                     patch('triad_physical_run.collect_binding', return_value=dict(hardware_rate_hz=48000)), \
                     patch('triad_physical_run.observation_geometry', return_value=geometry), \
                     patch('triad_physical_run.make_basis', return_value=(Mock(), Mock())), \
                     patch('triad_physical_admission.load_profile', return_value=Mock(variance_proxy=1.)), \
                     patch('triad_physical_admission.PhysicalReceiver'), \
                     patch('torch.set_num_threads') as set_threads:
                    prepared = PreparedPhysical(a, [], None)
                    set_threads.assert_called_once_with(expected)
                    prepared.close()

    def test_native_receipt_blocks_wrong_observation_and_incomplete_or_clipped(self):
        binding=dict(hardware_rate_hz=48000,hardware_channels=2,selected_channel=0)
        self.assertTrue(validate_receipt(receipt(), binding, 400))
        for key,value in [('complete',False),('completed_frames',399),('clipped_samples',1),
                ('capture_format','mapped-v1'),('capture_observation','post-asrc-logical-mono'),
                ('sample_rate_hz',6000),('discarded_post_asrc_frames',0),
                ('capture_discontinuity',True),('clock_discipline',dict(enabled=False,reports=2))]:
            with self.subTest(key=key),self.assertRaises(ValueError):
                validate_receipt(dict(receipt(),**{key:value}),binding,400)
        wrong=receipt();wrong['capture_stream_format']['selected_capture_channel']=1
        with self.assertRaisesRegex(ValueError,'format'):
            validate_receipt(wrong,binding,400)

    def test_receipt_parser_needs_one_actual_capture_receipt(self):
        with tempfile.TemporaryDirectory() as td:
            path=Path(td)/'stderr'
            path.write_text('ALSA informational text\n'+json.dumps(receipt())+'\n')
            self.assertEqual(read_receipt(path)['completed_frames'],400)
            path.write_text(json.dumps(receipt())+'\n'+json.dumps(receipt())+'\n')
            with self.assertRaisesRegex(ValueError,'exactly one'):
                read_receipt(path)

    def test_observation_geometry_keeps_native_rate_and_public_endpoint(self):
        a=SimpleNamespace(block_seconds=.25,clock_max_ppm=1000,fixed_gain=.001)
        d=dict(sample_rate_hz=6000,total_samples=60000,data_if_hz=1725)
        bank=dict(coordinates=[[0,1.2,0,0]],taps=[[.02,0]],
            projection=dict(required_bands_hz=[[-150,150]]))
        g=observation_geometry(a,d,bank,48000)
        self.assertEqual(g['block_samples'],12000)
        self.assertEqual(g['required_bands_hz'],[[1575,1875]])
        self.assertGreater(g['capture_seconds'],11.22)
        self.assertLess(g['capture_seconds'],11.25)
        bank['projection']['excluded_bands_hz']=[[-100,100]]
        with self.assertRaisesRegex(ValueError,'exclusions'):
            observation_geometry(a,d,bank,48000)

    def test_adc_grid_cannot_accept_asrc_offsets(self):
        calls=[]
        obj=object.__new__(PreparedPhysical)
        obj.next_sample=0;obj.sample_rate=48000
        obj.receiver=SimpleNamespace(push=lambda first,pcm:calls.append((first,pcm.copy())))
        obj.push(0,np.zeros(5),0,np.arange(5,dtype=float),48000)
        self.assertEqual(obj.next_sample,5)
        with self.assertRaisesRegex(ValueError,'original native'):
            obj.push(5,np.zeros(5),5,np.arange(5)*1.0002,48000)
        self.assertEqual(len(calls),1)

    def test_public_full_audio_basis_sign_and_rescaled_clock_coordinates(self):
        class Source:
            descriptor=dict(sample_rate_hz=6000,data_if_hz=1725.,data_offset_samples=6000)
            def basis_mapped(self,origin,offsets,rows,center):
                self.seen=(origin,offsets.copy(),rows,center)
                return np.full((len(rows),len(offsets)),1+2j,dtype=complex)
        source=Source()
        basis,_=make_basis(source,[[0,.3,1.7,50]],[[.01,.2]],48000,template_backend='direct')
        z=basis(384007,384039)
        np.testing.assert_allclose(z[1,0,0],(1+2j)*np.exp(1j*carrier_phase(384007,32,1725,48000)),atol=1e-14)
        self.assertTrue(np.all(z[0]==0))
        origin,offsets,rows,center=source.seen
        self.assertEqual(origin,48000)
        np.testing.assert_array_equal(offsets,(7+np.arange(32))/8)
        self.assertEqual(rows[0][:3],(0,0,1.3))
        self.assertAlmostEqual(rows[0][3],1.7-1725*50e-6)
        self.assertAlmostEqual(rows[0][4],50)

    @unittest.skipUnless((ROOT/'build/dev-cli/triad_helper_export').is_file(),'isolated native exporter unavailable')
    def test_full_audio_basis_matches_independent_native_emission_at_adc_times(self):
        args=['--case','hf','--sample-rate','6000','--rate','36','--added-symbols','10',
            '--seconds','8','--key-seed','73005','--epoch','1793850000',
            '--spread-preset','none','--selective-perturbation','off',
            '--base-oscillator','gpsdo-ocxo','--shift-oscillator','gpsdo-ocxo',
            '--oscillator-effects','off','--arrival-seconds','0','--helper','off']
        source=NativeStream(ROOT/'build/dev-cli/triad_helper_export',args,kind='emitted')
        try:
            first=int(source.descriptor['data_offset_samples'])+3000
            while source.next_sample<first:
                source.next(min(16384,first-source.next_sample))
            emitted,_=source.next(1000)
            basis,_=make_basis(source,[[0,0,0,0]],[[0,0]],48000,template_backend='direct')
            alternatives=basis(first*8,(first+1000)*8)[1:,0,0,::8]
            got=alternatives[0]
            want=emitted*np.exp(1j*carrier_phase(first,1000,source.descriptor['data_if_hz'],6000))
            # Emitted and basis IFs have different fixed time origins. The
            # initial naive raw comparison was 1.94474; a SINGLE shared unit
            # complex channel gain (already integrated by the detector) leaves
            # 1.4e-12 residual. Do not allow separate per-carrier/time fitting.
            gain=np.vdot(got,want)/np.vdot(got,got)
            self.assertAlmostEqual(abs(gain),1.,places=10)
            self.assertLess(np.linalg.norm(gain*got-want)/np.linalg.norm(want),1e-9)
            wrong=alternatives[1]
            wrong_gain=np.vdot(wrong,want)/np.vdot(wrong,wrong)
            self.assertGreater(np.linalg.norm(wrong_gain*wrong-want)/np.linalg.norm(want),.9)
        finally:
            source.close()

    def test_profile_route_precedes_diagnostic_noise_override_requirement(self):
        with tempfile.TemporaryDirectory() as td:
            with patch('triad_physical_run.execute',return_value={'test':'routed'}) as call:
                result=execute(['--mode','receive','--bank',str(Path(td)/'bank'),
                    '--report',str(Path(td)/'report'),'--physical-noise-profile',str(Path(td)/'profile')])
            self.assertEqual(result,{'test':'routed'})
            self.assertEqual(call.call_args.args[0].follow_system_clock,'continuous')
        a=parser().parse_args(['--report','/tmp/unused'])
        self.assertIn('coherences',settings(a))


if __name__=='__main__':
    unittest.main()
