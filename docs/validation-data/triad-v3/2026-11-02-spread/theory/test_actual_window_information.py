import json
from pathlib import Path
import tempfile
import unittest

import mpmath as mp
import numpy as np
from scipy.linalg import block_diag

import actual_window_information as subject


class InformationTests(unittest.TestCase):
    def test_spectral_formula_matches_raw_covariance_oracle(self):
        rng = np.random.default_rng(812)
        A = rng.normal(size=(3,2,4,2))+1j*rng.normal(size=(3,2,4,2))
        grams = A.conj().swapaxes(-1,-2)@A
        centers = np.array([0., .7, 2.])
        plan = subject.prepare_windowed_gaussian(grams, centers, doppler_width_hz=.3,
            window_blocks=2, stationary_power=(.5,.5))
        for candidate in range(2):
            expected = 0.
            for window in plan._windows:
                times = centers[window.first:window.stop]
                Q = np.kron(np.exp(-.5*(np.pi*.3*(times[:,None]-times[None,:]))**2), np.eye(2)*.5)
                design = block_diag(*A[window.first:window.stop,candidate])
                K = design@Q@design.conj().T/3.2
                sign, logdet = np.linalg.slogdet(np.eye(len(K))+K)
                self.assertAlmostEqual(sign.real, 1., places=12)
                expected += np.trace(K).real-logdet
            spectrum = np.concatenate([w.eigenvalues[candidate] for w in plan._windows])
            actual = subject.presence_kl(spectrum, 3.2, 0.)
            self.assertAlmostEqual(actual, expected, places=11)

    def test_small_signal_cancellation(self):
        with mp.workdps(90):
            for x in (0., 1e-30, 1e-12, 1e-5, .000999, .001, .2, 100.):
                exact = float(mp.mpf(x)-mp.log1p(mp.mpf(x)))
                actual = float(subject.gaussian_kl_terms(np.array([x]))[0])
                self.assertLessEqual(abs(actual-exact), max(1e-300, abs(exact)*3e-13))

    def test_inverse_and_zero_information(self):
        values = np.array([.1, 2., 17.])
        target = subject.presence_kl(values, .25, -7.)
        self.assertAlmostEqual(subject.necessary_cn0(values,.25,target)["necessary_cn0_db_hz"], -7., places=9)
        self.assertIsNone(subject.necessary_cn0(np.zeros(4),.25,10.)["necessary_cn0_db_hz"])
        self.assertIn("above", subject.necessary_cn0(values,.25,1e8,-100.,-90.)["status"])
        self.assertIn("below", subject.necessary_cn0(values,.25,1e-10,20.,30.)["status"])

    def fixture(self, directory):
        rng = np.random.default_rng(31)
        A = rng.normal(size=(3,4,4))+1j*rng.normal(size=(3,4,4))
        G = A.conj().swapaxes(-1,-2)@A
        G.astype('<c16').tofile(directory/'gram.c128')
        np.array([[0,4],[4,8],[8,12]],dtype='<u8').tofile(directory/'intervals.u64')
        np.array([4,4,4],dtype='<u8').tofile(directory/'valid_rows.u64')
        candidates = [{"bit": bit, "epoch_offset_seconds": 0, "arrival_seconds": 0,
            "frequency_hz": 0, "clock_ppm": 0, "frequency_drift_hz_per_second": 0,
            "tap_count": 2, "taps": [{"delay_seconds":0,"doppler_hz":0},{"delay_seconds":.1,"doppler_hz":.2}]} for bit in (0,1)]
        manifest = {"format":"triad-block-stats-v1","byte_order":"little","complex_type":"complex128",
            "real_type":"float64","integer_type":"uint64","layout":"block,row,column",
            "block_count":3,"dimension":4,"sample_count":12,"block_samples":4,"sample_rate":.25,
            "candidate_count":2,"column_mapping":[[0,1],[2,3]],"candidates":candidates,
            "gram_file":"gram.c128","gram_shape":[3,4,4],"intervals_file":"intervals.u64",
            "intervals_shape":[3,2],"valid_rows_file":"valid_rows.u64","valid_rows_shape":[3]}
        (directory/'manifest.json').write_text(json.dumps(manifest))
        return manifest

    def test_public_only_and_hidden_fields_do_not_affect_calculation(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            manifest = self.fixture(directory)
            first = subject.analyze(directory,[-60.,-20.],[2],.001)
            # Neither oracle nor noisy files exist on the first read. Bogus
            # unrelated fields/files cannot change the information calculation.
            manifest.update(noise_variance="forbidden", signal_bit=271, channel_seed=None)
            (directory/'manifest.json').write_text(json.dumps(manifest))
            for name in ('observation.c128','mean.c128','signal_energy.f64','received_energy.f64'):
                (directory/name).write_bytes(b'not a valid observation')
            second = subject.analyze(directory,[-60.,-20.],[2],.001)
            self.assertEqual(first['window_results'],second['window_results'])
            self.assertNotEqual(first['public_design']['public_source_sha256'], second['public_design']['public_source_sha256'])

    def test_bad_calendar_gram_and_memory_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.fixture(directory)
            with self.assertRaises(MemoryError):
                subject.load_public_design(directory,2,2,1024)
            np.array([5,4,4],dtype='<u8').tofile(directory/'valid_rows.u64')
            with self.assertRaisesRegex(ValueError,"calendar"):
                subject.load_public_design(directory,2,2,10**8)
            self.fixture(directory)
            G = np.fromfile(directory/'gram.c128',dtype='<c16').reshape(3,4,4)
            G[0,0,0] = -1000.
            G.tofile(directory/'gram.c128')
            with self.assertRaisesRegex(ValueError,"positive semidefinite"):
                subject.load_public_design(directory,2,2,10**8)


if __name__ == '__main__':
    unittest.main()
