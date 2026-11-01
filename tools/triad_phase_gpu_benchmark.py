#!/usr/bin/env python3
"""Bounded layout/performance discriminator for the finite triad phase model.

The receiver module remains unchanged. Compare its frequency einsum with
T_frequency.T @ weights, including the following phase gather and complete
predictor. Timings reuse resident inputs and cached transitions; they exclude
construction, data generation and host/device transfer. Independent workloads
on the same device can affect these measurements.
"""
from __future__ import annotations

import argparse
from dataclasses import replace
import hashlib
import json
import math
from pathlib import Path
import sys
import time

import torch

from triad_phase_frequency import PhaseFrequencyLikelihood, PhaseFrequencyModel


def _integer(text):
    value = int(text)
    if value < 1:
        raise argparse.ArgumentTypeError("value must be a positive integer")
    return value


def _float_list(text):
    result = tuple(float(value) for value in text.split(","))
    if not result or any(not math.isfinite(value) for value in result):
        raise argparse.ArgumentTypeError("a finite comma-separated list is required")
    return result


def _sync(device):
    if device.type == "cuda":
        torch.cuda.synchronize(device)


def matmul_predict(state, weights, dt):
    """The production finite transition with only frequency layout changed."""
    if dt == 0:
        return weights
    transition = state._transition(dt)
    predicted = state._advect(weights, transition)
    predicted = torch.matmul(transition["frequency"].transpose(0, 1), predicted)
    predicted = state._advect(predicted, transition)
    if len(transition["phase_offsets"]) != 1 or transition["phase_offsets"][0] != 0:
        mixed = torch.zeros_like(predicted)
        for offset, probability in zip(transition["phase_offsets"], transition["phase_probabilities"]):
            mixed.add_(torch.roll(predicted, offset, dims=-1), alpha=probability)
        predicted = mixed
    return predicted/predicted.sum(dim=(-3, -2, -1), keepdim=True)


class MatmulLikelihood(PhaseFrequencyLikelihood):
    def _predict(self, weights, dt):
        return matmul_predict(self, weights, dt)


def describe(tensor):
    return {"shape": list(tensor.shape), "strides": list(tensor.stride()),
            "contiguous": tensor.is_contiguous(), "dtype": str(tensor.dtype),
            "elements": tensor.numel(), "bytes": tensor.numel()*tensor.element_size()}


def compare(left, right):
    difference = (left-right).abs()
    scale = max(float(left.abs().max()), float(right.abs().max()), 1e-30)
    return {"maximum_absolute_error": float(difference.max()),
            "relative_to_maximum_magnitude": float(difference.max())/scale,
            "l1_error": float(difference.to(torch.float64).sum()),
            "exactly_equal": torch.equal(left, right)}


def timed(function, args, device):
    result = None
    for _ in range(args.warmup):
        result = function()
    _sync(device)
    started = time.perf_counter()
    if device.type == "cuda":
        begin, end = torch.cuda.Event(enable_timing=True), torch.cuda.Event(enable_timing=True)
        stream = torch.cuda.current_stream(device)
        begin.record(stream)
    for _ in range(args.repetitions):
        result = function()
    if device.type == "cuda":
        end.record(stream); end.synchronize()
        resident = begin.elapsed_time(end)/1000
    else:
        resident = time.perf_counter()-started
    wall = time.perf_counter()-started
    # Keep the last result live until synchronization; never time a host read.
    return {"repetitions": args.repetitions,
            "resident_loop_seconds": resident,
            "resident_seconds_per_call": resident/args.repetitions,
            "host_wall_seconds": wall,
            "host_wall_seconds_per_call": wall/args.repetitions,
            "last_output": describe(result)}


def parity(args, model, device, dtype):
    if args.parity_steps == 0:
        return {"status": "disabled"}
    batch = min(args.batch, args.parity_batch)
    original = PhaseFrequencyLikelihood(batch, args.bits, args.taps, model,
        device=device, dtype=dtype, memory_limit_bytes=int(args.memory_gib*1024**3), validate_inputs=False)
    proposed = MatmulLikelihood(batch, args.bits, args.taps, model,
        device=device, dtype=dtype, memory_limit_bytes=int(args.memory_gib*1024**3), validate_inputs=False)
    complex_dtype = torch.complex64 if dtype == torch.float32 else torch.complex128
    generator = torch.Generator().manual_seed(args.seed ^ 0x504152495459)
    real = torch.randn((args.parity_steps, batch, args.bits, args.taps), generator=generator, dtype=torch.float64)
    imag = torch.randn(real.shape, generator=generator, dtype=torch.float64)
    observations = (torch.complex(real, imag)*.12).to(device=device, dtype=complex_dtype)
    gram = torch.eye(args.taps, dtype=torch.complex128)[None, None].expand(batch, args.bits, -1, -1).clone()*.04
    if args.taps == 2:
        gram[..., 0, 1] = .004+.003j; gram[..., 1, 0] = .004-.003j
    gram *= (1+torch.arange(batch, dtype=torch.float64)*.05)[:, None, None, None]
    gram = gram.to(device=device, dtype=complex_dtype)
    # This is seeded numerical stress, not a channel/noise performance study.
    _sync(device); start = time.perf_counter()
    for index in range(args.parity_steps):
        dt = 0. if index == 0 else args.dt
        original.update(observations[index], gram, dt)
        proposed.update(observations[index], gram, dt)
    _sync(device)
    scores_old, scores_new = original.scores(), proposed.scores()
    scores = compare(scores_old, scores_new)
    weights = compare(original.weights, proposed.weights)
    tolerance = 2e-4 if dtype == torch.float32 else 2e-10
    passed = scores["maximum_absolute_error"] <= tolerance and weights["relative_to_maximum_magnitude"] <= tolerance
    thresholds = (math.log(100), math.log(10000), math.log(2*1440/1e-4))
    admission_differences = sum(int(((scores_old >= threshold) != (scores_new >= threshold)).sum()) for threshold in thresholds)
    ranking_differences = int((scores_old.argmax(-1) != scores_new.argmax(-1)).sum())
    return {"status": "passed" if passed else "failed", "steps": args.parity_steps,
            "batch": batch, "wall_seconds": time.perf_counter()-start,
            "log_lr": scores, "posterior_weights": weights,
            "absolute_log_lr_and_relative_weight_tolerance": tolerance,
            "checked_log_lr_thresholds": list(thresholds),
            "threshold_decision_differences": admission_differences,
            "candidate_ranking_differences": ranking_differences,
            "original_log_lr": scores_old.cpu().tolist(),
            "proposed_log_lr": scores_new.cpu().tolist(),
            "scope": "identical seeded statistics and finite model; not sensitivity, rare-tail calibration or a near-threshold ranking guarantee"}


def run(args):
    if args.output and args.output.exists():
        raise ValueError("benchmark output exists; choose a fresh file")
    for value, name in ((args.dt, "dt"), (args.memory_gib, "memory cap")):
        if not math.isfinite(value) or value <= 0:
            raise ValueError(f"{name} must be positive and finite")
    if not 0 <= args.warmup <= 100 or not 1 <= args.repetitions <= 10000 or not 0 <= args.parity_steps <= 10000:
        raise ValueError("warmup/repetition/parity count exceeds the explicit benchmark bound")
    torch.set_num_threads(args.cpu_threads)
    torch.set_float32_matmul_precision("highest")
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    device = torch.device(args.device)
    if device.type not in ("cpu", "cuda") or (device.type == "cuda" and not torch.cuda.is_available()):
        raise ValueError("requested CPU/CUDA device is unavailable")
    dtype = torch.float32 if args.precision == "fp32" else torch.float64
    model = PhaseFrequencyModel(phase_states=args.phase_states, frequency_bound_hz=args.frequency_bound_hz,
        nominal_dt_seconds=args.dt, frequency_std_hz=args.frequency_std_hz,
        frequency_correlation_seconds=args.frequency_correlation_seconds,
        random_walk_hz_per_sqrt_second=args.random_walk_hz_per_sqrt_second,
        phase_diffusion_rad_per_sqrt_second=args.phase_diffusion,
        relative_phase_states=args.relative_phase_states)
    if args.amplitude_nodes is not None:
        weights = args.amplitude_weights or tuple(1. for _ in args.amplitude_nodes)
        model = replace(model, amplitude_nodes=args.amplitude_nodes, amplitude_weights=weights)
    elif args.amplitude_weights is not None:
        model = replace(model, amplitude_weights=args.amplitude_weights)
    state = PhaseFrequencyLikelihood(args.batch, args.bits, args.taps, model,
        device=device, dtype=dtype, memory_limit_bytes=int(args.memory_gib*1024**3), validate_inputs=False)
    # Several fixed intermediate tensors remain live for layout/parity checks.
    # Two additional small filter states are used by the sequential check.
    estimate = state.working_bytes_bound*(2+2*min(args.batch, args.parity_batch)/args.batch)
    if estimate > args.memory_gib*1024**3:
        raise ValueError("benchmark's resident/intermediate/parity arrays exceed --memory-gib")
    generator = torch.Generator(device=device).manual_seed(args.seed)
    weights = torch.rand(state.weights.shape, generator=generator, device=device, dtype=dtype)+.001
    weights /= weights.sum(dim=(-3, -2, -1), keepdim=True)
    transition = state._transition(args.dt)
    first = state._advect(weights, transition)
    old_frequency = torch.einsum("...fp,fg->...gp", first, transition["frequency"])
    new_frequency = torch.matmul(transition["frequency"].transpose(0, 1), first)
    old_second, new_second = state._advect(old_frequency, transition), state._advect(new_frequency, transition)
    old_full, new_full = state._predict(weights, args.dt), matmul_predict(state, weights, args.dt)
    errors = {"frequency": compare(old_frequency, new_frequency),
              "frequency_plus_advection": compare(old_second, new_second),
              "full_predict": compare(old_full, new_full)}
    layouts = {"input": describe(weights), "first_advection": describe(first),
               "frequency_einsum": describe(old_frequency), "frequency_matmul": describe(new_frequency),
               "second_advection_einsum": describe(old_second), "second_advection_matmul": describe(new_second)}
    stages = {
        "frequency_only": (lambda: torch.einsum("...fp,fg->...gp", first, transition["frequency"]),
                           lambda: torch.matmul(transition["frequency"].transpose(0, 1), first)),
        "frequency_plus_second_advection": (
            lambda: state._advect(torch.einsum("...fp,fg->...gp", first, transition["frequency"]), transition),
            lambda: state._advect(torch.matmul(transition["frequency"].transpose(0, 1), first), transition)),
        "full_predict": (lambda: state._predict(weights, args.dt), lambda: matmul_predict(state, weights, args.dt))}
    _sync(device)
    if device.type == "cuda":
        torch.cuda.reset_peak_memory_stats(device)
    timing = {}
    for name, (old, new) in stages.items():
        original = timed(old, args, device); proposed = timed(new, args, device)
        timing[name] = {"einsum": original, "matmul": proposed,
                       "resident_speedup": original["resident_seconds_per_call"]/proposed["resident_seconds_per_call"]}
    sequential = parity(args, model, device, dtype)
    return {"schema": "triad-phase-layout-benchmark-v1", "torch_version": torch.__version__,
            "device": str(device), "device_name": torch.cuda.get_device_name(device) if device.type == "cuda" else "CPU",
            "precision": args.precision, "tf32_enabled": False, "cpu_threads": args.cpu_threads,
            "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "phase_module_sha256": hashlib.sha256(Path(__file__).with_name("triad_phase_frequency.py").read_bytes()).hexdigest(),
            "command_argv": sys.argv, "seed": args.seed, "warmup_calls": args.warmup,
            "timing_method": "CUDA events plus synchronized host wall" if device.type == "cuda" else "CPU perf_counter",
            "timing_scope": "resident cached inputs/transitions; excludes construction/transfers/parity and is subject to concurrent-device contention",
            "working_bytes_estimate": estimate, "cuda_peak_allocated_bytes": torch.cuda.max_memory_allocated(device) if device.type == "cuda" else None,
            "layouts": layouts, "single_step_errors": errors, "timings": timing,
            "sequential_parity": sequential, "model": state.diagnostics()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--batch", type=_integer, default=8)
    parser.add_argument("--bits", type=_integer, default=2)
    parser.add_argument("--taps", type=int, choices=(1, 2), default=2)
    parser.add_argument("--phase-states", type=_integer, default=512)
    parser.add_argument("--relative-phase-states", type=_integer, default=8)
    parser.add_argument("--amplitude-nodes", type=_float_list)
    parser.add_argument("--amplitude-weights", type=_float_list)
    parser.add_argument("--frequency-bound-hz", type=float, default=.02)
    parser.add_argument("--frequency-std-hz", type=float, default=.003)
    parser.add_argument("--frequency-correlation-seconds", type=float, default=1000.)
    parser.add_argument("--random-walk-hz-per-sqrt-second", type=float, default=1e-6)
    parser.add_argument("--phase-diffusion", type=float, default=.00872665)
    parser.add_argument("--dt", type=float, default=4.)
    parser.add_argument("--precision", choices=("fp32", "fp64"), default="fp32")
    parser.add_argument("--warmup", type=int, default=5)
    parser.add_argument("--repetitions", type=_integer, default=20)
    parser.add_argument("--parity-steps", type=int, default=100)
    parser.add_argument("--parity-batch", type=_integer, default=2)
    parser.add_argument("--memory-gib", type=float, default=4.)
    parser.add_argument("--cpu-threads", type=_integer, default=2)
    parser.add_argument("--seed", type=int, default=19129)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        with torch.inference_mode():
            result = run(args)
        encoded = json.dumps(result, indent=2, allow_nan=False)+"\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            with args.output.open("x") as stream:
                stream.write(encoded)
        else:
            sys.stdout.write(encoded)
        return 0 if result["sequential_parity"]["status"] != "failed" else 1
    except (ValueError, RuntimeError, OSError, OverflowError) as error:
        print(f"triad-phase-layout-benchmark: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
