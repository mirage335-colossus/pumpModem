#!/usr/bin/env python3
"""Small actual-sample/covariance/calendar checks; no historical campaign."""
import math
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
import torch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from triad_multiday_block_mc import conditional_trials,validate_sampled,_calendar,_block_statistics
import triad_multiday_block_mc as block_mc


class Source:
    def __init__(self,n=67):
        rng=np.random.default_rng(71231)
        self.samples=n;self.sample_rate=.5;self.template_ppm=0.
        self.basis=tuple((rng.normal(size=n)+1j*rng.normal(size=n))*.2 for _ in range(2))
        phase=np.exp(1j*np.linspace(.1,.6,n))
        self.signals=tuple(x*phase for x in self.basis)
    def basis_block(self,lo,hi):return np.stack([x[lo:hi] for x in self.basis]).astype(np.complex128)
    def signal_block(self,lo,hi):return np.stack([x[lo:hi] for x in self.signals]).astype(np.complex128)


class BlockMonteCarloTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1);torch.backends.cuda.matmul.allow_tf32=False
        torch.set_float32_matmul_precision('highest')

    def test_merged_calendar_and_joint_correlations(self):
        s=Source();c=_calendar(s,16,2.)
        self.assertEqual(c.block_count,4);self.assertEqual(c._block_bounds(3),(48,67))
        g,l,m,repair=_block_statistics(s,48,67,2.)
        a=s.basis_block(48,67);truth=s.signal_block(48,67)
        np.testing.assert_allclose(g,a.conj()@a.T/2,atol=1e-14)
        np.testing.assert_allclose(l@l.conj().T,g,atol=1e-14)
        np.testing.assert_allclose(m,(a.conj()@truth.T/2).T,atol=1e-14)
        self.assertLess(repair,1e-13)

    def test_actual_sampled_noise_matches_maintained_filter_all_cases(self):
        s=Source()
        for bit in (0,1,None):
            r=validate_sampled(s,cn0_db_hz=-6,seed=5432+(0 if bit is None else bit),true_bit=bit,
                block_samples=16,input_chunk_samples=16,coherences=(30.,math.inf),phase_states=(32,64),phase_diffusion=.01)
            self.assertLess(r['maximum_absolute_error'],1e-12)
            self.assertEqual(np.asarray(r['phase_bit_scores']).shape,(2,2))

    def test_conditional_trials_are_paired_deterministic_and_bounded(self):
        s=Source();kwargs=dict(cn0_db_hz=-6,block_samples=16,coherences=(30.,math.inf),
            trials=64,batch_trials=32,seed=97831,phase_states=(32,64),phase_diffusion=.01)
        r,a=conditional_trials(s,**kwargs);r2,b=conditional_trials(s,**kwargs)
        np.testing.assert_array_equal(a,b)
        self.assertEqual(a.shape,(4,3,64,2));self.assertEqual(r['terminal_block_rows'],19)
        self.assertEqual(len(r['batches']),2)
        for values in r['reports'].values():
            for case in ('bit0','bit1'):
                n=values[case]['counts'];self.assertEqual(n['correct_admissions']+n['wrong_admissions']+n['misses'],64)
        self.assertEqual(r['reports']['unit-phase-32']['null']['trials'],64)

    def test_invalid_public_parameters_reject(self):
        s=Source()
        for change in (dict(trials=0),dict(batch_trials=0),dict(block_samples=0),dict(coherences=(0.,)),
                       dict(phase_states=(32,32)),dict(phase_diffusion=-1),dict(precision='fp16')):
            with self.assertRaises((ValueError,RuntimeError)):conditional_trials(s,**change)
        with self.assertRaises(MemoryError):conditional_trials(s,memory_limit_bytes=1)

    def test_source_changes_invalidate_cli_result_without_hiding_output(self):
        source=Source();source.identity=lambda:{'fixture':'unchanged'}
        path=str(Path(block_mc.__file__).resolve())
        with tempfile.TemporaryDirectory() as folder:
            output=Path(folder)/'result.json'
            with patch.object(block_mc,'NativeBlockInputs',return_value=source), \
                 patch.object(block_mc,'_source_identities',side_effect=[{path:'old'},{path:'new'}]):
                with self.assertRaisesRegex(RuntimeError,'changed during qualification'):
                    block_mc.main(['--samples','unused','--output',str(output),'--trials','1'])
            report=json.loads(output.read_text())
            self.assertFalse(report['qualification_valid'])
            self.assertEqual(report['changed_source_files'],[path])
            self.assertEqual(report['changed_input_files'],[])


if __name__=='__main__':unittest.main()
