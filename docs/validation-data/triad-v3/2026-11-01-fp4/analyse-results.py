#!/usr/bin/env python3
"""Summarize the local exhaustive-FP4 study without pooling correlated trials.

Reads campaign index.json files and each capture's score NPZ/metadata. All inputs
remain immutable. Outputs JSON/CSV and empirical screening plots only.
"""
from __future__ import annotations
import argparse, csv, json, math, pathlib, re, statistics
from collections import defaultdict
import numpy as np
from scipy.stats import beta, kendalltau
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

RATE = 1.197 / 3600.0
GATES = ('default_99pct_1e-4_day', 'relaxed_90pct_1pct_12h')


def ci(k, n, alpha=.05):
    if n <= 0: return [None, None]
    return [0.0 if k == 0 else float(beta.ppf(alpha/2, k, n-k+1)),
            1.0 if k == n else float(beta.ppf(1-alpha/2, k+1, n-k))]


def ci_one_sided(k, n, alpha=.05):
    """Exact one-sided 95% lower/upper binomial bounds (Clopper-Pearson)."""
    if n <= 0: return [None, None]
    lower=0.0 if k == 0 else float(beta.ppf(alpha, k, n-k+1))
    upper=1.0 if k == n else float(beta.ppf(1-alpha, k+1, n-k))
    return [lower,upper]


def load_indexes(root):
    for p in sorted(root.rglob('index.json')):
        try: d=json.loads(p.read_text())
        except Exception: continue
        if isinstance(d,dict) and isinstance(d.get('captures'),list): yield p,d


def family(path, data):
    name=path.parent.name.lower()
    if 'holdout' in name or data.get('stage','').lower().startswith('holdout'): return 'holdout'
    if 'diagnostic' in name or data.get('stage')=='diagnostic': return 'diagnostic'
    if 'performance' in name or data.get('stage')=='performance': return 'performance'
    return 'development'


def parse_cn0(meta, input_name):
    x=meta.get('sampled_manifest',{}).get('cn0_db_hz')
    if x is None:
        # Case names with "cn-12" use a separator dash followed by a
        # nonnegative magnitude (thus -12); compact "cn12" names mean +12.
        m=re.search(r'(?:^|-)cn-([0-9]+)(?:-|$)',input_name)
        if m: x=-int(m.group(1))
        else:
            m=re.search(r'(?:^|-)cn([0-9]+)(?:-|$)',input_name)
            if m: x=int(m.group(1))
    return None if x is None else float(x)


def deduplicate_physical_inputs(records, label):
    """Collapse scan/copy rows to physical trials; reject conflicting repeats."""
    by_input={}
    for x in records:
        signature=(x['seed'],tuple(sorted(x['seed_roots'].items())),x['control'],x['cn0_db_hz'],x['channel'],x['duration_seconds'],x['outcome'],x['correct'],x['miss'],x['wrong'],x['admitted'],x['threshold'])
        prior=by_input.get(x['input'])
        if prior is not None and prior['_signature']!=signature:
            raise ValueError(f'inconsistent repeated physical input {label}/{x["input"]}')
        by_input[x['input']]={'row':x,'_signature':signature}
    use=[v['row'] for v in by_input.values()]
    roots=[str(x['seed']) for x in use if x['seed'] is not None]
    if len(roots)!=len(set(roots)):
        raise ValueError(f'duplicate seed root among physical inputs in {label}')
    return use


def outcome(dec, control=None):
    control=control or dec.get('control')
    if control in ('noise','noise_only','absent','null') or dec.get('noise_admitted') is not None:
        admitted=dec.get('noise_admitted')
        if admitted is None: admitted=dec.get('admitted')
        return ('noise_admission' if admitted else 'noise_reject')
    if control in ('wrong-key','wrong_key') and dec.get('wrong_key_admitted') is not None:
        return 'wrong' if dec.get('wrong_key_admitted') else 'wrong_key_reject'
    ca=int(dec.get('correct_accepted',0)); wa=int(dec.get('wrong_accepted',0)); miss=int(dec.get('missed',0))
    if ca: return 'correct'
    if wa: return 'wrong'
    if miss or not dec.get('admitted',0): return 'miss'
    return 'other_admit'


def score_info(meta_path):
    try:
        meta=json.loads(meta_path.read_text()); sf=meta.get('score_file')
        if not sf: return None
        with np.load(meta_path.parent/sf) as z:
            if 'score_0' not in z: return None
            a=np.asarray(z['score_0'],dtype=np.float64)
        if a.ndim != 4: return None
        # [capture, CFO, candidate, lag]; candidate scores are the best legal
        # CFO/lag score. Rank/margin therefore refer to the exhaustive candidate bank.
        cs=np.max(a[0],axis=(0,2))
        ranks=np.argsort(np.argsort(-cs,kind='stable'),kind='stable')
        margin=float(np.partition(cs,-1)[-1]-np.partition(cs,-2)[-2]) if len(cs)>1 else None
        candidates=meta.get('sampled_manifest',{}).get('candidates',[])
        truth=int(meta.get('sampled_manifest',{}).get('signal_simulation_truth',
                 meta.get('sampled_manifest',{}).get('signal_bit_simulation_truth',0)))
        truth_indices=[i for i,c in enumerate(candidates) if c.get('epoch_offset_seconds',0)==0 and c.get('bit')==truth]
        signal_present=(meta.get('sampled_manifest',{}).get('signal_enabled') is True and
                        meta.get('sampled_manifest',{}).get('control','signal')=='signal')
        truth_score=float(max(cs[i] for i in truth_indices)) if signal_present and truth_indices else None
        candidate_ids=[{'index':i,'bit':c.get('bit'),'epoch_offset_seconds':c.get('epoch_offset_seconds'),'frequency_hz':c.get('frequency_hz')} for i,c in enumerate(candidates)]
        return {'scores':cs.tolist(),'ranks':ranks.tolist(),'candidate_ids':candidate_ids,'margin':margin,'truth_score':truth_score,'truth_score_interpretation':'best CFO/lag/channel candidate with true bit and epoch offset 0; not asserted to be the physical true candidate' if truth_score is not None else None,'top':int(np.argmax(cs))}
    except (OSError,ValueError,KeyError,TypeError): return None


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--results-root',type=pathlib.Path,default=pathlib.Path('local-evidence/triad-fp4-20261101/results'))
    ap.add_argument('--out-dir',type=pathlib.Path,default=pathlib.Path(__file__).parent/'analysis')
    ap.add_argument('--rate-usd-hour',type=float,default=1.197)
    args=ap.parse_args(); rate=args.rate_usd_hour/3600
    args.out_dir.mkdir(parents=True,exist_ok=True)
    rows=[]; policy_scores={}; raw_timing=[]; source_indexes=[]; failed_captures=[]
    for ip,idx in load_indexes(args.results_root):
        split=family(ip,idx); source_indexes.append({'path':str(ip),'split':split,'stage':idx.get('stage'),'phase_states':idx.get('phase_states'),'captures':len(idx['captures']),'completed':sum(c.get('status')=='completed' for c in idx['captures']),'failed':sum(c.get('status')!='completed' for c in idx['captures'])})
        for cap in idx['captures']:
            policy=cap.get('policy','unknown'); inp=cap.get('input','unknown'); polkey=(split,inp,policy)
            if cap.get('status')!='completed': failed_captures.append({'split':split,'index':str(ip),'input':inp,'policy':policy,'status':cap.get('status'),'error':cap.get('error')})
            outpath=None
            for i,x in enumerate(cap.get('argv',[])):
                if x=='--output' and i+1<len(cap['argv']):
                    rel=pathlib.Path(cap['argv'][i+1])
                    candidates=[ip.parent/rel.name,args.results_root/rel,ip.parent/rel]
                    outpath=next((q for q in candidates if q.exists()),candidates[0]); break
            meta={}
            if outpath and outpath.exists():
                try: meta=json.loads(outpath.read_text())
                except Exception: pass
            manifest=meta.get('sampled_manifest',{})
            for d in cap.get('decisions',{}).get('final',[]):
                gate=d.get('gate','unknown'); control=manifest.get('control',d.get('control')); o=outcome(d,control)
                roots=manifest.get('random_seed_roots',{})
                seed_roots={'key':manifest.get('key_seed'),**{k:v for k,v in roots.items() if isinstance(v,int) and not isinstance(v,bool)}}
                rows.append({'split':split,'index':str(ip),'stage':idx.get('stage'),'input':inp,'policy':policy,'gate':gate,'control':control,'cn0_db_hz':parse_cn0(meta,inp),'channel':manifest.get('channel'),'duration_seconds':manifest.get('requested_active_seconds'),'seed':manifest.get('seed',seed_roots.get('channel')),'seed_roots':seed_roots,'root_id':manifest.get('holdout_root_id',manifest.get('independent_root')),'status':cap.get('status'),'outcome':o,'admitted':d.get('admitted'),'correct':int(o=='correct'),'miss':int(o=='miss'),'wrong':int(o=='wrong'),'noise_admission':int(o=='noise_admission'),'threshold':d.get('threshold'),'resident_seconds':(cap.get('scans') or [{}])[min(d.get('timing_scan',0),len(cap.get('scans') or [{}])-1)].get('complete_resident_seconds') if cap.get('scans') else None,'driver_call_wall_seconds':cap.get('driver_call_wall_seconds'),'copies':cap.get('copies')})
            if outpath and outpath.exists():
                sc=score_info(outpath)
                if sc: policy_scores[polkey]=sc
                try:
                    dat=json.loads(outpath.read_text())
                    for si,s in enumerate(dat.get('scans',[])):
                        raw_timing.append({'split':split,'input':inp,'policy':policy,'timing_scan':si,'precision':s.get('precision'),'correlation_seconds':s.get('correlation_seconds'),'correlation_device_event_seconds':s.get('correlation_device_event_seconds'),'correlation_host_enqueue_seconds':s.get('correlation_host_enqueue_seconds'),'complete_resident_seconds':s.get('complete_resident_seconds'),'batch_latency_seconds':s.get('batch_latency_seconds'),'single_capture_latency_seconds':s.get('single_capture_latency_seconds'),'streaming_realtime_factor':s.get('streaming_realtime_factor'),'hypotheses_per_second':s.get('exhaustive_hypotheses_per_second'),'bank_construction_seconds':s.get('bank_construction_seconds'),'observation_batch_prepare_seconds':s.get('observation_batch_prepare_seconds'),'inference_init_seconds':s.get('inference_init_seconds'),'inference_update_seconds':s.get('inference_update_seconds'),'inference_finish_seconds':s.get('inference_finish_seconds'),'verified_strict':dat.get('strict_reference_verification')} )
                except Exception: pass
    # Exact CIs are given per capture group. Development summaries are stratified
    # by input/CN0; only independent-root holdout can be aggregated across roots.
    groups=defaultdict(list)
    for r in rows:
        if r['status']=='completed': groups[(r['split'],r['input'],r['policy'],r['gate'],r['outcome'])].append(r)
    counts=[]
    basekeys=sorted({(r['split'],r['input'],r['policy'],r['gate']) for r in rows})
    for split,inp,policy,gate in basekeys:
        rs0=[r for r in rows if (r['split'],r['input'],r['policy'],r['gate'])==(split,inp,policy,gate) and r['status']=='completed']
        # Repeated-observation batching rows (for example b1/b8 in the
        # performance stage) are timing replicates of one physical capture.
        rs=deduplicate_physical_inputs(rs0,f'{split}/{inp}/{policy}/{gate}')
        if not rs: continue
        cnt={k:sum(r['outcome']==k for r in rs) for k in ['correct','miss','wrong','wrong_key_reject','noise_admission','noise_reject','other_admit']}
        n=len(rs)
        nnoise=cnt['noise_admission']+cnt['noise_reject']
        nw=cnt['wrong']+cnt['wrong_key_reject']
        counts.append({'split':split,'input':inp,'cn0_db_hz':rs[0]['cn0_db_hz'],'policy':policy,'gate':gate,'n':n,**cnt,'correct_rate':cnt['correct']/n if n else None,'correct_rate_exact95_two_sided':ci(cnt['correct'],n),'correct_rate_exact95_one_sided':ci_one_sided(cnt['correct'],n),'miss_rate_exact95_two_sided':ci(cnt['miss'],n),'wrong_rate_exact95_two_sided':ci(cnt['wrong'],n),'wrong_key_admission_rate_exact95_two_sided':ci(cnt['wrong'],nw) if nw else None,'wrong_key_admission_rate_exact95_one_sided':ci_one_sided(cnt['wrong'],nw) if nw else None,'wrong_key_trial_denominator':nw,'noise_admission_rate_exact95_two_sided':ci(cnt['noise_admission'],nnoise) if nnoise else None,'noise_admission_rate_exact95_one_sided':ci_one_sided(cnt['noise_admission'],nnoise) if nnoise else None,'noise_trial_denominator':nnoise,'independent_unit':'capture/root only when explicitly independent; development groups not pooled'})
    paired=[]
    for (split,inp,pol),sc in policy_scores.items():
        ref=policy_scores.get((split,inp,'fp64'))
        if not ref or pol=='fp64' or len(sc['scores'])!=len(ref['scores']): continue
        ds=np.array(sc['scores'])-np.array(ref['scores'])
        rank_delta=np.array(sc['ranks'])-np.array(ref['ranks'])
        tau=kendalltau(ref['scores'],sc['scores']).statistic if len(sc['scores'])>1 else None
        paired.append({'split':split,'input':inp,'policy':pol,'candidate_order':sc['candidate_ids'],'score_delta_by_candidate':ds.tolist(),'max_abs_score_delta':float(np.max(np.abs(ds))),'mean_abs_score_delta':float(np.mean(np.abs(ds))),'rank_delta_by_candidate_vs_fp64':rank_delta.tolist(),'rank_disagreements_vs_fp64':int(sum(a!=b for a,b in zip(sc['ranks'],ref['ranks']))),'kendall_tau_candidate_scores_vs_fp64':None if tau is None else float(tau),'top_candidate_matches_fp64':sc['top']==ref['top'],'candidate_margin_delta':None if sc['margin'] is None or ref['margin'] is None else sc['margin']-ref['margin'],'truth_score_delta':None if sc['truth_score'] is None or ref['truth_score'] is None else sc['truth_score']-ref['truth_score'],'truth_score_interpretation':sc['truth_score_interpretation']})
    diagnostic_pairs=[]
    for (split,inp,pol),sc in policy_scores.items():
        if split!='diagnostic' or pol=='strict': continue
        ref=policy_scores.get(('diagnostic',inp,'strict'))
        if not ref or len(sc['scores'])!=len(ref['scores']): continue
        ds=np.array(sc['scores'])-np.array(ref['scores']); rd=np.array(sc['ranks'])-np.array(ref['ranks'])
        tau=kendalltau(ref['scores'],sc['scores']).statistic if len(sc['scores'])>1 else None
        diagnostic_pairs.append({'split':split,'input':inp,'policy':pol,'reference_policy':'strict diagnostic FP64 product of FP32-staged inputs','reference_is_original_complex128':False,'candidate_order':sc['candidate_ids'],'score_delta_by_candidate':ds.tolist(),'max_abs_score_delta':float(np.max(np.abs(ds))),'rank_delta_by_candidate_vs_reference':rd.tolist(),'rank_disagreements_vs_reference':int(sum(a!=b for a,b in zip(sc['ranks'],ref['ranks']))),'kendall_tau_candidate_scores_vs_reference':None if tau is None else float(tau),'top_candidate_matches_reference':sc['top']==ref['top'],'candidate_margin_delta':None if sc['margin'] is None or ref['margin'] is None else sc['margin']-ref['margin'],'truth_score_delta':None if sc['truth_score'] is None or ref['truth_score'] is None else sc['truth_score']-ref['truth_score'],'truth_score_interpretation':sc['truth_score_interpretation']})
    # Holdout-only pooled aggregate checks explicitly distinct roots/seeds.
    holdout=[]
    def condition_key(r):
        ck=r['channel'] if r['channel'] is not None else f'<unknown-channel:{r["input"]}>'
        dk=r['duration_seconds'] if r['duration_seconds'] is not None else f'<unknown-duration:{r["input"]}>'
        return (r['policy'],r['gate'],r['input'].split('-cn',1)[0],r['cn0_db_hz'],ck,dk,r['control'])
    hold_keys={condition_key(r) for r in rows if r['split']=='holdout'}
    for policy,gate,group,cn0,channel,duration,control in sorted(hold_keys,key=lambda z:tuple('' if v is None else str(v) for v in z)):
        rs0=[r for r in rows if r['split']=='holdout' and condition_key(r)==(policy,gate,group,cn0,channel,duration,control) and r['status']=='completed']
        rs=deduplicate_physical_inputs(rs0,f'holdout/{policy}/{gate}/{group}/{cn0}/{channel}/{duration}/{control}')
        units=[r['root_id'] or r['seed'] for r in rs]
        distinct=sorted(set(str(x) for x in units if x is not None))
        domains=defaultdict(set)
        for r in rs:
            for name,value in r['seed_roots'].items(): domains[name].add(str(value))
        all_roots=[str(v) for r in rs for v in r['seed_roots'].values()]
        cnt={k:sum(r['outcome']==k for r in rs) for k in ['correct','miss','wrong','wrong_key_reject','noise_admission','noise_reject']}; n=len(rs)
        nw=cnt['wrong']+cnt['wrong_key_reject']; nn=cnt['noise_admission']+cnt['noise_reject']
        holdout.append({'policy':policy,'gate':gate,'group':group,'cn0_db_hz':cn0,'channel':channel if not str(channel).startswith('<unknown-channel:') else None,'channel_group_key':channel,'duration_seconds':duration if not str(duration).startswith('<unknown-duration:') else None,'duration_group_key':duration,'control':control,'n':n,'distinct_declared_roots_or_seeds':len(distinct),'roots_or_seeds':distinct,'seed_domain_unique_counts':{k:len(v) for k,v in domains.items()},'all_seed_domain_values_unique':len(all_roots)==len(set(all_roots)) if all_roots else None,'counts':cnt,'correct_rate':cnt['correct']/n if n else None,'correct_exact95_two_sided':ci(cnt['correct'],n) if n else None,'correct_exact95_one_sided':ci_one_sided(cnt['correct'],n) if n else None,'miss_exact95_two_sided':ci(cnt['miss'],n) if n else None,'wrong_exact95_two_sided':ci(cnt['wrong'],n) if n else None,'wrong_key_admission_exact95_two_sided':ci(cnt['wrong'],nw) if nw else None,'wrong_key_admission_exact95_one_sided':ci_one_sided(cnt['wrong'],nw) if nw else None,'wrong_key_trial_denominator':nw,'noise_admission_exact95_two_sided':ci(cnt['noise_admission'],nn) if nn else None,'noise_admission_exact95_one_sided':ci_one_sided(cnt['noise_admission'],nn) if nn else None,'noise_trial_denominator':nn})
    hold_inputs={r['input']:r for r in rows if r['split']=='holdout'}
    domain_values=defaultdict(set); all_domain_values=[]
    for r in hold_inputs.values():
        for name,value in r['seed_roots'].items(): domain_values[name].add(str(value)); all_domain_values.append(str(value))
    holdout_seed_audit={'distinct_input_roots':len(hold_inputs),'unique_values_by_seed_domain':{k:len(v) for k,v in domain_values.items()},'all_domain_values_globally_unique':len(all_domain_values)==len(set(all_domain_values)) if all_domain_values else None}
    paired_hold=defaultdict(dict)
    for r in rows:
        if r['split']=='holdout' and r['policy'] in ('fp64','nvfp4') and r['status']=='completed':
            paired_hold[(r['input'],r['gate'])][r['policy']]=r
    discord=defaultdict(list)
    for (inp,gate),pair in paired_hold.items():
        if set(pair)!={'fp64','nvfp4'}: continue
        ref,approx=pair['fp64'],pair['nvfp4']
        channel_key=ref['channel'] if ref['channel'] is not None else f'<unknown-channel:{inp}>'
        duration_key=ref['duration_seconds'] if ref['duration_seconds'] is not None else f'<unknown-duration:{inp}>'
        group=(inp.split('-cn',1)[0],gate,ref['cn0_db_hz'],channel_key,duration_key,ref['control'])
        group=group+(inp,)
        same_outcome=ref['outcome']==approx['outcome']
        a=ref['correct']==0 and approx['correct']==1
        b=ref['correct']==1 and approx['correct']==0
        ra=bool(ref['admitted']); aa=bool(approx['admitted'])
        discord[group].append({'outcome_discordant':not same_outcome,'approx_only_correct':a,'fp64_only_correct':b,'approx_only_admitted':aa and not ra,'fp64_only_admitted':ra and not aa,'admission_discordant':ra!=aa,'both_correct':ref['correct']==1 and approx['correct']==1,'both_miss':ref['miss']==1 and approx['miss']==1,'fp64_outcome':ref['outcome'],'nvfp4_outcome':approx['outcome']})
    paired_holdout=[]
    # Group input-level paired outcomes by fixed test condition, requiring a
    # unique independent seed per input and not mixing SNR/channel/duration/control.
    cond=defaultdict(list)
    for key,arr in discord.items(): cond[key[:-1]].extend(arr)
    for (group,gate,cn,channel,duration,control),arr in sorted(cond.items(),key=lambda z:tuple('' if v is None else str(v) for v in z[0])):
        n=len(arr); a=sum(x['approx_only_correct'] for x in arr); b=sum(x['fp64_only_correct'] for x in arr)
        # Bonferroni 97.5% exact intervals on each discordant direction yield
        # a conservative exact 95% interval for paired risk difference a/n-b/n.
        al,ah=ci(a,n,alpha=.025); bl,bh=ci(b,n,alpha=.025)
        aa=sum(x['approx_only_admitted'] for x in arr); ba=sum(x['fp64_only_admitted'] for x in arr)
        aal,aah=ci(aa,n,alpha=.025); bal,bah=ci(ba,n,alpha=.025)
        paired_holdout.append({'group':group,'gate':gate,'cn0_db_hz':cn,'channel':channel,'duration_seconds':duration,'control':control,'n_pairs':n,'outcome_discordances':sum(x['outcome_discordant'] for x in arr),'admission_discordances':sum(x['admission_discordant'] for x in arr),'nvfp4_only_admissions':aa,'fp64_only_admissions':ba,'approx_only_correct':a,'fp64_only_correct':b,'both_correct':sum(x['both_correct'] for x in arr),'both_miss':sum(x['both_miss'] for x in arr),'paired_correct_rate_delta_nvfp4_minus_fp64':(a-b)/n if n else None,'paired_delta_exact95_conservative':[(al-bh),(ah-bl)] if n else None,'paired_admission_rate_delta_nvfp4_minus_fp64':(aa-ba)/n if n else None,'paired_admission_delta_exact95_conservative':[(aal-bah),(aah-bal)] if n else None,'independent_capture_roots':n})
    hi=next((x for x in source_indexes if x['split']=='holdout'),{})
    holdout_campaign_audit={'expected_capture_pairs':753,'expected_policy_captures':1506,'observed_policy_captures':hi.get('captures',0),'completed_policy_captures':hi.get('completed',0),'failed_policy_captures':hi.get('failed',0),'distinct_input_cases':len(hold_inputs),'paired_input_gate_rows':len(paired_hold),'phase_states':hi.get('phase_states'),'status_matches_expected':hi.get('captures')==1506 and hi.get('completed')==1506 and len(hold_inputs)==753 and len(paired_hold)==1506}
    sensitivity_budgets={'budgets_db':[0.1,0.25,0.5,1.0],'status':'unresolved at all listed budgets','reason':'Holdout C/N0 points are discrete and separated by at least 1 dB for the short-signal curve; exact binomial and paired confidence bounds remain too wide to identify these loss thresholds. No interpolation or tiny-tail extrapolation is made.','320_of_320_two_sided_95_lower':ci(320,320)[0],'320_of_320_one_sided_95_lower':ci_one_sided(320,320)[0],'qualification_note':'At the -8 dB-Hz target point, 320/320 has a one-sided lower bound above 99%, but this single-point reception result does not qualify the default gate jointly because the noise-only bound is far above 1e-4/day and no full operational family-tail calibration was established.'}
    sens=defaultdict(list)
    for r in rows:
        # Only actual signal-screen strata contribute to sensitivity inference;
        # timing copies and diagnostic/performance rows are never trials.
        if r['status']!='completed' or r['control'] not in ('signal',None) or r['cn0_db_hz'] is None: continue
        if r['split']=='development':
            if not r['input'].startswith('boundary-cn-'): continue
        elif r['split']=='holdout':
            if not r['input'].startswith(('h1-short-','h1-target-')): continue
        else: continue
        family_name=('boundary' if r['split']=='development' else r['input'].split('-cn',1)[0])
        channel_key=r['channel'] if r['channel'] is not None else f'<unknown-channel:{r["input"]}>'
        duration_key=r['duration_seconds'] if r['duration_seconds'] is not None else f'<unknown-duration:{r["input"]}>'
        sens[(r['split'],family_name,r['policy'],r['gate'],r['cn0_db_hz'],channel_key,duration_key,r['control'])].append(r)
    sensitivity=[]
    for (sp,fam,pol,gate,cn,channel,duration,control),grp in sorted(sens.items(),key=lambda z:tuple('' if v is None else str(v) for v in z[0])):
        use=deduplicate_physical_inputs(grp,f'sensitivity/{sp}/{fam}/{pol}/{gate}/{cn}/{channel}/{duration}/{control}')
        seed_roots=[str(x['seed']) for x in use if x['seed'] is not None]
        if len(seed_roots)!=len(set(seed_roots)):
            raise ValueError(f"duplicate seed root in sensitivity stratum {sp}/{fam}/{pol}/{gate}/{cn}/{channel}/{duration}/{control}")
        n=len(use); k=sum(x['correct'] for x in use)
        sensitivity.append({'split':sp,'family':fam,'policy':pol,'gate':gate,'cn0_db_hz':cn,'channel':channel if not str(channel).startswith('<unknown-channel:') else None,'channel_group_key':channel,'duration_seconds':duration if not str(duration).startswith('<unknown-duration:') else None,'duration_group_key':duration,'control':control,'n_captures':n,'distinct_seed_roots':len(seed_roots),'correct':k,'miss':sum(x['miss'] for x in use),'wrong':sum(x['wrong'] for x in use),'correct_rate':k/n if n else None,'correct_exact95_two_sided':ci(k,n),'correct_exact95_one_sided':ci_one_sided(k,n),'independent_root_group':'one row per physical input; fixed CN0/channel/duration/control; no performance or diagnostic timing rows'})
    perf_points=[]
    for s in sensitivity:
        if s['gate']!=GATES[0]: continue
        subset=[x for x in rows if x['split']==s['split'] and x['policy']==s['policy'] and x['gate']==s['gate'] and x['cn0_db_hz']==s['cn0_db_hz'] and x['channel']==s['channel'] and x['duration_seconds']==s['duration_seconds'] and x['control']==s['control'] and x['status']=='completed' and x['resident_seconds'] is not None]
        prefix='boundary-cn-' if s['split']=='development' else ('h1-short-' if s['family'].startswith('h1-short') else 'h1-target-')
        subset=[x for x in subset if x['input'].startswith(prefix)]
        subset=[x for x in subset if (x['channel'] if x['channel'] is not None else f'<unknown-channel:{x["input"]}>')==s['channel_group_key'] and (x['duration_seconds'] if x['duration_seconds'] is not None else f'<unknown-duration:{x["input"]}>')==s['duration_group_key']]
        if subset:
            sec=sum(x['resident_seconds'] for x in subset); ncap=len(subset)
            perf_points.append({**s,'mean_resident_seconds_per_capture':sec/ncap,'captures_per_second':ncap/sec if sec else None,'cost_usd_per_correct_capture':rate*sec/s['correct'] if s['correct'] else None,'timing_scope':'complete resident campaign timing as recorded'})
    # Plot empirical detection only, no interpolation between measured CN0 points.
    series_keys=sorted({(r['split'],r['family'],r['policy']) for r in sensitivity if r['split'] in ('development','holdout') and r['gate']==GATES[0]})
    palette=plt.rcParams['axes.prop_cycle'].by_key()['color']; markers=['o','s','^','D','v','P','X']
    styles={k:(palette[i%len(palette)],markers[(i//len(palette))%len(markers)]) for i,k in enumerate(series_keys)}
    offsets=[(3,4),(3,11),(3,-8),(3,-15),(3,18),(3,-22),(3,25),(3,-29)]
    fig,ax=plt.subplots(figsize=(11,5.5)); seen_labels=set()
    for r in sensitivity:
        if r['split'] not in ('development','holdout') or r['gate']!=GATES[0]: continue
        key=(r['split'],r['family'],r['policy']); color,marker=styles[key]
        lo,hi=r['correct_exact95_two_sided']; label='/'.join(key) if key not in seen_labels else None
        seen_labels.add(key)
        ax.errorbar([r['cn0_db_hz']],[r['correct_rate']],yerr=[[r['correct_rate']-lo],[hi-r['correct_rate']]],marker=marker,color=color,ecolor=color,linestyle='-',capsize=2,label=label)
        off=offsets[series_keys.index(key)%len(offsets)]
        ax.annotate(f"{r['cn0_db_hz']:g}",(r['cn0_db_hz'],r['correct_rate']),xytext=off,textcoords='offset points',fontsize=6,color=color)
    ax.set(xlabel='C/N0 (dB-Hz)',ylabel='Empirical correct-gated fraction',ylim=(-.03,1.03),title='Screening sensitivity (exact two-sided 95% intervals; observed points only)');ax.grid(True,alpha=.25)
    if seen_labels: ax.legend(fontsize=7,ncol=1,loc='upper left',bbox_to_anchor=(1.01,1))
    fig.tight_layout(rect=(0,0,.78,1));fig.savefig(args.out_dir/'screening-sensitivity.png',dpi=160);plt.close(fig)
    for metric,filename,xlabel,title in [
        ('captures_per_second','screening-throughput-sensitivity.png','Measured resident captures / second','Screening throughput vs observed sensitivity (no qualification)'),
        ('cost_usd_per_correct_capture','screening-cost-sensitivity.png','Observed resident compute cost per correct-gated capture (USD)','Screening cost vs observed sensitivity (not operational cost)')]:
        fig,ax=plt.subplots(figsize=(10,5.2)); groups=defaultdict(list)
        for p in perf_points:
            if p[metric] is not None: groups[(p['split'],p['family'],p['policy'])].append(p)
        for series_i,(key,vals) in enumerate(sorted(groups.items())):
            color,marker=styles.get(key,(None,'o'))
            xs=[p[metric] for p in vals]; ys=[p['correct_rate'] for p in vals]
            ax.plot(xs,ys,linestyle='none',marker=marker,color=color,label='/'.join(key))
            off=offsets[series_i%len(offsets)]
            for p,x,y in zip(vals,xs,ys): ax.annotate(f"{p['cn0_db_hz']:g}",(x,y),xytext=off,textcoords='offset points',fontsize=6,color=color)
        ax.set(xlabel=xlabel,ylabel='Empirical correct-gated fraction',ylim=(-.03,1.03),title=title);ax.grid(True,alpha=.25)
        if groups: ax.legend(fontsize=7,loc='upper left',bbox_to_anchor=(1.01,1))
        fig.tight_layout(rect=(0,0,.76,1));fig.savefig(args.out_dir/filename,dpi=160);plt.close(fig)
    # Empirical observed-point cost per correct-gated capture, based on resident time.
    fig,ax=plt.subplots(figsize=(9,5.5))
    for policy in sorted(set(r['policy'] for r in rows)):
      xs=[];ys=[]
      for s in sensitivity:
          if s['split']!='development' or s['family']!='boundary' or s['policy']!=policy or s['gate']!=GATES[0] or s['correct']==0: continue
          original=[x for x in rows if x['split']=='development' and x['input'].startswith('boundary-cn-') and x['cn0_db_hz']==s['cn0_db_hz'] and x['policy']==policy and x['gate']==s['gate'] and x['status']=='completed' and x['resident_seconds'] is not None]
          if original:
              xs.append(s['cn0_db_hz']);ys.append(sum(x['resident_seconds'] for x in original)*rate/s['correct'])
      if xs: ax.plot(xs,ys,linestyle='none',marker='o',label=policy)
    ax.set(xlabel='C/N0 (dB-Hz)',ylabel='Observed resident compute cost / correct-gated capture (USD)',title='Development-screening cost (not validated operational cost)');ax.grid(True,alpha=.25)
    if ax.get_legend_handles_labels()[0]: ax.legend()
    fig.tight_layout();fig.savefig(args.out_dir/'screening-cost.png',dpi=160);plt.close(fig)
    result={'warning':'Screening only; no tiny-tail or operational cost qualification. Do not interpolate sensitivity-loss budgets (0.1/0.25/0.5/1 dB); current resolution is insufficient. Development sweeps are stratified by fixed SNR/scenario and never pooled across SNR as independent trials. Exact intervals are nominal binomial intervals, valid only for declared independent units.','source_indexes':source_indexes,'failed_captures':failed_captures,'rate_usd_per_hour':args.rate_usd_hour,'counts_per_input_policy_gate':counts,'sensitivity_by_independent_group':sensitivity,'throughput_and_cost_screening_points':perf_points,'paired_scores_vs_fp64':paired,'diagnostic_paired_scores_vs_strict_staging_fp32':diagnostic_pairs,'holdout_summary_separate':holdout,'holdout_paired_policy_comparison':paired_holdout,'holdout_seed_domain_audit':holdout_seed_audit,'holdout_campaign_audit':holdout_campaign_audit,'sensitivity_loss_budgets':sensitivity_budgets,'timings_by_capture_scan':raw_timing,'separate_timing_benchmarks':timing_benchmarks(args.results_root)}
    (args.out_dir/'analysis.json').write_text(json.dumps(result,indent=2)+'\n')
    with (args.out_dir/'counts.csv').open('w',newline='') as f:
        if counts:
            w=csv.DictWriter(f,fieldnames=list(counts[0]));w.writeheader();w.writerows(counts)
    print(json.dumps({'indexes':len(source_indexes),'capture_rows':len(rows),'summary_rows':len(counts),'paired_scores':len(paired),'holdout_groups':len(holdout),'output':str(args.out_dir)},indent=2))


def _curves(rows, policy=None,gate=None,split=None):
    d=defaultdict(list)
    for r in rows:
        if r['status']!='completed' or (policy and r['policy']!=policy) or (gate and r['gate']!=gate) or (split and r['split']!=split): continue
        # Retain each input as its independent stratum; merge only same CN0 within that input.
        d[(r['split'],r['gate'],r['policy'],r['input'],r['cn0_db_hz'])].append(r)
    grouped=defaultdict(list)
    for (sp,ga,po,inp,cn),arr in d.items(): grouped[(sp,ga,po)].append((cn,arr))
    for key,arr in grouped.items(): yield key,sorted(arr,key=lambda x:(x[0] is None,x[0]))


def timing_benchmarks(root):
    result=[]
    for filename in ['large-fp32-v1.json','large-native-fp4-v1.json','sweep-v1.json','sweep-v2.json','probe-mxfp4-v2.json','probe-nvfp4-v2.json','large-parent-v1.json','nsys-large-parent.json']:
        p=root/filename
        if not p.exists(): continue
        d=json.loads(p.read_text())
        if 'large-' in filename and 'parent' not in filename:
            result.append({'file':filename,'scope':'large receiver; per scan resident timing','precision':d.get('precision'),'scans':[{'precision':s.get('precision'),'implementation':s.get('inference_implementation'),'resident_s':s.get('complete_resident_seconds'),'batch_latency_s':s.get('batch_latency_seconds'),'hypotheses_per_s':s.get('exhaustive_hypotheses_per_second'),'bank_s':s.get('bank_construction_seconds'),'prepare_s':s.get('observation_batch_prepare_seconds'),'transfer_wall_s':s.get('observation_transfer_wall_seconds')} for s in d.get('scans',[])]})
        elif 'sweep-' in filename:
            cases=[]
            for c in d.get('cases',[]):
                h=c.get('host_paced',{}); g=c.get('product_graph',{})
                cases.append({'shape_mnk':c.get('shape_mnk'),'format':c.get('format'),'tile':c.get('tile'),'cold_pack_compile_product_ms':c.get('cold_native_pack_compile_product_ms'),'first_strict_use_ms':c.get('first_strict_use_ms'),'synthetic_host_paced':{'pack_a':h.get('pack_a'),'pack_b':h.get('pack_b'),'pack_both_product':h.get('pack_both_product'),'native_product':h.get('native_product'),'strict_fp32_product':h.get('strict_fp32_product')},'cached_product':{'cached_a_pack_b_product':h.get('cached_a_pack_b_product')},'native_graph_product':g.get('native_fp4'),'strict_graph_product':g.get('strict_fp32'),'scope_note':'synthetic engineering matrices, separately timed packing/cached/native/strict paths; no receiver validation'})
            result.append({'file':filename,'scope':d.get('timing_scope'),'cases':cases})
        else: result.append({'file':filename,'scope':'microbenchmark / profiler evidence; preserve full raw JSON separately','keys':list(d) if isinstance(d,dict) else 'array'})
    return result

if __name__=='__main__': main()
