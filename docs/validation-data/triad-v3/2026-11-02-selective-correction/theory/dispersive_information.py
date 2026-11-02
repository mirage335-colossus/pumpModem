#!/usr/bin/env python3
"""Corrected-operator oracle upper bounds, not reception or physical calibration."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import itertools
import json
import math
from pathlib import Path
import subprocess
import tempfile

import numpy as np

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[4]
OLD=HERE.parents[1]/'2026-11-02-selective'/'theory'
PRIOR=HERE.parents[1]/'2026-11-02-spread'/'theory'
spec=importlib.util.spec_from_file_location('legacy_selective_input_bounds',OLD/'selective_information.py')
previous=importlib.util.module_from_spec(spec);spec.loader.exec_module(previous)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def depth_parameters(slow,fast):
    if not all(math.isfinite(x) and 0<=x<=1 for x in (slow,fast)):
        raise ValueError('finite component amplitude depths in [0,1] required')
    d=slow+fast;q=(slow*slow+fast*fast)/2
    f1=(1+math.sqrt(3)*d)**2*(1+q)
    f2=1+6*q+3*d*d*q
    return dict(slow_depth=slow,fast_depth=fast,d=d,q=q,
        stage_l2_norm_upper=1+math.sqrt(3)*d,
        ensemble_energy_factor_lower=1+q/3,ensemble_energy_factor_upper=1+q,
        quadratic_bound_norm=f1,quadratic_bound_symmetric_moments=f2,
        quadratic_KL_multiplier_upper=min(f1,f2),
        legacy_quadratic_multiplier_upper=previous.depth_parameters(slow,fast)['quadratic_KL_multiplier_upper'])


def delay_matrix(count,fs,spacing,m,center=0):
    lag=m*fs/(3*spacing);floor=math.floor(lag);fraction=lag-floor
    support=math.floor(2*fs/(3*spacing))+2
    output=count+support
    result=np.zeros((output,count),np.complex128)
    if m==0:
        result[:count]=np.eye(count)
        return result
    for offset in (-1,0,1,2):
        weight=math.prod((fraction-j)/(offset-j) for j in (-1,0,1,2) if j!=offset)
        actual=floor+offset
        if actual<0 and weight:raise ValueError('noncausal requested delay')
        if weight:
            for n in range(count):result[n+actual,n]+=weight*np.exp(2j*math.pi*center*actual/fs)
    return result


def band_matrices(count,fs,spacing,center=0):
    delays=np.stack([delay_matrix(count,fs,spacing,m,center) for m in range(3)])
    phases=np.exp(2j*math.pi*np.outer(np.arange(3)-1,np.arange(3))/3)/3
    return np.einsum('bm,mnk->bnk',phases,delays),delays[0]


def modulation_operator(bands,identity,fs,doppler,slow,fast,phases):
    times=np.arange(bands.shape[-2])/fs
    excursions=slow*np.sin(2*math.pi*.1*doppler*times[None,:]+phases[:,0,None])
    excursions+=fast*np.sin(2*math.pi*doppler*times[None,:]+phases[:,1,None])
    return identity+np.einsum('bn,bnk->nk',excursions,bands)


def compile_probe(compiler='c++'):
    with tempfile.TemporaryDirectory(prefix='triad-dispersive-theory-') as temp:
        output=Path(temp)/'probe'
        subprocess.run([compiler,'-std=c++20','-O2','-ffp-contract=off','-fno-fast-math',
            '-I'+str(ROOT/'include'),'-I'+str(ROOT/'src'),str(HERE/'dispersive_probe.cpp'),
            '-o',str(output)],check=True,timeout=45,capture_output=True)
        return json.loads(subprocess.check_output([str(output)],timeout=10,text=True))


def actual_filter_check(probe):
    f=probe['filter'];n=f['input_count'];t=np.arange(n)
    x=np.sin(.37*t)+.2*np.cos(.11*t)+1j*np.cos(.23*t)
    bands,identity=band_matrices(n,f['sample_rate'],f['spacing_hz'],f['center_hz'])
    op=modulation_operator(bands,identity,f['sample_rate'],f['doppler_hz'],f['slow'],f['fast'],np.array(f['initial_phases']))
    expected=np.array(f['output']);expected=expected[:,0]+1j*expected[:,1]
    # The independent matrix conservatively reserves the same actual cubic
    # noninteger tail in this fixture; all output is compared.
    if len(expected)!=len(op):raise ArithmeticError('probe/full-tail matrix dimensions differ')
    error=float(np.max(np.abs(op@x-expected)))
    if error>2e-12:raise ArithmeticError(f'actual corrected helper mismatch: {error}')
    return dict(input_samples=n,output_samples=len(expected),maximum_absolute_error=error,passed=True,
        scope='Independent full-output cubic/DFT matrix versus actual C++ corrected helper; no modem reception claimed.')


def phase_moment_check(slow,fast):
    # Five roots per independent phase integrate all operator moments through
    # total degree four exactly (floating-point arithmetic remains approximate).
    fs=16.;doppler=.7;bands,identity=band_matrices(3,fs,3,.7)
    phases=np.array(list(itertools.product(2*math.pi*np.arange(5)/5,repeat=6))).reshape(-1,3,2)
    times=np.arange(bands.shape[-2])/fs
    excursions=slow*np.sin(2*math.pi*.1*doppler*times[None,None,:]+phases[:,:,0,None])
    excursions+=fast*np.sin(2*math.pi*doppler*times[None,None,:]+phases[:,:,1,None])
    u=np.einsum('pbn,bnk->pnk',excursions,bands);a=identity[None,:,:]+u
    rng=np.random.default_rng(1907);v=rng.normal(size=(3,3))+1j*rng.normal(size=(3,3));k=v@v.conj().T/20
    out=np.einsum('pni,ij,pmj->pnm',a,k,a.conj());squares=np.einsum('pij,pji->p',out,out).real
    euu=np.einsum('pni,pmi->nm',u,u.conj())/len(u)
    eusu=np.einsum('pni,pnj->ij',u.conj(),u)/len(u)
    p=depth_parameters(slow,fast);trace=float(np.trace(k).real);trace2=float(np.trace(k@k).real)
    stack_input=np.einsum('bni,bnj->ij',bands.conj(),bands)
    stack_output=np.einsum('bni,bmi->nm',bands,bands.conj())
    result=dict(phase_nodes=len(phases),slow_depth=slow,fast_depth=fast,
        input_stack_norm=float(np.linalg.eigvalsh(stack_input).max()),
        output_stack_norm=float(np.linalg.eigvalsh(stack_output).max()),
        eu_star_u_max_eigenvalue=float(np.linalg.eigvalsh(eusu).max()),
        eu_u_star_max_eigenvalue=float(np.linalg.eigvalsh(euu).max()),q=p['q'],
        mean_energy=float(np.trace(out,axis1=1,axis2=2).real.mean()),energy_upper=(1+p['q'])*trace,
        mean_trace_square=float(squares.mean()),trace_square_upper=p['quadratic_KL_multiplier_upper']*trace2,
        scope='Finite full-tail noncommuting covariance diagnostic; degree-four phase quadrature, not exact KL integration or rare-event calibration.')
    for field in ('input_stack_norm','output_stack_norm'):
        if result[field]>1+2e-12:raise ArithmeticError('DFT branch stack ceased to contract')
    for field in ('eu_star_u_max_eigenvalue','eu_u_star_max_eigenvalue'):
        if result[field]>p['q']+2e-12:raise ArithmeticError('second moment exceeds qI')
    if result['mean_energy']>result['energy_upper']+2e-12 or result['mean_trace_square']>result['trace_square_upper']+2e-12:
        raise ArithmeticError('checked oracle moment upper bound failed')
    return result


def necessary_span_lower(kernel,base,control,required_kl,cn0=1e-6):
    """Invert the same peak/covariance upper envelope, never an achievable
    performance law. At these very weak C/N0 values the crossing is far beyond
    finite FIR covariance length, where the covariance-square sum is affine N.
    """
    h=np.asarray(kernel['coefficients']);r=np.correlate(h,h,'full')[len(h)-1:]
    lags=np.arange(1,len(r));a=r[0]**2+2*float(np.sum(r[1:]**2));b=2*float(np.dot(lags,r[1:]**2))
    fs=kernel['sample_rate'];peak=base['interpolated_peak_power_upper']
    linear_per_row=control['ensemble_energy_factor_upper']*cn0*peak/fs*(r[0]+1e-9)
    quadratic_factor=control['quadratic_KL_multiplier_upper']*.5*(peak/fs)**2*cn0**2
    need=max(required_kl/linear_per_row,((required_kl/quadratic_factor-1e-9)/(1+1e-9)+b)/a)
    if need<len(r):raise ArithmeticError('long-span inversion crossed outside its declared affine region')
    # ceil(T*Fs) rows are allowed. A span <=(ceil(need)-1)/Fs still cannot
    # reach the necessary row count, up to the documented numerical margins.
    return max(0,math.ceil(need)-1)/fs


def report(compiler='c++'):
    probe=compile_probe(compiler);check=actual_filter_check(probe)
    transmitter=json.loads((PRIOR/'transmitter_bound.json').read_text());peak=transmitter['bounds']['absolute_peak_power_bound']
    for path in (ROOT/'include/datapump/pattern_pulse.hpp',ROOT/'src/triad_modem_v3.cpp'):
        if sha(path)!=transmitter['source_sha256'][str(path.relative_to(ROOT))]:
            raise ValueError('source synthesis changed; source peak envelope requires a new binding')
    targets=[dict(name='strict',pd=.99,alpha=1e-4),dict(name='relaxed',pd=.9,alpha=.01)]
    for target in targets:target['required_binary_KL_nats']=previous.binary_kl(target['pd'],target['alpha'])
    controls=[dict(id=name,**depth_parameters(d,d)) for name,d in [('off',0),('each-25',.25),('each-65',.65)]]
    rows=[]
    for seconds in (850*86400,10*365.25*86400):
        for kernel in probe['kernels']:
            for delay in (.000005,.007):
                base=previous.input_bounds(kernel,seconds,peak,delay)
                for control in controls:
                    linear=control['ensemble_energy_factor_upper']*base['noise_normalized_trace_per_CN0']
                    quadratic=control['quadratic_KL_multiplier_upper']*base['half_trace_square_per_CN0_squared']
                    upper=min(linear*1e-6,quadratic*1e-12)
                    rows.append(dict(control=control['id'],B_2sigma_hz=kernel['width_hz'],sample_rate_hz=kernel['sample_rate'],
                        excess_delay_seconds=delay,cn0_db_hz=-60,full_preselective_span_seconds=seconds,
                        oracle_mixture_KL_upper_nats=upper,input_bound=base,
                        targets={target['name']:dict(excluded_at_minus60=upper<target['required_binary_KL_nats'],
                            one_completion_detection_probability_upper=previous.detection_upper(upper,target['alpha']),
                            necessary_preselective_span_lower_seconds_at_minus60=necessary_span_lower(
                                kernel,base,control,target['required_binary_KL_nats']),
                            necessary_cn0_db_hz_at_this_span=10*math.log10(max(target['required_binary_KL_nats']/linear,
                                math.sqrt(target['required_binary_KL_nats']/quadratic)))) for target in targets}))
    files=[Path(__file__),HERE/'dispersive_probe.cpp',OLD/'selective_information.py',PRIOR/'transmitter_bound.json',
           ROOT/'src/channel_perturbation.hpp',ROOT/'src/triad_gaussian_fading.hpp',ROOT/'src/triad_simulation_stream.cpp',
           ROOT/'src/triad_modem_v3.cpp',ROOT/'include/datapump/pattern_pulse.hpp']
    return dict(schema='triad-dispersive-information-v1',source_sha256={str(p.relative_to(ROOT)):sha(p) for p in files},
        source_peak_power_upper=peak,actual_helper_check=check,phase_moment_checks=[phase_moment_check(.25,.25),phase_moment_check(.65,.65)],
        controls=controls,targets=targets,minus60_bounds=rows,
        assumptions=[
            'Whitened independent proper-complex Gaussian noise after the selective stage, known noise density for the oracle bound.',
            'Conditional on deterministic source/oscillator/initial-ray phase, the pre-selective signal is zero-mean proper-complex Gaussian with two independent stationary finite-FIR Gaussian ray gains.',
            'Six independently uniform ideal modulation phases, independent of Gaussian rays; identical component rates across the three branches. Not a proof about a finite seeded PRNG.',
            'Causal cubic fractional delays are L2 contractions; zero-extended finite input and all FIR output are included. No per-realization energy normalization or physical-playback gain.',
            'The reused pre-selective covariance envelope assumes integer-aligned direct arrival, zero audio-clock error and fixed delayed-ray 16-tap interpolation phase at each stated sample clock.',
            'Input spans are full pre-selective source/ray/interpolation support, not requested active payload time; all additional selective output is permitted.',
            'Positive AM reference rate; Doppler reference zero uses the Off bypass, irrespective of selected depth. Ray-center separation and deterministic phase do not affect the absolute covariance envelope.'],
        limits=[
            'A covariance-matched Gaussian is a lower KL proxy for a covariance mixture. Only the revealed-AM conditional-Gaussian oracle and explicit operator inequalities are used as upper bounds.',
            'Presence exclusion for one completion using its entire declared false-admission budget is necessary for correct-bit admission, but does not model timing/epoch acquisition, repeated daily attempts, key rejection or an achievable duration planner.',
            'The 850-day stable fixed-ray B=0 experiment has a coherent component and is outside the zero-mean renewing-Gaussian premise. These exclusions do not apply to it.',
            'Necessary span inversion holds the same stationary finite Gaussian teacher and peak envelope indefinitely. Large inferred spans are mathematical necessary conditions, not credible physical-duration predictions or achievable plans.',
            'Nonzero-clock, arbitrary fractional direct arrival, other sampling rates/teachers and measured physical propagation require separate covariance-envelope validation.',
            'No long waveform, actual reception, GPU speedup or physical-device measurement is claimed. Numeric padding is engineering margin, not formally certified interval arithmetic.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--compiler',default='c++');args=parser.parse_args()
    args.output.write_text(json.dumps(report(args.compiler),indent=2,allow_nan=False)+'\n')

if __name__=='__main__':main()
