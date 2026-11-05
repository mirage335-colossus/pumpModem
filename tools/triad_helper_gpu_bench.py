#!/usr/bin/env python3
"""Paired CPU/CUDA benchmark of the sampled HelperComparison path.

This times the actual fixture reader, Helper frequency estimate, DATA FIR and
the complete original/narrowed DATA search.  It is not a simulated DSP or a
kernel-only timing.  Setup (including CUDA context creation when applicable)
is reported separately from repeated resident full comparisons.
"""
from __future__ import annotations

import argparse
import json
import statistics
import sys
import time
from pathlib import Path

import torch

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))
from triad_helper_study import HelperComparison


def _synchronize(device):
    if device.startswith("cuda"):
        torch.cuda.synchronize(torch.device(device))


def _devices(value):
    devices = [part.strip() for part in value.split(",") if part.strip()]
    if not devices or len(devices) != len(set(devices)) or any(d not in ("cpu", "cuda") for d in devices):
        raise argparse.ArgumentTypeError("devices must be a unique comma-separated subset of cpu,cuda")
    return devices


def _outcome(result):
    baseline, assisted = result["baseline"], result["assisted"]
    return {
        "baseline_bit": baseline["bit"],
        "assisted_bit": assisted["bit"],
        "baseline_score": baseline["score"],
        "assisted_score": assisted["score"],
        "baseline_per_bit_scores": baseline["bit_log_likelihoods"],
        "assisted_per_bit_scores": assisted["bit_log_likelihoods"],
        "baseline_winning_epoch_offset_seconds": baseline["best_epoch_offset_seconds"],
        "assisted_winning_epoch_offset_seconds": assisted["best_epoch_offset_seconds"],
        "baseline_winning_timing_cfo": baseline["best_group_public_parameters"],
        "assisted_winning_timing_cfo": assisted["best_group_public_parameters"],
        "baseline_data_score_seconds": baseline["timing"]["total_score_prepared_seconds"],
        "assisted_data_score_seconds": assisted["timing"]["total_score_prepared_seconds"],
        "baseline_data_timing": baseline["timing"],
        "assisted_data_timing": assisted["timing"],
        "arithmetic": baseline["arithmetic"],
        "baseline_bank": baseline["geometry"]["frequency_offsets_hz"],
        "assisted_bank": assisted["geometry"]["frequency_offsets_hz"],
        "narrowed_bank": result["assistance"]["selected_frequencies"],
    }


def _parity(reference, candidate):
    a, b = _outcome(reference), _outcome(candidate)
    import numpy as np
    return {
        "baseline_bit_equal": a["baseline_bit"] == b["baseline_bit"],
        "assisted_bit_equal": a["assisted_bit"] == b["assisted_bit"],
        "baseline_bank_equal": a["baseline_bank"] == b["baseline_bank"],
        "assisted_bank_equal": a["assisted_bank"] == b["assisted_bank"],
        "narrowed_bank_equal": a["narrowed_bank"] == b["narrowed_bank"],
        "baseline_winning_epoch_equal": a["baseline_winning_epoch_offset_seconds"] == b["baseline_winning_epoch_offset_seconds"],
        "assisted_winning_epoch_equal": a["assisted_winning_epoch_offset_seconds"] == b["assisted_winning_epoch_offset_seconds"],
        "baseline_winning_timing_cfo_equal": a["baseline_winning_timing_cfo"] == b["baseline_winning_timing_cfo"],
        "assisted_winning_timing_cfo_equal": a["assisted_winning_timing_cfo"] == b["assisted_winning_timing_cfo"],
        "baseline_score_abs_delta": abs(a["baseline_score"] - b["baseline_score"]),
        "assisted_score_abs_delta": abs(a["assisted_score"] - b["assisted_score"]),
        "baseline_per_bit_score_deltas_cpu_minus_cuda": [x - y for x, y in zip(a["baseline_per_bit_scores"], b["baseline_per_bit_scores"])],
        "assisted_per_bit_score_deltas_cpu_minus_cuda": [x - y for x, y in zip(a["assisted_per_bit_scores"], b["assisted_per_bit_scores"])],
        "baseline_data_score_seconds": {"cpu": a["baseline_data_score_seconds"], "cuda": b["baseline_data_score_seconds"]},
        "assisted_data_score_seconds": {"cpu": a["assisted_data_score_seconds"], "cuda": b["assisted_data_score_seconds"]},
        "baseline_data_cuda_over_cpu_speedup": a["baseline_data_score_seconds"] / b["baseline_data_score_seconds"],
        "assisted_data_cuda_over_cpu_speedup": a["assisted_data_score_seconds"] / b["assisted_data_score_seconds"],
        "outcomes": {"cpu": a, "cuda": b},
        "score_deltas_finite": bool(np.isfinite([a["baseline_score"] - b["baseline_score"],
                                                  a["assisted_score"] - b["assisted_score"],
                                                  *[x - y for x, y in zip(a["baseline_per_bit_scores"], b["baseline_per_bit_scores"])],
                                                  *[x - y for x, y in zip(a["assisted_per_bit_scores"], b["assisted_per_bit_scores"])] ]).all()),
    }


def _summary(values):
    return {"median_seconds": statistics.median(values), "min_seconds": min(values),
            "max_seconds": max(values), "samples_seconds": values}


def _timing_ratios(cpu_timing, cuda_timing):
    """Return CPU/CUDA ratios for matching scalar timing fields."""
    ratios = {}
    for key in sorted(set(cpu_timing) & set(cuda_timing)):
        cpu_value, cuda_value = cpu_timing[key], cuda_timing[key]
        if (isinstance(cpu_value, (int, float)) and not isinstance(cpu_value, bool) and
                isinstance(cuda_value, (int, float)) and not isinstance(cuda_value, bool)):
            ratios[key] = {"cpu_seconds": cpu_value, "cuda_seconds": cuda_value,
                           "cuda_over_cpu_speedup": cpu_value / cuda_value if cuda_value > 0 else None}
    return ratios


def run(args, comparison_factory=HelperComparison):
    if args.output.exists():
        raise ValueError("output already exists; choose a fresh file")
    if args.repeats not in (3, 5):
        raise ValueError("--repeats must be 3 or 5")
    if args.cpu_threads < 1:
        raise ValueError("--cpu-threads must be positive")
    if args.frequency_batch_size < 1:
        raise ValueError("--frequency-batch-size must be positive")
    if args.max_cached_hypotheses < 1:
        raise ValueError("--max-cached-hypotheses must be positive")
    if args.memory_limit_bytes < 1:
        raise ValueError("--memory-limit-bytes must be positive")
    if "cuda" in args.devices and not torch.cuda.is_available():
        raise ValueError("CUDA requested but unavailable in this PyTorch runtime")
    torch.set_num_threads(args.cpu_threads)
    # Explicit strict FP32 policy: this benchmark does not opt into TF32.
    if hasattr(torch.backends, "cuda") and hasattr(torch.backends.cuda, "matmul"):
        torch.backends.cuda.matmul.allow_tf32 = False
    if hasattr(torch.backends, "cudnn"):
        torch.backends.cudnn.allow_tf32 = False

    comparisons, setup = {}, {}
    for device in args.devices:
        before = time.perf_counter()
        comparisons[device] = comparison_factory(args.fixture, args.templates,
                                                  device=device, precision="fp32",
                                                  fft_backend=args.fft_backend,
                                                  frontend_profile=args.frontend_profile,
                                                  frequency_batch_size=args.frequency_batch_size,
                                                  max_cached_hypotheses=args.max_cached_hypotheses,
                                                  memory_limit_bytes=args.memory_limit_bytes)
        _synchronize(device)
        setup[device] = {"wall_seconds_including_context_and_receiver_setup": time.perf_counter() - before,
                         "study_reported_preparation_seconds": comparisons[device].preparation_seconds}

    runs = {device: [] for device in args.devices}
    for repeat in range(args.repeats):
        order = list(args.devices)
        if repeat % 2:
            order.reverse()
        for device in order:
            _synchronize(device)
            started = time.perf_counter()
            result = comparisons[device].score(args.fixture)
            _synchronize(device)
            elapsed = time.perf_counter() - started
            runs[device].append({"repeat": repeat + 1, "order": order, "wall_seconds": elapsed,
                                 "study_timing": result["timing"], "outcome": _outcome(result),
                                 "procedure_id": result["procedure_id"],
                                 "input_sha256": result["input_sha256"],
                                 "template_sha256": result["template_sha256"],
                                 "source_hashes": result["source_hashes"]})

    paired = []
    if "cpu" in runs and "cuda" in runs:
        for i, (cpu, cuda) in enumerate(zip(runs["cpu"], runs["cuda"])):
            if (cpu["input_sha256"], cpu["template_sha256"], cpu["source_hashes"]) != \
                    (cuda["input_sha256"], cuda["template_sha256"], cuda["source_hashes"]):
                raise ValueError("CPU and CUDA runs did not use identical input, template and source bytes")
            paired.append({"repeat": i + 1, "execution_order": cpu["order"],
                           "cpu_over_cuda_speedup": cpu["wall_seconds"] / cuda["wall_seconds"],
                           "cpu_seconds": cpu["wall_seconds"], "cuda_seconds": cuda["wall_seconds"],
                           "baseline_data_stage_ratios": _timing_ratios(
                               cpu["outcome"]["baseline_data_timing"], cuda["outcome"]["baseline_data_timing"]),
                           "assisted_data_stage_ratios": _timing_ratios(
                               cpu["outcome"]["assisted_data_timing"], cuda["outcome"]["assisted_data_timing"]),
                           "parity": _parity(_result_view(cpu), _result_view(cuda))})
    source_hashes = next(iter(runs.values()))[0]["source_hashes"]
    input_hash = next(iter(runs.values()))[0]["input_sha256"]
    template_hash = next(iter(runs.values()))[0]["template_sha256"]
    report = {
        "schema": "triad-helper-gpu-benchmark-v1",
        "scope": "actual sampled HelperComparison; full fixture score including Helper/FIR/preparation/transfers/scoring/finalization",
        "configuration": {"fixture": str(args.fixture), "templates": str(args.templates or args.fixture),
                          "devices": args.devices, "precision": "fp32", "tf32_enabled": False,
                          "cpu_threads": args.cpu_threads, "repeats": args.repeats,
                          "fft_backend": args.fft_backend,
                          "frontend_profile": args.frontend_profile,
                          "frequency_batch_size": args.frequency_batch_size,
                          "max_cached_hypotheses": args.max_cached_hypotheses,
                          "memory_limit_bytes": args.memory_limit_bytes,
                          "precision_scope": "strict FP32 requested for bulk backend work; documented higher-precision Helper/refinement stages remain unchanged; per-run arithmetic is recorded"},
        "runtime": {"torch_version": torch.__version__, "cuda_runtime_version": torch.version.cuda,
                    "cuda_available": torch.cuda.is_available(),
                    "cuda_device": torch.cuda.get_device_name(0) if "cuda" in args.devices else None},
        "identity": {"input_sha256": input_hash, "template_sha256": template_hash,
                     "source_hashes": source_hashes,
                     "receiver_configuration": {"fft_backend": args.fft_backend,
                                                 "frontend_profile": args.frontend_profile,
                                                 "frequency_batch_size": args.frequency_batch_size,
                                                 "max_cached_hypotheses": args.max_cached_hypotheses,
                                                 "memory_limit_bytes": args.memory_limit_bytes}},
        "setup": setup,
        "devices": {d: {"runs": runs[d], "cold_full_comparison_seconds": runs[d][0]["wall_seconds"],
                        "resident_wall_summary": _summary([r["wall_seconds"] for r in runs[d][1:]]),
                        "all_wall_summary": _summary([r["wall_seconds"] for r in runs[d]])}
                    for d in args.devices},
        "paired_cpu_cuda": {"pairs": paired,
                             "median_cpu_over_cuda_speedup": statistics.median(
                                 [p["cpu_over_cuda_speedup"] for p in paired]) if paired else None,
                             "min_cpu_over_cuda_speedup": min([p["cpu_over_cuda_speedup"] for p in paired]) if paired else None,
                             "max_cpu_over_cuda_speedup": max([p["cpu_over_cuda_speedup"] for p in paired]) if paired else None,
                             "interval_claim": None},
        "timing_limits": ["setup is measured in-process and includes available CUDA context creation; process launch is not isolated",
                           "full wall timing synchronizes CUDA at both boundaries and includes fixture memmap opening and all score work",
                           "three or five paired replays are descriptive; no confidence interval is claimed",
                           "score and winning-parameter parity are diagnostics, not an admission gate or gate qualification"],
    }
    return report


def _result_view(row):
    """Rebuild the comparison subset retained in a benchmark run."""
    o = row["outcome"]
    return {"baseline": {"bit": o["baseline_bit"], "score": o["baseline_score"],
                         "bit_log_likelihoods": o["baseline_per_bit_scores"],
                         "best_epoch_offset_seconds": o["baseline_winning_epoch_offset_seconds"],
                         "best_group_public_parameters": o["baseline_winning_timing_cfo"],
                         "timing": o["baseline_data_timing"],
                         "arithmetic": o["arithmetic"],
                         "geometry": {"frequency_offsets_hz": o["baseline_bank"]}},
            "assisted": {"bit": o["assisted_bit"], "score": o["assisted_score"],
                         "bit_log_likelihoods": o["assisted_per_bit_scores"],
                         "best_epoch_offset_seconds": o["assisted_winning_epoch_offset_seconds"],
                         "best_group_public_parameters": o["assisted_winning_timing_cfo"],
                         "timing": o["assisted_data_timing"],
                         "geometry": {"frequency_offsets_hz": o["assisted_bank"]}},
            "assistance": {"selected_frequencies": o["narrowed_bank"]}}


def main(argv=None, comparison_factory=HelperComparison):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", required=True, type=Path)
    parser.add_argument("--templates", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--repeats", type=int, choices=(3, 5), default=3)
    parser.add_argument("--cpu-threads", type=int, default=4)
    parser.add_argument("--devices", type=_devices, default=["cpu", "cuda"])
    parser.add_argument("--fft-backend", choices=("legacy", "batched"), default="legacy")
    parser.add_argument("--frontend-profile", choices=("legacy", "wide"), default="legacy")
    parser.add_argument("--frequency-batch-size", "--batch-size", dest="frequency_batch_size",
                        type=int, default=8)
    parser.add_argument("--max-cached-hypotheses", type=int, default=1)
    parser.add_argument("--memory-limit-bytes", type=int, default=2 * 1024**3)
    args = parser.parse_args(argv)
    try:
        report = run(args, comparison_factory)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
        return report
    except (ValueError, RuntimeError, OSError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
