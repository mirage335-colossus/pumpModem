#!/usr/bin/env python3
"""Receipt-bound follow-up recount; no receiver inference or physical simulation.

--receipt PLAN=RESULTS binds each original run_jobs result to its exact plan.
Raw workspace arrays may be inventory-only; only score/window NPZs are read.
The ln100 guard is primary; zero is diagnostic. Neither authenticates a key.
"""
import argparse
from collections import Counter, defaultdict
import copy
import json
import math
from pathlib import Path
import sys
import numpy as np
import analyze_campaign as a

GUARD_SHA = '8b6d245dfa36acf24c57fcf2afee61777a3c1faab226a9fc3e602701c92cd9b5'
MAIN_SHA = 'ed48a3326c1968215481336b2e2b0d4102e9ee04473cbe3c6893e33fc8967e55'
BASE_SHA = '4f74b6d8ce6d78730ed92b517303ec1d8ab5a2f2606a6e4bf1deb6212d70792a'


def guard_recount(saved, archive, record, manifest):
    """Recompute all guard algebra from retained per-window scores."""
    a.require(saved['status'] == 'complete', 'guard incomplete')
    a.require(set(archive.files) == {'window_log_likelihoods', 'd', 'contrast'}, 'guard NPZ members')
    n = saved['window_count']; w = a.finite(archive['window_log_likelihoods'], (n, 2), 'window scores')
    a.require(n == math.ceil(record['geometry']['block_count']/record['geometry']['window_blocks']), 'window count')
    d = w[:, 1]-w[:, 0]
    C = np.array([(math.log(2)-np.logaddexp(0, d)).sum(), (math.log(2)-np.logaddexp(0, -d)).sum()])
    a.near(archive['d'], d, 'window differences'); a.near(archive['contrast'], C, 'NPZ contrast')
    a.near(saved['contrast'], C, 'saved contrast'); a.near(saved['window_ll_sums'], w.sum(0), 'window sum')
    winner = int(np.argmax(record['bit_log_likelihoods'])); best = record['best_scoring_group_for_winning_bit']
    a.require(saved['winner_bit_unchanged'] == winner and saved['best_group'] == best, 'guard changed winner/group')
    difference = w.sum(0)-record['group_bit_log_likelihoods'][best]
    a.require(np.max(abs(difference)) < 1e-5, 'window/full score sum mismatch')
    a.near(saved['baseline_difference'], difference, 'saved baseline difference'); a.near(saved['sum_d'], d.sum(), 'sum d')
    a.require(saved['difference_sign_counts'] == {'positive': int((d>0).sum()), 'negative': int((d<0).sum()), 'zero': int((d==0).sum())}, 'sign counts')
    a.require(saved['labels_read_after_scoring'] == {'control': manifest['control'], 'truth': manifest['signal_bit_simulation_truth']}, 'guard labels')
    result = {}
    for label, threshold in [('ln100', math.log(100)), ('zero', 0.)]:
        entry = saved['guards'][label]; passed = bool(C[winner] >= threshold)
        a.near(entry['threshold'], threshold, 'guard threshold')
        a.require(entry['contrast_passes'] == passed, 'guard contrast decision')
        result[label] = {}
        for gate, limit in a.GATES.items():
            decision = a.summarize_decision(record['bit_log_likelihoods'], manifest['control'], manifest['signal_bit_simulation_truth'], limit)
            accepted = decision['accepted'] and passed
            a.require(entry['and_existing_gate'][gate] == accepted, 'guard AND decision')
            if not accepted:
                decision['counts'] = ({'correct_admitted': 0, 'wrong_admitted': 0, 'missed': 1} if manifest['control']=='signal' else {'false_admitted': 0, 'not_admitted': 1})
            decision['accepted'] = accepted; result[label][gate] = decision
    return {'contrast': C.tolist(), 'winner_contrast': float(C[winner]), 'decisions': result}


def capture(job, population):
    opt = a.options(job['argv']); truth = job['input_binding']['simulation_only_expected']
    return {'name': job['name'], 'input_binding': job['input_binding'], 'input_key': job['input_key'],
            'cn0_db_hz': float(opt['--cn0'][0]), 'block_samples': round(float(opt['--block-seconds'][0])*float(opt['--sample-rate'][0])/float(opt['--sample-rate-denominator'][0])),
            'population': population, 'profile': 'high'+opt['--ray-gaussian-width-hz'][0], 'depth': opt['--selective-perturbation'][0],
            'control': truth['control'], 'truth_bit': truth['source_bit'], 'requested_seconds': float(opt['--seconds'][0]),
            'seed_vector': ':'.join(str(truth[d+'_seed']) for d in a.DOMAINS)}


def summary(rows):
    groups = defaultdict(list)
    for row in rows:
        k = tuple(row[x] for x in ('population', 'profile', 'depth', 'cn0_db_hz', 'control', 'requested_seconds'))
        groups[k].append(row)
    out = []
    for key, cases in sorted(groups.items()):
        item = dict(zip(('population', 'profile', 'depth', 'cn0_db_hz', 'control', 'requested_seconds'), key)); item['expected'] = len(cases); item['views'] = {}
        for view in (('baseline',) if key[0]=='paired-duration-development' else ('baseline', 'ln100', 'zero')):
            good = [r for r in cases if view in r.get('decisions', {})]; complete = len(good)==len(cases)
            a.require(len({r['seed_vector'] for r in good}) == len(good), 'duplicate roots within stratum')
            item['views'][view] = {'verified': len(good), 'complete': complete, 'gates': {}}
            for gate in a.GATES:
                count = Counter(); bits = defaultdict(Counter); sizes = Counter()
                for r in good:
                    c = r['decisions'][view][gate]['counts']; count.update(c); bits[r['truth_bit']].update(c); sizes[r['truth_bit']]+=1
                item['views'][view]['gates'][gate] = {'counts': dict(count), 'pointwise_common_probability_cp95': {k:a.cp95(v,len(good)) for k,v in count.items()} if complete else None,
                    'per_bit': {str(b): {'n':sizes[b], 'counts':dict(c), 'pointwise_cp95':{k:a.cp95(v,sizes[b])for k,v in c.items()} if complete else None} for b,c in bits.items()}}
        out.append(item)
    return out


def merge_inventory(evidence, additional):
    """Entries already use normalized keys; textual path spellings may differ."""
    for key, entry in additional.entries.items():
        if key in evidence.entries:
            old = evidence.entries[key]
            identity = lambda value: (value['sha256'], value.get('size_bytes', value.get('size')))
            a.require(identity(old) == identity(entry), 'conflicting inventory entries')
        evidence.entries[key] = entry


def run(args):
    a.require(a.sha(a.__file__) == MAIN_SHA, 'frozen helper changed')
    a.require(a.sha(args.base_analysis) == BASE_SHA, 'accepted base analysis changed')
    base = a.read(args.base_analysis); parent, parent_hashes = a.load_plans(args.plans/'campaign-plan')
    a.require(base['state']=='complete' and base['plan_sha256']==parent_hashes, 'base not accepted complete plan')
    e = a.Evidence(args.evidence_root, args.remote_prefix)
    for path in args.inventory:
        more = a.Evidence(args.evidence_root, args.remote_prefix, path)
        merge_inventory(e, more)
    build = a.read(args.build_provenance)
    a.require(build['binary_sha256']==base['provenance']['binary_sha256'], 'binary binding differs')
    for p,h in parent['source_file_sha256'].items(): a.require(build['source_file_sha256'].get(p)==h, 'build source differs '+p)
    receipts, plan_hashes, issues = {}, {}, []
    for spec in args.receipt:
        plan, path = map(Path, spec.split('=',1)); doc = a.read(path); jobs = a.read(plan)['jobs']; plan_hashes[str(plan.resolve())]=a.sha(plan)
        binding = doc.get('plan_sha256')
        if binding is not None: a.require(binding==a.sha(plan) or isinstance(binding,dict) and a.sha(plan) in binding.values(), 'receipt plan hash differs')
        found = {}
        for r in doc['results']:
            a.require(r['name'] not in found, 'duplicate receipt'); found[r['name']]=r
        a.require(set(found) <= {j['name']for j in jobs}, 'receipt has unplanned job')
        receipts[str(plan.resolve())] = found
    def receipt(plan, job):
        rows = receipts.get(str(plan.resolve()), {})
        return a.check_receipt(job, 'score', {('score',n):r for n,r in rows.items()})
    def checked(path, expected=None):
        binding=e.check(path,expected); a.require(expected is not None or e.relative(path) in e.entries, 'artifact missing collector inventory'); return binding
    def audit_score(job, cap, export, prior=None):
        folder = a.one(export['argv'],'--output'); mp=str(Path(folder)/'manifest.json')
        checked(mp, None if prior is None else prior['input_hash_bindings']['manifest.json']['sha256']); m=a.read(e.local(mp))
        a.verify_manifest(m,export,cap)
        a.near(m['requested_active_seconds'], cap['requested_seconds'], 'requested duration'); a.require(m['actual_elapsed_seconds']>0 and m['channel_signal_energy_samples']>=0, 'invalid duration/energy')
        sf=m['source_format']; a.require(sf['version']==3 and sf['encrypted'] is True and sf['inner_rate_hz']==360 and sf['added_symbols']==1 and sf['fine_rate_hz']==60 and sf['carrier_spacing_hz']==75, 'unexpected source format')
        for name,size in m['file_sizes'].items():e.check(str(Path(folder)/name),size=size,bytes_required=False)
        report=a.one(job['argv'],'--output'); checked(report,None if prior is None else prior['report_binding']['sha256']); payload=a.read(e.local(report))
        npz=a.one(job['argv'],'--scores-output'); checked(npz,payload['scores_npz_sha256'])
        adapted=dict(job,reblock=int(a.one(job['argv'],'--reblock')))
        with np.load(e.local(npz),allow_pickle=False)as archive: result=a.analyze_score(payload,archive,m,adapted,parent['source_file_sha256']['tools/triad_group_acquire.py'])
        a.require(set(result['input_sha256'])==set(a.INPUTS),'score input set')
        for name,digest in result['input_sha256'].items():e.check(str(Path(folder)/name),digest,bytes_required=name=='manifest.json')
        row={k:cap[k]for k in ('name','population','profile','depth','cn0_db_hz','control','truth_bit','requested_seconds','seed_vector')}
        row.update(state='verified',report=report,report_sha256=a.sha(e.local(report)),manifest_sha256=a.sha(e.local(mp)),decisions={'baseline':result['views']['full_bank']},
            actual_elapsed_seconds=m['actual_elapsed_seconds'],postchannel_signal_energy_seconds=m['channel_signal_energy_samples']/m['sample_rate'],nominal_useful_allocation_seconds_lower=m['nominal_payload_energy_seconds_lower'],receiver_seconds=result['receiver_seconds'])
        return row,payload['captures'][0],m
    rows, records, exports = [], {}, {j['name']:j for j in parent['jobs']}
    base_by_name={r['name']:r for r in base['scores']}
    selected=[j for j in parent['scoring_jobs'] if j['reblock']==1 and next(c for c in parent['captures']if c['name']==j['depends_on_export'])['block_samples']==16]
    a.require(len(selected)==168,'parent guard population')
    jobs_to_score=[]
    for job in selected:
        ex=dict(exports[job['depends_on_export']]); ex['input_key']=next(c['input_key']for c in parent['captures']if c['name']==ex['name']); pop='reused-development-'+base_by_name[job['name']]['kind']; jobs_to_score.append((job,capture(ex,pop),ex,base_by_name[job['name']],None))
    for folder,master_name,ep,sp,n,pop in [('duration-plan','duration-plan.json','240-export-plan.json','240-score-plan.json',4,'paired-duration-development'),('final-validation-plan','final-validation-plan.json','export-plan.json','score-plan.json',32,'fresh-final-independent')]:
        root=args.plans/folder; master=a.read(root/master_name); ej=a.read(root/ep)['jobs']; sj=a.read(root/sp)['jobs']
        a.require(ej==master['jobs'] and sj==master['scoring_jobs'] and len(ej)==len(sj)==n,'followup component mismatch')
        a.require(master['source_file_sha256']==parent['source_file_sha256'] and master['parent_campaign']['sha256']==parent_hashes['campaign'],'followup source/parent differs')
        a.require(a.key_hash(master['source_file_sha256'])==master['source_file_inventory_sha256'],'source inventory hash')
        exmap={j['name']:j for j in ej}
        for job in sj:
            ex=exmap[job['depends_on_export']]; a.require(job['input_binding']==ex['input_binding'] and a.key_hash(ex['input_key'])==ex['input_binding']['capture_input_key_sha256'],'capture binding')
            jobs_to_score.append((job,capture(ex,pop),ex,None,(root/ep,root/sp)))
    old_roots={c['seed_vector']for _,c,_,prior,_ in jobs_to_score if prior is not None}
    fresh=[c['seed_vector']for _,c,_,_,_ in jobs_to_score if c['population']=='fresh-final-independent']
    a.require(len(fresh)==len(set(fresh))==32 and not set(fresh)&old_roots, 'fresh seed roots reused')
    for job,cap,ex,prior,plans in jobs_to_score:
        row=copy.deepcopy(cap)
        try:
            if plans: row['export_runner']=receipt(plans[0],ex); row['score_runner']=receipt(plans[1],job)
            audited,record,m=audit_score(job,cap,ex,prior); row.update(audited); records[row['report']]=(row,record,m)
        except a.Incomplete as error:row.update(state='incomplete',reason=str(error))
        except (ValueError,KeyError,TypeError,OSError)as error:row.update(state='invalid',reason=str(error))
        rows.append(row)
    guard_jobs=[]
    for plan in [args.plans/'pattern-consistency/screen-plan.json',args.plans/'pattern-consistency/remaining-plan.json']:
        guard_jobs += [(plan,j)for j in a.read(plan)['jobs']]
    a.require(len(guard_jobs)==168 and {a.one(j['argv'],'--full-score')for _,j in guard_jobs}=={a.one(j['argv'],'--output')for j in selected},'parent guard coverage')
    if args.final_guard_plan and args.final_guard_plan.is_file():
        finaljobs=a.read(args.final_guard_plan)['jobs']; pending=a.read(args.plans/'final-validation-plan/final-validation-plan.json')['pending_guard_bindings']
        a.require(len(finaljobs)==len(pending)==32,'final guard count')
        expected={v['full_score']:v for v in pending}
        a.require({a.one(j['argv'],'--full-score')for j in finaljobs}==set(expected),'final guard coverage')
        for job in finaljobs:
            want=expected[a.one(job['argv'],'--full-score')]
            for flag,key in [('--workspace','workspace'),('--output','output')]:a.require(a.one(job['argv'],flag)==want[key],'final guard output/input mismatch')
        guard_jobs += [(args.final_guard_plan,j)for j in finaljobs]
    else: issues.append('missing frozen final guard plan (32 jobs)')
    guards=[]
    for plan,job in guard_jobs:
        r={'name':job['name'],'state':'incomplete'}
        try:
            r['runner']=receipt(plan,job); full=a.one(job['argv'],'--full-score')
            if full not in records:raise a.Incomplete('full score dependency not verified')
            row,record,m=records[full]; a.require(a.one(job['argv'],'--workspace')==record['input'],'guard workspace')
            checked(full,a.one(job['argv'],'--full-score-sha256')); path=a.one(job['argv'],'--output'); checked(path); saved=a.read(e.local(path)); h=saved['hashes']
            a.require(h['driver']==GUARD_SHA and h['receiver']==parent['source_file_sha256']['tools/triad_group_acquire.py'] and h['full_score']==row['report_sha256'] and h['inputs']==record['input_sha256'],'guard hash bindings')
            a.require(saved['configuration']=={'doppler_width_hz':float(a.one(job['argv'],'--doppler-width-hz')),'window_seconds':.25,'block_samples':16,'precision':'fp32'},'guard configuration')
            wp=str(Path(path).with_suffix('.windows.npz')); checked(wp,h['per_window_npz'])
            with np.load(e.local(wp),allow_pickle=False)as archive:r.update(guard_recount(saved,archive,record,m))
            row['decisions'].update(r['decisions']); r['state']='verified'
        except a.Incomplete as error:r['reason']=str(error)
        except (ValueError,KeyError,TypeError,OSError)as error:r.update(state='invalid',reason=str(error))
        guards.append(r)
    complete=not issues and all(r['state']=='verified'for r in rows+guards)
    pairs=[]; by_name={r['name']:r for r in rows}
    for row in rows:
        if row['population']=='paired-duration-development' and row['state']=='verified':
            old=by_name[row['name'].removesuffix('-t240')];
            if old['state']!='verified':continue
            a.require(old['seed_vector']==row['seed_vector'],'duration roots differ')
            pairs.append({'parent':old['name'],'extension':row['name'],'elapsed_ratio':row['actual_elapsed_seconds']/old['actual_elapsed_seconds'],'postchannel_energy_ratio':row['postchannel_signal_energy_seconds']/old['postchannel_signal_energy_seconds'],'baseline_parent':old['decisions']['baseline'],'baseline_extension':row['decisions']['baseline'],'scope':'paired roots; no assertion of identical indexed-noise prefix or equal mean power'})
    return {'state':'complete'if complete else 'invalid'if any(r['state']=='invalid'for r in rows+guards) else 'incomplete','scientific_qualification':False,'analyzer_sha256':a.sha(__file__),'helper_sha256':MAIN_SHA,'guard_source_sha256':GUARD_SHA,'source_file_sha256':parent['source_file_sha256'],'base_analysis_sha256':BASE_SHA,'receipt_plan_sha256':plan_hashes,
        'provenance':{'build_sha256':a.sha(args.build_provenance),'inventory_sha256':[a.sha(p)for p in args.inventory],'receipts_sha256':[a.sha(Path(s.split('=',1)[1]))for s in args.receipt]},'issues':issues,'rows':rows,'guards':guards,'strata':summary(rows),'duration_pairs':pairs,
        'limits':['Only local score/window NPZs and JSON rehashed; large input arrays use collector receipts.','Reused development and paired duration outcomes are distinct from fresh final validation.','Pointwise CP95 assumes common Bernoulli probability; per-bit intervals supplied; small n does not establish99%.','ln100 is the selected uncalibrated pattern-consistency AND guard; zero is diagnostic; neither proves authentication.','Postchannel energy is measured signal_energy/Fs, not normalized elapsed duration or equal mean power.']}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for flag in ['plans','base-analysis','evidence-root','build-provenance','output']:p.add_argument('--'+flag,type=Path,required=True)
    p.add_argument('--remote-prefix',default='/workspace/triad');p.add_argument('--inventory',type=Path,action='append',default=[]);p.add_argument('--receipt',action='append',default=[]);p.add_argument('--final-guard-plan',type=Path)
    args=p.parse_args()
    try:result=run(args)
    except (a.Incomplete,ValueError,KeyError,TypeError,OSError)as error:result={'state':'incomplete'if isinstance(error,a.Incomplete)else'invalid','fatal_error':str(error),'scientific_qualification':False}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n');print(json.dumps({k:result[k]for k in ['state','fatal_error','issues']if k in result}));return 0 if result['state']=='complete'else 2

if __name__=='__main__':raise SystemExit(main())
