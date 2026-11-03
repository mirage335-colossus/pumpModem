#!/usr/bin/env python3
"""Fixed equal-energy duration/power comparison for the favorable phase teacher."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import time

import numpy as np
from scipy.stats import norm
import torch

ROOT=Path(__file__).resolve().parents[5]
sys.path.insert(0,str(ROOT/'tools'))
from triad_phase_frequency import PhaseFrequencyLikelihood,PhaseFrequencyModel
from diagnostic import analytic_scores,classify


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--trials',type=int,default=128)
    p.add_argument('--seed',type=int,default=803011)
    a=p.parse_args()
    if a.output.exists() or a.output.with_suffix('.scores.npz').exists():
        raise ValueError('refusing existing evidence outputs')
    if not 1<=a.trials<=512:raise ValueError('trials outside bounded comparison')
    torch.set_num_threads(1);torch.set_num_interop_threads(1)
    torch.backends.cuda.matmul.allow_tf32=False;torch.set_float32_matmul_precision('highest')
    sigma=math.hypot(.05,.05)*math.pi/180;tau=2/sigma**2
    short_seconds=850*86400.;n=math.ceil(short_seconds/(.0125*tau));dt=short_seconds/n
    # Independent streams preserve exactly the same phase/noise prefix between
    # arms. The long arm continues the same physical path; it does not repeat it.
    phase_rng=np.random.default_rng(np.random.SeedSequence([a.seed,1]))
    noise_rng=np.random.default_rng(np.random.SeedSequence([a.seed,2]))
    phase=phase_rng.uniform(-math.pi,math.pi,(1,2*a.trials))+np.concatenate([
        np.zeros((1,2*a.trials)),np.cumsum(phase_rng.normal(0,sigma*math.sqrt(dt),
        (2*n-1,2*a.trials)),axis=0)],axis=0)
    noise=(noise_rng.normal(size=(2*n,2*a.trials,2))+1j*noise_rng.normal(size=(2*n,2*a.trials,2)))/math.sqrt(2)
    truth=np.arange(a.trials)%2
    alpha={'strict':.00005/(2*1440*8*16),'relaxed':.005/(2*720*8*16)}
    gates={k:-math.log(v) for k,v in alpha.items()}
    targets={'strict':.99,'relaxed':.90}
    bounds={}
    for key in gates:
        z=norm.ppf(targets[key]);theta=gates[key]
        bounds[key]=dict(per_candidate_alpha=alpha[key],target_detection=targets[key],
            known_waveform_Neyman_Pearson_energy=.5*(norm.isf(alpha[key])+z)**2,
            known_waveform_fixed_LR_gate_energy=((math.sqrt(2)*z+math.sqrt(2*z*z+4*theta))/2)**2)
    rows=[];raw={}
    for name,count,c in [('double_duration',2*n,1e-6),('double_power',n,2e-6)]:
        energy=c*dt;r=math.sqrt(energy)*noise[:count].copy()
        r[:,np.arange(a.trials),truth]+=energy*np.exp(1j*phase[:count,:a.trials])
        scores=analytic_scores(r,phase[:count],energy,dt,tau)
        model=PhaseFrequencyModel(phase_states=32,frequency_grid_hz=(0.,),frequency_model='static',
            nominal_dt_seconds=dt,phase_diffusion_rad_per_sqrt_second=sigma,
            amplitude_nodes=(1.,),amplitude_weights=(1.,),relative_phase_states=1,ray_amplitudes=(1.,))
        state=PhaseFrequencyLikelihood(2*a.trials,2,1,model,dtype=torch.float32,validate_inputs=False)
        gram=torch.full((2,1,1),energy,dtype=torch.complex64)
        start=time.perf_counter()
        with torch.inference_mode():
            for k,obs in enumerate(r):
                state.update(torch.as_tensor(obs[...,None],dtype=torch.complex64),gram,0. if k==0 else dt)
        scores['finite_phase_32']=state.scores().numpy()
        row=dict(arm=name,days=count*dt/86400,cn0_db_hz=10*math.log10(c),c_tau=c*tau,
            energy_snr=count*energy,blocks=count,dt_seconds=dt,phase_wall_seconds=time.perf_counter()-start,
            outcomes={method:{gate:classify(value,truth,threshold) for gate,threshold in gates.items()}
                      for method,value in scores.items()})
        rows.append(row)
        raw.update({name+'_'+method:value for method,value in scores.items()})
        print(json.dumps(dict(arm=name,phase=row['outcomes']['finite_phase_32'],wall_seconds=row['phase_wall_seconds'])),flush=True)
    result=dict(schema='triad-equal-energy-phase-v1',seed=a.seed,source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        dependency_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
            [Path(__file__).with_name('diagnostic.py'),ROOT/'tools/triad_phase_frequency.py']},
        paired_paths=a.trials,thresholds=gates,known_waveform_benchmarks=bounds,rows=rows,
        assumptions=['Controlled orthogonal-pattern, unit-gain, known-alignment Brownian-block teacher; not sampled triad propagation.',
            'Long arm continues the identical phase/noise prefix to twice duration; high-power arm doubles signal power, not noise.',
            'Both arms have energy SNR146.88; c*tau differs by two. Pairing does not create independent evidence between arms.',
            'interval95 fields are two one-sided95% Clopper-Pearson bounds, not two-sided95% intervals.',
            'Known-waveform bound uses the same operational per-candidate alpha as the gate, not one-completion alpha1e-4.',
            'Neyman-Pearson presence bound and conservative fixed-LR gate benchmark differ; neither is an unknown-phase achievability claim.'])
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    np.savez_compressed(a.output.with_suffix('.scores.npz'),**raw)


if __name__=='__main__':main()
