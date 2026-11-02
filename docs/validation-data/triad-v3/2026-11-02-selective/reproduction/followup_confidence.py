#!/usr/bin/env python3
"""Independent frozen followup saved-score recount and pointwise confidence."""
import pathlib,json,hashlib,math,collections,sys,importlib.util,os
import numpy as np
from scipy.stats import beta
REPO=pathlib.Path(__file__).resolve().parents[5]
os.chdir(REPO)
folder=pathlib.Path('docs/validation-data/triad-v3/2026-11-02-selective/reproduction')
base=pathlib.Path('local-evidence/triad-selective/followup-complete')
audit_path=pathlib.Path('local-evidence/triad-selective/followup-audit-preview.json')
out=folder/'followup-confidence.json'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
load=lambda p:json.loads(p.read_text())
spec=importlib.util.spec_from_file_location('followup_independent_generator',pathlib.Path('tools/triad-lowrate-campaign.py'));gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
ep=folder/'followup-export-plan.json';sp=folder/'followup-score-plan.json';export=load(ep);score=load(sp);audit=load(audit_path)
assert audit['integrity_complete'] and len(audit['cells'])==9 and len(audit['export_jobs'])==len(audit['score_jobs'])==34
assert audit['plans']['export']['sha256']==sha(ep) and audit['plans']['score']['sha256']==sha(sp)
source=pathlib.Path(audit['evidence']['source_root']);mapping=lambda p:base/pathlib.Path(p).relative_to(source)
jobs={j['name']:j for j in export['jobs']};ajobs={j['name']:j for j in audit['score_jobs']};acells={c['cell']['name']:c for c in audit['cells']}
expected={};campaigns={};receipts={};manifests={};manifest_hashes=[];source_hashes=set();deps=set()
for j in export['jobs']:
 args=gen.parser().parse_args(j['argv'][2:]);directory=str(args.output);campaign=load(mapping(directory)/'campaign.json');campaigns[j['name']]=campaign
 assert campaign['paths_succeeded']==campaign['paths_requested']==j['path_count'] and not campaign['cancelled']
 assert campaign['binary_sha256']==audit['evidence']['source_provenance']['binary_sha256']
 rows={r['path_index']:r for r in campaign['results']};assert set(rows)==set(v['index'] for v in j['simulation_only_expected'])
 for v in j['simulation_only_expected']:
  remote=str(pathlib.Path(directory)/f"path-{v['index']:05d}");mp=mapping(remote)/'manifest.json';m=load(mp);row=rows[v['index']]
  command,identity=gen.make_command(args,v['index'],pathlib.Path(remote));assert v=={'index':v['index'],**identity} and command==row['command']==m['command_argv']
  assert m==row['export_manifest'] and row['returncode']==0 and row['runner_status']['outcome']=='completed' and row['runner_status']['termination']['group_empty']
  assert m['signal_bit']==v['source_bit']==v['index']%2 and m['epoch']==v['epoch'] and m['control']==j['control']
  for d in gen.SEED_DOMAINS:assert (m['key_seed'] if d=='key' else m['random_seed_roots'][d])==v[d+'_seed']
  expected[remote]=v;receipts[remote]=row;manifests[remote]=m;manifest_hashes.append((remote,sha(mp)))
tally=collections.defaultdict(lambda:collections.Counter());strata=collections.defaultdict(lambda:{0:collections.Counter(),1:collections.Counter()});reports=[];admitted=[];allrows={};nscore=0
for j in score['jobs']:
 first=next(i for i,x in enumerate(j['argv']) if x.startswith('--'));inputs=j['argv'][2:first];rp=mapping(j['argv'][j['argv'].index('--output')+1]);r=load(rp);npz=rp.parent/r['scores_file'];assert sha(rp)==ajobs[j['name']]['report_sha256'] and sha(npz)==r['scores_sha256']==ajobs[j['name']]['scores_sha256']
 assert [x['input'] for x in r['paths']]==inputs and r['failed_physical_path_records']==0 and r['observation_mode']=='actual-noisy'
 source_hashes.add(r['source_sha256']);deps.add(json.dumps(r['dependency_sha256'],sort_keys=True));used=set()
 with np.load(npz,allow_pickle=False) as a:
  for row in r['paths']:
   remote=row['input'];v=expected[remote];assert row['status']=='completed' and len(row['conditions'])==1 and row['control']==j['control'];assert sha(mapping(remote)/'manifest.json')==row['input_hashes']['manifest.json'];assert set(row['input_hashes'])=={'manifest.json','gram.c128','observation.c128','received_energy.f64','intervals.u64','valid_rows.u64'}
   c=row['conditions'][0];assert c['control']==j['control'] and c['noise_trials']==c['actual_observations']==1 and set(c['methods'])=={'gaussian-window-ucb'};d=c['methods']['gaussian-window-ucb'];x=a[d['score_array']];used.add(d['score_array']);assert x.shape==(1,2) and np.isfinite(x).all();nscore+=1;winner=int(x[0].argmax());best=float(x[0,winner]);allrows[remote]=row
   for gate,threshold in [('strict',22.721063389585737),('relaxed',17.4227460230377)]:
    yes=best>=threshold;counts=tally[(j['cell'],gate)];counts['trials']+=1;bit=strata[(j['cell'],gate)][v['source_bit']];bit['trials']+=1
    if j['control']=='signal':
     label='correct_accepted' if yes and winner==v['source_bit'] else 'wrong_accepted' if yes else 'missed';counts[label]+=1;bit[label]+=1;counts['raw_correct']+=winner==v['source_bit'];bit['raw_correct']+=winner==v['source_bit']
     direct={'trials':1,'correct_accepted':int(yes and winner==v['source_bit']),'wrong_accepted':int(yes and winner!=v['source_bit']),'missed':int(not yes),'raw_correct':int(winner==v['source_bit'])}
    else:
     label='false_admitted' if yes else 'not_admitted';counts[label]+=1;bit[label]+=1;direct={'trials':1,'false_admitted':int(yes),'not_admitted':int(not yes)}
    if gate=='strict':assert direct==d['counts']
   if j['control']!='signal' and best>=17.4227460230377:admitted.append({'cell':j['cell'],'input':remote,'index':v['index'],'control':j['control'],'score':best,'scores':x[0].tolist(),'winner':winner,'strict_admitted':best>=22.721063389585737,'relaxed_admitted':True,'manifest_sha256':row['input_hashes']['manifest.json'],'transmit_key_seed':v['key_seed'],'receive_key_seed_by_exporter_branch':v['key_seed']^0x6a09e667f3bcc909 if j['control']=='wrong-key' else v['key_seed']})
  assert set(a.files)==used
 reports.append({'job':j['name'],'report':str(rp),'report_sha256':sha(rp),'score_npz_sha256':sha(npz),'physical_cases':len(r['paths'])})
assert nscore==len(expected)==1628 and len(reports)==34 and len(source_hashes)==len(deps)==1

def cp(k,n,alpha=.05):
 return {'successes':k,'trials':n,'fraction':k/n,'lower_one_sided_95':float(beta.ppf(alpha,k,n-k+1)) if k else 0.,'upper_one_sided_95':float(beta.ppf(1-alpha,k+1,n-k)) if k<n else 1.}
def balanced(bits,event):
 ns=[bits[b]['trials'] for b in (0,1)];ks=[bits[b][event] for b in (0,1)];assert ns[0]==ns[1]>0;n=sum(ns)
 lower=.05**(1/n) if ks==ns else sum(float(beta.ppf(.025,k,N-k+1)) if k else 0. for k,N in zip(ks,ns))/2
 upper=1-.05**(1/n) if not any(ks) else sum(float(beta.ppf(.975,k+1,N-k)) if k<N else 1. for k,N in zip(ks,ns))/2
 fraction=sum(ks)/n;assert 0<=lower<=fraction<=upper<=1
 return {'fraction':fraction,'lower_one_sided_95':lower,'upper_one_sided_95':upper,'lower_method':'equal-stratum all-success AM-GM' if ks==ns else 'average per-bit 97.5% one-sided CP lower bounds; Bonferroni','upper_method':'equal-stratum all-failure AM-GM (complement)' if not any(ks) else 'average per-bit 97.5% one-sided CP upper bounds; Bonferroni','per_bit':{str(b):cp(bits[b][event],bits[b]['trials']) for b in (0,1)}}

cells=[]
for c in export['cells']:
 name=c['name'];entry={'cell':c,'gates':{}}
 for gate in ['strict','relaxed']:
  counts=tally[(name,gate)]
  for key in (['trials','correct_accepted','wrong_accepted','missed','raw_correct'] if c['control']=='signal' else ['trials','false_admitted','not_admitted']):counts[key]+=0
  for bit in strata[(name,gate)].values():
   for key in (['trials','correct_accepted','wrong_accepted','missed','raw_correct'] if c['control']=='signal' else ['trials','false_admitted','not_admitted']):bit[key]+=0
  a=acells[name]['gates'][gate];assert a['attempted']==counts['trials'];keys=['correct_accepted','wrong_accepted','missed'] if c['control']=='signal' else ['false_admitted','not_admitted'];assert all(a[k]==counts[k] for k in keys)
  event='correct_accepted' if c['control']=='signal' else 'false_admitted';ci=balanced(strata[(name,gate)],event)
  if c['control']=='signal':assert math.isclose(ci['lower_one_sided_95'],a['balanced_mean_correct_probability_lower_95'],rel_tol=0,abs_tol=1e-14)
  status={str(target):('pointwise_lower_establishes_target' if ci['lower_one_sided_95']>=target else 'pointwise_upper_below_target' if ci['upper_one_sided_95']<target else 'target_not_resolved_by_pointwise_bounds') for target in [.99,.90]} if c['control']=='signal' else None
  entry['gates'][gate]={'counts':dict(counts),'per_bit_counts':{str(b):dict(strata[(name,gate)][b]) for b in (0,1)},'balanced_population_event':event,'balanced_probability_pointwise_bounds':ci,'correct_reception_target_assessment':status,'scope':'Noise-only admission probability' if c['control']=='null' else 'Mismatched-key signal admission probability; not noise-only FAP' if c['control']=='wrong-key' else 'Correct accepted bit probability'}
 cells.append(entry)
paired=[]
for c in export['cells']:
 if c['control']!='wrong-key':continue
 sourcecell=c['name'].replace('-wrong-key','-signal');matches=collections.Counter();vectors_same=0
 for remote,row in allrows.items():
  if '/'+c['name']+'/' not in remote:continue
  partner=remote.replace('/'+c['name']+'/', '/'+sourcecell+'/');s=allrows[partner];m=manifests[remote];p=manifests[partner];vectors_same+=m['key_seed']==p['key_seed'] and m['random_seed_roots']==p['random_seed_roots']
  for filename in ['received_energy.f64','intervals.u64','valid_rows.u64','gram.c128','observation.c128']:matches[filename]+=row['input_hashes'][filename]==s['input_hashes'][filename]
 assert vectors_same==32
 paired.append({'wrong_key_cell':c['name'],'paired_signal_cell':sourcecell,'pairs':32,'same_six_root_vectors':vectors_same,'identical_input_receipt_hash_count':dict(matches),'interpretation':'Same source/channel/noise roots and observed total energies/calendar, changed public basis/projections. Hash equality of energies alone does not prove full raw I/Q identity.'})
result={'schema':'triad-selective-followup-confidence-v1','state':'independent_recount_passed','scientific_qualification':False,'reproducer':{'path':str(pathlib.Path(__file__).resolve().relative_to(REPO)),'sha256':sha(pathlib.Path(__file__)),'command':'PYTHONDONTWRITEBYTECODE=1 OMP_NUM_THREADS=2 OPENBLAS_NUM_THREADS=2 MKL_NUM_THREADS=2 /tmp/triad-weak-env/bin/python docs/validation-data/triad-v3/2026-11-02-selective/reproduction/followup_confidence.py'},'runtime':sys.executable,'frozen_plan_sha256':{'export':sha(ep),'score':sha(sp)},'accepted_auditor_sha256':sha(folder/'audit_holdout.py'),'runtime_binding_audit':{'path':str(audit_path),'sha256':sha(audit_path),'integrity_complete':audit['integrity_complete'],'raw_array_bytes_rehashed':audit['evidence']['verify_raw_arrays'],'scope':'Accepted full runtime/source/manifest/score binding audit; this reviewer additionally checked all saved score arrays and 1628 actual manifest hash/seed/embedded command bindings. Omitted raw arrays were not independently rehashed here.'},'generation_driver_sha256':sha(pathlib.Path('tools/triad-lowrate-campaign.py')),'receiver_source_sha256':next(iter(source_hashes)),'receiver_dependencies':json.loads(next(iter(deps))),'independent_checks':{'reports_and_npz_hashes':len(reports),'score_rows':nscore,'actual_manifest_hash_bindings':len(manifest_hashes),'exact_manifest_embedded_receipt_and_exporter_argv_and_six_seeds':len(expected),'strict_relaxed_counts_and_per_bit_counts_match_accepted_audit':True},'manifest_identity_digest_sha256':hashlib.sha256(json.dumps(manifest_hashes,separators=(',',':')).encode()).hexdigest(),'reports':reports,'cells':cells,'admitted_control_paths':admitted,'wrong_key_pair_identity_checks':paired,'key_control_implementation':{'source':'tools/triad-long.cpp','source_sha256':sha(pathlib.Path('tools/triad-long.cpp')),'wrong_key_law':'txkey=key_for(key_seed,0); rxkey=key_for(key_seed XOR 0x6a09e667f3bcc909,0)','scope':'Source-inspected branch plus exact command/control/seed receipts; not a fresh RF or raw I/Q replay.'},'limitations':['Every lower and upper endpoint is separately pointwise one-sided 95%; together they are not a joint 95% confidence interval. No simultaneous guarantee across cells, gates, targets or adaptive development selections.','Bounds describe the equal-bit-population mean with independent iid Bernoulli outcomes within each bit stratum; p0 may differ from p1. Distinct pseudorandom roots support the simulation design, not real-world or PRNG independence proof.','All-success equal-stratum lower and all-failure upper endpoints use AM-GM; other endpoints average per-bit one-sided 97.5% Clopper-Pearson bounds (Bonferroni). No pooled common-p binomial assumption is made.','Followup points were selected prospectively using earlier results, and these new indices are disjoint. This does not make a comparison to differently seeded rate/CN0 populations a paired intervention.','Wrong-key signal admissions are material key-discrimination failures despite zero null admissions. They do not by themselves contradict the Gaussian noise-only analytical gate, and prevent a generic successful reception/rejection claim for these slower-rate settings.','Null/wrong-key controls share the first32 source/root cases with their signal cell. They are distinct control distributions, not additional independent signal paths, independent exposure duration or empirical daily false-admission proof.','Metadata-only raw verification status remains explicit; archive collector/full raw-array integrity audit is separate.']}
with out.open('w') as stream:json.dump(result,stream,indent=2,sort_keys=True,allow_nan=False);stream.write('\n')
print('OUTPUT',out,'SHA256',sha(out))
for c in cells:
 print(c['cell']['name'],[(g,c['gates'][g]['counts'],c['gates'][g]['balanced_probability_pointwise_bounds']['lower_one_sided_95'],c['gates'][g]['balanced_probability_pointwise_bounds']['upper_one_sided_95'],c['gates'][g]['correct_reception_target_assessment']) for g in ['strict','relaxed']])
