#!/usr/bin/env python3
"""Freeze and run an explicit scorer variant on existing physical workspaces.

Original plans, exports and failed acquisition receipts are immutable inputs.
This program cannot generate a waveform or silently retry an original job.
"""
from __future__ import annotations
import argparse
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
import importlib.util
import json
from pathlib import Path
import sys
import time

HERE=Path(__file__).resolve().parent
SPEC=importlib.util.spec_from_file_location('extension_physical_original',HERE/'run.py')
original=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(original)
common=original.common
SCORER=('tools/triad_group_acquire.py','tools/triad_gaussian_window.py',
        'tools/triad_noise_likelihood.py','tools/triad_state_workspace.py','tools/triad_gaussian_state.py')
RUNNER=('docs/validation-data/triad-v3/2026-11-03-extension/physical/rescore.py',
        'docs/validation-data/triad-v3/2026-11-03-extension/physical/run.py',
        'docs/validation-data/triad-v3/2026-11-03-broad/reproduction/run_plan.py',
        'tools/triad_bounded_run.py')


def fresh_json(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    with path.open('x') as stream:
        json.dump(value,stream,indent=2,sort_keys=True,allow_nan=False);stream.write('\n')


def phase_budget(case):
    return .02 if case=='shared-microwave-am65' else .01


def build(plan,plan_hash,prior,receipts,root):
    definitions={j['id']:j for j in plan['jobs']}
    jobs=[];inputs={};baselines=[]
    for source in plan['jobs']:
        if source['stage']!='acquire':continue
        baseline=prior.get(source['id'])
        baselines.append(dict(id=source['id'],record=baseline,
            status=baseline['status'] if baseline else 'not_recorded',
            record_sha256=common.digest(baseline) if baseline else None))
        unavailable=[d for d in source['depends_on'] if d not in prior or prior[d]['status']!='completed']
        export_id,configure_id=source['depends_on']
        if not unavailable:
            export=definitions[export_id]
            files=prior[export_id]['output_files']
            workspace=export['workspace'].replace('{OUT}/','')
            required={workspace+'/'+name for name in ('manifest.json','group_observation.c128','received_energy.f64')}
            if not required<={f['path'] for f in files}:raise ValueError('successful export lacks bound actual-noisy inputs')
            inputs[export_id]=dict(workspace=workspace,output_files=files,
                original_export_definition_sha256=export['definition_sha256'],
                public_configuration=prior[configure_id]['native_configuration']['configuration'])
        job={k:source[k] for k in ('case','control','bit','expected_groups','method')}
        ident='rescore-'+source['id']
        job.update(id=ident,stage='acquire',argv=[],timeout_seconds=source['timeout_seconds'],
            depends_on=[],input_id=export_id,unavailable_original_dependencies=unavailable,
            original_acquisition_id=source['id'],original_acquisition_definition_sha256=source['definition_sha256'],
            maximum_block_phase_loss_db=phase_budget(source['case']),
            acquisition_report='{OUT}/scores/'+ident+'.json',score_archive='{OUT}/scores/'+ident+'.npz')
        job['definition_sha256']=common.digest(job);jobs.append(job)
    sources={name:common.sha(root/name) for name in (*SCORER,*RUNNER)}
    return dict(schema='triad-extension-source-variant-v1',original_plan_sha256=plan_hash,
        original_binary_sha256=plan['expected_binary_sha256'],original_source_file_sha256=plan['source_file_sha256'],
        original_scorer_source_file_sha256=plan['scorer_source_file_sha256'],
        source_file_sha256=sources,scorer_source_file_sha256={k:sources[k] for k in SCORER},
        original_receipts=receipts,baseline_acquisitions=baselines,inputs=inputs,jobs=jobs,
        source_variant=dict(name='negative-scientific-cli-and-explicit-microwave-phase-budget-v1',
            declared_code_scope='Negative-scientific-notation CLI compatibility; likelihood arithmetic unchanged by that parser fix.',
            model_change='shared-microwave-am65 explicitly permits0.02dB original-block phase-arc approximation; all other cases retain0.01dB.',
            receiver_targets='Unchanged finite family and strict/relaxed gates; no threshold or noise calibration change.',
            scope_warning='A larger permitted mismatch is a separately declared approximation change, not a parser-only repair or improved sensitivity proof.'),
        planned_acquisitions=len(jobs),input_capture_count=len(inputs),
        baseline_status_counts={s:sum(b['status']==s for b in baselines) for s in sorted({b['status'] for b in baselines})})


def verify_inputs(manifest,input_root):
    checked={}
    for item in manifest['inputs'].values():
        for artifact in item['output_files']:
            path=input_root/artifact['path']; identity=(artifact['sha256'],artifact['size_bytes'])
            if artifact['path'] in checked:
                if checked[artifact['path']]!=identity:raise ValueError('conflicting input identity')
                continue
            if not path.is_file() or path.stat().st_size!=identity[1] or common.sha(path)!=identity[0]:
                raise ValueError('input bytes changed: '+str(path))
            checked[artifact['path']]=identity
    return len(checked)


def command(job,args,bindings,manifest):
    item=manifest['inputs'][job['input_id']]
    proxy={**job,'workspace':'{INPUT}/'+item['workspace'],'depends_on':['export','configure']}
    prior={'configure':{'native_configuration':{'configuration':item['public_configuration']}}}
    argv=original.acquire_argv(proxy,args,bindings,prior)
    flag='--maximum-block-phase-loss-db'
    if flag in argv:argv[argv.index(flag)+1]=str(job['maximum_block_phase_loss_db'])
    else:argv += [flag,str(job['maximum_block_phase_loss_db'])]
    return argv


def execute(job,args,bindings,manifest):
    if job['unavailable_original_dependencies']:
        return dict(id=job['id'],definition_sha256=job['definition_sha256'],status='not_started_dependency',
            unavailable_original_dependencies=job['unavailable_original_dependencies'],output_files=[])
    proxy={**job,'argv':command(job,args,bindings,manifest)}
    for field in ('acquisition_report','score_archive'):
        target=Path(common.translate(job[field],bindings))
        if target.exists():raise ValueError('rescore output already exists: '+str(target))
        target.parent.mkdir(parents=True,exist_ok=True)
    record=common.execute(proxy,args,bindings,{})
    record.update(case=job['case'],control=job['control'],bit=job['bit'],method=job['method'],
        original_acquisition_id=job['original_acquisition_id'],maximum_block_phase_loss_db=job['maximum_block_phase_loss_db'])
    record['physical_input_binding']=manifest['inputs'][job['input_id']]
    for field in ('acquisition_report','score_archive'):
        record['output_files']+=common.inventory(Path(common.translate(job[field],bindings)),args.output_root)
    if record['status']=='completed':
        try:record['acquisition_summary']=original.audit_score(job,args,bindings,manifest)
        except (OSError,ValueError,KeyError) as exc:record.update(status='invalid_output',reason=str(exc))
    return record


def prepare(args):
    plan=json.loads(args.plan.read_text());plan_hash=common.sha(args.plan)
    if plan.get('schema')!='triad-extension-physical-plan-v1' or not plan.get('frozen'):raise ValueError('expected original frozen plan')
    definitions={j['id']:j for j in plan['jobs']}
    if len(definitions)!=len(plan['jobs']):raise ValueError('duplicate original definitions')
    for job in definitions.values():common.check_job(job)
    prior,receipts=common.read_prior(args.prior_results,plan_hash,definitions,args.input_root,
        expected_binary=plan['expected_binary_sha256'],expected_source=plan['source_file_sha256'])
    value=build(plan,plan_hash,prior,receipts,args.root)
    value['original_plan_path']=str(args.plan.resolve())
    value['original_input_root']=str(args.input_root.resolve())
    fresh_json(args.manifest,value)
    print(json.dumps(dict(manifest=str(args.manifest),sha256=common.sha(args.manifest),
        acquisitions=len(value['jobs']),captures=len(value['inputs']),baseline_status_counts=value['baseline_status_counts'])))


def run(args):
    if args.run_dir.exists() or args.output_root.exists():raise ValueError('run directory and output root must be fresh')
    manifest=json.loads(args.manifest.read_text());identity=common.sha(args.manifest)
    if manifest.get('schema')!='triad-extension-source-variant-v1':raise ValueError('unexpected variant schema')
    common.verify_source(manifest,args.root)
    definitions={j['id']:j for j in manifest['jobs']}
    if len(definitions)!=len(manifest['jobs']):raise ValueError('duplicate variant definitions')
    for job in definitions.values():common.check_job(job)
    selected=manifest['jobs']
    if args.only_id:
        if not set(args.only_id)<=set(definitions):raise ValueError('unknown selected job')
        selected=[j for j in selected if j['id'] in args.only_id]
    if not selected:raise ValueError('empty selection')
    verified=verify_inputs(manifest,args.input_root)
    args.run_dir.mkdir(parents=True);args.output_root.mkdir(parents=True)
    bindings=dict(ROOT=str(args.root),OUT=str(args.output_root),INPUT=str(args.input_root),PYTHON=str(args.python))
    begin=time.monotonic();deadline=begin+args.global_seconds;records=[];pending={};cursor=0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        while cursor<len(selected) or pending:
            while cursor<len(selected) and len(pending)<args.jobs:
                job=selected[cursor];cursor+=1
                if time.monotonic()+job['timeout_seconds']+10>deadline:
                    records.append(dict(id=job['id'],definition_sha256=job['definition_sha256'],status='not_started_deadline',output_files=[]))
                else:pending[pool.submit(execute,job,args,bindings,manifest)]=job
            if pending:
                done,_=wait(pending,timeout=1,return_when=FIRST_COMPLETED)
                for future in done:
                    job=pending.pop(future)
                    try:record=future.result()
                    except Exception as exc:record=dict(id=job['id'],definition_sha256=job['definition_sha256'],status='runner_error',reason=repr(exc),output_files=[])
                    records.append(record);print(json.dumps(dict(finished=job['id'],status=record['status'],seconds=record.get('elapsed_seconds'))),flush=True)
    stable=True;error=None
    try:common.verify_source(manifest,args.root);verify_inputs(manifest,args.input_root)
    except (OSError,ValueError) as exc:stable=False;error=str(exc)
    result=dict(schema='triad-extension-source-variant-results-v1',manifest_sha256=identity,
        original_plan_sha256=manifest['original_plan_sha256'],source_variant=manifest['source_variant'],
        source_file_sha256=manifest['source_file_sha256'],scorer_source_file_sha256=manifest['scorer_source_file_sha256'],
        original_receipts=manifest['original_receipts'],baseline_status_counts=manifest['baseline_status_counts'],
        planned_acquisitions=len(manifest['jobs']),selected_acquisitions=len(selected),all_selected_jobs_accounted=len(records)==len(selected),
        input_files_verified=verified,source_and_inputs_unchanged=stable,identity_error=error,
        counts={s:sum(r['status']==s for r in records) for s in sorted({r['status'] for r in records})},
        elapsed_seconds=time.monotonic()-begin,concurrent_jobs=args.jobs,global_seconds=args.global_seconds,
        jobs=sorted(records,key=lambda r:r['id']))
    fresh_json(args.run_dir/'results.json',result)
    print(json.dumps(dict(receipt=str(args.run_dir/'results.json'),counts=result['counts'])))
    return int(not(stable and result['all_selected_jobs_accounted']))


def main():
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='action',required=True)
    first=sub.add_parser('prepare');second=sub.add_parser('run')
    for parser in (first,second):
        parser.add_argument('--manifest',type=Path,required=True);parser.add_argument('--root',type=Path,required=True)
        parser.add_argument('--input-root',type=Path,required=True)
    first.add_argument('--plan',type=Path,required=True)
    first.add_argument('--prior-results',action='append',type=Path,required=True)
    second.add_argument('--output-root',type=Path,required=True);second.add_argument('--run-dir',type=Path,required=True)
    second.add_argument('--python',type=Path,default=Path(sys.executable));second.add_argument('--only-id',action='append',default=[])
    second.add_argument('--jobs',type=int,default=1);second.add_argument('--global-seconds',type=float,default=1800)
    args=p.parse_args()
    args.root=args.root.resolve();args.input_root=args.input_root.resolve()
    if args.action=='prepare':prepare(args);return 0
    args.python=args.python.absolute();args.output_root=args.output_root.resolve();args.run_dir=args.run_dir.resolve()
    if not 1<=args.jobs<=24 or not 0<args.global_seconds<=28800:p.error('require1..24jobs and at most8h')
    return run(args)


if __name__=='__main__':raise SystemExit(main())
