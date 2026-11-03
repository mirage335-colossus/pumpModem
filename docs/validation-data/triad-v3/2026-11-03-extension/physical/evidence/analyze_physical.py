#!/usr/bin/env python3
"""Read-only recount of compact extension exports and source-variant scores.
No modem executions. All computations use one CPU; input identity is hashed.
"""
import os
for key in ('OPENBLAS_NUM_THREADS','OMP_NUM_THREADS','MKL_NUM_THREADS'): os.environ[key]='1'
import argparse, collections, hashlib, json, math
from pathlib import Path
import numpy as np
from scipy.special import logsumexp
from scipy.stats import beta, norm

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def cp(k,n):
    return {'lower95_one_sided':float(beta.ppf(.05,k,n-k+1)) if k else 0.,
            'upper95_one_sided':float(beta.ppf(.95,k+1,n-k)) if k<n else 1.}
def span(vals):
    return {'min':float(min(vals)), 'median':float(np.median(vals)), 'max':float(max(vals)), 'mean':float(np.mean(vals))} if vals else None

def main():
    a=argparse.ArgumentParser();a.add_argument('--input',type=Path,default=Path('local-evidence/triad-extension/remote/physical-reports'));a.add_argument('--output',type=Path,default=Path('local-evidence/triad-extension/analysis/physical-analysis.json'));args=a.parse_args()
    root=args.input; provenance={}; plans={}; exports={}; records=[]; failures=[]; receipts=[]; capture_rows={}
    def read(p):
        provenance[str(p.relative_to(root))]=sha(p);return json.loads(p.read_text())
    for p in sorted(root.glob('*-plan.json')):
        d=read(p)
        if d.get('schema')!='triad-extension-physical-plan-v1':continue
        plans[sha(p)]={'data':d,'name':p.name[:-10],'path':str(p.relative_to(root)),'jobs':{j['id']:j for j in d['jobs']},'cells':{c['name']:c for c in d['cells']}}
    receipt_data=[]
    for p in sorted(root.rglob('results.json')):
        d=read(p);receipt_data.append((p,d)); receipts.append({'path':str(p.relative_to(root)), 'schema':d['schema'],'counts':d.get('counts'), 'elapsed_seconds':d.get('elapsed_seconds'), 'source_and_inputs_unchanged':d.get('source_and_inputs_unchanged'), 'source_unchanged':d.get('source_unchanged')})
        ph=d.get('plan_sha256',d.get('original_plan_sha256'))
        if ph not in plans: raise ValueError('receipt without plan: '+str(p))
        for j in d['jobs']:
            if j.get('stage')=='export':
                key=(ph,j['id']); assert key not in exports
                exports[key]=j
            if d['schema']=='triad-extension-physical-stage-v1' and j.get('stage')=='acquire' and j['status']!='completed':
                failures.append({'receipt':str(p.relative_to(root)), 'plan_sha256':ph,'id':j['id'],'status':j['status'],'reason':j.get('reason'),'argv':j.get('argv'),'log_sha256':j.get('log_sha256')})
    for p,d in receipt_data:
        if d['schema']!='triad-extension-source-variant-results-v1':continue
        ph=d['original_plan_sha256']; plan=plans[ph]; prefix=plan['name']
        assert d['source_and_inputs_unchanged'] and d['all_selected_jobs_accounted']
        for j in d['jobs']:
            orig=plan['jobs'][j['original_acquisition_id']];eid=orig['depends_on'][0]; cell=plan['cells'][j['case']]; ex=exports[(ph,eid)]
            assert ex['status']=='completed' and j['status']=='completed'
            mp=root/prefix/'workspaces'/eid/'manifest.json';m=read(mp)
            ev=ex['evaluation_only']; energy=ev['post_erasure_signal_energy_seconds_evaluation_only'];cn0=cell['cn0_db_hz'];rho=energy*10**(cn0/10)
            assert math.isclose(energy*m['sample_rate'],m['channel_observed_energy_samples'],rel_tol=3e-12,abs_tol=1e-9)
            assert math.isclose(rho,m['channel_observed_energy_samples']/m['noise_variance'],rel_tol=3e-12,abs_tol=1e-9)
            capkey=prefix+'/'+eid
            if capkey not in capture_rows:
                argv=plan['jobs'][eid]['argv'];seeds={f:argv[argv.index(f)+1] for f in ('--seed','--key-seed','--channel-seed','--oscillator-seed','--noise-seed','--frontend-seed','--path-seed')}
                capture_rows[capkey]={'id':eid,'plan':prefix,'suite':plan['data']['suite'],'case':j['case'],'control':j['control'],'bit':j['bit'],'cn0_db_hz':cn0,'energy_seconds':energy,'rho':rho,'elapsed_source_seconds':m['actual_elapsed_seconds'],'sampled_seconds':m['sample_count']/m['sample_rate'],'fine_rate_hz':cell['rate_hz']*cell['added_symbols']/6,'seeds':seeds,'prior_no_loss_energy_ratio':ev['prior_same_seed_no_loss_energy_ratio_evaluation_only'],'parent_calendar_equal':ev['parent_calendar_equal']}
            rp=root/(prefix+'-rescored')/next(f['path'] for f in j['output_files'] if f['path'].endswith('.json'))
            rr=read(rp);v=rr['captures'][0]; sp=rp.with_suffix('.npz')
            assert sha(sp)==rr['scores_npz_sha256'];provenance[str(sp.relative_to(root))]=sha(sp)
            with np.load(sp) as ar:
                gs=np.asarray(ar['group_scores_0']); bs=np.asarray(ar['bit_scores_0']);assert np.array_equal(gs,np.asarray(v['group_bit_log_likelihoods']));assert np.array_equal(bs,np.asarray(v['bit_log_likelihoods']))
                np.testing.assert_allclose(logsumexp(gs,axis=0)-math.log(len(gs)),bs,rtol=1e-12,atol=1e-10)
            winner=int(np.argmax(bs)); new_out={}
            for gate in ('strict','relaxed'):
                threshold=v[gate]['log_threshold'];alpha=math.exp(-threshold);z=float(norm.isf(alpha));admit=bool(bs[winner]>=threshold)
                if j['control']=='signal': out={'correct_admitted':int(admit and winner==j['bit']),'wrong_admitted':int(admit and winner!=j['bit']),'missed':int(not admit)}
                else:out={'false_admitted':int(admit),'not_admitted':int(not admit)}
                new_out[gate]=out
                target=.99 if gate=='strict' else .9
                required=.5*(z+float(norm.ppf(target)))**2
                records.append({'plan':prefix,'suite':plan['data']['suite'],'case':j['case'],'cn0_db_hz':cn0,'method':j['method'],'gate':gate,'control':j['control'],'bit':j['bit'],'capture':eid,'capture_key':capkey,'outcomes':out,'scores':bs.tolist(),'threshold':threshold,'allocated_alpha':alpha,'genie_presence_pd':float(norm.cdf(math.sqrt(2*rho)-z)) if j['control']=='signal' else None,'required_rho_for_gate_target':required,'rho':rho,'energy_seconds':energy,'energy_margin_db_vs_genie_target':10*math.log10(rho/required) if rho>0 else None,'receiver_seconds':v['timing']['total_receiver_seconds'],'base_groups':v['geometry']['base_group_count'],'effective_groups':v['geometry']['group_count'],'window_seconds':v['geometry']['nominal_window_seconds'],'phase_loss_allowance_db':j['maximum_block_phase_loss_db'],'phase_loss_bound_db':v['frequency_reuse']['coherent_power_loss_bound_db'],'report':str(rp.relative_to(root))})
            assert new_out==j['acquisition_summary']['outcomes']==v['simulation_evaluation_only']['outcomes']
    group=collections.defaultdict(list)
    for r in records: group[tuple(r[x] for x in ('plan','suite','case','cn0_db_hz','method','gate'))].append(r)
    summary=[]
    for key,rows in sorted(group.items()):
        v=dict(zip(('plan','suite','case','cn0_db_hz','method','gate'),key)); counts={}
        for control in ('signal','null','wrong-key'):
            sub=[r for r in rows if r['control']==control];c=collections.Counter()
            for r in sub:c.update(r['outcomes'])
            counts[control]={'trials':len(sub),**dict(c)}
            if sub:counts[control]['cp95_one_sided']=cp(c['correct_admitted'] if control=='signal' else c['false_admitted'],len(sub))
        sig=[r for r in rows if r['control']=='signal']
        v.update(counts=counts,genie_presence_pd=span([r['genie_presence_pd'] for r in sig]),rho=span([r['rho'] for r in sig]),energy_seconds=span([r['energy_seconds'] for r in sig]),energy_margin_db_vs_genie_target=span([r['energy_margin_db_vs_genie_target'] for r in sig]),receiver_seconds=span([r['receiver_seconds'] for r in rows]),allocated_alpha=rows[0]['allocated_alpha'],threshold=rows[0]['threshold'],required_rho_for_gate_target=rows[0]['required_rho_for_gate_target'],phase_loss_allowance_db=sorted({r['phase_loss_allowance_db'] for r in rows}),phase_loss_bound_db=span([r['phase_loss_bound_db'] for r in rows]));summary.append(v)
    pathgroups=collections.defaultdict(list)
    for r in capture_rows.values():pathgroups[(r['suite'],r['case'],r['id'])].append(r)
    pairchecks=[]
    for k,rs in pathgroups.items():
        if len(rs)<2:continue
        assert len({json.dumps(r['seeds'],sort_keys=True) for r in rs})==1
        pairchecks.append({'suite':k[0],'case':k[1],'id':k[2],'cn0_values':sorted(r['cn0_db_hz'] for r in rs),'same_all_seed_arguments':True,'energy_relative_spread':(max(r['energy_seconds'] for r in rs)-min(r['energy_seconds'] for r in rs))/max(r['energy_seconds'] for r in rs) if max(r['energy_seconds'] for r in rs)>0 else 0})
    out={'schema':'triad-extension-physical-analysis-v1','generator_sha256':sha(Path(__file__)),'input_root':str(root),'receipts':receipts,'provenance_sha256':provenance,'counts':{'plans':len(plans),'captures':len(capture_rows),'variant_method_scores':len(records)//2,'original_failed_acquisitions':len(failures)},'original_failures':failures,'summary':summary,'captures':list(capture_rows.values()),'records':records,'same_seed_across_cn0':pairchecks,'benchmark':{'formula':'rho=post_erasure_signal_energy_seconds*10**(cn0_db_hz/10); alpha=exp(-actual_log_gate); Pd=Phi(sqrt(2*rho)-norm.isf(alpha)); rho_required=.5*(norm.isf(alpha)+norm.ppf(target))**2','scope':'Known complete complex waveform, channel, transmitted bit, alignment and white proper-complex noise variance. Fixed-path Gaussian presence-detection NP benchmark at the same per-test alpha allocation; not achieved receiver performance, bit-error probability, authentication, acquisition, full-system optimum or a duration guarantee. No waveform energy supplied to receiver.','target_mapping':{'strict':.99,'relaxed':.9}},'limitations':['Only16 independent signal paths per validation cell; methods and SNR alternatives reuse those paths. Same seeds across changed physical cases need not imply identical samples.','Pointwise one-sided Clopper-Pearson endpoints assume independent equal-probability Bernoulli trials within the stated cell; they are not simultaneous intervals across this selected grid.','Original six failed executions remain separate from repaired source-variant results. Microwave phase-loss tolerance0.02dB is a distinct approximation arm; all other cells retain0.01dB.','Compact report package omits raw physical statistics. Receipts and available JSON/NPZ hashes are checked; raw content hashes are retained from source-bound receipts, not independently re-read here.']}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(out,indent=2,sort_keys=True,allow_nan=False)+'\n')
    print(json.dumps(out['counts'])); print('Original failure IDs:',[f['id'] for f in failures]);print('Wrong signal admissions:',[(r['plan'],r['capture'],r['method'],r['gate'],r['scores']) for r in records if r['outcomes'].get('wrong_admitted')]);print('False control admissions:',[(r['plan'],r['capture'],r['method'],r['gate']) for r in records if r['outcomes'].get('false_admitted')]);
    for s in summary:
        if s['suite']=='validation' and s['gate']=='strict':print(s['plan'],s['case'],s['method'],s['counts']['signal'], 'genie_pd',s['genie_presence_pd'], 'rho',s['rho'])
if __name__=='__main__':main()
