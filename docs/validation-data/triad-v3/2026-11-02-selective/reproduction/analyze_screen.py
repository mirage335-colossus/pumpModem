#!/usr/bin/env python3
"""Compact, plan-bound recount of conditional/actual selective-channel evidence.

--root LOCAL_EVIDENCE --score-plan PLAN --output SUMMARY.json
[--source-root ORIGINAL_EVIDENCE_PREFIX] [--retention REPORT.json ...]
[--verify-input-arrays]

Conditional replicas stay clustered by physical path. This development summary
never qualifies reception or daily false admissions. Missing jobs/paths and
processing failures remain explicit; no failed score is invented.
"""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
import importlib.util
import json
import math
import os
from pathlib import Path
import sys

import numpy as np


def _module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module


HERE = Path(__file__).resolve().parent
REFERENCE_PATH = HERE.parents[1] / '2026-11-02-spread' / 'analyze.py'
REFERENCE = _module('selective_screen_reference', REFERENCE_PATH)
RETENTION = _module('selective_screen_retention', HERE / 'retention.py')
require = REFERENCE.require
read_json = REFERENCE.read_json
sha256 = REFERENCE.sha256
CONTROLS = ('signal', 'null', 'wrong-key')


def _jsonable(value):
    return 'infinity' if type(value) is float and value == math.inf else value


def parse_job(job):
    argv = job['argv']; require(isinstance(argv, list) and len(argv) > 3, 'invalid planned argv')
    leading, options = REFERENCE.parse_argv(argv)
    require(len(leading) >= 3 and Path(leading[1]).name == 'triad-conditional-sweep.py',
            'only conditional-sweep reports are supported by this focused analyzer')
    require('--output' in options and len(options['--output']) == 1, 'planned output missing')
    require(len(leading[2:]) == len(set(leading[2:])), 'duplicate planned workspace')
    return leading[2:], options, options['--output'][0]


class Locations:
    def __init__(self, root, plan, source_root=None):
        self.root = Path(root).resolve()
        all_paths = [p for job in plan['jobs'] for inputs, _, output in [parse_job(job)] for p in [*inputs, output]]
        require(all(Path(p).is_absolute() and '..' not in Path(p).parts and str(Path(p)) == p for p in all_paths), 'frozen paths must be canonical absolute paths')
        self.source_root = Path(source_root) if source_root else Path(os.path.commonpath(all_paths))
        require(self.source_root.is_absolute() and self.source_root != Path('/'), 'provide an unambiguous source root')
        for path in all_paths:
            self.resolve(path)

    def resolve(self, value):
        relative = Path(value).relative_to(self.source_root)
        path = (self.root / relative).resolve()
        require(path.is_relative_to(self.root), 'planned path escapes local evidence')
        return path


def _check_options(options, configuration):
    for flag, values in options.items():
        if flag == '--output':
            continue
        field = flag[2:].replace('-', '_')
        if field == 'methods':
            field = 'receiver_methods'
        require(field in configuration, f'explicit planned option unrecorded: {flag}')
        actual = configuration[field]
        expected = [] if actual is True and not values else [str(_jsonable(v)) for v in actual] if isinstance(actual, list) else [str(_jsonable(actual))]
        expected = ['inf' if value == 'infinity' else value for value in expected]
        if flag == '--cn0-grid':
            values = values[0].split(',')
        require(REFERENCE.option_equal(values, expected), f'planned option/configuration mismatch: {flag}')


def _manifest(locations, source, record, verify_arrays):
    directory = locations.resolve(source); path = directory / 'manifest.json'
    require(path.is_file(), f'evaluation manifest missing: {source}')
    manifest = read_json(path)
    require(manifest.get('format') == 'triad-block-stats-v1', 'unsupported evaluation manifest')
    hashes = record.get('input_hashes', record.get('sampled_input_hashes', {}))
    require(isinstance(hashes, dict) and 'manifest.json' in hashes, 'manifest hash receipt missing')
    checks = []
    for name, expected in hashes.items():
        require(isinstance(name, str) and Path(name).name == name and name not in ('.', '..'), 'unsafe input filename')
        require(isinstance(expected, str) and len(expected) == 64 and all(c in '0123456789abcdef' for c in expected), 'invalid input hash')
        file = directory / name
        if name == 'manifest.json' or verify_arrays:
            require(file.is_file(), f'input file missing for requested verification: {file}')
            require(sha256(file) == expected, f'input hash mismatch: {file}')
            checks.append({'file': name, 'sha256': expected, 'state': 'verified'})
        else:
            checks.append({'file': name, 'sha256': expected, 'state': 'recorded_receipt_not_rehashed'})
    return manifest, checks


def _seed_identity(manifest):
    roots = REFERENCE.physical_seeds(manifest)
    if roots is None or type(manifest.get('key_seed')) is not int:
        return None
    return {'key': manifest['key_seed'], **roots}


def _confidence(by_bit):
    """Pointwise equal-bit-population lower bound, allowing p0 != p1."""
    n = [by_bit[str(bit)]['trials'] for bit in (0, 1)]
    k = [by_bit[str(bit)]['correct_accepted'] for bit in (0, 1)]
    if not all(n):
        return None
    per_bit = {str(bit): REFERENCE.cp(k[bit], n[bit]) for bit in (0, 1)}
    if n[0] == n[1] and k == n:
        lower = .05 ** (1 / sum(n)); method = 'all-success equal-stratum AM-GM bound'
    else:
        from scipy.stats import beta
        lower = sum(float(beta.ppf(.025, k[b], n[b]-k[b]+1)) if k[b] else 0. for b in (0, 1)) / 2
        method = 'mean of per-bit one-sided97.5%CP bounds; Bonferroni pointwise95% coverage'
    return {'per_bit_pointwise_cp': per_bit, 'balanced_mean_correct_probability_lower_95': lower,
            'method': method, 'scope': 'Equal source-bit population; independent iid outcomes within each fixed bit stratum assumed. Pointwise only, not simultaneous across curves, C/N0 choices, methods or gates.'}


def _groups(rows, failures, mode):
    grouped = defaultdict(list)
    for row in rows:
        grouped[(row['receiver'], row['control'], row['cn0_db_hz'])].append(row)
    output = []
    for (method, control, cn0), selected in sorted(grouped.items(), key=lambda x: str(x[0])):
        cluster_ids = [r['input'] for r in selected]
        require(len(cluster_ids) == len(set(cluster_ids)), 'duplicated physical-path condition')
        applicable = [f for f in failures if mode == 'conditional' or
                      (f.get('cn0_db_hz') in (None, cn0) and f.get('control') in (None, control))]
        unknown = [f for f in applicable if f.get('control') not in CONTROLS or f.get('cn0_db_hz') is None]
        identities = [r.get('seed_roots') for r in selected]
        if mode == 'actual-noisy' and control == 'signal':
            identities += [f.get('seed_roots') for f in applicable]
        independent = all(x is not None for x in identities) and all(len({x[k] for x in identities}) == len(identities) for k in identities[0]) if identities else False
        entry = {'receiver': method, 'control': control, 'cn0_db_hz': cn0,
                 'observation_mode': mode, 'completed_physical_paths': len(selected),
                 'failed_or_missing_physical_paths': len(applicable),
                 'attempt_denominator_established': not unknown,
                 'distinct_recorded_seed_roots_within_point': independent,
                 'clustered_by_physical_path': mode == 'conditional', 'gates': {}}
        for gate in ('strict', 'relaxed'):
            counts = Counter(); by_bit = {str(b): Counter() for b in (0, 1)}
            for row in selected:
                counts.update(row[gate]);
                if control == 'signal':
                    by_bit[str(row['truth_bit'])].update(row[gate])
            observed_counts = dict(counts)
            confidence = None
            if mode == 'actual-noisy' and control == 'signal':
                for failure in applicable:
                    counts['trials'] += 1; counts['missed'] += 1
                    truth = failure.get('truth_bit')
                    if type(truth) is int and truth in (0, 1):
                        by_bit[str(truth)]['trials'] += 1; by_bit[str(truth)]['missed'] += 1
                known_bits = sum(x['trials'] for x in by_bit.values()) == counts['trials']
                if not unknown and known_bits and independent:
                    confidence = _confidence(by_bit)
            entry['gates'][gate] = {'counts': dict(counts), 'observed_completed_counts': observed_counts,
                'per_bit_counts': {b: dict(v) for b, v in by_bit.items()} if control == 'signal' else None,
                'actual_balanced_mean_confidence': confidence,
                'conditional_pooled_cp': None,
                'scope': ('Conditional noise replicas are clustered by physical path; no pooled binomial confidence interval.' if mode == 'conditional' else
                    'Known signal processing failures count as misses. Unknown controls/unknown bit labels leave attempt/confidence denominators unestablished. Source-root distinctness records design, not PRNG/field independence proof.')}
        entry['physical_path_clusters'] = [{'input': r['input'], 'truth_bit': r['truth_bit'],
            'conditional_trials': r['noise_trials'], 'seed_roots': r['seed_roots'],
            'strict': r['strict'], 'relaxed': r['relaxed']} for r in selected]
        output.append(entry)
    return output


def _retention_index(paths, locations):
    result = {}; provenance = []
    for path in paths:
        doc = read_json(Path(path)); require(doc.get('schema') == 'triad-selective-retention-v1', 'unsupported retention report')
        provenance.append({'path': str(path), 'sha256': sha256(Path(path)), 'passed': doc.get('passed')})
        for record in doc['workspaces']:
            raw = Path(record['input'])
            try:
                local = locations.resolve(raw)
            except ValueError:
                local = raw.resolve()
            require(local.is_relative_to(locations.root), 'retention workspace is outside evidence root')
            key = str(local)
            require(key not in result, 'duplicate retention workspace')
            result[key] = record
    return result, provenance



def _bind_retention(record, receiver_checks, directory, verify_arrays):
    """Bind diagnostic receipts without treating its truth arrays as RX inputs."""
    hashes = record.get('input_sha256')
    require(isinstance(hashes, dict) and 'manifest.json' in hashes,
            'retention input-hash receipts missing')
    receiver = {item['file']: item for item in receiver_checks}
    require('manifest.json' in receiver, 'receiver manifest hash receipt missing')
    overlap = sorted(set(hashes) & set(receiver))
    diagnostics = []
    for name, expected in hashes.items():
        require(isinstance(name, str) and Path(name).name == name and name not in ('', '.', '..'),
                'unsafe retention input filename')
        require(isinstance(expected, str) and len(expected) == 64
                and all(value in '0123456789abcdef' for value in expected),
                'invalid retention input SHA256')
        if name in receiver:
            require(receiver[name]['sha256'] == expected,
                    f'retention/receiver input hash receipts disagree: {name}')
            state = receiver[name]['state']
            role = 'receiver_and_retention'
        else:
            state = 'recorded_retention_receipt_not_rehashed'
            role = 'retention_only_simulation_diagnostic'
        # Actual reception does not read mean/signal-energy files. Their
        # optional diagnostic receipts need their own explicit verification.
        if name == 'manifest.json' or verify_arrays:
            path = Path(directory) / name
            require(path.is_file(), f'retention input missing for requested verification: {path}')
            require(sha256(path) == expected, f'retention input hash mismatch: {name}')
            state = 'verified'
        diagnostics.append({'file': name, 'sha256': expected, 'role': role, 'state': state})
    return {'state': 'overlapping_hash_receipts_match', 'overlapping_input_files': overlap,
            'retention_only_input_files': sorted(set(hashes) - set(receiver)),
            'input_file_checks': diagnostics,
            'scope': 'All overlapping receiver/retention hash receipts agree. Only manifest bytes are rehashed by default; --verify-input-arrays also rehashes retention-only files. Noiseless means and signal energies are simulation diagnostics and are not actual receiver inputs. Receipt equality alone does not verify omitted raw bytes.'}

def analyze_job(job, locations, retention, verify_arrays=False):
    inputs, options, output = parse_job(job); report_path = locations.resolve(output)
    result = {'name': job['name'], 'report': str(report_path.relative_to(locations.root)),
              'planned_physical_paths': len(inputs), 'planned_inputs': inputs, 'scientific_qualification': False}
    require(report_path.is_file(), f'planned receiver report missing: {report_path}')
    doc = read_json(report_path); require(doc.get('schema') == 'triad-conditional-sweep-v1', 'unexpected receiver schema')
    mode = doc.get('observation_mode'); require(mode in ('conditional', 'actual-noisy'), 'invalid observation mode')
    require(mode == options.get('--mode', ['conditional'])[0], 'observation mode differs from frozen plan/default')
    configuration = doc['configuration']; _check_options(options, configuration)
    require(configuration.get('mode') == mode, 'configuration/observation mode mismatch')
    methods = configuration['receiver_methods']
    require(isinstance(methods, list) and methods and len(set(methods)) == len(methods), 'invalid declared receiver methods')
    gates = REFERENCE.gates(configuration)
    score_name = doc['scores_file']; require(isinstance(score_name, str) and Path(score_name).name == score_name, 'unsafe score filename')
    scorepath = report_path.parent / score_name
    require(scorepath.is_file() and sha256(scorepath) == doc['scores_sha256'], 'score NPZ missing or hash mismatch')
    records = doc['paths']; require(isinstance(records, list), 'missing physical-path records')
    recorded = [r['input'] for r in records]
    require(len(recorded) == len(set(recorded)), 'duplicate report input')
    missing = [p for p in inputs if p not in recorded]; unexpected = [p for p in recorded if p not in inputs]
    require(not unexpected, 'receiver report contains unplanned physical paths')
    require(recorded == [p for p in inputs if p in recorded], 'receiver input order differs from frozen plan')
    rows = []; failures = []; details = []; used = set(); indices = set(); raw_checks = []
    with np.load(scorepath, allow_pickle=False) as archive:
        for record in records:
            source = record['input']; index = record['physical_path_index']
            require(type(index) is int and index >= 0 and index not in indices, 'duplicate/invalid physical-path index'); indices.add(index)
            failed = record['status'] != 'completed'; manifest = None; metadata_error = None
            try:
                manifest, checks = _manifest(locations, source, record, verify_arrays); raw_checks.extend(checks)
            except (OSError, ValueError, KeyError) as error:
                if not failed:
                    raise
                metadata_error = str(error)
            truth = manifest.get('signal_bit') if manifest else record.get('truth_bit_simulation_only')
            recorded_truth = record.get('truth_bit_simulation_only')
            require(recorded_truth is None or truth == recorded_truth, 'manifest/evaluation truth mismatch')
            control = record.get('control') or (manifest.get('control', 'signal') if manifest else None)
            if failed:
                failures.append({'input': source, 'status': record['status'], 'control': control,
                    'cn0_db_hz': manifest.get('cn0_db_hz') if manifest else None, 'truth_bit': truth,
                    'error_type': record.get('error_type'), 'error': record.get('error'),
                    'manifest_error': metadata_error, 'seed_roots': _seed_identity(manifest) if manifest else None, 'partial_conditions_excluded': len(record.get('conditions', []))})
            conditions = record.get('conditions', []); require(failed or conditions, 'completed path has no scored conditions')
            if mode == 'actual-noisy' and not failed:
                require(len(conditions) == 1, 'actual path must have one condition')
            seen_conditions = set()
            for condition in conditions:
                condition_control = condition['control']; require(condition_control in CONTROLS, 'invalid condition control')
                cn0 = condition['cn0_db_hz'] if mode == 'conditional' else manifest.get('cn0_db_hz') if manifest else condition.get('cn0_db_hz')
                require(type(cn0) in (int, float) and math.isfinite(cn0), 'invalid condition C/N0')
                require((cn0, condition_control) not in seen_conditions, 'duplicate path condition'); seen_conditions.add((cn0, condition_control))
                n = condition['noise_trials']; require(type(n) is int and n > 0, 'invalid condition trial count')
                if mode == 'conditional':
                    require(n <= configuration['noise_trials'] if failed else n == configuration['noise_trials'], 'conditional trial count differs from frozen configuration')
                if mode == 'actual-noisy':
                    require(n == 1 and not condition.get('draws_simulation_only'), 'actual path cannot contain generated noise replicas')
                    require(manifest is None or manifest.get('control', 'signal') == condition_control, 'actual control differs from manifest')
                returned = set(condition['methods']); require(returned <= set(methods), 'undeclared method result')
                require(failed or returned == set(methods), 'completed condition omitted a receiver method')
                for method, item in condition['methods'].items():
                    key = item['score_array']; require(key in archive and key not in used, 'missing/reused score array'); used.add(key)
                    scores = archive[key]; require(scores.ndim == 2 and scores.shape == (n, 2) and np.isfinite(scores).all(), 'invalid score matrix')
                    require(math.isclose(item['threshold'], gates['strict'], rel_tol=0, abs_tol=1e-10), 'wrong frozen strict gate')
                    strict = REFERENCE.recount(scores, truth, condition_control, gates['strict'], 'trials')
                    require(strict == item['counts'], 'saved score recount disagrees with reported counts')
                    relaxed = REFERENCE.recount(scores, truth, condition_control, gates['relaxed'], 'trials')
                    if not failed:
                        rows.append({'input': source, 'receiver': method, 'control': condition_control,
                            'cn0_db_hz': cn0, 'truth_bit': truth, 'noise_trials': n,
                            'seed_roots': _seed_identity(manifest), 'strict': strict, 'relaxed': relaxed})
            if not failed and mode == 'conditional':
                expected = {(float(c), control) for c in configuration['cn0_grid'] for control in configuration['controls']}
                require(seen_conditions == expected, 'completed path omitted/added a declared C/N0-control condition')
            if manifest is not None:
                local = locations.resolve(source); retained = retention.get(str(local))
                retention_binding = None
                if retained is not None and retained.get('status') == 'completed':
                    retention_binding = _bind_retention(retained, checks, local, verify_arrays)
                elif retained is not None:
                    retention_binding = {'state': 'retention_analysis_failed_not_bound',
                                         'scope': 'No completed diagnostic receipt is available; no raw verification is implied.'}
                details.append({'input': source, 'status': record['status'],
                    'manifest_sha256': sha256(local / 'manifest.json'),
                    'physical_configuration': REFERENCE.manifest_metadata(manifest),
                    'selective_stage_energy_simulation_only': RETENTION.selective_energy_telemetry(manifest),
                    'projection_retention_simulation_only': retained,
                    'projection_retention_binding': retention_binding,
                    'energy_scope': 'Per-realization measured channel energy; nominal injected C/N0 is never replaced or corrected by this energy.'})
        require(set(archive.files) == used, 'unaccounted score arrays in NPZ')
    for source in missing:
        path = locations.resolve(source) / 'manifest.json'
        manifest = read_json(path) if path.exists() else {}
        failures.append({'input': source, 'status': 'missing_report_record', 'control': manifest.get('control'),
                         'cn0_db_hz': manifest.get('cn0_db_hz'), 'truth_bit': manifest.get('signal_bit'),
                         'error': 'Planned physical path omitted from receiver report'})
    declared_failures = sum(r['status'] != 'completed' for r in records)
    require(doc.get('failed_physical_path_records', declared_failures) == declared_failures, 'reported failure count mismatch')
    require(doc.get('sampled_physical_paths', len(records)) == len(records), 'reported path count mismatch')
    groups = _groups(rows, failures, mode)
    return {**result, 'status': 'completed' if not failures else 'incomplete',
        'coverage_complete': not missing and len(records) == len(inputs), 'report_sha256': sha256(report_path),
        'scores_sha256': doc['scores_sha256'], 'source_sha256': doc.get('source_sha256'),
        'dependency_sha256': doc.get('dependency_sha256'), 'configuration': configuration, 'observation_mode': mode,
        'gates': gates, 'groups': groups, 'physical_paths': details, 'failures': failures,
        'input_file_checks': raw_checks,
        'plan_binding_scope': 'Exact planned input order and every explicit command option checked against recorded configuration; source/dependency hashes retained. This is not a prior timestamped generation/default/binary commitment.'}


def analyze(root, score_plan, retention_paths=(), source_root=None, verify_arrays=False):
    plan = read_json(Path(score_plan)); require(isinstance(plan.get('jobs'), list) and plan['jobs'], 'plan has no jobs')
    require(len({j['name'] for j in plan['jobs']}) == len(plan['jobs']), 'duplicate planned job name')
    outputs = [parse_job(j)[2] for j in plan['jobs']]; require(len(outputs) == len(set(outputs)), 'duplicate planned scoring output')
    locations = Locations(root, plan, source_root); retention, retention_sources = _retention_index(retention_paths, locations)
    cases = []
    for job in plan['jobs']:
        try:
            case = analyze_job(job, locations, retention, verify_arrays)
        except Exception as error:
            case = {'name': job.get('name'), 'status': 'error', 'error_type': type(error).__name__,
                    'error': str(error), 'planned_physical_paths': len(parse_job(job)[0]),
                    'scientific_qualification': False}
        cases.append(case)
    curves = []
    for case in cases:
        for method in sorted({g['receiver'] for g in case.get('groups', [])}):
            points = []
            for group in sorted((g for g in case['groups'] if g['receiver'] == method and g['control'] == 'signal'), key=lambda g: g['cn0_db_hz']):
                points.append({'cn0_db_hz': group['cn0_db_hz'], 'completed_physical_paths': group['completed_physical_paths'],
                    'failed_or_missing_physical_paths': group['failed_or_missing_physical_paths'],
                    **{gate: group['gates'][gate]['counts'] for gate in ('strict', 'relaxed')}})
            curves.append({'case': case['name'], 'observation_mode': case['observation_mode'], 'receiver': method,
                           'points': points, 'scope': 'Development transition points; no automatic sensitivity qualification or interpolation.'})
    return {'schema': 'triad-selective-screen-analysis-v1', 'scientific_qualification': False,
        'passed_integrity_and_complete_execution': all(c['status'] == 'completed' for c in cases),
        'source_sha256': sha256(Path(__file__)), 'dependency_sha256': {'spread_reference_analyzer': sha256(REFERENCE_PATH), 'retention_diagnostic': sha256(HERE / 'retention.py')},
        'score_plan': {'path': str(score_plan), 'sha256': sha256(Path(score_plan)), 'jobs': len(cases)},
        'path_mapping': {'source_root': str(locations.source_root), 'local_root': str(locations.root), 'method': 'exact relative path; no suffix fallback'},
        'retention_reports': retention_sources, 'cases': cases, 'curves': curves,
        'summary': {'completed_jobs': sum(c['status'] == 'completed' for c in cases),
                    'incomplete_jobs': sum(c['status'] == 'incomplete' for c in cases), 'error_jobs': sum(c['status'] == 'error' for c in cases)},
        'scope': 'Saved-score recount and supplied-plan binding for development. Conditional replicas remain per-physical-path clusters with no pooled CP. Actual confidence bounds are pointwise and require the stated independent simulation strata. Gaussian-noise model/family assumptions, continuous-operation false admissions, full source/binary and generation contracts require separate qualification.'}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True); parser.add_argument('--score-plan', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True); parser.add_argument('--source-root', type=Path)
    parser.add_argument('--retention', type=Path, action='append', default=[])
    parser.add_argument('--verify-input-arrays', action='store_true')
    args = parser.parse_args(argv)
    result = analyze(args.root, args.score_plan, args.retention, args.source_root, args.verify_input_arrays)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x') as stream:
        json.dump(result, stream, allow_nan=False, sort_keys=True, indent=2); stream.write('\n')
    print(json.dumps(result['summary']))
    return 0 if result['passed_integrity_and_complete_execution'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
