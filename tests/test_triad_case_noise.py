"""Small algebraic oracles for finite projected proper-complex noise."""
from __future__ import annotations

import copy
import hashlib
from pathlib import Path
import sys
import unittest

import numpy as np
from scipy.signal import upfirdn

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_case_noise import ProjectedNoise


def receipt(taps, *, raw=13, down=2, first=1, count=None, dtype=np.float64):
    h = np.asarray(taps, dtype=dtype)
    count_all = (raw+len(h)-2)//down+1
    if count is None:
        count = count_all-first
    tag = '<f4' if h.dtype == np.dtype(np.float32) else '<f8'
    h = h.astype(tag)
    return dict(schema='triad-complex-fir-projection-v1', raw_samples=raw,
        input_sample_rate_hz=800*down, output_sample_rate_hz=800,
        downsample=down, fir_taps_count=len(h), fir_taps_dtype=h.dtype.str,
        fir_taps_sha256=hashlib.sha256(h.tobytes()).hexdigest(),
        template_source_samples=0, template_source_identity_sha256='0'*64,
        decimation_phase_raw_samples=0, cropped_output_start=first,
        cropped_output_count=count, first_cropped_convolution_raw_sample=first*down,
        complete_filtered_output_count=count_all,
        capture_lead_seconds=0., finite_input_boundary='Exactly zero outside raw sample indices0..raw_samples-1; complete causal FIR tail retained before crop.', noise_scope='colored'), h


def dense_matrix(h, raw, down, first, count, start=0, stop=None):
    if stop is None:
        stop = raw
    f = np.zeros((count, stop-start), np.complex128)
    for j in range(count):
        p = (first+j)*down
        for r in range(start, stop):
            k = p-r
            if 0 <= k < len(h):
                f[j, r-start] = h[k]
    return f


class ProjectedNoiseTests(unittest.TestCase):
    def test_lower_band_and_factor_match_dense_finite_edges_and_crop(self):
        for h, raw, down, first, prefix in (
                ([.2, -.1, .5, .3, .2, -.05], 13, 2, 1, 0),
                ([.1, .4, -.2, .3, .5, .1, -.05], 17, 3, 2, 4),
                ([.7, .2], 10, 1, 0, 4),
                ([.15, .5, -.2, .3, .11, -.04, .01], 31, 2, 0, 16)):
            rec, taps = receipt(h, raw=raw, down=down, first=first)
            sampler = ProjectedNoise(rec, taps, helper_prefix_samples=prefix)
            frest = dense_matrix(taps, raw, down, first, rec['cropped_output_count'], prefix)
            covariance = frest @ frest.conj().T
            lower_band = sampler._rest_covariance()
            band = np.zeros_like(covariance)
            for lag in range(sampler.bandwidth+1):
                count = len(covariance)-lag
                band[np.arange(lag, len(covariance)), np.arange(count)] = lower_band[lag, :count]
                band[np.arange(count), np.arange(lag, len(covariance))] = lower_band[lag, :count].conj()
            np.testing.assert_allclose(band, covariance, rtol=2e-13, atol=2e-14)
            if sampler._factor is not None:
                factor = np.zeros_like(covariance)
                start, stop = sampler._active_start, sampler._active_stop
                for lag in range(sampler.bandwidth+1):
                    count = stop-start-lag
                    if count > 0:
                        i = np.arange(start+lag, start+lag+count)
                        j = np.arange(start, start+count)
                        factor[i, j] = sampler._factor[lag, :count]
                np.testing.assert_allclose(factor @ factor.conj().T, covariance,
                                           rtol=3e-12, atol=3e-13)
                if prefix and first == 0:
                    self.assertGreater(sampler._active_start, 0)

    def test_draw_is_proper_and_helper_prefix_is_the_same_convolution(self):
        rec, taps = receipt([.2, .7, -.1, .3, .05], raw=12, down=2, first=0, count=5)
        sampler = ProjectedNoise(rec, taps, helper_prefix_samples=12)
        result = sampler.draw(47)
        rng = np.random.default_rng(47)
        expected_prefix = (rng.standard_normal(12)+1j*rng.standard_normal(12))/np.sqrt(2)
        full = upfirdn(taps, result['helper_raw_noise'], down=2)
        expected = np.zeros(5, complex); expected[:min(len(full), 5)] = full[:5]
        np.testing.assert_allclose(result['projected_noise'], expected, rtol=0, atol=0)
        np.testing.assert_array_equal(result['helper_raw_noise'], expected_prefix)
        self.assertEqual(sampler.receipt, rec)
        self.assertEqual(sampler.covariance_bandwidth, 2)
        self.assertLessEqual(sampler.owned_workspace_bytes, sampler.memory_limit_bytes)

    def test_seeded_draw_and_exact_joint_covariance_match_direct_white_raw_noise(self):
        rec, taps = receipt([.4, -.2, .5, .1], raw=9, down=2, first=1, count=4)
        sampler = ProjectedNoise(rec, taps, helper_prefix_samples=3)
        seed = 61
        result = sampler.draw(seed)
        rng = np.random.default_rng(seed)
        prefix = (rng.standard_normal(3)+1j*rng.standard_normal(3))/np.sqrt(2)
        active_count = sampler._active_stop-sampler._active_start
        z = (rng.standard_normal(active_count)+1j*rng.standard_normal(active_count))/np.sqrt(2)
        prefix_matrix = dense_matrix(taps, 9, 2, 1, 4, 0, 3)
        rest_matrix = dense_matrix(taps, 9, 2, 1, 4, 3, 9)
        lower = np.zeros((active_count, active_count), complex)
        for lag in range(min(sampler.bandwidth, active_count-1)+1):
            count = active_count-lag
            lower[np.arange(lag, lag+count), np.arange(count)] = sampler._factor[lag, :count]
        expected_output = prefix_matrix @ prefix
        expected_output[sampler._active_start:sampler._active_stop] += lower @ z
        np.testing.assert_array_equal(result['helper_raw_noise'], prefix)
        np.testing.assert_allclose(result['projected_noise'], expected_output, rtol=2e-14, atol=2e-14)

        # The entire [Helper raw prefix, projected output] block is an exact
        # linear map of [prefix, remaining raw noise]. Its covariance includes
        # both the prefix/output cross block and the independent-rest block.
        direct = dense_matrix(taps, 9, 2, 1, 4)
        transform = np.block([[np.eye(3), np.zeros((3, 6))],
                              [prefix_matrix, rest_matrix]])
        embedded_lower = np.zeros((4, active_count), complex)
        embedded_lower[sampler._active_start:sampler._active_stop] = lower
        factor_transform = np.block([[np.eye(3), np.zeros((3, active_count))],
                                     [prefix_matrix, embedded_lower]])
        expected_covariance = transform @ transform.conj().T
        np.testing.assert_allclose(expected_covariance[:3, 3:], prefix_matrix.conj().T,
                                   rtol=0, atol=0)
        np.testing.assert_allclose(expected_covariance[3:, 3:], direct @ direct.conj().T,
                                   rtol=2e-14, atol=2e-14)
        np.testing.assert_allclose(factor_transform @ factor_transform.conj().T,
                                   expected_covariance, rtol=3e-12, atol=3e-13)

    def test_factor_work_allowance_changes_admission_not_the_noise(self):
        rec, taps = receipt([.4, -.2, .5, .1], raw=9, down=2, first=1, count=4)
        with self.assertRaisesRegex(ValueError, 'Cholesky work'):
            ProjectedNoise(rec, taps, helper_prefix_samples=3, maximum_factor_work=11)
        default = ProjectedNoise(rec, taps, helper_prefix_samples=3)
        allowed = ProjectedNoise(rec, taps, helper_prefix_samples=3, maximum_factor_work=12)
        self.assertEqual(default.maximum_factor_work, 100_000_000)
        np.testing.assert_array_equal(allowed._rest_covariance(), default._rest_covariance())
        for key in ('projected_noise', 'helper_raw_noise'):
            np.testing.assert_array_equal(allowed.draw(82)[key], default.draw(82)[key])
        for invalid in (True, 0, 500_000_001):
            with self.subTest(invalid=invalid):
                with self.assertRaisesRegex(ValueError, 'factor work'):
                    ProjectedNoise(rec, taps, maximum_factor_work=invalid)

    def test_reject_bad_receipt_hash_bandwidth_memory_and_singular_without_jitter(self):
        rec, taps = receipt([1., 1.], raw=4, down=1, first=0)
        with self.assertRaisesRegex(ValueError, 'hash'):
            ProjectedNoise(rec, taps*np.float64(1.01))
        bad = copy.deepcopy(rec); bad['decimation_phase_raw_samples'] = 1
        with self.assertRaisesRegex(ValueError, 'phase'):
            ProjectedNoise(bad, taps)
        bad = copy.deepcopy(rec); bad['finite_input_boundary'] = 'zero, but unspecified endpoints'
        with self.assertRaisesRegex(ValueError, 'boundary'):
            ProjectedNoise(bad, taps)
        bad = copy.deepcopy(rec); bad['output_sample_rate_hz'] = 2
        with self.assertRaisesRegex(ValueError, 'boundary'):
            ProjectedNoise(bad, taps)
        with self.assertRaisesRegex(ValueError, 'dtype'):
            ProjectedNoise(rec, taps.astype(np.float32))
        rec2, taps2 = receipt(np.ones(9), raw=20, down=1, first=0)
        with self.assertRaisesRegex(ValueError, 'bandwidth'):
            ProjectedNoise(rec2, taps2, maximum_bandwidth=4)
        with self.assertRaises(MemoryError):
            ProjectedNoise(rec, taps, memory_limit_bytes=1)
        large_rec, large_taps = receipt(np.ones(20_000), raw=100_000,
            down=100, first=0, count=400)
        with self.assertRaisesRegex(ValueError, 'boundary work'):
            ProjectedNoise(large_rec, large_taps)
        long_prefix_rec, long_prefix_taps = receipt(np.ones(65_000),
            raw=10_000_000, down=256, first=0, count=1600)
        long_prefix = ProjectedNoise(long_prefix_rec, long_prefix_taps,
                                     helper_prefix_samples=1_000_000)
        self.assertEqual(long_prefix.boundary_work_estimate, 0)
        self.assertEqual(long_prefix._active_start, long_prefix._active_stop)
        # Five output rows from four raw degrees of freedom are singular.
        with self.assertRaisesRegex(np.linalg.LinAlgError, 'singular'):
            ProjectedNoise(rec, taps)


if __name__ == '__main__':
    unittest.main()
