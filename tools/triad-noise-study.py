#!/usr/bin/env python3
"""Paired noise-scale likelihood experiments on triad block workspaces.

Projected mode samples projected correlations and received energy jointly,
including independent orthogonal-complement energy. Hidden simulator means and
noise variance are used only in that sampler and in validation diagnostics.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import platform
import time
from pathlib import Path

import numpy as np
import torch

from triad_gaussian_state import GaussianPrior, GaussianStateFilter
from triad_noise_likelihood import (AngularGaussianLikelihood, FalseAdmissionBudget,
                                    normalize_by_variance_upper, variance_upper_confidence,
                                    whole_bank_residual)
from triad_phase_frequency import PhaseFrequencyLikelihood, PhaseFrequencyModel
from triad_state_workspace import BlockWorkspace, WorkspaceError


METHODS = ("gaussian-ou", "angular-gaussian", "residual-ucb",
           "phase-frequency", "phase-frequency-ucb")
SPLIT_SALT = {"development": 0x444556, "calibration": 0x43414C, "holdout": 0x484F4C44}


def _json_model_parameters(value):
    """Encode positive infinite model durations as the JSON string 'infinity'.

    Apply only to declared configuration, leaving numeric DSP state untouched.
    NaN/negative infinity and invalid score values still fail strict JSON output.
    """
    if isinstance(value, dict):
        return {key: _json_model_parameters(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [_json_model_parameters(item) for item in value]
    if isinstance(value, float) and value == math.inf:
        return "infinity"
    return value


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def _close_workspace_maps(workspace: BlockWorkspace) -> None:
    """Release NPZ-like mapped files as soon as one path is finished.

    BlockWorkspace intentionally caches mappings while it is in active use.
    The campaign handles many independent workspaces serially, so it must
    explicitly drop that cache after copying any needed public calendar data.
    """
    maps = getattr(workspace, "_maps", None)
    if not maps:
        return
    workspace._maps = {}
    for array in maps.values():
        mapped_file = getattr(array, "_mmap", None)
        if mapped_file is not None and not mapped_file.closed:
            mapped_file.close()


def _cn(rng: np.random.Generator, shape: tuple[int, ...]) -> np.ndarray:
    return (rng.standard_normal(shape) + 1j * rng.standard_normal(shape)) / math.sqrt(2.)


def project_joint_statistics(gram: np.ndarray, mean: np.ndarray, signal_energy: float,
                             valid_rows: int, noise_variance: float,
                             rng: np.random.Generator, batch: int,
                             include_signal: bool) -> tuple[np.ndarray, np.ndarray]:
    """Draw y's bank projection and total energy from full-bank statistics.

    The signal mean/energy arguments are simulator-only. The output consists
    only of receiver-facing A.H@y and ||y||^2 in physical units. Positive Gram
    modes draw coupled projected noise. The orthogonal complement draws an
    independent proper-complex noncentral norm with residual signal energy.
    """
    G = np.asarray(gram, dtype=np.complex128)
    mu = np.asarray(mean, dtype=np.complex128)
    if G.ndim != 2 or G.shape[0] != G.shape[1] or mu.shape != (G.shape[0],):
        raise ValueError("projected Gram/mean shapes are inconsistent")
    if valid_rows < 1 or batch < 1 or not math.isfinite(noise_variance) or noise_variance <= 0:
        raise ValueError("projected simulation needs positive rows, batch and variance")
    if not math.isfinite(signal_energy) or signal_energy < 0:
        raise ValueError("projected simulation signal energy is invalid")
    G = (G + G.conj().T) * .5
    values, vectors = np.linalg.eigh(G)
    scale = max(float(values[-1]), np.finfo(np.float64).tiny)
    if values[0] < -1e-9 * scale:
        raise ValueError("projected Gram is not positive semidefinite")
    values = np.maximum(values, 0.)
    keep = values > scale * 1e-10
    rank = int(keep.sum())
    if rank > valid_rows:
        raise ValueError("projected Gram rank exceeds valid raw rows")
    U, lam = vectors[:, keep], values[keep]
    if rank:
        mu_eigen = U.conj().T @ mu
        projected_mean_energy = float(np.sum(np.abs(mu_eigen)**2 / lam))
        mu = U @ mu_eigen
        factor = U * np.sqrt(lam)[None, :]
        noise_r = math.sqrt(noise_variance) * (_cn(rng, (batch, rank)) @ factor.T)
    else:
        projected_mean_energy = 0.
        noise_r = np.zeros((batch, G.shape[0]), dtype=np.complex128)
    residual_signal_energy = signal_energy - projected_mean_energy
    tolerance = 2e-9 * max(signal_energy, projected_mean_energy, 1.)
    if residual_signal_energy < -tolerance:
        raise ValueError("projected signal energy exceeds the simulator's total energy")
    residual_signal_energy = max(0., residual_signal_energy)
    dof = valid_rows - rank
    if dof == 0 and residual_signal_energy > tolerance:
        raise ValueError("full-rank Gram leaves nonzero orthogonal signal energy")
    r = noise_r + mu[None, :] if include_signal else noise_r
    if dof:
        # H0 and H1 consume the same random orthogonal noise coordinates;
        # H1 adds only the deterministic simulator-only residual mean.
        z0 = _cn(rng, (batch,))
        if include_signal and residual_signal_energy > 0.:
            z0 += math.sqrt(residual_signal_energy / noise_variance)
        orthogonal_norm = np.abs(z0)**2
        if dof > 1:
            orthogonal_norm += rng.gamma(shape=dof-1, scale=1., size=batch)
    else:
        orthogonal_norm = np.zeros(batch, dtype=np.float64)
    if rank:
        pinv = (U / lam[None, :]) @ U.conj().T
        projected_energy = np.einsum("bi,ij,bj->b", r.conj(), pinv, r).real
    else:
        projected_energy = np.zeros(batch, dtype=np.float64)
    total_energy = np.maximum(projected_energy, 0.) + noise_variance * orthogonal_norm
    if not np.isfinite(r).all() or not np.isfinite(total_energy).all():
        raise ValueError("projected simulator produced nonfinite statistics")
    return r, total_energy


def _rank_stable_gram(gram: np.ndarray, rank_rtol: float = 1e-10) -> np.ndarray:
    """Apply the same public relative eigenvalue cutoff as the residual UCB."""
    G = np.asarray(gram, dtype=np.complex128)
    values, vectors = np.linalg.eigh((G + G.conj().T) * .5)
    scale = max(float(values[-1]), np.finfo(np.float64).tiny)
    if values[0] < -1e-9 * scale:
        raise ValueError("projected Gram is not positive semidefinite")
    keep = values > scale * rank_rtol
    return (vectors[:, keep] * values[keep][None, :]) @ vectors[:, keep].conj().T


def _projection_self_test(seed: int = 51543, trials: int = 6000) -> dict:
    """Dense raw-waveform validation of joint r/energy/residual sampling."""
    from triad_noise_likelihood import angular_log_likelihood
    rng = np.random.default_rng(seed)
    n, d = 13, 4
    A = _cn(rng, (n, d))
    s = _cn(rng, (n,))
    G, mu, Es = A.conj().T @ A, A.conj().T @ s, float(np.vdot(s, s).real)
    variance = 1.7
    raw_y = math.sqrt(variance) * _cn(rng, (trials, n)) + s[None, :]
    raw_r, raw_E = raw_y @ A.conj(), np.sum(np.abs(raw_y)**2, axis=1)
    stats_r, stats_E = project_joint_statistics(G, mu, Es, n, variance,
                                                np.random.default_rng(seed + 1), trials, True)
    empirical_cov = (stats_r - mu).T @ (stats_r - mu).conj() / trials
    covariance_error = float(np.linalg.norm(empirical_cov - variance * G) / np.linalg.norm(variance * G))
    expected_E_mean = Es + n * variance
    expected_E_var = n * variance**2 + 2 * variance * Es
    energy_z = abs(float(stats_E.mean()) - expected_E_mean) / math.sqrt(expected_E_var / trials)
    energy_var_rel = abs(float(stats_E.var(ddof=1)) - expected_E_var) / expected_E_var
    pinv = np.linalg.pinv(G)
    raw_projected = np.real(np.einsum("bi,ij,bj->b", raw_r.conj(), pinv, raw_r))
    stats_projected = np.real(np.einsum("bi,ij,bj->b", stats_r.conj(), pinv, stats_r))
    q, _ = np.linalg.qr(A, mode="complete")
    raw_orth = np.sum(np.abs(raw_y @ q[:, d:].conj())**2, axis=1)
    raw_resid = raw_E - raw_projected
    stats_resid = stats_E - stats_projected
    qr_error = float(np.max(np.abs(raw_resid - raw_orth)))
    f, qstat, energy = (torch.tensor([.17, .4], dtype=torch.float64),
                        torch.tensor([.21, .39], dtype=torch.float64),
                        torch.tensor([1.2, 1.9], dtype=torch.float64))
    angular_scale_error = float(torch.max(torch.abs(
        angular_log_likelihood(f, qstat, energy, n) -
        angular_log_likelihood(f, qstat * 3.25, energy * 3.25, n))))
    checks = {"covariance_relative_error": covariance_error,
              "received_energy_mean_z": energy_z,
              "received_energy_variance_relative_error": energy_var_rel,
              "dense_raw_qr_residual_max_error": qr_error,
              "angular_common_unit_scale_error": angular_scale_error,
              "raw_energy_mean": float(raw_E.mean()),
              "simulated_energy_mean": float(stats_E.mean()),
              "raw_residual_energy_mean": float(raw_resid.mean()),
              "simulated_residual_energy_mean": float(stats_resid.mean())}
    limits = {"covariance_relative_error": max(.06, 3.2 / math.sqrt(trials)), "received_energy_mean_z": 4.5,
              "received_energy_variance_relative_error": .09,
              "dense_raw_qr_residual_max_error": 1e-9,
              "angular_common_unit_scale_error": 1e-12}
    if (covariance_error > limits["covariance_relative_error"]
            or energy_z > limits["received_energy_mean_z"]
            or energy_var_rel > limits["received_energy_variance_relative_error"]
            or qr_error > limits["dense_raw_qr_residual_max_error"]
            or angular_scale_error > limits["angular_common_unit_scale_error"]):
        raise RuntimeError(f"joint projected-sampler dense validation failed: {checks}")
    return {"passed": True, "seed": seed, "trials": trials,
            "dimensions": {"complex_samples": n, "full_bank_columns": d, "orthogonal_complex_dof": n-d},
            "metrics": checks, "limits": limits,
            "scope": "independent dense raw samples vs full-bank projection and orthogonal-energy sampler"}


def _parse_args(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("inputs", type=Path, nargs="+", help="independent triad-block-stats-v1 workspaces")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--mode", choices=("actual", "projected"), default="projected")
    p.add_argument("--cn0", type=float, required=True, help="declared C/N0 used to form the covariance-shape reference")
    p.add_argument("--reference-variance", type=float, default=None,
                   help="explicit positive reference power; default Fs/10^(C/N0/10); never read from the manifest")
    p.add_argument("--noise-scale", type=float, default=1.,
                   help="projected-mode actual noise POWER/variance divided by reference variance (not amplitude)")
    p.add_argument("--noise-step-factor", type=float, default=None,
                   help="second piecewise-constant block-segment power multiplier")
    p.add_argument("--noise-step-fraction", type=float, default=None,
                   help="fraction of initial blocks at --noise-scale; remaining blocks use factor times this")
    p.add_argument("--controls", choices=("signal", "null"), nargs="+", default=None,
                   help="projected defaults to paired signal+null; actual infers each workspace's declared control")
    p.add_argument("--trials", type=int, default=1, help="conditional noise captures per physical path in projected mode")
    p.add_argument("--seed", type=int, default=73001)
    p.add_argument("--split", choices=tuple(SPLIT_SALT), default="development")
    p.add_argument("--device", default="cpu")
    p.add_argument("--precision", choices=("fp32", "fp64"), default="fp64")
    p.add_argument("--threads", type=int, default=2)
    p.add_argument("--memory-gib", type=float, default=4.)
    p.add_argument("--tile-blocks", type=int, default=128)
    p.add_argument("--methods", nargs="+", choices=METHODS, default=["gaussian-ou", "angular-gaussian", "residual-ucb", "phase-frequency", "phase-frequency-ucb"])
    p.add_argument("--coherences", type=float, nargs="+", default=[16., 64., 256., 1024.])
    p.add_argument("--taps", type=int, choices=(1, 2), default=2)
    p.add_argument("--phase-states", type=int, default=128)
    p.add_argument("--relative-phase-states", type=int, default=8)
    p.add_argument("--frequency-bound", type=float, default=.02)
    p.add_argument("--frequency-step", type=float, default=None)
    p.add_argument("--frequency-model", choices=("ou", "static", "random_walk"), default="ou")
    p.add_argument("--frequency-std", type=float, default=.003)
    p.add_argument("--frequency-coherence", type=float, default=1000.)
    p.add_argument("--frequency-random-walk", type=float, default=1e-6)
    p.add_argument("--phase-diffusion", type=float, default=.00872665)
    p.add_argument("--phase-implementation", choices=("reference", "optimized", "graph", "graph-fp64"), default="reference")
    p.add_argument("--false-probability", type=float, default=1e-4)
    p.add_argument("--horizon-seconds", type=float, default=86400.)
    p.add_argument("--attempt-spacing", type=float, default=60.)
    p.add_argument("--duration-alternatives", type=int, default=1)
    p.add_argument("--selection-methods", type=int, default=None)
    p.add_argument("--calibration-scope", choices=("per-block", "stationary"), default="per-block",
                   help="UCB pools: independent block powers, or one constant unknown power over the completed window")
    p.add_argument("--calibration-alternatives", type=int, default=1,
                   help="predeclared selectable calibration policies sharing the total delta budget")
    p.add_argument("--projection-validation-trials", type=int, default=6000)
    a = p.parse_args(argv)
    if a.output.exists() or a.output.with_suffix(".scores.npz").exists():
        p.error("output JSON and score NPZ paths must both be fresh")
    if not a.inputs or not a.methods or len(set(a.methods)) != len(a.methods):
        p.error("inputs and distinct methods are required")
    if a.selection_methods is None:
        a.selection_methods = len(a.methods)
    if a.selection_methods < len(a.methods):
        p.error("selection-methods cannot omit exercised receiver alternatives")
    if a.calibration_alternatives < 1:
        p.error("calibration-alternatives must be positive")
    if (a.calibration_scope == "stationary" and
            any(m in ("residual-ucb", "phase-frequency-ucb") for m in a.methods) and
            a.noise_step_factor is not None and a.noise_step_factor != 1. and
            a.noise_step_fraction is not None and 0 < a.noise_step_fraction < 1):
        p.error("stationary UCB calibration cannot pool a nonconstant projected noise-step schedule")
    if a.mode == "actual" and a.noise_scale != 1.:
        p.error("noise-scale changes generated noise only; actual mode uses the unscaled capture and explicit reference variance")
    if a.mode == "actual" and (a.noise_step_factor is not None or a.noise_step_fraction is not None):
        p.error("piecewise noise controls apply only to projected mode")
    if a.controls is None:
        a.controls = ["signal", "null"] if a.mode == "projected" else None
    if a.mode == "actual" and a.controls is not None and len(set(a.controls)) != 1:
        p.error("actual mode has one observed control per input; it cannot synthesize paired null/signal captures")
    for name in ("noise_scale", "cn0", "horizon_seconds", "attempt_spacing", "memory_gib"):
        value = getattr(a, name)
        if not math.isfinite(value) or (name != "cn0" and value <= 0):
            p.error(f"{name} must be finite" + (" and positive" if name != "cn0" else ""))
    if a.reference_variance is not None and (not math.isfinite(a.reference_variance) or a.reference_variance <= 0):
        p.error("reference-variance must be positive and finite")
    if (isinstance(a.trials, bool) or a.trials < 1 or a.trials > 100000
            or a.threads < 1 or a.tile_blocks < 1 or a.memory_gib <= 0):
        p.error("invalid trial, threads, tile-blocks or memory limit")
    if a.mode == "actual" and a.trials != 1:
        p.error("actual mode contains one observed capture per physical path; set --trials 1")
    if (not 0 < a.false_probability < 1 or a.duration_alternatives < 1
            or not math.isfinite(a.attempt_spacing) or a.attempt_spacing <= 0):
        p.error("invalid family probability, duration or attempt spacing")
    if any(math.isnan(c) or c <= 0 for c in a.coherences):
        p.error("coherences must be positive finite values or infinity")
    if any(not math.isfinite(x) for x in (a.frequency_bound, a.frequency_std,
                                          a.frequency_coherence, a.frequency_random_walk,
                                          a.phase_diffusion)):
        p.error("phase/frequency priors must be finite")
    if (a.frequency_bound < 0 or a.frequency_std < 0 or a.frequency_coherence <= 0 or
            a.frequency_random_walk < 0 or a.phase_diffusion < 0 or
            (a.frequency_step is not None and
             (not math.isfinite(a.frequency_step) or a.frequency_step <= 0))):
        p.error("frequency bound must be nonnegative and explicit frequency step positive")
    if a.projection_validation_trials < 500:
        p.error("projection validation requires at least 500 independent dense draws")
    a.memory_limit_bytes = int(a.memory_gib * 1024**3)
    return a


def _public_blocks(workspace: BlockWorkspace):
    previous = None
    index = 0
    for tile in workspace.iter_public(tile_blocks=workspace.max_tile_blocks):
        for G, interval, valid in zip(tile.gram, tile.intervals, tile.valid_rows):
            center = (int(interval[0]) + int(interval[1])) / (2. * workspace.sample_rate)
            dt = 0. if previous is None else center - previous
            if previous is not None and dt <= 0:
                raise WorkspaceError("block centers are not strictly increasing")
            yield index, G, interval, int(valid), center, dt
            previous = center
            index += 1
    if index != workspace.block_count:
        raise WorkspaceError(f"public block stream ended at {index}/{workspace.block_count}")


def _candidate_views(r_full: torch.Tensor, G_full: torch.Tensor,
                     mapping: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
    r = r_full[:, mapping]
    if G_full.ndim == 2:
        G = G_full[mapping[:, :, None], mapping[:, None, :]]
    else:
        G = G_full[:, mapping[:, :, None], mapping[:, None, :]]
    return r, G


def _logmeanexp(values: torch.Tensor, axis: int) -> torch.Tensor:
    return torch.logsumexp(values, dim=axis) - math.log(values.shape[axis])


def _bit_mixture(candidate_scores: torch.Tensor, labels: torch.Tensor) -> torch.Tensor:
    return torch.stack([_logmeanexp(candidate_scores[:, labels == b], 1) for b in (0, 1)], dim=1)


def _selected_candidates(workspace: BlockWorkspace, taps: int):
    """Choose a homogeneous scored model without narrowing calibration's bank."""
    indices = [index for index, row in enumerate(workspace.column_mapping)
               if len(row) == taps]
    if not indices:
        raise WorkspaceError("no public candidates have the requested tap dimension")
    mapping = np.asarray([workspace.column_mapping[index] for index in indices], dtype=np.int64)
    labels = np.asarray([workspace.candidates[index]["bit"] for index in indices], dtype=np.int64)
    if set(labels.tolist()) != {0, 1}:
        raise WorkspaceError("selected public candidate bank must include both bit hypotheses")
    return indices, mapping, labels


def _gates(args, block_count: int) -> tuple[int, dict[str, dict]]:
    attempts = math.ceil(args.horizon_seconds / args.attempt_spacing)
    result = {}
    any_ucb = any(method in ("residual-ucb", "phase-frequency-ucb") for method in args.methods)
    # Keep subruns combinable within one declared receiver family: known-only
    # rows must not silently consume the calibration half reserved for UCB rows.
    score_alpha = args.false_probability * .5
    for method in args.methods:
        ucb = method in ("residual-ucb", "phase-frequency-ucb")
        threshold = math.nextafter(math.log(attempts) + math.log(args.duration_alternatives) +
                                   math.log(args.selection_methods) + math.log(2) -
                                   math.log(score_alpha), math.inf)
        entry = {"score_probability": score_alpha,
                 "calibration_failure_probability": args.false_probability - score_alpha if any_ucb else 0.,
                 "reserved_calibration_probability": args.false_probability-score_alpha,
                 "threshold_log_likelihood_ratio": threshold,
                 "family_terms": {"scheduled_attempts": attempts,
                                  "duration_alternatives": args.duration_alternatives,
                                  "method_alternatives": args.selection_methods,
                                  "bit_hypotheses": 2}}
        if ucb:
            actual_segments = block_count if args.calibration_scope == "per-block" else 1
            budget = FalseAdmissionBudget(false_probability=args.false_probability,
                                          calibration_fraction=.5, attempts=attempts,
                                          duration_alternatives=args.duration_alternatives,
                                          model_alternatives=args.selection_methods,
                                          candidates=2,
                                          calibration_segments=actual_segments*args.calibration_alternatives)
            if abs(budget.log_threshold - threshold) > 2e-12:
                raise AssertionError("UCB gate differs from the public budget helper")
            entry["ucb_budget"] = budget.diagnostics()
            entry["ucb_budget"].update({"calibration_scope": args.calibration_scope,
                "actual_segments_per_capture": actual_segments,
                "calibration_alternatives": args.calibration_alternatives,
                "allocated_segments_per_capture": actual_segments*args.calibration_alternatives})
        result[method] = entry
    return attempts, result


def _new_states(args, workspace, mapping_np: np.ndarray, trials: int, is_step: bool):
    device = torch.device(args.device)
    complex_dtype = torch.complex128 if args.precision == "fp64" else torch.complex64
    real_dtype = torch.float64 if args.precision == "fp64" else torch.float32
    taps = mapping_np.shape[1]
    prior_power = tuple([1. / taps] * taps)
    states = {}
    for method in args.methods:
        states[method] = []
        if method == "angular-gaussian" and is_step:
            continue
        if method in ("gaussian-ou", "angular-gaussian", "residual-ucb"):
            for coherence in args.coherences:
                prior = GaussianPrior(prior_power, coherence)
                if method == "angular-gaussian":
                    states[method].append(AngularGaussianLikelihood(
                        prior, dtype=complex_dtype, check_inputs=False))
                else:
                    states[method].append(GaussianStateFilter(
                        prior, dtype=complex_dtype, check_inputs=False))
        elif method in ("phase-frequency", "phase-frequency-ucb"):
            frequency_grid = None
            if args.frequency_step is not None:
                half = math.ceil(args.frequency_bound / args.frequency_step)
                if half > 512:
                    raise ValueError("explicit frequency grid exceeds 1025 states")
                frequency_grid = tuple(i * args.frequency_step for i in range(-half, half + 1))
            model = PhaseFrequencyModel(
                phase_states=args.phase_states,
                relative_phase_states=args.relative_phase_states,
                frequency_bound_hz=args.frequency_bound,
                frequency_grid_hz=frequency_grid,
                maximum_frequency_states=1025,
                nominal_dt_seconds=workspace.block_samples / workspace.sample_rate,
                frequency_model=args.frequency_model,
                frequency_std_hz=args.frequency_std,
                frequency_correlation_seconds=args.frequency_coherence,
                random_walk_hz_per_sqrt_second=args.frequency_random_walk,
                phase_diffusion_rad_per_sqrt_second=args.phase_diffusion)
            phase_type = PhaseFrequencyLikelihood
            if args.phase_implementation != "reference":
                from triad_phase_optimized import OptimizedPhaseFrequencyLikelihood, GraphPhaseFrequencyLikelihood
                phase_type = (GraphPhaseFrequencyLikelihood if args.phase_implementation == "graph"
                              else OptimizedPhaseFrequencyLikelihood)
            if args.phase_implementation == "graph-fp64":
                from triad_phase_reduction import Fp64ReductionGraphLikelihood
                phase_type = Fp64ReductionGraphLikelihood
            states[method].append(phase_type(
                trials, mapping_np.shape[0], taps, model, device=device,
                dtype=real_dtype, memory_limit_bytes=args.memory_limit_bytes,
                validate_inputs=False))
    return states


def _final_bit_scores(method: str, states: list, labels: torch.Tensor,
                      device: torch.device) -> np.ndarray | None:
    if not states:
        return None
    if method == "phase-frequency" or method == "phase-frequency-ucb":
        candidate = states[0].scores()
        bit = _bit_mixture(candidate, labels)
        return bit.detach().cpu().numpy()
    per_model = []
    for state in states:
        raw = state.finish()
        per_model.append(_bit_mixture(raw, labels))
    return _logmeanexp(torch.stack(per_model, 0), 0).detach().cpu().numpy()


def _classify(scores: np.ndarray, truth_bit: int, gate: float, control: str) -> dict:
    values = np.asarray(scores, dtype=np.float64)
    winners = np.argmax(values, axis=1)
    admitted = np.max(values, axis=1) >= gate
    if control == "signal":
        correct = int(np.count_nonzero(admitted & (winners == truth_bit)))
        wrong = int(np.count_nonzero(admitted & (winners != truth_bit)))
        return {"captures": len(values), "correct_accepted": correct,
                "wrong_accepted": wrong, "missed": len(values)-correct-wrong,
                "raw_correct": int(np.count_nonzero(winners == truth_bit))}
    return {"captures": len(values), "false_admitted": int(admitted.sum()),
            "not_admitted": int(len(values)-admitted.sum())}


def _capture_blocks(args, workspace, path_index, control, reference_variance,
                    noise_schedule, *, actual):
    """Replayable bounded stream; per-block RNG addresses never depend on pass."""
    if actual:
        for block in workspace.iter_received_blocks(reference_variance=reference_variance,
                                                    tile_blocks=args.tile_blocks):
            yield (int(block.index), np.asarray(block.r, dtype=np.complex128),
                   np.asarray(block.gram, dtype=np.complex128),
                   np.asarray([block.received_energy], dtype=np.float64),
                   int(block.valid_rows), float(block.dt_seconds), 0.)
        return
    if not workspace.gaussian_projection_supported:
        raise WorkspaceError("projected simulation unsupported for this sampled frontend")
    means = np.memmap(workspace.directory/workspace.manifest["mean_file"], dtype="<c16", mode="r",
                      shape=(workspace.block_count, workspace.dimension))
    truth_energy = np.memmap(workspace.directory/workspace.manifest["signal_energy_file"], dtype="<f8", mode="r",
                             shape=(workspace.block_count,))
    root = np.random.SeedSequence([args.seed, SPLIT_SALT[args.split], path_index]).generate_state(1, dtype=np.uint64)[0].item()
    for index, G, interval, valid, center, dt in _public_blocks(workspace):
        rng = np.random.default_rng(np.random.SeedSequence([root, index]))
        start = time.perf_counter()
        r, energy = project_joint_statistics(G, means[index], float(truth_energy[index]),
                        valid, reference_variance*noise_schedule[index], rng,
                        args.trials, control == "signal")
        seconds = time.perf_counter()-start
        yield (index, r/reference_variance, _rank_stable_gram(G)/reference_variance,
               energy/reference_variance, valid, dt, seconds)


def _hash_capture_block(digest, block):
    index, r, G, energy, valid, dt, _ = block
    digest.update(np.asarray([index, valid], dtype="<u8").tobytes())
    digest.update(np.asarray([dt], dtype="<f8").tobytes())
    for array, dtype in ((r, "<c16"), (G, "<c16"), (energy, "<f8")):
        digest.update(np.asarray(array, dtype=dtype).tobytes(order="C"))


def _pool_stationary_residuals(blocks, delta: float, *, device):
    """First-pass independent whole-bank residual pool; no noise truth used."""
    total_energy = correction = total_dof = None
    digest = hashlib.sha256()
    count = 0
    sampler_seconds = 0.
    start = time.perf_counter()
    for block in blocks:
        _hash_capture_block(digest, block)
        index, r, G, energy, valid, dt, sampling = block
        sampler_seconds += sampling
        residual = whole_bank_residual(
            torch.as_tensor(np.array(r, copy=True), dtype=torch.complex128, device=device),
            torch.as_tensor(np.array(G, copy=True), dtype=torch.complex128, device=device),
            torch.as_tensor(np.array(energy, copy=True), dtype=torch.float64, device=device), valid)
        if total_energy is None:
            total_energy = torch.zeros_like(residual.energy, dtype=torch.float64)
            correction = torch.zeros_like(total_energy)
            total_dof = torch.zeros_like(residual.complex_dof, dtype=torch.float64)
        adjusted = residual.energy-correction
        updated = total_energy+adjusted
        correction = (updated-total_energy)-adjusted
        total_energy = updated
        total_dof += residual.complex_dof.to(torch.float64)
        count += 1
    if total_energy is None:
        raise ValueError("stationary residual pool is empty")
    upper = variance_upper_confidence(total_energy, total_dof, delta)
    if device.type == "cuda":
        torch.cuda.synchronize(device)
    return upper, {"blocks": count, "complex_dof": total_dof.detach().cpu().tolist(),
        "residual_energy_quantiles_05_50_95": np.quantile(total_energy.detach().cpu().numpy(), [.05, .5, .95]).tolist(),
        "variance_upper_to_reference_quantiles_05_50_95": np.quantile(upper.detach().cpu().numpy(), [.05, .5, .95]).tolist(),
        "pass_statistics_sha256": digest.hexdigest(),
        "wall_seconds": time.perf_counter()-start,
        "sampler_wall_seconds": sampler_seconds,
        "assumption": "one constant unknown proper-complex white-noise variance across the completed window; independent whole-bank residual coordinates",
        "memory_scope": "FP64 pooled residual energy/dof and one streamed block; observations are replayed, not retained"}


def _run_capture(args, workspace: BlockWorkspace, path_index: int, control: str,
                 truth_bit: int, mapping_np: np.ndarray, labels_np: np.ndarray,
                 reference_variance: float, noise_schedule: list[float],
                 method_gates: dict, *, actual: bool) -> tuple[dict, dict[str, np.ndarray]]:
    trials = 1 if actual else args.trials
    is_step = any(abs(value-noise_schedule[0]) > 1e-15 for value in noise_schedule)
    has_ucb = any(method in args.methods for method in ("residual-ucb", "phase-frequency-ucb"))
    stationary_ucb = has_ucb and args.calibration_scope == "stationary"
    if stationary_ucb and is_step:
        raise ValueError("stationary UCB cannot pool nonconstant noise power")
    receivers = _new_states(args, workspace, mapping_np, trials, is_step)
    gates = method_gates
    device = torch.device(args.device)
    complex_dtype = torch.complex128 if args.precision == "fp64" else torch.complex64
    real_dtype = torch.float64 if args.precision == "fp64" else torch.float32
    labels = torch.as_tensor(labels_np, dtype=torch.long, device=device)
    mapping = torch.as_tensor(mapping_np, dtype=torch.long, device=device)
    update_seconds = {method: 0. for method in args.methods}
    sampler_seconds = 0.
    ucb_seconds = 0.
    ucb_covered = ucb_checked = 0
    total_rows = 0
    def fresh_stream():
        return _capture_blocks(args, workspace, path_index, control,
                               reference_variance, noise_schedule, actual=actual)
    ucb_gate = gates.get("residual-ucb", gates.get("phase-frequency-ucb"))
    delta = ucb_gate["ucb_budget"]["delta_per_segment"] if has_ucb else None
    pooled_upper = pool_info = None
    replay_digest = hashlib.sha256() if stationary_ucb else None
    start = time.perf_counter()
    with torch.inference_mode():
        if stationary_ucb:
            pooled_upper, pool_info = _pool_stationary_residuals(fresh_stream(), delta, device=device)
        for block_item in fresh_stream():
            block_index, r_np, G_np, E_np, valid_rows, dt_seconds, sampling_seconds = block_item
            sampler_seconds += sampling_seconds
            if replay_digest is not None:
                _hash_capture_block(replay_digest, block_item)

            if valid_rows < 1:
                raise ValueError(f"zero-row block {block_index} is outside this capture profile")
            total_rows += valid_rows
            r_reduction = torch.as_tensor(np.array(r_np, copy=True), dtype=torch.complex128, device=device)
            G_reduction = torch.as_tensor(np.array(G_np, copy=True), dtype=torch.complex128, device=device)
            r_full = r_reduction.to(complex_dtype)
            G_full = G_reduction.to(complex_dtype)
            E_full = torch.as_tensor(np.asarray(E_np).copy(), dtype=torch.float64, device=device)
            r_bank, G_bank = _candidate_views(r_full, G_full, mapping)

            r_ucb_bank = G_ucb_bank = None
            if has_ucb:
                t0 = time.perf_counter()
                residual = whole_bank_residual(r_reduction, G_reduction, E_full, valid_rows)
                upper = pooled_upper if stationary_ucb else variance_upper_confidence(residual.energy, residual.complex_dof, delta)
                r_ucb_full, G_ucb_full = normalize_by_variance_upper(
                    residual.projected_correlations, residual.projected_gram, upper)
                r_ucb_bank, G_ucb_bank = _candidate_views(r_ucb_full.to(complex_dtype), G_ucb_full.to(complex_dtype), mapping)
                ucb_seconds += time.perf_counter() - t0
                if actual:
                    actual_ratio = workspace.noise_variance / reference_variance
                    comparison_ratio = torch.full_like(upper, actual_ratio)
                else:
                    comparison_ratio = torch.full_like(upper, noise_schedule[block_index])
                if not stationary_ucb or block_index == 0:
                    ucb_covered += int(torch.count_nonzero(upper >= comparison_ratio).item())
                    ucb_checked += int(upper.numel())

            if actual:
                angular_energy = E_full[:, None]
            else:
                angular_energy = E_full[:, None]
            for method in args.methods:
                if method == "angular-gaussian" and is_step:
                    continue
                t0 = time.perf_counter()
                if device.type == "cuda":
                    torch.cuda.synchronize(device)
                if method in ("gaussian-ou", "angular-gaussian"):
                    for state in receivers[method]:
                        if method == "gaussian-ou":
                            state.update(r_bank, G_bank, dt_seconds)
                        else:
                            state.update(r_bank, G_bank, dt_seconds, angular_energy, valid_rows)
                elif method == "residual-ucb":
                    for state in receivers[method]:
                        state.update(r_ucb_bank, G_ucb_bank, dt_seconds)
                elif method == "phase-frequency":
                    receivers[method][0].update(r_bank, G_bank, dt_seconds)
                elif method == "phase-frequency-ucb":
                    receivers[method][0].update(r_ucb_bank, G_ucb_bank, dt_seconds)
                if device.type == "cuda":
                    torch.cuda.synchronize(device)
                update_seconds[method] += time.perf_counter() - t0
    if device.type == "cuda":
        torch.cuda.synchronize(device)
    if stationary_ucb:
        replay_hash = replay_digest.hexdigest()
        if replay_hash != pool_info["pass_statistics_sha256"]:
            raise RuntimeError("stationary UCB replay statistics differ from calibration pass")
        pool_info["replay_statistics_sha256"] = replay_hash
        pool_info["replay_identity_verified"] = True
        pool_info["allocated_calibration_alternatives"] = args.calibration_alternatives
        pool_info["actual_calibration_pools"] = 1
    score_arrays = {}
    method_results = {}
    for method in args.methods:
        gate_info = gates[method]
        if method == "angular-gaussian" and is_step:
            method_results[method] = {"status": "skipped_model_mismatch",
                                      "reason": "AngularGaussianLikelihood assumes one common noise scale over the complete fixed window; configured scale changes by block"}
            continue
        bit_scores = _final_bit_scores(method, receivers[method], labels, device)
        score_arrays[method] = bit_scores
        counts = _classify(bit_scores, truth_bit, gate_info["threshold_log_likelihood_ratio"], control)
        maximum = np.max(bit_scores, axis=1)
        ratio_matches = (all(abs(x-1.) <= 1e-12 for x in noise_schedule) if not actual else
                         abs(workspace.noise_variance/reference_variance - 1.) <= 1e-12)
        if method in ("gaussian-ou", "phase-frequency"):
            if not workspace.gaussian_projection_supported:
                calibration_status = "diagnostic_only_frontend_model_not_gaussian"
            else:
                calibration_status = ("known_variance_model_matches_reference" if ratio_matches
                                      else "diagnostic_only_known_variance_mismatch_uncalibrated")
        elif method == "angular-gaussian":
            calibration_status = ("common_scale_angular_model" if workspace.gaussian_projection_supported
                                  else "diagnostic_only_frontend_model_not_gaussian")
        else:
            calibration_status = ("independent_whole_bank_ucb_budgeted" if workspace.gaussian_projection_supported
                                  else "diagnostic_only_frontend_model_not_gaussian")
        diagnostics = receivers[method][0].diagnostics() if receivers[method] else {}
        method_results[method] = {
            "status": "scored", "calibration_status": calibration_status,
            "threshold": gate_info, "counts": counts,
            "maximum_score_quantiles_05_50_95": [float(x) for x in np.quantile(maximum, [.05, .5, .95])],
            "update_wall_seconds": update_seconds[method],
            "shared_ucb_prepare_wall_seconds": ucb_seconds if method in ("residual-ucb", "phase-frequency-ucb") else 0.,
            "stationary_pool_first_pass_wall_seconds": pool_info["wall_seconds"] if pool_info and method in ("residual-ucb", "phase-frequency-ucb") else 0.,
            "receiver_diagnostics": diagnostics,
        }
    if device.type == "cuda":
        torch.cuda.synchronize(device)
    wall = time.perf_counter() - start
    capture = {
        "status": "completed",
        "physical_path_index": path_index,
        "control": control,
        "truth_bit_simulation_only": truth_bit if control == "signal" else None,
        "conditional_noise_trials": trials,
        "valid_raw_complex_samples": total_rows,
        "wall_seconds_joint": wall,
        "projected_sampler_wall_seconds": sampler_seconds,
        "whole_bank_ucb_prepare_wall_seconds": ucb_seconds,
        "calibration_scope": args.calibration_scope if has_ucb else None,
        "stationary_residual_pool": pool_info,
        "whole_bank_ucb_truth_coverage_evaluation_only": {"covered": ucb_covered, "segments_checked": ucb_checked},
        "methods": method_results,
    }
    return capture, score_arrays


def _noise_schedule(args, block_count: int) -> list[float]:
    if args.noise_step_factor is None and args.noise_step_fraction is None:
        return [args.noise_scale] * block_count
    if args.noise_step_factor is None or args.noise_step_fraction is None:
        raise ValueError("noise-step-factor and noise-step-fraction must be provided together")
    if (not math.isfinite(args.noise_step_factor) or args.noise_step_factor <= 0
            or not math.isfinite(args.noise_step_fraction)
            or not 0 <= args.noise_step_fraction <= 1):
        raise ValueError("noise step factor must be positive and fraction in [0,1]")
    cut = int(math.floor(block_count * args.noise_step_fraction))
    return [args.noise_scale] * cut + [args.noise_scale * args.noise_step_factor] * (block_count-cut)


def _noise_schedule_description(schedule: list[float]) -> dict:
    if not schedule:
        return {"kind": "empty"}
    changes = [index for index in range(1, len(schedule))
               if schedule[index] != schedule[index-1]]
    if not changes:
        return {"kind": "constant_power_multiplier", "value": schedule[0]}
    cut = changes[0]
    if len(changes) != 1:
        raise ValueError("only one piecewise-constant noise transition is supported")
    return {"kind": "piecewise_constant_power_multiplier", "first_segment_value": schedule[0],
            "second_segment_first_block": cut, "second_segment_value": schedule[cut]}


def _validate_path_profiles(workspaces: list[BlockWorkspace]) -> None:
    first = workspaces[0]
    keys = ("sample_rate", "sample_count", "block_samples", "block_count", "dimension",
            "column_mapping", "candidates", "column_descriptors")
    reference = {key: first.manifest.get(key) for key in keys}
    try:
        # Keep an owned copy; the memmap is closed in the finally block.
        reference_intervals = np.array(first._map(
            "intervals.u64", first._U64_DTYPE, (first.block_count, 2)), copy=True)
        for workspace in workspaces[1:]:
            actual = {key: workspace.manifest.get(key) for key in keys}
            if actual != reference:
                raise WorkspaceError(f"path {workspace.directory} has a different public bank or sample calendar")
            for field in ("channel_model", "oscillator_model", "source_format", "channel", "control"):
                if workspace.manifest.get(field) != first.manifest.get(field):
                    raise WorkspaceError(f"path {workspace.directory} has different generative {field}")
            cursor = 0
            for tile in workspace.iter_public(tile_blocks=workspace.max_tile_blocks):
                count = len(tile.intervals)
                if not np.array_equal(tile.intervals, reference_intervals[cursor:cursor+count]):
                    raise WorkspaceError(f"path {workspace.directory} has different block intervals")
                cursor += count
                del tile
            if cursor != workspace.block_count:
                raise WorkspaceError(f"path {workspace.directory} public calendar ended early")
            # Independent physical paths may have different outage/erasure masks.
            # Each reader validates its own valid-row counts against this calendar.
            _close_workspace_maps(workspace)
    finally:
        for workspace in workspaces:
            _close_workspace_maps(workspace)


def _controls_for_workspace(args, workspace: BlockWorkspace) -> list[str]:
    if args.mode == "projected":
        return list(args.controls)
    declared = workspace.manifest.get("control")
    if declared == "wrong-key":
        raise WorkspaceError("actual wrong-key captures are not signal/null trials; export a supported control")
    if declared not in ("signal", "null", "wrong-key"):
        declared = "signal" if workspace.manifest.get("signal_enabled") is True else "null"
    control = "signal" if declared in ("signal", "wrong-key") else "null"
    if args.controls is not None and args.controls != [control]:
        raise WorkspaceError(f"actual control {control!r} disagrees with requested controls {args.controls}")
    return [control]


def _file_hashes(workspace: BlockWorkspace) -> dict[str, str]:
    names = ["manifest.json", "gram.c128", "intervals.u64", "valid_rows.u64"]
    for key in ("mean_file", "signal_energy_file", "observation_file", "received_energy_file"):
        name = workspace.manifest.get(key)
        if name:
            names.append(name)
    return {name: _sha256(workspace.directory / name) for name in names}


def _confidence_summary(captures: list[dict], method: str, control: str,
                        alpha: float) -> dict:
    # A physical path is the independent unit. Conditional noise replicates
    # within one path are not promoted to independent-channel evidence.
    path_fractions = []
    total = correct = wrong = missed = false = conditional = 0
    for capture in captures:
        row = capture["methods"].get(method, {})
        counts = row.get("counts")
        if not counts:
            continue
        if control == "signal":
            n = counts["captures"]
            c, w = counts["correct_accepted"], counts["wrong_accepted"]
            path_fractions.append(c / n if n else 0.)
            correct += c; wrong += w; missed += counts["missed"]; total += n
        else:
            n = counts["captures"]
            path_fractions.append(counts["false_admitted"] / n if n else 0.)
            false += counts["false_admitted"]; total += n
        conditional += counts["captures"]
    npaths = len(path_fractions)
    # Hoeffding interval for independent, bounded per-path conditional success
    # fractions. It remains valid when trial counts differ across paths.
    margin = min(1., math.sqrt(math.log(2. / alpha) / (2. * npaths))) if npaths else None
    mean = float(np.mean(path_fractions)) if path_fractions else None
    return {"independent_physical_paths": npaths,
            "conditional_captures": conditional,
            "failed_physical_capture_records": sum(capture.get("status") == "error" for capture in captures),
            "aggregate_counts": {"captures": total, "correct_accepted": correct,
                                 "wrong_accepted": wrong, "missed": missed,
                                 "false_admitted": false},
            "path_mean_fraction": mean,
            "path_mean_fraction_hoeffding_two_sided":
                None if mean is None else [max(0., mean-margin), min(1., mean+margin)],
            "confidence_level": 1.-alpha,
            "unit": "independent physical paths; within-path conditional replicates are averaged first"}


def run(args) -> dict:
    if args.threads > 2:
        raise ValueError("local harness is bounded to at most two CPU threads")
    torch.set_num_threads(args.threads)
    torch.set_float32_matmul_precision("highest")
    if args.device.startswith("cuda") and not torch.cuda.is_available():
        raise ValueError("CUDA requested but unavailable")
    if args.mode == "projected":
        projection_validation = _projection_self_test(
            args.seed + 991, args.projection_validation_trials)
    else:
        projection_validation = {"status": "not_run", "reason": "actual sampled observations"}

    workspaces = [BlockWorkspace(path, max_tile_blocks=args.tile_blocks,
                                 memory_limit_bytes=args.memory_limit_bytes)
                  for path in args.inputs]
    resolved_paths = [str(workspace.directory.resolve()) for workspace in workspaces]
    if len(set(resolved_paths)) != len(resolved_paths):
        raise WorkspaceError("the same physical workspace was supplied more than once")
    identities = []
    for workspace in workspaces:
        m = workspace.manifest
        identity_fields = ("channel_seed", "key_seed", "epoch")
        if all(field in m for field in identity_fields):
            identities.append(tuple(m[field] for field in identity_fields))
        else:
            identities.append(None)
    known_identities = [identity for identity in identities if identity is not None]
    if len(set(known_identities)) != len(known_identities):
        raise WorkspaceError("duplicate sampled-path channel/key/epoch identity would inflate independent-path counts")
    _validate_path_profiles(workspaces)
    reports, npz_arrays = [], {}
    start = time.perf_counter()
    gates_by_path = {}
    for path_index, workspace in enumerate(workspaces):
        m = workspace.manifest
        if not workspace.has_observation and args.mode == "actual":
            raise WorkspaceError(f"actual mode requires received samples in {workspace.directory}")
        selected_indices, mapping_np, labels_np = _selected_candidates(workspace, args.taps)
        if args.mode == "projected":
            batch_bytes = args.trials * (4 * workspace.dimension * 16 + 128)
            if batch_bytes > args.memory_limit_bytes:
                raise WorkspaceError(f"projected trial batch needs about {batch_bytes} bytes, over memory limit")
        cn0_variance = workspace.sample_rate / (10. ** (args.cn0 / 10.))
        reference_variance = args.reference_variance or cn0_variance
        if not math.isfinite(reference_variance) or reference_variance <= 0:
            raise ValueError("computed reference variance is invalid")
        noise_schedule = _noise_schedule(args, workspace.block_count)
        if args.mode == "actual":
            noise_schedule = [workspace.noise_variance / reference_variance] * workspace.block_count
        truth_bit = int(m.get("signal_bit_simulation_truth", m.get("signal_bit", 0)))
        if truth_bit not in (0, 1):
            raise WorkspaceError("exported simulation truth bit must be zero or one")
        attempts, method_gates = _gates(args, workspace.block_count)
        gates_by_path[str(workspace.directory)] = method_gates
        for control in _controls_for_workspace(args, workspace):
            try:
                capture, score_arrays = _run_capture(
                    args, workspace, path_index, control, truth_bit, mapping_np,
                    labels_np, reference_variance, noise_schedule, method_gates,
                    actual=args.mode == "actual")
            except Exception as error:
                capture, score_arrays = ({
                    "status": "error", "physical_path_index": path_index,
                    "control": control, "conditional_noise_trials": 1 if args.mode == "actual" else args.trials,
                    "error_type": type(error).__name__, "error": str(error),
                    "methods": {},
                }, {})
            capture.update({
                "input": str(workspace.directory),
                "manifest_sha256": _sha256(workspace.manifest_path),
                "input_hashes": _file_hashes(workspace),
                "sample_rate_hz": workspace.sample_rate,
                "sample_count": workspace.sample_count,
                "block_count": workspace.block_count,
                "candidate_count": len(workspace.candidates),
                "selected_candidate_indices": selected_indices,
                "selected_candidate_count": len(selected_indices),
                "selected_candidate_taps": args.taps,
                "calibration_full_bank_dimension": workspace.dimension,
                "reference_variance": reference_variance,
                "declared_cn0_db_hz": args.cn0,
                "noise_power_multiplier_schedule": _noise_schedule_description(noise_schedule),
                "simulation_truth_evaluation_only": {
                    "manifest_noise_variance": workspace.noise_variance,
                    "manifest_noise_to_reference_ratio": workspace.noise_variance / reference_variance,
                    "truth_bit": truth_bit if control == "signal" else None,
                    "control": m.get("control"),
                    "channel": m.get("channel"),
                },
                "attempts_per_day_model": attempts,
            })
            array_prefix = f"path{path_index:05d}_{control}"
            for method, scores in score_arrays.items():
                npz_arrays[f"{array_prefix}_{method}"] = scores
            reports.append(capture)
        # Only one physical path is resident at a time. Closing the workspace
        # cache here prevents FD usage from growing with the input batch.
        _close_workspace_maps(workspace)

    grouped = {}
    for capture in reports:
        grouped.setdefault(capture["control"], []).append(capture)
    summary = {}
    for control, captures in grouped.items():
        summary[control] = {
            method: _confidence_summary(captures, method, control, .05)
            for method in args.methods
        }
    output = args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    npz_path = output.with_suffix(".scores.npz")
    np.savez_compressed(npz_path, **npz_arrays)
    elapsed = time.perf_counter() - start
    report = {
        "schema": "triad-noise-study-v1",
        "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "scope": "independent block-workspace likelihood comparison; projected statistics validated against dense raw Gaussian samples; not field qualification",
        "configuration": _json_model_parameters({
            key: value for key, value in vars(args).items()
            if key not in ("inputs", "output", "memory_limit_bytes")}),
        "inputs": [{"path": str(w.directory), "manifest_sha256": _sha256(w.manifest_path),
                    "hashes": _file_hashes(w)} for w in workspaces],
        "path_independence": {"rule": "unique workspaces; duplicate exported channel_seed/key_seed/epoch identities rejected when available",
                              "basis": "independence is supplied by sampled simulation path seeds, not inferred from outcomes"},
        "projection_sampler_validation": projection_validation,
        "family": {"false_probability": args.false_probability,
                   "horizon_seconds": args.horizon_seconds,
                   "attempt_spacing_seconds": args.attempt_spacing,
                   "duration_alternatives": args.duration_alternatives,
                   "external_selection_methods": args.selection_methods,
                   "calibration_alternatives": args.calibration_alternatives,
                   "reserved_calibration_probability": args.false_probability*.5,
                   "attempts_per_horizon": math.ceil(args.horizon_seconds / args.attempt_spacing),
                   "gates_by_path": gates_by_path},
        "summary_by_control": summary,
        "capture_outcomes": {
            "attempted_capture_records": len(reports),
            "completed_capture_records": sum(capture.get("status") == "completed" for capture in reports),
            "failed_capture_records": sum(capture.get("status") == "error" for capture in reports),
            "failed_records_are_excluded_from_reception_denominators": True,
        },
        "captures": reports,
        "scores_file": npz_path.name,
        "scores_sha256": _sha256(npz_path),
        "wall_seconds_joint": elapsed,
        "host": {"python": platform.python_version(), "numpy": np.__version__,
                 "torch": torch.__version__, "device": args.device,
                 "torch_threads": torch.get_num_threads()},
        "limitations": ["within-path conditional trials are not independent physical paths",
                        "angular likelihood assumes one common white-noise scale over a complete fixed window",
                        "Gaussian and phase-frequency known-variance rows are uncalibrated when the declared reference misses actual variance",
                        "stationary residual pooling assumes one constant unknown power over the completed window; per-block calibration permits independently varying block powers",
                        "stationary calibration replays observations in a second pass and includes that overhead; no full raw capture is retained",
                        "GPU batching is across conditional projected trials within one physical path; independent paths are currently processed serially",
                        "projected mode models independent white Gaussian noise in exported block subspaces; it does not resample raw channel waveforms"],
    }
    # Keep JSON free of accidental numpy scalars and invalid NaN/Infinity.
    output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    return report


def main(argv=None) -> int:
    args = _parse_args(argv)
    try:
        report = run(args)
    except Exception as error:
        raise SystemExit(f"triad-noise-study failed: {type(error).__name__}: {error}") from error
    outcomes = report["capture_outcomes"]
    print(json.dumps({"output": str(args.output), "scores": report["scores_file"],
                      **outcomes, "wall_seconds_joint": report["wall_seconds_joint"]}, sort_keys=True))
    return 2 if outcomes["failed_capture_records"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
