#!/usr/bin/env python3
"""Traceable duration forecasts for the real native triad simulation.

Short siblings execute complete streams, natural tails and final scoring. They
keep the full candidate bank. Predictions refer to complete CLI test wall time,
including startup/output, with native pipeline and receiver work also retained.
"""
from __future__ import annotations

from dataclasses import replace
import hashlib
import json
import math
from pathlib import Path
import platform
import statistics
import subprocess
import time

SCHEMA = "triad-performance-native-v4"
LEGACY_ANCHOR_SCHEMA = "triad-performance-native-v3"
# Bump the measurement contract for changes to native timed boundaries,
# complete-sibling work, the affine forecast, or qualification semantics.
MEASUREMENT_CONTRACT = "triad-native-complete-cli-affine-v1"
# Reviewed CPU002 implementation pair. Its full target, arithmetic, timing
# boundary and affine model are identical to this contract. The original
# artifact and its byte identities remain the historical proof.
LEGACY_MEASUREMENT_CONTRACTS = {
    ("complete-native-duration-siblings-v3",
     "538a03c9002d7767c61faac3ecac3ba4ac7ba1ac9600a51ca7760373fc521bbe",
     "3ad99d54b955e735f0db832bd6ac3afe91b016d993de80909827efd491174106"): MEASUREMENT_CONTRACT,
}
ROOT = Path(__file__).resolve().parents[1]
CONFIG_PATH = Path(__file__).with_name("triad-performance-native-cases.json")
STAGES = ("receiver", "generation_setup_other")
TIMED_STAGES = (*STAGES, "process_startup_output")
MINIMUM_BLOCK_SECONDS = .25
MAXIMUM_BLOCK_INVOCATIONS = 8


class NativePerformanceError(RuntimeError):
    pass


class NativeTimingModelUnresolved(NativePerformanceError):
    """Observed timing did not support a finite physical cost fit."""


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()


def file_sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for part in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(part)
    return h.hexdigest()


def scenarios(profile="ci"):
    config = json.loads(CONFIG_PATH.read_text())
    if profile not in config["profiles"]:
        raise ValueError("unknown native performance profile")
    selected = set(config["profiles"][profile])
    return [{**row, "args": config["common_args"] + row["args"],
             "maximum_extrapolation": config["maximum_extrapolation"],
             "model_relative_error_limit": config["model_relative_error_limit"]}
            for row in config["cases"] if row["id"] in selected]


def environment_identity(threads):
    from triad_perf_hardware import _cpu_limits

    model = "unknown"
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.exists():
        for line in cpuinfo.read_text().splitlines():
            if line.startswith("model name"):
                model = line.split(":", 1)[1].strip()
                break
    limits = _cpu_limits()
    return {"system": platform.system(), "release": platform.release(), "machine": platform.machine(),
            "cpu_model": model, "threads": threads,
            **{key: limits[key] for key in ("affinity_cpu_count", "affinity_cpus", "affinity_topology",
                                            "quota_cores", "quota_source")}}


def identity(binary, case, threads):
    return {"binary_sha256": file_sha(binary), "environment": environment_identity(threads),
            "case_sha256": digest(case), "adapter_sha256": file_sha(__file__),
            "adaptive_protocol_sha256": file_sha(Path(__file__).with_name("triad_perf_adaptive.py")),
            "protocol": "complete-native-duration-siblings-v4", "arithmetic": "fp64",
            "measurement_contract": MEASUREMENT_CONTRACT,
            "timing_boundary": "external complete CLI test including startup and output; stages inside native trial separately retained"}


def _argv(binary, case, seconds, seed, threads, *, trace=False, configure=False):
    if not math.isfinite(seconds) or seconds < 8 or isinstance(seed, bool) or not isinstance(seed, int):
        raise ValueError("invalid native duration or seed")
    args = [str(Path(binary).resolve()), *case["args"], "--seconds", repr(float(seconds)),
            "--seed", str(seed), "--threads", str(threads)]
    if configure:
        args += ["--mode", "configure"]
    else:
        args += ["--trials", str(case.get("batch_trials", 1))]
        if trace:
            args += ["--performance-trace", "yes"]
    return args


def _invoke(argv, deadline=None, timeout=120.):
    remaining = timeout if deadline is None else min(timeout, deadline-time.monotonic())
    if remaining <= 0:
        raise TimeoutError("native performance deadline exhausted")
    start = time.perf_counter()
    result = subprocess.run(argv, text=True, capture_output=True, timeout=remaining)
    elapsed = time.perf_counter()-start
    if result.returncode:
        raise NativePerformanceError(f"native command exited {result.returncode}: {result.stderr[-1600:]}")
    try:
        rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
    except ValueError as exc:
        raise NativePerformanceError("native command returned invalid JSON") from exc
    return rows, elapsed


def _positive(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
        raise NativePerformanceError(f"invalid native {label}")
    return value


def coverage_witnesses(row):
    """Actual completed execution/counter evidence, not source line coverage."""
    fmt = row["source_format"]
    model = row["channel_model"]
    path = model["path_evolution"]
    truth = row["path_truth_simulation_only"]
    front = row["frontend_report"]
    rows = row["capture_samples"]
    complete = row.get("complete_observation") is True and row["channel_samples"] == rows
    selective = truth["selective_perturbation"]
    columns = sum(c["tap_count"] for c in row["candidates"])
    if row["basis_values"] != rows * columns:
        raise NativePerformanceError("native basis work differs from full rows times complete candidate columns")
    if row["candidate_count"] != len(row["candidates"]):
        raise NativePerformanceError("native candidate accounting mismatch")
    if not complete or row["blocks"] < 1 or row["missing_samples"] > rows:
        raise NativePerformanceError("native run did not complete its declared stream")
    result = {
        "complete": complete,
        "encrypted": complete and fmt["encrypted"],
        "flat": complete and all(c["tap_count"] == 1 for c in row["candidates"]),
        "two_ray": complete and any(c["tap_count"] == 2 for c in row["candidates"]),
        "white_noise": complete and row["gaussian_projection_supported"] and row["complex_noise_variance"] > 0,
        "selective": complete and selective["enabled"] and selective.get("processed_rows", 0) >= rows,
        "erasure": complete and row["missing_samples"] > 0,
        "internal_burst": complete and row["burst_wait_tiles"] > 0 and fmt["tile_count"] > 1
                          and row["bit_seconds"] > fmt["tile_seconds"] * fmt["tile_count"],
        "sub_hz": complete and row["sample_rate"] < 1,
        "weak_signal": complete and row["cn0_db_hz"] <= -60,
        "null": complete and row["control"] == "null",
        "wrong_key": complete and row["control"] == "wrong-key",
        "signal_energy": complete and row["signal_energy"] > 0,
        "no_signal_energy": complete and row["signal_energy"] == 0,
        "frontend": complete and row["sampled_frontend"]["active"] and not row["analytical_null_supported"],
        "adc_clipping": complete and front["adc_clipped_components"] > 0,
        "dac_clipping": complete and front["dac_clipped_components"] > 0,
        "impulses": complete and front["impulse_events"] > 0,
        "overload": complete and front["overload_samples"] > 0,
        "ou_path": complete and path["ray_coherence_seconds"] > 0,
        "gaussian_path": complete and len(truth["gaussian_ray_teacher"]["fir_coefficients"]) > 1,
        "outage": complete and 0 <= path["outage_start_seconds"] < path["outage_end_seconds"] < row["bit_seconds"],
        "max_added": complete and fmt["added_symbols"] == 1000,
    }
    return result


def regime(row, case):
    """Duration-independent measured geometry; exact selected algorithm remains fixed."""
    fmt = {k: v for k, v in row["source_format"].items() if k != "tile_count"}
    return {"source_format": fmt, "sample_rate": row["sample_rate"],
            "candidate_bank": row["candidates"], "channel_model": row["channel_model"],
            "frontend": row["sampled_frontend"], "channel_tail_samples": row["channel_tail_samples"],
            "coherent_block_samples": row["coherent_block_samples"], "threads_used": row["threads_used"],
            "chunk_samples": int(_option(case["args"], "--chunk", 4096)),
            "control": row["control"], "cn0_db_hz": row["cn0_db_hz"],
            "workspace_bytes": row["working_bytes"], "arithmetic": "fp64"}


def _option(args, name, default):
    indices = [i for i, value in enumerate(args) if value == name]
    return args[indices[-1]+1] if indices else default


def run_native(binary, case, seconds, seed, *, threads=1, trace=False, deadline=None, timeout=120.):
    argv = _argv(binary, case, seconds, seed, threads, trace=trace)
    rows, external = _invoke(argv, deadline, timeout)
    if len(rows) != case.get("batch_trials", 1) or any(row.get("mode") != "run" for row in rows):
        raise NativePerformanceError("native trial count does not match the frozen test")
    witnesses, geometries = [], []
    for row in rows:
        for name in ("capture_samples", "end_to_end_wall_seconds", "wall_seconds", "basis_values"):
            _positive(row.get(name), name)
        witnesses.append(coverage_witnesses(row))
        geometries.append(regime(row, case))
    if any(value != geometries[0] for value in geometries[1:]):
        raise NativePerformanceError("native trial seeds changed the cost regime")
    actual = {name: all(w.get(name, False) for w in witnesses) for name in case["witnesses"]}
    if not all(actual.values()):
        raise NativePerformanceError("required native execution witnesses absent: " + ", ".join(k for k, v in actual.items() if not v))
    pipeline = math.fsum(row["end_to_end_wall_seconds"] for row in rows)
    receiver = math.fsum(row["wall_seconds"] for row in rows)
    if pipeline < receiver or external < pipeline:
        raise NativePerformanceError("native timing boundaries did not reconcile")
    return {"schema": SCHEMA, "argv": argv, "argv_sha256": digest(argv), "seed": seed,
            "requested_seconds": seconds, "batch_trials": len(rows),
            "samples_per_trial": rows[0]["capture_samples"], "total_rows": sum(row["capture_samples"] for row in rows),
            "stage_seconds": {"receiver": receiver, "generation_setup_other": pipeline-receiver,
                              "process_startup_output": external-pipeline},
            "full_test_wall_seconds": external, "native_pipeline_seconds": pipeline,
            "regime": geometries[0], "coverage": actual,
            "counter_summary": {"basis_values": sum(r["basis_values"] for r in rows),
                                "blocks": sum(r["blocks"] for r in rows),
                                "missing_samples": sum(r["missing_samples"] for r in rows),
                                "channel_samples": sum(r["channel_samples"] for r in rows)},
            "native_results": rows, "performance_only": True,
            "admission_scope": "short native decisions apply only to the actual short geometry, never the predicted full duration"}



def compare_native_outputs(reference, candidate):
    """Compare full physical work and decisions on the same paired seeds."""
    a, b = reference["native_results"], candidate["native_results"]
    if len(a) != len(b):
        raise NativePerformanceError("paired native trial counts differ")
    exact = ("capture_samples", "channel_samples", "source_samples", "candidate_count", "basis_values",
             "blocks", "missing_samples", "complete_observation", "all_samples_observed", "accepted",
             "presence", "correct", "wrong_accepted", "missed", "false_admission", "decoded_bit",
             "analytical_null_supported", "source_format", "candidates")
    maximum_error = 0.
    for old, new in zip(a, b):
        if any(old.get(key) != new.get(key) for key in exact):
            raise NativePerformanceError("paired native work, candidate geometry or discrete result differs")
        for key in ("score", "threshold", "margin", "observed_energy", "signal_energy"):
            x, y = float(old[key]), float(new[key])
            error = abs(x-y)
            if not math.isfinite(x) or not math.isfinite(y) or error > 1e-3 + 1e-6*max(abs(x), abs(y)):
                raise NativePerformanceError("paired native numerical result differs: " + key)
            maximum_error = max(maximum_error, error)
    return {"eligible": True, "maximum_absolute_error": maximum_error,
            "scope": "paired declared physical cases and numerical/discrete outcomes; not universal native sensitivity equivalence"}


def target_geometry(binary, case, *, threads=1, deadline=None, seconds=None):
    duration = case["target_seconds"] if seconds is None else seconds
    rows, elapsed = _invoke(_argv(binary, case, duration, 1, threads, configure=True), deadline, 20.)
    if len(rows) != 1 or rows[0].get("mode") != "configure":
        raise NativePerformanceError("target configuration response missing")
    return {"configuration": rows[0]["configuration"], "performance_geometry": rows[0].get("performance_geometry"),
            "configuration_wall_seconds": elapsed, "configuration_sha256": digest(rows[0])}


def target_rows_from_configuration(configuration, training):
    explicit = configuration.get("performance_geometry")
    if explicit and "capture_samples" in explicit:
        return int(explicit["capture_samples"]) * training["batch_trials"]
    # Legacy configure lacks capture rows. This fallback is scoped to the exact
    # static-clock recipes: complete source support changes while public padding
    # and tails remain fixed. Full holdouts must confirm the predicted row count.
    if training["regime"]["channel_model"].get("sample_clock_reference") != "RX0ppm; TX carries declared relative error":
        raise NativePerformanceError("legacy target geometry needs exact configure metadata for clocked paths")
    support = configuration["configuration"]["duration"]["source_support_seconds"]
    fs = training["regime"]["sample_rate"]
    old = training["native_results"][0]
    target_source = int(round(support*fs))
    return (target_source + old["capture_samples"]-old["source_samples"]) * training["batch_trials"]


def fit_duration_model(training, target_rows, *, maximum_extrapolation=8.):
    if isinstance(target_rows, bool) or not isinstance(target_rows, int) or target_rows < 1:
        raise ValueError("target_rows must be a positive integer")
    if len(training) != 2:
        raise ValueError("native duration model requires exactly two independent complete siblings")
    small, large = sorted(training, key=lambda row: row["total_rows"])
    if small["regime"] != large["regime"] or small["batch_trials"] != large["batch_trials"]:
        raise NativePerformanceError("duration siblings changed measured cost regime")
    x0, x1 = small["total_rows"], large["total_rows"]
    if not 0 < x0 < x1 <= target_rows or target_rows/x1 > maximum_extrapolation:
        raise NativePerformanceError("native resolved row geometry is duplicate, reversed or out of extrapolation bounds")
    predictions, slopes = {}, {}
    for stage in STAGES:
        a, b = small["stage_seconds"][stage], large["stage_seconds"][stage]
        slope = (b-a)/(x1-x0)
        prediction = b + slope*(target_rows-x1)
        if slope < 0 or not math.isfinite(prediction) or prediction <= 0:
            raise NativeTimingModelUnresolved("native duration fit is unstable or nonpositive: " + stage)
        slopes[stage], predictions[stage] = slope, prediction
    predictions["process_startup_output"] = statistics.mean(row["stage_seconds"]["process_startup_output"] for row in training)
    return {"schema": SCHEMA, "stage_seconds": predictions,
            "full_test_wall_seconds": math.fsum(predictions.values()), "target_total_rows": target_rows,
            "slopes_seconds_per_row": slopes, "larger_sibling_total_rows": x1,
            "target_over_larger_rows": target_rows/x1, "regime_sha256": digest(large["regime"]),
            "training_record_sha256": [digest(row) for row in training],
            "method": "anchored affine complete-duration native stage model; fixed process startup/output",
            "prediction_is_frozen_before_full_execution": True}


def validate_holdout(prediction, actual, *, maximum_relative_error=.05):
    same_geometry = (prediction["target_total_rows"] == actual["total_rows"] and
                     prediction["regime_sha256"] == digest(actual["regime"]))
    errors = {stage: abs(prediction["stage_seconds"][stage]-actual["stage_seconds"][stage])/
                    prediction["stage_seconds"][stage] for stage in STAGES}
    predicted, observed = prediction["full_test_wall_seconds"], actual["full_test_wall_seconds"]
    relative = abs(observed-predicted)/predicted
    return {"single_run_within_bound": same_geometry and relative <= maximum_relative_error,
            "geometry_matched": same_geometry, "absolute_relative_error": relative,
            "allowed_relative_error": maximum_relative_error,
            "stage_absolute_relative_errors": errors, "log_error": math.log(observed/predicted),
            "prediction_sha256": digest(prediction), "actual_record_sha256": digest(actual),
            "predicted_full_seconds": predicted, "actual_full_seconds": observed,
            "stage_validation_scope": "diagnostic only; qualification uses independent log-mean equivalence"}


def _qualification(holds, allowance, *, confidence=.90):
    from triad_perf_adaptive import qualify_log_residuals

    errors = {name: [h["validation"]["log_error"] for h in holds if h["source"] == name]
              for name in ("reference", "candidate")}
    errors["paired"] = [c - r for r, c in zip(errors["reference"], errors["candidate"], strict=True)]
    intervals = {name: qualify_log_residuals(values, maximum_relative_error=allowance, confidence=confidence)
                 for name, values in errors.items()}
    statuses = {interval["status"] for interval in intervals.values()}
    geometry_matches = all(h["validation"]["geometry_matched"] for h in holds)
    if not geometry_matches or "invalid" in statuses:
        status = "invalid"
    elif statuses == {"validated"}:
        status = "validated"
    else:
        status = "inconclusive"
    return {"status": status, "geometry_matches": geometry_matches, "log_errors": errors,
            "sample_count_per_source": len(errors["reference"]), "intervals": intervals,
            "scope": "both source means and paired ratio must establish equivalence at the same confirmation look"}


def _confirmation_seed(mode, index):
    # Full historical and fresh intermediate runs use separate roots. Training
    # starts at 1,000,000 and the discarded pilot starts at 640,000.
    return (840_000 if mode == "qualify" else 900_000) + 1000 * index


def _timing_block(invocations):
    """Average full original CLI invocations as one timing observation."""
    first = invocations[0]
    keys = ("total_rows", "batch_trials", "regime")
    if any(any(run[key] != first[key] for key in keys) for run in invocations):
        raise NativePerformanceError("timing block changed native geometry across invocations")
    if len(invocations) == 1:
        return first
    return {**{key: first[key] for key in keys},
            "stage_seconds": {stage: statistics.fmean(r["stage_seconds"][stage] for r in invocations)
                              for stage in TIMED_STAGES},
            "full_test_wall_seconds": statistics.fmean(r["full_test_wall_seconds"] for r in invocations),
            "invocations": invocations}


def _freeze_model(predictions):
    first = predictions[0]
    geometry_keys = ("target_total_rows", "regime_sha256")
    if any(any(p[key] != first[key] for key in geometry_keys) for p in predictions):
        raise NativePerformanceError("frozen prediction combines incompatible geometry")
    point = math.exp(statistics.fmean(math.log(p["full_test_wall_seconds"]) for p in predictions))
    stage_means = {stage: statistics.fmean(p["stage_seconds"][stage] for p in predictions)
                   for stage in TIMED_STAGES}
    # Report pooled additive stage shares at the same geometric-mean full-time
    # estimand used by the timing intervals and the independent holdout check.
    stage_sum = math.fsum(stage_means.values())
    return {**{key: first[key] for key in geometry_keys}, "full_test_wall_seconds": point,
            "stage_seconds": {stage: seconds * (point / stage_sum) for stage, seconds in stage_means.items()},
            "outer_training_prediction_sha256": [digest(p) for p in predictions],
            "outer_training_count": len(predictions),
            "method": "geometric mean of predeclared outer full-test forecasts; normalized pooled stage shares"}


def historical_anchor_proof(anchor, case):
    """Verify historical provenance and its original complete-run qualification."""
    if (not isinstance(anchor, dict) or anchor.get("schema") not in (SCHEMA, LEGACY_ANCHOR_SCHEMA) or
            anchor.get("case_sha256") != digest(case) or
            anchor.get("validated") is not True):
        return False
    try:
        from triad_perf_adaptive import (MODEL_CONFIRMATION_CONFIDENCE, MODEL_CONFIRMATION_LOOKS,
                                         model_confirmation_policy)

        if anchor["anchor_sha256"] != digest({k: v for k, v in anchor.items() if k != "anchor_sha256"}):
            return False
        frozen, holds = anchor["frozen_prediction"], anchor["full_holdouts"]
        historical_identities = anchor["identities"]
        allowance = float(case.get("model_relative_error_limit", .05))
        if (frozen["sha256"] != digest({k: v for k, v in frozen.items() if k != "sha256"}) or
                set(historical_identities) != {"reference", "candidate"} or
                frozen["identities"] != historical_identities or frozen["case_sha256"] != anchor["case_sha256"] or
                frozen["validation_predictions"] != frozen["predictions"] or
                frozen["model_confirmation_policy"] != model_confirmation_policy().to_dict()):
            return False
        count = len(holds) // 2
        if count not in MODEL_CONFIRMATION_LOOKS or len(holds) != 2*count:
            return False
        expected = [(source, index) for index in range(count)
                    for source in (("reference", "candidate") if index % 2 == 0 else ("candidate", "reference"))]
        if ([(h["source"], h["index"]) for h in holds] != expected or
                frozen["validation_duration_seconds"] != case["target_seconds"] or
                anchor["confirmation_stop_reason"] != "validated"):
            return False
        for hold in holds:
            prediction = frozen["validation_predictions"][hold["source"]]
            if (hold["kind"] != "qualify" or hold["frozen_prediction_sha256"] != frozen["sha256"] or
                    hold["prediction"] != prediction or
                    hold["actual"]["seed"] != _confirmation_seed("qualify", hold["index"]) or
                    hold["actual"]["requested_seconds"] != case["target_seconds"] or
                    hold["validation"] != validate_holdout(prediction, hold["actual"], maximum_relative_error=allowance)):
                return False
        expected_looks = [_qualification(holds[:2*n], allowance, confidence=MODEL_CONFIRMATION_CONFIDENCE)
                          for n in MODEL_CONFIRMATION_LOOKS if n <= count]
        qualification = expected_looks[-1]
        return (all(row["status"] == "inconclusive" for row in expected_looks[:-1]) and
                expected_looks == anchor["confirmation_looks"] and
                qualification["status"] == "validated" and anchor["qualification"] == qualification and
                anchor["log_errors"] == qualification["log_errors"])
    except (KeyError, TypeError, ValueError, ArithmeticError):
        return False


def _measurement_identity(record):
    contract = record.get("measurement_contract")
    if contract is None:
        contract = LEGACY_MEASUREMENT_CONTRACTS.get((record.get("protocol"), record.get("adapter_sha256"),
                                                     record.get("adaptive_protocol_sha256")))
    if contract != MEASUREMENT_CONTRACT:
        raise ValueError("unrecognized native measurement contract")
    return {"measurement_contract": contract,
            **{key: record[key] for key in ("binary_sha256", "case_sha256", "environment",
                                            "arithmetic", "timing_boundary")}}


def anchor_compatible(anchor, identities, case):
    """Check current applicability separately from the untouched historical proof."""
    if not historical_anchor_proof(anchor, case):
        return False
    try:
        return (set(identities) == {"reference", "candidate"} and
                all(_measurement_identity(anchor["identities"][source]) == _measurement_identity(identities[source])
                    for source in identities))
    except (AttributeError, KeyError, TypeError, ValueError):
        return False


def measure_native_case(reference_binary, candidate_binary, case, policy, *, threads=1,
                        deadline=None, validation_mode="qualify", anchor=None,
                        trace_candidate=False, freeze_callback=None):
    """Collect paired full-time predictions and independently test one frozen fit.

    `freeze_callback` persists the pooled prediction before independent holds.
    Independent confirmation pairs accumulate at predeclared looks. Routine
    mode checks one full endpoint against a compatible historical anchor.
    Absent anchors leave abbreviated forecasts unqualified.
    """
    from triad_perf_adaptive import (MODEL_CONFIRMATION_CONFIDENCE, MODEL_CONFIRMATION_LOOKS,
                                     model_confirmation_policy, qualification_policy, summarize)
    if validation_mode not in ("qualify", "routine"):
        raise ValueError("unknown native anchor validation mode")
    if validation_mode == "qualify" and policy.stopping_rule == "decision":
        policy = qualification_policy(policy)
    if trace_candidate:
        raise ValueError("candidate-only trace clocks are for run_native diagnostics, outside paired timing")
    binaries = {"reference": Path(reference_binary), "candidate": Path(candidate_binary)}
    identities = {name: identity(binary, case, threads) for name, binary in binaries.items()}
    direct = "training_seconds" not in case
    compatible = not direct and anchor_compatible(anchor, identities, case)
    routine = compatible and validation_mode == "routine"
    validation_duration = case["target_seconds"] if not direct and (validation_mode == "qualify" or routine) else None
    configs = {n: target_geometry(b, case, threads=threads, deadline=deadline) for n, b in binaries.items()}
    records = {name: [] for name in binaries}
    values = {name: [] for name in binaries}
    models = {name: [] for name in binaries}
    holds, looks, confirmation_looks, numerical_checks = [], [], [], []
    allowance = float(case.get("model_relative_error_limit", .05))
    max_extrapolation = case.get("maximum_extrapolation", 8.)
    adjusted_policy = replace(policy, model_allowance_log=0. if direct else max(
        policy.model_allowance_log, -math.log1p(-allowance)))

    def run_pair(seconds, seed, order_index):
        order = ("reference", "candidate") if order_index % 2 == 0 else ("candidate", "reference")
        results = {name: run_native(binaries[name], case, seconds, seed, threads=threads,
                                    deadline=deadline) for name in order}
        numerical_checks.append(compare_native_outputs(results["reference"], results["candidate"]))
        return results

    block_invocations = 1
    pilot = None
    hold_pair_seconds = 0.
    if not direct:
        probes = [run_pair(duration, 640000 + 100 * j, j) for j, duration in enumerate(case["training_seconds"])]
        target_rows = {name: target_rows_from_configuration(configs[name], probes[-1][name]) for name in binaries}
        if len(set(target_rows.values())) != 1:
            raise NativePerformanceError("paired native target geometries differ")
        # Repeat the original short CLI invocation inside a single outer
        # sample, preserving per-invocation startup and the declared workload.
        # A noisy pilot chooses batch length only and never enters inference.
        pilot_seconds = {n: math.fsum(p[n]["full_test_wall_seconds"] for p in probes) for n in binaries}
        block_cost_fraction = max(sum(p[n]["total_rows"] for p in probes)/target_rows[n] for n in binaries)
        desired = math.ceil(MINIMUM_BLOCK_SECONDS / min(pilot_seconds.values()))
        work_cap = max(1, math.floor(.80 / block_cost_fraction))
        block_invocations = max(1, min(desired, work_cap, MAXIMUM_BLOCK_INVOCATIONS))
        pilot = {"source_record_sha256": [{n: digest(p[n]) for n in binaries} for p in probes],
                 "training_seconds_per_source": pilot_seconds, "inference_eligible": False,
                 "minimum_training_block_seconds_target": MINIMUM_BLOCK_SECONDS,
                 "target_rows_per_source": target_rows, "planned_fraction_of_full_row_work": block_invocations*block_cost_fraction,
                 "requested_block_invocations": desired, "row_work_cap_invocations": work_cap}
        if validation_duration is not None:
            hold_pair_seconds = math.fsum(
                probes[-1][n]["full_test_wall_seconds"] * target_rows[n]
                / probes[-1][n]["total_rows"] for n in binaries)

    sampling_started = time.monotonic()
    sampling_stop = None
    fit_instability = None
    conditional_plan = None
    readiness = None
    decision_sampling = policy.stopping_rule == "decision"
    if routine:
        from triad_perf_conditional import (build_conditional_contract, check_conditional_sentinel,
                                            conditional_gate_readiness, conditional_training_confidence)

        applicability_key = digest({name: _measurement_identity(value) for name, value in identities.items()})
        historical_ids = [f"{anchor['anchor_sha256']}:{hold['actual']['seed']}"
                          for hold in anchor["full_holdouts"] if hold["source"] == "reference"]

        def build_plan(summary):
            return build_conditional_contract(anchor["log_errors"], values,
                applicability_key=applicability_key, historical_anchor_ids=historical_ids,
                training_pair_ids=[f"native-training:{1_000_000 + 1000*i}" for i in range(len(values["reference"]))],
                timing_confidence=conditional_training_confidence(adjusted_policy, summary))

    reserve_count = 1 if routine else MODEL_CONFIRMATION_LOOKS[0]
    confirmation_plan = ({"initial_pair_wall_estimate_seconds": hold_pair_seconds, "reserved_full_pairs": reserve_count,
                          "scope": "reserve one independent full endpoint" if routine else
                                   "reserve first confirmation look; check measured cost before later looks"}
                         if validation_duration is not None else None)
    for index in range(policy.looks[-1]):
        seed = 1_000_000 + 1000*index
        if direct:
            pair = run_pair(case["target_seconds"], seed, index)
            for name, record in pair.items():
                records[name].append(record)
                values[name].append(record["full_test_wall_seconds"])
        else:
            pairs = [[], []]
            for invocation in range(block_invocations):
                order = (0, 1) if (index+invocation) % 2 == 0 else (1, 0)
                for j in order:
                    pairs[j].append(run_pair(case["training_seconds"][j], seed + 100*invocation + 10*j,
                                             index+invocation+j))
            sibling_pairs = {name: [_timing_block([pair[name] for pair in group]) for group in pairs]
                             for name in binaries}
            for name, siblings in sibling_pairs.items():
                records[name].append(siblings)

            try:
                fitted = {name: fit_duration_model(siblings,
                            target_rows_from_configuration(configs[name], pairs[-1][0][name]),
                            maximum_extrapolation=max_extrapolation) for name, siblings in sibling_pairs.items()}
            except NativeTimingModelUnresolved as error:
                fit_instability = {"reason": str(error), "outer_index": index}
                sampling_stop = "unstable_duration_fit"
                break
            for name, model in fitted.items():
                models[name].append(model)
                values[name].append(model["full_test_wall_seconds"])
        count = index+1
        if count in policy.looks:
            summary = summarize(values["reference"], values["candidate"], adjusted_policy,
                                model_validation="direct_full" if direct else "unvalidated")
            looks.append(summary)
            sampling_stop = summary["stop_reason"]
            if routine and decision_sampling:
                conditional_plan = build_plan(summary)
                readiness = conditional_gate_readiness(conditional_plan,
                    summary["paired_candidate_over_reference"]["timing_mean_confidence_interval_seconds"],
                    slowdown_threshold=policy.slowdown_threshold)
                summary["conditional_readiness"] = readiness
                if readiness["sentinel_feasible"] and readiness["provisional_gate"] in ("pass", "performance_regression"):
                    sampling_stop = "conditional_decision_ready_for_endpoint"
                    break
                if readiness["provisional_gate"] == "unvalidated":
                    sampling_stop = "historical_contract_unavailable"
                    break
            if sampling_stop != "continue":
                break
            if not direct and validation_duration is None:
                sampling_stop = "historical_anchor_unavailable"
                break
            if (not direct and not decision_sampling and summary["paired_candidate_over_reference"]["model_adjusted_gate_interval"][0]
                    > policy.slowdown_threshold):
                sampling_stop = "freeze_for_potential_regression_validation"
                break
            validation_reserve = reserve_count * hold_pair_seconds
            projected_seconds = ((summary["next_look"]-count) * (time.monotonic()-sampling_started)/count
                                 + validation_reserve)
            if deadline is not None and time.monotonic() + 1.25*projected_seconds >= deadline:
                sampling_stop = "budget_limit"
                break

    frozen = None
    qualification = None
    confirmation_stop = None
    conditional_result = None
    conditional_bounds = None
    if not direct and fit_instability is None:
        prediction = {n: _freeze_model(models[n]) for n in binaries}
        frozen = {"index": len(values["reference"])-1, "identities": identities, "case_sha256": digest(case),
                  "predictions": prediction, "validation_predictions": prediction,
                  "validation_duration_seconds": validation_duration,
                  "model_confirmation_policy": None if routine else model_confirmation_policy().to_dict(),
                  "validation_plan": confirmation_plan,
                  "frozen_at_unix_seconds": time.time()}
        if routine:
            if conditional_plan is None:
                conditional_plan = build_plan(looks[-1])
            frozen["historical_anchor_sha256"] = anchor["anchor_sha256"]
            frozen["conditional_model"] = conditional_plan
            if readiness is not None:
                frozen["conditional_readiness"] = readiness
        frozen["sha256"] = digest(frozen)
        if freeze_callback:
            freeze_callback(frozen)

        def hold_pair(index):
            actuals = run_pair(validation_duration, _confirmation_seed(validation_mode, index), index)
            for name, actual in actuals.items():
                predicted = prediction[name]
                check = validate_holdout(predicted, actual, maximum_relative_error=allowance)
                holds.append({"source": name, "index": index, "kind": validation_mode,
                              "frozen_prediction_sha256": frozen["sha256"],
                              "prediction": predicted, "actual": actual, "validation": check})
            return actuals

        if routine:
            if conditional_plan["status"] != "conditional_contract_ready":
                confirmation_stop = "unvalidated_request_focused_qualification"
                conditional_result = {"status": confirmation_stop,
                                      "reason": conditional_plan["status"],
                                      "focused_qualification_required": True}
            elif readiness is not None and not readiness["sentinel_feasible"]:
                confirmation_stop = "inconclusive_training_uncertainty"
                conditional_result = {"status": "inconclusive", "reason": confirmation_stop,
                                      "timing_blockers": readiness["timing_blockers"]}
            elif readiness is not None and readiness["provisional_gate"] == "inconclusive":
                confirmation_stop = "decision_inconclusive_at_frozen_look"
                conditional_result = {"status": "inconclusive", "reason": confirmation_stop}
            elif deadline is not None and time.monotonic() + 1.25*hold_pair_seconds >= deadline:
                confirmation_stop = "budget_limit"
            else:
                actuals = hold_pair(0)
                if not all(row["validation"]["geometry_matched"] for row in holds):
                    conditional_result = {"status": "unvalidated_request_focused_qualification",
                                          "reason": "full sentinel work differs from the frozen complete target",
                                          "focused_qualification_required": True}
                else:
                    conditional_result = check_conditional_sentinel(conditional_plan,
                        {name: actual["full_test_wall_seconds"] for name, actual in actuals.items()},
                        applicability_key=applicability_key,
                        sentinel_id=f"native-full-sentinel:{_confirmation_seed(validation_mode, 0)}")
                confirmation_stop = conditional_result["status"]
        elif validation_duration is not None:
            pair_wall_seconds = []
            confirmation_stop = "maximum_looks"
            for look in MODEL_CONFIRMATION_LOOKS:
                completed = len(pair_wall_seconds)
                estimated_pair_seconds = max(hold_pair_seconds, statistics.fmean(pair_wall_seconds)
                                             if pair_wall_seconds else 0.)
                if deadline is not None and time.monotonic() + 1.25*(look-completed)*estimated_pair_seconds >= deadline:
                    confirmation_stop = "budget_limit"
                    break
                for index in range(completed, look):
                    pair_started = time.monotonic()
                    hold_pair(index)
                    pair_wall_seconds.append(time.monotonic()-pair_started)
                qualification = _qualification(holds, allowance, confidence=MODEL_CONFIRMATION_CONFIDENCE)
                confirmation_looks.append(qualification)
                if qualification["status"] in ("validated", "invalid"):
                    confirmation_stop = qualification["status"]
                    break

    error_keys = ("reference", "candidate", "paired")
    evidence = {name: [] for name in error_keys}
    if qualification is not None:
        for name in error_keys:
            evidence[name].extend(qualification["log_errors"][name])
    if fit_instability is None:
        state = ("direct_full" if direct else "unvalidated" if qualification is None else
                 "unvalidated" if qualification["status"] == "inconclusive" else qualification["status"])
        final = summarize(values["reference"], values["candidate"], adjusted_policy, model_validation=state,
                          reference_model_log_errors=evidence["reference"], candidate_model_log_errors=evidence["candidate"],
                          paired_model_log_errors=evidence["paired"])
        if final["performance_gate"] != "performance_regression":
            final["stop_reason"] = sampling_stop
        if readiness is not None:
            final["conditional_readiness"] = readiness
        looks[-1] = final
    else:
        state = "invalid"
        final = {"stop_reason": sampling_stop, "model_validation": state, "performance_gate": "unvalidated",
                 "timing_precision_reached": False,
                 "paired_candidate_over_reference": {"model_adjusted_gate_interval": None},
                 "fit_instability": fit_instability}
    comparison = {"gate": final["performance_gate"],
                  "expanded_full_prediction_ratio_interval": final["paired_candidate_over_reference"]["model_adjusted_gate_interval"]}
    if conditional_result is not None and conditional_result["status"] == "historically_supported_conditional":
        from triad_perf_conditional import conditional_comparison, conditional_model_log_bounds

        conditional_bounds = conditional_model_log_bounds(conditional_plan, conditional_result)
        if conditional_bounds["eligible"]:
            state = conditional_result["status"]
            timing_ratio = final["paired_candidate_over_reference"]["timing_mean_confidence_interval_seconds"]
            adjusted = conditional_comparison(timing_ratio, conditional_bounds,
                                              slowdown_threshold=policy.slowdown_threshold)
            comparison = {**adjusted, "evidence": state,
                          "expanded_full_prediction_ratio_interval": adjusted["model_adjusted_ratio_interval"]}
            final["conditional_full_time_intervals_seconds"] = {
                name: [bound * math.exp(sign * conditional_bounds["model_log_error_bounds"][name])
                       for sign, bound in zip((-1, 1), final[name]["timing_mean_confidence_interval_seconds"], strict=True)]
                for name in binaries}
            # The ordinary timing summary retains its no-fresh-qualification
            # gate. This comparison depends on historical applicability.
            final["model_validation"] = state
            final["conditional_comparison"] = comparison
    new_anchor = anchor if compatible and validation_mode == "routine" else None
    if validation_mode == "qualify" and qualification is not None:
        new_anchor = {"schema": SCHEMA, "identities": identities, "case_sha256": digest(case),
                      "validated": state == "validated", "log_errors": qualification["log_errors"],
                      "qualification": qualification, "full_holdouts": holds, "frozen_prediction": frozen,
                      "confirmation_looks": confirmation_looks, "confirmation_stop_reason": confirmation_stop,
                      "scope": "exact binary/environment/case/protocol only; sequential mean equivalence of a frozen forecast"}
        new_anchor["anchor_sha256"] = digest(new_anchor)
    return {"id": case["id"], "adapter": "native_complete_simulation", "scope": case["scope"],
            "precision": "fp64", "prediction_valid": state in ("direct_full", "validated", "historically_supported_conditional"),
            "measurement_plan": {"direct": direct, "validation_mode": validation_mode,
                                 "compatible_historical_anchor": compatible, "case": case,
                                 "timing_block_invocations": block_invocations, "discarded_pilot": pilot,
                                 "confirmation_plan": confirmation_plan},
            "target_configuration": configs["candidate"], "reference_target_configuration": configs["reference"],
            "identities": identities,
            "raw": records, "predicted_full_seconds": values, "prediction": final,
            "outer_predictions": models,
            "comparison": comparison,
            "adaptive_looks": looks, "frozen_predictions": [frozen] if frozen is not None else [],
            "anchors": new_anchor or {}, "current_validation": {"holds": holds, "qualification": qualification,
                "confirmation_looks": confirmation_looks, "confirmation_stop_reason": confirmation_stop,
                "conditional_model": conditional_result, "conditional_model_log_bounds": conditional_bounds},
            "paired_native_numerical_checks": numerical_checks,
            "coverage": {name: True for name in case["witnesses"]},
            "limits": ["Native receiver currently executes FP64 regardless of Python precision selection",
                       "Named completed execution witnesses are not exhaustive source-line or numerical-state coverage",
                       "Finite independent full holdouts bound only the declared matched regime",
                       "Timing precision does not establish smaller model error"]}
