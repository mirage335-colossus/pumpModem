#!/usr/bin/env python3
"""Resident CPU/GPU batch of the existing narrow flat-channel phase diagnostic.

All observations are actual noisy exports. This wrapper changes placement and
batching only; it does not implement a new phase inference algorithm.
"""
import argparse
import hashlib
from importlib.metadata import PackageNotFoundError,version
import json
import math
from pathlib import Path
import sys
import time

PROCESS_START=time.perf_counter()
import numpy as np
import torch

ROOT=Path(__file__).resolve().parents[5]
sys.path.insert(0,str(ROOT/'tools'))
from triad_phase_frequency import PhaseFrequencyLikelihood,PhaseFrequencyModel
from triad_state_workspace import BlockWorkspace
from diagnostic import phase_integral,interval
from score_actual import validate_actual_workspace


def load_inputs(paths,memory_mib):
    workspaces=[BlockWorkspace(p) for p in paths]
    if not workspaces:raise ValueError('at least one workspace is required')
    first=workspaces[0];n=first.block_count;b=len(workspaces)
    estimate=n*b*2*24*4+n*8*4
    if estimate>memory_mib*1024**2:raise ValueError('resident input workspace exceeds explicit memory limit')
    r=np.empty((n,b,2,1),dtype=np.complex128)
    g=np.empty((n,b,2,1,1),dtype=np.float64)
    dt=np.empty(n,dtype=np.float64);centers=np.empty(n,dtype=np.float64)
    descriptions=[]
    for j,w in enumerate(workspaces):
        mapping=validate_actual_workspace(w)
        if (w.block_count,w.block_samples,w.sample_count,w.sample_rate)!=(n,first.block_samples,first.sample_count,first.sample_rate):
            raise ValueError('batch requires identical complete public observation calendars')
        for k,block in enumerate(w.iter_observation()):
            if j==0:dt[k]=block.dt_seconds;centers[k]=block.center_seconds
            elif dt[k]!=block.dt_seconds or centers[k]!=block.center_seconds:
                raise ValueError('batch block centers differ; cannot share transition calendar')
            r[k,j,:,0]=block.r[0,mapping]
            g[k,j,:,0,0]=block.gram[mapping,mapping].real
        descriptions.append(dict(workspace=str(w.directory),manifest_sha256=hashlib.sha256(w.manifest_path.read_bytes()).hexdigest(),
            control=w.manifest['control'],bit=int(w.manifest['signal_bit']),sample_count=w.sample_count,
            sample_rate=w.sample_rate,noise_variance=w.noise_variance))
    digest=hashlib.sha256()
    for x in (r,g,dt,centers):digest.update(x.tobytes())
    return r,g,dt,centers,descriptions,digest.hexdigest(),estimate


def analytic(r,g,centers,sigma):
    rr=r[...,0];gg=g[...,0,0];tau=2/sigma**2
    scores={'global_unknown_phase':phase_integral(rr.sum(0),gg.sum(0))}
    for ratio in (.25,1.,4.):
        groups=np.floor(centers/(ratio*tau)).astype(np.int64)
        first=np.r_[0,np.flatnonzero(np.diff(groups))+1]
        scores[f'segmented_{ratio:g}_tau']=phase_integral(np.add.reduceat(rr,first,axis=0),np.add.reduceat(gg,first,axis=0)).sum(0)
    return scores


def infer(r,g,dt,*,sigma,states,device,precision,implementation,memory_mib,resident_bound_bytes=None):
    memory_bytes=memory_mib*1024**2
    resident_bound_bytes=(4*(r.nbytes+g.nbytes+dt.nbytes) if resident_bound_bytes is None else resident_bound_bytes)
    state_budget=memory_bytes-resident_bound_bytes
    if state_budget<=0:raise ValueError('resident inputs leave no receiver-state memory budget')
    device=torch.device(device)
    dtype=torch.float32 if precision=='fp32' else torch.float64
    ctype=torch.complex64 if precision=='fp32' else torch.complex128
    model=PhaseFrequencyModel(phase_states=states,frequency_grid_hz=(0.,),frequency_model='static',
        nominal_dt_seconds=float(dt[1]) if len(dt)>1 else 1.,phase_diffusion_rad_per_sqrt_second=sigma,
        amplitude_nodes=(1.,),amplitude_weights=(1.,),relative_phase_states=1,ray_amplitudes=(1.,))
    kind=PhaseFrequencyLikelihood
    if implementation!='reference':
        from triad_phase_optimized import OptimizedPhaseFrequencyLikelihood,GraphPhaseFrequencyLikelihood
        kind=GraphPhaseFrequencyLikelihood if implementation=='graph' else OptimizedPhaseFrequencyLikelihood
    start=time.perf_counter()
    rr=torch.as_tensor(r,dtype=ctype,device=device);gg=torch.as_tensor(g,dtype=ctype,device=device)
    state=kind(r.shape[1],2,1,model,device=device,dtype=dtype,validate_inputs=False,
               memory_limit_bytes=state_budget)
    if device.type=='cuda':torch.cuda.synchronize(device)
    prepared=time.perf_counter()-start
    start=time.perf_counter()
    with torch.inference_mode():
        for k,step in enumerate(dt):state.update(rr[k],gg[k],float(step))
    if device.type=='cuda':torch.cuda.synchronize(device)
    update=time.perf_counter()-start
    diagnostic=state.diagnostics()
    state_bound=diagnostic.get('graph_peak_working_bytes_estimate',diagnostic['working_bytes_bound'])
    return state.scores().cpu().numpy(),dict(preparation_and_transfer_seconds=prepared,
        synchronized_update_seconds=update,update_timing_scope='resident input indexing, all updates and final device synchronization; includes first graph warmup/capture',
        memory_budget=dict(total_bytes=memory_bytes,resident_estimate_bytes=resident_bound_bytes,
            remaining_state_budget_bytes=state_budget,state_estimate_bytes=state_bound,
            combined_estimate_bytes=resident_bound_bytes+state_bound,
            scope='conservative owned input/state/temporary bounds; excludes framework runtime and allocator caches'),
        diagnostics=diagnostic)


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('workspaces',type=Path,nargs='+')
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--device',default='cpu')
    p.add_argument('--precision',choices=['fp32','fp64'],default='fp32')
    p.add_argument('--implementation',choices=['reference','optimized','graph'],default='reference')
    p.add_argument('--phase-states',type=int,default=32)
    p.add_argument('--sigma',type=float,default=math.hypot(.05,.05)*math.pi/180)
    p.add_argument('--memory-mib',type=int,default=512)
    a=p.parse_args(argv)
    if a.output.exists():raise ValueError('output exists; choose a fresh evidence path')
    if a.implementation=='graph' and not a.device.startswith('cuda'):raise ValueError('CUDA graph requires explicit CUDA device')
    if not math.isfinite(a.sigma) or a.sigma<=0 or a.memory_mib<1:raise ValueError('invalid sigma or memory bound')
    if len(a.workspaces)>64:raise ValueError('batch capped at64 independent captures')
    torch.set_num_threads(1);torch.set_num_interop_threads(1)
    torch.backends.cuda.matmul.allow_tf32=False;torch.set_float32_matmul_precision('highest')
    started=time.perf_counter()
    r,g,dt,centers,descriptions,input_hash,estimate=load_inputs(a.workspaces,a.memory_mib)
    loading=time.perf_counter()-started
    started=time.perf_counter();scores=analytic(r,g,centers,a.sigma);analytic_seconds=time.perf_counter()-started
    phase,timing=infer(r,g,dt,sigma=a.sigma,states=a.phase_states,device=a.device,precision=a.precision,
        implementation=a.implementation,memory_mib=a.memory_mib,resident_bound_bytes=estimate)
    scores['finite_phase']=phase
    gates={'strict':math.log(2*1440*8*16/.00005),'relaxed':math.log(2*720*8*16/.005)}
    for j,description in enumerate(descriptions):
        description['methods']={}
        for method,values in scores.items():
            row=values[j];winner=int(row.argmax());control=description['control'];truth=description['bit']
            outcomes={gate:('false_admission' if control=='null' else 'correct_accepted' if winner==truth else 'wrong_accepted')
                if row.max()>=threshold else ('null_rejected' if control=='null' else 'miss') for gate,threshold in gates.items()}
            description['methods'][method]=dict(scores=row.tolist(),winner=winner,outcomes=outcomes)
    summary={}
    ns=sum(d['control']=='signal' for d in descriptions);nn=len(descriptions)-ns
    for method in scores:
        summary[method]={}
        for gate in gates:
            outcomes=[d['methods'][method]['outcomes'][gate] for d in descriptions]
            counts={name:outcomes.count(name) for name in ('correct_accepted','wrong_accepted','miss','false_admission','null_rejected')}
            if ns:counts['correct_two_one_sided_95_bounds']=interval(counts['correct_accepted'],ns)
            if nn:counts['false_admission_two_one_sided_95_bounds']=interval(counts['false_admission'],nn)
            summary[method][gate]=counts
    dependencies=[Path(__file__),Path(__file__).with_name('score_actual.py'),Path(__file__).with_name('diagnostic.py'),
        ROOT/'tools/triad_state_workspace.py',ROOT/'tools/triad_phase_frequency.py']
    if a.implementation!='reference':dependencies.append(ROOT/'tools/triad_phase_optimized.py')
    try:triton_version=version('triton')
    except PackageNotFoundError:triton_version=None
    result=dict(schema='triad-resident-actual-phase-v1',failed_physical_path_records=0,device=a.device,precision=a.precision,implementation=a.implementation,
        sigma=a.sigma,phase_states=a.phase_states,prepared_input_sha256=input_hash,inputs=descriptions,
        source_sha256={str(x.relative_to(ROOT)):hashlib.sha256(x.read_bytes()).hexdigest() for x in dependencies},
        thresholds=gates,summary=summary,signal_paths=ns,null_paths=nn,
        timings=dict(workspace_load_validate_seconds=loading,analytic_baseline_seconds=analytic_seconds,
            **timing,process_seconds_before_json_write=time.perf_counter()-PROCESS_START),
        resident_host_device_estimate_bytes=estimate,
        environment=dict(torch=torch.__version__,numpy=np.__version__,cuda=torch.version.cuda,triton=triton_version,
            gpu=torch.cuda.get_device_name(torch.device(a.device)) if a.device.startswith('cuda') else None),
        limitations=['Actual noisy complex observations; no replacement noise or simulator signal means are consumed.',
            'The injected Gaussian noise variance is known from the export manifest and normalizes observations/Grams. This is not an unknown-noise calibrated receiver.',
            'Known unit gain, known arrival equal to public capture lead, zeroCFO/clock/epoch, one flat tap. Not general physical oscillator acquisition.',
            'Each case retains its own candidate Gram and actual noise; batches require identical complete public calendars.',
            'Finite phase model is block-constant; it is not exact within-block Brownian waveform inference.',
            'FP32 bulk retains FP64 log-evidence reductions and public input preparation. TF32 disabled.',
            'Process time includes imports but excludes interpreter startup and JSON writing; runner external wall time is the full-process comparison.',
            'No GPU speedup is claimed until same-input paired backend results are measured.'])
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    print(json.dumps(dict(summary=summary,timings={k:v for k,v in result['timings'].items() if k!='diagnostics'})),flush=True)


if __name__=='__main__':main()
