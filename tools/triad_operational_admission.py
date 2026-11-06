"""Fixed-grid, fixed-mass admission accounting for adaptive triad proposals.

This is a conditional model bound, not certification of an audio/null model.
For fixed designs with normalized Gaussian signal-versus-noise likelihoods,
selected terms retain their ORIGINAL prior mass. Adaptive search cannot increase
their sum above the full nonnegative likelihood mixture. Markov plus a union
bound accounts for repeated attempts without assuming independent windows.

The null must be zero-mean Gaussian with covariance dominated by the declared
receiver covariance, or supply a proven noncentral expectation penalty. Helper
leakage, estimated noise, clipping and arbitrary physical audio are not covered
merely by setting a flag. Each result explicitly keeps that qualification open.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import math

import numpy as np
from scipy.special import logsumexp


@dataclass(frozen=True)
class PublicGrid:
    epochs: tuple
    arrival_lower: float
    arrival_upper: float
    arrival_step: float
    frequency_bound: float
    frequency_step: float
    clock_bound: float
    clock_intervals: int = 8

    def __post_init__(self):
        if not self.epochs or len(set(self.epochs)) != len(self.epochs):
            raise ValueError('nonempty unique epoch grid required')
        if any(isinstance(e, bool) or not isinstance(e, (int, np.integer)) for e in self.epochs):
            raise ValueError('integer epoch coordinates required')
        vals = (self.arrival_lower, self.arrival_upper, self.arrival_step,
                self.frequency_bound, self.frequency_step, self.clock_bound)
        if not all(math.isfinite(x) for x in vals):
            raise ValueError('finite grid geometry required')
        if (self.arrival_lower > self.arrival_upper or self.arrival_step <= 0 or
                self.frequency_step <= 0 or min(self.frequency_bound, self.clock_bound) < 0 or
                not isinstance(self.clock_intervals, int) or self.clock_intervals < 1):
            raise ValueError('invalid public grid extent')

    @property
    def arrival_intervals(self):
        # Uniform endpoint-inclusive grid, with resolved step <= requested step.
        return max(1, math.ceil((self.arrival_upper-self.arrival_lower)/self.arrival_step))

    @property
    def frequency_radius(self):
        return math.ceil(self.frequency_bound/self.frequency_step)

    @property
    def counts(self):
        return (len(self.epochs), self.arrival_intervals+1 if self.arrival_upper>self.arrival_lower else 1,
                2*self.frequency_radius+1, self.clock_intervals+1 if self.clock_bound else 1)

    @property
    def candidate_count(self):
        return math.prod(self.counts)

    def snap(self, coordinate):
        epoch, arrival, frequency, clock = coordinate
        if epoch not in self.epochs or not all(math.isfinite(x) for x in (arrival, frequency, clock)):
            raise ValueError('coordinate outside finite epoch grid')
        if self.arrival_upper == self.arrival_lower:
            a = self.arrival_lower
        else:
            step = (self.arrival_upper-self.arrival_lower)/self.arrival_intervals
            index = min(self.arrival_intervals, max(0, round((arrival-self.arrival_lower)/step)))
            a = self.arrival_lower+index*step
        fi = min(self.frequency_radius, max(-self.frequency_radius, round(frequency/self.frequency_step)))
        if self.clock_bound:
            qi = min(self.clock_intervals, max(0, round((clock+self.clock_bound)*self.clock_intervals/(2*self.clock_bound))))
            q = -self.clock_bound+qi*(2*self.clock_bound/self.clock_intervals)
        else:
            q = 0.
        return (int(epoch), float(a), float(fi*self.frequency_step), float(q))

    def validate_selected(self, coordinates):
        rows = [tuple(c) for c in coordinates]
        if not rows or len(set(rows)) != len(rows):
            raise ValueError('selected coordinates must be nonempty and unique')
        for row in rows:
            snapped = self.snap(row)
            if row[0] != snapped[0] or any(abs(x-y)>1e-10 for x,y in zip(row[1:],snapped[1:])):
                raise ValueError('selected coordinate is not a declared grid atom')
        return len(rows)

    def identity(self):
        return hashlib.sha256(json.dumps(self.__dict__,sort_keys=True).encode()).hexdigest()


def operational_decision(bit_log_evidence, coordinates, grid, *,
                         alpha_per_day=1e-4, minimum_attempt_spacing_seconds=.4,
                         duration_alternatives=1, log_null_expectation_penalty=0.,
                         zero_h0_verified=False):
    """Correct selected-candidate averages, then apply an operational budget.

    Input is the two normalized logLRs already averaged equally across selected
    grid coordinates and a FIXED public model mixture; H0 is exactly zero signal.
    Models and bits retain normalized priors, so are not counted twice in N.
    An omitted candidate contributes zero mass, not reallocated prior weight.
    The caller enforces actual schedule/rate and the declared null assumptions.
    """
    evidence = np.asarray(bit_log_evidence, dtype=float)
    if evidence.shape != (2,) or not np.isfinite(evidence).all():
        raise ValueError('two finite DATA-versus-H0 log evidences required')
    if not zero_h0_verified:
        raise ValueError('fixed zero-signal H0 likelihood must be verified')
    if (not math.isfinite(alpha_per_day) or not 0<alpha_per_day<1 or
            not math.isfinite(minimum_attempt_spacing_seconds) or minimum_attempt_spacing_seconds<=0 or
            not isinstance(duration_alternatives,int) or duration_alternatives<1 or
            not math.isfinite(log_null_expectation_penalty) or log_null_expectation_penalty<0):
        raise ValueError('invalid operational error budget')
    selected = grid.validate_selected(coordinates)
    attempts = math.ceil(86400/minimum_attempt_spacing_seconds)+1
    log_mass = math.log(selected)-math.log(grid.candidate_count)
    log_e = float(logsumexp(evidence)-math.log(2)+log_mass-log_null_expectation_penalty)
    threshold = math.log(attempts*duration_alternatives/alpha_per_day)
    return dict(schema='triad-operational-grid-budget-v1',
        candidate_bit=int(np.argmax(evidence)), passes_conditional_bound=log_e>=threshold,
        log_e_lower_bound=log_e, log_e_threshold=threshold,
        selected_candidates=selected, full_grid_candidates=grid.candidate_count,
        grid_counts=list(grid.counts), grid_identity=grid.identity(),
        selected_prior_mass=selected/grid.candidate_count,
        attempts_per_24h_bound=attempts,duration_alternatives=duration_alternatives,
        alpha_per_24h=alpha_per_day,log_null_expectation_penalty=log_null_expectation_penalty,
        scope='Conditional zero-mean covariance-dominated Gaussian null; adaptive subset with fixed public prior mass; union bound over declared attempt schedule and durations.',
        physical_null_qualified=False,
        exclusions=['Unbounded Helper residual mean or non-Gaussian sidebands',
                    'Unknown or adaptively underestimated noise covariance',
                    'Unaccounted model, duration or acquisition attempts',
                    'Continuously optimized coordinates outside the frozen grid',
                    'Physical audio noise/ADC without a separately justified null envelope'])
