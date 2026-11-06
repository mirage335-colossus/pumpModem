"""Small source-only checks for the carrier spacing analysis."""
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_spacing_diagnostic import analyze, parse_ratios, reconstruct, _one_pole, campaign_work, MAX_CAMPAIGN_WORK, beat_nulls_in_window


def fixture(path, n=4000, fs=2000., fine=60., spacing=75.):
    t=np.arange(n)/fs
    carrier=np.empty((2,3,n),dtype=np.complex128)
    for b in range(2):
        for k in range(3):
            carrier[b,k]=(.6+.1*k)*(1+.08*np.sin(2*np.pi*.7*t))*np.exp(1j*(.2*b+.3*k))
    env=np.ones(n)
    composite=reconstruct(carrier,fs,0,0,spacing)
    for name,array in [('composite.c128',composite),('carrier.c128',carrier),('envelope.f64',env)]:
        array.tofile(path/name)
    m=dict(schema='triad-pattern-probe-v1',source_only=True,channel_applied=False,
           rows=n,sample_rate_hz=fs,fine_rate_hz=fine,carrier_spacing_hz=spacing,
           first_sample=0,calendar_origin_sample=0,seed=11,dtype='<c16',envelope_dtype='<f8',
           composite_file='composite.c128',composite_shape=[2,n],
           carrier_file='carrier.c128',carrier_shape=[2,3,n],
           envelope_file='envelope.f64',envelope_shape=[n],
           envelope_scope='unit steady source',format={'pattern_revision':'fixture','pulse_shape':'rrc-025-span8'},
           shaping_policy='fixture')
    (path/'manifest.json').write_text(json.dumps(m))


class SpacingDiagnosticTests(unittest.TestCase):
    def test_reconstruction_and_correct_nominal_width(self):
        with tempfile.TemporaryDirectory() as d:
            fixture(Path(d))
            r=analyze(d,[1.25,4/3])
            self.assertLess(r['baseline_reconstruction_relative_error'],1e-12)
            self.assertAlmostEqual(r['candidates'][0]['nominal_triplet_width_hz'],225.)
            self.assertAlmostEqual(r['candidates'][1]['spacing_hz'],80.)
            self.assertAlmostEqual(r['candidates'][1]['nominal_triplet_width_hz'],235.)
            self.assertIn(.1,r['candidates'][1]['rectangular_beat_nulls_80_120ms']['seconds'])
            self.assertIn('not confidence bounds',r['scope'])
            self.assertIn('one window alone',r['interpretation']['frequency_notch'])
            self.assertTrue(r['baseline']['bits'][0]['one_pole_power_detector']['0.1']['measured'])
            self.assertGreaterEqual(r['baseline']['bits'][0]['one_pole_power_detector']['0.1']['matched_template_loss_db'],0.)

    def test_malformed_and_nonfinite_fixtures_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);fixture(p)
            m=json.loads((p/'manifest.json').read_text());m['carrier_shape']=[2,2,4000]
            (p/'manifest.json').write_text(json.dumps(m))
            with self.assertRaisesRegex(ValueError,'carrier_shape'):
                analyze(p,[1.25])
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);fixture(p)
            a=np.memmap(p/'carrier.c128',mode='r+',dtype='<c16',shape=(2,3,4000));a[0,0,0]=complex(float('nan'),0);del a
            with self.assertRaisesRegex(ValueError,'nonfinite'):
                analyze(p,[1.25])

    def test_invalid_ratio_and_nyquist_guard(self):
        with self.assertRaises(ValueError):parse_ratios('1,1')
        with self.assertRaises(ValueError):parse_ratios('nan')
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);fixture(p,fs=1000.)
            with self.assertRaisesRegex(ValueError,'geometry guards'):
                analyze(p,[8.])

    def test_constant_gain_spacing_changes_composite_but_not_carrier_power(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);fixture(p)
            r=analyze(p,[1.25,1.5])
            self.assertEqual(r['candidates'][0]['relative_energy_change_db'],[0.,0.])
            self.assertGreater(abs(r['candidates'][1]['relative_energy_change_db'][0]),1e-5)
            sums0=r['candidates'][0]['bits'][0]['summed_carrier_windows']
            sums1=r['candidates'][1]['bits'][0]['summed_carrier_windows']
            self.assertEqual(sums0,sums1)

    def test_large_integer_coordinates_preserve_fractional_phase(self):
        carriers=np.ones((1,3,12),complex)
        a=reconstruct(carriers,2000.,2**53+123,2**53+120,75.)
        b=reconstruct(carriers,2000.,3,0,75.)
        np.testing.assert_allclose(a,b,rtol=1e-14,atol=1e-14)

    def test_detector_gain_has_positive_matched_loss_for_varying_amplitude(self):
        wave=np.ones(2000,complex)
        wave[300:900]=.35
        wave[900:1500]=1.7
        result=_one_pole(wave,np.ones(len(wave)),1000.,.1)
        self.assertTrue(result['measured'])
        self.assertGreater(result['matched_template_loss_db'],0.)
        self.assertLessEqual(result['normalized_matched_template_overlap'],1.)

    def test_pulse_metadata_and_propagated_probe_rejected(self):
        for field,value,pattern in [('channel_applied',True,'unpropagated'),('dtype','<f4','unpropagated')]:
            with tempfile.TemporaryDirectory() as d:
                p=Path(d);fixture(p);m=json.loads((p/'manifest.json').read_text());m[field]=value;(p/'manifest.json').write_text(json.dumps(m))
                with self.assertRaisesRegex(ValueError,pattern):analyze(p,[1.25])
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);fixture(p);m=json.loads((p/'manifest.json').read_text());m['format']['pulse_shape']='rrc-05-span8';(p/'manifest.json').write_text(json.dumps(m))
            with self.assertRaisesRegex(ValueError,'rolloff'):analyze(p,[1.25])

    def test_campaign_work_is_bounded_before_array_loading(self):
        self.assertEqual(campaign_work([{'rows':100}],4),500)
        with self.assertRaisesRegex(ValueError,'bounded row budget'):
            campaign_work([{'rows':MAX_CAMPAIGN_WORK}],1)

    def test_high_rate_beat_null_report_is_complete_and_bounded(self):
        result=beat_nulls_in_window(75000.)
        self.assertEqual(result['total_count'],3001)
        self.assertEqual(len(result['seconds']),64)
        self.assertTrue(result['truncated'])
        self.assertAlmostEqual(result['seconds'][0],.08)
        self.assertAlmostEqual(result['seconds'][-1],(.08*75000+63)/75000)
        modest=beat_nulls_in_window(80.)
        self.assertEqual(modest['total_count'],3)
        self.assertFalse(modest['truncated'])

    def test_ratio_cardinality_is_bounded(self):
        with self.assertRaises(ValueError):parse_ratios(','.join(['1.25']*17))


if __name__=='__main__':unittest.main()
