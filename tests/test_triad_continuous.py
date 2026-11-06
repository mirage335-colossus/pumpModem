"""Cheap stream adapter guards; waveform/likelihood checks live separately."""
import io
import math
from pathlib import Path
import sys
import unittest
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_continuous import (carrier_phase, endpoint_decision, read_exact,
                              receiver_noise_variance, validate_stream_identity)


class ContinuousTests(unittest.TestCase):
    def test_live_stream_requires_matching_explicit_pattern_identity(self):
        for revision in ('legacy-v1', 'independent-apsk-v2'):
            identity = dict(pattern_revision=revision, added_amplitude_depth=.25)
            self.assertEqual(validate_stream_identity({'format': identity}, identity), identity)
            for changed in ({**identity, 'pattern_revision': 'future-unknown'},
                            {**identity, 'pattern_revision': []},
                            {**identity, 'pattern_revision': 'legacy-v1' if revision != 'legacy-v1' else 'independent-apsk-v2'},
                            {**identity, 'added_amplitude_depth': .5},
                            {**identity, 'added_amplitude_depth': math.nan},
                            {**identity, 'added_amplitude_depth': True},
                            {**identity, 'added_amplitude_depth': 0}, {}):
                with self.subTest(revision=revision, changed=changed):
                    with self.assertRaises(ValueError):
                        validate_stream_identity({'format': identity}, changed)
        with self.assertRaises(ValueError): validate_stream_identity({}, {})

    def test_declared_variance_cannot_underestimate_known_simulation_noise(self):
        self.assertEqual(receiver_noise_variance(None, 3., simulated=True), 3.)
        self.assertEqual(receiver_noise_variance(4., 3., simulated=True), 4.)
        for variance in (2., 0., -1., math.nan, math.inf):
            with self.assertRaises(ValueError):
                receiver_noise_variance(variance, 3., simulated=True)
        self.assertEqual(receiver_noise_variance(1., 0., simulated=False), 1.)

    def test_endpoint_and_null_scope_cannot_be_overridden_by_large_scores(self):
        for complete, simulated, helper in ((False,True,False),(True,False,False),(True,True,True)):
            result=endpoint_decision([100,0],complete=complete,simulated=simulated,
                helper_enabled=helper,log_threshold=22.)
            self.assertFalse(result['admitted'])
        self.assertTrue(endpoint_decision([100,0],complete=True,simulated=True,
            helper_enabled=False,log_threshold=22.)['admitted'])

    def test_large_integer_phase_origin_and_arbitrary_chunk_boundaries(self):
        start=2**54+137
        complete=carrier_phase(start,100,1725.125,6000)
        pieces=np.concatenate([carrier_phase(start,37,1725.125,6000),
            carrier_phase(start+37,63,1725.125,6000)])
        np.testing.assert_allclose(np.exp(1j*complete),np.exp(1j*pieces),atol=4e-14,rtol=0)
        n,d=1725.125.as_integer_ratio()
        self.assertAlmostEqual(complete[0]/(2*math.pi), ((start*n)%(d*6000))/(d*6000))

    def test_truncated_binary_payload_is_not_silently_completed(self):
        self.assertEqual(read_exact(io.BytesIO(b'1234'),4),b'1234')
        with self.assertRaises(EOFError):read_exact(io.BytesIO(b'123'),4)


if __name__=='__main__':unittest.main()
