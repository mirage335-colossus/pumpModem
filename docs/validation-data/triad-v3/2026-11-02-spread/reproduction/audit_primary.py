import pathlib,json,math,hashlib,collections,numpy as np
import argparse,importlib.util,sys,types,datetime
from scipy.stats import beta
cli=argparse.ArgumentParser(description='Independent frozen spread-matrix metadata and saved-score audit; no receiver inference.')
cli.add_argument('--metadata-root',type=pathlib.Path,default=pathlib.Path('local-evidence/triad-spread-all-metadata'))
cli.add_argument('--plan-root',type=pathlib.Path,default=pathlib.Path('docs/validation-data/triad-v3/2026-11-02-spread/reproduction'))
cli.add_argument('--audits-root',type=pathlib.Path,default=pathlib.Path('local-evidence/triad-spread-audits'))
cli.add_argument('--output',type=pathlib.Path,default=pathlib.Path('/tmp/triad-spread-primary-review.json'))
cliargs=cli.parse_args()
sys.path.insert(0,str(pathlib.Path('tools').resolve()))
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);sys.modules[name]=m;spec.loader.exec_module(m);return m
generator=module('spread_review_generator',pathlib.Path('tools/triad-lowrate-campaign.py'))
receiver=module('spread_review_receiver',pathlib.Path('tools/triad-conditional-sweep.py'))
base=cliargs.metadata_root
plans=cliargs.plan_root
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def opts(av):
    out={}; key=None
    for token in av[1:]:
        if token.startswith('--'):
            key=token; out[key]=[]
        elif key is not None:
            out[key].append(token)
    return out

def equal(a,b):
    try: return math.isclose(float(a),float(b),rel_tol=1e-12,abs_tol=1e-12)
    except (ValueError,TypeError): return str(a)==str(b)

def seed(args,index,domain):
    value=['triad-lowrate-campaign-v1',args['split'],args['seconds'],args['rate'],args['sample_rate'],args['cn0'],index,index%2,domain]
    if args['sample_rate_denominator']!=1:
        value+=['sample-rate-denominator',args['sample_rate_denominator']]
    return int.from_bytes(hashlib.sha256(json.dumps(value,separators=(',',':')).encode()).digest()[:8],'little')

exports={j['name']:j for fn in ['holdout-export-plan.json','boundary-export-plan.json','g1-export-plan.json'] for j in json.loads((plans/fn).read_text())['jobs']}
scores={j['name']:j for fn in ['holdout-score-plan.json','boundary-score-plan.json','g1-score-plan.json'] for j in json.loads((plans/fn).read_text())['jobs']}
scores['stable-005-m60']=json.loads((plans/'stable-resume-score-plan.json').read_text())['jobs'][0]
resume=json.loads((base/'holdout/stable-005-m60-resume/campaign.json').read_text())
resume_rows={r['path_index']:r for r in resume['results']}
assert set(resume_rows)==set(range(288,300))
combined=json.loads((plans/'primary-score-plan.json').read_text())
assert len(combined['jobs'])==15
assert {j['name']:j for j in combined['jobs']}=={j['name']:j for j in scores.values()}
full_path=cliargs.audits_root/'full-analysis.json';full=json.loads(full_path.read_text())
assert full['plan_binding']['sha256']==sha(plans/'primary-score-plan.json')
assert len(full['plan_binding']['checks'])==15 and not full['plan_binding']['skipped_jobs']
assert all(not c['failed_paths'] and not c['unresolved_command_options'] for c in full['plan_binding']['checks'])
assert len(full['input_file_checks'])==24912
assert all(c['state']=='verified' and c['actual_sha256']==c['expected_sha256'] for c in full['input_file_checks'])
full_reports={r['report']:r for r in full['reports']}
full_checks={(r['input'],r['filename']):r for r in full['input_file_checks']}
cohorts=[];receipts=[];primary_raw_checks=0
def cp(k,n,alpha=.05):
    return {'successes':k,'trials':n,'fraction':k/n,'lower_one_sided_95':float(beta.ppf(alpha,k,n-k+1)) if k else 0.,'upper_one_sided_95':float(beta.ppf(1-alpha,k+1,n-k)) if k<n else 1.}
def endpoints(tally,bybit):
    k,n=tally['correct_accepted'],tally['trials']
    assert bybit[0]['trials']==bybit[1]['trials']
    if k==n:
        lower=.05**(1/n);method='balanced-all-success AM-GM bound; independent Bernoulli outcomes within each bit stratum, equal stratum weights; p0 may differ from p1'
    else:
        lower=sum(float(beta.ppf(.025,b['correct_accepted'],b['trials']-b['correct_accepted']+1)) if b['correct_accepted'] else 0. for b in bybit.values())/2
        method='average of per-bit one-sided 97.5% CP lower bounds (Bonferroni gives pointwise 95% lower bound for balanced mean)'
    return {'counts':dict(tally),'by_bit':{str(b):{'counts':dict(v),'correct_accepted_pointwise_cp':cp(v['correct_accepted'],v['trials'])} for b,v in bybit.items()},'correct_accepted_pooled_cp_assuming_identical_success_probability':cp(k,n),'balanced_mean_correct_probability_lower_95':lower,'balanced_mean_bound_method':method}
roots=('key','channel','oscillator','noise','frontend','path')
binaries=set(); generators=set(); receivers=set(); dependencies=set(); all_seeds=[]
total=0; total_counts=collections.Counter(); parity=0
old=np.load(base/'holdout-scores/stable-005-m60.scores.npz',allow_pickle=False)
old_report=json.loads((base/'holdout-scores/stable-005-m60.json').read_text())
old_paths={int(pathlib.Path(r['input']).name.split('-')[-1]):r for r in old_report['paths']}
for name,job in exports.items():
    ep=opts(job['argv']); count=int(ep['--paths'][0]); first=int(ep.get('--start-index',['0'])[0])
    parent=pathlib.Path(ep['--output'][0]); relative=pathlib.Path(*parent.parts[parent.parts.index('spread')+1:])
    campaign=json.loads((base/relative/'campaign.json').read_text())
    rows={r['path_index']:r for r in campaign['results']}; args=campaign['arguments']
    binaries.add(campaign['binary_sha256']); generators.add(campaign['source_inputs']['sha256'])
    assert campaign['source_inputs']['sha256']==hashlib.sha256(json.dumps(campaign['source_inputs']['files'],sort_keys=True).encode()).hexdigest()
    expected_generation=vars(generator.parser().parse_args(job['argv'][2:]))
    expected_generation={k:str(v) if isinstance(v,pathlib.Path) else v for k,v in expected_generation.items()}
    assert json.loads(json.dumps(expected_generation))==args,(name,'generation defaults/options mismatch')
    for flag,value in ep.items():
        if flag in ['--output','--binary','--jobs','--timeout','--term-grace']: continue
        actual=args[flag[2:].replace('-','_')]
        actual=actual if isinstance(actual,list) else [actual]
        assert len(value)==len(actual) and all(equal(a,b) for a,b in zip(value,actual)),(name,flag,value,actual)
    sp=opts(scores[name]['argv']); output=pathlib.Path(sp['--output'][0])
    report_path=pathlib.Path(*output.parts[output.parts.index('spread')+1:])
    report=json.loads((base/report_path).read_text())
    assert report['observation_mode']=='actual-noisy' and report['failed_physical_path_records']==0
    report_inputs=scores[name]['argv'][2:next(i for i,v in enumerate(scores[name]['argv']) if v.startswith('--'))]
    assert report_inputs==[r['input'] for r in report['paths']],(name,'score input order/sources')
    parsed=receiver.parse_args(scores[name]['argv'][2:])
    expected_config={k:v for k,v in vars(parsed).items() if k not in ('inputs','output','budget','tap_prior','phase_prior')}
    expected_config['coherence']='infinity' if math.isinf(parsed.coherence) else parsed.coherence
    expected_config['tap_powers']=list(parsed.tap_prior.stationary_power)
    assert expected_config==report['configuration'],(name,'receiver defaults/options mismatch')
    assert parsed.budget.diagnostics()==report['family'],(name,'receiver budget mismatch')
    assert full_reports[str(report_path)]['sha256']==sha(base/report_path) and full_reports[str(report_path)]['scores_sha256']==report['scores_sha256']
    receivers.add(report['source_sha256']); dependencies.add(json.dumps(report['dependency_sha256'],sort_keys=True))
    archive_path=base/report_path.parent/report['scores_file']
    assert sha(archive_path)==report['scores_sha256']
    archive=np.load(archive_path,allow_pickle=False)
    for flag,value in sp.items():
        if flag=='--output': continue
        key='receiver_methods' if flag=='--methods' else flag[2:].replace('-','_')
        actual=report['configuration'][key]; actual=actual if isinstance(actual,list) else [actual]
        assert len(value)==len(actual) and all(equal(a,b) for a,b in zip(value,actual)),(name,flag,value,actual)
    primary=sp['--methods'][0]; tally=collections.Counter(); relaxed=collections.Counter(); bits=collections.Counter(); seen=set()
    strict_bits={b:collections.Counter() for b in (0,1)};relaxed_bits={b:collections.Counter() for b in (0,1)}
    for record in report['paths']:
        index=int(pathlib.Path(record['input']).name.split('-')[-1])
        assert index not in seen; seen.add(index)
        row=resume_rows[index] if name=='stable-005-m60' and index>=288 else rows[index]
        location=pathlib.Path('holdout/stable-005-m60-resume') if name=='stable-005-m60' and index>=288 else relative
        manifest_path=base/location/row['directory']/'manifest.json'; manifest=json.loads(manifest_path.read_text())
        assert sha(manifest_path)==record['input_hashes']['manifest.json']
        assert manifest==row['export_manifest'] and row['returncode']==0 and row['runner_started']
        assert row['command']==manifest['command_argv']; cmd=opts(row['command'])
        reconstructed,_=generator.make_command(types.SimpleNamespace(**args),index,cmd['--output'][0])
        assert reconstructed==row['command'],(name,index,'full generated export command mismatch')
        relative_input=str(location/row['directory'])
        for filename,h in record['input_hashes'].items():
            check=full_checks[(relative_input,filename)]
            assert check['expected_sha256']==h and check['actual_sha256']==h
            primary_raw_checks+=1
        receipts.append({'cohort':name,'index':index,'source':relative_input,'manifest_sha256':sha(manifest_path),'score_key':record['conditions'][0]['methods'][primary]['score_array']})
        for domain in roots:
            expected=seed(args,index,domain)
            assert row[domain+'_seed']==expected==int(cmd['--'+domain+'-seed'][0])
            actual=manifest['key_seed'] if domain=='key' else manifest['random_seed_roots'][domain]
            assert type(actual) is int and actual==expected
            all_seeds.append(expected)
        assert row['source_bit']==index%2==manifest['signal_bit']==record['truth_bit_simulation_only']
        assert row['epoch']==1800000000+60*index==manifest['epoch']; bits[index%2]+=1
        assert equal(manifest['requested_active_seconds'],args['seconds'])
        assert equal(manifest['cn0_db_hz'],args['cn0']) and equal(manifest['source_format']['inner_rate_hz'],args['rate'])
        fs=args['sample_rate']/args['sample_rate_denominator']
        assert equal(manifest['sample_rate'],fs) and manifest['block_samples']==round(args['block_seconds']*fs)
        assert equal(manifest['noise_variance'],fs/10**(args['cn0']/10))
        assert manifest['stats_noisy'] and manifest['gaussian_projection_supported'] and manifest['source_format']['encrypted']
        assert equal(manifest['channel_model']['spread_delay_ms'],args['spread_delay_ms'])
        assert equal(manifest['channel_model']['spread_doppler_hz'],args['spread_doppler_hz'])
        assert equal(manifest['channel_model']['path_evolution']['gaussian_doppler_width_hz'],args['ray_gaussian_width_hz'])
        assert record['status']=='completed' and len(record['conditions'])==1
        condition=record['conditions'][0]
        assert condition['control']=='signal' and condition['noise_trials']==1 and condition['actual_observations']==1
        assert set(condition['methods'])=={primary}
        detail=condition['methods'][primary]; values=archive[detail['score_array']]
        assert values.shape==(1,2) and np.isfinite(values).all()
        assert abs(detail['threshold']-22.721063389585737)<1e-10
        winner=int(values[0].argmax())
        def classify(gate):
            admitted=bool(values[0,winner]>=gate)
            return {'trials':1,'correct_accepted':int(admitted and winner==index%2),'wrong_accepted':int(admitted and winner!=index%2),'missed':int(not admitted),'raw_correct':int(winner==index%2)}
        counts=classify(22.721063389585737); assert counts==detail['counts']
        tally.update(counts); relaxed_counts=classify(17.4227460230377);relaxed.update(relaxed_counts)
        strict_bits[index%2].update(counts);relaxed_bits[index%2].update(relaxed_counts)
        if name=='stable-005-m60' and index<288:
            original=old_paths[index]; prior=original['conditions'][0]['methods'][primary]
            assert np.array_equal(values,old[prior['score_array']]) and detail['counts']==prior['counts']
            parity+=1
    assert seen==set(range(first,first+count)) and bits[0]==bits[1]
    total+=len(seen); total_counts.update(tally)
    print(name,'N',len(seen),'bits',dict(bits),'strict',dict(tally),'relaxed',dict(relaxed),flush=True)
    cohorts.append({'name':name,'physical_cases':len(seen),'bits':dict(bits),'method':primary,'requested_seconds':args['seconds'],'inner_rate_hz':args['rate'],'sample_rate_hz':args['sample_rate']/args['sample_rate_denominator'],'cn0_db_hz':args['cn0'],'gaussian_doppler_width_hz':args['ray_gaussian_width_hz'],'strict':endpoints(tally,strict_bits),'relaxed':endpoints(relaxed,relaxed_bits),'calibration_alternatives':report['configuration']['calibration_alternatives'],'selection_methods':report['configuration']['selection_methods'],'duration_alternatives':report['configuration']['duration_alternatives'],'report':str(report_path),'report_sha256':sha(base/report_path),'scores_sha256':report['scores_sha256']})
    archive.close()
old.close()
print('TOTAL',total,dict(total_counts),'binaryN',len(binaries),'generatorSourceN',len(generators),'receiverSourceN',len(receivers),'receiverDepN',len(dependencies),'allSixRootsGloballyUnique',len(all_seeds)==len(set(all_seeds)),'first288bitidentical',parity,flush=True)
print('originalstablefailedindices',[i for i,r in old_paths.items() if r['status']!='completed'],'resumeindices',sorted(resume_rows),flush=True)

assert total==3996 and parity==288 and primary_raw_checks==3996*6
assert len(binaries)==len(generators)==len(receivers)==len(dependencies)==1
assert len(all_seeds)==len(set(all_seeds))==3996*6
assert [i for i,r in old_paths.items() if r['status']!='completed']==list(range(288,300))
source_inputs=campaign['source_inputs']
assert all(pathlib.Path(p).is_file() and sha(pathlib.Path(p))==h for p,h in source_inputs['files'].items())
assert sha(pathlib.Path('tools/triad-conditional-sweep.py'))==next(iter(receivers))
deps=json.loads(next(iter(dependencies)))
assert all(sha(pathlib.Path('tools')/p)==h for p,h in deps.items())
plan_names=['holdout-export-plan.json','holdout-score-plan.json','boundary-export-plan.json','boundary-score-plan.json','g1-export-plan.json','g1-score-plan.json','stable-resume-export-plan.json','stable-resume-score-plan.json','primary-score-plan.json']
admin_path=cliargs.audits_root/'administrative-failure-analysis.json';admin=json.loads(admin_path.read_text())
assert admin['summary']['reports']==1 and admin['summary']['actual_method_path_observations']==288 and admin['summary']['failed_path_records']==12
assert all(not r['scientific_qualification'] for r in full['reports'])
receipt={'schema':'triad-spread-independent-primary-review-v1','state':'accepted-scoped-recount-and-bindings','created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'reviewer':'/root/noise_review','runtime':{'executable':sys.executable,'python':sys.version,'numpy':np.__version__},'reproducer':{'path':str(pathlib.Path(__file__).resolve()),'sha256':sha(pathlib.Path(__file__)),'example_command':'PYTHONDONTWRITEBYTECODE=1 OMP_NUM_THREADS=2 OPENBLAS_NUM_THREADS=2 MKL_NUM_THREADS=2 /tmp/triad-weak-env/bin/python /tmp/spread-primary-audit.py'},'metadata_root':str(base),'plan_root':str(plans),'plans_sha256':{n:sha(plans/n) for n in plan_names},'remote_full_raw_audit':{'path':str(full_path),'sha256':sha(full_path),'summary':full['summary'],'verified_file_records':len(full['input_file_checks']),'primary_verified_file_records':primary_raw_checks,'scope':'Independently reviewed remote audit records and cross-bound all 3996 primary manifest/score input hashes. Raw arrays were hashed by the recorded remote analyzer; this reviewer did not independently re-read omitted raw arrays locally.'},'administrative_failure_audit':{'path':str(admin_path),'sha256':sha(admin_path),'summary':admin['summary']},'executed_checks':['exact combined 15-job plan equality to original component jobs with final stable replacement','exact complete planned case-index coverage, one record per primary index, balanced bit strata','all six exact uint64 seed derivations checked against row metadata, exporter argv and actual manifests; globally distinct roots','full generation argv reconstructed using hash-matching driver; all defaults and explicit generation options compared to frozen export plans','all receiver defaults and explicit options reconstructed using hash-matching receiver parser and matched to report configuration; full budget diagnostics matched','actual manifest SHA matched to receiver input receipt and remote raw audit; manifest content matched embedded export receipt','manifest physical duration/rate/Fs/spread/Doppler-width/noise-variance/block/encryption/noisy-stat fields checked','all saved primary score NPZ SHA hashes and finite 1x2 rows verified; strict and relaxed independent recount including per-bit counts','all current 516 generation source inputs, receiver source and nine dependency file hashes match recorded running-source fingerprints','stable resumption exact failed-index identity and original-success score bit identity','remote full-raw summary and exact primary plan coverage reviewed; local receipt does not repeat raw receiver inference'],'source_binding':{'generation_source_fingerprint':next(iter(generators)),'generation_file_count':len(source_inputs['files']),'all_generation_files_match_current_checkout':True,'recorded_binary_sha256':next(iter(binaries)),'binary_scope':'One recorded supplied-binary hash across all cases. This audit does not prove that these sources built that binary, nor independently rehash a remote binary.','receiver_source_sha256':next(iter(receivers)),'receiver_dependency_sha256':deps,'all_receiver_files_match_current_checkout':True},'case_receipts_sha256':hashlib.sha256(json.dumps(receipts,sort_keys=True,separators=(',',':')).encode()).hexdigest(),'case_receipts_digest_scope':'ordered cohort/index/source/manifest hash/score-array-key receipts checked during this run','primary_physical_cases':total,'strict_execution_total':dict(total_counts),'totals_scope':'Execution accounting only; distinct channel/SNR/duration populations are not a pooled performance estimate.','strict_gate':22.721063389585737,'relaxed_gate':17.4227460230377,'relaxed_scope':'Same strict-UCB-calibrated scores re-gated; no independent recalibration or additional physical trials.','cohorts':cohorts,'stable_administrative_resumption':{'original_campaign_status':'timed out, not passed','original_complete_indices':[0,287],'original_failure_indices':list(range(288,300)),'resumed_indices':sorted(resume_rows),'resumed_same_frozen_seeds_configuration':True,'original_success_score_arrays_bit_identical':parity,'final_physical_cases':300,'final_strict_correct_accepted':300,'original_failed_report_retained':str(base/'holdout-scores/stable-005-m60.json'),'final_report':str(base/'holdout-scores/stable-005-m60-complete.json'),'original_campaign_sha256':sha(base/'holdout/stable-005-m60/campaign.json'),'resume_campaign_sha256':sha(base/'holdout/stable-005-m60-resume/campaign.json'),'original_score_report_sha256':sha(base/'holdout-scores/stable-005-m60.json')},'assumptions_and_limits':['Pointwise one-sided confidence endpoints, not simultaneous intervals or selection-corrected performance guarantees across cohorts, rates, receiver choices, strict/relaxed gates or adaptive development.','Within-bit independent identically distributed Bernoulli outcomes from the declared simulation population are required for per-bit CP; globally distinct seed roots support recorded independence design but do not prove PRNG independence or field validity.','Balanced-mean lower bounds allow different bit-stratum success probabilities. All-success equal-stratum cases use AM-GM; partial-success cases use Bonferroni per-bit bounds. Pooled CP assumes a common success probability and is labeled separately.','Fixed aligned/tightly bounded acquisition model and selected channel/oscillator/noise settings only; this is not generic all-purpose blind-acquisition or RF hardware qualification.','Conditional noise replicas, paired receiver/compression variants and administrative reruns are not additional independent primary physical paths.','No empirical daily false-admission qualification is established by the signal matrix or sparse null controls. Analytical gate guarantees require the detector/calibration and declared candidate/attempt/model/duration families assumptions.','G1 jobs explicitly select one calibration alternative, other primary jobs select two; do not silently combine their policies into an unallocated common selectable family.','This receipt binds reviewed artifacts by hashes after execution; it is not proof of a prior timestamped source/binary hash commitment.','Archive inventory binding and remote-to-local archive integrity remain root-owned final packaging checks.'],'scientific_qualification':False}
cliargs.output.write_text(json.dumps(receipt,indent=2,sort_keys=True,allow_nan=False)+'\n')
print('RECEIPT',str(cliargs.output),sha(cliargs.output))
