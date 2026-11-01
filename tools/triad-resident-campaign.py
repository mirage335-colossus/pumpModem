#!/usr/bin/env python3
"""Bounded persistent-receiver campaign on one exported physical design.

Independent noise batches share one fixed sampled physical path. H0/H1 are
paired, not independent of each other. All selected candidates and finite
phase/frequency/gain states are retained. Compare original and optimized math
under the same preparation/lifetime policy before comparing different policies.
This is conditional screening, not operational admission qualification.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import sys
import time

import numpy as np
from scipy.stats import beta
import torch

from triad_phase_frequency import PhaseFrequencyLikelihood, PhaseFrequencyModel
from triad_state_workspace import BlockWorkspace


SPLITS = {"development": 0x444556, "calibration": 0x43414C, "holdout": 0x484F4C44}
TARGETS = {"default": (.99, 1e-4, 86400.), "relaxed": (.90, .01, 43200.)}
GRAPH_BACKENDS = ("graph", "graph-fp64")
MAX_NOISE_BATCHES = 32


def sync(device):
    if device.type == "cuda":
        torch.cuda.synchronize(device)


def timed(function, device):
    sync(device)
    events = (torch.cuda.Event(enable_timing=True), torch.cuda.Event(enable_timing=True)) if device.type == "cuda" else None
    started = time.perf_counter()
    if events:
        events[0].record()
    value = function()
    if events:
        events[1].record()
    sync(device)
    return value, {"wall_seconds": time.perf_counter()-started,
                   "cuda_event_seconds": events[0].elapsed_time(events[1])/1000 if events else None}


def sha_file(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b""):
            result.update(chunk)
    return result.hexdigest()


def hash_array(array):
    return hashlib.sha256(memoryview(np.ascontiguousarray(array)).cast("B")).hexdigest()


def noise_seed(root, split, batch):
    return int(np.random.SeedSequence([root, SPLITS[split], batch]).generate_state(1, dtype=np.uint64)[0])


def interval(successes, total):
    return [0. if successes == 0 else float(beta.ppf(.05, successes, total-successes+1)),
            1. if successes == total else float(beta.ppf(.95, successes+1, total-successes))]


def bit_scores(scores, labels):
    output = []
    for bit in (0, 1):
        selected = scores[..., labels == bit]
        maximum = selected.max(-1, keepdims=True)
        output.append((maximum+np.log(np.exp(selected-maximum).mean(-1, keepdims=True)))[..., 0])
    return np.stack(output, axis=-1)


def decisions(scores, labels, truth, args):
    # score shape [noise_batches, null/signal, trials, candidates]. Only
    # independent noise trials at this one physical path enter the interval.
    bits = bit_scores(scores, labels)
    result = {}
    total = scores.shape[0]*scores.shape[2]
    for name, (correct_target, false_target, horizon) in TARGETS.items():
        attempts = math.ceil(horizon/args.attempt_spacing)
        threshold = math.log(attempts*args.duration_alternatives*args.selection_methods*2/false_target)
        accepted = bits.max(-1) >= threshold
        winner = bits.argmax(-1)
        correct = int((accepted[:, 1] & (winner[:, 1] == truth)).sum())
        wrong = int((accepted[:, 1] & (winner[:, 1] != truth)).sum())
        null = int(accepted[:, 0].sum())
        result[name] = {"correct_target": correct_target, "false_probability_target": false_target,
            "horizon_seconds": horizon, "attempts_per_horizon": attempts,
            "threshold_nats": threshold, "conditional_noise_trials_per_kind": total,
            "correct_bit_accepted": correct, "wrong_bit_accepted": wrong,
            "missed": total-correct-wrong, "null_admitted": null,
            "correct_one_sided_95": interval(correct, total),
            "null_one_sided_95": interval(null, total),
            "scope": "conditional fixed physical path, known Gaussian variance; analytic family gate only; no empirical operational horizon or synchronization qualification"}
    return result


def select(workspace, taps):
    indices = [i for i, mapping in enumerate(workspace.column_mapping) if len(mapping) == taps]
    labels = np.asarray([workspace.candidates[i]["bit"] for i in indices])
    if set(labels.tolist()) != {0, 1}:
        raise ValueError("both source bits with the selected tap count are required")
    return indices, np.asarray([workspace.column_mapping[i] for i in indices]), labels


def calendar_dts(workspace, count):
    previous, result = None, []
    for index in range(count):
        first = index*workspace.block_samples
        last = min(first+workspace.block_samples, workspace.sample_count)
        center = (first+last)/(2.*workspace.sample_rate)
        result.append(0. if previous is None else center-previous)
        previous = center
    return np.asarray(result)


def memory_preflight(workspace, args, candidate_count, dts, device):
    """Conservative live-array bounds before design/observation allocation."""
    blocks, batch, dimension = args.stop, args.batch, workspace.dimension
    item = 8 if args.precision == "fp32" else 16
    real_item = item//2
    host_observations = 2*blocks*batch*candidate_count*args.taps*16
    represented_observations = host_observations//16*item
    host_grams = blocks*candidate_count*args.taps**2*16
    represented_grams = host_grams//16*item
    tile = min(workspace.max_tile_blocks, blocks)
    validation_scratch = tile*(4*dimension**2*16+4*dimension*16+256)
    factor_scratch = 4*dimension**2*16+8*dimension*16
    pair_scratch = batch*dimension*16*6+dimension**2*16
    if args.observation_preparation == "cached":
        design = blocks*(2*dimension**2*16+dimension*16+40)
    else:
        # Public Gram list plus stack can coexist during original preparation.
        design = 2*blocks*dimension**2*16
    score_scratch = args.batches*2*batch*candidate_count*8*8
    # A new complex128 batch can coexist with the previous host batch and its
    # represented copy before assignment releases the old arrays.
    host_preparation = (design+validation_scratch+factor_scratch+pair_scratch+
        2*host_observations+represented_observations+2*host_grams+represented_grams+
        score_scratch+len(dts)*8+1024**2)
    host_cap = int(args.workspace_memory_mib*1024**2)
    if host_preparation > host_cap:
        raise MemoryError(f"retained host/design/observation bound {host_preparation} bytes exceeds --workspace-memory-mib before allocation")
    spacing = (2/args.phase_states)/args.nominal_dt
    frequencies = 2*math.ceil(args.frequency_bound/spacing)+1
    if frequencies > 257:
        raise ValueError("aligned frequency grid exceeds 257 states")
    entries = max(1, min(32, len(set(dts))))
    gains = 3*(args.relative_phase_states if args.taps == 2 else 1)
    elements = batch*candidate_count*gains*frequencies*args.phase_states
    base = elements*real_item*16+entries*(frequencies**2*16+
        frequencies*args.phase_states*24+args.phase_states*32)+65536
    receiver = base
    if args.backend in GRAPH_BACKENDS:
        graph_inputs = batch*candidate_count*(args.taps+args.taps**2)*item
        receiver = (entries+1)*base+elements*real_item+graph_inputs
    else:
        # The unmodified reference/fused objects need one retained initial
        # posterior copy for reset; the graph class already owns its own copy.
        receiver += elements*real_item
    fixed = represented_observations+represented_grams
    device_total = fixed+receiver
    if device_total > int(args.memory_gib*1024**3):
        raise MemoryError(f"persistent device/state/graph bound {device_total} bytes exceeds --memory-gib before allocation")
    host_total = host_preparation+(device_total if device.type == "cpu" else 0)
    host_available = None
    try:
        host_available = os.sysconf("SC_AVPHYS_PAGES")*os.sysconf("SC_PAGE_SIZE")
    except (OSError, ValueError, AttributeError):
        pass
    if host_available is not None and host_total > .85*host_available:
        raise MemoryError("persistent campaign exceeds 85% of available host memory before allocation")
    device_free = None
    if device.type == "cuda":
        device_free, _ = torch.cuda.mem_get_info(device)
        if device_total > .85*device_free:
            raise MemoryError("persistent inputs/state/graph pools exceed 85% of free VRAM before allocation")
    return {"host_preparation_and_retained_arrays_bound_bytes": host_preparation,
        "host_total_bound_including_cpu_execution_bytes": host_total,
        "host_limit_bytes": host_cap, "host_available_bytes": host_available,
        "fixed_device_input_buffer_bytes": fixed, "receiver_working_bound_bytes": receiver,
        "combined_device_input_and_receiver_bound_bytes": device_total,
        "device_limit_bytes": int(args.memory_gib*1024**3), "device_free_bytes": device_free,
        "scope": "conservative owned-array/scratch bound; excludes framework runtime, allocator cache, mapped-file page residency and Python object overhead"}


def prepare_design(workspace, args, mapping):
    prepared = None
    if args.observation_preparation == "cached":
        from triad_prepared_observation import PreparedGaussianObservation
        prepared = PreparedGaussianObservation(workspace, seed=noise_seed(args.seed, args.split, 0),
            batch=args.batch, cn0_db_hz=args.cn0, stop=args.stop,
            memory_limit_bytes=args.workspace_memory_mib*1024**2)
        raw_grams = prepared.grams
        dts = prepared.dts.copy()
    else:
        grams, dts, previous = [], [], None
        for tile in workspace.iter_public(stop=args.stop):
            for gram, interval in zip(tile.gram, tile.intervals):
                center = (int(interval[0])+int(interval[1]))/(2*workspace.sample_rate)
                grams.append(gram.copy())
                dts.append(0. if previous is None else center-previous)
                previous = center
        raw_grams, dts = np.stack(grams), np.asarray(dts)
    variance = workspace.sample_rate/10**(args.cn0/10)
    grams = raw_grams[:, mapping[:, :, None], mapping[:, None, :]]/variance
    return prepared, np.ascontiguousarray(grams), np.asarray(dts)


def collect_pair(workspace, prepared, args, mapping, seed):
    observations = np.empty((2, args.stop, args.batch, len(mapping), args.taps), dtype=np.complex128)
    if prepared is not None:
        for index, pair in enumerate(prepared.iter_paired(seed=seed)):
            observations[0, index] = pair.null.r[:, mapping]
            observations[1, index] = pair.signal.r[:, mapping]
    else:
        for kind in (0, 1):
            for index, block in enumerate(workspace.iter_simulator(seed=seed, batch=args.batch,
                    cn0_db_hz=args.cn0, signal=bool(kind), stop=args.stop)):
                observations[kind, index] = block.r[:, mapping]
    return observations


def make_receiver(args, bits, device, dts):
    cls = PhaseFrequencyLikelihood
    if args.backend != "reference":
        from triad_phase_optimized import GraphPhaseFrequencyLikelihood, OptimizedPhaseFrequencyLikelihood
        if args.backend == "graph-fp64":
            from triad_phase_reduction import Fp64ReductionGraphLikelihood
            cls = Fp64ReductionGraphLikelihood
        else:
            cls = GraphPhaseFrequencyLikelihood if args.backend == "graph" else OptimizedPhaseFrequencyLikelihood
    model = PhaseFrequencyModel(phase_states=args.phase_states, relative_phase_states=args.relative_phase_states,
        nominal_dt_seconds=args.nominal_dt, frequency_bound_hz=args.frequency_bound,
        frequency_std_hz=args.frequency_std, frequency_correlation_seconds=args.frequency_correlation,
        phase_diffusion_rad_per_sqrt_second=args.phase_diffusion,
        transition_cache_entries=max(1, min(32, len(set(dts)))))
    return cls(args.batch, bits, args.taps, model, device=device,
        dtype=torch.float32 if args.precision == "fp32" else torch.float64,
        memory_limit_bytes=int(args.memory_gib*1024**3), validate_inputs=False)


def reset_receiver(receiver, initial):
    if hasattr(receiver, "reset"):
        receiver.reset()
    else:
        receiver.weights.copy_(initial)
        receiver._scores.zero_()
        receiver.updates = 0
        receiver.elapsed_seconds = 0.


def initialize_receiver(args, bits, device, dts, observations, grams):
    receiver = make_receiver(args, bits, device, dts)
    initial = None if hasattr(receiver, "reset") else receiver.weights.clone()
    seen = set()
    for index, dt in enumerate(dts):
        dt = float(dt)
        if dt not in seen:
            receiver.update(observations[0, index], grams[index], dt)
            seen.add(dt)
    reset_receiver(receiver, initial)
    return receiver, initial


def run_inference(receiver, observations, grams, dts):
    for index, dt in enumerate(dts):
        receiver.update(observations[index], grams[index], float(dt))


def compare_report(path, identity, args, scores, hashes, labels):
    prior = json.loads(path.read_text())
    if prior["configuration"]["backend"] != "reference":
        raise ValueError("numerical comparison requires an original reference backend campaign")
    keys = ("batch", "batches", "precision", "cn0", "seed", "split", "phase_states", "relative_phase_states",
            "taps", "frequency_bound", "frequency_std", "frequency_correlation", "phase_diffusion", "nominal_dt",
            "max_blocks", "attempt_spacing", "duration_alternatives", "selection_methods")
    if any(prior["configuration"].get(key) != getattr(args, key) for key in keys):
        raise ValueError("reference campaign model/seed/family settings differ")
    if prior["input_identity"] != identity or prior["represented_observation_sha256_by_batch"] != hashes:
        raise ValueError("reference campaign input bytes or represented observations differ")
    archive_path = path.with_suffix(".scores.npz")
    with np.load(archive_path) as archive:
        reference = archive["scores"]
    if reference.shape != scores.shape:
        raise ValueError("reference score shapes differ")
    indices = list(range(args.batches))
    left, right = reference[indices], scores[indices]
    if not np.isfinite(left).all() or not np.isfinite(right).all():
        raise ValueError("nonfinite comparison score")
    lbits, rbits = bit_scores(left, labels), bit_scores(right, labels)
    result = {"reference_report": str(path), "reference_report_sha256": sha_file(path),
        "reference_archive_sha256": sha_file(archive_path), "checked_batch_indices": indices,
        "boundary_batch_indices": sorted({0, args.batches-1}),
        "maximum_absolute_log_lr_error": float(np.abs(left-right).max()),
        "candidate_ranking_differences": int((left.argmax(-1) != right.argmax(-1)).sum()),
        "bit_ranking_differences": int((lbits.argmax(-1) != rbits.argmax(-1)).sum()),
        "reference_complete_campaign_wall_seconds": prior["timing"]["complete_campaign_wall_before_json_seconds"],
        "reference_processing_pipeline_seconds": prior["timing"]["complete_processing_pipeline_seconds"],
        "scope": "all saved independent noise batches, including first/last, with identical represented observations; numerical screen only; reference campaign cost is reported separately"}
    for name, (_, false_target, horizon) in TARGETS.items():
        threshold = math.log(math.ceil(horizon/args.attempt_spacing)*args.duration_alternatives*args.selection_methods*2/false_target)
        result[name+"_admission_differences"] = int(((lbits.max(-1) >= threshold) != (rbits.max(-1) >= threshold)).sum())
    return result


def run(args):
    started = time.perf_counter()
    if args.output.exists() or args.output.with_suffix(".scores.npz").exists():
        raise ValueError("fresh report and score paths required")
    if not 1 <= args.batches <= MAX_NOISE_BATCHES or args.batch < 1 or args.max_blocks < 0:
        raise ValueError(f"one through {MAX_NOISE_BATCHES} independent noise batches and positive batch required")
    for value in (args.cn0, args.memory_gib, args.frequency_bound, args.frequency_std,
                  args.frequency_correlation, args.phase_diffusion, args.attempt_spacing):
        if not math.isfinite(value):
            raise ValueError("finite parameters required")
    if min(args.memory_gib, args.workspace_memory_mib, args.attempt_spacing,
           args.duration_alternatives, args.selection_methods) <= 0:
        raise ValueError("positive resource/family limits required")
    if not 2 <= args.phase_states <= 4096 or not 1 <= args.relative_phase_states <= 256 or args.cpu_threads < 1:
        raise ValueError("invalid finite-state geometry or CPU thread count")
    if args.frequency_bound < 0 or args.frequency_std < 0 or args.frequency_correlation <= 0 or args.phase_diffusion < 0:
        raise ValueError("invalid finite oscillator model")
    torch.set_num_threads(args.cpu_threads)
    torch.set_float32_matmul_precision("highest")
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    device = torch.device(args.device)
    context_started = time.perf_counter()
    if device.type == "cuda":
        if not torch.cuda.is_available():
            raise ValueError("CUDA unavailable")
        torch.cuda.set_device(device)
        torch.empty(1, device=device)
        sync(device)
    elif device.type != "cpu":
        raise ValueError("CPU/CUDA only")
    context_seconds = time.perf_counter()-context_started
    prepare_started = time.perf_counter()
    workspace = BlockWorkspace(args.input, memory_limit_bytes=args.workspace_memory_mib*1024**2)
    if not workspace.gaussian_projection_supported:
        raise ValueError("nonlinear/mismatched sampled frontend cannot use a Gaussian resident campaign")
    args.stop = workspace.block_count if args.max_blocks == 0 else min(args.max_blocks, workspace.block_count)
    if args.nominal_dt is None:
        args.nominal_dt = workspace.block_samples/workspace.sample_rate
    if not math.isfinite(args.nominal_dt) or args.nominal_dt <= 0:
        raise ValueError("positive finite nominal dt required")
    selected, mapping, labels = select(workspace, args.taps)
    expected_dts = calendar_dts(workspace, args.stop)
    memory = memory_preflight(workspace, args, len(selected), expected_dts, device)
    prepared, gram_host, dts = prepare_design(workspace, args, mapping)
    if not np.array_equal(dts, expected_dts):
        raise ValueError("public block calendar differs from the memory preflight")
    if args.backend in GRAPH_BACKENDS and len(set(dts)) > 32:
        raise ValueError("more than 32 exact public dt values cannot remain resident in the bounded graph cache")
    dtype = torch.complex64 if args.precision == "fp32" else torch.complex128
    represented_grams = gram_host.astype(np.complex64 if args.precision == "fp32" else np.complex128)
    design_seconds = time.perf_counter()-prepare_started
    # Persistent observation storage is overwritten per independent batch.
    def allocate():
        gram_device = torch.as_tensor(represented_grams, dtype=dtype, device=device)
        observations = torch.zeros((2, args.stop, args.batch, len(selected), args.taps), dtype=dtype, device=device)
        return gram_device, observations
    (gram_device, observation_device), allocation = timed(allocate, device)
    receiver = initial = None
    setup_seconds = 0.
    if args.receiver_lifetime == "campaign":
        (receiver, initial), setup = timed(lambda: initialize_receiver(args, len(selected), device,
            dts, observation_device, gram_device), device)
        setup_seconds += setup["wall_seconds"]
    scores = np.empty((args.batches, 2, args.batch, len(selected)), dtype=np.float64)
    batches, hashes = [], []
    for batch_index in range(args.batches):
        seed = noise_seed(args.seed, args.split, batch_index)
        tick = time.perf_counter()
        host = collect_pair(workspace, prepared, args, mapping, seed)
        represented = np.ascontiguousarray(host, dtype=np.complex64 if args.precision == "fp32" else np.complex128)
        generation = time.perf_counter()-tick
        tick = time.perf_counter()
        hashes.append(hash_array(represented))
        hash_seconds = time.perf_counter()-tick
        _, transfer = timed(lambda: observation_device.copy_(torch.from_numpy(represented)), device)
        inference, reset, d2h, capture_setup = [], [], [], []
        for kind in (0, 1):
            if args.receiver_lifetime == "capture":
                (receiver, initial), setup = timed(lambda: initialize_receiver(args, len(selected), device,
                    dts, observation_device, gram_device), device)
                setup_seconds += setup["wall_seconds"]
                capture_setup.append(setup["wall_seconds"])
            _, reset_time = timed(lambda: reset_receiver(receiver, initial), device)
            reset.append(reset_time["wall_seconds"])
            _, measured = timed(lambda: run_inference(receiver, observation_device[kind], gram_device, dts), device)
            inference.append(measured)
            value, score_copy = timed(lambda: receiver.scores().cpu().numpy().copy(), device)
            scores[batch_index, kind] = value
            d2h.append(score_copy["wall_seconds"])
            if args.receiver_lifetime == "capture":
                del receiver, initial
                receiver = initial = None
        batches.append({"noise_batch_index": batch_index, "seed": seed,
            "paired_observation_generation_and_storage_conversion_seconds": generation,
            "represented_input_hashing_seconds": hash_seconds, "h2d": transfer,
            "reset_seconds": reset, "inference_null_signal": inference,
            "score_d2h_seconds": d2h, "receiver_setup_seconds": capture_setup})
    processing_done = time.perf_counter()
    verification_start = time.perf_counter()
    if not np.isfinite(scores).all():
        raise ValueError("nonfinite receiver scores")
    metrics = decisions(scores, labels, int(workspace.manifest["signal_bit"]), args)
    identity = {"manifest_sha256": sha_file(workspace.manifest_path),
        "files": {name: sha_file(args.input/name) for name in sorted(workspace.manifest["file_sizes"])},
        "selected_candidates": selected, "column_mapping": mapping.tolist(),
        "represented_gram_sha256": hash_array(represented_grams), "scored_blocks": args.stop}
    comparison = compare_report(args.compare_report, identity, args, scores, hashes, labels) if args.compare_report else None
    verification_seconds = time.perf_counter()-verification_start
    artifacts_start = time.perf_counter()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(args.output.with_suffix(".scores.npz"), scores=scores)
    sources = [Path(__file__), Path(__file__).with_name("triad_phase_frequency.py"),
               Path(__file__).with_name("triad_state_workspace.py")]
    if args.backend != "reference":
        sources.append(Path(__file__).with_name("triad_phase_optimized.py"))
    if args.backend == "graph-fp64":
        sources.append(Path(__file__).with_name("triad_phase_reduction.py"))
    if prepared is not None:
        sources.append(Path(__file__).with_name("triad_prepared_observation.py"))
    source_hashes = {p.name: sha_file(p) for p in sources}
    artifact_seconds = time.perf_counter()-artifacts_start
    generation_total = sum(b["paired_observation_generation_and_storage_conversion_seconds"] for b in batches)
    transfer_total = sum(b["h2d"]["wall_seconds"] for b in batches)
    inference_total = sum(sum(t["wall_seconds"] for t in b["inference_null_signal"]) for b in batches)
    reset_total = sum(sum(b["reset_seconds"]) for b in batches)
    d2h_total = sum(sum(b["score_d2h_seconds"]) for b in batches)
    pipeline = context_seconds+design_seconds+allocation["wall_seconds"]+setup_seconds+generation_total+transfer_total+inference_total+reset_total+d2h_total+verification_seconds
    observations_scored = args.batches*args.batch*2
    fixed = context_seconds+design_seconds+allocation["wall_seconds"]+(setup_seconds if args.receiver_lifetime == "campaign" else 0.)
    variable = (generation_total+transfer_total+inference_total+reset_total+d2h_total+
                (setup_seconds if args.receiver_lifetime == "capture" else 0.))
    per_batch_variable = variable/args.batches
    report = {"schema": "triad-resident-campaign-v1", "status": "conditional_screening_only",
        "configuration": {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items() if k != "stop"},
        "environment": {"python": platform.python_version(), "torch": torch.__version__, "numpy": np.__version__,
            "device": str(device), "device_name": torch.cuda.get_device_name(device) if device.type == "cuda" else "CPU",
            "cuda": torch.version.cuda, "tf32_enabled": torch.backends.cuda.matmul.allow_tf32,
            "float32_matmul_precision": torch.get_float32_matmul_precision()},
        "input_identity": identity, "source_sha256": source_hashes,
        "represented_observation_sha256_by_batch": hashes,
        "memory_preflight": memory,
        "workload": {"physical_paths": 1, "conditional_independent_noise_trials_per_kind": args.batch*args.batches,
            "paired_observations_scored": observations_scored, "candidates": len(selected),
            "scored_blocks": args.stop, "full_blocks": workspace.block_count,
            "physical_capture_seconds": workspace.sample_count/workspace.sample_rate,
            "truncated": args.stop != workspace.block_count, "all_selected_candidates_scored": True,
            "null_and_signal_share_noise": True, "batch_seed_policy": "SeedSequence([root, fixed split salt, independent noise batch index]); per-block RNG preserved",
            "receiver_diagnostics": receiver.diagnostics() if receiver is not None else None,
            "prepared_design_diagnostics": prepared.diagnostics() if prepared is not None else None},
        "timing": {"cuda_context_initialization_seconds": context_seconds,
            "workspace_validation_and_design_preparation_seconds": design_seconds,
            "persistent_allocation_and_gram_h2d": allocation, "receiver_setup_warmup_capture_seconds": setup_seconds,
            "paired_observation_generation_seconds": generation_total, "observation_h2d_seconds": transfer_total,
            "inference_seconds": inference_total, "explicit_reset_seconds": reset_total,
            "score_d2h_seconds": d2h_total, "verification_and_input_hashing_seconds": verification_seconds,
            "artifact_serialization_and_source_hashing_seconds": artifact_seconds,
            "complete_processing_pipeline_seconds": pipeline,
            "processing_wall_before_verification_seconds": processing_done-started,
            "complete_campaign_wall_before_json_seconds": time.perf_counter()-started,
            "paired_observations_per_second_complete_processing": observations_scored/pipeline,
            "paired_observations_per_second_inference": observations_scored/inference_total,
            "steady_variable_pipeline_paired_observations_per_second": 2*args.batch/per_batch_variable,
            "fixed_setup_seconds": fixed, "variable_pipeline_seconds_per_noise_batch": per_batch_variable,
            "amortization": [{"independent_noise_batches": n,
                "projected_processing_seconds_excluding_verification": fixed+n*per_batch_variable,
                "projected_paired_observations_per_second": 2*args.batch*n/(fixed+n*per_batch_variable)} for n in (1, 2, 4, 8, 16, 32)],
            "resident_pipeline_speedup_ceiling_if_inference_zero": pipeline/(pipeline-inference_total) if pipeline > inference_total else None,
            "scope": "complete processing includes all first-use context/design/receiver capture/reset/generation/transfer/inference/D2H and verification; external reference campaign cost reported separately, not charged again; provenance observation hashes and archive/JSON I/O are outside processing total and inside complete driver wall"},
        "batches": batches, "decisions": metrics, "comparison_to_original_reference": comparison,
        "limits": ["One sampled physical design; independent noise batches are not independent channels.",
            "Receiver nuisance parameters are explicit in configuration: this benchmark's default phase diffusion 0.00872665 differs from the existing path-holdout profile default 0.0153; sensitivity claims do not transfer between these workloads.",
            "No sampled ADC/DAC/interference processing, blind acquisition, hour/day operational null schedule or sensitivity-loss certification.",
            "Same preparation and receiver lifetime are the primary matched arithmetic comparison; compare best absolute baseline and optimized configurations across batch sizes.",
            "Amortization points beyond executed batch count are labeled projections, not measured campaigns.",
            "No cost per operationally validated result is established."]}
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False)+"\n")
    return report


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("input", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--compare-report", type=Path)
    p.add_argument("--backend", choices=("reference", "optimized", "graph", "graph-fp64"), default="reference")
    p.add_argument("--observation-preparation", choices=("cached", "reference"), default="cached")
    p.add_argument("--receiver-lifetime", choices=("campaign", "capture"), default="campaign")
    p.add_argument("--device", default="cpu")
    p.add_argument("--precision", choices=("fp32", "fp64"), default="fp32")
    p.add_argument("--batch", type=int, default=8)
    p.add_argument("--batches", type=int, default=8,
                   help=f"independent noise batches, bounded to 1 through {MAX_NOISE_BATCHES}")
    p.add_argument("--cn0", type=float, default=-18.)
    p.add_argument("--seed", type=int, default=170041)
    p.add_argument("--split", choices=tuple(SPLITS), default="development")
    p.add_argument("--phase-states", type=int, default=512)
    p.add_argument("--relative-phase-states", type=int, default=8)
    p.add_argument("--taps", type=int, choices=(1, 2), default=2)
    p.add_argument("--nominal-dt", type=float)
    p.add_argument("--frequency-bound", type=float, default=.02)
    p.add_argument("--frequency-std", type=float, default=.003)
    p.add_argument("--frequency-correlation", type=float, default=1000.)
    p.add_argument("--phase-diffusion", type=float, default=.00872665)
    p.add_argument("--memory-gib", type=float, default=12.)
    p.add_argument("--workspace-memory-mib", type=int, default=512)
    p.add_argument("--cpu-threads", type=int, default=2)
    p.add_argument("--max-blocks", type=int, default=0)
    p.add_argument("--attempt-spacing", type=float, default=60.)
    p.add_argument("--duration-alternatives", type=int, default=1)
    p.add_argument("--selection-methods", type=int, default=1)
    return p


if __name__ == "__main__":
    with torch.inference_mode():
        run(parser().parse_args())
