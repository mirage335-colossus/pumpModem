#!/usr/bin/env python3
"""Pair old/new raw evidence, admissions, failures and cost without fitting gates."""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import numpy as np
import prepare
import run

evaluate=prepare.load_file('rescore_original_analysis',prepare.HERE.parent/'reproduction/analyze.py')

def opportunity(job,record,completed_controls=None):
    n=4 if job['stage']=='conditional' else 1
    status=record['status'] if record else 'unexecuted'
    unstarted=status in ('unexecuted','unsupported_receiver_grid','not_started_dependency','not_started_deadline','not_started','setup_error')
    controls=['signal','null'] if job['stage']=='conditional' else [job['control']]
    completed=set(controls if status=='completed' else ()) if completed_controls is None else set(completed_controls)
    rows=[]
    for control in controls:
        done=n if control in completed else 0
        failed=n if not unstarted and not done else 0
        rows.append(dict(control=control,planned_trials=n,attempted_denominator=0 if unstarted else n,
            completed_trials=done,processing_failed_trials=failed,
            failed_signal_opportunities_as_misses=failed if control=='signal' else 0,
            unresolved_control_trials=failed if control!='signal' else 0,not_started_trials=n if unstarted else 0))
    return rows

def completed_report(job,record,source,output,input_hashes=None):
    """Validate retained complete conditions even when a later condition failed.

    This never promotes the enclosing job to success. The physical workspace
    inventory is already verified by prepare.verify_manifest; passing its hashes
    avoids rereading large waveform arrays for every conditional C/N0 row.
    """
    result={'arrays':{},'outcomes':[],'completed_controls':set(),'partial':False}
    if record is None:return result
    path=evaluate.resolve(job['report'],output)
    if not path.is_file():
        if record['status']=='completed':raise ValueError('completed job has no report')
        return result
    data=json.loads(path.read_text());sources=source['source_file_sha256']
    if data['source_sha256']!=sources['tools/triad-conditional-sweep.py']:
        raise ValueError('receiver source does not match bound variant')
    dependencies=set(prepare.SCORER)-{'triad-conditional-sweep.py','triad_phase_frequency.py'}
    if set(data['dependency_sha256'])!=dependencies or any(
            sources.get('tools/'+name)!=value for name,value in data['dependency_sha256'].items()):
        raise ValueError('receiver dependency closure differs')
    name=data['scores_file']
    if Path(name).name!=name or name!=path.with_suffix('.scores.npz').name:
        raise ValueError('score archive location differs')
    arraypath=path.with_name(name)
    if prepare.sha(arraypath)!=data['scores_sha256']:raise ValueError('score archive changed')
    conditional=job['stage']=='conditional';mode='conditional' if conditional else 'actual-noisy'
    if data['observation_mode']!=mode or len(data['paths'])!=1:raise ValueError('observation mode/path count differs')
    physical=data['paths'][0];workspace=evaluate.resolve(job['workspace'],output)
    if Path(physical['input']).resolve()!=workspace.resolve():raise ValueError('physical workspace differs')
    manifest=json.loads((workspace/'manifest.json').read_text());truth=manifest['signal_bit']
    if truth!=job['bit'] or isinstance(truth,bool):raise ValueError('transmitted bit differs')
    conditions=physical['conditions'];expected={'signal','null'} if conditional else {job['control']}
    result['partial']=record['status']!='completed' or physical['status']!='completed'
    if conditions:
        names={'manifest.json','gram.c128','intervals.u64','valid_rows.u64'}|(
            {'mean.c128','signal_energy.f64'} if conditional else {'observation.c128','received_energy.f64'})
        reported=physical['sampled_input_hashes' if conditional else 'input_hashes']
        bound=input_hashes if input_hashes is not None else {name:prepare.sha(workspace/name) for name in names}
        if set(reported)!=names or any(reported[name]!=bound.get(name) for name in names):
            raise ValueError('reported physical input hashes differ')
    def argument(flag):
        return job['argv'][job['argv'].index(flag)+1]
    horizon=float(argument('--horizon-seconds'));spacing=float(argument('--attempt-spacing'))
    alpha=float(argument('--false-probability'));durations=int(argument('--duration-alternatives'))
    methods=int(argument('--selection-methods'));segments=int(argument('--calibration-alternatives'))
    if not (math.isfinite(horizon) and horizon>0 and math.isfinite(spacing) and spacing>0 and 0<alpha<1 and min(durations,methods,segments)>0):
        raise ValueError('invalid frozen admission family')
    attempts=math.ceil(horizon/spacing);strict=math.log(attempts*durations*methods*2/(alpha*.5))
    family=data['family']
    expected_family={'fixed_window_log_threshold':strict,'score_probability':alpha*.5,
        'calibration_failure_probability':alpha*.5,'total_false_admission_bound':alpha,
        'delta_per_segment':alpha*.5/(attempts*durations*segments)}
    if any(not math.isclose(float(family[k]),v,rel_tol=2e-14,abs_tol=0) for k,v in expected_family.items()):
        raise ValueError('predeclared admission family differs')
    # Preserve the receiver's upward-rounded gate after checking its declared
    # budget; recomputing logs can move a threshold by one floating-point ULP.
    strict=float(family['fixed_window_log_threshold'])
    def seconds(value):
        value=float(value)
        if not math.isfinite(value) or value<0:raise ValueError('invalid reported execution timing')
        return value
    wall=seconds(data['wall_seconds'])
    if record.get('elapsed_seconds') is not None:seconds(record['elapsed_seconds'])
    with np.load(arraypath,allow_pickle=False) as archive:
        for condition in conditions:
            control=condition['control'];cn0=condition['cn0_db_hz']
            if control not in expected or control in result['completed_controls']:raise ValueError('unexpected or duplicate completed condition')
            if cn0!=(job['cn0_db_hz'] if conditional else None):raise ValueError('condition C/N0 differs')
            if set(condition['methods'])!={job['method']}:raise ValueError('receiver method differs')
            detail=condition['methods'][job['method']];scores=np.array(archive[detail['score_array']])
            if len(scores)!=(4 if conditional else 1):raise ValueError('condition trial count differs')
            if float(detail['threshold'])!=strict:raise ValueError('method gate differs')
            counts=evaluate.recount(scores,truth,strict,control)
            if counts!=detail['counts']:raise ValueError('independent condition recount differs')
            duration=seconds(detail['receiver_seconds'])
            result['arrays'][(control,cn0)]=(scores,strict,duration)
            result['completed_controls'].add(control)
            for target,gate in [('strict',strict),('relaxed',strict-math.log(200.))]:
                checked=evaluate.recount(scores,truth,gate,control)
                success=checked['correct_accepted' if control=='signal' else 'false_admitted']
                result['outcomes'].append(dict(job_id=job['id'],case=job['case'],stage=job['stage'],method=job['method'],
                    target=target,threshold=gate,cn0_db_hz=job['cn0_db_hz'],control=control,bit=truth,counts=checked,
                    interval=evaluate.interval(success,len(scores),'Completed condition on a fixed physical path; partial jobs remain failed and unqualified.'),
                    score_min=float(np.min(scores)),score_max=float(np.max(scores)),receiver_seconds=duration,
                    report_wall_seconds=wall,bounded_job_seconds=record.get('elapsed_seconds'),
                    diagnostics=detail.get('diagnostics_last_batch'),arithmetic=data.get('arithmetic'),
                    enclosing_job_status=record['status'],enclosing_receiver_status=physical['status'],partial_job_evidence=result['partial']))
    if record['status']=='completed' and (physical['status']!='completed' or result['completed_controls']!=expected):
        raise ValueError('completed job lacks complete valid conditions')
    return result

def compare_scores(reference,candidate,threshold):
    a=np.asarray(reference);b=np.asarray(candidate)
    if a.shape!=b.shape or a.ndim!=2 or a.shape[1]!=2 or not np.isfinite(a).all() or not np.isfinite(b).all():
        raise ValueError('raw paired scores have invalid shape or nonfinite values')
    diff=b-a
    result=dict(trials=len(a),maximum_absolute_score_change=float(np.max(np.abs(diff))),
        relative_linf_score_change=float(np.max(np.abs(diff))/max(1.,float(np.max(np.abs(a))))),
        winner_changes=int(np.sum(a.argmax(axis=1)!=b.argmax(axis=1))))
    for name,gate in [('strict',threshold),('relaxed',threshold-math.log(200.))]:
        result[name+'_admission_changes']=int(np.sum((np.max(a,axis=1)>=gate)!=(np.max(b,axis=1)>=gate)))
    return result

def compare_jobs(first_report,second_report):
    a=first_report['arrays'];b=second_report['arrays']
    rows=[]
    for key in sorted(set(a)&set(b),key=str):
        av,ag,at=a[key];bv,bg,bt=b[key]
        if ag!=bg:raise ValueError('predeclared admission gate changed')
        rows.append(dict(control=key[0],cn0_db_hz=key[1],**compare_scores(av,bv,ag),
            reference_receiver_seconds=at,candidate_receiver_seconds=bt,
            reference_over_candidate_receiver_time=at/bt if bt>0 else None))
    return rows

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest',type=Path,required=True);p.add_argument('--receipts',nargs='+',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);args=p.parse_args(argv)
    if args.output.exists():p.error('analysis output must be fresh')
    manifest=json.loads(args.manifest.read_text());core,baseline=prepare.verify_manifest(manifest)
    definitions={j['id']:j for j in manifest['jobs']};originals={j['id']:j for j in core['jobs']}
    output=Path(manifest['output_root']);oldoutput=Path(manifest['original_output'])
    fixed,refs=run.read_variant_receipts(args.receipts,prepare.sha(args.manifest),definitions,output)
    for path in args.receipts:
        value=json.loads(path.read_text())
        if value['source_file_sha256']!=manifest['source_file_sha256'] or value['binary_sha256']!=manifest['binary']['sha256']:
            raise ValueError('variant receipt source/binary closure differs')
    newsource={'source_file_sha256':{**core['source_file_sha256'],**manifest['source_file_sha256']}}
    pairs=[];failures=[];outcomes=[];checked_reports={}
    for ident,job in definitions.items():
        old=baseline[ident];new=fixed.get(ident);original=originals[ident]
        row=dict(job_id=ident,case=job['case'],stage=job['stage'],method=job['method'],cn0_db_hz=job['cn0_db_hz'],
            baseline_status=old['status'],fixed_status=new['status'] if new else 'unexecuted')
        exported=baseline[job['depends_on'][0]]
        input_hashes={Path(item['path']).name:item['sha256'] for item in exported.get('output_files',[])}
        for variant,definition,record,source,outdir in [('baseline',original,old,core,oldoutput),('numerical-fix',job,new,newsource,output)]:
            checked=completed_report(definition,record,source,outdir,input_hashes)
            checked_reports[(variant,ident)]=checked
            for item in checked['outcomes']:item['variant']=variant
            outcomes.extend(checked['outcomes'])
            field='baseline_opportunities' if variant=='baseline' else 'fixed_opportunities'
            row[field]=opportunity(definition,record,checked['completed_controls'])
            if record is not None and record['status']!='completed':
                failures.append(dict(job_id=ident,variant=variant,status=record['status'],reason=record.get('reason'),
                    retained_completed_controls=sorted(checked['completed_controls'])))
        row['paired_raw_scores']=compare_jobs(checked_reports[('baseline',ident)],checked_reports[('numerical-fix',ident)])
        row['baseline_bounded_seconds']=old.get('elapsed_seconds')
        row['fixed_bounded_seconds']=new.get('elapsed_seconds') if new else None
        if old.get('elapsed_seconds') and new and new.get('elapsed_seconds'):
            row['baseline_over_fixed_bounded_time']=old['elapsed_seconds']/new['elapsed_seconds']
        pairs.append(row)
    precision=[]
    benchmarks=[j for j in definitions.values() if j['stage']=='benchmark']
    for variant,records,outdir,jobs in [('baseline',baseline,oldoutput,originals),('numerical-fix',fixed,output,definitions)]:
        for candidate in benchmarks:
            reference=next((j for j in benchmarks if j['case']==candidate['case'] and j['method']==candidate['method'] and j['backend']=='cpu' and j['precision']=='fp64'),None)
            if reference is None or candidate['id']==reference['id']:continue
            precision.append(dict(variant=variant,case=candidate['case'],method=candidate['method'],
                reference_job=reference['id'],candidate_job=candidate['id'],
                reference_status=records.get(reference['id'],{}).get('status','unexecuted'),
                candidate_status=records.get(candidate['id'],{}).get('status','unexecuted'),
                paired_raw_scores=compare_jobs(checked_reports[(variant,reference['id'])],checked_reports[(variant,candidate['id'])])))
    transitions={}
    for row in pairs:
        key=row['baseline_status']+' -> '+row['fixed_status'];transitions[key]=transitions.get(key,0)+1
    result=dict(schema='triad-broad-numerical-rescore-analysis-v1',manifest_sha256=prepare.sha(args.manifest),
        analysis_sha256=prepare.sha(__file__),baseline_plan_sha256=manifest['baseline_plan']['sha256'],
        source_closure=manifest['source_file_sha256'],baseline_source_closure={**core['source_file_sha256'],**manifest['baseline_extra_file_sha256']},
        variant_receipts=refs,status_transitions=transitions,
        job_pairs=pairs,outcomes=outcomes,failures=failures,precision_comparisons=precision,
        limitations=['Numerical repair comparison on identical retained paths/noise addresses; not independent physical validation.',
            'All predeclared strict/relaxed gates are unchanged. Strict residual-UCB allocation remains conservative at the relaxed gate.',
            'Completed conditions from partially failed jobs retain validated scores/outcomes; enclosing jobs remain failed. Only missing signal conditions become processing misses; missing controls remain unresolved.',
            'Single shared-host latency samples include contention and startup. They do not establish general backend speedups.',
            'No tuning to these outcomes, threshold fitting, new acquisition hypotheses or duration inversion is performed.'])
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+'\n')
    print(json.dumps({'output':str(args.output),'status_transitions':transitions,'paired_jobs':len(pairs)}))

if __name__=='__main__':main()
