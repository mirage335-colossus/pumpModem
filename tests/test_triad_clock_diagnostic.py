"""Bounded sampled-template clock-diagnostic tests; no native build required."""
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_clock_diagnostic import (analyze, awgn_experiment, binomial_result,
                                    gaussian_factor, load_probe, paired_gram)


def fixture(path, values, *, warped=False, target=2.):
    path.mkdir()
    values = np.asarray(values, dtype='<c16')
    n = values.shape[1]
    values.tofile(path/'composite.c128')
    m = dict(schema='triad-pattern-probe-v1', source_only=True, channel_applied=False,
        rows=n, sample_rate_hz=8., fine_rate_hz=.001, carrier_spacing_hz=.00125,
        first_sample=1000, source_samples=2000, calendar_origin_sample=10,
        first_calendar_seconds=123.75, window_seconds=n/8, seed=11, epoch=1793850000,
        symbol_ordinal=0, encrypted=True, key_kind='public test fixture only',
        dtype='<c16', composite_file='composite.c128', composite_shape=[2,n],
        format={'pattern_revision':'independent-apsk-v2'}, geometry={'source_samples':2000},
        shaping_policy='auto', shaping_fallback=False,
        clock_follow=dict(enabled=warped, law='bounded-source-quintic-v1',
            target_offset_seconds=target if warped else 0,
            correction_at_calendar_seconds=0,
            transition_duration_seconds=12500 if warped else 0))
    (path/'manifest.json').write_text(json.dumps(m))
    return m


class ClockDiagnosticTests(unittest.TestCase):
    def setUp(self):
        self.original = np.array([[1,1,1,1],[1,-1,1,-1]], dtype=complex)

    def test_rank_deficient_gram_preserves_joint_noise(self):
        gram, energies = paired_gram(self.original, self.original)
        factor, rank = gaussian_factor(gram)
        self.assertEqual(rank, 2)
        np.testing.assert_allclose(factor@factor.conj().T, gram, atol=2e-15)
        np.testing.assert_allclose(energies, [4,4,4,4])
        np.testing.assert_allclose(factor[:2], factor[2:], atol=2e-15)

    def test_energy_change_is_not_hidden_by_template_normalization(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = Path(d)/'a', Path(d)/'b'
            fixture(a, self.original)
            fixture(b, 2j*self.original, warped=True)
            r = analyze(a, b, ebn0_db=[10], trials=256)
            for bit in r['bits']:
                self.assertAlmostEqual(bit['actual_energy_change_db'], 20*math.log10(2))
                self.assertAlmostEqual(bit['squared_overlap_after_common_complex_gain'], 1.)
                self.assertAlmostEqual(bit['shape_mismatch_loss_db'], 0.)
            self.assertAlmostEqual(r['awgn']['cases'][0]['bits'][0]['actual_warped_en0_db'], 10+20*math.log10(2))
            self.assertEqual(len(r['input_identities']), 4)
            self.assertIn('not a full-message', r['scope'])

    def test_wrong_codeword_discriminator_and_infinite_loss_are_serializable(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = Path(d)/'a', Path(d)/'b'
            fixture(a, self.original)
            fixture(b, self.original[::-1], warped=True)
            r = analyze(a, b)
            for bit in r['bits']:
                self.assertEqual(bit['squared_overlap_after_common_complex_gain'], 0.)
                self.assertTrue(bit['loss_is_infinite'])
                self.assertIsNone(bit['shape_mismatch_loss_db'])
                self.assertEqual(bit['wrong_nominal_codeword_squared_overlap'], 1.)
            json.dumps(r, allow_nan=False)

    def test_paired_seeded_awgn_and_outcome_counts(self):
        gram, energies = paired_gram(self.original, self.original)
        options = dict(ebn0_db=[10,30], trials=512, alpha=.01, seed=37)
        a = awgn_experiment(gram, energies, **options)
        b = awgn_experiment(gram, energies, **options)
        self.assertEqual(a, b)
        self.assertAlmostEqual(a['threshold'], math.log(200))
        self.assertEqual(a['noise_only']['nominal_receiver'], a['noise_only']['oracle_receiver'])
        for case in a['cases']:
            for bit in case['bits']:
                results = [bit[k] for k in ('nominal_source_nominal_receiver',
                    'warped_source_nominal_receiver', 'warped_source_oracle_receiver')]
                self.assertEqual(results[0], results[1])
                self.assertEqual(results[0], results[2])
                self.assertEqual(sum(x['count'] for x in results[0].values()), 512)
        self.assertEqual(a['cases'][1]['bits'][0]['nominal_source_nominal_receiver']['correct_admissions']['count'], 512)

    def test_source_identity_and_trace_guards(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = Path(d)/'a', Path(d)/'b'
            fixture(a, self.original)
            m = fixture(b, self.original, warped=True)
            for key, value in (('seed', 12), ('first_sample', 1001), ('format', {})):
                changed = {**m, key:value}
                (b/'manifest.json').write_text(json.dumps(changed))
                with self.assertRaisesRegex(ValueError, 'identity mismatch'):
                    analyze(a, b)
            (b/'manifest.json').write_text(json.dumps({**m, 'clock_follow':{**m['clock_follow'], 'enabled':False, 'target_offset_seconds':0}}))
            with self.assertRaisesRegex(ValueError, 'nominal trace'):
                analyze(a, b)

    def test_malformed_arrays_and_unsafe_paths_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)/'probe'
            m = fixture(p, self.original)
            (p/'manifest.json').write_text(json.dumps({**m, 'composite_file':'../composite.c128'}))
            with self.assertRaisesRegex(ValueError, 'unsafe'):
                load_probe(p)
            (p/'manifest.json').write_text(json.dumps(m))
            (p/'composite.c128').write_bytes(b'123')
            with self.assertRaisesRegex(ValueError, 'size'):
                load_probe(p)
            values = self.original.copy(); values[0,0] = np.nan
            values.astype('<c16').tofile(p/'composite.c128')
            with self.assertRaisesRegex(ValueError, 'nonfinite'):
                load_probe(p)

    def test_invalid_statistics_inputs_and_exact_interval(self):
        with self.assertRaisesRegex(ValueError, 'nonzero'):
            paired_gram(self.original*0, self.original)
        with self.assertRaisesRegex(ValueError, 'semidefinite'):
            gaussian_factor(np.diag([1,1,1,-1]))
        gram, energies = paired_gram(self.original, self.original)
        for changed in (dict(trials=0), dict(alpha=0), dict(seed=-1), dict(ebn0_db=[math.nan])):
            args = dict(ebn0_db=[10], trials=10, alpha=.01, seed=1)
            args.update(changed)
            with self.assertRaises(ValueError):
                awgn_experiment(gram, energies, **args)
        result = binomial_result(0, 100)
        self.assertEqual(result['interval'][0], 0)
        self.assertAlmostEqual(result['interval'][1], 1-.025**.01)


if __name__ == '__main__':
    unittest.main()
