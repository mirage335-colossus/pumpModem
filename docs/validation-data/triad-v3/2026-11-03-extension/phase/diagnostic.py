#!/usr/bin/env python3
"""Cheap phase-continuity discriminator, NOT a sampled triad experiment.

Two exactly orthogonal candidate patterns produce independent complex matched
statistics in each disjoint block. The transmitted candidate has unit gain and
block-constant Brownian phase. This teacher intentionally removes shaping,
timing/CFO search, two-ray fading and additional modulation. It tests only the
potential value of phase continuity at a declared c*tau and T/tau.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict
import hashlib
import json
import math
from pathlib import Path
import sys
import time

import numpy as np
from scipy.special import i0e
from scipy.stats import beta, norm
import torch

ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT / "tools"))
from triad_phase_frequency import PhaseFrequencyLikelihood, PhaseFrequencyModel


def interval(n, total):
    return [0. if n == 0 else float(beta.ppf(.05, n, total-n+1)),
            1. if n == total else float(beta.ppf(.95, n+1, total-n))]


def classify(scores, truth, threshold):
    n = len(truth)
    winners = scores[:n].argmax(1)
    admitted = scores[:n].max(1) >= threshold
    correct = int(np.sum(admitted & (winners == truth)))
    wrong = int(np.sum(admitted & (winners != truth)))
    nulls = int(np.sum(scores[n:].max(1) >= threshold))
    return dict(signal_trials=n, correct_accepted=correct, wrong_accepted=wrong,
                misses=n-correct-wrong, correct_interval95=interval(correct, n),
                null_trials=n, false_admissions=nulls,
                null_interval95=interval(nulls, n),
                raw_correct=int(np.sum(winners == truth)))


def phase_integral(r, energy):
    x = 2*np.abs(r)
    return np.log(i0e(x)) + x - energy


def analytic_scores(r, phase, energy, dt, tau):
    """Every non-oracle method is a normalized, fixed-prior Gaussian H0 LR."""
    blocks = len(r)
    values = {"global_unknown_phase": phase_integral(r.sum(0), energy*blocks)}
    # Independent uniform phase per segment is deliberately a mismatched
    # receiver prior when the true phase follows a continuous Brownian path.
    for ratio in (.25, 1., 4.):
        size = max(1, round(ratio*tau/dt))
        score = np.zeros(r.shape[1:], dtype=np.float64)
        for first in range(0, blocks, size):
            part = r[first:first+size]
            score += phase_integral(part.sum(0), len(part)*energy)
        values[f"segmented_{ratio:g}_tau"] = score
    # The genie reveals the exact teacher trajectory for both candidate
    # patterns. It also reveals a fresh independent trajectory in each null.
    # This is a known-waveform likelihood, not an operational receiver.
    values["oracle_phase"] = 2*np.real((r*np.exp(-1j*phase[..., None])).sum(0))-energy*blocks
    return values


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--trials', type=int, default=64)
    p.add_argument('--seed', type=int, default=603011)
    p.add_argument('--days', type=float, default=850.)
    p.add_argument('--cn0', type=float, nargs='+', default=[-60.])
    p.add_argument('--dt-over-tau', type=float, default=.025)
    p.add_argument('--phase-states', type=int, nargs='+', default=[32, 64])
    p.add_argument('--reblocks', type=int, nargs='+', default=[1])
    p.add_argument('--precision', choices=['fp32', 'fp64'], default='fp32')
    p.add_argument('--threads', type=int, default=1)
    a = p.parse_args(argv)
    if not 1 <= a.trials <= 1024 or not 0 < a.days <= 3400 or not .001 <= a.dt_over_tau <= .25:
        p.error('outside bounded diagnostic size')
    if any(x < 1 or x > 16 for x in a.reblocks) or not 1 <= a.threads <= 2:
        p.error('invalid reblock or CPU budget')
    torch.set_num_threads(a.threads)
    torch.set_num_interop_threads(1)
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.set_float32_matmul_precision('highest')
    sigma = math.hypot(.05, .05)*math.pi/180
    tau = 2/sigma**2
    total_time = a.days*86400
    count = math.ceil(total_time/(a.dt_over_tau*tau))
    dt = total_time/count
    dtype = torch.float32 if a.precision == 'fp32' else torch.float64
    ctype = torch.complex64 if a.precision == 'fp32' else torch.complex128
    rng = np.random.default_rng(a.seed)
    # Half signals, half independent H0 draws. Equal bit balance by design.
    truth = np.arange(a.trials) % 2
    phase = rng.uniform(-math.pi, math.pi, (1, 2*a.trials)) + np.concatenate(
        [np.zeros((1, 2*a.trials)), np.cumsum(rng.normal(0, sigma*math.sqrt(dt),
         (count-1, 2*a.trials)), axis=0)], axis=0)
    noise = (rng.normal(size=(count, 2*a.trials, 2)) +
             1j*rng.normal(size=(count, 2*a.trials, 2)))/math.sqrt(2)
    gates = {
        'strict': math.log(2*1440*8*16/(.5e-4)),
        'relaxed': math.log(2*720*8*16/(.5*.01)),
    }
    result = dict(schema='triad-common-phase-discriminator-v1', seed=a.seed,
        teacher='orthogonal candidate sufficient statistics; block-constant continuous-Brownian skeleton',
        sampled_waveform=False, sigma_rad_sqrt_second=sigma, tau_seconds=tau,
        days=a.days, blocks=count, dt_seconds=dt, dt_over_tau=dt/tau,
        independent_synthetic_phase_noise_paths=a.trials,
        null_paths=a.trials, thresholds=gates,
        calibration='Gaussian H0 exact-arithmetic normalized likelihood mixtures; union over 2 bits, 16 methods, 8 durations, 1440/day or 720/12h attempts; half budget to scores',
        source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        library_sha256=hashlib.sha256((ROOT/'tools/triad_phase_frequency.py').read_bytes()).hexdigest(),
        environment=dict(torch=torch.__version__, numpy=np.__version__, precision=a.precision, threads=a.threads),
        limitations=['No triad pulse shape, carrier overlap, delay, Doppler, selective modulation, timing/epoch/CFO search or unknown noise.',
          'Brownian phase is constant inside teacher blocks; convergence in this script does not validate continuous within-block waveform behavior.',
          'C/N0 sweeps share paired phase and indexed noise. Counts refer to synthetic independent paths, not physical propagation reliability.',
          'Known amplitude and orthogonal candidate patterns make this a favorable diagnostic. Oracle phase is unavailable to an ordinary receiver.',
          'Time scaling preserves c*tau, T/tau, dt/tau only for this stipulated sufficient-statistic teacher, not an accelerated real channel.'], rows=[])
    raw = {}
    for cn0 in a.cn0:
        c = 10**(cn0/10)
        energy = c*dt
        r = math.sqrt(energy)*noise.copy()
        r[:, np.arange(a.trials), truth] += energy*np.exp(1j*phase[:, :a.trials])
        started = time.perf_counter()
        scores = analytic_scores(r, phase, energy, dt, tau)
        analytic_wall = time.perf_counter()-started
        for states in a.phase_states:
            for reblock in a.reblocks:
                name = f'finite_phase_{states}_reblock_{reblock}'
                model = PhaseFrequencyModel(phase_states=states, frequency_grid_hz=(0.,),
                    frequency_model='static', nominal_dt_seconds=dt*reblock,
                    phase_diffusion_rad_per_sqrt_second=sigma,
                    amplitude_nodes=(1.,), amplitude_weights=(1.,),
                    relative_phase_states=1, ray_amplitudes=(1.,))
                state = PhaseFrequencyLikelihood(2*a.trials, 2, 1, model, dtype=dtype,
                    validate_inputs=False, memory_limit_bytes=512*1024**2)
                previous = None
                t0 = time.perf_counter()
                with torch.inference_mode():
                    for first in range(0, count, reblock):
                        last = min(first+reblock, count)
                        center = (first+last)*dt/2
                        obs = torch.as_tensor(r[first:last].sum(0)[..., None], dtype=ctype)
                        gram = torch.full((2, 1, 1), (last-first)*energy, dtype=ctype)
                        state.update(obs, gram, 0. if previous is None else center-previous)
                        previous = center
                scores[name] = state.scores().cpu().numpy()
                wall = time.perf_counter()-t0
                row = dict(cn0_db_hz=cn0, c_tau=c*tau, energy_snr=c*total_time,
                           method=name, wall_seconds=wall, diagnostics=state.diagnostics(),
                           outcomes={gate: classify(scores[name], truth, value) for gate,value in gates.items()})
                result['rows'].append(row)
                print(json.dumps({k:row[k] for k in ('cn0_db_hz','method','wall_seconds','outcomes')}), flush=True)
        for name, value in scores.items():
            raw[f'cn0_{cn0:g}_{name}'] = value
            if name.startswith('finite_phase_'):
                continue
            row = dict(cn0_db_hz=cn0, c_tau=c*tau, energy_snr=c*total_time,
                method=name, wall_seconds_joint_analytic=analytic_wall,
                outcomes={gate:classify(value, truth, threshold) for gate,threshold in gates.items()})
            if name == 'oracle_phase':
                row['known_waveform_single_candidate_Pd'] = {
                    gate:float(norm.sf((threshold-c*total_time)/math.sqrt(2*c*total_time)))
                    for gate,threshold in gates.items()}
            result['rows'].append(row)
        # A paired ranking/threshold check is more useful than only score error.
        reference = scores[f'finite_phase_{max(a.phase_states)}_reblock_{min(a.reblocks)}']
        for row in result['rows']:
            if row['cn0_db_hz'] == cn0 and row['method'].startswith('finite_phase_'):
                s = scores[row['method']]
                row['paired_vs_finest'] = dict(max_abs_log_lr_difference=float(np.max(np.abs(s-reference))),
                    winner_changes=int(np.sum(s.argmax(1) != reference.argmax(1))),
                    admission_changes={gate:int(np.sum((s.max(1)>=threshold) != (reference.max(1)>=threshold)))
                                       for gate,threshold in gates.items()})
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n')
    np.savez_compressed(a.output.with_suffix('.scores.npz'), **raw)


if __name__ == '__main__':
    main()
