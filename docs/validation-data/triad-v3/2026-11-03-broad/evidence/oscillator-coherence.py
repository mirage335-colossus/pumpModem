#!/usr/bin/env python3
"""Standalone historical oscillator/coherence mathematics (standard library).

No native binary is queried. The preserved oscillator-coherence.json retains
original configure receipts; this program reproduces only analytical values
using their explicitly frozen preset and reference parameters.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

HERE=Path(__file__).resolve().parent
SECONDS=850*86400
CN0=1e-6
HISTORICAL_REVISION="357a50970a0c6214eeaade7c5704bbc39abd4b79"
ORIGINAL_SOURCE_SHA256="23f1277f100bea31a9436b2dee12e82e872ccdea041442cf9c97439088161b04"
HISTORICAL_PRESETS={'crystal':(100.,.5),'gpsdo-xo':(.0001,.5),
                    'gpsdo-tcxo':(.0001,.05),'gpsdo-ocxo':(.0001,.005)}


def retention(x):
    if not math.isfinite(x) or x < 0: raise ValueError('finite nonnegative x required')
    if x < .01:
        return 2*math.fsum((-x)**k/math.factorial(k+2) for k in range(12))
    return 2*(1 + math.expm1(-x)/x)/x


def rayleigh_upper(kappa, variance):
    """Top eigenvalue upper bound for variance*d²/dtheta²+2*kappa*cos(theta)."""
    a=variance-2*kappa
    root=math.hypot(a,math.sqrt(8)*kappa)
    bound=4*kappa*kappa/(root+a) if a>=0 else .5*(root-a)
    return min(2*kappa,bound)


def phase_information_reference(sigma,seconds,cn0):
    variance=sigma*sigma
    eigenvalue=rayleigh_upper(cn0,variance)
    stationary_renyi=seconds*eigenvalue
    # Any point initial phase: first allow heat-kernel smoothing for one
    # dimensionless diffusion time. Weight is bounded above by exp(2*c*t0).
    t0=min(seconds,1/variance)
    a=2*variance*t0
    terms=math.fsum(math.exp(-a*n*n) for n in range(1,33))
    tail=math.sqrt(math.pi)/(2*math.sqrt(a))*math.erfc(32*math.sqrt(a))
    log_heat_norm=.5*math.log1p(2*(terms+tail))
    arbitrary_initial_renyi=min(2*cn0*seconds,
        2*cn0*t0+(seconds-t0)*eigenvalue+log_heat_norm)
    return dict(scope='flat constant-power continuous Brownian-phase channel only; no triad exclusion',
        total_energy_snr=cn0*seconds,phase_difference_generator_coefficient=variance,
        spectral_growth_upper_per_second=eigenvalue,
        uniform_initial_phase_kl_upper_nats=min(cn0*seconds,stationary_renyi),
        arbitrary_initial_phase_kl_upper_nats=min(cn0*seconds,arbitrary_initial_renyi),
        heat_kernel_smoothing_seconds=t0,heat_kernel_log_l2_norm_upper=log_heat_norm,
        numerical_status='FP64 evaluation with bounded analytic tail; no interval arithmetic certificate')


def build_report():
    presets={}
    for name,(ppm,degrees) in HISTORICAL_PRESETS.items():
        sigma=math.hypot(degrees,degrees)*math.pi/180
        tau=2/(sigma*sigma);x=SECONDS/tau;factor=retention(x)
        presets[name]=dict(relative_accuracy_ppm=ppm,per_selected_class_phase_degrees_sqrt_s=degrees,
            relative_phase_sigma_rad_sqrt_s=sigma,coherence_tau_seconds=tau,coherence_tau_days=tau/86400,
            x_850_days=x,untracked_full_span_retention=factor,retention_db=10*math.log10(factor),
            energy_per_coherence_time_at_minus60=CN0*tau,
            untracked_full_span_mean_projected_energy_snr=CN0*SECONDS*factor,
            optional_brownian_phase_information_reference=phase_information_reference(sigma,SECONDS,CN0))
    capture=[]
    for name,fs in (('gpsdo-ocxo',2),('gpsdo-xo',2),('crystal',2000000)):
        ppm=presets[name]['relative_accuracy_ppm'];rxmin=1-ppm*.5e-6;txmax=1+ppm*.5e-6
        bandwidth=.000625
        cfo=(5.8e9*ppm+.0005*ppm)*1e-6/rxmin
        minimum=(bandwidth*txmax+2*cfo)/rxmin
        capture.append(dict(oscillator=name,selected_sample_rate_hz=fs,
            relative_cfo_bound_hz=cfo,conservative_sample_rate_must_exceed_hz=minimum,
            smallest_integer_rate_above_margin=math.floor(minimum)+1,
            bare_850_day_samples_at_selected_rate=SECONDS*fs,
            deterministic_margin_hz=fs*rxmin-bandwidth*txmax-2*cfo,
            scope='Formula-only evaluation of historical deterministic reference parameters; no native configure run.'))
    assert retention(0)==1
    for x in (1e-8,.1,1,10):
        n=20000;step=1/n
        quad=2*math.fsum((1-(k+.5)*step)*math.exp(-x*(k+.5)*step)*step for k in range(n))
        assert abs(quad-retention(x))<3e-8
    assert presets['gpsdo-ocxo']['untracked_full_span_retention']>presets['gpsdo-tcxo']['untracked_full_span_retention']>presets['gpsdo-xo']['untracked_full_span_retention']
    return dict(schema='triad-oscillator-coherence-math-recovery-v1',seconds=SECONDS,cn0_db_hz=-60,
        presets=presets,capture_formula_reference=capture,
        recovery_provenance=dict(historical_revision=HISTORICAL_REVISION,
            original_source_sha256=ORIGINAL_SOURCE_SHA256,
            source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            original_result='oscillator-coherence.json retained unchanged'),
        checks='four scalar quadrature checks, zero-diffusion retention limit and preset ordering passed',
        historical_only='Original binary identity, native source-support lengths, frequency-grid counts and configure receipts are not regenerated.',
        limitations=['Mean coherent energy is not reception probability or a sufficient-statistic information bound.',
            'Frozen preset parameters are illustrative historical stress laws, not measured hardware PSDs or current presets.',
            'The optional Brownian bound covers only its flat constant-power continuous model, not a sampled-triad exclusion.',
            'Deterministic capture formulas do not qualify Brownian tails or analog capture/decimation.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.output.exists():parser.error('choose a fresh output path')
    report=build_report()
    args.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    print(json.dumps(dict(status='mathematical_reference_only',output=str(args.output),checks=report['checks'])))


if __name__=='__main__':main()
