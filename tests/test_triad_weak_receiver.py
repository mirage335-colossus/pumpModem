import copy
from pathlib import Path
import sys,unittest
from unittest import mock
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from test_triad_dynamic_acquire import fixture
from triad_weak_receiver import WeakReceiver


class WeakContracts(unittest.TestCase):
    def inputs(self):
        m,t=fixture(True);m.update(schema='test',public_epoch_seconds=1000,key_seed=1)
        m['format']['bit_seconds']=1.
        m['channel'].update(deterministic_cfo_bound_hz=5.,spread_margin=1.,sample_clock_pair_ppm=10.)
        return m,t

    def test_auto_mode_uses_public_renewal_and_preserves_manifest(self):
        m,t=self.inputs();old=copy.deepcopy(m)
        with mock.patch('triad_weak_receiver.FFTScreen')as whole,mock.patch('triad_weak_receiver.SegmentedFFTScreen')as segmented:
            whole.return_value.procedure_id='whole';segmented.return_value.procedure_id='segmented'
            a=WeakReceiver(m,t,[0]);self.assertEqual(a.screen_mode,'whole');self.assertEqual(m,old)
            m['channel']['random_doppler_width_hz']=.5
            b=WeakReceiver(m,t,[0]);self.assertEqual(b.screen_mode,'segmented')
            self.assertNotEqual(a.procedure_id,b.procedure_id)

    def pipeline(self, h0=0.):
        m,t=self.inputs()
        proposals=[dict(epoch=0,arrival=.00041,frequency=.1234,clock_ppm=0.,bit=b)for b in (0,1)]
        screen=mock.Mock(procedure_id='coarse');screen.score.return_value=dict(candidates=proposals)
        dynamic=mock.Mock(procedure_id='dynamic');dynamic.work.return_value={}
        dynamic.score.return_value=dict(bit_scores=np.array([[120.,-5.]]),
            hypothesis_log_evidence=np.array([[[h0,120.,-5.]]]))
        helper=dict(frequency=dict(status='narrowed',expanded_intervals_hz=[[0.,1.]]))
        return m,t,screen,dynamic,helper

    def test_grid_mass_bit_order_and_physical_qualification(self):
        m,t,screen,dynamic,helper=self.pipeline()
        with mock.patch('triad_weak_receiver.FFTScreen',return_value=screen),mock.patch('triad_weak_receiver.ScanJointReceiver',return_value=dynamic),mock.patch('triad_weak_receiver.estimate_weak_helper',return_value=helper):
            rx=WeakReceiver(m,t,[0]);r=rx.score(np.zeros(m['calendar']['capture_samples'],complex))
        self.assertEqual(r['bit'],0);self.assertEqual(r['margin'],125.)
        rx.grid.validate_selected(r['refinement']['candidates'])
        self.assertTrue(r['operational']['passes_conditional_bound'])
        self.assertFalse(r['operational']['physical_null_qualified'])
        self.assertLess(r['operational']['selected_prior_mass'],1.)

    def test_nonzero_helper_null_cannot_borrow_noise_evalue(self):
        m,t,screen,dynamic,helper=self.pipeline(h0=1.)
        with mock.patch('triad_weak_receiver.FFTScreen',return_value=screen),mock.patch('triad_weak_receiver.ScanJointReceiver',return_value=dynamic),mock.patch('triad_weak_receiver.estimate_weak_helper',return_value=helper):
            with self.assertRaisesRegex(RuntimeError,'zero-signal'):WeakReceiver(m,t,[0]).score(np.zeros(m['calendar']['capture_samples'],complex))

    def test_helper_failure_does_not_run_data(self):
        m,t,screen,dynamic,helper=self.pipeline()
        with mock.patch('triad_weak_receiver.FFTScreen',return_value=screen),mock.patch('triad_weak_receiver.estimate_weak_helper',return_value=dict(frequency=None)):
            r=WeakReceiver(m,t,[0]).score(np.zeros(m['calendar']['capture_samples'],complex))
        self.assertEqual(r['reason'],'helper_acquisition_unresolved');screen.score.assert_not_called()

    def test_refinement_work_limit_is_explicit(self):
        m,t,screen,dynamic,helper=self.pipeline()
        with mock.patch('triad_weak_receiver.FFTScreen',return_value=screen),mock.patch('triad_weak_receiver.estimate_weak_helper',return_value=helper),mock.patch('triad_weak_receiver.refinement_coordinates',side_effect=ValueError('sparse refinement exceeds configured work budget')):
            r=WeakReceiver(m,t,[0]).score(np.zeros(m['calendar']['capture_samples'],complex))
        self.assertEqual(r['reason'],'refinement_work_limit')

if __name__=='__main__':unittest.main()
