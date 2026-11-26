#!/usr/bin/env python3
"""Replay the retained finite-FIR harmonic expansion, not a receiver campaign.

Requires NumPy and SciPy. Use one BLAS/OpenMP thread. The exact public basis is
retained in harmonic-input/; noisy observations and old receiver code are unused.
Original numerical results remain unchanged in harmonic-probe.json.
"""
import argparse
import json, math, hashlib, time
from pathlib import Path
import numpy as np
from scipy.linalg import svd
from scipy.stats import gamma, ncx2
from scipy.optimize import brentq

HERE=Path(__file__).resolve().parent
HISTORICAL_REVISION="357a50970a0c6214eeaade7c5704bbc39abd4b79"
ORIGINAL_SOURCE_SHA256="19e05f8025708877d431f890e5550271b5d0db5205345e31c8f8980bfb88fc1f"
BASIS_SHA256="376f2fe4cfc30b7da313a88bf80ab7cc291b4f774c42cc713c54441a1401f09a"
MANIFEST_SHA256="96d3878332ee6868809c611b62589aac86e51afeda956628406f6082f602909c"


def build_report(basis_directory=HERE/'harmonic-input'):
    start=time.monotonic()
    p=Path(basis_directory)
    if hashlib.sha256((p/'basis.c128').read_bytes()).hexdigest()!=BASIS_SHA256:
     raise ValueError('basis does not match the preserved public-template calculation')
    if hashlib.sha256((p/'manifest.json').read_bytes()).hexdigest()!=MANIFEST_SHA256:
     raise ValueError('manifest does not match the preserved public-template calculation')
    m=json.loads((p/'manifest.json').read_text()); fs=m['sample_rate']; a=np.fromfile(p/'basis.c128',dtype='<c16').reshape(m['basis_shape'])[[2,3]]
    N=a.shape[1]; spacing=75.;D=30.; lagmax=math.floor(2*fs/(3*spacing))+2; n=N+lagmax; t=np.arange(n)/fs
    branches=[]; expanded=[]; originals=[]; singulars=[]
    for bit in range(2):
     delays=np.zeros((3,n,2),complex);delays[0,:N]=a[bit]
     for j in (1,2):
      lag=j*fs/(3*spacing);floor=math.floor(lag);f=lag-floor
      for k in (-1,0,1,2):
       w=math.prod((f-h)/(k-h) for h in (-1,0,1,2) if h!=k)
       if w:delays[j,floor+k:floor+k+N]+=w*a[bit]
     b=np.einsum('bm,mnk->bnk',np.exp(2j*np.pi*np.outer(np.arange(3)-1,np.arange(3))/3)/3,delays)
     cols=[delays[0]]
     for nu in (.1*D,D):
      for sign in (-1,1):
       for band in range(3):cols.append(b[band]*np.exp(sign*2j*np.pi*nu*t[:,None]))
     z=np.concatenate(cols,axis=1)
     u,s,_=svd(z,full_matrices=False);rank=int(np.sum(s>s[0]*1e-10));expanded.append(u[:,:rank]);singulars.append(s.tolist());branches.append(b)
     originals.append(svd(delays[0],full_matrices=False)[0][:,:2])
    rng=np.random.default_rng(784197);results=[]
    for depth in (0,.01,.25,.65):
     phase=rng.uniform(0,2*np.pi,size=(64,3,2)); theta=rng.uniform(0,2*np.pi,size=64)
     data=[]
     for bit in range(2):
      base=np.zeros((n,2),complex);base[:N]=a[bit]
      hs=np.stack((np.ones(64),np.exp(1j*theta)),axis=1)/np.sqrt(2)
      y=base@hs.T
      for band in range(3):
       gain=depth*(np.sin(2*np.pi*.1*D*t[:,None]+phase[:,band,0])+np.sin(2*np.pi*D*t[:,None]+phase[:,band,1]))
       y+=gain*(branches[bit][band]@hs.T)
      energy=np.sum(abs(y)**2,axis=0)
      p2=np.sum(abs(originals[bit].conj().T@y)**2,axis=0)
      p26=np.sum(abs(expanded[bit].conj().T@y)**2,axis=0)
      pwrong=np.sum(abs(expanded[1-bit].conj().T@y)**2,axis=0)
      data.append(dict(bit=bit,nominal_rank2_energy_fraction=[float(np.min(p2/energy)),float(np.median(p2/energy)),float(np.max(p2/energy))],expanded_projection_relative_residual_max=float(np.max(abs(energy-p26)/energy)),wrongbit_expanded_energy_fraction=[float(np.min(pwrong/energy)),float(np.median(pwrong/energy)),float(np.max(pwrong/energy))]))
     results.append(dict(each_depth=depth,cases=data))
    thresholds=[]
    for rank in (1,2,26,72,960):
     for pd,alpha in ((.99,1e-4),(.9,.01)):
      thresh=gamma.isf(alpha,rank)
      energy=brentq(lambda e:ncx2.sf(2*thresh,2*rank,2*e)-pd,0,10000)
      thresholds.append(dict(rank=rank,pd=pd,alpha=alpha,required_projected_energy_snr=energy))
    report=dict(schema='triad-harmonic-public-basis-diagnostic-v1',scope='Independent finite cubic-FIR harmonic expansion of retained public shaped templates; no actual channel observation or acquisition. No new physical receiver qualification.',basis_sha256=hashlib.sha256((p/'basis.c128').read_bytes()).hexdigest(),basis_shape=m['basis_shape'],used_candidates=[2,3],sample_rate=fs,spacing=spacing,D=D,input_samples=N,full_output_samples=n,columns=26,rank=[q.shape[1] for q in expanded],smallest_relative_singular=[s[-1]/s[0] for s in singulars],maximum_interbit_canonical_correlation=float(svd(expanded[0].conj().T@expanded[1],compute_uv=False)[0]),samples_per_bit_depth=64,results=results,ideal_known_noise_projector_presence_thresholds=thresholds,elapsed_seconds=time.monotonic()-start)
    report['recovery_provenance']=dict(historical_revision=HISTORICAL_REVISION,
     original_source_sha256=ORIGINAL_SOURCE_SHA256,
     source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
     manifest_sha256=MANIFEST_SHA256,
     adaptation='Explicit portable input/output paths and input identity checks; original mathematical operations retained.')
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--basis-directory',type=Path,default=HERE/'harmonic-input')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.output.exists():
        parser.error('choose a fresh output path; the historical result must remain unchanged')
    report=build_report(args.basis_directory)
    args.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    print(json.dumps(dict(status='analytical_template_diagnostic',output=str(args.output),
                         rank=report['rank'],elapsed_seconds=report['elapsed_seconds'])))


if __name__=='__main__':main()
