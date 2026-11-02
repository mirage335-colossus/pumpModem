#!/usr/bin/env python3
"""Derive a bounded sequential CPU/GPU benchmark from frozen screen jobs."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


def set_option(argv: list[str], option: str, value: str) -> None:
    try:
        i = argv.index(option)
    except ValueError as exc:
        raise ValueError(f"template is missing required option {option}") from exc
    if i + 1 >= len(argv):
        raise ValueError(f"template has no value for {option}")
    argv[i + 1] = value


def make_job(template: dict[str, Any], *, name: str, device: str, seed: int,
             trials: int, output: Path, timeout: int, python: str) -> dict[str, Any]:
    argv = list(template["argv"])
    argv[0] = python
    if "--output" not in argv:
        raise ValueError(f"{template['name']} lacks --output")
    inputs = argv[2:argv.index("--output")]
    if len(inputs) != 2:
        raise ValueError(f"{template['name']} expected exactly two fixed input paths, got {len(inputs)}")
    set_option(argv, "--noise-trials", str(trials))
    set_option(argv, "--trial-batch", str(trials))
    set_option(argv, "--device", device)
    set_option(argv, "--precision", "fp32")
    set_option(argv, "--threads", "2")
    set_option(argv, "--memory-gib", "12")
    set_option(argv, "--particles", "128")
    set_option(argv, "--seed", str(seed))
    argv[argv.index("--output") + 1] = str(output)
    if "gaussian-window-ucb" in argv:
        set_option(argv, "--window-blocks", "32")
        set_option(argv, "--window-doppler-width", "0.001")
    if "--cn0-grid=-28" not in argv:
        raise ValueError(f"{template['name']} is not the frozen C/N0=-28 dB-Hz case")
    return {"name": name, "timeout_seconds": timeout, "argv": argv}


def file_sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=Path("/workspace/triad"))
    ap.add_argument("--python", default="/workspace/triad-env/bin/python")
    ap.add_argument("--source-plan", type=Path)
    ap.add_argument("--output", type=Path)
    args = ap.parse_args()
    root = args.root.resolve()
    here = Path(__file__).resolve().parent
    src = (args.source_plan or root / "docs/validation-data/triad-v3/2026-11-02-spread/reproduction/score-plan.json").resolve()
    output = (args.output or here / "benchmark-plan.json").resolve()
    if output.exists():
        raise SystemExit(f"refusing to overwrite existing plan: {output}")
    screen = json.loads(src.read_text())
    by_name = {j["name"]: j for j in screen["jobs"]}
    templates = {"cfo": by_name["g001-cfo-cpu"], "window": by_name["g001-window-cpu"]}
    benchmark_dir = root / "local-evidence/spread/benchmark-1024-seed231104"
    jobs = [
        make_job(templates["cfo"], name="cfo-cpu-fp32", device="cpu", seed=231104,
                 trials=1024, output=benchmark_dir / "cfo-cpu.json", timeout=300, python=args.python),
        make_job(templates["cfo"], name="cfo-cuda-fp32", device="cuda:0", seed=231104,
                 trials=1024, output=benchmark_dir / "cfo-cuda.json", timeout=300, python=args.python),
        make_job(templates["window"], name="window-cpu-fp32", device="cpu", seed=231104,
                 trials=1024, output=benchmark_dir / "window-cpu.json", timeout=90, python=args.python),
        make_job(templates["window"], name="window-cuda-fp32", device="cuda:0", seed=231104,
                 trials=1024, output=benchmark_dir / "window-cuda.json", timeout=90, python=args.python),
    ]
    plan = {
        "schema": "triad-spread-sequential-cpu-gpu-benchmark-v1",
        "scope": "Matched two-path, fixed-C/N0 conditional receiver throughput benchmark. Each method runs CPU FP32 then CUDA FP32 sequentially; no concurrent jobs. Not a receiver reliability qualification.",
        "repair_history": {
            "supersedes": "benchmark-plan-original.json",
            "original_setup_error": "KeyError: 'jobs' in reproduction/run_jobs.py; no child job was launched and no benchmark measurement was made.",
        },
        "provenance": {
            "derived_from": "score-plan.json",
            "source_plan_sha256": file_sha256(src),
            "source_template_jobs": {"cfo": "g001-cfo-cpu", "window": "g001-window-cpu"},
            "source_input_paths": jobs[0]["argv"][2:jobs[0]["argv"].index("--output")],
        },
        "workload": {
            "physical_path_count": 2,
            "cn0_db_hz": -28,
            "noise_trials_per_control_and_path": 1024,
            "trial_batch": 1024,
            "controls": ["signal", "null"],
            "methods_and_order": ["cfo-ou-ucb CPU FP32", "cfo-ou-ucb CUDA FP32", "gaussian-window-ucb CPU FP32", "gaussian-window-ucb CUDA FP32"],
            "common_seed": 231104,
            "threads": 2,
            "memory_gib_argument": 12,
            "particles": 128,
            "window_doppler_width_hz": 0.001,
            "window_blocks": 32,
            "coherence_seconds": 450.1581580785531,
            "frequency_bound_hz": 1e-6,
        },
        "feasibility_estimate": {
            "input_shape_per_path": {"blocks": 1543, "dimension": 4, "sample_rate_hz": 0.25, "block_samples": 16},
            "preflight_memory_estimate_gib_per_job": 0.94,
            "memory_estimate_scope": "triad-conditional-sweep.py array preflight formula for batch=1024; framework, CUDA context and allocator overhead are excluded. Have at least 2 GiB free device memory before launch; explicit 12 GiB/75%-free cap remains enforced by the receiver tool.",
            "prior_timing_baseline_seconds_under_concurrent_load": {
                "cfo_cpu_fp32_32_trials": 5.924,
                "cfo_cuda_fp32_32_trials": 6.995,
                "window_cpu_fp32_256_trials": 3.363,
                "window_cuda_fp32_256_trials": 3.390,
                "warning": "Existing timings are noisy concurrent-load observations, not isolated performance evidence; linear trial scaling is only a budget estimate.",
            },
            "estimated_sequential_runtime_seconds": 440,
            "job_timeout_seconds": {"cfo": 300, "window": 90},
            "maximum_sum_of_job_timeouts_seconds": 780,
            "deadline_utc": "2026-11-02T03:55:00Z",
            "recommendation": "Estimated runtime is about 7.3 minutes; the sum of hard job timeouts is 13 minutes. Launch promptly and sequentially to meet the 03:55Z deadline. If available device memory is below the preflight requirement or CPU/GPU resources are busy, defer rather than parallelize or silently shrink the matched workload.",
        },
        "execution": {
            "must_run_sequentially": True,
            "runner_worker_count": 1,
            "results_are_comparable_only_if": [
                "All four jobs complete without timeout or CUDA fallback.",
                "CPU/GPU pair manifests have identical sampled input hashes and the same seed/control/trial index schedule.",
                "Reported score/admission decisions agree within the recorded precision tolerance; analyze runtime separately from initialization and noise-draw preparation.",
                "No other workload is using the measured CPU/GPU resources during these runs.",
            ],
            "limitations": [
                "A single CPU/GPU run pair per method is a throughput sample, not a robust benchmark distribution.",
                "Job timing includes process startup and, for CUDA, context initialization; report wall time and receiver kernel/scoring time separately where available.",
                "The two fixed paths and conditional Gaussian draws do not qualify channel-population reception probability.",
            ],
        },
        "jobs": jobs,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(plan, indent=2) + "\n")
    print(json.dumps({"output": str(output), "jobs": len(jobs), "sum_timeout_seconds": 780}, indent=2))


if __name__ == "__main__":
    main()
