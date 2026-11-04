#!/usr/bin/env python3
"""Source-bound cold-process and fixed mixed-family simulation timing.

The controller is already running; the target starts before creating its
receiver process and ends after close/wait and sampled-file cleanup. OS page
cache is retained. Mixed campaigns share native captures by exact profile and
capture count, then create a complete independent receiver state on every call.
"""
from __future__ import annotations

import argparse
import ast
from contextlib import contextmanager
from dataclasses import replace
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
import time

import numpy as np
from scipy.stats import t as student_t

import triad_perf_families as sample
from triad_perf_adaptive import AdaptivePolicy, model_confirmation_policy, regression_profile
from triad_perf_family_regression import canonical_library_call, numerical_agreement

ROOT = Path(__file__).resolve().parents[1]
WORKER = Path(__file__).resolve()
MANIFEST = WORKER.with_name("triad-performance-lifecycle.json")
SCHEMA = "triad-performance-lifecycle-v1"
SOURCES = ("reference", "candidate")
LOOKS = (3, 5, 9, 17)
REVIEWED_GPU_PROTOCOL35 = "35e543ac8a84ad879dedb1aa39628e995ad1584e5e4e921d207dda581fe3b40a"
REVIEWED_GPU_MANIFEST35 = "482af707eedc09962b3253c2c146eb0e7204a6e6afcd2dc370f74165d4700de3"
REVIEWED_DIRECT_PROTOCOL5DFA = "5dfa0492f4cd3dac3db262feee8b81f5c076ceeece3c56a210a54d3e58fcb336"
REVIEWED_DIRECT_MANIFEST5DFA = "eee47f2396e0a230bf95a60029c958041449e37ab7d54872a2a5156a898643d4"
REVIEWED_POOL_ENUM_PROTOCOL93DE = "93de7a47b360bcb7531e72de5381cdb4b6feb0d10c0c44150697bc62704d7b80"
MEASUREMENT_SCOPE = (
    "fresh receiver process creation, numerical imports, native shared full sampled exports, "
    "family setup/device initialization, first complete calls, declared warm cycles, "
    "receiver finish/result IPC, close/wait and scratch cleanup; controller initialization, "
    "fixture hashing and evidence serialization excluded; ordinary OS page cache retained"
)


def sha(path):
    return sample.file_sha(path)


def digest(value):
    return sample.digest(value)


def save(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    sample.write_json(path, value)


def load_manifest():
    value = json.loads(MANIFEST.read_text())
    if value.get("schema") != SCHEMA or len({c["id"] for c in value["cases"]}) != len(value["cases"]):
        raise ValueError("invalid lifecycle manifest")
    families = sample.manifest()
    known = {c["id"]: c for c in families["cases"]}
    for case in value["cases"]:
        if case["kind"] not in ("cold", "mixed") or not case["members"] or len(set(case["members"])) != len(case["members"]):
            raise ValueError("invalid lifecycle case")
        if any(name not in known or case["device"] not in known[name]["devices"] for name in case["members"]):
            raise ValueError("lifecycle member unavailable on its declared backend")
        if case["kind"] == "mixed" and set(case["members"]) != {c["id"] for c in known.values() if case["device"] in c["devices"]}:
            raise ValueError("mixed campaign omits an applicable maintained family")
    return value, families, known


def _api():
    return sample.load_source(ROOT, "triad-performance.py")


def _body_hashes(path, names):
    tree = ast.parse(Path(path).read_text())
    found = {node.name: hashlib.sha256(ast.dump(node, include_attributes=False).encode()).hexdigest()
             for node in tree.body if isinstance(node, (ast.FunctionDef, ast.ClassDef)) and node.name in names}
    if set(found) != set(names):
        raise ValueError("measurement body missing")
    return found


def _driver_inventory(device):
    if device == "cpu":
        return {"status": "not_applicable"}
    result = subprocess.run(["nvidia-smi", "--query-gpu=uuid,driver_version", "--format=csv,noheader,nounits"],
                            capture_output=True, text=True, timeout=10)
    if result.returncode or not result.stdout.strip():
        raise ValueError("GPU driver/UUID inventory unavailable; lifecycle hardware applicability unqualified")
    rows = []
    for line in result.stdout.splitlines():
        if line.strip():
            identifier, driver = line.split(",", 1)
            rows.append({"uuid_sha256": hashlib.sha256(identifier.strip().encode()).hexdigest(),
                         "driver_version": driver.strip()})
    visibility = os.environ.get("CUDA_VISIBLE_DEVICES")
    return {"status": "observed", "driver_and_uuid_rows": sorted(rows, key=lambda r: r["uuid_sha256"]),
            "cuda_visible_devices_sha256": hashlib.sha256(visibility.encode()).hexdigest() if visibility is not None else None,
            "source": "nvidia-smi UUID/driver query; runtime device identity also checked in worker"}


def static_binding(roots, binaries, case, families, known, *, device, precision, threads, native_threads,
                   mixed_method="modeled", direct_cycles="6"):
    # Gate/profile/rendering changes do not change a timed loop. Full module
    # hashes remain in each report; applicability binds reviewed measurement
    # bodies and exact DSP/adapter/native inputs instead.
    selected = [known[name] for name in case["members"]]
    result = {
        "schema": SCHEMA, "case": case, "profiles": {c["fixture"]: families["fixtures"][c["fixture"]] for c in selected},
        "members": selected, "binary_sha256": {n: sha(binaries[n]) for n in SOURCES},
        "stage_sources": {n: {c["id"]: sample.stage_sources(roots[n], c) for c in selected} for n in SOURCES},
        "measurement_bodies": {
            "lifecycle": _body_hashes(WORKER, ("observe", "group_plan", "cache_environment", "_compiled_cache_files", "_api")),
            "transport": _body_hashes(WORKER.with_name("triad-performance.py"), ("ReceiverWorker", "child_environment", "stop_group")),
            "sampled": _body_hashes(WORKER.with_name("triad_perf_families.py"),
                ("serve", "native_arguments", "export_fixtures", "receive", "case_worker_setup", "complete_work_signature",
                 "load_source", "load_harness_module", "_guard_imports", "stage_sources", "validate_measurement",
                 "resolve_worker_device", "triton_policy", "remaining", "fixture_evidence", "digest", "file_sha")),
            "hardware": _body_hashes(WORKER.with_name("triad_perf_hardware.py"),
                                      ("discover_hardware", "_cpu_limits", "_cpu_features", "_read")),
        },
        "worker_dispatch": {"adapters": sample.ADAPTERS, "state": list(sample.STATE),
                            "triton_phase": list(sample.TRITON_PHASE), "other": list(sample.OTHER)},
        "model_contract": {"relative_mean_error_band": json.loads(MANIFEST.read_text())["model_relative_error"],
                           "minimum_warm_campaign_seconds": json.loads(MANIFEST.read_text())["minimum_campaign_seconds"],
                           "minimum_cycles": 2, "maximum_cycles": 256, "planning_margin": 1.25,
                           "model_bodies": _body_hashes(WORKER, ("forecast", "model_validation", "mean_interval",
                               "ratio_interval", "confirmation_policy", "sustained_evidence")),
                           "calibration_looks": list(LOOKS),
                           "confirmation_policy": confirmation_policy()},
        "device": device, "precision": precision, "threads": threads, "native_threads": native_threads,
        "gpu_driver_inventory": _driver_inventory(device),
        "timing_scope": MEASUREMENT_SCOPE,
        "cache_contract": {"policy": case["cache"], "os_page_cache": "retained",
                           "controller_already_running": True, "global_device_or_disk_cold_claim": False},
    }
    if case["kind"] == "mixed" and mixed_method == "direct":
        result["direct_target"] = {"cycle_selection": str(direct_cycles), "forecast_applied": False,
            "all_members_execute_full_public_work": True,
            "selection_body": _body_hashes(WORKER, ("direct_cycle_plan",)),
            "minimum_warm_seconds": json.loads(MANIFEST.read_text())["minimum_campaign_seconds"]}
    return result


def group_plan(members, known):
    # A three-capture CFO batch can share its first physical capture with a
    # one-capture receiver of the identical native profile.
    result = {}
    for name in members:
        case = known[name]
        result[case["fixture"]] = max(result.get(case["fixture"], 0), case["captures"])
    return result


@contextmanager
def cache_environment(cache, directory):
    previous = os.environ.get("TRITON_CACHE_DIR")
    if cache != "process-cold":
        Path(directory).mkdir(parents=True, exist_ok=True)
        os.environ["TRITON_CACHE_DIR"] = str(directory)
    try:
        yield
    finally:
        if previous is None:
            os.environ.pop("TRITON_CACHE_DIR", None)
        else:
            os.environ["TRITON_CACHE_DIR"] = previous


def _compiled_cache_files(directory):
    return sorted(str(p.relative_to(directory)) for p in Path(directory).rglob("*")
                  if p.is_file() and p.suffix in (".cubin", ".ptx", ".hsaco"))


def _environment(value, cache):
    value = json.loads(json.dumps(value))
    policy = value.get("triton_jit_policy")
    if policy:
        policy["explicit_cache_directory_sha256"] = "owned-directory-path-is-not-a-compute-regime"
        policy["disk_cache"] = cache
    return value


def observe(root, binary, case, families, known, *, cycles, seed, threads, native_threads,
            device, precision, deadline, directory, cache_directory, delay=None, label="observation"):
    """One complete physical session; every member runs at full public work."""
    api = _api()
    directory = Path(directory)
    delay = {} if delay is None else delay
    delay_actual = {}
    def pause(stage):
        seconds = delay.get(stage, 0.)
        if seconds:
            before = time.perf_counter()
            time.sleep(seconds)
            delay_actual[stage] = time.perf_counter()-before
    receipts, setup, generation, signatures, fixture = [], {}, {}, {}, []
    worker = None
    start = time.perf_counter()
    with tempfile.TemporaryDirectory(prefix="lifecycle-", dir=directory) as temp:
        scratch = Path(temp)
        cache_dir = scratch/"private-triton-cache" if case["cache"] == "private-empty-compiled" else cache_directory
        with cache_environment(case["cache"], cache_dir):
            cache_before = _compiled_cache_files(cache_dir) if case["cache"] != "process-cold" else []
            if case["cache"] == "private-empty-compiled" and cache_before:
                raise ValueError("private cold compiler cache was not empty")
            try:
                pause("startup")
                before = time.perf_counter()
                worker = api.ReceiverWorker(root, threads=threads, deadline=deadline,
                    log_directory=directory, name=f"{label}-{seed}", adapter=sample.WORKER)
                startup = time.perf_counter()-before
                for name in case["members"]:
                    setup[name] = sample.case_worker_setup(worker, known[name], device)
                paths = {}
                pause("simulation")
                for index, (profile, captures) in enumerate(group_plan(case["members"], known).items()):
                    paths[profile], generation[profile] = sample.export_fixtures(binary, families["fixtures"][profile],
                        seed+1000*index, native_threads, scratch/profile, captures, deadline)
                pause("preparation")
                first_start = time.perf_counter()
                first = []
                for name in case["members"]:
                    member = known[name]
                    call = sample.receive(worker, paths[member["fixture"]][:member["captures"]], member, device, precision)
                    signature = sample.complete_work_signature({"receiver_runs": [call]})
                    signatures[name] = signature
                    first.append({"id": name, "call": call})
                first_seconds = time.perf_counter()-first_start
                warm_start = time.perf_counter()
                cycle_seconds = []
                for cycle in range(cycles):
                    cycle_start = time.perf_counter()
                    row = []
                    for name in case["members"]:
                        member = known[name]
                        call = sample.receive(worker, paths[member["fixture"]][:member["captures"]], member, device, precision)
                        if sample.complete_work_signature({"receiver_runs": [call]}) != signatures[name]:
                            raise ValueError("mixed campaign changed a full receiver work regime")
                        row.append({"id": name, "call": call})
                    cycle_seconds.append(time.perf_counter()-cycle_start)
                    receipts.append(row)
                warm_seconds = time.perf_counter()-warm_start
                for name in case["members"]:
                    checked = worker.request({"action": "family-verify", "case": known[name], "verify_environment": True})
                    if checked.get("environment_matches_setup") is not True:
                        raise ValueError("receiver environment changed during lifecycle observation")
                pause("finalization")
                cache_after = _compiled_cache_files(cache_dir) if case["cache"] != "process-cold" else []
            finally:
                before = time.perf_counter()
                if worker is not None:
                    worker.close()
                close_seconds = time.perf_counter()-before
        # Hash completed inputs after computation and worker finalization,
        # before removing their files. Its measured cost is excluded.
        audit_start = time.perf_counter()
        fixture = {profile: sample.fixture_evidence(values) for profile, values in paths.items()}
        auditing_seconds = time.perf_counter()-audit_start
    raw_elapsed = time.perf_counter()-start
    elapsed = raw_elapsed-auditing_seconds
    environment = {name: _environment(row["environment"], case["cache"]) for name, row in setup.items()}
    return {
        "seed": seed, "cycles": cycles, "complete_seconds": elapsed,
        "fixed_seconds": elapsed-warm_seconds, "warm_seconds": warm_seconds,
        "cycle_seconds": cycle_seconds, "startup_seconds": startup, "close_seconds": close_seconds,
        "first_seconds": first_seconds, "generation": generation, "setup": setup,
        "fixture": fixture, "raw_outer_seconds": raw_elapsed, "excluded_fixture_audit_seconds": auditing_seconds,
        "first": first, "warm": receipts, "work_signature": signatures, "environment": environment,
        "cache": case["cache"], "timing_scope": MEASUREMENT_SCOPE,
        "compiled_cache": {"before_count": len(cache_before), "after_count": len(cache_after),
                           "new_artifacts": sorted(set(cache_after)-set(cache_before)),
                           "empty_at_start": not cache_before,
                           "scope": "Triton compiler artifacts; OS/driver caches retained"},
        "hashing_and_evidence_outside_target": True, "injected_delay_seconds": delay,
        "actual_injected_delay_seconds": delay_actual,
    }


def mean_interval(values, confidence):
    values = np.asarray(values, dtype=np.float64)
    if values.ndim != 1 or len(values) < 3 or not np.isfinite(values).all():
        raise ValueError("at least three finite independent outer observations required")
    point = float(np.mean(values))
    half = float(student_t.ppf((1+confidence)/2, len(values)-1)*np.std(values, ddof=1)/math.sqrt(len(values)))
    return {"mean": point, "interval": [point-half, point+half], "n": len(values)}


def confirmation_policy():
    result = model_confirmation_policy().to_dict()
    result.update(metric="arithmetic mean actual/frozen-forecast ratio; paired ratio uses paired Fieller inversion",
        scope="fixed-look arithmetic equivalence; one-sided false-equivalence spending across looks for each required component",
        intersection_union_components=["reference", "candidate", "paired"],
        false_equivalence_error_budget=model_confirmation_policy().family_alpha/2,
        simultaneous_all_component_interval_claim=False)
    return result


def ratio_interval(reference, candidate, tail_alpha):
    """Fieller interval with each endpoint spending the stated one-sided tail."""
    a, b = (np.asarray(v, dtype=np.float64) for v in (reference, candidate))
    if len(a) != len(b) or len(a) < 3 or np.any(a <= 0) or np.any(b <= 0) or not np.isfinite(a).all() or not np.isfinite(b).all():
        raise ValueError("three paired finite positive outer runtimes required")
    n = len(a)
    x, y = float(np.mean(a)), float(np.mean(b))
    scale = float(student_t.ppf(1-tail_alpha, n-1)**2/n)
    aa = x*x-scale*float(np.var(a, ddof=1))
    ratio = y/x
    residual = b-ratio*a
    # Same centered Fieller construction as the existing staged validator:
    # avoid cancellation of O(mean**4) terms for nearly identical timings.
    shifted_b = float(scale*np.cov(a, residual, ddof=1)[0, 1])
    shifted_c = -float(scale*np.var(residual, ddof=1))
    disc = shifted_b*shifted_b-aa*shifted_c
    if aa <= 0 or disc < 0 or not math.isfinite(disc):
        return None
    center = ratio-shifted_b/aa
    return [center-math.sqrt(disc)/aa, center+math.sqrt(disc)/aa]


def comparison(reference, candidate, policy, *, allowance=0.):
    n = len(reference)
    if n not in policy.looks:
        raise ValueError("comparison must be at a predeclared look")
    upper = ratio_interval(reference, candidate, policy.pass_error_budget/len(policy.looks))
    lower = ratio_interval(reference, candidate, policy.failure_error_budget/len(policy.looks))
    lo = None if lower is None else max(0., lower[0])*(1-allowance)
    hi = None if upper is None else upper[1]*(1+allowance)
    gate = ("performance_regression" if lo is not None and lo > policy.slowdown_threshold else
            "pass" if hi is not None and hi <= policy.slowdown_threshold else "inconclusive")
    return {"gate": gate, "sample_count": n, "timing_only_lower_bound": None if lower is None else lower[0],
            "timing_only_upper_bound": None if upper is None else upper[1],
            "model_adjusted_ratio_interval": [lo, hi], "engineering_model_relative_allowance": allowance,
            "pass_error_budget": policy.pass_error_budget, "failure_error_budget": policy.failure_error_budget,
            "threshold": policy.slowdown_threshold, "looks": list(policy.looks),
            "estimand": "ratio of arithmetic mean complete-target runtimes",
            "confidence_includes_unverified_model_error": False, "future_model_coverage_qualified": False}


def forecast(rows, cycles):
    values = {name: [r[name]["fixed_seconds"]+cycles*r[name]["warm_seconds"] for r in rows] for name in SOURCES}
    return {"cycles": cycles, "point_seconds": {n: statistics.fmean(v) for n, v in values.items()},
            "outer_predictions": values,
            "joint_components": "F + C*W per outer session; covariance retained and shared setup counted once",
            "timing_scope": MEASUREMENT_SCOPE}


def model_validation(rows, frozen, allowance):
    confidence = model_confirmation_policy().confidence
    times = {n: [r[n]["complete_seconds"] for r in rows] for n in SOURCES}
    checks = {}
    for name in SOURCES:
        ratio = mean_interval([v/frozen["point_seconds"][name] for v in times[name]], confidence)
        checks[name] = ratio["interval"]
    band = ratio_interval(times["reference"], times["candidate"], (1-confidence)/2)
    predicted_ratio = frozen["point_seconds"]["candidate"]/frozen["point_seconds"]["reference"]
    checks["paired"] = None if band is None else [v/predicted_ratio for v in band]
    valid = all(v is not None and 1-allowance <= v[0] <= v[1] <= 1+allowance for v in checks.values())
    invalid = any(v is not None and (v[1] < 1-allowance or v[0] > 1+allowance) for v in checks.values())
    return {"status": "validated" if valid else "invalid" if invalid else "inconclusive",
            "arithmetic_actual_over_frozen_prediction_intervals": checks,
            "allowed_relative_error": allowance, "confidence_per_confirmation_look": confidence,
            "confirmation_policy": confirmation_policy(), "n": len(rows),
            "future_prediction_coverage_claim": False}


def sustained_evidence(rows, minimum_seconds):
    results = []
    for row in rows:
        for name in SOURCES:
            values = row[name]["cycle_seconds"]
            q = max(1, len(values)//4)
            first = statistics.fmean(values[:q])
            last = statistics.fmean(values[-q:])
            results.append({"pair_id": row["pair_id"], "source": name,
                "warm_seconds": row[name]["warm_seconds"], "minimum_seconds": minimum_seconds,
                "minimum_reached": row[name]["warm_seconds"] >= minimum_seconds,
                "first_quarter_seconds_per_cycle": first, "last_quarter_seconds_per_cycle": last,
                "last_over_first_quarter": last/first, "trend_review_required": last/first > 1.10})
    return {"rows": results, "minimum_reached": all(r["minimum_reached"] for r in results),
            "trend_review_required": any(r["trend_review_required"] for r in results),
            "scope": "processor warm-cycle wall, not simulated observation duration; quarter ratio is an engineering diagnostic"}


def direct_sustained_outcomes(sustained):
    """Direct complete runtimes include observed variation; no forecast is applied.

    The unchanged 1.10 last/first diagnostic still requires stability review.
    It is neither a calibrated thermal test nor a reason to discard measured
    complete-target arithmetic means. Modeled-route guards remain separate.
    """
    return {"sustained_observation": "complete" if sustained["minimum_reached"] else "incomplete",
            "sustained_stability": "review_required" if sustained["trend_review_required"] else "no_flag_observed"}


def _comparison_call(call):
    """Order only the inspection multiset; preserve every actual regime field.

    Fresh processes may enumerate the same loaded numerical libraries in a
    different order. Paths, versions, threading limits and duplicates remain
    part of the comparison. Original observations and signatures stay intact.
    """
    return canonical_library_call(call)


def _comparison_work(observation):
    """Validate raw first-call signatures, then derive comparison signatures."""
    first = observation["first"]
    if len({member["id"] for member in first}) != len(first):
        raise ValueError("duplicate lifecycle first-call member")
    raw = {member["id"]: sample.complete_work_signature({"receiver_runs": [member["call"]]})
           for member in first}
    if observation["work_signature"] != raw:
        raise ValueError("raw lifecycle work signature contradicts first receiver calls")
    return {member["id"]: sample.complete_work_signature({"receiver_runs": [_comparison_call(member["call"])]})
            for member in first}


def _row_work(row):
    return {name: _comparison_work(row[name]) for name in SOURCES}


def comparison_contract():
    return {"schema": "triad-lifecycle-regime-comparison-v1",
            "unordered_multiset": "receiver.regime.threads.library_pools only",
            "all_fields_and_duplicates_preserved": True,
            "raw_observations_rewritten": False,
            "bodies": _body_hashes(WORKER, ("_comparison_call", "_comparison_work", "_row_work",
                                            "_pair_check", "_check_rows")),
            "shared_validator_bodies": _body_hashes(WORKER.with_name("triad_perf_family_regression.py"),
                ("canonical_library_call", "numerical_agreement", "_regimes"))}


def _pair_check(row, precision):
    for section in ("first", "warm"):
        left, right = (row[n][section] for n in SOURCES)
        if section == "first":
            left, right = [left], [right]
        if len(left) != len(right):
            raise ValueError("paired cycle counts differ")
        for a, b in zip(left, right, strict=True):
            if [v["id"] for v in a] != [v["id"] for v in b]:
                raise ValueError("paired member ordering differs")
            for aa, bb in zip(a, b, strict=True):
                numerical_agreement({"reference": {"receiver_runs": [aa["call"]]},
                                     "candidate": {"receiver_runs": [bb["call"]]}}, precision)


def _check_rows(rows, members, work, environment, cycles, precision):
    if len({r["pair_id"] for r in rows}) != len(rows):
        raise ValueError("duplicate physical lifecycle pair id")
    if not rows:
        raise ValueError("physical lifecycle observations missing")
    canonical_work = _row_work(rows[0])
    # Retained pre-correction reports name raw first-process hashes; new
    # reports name the derived multiset hashes. Both must match the actual
    # complete first call, and all following calls must preserve that work.
    if work not in (canonical_work, {n: rows[0][n]["work_signature"] for n in SOURCES}):
        raise ValueError("expected lifecycle work is not bound to the first complete observation")
    for row in rows:
        _pair_check(row, precision)
        for name in SOURCES:
            value = row[name]
            if (value["cycles"] != cycles or len(value["warm"]) != cycles or
                    len(value["cycle_seconds"]) != cycles or _comparison_work(value) != canonical_work[name] or
                    value["environment"] != environment[name] or value["complete_seconds"] <= 0 or
                    not math.isfinite(value["complete_seconds"])):
                raise ValueError("physical lifecycle work/environment or complete timing differs")
            for calls in (value["first"], *value["warm"]):
                if [r["id"] for r in calls] != members:
                    raise ValueError("physical lifecycle member ordering differs")
                for call in calls:
                    if sample.complete_work_signature({"receiver_runs": [_comparison_call(call["call"])]}) != canonical_work[name][call["id"]]:
                        raise ValueError("physical receiver receipt contradicts complete work signature")
    _physical_seeds(rows)


def _physical_seeds(rows):
    used = set()
    for row in rows:
        sources = []
        for name in SOURCES:
            current = set()
            for generated in row[name]["generation"].values():
                count, first = generated["count"], generated["seed"]
                if count != len(generated["commands"]):
                    raise ValueError("native generation count differs from executed commands")
                for offset, command in enumerate(generated["commands"]):
                    if int(command[command.index("--seed")+1]) != first+offset:
                        raise ValueError("native generation command has a different physical seed")
                    current.add(first+offset)
            sources.append(current)
        if sources[0] != sources[1] or used & sources[0]:
            raise ValueError("physical lifecycle seeds differ across sources or repeat across observations")
        used.update(sources[0])
    return used


def _validate_anchor(value):
    if not isinstance(value, dict) or value.get("schema") != SCHEMA or value.get("status") != "validated":
        return False
    try:
        if "comparison_contract" in value and value["comparison_contract"] != comparison_contract():
            return False
        if value["record_sha256"] != digest({k: v for k, v in value.items() if k != "record_sha256"}):
            return False
        frozen = value["forecast"]
        contract = value["static_binding"]["model_contract"]
        if (value["model_relative_error"] != contract["relative_mean_error_band"] or
                value["minimum_campaign_seconds"] != contract["minimum_warm_campaign_seconds"] or
                len(value["training"]) not in LOOKS or
                any(r[n]["cycles"] != 1 for r in value["training"] for n in SOURCES)):
            return False
        if len(value["full_pairs"]) not in model_confirmation_policy().looks:
            return False
        rebuilt = forecast(value["training"], frozen["cycles"])
        if any(frozen[key] != content for key, content in rebuilt.items()):
            return False
        all_rows = [*value["training"], *value["full_pairs"]]
        if (set(value["training_pair_ids"]) & {r["pair_id"] for r in value["full_pairs"]} or
                value["training_pair_ids"] != [r["pair_id"] for r in value["training"]] or
                any(_environment_signature(r) != frozen["environment"] for r in all_rows)):
            return False
        if _physical_seeds(value["training"]) & _physical_seeds(value["full_pairs"]):
            return False
        if any(r[n]["cycles"] != frozen["cycles"] for r in value["full_pairs"] for n in SOURCES):
            return False
        members = value["static_binding"]["case"]["members"]
        precision = value["static_binding"]["precision"]
        _check_rows(value["training"], members, frozen["work_signature"], frozen["environment"], 1, precision)
        _check_rows(value["full_pairs"], members, frozen["work_signature"], frozen["environment"], frozen["cycles"], precision)
        sustained = sustained_evidence(value["full_pairs"], value["minimum_campaign_seconds"])
        if sustained != value["sustained"] or not sustained["minimum_reached"] or sustained["trend_review_required"]:
            return False
        checks = [model_validation(value["full_pairs"][:n], frozen, value["model_relative_error"])
                  for n in model_confirmation_policy().looks if n <= len(value["full_pairs"])]
        return (checks == value["confirmation"] and checks[-1]["status"] == "validated" and
                all(c["status"] == "inconclusive" for c in checks[:-1]))
    except (KeyError, ValueError, TypeError, IndexError):
        return False


def _anchor_path(directory, identifier):
    return Path(directory)/(identifier+".lifecycle-anchor.json")


def _sentinel(anchor, current, full):
    """Fixed historical observation envelope plus current mean uncertainty."""
    confidence = 1-.05/(3*len(LOOKS))
    result = {}
    for name, bounds, historical in _sentinel_components(anchor, current, confidence):
        actual = (full[name]["complete_seconds"] if name in SOURCES else
                  full["candidate"]["complete_seconds"]/full["reference"]["complete_seconds"])
        envelope = max(math.log1p(anchor["model_relative_error"]), *(abs(v) for v in historical))
        interval = [math.log(actual/bounds[1]), math.log(actual/bounds[0])] if bounds is not None and bounds[0] > 0 else None
        result[name] = {"actual_over_timing_log_interval": interval, "historical_log_envelope": envelope,
                        "eligible": interval is not None and -envelope <= interval[0] <= interval[1] <= envelope}
    return {"eligible": all(v["eligible"] for v in result.values()), "sources": result,
            "scope": "one independent complete fixed campaign; historical engineering envelope, no future-tail guarantee"}


def _sentinel_components(anchor, current, confidence):
    rows = []
    for name in SOURCES:
        interval = mean_interval(current["outer_predictions"][name], confidence)["interval"]
        errors = [math.log(r[name]["complete_seconds"]/anchor["forecast"]["point_seconds"][name])
                  for r in anchor["full_pairs"]]
        rows.append((name, interval, errors))
    ratio = ratio_interval(current["outer_predictions"]["reference"], current["outer_predictions"]["candidate"], (1-confidence)/2)
    expected = anchor["forecast"]["point_seconds"]["candidate"]/anchor["forecast"]["point_seconds"]["reference"]
    errors = [math.log((r["candidate"]["complete_seconds"]/r["reference"]["complete_seconds"])/expected)
              for r in anchor["full_pairs"]]
    return [*rows, ("paired", ratio, errors)]


def sentinel_readiness(anchor, current):
    blockers = []
    confidence = 1-.05/(3*len(LOOKS))
    for name, interval, errors in _sentinel_components(anchor, current, confidence):
        envelope = max(math.log1p(anchor["model_relative_error"]), *(abs(v) for v in errors))
        if interval is None or interval[0] <= 0:
            blockers.append(name)
            continue
        half = math.log(interval[1]/interval[0])/2
        if half > envelope or half+math.log1p(anchor["model_relative_error"]) > math.log1p(.20):
            blockers.append(name)
    return {"feasible": not blockers, "timing_blockers": blockers,
            "scope": "necessary pre-endpoint uncertainty check; not acceptance or future coverage"}


def _validate_cold_anchor(value):
    if not isinstance(value, dict):
        return False
    try:
        if "comparison_contract" in value and value["comparison_contract"] != comparison_contract():
            return False
        if value.get("schema") != SCHEMA or value.get("status") != "direct_full" or value.get("kind") != "cold":
            return False
        if value["record_sha256"] != digest({k: v for k, v in value.items() if k != "record_sha256"}):
            return False
        rows = value["full_pairs"]
        _check_rows(rows, value["static_binding"]["case"]["members"], value["expected_work"],
                    value["environment"], 0, value["static_binding"]["precision"])
        policy = AdaptivePolicy(stopping_rule="decision", looks=LOOKS, **value["regression_policy"])
        times = {n: [r[n]["complete_seconds"] for r in rows] for n in SOURCES}
        if comparison(times["reference"], times["candidate"], policy) != value["comparison"]:
            return False
        if value["static_binding"]["case"]["cache"] == "private-empty-compiled" and any(
                r[n]["compiled_cache"]["before_count"] != 0 or r[n]["compiled_cache"]["after_count"] <= 0
                for r in rows for n in SOURCES):
            return False
        return (len(rows) in LOOKS and all(r[n]["cycles"] == 0 and
                    r[n]["environment"] == value["environment"][n] for r in rows for n in SOURCES))
    except (KeyError, ValueError, TypeError):
        return False


def _environment_signature(row):
    return {n: row[n]["environment"] for n in SOURCES}


def next_look_budget(completed, *, looks, maximum, remaining_seconds, estimated_pair_seconds):
    """Plan the complete next look, rather than leave unscorable extra pairs.

    This is an engineering cost estimate with a fixed safety factor. A budget
    stop is inconclusive; it is not evidence about the performance hypothesis.
    """
    next_count = next((n for n in looks if completed < n <= maximum), None)
    needed = None if next_count is None or estimated_pair_seconds is None else (
        1.2*(next_count-completed)*estimated_pair_seconds)
    return {"allowed": next_count is not None and (needed is None or needed <= remaining_seconds),
            "completed_pairs": completed, "next_look": next_count,
            "estimated_additional_seconds_with_margin": needed,
            "remaining_seconds": remaining_seconds,
            "scope": "predeclared-look scheduling estimate, not a statistical future-runtime bound"}


def direct_cycle_plan(selection, pilot, minimum_seconds):
    selection = str(selection)
    if selection == "auto":
        if pilot is None:
            raise ValueError("automatic direct cycle selection requires its discarded pilot")
        warm = {n: pilot[n]["warm_seconds"] for n in SOURCES}
        if any(not math.isfinite(v) or v <= 0 for v in warm.values()):
            raise ValueError("direct cycle pilot did not perform positive complete warm work")
        cycles = max(2, *(math.ceil(1.25*minimum_seconds/v) for v in warm.values()))
    else:
        if not selection.isdecimal():
            raise ValueError("direct cycles must be auto or an integer from 2 through 256")
        cycles, warm = int(selection), None
    if not 2 <= cycles <= 256:
        raise ValueError("direct cycle count leaves the declared range 2 through 256")
    return {"selection": selection, "cycles": cycles, "minimum_warm_seconds": minimum_seconds,
            "planning_margin": 1.25, "per_source_discarded_pilot_warm_seconds": warm,
            "frozen_before_direct_samples": True, "pilot_is_statistical_sample": False,
            "forecast_applied": False}


def run_case(case, families, known, roots, binaries, output, args, deadline):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    mixed_method = getattr(args, "mixed_method", "modeled")
    cycle_selection = str(getattr(args, "direct_cycles", "6"))
    direct_mixed = case["kind"] == "mixed" and mixed_method == "direct"
    static = static_binding(roots, binaries, case, families, known, device=args.device, precision=args.precision,
                            threads=args.threads, native_threads=args.native_threads, mixed_method=mixed_method,
                            direct_cycles=cycle_selection)
    report = {"schema": SCHEMA, "id": case["id"], "kind": case["kind"], "case": case,
              "static_binding": static, "static_binding_sha256": digest(static), "status": "unqualified",
              "prediction_valid": False, "comparison": {"gate": "inconclusive"}, "training": [],
              "full_pairs": [], "confirmation": [], "mode": args.mode, "timing_scope": MEASUREMENT_SCOPE,
              "mixed_method": mixed_method, "budget_looks": [], "comparison_contract": comparison_contract()}
    anchor_path = _anchor_path(args.anchor_directory, case["id"])
    anchor = json.loads(anchor_path.read_text()) if not direct_mixed and args.mode == "reuse" and anchor_path.is_file() else None
    if case["kind"] == "mixed" and not direct_mixed and args.mode == "reuse" and (not _validate_anchor(anchor) or anchor["static_binding"] != static):
        report["reason"] = "missing applicable independently validated fixed-campaign anchor"
        save(output/"result.json", report)
        return report
    policy = AdaptivePolicy(stopping_rule="decision", looks=LOOKS, **regression_profile(args.regression_profile))
    cache_base = Path(args.anchor_directory)/"compiled-cache"/digest(static)[:20]
    base_seed = int(digest(case)[:10], 16) + (0 if args.mode == "qualify" else 200000000)
    expected_work = None
    environment = None
    def pair(cycles, index, phase):
        nonlocal expected_work, environment
        row = {}
        seed = base_seed+index*1000000+(100000000 if phase == "validation" else 0)
        for name in SOURCES if index % 2 == 0 else SOURCES[::-1]:
            row[name] = observe(roots[name], binaries[name], case, families, known, cycles=cycles, seed=seed,
                threads=args.threads, native_threads=args.native_threads, device=args.device, precision=args.precision,
                deadline=deadline, directory=output, cache_directory=cache_base/name, label=f"{phase}-{name}")
            cache = row[name]["compiled_cache"]
            if case["cache"] == "private-empty-compiled" and cache["after_count"] == 0:
                raise ValueError("private CUDA compiler-cache observation produced no compiled artifacts")
            if case["cache"] == "persistent-compiled" and phase != "cache-precondition" and (
                    cache["before_count"] == 0 or cache["new_artifacts"]):
                raise ValueError("persistent compiled-cache target encountered a cache miss")
        # Keep complete raw observations even when validation rejects the pair.
        # Serialization remains outside both source processing timers.
        row["pair_id"] = digest({"case": case["id"], "seed": seed, "phase": phase})
        save(output/f"{phase}-{index:03d}.json", row)
        _pair_check(row, args.precision)
        work = _row_work(row)
        env = _environment_signature(row)
        if expected_work is None:
            expected_work, environment = work, env
        if work != expected_work or env != environment:
            raise ValueError("lifecycle work or environment changed across physical observations")
        return row
    if case["cache"] == "persistent-compiled":
        report["cache_precondition"] = pair(0, 0, "cache-precondition")
    if case["kind"] == "cold" or direct_mixed:
        values = {n: [] for n in SOURCES}
        cycles = 0
        planning_pair_seconds = None
        if direct_mixed:
            pilot = pair(1, 0, "direct-discarded-pilot") if cycle_selection == "auto" else None
            if pilot is not None:
                report["discarded_direct_pilot"] = pilot
            plan = direct_cycle_plan(cycle_selection, pilot, args.minimum_campaign_seconds)
            cycles = plan["cycles"]
            report["direct_work_plan"] = plan
            save(output/"direct-work-plan.json", plan)
            if pilot is not None:
                planning_pair_seconds = sum(pilot[n]["fixed_seconds"]+cycles*pilot[n]["warm_seconds"] for n in SOURCES)
                report["discarded_pilot_scheduling_estimate"] = {
                    "pair_seconds": planning_pair_seconds, "acceptance_prediction_applied": False,
                    "scope": "unqualified duration-selection/scheduling heuristic only"}
        for i in range(LOOKS[-1]):
            if i in (0, 1, *LOOKS):
                estimate = (statistics.fmean(sum(r[n]["complete_seconds"] for n in SOURCES)
                            for r in report["full_pairs"]) if report["full_pairs"] else
                            planning_pair_seconds if planning_pair_seconds is not None else
                            2*args.minimum_campaign_seconds if direct_mixed else None)
                plan = next_look_budget(i, looks=LOOKS, maximum=LOOKS[-1],
                    remaining_seconds=sample.remaining(deadline), estimated_pair_seconds=estimate)
                report["budget_looks"].append(plan)
                if not plan["allowed"]:
                    report["reason"] = "next complete direct timing look cannot fit declared budget"
                    break
            row = pair(cycles, i+1, "mixed-direct" if direct_mixed else "cold-direct")
            report["full_pairs"].append(row)
            for n in SOURCES:
                values[n].append(row[n]["complete_seconds"])
            if i+1 in LOOKS:
                report["comparison"] = comparison(values["reference"], values["candidate"], policy)
                if report["comparison"]["gate"] != "inconclusive":
                    break
        if len(report["full_pairs"]) in LOOKS:
            report.update(status="direct_full", prediction_valid=True)
        if direct_mixed:
            report.update(runtime_method="direct_full_simulation", model_contract_applied=False,
                          history_status="inconclusive_not_applied", historical_model_applied=False,
                          computational_savings={"seconds": 0., "reason": "complete targets are measured directly"})
            report["sustained"] = sustained_evidence(report["full_pairs"], args.minimum_campaign_seconds)
            report.update(direct_sustained_outcomes(report["sustained"]))
            if not report["sustained"]["minimum_reached"]:
                report.update(status="sustained_observation_incomplete", prediction_valid=False,
                              comparison={"gate": "inconclusive"})
        if case["kind"] == "cold" and args.mode == "qualify" and not args.diagnostic and report["prediction_valid"]:
            record = {"schema": SCHEMA, "id": case["id"], "kind": "cold", "status": "direct_full",
                      "static_binding": static, "environment": environment, "expected_work": expected_work,
                      "full_pairs": report["full_pairs"], "comparison": report["comparison"],
                      "regression_policy": regression_profile(args.regression_profile),
                      "comparison_contract": comparison_contract()}
            record["record_sha256"] = digest(record)
            if anchor_path.exists():
                raise ValueError("refusing to overwrite a historical cold qualification")
            save(anchor_path, record)
    else:
        if args.mode == "qualify":
            pilot = pair(1, 0, "discarded-pilot")
            cycles = max(2, *(math.ceil(1.25*args.minimum_campaign_seconds/pilot[n]["warm_seconds"]) for n in SOURCES))
            if cycles > 256:
                raise ValueError("campaign requires more than its declared cycle limit")
            report["discarded_pilot"] = pilot
        else:
            cycles = anchor["forecast"]["cycles"]
        for i in range(LOOKS[-1]):
            report["training"].append(pair(1, i+1, "training"))
            if i+1 not in LOOKS:
                continue
            frozen = forecast(report["training"], cycles)
            gate = comparison(frozen["outer_predictions"]["reference"], frozen["outer_predictions"]["candidate"],
                              policy, allowance=anchor["model_relative_error"] if anchor else args.model_relative_error)
            ready = sentinel_readiness(anchor, frozen) if anchor else {"feasible": True}
            if args.mode == "qualify" or (gate["gate"] != "inconclusive" and ready["feasible"]) or i+1 == LOOKS[-1]:
                break
        report["forecast"] = {**frozen, "work_signature": expected_work, "environment": environment}
        save(output/"forecast.json", report["forecast"])
        if anchor and (_row_work(anchor["training"][0]) != expected_work or anchor["forecast"]["environment"] != environment):
            report["reason"] = "current full work/environment differs from historical mixed model"
        elif anchor and (gate["gate"] == "inconclusive" or not ready["feasible"]):
            report["reason"] = "frozen routine decision or sentinel timing remains inconclusive"
            report["sentinel_readiness"] = ready
        else:
            maximum = 1 if anchor else args.maximum_holds
            for i in range(maximum):
                if i in (0, *model_confirmation_policy().looks):
                    estimate = (statistics.fmean(sum(r[n]["complete_seconds"] for n in SOURCES)
                                for r in report["full_pairs"]) if report["full_pairs"] else
                                sum(frozen["point_seconds"].values()))
                    plan = next_look_budget(i, looks=(1,) if anchor else model_confirmation_policy().looks,
                        maximum=maximum, remaining_seconds=sample.remaining(deadline), estimated_pair_seconds=estimate)
                    report["budget_looks"].append(plan)
                    if not plan["allowed"]:
                        report["reason"] = "next independent full validation look cannot fit declared budget"
                        break
                row = pair(cycles, i+1, "validation")
                report["full_pairs"].append(row)
                if anchor:
                    report["sentinel"] = _sentinel(anchor, frozen, row)
                    if report["sentinel"]["eligible"]:
                        report.update(status="historically_supported_conditional", prediction_valid=True,
                                      comparison=gate, historical_anchor_sha256=anchor["record_sha256"])
                    break
                if i+1 in model_confirmation_policy().looks:
                    check = model_validation(report["full_pairs"], frozen, args.model_relative_error)
                    report["confirmation"].append(check)
                    if check["status"] != "inconclusive":
                        report["status"] = check["status"]
                        report["prediction_valid"] = check["status"] == "validated"
                        break
            report["sustained"] = sustained_evidence(report["full_pairs"], args.minimum_campaign_seconds)
            if not report["sustained"]["minimum_reached"] or report["sustained"]["trend_review_required"]:
                report["status"] = "sustained_evidence_requires_review"
                report["prediction_valid"] = False
                report["comparison"] = {"gate": "inconclusive"}
            if report["status"] == "validated":
                report["comparison"] = gate
                record = {"schema": SCHEMA, "id": case["id"], "status": "validated", "static_binding": static,
                          "forecast": report["forecast"], "full_pairs": report["full_pairs"],
                          "training": report["training"], "training_pair_ids": [r["pair_id"] for r in report["training"]],
                          "minimum_campaign_seconds": args.minimum_campaign_seconds, "sustained": report["sustained"],
                          "confirmation": report["confirmation"], "model_relative_error": args.model_relative_error}
                record["comparison_contract"] = comparison_contract()
                record["record_sha256"] = digest(record)
                if anchor_path.exists() and not args.diagnostic:
                    raise ValueError("refusing to overwrite a historical lifecycle anchor")
                if not args.diagnostic:
                    save(anchor_path, record)
    report["environment"] = environment
    report["expected_work"] = expected_work
    report["independent_validation"] = {
        "full_target_pairs": len(report["full_pairs"]), "forecast_frozen_before_validation": case["kind"] == "mixed" and not direct_mixed,
        "physical_seed_sets_disjoint": True, "work_regime_checked": True,
        "ordinary_page_cache_retained": True, "controller_startup_included": False,
        "complete_processing_target_includes_receiver_startup": True,
    }
    report["source_binding_sha256"] = digest({"static": static, "environment": environment, "work": expected_work})
    report["diagnostic"] = args.diagnostic
    if args.diagnostic:
        report.update(status="diagnostic", prediction_valid=False, comparison={"gate": "inconclusive"})
    if static_binding(roots, binaries, case, families, known, device=args.device, precision=args.precision,
                      threads=args.threads, native_threads=args.native_threads, mixed_method=mixed_method,
                      direct_cycles=cycle_selection) != static:
        raise ValueError("source, native binary or measurement body changed during lifecycle run")
    save(output/"result.json", report)
    return report


def validate_routine_report(report, context):
    """Lightweight wrapper guard; no worker, simulation or numerical kernel."""
    errors, unqualified, regressions, inconclusive = [], [], [], []
    source_transfer = None
    reviewed_direct_reclassification = False
    derived_decisions, stability_cases, observation_cases, case_outcomes = {}, [], [], {}
    required, cold_passed, cold_gates, cold_errors = [], [], {}, []
    header_valid, private_cold_valid, current_case = False, False, None
    try:
        if report.get("schema") != SCHEMA or report.get("mode") != "reuse" or report.get("diagnostic"):
            raise ValueError("unsupported lifecycle routine schema/mode or diagnostic evidence")
        if not isinstance(report.get("errors"), list) or report["errors"]:
            raise ValueError("child lifecycle measurement recorded an error or lacks its completion ledger")
        if report.get("status") not in ("passed", "failed", "incomplete"):
            raise ValueError("lifecycle child did not produce a completed status")
        mixed_method = context.get("mixed_method", "modeled")
        if report.get("protocol_sha256") != sha(WORKER) or report.get("manifest_sha256") != sha(MANIFEST):
            # One reviewed transfer: protocol35's GPU path is unchanged by
            # the explicit CPU direct route and the look-budget scheduler.
            # Every current timed/model body, source/work/environment and
            # complete raw proof is still reconstructed below.
            if (context["device"] == "cuda" and mixed_method == "modeled" and
                    report.get("protocol_sha256") == REVIEWED_GPU_PROTOCOL35 and
                    report.get("manifest_sha256") == REVIEWED_GPU_MANIFEST35):
                source_transfer = {"kind": "reviewed_protocol35_gpu_modeled_path",
                    "old_protocol_sha256": REVIEWED_GPU_PROTOCOL35, "current_protocol_sha256": sha(WORKER),
                    "old_manifest_sha256": REVIEWED_GPU_MANIFEST35, "current_manifest_sha256": sha(MANIFEST),
                    "basis": "unchanged current timed/model bodies and complete raw proof replay; CPU route metadata and scheduling only"}
            elif (context["device"] == "cpu" and mixed_method == "direct" and
                    report.get("protocol_sha256") == REVIEWED_DIRECT_PROTOCOL5DFA and
                    report.get("manifest_sha256") == REVIEWED_DIRECT_MANIFEST5DFA):
                reviewed_direct_reclassification = True
                source_transfer = {"kind": "reviewed_protocol5dfa_cpu_direct_outcome_separation",
                    "old_protocol_sha256": REVIEWED_DIRECT_PROTOCOL5DFA, "current_protocol_sha256": sha(WORKER),
                    "old_manifest_sha256": REVIEWED_DIRECT_MANIFEST5DFA, "current_manifest_sha256": sha(MANIFEST),
                    "raw_report_sha256": digest(report), "raw_report_rewritten": False,
                    "raw_report_hash_encoding": "canonical JSON via sample.digest; wrapper retains byte-file identity",
                    "basis": "unchanged full timing/work/model bodies; recompute direct arithmetic-mean decision while retaining the unchanged 1.10 stability-review diagnostic"}
            elif (report.get("protocol_sha256") == REVIEWED_POOL_ENUM_PROTOCOL93DE and
                    report.get("manifest_sha256") == sha(MANIFEST)):
                source_transfer = {"kind": "reviewed_protocol93de_library_inspection_order",
                    "old_protocol_sha256": REVIEWED_POOL_ENUM_PROTOCOL93DE,
                    "current_protocol_sha256": sha(WORKER),
                    "raw_report_sha256": digest(report), "raw_report_rewritten": False,
                    "comparison_contract": comparison_contract(),
                    "basis": "exact current timed/model/static bindings and raw complete-work replay; only numerical-library multiset enumeration is canonicalized in derived validation copies"}
            else:
                raise ValueError("lifecycle protocol or manifest differs from completed child")
        elif report.get("comparison_contract") != comparison_contract():
            raise ValueError("lifecycle comparison contract missing or changed")
        if report.get("mixed_method", "modeled") != mixed_method:
            raise ValueError("child mixed measurement method differs from selected route")
        if mixed_method == "direct" and report.get("direct_cycles_selection") != str(context.get("direct_cycles", "6")):
            raise ValueError("child direct work selection differs from the selected input")
        config, families, known = load_manifest()
        device = context["device"]
        required = [c for c in config["cases"] if c["device"] == device and c["routine"]]
        actual = {r["id"]: r for r in report["cases"]}
        if set(actual) != {c["id"] for c in required} or len(actual) != len(report["cases"]):
            errors.append("lifecycle case set is incomplete or duplicated")
        roots = {"candidate": Path(context["candidate_root"]), "reference": Path(context["reference_root"])}
        binaries = {"candidate": Path(context["binary"]), "reference": Path(context["reference_binary"])}
        policy = AdaptivePolicy(stopping_rule="decision", looks=LOOKS, **regression_profile(context["regression_profile"]))
        if report.get("regression_profile") != context["regression_profile"] or report.get("regression_policy") != regression_profile(context["regression_profile"]):
            raise ValueError("child profile differs from selected regression decision")
        header_valid = not errors
        for case in required:
            current_case = case
            case_error_start = len(errors)
            case_outcomes[case["id"]] = "unqualified"
            row = actual[case["id"]]
            binding = static_binding(roots, binaries, case, families, known, device=device, precision=context["precision"],
                                     threads=context["threads"], native_threads=context["native_threads"], mixed_method=mixed_method,
                                     direct_cycles=context.get("direct_cycles", "6"))
            if row["static_binding"] != binding or row["static_binding_sha256"] != digest(binding):
                errors.append(case["id"]+": current source/build/workload binding mismatch")
            legacy_erased_direct = (reviewed_direct_reclassification and case["kind"] == "mixed" and
                row.get("status") == "sustained_evidence_requires_review" and
                row.get("prediction_valid") is False and row.get("comparison") == {"gate": "inconclusive"})
            if not row["prediction_valid"] and not legacy_erased_direct:
                unqualified.append(case["id"]+": "+row.get("reason", row.get("status", "unqualified")))
                if case["kind"] == "cold":
                    cold_errors.append(case["id"]+": complete cold measurement is unqualified")
                continue
            if row["source_binding_sha256"] != digest({"static": binding, "environment": row["environment"], "work": row["expected_work"]}):
                errors.append(case["id"]+": observation binding digest mismatch")
            if case["kind"] == "mixed" and mixed_method == "direct":
                if (device != "cpu" or row.get("runtime_method") != "direct_full_simulation" or
                    row.get("historical_model_applied") is not False or row.get("history_status") != "inconclusive_not_applied" or
                    row.get("model_contract_applied") is not False or row.get("computational_savings", {}).get("seconds") != 0. or
                    "forecast" in row or row.get("historical_anchor_sha256")):
                    errors.append(case["id"]+": direct measurement improperly claims a forecast or savings")
                pilot = row.get("discarded_direct_pilot")
                plan = direct_cycle_plan(context.get("direct_cycles", "6"), pilot, config["minimum_campaign_seconds"])
                if plan != row.get("direct_work_plan"):
                    errors.append(case["id"]+": direct full work selection does not replay")
                if pilot is not None:
                    _check_rows([pilot], case["members"], row["expected_work"], row["environment"], 1, context["precision"])
                    if _physical_seeds([pilot]) & _physical_seeds(row["full_pairs"]):
                        errors.append(case["id"]+": direct timing samples reused discarded pilot seeds")
                _check_rows(row["full_pairs"], case["members"], row["expected_work"], row["environment"], plan["cycles"], context["precision"])
                times = {n: [r[n]["complete_seconds"] for r in row["full_pairs"]] for n in SOURCES}
                expected = comparison(times["reference"], times["candidate"], policy)
                sustained = sustained_evidence(row["full_pairs"], config["minimum_campaign_seconds"])
                if sustained != row.get("sustained") or not sustained["minimum_reached"]:
                    errors.append(case["id"]+": direct full mixed processing did not establish sustained scope")
                direct_outcomes = direct_sustained_outcomes(sustained)
                if not reviewed_direct_reclassification and any(row.get(key) != value for key, value in direct_outcomes.items()):
                    errors.append(case["id"]+": separate direct sustained outcomes do not replay")
                if legacy_erased_direct and not sustained["trend_review_required"]:
                    errors.append(case["id"]+": historical erased direct decision lacks its declared stability flag")
                if sustained["minimum_reached"]:
                    observation_cases.append(case["id"])
                if sustained["trend_review_required"]:
                    stability_cases.append(case["id"])
                if legacy_erased_direct:
                    derived_decisions[case["id"]] = {"comparison": expected, **direct_outcomes,
                        "old_status": row["status"], "old_comparison": row["comparison"],
                        "full_pair_count": len(row["full_pairs"]), "historical_model_applied": False,
                        "scope": "derived classification of preserved complete raw observations; no new measurement"}
            elif case["kind"] == "mixed":
                anchor_path = _anchor_path(context["anchor_directory"], case["id"])
                anchor = json.loads(anchor_path.read_text())
                if not _validate_anchor(anchor) or anchor["record_sha256"] != row.get("historical_anchor_sha256") or anchor["static_binding"] != binding:
                    errors.append(case["id"]+": historical prediction proof missing or changed")
                    continue
                rebuilt = forecast(row["training"], anchor["forecast"]["cycles"])
                if any(row["forecast"][key] != value for key, value in rebuilt.items()):
                    errors.append(case["id"]+": current frozen forecast does not replay")
                _check_rows(row["training"], case["members"], row["expected_work"], row["environment"], 1, context["precision"])
                _check_rows(row["full_pairs"], case["members"], row["expected_work"], row["environment"], rebuilt["cycles"], context["precision"])
                if _physical_seeds(row["training"]) & _physical_seeds(row["full_pairs"]):
                    errors.append(case["id"]+": current validation reused calibration physical seeds")
                expected = comparison(rebuilt["outer_predictions"]["reference"], rebuilt["outer_predictions"]["candidate"], policy,
                                      allowance=anchor["model_relative_error"])
                if (not sentinel_readiness(anchor, rebuilt)["feasible"] or len(row["full_pairs"]) != 1 or
                    _sentinel(anchor, rebuilt, row["full_pairs"][0]) != row.get("sentinel") or not row.get("sentinel", {}).get("eligible")):
                    errors.append(case["id"]+": independent complete campaign sentinel missing")
                sustained = sustained_evidence(row["full_pairs"], config["minimum_campaign_seconds"])
                if sustained != row.get("sustained") or not sustained["minimum_reached"] or sustained["trend_review_required"]:
                    errors.append(case["id"]+": actual sustained campaign evidence is unresolved")
                elif sustained["minimum_reached"]:
                    observation_cases.append(case["id"])
            else:
                _check_rows(row["full_pairs"], case["members"], row["expected_work"], row["environment"], 0, context["precision"])
                times = {n: [r[n]["complete_seconds"] for r in row["full_pairs"]] for n in SOURCES}
                expected = comparison(times["reference"], times["candidate"], policy)
            if expected != row["comparison"] and not legacy_erased_direct:
                errors.append(case["id"]+": current regression decision does not replay")
            gate = expected["gate"]
            if gate == "performance_regression":
                regressions.append(case["id"])
            elif gate != "pass":
                inconclusive.append(case["id"])
            if not row["independent_validation"]["work_regime_checked"] or not row["full_pairs"]:
                errors.append(case["id"]+": completed work evidence missing")
            case_outcomes[case["id"]] = ("unqualified" if len(errors) != case_error_start else
                "failed" if gate == "performance_regression" else "passed" if gate == "pass" else "inconclusive")
            if case["kind"] == "cold":
                cold_gates[case["id"]] = gate
                if len(errors) == case_error_start and gate == "pass":
                    cold_passed.append(case["id"])
                cold_errors.extend(errors[case_error_start:])
        if device == "cuda":
            private = next(c for c in config["cases"] if c["id"] == "cold-cuda-private")
            current_case = private
            private_error_start, private_unqualified_start = len(errors), len(unqualified)
            binding = static_binding(roots, binaries, private, families, known, device=device, precision=context["precision"],
                                     threads=context["threads"], native_threads=context["native_threads"])
            path = _anchor_path(context["anchor_directory"], private["id"])
            cold = json.loads(path.read_text()) if path.is_file() else None
            current = actual["cold-cuda-persistent"]
            if not _validate_cold_anchor(cold) or cold["static_binding"] != binding:
                errors.append("source-bound private-cache GPU cold qualification missing")
            else:
                expected_env = {n: {key: _environment(env, private["cache"]) for key, env in current["environment"][n].items()}
                                for n in SOURCES}
                if cold["environment"] != expected_env or cold["expected_work"] != current["expected_work"]:
                    errors.append("private-cache cold qualification has different current hardware or work")
                old_profile = cold["regression_policy"]
                if (old_profile["pass_alpha"] > policy.pass_error_budget or
                        old_profile["slowdown_threshold"] > policy.slowdown_threshold or cold["comparison"]["gate"] != "pass"):
                    unqualified.append("private-cache cold qualification did not establish the selected pass criterion")
            private_cold_valid = len(errors) == private_error_start and len(unqualified) == private_unqualified_start
            cold_errors.extend(errors[private_error_start:])
            cold_errors.extend(unqualified[private_unqualified_start:])
    except (KeyError, TypeError, ValueError, OSError) as error:
        errors.append(type(error).__name__+": "+str(error))
        if current_case is None or current_case["kind"] == "cold":
            cold_errors.append(errors[-1])
    status = ("unqualified" if errors or unqualified else "failed" if regressions else
              "inconclusive" if inconclusive else "passed")
    if status == "passed" and report.get("status") != "passed" and not (
            reviewed_direct_reclassification and report.get("status") == "incomplete" and derived_decisions):
        errors.append("child did not complete with a passing status despite replayable rows")
        status = "unqualified"
    cold_ids = {c["id"] for c in required if c["kind"] == "cold"}
    cold_complete = (header_valid and bool(cold_ids) and cold_ids == set(cold_passed) and not cold_errors and
                     (context.get("device") != "cuda" or private_cold_valid))
    cold_status = ("passed" if cold_complete else "failed" if "performance_regression" in cold_gates.values() else
                   "inconclusive" if "inconclusive" in cold_gates.values() else "unqualified")
    mixed_ids = {c["id"] for c in required if c["kind"] == "mixed"}
    mixed_states = {case_outcomes.get(name, "unqualified") for name in mixed_ids}
    mixed_status = ("unqualified" if not header_valid or not mixed_ids or "unqualified" in mixed_states else
                    "failed" if "failed" in mixed_states else "inconclusive" if "inconclusive" in mixed_states else "qualified")
    observation_status = "complete" if mixed_ids and mixed_ids == set(observation_cases) else "unresolved"
    review_required = ([{"id": "sustained_stability", "case_ids": stability_cases,
        "basis": "unchanged last/first warm-cycle diagnostic exceeds 1.10; direct full timings already include variation",
        "release_review_required": True}] if stability_cases else [])
    return {"valid": not errors, "status": status, "errors": errors,
            "gap_ids": ["scheduler.concurrency-and-batches"] if status == "passed" else [],
            "cold_latency_qualified": cold_complete, "source_transfer": source_transfer,
            "cold_validation": {"valid": header_valid and not cold_errors, "status": cold_status,
                                "case_ids": sorted(cold_ids), "errors": cold_errors},
            "review_required": review_required, "derived_decisions": derived_decisions,
            "outcomes": {"cold": "qualified" if cold_complete else cold_status,
                         "fixed_mixed_campaign": mixed_status, "case_status": case_outcomes,
                         "sustained_observation": observation_status,
                         "sustained_stability": "review_required" if stability_cases else "no_flag_observed" if observation_status == "complete" else "unresolved",
                         "regressions": regressions, "inconclusive": inconclusive, "unqualified": unqualified}}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-root", type=Path, default=ROOT)
    parser.add_argument("--reference-root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--reference-binary", type=Path, required=True)
    parser.add_argument("--anchor-directory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--device", choices=("cpu", "cuda"), default="cpu")
    parser.add_argument("--precision", choices=("fp32", "fp64"), default="fp32")
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--native-threads", type=int, default=1)
    parser.add_argument("--mode", choices=("qualify", "reuse"), default="reuse")
    parser.add_argument("--mixed-method", choices=("modeled", "direct"), default="modeled",
                        help="explicit CPU direct full mixed target; does not qualify an inconclusive forecast")
    parser.add_argument("--direct-cycles", default="6",
                        help="direct CPU full cycles: explicit integer2..256 or auto from an independent discarded pilot")
    parser.add_argument("--regression-profile", choices=("normal", "ci", "strict"), default="normal")
    parser.add_argument("--budget-seconds", type=float, default=1500.)
    parser.add_argument("--minimum-campaign-seconds", type=float, default=60.)
    parser.add_argument("--maximum-holds", type=int, choices=(3, 5, 9, 17), default=9)
    parser.add_argument("--model-relative-error", type=float, default=.10)
    parser.add_argument("--diagnostic", action="store_true", help="allow weaker experiment limits but create no qualification or anchor")
    parser.add_argument("--case", action="append")
    args = parser.parse_args(argv)
    if not 0 < args.budget_seconds <= 28800 or not 0 < args.minimum_campaign_seconds <= 3600 or not 0 < args.model_relative_error < 1:
        parser.error("invalid declared timing or model limits")
    config, families, known = load_manifest()
    if args.device != "cpu" and args.mixed_method != "modeled":
        parser.error("the explicit direct mixed route is defined for CPU only")
    if args.direct_cycles != "auto":
        try:
            direct_cycle_plan(args.direct_cycles, None, args.minimum_campaign_seconds)
        except ValueError as error:
            parser.error(str(error))
    if not args.diagnostic and (args.minimum_campaign_seconds != config["minimum_campaign_seconds"] or
                               args.model_relative_error != config["model_relative_error"]):
        parser.error("full-scope limits must match the manifest; use --diagnostic for a nonqualifying experiment")
    available = [c for c in config["cases"] if c["device"] == args.device and (args.mode == "qualify" or c["routine"])]
    if args.case and set(args.case)-{c["id"] for c in available}:
        parser.error("unknown or unavailable lifecycle case")
    selected = [c for c in available if not args.case or c["id"] in args.case]
    args.output.mkdir(parents=True, exist_ok=False)
    args.anchor_directory.mkdir(parents=True, exist_ok=True)
    roots = {"reference": args.reference_root.resolve(), "candidate": args.candidate_root.resolve()}
    binaries = {"reference": args.reference_binary.resolve(), "candidate": args.binary.resolve()}
    report = {"schema": SCHEMA, "mode": args.mode, "device": args.device,
              "mixed_method": args.mixed_method,
              "direct_cycles_selection": str(args.direct_cycles),
              "regression_profile": args.regression_profile, "regression_policy": regression_profile(args.regression_profile),
              "selected_case_ids": [c["id"] for c in selected], "cases": [], "errors": [],
              "diagnostic": args.diagnostic,
              "protocol_sha256": sha(WORKER), "manifest_sha256": sha(MANIFEST), "status": "running",
              "comparison_contract": comparison_contract()}
    report["limitations"] = [
        "Paired source comparisons can cancel common startup, import, storage and scheduling slowdowns.",
        "Arithmetic hardware probes do not establish absolute cold-import or disk health.",
        "Cold means a fresh receiver process with retained OS/driver caches; no global disk/device cold claim.",
        "Fixed serial mixed campaigns do not predict the random stopping cost of an adaptive regression suite.",
        "Historical model envelopes are engineering applicability conditions, not proven future-error bounds.",
        "Direct complete-runtime inference does not establish sustained hardware stability; unchanged warm-cycle flags require separate review.",
    ]
    start = time.perf_counter()
    deadline = time.monotonic()+args.budget_seconds
    try:
        for case in selected:
            report["cases"].append(run_case(case, families, known, roots, binaries, args.output/case["id"], args, deadline))
    except Exception as error:
        report["errors"].append(type(error).__name__+": "+str(error))
    if report["protocol_sha256"] != sha(WORKER) or report["manifest_sha256"] != sha(MANIFEST):
        report["errors"].append("lifecycle protocol changed during measurement")
    report["wall_seconds"] = time.perf_counter()-start
    report["status"] = "passed" if not report["errors"] and all(r["prediction_valid"] and r["comparison"]["gate"] == "pass" for r in report["cases"]) else "incomplete"
    routine_ids = {c["id"] for c in config["cases"] if c["device"] == args.device and c["routine"]}
    if args.mode == "reuse":
        context = {"candidate_root": roots["candidate"], "reference_root": roots["reference"],
                   "binary": binaries["candidate"], "reference_binary": binaries["reference"],
                   "device": args.device, "precision": args.precision, "threads": args.threads,
                   "native_threads": args.native_threads, "regression_profile": args.regression_profile,
                   "anchor_directory": args.anchor_directory, "mixed_method": args.mixed_method,
                   "direct_cycles": str(args.direct_cycles)}
        report["routine_validation"] = validate_routine_report(report, context)
        complete = report["routine_validation"]["status"] == "passed"
        cold_complete = report["routine_validation"]["cold_latency_qualified"]
        report["review_required"] = report["routine_validation"]["review_required"]
        if not complete:
            report["status"] = "failed" if report["routine_validation"]["status"] == "failed" else "incomplete"
            report["errors"].extend(report["routine_validation"]["errors"])
    else:
        complete = report["status"] == "passed" and {c["id"] for c in selected} == {c["id"] for c in available}
        required_cold = {c["id"] for c in available if c["kind"] == "cold"}
        passed_cold = {c["id"] for c in report["cases"] if c["kind"] == "cold" and
                       c["prediction_valid"] and c["comparison"]["gate"] == "pass"}
        cold_complete = bool(required_cold) and required_cold == passed_cold and not report["errors"]
        stability_cases = [c["id"] for c in report["cases"] if c.get("sustained_stability") == "review_required"]
        report["review_required"] = ([{"id": "sustained_stability", "case_ids": stability_cases,
            "basis": "unchanged last/first warm-cycle diagnostic exceeds 1.10; direct full timings already include variation",
            "release_review_required": True}] if stability_cases else [])
    report["cold_latency_qualification"] = "qualified" if cold_complete else "unqualified"
    save(args.output/"summary.json", report)
    print(f"Triad lifecycle: {report['status']}; {report['wall_seconds']:.3f}s; {args.output/'summary.json'}")
    return 0 if report["status"] == "passed" else 2


if __name__ == "__main__":
    raise SystemExit(main())
