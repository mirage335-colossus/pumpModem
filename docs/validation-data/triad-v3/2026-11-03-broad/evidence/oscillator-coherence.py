#!/usr/bin/env python3
"""Cheap oscillator/coherent-integration references, not receiver qualification.

Uses only the standard library. Native configure calls do not generate samples.
The optional Feynman--Kac calculation is explicitly for a flat constant-power
Brownian-phase channel, not a Gaussian fading substitution or triad exclusion.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[5]
SECONDS = 850*86400
CN0 = 1e-6


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


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--binary',type=Path,default=ROOT/'build/dev-cli/triad_long')
    a=p.parse_args()
    if a.output.exists():raise ValueError('choose a fresh output path')
    preset_file=ROOT/'src/tuning_presets.cpp'
    text=preset_file.read_text()
    presets={}
    for name in ('crystal','gpsdo-xo','gpsdo-tcxo','gpsdo-ocxo'):
        match=re.search(r'OscillatorPreset\{"'+re.escape(name)+r'","[^"]+",([^,]+),([^},]+)',text)
        if not match:raise ValueError('native preset not found: '+name)
        ppm,degrees=map(float,match.groups())
        sigma=math.hypot(degrees,degrees)*math.pi/180
        tau=2/(sigma*sigma);x=SECONDS/tau;factor=retention(x)
        presets[name]=dict(relative_accuracy_ppm=ppm,per_selected_class_phase_degrees_sqrt_s=degrees,
            relative_phase_sigma_rad_sqrt_s=sigma,coherence_tau_seconds=tau,coherence_tau_days=tau/86400,
            x_850_days=x,untracked_full_span_retention=factor,retention_db=10*math.log10(factor),
            energy_per_coherence_time_at_minus60=CN0*tau,
            untracked_full_span_mean_projected_energy_snr=CN0*SECONDS*factor,
            optional_brownian_phase_information_reference=phase_information_reference(sigma,SECONDS,CN0))
    base=[str(a.binary),'--mode','configure','--rate','.001','--added','1',
        '--seconds',str(SECONDS),'--rf-hz','5800000000','--nominal-if-hz','.0005',
        '--spread-preset','none','--selective-perturbation','off','--margin','1',
        '--clock-sync','GPS_10ms-1ms_region-0ms_offset','--audio-error-ms','70',
        '--cn0','-60','--block-seconds','1024']
    capture=[]
    for name,fs in (('gpsdo-ocxo',2),('gpsdo-xo',2),('crystal',2000000)):
        argv=base+['--sample-rate',str(fs),'--base-oscillator',name,'--shift-oscillator',name]
        result=subprocess.run(argv,check=True,text=True,capture_output=True,timeout=3)
        config=json.loads(result.stdout)['configuration'];ppm=presets[name]['relative_accuracy_ppm']
        rxmin=1-ppm*.5e-6;txmax=1+ppm*.5e-6
        bandwidth=config['capture']['nominal_triplet_bandwidth_hz']
        cfo=(5.8e9*ppm+.0005*ppm)*1e-6/rxmin
        minimum=(bandwidth*txmax+2*cfo)/rxmin
        assert math.isclose(cfo,config['deterministic_oscillator_cfo_bound_hz'],rel_tol=1e-14)
        assert math.isclose(presets[name]['relative_phase_sigma_rad_sqrt_s'],
            config['relative_phase_diffusion_rad_sqrt_s'],rel_tol=1e-14)
        assert math.isclose(fs*rxmin-bandwidth*txmax-2*cfo,
            config['capture']['deterministic_nyquist_margin_hz'],rel_tol=1e-14)
        capture.append(dict(oscillator=name,argv=argv,native_sample_rate_hz=fs,
            relative_cfo_bound_hz=cfo,native_conservative_sample_rate_must_exceed_hz=minimum,
            smallest_integer_rate_above_native_margin=math.floor(minimum)+1,
            bare_850_day_samples_at_selected_rate=SECONDS*fs,
            native_source_support_seconds=config['duration']['source_support_seconds'],
            native_source_support_samples=round(config['duration']['source_support_seconds']*fs),
            native_rounded_active_seconds=config['duration']['rounded_active_seconds'],
            native_whole_bit_frequency_grid_positions=config['search_estimates']['whole_bit_frequency_positions'],
            native_materialization_cap=config['public_search']['materialization_cap'],
            native_capture_margin_hz=config['capture']['deterministic_nyquist_margin_hz'],
            native_capture_scope=config['capture']['scope'],
            scope='configure only; no sampled observation generated, no throughput or capture-tail qualification'))
    # Small independent scalar checks; no sampled-modem Monte Carlo.
    assert retention(0)==1
    for x in (1e-8,.1,1,10):
        n=20000;step=1/n
        quad=2*math.fsum((1-(k+.5)*step)*math.exp(-x*(k+.5)*step)*step for k in range(n))
        assert abs(quad-retention(x))<3e-8
    assert presets['gpsdo-ocxo']['untracked_full_span_retention']>presets['gpsdo-tcxo']['untracked_full_span_retention']>presets['gpsdo-xo']['untracked_full_span_retention']
    files=[Path(__file__),preset_file,ROOT/'tools/triad-long.cpp',ROOT/'src/triad_simulation_stream.cpp',ROOT/'src/correlation_experiment.cpp']
    report=dict(schema='triad-oscillator-coherence-reference-v1',seconds=SECONDS,cn0_db_hz=-60,
        presets=presets,capture=capture,binary_sha256=hashlib.sha256(a.binary.read_bytes()).hexdigest(),
        source_sha256={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},
        source_binary_note='Native executable hash and inspected-source hashes are separate identities; this script does not certify their build equivalence.',
        checks='four scalar quadrature checks, zero-diffusion limit, ordering and three native metadata/formula comparisons passed',
        brownian_bound_status='derived explicitly for separate flat constant-power continuous model; requires independent mathematical review, not a measured triad exclusion',
        limitations=['Untracked correlation mean energy is not an achievable reception probability or a sufficient-statistic information bound.',
            'The selected oscillator presets are illustrative stress laws, not phase-noise PSD measurements or a calibrated long-term GPSDO model.',
            'Additional FM/PM components, hop phase changes, delayed TX phase, multiple rays, selective filters, shaping and clock warp are outside the flat Brownian formula.',
            'No Gaussian-signal KL formula is applied to the constant-modulus random-phase mixture.',
            'RF sample requirements are native conservative deterministic-prior requirements; Wiener spectral tails are unbounded, and analog capture/decimation fidelity is not qualified.'])
    a.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    print(json.dumps(dict(status='analytical_and_configure_only',output=str(a.output),checks=report['checks'])))

if __name__=='__main__':main()
