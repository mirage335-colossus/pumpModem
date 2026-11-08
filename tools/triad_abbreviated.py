#!/usr/bin/env python3
"""Bounded hierarchical Robust tests and capacity estimates.

Kernel mode measures short fixed-geometry units and conditional projections.
Manual-case mode uses bounded native simulation and coarse full-bit trials
with explicit work, uncertainty and stopping limits. Neither mode qualifies
the deployed receiver or runs a sustained heavy calibration campaign.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
from pathlib import Path
import statistics
import sys
import time

LOOKS = (3, 5, 9)
BATCHES = (1, 2, 4)  # Two training sizes followed by an independent holdout.
KERNELS = ("acquisition_prepare", "acquisition_score", "gaussian_update", "timing_forward")
ALPHA = .05
PRECISION = .15
MODEL_LIMIT = .30
SLOWDOWN = 1.30
SCHEMA = "triad-abbreviated-v1"


def log_interval(values, *, alpha):
    """Student interval for a log mean, conditional on independent normal logs."""
    from scipy.stats import t
    values = tuple(float(x) for x in values)
    if len(values) < 3 or any(not math.isfinite(x) or x <= 0 for x in values):
        raise ValueError("at least three finite positive outer observations required")
    if not 0 < alpha < 1:
        raise ValueError("alpha must be in (0,1)")
    logs = tuple(math.log(x) for x in values)
    center = statistics.fmean(logs)
    half = float(t.ppf(1-alpha/2, len(logs)-1))*statistics.stdev(logs)/math.sqrt(len(logs))
    return math.exp(center), [math.exp(center-half), math.exp(center+half)]


def analyze(rows, target_units, *, baseline=None):
    """Analyze a declared look; each row is one outer trial of all three sizes."""
    if len(rows) not in LOOKS:
        raise ValueError("observations must end at a predeclared look")
    if isinstance(target_units, bool) or not isinstance(target_units, int) or not 4 <= target_units <= 10**9:
        raise ValueError("target units must be an integer in [4,1000000000]")
    if any(len(row) != 3 or any(not math.isfinite(x) or x <= 0 for x in row) for row in rows):
        raise ValueError("each outer trial needs three positive batch timings")
    # Reserve all candidate/reference look pairs as well as three claim kinds.
    # A baseline may itself have stopped early; allocation covers that selection.
    # Unmade claims and early stopping never reclaim their reserved alpha.
    alpha = ALPHA/(len(KERNELS)*len(LOOKS)**2*3)
    units = [(row[0] + row[1])/3 for row in rows]
    errors = [row[2]/(4*unit) for row, unit in zip(rows, units)]
    point, interval = log_interval(units, alpha=alpha)
    _, error_interval = log_interval(errors, alpha=alpha)
    # Observed lack-of-fit is a nonshrinking floor, not a confidence bound on
    # arbitrary future regimes. Repetition cannot average this allowance away.
    error_floor = max(abs(math.log(x)) for x in errors)
    allowance = max(error_floor, *(abs(math.log(x)) for x in error_interval))
    model_supported = (error_interval[0] >= 1/(1+MODEL_LIMIT)
                       and error_interval[1] <= 1+MODEL_LIMIT
                       and error_floor <= math.log1p(MODEL_LIMIT))
    relative_width = max(interval[1]/point-1, 1-interval[0]/point)
    decision = "uncompared"
    ratio = None
    if baseline is not None:
        baseline_floor = baseline["observed_model_log_error_floor"]
        if (isinstance(baseline_floor, bool) or
            not isinstance(baseline_floor, (int, float)) or
            not math.isfinite(baseline_floor) or baseline_floor < 0):
            raise ValueError("baseline model error floor must be finite and nonnegative")
        if baseline["model_status"] not in ("abbreviated_holdout_supported", "unresolved"):
            raise ValueError("invalid baseline model status")
        reference = baseline["training_unit_seconds"]
        if (len(reference) not in LOOKS or
            any(not math.isfinite(x) or x <= 0 for x in reference)):
            raise ValueError("baseline requires finite positive outer observations at a declared look")
        # This is an independent-run comparison, not fictitious paired data.
        from scipy.stats import t
        a = [math.log(x) for x in units]
        b = [math.log(x) for x in reference]
        if len(b) not in LOOKS or any(not math.isfinite(x) for x in b):
            raise ValueError("invalid baseline outer observations")
        va, vb = statistics.variance(a)/len(a), statistics.variance(b)/len(b)
        variance = va+vb
        degrees = (variance*variance/(va*va/(len(a)-1)+vb*vb/(len(b)-1))
                   if variance else len(a)+len(b)-2)
        half = float(t.ppf(1-alpha/2, degrees))*math.sqrt(variance)
        center = statistics.fmean(a)-statistics.fmean(b)
        floor = error_floor + baseline_floor
        ratio = [math.exp(center-half-floor), math.exp(center+half+floor)]
        if not model_supported or baseline["model_status"] != "abbreviated_holdout_supported":
            decision = "unresolved_model"
        elif ratio[0] > SLOWDOWN:
            decision = "material_slowdown"
        elif ratio[1] < SLOWDOWN:
            decision = "no_material_slowdown_for_measured_kernels"
        else:
            decision = "inconclusive"
    resolved = (decision in ("material_slowdown", "no_material_slowdown_for_measured_kernels")
                if baseline is not None else model_supported and relative_width <= PRECISION)
    return {
        "outer_trials": len(rows), "batch_seconds": rows,
        "training_unit_seconds": units,
        "measured_kernel_units": sum(BATCHES)*len(rows),
        "measured_seconds": sum(sum(row) for row in rows),
        "unit_geometric_mean_seconds": point, "unit_geometric_mean_interval_seconds": interval,
        "holdout_ratio_interval": error_interval,
        "observed_model_log_error_floor": error_floor,
        "model_status": "abbreviated_holdout_supported" if model_supported else "unresolved",
        "projected_target_units": target_units,
        "projected_seconds": point*target_units if model_supported else None,
        "conditional_projection_interval_seconds":
            [target_units*interval[0]*math.exp(-allowance), target_units*interval[1]*math.exp(allowance)]
            if model_supported else None,
        "comparison": decision, "candidate_over_baseline_interval": ratio,
        "stop": resolved or len(rows) == LOOKS[-1],
        "stop_reason": "decision_or_precision" if resolved else "bounded_limit" if len(rows) == LOOKS[-1] else "next_abbreviated_look",
    }


def kernels(device):
    """Small reproducible inputs; each closure is a complete fixed-size unit."""
    import numpy as np
    import torch
    from triad_realtime_fft import FFTScreen
    from triad_gaussian_state import GaussianPrior, GaussianStateFilter
    from triad_timing_forward import SparseTimingForward, TimingState
    torch.set_num_threads(1)
    if device == "cuda" and not torch.cuda.is_available():
        raise ValueError("CUDA requested but unavailable; no implicit CPU fallback")
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.set_float32_matmul_precision("highest")
    fs, samples = 400, 200
    x = np.arange(samples)/fs
    envelope = np.sin(np.pi*np.arange(samples)/(samples-1))**2
    source = np.array([[envelope*np.exp(2j*np.pi*(4*x+8*x*x)),
                        envelope*np.exp(2j*np.pi*(-7*x-9*x*x))]], np.complex128)
    manifest = dict(sample_rate_hz=fs,
        calendar=dict(capture_samples=2000, capture_lead_seconds=1.,
                      data_sample_offset=400, arrival_prior_seconds=[-.1, .1]),
        channel=dict(spread_delay_ms=17.5, spread_doppler_hz=13.),
        format=dict(occupied_bandwidth_hz=40., fine_chip_rate_hz=30.),
        power=dict(complex_noise_variance=.1))
    def prepare():
        return FFTScreen(manifest, source, (0,), clock_ppm=(0.,), precision="fp32",
            output_sample_rate=200, pass_edge_hz=60., stop_edge_hz=90., workers=1)
    prepared = prepare()
    rng = np.random.default_rng(73019)
    raw = (rng.normal(size=2000)+1j*rng.normal(size=2000)).astype(np.complex64)
    gram = torch.eye(2, dtype=torch.complex128, device=device).expand(4, 2, 2).clone()
    correlations = torch.full((4, 2), .1+.2j, dtype=torch.complex128, device=device)
    def gaussian():
        state = GaussianStateFilter(GaussianPrior((.5, .5), .7, .1), dtype=torch.complex64)
        for _ in range(4):
            state.update(correlations, gram, .1)
        return state.finish()
    def timing():
        state = SparseTimingForward([TimingState(0, 0, 0)], model_count=2,
            maximum_rate_ppm=50, maximum_acceleration_ppm_per_second=50,
            maximum_states=16, maximum_proposals=32)
        for _ in range(4):
            state.predict(1.)
            state.observe(np.zeros((2, 2, len(state.states))))
        return state.scores()
    calls = {"acquisition_prepare": prepare,
             "acquisition_score": lambda: prepared.score(raw, [0.], top_per_bit=1),
             "gaussian_update": gaussian, "timing_forward": timing}
    inputs = hashlib.sha256(source.tobytes()+raw.tobytes()+json.dumps(manifest, sort_keys=True).encode()).hexdigest()
    geometry = {"fixture": 1, "sample_rate": fs, "capture_samples": 2000,
        "template_samples": samples, "epochs": 1, "bits": 2, "rays": 2,
        "gaussian_batch": 4, "gaussian_taps": 2, "updates_per_unit": 4,
        "precision": "fp32 bulk / canonical fp64 preparation and reductions",
        "input_sha256": inputs, "seed": 73019,
        "execution": {name: device if name == "gaussian_update" else "cpu" for name in KERNELS}}
    return calls, geometry


def source_identity():
    # Hash the importable canonical implementation conservatively; a changed
    # source hash is expected across a regression comparison, never hidden.
    paths = sorted(Path(__file__).parent.glob("triad_*.py"))
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}


def environment(device):
    import numpy as np
    import scipy
    import torch
    from threadpoolctl import threadpool_info
    cpu = platform.processor()
    if Path("/proc/cpuinfo").exists():
        cpu = next((line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines()
                    if line.startswith("model name")), cpu)
    return {"system": platform.platform(), "host": platform.node(), "machine": platform.machine(),
        "processor": cpu, "python": platform.python_version(),
        "numpy": np.__version__, "scipy": scipy.__version__, "torch": torch.__version__,
        "torch_threads": torch.get_num_threads(), "fft_workers": 1,
        "numerical_thread_pools": [{key: item.get(key) for key in
            ("user_api", "internal_api", "prefix", "num_threads", "version", "architecture")}
            for item in threadpool_info()],
        "device": device, "cpu_count": os.cpu_count(),
        "cpu_affinity": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
        "torch_build": hashlib.sha256(torch.__config__.show().encode()).hexdigest(),
        "cuda_device": torch.cuda.get_device_name() if device == "cuda" else None,
        "cuda_runtime": torch.version.cuda if device == "cuda" else None}


def validate_baseline(baseline, geometry, host):
    if baseline.get("schema") != SCHEMA or baseline.get("geometry") != geometry or baseline.get("environment") != host:
        raise ValueError("baseline must match schema, exact geometry/input, environment and device")
    if baseline.get("policy") != policy():
        raise ValueError("baseline must use the same declared inference policy")
    if set(baseline.get("kernels", {})) != set(KERNELS):
        raise ValueError("baseline must cover every declared kernel")


def policy():
    return {"looks": list(LOOKS), "batch_units": list(BATCHES), "family_alpha": ALPHA,
        "claims_per_kernel_look_pair": 3, "relative_precision": PRECISION,
        "maximum_holdout_error": MODEL_LIMIT, "material_slowdown_ratio": SLOWDOWN,
        "model": "additive cost of independent identical fixed-geometry kernel units",
        "projected_quantity": "geometric-mean unit cost multiplied by count; not arithmetic expected runtime or a future-run prediction interval",
        "assumptions": "independent stationary approximately normal outer log timings; same thermal/cache/device regime",
        "coverage": "kernel preparation/scoring, Gaussian updates, timing forward only; not complete acquisition/tracking, physical admission or sensitivity",
        "projection_limit": "changing only the count of identical independent units; not duration, state growth, geometry or end-to-end latency",
        "confidence_limit": "conditional log-mean intervals; observed model-error floor is not a future-regime bound",
        "excluded_cost": "interpreter/import/device startup; preparation is measured separately"}


def run(*, target_units=1000, device="cpu", baseline=None):
    sources = source_identity()
    started = time.perf_counter()
    calls, geometry = kernels(device)
    setup_seconds = time.perf_counter()-started
    # Load every numerical library first, then constrain all discovered pools.
    # Restoring the caller's pool limits prevents this module altering a host.
    from threadpoolctl import threadpool_limits
    with threadpool_limits(limits=1):
        return _measure(calls, geometry, sources, setup_seconds, target_units, device, baseline)


def _measure(calls, geometry, sources, setup_seconds, target_units, device, baseline):
    host = environment(device)
    if baseline is not None:
        validate_baseline(baseline, geometry, host)
    import torch
    synchronize = torch.cuda.synchronize if device == "cuda" else lambda: None
    results = {}
    for name, call in calls.items():
        call()  # One explicitly excluded warm-up, fixed before the first look.
        synchronize()
        rows = []
        for look in LOOKS:
            while len(rows) < look:
                row = [None]*3
                # Fixed cyclic order avoids always assigning thermal drift to
                # one batch size; each complete outer row is the sampling unit.
                for index in ((len(rows)+offset) % 3 for offset in range(3)):
                    synchronize()
                    start = time.perf_counter()
                    for _ in range(BATCHES[index]):
                        call()
                    synchronize()
                    row[index] = time.perf_counter()-start
                rows.append(row)
            result = analyze(rows, target_units,
                baseline=baseline["kernels"][name] if baseline else None)
            if result["stop"]:
                break
        result["warmup_units"] = 1
        results[name] = result
    comparisons = [r["comparison"] for r in results.values()]
    status = ("material_slowdown" if "material_slowdown" in comparisons else
              "no_material_slowdown_for_measured_kernels" if baseline and
              all(c == "no_material_slowdown_for_measured_kernels" for c in comparisons) else
              "inconclusive" if baseline else "abbreviated_evidence_only")
    if sources != source_identity():
        raise RuntimeError("canonical source changed during timing; discard this measurement")
    return {"schema": SCHEMA, "geometry": geometry, "environment": host,
        "sources": sources, "baseline_sources": baseline.get("sources") if baseline else None,
        "excluded_import_and_fixture_setup_seconds": setup_seconds,
        "policy": policy(), "kernels": results, "status": status}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--device", choices=("cpu", "cuda"), default="cpu")
    parser.add_argument("--target-units", type=int, default=1000,
                        help="project repeated identical units; never execute this many")
    parser.add_argument("--cases", nargs="?", const=str(Path(__file__).resolve().parents[1] / "_tests-RobustModem.md"),
                        help="run the current editable case list (default: _tests-RobustModem.md)")
    parser.add_argument("--exporter", type=Path,
                        default=Path(__file__).resolve().parents[1] / "build/dev/triad_helper_export")
    parser.add_argument("--probe-seconds", type=float, default=1.)
    parser.add_argument("--threads", type=int)
    parser.add_argument("--case-index", type=int, action="append",
                        help="explicit partial rerun of a current manual row; repeat to select several")
    parser.add_argument("--paired-followup", action="store_true",
                        help="optional 22 common-noise pairs with 90%% confidence when standard 95%% confirmation is too costly")
    args = parser.parse_args(argv)
    if args.cases is not None:
        if args.baseline or args.device != "cpu":
            parser.error("manual case capacity uses the local CPU; kernel baseline/CUDA options do not apply")
        from triad_case_suite import run_cases
        run_cases(executable=args.exporter, output=args.output, case_list=Path(args.cases),
                  measurement_seconds=args.probe_seconds, threads=args.threads,
                  case_indices=args.case_index, paired_followup=args.paired_followup)
        return 2  # Staged subwindows do not certify a complete receiver.
    if args.case_index is not None:
        parser.error("--case-index requires --cases")
    if args.paired_followup:
        parser.error("--paired-followup requires --cases")
    if not 4 <= args.target_units <= 10**9:
        parser.error("target units must be in [4,1000000000]")
    baseline = json.loads(args.baseline.read_text()) if args.baseline else None
    result = run(target_units=args.target_units, device=args.device, baseline=baseline)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n")
    print(json.dumps({"status": result["status"], "output": str(args.output),
        "kernels": {k: {n: v[n] for n in ("outer_trials", "model_status", "comparison", "measured_seconds")}
                    for k, v in result["kernels"].items()}}, indent=2))
    return 1 if result["status"] == "material_slowdown" else 2 if result["status"] == "inconclusive" else 0


if __name__ == "__main__":
    sys.exit(main())
