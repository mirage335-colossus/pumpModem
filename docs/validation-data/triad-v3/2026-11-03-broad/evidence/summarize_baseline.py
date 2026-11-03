#!/usr/bin/env python3
"""Compact a frozen broad-study analysis without pooling heterogeneous trials.

This summarizes the already independently recounted baseline. It does not read
raw scores, repair outcomes, infer a sensitivity boundary, or qualify reliability.
Run with --analysis core-analysis.json --plan core-frozen.json --output NEW.json.
The frozen plan may deliberately differ from today's receiver source.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def counts(row):
    value = row['counts']
    keys = ('trials', 'correct_accepted', 'wrong_accepted', 'missed', 'raw_correct') if row['control'] == 'signal' else ('trials', 'false_admitted', 'not_admitted')
    return [value[key] for key in keys]


def make_summary(data, plan, analysis_path, plan_path):
    if data['plan_sha256'] != sha(plan_path):
        raise ValueError('analysis and frozen plan identities differ')
    jobs = {job['id']: job for job in plan['jobs']}
    if len(jobs) != len(plan['jobs']):
        raise ValueError('duplicate planned job')
    unexecuted = set(data['unexecuted_job_ids'])
    status = {key: 'unexecuted' if key in unexecuted else 'completed' for key in jobs}
    failures = {}
    for row in data['failures']:
        key = row['job_id']
        if key not in jobs:
            raise ValueError('failure not in frozen plan')
        if key in failures and failures[key]['status'] != row['status']:
            raise ValueError('conflicting failure states')
        status[key] = row['status']
        entry = failures.setdefault(key, {'job_id': key, 'case': jobs[key]['case'],
            'stage': jobs[key]['stage'], 'status': row['status']})
        for field in ('reason', 'error', 'dependencies', 'receiver_status'):
            if row.get(field) is not None:
                entry[field] = row[field]
    if dict(Counter(status.values())) != data['job_status_counts']:
        raise ValueError('unique planned-job statuses disagree with input analysis')

    outcomes = {}
    intervals = {}
    for row in data['outcomes']:
        key = (row['job_id'], row['target'], row['control'])
        if key in outcomes or row['job_id'] not in jobs or status[row['job_id']] != 'completed':
            raise ValueError('duplicate, unplanned or failed scored outcome')
        outcomes[key] = row
        n = row['counts']['trials']
        success = row['counts']['correct_accepted' if row['control'] == 'signal' else 'false_admitted']
        bound = [row['interval']['one_sided_95_lower'], row['interval']['one_sided_95_upper']]
        interval_key = f'{success}/{n}'
        if interval_key in intervals and intervals[interval_key] != bound:
            raise ValueError('inconsistent pointwise interval')
        intervals[interval_key] = bound

    opportunities = {(row['job_id'], row['control']): row for row in data['opportunity_accounting']}
    if len(opportunities) != len(data['opportunity_accounting']):
        raise ValueError('duplicate opportunity records')
    actual = []
    for key, job in sorted(jobs.items()):
        if job['stage'] != 'actual':
            continue
        for target in ('strict', 'relaxed'):
            row = outcomes.get((key, target, job['control']))
            actual.append([key, job['case'], job['bit'], job['method'], target,
                job['cn0_db_hz'], job['control'], status[key], counts(row) if row else None,
                row['score_min'] if row else None, row['score_max'] if row else None,
                row['threshold'] if row else None])

    groups = defaultdict(list)
    for key, job in sorted(jobs.items()):
        if job['stage'] == 'conditional':
            for target in ('strict', 'relaxed'):
                groups[(job['case'], job['bit'], job['method'], target)].append((key, job))
    conditional = []
    for (case, bit, method, target), group in sorted(groups.items()):
        cells = []
        for key, job in sorted(group, key=lambda pair: pair[1]['cn0_db_hz']):
            signal = outcomes.get((key, target, 'signal'))
            null = outcomes.get((key, target, 'null'))
            if bool(signal) != bool(null):
                raise ValueError('conditional signal/null availability differs')
            cells.append([job['cn0_db_hz'], status[key], counts(signal) if signal else None,
                counts(null) if null else None,
                signal['score_min'] if signal else None, signal['score_max'] if signal else None,
                signal['threshold'] if signal else None])
        scored = [row for row in cells if row[2] is not None]
        any_correct = [row[0] for row in scored if row[2][1] > 0]
        all_correct = [row[0] for row in scored if row[2][1] == row[2][0]]
        any_wrong = [row[0] for row in scored if row[2][2] > 0]
        no_admission = [row[0] for row in scored if row[2][3] == row[2][0]]
        false_null = [row[0] for row in scored if row[3][1] > 0]
        nonmonotone = [row[0] for row in scored if all_correct and row[0] > min(all_correct)
                      and row[2][1] < row[2][0]]
        conditional.append({'case': case, 'bit': bit, 'method': method, 'target': target,
            'cells': cells, 'sampled_crossing_descriptors_only': {
                'lowest_tested_cn0_with_any_correct': min(any_correct) if any_correct else None,
                'lowest_tested_cn0_with_four_correct': min(all_correct) if all_correct else None,
                'tested_cn0_with_no_admissions': no_admission,
                'tested_cn0_with_any_wrong_admission': any_wrong,
                'tested_cn0_with_any_null_admission': false_null,
                'higher_tested_cn0_losing_four_correct_after_first_four': nonmonotone}})

    benchmarks = []
    benchmark_by_cell = {}
    for row in data['outcomes']:
        if row['stage'] != 'benchmark' or row['target'] != 'strict':
            continue
        job = jobs[row['job_id']]
        prep = row['diagnostics'].get('preparation_seconds_this_call')
        item = {key: row[key] for key in ('job_id', 'case', 'method', 'counts', 'score_min', 'score_max',
            'threshold', 'receiver_seconds', 'report_wall_seconds', 'bounded_job_seconds', 'arithmetic')}
        item.update(backend=job['backend'], precision=job['precision'],
            gaussian_factor_preparation_seconds=prep,
            gaussian_preparation_fraction_of_method=(prep / row['receiver_seconds']) if prep is not None else None,
            report_nonmethod_seconds=row['report_wall_seconds'] - row['receiver_seconds'],
            bounded_minus_report_seconds=row['bounded_job_seconds'] - row['report_wall_seconds'],
            source_blocks=row['diagnostics'].get('blocks'))
        benchmarks.append(item)
        benchmark_by_cell[(row['case'], row['method'], job['backend'], job['precision'])] = item
    backend_pairs = []
    for key, cpu in sorted(benchmark_by_cell.items()):
        case, method, backend, precision = key
        if backend != 'cpu':
            continue
        gpu = benchmark_by_cell[(case, method, 'cuda:0', precision)]
        backend_pairs.append({'case': case, 'method': method, 'precision': precision,
            'decision_counts_match': cpu['counts'] == gpu['counts'],
            'cpu_over_cuda_method_seconds': cpu['receiver_seconds'] / gpu['receiver_seconds'],
            'cpu_over_cuda_report_seconds': cpu['report_wall_seconds'] / gpu['report_wall_seconds'],
            'cpu_over_cuda_bounded_seconds': cpu['bounded_job_seconds'] / gpu['bounded_job_seconds']})
    precision_pairs = []
    for key, low in sorted(benchmark_by_cell.items()):
        case, method, backend, precision = key
        if precision != 'fp32':
            continue
        high = benchmark_by_cell[(case, method, backend, 'fp64')]
        precision_pairs.append({'case': case, 'method': method, 'backend': backend,
            'decision_counts_match': low['counts'] == high['counts'],
            'absolute_score_min_difference': abs(low['score_min'] - high['score_min']),
            'absolute_score_max_difference': abs(low['score_max'] - high['score_max'])})

    alarms = [{'job_id': row['job_id'], 'case': row['case'], 'stage': row['stage'],
               'method': row['method'], 'control': row['control'], 'cn0_db_hz': row['cn0_db_hz'],
               'counts': row['counts'], 'interval': row['interval']}
              for row in data['outcomes'] if row['target'] == 'strict' and
              ((row['control'] != 'signal' and row['counts']['false_admitted']) or
               (row['control'] == 'signal' and row['counts']['wrong_accepted']))]
    by_stage = {}
    for stage in sorted({job['stage'] for job in jobs.values()}):
        by_stage[stage] = dict(Counter(status[key] for key, job in jobs.items() if job['stage'] == stage))
    return {'schema': 'triad-broad-baseline-summary-v1',
        'scope': 'Frozen pre-numerics-repair baseline; diagnostic cells, not a qualification or sensitivity-boundary estimate',
        'inputs': {'analysis_file': analysis_path.name, 'analysis_sha256': sha(analysis_path),
            'plan_file': plan_path.name, 'plan_sha256': sha(plan_path),
            'independent_analysis_source_sha256': data['analysis_sha256'],
            'binary_sha256': plan['expected_binary_sha256'], 'source_archive_sha256': plan['source_archive_sha256'],
            'source_file_sha256': plan['source_file_sha256'], 'receipts': data['receipts'],
            'summary_source_sha256': sha(__file__)},
        'job_status_counts': data['job_status_counts'], 'job_status_by_stage': by_stage,
        'unexecuted_job_ids': sorted(unexecuted), 'unique_failed_or_unstarted_jobs': list(failures.values()),
        'opportunity_counts_by_stage_control_status': [
            {'stage': key[0], 'control': key[1], 'status': key[2], 'job_control_records': value}
            for key, value in sorted(Counter((row['stage'], row['control'], row['status'])
                                             for row in opportunities.values()).items())],
        'case_configurations': [{**row['case'], 'sample_rate': row.get('sample_rate'),
            'block_samples': row.get('block_samples'), 'receiver_limits': row.get('receiver_limits'),
            'status': row['status']} for row in plan['configuration_cases']],
        'pointwise_one_sided_95_bounds_by_success_over_trials': intervals,
        'actual_columns': ['job_id', 'case', 'bit', 'method', 'target', 'cn0_db_hz', 'control', 'status',
            'counts', 'score_min', 'score_max', 'threshold'],
        'signal_count_columns': ['trials', 'correct_accepted', 'wrong_accepted', 'missed', 'raw_correct'],
        'control_count_columns': ['trials', 'false_admitted', 'not_admitted'],
        'actual_observations': actual,
        'conditional_cell_columns': ['cn0_db_hz', 'status', 'signal_counts', 'null_counts',
            'signal_score_min', 'signal_score_max', 'threshold'],
        'conditional_curves': conditional,
        'strict_wrong_or_control_admissions': alarms,
        'physical_energy_evaluation': data['physical_energy_evaluation'],
        'benchmark_runs': benchmarks, 'benchmark_backend_pairs': backend_pairs,
        'benchmark_precision_pairs': precision_pairs,
        'interpretation': [
            'All planned jobs retain status. The failure list is deduplicated by job, not by repeated execution/detail entries.',
            'Actual rows retain each bit-specific physical path. Conditional curves have four noise draws on one fixed path at each level; no cells or physical paths are pooled.',
            'The sampled crossing descriptors are not estimated sensitivity boundaries, required durations or reliability targets. High-SNR numerical failures and nonmonotone outcomes remain visible.',
            'Strict null admissions at extreme SNR are baseline numerical defects under investigation; they are not valid rare-event calibration or physical-channel limits. Later fixes require separately bound paired evidence.',
            'Wrong-key Aurora-worst admissions are structured-signal controls, distinct from Gaussian null calibration. A Gaussian renewing-HF-worst signal also has a wrong-bit admission.',
            'Nearly all physical cases use nominal arrival/clock/epoch projections with independently drawn physical timing; misses cannot be attributed solely to SNR, spreading or channel limits.',
            'Receiver_seconds includes each method preparation and score. Report wall includes common input preparation; bounded wall additionally includes interpreter/runner startup and shutdown. Differences are not isolated kernels.',
            'Gaussian preparation is explicitly CPU FP64. Static preparation and evidence also include CPU FP64. FP32 selects bulk arithmetic and disables TF32; it is not an all-FP32 pipeline.',
            'Benchmarks are one cold process per cell on two reused observations. CPU/CUDA ratios below1 mean CUDA was slower here; these are not universal backend speed limits.',
            'Pointwise intervals describe the stated fixed-size trial unit, not a whole searched family. Four conditional draws do not establish90% or99% reception or rare operational false admissions.',
            'Failed signal processing opportunities count as missed opportunities in reliability interpretation; failed or unscored controls prevent qualification.',
        ], 'inherited_limitations': data['limitations']}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--analysis', type=Path, required=True)
    p.add_argument('--plan', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    if args.output.exists():
        p.error('output must be fresh')
    data = json.loads(args.analysis.read_text())
    plan = json.loads(args.plan.read_text())
    result = make_summary(data, plan, args.analysis, args.plan)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, separators=(',', ':'), allow_nan=False) + '\n')
    print(json.dumps({'output': str(args.output), 'sha256': sha(args.output), 'bytes': args.output.stat().st_size,
        'actual_rows': len(result['actual_observations']), 'conditional_curves': len(result['conditional_curves']),
        'unique_failures': len(result['unique_failed_or_unstarted_jobs']), 'benchmarks': len(result['benchmark_runs'])}))


if __name__ == '__main__':
    main()
