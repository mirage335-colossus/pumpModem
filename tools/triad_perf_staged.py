#!/usr/bin/env python3
"""Independent stage calibration for complete warm sampled Triad workloads."""
from __future__ import annotations

import argparse
import inspect
import json
import math
from pathlib import Path
import runpy
import secrets
import statistics as stats
import subprocess
import tempfile
import time

import numpy as np
from scipy.stats import t as student_t

import triad_perf_families as sample
import triad_perf_family_regression as paired
from triad_perf_adaptive import AdaptivePolicy, model_confirmation_policy


SCHEMA = "triad-performance-staged-v1"
SOURCES = paired.SOURCES
STAGES = ("generation", "receiver", "pipeline")
# This scope is deliberately narrow. The hour waveform is never shortened for
# receiver timing. The two native generation siblings run complete exports.
GENERATION_SIBLINGS = {"hf-hour-state": (128, 450), "hf-256-state": None}
EQUIVALENCE = .05
CONDITIONAL_FLOOR = .10
MAX_CONDITIONAL_MEAN = .20
LEGACY_DRIVER_SHA256 = "b11e2d8df8b0d27d92653d2302a06ff21b4f0b6e4020f1cca13280d81493ad6a"
LEGACY_SPLIT_DRIVER_SHA256 = "9f3d660016bc56dd6e0c240239fb0bc2587b759cd8ce1eafbd12fac9ddbe2237"
LEGACY_MODEL_CORE_SHA256 = "6378161e650b8cfbe427a283d231c137085b3f47be62729f9d25384c26110b6f"
SPLIT_STOP = "independent-stage-look-v2"


def training_policy(policy):
    return {"looks": list(policy.looks), "family_count": policy.family_count, "alpha": policy.alpha,
            "relative_precision": policy.relative_precision, "metrics_per_stage": 3,
            "estimand": "arithmetic means of paired stage timings and their difference"}


def component_confidence(policy):
    return 1-policy["alpha"]/(policy["family_count"]*len(policy["looks"])*policy["metrics_per_stage"])


def confirmation_policy():
    shared = model_confirmation_policy()
    return {"looks": list(shared.looks), "family_alpha_two_sided": shared.family_alpha,
            "per_look_two_sided_confidence": shared.confidence,
            "relative_equivalence": EQUIVALENCE,
            "source_estimand": "arithmetic actual mean divided by frozen arithmetic forecast, minus one",
            "paired_estimand": "ratio of actual arithmetic means divided by forecast ratio, minus one",
            "paired_interval": "Fieller inversion of paired Student-t contrasts",
            "qualification_rule": "all generation, receiver and complete-pipeline intervals must fit the equivalence band",
            "future_coverage_qualified": False}


def stage_interval(values, confidence):
    data = np.asarray(values, dtype=np.float64)
    if data.ndim != 1 or data.size < 3 or not np.isfinite(data).all():
        raise ValueError("stage intervals require at least three finite outer observations")
    point = float(data.mean())
    half = float(student_t.ppf((1+confidence)/2, len(data)-1)*data.std(ddof=1)/math.sqrt(len(data)))
    return {"mean": point, "interval": [point-half, point+half], "count": len(data)}


def ratio_interval(reference, candidate, confidence):
    """Invert the paired Student-t interval for C-rR (Fieller interval)."""
    r, c = (np.asarray(x, dtype=np.float64) for x in (reference, candidate))
    if (r.shape != c.shape or r.ndim != 1 or len(r) < 3 or
        not np.isfinite(r).all() or not np.isfinite(c).all() or np.any(r <= 0) or np.any(c <= 0)):
        raise ValueError("ratio validation requires matched positive full observations")
    n = len(r)
    scale = student_t.ppf((1+confidence)/2, n-1)**2/n
    rmean, cmean = float(r.mean()), float(c.mean())
    a = rmean*rmean-scale*r.var(ddof=1)
    # Center the discriminant at the estimated ratio to avoid subtracting
    # nearly equal O(mean**4) terms when timings are identical.
    ratio = cmean/rmean
    residual = c-ratio*r
    shifted_b = float(scale*np.cov(r, residual, ddof=1)[0, 1])
    shifted_c = -float(scale*residual.var(ddof=1))
    discriminant = shifted_b*shifted_b-a*shifted_c
    if a <= 0 or discriminant < 0 or not math.isfinite(discriminant):
        return {"mean": ratio, "interval": None, "count": n}
    half = math.sqrt(discriminant)/a
    center = ratio-shifted_b/a
    return {"mean": ratio, "interval": [center-half, center+half], "count": n}


def stage_timing(rows, confidence):
    """Paired stage means. Shared calibration rows are counted once."""
    a, b = (rows[n] for n in SOURCES)
    if len(a) != len(b) or any(v <= 0 for v in (*a, *b)):
        raise ValueError("paired positive stage timings required")
    return {
        "reference": stage_interval(a, confidence),
        "candidate": stage_interval(b, confidence),
        "paired_difference": stage_interval(np.asarray(b)-np.asarray(a), confidence),
    }


def compose_timing(generation, receiver, confidence):
    stages = {name: stage_timing(values, confidence)
              for name, values in (("generation", generation), ("receiver", receiver))}
    result = {}
    for metric in ("reference", "candidate", "paired_difference"):
        parts = [stage[metric] for stage in stages.values()]
        result[metric] = {
            "mean": sum(p["mean"] for p in parts),
            "interval": [sum(p["interval"][i] for p in parts) for i in range(2)],
        }
    r, d = result["reference"], result["paired_difference"]
    ratio = 1+d["mean"]/r["mean"]
    if r["interval"][0] > 0:
        corners = [1+dv/rv for dv in d["interval"] for rv in r["interval"]]
        interval = [min(corners), max(corners)]
    else:
        interval = None
    result["paired"] = {"mean": ratio, "interval": interval}
    return {"stages": stages, "pipeline": result, "per_component_confidence": confidence,
            "scope": "additive arithmetic stage means; simultaneous component intervals via Bonferroni; shared generation uncertainty included once"}


def stage_forecast(timing):
    result = {"pipeline": {name: timing["pipeline"][name]["mean"] for name in SOURCES}}
    for stage in ("generation", "receiver"):
        result[stage] = {name: timing["stages"][stage][name]["mean"] for name in SOURCES}
    return result


def _precision(timing, threshold):
    for metric in (*SOURCES, "paired"):
        row = timing["pipeline"][metric]
        if row["interval"] is None or max(abs(x/row["mean"]-1) for x in row["interval"]) > threshold:
            return False
    return True


def _stage_metrics(row):
    r, d = row["reference"], row["paired_difference"]
    if r["interval"][0] > 0:
        corners = [1+dv/rv for dv in d["interval"] for rv in r["interval"]]
        interval = [min(corners), max(corners)]
    else:
        interval = None
    return {**row, "paired": {"mean": 1+d["mean"]/r["mean"], "interval": interval}}


def _relative_width(row):
    return max((abs(x/row[m]["mean"]-1) for m in (*SOURCES, "paired")
                for x in row[m]["interval"]), default=math.inf) if all(
                    row[m]["interval"] is not None and row[m]["mean"] > 0
                    for m in (*SOURCES, "paired")) else math.inf


def _next_stage_counts(timing, counts, policy):
    """Choose another marginal look; the intervals already cover their combinations."""
    stages = ("generation", "receiver")
    widths = {stage: _relative_width(_stage_metrics(timing["stages"][stage])) for stage in stages}
    next_look = {stage: next((n for n in policy.looks if n > counts[stage]), None) for stage in stages}
    available = [stage for stage in stages if next_look[stage] is not None]
    target = policy.relative_precision
    pending = [stage for stage in available if widths[stage] > target]
    if pending:
        choice = max(pending, key=widths.__getitem__)
    elif available and not _precision(timing, target):
        # Both component-specific intervals are adequate or exhausted.
        # Refine the largest contribution to complete-pipeline uncertainty.
        total = timing["pipeline"]
        def contribution(stage):
            part = timing["stages"][stage]
            return max((part[m]["interval"][1]-part[m]["interval"][0])/total[denominator]["mean"]
                       for m, denominator in (("reference", "reference"), ("candidate", "candidate"),
                                               ("paired_difference", "reference")))
        choice = max(available, key=contribution)
    else:
        return None
    return {**counts, choice: next_look[choice]}


def _capped_conditional_blockers(timing, counts, policy):
    """Find exhausted intervals that no full endpoint can make eligible."""
    blockers = []
    floor = math.log1p(CONDITIONAL_FLOOR)
    limit = math.log1p(MAX_CONDITIONAL_MEAN)
    for stage, count in counts.items():
        if count != policy.looks[-1]:
            continue
        rows = _stage_metrics(timing["stages"][stage])
        for metric in (*SOURCES, "paired"):
            row = rows[metric]
            interval = row["interval"]
            if interval is None or interval[0] <= 0:
                blockers.append({"stage": stage, "metric": metric, "reason": "no_positive_timing_interval"})
                continue
            half = max(abs(math.log(value/row["mean"])) for value in interval)
            if half+floor > limit:
                blockers.append({"stage": stage, "metric": metric, "reason": "mean_uncertainty_above_cap",
                                 "timing_log_half_width": half, "mean_model_log_allowance": floor,
                                 "mean_model_plus_timing_limit": limit})
    return blockers


def _timing_at(generation, receiver, counts, confidence):
    return compose_timing(_profile_values(generation[:counts["generation"]]),
                          _profile_values(receiver[:counts["receiver"]]), confidence)


def _replay_split_looks(proof, frozen, policy, confidence):
    if frozen["stopping_policy"] != SPLIT_STOP:
        raise ValueError("unsupported staged stopping policy")
    generation, receiver = proof["generation_calibration"], proof["receiver_calibration"]
    endpoint = {"generation": len(generation), "receiver": len(receiver)}
    counts = {stage: policy.looks[0] for stage in endpoint}
    schedule, timings = [], []
    while counts is not None:
        if any(counts[stage] > endpoint[stage] for stage in endpoint):
            raise ValueError("staged proof ended before its stopping rule allowed")
        timing = _timing_at(generation, receiver, counts, confidence)
        schedule.append(counts)
        timings.append(timing)
        counts = _next_stage_counts(timing, counts, policy)
    if schedule[-1] != endpoint or schedule != frozen["timing_schedule"]:
        raise ValueError("staged proof does not replay its declared timing schedule")
    return timings


def validation(holds, forecast):
    """One prespecified arithmetic mean-bias look for all nine scopes."""
    conf = confirmation_policy()["per_look_two_sided_confidence"]
    result = {}
    for stage in STAGES:
        values = {name: [paired._actual_stage(row, stage)[name] for row in holds] for name in SOURCES}
        expected = dict(forecast[stage])
        expected["paired"] = expected["candidate"]/expected["reference"]
        errors = {name: stage_interval(values[name], conf) for name in SOURCES}
        errors["paired"] = ratio_interval(values["reference"], values["candidate"], conf)
        result[stage] = {}
        for metric, observed in errors.items():
            interval = observed["interval"]
            relative = None if interval is None else [x/expected[metric]-1 for x in interval]
            outcome = "inconclusive"
            if relative is not None:
                if -EQUIVALENCE <= relative[0] <= relative[1] <= EQUIVALENCE:
                    outcome = "validated"
                elif relative[0] > EQUIVALENCE or relative[1] < -EQUIVALENCE:
                    outcome = "invalid"
            result[stage][metric] = {**observed, "forecast": expected[metric],
                                     "relative_error_interval": relative, "status": outcome}
    return result


def outcome(checks):
    states = {entry["status"] for stage in checks.values() for entry in stage.values()}
    return "validated" if states == {"validated"} else "invalid" if "invalid" in states else "inconclusive"


def generation_calendar(path):
    m = json.loads((Path(path)/"manifest.json").read_text())
    count, processed = m["sample_count"], m["channel_samples"]
    if type(count) is not int or count < 1 or processed != count or not m["stats_noisy"]:
        raise ValueError("native exporter did not complete its sampled calendar and noisy pass")
    # Work geometry is from the actual native export. Fixed args, binary,
    # format and channel identities are separately bound to the calibration.
    keys = ("format", "sample_rate", "block_samples", "dimension", "candidate_count",
            "column_mapping", "column_descriptors", "group_count", "groups", "candidates",
            "oscillator_model", "cn0_db_hz", "channel", "control", "capture_lead_seconds",
            "public_timing_bounds_capture_seconds", "grouped_export_execution")
    regime = {k: m[k] for k in keys if k in m}
    return {"samples": count, "source_samples": m["source_samples"], "blocks": m["block_count"],
            "tail_samples": m["channel_tail_samples"], "regime": regime}


def short_profile(profile, seconds):
    args = list(profile["args"])
    if args.count("--seconds") != 1:
        raise ValueError("duration siblings require one explicit native duration")
    args[args.index("--seconds")+1] = str(seconds)
    return {"mode": profile["mode"], "args": args}


def extrapolate(small_count, large_count, target_count, small_time, large_time):
    if not 0 < small_count < large_count < target_count <= 8*large_count:
        raise ValueError("abbreviation work counts leave the declared eightfold range")
    slope = (large_time-small_time)/(large_count-small_count)
    full = large_time+slope*(target_count-large_count)
    if full <= 0 or not math.isfinite(full):
        raise ValueError("abbreviated stage does not predict a positive finite target")
    return full


def _profile_values(rows):
    return {name: [row[name]["predicted_seconds"] for row in rows] for name in SOURCES}


def _capture_signature(paths):
    geometry = [generation_calendar(path) for path in paths]
    return {"calendars": geometry, "samples": sum(row["samples"] for row in geometry)}


def _write_receipt(directory, prefix, index, value):
    sample.write_json(directory/f"{prefix}-{index:03d}.json", value)


class Calibration:
    """Retain a complete physical fixture and one generation ledger per profile."""

    def __init__(self, workers, binaries, directory, evidence, *, native_threads, device, precision, deadline, seed):
        self.workers, self.binaries, self.directory = workers, binaries, Path(directory)
        self.evidence = Path(evidence)
        self.native_threads, self.device, self.precision = native_threads, device, precision
        self.deadline, self.seed = deadline, seed
        self.groups = {}
        self.binary_hashes = {name: sample.file_sha(binaries[name]) for name in SOURCES}

    def verify_binaries(self):
        if any(sample.file_sha(self.binaries[n]) != self.binary_hashes[n] for n in SOURCES):
            raise ValueError("native sampled exporter changed during stage calibration")

    def _export(self, profile, name, path, seed, captures):
        paths, timings = sample.export_fixtures(self.binaries[name], profile, seed, self.native_threads,
                                                path, captures, self.deadline)
        return paths, {"generation": timings, "work": _capture_signature(paths),
                       "fixture": sample.fixture_evidence(paths)}

    def group(self, profile_name, profile, captures):
        key = sample.digest({"profile": profile, "binaries": self.binary_hashes, "captures": captures,
                             "native_threads": self.native_threads})
        if key in self.groups:
            return self.groups[key]
        directory = self.evidence/("generation-"+profile_name+"-"+key[:12])
        directory.mkdir(parents=True, exist_ok=False)
        scratch = self.directory/(profile_name+"-"+key[:12])
        scratch.mkdir(parents=True, exist_ok=False)
        seed = self.seed+len(self.groups)*1000000
        cached, raw = {}, {}
        for name in SOURCES:
            cached[name], raw[name] = self._export(profile, name, scratch/f"cached-{name}", seed, captures)
        if raw["reference"]["work"] != raw["candidate"]["work"]:
            raise ValueError("native sources resolved different full generation regimes")
        group = {"id": key, "directory": directory, "scratch": scratch, "seed": seed, "profile": profile,
                 "captures": captures, "paths": cached, "cached": raw, "calibration": [],
                 "siblings": GENERATION_SIBLINGS.get(profile_name)}
        sample.write_json(directory/"cached-receipt.json", raw)
        self.groups[key] = group
        return group

    def generation_pair(self, group):
        index = len(group["calibration"])
        seed = group["seed"]+1000+17*index
        result = {}
        order = SOURCES if index % 2 == 0 else SOURCES[::-1]
        with tempfile.TemporaryDirectory(prefix="short-", dir=group["scratch"]) as directory:
            for offset, name in enumerate(order):
                full = group["cached"][name]["work"]
                durations = group["siblings"]
                if durations is None:
                    _, receipt = self._export(group["profile"], name, Path(directory)/name, seed, group["captures"])
                    if receipt["work"] != full:
                        raise ValueError("direct native generation calendar changed")
                    target = receipt["generation"]["wall_seconds"]
                    runs = [receipt]
                else:
                    runs = []
                    ordered = durations[::-1] if (index+offset) % 2 else durations
                    for seconds in ordered:
                        _, receipt = self._export(short_profile(group["profile"], seconds), name,
                            Path(directory)/f"{name}-{seconds}", seed, group["captures"])
                        a = [cal["regime"] for cal in receipt["work"]["calendars"]]
                        b = [cal["regime"] for cal in full["calendars"]]
                        if a != b:
                            raise ValueError("native duration sibling changed the channel, basis or execution regime")
                        runs.append(receipt)
                    if ordered != durations:
                        runs.reverse()
                    a, b = runs
                    target = extrapolate(a["work"]["samples"], b["work"]["samples"], full["samples"],
                                         a["generation"]["wall_seconds"], b["generation"]["wall_seconds"])
                result[name] = {"predicted_seconds": target, "target_work": full, "runs": runs}
        group["calibration"].append(result)
        _write_receipt(group["directory"], "generation", index, result)
        return result

    def receiver_pair(self, group, case, index):
        order = SOURCES if index % 2 == 0 else SOURCES[::-1]
        rows = {}
        for name in order:
            call = sample.receive(self.workers[name], group["paths"][name], case, self.device, self.precision)
            rows[name] = {"predicted_seconds": call["request_wall_seconds"], "receiver_runs": [call]}
            rows[name]["target_work"] = sample.complete_work_signature(rows[name])
        rows["numeric_agreement"] = paired.numerical_agreement(rows, self.precision)
        return rows

    def full_pair(self, group, case, index, output, seed):
        order = SOURCES if index % 2 == 0 else SOURCES[::-1]
        result = {}
        with tempfile.TemporaryDirectory(prefix="full-", dir=output) as directory:
            for name in order:
                result[name] = sample.measure_job(self.workers[name], self.binaries[name],
                    group["profile"], case, seed, self.native_threads, Path(directory)/name,
                    self.device, self.precision, self.deadline)
                paths = [Path(argv[-1]) for argv in result[name]["generation"]["commands"]]
                result[name]["generation_work"] = _capture_signature(paths)
        result["pair_id"] = sample.digest({"seed": seed, "case": case["id"]})
        result["numeric_agreement"] = paired.numerical_agreement(result, self.precision)
        _write_receipt(output, "full", index, result)
        return result

    def verify_cache(self, group):
        for name in SOURCES:
            current = sample.fixture_evidence(group["paths"][name])
            earlier = group["cached"][name]["fixture"]
            if [f["sha256"] for f in current] != [f["sha256"] for f in earlier]:
                raise ValueError("retained receiver input changed during calibration")


def campaigns(calibration, group, case, output, forecast, expected_work, seconds, seed):
    """Independently observe both full fresh jobs and cached receiver replay."""
    out = Path(output)
    out.mkdir()
    name = "candidate"
    g, r = (forecast[stage][name] for stage in ("generation", "receiver"))
    fresh_jobs = max(2, math.ceil(seconds/(g+r)))
    cached_jobs = max(2, math.ceil(seconds/r))
    predicted = fresh_jobs*(g+r)+g+cached_jobs*r
    if 1.2*predicted > sample.remaining(calibration.deadline):
        return {"status": "insufficient_declared_budget", "minimum_predicted_seconds": predicted}
    stored_bytes = sum(v["bytes"] for v in group["cached"][name]["fixture"])
    if fresh_jobs*stored_bytes > 256*1024**2:
        return {"status": "campaign_scratch_budget_exceeded", "predicted_capture_bytes": fresh_jobs*stored_bytes}
    worker, binary = calibration.workers[name], calibration.binaries[name]
    cached_seed = seed+100000
    plan = {"jobs": cached_jobs, "generation_seconds": g, "receiver_seconds_per_job": r,
            "predicted_receiver_seconds": cached_jobs*r, "predicted_full_batch_seconds": g+cached_jobs*r,
            "expected_complete_work": expected_work, "seed": cached_seed,
            "scope": "one fresh sampled export, then complete independent receiver states on one retained capture; startup shared outside the timed batch"}
    sample.write_json(out/"cached-forecast.json", plan)
    with tempfile.TemporaryDirectory(prefix="campaign-", dir=out) as directory:
        scratch = Path(directory)
        fresh = sample.sustain(worker, binary, group["profile"], case, seed, calibration.native_threads,
            scratch, calibration.device, calibration.precision, calibration.deadline,
            fresh_jobs, g+r, out, expected_work)
        gen_work = group["cached"][name]["work"]
        fresh["generation_regime_matched"] = all(_capture_signature(
            [Path(argv[-1]) for argv in row["generation"]["commands"]]) == gen_work for row in fresh["jobs"])
        fresh["actual_minimum_wall_reached"] = fresh["warm_pipeline_actual_seconds"] >= seconds
        sample.write_json(out/"fresh-observed.json", fresh)

        start = time.perf_counter()
        paths, generation = sample.export_fixtures(binary, group["profile"], cached_seed, calibration.native_threads,
            scratch/"cached-batch", case["captures"], calibration.deadline)
        receiver_start = time.perf_counter()
        calls = [sample.receive(worker, paths, case, calibration.device, calibration.precision)
                 for _ in range(cached_jobs)]
        end = time.perf_counter()
        receiver_seconds = end-receiver_start
        batch_seconds = end-start
        work_ok = all(sample.complete_work_signature({"receiver_runs": [call]}) == expected_work for call in calls)
        quarter = max(1, cached_jobs//4)
        first = stats.fmean(call["request_wall_seconds"] for call in calls[:quarter])
        last = stats.fmean(call["request_wall_seconds"] for call in calls[-quarter:])
        cached = {**plan, "actual_receiver_seconds": receiver_seconds, "actual_batch_seconds": batch_seconds,
                  "receiver_relative_error": receiver_seconds/plan["predicted_receiver_seconds"]-1,
                  "batch_relative_error": batch_seconds/plan["predicted_full_batch_seconds"]-1,
                  "actual_minimum_receiver_wall_reached": receiver_seconds >= seconds,
                  "last_over_first_quarter": last/first, "work_regime_matched": work_ok,
                  "generation_regime_matched": _capture_signature(paths) == gen_work,
                  "generation_receipt": generation, "fixture": sample.fixture_evidence(paths),
                  "receiver_receipts": calls, "future_coverage_qualified": False}
        sample.write_json(out/"cached-observed.json", cached)
    return {"status": "observed", "fresh": fresh, "cached": cached}


def _native_receipt_seeds(raw, work, profile, captures, threads):
    generation, fixtures = raw["generation"], raw["fixture"]
    calendars = work["calendars"]
    commands = generation["commands"]
    invocations = generation["invocation_wall_seconds"]
    if not (generation["count"] == len(calendars) == len(commands) == len(fixtures) ==
            len(invocations) == captures):
        raise ValueError("native receipt did not produce its declared captures")
    if (any(not math.isfinite(v) or v <= 0 for v in invocations) or
        sum(invocations) != generation["wall_seconds"] or
        sum(row["samples"] for row in calendars) != work["samples"]):
        raise ValueError("native receipt wall time or sample totals changed")
    seeds = set()
    for i, (calendar, fixture, command) in enumerate(zip(calendars, fixtures, commands, strict=True)):
        seed = generation["seed"]+i
        if command != sample.native_arguments(command[0], profile, seed, threads, command[-1]):
            raise ValueError("native receipt arguments differ from the declared profile")
        n, block = calendar["samples"], calendar["regime"]["block_samples"]
        if (type(n) is not int or type(block) is not int or n < 1 or block < 1 or
            type(calendar["source_samples"]) is not int or calendar["source_samples"] < 1 or
            calendar["blocks"] != (n+block-1)//block or
            not 0 <= calendar["tail_samples"] <= n or
            fixture["sample_count"] != n or fixture["block_count"] != calendar["blocks"] or
            "manifest.json" not in fixture["sha256"]):
            raise ValueError("native receipt calendar or input binding changed")
        seeds.add(seed)
    return seeds


def _use_independent_seeds(reference, candidate, seen):
    if reference != candidate or reference & seen:
        raise ValueError("paired physical seeds differ or overlap another experiment")
    seen.update(reference)


def _history_source_applicability(original, current):
    if sample.digest(original) == sample.digest(current):
        return {"method": "exact_measurement_identity"}
    name = Path(__file__).name
    previous = original["measurement_dependencies"][name]
    latest = current["measurement_dependencies"][name]
    normalized = {**original, "measurement_dependencies": {**original["measurement_dependencies"], name: latest}}
    methods = {LEGACY_DRIVER_SHA256: "b11_stopping_policy_only",
               LEGACY_SPLIT_DRIVER_SHA256: "9f_routine_effort_only"}
    if previous not in methods or sample.digest(normalized) != sample.digest(current):
        return None
    if (EQUIVALENCE, CONDITIONAL_FLOOR, MAX_CONDITIONAL_MEAN) != (.05, .10, .20):
        return None
    # The archived drivers proved the same physical work and estimator.
    # Only the order/count of pre-endpoint timing observations is allowed to
    # change. Keep the original proof identity and replay its original rule.
    names = ("Calibration", "extrapolate", "generation_calendar", "short_profile", "stage_interval",
             "ratio_interval", "stage_timing", "compose_timing", "stage_forecast", "validation", "outcome",
             "_native_receipt_seeds", "_individual_residuals", "conditional_plan", "check_sentinel")
    core = sample.digest({unit: inspect.getsource(globals()[unit]) for unit in names})
    if core != LEGACY_MODEL_CORE_SHA256:
        return None
    return {"method": methods[previous], "original_driver_sha256": previous,
            "current_driver_sha256": latest, "unchanged_measurement_core_sha256": core,
            "fresh_endpoint_required": True}


def _historical(path, identity):
    if path is None:
        return None
    try:
        proof = json.loads(Path(path).read_text())
        check_hash = proof.pop("record_sha256")
        if (proof.get("schema") != SCHEMA or check_hash != sample.digest(proof) or
            proof["status"] != "validated"):
            return None
        original = proof["identity"]
        applicability = _history_source_applicability(original, identity)
        if applicability is None:
            return None
        frozen = dict(proof["frozen"])
        commitment = frozen.pop("commitment_sha256")
        if commitment != sample.digest(frozen) or frozen["identity_sha256"] != sample.digest(original):
            return None
        if frozen["confirmation_policy"] != confirmation_policy():
            return None
        timing_policy = frozen["training_policy"]
        policy = AdaptivePolicy(looks=tuple(timing_policy["looks"]), family_count=timing_policy["family_count"],
                                alpha=timing_policy["alpha"], relative_precision=timing_policy["relative_precision"])
        counts = [len(proof[key]) for key in ("generation_calibration", "receiver_calibration")]
        if (timing_policy != training_policy(policy) or any(n not in policy.looks for n in counts) or
            policy.family_count != identity["timing_family_count"] or policy.alpha != identity["timing_alpha"]):
            return None
        confidence = component_confidence(timing_policy)
        if confidence != frozen["timing"]["per_component_confidence"]:
            return None
        profile = identity["generation_profile"]
        durations = identity["generation_sibling_seconds"]
        captures, threads = identity["case"]["captures"], identity["native_threads"]
        seen = set()
        cached_seeds = []
        for name in SOURCES:
            cached = proof["cached_fixtures"][name]
            target = identity["generation_target_work"][name]
            if cached["work"] != target:
                return None
            cached_seeds.append(_native_receipt_seeds(cached, target, profile, captures, threads))
        _use_independent_seeds(*cached_seeds, seen)
        for row in proof["generation_calibration"]:
            paired_seeds = []
            for name in SOURCES:
                record = row[name]
                runs, target = record["runs"], record["target_work"]
                if target != identity["generation_target_work"][name]:
                    return None
                if len(runs) == 1 and durations is None:
                    if runs[0]["work"] != target:
                        return None
                    expected = runs[0]["generation"]["wall_seconds"]
                    seeds = _native_receipt_seeds(runs[0], runs[0]["work"], profile, captures, threads)
                elif len(runs) == 2 and durations is not None:
                    a, b = runs
                    target_regimes = [c["regime"] for c in target["calendars"]]
                    if any([c["regime"] for c in run["work"]["calendars"]] != target_regimes for run in runs):
                        return None
                    seeds = [_native_receipt_seeds(run, run["work"], short_profile(profile, seconds),
                                                  captures, threads)
                             for run, seconds in zip(runs, durations, strict=True)]
                    if seeds[0] != seeds[1]:
                        return None
                    seeds = seeds[0]
                    expected = extrapolate(a["work"]["samples"], b["work"]["samples"],
                                           target["samples"], a["generation"]["wall_seconds"],
                                           b["generation"]["wall_seconds"])
                else:
                    return None
                if expected != record["predicted_seconds"]:
                    return None
                paired_seeds.append(seeds)
            _use_independent_seeds(*paired_seeds, seen)
        for row in proof["receiver_calibration"]:
            paired.numerical_agreement(row, identity["precision"])
            for name in SOURCES:
                if (sample.complete_work_signature(row[name]) != identity["receiver_target_work"][name] or
                    row[name]["receiver_runs"][0]["request_wall_seconds"] != row[name]["predicted_seconds"]):
                    return None
        if applicability["method"] == "b11_stopping_policy_only":
            if "stopping_policy" in frozen or counts[0] != counts[1]:
                return None
            training_looks = [_timing_at(proof["generation_calibration"], proof["receiver_calibration"],
                                        {"generation": n, "receiver": n}, confidence)
                              for n in policy.looks if n <= counts[0]]
            if (any(_precision(look, policy.relative_precision) for look in training_looks[:-1]) or
                not _precision(training_looks[-1], policy.relative_precision) and counts[0] != policy.looks[-1]):
                return None
        else:
            training_looks = _replay_split_looks(proof, frozen, policy, confidence)
            expected_stage_precision = {stage: _relative_width(_stage_metrics(row)) <= policy.relative_precision
                                        for stage, row in training_looks[-1]["stages"].items()}
            if proof["stage_timing_precision_reached"] != expected_stage_precision:
                return None
        if training_looks != proof["training_looks"]:
            return None
        timing = training_looks[-1]
        if timing != frozen["timing"] or stage_forecast(timing) != frozen["forecast"]:
            return None
        reached = _precision(timing, policy.relative_precision)
        if proof["timing_precision_reached"] != reached:
            return None
        holds = proof["full_pairs"]
        looks = [n for n in confirmation_policy()["looks"] if n <= len(holds)]
        if not looks or looks[-1] != len(holds):
            return None
        for row in holds:
            paired.numerical_agreement(row, identity["precision"])
            hold_seeds = []
            for name in SOURCES:
                if (sample.complete_work_signature(row[name]) != identity["receiver_target_work"][name] or
                    row[name]["generation_work"] != identity["generation_target_work"][name]):
                    return None
                hold_seeds.append(_native_receipt_seeds(row[name], row[name]["generation_work"],
                                                       profile, captures, threads))
            _use_independent_seeds(*hold_seeds, seen)
        checks = [validation(holds[:n], frozen["forecast"]) for n in looks]
        if (checks != proof["confirmation"] or outcome(checks[-1]) != "validated" or
            any(outcome(previous) != "inconclusive" for previous in checks[:-1])):
            return None
        return {"record_sha256": check_hash, "full_pairs": holds, "forecast": frozen["forecast"],
                "physical_seeds": seen, "source_applicability": applicability}
    except (OSError, ValueError, KeyError, TypeError, IndexError, OverflowError):
        return None


def _individual_residuals(row, stage, forecast):
    actual = paired._actual_stage(row, stage)
    signed = {name: math.log(actual[name]/forecast[name]) for name in SOURCES}
    signed["paired"] = signed["candidate"]-signed["reference"]
    return signed


def conditional_plan(history, timing, identity):
    observation_envelopes = {}
    for stage in STAGES:
        residuals = [_individual_residuals(row, stage, history["forecast"][stage])
                     for row in history["full_pairs"]]
        observation_envelopes[stage] = {metric: max(math.log1p(CONDITIONAL_FLOOR),
             *(abs(row[metric]) for row in residuals)) for metric in (*SOURCES, "paired")}
    return {"identity_sha256": sample.digest(identity), "historical_record_sha256": history["record_sha256"],
            "historical_observation_envelopes": observation_envelopes,
            "fixed_mean_model_log_allowance": math.log1p(CONDITIONAL_FLOOR),
            "mean_model_plus_timing_limit": math.log1p(MAX_CONDITIONAL_MEAN),
            "current_timing": timing, "future_coverage_qualified": False}


def check_sentinel(plan, full):
    checks = {}
    for stage in STAGES:
        row = plan["current_timing"]["pipeline" if stage == "pipeline" else "stages"]
        if stage != "pipeline":
            row = row[stage]
            r, d = row["reference"], row["paired_difference"]
            corners = ([1+dv/rv for dv in d["interval"] for rv in r["interval"]]
                       if r["interval"][0] > 0 else None)
            row = {**row, "paired": {"mean": 1+d["mean"]/r["mean"],
                                     "interval": [min(corners), max(corners)] if corners else None}}
        actual = paired._actual_stage(full, stage)
        actual["paired"] = actual["candidate"]/actual["reference"]
        checks[stage] = {}
        for metric in (*SOURCES, "paired"):
            bounds = row[metric]["interval"]
            if bounds is None or bounds[0] <= 0:
                checks[stage][metric] = {"status": "inconclusive"}
                continue
            mean = row[metric]["mean"]
            half = max(abs(math.log(x/mean)) for x in bounds)
            residual = [math.log(actual[metric]/bounds[1]), math.log(actual[metric]/bounds[0])]
            allowed = plan["historical_observation_envelopes"][stage][metric]
            if residual[0] > allowed or residual[1] < -allowed:
                status = "outside_frozen_envelope"
            elif (half+plan["fixed_mean_model_log_allowance"] <= plan["mean_model_plus_timing_limit"] and
                  -allowed <= residual[0] <= residual[1] <= allowed):
                status = "within_frozen_envelope"
            else:
                status = "inconclusive"
            checks[stage][metric] = {"status": status, "observation_residual_log_interval": residual,
                                    "historical_observation_log_allowance": allowed,
                                    "severe_underprediction": residual[0]-allowed > math.log1p(.30)}
    results = [v for c in checks.values() for v in c.values()]
    return {"eligible": all(v["status"] == "within_frozen_envelope" for v in results),
            "severe_endpoint_prediction_mismatch": any(v.get("severe_underprediction") for v in results),
            "stage_checks": checks, "future_coverage_qualified": False}


def staged_case(calibration, case, profile, output, *, policy, mode, anchor=None, maximum_holds=9,
                sustained_seconds=0.):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    started = time.perf_counter()
    setup = {name: sample.case_worker_setup(calibration.workers[name], case, calibration.device) for name in SOURCES}
    group = calibration.group(case["fixture"], profile, case["captures"])
    warmup = calibration.receiver_pair(group, case, 0)
    target_work = {name: warmup[name]["target_work"] for name in SOURCES}
    driver = Path(__file__).resolve()
    dependencies = (*sample.MEASUREMENT_DEPENDENCIES, "triad_perf_family_regression.py", driver.name)
    identity = {"case": case, "generation_profile": profile, "generation_group": group["id"],
                "generation_target_work": {n: group["cached"][n]["work"] for n in SOURCES},
                "generation_sibling_seconds": group["siblings"],
                "receiver_method": "direct full retained physical workspace", "receiver_target_work": target_work,
                "binaries": calibration.binary_hashes, "native_threads": calibration.native_threads,
                "device": calibration.device, "precision": calibration.precision,
                "timing_family_count": policy.family_count, "timing_alpha": policy.alpha,
                "source_sha256": {n: setup[n]["source_sha256"] for n in SOURCES},
                "environment": {n: setup[n]["environment"] for n in SOURCES},
                "measurement_dependencies": {name: sample.file_sha(driver.with_name(name)) for name in dependencies}}
    history = _historical(anchor, identity) if mode == "reuse" else None
    report = {"schema": SCHEMA, "id": case["id"], "status": "unqualified", "identity": identity,
              "cached_fixtures": group["cached"], "discarded_receiver_warmup": warmup, "setup": setup,
              "worker_startup_seconds": {name: calibration.workers[name].external_startup_seconds for name in SOURCES},
              "training_looks": [], "full_pairs": [], "confirmation": [], "comparison": {"gate": "inconclusive"},
              "cold_latency_qualified": False, "prediction_valid": False,
              "timing_scope": "fresh full native export plus complete receiver request in a persistent initialized worker"}
    if mode == "reuse" and history is None:
        report["reason"] = "no matching staged arithmetic-mean anchor; routine cannot launch qualification"
    else:
        timing_policy = training_policy(policy)
        confidence = component_confidence(timing_policy)
        sample.write_json(output/"training-policy.json", {"training_policy": timing_policy,
                          "stopping_policy": SPLIT_STOP, "component_confidence": confidence,
                          "routine_capped_stage_guard_enabled": history is not None,
                          "scope": "simultaneous marginal looks; stage count choices reuse these bounds"})
        rx, schedule = [], []
        refinement_stop = None
        counts = {"generation": policy.looks[0], "receiver": policy.looks[0]}
        while counts is not None:
            while len(group["calibration"]) < counts["generation"] or len(rx) < counts["receiver"]:
                if len(group["calibration"]) < counts["generation"]:
                    calibration.generation_pair(group)
                if len(rx) < counts["receiver"]:
                    index = len(rx)
                    row = calibration.receiver_pair(group, case, index)
                    if {name: row[name]["target_work"] for name in SOURCES} != target_work:
                        raise ValueError("receiver work regime changed during stage timing")
                    rx.append(row)
                    _write_receipt(output, "receiver", index, row)
            timing = _timing_at(group["calibration"], rx, counts, confidence)
            report["training_looks"].append(timing)
            schedule.append(counts)
            next_counts = _next_stage_counts(timing, counts, policy)
            if history is not None and next_counts is not None:
                blockers = _capped_conditional_blockers(timing, counts, policy)
                if blockers:
                    refinement_stop = {"reason": "capped_stage_precludes_conditional_endpoint",
                                       "stage_counts": counts, "unneeded_next_look": next_counts,
                                       "blockers": blockers}
                    break
            counts = next_counts
        report["timing_precision_reached"] = _precision(timing, policy.relative_precision)
        report["stage_timing_precision_reached"] = {stage: _relative_width(_stage_metrics(row)) <= policy.relative_precision
                                                   for stage, row in timing["stages"].items()}
        report["generation_calibration"] = group["calibration"][:schedule[-1]["generation"]]
        report["receiver_calibration"] = rx
        forecast = stage_forecast(timing)
        frozen = {"forecast": forecast, "timing": timing, "identity_sha256": sample.digest(identity),
                  "training_policy": timing_policy, "confirmation_policy": confirmation_policy(),
                  "stopping_policy": SPLIT_STOP, "timing_schedule": schedule}
        if history:
            frozen["conditional_plan"] = conditional_plan(history, timing, identity)
            frozen["routine_refinement_stop"] = refinement_stop
            report["historical_source_applicability"] = history["source_applicability"]
        report["frozen"] = {**frozen, "commitment_sha256": sample.digest(frozen)}
        sample.write_json(output/"forecast.json", report["frozen"])
        total = sum(forecast["pipeline"].values())
        for index in range(1 if history else maximum_holds):
            if sample.remaining(calibration.deadline) < 1.2*total:
                report["reason"] = "full validation no longer fits declared deadline"
                break
            full = calibration.full_pair(group, case, index, output, calibration.seed+30000000+case["seed"]+17*index)
            report["full_pairs"].append(full)
            if any(sample.complete_work_signature(full[n]) != target_work[n] or
                   full[n]["generation_work"] != identity["generation_target_work"][n] for n in SOURCES):
                report["reason"] = "full workload changed receiver or generation geometry"
                break
            if history:
                seed = full["reference"]["generation"]["seed"]
                if set(range(seed, seed+case["captures"])) & history["physical_seeds"]:
                    report["reason"] = "routine full observation reused historical physical seeds"
                    break
                check = check_sentinel(frozen["conditional_plan"], full)
                report["sentinel"] = check
                if check["eligible"]:
                    report["status"] = "historically_supported_conditional"
                elif check["severe_endpoint_prediction_mismatch"]:
                    report["comparison"] = {"gate": "severe_endpoint_prediction_mismatch",
                                            "mean_runtime_regression_established": False}
                break
            if index+1 in confirmation_policy()["looks"]:
                check = validation(report["full_pairs"], forecast)
                report["confirmation"].append(check)
                status = outcome(check)
                if status == "validated":
                    report["status"] = "validated"
                    break
                if status == "invalid":
                    report["reason"] = "at least one arithmetic stage or paired ratio missed equivalence"
                    break
        if report["status"] in ("validated", "historically_supported_conditional"):
            report["prediction_valid"] = True
            band = timing["pipeline"]["paired"]["interval"]
            if band is not None:
                # Same 10% conditional engineering allowance used by the
                # existing reuse scheme. Keep it separate from timing precision.
                band = [band[0]/(1+CONDITIONAL_FLOOR), band[1]*(1+CONDITIONAL_FLOOR)]
                gate = ("performance_regression" if band[0] > policy.slowdown_threshold else
                        "pass" if band[1] <= policy.slowdown_threshold else "inconclusive")
                report["comparison"] = {"gate": gate, "model_adjusted_ratio_interval": band,
                                        "future_coverage_qualified": False}
            if sustained_seconds:
                report["campaigns"] = campaigns(calibration, group, case, output/"campaigns", forecast,
                    target_work["candidate"], sustained_seconds, calibration.seed+50000000+case["seed"])
    calibration.verify_cache(group)
    calibration.verify_binaries()
    for name in SOURCES:
        calibration.workers[name].request({"action": "family-verify", "case": case})
    if any(sample.file_sha(driver.with_name(name)) != h for name, h in identity["measurement_dependencies"].items()):
        raise ValueError("staged measurement source changed")
    report["wall_seconds"] = time.perf_counter()-started
    report["record_sha256"] = sample.digest(report)
    sample.write_json(output/"result.json", report)
    return report


def cli(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-root", type=Path, default=sample.ROOT)
    parser.add_argument("--candidate-root", type=Path, default=sample.ROOT)
    parser.add_argument("--reference-binary", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--anchor-directory", type=Path)
    parser.add_argument("--case", action="append", required=True, dest="cases")
    parser.add_argument("--device", choices=("cpu", "cuda"), default="cpu")
    parser.add_argument("--precision", choices=("fp32", "fp64"), default="fp32")
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--native-threads", type=int, default=1)
    parser.add_argument("--budget-seconds", type=float, default=600.)
    parser.add_argument("--looks", default="5,9,17,33")
    parser.add_argument("--maximum-holds", type=int, choices=(3,5,9,17), default=9)
    parser.add_argument("--sustained-seconds", type=float, default=0.)
    parser.add_argument("--mode", choices=("reuse", "qualify", "direct"), default="reuse")
    args = parser.parse_args(argv)
    config = sample.manifest()
    cases = {case["id"]: case for case in config["cases"]}
    if set(args.cases)-cases.keys() or len(set(args.cases)) != len(args.cases):
        parser.error("unknown or repeated sampled case")
    if (not 1 <= args.threads <= 256 or not 1 <= args.native_threads <= 256 or
        not 0 < args.budget_seconds <= 28800 or not 0 <= args.sustained_seconds <= 3600):
        parser.error("invalid thread count or budget")
    generation_groups = {(case["fixture"], case["captures"]) for case in cases.values()
                         if case["fixture"] in GENERATION_SIBLINGS}
    selected_groups = [(cases[name]["fixture"], cases[name]["captures"]) for name in args.cases]
    policy = AdaptivePolicy(looks=tuple(map(int, args.looks.split(","))),
                            family_count=len(cases)+len(generation_groups))
    common = runpy.run_path(str(sample.WORKER.with_name("triad-performance.py")))
    start = time.perf_counter()
    deadline = time.monotonic()+args.budget_seconds
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    binaries = {"reference": args.reference_binary.resolve(strict=True), "candidate": args.binary.resolve(strict=True)}
    roots = {"reference": args.reference_root.resolve(strict=True), "candidate": args.candidate_root.resolve(strict=True)}
    workers, statuses = {}, {}
    try:
        for name in SOURCES:
            workers[name] = common["ReceiverWorker"](roots[name], threads=args.threads, deadline=deadline,
                                                      log_directory=output, name=name, adapter=sample.WORKER)
        with tempfile.TemporaryDirectory(prefix="calibration-", dir=output) as directory:
            calibration = Calibration(workers, binaries, directory, output, native_threads=args.native_threads,
                device=args.device, precision=args.precision, deadline=deadline, seed=secrets.randbits(40))
            for name in args.cases:
                case = cases[name]
                if args.device not in case["devices"] or (args.device == "cuda" and
                    not all(w.identity["cuda_available"] for w in workers.values())):
                    statuses[name] = {"status": "unsupported_backend"}
                    continue
                profile = config["fixtures"][case["fixture"]]
                anchor = args.anchor_directory/f"{name}.staged-anchor.json" if args.anchor_directory else None
                group_key = (case["fixture"], case["captures"])
                use_stages = (args.mode != "direct" and case["fixture"] in GENERATION_SIBLINGS and
                              (GENERATION_SIBLINGS[case["fixture"]] is not None or selected_groups.count(group_key) > 1))
                try:
                    if not use_stages:
                        row = paired.measure_case(workers, binaries, case, profile, output/name,
                            device=args.device, precision=args.precision, native_threads=args.native_threads,
                            deadline=deadline, policy=policy, mode="direct", sustained_seconds=args.sustained_seconds)
                    elif args.mode == "reuse" and (anchor is None or not anchor.is_file()):
                        statuses[name] = {"status": "unqualified", "gate": "inconclusive",
                                          "reason": "no staged historical anchor; qualification is explicit"}
                        continue
                    else:
                        row = staged_case(calibration, case, profile, output/name, policy=policy, mode=args.mode,
                                          anchor=anchor, maximum_holds=args.maximum_holds,
                                          sustained_seconds=args.sustained_seconds)
                except (ValueError, RuntimeError, TimeoutError, subprocess.TimeoutExpired) as error:
                    statuses[name] = {"status": "error", "detail": f"{type(error).__name__}: {error}"}
                    continue
                if anchor and row["status"] == "validated" and not anchor.exists():
                    anchor.parent.mkdir(parents=True, exist_ok=True)
                    sample.write_json(anchor, row)
                statuses[name] = {"status": row["status"], "gate": row["comparison"]["gate"],
                                  "prediction_valid": row["prediction_valid"],
                                  "timing_precision_reached": row.get("timing_precision_reached",
                                     row.get("adaptive", {}).get("timing_precision_reached")),
                                  "stage_timing_precision_reached": row.get("stage_timing_precision_reached")}
    finally:
        for worker in workers.values():
            worker.close()
        sample.write_json(output/"summary.json", {"schema": SCHEMA, "case_status": statuses,
            "wall_seconds": time.perf_counter()-start, "complete_scope_qualification": False})
    return 0 if all(v.get("gate") == "pass" for v in statuses.values()) else 2


if __name__ == "__main__":
    raise SystemExit(cli())
