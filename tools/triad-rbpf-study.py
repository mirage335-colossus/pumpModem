#!/usr/bin/env python3
"""Actual sampled-observation phase/channel particle likelihood study.

Only actual correlations, received energy and public full-bank Grams are read.
The channel is conditionally Gaussian; phase is constant inside an exported
block. A finite, normalized acquisition mixture is scored for each bit. This
is a fixed-window experiment, not an anytime decoder or duration planner.
"""
from __future__ import annotations

import argparse
from contextlib import ExitStack
import hashlib
import json
import math
from pathlib import Path
import platform
import time

import numpy as np
import torch
from scipy.stats import beta

from triad_gaussian_state import GaussianPrior
from triad_noise_likelihood import FalseAdmissionBudget, variance_upper_confidence, whole_bank_residual
from triad_rbpf import RBPhaseFilter, RBPhasePrior
from triad_state_workspace import BlockWorkspace, WorkspaceError

METHODS = ("rbpf", "rbpf-ucb")
INPUT_FILES = ("manifest.json", "gram.c128", "observation.c128", "received_energy.f64", "intervals.u64", "valid_rows.u64")


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024**2), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_args(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("inputs", nargs="+", type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--cn0", type=float, help="declared reference total-triplet C/N0 dB-Hz; never simulator truth")
    p.add_argument("--reference-variance", type=float, help="explicit receiver reference; overrides --cn0 if both supplied")
    p.add_argument("--methods", choices=METHODS, nargs="+", default=list(METHODS))
    p.add_argument("--taps", type=int, choices=(1, 2), default=2)
    p.add_argument("--tap-powers", type=float, nargs="+", help="proper-complex per-tap variances; default total power one")
    p.add_argument("--coherence", type=float, default=math.inf, help="Gaussian tap OU coherence seconds; inf gives static taps")
    p.add_argument("--frequency-bound", type=float, default=0., help="initial common CFO prior +/- Hz, independent of basis tap Dopplers")
    p.add_argument("--phase-diffusion", type=float, default=0., help="common Wiener phase rad/sqrt(second)")
    p.add_argument("--frequency-random-walk", type=float, default=0., help="common Brownian frequency Hz/sqrt(second)")
    p.add_argument("--particles", type=int, default=128)
    p.add_argument("--seed", type=int, default=1, help="independent predeclared algorithm seed; never optimize over seeds")
    p.add_argument("--resampling", choices=("none", "multinomial", "systematic"), default="none")
    p.add_argument("--ess-fraction", type=float, default=.5)
    p.add_argument("--device", default="cpu")
    p.add_argument("--precision", choices=("fp32", "fp64"), default="fp64")
    p.add_argument("--threads", type=int, default=2)
    p.add_argument("--memory-gib", type=float, default=1.)
    p.add_argument("--false-probability", type=float, default=1e-4)
    p.add_argument("--horizon-seconds", type=float, default=86400.)
    p.add_argument("--attempt-spacing", type=float, default=60.)
    p.add_argument("--duration-alternatives", type=int, default=4)
    p.add_argument("--selection-methods", type=int, default=11)
    p.add_argument("--calibration-alternatives", type=int, default=2)
    p.add_argument("--correct-reception-target", type=float, default=.99, help="reporting target only; does not change admission threshold")
    a = p.parse_args(argv)
    if a.output.exists() or a.output.with_suffix(".scores.npz").exists():
        p.error("report and score paths must both be fresh")
    if a.cn0 is None and a.reference_variance is None:
        p.error("supply --cn0 or --reference-variance explicitly")
    if a.cn0 is not None and not math.isfinite(a.cn0):
        p.error("C/N0 must be finite")
    for name in ("reference_variance", "memory_gib", "horizon_seconds", "attempt_spacing"):
        value = getattr(a, name)
        if value is not None and (not math.isfinite(value) or value <= 0):
            p.error(name + " must be positive finite")
    if a.memory_gib > 1024 or a.horizon_seconds/a.attempt_spacing > 1e9:
        p.error("memory or attempt family exceeds explicit supported range")
    if not 1 <= a.threads <= 2 or len(a.methods) != len(set(a.methods)):
        p.error("use one or two CPU threads and distinct methods")
    for name in ("selection_methods", "duration_alternatives", "calibration_alternatives"):
        if not 1 <= getattr(a, name) <= 1_000_000_000:
            p.error(name + " must be in 1..1000000000")
    if a.selection_methods < len(a.methods):
        p.error("selection family must include all selected methods")
    if not math.isfinite(a.correct_reception_target) or not 0 < a.correct_reception_target < 1:
        p.error("correct reception target must be strictly between zero and one")
    a.memory_limit_bytes = int(a.memory_gib*1024**3)
    try:
        a.tap_prior = GaussianPrior(tuple(a.tap_powers) if a.tap_powers is not None else (1/a.taps,)*a.taps, a.coherence)
        if len(a.tap_prior.stationary_power) != a.taps:
            raise ValueError("tap power count must match --taps")
        a.phase_prior = RBPhasePrior(a.frequency_bound, a.phase_diffusion, a.frequency_random_walk)
        RBPhaseFilter(a.tap_prior, a.phase_prior, particles=a.particles, seed=a.seed,
                      resampling=a.resampling, ess_fraction=a.ess_fraction, memory_limit_bytes=a.memory_limit_bytes)
        a.budget = FalseAdmissionBudget(false_probability=a.false_probability,
            attempts=math.ceil(a.horizon_seconds/a.attempt_spacing), duration_alternatives=a.duration_alternatives,
            model_alternatives=a.selection_methods, candidates=2, calibration_segments=a.calibration_alternatives)
        a.budget.diagnostics()
    except (ValueError, OverflowError, TypeError) as error:
        p.error(str(error))
    return a


def actual_blocks(workspace, reference):
    """Bounded sequential reads; no mean, signal energy or variance truth reads."""
    d = workspace.dimension
    previous_center = None
    with ExitStack() as stack:
        files = {name: stack.enter_context((workspace.directory/name).open("rb")) for name in INPUT_FILES[1:]}
        for j in range(workspace.block_count):
            G = np.fromfile(files["gram.c128"], dtype="<c16", count=d*d)
            r = np.fromfile(files["observation.c128"], dtype="<c16", count=d)
            E = np.fromfile(files["received_energy.f64"], dtype="<f8", count=1)
            interval = np.fromfile(files["intervals.u64"], dtype="<u8", count=2)
            valid = np.fromfile(files["valid_rows.u64"], dtype="<u8", count=1)
            if (G.size, r.size, E.size, interval.size, valid.size) != (d*d, d, 1, 2, 1):
                raise WorkspaceError("actual statistics changed or ended during read")
            expected = (j*workspace.block_samples, min((j+1)*workspace.block_samples, workspace.sample_count))
            if tuple(map(int, interval)) != expected or int(valid[0]) > expected[1]-expected[0]:
                raise WorkspaceError("block calendar or valid-row count differs from public geometry")
            if not np.isfinite(r).all() or not np.isfinite(G).all() or not np.isfinite(E[0]) or E[0] < 0:
                raise WorkspaceError("nonfinite actual statistics or negative received energy")
            center = (int(interval[0])+int(interval[1]))/(2*workspace.sample_rate)
            dt = 0. if previous_center is None else center-previous_center
            if previous_center is not None and dt <= 0:
                raise WorkspaceError("physical block centers do not increase")
            previous_center = center
            yield r/reference, G.reshape(d, d)/reference, float(E[0])/reference, int(valid[0]), dt


def candidate_mapping(workspace, taps):
    indices = [i for i, columns in enumerate(workspace.column_mapping) if len(columns) == taps]
    mapping = np.asarray([workspace.column_mapping[i] for i in indices], dtype=np.int64)
    labels = np.asarray([workspace.candidates[i]["bit"] for i in indices], dtype=np.int64)
    if mapping.ndim != 2 or set(labels.tolist()) != {0, 1}:
        raise WorkspaceError("bank must contain both bits at the requested tap dimension")
    return indices, mapping, labels


def mix_candidates(candidate_scores, labels):
    labels = torch.as_tensor(labels, device=candidate_scores.device)
    if candidate_scores.ndim != 1 or labels.shape != candidate_scores.shape or set(labels.cpu().tolist()) != {0, 1}:
        raise ValueError("candidate scores need one finite mixture for each bit")
    scores = torch.stack([torch.logsumexp(candidate_scores[labels == bit], 0)-math.log(int((labels == bit).sum())) for bit in (0, 1)])
    if not bool(torch.isfinite(scores).all()):
        raise ValueError("nonfinite candidate mixture")
    return scores.detach().cpu().numpy()[None, :]


def classify(scores, truth, threshold, control):
    winner = int(np.argmax(scores[0])); accepted = bool(scores[0, winner] >= threshold)
    if control == "signal":
        return {"captures": 1, "correct_accepted": int(accepted and winner == truth),
                "wrong_accepted": int(accepted and winner != truth), "missed": int(not accepted), "raw_correct": int(winner == truth)}
    return {"captures": 1, "false_admitted": int(accepted), "not_admitted": int(not accepted)}


def summary(captures, method, control):
    rows = [r for r in captures if r["control"] == control]
    good = [r for r in rows if r["status"] == "completed"]
    counts = {key: sum(r["methods"][method]["counts"].get(key, 0) for r in good)
              for key in ("captures", "correct_accepted", "wrong_accepted", "missed", "false_admitted")}
    failures = len(rows)-len(good)
    n = len(rows) if control == "signal" else len(good)
    if control == "signal":
        counts["captures"] = n; counts["missed"] += failures
    successes = counts["correct_accepted"] if control == "signal" else counts["false_admitted"]
    return {"attempted_physical_paths": len(rows), "completed_physical_paths": len(good),
        "failed_physical_paths": failures, "aggregate_counts": counts,
        "pointwise_one_sided_95_lower": float(beta.ppf(.05, successes, n-successes+1)) if successes else 0.,
        "pointwise_one_sided_95_upper": float(beta.ppf(.95, successes+1, n-successes)) if n and successes < n else 1.,
        "confidence_scope": "pointwise independent physical paths; no simultaneous or sequential coverage",
        "null_qualification_complete": failures == 0 if control == "null" else None,
        "wrong_key_campaign_complete": failures == 0 if control == "wrong-key" else None,
        "false_admission_scope": ("empirical mismatched-key signal rejection; not a Gaussian noise/null probability bound"
                                  if control == "wrong-key" else "declared control distribution") }


def calibrate(workspace, reference, delta):
    residual = correction = 0.; dof = valid_total = 0
    for r, G, energy, valid, _ in actual_blocks(workspace, reference):
        result = whole_bank_residual(torch.from_numpy(r[None]), torch.from_numpy(G), energy, valid)
        adjusted = float(result.energy[0])-correction
        updated = residual+adjusted; correction = (updated-residual)-adjusted; residual = updated
        dof += int(result.complex_dof); valid_total += valid
    upper = float(variance_upper_confidence(residual, dof, delta))
    if not math.isfinite(upper) or upper <= 0:
        raise ValueError("stationary noise upper confidence bound is unresolved")
    return upper, {"pooled_residual_energy_over_reference": residual, "pooled_complex_dof": dof,
                   "variance_upper_over_reference": upper, "valid_complex_samples": valid_total,
                   "scope": "one constant white proper-complex noise variance over the whole capture; full-bank residuals"}


def infer(workspace, mapping, labels, reference, args, upper, algorithm_seed):
    dtype = torch.complex128 if args.precision == "fp64" else torch.complex64
    # Reserve one full-bank preparation plus framework-independent temporary
    # arrays before asking the core to allocate its bounded state.
    preparation_bytes = 12*workspace.dimension**2*16+8*workspace.dimension*16+65536
    per_filter_bytes = (args.memory_limit_bytes-preparation_bytes)//len(args.methods)
    if per_filter_bytes < 4096:
        raise MemoryError("full-bank preparation and RB state exceed declared memory limit")
    states = {method: RBPhaseFilter(args.tap_prior, args.phase_prior, particles=args.particles,
                seed=algorithm_seed, dtype=dtype, resampling=args.resampling, ess_fraction=args.ess_fraction,
                memory_limit_bytes=per_filter_bytes) for method in args.methods}
    cpu_preparation = transfer_inference = 0.
    for r, G, energy, valid, dt in actual_blocks(workspace, reference):
        t0 = time.perf_counter()
        result = whole_bank_residual(torch.from_numpy(r[None]), torch.from_numpy(G), energy, valid)
        raw = (r[mapping], G[mapping[:, :, None], mapping[:, None, :]])
        projected = (result.projected_correlations.numpy()[0][mapping]/upper,
                     result.projected_gram.numpy()[mapping[:, :, None], mapping[:, None, :]]/upper) if upper is not None else None
        cpu_preparation += time.perf_counter()-t0
        if args.device.startswith("cuda"): torch.cuda.synchronize(args.device)
        t0 = time.perf_counter()
        for method, state in states.items():
            rr, gg = projected if method == "rbpf-ucb" else raw
            state.update(torch.as_tensor(rr, device=args.device), torch.as_tensor(gg, device=args.device), dt)
        if args.device.startswith("cuda"): torch.cuda.synchronize(args.device)
        transfer_inference += time.perf_counter()-t0
    results = {}
    for method, state in states.items():
        results[method] = {"scores": mix_candidates(state.finish(), labels), "diagnostics": state.diagnostics()}
    return results, {"cpu_fp64_full_bank_preparation_seconds": cpu_preparation,
                     "transfer_and_inference_seconds_joint": transfer_inference,
                     "preparation_bound_bytes": preparation_bytes,
                     "core_bound_bytes_each": per_filter_bytes}


def run(args):
    torch.set_num_threads(args.threads); torch.set_float32_matmul_precision("highest")
    if args.device.startswith("cuda") and not torch.cuda.is_available():
        raise ValueError("CUDA unavailable")
    if args.device.startswith("cuda"):
        # Allocator-stat APIs do not necessarily initialize CUDA themselves.
        # Initialize and select the requested device before resetting its peaks.
        torch.cuda.init()
        torch.cuda.set_device(args.device)
        torch.backends.cuda.matmul.allow_tf32 = False
        torch.cuda.reset_peak_memory_stats(args.device)
    paths = [str(p.resolve()) for p in args.inputs]
    if len(set(paths)) != len(paths): raise WorkspaceError("duplicate physical path")
    captures = []; arrays = {}; seen = set(); started = time.perf_counter()
    for index, path in enumerate(args.inputs):
        ws = None
        row = {"input": str(path), "physical_path_index": index, "control": "signal", "methods": {}}
        try:
            ws = BlockWorkspace(path, max_tile_blocks=1, memory_limit_bytes=args.memory_limit_bytes)
            control = ws.manifest.get("control", "signal"); row["control"] = control
            if control not in ("signal", "null", "wrong-key"): raise WorkspaceError("only actual signal/null/wrong-key controls are supported")
            identity = tuple(ws.manifest.get(k) for k in ("channel_seed", "key_seed", "epoch"))
            if all(x is not None for x in identity):
                if identity in seen: raise WorkspaceError("duplicate channel/key/epoch physical identity")
                seen.add(identity)
            if not ws.has_observation or not ws.has_received_energy: raise WorkspaceError("actual observations and received energy are required")
            if not ws.gaussian_projection_supported: raise WorkspaceError("sampled frontend does not support this Gaussian null")
            indices, mapping, labels = candidate_mapping(ws, args.taps)
            reference = args.reference_variance
            if reference is None:
                try: reference = ws.sample_rate/10**(args.cn0/10)
                except (OverflowError, ZeroDivisionError) as error: raise ValueError("reference variance is unresolved") from error
            if not math.isfinite(reference) or reference <= 0: raise ValueError("reference variance is unresolved")
            calibration = None; upper = None; calibration_seconds = 0.
            if "rbpf-ucb" in args.methods:
                t0 = time.perf_counter(); upper, calibration = calibrate(ws, reference, args.budget.delta_per_segment)
                calibration_seconds = time.perf_counter()-t0
            algorithm_seed = (args.seed+index) % 2**63
            results, timings = infer(ws, mapping, labels, reference, args, upper, algorithm_seed)
            # Evaluation truth is read only after every receiver score is frozen.
            truth = None
            if control != "wrong-key":
                truth = ws.manifest.get("signal_bit_simulation_truth", ws.manifest.get("signal_bit", 0))
                if isinstance(truth, bool) or truth not in (0, 1): raise WorkspaceError("evaluation truth must be a binary integer")
            for method, result in results.items():
                scores = result.pop("scores")
                arrays[f"path{index:05d}_{control}_{method}"] = scores
                row["methods"][method] = dict(result, bit_log_scores=scores[0].tolist(), threshold=args.budget.log_threshold,
                    counts=classify(scores, truth, args.budget.log_threshold, control))
            row.update(status="completed", truth_bit_simulation_only=truth if control == "signal" else None,
                reference_variance=reference, calibration=calibration, calibration_seconds=calibration_seconds,
                timings=timings, algorithm_seed=algorithm_seed, sampled_seconds=ws.sample_count/ws.sample_rate,
                block_count=ws.block_count, full_bank_dimension=ws.dimension, selected_candidate_count=len(indices),
                selected_candidate_indices=indices, candidate_count_by_bit=[int((labels == bit).sum()) for bit in (0, 1)],
                input_hashes={name: sha256(ws.directory/name) for name in INPUT_FILES})
        except Exception as error:
            row.update(status="error", error_type=type(error).__name__, error=str(error))
        finally:
            if ws is not None:
                for array in ws._maps.values(): array._mmap.close()
                ws._maps.clear()
        captures.append(row)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    scores_path = args.output.with_suffix(".scores.npz"); np.savez_compressed(scores_path, **arrays)
    config = {k: v for k, v in vars(args).items() if k not in ("inputs", "output", "budget", "tap_prior", "phase_prior")}
    config["coherence"] = "infinity" if math.isinf(args.coherence) else args.coherence
    config["tap_powers"] = list(args.tap_prior.stationary_power)
    result = {"schema": "triad-rbpf-study-v1", "source_sha256": sha256(__file__),
        "dependency_sha256": {name: sha256(Path(__file__).with_name(name)) for name in ("triad_rbpf.py", "triad_gaussian_state.py", "triad_noise_likelihood.py", "triad_state_workspace.py")},
        "configuration": config, "family": args.budget.diagnostics(), "captures": captures,
        "summary_by_control": {control: {method: summary(captures, method, control) for method in args.methods}
            for control in ("signal", "null", "wrong-key") if any(c["control"] == control for c in captures)},
        "failed_physical_capture_records": sum(c["status"] != "completed" for c in captures),
        "scores_file": scores_path.name, "scores_sha256": sha256(scores_path), "wall_seconds_joint": time.perf_counter()-started,
        "host": {"python": platform.python_version(), "numpy": np.__version__, "torch": torch.__version__, "device": args.device, "threads": torch.get_num_threads()},
        "arithmetic": {"bulk_particle_means_and_products": args.precision, "rank_gram_covariance_factorization": "FP64",
            "phase_frequency_and_evidence_reductions": "FP64", "tf32_allowed": False},
        "limitations": ["Block-constant common phase and Gaussian tap dynamics need block-length and physical-model validation.",
            "K11/D4 is the expanded study family, distinct from earlier K9/D3 operational gates; all additional selected alternatives must be charged.",
            "No-resampling LR is conditional on a predeclared independent prior-path bank; adaptive SMC is calibrated jointly over null noise and independent algorithm randomness, not every fixed adaptive seed.",
            "Known-reference rbpf needs correct noise variance; rbpf-ucb assumes one constant unknown white proper-complex Gaussian variance and uses full-bank independent residuals.",
            "Wrong-key controls contain mismatched-key signals; their false admissions are empirical rejection checks, not covered by the Gaussian noise-only false-admission bound.",
            "Only finite exported acquisition alternatives are covered; acquisition priors are uniform normalized mixtures per bit, not maxima.",
            "First center initializes stationary circular tap prior and phase zero; deltas are physical center separations including erasures and the partial last block.",
            "Bounded streaming arrays exclude framework allocator, file cache and process overhead; independent physical workspaces are processed serially, candidates and particles in parallel.",
            "Correct reception target is a reported experimental target, not a calibrated duration recommendation; no anytime stopping or field-optimality claim."]}
    if args.device.startswith("cuda"):
        result["cuda_memory"] = {"maximum_allocated_bytes": torch.cuda.max_memory_allocated(args.device), "maximum_reserved_bytes": torch.cuda.max_memory_reserved(args.device)}
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n")
    return result


def main(argv=None):
    args = parse_args(argv); result = run(args)
    print(json.dumps({"output": str(args.output), "captures": len(result["captures"]),
                      "failed": result["failed_physical_capture_records"], "wall_seconds_joint": result["wall_seconds_joint"]}))
    return int(bool(result["failed_physical_capture_records"]))


if __name__ == "__main__": raise SystemExit(main())
