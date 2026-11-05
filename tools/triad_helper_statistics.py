#!/usr/bin/env python3
"""Reporting-only stratified uncertainty for fixed-balance Helper experiments.

Alternating fixed source bits are two strata, not identically distributed
Bernoulli trials. These intervals target an equally weighted population of bit
0 and bit 1. They require independent representative draws within each stratum,
conditional on a frozen admission gate; they do not establish operational daily
false-admission rates or simultaneous coverage across all outcome categories.
"""
from __future__ import annotations

import hashlib
import json
import math
from numbers import Integral, Real

from scipy.stats import beta

CATEGORIES = ('correct_admitted', 'wrong_admitted', 'missed', 'raw_bit_errors')


def _count(value, name):
    if isinstance(value, bool) or not isinstance(value, Integral) or value < 0:
        raise ValueError(f'{name} must be a nonnegative integer')
    return int(value)


def _confidence(value):
    if isinstance(value, bool) or not isinstance(value, Real) or not math.isfinite(value) or not 0 < value < 1:
        raise ValueError('confidence must be finite and strictly between zero and one')
    return float(value)


def binomial_interval(successes, trials, confidence=.95):
    """Exact CP intervals within one stratum; absent evidence is explicitly [0,1]."""
    successes = _count(successes, 'successes')
    trials = _count(trials, 'trials')
    confidence = _confidence(confidence)
    if successes > trials:
        raise ValueError('successes cannot exceed trials')
    if not trials:
        return dict(count=successes, trials=trials, fraction=None, confidence=confidence,
                    two_sided=[0., 1.], one_sided_upper=1., qualified=False,
                    qualification='No observations in this stratum.')
    tail = 1 - confidence
    low = 0. if successes == 0 else float(beta.ppf(tail / 2, successes, trials - successes + 1))
    high = 1. if successes == trials else float(beta.ppf(1 - tail / 2, successes + 1, trials - successes))
    upper = 1. if successes == trials else float(beta.ppf(confidence, successes + 1, trials - successes))
    return dict(count=successes, trials=trials, fraction=successes / trials,
                confidence=confidence, two_sided=[low, high], one_sided_upper=upper,
                qualified=True,
                qualification='Exact binomial interval conditional on independent representative '
                              'within-stratum draws and the frozen gate.')


def balanced_signal_statistics(per_bit_counts, per_bit_trials, confidence=.95):
    """Return per-bit CP diagnostics and conservative balanced-population bounds.

    Mappings use integer bit keys 0 and 1. An omitted bit is a zero-trial stratum.
    Each present count mapping contains every CATEGORIES entry. The first three
    categories partition signal outcomes; raw-bit errors also include rejected
    signals whose raw bit choice was wrong.

    At confidence .95 each balanced bound averages the two .975 CP bounds.
    Bonferroni supplies at least .95 coverage for a *single named category* even
    without assuming independence between the two strata. Equal .5 weights are
    retained when trial counts differ. If either stratum is absent, the headline
    balanced interval is conservatively [0,1] and explicitly unqualified.
    """
    confidence = _confidence(confidence)
    for values in (per_bit_counts, per_bit_trials):
        if any(isinstance(bit, bool) or not isinstance(bit, Integral) or bit not in (0, 1)
               for bit in values):
            raise ValueError('stratum keys must be integer bits 0 or 1')
    counts, trials = {}, {}
    for bit in (0, 1):
        trials[bit] = _count(per_bit_trials.get(bit, 0), f'bit {bit} trials')
        given = per_bit_counts.get(bit, {name: 0 for name in CATEGORIES})
        if set(given) != set(CATEGORIES):
            raise ValueError(f'bit {bit} counts must contain every signal outcome category')
        counts[bit] = {name: _count(given[name], f'bit {bit} {name}') for name in CATEGORIES}
        if any(counts[bit][name] > trials[bit] for name in CATEGORIES):
            raise ValueError('category count exceeds stratum trials')
        if sum(counts[bit][name] for name in CATEGORIES[:3]) != trials[bit]:
            raise ValueError('correct/wrong admissions and misses must partition signal trials')
        if not counts[bit]['wrong_admitted'] <= counts[bit]['raw_bit_errors'] <= (
                counts[bit]['wrong_admitted'] + counts[bit]['missed']):
            raise ValueError('raw-bit errors must include wrong admissions and exclude correct admissions')

    component_confidence = 1 - (1 - confidence) / 2
    qualified = all(trials.values())
    per_bit = {str(bit): dict(trials=trials[bit], counts=counts[bit],
        intervals={name: binomial_interval(counts[bit][name], trials[bit], confidence)
                   for name in CATEGORIES}) for bit in (0, 1)}
    balanced = {}
    for name in CATEGORIES:
        components = [binomial_interval(counts[bit][name], trials[bit], component_confidence)
                      for bit in (0, 1)]
        balanced[name] = dict(
            confidence=confidence, weights=[.5, .5],
            fraction=(sum(counts[bit][name] / trials[bit] for bit in (0, 1)) / 2
                      if qualified else None),
            two_sided=([sum(c['two_sided'][side] for c in components) / 2 for side in (0, 1)]
                       if qualified else [0., 1.]),
            one_sided_upper=(sum(c['one_sided_upper'] for c in components) / 2 if qualified else 1.),
            component_confidence=component_confidence,
            component_bounds=components, qualified=qualified,
            method='Average of per-bit Bonferroni Clopper-Pearson bounds; fixed equal population weights.',
            qualification=('Conditional on independent representative within-bit draws and the frozen gate.'
                           if qualified else 'At least one source-bit stratum is absent; balanced uncertainty unqualified.'))
    totals = {name: sum(counts[bit][name] for bit in (0, 1)) for name in CATEGORIES}
    total_trials = sum(trials.values())
    return dict(schema='triad-helper-balanced-signal-statistics-v1', counts=totals,
                signal_trials=total_trials, per_bit=per_bit, balanced_population=balanced,
                pooled_observed_fractions={name: totals[name] / total_trials if total_trials else None
                                           for name in CATEGORIES},
                uncertainty_scope='Fixed source-bit strata; balanced-population weights are .5 each. '
                    'Pooled fractions are descriptive only, with no pooled iid-binomial interval. '
                    'Coverage is per named category, not simultaneous across categories or receiver arms.')


def signal_outcome_statistics(rows, arm, confidence=.95):
    """Adapt retained campaign rows without receiver work or altered decisions."""
    counts = {bit: {name: 0 for name in CATEGORIES} for bit in (0, 1)}
    trials = {0: 0, 1: 0}
    for row in rows:
        if row['trial']['phase'] != 'signal':
            continue
        bit = row['evaluator']['source_bit']
        decision = row['decisions'][arm]
        decoded = decision['bit']
        if (isinstance(bit, bool) or not isinstance(bit, Integral) or bit not in (0, 1) or
                isinstance(decoded, bool) or not isinstance(decoded, Integral) or decoded not in (0, 1)):
            raise ValueError('source and decoded bits must be integer 0 or 1')
        accepted = decision['accepted']
        if not isinstance(accepted, bool):
            raise ValueError('accepted must be a boolean')
        trials[bit] += 1
        wrong = decoded != bit
        counts[bit]['raw_bit_errors'] += wrong
        counts[bit]['missed' if not accepted else ('wrong_admitted' if wrong else 'correct_admitted')] += 1
    return balanced_signal_statistics(counts, trials, confidence)


def calibration_condition_id(procedure_id, helper_enabled):
    """Bind gate reuse to the complete receiver procedure and Helper on/off null."""
    if not isinstance(procedure_id, str) or not procedure_id.strip():
        raise ValueError('procedure_id must be a nonempty string')
    if not isinstance(helper_enabled, bool):
        raise ValueError('helper_enabled must be a boolean')
    identity = dict(schema='triad-helper-calibration-condition-v1',
                    procedure_id=procedure_id, helper_enabled=helper_enabled)
    return hashlib.sha256(json.dumps(identity, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
