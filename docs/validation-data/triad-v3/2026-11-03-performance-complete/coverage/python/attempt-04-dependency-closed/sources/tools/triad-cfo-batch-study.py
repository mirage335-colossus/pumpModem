#!/usr/bin/env python3
"""Resident actual-observation batches with a deterministic CFO/Gaussian model.

This is the existing midpoint-grid RBPhaseFilter, with Gaussian tap evolution,
zero oscillator diffusion and no resampling. Every path has its own whole-bank
residual noise UCB. No hidden simulator means or noise variances enter scoring.
"""
from __future__ import annotations
import argparse
import importlib.util
import json
import math
from pathlib import Path
import sys
import time
import numpy as np
import torch
from triad_rbpf import RBPhaseFilter
from triad_state_workspace import BlockWorkspace, WorkspaceError

_SPEC=importlib.util.spec_from_file_location('triad_cfo_adapter',Path(__file__).with_name('triad-rbpf-study.py'))
adapter=importlib.util.module_from_spec(_SPEC);_SPEC.loader.exec_module(adapter)
METHOD='cfo-grid-ucb'


def parse_args(argv=None):
    parser=argparse.ArgumentParser(add_help=False)
    parser.add_argument('--batch-size',type=int,default=8)
    parser.add_argument('--projection-tile-blocks',type=int,default=128)
    extra,remaining=parser.parse_known_args(sys.argv[1:] if argv is None else argv)
    args=adapter.parse_args(['--selection-methods','16','--duration-alternatives','8','--methods','rbpf-ucb',
        '--frequency-initialization','midpoint-grid',*remaining])
    if not 1<=extra.batch_size<=256:parser.error('batch size must be in 1..256')
    if not 1<=extra.projection_tile_blocks<=1024:parser.error('projection tile blocks must be in 1..1024')
    if args.methods!=['rbpf-ucb'] or args.frequency_initialization!='midpoint-grid':
        parser.error('this tool requires rbpf-ucb and midpoint-grid')
    if args.phase_diffusion or args.frequency_random_walk or args.resampling!='none':
        parser.error('oscillator diffusion and resampling must be off')
    if args.device!='cpu' and not args.device.startswith('cuda'):parser.error('device must be cpu or cuda[:index]')
    args.batch_size=extra.batch_size
    args.projection_tile_blocks=extra.projection_tile_blocks
    return args


def geometry_signature(ws):
    # Absolute epochs and secret keys intentionally do not enter this signature.
    # Relative epoch hypotheses, tap shapes and column aliases must agree.
    return (ws.block_count,ws.sample_count,ws.block_samples,ws.sample_rate,ws.dimension,
        ws.column_mapping,json.dumps(ws.candidates,sort_keys=True),
        json.dumps(ws.manifest['column_descriptors'],sort_keys=True))


def memory_preflight(blocks,batch,candidates,taps,dimension,particles,precision,tile_blocks=128):
    resident=blocks*batch*candidates*(taps+taps*taps)*16
    cache_bound=3*resident+blocks*8*3
    bulk=8 if precision=='fp32' else 16
    core=batch*candidates*(particles*(8*taps*bulk+24*8)+16*16*taps*taps+128)
    # One path's raw list, stacked/grouped inputs, eigenvectors, projected
    # Grams and Torch expression temporaries can coexist. Conservative tile
    # allowance is additional to the resident selected-output bank above.
    tile=min(blocks,tile_blocks)
    preparation=24*tile*dimension*dimension*16+16*tile*dimension*16+tile*16*8+65536
    return {'host_resident_bytes':resident+blocks*8,'host_device_conversion_bound_bytes':cache_bound,
        'core_array_bound_bytes':core,'residual_preparation_bound_bytes':preparation,'projection_tile_blocks':tile,
        'total_array_bound_bytes':cache_bound+core+preparation}


def reference_variance(ws,args):
    value=args.reference_variance
    if value is None:
        try:value=ws.sample_rate/10**(args.cn0/10)
        except (OverflowError,ZeroDivisionError) as error:raise ValueError('reference variance unresolved') from error
    if not math.isfinite(value) or value<=0:raise ValueError('reference variance unresolved')
    return value


def prepare_batch(workspaces,mapping,args):
    blocks,batch,c,k=workspaces[0].block_count,len(workspaces),*mapping.shape
    rbank=np.empty((blocks,batch,c,k),np.complex128)
    gbank=np.empty((blocks,batch,c,k,k),np.complex128)
    elapsed=np.empty(blocks,np.float64)
    metadata=[]
    tile_limit=args.projection_tile_blocks
    for path_index,ws in enumerate(workspaces):
        reference=reference_variance(ws,args)
        residual=correction=0.;dof=valid_total=0
        block_iterator=iter(adapter.actual_blocks(ws,reference))
        for first in range(0,blocks,tile_limit):
            rows=[next(block_iterator) for _ in range(min(tile_limit,blocks-first))]
            # actual_blocks validates finite observations and the exact public
            # sample calendar before these bounded tile allocations.
            raw=np.stack([row[0] for row in rows]);grams=np.stack([row[1] for row in rows])
            energy=np.asarray([row[2] for row in rows],dtype=np.float64)
            valid=np.asarray([row[3] for row in rows],dtype=np.int64)
            delta=np.asarray([row[4] for row in rows],dtype=np.float64)
            if path_index==0:elapsed[first:first+len(rows)]=delta
            elif not np.array_equal(elapsed[first:first+len(rows)],delta):
                raise WorkspaceError('batch physical block calendars differ')
            # valid_rows is scalar in the shared residual contract. Group only
            # equal row counts, retaining independent full matrices per block.
            for valid_count in np.unique(valid):
                select=np.flatnonzero(valid==valid_count)
                projection=adapter.whole_bank_residual(torch.from_numpy(raw[select]),
                    torch.from_numpy(grams[select]),torch.from_numpy(energy[select]),int(valid_count))
                projected_r=projection.projected_correlations.numpy()
                projected_G=projection.projected_gram.numpy()
                rr=projected_r[:,mapping]
                gg=projected_G[:,mapping[:,:,None],mapping[:,None,:]]
                if not np.isfinite(rr).all() or not np.isfinite(gg).all():
                    raise ValueError('projected reference-normalized bank is nonfinite')
                rbank[first+select,path_index]=rr;gbank[first+select,path_index]=gg
                # Stable FP64 summation inside each tile/group plus compensated
                # accumulation across groups. Integer DOF never use float sums.
                subtotal=math.fsum(float(v) for v in projection.energy.numpy())
                adjusted=subtotal-correction;updated=residual+adjusted
                correction=(updated-residual)-adjusted;residual=updated
                dof+=sum(int(v) for v in projection.complex_dof.numpy())
                valid_total+=int(valid_count)*len(select)
        upper=float(adapter.variance_upper_confidence(residual,dof,args.budget.delta_per_segment))
        if not math.isfinite(upper) or upper<=0:raise ValueError('stationary noise upper bound unresolved')
        rbank[:,path_index]/=upper;gbank[:,path_index]/=upper
        if not np.isfinite(rbank[:,path_index]).all() or not np.isfinite(gbank[:,path_index]).all():
            raise ValueError('projected UCB bank is nonfinite')
        calibration={'pooled_residual_energy_over_reference':residual,'pooled_complex_dof':dof,
            'variance_upper_over_reference':upper,'valid_complex_samples':valid_total,
            'scope':'one constant white proper-complex noise variance over the whole capture; full-bank residuals'}
        metadata.append({'reference_variance':reference,'calibration':calibration})
    return rbank,gbank,elapsed,metadata


def score_batch(rbank,gbank,elapsed,args,state_limit):
    state=RBPhaseFilter(args.tap_prior,args.phase_prior,particles=args.particles,seed=args.seed,
        dtype=torch.complex64 if args.precision=='fp32' else torch.complex128,
        resampling='none',frequency_initialization='midpoint-grid',check_inputs=False,memory_limit_bytes=state_limit)
    if args.device.startswith('cuda'):torch.cuda.synchronize(args.device)
    started=time.perf_counter()
    rr=torch.as_tensor(rbank,device=args.device);gg=torch.as_tensor(gbank,device=args.device)
    if args.device.startswith('cuda'):torch.cuda.synchronize(args.device)
    transfer=time.perf_counter()-started
    started=time.perf_counter()
    for j,dt in enumerate(elapsed):state.update(rr[j],gg[j],float(dt))
    scores=state.finish().detach().cpu().numpy()
    diagnostics=state.diagnostics()
    if args.device.startswith('cuda'):torch.cuda.synchronize(args.device)
    return scores,diagnostics,{'transfer_seconds':transfer,'resident_inference_seconds':time.perf_counter()-started}


def run(args):
    torch.set_num_threads(args.threads);torch.set_float32_matmul_precision('highest')
    if args.device.startswith('cuda'):
        if not torch.cuda.is_available():raise ValueError('CUDA unavailable')
        torch.cuda.init();torch.cuda.set_device(args.device);torch.backends.cuda.matmul.allow_tf32=False
        torch.cuda.reset_peak_memory_stats(args.device)
    started=time.perf_counter();captures=[];batches=[];arrays={};signature=None;seen=set()
    paths=[str(p.resolve()) for p in args.inputs]
    if len(set(paths))!=len(paths):raise WorkspaceError('duplicate physical path')
    for first in range(0,len(args.inputs),args.batch_size):
        workspaces=[];records=[];indices=[]
        for index,path in enumerate(args.inputs[first:first+args.batch_size],first):
            row={'input':str(path),'physical_path_index':index,'control':'signal','methods':{}}
            try:
                ws=BlockWorkspace(path,max_tile_blocks=1,memory_limit_bytes=args.memory_limit_bytes)
                row['control']=ws.manifest.get('control','signal')
                if row['control'] not in ('signal','null','wrong-key'):raise WorkspaceError('unsupported actual control')
                if not ws.has_observation or not ws.has_received_energy:raise WorkspaceError('actual observations and received energy required')
                if not ws.gaussian_projection_supported:raise WorkspaceError('sampled frontend does not support this Gaussian noise model')
                current=geometry_signature(ws)
                if signature is None:signature=current
                elif current!=signature:raise WorkspaceError('batch geometry/candidate-column signature mismatch')
                identity=tuple(ws.manifest.get(k) for k in ('channel_seed','key_seed','epoch'))
                if all(v is not None for v in identity):
                    if identity in seen:raise WorkspaceError('duplicate channel/key/epoch physical identity')
                    seen.add(identity)
                workspaces.append(ws);records.append(row);indices.append(index)
            except Exception as error:
                row.update(status='error',error_type=type(error).__name__,error=str(error));captures.append(row)
        if not workspaces:continue
        batch_record={'physical_path_indices':indices}
        try:
            selected,mapping,labels=adapter.candidate_mapping(workspaces[0],args.taps)
            bounds=memory_preflight(workspaces[0].block_count,len(workspaces),len(mapping),args.taps,
                workspaces[0].dimension,args.particles,args.precision,args.projection_tile_blocks)
            effective_limit=args.memory_limit_bytes
            free_gpu=None
            if args.device.startswith('cuda'):
                free_gpu=int(torch.cuda.mem_get_info(args.device)[0]);effective_limit=min(effective_limit,int(.75*free_gpu))
            if bounds['total_array_bound_bytes']>effective_limit:
                raise MemoryError(f"batch array bound {bounds['total_array_bound_bytes']} exceeds effective limit {effective_limit}")
            t0=time.perf_counter();rbank,gbank,elapsed,metadata=prepare_batch(workspaces,mapping,args)
            preparation_seconds=time.perf_counter()-t0
            state_limit=effective_limit-bounds['host_device_conversion_bound_bytes']-bounds['residual_preparation_bound_bytes']
            candidate,diagnostics,timings=score_batch(rbank,gbank,elapsed,args,state_limit)
            for j,(ws,row,index) in enumerate(zip(workspaces,records,indices)):
                scores=adapter.mix_candidates(torch.from_numpy(candidate[j]),labels)
                # Truth is consulted only for signal evaluation after scoring.
                truth=None
                if row['control']=='signal':
                    truth=ws.manifest.get('signal_bit_simulation_truth',ws.manifest.get('signal_bit',0))
                    if isinstance(truth,bool) or truth not in (0,1):raise WorkspaceError('signal evaluation truth must be binary')
                arrays[f"path{index:05d}_{row['control']}_{METHOD}"]=scores
                row['methods'][METHOD]={'bit_log_scores':scores[0].tolist(),'threshold':args.budget.log_threshold,
                    'counts':adapter.classify(scores,truth,args.budget.log_threshold,row['control'])}
                row.update(status='completed',truth_bit_simulation_only=truth,**metadata[j],
                    sampled_seconds=ws.sample_count/ws.sample_rate,block_count=ws.block_count,
                    selected_candidate_indices=selected,full_bank_dimension=ws.dimension,
                    input_hashes={name:adapter.sha256(ws.directory/name) for name in adapter.INPUT_FILES})
            batch_record.update(status='completed',batch_count=len(workspaces),memory=bounds,
                requested_memory_limit_bytes=args.memory_limit_bytes,effective_memory_limit_bytes=effective_limit,
                free_gpu_bytes_at_preflight=free_gpu,preparation_seconds=preparation_seconds,
                diagnostics=diagnostics,**timings)
        except Exception as error:
            batch_record.update(status='error',error_type=type(error).__name__,error=str(error))
            for row in records:
                row.update(status='error',error_type=type(error).__name__,error=str(error));row['methods']={}
                arrays.pop(f"path{row['physical_path_index']:05d}_{row['control']}_{METHOD}",None)
        captures.extend(records);batches.append(batch_record)
        # No memory mappings are used by the actual-only adapter reader.
        del workspaces
    captures.sort(key=lambda r:r['physical_path_index'])
    args.output.parent.mkdir(parents=True,exist_ok=True)
    scores_path=args.output.with_suffix('.scores.npz');np.savez_compressed(scores_path,**arrays)
    config={k:v for k,v in vars(args).items() if k not in ('inputs','output','budget','tap_prior','phase_prior')}
    config['coherence']='infinity' if math.isinf(args.coherence) else args.coherence
    config['tap_powers']=list(args.tap_prior.stationary_power)
    result={'schema':'triad-cfo-batch-study-v1','source_sha256':adapter.sha256(__file__),
        'dependency_sha256':{n:adapter.sha256(Path(__file__).with_name(n)) for n in ('triad-rbpf-study.py','triad_rbpf.py','triad_gaussian_state.py','triad_noise_likelihood.py','triad_state_workspace.py')},
        'configuration':config,'family':args.budget.diagnostics(),'captures':captures,'batches':batches,
        'summary_by_control':{control:{METHOD:adapter.summary(captures,METHOD,control)} for control in ('signal','null','wrong-key') if any(r['control']==control for r in captures)},
        'failed_physical_capture_records':sum(r['status']!='completed' for r in captures),
        'scores_file':scores_path.name,'scores_sha256':adapter.sha256(scores_path),'wall_seconds_joint':time.perf_counter()-started,
        'arithmetic':{'bulk_particle_products':args.precision,'residual_projection_gram_covariance':'FP64','phase_evidence_reductions':'FP64','tf32_allowed':False,'input_validation':'bounded tiled whole-bank projection once per block, stationary UCB applied after compensated residual accumulation; core finish diagnostics retained'},
        'limitations':['Deterministic normalized midpoint CFO prior and Gaussian tap dynamics; zero oscillator phase/frequency diffusion and no resampling.',
            'Each physical path has one stationary white proper-complex noise UCB from its actual whole-bank residual; arbitrary colored/time-varying noise is not calibrated.',
            'Unknown initial phase is integrated by the circular Gaussian tap prior; no phase-tracking ability is implied.',
            'K16/D8 is a configurable predeclared family; additional receiver/grid/prior/duration selection must be counted.',
            'Wrong-key rejection is empirical and is not covered by the Gaussian noise-only false-admission bound.',
            'Memory bound covers explicit host/device arrays and conversion allowance; framework overhead is separate. GPU effective cap is at most75%of reported free memory.',
            'Same geometry and relative candidate bank required; absolute epochs and key identities may differ. No resampling or silent calendar alignment.']}
    if args.device.startswith('cuda'):result['cuda_memory']={'maximum_allocated_bytes':torch.cuda.max_memory_allocated(args.device),'maximum_reserved_bytes':torch.cuda.max_memory_reserved(args.device)}
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    return result


def main(argv=None):
    args=parse_args(argv);result=run(args)
    print(json.dumps({'output':str(args.output),'captures':len(result['captures']),'failed':result['failed_physical_capture_records'],'wall_seconds':result['wall_seconds_joint']}))
    return int(bool(result['failed_physical_capture_records']))


if __name__=='__main__':raise SystemExit(main())
