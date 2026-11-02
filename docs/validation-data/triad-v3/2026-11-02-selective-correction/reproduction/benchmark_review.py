import pathlib,json,hashlib,math,collections,itertools,sys,importlib.util,tempfile
import numpy as np
repo=pathlib.Path.cwd();base=repo/'local-evidence/triad-selective-correction';root=base/'gpu-results/benchmark';scores=root/'local-evidence/triad-selective-correction/gpu-boundary/benchmark';out=base/'analysis/benchmark-review';out.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(repo/'tools'))
spec=importlib.util.spec_from_file_location('benchmark_review_scorer',repo/'tools/triad-conditional-sweep.py');scorer=importlib.util.module_from_spec(spec);spec.loader.exec_module(scorer)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();load=lambda p:json.loads(p.read_text());planpath=base/'plans/gpu-boundary/boundary-final/benchmark-score-jobs.json';plan=load(planpath);runtime=load(root/'metadata/benchmark-runtime.json');package=load(root/'metadata/package-manifest.json');source_manifest=load(root/'metadata/source-file-manifest.json');sourcefiles={x['path']:x['sha256'] for x in source_manifest['files']};runner=load(root/'runner/results.json');runs={r['name']:r for r in runner['results']};gates={'strict':math.log(1440*16*8*2/(1e-4/2)),'relaxed':math.log(720*16*8*2/(.01/2))}
assert sha(root/'gpu-benchmark.tar.gz')=='59f65eeba016aeb1bc39cf72207668b2ecce2003990fbd8025f4dfc56c98a62b'
assert sha(root/'plans/benchmark-score-jobs.json')==sha(planpath)==runtime['benchmark_plan_sha256']==package['benchmark_plan_sha256'];assert sha(root/'metadata/source-file-manifest.json')==package['source_file_manifest_sha256'];assert runtime['benchmark_runner_workers']==package['sequential_workers']==1 and package['all_exit_zero'] and package['completed_jobs']==6 and not runner['cancelled']
for m in package['members']:
 p=root/m['path'];assert p.is_file() and p.stat().st_size==m['size'] and sha(p)==m['sha256']
assert len(runs)==len(plan['jobs'])==6 and [r['name'] for r in runner['results']]==[j['name'] for j in plan['jobs']]
assert runtime['torch_num_threads']==1 and all(v=='1' for v in runtime['environment'].values())
q=runtime['pre_benchmark_quiescence'];assert q['science_score_jobs']==q['completed_exit_zero']==30 and q['missing_outputs']==q['active_python_processes']==q['gpu_memory_used_mib']==q['gpu_utilization_percent']==0
reports={};tables={};allcounts=[];jobs_out=[];condition_rows={};all_sources=set();all_deps=set();source_inputs={}
for job in plan['jobs']:
 name=job['name'];run=runs[name];argv=job['argv'];assert run['command']==argv and run['returncode']==0 and run['runner']['exit_code']==0 and run['runner']['outcome']=='completed' and run['runner']['termination']['group_empty'];assert load(root/'runner'/(name+'.status.json'))==run['runner']
 target=pathlib.Path(argv[argv.index('--output')+1]).name;p=scores/target;r=load(p);assert r['schema']=='triad-conditional-sweep-v1' and r['failed_physical_path_records']==0 and r['observation_mode']=='conditional';assert sha(p.parent/r['scores_file'])==r['scores_sha256'];assert sha(repo/'tools/triad-conditional-sweep.py')==r['source_sha256']==sourcefiles['tools/triad-conditional-sweep.py'];assert len(r['dependency_sha256'])==9
 for dep,h in r['dependency_sha256'].items():assert sha(repo/'tools'/dep)==h==sourcefiles['tools/'+dep]
 all_sources.add(r['source_sha256']);all_deps.add(json.dumps(r['dependency_sha256'],sort_keys=True));cfg=r['configuration']
 with tempfile.TemporaryDirectory(prefix='benchmark-review-parser-') as temporary:
  parse=list(argv[2:]);parse[parse.index('--output')+1]=str(pathlib.Path(temporary)/'fresh.json');parsed=scorer.parse_args(parse)
 expected_cfg={k:v for k,v in vars(parsed).items() if k not in ('inputs','output','budget','tap_prior','phase_prior')};expected_cfg['coherence']='infinity' if math.isinf(parsed.coherence) else parsed.coherence;expected_cfg['tap_powers']=list(parsed.tap_prior.stationary_power);assert cfg==expected_cfg and r['family']==parsed.budget.diagnostics()
 assert cfg['threads']==1 and cfg['seed']==231207 and cfg['noise_trials']==cfg['trial_batch']==64 and cfg['controls']==['signal','null'] and cfg['receiver_methods']==['static-fft-ucb','gaussian-window-ucb'];assert r['arithmetic']['tf32_allowed'] is False
 inputs=argv[2:argv.index('--output')];assert inputs==[x['input'] for x in r['paths']] and len(inputs)==4
 table={};used=set();load_s=draw_s=prep_s=0.;methods=collections.Counter();thiscounts=collections.defaultdict(collections.Counter)
 with np.load(p.parent/r['scores_file'],allow_pickle=False) as a:
  for pi,row in enumerate(r['paths']):
   assert row['status']=='completed' and row['physical_path_index']==pi and row['observation_mode']=='conditional' and row['control']=='signal';manifestpath=pathlib.Path(row['input'])/'manifest.json';m=load(manifestpath);assert m['resolved_configuration']['selective_perturbation']=='each-65' and m['path_truth_simulation_only']['selective_perturbation']['model']=='dispersive-dft-v1';assert m['source_format']['encrypted'] and m['signal_bit'] in (0,1)
   for f,h in row['sampled_input_hashes'].items():assert sha(pathlib.Path(row['input'])/f)==h
   source_inputs[row['input']]={'input_sha256':row['sampled_input_hashes'],'key_seed':m['key_seed'],'physical_seed_roots':m['random_seed_roots'],'selective_model':m['path_truth_simulation_only']['selective_perturbation']['model']}
   load_s+=row['load_seconds'];assert len(row['conditions'])==8
   seen=set()
   for c in row['conditions']:
    assert c['noise_trials']==64 and len(c['draws_simulation_only'])==1;control=c['control'];snr=c['cn0_db_hz'];seen.add((snr,control));draw_s+=c['sampler_and_ucb_seconds'];assert set(c['methods'])==set(cfg['receiver_methods'])
    draw=c['draws_simulation_only'][0];assert draw['conditional_noise_trials']==64 and draw['control']==control and draw['injected_cn0_db_hz_simulation_only']==snr
    expected_seed=int(np.random.SeedSequence([cfg['seed'],pi,0]).generate_state(1,dtype=np.uint64)[0]);assert draw['noise_seed']==expected_seed
    for method,d in c['methods'].items():
     methods[method]+=d['receiver_seconds'];prep_s+=d['diagnostics_last_batch'].get('preparation_seconds_this_call',0.);key=d['score_array'];used.add(key);x=np.asarray(a[key]);assert x.shape==(64,2) and np.isfinite(x).all();assert math.isclose(d['threshold'],gates['strict'],abs_tol=1e-12,rel_tol=0);identity=(pi,snr,control,method);assert identity not in table;table[identity]=x.copy();condition_rows[(name,identity)]={'draw':c['draws_simulation_only'],'source_hashes':row['sampled_input_hashes'],'diagnostics':d['diagnostics_last_batch']}
     for gate,t in gates.items():
      w=x.argmax(1);ad=x[np.arange(len(x)),w]>=t
      count={'trials':64,'correct_accepted':int(np.sum(ad&(w==m['signal_bit']))),'wrong_accepted':int(np.sum(ad&(w!=m['signal_bit']))),'missed':int(np.sum(~ad)),'raw_correct':int(np.sum(w==m['signal_bit']))} if control=='signal' else {'trials':64,'false_admitted':int(ad.sum()),'not_admitted':int((~ad).sum())}
      if gate=='strict':assert count==d['counts']
      thiscounts[(method,snr,control,gate)].update(count)
   assert seen=={(v,c) for v in cfg['cn0_grid'] for c in cfg['controls']}
  assert set(a.files)==used
 for (method,snr,control,gate),count in thiscounts.items():allcounts.append({'job':name,'method':method,'cn0_db_hz':snr,'control':control,'gate':gate,'counts':dict(count)})
 profile=name.split('-arithmetic-')[0];tables[name]=table;reports[name]=r
 timing={'scorer_wall_seconds':r['wall_seconds'],'bounded_child_elapsed_seconds':run['runner']['elapsed_seconds'],'launcher_elapsed_seconds':run['elapsed_seconds'],'source_load_seconds':load_s,'conditional_noise_simulation_and_ucb_seconds':draw_s,'receiver_seconds_by_method':dict(methods),'gaussian_cpu_covariance_preparation_seconds':prep_s,'gaussian_receiver_excluding_named_covariance_preparation_seconds':methods['gaussian-window-ucb']-prep_s,'scorer_wall_unassigned_seconds':r['wall_seconds']-load_s-draw_s-sum(methods.values()),'cuda_memory':r.get('cuda_memory')}
 jobs_out.append({'name':name,'profile':profile,'device':cfg['device'],'precision':cfg['precision'],'report':str(p.relative_to(repo)),'report_sha256':sha(p),'npz_sha256':r['scores_sha256'],'runner_status_sha256':sha(root/'runner'/(name+'.status.json')),'timing':timing})
assert len(all_sources)==len(all_deps)==1 and len(source_inputs)==8
comparisons=[]
for profile in ['g1','high10']:
 names=[j['name'] for j in plan['jobs'] if j['name'].startswith(profile+'-arithmetic-')]
 normalized=[]
 for n in names:
  c=dict(reports[n]['configuration']);c.pop('device');c.pop('precision');normalized.append(c)
 assert normalized[0]==normalized[1]==normalized[2] and all(reports[n]['family']==reports[names[0]]['family'] for n in names)
 for left,right in itertools.combinations(names,2):
  assert set(tables[left])==set(tables[right]);acc={m:{'paired_trial_vectors':0,'paired_scalar_scores':0,'max_absolute_score_difference':0.,'max_relative_difference_nonzero_reference':0.,'max_abs_difference_over_max_1_abs_reference':0.,'bit_winner_disagreements':0,'strict_admission_disagreements':0,'relaxed_admission_disagreements':0,'minimum_reference_bit_score_gap':math.inf,'minimum_reference_gate_distance':{g:math.inf for g in gates}} for m in ['static-fft-ucb','gaussian-window-ucb']}
  for identity,x in tables[left].items():
   y=tables[right][identity];a=condition_rows[(left,identity)];b=condition_rows[(right,identity)];assert a['draw']==b['draw'] and a['source_hashes']==b['source_hashes'];method=identity[-1];d=a['diagnostics'];e=b['diagnostics']
   if method=='gaussian-window-ucb':assert all(d[k]==e[k] for k in ['blocks','candidates','taps','window_blocks','windows','doppler_width_hz','stationary_power','maximum_window_dimension','maximum_window_rank','maximum_relative_gram_repair','maximum_relative_prior_repair','maximum_relative_information_repair','prepared_plan_reused'])
   else:assert d==e
   z=acc[method];z['minimum_reference_bit_score_gap']=min(z['minimum_reference_bit_score_gap'],float(np.abs(x[:,0]-x[:,1]).min()));delta=np.abs(y-x);nz=x!=0;z['paired_trial_vectors']+=len(x);z['paired_scalar_scores']+=x.size;z['max_absolute_score_difference']=max(z['max_absolute_score_difference'],float(delta.max()));z['max_relative_difference_nonzero_reference']=max(z['max_relative_difference_nonzero_reference'],float((delta[nz]/np.abs(x[nz])).max(initial=0)));z['max_abs_difference_over_max_1_abs_reference']=max(z['max_abs_difference_over_max_1_abs_reference'],float((delta/np.maximum(1,np.abs(x))).max()));z['bit_winner_disagreements']+=int(np.sum(x.argmax(1)!=y.argmax(1)))
   for gate,t in gates.items():z[gate+'_admission_disagreements']+=int(np.sum((x.max(1)>=t)!=(y.max(1)>=t)));z['minimum_reference_gate_distance'][gate]=min(z['minimum_reference_gate_distance'][gate],float(np.abs(x.max(1)-t).min()))
  comparisons.append({'profile':profile,'reference':left,'comparison':right,'methods':acc})
assert all(not any(c['methods'][m][k] for k in ['bit_winner_disagreements','strict_admission_disagreements','relaxed_admission_disagreements']) for c in comparisons for m in c['methods'])
byname={j['name']:j for j in jobs_out};ratios=[]
for profile in ['g1','high10']:
 ref=byname[profile+'-arithmetic-cpu-fp64']['timing']
 for backend in ['cuda-fp64','cuda-fp32-min']:
  target=byname[profile+'-arithmetic-'+backend]['timing'];ratios.append({'profile':profile,'target':backend,'cpu_reference':'cpu-fp64','single_run_descriptive_ratios_cpu_over_target':{'scorer_wall':ref['scorer_wall_seconds']/target['scorer_wall_seconds'],'bounded_child_elapsed':ref['bounded_child_elapsed_seconds']/target['bounded_child_elapsed_seconds'],'static_method_total':ref['receiver_seconds_by_method']['static-fft-ucb']/target['receiver_seconds_by_method']['static-fft-ucb'],'gaussian_method_total':ref['receiver_seconds_by_method']['gaussian-window-ucb']/target['receiver_seconds_by_method']['gaussian-window-ucb'],'gaussian_excluding_named_covariance_prep':ref['gaussian_receiver_excluding_named_covariance_preparation_seconds']/target['gaussian_receiver_excluding_named_covariance_preparation_seconds']}})
result={'schema':'triad-corrected-benchmark-independent-review-v1','status':'paired-numeric-decision-and-timing-review-complete','scientific_qualification':False,'reproducer':{'path':str(pathlib.Path(__file__)),'sha256':sha(pathlib.Path(__file__))},'archive_sha256':sha(root/'gpu-benchmark.tar.gz'),'plan_sha256':sha(planpath),'runtime_receipt_sha256':sha(root/'metadata/benchmark-runtime.json'),'source_manifest_sha256':sha(root/'metadata/source-file-manifest.json'),'package_members_verified':len(package['members']),'scorer_source_sha256':next(iter(all_sources)),'scorer_dependencies':json.loads(next(iter(all_deps))),'physical_input_receipts':source_inputs,'runtime':runtime,'jobs':jobs_out,'pairwise_comparisons':comparisons,'strict_relaxed_counts_by_job_method_cn0_control':allcounts,'descriptive_timing_ratios':ratios,'gates':gates,'all_sampled_two_bit_winners_and_strict_relaxed_admissions_unchanged':True,'scope':['The same four physical paths per profile and common conditional noise draws are reused across backends, methods, C/N0 and controls. Conditional replica counts are arithmetic checks, not additional independent physical trials or tail qualification.','FP32-min covers complex64 FFT/GEMM; rank/UCB/physical simulation/static whitening/Gaussian covariance preparation remain CPU FP64. Gaussian/static score reductions use FP64 on the selected device. TF32 is disabled and highest matmul policy enforced; no lower/tensor arithmetic or native-instruction claim is made.','Only exported two-bit likelihood argmax is compared. Unexported per-frequency or internal covariance-mode rankings are not directly checked.','scorer wall begins after CUDA initialization; bounded child elapsed includes process startup. Conditional sampler+UCB includes simulation, not just reception. Method times include transfers, CPU work and finishing; Gaussian first-call covariance preparation is separately identified. No isolated GPU kernel or transfer-only timing is available.','Six jobs ran sequentially once in fixed order after a recorded idle precheck. Ratios are descriptive measurements for this shape, not randomized repeated benchmarks, universal acceleration, sensitivity qualification or an extrapolation to broad acquisition.','Runtime records one requested torch thread and OMP/BLAS environment1; threadpoolctl was unavailable, so independent BLAS thread-pool inventory is not recorded. Nominal32 vCPU differs from observed27.2 cgroup quota and256 visible logical CPUs.','Reported peak allocated/reserved CUDA bytes are framework counters, not total process RSS or whole-system peak memory.','The original remote temporary benchmark archive became0bytes for an unexplained reason. Scientific files were intact and repackaged under a new name; the verified local replacement archive is the one bound here. No benchmark jobs were rerun according to collection provenance.']}
p=out/'review.json';p.write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+'\n');print('REVIEW',str(p.relative_to(repo)),sha(p))
for j in jobs_out:print('TIMING',j['name'],j['timing'])
for c in comparisons:print('PARITY',c['reference'],c['comparison'],c['methods'])
print('RATIOS',ratios)
