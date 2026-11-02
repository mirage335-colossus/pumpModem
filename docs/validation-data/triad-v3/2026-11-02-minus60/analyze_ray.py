#!/usr/bin/env python3
"""Recount paired ray-search development evidence; no N=8 qualification.

Usage: python analyze_ray.py --root EVIDENCE [--output analysis-ray.json]
Reads minus60-ray/{case}-{cpu,gpu}.{json,scores.npz} and
minus60-static/{case}.{json,scores.npz}. Pairing requires all six recorded
input hashes to agree, never only a basename or an ordinal. Optional
--source-root REPOSITORY also verifies reported source/dependency hashes.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re
import sys

import numpy as np

GATES={'strict':math.log(1440*8*16*2/(.5e-4)),
       'relaxed':math.log(720*8*16*2/(.5e-2))}
INPUTS=('manifest.json','gram.c128','observation.c128','received_energy.f64','intervals.u64','valid_rows.u64')
METHODS={'ray':('ray-frequency','ray-frequency-ucb'),
         'static':('static-frequency','static-frequency-ucb')}
DEFAULT_CASES=('static750','doppler_mismatch','acquisition','each1')


def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024**2),b''):h.update(block)
    return h.hexdigest()


def require(condition,message):
    if not condition:raise ValueError(message)


def read(path):
    def reject(value):raise ValueError('nonfinite JSON '+value)
    return json.loads(path.read_text(),parse_constant=reject)


def classify(scores,truth,control,gate):
    winner=int(np.argmax(scores));accepted=bool(scores[winner]>=gate)
    if control=='signal':
        require(type(truth) is int and truth in (0,1),'invalid signal evaluation truth')
        return {'captures':1,'correct_accepted':int(accepted and winner==truth),
                'wrong_accepted':int(accepted and winner!=truth),'missed':int(not accepted),
                'raw_correct':int(winner==truth)}
    require(control in ('null','wrong-key'),'unknown control')
    return {'captures':1,'false_admitted':int(accepted),'not_admitted':int(not accepted)}


def local_input(root,recorded):
    parts=Path(recorded).parts
    for anchor in ('minus60-screen','minus60-controls','minus60-holdout'):
        if anchor in parts:return root.joinpath(*parts[parts.index(anchor):])
    return None


def load_report(path,kind,root,expected,source_root,errors):
    if not path.is_file():
        errors.append({'file':str(path),'error':'missing required report'})
        return None
    try:
        report=read(path)
        schema='triad-ray-frequency-study-v1' if kind=='ray' else 'triad-static-frequency-study-v1'
        require(report.get('schema')==schema,'unexpected report schema')
        config=report['configuration'];methods=METHODS[kind]
        require(set(config['methods'])==set(methods),'unexpected method set')
        require(config['selection_methods']==16 and config['duration_alternatives']==8,'different K16/D8 family')
        require(config['calibration_alternatives']==2,'different calibration-policy budget')
        require(math.isclose(report['family']['fixed_window_log_threshold'],GATES['strict'],abs_tol=1e-12),'strict gate mismatch')
        filename=report['scores_file'];require(Path(filename).name==filename,'scores filename must be local')
        array_path=path.parent/filename
        require(array_path.is_file(),'missing required score archive: '+str(array_path))
        require(sha(array_path)==report['scores_sha256'],'score archive SHA256 mismatch')
        require(len(report['captures'])==expected,f'expected {expected} capture records')
        source_name='triad-ray-frequency-study.py' if kind=='ray' else 'triad-static-frequency-study.py'
        bindings={source_name:report['source_sha256'],**report.get('dependency_sha256',{})}
        if kind=='static':
            bindings.update({'triad_state_workspace.py':report['workspace_source_sha256'],
                             'triad_noise_likelihood.py':report['noise_likelihood_source_sha256']})
        require(all(re.fullmatch('[0-9a-f]{64}',v) for v in bindings.values()),'invalid source hash metadata')
        source_checks={}
        for name,digest in bindings.items():
            require(Path(name).name==name,'dependency must be a basename')
            source=source_root/'tools'/name if source_root else None
            if source is None:source_checks[name]='not independently checked; no source root supplied'
            else:
                require(source.is_file(),'missing bound source: '+str(source))
                require(sha(source)==digest,'source SHA256 mismatch: '+name)
                source_checks[name]='verified'
        rows=[];identities=set();expected_arrays=set();input_verification=[]
        with np.load(array_path,allow_pickle=False) as archive:
            for capture in report['captures']:
                row={'input':capture['input'],'control':capture['control'],
                     'failed':capture['status']!='completed','truth':capture.get('truth_bit_simulation_only')}
                if row['failed']:
                    row['error']=capture.get('error','capture failed');rows.append(row);continue
                hashes=capture['input_hashes']
                require(set(hashes)==set(INPUTS),'incomplete input-hash identity')
                require(all(re.fullmatch('[0-9a-f]{64}',v) for v in hashes.values()),'invalid input hash')
                identity=tuple(hashes[name] for name in INPUTS)
                require(identity not in identities,'duplicate physical input hashes within report')
                identities.add(identity);row['identity']=identity;row['scores']={};row['grid']=capture.get('frequency_grid',{})
                location=local_input(root,capture['input']);checks={}
                for name in INPUTS:
                    local=location/name if location is not None else None
                    if local is None or not local.is_file():checks[name]='unavailable locally; only cross-report identity can be checked'
                    else:
                        require(sha(local)==hashes[name],'input SHA256 mismatch: '+str(local))
                        checks[name]='verified'
                input_verification.append({'input':capture['input'],'files':checks})
                for method in methods:
                    key=f"path{capture['physical_path_index']:05d}_{capture['control']}_{method}"
                    require(key in archive,'missing score array: '+key)
                    expected_arrays.add(key);scores=np.asarray(archive[key],dtype=np.float64)
                    require(scores.shape==(1,2) and np.isfinite(scores).all(),'invalid actual score shape/values')
                    stored=capture['methods'][method]
                    require(np.array_equal(scores[0],np.asarray(stored['bit_log_scores'])),'JSON/NPZ score mismatch')
                    require(math.isclose(stored['threshold'],GATES['strict'],abs_tol=1e-12),'capture strict gate mismatch')
                    counted=classify(scores[0],row['truth'],row['control'],GATES['strict'])
                    require(counted==stored['counts'],'stored count differs from independent recount')
                    row['scores'][method]=scores[0]
                rows.append(row)
            require(set(archive.files)==expected_arrays,'unexpected/orphan score arrays')
        failed=sum(r['failed'] for r in rows)
        require(failed==report['failed_physical_capture_records'],'failed-record count mismatch')
        if failed:errors.append({'file':str(path),'error':f'{failed} capture failures preserved'})
        return {'path':str(path),'report_sha256':sha(path),'score_sha256':sha(array_path),
                'source_bindings':bindings,'source_checks':source_checks,'input_verification':input_verification,
                'config':config,'rows':rows,'wall_seconds_joint':report['wall_seconds_joint']}
    except (ValueError,KeyError,OSError,TypeError) as error:
        errors.append({'file':str(path),'error':str(error)})
        return None


def summarize(report,kind):
    if report is None:return None
    out={'report':report['path'],'report_sha256':report['report_sha256'],
         'scores_sha256':report['score_sha256'],'source_checks':report['source_checks'],
         'source_bindings':report['source_bindings'],'input_file_checks':report['input_verification'],
         'device':report['config'].get('device'),'precision':report['config'].get('precision'),
         'attempted':len(report['rows']),'failed':sum(r['failed'] for r in report['rows']),
         'wall_seconds_joint':report['wall_seconds_joint'],'methods':{}}
    for method in METHODS[kind]:
        groups={}
        for control in sorted({r['control'] for r in report['rows']}):
            rows=[r for r in report['rows'] if r['control']==control]
            counts={}
            for gate,threshold in GATES.items():
                count=Counter()
                for row in rows:
                    if not row['failed']:count.update(classify(row['scores'][method],row['truth'],control,threshold))
                failed=sum(r['failed'] for r in rows)
                if control=='signal':count.update(captures=failed,missed=failed)
                counts[gate]={'counts':dict(count),'failed_attempts':failed}
            groups[control]=counts
        out['methods'][method]=groups
    return out


def pair(left,right,left_method,right_method):
    if left is None or right is None:return {'status':'unavailable; report missing or invalid'}
    a={r['identity']:r for r in left['rows'] if not r['failed']}
    b={r['identity']:r for r in right['rows'] if not r['failed']}
    common=sorted(set(a)&set(b));details=[];maximum=0.;true_deltas=[];changes={g:Counter() for g in GATES}
    def common_grid(row):
        grid=row['grid']
        return (grid.get('nfft'),grid.get('spacing_hz'),grid.get('common_bins',grid.get('count')),
                grid.get('common_endpoints_hz',grid.get('actual_endpoints_hz')))
    for identity in common:
        x,y=a[identity],b[identity]
        require(x['truth']==y['truth'] and x['control']==y['control'],'same hashes but different evaluation metadata')
        delta=x['scores'][left_method]-y['scores'][right_method]
        maximum=max(maximum,float(np.abs(delta).max()))
        if x['control']=='signal':true_deltas.append(float(delta[x['truth']]))
        row={'input':x['input'],'left_minus_right_scores':delta.tolist(),
             'same_common_frequency_grid':common_grid(x)==common_grid(y),'decisions':{}}
        for gate,threshold in GATES.items():
            left_out=classify(x['scores'][left_method],x['truth'],x['control'],threshold)
            right_out=classify(y['scores'][right_method],y['truth'],y['control'],threshold)
            admitted_key='false_admitted' if x['control']!='signal' else None
            la=bool(left_out[admitted_key]) if admitted_key else bool(left_out['correct_accepted']+left_out['wrong_accepted'])
            ra=bool(right_out[admitted_key]) if admitted_key else bool(right_out['correct_accepted']+right_out['wrong_accepted'])
            changes[gate]['admission_mismatches']+=int(la!=ra)
            changes[gate]['winner_mismatches']+=int(np.argmax(x['scores'][left_method])!=np.argmax(y['scores'][right_method]))
            changes[gate]['complete_outcome_mismatches']+=int(left_out!=right_out)
            if x['control']=='signal':
                changes[gate]['left_correct_right_not']+=int(left_out['correct_accepted'] and not right_out['correct_accepted'])
                changes[gate]['right_correct_left_not']+=int(right_out['correct_accepted'] and not left_out['correct_accepted'])
            row['decisions'][gate]={'left':left_out,'right':right_out}
        details.append(row)
    return {'status':'paired' if common else 'no identical input hashes',
            'paired_paths':len(common),'left_unpaired_completed':len(set(a)-set(b)),
            'right_unpaired_completed':len(set(b)-set(a)),
            'max_absolute_bit_log_score_difference':maximum if common else None,
            'true_bit_score_left_minus_right':{'minimum':min(true_deltas),'median':float(np.median(true_deltas)),
                'maximum':max(true_deltas),'mean':float(np.mean(true_deltas))} if true_deltas else None,
            'same_common_frequency_grid_all_pairs':all(r['same_common_frequency_grid'] for r in details) if common else None,
            'gates':{g:dict(v) for g,v in changes.items()},'paths':details}


def analyze(root,cases,expected,source_root=None):
    errors=[];result=[]
    for case in cases:
        cpu=load_report(root/'minus60-ray'/(case+'-cpu.json'),'ray',root,expected,source_root,errors)
        gpu=load_report(root/'minus60-ray'/(case+'-gpu.json'),'ray',root,expected,source_root,errors)
        static=load_report(root/'minus60-static'/(case+'.json'),'static',root,expected,source_root,errors)
        row={'case':case,'ray_cpu':summarize(cpu,'ray'),'ray_gpu':summarize(gpu,'ray'),
             'static':summarize(static,'static'),'cpu_gpu':{},'ray_minus_static':{}}
        for left,label in ((cpu,'cpu'),(gpu,'gpu')):
            if left:
                device=left['config'].get('device','')
                if (label=='cpu' and device!='cpu') or (label=='gpu' and not device.startswith('cuda')):
                    errors.append({'file':left['path'],'error':'filename CPU/GPU designation disagrees with device'})
        for method,reference in zip(METHODS['ray'],METHODS['static']):
            try:
                row['cpu_gpu'][method]=pair(cpu,gpu,method,method)
                parity=row['cpu_gpu'][method]
                if parity.get('status')=='paired' and (parity['left_unpaired_completed'] or parity['right_unpaired_completed']
                                                       or not parity['same_common_frequency_grid_all_pairs']):
                    errors.append({'case':case,'method':method,'error':'CPU/GPU inputs or actual common grids differ'})
                row['ray_minus_static'][method]={label:pair(report,static,method,reference)
                    for label,report in (('cpu',cpu),('gpu',gpu))}
            except ValueError as error:errors.append({'case':case,'error':str(error)})
        if cpu and gpu:
            keys=('cn0','reference_variance','frequency_bound','ray_frequency_bound','frequency_step','zero_ray_mass')
            row['cpu_gpu_same_likelihood_configuration']=all(cpu['config'].get(k)==gpu['config'].get(k) for k in keys)
            row['cpu_gpu_same_source_bindings']=cpu['source_bindings']==gpu['source_bindings']
            if not row['cpu_gpu_same_likelihood_configuration'] or not row['cpu_gpu_same_source_bindings']:
                errors.append({'case':case,'error':'CPU/GPU likelihood configuration or source differs'})
        row['comparison_scope']='Paired same captured inputs; static and ray tools can use different common-CFO grids/priors. Score or reception improvement is not isolated proof of relative-Doppler benefit.'
        result.append(row)
    return {'schema':'triad-minus60-ray-analysis-v1','analyzer_sha256':sha(__file__),
            'gates':{**GATES,'family':'K16/D8, two bit alternatives, half alpha to scoring, 60s attempt cadence',
                     'strict_target':'1e-4 any Gaussian-noise admission per24h',
                     'relaxed_target':'1e-2 per12h; re-gates existing scores, retains original strict UCB calibration'},
            'expected_paths_per_report':expected,'cases':result,'errors_or_missing_evidence':errors,
            'required_reports_and_score_checks_complete':not errors,
            'statistical_qualification':False,
            'limitations':['Development N=8 is not qualification of99% or90% correct reception or operational false admission.',
                           'CPU/GPU timing under concurrent work is diagnostic; no isolated acceleration claim.',
                           'Absent raw input files are explicitly recorded; matching reported hashes alone is not independent verification of those bytes.',
                           'Wrong-key results are empirical; Gaussian noise-only bounds do not cover wrong-key signals.']}


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',required=True,type=Path);p.add_argument('--output',type=Path)
    p.add_argument('--cases',nargs='+',default=list(DEFAULT_CASES))
    p.add_argument('--expected-paths',type=int,default=8);p.add_argument('--source-root',type=Path)
    args=p.parse_args(argv)
    if args.expected_paths<1 or len(args.cases)!=len(set(args.cases)):p.error('positive expected count and distinct cases required')
    if any(Path(case).name!=case for case in args.cases):p.error('case names must be basenames')
    result=analyze(args.root,args.cases,args.expected_paths,args.source_root)
    text=json.dumps(result,indent=2,allow_nan=False)+'\n'
    if args.output:args.output.write_text(text)
    else:sys.stdout.write(text)
    return int(not result['required_reports_and_score_checks_complete'])


if __name__=='__main__':raise SystemExit(main())
