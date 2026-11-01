#!/usr/bin/env python3
"""Summarize the six retained whole-scan GPU timing records (no reruns)."""
from __future__ import annotations
import json
import argparse
import math
import pathlib
import statistics

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

HERE=pathlib.Path(__file__).resolve().parent
REPO=HERE.parents[3]
EVIDENCE=REPO/'local-evidence/triad-fp4-20261101'
OUT=HERE/'analysis'
PRICES={'5090-final':1.197,'pro-final':2.497}
POLICIES=('fp32','native','fp64')
STAGES=('validation_seconds','copy_enqueue_seconds','graph_and_sync_wall_seconds',
        'graph_device_seconds','score_transfer_seconds','complete_replay_seconds',
        'candidate_replay_seconds','finish_seconds','complete_receiver_compute_seconds')


def median(values):
    xs=[float(x) for x in values if x is not None]
    return statistics.median(xs) if xs else None


def find_result(evidence_root, device, policy):
    layouts=('5090-final','gpu-5090') if device=='5090-final' else ('pro-final','gpu-pro')
    for layout in layouts:
        path=evidence_root/layout/'results'/f'whole-scan-{policy}.json'
        if path.is_file(): return path
    raise FileNotFoundError(f'cannot find {device}/results/whole-scan-{policy}.json under {evidence_root}')


def find_input_manifest(evidence_root):
    candidates=(
        evidence_root/'inputs/boundary-cn-10-r0/manifest.json',
        evidence_root/'development-inputs/inputs/boundary-cn-10-r0/manifest.json',
        evidence_root/'development-inputs/inputs/manifest.json',
        evidence_root/'development-inputs/inputs/manifest')
    for path in candidates:
        if path.is_file(): return path
        if path.is_dir():
            for nested in (path/'boundary-cn-10-r0/manifest.json',path/'manifest.json'):
                if nested.is_file(): return nested
    raise FileNotFoundError(f'cannot find boundary-cn-10-r0 manifest under {evidence_root}')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence-root',type=pathlib.Path,default=EVIDENCE,
                        help='current evidence root or extracted archive root')
    parser.add_argument('--output',type=pathlib.Path,default=OUT,
                        help='output directory for workspace-summary.json and workspace-timings.png')
    args=parser.parse_args()
    evidence_root=args.evidence_root.resolve()
    output=args.output.resolve()
    output.mkdir(parents=True,exist_ok=True)
    reports=[]
    for device in ('5090-final','pro-final'):
        for policy in POLICIES:
            path=find_result(evidence_root,device,policy)
            report=json.loads(path.read_text())
            reports.append((device,policy,path,report))
    if len(reports)!=6: raise ValueError(f'expected six records, got {len(reports)}')
    reference_hashes=reports[0][3]['input_hashes']
    if any(r['input_hashes']!=reference_hashes for _,_,_,r in reports):
        raise ValueError('whole-scan records do not share identical sampled input hashes')
    input_manifest_path=find_input_manifest(evidence_root)
    manifest=json.loads(input_manifest_path.read_text())
    summary=[]
    for device,policy,path,d in reports:
        cfg=d['configuration']; replays=d.get('timing_replays',[]); baseline=d.get('baseline_aba_timings',[])
        if len(replays)!=7 or len(baseline)!=3: raise ValueError(f'unexpected replay counts in {path}')
        if any(x.get('copies_are_independent_trials') is not False for x in replays):
            raise ValueError(f'replay rows not marked non-independent in {path}')
        if cfg.get('repeat_observations')!=1 or cfg.get('phase_states')!=512:
            raise ValueError(f'configuration differs from requested B1/P512 scope in {path}')
        third=baseline[2]
        stage_medians={k:median(x.get(k) for x in replays) for k in STAGES}
        compute=stage_medians['complete_receiver_compute_seconds']
        dev_event=stage_medians['graph_device_seconds']
        baseline_s=float(third['complete_receiver_compute_seconds'])
        price=PRICES[device]
        fixed=float(d['fixed_workspace_preparation_seconds'])
        summary.append({
            'device':device,'gpu_name':d['environment'].get('gpu_name'),
            'policy':policy,'precision':cfg.get('precision'),'price_usd_per_hour':price,
            'record':path.relative_to(evidence_root).as_posix(),'source_hashes':d.get('source_hashes'),
            'input_hashes':d.get('input_hashes'),'signature_sha256':d.get('signature_sha256'),
            'timing_replays':len(replays),'replays_are_independent_trials':False,
            'median_full_complete_receiver_compute_seconds':compute,
            'median_graph_device_event_seconds':dev_event,
            'median_stages_seconds':stage_medians,
            'stage_iqr_seconds':{k:(float(np.quantile([x[k] for x in replays],.25)),
                                    float(np.quantile([x[k] for x in replays],.75)))
                                 for k in STAGES if all(x.get(k) is not None for x in replays)},
            'matched_warm_third_baseline':{
                'aba_position':'third A after baseline A/B/A warmup',
                'complete_receiver_compute_seconds':baseline_s,
                'candidate_replay_seconds':third.get('candidate_replay_seconds'),
                'finish_seconds':third.get('finish_seconds'),
                'placement':third.get('finish',{}).get('placement'),
                'compute_only_captures_per_hour':3600/baseline_s,
                'compute_only_cost_usd_per_capture':price*baseline_s/3600},
            'graph_over_matched_warm_baseline_speedup':baseline_s/compute if compute else None,
            'input_load_hash_seconds':d.get('input_load_hash_seconds'),
            'bank_receiver_construction_seconds':d.get('bank_receiver_construction_seconds'),
            'workspace_preparation_seconds':d.get('preparation_seconds'),
            'complete_inside_main_benchmark_seconds':d.get('complete_inside_main_benchmark_seconds'),
            'compute_only_captures_per_hour':3600/compute if compute else None,
            'compute_only_cost_usd_per_capture':price*compute/3600,
            'one_capture_including_fixed_workspace_prep_seconds':fixed+compute,
            'one_capture_including_fixed_workspace_prep_cost_usd':price*(fixed+compute)/3600,
            'admission_status':d.get('admission_status'),
            'a_after_b_bit_identical':d.get('a_after_b_bit_identical'),
            'all_baseline_comparisons_within_2e_5':d.get('all_baseline_comparisons_within_2e_5')})
    input_sample_count=int(manifest['sample_count'])
    block_samples=int(reports[0][3]['configuration']['block_samples'])
    block_count=math.ceil(float(manifest['actual_elapsed_seconds'])/
                          (block_samples/float(manifest['sample_rate'])))
    scope={
        'comparison':'Matched warm third baseline A versus median seven replay graph workspace timing',
        'physical_input':'one fixed sampled input boundary-cn-10-r0; identical manifest/basis/observation/validity hashes across all six records',
        'batch':'B1; one observation; repeated graph replays are timing measurements, not independent trials',
        'bank':'fixed flat bank; four bit/epoch candidates; three CFO centers × three lags = 36 external hypotheses',
        'physical_blocks':block_count,'sample_count':input_sample_count,
        'phase_frequency_quadrature':'P512/F43/Q3',
        'receiver':'full graph-fp64 inference with FP32 bulk and FP64 reduction; CPU Torch FP64 final decision',
        'precision_scope':'correlation precision varies by policy (FP32, native NVFP4, FP64); native FP4 remains uncalibrated experimental',
        'exclusions':'fixed key/epoch/model only; no sensitivity or false-admission qualification; exporter/AES/template generation and process startup excluded; fixed workspace prep is reported separately'}
    data={'format':'triad-whole-scan-workspace-analysis-v1','source_records':[p.relative_to(evidence_root).as_posix() for _,_,p,_ in reports],
          'scope':scope,'rates_usd_per_hour':PRICES,'matched_results':summary,
          'evidence_records_relative_to_root':True,
          'interpretation':'Compute-only cost uses median complete receiver compute time. One-capture cost additionally charges fixed workspace preparation once, but still excludes input loading/hash, process startup and exporter/AES/template generation. Seven replay timings are not reception trials.'}
    (output/'workspace-summary.json').write_text(json.dumps(data,indent=2)+'\n')
    plot(data,output/'workspace-timings.png')
    print(json.dumps({'records':len(summary),'scope':scope,'evidence_root':str(evidence_root),'out':str(output)},indent=2))


def plot(data,path):
    groups=('5090-final','pro-final')
    titles={'5090-final':'RTX 5090 — $1.197/hour','pro-final':'RTX PRO 6000 Blackwell — $2.497/hour'}
    colors={'matched baseline':'#666666','workspace graph':'#2878B5'}
    fig,axes=plt.subplots(1,2,figsize=(12,5),sharey=True)
    for ax,device in zip(axes,groups):
        rows=[r for r in data['matched_results'] if r['device']==device]
        x=np.arange(len(POLICIES)); width=.35
        base=[r['matched_warm_third_baseline']['complete_receiver_compute_seconds']*1000 for r in rows]
        graph=[r['median_full_complete_receiver_compute_seconds']*1000 for r in rows]
        q1=[r['stage_iqr_seconds']['complete_receiver_compute_seconds'][0]*1000 for r in rows]
        q3=[r['stage_iqr_seconds']['complete_receiver_compute_seconds'][1]*1000 for r in rows]
        graph_err=np.array([[g-l for g,l in zip(graph,q1)],[h-g for h,g in zip(q3,graph)]])
        ax.bar(x-width/2,base,width,label='Matched warm third baseline A',color=colors['matched baseline'])
        ax.bar(x+width/2,graph,width,label='Workspace graph median (7 replays)',color=colors['workspace graph'],yerr=graph_err,capsize=3)
        for j,r in enumerate(rows):
            ax.text(j+width/2,graph[j],f"{r['graph_over_matched_warm_baseline_speedup']:.2f}×",ha='center',va='bottom',fontsize=8)
        ax.set_xticks(x, [r['policy'] for r in rows]); ax.set_title(titles[device]);ax.grid(axis='y',alpha=.25)
    axes[0].set_ylabel('Full receiver compute time (ms)')
    axes[1].legend(loc='upper right',fontsize=8)
    fig.suptitle('Matched B1 whole-scan timings — one fixed bank/capture; replay timing, not trials',fontsize=12)
    fig.tight_layout(rect=(0,0,1,.94));fig.savefig(path,dpi=170);plt.close(fig)


if __name__=='__main__': main()
