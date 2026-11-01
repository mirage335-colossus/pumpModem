#!/usr/bin/env python3
"""Isolated RBPF CPU/CUDA kernel and precision benchmark, not a modem trial.

All devices consume the same CPU-generated r/G and declared finite phase bank.
The bank is independent of observations, with no adaptive resampling or best-
seed selection. Its CPU generation, transfer, initialization/filtering, finish,
and complete single-use times are charged separately. This synthetic Gaussian
workload cannot establish waveform sensitivity, oscillator realism, or calibrated
operational admission. Actual sampled receiver validation is a separate study.

Cheap default: --batches 8 --particles 32 --blocks 8 --devices cpu
Larger explicit run: --batches 32,128,256 --particles 128,512,2048 --blocks 64
                    --devices cpu,cuda:0 --output fresh-result.json
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import platform
import statistics
import sys
import time

import torch

from triad_gaussian_state import GaussianPrior
from triad_rbpf import RBPhaseFilter


def memory_estimate(batch, particles, blocks, taps):
    """Conservative host+device array allowance, excluding allocator caches.

    Ten bulk and 32 scalar particle arrays deliberately exceed the core's
    eight/24 allowance. Two bank copies cover host/device residency, and an
    independent preparation allowance covers temporary CPU design/noise arrays.
    """
    samples = max(8, 4*taps)
    bank = blocks*batch*(16*taps+16*taps*taps+8*particles)
    core = batch*(particles*(10*taps*16+32*8)+32*16*taps*taps+256)
    preparation = batch*(4*samples*taps*16+4*samples*16+8*taps*taps*16)
    return {"bank_bytes": bank, "core_array_allowance_bytes": core,
            "estimated_peak_working_bytes": 2*bank+core+preparation,
            "estimated_device_array_bytes": bank+core}


def _complex_normal(shape, generator):
    return torch.complex(torch.randn(shape, dtype=torch.float64, generator=generator),
                         torch.randn(shape, dtype=torch.float64, generator=generator))/math.sqrt(2.)


def prepare(batch, particles, blocks, taps, seed, dt, phase_sigma, frequency_bound, memory_limit):
    estimate = memory_estimate(batch, particles, blocks, taps)
    if estimate["estimated_peak_working_bytes"] > memory_limit:
        raise ValueError("benchmark preflight array estimate exceeds memory budget")
    start = time.perf_counter()
    generator = torch.Generator(device="cpu").manual_seed(seed)
    samples = max(8, 4*taps)
    r = torch.empty((blocks, batch, taps), dtype=torch.complex128)
    G = torch.empty((blocks, batch, taps, taps), dtype=torch.complex128)
    # Different leading items are independent synthetic captures/models. Their
    # ranking is only a paired arithmetic diagnostic, not a bit-error metric.
    h = _complex_normal((batch, taps), generator)/math.sqrt(taps)
    h *= torch.linspace(0., 2., batch, dtype=torch.float64)[:, None]
    for block in range(blocks):
        A = _complex_normal((batch, samples, taps), generator)/math.sqrt(samples)
        y = (A@h[..., None]).squeeze(-1)+_complex_normal((batch, samples), generator)
        r[block] = (A.mH@y[..., None]).squeeze(-1)
        G[block] = A.mH@A
    # Separate independent randomness prevents phase trajectories from depending
    # on this realization's received evidence. These exact increments are copied
    # to every device and precision mode; no CUDA RNG identity is assumed.
    phase_generator = torch.Generator(device="cpu").manual_seed(seed ^ 0x5A17)
    increments = torch.randn((blocks, batch, particles), dtype=torch.float64, generator=phase_generator)
    increments.mul_(phase_sigma*math.sqrt(dt))
    frequency = torch.rand((batch, particles), dtype=torch.float64, generator=phase_generator)
    frequency.mul_(2*frequency_bound).sub_(frequency_bound)
    increments.add_(frequency*(2*math.pi*dt))
    increments[0].zero_()
    if not bool(torch.isfinite(r).all() and torch.isfinite(G).all() and torch.isfinite(increments).all()):
        raise ValueError("nonfinite benchmark inputs")
    if float((G-G.mH).abs().max()) > 1e-12 or float(torch.linalg.eigvalsh(G).min()) < -1e-12:
        raise ValueError("invalid prepared benchmark Gram")
    digest = hashlib.sha256()
    for value in (r, G, increments):
        digest.update(memoryview(value.numpy()).cast("B"))
    return {"r": r, "G": G, "increments": increments,
            "input_sha256": digest.hexdigest(), "memory": estimate,
            "preparation_seconds": time.perf_counter()-start}


def _sync(device):
    if device.type == "cuda":
        torch.cuda.synchronize(device)


def _one_run(inputs, prior, particles, dtype, device, dt, memory_limit):
    _sync(device)
    if device.type == "cuda":
        torch.cuda.reset_peak_memory_stats(device)
        baseline = torch.cuda.memory_allocated(device)
    else:
        baseline = None
    start = time.perf_counter()
    f = RBPhaseFilter(prior, particles=particles, dtype=dtype, seed=1,
                      resampling="none", check_inputs=False, memory_limit_bytes=memory_limit)
    for block in range(len(inputs["r"])):
        f.update(inputs["r"][block], inputs["G"][block], 0. if block == 0 else dt,
                 phase_increment_radians=inputs["increments"][block])
    _sync(device)
    core_seconds = time.perf_counter()-start
    start = time.perf_counter()
    scores = f.finish().detach().to("cpu").clone()
    diagnostics = f.diagnostics()
    _sync(device)
    finish_seconds = time.perf_counter()-start
    return {"core_seconds": core_seconds, "finish_seconds": finish_seconds,
            "resident_total_seconds": core_seconds+finish_seconds,
            "cuda_baseline_allocated_bytes": baseline,
            "cuda_peak_allocated_bytes": torch.cuda.max_memory_allocated(device) if device.type == "cuda" else None,
            "cuda_peak_reserved_bytes": torch.cuda.max_memory_reserved(device) if device.type == "cuda" else None,
            "diagnostics": diagnostics, "scores": scores.tolist()}


def compare(reference, candidate, gate):
    a, b = torch.tensor(reference, dtype=torch.float64), torch.tensor(candidate, dtype=torch.float64)
    error = b-a
    return {"maximum_absolute_log_lr_error": float(error.abs().max()),
            "rms_log_lr_error": float(error.square().mean().sqrt()),
            "top_rank_changed": int(a.argmax()) != int(b.argmax()),
            "rank_positions_changed": int((torch.argsort(a) != torch.argsort(b)).sum()),
            "gate_log_lr": gate, "gate_decisions_changed": int(((a >= gate) != (b >= gate)).sum()),
            "minimum_reference_distance_to_gate": float((a-gate).abs().min()),
            "reference_passes_gate": int((a >= gate).sum()),
            "candidate_passes_gate": int((b >= gate).sum())}


def benchmark_case(args, batch, particles):
    data = prepare(batch, particles, args.blocks, args.taps, args.seed, args.dt,
                   args.phase_sigma, args.frequency_bound, args.memory_limit)
    case = {"batch": batch, "particles": particles, "blocks": args.blocks,
            "taps": args.taps, "input_sha256": data["input_sha256"],
            "memory_preflight": data["memory"], "preparation_seconds": data["preparation_seconds"],
            "runs": [], "comparisons": [], "speedups": []}
    prior = GaussianPrior((1./args.taps,)*args.taps, coherence_seconds=args.coherence)
    for device_name in args.devices:
        device = torch.device(device_name)
        if device.type == "cuda":
            free, _ = torch.cuda.mem_get_info(device)
            if data["memory"]["estimated_device_array_bytes"] > .75*free:
                raise ValueError("benchmark device estimate exceeds 75% of currently free CUDA memory")
        _sync(device)
        start = time.perf_counter()
        inputs = {key: data[key].to(device) for key in ("r", "G", "increments")}
        _sync(device)
        transfer_seconds = time.perf_counter()-start
        for label, dtype in (("fp64", torch.complex128), ("fp32", torch.complex64)):
            start = time.perf_counter()
            _one_run(inputs, prior, particles, dtype, device, args.dt, args.memory_limit)
            warmup_seconds = time.perf_counter()-start
            repeats = [_one_run(inputs, prior, particles, dtype, device, args.dt, args.memory_limit)
                       for _ in range(args.repeats)]
            first = repeats[0]
            if any(row["scores"] != first["scores"] for row in repeats[1:]):
                raise ValueError("same-device repeated fixed-bank scores changed")
            median = {key: statistics.median(row[key] for row in repeats)
                      for key in ("core_seconds", "finish_seconds", "resident_total_seconds")}
            median["single_use_seconds"] = data["preparation_seconds"]+transfer_seconds+median["resident_total_seconds"]
            case["runs"].append({"device": device_name, "precision": label,
                "input_sha256": data["input_sha256"], "transfer_seconds": transfer_seconds,
                "warmup_seconds": warmup_seconds, "median": median, "repeats": repeats,
                "particle_block_updates_per_core_second": args.blocks*batch*particles/median["core_seconds"]})
        del inputs
    reference = next((row for row in case["runs"] if row["device"] == "cpu" and row["precision"] == "fp64"), case["runs"][0])
    for row in case["runs"]:
        case["comparisons"].append({"reference": reference["device"]+":"+reference["precision"],
            "candidate": row["device"]+":"+row["precision"],
            **compare(reference["repeats"][0]["scores"], row["repeats"][0]["scores"], args.gate_log_lr)})
        cpu = next((r for r in case["runs"] if r["device"] == "cpu" and r["precision"] == row["precision"]), None)
        if row["device"] != "cpu" and cpu is not None:
            case["speedups"].append({"device": row["device"], "precision": row["precision"],
                **{key: cpu["median"][key]/row["median"][key] for key in
                   ("core_seconds", "resident_total_seconds", "single_use_seconds")}})
    return case


def _positive_csv(text):
    try:
        values = [int(value) for value in text.split(",")]
    except ValueError as error:
        raise argparse.ArgumentTypeError("expected comma-separated positive integers") from error
    if not values or any(value < 1 for value in values) or len(set(values)) != len(values):
        raise argparse.ArgumentTypeError("expected distinct positive integers")
    return values


def parser():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--batches", type=_positive_csv, default=[8])
    p.add_argument("--particles", type=_positive_csv, default=[32])
    p.add_argument("--blocks", type=int, default=8)
    p.add_argument("--taps", type=int, default=2)
    p.add_argument("--devices", default="cpu", help="cpu or explicit comma-separated cpu,cuda:0[,cuda:1]")
    p.add_argument("--threads", type=int, default=2)
    p.add_argument("--repeats", type=int, default=2)
    p.add_argument("--seed", type=int, default=16841)
    p.add_argument("--dt", type=float, default=1.)
    p.add_argument("--phase-sigma", type=float, default=.03)
    p.add_argument("--frequency-bound", type=float, default=.005)
    p.add_argument("--coherence", type=float, default=128.)
    p.add_argument("--gate-log-lr", type=float, default=math.log(1e10), help="numerical comparison gate, not calibrated admission")
    p.add_argument("--memory-mib", type=float, default=1024.)
    p.add_argument("--output", type=Path, help="fresh JSON file; existing files are never overwritten")
    return p


def main(argv=None):
    p = parser()
    args = p.parse_args(argv)
    args.devices = args.devices.split(",")
    if not 1 <= args.taps <= 16 or args.blocks < 1 or args.threads < 1 or args.repeats < 1:
        p.error("taps must be1..16; blocks, threads and repeats must be positive")
    if not 0 <= args.seed < 2**63 or max(args.particles) > 1048576:
        p.error("seed or particle count outside supported range")
    if not all(math.isfinite(v) and v >= 0 for v in (args.dt, args.phase_sigma, args.frequency_bound)):
        p.error("time and phase/frequency parameters must be finite and nonnegative")
    if not math.isfinite(args.coherence) or args.coherence <= 0 or not math.isfinite(args.gate_log_lr):
        p.error("coherence must be finite and positive and gate finite")
    if not math.isfinite(args.memory_mib) or args.memory_mib < 1:
        p.error("memory budget must be at least1MiB")
    if len(set(args.devices)) != len(args.devices) or any(name != "cpu" and not (name.startswith("cuda:") and name[5:].isdigit()) for name in args.devices):
        p.error("devices must be distinct cpu or cuda:index names")
    if args.output and args.output.exists():
        p.error("output already exists; choose a fresh path")
    args.memory_limit = int(args.memory_mib*2**20)
    torch.set_num_threads(args.threads)
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    start = time.perf_counter()
    devices = {"cpu": {"threads": torch.get_num_threads()}} if "cpu" in args.devices else {}
    for name in args.devices:
        if name.startswith("cuda:"):
            index = int(name[5:])
            if not torch.cuda.is_available() or index >= torch.cuda.device_count():
                p.error("requested CUDA device is unavailable; no CPU fallback")
            props = torch.cuda.get_device_properties(index)
            devices[name] = {"name": props.name, "total_memory_bytes": props.total_memory,
                             "capability": [props.major, props.minor]}
            _sync(torch.device(name))
    initialization_seconds = time.perf_counter()-start
    files = (Path(__file__), Path(__file__).with_name("triad_rbpf.py"), Path(__file__).with_name("triad_gaussian_state.py"))
    report = {"schema": "triad-rbpf-synthetic-benchmark-v1", "scope": __doc__,
        "environment": {"python": platform.python_version(), "torch": str(torch.__version__),
                        "cuda_runtime": torch.version.cuda, "machine": platform.machine(), "devices": devices},
        "arithmetic": {"bulk": ["complex128", "complex64"], "covariance_preparation": "complex128",
                       "phase_and_log_reductions": "float64", "tf32_allowed": False,
                       "check_inputs": "CPU inputs validated; core checks disabled; finish and phase-hook checks retained"},
        "sources": {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in files},
        "configuration": {key: getattr(args, key) for key in ("batches", "particles", "blocks", "taps", "devices", "threads", "repeats", "seed", "dt", "phase_sigma", "frequency_bound", "coherence", "gate_log_lr", "memory_mib")},
        "device_initialization_seconds": initialization_seconds,
        "timing_scope": "median single-use excludes warmup and device initialization; both are reported; resident includes finish; preparation and input transfer are charged once per single-use alternative",
        "memory_scope": "preflight covers host/device arrays; CUDA peaks measured; allocator/solver overhead excluded from preflight; CPU RSS unmeasured",
        "cases": []}
    start = time.perf_counter()
    for batch in args.batches:
        for particles in args.particles:
            print(f"RBPF benchmark batch={batch} particles={particles} blocks={args.blocks}", file=sys.stderr, flush=True)
            report["cases"].append(benchmark_case(args, batch, particles))
    report["campaign_seconds_including_warmups"] = time.perf_counter()-start
    rendered = json.dumps(report, indent=2, allow_nan=False)+"\n"
    if args.output:
        with args.output.open("x", encoding="utf-8") as stream:
            stream.write(rendered)
    else:
        print(rendered, end="")
    return report


if __name__ == "__main__":
    main()
