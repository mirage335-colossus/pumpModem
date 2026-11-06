"""Cheap stream adapter guards; waveform/likelihood checks live separately."""
import io
import math
from pathlib import Path
import sys
import tempfile
import unittest
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_continuous import (carrier_phase, endpoint_decision, read_exact,
                              receiver_noise_variance, validate_stream_identity, BoundedClockControl, execute, parser,
                              disciplined_capture_extent, make_timing_forward, tracking_source_basis,
                              tracking_receiver_class)
from triad_timing_forward import TimingState
from triad_control_clock import ControlClock, ClockReading


class ContinuousTests(unittest.TestCase):
    def test_default_live_clock_and_public_tracking_policy(self):
        options = parser().parse_args(['--report', '/tmp/unused-triad-clock-report'])
        self.assertEqual(options.follow_system_clock, 'auto')
        self.assertEqual(options.receiver, 'auto')
        self.assertEqual(options.clock_phase_max_ppm, 200)
        from triad_tracking_receiver import PracticalTrackingReceiver, PersistentTrackingReceiver, TrackingReceiver
        self.assertIs(tracking_receiver_class(), PracticalTrackingReceiver)
        self.assertIs(tracking_receiver_class('tracking-persistent'), PersistentTrackingReceiver)
        self.assertIs(tracking_receiver_class('tracking-reset'), TrackingReceiver)
        bank = dict(coordinates=[[0, 0, 0, 0]])
        forward, seconds = make_timing_forward(bank, dict(nominal_triplet_bandwidth_hz=225), 3)
        self.assertEqual(forward.states, (TimingState(0, 0, 0),))
        self.assertEqual(seconds, 4)
        self.assertAlmostEqual(forward.delay_step_seconds, 1/900)
        for config in ([], dict(initial_rate_indices=2), dict(segment_seconds='4'),
                       dict(segment_seconds=.1), dict(secret_tx_trace=True)):
            with self.assertRaises(ValueError):
                make_timing_forward(dict(bank, tracking=config), dict(nominal_triplet_bandwidth_hz=225), 3)

    def test_slow_envelope_cadence_and_audio_phase_are_separate(self):
        bank = dict(coordinates=[[0, 0, 0, 0]])
        for width, center, expected in ((.000625, .0005, 4096.), (.0625, .05, 500.),
                                        (.000625, 1725., 4.), (62.5, .05, 4.)):
            _, seconds = make_timing_forward(bank, dict(nominal_triplet_bandwidth_hz=width,
                data_if_hz=center), 1)
            self.assertAlmostEqual(seconds, expected)
        _, seconds = make_timing_forward(dict(bank, tracking=dict(segment_seconds=64)),
            dict(nominal_triplet_bandwidth_hz=.000625, data_if_hz=.0005), 1)
        self.assertEqual(seconds, 64)
        _, seconds = make_timing_forward(bank,
            dict(nominal_triplet_bandwidth_hz=.000625, data_if_hz=.5), 1)
        self.assertLess(seconds, 224.)
        self.assertLessEqual((.5+.000625/2)*20e-6*seconds**2/8, .0625+1e-12)
        forward, seconds = make_timing_forward(bank, dict(nominal_triplet_bandwidth_hz=.000625,
            data_if_hz=.0005, sample_rate_hz=6000), 1)
        self.assertLessEqual(round(seconds*6000), 1_048_576)
        self.assertEqual(forward.rate_step_ppm, 200.)
        # Explicit lattice indices retain their previous physical meaning.
        forward, _ = make_timing_forward(dict(bank, tracking=dict(initial_rate_indices=[1])),
            dict(nominal_triplet_bandwidth_hz=.000625, data_if_hz=.0005), 1)
        self.assertEqual(forward.rate_step_ppm, 50.)

    def test_mapped_basis_composes_local_coordinates_remote_rate_and_if_once(self):
        calls = []
        class Source:
            def basis_mapped(self, origin, offsets, rows, center):
                calls.append((origin, offsets.copy(), rows, center))
                return np.ones((len(rows), len(offsets)), complex)
        class Forward:
            rate_step_ppm = 50
            delay_step_seconds = .001
        clock, arrival, frequency, center, anchor = 75., .7, 1.5, 1725., 20.
        render = tracking_source_basis(Source(), [[0, arrival, frequency, clock]], [[.01, .2]],
            38., Forward(), center, template_backend='direct')
        result = render([TimingState(0, 31, -4)], anchor, 0, 3, 100000,
            np.array([.25, 1.2501, 2.2502]))
        self.assertEqual(result.shape, (3, 1, 1, 3))
        self.assertTrue(np.all(result[0] == 0))
        origin, offsets, rows, fc = calls[0]
        self.assertEqual((origin, fc), (100000, center))
        b, epoch, mapped_arrival, cfo, ppm, delay, doppler = rows[0]
        t = (origin+offsets)/6000
        base, velocity = 1+clock*1e-6, -200e-6
        actual_source = (t-mapped_arrival-delay)*(1+ppm*1e-6)
        expected_source = base*(t-arrival-38.)+.031+velocity*(t-anchor)-delay*(base+velocity)
        np.testing.assert_allclose(actual_source, expected_source, atol=2e-14, rtol=0)
        # Baseband clock already contributes to the old acquisition CFO.
        self.assertAlmostEqual(cfo+center*(base-1), frequency)

    def test_full_audio_capture_endpoint_covers_both_clocks_and_public_delays(self):
        frames = 1_000_000*6000
        result = disciplined_capture_extent(frames, 6000, .001, [[0, 0, 0, 0]], [[0, 0]])
        self.assertEqual(result, 6_012_006_003)
        self.assertEqual(disciplined_capture_extent(6000, 6000, .001,
            [[0, .1, 0, 100]], [[.01, 0]]), 6676)
        self.assertGreater(disciplined_capture_extent(2**54+1, 6000, .001,
            [[0, 0, 0, 0]], [[0, 0]]), 2**54+1)
        with self.assertRaisesRegex(ValueError, 'sample counter'):
            disciplined_capture_extent(2**64-1, 6000, .001, [[0, 0, 0, 0]], [[0, 0]])

    def test_full_audio_policy_is_explicit_and_rejects_double_steering(self):
        with tempfile.TemporaryDirectory() as directory:
            report = str(Path(directory)/'result.json')
            flags = ['--mode', 'transmit', '--report', report, '--follow-system-clock', 'continuous']
            self.assertEqual(parser().parse_args(flags).follow_system_clock, 'continuous')
            with self.assertRaisesRegex(ValueError, 'source-only clock steering'):
                execute(flags+['--', '--clock-follow-bounded'])
            with self.assertRaisesRegex(ValueError, 'dated TX/RX hardware'):
                execute(['--mode', 'simulate', '--bank', 'not-opened.json', '--report', report,
                    '--follow-system-clock', 'continuous'])
            self.assertFalse(Path(report).exists())

    def test_clock_follow_uses_original_envelope_without_changing_receiver_bank(self):
        now = [10000000000]
        offset = [1700000000000000000]
        clock = ControlClock(reference_now=lambda: now[0], reader=lambda:
            ClockReading(now[0]+offset[0], now[0], now[0]))
        control = BoundedClockControl(clock)
        class Source:
            targets = []
            def clock_target(self, target):
                self.targets.append(target)
                return {'accepted': True}
        source = Source()
        def update(delta):
            now[0] += 1000000000
            offset[0] += delta
            control.poll(source)
        update(2_000_000_000)
        update(1_000_000_000)  # individually small, but outside original envelope
        update(-2_000_000_000)
        update(-2_000_000_000)
        update(10)  # paired-read jitter is not another waveform correction
        self.assertEqual(source.targets, [2., 1., -1.])
        self.assertEqual(control.report['outside_envelope'], 1)
        self.assertEqual(control.origin.offset_ns, 1700000000000000000)

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
