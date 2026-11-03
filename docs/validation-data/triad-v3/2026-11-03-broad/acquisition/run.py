#!/usr/bin/env python3
"""Run one frozen acquisition stage with bounded children and explicit provenance."""
from __future__ import annotations
import argparse
from array import array
from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED
import importlib.util
import json
import math
from pathlib import Path
import sys
import time

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('acquisition_common_runner', HERE.parent/'reproduction/run_plan.py')
common = importlib.util.module_from_spec(spec); spec.loader.exec_module(common)

def read_doubles(path):
    values = array('d')
    with path.open('rb') as stream: values.frombytes(stream.read())
    if sys.byteorder != 'little': values.byteswap()
    if any(not math.isfinite(x) or x < 0 for x in values): raise ValueError('invalid evaluation energy')
    return values

def export_audit(job, bindings):
    directory = Path(common.translate(job['workspace'], bindings))
    manifest = json.loads((directory/'manifest.json').read_text())
    if manifest['group_count'] != job['expected_groups']: raise ValueError('native group count differs')
    if manifest['sample_count'] > 2000000: raise ValueError('native export row bound exceeded')
    size = sum(path.stat().st_size for path in directory.iterdir() if path.is_file())
    if size > 2048*1024**2: raise ValueError('native export byte bound exceeded')
    energy = read_doubles(directory/'signal_energy.f64')
    if len(energy) != manifest['block_count']: raise ValueError('evaluation energy rows differ')
    seconds = math.fsum(energy)/manifest['sample_rate']
    parent = job['parent_signal_evaluation']
    same_calendar = (parent is not None and manifest['sample_count'] == parent['sample_count']
                     and manifest['sample_rate'] == parent['sample_rate'])
    return dict(group_count=manifest['group_count'], sample_count=manifest['sample_count'],
        sample_rate=manifest['sample_rate'], block_samples=manifest['block_samples'],
        total_export_bytes=size, post_erasure_signal_energy_seconds_evaluation_only=seconds,
        parent_capture_calendar_matches=same_calendar,
        signal_energy_ratio_to_parent=(seconds/parent['post_erasure_channel_energy_seconds']
            if job['control'] == 'signal' and parent and parent['post_erasure_channel_energy_seconds'] > 0 else None),
        selected_truth_arrival_seconds_simulation_only=manifest.get('selected_truth_arrival_seconds_simulation_only'),
        caveat='Energy/calendar are evaluated after export, never provided to the scorer; equal energy/calendar alone does not prove byte-identical I/Q')

def outcome(scores, threshold, truth, control):
    bit = 0 if scores[0] >= scores[1] else 1
    accepted = scores[bit] >= threshold
    if control == 'signal':
        return dict(correct_admitted=int(accepted and bit == truth), wrong_admitted=int(accepted and bit != truth), missed=int(not accepted))
    return dict(false_admitted=int(accepted), not_admitted=int(not accepted))

def score_audit(job, bindings, scorer):
    path = Path(common.translate(job['acquisition_report'], bindings))
    payload = json.loads(path.read_text())
    if payload['receiver_source_sha256'] != scorer['source_file_sha256']['tools/triad_group_acquire.py']:
        raise ValueError('scorer source differs from declared variant')
    if len(payload['captures']) != 1: raise ValueError('one physical capture required')
    result = payload['captures'][0]
    if result['geometry']['group_count'] != job['expected_groups']: raise ValueError('scored group count differs')
    scorefile = Path(common.translate(job['score_archive'], bindings))
    if payload['scores_npz_sha256'] != common.sha(scorefile): raise ValueError('score archive hash differs')
    values = result['group_bit_log_likelihoods']
    if len(values) != job['expected_groups'] or any(len(r) != 2 or not all(math.isfinite(v) for v in r) for r in values):
        raise ValueError('invalid group likelihood array')
    selected = job['nominal_group_index']
    public = result['geometry']['public_group_parameters'][selected]
    manifest = json.loads((Path(common.translate(job['workspace'], bindings))/'manifest.json').read_text())
    if not math.isclose(public['arrival_seconds'], manifest['capture_lead_seconds'], abs_tol=1e-10, rel_tol=0):
        raise ValueError('middle group is not predeclared zero arrival')
    if any(public[k] != 0 for k in ('frequency_hz', 'clock_ppm', 'epoch_offset_seconds', 'frequency_drift_hz_per_second')):
        raise ValueError('nominal group has nonzero unsearched parameters')
    evaluated = result['simulation_evaluation_only']
    if evaluated['control'] != job['control'] or evaluated['signal_bit'] != job['bit']:
        raise ValueError('simulation evaluation identity differs')
    primary = {}; nominal = {}
    for gate in ('strict', 'relaxed'):
        threshold = result[gate]['log_threshold']
        primary[gate] = outcome(result['bit_log_likelihoods'], threshold, job['bit'], job['control'])
        if primary[gate] != evaluated['outcomes'][gate]: raise ValueError('independent outcome recount differs')
        nominal[gate] = outcome(values[selected], threshold, job['bit'], job['control'])
    return dict(primary_full_timing_mixture=primary, same_workspace_nominal_group=nominal,
        nominal_group_index=selected, nominal_group_scores=values[selected],
        primary_scores=result['bit_log_likelihoods'], nominal_baseline_scope='Same full-bank UCB calibration and gates, fixed zero group; not best-group selection',
        timing=result['timing'], arithmetic=result['arithmetic'],
        gates={g:result[g]['log_threshold'] for g in ('strict', 'relaxed')})

def execute(job, args, bindings, prior, scorer):
    # The common child owner reaps timed-out process groups. Its receipts and
    # hashes are reused, while grouped exports need their own filenames and
    # validation rather than the ordinary export-stats output parser.
    proxy = job
    if job['stage'] == 'export':
        directory = Path(common.translate(job['workspace'], bindings))
        if directory.exists(): raise ValueError('grouped workspace already exists')
        directory.parent.mkdir(parents=True, exist_ok=True)
        proxy = dict(job, stage='group-export')
    if job['stage'] == 'acquire':
        for key in ('acquisition_report', 'score_archive'):
            path = Path(common.translate(job[key], bindings))
            if path.exists(): raise ValueError('score output already exists')
            path.parent.mkdir(parents=True, exist_ok=True)
    record = common.execute(proxy, args, bindings, prior)
    record['stage'] = job['stage']
    if job['stage'] == 'export':
        record['output_files'] = common.inventory(Path(common.translate(job['workspace'], bindings)), args.output_root)
    elif job['stage'] == 'acquire':
        for key in ('acquisition_report', 'score_archive'):
            record['output_files'] += common.inventory(Path(common.translate(job[key], bindings)), args.output_root)
    if record['status'] != 'completed': return record
    try:
        if job['stage'] == 'configure':
            cfg = record['native_configuration']['configuration']
            if not cfg['public_search']['covers_initial_timing_prior']: raise ValueError('timing prior not covered')
            if cfg['public_search']['estimated_hypotheses'] > 10000: raise ValueError('candidate cap exceeded')
        elif job['stage'] == 'export':
            directory = Path(common.translate(job['workspace'], bindings))
            if not (directory/'group_observation.c128').is_file() or not (directory/'received_energy.f64').is_file():
                raise ValueError('grouped actual-noisy observation missing')
            record['evaluation_only'] = export_audit(job, bindings)
        else:
            record['acquisition_summary'] = score_audit(job, bindings, scorer)
    except (KeyError, ValueError, OSError) as error:
        record.update(status='invalid_output', reason=str(error))
    return record

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('plan', 'root', 'output-root', 'binary', 'run-dir', 'build-provenance', 'source-archive'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--python', type=Path, default=Path(sys.executable))
    p.add_argument('--stage', choices=('configure', 'export', 'acquire'), required=True)
    p.add_argument('--scorer-manifest', type=Path)
    p.add_argument('--prior-results', action='append', type=Path, default=[])
    p.add_argument('--only-id', action='append', default=[])
    p.add_argument('--jobs', type=int, default=2)
    p.add_argument('--global-seconds', type=float, default=900)
    args = p.parse_args()
    if not 1 <= args.jobs <= 4 or not 0 < args.global_seconds <= 1800: p.error('require1..4jobs and <=1800seconds')
    for key in ('plan', 'root', 'output_root', 'binary', 'run_dir', 'build_provenance', 'source_archive'):
        setattr(args, key, getattr(args, key).resolve())
    args.python = args.python.absolute()  # Do not resolve a virtualenv symlink.
    if args.run_dir.exists(): p.error('run directory must be fresh')
    plan = json.loads(args.plan.read_text()); plan_hash = common.sha(args.plan)
    definitions = {j['id']:j for j in plan['jobs']}
    if len(definitions) != len(plan['jobs']): raise ValueError('duplicate job identifiers')
    for value in definitions.values(): common.check_job(value)
    common.verify_source(plan, args.root)
    binary_hash = common.sha(args.binary)
    build = common.verify_build(plan, args.build_provenance, args.source_archive, binary_hash)
    prior, receipts = common.read_prior(args.prior_results, plan_hash, definitions, args.output_root,
        binary_hash, plan['build_provenance_sha256'], plan['source_file_sha256'])
    selected = [j for j in plan['jobs'] if j['stage'] == args.stage]
    if args.only_id:
        if not set(args.only_id) <= {j['id'] for j in selected}: raise ValueError('unknown stage job')
        selected = [j for j in selected if j['id'] in args.only_id]
    if not selected or any(j['id'] in prior for j in selected): raise ValueError('empty selection or implicit rerun')
    scorer = None; scorer_hash = None
    if args.stage == 'acquire':
        if args.scorer_manifest is None: p.error('acquire requires an explicit scorer source manifest')
        scorer = json.loads(args.scorer_manifest.read_text()); scorer_hash = common.sha(args.scorer_manifest)
        if scorer.get('schema') != 'triad-broad-acquisition-scorer-v1': raise ValueError('scorer schema differs')
        required = {'tools/triad_group_acquire.py', 'tools/triad_gaussian_window.py',
                    'tools/triad_noise_likelihood.py', 'tools/triad_state_workspace.py'}
        if set(scorer.get('source_file_sha256', {})) != required:
            raise ValueError('scorer manifest must bind every numerical dependency')
        common.verify_source(scorer, args.root)
    args.run_dir.mkdir(parents=True); args.output_root.mkdir(parents=True, exist_ok=True)
    bindings = dict(ROOT=str(args.root), OUT=str(args.output_root), BINARY=str(args.binary), PYTHON=str(args.python))
    start = time.monotonic(); deadline = start + args.global_seconds
    results = []; cursor = 0; pending = {}
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        while cursor < len(selected) or pending:
            while cursor < len(selected) and len(pending) < args.jobs:
                value = selected[cursor]; cursor += 1
                if time.monotonic() + value['timeout_seconds'] + 10 > deadline:
                    results.append(dict(id=value['id'], definition_sha256=value['definition_sha256'],
                        stage=value['stage'], depends_on=value['depends_on'], status='not_started_deadline', output_files=[]))
                else: pending[pool.submit(execute, value, args, bindings, prior, scorer)] = value
            if pending:
                done, _ = wait(pending, timeout=1, return_when=FIRST_COMPLETED)
                for future in done:
                    value = pending.pop(future)
                    try: record = future.result()
                    except Exception as error:
                        record = dict(id=value['id'], stage=value['stage'], definition_sha256=value['definition_sha256'],
                            depends_on=value['depends_on'], status='runner_error', reason=repr(error), output_files=[])
                    results.append(record)
                    print(json.dumps(dict(finished=record['id'], status=record['status'], seconds=record.get('elapsed_seconds'))), flush=True)
    source_valid = True; source_error = None
    try:
        common.verify_source(plan, args.root)
        if scorer is not None: common.verify_source(scorer, args.root)
    except ValueError as error: source_valid = False; source_error = str(error)
    binary_valid = common.sha(args.binary) == binary_hash
    counts = {s:sum(r['status'] == s for r in results) for s in sorted({r['status'] for r in results})}
    receipt = dict(schema='triad-broad-acquisition-stage-v1', plan_sha256=plan_hash,
        runner_sha256=common.sha(__file__), stage=args.stage, binary_sha256=binary_hash,
        source_file_sha256=plan['source_file_sha256'], build_provenance_sha256=common.sha(args.build_provenance),
        source_archive_sha256=build['source_archive_sha256'], scorer_manifest_sha256=scorer_hash,
        scorer_source_file_sha256=scorer['source_file_sha256'] if scorer else None,
        source_unchanged_at_end=source_valid, source_error=source_error, binary_unchanged_at_end=binary_valid,
        all_selected_jobs_accounted=len(results) == len(selected), prior_receipts=receipts,
        elapsed_seconds=time.monotonic()-start, global_seconds_bound=args.global_seconds,
        concurrent_jobs=args.jobs, counts=counts, jobs=sorted(results, key=lambda r:r['id']))
    (args.run_dir/'results.json').write_text(json.dumps(receipt, indent=2, sort_keys=True, allow_nan=False)+'\n')
    print(json.dumps(dict(receipt=str(args.run_dir/'results.json'), counts=counts)))
    return int(not (source_valid and binary_valid and receipt['all_selected_jobs_accounted']))

if __name__ == '__main__': raise SystemExit(main())
