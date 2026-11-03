#!/usr/bin/env python3
"""Recount frozen broad-study outcomes and post-channel energy independently.

This reads simulator truth only for evaluation, after the receiver has scored.
It never feeds truth or useful energy back into a receiver or duration planner.
"""
from __future__ import annotations
import argparse
import importlib.util
import json
import math
from pathlib import Path
import numpy as np
from scipy.stats import beta

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('broad_runner',HERE/'run_plan.py')
runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)

def recount(scores,truth,threshold,control):
    scores=np.asarray(scores)
    if scores.ndim!=2 or scores.shape[1]!=2 or not len(scores) or not np.isfinite(scores).all():
        raise ValueError('expected finite trial-by-two-candidate scores')
    winners=np.argmax(scores,axis=1)
    accepted=np.max(scores,axis=1)>=threshold
    if control=='signal':
        return {'trials':len(scores),'correct_accepted':int(np.sum(accepted&(winners==truth))),
            'wrong_accepted':int(np.sum(accepted&(winners!=truth))),
            'missed':int(np.sum(~accepted)),'raw_correct':int(np.sum(winners==truth))}
    if control not in ('null','wrong-key'):raise ValueError('unknown control')
    return {'trials':len(scores),'false_admitted':int(np.sum(accepted)),
        'not_admitted':int(np.sum(~accepted))}

def interval(success,n,scope):
    return {'one_sided_95_lower':float(beta.ppf(.05,success,n-success+1)) if success else 0.,
        'one_sided_95_upper':float(beta.ppf(.95,success+1,n-success)) if success<n else 1.,'scope':scope}

def resolve(text,output):
    return Path(text.replace('{OUT}',str(output)))

def workspace_energy(job,output):
    directory=resolve(job['workspace'],output)
    manifest=json.loads((directory/'manifest.json').read_text())
    blocks=int(manifest['block_count']);fs=float(manifest['sample_rate'])
    energy=np.fromfile(directory/'signal_energy.f64',dtype='<f8')
    valid=np.fromfile(directory/'valid_rows.u64',dtype='<u8')
    if energy.shape!=(blocks,) or valid.shape!=(blocks,) or not np.isfinite(energy).all() or np.any(energy<0):
        raise ValueError('invalid evaluation energy/validity arrays')
    usable=float(np.sum(energy,dtype=np.float64))/fs
    return {'export_id':job['id'],'case':job['case'],'bit':job['bit'],'control':job['control'],
        'cn0_db_hz':job['cn0_db_hz'],'seed_address_index':job['seed_address_index'],
        'sample_count':manifest['sample_count'],'sample_rate':fs,'block_count':blocks,
        'source_elapsed_seconds':manifest['actual_elapsed_seconds'],
        'capture_seconds':manifest['sample_count']/fs,'valid_observation_seconds':int(np.sum(valid,dtype=np.uint64))/fs,
        'valid_observation_fraction':int(np.sum(valid,dtype=np.uint64))/manifest['sample_count'],
        'post_erasure_channel_energy_seconds':usable,
        'available_energy_over_n0':usable*10**(job['cn0_db_hz']/10),
        'energy_scope':'post-erasure sampled signal energy; evaluation only; no normalization to nominal transmit energy',
        'selected_truth_arrival_seconds_simulation_only':manifest.get('selected_truth_arrival_seconds_simulation_only'),
        'nominal_payload_energy_seconds_lower':manifest.get('nominal_payload_energy_seconds_lower')}

def score_rows(job,record,plan,output):
    report_path=resolve(job['report'],output)
    data=json.loads(report_path.read_text())
    if data['source_sha256']!=plan['source_file_sha256']['tools/triad-conditional-sweep.py']:
        raise ValueError('receiver source does not match frozen plan')
    for name,value in data['dependency_sha256'].items():
        if plan['source_file_sha256'].get('tools/'+name)!=value:raise ValueError('receiver dependency differs: '+name)
    scores_path=report_path.with_name(data['scores_file'])
    if scores_path.parent!=report_path.parent or runner.sha(scores_path)!=data['scores_sha256']:
        raise ValueError('score archive hash or location differs')
    if len(data['paths'])!=1:raise ValueError('expected one frozen physical path per job')
    expected_mode='conditional' if job['stage']=='conditional' else 'actual-noisy'
    if data['observation_mode']!=expected_mode:raise ValueError('observation mode differs')
    path=data['paths'][0]
    if path['status']!='completed' or record['status']!='completed':
        return [],{'job_id':job['id'],'status':record['status'],'receiver_status':path['status'],
            'error':path.get('error'),'scope':'unscored/failed cells are retained separately; no successful-reception credit'}
    manifest=json.loads((resolve(job['workspace'],output)/'manifest.json').read_text())
    truth=int(manifest['signal_bit'])
    if truth!=job['bit']:raise ValueError('frozen transmitted bit differs')
    rows=[]
    with np.load(scores_path,allow_pickle=False) as arrays:
        for condition in path['conditions']:
            control=condition['control'];cn0=condition['cn0_db_hz'] if expected_mode=='conditional' else job['cn0_db_hz']
            if expected_mode=='conditional' and cn0!=job['cn0_db_hz']:raise ValueError('conditional C/N0 differs')
            methods=condition['methods']
            if set(methods)!={job['method']}:raise ValueError('receiver method differs')
            detail=methods[job['method']];scores=arrays[detail['score_array']]
            if len(scores)!=(4 if expected_mode=='conditional' else 1):raise ValueError('trial count differs')
            strict=float(detail['threshold'])
            if not math.isclose(strict,data['family']['fixed_window_log_threshold'],rel_tol=0,abs_tol=1e-12):
                raise ValueError('family and method threshold differ')
            checked=recount(scores,truth,strict,control)
            if checked!=detail['counts']:raise ValueError('independent count disagrees with receiver report')
            # Same 60-second attempts, family sizes and strict residual UCB.
            # Relaxed horizon halves attempts and its score-alpha is100x:
            # log threshold falls by log(2*100). No noisy-score rescaling.
            relaxed=strict-math.log(200.)
            scope=('conditional noise on one fixed sampled path; no physical-path qualification'
                if expected_mode=='conditional' else 'pointwise fixed-size actual physical observations; diagnostic sample size')
            for target,threshold in [('strict',strict),('relaxed',relaxed)]:
                counts=recount(scores,truth,threshold,control)
                success=counts['correct_accepted' if control=='signal' else 'false_admitted']
                rows.append({'job_id':job['id'],'case':job['case'],'stage':job['stage'],'method':job['method'],
                    'target':target,'threshold':threshold,'cn0_db_hz':cn0,'control':control,'bit':truth,
                    'counts':counts,'interval':interval(success,len(scores),scope),
                    'score_min':float(np.min(scores)),'score_max':float(np.max(scores)),
                    'receiver_seconds':detail['receiver_seconds'],'report_wall_seconds':data['wall_seconds'],
                    'bounded_job_seconds':record.get('elapsed_seconds'),'diagnostics':detail.get('diagnostics_last_batch'),
                    'arithmetic':data.get('arithmetic')})
    return rows,None

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--plan',type=Path,required=True);p.add_argument('--receipts',nargs='+',type=Path,required=True)
    p.add_argument('--output-root',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args(argv)
    if args.output.exists():p.error('analysis output must be fresh')
    plan=json.loads(args.plan.read_text());definitions={j['id']:j for j in plan['jobs']}
    for job in definitions.values():runner.check_job(job)
    records,receipts=runner.read_prior(args.receipts,runner.sha(args.plan),definitions,args.output_root,
        plan.get('expected_binary_sha256'),plan['build_provenance_sha256'],plan['source_file_sha256'])
    for receipt in args.receipts:
        data=json.loads(receipt.read_text())
        if not data['source_unchanged_at_end'] or not data['binary_unchanged_at_end'] or not data['all_selected_jobs_accounted']:
            raise ValueError('incomplete or mutated stage: '+str(receipt))
        if data['source_file_sha256']!=plan['source_file_sha256']:raise ValueError('stage source binding differs')
    outcomes=[];failures=[];energies=[];opportunities=[]
    for ident,record in sorted(records.items()):
        job=definitions[ident]
        if 'report' in job:
            planned=4 if job['stage']=='conditional' else 1
            attempted=record['status'] not in ('unsupported_receiver_grid','not_started_dependency','not_started_deadline','not_started','setup_error')
            controls=['signal','null'] if job['stage']=='conditional' else [job['control']]
            for control in controls:
                failed=planned if attempted and record['status']!='completed' else 0
                opportunities.append({'job_id':ident,'case':job['case'],'stage':job['stage'],'method':job['method'],
                    'cn0_db_hz':job['cn0_db_hz'],'control':control,'status':record['status'],
                    'planned_trials':planned,'attempted_opportunity_denominator':planned if attempted else 0,
                    'completed_scored_trials':planned if record['status']=='completed' else 0,
                    'processing_failed_trials':failed,'not_started_trials':0 if attempted else planned,
                    'failed_signal_opportunities_count_as_misses':failed if control=='signal' else 0,
                    'unresolved_control_trials':failed if control!='signal' else 0,
                    'control_qualification_complete':record['status']=='completed' if control!='signal' else None})
        if record['status']!='completed':
            failures.append({'job_id':ident,'case':job['case'],'stage':job['stage'],'status':record['status'],
                'reason':record.get('reason'),'dependencies':record.get('unavailable_dependencies')})
        if job['stage']=='export' and record['status']=='completed':energies.append(workspace_energy(job,args.output_root))
        if 'report' in job and resolve(job['report'],args.output_root).is_file():
            rows,error=score_rows(job,record,plan,args.output_root);outcomes+=rows
            if error:failures.append(error)
    baselines={e['bit']:e for e in energies if e['case']=='erasures-none'}
    for row in energies:
        if row['case'].startswith('erasures-') and row['bit'] in baselines:
            ref=baselines[row['bit']]
            if ref['seed_address_index']!=row['seed_address_index']:raise ValueError('erasure pairing differs')
            row['energy_ratio_to_same_seed_no_erasure']=row['post_erasure_channel_energy_seconds']/ref['post_erasure_channel_energy_seconds']
    counts={status:sum(r['status']==status for r in records.values()) for status in sorted({r['status'] for r in records.values()})}
    result={'schema':'triad-broad-independent-analysis-v1','plan_sha256':runner.sha(args.plan),
        'analysis_sha256':runner.sha(__file__),'receipts':receipts,'job_status_counts':counts,
        'unexecuted_job_ids':sorted(set(definitions)-set(records)),'outcomes':outcomes,'failures':failures,
        'opportunity_accounting':opportunities,
        'physical_energy_evaluation':energies,'limitations':[
            'No pooling across heterogeneous configurations, methods, targets or controls; these are diagnostic rows, not Cartesian coverage.',
            'Conditional repetitions share one physical path and noise addresses across C/N0/methods. They do not add independent physical paths.',
            'Actual receiver failures must count as missed opportunities in reliability interpretation; failed controls prevent qualification.',
            'Wrong-key admission counts are empirical structured-signal controls, separate from Gaussian noise-only calibration.',
            'Relaxed decisions retain the stricter residual UCB and change only the predeclared log gate; no empirical rare-event claim.',
            'Nominal timing/clock/epoch compression and omitted trajectory uncertainty can explain failures; no duration-bound inversion is performed.']}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+'\n')
    print(json.dumps({'output':str(args.output),'job_status_counts':counts,'outcome_rows':len(outcomes),'failure_rows':len(failures)}))

if __name__=='__main__':main()
