#!/usr/bin/env python3
"""Audit a paired, simulation-truth timing revelation without relaxing blind acquisition.

The original nominal analyzer remains strict. This separate audit allows only
the two public arrival endpoints and output directory to change. It verifies
the complete physical metadata and unchanged sampled-energy/calendar bytes,
then reuses the accepted actual-observation scorer audit. No transmitter,
receiver, simulator, or scientific scoring job is launched by this program.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys


HERE = Path(__file__).resolve().parent
ANALYZER_PATH = HERE / 'analyze.py'
ANALYZER_SHA256 = '45f4960da40c216c07be8a632182756679a315c5d6ad645506fe102b17b7477e'
RETENTION_SHA256 = '5ca0cd5fe9d84bf629999a1621cf5c9799d3fd4e6629e5fc8e84a128676989f9'
GENERATION_FILES = {
    'tools/triad-long.cpp', 'tools/triad-lowrate-campaign.py',
    'tools/triad-conditional-sweep.py', 'tools/triad_bounded_run.py',
    'src/channel_perturbation.hpp', 'src/triad_simulation_stream.cpp',
    'include/datapump/triad_modem_v3.hpp',
    'docs/validation-data/triad-v3/2026-11-02-selective-correction/reproduction/make_campaign.py',
}
UNCHANGED_ARRAYS = ('received_energy.f64', 'signal_energy.f64',
                    'intervals.u64', 'valid_rows.u64')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def valid_sha(value):
    return isinstance(value, str) and len(value) == 64 and all(c in '0123456789abcdef' for c in value)


def load_analyzer():
    require(sha256(ANALYZER_PATH) == ANALYZER_SHA256, 'accepted nominal analyzer source changed')
    require(sha256(HERE / 'retention_batch.py') == RETENTION_SHA256,
            'accepted batched retention source changed')
    spec = importlib.util.spec_from_file_location('oracle_timing_nominal_analyzer', ANALYZER_PATH)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def read_json(path):
    def reject(value):
        raise ValueError(f'nonfinite JSON value {value}: {path}')
    result = json.loads(Path(path).read_text(), parse_constant=reject)
    require(isinstance(result, dict), f'expected JSON object: {path}')
    return result


def option(argv, flag):
    require(isinstance(argv, list) and argv.count(flag) == 1,
            f'expected exactly one command option {flag}')
    i = argv.index(flag)
    require(i + 1 < len(argv), f'missing command value {flag}')
    return argv[i + 1]


def output(job):
    return option(job['argv'], '--output')


def job_map(jobs):
    require(isinstance(jobs, list) and jobs, 'missing nonempty frozen job inventory')
    result = {}
    names = set()
    for job in jobs:
        source = output(job)
        require(source not in result and job.get('name') not in names, 'duplicate frozen job identity')
        result[source] = job
        names.add(job['name'])
    return result


def audit_source(plan, paths, generator):
    source = plan.get('source', {})
    inventory = source.get('source_file_sha256')
    require(isinstance(inventory, dict) and set(inventory) == GENERATION_FILES,
            'generation source inventory differs from required exact nonempty set')
    for name, digest in inventory.items():
        require(valid_sha(digest) and sha256(paths.evidence_root / name) == digest,
                f'generation source SHA mismatch: {name}')
    bundle = hashlib.sha256(json.dumps(inventory, sort_keys=True).encode()).hexdigest()
    require(valid_sha(source.get('source_bundle_sha256')) and source['source_bundle_sha256'] == bundle,
            'generation source-bundle digest mismatch')
    require(source.get('campaign_helper_sha256') == inventory['tools/triad-lowrate-campaign.py'],
            'campaign helper binding differs from generation inventory')
    require(valid_sha(plan.get('generator_sha256')) and sha256(generator) == plan['generator_sha256'],
            'oracle plan generator SHA mismatch')
    binaries = {job['argv'][0] for job in plan['reference_jobs'] + plan['jobs']}
    require(len(binaries) == 1, 'oracle and nominal commands use different generator binaries')
    binary = paths.local(next(iter(binaries)))
    require(valid_sha(source.get('binary_sha256')) and sha256(binary) == source['binary_sha256'],
            'local original generator binary SHA mismatch')
    return {'generation_source_sha256': inventory, 'source_bundle_sha256': bundle,
            'binary_path': str(binary), 'binary_sha256': sha256(binary),
            'oracle_plan_generator': str(generator), 'oracle_plan_generator_sha256': sha256(generator),
            'scope': 'Current source/binary bytes and frozen provenance declarations are bound; compiler execution provenance is not established by argv alone.'}


def audit_parent_plans(plan, paths):
    parents = {}
    for key in ('parent_export_plan', 'parent_score_plan'):
        receipt = plan.get(key, {})
        require(valid_sha(receipt.get('sha256')), f'missing frozen {key} SHA')
        p = paths.local(receipt['path'])
        require(sha256(p) == receipt['sha256'], f'frozen {key} SHA mismatch')
        parents[key] = read_json(p)
    parent = parents['parent_export_plan']
    require(parent.get('source') == plan.get('source'), 'oracle source provenance differs from original plan')
    parent_jobs = job_map(parent.get('jobs'))
    for job in plan['reference_jobs']:
        require(parent_jobs.get(output(job)) == job, 'reference job differs from original frozen export job')
    parent_profiles = {p['name']: p for p in parent.get('profiles', [])}
    require(plan.get('profiles') and all(parent_profiles.get(p['name']) == p for p in plan['profiles']),
            'oracle profiles differ from frozen physical profiles')
    return parents


def audit_command_pair(pair, nominal_job, oracle_job, nominal):
    """Bind exact simulation truth, and permit only output plus arrival endpoints."""
    a, b = nominal_job['argv'], oracle_job['argv']
    require(len(a) == len(b), 'oracle generation command length changed')
    require(a.count('--start') == b.count('--start') == 1, 'ambiguous arrival command')
    i = a.index('--start')
    require(i == b.index('--start') and i + 3 < len(a), 'oracle arrival option moved')
    j = a.index('--output')
    require(j == b.index('--output'), 'oracle output option moved')
    allowed = {i + 1, i + 2, j + 1}
    require(all(x == y for k, (x, y) in enumerate(zip(a, b)) if k not in allowed),
            'oracle command changed a physical, CFO, clock, phase, or other non-arrival option')
    require(pair.get('nominal_source') == output(nominal_job) and
            pair.get('oracle_source') == output(oracle_job), 'pair source does not match commands')
    expected = nominal_job.get('simulation_only_expected')
    require(isinstance(expected, dict) and expected == oracle_job.get('simulation_only_expected') == pair.get('expected_identity'),
            'oracle bit/epoch/physical seed identity changed')
    truth = nominal.get('selected_truth_arrival_seconds_simulation_only')
    lead = nominal.get('capture_lead_seconds')
    require(type(truth) in (int, float) and math.isfinite(truth) and
            type(lead) in (int, float) and math.isfinite(lead), 'missing finite nominal timing truth')
    delta = truth - lead
    prior = nominal.get('resolved_configuration', {}).get('initial_arrival_bounds_seconds')
    require(isinstance(prior, list) and len(prior) == 2 and all(type(v) in (int, float) and math.isfinite(v) for v in prior)
            and prior[0] <= delta <= prior[1], 'exact oracle arrival lies outside independently configured physical prior')
    require(pair.get('capture_lead_seconds') == lead and pair.get('exact_nominal_capture_arrival_seconds') == truth
            and pair.get('oracle_start_seconds') == delta and pair.get('physical_start_prior_seconds') == prior,
            'oracle plan timing revelation differs from the bound parent truth/prior')
    require(float(b[i+1]) == float(b[i+2]) == delta, 'oracle endpoints are not the exact nominal timing revelation')
    return truth, delta


def audit_manifest_pair(pair, nominal_job, oracle_job, nominal, oracle):
    truth, delta = audit_command_pair(pair, nominal_job, oracle_job, nominal)
    expected = copy.deepcopy(nominal)
    expected['command_argv'] = oracle_job['argv']
    # The complete manifest comparison is deliberately fail-closed. Every other
    # field, including oscillator truth, source format, physical channel teacher,
    # tails, received energy summaries, search configuration and capture calendar,
    # must remain byte-value equivalent. No hidden CFO/clock/phase fix is allowed.
    for field in ('candidates', 'column_descriptors'):
        require(isinstance(expected.get(field), list) and expected[field], f'parent lacks {field}')
        for candidate in expected[field]:
            require(candidate.get('arrival_seconds') == nominal['capture_lead_seconds'],
                    'parent candidate is not at nominal arrival')
            candidate['arrival_seconds'] = truth
    expected['public_timing_bounds_capture_seconds'] = [truth, truth]
    expected['resolved_configuration']['public_search']['timing_bounds_seconds'] = [delta, delta]
    require(oracle == expected, 'oracle manifest changed beyond the exact public template arrival/output')
    return truth


def audit_unchanged_arrays(nominal_dir, oracle_dir, nominal, oracle):
    receipts = {}
    for filename in UNCHANGED_ARRAYS:
        stem, _ = filename.rsplit('.', 1)
        require(nominal.get(stem + '_file') == oracle.get(stem + '_file') == filename,
                f'noncanonical physical array filename: {filename}')
        before, after = Path(nominal_dir) / filename, Path(oracle_dir) / filename
        require(before.is_file() and after.is_file(), f'missing unchanged sampled array: {filename}')
        size = before.stat().st_size
        require(size > 0 and size == after.stat().st_size == nominal.get('file_sizes', {}).get(filename)
                == oracle.get('file_sizes', {}).get(filename), f'unchanged sampled array size mismatch: {filename}')
        digest = sha256(before)
        require(digest == sha256(after), f'physical observation/calendar array changed: {filename}')
        receipts[filename] = {'sha256': digest, 'bytes': size, 'byte_identical': True}
    return receipts


def projection_summary(analyzer, directory, manifest, arrival):
    report, actual_manifest = analyzer.RETENTION.analyze_workspace(directory)
    require(actual_manifest == manifest and report.get('status') == 'completed',
            'retention manifest changed during audit')
    def selected(bit):
        candidates = [row for row in report['candidate_projections']
                      if row['public_candidate'].get('bit') == bit
                      and row['public_candidate'].get('epoch_offset_seconds') == manifest.get('truth_epoch_offset_seconds', 0)
                      and row['public_candidate'].get('arrival_seconds') == arrival
                      and all(row['public_candidate'].get(k) == 0 for k in
                              ('frequency_hz', 'clock_ppm', 'frequency_drift_hz_per_second'))]
        require(candidates, 'missing requested exact-arrival projection candidate')
        richest = max(row['public_candidate'].get('tap_count', 0) for row in candidates)
        candidates = [row for row in candidates if row['public_candidate'].get('tap_count', 0) == richest]
        require(len(candidates) == 1, 'ambiguous richest exact-arrival projection candidate')
        return candidates[0]
    correct = selected(manifest['signal_bit'])
    wrong = selected(1 - manifest['signal_bit'])
    energy = report['signal_energy_samples']
    return {'input_sha256': report['input_sha256'], 'sampled_signal_energy_samples': energy,
            'arrival_seconds': arrival,
            'correct_projection_energy_samples': correct['projection_energy_samples'],
            'wrong_projection_energy_samples': wrong['projection_energy_samples'],
            'correct_projection_fraction': correct['projection_energy_samples'] / energy if energy else None,
            'wrong_projection_fraction': wrong['projection_energy_samples'] / energy if energy else None,
            'correct_rank_histogram': correct['rank_histogram'], 'wrong_rank_histogram': wrong['rank_histogram'],
            'selective_stage_energy_simulation_only': report['selective_stage_energy_simulation_only'],
            'scope': 'Noiseless simulation-only free-block channel-span projection; not an achievable receiver likelihood or sensitivity.'}


def audit_runner(rows, jobs):
    planned = {job['name']: job for job in jobs}
    require(len(planned) == len(jobs), 'duplicate planned runner names')
    found = {}
    for row in rows:
        name = row.get('name')
        require(name in planned and name not in found, 'unplanned or duplicate selected runner receipt')
        require(row.get('command') == planned[name]['argv'] and row.get('returncode') == 0,
                f'runner command/final return code mismatch: {name}')
        runner = row.get('runner', {})
        require(runner.get('schema') == 'triad-bounded-run-v1' and runner.get('outcome') == 'completed'
                and runner.get('cancelled') is False and runner.get('timed_out') is False
                and runner.get('cancel_signal') is None and runner.get('exit_code') == 0
                and runner.get('child_exit_code') == 0
                and runner.get('termination', {}).get('group_empty') is True,
                f'runner cleanup/completion receipt not successful: {name}')
        require(runner.get('declared_max_seconds') == planned[name]['timeout_seconds']
                == runner.get('timeout_seconds'), f'runner timeout differs from frozen job: {name}')
        found[name] = row
    require(set(found) == set(planned), 'runner receipts omit frozen selected jobs')


def runner_receipts(plan, paths, new_receipts, parent_receipt):
    require(len(new_receipts) > 0, 'new oracle runner receipts are required')
    jobs = plan['jobs'] + plan['scoring_jobs']
    all_rows = []
    summaries = []
    for p in new_receipts:
        receipt = read_json(p)
        require(receipt.get('cancelled') is False and isinstance(receipt.get('results'), list),
                'cancelled or malformed oracle runner results')
        all_rows.extend(receipt['results'])
        summaries.append({'path': str(p), 'sha256': sha256(p), 'selected_jobs': len(receipt['results']), 'excluded_jobs': 0})
    audit_runner(all_rows, jobs)
    original = read_json(parent_receipt)
    require(original.get('cancelled') is False and isinstance(original.get('results'), list),
            'cancelled or malformed original runner results')
    parent = read_json(paths.local(plan['parent_export_plan']['path']))
    # Validate the complete original inventory, while reporting only the selected
    # sixteen paths as this diagnostic's denominator. Other original jobs remain
    # explicitly outside the oracle comparison.
    audit_runner(original['results'], parent['jobs'])
    selected_names = {job['name'] for job in plan['reference_jobs']}
    selected = [row for row in original['results'] if row['name'] in selected_names]
    audit_runner(selected, plan['reference_jobs'])
    summaries.append({'path': str(parent_receipt), 'sha256': sha256(parent_receipt),
                      'selected_jobs': len(selected), 'excluded_jobs': len(original['results']) - len(selected),
                      'complete_parent_inventory_verified': True})
    return summaries


def normalized_score_argv(job):
    argv = job['argv']
    end = argv.index('--output')
    return [*argv[:2], '--output', '<paired-output>', *argv[end+2:]]


def run(plan_path, expected_plan_sha256, evidence_root, output_path, generator,
        new_receipts, parent_receipt, source_root=None):
    require(valid_sha(expected_plan_sha256) and sha256(plan_path) == expected_plan_sha256,
            'oracle plan differs from explicitly frozen plan SHA')
    analyzer = load_analyzer()
    plan = read_json(plan_path)
    require(plan.get('schema') == 'triad-paired-oracle-timing-diagnostic-v1' and
            plan.get('stage') == 'oracle-timing-actual-noisy', 'unsupported oracle timing plan')
    paths = analyzer.Paths(Path(evidence_root), source_root, plan)
    audit_parent_plans(plan, paths)
    provenance = audit_source(plan, paths, Path(generator))
    references = job_map(plan.get('reference_jobs'))
    oracles = job_map(plan.get('jobs'))
    scorers = job_map(plan.get('scoring_jobs'))
    require(plan.get('counts') == {'new_exports': len(oracles), 'existing_reference_exports': len(references),
                                 'actual_scoring_jobs': len(scorers)}, 'oracle frozen counts differ from job inventories')
    profiles = {p['name']: p for p in plan['profiles']}
    require(len(profiles) == len(plan['profiles']), 'duplicate oracle profile')
    manifest_map = {}
    paired = []
    cells = []
    consumed_nominal, consumed_oracle, consumed_scores = set(), set(), set()
    for cell in plan.get('cells', []):
        require(cell.get('profile') in profiles and cell.get('modulation') == 'each-65', 'unexpected oracle physical cell')
        pairs = cell.get('pairs', [])
        nominal_sources = cell.get('nominal_sources')
        oracle_sources = cell.get('oracle_sources')
        require(pairs and cell.get('paths') == len(pairs) and
                nominal_sources == [p['nominal_source'] for p in pairs] and
                oracle_sources == [p['oracle_source'] for p in pairs], 'oracle cell pair/source inventory mismatch')
        cell_pairs = []
        for pair in pairs:
            ns, os = pair['nominal_source'], pair['oracle_source']
            require(ns in references and os in oracles and ns not in consumed_nominal and os not in consumed_oracle,
                    'unplanned or duplicate oracle pair')
            nominal_dir, oracle_dir = paths.local(ns), paths.local(os)
            require(valid_sha(pair.get('nominal_manifest_sha256')) and
                    sha256(nominal_dir / 'manifest.json') == pair['nominal_manifest_sha256'], 'parent nominal manifest SHA mismatch')
            nominal = analyzer._check_manifest(references[ns], nominal_dir, plan, profiles[cell['profile']])
            require(option(references[ns]['argv'], '--selective-model') == cell['selective_model'] and
                    option(references[ns]['argv'], '--selective-perturbation') == cell['modulation'],
                    'oracle cell label differs from physical export')
            oracle = read_json(oracle_dir / 'manifest.json')
            arrival = audit_manifest_pair(pair, references[ns], oracles[os], nominal, oracle)
            unchanged = audit_unchanged_arrays(nominal_dir, oracle_dir, nominal, oracle)
            manifest_map[ns], manifest_map[os] = nominal, oracle
            nominal_projection = projection_summary(analyzer, nominal_dir, nominal, nominal['capture_lead_seconds'])
            oracle_projection = projection_summary(analyzer, oracle_dir, oracle, arrival)
            for filename in ('manifest.json', 'signal_energy.f64', 'intervals.u64', 'valid_rows.u64'):
                expected_hash = sha256((nominal_dir if filename == 'manifest.json' else oracle_dir) / filename)
                require(nominal_projection['input_sha256'][filename] == expected_hash,
                        'nominal retention input changed during audit')
            require(oracle_projection['input_sha256']['manifest.json'] == sha256(oracle_dir / 'manifest.json'),
                    'oracle retention manifest changed during audit')
            row = {'profile': cell['profile'], 'model': cell['selective_model'],
                   'nominal_source': ns, 'oracle_source': os,
                   'source_bit': nominal['signal_bit'], 'epoch': nominal['epoch'],
                   'seed_roots': analyzer._seed_roots(nominal), 'sampled_cn0_db_hz': nominal['cn0_db_hz'],
                   'oracle_start_seconds': pair['oracle_start_seconds'],
                   'unchanged_physical_arrays': unchanged,
                   'nominal_projection': nominal_projection, 'oracle_projection': oracle_projection}
            paired.append(row); cell_pairs.append(row)
            consumed_nominal.add(ns); consumed_oracle.add(os)
        arms = {}
        score_configs = []
        for arm, sources in (('nominal', nominal_sources), ('oracle', oracle_sources)):
            candidates = [job for job in plan['scoring_jobs'] if job['argv'][2:job['argv'].index('--output')] == sources]
            require(len(candidates) == 1, f'cannot identify one exact {arm} score job')
            job = candidates[0]
            require(job['name'] not in consumed_scores and option(job['argv'], '--mode') == 'actual-noisy',
                    'duplicate scorer or non-actual oracle score mode')
            receipt = analyzer._score_receipt(job, paths, sources, manifest_map, True)
            require(not receipt['failed_physical_paths'] and receipt['completed_physical_paths'] == len(sources),
                    'actual oracle scorer did not complete every frozen path')
            arms[arm] = {'name': job['name'], **receipt}
            consumed_scores.add(job['name']); score_configs.append(normalized_score_argv(job))
        require(score_configs[0] == score_configs[1], 'paired scorers use different methods, priors, arithmetic, or family')
        for row in cell_pairs:
            row['paired_actual_outcomes'] = {}
            for arm, source in (('nominal', row['nominal_source']), ('oracle', row['oracle_source'])):
                outcomes = [p for p in arms[arm]['per_path'] if p['input'] == source]
                require(outcomes, 'actual paired per-path outcomes missing')
                row['paired_actual_outcomes'][arm] = outcomes
        cells.append({'profile': cell['profile'], 'model': cell['selective_model'],
                      'modulation': cell['modulation'], 'paired_paths': len(pairs), 'receiver_arms': arms})
    require(consumed_nominal == set(references) and consumed_oracle == set(oracles)
            and consumed_scores == {job['name'] for job in plan['scoring_jobs']}, 'frozen oracle jobs omitted from diagnostic cells')
    runners = runner_receipts(plan, paths, new_receipts, Path(parent_receipt))
    result = {'schema': 'triad-oracle-timing-audit-v1', 'passed': True,
              'plan': str(Path(plan_path).resolve()), 'plan_sha256': expected_plan_sha256,
              'auditor_sha256': sha256(Path(__file__)), 'nominal_analyzer_sha256': ANALYZER_SHA256,
              'retention_source_bindings': analyzer.RETENTION.source_bindings(),
              'generation_source_bindings': provenance, 'runner_receipts': runners,
              'paired_physical_paths': len(paired), 'new_exports': len(oracles),
              'actual_score_jobs': len(scorers), 'raw_input_arrays_verified': True,
              'cells': cells, 'pairs': paired,
              'interpretation': [
                  'This reveals exact simulation timing only; it is neither blind acquisition nor an operational sensitivity demonstration.',
                  'CFO, sample-clock, phase, physical path, waveform, power, bit, epoch, and all six seed roots remain unchanged.',
                  'Nominal and oracle outcomes reuse the same physical/noise realizations; they are paired and must not be pooled as independent trials.',
                  'Only actual sampled C/N0 is counted; earlier conditional-noise C/N0 screens are distinct observations.',
                  'Noiseless free-block projection is an upper channel-span diagnostic, not achievable receiver sensitivity.',
                  'Pointwise per-arm, per-bit confidence bounds do not cover model/method/cell selection or establish rare continuous-operation false admissions.',
              ]}
    Path(output_path).parent.mkdir(parents=True, exist_ok=True)
    Path(output_path).write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--expected-plan-sha256', required=True)
    parser.add_argument('--evidence-root', type=Path, required=True)
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--generator', type=Path, required=True)
    parser.add_argument('--runner-results', type=Path, action='append', required=True)
    parser.add_argument('--parent-runner-results', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        report = run(args.plan, args.expected_plan_sha256, args.evidence_root, args.output,
                     args.generator, args.runner_results, args.parent_runner_results, args.source_root)
    except (ValueError, KeyError, OSError, TypeError, IndexError) as error:
        print(f'oracle timing audit failed: {error}', file=sys.stderr)
        return 1
    print(json.dumps({'passed': report['passed'], 'paired_physical_paths': report['paired_physical_paths'],
                      'actual_score_jobs': report['actual_score_jobs'], 'output': str(args.output)}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
