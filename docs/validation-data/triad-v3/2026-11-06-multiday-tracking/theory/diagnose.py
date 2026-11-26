#!/usr/bin/env python3
"""Ideal block-orthogonal AWGN thought experiment, not modem qualification."""
from pathlib import Path
import argparse
import json,math,sys,time
import numpy as np
from scipy.stats import norm,ncx2
from scipy.optimize import brentq

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True,
                        help='fresh report path; retained historical results are separate')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('output must not already exist')
    started=time.process_time()
    rng=np.random.default_rng(771031)
    C=1e-6;T=38*86400.;tau=4.;E=C*T;M=int(T/tau);gamma=C*tau;n=200_000
    # Exact sufficient-statistic thought experiment for two unit-energy patterns
    # orthogonal in every block, fixed unit complex signal coefficient, complex AWGN.
    # Coherent sum and orthogonal residual energy are independent. This is not a
    # sampled-waveform or native-format test.
    z=(rng.standard_normal((2,n))+1j*rng.standard_normal((2,n)))/math.sqrt(2)
    z[0]+=math.sqrt(E)
    persistent=np.abs(z)**2*E/(1+E)-math.log1p(E)
    residual=rng.chisquare(2*(M-1),size=(2,n))/2
    reset=(np.abs(z)**2+residual)*gamma/(1+gamma)-M*math.log1p(gamma)
    alpha=1e-4
    bench={
     'energy':E,'energy_db':10*math.log10(E),'known_phase_orthogonal_correct':float(norm.cdf(math.sqrt(E))),
     'unknown_phase_orthogonal_correct':1-.5*math.exp(-E/2),
     'known_signal_presence_Pd_at_alpha':float(norm.cdf(math.sqrt(2*E)-norm.ppf(1-alpha))),
     'known_signal_E_for_99pct_presence_at_alpha':.5*(norm.ppf(1-alpha)+norm.ppf(.99))**2,
     'reset_gaussian_alt_KL':M*(gamma-math.log1p(gamma)),
     'persistent_gaussian_alt_KL':E-math.log1p(E),
     'reset_statistic_correct_MC':float((reset[0]>reset[1]).mean()),
     'persistent_statistic_correct_MC':float((persistent[0]>persistent[1]).mean()),
     'MC_trials':n,'alpha_per_one_fixed_presence_test':alpha,
     'persistent_score_quantiles':np.quantile(persistent,[.05,.5,.95],axis=1).T.tolist(),
     'reset_score_quantiles':np.quantile(reset,[.05,.5,.95],axis=1).T.tolist(),
     'scope':'Exact low-dimensional distribution for ideal block-orthogonal patterns in constant-coefficient AWGN; not native-waveform or physical-channel qualification.'}
    bench['known_signal_T_days_for_99pct_presence_at_alpha']=bench['known_signal_E_for_99pct_presence_at_alpha']/C/86400
    # Presence in the classic orthogonal-envelope max-energy detector, alpha split
    # equally between two orthogonal noise modes. This is a conservative union gate.
    threshold=math.log(2/alpha)
    reqE=brentq(lambda e:ncx2.sf(2*threshold,2,2*e)-.99,1,100)
    bench['unknown_phase_union_energy_threshold']=threshold
    bench['unknown_phase_union_E_for_true_branch_Pd99pct']=reqE
    bench['unknown_phase_union_T_days_for_true_branch_Pd99pct']=reqE/C/86400
    bench['cpu_seconds']=time.process_time()-started
    args.output.write_text(json.dumps(bench,indent=2)+'\n')
    print(json.dumps(bench,indent=2))


if __name__ == '__main__':
    main()
