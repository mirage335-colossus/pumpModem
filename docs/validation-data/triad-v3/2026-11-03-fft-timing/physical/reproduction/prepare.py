#!/usr/bin/env python3
"""Configure and freeze an actual-sampled FFT acquisition campaign; never decode."""
from __future__ import annotations
import argparse
from decimal import Decimal, ROUND_CEILING
import hashlib
import json
import math
from pathlib import Path
import random
import subprocess
import time

EPOCH=1793664000
SEED_FLAGS=('seed','key-seed','channel-seed','oscillator-seed','noise-seed','frontend-seed','path-seed')
TRIALS=((0,'signal'),(1,'signal'),(0,'signal'),(1,'signal'),(0,'null'),(1,'null'),(0,'wrong-key'),(1,'wrong-key'))
MAX_SAMPLES=2_000_000
MAX_GROUPS=1_000_000
MAX_FREQUENCIES=4096
MEMORY=1024*1024**2
SOURCES=('tools/triad-long.cpp','include/datapump/triad_modem_v3.hpp','src/triad_modem_v3.cpp',
    'src/triad_simulation_stream.cpp','src/triad_receiver_v3.cpp','src/channel_spread.hpp',
    'src/channel_perturbation.hpp','src/triad_gaussian_fading.hpp','src/tuning.cpp','src/tuning_presets.cpp',
    'tools/triad_fft_acquire.py','tools/triad_group_acquire.py','tools/triad_gaussian_window.py',
    'tools/triad_gaussian_state.py','tools/triad_noise_likelihood.py','tools/triad_state_workspace.py')


def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()


def cell(name,**kw):
    return dict(name=name,rate=36,added=10,rf=500000,nominal_if=1725,base='gpsdo-tcxo',
                shift='ic-7100',spread='sub9-moderate',selective='each-1',margin=1,
                timing='tight',seconds=8,cn0=12,fhss=False,wait=0,whole_burst=False,**kw)


def cells():
    def edited(original,name,**kw):return {**original,'name':name,**kw}
    anchor=cell('01-anchor')
    hf=edited(anchor,'05-hf30-xo',rate=360,rf=30e6,base='gpsdo-xo',shift='gpsdo-xo',spread='hf-worst',selective='each-25')
    micro=edited(hf,'07-microwave-ocxo',rf=5.8e9,base='gpsdo-ocxo',shift='gpsdo-ocxo',spread='aurora-worst')
    vlf=edited(anchor,'10-vlf-zero',rate=3.6,rf=0,nominal_if=8270,base='gpsdo-tcxo',shift='baseband-clock',spread='sub9-worst',selective='each-25')
    free=edited(hf,'12-free-cb',rf=500000,nominal_if=180,base='crystal',shift='cb-ssb',spread='hf-moderate',selective='fast-10')
    loss=edited(hf,'14-miss-first',rf=500000,selective='each-65',seconds=40,fhss=True,loss='first')
    wait=edited(anchor,'17-internal-wait3',base='gpsdo-xo',shift='gpsdo-xo',selective='each-5',seconds=40,wait=3)
    low=edited(anchor,'20-low001-preflight',rate=.001,added=1,rf=0,nominal_if=.0005,base='gpsdo-ocxo',shift='baseband-clock',spread='sub9-best',preflight_only=True,fs=1,fs_denominator=128)
    return [anchor,edited(anchor,'02-wide',timing='wide'),edited(anchor,'03-depth65',selective='each-65'),
        edited(anchor,'04-depth65-margin3',selective='each-65',margin=3),hf,edited(hf,'06-hf30-margin3',margin=3),
        micro,edited(micro,'08-microwave-shared',shift='baseband-clock'),edited(micro,'09-microwave-depth1',selective='each-1'),
        vlf,edited(vlf,'11-vlf-margin3',margin=3),free,edited(free,'13-disciplined',base='gpsdo-xo',shift='gpsdo-xo'),
        loss,edited(loss,'15-miss-last',loss='last'),edited(loss,'16-miss-middle-scattered',loss='middle-scattered',selective='each-25'),
        wait,edited(wait,'18-whole-bit-burst3',wait=0,whole_burst=True),
        edited(anchor,'19-rate24k',rate=24000,added=1,rf=30e6,base='gpsdo-ocxo',shift='gpsdo-ocxo',spread='none',selective='off',cn0=20),
        low,edited(low,'21-low01-preflight',rate=.01,nominal_if=.005,fs_denominator=32),
        edited(anchor,'22-rate1m-preflight',rate=1e6,added=1,spread='none',selective='off',preflight_only=True),
        edited(anchor,'23-rate30m-preflight',rate=30e6,added=1,spread='none',selective='off',preflight_only=True)]


SPREAD={'none':(0,0),'sub9-best':(.005,.001),'sub9-moderate':(.05,.005),'sub9-worst':(5,.05),
        'hf-worst':(7,30),'hf-moderate':(6,10),'aurora-worst':(11,55)}


def argv_for(c):
    delay,doppler=SPREAD[c['spread']]
    half=7.035 if c['timing']=='wide' else .090500000001
    args=['{BINARY}','--mode','export-sampled','--rate',str(c['rate']),'--added',str(c['added']),
        '--rf-hz',str(c['rf']),'--nominal-if-hz',str(c['nominal_if']),
        '--base-oscillator',c['base'],'--shift-oscillator',c['shift'],'--spread-preset',c['spread'],
        '--selective-perturbation',c['selective'],'--margin',str(c['margin']),'--seconds',str(c['seconds']),
        '--basis','two-ray','--tap-delay-ms',str(delay*c['margin']),'--tap-doppler-hz',str(doppler*c['margin']),
        '--cn0',str(c['cn0']),'--epoch',str(EPOCH),'--clock-ppm','0','--epoch-offsets','0','--frequency','0','0','1',
        '--frequency-drift','0','--start',str(-half),str(-half),'1',
        '--threads','1','--memory-mib','1024','--export-max-samples',str(MAX_SAMPLES),'--export-max-mib','512',
        '--block','128','--output','{OUTPUT}/workspaces/'+c['name']+'-trial0']
    if c['timing']=='wide':args += ['--clock-sync','default','--arrival-prior-seconds',str(-half),str(half)]
    else:args += ['--clock-sync','GPS_10ms-1ms_region-0ms_offset','--audio-error-ms','70']
    if c.get('fs'):args += ['--sample-rate',str(c['fs']),'--sample-rate-denominator',str(c.get('fs_denominator',1))]
    if c['fhss']:args += ['--fhss']
    if c['wait']:args += ['--burst-wait-tiles',str(c['wait'])]
    if c['whole_burst']:args += ['--symbol-burst','3']
    return args


def ceiling(value):return int(value.to_integral_value(rounding=ROUND_CEILING))


def calendar_bound(cfg,c):
    """Public native calendar formula, decimal evaluation + two-row safety guard."""
    D=lambda x:Decimal(str(x))
    fs=D(cfg['capture']['sample_rate_hz']);ppm=D(cfg['base_nominal_relative_bound_ppm'])
    rx=1+ppm*D('0.0000005');tx=1-ppm*D('0.0000005');physical=fs*rx
    half=D('7.035') if c['timing']=='wide' else D('.090500000001')
    arrival_low,arrival_high=map(D,cfg['initial_arrival_bounds_seconds'])
    low=min(-half,arrival_low);high=max(-half,arrival_high)
    pad=max(D(0),-low)+D('.05');source=D(cfg['duration']['source_support_seconds'])
    tail=0
    selective=cfg['selective_channel']
    if selective['enabled']:
        tail=ceiling(2*physical/(3*D(selective['resolved_correlation_scale_hz'])))+2
    delay=D(cfg['spread_delay_ms'])/1000
    complete=ceiling(delay*physical)+tail+ceiling(8*rx/tx)+2
    nominal=ceiling(fs*rx*(pad+max(D(0),high)+source/tx)+complete+2)
    upper=nominal+2
    return dict(predicted_sample_count=nominal,conservative_sample_upper_bound=upper,
        source_support_seconds=float(source),sample_rate_hz=float(fs),capture_seconds_upper=float(D(upper)/fs),
        added_rounding_guard_samples=2,selective_tail_samples_bound=tail,
        native_initial_arrival_bounds_seconds=cfg['initial_arrival_bounds_seconds'],
        native_full_calendar_half_width_seconds=cfg['full_observation_calendar_half_width_seconds'],
        scope='Reimplementation of public observation_calendar with Decimal and2extra rows, not a measured export. Actual manifest must be checked before scoring.')


def preflight(cfg,c):
    cal=calendar_bound(cfg,c);n=cal['conservative_sample_upper_bound'];fs=cal['sample_rate_hz']
    lag_seconds=14.07 if c['timing']=='wide' else .181000000002
    lag_max=ceiling(Decimal(str(lag_seconds))*Decimal(str(fs)));lags=lag_max+1
    bound=cfg['search_estimates']['margin_frequency_half_width_hz'];step=1/(4*cal['capture_seconds_upper'])
    frequencies=1 if bound==0 else 2*math.ceil(bound/step)+1
    groups=lags*frequencies
    try:
        from scipy.fft import next_fast_len
        nfft=next_fast_len(2*n-1);fft_scope='scipy.fft.next_fast_len(2*Nupper-1)'
    except ImportError:
        nfft=1<<(2*n-2).bit_length();fft_scope='conservative next-power-of-two upper bound; scipy unavailable'
    input_bytes=145*n
    workspace=input_bytes+320*n+320*nfft+320*lags+65536
    reserve=min(512,lags)*128*1024
    cpu=workspace+reserve;cuda=cpu+192*nfft+96*n
    reasons=[]
    low=-(7.035 if c['timing']=='wide' else .090500000001)
    lattice_high=low+lag_max/fs
    if low>cfg['initial_arrival_bounds_seconds'][0] or lattice_high<cfg['initial_arrival_bounds_seconds'][1]:
        reasons.append('timing_lattice_does_not_cover_initial_prior')
    if not cfg['capture']['feasible_for_deterministic_prior']:reasons.append('capture_prior_infeasible')
    if n>MAX_SAMPLES:reasons.append('sample_cap_exceeded')
    if frequencies>MAX_FREQUENCIES:reasons.append('frequency_bank_4096_cap_exceeded')
    if groups>MAX_GROUPS:reasons.append('effective_group_cap_exceeded')
    if cpu>=MEMORY:reasons.append('cpu_explicit_array_memory_cap_exceeded')
    return dict(**cal,lag_max_seconds=lag_seconds,lag_max_samples=lag_max,timing_count=lags,
        actual_integer_lattice_bounds_seconds=[low,lattice_high],
        nominal_in_band_snr_db=c['cn0']-10*math.log10(cfg['capture']['nominal_triplet_bandwidth_hz']),
        nominal_per_sample_snr_db=c['cn0']-10*math.log10(fs),
        frequency_bound_hz=bound,frequency_maximum_step_hz=step,frequency_count=frequencies,
        effective_groups=groups,nfft_bound=nfft,nfft_bound_scope=fft_scope,input_bytes_upper=input_bytes,
        cpu_explicit_array_bytes_upper=cpu,cuda_explicit_array_bytes_upper=cuda,
        cuda_memory_budget_feasible=cuda<MEMORY,blocked_reasons=reasons,
        status='preflight_only' if c.get('preflight_only') else ('resource_or_capture_unavailable' if reasons else 'eligible'),
        memory_scope='Current FFTWorkspace and score_tile explicit-array bounds including145bytes/rawrow and omitted full group-score array; not total process RSS or allocator guarantee.')


def freeze_trials(c,args,bounds):
    records=[]
    for index,(bit,control) in enumerate(TRIALS):
        roots={name:400_000_000+domain*10_000_000+index for domain,name in enumerate(SEED_FLAGS)}
        ident=c['name']+'-trial'+str(index)+'-'+control
        export=list(args);export[export.index('--output')+1]='{OUTPUT}/workspaces/'+ident
        export += ['--signal-bit',str(bit),'--control',control]
        for name,value in roots.items():export += ['--'+name,str(value)]
        score=['{PYTHON}','{ROOT}/tools/triad_fft_acquire.py','{OUTPUT}/workspaces/'+ident,
            '--lag-max-seconds',str(bounds['lag_max_seconds']),'--lag-step-samples','1',
            '--frequency-offset-grid',str(-bounds['frequency_bound_hz']),str(bounds['frequency_bound_hz']),str(bounds['frequency_maximum_step_hz']),
            '--group-tile','512','--memory-mib','1024','--device','cpu','--precision','fp32',
            '--attempts-per-day','1440','--duration-alternatives','8','--selection-methods','16',
            '--omit-group-scores','--evaluate-simulation','--output','{OUTPUT}/scores/'+ident+'.json']
        records.append(dict(id=ident,bit=bit,control=control,seed_roots=roots,export_argv=export,score_argv=score,
            export_timeout_seconds=300,score_timeout_seconds=900,
            execution_status='not_started_root_approval_required'))
    return records


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,required=True);p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--max-configure-seconds',type=float,default=60)
    a=p.parse_args();root=a.root.resolve();binary=a.binary.resolve()
    if a.output.exists():p.error('output must be fresh')
    if not 0<a.max_configure_seconds<=60:p.error('configure budget must be in(0,60]')
    a.output.mkdir(parents=True)
    before={name:sha(root/name) for name in SOURCES};binary_before=sha(binary)
    start=time.monotonic();deadline=start+a.max_configure_seconds;rows=[];native_records=[]
    for c in cells():
        args=argv_for(c);probe=[x.replace('{BINARY}',str(binary)).replace('{OUTPUT}',str(a.output/'unused')) for x in args]
        probe[probe.index('--mode')+1]='configure'
        remaining=deadline-time.monotonic()
        if remaining<=0:rows.append(dict(case=c,status='configure_not_started_deadline',trials=[]));continue
        try:proc=subprocess.run(probe,cwd=root,capture_output=True,text=True,timeout=min(10,remaining))
        except subprocess.TimeoutExpired:
            rows.append(dict(case=c,status='native_configure_timeout',trials=[]));continue
        receipt=dict(case=c['name'],argv=probe,returncode=proc.returncode,stdout=proc.stdout,stderr=proc.stderr)
        native_records.append(receipt)
        if proc.returncode:
            rows.append(dict(case=c,status='native_configuration_rejected',configure_receipt_index=len(native_records)-1,trials=[]));continue
        cfg=json.loads(proc.stdout)['configuration']
        if c.get('loss'):
            # Native sampled-buffer length has a conservative ceiling that can
            # add one row at a logical dwell boundary. Do not call that row a
            # new final hop: use the whole-dwell source calendar instead.
            dwell_ratio=cfg['duration']['elapsed_source_calendar_seconds']/.4
            dwell_count=round(dwell_ratio)
            if not math.isclose(dwell_ratio,dwell_count,rel_tol=1e-12,abs_tol=1e-12):
                raise ValueError('missed-hop experiment requires a public whole-dwell source calendar')
            last=dwell_count-1
            if c['loss']=='first':missed=[0]
            elif c['loss']=='last':missed=[last]
            else:
                middle=last//2;pool=[i for i in range(1,last) if i not in (middle,middle+1)]
                missed=sorted({middle,middle+1,*random.Random(0x5411A9).sample(pool,min(8,len(pool)))})
            args += ['--missed-dwells',','.join(map(str,missed))]
            c={**c,'resolved_missed_dwells':missed,'nominal_dwell_count':last+1,
               'nominal_dwell_count_source':'logical elapsed_source_calendar_seconds /0.4, near-integer roundoff snapped; not conservatively rounded buffer rows',
               'loss_scope':'Fixed nominal receiver-calendar indices derived before decoding; timing/clock uncertainty can shift physical hop overlap.'}
            probe += ['--missed-dwells',','.join(map(str,missed))]
            remaining=deadline-time.monotonic()
            if remaining<=0:rows.append(dict(case=c,status='configure_not_started_deadline',trials=[]));continue
            try:proc=subprocess.run(probe,cwd=root,capture_output=True,text=True,timeout=min(10,remaining))
            except subprocess.TimeoutExpired:
                rows.append(dict(case=c,status='native_configure_timeout',trials=[]));continue
            native_records.append(dict(case=c['name'],purpose='resolved-loss-configure',argv=probe,
                returncode=proc.returncode,stdout=proc.stdout,stderr=proc.stderr))
            if proc.returncode:
                rows.append(dict(case=c,status='native_configuration_rejected',configure_receipt_index=len(native_records)-1,trials=[]));continue
            cfg=json.loads(proc.stdout)['configuration']
        bounds=preflight(cfg,c)
        rows.append(dict(case=c,configure_receipt_index=len(native_records)-1,configuration=cfg,
            status=bounds['status'],preflight=bounds,
            trials=freeze_trials(c,args,bounds) if bounds['status']=='eligible' else []))
    after={name:sha(root/name) for name in SOURCES};binary_after=sha(binary)
    result=dict(schema='triad-fft-physical-campaign-v1',root_approval_required_before_exports_or_scores=True,
        generator_sha256=sha(__file__),source_file_sha256=before,source_unchanged=before==after,
        native_binary_sha256=binary_before,binary_unchanged=binary_before==binary_after,
        configure_elapsed_seconds=time.monotonic()-start,configure_budget_seconds=a.max_configure_seconds,
        root_seed_schedule=[dict(index=i,bit=b,control=c,roots={name:400_000_000+d*10_000_000+i for d,name in enumerate(SEED_FLAGS)}) for i,(b,c) in enumerate(TRIALS)],
        pairing_scope='The same eight fresh root tuples are reused across contrast cells. Changed rate/clock/spread geometry changes RNG consumption and physical samples; paired seeds are not equal realizations or independent receptions across cells.',
        declared_distributions=dict(tight_arrival='Native GPS10ms/1msregion/0offset + causal uniform station audio lags70ms; physical endpoint draws remain independent of public search.',
            wide_arrival='Explicit independent uniform arrival[-7.035,7.035] seconds.',
            oscillator='Selected effective endpoint presets and native independent per-end realizations; sharedclock correlates static RF/audio ppm within eachendpoint only.',
            noise='Native proper complex thermal Gaussian samples at selected nominal total triplet C/N0; no post-channel normalization.',
            selective='Default dispersive-dft-v1 fixed-frame continuous synthetic stress; rates inherited from effective rayDoppler.',
            crypto='Encryption enabled; fixed reproducibleUTCepoch1793664000 and fresh independent key-seed domain. No key/epoch truth pruning.'),
        limits=dict(maximum_samples=MAX_SAMPLES,maximum_effective_groups=MAX_GROUPS,maximum_frequency_offsets=MAX_FREQUENCIES,memory_mib=1024,group_tile=512),
        receiver_scope='Whole-observation finite two-ray likelihood; full declared integer timing lattice and bounded CFO, fixed publicclock0ppm/epochoffset0/drift0. Exact physical preset taps are model-informed; this is not full blind channel/clock/epoch acquisition.',
        arithmetic_scope='Requested FP32 correlation FFTs; current substantial Gram FFT bulk and sensitive likelihood preparation/reductions remain explicit FP64. No all-FP32 or GPU acceleration claim.',
        grid_scope='No timing prior narrowing. Integer sample lattice is exhaustive only on that lattice, not continuous sub-sample timing; CFOstep<=1/(4Tupper) is rectangular coherent guidance, not a finite multipath mismatch certificate.',
        required_post_export_guards=['Bind source/executable and all four actual sampled files bySHA256 before scoring.',
            'Verify actual manifest sample_count<=declared conservative bound and2000000, complete template support at maximumlag, actualsample_rate==configured.',
            'Recompute actual FFTWorkspace+scoretile memory<=1GiB and actual effectivegroups<=1000000/frequencyoffsets<=4096.',
            'Check planned frequency_maximum_step<=1/(4*actual sample_count/sample_rate); reject mismatch, never shrink the publicbank.',
            'Use actual score outcomes and retain export/score failures separately; no configuration is a reception success.'],
        whole_bit_burst_scope='Whole-bit3x changes next-slot metadata, not silent waits within this capturedbit. Internalwaitrow usesactual --burst-wait-tiles3.',
        controls_per_eligible_cell=dict(signal=4,null=2,wrong_key=2),rows=rows)
    for name,value in (('plan.json',result),('preflight.json',native_records)):
        (a.output/name).write_text(json.dumps(value,indent=2,sort_keys=True,allow_nan=False)+'\n')
    print(json.dumps(dict(plan=str(a.output/'plan.json'),sha256=sha(a.output/'plan.json'),
        statuses={s:sum(r['status']==s for r in rows) for s in sorted({r['status'] for r in rows})},
        captures=sum(len(r['trials']) for r in rows),stable=result['source_unchanged'] and result['binary_unchanged'])))


if __name__=='__main__':main()
