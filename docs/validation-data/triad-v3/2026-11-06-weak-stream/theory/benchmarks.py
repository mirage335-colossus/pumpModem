#!/usr/bin/env python3
"""Ideal known-channel and Gaussian-renewal calculations, not modem validation."""
import os
os.environ.update(OPENBLAS_NUM_THREADS='1', OMP_NUM_THREADS='1')
import argparse
import json, math, pathlib, time
import numpy as np
from scipy.special import ndtri_exp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path, required=True,
                        help='fresh report path; retained historical results are separate')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('output must not already exist')
    START=time.process_time()
    alpha_day=1e-4; attempts=216000
    atoms=14071*18107*11*13*2*2
    threshold=math.log(attempts*atoms/alpha_day)
    gamma=10**.8

    def lower_tail_chernoff(lam, level):
        # H1 logLR = sum lambda_i Exp(1)-sum log(1+lambda_i).
        a=level+np.log1p(lam).sum()
        if a<=0:return 0.
        if a>=lam.sum():return 1.
        lo,hi=0.,1.
        while (lam/(1+hi*lam)).sum()>a:hi*=2
        for _ in range(80):
            mid=(lo+hi)/2
            if (lam/(1+mid*lam)).sum()>a:lo=mid
            else:hi=mid
        s=(lo+hi)/2
        return min(1.,float(np.exp(s*a-np.log1p(s*lam).sum())))

    rows=[]
    for duration in (23.2,46.4):
        dt=.1;n=round(duration/dt);delta=(np.arange(n)[:,None]-np.arange(n)[None,:])*dt
        for law, width in [('static-gaussian',0.),('gaussian-doppler',.05),('gaussian-doppler',.5),('gaussian-doppler',2.),('ou',.5)]:
            if law=='static-gaussian':cov=np.ones((n,n))
            elif law=='ou':cov=np.exp(-abs(delta)/width)
            else:cov=np.exp(-.5*(math.pi*width*delta)**2)
            # Two observable independent equal-power rays; ideal flat unit-power
            # source with complete independent white complex-noise observations.
            one=np.linalg.eigvalsh(cov)*(gamma*.5*dt)
            lam=np.tile(np.maximum(one,0.),2)
            rows.append(dict(duration_seconds=duration,law=law,parameter=width,
                energy=float(lam.sum()),energy_db=10*math.log10(lam.sum()),
                effective_modes=float(lam.sum()**2/np.dot(lam,lam)),
                h1_loglr_mean=float((lam-np.log1p(lam)).sum()),
                h1_loglr_sd=float(np.sqrt(np.dot(lam,lam))),
                h0_loglr_mean=float((lam/(1+lam)-np.log1p(lam)).sum()),
                miss_chernoff_upper_at_daily_union_threshold=lower_tail_chernoff(lam,threshold)))
    qfalse=-float(ndtri_exp(math.log(alpha_day/(attempts*atoms))))
    qmiss=-float(ndtri_exp(math.log(.01)))
    known_energy=(qfalse+qmiss)**2/2
    report=dict(scope='Ideal calculations only: no actual keyed waveform, selective AM, Helper residual, acquisition mismatch, clock error, colored hardware noise or outages. Not demonstrated receiver sensitivity.',
        daily_alpha=alpha_day,attempts_per_day=attempts,public_atoms=atoms,
        per_atom_loglr_threshold=threshold,data_cn0_db_hz=8.,
        known_channel_fixed_signal_99pct_detection_energy=known_energy,
        known_channel_fixed_signal_99pct_detection_energy_db=10*math.log10(known_energy),
        known_channel_fixed_signal_duration_seconds=known_energy/gamma,
        known_channel_scope='Known deterministic received complex waveform; per-atom false probability alpha_day/(attempts*atoms). Gaussian NP matched-filter benchmark, no nuisance estimation.',
        renewal_grid_dt_seconds=.1,rows=rows,cpu_seconds=time.process_time()-START)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
