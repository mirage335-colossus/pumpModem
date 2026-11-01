#!/usr/bin/env python3
"""Paired tensor validation summary; no operational calibration is inferred.

Run with --evidence at the campaign root, not just its final/results directory.
Missing stages are reported explicitly.  Incompatible paired inputs, model
settings, seeds or score shapes fail instead of being silently pooled.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import tarfile

import numpy as np
from scipy.stats import beta


POLICIES = ("baseline", "bf16-split2", "tf32x3")
GROUPS = {"validation-noise-v2":15, "validation-noise-high-v2":6,
          "validation-physical-v2":30, "physical-holdout":24, "long-boundary":6}
GATES = {"default":(math.log(1440*2/1e-4),1440,1e-4),
         "relaxed":(math.log(720*2/.01),720,.01)}
EXCLUDED_CONFIG = {"output","compare_report","backend","phase_implementation",
                   "tensor_mode","tensor_block_columns","tensor_fuse_advection"}


def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda:stream.read(1<<20),b''):h.update(chunk)
    return h.hexdigest()


def chernoff_interval(k,n,alpha=.05):
    """Separate one-sided bounds on mean independent Bernoullis; no iid claim."""
    if not 0<=k<=n or n<1:raise ValueError('invalid Bernoulli counts')
    if k==0:return [0.,-math.expm1(math.log(alpha)/n)]
    if k==n:return [math.exp(math.log(alpha)/n),1.]
    q=k/n;target=-math.log(alpha)/n
    def divergence(p):return q*math.log(q/p)+(1-q)*math.log((1-q)/(1-p))
    lo,hi=0.,q
    for _ in range(60):
        m=(lo+hi)/2
        if divergence(m)>target:lo=m
        else:hi=m
    lower=lo;lo,hi=q,1.
    for _ in range(60):
        m=(lo+hi)/2
        if m==1. or divergence(m)>target:hi=m
        else:lo=m
    return [lower,hi]


def binomial_interval(k,n):
    return [0. if k==0 else float(beta.ppf(.05,k,n-k+1)),
            1. if k==n else float(beta.ppf(.95,k+1,n-k))]


def bits(scores,labels):
    out=[]
    for bit in (0,1):
        selected=scores[...,labels==bit]
        if not selected.shape[-1]:raise ValueError('both bit hypotheses required')
        top=selected.max(-1,keepdims=True)
        out.append((top+np.log(np.exp(selected-top).mean(-1,keepdims=True)))[...,0])
    return np.stack(out,-1)


def counts(scores,truth):
    winner=scores.argmax(-1);n=len(scores);out={}
    for name,(threshold,attempts,alpha) in GATES.items():
        admitted=scores.max(-1)>=threshold
        row={'reference_threshold_nats':threshold,'observations':n,'gate_crossings':int(admitted.sum())}
        if truth is None:
            k=int(admitted.sum());upper=chernoff_interval(k,n)[1]
            row.update(null_gate_crossings=k,per_attempt_independent_chernoff_one_sided95=chernoff_interval(k,n),
                required_any_admission_probability_per_attempt=alpha/attempts,
                required_per_bit_allocation=alpha/(attempts*2),
                horizon_union_upper_from_empirical_per_attempt_upper=min(1.,attempts*upper),
                tail_scope='per-attempt diagnostic confidence; no observed hour/day schedule or tensor LR guarantee')
        else:
            correct=int((admitted&(winner==truth)).sum());wrong=int((admitted&(winner!=truth)).sum())
            row.update(correct=correct,wrong=wrong,missed=n-correct-wrong,
                correct_independent_chernoff_one_sided95=chernoff_interval(correct,n))
        out[name]=row
    return out


def compare(reference,current,labels):
    if reference.shape!=current.shape or not np.isfinite(reference).all() or not np.isfinite(current).all():
        raise ValueError('paired scores have mismatched shape or nonfinite values')
    r=reference.reshape(-1,reference.shape[-1]);c=current.reshape(-1,current.shape[-1]);delta=c-r
    rb,cb=bits(r,labels),bits(c,labels)
    out={'score_values':int(delta.size),'maximum_absolute_log_lr_error':float(abs(delta).max()),
         'mean_signed_log_lr_error':float(delta.mean()),'minimum_signed_log_lr_error':float(delta.min()),
         'maximum_signed_log_lr_error':float(delta.max()),
         'maximum_bit_likelihood_margin_error':float(abs((cb[:,0]-cb[:,1])-(rb[:,0]-rb[:,1])).max()),
         'candidate_ranking_changes':int((r.argmax(-1)!=c.argmax(-1)).sum()),
         'bit_ranking_changes':int((rb.argmax(-1)!=cb.argmax(-1)).sum()),'gates':{}}
    for name,(threshold,_,_) in GATES.items():
        ra,ca=rb.max(-1)>=threshold,cb.max(-1)>=threshold
        out['gates'][name]={'admission_changes':int((ra!=ca).sum()),
            'admitted_bit_changes':int((np.where(ra,rb.argmax(-1),-1)!=np.where(ca,cb.argmax(-1),-1)).sum())}
    return out


def policy(path,report):
    name=next((p for p in POLICIES if path.stem.endswith('-'+p)),None)
    if name is None:raise ValueError(f'unknown report policy: {path}')
    cfg=report['configuration'];backend=cfg.get('backend',cfg.get('phase_implementation'))
    if (cfg.get('duration_alternatives',1)!=1 or cfg.get('selection_methods',1)!=1
            or cfg.get('attempt_spacing',60)!=60):
        raise ValueError('analysis gates require the declared one-method/one-duration/60-second family')
    if name=='baseline' and backend!='graph-fp64':raise ValueError('baseline is not graph-fp64')
    if name!='baseline' and (backend!='tensor' or cfg.get('tensor_mode')!=name):raise ValueError('tensor policy mismatch')
    return name,path.stem[:-(len(name)+1)]


def normalized_sources(report):
    src=report.get('source_sha256',report.get('provenance',{}).get('source_sha256',{}))
    return {Path(k).name:v for k,v in src.items()}


def matching(reference,current):
    left={k:v for k,v in reference['configuration'].items() if k not in EXCLUDED_CONFIG}
    right={k:v for k,v in current['configuration'].items() if k not in EXCLUDED_CONFIG}
    if left!=right:raise ValueError('paired configuration mismatch: '+str(sorted(k for k in set(left)|set(right) if left.get(k)!=right.get(k))))
    for key in ('input_identity','represented_observation_sha256_by_batch',
                'independent_path_manifest_hashes','independent_noise_seeds'):
        if reference.get(key)!=current.get(key):raise ValueError(f'paired {key} mismatch')
    for key in ('input_manifest_sha256','input_files','effective_noise_seed'):
        if reference.get('provenance',{}).get(key)!=current.get('provenance',{}).get(key):raise ValueError(f'paired provenance {key} mismatch')
    for key in ('candidate_labels','candidate_mappings','scored_blocks','full_blocks','phase_states','frequency_states','gain_quadrature_states'):
        if reference.get('workload',{}).get(key)!=current.get('workload',{}).get(key):raise ValueError(f'paired workload {key} mismatch')
    a,b=normalized_sources(reference),normalized_sources(current)
    if not all(a[k]==b[k] for k in a.keys()&b.keys()):raise ValueError('paired shared scorer source mismatch')


class Evidence:
    def __init__(self,root):
        self.root=root;self.files={};self.manifests={}
        for folder in (root/'prepared/inputs',root/'physical-holdout'):
            if folder.exists():
                for path in folder.rglob('manifest.json'):self._manifest(path.read_bytes(),str(path.relative_to(root)))
        for name in ('gpu-bundle.tar.gz','physical-inputs.tar.gz','holdout-inputs.tar.gz'):
            path=root/name
            if path.exists():
                self.files[name]=sha(path)
                with tarfile.open(path) as archive:
                    for member in archive:
                        if member.isfile() and member.name.startswith('inputs/') and member.name.endswith('/manifest.json'):
                            self._manifest(archive.extractfile(member).read(),name+':'+member.name)

    def _manifest(self,data,location):
        digest=hashlib.sha256(data).hexdigest();self.manifests[digest]={'data':json.loads(data),'locations':[location]+self.manifests.get(digest,{}).get('locations',[])}

    def report(self,path):
        self.files[str(path.relative_to(self.root))]=sha(path);d=json.loads(path.read_text())
        archive=path.with_suffix('.scores.npz')
        if not archive.exists():raise ValueError(f'missing scores: {archive}')
        self.files[str(archive.relative_to(self.root))]=sha(archive)
        with np.load(archive) as z:scores={k:z[k].copy() for k in z.files}
        return d,scores

    def manifest(self,digest):
        if digest not in self.manifests:raise ValueError('actual input manifest bytes unavailable: '+str(digest))
        return self.manifests[digest]['data']


def driver_index(evidence,folder):
    path=folder/'driver.json'
    if not path.exists():return {},{'status':'missing'}
    evidence.files[str(path.relative_to(evidence.root))]=sha(path);d=json.loads(path.read_text());index={}
    for row in d.get('runs',[]):
        if row.get('returncode')!=0:continue
        command=row.get('command',[])
        if '--output' in command:name=Path(command[command.index('--output')+1]).stem
        else:name=row.get('name')
        index[name]=row.get('parent_wall_seconds',row.get('wall_seconds'))
    return index,{'source_sha256':d.get('source_sha256',{}),'completed_parent_jobs':len(index)}


def optimization_timing(d,parent):
    t=d['timing_seconds'];work=t['candidate_scoring_work'];stream=d['results'].get('streamed_vs_resident_max_abs_log_lr',{})
    maxes={k:v for k,v in stream.items() if k.endswith('_max_abs_log_lr')}
    return {'parent_complete_diagnostic_job_seconds':parent,'complete_driver_job_wall_seconds':t['complete_job_wall_seconds'],
        'reconstructed_resident_organization_budget_seconds':work['resident_warm_pipeline_seconds'],
        'reconstructed_streamed_organization_budget_seconds':work['streamed_warm_pipeline_seconds'],
        'observation_sets_scored_per_organization':work['observation_sets_scored'],
        'reported_streamed_resident_maximum_log_lr_errors':maxes,
        'reported_streamed_resident_numeric_identity':all(v==0 for v in maxes.values()) if maxes else None,
        'stream_identity_scope':'scalar comparison reported by scorer; separate streamed score arrays are not archived for independent byte equality',
        'measured_stage_seconds':{k:v for k,v in t.items() if k not in ('candidate_scoring_work','signal_cold_first_use_capture')},
        'scope':'complete diagnostic job executes resident and streamed passes plus warmup/cold checks; reconstructed budgets are not independent measured single-capture jobs'}


def summarise_group(evidence,name,expected):
    folder=evidence.root/'final/results'/name
    if not folder.exists():return {'status':'missing','expected_reports':expected,'present_reports':0},[]
    driver,driver_info=driver_index(evidence,folder);cases={};rows=[];physical_records=[]
    for path in sorted(folder.glob('*.json')):
        if path.name in ('driver.json','timeout.json'):continue
        d,s=evidence.report(path);p,case=policy(path,d)
        if p in cases.setdefault(case,{}):raise ValueError('duplicate policy report')
        cases[case][p]=(path,d,s)
    missing=[]
    for case,policies in sorted(cases.items()):
        if 'baseline' not in policies:missing.append(case+':baseline');continue
        bp,base,bs=policies['baseline']
        for p in POLICIES:
            if p not in policies:missing.append(case+':'+p);continue
            path,d,scores=policies[p];matching(base,d)
            if set(bs)!=set(scores):raise ValueError('paired archive keys mismatch')
            row={'case':case,'policy':p,'report':str(path.relative_to(evidence.root)),
                 'report_sha256':evidence.files[str(path.relative_to(evidence.root))],
                 'score_archive_sha256':evidence.files[str(path.with_suffix('.scores.npz').relative_to(evidence.root))],
                 'cn0_db_hz':d['configuration'].get('cn0'),'comparisons':{},'gate_counts':{},
                 'qualification':'reference-gate paired diagnostics only; no operational precision or tail qualification'}
            if d['schema']=='triad-resident-campaign-v1':
                identity=d['input_identity'];manifest=evidence.manifest(identity['manifest_sha256'])
                labels=np.asarray([manifest['candidates'][i]['bit'] for i in identity['selected_candidates']])
                a=scores['scores'];b=bs['scores'];shape=(d['configuration']['batches'],2,d['configuration']['batch'],len(labels))
                if a.shape!=shape:raise ValueError('resident score shape differs from complete work declaration')
                row['input_manifest_sha256']=identity['manifest_sha256'];row['independent_noise_trials_per_kind']=shape[0]*shape[2]
                for kind,i in (('null',0),('signal',1)):
                    x,y=b[:,i].reshape(-1,len(labels)),a[:,i].reshape(-1,len(labels))
                    row['comparisons'][kind]=compare(x,y,labels)
                    row['gate_counts'][kind]=counts(bits(y,labels),None if kind=='null' else manifest['signal_bit'])
                row['timing']={'parent_complete_job_seconds':driver.get(path.stem),'measured_pipeline_and_stages':d['timing'],
                    'scope':'one fixed physical design, paired projected H0/H1; startup/output included only in parent job'}
            elif d['schema']=='triad-gpu-optimization-report-v1':
                digest=d['provenance']['input_manifest_sha256'];manifest=evidence.manifest(digest);labels=np.asarray(d['workload']['candidate_labels'])
                row['input_manifest_sha256']=digest;row['physical_signal_enabled']=manifest.get('signal_enabled',True)
                actual=d['configuration']['actual_observation'];control=actual and not manifest.get('signal_enabled',True)
                row['observation_kind']='sampled_physical_null_control' if control else ('actual_sampled_projection' if actual else 'projected_Gaussian_noise_on_sampled_design')
                for kind,a in scores.items():
                    if a.shape[-1]!=len(labels):raise ValueError('optimization score candidate shape mismatch')
                    row['comparisons'][kind]=compare(bs[kind],a,labels)
                    row['gate_counts'][kind]=counts(bits(a,labels),None if kind=='null' or control else manifest['signal_bit'])
                row['timing']=optimization_timing(d,driver.get(path.stem))
                if control:row['physical_null_warning']='archive key signal contains actual signal-absent interference; threshold crossings are model-mismatch diagnostics, not successful receptions'
            elif d['schema']=='triad-path-study-report-v1':
                if len(d['rows'])!=1 or d['rows'][0]['summary']['method']!='phase-frequency':raise ValueError('holdout summary expects one phase-frequency C/N0 per report')
                paths=d['rows'][0]['paths'];digests=d['independent_path_manifest_hashes'];n=len(paths)
                if len(set(digests))!=n or len(d['independent_noise_seeds'])!=n:raise ValueError('path identity/count mismatch')
                truth=np.asarray([x['transmitted_bit'] for x in paths]);labels=np.asarray([0,1])
                for i,x in enumerate(paths):
                    manifest=evidence.manifest(digests[i])
                    if (x['manifest_sha256']!=digests[i] or x['noise_seed']!=d['independent_noise_seeds'][i] or x['transmitted_bit']!=manifest['signal_bit'] or x['channel_seed']!=manifest['channel_seed'] or x['key_seed']!=manifest['key_seed']):raise ValueError('path seed/bit/manifest mismatch')
                row['cn0_db_hz']=d['rows'][0]['summary']['cn0_db_hz'];row['path_manifest_sha256']=digests
                row['independent_noise_seeds']=d['independent_noise_seeds'];row['independent_paths']=n
                for kind in ('signal','null','actual'):
                    key='cn0_phase-frequency_'+kind
                    if key not in scores:raise ValueError('missing path observation kind '+kind)
                    if scores[key].shape!=(n,2):raise ValueError('path score shape mismatch')
                    row['comparisons'][kind]=compare(bs[key],scores[key],labels)
                    row['gate_counts'][kind]=counts(scores[key],None if kind=='null' else truth)
                row['timing']={'parent_complete_joint_job_seconds':driver.get(path.stem),
                    'joint_methods_wall_seconds':d['rows'][0]['summary']['joint_methods_wall_seconds'],
                    'scope':'parent job scores projected H1, projected H0 and actual sampled projection; actual scoring wall is separately reported, not parent single-capture latency'}
                physical_records.append({'policy':p,'digests':digests,'truth':truth,'scores':scores,
                                         'noise_seeds':d['independent_noise_seeds'],
                                         'key_seeds':[v['key_seed'] for v in paths],
                                         'channel_seeds':[v['channel_seed'] for v in paths],
                                         'parent_seconds':driver.get(path.stem),'timing':row['timing']['joint_methods_wall_seconds']})
            else:raise ValueError('unsupported validation schema '+d['schema'])
            rows.append(row)
    numerics={}
    for p in POLICIES:
        numerics[p]={}
        for kind in sorted({k for row in rows if row['policy']==p for k in row['comparisons']}):
            selected=[row['comparisons'][kind] for row in rows if row['policy']==p and kind in row['comparisons']]
            n=sum(v['score_values'] for v in selected)
            numerics[p][kind]={
                'maximum_absolute_log_lr_error':max(v['maximum_absolute_log_lr_error'] for v in selected),
                'mean_signed_log_lr_error_over_saved_score_points':sum(v['mean_signed_log_lr_error']*v['score_values'] for v in selected)/n,
                'minimum_signed_log_lr_error':min(v['minimum_signed_log_lr_error'] for v in selected),
                'maximum_signed_log_lr_error':max(v['maximum_signed_log_lr_error'] for v in selected),
                'maximum_bit_likelihood_margin_error':max(v['maximum_bit_likelihood_margin_error'] for v in selected),
                'candidate_ranking_changes_over_saved_score_points':sum(v['candidate_ranking_changes'] for v in selected),
                'bit_ranking_changes_over_saved_score_points':sum(v['bit_ranking_changes'] for v in selected),
                'gate_changes_over_saved_score_points':{g:sum(v['gates'][g]['admission_changes'] for v in selected) for g in GATES},
                'scope':'numerical extrema/counts over saved score points only; repeated SNRs/policies are not independent trial counts'}
    return {'status':'complete' if len(rows)==expected and not missing else 'incomplete',
            'expected_reports':expected,'present_reports':len(rows),'missing_policies':missing,'driver':driver_info,
            'rows':rows,'policy_numerics':numerics,
            'sweep_dependence':'SNR points reuse root/noise draws; do not pool as independent trials'},physical_records


def aggregate_paths(records):
    if not records:return {'status':'missing'}
    out={}
    for p in POLICIES:
        rows=[r for r in records if r['policy']==p]
        if not rows:continue
        digests=[v for r in rows for v in r['digests']]
        if len(set(digests))!=len(digests):raise ValueError('physical paths were reused across chunks')
        for key in ('noise_seeds','key_seeds','channel_seeds'):
            seeds=[v for r in rows for v in r[key]]
            if len(set(seeds))!=len(seeds):raise ValueError('physical holdout seed reused across chunks: '+key)
        truth=np.concatenate([r['truth'] for r in rows]);result={'independent_paths':len(truth),'path_manifest_sha256':digests,
            'balanced_bits':{str(b):int((truth==b).sum()) for b in (0,1)},'gate_counts':{}}
        for kind in ('signal','null','actual'):
            scores=np.concatenate([r['scores']['cn0_phase-frequency_'+kind] for r in rows])
            result['gate_counts'][kind]=counts(scores,None if kind=='null' else truth)
        result['summed_parent_joint_job_seconds']=sum(r['parent_seconds'] for r in rows if r['parent_seconds'] is not None)
        result['summed_projected_signal_null_scoring_seconds']=sum(r['timing']['signal_and_null'] for r in rows)
        result['summed_actual_sampled_scoring_seconds']=sum(r['timing']['actual_sampled_noise'] for r in rows)
        result['timing_scope']='parent includes projected H1/H0 plus actual; actual scoring is separately timed, excludes source generation/startup'
        out[p]=result
    return {'status':'complete' if all(out.get(p,{}).get('independent_paths')==128 for p in POLICIES) else 'incomplete',
        'policies':out,'confidence_scope':'separate one-sided95 Chernoff bounds for independent possibly heterogeneous paths; reused policies are paired, not extra independent paths; nulls are projected Gaussian'}


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--evidence',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args(argv)
    evidence=Evidence(args.evidence);groups={};paths=[]
    for name,expected in GROUPS.items():groups[name],records=summarise_group(evidence,name,expected);paths.extend(records)
    out={'schema':'triad-tensor-validation-analysis-v1','analysis_source_sha256':sha(Path(__file__)),
        'groups':groups,'physical_path_aggregate':aggregate_paths(paths),'input_file_sha256':evidence.files,
        'input_manifests':{k:{'locations':v['locations'],'signal_bit':v['data'].get('signal_bit'),'signal_enabled':v['data'].get('signal_enabled',True)} for k,v in evidence.manifests.items()},
        'input_binding_scope':'actual manifest bytes rehashed; paired scorer-declared input component hashes checked equal. Historical exporter provenance and every component byte are not independently certified by this analysis.',
        'calibration':{'status':'unqualified','qualified_result_count':0,'cost_per_qualified_result_usd':None,
            'sensitivity_loss_budgets_db':{str(x):'unresolved' for x in (.1,.25,.5,1.)},
            'zero_of_384_one_sided95_upper_per_attempt':-math.expm1(math.log(.05)/384),
            'required_default_probability_per_attempt':1e-4/1440,'required_relaxed_probability_per_attempt':.01/720,
            'warning':'pointwise noise/path screens cannot certify tiny horizon tails or safe precision reduction; SNR sweeps and policies are paired and not pooled'}}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(out,indent=2,allow_nan=False)+'\n')
    print(json.dumps({'output':str(args.output),'groups':{k:{'status':v['status'],'reports':v.get('present_reports')} for k,v in groups.items()},'physical_paths':out['physical_path_aggregate']['status']}))
    return out


if __name__=='__main__':main()
