#!/usr/bin/env python3
"""Summarize the fully audited corrected-selectivity holdout, without rereading I/Q.

This consumes the accepted analyzer's actual-observation counts and per-path
recounts. It independently reconciles those two levels and fixed-bit strata.
Only balanced all-success/all-failure endpoints use the sharper AM-GM bound
from the accepted earlier followup; other endpoints retain the analyzer's
Bonferroni average of two per-bit 97.5% one-sided Clopper-Pearson bounds.
"""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
import copy
import hashlib
import json
import math
from pathlib import Path
import sys

from scipy.stats import beta

HERE = Path(__file__).resolve().parent
ANALYZER_SHA256 = '45f4960da40c216c07be8a632182756679a315c5d6ad645506fe102b17b7477e'
FOLLOWUP = HERE.parents[1] / '2026-11-02-selective' / 'reproduction' / 'followup_confidence.py'
FOLLOWUP_SHA256 = '4483e0b3289319a048d5cb1c67eea74846f75cc333a09b464f041b81cd331e98'
PROFILES = ('g001', 'g01', 'g1', 'high10', 'high30')
METHODS = {'static-fft-ucb', 'gaussian-window-ucb'}
CONTROLS = {'signal', 'null', 'wrong-key'}
GATES = ('strict', 'relaxed')


def require(value, message):
    if not value: raise ValueError(message)


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''): digest.update(block)
    return digest.hexdigest()


def load(path):
    def reject(value): raise ValueError(f'nonfinite JSON {value}: {path}')
    value = json.loads(Path(path).read_text(), parse_constant=reject)
    require(isinstance(value, dict), f'expected object: {path}')
    return value


def cp(k, n, alpha=.05):
    require(type(k) is int and type(n) is int and n > 0 and 0 <= k <= n, 'invalid binomial counts')
    return {'successes': k, 'trials': n, 'fraction': k/n,
            'lower_one_sided_95': float(beta.ppf(alpha, k, n-k+1)) if k else 0.,
            'upper_one_sided_95': float(beta.ppf(1-alpha, k+1, n-k)) if k < n else 1.}


def balanced_bound(bits, event):
    require(set(bits) == {'0', '1'}, 'both fixed-bit strata required')
    n = [bits[str(b)]['trials'] for b in (0, 1)]
    k = [bits[str(b)][event] for b in (0, 1)]
    require(all(type(v) is int and v > 0 for v in n) and
            all(type(v) is int and 0 <= v <= N for v, N in zip(k, n)), 'invalid stratum counts')
    balanced = n[0] == n[1]
    all_success = balanced and k == n
    all_failure = balanced and not any(k)
    lower = .05**(1/sum(n)) if all_success else sum(
        float(beta.ppf(.025, v, N-v+1)) if v else 0. for v, N in zip(k, n))/2
    upper = 1-.05**(1/sum(n)) if all_failure else sum(
        float(beta.ppf(.975, v+1, N-v)) if v < N else 1. for v, N in zip(k, n))/2
    estimate = sum(v/N for v, N in zip(k, n))/2
    require(0 <= lower <= estimate <= upper <= 1, 'invalid equal-bit probability interval')
    return {'estimate': estimate, 'lower_one_sided_95': lower, 'upper_one_sided_95': upper,
            'lower_method': 'balanced all-success AM-GM' if all_success else 'mean per-bit 97.5% one-sided CP; Bonferroni',
            'upper_method': 'balanced all-failure AM-GM complement' if all_failure else 'mean per-bit 97.5% one-sided CP; Bonferroni',
            'per_bit': {str(b): cp(k[b], n[b]) for b in (0, 1)}}


def bound_close(actual, expected):
    require(type(actual) in (int, float) and math.isfinite(actual) and
            math.isclose(actual, expected, rel_tol=0, abs_tol=2e-13), 'audited confidence endpoint mismatch')


def verify_main_bounds(counts, bits, events):
    """Check the accepted summary's original Bonferroni numbers before sharpening."""
    require(counts.get('pointwise_interval') is None and counts.get('outcome_intervals') is None,
            'pooled common-p confidence must remain suppressed')
    require(counts.get('interval_suppressed_due_to_failed_control_paths') is False,
            'failed control paths cannot qualify confidence')
    for event in events:
        bound = counts['balanced_bit_mixture_interval']['outcomes'][event]
        lows = []; highs = []
        for bit in ('0', '1'):
            k, n = bits[bit][event], bits[bit]['trials']
            original = counts['per_bit'][bit]['outcome_intervals'][event]
            current = cp(k, n)
            for direction in ('lower_one_sided_95', 'upper_one_sided_95'):
                bound_close(original[direction], current[direction])
            lows.append(float(beta.ppf(.025, k, n-k+1)) if k else 0.)
            highs.append(float(beta.ppf(.975, k+1, n-k)) if k < n else 1.)
        bound_close(bound['lower_one_sided_95'], sum(lows)/2)
        bound_close(bound['upper_one_sided_95'], sum(highs)/2)
        bound_close(bound['estimate'], sum(bits[b][event]/bits[b]['trials'] for b in ('0', '1'))/2)


def target_assessment(bound):
    return {str(target): ('pointwise_lower_establishes_target' if bound['lower_one_sided_95'] >= target
            else 'pointwise_upper_below_target' if bound['upper_one_sided_95'] < target
            else 'not_resolved_by_pointwise_bounds') for target in (.99, .90)}


def summarize_scores(analysis):
    result = []; unique_inputs = set(); cells = set(); total_control = Counter()
    for score in analysis['receiver_score_summaries']:
        require(score.get('profile') in PROFILES and score.get('modulation') in ('each-25', 'each-65')
                and score.get('model') == 'dispersive-dft', 'unexpected corrected holdout profile/model/depth')
        require(score.get('observation_mode') == 'actual-noisy' and score.get('all_input_hash_receipts_reverified') is True
                and not score.get('failed_physical_paths')
                and score.get('actual_trial_root_independence', {}).get('verified') is True,
                'score is incomplete, conditional, unverified, or contains failed paths')
        paths = score['per_path']; keys = set(); tallies = defaultdict(Counter)
        bits = defaultdict(lambda: {'0': Counter(), '1': Counter()}); sources = defaultdict(set)
        for row in paths:
            require(row.get('control') in CONTROLS and row.get('method') in METHODS and
                    type(row.get('truth_bit')) is int and row['truth_bit'] in (0, 1) and row.get('trials') == 1,
                    'invalid actual per-path trial/control/method/bit')
            identity = (row['input'], row['method'])
            require(identity not in keys, 'duplicate physical path/method recount')
            keys.add(identity); sources[row['method']].add(row['input'])
            for gate in GATES:
                key = (row['method'], row['control'], row['cn0_db_hz'], gate)
                values = row[gate]
                fields = ('trials', 'correct_accepted', 'wrong_accepted', 'missed', 'raw_correct') if row['control'] == 'signal' else ('trials', 'false_admitted', 'not_admitted')
                require(set(values) == set(fields) and all(type(v) is int and 0 <= v <= 1 for v in values.values())
                        and values['trials'] == 1, 'invalid one-observation outcome counts')
                outcomes = fields[1:4] if row['control'] == 'signal' else fields[1:]
                require(sum(values[name] for name in outcomes) == 1, 'per-path outcomes do not partition one observation')
                tallies[key].update(values); bits[key][str(row['truth_bit'])].update(values)
        require(set(sources) == METHODS and sources['static-fft-ucb'] == sources['gaussian-window-ucb']
                and len(sources['static-fft-ucb']) == score['completed_physical_paths'], 'method/path inventories differ')
        source_set = sources['static-fft-ucb']
        require(not unique_inputs.intersection(source_set), 'physical source occurs in multiple score jobs')
        unique_inputs.update(source_set)
        require(len(score['counts']) == 4 and len(tallies) == 4, 'incomplete two-method/two-gate counts')
        controls = {key[1] for key in tallies}; require(len(controls) == 1, 'mixed controls in score job')
        control = next(iter(controls)); total_control[control] += len(source_set)
        cell_key = (score['profile'], score['modulation'], control)
        require(cell_key not in cells, 'duplicate fixed holdout cell'); cells.add(cell_key)
        for row in score['counts']:
            key = (row['method'], row['control'], row['cn0_db_hz'], row['gate'])
            require(key in tallies and row.get('independent_unit') == 'actual physical path', 'unexpected audit count key/unit')
            expected = dict(tallies.pop(key)); strata = {bit: dict(values) for bit, values in bits[key].items()}
            counts = row['counts']
            require(all(counts.get(k) == v for k, v in expected.items()) and
                    all(counts.get('per_bit', {}).get(bit, {}).get('counts') == values for bit, values in strata.items()),
                    'audited counts do not match complete per-path/per-bit recount')
            events = ('correct_accepted', 'wrong_accepted', 'missed') if control == 'signal' else ('false_admitted', 'not_admitted')
            verify_main_bounds(counts, strata, events)
            bounds = {event: balanced_bound(strata, event) for event in events}
            result.append({'profile': score['profile'], 'modulation': score['modulation'],
                           'control': control, 'method': row['method'], 'gate': row['gate'],
                           'sampled_cn0_db_hz': row['cn0_db_hz'], 'counts': expected, 'per_bit_counts': strata,
                           'pointwise_equal_bit_probability_bounds': bounds,
                           'correct_reception_target_assessment': target_assessment(bounds['correct_accepted']) if control == 'signal' else None,
                           'population': 'Correct accepted bit' if control == 'signal' else
                              'Noise-only admission' if control == 'null' else 'Mismatched-key signal admission; distinct from noise-only false admission',
                           'source_score_report_sha256': score['report_sha256'],
                           'source_score_archive_sha256': score['score_archive_sha256']})
        require(not tallies, 'unmatched audited outcome tally')
    require(cells == {(p, d, c) for p in PROFILES for d in ('each-25', 'each-65') for c in CONTROLS}
            and len(unique_inputs) == analysis['workspace_count'] == 1024
            and total_control == {'signal': 512, 'null': 256, 'wrong-key': 256}, 'holdout coverage/denominator differs from frozen 1024-path matrix')
    return sorted(result, key=lambda r: (PROFILES.index(r['profile']), r['modulation'], r['control'], r['method'], r['gate'])), dict(total_control)


def verify_interpreter_repair(root, repaired_path, original_path):
    repaired, original = load(repaired_path), load(original_path)
    repair = repaired.get('interpreter_repair', {})
    require(repair.get('parent_plan_sha256') == sha256(original_path), 'interpreter repair original plan SHA mismatch')
    adjusted = copy.deepcopy(repaired); adjusted.pop('interpreter_repair', None)
    for inventory in ('jobs', 'scoring_jobs'):
        require(len(adjusted.get(inventory, [])) == len(original.get(inventory, [])) == 30,
                'interpreter repair lacks original exact 30-job inventory')
        for new, old in zip(adjusted[inventory], original[inventory]):
            require(new['argv'][0] == '/tmp/triad-weak-env/bin/python' and old['argv'][0] == '/usr/bin/python3',
                    'unexpected interpreter replacement')
            new['argv'][0] = old['argv'][0]
    require(adjusted == original, 'interpreter repair changed scientific argv, outputs, jobs, or other frozen fields')
    failed_path = Path(repair.get('original_failure_results', ''))
    require(not failed_path.is_absolute() and '..' not in failed_path.parts, 'unsafe original failure receipt path')
    failed_path = root / failed_path; failure = load(failed_path)
    require(failure.get('cancelled') is False and len(failure.get('results', [])) == 30, 'missing original 30 import failures')
    jobs = {job['name']: job for job in original['scoring_jobs']}; seen = set(); logs = []
    for row in failure['results']:
        name = row.get('name'); require(name in jobs and name not in seen, 'duplicate or unplanned failed job')
        seen.add(name); runner = row.get('runner', {})
        require(row.get('command') == jobs[name]['argv'] and row.get('returncode') == 1
                and runner.get('schema') == 'triad-bounded-run-v1' and runner.get('outcome') == 'child_failure'
                and runner.get('exit_code') == runner.get('child_exit_code') == 1
                and runner.get('timed_out') is False and runner.get('cancelled') is False
                and runner.get('cancel_signal') is None
                and runner.get('declared_max_seconds') == runner.get('timeout_seconds') == jobs[name]['timeout_seconds']
                and runner.get('termination', {}).get('group_empty') is True,
                'original failed interpreter receipt does not bind a clean import failure')
        log = Path(runner['log']); require(log.is_absolute() and log.is_relative_to(root), 'failure log outside supplied root')
        text = log.read_text()
        require("import numpy as np" in text and "ModuleNotFoundError: No module named 'numpy'" in text,
                'original failure was not the recorded NumPy import error')
        logs.append({'job': name, 'log': str(log), 'sha256': sha256(log)})
    require(seen == set(jobs), 'original failed jobs incomplete')
    return {'original_score_plan': str(original_path), 'original_score_plan_sha256': sha256(original_path),
            'repaired_score_plan': str(repaired_path), 'repaired_score_plan_sha256': sha256(repaired_path),
            'original_failed_results': str(failed_path), 'original_failed_results_sha256': sha256(failed_path),
            'original_failed_jobs': 30, 'verified_change': 'Only argv[0]: /usr/bin/python3 to /tmp/triad-weak-env/bin/python',
            'failure': "All 30 original jobs stopped at import numpy: ModuleNotFoundError; this is an environment setup failure, not a receiver result.",
            'failure_logs': logs}


def run(args):
    root = args.root.resolve(); analysis = load(args.analysis); status = load(args.status)
    require(sha256(args.analysis) == args.expected_analysis_sha256, 'audited holdout analysis SHA differs from frozen input')
    require(analysis.get('schema') == 'triad-selective-model-correction-analysis-v1' and analysis.get('stage') == 'holdout'
            and analysis.get('comparison_scope') == 'independent-corrected-model-validation'
            and analysis.get('analyzer_sha256') == ANALYZER_SHA256
            and analysis.get('execution_receipts_complete') is True, 'unexpected or incomplete accepted holdout analysis')
    require(status.get('schema') == 'triad-bounded-run-v1' and status.get('outcome') == 'completed'
            and status.get('exit_code') == status.get('child_exit_code') == 0
            and status.get('timed_out') is False and status.get('cancelled') is False
            and status.get('termination', {}).get('group_empty') is True, 'holdout analyzer execution did not complete cleanly')
    require(sha256(HERE / 'analyze.py') == ANALYZER_SHA256 and sha256(FOLLOWUP) == FOLLOWUP_SHA256,
            'accepted analysis/confidence-reference source changed')
    plan_path, score_path = Path(analysis['plan']), Path(analysis['score_plan'])
    require(plan_path.is_relative_to(root) and score_path.is_relative_to(root)
            and sha256(plan_path) == analysis['plan_sha256'] and sha256(score_path) == analysis['score_plan_sha256'],
            'frozen generation/scoring plan binding mismatch')
    plan = load(plan_path)
    rows, totals = summarize_scores(analysis)
    repair = verify_interpreter_repair(root, score_path, args.original_score_plan)
    for receipt in analysis['runner_receipts']:
        path = Path(receipt['path']); path = path if path.is_absolute() else root / path
        require(sha256(path) == receipt['sha256'], 'accepted completed runner receipt SHA mismatch')
    result = {'schema': 'triad-corrected-selectivity-holdout-summary-v1', 'state': 'audited-count-reconciliation-passed',
              'scientific_qualification': False, 'summary_source_sha256': sha256(Path(__file__)),
              'audited_input': {'path': str(args.analysis.resolve()), 'sha256': sha256(args.analysis),
                               'analyzer_sha256': ANALYZER_SHA256, 'execution_status': str(args.status.resolve()),
                               'execution_status_sha256': sha256(args.status), 'raw_arrays_reverified_by_accepted_analyzer': True},
              'frozen_plans': {'export': {'path': str(plan_path), 'sha256': analysis['plan_sha256']},
                               'scoring': {'path': str(score_path), 'sha256': analysis['score_plan_sha256']}},
              'confidence_reference': {'path': str(FOLLOWUP), 'sha256': FOLLOWUP_SHA256,
                                       'reuse': 'balanced() endpoint formula; reference script is not imported or executed'},
              'physical_profiles': plan['profiles'], 'physical_exports': 1024, 'physical_exports_by_control': totals,
              'receiver_methods': sorted(METHODS), 'row_count': len(rows), 'rows': rows,
              'interpreter_setup_repair': repair,
              'scope': [
                  'Counts and strata are reconciled against all audited actual-observation per-path rows; raw arrays and retained projections are not reopened here.',
                  'Each endpoint is separately pointwise one-sided 95%, not a joint interval or simultaneous guarantee across outcomes, methods, gates, profiles, depths, or selected targets.',
                  'Bounds concern the equal-probability bit population. Independent IID Bernoulli trials are assumed within each fixed-bit stratum; p0 and p1 may differ. No pooled common-p CP is used.',
                  'AM-GM sharpening applies only to balanced all-success lower or balanced all-failure upper endpoints. Other endpoints average two per-bit 97.5% one-sided CP bounds by Bonferroni.',
                  'Repeated methods, gates, depths, and control distributions do not create extra independent signal trials. Null and wrong-key cases remain distinct populations.',
                  'Zero noise-only admissions among 16 or 32 captures per cell cannot empirically establish 1e-4/day or 1%/12h continuous-operation false admission.',
                  'A wrong-key signal admission is a key-discrimination failure, distinct from noise-only admission. Both distributions are reported without pooling.',
                  'These nominal public timing/frequency/clock banks do not demonstrate blind acquisition; high-spread timing and intrablock channel mismatch remain possible limits.',
              ]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
    return result


def self_test():
    for n in (16, 32, 64):
        for k in (0, n//2, n):
            bits = {'0': {'trials': n, 'x': k}, '1': {'trials': n, 'x': k}}
            value = balanced_bound(bits, 'x')
            if k == n: bound_close(value['lower_one_sided_95'], .05**(1/(2*n)))
            if k == 0: bound_close(value['upper_one_sided_95'], 1-.05**(1/(2*n)))
    unequal = balanced_bound({'0': {'trials': 3, 'x': 3}, '1': {'trials': 5, 'x': 5}}, 'x')
    require('AM-GM' not in unequal['lower_method'], 'unequal strata accidentally sharpened')
    require(target_assessment({'lower_one_sided_95': .95, 'upper_one_sided_95': 1.})['0.9'] == 'pointwise_lower_establishes_target', '90% target status error')
    for invalid in ({'0': {'trials': 0, 'x': 0}, '1': {'trials': 2, 'x': 1}},
                    {'0': {'trials': 2, 'x': 3}, '1': {'trials': 2, 'x': 1}}):
        try: balanced_bound(invalid, 'x')
        except ValueError: pass
        else: raise ValueError('invalid stratum accepted')
    print('confidence endpoint/balance/target/fail-closed self-tests passed')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--analysis', type=Path)
    parser.add_argument('--expected-analysis-sha256')
    parser.add_argument('--status', type=Path)
    parser.add_argument('--root', type=Path, default=HERE.parents[4])
    parser.add_argument('--original-score-plan', type=Path)
    parser.add_argument('--output', type=Path, default=HERE / 'holdout-summary.json')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args(argv)
    try:
        if args.self_test: self_test(); return 0
        require(all((args.analysis, args.expected_analysis_sha256, args.status, args.original_score_plan)),
                '--analysis, --expected-analysis-sha256, --status and --original-score-plan are required')
        result = run(args)
    except (ValueError, KeyError, OSError, TypeError) as error:
        print(f'holdout summary failed: {error}', file=sys.stderr); return 1
    print(json.dumps({'state': result['state'], 'physical_exports': result['physical_exports'],
                      'summary_rows': result['row_count'], 'output': str(args.output), 'sha256': sha256(args.output)}))
    return 0


if __name__ == '__main__': raise SystemExit(main())
