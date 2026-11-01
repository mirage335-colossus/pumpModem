#!/usr/bin/env python3
"""Paired exhaustive direct/FFT organization screen on a sampled rank-one bank.

One unknown constant complex gain is fitted per candidate. This is conditional
fixed-waveform acquisition, not the richer phase/channel receiver. No candidate
is pruned. The CPU FP64 reference sums the same represented sampled inputs.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import platform
import statistics
import time

import torch


FFT_PATH = Path(__file__).with_name("triad-fft-acquire.py")
_spec = importlib.util.spec_from_file_location("triad_paired_fft", FFT_PATH)
assert _spec and _spec.loader
fft = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(fft)


def sync(device):
    if device.type == "cuda":
        torch.cuda.synchronize(device)


def timed(fn, device):
    """Synchronized host wall plus stream CUDA events; neither is a HW counter."""
    sync(device)
    first = last = None
    if device.type == "cuda":
        first = torch.cuda.Event(enable_timing=True)
        last = torch.cuda.Event(enable_timing=True)
    start = time.perf_counter()
    if first is not None:
        first.record()
    result = fn()
    if last is not None:
        last.record()
    sync(device)
    return result, {"wall_seconds": time.perf_counter()-start,
                    "cuda_event_seconds": first.elapsed_time(last)/1000 if first is not None else None}


def direct_bank(work):
    """Rows are conjugated CFO-modulated shifted templates in F,C,L order."""
    template = work.templates_cpu.to(device=work.device, dtype=torch.complex64)
    rows = []
    for f in range(len(work.frequencies)):
        for c in range(work.count):
            for lag in work.lags:
                shifted = torch.zeros(work.samples, device=work.device, dtype=torch.complex64)
                shifted[lag:] = template[c, :work.samples-lag]
                rows.append((shifted.conj()*work.phasors[f]).contiguous())
    return torch.stack(rows).transpose(0, 1).contiguous()


def score_direct(observations, bank, work):
    value = observations @ bank
    return value.abs().square().to(torch.float64).reshape(
        observations.shape[0], len(work.frequencies), work.count, len(work.lags)
    )/work.energies[None, None, :, None]


def score_fft(observations, work, *, recompute_templates=False):
    template_fft = work.template_fft_conjugate
    if recompute_templates:
        template = work.templates_cpu.to(device=work.device, dtype=torch.complex64)
        template_fft = torch.fft.fft(template, n=work.nfft, dim=-1).conj().contiguous()
    scores = []
    for first in range(0, len(work.frequencies), work.frequency_tile):
        last = min(len(work.frequencies), first+work.frequency_tile)
        demodulated = observations[:, None, :]*work.phasors[None, first:last, :]
        observed_fft = torch.fft.fft(demodulated, n=work.nfft, dim=-1)
        correlation = torch.fft.ifft(
            observed_fft[:, :, None, :]*template_fft[None, None, :, :], dim=-1)
        scores.append(correlation[:, :, :, work.lag_tensor].abs().square().to(torch.float64)
                      /work.energies[None, None, :, None])
    return torch.cat(scores, dim=1)


def reference_scores(observations, work):
    """Independent full-family complex128 direct sums from the existing scanner."""
    rows = []
    for observation in observations.to(device="cpu", dtype=torch.complex128):
        values = [work.reference(observation, f, c, l)
                  for f in range(len(work.frequencies))
                  for c in range(work.count) for l in range(len(work.lags))]
        rows.append(values)
    return torch.tensor(rows, dtype=torch.float64).reshape(
        len(rows), len(work.frequencies), work.count, len(work.lags))


def summarize(samples):
    values = sorted(samples)
    return {"median": statistics.median(values), "minimum": values[0],
            "p95_nearest_rank": values[math.ceil(.95*len(values))-1], "all": samples}


def observation_seed(base, split, trial):
    """Disjoint versioned RNG domains; never borrow another CLI's split rules."""
    if split not in ("development", "calibration", "holdout"):
        raise ValueError("unknown observation seed split")
    payload = json.dumps(["triad-acquisition-compare-observation-v1", int(base), split,
                          int(trial)], separators=(",", ":")).encode("ascii")
    return int.from_bytes(hashlib.sha256(payload).digest()[:8], "little") & ((1 << 63)-1)


def validate_linear_noise_model(manifest):
    frontend = manifest.get("sampled_frontend", {})
    if not isinstance(frontend, dict):
        raise ValueError("sampled_frontend metadata must be an object")
    if frontend.get("active", False):
        raise ValueError("nonlinear frontend observations cannot use this additive Gaussian sample experiment")
    for flag in ("gaussian_projection_supported", "analytic_admission_supported"):
        if flag in manifest and manifest[flag] is not True:
            raise ValueError(f"{flag} must be true for this additive Gaussian sample experiment")


def metrics(scores, work, truth, trials):
    flattened = scores.flatten(start_dim=1)
    best, code = flattened.max(1)
    candidate = (code//len(work.lags)) % work.count
    labels = torch.tensor(work.labels)[candidate]
    admitted = best >= work.threshold
    h0, h1 = admitted[:trials], admitted[trials:]
    correct = int((h1 & (labels[trials:] == truth)).sum())
    wrong = int((h1 & (labels[trials:] != truth)).sum())
    return {"trials_per_kind": trials, "null_admitted": int(h0.sum()),
            "correct_bit_admitted": correct, "wrong_bit_admitted": wrong,
            "missed": trials-correct-wrong,
            "correct_bit_admission_wilson_95pct_ci": fft.wilson(correct, trials),
            "null_admission_wilson_95pct_ci": fft.wilson(int(h0.sum()), trials),
            "synchronization_success": None,
            "scope": "correct-bit admission on one fixed sampled path; timing/CFO/epoch synchronization success is unmeasured; paired H0/H1 single-observation trials, no empirical operational-horizon tail validation"}


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("manifest", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--device", default="cpu")
    p.add_argument("--trials", type=fft.shared._positive_int, default=32)
    p.add_argument("--batch", type=fft.shared._positive_int, default=8)
    p.add_argument("--repeats", type=fft.shared._positive_int, default=3)
    p.add_argument("--warmups", type=fft.shared._positive_int, default=1)
    p.add_argument("--cn0-grid", type=fft.shared._parse_cn0_grid, default=[0., 2., 4.])
    p.add_argument("--lag-max-seconds", type=fft.shared._finite_float, default=.25)
    p.add_argument("--lag-step-samples", type=fft.shared._positive_int, default=16)
    p.add_argument("--frequency-bound", type=fft.shared._finite_float, default=.02)
    p.add_argument("--frequency-step", type=fft.shared._finite_float, default=.005)
    p.add_argument("--frequency-tile", type=fft.shared._positive_int, default=3)
    p.add_argument("--memory-gib", type=fft.shared._memory_gib, default=2.)
    p.add_argument("--reference-trials", type=fft.shared._positive_int, default=2)
    p.add_argument("--reference-points", type=fft.shared._positive_int, default=2)
    p.add_argument("--seed", type=lambda x: fft.shared._integer(x, "seed", 0, fft.shared.MASK64), default=710191)
    p.add_argument("--split", choices=("development", "calibration", "holdout"), default="development")
    p.add_argument("--attempts-per-horizon", type=fft.shared._positive_int, default=1440)
    p.add_argument("--duration-alternatives", type=fft.shared._positive_int, default=1)
    p.add_argument("--model-alternatives", type=fft.shared._positive_int, default=1)
    p.add_argument("--horizon-seconds", type=fft.shared._finite_float, default=86400.)
    p.add_argument("--false-per-horizon", type=fft.shared._finite_float, default=1e-4)
    p.add_argument("--cpu-threads", type=fft.shared._positive_int, default=2)
    return p


def run(a):
    driver_start = time.perf_counter()
    if a.output.exists():
        raise ValueError("output already exists")
    if a.horizon_seconds <= 0 or not 0 < a.false_per_horizon < 1 or a.lag_max_seconds < 0:
        raise ValueError("invalid horizon/false-admission target or lag range")
    torch.set_num_threads(a.cpu_threads)
    fft.shared._configure_torch()
    device = fft.shared._resolve_device(a.device)
    if device.type == "cuda":
        torch.cuda.set_device(device)
    start = time.perf_counter()
    directory, manifest = fft.shared._load_manifest(a.manifest)
    data = fft.shared._validate_manifest(directory, manifest)
    if data["signal_bit"] is None or manifest.get("control") not in (None, "signal"):
        raise ValueError("positive sampled signal bank required")
    validate_linear_noise_model(manifest)
    load_seconds = time.perf_counter()-start
    work, workspace_timing = timed(lambda: fft.Workspace(data, manifest, a), device)
    # Workspace's memory guard covers its own FFT arrays. Also guard the full
    # cached GEMM bank and retained represented observations before allocation.
    direct_bytes = work.hypotheses*work.samples*8
    observation_bytes = 2*a.trials*work.samples*(16+8)
    score_bytes = 2*a.trials*work.hypotheses*8*4
    estimated = work.workspace_bytes+direct_bytes*3+observation_bytes+score_bytes
    cap = int(a.memory_gib*1024**3)
    if estimated > cap:
        raise MemoryError(f"paired bank/observation estimate {estimated} exceeds --memory-gib; no family was pruned")
    if device.type == "cuda" and estimated > .85*torch.cuda.mem_get_info(device)[0]:
        raise MemoryError("paired arrays exceed85% of free VRAM; no family was pruned")
    bank, bank_timing = timed(lambda: direct_bank(work), device)
    modes = {"direct-gemm-cached": lambda y: score_direct(y, bank, work),
             "fft-cached": lambda y: score_fft(y, work),
             "fft-template-recomputed": lambda y: score_fft(y, work, recompute_templates=True)}
    records = []
    for cn0 in a.cn0_grid:
        begin = time.perf_counter()
        variance = data["noise_reference_power"]*work.sample_rate/10**(cn0/10)
        if not math.isfinite(variance) or variance <= 0:
            raise ValueError("invalid noise variance")
        noise = []
        for trial in range(a.trials):
            seed = observation_seed(a.seed, a.split, trial)
            generator = torch.Generator(device="cpu").manual_seed(seed)
            parts = torch.randn((2, work.samples), dtype=torch.float64, generator=generator)/math.sqrt(2)
            noise.append(torch.complex(parts[0], parts[1]))
        null = torch.stack(noise)
        positive = null+data["signal"].to(device="cpu", dtype=torch.complex128)[None, :]/math.sqrt(variance)
        # These complex64 sampled observations are identical for every mode.
        represented = torch.cat((null, positive), dim=0).to(torch.complex64)
        generation_seconds = time.perf_counter()-begin
        observation, transfer = timed(lambda: represented.to(device), device)

        def sequence(mode):
            return torch.cat([modes[mode](observation[first:first+work.batch])
                              for first in range(0, len(observation), work.batch)])

        cold, warmups, timings, outputs = {}, {}, {m: [] for m in modes}, {}
        for mode in modes:
            _, cold[mode] = timed(lambda: sequence(mode), device)
            def warm_sequence():
                for _ in range(a.warmups):
                    sequence(mode)
            _, warmups[mode] = timed(warm_sequence, device)
        for repetition in range(a.repeats):
            order = list(modes) if repetition % 2 == 0 else list(reversed(modes))
            for mode in order:
                output, measured = timed(lambda: sequence(mode), device)
                timings[mode].append(measured)
                outputs[mode] = output
        collected, score_transfer = {}, {}
        for mode, y in outputs.items():
            collected[mode], score_transfer[mode] = timed(lambda: y.cpu(), device)
        begin = time.perf_counter()
        reference_count = min(a.reference_trials, a.trials)
        reference_indices = list(range(reference_count))+list(range(a.trials, a.trials+reference_count))
        ref = reference_scores(represented[reference_indices], work)
        fp64_seconds = time.perf_counter()-begin
        begin = time.perf_counter()
        baseline = collected["fft-cached"]
        comparisons, decisions, mode_verification_seconds = {}, {}, {}
        for mode, score in collected.items():
            mode_verify_start = time.perf_counter()
            same_subset = score[reference_indices]
            flat, exact = same_subset.flatten(1), ref.flatten(1)
            comparisons[mode] = {
                "full_family_maximum_absolute_score_difference_vs_cached_fft": float((score-baseline).abs().max()),
                "full_family_winner_differences_vs_cached_fft": int((score.flatten(1).argmax(1) != baseline.flatten(1).argmax(1)).sum()),
                "full_family_admission_differences_vs_cached_fft": int(((score.flatten(1).max(1).values >= work.threshold) != (baseline.flatten(1).max(1).values >= work.threshold)).sum()),
                "fp64_reference_capture_count": len(reference_indices),
                "fp64_reference_hypothesis_scores": ref.numel(),
                "maximum_absolute_score_error_vs_fp64": float((same_subset-ref).abs().max()),
                "winner_differences_vs_fp64": int((flat.argmax(1) != exact.argmax(1)).sum()),
                "admission_differences_vs_fp64": int(((flat.max(1).values >= work.threshold) != (exact.max(1).values >= work.threshold)).sum()),
                "minimum_absolute_fp64_distance_to_admission_threshold": float((exact.max(1).values-work.threshold).abs().min()),
                "minimum_fp64_winner_margin": float((exact.topk(2).values[:, 0]-exact.topk(2).values[:, 1]).min()),
                "maximum_absolute_winner_margin_error_vs_fp64": float(((flat.topk(2).values[:, 0]-flat.topk(2).values[:, 1])-(exact.topk(2).values[:, 0]-exact.topk(2).values[:, 1])).abs().max())}
            decisions[mode] = metrics(score, work, data["signal_bit"], a.trials)
            mode_verification_seconds[mode] = time.perf_counter()-mode_verify_start
        # Verify the reused existing scanner's selected maxima on the same rows.
        scan_best, scan_code, _ = work.scan(observation[:reference_count], [[] for _ in range(reference_count)])
        scanner_parity = {"captures": reference_count,
            "maximum_absolute_maximum_difference": float((scan_best-baseline[:reference_count].flatten(1).max(1).values).abs().max()),
            "winner_differences": int((scan_code != baseline[:reference_count].flatten(1).argmax(1)).sum())}
        for mode in comparisons:
            if comparisons[mode]["admission_differences_vs_fp64"] or comparisons[mode]["winner_differences_vs_fp64"]:
                comparisons[mode]["numerical_acceptance"] = "ranking/admission disagreement; investigate, not qualified"
            else:
                comparisons[mode]["numerical_acceptance"] = "bounded paired screen only; no sensitivity-loss qualification"
        verification_seconds = time.perf_counter()-begin
        mode_reports = {}
        for mode in modes:
            wall = summarize([row["wall_seconds"] for row in timings[mode]])
            event = [row["cuda_event_seconds"] for row in timings[mode] if row["cuda_event_seconds"] is not None]
            # All modes intentionally charge the same existing Workspace setup;
            # direct GEMM additionally charges its distinct cached design build.
            prep = workspace_timing["wall_seconds"]+(bank_timing["wall_seconds"] if mode.startswith("direct") else 0.)
            pipeline = (load_seconds+prep+generation_seconds+transfer["wall_seconds"]+wall["median"]+
                        score_transfer[mode]["wall_seconds"]+mode_verification_seconds[mode]+fp64_seconds)
            mode_reports[mode] = {"resident_full_family_sequence_wall_seconds": wall,
                "resident_full_family_sequence_cuda_event_seconds": summarize(event) if event else None,
                "first_call_after_preparation": cold[mode], "separate_warmup": warmups[mode],
                "resident_paired_observations_scored_per_second": 2*a.trials/wall["median"],
                "resident_hypotheses_per_second": 2*a.trials*work.hypotheses/wall["median"],
                "sampled_input_pipeline_seconds_including_reference_verification": pipeline,
                "sampled_input_pipeline_paired_observations_scored_per_second": 2*a.trials/pipeline,
                "throughput_counting_unit": "2*T scored observations from T independent noise draws; H0 and H1 share each draw and are not 2*T independent trials",
                "decision_and_comparison_verification_seconds": mode_verification_seconds[mode],
                "metrics": decisions[mode],
                "paired_numerical_comparison": comparisons[mode]}
        records.append({"cn0_db_hz": cn0, "noise_variance_before_unit_noise_scaling": variance,
            "observation_generation_rng_seconds": generation_seconds, "h2d_observation_transfer": transfer,
            "score_d2h_transfer_by_mode": score_transfer, "cpu_fp64_reference_seconds": fp64_seconds,
            "verification_seconds": verification_seconds, "existing_scanner_parity": scanner_parity,
            "modes": mode_reports})
        del observation, outputs, collected
    files = [directory/"manifest.json", directory/manifest.get("basis_file", "basis.c64"), directory/manifest.get("signal_file", "signal.c64")]
    source = [Path(__file__), FFT_PATH, FFT_PATH.with_name("triad-gpu-study.py")]
    record = {"schema": "triad-acquisition-compare-v1", "configuration": {k: str(v) if isinstance(v, Path) else v for k, v in vars(a).items()},
        "scope": "paired exhaustive fixed sampled-waveform acquisition; one unknown constant complex gain; conditional channel realization",
        "environment": {"python": platform.python_version(), "torch": torch.__version__, "cuda": torch.version.cuda,
            "device": str(device), "device_name": torch.cuda.get_device_name(device) if device.type == "cuda" else "CPU",
            "compute_capability": list(torch.cuda.get_device_capability(device)) if device.type == "cuda" else None},
        "precision": {"bulk": "strict complex64; TF32 disabled", "normalization": "FP64 template energies and score division", "reference": "CPU complex128 direct sampled sums; same represented templates and observations", "hardware_counters": "not collected; CUDA events are timings, not utilization counters"},
        "family": {"template_indices": work.indices, "template_parameters": work.params, "lags_samples": work.lags,
            "frequencies_hz": work.frequencies, "hypotheses_per_capture": work.hypotheses,
            "attempts_per_horizon": a.attempts_per_horizon, "duration_alternatives": a.duration_alternatives,
            "model_alternatives": a.model_alternatives, "total_horizon_hypotheses": work.family,
            "threshold": work.threshold, "horizon_seconds": a.horizon_seconds, "false_probability_per_horizon": a.false_per_horizon,
            "threshold_assumptions": "ideal known white circular complex Gaussian sampled noise; fixed full-support rank-one templates; exponential tail and finite-family union bound, independence not required; floating-point input/product rounding is not part of that ideal tail proof",
            "operational_qualification": False,
            "limits": "Only one acquisition attempt is sampled per trial; complete continuous timing/clock/channel alternatives and operational admission schedule are not exercised"},
        "physical_input": {"sample_rate": work.sample_rate, "samples": work.samples, "channel": manifest.get("channel"),
            "channel_realizations": 1, "truth_bit": data["signal_bit"], "observations_paired_across_modes_and_snr": True,
            "independent_noise_draws_per_snr": a.trials, "paired_h0_h1_observations_per_snr": 2*a.trials,
            "noise_draws_reused_across_snr": True,
            "seed_derivation": "first8 SHA256 bytes, little-endian, mask63bits, of compact ASCII JSON [triad-acquisition-compare-observation-v1, base_seed, split, trial_index]; development/calibration/holdout are distinct domains",
            "nonlinear_frontend": "unsupported: use actual sampled observations through nonlinear processing"},
        "preparation": {"load_and_validate_seconds": load_seconds, "existing_workspace_including_cached_fft_and_phasors": workspace_timing,
            "cached_gemm_bank_build_and_transfer": bank_timing, "gemm_bank_bytes": direct_bytes,
            "estimated_peak_array_bytes": estimated, "actual_batch": work.batch, "frequency_tile": work.frequency_tile, "nfft": work.nfft,
            "pipeline_accounting": "Each organization charges full existing Workspace initialization, even GEMM; GEMM additionally charges its bank build. Warmups/repetitions excluded from organization totals; driver wall and source waveform/export excluded. FP64 reference/score verification included. No acquisition-qualified cost claim."},
        "results": records, "source_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in source},
        "input_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
        "unimplemented_organizations": ["overlap-save streaming", "hybrid refinement", "search partition across GPUs"],
        "cost_per_operationally_qualified_result_usd": None}
    record["driver_wall_seconds_before_json_serialization"] = time.perf_counter()-driver_start
    record["driver_wall_scope"] = "all organizations, preparation, RNG, transfers, first calls, warmups, alternating repeated scoring, CPU references, verification and hashing; excludes Python process startup, source waveform/export and JSON serialization"
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(record, indent=2, allow_nan=False)+"\n")
    return record


if __name__ == "__main__":
    with torch.inference_mode():
        run(parser().parse_args())
