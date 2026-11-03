#!/usr/bin/env python3
"""Score an actual-noisy one-tap workspace with the existing phase likelihood.

This deliberately supports only known acquisition and known unit amplitude.
It does not consume simulator signal means or phase truth. No replacement noise
draws are used. Diagnostic channel energy is read only after all decisions.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import time

import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT/'tools'))
from triad_phase_frequency import PhaseFrequencyLikelihood, PhaseFrequencyModel
from triad_state_workspace import BlockWorkspace
from diagnostic import phase_integral


def validate_actual_workspace(w):
    """Return bit-ordered columns after validating this narrow model contract."""
    if not w.gaussian_projection_supported:
        raise ValueError('Gaussian likelihood unsupported after this sampled frontend')
    if w.manifest.get('control') not in ('signal','null'):
        raise ValueError('this diagnostic categorizes only signal and null controls')
    if w.dimension != 2 or len(w.candidates) != 2 or any(len(m)!=1 for m in w.column_mapping):
        raise ValueError('requires exactly two one-tap candidate patterns')
    lead=w.manifest.get('capture_lead_seconds')
    if (not isinstance(lead,(int,float)) or not math.isfinite(lead) or
        w.manifest.get('public_timing_bounds_capture_seconds') != [lead,lead] or
        w.manifest.get('initial_physical_arrival_bounds_capture_seconds') != [lead,lead] or
        any(c['arrival_seconds']!=lead for c in w.candidates) or
        any(any(c[k]!=0 for k in ('frequency_hz','clock_ppm','frequency_drift_hz_per_second','epoch_offset_seconds')) for c in w.candidates)):
        raise ValueError('this diagnostic requires the fixed zero-offset acquisition bank')
    labels=np.array([c['bit'] for c in w.candidates]);order=np.argsort(labels)
    if set(labels)!={0,1} or any(c['taps']!=[{'delay_seconds':0,'doppler_hz':0}] for c in w.candidates):
        raise ValueError('requires both bits and one flat zero-delay/zero-Doppler tap')
    return np.array([w.column_mapping[i][0] for i in order])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('workspace', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--phase-states', type=int, nargs='+', default=[32, 64])
    p.add_argument('--sigma', type=float, default=math.hypot(.05,.05)*math.pi/180)
    p.add_argument('--precision', choices=['fp32','fp64'], default='fp32')
    a=p.parse_args()
    if a.output.exists():
        raise ValueError('output already exists; choose a fresh evidence path')
    torch.set_num_threads(1);torch.set_num_interop_threads(1)
    torch.backends.cuda.matmul.allow_tf32=False
    torch.set_float32_matmul_precision('highest')
    w=BlockWorkspace(a.workspace)
    mapping=validate_actual_workspace(w)
    dtype=torch.float32 if a.precision=='fp32' else torch.float64
    ctype=torch.complex64 if a.precision=='fp32' else torch.complex128
    states=[]
    for count in a.phase_states:
        model=PhaseFrequencyModel(phase_states=count,frequency_grid_hz=(0.,),frequency_model='static',
            nominal_dt_seconds=w.block_samples/w.sample_rate,phase_diffusion_rad_per_sqrt_second=a.sigma,
            amplitude_nodes=(1.,),amplitude_weights=(1.,),relative_phase_states=1,ray_amplitudes=(1.,))
        states.append(PhaseFrequencyLikelihood(1,2,1,model,dtype=dtype,validate_inputs=False,
            memory_limit_bytes=256*1024**2))
    tau=2/a.sigma**2
    sums=np.zeros(2,dtype=np.complex128);energies=np.zeros(2)
    seg_sums={r:np.zeros(2,dtype=np.complex128) for r in (.25,1.,4.)}
    seg_energy={r:np.zeros(2) for r in seg_sums}
    seg_scores={r:np.zeros(2) for r in seg_sums}
    seg_group={r:None for r in seg_sums}
    started=time.perf_counter()
    with torch.inference_mode():
        for block in w.iter_observation():
            r=block.r[:,mapping];g=block.gram[mapping,mapping].real
            sums+=r[0];energies+=g
            for ratio in seg_sums:
                group=math.floor(block.center_seconds/(ratio*tau))
                if seg_group[ratio] is not None and group!=seg_group[ratio]:
                    seg_scores[ratio]+=phase_integral(seg_sums[ratio],seg_energy[ratio])
                    seg_sums[ratio].fill(0);seg_energy[ratio].fill(0)
                seg_group[ratio]=group;seg_sums[ratio]+=r[0];seg_energy[ratio]+=g
            obs=torch.as_tensor(r[...,None],dtype=ctype)
            gram=torch.as_tensor(g[:,None,None],dtype=ctype)
            for state in states:state.update(obs,gram,block.dt_seconds)
    scores={'global_unknown_phase':phase_integral(sums,energies)}
    for ratio in seg_sums:
        scores[f'segmented_{ratio:g}_tau']=seg_scores[ratio]+phase_integral(seg_sums[ratio],seg_energy[ratio])
    for count,state in zip(a.phase_states,states):scores[f'finite_phase_{count}']=state.scores()[0].numpy()
    gates={'strict':math.log(2*1440*8*16/.00005),'relaxed':math.log(2*720*8*16/.005)}
    truth=int(w.manifest['signal_bit']);control=w.manifest.get('control','signal')
    result=dict(schema='triad-actual-phase-diagnostic-v1',workspace_manifest_sha256=hashlib.sha256(w.manifest_path.read_bytes()).hexdigest(),
        source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        library_sha256=hashlib.sha256((ROOT/'tools/triad_phase_frequency.py').read_bytes()).hexdigest(),
        workspace_reader_sha256=hashlib.sha256((ROOT/'tools/triad_state_workspace.py').read_bytes()).hexdigest(),
        sigma=a.sigma,thresholds=gates,sample_count=w.sample_count,elapsed_seconds=w.sample_count/w.sample_rate,
        block_count=w.block_count,block_seconds=w.block_samples/w.sample_rate,precision=a.precision,
        wall_seconds_joint=time.perf_counter()-started,control=control,truth_bit=truth,methods={})
    for name,value in scores.items():
        winner=int(np.argmax(value))
        outcomes={gate:('false_admission' if control=='null' else 'correct_accepted' if winner==truth else 'wrong_accepted')
                  if float(max(value))>=threshold else 'miss' if control!='null' else 'null_rejected'
                  for gate,threshold in gates.items()}
        result['methods'][name]=dict(scores=value.tolist(),winner=winner,outcomes=outcomes)
    result['diagnostics']=[state.diagnostics() for state in states]
    result['post_decision_simulation_only_energy_snr']=float(np.memmap(a.workspace/'signal_energy.f64',dtype='<f8',mode='r').sum()/w.noise_variance)
    result['limitations']=['One independent sampled capture is not a reception-probability measurement.',
        'Known unit gain and arrival equal to public capture lead; zero CFO/clock/epoch, flat channel. No physical oscillator profile acquisition, two rays, or added modulation.',
        'Fixed finite phase prior approximates continuous within-block Brownian phase; numerical grid comparison is not complete model validation.',
        'Gaussian noise density is known; no structured interference or empirical operational rare-event calibration.']
    a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    print(json.dumps({k:result[k] for k in ('wall_seconds_joint','methods','elapsed_seconds')}))


if __name__=='__main__':main()
