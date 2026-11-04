#!/usr/bin/env python3
"""Prospective decision controls for the isolated triad performance suite.

This is a simulation of the *timing decisions*, not a receiver/channel test.
It calls the maintained decision helpers at their declared looks. Its binomial
intervals describe the specified Monte Carlo generator only. Historical timing
resampling does not turn a finite observed jitter distribution into a future
hardware guarantee.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import hashlib
import importlib.util
from functools import lru_cache
import json
import math
import os
from pathlib import Path
import sys
import time
from typing import Callable

import numpy as np
from scipy.stats import beta

from triad_perf_adaptive import AdaptivePolicy, summarize
from triad_perf_conditional import (build_conditional_contract, check_conditional_sentinel,
                                    conditional_comparison, conditional_model_log_bounds)

SCHEMA = 'triad-performance-decision-controls-v1'
FACTORS = (1., 1.2, 1.29, 1.31, 1.4, 1.6)
ROOT = Path(__file__).resolve().parents[1]


def digest_file(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
                                    allow_nan=False).encode()).hexdigest()


@lru_cache(maxsize=1)
def _runner():
    path = Path(__file__).with_name('triad-performance.py')
    spec = importlib.util.spec_from_file_location('_triad_decision_controls_runner', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def binomial_interval(successes, trials, confidence=.95):
    """Exact, pointwise Clopper-Pearson interval over independent campaigns."""
    if (isinstance(successes, bool) or isinstance(trials, bool) or
            not isinstance(successes, int) or not isinstance(trials, int) or
            not 0 <= successes <= trials or trials < 1 or not 0 < confidence < 1):
        raise ValueError('invalid binomial counts or confidence')
    alpha = 1. - confidence
    low = 0. if successes == 0 else float(beta.ppf(alpha / 2, successes, trials - successes + 1))
    high = 1. if successes == trials else float(beta.ppf(1 - alpha / 2, successes + 1, trials - successes))
    return {'count': successes, 'campaigns': trials, 'fraction': successes / trials,
            'interval': [low, high], 'confidence': confidence,
            'interval_scope': 'pointwise exact binomial interval across independently seeded simulated campaigns; not simultaneous across reported outcomes'}


def read_residuals(path):
    """Read actual outer full-time predictions; preserve paired time ordering.

    Separately center each source's log times: this tests an imposed slowdown,
    not the historical implementation's measured candidate/reference bias.
    Inner invocations and endpoint holdouts are never extra independent rows.
    """
    path = Path(path).resolve(strict=True)
    report = json.loads(path.read_text())
    if (report.get('status') not in ('passed', 'warning', 'failed') or
            report.get('exit_code') not in (0, 2) or
            report.get('complete_required_coverage') is not True or
            not isinstance(report.get('elapsed_seconds'), (float, int)) or
            not math.isfinite(report['elapsed_seconds']) or report['elapsed_seconds'] <= 0 or
            not report.get('harness_sha256') or not report.get('candidate')):
        raise ValueError('residual report must be a completed, source-bound backend report')
    rows = []
    for case in report.get('cases', []):
        values = case.get('predicted_full_seconds') or case.get('raw_full_seconds')
        if values is None and isinstance(case.get('raw', {}).get('reference'), dict):
            values = {}
            for source in ('reference', 'candidate'):
                raw = case['raw'][source]
                values[source] = (raw.get('direct') or
                                  [row['predicted_full_seconds'] for row in raw.get('models', [])])
        if not values:
            raise ValueError('no outer runtime samples for ' + case.get('id', '<unnamed>'))
        a = np.asarray([values['reference'], values['candidate']], dtype=np.float64).T
        if a.ndim != 2 or a.shape[1] != 2 or a.shape[0] < 5 or not np.isfinite(a).all() or (a <= 0).any():
            raise ValueError('invalid outer runtime samples for ' + case.get('id', '<unnamed>'))
        logs = np.log(a)
        centered = logs - logs.mean(axis=0)
        rows.append({'id': case['id'], 'logs': centered,
                     'outer_count': len(a), 'source_geometric_seconds': np.exp(logs.mean(axis=0)).tolist(),
                     'source_identity': case.get('identities') or case.get('source_bound_metadata'),
                     'measured_model_valid': case.get('prediction_valid')})
    if not rows:
        raise ValueError('no backend timing cases found')
    return rows, {'path': str(path), 'sha256': digest_file(path),
                  'harness_sha256': report.get('harness_sha256'),
                  'candidate': report.get('candidate'), 'reference': report.get('reference'),
                  'original_status': report['status'], 'original_exit_code': report['exit_code'],
                  'case_ids': [r['id'] for r in rows],
                  'meaning': 'finite empirical timing jitter only, including unresolved historical cases; no model qualification is imported'}


@dataclass(frozen=True)
class NoiseModel:
    kind: str = 'lognormal'
    log_sigma: float = .04
    paired_correlation: float = .6
    block_length: int = 2

    def __post_init__(self):
        if self.kind not in ('lognormal', 'empirical-iid', 'empirical-block'):
            raise ValueError('unknown noise model')
        if not math.isfinite(self.log_sigma) or self.log_sigma < 0:
            raise ValueError('log_sigma must be finite and nonnegative')
        if not math.isfinite(self.paired_correlation) or not -1 <= self.paired_correlation <= 1:
            raise ValueError('paired_correlation must lie in [-1,1]')
        if isinstance(self.block_length, bool) or not isinstance(self.block_length, int) or self.block_length < 1:
            raise ValueError('block_length must be a positive integer')


def sample_logs(rng, count, model, empirical=None):
    if model.kind == 'lognormal':
        first, independent = rng.standard_normal((2, count))
        second = model.paired_correlation * first + math.sqrt(1 - model.paired_correlation ** 2) * independent
        return model.log_sigma * np.column_stack((first, second))
    if empirical is None or len(empirical) < model.block_length:
        raise ValueError('empirical model needs enough paired observations')
    a = np.asarray(empirical, dtype=float)
    if model.kind == 'empirical-iid':
        return a[rng.integers(0, len(a), count)]
    # Non-overlapping adjacent blocks preserve the measured ABBA pair ordering.
    # Drop the incomplete tail instead of wrapping the final pair onto the first.
    blocks = len(a) // model.block_length
    a = a[:blocks * model.block_length]
    a = a - a.mean(axis=0)  # discarded odd tail must not introduce imposed null bias
    chosen = rng.integers(0, blocks, math.ceil(count / model.block_length))
    return np.concatenate([a[i * model.block_length:(i + 1) * model.block_length]
                           for i in chosen], axis=0)[:count]


def policy_decision(reference, candidate, policy, *, conditional=False):
    """Run actual fixed-look logic; no observations between declared looks."""
    for n in policy.looks:
        result = summarize(reference[:n], candidate[:n], policy,
                           model_validation='unvalidated' if conditional else 'direct_full')
        if result['stop_reason'] != 'continue':
            return result
    raise AssertionError('the last declared look must stop')


def conditional_decision(reference, candidate, actual, policy, *, changed_regime=False):
    timing = policy_decision(reference, candidate, policy, conditional=True)
    n = timing['sample_count']
    # This deliberately idealized historical model is a frozen control fixture,
    # NOT a substitute for experimentally qualified full-workload anchors.
    history = {name: [0.] * 9 for name in ('reference', 'candidate', 'paired')}
    plan = build_conditional_contract(history, {'reference': reference[:n], 'candidate': candidate[:n]},
        applicability_key='synthetic-fixed-regime', historical_anchor_ids=[f'control-history-{i}' for i in range(9)],
        training_pair_ids=[f'control-training-{i}' for i in range(n)],
        timing_confidence=timing['policy']['interval_confidence_per_look'])
    sentinel = check_conditional_sentinel(plan, actual,
        applicability_key='synthetic-changed-regime' if changed_regime else 'synthetic-fixed-regime',
        sentinel_id='independent-control-endpoint')
    bounds = conditional_model_log_bounds(plan, sentinel)
    compared = conditional_comparison(timing['paired_candidate_over_reference']['timing_mean_confidence_interval_seconds'],
                                     bounds, slowdown_threshold=policy.slowdown_threshold)
    guard = _runner().endpoint_calibration_guard({'anchors': {'conditional_check': sentinel}})
    return {'timing': timing, 'gate': compared['gate'], 'sentinel_status': sentinel['status'],
            'calibration_blocking': guard['blocking'], 'plan_sha256': plan['plan_sha256'],
            'applicability_accepted': sentinel['conditional_performance_pass_eligible']}


def _rates(events, trials, *, complete=True):
    rows = {name: (binomial_interval(int(count), trials) if trials else
                  {'count': 0, 'campaigns': 0, 'fraction': None, 'interval': None})
            for name, count in events.items()}
    for row in rows.values():
        row['fixed_campaign_count_completed'] = complete
        row['nominal_binomial_coverage_claim'] = bool(complete and trials)
        if not complete:
            row['interval_scope'] = 'incomplete wall-budget-stopped diagnostic; fixed-n interval is descriptive only because stopping may depend on outcomes'
    return rows


def run_control(model, *, factor, mode, campaigns, policy, seed, empirical_rows=None, deadline=None):
    """A whole independently seeded multi-case campaign is the binomial unit."""
    if mode not in ('direct', 'conditional', 'full-only', 'outside-score', 'environment', 'regime-change'):
        raise ValueError('unknown control mode')
    if not math.isfinite(factor) or factor < 1 or campaigns < 1:
        raise ValueError('invalid control factor or campaign count')
    if empirical_rows is not None and len(empirical_rows) != policy.family_count:
        raise ValueError('empirical cases must exactly match declared family count')
    per_case = [{k: 0 for k in ('performance_failure', 'calibration_failure', 'inconclusive',
                               'precision_reached', 'applicability_rejected')} for _ in range(policy.family_count)]
    any_events = {k: 0 for k in per_case[0]}
    counts = []
    roots = np.random.SeedSequence(seed).spawn(campaigns)
    completed = 0
    for campaign_seed in roots:
        if deadline is not None and time.perf_counter() >= deadline:
            break
        rng = np.random.default_rng(campaign_seed)
        flags = {k: False for k in any_events}
        for ci in range(policy.family_count):
            empirical = empirical_rows[ci]['logs'] if empirical_rows is not None else None
            # Independent endpoint draw: it is not a retained training sample.
            logs = sample_logs(rng, policy.looks[-1], model, empirical)
            endpoint = sample_logs(rng, 1, model, empirical)[0]
            ref, cand = np.exp(logs[:, 0]), np.exp(logs[:, 1])
            if mode == 'outside-score':
                cand = cand + (factor - 1.)  # delay outside receiver; units are nominal full runtime
            elif mode not in ('full-only', 'regime-change'):
                cand *= factor
            if mode == 'environment':
                ref *= factor
            if mode in ('conditional', 'full-only', 'regime-change'):
                end_factor = factor if mode != 'regime-change' else 1.
                actual = {'reference': math.exp(endpoint[0]), 'candidate': math.exp(endpoint[1]) * end_factor}
                result = conditional_decision(ref.tolist(), cand.tolist(), actual, policy,
                                              changed_regime=mode == 'regime-change')
                timing = result['timing']
                gate = result['gate']
                calibration = result['calibration_blocking']
                rejected = not result['applicability_accepted']
            else:
                timing = policy_decision(ref, cand, policy)
                gate, calibration, rejected = timing['performance_gate'], False, False
            values = {'performance_failure': gate == 'performance_regression',
                      'calibration_failure': calibration,
                      'inconclusive': gate in ('inconclusive', 'unvalidated'),
                      'precision_reached': timing['timing_precision_reached'],
                      'applicability_rejected': rejected}
            counts.append(timing['sample_count'])
            for name, flag in values.items():
                per_case[ci][name] += int(flag)
                flags[name] |= flag
        for name, flag in flags.items():
            any_events[name] += int(flag)
        completed += 1
    complete = completed == campaigns
    return {'mode': mode, 'factor': factor, 'seed': seed, 'campaigns': completed,
            'requested_campaigns': campaigns, 'status': 'complete' if complete else 'budget_exhausted',
            'any_case': _rates(any_events, completed, complete=complete),
            'per_case': [{'id': empirical_rows[i]['id'] if empirical_rows else f'synthetic-family-{i}',
                          **_rates(row, completed, complete=complete)} for i, row in enumerate(per_case)],
            'outer_sample_count_mean': float(np.mean(counts)) if counts else None,
            'outer_sample_count_max': max(counts) if counts else None,
            'event_interpretation': ('the additive outside-score factor is relative to nominal unit runtime; nonlinear jitter changes its exact geometric mean, so counts alone are not near-threshold false-failure calibration' if mode == 'outside-score' else
                'environment changes both sources equally: a relative performance failure is false under this imposed control' if mode == 'environment' else
                'changed regime tests refusal, not a runtime slowdown' if mode == 'regime-change' else
                'full-only uses unchanged abbreviation and altered full endpoints: calibration failure is distinct from confirmed mean regression' if mode == 'full-only' else
                'performance failure at factor <= threshold is a false failure under the specified timing generator; above threshold it is detection power'),
            'full_only_limit': 'a full-only change cannot be discovered by unchanged abbreviated times; only the full endpoint/applicability paths respond' if mode == 'full-only' else None}


def environment_screen(factor):
    """Exercise the actual raw health classifier using explicitly synthetic data."""
    runner = _runner()
    hardware = {'requested_threads': 2, 'cpu': {'model name': 'synthetic control CPU',
                    'affinity_cpu_count': 2, 'affinity_cpus': [0, 2]}, 'device': {}}
    baseline = {'device': 'cpu', 'hardware': hardware,
                'cases': {'control-kernel': {'status': 'complete', 'operations_per_second': 1000.,
                                            'shape': [32, 32], 'process_cpu_cores_used': 2.}}}
    current = json.loads(json.dumps(baseline))
    current['cases']['control-kernel']['operations_per_second'] /= factor
    import tempfile
    with tempfile.TemporaryDirectory(prefix='triad-control-health-') as directory:
        path = Path(directory) / 'synthetic-reference.json'
        path.write_text(json.dumps(baseline))
        result = runner.hardware_health(current, path)
    return {'input_kind': 'synthetic compatible hardware profile', 'factor': factor,
            'result': result, 'actual_hardware_test': False,
            'limitation': 'the raw single-block screen has no confidence-qualified false-failure guarantee'}


@dataclass(frozen=True)
class DelayRule:
    multiplier: float = 1.
    extra_seconds: float = 0.

    def __post_init__(self):
        if not math.isfinite(self.multiplier) or self.multiplier < 1 or not math.isfinite(self.extra_seconds) or self.extra_seconds < 0:
            raise ValueError('delay rules require finite multiplier >=1 and extra_seconds >=0')


class DelayLedger:
    """Disclosed bounded stage-delay injection around a real caller's operation.

    The caller's outer timer must surround this wrapper. Returned receiver
    values are unmodified. Sleeping tests a timing decision, not changed DSP
    arithmetic, utilization, cache or thermal behavior. One serial worker owns
    a ledger; it is deliberately not a concurrent timing scheduler.
    """
    def __init__(self, rules, *, maximum_delay_seconds=30., clock=time.perf_counter, sleep=time.sleep):
        if not math.isfinite(maximum_delay_seconds) or maximum_delay_seconds < 0:
            raise ValueError('maximum_delay_seconds must be finite and nonnegative')
        self.rules = {str(k): v if isinstance(v, DelayRule) else DelayRule(**v) for k, v in rules.items()}
        self.maximum = maximum_delay_seconds
        self.clock, self.sleep = clock, sleep
        self.rows = []
        self.requested_total = 0.
        self.frozen_plan = {'rules': {k: asdict(v) for k, v in self.rules.items()},
                            'maximum_requested_delay_seconds': maximum_delay_seconds,
                            'actual_sleep_may_overshoot_requested_delay': True,
                            'injection_kind': 'post-operation sleep', 'returned_values_modified': False}
        self.plan_sha256 = digest(self.frozen_plan)

    def call(self, stage, function: Callable, *args, **kwargs):
        before = self.clock()
        result = function(*args, **kwargs)
        completed = self.clock()
        raw = completed - before
        if raw < 0 or not math.isfinite(raw):
            raise RuntimeError('non-monotonic stage clock')
        rule = self.rules.get(stage, DelayRule())
        delay = (rule.multiplier - 1.) * raw + rule.extra_seconds
        if self.requested_total + delay > self.maximum:
            raise RuntimeError('predeclared injection delay budget exhausted')
        self.requested_total += delay
        if delay:
            self.sleep(delay)
        after = self.clock()
        self.rows.append({'stage': stage, 'raw_operation_seconds': raw,
                          'requested_injected_seconds': delay, 'actual_injected_seconds': after - completed,
                          'complete_wrapped_seconds': after - before})
        return result

    def report(self):
        return {'frozen_plan': json.loads(json.dumps(self.frozen_plan)), 'plan_sha256': self.plan_sha256,
                'wrapper_source_sha256': digest_file(__file__),
                'calls': list(self.rows), 'requested_total_seconds': self.requested_total,
                'actual_total_seconds': sum(row['actual_injected_seconds'] for row in self.rows),
                'scope': 'real operation plus explicitly injected post-operation sleep; not a hardware slowdown or receiver correctness test'}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--campaigns', type=int, default=200)
    parser.add_argument('--families', type=int, default=14)
    parser.add_argument('--seed', type=int, default=728510)
    parser.add_argument('--budget-seconds', type=float, help='optional wall budget; incomplete controls remain explicitly unresolved')
    parser.add_argument('--noise', choices=('lognormal', 'empirical-iid', 'empirical-block'), default='lognormal')
    parser.add_argument('--log-sigma', type=float, default=.04)
    parser.add_argument('--paired-correlation', type=float, default=.6)
    parser.add_argument('--residual-report', type=Path)
    parser.add_argument('--block-length', type=int, default=2)
    parser.add_argument('--mode', action='append', choices=('direct', 'conditional', 'full-only', 'outside-score', 'environment', 'regime-change'))
    parser.add_argument('--factor', type=float, action='append')
    args = parser.parse_args(argv)
    if not 1 <= args.campaigns <= 10000 or not 1 <= args.families <= 128:
        parser.error('campaigns must be1..10000 and families1..128')
    if args.budget_seconds is not None and (not math.isfinite(args.budget_seconds) or args.budget_seconds <= 0):
        parser.error('budget-seconds must be finite and positive')
    if args.output.exists() or args.output.with_suffix(args.output.suffix + '.plan.json').exists():
        parser.error('output and commitment must be fresh')
    policy = AdaptivePolicy(family_count=args.families)
    model = NoiseModel(args.noise, args.log_sigma, args.paired_correlation, args.block_length)
    empirical, provenance = None, None
    if model.kind != 'lognormal':
        if args.residual_report is None:
            parser.error('empirical noise requires --residual-report')
        empirical, provenance = read_residuals(args.residual_report)
        if len(empirical) != args.families:
            parser.error('--families must match every measured case in residual report')
    modes = args.mode or ['direct', 'conditional', 'full-only', 'outside-score', 'environment', 'regime-change']
    factors = args.factor or list(FACTORS)
    if any(not math.isfinite(f) or f < 1 for f in factors):
        parser.error('factors must be finite and >=1')
    # Controls are qualification work, not a mandatory many-trial routine test.
    sources = {p.name: digest_file(p) for p in [Path(__file__), Path(__file__).with_name('triad-performance.py'),
              Path(__file__).with_name('triad_perf_adaptive.py'), Path(__file__).with_name('triad_perf_conditional.py')]}
    plan = {'schema': SCHEMA, 'policy': asdict(policy), 'noise': asdict(model),
            'modes': modes, 'factors': factors, 'campaigns': args.campaigns, 'seed': args.seed,
            'budget_seconds': args.budget_seconds,
            'checkpoint_contract': 'one atomic checkpoint per finished control; partial budget-stopped controls do not qualify fixed-n uncertainty',
            'source_sha256': sources, 'empirical_provenance': provenance,
            'runtime': {'python': sys.version, 'numpy': np.__version__,
                        'scipy': __import__('scipy').__version__},
            'assumptions': ['independently seeded simulated campaigns are binomial units',
                'lognormal generator is stationary; empirical resampling is conditional on finite retained observations',
                'pair dependence is retained; empirical blocks preserve only their declared short ordering',
                'different workload families are generated independently; shared environmental shifts are a separate deterministic control',
                'empirical block resampling deliberately violates independent outer times within blocks: it measures robustness, not validity of the Student-t assumptions',
                'conditional history is an idealized zero-bias fixture, not a claimed experimental anchor'],
            'not_claimed': ['receiver/channel simulation', 'future hardware jitter distribution',
                           'universal model-error bound', 'confidence-qualified raw hardware screen'],
            'confidence_intervals': '95% pointwise exact binomial; no simultaneous interval claim across output rows'}
    plan['plan_sha256'] = digest(plan)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    commitment = args.output.with_suffix(args.output.suffix + '.plan.json')
    with commitment.open('x') as stream:
        json.dump(plan, stream, indent=2, allow_nan=False)
    started = time.perf_counter()
    deadline = None if args.budget_seconds is None else started + args.budget_seconds
    schedule = [{'mode': mode, 'factor': factor,
                 'seed': args.seed + mi * 100000 + fi * 1000}
                for mi, mode in enumerate(modes) for fi, factor in enumerate(factors)]
    result = {'schema': SCHEMA, 'plan': plan, 'status': 'running', 'controls': [],
              'pending_controls': schedule.copy(), 'environment_screens': [],
              'elapsed_seconds': 0., 'completed_control_count': 0,
              'requested_control_count': len(schedule), 'exit_code': None,
              'scope': 'decision-system calibration under explicit timing distributions; no measured receiver performance claim'}
    # Own the pathname before beginning any expensive work. Existing output is
    # never overwritten, while subsequent atomic replacements update this run.
    with args.output.open('x') as stream:
        json.dump(result, stream, indent=2, allow_nan=False)

    def checkpoint():
        result['elapsed_seconds'] = time.perf_counter() - started
        temporary = args.output.with_suffix(args.output.suffix + '.writing')
        with temporary.open('w') as stream:
            json.dump(result, stream, indent=2, allow_nan=False)
            stream.write('\n')
        os.replace(temporary, args.output)

    try:
        for i, control in enumerate(schedule):
            if deadline is not None and time.perf_counter() >= deadline:
                result['status'] = 'budget_exhausted'
                break
            row = run_control(model, campaigns=args.campaigns, policy=policy,
                              empirical_rows=empirical, deadline=deadline, **control)
            result['controls'].append(row)
            if row['status'] == 'complete':
                result['completed_control_count'] += 1
                result['pending_controls'] = schedule[i + 1:]
            else:
                result['status'] = 'budget_exhausted'
                result['pending_controls'] = schedule[i:]
            after = {name: digest_file(Path(__file__).with_name(name)) for name in sources}
            if sources != after:
                result['status'] = 'invalid_source_change'
                result['source_sha256_after'] = after
            checkpoint()
            print(f"control {i+1}/{len(schedule)} {control['mode']} factor={control['factor']} "
                  f"campaigns={row['campaigns']}/{args.campaigns} status={row['status']}", flush=True)
            if result['status'] != 'running':
                break
        if result['status'] == 'running':
            result['environment_screens'] = [environment_screen(f) for f in factors] if 'environment' in modes else []
            result['status'] = 'complete'
        result['exit_code'] = 0 if result['status'] == 'complete' else 2
    except Exception as error:
        result['status'] = 'failed'
        result['error'] = {'type': type(error).__name__, 'message': str(error)}
        result['exit_code'] = 2
    checkpoint()
    return result['exit_code']



if __name__ == '__main__':
    raise SystemExit(main())
