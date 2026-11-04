#!/usr/bin/env python3
"""Controls for per-test, one-sided, decision-stopping performance inference.

This is a timing-decision simulation. It is not a receiver benchmark or a bound
on future CI errors. The historical precision-stopping controls remain separate.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict
import json
import math
import os
from pathlib import Path
import sys
import time

import numpy as np

from triad_perf_adaptive import AdaptivePolicy, regression_profile, summarize
from triad_perf_decision_controls import (NoiseModel, binomial_interval, conditional_decision,
    digest, digest_file, environment_screen, read_residuals, sample_logs)

SCHEMA = 'triad-performance-per-test-decision-controls-v2'
FACTORS = (1., 1.29, 1.30, 1.31, 1.6)
POLICY = AdaptivePolicy(alpha=.05, slowdown_threshold=1.30, stopping_rule='decision')


def policy_for_profile(name):
    """Resolve the maintained profile; keep the original default strict policy."""
    settings = regression_profile(name)
    return POLICY if name == 'strict' else AdaptivePolicy(stopping_rule='decision', **settings)


def default_factors(policy):
    threshold = policy.slowdown_threshold
    return [1., round(threshold - .01, 12), threshold,
            round(threshold + .01, 12), round(threshold + .30, 12)]


def decide(reference, candidate, policy=POLICY):
    """Call the maintained gate only at its fixed looks, stopping when it does."""
    if policy.stopping_rule != 'decision':
        raise ValueError('this control requires the decision stopping rule')
    if len(reference) < policy.looks[-1] or len(candidate) < policy.looks[-1]:
        raise ValueError('provide the entire predeclared maximum observation bank')
    for n in policy.looks:
        row = summarize(reference[:n], candidate[:n], policy, model_validation='direct_full')
        if row['stop_reason'] != 'continue':
            return row
    raise AssertionError('last look must stop')


def simulate_factor(factor, *, trials, seed, model=NoiseModel(), empirical=None, policy=POLICY):
    if not math.isfinite(factor) or factor <= 0 or isinstance(trials, bool) or trials < 1:
        raise ValueError('finite positive factor and positive trial count required')
    rng = np.random.default_rng(seed)
    counts = {'pass': 0, 'performance_regression': 0, 'inconclusive': 0,
              'false_pass': 0, 'false_regression': 0, 'timing_precision_reached': 0,
              'decision_before_timing_precision': 0}
    looks = {str(n): 0 for n in policy.looks}
    for _ in range(trials):
        logs = sample_logs(rng, policy.looks[-1], model, empirical)
        reference, candidate = np.exp(logs[:, 0]), np.exp(logs[:, 1]) * factor
        row = decide(reference, candidate, policy)
        gate = row['performance_gate']
        counts[gate] += 1
        counts['false_pass'] += int(gate == 'pass' and factor > policy.slowdown_threshold)
        counts['false_regression'] += int(gate == 'performance_regression' and factor <= policy.slowdown_threshold)
        counts['timing_precision_reached'] += int(row['timing_precision_reached'])
        counts['decision_before_timing_precision'] += int(gate != 'inconclusive' and not row['timing_precision_reached'])
        looks[str(row['sample_count'])] += 1
    return {'true_geometric_mean_ratio': factor, 'seed': seed, 'trials': trials,
            'outcomes': {key: binomial_interval(value, trials) for key, value in counts.items()},
            'stopping_sample_count': looks,
            'mean_sample_count': sum(int(k) * v for k, v in looks.items()) / trials,
            'false_pass_applicable': factor > policy.slowdown_threshold,
            'false_regression_applicable': factor <= policy.slowdown_threshold,
            'scope': 'one test at a time; Monte Carlo probabilities conditional on the stated timing generator'}


def deterministic_scope_controls(policy=POLICY):
    """Actual decision helpers expose timer/model/environment blind spots."""
    n = policy.looks[-1]
    ref = np.exp(np.linspace(-.002, .002, n))
    # Cost is split before applying the change: 80% receiver, 20% other work.
    # The injected external stage moves the full runtime past this profile's
    # threshold. The strict policy retains the original 60% extra cost.
    clear_failure = round(policy.slowdown_threshold + .30, 12)
    external_fraction = clear_failure - 1.
    receiver_ref = .8 * ref
    outside_total = receiver_ref + .2 * ref + external_fraction * ref
    external = decide(ref, outside_total, policy)
    incorrectly_receiver_only = decide(receiver_ref, receiver_ref, policy)
    full_only_abbreviated = decide(ref, ref, policy)
    full_only_endpoint = conditional_decision(ref.tolist(), ref.tolist(),
        {'reference': 1., 'candidate': clear_failure}, policy)
    changed = conditional_decision(ref.tolist(), ref.tolist(),
        {'reference': 1., 'candidate': 1.}, policy, changed_regime=True)
    environment = decide(ref * 1.6, ref * 1.6, policy)
    return {
        'outside_receiver': {'complete_timer_gate': external['performance_gate'],
            'incorrect_receiver_only_gate': incorrectly_receiver_only['performance_gate'],
            'nominal_receiver_fraction': .8, 'injected_external_fraction': external_fraction,
            'scope': 'synthetic stage costs demonstrate why complete timer boundaries matter'},
        'full_size_only': {'abbreviated_gate': full_only_abbreviated['performance_gate'],
            'full_target_ratio': clear_failure, 'sentinel': full_only_endpoint['sentinel_status'],
            'calibration_blocking': full_only_endpoint['calibration_blocking'],
            'scope': 'unchanged abbreviation alone passes incorrectly for the full target; separate full endpoint is required'},
        'changed_regime': {'applicability_accepted': changed['applicability_accepted'],
            'sentinel': changed['sentinel_status'], 'scope': 'actual applicability validator, not measured slowdown'},
        'common_environment': {'paired_gate': environment['performance_gate'],
            'absolute_factor': 1.6, 'hardware_screen': environment_screen(1.6),
            'scope': 'relative source comparison cannot establish absolute hardware health'},
        'not_actual_receiver_or_hardware_runs': True,
    }


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--trials', type=int, default=1000)
    p.add_argument('--seed', type=int, default=1913041)
    p.add_argument('--regression-profile', choices=('normal', 'ci', 'strict'), default='strict',
                   help='maintained directional decision policy; strict preserves the original default')
    p.add_argument('--factor', action='append', type=float)
    p.add_argument('--noise', choices=('lognormal', 'empirical-iid', 'empirical-block'), default='lognormal')
    p.add_argument('--log-sigma', type=float, default=.04)
    p.add_argument('--paired-correlation', type=float, default=.6)
    p.add_argument('--residual-report', type=Path)
    p.add_argument('--case', help='one required named case for empirical jitter; no whole-suite error claim')
    args = p.parse_args(argv)
    if not 1 <= args.trials <= 20000:
        p.error('trials must be in 1..20000')
    policy = policy_for_profile(args.regression_profile)
    factors = default_factors(policy) if args.factor is None else args.factor
    if any(not math.isfinite(f) or f <= 0 for f in factors):
        p.error('factors must be finite and positive')
    if args.output.exists() or args.output.with_suffix('.plan.json').exists():
        p.error('output and plan must be fresh')
    model = NoiseModel(args.noise, args.log_sigma, args.paired_correlation)
    empirical, provenance = None, None
    if model.kind != 'lognormal':
        if not args.case or not args.residual_report:
            p.error('empirical jitter requires --residual-report and --case')
        rows, provenance = read_residuals(args.residual_report)
        selected = [row for row in rows if row['id'] == args.case]
        if len(selected) != 1:
            p.error('case must identify exactly one measured timing family')
        empirical = selected[0]['logs']
        provenance['selected_case'] = args.case
    paths = [Path(__file__), *[Path(__file__).with_name(name) for name in
             ('triad_perf_adaptive.py', 'triad_perf_decision_controls.py',
              'triad_perf_conditional.py', 'triad-performance.py')]]
    sources = {path.name: digest_file(path) for path in paths}
    critical_names = {Path(__file__).name, 'triad_perf_adaptive.py', 'triad_perf_decision_controls.py'}
    plan = {'schema': SCHEMA, 'policy': asdict(policy), 'noise': asdict(model),
        'regression_profile': args.regression_profile,
        'directional_error_budgets': {'pass': policy.pass_error_budget,
                                     'failure': policy.failure_error_budget},
        'trials_per_factor': args.trials, 'factors': factors, 'seed': args.seed,
        'source_sha256': sources, 'empirical_provenance': provenance,
        'runtime': {'python': sys.version, 'numpy': np.__version__, 'scipy': __import__('scipy').__version__},
        'gate_contract': 'per test, separately declared one-sided pass/failure error budgets across fixed looks 5,9,17,33; no whole-suite multiplicity adjustment',
        'timing_precision_is_not_a_stop_requirement': True,
        'mean_runtime_estimand': 'geometric mean of paired complete-runtime ratios',
        'interval_scope': '95% pointwise Clopper-Pearson intervals over independent simulated test replications, not simultaneous across factors/outcomes',
        'assumptions': ['stationary paired lognormal outer runtimes for the parametric generator',
            'the empirical generator is conditional on a finite measured jitter sample; its uncertainty is not a future hardware bound',
            'block resampling preserves short serial dependence and therefore stress-tests, rather than proves, the independent-time assumption',
            'model applicability and hardware health are separate; simulated direct-full timing has no abbreviation model error'],
        'qualification_scope': 'decision logic simulation only; no receiver speed, mixed-CI-dispatch, or future error-rate qualification'}
    plan['plan_sha256'] = digest(plan)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.with_suffix('.plan.json').open('x') as f:
        json.dump(plan, f, indent=2, allow_nan=False)
    result = {'schema': SCHEMA, 'plan': plan, 'status': 'running', 'results': [],
              'pending_factors': factors.copy(), 'scope_controls': None, 'exit_code': None}
    with args.output.open('x') as f:
        json.dump(result, f, indent=2, allow_nan=False)
    started = time.perf_counter()

    def checkpoint():
        result['elapsed_seconds'] = time.perf_counter() - started
        temp = args.output.with_suffix('.writing')
        temp.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
        os.replace(temp, args.output)

    try:
        for i, factor in enumerate(factors):
            row = simulate_factor(factor, trials=args.trials, seed=args.seed + 1000 * i,
                                  model=model, empirical=empirical, policy=policy)
            result['results'].append(row)
            result['pending_factors'] = factors[i + 1:]
            current = {path.name: digest_file(path) for path in paths}
            if any(sources[name] != current[name] for name in critical_names):
                raise RuntimeError('timing-decision or sampling source changed during the Monte Carlo run')
            result['monte_carlo_sources_verified'] = True
            result['unrelated_source_changes'] = {name: {'before': sources[name], 'after': current[name]}
                for name in sources if name not in critical_names and sources[name] != current[name]}
            checkpoint()
        scope_before = {path.name: digest_file(path) for path in paths}
        result['scope_controls'] = deterministic_scope_controls(policy)
        scope_after = {path.name: digest_file(path) for path in paths}
        result['scope_control_sources'] = {'before': scope_before, 'after': scope_after,
                                           'stable': scope_before == scope_after}
        result['status'] = 'complete'
        result['exit_code'] = 0
    except Exception as error:
        result['status'] = 'failed'
        result['exit_code'] = 2
        result['error'] = {'type': type(error).__name__, 'message': str(error)}
    checkpoint()
    return result['exit_code']


if __name__ == '__main__':
    raise SystemExit(main())
