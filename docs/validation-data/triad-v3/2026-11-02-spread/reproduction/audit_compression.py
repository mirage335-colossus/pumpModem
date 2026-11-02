#!/usr/bin/env python3
"""Audit planned actual-noise fine/coarse pairs; never repair or trim evidence.

Only NumPy is required. Summed statistics establish agreement of the retained
observations, not bitwise identity of unavailable raw I/Q or new independent
paths. Actual decoder outcomes are diagnostic (four paths per configuration).
"""
import argparse
from collections import Counter
import copy
import hashlib
import json
from pathlib import Path
import time

import numpy as np

REMOTE_ROOT=Path('/workspace/triad/local-evidence/spread')
HERE=Path(__file__).resolve().parent
ARRAYS={'gram':('<c16',('dimension','dimension')),'mean':('<c16',('dimension',)),
        'observation':('<c16',('dimension',)),'signal_energy':('<f8',()),
        'received_energy':('<f8',()),'valid_rows':('<u8',())}
ATOL=2e-10
RTOL=2e-11


def require(ok,message):
    if not ok:raise ValueError(message)


def read_json(path):
    def reject(value):raise ValueError(f'nonfinite JSON: {path}: {value}')
    return json.loads(Path(path).read_text(),parse_constant=reject)


def sha256(path):
    digest=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):digest.update(chunk)
    return digest.hexdigest()


def options(argv):
    result={};current=None
    for value in argv[1:]:
        if value.startswith('--'):
            require(value not in result,'duplicate command option')
            result[value]=[];current=value
        else:
            require(current is not None,'unexpected positional export argument')
            result[current].append(value)
    return result


def under_root(root,value):
    relative=Path(value).relative_to(REMOTE_ROOT)
    path=(Path(root)/relative).resolve()
    require(path.is_relative_to(Path(root).resolve()),'planned path escapes root')
    return path


def planned_pairs(plan,root):
    jobs={job['name']:job for job in plan['jobs']}
    require(len(jobs)==len(plan['jobs']),'duplicate planned job name')
    pairs=[];used=set()
    for name,coarse in jobs.items():
        if '-paired-coarse' not in name:continue
        fine_name=name.replace('-paired-coarse','')
        require(fine_name in jobs,f'missing fine plan for {name}')
        fine=jobs[fine_name];fo=options(fine['argv']);co=options(coarse['argv'])
        fbase={k:v for k,v in fo.items() if k not in ('--block-seconds','--output')}
        cbase={k:v for k,v in co.items() if k not in ('--block-seconds','--output')}
        require(fine['argv'][0]==coarse['argv'][0] and fbase==cbase,f'noncompression plan difference: {name}')
        require(float(co['--block-seconds'][0])==4*float(fo['--block-seconds'][0]),'planned compression is not fourfold')
        require(fine.get('simulation_only_expected')==coarse.get('simulation_only_expected'),'planned simulation identities differ')
        pairs.append((fine,coarse,under_root(root,fo['--output'][0]),under_root(root,co['--output'][0])))
        used.update((name,fine_name))
    require(pairs and used==set(jobs),'unpaired planned export jobs')
    return pairs


def load_array(directory,manifest,name,shape,dtype):
    filename=manifest[name+'_file']
    require(Path(filename).name==filename,'nonlocal array filename')
    require(manifest[name+'_shape']==list(shape),f'{name} shape mismatch')
    path=directory/filename;size=int(np.prod(shape,dtype=object))*np.dtype(dtype).itemsize
    require(path.stat().st_size==size,f'{filename} actual byte count mismatch')
    require(manifest['file_sizes'][filename]==size,f'{filename} declared byte count mismatch')
    return np.memmap(path,mode='r',dtype=dtype,shape=shape),{'sha256':sha256(path),'bytes':size}


def workspace(directory):
    m=read_json(directory/'manifest.json')
    require(m['format']=='triad-block-stats-v1' and m['byte_order']=='little'
        and m['complex_type']=='complex128' and m['real_type']=='float64'
        and m['integer_type']=='uint64' and m['layout']=='block,row,column','unsupported workspace representation')
    for key in ('block_count','block_samples','dimension','sample_count'):
        require(type(m[key]) is int and m[key]>0,f'invalid {key}')
    require(m['stats_noisy'] is True,'actual sampled noise is required')
    require(type(m.get('key_seed')) is int and type(m.get('epoch')) is int,'source key/epoch identity is missing')
    roots=m.get('random_seed_roots',{})
    require(all(type(roots.get(k)) is int for k in ('channel','oscillator','noise','frontend','path')),'independent physical/noise seed roots are missing')
    require(m['dimension']<=64,'dimension exceeds exported cap')
    require(m['block_count']==(m['sample_count']+m['block_samples']-1)//m['block_samples'],'block/sample count mismatch')
    intervals,provenance=load_array(directory,m,'intervals',(m['block_count'],2),'<u8')
    require(intervals[0,0]==0 and intervals[-1,1]==m['sample_count'],'capture interval endpoints mismatch')
    require(np.all(intervals[:,1]>intervals[:,0]) and np.all(intervals[1:,0]==intervals[:-1,1]),'noncontiguous intervals')
    lengths=intervals[:,1]-intervals[:,0]
    require(np.all(lengths[:-1]==m['block_samples']) and lengths[-1]<=m['block_samples'],'interval/block size mismatch')
    return m,intervals,{'manifest.json':{'sha256':sha256(directory/'manifest.json')},m['intervals_file']:provenance}


def metadata_without_compression(m):
    result=copy.deepcopy(m)
    for key in ('block_count','block_samples','file_sizes','command_argv'):
        result.pop(key,None)
    for name in (*ARRAYS,'intervals'):result.pop(name+'_shape',None)
    search=result.get('resolved_configuration',{}).get('search_estimates',{})
    search.pop('block_frequency_maximum_step_hz',None)
    return result


def differences(left,right,prefix=''):
    if isinstance(left,dict) and isinstance(right,dict):
        found=[]
        for key in sorted(set(left)|set(right)):
            path=prefix+'.'+key if prefix else key
            if key not in left or key not in right:found.append(path)
            else:found.extend(differences(left[key],right[key],path))
        return found
    return [] if left==right else [prefix]


def compare_sums(fine,coarse,starts,ends,integer=False):
    maximum=0.;scaled=0.;squared_error=0.;squared_reference=0.;total_fine=0.;total_coarse=0.
    passed=True
    for first in range(0,len(starts),1024):
        stop=min(first+1024,len(starts));lo=int(starts[first]);hi=int(ends[stop-1])+1
        value=np.add.reduceat(fine[lo:hi],starts[first:stop]-lo,axis=0)
        reference=np.asarray(coarse[first:stop])
        require(np.isfinite(value).all() and np.isfinite(reference).all(),'nonfinite statistics')
        if integer:
            equal=value==reference;passed=passed and bool(np.all(equal))
            delta=np.abs(value.astype(np.float64)-reference.astype(np.float64))
        else:
            delta=np.abs(value-reference);tolerance=ATOL+RTOL*np.abs(reference)
            scaled=max(scaled,float(np.max(delta/tolerance)))
            passed=passed and bool(np.all(delta<=tolerance))
        maximum=max(maximum,float(np.max(delta)))
        squared_error+=float(np.sum(delta*delta));squared_reference+=float(np.sum(np.abs(reference)**2))
        if fine.ndim==1:
            total_fine+=float(np.sum(value));total_coarse+=float(np.sum(reference))
    return {'passed':passed,'coarse_blocks_compared':len(starts),'max_absolute_error':maximum,
        'max_tolerance_ratio':None if integer else scaled,
        'relative_l2_error':(squared_error/squared_reference)**.5 if squared_reference else 0.,
        'fine_aggregated_scalar_total':total_fine if fine.ndim==1 else None,
        'coarse_scalar_total':total_coarse if fine.ndim==1 else None,
        'comparison':'exact integer' if integer else f'abs(error) <= {ATOL} + {RTOL}*abs(coarse)'}


def audit_pair(fine_dir,coarse_dir,fine_job=None,coarse_job=None):
    started=time.perf_counter();fm,fi,fh=workspace(fine_dir);cm,ci,ch=workspace(coarse_dir)
    require(cm['block_samples']==4*fm['block_samples'],'actual compression is not fourfold')
    require(fm['dimension']==cm['dimension'],'actual design dimensions differ')
    require(float(fm['sample_rate'])>0 and fm['sample_rate']==cm['sample_rate'],'sample rates differ')
    fo=options(fm['command_argv']);co=options(cm['command_argv'])
    require({k:v for k,v in fo.items() if k not in ('--block-seconds','--output')}==
        {k:v for k,v in co.items() if k not in ('--block-seconds','--output')},'actual commands differ outside compression')
    for manifest,job in ((fm,fine_job),(cm,coarse_job)):
        if job is not None:require(manifest['command_argv']==job['argv'],'manifest command differs from frozen export plan')
    md=differences(metadata_without_compression(fm),metadata_without_compression(cm))
    starts=np.searchsorted(fi[:,0],ci[:,0]);ends=np.searchsorted(fi[:,1],ci[:,1])
    aligned=(starts<len(fi))&(ends<len(fi))
    aligned&=fi[np.minimum(starts,len(fi)-1),0]==ci[:,0]
    aligned&=fi[np.minimum(ends,len(fi)-1),1]==ci[:,1]
    invalid=np.flatnonzero(~aligned);count=int(invalid[0]) if len(invalid) else len(ci)
    require(count>0,'no common complete interval prefix')
    starts=starts[:count];ends=ends[:count]
    full_support=fm['sample_count']==cm['sample_count'] and count==len(ci) and int(ends[-1])==len(fi)-1
    checks={}
    for name,(dtype,axes) in ARRAYS.items():
        fa,fp=load_array(fine_dir,fm,name,(fm['block_count'],*(fm[k] for k in axes)),dtype)
        ca,cp=load_array(coarse_dir,cm,name,(cm['block_count'],*(cm[k] for k in axes)),dtype)
        fh[fm[name+'_file']]=fp;ch[cm[name+'_file']]=cp
        if name=='valid_rows':
            require(np.all(fa<=fi[:,1]-fi[:,0]) and np.all(ca<=ci[:,1]-ci[:,0]),'valid rows exceed physical rows')
        checks[name]=compare_sums(fa,ca,starts,ends,name=='valid_rows')
        del fa,ca
    return {'status':'completed','passed':not md and full_support and all(v['passed'] for v in checks.values()),
        'fine_input':str(fine_dir),'coarse_input':str(coarse_dir),'metadata_parity':not md,'unexpected_metadata_differences':md,
        'full_capture_support_equal':full_support,'fine_samples':fm['sample_count'],'coarse_samples':cm['sample_count'],
        'compared_prefix_samples':int(ci[count-1,1]),'fine_uncompared_tail_samples':fm['sample_count']-int(ci[count-1,1]),
        'coarse_uncompared_tail_samples':cm['sample_count']-int(ci[count-1,1]),
        'fine_block_samples':fm['block_samples'],'coarse_block_samples':cm['block_samples'],
        'last_compared_coarse_fine_blocks':int(ends[-1]-starts[-1]+1),'last_fine_interval_rows':int(fi[-1,1]-fi[-1,0]),
        'last_coarse_interval_rows':int(ci[-1,1]-ci[-1,0]),
        'tail_interpretation':('Identical sample support; the final partial block, if any, is summed with its actual row count and no invented padding.' if full_support else
            'Capture supports differ. Only the common aligned prefix was checked; unmatched tails are explicit and no full paired-identity pass is granted.'),
        'statistics':checks,'signal_energy_seconds':{'fine':checks['signal_energy']['fine_aggregated_scalar_total']/fm['sample_rate'],
            'coarse':checks['signal_energy']['coarse_scalar_total']/cm['sample_rate']},
        'source_identity':{k:fm.get(k) for k in ('key_seed','epoch','signal_bit','control','random_seed_roots','source_samples','channel_samples','channel_tail_samples')},
        'file_provenance':{'fine':fh,'coarse':ch},'wall_seconds':time.perf_counter()-started}


def score_summary(root,job):
    cmd=job['argv'];output=under_root(root,cmd[cmd.index('--output')+1]);doc=read_json(output)
    require(doc['schema']=='triad-conditional-sweep-v1' and doc['observation_mode']=='actual-noisy','not an actual score report')
    require(doc['configuration']['noise_trials']==1,'actual score report has replicated trials')
    expected=[under_root(root,x) for x in cmd[2:cmd.index('--output')]]
    require(len(expected)==len(set(expected)),'duplicate planned score path')
    records={under_root(root,r['input']):r for r in doc['paths']}
    require(len(records)==len(doc['paths']) and set(records)==set(expected),'actual score path coverage mismatch')
    archive=output.parent/doc['scores_file'];require(sha256(archive)==doc['scores_sha256'],'score archive hash mismatch')
    total=Counter();errors=[];details=[];used=set()
    with np.load(archive,allow_pickle=False) as arrays:
        for source in expected:
            record=records[source];total['signal_attempts']+=1
            if record['status']!='completed':
                total['failed_paths']+=1;total['missed']+=1
                errors.append({'input':str(source),'error_type':record.get('error_type'),'error':record.get('error')});continue
            require(len(record['conditions'])==1,'actual path has multiple conditions')
            row=record['conditions'][0];require(row['control']=='signal' and row['noise_trials']==1,'expected one actual signal observation')
            values=row['methods'];require(set(values)=={'gaussian-window-ucb'},'unexpected receiver method')
            item=values['gaussian-window-ucb'];key=item['score_array'];require(key not in used,'reused score array');used.add(key)
            scores=np.asarray(arrays[key]);require(scores.shape==(1,2) and np.isfinite(scores).all(),'invalid actual bit scores')
            truth=record['truth_bit_simulation_only'];require(type(truth) is int and truth in (0,1),'invalid actual bit label')
            winner=int(scores[0].argmax());accepted=bool(scores[0,winner]>=item['threshold'])
            observed={'trials':1,'correct_accepted':int(accepted and winner==truth),'wrong_accepted':int(accepted and winner!=truth),
                'missed':int(not accepted),'raw_correct':int(winner==truth)}
            require(item['counts']==observed,'actual frozen counts disagree with scores')
            total.update({k:v for k,v in observed.items() if k!='trials'})
            details.append({'input':str(source),'truth_bit':truth,'winner_bit':winner,'bit_log_scores':scores[0].tolist(),
                'threshold':item['threshold'],'outcome':'correct_accepted' if observed['correct_accepted'] else 'wrong_accepted' if observed['wrong_accepted'] else 'missed'})
        require(set(arrays.files)==used,'unaccounted score arrays, including any failed-path partial scores')
    return {'status':'completed','passed':not errors,'name':job['name'],'report_sha256':sha256(output),
        'window_blocks':doc['configuration']['window_blocks'],'doppler_width_hz':doc['configuration']['window_doppler_width'],
        'counts':{k:total[k] for k in ('signal_attempts','correct_accepted','wrong_accepted','missed','raw_correct','failed_paths')},
        'path_errors':errors,'paths':details,'scope':'Four actual paths per configuration are paired diagnostics; not reliability qualification. Fine/coarse evidence compresses the same observation only when the separate statistics audit passes.'}


def audit(root,export_plan=None,score_plan=None):
    root=Path(root).resolve();export_plan=Path(export_plan or HERE/'compression-actual-export-plan.json');score_plan=Path(score_plan or HERE/'compression-actual-score-plan.json')
    started=time.perf_counter();pairs=[];scores=[]
    for fine,coarse,fd,cd in planned_pairs(read_json(export_plan),root):
        try:value=audit_pair(fd,cd,fine,coarse)
        except Exception as error:value={'status':'error','passed':False,'fine_input':str(fd),'coarse_input':str(cd),'error_type':type(error).__name__,'error':str(error)}
        pairs.append(value)
    for job in read_json(score_plan)['jobs']:
        try:value=score_summary(root,job)
        except Exception as error:value={'status':'error','passed':False,'name':job['name'],'error_type':type(error).__name__,'error':str(error)}
        scores.append(value)
    paired=all(v['passed'] for v in pairs);scored=bool(scores) and all(v['passed'] for v in scores)
    return {'schema':'triad-compression-audit-v1','source_sha256':sha256(__file__),
        'plans':{p.name:sha256(p) for p in (export_plan,score_plan)},'paired_statistics_passed':paired,'actual_score_reports_complete':scored,
        'passed':paired and scored,'pairs_expected':len(pairs),'pairs_passed':sum(v['passed'] for v in pairs),
        'pairs':pairs,'actual_score_reports':scores,'wall_seconds':time.perf_counter()-started,
        'limitations':['Agreement is over exported sufficient statistics; raw sample identity is not independently proved without raw I/Q.',
            'Plans, physical metadata, key/epoch and separate noise/path/oscillator roots must match; projected-noise replicas are not substituted.',
            'Residual noise calibration rank and block-center channel approximation can change with blocking, even for the identical sampled observation.',
            'The final short block is legitimate rounding; different capture extents are not silently trimmed or called paired.',
            'No receiver improvement, daily false-admission qualification or correct-reception probability follows from four paired paths.']}


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--root',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    try:result=audit(args.root)
    except Exception as error:result={'schema':'triad-compression-audit-v1','passed':False,'error_type':type(error).__name__,'error':str(error)}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    print(json.dumps({k:result[k] for k in ('passed','pairs_expected','pairs_passed','paired_statistics_passed','actual_score_reports_complete','wall_seconds','error') if k in result}))
    return int(not result['passed'])


if __name__=='__main__':raise SystemExit(main())
