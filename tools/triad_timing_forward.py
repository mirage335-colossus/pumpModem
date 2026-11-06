#!/usr/bin/env python3
"""Bounded sparse forward likelihood for a public clock-motion model.

A state stores source-clock offset and rate at a segment's *physical-time*
anchor. Positive offset means source time is ahead of physical input time.
Segment emissions must be normalized log likelihood ratios from independent
segment channel priors. Carrying/merging channel posteriors from different
clock histories would define a different, generally approximate algorithm.

The state lattice can travel indefinitely. Beam pruning deletes path terms;
it NEVER redistributes retained probability or divides by retained mass. Thus
its score is a pointwise lower bound on the corresponding full timing-model
likelihood ratio. This property does not qualify an incorrectly specified
physical/colored-noise null and does not provide an anytime admission rule.
"""
from __future__ import annotations

from dataclasses import dataclass
import math
from typing import Iterable, Sequence

import numpy as np


@dataclass(frozen=True, order=True)
class TimingState:
    group: int
    delay_index: int
    rate_index: int


def _logsumexp(values, axis=None):
    """FP64 reduction with a defined all-zero-probability result."""
    x = np.asarray(values, dtype=np.float64)
    maximum = np.max(x, axis=axis, keepdims=True)
    finite = np.isfinite(maximum)
    shifted = np.subtract(x, maximum, out=np.full_like(x, -np.inf), where=finite)
    result = np.log(np.exp(shifted).sum(axis=axis, keepdims=True),
                    out=np.full_like(maximum, -np.inf), where=finite) + np.where(finite, maximum, 0.)
    if axis is None:
        return float(result.reshape(-1)[0])
    return np.squeeze(result, axis=axis)


def _integer(value, name, minimum, maximum):
    if isinstance(value, bool) or not isinstance(value, (int, np.integer)) or not minimum <= int(value) <= maximum:
        raise ValueError(f"{name} must be an integer in [{minimum},{maximum}]")
    return int(value)


class SparseTimingForward:
    """Forward sum over a fixed public delay/rate Markov model.

    ``predict(dt)`` propagates between segment anchors. At each anchor the
    new rate is uniform over grid rates within ``acceleration*dt`` of the old
    rate and the declared absolute bound. Offset advances by the trapezoidal
    integral of old/new rate, split between adjacent delay-grid indices using
    linear interpolation probabilities. This is a discrete anchor model, not
    an assertion that all continuously bounded clock trajectories are exact
    lattice members.

    ``observe`` accepts ``[model, bit(0/1), state]`` segment log-LRs. A model
    choice (for example channel coherence) is global and mixed only by
    ``scores()``. The bit prior is deliberately NOT included; the operational
    endpoint policy owns that prior and all duration/acquisition alternatives.

    The initial states/probabilities are public and supplied by the caller.
    A single zero-rate initial state is a calibrated-start assumption, not an
    estimate of an unknown oscillator. Prediction and observation alternate;
    an initial observation or one initial prediction is allowed. Erased segments
    use unit-likelihood observations, never consecutive predictions that bypass
    the posterior state cap. All state lists are deterministic and
    sorted; emission columns must use the current ``states`` ordering.
    """
    def __init__(self, initial_states: Iterable[TimingState],
                 initial_probabilities: Sequence[float] | None = None, *,
                 model_count=1, model_probabilities: Sequence[float] | None = None,
                 delay_step_seconds=.001, rate_step_ppm=50., maximum_rate_ppm=2000.,
                 maximum_acceleration_ppm_per_second=20.,
                 maximum_states=64, maximum_proposals=256):
        self.model_count = _integer(model_count, "model count", 1, 64)
        self.maximum_states = _integer(maximum_states, "state cap", 1, 4096)
        self.maximum_proposals = _integer(maximum_proposals, "proposal cap", self.maximum_states, 16384)
        self.delay_step_seconds = float(delay_step_seconds)
        self.rate_step_ppm = float(rate_step_ppm)
        self.maximum_rate_ppm = float(maximum_rate_ppm)
        self.maximum_acceleration_ppm_per_second = float(maximum_acceleration_ppm_per_second)
        if (not math.isfinite(self.delay_step_seconds) or self.delay_step_seconds <= 0 or
            not math.isfinite(self.rate_step_ppm) or self.rate_step_ppm <= 0 or
            not math.isfinite(self.maximum_rate_ppm) or self.maximum_rate_ppm < 0 or self.maximum_rate_ppm >= 1_000_000 or
            not math.isfinite(self.maximum_acceleration_ppm_per_second) or self.maximum_acceleration_ppm_per_second < 0):
            raise ValueError("finite positive timing-grid steps and nonnegative dynamics bounds required")
        self.rate_index_bound = math.floor(self.maximum_rate_ppm/self.rate_step_ppm)
        if self.rate_index_bound > 2048:
            raise ValueError("timing rate lattice exceeds the bounded transition workspace")
        states = tuple(initial_states)
        if not states or len(states) > self.maximum_proposals:
            raise ValueError("public initial state count must fit the proposal cap")
        for state in states:
            self._validate_state(state)
        states = tuple(TimingState(int(state.group), int(state.delay_index), int(state.rate_index)) for state in states)
        self.transition_workspace_state_bound = 2*max(self.maximum_states, len(states))*(2*self.rate_index_bound+1)
        if self.transition_workspace_state_bound > 131072:
            raise ValueError("timing transition lattice exceeds the bounded candidate workspace")
        probabilities = self._probabilities(initial_probabilities, len(states), "initial timing")
        model_probability = self._probabilities(model_probabilities, self.model_count, "global model")
        # Merge duplicate public states without altering their total prior.
        merged = {}
        for state, probability in zip(states, probabilities):
            if probability:
                merged[state] = merged.get(state, 0.) + probability
        self.states = tuple(sorted(merged))
        self._prior = np.array([merged[state] for state in self.states], dtype=np.float64)
        with np.errstate(divide="ignore"):
            self._log_weights = np.broadcast_to(
                np.log(model_probability)[:, None, None] + np.log(self._prior)[None, None, :],
                (self.model_count, 2, len(self.states))).copy()
        self._prediction_pending = False
        self.predictions = 0
        self.observations = 0
        self.pruning_events = 0
        self.states_dropped = 0
        self.elapsed_seconds = 0.
        self.last_retained_evidence_fraction = np.ones(2, dtype=np.float64)
        self.minimum_retained_evidence_fraction = np.ones(2, dtype=np.float64)
        self.peak_states = len(self.states)
        self.peak_transition_states = len(self.states)
        # Before an observation there may be maximum_proposals states; after
        # one there are at most maximum_states. Both counts are hard caps.

    @staticmethod
    def _probabilities(values, count, name):
        p = np.full(count, 1/count, dtype=np.float64) if values is None else np.asarray(values, dtype=np.float64)
        if p.shape != (count,) or not np.isfinite(p).all() or (p < 0).any() or not p.max() > 0:
            raise ValueError(f"invalid {name} probabilities")
        # Normalization happens once for the user-declared PUBLIC prior, never
        # after data-dependent pruning.
        scaled = p / p.max()
        return scaled / scaled.sum()

    def _validate_state(self, state):
        if not isinstance(state, TimingState):
            raise TypeError("timing states must be TimingState objects")
        _integer(state.group, "timing group", 0, (1 << 31)-1)
        _integer(state.delay_index, "global delay index", -(1 << 63), (1 << 63)-1)
        if not math.isfinite(int(state.delay_index)*self.delay_step_seconds):
            raise ValueError("timing source-offset coordinate is not representable")
        _integer(state.rate_index, "rate index", -self.rate_index_bound, self.rate_index_bound)

    def transitions(self, state: TimingState, dt_seconds):
        """Return the public stochastic transition stencil for one state."""
        self._validate_state(state)
        dt = float(dt_seconds)
        if not math.isfinite(dt) or dt < 0:
            raise ValueError("timing elapsed seconds must be finite and nonnegative")
        if dt == 0:
            return ((state, 1.),)
        allowed = self.maximum_acceleration_ppm_per_second*dt/self.rate_step_ppm
        if not math.isfinite(allowed):
            raise ValueError("timing transition interval overflows its rate bound")
        # nextafter keeps exact decimal-boundary choices from losing a public
        # grid neighbor through one floating point rounding operation.
        radius = min(2*self.rate_index_bound, math.floor(math.nextafter(allowed, math.inf)))
        first = max(-self.rate_index_bound, state.rate_index-radius)
        last = min(self.rate_index_bound, state.rate_index+radius)
        rate_probability = 1/(last-first+1)
        edges = []
        for new_rate in range(first, last+1):
            displacement = .5*(state.rate_index+new_rate)*self.rate_step_ppm*1e-6*dt/self.delay_step_seconds
            if not math.isfinite(displacement):
                raise ValueError("timing offset propagation overflows")
            # Split the displacement before adding it to a potentially very
            # large integer offset. Float(global_index)+fraction loses the
            # fractional map after 2**53 and breaks chunk independence.
            whole = math.floor(displacement)
            fraction = displacement-whole
            lower = int(state.delay_index)+whole
            if not -(1 << 63) <= lower < (1 << 63) or (fraction and lower == (1 << 63)-1):
                raise OverflowError("global timing offset counter exhausted")
            if (not math.isfinite(lower*self.delay_step_seconds) or
                    (fraction and not math.isfinite((lower+1)*self.delay_step_seconds))):
                raise ValueError("timing source-offset coordinate is not representable")
            if fraction < 1:
                edges.append((TimingState(state.group, lower, new_rate), rate_probability*(1-fraction)))
            if fraction > 0:
                edges.append((TimingState(state.group, lower+1, new_rate), rate_probability*fraction))
        return tuple(edges)

    def _cap(self, count):
        if len(self.states) <= count:
            return
        before = self.scores()
        rank = _logsumexp(self._log_weights, axis=(0, 1))
        # Public ordering breaks equal-score ties deterministically. Sorting
        # retained columns back into state order keeps caller identities stable.
        chosen = sorted(sorted(range(len(self.states)), key=lambda index: (-rank[index], self.states[index]))[:count])
        self.states_dropped += len(self.states)-len(chosen)
        self.pruning_events += 1
        self.states = tuple(self.states[index] for index in chosen)
        self._log_weights = self._log_weights[:, :, chosen]
        self._prior = self._prior[chosen]
        after = self.scores()
        with np.errstate(under="ignore", invalid="ignore"):
            fraction = np.exp(after-before)
        fraction = np.where(np.isneginf(before), 1., fraction)
        self.last_retained_evidence_fraction = np.minimum(1., fraction)
        self.minimum_retained_evidence_fraction = np.minimum(
            self.minimum_retained_evidence_fraction, self.last_retained_evidence_fraction)

    def predict(self, dt_seconds):
        """Advance physical-time anchors and return this emission's states."""
        dt = float(dt_seconds)
        if not math.isfinite(dt) or dt < 0:
            raise ValueError("timing elapsed seconds must be finite and nonnegative")
        if self._prediction_pending:
            raise ValueError("timing prediction requires observing the preceding segment; use unit emissions for erasures")
        # Every source creates at most 2*(2*rate_index_bound+1) targets; this
        # finite workspace depends on configured caps, never elapsed duration.
        next_elapsed = self.elapsed_seconds+dt
        if not math.isfinite(next_elapsed):
            raise OverflowError("timing elapsed-time counter exhausted")
        merged = {}
        prior = {}
        for index, state in enumerate(self.states):
            for target, probability in self.transitions(state, dt):
                if not probability:
                    continue
                weight = self._log_weights[:, :, index]+math.log(probability)
                merged[target] = weight.copy() if target not in merged else np.logaddexp(merged[target], weight)
                prior[target] = prior.get(target, 0.)+self._prior[index]*probability
        self.states = tuple(sorted(merged))
        self._log_weights = np.stack([merged[state] for state in self.states], axis=-1)
        self._prior = np.array([prior[state] for state in self.states], dtype=np.float64)
        self.peak_transition_states = max(self.peak_transition_states, len(self.states))
        self._cap(self.maximum_proposals)
        self.peak_states = max(self.peak_states, len(self.states))
        self._prediction_pending = True
        self.predictions += 1
        self.elapsed_seconds = next_elapsed
        return self.states

    def observe(self, log_emissions):
        """Add independent segment log-LRs and conservatively cap the beam."""
        if self.observations and not self._prediction_pending:
            raise ValueError("timing observation requires one prediction after the preceding segment")
        emissions = np.asarray(log_emissions, dtype=np.float64)
        if emissions.shape != (self.model_count, 2, len(self.states)) or not np.isfinite(emissions).all():
            raise ValueError("timing emissions must be finite [model,2,state] log likelihood ratios")
        with np.errstate(over="ignore", invalid="ignore"):
            updated = self._log_weights+emissions
        if np.isnan(updated).any() or np.isposinf(updated).any() or (np.isneginf(updated) & np.isfinite(self._log_weights)).any():
            raise FloatingPointError("timing accumulated log likelihood overflow")
        self._log_weights = updated
        self._prediction_pending = False
        self.observations += 1
        self.last_retained_evidence_fraction = np.ones(2, dtype=np.float64)
        self._cap(self.maximum_states)
        return self.report()

    def scores(self):
        """Return each bit's retained normalized log-LR, without a bit prior."""
        return _logsumexp(self._log_weights, axis=(0, 2)).copy()

    def component_log_weights(self):
        """Owned [model,bit,state] weights for normalized posterior transport.

        No prior is reapplied and no retained mass is renormalized.  A caller
        carrying channel posteriors defines a different alternative from this
        module's independent-emission reference and must report that scope.
        """
        return self._log_weights.copy()

    def report(self):
        scores = self.scores()
        state_weight = _logsumexp(self._log_weights, axis=0)  # [bit,state]
        posterior = np.exp(state_weight-scores[:, None])
        maps = []
        for bit in range(2):
            best = int(np.argmax(posterior[bit]))
            state = self.states[best]
            maps.append(dict(group=state.group, delay_index=state.delay_index,
                source_offset_seconds=state.delay_index*self.delay_step_seconds,
                rate_index=state.rate_index, rate_ppm=state.rate_index*self.rate_step_ppm,
                conditional_retained_probability=float(posterior[bit, best])))
        return dict(log_evidence=scores.tolist(), state_count=len(self.states),
            observations=self.observations, predictions=self.predictions,
            elapsed_seconds=self.elapsed_seconds, pruning_events=self.pruning_events,
            states_dropped=self.states_dropped,
            retained_initial_path_probability=float(self._prior.sum()),
            last_retained_evidence_fraction=self.last_retained_evidence_fraction.tolist(),
            minimum_retained_evidence_fraction=self.minimum_retained_evidence_fraction.tolist(),
            peak_states=self.peak_states, peak_transition_states=self.peak_transition_states,
            transition_workspace_state_bound=self.transition_workspace_state_bound,
            delay_index_min=min(state.delay_index for state in self.states),
            delay_index_max=max(state.delay_index for state in self.states),
            posterior_map_display=maps,
            probability_scope="posterior conditional on retained paths; pruning can lose the true path",
            evidence_scope="subprobability path sum; no pruning-mass renormalization or admission decision")
