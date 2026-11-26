"""Independent historical Gaussian-covariance diagnostic, not a receiver.

Adapted from archive member model-reference.py: explicit output and trial budget,
importable functions, minimal mathematical dependencies and honest provenance.
The original 100,000-trial result is retained unchanged beside this script.
"""
import argparse
import hashlib
import json
from pathlib import Path
import time

import numpy as np
from scipy.linalg import toeplitz,eigvalsh
from scipy.stats import beta

from historical_model import reference_threshold,gaussian_prediction,effective_modes

HERE=Path(__file__).resolve().parent
SEED=20261126000
POINTS=((8.,23.2),(8.,34.8),(8.,46.4),(5.,34.8),(5.,81.2),(5.,92.8))
HISTORICAL_SOURCE_SHA256={
    'model-reference.py':'713bb8f008e8470ab49af0c526776958de4f3b7fb649ba9997e488d93b0cd14f',
    'frozen-duration-model.py':'7ab98c534f0c83b9f57894cda457706ec3c0b41f3f00919be168424df07fed06',
    'source-current/tools/triad_operational_admission.py':'9cc99ce26c445c4c87d72600fc621b60ff9d0b61da9bed3e4f3174272904d0ea',
    'hf-native-plan.json':'327f602ebb2bbc88b01652c618d3aa009763a535c93b752bfc2867ba16d1f57b',
    'model-reference.json':'a7cb8e129eaa7f12ade2f8dc1f591e0eb28890f88f4a8760e498148fe0a83340',
}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_plan():
    path=HERE/'hf-native-plan.json'
    if sha(path)!=HISTORICAL_SOURCE_SHA256[path.name]:
        raise ValueError('historical native-plan fixture changed; rebind the model explicitly')
    return json.loads(path.read_text())


def reference_eigenvalues(p,cn,t):
    # Original 512-point per-ray discretization and strict eigenvalue cutoff.
    n=512;dt=t/n;lag=np.arange(n)*dt
    corr=np.exp(-.5*(np.pi*.5*lag)**2-.5*p['physics']['relative_phase_diffusion_rad_sqrt_s']**2*lag)
    e=np.maximum(0,eigvalsh(toeplitz(corr),check_finite=False))*dt*10**(cn/10)/2
    e=np.tile(e,2);e=e[e>1e-11]
    return e


def sampled_misses(e,q,trials,rng):
    # Keep original chunk size, draw order and likelihood arithmetic. A smaller
    # budget changes later points' RNG positions and does not replay 100k results.
    misses=0
    for off in range(0,trials,2000):
        scores=rng.exponential(size=(min(2000,trials-off),len(e)))@e-np.log1p(e).sum()
        misses+=int(np.count_nonzero(scores<q))
    return misses


def miss_interval(misses,trials):
    return [float(beta.ppf(.025,misses,trials-misses+1)) if misses else 0.,
            float(beta.ppf(.975,misses+1,trials-misses)) if misses<trials else 1.]


def report(trials=0):
    if isinstance(trials,bool) or not isinstance(trials,int) or not 0<=trials<=100000:
        raise ValueError('trial budget must be an integer in [0,100000]')
    p=load_plan();rows=[];rng=np.random.default_rng(SEED);start=time.process_time()
    for cn,t in POINTS:
        q=reference_threshold(p)
        e=reference_eigenvalues(p,cn,t)
        modes=effective_modes(t,.5,p['physics']['relative_phase_diffusion_rad_sqrt_s'],2)
        pred=gaussian_prediction(t*10**(cn/10),modes,q)
        misses=sampled_misses(e,q,trials,rng) if trials else None
        ci=miss_interval(misses,trials) if trials else None
        rows.append(dict(cn0=cn,data_seconds=t,threshold=q,prediction=pred,
            reference_eigenvalues=len(e),energy_trace=float(e.sum()),
            likelihood_mean=float((e-np.log1p(e)).sum()),misses=misses,trials=trials,
            miss_probability_ci95=ci))
    return dict(schema='triad-duration-Gaussian-reference-recovered-v1',
        scope='Discretized known covariance with two independently resolved Gaussian-spectrum rays; independent exponential draws. No waveform acquisition, selective modulation, moving delay or audio. Seeded PRNG here is statistical quadrature, not transmitted background noise.',
        seed=SEED,points=rows,cpu_seconds=time.process_time()-start,
        reference_mode='sampled' if trials else 'deterministic; no Monte Carlo draws',
        historical_archive_revision='357a50970a0c6214eeaade7c5704bbc39abd4b79',
        historical_archive_sha256='97490190ec0bd8007e468d2a906bc1a6fb1661be189867cf977e527011328251',
        historical_source_sha256=HISTORICAL_SOURCE_SHA256,
        source_sha256={name:sha(HERE/name) for name in
            ('model-reference.py','historical_model.py','hf-native-plan.json')})


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--trials',type=int,default=0,
        help='draws per condition: 0 for deterministic checks; original study used 100000')
    args=parser.parse_args()
    if args.output.resolve() in (HERE/'model-reference.json',HERE/'hf-native-plan.json'):
        parser.error('choose a fresh output path; historical input/results are preserved')
    args.output.write_text(json.dumps(report(args.trials),indent=2,allow_nan=False)+'\n')


if __name__=='__main__':main()
