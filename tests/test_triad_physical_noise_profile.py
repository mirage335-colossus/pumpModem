#!/usr/bin/env python3
"""Small synthetic checks of the frozen physical-noise diagnostic workflow."""
import copy
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_physical_admission import source_sha256
from triad_physical_noise_profile import characterize, checks_for, finalize, prepare


class NoiseProfileTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.binding = dict(backend='test-fixture', device='synthetic', hardware_rate_hz=1024,
            hardware_channels=2, selected_channel=0, mixer_sha256='a'*64,
            audio_binary_sha256='b'*64, receiver_sha256=source_sha256())
        self.geometry = dict(sample_rate_hz=1024, block_samples=256, data_if_hz=180.,
            required_bands_hz=[[150.,210.]], fixed_gain=.01, maximum_capture_seconds=64.)
        self.calibration, self.metadata = self.capture('cal', 10)

    def capture(self, name, seed, *, scale=.01, dc=0., values=None):
        data = (np.random.default_rng(seed).normal(0, scale, 16*1024)+dc
                if values is None else np.asarray(values))
        path, receipt = self.root/(name+'.f32'), self.root/(name+'.json')
        data.astype('<f4').tofile(path)
        receipt.write_text(json.dumps(dict(schema='triad-audio-capture-v1', status='complete',
            failure='', capture_error='', discontinuity=dict(capture=False, playback=False),
            capture=dict(raw_samples=len(data), raw_rate=1024),
            audio_format=dict(capture=dict(hardware_rate=1024, hardware_channels=2,
                                           selected_capture_channel=0)))))
        return path, receipt

    def preparation(self, **kw):
        return prepare(self.calibration, self.metadata, binding=self.binding, geometry=self.geometry,
            variance_proxy=4e-4, assumptions=['Synthetic globally independent Gaussian control.'],
            trim_seconds=1., window_samples=256, **kw)

    def test_independent_heldout_profile_keeps_conditional_scope(self):
        p = self.preparation()
        path = self.root/'prepared.json'; path.write_text(json.dumps(p))
        validation, metadata = self.capture('val', 11)
        result = finalize(path, validation, metadata)
        self.assertTrue(result['qualification']['passed'])
        self.assertNotEqual(result['qualification']['calibration_sha256'],
                            result['qualification']['validation_sha256'])
        self.assertEqual(result['null_model']['bound_scope'], 'conditional-on-profile')
        self.assertIn('no probability coverage', result['qualification']['diagnostic_policy']['statistical_scope'])

    def test_payload_cannot_refit_an_underestimated_proxy(self):
        with self.assertRaisesRegex(ValueError, 'calibration contradicts'):
            prepare(self.calibration, self.metadata, binding=self.binding, geometry=self.geometry,
                variance_proxy=1e-6, assumptions=['Fixture.'], trim_seconds=1., window_samples=256)

    def test_validation_failure_is_retained_without_refitting(self):
        p = self.preparation()
        path = self.root/'prepared.json'; path.write_text(json.dumps(p))
        validation, metadata = self.capture('val', 12, scale=.1)
        result = finalize(path, validation, metadata)
        self.assertFalse(result['qualification']['passed'])
        self.assertEqual(result['null_model']['variance_proxy'], 4e-4)
        self.assertTrue(any(not x['passed'] for x in result['qualification']['checks']))

    def test_calibration_replay_and_old_validation_rejected(self):
        p = self.preparation()
        path = self.root/'prepared.json'; path.write_text(json.dumps(p))
        with self.assertRaisesRegex(ValueError, 'after the frozen'):
            finalize(path, self.calibration, self.metadata)
        os.utime(self.calibration, ns=(p['prepared_unix_ns']+1, p['prepared_unix_ns']+1))
        with self.assertRaisesRegex(ValueError, 'independent capture'):
            finalize(path, self.calibration, self.metadata)

    def test_clipped_capture_and_tone_fail_frozen_diagnostics(self):
        for name, data in [('clip', np.ones(16*1024)),
                           ('tone', .05*np.cos(2*np.pi*180*np.arange(16*1024)/1024))]:
            path, receipt = self.capture(name, 0, values=data)
            result = characterize(path, receipt, binding=self.binding, geometry=self.geometry,
                                  trim_seconds=1., window_samples=256)
            self.assertFalse(all(x['passed'] for x in checks_for(result, 4e-4)))

    def test_constant_dc_removed_but_mean_motion_diagnosed(self):
        path, receipt = self.capture('dc', 13, dc=.1)
        result = characterize(path, receipt, binding=self.binding, geometry=self.geometry,
                              trim_seconds=1., window_samples=256)
        self.assertTrue(all(x['passed'] for x in checks_for(result, 4e-4)))
        data = np.linspace(-.1, .1, 16*1024)
        path, receipt = self.capture('drift', 0, values=data)
        result = characterize(path, receipt, binding=self.binding, geometry=self.geometry,
                              trim_seconds=1., window_samples=256)
        self.assertFalse(all(x['passed'] for x in checks_for(result, 4e-4)))

    def test_global_psd_checks_undoubled_dc_and_nyquist(self):
        n = np.arange(16*1024)
        # At these endpoints the one-sided Welch ordinate is not doubled.
        # Pick a proxy between the old interior maximum and the true diagnostic
        # endpoint maximum: the stronger check must reject without refitting it.
        for name, data in [('dc-motion', .01*np.sin(2*np.pi*.05*n/1024)),
                           ('nyquist', .01*(-1.)**n)]:
            path, receipt = self.capture(name, 0, values=data)
            result = characterize(path, receipt, binding=self.binding, geometry=self.geometry,
                                  trim_seconds=0., window_samples=256)
            interior = result['observed_positive_psd_variance_proxy']
            overall = result['observed_global_psd_variance_proxy']
            self.assertGreater(overall, interior)
            proxy = (interior+overall)/2
            checks = {x['name']: x['passed'] for x in checks_for(result, proxy)}
            self.assertFalse(checks['observed_positive_psd_within_declared_proxy'])

    def test_receipt_format_and_discontinuity_rejected(self):
        initial = json.loads(self.metadata.read_text())
        for change in ('discontinuity', 'channel', 'rate', 'count'):
            m = copy.deepcopy(initial)
            if change == 'discontinuity': m['discontinuity']['capture'] = True
            if change == 'channel': m['audio_format']['capture']['selected_capture_channel'] = 1
            if change == 'rate': m['capture']['raw_rate'] = 512
            if change == 'count': m['capture']['raw_samples'] -= 1
            self.metadata.write_text(json.dumps(m))
            with self.assertRaises(ValueError):
                self.preparation()

    def test_native_adc_receipt_requires_active_asrc_witness(self):
        self.metadata.write_text(json.dumps(dict(schema='triad-audio-stream-v1', mode='capture',
            complete=True, capture_format='adc-v1', capture_observation='pre-asrc-native-mono',
            requested_frames=16384, completed_frames=16384, clipped_samples=0,
            discarded_post_asrc_frames=16000, clock_discipline=dict(enabled=True, reports=10),
            capture_stream_format=dict(hardware_rate=1024, hardware_channels=2, selected_capture_channel=0))))
        self.assertEqual(self.preparation()['calibration']['receipt_scope'],
                         'pre-ASRC ADC with active full-audio clock/ASRC pipeline')
        m = json.loads(self.metadata.read_text()); m['clock_discipline']['enabled'] = False
        self.metadata.write_text(json.dumps(m))
        with self.assertRaises(ValueError): self.preparation()

    def test_short_nonfinite_and_partial_sample_files_rejected(self):
        for name, values in [('short', np.zeros(100)), ('nan', np.full(16384, np.nan))]:
            path, receipt = self.capture(name, 0, values=values)
            with self.assertRaises(ValueError):
                characterize(path, receipt, binding=self.binding, geometry=self.geometry,
                             trim_seconds=1., window_samples=256)
        with self.calibration.open('ab') as stream: stream.write(b'x')
        with self.assertRaisesRegex(ValueError, 'float32'):
            self.preparation()


if __name__ == '__main__':
    unittest.main()
