#!/usr/bin/env python3
import sys
from pathlib import Path
import unittest
import tempfile
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from triad_live_level_analysis import repeated_metrics, repeat_input_identities


class RepeatMoments(unittest.TestCase):
    def test_repeat_identity_rejects_alias_and_copied_capture(self):
        with tempfile.TemporaryDirectory() as tmp:
            a=Path(tmp)/'a'; b=Path(tmp)/'b'
            a.write_bytes(b'physical');b.write_bytes(b'physical')
            with self.assertRaises(ValueError):repeat_input_identities([a,a])
            with self.assertRaises(ValueError):repeat_input_identities([a,b])
            b.write_bytes(b'new physical recording')
            self.assertEqual(len(set(repeat_input_identities([a,b]))),2)

    def test_repeatable_error_and_opposite_innovation_remain_distinct(self):
        p=np.ones(128,dtype=complex);bias=np.full(128,.2+.1j)
        innovation=np.tile([.1j,-.1j],64)
        a=np.asarray([p+bias+innovation,p+bias-innovation])
        r=repeated_metrics(a,p,np.ones(128,bool))
        self.assertAlmostEqual(r['repeat_difference_power_over_two'],.02)
        self.assertAlmostEqual(r['residual_cross_power'],.04)
        self.assertAlmostEqual(r['repeat_mean_residual_power'],.05)
        self.assertAlmostEqual(sum(r['residual_power'])/2,
            r['residual_cross_power']+r['repeat_difference_power_over_two'])

    def test_no_clipping_of_negative_cross_power_or_zero_source(self):
        a=np.asarray([np.ones(8),-np.ones(8)])
        r=repeated_metrics(a,np.zeros(8),np.ones(8,bool))
        self.assertEqual(r['residual_cross_power'],-1.)
        self.assertEqual(r['energy_ratio_db'],[None,None])

    def test_fixed_mask_and_shape_guards(self):
        with self.assertRaises(ValueError):repeated_metrics(np.zeros((1,8)),np.zeros(8),np.ones(8,bool))
        with self.assertRaises(ValueError):repeated_metrics(np.zeros((2,8)),np.zeros(8),np.zeros(8,bool))
        a=np.ones((2,8));a[:,0]=10
        r=repeated_metrics(a,np.ones(8),np.arange(8)>0)
        self.assertEqual(r['residual_power'],[0.,0.])


if __name__=='__main__':unittest.main()
