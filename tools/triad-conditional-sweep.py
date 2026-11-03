#!/usr/bin/env python3
"""Conditional Gaussian-noise screens on independently sampled channel paths.

Sampled full-bank Grams, signal correlations and signal energy are simulator
inputs only. Receivers consume generated observations and a full-bank residual
noise UCB. Conditional noise replicas are not new physical channel holdouts.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys
import time
from typing import NamedTuple
import numpy as np
import torch
from scipy.stats import beta
from triad_gaussian_state import GaussianStateFilter
from triad_rbpf import RBPhaseFilter, RBPhasePrior
from triad_state_workspace import BlockWorkspace, WorkspaceError


def _module(name,filename):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(filename))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

adapter=_module('conditional_actual_adapter','triad-rbpf-study.py')
noise=_module('conditional_noise_sampler','triad-noise-study.py')
static=_module('conditional_static_receiver','triad-static-frequency-study.py')
actual_adapter=_module('conditional_actual_batch','triad-cfo-batch-study.py')
METHODS=('static-fft-ucb','gaussian-ou-ucb','cfo-ou-ucb','gaussian-window-ucb')
ACTUAL_CONTROLS=('signal','null','wrong-key')


class SampledDesign(NamedTuple):
    gram: np.ndarray
    mean: np.ndarray
    signal_energy: np.ndarray
    valid_rows: np.ndarray
    centers_seconds: np.ndarray
    dt_seconds: np.ndarray
    mapping: np.ndarray
    labels: np.ndarray
    sample_rate: float
    sample_count: int
    block_samples: int
    identity: dict


class PreparedBatch(NamedTuple):
    """Receiver-only arrays, already divided by reference and observed UCB.

    correlations[B,T,C,K], grams[B,T,C,K,K], physical center deltas[B].
    Flatten T*C when calling a helper accepting grams[B,C,K,K]. Hidden sampled
    means, source bits and injected noise variances deliberately are absent.
    """
    correlations: np.ndarray
    grams: np.ndarray
    dt_seconds: np.ndarray
    centers_seconds: np.ndarray
    labels: np.ndarray
    variance_upper: np.ndarray
    metadata: dict


def parse_args(argv=None):
    parser=argparse.ArgumentParser(add_help=False)
    parser.add_argument('--mode',choices=('conditional','actual-noisy'),default='conditional')
    parser.add_argument('--window-doppler-width',type=float,help='explicit twice-RMS Gaussian coefficient Doppler width Hz; not two-ray separation')
    parser.add_argument('--window-blocks',type=int,default=16)
    parser.add_argument('--cn0-grid',default='-66,-64,-62,-60',help='comma-separated injected total-triplet C/N0 values; use --cn0-grid=-66,-64')
    parser.add_argument('--noise-trials',type=int,default=32)
    parser.add_argument('--trial-batch',type=int,default=16)
    parser.add_argument('--controls',nargs='+',choices=('signal','null'),default=['signal'])
    parser.add_argument('--methods',nargs='+',choices=METHODS,default=list(METHODS[:3]))
    supplied=list(sys.argv[1:] if argv is None else argv)
    extra,remaining=parser.parse_known_args(supplied)
    args=adapter.parse_args(['--reference-variance','1','--selection-methods','16','--duration-alternatives','8',
                            '--methods','rbpf-ucb','--frequency-initialization','midpoint-grid',*remaining])
    try:cn0=[float(x) for x in extra.cn0_grid.split(',')]
    except ValueError:parser.error('C/N0 grid must be comma-separated finite numbers')
    if not cn0 or not all(math.isfinite(v) and -300<v<300 for v in cn0) or len(set(cn0))!=len(cn0):parser.error('C/N0 grid must be distinct and inside (-300,300)')
    if not 1<=extra.noise_trials<=4096 or not 1<=extra.trial_batch<=extra.noise_trials:parser.error('require 1<=trial-batch<=noise-trials<=4096')
    if len(extra.controls)!=len(set(extra.controls)) or len(extra.methods)!=len(set(extra.methods)):parser.error('controls and methods must be distinct')
    if args.selection_methods<len(extra.methods):parser.error('method family must cover selected receivers')
    if args.frequency_initialization!='midpoint-grid':parser.error('this screen requires midpoint-grid frequency initialization')
    if args.phase_diffusion or args.frequency_random_walk or args.resampling!='none':parser.error('this screen has no oscillator diffusion/resampling receiver model')
    if not 1<=extra.window_blocks<=1024:parser.error('window blocks must be in1..1024')
    if extra.window_doppler_width is not None and (not math.isfinite(extra.window_doppler_width) or extra.window_doppler_width<0):parser.error('window Doppler width must be finite nonnegative')
    if 'gaussian-window-ucb' in extra.methods and extra.window_doppler_width is None:parser.error('Gaussian window method requires explicit --window-doppler-width')
    if extra.mode=='actual-noisy':
        if any(v=='--cn0-grid' or v.startswith('--cn0-grid=') for v in supplied):parser.error('actual-noisy mode does not inject or sweep C/N0')
        if any(v=='--controls' for v in supplied):parser.error('actual-noisy controls come from each observed workspace')
        if any(v=='--noise-trials' or v.startswith('--noise-trials=') or v=='--trial-batch' or v.startswith('--trial-batch=') for v in supplied):parser.error('actual-noisy mode has exactly one observed trial per physical path')
        cn0=[];extra.noise_trials=extra.trial_batch=1
    args.mode=extra.mode;args.window_doppler_width=extra.window_doppler_width;args.window_blocks=extra.window_blocks
    args.cn0_grid=cn0;args.noise_trials=extra.noise_trials;args.trial_batch=extra.trial_batch
    args.controls=extra.controls;args.receiver_methods=extra.methods
    return args


def memory_preflight(ws,args,candidates):
    b,d,k,t=ws.block_count,ws.dimension,args.taps,args.trial_batch
    source=b*(d*d*16+d*16+8*5)
    prepared=b*t*candidates*(k+k*k)*16
    sampler=24*d*d*16+24*t*d*16+65536
    bulk=8 if args.precision=='fp32' else 16
    core=t*candidates*(args.particles*(8*k*bulk+24*8)+16*16*k*k+128)
    fft=0
    if 'static-fft-ucb' in args.receiver_methods:
        nfft,half=static.grid_shape(b,ws.block_samples/ws.sample_rate,ws.sample_count/ws.sample_rate,args.frequency_bound)
        fft=static.memory_bound(b,t*candidates,k,d,nfft,2*half+1,args.precision)
    window=0;window_retained=0
    if 'gaussian-window-ucb' in args.receiver_methods:
        w=min(b,args.window_blocks);dimension=w*k;full,remainder=divmod(b,args.window_blocks)
        stored=candidates*(full*args.window_blocks**2+remainder**2)*k*k*16+candidates*b*k*8+candidates*b*k*k*16
        preparation=stored+14*b*candidates*k*k*16+24*dimension*dimension*16+b*8
        scoring=stored+160*t*candidates*dimension+64*candidates*dimension**2+64*t*candidates
        window=max(preparation,scoring)+3*b*candidates*k*k*16
        window_retained=stored+2*b*candidates*k*k*16
    total=source+3*prepared+sampler+window_retained+max(core,fft,window)
    return {'sampled_design_bytes':source,'prepared_batch_bytes':prepared,'sampler_preparation_bound_bytes':sampler,
            'cfo_core_bound_bytes':core,'fft_bound_bytes':fft,'window_bound_bytes':window,'retained_window_cache_bound_bytes':window_retained,'total_array_bound_bytes':total}


def load_design(ws,args):
    if not ws.gaussian_projection_supported:raise WorkspaceError('nonlinear or non-Gaussian frontend cannot use projected Gaussian noise draws')
    if ws.manifest.get('control','signal')!='signal':raise WorkspaceError('sampled design must be a signal path; null is generated explicitly')
    _,mapping,labels=adapter.candidate_mapping(ws,args.taps)
    bound=memory_preflight(ws,args,len(mapping))
    effective=args.memory_limit_bytes
    if args.device.startswith('cuda'):effective=min(effective,int(.75*torch.cuda.mem_get_info(args.device)[0]))
    if bound['total_array_bound_bytes']>effective:raise MemoryError('conditional cache/batch estimate exceeds explicit effective memory limit')
    b,d=ws.block_count,ws.dimension
    G=np.fromfile(ws.directory/'gram.c128',dtype='<c16').reshape(b,d,d)
    mean=np.fromfile(ws.directory/'mean.c128',dtype='<c16').reshape(b,d)
    energy=np.fromfile(ws.directory/'signal_energy.f64',dtype='<f8')
    valid=np.fromfile(ws.directory/'valid_rows.u64',dtype='<u8')
    intervals=np.fromfile(ws.directory/'intervals.u64',dtype='<u8').reshape(b,2)
    expected_start=np.arange(b,dtype=np.uint64)*ws.block_samples
    expected_end=np.minimum(expected_start+ws.block_samples,ws.sample_count)
    if not np.array_equal(intervals[:,0],expected_start) or not np.array_equal(intervals[:,1],expected_end) or np.any(valid>expected_end-expected_start):raise WorkspaceError('sampled design calendar/valid rows inconsistent')
    if not np.isfinite(G).all() or not np.isfinite(mean).all() or not np.isfinite(energy).all() or np.any(energy<0):raise WorkspaceError('sampled simulator statistics nonfinite or negative energy')
    empty=valid==0
    if np.any(G[empty]!=0) or np.any(mean[empty]!=0) or np.any(energy[empty]!=0):raise WorkspaceError('erased rows require zero sampled statistics')
    centers=(intervals[:,0].astype(np.float64)+intervals[:,1].astype(np.float64))/(2*ws.sample_rate)
    dt=np.r_[0.,np.diff(centers)]
    if np.any(dt[1:]<=0):raise WorkspaceError('sampled centers do not increase')
    identity={name:adapter.sha256(ws.directory/name) for name in ('manifest.json','gram.c128','mean.c128','signal_energy.f64','valid_rows.u64','intervals.u64')}
    return SampledDesign(G,mean,energy,valid,centers,dt,mapping,labels,ws.sample_rate,ws.sample_count,ws.block_samples,identity),dict(bound,effective_memory_limit_bytes=effective)


def draw_batch(design,args,cn0,seed,trials,control):
    """The only simulator-truth consumer; outputs receiver statistics separately."""
    if control not in ('signal','null'):raise ValueError('unknown simulated control')
    if trials<1 or trials>args.trial_batch:raise ValueError('draw exceeds preflight trial batch')
    variance=design.sample_rate/10**(cn0/10)
    if not math.isfinite(variance) or variance<=0:raise ValueError('injected variance unresolved')
    reference=args.reference_variance
    rng=np.random.default_rng(seed)
    b=len(design.valid_rows);c,k=design.mapping.shape
    correlations=np.empty((b,trials,c,k),np.complex128)
    base_grams=np.empty((b,c,k,k),np.complex128)
    residual=np.zeros(trials,np.float64);correction=np.zeros_like(residual);dof=0
    for j in range(b):
        valid=int(design.valid_rows[j])
        if valid:
            r,E=noise.project_joint_statistics(design.gram[j],design.mean[j],float(design.signal_energy[j]),valid,variance,rng,trials,control=='signal')
        else:r=np.zeros((trials,design.gram.shape[1]),np.complex128);E=np.zeros(trials,np.float64)
        result=adapter.whole_bank_residual(torch.from_numpy(r/reference),torch.from_numpy(design.gram[j]/reference),torch.from_numpy(E/reference),valid)
        correlations[j]=result.projected_correlations.numpy()[:,design.mapping]
        base_grams[j]=result.projected_gram.numpy()[design.mapping[:,:,None],design.mapping[:,None,:]]
        adjusted=result.energy.numpy()-correction;updated=residual+adjusted
        correction=(updated-residual)-adjusted;residual=updated;dof+=int(result.complex_dof)
    upper=adapter.variance_upper_confidence(residual,dof,args.budget.delta_per_segment).numpy()
    if not np.isfinite(upper).all() or np.any(upper<=0):raise ValueError('observed pooled UCB unresolved')
    correlations/=upper[None,:,None,None]
    grams=base_grams[:,None,:,:,:]/upper[None,:,None,None,None]
    if not np.isfinite(correlations).all() or not np.isfinite(grams).all():raise ValueError('normalized receiver statistics nonfinite')
    prepared=PreparedBatch(correlations,grams,design.dt_seconds,design.centers_seconds,design.labels,upper,
        {'complex_residual_dof':dof,'reference_variance':reference,'pooled_residual_energy_over_reference':residual.tolist(),
         'sample_rate':design.sample_rate,'sample_count':design.sample_count,'block_samples':design.block_samples})
    return prepared,{'injected_noise_variance_simulation_only':variance,'injected_cn0_db_hz_simulation_only':cn0,
        'noise_seed':int(seed),'conditional_noise_trials':trials,'control':control}


def score_prepared(prepared,args,method,receiver_cache=None):
    """Receiver-facing entry point: no sampled mean, truth bit or injected N0."""
    if method not in METHODS:raise ValueError('unsupported conditional receiver')
    r,G=prepared.correlations,prepared.grams;b,t,c,k=r.shape
    dtype=torch.complex64 if args.precision=='fp32' else torch.complex128
    if method=='gaussian-window-ucb':
        from triad_gaussian_window import prepare_windowed_gaussian
        cache={} if receiver_cache is None else receiver_cache
        alpha=prepared.variance_upper
        reference_gram=G[:,0]*alpha[0]
        for trial in range(t):
            if not np.allclose(G[:,trial]*alpha[trial],reference_gram,rtol=1e-10,atol=1e-12):raise ValueError('Gaussian window reuse requires one shared reference Gram and stationary per-trial UCB')
        entry=cache.get('gaussian-window-ucb')
        preparation_seconds=0.
        if entry is None:
            t0=time.perf_counter()
            plan=prepare_windowed_gaussian(reference_gram,prepared.centers_seconds,
                doppler_width_hz=args.window_doppler_width,window_blocks=args.window_blocks,
                stationary_power=args.tap_prior.stationary_power,memory_limit_bytes=args.memory_limit_bytes)
            preparation_seconds=time.perf_counter()-t0
            cache['gaussian-window-ucb']=(plan,reference_gram.copy(),prepared.centers_seconds.copy())
        else:
            plan,previous_gram,previous_centers=entry
            if plan.doppler_width_hz!=args.window_doppler_width or plan.window_blocks!=args.window_blocks or plan.stationary_power!=args.tap_prior.stationary_power:raise ValueError('Gaussian window cache prior changed')
            if not np.array_equal(previous_centers,prepared.centers_seconds) or not np.allclose(previous_gram,reference_gram,rtol=1e-10,atol=1e-12):raise ValueError('Gaussian window cache identity changed')
        values=plan.score(r,variance_scale=alpha,correlations_scaled=True,device=args.device,precision=args.precision)
        scores=noise._bit_mixture(values,torch.as_tensor(prepared.labels,device=values.device)).detach().cpu().numpy()
        details=plan.diagnostics();details['preparation_seconds_this_call']=preparation_seconds
        details['prepared_plan_reused']=entry is not None
    elif method=='static-fft-ucb':
        sums=np.zeros((t,c,k,k),np.complex128);correction=np.zeros_like(sums)
        for item in G:
            adjusted=item-correction;updated=sums+adjusted;correction=(updated-sums)-adjusted;sums=updated
        whitening_diagnostics={}
        w,det=static.whiten_channel(r.reshape(b,t*c,k),sums.reshape(t*c,k,k),args.tap_prior.stationary_power,diagnostics=whitening_diagnostics)
        meta=prepared.metadata;dt=meta['block_samples']/meta['sample_rate']
        nfft,bins,frequencies=static.grid_for(b,dt,meta['sample_count']/meta['sample_rate'],args.frequency_bound)
        R=static.frequency_correlations(w,prepared.centers_seconds,dt,nfft,bins,device=args.device,precision=args.precision)
        R=R.reshape(len(bins),t,c,k);det=det.reshape(t,c)
        scores=np.concatenate([static.scores_from_correlations(R[:,j],det[j],prepared.labels) for j in range(t)],axis=0)
        details={'channel_prior':'static Gaussian coefficients; --coherence applies only to the OU receivers','frequency_grid':'existing outward-rounded uniform FFT grid','frequency_count':len(bins),'frequency_endpoints_hz':[float(frequencies[0]),float(frequencies[-1])],
                 'arithmetic':'FP64 CPU channel whitening and evidence; selected bulk FFT width','whitening':whitening_diagnostics}
    else:
        rr=torch.as_tensor(r,device=args.device);gg=torch.as_tensor(G,device=args.device)
        if method=='gaussian-ou-ucb':
            state=GaussianStateFilter(args.tap_prior,dtype=dtype,check_inputs=False)
        else:
            state=RBPhaseFilter(args.tap_prior,RBPhasePrior(frequency_bound_hz=args.frequency_bound),particles=args.particles,seed=args.seed,
                dtype=dtype,frequency_initialization='midpoint-grid',resampling='none',check_inputs=False,memory_limit_bytes=args.memory_limit_bytes)
        for j,dt in enumerate(prepared.dt_seconds):state.update(rr[j],gg[j],float(dt))
        values=state.finish()
        scores=noise._bit_mixture(values,torch.as_tensor(prepared.labels,device=values.device)).detach().cpu().numpy()
        details=state.diagnostics()
        details['frequency_grid']='zero residual CFO' if method=='gaussian-ou-ucb' else 'equal-weight midpoint finite CFO grid'
    if scores.shape!=(t,2) or not np.isfinite(scores).all():raise ValueError('receiver score shape/value invalid')
    return scores,details


def prepare_actual(ws,args):
    if not ws.has_observation or not ws.has_received_energy:raise WorkspaceError('actual-noisy mode requires observed correlations and received energy')
    if not ws.gaussian_projection_supported:raise WorkspaceError('actual frontend does not support Gaussian noise calibration')
    _,mapping,labels=adapter.candidate_mapping(ws,args.taps)
    bound=memory_preflight(ws,args,len(mapping))
    actual_bound=actual_adapter.memory_preflight(ws.block_count,1,len(mapping),args.taps,ws.dimension,args.particles,args.precision,128)
    bound['total_array_bound_bytes']=max(bound['total_array_bound_bytes'],actual_bound['total_array_bound_bytes'])
    effective=args.memory_limit_bytes
    if args.device.startswith('cuda'):effective=min(effective,int(.75*torch.cuda.mem_get_info(args.device)[0]))
    if bound['total_array_bound_bytes']>effective:raise MemoryError('actual observation cache/batch estimate exceeds effective memory limit')
    actual_args=argparse.Namespace(**vars(args));actual_args.projection_tile_blocks=128
    r,G,dt,metadata=actual_adapter.prepare_batch([ws],mapping,actual_args)
    calibration=metadata[0]['calibration'];upper=np.asarray([calibration['variance_upper_over_reference']],np.float64)
    start=np.arange(ws.block_count,dtype=np.uint64)*ws.block_samples
    stop=np.minimum(start+ws.block_samples,ws.sample_count)
    centers=(start.astype(np.float64)+stop.astype(np.float64))/(2*ws.sample_rate)
    prepared=PreparedBatch(r,G,dt,centers,labels,upper,{'complex_residual_dof':calibration['pooled_complex_dof'],
        'reference_variance':metadata[0]['reference_variance'],'pooled_residual_energy_over_reference':[calibration['pooled_residual_energy_over_reference']],
        'sample_rate':ws.sample_rate,'sample_count':ws.sample_count,'block_samples':ws.block_samples})
    return prepared,dict(bound,effective_memory_limit_bytes=effective),metadata[0]


def declared_control(path):
    """Recover only an explicit valid control before array validation can fail.

    A missing or unreadable declaration is not evidence that a failed path was
    a signal. Successful legacy workspaces may still use the signal default.
    """
    try:
        manifest=json.loads((Path(path)/'manifest.json').read_text())
    except (OSError,ValueError):return None
    value=manifest.get('control') if isinstance(manifest,dict) else None
    return value if isinstance(value,str) and value in ACTUAL_CONTROLS else None


def unclassified_failures(records):
    return sum(r['status']!='completed' and r.get('control') not in ACTUAL_CONTROLS for r in records)


def actual_summary(records,method,control):
    rows=[r for r in records if r.get('control')==control]
    good=[r for r in rows if r['status']=='completed']
    keys=('trials','correct_accepted','wrong_accepted','missed','false_admitted','not_admitted')
    total={k:sum(r['conditions'][0]['methods'][method]['counts'].get(k,0) for r in good) for k in keys}
    failed=len(rows)-len(good)
    if control=='signal':total['trials']=len(rows);total['missed']+=failed
    n=total['trials'];success=total['correct_accepted' if control=='signal' else 'false_admitted']
    confidence=interval(success,n) if n else {'one_sided_95_lower':0.,'one_sided_95_upper':1.}
    confidence['scope']='pointwise fixed-size independent actual physical paths; signal processing failures count as misses, failed null/control records prevent qualification'
    unknown=unclassified_failures(records)
    complete=bool(rows) and failed==0 and unknown==0
    return {'attempted_physical_paths':len(rows),'completed_physical_paths':len(good),'failed_physical_paths':failed,
        'unclassified_failed_physical_paths':unknown,'qualification_complete':complete,
        'counts':total,'interval':confidence,'null_qualification_complete':complete if control=='null' else None,
        'wrong_key_campaign_complete':complete if control=='wrong-key' else None,
        'wrong_key_scope':'empirical mismatched-key signals, not a Gaussian noise-only bound' if control=='wrong-key' else None}


def counts(scores,truth,gate,control):
    winners=scores.argmax(axis=1);accepted=scores[np.arange(len(scores)),winners]>=gate
    if control=='signal':
        return {'trials':len(scores),'correct_accepted':int(np.sum(accepted&(winners==truth))),'wrong_accepted':int(np.sum(accepted&(winners!=truth))),
                'missed':int(np.sum(~accepted)),'raw_correct':int(np.sum(winners==truth))}
    return {'trials':len(scores),'false_admitted':int(np.sum(accepted)),'not_admitted':int(np.sum(~accepted))}


def interval(success,n):
    return {'one_sided_95_lower':float(beta.ppf(.05,success,n-success+1)) if success else 0.,
            'one_sided_95_upper':float(beta.ppf(.95,success+1,n-success)) if success<n else 1.,
            'scope':'pointwise conditional noise trials on this fixed physical path; not independent channel validation'}


def run(args):
    torch.set_num_threads(args.threads);torch.set_float32_matmul_precision('highest')
    if args.device.startswith('cuda'):
        if not torch.cuda.is_available():raise ValueError('CUDA unavailable')
        torch.cuda.init();torch.cuda.set_device(args.device);torch.backends.cuda.matmul.allow_tf32=False;torch.cuda.reset_peak_memory_stats(args.device)
    started=time.perf_counter();records=[];arrays={};seen=set()
    if len({str(p.resolve()) for p in args.inputs})!=len(args.inputs):raise ValueError('duplicate sampled physical path')
    for path_index,path in enumerate(args.inputs):
        # Release duration-sized source/prepared caches before the next load,
        # including after an earlier condition failed. Reports retain scalars only.
        design=None;prepared=None;receiver_cache={}
        path_record={'input':str(path),'physical_path_index':path_index,'conditions':[],'observation_mode':args.mode,
            'control':declared_control(path)}
        try:
            ws=BlockWorkspace(path,max_tile_blocks=1,memory_limit_bytes=args.memory_limit_bytes)
            control=ws.manifest.get('control','signal')
            if args.mode=='actual-noisy' and control not in ACTUAL_CONTROLS:raise WorkspaceError('unknown actual observation control')
            path_record['control']=control
            identity=tuple(ws.manifest.get(k) for k in ('channel_seed','key_seed','epoch'))
            if all(v is not None for v in identity):
                if identity in seen:raise ValueError('duplicate channel/key/epoch identity')
                seen.add(identity)
            if args.mode=='actual-noisy':
                control=path_record['control']
                t0=time.perf_counter();prepared,bound,calibration=prepare_actual(ws,args)
                preparation_seconds=time.perf_counter()-t0
                receiver_results={};receiver_seconds={};details={}
                for method in args.receiver_methods:
                    if args.device.startswith('cuda'):torch.cuda.synchronize(args.device)
                    t0=time.perf_counter();receiver_results[method],details[method]=score_prepared(prepared,args,method,receiver_cache)
                    if args.device.startswith('cuda'):torch.cuda.synchronize(args.device)
                    receiver_seconds[method]=time.perf_counter()-t0
                truth=ws.manifest.get('signal_bit_simulation_truth',ws.manifest.get('signal_bit',0)) if control=='signal' else None
                if control=='signal' and (isinstance(truth,bool) or truth not in (0,1)):raise ValueError('signal evaluation bit must be binary')
                condition={'cn0_db_hz':None,'control':control,'noise_trials':1,'actual_observations':1,'methods':{},
                    'observed_calibration':calibration,'preparation_seconds':preparation_seconds}
                for method,scores in receiver_results.items():
                    key=f'path{path_index:05d}_actual_{control}_{method}';arrays[key]=scores
                    condition['methods'][method]={'counts':counts(scores,truth,args.budget.log_threshold,control),'score_array':key,
                        'threshold':args.budget.log_threshold,'receiver_seconds':receiver_seconds[method],'diagnostics_last_batch':details[method]}
                path_record.update(status='completed',conditions=[condition],truth_bit_simulation_only=truth,memory=bound,
                    input_hashes={name:adapter.sha256(ws.directory/name) for name in adapter.INPUT_FILES},
                    sampled_seconds=ws.sample_count/ws.sample_rate,block_count=ws.block_count)
            else:
                t0=time.perf_counter();design,bound=load_design(ws,args)
                path_record.update(load_seconds=time.perf_counter()-t0,memory=bound,sampled_input_hashes=design.identity,
                    sampled_seconds=design.sample_count/design.sample_rate,block_count=len(design.valid_rows))
            for snr_index,cn0 in enumerate(args.cn0_grid):
                for control in args.controls:
                    scores_by_method={m:[] for m in args.receiver_methods};draw_seconds=0.;receiver_seconds={m:0. for m in args.receiver_methods};metadata=[];details={}
                    for first in range(0,args.noise_trials,args.trial_batch):
                        size=min(args.trial_batch,args.noise_trials-first)
                        # Fixed seed addresses do not use C/N0 or control, pairing
                        # noise draws across the whole predeclared sweep.
                        seed=int(np.random.SeedSequence([args.seed,path_index,first]).generate_state(1,dtype=np.uint64)[0])
                        t0=time.perf_counter();prepared,draw=draw_batch(design,args,cn0,seed,size,control);draw_seconds+=time.perf_counter()-t0
                        metadata.append(dict(draw,variance_upper_over_reference=prepared.variance_upper.tolist(),complex_residual_dof=prepared.metadata['complex_residual_dof']))
                        for method in args.receiver_methods:
                            if args.device.startswith('cuda'):torch.cuda.synchronize(args.device)
                            t0=time.perf_counter();score,detail=score_prepared(prepared,args,method,receiver_cache)
                            if args.device.startswith('cuda'):torch.cuda.synchronize(args.device)
                            receiver_seconds[method]+=time.perf_counter()-t0;scores_by_method[method].append(score);details[method]=detail
                    # Source labels are evaluation-only, after all scores freeze.
                    truth=ws.manifest.get('signal_bit_simulation_truth',ws.manifest.get('signal_bit',0)) if control=='signal' else None
                    if control=='signal' and (isinstance(truth,bool) or truth not in (0,1)):raise ValueError('signal evaluation bit must be binary')
                    condition={'cn0_db_hz':cn0,'control':control,'noise_trials':args.noise_trials,'methods':{},'sampler_and_ucb_seconds':draw_seconds,'draws_simulation_only':metadata}
                    for method in args.receiver_methods:
                        scores=np.concatenate(scores_by_method[method],axis=0);outcomes=counts(scores,truth,args.budget.log_threshold,control)
                        success=outcomes['correct_accepted' if control=='signal' else 'false_admitted']
                        key=f'path{path_index:05d}_snr{snr_index:03d}_{control}_{method}';arrays[key]=scores
                        condition['methods'][method]={'counts':outcomes,'conditional_interval':interval(success,args.noise_trials),'score_array':key,
                            'threshold':args.budget.log_threshold,'receiver_seconds':receiver_seconds[method],'diagnostics_last_batch':details[method]}
                    path_record['conditions'].append(condition)
            path_record['status']='completed'
        except Exception as error:path_record.update(status='error',error_type=type(error).__name__,error=str(error))
        records.append(path_record)
        design=None;prepared=None;receiver_cache.clear()
    args.output.parent.mkdir(parents=True,exist_ok=True);scorepath=args.output.with_suffix('.scores.npz');np.savez_compressed(scorepath,**arrays)
    config={k:v for k,v in vars(args).items() if k not in ('inputs','output','budget','tap_prior','phase_prior')};config['coherence']='infinity' if math.isinf(args.coherence) else args.coherence
    config['tap_powers']=list(args.tap_prior.stationary_power)
    result={'schema':'triad-conditional-sweep-v1','source_sha256':adapter.sha256(__file__),
        'dependency_sha256':{n:adapter.sha256(Path(__file__).with_name(n)) for n in ('triad-noise-study.py','triad-rbpf-study.py','triad-cfo-batch-study.py','triad_gaussian_window.py','triad-static-frequency-study.py','triad_gaussian_state.py','triad_rbpf.py','triad_noise_likelihood.py','triad_state_workspace.py')},
        'configuration':config,'family':args.budget.diagnostics(),'sampled_physical_paths':len(records),'paths':records,
        'observation_mode':args.mode,'counting_scope':('one independently sampled actual observation per physical path' if args.mode=='actual-noisy' else 'conditional noise replicas on each fixed sampled physical path'),
        'actual_summary_by_control':({control:{method:actual_summary(records,method,control) for method in args.receiver_methods} for control in ACTUAL_CONTROLS if any(r.get('control')==control for r in records) or unclassified_failures(records)} if args.mode=='actual-noisy' else None),
        'failed_physical_path_records':sum(r['status']!='completed' for r in records),'scores_file':scorepath.name,'scores_sha256':adapter.sha256(scorepath),
        'unclassified_failed_physical_path_records':unclassified_failures(records) if args.mode=='actual-noisy' else None,
        'wall_seconds':time.perf_counter()-started,'arithmetic':{'sampler_rank_noise_ucb_gram':'FP64','bulk_fft_and_state':args.precision,
            'gaussian_ou_covariance':'same selected width as GaussianStateFilter','cfo_covariance_and_all_evidence':'FP64','tf32_allowed':False},
        'limitations':['observation_mode distinguishes generated conditional-noise screens from actual-noisy independent physical observations; the latter reads only actual r/E and public Grams, never mean/signal energy.',
            'Gaussian-window uses explicit twice-RMS Gaussian Doppler width and independent prior windows; this is not the two-ray separation parameter and needs physical sampling/window refinement.',
            'Conditional Gaussian noise resampling on fixed independently sampled physical paths; replicas are not independent channel/oscillator realizations or new holdout qualification.',
            'Full-band sampled path statistics preserve selected shaping, multipath and oscillator realization. Nonlinear sampled frontend is rejected; colored/impulsive noise is not modeled here.',
            'Receiver interface contains projected observations, Grams and observed stationary residual-UCB only; sampled means, source bits and injected noise variance remain simulator/evaluation inputs.',
            'A single constant proper-complex Gaussian variance per path/trial is assumed for exact residual-UCB calibration. No rare operational false-alarm claim follows from a small H0 screen.',
            'C/N0/control alternatives reuse seed addresses as common random numbers; total energy is regenerated by the joint sampler, never scaled from an earlier noisy capture. Trial batch size is part of frozen noise-stream identity.',
            'Static FFT uses static Gaussian tap coefficients; configured coherence applies only to Gaussian-OU and CFO-OU methods.',
            'FFT and midpoint CFO grids have different finite priors; score differences between them are not pure algorithm or arithmetic gains.',
            'Gaussian OU receiver has zero residual CFO. CFO-OU models a constant CFO and Gaussian tap evolution, not stochastic oscillator phase/frequency diffusion.',
            'All methods, duration alternatives and any later receiver selection require declared family accounting. No best-case selection, sequential stopping or automatic practical duration inversion.']}
    if args.device.startswith('cuda'):result['cuda_memory']={'maximum_allocated_bytes':torch.cuda.max_memory_allocated(args.device),'maximum_reserved_bytes':torch.cuda.max_memory_reserved(args.device)}
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n');return result


def main(argv=None):
    args=parse_args(argv);result=run(args)
    print(json.dumps({'output':str(args.output),'physical_paths':len(result['paths']),'failed':result['failed_physical_path_records'],'wall_seconds':result['wall_seconds']}))
    return int(bool(result['failed_physical_path_records']))


if __name__=='__main__':raise SystemExit(main())
