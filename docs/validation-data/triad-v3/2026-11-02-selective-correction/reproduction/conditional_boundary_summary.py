#!/usr/bin/env python3
"""Reconcile the audited GPU conditional screen without inventing a sensitivity bound.

All observed fractions retain four physical-path clusters. The 64 generated
noise replicas per path are not independent physical trials. This summary
launches no scientific job and reads no waveform/statistic/score-array bytes.
"""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import sys

import numpy as np

HERE = Path(__file__).resolve().parent
ANALYZER_SHA256 = '45f4960da40c216c07be8a632182756679a315c5d6ad645506fe102b17b7477e'
METHODS = {'static-fft-ucb', 'gaussian-window-ucb'}
PROFILES = ('g001', 'g01', 'g1', 'g001-fastam', 'high10', 'high30')
ARMS = {('legacy-allpass', 'each-25'), ('legacy-allpass', 'each-65'),
        ('dispersive-dft', 'each-25'), ('dispersive-dft', 'each-65'), ('shared-off-bypass', 'off')}
DEPENDENCIES = {'triad-noise-study.py', 'triad-rbpf-study.py', 'triad-cfo-batch-study.py',
                'triad_gaussian_window.py', 'triad-static-frequency-study.py', 'triad_gaussian_state.py',
                'triad_rbpf.py', 'triad_noise_likelihood.py', 'triad_state_workspace.py'}


def require(value, message):
    if not value: raise ValueError(message)


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''): h.update(block)
    return h.hexdigest()


def load(path):
    def reject(value): raise ValueError(f'nonfinite JSON {value}: {path}')
    result = json.loads(Path(path).read_text(), parse_constant=reject)
    require(isinstance(result, dict), 'expected JSON object')
    return result


def fields(control):
    require(control in ('signal', 'null'), 'unexpected conditional control')
    return ('correct_accepted', 'wrong_accepted', 'missed', 'raw_correct') if control == 'signal' else ('false_admitted', 'not_admitted')


def reconcile_counts(per_path, counts, draws, sources, grid):
    """Every path/control/CN0/method is fixed; no confidence model is fitted."""
    tallies = defaultdict(Counter); clusters = defaultdict(dict); seen = set()
    for p in per_path:
        require(p.get('input') in sources and p.get('method') in METHODS and
                p.get('cn0_db_hz') in grid and p.get('trials') == draws and
                type(p.get('truth_bit')) is int and p['truth_bit'] in (0, 1), 'invalid conditional cluster')
        identity = (p['input'], p['control'], p['cn0_db_hz'], p['method'])
        require(identity not in seen, 'duplicate conditional cluster'); seen.add(identity)
        for gate in ('strict', 'relaxed'):
            key = (p['method'], p['control'], p['cn0_db_hz'], gate)
            values = p[gate]; names = fields(p['control'])
            require(set(values) == {'trials', *names} and values['trials'] == draws
                    and all(type(v) is int and 0 <= v <= draws for v in values.values()), 'invalid conditional counts')
            partition = names[:3] if p['control'] == 'signal' else names
            require(sum(values[k] for k in partition) == draws, 'conditional outcomes do not partition replicas')
            tallies[key].update(values); clusters[key][p['input']] = [values[name] for name in names]
    expected = {(s, c, n, m) for s in sources for c in ('signal', 'null') for n in grid for m in METHODS}
    require(seen == expected and len(counts) == len(grid)*8, 'conditional path/control/CN0/method coverage incomplete')
    result = []
    for row in counts:
        key = (row['method'], row['control'], row['cn0_db_hz'], row['gate'])
        require(key in tallies, 'duplicate or unexpected conditional count key')
        actual = dict(tallies.pop(key)); c = row['counts']
        require(all(c.get(k) == v for k, v in actual.items()) and actual['trials'] == len(sources)*draws,
                'conditional totals disagree with path cluster recount')
        require(all(c.get(k) is None for k in ('pointwise_interval', 'outcome_intervals', 'per_bit', 'balanced_bit_mixture_interval'))
                and row.get('independent_unit') == 'none pooled; conditional trials are clustered by physical path',
                'conditional screen acquired an invalid pooled confidence interpretation')
        result.append({'method': row['method'], 'control': row['control'], 'sampled_cn0_db_hz': row['cn0_db_hz'],
                       'gate': row['gate'], 'counts': actual,
                       'conditional_observed_fraction': actual['correct_accepted' if row['control'] == 'signal' else 'false_admitted']/actual['trials'],
                       'path_count_vectors': [clusters[key][source] for source in sources]})
    require(not tallies, 'unmatched conditional outcome tallies')
    return sorted(result, key=lambda x: (x['method'], x['control'], x['gate'], x['sampled_cn0_db_hz']))


def weakest_tested(rows):
    result = []
    for method in sorted(METHODS):
        for gate in ('strict', 'relaxed'):
            points = sorted((r for r in rows if r['method'] == method and r['gate'] == gate and r['control'] == 'signal'),
                            key=lambda r: r['sampled_cn0_db_hz'])
            for target in (.90, .99):
                passing = [r for r in points if r['conditional_observed_fraction'] >= target]
                first = passing[0] if passing else None
                result.append({'method': method, 'gate': gate, 'target_fraction': target,
                               'weakest_tested_passing_cn0_db_hz': first['sampled_cn0_db_hz'] if first else None,
                               'correct_count_at_that_point': first['counts']['correct_accepted'] if first else None,
                               'replica_count': first['counts']['trials'] if first else None,
                               'all_stronger_tested_points_also_pass': all(r['conditional_observed_fraction'] >= target
                                    for r in points if first and r['sampled_cn0_db_hz'] >= first['sampled_cn0_db_hz']) if first else None,
                               'scope': 'Conditional observed fraction on four retained physical paths only; no confidence bound, interpolation, practical acquisition claim, or qualified sensitivity boundary.'})
    return result


def verify_config(report, job, draws, device, width):
    c = report['configuration']
    expected = {'mode': 'conditional', 'noise_trials': draws, 'trial_batch': draws, 'precision': 'fp64',
                'device': device, 'seed': 231207, 'selection_methods': 16, 'duration_alternatives': 8,
                'calibration_alternatives': 2, 'attempt_spacing': 60., 'window_blocks': 64,
                'window_doppler_width': width, 'horizon_seconds': 86400., 'false_probability': .0001,
                'correct_reception_target': .99, 'controls': ['signal', 'null'],
                'receiver_methods': ['static-fft-ucb', 'gaussian-window-ucb']}
    require(all(c.get(k) == v for k, v in expected.items()), 'conditional batch, receiver prior, arithmetic, or calibrated family changed')
    require(report.get('arithmetic', {}).get('tf32_allowed') is False, 'unexpected TF32 arithmetic')
    require(set(report.get('dependency_sha256', {})) == DEPENDENCIES, 'incomplete receiver dependency inventory')
    require(report.get('observation_mode') == 'conditional', 'receiver report is not conditional')
    strict = math.log(math.ceil(86400/60)*16*8*2/(.0001/2))
    require(math.isclose(report['family']['fixed_window_log_threshold'], strict, rel_tol=0, abs_tol=1e-12),
            'strict gate differs from frozen family')
    sources = job['argv'][2:job['argv'].index('--output')]
    require([p['input'] for p in report['paths']] == sources, 'receiver report source order differs from plan')
    seed_records = []
    for index, p in enumerate(report['paths']):
        require(p.get('status') == 'completed' and p.get('physical_path_index') == index, 'failed or reordered conditional physical path')
        expected_seed = int(np.random.SeedSequence([231207, index, 0]).generate_state(1, dtype=np.uint64)[0])
        for condition in p['conditions']:
            batches = condition['draws_simulation_only']
            require(len(batches) == 1 and batches[0].get('noise_seed') == expected_seed
                    and batches[0].get('conditional_noise_trials') == draws
                    and condition.get('noise_trials') == draws, 'conditional fixed-batch noise address changed')
        seed_records.append({'source': p['input'], 'physical_path_index': index, 'conditional_noise_seed': expected_seed,
                             'sampler_input_hashes': p['sampled_input_hashes']})
    return seed_records


def summarize(analysis, plan, draws, device):
    profiles = {p['name']: p for p in plan['profiles']}
    cell_by_source = {tuple(c['sources']): c for c in plan['cells']}
    jobs = {j['name']: j for j in plan['scoring_jobs']}
    summaries = []; receiver_identity = None
    for score in analysis['receiver_score_summaries']:
        require(score.get('observation_mode') == 'conditional' and score.get('all_input_hash_receipts_reverified') is True
                and not score.get('failed_physical_paths') and score.get('completed_physical_paths') == 4,
                'conditional score is incomplete, unverified, or failed')
        job = jobs.pop(score['name']); argv = job['argv']; sources = argv[2:argv.index('--output')]
        cell = cell_by_source[tuple(sources)]
        require(score['profile'] == cell['profile'] and score['model'] == cell['selective_model']
                and score['modulation'] == cell['modulation'], 'audited cell label differs from frozen plan')
        require(score['profile'] in PROFILES and (score['model'], score['modulation']) in ARMS and len(sources) == 4,
                'unexpected conditional physical arm')
        report_path = Path(score['report'])
        require(sha256(report_path) == score['report_sha256'], 'conditional receiver report SHA mismatch')
        report = load(report_path)
        identity = {'source_sha256': report['source_sha256'], 'dependency_sha256': report['dependency_sha256']}
        if receiver_identity is None: receiver_identity = identity
        require(identity == receiver_identity, 'receiver implementation differs across conditional arms')
        seed_records = verify_config(report, job, draws, device, profiles[score['profile']]['width'])
        grid = report['configuration']['cn0_grid']
        rows = reconcile_counts(score['per_path'], score['counts'], draws, sources, grid)
        summaries.append({'profile': score['profile'], 'model': score['model'], 'modulation': score['modulation'],
                          'physical_paths_ordered': sources, 'draws_per_physical_path': draws,
                          'cn0_grid_db_hz': grid, 'conditional_rows': rows,
                          'weakest_tested_conditional_points': weakest_tested(rows),
                          'noise_address_and_basis_receipts': seed_records,
                          'receiver_report_sha256': score['report_sha256'], 'score_archive_sha256': score['score_archive_sha256']})
    require(not jobs and len(summaries) == 30 and
            {(c['profile'], c['model'], c['modulation']) for c in summaries} ==
            {(p, m, d) for p in PROFILES for m, d in ARMS}, 'conditional 30-cell arm inventory incomplete')
    return summaries, receiver_identity


def pairing(gpu_cells, analysis):
    require(analysis['paired_path_count'] == len(analysis['paired_model_comparisons']) == 48, 'missing paired-model physical evidence')
    for p in analysis['paired_model_comparisons']:
        q = p['paired_noise_projection']
        require(p['equal_calendar'] is True and all(q['basis_gram_intervals_validity_exactly_equal'].get(k) is True
                for k in ('gram.c128', 'intervals.u64', 'valid_rows.u64')),
                'paired legacy/new bases or calendars differ')
        require(q['observation_minus_mean_projected_noise_max_abs_difference'] <=
                q['relative_tolerance']*max(1., q['maximum_projected_noise_magnitude']), 'paired retained actual noise test failed')
    by_profile = defaultdict(list)
    for c in gpu_cells: by_profile[c['profile']].append(c)
    result = []
    for profile in PROFILES:
        arms = by_profile[profile]
        for index in range(4):
            base = arms[0]['noise_address_and_basis_receipts'][index]
            for arm in arms[1:]:
                other = arm['noise_address_and_basis_receipts'][index]
                require(base['conditional_noise_seed'] == other['conditional_noise_seed'] and
                        all(base['sampler_input_hashes'][k] == other['sampler_input_hashes'][k]
                            for k in ('gram.c128', 'intervals.u64', 'valid_rows.u64')),
                        'GPU arms do not share paired noise addresses and public projection bases')
        result.append({'profile': profile, 'arms': len(arms), 'physical_path_positions': 4,
                       'same_batch64_seed_addresses': True, 'same_public_gram_intervals_validity_hashes': True,
                       'scope': 'Common conditional noise addresses on equal public bases within GPU profile arms; source/path realizations are reused. CPU batch16 individual draws are not paired with GPU batch64.'})
    return result


def comparison(cpu_cells, gpu_cells):
    old = {(c['profile'], c['model'], c['modulation']): c for c in cpu_cells}
    result = []
    for c in gpu_cells:
        baseline = old[(c['profile'], c['model'], c['modulation'])]
        require(c['physical_paths_ordered'] == baseline['physical_paths_ordered'], 'CPU/GPU screens use different physical paths')
        prior = {(r['method'], r['control'], r['gate'], r['sampled_cn0_db_hz']): r for r in baseline['conditional_rows']}
        matches = []
        for r in c['conditional_rows']:
            key = (r['method'], r['control'], r['gate'], r['sampled_cn0_db_hz'])
            if key not in prior: continue
            p = prior[key]
            matches.append({'method': key[0], 'control': key[1], 'gate': key[2], 'cn0_db_hz': key[3],
                            'cpu_batch16_counts': p['counts'], 'gpu_batch64_counts': r['counts']})
        result.append({'profile': c['profile'], 'model': c['model'], 'modulation': c['modulation'],
                       'shared_tested_points': matches,
                       'scope': 'Same four physical paths and receiver implementation; independent noise-address grouping across batch sizes. Count differences are not paired individual-trial changes or a measured GPU sensitivity gain.'})
    return result


def bind_analysis(path, expected_sha, status_path, root):
    require(sha256(path) == expected_sha, 'conditional analysis differs from explicitly frozen input SHA')
    a, status = load(path), load(status_path)
    require(a.get('schema') == 'triad-selective-model-correction-analysis-v1' and a.get('analyzer_sha256') == ANALYZER_SHA256
            and a.get('comparison_scope') == 'paired-legacy-corrected-model-comparison' and a.get('workspace_count') == 120
            and a.get('execution_receipts_complete') is True, 'unsupported/incomplete accepted conditional analysis')
    require(status.get('schema') == 'triad-bounded-run-v1' and status.get('outcome') == 'completed'
            and status.get('exit_code') == status.get('child_exit_code') == 0 and status.get('timed_out') is False
            and status.get('cancelled') is False and status.get('termination', {}).get('group_empty') is True,
            'conditional analyzer did not complete cleanly')
    for key in ('plan', 'score_plan'):
        require(sha256(a[key]) == a[key+'_sha256'], 'conditional frozen plan SHA mismatch')
    for receipt in a['runner_receipts']:
        p = Path(receipt['path']); p = p if p.is_absolute() else root/p
        require(sha256(p) == receipt['sha256'], 'completed execution receipt SHA mismatch')
    return a, load(a['score_plan'])


def run(args):
    root = args.root.resolve()
    require(sha256(HERE/'analyze.py') == ANALYZER_SHA256, 'accepted analyzer source changed')
    gpu, gpu_plan = bind_analysis(args.analysis, args.expected_analysis_sha256, args.status, root)
    cpu, cpu_plan = bind_analysis(args.cpu_analysis, args.expected_cpu_analysis_sha256, args.cpu_status, root)
    require(gpu['stage'] == 'gpu-boundary-conditional' and cpu['stage'] == 'development', 'unexpected CPU/GPU stages')
    gpu_cells, gpu_identity = summarize(gpu, gpu_plan, 64, 'cuda:0')
    cpu_cells, cpu_identity = summarize(cpu, cpu_plan, 16, 'cpu')
    require(gpu_identity == cpu_identity, 'CPU/GPU conditional screens changed receiver source/dependencies')
    paired = pairing(gpu_cells, gpu)
    profiles = []
    for p in gpu_plan['profiles']:
        profiles.append({**p, 'gpu_tested_cn0_db_hz': gpu_plan['conditional_boundary']['grid_by_profile'][p['name']],
                         'block_times_gaussian_width': p['block']*p['width'],
                         'block_times_ray_doppler_separation': p['block']*p['doppler'],
                         'delay_over_block_duration': p['delay_ms']*.001/p['block']})
    wrong = [{'profile': c['profile'], 'model': c['model'], 'modulation': c['modulation'], **r}
             for c in gpu_cells for r in c['conditional_rows'] if r['control'] == 'signal' and r['counts']['wrong_accepted']]
    result = {'schema': 'triad-selective-conditional-boundary-summary-v1', 'state': 'audited-conditional-reconciliation-passed',
              'scientific_qualification': False, 'summary_source_sha256': sha256(Path(__file__)),
              'inputs': {'gpu': {'path': str(args.analysis.resolve()), 'sha256': args.expected_analysis_sha256,
                                 'status_sha256': sha256(args.status), 'plan_sha256': gpu['plan_sha256']},
                         'cpu': {'path': str(args.cpu_analysis.resolve()), 'sha256': args.expected_cpu_analysis_sha256,
                                 'status_sha256': sha256(args.cpu_status), 'plan_sha256': cpu['plan_sha256'], 'score_plan_sha256': cpu['score_plan_sha256']}},
              'accepted_analyzer_sha256': ANALYZER_SHA256, 'receiver_implementation': gpu_identity,
              'raw_arrays_reverified_by_accepted_analyzer': True, 'physical_workspace_count': 120,
              'gpu_physical_cells': 30, 'gpu_points_per_cell': 4, 'gpu_replica_design': {'physical_paths': 4, 'draws_per_path': 64, 'total_replicas': 256},
              'path_count_vector_fields': {'signal': list(fields('signal')), 'null': list(fields('null'))},
              'calibration': {'receiver_alternatives': 16, 'duration_alternatives': 8, 'calibration_alternatives': 2,
                              'attempt_spacing_seconds': 60, 'strict_log_gate': math.log(math.ceil(86400/60)*16*8*2/(.0001/2)),
                              'relaxed_log_gate': math.log(math.ceil(43200/60)*16*8*2/(.01/2)),
                              'duration_change_in_this_screen': False, 'relaxed_calibration': 'Original strict calibration retained; saved scores only re-gated.'},
              'profile_geometry': profiles, 'gpu_cells': gpu_cells, 'gpu_arm_pairing': paired,
              'cpu_batch16_vs_gpu_batch64': comparison(cpu_cells, gpu_cells), 'signal_wrong_admission_points': wrong,
              'scope': [
                  'Every point is an observed conditional fraction over 4 fixed physical paths x 64 generated noise replicas, not 256 independent physical observations. No CP or other pooled physical-path confidence is supplied.',
                  'Weakest tested passing points describe conditional point fractions only; they are not an interpolated or qualified sensitivity boundary, 90/99% guarantee, or universal optimality claim.',
                  'All rates, powers, pattern durations, physical Doppler/delay, oscillator realizations, and selective modulation remain fixed per profile. This sweep changes injected C/N0 only.',
                  'The same batch64 noise addresses and public Gram/calendar bases support within-GPU model/depth pairing. CPU batch16 and GPU batch64 have different individual noise draws; their counts are descriptive comparisons.',
                  'Nominal timing, frequency, clock and epoch banks remain unsearched. High10/high30 losses and wrong admissions may reflect timing mismatch, block compression and final-channel model mismatch; no fundamental Doppler limit is established.',
                  'Selective pre/post energy and noiseless projection are separate from nominal injected C/N0. No energy gain is silently treated as a noise-density improvement.',
                  'Zero null admissions in these clustered screens do not empirically qualify continuous daily false admissions. There are no wrong-key controls in this conditional matrix.',
                  'Raw waveform/statistic/NPZ bytes are not reopened by this compact summary; the frozen accepted full audit supplies those bindings. Receiver JSON metadata and saved count strata are independently reconciled here.',
              ]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n')
    return result


def self_test():
    points = [{'method': m, 'gate': g, 'control': 'signal', 'sampled_cn0_db_hz': n,
               'conditional_observed_fraction': f, 'counts': {'correct_accepted': int(f*100), 'trials': 100}}
              for m in METHODS for g in ('strict', 'relaxed') for n, f in ((-20., .5), (-19., .95), (-18., 1.))]
    frontier = weakest_tested(points)
    require(all(r['weakest_tested_passing_cn0_db_hz'] == (-19. if r['target_fraction'] == .9 else -18.) for r in frontier),
            'weakest tested point selection failed')
    points[-1]['conditional_observed_fraction'] = .1
    require(any(r['all_stronger_tested_points_also_pass'] is False for r in weakest_tested(points)),
            'nonmonotone stronger-point failure was hidden')
    try: reconcile_counts([], [], 64, ['a', 'b', 'c', 'd'], [-20.])
    except ValueError: pass
    else: raise ValueError('missing clusters accepted')
    print('conditional weakest-point/nonmonotone/missing-cluster self-tests passed')


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--analysis', type=Path); p.add_argument('--expected-analysis-sha256'); p.add_argument('--status', type=Path)
    p.add_argument('--cpu-analysis', type=Path); p.add_argument('--expected-cpu-analysis-sha256'); p.add_argument('--cpu-status', type=Path)
    p.add_argument('--root', type=Path, default=HERE.parents[4])
    p.add_argument('--output', type=Path, default=HERE/'conditional-boundary-summary.json'); p.add_argument('--self-test', action='store_true')
    a = p.parse_args(argv)
    try:
        if a.self_test: self_test(); return 0
        require(all((a.analysis, a.expected_analysis_sha256, a.status, a.cpu_analysis, a.expected_cpu_analysis_sha256, a.cpu_status)), 'all GPU/CPU analysis SHA and status arguments are required')
        result = run(a)
    except (ValueError, KeyError, OSError, TypeError) as error:
        print(f'conditional boundary summary failed: {error}', file=sys.stderr); return 1
    print(json.dumps({'state': result['state'], 'cells': result['gpu_physical_cells'], 'output': str(a.output), 'sha256': sha256(a.output)}))
    return 0


if __name__ == '__main__': raise SystemExit(main())
