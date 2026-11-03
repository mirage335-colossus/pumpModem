#!/usr/bin/env python3
"""Run one frozen extension stage; retain every failure and original denominator.

Reuses the existing bounded child owner, not a shell or a new process manager.
No run is implicit: separate configure, export and acquire invocations are required.
"""
from __future__ import annotations
import argparse
from array import array
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
import importlib.util
import json
import math
from pathlib import Path
import sys
import time

HERE=Path(__file__).resolve().parent
SPEC=importlib.util.spec_from_file_location('extension_common', HERE.parents[1]/'2026-11-03-broad/reproduction/run_plan.py')
common=importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(common)


def outcome(values, threshold, bit, control):
    if len(values)!=2 or not all(math.isfinite(v) for v in values): raise ValueError('invalid bit scores')
    winner=0 if values[0]>=values[1] else 1
    accepted=values[winner]>=threshold
    if control=='signal':
        return dict(correct_admitted=int(accepted and winner==bit), wrong_admitted=int(accepted and winner!=bit), missed=int(not accepted))
    return dict(false_admitted=int(accepted), not_admitted=int(not accepted))


def read_energy(path):
    data=array('d')
    with path.open('rb') as stream: data.frombytes(stream.read())
    if sys.byteorder!='little': data.byteswap()
    if any(not math.isfinite(x) or x<0 for x in data): raise ValueError('invalid signal energy')
    return data


def acquire_argv(job, args, bindings, prior):
    workspace=Path(common.translate(job['workspace'],bindings))
    manifest=json.loads((workspace/'manifest.json').read_text())
    cfg=prior[job['depends_on'][1]]['native_configuration']['configuration']
    command=[str(args.python),str(args.root/'tools/triad_group_acquire.py'),str(workspace),
        '--doppler-width-hz','0','--window-blocks','2' if job['method']=='window' else '1',
        '--group-tile','8','--projection-tile-blocks','256','--memory-mib','512',
        '--device','cpu','--precision','fp32','--threads','1',
        '--attempts-per-day','1440','--duration-alternatives','8','--selection-methods','16',
        '--calibration-segments','2','--evaluate-simulation',
        '--output',common.translate(job['acquisition_report'],bindings),
        '--scores-output',common.translate(job['score_archive'],bindings)]
    if job['method']!='window': command += ['--reblock',str(manifest['block_count'])]
    if job['method']=='global-frequency':
        bound=float(cfg['deterministic_oscillator_cfo_bound_hz'])
        duration=float(manifest['sample_count'])/float(manifest['sample_rate'])
        if not math.isfinite(bound) or bound<0 or duration<=0: raise ValueError('invalid public frequency/calendar bound')
        command += ['--frequency-offset-grid',str(-bound),str(bound),str(1/(4*duration)),
                    '--maximum-block-phase-loss-db','0.01']
    return command


def audit_export(job, args, bindings):
    workspace=Path(common.translate(job['workspace'],bindings))
    manifest=json.loads((workspace/'manifest.json').read_text())
    if manifest['group_count']!=job['expected_groups']: raise ValueError('actual exported group count differs')
    size=sum(p.stat().st_size for p in workspace.iterdir() if p.is_file())
    if manifest['sample_count']>2_000_000 or size>2048*1024**2: raise ValueError('export exceeds declared resource bound')
    for name in ('group_observation.c128','received_energy.f64'):
        if not (workspace/name).is_file(): raise ValueError('actual noisy export lacks '+name)
    energy=read_energy(workspace/'signal_energy.f64')
    if len(energy)!=manifest['block_count']: raise ValueError('signal-energy calendar differs')
    seconds=math.fsum(energy)/manifest['sample_rate']
    parent=job.get('parent_signal_evaluation')
    return dict(group_count=manifest['group_count'],sample_count=manifest['sample_count'],
        block_count=manifest['block_count'],sample_rate=manifest['sample_rate'],export_bytes=size,
        post_erasure_signal_energy_seconds_evaluation_only=seconds,
        prior_same_seed_no_loss_energy_ratio_evaluation_only=(seconds/parent['post_erasure_channel_energy_seconds']
            if parent and job['control']=='signal' and parent['post_erasure_channel_energy_seconds']>0 else None),
        parent_calendar_equal=(manifest['sample_count']==parent['sample_count'] and manifest['sample_rate']==parent['sample_rate']) if parent else None,
        pairing_warning='Energy and seed equality are not proof of equal samples; model/rate/internal waits can change the physical calendar.')


def audit_score(job, args, bindings, plan):
    report=Path(common.translate(job['acquisition_report'],bindings))
    data=json.loads(report.read_text())
    if data['receiver_source_sha256']!=plan['scorer_source_file_sha256']['tools/triad_group_acquire.py']:
        raise ValueError('receiver hash differs')
    if len(data['captures'])!=1: raise ValueError('expected exactly one capture')
    value=data['captures'][0]; geometry=value['geometry']
    base=geometry.get('base_group_count',geometry['group_count'])
    if base!=job['expected_groups']: raise ValueError('scored base group count differs')
    groups=value['group_bit_log_likelihoods']
    if len(groups)!=geometry['group_count'] or any(len(row)!=2 or not all(math.isfinite(v) for v in row) for row in groups):
        raise ValueError('invalid effective group scores')
    score=Path(common.translate(job['score_archive'],bindings))
    if common.sha(score)!=data['scores_npz_sha256']: raise ValueError('score archive identity differs')
    truth=value['simulation_evaluation_only']
    if truth['control']!=job['control'] or truth['signal_bit']!=job['bit']: raise ValueError('evaluation identity differs')
    outcomes={g:outcome(value['bit_log_likelihoods'],value[g]['log_threshold'],job['bit'],job['control']) for g in ('strict','relaxed')}
    if outcomes!=truth['outcomes']: raise ValueError('independent outcome recount differs')
    return dict(outcomes=outcomes,bit_log_likelihoods=value['bit_log_likelihoods'],
        gates={g:value[g]['log_threshold'] for g in ('strict','relaxed')},
        base_groups=base,effective_groups=geometry['group_count'],frequency_reuse=value.get('frequency_reuse'),
        timing=value['timing'],arithmetic=value['arithmetic'])


def execute(job,args,bindings,prior,plan):
    proxy=dict(job)
    if job['stage']=='export': proxy['stage']='group-export'
    if job['stage']=='acquire':
        missing=[d for d in job['depends_on'] if d not in prior or prior[d]['status']!='completed']
        if not missing: proxy['argv']=acquire_argv(job,args,bindings,prior)
        for field in ('acquisition_report','score_archive'):
            path=Path(common.translate(job[field],bindings))
            if path.exists(): raise ValueError('output exists: '+str(path))
            path.parent.mkdir(parents=True,exist_ok=True)
    if job['stage']=='export':
        workspace=Path(common.translate(job['workspace'],bindings))
        if workspace.exists(): raise ValueError('workspace exists: '+str(workspace))
        workspace.parent.mkdir(parents=True,exist_ok=True)
    record=common.execute(proxy,args,bindings,prior); record['stage']=job['stage']
    record.update(case=job['case'],control=job['control'],bit=job['bit'],method=job.get('method'))
    if job['stage']=='export': record['output_files']=common.inventory(Path(common.translate(job['workspace'],bindings)),args.output_root)
    if job['stage']=='acquire':
        for field in ('acquisition_report','score_archive'):
            record['output_files']+=common.inventory(Path(common.translate(job[field],bindings)),args.output_root)
    if record['status']!='completed': return record
    try:
        if job['stage']=='configure':
            cfg=record['native_configuration']['configuration']
            if not cfg['public_search']['covers_initial_timing_prior']: raise ValueError('public timing bank does not cover prior')
            if job['expected_groups']>10000: raise ValueError('native group cap exceeded')
            if job['expected_groups']>5000: raise ValueError('receiver base-group cap exceeded')
        elif job['stage']=='export': record['evaluation_only']=audit_export(job,args,bindings)
        else: record['acquisition_summary']=audit_score(job,args,bindings,plan)
    except (OSError,ValueError,KeyError) as error: record.update(status='invalid_output',reason=str(error))
    return record


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('plan','root','binary','output-root','run-dir'): p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--python',type=Path,default=Path(sys.executable))
    p.add_argument('--stage',choices=('configure','export','acquire'),required=True)
    p.add_argument('--prior-results',action='append',type=Path,default=[])
    p.add_argument('--only-id',action='append',default=[])
    p.add_argument('--jobs',type=int,default=1); p.add_argument('--global-seconds',type=float,default=1800)
    args=p.parse_args()
    if not 1<=args.jobs<=24 or not 0<args.global_seconds<=8*3600: p.error('require1..24jobs and at most8h')
    for field in ('plan','root','binary','output_root','run_dir'): setattr(args,field,getattr(args,field).resolve())
    args.python=args.python.absolute()
    if args.run_dir.exists(): p.error('run-dir must be fresh')
    plan=json.loads(args.plan.read_text()); plan_hash=common.sha(args.plan)
    if plan.get('schema')!='triad-extension-physical-plan-v1' or not plan.get('frozen'): p.error('a frozen extension plan is required')
    common.verify_source(plan,args.root)
    if args.stage=='acquire': common.verify_source({'source_file_sha256':plan['scorer_source_file_sha256']},args.root)
    binary_hash=common.sha(args.binary)
    if binary_hash!=plan['expected_binary_sha256']: p.error('executable differs from frozen plan')
    definitions={j['id']:j for j in plan['jobs']}
    if len(definitions)!=len(plan['jobs']): p.error('duplicate job identifiers')
    for job in definitions.values(): common.check_job(job)
    prior,receipts=common.read_prior(args.prior_results,plan_hash,definitions,args.output_root,
                                   expected_binary=binary_hash,expected_source=plan['source_file_sha256'])
    selected=[j for j in plan['jobs'] if j['stage']==args.stage]
    if args.only_id:
        if not set(args.only_id)<={j['id'] for j in selected}: p.error('unknown selected job')
        selected=[j for j in selected if j['id'] in args.only_id]
    if not selected or any(j['id'] in prior for j in selected): p.error('empty selection or implicit rerun')
    args.run_dir.mkdir(parents=True); args.output_root.mkdir(parents=True,exist_ok=True)
    bindings=dict(ROOT=str(args.root),OUT=str(args.output_root),BINARY=str(args.binary),PYTHON=str(args.python))
    started=time.monotonic(); deadline=started+args.global_seconds; records=[]; pending={}; cursor=0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        while cursor<len(selected) or pending:
            while cursor<len(selected) and len(pending)<args.jobs:
                job=selected[cursor]; cursor+=1
                if time.monotonic()+job['timeout_seconds']+10>deadline:
                    records.append(dict(id=job['id'],stage=job['stage'],definition_sha256=job['definition_sha256'],
                        status='not_started_deadline',depends_on=job['depends_on'],output_files=[]))
                else: pending[pool.submit(execute,job,args,bindings,prior,plan)]=job
            if pending:
                done,_=wait(pending,timeout=1,return_when=FIRST_COMPLETED)
                for future in done:
                    job=pending.pop(future)
                    try: record=future.result()
                    except Exception as error: record=dict(id=job['id'],stage=job['stage'],definition_sha256=job['definition_sha256'],
                        status='runner_error',reason=repr(error),depends_on=job['depends_on'],output_files=[])
                    records.append(record)
                    print(json.dumps(dict(finished=job['id'],status=record['status'],seconds=record.get('elapsed_seconds'))),flush=True)
    valid=True; error=None
    try:
        common.verify_source(plan,args.root)
        if args.stage=='acquire': common.verify_source({'source_file_sha256':plan['scorer_source_file_sha256']},args.root)
    except ValueError as exc: valid=False; error=str(exc)
    binary_valid=common.sha(args.binary)==binary_hash
    payload=dict(schema='triad-extension-physical-stage-v1',plan_sha256=plan_hash,stage=args.stage,
        binary_sha256=binary_hash,source_file_sha256=plan['source_file_sha256'],
        scorer_source_file_sha256=plan['scorer_source_file_sha256'] if args.stage=='acquire' else None,
        source_unchanged_at_end=valid,source_error=error,binary_unchanged_at_end=binary_valid,
        all_selected_jobs_accounted=len(records)==len(selected),prior_receipts=receipts,
        planned_stage_jobs=sum(j['stage']==args.stage for j in plan['jobs']),selected_jobs=len(selected),
        global_seconds_bound=args.global_seconds,concurrent_jobs=args.jobs,elapsed_seconds=time.monotonic()-started,
        counts={s:sum(r['status']==s for r in records) for s in sorted({r['status'] for r in records})},
        jobs=sorted(records,key=lambda r:r['id']))
    (args.run_dir/'results.json').write_text(json.dumps(payload,indent=2,sort_keys=True,allow_nan=False)+'\n')
    print(json.dumps(dict(receipt=str(args.run_dir/'results.json'),counts=payload['counts'])))
    return int(not(valid and binary_valid and payload['all_selected_jobs_accounted']))


if __name__=='__main__': raise SystemExit(main())
