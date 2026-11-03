#!/usr/bin/env python3
"""Freeze small physical triad diagnostics; this command never runs them."""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import random
import subprocess

CN0 = [200,140,120,100,80,60,55,40,32,20,6,-6,-10,-16,-20,-23,-26,-30,-60]
METHODS = ['static-fft-ucb','gaussian-window-ucb']
CONDITIONAL_CASES = [
    'rate-0.001','rate-0.01','rate-0.1','rate-1','rate-3.6','rate-10',
    'rate-36','rate-360','rate-24000','mod-off','mod-slow-65','mod-fast-65',
    'mod-each-1','mod-each-25','mod-each-65','spread-sub9-worst',
    'spread-hf-moderate','spread-hf-worst','spread-aurora-worst',
    'shift-5800000000.0','base-crystal','shiftosc-ic-7100','added-1000','fhss-360']
CONTROL_CASES = [
    ('mod-off',20),('mod-each-1',80),('mod-each-25',20),('mod-each-65',80),
    ('spread-hf-worst',20),('spread-aurora-worst',80),('added-1000',200),
    ('shift-5800000000.0',200)]
SOURCE_PATHS = [
    'tools/triad-long.cpp','include/datapump/triad_modem_v3.hpp',
    'src/triad_modem_v3.cpp','src/triad_simulation_stream.cpp',
    'src/triad_receiver_v3.cpp','src/channel_perturbation.hpp',
    'src/channel_spread.hpp','src/triad_gaussian_fading.hpp',
    'src/tuning.cpp','src/tuning_presets.cpp','tools/triad_bounded_run.py',
    'tools/triad-conditional-sweep.py','tools/triad-noise-study.py',
    'tools/triad-rbpf-study.py','tools/triad-cfo-batch-study.py',
    'tools/triad_gaussian_window.py','tools/triad-static-frequency-study.py',
    'tools/triad_gaussian_state.py','tools/triad_rbpf.py',
    'tools/triad_noise_likelihood.py','tools/triad_state_workspace.py',
    *['docs/validation-data/triad-v3/2026-11-03-broad/reproduction/'+name for name in
      ('make_plan.py','run_plan.py','analyze.py','test_plan.py','inputs/physical-cases.json')]]

def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for part in iter(lambda:stream.read(1024*1024),b''):h.update(part)
    return h.hexdigest()

def digest(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest()

def replace_option(argv,key,*values):
    if key in argv:
        i=argv.index(key);stop=i+1
        while stop<len(argv) and not argv[stop].startswith('--'):stop+=1
        argv[i:stop]=[key,*map(str,values)]
    else:argv.extend([key,*map(str,values)])

def remove_option(argv,key):
    if key in argv:
        i=argv.index(key);stop=i+1
        while stop<len(argv) and not argv[stop].startswith('--'):stop+=1
        del argv[i:stop]

def option(argv,key,default=None):
    return argv[argv.index(key)+1] if key in argv else default

def pow2_at_least(value):
    return 2**math.ceil(math.log2(value)) if value>1 else 1

def prepare_case(row):
    row=copy.deepcopy(row);case=row['case'];name=case['name']
    unsupported=case['shift_hz']==0 or case['osc']=='baseband-clock'
    if unsupported:
        row.update(status='unsupported_topology',physical_argv=None)
        return row
    argv=list(row['proposed_argv'][1:])
    if not argv:raise ValueError('represented case lacks native arguments: '+name)
    replace_option(argv,'--mode','configure')
    replace_option(argv,'--cn0',20)
    replace_option(argv,'--threads',1)
    replace_option(argv,'--memory-mib',256)
    replace_option(argv,'--export-max-samples',2000000)
    replace_option(argv,'--export-max-mib',256)
    # Source proposals retain their requested eight seconds (or explicit
    # internal-wait case). Native geometry, not this request, determines span.
    if case['timing']=='default':
        replace_option(argv,'--clock-sync','default')
        replace_option(argv,'--arrival-prior-seconds',-7.035,7.035)
        remove_option(argv,'--start')
    fs=float(row['sample_rate'])
    physical=row.get('configure_reference',{})
    doppler=float(physical.get('spread_doppler_hz',0))
    width=float(case.get('width',0))
    # Additional samples are a deliberate physical sampling refinement, not
    # a change to C/N0 or ray evolution. Preserve reciprocal low-rate clocks.
    if (case['mod']!='off' or width>0) and max(doppler,width)>0:
        fs=max(fs,float(pow2_at_least(64*max(doppler,width))))
    if fs>=1:
        replace_option(argv,'--sample-rate',int(fs))
        replace_option(argv,'--sample-rate-denominator',1)
    else:
        replace_option(argv,'--sample-rate',1)
        replace_option(argv,'--sample-rate-denominator',int(round(1/fs)))
    remove_option(argv,'--block');remove_option(argv,'--block-seconds')
    if case['rate']<1 and fs<64:
        dt=float(option(row['proposed_argv'],'--block-seconds',16/fs))
        block=max(16,int(math.ceil(dt*fs)))
    else:block=16
    replace_option(argv,'--block',block)
    cfo=float(physical.get('deterministic_oscillator_cfo_bound_hz',0))*float(case['margin'])
    nyquist=fs/(2*block)
    # Retain broad oscillators physically. A compressed bank unable to cover
    # them is not silently narrowed to a frequency range that happens to fit.
    row.update(status='planned_native_preflight',physical_argv=argv,
        sample_rate=fs,block_samples=block,receiver_frequency_bound_hz=cfo,
        receiver_block_nyquist_hz=nyquist,static_frequency_grid_supported=cfo<nyquist,
        receiver_limits={
            'acquisition':'nominal arrival/clock/epoch bank; physical timing remains independently drawn',
            'static_fft':'constant residual CFO mixture; phase constant within each retained block',
            'gaussian_window':'zero residual common CFO; independent windows, explicit Gaussian width only',
            'oscillator':'physical independent preset offsets/Wiener phase retained; no stochastic oscillator tracking in these two scorers',
            'frontend':'nominal IF is an oscillator-reference coordinate, not validated real-audio placement',
            'fhss':'source envelope plus explicit receiver-IQ losses only; no physical RF retuning'},
        sampling_note='Fs raised to at least64 times active modulation frequency or explicit Gaussian width; this heuristic requires separate sampling-refinement evidence')
    return row

def seed_args(index):
    # Disjoint domains and injective case/bit addresses. Do not choose a seed
    # after examining reception. Erasure pairs deliberately reuse one address.
    return [x for tag,base in [('--seed',1100000),('--key-seed',2100000),
        ('--channel-seed',3100000),('--oscillator-seed',4100000),
        ('--noise-seed',5100000),('--frontend-seed',6100000),('--path-seed',7100000)]
        for x in (tag,str(base+index))]

def make_job(identifier,stage,argv,timeout,**meta):
    job={'id':identifier,'stage':stage,'argv':argv,'timeout_seconds':timeout,**meta}
    job['definition_sha256']=digest(job)
    return job

def score_argv(workspace,case,method,output,mode='actual-noisy',cn0=None,seed=8100000,device='cpu',precision='fp32'):
    argv=['{PYTHON}','{ROOT}/tools/triad-conditional-sweep.py',workspace,'--mode',mode,
        '--methods',method,'--taps','2','--coherence','inf','--window-doppler-width',
        str(case['case'].get('width',0)),'--window-blocks','16','--frequency-bound',
        format(case['receiver_frequency_bound_hz'] if method=='static-fft-ucb' else 0,'.17g'),
        '--device',device,'--precision',precision,'--threads','1','--memory-gib','0.5',
        '--seed',str(seed),'--duration-alternatives','8','--selection-methods','16',
        '--calibration-alternatives','2','--attempt-spacing','60','--horizon-seconds','86400',
        '--false-probability','0.0001','--output',output]
    if mode=='conditional':argv += ['--cn0-grid='+str(cn0),'--noise-trials','4','--trial-batch','4','--controls','signal','null']
    return argv

def build_plan(source,root,device='cpu'):
    cases=[prepare_case(row) for row in source['cases']]
    by_name={row['case']['name']:row for row in cases}
    if len(by_name)!=len(cases):raise ValueError('duplicate configuration name')
    jobs=[];exports={};index=0
    for row in cases:
        name=row['case']['name']
        if row['status']=='unsupported_topology':continue
        cfg='configure-'+name
        jobs.append(make_job(cfg,'configure',['{BINARY}',*row['physical_argv']],10,case=name,depends_on=[]))
        for bit in (0,1):
            identifier=name+'-b'+str(bit)+'-signal';index+=1
            add_export(jobs,exports,row,identifier,index,bit,'signal',20,[cfg])
    # The eight-second proposal rounded to exactly one complete private tile.
    # Use that public tile boundary, not an invented physical RF hop cycle.
    # These are losses in receiver-IQ collection, not a claim about RF retunes.
    base=copy.deepcopy(by_name['fhss-360'])
    base['case']['seconds']=40
    replace_option(base['physical_argv'],'--seconds',40)
    tile_seconds=base['configure_reference']['duration']['rounded_active_seconds']
    cycle_dwells=int(round(tile_seconds/.4))
    expected_dwells=cycle_dwells*math.ceil(40/tile_seconds)
    random_ids=sorted(random.Random(2026110301).sample(range(expected_dwells),round(.10*expected_dwells)))
    random25=sorted(random.Random(2026110302).sample(range(expected_dwells),round(.25*expected_dwells)))
    erasures={'none':[],'cycle-edge':[cycle_dwells-1,cycle_dwells],
              'middle-pair':[cycle_dwells//2,cycle_dwells//2+1],
              'random10':random_ids,'random25':random25}
    pair_seed_index=5000
    for name,indices in erasures.items():
        row=copy.deepcopy(base);row['case']['name']='erasures-'+name
        row['case']['missed_dwells']=indices
        if indices:replace_option(row['physical_argv'],'--missed-dwells',','.join(map(str,indices)))
        cfg='configure-'+row['case']['name']
        cases.append(row);by_name[row['case']['name']]=row
        jobs.append(make_job(cfg,'configure',['{BINARY}',*row['physical_argv']],10,case=row['case']['name'],depends_on=[]))
        for bit in (0,1):
            identifier=row['case']['name']+'-b'+str(bit)+'-signal'
            add_export(jobs,exports,row,identifier,pair_seed_index+bit,bit,'signal',20,[cfg],
                pairing='same bit-specific seven seed roots across all five erasure schedules')
    # Fresh controls; the sampled null and wrong-key observations are distinct
    # from conditional null replicas and from the original signal paths.
    for control in ('null','wrong-key'):
        for i,(name,cn0) in enumerate(CONTROL_CASES):
            row=by_name[name];identifier=name+'-'+control+'-cn'+str(cn0)
            add_export(jobs,exports,row,identifier,10000+(control=='wrong-key')*100+i,i%2,control,cn0,['configure-'+name])
    # Actual scorers each consume the SAME exported physical observation.
    for identifier,item in exports.items():
        row=by_name[item['case']]
        for method in METHODS:
            score_id='actual-'+identifier+'-'+method
            jobs.append(make_job(score_id,'actual',score_argv(item['workspace'],row,method,
                '{OUT}/scores/'+score_id+'.json',device=device),120,
                case=item['case'],control=item['control'],bit=item['bit'],cn0_db_hz=item['cn0_db_hz'],
                method=method,workspace=item['workspace'],report='{OUT}/scores/'+score_id+'.json',
                depends_on=[identifier],predeclared_unsupported=(
                    'compressed block-center frequency Nyquist excludes selected oscillator prior'
                    if method=='static-fft-ucb' and not row['static_frequency_grid_supported'] else None)))
    # Twenty-four fixed paths, balanced across labels, selected before any
    # outcomes. Not24independent paths per setting or a reliability campaign.
    conditional_paths=[]
    for i,name in enumerate(CONDITIONAL_CASES):
        if name not in by_name:raise ValueError('missing predeclared conditional case: '+name)
        bit=i%2;identifier=name+'-b'+str(bit)+'-signal';item=exports[identifier];row=by_name[name]
        conditional_paths.append(identifier)
        for cn0 in CN0:
            for method in METHODS:
                score_id='conditional-'+identifier+'-cn'+str(cn0)+'-'+method
                jobs.append(make_job(score_id,'conditional',score_argv(item['workspace'],row,method,
                    '{OUT}/scores/'+score_id+'.json','conditional',cn0,8200000+i,device),120,
                    case=name,control='conditional-signal-and-null',bit=bit,cn0_db_hz=cn0,
                    method=method,workspace=item['workspace'],report='{OUT}/scores/'+score_id+'.json',
                    depends_on=[identifier],predeclared_unsupported=(
                        'compressed block-center frequency Nyquist excludes selected oscillator prior'
                        if method=='static-fft-ucb' and not row['static_frequency_grid_supported'] else None)))
    # Backend/precision comparison uses exact already-exported observations.
    for name in ('spread-hf-worst','added-1000'):
        identifier=name+'-b0-signal';item=exports[identifier];row=by_name[name]
        for backend in ('cpu','cuda:0'):
            for precision in ('fp32','fp64'):
                for method in METHODS:
                    score_id='benchmark-'+identifier+'-'+backend.replace(':','')+'-'+precision+'-'+method
                    jobs.append(make_job(score_id,'benchmark',score_argv(item['workspace'],row,method,
                        '{OUT}/scores/'+score_id+'.json',device=backend,precision=precision),120,
                        case=name,method=method,control='signal',bit=0,cn0_db_hz=20,
                        workspace=item['workspace'],report='{OUT}/scores/'+score_id+'.json',
                        depends_on=[identifier],backend=backend,precision=precision))
    ids=[job['id'] for job in jobs]
    if len(ids)!=len(set(ids)):raise ValueError('duplicate scientific job identity')
    for job in jobs:
        if any(dep not in ids for dep in job['depends_on']):raise ValueError('missing dependency')
    return {'schema':'triad-broad-physical-plan-v1','scope':'Fixed sampled-path diagnostic breadth; no exhaustive Cartesian reception qualification',
        'configuration_cases':cases,'jobs':jobs,'conditional_paths':conditional_paths,'conditional_cn0_grid':CN0,
        'erasure_schedules':erasures,'erasure_private_tile_dwells':cycle_dwells,
        'erasure_expected_source_dwells':expected_dwells,
        'erasure_fraction_scope':'Nominal randomized dwell counts over expected complete source tiles; actual lost observations and signal energy must be measured from exported valid rows/energy. No physical RF hop-cycle claim.',
        'source_file_sha256':{rel:sha(root/rel) for rel in SOURCE_PATHS},
        'method_comparison':'Paired receiver methods reuse each physical observation. Conditional draws reuse a frozen noise address across C/N0/methods, never count as independent physical paths.',
        'targets':{'strict':{'correct_probability':.99,'false_admission_probability':1e-4,'horizon_seconds':86400},
                   'relaxed':{'correct_probability':.9,'false_admission_probability':.01,'horizon_seconds':43200},
                   'calibration':'strict residual UCB retained for both scoring thresholds; complete empirical rare-event qualification is not claimed'},
        'resource_limits':{'export_samples':2000000,'export_mib':256,'process_memory_gib':.5,'export_timeout_seconds':300,'score_timeout_seconds':120,'threads_per_child':1},
        'stage_counts':{stage:sum(j['stage']==stage for j in jobs) for stage in ('configure','export','actual','conditional','benchmark')},
        'pilot_ids':['rate-0.001-b0-signal','spread-hf-worst-b0-signal'],
        'limitations':['nominal arrival/clock/epoch projection remains incomplete for nearly all selected physical uncertainty; measured failure may be acquisition mismatch',
            'no real RF hopping or shared baseband-clock topology; no validated physical audio/VLF placement',
            'per-preset stable ray separation is separate from explicitly selected stochastic Gaussian width',
            'source elapsed duration is complete native tile/calendar/tail support, not requested eight seconds',
            'high C/N0 residual/subtraction precision failures remain failure cells, never noiseless successes',
            'controls and conditional samples are diagnostic and too small to establish operational false-admission/reliability targets']}

def add_export(jobs,exports,row,identifier,index,bit,control,cn0,depends,pairing=None,timeout=300):
    argv=list(row['physical_argv']);replace_option(argv,'--mode','export-stats')
    replace_option(argv,'--cn0',cn0);replace_option(argv,'--signal-bit',bit)
    replace_option(argv,'--control',control);replace_option(argv,'--output','{OUT}/workspaces/'+identifier)
    argv+=['--stats-noisy',*seed_args(index)]
    item={'case':row['case']['name'],'control':control,'bit':bit,'cn0_db_hz':cn0,
          'workspace':'{OUT}/workspaces/'+identifier,'seed_address_index':index,'pairing':pairing}
    exports[identifier]=item
    jobs.append(make_job(identifier,'export',['{BINARY}',*argv],timeout,depends_on=depends,**item))

def build_long_plan(source,root,device='cpu'):
    core=build_plan(source,root,device);base={r['case']['name']:r for r in core['configuration_cases']}
    cases=[];jobs=[];exports={};index=20000
    for rate in (.001,.01):
        for spread,delay,doppler,denominator in [('sub9-best',.005,.001,4),('sub9-worst',5,.05,2)]:
            row=copy.deepcopy(base['rate-'+str(rate)])
            name='long-rate-'+str(rate)+'-'+spread
            row['case'].update(name=name,spread=spread,mod='each-65',seconds=850*86400)
            row['sample_rate']=1/denominator;row['block_samples']=1000//denominator
            row['receiver_block_nyquist_hz']=.0005
            row['static_frequency_grid_supported']=row['receiver_frequency_bound_hz']<.0005
            argv=row['physical_argv']
            for key,value in [('--seconds',850*86400),('--spread-preset',spread),
                ('--selective-perturbation','each-65'),('--tap-delay-ms',delay),
                ('--tap-doppler-hz',doppler),('--sample-rate',1),
                ('--sample-rate-denominator',denominator),('--block',row['block_samples']),
                ('--export-max-samples',128000000),('--export-max-mib',2048),('--cn0',-60)]:
                replace_option(argv,key,value)
            row['sampling_note']='Whole complex sampled waveform at .25/.5samples/s; nominal deterministic Doppler/modulation sidebands fit. Long block projection intentionally omits most modulation sideband evidence; no64x modulation-rate sampling heuristic is claimed for this separate tier.'
            row['configure_reference_scope']='Historical short proposal only; long nativeconfigure stage must establish actual calendar/capture bounds.'
            cases.append(row);cfg='configure-'+name
            jobs.append(make_job(cfg,'configure',['{BINARY}',*argv],10,case=name,depends_on=[]))
            for bit in (0,1):
                index+=1;identifier=name+'-b'+str(bit)+'-signal'
                add_export(jobs,exports,row,identifier,index,bit,'signal',-60,[cfg],timeout=1200)
                for method in METHODS:
                    sid='actual-'+identifier+'-'+method
                    jobs.append(make_job(sid,'actual',score_argv(exports[identifier]['workspace'],row,method,
                        '{OUT}/scores/'+sid+'.json',device=device),120,
                        case=name,control='signal',bit=bit,cn0_db_hz=-60,method=method,
                        workspace=exports[identifier]['workspace'],report='{OUT}/scores/'+sid+'.json',
                        depends_on=[identifier]))
    core.update(configuration_cases=cases,jobs=jobs,conditional_paths=[],conditional_cn0_grid=[],
        erasure_schedules={},pilot_ids=[next(iter(exports))],
        scope='Separate optional genuine850-day sampled stable-ray diagnostics at−60dB-Hz; no automatic launch with broad short campaign',
        resource_limits={'export_samples':128000000,'export_mib':2048,'process_memory_gib':.5,
            'export_timeout_seconds':1200,'score_timeout_seconds':120,'threads_per_child':1},
        stage_counts={stage:sum(j['stage']==stage for j in jobs) for stage in ('configure','export','actual','conditional','benchmark')})
    core['limitations'] += ['Longtier has8fresh signal paths only; no longtier actual null/wrong-key controls or reliability qualification.',
        'Stable-ray B=0 cases retain configured independent OCXO Wiener phase. They do not validate diffuse fading, free-running long-span tracking or physical RFretuning.',
        'Fs .25/.5 samples/s and long block projection need relevant independent sampling/compression refinement; capture feasibility is not an exact bandwidth certificate.']
    return core

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[5])
    p.add_argument('--inputs',type=Path,default=Path(__file__).with_name('inputs')/'physical-cases.json')
    p.add_argument('--device',default='cpu',choices=('cpu','cuda:0'))
    p.add_argument('--tier',choices=('core','long'),default='core')
    p.add_argument('--build-provenance',type=Path,required=True)
    p.add_argument('--source-archive',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args(argv)
    if a.output.exists():p.error('plan output must be fresh')
    root=a.root.resolve();source=json.loads(a.inputs.read_text())
    plan=(build_long_plan if a.tier=='long' else build_plan)(source,root,a.device)
    plan['tier']=a.tier
    plan['input_sha256']=sha(a.inputs);plan['generator_sha256']=sha(__file__)
    provenance=json.loads(a.build_provenance.read_text())
    if provenance.get('all_required_checks_passed') is not True:p.error('build checks have not passed')
    archive_hash=sha(a.source_archive)
    if provenance.get('source_archive_sha256')!=archive_hash:p.error('source archive differs from build provenance')
    plan['build_provenance_sha256']=sha(a.build_provenance)
    plan['source_archive_sha256']=archive_hash
    plan['expected_binary_sha256']=provenance['binary_sha256']
    plan['base_head']=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
    plan['source_binding']='file hashes include uncommitted working-tree contents; HEAD alone is not the source identity'
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(plan,indent=2,sort_keys=True,allow_nan=False)+'\n')
    print(json.dumps({'output':str(a.output),'plan_sha256':sha(a.output),'stage_counts':plan['stage_counts'],'case_count':len(plan['configuration_cases'])}))

if __name__=='__main__':main()
