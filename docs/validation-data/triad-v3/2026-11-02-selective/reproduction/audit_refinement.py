#!/usr/bin/env python3
"""Audit all eight planned 4096s/8192s actual-observation refinement pairs.

The entire sampled capture must agree. No unmatched tails or failed pairs are
discarded, and agreement of retained statistics is not proof of raw I/Q identity.
This is an adaptive diagnostic, not an independent reliability qualification.
"""
from __future__ import annotations

import argparse
import importlib.util
import math
from pathlib import Path
import time

import numpy as np

HERE=Path(__file__).resolve().parent
HELPER_PATH=HERE.parents[1]/'2026-11-02-spread/reproduction/audit_compression.py'
SPEC=importlib.util.spec_from_file_location('accepted_spread_compression_audit',HELPER_PATH)
if SPEC is None or SPEC.loader is None:raise RuntimeError('cannot load accepted compression audit helpers')
BASE=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(BASE)
REMOTE_ROOT=Path('/workspace/triad/local-evidence/selective')
TRUTH_FIELDS=('selected_truth_arrival_seconds_simulation_only','selected_truth_frequency_hz_simulation_only',
    'selected_truth_clock_ppm_simulation_only','selected_truth_frequency_drift_hz_per_second_simulation_only')


def resolve(root,value):
    source=Path(value)
    BASE.require(source.is_absolute() and '..' not in source.parts,'planned input is not canonical absolute')
    path=(root/source.relative_to(REMOTE_ROOT)).resolve()
    BASE.require(path.is_relative_to(root),'planned input escapes selective evidence root')
    return path


def planned_pairs(root,plan):
    BASE.require(plan['schema']=='triad-selective-stable850-block-refinement-plan-v1','unexpected refinement plan schema')
    BASE.require(plan['geometry']['block_seconds']==4096 and plan['geometry']['block_samples']==1024,
        'plan is not the declared 4096-second refinement')
    BASE.require(plan['sampled_export_count']==8,'plan does not declare eight sampled refinements')
    result=[];seen_parent=set();seen_refinement=set();seen_modes=set()
    for group in plan['paired_parent_inputs']:
        mode=group['mode'];BASE.require(mode in ('off','each-65') and mode not in seen_modes,'duplicate or unknown paired mode')
        seen_modes.add(mode)
        BASE.require(len(group['parent'])==len(group['refinement'])==4,'mode must pair four physical paths')
        for parent,refinement in zip(group['parent'],group['refinement']):
            pp=resolve(root,parent);rp=resolve(root,refinement)
            pr=pp.relative_to(root);rr=rp.relative_to(root)
            BASE.require(pr.parts[0]=='long' and rr.parts[0]=='long-refinement' and pr.parts[1:]==rr.parts[1:],
                'parent/refinement path addresses do not match')
            BASE.require(pp not in seen_parent and rp not in seen_refinement,'duplicate paired physical path')
            seen_parent.add(pp);seen_refinement.add(rp);result.append((mode,rp,pp))
    BASE.require(seen_modes=={'off','each-65'} and len(result)==8,'incomplete paired mode/path matrix')
    scored=[]
    for job in plan['jobs']:
        argv=job['argv'];BASE.require(Path(argv[1]).name=='triad-conditional-sweep.py' and argv.count('--output')==1,
            'unexpected refinement scoring job')
        scored.extend(resolve(root,p) for p in argv[2:argv.index('--output')])
    BASE.require(len(scored)==len(set(scored))==8 and set(scored)==seen_refinement,
        'scoring jobs do not cover exactly the declared refinement paths')
    return result


def required_identity(manifest):
    BASE.require(type(manifest.get('signal_bit')) is int and manifest['signal_bit'] in (0,1)
        and manifest.get('control')=='signal','missing actual source-bit/control identity')
    BASE.require(manifest.get('source_format',{}).get('encrypted') is True,'encrypted source format is not established')
    for field in TRUTH_FIELDS:
        value=manifest.get(field)
        BASE.require(type(value) in (int,float) and math.isfinite(value),f'missing physical truth: {field}')
    truth=manifest.get('path_truth_simulation_only',{})
    BASE.require(isinstance(truth,dict) and isinstance(truth.get('selective_perturbation'),dict)
        and type(truth['selective_perturbation'].get('enabled')) is bool,'missing selective/DAM source truth')


def audit_pair(refinement,parent,expected_refinement_seconds=None):
    started=time.perf_counter()
    fm,fi,fh=BASE.workspace(refinement);cm,ci,ch=BASE.workspace(parent)
    required_identity(fm);required_identity(cm)
    BASE.require(cm['block_samples']==2*fm['block_samples'],'actual block refinement is not twofold')
    BASE.require(fm['dimension']==cm['dimension'],'actual design dimensions differ')
    BASE.require(type(fm['sample_rate']) in (int,float) and math.isfinite(fm['sample_rate'])
        and fm['sample_rate']>0 and fm['sample_rate']==cm['sample_rate'],'sample rates differ or are invalid')
    if expected_refinement_seconds is not None:
        BASE.require(fm['block_samples']/fm['sample_rate']==expected_refinement_seconds
            and cm['block_samples']/cm['sample_rate']==2*expected_refinement_seconds,'actual block durations differ from plan')
    fo=BASE.options(fm['command_argv']);co=BASE.options(cm['command_argv'])
    BASE.require(len(fo.get('--block-seconds',[]))==len(co.get('--block-seconds',[]))==1
        and float(fo['--block-seconds'][0])==fm['block_samples']/fm['sample_rate']
        and float(co['--block-seconds'][0])==cm['block_samples']/cm['sample_rate'],
        'command block durations disagree with actual metadata')
    command_parity=(fm['command_argv'][0]==cm['command_argv'][0] and
        {k:v for k,v in fo.items() if k not in ('--block-seconds','--output')}==
        {k:v for k,v in co.items() if k not in ('--block-seconds','--output')})
    BASE.require(command_parity,'actual commands differ outside block duration/output')
    md=BASE.differences(BASE.metadata_without_compression(fm),BASE.metadata_without_compression(cm))
    starts=np.searchsorted(fi[:,0],ci[:,0]);ends=np.searchsorted(fi[:,1],ci[:,1])
    aligned=(starts<len(fi))&(ends<len(fi))
    aligned&=fi[np.minimum(starts,len(fi)-1),0]==ci[:,0]
    aligned&=fi[np.minimum(ends,len(fi)-1),1]==ci[:,1]
    full=(fm['sample_count']==cm['sample_count'] and bool(np.all(aligned))
        and int(starts[0])==0 and int(ends[-1])==len(fi)-1)
    checks={}
    for name,(dtype,axes) in BASE.ARRAYS.items():
        fa,fp=BASE.load_array(refinement,fm,name,(fm['block_count'],*(fm[k] for k in axes)),dtype)
        ca,cp=BASE.load_array(parent,cm,name,(cm['block_count'],*(cm[k] for k in axes)),dtype)
        fh[fm[name+'_file']]=fp;ch[cm[name+'_file']]=cp
        if name=='valid_rows':
            BASE.require(np.all(fa<=fi[:,1]-fi[:,0]) and np.all(ca<=ci[:,1]-ci[:,0]),'valid rows exceed interval lengths')
        checks[name]=(BASE.compare_sums(fa,ca,starts,ends,name=='valid_rows') if full else
            {'passed':False,'state':'not_compared','reason':'complete capture calendars differ; no prefix or tail is discarded'})
        del fa,ca
    return {'status':'completed','passed':not md and full and all(v['passed'] for v in checks.values()),
        'refinement_input':str(refinement),'parent_input':str(parent),'complete_capture_coverage':full,
        'metadata_parity':not md,'unexpected_metadata_differences':md,'command_parity_except_block_and_output':command_parity,
        'refinement_samples':fm['sample_count'],'parent_samples':cm['sample_count'],
        'refinement_block_samples':fm['block_samples'],'parent_block_samples':cm['block_samples'],
        'refinement_block_seconds':fm['block_samples']/fm['sample_rate'],'parent_block_seconds':cm['block_samples']/cm['sample_rate'],
        'refinement_last_interval_rows':int(fi[-1,1]-fi[-1,0]),'parent_last_interval_rows':int(ci[-1,1]-ci[-1,0]),
        'last_parent_refinement_blocks':int(ends[-1]-starts[-1]+1) if full else None,
        'statistics':checks,'tail_policy':'All capture rows are required; a short final block uses its actual interval, without padding or trimming.',
        'source_identity':{k:fm[k] for k in ('key_seed','epoch','signal_bit','control','random_seed_roots')},
        'selective_truth_simulation_only':fm['path_truth_simulation_only']['selective_perturbation'],
        'file_provenance':{'refinement':fh,'parent':ch},'wall_seconds':time.perf_counter()-started}


def audit(root,plan_path):
    root=Path(root).resolve();plan_path=Path(plan_path);plan=BASE.read_json(plan_path)
    started=time.perf_counter();records=[]
    for mode,refinement,parent in planned_pairs(root,plan):
        try:record=audit_pair(refinement,parent,4096)
        except Exception as error:record={'status':'error','passed':False,'refinement_input':str(refinement),
            'parent_input':str(parent),'error_type':type(error).__name__,'error':str(error)}
        record['mode']=mode;records.append(record)
    return {'schema':'triad-selective-long-refinement-audit-v1','source_sha256':BASE.sha256(__file__),
        'helper_sha256':BASE.sha256(HELPER_PATH),'plan_sha256':BASE.sha256(plan_path),'passed':all(r['passed'] for r in records),
        'pairs_expected':len(records),'pairs_passed':sum(r['passed'] for r in records),'pairs':records,
        'modes':{mode:{'pairs':sum(r['mode']==mode for r in records),'passed':sum(r['mode']==mode and r['passed'] for r in records)}
            for mode in ('off','each-65')},'wall_seconds':time.perf_counter()-started,
        'scope':'Complete sampled-capture sufficient-statistics agreement under twofold blocking; source/noise/oscillator/DAM metadata and hashes retained. Adaptive eight-path diagnostic, not raw-I/Q identity proof or independent reliability qualification.'}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True);parser.add_argument('--plan',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    try:result=audit(args.root,args.plan)
    except Exception as error:result={'schema':'triad-selective-long-refinement-audit-v1','passed':False,
        'source_sha256':BASE.sha256(__file__),'error_type':type(error).__name__,'error':str(error)}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    import json
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    print(json.dumps({k:result[k] for k in ('passed','pairs_expected','pairs_passed','modes','wall_seconds','error') if k in result}))
    return int(not result['passed'])


if __name__=='__main__':raise SystemExit(main())
