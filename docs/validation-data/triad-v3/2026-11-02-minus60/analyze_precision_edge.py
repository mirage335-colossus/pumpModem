#!/usr/bin/env python3
"""Recount fixed-grid CPUFP64/GPUFP32 diagnostics on reused marginal captures.

Selection is explicitly posthoc. Report observed score distances from the gate,
never infer a universal rounding bound or treat reused paths as fresh holdout.
"""
from __future__ import annotations
import argparse
import importlib.util
import json
import math
from pathlib import Path

_SPEC=importlib.util.spec_from_file_location('precision_benchmark_support',Path(__file__).with_name('analyze_benchmark.py'))
benchmark=importlib.util.module_from_spec(_SPEC);_SPEC.loader.exec_module(benchmark)
METHOD=benchmark.METHOD


def inspect(job,group,directory,runner):
    metadata=job['diagnostic'];path=directory/metadata['report']
    row={'name':job['name'],**metadata,'report_present':path.exists()}
    if job['name'] in runner:
        receipt=runner[job['name']];row['runner_returncode']=receipt.get('returncode');row['runner_elapsed_seconds']=receipt.get('elapsed_seconds')
    if not path.exists():return dict(row,status='missing',error='report missing'),None
    try:
        report=json.loads(path.read_text());config=report['configuration']
        if report.get('schema')!='triad-cfo-batch-study-v1':raise ValueError('unexpected report schema')
        for key in ('device','precision','particles'):
            if config.get(key)!=metadata[key]:raise ValueError('planned '+key+' differs')
        expected_config={'cn0':-60,'reference_variance':None,'coherence':'infinity','frequency_bound':1e-6,
            'phase_diffusion':0.,'frequency_random_walk':0.,'resampling':'none','frequency_initialization':'midpoint-grid',
            'selection_methods':16,'duration_alternatives':8}
        for key,value in expected_config.items():
            if config.get(key)!=value:raise ValueError('fixed configuration differs: '+key)
        captures=report['captures']
        expected={f"{group['cohort']}/path-{i:05d}" for i in group['input_indices']}
        actual=[benchmark.path_key(c) for c in captures]
        if len(actual)!=len(expected) or set(actual)!=expected:raise ValueError('actual cohort/path set differs from plan')
        if report['failed_physical_capture_records'] or any(c.get('status')!='completed' for c in captures):raise ValueError('one or more capture records failed')
        if row.get('runner_returncode',0)!=0:raise ValueError('bounded runner did not complete successfully')
        for capture in captures:
            if capture['control']!='signal':raise ValueError('expected actual signal control')
            if set(capture['input_hashes'])!=benchmark.REQUIRED_HASHES:raise ValueError('missing actual input hashes')
            method=capture['methods'][METHOD]
            if len(method['bit_log_scores'])!=2:raise ValueError('expected two bit scores')
            [benchmark.finite(v,'bit score') for v in method['bit_log_scores']]
            benchmark.finite(method['threshold'],'threshold')
        row.update(status='completed',report_sha256=benchmark.sha256(path),capture_count=len(captures),
            correct_accepted=sum(c['methods'][METHOD]['counts'].get('correct_accepted',0) for c in captures),
            wrong_accepted=sum(c['methods'][METHOD]['counts'].get('wrong_accepted',0) for c in captures),
            missed=sum(c['methods'][METHOD]['counts'].get('missed',0) for c in captures),
            tool_wall_seconds=benchmark.finite(report['wall_seconds_joint'],'wall time'))
        return row,report
    except (KeyError,TypeError,ValueError,OSError) as error:
        return dict(row,status='invalid',error=str(error)),None


def analyze(plan_path,directory,runner_path=None):
    plan=json.loads(plan_path.read_text());runner={}
    if runner_path is not None:runner={r['name']:r for r in json.loads(runner_path.read_text())['results']}
    group_lookup={g['name']:g for g in plan['groups']};records=[];reports={}
    for job in plan['jobs']:
        record,report=inspect(job,group_lookup[job['diagnostic']['group']],directory,runner);records.append(record)
        if record['status']=='completed':reports[job['name']]=report
    groups=[]
    for declared in plan['groups']:
        ref=declared['cpu_fp64_job'];candidate=declared['gpu_fp32_job']
        row={'name':declared['name'],'selection':declared['selection'],'path_indices':declared['input_indices']}
        if ref not in reports or candidate not in reports:
            groups.append(dict(row,status='unavailable',error='both completed arithmetic reports required'));continue
        comparison=benchmark.compare(reports[ref],reports[candidate],ref,candidate)
        row['comparison']=comparison;row['status']=comparison['status']
        if comparison['status']!='completed':groups.append(row);continue
        cpu={benchmark.path_key(c):c for c in reports[ref]['captures']};gpu={benchmark.path_key(c):c for c in reports[candidate]['captures']}
        distances=[]
        for key in sorted(cpu):
            a=cpu[key]['methods'][METHOD];b=gpu[key]['methods'][METHOD]
            score=a['bit_log_scores'];other=b['bit_log_scores'];threshold=a['threshold']
            distances.append({'path':key,'cpu_fp64_bit_log_scores':score,'gpu_fp32_bit_log_scores':other,'threshold':threshold,
                'cpu_signed_winning_score_minus_gate':max(score)-threshold,'gpu_signed_winning_score_minus_gate':max(other)-threshold,
                'cpu_bit_score_margin':abs(score[1]-score[0]),
                'maximum_absolute_score_error':max(abs(x-y) for x,y in zip(score,other))})
        nearest=min(distances,key=lambda r:abs(r['cpu_signed_winning_score_minus_gate']))
        observed_error=comparison['maximum_absolute_log_score_difference'];distance=abs(nearest['cpu_signed_winning_score_minus_gate'])
        row.update(per_path_distances=distances,closest_reference_gate_case=nearest,
            observed_gate_distance_over_maximum_score_error=None if observed_error==0 else distance/observed_error,
            proximity_scope='Exact observed distance for these reused captures. Observed arithmetic differences are not a worst-case numerical bound and do not qualify arbitrarily closer scores.')
        groups.append(row)
    return {'schema':'triad-minus60-precision-edge-analysis-v1','analysis_source_sha256':benchmark.sha256(__file__),
        'benchmark_support_source_sha256':benchmark.sha256(Path(__file__).with_name('analyze_benchmark.py')),
        'plan_sha256':benchmark.sha256(plan_path),'jobs':records,'groups':groups,
        'all_planned_jobs_completed':all(r['status']=='completed' for r in records),
        'all_pairs_same_rank_and_gate':all(g['status']=='completed' and g['comparison']['winning_bit_disagreements']==0 and g['comparison']['admission_disagreements']==0 for g in groups),
        'limitations':['Posthoc diagnostic selection from existing development and holdout results; no new independent detection qualification.',
            'CPUFP64 and GPUFP32 use the same1024-point discrete CFO grid, physical observations and Gaussian channel prior. Prior FFT scores were used only to select cases.',
            'Actual nearest gate distances are reported. Agreement several log units from a gate is not a demonstration at machine-precision distance.',
            'Missing/failed pairs cannot establish arithmetic agreement; no replacement cases or modified gates are chosen.']}


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reports',type=Path,required=True)
    p.add_argument('--plan',type=Path,default=Path(__file__).with_name('reproduction')/'precision_edge_plan.json')
    p.add_argument('--runner-results',type=Path);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args(argv)
    if args.output.exists():p.error('output must be fresh')
    result=analyze(args.plan,args.reports,args.runner_results);args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    print(json.dumps({'output':str(args.output),'complete':result['all_planned_jobs_completed'],'same_rank_and_gate':result['all_pairs_same_rank_and_gate']}))
    return 0


if __name__=='__main__':raise SystemExit(main())
