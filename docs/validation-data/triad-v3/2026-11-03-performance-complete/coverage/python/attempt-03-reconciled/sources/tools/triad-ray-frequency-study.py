#!/usr/bin/env python3
"""Actual-only two-ray static likelihood with common and relative frequency.

The public retained model is A0*exp(i*2*pi*f*t_b),
A1*exp(i*2*pi*(f+delta)*t_b), with fixed public tap delays. Phase is
constant within each exported block. Complex correlations and cross-Grams are
accumulated before the Gaussian coefficient integral; no summed-Gram whitener
is applied before the two distinct ray transforms. This is not an OU receiver.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import math
from pathlib import Path
import platform
import time

import numpy as np
import torch

from triad_noise_likelihood import FalseAdmissionBudget, variance_upper_confidence, whole_bank_residual
from triad_state_workspace import BlockWorkspace, WorkspaceError

_SPEC=importlib.util.spec_from_file_location('triad_ray_static_adapter',Path(__file__).with_name('triad-static-frequency-study.py'))
adapter=importlib.util.module_from_spec(_SPEC);_SPEC.loader.exec_module(adapter)
METHODS=('ray-frequency','ray-frequency-ucb')
CONTROLS=('signal','null','wrong-key')
INPUT_FILES=('manifest.json','gram.c128','observation.c128','received_energy.f64','intervals.u64','valid_rows.u64')


def parse_args(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('inputs',nargs='+',type=Path)
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--cn0',required=True,type=float)
    p.add_argument('--reference-variance',type=float)
    p.add_argument('--methods',choices=METHODS,nargs='+',default=list(METHODS))
    p.add_argument('--frequency-bound',type=float,default=1e-6)
    p.add_argument('--ray-frequency-bound',type=float,default=2e-7)
    p.add_argument('--frequency-step',type=float,help='maximum common FFT-lattice spacing; default 1/(8*T)')
    p.add_argument('--zero-ray-mass',type=float,default=.5,help='prior atom at delta=0; remainder is uniform over all relative-frequency bins')
    p.add_argument('--frequency-tile',type=int,default=64)
    p.add_argument('--ray-frequency-tile',type=int,default=32)
    p.add_argument('--projection-tile-blocks',type=int,default=128)
    p.add_argument('--device',default='cpu')
    p.add_argument('--precision',choices=('fp32','fp64'),default='fp64')
    p.add_argument('--threads',type=int,default=2)
    p.add_argument('--memory-gib',type=float,default=4.)
    p.add_argument('--false-probability',type=float,default=1e-4)
    p.add_argument('--horizon-seconds',type=float,default=86400.)
    p.add_argument('--attempt-spacing',type=float,default=60.)
    p.add_argument('--duration-alternatives',type=int,default=8)
    p.add_argument('--selection-methods',type=int,default=16)
    p.add_argument('--calibration-alternatives',type=int,default=2)
    a=p.parse_args(argv)
    if a.output.exists() or a.output.with_suffix('.scores.npz').exists():p.error('report and scores must be fresh')
    if not math.isfinite(a.cn0):p.error('C/N0 must be finite')
    for name in ('frequency_bound','ray_frequency_bound'):
        if not math.isfinite(getattr(a,name)) or getattr(a,name)<0:p.error(name+' must be nonnegative finite')
    for name in ('reference_variance','frequency_step'):
        x=getattr(a,name)
        if x is not None and (not math.isfinite(x) or x<=0):p.error(name+' must be positive finite')
    for name in ('memory_gib','horizon_seconds','attempt_spacing'):
        if not math.isfinite(getattr(a,name)) or getattr(a,name)<=0:p.error(name+' must be positive finite')
    if not math.isfinite(a.zero_ray_mass) or not 0<=a.zero_ray_mass<=1:p.error('zero ray mass must be in [0,1]')
    for name,maximum in (('frequency_tile',1024),('ray_frequency_tile',1024),('projection_tile_blocks',1024),('threads',2)):
        if not 1<=getattr(a,name)<=maximum:p.error(name+' outside supported range')
    if a.device!='cpu' and not a.device.startswith('cuda'):p.error('device must be cpu or cuda[:index]')
    if len(a.methods)!=len(set(a.methods)) or a.selection_methods<len(a.methods):p.error('invalid method family')
    a.memory_limit_bytes=int(a.memory_gib*1024**3)
    try:
        a.budget=FalseAdmissionBudget(false_probability=a.false_probability,
            attempts=math.ceil(a.horizon_seconds/a.attempt_spacing),duration_alternatives=a.duration_alternatives,
            model_alternatives=a.selection_methods,candidates=2,calibration_segments=a.calibration_alternatives)
        a.budget.diagnostics()
    except (ValueError,OverflowError) as error:p.error(str(error))
    return a


def grid_shape(blocks,dt,duration,common_bound,ray_bound,maximum_step=None):
    """Scalar preflight: no grids allocated before the memory contract is checked."""
    if any(not math.isfinite(v) or v<0 for v in (common_bound,ray_bound)):
        raise ValueError('frequency bounds must be nonnegative finite')
    step=maximum_step if maximum_step is not None else 1/(8*duration)
    nfft,_=adapter.grid_shape(blocks,dt,duration,common_bound+ray_bound,step)
    while True:
        spacing=1/(nfft*dt)
        fhalf=math.ceil(common_bound/spacing) if common_bound else 0
        dhalf=math.ceil(ray_bound/spacing) if ray_bound else 0
        if fhalf+dhalf<nfft/2:return nfft,fhalf,dhalf
        nfft*=2
        if nfft>2**26:raise MemoryError('joint outward FFT grid exceeds 2^26 supported length')


def memory_bound(blocks,candidates,dimension,nfft,fhalf,dhalf,args):
    bulk=8 if args.precision=='fp32' else 16
    # Raw/projected banks, scaled copies, host/device conversion and sums.
    resident=8*blocks*candidates*6*16+blocks*8*4
    fft=8*nfft*candidates*(2*bulk+16)
    frequency_banks=8*(2*(fhalf+dhalf)+1)*candidates*(2*bulk+16)
    f=min(args.frequency_tile,2*fhalf+1);d=min(args.ray_frequency_tile,2*dhalf+1)
    joint=320*f*d*candidates+256*d*candidates
    tile=min(blocks,args.projection_tile_blocks)
    preparation=24*tile*dimension*dimension*16+16*tile*dimension*16+tile*128+2**20
    grids=64*(2*fhalf+2*dhalf+3)
    return {'resident_conversion_bytes':resident,'fft_workspace_bytes':fft,
        'retained_frequency_bank_bytes':frequency_banks,'joint_tile_bytes':joint,
        'projection_preparation_bytes':preparation,'grid_bytes':grids,
        'total_array_bound_bytes':resident+fft+frequency_banks+joint+preparation+grids}


def prepare(workspace,mapping,reference,delta,tile_blocks):
    """Keep original-column coordinates, including per-block off-diagonal G."""
    b,c=workspace.block_count,len(mapping)
    rr=np.empty((2,b,c,2),np.complex128)
    gg=np.empty((2,b,c,2,2),np.complex128)
    centers=np.empty(b,np.float64)
    iterator=iter(adapter._blocks(workspace,reference))
    residual=correction=0.;dof=valid_total=0
    for first in range(0,b,tile_blocks):
        rows=[next(iterator) for _ in range(min(tile_blocks,b-first))]
        raw=np.stack([x[0] for x in rows]);grams=np.stack([x[1] for x in rows])
        energy=np.asarray([x[2] for x in rows]);valid=np.asarray([x[3] for x in rows])
        centers[first:first+len(rows)]=[x[4] for x in rows]
        rr[0,first:first+len(rows)]=raw[:,mapping]
        gg[0,first:first+len(rows)]=grams[:,mapping[:,:,None],mapping[:,None,:]]
        for count in np.unique(valid):
            select=np.flatnonzero(valid==count)
            result=whole_bank_residual(torch.from_numpy(raw[select]),torch.from_numpy(grams[select]),
                torch.from_numpy(energy[select]),int(count))
            rr[1,first+select]=result.projected_correlations.numpy()[:,mapping]
            gg[1,first+select]=result.projected_gram.numpy()[:,mapping[:,:,None],mapping[:,None,:]]
            subtotal=math.fsum(float(v) for v in result.energy.numpy())
            adjusted=subtotal-correction;updated=residual+adjusted
            correction=(updated-residual)-adjusted;residual=updated
            dof+=sum(int(v) for v in result.complex_dof.numpy());valid_total+=int(count)*len(select)
    if not np.isfinite(rr).all() or not np.isfinite(gg).all():raise ValueError('nonfinite retained bank')
    upper=float(variance_upper_confidence(residual,dof,delta))
    if not math.isfinite(upper) or upper<=0:raise ValueError('noise upper bound unresolved')
    return rr,gg,centers,upper,{'pooled_residual_energy_over_reference':residual,
        'pooled_complex_dof':dof,'variance_upper_over_reference':upper,'valid_complex_samples':valid_total,
        'scope':'stationary white proper-complex noise; independent full-bank residuals'}


def ray_weights(delta_bins,zero_mass):
    bins=np.asarray(delta_bins)
    if bins.ndim!=1 or len(bins)==0 or np.count_nonzero(bins==0)!=1 or not 0<=zero_mass<=1:
        raise ValueError('relative-frequency grid needs exactly one zero and a valid prior atom')
    weights=np.full(len(bins),(1-zero_mass)/len(bins),np.float64)
    weights[bins==0]+=zero_mass
    return weights


def joint_evidence(r,gram,centers,dt,nfft,f_bins,d_bins,*,zero_mass=.5,
                   device='cpu',precision='fp64',frequency_tile=64,ray_tile=32):
    """Return candidate log-LRs; all frequency/candidate nonlinearities follow FFTs."""
    r=np.asarray(r);gram=np.asarray(gram)
    if r.ndim!=3 or r.shape[-1]!=2 or gram.shape!=(*r.shape,2):raise ValueError('two-ray statistic shapes differ')
    if not np.isfinite(r).all() or not np.isfinite(gram).all():raise ValueError('nonfinite two-ray statistics')
    f_bins=np.asarray(f_bins,dtype=np.int64);d_bins=np.asarray(d_bins,dtype=np.int64)
    if len(f_bins)==0 or not np.array_equal(f_bins,np.arange(f_bins[0],f_bins[-1]+1)):
        raise ValueError('common frequencies must be a nonempty consecutive integer grid')
    weights=ray_weights(d_bins,zero_mass)
    if not np.array_equal(d_bins,np.arange(d_bins[0],d_bins[-1]+1)):
        raise ValueError('relative frequencies must be consecutive integer bins')
    if frequency_tile<1 or ray_tile<1:raise ValueError('frequency tiles must be positive')
    if max(abs(int(f_bins[0]+d_bins[0])),abs(int(f_bins[-1]+d_bins[-1])))>=nfft/2:
        raise ValueError('joint frequency lattice reaches block-center Nyquist')
    if device.startswith('cuda'):torch.cuda.synchronize(device)
    start=time.perf_counter()
    combined=np.arange(f_bins[0]+d_bins[0],f_bins[-1]+d_bins[-1]+1,dtype=np.int64)
    # This FFT sees the ORIGINAL selected ray columns, never a mixed whitener.
    transformed=adapter.frequency_correlations(r,centers,dt,nfft,combined,device=device,precision=precision)
    if device.startswith('cuda'):torch.cuda.synchronize(device)
    correlation_seconds=time.perf_counter()-start
    start=time.perf_counter()
    hermitian=(gram+gram.conj().swapaxes(-1,-2))*.5
    cross=adapter.frequency_correlations(hermitian[...,0,1,None],centers,dt,nfft,-d_bins,
        device=device,precision='fp64')[...,0]
    # A0^H A1 has the POSITIVE delta exponent: hence negative FFT indices.
    diagonal=torch.as_tensor(hermitian.diagonal(axis1=-2,axis2=-1).real.sum(0),dtype=torch.float64,device=device)
    if device.startswith('cuda'):torch.cuda.synchronize(device)
    gram_seconds=time.perf_counter()-start
    start=time.perf_counter();c=r.shape[1]
    total=torch.full((c,),-math.inf,dtype=torch.float64,device=device)
    active=np.flatnonzero(weights>0)
    for first in range(0,len(active),ray_tile):
        selected=active[first:first+ray_tile];d=d_bins[selected]
        H=torch.zeros((len(d),c,2,2),dtype=torch.complex128,device=device)
        H[...,0,0]=diagonal[None,:,0]+2;H[...,1,1]=diagonal[None,:,1]+2
        H[...,0,1]=cross[selected];H[...,1,0]=cross[selected].conj()
        L=torch.linalg.cholesky(H)
        logdet=2*L.diagonal(dim1=-2,dim2=-1).real.log().sum(-1)-2*math.log(2)
        logweight=torch.as_tensor(np.log(weights[selected]),dtype=torch.float64,device=device)
        for begin in range(0,len(f_bins),frequency_tile):
            f=f_bins[begin:begin+frequency_tile]
            i0=torch.as_tensor(f-combined[0],dtype=torch.long,device=device)
            i1=torch.as_tensor(f[:,None]+d[None,:]-combined[0],dtype=torch.long,device=device)
            z=torch.empty((len(f),len(d),c,2),dtype=torch.complex128,device=device)
            z[...,0]=transformed[i0,:,0][:,None,:]
            z[...,1]=transformed[i1,:,1]
            solved=torch.linalg.solve_triangular(L[None,:,:,:,:],z[...,None],upper=False)[...,0]
            values=(solved.abs()**2).sum(-1)-logdet[None,:,:]+logweight[None,:,None]
            subtotal=torch.logsumexp(values,dim=(0,1))
            total=torch.logaddexp(total,subtotal)
    total-=math.log(len(f_bins))
    if not bool(torch.isfinite(total).all()):raise ValueError('joint frequency evidence nonfinite')
    if device.startswith('cuda'):torch.cuda.synchronize(device)
    return total,{'correlation_fft_seconds':correlation_seconds,'gram_fp64_fft_and_diagonal_seconds':gram_seconds,
        'fp64_cholesky_solve_and_mixture_seconds':time.perf_counter()-start,
        'common_bins':len(f_bins),'relative_bins':len(d_bins),'active_relative_bins':len(active),
        'evaluated_frequency_pairs_per_candidate':len(f_bins)*len(active),
        'zero_relative_prior_weight':float(weights[d_bins==0][0])}


def mix_candidates(candidate,labels):
    labels=torch.as_tensor(labels,device=candidate.device)
    scores=torch.stack([torch.logsumexp(candidate[labels==bit],0)-math.log(int((labels==bit).sum())) for bit in (0,1)])
    if not bool(torch.isfinite(scores).all()):raise ValueError('candidate mixture nonfinite')
    return scores.detach().cpu().numpy()[None,:]


def run(args):
    torch.set_num_threads(args.threads);torch.set_float32_matmul_precision('highest')
    if args.device.startswith('cuda'):
        if not torch.cuda.is_available():raise ValueError('CUDA unavailable')
        torch.cuda.init();torch.cuda.set_device(args.device);torch.backends.cuda.matmul.allow_tf32=False
        torch.cuda.reset_peak_memory_stats(args.device)
    if len({str(p.resolve()) for p in args.inputs})!=len(args.inputs):raise WorkspaceError('duplicate physical path')
    captures=[];arrays={};seen=set();started=time.perf_counter()
    for index,path in enumerate(args.inputs):
        row={'input':str(path),'physical_path_index':index,'control':'signal','methods':{}}
        try:
            ws=BlockWorkspace(path,max_tile_blocks=1,memory_limit_bytes=args.memory_limit_bytes)
            control=ws.manifest.get('control','signal');row['control']=control
            if control not in CONTROLS:raise WorkspaceError('unsupported actual control')
            if not ws.has_observation or not ws.has_received_energy:raise WorkspaceError('actual observations and received energy required')
            if not ws.gaussian_projection_supported:raise WorkspaceError('sampled frontend does not support this Gaussian noise model')
            identity=tuple(ws.manifest.get(k) for k in ('channel_seed','key_seed','epoch'))
            if all(v is not None for v in identity):
                if identity in seen:raise WorkspaceError('duplicate channel/key/epoch identity')
                seen.add(identity)
            indices=[i for i,columns in enumerate(ws.column_mapping) if len(columns)==2]
            mapping=np.asarray([ws.column_mapping[i] for i in indices],dtype=np.int64)
            labels=np.asarray([ws.candidates[i]['bit'] for i in indices],dtype=np.int64)
            if mapping.ndim!=2 or set(labels.tolist())!={0,1}:raise WorkspaceError('both bits need two-ray candidate mappings')
            dt=ws.block_samples/ws.sample_rate;duration=ws.sample_count/ws.sample_rate
            nfft,fh,dh=grid_shape(ws.block_count,dt,duration,args.frequency_bound,args.ray_frequency_bound,args.frequency_step)
            bounds=memory_bound(ws.block_count,len(indices),ws.dimension,nfft,fh,dh,args)
            limit=args.memory_limit_bytes;free=None
            if args.device.startswith('cuda'):
                free=int(torch.cuda.mem_get_info(args.device)[0]);limit=min(limit,int(.75*free))
            if bounds['total_array_bound_bytes']>limit:raise MemoryError(f"joint array bound {bounds['total_array_bound_bytes']} exceeds effective limit {limit}")
            f_bins=np.arange(-fh,fh+1,dtype=np.int64);d_bins=np.arange(-dh,dh+1,dtype=np.int64)
            reference=args.reference_variance or ws.sample_rate/10**(args.cn0/10)
            if not math.isfinite(reference) or reference<=0:raise ValueError('reference variance unresolved')
            start=time.perf_counter()
            rr,gg,centers,upper,calibration=prepare(ws,mapping,reference,args.budget.delta_per_segment,args.projection_tile_blocks)
            preparation=time.perf_counter()-start
            truth=None
            if control=='signal':
                truth=ws.manifest.get('signal_bit_simulation_truth',ws.manifest.get('signal_bit',0))
                if isinstance(truth,bool) or truth not in (0,1):raise WorkspaceError('signal evaluation truth must be binary')
            for method in args.methods:
                ucb=method.endswith('-ucb');scale=upper if ucb else 1.;bank=int(ucb)
                candidate,timings=joint_evidence(rr[bank]/scale,gg[bank]/scale,centers,dt,nfft,f_bins,d_bins,
                    zero_mass=args.zero_ray_mass,device=args.device,precision=args.precision,
                    frequency_tile=args.frequency_tile,ray_tile=args.ray_frequency_tile)
                scores=mix_candidates(candidate,labels)
                arrays[f'path{index:05d}_{control}_{method}']=scores
                row['methods'][method]={'bit_log_scores':scores[0].tolist(),'threshold':args.budget.log_threshold,
                    'counts':adapter.classify(scores,truth,args.budget.log_threshold,control),'timings_and_grid':timings}
            row.update(status='completed',truth_bit_simulation_only=truth,reference_variance=reference,
                calibration=calibration,cpu_fp64_projection_seconds=preparation,memory=bounds,
                requested_memory_limit_bytes=args.memory_limit_bytes,effective_memory_limit_bytes=limit,
                free_gpu_bytes_at_preflight=free,sampled_seconds=duration,block_count=ws.block_count,
                selected_candidate_indices=indices,full_bank_dimension=ws.dimension,
                frequency_grid={'nfft':nfft,'spacing_hz':1/(nfft*dt),'common_bins':len(f_bins),'relative_bins':len(d_bins),
                    'common_endpoints_hz':[float(f_bins[0]/(nfft*dt)),float(f_bins[-1]/(nfft*dt))],
                    'relative_endpoints_hz':[float(d_bins[0]/(nfft*dt)),float(d_bins[-1]/(nfft*dt))],
                    'prior':'uniform common finite grid; relative zero atom plus uniform finite grid; outward endpoints',
                    'zero_ray_mass':args.zero_ray_mass,'last_partial_center_corrected':bool(centers[-1]!=(ws.block_count-.5)*dt)},
                input_hashes={name:adapter.sha256(ws.directory/name) for name in INPUT_FILES})
        except Exception as error:
            row.update(status='error',error_type=type(error).__name__,error=str(error));row['methods']={}
            for method in args.methods:arrays.pop(f"path{index:05d}_{row['control']}_{method}",None)
        captures.append(row)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    scores_path=args.output.with_suffix('.scores.npz');np.savez_compressed(scores_path,**arrays)
    configuration={k:v for k,v in vars(args).items() if k not in ('inputs','output','budget')}
    result={'schema':'triad-ray-frequency-study-v1','source_sha256':adapter.sha256(__file__),
        'dependency_sha256':{name:adapter.sha256(Path(__file__).with_name(name)) for name in
            ('triad-static-frequency-study.py','triad_noise_likelihood.py','triad_state_workspace.py','triad_gaussian_state.py')},
        'configuration':configuration,'family':args.budget.diagnostics(),'captures':captures,
        'summary_by_control':{control:{method:adapter.summary(captures,method,control) for method in args.methods}
            for control in CONTROLS if any(r['control']==control for r in captures)},
        'failed_physical_capture_records':sum(r['status']!='completed' for r in captures),
        'scores_file':scores_path.name,'scores_sha256':adapter.sha256(scores_path),'wall_seconds_joint':time.perf_counter()-started,
        'host':{'python':platform.python_version(),'numpy':np.__version__,'torch':torch.__version__,'device':args.device,'threads':args.threads},
        'arithmetic':{'correlation_ffts':args.precision,'gram_fft_diagonal_rank_residual':'FP64',
            'cholesky_triangular_solve_quadratic_and_mixtures':'FP64','phase_arguments':'FP64 then cast to transform width','tf32_allowed':False},
        'limitations':['Exact finite-grid likelihood only for retained block-constant common/relative phase and static Gaussian two-ray coefficients; fixed public tap delays, no OU or general waveform guarantee.',
            'Default spacing <=1/(8T) is a rectangular coherent reference convention, not a certified finite shaped-waveform loss bound.',
            'Half-mass zero-relative prior guarantees logLR >= same-common-grid zero-relative logLR minus log(2); this is not a sensitivity-dB guarantee or comparison to a different old FFT grid.',
            'Known-reference method requires correct variance; UCB assumes one stationary white proper-complex variance and independent complete-bank residuals.',
            'Wrong-key false admissions are empirical; Gaussian noise-only qualification does not cover wrong-key signals.',
            'K16/D8 is a predeclared configurable family; additional prior/grid/model selection must be counted.',
            'Declared memory covers explicit host/device arrays and allowance; framework overhead is separate. GPU cap is at most75%of free memory.']}
    if args.device.startswith('cuda'):result['cuda_memory']={'maximum_allocated_bytes':torch.cuda.max_memory_allocated(args.device),'maximum_reserved_bytes':torch.cuda.max_memory_reserved(args.device)}
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    return result


def main(argv=None):
    args=parse_args(argv);result=run(args)
    print(json.dumps({'output':str(args.output),'captures':len(result['captures']),'failed':result['failed_physical_capture_records'],'wall_seconds':result['wall_seconds_joint']}))
    return int(bool(result['failed_physical_capture_records']))


if __name__=='__main__':raise SystemExit(main())
