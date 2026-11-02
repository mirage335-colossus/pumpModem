#!/usr/bin/env python3
"""Recount the frozen actual-input CFO CPU/GPU benchmark, including failures.

No receiver results are generated here. Precision comparisons require matching
input and source hashes, priors, grids, thresholds and path identities. Different
8192 path counts permit score comparison on their overlap, not a speedup ratio.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys

METHOD='cfo-grid-ucb'
REQUIRED_HASHES={'manifest.json','gram.c128','observation.c128','received_energy.f64','intervals.u64','valid_rows.u64'}


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def finite(value,name):
    if isinstance(value,bool) or not isinstance(value,(int,float)) or not math.isfinite(value):
        raise ValueError(name+' must be finite numeric')
    return float(value)


def path_key(capture):
    p=Path(capture['input'])
    return '/'.join(p.parts[-2:])


def inspect_report(job,directory,runner,declared_inputs):
    meta=job['benchmark'];path=directory/meta['report']
    row={'name':job['name'],**meta,'report_present':path.exists(),'timeout_seconds':job['timeout_seconds']}
    if job['name'] in runner:
        receipt=runner[job['name']]
        row['runner_returncode']=receipt.get('returncode')
        row['runner_elapsed_seconds']=receipt.get('elapsed_seconds')
        row['runner_outcome']=receipt.get('runner',{}).get('outcome',receipt.get('outcome'))
    if not path.exists():
        row.update(status='missing',error='No completed report; consult bounded-run receipt/log.')
        return row,None
    try:
        report=json.loads(path.read_text())
        if report.get('schema')!='triad-cfo-batch-study-v1':raise ValueError('unexpected report schema')
        config=report['configuration']
        for key in ('device','precision','particles'):
            if config.get(key)!=meta[key]:raise ValueError('planned '+key+' differs from actual report')
        if config.get('cn0')!=-60 or config.get('reference_variance') is not None:
            raise ValueError('planned C/N0=-60 dB-Hz reference differs from actual report')
        if config.get('coherence')!='infinity' or config.get('frequency_bound')!=1e-6:
            raise ValueError('fixed static channel/CFO prior differs from plan')
        if config.get('phase_diffusion')!=0 or config.get('frequency_random_walk')!=0 or config.get('resampling')!='none' or config.get('frequency_initialization')!='midpoint-grid':
            raise ValueError('receiver is not the declared deterministic CFO model')
        if config.get('selection_methods')!=16 or config.get('duration_alternatives')!=8:
            raise ValueError('operational family differs from declared K16/D8')
        captures=report['captures']
        if len(captures)!=meta['path_count']:raise ValueError('planned capture count differs')
        completed=[c for c in captures if c.get('status')=='completed']
        if len(completed)!=len(captures) or report['failed_physical_capture_records']:
            row.update(status='failed',failed_captures=len(captures)-len(completed),capture_errors=[{k:c.get(k) for k in ('input','status','error_type','error')} for c in captures if c.get('status')!='completed'])
            return row,report
        if row.get('runner_returncode',0)!=0:raise ValueError('bounded runner did not complete successfully')
        if meta['particles']==1024:
            expected_indices=declared_inputs['paired1024_path_indices']
        elif meta['particles']==8192:
            expected_indices=declared_inputs['gpu8192_path_indices' if meta['device'].startswith('cuda') else 'cpu8192_path_indices']
        else:
            raise ValueError('particle count has no declared benchmark path set')
        if not isinstance(expected_indices,list) or any(isinstance(i,bool) or not isinstance(i,int) or i<0 for i in expected_indices):
            raise ValueError('declared benchmark path indices are invalid')
        expected_identities={f"{declared_inputs['cohort']}/path-{i:05d}" for i in expected_indices}
        if len(expected_identities)!=len(expected_indices) or len(expected_indices)!=meta['path_count']:
            raise ValueError('declared benchmark path count or uniqueness differs from job')
        identities=set()
        for capture in captures:
            key=path_key(capture)
            if key in identities:raise ValueError('duplicate physical path identity')
            identities.add(key)
            if set(capture['input_hashes'])!=REQUIRED_HASHES:raise ValueError('missing actual input hashes')
            method=capture['methods'][METHOD]
            if len(method['bit_log_scores'])!=2:raise ValueError('expected two bit scores')
            [finite(v,'bit score') for v in method['bit_log_scores']]
            finite(method['threshold'],'score threshold')
        if identities!=expected_identities:
            raise ValueError('actual cohort/path indices differ from the declared benchmark input set')
        batches=report['batches']
        if not batches or any(b.get('status')!='completed' for b in batches):raise ValueError('incomplete batch timing records')
        times={name:sum(finite(b[name],name) for b in batches) for name in ('preparation_seconds','transfer_seconds','resident_inference_seconds')}
        times['tool_end_to_end_seconds']=finite(report['wall_seconds_joint'],'wall_seconds_joint')
        if row.get('runner_elapsed_seconds') is not None:times['process_end_to_end_seconds']=finite(row['runner_elapsed_seconds'],'runner elapsed')
        row.update(status='completed',report_sha256=sha256(path),timing=times,source_sha256=report['source_sha256'],dependency_sha256=report['dependency_sha256'],
            correct_accepted=sum(c['methods'][METHOD]['counts'].get('correct_accepted',0) for c in captures),
            wrong_accepted=sum(c['methods'][METHOD]['counts'].get('wrong_accepted',0) for c in captures),
            missed=sum(c['methods'][METHOD]['counts'].get('missed',0) for c in captures))
        return row,report
    except (KeyError,TypeError,ValueError,OSError) as error:
        row.update(status='invalid',error=str(error));return row,None


def compare(left,right,left_name,right_name):
    result={'reference':left_name,'candidate':right_name}
    if left['source_sha256']!=right['source_sha256'] or left['dependency_sha256']!=right['dependency_sha256']:
        return dict(result,status='invalid',error='receiver source/dependency hashes differ')
    for key in ('particles','frequency_bound','frequency_initialization','coherence','tap_powers','taps','cn0','reference_variance','selection_methods','duration_alternatives','false_probability','attempt_spacing','horizon_seconds','calibration_alternatives'):
        if left['configuration'].get(key)!=right['configuration'].get(key):
            return dict(result,status='invalid',error='comparison configuration differs: '+key)
    a={path_key(c):c for c in left['captures']};b={path_key(c):c for c in right['captures']}
    keys=sorted(set(a)&set(b))
    if not keys:return dict(result,status='invalid',error='no common physical paths')
    max_abs=max_rel=0.;rank_diff=admission_diff=0;minimum_margin=minimum_gate=math.inf
    per_path=[]
    for key in keys:
        x,y=a[key],b[key]
        if x['input_hashes']!=y['input_hashes']:return dict(result,status='invalid',error='actual input hashes differ for '+key)
        if x['control']!=y['control']:return dict(result,status='invalid',error='control differs for '+key)
        rx,ry=x['methods'][METHOD],y['methods'][METHOD]
        if rx['threshold']!=ry['threshold']:return dict(result,status='invalid',error='threshold differs for '+key)
        sx,sy=rx['bit_log_scores'],ry['bit_log_scores'];delta=[float(v-u) for u,v in zip(sx,sy)]
        rank_x=int(sx[1]>sx[0]);rank_y=int(sy[1]>sy[0]);threshold=rx['threshold']
        admit_x=max(sx)>=threshold;admit_y=max(sy)>=threshold
        absolute=max(map(abs,delta));relative=max(abs(v-u)/max(1.,abs(u)) for u,v in zip(sx,sy))
        max_abs=max(max_abs,absolute);max_rel=max(max_rel,relative)
        rank_diff+=int(rank_x!=rank_y);admission_diff+=int(admit_x!=admit_y)
        minimum_margin=min(minimum_margin,abs(sx[1]-sx[0]));minimum_gate=min(minimum_gate,abs(max(sx)-threshold))
        per_path.append({'path':key,'bit_log_score_delta':delta,'same_winning_bit':rank_x==rank_y,'same_gate_decision':admit_x==admit_y})
    return dict(result,status='completed',common_paths=len(keys),reference_paths=len(a),candidate_paths=len(b),
        maximum_absolute_log_score_difference=max_abs,maximum_relative_log_score_difference=max_rel,
        winning_bit_disagreements=rank_diff,admission_disagreements=admission_diff,
        reference_minimum_bit_score_margin=minimum_margin,reference_minimum_distance_to_gate=minimum_gate,per_path=per_path)


def analyze(plan_path,directory,runner_path=None):
    plan=json.loads(plan_path.read_text())
    runner={}
    if runner_path is not None:
        runner={r['name']:r for r in json.loads(runner_path.read_text())['results']}
    records=[];reports={}
    for job in plan['jobs']:
        record,report=inspect_report(job,directory,runner,plan['inputs']);records.append(record)
        if record['status']=='completed':reports[job['name']]=report
    comparisons=[]
    reference='cfo1024-cpu-fp64-reference'
    for record in records:
        if record['particles']==1024 and record['name']!=reference:
            if reference in reports and record['name'] in reports:comparisons.append(compare(reports[reference],reports[record['name']],reference,record['name']))
            else:comparisons.append({'reference':reference,'candidate':record['name'],'status':'unavailable','error':'comparison requires both completed reports'})
    a,b='cfo8192-cpu-fp32-8paths','cfo8192-gpu-fp32-32paths'
    comparisons.append(compare(reports[a],reports[b],a,b) if a in reports and b in reports else {'reference':a,'candidate':b,'status':'unavailable','error':'comparison requires both completed reports'})
    groups={}
    for row in records:
        key=f"N{row['particles']}-{row['device']}-{row['precision']}-{row['path_count']}paths"
        group=groups.setdefault(key,{'planned_repeats':0,'completed_repeats':0,'times':{}});group['planned_repeats']+=1
        if row['status']!='completed':continue
        group['completed_repeats']+=1
        for name,value in row['timing'].items():group['times'].setdefault(name,[]).append(value)
    for group in groups.values():
        group['timing_summary']={name:{'samples':values,'median':statistics.median(values),'minimum':min(values),'maximum':max(values)} for name,values in group.pop('times').items()}
    cpu=groups.get('N1024-cpu-fp32-8paths',{});gpu=groups.get('N1024-cuda:0-fp32-8paths',{})
    speedup={}
    if cpu.get('completed_repeats')==cpu.get('planned_repeats')==3 and gpu.get('completed_repeats')==gpu.get('planned_repeats')==3:
        for name,ct in cpu['timing_summary'].items():
            gt=gpu['timing_summary'].get(name)
            if gt and gt['median']>0:speedup[name]=ct['median']/gt['median']
    return {'schema':'triad-minus60-benchmark-analysis-v1','analysis_source_sha256':sha256(__file__),'plan_sha256':sha256(plan_path),
        'jobs':records,'timing_groups':groups,'precision_and_device_comparisons':comparisons,'cpu_over_gpu_median_time_ratio_1024_same8paths':speedup,
        'all_planned_jobs_completed':all(r['status']=='completed' for r in records),
        'all_available_comparisons_same_rank_and_gate':all(c['status']=='completed' and c['winning_bit_disagreements']==0 and c['admission_disagreements']==0 for c in comparisons),
        'limitations':['Timings are actual same-host workload measurements; no end-user hardware throughput qualification.',
            'Tool wall starts after CUDA initialization; bounded-run elapsed includes process startup when receipts are supplied.',
            'All three1024 timing repeats retained; no fastest-run selection.8192 GPU32 versus CPU8 are unequal workloads, so no runtime speedup is computed.',
            'CPUFP64 strict reference exists only at1024;8192 comparison is CPU/GPUFP32 on common8paths only.',
            'These reused holdout observations qualify arithmetic and compute only, not a new receiver/channel model or additional independent receptions.',
            'Missing reports/timeouts/failures remain explicit and never count as successful timing or parity measurements.']}


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reports',type=Path,required=True)
    p.add_argument('--plan',type=Path,default=Path(__file__).with_name('reproduction')/'benchmark_plan.json')
    p.add_argument('--runner-results',type=Path);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args(argv)
    if args.output.exists():p.error('output must be fresh')
    result=analyze(args.plan,args.reports,args.runner_results)
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    print(json.dumps({'output':str(args.output),'completed':sum(r['status']=='completed' for r in result['jobs']),'planned':len(result['jobs'])}))
    return 0


if __name__=='__main__':raise SystemExit(main())
