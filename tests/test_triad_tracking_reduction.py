#!/usr/bin/env python3
"""Local-window numerical validation of the multi-day complex-envelope export.

These tests do not count as complete multi-day reception or physical audio
qualification. A high-rate unwarped native source is independently interpolated
at exported clock coordinates; high-IF cases additionally pass through a real
audio carrier, downconversion and anti-alias filtering. No private coordinates
are supplied to a receiver. None/Off channel is explicit.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import resource
import subprocess
import tempfile
import time
import unittest

import numpy as np
from scipy.interpolate import CubicSpline
from scipy.signal import resample_poly


ROOT = Path(__file__).resolve().parents[1]
PARSER = argparse.ArgumentParser()
PARSER.add_argument('native', nargs='?', type=Path)
PARSER.add_argument('--report', type=Path)
PARSER.add_argument('--channel-refinement', action='store_true',
                    help='Include the occasional full 38-day shared-channel refinement')
ARGS, REMAINING = PARSER.parse_known_args()
NATIVE = ARGS.native.resolve() if ARGS.native else None
REPORT = {'window_scope': 'Independent source-window reduction checks, not full-span reception, '
          'channel qualification, physical resampler qualification, or hardware measurement',
          'window_channel': 'None/Off', 'cases': []}


def export(path, *, rate, fs, start, length, center, profile):
    numerator, denominator = (1, round(1/fs)) if fs < 1 else (int(fs), 1)
    command = [str(NATIVE), '--output', str(path), '--rate', str(rate),
        '--sample-rate', str(numerator), '--sample-rate-denominator', str(denominator),
        '--days', '40', '--data-if', str(center), '--clock-profile', profile,
        '--seed', '87231', '--key-seed', '73219', '--template-ppm', '0',
        '--source-window', str(start), str(length), '--max-output-bytes', '100000000']
    subprocess.run(command, check=True, capture_output=True, timeout=30)
    manifest = json.loads((path/'manifest.json').read_text())
    return manifest


def complex_samples(path, bit, warped):
    return np.fromfile(path/f'source_{"warped" if warped else "unwarped"}_bit{bit}.c64', '<c8').astype(np.complex128)


def metrics(actual, reference):
    energy = float(np.vdot(reference, reference).real)
    if energy <= 1e-12:
        raise AssertionError('window is uninformative: no source energy')
    norm_actual = float(np.vdot(actual, actual).real)
    overlap = abs(np.vdot(reference, actual))**2/(energy*norm_actual)
    return {'relative_rms_error': float(np.linalg.norm(actual-reference)/np.sqrt(energy)),
            'peak_error': float(np.max(np.abs(actual-reference))),
            'coherent_mismatch_loss_db': float(-10*np.log10(min(1., overlap))),
            'energy_ratio_db': float(10*np.log10(norm_actual/energy))}


@unittest.skipUnless(NATIVE, 'pass the native triad_tracking_export binary')
class ReducedEnvelopeChecks(unittest.TestCase):
    def check_window(self, name, rate, fs, start, length, center, profile, audio=False):
        began = time.perf_counter()
        with tempfile.TemporaryDirectory(prefix='triad-reduction-') as temporary:
            root = Path(temporary)
            low = root/'reduced'
            m = export(low, rate=rate, fs=fs, start=start, length=length,
                       center=center, profile=profile)
            t = (m['first_sample']+np.arange(m['samples']))/fs
            u = np.fromfile(low/'oracle_source_time.f64', '<f8')
            self.assertEqual(len(u), len(t))
            self.assertTrue(np.all(np.diff(u) > 0))
            high_fs = 6000 if audio else 64
            self.assertEqual(m['source_calendar'], 'canonical-8chips-v1')
            # The exporter now declares a common physical origin at every Fs;
            # native integer padding is not a physical time displacement.
            source_coordinate = u
            high_start = np.floor(source_coordinate.min())-2
            high_length = np.ceil(source_coordinate.max())-high_start+2
            high = root/'high-rate'
            mh = export(high, rate=rate, fs=high_fs, start=high_start,
                        length=high_length, center=0, profile='off')
            self.assertEqual(mh['source_calendar'], 'canonical-8chips-v1')
            origin_delta = mh['canonical_payload_origin_seconds']-m['canonical_payload_origin_seconds']
            self.assertEqual(origin_delta, 0)
            source_coordinate = u+origin_delta
            th = (mh['first_sample']+np.arange(mh['samples']))/high_fs
            # Interpolation is an independent SciPy numerical reference, not
            # the production ASRC. This is safe for the deliberately slow DATA
            # envelope; the high IF itself is evaluated analytically below.
            phase = np.exp(2j*np.pi*np.remainder(np.longdouble(center)*
                           (u.astype(np.longdouble)-t.astype(np.longdouble)), 1)).astype(np.complex128)
            outcomes = []
            for bit in (0, 1):
                high_source = complex_samples(high, bit, False)
                source = CubicSpline(th-th[0], high_source)
                envelope = source(source_coordinate-th[0])
                predicted = envelope*phase
                actual = complex_samples(low, bit, True)
                measurement = metrics(actual, predicted)
                self.assertLess(measurement['relative_rms_error'], 3e-5)
                self.assertLess(measurement['coherent_mismatch_loss_db'], .00001)
                outcome = {'bit': bit, 'source_reference': measurement,
                           'omitted_if_phase_control': metrics(actual, envelope)}
                if audio and start >= 4000:
                    self.assertGreater(outcome['omitted_if_phase_control']['coherent_mismatch_loss_db'], 10)
                if audio:
                    ratio = high_fs//int(fs)
                    ta = t[0]+np.arange(round(length*high_fs))/high_fs
                    # Reconstruct only this local smooth coordinate window.
                    # Data is resampled before the IF phase, matching whole-
                    # audio time warp, rather than an envelope-only shortcut.
                    ua = CubicSpline(t-t[0], u-t)(ta-t[0])+ta
                    warped = source(ua+origin_delta-th[0])*np.exp(2j*np.pi*
                        np.remainder(np.longdouble(center)*(ua.astype(np.longdouble)-ta.astype(np.longdouble)), 1))
                    carrier = np.exp(2j*np.pi*np.remainder(np.longdouble(center)*ta.astype(np.longdouble), 1))
                    pcm = 2*np.real(warped*carrier)
                    mixed = np.asarray(pcm*np.conj(carrier), np.complex128)
                    recovered = resample_poly(mixed, 1, ratio, window=('kaiser', 10.))
                    # Exclude anti-alias-filter boundary transients explicitly.
                    edge = int(4*fs)
                    kept = slice(edge, len(actual)-edge)
                    outcome['real_audio_downconversion'] = metrics(recovered[kept], actual[kept])
                    self.assertLess(outcome['real_audio_downconversion']['relative_rms_error'], .0005)
                    self.assertLess(outcome['real_audio_downconversion']['coherent_mismatch_loss_db'], .0001)
                outcomes.append(outcome)
            rates_ppm = (np.diff(u)/np.diff(t)-1)*1e6
            REPORT['cases'].append({'name': name, 'rate_hz': rate, 'sample_rate_hz': fs,
                'reference_sample_rate_hz': high_fs, 'start_seconds': start,
                'window_seconds': length, 'data_if_hz': center, 'clock_profile': profile,
                'source_calendar_alignment_seconds': float(origin_delta),
                'source_clock_departure_seconds': [float((u-t).min()), float((u-t).max())],
                'finite_difference_rate_ppm_range': [float(rates_ppm.min()), float(rates_ppm.max())],
                'native_manifest': {key:m[key] for key in ('source_sha256','binary_sha256','clock_controller_ticks','oracle_clock_max_applied_rate_ppm')},
                'outcomes': outcomes, 'wall_seconds': time.perf_counter()-began})

    def test_startup_high_if_without_updates(self):
        self.check_window('startup-high-if', .03, 8, 3000, 20, 1725, 'periodic', True)

    def test_correction_high_if(self):
        self.check_window('correction-high-if', .03, 8, 4000, 20, 1725, 'periodic', True)

    def test_38_day_high_if(self):
        self.check_window('38-day-high-if', .03, 8, 38*86400, 20, 1725, 'wandering', True)

    def test_38_day_reciprocal_rate_complex_hardware(self):
        self.check_window('38-day-low-if-complex-hardware', .001, 1/16,
                          38*86400, 512, .0005, 'periodic')


@unittest.skipUnless(NATIVE and ARGS.channel_refinement, 'occasional --channel-refinement qualification')
class SharedChannelRefinement(unittest.TestCase):
    def test_complete_38_day_random_channel(self):
        began = time.perf_counter()
        with tempfile.TemporaryDirectory(prefix='triad-channel-refinement-') as temporary:
            root = Path(temporary)
            manifests, samples, commands = [], [], []
            for denominator in (8, 4):
                path = root/f'fs1over{denominator}'
                command = [str(NATIVE), '--output', str(path), '--rate', '.001',
                    '--sample-rate', '1', '--sample-rate-denominator', str(denominator),
                    '--days', '38', '--data-if', 'half-rate', '--clock-profile', 'periodic',
                    '--seed', '9109', '--key-seed', '73219', '--template-ppm', '0', '--bit', '0',
                    '--shift', '8270', '--spread', 'sub9best', '--random-doppler-width-hz', '.001',
                    '--delay-excursion-ms', '.005', '--delay-knot-seconds', '3600',
                    '--selective-each-depth', '.65', '--max-output-bytes', '100000000']
                subprocess.run(command, check=True, capture_output=True, timeout=30)
                m = json.loads((path/'manifest.json').read_text())
                self.assertEqual(m['source_calendar'], 'canonical-8chips-v1')
                self.assertEqual(m['channel_seed'], 9109)
                self.assertFalse(m['channel_noise_enabled'])
                manifests.append(m)
                samples.append(np.fromfile(path/'signal_bit0.c64', '<c8').astype(np.complex128))
                commands.append(command)
            low, high = samples
            downsampled = resample_poly(high, 1, 2, window=('kaiser', 10.))
            count = min(len(low), len(downsampled))
            # Complete channel tails were generated in both runs. Only the
            # numerical resampling filter's boundary pads are excluded here.
            edge = 16
            a, b = low[edge:count-edge], downsampled[edge:count-edge]
            result = metrics(a, b)
            scale = np.sqrt(np.vdot(b, b).real/np.vdot(a, a).real)
            result['energy_normalized_relative_rms_error'] = float(np.linalg.norm(a*scale-b)/np.linalg.norm(b))
            self.assertTrue(np.isfinite(list(result.values())).all())
            REPORT['shared_channel_refinement'] = {
                'scope': 'Full sampled source/channel refinement, no receiver/noise reliability claim',
                'sample_rates_hz': [.125, .25], 'actual_source_bit_seconds': manifests[0]['bit_seconds'],
                'common_compared_samples': len(a), 'compared_seconds': len(a)*8,
                'excluded_resampling_boundary_seconds_each': edge*8,
                'source_calendar_alignment_seconds': 0,
                'sample_counts': [len(x) for x in samples],
                'channel_tail_samples': [m['channel_reports'][0]['tail_samples'] for m in manifests],
                'metrics': result, 'commands': commands,
                'manifests': manifests,
                'coupling_limits': 'Fixed physical-time random ray, delay and GaussianErf fields share the seed. '
                    'Sample-dependent Wiener/colored oscillator, noise and physical ASRC are disabled. '
                    'Discrete selective-filter and delay interpolation responses can converge with Fs; '
                    'this pair quantifies refinement and does not prove continuous-time convergence.',
                'wall_seconds': time.perf_counter()-began}


if __name__ == '__main__':
    started = time.perf_counter()
    result = unittest.main(argv=[__file__]+REMAINING, exit=False)
    REPORT['wall_seconds'] = time.perf_counter()-started
    REPORT['cpu_seconds_including_children'] = sum(resource.getrusage(w).ru_utime+
        resource.getrusage(w).ru_stime for w in (resource.RUSAGE_SELF, resource.RUSAGE_CHILDREN))
    REPORT['test_source_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    REPORT['successful'] = result.result.wasSuccessful()
    if ARGS.report:
        ARGS.report.parent.mkdir(parents=True, exist_ok=True)
        ARGS.report.write_text(json.dumps(REPORT, indent=2)+'\n')
    raise SystemExit(not result.result.wasSuccessful())
