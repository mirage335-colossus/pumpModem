#!/usr/bin/env python3
"""Fixed prospective FFT timing diagnostic; never creates qualification anchors.

Four counterbalanced training blocks freeze both an affine and a component-cost
forecast before one full ABBA endpoint block. This is a bounded diagnostic, not
an adaptive test or a replacement for independent model qualification.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import statistics
import sys
import time

from triad_perf_fft_trace import GEOMETRY, _digest, _fit, _identity, _positive

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "triad-performance-fft-prospective-diagnostic-v2"
BASELINE = "242ea15fa032ee117b9113a7f3cd07cc6e9f0254"
SOURCES = ("reference", "candidate")
LOOP_STAGES = ("observation_fft_seconds", "direct_correlation_refinement_seconds",
               "residual_and_likelihood_seconds")


def _write(path, value, *, exclusive=False):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(value, indent=2, allow_nan=False) + "\n"
    if exclusive:
        with path.open("x") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
    else:
        temp = path.with_name(path.name + ".writing")
        temp.write_text(payload)
        temp.replace(path)


def _source_bindings():
    names = ("triad_perf_fft_diagnostic.py", "triad_perf_fft_trace.py", "triad-performance.py",
             "triad_perf_receiver.py", "triad_perf_cases.py")
    return {name: hashlib.sha256((ROOT / "tools" / name).read_bytes()).hexdigest() for name in names}


def schedule(target, large_counts=None):
    """Fixed before timing; every source sees each bank order and its reverse."""
    if isinstance(target, bool) or not isinstance(target, int) or target < 9:
        raise ValueError("diagnostic requires at least nine CFO alternatives")
    large_counts = [math.ceil(target / 8)] if large_counts is None else list(large_counts)
    if (not large_counts or any(isinstance(c, bool) or not isinstance(c, int) or not 1 < c < target
                               for c in large_counts) or len(set(large_counts)) != len(large_counts)):
        raise ValueError("large counts must be distinct integers above one and below the full count")
    large_counts = sorted(large_counts)
    counts = [1, *large_counts]
    blocks = []
    for index in range(4):
        names = ("reference", "candidate", "candidate", "reference") if index % 2 == 0 else (
            "candidate", "reference", "reference", "candidate")
        # Each bank precedes and follows every other bank once per source and
        # block. Rotate the start across blocks to vary the middle bank too.
        offset = 0 if len(large_counts) == 1 else index % len(counts)
        order = tuple(counts[offset:] + counts[:offset])
        if index % 2:
            order = tuple(reversed(order))
        reverse = tuple(reversed(order))
        orders = (order, order, reverse, reverse)
        blocks.append([{"source": name, "counts": list(order)} for name, order in zip(names, orders)])
    return {"training_blocks": blocks, "training_counts": counts,
            "training_large_counts": large_counts, "full_target_count": target,
            "extrapolation_factors": {str(c): target/c for c in large_counts},
            "extrapolation_scope": "diagnostic only, including factors above the production eight-fold limit; no qualified production anchor or tolerance change",
            "endpoint_block": [{"source": n, "counts": [target]} for n in
                               ("reference", "candidate", "candidate", "reference")],
            "training_outer_blocks": 4, "endpoint_outer_blocks": 1,
            "endpoint_invocations_per_source": 2,
            "selection_scope": "all requested banks and both models committed before timing; one shared endpoint, no retry, post-endpoint tuning or model selection"}


def _component_model(preparation, counts, rows, target):
    """Scale instrumented per-CFO loops; retain measured residual overhead."""
    try:
        stage_values = {count: {stage: statistics.fmean(float(r["timing"][stage]) for r in rows[count])
                               for stage in LOOP_STAGES} for count in counts}
        if any(not math.isfinite(v) or v < 0 for row in stage_values.values() for v in row.values()):
            raise ValueError("instrumented score stages must be finite nonnegative seconds")
        wrapped = {count: statistics.fmean(_positive(r["wall_seconds_including_finish"], "wrapped score")
                                          for r in rows[count]) for count in counts}
        variable = {count: sum(stage_values[count].values()) for count in counts}
        residual = {count: wrapped[count] - variable[count] for count in counts}
        if any(not math.isfinite(v) or v <= 0 for v in residual.values()):
            raise ValueError("instrumented stages leave a nonpositive/inconsistent wrapped residual; no clamp")
        residual_fit = _fit(preparation, counts, [residual[c] for c in counts], target)
        scale = target/counts[-1]
        stage_prediction = {stage: scale*stage_values[counts[-1]][stage] for stage in LOOP_STAGES}
        predicted = residual_fit["predicted_full_seconds"] + sum(stage_prediction.values())
        return {"available": True, "predicted_full_seconds": predicted,
                "preparation_seconds": preparation, "residual_affine_model": residual_fit,
                "measured_stage_seconds_by_count": stage_values, "measured_residual_seconds_by_count": residual,
                "projected_stage_seconds": stage_prediction,
                "large_bank_instrumented_fraction": variable[counts[-1]]/wrapped[counts[-1]],
                "large_bank_scaling_factor": scale,
                "formula": "P + (F/m)*V_m + constrained_affine(R_1,R_m;F), R_c=wrapped_score_c-sum(disjoint_loop_timers_c)",
                "assumptions": "same full sample/lag/tile geometry; loop timings scale with CFO count; uninstrumented work retains a measured fixed and per-CFO term"}
    except (KeyError, TypeError, ValueError, ArithmeticError) as error:
        return {"available": False, "reason": str(error),
                "predicted_full_seconds": None, "scope": "affine diagnostic remains available; missing or inconsistent stage costs are never set to zero"}


def _block_forecasts(block, counts, target):
    result = {}
    for name in SOURCES:
        rows = [row for row in block if row["source"] == name]
        preparation = statistics.fmean(row["preparation"]["wall_seconds"] for row in rows)
        scores = {count: [score for row in rows for score in row["scores"] if score["cfo_count"] == count]
                  for count in counts}
        times = [statistics.fmean(score["wall_seconds_including_finish"] for score in scores[count]) for count in counts]
        result[name] = {"affine": _fit(preparation, counts, times, target),
                        "component": _component_model(preparation, counts, scores, target)}
    return result


def _pooled_forecasts(block_models):
    result = {}
    for model in ("affine", "component"):
        by_source = {}
        for name in SOURCES:
            rows = [block[name][model] for block in block_models]
            available = all(row.get("available", True) for row in rows)
            if available:
                values = [row["predicted_full_seconds"] for row in rows]
                by_source[name] = {"available": True, "predicted_full_seconds": math.exp(statistics.fmean(math.log(v) for v in values)),
                                   "block_predictions_seconds": values,
                                   "coefficient_of_variation": statistics.stdev(values)/statistics.fmean(values)}
            else:
                by_source[name] = {"available": False, "reason": "one or more training blocks lack valid component accounting"}
        result[model] = by_source
    return result


def measure_diagnostic(case, workers, precision, device, *, api, commitment_path,
                       deadline=None, source_guard=None, large_counts=None):
    """Run the predeclared four-block diagnostic on existing persistent workers."""
    if set(workers) != set(SOURCES):
        raise ValueError("exactly reference and candidate workers are required")
    plan = schedule(len(case["frequency_offsets_hz"]), large_counts)
    expected_geometry, identity = None, None
    worker_identities = copy.deepcopy({name: worker.identity for name, worker in workers.items()})
    checks = []
    started = time.perf_counter()

    def guard():
        if deadline is not None and time.monotonic() >= deadline:
            raise TimeoutError("diagnostic deadline exhausted; no automatic retry")
        if source_guard:
            source_guard()
        if {name: worker.identity for name, worker in workers.items()} != worker_identities:
            raise ValueError("worker identity changed during diagnostic")

    def run_block(entries, purpose, block_index):
        nonlocal expected_geometry, identity
        block = []
        for order, entry in enumerate(entries):
            guard()
            name = entry["source"]
            prepared = workers[name].request(api.prepare_request(case, precision, device))
            _positive(prepared["wall_seconds"], "preparation")
            geometry = {key: prepared["geometry"][key] for key in GEOMETRY}
            if expected_geometry is None:
                expected_geometry = geometry
                identity = _identity(case, workers, precision, device, geometry)
            if geometry != expected_geometry:
                raise ValueError("full FFT geometry changed across diagnostic calls")
            if prepared.get("input_hashes") != case["fixture_hashes"]:
                raise ValueError("prepared fixture hashes differ from the declared cached input")
            if prepared.get("source_hashes") != worker_identities[name]["source_hashes"]:
                raise ValueError("prepared timed-source hashes differ from worker identity")
            scores = []
            for count in entry["counts"]:
                guard()
                score = workers[name].request(api.score_request(case, count))
                _positive(score["wall_seconds_including_finish"], "wrapped score")
                if score["measured_group_count"] != count*case["configuration"]["lag_group_count"]:
                    raise ValueError("diagnostic score omitted declared CFO/lag work")
                scores.append({**score, "cfo_count": count})
            block.append({"source": name, "order": order, "preparation": prepared, "scores": scores})
        for count in entries[0]["counts"]:
            matched = {name: [score for row in block if row["source"] == name
                             for score in row["scores"] if score["cfo_count"] == count] for name in SOURCES}
            if any(len(rows) != 2 for rows in matched.values()):
                raise ValueError("each diagnostic block must retain two visits per source and count")
            for visit in range(2):
                agreement = api.numeric_agreement(matched["reference"][visit], matched["candidate"][visit])
                if not agreement["eligible"] or matched["reference"][visit]["geometry"] != matched["candidate"][visit]["geometry"]:
                    raise ValueError("diagnostic full-work numerical/rank/gate/geometry mismatch")
                checks.append({"purpose": purpose, "block": block_index, "count": count, "visit": visit,
                               "agreement": agreement})
        guard()
        return block

    training = [run_block(entries, "training", i) for i, entries in enumerate(plan["training_blocks"])]
    models_by_count = {str(count): [_block_forecasts(block, [1, count], plan["full_target_count"])
                                   for block in training] for count in plan["training_large_counts"]}
    forecasts_by_count = {count: _pooled_forecasts(models) for count, models in models_by_count.items()}
    attributed_costs = {}
    for count in plan["training_large_counts"]:
        attributed_costs[str(count)] = {name: sum(row["preparation"]["wall_seconds"] + sum(
            score["wall_seconds_including_finish"] for score in row["scores"] if score["cfo_count"] in (1, count))
            for block in training for row in block if row["source"] == name) for name in SOURCES}
    only = str(plan["training_large_counts"][0]) if len(plan["training_large_counts"]) == 1 else None
    frozen = {"schema": SCHEMA, "plan": plan, "identity": identity, "worker_identities": worker_identities,
              "training_blocks": training, "block_models_by_large_count": models_by_count,
              "forecasts_by_large_count": forecasts_by_count,
              "block_models": models_by_count[only] if only else None,
              "forecasts": forecasts_by_count[only] if only else None,
              "attributed_training_call_seconds_by_large_count": attributed_costs,
              "attributed_cost_scope": "sum of measured preparation and selected score calls within the shared balanced blocks; excludes the other comparison bank, not an independent isolated-pipeline wall measurement",
              "prediction_frozen_before_endpoint": True, "qualification_claim": False,
              "model_selection_permitted": False}
    frozen = json.loads(json.dumps(frozen, allow_nan=False))
    frozen["sha256"] = _digest(frozen)
    _write(commitment_path, frozen, exclusive=True)
    endpoint = run_block(plan["endpoint_block"], "full_endpoint", 0)
    if json.loads(Path(commitment_path).read_text()) != frozen:
        raise ValueError("frozen diagnostic forecast changed during endpoint execution")
    actual = {name: statistics.fmean(row["preparation"]["wall_seconds"] + row["scores"][0]["wall_seconds_including_finish"]
                                   for row in endpoint if row["source"] == name) for name in SOURCES}
    comparisons_by_count = {}
    for count, by_model in forecasts_by_count.items():
        comparisons = {}
        for model, forecasts in by_model.items():
            if all(forecasts[name]["available"] for name in SOURCES):
                errors = {name: math.log(actual[name]/forecasts[name]["predicted_full_seconds"]) for name in SOURCES}
                comparisons[model] = {"available": True, "actual_block_seconds": actual,
                                      "log_actual_over_predicted": errors,
                                      "relative_actual_over_predicted_error": {name: math.expm1(v) for name, v in errors.items()},
                                      "paired_log_prediction_error": errors["candidate"]-errors["reference"]}
            else:
                comparisons[model] = {"available": False}
        comparisons_by_count[count] = comparisons
    guard()
    return {"schema": SCHEMA, "status": "diagnostic_complete", "case_id": case["id"],
            "frozen_commitment": frozen, "commitment_path": str(commitment_path),
            "endpoint_block": endpoint, "model_comparisons_by_large_count": comparisons_by_count,
            "model_comparisons": comparisons_by_count[only] if only else None,
            "numerical_checks": checks, "elapsed_seconds": time.perf_counter()-started,
            "qualification_claim": False, "regression_gate": None,
            "scope": "one independent full ABBA block; two within-block visits per source are averaged, not independent validation trials; both forecasts reported without selection"}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/dev-cli/triad_long")
    parser.add_argument("--fixture-cache", type=Path)
    parser.add_argument("--baseline", default=BASELINE)
    parser.add_argument("--case-id", default="free-crystal")
    parser.add_argument("--large-count", action="append", type=int, help="predeclare each training large bank; repeat to compare banks against one shared full endpoint; default ceil(full/8)")
    parser.add_argument("--precision", choices=("fp32", "fp64"), default="fp32")
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--budget-seconds", type=float, default=240.)
    args = parser.parse_args(argv)
    if args.threads < 1 or not math.isfinite(args.budget_seconds) or not 0 < args.budget_seconds <= 600:
        parser.error("positive threads and a budget at most 600 seconds are required")
    args.output = args.output.resolve()
    commitment = args.output.with_suffix(".commitment.json")
    work = args.output.with_suffix(".work")
    if args.output.exists() or commitment.exists() or work.exists():
        parser.error("output, commitment or work directory already exists; no implicit retry")
    work.mkdir(parents=True)
    bindings = _source_bindings()
    def guard():
        if _source_bindings() != bindings:
            raise ValueError("diagnostic harness changed during measurement")
    spec = importlib.util.spec_from_file_location("fft_diagnostic_runner", ROOT / "tools/triad-performance.py")
    api = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(api)
    from triad_perf_cases import prepare_cases
    from triad_perf_hardware import discover_hardware
    deadline = time.monotonic()+args.budget_seconds
    workers = {}
    report = {"schema": SCHEMA, "status": "running", "source_bindings": bindings,
              "invocation": [sys.executable, __file__, *(sys.argv[1:] if argv is None else argv)]}
    _write(args.output, report, exclusive=True)
    try:
        cases = prepare_cases(args.binary, args.fixture_cache or args.output.parent / "triad-performance-fixtures",
                              profile="ci", deadline=deadline, source_root=ROOT, case_ids=[args.case_id])
        case = cases[0]
        schedule(len(case["frequency_offsets_hz"]), args.large_count)  # validate before starting workers
        case["performance_environment"] = discover_hardware("cpu", args.threads)
        baseline = work / "reference-source"
        report["reference"] = api.extract_baseline(ROOT, args.baseline, baseline)
        for name, root in (("reference", baseline), ("candidate", ROOT)):
            workers[name] = api.ReceiverWorker(root, threads=args.threads, deadline=deadline, log_directory=work, name=name)
        report["precision_qualification"] = api.qualify_fft(case, workers, "cpu")
        if not report["precision_qualification"]["alternatives"][args.precision]["eligible"]:
            raise ValueError("explicit precision failed numerical qualification")
        report["diagnostic"] = measure_diagnostic(case, workers, args.precision, "cpu", api=api,
            commitment_path=commitment, deadline=deadline, source_guard=guard, large_counts=args.large_count)
        for name, worker in workers.items():
            if worker.request({"action": "hello"}) != worker.identity:
                raise ValueError(name+" worker source identity changed")
        guard()
        report["status"], report["exit_code"] = "diagnostic_complete", 0
    except Exception as error:
        report["status"], report["exit_code"] = "failed", 2
        report["error"] = {"type": type(error).__name__, "message": str(error)}
    finally:
        for worker in workers.values():
            worker.close()
        _write(args.output, report)
    print(json.dumps({"status": report["status"], "output": str(args.output)}))
    return report["exit_code"]


if __name__ == "__main__":
    raise SystemExit(main())
