#!/usr/bin/env python3
"""Tiny exact references and long-coordinate checks for timing inference."""
from collections import defaultdict
import itertools
import math
from pathlib import Path
import sys
import unittest

import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_timing_forward import SparseTimingForward, TimingState


class TimingForwardTests(unittest.TestCase):
    def test_exact_path_sum_and_fixed_global_model_prior(self):
        # Tiny exhaustive reference uses ordinary path products, not the core
        # forward/logaddexp reduction. Rates -1/0/+1 on this chosen grid give
        # offsets -1/0/+1 after one second (with half-cell interpolation).
        receiver = SparseTimingForward([TimingState(0, 0, 0)], model_count=2,
            model_probabilities=[.3, .7], delay_step_seconds=.00005,
            rate_step_ppm=50, maximum_rate_ppm=50,
            maximum_acceleration_ppm_per_second=50,
            maximum_states=256, maximum_proposals=256)
        paths = [(TimingState(0, 0, 0), 1., np.ones((2, 2)))]
        model_prior = np.array([.3, .7])
        for segment in range(3):
            if segment:
                next_paths = []
                for state, prior, likelihood in paths:
                    targets = range(max(-1, state.rate_index-1), min(1, state.rate_index+1)+1)
                    for rate in targets:
                        move = (state.rate_index+rate)/2
                        floor = math.floor(move)
                        fraction = move-floor
                        for delay_move, split in [(floor, 1-fraction), (floor+1, fraction)]:
                            if split:
                                next_paths.append((TimingState(0, state.delay_index+delay_move, rate),
                                    prior*split/len(targets), likelihood.copy()))
                paths = next_paths
                receiver.predict(1.)
            def evidence(state):
                # Distinct fixed model behavior, both bits, delay/rate and time.
                return np.array([[.2*(state.delay_index+1)-.1*segment, -.15*state.rate_index],
                                 [-.3*state.delay_index+.07*segment, .1*(state.rate_index+segment)]])
            emissions = np.stack([evidence(state) for state in receiver.states], axis=-1)
            receiver.observe(emissions)
            paths = [(state, prior, likelihood*np.exp(evidence(state))) for state, prior, likelihood in paths]
            exact = np.zeros((2, 2))
            for _, prior, likelihood in paths:
                exact += prior*likelihood
            expected = np.log((model_prior[:, None]*exact).sum(axis=0))
            np.testing.assert_allclose(receiver.scores(), expected, rtol=0, atol=2e-14)
        self.assertEqual(receiver.observations, 3)
        self.assertEqual(receiver.pruning_events, 0)

    def test_pruning_is_conservative_and_does_not_renormalize_noise_evidence(self):
        states = [TimingState(0, x, 0) for x in range(5)]
        full = SparseTimingForward(states, maximum_rate_ppm=0, maximum_states=5, maximum_proposals=5)
        beam = SparseTimingForward(states, maximum_rate_ppm=0, maximum_states=2, maximum_proposals=5)
        for segment in range(4):
            if segment:
                full.predict(4); beam.predict(4)
            def score(state):
                return np.array([.1*(state.delay_index+segment), -.2*state.delay_index])
            full.observe(np.stack([score(s) for s in full.states], axis=-1)[None, ...])
            beam.observe(np.stack([score(s) for s in beam.states], axis=-1)[None, ...])
            self.assertTrue(np.all(beam.scores() <= full.scores()+1e-14))
        null = SparseTimingForward(states, maximum_rate_ppm=0, maximum_states=2, maximum_proposals=5)
        null.observe(np.zeros((1, 2, 5)))
        np.testing.assert_allclose(null.scores(), math.log(2/5), rtol=0, atol=1e-15)
        null.predict(100000)
        null.observe(np.zeros((1, 2, 2)))
        np.testing.assert_allclose(null.scores(), math.log(2/5), rtol=0, atol=1e-15)
        self.assertAlmostEqual(null.report()['retained_initial_path_probability'], .4)
        self.assertFalse(null.report().get('admitted', False))

    def test_proposal_cap_and_erasure_unit_likelihood(self):
        receiver = SparseTimingForward([TimingState(0, 0, 0)], delay_step_seconds=.00001,
            rate_step_ppm=50, maximum_rate_ppm=200,
            maximum_acceleration_ppm_per_second=100,
            maximum_states=3, maximum_proposals=4)
        receiver.observe(np.zeros((1, 2, 1)))
        receiver.predict(4)
        self.assertLessEqual(len(receiver.states), 4)
        score = receiver.scores().copy()
        self.assertTrue(np.all(score < 0))
        receiver.observe(np.zeros((1, 2, len(receiver.states))))
        self.assertTrue(np.all(receiver.scores() <= score+1e-14))
        self.assertLessEqual(len(receiver.states), 3)
        self.assertGreater(receiver.report()['states_dropped'], 0)

    def test_transition_probabilities_and_acceleration_bound(self):
        receiver = SparseTimingForward([TimingState(0, 0, 0)], maximum_rate_ppm=100,
            maximum_acceleration_ppm_per_second=20)
        for rate in range(-2, 3):
            for elapsed in (0., .1, 4., 10., 1000.):
                state = TimingState(2, 17, rate)
                edges = receiver.transitions(state, elapsed)
                self.assertAlmostEqual(sum(p for _, p in edges), 1.)
                for target, probability in edges:
                    self.assertGreater(probability, 0)
                    self.assertEqual(target.group, 2)
                    self.assertLessEqual(abs(target.rate_index*50), 100)
                    self.assertLessEqual(abs(target.rate_index-rate)*50, 20*elapsed+1e-12)

    def test_large_global_integer_offset_keeps_fractional_transitions(self):
        receiver = SparseTimingForward([TimingState(0, 0, 1)], maximum_rate_ppm=50,
            maximum_acceleration_ppm_per_second=0, delay_step_seconds=.0001)
        small = receiver.transitions(TimingState(0, 0, 1), 1.)
        origin = 2**54+137
        large = receiver.transitions(TimingState(0, origin, 1), 1.)
        self.assertEqual([(s.delay_index, p) for s, p in small],
                         [(s.delay_index-origin, p) for s, p in large])
        self.assertEqual(len(large), 2)
        with self.assertRaises(OverflowError):
            receiver.transitions(TimingState(0, 2**63-1, 1), 1.)

    def test_multi_day_lattice_travel_keeps_constant_workspace(self):
        # 32 widely spaced public anchors span >47days; emissions are erased
        # (unit likelihood), not a claim of long sampled-waveform reception.
        receiver = SparseTimingForward([TimingState(0, 0, 1)], maximum_rate_ppm=50,
            maximum_acceleration_ppm_per_second=0, delay_step_seconds=.001,
            maximum_states=1, maximum_proposals=2)
        for _ in range(32):
            receiver.predict(128000.)
            receiver.observe(np.zeros((1, 2, len(receiver.states))))
        self.assertGreater(receiver.elapsed_seconds, 47*86400)
        self.assertGreater(receiver.states[0].delay_index*.001, 200.)
        self.assertLessEqual(receiver.report()['peak_states'], 2)
        self.assertLessEqual(receiver.report()['peak_transition_states'], 2)
        self.assertTrue(np.all(receiver.scores() <= 0))

    def test_public_group_and_model_priors_are_applied_only_once(self):
        receiver = SparseTimingForward([TimingState(7, 0, 0), TimingState(3, 0, 0), TimingState(7, 0, 0)],
            initial_probabilities=[.2, .5, .3], model_count=2, model_probabilities=[.25, .75],
            maximum_rate_ppm=0)
        self.assertEqual(receiver.states, (TimingState(3, 0, 0), TimingState(7, 0, 0)))
        for _ in range(10):
            receiver.observe(np.zeros((2, 2, 2)))
            receiver.predict(4.)
        np.testing.assert_allclose(receiver.scores(), 0., rtol=0, atol=2e-15)
        weights = SparseTimingForward([TimingState(0, 0, 0), TimingState(1, 0, 0)],
            initial_probabilities=[1e308, 1e308], maximum_rate_ppm=0)
        np.testing.assert_allclose(weights.scores(), 0., rtol=0, atol=2e-15)

    def test_prediction_observation_lifecycle_bounds_transition_workspace(self):
        receiver = SparseTimingForward([TimingState(0, 0, 0)], delay_step_seconds=.00001,
            maximum_rate_ppm=200, maximum_states=2, maximum_proposals=16)
        receiver.predict(20.)  # Optional initial prediction is supported.
        self.assertGreater(len(receiver.states), receiver.maximum_states)
        prior_states = receiver.states
        prior_scores = receiver.scores().copy()
        with self.assertRaisesRegex(ValueError, "preceding segment"):
            receiver.predict(20.)
        self.assertEqual(receiver.states, prior_states)
        np.testing.assert_array_equal(receiver.scores(), prior_scores)
        receiver.observe(np.zeros((1, 2, len(receiver.states))))
        self.assertLessEqual(len(receiver.states), receiver.maximum_states)
        with self.assertRaisesRegex(ValueError, "one prediction"):
            receiver.observe(np.zeros((1, 2, len(receiver.states))))
        for _ in range(8):
            receiver.predict(20.)
            receiver.observe(np.zeros((1, 2, len(receiver.states))))
        self.assertLessEqual(receiver.report()['peak_transition_states'],
                             receiver.report()['transition_workspace_state_bound'])

    def test_invalid_input_and_overflow_fail_explicitly(self):
        bad = [dict(delay_step_seconds=0), dict(rate_step_ppm=math.nan),
               dict(maximum_rate_ppm=-1), dict(maximum_acceleration_ppm_per_second=-1),
               dict(maximum_states=0), dict(maximum_proposals=1, maximum_states=2),
               dict(rate_step_ppm=.001, maximum_rate_ppm=1000),
               dict(model_probabilities=[0], model_count=1)]
        for options in bad:
            with self.subTest(options=options), self.assertRaises(ValueError):
                SparseTimingForward([TimingState(0, 0, 0)], **options)
        with self.assertRaisesRegex(ValueError, "not representable"):
            SparseTimingForward([TimingState(0, 2, 0)], delay_step_seconds=1e308)
        huge = SparseTimingForward([TimingState(0, 1, 1)], delay_step_seconds=1e308,
            maximum_acceleration_ppm_per_second=0)
        with self.assertRaisesRegex(ValueError, "not representable"):
            huge.predict(1e308)
        self.assertEqual(huge.states, (TimingState(0, 1, 1),))
        receiver = SparseTimingForward([TimingState(0, 0, 0)])
        for dt in (-1, math.nan, math.inf):
            with self.assertRaises(ValueError): receiver.predict(dt)
        for emission in (np.zeros((2, 1)), np.full((1, 2, 1), math.nan), np.full((1, 2, 1), math.inf)):
            with self.assertRaises(ValueError): receiver.observe(emission)
        receiver.observe(np.full((1, 2, 1), 1e308))
        receiver.predict(0.)
        with self.assertRaises(FloatingPointError): receiver.observe(np.full((1, 2, 1), 1e308))


if __name__ == '__main__':
    unittest.main()
