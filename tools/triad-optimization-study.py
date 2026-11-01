#!/usr/bin/env python3
"""Reproducible full-block phase/frequency receiver optimization benchmark.

This diagnostic replays an exported fixed candidate bank through the complete
sequential finite-state likelihood.  It reports stage timings for workspace
validation, correlated sufficient-statistic noise generation, host-to-device
transfer, construction, inference, and result verification.  It is not the
sampled-waveform transmitter/channel/receiver, blind acquisition, an operational
false-admission campaign, or a device qualification.

Example (small screening run):
  python3 tools/triad-optimization-study.py EXPORT --output /tmp/run.json \
    --device cpu --precision fp32 --batch 2 --max-blocks 8 --trials 8
"""
from __future__ import annotations

import argparse
import hashlib
import importlib
import json
import math
import os
import platform
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
import torch
from scipy.stats import beta

from triad_phase_frequency import PhaseFrequencyLikelihood, PhaseFrequencyModel
from triad_state_workspace import BlockWorkspace


SPLIT_SALTS = {"development": 0x444556, "calibration": 0x43414C, "holdout": 0x484F4C44}
PROTOCOL = {
    "operational_default": {"correct_probability": .99, "false_probability": 1e-4, "horizon_seconds": 86400.},
    "relaxed": {"correct_probability": .90, "false_probability": .01, "horizon_seconds": 43200.},
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _git_head() -> str | None:
    try:
        result = subprocess.run(["git", "rev-parse", "HEAD"], cwd=Path(__file__).resolve().parents[1],
                                capture_output=True, check=True, text=True)
        return result.stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def _git_working_tree_dirty() -> bool | None:
    try:
        result = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"],
                                cwd=Path(__file__).resolve().parents[1],
                                capture_output=True, check=True, text=True)
        return bool(result.stdout.strip())
    except (OSError, subprocess.CalledProcessError):
        return None


def interval(successes: int, total: int) -> list[float]:
    """One-sided pointwise 95% Clopper-Pearson lower and upper bounds."""
    if total < 1:
        return [0., 1.]
    lower = 0. if successes == 0 else float(beta.ppf(.05, successes, total-successes+1))
    upper = 1. if successes == total else float(beta.ppf(.95, successes+1, total-successes))
    return [lower, upper]


def _logmeanexp(values: np.ndarray, axis: int) -> np.ndarray:
    maximum = np.max(values, axis=axis, keepdims=True)
    result = maximum + np.log(np.exp(values - maximum).mean(axis=axis, keepdims=True))
    return np.squeeze(result, axis=axis)


def _sync(device: torch.device) -> None:
    if device.type == "cuda":
        torch.cuda.synchronize(device)


def _events(device: torch.device):
    if device.type != "cuda":
        return None, None
    return torch.cuda.Event(enable_timing=True), torch.cuda.Event(enable_timing=True)


def _time_transfer(blocks, device, complex_dtype):
    """Transfer all prepared blocks; report synchronized wall and CUDA events."""
    _sync(device)
    start_wall = time.perf_counter()
    start_event, end_event = _events(device)
    if start_event is not None:
        start_event.record(torch.cuda.current_stream(device))
    moved = []
    for r, gram, dt, index in blocks:
        rr = torch.as_tensor(r, dtype=complex_dtype, device=device)
        gg = torch.as_tensor(gram, dtype=complex_dtype, device=device)
        moved.append((rr, gg, dt, index))
    if end_event is not None:
        end_event.record(torch.cuda.current_stream(device))
        end_event.synchronize()
    wall = time.perf_counter() - start_wall
    event_seconds = None if start_event is None else start_event.elapsed_time(end_event) / 1000.
    return moved, {"synchronized_wall_seconds": wall, "cuda_event_seconds": event_seconds}


def _calendar_dt_values(workspace, block_count):
    """Exact public float64 center differences, including tail/roundoff values."""
    values, previous = set(), None
    for index in range(block_count):
        first = index*workspace.block_samples
        last = min(first+workspace.block_samples, workspace.sample_count)
        center = (first+last)/(2.*workspace.sample_rate)
        values.add(0. if previous is None else center-previous)
        previous = center
    return sorted(values)


def _memory_preflight(args, device, workspace, candidate_count, block_count, capture_count):
    """Bound retained CPU/GPU arrays before collecting or transferring any blocks."""
    payload_per_block = (args.batch*candidate_count*args.taps +
                         candidate_count*args.taps*args.taps)
    host_payload = capture_count*block_count*payload_per_block*16  # complex128 host statistics
    tensor_item = 8 if args.precision == "fp32" else 16  # complex64 / complex128 bytes
    device_payload = capture_count*block_count*payload_per_block*tensor_item
    phase_states = args.phase_states
    spacing = (2/phase_states)/args.nominal_dt
    half = math.ceil(args.frequency_bound_hz/spacing)
    frequency_states = 2*half+1
    if frequency_states > 257:
        raise ValueError("automatic frequency grid exceeds the phase receiver's 257-state bound")
    quadrature_states = 3*(args.relative_phase_states if args.taps == 2 else 1)
    dtype_bytes = 4 if args.precision == "fp32" else 8
    elements = args.batch*candidate_count*quadrature_states*frequency_states*phase_states
    cache_entries = max(1, min(32, len(args.public_dt_values)))
    receiver_bytes = elements*dtype_bytes*16 + cache_entries*(frequency_states**2*16+
        frequency_states*phase_states*24+phase_states*32)+65536
    graph_bytes = None
    if args.backend == "graph":
        graph_inputs = args.batch*candidate_count*(args.taps+args.taps*args.taps)*tensor_item
        graph_bytes = (cache_entries+1)*receiver_bytes+elements*dtype_bytes+graph_inputs
    planned_receiver_bytes = graph_bytes if graph_bytes is not None else receiver_bytes
    configured_cap = int(args.memory_gib*1024**3)
    if planned_receiver_bytes > configured_cap:
        raise ValueError(f"estimated {args.backend} phase receiver working set ({planned_receiver_bytes} bytes) exceeds --memory-gib cap; graph estimates include all cached public dt values")
    host_available = None
    try:
        host_available = os.sysconf("SC_AVPHYS_PAGES")*os.sysconf("SC_PAGE_SIZE")
    except (ValueError, OSError, AttributeError):
        pass
    host_total = host_payload+(device_payload+planned_receiver_bytes if device.type == "cpu" else 0)
    if host_available is not None and host_total > .85*host_available:
        raise MemoryError("estimated prepared inputs and phase state exceed 85% of currently available host memory")
    device_free = device_total = None
    if device.type == "cuda":
        device_free, device_total = torch.cuda.mem_get_info(device)
        if planned_receiver_bytes+device_payload > .85*device_free:
            raise MemoryError("estimated resident inputs and receiver state exceed 85% of currently free GPU memory; reduce --batch/--max-blocks or use streaming-only after extending this runner")
    return {"host_prepared_statistics_bytes": host_payload,
            "resident_tensor_statistics_bytes": device_payload,
            "estimated_host_total_including_cpu_tensors_and_state_bytes": host_total,
            "estimated_base_receiver_working_bytes": receiver_bytes,
            "estimated_receiver_working_bytes": planned_receiver_bytes,
            "estimated_graph_cache_working_bytes": graph_bytes,
            "public_distinct_dt_count": len(args.public_dt_values),
            "graph_cache_entries_at_peak": cache_entries if graph_bytes is not None else None,
            "host_available_bytes_at_preflight": host_available,
            "gpu_free_bytes_at_preflight": device_free,
            "gpu_total_bytes": device_total,
            "capacity_rule": "pre-allocation estimate; reserves 15% of currently available host/device memory; allocator/runtime and input memmaps add overhead"}


def _make_receiver(args, batch, bits, taps, device):
    if args.backend == "reference":
        receiver_type = PhaseFrequencyLikelihood
    else:
        try:
            module = importlib.import_module("triad_phase_optimized")
            receiver_type = (module.GraphPhaseFrequencyLikelihood if args.backend == "graph"
                             else module.OptimizedPhaseFrequencyLikelihood)
        except (ImportError, AttributeError) as exc:
            raise RuntimeError("optimized backend is not installed in this source revision") from exc
    model = PhaseFrequencyModel(
        phase_states=args.phase_states,
        frequency_bound_hz=args.frequency_bound_hz,
        nominal_dt_seconds=args.nominal_dt,
        frequency_std_hz=args.frequency_std_hz,
        frequency_correlation_seconds=args.frequency_correlation_seconds,
        phase_diffusion_rad_per_sqrt_second=args.phase_diffusion,
        relative_phase_states=args.relative_phase_states,
        transition_cache_entries=min(32, max(1, args.distinct_dts)),
    )
    dtype = torch.float32 if args.precision == "fp32" else torch.float64
    return receiver_type(batch, bits, taps, model, device=device, dtype=dtype,
                         memory_limit_bytes=args.memory_gib * 1024**3,
                         validate_inputs=False)


def _verified_method(receiver):
    diagnostic = receiver.diagnostics()
    if diagnostic.get("tf32_allowed") is True:
        raise RuntimeError("FP32 minimum violated: receiver reports TF32 allowed")
    return diagnostic


def _selected_candidates(workspace, taps):
    selected = [i for i, mapping in enumerate(workspace.column_mapping) if len(mapping) == taps]
    if not selected:
        raise ValueError(f"export contains no candidates with {taps} taps")
    labels = np.asarray([workspace.candidates[i]["bit"] for i in selected], dtype=np.int64)
    if set(labels.tolist()) != {0, 1}:
        raise ValueError("selected exhaustive bank must contain both source bits")
    mappings = [workspace.column_mapping[i] for i in selected]
    return selected, labels, mappings


def _collect(workspace, args, selected, mappings, batch, signal, actual=False):
    source = workspace.iter_observation(stop=args.stop_block) if actual else workspace.iter_simulator(
        seed=args.effective_seed, batch=batch, cn0_db_hz=args.cn0, signal=signal,
        stop=args.stop_block)
    output = []
    for block in source:
        full_r = block.r
        full_g = block.gram
        # Every candidate is kept; no pruning and no y-dependent rank choice.
        r = np.stack([full_r[:, mapping] for mapping in mappings], axis=1)
        gram = np.stack([full_g[np.ix_(mapping, mapping)] for mapping in mappings], axis=0)
        output.append((np.ascontiguousarray(r), np.ascontiguousarray(gram), block.dt_seconds, block.index))
    if not output:
        raise ValueError("selected block range is empty")
    return output


def _collect_prepared_pair(workspace, args, mappings):
    from triad_prepared_observation import PreparedGaussianObservation
    prepared = PreparedGaussianObservation(workspace, seed=args.effective_seed,
        batch=args.batch, cn0_db_hz=args.cn0, stop=args.stop_block,
        memory_limit_bytes=args.workspace_memory_mib*1024*1024)
    signal, null = [], []
    for pair in prepared.iter_paired():
        for output, block in ((signal, pair.signal), (null, pair.null)):
            r = np.stack([block.r[:, mapping] for mapping in mappings], axis=1)
            g = np.stack([block.gram[np.ix_(mapping, mapping)] for mapping in mappings])
            output.append((np.ascontiguousarray(r), np.ascontiguousarray(g),
                           block.dt_seconds, block.index))
    return signal, null, prepared.diagnostics()


def _run_updates(receiver, blocks, device, *, measure_events):
    """Measure complete sequential updates. No synchronization occurs per block."""
    _sync(device)
    wall_start = time.perf_counter()
    start_event, end_event = _events(device) if measure_events else (None, None)
    if start_event is not None:
        start_event.record(torch.cuda.current_stream(device))
    with torch.inference_mode():
        for r, gram, dt, _ in blocks:
            receiver.update(r, gram, dt)
    if end_event is not None:
        end_event.record(torch.cuda.current_stream(device))
        end_event.synchronize()
    else:
        _sync(device)
    wall = time.perf_counter() - wall_start
    event_seconds = None if start_event is None else start_event.elapsed_time(end_event) / 1000.
    return {"synchronized_wall_seconds": wall, "cuda_event_seconds": event_seconds,
            "blocks": len(blocks), "block_updates_per_second_wall": len(blocks)/wall if wall else None}


def _run_streamed_updates(receiver, blocks, device):
    """Original host-block-at-a-time H2D/update organization, no per-block sync."""
    _sync(device)
    wall_start = time.perf_counter()
    start_event, end_event = _events(device)
    if start_event is not None:
        start_event.record(torch.cuda.current_stream(device))
    with torch.inference_mode():
        for r, gram, dt, _ in blocks:
            rr = torch.as_tensor(r, dtype=receiver.complex_dtype, device=device)
            gg = torch.as_tensor(gram, dtype=receiver.complex_dtype, device=device)
            receiver.update(rr, gg, dt)
    if end_event is not None:
        end_event.record(torch.cuda.current_stream(device))
        end_event.synchronize()
    else:
        _sync(device)
    wall = time.perf_counter()-wall_start
    event_seconds = None if start_event is None else start_event.elapsed_time(end_event)/1000.
    return {"synchronized_wall_seconds": wall, "cuda_event_seconds": event_seconds,
            "blocks": len(blocks), "block_updates_per_second_wall": len(blocks)/wall if wall else None}


def _warmup(args, blocks, device, batch, bits, taps):
    receiver = _make_receiver(args, batch, bits, taps, device)
    # dt=0 bypasses prediction; warming only the first block leaves the real
    # stencil JIT (and an irregular final-block kernel) in measured inference.
    sample = list(blocks[:args.warmup_blocks])
    seen = {block[2] for block in sample}
    if args.warmup_blocks:
        for block in blocks:
            if block[2] not in seen:
                sample.append(block)
                seen.add(block[2])
    start = time.perf_counter()
    with torch.inference_mode():
        for r, gram, dt, _ in sample:
            receiver.update(r, gram, dt)
    _sync(device)
    return {"seconds": time.perf_counter()-start, "updates": len(sample),
            "excluded_from_measured_inference": True,
            "prepared_dt_values": sorted(seen),
            "note": "covers every public dt specialization; graph capture is per receiver instance"}


def _verify(scores, candidate_labels, truth, family_multiplier, *, calibration_supported=True):
    if not np.isfinite(scores).all():
        raise RuntimeError("receiver produced nonfinite scores")
    bit_scores = np.stack([_logmeanexp(scores[:, candidate_labels == b], axis=1)
                           for b in (0, 1)], axis=1)
    winner = np.argmax(bit_scores, axis=1)
    p = PROTOCOL
    reports = {}
    for name, target in p.items():
        attempts = max(1, math.ceil(target["horizon_seconds"] / 60.))
        threshold = math.log(attempts * family_multiplier["duration_alternatives"] *
                             family_multiplier["selection_methods"] * 2 / target["false_probability"])
        per_test_budget = target["false_probability"] / (attempts * family_multiplier["duration_alternatives"] *
                                                           family_multiplier["selection_methods"] * 2)
        admitted = np.max(bit_scores, axis=1) >= threshold
        correct = int(np.sum(admitted & (winner == truth))) if truth is not None else None
        wrong = int(np.sum(admitted & (winner != truth))) if truth is not None else None
        null_admitted = int(np.sum(admitted)) if truth is None else None
        reports[name] = {
            "correct_target": target["correct_probability"],
            "false_probability_target": target["false_probability"],
            "horizon_seconds": target["horizon_seconds"],
            "attempt_spacing_seconds": 60., "attempts_per_horizon": attempts,
            "duration_alternatives": family_multiplier["duration_alternatives"],
            "receiver_selection_methods": family_multiplier["selection_methods"],
            "bit_hypotheses": 2, "threshold_nats": threshold,
            "union_bound_per_test_false_probability": per_test_budget,
            "admission_calibration_supported": calibration_supported,
            "conditional_correct_accepted": correct if calibration_supported else None,
            "conditional_wrong_accepted": wrong if calibration_supported else None,
            "conditional_missed": len(scores)-correct-wrong if calibration_supported and truth is not None else None,
            "conditional_correct_one_sided_95": interval(correct, len(scores)) if calibration_supported and truth is not None else None,
            "null_admitted": null_admitted if calibration_supported else None,
            "null_admission_one_sided_95": interval(null_admitted, len(scores)) if calibration_supported and null_admitted is not None else None,
            "hypothetical_score_gate_crossings": int(np.sum(admitted)),
            "scope": "conditional trials on one exported fixed basis; pointwise interval only; not a calibrated operational tail or independent path estimate" if calibration_supported else "uncalibrated likelihood score diagnostics only; active sampled frontend violates the Gaussian projection/null model, so threshold crossings are hypothetical and are not admissions",
        }
    return bit_scores, reports


def _decision_differences(left, right, labels, family):
    result = {"candidate_winner_disagreements": int(np.sum(left.argmax(axis=1) != right.argmax(axis=1)))}
    left_bits = np.stack([_logmeanexp(left[:, labels == b], 1) for b in (0, 1)], 1)
    right_bits = np.stack([_logmeanexp(right[:, labels == b], 1) for b in (0, 1)], 1)
    result["bit_winner_disagreements"] = int(np.sum(left_bits.argmax(1) != right_bits.argmax(1)))
    for name, target in PROTOCOL.items():
        attempts = math.ceil(target["horizon_seconds"] / 60.)
        threshold = math.log(attempts*family["duration_alternatives"]*family["selection_methods"]*2/
                             target["false_probability"])
        ldec = left_bits.max(1) >= threshold
        rdec = right_bits.max(1) >= threshold
        result[name+"_admission_disagreements"] = int(np.sum(ldec != rdec))
        result[name+"_bit_decision_disagreements"] = int(np.sum(ldec & rdec &
            (left_bits.argmax(1) != right_bits.argmax(1))))
    return result


def _compare_frozen_report(report_path, current_args, current_inputs, current_signal,
                           current_null, labels, family, current_pipeline):
    report_path = report_path.resolve()
    prior = json.loads(report_path.read_text())
    current_config = vars(current_args)
    prior_config = prior["configuration"]
    common = ("split", "seed", "cn0", "batch", "trials", "max_blocks", "taps", "phase_states",
              "relative_phase_states", "frequency_bound_hz", "nominal_dt",
              "frequency_std_hz", "frequency_correlation_seconds", "phase_diffusion",
              "duration_alternatives", "selection_methods", "actual_observation")
    changed = [key for key in common if prior_config.get(key) != current_config.get(key)]
    if changed:
        raise ValueError("paired comparison has mismatched settings: " + ", ".join(changed))
    if prior.get("provenance", {}).get("input_manifest_sha256") != current_inputs["input_manifest_sha256"]:
        raise ValueError("paired comparison uses a different export manifest")
    if prior.get("provenance", {}).get("input_files") != current_inputs["input_files"]:
        raise ValueError("paired comparison uses different exported input bytes")
    if prior.get("workload", {}).get("scored_blocks") != current_inputs["scored_blocks"]:
        raise ValueError("paired comparison scores a different block range")
    if prior.get("workload", {}).get("candidate_labels") != current_inputs["candidate_labels"] or \
       prior.get("workload", {}).get("candidate_mappings") != current_inputs["candidate_mappings"]:
        raise ValueError("paired comparison scores a different candidate bank")
    prior_npz = report_path.with_suffix(".scores.npz")
    if not prior_npz.is_file():
        raise ValueError(f"paired score archive is absent: {prior_npz}")
    with np.load(prior_npz) as archive:
        if "signal" not in archive.files:
            raise ValueError("paired score archive must contain a signal array")
        comparisons = {}
        for name, current in (("signal", current_signal), ("null", current_null)):
            if current is None:
                continue
            if name not in archive.files:
                raise ValueError(f"paired score archive has no {name} array")
            reference = archive[name]
            if reference.shape != current.shape:
                raise ValueError(f"paired {name} score shapes differ")
            delta = current-reference
            row = {"reference_backend": prior["workload"]["receiver_backend"],
                   "reference_precision": prior["workload"]["precision"],
                   "maximum_absolute_log_lr_error": float(np.max(np.abs(delta))),
                   "mean_absolute_log_lr_error": float(np.mean(np.abs(delta))),
                   "rms_log_lr_error": float(np.sqrt(np.mean(delta*delta))),
                   "candidate_ranking_disagreements": int(np.sum(current.argmax(1) != reference.argmax(1)))}
            row["decision_disagreements"] = _decision_differences(reference, current, labels, family)
            comparisons[name] = row
    prior_work = prior["timing_seconds"]["candidate_scoring_work"]
    prior_environment = prior.get("environment", {})
    same_device_model = (prior_environment.get("device") == current_pipeline["device"] and
                         prior_environment.get("device_name") == current_pipeline["device_name"] and
                         prior_environment.get("gpu_properties") == current_pipeline["gpu_properties"])
    prior_resident = prior_work.get("resident_warm_pipeline_seconds")
    prior_streamed = prior_work.get("streamed_warm_pipeline_seconds")
    timing_protocol_match = prior["timing_seconds"].get("timing_protocol_version") == 2
    speedup = {"same_device_model_and_properties": same_device_model,
               "timing_protocol_match": timing_protocol_match,
               "specific_gpu_identity_verified": False,
               "resident_pipeline_speedup": prior_resident/current_pipeline["resident_warm_pipeline_seconds"]
                   if timing_protocol_match and prior_resident and current_pipeline["resident_warm_pipeline_seconds"] else None,
               "streamed_pipeline_speedup": prior_streamed/current_pipeline["streamed_warm_pipeline_seconds"]
                   if timing_protocol_match and prior_streamed and current_pipeline["streamed_warm_pipeline_seconds"] else None,
               "scope": "matched report timing ratio; device model/property match does not verify a specific GPU UUID or exclude contention"}
    return {"reference_report": str(report_path),
            "reference_source_sha256": prior.get("provenance", {}).get("source_sha256"),
            "matched_inputs_and_reception_model": True,
            "measured_pipeline_speedup": speedup,
            "results": comparisons,
            "warning": "paired numerical comparison on this selected seed/path is not sensitivity-loss or tail equivalence evidence"}


def run(args):
    output = args.output.resolve()
    if output.exists():
        raise ValueError("output path already exists")
    if args.backend != "reference":
        try:
            importlib.import_module("triad_phase_optimized")
        except ImportError as exc:
            raise RuntimeError("optimized backend requested but triad_phase_optimized is unavailable") from exc
    torch.set_num_threads(args.cpu_threads)
    torch.set_float32_matmul_precision("highest")
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    device = torch.device(args.device)
    if device.type not in ("cpu", "cuda") or (device.type == "cuda" and not torch.cuda.is_available()):
        raise ValueError("requested CPU/CUDA device is unavailable")
    if args.precision == "fp32" and device.type == "cuda":
        if torch.backends.cuda.matmul.allow_tf32 or torch.get_float32_matmul_precision() != "highest":
            raise RuntimeError("strict FP32 requires TF32 disabled and highest matmul precision")
    if args.batch < 1 or args.trials != args.batch or args.warmup_blocks < 0 or args.cpu_threads < 1 or args.max_blocks < 0:
        raise ValueError("trial/warmup/thread/block limits are invalid")
    if (not 2 <= args.phase_states <= 4096 or args.batch < 1 or
        not 1 <= args.relative_phase_states <= 256 or args.taps not in (1, 2)):
        raise ValueError("phase/batch/tap geometry is invalid")
    for v in (args.cn0, args.frequency_bound_hz, args.frequency_std_hz,
              args.frequency_correlation_seconds, args.phase_diffusion, args.memory_gib):
        if not math.isfinite(v):
            raise ValueError("model and memory parameters must be finite")
    if (args.nominal_dt is not None and args.nominal_dt <= 0) or args.memory_gib <= 0 or args.duration_alternatives < 1 or args.selection_methods < 1 or args.frequency_bound_hz < 0 or args.frequency_std_hz < 0 or args.frequency_correlation_seconds <= 0 or args.phase_diffusion < 0:
        raise ValueError("duration, method count, dt, and memory must be positive")

    wall_start = time.perf_counter()
    input_dir = args.input.resolve()
    if args.output_scores and output.with_suffix(".scores.npz").exists():
        raise ValueError("score output path already exists")
    start = time.perf_counter()
    workspace = BlockWorkspace(input_dir, max_tile_blocks=args.tile_blocks,
                               memory_limit_bytes=args.workspace_memory_mib*1024*1024)
    selected, labels, mappings = _selected_candidates(workspace, args.taps)
    gaussian_projection_supported = bool(getattr(workspace, "gaussian_projection_supported", True))
    if not gaussian_projection_supported and not args.actual_observation:
        raise ValueError("this export marks its sampled frontend as outside the Gaussian projection model; use --actual-observation or a separately sampled null observation")
    if args.nominal_dt is None:
        args.nominal_dt = workspace.block_samples/workspace.sample_rate
    total_blocks = workspace.block_count
    run_blocks = total_blocks if args.max_blocks == 0 else min(total_blocks, args.max_blocks)
    args.stop_block = run_blocks
    truncated = run_blocks < total_blocks
    if args.actual_observation:
        actual_cn0 = 10*math.log10(workspace.sample_rate/workspace.noise_variance)
        if not math.isclose(actual_cn0, args.cn0, abs_tol=1e-9):
            raise ValueError("actual noisy projection requires its manifest C/N0; it cannot be rescaled")
        args.batch = args.trials = 1
    capture_count = 1 if (not gaussian_projection_supported and args.actual_observation) else 2
    args.public_dt_values = _calendar_dt_values(workspace, run_blocks)
    memory_estimate = _memory_preflight(args, device, workspace, len(selected), run_blocks, capture_count)
    args.simulation_noise_variance = workspace.noise_variance if args.actual_observation else \
        workspace.sample_rate / (10.0 ** (args.cn0/10.0))
    args.effective_seed = int(np.random.SeedSequence([args.seed, SPLIT_SALTS[args.split]]).generate_state(1, dtype=np.uint64)[0])
    workspace_setup_seconds = time.perf_counter()-start

    validation_start = time.perf_counter()
    # Independent tiled validation time is not mixed with the noise-generation
    # timer. The iterator below still performs its own validity checks.
    checked = sum(len(tile.intervals) for tile in workspace.iter_public(stop=run_blocks,
                                                                         tile_blocks=args.tile_blocks))
    validation_seconds = time.perf_counter()-validation_start
    if checked != run_blocks:
        raise RuntimeError("workspace validation returned an incomplete block range")

    shared_observation_preparation_seconds = 0.
    prepared_observation_diagnostics = None
    if args.observation_preparation == "cached":
        if args.actual_observation:
            raise ValueError("cached Gaussian preparation is only for simulated paired observations")
        prep_time = time.perf_counter()
        signal_host, null_host, prepared_observation_diagnostics = _collect_prepared_pair(workspace, args, mappings)
        shared_observation_preparation_seconds = time.perf_counter()-prep_time
        signal_generation_seconds = null_generation_seconds = 0.
    else:
        prep_time = time.perf_counter()
        signal_host = _collect(workspace, args, selected, mappings, args.batch, signal=True,
                               actual=args.actual_observation)
        signal_generation_seconds = time.perf_counter()-prep_time
        null_host = None
        null_generation_seconds = None
        if gaussian_projection_supported or not args.actual_observation:
            prep_time = time.perf_counter()
            null_host = _collect(workspace, args, selected, mappings, args.batch, signal=False)
            null_generation_seconds = time.perf_counter()-prep_time
    args.distinct_dts = max(1, min(32, len({float(block[2]) for block in signal_host + (null_host or [])})))
    if sorted({float(block[2]) for block in signal_host}) != args.public_dt_values:
        raise RuntimeError("collected block-center calendar differs from memory preflight")

    complex_dtype = torch.complex64 if args.precision == "fp32" else torch.complex128
    signal_device, signal_transfer = _time_transfer(signal_host, device, complex_dtype)
    null_device, null_transfer = (None, None) if null_host is None else _time_transfer(null_host, device, complex_dtype)
    bits, batch, taps = len(selected), args.batch, args.taps

    cold_capture = None
    if batch == 1:
        cold_state_start = time.perf_counter()
        cold_receiver = _make_receiver(args, batch, bits, taps, device)
        _sync(device)
        cold_construction_seconds = time.perf_counter()-cold_state_start
        cold_inference = _run_updates(cold_receiver, signal_device, device, measure_events=True)
        cold_d2h_start = time.perf_counter()
        cold_scores = cold_receiver.scores().detach().cpu().numpy()
        cold_d2h_seconds = time.perf_counter()-cold_d2h_start
        if not np.isfinite(cold_scores).all():
            raise RuntimeError("cold receiver produced nonfinite scores")
        cold_capture = {"receiver_construction_seconds": cold_construction_seconds,
                        "first_use_full_capture_inference": cold_inference,
                        "score_d2h_seconds": cold_d2h_seconds,
                        "end_to_end_after_pretransferred_inputs_seconds": cold_construction_seconds+
                            cold_inference["synchronized_wall_seconds"]+cold_d2h_seconds,
                        "scope": "first full capture through an unprimed receiver state and first backend use; includes transition construction and lazy runtime compilation if the backend performs it; excludes export, validation, input generation, and H2D transfer"}
        del cold_receiver
        _sync(device)

    prep_start = time.perf_counter()
    warmup = _warmup(args, signal_device, device, batch, bits, taps)
    warmup_seconds = time.perf_counter()-prep_start
    # Record high-water memory after allocation/workload setup and before
    # measured inference; runtime/allocator memory remains outside estimates.
    peak_memory = torch.cuda.max_memory_allocated(device) if device.type == "cuda" else None

    setup_times = {}
    transition_times = {}
    resident_scores, resident_timing, resident_score_transfer, model_diagnostics = {}, {}, {}, None
    receiver_inputs = [("signal", signal_device)] + ([] if null_device is None else [("null", null_device)])
    for label, blocks in receiver_inputs:
        st = time.perf_counter()
        receiver = _make_receiver(args, batch, bits, taps, device)
        setup_times[label] = time.perf_counter()-st
        st = time.perf_counter()
        with torch.inference_mode():
            for dt in sorted({block[2] for block in blocks if block[2] > 0}):
                receiver._transition(dt)
            if args.backend == "graph":
                seen = set()
                for r, gram, dt, _ in blocks:
                    if dt not in seen:
                        receiver.update(r, gram, dt)
                        seen.add(dt)
                receiver.reset()
        _sync(device)
        transition_times[label] = time.perf_counter()-st
        # Warm the framework/kernel path on a disposable state; measured state
        # starts with a prepared transition cache but cold posterior weights.
        resident_timing[label] = _run_updates(receiver, blocks, device, measure_events=True)
        if model_diagnostics is None:
            model_diagnostics = _verified_method(receiver)
            state_working_bytes = receiver.working_bytes_bound
        d2h_start = time.perf_counter()
        resident_scores[label] = receiver.scores().detach().cpu().numpy()
        resident_score_transfer[label] = time.perf_counter()-d2h_start
        del receiver
        _sync(device)

    streamed_timing, streamed_scores, streamed_score_transfer = {}, {}, {}
    stream_inputs = [("signal", signal_host)] + ([] if null_host is None else [("null", null_host)])
    for label, blocks in stream_inputs:
        st = time.perf_counter()
        receiver = _make_receiver(args, batch, bits, taps, device)
        setup_times[label+"_streamed"] = time.perf_counter()-st
        st = time.perf_counter()
        for dt in sorted({block[2] for block in blocks if block[2] > 0}):
            receiver._transition(dt)
        if args.backend == "graph":
            seen = set()
            for r, gram, dt, _ in blocks:
                if dt not in seen:
                    receiver.update(torch.as_tensor(r, dtype=complex_dtype, device=device),
                                    torch.as_tensor(gram, dtype=complex_dtype, device=device), dt)
                    seen.add(dt)
            receiver.reset()
        _sync(device)
        transition_times[label+"_streamed"] = time.perf_counter()-st
        streamed_timing[label] = _run_streamed_updates(receiver, blocks, device)
        d2h_start = time.perf_counter()
        streamed_scores[label] = receiver.scores().detach().cpu().numpy()
        streamed_score_transfer[label] = time.perf_counter()-d2h_start
        del receiver
        _sync(device)

    verification_start = time.perf_counter()
    s_scores = resident_scores["signal"]
    n_scores = resident_scores.get("null")
    streamed_signal_scores = streamed_scores["signal"]
    streamed_null_scores = streamed_scores.get("null")
    truth = int(workspace.manifest["signal_bit"])
    family = {"duration_alternatives": args.duration_alternatives,
              "selection_methods": args.selection_methods}
    signal_verify_start = time.perf_counter()
    _, signal_protocol = _verify(s_scores, labels, truth, family,
                                 calibration_supported=gaussian_projection_supported)
    signal_decision_verification_seconds = time.perf_counter()-signal_verify_start
    if n_scores is None:
        null_protocol = {"status": "unavailable", "reason": "no sampled noise-only observation was provided; do not infer null calibration from the signal observation"}
    else:
        null_verify_start = time.perf_counter()
        _, null_protocol = _verify(n_scores, labels, None, family,
                                   calibration_supported=gaussian_projection_supported)
        null_decision_verification_seconds = time.perf_counter()-null_verify_start
    if n_scores is None:
        null_decision_verification_seconds = 0.
    # Paired input construction must differ only by the noiseless mean.
    paired_error = max(float(np.max(np.abs((sh[0]-nh[0]) -
                          (np.stack([workspace._map("mean.c128", workspace._MEAN_DTYPE,
                             (workspace.block_count, workspace.dimension))[sh[3], mapping] for mapping in mappings], axis=0)
                           / args.simulation_noise_variance)[None])))
                       for sh, nh in zip(signal_host, null_host or [])) if not args.actual_observation and null_host is not None else None
    if cold_capture is not None:
        cold_verify_start = time.perf_counter()
        _verify(cold_scores, labels, truth, family, calibration_supported=gaussian_projection_supported)
        cold_verify_seconds = time.perf_counter()-cold_verify_start
        cold_capture["decision_verification_seconds"] = cold_verify_seconds
        cold_capture["end_to_end_full_first_capture_seconds"] = (
            workspace_setup_seconds+validation_seconds+shared_observation_preparation_seconds+signal_generation_seconds+
            signal_transfer["synchronized_wall_seconds"]+
            cold_capture["end_to_end_after_pretransferred_inputs_seconds"]+
            cold_verify_seconds)
    score_transfer_seconds = sum(resident_score_transfer.values())+sum(streamed_score_transfer.values())
    organization_error = {"signal_max_abs_log_lr": float(np.max(np.abs(s_scores-streamed_signal_scores)))}
    if n_scores is not None:
        organization_error["null_max_abs_log_lr"] = float(np.max(np.abs(n_scores-streamed_null_scores)))
    organization_error["signal_decisions"] = _decision_differences(s_scores, streamed_signal_scores, labels, family)
    if n_scores is not None:
        organization_error["null_decisions"] = _decision_differences(n_scores, streamed_null_scores, labels, family)
    verification_diagnostics_seconds = time.perf_counter()-verification_start
    decision_verification_seconds = signal_decision_verification_seconds+null_decision_verification_seconds
    verification_seconds = verification_diagnostics_seconds+score_transfer_seconds
    resident_verification_seconds = decision_verification_seconds+sum(resident_score_transfer.values())
    streamed_verification_seconds = decision_verification_seconds+sum(streamed_score_transfer.values())
    # Source hashes bind the benchmark logic. All manifest-listed input bytes
    # bind the exported bank, including optional actual observation content.
    provenance_start = time.perf_counter()
    source_paths = [Path(__file__).resolve(), Path(__file__).with_name("triad_phase_frequency.py"),
                    Path(__file__).with_name("triad_state_workspace.py"),
                    Path(__file__).with_name("triad-long.cpp")]
    if args.backend != "reference":
        source_paths.append(Path(__file__).with_name("triad_phase_optimized.py"))
    if args.observation_preparation == "cached":
        source_paths.append(Path(__file__).with_name("triad_prepared_observation.py"))
    input_files = [input_dir / "manifest.json"] + [input_dir / name for name in sorted(workspace.manifest["file_sizes"])]
    sources = {str(p): sha256(p) for p in source_paths}
    inputs = {p.name: {"sha256": sha256(p), "bytes": p.stat().st_size} for p in input_files}
    input_identity = {"input_manifest_sha256": sha256(workspace.manifest_path), "input_files": inputs,
                      "scored_blocks": run_blocks, "candidate_labels": labels.tolist(),
                      "candidate_mappings": [list(x) for x in mappings]}
    provenance_hashing_seconds = time.perf_counter()-provenance_start
    score_archive_start = time.perf_counter()
    if args.output_scores:
        scores_path = output.with_suffix(".scores.npz")
        if scores_path.exists():
            raise ValueError("score output path already exists")
        score_arrays = {"signal": s_scores}
        if n_scores is not None:
            score_arrays["null"] = n_scores
        np.savez_compressed(scores_path, **score_arrays)
    else:
        scores_path = None
    score_serialization_seconds = time.perf_counter()-score_archive_start
    resident_seconds = (workspace_setup_seconds + validation_seconds + shared_observation_preparation_seconds + signal_generation_seconds +
                        (null_generation_seconds or 0.) + signal_transfer["synchronized_wall_seconds"] +
                        (null_transfer["synchronized_wall_seconds"] if null_transfer else 0.) + setup_times["signal"] +
                        (setup_times.get("null", 0.)) + transition_times["signal"] + transition_times.get("null", 0.) +
                        resident_timing["signal"]["synchronized_wall_seconds"] +
                        resident_timing.get("null", {}).get("synchronized_wall_seconds", 0.) + resident_verification_seconds)
    streamed_seconds = (workspace_setup_seconds + validation_seconds + shared_observation_preparation_seconds + signal_generation_seconds +
                        (null_generation_seconds or 0.) + setup_times["signal_streamed"] +
                        setup_times.get("null_streamed", 0.) + transition_times["signal_streamed"] +
                        transition_times.get("null_streamed", 0.) + streamed_timing["signal"]["synchronized_wall_seconds"] +
                        streamed_timing.get("null", {}).get("synchronized_wall_seconds", 0.) + streamed_verification_seconds)
    pass_count = 1 if n_scores is None else 2
    candidate_updates = bits*run_blocks*batch*pass_count
    signal_pipeline_seconds = (workspace_setup_seconds + validation_seconds + shared_observation_preparation_seconds + signal_generation_seconds +
        signal_transfer["synchronized_wall_seconds"] + setup_times["signal"] + transition_times["signal"] +
        resident_timing["signal"]["synchronized_wall_seconds"] + signal_decision_verification_seconds + resident_score_transfer["signal"])
    null_pipeline_seconds = None if n_scores is None else (workspace_setup_seconds + validation_seconds + shared_observation_preparation_seconds + null_generation_seconds +
        null_transfer["synchronized_wall_seconds"] + setup_times["null"] + transition_times["null"] +
        resident_timing["null"]["synchronized_wall_seconds"] + null_decision_verification_seconds + resident_score_transfer["null"])
    signal_streamed_pipeline_seconds = (workspace_setup_seconds + validation_seconds + shared_observation_preparation_seconds + signal_generation_seconds +
        setup_times["signal_streamed"] + transition_times["signal_streamed"] +
        streamed_timing["signal"]["synchronized_wall_seconds"] + signal_decision_verification_seconds + streamed_score_transfer["signal"])
    null_streamed_pipeline_seconds = None if n_scores is None else (workspace_setup_seconds + validation_seconds + shared_observation_preparation_seconds + null_generation_seconds +
        setup_times["null_streamed"] + transition_times["null_streamed"] +
        streamed_timing["null"]["synchronized_wall_seconds"] + null_decision_verification_seconds + streamed_score_transfer["null"])
    single_capture_latency = None
    if batch == 1:
        single_capture_latency = {
            "pretransferred_resident_seconds": signal_pipeline_seconds,
            "streamed_host_block_seconds": signal_streamed_pipeline_seconds,
            "first_use_cold_capture": cold_capture,
            "scope": "one signal sufficient-statistic observation; includes validation, observation simulation/RNG when applicable, receiver setup, state updates and score verification; excludes source waveform/export, physical sampled-IQ acquisition, and calibration"}
    paired_numeric = None
    if args.compare_report is not None:
        compare_start = time.perf_counter()
        paired_numeric = _compare_frozen_report(args.compare_report, args, input_identity,
            s_scores, n_scores, labels, family,
            {"device": str(device), "device_name": torch.cuda.get_device_name(device) if device.type == "cuda" else "CPU",
             "gpu_properties": ({"compute_capability": list(torch.cuda.get_device_capability(device)),
                 "multiprocessor_count": torch.cuda.get_device_properties(device).multi_processor_count,
                 "total_memory_bytes": torch.cuda.get_device_properties(device).total_memory} if device.type == "cuda" else None),
             "resident_warm_pipeline_seconds": resident_seconds,
             "streamed_warm_pipeline_seconds": streamed_seconds})
        paired_numeric["verification_seconds"] = time.perf_counter()-compare_start
    complete_job_wall = time.perf_counter()-wall_start
    measured_receiver_inference = resident_timing["signal"]["synchronized_wall_seconds"] + \
        resident_timing.get("null", {}).get("synchronized_wall_seconds", 0.)
    report = {
        "schema": "triad-gpu-optimization-report-v1",
        "status": ("uncalibrated_frontend_score_diagnostic" if not gaussian_projection_supported else
                   "screening_only" if args.split != "holdout" or truncated else "frozen_reference_holdout_screening"),
        "configuration": {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()
                           if k not in ("effective_seed", "stop_block")},
        "provenance": {"input_export": str(input_dir), "input_manifest_sha256": sha256(workspace.manifest_path),
                       "input_files": inputs, "source_sha256": sources,
                       "triad_long_cpp_current_checkout_sha256": sha256(Path(__file__).with_name("triad-long.cpp")),
                       "triad_long_cpp_is_original_export_binding": False,
                       "exporter_binary_sha256": None,
                       "exporter_binding_limit": "export manifest does not record source or binary hashes; supplied exporter source hash is not proof that it generated this export",
                       "git_head": _git_head(), "tracked_working_tree_dirty": _git_working_tree_dirty(),
                       "command_argv": sys.argv,
                       "split": args.split, "seed_root": args.seed, "effective_noise_seed": args.effective_seed,
                       "seed_policy": "SeedSequence([root_seed, fixed_split_salt]); workspace derives each block's independent RNG from [effective_seed, block_index]",
                       "paired_noise": ("signal and null sufficient-statistic observations reuse the same seed, per block; independent trials within batch are distinct RNG outputs"
                           if n_scores is not None and not args.actual_observation else "actual observation has no paired simulated noise counterpart")},
        "environment": {"python": platform.python_version(), "torch": torch.__version__, "numpy": np.__version__,
                        "scipy": importlib.import_module("scipy").__version__, "device": str(device),
                        "device_name": torch.cuda.get_device_name(device) if device.type == "cuda" else "CPU",
                        "host_cpu_count": os.cpu_count(), "host_architecture": platform.machine(),
                        "cpu_threads": args.cpu_threads, "tf32_allowed": bool(torch.backends.cuda.matmul.allow_tf32),
                        "float32_matmul_precision": torch.get_float32_matmul_precision(),
                        "cuda_version": torch.version.cuda, "cudnn_version": torch.backends.cudnn.version() if torch.cuda.is_available() else None,
                        "gpu_properties": ({"compute_capability": list(torch.cuda.get_device_capability(device)),
                            "multiprocessor_count": torch.cuda.get_device_properties(device).multi_processor_count,
                            "total_memory_bytes": torch.cuda.get_device_properties(device).total_memory}
                            if device.type == "cuda" else None)},
        "workload": {"family": "finite phase/frequency fixed candidate bank; all exported candidates of selected tap count exhaustively scored",
                     "receiver_backend": args.backend, "precision": args.precision, "precision_contract": "strict FP32 or FP64 inputs/accumulation; TF32 disabled; log evidence FP64",
                     "candidate_count_scored": bits, "candidate_count_exported": len(workspace.candidates),
                     "candidate_labels": labels.tolist(), "candidate_mappings": [list(x) for x in mappings],
                     "batch_independent_trials": batch, "taps": taps,
                     "phase_states": args.phase_states, "frequency_states": model_diagnostics["frequency_states"],
                     "gain_quadrature_states": model_diagnostics["quadrature_states"],
                     "public_distinct_dt_values": args.public_dt_values,
                     "graph_cache_covers_public_dt_values": args.backend != "graph" or len(args.public_dt_values) <= 32,
                     "full_blocks": total_blocks, "scored_blocks": run_blocks,
                     "block_updates_per_capture": run_blocks,
                     "source_sample_count": workspace.sample_count, "sample_rate_hz": workspace.sample_rate,
                     "physical_observation_seconds": workspace.sample_count/workspace.sample_rate,
                     "truncated": truncated, "actual_noisy_projection": args.actual_observation,
                     "gaussian_projection_supported": gaussian_projection_supported,
                     "search_scope": "fixed exported alignment/frequency/epoch candidate set; no acquisition search is performed"},
        "threshold_family": {"attempt_spacing_seconds": 60., "duration_alternatives": args.duration_alternatives,
                             "receiver_selection_methods": args.selection_methods,
                             "bit_hypotheses": 2,
                             "calibration_status": "conditional model supported" if gaussian_projection_supported else "unsupported for active sampled frontend; gate crossings below are hypothetical diagnostics",
                             "note": "per-test union allocation for the declared default/relaxed horizons; threshold audit only, not empirical 24h/12h calibration"},
        "timing_seconds": {"timing_protocol_version": 2,
                           "workspace_setup_schema_and_file_validation": workspace_setup_seconds,
                           "public_gram_interval_psd_validation": validation_seconds,
                           "shared_paired_observation_preparation_seconds": shared_observation_preparation_seconds,
                           "prepared_observation_diagnostics": prepared_observation_diagnostics,
                           "shared_preparation_accounting": "counted once in the paired total and fully charged to either standalone capture latency; no amortization across calls",
                           "signal_statistics_noise_rng_generation": signal_generation_seconds if not args.actual_observation else None,
                           "signal_actual_observation_iteration": signal_generation_seconds if args.actual_observation else None,
                           "null_statistics_noise_rng_generation": null_generation_seconds,
                           "signal_host_to_device": signal_transfer,
                           "null_host_to_device": null_transfer,
                           "signal_receiver_construction": setup_times["signal"],
                           "null_receiver_construction": setup_times.get("null"),
                           "signal_streamed_receiver_construction_and_transition_cache": setup_times["signal_streamed"],
                           "null_streamed_receiver_construction_and_transition_cache": setup_times.get("null_streamed"),
                           "signal_transition_cache_preparation": transition_times["signal"],
                           "null_transition_cache_preparation": transition_times.get("null"),
                           "signal_streamed_transition_cache_preparation": transition_times["signal_streamed"],
                           "null_streamed_transition_cache_preparation": transition_times.get("null_streamed"),
                           "warmup_separate_excluded": warmup,
                           "signal_cold_first_use_capture": cold_capture,
                           "signal_pretransferred_resident_inference": resident_timing["signal"],
                           "null_pretransferred_resident_inference": resident_timing.get("null"),
                           "signal_streamed_host_transfer_plus_inference": streamed_timing["signal"],
                           "null_streamed_host_transfer_plus_inference": streamed_timing.get("null"),
                           "verification_d2h_and_decision_metrics": verification_seconds,
                           "score_d2h_seconds_by_organization": {"resident": resident_score_transfer,
                                                                 "streamed": streamed_score_transfer},
                           "decision_verification_cpu_seconds": decision_verification_seconds,
                           "verification_cross_checks_and_cold_decision_cpu_seconds": verification_diagnostics_seconds-decision_verification_seconds,
                           "source_and_input_hashing_seconds": provenance_hashing_seconds,
                           "score_archive_serialization_seconds": score_serialization_seconds,
                           "signal_decision_verification_cpu_seconds": signal_decision_verification_seconds,
                           "null_decision_verification_cpu_seconds": null_decision_verification_seconds if n_scores is not None else None,
                           "acquisition_seconds": None,
                           "acquisition_status": "not measured: input is already a fixed exported candidate bank; no timing/frequency/epoch search execution in this runner",
                           "complete_job_wall_seconds": complete_job_wall,
                           "candidate_scoring_work": {"candidate_block_score_evaluations": candidate_updates,
                               "observation_sets_scored": pass_count,
                               "signal_candidate_block_scores_per_second": bits*run_blocks*batch/signal_pipeline_seconds if signal_pipeline_seconds else None,
                               "null_candidate_block_scores_per_second": bits*run_blocks*batch/null_pipeline_seconds if null_pipeline_seconds else None,
                               "resident_warm_pipeline_seconds": resident_seconds,
                               "resident_candidate_block_updates_per_second": candidate_updates/resident_seconds if resident_seconds else None,
                               "resident_signal_capture_trials_per_second": batch/signal_pipeline_seconds if signal_pipeline_seconds else None,
                               "resident_null_capture_trials_per_second": batch/null_pipeline_seconds if null_pipeline_seconds else None,
                               "streamed_warm_pipeline_seconds": streamed_seconds,
                               "streamed_candidate_block_updates_per_second": candidate_updates/streamed_seconds if streamed_seconds else None,
                               "streamed_signal_capture_trials_per_second": batch/signal_streamed_pipeline_seconds if signal_streamed_pipeline_seconds else None,
                               "streamed_null_capture_trials_per_second": batch/null_streamed_pipeline_seconds if null_streamed_pipeline_seconds else None,
                               "single_capture_latency": single_capture_latency,
                               "resident_receiver_inference_fraction_of_pipeline": measured_receiver_inference/resident_seconds if resident_seconds else None,
                               "resident_pipeline_speedup_ceiling_if_inference_cost_were_zero":
                                   resident_seconds/(resident_seconds - measured_receiver_inference)
                                   if resident_seconds > measured_receiver_inference else None,
                               "qualification": "fixed-family screening throughput only; calibrated acquisition attempts/sec remains unmeasured"},
                           "cost_per_target_qualified_result_usd": None,
                           "cost_status": "not measured: no cloud rental or amortized export, calibration, holdout, transfer, and cleanup invoice is attached; this screening run is not a target-qualified result",
                           "benchmark_driver_complete_job_wall_scope": "runs both streamed and pretransferred organizations, all available signal/null passes, separate warmup/cold check and output verification/hash/archive work; organization-specific warm pipeline totals exclude disposable warmup, cold checks, provenance I/O and cross-organization diagnostics",
                           "complete_job_definition": "existing export loading/validation, available paired signal/null RNG+statistics generation or actual observation iteration, transfers, setup, separate warmup, optional single-capture cold check, both organizations' sequential inference passes, D2H and decision/cross-check verification, source/input hashing, score NPZ serialization and optional frozen-report comparison; excludes process launch, waveform export, final JSON serialization, independent holdout calibration, and physical sampled-signal acquisition",
                           "profiler_counters": {"status": "not_collected_by_this_runner", "note": "use Nsight Systems/Compute separately on the frozen workload; no utilization inference from memory allocation"}},
        "memory": {"reference_state_working_bytes_bound_each": state_working_bytes,
                   "preflight_estimates": memory_estimate,
                   "cuda_peak_allocated_bytes_after_setup_warmup": peak_memory,
                   "workspace_peak_observed_bytes": workspace.memory_bytes,
                   "scope": "reported tensors omit allocator/runtime state; host workspace high-water is the workspace's tracked arrays only"},
        "results": {"signal": signal_protocol, "null": null_protocol,
                    "paired_sufficient_mean_difference_max_abs": paired_error,
                    "streamed_vs_resident_max_abs_log_lr": organization_error,
                    "paired_numerical_reference": paired_numeric,
                    "signal_log_lr_shape": list(s_scores.shape), "null_log_lr_shape": list(n_scores.shape) if n_scores is not None else None,
                    "scores_file": str(scores_path) if scores_path else None},
        "limitations": ["sufficient-statistic Gaussian receiver only; quantization, clipping, ADC/DAC nonlinearity and interference require full sampled IQ through physical processing",
                        "active sampled frontends marked gaussian_projection_supported=false can be scored only as uncalibrated diagnostics until a matched sampled null model is supplied",
                        "statistics simulation and its RNG are jointly timed; the workspace iterator does not expose separate generation-phase counters",
                        "not full blind acquisition, ordinary Robust receiver integration, or sampled waveform discrimination",
                        "conditional noise trials on a fixed exported physical path are not independent channel paths",
                        "small screening counts cannot establish 99%/90% reception or 1e-4/day or 1%/12h operational tails",
                        "sensitivity loss in dB is not estimated by this runner",
                        "no Nsight utilization counters, cost data, or hardware-rental measurements are produced"],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, allow_nan=False)+"\n")
    print(json.dumps({"output": str(output), "status": report["status"], "blocks": run_blocks,
                      "signal_correct": report["results"]["signal"]["operational_default"]["conditional_correct_accepted"],
                      "null_admitted": report["results"]["null"].get("operational_default", {}).get("null_admitted"),
                      "complete_job_wall_seconds": complete_job_wall}))
    return report


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="triad-block-stats-v1 export directory")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--output-scores", action="store_true")
    parser.add_argument("--compare-report", type=Path,
                        help="paired prior run report; requires its .scores.npz and exactly matched model/input settings")
    parser.add_argument("--observation-preparation", choices=("reference", "cached"), default="reference")
    parser.add_argument("--backend", choices=("reference", "optimized", "graph"), default="reference")
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--precision", choices=("fp32", "fp64"), default="fp64")
    parser.add_argument("--batch", type=int, default=1, help="independent paired conditional noise trials")
    parser.add_argument("--trials", type=int, default=None, help="deprecated alias for --batch")
    parser.add_argument("--taps", type=int, choices=(1, 2), default=2)
    parser.add_argument("--phase-states", type=int, default=512)
    parser.add_argument("--relative-phase-states", type=int, default=8)
    parser.add_argument("--frequency-bound-hz", type=float, default=.02)
    parser.add_argument("--nominal-dt", type=float, default=None,
                        help="default is the export's physical block-center interval")
    parser.add_argument("--frequency-std-hz", type=float, default=.003)
    parser.add_argument("--frequency-correlation-seconds", type=float, default=1000.)
    parser.add_argument("--phase-diffusion", type=float, default=.00872665)
    parser.add_argument("--cn0", type=float, default=-24.)
    parser.add_argument("--seed", type=int, default=31001)
    parser.add_argument("--split", choices=tuple(SPLIT_SALTS), default="development")
    parser.add_argument("--cpu-threads", type=int, default=2)
    parser.add_argument("--memory-gib", type=float, default=4.)
    parser.add_argument("--workspace-memory-mib", type=int, default=512)
    parser.add_argument("--tile-blocks", type=int, default=64)
    parser.add_argument("--max-blocks", type=int, default=0, help="0 means the entire export")
    parser.add_argument("--warmup-blocks", type=int, default=1)
    parser.add_argument("--duration-alternatives", type=int, default=1)
    parser.add_argument("--selection-methods", type=int, default=1)
    parser.add_argument("--actual-observation", action="store_true",
                        help="score the one exported sampled noisy sufficient projection (requires matching C/N0)")
    args = parser.parse_args(argv)
    if args.trials is not None:
        if args.batch != 1:
            parser.error("choose --batch or --trials, not both")
        args.batch = args.trials
    args.trials = args.batch
    return args


if __name__ == "__main__":
    run(parse_args())
