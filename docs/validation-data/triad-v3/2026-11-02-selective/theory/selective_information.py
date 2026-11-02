#!/usr/bin/env python3
"""Source-aware bounds, not modem reception or physical-channel measurements.

Gaussian covariance proxies are LOWER bounds on mixture KL. The oracle and
operator inequalities below are UPPER bounds under their explicit assumptions.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tempfile

import numpy as np
from scipy.linalg import toeplitz
from scipy.special import logsumexp
from scipy.integrate import quad
from scipy.optimize import brentq

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[4]
PRIOR=HERE.parents[1]/'2026-11-02-spread'/'theory'
MAX_SECONDS=10*365.25*86400
DEPTHS=(0.,.01,.05,.1,.25,.5,.65)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def depth_parameters(slow,fast):
    if not all(math.isfinite(x) and 0<=x<=1 for x in (slow,fast)):
        raise ValueError('component amplitude depths must be finite in [0,1]')
    d=slow+fast;q=(slow*slow+fast*fast)/2
    # E[U*U], E[UU*] <= 3q I, ||U|| <= 3d, U has a symmetric law.
    # Two valid quadratic bounds; neither uses a Gaussian mixture proxy as upper bound.
    factor_a=(1+3*d)**2*(1+3*q)
    factor_b=1+18*q+27*d*d*q
    return {'slow_depth':slow,'fast_depth':fast,'sum_depth':d,'gain_minimum':1-d,
        'gain_maximum':1+d,'q':q,'stage_l2_norm_upper':1+3*d,
        'ensemble_energy_factor_upper':1+3*q,
        'quadratic_KL_multiplier_upper':min(factor_a,factor_b),
        'quadratic_bound_A':factor_a,'quadratic_bound_B':factor_b,
        'mean_coherent_fraction_lower':1/(1+3*q),
        'mean_coherent_fraction_upper':1/(1+q/3),
        'coherent_fraction_scope':'Ratio ||E_phase[A x]||²/E_phase||A x||² for fixed x and the retained finite operator; not a per-realization decoder penalty.'}


def band_matrices(count,fs,bandwidth):
    """Independent finite zero-state implementation of the inspected all-pass law."""
    if count<1 or fs<=0 or bandwidth<=0:raise ValueError('positive geometry required')
    cutoff=min(bandwidth/6,fs*.24)
    k=math.tan(math.pi*cutoff/fs);a=(k-1)/(k+1)
    identity=np.eye(count,dtype=np.complex128);filters=[]
    for center in (-bandwidth/3,bandwidth/3):
        rotation=np.exp(2j*math.pi*center/fs)
        impulse=np.zeros(count,dtype=np.complex128);previous_input=previous_output=0j
        for n in range(count):
            x=1. if n==0 else 0.
            y=a*x+rotation*(previous_input-a*previous_output)
            previous_input=x;previous_output=y;impulse[n]=y
        allpass=toeplitz(impulse,np.r_[impulse[0],np.zeros(count-1)])
        filters.append((identity+allpass)/2)
    low,high=filters
    return np.stack((low,identity-low-high,high))


def modulation_operator(bands,fs,doppler,slow,fast,phases):
    depth_parameters(slow,fast)
    count=bands.shape[-1]
    if doppler==0 or slow==fast==0:return np.eye(count,dtype=np.complex128)
    if doppler<0:raise ValueError('negative Doppler reference')
    t=np.arange(count)/fs
    gain=1+slow*np.sin(2*math.pi*.1*doppler*t[None,:]+phases[:,0,None])
    gain+=fast*np.sin(2*math.pi*doppler*t[None,:]+phases[:,1,None])
    return np.einsum('bn,bnk->nk',gain,bands)


def gaussian_kl(covariance):
    values=np.linalg.eigvalsh((covariance+covariance.conj().T)/2)
    if values.min()<-1e-11:raise ValueError('non-PSD covariance')
    values=np.maximum(values,0)
    # Stable scalar form for the small eigenvalue diagnostic.
    return float(math.fsum(sum((-1)**n*x**n/n for n in range(2,15)) if x<1e-3
        else x-math.log1p(x) for x in values))


def scalar_gaussian_mixture_kl(variances,weights):
    """Exact 1-D proper-complex radial integration for a finite covariance mixture."""
    v=np.asarray(variances,float);w=np.asarray(weights,float)
    if not np.all(v>0) or not np.all(w>0) or not np.isclose(w.sum(),1):
        raise ValueError('positive normalized covariance mixture required')
    logs=np.log(w/v)
    def integrand(u):
        log_density=float(logsumexp(logs-u/v))
        return math.exp(log_density)*(log_density+u)
    value,error=quad(integrand,0,np.inf,epsabs=1e-11,epsrel=1e-11,limit=300)
    proxy=float(np.dot(w,v)-1-math.log(float(np.dot(w,v))))
    oracle=float(np.dot(w,v-1-np.log(v)))
    return {'mixture_KL_nats':value,'quadrature_error_estimate':error,
        'same_covariance_Gaussian_KL_lower_nats':proxy,'latent_oracle_KL_upper_nats':oracle,
        'scope':'A scalar numerical inequality diagnostic, not the actual three-carrier channel.'}


def compile_probe(compiler='c++'):
    with tempfile.TemporaryDirectory(prefix='triad-selective-theory-') as temporary:
        output=Path(temporary)/'probe'
        subprocess.run([compiler,'-std=c++20','-O2','-ffp-contract=off','-fno-fast-math',
            '-I'+str(ROOT/'include'),'-I'+str(ROOT/'src'),str(HERE/'selective_probe.cpp'),
            '-o',str(output)],check=True,timeout=45,capture_output=True)
        return json.loads(subprocess.check_output([str(output)],timeout=10,text=True))


def actual_filter_check(probe):
    f=probe['filter'];n=len(f['output']);time=np.arange(n)
    x=np.sin(.37*time)+.2*np.cos(.11*time)+1j*np.cos(.23*time)
    bands=band_matrices(n,f['sample_rate'],f['bandwidth'])
    op=modulation_operator(bands,f['sample_rate'],f['doppler'],f['slow'],f['fast'],np.array(f['initial_phases']))
    expected=np.asarray(f['output']);expected=expected[:,0]+1j*expected[:,1]
    error=float(np.max(np.abs(op@x-expected)))
    if error>2e-12:raise ArithmeticError(f'actual C++ selective helper mismatch: {error}')
    return {'samples':n,'maximum_absolute_error':error,'passed':True,
        'scope':'Independent finite operator versus actual C++ FrequencySelectivePerturbation; not a sampled modem reception test.'}


def binary_kl(p,q):
    return p*math.log(p/q)+(1-p)*math.log((1-p)/(1-q))


def detection_upper(information,alpha):
    if information<0 or not 0<alpha<1:raise ValueError('invalid information or false probability')
    if information==0:return alpha
    if information>=math.log(1/alpha):return 1.
    return brentq(lambda p:binary_kl(p,alpha)-information,alpha+1e-15,1-1e-15)


def interpolation_gain(phase):
    phase%=1
    if phase==0:return 1.
    coefficients=[]
    for offset in range(-7,9):
        d=phase-offset
        if abs(d)>=8:continue
        z=(d+8)/16;window=.42-.5*math.cos(2*math.pi*z)+.08*math.cos(4*math.pi*z)
        coefficients.append((1. if abs(d)<1e-12 else math.sin(math.pi*d)/(math.pi*d))*window)
    return math.fsum(abs(c) for c in coefficients)/abs(math.fsum(coefficients))


def input_bounds(kernel,seconds,peak_power,delay_seconds):
    """Bounds for the full pre-selective sampled input interval, not active time."""
    fs=kernel['sample_rate'];dt=1/fs;h=np.asarray(kernel['coefficients'])
    r=np.correlate(h,h,'full')[len(h)-1:]
    count=math.ceil(seconds*fs)
    peak=peak_power*(max(1.,interpolation_gain(-delay_seconds*fs))+1e-9)**2
    # Independent two rays and unit-modulus deterministic phase imply absolute
    # covariance <= P_eff*|r[n-m]|, irrespective of ray center separation.
    lag=np.arange(1,min(count,len(r)),dtype=np.float64)
    correlation_sum=count*r[0]**2+2*float(np.dot(count-lag,r[1:len(lag)+1]**2))
    return {'sample_count':count,'full_preselective_span_seconds':count/fs,
        'interpolated_peak_power_upper':float(peak),'noise_normalized_trace_per_CN0':float(peak*count/fs*(r[0]+1e-9)),
        'half_trace_square_per_CN0_squared':float(.5*(dt*peak)**2*(correlation_sum*(1+1e-9)+1e-9)),
        'FIR_taps':len(h),'FIR_coefficient_sha256':hashlib.sha256(h.astype('<f8').tobytes()).hexdigest(),
        'FIR_covariance_zero':float(r[0])}


def report(compiler='c++'):
    probe=compile_probe(compiler);helper_check=actual_filter_check(probe)
    prior=json.loads((PRIOR/'transmitter_bound.json').read_text())
    # Use only the recorded source peak envelope, never its continuous-energy
    # bound as a point-sampled energy bound. Source files are re-hashed below.
    peak=prior['bounds']['absolute_peak_power_bound']
    header=ROOT/'include/datapump/pattern_pulse.hpp'
    for p in (header,ROOT/'src/triad_modem_v3.cpp'):
        if sha(p)!=prior['source_sha256'][str(p.relative_to(ROOT))]:
            raise ValueError('source synthesis changed; regenerate or independently rebind the source-peak bound')
    profiles=[{'name':'strict','pd':.99,'alpha':1e-4,'horizon_seconds':86400},
              {'name':'relaxed','pd':.9,'alpha':.01,'horizon_seconds':43200}]
    for p in profiles:p['required_binary_KL_nats']=binary_kl(p['pd'],p['alpha'])
    controls=[]
    for percent in DEPTHS:
        for mode in (['off'] if percent==0 else ['slow','fast','each']):
            slow=percent if mode in ('slow','each') else 0.
            fast=percent if mode in ('fast','each') else 0.
            controls.append({'id':mode+'-'+str(round(percent*100)),'gui_preset_id_available':percent!=.5,
                **depth_parameters(slow,fast)})
    rows=[]
    for kernel in probe['kernels']:
        for delay in (.000005,.007):
            base=input_bounds(kernel,MAX_SECONDS,peak,delay)
            for c in controls:
                linear=c['ensemble_energy_factor_upper']*base['noise_normalized_trace_per_CN0']
                quadratic=c['quadratic_KL_multiplier_upper']*base['half_trace_square_per_CN0_squared']
                upper=min(linear*1e-6,quadratic*1e-12)
                rows.append({'control':c['id'],'B_2sigma_hz':kernel['width_hz'],
                    'sample_rate_hz':kernel['sample_rate'],'excess_delay_seconds':delay,
                    'cn0_db_hz':-60,'full_preselective_span_seconds':MAX_SECONDS,
                    'oracle_mixture_KL_upper_nats':upper,
                    'targets':{p['name']:{'excluded_at_minus60':bool(upper<p['required_binary_KL_nats']),
                        'one_completion_detection_probability_upper':detection_upper(upper,p['alpha']),
                        'necessary_cn0_db_hz_at_this_span':10*math.log10(max(p['required_binary_KL_nats']/linear,
                            math.sqrt(p['required_binary_KL_nats']/quadratic)))} for p in profiles},
                    'input_bound':base})
    paths=[Path(__file__),HERE/'selective_probe.cpp',ROOT/'src/channel_perturbation.hpp',
        ROOT/'src/channel_spread.hpp',ROOT/'src/triad_gaussian_fading.hpp',ROOT/'src/triad_simulation_stream.cpp',
        ROOT/'src/triad_modem_v3.cpp',header,PRIOR/'transmitter_bound.json']
    return {'schema':'triad-selective-information-v1','source_sha256':{str(p.relative_to(ROOT)):sha(p) for p in paths},
        'source_peak_power_upper':peak,'actual_helper_check':helper_check,'controls':controls,'profiles':profiles,
        'scalar_mixture_diagnostic':scalar_gaussian_mixture_kl([1.1,3.],[.5,.5]),'ten_year_minus60_bounds':rows,
        'assumptions':['Independent uniform selective phases; independent proper-complex zero-mean Gaussian ray innovations; white proper-complex noise added after the perturbation, with known density for the bound.',
            'Finite pre-selective input support of at most ten Julian years including source pulses, delayed input and interpolation tails; this is not ten years of active payload plus unlimited tails.',
            'Integer-aligned direct source arrival and zero clock error; fixed delayed-ray interpolation phase; unit source gain; Gaussian FIR coefficients probed at each listed sample clock.',
            'Two equal-power rays; deterministic unit-modulus phase and ray-center rotations do not change the absolute covariance bound. A delayed waveform is bounded separately before ray summation.',
            'Shared zero-state all-pass selective bank is linear for fixed phases. All of its output tail is allowed in the operator bound; finite capture or deterministic postfiltering cannot increase KL when noise is transformed consistently.',
            'No per-realization or average power normalization; no physical-playback headroom attenuation; extra frontend, AGC, nonlinear or independent amplitude processes are excluded.',
            'The depth percentages are amplitudes. Each50 reaches a possible zero gain; Each65 permits negative gain. Margin changes reference frequencies/delay, not depths. Positive selected depth with Doppler reference0 is exact bypass and must use Off bounds.',
            'Bounds apply to an ideal Gaussian/uniform stochastic law, not a proof about the finite PRNG or a calibrated physical propagation environment. Numeric padding is engineering margin, not formal interval certification.'],
        'limits':['Gaussian covariance-matched KL is a lower bound for a non-Gaussian covariance mixture and cannot establish impossibility.',
            'Oracle upper bounds can exclude a declared target; failing to exclude it does not demonstrate feasibility or receiver optimality.',
            'Necessary C/N0 crossings are presence bounds for one completion using the full horizon false budget. They are not achievable decoding plans or a minute-family search allocation.',
            'No bit-discrimination, acquisition or continuous-operation receiver trial is claimed by these analytical calculations.']}


def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--compiler',default='c++');a=p.parse_args()
    a.output.write_text(json.dumps(report(a.compiler),indent=2,allow_nan=False)+'\n')

if __name__=='__main__':main()
