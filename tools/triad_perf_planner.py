#!/usr/bin/env python3
"""Direct full-target regression for the isolated native planners and bounds.

Each statistical observation is a fresh complete native process with a fixed
bundle. Cheap full work is measured directly: there is no abbreviated model to
qualify or historical calibration to repeat. Retained pairs are source-bound
full-run anchors, not predictions for an unmeasured duration or hardware.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict, replace
import hashlib
import importlib.metadata
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = Path(__file__).with_name("triad-performance-planner.json")
DRIVER = "tools/triad-planner-perf.cpp"
SCHEMA = "triad-performance-planner-v1"
GAP = "regime.v3-native-planner-statistics"
BOUNDS_GAP = "regime.gaussian-bound-planner"
BOUNDS_DEPENDENCIES = ("triad_gaussian_bounds.py", "triad_gaussian_state.py", "triad_state_workspace.py")
BOUNDS_THREAD_ENV = {name: "2" for name in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS",
                                          "NUMEXPR_NUM_THREADS", "BLIS_NUM_THREADS", "VECLIB_MAXIMUM_THREADS")}
BOUNDS_THREAD_ENV.update(OMP_DYNAMIC="FALSE", MKL_DYNAMIC="FALSE")
BOUNDS_INHERITED_ENV = ("OMP_PROC_BIND", "OMP_PLACES", "GOMP_CPU_AFFINITY", "KMP_AFFINITY",
                        "KMP_BLOCKTIME", "OMP_WAIT_POLICY", "OPENBLAS_CORETYPE", "MKL_ENABLE_INSTRUCTIONS", "MKL_CBWR")
DEPENDENCIES = (
    "src/triad_statistics.cpp", "src/triad_modem_v3.cpp", "src/triad_shaping.cpp",
    "include/datapump/triad_statistics.hpp", "include/datapump/triad_modem_v3.hpp",
    "include/datapump/triad_modem.hpp", "include/datapump/triad_simulation.hpp",
    "include/datapump/crypto.hpp", "include/datapump/pattern_pulse.hpp", "include/datapump/triad_shaping.hpp",
)


def file_sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                    allow_nan=False).encode()).hexdigest()


def write_json(path, value):
    with Path(path).open("x") as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write("\n")


def manifest():
    value = json.loads(MANIFEST.read_text())
    cases = value.get("cases", [])
    if value.get("schema") != "triad-performance-planner-cases-v1" or len(cases) != 6:
        raise ValueError("invalid planner manifest")
    if len({row["id"] for row in cases}) != len(cases):
        raise ValueError("duplicate planner case")
    for row in cases:
        if type(row["repetitions"]) is not int or not 1 <= row["repetitions"] <= 1000000:
            raise ValueError("invalid planner repetitions")
        if row["scenarios_per_repetition"] != len(row["outputs"]):
            raise ValueError("invalid planner scenario count")
    return value


def source_identity(root, binary):
    root = Path(root).resolve(strict=True)
    files = {name: file_sha(root/name) for name in DEPENDENCIES}
    # This exact ordered byte format is also computed by CMake.
    production = hashlib.sha256("".join(f"{name}:{files[name]}\n" for name in DEPENDENCIES).encode()).hexdigest()
    return {"binary_sha256": file_sha(binary), "driver_sha256": file_sha(root/DRIVER),
            "production_sha256": production, "declared_production_dependencies": files,
            "dependency_scope": "listed source/header bytes; standard library, compiler and runtime are separately identified by build/environment evidence"}


def prepare_reference_driver(candidate_root, reference_root):
    """Explicit build preparation: overlay only this identical test driver.

    The caller selects a private reference checkout. No production source is
    copied from the candidate. Existing native binaries are not overwritten by
    this operation; the caller builds the new target through its normal build.
    """
    candidate_root = Path(candidate_root).resolve(strict=True)
    reference_root = Path(reference_root).resolve(strict=True)
    if candidate_root == reference_root:
        raise ValueError("reference test adapter requires a distinct private checkout")
    before = {name: file_sha(reference_root/name) for name in DEPENDENCIES}
    candidate = (candidate_root/"CMakeLists.txt").read_text()
    start = "  add_executable(triad_planner_perf "
    end = "  add_executable(triad_oscillator_study "
    begin = candidate.index(start)
    block = candidate[begin:candidate.index(end, begin)]
    cmake_path = reference_root/"CMakeLists.txt"
    old = cmake_path.read_text()
    if start in old:
        old_begin = old.index(start)
        updated = old[:old_begin]+block+old[old.index(end, old_begin):]
    else:
        if old.count(end) != 1:
            raise ValueError("reference CMake target insertion anchor is ambiguous")
        updated = old.replace(end, block+end, 1)
    old_exclusion = '_target STREQUAL "triad_long" OR _target STREQUAL "triad_oscillator_study"'
    if '_target STREQUAL "triad_planner_perf"' not in updated:
        if updated.count(old_exclusion) != 1:
            raise ValueError("reference isolated-audio exclusion anchor is ambiguous")
        updated = updated.replace(old_exclusion,
            '_target STREQUAL "triad_long" OR _target STREQUAL "triad_planner_perf" OR _target STREQUAL "triad_oscillator_study"', 1)
    old_cmake_sha = file_sha(cmake_path)
    cmake_path.write_text(updated)
    (reference_root/DRIVER).write_bytes((candidate_root/DRIVER).read_bytes())
    after = {name: file_sha(reference_root/name) for name in DEPENDENCIES}
    if after != before:
        raise ValueError("reference production source changed during test-adapter preparation")
    return {"schema": "triad-planner-reference-build-overlay-v1",
            "driver_sha256": file_sha(reference_root/DRIVER),
            "reference_cmake_before_sha256": old_cmake_sha,
            "reference_cmake_after_sha256": file_sha(cmake_path),
            "candidate_cmake_sha256": file_sha(candidate_root/"CMakeLists.txt"),
            "production_dependencies_before": before, "production_dependencies_after": after,
            "production_unchanged": True, "compiled_or_built": False}


def environment_identity():
    from triad_perf_native import environment_identity as native_environment
    return native_environment(1)


def workspace_identity(directory):
    directory = Path(directory).resolve(strict=True)
    metadata = json.loads((directory/"manifest.json").read_text())
    if metadata.get("format") != "triad-block-stats-v1":
        raise ValueError("Gaussian bounds require a complete block-statistics workspace")
    names = ("manifest.json", "gram.c128", "mean.c128", "intervals.u64", "valid_rows.u64", "signal_energy.f64")
    optional = tuple(name for name in ("observation.c128", "received_energy.f64") if (directory/name).exists())
    if any((directory/name).is_symlink() for name in (*names, *optional)):
        raise ValueError("Gaussian bounds workspace files must not be symlinks")
    selected = [i for i, mapping in enumerate(metadata["column_mapping"]) if len(mapping) == 2]
    descriptors = [metadata["candidates"][i] for i in selected]
    identities_ = [(row["bit"], row["epoch_offset_seconds"]) for row in descriptors]
    if len(selected) < 2 or {row[0] for row in identities_} != {0, 1}:
        raise ValueError("Gaussian bounds fixed bank must retain both two-tap bit patterns")
    pairs = sum(identities_[i] != identities_[j] for i in range(len(selected)) for j in range(i+1, len(selected)))
    return {"files": {name: file_sha(directory/name) for name in (*names, *optional)},
            "sample_count": metadata["sample_count"], "sample_rate": metadata["sample_rate"],
            "blocks": metadata["block_count"], "candidates": len(selected), "pairs": pairs,
            "selected_candidates": descriptors,
            "fixture_generation_in_timing_target": False}


def identities(candidate_root, reference_root, candidate_binary, reference_binary, bounds_workspace=None):
    sources = {name: source_identity(root, binary) for name, root, binary in (
        ("candidate", candidate_root, candidate_binary), ("reference", reference_root, reference_binary))}
    if sources["candidate"]["driver_sha256"] != sources["reference"]["driver_sha256"]:
        raise ValueError("reference and candidate must use the identical planner driver")
    if sources["candidate"]["driver_sha256"] != file_sha(ROOT/DRIVER):
        raise ValueError("planner driver differs from this harness's workload definition")
    value = {"sources": sources, "environment": environment_identity(),
            "harness": {name: file_sha(Path(__file__).with_name(name)) for name in
                        ("triad_perf_planner.py", "triad_perf_adaptive.py", "triad_perf_native.py",
                         "triad_perf_hardware.py", MANIFEST.name)},
            "manifest_sha256": file_sha(MANIFEST)}
    if bounds_workspace is not None:
        value["bounds"] = {"workspace": workspace_identity(bounds_workspace),
            "sources": {side: {name: file_sha(Path(root)/"tools"/name) for name in BOUNDS_DEPENDENCIES}
                        for side, root in (("candidate", candidate_root), ("reference", reference_root))},
            "libraries": {name: importlib.metadata.version(name) for name in ("torch", "numpy")},
            "python_sha256": file_sha(sys.executable), "threads": 2, "device": "cpu",
            "arithmetic": "complex128", "explicit_environment_controls": BOUNDS_THREAD_ENV,
            "inherited_environment_controls": {name: os.environ.get(name) for name in BOUNDS_INHERITED_ENV}}
    return value


def validate_native(row, case, identity):
    if row.get("schema") != "triad-planner-perf-native-v1" or row.get("complete") is not True:
        raise ValueError("incomplete native planner target")
    for key in ("driver_sha256", "production_sha256"):
        if row.get(key) != identity[key]:
            raise ValueError(f"native planner embedded {key} does not match selected source")
    if row.get("case") != case["id"] or row.get("repetitions") != case["repetitions"]:
        raise ValueError("native planner workload changed")
    if row.get("scenario_evaluations") != case["repetitions"]*case["scenarios_per_repetition"]:
        raise ValueError("native planner scenario count changed")
    if row.get("arithmetic") != "fp64_and_native_long_double_bounds":
        raise ValueError("native planner arithmetic changed")
    if [value.get("name") for value in row.get("outputs", [])] != case["outputs"]:
        raise ValueError("native planner branch output inventory changed")
    for value in [row.get("checksum"), row.get("operation_seconds"),
                  *(item.get("value") for item in row["outputs"])]:
        if isinstance(value, bool) or not isinstance(value, (float, int)) or not math.isfinite(value):
            raise ValueError("native planner returned a non-finite result")
    if row["operation_seconds"] <= 0:
        raise ValueError("native planner operation timer is not positive")
    return True


def numerical_agreement(reference, candidate):
    for key in ("case", "repetitions", "scenario_evaluations", "arithmetic", "internal_iterations_instrumented"):
        if reference[key] != candidate[key]:
            raise ValueError(f"planner work regime differs: {key}")
    if len(reference["outputs"]) != len(candidate["outputs"]):
        raise ValueError("planner output count differs")
    for left, right in zip(reference["outputs"], candidate["outputs"]):
        if any(left[key] != right[key] for key in ("name", "status", "coordinate")):
            raise ValueError("planner status or coordinate differs")
        if not math.isclose(left["value"], right["value"], rel_tol=1e-12, abs_tol=1e-12):
            raise ValueError("planner numerical output differs")
    if not math.isclose(reference["checksum"], candidate["checksum"], rel_tol=1e-12, abs_tol=1e-12):
        raise ValueError("planner checksum differs")


def _run_process(argv, deadline, receipt_path=None, *, env=None):
    remaining = deadline-time.monotonic()
    if remaining <= 0:
        raise TimeoutError("planner suite deadline exhausted")
    started = time.perf_counter()
    try:
        process = subprocess.run(argv, capture_output=True, text=True, timeout=remaining, check=False, env=env)
    except (OSError, subprocess.TimeoutExpired) as error:
        def text(value):
            return value.decode(errors="replace") if isinstance(value, bytes) else value or ""
        receipt = {"argv": argv, "wall_seconds": time.perf_counter()-started, "returncode": None,
                   "stdout": text(getattr(error, "stdout", "")), "stderr": text(getattr(error, "stderr", "")),
                   "error": str(error), "timeout": isinstance(error, subprocess.TimeoutExpired)}
        if receipt_path is not None:
            write_json(receipt_path, receipt)
        raise
    receipt = {"argv": argv, "wall_seconds": time.perf_counter()-started,
               "returncode": process.returncode, "stdout": process.stdout,
               "stderr": process.stderr, "error": None, "timeout": False}
    if receipt_path is not None:
        write_json(receipt_path, receipt)
    if process.returncode:
        raise ValueError(f"planner target exited {process.returncode}: {process.stderr[-1200:]}")
    return receipt


def invoke(binary, case, identity, deadline, receipt_path=None):
    argv = [str(Path(binary).resolve()), "--case", case["id"], "--repetitions", str(case["repetitions"])]
    receipt = _run_process(argv, deadline, receipt_path)
    wall = receipt["wall_seconds"]
    row = json.loads(receipt["stdout"])
    validate_native(row, case, identity)
    if row["operation_seconds"] > wall + .001:
        raise ValueError("planner native timer exceeds external process duration")
    return {"argv": argv, "wall_seconds": wall, "native": row}


def bound_payload(row):
    omitted = {"wall_seconds", "torch_version", "command_argv", "source_manifest_sha256",
               "script_sha256", "gaussian_state_sha256", "workspace_module_sha256"}
    return {key: value for key, value in row.items() if key not in omitted}


def validate_bound(row, case, identity, side):
    workspace = identity["workspace"]
    if row.get("schema") != "triad-gaussian-fixed-bank-bounds-v1":
        raise ValueError("unknown Gaussian design result")
    for report_key, filename in (("script_sha256", "triad_gaussian_bounds.py"),
                                 ("gaussian_state_sha256", "triad_gaussian_state.py"),
                                 ("workspace_module_sha256", "triad_state_workspace.py")):
        if row.get(report_key) != identity["sources"][side][filename]:
            raise ValueError("Gaussian design source identity changed")
    expected = {"source_manifest_sha256": workspace["files"]["manifest.json"],
                "observation_blocks": workspace["blocks"], "candidate_count": workspace["candidates"],
                "pair_designs": workspace["pairs"], "selected_candidates": workspace["selected_candidates"],
                "chernoff_s_grid": case["scales"], "null_calibration": case["null_calibration"],
                "hidden_channel_means_or_signal_energies_read": False,
                "torch_version": identity["libraries"]["torch"]}
    if any(row.get(key) != value for key, value in expected.items()):
        raise ValueError("Gaussian design work, input, arithmetic library or declared path changed")
    if row.get("stationary_coefficient_powers") != [1/case["taps"]]*case["taps"]:
        raise ValueError("Gaussian design tap regime changed")
    if not math.isclose(row["full_exported_capture_seconds"], workspace["sample_count"]/workspace["sample_rate"],
                        rel_tol=1e-12, abs_tol=1e-12):
        raise ValueError("Gaussian design did not process the full calendar")
    if not isinstance(row.get("wall_seconds"), (int, float)) or not math.isfinite(row["wall_seconds"]) or row["wall_seconds"] <= 0:
        raise ValueError("Gaussian design operation timer is invalid")


def bounds_agreement(left, right):
    def same(a, b):
        if isinstance(a, dict) and isinstance(b, dict):
            return a.keys() == b.keys() and all(same(a[key], b[key]) for key in a)
        if isinstance(a, list) and isinstance(b, list):
            return len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
        if type(a) in (float, int) and type(b) in (float, int):
            return math.isfinite(a) and math.isfinite(b) and math.isclose(a, b, rel_tol=1e-10, abs_tol=1e-10)
        return type(a) is type(b) and a == b
    if not same(bound_payload(left), bound_payload(right)):
        raise ValueError("Gaussian design numerical result differs")


def metric_policy(policy):
    # A case passes only if BOTH boundaries rule out the threshold violation:
    # this is an intersection-union test, with no extra upper-tail penalty.
    # Either boundary can declare failure, so split the lower-tail budget.
    return replace(policy, failure_alpha=policy.failure_error_budget/2)


def operation_seconds(observation):
    return (observation["bounds"]["wall_seconds"] if "bounds" in observation
            else observation["native"]["operation_seconds"])


def decision_pair(pairs, policy):
    from triad_perf_adaptive import summarize
    controlled = metric_policy(policy)
    process = summarize([row["reference"]["wall_seconds"] for row in pairs],
                        [row["candidate"]["wall_seconds"] for row in pairs],
                        controlled, model_validation="direct_full")
    operations = summarize([operation_seconds(row["reference"]) for row in pairs],
                           [operation_seconds(row["candidate"]) for row in pairs],
                           controlled, model_validation="direct_full")
    gates = [process["performance_gate"], operations["performance_gate"]]
    gate = ("performance_regression" if "performance_regression" in gates else
            "pass" if gates == ["pass", "pass"] else "inconclusive")
    stop = (gate if gate != "inconclusive" else
            "maximum_samples" if len(pairs) == policy.looks[-1] else "continue")
    return {"full_process": process, "operations": operations, "gate": gate, "stop_reason": stop}


def invoke_bound(root, workspace, case, identity, side, deadline, receipt_path=None):
    argv = [sys.executable, str(Path(root).resolve()/"tools/triad_gaussian_bounds.py"),
            str(Path(workspace).resolve()), *case["args"]]
    receipt = _run_process(argv, deadline, receipt_path, env={**os.environ, **BOUNDS_THREAD_ENV})
    wall = receipt["wall_seconds"]
    row = json.loads(receipt["stdout"])
    validate_bound(row, case, identity, side)
    if row["wall_seconds"] > wall + .001:
        raise ValueError("Gaussian design timer exceeds complete process duration")
    return {"argv": argv, "wall_seconds": wall, "bounds": row}


def measure_case(case, binaries, identity, policy, deadline, output, bound_context=None):
    pairs = []; looks = []; case_dir = Path(output)/case["id"]
    case_dir.mkdir()
    for count in range(1, policy.looks[-1]+1):
        order = ("reference", "candidate") if count % 2 else ("candidate", "reference")
        pair = {"index": count, "order": list(order), "raw_receipts": {}}
        try:
            for name in order:
                raw_path = case_dir/f"pair-{count:03d}-{name}-raw.json"
                pair["raw_receipts"][name] = {"path": raw_path.name}
                try:
                    if bound_context is None:
                        pair[name] = invoke(binaries[name], case, identity["sources"][name], deadline, raw_path)
                    else:
                        pair[name] = invoke_bound(bound_context["roots"][name], bound_context["workspace"],
                                                  case, identity["bounds"], name, deadline, raw_path)
                finally:
                    pair["raw_receipts"][name]["sha256"] = file_sha(raw_path) if raw_path.exists() else None
            if bound_context is None:
                numerical_agreement(pair["reference"]["native"], pair["candidate"]["native"])
            else:
                bounds_agreement(pair["reference"]["bounds"], pair["candidate"]["bounds"])
        except (OSError, ValueError, KeyError, TypeError, OverflowError, TimeoutError, subprocess.TimeoutExpired) as error:
            pair.update(status="failed", error=str(error), numerical_agreement=False)
            write_json(case_dir/f"pair-{count:03d}.json", pair)
            raise
        pair["status"] = "complete"
        pair["numerical_agreement"] = True
        write_json(case_dir/f"pair-{count:03d}.json", pair)
        pairs.append(pair)
        if count not in policy.looks:
            continue
        decisions = decision_pair(pairs, policy)
        looks.append(decisions)
        if decisions["stop_reason"] != "continue":
            break
    result = {"id": case["id"], "workload": case, "status": "direct_full",
              "prediction_valid": True, "complete": True, "source_identity_verified": True,
              "functional_valid": True, "timing_boundary": manifest()[
                  "timing_boundary" if bound_context is None else "bounds_timing_boundary"],
              "full_target_observations_per_source": len(pairs), "pairs": pairs, "looks": looks,
              "adaptive": decisions["full_process"], "compute_adaptive": decisions["operations"],
              "comparison": {"gate": decisions["gate"]}, "sampling_stop_reason": decisions["stop_reason"],
              "metric_policy": json.loads(json.dumps(asdict(metric_policy(policy)))),
              "decision_scope": "Both complete-process and nested operation mean-ratio bounds must pass; upper-tail budgets use an intersection-union test, lower-tail erroneous-declaration budget is split equally across the two boundaries and their declared looks. Timers are nested, never added.",
              "runtime_estimand": "geometric mean complete process runtime; paired log-runtime ratio",
              "abbreviation_model": None,
              "unmeasured_work_scaling_claim": False}
    write_json(case_dir/"result.json", result)
    return result


def validate_routine_report(report, context):
    """Fail closed on changed sources, missing raw work, or non-passing cases."""
    from triad_perf_adaptive import AdaptivePolicy, regression_profile
    errors = []
    try:
        if report.get("schema") != SCHEMA:
            raise ValueError("unknown planner report schema")
        expected = identities(context["candidate_root"], context["reference_root"],
                              context["candidate_binary"], context["reference_binary"], context.get("bounds_workspace"))
        if report.get("identity") != expected:
            raise ValueError("planner source, binary, harness, or environment identity changed")
        profile = context["regression_profile"]
        policy = AdaptivePolicy(stopping_rule="decision", **regression_profile(profile))
        if report.get("policy") != json.loads(json.dumps(asdict(policy))):
            raise ValueError("planner decision policy changed")
        case_rows = manifest()["cases"] + (manifest()["bounds_cases"] if context.get("bounds_workspace") else [])
        if [row.get("id") for row in report.get("cases", [])] != [row["id"] for row in case_rows]:
            raise ValueError("planner full inventory is incomplete")
        if not all(report.get(key) is True for key in
                   ("completed", "source_identity_verified", "functional_valid", "prediction_valid")):
            raise ValueError("planner run is incomplete or invalid")
        path = Path(context["report_path"])
        plan = json.loads((path.parent/"plan.json").read_text())
        if plan != report.get("plan") or plan["identity"] != expected:
            raise ValueError("planner frozen plan differs")
        if plan["cases"] != case_rows or plan["policy"] != report["policy"]:
            raise ValueError("planner plan workload or policy differs")
        for result, case in zip(report["cases"], case_rows):
            pairs = result["pairs"]
            if result["workload"] != case or len(pairs) not in policy.looks:
                raise ValueError("planner work or observation count differs")
            if result.get("status") != "direct_full" or result.get("full_target_observations_per_source") != len(pairs):
                raise ValueError("planner direct-target accounting differs")
            if not all(result.get(key) is True for key in
                       ("complete", "source_identity_verified", "functional_valid", "prediction_valid")):
                raise ValueError("planner case is incomplete or invalid")
            replayed_looks = []
            for index, pair in enumerate(pairs, 1):
                if pair != json.loads((path.parent/case["id"]/f"pair-{index:03d}.json").read_text()):
                    raise ValueError("planner raw pair differs")
                order = ["reference", "candidate"] if index % 2 else ["candidate", "reference"]
                if pair["index"] != index or pair["order"] != order:
                    raise ValueError("planner pair ordering changed")
                if pair.get("status") != "complete":
                    raise ValueError("planner pair is incomplete")
                for side in ("reference", "candidate"):
                    expected_name = f"pair-{index:03d}-{side}-raw.json"
                    raw_path = path.parent/case["id"]/expected_name
                    if pair["raw_receipts"][side] != {"path": expected_name, "sha256": file_sha(raw_path)}:
                        raise ValueError("planner raw invocation identity differs")
                    raw = json.loads(raw_path.read_text())
                    if raw.get("returncode") != 0 or raw.get("timeout") or raw.get("error") is not None:
                        raise ValueError("planner raw invocation did not complete successfully")
                    is_bound = case.get("kind") == "gaussian-design"
                    parsed = pair[side]["bounds" if is_bound else "native"]
                    if (json.loads(raw["stdout"]) != parsed or raw["wall_seconds"] != pair[side]["wall_seconds"]
                            or raw["argv"] != pair[side]["argv"]):
                        raise ValueError("planner parsed observation differs from its raw invocation")
                    if is_bound:
                        validate_bound(pair[side]["bounds"], case, expected["bounds"], side)
                        native_wall = pair[side]["bounds"]["wall_seconds"]
                    else:
                        validate_native(pair[side]["native"], case, expected["sources"][side])
                        native_wall = pair[side]["native"]["operation_seconds"]
                    wall = pair[side]["wall_seconds"]
                    if not math.isfinite(wall) or wall <= 0 or native_wall > wall + .001:
                        raise ValueError("planner external timer is invalid")
                if is_bound:
                    bounds_agreement(pair["reference"]["bounds"], pair["candidate"]["bounds"])
                else:
                    numerical_agreement(pair["reference"]["native"], pair["candidate"]["native"])
                if index in policy.looks:
                    replay = decision_pair(pairs[:index], policy)
                    replayed_looks.append(replay)
                    if replay["stop_reason"] != "continue" and index != len(pairs):
                        raise ValueError("planner collected observations after its stopping decision")
            if (replayed_looks != result["looks"] or replay["full_process"] != result["adaptive"]
                    or replay["operations"] != result["compute_adaptive"] or replay["gate"] != "pass"
                    or result["comparison"] != {"gate": "pass"}
                    or result["metric_policy"] != json.loads(json.dumps(asdict(metric_policy(policy))))):
                raise ValueError("planner replay does not support a pass")
        gaps = [GAP] + ([BOUNDS_GAP] if context.get("bounds_workspace") else [])
        return {"valid": True, "status": "passed", "errors": [], "gap_ids": gaps,
                "outcomes": {"functional": "pass", "timing": "direct_full", "regression": "pass",
                             "scope": "six fixed native planner/statistics bundles and, when selected, two fixed retained-workspace Gaussian-design jobs; no GPU claim"}}
    except (OSError, ValueError, KeyError, TypeError, OverflowError) as exc:
        errors.append(str(exc))
    return {"valid": False, "status": "unqualified", "errors": errors, "gap_ids": [],
            "outcomes": {"regression": "unqualified"}}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-root", type=Path, default=ROOT)
    parser.add_argument("--reference-root", type=Path, required=True)
    parser.add_argument("--candidate-binary", type=Path, required=True)
    parser.add_argument("--reference-binary", type=Path, required=True)
    parser.add_argument("--bounds-workspace", type=Path,
                        help="retained complete block-statistics workspace for the two additional CPU design-bound jobs")
    parser.add_argument("--regression-profile", choices=("normal", "ci", "strict"), default="normal")
    parser.add_argument("--budget-seconds", type=float, default=120.)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--case", action="append", default=[])
    args = parser.parse_args(argv)
    started = time.monotonic()
    from triad_perf_adaptive import AdaptivePolicy, regression_profile
    policy = AdaptivePolicy(stopping_rule="decision", **regression_profile(args.regression_profile))
    args.output.mkdir(parents=True, exist_ok=False)
    report = {"schema": SCHEMA, "regression_profile": args.regression_profile,
              "policy": json.loads(json.dumps(asdict(policy))), "cases": [], "errors": [],
              "completed": False, "source_identity_verified": False,
              "functional_valid": False, "prediction_valid": False, "coverage_gap_ids": []}
    try:
        if not math.isfinite(args.budget_seconds) or args.budget_seconds <= 0:
            raise ValueError("planner budget must be finite and positive")
        rows = manifest()["cases"] + (manifest()["bounds_cases"] if args.bounds_workspace else [])
        if set(args.case)-{row["id"] for row in rows}:
            raise ValueError("unknown planner case selection")
        selected = [row for row in rows if not args.case or row["id"] in args.case]
        identity = identities(args.candidate_root, args.reference_root, args.candidate_binary, args.reference_binary,
                              args.bounds_workspace)
        report["identity"] = identity
        plan = {"schema": SCHEMA, "identity": identity, "cases": selected,
                "policy": report["policy"], "timing_boundary": manifest()["timing_boundary"],
                "all_observations_are_direct_full_targets": True,
                "deadline_seconds": args.budget_seconds}
        report["plan"] = plan
        write_json(args.output/"plan.json", plan)
        binaries = {"candidate": args.candidate_binary, "reference": args.reference_binary}
        for case in selected:
            bound_context = ({"roots": {"candidate": args.candidate_root, "reference": args.reference_root},
                              "workspace": args.bounds_workspace} if case.get("kind") == "gaussian-design" else None)
            report["cases"].append(measure_case(case, binaries, identity, policy,
                                               started+args.budget_seconds, args.output, bound_context))
        if identity != identities(args.candidate_root, args.reference_root, args.candidate_binary, args.reference_binary,
                                   args.bounds_workspace):
            raise ValueError("planner identities changed during measurement")
        report.update(completed=True, source_identity_verified=True, functional_valid=True, prediction_valid=True)
        report["full_inventory"] = len(selected) == len(rows)
        if report["full_inventory"] and all(row["comparison"]["gate"] == "pass" for row in report["cases"]):
            report["coverage_gap_ids"] = [GAP] + ([BOUNDS_GAP] if args.bounds_workspace else [])
    except (OSError, ValueError, KeyError, TypeError, OverflowError, TimeoutError, subprocess.TimeoutExpired) as exc:
        report["errors"].append(str(exc))
    report["wall_seconds"] = time.monotonic()-started
    write_json(args.output/"summary.json", report)
    print(json.dumps({"summary": str(args.output/"summary.json"), "wall_seconds": report["wall_seconds"],
                      "complete": report["completed"], "errors": report["errors"]}))
    return 0 if report["completed"] and all(row["comparison"]["gate"] == "pass" for row in report["cases"]) else 2


if __name__ == "__main__":
    raise SystemExit(main())
