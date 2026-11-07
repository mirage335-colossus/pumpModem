#!/usr/bin/env python3
"""One measured FFT stream when both implementations have identical identities.

This is a separate experiment, not a replacement for the paired harness.
Identity equality removes an implementation comparison; it does not remove the
absolute workload regression check, a full-bank sentinel, or model uncertainty.
The timing boundary is the unchanged ReceiverService preparation plus wrapped
score. Input generation, process startup and audit hashing are outside it.
"""
from __future__ import annotations

import argparse
import ast
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import statistics
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "triad-performance-identical-fft-v1"
WORKER = Path(__file__).resolve()
SOURCES = ("reference", "candidate")
CONTROL_PREFIXES = ("PYTHON", "LD_", "OMP_", "GOMP_", "KMP_", "MKL_", "OPENBLAS_",
                    "NPY_", "ATEN_", "ONEDNN_", "TORCH", "CUDA", "CUBLAS", "CUDNN",
                    "TRITON", "NUMEXPR", "VECLIB", "TRIAD_")
MEASUREMENT_FILES = ("triad_perf_identical_fft.py", "triad_perf_receiver.py",
                     "triad_perf_fft_trace.py", "triad_perf_adaptive.py",
                     "triad-performance.py", "triad_perf_hardware.py")
LEGACY_WORKER_SHA256 = "fd2fd7109e9dfcd8fe984fc526995a65b4517335cf6d80428a749f13994fcb34"


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                    allow_nan=False).encode()).hexdigest()


def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def save(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x") as out:
        json.dump(value, out, indent=2, allow_nan=False)
        out.write("\n")


def append_receipt(directory, event, value):
    """Persist completed observations even if a later call or fit fails."""
    with (Path(directory)/"observations.jsonl").open("a") as out:
        out.write(json.dumps({"event": event, "value": value}, separators=(",", ":"), allow_nan=False)+"\n")


def load_harness(filename, name):
    path = WORKER.with_name(filename)
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    if Path(module.__file__).resolve() != path:
        raise ValueError("benchmark adapter was not loaded from this harness")
    return module


class RuntimeAudit:
    """Observe actual imports/native mappings outside the timed service calls.

    Native mapping enumeration currently requires Linux /proc. Missing mapping
    evidence disables identity deduplication; it is not inferred from versions.
    Hashes are cached while device/inode/size/mtime/ctime are unchanged. This is
    non-adversarial benchmark provenance, not an integrity/security monitor.
    """
    def __init__(self, root, service):
        self.root, self.service = Path(root).resolve(), service
        self.cache = {}

    def file_hash(self, path):
        path = Path(path).resolve(strict=True)
        st = path.stat()
        stamp = (st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns, st.st_ctime_ns)
        if self.cache.get(path, (None,))[0] != stamp:
            self.cache[path] = (stamp, sha(path))
        return self.cache[path][1]

    def snapshot(self, device):
        import numpy
        import scipy
        import torch
        hardware = load_harness("triad_perf_hardware.py", "_triad_identical_hardware")
        repository, numerical = {}, {}
        pinned = {WORKER.with_name(name).resolve() for name in MEASUREMENT_FILES}
        dynamic_torch = {"torch.ops": ("ops", "torch._ops", "_Ops"),
                         "torch.classes": ("classes", "torch._classes", "_Classes")}
        for name, module in tuple(sys.modules.items()):
            origin = getattr(module, "__file__", None)
            if not isinstance(origin, (str, os.PathLike)):
                continue
            if name in dynamic_torch:
                attribute, owner_name, type_name = dynamic_torch[name]
                owner = sys.modules.get(owner_name)
                namespace_type = vars(owner).get(type_name) if owner is not None else None
                # These two PyTorch ModuleType namespaces deliberately expose
                # pseudo filenames. Bind their exact implementation classes;
                # no general exemption for a missing or relative __file__.
                if module is vars(torch).get(attribute) and type(module) is namespace_type:
                    implementation = Path(vars(owner)["__file__"]).resolve(strict=True)
                    if implementation.is_relative_to(self.root):
                        raise ValueError("numerical library was shadowed by source checkout")
                    numerical[name] = {"kind": "known_torch_dynamic_namespace",
                        "reported_pseudo_file": str(origin), "implementation_module": owner_name,
                        "implementation_type": type_name, "file": implementation.name,
                        "sha256": self.file_hash(implementation),
                        "native_binding": "loaded executable mappings are separately hashed"}
                    continue
            path = Path(origin).resolve()
            # Only actual pinned harness origins are exempt. An application
            # helper named triad_perf_extra is still application code.
            if path in pinned:
                pass
            elif path.is_relative_to(self.root):
                repository[str(path.relative_to(self.root))] = self.file_hash(path)
            elif name.startswith(("triad_", "_triad")):
                raise ValueError("receiver dependency loaded outside the requested checkout: " + name)
            if name.split(".")[0] in ("numpy", "scipy", "torch"):
                if path.is_relative_to(self.root):
                    raise ValueError("numerical library was shadowed by source checkout")
                numerical[name] = {"file": path.name, "sha256": self.file_hash(path)}
        mappings = Path("/proc/self/maps")
        native = []
        if mappings.is_file():
            files = set()
            for line in mappings.read_text().splitlines():
                parts = line.split(maxsplit=5)
                if len(parts) == 6 and "x" in parts[1] and parts[5].startswith("/"):
                    files.add(parts[5])
            for file in sorted(files):
                path = Path(file)
                if not path.is_file():
                    raise ValueError("an executable mapping cannot be identified: " + path.name)
                native.append({"file": path.name, "sha256": self.file_hash(path)})
        environment = {key: digest(value) for key, value in sorted(os.environ.items())
                       if key.startswith(CONTROL_PREFIXES)}
        result = {"schema": SCHEMA, "repository_modules": repository,
                  "numerical_modules": numerical, "executable_mappings": native,
                  "native_mapping_status": "observed" if native else "unavailable",
                  "versions": {"python": sys.version, "numpy": numpy.__version__,
                               "scipy": scipy.__version__, "torch": str(torch.__version__)},
                  "controls": {"environment_value_hashes": environment,
                               "threads": torch.get_num_threads(), "interop_threads": torch.get_num_interop_threads(),
                               "tf32_matmul": torch.backends.cuda.matmul.allow_tf32,
                               "tf32_cudnn": torch.backends.cudnn.allow_tf32,
                               "float32_matmul_precision": torch.get_float32_matmul_precision(),
                               "torch_parallel_info": torch.__config__.parallel_info()},
                  "hardware": hardware.discover_hardware(device, torch.get_num_threads()),
                  "measurement_files": {name: self.file_hash(WORKER.with_name(name)) for name in MEASUREMENT_FILES},
                  "timing_boundary": "unchanged ReceiverService.prepare wall_seconds plus score wall_seconds_including_finish",
                  "closure_scope": "observed repository Python helpers, NumPy/SciPy/Torch module files and executable native mappings; other Python stdlib/third-party sources are version scoped, not exhaustively attested",
                  "hash_cache_scope": "file-stat guarded hashes; no hostile file mutation guarantee"}
        return result


def serve(root, threads):
    # Load numerical packages before the requested source path enters sys.path.
    import numpy  # noqa: F401
    import scipy  # noqa: F401
    import torch  # noqa: F401
    module = load_harness("triad_perf_receiver.py", "_triad_identical_receiver")
    service = module.ReceiverService(root, threads)
    audit = RuntimeAudit(root, service)
    for line in sys.stdin:
        request = None
        try:
            request = json.loads(line)
            if request.get("action") == "audit":
                result = audit.snapshot(request["device"])
            else:
                result = service.dispatch(request)
            response = {"ok": True, "result": result}
        except Exception as error:
            response = {"ok": False, "error": f"{type(error).__name__}: {error}"}
        sys.stdout.write(json.dumps(response, separators=(",", ":"), allow_nan=False) + "\n")
        sys.stdout.flush()
        if isinstance(request, dict) and request.get("action") == "close":
            break


def identity_agreement(audits, prepared, *, request_contract):
    """No fallback-to-identical when an identity field is absent or changed."""
    if set(audits) != set(SOURCES) or set(prepared) != set(SOURCES):
        raise ValueError("two independently observed implementation identities are required")
    missing, different = [], []
    for source in SOURCES:
        audit = audits[source]
        for key in ("repository_modules", "numerical_modules", "executable_mappings", "versions",
                    "controls", "hardware", "measurement_files"):
            if not audit.get(key):
                missing.append(source + ":" + key)
        if audit.get("native_mapping_status") != "observed":
            missing.append(source + ":native_mappings")
        for key in ("geometry", "input_hashes", "source_hashes"):
            if not prepared[source].get(key):
                missing.append(source + ":" + key)
    for key in set(audits["reference"]) | set(audits["candidate"]):
        if audits["reference"].get(key) != audits["candidate"].get(key):
            different.append("runtime:" + key)
    for key in ("geometry", "input_hashes", "source_hashes"):
        if prepared["reference"].get(key) != prepared["candidate"].get(key):
            different.append("prepared:" + key)
    core = {"runtime": audits["candidate"], "geometry": prepared["candidate"].get("geometry"),
            "input_hashes": prepared["candidate"].get("input_hashes"), "request_contract": request_contract}
    return {"status": "implementation_unchanged" if not missing and not different else "deduplication_ineligible",
            "missing": missing, "different": different, "identity": core, "identity_sha256": digest(core),
            "implementation_ratio": None, "implementation_ratio_confidence_interval": None,
            "scope": "identical observed DSP/numerical/native closure and requests; implementation comparison is not measured"}


def scalar_summary(values, policy):
    """One real sample stream; never manufacture a paired timing interval."""
    import numpy as np
    from triad_perf_adaptive import _log_interval
    data = tuple(values)
    if len(data) not in policy.looks or any(type(v) not in (int, float) or not math.isfinite(v) or v <= 0 for v in data):
        raise ValueError("positive full-time predictions at a predeclared look are required")
    # Reserve the existing three-slot budget for current mean, historical mean
    # and their absolute comparison; no multiplicity saving is claimed here.
    confidence = 1. - policy.alpha/(policy.family_count*len(policy.looks)*3)
    interval = _log_interval(np.log(data), confidence)
    # These samples may be abbreviated forecasts. Their replicate variation is
    # not a qualified future-full-execution prediction distribution.
    replicate_interval = interval.pop("future_execution_prediction_interval_seconds")
    precise = interval["relative_timing_half_width"] <= policy.relative_precision
    return {**interval, "sample_count": len(data), "confidence": confidence,
            "timing_precision_reached": precise,
            "stop_reason": "precision_reached" if precise else "sample_limit" if len(data) == policy.looks[-1] else "continue",
            "policy": {"looks": list(policy.looks), "family_count": policy.family_count,
                       "alpha": policy.alpha, "confidence_per_interval": confidence,
                       "relative_precision": policy.relative_precision,
                       "slowdown_threshold": policy.slowdown_threshold,
                       "model_allowance_log": policy.model_allowance_log},
            "estimand": "geometric mean full resident preparation-plus-score time",
            "training_prediction_replicate_interval_seconds": replicate_interval,
            "future_full_execution_interval_qualified": False,
            "assumptions": "independent stationary outer log times; conditional Student-t intervals",
            "paired_observations_constructed": False}


def absolute_comparison(summary, historical_full, *, model_log_allowance, threshold=1.3,
                        historical_look_count=4):
    """Conservative independent current/historical interval ratio in full seconds."""
    import numpy as np
    from triad_perf_adaptive import _log_interval
    values = tuple(historical_full)
    if len(values) < 3 or any(not math.isfinite(v) or v <= 0 for v in values):
        raise ValueError("historical absolute baseline requires at least three actual full runtimes")
    if not math.isfinite(model_log_allowance) or model_log_allowance < 0 or threshold <= 1:
        raise ValueError("invalid absolute regression allowance or threshold")
    if type(historical_look_count) is not int or historical_look_count < 1:
        raise ValueError("historical look count must be positive")
    current_looks = len(summary["policy"]["looks"])
    confidence = 1. - (1.-summary["confidence"])*min(1., current_looks/historical_look_count)
    historical = _log_interval(np.log(values), confidence)
    low, high = summary["timing_mean_confidence_interval_seconds"]
    old_low, old_high = historical["timing_mean_confidence_interval_seconds"]
    ratio = [low*math.exp(-model_log_allowance)/old_high,
             high*math.exp(model_log_allowance)/old_low]
    gate = "performance_regression" if ratio[0] > threshold else "pass" if ratio[1] <= threshold else "inconclusive"
    return {"gate": gate, "threshold": threshold, "full_time_ratio_interval": ratio,
            "current_full_seconds": summary["point_seconds"],
            "historical_full_seconds": historical["point_seconds"],
            "absolute_full_seconds_difference": summary["point_seconds"]-historical["point_seconds"],
            "historical": historical, "historical_interval_confidence": confidence,
            "historical_selection_look_count": historical_look_count, "model_log_allowance": model_log_allowance,
            "scope": "absolute workload comparison across independent sessions; no within-run implementation ratio",
            "future_model_coverage_qualified": False}


def routine_decision(values, historical_full, policy, *, model_log_allowance=0.):
    """Per-test log-mean slowdown bounds; no width target and no paired data.

    Historical first-three observations are fixed before any historical stopping
    look. Each one-sided ledger spends half its alpha on that historical bound
    and half across all declared current looks. Upper and lower directions have
    their own error budgets.
    """
    import numpy as np
    from scipy.stats import t as student_t
    current = tuple(values)
    historical = tuple(historical_full)[:3]
    if (len(current) not in policy.looks or len(historical) != 3 or
            any(type(v) not in (int, float) or not math.isfinite(v) or v <= 0
                for v in (*current, *historical))):
        raise ValueError("positive current times at a declared look and three historical full times are required")
    if not math.isfinite(model_log_allowance) or model_log_allowance < 0:
        raise ValueError("model allowance must be finite and nonnegative")
    current_log, historical_log = np.log(current), np.log(historical)
    current_mean, historical_mean = float(np.mean(current_log)), float(np.mean(historical_log))
    current_se = float(np.std(current_log, ddof=1)/math.sqrt(len(current)))
    historical_se = float(np.std(historical_log, ddof=1)/math.sqrt(len(historical)))
    current_alpha = policy.failure_error_budget/(2*len(policy.looks))
    historical_alpha = policy.failure_error_budget/2
    current_margin = float(student_t.ppf(1-current_alpha, len(current)-1))*current_se
    historical_margin = float(student_t.ppf(1-historical_alpha, len(historical)-1))*historical_se
    pass_current_alpha = policy.pass_error_budget/(2*len(policy.looks))
    pass_historical_alpha = policy.pass_error_budget/2
    pass_current_margin = float(student_t.ppf(1-pass_current_alpha, len(current)-1))*current_se
    pass_historical_margin = float(student_t.ppf(1-pass_historical_alpha, len(historical)-1))*historical_se
    point_log = current_mean-historical_mean
    timing_margin = current_margin+historical_margin
    lower = math.exp(point_log-timing_margin-model_log_allowance)
    upper = math.exp(point_log+pass_current_margin+pass_historical_margin+model_log_allowance)
    gate = "pass" if upper <= policy.slowdown_threshold else "performance_regression" if lower > policy.slowdown_threshold else "inconclusive"
    stop = ("decision_pass" if gate == "pass" else "decision_regression" if gate == "performance_regression"
            else "sample_limit" if len(current) == policy.looks[-1] else "continue")
    result = {"point_seconds": math.exp(current_mean), "sample_count": len(current),
        "timing_mean_confidence_interval_seconds": [math.exp(current_mean-current_margin), math.exp(current_mean+current_margin)],
        "relative_timing_half_width": math.expm1(current_margin), "log_standard_error": current_se,
        "timing_precision_reached": None, "stop_reason": stop,
        "policy": {"stopping_rule": "decision", "looks": list(policy.looks), "alpha": policy.alpha,
                   "slowdown_threshold": policy.slowdown_threshold, "cross_case_correction": False,
                   "relative_precision_used_for_stopping": False},
        "estimand": "geometric mean full resident preparation-plus-score time",
        "paired_observations_constructed": False, "future_full_execution_interval_qualified": False,
        "comparison": {"gate": gate, "threshold": policy.slowdown_threshold,
            "full_time_ratio_interval": [lower, upper],
            "full_time_ratio_interval_interpretation": "separate one-sided bounds with their declared directional error budgets",
            "one_sided_upper_slowdown_bound": upper, "one_sided_lower_slowdown_bound": lower,
            "current_full_seconds": math.exp(current_mean), "historical_full_seconds": math.exp(historical_mean),
            "absolute_full_seconds_difference": math.exp(current_mean)-math.exp(historical_mean),
            "per_test_one_sided_confidence": 1-policy.alpha,
            "upper_ledger": {"family_alpha": policy.alpha, "current_per_look_alpha": current_alpha,
                             "historical_fixed_alpha": historical_alpha, "current_looks": list(policy.looks)},
            "lower_ledger": {"family_alpha": policy.alpha, "current_per_look_alpha": current_alpha,
                             "historical_fixed_alpha": historical_alpha, "separate_from_upper": True},
            "historical_selection": "fixed first three actual full observations, before the first qualification stopping look",
            "historical_full_seconds_used": list(historical), "timing_log_margin": timing_margin,
            "model_log_allowance": model_log_allowance,
            "model_allowance_scope": "separate historical engineering envelope; not a 95% confidence statement",
            "scope": "independent historical/current log-mean component bounds; no within-run implementation ratio",
            "assumptions": "independent stationary normal outer log runtimes; current session independent of historical session",
            "future_model_coverage_qualified": False}}
    if policy.pass_alpha is not None or policy.failure_alpha is not None:
        result["policy"].update(pass_alpha=policy.pass_error_budget, failure_alpha=policy.failure_error_budget)
        comparison = result["comparison"]
        comparison.pop("per_test_one_sided_confidence")
        comparison.update(pass_one_sided_confidence=1-policy.pass_error_budget,
                          failure_one_sided_confidence=1-policy.failure_error_budget,
                          pass_timing_log_margin=pass_current_margin+pass_historical_margin,
                          failure_timing_log_margin=timing_margin)
        comparison["upper_ledger"].update(family_alpha=policy.pass_error_budget,
            current_per_look_alpha=pass_current_alpha, historical_fixed_alpha=pass_historical_alpha)
        comparison["lower_ledger"].update(family_alpha=policy.failure_error_budget)
    return result


def measurement_core_hashes():
    """Stable bodies a reviewed reporting-only projection cannot change."""
    selected = {"triad_perf_identical_fft.py": ("RuntimeAudit", "serve", "identity_agreement", "work_receipt",
                    "scalar_summary", "absolute_comparison", "_qualification", "anchor_valid", "replay_work"),
                "triad-performance.py": ("ReceiverWorker", "child_environment", "prepare_request", "score_request"),
                "triad_perf_fft_trace.py": ("measurement_plan", "_fit", "_positive", "_remaining"),
                "triad_perf_adaptive.py": ("ModelConfirmationPolicy", "model_confirmation_policy", "_log_interval", "qualify_log_residuals")}
    sources = {name: ast.parse(WORKER.with_name(name).read_text()) for name in selected}
    found = {}
    for filename, names in selected.items():
        for node in sources[filename].body:
            if isinstance(node, (ast.FunctionDef, ast.ClassDef)) and node.name in names:
                found[filename+":"+node.name] = hashlib.sha256(ast.dump(node, include_attributes=False).encode()).hexdigest()
    for node in sources["triad_perf_identical_fft.py"].body:
        if isinstance(node, ast.FunctionDef) and node.name == "_measure_identical_fft":
            inner = next(n for n in node.body if isinstance(n, ast.FunctionDef) and n.name == "measure")
            found["triad_perf_identical_fft.py:_measure_identical_fft.measure"] = hashlib.sha256(ast.dump(inner, include_attributes=False).encode()).hexdigest()
    if len(found) != 22:
        raise ValueError("declared measurement body missing")
    return found


def historical_applicability(record, identity, projection=None):
    """Keep raw proof; optionally apply an explicit, exact reviewed hash map."""
    if not record or not anchor_valid(record):
        return {"compatible": False, "reason": "missing or invalid original anchor proof"}
    if record["identity"] == identity:
        return {"compatible": True, "method": "exact_identity", "original_anchor_sha256": record["record_sha256"]}
    if not projection:
        return {"compatible": False, "reason": "identity changed; no reviewed reporting-only projection"}
    try:
        if (projection["schema"] != "triad-identical-fft-reporting-projection-v1" or
                projection["reviewed_reporting_only"] is not True or
                projection["unchanged_measurement_core"] != measurement_core_hashes()):
            raise ValueError("projection lacks matching reviewed measurement bodies")
        old = record["identity"]["runtime"]["measurement_files"]
        new = identity["runtime"]["measurement_files"]
        changes = projection["measurement_files"]
        different = {name for name in old.keys() | new.keys() if old.get(name) != new.get(name)}
        if (not different or set(changes) != different or
                not different <= {"triad_perf_identical_fft.py", "triad_perf_adaptive.py", "triad-performance.py", "triad_perf_fft_trace.py"}):
            raise ValueError("projection must match only the exact declared reporting-file changes")
        for name in different:
            if changes[name] != {"historical_sha256": old[name], "current_sha256": new[name]}:
                raise ValueError("projection source hashes do not match")
        projected = json.loads(json.dumps(record["identity"]))
        projected["runtime"]["measurement_files"] = new
        if projected != identity:
            raise ValueError("DSP/runtime/control/input/geometry identity changed outside the reporting projection")
        return {"compatible": True, "method": "reviewed_reporting_only_projection",
            "projection_sha256": digest(projection), "original_anchor_sha256": record["record_sha256"],
            "original_identity_sha256": record["identity_sha256"], "current_identity_sha256": digest(identity),
            "source_changes": changes, "original_proof_rewritten": False}
    except (KeyError, ValueError, TypeError, AttributeError):
        return {"compatible": False, "reason": "reviewed reporting-only projection inapplicable"}


def _qualification(errors):
    from triad_perf_adaptive import qualify_log_residuals, model_confirmation_policy
    return qualify_log_residuals(errors, maximum_relative_error=.05,
                                 confidence=model_confirmation_policy().confidence)


def work_receipt(value, requested_count, lag_count):
    """Use accumulated observable-rank counts, not only nominal bank metadata."""
    histogram = value.get("rank_histogram")
    if (not isinstance(histogram, list) or not histogram or
            any(type(v) is not int or v < 0 for v in histogram)):
        raise ValueError("FFT returned no valid executed-group rank histogram")
    actual = sum(histogram)
    expected = requested_count*lag_count
    geometry = value.get("geometry", {})
    if (actual != expected or value.get("measured_group_count") != actual or
            geometry.get("group_count") != actual):
        raise ValueError("FFT executed a different work count or contradictory geometry")
    return {"requested_cfo_count": requested_count, "lag_count": lag_count,
            "measured_group_count": value["measured_group_count"],
            "completed_group_count_from_rank_histogram": actual,
            "rank_histogram": histogram, "geometry": geometry,
            "counter_scope": "sum of rank counts accumulated by every executed whole-observation likelihood tile"}


def replay_work(row, counts, target, lags):
    if [s["count"] for s in row["scores"]] != counts:
        return False
    for score, count in zip(row["scores"], counts, strict=True):
        receipt = score["work"]
        if (receipt["requested_cfo_count"] != count or receipt["lag_count"] != lags or
                receipt != work_receipt(receipt, count, lags)):
            return False
        if not math.isfinite(score["seconds"]) or score["seconds"] <= 0:
            return False
    if counts == [target]:
        return (row["score_seconds"] == row["scores"][0]["seconds"] and
                row["full_seconds"] == row["preparation_seconds"]+row["score_seconds"])
    return True


def anchor_valid(record, identity=None):
    """Replay new single-source proof without rewriting any historical record."""
    from triad_perf_adaptive import AdaptivePolicy, model_confirmation_policy
    try:
        if record["schema"] != SCHEMA or record["status"] not in ("validated", "direct_full"):
            return False
        if record["record_sha256"] != digest({k: v for k, v in record.items() if k != "record_sha256"}):
            return False
        if identity is not None and record["identity"] != identity:
            return False
        if record["identity_sha256"] != digest(record["identity"]):
            return False
        identity = record["identity"]
        target = len(identity["request_contract"]["score"]["frequency_offsets_hz"])
        lags = identity["geometry"]["lags"]
        if (type(target) is not int or target < 1 or type(lags) is not int or lags < 1 or
                identity["request_contract"]["score"]["full_group_count_for_reporting"] != target*lags):
            return False
        raw_policy = record["adaptive"]["policy"]
        policy = AdaptivePolicy(relative_precision=raw_policy["relative_precision"], looks=tuple(raw_policy["looks"]),
            family_count=raw_policy["family_count"], alpha=raw_policy["alpha"],
            slowdown_threshold=raw_policy["slowdown_threshold"], model_allowance_log=raw_policy["model_allowance_log"])
        from triad_perf_fft_trace import _fit, measurement_plan
        original = measurement_plan(target, policy.looks[0])
        plan = record["measurement_plan"]
        if plan["target_count"] != target:
            return False
        if plan != original:
            if plan != {**original, "direct": True, "direct_reason": "prospective_full_cost_guard"} or original["direct"]:
                return False
            pilot = record["discarded_pilot"]
            if not replay_work(pilot, original["training_counts"], target, lags):
                return False
            pilot_model = _fit(pilot["preparation_seconds"], original["training_counts"],
                               [r["seconds"] for r in pilot["scores"]], target)
            cost = pilot["preparation_seconds"]+sum(r["seconds"] for r in pilot["scores"])
            if pilot_model["predicted_full_seconds"] >= 1. and cost < .8*pilot_model["predicted_full_seconds"]:
                return False
        counts = [target] if plan["direct"] else plan["training_counts"]
        if not plan["direct"] and target > 8*counts[-1]:
            return False
        if (any(not replay_work(r, counts, target, lags) for r in record["training"]) or
                any(not replay_work(r, [target], target, lags) for r in record["full_runs"])):
            return False
        values = [r["full_seconds"] if plan["direct"] else _fit(r["preparation_seconds"], counts,
                  [s["seconds"] for s in r["scores"]], target)["predicted_full_seconds"] for r in record["training"]]
        if record["adaptive"] != scalar_summary(values, policy):
            return False
        if any(scalar_summary(values[:n], policy)["stop_reason"] != "continue" for n in policy.looks if n < len(values)):
            return False
        unique_rows = record["training"] if plan["direct"] else [*record["training"], *record["full_runs"]]
        ids = [r["id"] for r in unique_rows]
        if (len(ids) != len(set(ids)) or any(not isinstance(i, str) or not i for i in ids) or
                any(not math.isfinite(r["preparation_seconds"]) or r["preparation_seconds"] <= 0 for r in unique_rows)):
            return False
        if record["status"] == "direct_full":
            return plan["direct"] and record["training"] == record["full_runs"]
        if plan["direct"]:
            return False
        frozen = record["frozen"]
        if frozen["commitment_sha256"] != digest({k: v for k, v in frozen.items() if k != "commitment_sha256"}):
            return False
        if frozen["confirmation_policy"] != model_confirmation_policy().to_dict():
            return False
        if (len(record["full_runs"]) not in model_confirmation_policy().looks or
                frozen["maximum_holds"] not in model_confirmation_policy().looks or
                len(record["full_runs"]) > frozen["maximum_holds"] or
                frozen["training_ids"] != [r["id"] for r in record["training"]] or
                frozen["identity_sha256"] != record["identity_sha256"] or
                frozen["prediction_frozen_before_validation"] is not True):
            return False
        if frozen["target_count"] != target:
            return False
        models = [_fit(r["preparation_seconds"], [s["count"] for s in r["scores"]],
                       [s["seconds"] for s in r["scores"]], target) for r in record["training"]]
        if (models != [r["model"] for r in record["training"]] or
                not math.isclose(statistics.geometric_mean(m["predicted_full_seconds"] for m in models),
                                 frozen["predicted_full_seconds"], rel_tol=1e-13)):
            return False
        errors = [math.log(r["full_seconds"]/frozen["predicted_full_seconds"]) for r in record["full_runs"]]
        if any(r["full_seconds"] != r["preparation_seconds"]+r["score_seconds"] for r in record["full_runs"]):
            return False
        ids = [r["id"] for r in (*record["training"], *record["full_runs"])]
        earlier = [_qualification(errors[:n])["status"] for n in model_confirmation_policy().looks if n < len(errors)]
        return (len(ids) == len(set(ids)) and all(s == "inconclusive" for s in earlier)
                and record["qualification"] == _qualification(errors)
                and record["qualification"]["status"] == "validated")
    except (KeyError, ValueError, TypeError, ArithmeticError):
        return False


def legacy_projection(record, current_legacy_identity, archive_hashes):
    """Retain a reviewed original proof at its declared runtime-profile scope.

    The old archive binds the measured adapter; it did not retain loaded native
    library bytes. Therefore this projection alone cannot authorize the new
    exact-binary absolute regression gate. It is still useful historical full
    evidence, and is never modified into a new-style anchor.
    """
    from triad_perf_fft_trace import anchor_applicability
    applied = anchor_applicability(record, current_legacy_identity)
    worker_matches = (archive_hashes.get("tools/triad_perf_receiver.py") == LEGACY_WORKER_SHA256
                      and sha(WORKER.with_name("triad_perf_receiver.py")) == LEGACY_WORKER_SHA256)
    compatible = applied["compatible"] and worker_matches
    return {"status": "historical_profile_matched" if compatible else "inapplicable",
            "original_proof_sha256": record.get("payload_sha256"), "applicability": applied,
            "archived_worker_matches": worker_matches,
            "absolute_regression_qualified": False,
            "historical_full_seconds": [r["observed"]["candidate"]["full_seconds"]
                                        for r in record.get("full_validation", [])] if compatible else [],
            "missing_historical_evidence": ["loaded numerical/native binary closure was not retained"],
            "scope": "original source/version/build-profile timing contract; not retroactive executable-byte attestation"}


def _measure_identical_fft(case, workers, precision, device, *, policy, api, deadline,
                          output, mode="reuse", historical=None, maximum_holds=9,
                          reporting_projection=None):
    """Measure one stream, with exact-identity eligibility and full-bank proof."""
    from triad_perf_adaptive import model_confirmation_policy
    import triad_perf_fft_trace as trace
    if mode not in ("reuse", "qualify") or maximum_holds not in model_confirmation_policy().looks:
        raise ValueError("invalid identical FFT qualification mode or hold count")
    if mode == "qualify" and policy.stopping_rule != "precision":
        raise ValueError("qualification retains the original precision stopping policy")
    output = Path(output)
    start = time.perf_counter()
    target = len(case["frequency_offsets_hz"])
    plan = trace.measurement_plan(target, policy.looks[0])
    append_receipt(output, "start", {"case": case, "plan": plan, "precision": precision, "device": device,
                                    "mode": mode, "maximum_holds": maximum_holds})
    prepared, probes, audits = {}, {}, {}
    for name in SOURCES:
        prepared[name] = workers[name].request(api.prepare_request(case, precision, device))
        append_receipt(output, "identity-preparation", {"source": name, "receipt": prepared[name]})
        probes[name] = workers[name].request(api.score_request(case, min(3, target)))
        append_receipt(output, "identity-score", {"source": name, "receipt": probes[name]})
        work_receipt(probes[name], min(3, target), case["configuration"]["lag_group_count"])
        audits[name] = workers[name].request({"action": "audit", "device": device})
    contract = {"prepare": {k: v for k, v in api.prepare_request(case, precision, device).items()
                            if k != "input_directory"},
                "score": api.score_request(case, target)}
    agreement = identity_agreement(audits, prepared, request_contract=contract)
    numeric = api.numeric_agreement(probes["reference"], probes["candidate"])
    append_receipt(output, "identity", agreement)
    report = {"schema": SCHEMA, "id": case["id"], "status": "running", "identity_agreement": agreement,
              "identity": agreement["identity"], "identity_sha256": agreement["identity_sha256"],
              "numeric_agreement": numeric, "mode": mode, "training": [], "full_runs": [],
              "prediction_valid": False, "comparison": {"gate": "inconclusive"},
              "implementation_comparison": {"status": agreement["status"], "ratio": None,
                                             "confidence_interval": None, "measured": False},
              "timing_scope": audits["candidate"].get("timing_boundary"),
              "excluded_costs": ["fixture generation", "worker startup", "runtime identity audit", "evidence writes"],
              "qualification": None, "historical_anchor_sha256": None}
    if agreement["status"] != "implementation_unchanged" or not numeric["eligible"]:
        report["status"] = "deduplication_ineligible"
        save(output/"result.json", report)
        return report
    applicability = historical_applicability(historical, report["identity"], reporting_projection)
    report["historical_applicability"] = applicability
    historical_valid = applicability["compatible"]
    if historical_valid:
        report["historical_anchor_sha256"] = historical["record_sha256"]
    worker = workers["candidate"]
    expected = prepared["candidate"]
    serial = 0
    run_id = str(time.time_ns())

    def measure(counts):
        nonlocal serial
        serial += 1
        identifier = run_id+":"+str(serial)
        trace._remaining(deadline)
        before = worker.request(api.prepare_request(case, precision, device))
        append_receipt(output, "preparation", {"id": identifier, "requested_counts": counts, "receipt": before})
        if any(before[k] != expected[k] for k in ("geometry", "input_hashes", "source_hashes")):
            raise ValueError("FFT input/geometry/source changed during single-stream timing")
        scores = []
        for count in counts:
            trace._remaining(deadline)
            value = worker.request(api.score_request(case, count))
            append_receipt(output, "score", {"id": identifier, "requested_count": count, "receipt": value})
            work = work_receipt(value, count, case["configuration"]["lag_group_count"])
            trace._positive(value["wall_seconds_including_finish"], "score")
            if not all(math.isfinite(v) for v in value["bit_log_likelihoods"]):
                raise ValueError("FFT numerical output is nonfinite")
            scores.append({"count": count, "seconds": value["wall_seconds_including_finish"],
                           "work": work,
                           "arithmetic": value.get("arithmetic"), "resources": value.get("resources"),
                           "receiver_stages": value.get("timing")})
        row = {"id": identifier, "preparation_seconds": trace._positive(before["wall_seconds"], "preparation"),
               "scores": scores, "preparation_stages": before.get("stages")}
        if counts == [target]:
            row["score_seconds"] = scores[0]["seconds"]
            row["full_seconds"] = row["preparation_seconds"]+row["score_seconds"]
        append_receipt(output, "observation", row)
        return row

    counts = [target] if plan["direct"] else plan["training_counts"]
    pilot = measure(counts)
    if not plan["direct"]:
        pilot_model = trace._fit(pilot["preparation_seconds"], counts, [r["seconds"] for r in pilot["scores"]], target)
        cost = pilot["preparation_seconds"]+sum(r["seconds"] for r in pilot["scores"])
        if pilot_model["predicted_full_seconds"] < trace.CHEAP_FULL_SECONDS or cost >= .8*pilot_model["predicted_full_seconds"]:
            plan = {**plan, "direct": True, "direct_reason": "prospective_full_cost_guard"}
            counts = [target]
    report.update(measurement_plan=plan, discarded_pilot=pilot)
    model_allowance, individual = 0., 0.
    if historical_valid and mode == "reuse" and not plan["direct"]:
        if historical["status"] != "validated":
            raise ValueError("an abbreviated route requires a validated abbreviated anchor")
        history_errors = [math.log(r["full_seconds"]/historical["frozen"]["predicted_full_seconds"])
                          for r in historical["full_runs"]]
        validation = _qualification(history_errors)
        model_allowance = max(math.log1p(.10), *(abs(v) for v in validation["log_residual_interval"]))
        individual = max(model_allowance, *(abs(v) for v in history_errors))
    values = []
    for count in range(1, policy.looks[-1]+1):
        row = measure(counts)
        if plan["direct"]:
            predicted = row["full_seconds"]
        else:
            row["model"] = trace._fit(row["preparation_seconds"], counts, [r["seconds"] for r in row["scores"]], target)
            predicted = row["model"]["predicted_full_seconds"]
        values.append(predicted)
        report["training"].append(row)
        if count in policy.looks:
            if mode == "reuse" and policy.stopping_rule == "decision":
                if historical_valid:
                    summary = routine_decision(values, [r["full_seconds"] for r in historical["full_runs"]],
                                               policy, model_log_allowance=model_allowance)
                    if not plan["direct"] and summary["comparison"]["gate"] == "pass":
                        half = math.log(summary["timing_mean_confidence_interval_seconds"][1]/summary["point_seconds"])
                        # This is the unchanged historical model applicability
                        # budget, not a new target for runtime interval width.
                        possible = model_allowance+half <= math.log1p(.20) and half <= individual
                        summary["historical_model_applicability_possible"] = possible
                        if not possible:
                            summary["stop_reason"] = "sample_limit" if count == policy.looks[-1] else "continue"
                else:
                    summary = {"point_seconds": statistics.geometric_mean(values), "sample_count": count,
                        "stop_reason": "no_applicable_absolute_anchor", "timing_precision_reached": None,
                        "policy": {"stopping_rule": "decision", "looks": list(policy.looks), "alpha": policy.alpha},
                        "estimand": "geometric mean full resident preparation-plus-score time",
                        "paired_observations_constructed": False}
            else:
                summary = scalar_summary(values, policy)
            append_receipt(output, "timing-look", summary)
            if summary["stop_reason"] != "continue":
                break
    report["adaptive"] = summary
    mean = summary["point_seconds"]
    report["frozen"] = {"predicted_full_seconds": mean, "target_count": target,
                        "identity_sha256": report["identity_sha256"],
                        "training_ids": [r["id"] for r in report["training"]],
                        "confirmation_policy": model_confirmation_policy().to_dict(),
                        "maximum_holds": maximum_holds,
                        "prediction_frozen_before_validation": True}
    if plan["direct"]:
        report["status"], report["prediction_valid"] = "direct_full", True
        report["full_runs"] = report["training"]
    elif historical_valid and mode == "reuse":
        report["frozen"]["conditional_model"] = {"historical_anchor_sha256": historical["record_sha256"],
            "mean_model_log_allowance": model_allowance, "observation_log_allowance": individual,
            "maximum_mean_plus_timing_log_allowance": math.log1p(.20),
            "future_coverage_qualified": False, "scope": "fixed engineering envelope; one sentinel cannot establish future coverage"}
    elif mode == "reuse":
        report["status"] = "no_applicable_absolute_anchor"
    report["frozen"]["commitment_sha256"] = digest(report["frozen"])
    save(output/"forecast.json", report["frozen"])
    if not plan["direct"] and (mode == "qualify" or historical_valid):
        for index in range(1 if mode == "reuse" else maximum_holds):
            row = measure([target]); report["full_runs"].append(row)
            errors = [math.log(r["full_seconds"]/mean) for r in report["full_runs"]]
            if mode == "reuse":
                half = math.log(summary["timing_mean_confidence_interval_seconds"][1]/mean)
                residual = errors[0]
                supported = (model_allowance+half <= math.log1p(.20)
                             and -individual <= residual-half <= residual+half <= individual)
                report["sentinel"] = {"log_residual": residual, "training_log_half_width": half,
                    "status": "historically_supported_conditional" if supported else "unqualified",
                    "model_envelope_changed": False, "future_coverage_qualified": False}
                report["status"] = report["sentinel"]["status"]
                report["prediction_valid"] = supported
                break
            if index+1 in model_confirmation_policy().looks:
                report["qualification"] = _qualification(errors)
                report["status"] = report["qualification"]["status"]
                report["prediction_valid"] = report["status"] == "validated"
                if report["status"] in ("validated", "invalid"):
                    break
        if mode == "qualify" and report["qualification"]:
            model_allowance = max(abs(v) for v in report["qualification"]["log_residual_interval"])
    if report["prediction_valid"] and historical_valid:
        if mode == "reuse" and policy.stopping_rule == "decision":
            report["comparison"] = summary["comparison"]
        else:
            report["comparison"] = absolute_comparison(summary, [r["full_seconds"] for r in historical["full_runs"]],
                model_log_allowance=model_allowance, threshold=policy.slowdown_threshold,
                historical_look_count=(len(historical["adaptive"]["policy"]["looks"])
                    if historical["status"] == "direct_full" else len(model_confirmation_policy().looks)))
    else:
        report["comparison"] = {"gate": "inconclusive", "reason": "no compatible historical absolute baseline or current model unsupported"}
    final_audits = {name: workers[name].request({"action": "audit", "device": device}) for name in SOURCES}
    if final_audits != audits:
        report["status"], report["prediction_valid"] = "runtime_identity_changed", False
        report["comparison"] = {"gate": "inconclusive"}
    report["wall_seconds"] = time.perf_counter()-start
    report["full_measurement_count"] = len(report["full_runs"])
    report["fresh_timing_streams"] = 1
    report["record_sha256"] = digest(report)
    save(output/"result.json", report)
    return report


def measure_identical_fft(*args, **kwargs):
    """Retain raw completed operations and an explicit error receipt on failure."""
    output = Path(kwargs["output"])
    output.mkdir(parents=True, exist_ok=False)
    started = time.perf_counter()
    try:
        return _measure_identical_fft(*args, **kwargs)
    except BaseException as error:
        save(output/"failure.json", {"schema": SCHEMA, "status": "measurement_failed",
            "error": {"type": type(error).__name__, "message": str(error)},
            "wall_seconds": time.perf_counter()-started, "raw_observations": "observations.jsonl",
            "prediction_valid": False, "comparison": {"gate": "inconclusive"}})
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serve", action="store_true")
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--historical-fixture-source", action="store_true",
                        help="allow missing Serpent files in an explicitly historical fixture source tree")
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--reference-root", type=Path)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--fixture-cache", type=Path)
    parser.add_argument("--case", action="append", dest="cases")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--anchor-directory", type=Path)
    parser.add_argument("--device", choices=("cpu", "cuda"), default="cpu")
    parser.add_argument("--precision", choices=("auto", "fp32", "fp64"), default="auto")
    parser.add_argument("--mode", choices=("qualify", "reuse"), default="reuse")
    parser.add_argument("--stopping-rule", choices=("decision", "precision"),
                        help="defaults to decision for reuse and original precision for qualification")
    parser.add_argument("--regression-profile", choices=("normal", "ci", "strict"), default="normal",
                        help="routine decision criteria; precision stopping uses historical criteria")
    parser.add_argument("--reporting-projection", type=Path,
                        help="reviewed reporting-only hash map; defaults to anchor-directory/reporting-projection.json if present")
    parser.add_argument("--budget-seconds", type=float, default=600.)
    parser.add_argument("--maximum-holds", type=int, choices=(3, 5, 9, 17), default=9)
    parser.add_argument("--looks", default="5,9,17,33")
    args = parser.parse_args(argv)
    if not 1 <= args.threads <= 256:
        parser.error("threads must be 1..256")
    if args.serve:
        serve(args.source_root, args.threads); return 0
    if not all((args.reference_root, args.binary, args.fixture_cache, args.cases, args.output)):
        parser.error("controller mode needs reference-root, binary, fixture-cache, case and output")
    if not math.isfinite(args.budget_seconds) or not 0 < args.budget_seconds <= 28800:
        parser.error("invalid deadline")
    from triad_perf_adaptive import AdaptivePolicy, regression_profile
    from triad_perf_cases import prepare_cases
    from triad_perf_hardware import discover_hardware
    api = load_harness("triad-performance.py", "_triad_identical_controller")
    stopping_rule = args.stopping_rule or ("precision" if args.mode == "qualify" else "decision")
    if args.mode == "qualify" and stopping_rule != "precision":
        parser.error("qualification requires the original precision stopping policy")
    policy = AdaptivePolicy(looks=tuple(map(int, args.looks.split(","))), family_count=14,
                            stopping_rule=stopping_rule,
                            **(regression_profile(args.regression_profile) if stopping_rule == "decision" else {}))
    projection_path = args.reporting_projection or (args.anchor_directory/"reporting-projection.json" if args.anchor_directory else None)
    projection = json.loads(projection_path.read_text()) if projection_path and projection_path.is_file() else None
    if args.reporting_projection and projection is None:
        parser.error("explicit reporting projection does not exist")
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=False)
    deadline = time.monotonic()+args.budget_seconds
    workers, rows = {}, []
    try:
        cases = prepare_cases(args.binary.resolve(), args.fixture_cache.resolve(), profile="ci",
                              deadline=deadline, source_root=args.source_root.resolve(),
                              allow_historical_missing=args.historical_fixture_source, case_ids=args.cases)
        if sorted(c["id"] for c in cases) != sorted(args.cases):
            raise ValueError("requested FFT cases missing or duplicated")
        for name, root in (("reference", args.reference_root), ("candidate", args.source_root)):
            workers[name] = api.ReceiverWorker(root.resolve(), threads=args.threads, deadline=deadline,
                log_directory=output, name=name, adapter=WORKER)
        for case in cases:
            case["performance_environment"] = discover_hardware(args.device, args.threads)
            numeric = api.qualify_fft(case, workers, args.device)
            precision = numeric["selected"] if args.precision == "auto" else args.precision
            if not numeric["alternatives"][precision]["eligible"]:
                raise ValueError("requested FFT arithmetic failed the strict reference corpus")
            path = args.anchor_directory/(case["id"]+".identical-anchor.json") if args.anchor_directory else None
            historical = json.loads(path.read_text()) if path and path.is_file() else None
            value = measure_identical_fft(case, workers, precision, args.device, policy=policy, api=api,
                deadline=deadline, output=output/case["id"], mode=args.mode, historical=historical,
                maximum_holds=args.maximum_holds, reporting_projection=projection)
            save(output/case["id"] / "precision-qualification.json", numeric)
            rows.append(value)
            if path and not path.exists() and anchor_valid(value):
                save(path, value)
    finally:
        for worker in workers.values():
            worker.close()
        save(output/"summary.json", {"schema": SCHEMA,
            "cases": [{"id": row["id"], "status": row["status"], "gate": row["comparison"]["gate"],
                       "prediction_valid": row["prediction_valid"]} for row in rows],
            "complete_suite_claim": False})
    supported = rows and all(r["prediction_valid"] for r in rows)
    if args.mode == "reuse":
        supported = supported and all(r["comparison"]["gate"] == "pass" for r in rows)
    return 0 if supported else 2


if __name__ == "__main__":
    raise SystemExit(main())
