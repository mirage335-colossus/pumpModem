import json,math,hashlib,gzip,collections,time
from pathlib import Path
from scipy.stats import beta
R=Path(__file__).resolve().parent
read=lambda p:json.loads(p.read_text())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
errors=[]
def check(v,label):
 if not v:errors.append(label)
def close(a,b,label):check(math.isclose(a,b,rel_tol=1e-10,abs_tol=1e-9),label)
def lse(v):
 m=max(v);return m+math.log(sum(math.exp(x-m) for x in v))
def interval(k,n,a=.05):return [0 if k==0 else float(beta.ppf(a/2,k,n-k+1)),1 if k==n else float(beta.ppf(1-a/2,k+1,n-k))]
t0=time.monotonic();gen=read(R/'hf-free-ic7100-independent/protocol.json');cal=read(R/'coverage-calibration/cutoff.json');cp=read(R/'coverage-calibration/protocol.json');p=read(R/'independent-reception/protocol.json');s=read(R/'independent-reception/summary.json');rr=read(R/'independent-reception/rows.json')
check(sha(R/'independent-reception/protocol.json')==s['protocol_sha256'],'protocol hash')
check(sha(R/'coverage-calibration/protocol.json')==cal['calibration_protocol_sha256'],'cal protocol hash')
check(sha(R/'coverage-calibration/profiles.jsonl.gz')==cal['profiles_sha256'],'profiles hash')
check(cp['input_protocol_sha256']==sha(R/'hf-free-ic7100-independent/protocol.json'),'native protocol binding')
schedule=gen['trial_schedule'];phases=collections.Counter(t['stage'] for t in schedule);seeds=[t['seed'] for t in schedule]
check(len(seeds)==len(set(seeds))==202,'202 distinct native seeds')
check(len([t for t in schedule if t['stage']=='frequency-calibration'])==99,'99 frequency training')
check(all(not t['data'] for t in schedule if t['phase']!='signal'),'DATA absent in all calibration/null')
check(len(cal['receipts'])==cal['count']==99 and cal['rank']==math.ceil(100*(1-cal['beta']))==99,'cal rank')
check(cal['empirical_quantile']==sorted(x['nonconformity'] for x in cal['receipts'])[98]==0.,'cal quantile')
check(cal['config']['competitive_score_drop']==max(18.,cal['empirical_quantile']),'conservative cutoff')
bank=p['public_search']['frequency_hz'];calcoverage=0;keys=set();phasenatives=collections.defaultdict(set)
with gzip.open(R/'coverage-calibration/profiles.jsonl.gz','rt') as stream:
 for index,line in enumerate(stream):
  z=json.loads(line);receipt=z['receipt'];est=z['estimator'];check(receipt==cal['receipts'][index],'cal profile receipt')
  folder=R/'hf-free-ic7100-independent'/receipt['id'];m=read(folder/'manifest.json');truth=read(folder/'evaluator-truth.json');keys.add(m['key_seed']);phasenatives['frequency'].add(truth['random_seed_roots']['fixture'])
  check(not m['data_enabled'],'cal DATA absent');check(sha(folder/'manifest.json')==receipt['manifest_sha256'] and sha(folder/'evaluator-truth.json')==receipt['truth_sha256'],'cal manifest/truth hashes')
  ch=m['channel'];tx=truth['tx'];rx=truth['rx'];cfo=((tx['rf_ppm']-rx['rf_ppm'])*ch['rf_hz']+(tx['audio_ppm']-rx['audio_ppm'])*ch['nominal_conversion_if_hz'])*1e-6/(1+rx['audio_ppm']*1e-6)
  close(cfo,receipt['cfo_evaluator_only'],'cal CFO formula')
  nearest=min(bank,key=lambda f:abs(f-cfo));close(nearest,receipt['nearest_original_frequency_hz'],'cal nearest CFO')
  close(est['original_bank_nonconformity'][bank.index(nearest)],receipt['nonconformity'],'cal nonconformity')
  calcoverage+=nearest in est['selected_frequencies']
results={};coverage=0;selected=[];overlap=0
for arm,rows in rr.items():
 check(len(rows)==103,'103 reception rows');g=s['gates'][arm];check(g['count']==39 and g['alpha']==.05,'39 rank gate')
 counts=collections.Counter(dict(correct_admitted=0,wrong_admitted=0,missed=0,raw_bit_errors=0,false_admitted=0));bits={0:collections.Counter(),1:collections.Counter()}
 for i,row in enumerate(rows):
  entry=p['schedule'][i];check(row['trial']['id']==entry['id'] and row['trial']['phase']==entry['phase'],'reception schedule')
  ll=row['dynamic']['bit_log_likelihoods'];presence=lse(ll)-math.log(2);chosen=0 if ll[0]>=ll[1] else 1
  close(presence,row['dynamic']['working_presence'],'presence');check(chosen==row['dynamic']['bit'],'raw bit');check(row['static']==row['dynamic'],'shared compatibility arm is not distinct static receiver')
  check(row['procedure_id']==g['procedure_id']==s['procedure_ids'][arm],'procedure ID')
  if i<39:close(presence,g['scores']['dynamic'][i],'gate source scores')
  else:
   pv=(1+sum(v>=presence for v in g['scores']['dynamic']))/40;accept=pv<=.05;dec=row['decisions']['dynamic'];close(pv,dec['p_value'],'p-value');check(accept==dec['accepted'],'admission')
   if row['trial']['phase']=='signal':
    truthbit=row['evaluator']['source_bit'];c='missed' if not accept else 'correct_admitted' if chosen==truthbit else 'wrong_admitted';counts[c]+=1;bits[truthbit][c]+=1;counts['raw_bit_errors']+=chosen!=truthbit
   else:counts['false_admitted']+=accept
  if arm=='adaptive':
   folder=Path(entry['directory']);m=read(folder/'manifest.json');truth=read(folder/'evaluator-truth.json');keys.add(m['key_seed']);phasenatives[row['trial']['phase']].add(truth['random_seed_roots']['fixture'])
   check(sha(folder/'manifest.json')==entry['hashes']['manifest_sha256'] and sha(folder/'evaluator-truth.json')==row['evaluator']['truth_sha256'],'heldout identity')
   fa=row['frequency_assistance'];nearest=min(bank,key=lambda f:abs(f-row['evaluator']['actual_cfo_hz_after_score']));coverage+=nearest in fa['selected_frequencies'];selected.append(len(fa['selected_frequencies']));overlap+=bool(fa['overlaps_possible_data'])
   check(set(fa['selected_frequencies']).issubset(bank) and len(fa['selected_frequencies'])==fa['selected_count'],'selected bank')
   check(row['evaluator']==rr['baseline'][i]['evaluator'],'paired evaluator')
 check({k:counts[k] for k in ('correct_admitted','wrong_admitted','missed','raw_bit_errors')}==s['outcomes'][arm]['signal']['counts'],'signal count')
 check(counts['false_admitted']==s['outcomes'][arm]['null']['false_admitted'],'null count')
 bounds=[interval(bits[b]['correct_admitted'],16,.025) for b in (0,1)];balanced=[sum(x[j] for x in bounds)/2 for j in (0,1)]
 for j in (0,1):close(balanced[j],s['outcomes'][arm]['signal']['balanced_population']['correct_admitted']['two_sided'][j],'correct CI')
 results[arm]=dict(counts=dict(counts),balanced_correct_95_interval=balanced,null_95_interval=interval(counts['false_admitted'],32),null_95_upper=float(beta.ppf(.95,counts['false_admitted']+1,32-counts['false_admitted'])))
check(sum(selected)==s['candidate_work']['total_candidate_trial_evaluations']==1112,'candidate work')
check(all(phasenatives[a].isdisjoint(phasenatives[b]) for a in phasenatives for b in phasenatives if a!=b),'phase seed separation')
diag=read(R/'diagnostic-reception/summary.json');check(diag['gates']=={'adaptive':None,'baseline':None} and all(v is None for v in diag['outcomes'].values()),'diagnostic no admission qualification')
diagp=read(R/'hf-free-ic7100/protocol.json');diagkeys=[]
for folder in sorted((R/'hf-free-ic7100').glob('diagnostic-*')):
 if folder.is_dir() and (folder/'manifest.json').exists():
  dm=read(folder/'manifest.json');dt=read(folder/'evaluator-truth.json');diagkeys.append(dm['key_seed']);check(dt['random_seed_roots']['fixture'] not in set(seeds),'diagnostic seed independence')
check(set(diagkeys).isdisjoint(keys),'diagnostic key differs; phase keys intentionally shared')
sources={f:Path(f).exists() and sha(Path(f))==h for f,h in p['source_hashes'].items()}
check(sha(R/'triad_dynamic_acquire.measured.py')==p['source_hashes']['tools/triad_dynamic_acquire.py'],'measured acquisition snapshot')
output=dict(schema='triad-helper-frequency-independent-audit-v1',status='passed-with-limits' if not errors else 'failed',errors=errors,
 protocol_sha256=sha(R/'independent-reception/protocol.json'),rows_sha256=sha(R/'independent-reception/rows.json'),
 calibration=dict(count=99,DATA_present=0,rank=99,empirical_quantile=0.,frozen_cutoff=18.,nearest_original_bank_covered=calcoverage),
 separation=dict(native_seed_count=202,distinct_seeds=202,cohort_keys=sorted(keys),diagnostic_keys=sorted(set(diagkeys)),phase_seed_counts={k:len(v) for k,v in phasenatives.items()},scope='Frequency calibration, admission calibration, signals and nulls have disjoint seeds; all share key2409. Different key versus diagnostics, not across calibration/test phases.'),
 outcomes=results,coverage=dict(covered=coverage,trials=103,overlapping_possible_DATA=overlap,miss_probability_95_upper=1-.05**(1/103),selected_min=min(selected),selected_max=max(selected),selected_mean=sum(selected)/103,total=1112,original_per_trial=365),
 timing=dict(independent_103=s['timing'],two_capture_diagnostic=diag['timing'],speedup_103=s['timing']['baseline_scoring_seconds']/s['timing']['adaptive_scoring_seconds'],speedup_two=diag['timing']['baseline_scoring_seconds']/diag['timing']['adaptive_scoring_seconds'],scope='Complete receiver calls include estimation, preparation and candidate scoring; native generation excluded. Two-capture preparation amortization differs from103-capture throughput; no speedup confidence interval or sustained bound.'),
 provenance=dict(current_source_matches=sources,measured_acquire_snapshot_sha256=sha(R/'triad_dynamic_acquire.measured.py'),frequency_source_sha256=p['source_hashes']['tools/triad_dynamic_frequency.py'],read_order='Reviewed runner: both full arms scored; gates fixed; heldout decisions made; then evaluator/CFO truth opened. Training evaluator reads truth only after each fixed-policy estimator profile. No independent trusted timestamp guarantee.',poststudy_diff='Measured acquisition snapshot→current changes only returned procedure-ID hash input: default full-bank repeat lists omitted; explicit selections retained. Numerical arrays/projections/arithmetic unchanged. Source-based outer identities change; do not reuse old gates as current-source gates.'),
 limits=['Known discrete arrival0.013, nominal epoch and clock bank0; actual free-running baseband/IC7100 oscillators enabled. Not full timing/epoch/clock acquisition.',
 'Nearest original CFO-bin coverage103/103 is measured coverage, not a95% confidence demonstration of99% population coverage: one-sided miss upper2.87%. Split-conformal marginal99% statement additionally requires exchangeability and the reviewed monotone complete selection rule.',
 'Separate39-sample admission gates are marginal per-capture diagnostics; not conditional or daily false-admission guarantees. Paired differences are descriptive, not evidence of sensitivity improvement.',
 'Static rows duplicate the already-mixed dynamic score for shared-gate compatibility; not another receiver comparison.',
 'No raw-IQ rescore, no strong-AM90 reception qualification, no GPU/CPU comparison or sustained runtime qualification in this audit.'],audit_wall_seconds=time.monotonic()-t0,reproducer_sha256=sha(Path(__file__)))
(R/'audit.json').write_text(json.dumps(output,indent=2)+'\n')
print(json.dumps({k:output[k] for k in ['status','errors','separation','outcomes','coverage','audit_wall_seconds']},indent=2))
