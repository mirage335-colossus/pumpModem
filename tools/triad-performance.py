#!/usr/bin/env python3
"""Bounded, paired performance regression study for the isolated triad receiver.

The immutable reference and candidate run in separate persistent processes.
Native duration siblings preserve complete streaming execution; FFT abbreviation
retains the full observation and arrival lattice. Independent full observations
validate each supported model. Ordinary Robust remains outside this suite.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import queue
import re
import signal
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
BASELINE = "242ea15fa032ee117b9113a7f3cd07cc6e9f0254"
SOURCE_FILES = (
    "src/crypto.cpp", "include/datapump/crypto.hpp",
    "src/serpent.cpp", "include/datapump/serpent.hpp", "tools/triad_fixture_key.hpp",
    "include/datapump/triad_modem_v3.hpp", "src/channel_perturbation.hpp",
    "src/channel_spread.hpp", "src/triad_gaussian_fading.hpp", "src/triad_modem_v3.cpp",
    "src/triad_receiver_v3.cpp", "src/triad_simulation_stream.cpp", "src/tuning.cpp",
    "src/tuning_presets.cpp", "tools/triad-long.cpp", "tools/triad_fft_acquire.py",
    "tools/triad_gaussian_state.py", "tools/triad_gaussian_window.py",
    "tools/triad_group_acquire.py", "tools/triad_noise_likelihood.py", "tools/triad_state_workspace.py")
SCHEMA = "triad-performance-v2"
OPTIONAL_HISTORICAL_FILES = frozenset(("src/serpent.cpp", "include/datapump/serpent.hpp",
                                       "tools/triad_fixture_key.hpp"))


class SuiteError(RuntimeError):
    pass


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".writing")
    with tmp.open("w") as f:
        json.dump(value, f, indent=2, allow_nan=False)
        f.write("\n")
    os.replace(tmp, path)


def remaining(deadline):
    result = deadline - time.monotonic()
    if result <= 0:
        raise TimeoutError("triad performance budget exhausted")
    return result


def extract_baseline(repository, ref, destination):
    """Export only named receiver sources from an immutable commit, without checkout."""
    if not re.fullmatch(r"[0-9a-f]{40}", ref):
        raise SuiteError("baseline-ref must be a complete immutable 40-character commit SHA")
    command = ["git", "-C", str(repository), "rev-parse", "--verify", ref + "^{commit}"]
    resolved = subprocess.run(command, capture_output=True, text=True, timeout=10, check=True).stdout.strip()
    if resolved != ref:
        raise SuiteError("baseline-ref did not resolve to its exact commit")
    hashes = {}
    for name in SOURCE_FILES:
        if name in OPTIONAL_HISTORICAL_FILES:
            present = subprocess.run(["git", "-C", str(repository), "ls-tree", "--name-only", ref, "--", name],
                                     capture_output=True, text=True, timeout=10, check=True).stdout.strip()
            if not present:
                hashes[name] = "absent"
                continue
        payload = subprocess.run(["git", "-C", str(repository), "show", ref + ":" + name],
                                 capture_output=True, timeout=10, check=True).stdout
        target = Path(destination) / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
        hashes[name] = hashlib.sha256(payload).hexdigest()
    if hashes.get("tools/triad_fixture_key.hpp") == "absent":
        driver = (Path(destination) / "tools/triad-long.cpp").read_text()
        if "triad_fixture_key.hpp" in driver:
            raise SuiteError("historical triad driver includes missing fixture key header")
    serpent = [hashes[name] == "absent" for name in ("src/serpent.cpp", "include/datapump/serpent.hpp")]
    if any(serpent) and not all(serpent):
        raise SuiteError("historical source has incomplete Serpent implementation")
    if all(serpent):
        for name in SOURCE_FILES:
            path = Path(destination) / name
            if path.suffix in (".cpp", ".hpp", ".h", ".cc") and path.is_file() and "serpent.hpp" in path.read_text():
                raise SuiteError("historical source includes missing Serpent header")
        cmake = subprocess.run(["git", "-C", str(repository), "show", ref + ":CMakeLists.txt"],
                               capture_output=True, timeout=10, check=True).stdout
        hashes["CMakeLists.txt"] = hashlib.sha256(cmake).hexdigest()
        if b"serpent.cpp" in cmake:
            raise SuiteError("historical build requires missing Serpent source")
    return {"commit": ref, "source_sha256": hashes}


def child_environment(threads):
    env = dict(os.environ)
    for name in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS",
                 "NUMEXPR_NUM_THREADS", "VECLIB_MAXIMUM_THREADS"):
        env[name] = str(threads)
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    return env


def stop_group(process):
    # The group can outlive its leader. Always escalate for orphan descendants,
    # including children that ignore TERM; waiting only for the leader is unsafe.
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        process.wait()
        return
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait(timeout=5)


def supervise(command, *, timeout, stdout, stderr, env=None, cwd=None):
    """A process-group deadline also covers grandchildren blocked in native code."""
    if os.name != "posix":
        raise SuiteError("hard descendant cleanup currently requires a POSIX runner")
    with open(stdout, "wb") as out, open(stderr, "wb") as err:
        process = subprocess.Popen(command, stdout=out, stderr=err, env=env, cwd=cwd,
                                   start_new_session=True)
        try:
            return process.wait(timeout=timeout)
        except BaseException:
            stop_group(process)
            raise
        finally:
            # A failed startup can leave a receiver child before its handle was
            # registered. The backend's group remains ours after leader exit.
            stop_group(process)


class ReceiverWorker:
    """One source revision per interpreter; no receiver modules enter the harness."""
    def __init__(self, root, *, threads, deadline, log_directory, name, adapter=None):
        self.deadline = deadline
        log_directory = Path(log_directory)
        log_directory.mkdir(parents=True, exist_ok=True)
        self.log = (log_directory / (name + ".stderr.log")).open("w")
        command = [sys.executable, str(adapter or ROOT / "tools/triad_perf_receiver.py"),
                   "--serve", "--source-root", str(root), "--threads", str(threads)]
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=self.log, text=True, bufsize=1,
                                        env=child_environment(threads))
        self.responses = queue.Queue()
        def reader():
            try:
                for line in self.process.stdout:
                    self.responses.put(line)
            finally:
                self.responses.put(None)
        self.reader = threading.Thread(target=reader, daemon=True)
        self.reader.start()
        started = time.perf_counter()
        try:
            self.identity = self.request({"action": "hello"})
        except BaseException:
            self.close()
            raise
        self.external_startup_seconds = time.perf_counter() - started

    def request(self, request):
        remaining(self.deadline)
        if self.process.poll() is not None:
            raise SuiteError("receiver worker exited before request")
        self.process.stdin.write(json.dumps(request, allow_nan=False) + "\n")
        self.process.stdin.flush()
        try:
            line = self.responses.get(timeout=remaining(self.deadline))
        except queue.Empty as e:
            raise TimeoutError("receiver worker exceeded suite deadline") from e
        if line is None:
            raise SuiteError("receiver worker closed its protocol unexpectedly")
        try:
            response = json.loads(line)
        except (ValueError, TypeError) as e:
            raise SuiteError("invalid receiver JSONL response") from e
        if response.get("ok") is not True or not isinstance(response.get("result"), dict):
            raise SuiteError("receiver request failed: " + str(response.get("error", "malformed result")))
        return response["result"]

    def close(self):
        try:
            if self.process.poll() is None and self.deadline > time.monotonic():
                self.request({"action": "close"})
                self.process.stdin.close()
                self.process.wait(timeout=min(2., remaining(self.deadline)))
        except (OSError, TimeoutError, subprocess.TimeoutExpired, SuiteError):
            pass
        finally:
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=5)
            self.process.stdout.close()
            if not self.process.stdin.closed:
                self.process.stdin.close()
            self.reader.join(timeout=1)
            self.log.close()


def frequency_subset(values, count):
    """Fixed center/endpoints and evenly spaced unique alternatives, without data."""
    if not isinstance(count, int) or isinstance(count, bool) or not 1 <= count <= len(values):
        raise ValueError("invalid CFO subset count")
    if len(set(values)) != len(values) or not all(math.isfinite(x) for x in values):
        raise ValueError("CFO grid must be finite and unique")
    if count == 1:
        return [min(values, key=abs)]
    indices = {round(i * (len(values) - 1) / (count - 1)) for i in range(count)}
    if len(indices) != count:
        raise SuiteError("CFO subset construction lost alternatives")
    return [values[i] for i in sorted(indices)]


def prepare_request(case, precision, device):
    return {"action": "prepare", "input_directory": case["input_directory"],
            "lag_max_seconds": case["lag_max_seconds"],
            "lag_step_samples": case.get("lag_step_samples", 1), "precision": precision,
            "device": device, "memory_mib": case.get("memory_mib", 1024),
            "group_tile": case.get("group_tile", 128)}


def score_request(case, count):
    lag_count = case["configuration"]["lag_group_count"]
    return {"action": "score", "frequency_offsets_hz": frequency_subset(case["frequency_offsets_hz"], count),
            "full_group_count_for_reporting": lag_count * len(case["frequency_offsets_hz"]),
            "group_tile": case.get("group_tile", 128)}


def numeric_agreement(reference, candidate, *, gaussian=False):
    """Qualification of this fixed corpus, not an operational sensitivity claim."""
    a = reference["score_summary"]["scores"] if gaussian else reference["bit_log_likelihoods"]
    b = candidate["score_summary"]["scores"] if gaussian else candidate["bit_log_likelihoods"]
    if not a or not b or len(a) != len(b):
        return {"eligible": False, "reason": "missing numerical reference scores"}
    if not all(isinstance(x, (int, float)) and math.isfinite(x) for x in a + b):
        return {"eligible": False, "reason": "nonfinite numerical scores"}
    error = max(abs(x-y) for x, y in zip(a, b))
    tolerance = 1e-3 + 1e-6 * max(abs(x) for x in a)
    reasons = []
    if error > tolerance:
        reasons.append("score difference exceeds fixed qualification tolerance")
    if max(range(len(a)), key=a.__getitem__) != max(range(len(b)), key=b.__getitem__):
        reasons.append("winning candidate changed")
    if not gaussian:
        for name in ("strict", "relaxed"):
            for field in ("accepted", "bit"):
                if reference[name].get(field) != candidate[name].get(field):
                    reasons.append(name + " " + field + " changed")
        if reference.get("rank_histogram") != candidate.get("rank_histogram"):
            reasons.append("observable rank changed")
    return {"eligible": not reasons, "maximum_absolute_score_error": error,
            "allowed_score_error": tolerance, "reasons": reasons}


def qualify_fft(case, workers, device, *, historical_anchor_directory=None):
    results, eligibility, times, geometry = {}, {}, {}, {}
    count = min(3, len(case["frequency_offsets_hz"]))
    for precision in ("fp64", "fp32"):
        for name in ("reference", "candidate"):
            prep = workers[name].request(prepare_request(case, precision, device))
            score = workers[name].request(score_request(case, count))
            results[(name, precision)] = score
            geometry[(name, precision)] = prep["geometry"]
            times[(name, precision)] = prep["wall_seconds"] + score["wall_seconds_including_finish"]
    exact = numeric_agreement(results[("reference", "fp64")], results[("candidate", "fp64")])
    if not exact["eligible"]:
        raise SuiteError(case["id"] + ": candidate FP64 disagrees with the pinned receiver")
    for precision in ("fp64", "fp32"):
        checks = [numeric_agreement(results[(name, "fp64")], results[(name, precision)])
                  for name in ("reference", "candidate")]
        checks.append(numeric_agreement(results[("reference", precision)], results[("candidate", precision)]))
        eligibility[precision] = {"eligible": all(c["eligible"] for c in checks), "checks": checks}
    eligible = [p for p in eligibility if eligibility[p]["eligible"]]
    fastest = min(eligible, key=lambda p: times[("reference", p)] + times[("candidate", p)])
    lookups = {}
    if historical_anchor_directory is not None:
        from triad_perf_fft_trace import GEOMETRY, _find_anchor, _identity
        for precision in ("fp32", "fp64"):
            if precision not in eligible:
                continue
            geometries = [{key: geometry[(name, precision)][key] for key in GEOMETRY}
                          for name in ("reference", "candidate")]
            if geometries[0] != geometries[1]:
                lookups[precision] = {"compatible": False, "reason": "paired preparation geometry differs"}
                continue
            identity = _identity(case, workers, precision, device, geometries[0])
            anchor, reason = _find_anchor(historical_anchor_directory, identity)
            lookups[precision] = {"compatible": anchor is not None,
                "anchor_path": anchor["path"] if anchor else None, "reason": reason}
    historical = [precision for precision in ("fp32", "fp64") if lookups.get(precision, {}).get("compatible")]
    chosen = historical[0] if historical else fastest
    return {"selected": chosen, "alternatives": eligibility,
            "tuning_total_seconds": {p: {n: times[(n, p)] for n in ("reference", "candidate")}
                                     for p in eligibility},
            "selection_frozen_before_prediction_measurements": True,
            "qualification_cfo_count": count,
            "selection_method": "eligible_exact_historical_precision" if historical else "fastest_eligible_pilot",
            "fresh_pilot_fastest": fastest, "historical_precision_lookups": lookups,
            "historical_preference_order": ["fp32", "fp64"],
            "historical_preference_scope": "stable exact qualified arithmetic; fresh eligible timing alternatives remain reported",
            "scope": "fixed numerical corpus; lower tensor modes have no implemented FFT adapter and are calibration-only"}


def direct_full_interval(values, confidence):
    """Conditional mean CI and future full-execution PI for actual full calls."""
    from statistics import mean, stdev
    from scipy.stats import t
    logs = [math.log(x) for x in values]
    if len(logs) < 3:
        raise SuiteError("three outer full measurements are required")
    center, deviation = mean(logs), stdev(logs)
    critical = float(t.ppf((1. + confidence) / 2., len(logs)-1))
    mean_radius = critical * deviation / math.sqrt(len(logs))
    prediction_radius = critical * deviation * math.sqrt(1. + 1./len(logs))
    return {"point_seconds": math.exp(center),
            "conditional_mean_confidence_interval_seconds": [math.exp(center-mean_radius), math.exp(center+mean_radius)],
            "conditional_prediction_interval_seconds": [math.exp(center-prediction_radius), math.exp(center+prediction_radius)],
            "distribution_free_coverage_claim": False,
            "assumptions": "stationary independent outer log full-run times; direct observation has no abbreviation scaling allowance"}


def measure_fft(case, workers, precision, device, *, repeats, confidence, full_holdout=False,
                force_direct=False):
    from triad_perf_statistics import predict_full, compare_paired
    target = len(case["frequency_offsets_hz"])
    training = sorted(set(case["training_counts"]))
    default_holdout = target if full_holdout else case["holdout_count"]
    planned_holds = [target if r == 0 and case.get("full_holdout_first_repeat", False) else default_holdout
                     for r in range(repeats)]
    abbreviated_work = repeats * sum(training) + sum(planned_holds)
    full_work = repeats * target
    # Estimation should save meaningful work. Small banks can cost MORE when
    # training and validation are counted; direct measurement is then cheaper
    # and avoids amplifying fitted-slope noise. This is a CFO-work heuristic,
    # not a universal hardware cost model; full geometry remains identical.
    direct = target <= 3 or force_direct or abbreviated_work >= .8 * full_work
    raw = {name: {"preparation": [], "scores": [], "holdouts": [], "direct": [], "geometry": []}
           for name in workers}
    checked_scores = []
    for repeat in range(repeats):
        holdout = target if repeat == 0 and case.get("full_holdout_first_repeat", False) else default_holdout
        measured_outputs = {}
        order = ("reference", "candidate") if repeat % 2 == 0 else ("candidate", "reference")
        for name in order:
            worker = workers[name]
            measured_outputs[name] = {}
            prepared = worker.request(prepare_request(case, precision, device))
            raw[name]["preparation"].append(prepared["wall_seconds"])
            raw[name]["geometry"].append(prepared["geometry"])
            if direct:
                scored = worker.request(score_request(case, target))
                measured_outputs[name][target] = scored
                raw[name]["direct"].append(prepared["wall_seconds"] + scored["wall_seconds_including_finish"])
            else:
                for count in training:
                    scored = worker.request(score_request(case, count))
                    measured_outputs[name][count] = scored
                    raw[name]["scores"].append({"repeat": repeat, "count": count,
                                                "seconds": scored["wall_seconds_including_finish"]})
                scored = worker.request(score_request(case, holdout))
                measured_outputs[name][holdout] = scored
                raw[name]["holdouts"].append({"repeat": repeat, "count": holdout,
                                              "seconds": scored["wall_seconds_including_finish"]})
        for count, reference_score in measured_outputs["reference"].items():
            candidate_score = measured_outputs["candidate"][count]
            agreement = numeric_agreement(reference_score, candidate_score)
            expected_groups = count * case["configuration"]["lag_group_count"]
            for scored in (reference_score, candidate_score):
                if scored.get("measured_group_count") != expected_groups:
                    raise SuiteError(case["id"] + ": measured CFO/lag bank differs from declared work")
            if not agreement["eligible"] or reference_score["geometry"] != candidate_score["geometry"]:
                raise SuiteError(case["id"] + ": measured-bank score, rank, gate or geometry mismatch")
            checked_scores.append({"repeat": repeat, "cfo_count": count, "agreement": agreement})
    geometry_fields = ("sample_count", "sample_rate", "nfft", "lags", "lag_first_sample",
                       "lag_last_sample", "lag_step_samples", "group_tile", "precision", "device")
    geometries = {n: [{k: g[k] for k in geometry_fields} for g in raw[n]["geometry"]] for n in workers}
    if geometries["reference"] != geometries["candidate"]:
        raise SuiteError(case["id"] + ": baseline/candidate computational geometry differs")
    predictions = {}
    for name in workers:
        r = raw[name]
        if direct:
            predictions[name] = {"status": "direct_full_measurement", "performance_pass_eligible": True,
                                 "predicted_full_seconds_by_repeat": r["direct"],
                                 "target_full_cfo_count": target,
                                 "predicted_full_time": direct_full_interval(r["direct"], confidence)}
        else:
            predictions[name] = predict_full(r["preparation"], r["scores"], target,
                                             holdouts=r["holdouts"], confidence=confidence)
    valid = all(p["performance_pass_eligible"] for p in predictions.values())
    errors = []
    if not direct:
        for repeat in range(repeats):
            residual = []
            for name in ("reference", "candidate"):
                p = predictions[name]
                actual = raw[name]["preparation"][repeat] + raw[name]["holdouts"][repeat]["seconds"]
                expected = (raw[name]["preparation"][repeat] + p["fixed_score_seconds_by_repeat"][repeat]
                            + p["per_cfo_seconds_by_repeat"][repeat] * raw[name]["holdouts"][repeat]["count"])
                residual.append(math.log(expected / actual))
            errors.append(residual[1] - residual[0])
    comparison = compare_paired(predictions["reference"]["predicted_full_seconds_by_repeat"],
                                predictions["candidate"]["predicted_full_seconds_by_repeat"],
                                paired_model_log_errors=errors, confidence=confidence,
                                relative_allowance=0. if direct else .20)
    return {"id": case["id"], "adapter": "fft", "scope": case["fixture_scope"],
            "configuration": case["configuration"], "precision": precision, "raw": raw,
            "measured_bank_numerical_checks": checked_scores,
            "measurement_plan": {"direct": direct, "forced_direct": force_direct,
                "abbreviated_cfo_work": abbreviated_work, "full_cfo_work": full_work,
                "planned_abbreviated_over_full_work": abbreviated_work / full_work,
                "minimum_cfo_work_saving_for_abbreviation": .20},
            "full_cfo_count": target,
            "heldout_cfo_counts": [target] * repeats if direct else [r["count"] for r in raw["reference"]["holdouts"]],
            "full_target_measured": direct or any(r["count"] == target for r in raw["reference"]["holdouts"]),
            "full_target_measurement_count_per_source": repeats if direct else sum(r["count"] == target for r in raw["reference"]["holdouts"]),
            "prediction": predictions, "comparison": comparison,
            "prediction_valid": valid, "fixture_hashes": case["fixture_hashes"],
            "source_bound_metadata": case["source_bound_metadata"]}


def gaussian_control(workers, device, *, repeats, confidence):
    from triad_perf_statistics import compare_paired
    requests = [dict(action="gaussian", blocks=8, candidates=32, taps=2, window_blocks=8,
                     doppler_width_hz=.1, memory_mib=256, seed=9182, condition="ordinary"),
                dict(action="gaussian", blocks=4, candidates=16, taps=8, window_blocks=4,
                     doppler_width_hz=.01, memory_mib=256, seed=9183, condition="ordinary"),
                dict(action="gaussian", blocks=8, candidates=16, taps=2, window_blocks=8,
                     doppler_width_hz=.1, memory_mib=256, seed=9184, condition="strong"),
                dict(action="gaussian", blocks=8, candidates=16, taps=2, window_blocks=8,
                     doppler_width_hz=.1, memory_mib=256, seed=9185, condition="near-singular")]
    qualification = []
    eligible = {"fp64": True, "fp32": True}
    tuning = {p: 0. for p in eligible}
    for request in requests:
        outputs = {}
        for precision in eligible:
            for name in workers:
                outputs[(name, precision)] = workers[name].request({**request, "precision": precision, "device": device})
                x = outputs[(name, precision)]
                tuning[precision] += x["preparation_seconds"] + x["scoring_seconds"]
        if not numeric_agreement(outputs[("reference", "fp64")], outputs[("candidate", "fp64")], gaussian=True)["eligible"]:
            raise SuiteError("Gaussian candidate FP64 differs from fixed numerical reference")
        for precision in eligible:
            checks = [numeric_agreement(outputs[(n, "fp64")], outputs[(n, precision)], gaussian=True) for n in workers]
            checks.append(numeric_agreement(outputs[("reference", precision)], outputs[("candidate", precision)], gaussian=True))
            good = all(x["eligible"] for x in checks)
            eligible[precision] &= good
            qualification.append({"shape": request, "precision": precision, "eligible": good, "checks": checks})
    chosen = min((p for p in eligible if eligible[p]), key=tuning.__getitem__)
    times = {name: [] for name in workers}
    for repeat in range(repeats):
        for name in (("reference", "candidate") if repeat % 2 == 0 else ("candidate", "reference")):
            value = workers[name].request({**requests[0], "precision": chosen, "device": device})
            times[name].append(value["preparation_seconds"] + value["scoring_seconds"])
    return {"id": "gaussian-multiblock", "adapter": "gaussian", "full_target_measured": True,
            "precision": chosen, "qualification": qualification, "prediction_valid": True,
            "raw_full_seconds": times,
            "full_time_intervals": {n: direct_full_interval(v, confidence) for n, v in times.items()},
            "comparison": compare_paired(times["reference"], times["candidate"], confidence=confidence, relative_allowance=0.),
            "scope": "direct full algebraic multiblock Gaussian helper; not sampled propagation or a native receiver"}


def profile_identity(profile):
    h = profile.get("hardware", {})
    cpu, gpu = h.get("cpu", {}), h.get("device", {})
    return {"device": profile.get("device"), "threads": h.get("requested_threads"),
            "cpu_model": cpu.get("model name", cpu.get("model_name", cpu.get("model"))),
            "affinity": cpu.get("affinity_cpu_count"), "quota": cpu.get("quota_cores"),
            "affinity_cpus": cpu.get("affinity_cpus"), "affinity_topology": cpu.get("affinity_topology"),
            "gpu": gpu.get("name"), "capability": gpu.get("compute_capability")}


def hardware_health(profile, baseline_path):
    if baseline_path is None:
        return {"status": "hardware_baseline_unavailable", "performance_pass_eligible": False,
                "reason": "no comparable known-good measured hardware profile was supplied"}
    saved = json.loads(Path(baseline_path).read_text())
    if "hardware_profiles" in saved:
        saved = next((x for x in saved["hardware_profiles"] if x.get("device") == profile.get("device")), {})
    if "hardware_profile" in saved:
        saved = saved["hardware_profile"]
    if profile_identity(saved) != profile_identity(profile):
        return {"status": "hardware_baseline_unavailable", "performance_pass_eligible": False,
                "reason": "measured hardware identity, resource share or thread count differs"}
    rows = []
    for name, current in profile["cases"].items():
        prior = saved.get("cases", {}).get(name, {})
        key = "operations_per_second" if current.get("operations_per_second") else "bytes_per_second"
        before, after = prior.get(key), current.get(key)
        if prior.get("status") == "complete" and current.get("status") != "complete":
            rows.append({"case": name, "regression_screen": True,
                         "reason": "previously measured arithmetic/kernel path is no longer supported or complete"})
        if prior.get("status") == current.get("status") == "complete" and before and after:
            regime = ("shape", "input_storage", "multiplication", "accumulation", "output",
                      "estimated_operations_per_iteration", "bytes_moved_per_iteration",
                      "estimated_read_write_bytes_per_iteration")
            if any(prior.get(k) != current.get(k) for k in regime):
                rows.append({"case": name, "regression_screen": False, "status": "kernel_regime_incompatible"})
                continue
            previous_cpu, current_cpu = prior.get("process_cpu_cores_used"), current.get("process_cpu_cores_used")
            low_cpu = (previous_cpu is not None and current_cpu is not None and
                       previous_cpu >= 1. and current_cpu < .7 * previous_cpu)
            gpu_before = prior.get("device_busy_telemetry", {}).get("metrics", {}).get("gpu_busy_percent", {})
            gpu_after = current.get("device_busy_telemetry", {}).get("metrics", {}).get("gpu_busy_percent", {})
            low_gpu = (gpu_before.get("sample_count", 0) >= 5 and gpu_after.get("sample_count", 0) >= 5 and
                       gpu_before.get("mean", 0) >= 50. and gpu_after["mean"] < .7 * gpu_before["mean"])
            rows.append({"case": name, "throughput_measure": key, "old": before, "current": after,
                         "time_equivalent_ratio": before / after,
                         "regression_screen": before / after > 1.30 or low_cpu or low_gpu,
                         "low_cpu_utilization_screen": low_cpu, "low_device_busy_screen": low_gpu,
                         "raw_utilization_current": current_cpu})
    return {"status": "raw_hardware_screen", "performance_pass_eligible": False, "rows": rows,
            "warning": any(x["regression_screen"] for x in rows),
            "scope": "raw environment-health screen; one five-second block does not establish independent-replicate confidence"}


def measure_gaussian_adaptive(workers, device, policy):
    """Time complete helper preparation/scoring in pre-sized outer blocks.

    The pilot chooses only batch size and eligible arithmetic. Its measurements
    do not enter the subsequent confidence intervals.
    """
    from statistics import mean
    from triad_perf_adaptive import summarize
    pilot = gaussian_control(workers, device, repeats=3, confidence=.95)
    chosen = pilot["precision"]
    calls = min(64, max(1, math.ceil(.1 / max(mean(v) for v in pilot["raw_full_seconds"].values()))))
    request = dict(action="gaussian", blocks=8, candidates=32, taps=2, window_blocks=8,
                   doppler_width_hz=.1, memory_mib=256, seed=9182, condition="ordinary",
                   precision=chosen, device=device)
    values = {n: [] for n in workers}
    looks, checks = [], []
    for index in range(policy.looks[-1]):
        outputs = {}
        for name in (("reference", "candidate") if index % 2 == 0 else ("candidate", "reference")):
            seconds = 0.
            for _ in range(calls):
                value = workers[name].request(request)
                seconds += value["preparation_seconds"] + value["scoring_seconds"]
            values[name].append(seconds/calls)
            outputs[name] = value
        agreement = numeric_agreement(outputs["reference"], outputs["candidate"], gaussian=True)
        if not agreement["eligible"]:
            raise SuiteError("Gaussian measured helper numerical disagreement")
        checks.append(agreement)
        if index+1 in policy.looks:
            summary = summarize(values["reference"], values["candidate"], policy, model_validation="direct_full")
            looks.append(summary)
            if summary["stop_reason"] != "continue":
                break
    return {"id": "gaussian-multiblock", "adapter": "gaussian", "precision": chosen,
            "qualification": pilot["qualification"], "pilot_excluded_from_inference": True,
            "calls_per_outer_block": calls, "raw_full_seconds": values,
            "prediction_valid": True, "full_target_measured": True,
            "adaptive": summary, "looks": looks, "timing_precision_reached": summary["timing_precision_reached"],
            "comparison": {"gate": summary["performance_gate"],
                "expanded_full_prediction_ratio_interval": summary["paired_candidate_over_reference"]["model_adjusted_gate_interval"]},
            "measured_numerical_checks": checks,
            "scope": "complete resident algebraic Gaussian helper preparation plus scoring; fixture construction and process startup excluded"}


def endpoint_calibration_guard(value):
    """Block gross underprediction without claiming a new mean-runtime CI.

    This uses the already frozen historical single-observation allowance. It
    never increases that allowance from the endpoint or retries the endpoint.
    Ordinary overlap/no-history cases remain unresolved warnings.
    """
    check = (value.get("anchors", {}).get("conditional_check") or
             value.get("current_validation", {}).get("conditional_model") or {})
    triggers = []
    if (check.get("schema") == "triad-performance-conditional-v2" and
            check.get("status") == "unvalidated_request_focused_qualification"):
        for metric, row in check.get("checks", {}).items():
            residual = row["log_actual_over_predicted"]
            timing = row["training_timing_log_half_width"]
            scatter = row["pre_sentinel_historical_observation_log_envelope"]
            if not all(math.isfinite(x) for x in (residual, timing, scatter)) or min(timing, scatter) < 0:
                raise SuiteError("invalid frozen endpoint-calibration evidence")
            unexplained = residual - timing - scatter
            if unexplained > math.log1p(.30):
                triggers.append({"metric": metric, "unexplained_log_underprediction": unexplained,
                                 "underprediction_ratio_after_frozen_allowances": math.exp(unexplained),
                                 "training_log_half_width": timing,
                                 "historical_observation_log_envelope": scatter})
    return {"status": "severe_endpoint_prediction_mismatch" if triggers else "not_triggered",
            "blocking": bool(triggers), "threshold_ratio": 1.30, "triggers": triggers,
            "scope": "engineering calibration guard; not a confirmed regression in mean runtime",
            "false_failure_rate_qualified": False,
            "historical_scatter_is_a_proven_tail_bound": False}


def native_direct_overrides(cases, requested, *, binary, threads):
    """Freeze explicit direct routes without changing any native full work."""
    from triad_perf_native import _argv, digest

    requested = list(requested)
    by_id = {case["id"]: case for case in cases}
    if len(set(requested)) != len(requested):
        raise SuiteError("native-direct-case IDs must be unique")
    unknown = set(requested) - by_id.keys()
    if unknown:
        raise SuiteError("native-direct-case must select a native case in this run: " + ", ".join(sorted(unknown)))
    records = {}
    transformed = []
    for original in cases:
        case = dict(original)
        if case["id"] in requested:
            if "training_seconds" not in case:
                raise SuiteError("native-direct-case already uses direct measurement: " + case["id"])
            del case["training_seconds"]
            run_arguments = [_argv(binary, row, row["target_seconds"], 1_000_000, threads)
                             for row in (original, case)]
            configure_arguments = [_argv(binary, row, row["target_seconds"], 1_000_000, threads, configure=True)
                                   for row in (original, case)]
            if run_arguments[0] != run_arguments[1] or configure_arguments[0] != configure_arguments[1]:
                raise SuiteError("native direct route changed full target arguments")
            records[case["id"]] = {
                "id": case["id"], "route": "explicit_direct_full", "removed_fields": ["training_seconds"],
                "original_case_sha256": digest(original), "transformed_case_sha256": digest(case),
                "original_case": original, "transformed_case": case,
                "full_target_equivalence": {"run_argv_equal": True, "configuration_argv_equal": True,
                    "target_seconds": case["target_seconds"], "run_argv": run_arguments[0],
                    "configure_argv": configure_arguments[0]},
                "historical_anchor_read": False, "historical_anchor_written": False}
        transformed.append(case)
    return transformed, records


def run_backend(args, report):
    """Independent hardware, path witnesses, and source-bound runtime models."""
    from triad_perf_cases import prepare_cases, scenarios
    from triad_perf_hardware import collect_profile
    from triad_perf_adaptive import AdaptivePolicy, regression_profile
    from triad_perf_fft_trace import measure_fft as measure_fft_trace
    from triad_perf_native import scenarios as native_scenarios, measure_native_case
    from triad_perf_native import identity as native_identity, anchor_compatible, digest
    from triad_perf_sustained import native_sustained, fft_sustained, _require_native_candidate
    deadline = time.monotonic() + args.budget_seconds
    started = time.perf_counter()
    output = Path(args.output)
    working = output.parent / (output.stem + ".work")
    working.mkdir(parents=True, exist_ok=True)
    anchor_directory = args.anchor_directory or output.parent / "triad-performance-anchors"
    anchor_directory.mkdir(parents=True, exist_ok=True)
    workers = {}
    harness_paths = sorted(set([Path(__file__), ROOT / "tools/triad_tensor_native.py",
        *ROOT.glob("tools/triad_perf*.py"), *ROOT.glob("tools/triad-performance*.json")]))
    harness_identity = {p.name: sha(p) for p in harness_paths}
    application_paths = sorted(set([args.candidate_root / p for p in SOURCE_FILES] +
                                  list(args.candidate_root.glob("tools/triad*.py"))))
    application_identity = {str(p.relative_to(args.candidate_root)): sha(p) for p in application_paths}
    try:
        profile = {"quick": "smoke", "sustain": "extended"}.get(args.profile, args.profile)
        configured_native = native_scenarios(args.profile)
        configured_fft = scenarios(profile)
        all_case_ids = [c["id"] for c in configured_native + configured_fft] + ["gaussian-multiblock"]
        selected = set(args.case or all_case_ids)
        unknown = selected - set(all_case_ids)
        if unknown:
            raise SuiteError("unknown cases for this profile: " + ", ".join(sorted(unknown)))
        native_cases = [c for c in configured_native if c["id"] in selected]
        native_cases, direct_overrides = native_direct_overrides(native_cases, args.native_direct_case,
            binary=args.binary, threads=args.native_threads)
        report["native_direct_overrides"] = list(direct_overrides.values())
        fft_ids = [c["id"] for c in configured_fft if c["id"] in selected]
        report["full_profile_case_ids"] = all_case_ids
        report["selected_case_ids"] = [name for name in all_case_ids if name in selected]
        report["harness_sha256"] = harness_identity
        report["candidate"] = {"root": str(args.candidate_root), "source_sha256": application_identity}
        report["hardware_profile"] = collect_profile(device=args._backend_worker, threads=args.threads,
            seconds=args.stress_seconds, memory_limit_bytes=512*1024**2, deadline=deadline)
        if report["hardware_profile"]["status"] != "complete":
            raise SuiteError("mandatory hardware calibration incomplete")
        environment = report["hardware_profile"]["hardware"]
        report["hardware_health"] = hardware_health(report["hardware_profile"], args.hardware_baseline)
        if report["hardware_health"].get("warning"):
            report["warnings"].append("raw hardware throughput/utilization screen degraded")

        # The instrumented pass is kept outside every timed receiver call.
        path_output = working / "executed-paths.json"
        path_process = subprocess.run([sys.executable, str(ROOT / "tools/triad_perf_paths.py"),
            "--source-root", str(args.candidate_root), "--device", args._backend_worker,
            "--threads", str(args.threads), "--output", str(path_output),
            "--deadline-seconds", str(min(180., remaining(deadline)))],
            capture_output=True, text=True, timeout=min(185., remaining(deadline)), env=child_environment(args.threads))
        (working / "executed-paths.stderr.log").write_text(path_process.stderr)
        if not path_output.is_file():
            raise SuiteError("executed-path coverage did not produce a report: " + path_process.stderr[-1200:])
        report["executed_path_coverage"] = json.loads(path_output.read_text())
        if path_process.returncode or not report["executed_path_coverage"]["complete_applicable_coverage"]:
            raise SuiteError("required receiver computational paths did not execute successfully")

        # Qualification keeps its historical multiplicity ledger. Routine
        # decisions spend alpha per test across fixed looks, independently of
        # the diagnostic timing-width target and number of selected cases.
        family_count = len(all_case_ids)
        stopping_rule = args.stopping_rule or ("precision" if args.validation_mode == "qualify" else "decision")
        policy = AdaptivePolicy(relative_precision=args.precision_target, looks=args.looks,
                                family_count=family_count, stopping_rule=stopping_rule,
                                **(regression_profile(args.regression_profile) if stopping_rule == "decision" else {}))
        report["adaptive_policy"] = {"relative_timing_precision": args.precision_target, "looks": list(args.looks),
            "families": family_count, "model_accuracy_is_separate": True,
            "stopping_rule": stopping_rule, "regression_profile": args.regression_profile if stopping_rule == "decision" else None,
            "requested_regression_profile": args.regression_profile,
            "profile_status": "applied_to_routine_decisions" if stopping_rule == "decision" else "disabled_by_precision_stopping",
            "pass_alpha": policy.pass_error_budget, "failure_alpha": policy.failure_error_budget,
            "slowdown_threshold": policy.slowdown_threshold,
            "stop_rule": (f"first predeclared look resolving the {policy.slowdown_threshold:g} slowdown decision; or resource/sample ceiling"
                          if stopping_rule == "decision" else
                          "first predeclared look meeting precision; confirmed regression; or resource/sample ceiling")}

        def retain_case(value):
            value["endpoint_calibration_guard"] = endpoint_calibration_guard(value)
            report["cases"].append(value)
            comparison = value.get("comparison", {})
            adaptive = value.get("adaptive", value.get("prediction", {}))
            if value["endpoint_calibration_guard"]["blocking"]:
                report["failures"].append(value["id"] +
                    ": severe_endpoint_prediction_mismatch; focused calibration required before release")
            if comparison.get("gate") == "performance_regression":
                threshold = comparison.get("threshold", adaptive.get("policy", {}).get("slowdown_threshold", policy.slowdown_threshold))
                report["failures"].append(value["id"] + f": full-runtime slowdown lower bound exceeds {threshold:g}")
            elif comparison.get("gate") not in ("pass",):
                report["warnings"].append(value["id"] + ": full-runtime comparison is unresolved")
            if not value.get("prediction_valid", False):
                report["warnings"].append(value["id"] + ": duration model is invalid or unqualified")
            if stopping_rule == "precision" and not adaptive.get("timing_precision_reached", value.get("timing_precision_reached", False)):
                report["warnings"].append(value["id"] + ": requested timing precision was not reached")
            atomic_json(output, report)

        report["required_case_ids"] = report["selected_case_ids"]
        reference_binary = args.reference_binary or args.binary
        report["native_binary_sha256"] = {"reference": sha(reference_binary), "candidate": sha(args.binary)}
        report["native_reference_is_candidate"] = args.reference_binary is None
        if args.reference_binary is None and native_cases:
            report["warnings"].append("native reference binary was not supplied: native results validate prediction cost only, not an implementation regression")
        for case in native_cases:
            remaining(deadline)
            anchor_path = anchor_directory / (case["id"] + ".native-anchor.json")
            if case["id"] in direct_overrides:
                anchor, mode = None, "routine"
            else:
                anchor = json.loads(anchor_path.read_text()) if anchor_path.is_file() else None
                identities = {"reference": native_identity(reference_binary, case, args.native_threads),
                              "candidate": native_identity(args.binary, case, args.native_threads)}
                mode = "qualify" if args.validation_mode == "qualify" or (args.validation_mode == "auto" and not anchor_compatible(anchor, identities, case)) else "routine"
            def freeze(value, case_id=case["id"]):
                atomic_json(working / (case_id + ".prediction-" + str(value["index"]) + ".json"), value)
            measured = measure_native_case(reference_binary, args.binary, case, policy,
                threads=args.native_threads, deadline=deadline, validation_mode=mode,
                anchor=anchor, trace_candidate=False, freeze_callback=freeze)
            if case["id"] in direct_overrides:
                override = direct_overrides[case["id"]]
                measured["measurement_override"] = {**override,
                    "original_measurement_identities": {name: {**identity,
                        "case_sha256": override["original_case_sha256"]}
                        for name, identity in measured["identities"].items()},
                    "transformed_measurement_identities": measured["identities"]}
            # Trace clocks are a separately executed witness, never mixed into
            # reference/candidate timing comparisons on legacy executables.
            if mode == "qualify" and measured.get("anchors", {}).get("validated"):
                atomic_json(anchor_path, measured["anchors"])
            retain_case(measured)
        report["native_receiver_performance"] = ("measured; fresh model qualification, historical conditional forecasts and timing precision reported separately per case"
            if native_cases else "not selected; native performance unmeasured in this focused run")
        report["sustained_checks"] = []
        if args.sustained_seconds:
            eligible = []
            for case, measured in zip(native_cases, report["cases"]):
                try:
                    _require_native_candidate(measured, case)
                    eligible.append((case, measured))
                except ValueError:
                    pass
            if eligible:
                # Predeclared preference: largest full observation; select once,
                # then never retry a disappointing sustained result.
                case, measured = max(eligible, key=lambda pair: pair[0]["target_seconds"])
                sustained = native_sustained(args.binary, case, measured,
                    minimum_seconds=args.sustained_seconds, threads=args.native_threads,
                    deadline=deadline, freeze_callback=lambda value: atomic_json(
                        working / "native-sustained-commitment.json", value))
                report["sustained_checks"].append(sustained)
                atomic_json(output, report)
            else:
                report["warnings"].append("native sustained check unavailable: no independently qualified candidate forecast")

        fixture_start = time.perf_counter()
        fixture_directory = args.fixture_cache or output.parent / "triad-performance-fixtures"
        report["fixture_cache_directory"] = str(fixture_directory)
        cases = prepare_cases(args.binary, fixture_directory, profile=profile,
                              deadline=deadline, source_root=args.candidate_root, case_ids=fft_ids) if fft_ids else []
        cases = [case for case in cases if case["id"] in selected]
        report["fixture_preparation_seconds"] = time.perf_counter() - fixture_start
        expected = fft_ids
        actual = [c["id"] for c in cases]
        if len(actual) != len(set(actual)) or set(actual) != set(expected):
            raise SuiteError("mandatory FFT cases missing or duplicated")
        if cases or "gaussian-multiblock" in selected:
            with tempfile.TemporaryDirectory(prefix="triad-reference-", dir=working) as directory:
                report["reference"] = extract_baseline(ROOT, args.baseline_ref, directory)
                for name, source in (("reference", directory), ("candidate", args.candidate_root)):
                    workers[name] = ReceiverWorker(source, threads=args.threads, deadline=deadline,
                                                  log_directory=working, name=name)
                report["worker_startup"] = {n: {"external_seconds": w.external_startup_seconds, "identity": w.identity}
                                            for n,w in workers.items()}
                fft_results = []
                for case in cases:
                    case["performance_environment"] = environment
                    qualification = qualify_fft(case, workers, args._backend_worker,
                        historical_anchor_directory=anchor_directory / args._backend_worker
                        if args.validation_mode in ("auto", "reuse") else None)
                    measured = measure_fft_trace(case, workers, qualification["selected"], args._backend_worker,
                        policy=policy, deadline=deadline, api=sys.modules[__name__],
                        anchor_directory=anchor_directory / args._backend_worker, validation_mode=args.validation_mode)
                    measured["precision_qualification"] = qualification
                    retain_case(measured)
                    fft_results.append((case, measured))
                if args.sustained_seconds:
                    eligible = [(case, value) for case, value in fft_results
                                if value.get("prediction_valid") and value.get("anchors", {}).get("status")
                                in ("validated", "historically_supported_conditional")]
                    if eligible:
                        case, measured = max(eligible, key=lambda pair: len(pair[0]["frequency_offsets_hz"]))
                        sustained = fft_sustained(case, workers["candidate"], measured,
                            api=sys.modules[__name__], minimum_seconds=args.sustained_seconds,
                            deadline=deadline, freeze_callback=lambda value: atomic_json(
                                working / "fft-sustained-commitment.json", value))
                        report["sustained_checks"].append(sustained)
                        atomic_json(output, report)
                    else:
                        report["warnings"].append("FFT sustained check unavailable: no validated full-bank forecast")
                if "gaussian-multiblock" in selected:
                    retain_case(measure_gaussian_adaptive(workers, args._backend_worker, policy))
                for worker in workers.values():
                    worker.request({"action": "hello"})
        if harness_identity != {p.name: sha(p) for p in harness_paths}:
            raise SuiteError("performance harness changed during measurement")
        if application_identity != {str(p.relative_to(args.candidate_root)): sha(p) for p in application_paths}:
            raise SuiteError("application source changed during measurement")
        if sorted(c["id"] for c in report["cases"]) != sorted(report["required_case_ids"]):
            raise SuiteError("required computational cases are incomplete or duplicated")
        report["conditional_forecast_cases"] = [c["id"] for c in report["cases"]
            if (c.get("adaptive", c.get("prediction", {})).get("model_validation")
                == "historically_supported_conditional" or c.get("anchors", {}).get("status")
                == "historically_supported_conditional")]
        report["conditional_forecast_scope"] = (
            "historically qualified cost relationship with current abbreviated coefficients and a "
            "precommitted full paired endpoint check; conditional engineering model allowance, "
            "not fresh mean-equivalence qualification or statistical future coverage")
        report["complete_required_coverage"] = args.profile != "quick" and not args.case
        if args.case:
            report["warnings"].append("focused case selection is diagnostic and incomplete for normal CI")
        if args.profile == "quick":
            report["warnings"].append("quick profile is diagnostic and incomplete for normal CI")
        report["cross_architecture"] = {"status": "unqualified", "performance_pass_eligible": False,
            "reason": "no matched target-stage validation; hardware peak-rate scaling is not a runtime model"}
        if args.target_profile:
            report["cross_architecture"]["target_profile_sha256"] = sha(args.target_profile)
    except Exception as error:
        report["failures"].append(type(error).__name__ + ": " + str(error))
    finally:
        for worker in workers.values():
            worker.close()
        report["elapsed_seconds"] = time.perf_counter() - started
        report["status"] = "failed" if report["failures"] else ("warning" if report["warnings"] else "passed")
        report["exit_code"] = 2 if report["failures"] else 0
        report["descendant_cleanup"] = "receiver workers reaped; outer supervisor owns process-group termination"
        atomic_json(output, report)
    return report["exit_code"]


def initial_report(args, device):
    return {"schema": SCHEMA, "status": "running", "exit_code": None, "device": device,
            "complete_required_coverage": False, "cases": [], "failures": [], "warnings": [],
            "budget_seconds": args.budget_seconds, "stress_seconds_per_case": args.stress_seconds,
            "profile": args.profile, "preferred_budget_seconds": 300,
            "scope": "native complete simulations, resident FFT/Gaussian runtime predictions, and executed offline receiver-path witnesses; ordinary receiver excluded",
            "native_receiver_performance": "unmeasured",
            "native_receiver_target": "complete fixed-bank duration siblings and independent full holds; actual FP64",
            "tensor_receiver_policy": "explicit supported helper paths and separate hardware calibration; no tensor FFT claim",
            "uncertainty": "adaptive multiplicity-adjusted timing intervals; fresh mean-error qualification or separately labelled historical conditional model allowance; future variation separate; no distribution-free coverage claim"}


def parse_args(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--binary", type=Path, required=True)
    p.add_argument("--device", choices=("cpu", "cuda", "auto"), default="cpu")
    p.add_argument("--budget-seconds", type=float, default=1800.)
    p.add_argument("--stress-seconds", type=float, default=5.)
    p.add_argument("--threads", type=int, default=2)
    p.add_argument("--native-threads", type=int, default=1)
    p.add_argument("--reference-binary", type=Path)
    p.add_argument("--precision-target", type=float, default=.05)
    p.add_argument("--stopping-rule", choices=("decision", "precision"),
                   help="routine defaults to a profile-specific one-sided decision; qualification retains historical precision stopping")
    p.add_argument("--regression-profile", choices=("normal", "ci", "strict"), default="normal",
                   help="routine decision criteria; precision stopping uses historical criteria")
    p.add_argument("--looks", default="5,9,17,33")
    p.add_argument("--validation-mode", choices=("auto", "qualify", "reuse"), default="auto")
    p.add_argument("--anchor-directory", type=Path)
    p.add_argument("--fixture-cache", type=Path,
                   help="retained exact FFT fixture cache shared by qualification and routine runs")
    p.add_argument("--sustained-seconds", type=float, default=None,
                   help="one frozen campaign per eligible native/FFT family; default 60 for sustain, otherwise 0")
    p.add_argument("--baseline-ref", default=BASELINE)
    p.add_argument("--candidate-root", type=Path, default=ROOT)
    p.add_argument("--profile", choices=("ci", "quick", "sustain"), default="ci")
    p.add_argument("--case", action="append", help="focus on one case (repeatable); never counts as complete CI coverage")
    p.add_argument("--native-direct-case", action="append", default=[],
                   help="measure this native modeled case directly at its unchanged full target (repeatable)")
    p.add_argument("--hardware-baseline", type=Path)
    p.add_argument("--target-profile", type=Path)
    p.add_argument("--_backend-worker", choices=("cpu", "cuda"), help=argparse.SUPPRESS)
    args = p.parse_args(argv)
    if args.sustained_seconds is None:
        args.sustained_seconds = 60. if args.profile == "sustain" else 0.
    if not math.isfinite(args.sustained_seconds) or not 0 <= args.sustained_seconds <= 3600:
        p.error("sustained-seconds must be in [0,3600]")
    if not math.isfinite(args.budget_seconds) or not 0 < args.budget_seconds <= 28800:
        p.error("budget-seconds must be in (0,28800]")
    if not math.isfinite(args.stress_seconds) or not 0 < args.stress_seconds <= 5:
        p.error("stress-seconds must be in (0,5]")
    if args.threads < 1 or args.native_threads < 1:
        p.error("threads must be positive")
    try:
        from triad_perf_adaptive import AdaptivePolicy
        args.looks = tuple(map(int, args.looks.split(",")))
        AdaptivePolicy(relative_precision=args.precision_target, looks=args.looks)
    except ValueError as error:
        p.error(str(error))
    if not re.fullmatch(r"[0-9a-f]{40}", args.baseline_ref):
        p.error("baseline-ref must be an immutable full commit SHA")
    args.candidate_root = args.candidate_root.resolve()
    args.binary = args.binary.resolve()
    if args.reference_binary:
        args.reference_binary = args.reference_binary.resolve()
    if args.anchor_directory:
        args.anchor_directory = args.anchor_directory.resolve()
    if args.fixture_cache:
        args.fixture_cache = args.fixture_cache.resolve()
    args.output = args.output.resolve()
    if args.native_direct_case:
        if args.validation_mode == "qualify":
            p.error("native-direct-case is a routine route and cannot be used with validation-mode qualify")
        from triad_perf_native import scenarios as native_scenarios
        selected_native = [case for case in native_scenarios(args.profile)
                           if not args.case or case["id"] in args.case]
        try:
            native_direct_overrides(selected_native, args.native_direct_case,
                binary=args.binary, threads=args.native_threads)
        except SuiteError as error:
            p.error(str(error))
    return args


def main(argv=None):
    args = parse_args(argv)
    if args._backend_worker:
        return run_backend(args, initial_report(args, args._backend_worker))
    if args.output.exists():
        raise SuiteError("refusing to overwrite an existing suite report")
    if os.name != "posix":
        raise SuiteError("the hard-deadline suite currently requires a POSIX host")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.device == "auto":
        from triad_perf_hardware import discover_hardware
        devices = ["cpu"]
        if discover_hardware("cuda", args.threads)["device"]["status"] == "available":
            devices.append("cuda")
    else:
        devices = [args.device]
    result = {"schema": SCHEMA, "status": "running", "exit_code": None,
              "complete_required_coverage": False, "backends": [], "hardware_profiles": [],
              "failures": [], "warnings": [], "budget_seconds_per_backend": args.budget_seconds}
    start = time.perf_counter()
    for device in devices:
        backend_output = args.output.with_name(args.output.stem + "." + device + ".json")
        if backend_output.exists():
            raise SuiteError("backend report already exists; choose a fresh output name")
        command = [sys.executable, str(Path(__file__).resolve()), *list(sys.argv[1:] if argv is None else argv),
                   "--output", str(backend_output), "--_backend-worker", device]
        try:
            code = supervise(command, timeout=args.budget_seconds + 5,
                stdout=backend_output.with_suffix(".stdout.log"), stderr=backend_output.with_suffix(".stderr.log"),
                env=child_environment(args.threads), cwd=ROOT)
            if not backend_output.exists():
                raise SuiteError("backend exited without a complete evidence report")
            backend = json.loads(backend_output.read_text())
            if backend.get("schema") != SCHEMA or backend.get("exit_code") != code:
                raise SuiteError("backend exit code/report identity mismatch")
            result["backends"].append(backend)
            if "hardware_profile" in backend:
                result["hardware_profiles"].append(backend["hardware_profile"])
            result["failures"].extend(backend["failures"])
            result["warnings"].extend(backend["warnings"])
        except Exception as error:
            result["failures"].append(device + ": " + type(error).__name__ + ": " + str(error))
        atomic_json(args.output, result)
    result["complete_required_coverage"] = (len(result["backends"]) == len(devices)
        and all(b.get("complete_required_coverage") for b in result["backends"]))
    if not result["complete_required_coverage"] and args.profile != "quick" and not args.case:
        result["failures"].append("required backend/case coverage is incomplete")
    result["status"] = "failed" if result["failures"] else ("warning" if result["warnings"] else "passed")
    result["exit_code"] = 2 if result["failures"] else 0
    result["elapsed_seconds"] = time.perf_counter() - start
    atomic_json(args.output, result)
    print(f"Triad performance: {result['status']}; {len(result['backends'])}/{len(devices)} backends; "
          f"{result['elapsed_seconds']:.2f} s. Report: {args.output}")
    for failure in result["failures"]:
        print("Failure: " + failure)
    for warning in result["warnings"]:
        print("Warning: " + warning)
    return result["exit_code"]


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(type(error).__name__ + ": " + str(error), file=sys.stderr)
        raise SystemExit(2)
