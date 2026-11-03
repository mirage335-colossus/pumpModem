#!/usr/bin/env python3
"""Reuse completed grouped observations for one global B=0 two-ray fit.

No waveform generation, truth-selected alignment, or new noise draw occurs.
The only scientific argument changes are reblock=public block_count and
window_blocks=1. All timing groups, priors and admission-family budgets remain.
"""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED
import importlib.util
import json
from pathlib import Path
import sys
import time

HERE = Path(__file__).resolve().parent
def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, HERE/filename)
    value = importlib.util.module_from_spec(spec); spec.loader.exec_module(value)
    return value
base = module('global_acquisition_runner', 'run.py')
prepare = module('global_acquisition_prepare', 'prepare.py')
common = base.common
SCORER_FILES = {'tools/triad_group_acquire.py', 'tools/triad_gaussian_window.py',
                'tools/triad_noise_likelihood.py', 'tools/triad_state_workspace.py'}

def verify_inventory(items, root):
    if not items: raise ValueError('completed input lacks file inventory')
    for item in items:
        path = root/item['path']
        if not path.is_file() or path.stat().st_size != item['size_bytes'] or common.sha(path) != item['sha256']:
            raise ValueError('input artifact missing or changed: '+str(path))

def global_job(original, workspace, blocks, tag):
    if isinstance(blocks, bool) or not isinstance(blocks, int) or blocks < 1:
        raise ValueError('positive public block count required')
    value = dict(original)
    ident = tag+'-'+original['id']
    argv = list(original['argv'])
    argv[2] = str(workspace)
    prepare.replace(argv, '--reblock', blocks)
    prepare.replace(argv, '--window-blocks', 1)
    report = '{OUT}/scores/'+ident+'.json'
    scorefile = '{OUT}/scores/'+ident+'.npz'
    prepare.replace(argv, '--output', report)
    prepare.replace(argv, '--scores-output', scorefile)
    value.update(id=ident, argv=argv, depends_on=[], timeout_seconds=120,
        workspace=str(workspace), acquisition_report=report, score_archive=scorefile,
        window_blocks=1, reblock=blocks, original_job_id=original['id'],
        original_definition_sha256=original['definition_sha256'],
        original_dependencies=original['depends_on'])
    value['definition_sha256'] = common.digest({k:v for k,v in value.items() if k != 'definition_sha256'})
    return value

def read_dataset(plan_path, receipt_path, output_root, tag, root):
    plan = json.loads(plan_path.read_text()); receipt = json.loads(receipt_path.read_text())
    if receipt.get('plan_sha256') != common.sha(plan_path): raise ValueError('export receipt binds another plan')
    if receipt.get('stage') != 'export': raise ValueError('dataset receipt must be the export stage')
    for key in ('source_unchanged_at_end', 'binary_unchanged_at_end', 'all_selected_jobs_accounted'):
        if receipt.get(key) is not True: raise ValueError('export receipt has unresolved integrity/accounting')
    if (receipt.get('binary_sha256') != plan['expected_binary_sha256'] or
        receipt.get('build_provenance_sha256') != plan['build_provenance_sha256'] or
        receipt.get('source_archive_sha256') != plan['source_archive_sha256'] or
        receipt.get('source_file_sha256') != plan['source_file_sha256']):
        raise ValueError('native source/build binding differs')
    common.verify_source(plan, root)
    definitions = {j['id']:j for j in plan['jobs']}
    for value in definitions.values(): common.check_job(value)
    records = {r['id']:r for r in receipt['jobs']}
    if len(records) != len(receipt['jobs']): raise ValueError('duplicate export receipt identity')
    jobs = []; skipped = []; inputs = []
    for ident, definition in definitions.items():
        if definition['stage'] != 'export': continue
        record = records.get(ident)
        if record is None or record.get('status') != 'completed':
            skipped.append(dict(dataset=tag, export_id=ident,
                status='missing_export_receipt' if record is None else record['status'],
                scope='No global receiver run; original export failure/missing work retained'))
            continue
        if record['definition_sha256'] != definition['definition_sha256']:
            raise ValueError('export job definition differs')
        items = record.get('output_files', [])
        verify_inventory(items, output_root)
        workspace = Path(common.translate(definition['workspace'], {'OUT':str(output_root)}))
        manifest = json.loads((workspace/'manifest.json').read_text())
        blocks = manifest['block_count']
        original = next(j for j in plan['jobs'] if j['stage'] == 'acquire' and j['depends_on'] == [ident])
        value = global_job(original, workspace, blocks, tag)
        value.update(input_plan_sha256=common.sha(plan_path), input_export_receipt_sha256=common.sha(receipt_path))
        value['definition_sha256'] = common.digest({k:v for k,v in value.items() if k != 'definition_sha256'})
        jobs.append(value)
        inputs.append(dict(job_id=value['id'], output_root=str(output_root), workspace=str(workspace),
                           files=items, parent_export_id=ident, physical_capture_count=1))
    binding = dict(tag=tag, plan_path=str(plan_path), plan_sha256=common.sha(plan_path),
        export_receipt_path=str(receipt_path), export_receipt_sha256=common.sha(receipt_path),
        output_root=str(output_root), source_file_sha256=plan['source_file_sha256'])
    return jobs, skipped, inputs, binding

def baselines(paths, datasets, scorer_hash):
    allowed = {d['plan_sha256'] for d in datasets}; result = {}; bindings = []
    for path in paths:
        value = json.loads(path.read_text()); plan_hash = value.get('plan_sha256')
        if plan_hash not in allowed or value.get('stage') != 'acquire': raise ValueError('baseline receipt dataset/stage differs')
        if any(value.get(k) is not True for k in ('source_unchanged_at_end','binary_unchanged_at_end','all_selected_jobs_accounted')):
            raise ValueError('baseline receipt has unresolved integrity/accounting')
        if value.get('scorer_manifest_sha256') != scorer_hash:
            raise ValueError('baseline scorer variant differs; not a pure global/short comparison')
        dataset = next(d for d in datasets if d['plan_sha256'] == plan_hash)
        plan = json.loads(Path(dataset['plan_path']).read_text())
        definitions = {j['id']:j for j in plan['jobs']}
        if (value.get('source_file_sha256') != plan['source_file_sha256'] or
            value.get('binary_sha256') != plan['expected_binary_sha256'] or
            value.get('build_provenance_sha256') != plan['build_provenance_sha256']):
            raise ValueError('baseline source/build binding differs')
        for record in value['jobs']:
            if record.get('status') != 'completed': continue
            if record['id'] not in definitions or record['definition_sha256'] != definitions[record['id']]['definition_sha256']:
                raise ValueError('baseline job definition differs')
            key = (plan_hash, record['id'])
            if key in result: raise ValueError('duplicate baseline job')
            verify_inventory(record['output_files'], Path(dataset['output_root']))
            result[key] = record
        bindings.append(dict(path=str(path), sha256=common.sha(path)))
    return result, bindings

def main():
    wall_start = time.monotonic()
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--dataset', nargs=3, action='append', required=True,
                   metavar=('PLAN','EXPORT_RECEIPT','SOURCE_OUTPUT_ROOT'))
    p.add_argument('--baseline-receipt', type=Path, action='append', default=[])
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--scorer-manifest', type=Path, required=True)
    p.add_argument('--output-root', type=Path, required=True)
    p.add_argument('--python', type=Path, default=Path(sys.executable))
    p.add_argument('--jobs', type=int, default=3)
    p.add_argument('--global-seconds', type=float, default=900)
    args = p.parse_args()
    if not 1 <= args.jobs <= 3 or not 0 < args.global_seconds <= 1200: p.error('require1..3jobs and <=1200seconds')
    args.root = args.root.resolve(); args.output_root = args.output_root.resolve(); args.python = args.python.absolute()
    if args.output_root.exists(): p.error('global output root must be fresh')
    scorer = json.loads(args.scorer_manifest.read_text()); scorer_hash = common.sha(args.scorer_manifest)
    if scorer.get('schema') != 'triad-broad-acquisition-scorer-v1' or set(scorer.get('source_file_sha256',{})) != SCORER_FILES:
        raise ValueError('scorer manifest must bind all numerical dependencies')
    common.verify_source(scorer, args.root)
    jobs = []; skipped = []; inputs = []; datasets = []
    for index, triple in enumerate(args.dataset):
        paths = [Path(s).resolve() for s in triple]
        values = read_dataset(*paths, 'set'+str(index), args.root)
        jobs.extend(values[0]); skipped.extend(values[1]); inputs.extend(values[2]); datasets.append(values[3])
    if not jobs: raise ValueError('no completed physical observations to reuse')
    if len({i['workspace'] for i in inputs}) != len(inputs): raise ValueError('duplicate physical workspace')
    baseline, baseline_bindings = baselines(args.baseline_receipt, datasets, scorer_hash)
    own_sources = {str(path.relative_to(args.root)):common.sha(path) for path in
                   (Path(__file__).resolve(), HERE/'run.py', HERE/'prepare.py', HERE.parent/'reproduction/run_plan.py', args.root/'tools/triad_bounded_run.py')}
    args.output_root.mkdir(parents=True); args.run_dir = args.output_root/'runs'; args.run_dir.mkdir()
    frozen = dict(schema='triad-global-reblock-plan-v1', jobs=jobs, skipped_exports=skipped,
        datasets=datasets, inputs=inputs, scorer_manifest_sha256=scorer_hash,
        scorer_source_file_sha256=scorer['source_file_sha256'], source_file_sha256=own_sources,
        baseline_receipts=baseline_bindings,
        scope='One global constant two-ray B=0 Gaussian fit, retaining every original timing group and admission budget; same actual observations, no new independent physical trial',
        limitations=['Sampled Doppler ray phases remain in the native basis; renewing channel gains and substantial unsearched common phase/CFO are model mismatch.',
            'Strong selective modulation is absent from the two-ray basis; unexplained signal enters residual-UCB and can limit sensitivity.',
            'Full-observation reblocking changes the channel model and residual dimension; it is not only a speed optimization.',
            'Fixed zero-group diagnostic and primary full-timing mixture reuse the same observed input; do not double the trial count.'])
    plan_path = args.output_root/'plan.json'
    plan_path.write_text(json.dumps(frozen, indent=2, sort_keys=True, allow_nan=False)+'\n')
    for value in jobs: common.check_job(value)
    bindings = dict(ROOT=str(args.root), OUT=str(args.output_root), PYTHON=str(args.python))
    start = time.monotonic(); deadline = start+args.global_seconds
    results = []; cursor = 0; pending = {}
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        while cursor < len(jobs) or pending:
            while cursor < len(jobs) and len(pending) < args.jobs:
                value = jobs[cursor]; cursor += 1
                if time.monotonic()+130 > deadline:
                    results.append(dict(id=value['id'], definition_sha256=value['definition_sha256'],
                        status='not_started_deadline', output_files=[]))
                else: pending[pool.submit(base.execute, value, args, bindings, {}, scorer)] = value
            if pending:
                done, _ = wait(pending, timeout=1, return_when=FIRST_COMPLETED)
                for future in done:
                    value = pending.pop(future)
                    try: record = future.result()
                    except Exception as error:
                        record = dict(id=value['id'], definition_sha256=value['definition_sha256'],
                                      status='runner_error', reason=repr(error), output_files=[])
                    reference = baseline.get((value['input_plan_sha256'], value['original_job_id']))
                    record['same_capture_short_window_baseline'] = reference.get('acquisition_summary') if reference else None
                    record['baseline_scope'] = 'Same scorer version, physical workspace, candidates and family gates; channel accumulation/window differs' if reference else 'No matching accepted short-window receipt supplied'
                    results.append(record)
                    print(json.dumps(dict(finished=record['id'], status=record['status'], seconds=record.get('elapsed_seconds'))), flush=True)
    integrity_errors = []
    try:
        common.verify_source(scorer, args.root)
        common.verify_source({'source_file_sha256':own_sources}, args.root)
        for dataset in datasets:
            if common.sha(dataset['plan_path']) != dataset['plan_sha256'] or common.sha(dataset['export_receipt_path']) != dataset['export_receipt_sha256']:
                raise ValueError('input plan/receipt changed')
            common.verify_source({'source_file_sha256':dataset['source_file_sha256']}, args.root)
        for item in inputs: verify_inventory(item['files'], Path(item['output_root']))
        for item in baseline_bindings:
            if common.sha(item['path']) != item['sha256']: raise ValueError('baseline receipt changed')
    except (ValueError, OSError) as error: integrity_errors.append(str(error))
    counts = {s:sum(r['status'] == s for r in results) for s in sorted({r['status'] for r in results})}
    receipt = dict(schema='triad-global-reblock-result-v1', plan_sha256=common.sha(plan_path),
        source_file_sha256=own_sources, scorer_manifest_sha256=scorer_hash,
        scorer_source_file_sha256=scorer['source_file_sha256'], source_and_inputs_unchanged=not integrity_errors,
        integrity_errors=integrity_errors, all_selected_jobs_accounted=len(results) == len(jobs),
        counts=counts, skipped_exports=skipped, elapsed_seconds=time.monotonic()-start,
        full_driver_wall_seconds=time.monotonic()-wall_start,
        preparation_seconds=start-wall_start,
        concurrent_jobs=args.jobs, global_seconds_bound=args.global_seconds,
        jobs=sorted(results, key=lambda r:r['id']))
    (args.output_root/'results.json').write_text(json.dumps(receipt, indent=2, sort_keys=True, allow_nan=False)+'\n')
    print(json.dumps(dict(receipt=str(args.output_root/'results.json'), counts=counts)))
    return int(bool(integrity_errors) or len(results) != len(jobs))

if __name__ == '__main__': raise SystemExit(main())
