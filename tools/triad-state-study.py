#!/usr/bin/env python3
"""Paired block-statistic receiver experiments; no ordinary modem integration.

Each input is an independently sampled C++ channel path. Noise replications
conditional on that path are reported separately from path diversity. Admission
uses proper likelihood mixtures or a fixed-rank Gamma null law, with a union
budget for every declared attempt, duration and receiver-selection alternative.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import platform
import time

import numpy as np
from scipy.stats import beta, gamma, norm
from scipy.special import gammaln
import torch

from triad_gaussian_state import GaussianPrior, GaussianStateFilter
from triad_state_workspace import BlockWorkspace


def interval(successes, total):
    """Two one-sided 95% Clopper-Pearson bounds, conditional on a fixed path."""
    return [0. if successes == 0 else float(beta.ppf(.05, successes, total-successes+1)),
            1. if successes == total else float(beta.ppf(.95, successes+1, total-successes))]


def logmean(x, axis):
    return torch.logsumexp(x, dim=axis)-math.log(x.shape[axis])


def bit_mixture(scores, labels):
    return torch.stack([logmean(scores[:, labels == bit], 1) for bit in (0, 1)], dim=1)


def gamma_log_survival(energy, ranks):
    """Integer-shape Gamma log-tail, retaining ordering after SF underflow."""
    ranks = np.broadcast_to(ranks, energy.shape)
    result = gamma.logsf(energy, np.maximum(ranks, 1))
    for trial, candidate in np.argwhere(~np.isfinite(result)):
        x, k = float(energy[trial, candidate]), int(ranks[trial, candidate])
        if not math.isfinite(x) or x <= k or k <= 0:
            raise ValueError('invalid energy or unsupported Gamma tail failure')
        # Q(k,x)=exp(-x)*sum_{j=0}^{k-1} x^j/j!; scale at its largest
        # term. All remaining terms decrease, with a geometric tail bound.
        term, total = 1., 1.
        for j in range(k-1, 0, -1):
            term *= j/x
            total += term
            ratio = (j-1)/x
            if term*ratio/(1-ratio) <= 2e-15*total:
                break
        result[trial, candidate] = -x+(k-1)*math.log(x)-float(gammaln(k))+math.log(total)
    result[ranks == 0] = 0
    return result


def describe(scores, truth, threshold):
    winner = scores.argmax(axis=1)
    accepted = scores.max(axis=1) >= threshold
    correct = int(np.sum(accepted & (winner == truth)))
    wrong = int(np.sum(accepted & (winner != truth)))
    total = len(scores)
    # Gamma survival probabilities can underflow at decisive high energies.
    # Preserve infinite scores in NPZ and admission, but keep JSON valid.
    quantiles = np.quantile(scores.max(axis=1), [.05, .5, .95], method='inverted_cdf')
    return dict(trials=total, correct_accepted=correct, wrong_accepted=wrong,
                missed=total-correct-wrong, raw_correct=int(np.sum(winner == truth)),
                correct_one_sided_95=interval(correct, total),
                score_quantiles=[float(x) if math.isfinite(x) else None for x in quantiles],
                positive_infinite_scores=int(np.isposinf(scores).sum()))


def parse():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('inputs', type=Path, nargs='+')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cn0', type=float, nargs='+', default=[-24.])
    p.add_argument('--trials', type=int, default=32)
    p.add_argument('--seed', type=int, default=31001)
    p.add_argument('--split', choices=['development', 'calibration', 'holdout'], default='development')
    p.add_argument('--device', default='cpu')
    p.add_argument('--precision', choices=['fp32', 'fp64'], default='fp64')
    p.add_argument('--threads', type=int, default=2)
    p.add_argument('--memory-mib', type=int, default=4096)
    p.add_argument('--methods', nargs='+', choices=['gaussian', 'phase-frequency', 'block-energy'],
                   default=['gaussian', 'block-energy'])
    p.add_argument('--coherences', type=float, nargs='+', default=[16., 64., 256., 1024.])
    p.add_argument('--taps', type=int, choices=[1, 2], default=2)
    p.add_argument('--phase-states', type=int, default=128)
    p.add_argument('--phase-implementation', choices=['reference', 'optimized', 'graph'], default='reference',
                   help='Explicit numerical implementation; finite prior and search bank are unchanged.')
    p.add_argument('--relative-phase-states', type=int, default=8)
    p.add_argument('--frequency-bound', type=float, default=.02)
    p.add_argument('--frequency-step', type=float, default=None,
                   help='Explicit fixed frequency spacing for separate phase/frequency convergence checks.')
    p.add_argument('--frequency-std', type=float, default=.003)
    p.add_argument('--frequency-coherence', type=float, default=1000.)
    p.add_argument('--phase-diffusion', type=float, default=.00872665)
    p.add_argument('--false-probability', type=float, default=1e-4)
    p.add_argument('--horizon-seconds', type=float, default=86400.)
    p.add_argument('--attempt-spacing', type=float, default=60.)
    p.add_argument('--duration-alternatives', type=int, default=1)
    p.add_argument('--selection-methods', type=int, default=None,
                   help='Total deployed alternatives; default number of requested methods.')
    p.add_argument('--actual-observation', action='store_true',
                   help='Also score the exported independently sampled raw noisy projection.')
    a = p.parse_args()
    if a.trials < 1 or a.trials > 100000 or a.threads < 1 or a.duration_alternatives < 1:
        p.error('invalid trial, thread or duration count')
    if not 0 < a.false_probability < 1 or not math.isfinite(a.horizon_seconds) or a.horizon_seconds <= 0 or not math.isfinite(a.attempt_spacing) or a.attempt_spacing <= 0:
        p.error('invalid operational admission budget')
    if any(not math.isfinite(c) for c in a.cn0) or any(c <= 0 or math.isnan(c) for c in a.coherences):
        p.error('invalid C/N0 or coherence')
    if len(set(a.methods)) != len(a.methods) or not a.methods:
        p.error('methods must be distinct')
    a.selection_methods = a.selection_methods or len(a.methods)
    a.memory_limit_bytes = a.memory_mib*1024*1024
    if a.memory_mib < 1:
        p.error('memory-mib must be positive')
    if a.selection_methods < len(a.methods):
        p.error('selection-methods cannot omit exercised receiver alternatives')
    return a


def evaluate(workspace, blocks, args, selected, labels, batch):
    """Only this function calls detectors. It receives no simulated H1 means."""
    device = torch.device(args.device)
    dtype = torch.complex128 if args.precision == 'fp64' else torch.complex64
    mapping = torch.tensor([workspace.column_mapping[c] for c in selected], device=device)
    gaussian = [GaussianStateFilter(GaussianPrior(tuple([1/args.taps]*args.taps), c),
                                    dtype=dtype, check_inputs=False) for c in args.coherences]
    phase = None
    if 'phase-frequency' in args.methods:
        from triad_phase_frequency import PhaseFrequencyLikelihood, PhaseFrequencyModel
        frequency_grid = None
        if getattr(args, 'frequency_step', None) is not None:
            step = args.frequency_step
            if not math.isfinite(step) or step <= 0 or not math.isfinite(args.frequency_bound) or args.frequency_bound < 0:
                raise ValueError('invalid explicit frequency grid')
            half = math.ceil(args.frequency_bound/step)
            if half > 128:
                raise ValueError('explicit frequency grid exceeds 257 states')
            frequency_grid = tuple(i*step for i in range(-half,half+1))
        model = PhaseFrequencyModel(phase_states=args.phase_states,
                                    relative_phase_states=args.relative_phase_states,
                                    frequency_bound_hz=args.frequency_bound,
                                    frequency_grid_hz=frequency_grid,
                                    nominal_dt_seconds=workspace.block_samples/workspace.sample_rate,
                                    frequency_std_hz=args.frequency_std,
                                    frequency_correlation_seconds=args.frequency_coherence,
                                    phase_diffusion_rad_per_sqrt_second=args.phase_diffusion)
        phase_type = PhaseFrequencyLikelihood
        implementation = getattr(args, 'phase_implementation', 'reference')
        if implementation != 'reference':
            from triad_phase_optimized import OptimizedPhaseFrequencyLikelihood, GraphPhaseFrequencyLikelihood
            phase_type = GraphPhaseFrequencyLikelihood if implementation == 'graph' else OptimizedPhaseFrequencyLikelihood
        phase = phase_type(batch, len(selected), args.taps, model,
                                         device=device, dtype=torch.float64 if args.precision == 'fp64' else torch.float32,
                                         memory_limit_bytes=getattr(args, 'memory_limit_bytes', 4*1024**3),
                                         validate_inputs=False)
    energies = torch.zeros((batch, len(selected)), dtype=torch.float64, device=device)
    ranks = np.zeros((batch, len(selected)), dtype=np.int64)
    started = time.perf_counter()
    with torch.inference_mode():
        for block in blocks:
            full_g = torch.as_tensor(block.gram.copy(), dtype=dtype, device=device)
            full_r = torch.as_tensor(block.r, dtype=dtype, device=device)
            g = (full_g[mapping[:, :, None], mapping[:, None, :]] if full_g.ndim == 2 else
                 full_g[:, mapping[:, :, None], mapping[:, None, :]])
            r = full_r[:, mapping]
            if 'gaussian' in args.methods:
                for state in gaussian:
                    state.update(r, g, block.dt_seconds)
            if phase is not None:
                phase.update(r, g, block.dt_seconds)
            if 'block-energy' in args.methods:
                # Calibration of nearly dependent columns cannot use rounded
                # FP32 eigenvalues. Keep this sensitive whitening in FP64.
                reduction_g = torch.as_tensor(block.gram.copy(), dtype=torch.complex128, device=device)
                reduction_r = torch.as_tensor(block.r, dtype=torch.complex128, device=device)
                reduction_g = (reduction_g[mapping[:, :, None], mapping[:, None, :]] if reduction_g.ndim == 2 else
                               reduction_g[:, mapping[:, :, None], mapping[:, None, :]])
                reduction_r = reduction_r[:, mapping]
                values, vectors = torch.linalg.eigh(reduction_g)
                # Fixed public rank; no observed y enters rank selection.
                keep = values > values.amax(-1, keepdim=True).clamp_min(1e-300)*1e-10
                inv = torch.where(keep, 1/values.clamp_min(torch.finfo(values.dtype).tiny), 0)
                transformed = (vectors.mH @ reduction_r[..., None]).squeeze(-1)
                energies += (transformed.abs().square()*inv).sum(-1).to(torch.float64)
                ranks += keep.sum(-1).cpu().numpy()
    if device.type == 'cuda':
        torch.cuda.synchronize(device)
    elapsed = time.perf_counter()-started
    result, diagnostic = {}, {}
    bitlabels = torch.tensor(labels, device=device)
    if 'gaussian' in args.methods:
        all_scores = torch.stack([s.finish() for s in gaussian])
        result['gaussian'] = bit_mixture(logmean(all_scores, 0), bitlabels).cpu().numpy()
        diagnostic['gaussian'] = [s.diagnostics() for s in gaussian]
    if phase is not None:
        result['phase-frequency'] = bit_mixture(phase.scores(), bitlabels).cpu().numpy()
        diagnostic['phase-frequency'] = phase.diagnostics()
    if 'block-energy' in args.methods:
        # Convert each Gamma tail to -log(p), then maximize within each bit.
        energy = energies.cpu().numpy()
        log_tail = gamma_log_survival(energy, ranks)
        result['block-energy'] = np.stack([-log_tail[:, labels == b].min(axis=1) for b in (0, 1)], axis=1)
        diagnostic['block-energy'] = {'ranks': ranks.tolist(), 'candidates': len(selected),
                                      'whitening_and_reduction_dtype': 'torch.complex128'}
    return result, diagnostic, elapsed


def main():
    a = parse()
    if a.output.exists():
        raise ValueError('output must be fresh')
    torch.set_num_threads(a.threads)
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    rows, saved = [], {}
    attempts = math.ceil(a.horizon_seconds/a.attempt_spacing)
    family = attempts*a.duration_alternatives*a.selection_methods
    salts = dict(development=0x444556, calibration=0x43414c, holdout=0x484f4c44)
    for path_index, directory in enumerate(a.inputs):
        w = BlockWorkspace(directory)
        selected = [i for i, mapping in enumerate(w.column_mapping) if len(mapping) == a.taps]
        labels = np.array([w.candidates[i]['bit'] for i in selected])
        if not selected or set(labels) != {0, 1}:
            raise ValueError('input needs both bits with the selected public tap dimension')
        manifest_hash = hashlib.sha256(w.manifest_path.read_bytes()).hexdigest()
        signal_energy = float(np.memmap(directory/'signal_energy.f64', dtype='<f8', mode='r').sum())
        for ci, cn0 in enumerate(a.cn0):
            seed = int(np.random.SeedSequence([a.seed, salts[a.split], path_index]).generate_state(1, dtype=np.uint64)[0])
            results, diagnostics, seconds = {}, {}, {}
            for signal in (False, True):
                name = 'signal' if signal else 'null'
                blocks = w.iter_simulator(seed=seed, batch=a.trials, cn0_db_hz=cn0, signal=signal)
                results[name], diagnostics[name], seconds[name] = evaluate(w, blocks, a, selected, labels, a.trials)
            actual = None
            if a.actual_observation:
                actual_cn0 = 10*math.log10(w.sample_rate/w.noise_variance)
                if not math.isclose(cn0, actual_cn0, abs_tol=1e-9):
                    raise ValueError('actual-observation C/N0 must match the physical export; raw noisy data cannot be rescaled as a noiseless mean')
                actual, actual_diagnostic, actual_seconds = evaluate(w, w.iter_observation(), a, selected, labels, 1)
            for method in a.methods:
                threshold = math.log(family*(len(selected) if method == 'block-energy' else 2)/a.false_probability)
                truth = int(w.manifest['signal_bit'])
                row = dict(schema='triad-state-study-v1', path=str(directory), manifest_sha256=manifest_hash,
                           cn0_db_hz=cn0, split=a.split, seed=seed, method=method,
                           elapsed_seconds=w.sample_count/w.sample_rate,
                           independent_physical_paths=1, conditional_noise_trials=a.trials,
                           admission_threshold=threshold, attempts_per_horizon=attempts,
                           horizon_seconds=a.horizon_seconds, false_probability=a.false_probability,
                           duration_alternatives=a.duration_alternatives, selection_methods=a.selection_methods,
                           signal=describe(results['signal'][method], truth, threshold),
                           diagnostics=diagnostics['signal'][method], wall_seconds=seconds)
                row['wall_seconds_scope'] = 'joint time for all requested methods, including input iteration; run a single method for individual latency'
                admitted = int(np.sum(results['null'][method].max(axis=1) >= threshold))
                row['null'] = dict(admitted=admitted, trials=a.trials, one_sided_95=interval(admitted,a.trials),
                                   scope='one capture; analytic family union bound covers declared repeated attempts, not empirically simulated days')
                if actual is not None:
                    row['actual_sampled_noise'] = describe(actual[method], truth, threshold)
                    row['actual_sampled_noise_wall_seconds'] = actual_seconds
                rho = signal_energy/w.sample_rate*10**(cn0/10)
                row['known_waveform_genie'] = dict(energy_snr=rho,
                    detection_probability_single_test=float(norm.sf(norm.isf(a.false_probability/family)-math.sqrt(2*rho))),
                    scope='Gaussian known entire received waveform with its actual gain and trajectory; detection bound, not bit/acquisition guarantee')
                rows.append(row)
                saved[f'p{path_index}_c{ci}_{method}_signal'] = results['signal'][method]
                saved[f'p{path_index}_c{ci}_{method}_null'] = results['null'][method]
                print(json.dumps({k: row[k] for k in ['path','cn0_db_hz','method','signal','null','wall_seconds']}), flush=True)
    report = dict(schema='triad-state-study-report-v1', configuration={k: str(v) if isinstance(v,Path) else [str(x) for x in v] if k=='inputs' else v for k,v in vars(a).items()},
                  environment=dict(python=platform.python_version(), torch=torch.__version__, numpy=np.__version__, device=a.device), rows=rows,
                  limitations=['Each noise batch is conditional on one physical channel path; do not pool its confidence interval as independent path qualification.',
                               'Projection noise is exactly coupled Gaussian conditional on exported deterministic bases; physical waveform mismatch is retained in means.',
                               'Likelihood bounds require known white Gaussian noise variance; no claim for unmodeled interference or noise uncertainty.',
                               'Only listed durations and attempts are covered. Repeated research parameter tuning is not an operational receiver.'])
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report, indent=2, allow_nan=False)+'\n')
    np.savez_compressed(a.output.with_suffix('.scores.npz'), **saved)


if __name__ == '__main__':
    main()
