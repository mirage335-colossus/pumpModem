#!/usr/bin/env python3
"""Run the isolated Triad routine checks under one deadline.

This is an evidence aggregator, not a new timing model. It preserves the native
and FFT harness and invokes both timing harnesses in historical-reuse mode.
Qualification remains an explicit, separate operation. Source coverage and
passing functional commands do not qualify a full-workload runtime forecast.
Use --family-route staged with its exact anchor directory to reuse independently
qualified stage forecasts. Repeat --identical-fft-case for qualified FFT cases
whose actual runtime identities permit one timing stream; ineligible identities
stay unresolved and do not trigger an expensive automatic fallback.
Routine timing defaults to --stopping-rule decision: the child harnesses decide
whether the per-test slowdown limit is ruled out. Timing interval width remains
a diagnostic. The normal regression profile permits pass alpha 0.15 at a 1.30
slowdown ratio; CI selects its separate ci profile explicitly. Failure-declaration
alpha remains 0.05. Use precision explicitly for measurement-precision studies;
qualification and its independent validation stay in the child qualification CLIs.

Use --functional-defaults for the four focused native checks and the Python
checks named in the maintained coverage matrix. Alternatively supply repeated
--functional-command '["program", "argument", ...]' options. Commands run without
a shell in --candidate-root and share the total deadline. Explicit commands are
reported as supplied scope; the wrapper does not infer comprehensive coverage.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "triad-performance-complete-routine-v1"
BASELINE = "242ea15fa032ee117b9113a7f3cd07cc6e9f0254"
NATIVE_TESTS = "^triad_(statistics|v3_(source|channel|receiver))$"
NATIVE_NAMES = {"triad_statistics", "triad_v3_source", "triad_v3_channel", "triad_v3_receiver"}
DEVICE_TESTS = {"test_triad_phase_frequency.py", "test_triad_phase_optimized.py",
                "test_triad_phase_reduction.py", "test_triad_phase_tensor.py"}


def digest(path):
    value = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def write_json(path, data):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(data, indent=2, allow_nan=False) + "\n")
    temporary.replace(path)


def read_json(path):
    return json.loads(Path(path).read_text())


def relative_file(root, name):
    path = (root / name).resolve()
    if not path.is_relative_to(root) or not path.is_file():
        raise ValueError(f"declared source is absent or outside source root: {name}")
    return path


def source_identity(root, matrix, families):
    """Bind functional scope, including headers and tests, before execution."""
    names = set()
    for row in matrix["rows"]:
        names.update(row["source_files"])
        names.update(test.split("::")[0] for test in row["cheap_functional_tests"]
                     if test.startswith(("tests/", "tools/")))
    for case in families["cases"]:
        names.update("tools/" + name for name in case["dependencies"])
    names.add("CMakeLists.txt")
    names.update(str(path.relative_to(root)) for path in (root/"cmake").rglob("*.cmake"))
    return {name: digest(relative_file(root, name)) for name in sorted(names)}


def check_coverage(path, root, expected_sources, required_dependencies=()):
    """Accept counters only for the exact recorded source bytes; never relabel fresh."""
    data = read_json(path)
    schema = data.get("schema")
    if schema not in ("datapump.triad.native-gcov-coverage", "datapump.triad.python-branch-coverage"):
        raise ValueError(f"unsupported coverage receipt schema: {schema}")
    sources = data.get("sources", {})
    if not isinstance(sources, dict) or not sources:
        raise ValueError("coverage receipt has no source rows")
    mismatched = []
    for name, row in sources.items():
        if row.get("sha256") != digest(relative_file(root, name)):
            mismatched.append(name)
    expected = set(expected_sources)
    # Older collectors bind instrumented source rows, but not included headers,
    # imported modules or build inputs. Keep those useful counters without
    # promoting them to dependency-complete historical applicability.
    dependencies = data.get("dependency_sha256", {})
    required = set(required_dependencies)-sources.keys()
    missing_dependencies = sorted(required-dependencies.keys())
    changed_dependencies = [name for name, value in dependencies.items()
                            if value != digest(relative_file(root, name))]
    closure = not missing_dependencies and not changed_dependencies and bool(dependencies)
    counter_issues = []
    unverified_counters = []
    for category in ("sources", "headers"):
        for name, row in data.get(category, {}).items():
            for metric in ("lines", "gcov_branches"):
                counters = row.get(metric, {})
                state = counters.get("coverage_status")
                if (str(state).startswith("unqualified") or counters.get("invalid_negative_counts") or
                        counters.get("invalid_negative_outcomes")):
                    counter_issues.append({"category": category, "source": name, "metric": metric, "counters": counters})
                elif schema == "datapump.triad.native-gcov-coverage" and state != "counted":
                    unverified_counters.append(f"{category}:{name}:{metric}")
    quality = "invalid" if counter_issues else "unverified" if unverified_counters else "valid"
    return {"path": str(Path(path).resolve()), "sha256": digest(path), "schema": schema,
            "status": "source_mismatch" if mismatched else "source_rows_matched_historical",
            "mismatched_sources": mismatched, "missing_expected_sources": sorted(expected-sources.keys()),
            "declared_dependency_status": "matched" if closure else "unverified",
            "missing_dependency_identities": missing_dependencies,
            "changed_dependencies": changed_dependencies,
            "historical_applicability": "declared_source_and_dependency_match" if closure and not mismatched and quality == "valid" else "unqualified",
            "counter_quality": {"status": quality, "invalid": counter_issues, "unverified": unverified_counters},
            "sources": sources, "original_test_exit_code": data.get("test_exit_code"),
            "reconciliation": data.get("reconciliation"),
            "scope": "retained instrumented source counters, not a fresh run or semantic coverage proof"}


def coverage_dependencies(root, matrix, expected_sources, kind):
    """Bind mapped test code as well as runtime inputs, without binding prose."""
    expected = set(expected_sources)
    rows = [row for row in matrix["rows"] if set(row["source_files"]) & expected]
    dependencies = {name for row in rows for name in row["source_files"]}
    for row in rows:
        for test in row["cheap_functional_tests"]:
            name = test.split("::", 1)[0]
            if name.startswith(("tests/", "tools/")):
                relative_file(root, name)
                dependencies.add(name)
    if kind == "native":
        dependencies.update(["CMakeLists.txt", *[str(p.relative_to(root)) for p in (root/"cmake").rglob("*.cmake")]])
    return sorted(dependencies)


def functional_defaults(root, binary, matrix, device="cpu"):
    tests = sorted({test.split("::")[0] for row in matrix["rows"]
                    for test in row["cheap_functional_tests"] if test.split("::")[0].endswith(".py")})
    commands = [["ctest", "--test-dir", str(binary.parent), "--output-on-failure", "--no-tests=error", "-R", NATIVE_TESTS, "-j", "1"]]
    for test in tests:
        command = [sys.executable, "-B", str(relative_file(root, test))]
        if any(item.startswith(test+"::") for row in matrix["rows"] for item in row["cheap_functional_tests"]):
            command.append("-v")
        if Path(test).name in DEVICE_TESTS:
            command += ["--device", "cuda:0" if device == "cuda" else "cpu"]
        commands.append(command)
    return commands


def command_output(receipt):
    return "\n".join(Path(receipt[key]).read_text(errors="replace") for key in ("stdout", "stderr") if receipt.get(key))


def native_inventory(receipt):
    if receipt["status"] != "passed":
        return {"status": "incomplete", "required": sorted(NATIVE_NAMES)}
    data = json.loads(Path(receipt["stdout"]).read_text())
    rows = data.get("tests", [])
    names = [row["name"] for row in rows]
    disabled = [row["name"] for row in rows if any(p.get("name") == "DISABLED" and p.get("value")
                for p in row.get("properties", []))]
    return {"status": "passed" if set(names) == NATIVE_NAMES and len(names) == 4 and not disabled else "incomplete",
            "required": sorted(NATIVE_NAMES), "present": names, "disabled": disabled}


def functional_evidence(command, receipt, device, required_methods=()):
    """An exit code is insufficient for all-skipped tests or an unexecuted backend."""
    evidence = {"status": "incomplete", "selected_backend": device, "command_exit_status": receipt["status"]}
    if receipt["status"] != "passed":
        evidence["status"] = "failed" if receipt["status"] == "failed" else "incomplete"
        return evidence
    output = command_output(receipt)
    if Path(command[0]).name == "ctest":
        passed = re.findall(r"Test\s+#\d+:\s+(\S+)\s+\.+\s+Passed", output)
        return {**evidence, "status": "passed" if set(passed) == NATIVE_NAMES and len(passed) == 4 else "incomplete",
                "executed_native_tests": passed, "backend_scope": "native CPU", "required_native_tests": sorted(NATIVE_NAMES)}
    match = re.search(r"Ran\s+(\d+)\s+tests?\s+in\s+", output)
    if match:
        total = int(match.group(1))
        skip_match = re.search(r"skipped=(\d+)", output)
        skipped = int(skip_match.group(1)) if skip_match else 0
        executed_methods = sorted(set(re.findall(r"(?m)^([A-Za-z_]\w*)\s+\([^\n]+\)\s+\.\.\.\s+ok\s*$", output)))
        missing_methods = sorted(set(required_methods)-set(executed_methods))
        return {**evidence, "status": "passed" if total > 0 and not skipped and not missing_methods else "incomplete",
                "reported_tests": total, "skipped": skipped, "executed_tests": max(0, total-skipped),
                "required_named_methods": list(required_methods), "executed_named_methods": executed_methods,
                "missing_named_methods": missing_methods,
                "backend_scope": "test-defined support checks; unittest counts do not establish CUDA execution"}
    if required_methods:
        return {**evidence, "required_named_methods": list(required_methods),
                "missing_named_methods": list(required_methods), "reason": "no verbose execution receipt for declared Python methods"}
    try:
        # CLI checks emit their structured receipt on stdout; harmless library
        # diagnostics on stderr are retained, not appended to the JSON document.
        data = json.loads(Path(receipt["stdout"]).read_text().strip() if receipt.get("stdout") else output.strip())
    except ValueError:
        return {**evidence, "reason": "no structured executed-check evidence"}
    if not isinstance(data, dict):
        return evidence
    test = next((Path(item).name for item in command if item.endswith(".py")), "")
    if test in DEVICE_TESTS:
        if test in {"test_triad_phase_reduction.py", "test_triad_phase_tensor.py"}:
            required = ("cpu", "cuda") if device == "cuda" else ("cpu",)
            passed = all(data.get(key, {}).get("status") == "passed" for key in required)
            scopes = {key: data.get(key, {}).get("status", "missing") for key in ("cpu", "cuda")}
        else:
            observed = str(data.get("device", "")).split(":")[0]
            passed = observed == device and (data.get("status") == "passed" or isinstance(data.get("passed"), int) and data["passed"] > 0)
            scopes = {observed or "missing": data.get("status", "passed" if data.get("passed", 0) else "missing")}
            for key in ("cuda_check", "cuda_graph"):
                if isinstance(data.get(key), dict):
                    scopes[key] = data[key].get("status", "missing")
                    if device == "cuda" and scopes[key] != "passed":
                        passed = False
        return {**evidence, "status": "passed" if passed else "incomplete", "executed_scope_receipt": scopes,
                "backend_scope": "explicit device-selected CLI checks", "reported_checks": data.get("passed", data.get("checks"))}
    checks = data.get("checks", data.get("passed"))
    count = checks if isinstance(checks, int) else len(checks) if isinstance(checks, (dict, list)) else 0
    return {**evidence, "status": "passed" if count > 0 and data.get("status", "passed") == "passed" else "incomplete",
            "reported_checks": count, "backend_scope": "test-defined support checks; no CUDA execution inferred"}


def inventory_gaps(matrix):
    result = []
    for row in matrix["rows"] + matrix.get("unrepresented_expensive_boundaries", []):
        name = row.get("row_id", row.get("id"))
        if "required_gaps" in row:
            gaps = row["required_gaps"]
            disposition = "explicit_required_gaps"
        else:
            # Older inventories did not distinguish limitations from required
            # work. Keep their conservative interpretation until reviewed.
            gaps = row.get("unresolved", [row["scope"]] if "scope" in row else None)
            disposition = "legacy_fail_closed"
        if not isinstance(gaps, list) or not all(isinstance(item, str) and item.strip() for item in gaps):
            raise ValueError(f"inventory {name!r} requires an explicit string-list gap disposition")
        if gaps:
            entry = {"id": name, "unresolved": gaps, "disposition": disposition}
            if "required_extension" in row:
                mapping = row["required_extension"]
                if (not isinstance(mapping, dict) or not isinstance(mapping.get("id"), str) or not mapping["id"] or
                        not isinstance(mapping.get("gap_ids"), list) or not mapping["gap_ids"] or
                        any(not isinstance(item, str) or not item for item in mapping["gap_ids"]) or
                        set(mapping["gap_ids"]) != {name}):
                    raise ValueError(f"inventory {name!r} has an invalid extension mapping")
                entry["required_extension"] = mapping
            result.append(entry)
    return result


def inventory_limitations(matrix):
    """Retain reviewed scope limits without converting them into missing tests."""
    return [{"id": row.get("row_id", row.get("id")), "limitations": row["limitations"],
             "legacy_prose": row.get("unresolved", row.get("scope"))}
            for row in matrix["rows"] + matrix.get("unrepresented_expensive_boundaries", [])
            if row.get("limitations")]


def extension_passed(receipt):
    validation = receipt.get("validation", {})
    command = receipt.get("command", {})
    return (command.get("status") == "passed" and command.get("exit_code") == 0 and
            validation.get("valid") is True and validation.get("status") == "passed" and not validation.get("errors"))


def cold_receipt_qualified(receipt):
    """A reconstructed cold proof is independent of a mixed-campaign verdict."""
    validation, command = receipt.get("validation", {}), receipt.get("command", {})
    cold = validation.get("cold_validation")
    if cold is None:
        return extension_passed(receipt) and validation.get("cold_latency_qualified") is True
    return (command.get("status") in ("passed", "failed") and command.get("exit_code") in (0, 2) and
            isinstance(cold, dict) and cold.get("valid") is True and cold.get("status") == "passed" and
            bool(cold.get("case_ids")) and not cold.get("errors") and validation.get("cold_latency_qualified") is True)


def resolve_extension_gaps(gaps, receipts):
    """Only the maintained row mapping authorizes a validated extension closure."""
    remaining, closed = [], []
    for gap in gaps:
        mapping = gap.get("required_extension", {})
        receipt = receipts.get(mapping.get("id"), {})
        validated_ids = receipt.get("validation", {}).get("gap_ids", [])
        if (mapping and extension_passed(receipt) and gap["id"] in mapping["gap_ids"] and
                all(name in validated_ids for name in mapping["gap_ids"])):
            closed.append({"id": gap["id"], "extension": mapping["id"], "report_sha256": receipt.get("report_sha256")})
        else:
            remaining.append(gap)
    return remaining, closed


def extension_specs(args, candidate, reference, binary, reference_binary, output):
    """Fixed maintained CLIs only; supplying inputs never authorizes qualification."""
    common = {"candidate_root": str(candidate), "reference_root": str(reference), "binary": str(binary),
              "reference_binary": str(reference_binary), "device": args.device, "precision": args.precision,
              "threads": args.threads, "native_threads": args.native_threads,
              "regression_profile": args.regression_profile}
    specs = []
    if args.lifecycle_anchor_directory or args.lifecycle_mixed_method == "direct":
        anchor_directory = (args.lifecycle_anchor_directory or output/"lifecycle-unused-anchors").resolve()
        specs.append({"id": "lifecycle", "module": "triad_perf_lifecycle", "context": {
            **common, "anchor_directory": str(anchor_directory)}, "arguments": [
            "--candidate-root", str(candidate), "--reference-root", str(reference), "--binary", str(binary),
            "--reference-binary", str(reference_binary), "--device", args.device, "--precision", args.precision,
            "--threads", str(args.threads), "--native-threads", str(args.native_threads), "--mode", "reuse",
            "--anchor-directory", str(anchor_directory)]})
        # Preserve the modeled backend argv/context exactly; explicit CPU direct
        # measurement activates independently of any historical forecast.
        if args.lifecycle_mixed_method == "direct":
            specs[-1]["arguments"] += ["--mixed-method", "direct", "--direct-cycles", str(args.lifecycle_direct_cycles)]
            specs[-1]["context"]["mixed_method"] = "direct"
            specs[-1]["context"]["direct_cycles"] = args.lifecycle_direct_cycles
    if args.resource_anchor or args.resource_direct:
        mode = "direct" if args.resource_direct else "routine"
        specs.append({"id": "resource", "module": "triad_perf_resource", "context": {
            **common, "threads": args.native_threads, "mode": mode}, "arguments": [
            "--binary", str(binary), "--reference-binary", str(reference_binary), "--mode", mode,
            "--source-root", str(candidate), "--reference-root", str(reference),
            "--threads", str(args.native_threads)]})
        if args.resource_anchor:
            anchor = str(args.resource_anchor.resolve())
            specs[-1]["context"]["anchor"] = anchor
            specs[-1]["arguments"] += ["--anchor", anchor]
    if args.planner_binary:
        planner = args.planner_binary.resolve(strict=True)
        planner_reference = args.planner_reference_binary.resolve(strict=True)
        specs.append({"id": "planner", "module": "triad_perf_planner", "context": {
            **common, "candidate_binary": str(planner), "reference_binary": str(planner_reference),
            "threads": args.native_threads}, "arguments": [
            "--candidate-root", str(candidate), "--reference-root", str(reference),
            "--candidate-binary", str(planner), "--reference-binary", str(planner_reference)]})
        if args.planner_bounds_workspace:
            workspace = args.planner_bounds_workspace.resolve(strict=True)
            specs[-1]["context"]["bounds_workspace"] = str(workspace)
            specs[-1]["arguments"] += ["--bounds-workspace", str(workspace)]
    for spec in specs:
        spec["driver"] = ROOT/"tools"/(spec["module"]+".py")
        spec["output"] = output/"extensions"/spec["id"]
        spec["context"]["report_path"] = str(spec["output"]/"summary.json")
    return specs


def validate_extension_report(spec):
    path = Path(spec["context"]["report_path"])
    if not path.is_file():
        return {"valid": False, "status": "unqualified", "errors": ["extension report was not produced"], "gap_ids": []}
    validator = getattr(importlib.import_module(spec["module"]), "validate_routine_report")
    value = validator(read_json(path), spec["context"])
    if (not isinstance(value, dict) or not isinstance(value.get("valid"), bool) or
            value.get("status") not in ("passed", "failed", "inconclusive", "unqualified", "not_applicable") or
            not isinstance(value.get("errors"), list) or not all(isinstance(item, str) for item in value["errors"]) or
            not isinstance(value.get("gap_ids"), list) or not all(isinstance(item, str) for item in value["gap_ids"])):
        raise ValueError(f"{spec['id']} validator returned an invalid receipt")
    if not isinstance(value.get("review_required", []), list):
        raise ValueError(f"{spec['id']} validator review requirements must be a list")
    for review in value.get("review_required", []):
        if (not isinstance(review, dict) or not isinstance(review.get("id"), str) or not review["id"] or
                not isinstance(review.get("case_ids"), list) or not all(isinstance(name, str) for name in review["case_ids"]) or
                not isinstance(review.get("basis"), str)):
            raise ValueError(f"{spec['id']} validator returned an invalid review requirement")
    cold = value.get("cold_validation")
    if cold is not None and (not isinstance(cold, dict) or not isinstance(cold.get("valid"), bool) or
            cold.get("status") not in ("passed", "failed", "inconclusive", "unqualified") or
            not isinstance(cold.get("case_ids"), list) or not all(isinstance(name, str) for name in cold["case_ids"]) or
            not isinstance(cold.get("errors"), list) or not all(isinstance(error, str) for error in cold["errors"])):
        raise ValueError(f"{spec['id']} validator returned an invalid independent cold receipt")
    return value


def connect_fixture_cache(output, cache):
    """Connect the frozen legacy driver's fixed cache location to retained data."""
    if cache is None:
        return {"status": "per_run_cache", "reason": "no retained --fixture-cache was supplied"}
    cache = cache.resolve()
    if cache == output or cache.is_relative_to(output) or output.is_relative_to(cache):
        raise ValueError("fixture cache and output must be separate non-nested directories")
    cache.mkdir(parents=True, exist_ok=True)
    cache = cache.resolve(strict=True)
    alias = output/"triad-performance-fixtures"
    alias.symlink_to(cache, target_is_directory=True)
    if not alias.is_dir() or alias.resolve(strict=True) != cache:
        raise ValueError("retained fixture-cache link could not be verified")
    return {"status": "retained_link_verified", "target": str(cache), "legacy_alias": str(alias),
            "validity": "the existing fixture helper still validates source/binary/configuration and individual file hashes"}


def child_environment(threads):
    env = os.environ.copy()
    for key in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS", "NUMEXPR_NUM_THREADS", "VECLIB_MAXIMUM_THREADS"):
        env[key] = str(threads)
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    return env


def _stop_group(process):
    # Both receiver harnesses are entered without their detached outer
    # supervisor. Their workers and native exports remain in this group.
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        process.wait()
        return
    try:
        process.wait(timeout=2.)
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait(timeout=5.)


def execute(command, *, cwd, directory, deadline, env):
    """Reap a child group on success, failure or deadline; retain both logs."""
    start = time.perf_counter()
    remaining = deadline-time.monotonic()
    result = {"argv": list(command), "cwd": str(cwd), "status": "not_started_budget_exhausted",
              "exit_code": None, "wall_seconds": 0., "descendant_cleanup": "not_started"}
    if remaining <= 0:
        return result
    directory.mkdir(parents=True, exist_ok=False)
    with (directory/"stdout.log").open("wb") as out, (directory/"stderr.log").open("wb") as err:
        process = subprocess.Popen(command, cwd=cwd, env=env, stdout=out, stderr=err, start_new_session=True)
        try:
            result["exit_code"] = process.wait(timeout=remaining)
            result["status"] = "passed" if result["exit_code"] == 0 else "failed"
        except subprocess.TimeoutExpired:
            result["status"] = "deadline_exceeded"
        finally:
            _stop_group(process)
            result["descendant_cleanup"] = "owned_process_group_terminated_and_leader_reaped"
    result["wall_seconds"] = time.perf_counter()-start
    result["stdout"] = str(directory/"stdout.log")
    result["stderr"] = str(directory/"stderr.log")
    return result


def make_route_plan(native_config, fft_config, identical_cases):
    native = list(native_config["profiles"]["ci"])
    fft = list(fft_config["profiles"]["ci"])
    full = native+fft+["gaussian-multiblock"]
    if len(full) != len(set(full)):
        raise ValueError("CI workload manifests contain duplicate case IDs")
    if len(identical_cases) != len(set(identical_cases)) or set(identical_cases)-set(fft):
        raise ValueError("identical FFT selection contains duplicate or non-CI FFT cases")
    selected = [name for name in fft if name in identical_cases]
    return {"full_legacy_profile_ids": full,
            "legacy_case_ids": [name for name in full if name not in selected],
            "identical_fft_case_ids": selected}


def timing_outcomes(legacy, sampled, expected_sampled, *, identical=None, route_plan=None):
    """Keep observed gates distinct from case completeness and model validity."""
    rows = []
    for row in (legacy or {}).get("cases", []):
        adaptive = row.get("adaptive", row.get("prediction", {}))
        rows.append({"id": row["id"], "harness": "native_fft", "gate": row.get("comparison", {}).get("gate"),
                     "comparison": row.get("comparison", {}),
                     "prediction_valid": row.get("prediction_valid", False),
                     "timing_precision_reached": adaptive.get("timing_precision_reached", row.get("timing_precision_reached", False)),
                     "endpoint_blocking": row.get("endpoint_calibration_guard", {}).get("blocking", False)})
    for name, row in sampled.items():
        rows.append({"id": name, "harness": "sampled_family", "gate": row.get("comparison", {}).get("gate"),
                     "comparison": row.get("comparison", {}),
                     "prediction_valid": row.get("id") == name and row.get("prediction_valid", False),
                     "timing_precision_reached": row.get("adaptive", {}).get("timing_precision_reached", row.get("timing_precision_reached", False)),
                     "endpoint_blocking": row.get("status") in ("work_regime_changed", "invalid") or row.get("comparison", {}).get("gate") == "severe_endpoint_prediction_mismatch",
                     "measurement_schema": row.get("schema"), "timing_scope": row.get("timing_scope"),
                     "runtime_method": "direct_full_measurement" if row.get("status") == "direct_full" or row.get("selected_measurement") == "direct" else "forecast; validity reported separately"})
    identical = identical or {}
    for name, row in identical.items():
        eligible = row.get("identity_agreement", {}).get("status") == "implementation_unchanged" and row.get("numeric_agreement", {}).get("eligible", False)
        rows.append({"id": name, "harness": "identical_fft", "gate": row.get("comparison", {}).get("gate"),
                     "comparison": row.get("comparison", {}),
                     "prediction_valid": row.get("id") == name and eligible and row.get("prediction_valid", False),
                     "timing_precision_reached": row.get("adaptive", {}).get("timing_precision_reached", False),
                     "endpoint_blocking": False, "identity_deduplication_eligible": eligible,
                     "implementation_comparison": row.get("implementation_comparison"),
                     "timing_scope": row.get("timing_scope"), "excluded_costs": row.get("excluded_costs", [])})
    legacy_ids = [r["id"] for r in rows if r["harness"] == "native_fft"]
    legacy_expected = (legacy or {}).get("full_profile_case_ids", [])
    legacy_complete = bool(legacy and legacy.get("complete_required_coverage") and legacy_expected and
                           len(legacy_ids) == len(set(legacy_ids)) and sorted(legacy_ids) == sorted(legacy_expected))
    if route_plan:
        legacy_complete = legacy_complete and sorted(legacy_expected) == sorted(route_plan["full_legacy_profile_ids"])
    expected_identical = []
    if route_plan and route_plan["identical_fft_case_ids"]:
        # The unchanged legacy tool deliberately labels --case runs focused.
        # Only this frozen manifest partition can close that focused gap; no
        # generic selected subset is promoted to complete profile coverage.
        expected_identical = route_plan["identical_fft_case_ids"]
        legacy_complete = bool(legacy and len(legacy_ids) == len(set(legacy_ids)) and
            sorted(legacy_expected) == sorted(route_plan["full_legacy_profile_ids"]) and
            sorted(legacy_ids) == sorted(route_plan["legacy_case_ids"]) and
            sorted(legacy.get("selected_case_ids", [])) == sorted(route_plan["legacy_case_ids"]))
    identical_complete = set(identical) == set(expected_identical)
    sampled_complete = set(sampled) == set(expected_sampled)
    complete = legacy_complete and identical_complete and sampled_complete and bool(rows)
    regressions = [r["id"] for r in rows if r["gate"] == "performance_regression"]
    unknown_gates = [r["id"] for r in rows if r["gate"] != "pass" and r["id"] not in regressions]
    invalid = [r["id"] for r in rows if not r["prediction_valid"] or r["endpoint_blocking"]]
    imprecise = [r["id"] for r in rows if not r["timing_precision_reached"]]
    return {"rows": rows, "expected_sampled_cases": list(expected_sampled),
            "expected_identical_fft_cases": expected_identical,
            "missing_identical_fft_cases": sorted(set(expected_identical)-identical.keys()),
            "unexpected_identical_fft_cases": sorted(identical.keys()-set(expected_identical)),
            "missing_sampled_cases": sorted(set(expected_sampled)-sampled.keys()),
            "unexpected_sampled_cases": sorted(sampled.keys()-set(expected_sampled)),
            "all_declared_cases_measured": complete,
            "timing_precision": {"status": "passed" if complete and not imprecise else "inconclusive",
                                 "precision_target_met": complete and not imprecise, "unresolved": imprecise},
            "prediction_accuracy": {"status": "passed" if complete and not invalid else "unqualified", "unresolved": invalid,
                                    "sampled_direct_case_ids": [row["id"] for row in rows if row.get("runtime_method") == "direct_full_measurement"],
                                    "direct_scope": "Direct full measurements need no extrapolation model; they do not qualify a failed or absent model."},
            "performance_regression": {"status": "failed" if regressions else "passed" if complete and not unknown_gates else "inconclusive",
                                       "regressions": regressions, "unresolved": unknown_gates}}


def method_qualification(timing, *, functional_complete, coverage_complete, cold_complete,
                         remaining_gaps=(), extension_unresolved=(), errors=()):
    """Assess the selected timing methods without certifying implementation speed.

    All inputs are already replayed by their owning validators. A valid direct
    complete measurement needs no extrapolation proof. A selected forecast still
    needs its independently checked full-target proof. Performance thresholds,
    timing-width goals and hardware/trend screens are separate outcomes.
    """
    blockers = []
    if not timing.get("all_declared_cases_measured"):
        blockers.append("the declared complete-target inventory was not measured")
    if timing.get("prediction_accuracy", {}).get("status") != "passed":
        blockers.append("one or more selected complete-target timing methods are unqualified")
    if not functional_complete:
        blockers.append("mapped functional and numerical checks are incomplete")
    if not coverage_complete:
        blockers.append("source/dependency-matched instrumentation is missing or stale")
    if not cold_complete:
        blockers.append("selected cold-start timing evidence is unqualified")
    if remaining_gaps:
        blockers.append("the maintained inventory has unresolved computational boundaries")
    if extension_unresolved:
        blockers.append("one or more selected extension methods are unqualified")
    if errors:
        blockers.append("execution or integrity validation recorded errors")
    return {"schema": "triad-selected-runtime-method-qualification-v1",
            "status": "qualified" if not blockers else "incomplete",
            "complete": not blockers, "blockers": blockers,
            "case_ids": [row["id"] for row in timing.get("rows", [])],
            "performance_acceptability": timing.get("performance_regression", {}).get("status", "unavailable"),
            "performance_acceptability_required_for_method_qualification": False,
            "timing_precision_required_for_method_qualification": False,
            "hardware_health_or_trend_screen_required_for_method_qualification": False,
            "future_prediction_coverage_qualified": False,
            "scope": "selected source-bound finite targets and their declared timing boundaries; not release certification, universal scaling or a hardware-health certificate"}


def summarize(report, legacy, sampled, expected_sampled, identical=None):
    timing = timing_outcomes(legacy, sampled, expected_sampled, identical=identical, route_plan=report.get("route_plan"))
    functional = report["functional_commands"]
    fresh = ("failed" if any(r["status"] == "failed" or r.get("functional_evidence", {}).get("status") == "failed" for r in functional)
             else "passed" if functional and all(r.get("functional_evidence", {}).get("status") == "passed" for r in functional)
             else "incomplete")
    required_commands = report.get("required_default_functional_commands", [])
    inventory = report.get("native_test_inventory", {})
    functional_scope_complete = bool(report.get("functional_defaults_requested") and required_commands and
        inventory.get("status") == "passed" and set(inventory.get("present", [])) == NATIVE_NAMES and
        len(functional) >= len(required_commands) and all(
            row.get("argv") == command and row.get("functional_evidence", {}).get("status") == "passed"
            for row, command in zip(functional, required_commands)))
    coverage = report["coverage_receipts"]
    coverage_complete = len(coverage) == 2 and all(r["status"] == "source_rows_matched_historical" and not r["missing_expected_sources"]
                                                and r.get("declared_dependency_status") == "matched"
                                                and r.get("counter_quality", {}).get("status") == "valid" for r in coverage)
    hardware = (legacy or {}).get("hardware_health", {"status": "unavailable"})
    hardware_warning = bool(hardware.get("warning"))
    compared = any("time_equivalent_ratio" in row or row.get("regression_screen") for row in hardware.get("rows", []))
    hardware_status = "degraded_screen" if hardware_warning else "screen_observed" if hardware.get("status") == "raw_hardware_screen" and compared else "unqualified"
    outcomes = {"functional_correctness": {"status": fresh, "scope": report["functional_scope"],
                                           "required_scope_complete": functional_scope_complete},
                "code_path_coverage": {"status": "declared_source_and_dependency_matched_historical" if coverage_complete else "incomplete_stale_or_dependency_unverified",
                                       "semantic_completeness": "see the finite maintained inventory; counters alone do not establish it", "fresh": False},
                **{key: timing[key] for key in ("timing_precision", "prediction_accuracy", "performance_regression")},
                "hardware_health": {"status": hardware_status, "receipt": hardware}}
    warm = timing["all_declared_cases_measured"] and all(timing[k]["status"] == "passed" for k in
            ("prediction_accuracy", "performance_regression"))
    errors = report.get("errors", [])
    extensions = report.get("extensions", {})
    extension_failures = [name for name, receipt in extensions.items() if receipt.get("validation", {}).get("status") == "failed"]
    extension_unresolved = sorted(set(report.get("configured_extensions", []))-extensions.keys() |
                                  {name for name, receipt in extensions.items() if not extension_passed(receipt)})
    failure = fresh == "failed" or timing["performance_regression"]["status"] == "failed" or bool(errors) or bool(extension_failures)
    # The currently emitted cold status is unqualified. Consult the actual
    # finite inventory and harness status; do not require universal 100% arcs.
    cold = report.get("sampled_summary", {}).get("cold_latency_qualification", "not_requested_unqualified")
    lifecycle = extensions.get("lifecycle", {})
    if cold_receipt_qualified(lifecycle):
        cold = "qualified"
    gaps, closed_gaps = resolve_extension_gaps(report.get("inventory_unresolved", []), extensions)
    reviews = [{"extension": name, **review} for name, receipt in extensions.items()
               for review in receipt.get("validation", {}).get("review_required", [])]
    workload_scope_complete = (cold == "qualified" and not gaps and warm and fresh == "passed" and
                               functional_scope_complete and coverage_complete and not extension_unresolved and not errors)
    blockers = []
    if cold != "qualified":
        blockers.append(f"cold startup/loading/first-use prediction: {cold}")
    if gaps:
        blockers.append(f"maintained inventory declares {len(gaps)} unresolved rows or computational boundaries")
    if not warm:
        blockers.append("one or more declared warm full-workload timing decisions are unresolved")
    if fresh != "passed":
        blockers.append("fresh functional command set did not pass")
    if not functional_scope_complete:
        blockers.append("complete mapped default functional scope was not executed; custom commands remain diagnostic")
    if not coverage_complete:
        blockers.append("required source-matched native/Python coverage receipts are missing or stale")
    if hardware_status != "screen_observed":
        blockers.append("hardware health comparison is unavailable or degraded")
    if extension_unresolved:
        blockers.append("requested extension decisions are unresolved: " + ", ".join(extension_unresolved))
    for review in reviews:
        blockers.append(f"extension review required: {review['extension']}/{review['id']} ({', '.join(review['case_ids'])}): {review['basis']}")
    complete = not blockers and not errors
    profile = report.get("regression_profile", {})
    decision_scope = ""
    if report.get("requested_stopping_rule") == "decision" and profile:
        decision_scope = (
            f"Decision profile {profile['name']} requests per-test one-sided pass alpha={profile['pass_alpha']:g} "
            f"at full-workload slowdown ratio {profile['slowdown_threshold']:g}, with failure-declaration alpha={profile['failure_alpha']:g}; "
            "child harnesses handle repeated looks. These error budgets are not runtime-width targets or a whole-suite confidence guarantee. ")
    selected_methods = method_qualification(timing, functional_complete=functional_scope_complete and fresh == "passed",
        coverage_complete=coverage_complete, cold_complete=cold == "qualified", remaining_gaps=gaps,
        extension_unresolved=extension_unresolved, errors=errors)
    return {"outcomes": outcomes, "timing_cases": timing,
            "selected_method_qualification": selected_methods,
            "warm_target_decisions_complete": warm,
            "precision_target_met": timing["timing_precision"]["precision_target_met"],
            "extension_outcomes": {name: receipt.get("validation", {}) for name, receipt in extensions.items()},
            "maintained_workload_scope_complete": workload_scope_complete,
            "release_review_requirements": reviews,
            "remaining_inventory_gaps": gaps, "validated_extension_gap_closures": closed_gaps,
            "cold_latency_qualification": cold,
            "warm_decision_statistical_scope": decision_scope +
                "Existing per-case prediction-validity and performance gates determine warm decisions; timing-precision goals are diagnostic and reported separately. Comparison intervals are retained per case. A conditional model's 20% uncertainty cap is not a universal comparison-ratio interval-width cap. No new threshold, pooled confidence level or future-coverage guarantee is introduced.",
            "complete_scope_qualification": complete, "status": "failed" if failure else "passed" if complete else "incomplete",
            "exit_code": 0 if complete else 2, "scope_blockers": blockers,
            "scope_note": "This wrapper aggregates current finite warm targets. It cannot certify unrepresented regimes or qualify cold latency by addition."}


def _method_digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()


def _method_require(condition, message):
    if not condition:
        raise ValueError(message)


def _method_seconds(value, label):
    _method_require(not isinstance(value, bool) and isinstance(value, (int, float)) and
                    math.isfinite(value) and value > 0, label + " is not a positive complete-target time")
    return float(value)


def _method_seal(value):
    return {**value, "record_sha256": _method_digest(value)}


def _method_verify_seal(value):
    body = dict(value)
    claimed = body.pop("record_sha256", None)
    _method_require(claimed == _method_digest(body), "method evidence content identity differs")
    return body


def _method_guard(path):
    """Replay the guard's decision proof and every saved raw measurement."""
    import triad_perf_guard_policy as policy
    path = Path(path).resolve(strict=True)
    value = read_json(path)
    _method_require(value.get("schema") in ("triad-performance-fast-guard-run-v1", "triad-performance-fast-guard-run-v2"), "unsupported guard report")
    plan = read_json(path.with_name("plan.json"))
    raw = policy._plan(plan)
    _method_require(value.get("plan_sha256") == plan["record_sha256"], "guard plan binding differs")
    _method_require(raw["metadata"].get("case_selection") == "all declared applicable workload families", "a diagnostic subset cannot qualify full method scope")
    decision = value["guard_decision"]
    policy._verify(decision, schema=policy._record_schema(raw, "report"))
    futility_records = []
    for index, receipt in enumerate(decision.get("futility_records", [])):
        if "record_sha256" not in receipt:
            # The original report retained the verified body instead of the
            # sealed receipt. Recover only its separately retained original;
            # the outer report already binds the exact stripped body.
            retained = read_json(path.parent/"measurements"/("futility-%04d.json" % index))
            body = policy._verify(retained, schema=policy._record_schema(raw, "futility"))
            _method_require(body == receipt, "retained futility receipt differs from the sealed guard report body")
            receipt = retained
        futility_records.append(receipt)
    replay = policy.build_guard_report(plan, decision["screens"], decision["confirmations"],
        functional_failures=decision["functional_failures"], futility_records=futility_records)
    for key in ("functional_failures", "confirmed_regressions", "missing_screens", "screened_hypotheses"):
        _method_require(replay[key] == decision[key], "guard decision does not replay: " + key)
    _method_require(not decision["functional_failures"] and not decision["missing_screens"], "guard functional/integrity or full-screen coverage failed")
    _method_require(value.get("source_integrity", {}).get("status") == "passed", "guard did not finish source/build verification")
    measurements = value.get("measurements", [])
    _method_require(len(measurements) == value.get("measurement_count"), "guard raw measurement count differs")
    for index, row in enumerate(measurements):
        _method_require(read_json(path.parent/"measurements"/f"pair-{index:04d}.json") == row,
                        "saved guard raw observation differs from summary")
    return value, plan


def freeze_family_totals(guard_path, output):
    """Freeze composed total points before independent complete-pipeline holds."""
    value, plan = _method_guard(guard_path)
    rows = {}
    for row in value["measurements"]:
        if row["phase"] != "screen" or not row["hypothesis_id"].startswith("family:"):
            continue
        _method_require(row["status"] == "completed", "family screen did not complete")
        details = row["observation"]["details"]
        _method_require(details.get("full_work") is True, "family screen omitted public target work")
        rows[row["hypothesis_id"]] = {
            "point_seconds": {name: _method_seconds(row["observation"][name+"_seconds"], "frozen family total")
                              for name in ("reference", "candidate")},
            "work_signatures": details["comparison_work_signatures"],
            "source_screen_run_id": row["observation"]["run_id"],
            "physical_generation_seed": details["metadata"].get("actual_generation_seed", details.get("requested_seed")),
            "meaning": "composed measured generation plus complete receiver; independent whole-pipeline validation still required"}
    _method_require(rows, "guard has no complete family screens to freeze")
    result = _method_seal({"schema": "triad-family-total-point-forecast-v1",
        "guard_report_sha256": digest(guard_path), "guard_plan_sha256": plan["record_sha256"],
        "frozen_unix_seconds": time.time(), "cases": rows,
        "validation_policy": {"full_pairs": 3, "two_sided_confidence": .95, "relative_equivalence": .10,
            "metrics": ["reference_total", "candidate_total", "paired_total_ratio"],
            "component_intervals_blocking": False, "fixed_look": True},
        "future_prediction_coverage_qualified": False})
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x") as stream:
        json.dump(result, stream, indent=2, allow_nan=False)
        stream.write("\n")
    return result


def _method_assessment_source_transfer(old_path, expected_sha):
    """Only assessment helpers may change after an unmodified guard run."""
    import ast
    old_path = Path(old_path).resolve(strict=True)
    current = Path(__file__).resolve()
    _method_require(digest(old_path) == expected_sha, "assessment source transfer does not match the measured guard source")
    old, new = (ast.parse(path.read_text()) for path in (old_path, current))
    def assessment(node):
        return isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and (
            node.name.startswith("_method_") or node.name in ("assess_guard_methods", "method_cli", "freeze_family_totals"))
    def protected(tree):
        return ast.dump(ast.Module(body=[node for node in tree.body if not assessment(node)], type_ignores=[]), include_attributes=False)
    _method_require(protected(old) == protected(new), "guard-reachable code, imports or constants changed outside assessment-only helpers")
    return {"kind": "assessment_helpers_only", "old_source_path": str(old_path), "old_source_sha256": expected_sha,
            "current_source_sha256": digest(current), "all_guard_reachable_bodies_constants_imports_unchanged": True,
            "scope": "selected complete-case timing boundaries only; original whole-guard command timing and source record remain unchanged"}


def _method_family_source_transfer(path, expected_sha):
    """One reviewed validation-only library enumeration repair; never a wildcard."""
    import ast
    import triad_perf_families as sample
    import triad_perf_family_regression as paired
    path = Path(path).resolve(strict=True)
    record = read_json(path)
    old_path = Path(record["old_source_path"])
    if not old_path.is_file():
        old_path = path.parent/old_path.name
    current = Path(paired.__file__)
    _method_require(record.get("schema") == "triad-family-validation-source-transfer-v1" and
                    expected_sha == record["old_module_sha256"] == "28d467b7c2261f0878bb2d4050129743c74f602c3f22d0c23e5f4cf6c990b4df" and
                    digest(old_path) == expected_sha and digest(current) == record["current_module_sha256"] ==
                    "49a3643bd245b1ab6e23442d3dd28215e3752b352dc32a4661bb4986128af48a",
                    "family validation transfer is not the reviewed exact source transition")
    def bodies(source):
        return {node.name: hashlib.sha256(ast.dump(node, include_attributes=False).encode()).hexdigest()
                for node in ast.parse(source).body if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))}
    old_bodies, new_bodies = bodies(old_path.read_text()), bodies(current.read_text())
    allowed = {"numerical_agreement", "_regimes"}
    _method_require(old_bodies == record["old_function_bodies"] and new_bodies == record["current_function_bodies"] and
                    set(new_bodies)-set(old_bodies) == {"canonical_library_call"} and
                    set(old_bodies)-set(new_bodies) == set() and
                    {name for name in old_bodies if old_bodies[name] != new_bodies[name]} == allowed,
                    "family validation transfer changed another timed/model function")
    _method_require(all(old_bodies[name] == new_bodies[name] for name in record["protected_timed_and_model_functions"]) and
                    digest(sample.__file__) == record["sample_measurement_source_sha256"],
                    "family timing or generation/receiver measurement source changed")
    return {"kind": "reviewed_library_enumeration_validation_only", "receipt_path": str(path), "receipt_sha256": digest(path),
            "old_module_sha256": expected_sha, "current_module_sha256": digest(current),
            "raw_report_rewritten": False, "protected_timed_and_model_functions_unchanged": True}


def _method_guard_policy_source_transfer(old_path, expected_sha):
    """One reviewed proof-retention repair; no statistical/timing transfer."""
    import ast
    import triad_perf_guard_policy as policy
    old_path = Path(old_path).resolve(strict=True)
    current = Path(policy.__file__).resolve(strict=True)
    _method_require(expected_sha == digest(old_path) ==
                    "31a2c00a236cb2e458c47518380181b75d70ef8b023c228d2283e1a9ccafa529" and
                    digest(current) == "05e6323d1ff3cd31783b4876e9c48212c2c1bee130150a22654f0319cb8f351b",
                    "guard policy transfer is not the reviewed exact futility-retention transition")
    def protected(path):
        tree = ast.parse(path.read_text())
        tree.body = [node for node in tree.body if not (
            isinstance(node, ast.FunctionDef) and node.name == "build_guard_report")]
        return ast.dump(tree, include_attributes=False)
    _method_require(protected(old_path) == protected(current),
                    "guard policy transfer changed statistical, timing or module-level code")
    return {"kind": "reviewed_sealed_futility_retention_only", "old_source_path": str(old_path),
            "old_source_sha256": expected_sha, "current_source_sha256": digest(current),
            "all_statistical_and_measurement_functions_unchanged": True, "raw_report_rewritten": False,
            "scope": "validated sealed futility receipt retention in new report serialization only"}


def _method_source_check(snapshot, roots, binaries, assessment_transfer=None, guard_policy_transfer=None):
    """Verify archive-independent source names against explicitly selected roots."""
    checked = {}
    transfers = []
    for label, expected in snapshot["sha256"].items():
        side, relative = label.split(":", 1)
        if side in roots:
            path = relative_file(roots[side], relative)
        elif side == "harness":
            path = relative_file(ROOT, relative)
        elif side == "binary":
            _method_require(relative in binaries and binaries[relative] is not None, "missing selected binary: " + relative)
            path = Path(binaries[relative]).resolve(strict=True)
        else:
            raise ValueError("unknown source identity category: " + side)
        if digest(path) != expected and label == "harness:tools/triad_perf_complete.py" and assessment_transfer:
            transfers.append(_method_assessment_source_transfer(assessment_transfer, expected))
        elif digest(path) != expected and label == "harness:tools/triad_perf_guard_policy.py" and guard_policy_transfer:
            transfers.append(_method_guard_policy_source_transfer(guard_policy_transfer, expected))
        else:
            _method_require(digest(path) == expected, "current guard source/build identity differs: " + label)
        checked[label] = expected
    return checked, transfers


def _method_expected_inventory(device):
    import triad_perf_native as native
    import triad_perf_cases as fixtures
    import triad_perf_families as families
    import triad_perf_planner as planner
    import triad_perf_resource as resource
    cases = {row["id"]: ("native", row) for row in native.scenarios("ci")}
    cases.update({row["id"]: ("native", row) for row in native.scenarios("sustain") if row["id"] == "native-added-max"})
    cases.update({"fft:"+row["id"]: ("fft", row) for row in fixtures.scenarios("ci")})
    cases.update({"family:"+row["id"]: ("family", row) for row in families.manifest()["cases"] if device in row["devices"]})
    cases.update({"planner:"+row["id"]: ("bounds" if row.get("kind") == "gaussian-design" else "planner", row)
                  for row in planner.manifest()["cases"]+planner.manifest()["bounds_cases"]})
    cases.update({"resource:"+mode: ("resource", mode) for mode in resource.REGIMES})
    return cases


def _method_native(details, case, binaries, threads):
    import triad_perf_native as native
    raw = details["raw"]
    _method_require(details.get("full_target") is True, "native target was abbreviated")
    for side in ("reference", "candidate"):
        row = raw[side]
        actual = row["native_results"]
        _method_require(row["argv"] == native._argv(binaries[side], case, case["target_seconds"], row["seed"], threads), "native full-target invocation changed")
        _method_require(row["argv_sha256"] == native.digest(row["argv"]) and len(actual) == case.get("batch_trials", 1), "native raw invocation/trial accounting differs")
        for result in actual:
            witness = native.coverage_witnesses(result)
            _method_require(all(witness.get(key, False) for key in case["witnesses"]), "native required actual work was not executed")
            _method_require(native.regime(result, case) == row["regime"], "native raw measured regime differs")
        _method_require(row["total_rows"] == sum(r["capture_samples"] for r in actual), "native actual row accounting differs")
        for field in ("basis_values", "blocks", "missing_samples", "channel_samples"):
            _method_require(row["counter_summary"][field] == sum(r[field] for r in actual), "native actual work counter differs")
        _method_require(row["full_test_wall_seconds"] >= row["native_pipeline_seconds"] >=
                        sum(r["wall_seconds"] for r in actual), "native complete timer omitted a nested stage")
    agreement = native.compare_native_outputs(raw["reference"], raw["candidate"])
    _method_require(agreement == details["agreement"] and agreement["eligible"], "native numerical parity does not replay")


def _method_fft(details, fixture, recipe, device, roots):
    import numpy as np
    import triad_perf_cases as fixtures
    from triad_perf_guard_native_fft import _api
    cache = Path(fixture["cache_manifest"])
    receipt = read_json(cache)
    _method_require(receipt["cache_key"] == fixture["cache_key"] and receipt["source_hashes"] == fixture["source_hashes"], "FFT native cache identity differs")
    hashes = fixtures._valid_cache(cache.parent, fixture["cache_key"], fixture["source_hashes"])
    _method_require(hashes and hashes == details["fixture_hashes"], "FFT complete sampled fixture hashes differ")
    manifest = read_json(cache.parent/"workspace/manifest.json")
    configuration = receipt["native_configure"]
    n, fs = manifest["sample_count"], manifest["sample_rate"]
    bounds = configuration["initial_arrival_bounds_seconds"]
    lags = int(np.ceil(np.longdouble(float(bounds[1])-float(bounds[0]))*np.longdouble(fs)))+1
    frequencies = fixtures._frequency_grid(configuration, n, fs, fixtures._config()["limits"])
    groups = lags*len(frequencies)
    for side in ("reference", "candidate"):
        row = details["raw"][side]; prepared, scored = row["prepare"], row["score"]
        _method_require(all(digest(roots[side]/"tools"/relative) == expected for relative, expected in prepared["source_hashes"].items()),
                        "FFT measured receiver sources differ from the selected implementation")
        geometry = prepared["geometry"]; score_geometry = scored["geometry"]
        _method_require(prepared["input_hashes"] == hashes and geometry["sample_count"] == n and geometry["lags"] == lags,
                        "FFT prepared shortened or different sampled input")
        _method_require(geometry["group_tile"] == recipe.get("group_tile", 512) and geometry["device"] == device,
                        "FFT tile/backend regime changed")
        _method_require(score_geometry["frequency_offsets_hz"] == frequencies and score_geometry["base_group_count"] == lags and
                        scored["measured_group_count"] == groups == score_geometry["group_count"] == scored["full_group_count_for_reporting"],
                        "FFT did not score the complete public timing/frequency bank")
        _method_require(sum(scored["rank_histogram"]) == groups, "FFT rank accounting is incomplete")
        _method_require(row["external_seconds"] >= prepared["wall_seconds"]+scored["wall_seconds_including_finish"], "FFT complete timer omits preparation or finish")
    agreement = _api().numeric_agreement(details["raw"]["reference"]["score"], details["raw"]["candidate"]["score"])
    _method_require(agreement == details["agreement"] and agreement["eligible"], "FFT numerical parity does not replay")


def _method_extra(details, case, kind):
    import triad_perf_planner as planner
    import triad_perf_resource as resource
    raw = details["raw"]
    _method_require(details.get("method") == "direct_full" and details.get("model_used") is False, "extra target applied an undeclared forecast")
    if kind == "resource":
        config = resource.load_config(); budget = 5 if case == "paired" else 4
        for side in ("reference", "candidate"):
            resource.verify_counter_record(raw[side], config, mode=case, memory_mib=budget, geometry=details["geometry"][side])
            _method_require(raw[side]["requested_seconds"] == config["target_seconds"] and resource.verify_seal(raw[side]), "resource raw target or identity differs")
        resource.compare_outputs(raw["reference"], raw["candidate"])
    else:
        for side in ("reference", "candidate"):
            if kind == "bounds":
                planner.validate_bound(raw[side]["bounds"], case, details["identity"], side)
            else:
                planner.validate_native(raw[side]["native"], case, details["identity"]["sources"][side])
            _method_require(planner.operation_seconds(raw[side]) <= raw[side]["wall_seconds"]+.001, "planner complete timer omits nested computation")
        if kind == "bounds":
            planner.bounds_agreement(raw["reference"]["bounds"], raw["candidate"]["bounds"])
        else:
            planner.numerical_agreement(raw["reference"]["native"], raw["candidate"]["native"])

def _method_family_job(job, case, root, binary, native_threads):
    import triad_perf_families as sample
    profile = sample.manifest()["fixtures"][case["fixture"]]
    generation = job["generation"]
    count = case.get("captures", 1)
    _method_require(generation["count"] == count == len(generation["commands"]) == len(generation["invocation_wall_seconds"]) == len(job["fixture"]),
                    "family generation omitted complete capture invocations")
    for index, command in enumerate(generation["commands"]):
        _method_require(command == sample.native_arguments(binary, profile, generation["seed"]+index, native_threads, command[-1]),
                        "family native source/profile/seed invocation differs")
        _method_seconds(generation["invocation_wall_seconds"][index], "family complete export")
    _method_require(math.isclose(generation["wall_seconds"], math.fsum(generation["invocation_wall_seconds"]), rel_tol=1e-12, abs_tol=1e-12),
                    "family generation timer differs from complete export invocation totals")
    _method_require(len(job["receiver_runs"]) == 1, "family whole pipeline has an abbreviated or extra receiver request")
    for receipt in job["receiver_runs"]:
        sample.validate_measurement(case, receipt["receiver"], None)
        _method_require(receipt.get("complete_receiver") is True and receipt["source_sha256"] == sample.stage_sources(root, case),
                        "family receiver source changed or did not complete the public work")
        receiver = _method_seconds(receipt["receiver_wall_seconds"], "family receiver")
        _method_require(_method_seconds(receipt["request_wall_seconds"], "family receiver plus IPC") >= receiver,
                        "family receiver/IPC timer omitted measured processing")
    pipeline = _method_seconds(job["complete_pipeline_wall_seconds"], "family complete pipeline")
    _method_require(pipeline+.001 >= generation["wall_seconds"]+job["receiver_runs"][0]["request_wall_seconds"],
                    "family complete pipeline timer omitted generation or receiver/IPC")


def _method_inventory_closures(matrix, method_ids, lifecycle):
    """Only declared mappings backed by the full known targets close gaps."""
    import triad_perf_planner as planner
    import triad_perf_resource as resource
    method_ids = set(method_ids)
    native_planner = {"planner:"+row["id"] for row in planner.manifest()["cases"]}
    bounds = {"planner:"+row["id"] for row in planner.manifest()["bounds_cases"]}
    allowed = {
        planner.GAP: ("planner", native_planner),
        planner.BOUNDS_GAP: ("planner", bounds),
        resource.load_config()["gap_id"]: ("resource", {"resource:"+mode for mode in resource.REGIMES}),
        "scheduler.concurrency-and-batches": ("lifecycle", set()),
    }
    closed = []
    for gap in inventory_gaps(matrix):
        name = gap["id"]
        _method_require(name in allowed, "unrepresented required computational boundary: " + name)
        extension, targets = allowed[name]
        mapping = gap.get("required_extension")
        _method_require(mapping == {"id": extension, "gap_ids": [name]}, "required boundary mapping differs: " + name)
        _method_require(targets <= method_ids and (extension != "lifecycle" or lifecycle.get("method_status") == "qualified"),
                        "required computational boundary lacks complete selected-method evidence: " + name)
        closed.append({"id": name, "extension": extension, "full_target_case_ids": sorted(targets),
                       "method": "replayed selected full targets" if targets else "replayed complete cold/mixed campaign proof"})
    return closed


def _method_direct_family(details, case, observation, roots, binaries, precision, native_threads):
    import triad_perf_families as sample
    import triad_perf_family_regression as paired
    from triad_perf_guard_families import comparison_pair
    metadata = details.get("metadata", {})
    _method_require(metadata.get("direct_complete_pipeline_observation") is True and
                    metadata.get("generation_shared") is False and metadata.get("extrapolation") is False and
                    details.get("timing_field") == "complete_pipeline_wall_seconds" and details.get("full_work") is True,
                    "family screen is a composed estimate, not a complete pipeline measurement")
    raw = details["pair"]
    comparable, _ = comparison_pair(raw, case)
    agreement = paired.numerical_agreement(comparable, precision)
    _method_require(agreement == details["numerical_agreement"], "family numerical result does not replay")
    for side in ("reference", "candidate"):
        job = raw[side]
        _method_family_job(job, case, roots[side], binaries[side], native_threads)
        _method_require(sample.complete_work_signature(comparable[side]) == details["comparison_work_signatures"][side],
                        "family complete target work does not replay")
        _method_require(job["generation"]["count"] == case.get("captures", 1) and len(job["fixture"]) == case.get("captures", 1),
                        "family generation omitted a complete capture")
        wall = _method_seconds(job["complete_pipeline_wall_seconds"], "family complete pipeline")
        _method_require(wall == observation[side+"_seconds"] and
                        wall+.001 >= job["generation"]["wall_seconds"]+job["receiver_runs"][0]["request_wall_seconds"],
                        "family full timer omits generation/receiver or differs from its raw observation")
        checked = details["source_checks"][side]
        _method_require(checked.get("environment_matches_setup") is True and
                        checked["source_sha256"] == sample.stage_sources(roots[side], case), "family measured source or environment drifted")
    return {"observation_count": 1, "method": "direct_full", "forecast_applied": False,
            "complete_pipeline_seconds": {side: raw[side]["complete_pipeline_wall_seconds"] for side in ("reference", "candidate")},
            "statistical_independence_claim": False, "scope": "one actual complete fresh-capture pipeline; warm workers, no mean confidence interval or extrapolation"}


def _method_full_family(directory, name, case, screen, guard, roots, binaries, precision, source_transfer=None):
    import triad_perf_families as sample
    import triad_perf_family_regression as paired
    from triad_perf_guard_families import comparison_pair
    path = Path(directory)/case["id"]/"result.json"
    value = read_json(path)
    _method_verify_seal(value)
    _method_require(value.get("schema") == paired.SCHEMA and value.get("selected_measurement") == "direct" and
                    value.get("status") == "direct_full" and value.get("prediction_valid") is True,
                    name + ": no valid direct whole-pipeline evidence")
    identity = value["identity"]
    expected_profile = sample.manifest()["fixtures"][case["fixture"]]
    _method_require(identity["case"] == case and identity["fixture_profile"] == expected_profile and
                    identity["precision"] == precision and identity["device"] == guard["device"],
                    name + ": full-family target/arithmetic/backend differs")
    for side in ("reference", "candidate"):
        _method_require(identity["binaries"][side] == digest(binaries[side]) and
                        identity["adapters"][side] == sample.stage_sources(roots[side], case),
                        name + ": full-family implementation identity differs")
        setup = guard["adapter_summaries"]["families"]["family_setup"][name][side]
        _method_require(identity["environment"][side] == setup["environment"], name + ": full-family hardware/library/cache environment differs")
    transfers = []
    for relative, expected in identity["measurement_dependencies"].items():
        if digest(ROOT/"tools"/relative) == expected:
            continue
        _method_require(relative == "triad_perf_family_regression.py" and source_transfer is not None,
                        name + ": full-family measurement source differs: " + relative)
        transfers.append(_method_family_source_transfer(source_transfer, expected))
    rows = value["training"]
    _method_require(len(rows) >= 3 and len({row["pair_id"] for row in rows}) == len(rows), name + ": fewer than three distinct full-pipeline observations")
    times = {side: [] for side in ("reference", "candidate")}
    physical_seeds = []
    for row in rows:
        comparable, _ = comparison_pair(row, case)
        paired.numerical_agreement(comparable, precision)
        for side in times:
            signature = sample.complete_work_signature(comparable[side])
            _method_require(signature == screen["comparison_work_signatures"][side], name + ": full-family public work differs from current screen")
            job = row[side]
            _method_family_job(job, case, roots[side], binaries[side], identity["native_threads"])
            wall = _method_seconds(job["complete_pipeline_wall_seconds"], name+" full pipeline")
            nested = job["generation"]["wall_seconds"]+job["receiver_runs"][0]["request_wall_seconds"]
            _method_require(wall+.001 >= nested, name + ": whole-pipeline timer omits generation or receiver")
            times[side].append(wall)
        physical_seeds.append(row["reference"]["generation"]["seed"])
    _method_require(len(set(physical_seeds)) == len(physical_seeds), name + ": physical full-family seeds were reused")
    return {"path": str(path.resolve()), "sha256": digest(path), "times": times,
            "physical_seeds": physical_seeds, "observation_count": len(rows),
            "report_mtime_unix_seconds": path.stat().st_mtime, "reviewed_source_transfers": transfers,
            "timing_boundary": value["timing_scope"], "prediction_model_used": False,
            "sampling_scope": "complete fresh captures in the recorded warm worker/environment; no unmeasured duration or state-count scaling"}


def _method_forecast_validation(frozen, name, full):
    """Assess only full-pipeline means; stage uncertainty is diagnostic."""
    from triad_perf_staged import stage_interval, ratio_interval
    policy = frozen["validation_policy"]
    _method_require(policy == {"full_pairs": 3, "two_sided_confidence": .95, "relative_equivalence": .10,
        "metrics": ["reference_total", "candidate_total", "paired_total_ratio"], "component_intervals_blocking": False, "fixed_look": True},
        "unknown total-forecast validation contract")
    row = frozen["cases"][name]
    _method_require(full["report_mtime_unix_seconds"] >= frozen["frozen_unix_seconds"], "historical full-family data predates the frozen forecast and is not an independent prospective holdout")
    _method_require(row["physical_generation_seed"] not in full["physical_seeds"], "forecast reused its screen capture as a full holdout")
    values = {side: times[:3] for side, times in full["times"].items()}
    checks = {side: stage_interval(times, .95) for side, times in values.items()}
    checks["paired"] = ratio_interval(values["reference"], values["candidate"], .95)
    expected = dict(row["point_seconds"])
    expected["paired"] = expected["candidate"]/expected["reference"]
    results = {}
    for metric, check in checks.items():
        band = check["interval"]
        relative = None if band is None else [value/expected[metric]-1 for value in band]
        status = ("validated" if relative is not None and -.10 <= relative[0] <= relative[1] <= .10 else
                  "invalid" if relative is not None and (relative[1] < -.10 or relative[0] > .10) else "inconclusive")
        results[metric] = {"point_forecast_seconds_or_ratio": expected[metric], "actual": check,
                           "relative_error_interval": relative, "status": status}
    states = {row["status"] for row in results.values()}
    return {"status": "validated" if states == {"validated"} else "invalid" if "invalid" in states else "inconclusive",
            "checks": results, "selected_timing_method": "direct_full",
            "routine_blocking": False, "future_prediction_coverage_qualified": False,
            "interval_scope": "conditional Student-t arithmetic total means and paired Fieller ratio at one fixed independent holdout look; warm-context sampling assumptions retained"}


def _method_lifecycle(path, anchors, context):
    """Use the existing source/work/numerical proof checkers, never status alone."""
    import triad_perf_lifecycle as lifecycle
    value = read_json(path)
    if value.get("mode") == "reuse":
        checked = lifecycle.validate_routine_report(value, {**context, "report_path": str(path)})
        _method_require(checked["valid"] and not checked["errors"], "lifecycle raw source/work/model replay failed: " + "; ".join(checked["errors"]))
        outcomes = checked["outcomes"]
        _method_require(outcomes["sustained_observation"] == "complete" and
                        outcomes["fixed_mixed_campaign"] in ("qualified", "failed", "inconclusive"), "complete mixed-family sustained target was not observed")
        cold = checked["cold_validation"]
        _method_require(cold["valid"] and cold["case_ids"] and not cold["errors"], "cold full-target method evidence is unqualified")
        _method_require(all(state != "unqualified" for state in outcomes["case_status"].values()), "lifecycle selected method remains unqualified")
        return {"path": str(Path(path).resolve()), "sha256": digest(path), "validation": checked,
                "method_status": "qualified", "performance_decision": checked["status"],
                "stability_review_preserved": checked.get("review_required", [])}
    _method_require(value.get("mode") == "qualify" and value.get("diagnostic") is False and not value.get("errors"), "invalid lifecycle qualification execution")
    _method_require(value["protocol_sha256"] == lifecycle.sha(lifecycle.WORKER) and value["manifest_sha256"] == lifecycle.sha(lifecycle.MANIFEST), "qualification source/protocol identity differs")
    config, families, known = lifecycle.load_manifest()
    cases = [case for case in config["cases"] if case["device"] == context["device"]]
    expected_ids = {case["id"] for case in cases}
    rows = {row["id"]: row for row in value["cases"]}
    _method_require(len(rows) == len(value["cases"]) and set(rows) <= expected_ids, "lifecycle qualification has unexpected or duplicated cases")
    cold_origin = None
    if set(rows) != expected_ids:
        _method_require(context.get("cold_report") is not None, "lifecycle qualification inventory is incomplete")
        cold_origin = read_json(context["cold_report"])
        _method_require(cold_origin.get("mode") == "qualify" and cold_origin.get("diagnostic") is False and
                        cold_origin.get("protocol_sha256") == lifecycle.REVIEWED_POOL_ENUM_PROTOCOL93DE and
                        cold_origin.get("manifest_sha256") == lifecycle.sha(lifecycle.MANIFEST),
                        "retained cold report is not the reviewed validation-only source transition")
        old_rows = {row["id"]: row for row in cold_origin["cases"]}
        for case in cases:
            if case["id"] not in rows:
                _method_require(case["kind"] == "cold" and case["id"] in old_rows, "mixed campaign cannot be filled from an incomplete cold run")
                rows[case["id"]] = old_rows[case["id"]]
    roots = {side: Path(context[side+"_root"]) for side in ("reference", "candidate")}
    binaries = {"reference": context["reference_binary"], "candidate": context["binary"]}
    proofs = []
    for case in cases:
        anchor = Path(anchors)/(case["id"]+".lifecycle-anchor.json")
        proof = read_json(anchor)
        valid = lifecycle._validate_cold_anchor(proof) if case["kind"] == "cold" else lifecycle._validate_anchor(proof)
        _method_require(valid, case["id"] + ": independent lifecycle proof does not replay")
        binding = lifecycle.static_binding(roots, binaries, case, families, known, device=context["device"],
            precision=context["precision"], threads=context["threads"], native_threads=context["native_threads"])
        _method_require(proof["static_binding"] == binding and rows[case["id"]]["static_binding"] == binding,
                        case["id"] + ": lifecycle current implementation/work identity differs")
        _method_require(proof["full_pairs"] == rows[case["id"]]["full_pairs"], case["id"] + ": lifecycle proof/raw report differs")
        proofs.append({"id": case["id"], "path": str(anchor.resolve()), "sha256": digest(anchor),
                       "method": "direct_full" if case["kind"] == "cold" else "independently_validated_F_plus_C_times_W",
                       "full_pair_count": len(proof["full_pairs"]), "comparison": rows[case["id"]]["comparison"]})
    return {"path": str(Path(path).resolve()), "sha256": digest(path), "method_status": "qualified", "proofs": proofs,
            "performance_decision": value.get("status"), "stability_review_preserved": value.get("review_required", []),
            "retained_cold_origin": ({"path": str(Path(context["cold_report"]).resolve()), "sha256": digest(context["cold_report"]),
                                      "original_global_status": cold_origin.get("status"), "original_errors_preserved": cold_origin.get("errors"),
                                      "transfer": "only individually reconstructed cold proofs; failed original mixed case is not reused"} if cold_origin else None),
            "source_scope": "exact backend/hardware/library/cache identity in each reconstructed proof; no cross-device transfer"}


def assess_guard_methods(args):
    """File-only selected-method qualification; no simulation or receiver launch."""
    import tempfile
    import triad_perf_guard as guard_api
    import triad_perf_native as native
    value, plan = _method_guard(args.guard_report)
    roots = {"candidate": args.candidate_root.resolve(strict=True), "reference": args.reference_root.resolve(strict=True)}
    binaries = {"candidate": args.binary.resolve(strict=True), "reference": args.reference_binary.resolve(strict=True),
                "candidate_planner": args.planner_binary.resolve(strict=True), "reference_planner": args.planner_reference_binary.resolve(strict=True)}
    current_sources, source_transfers = _method_source_check(value["source_identity"], roots, binaries, args.assessment_source_transfer,
                                                           args.guard_policy_source_transfer)
    metadata = plan["metadata"]
    _method_require(metadata["source_and_build_identity"] == value["source_identity"], "guard plan/source snapshot differs")
    original = guard_api.parse_args(value["argv"][1:])
    def input_path(path):
        return path.resolve() if path.is_absolute() else (roots["candidate"]/path).resolve()
    context = {"candidate_root": roots["candidate"], "reference_root": roots["reference"],
               "binary": binaries["candidate"], "reference_binary": binaries["reference"],
               "planner_binary": binaries["candidate_planner"], "planner_reference_binary": binaries["reference_planner"],
               "planner_bounds_workspace": args.planner_bounds_workspace.resolve(strict=True),
               "fixture_cache": (input_path(original.fixture_cache) if original.fixture_cache else args.guard_report.parent/"fixtures"), "device": value["device"],
               "precision": metadata["precision"], "threads": metadata["threads"], "native_threads": metadata["native_threads"],
               "deadline": time.monotonic()+60.}
    with tempfile.TemporaryDirectory(prefix="triad-method-inspection-") as scratch:
        adapters = [(name, factory({**context, "output": Path(scratch)/name})) for name, factory in guard_api.adapter_factories()]
        try:
            descriptors, _ = guard_api.collect_descriptors(adapters)
        finally:
            for _, adapter in reversed(adapters):
                adapter.close()
    _method_require(descriptors == value["descriptors"] == metadata["descriptors"], "current maintained workload descriptions or identities differ")
    expected = _method_expected_inventory(value["device"])
    _method_require(set(expected) == set(plan["hypothesis_ids"]) == {row["id"] for row in descriptors}, "guard inventory is not the complete maintained backend scope")
    _method_require(all(row.get("available", True) for row in descriptors), "a maintained workload was unavailable")
    functional = value["functional_commands"]
    required = functional_defaults(roots["candidate"], binaries["candidate"], read_json(ROOT/"tools/triad-performance-coverage.json"), value["device"])
    _method_require(metadata.get("functional_defaults_requested") is True and len(functional) == len(required)+1, "complete mapped functional command set was not supplied")
    _method_require(all(row.get("command", {}).get("status") == "passed" and row.get("evidence", {}).get("status") == "passed" for row in functional), "mapped actual functional execution failed or was skipped")
    _method_require([row["command"]["argv"] for row in functional[1:]] == required, "mapped functional command set changed")
    _method_require(functional[0]["kind"] == "native_inventory" and
                    native_inventory(functional[0]["command"]) == functional[0]["evidence"], "native functional inventory does not replay")
    matrix = read_json(ROOT/"tools/triad-performance-coverage.json")
    for command, receipt in zip(required, functional[1:]):
        methods = sorted({item.split("::", 1)[1] for row in matrix["rows"] for item in row["cheap_functional_tests"]
                          if ".py::" in item and str((roots["candidate"]/item.split("::", 1)[0]).resolve()) in command})
        _method_require(functional_evidence(command, receipt["command"], value["device"], methods) == receipt["evidence"],
                        "raw functional result does not replay")
    coverage = []
    for kind, path in (("native", args.native_coverage), ("python", args.python_coverage)):
        sources = matrix["coverage_reports"][kind]["source_files"]
        checked = check_coverage(path, roots["candidate"], sources, coverage_dependencies(roots["candidate"], matrix, sources, kind))
        _method_require(checked["historical_applicability"] == "declared_source_and_dependency_match" and not checked["missing_expected_sources"], "current "+kind+" source/branch instrumentation does not apply")
        coverage.append(checked)
    screens = {row["hypothesis_id"]: row for row in value["measurements"] if row["phase"] == "screen"}
    _method_require(set(screens) == set(expected) and all(row["status"] == "completed" for row in screens.values()), "actual complete workload screens are missing")
    fixture_events = [row for row in value["adapter_summaries"]["native_fft"]["events"] if row["kind"] == "immutable_fixture_preparation"]
    _method_require(len(fixture_events) == 1, "source-bound FFT fixture preparation evidence missing")
    full_families, methods, forecasts = {}, [], {}
    frozen = read_json(args.family_total_forecasts) if args.family_total_forecasts else None
    if frozen is not None:
        _method_verify_seal(frozen)
        _method_require(frozen["schema"] == "triad-family-total-point-forecast-v1" and
                        frozen["guard_report_sha256"] == digest(args.guard_report) and frozen["guard_plan_sha256"] == plan["record_sha256"], "frozen total forecast belongs to different calibration")
    for name, (kind, case) in expected.items():
        row = screens[name]; observation = row["observation"]; details = observation["details"]
        _method_require(row["plan_sha256"] == plan["record_sha256"] and observation["run_id"] == row["invocation"]["run_id"], "raw workload plan/run identity differs")
        for side in ("reference", "candidate"):
            _method_seconds(observation[side+"_seconds"], name+" complete target")
        descriptor = next(d for d in descriptors if d["id"] == name)
        boundary = descriptor["boundary"]
        if kind == "native":
            _method_native(details, case, binaries, metadata["native_threads"])
        elif kind == "fft":
            _method_fft(details, fixture_events[0]["cases"][case["id"]], case, value["device"], roots)
        elif kind == "family":
            direct = _method_direct_family(details, case, observation, roots, binaries, metadata["precision"], metadata["native_threads"])
            full_families[name] = direct
            if args.full_family_directory is not None:
                full = _method_full_family(args.full_family_directory, name, case, details, value, roots, binaries, metadata["precision"], args.family_source_transfer)
                full_families[name]["retained_full_pipeline_evidence"] = full
                if frozen is not None:
                    forecasts[name] = _method_forecast_validation(frozen, name, full)
            elif frozen is not None:
                raise ValueError("prospective total-forecast validation requires independent full-family holdouts")
        else:
            _method_extra(details, case, kind)
        if kind != "family":
            field = "external_seconds" if kind == "fft" else "full_test_wall_seconds" if kind in ("native", "resource") else "wall_seconds"
            _method_require(all(observation[side+"_seconds"] == details["raw"][side][field]
                                for side in ("reference", "candidate")), "reported complete-target time differs from its actual raw timer")
        methods.append({"id": name, "method": "direct_full", "complete_work_executed": True,
                        "timing_boundary": boundary, "workload": descriptor,
                        "extrapolation_applied": False, "unmeasured_scaling_qualified": False,
                        "point_seconds": {side: observation[side+"_seconds"] for side in ("reference", "candidate")},
                        "observation_scope": "one current full-target screen; no mean-runtime confidence interval inferred"})
    lifecycle_context = {**context, "regression_profile": read_json(args.lifecycle_report)["regression_profile"],
                         "anchor_directory": args.lifecycle_anchor_directory,
                         "cold_report": args.lifecycle_cold_report,
                         "mixed_method": read_json(args.lifecycle_report).get("mixed_method", "modeled"),
                         "direct_cycles": read_json(args.lifecycle_report).get("direct_cycles_selection", "6")}
    lifecycle = _method_lifecycle(args.lifecycle_report, args.lifecycle_anchor_directory, lifecycle_context)
    _method_require(value["adapter_summaries"]["extras"].get("resource_cross_regime_functional") == "passed", "resource memory regimes lack actual cross-regime byte parity")
    closures = _method_inventory_closures(matrix, [method["id"] for method in methods], lifecycle)
    result = {"schema": "triad-selected-runtime-method-assessment-v1", "status": "qualified", "complete": True,
              "qualification_objective": "finite selected computational/timing methods; implementation speed and CI decision calibration are separate",
              "guard_report": {"path": str(args.guard_report.resolve()), "sha256": digest(args.guard_report), "plan_sha256": plan["record_sha256"],
                  "restored_futility_receipts": [{"path": str((args.guard_report.parent/"measurements"/("futility-%04d.json" % index)).resolve()),
                      "sha256": digest(args.guard_report.parent/"measurements"/("futility-%04d.json" % index)),
                      "stored_body_matched_exactly": True} for index, receipt in enumerate(value["guard_decision"].get("futility_records", []))
                      if "record_sha256" not in receipt]},
              "source_sha256": current_sources, "assessment_source_transfers": source_transfers, "device": value["device"], "selected_methods": methods,
              "coverage": coverage, "full_family_receipts": full_families, "lifecycle": lifecycle,
              "validated_inventory_gap_closures": closures, "remaining_required_inventory_gaps": [],
              "total_forecast_assessment": forecasts, "frozen_total_forecast_sha256": digest(args.family_total_forecasts) if frozen else None,
              "original_guard_result_unchanged": True, "legacy_qualification_result_unchanged": True,
              "performance_comparison": {"guard_status": value["performance_status"], "confirmed_regressions": value["confirmed_regressions"],
                                         "affirmative_non_regression_certification": False},
              "hardware_health": value["hardware_health"],
              "limits": ["Direct targets do not qualify arbitrary observation lengths, state counts, channel combinations or memory regimes.",
                         "Standalone full-family timing samples retain their recorded warm-worker sampling conditions; no global hardware or rare-stall confidence bound is inferred.",
                         "Historical profitable native-duration and FFT-bank forecasts retain their separate source-bound independent holdout qualifications; no failed alternative model is promoted here.",
                         "Whole-workflow CI false-block controls are qualified separately by the guard measurement-protocol campaign.",
                         "Hardware/stability diagnostics remain visible; method qualification is not a hardware-health pass or release certification."]}
    return _method_seal(result)


def method_cli(argv):
    """Qualification-only file replay, deliberately separate from legacy execution."""
    parser = argparse.ArgumentParser(description="Freeze total forecasts or assess selected Triad timing methods without receiver computation.")
    parser.add_argument("mode", choices=("assess-methods", "freeze-family-totals"))
    parser.add_argument("--guard-report", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--candidate-root", type=Path, default=ROOT)
    parser.add_argument("--reference-root", type=Path)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--reference-binary", type=Path)
    parser.add_argument("--planner-binary", type=Path)
    parser.add_argument("--planner-reference-binary", type=Path)
    parser.add_argument("--planner-bounds-workspace", type=Path)
    parser.add_argument("--native-coverage", type=Path)
    parser.add_argument("--python-coverage", type=Path)
    parser.add_argument("--lifecycle-report", type=Path)
    parser.add_argument("--lifecycle-anchor-directory", type=Path)
    parser.add_argument("--full-family-directory", type=Path)
    parser.add_argument("--family-total-forecasts", type=Path)
    parser.add_argument("--family-source-transfer", type=Path)
    parser.add_argument("--assessment-source-transfer", type=Path)
    parser.add_argument("--guard-policy-source-transfer", type=Path)
    parser.add_argument("--lifecycle-cold-report", type=Path)
    args = parser.parse_args(argv)
    if args.mode == "freeze-family-totals":
        result = freeze_family_totals(args.guard_report, args.output)
    else:
        required = ("reference_root", "binary", "reference_binary", "planner_binary", "planner_reference_binary",
                    "planner_bounds_workspace", "native_coverage", "python_coverage", "lifecycle_report",
                    "lifecycle_anchor_directory")
        if any(getattr(args, key) is None for key in required):
            parser.error("assess-methods requires explicit roots/binaries, both coverage receipts and lifecycle proof")
        _method_require(not args.output.exists(), "refusing to overwrite method qualification evidence")
        start = time.perf_counter()
        try:
            result = assess_guard_methods(args)
        except (OSError, ValueError, KeyError, TypeError, IndexError, OverflowError, ImportError, AttributeError) as error:
            result = _method_seal({"schema": "triad-selected-runtime-method-assessment-v1", "status": "incomplete", "complete": False,
                                   "errors": [type(error).__name__+": "+str(error)], "original_reports_rewritten": False})
        result["assessment_wall_seconds"] = time.perf_counter()-start
        result = _method_seal({key: value for key, value in result.items() if key != "record_sha256"})
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("x") as stream:
            json.dump(result, stream, indent=2, allow_nan=False)
            stream.write("\n")
    print(json.dumps({"status": result.get("status", "frozen"), "complete": result.get("complete"), "output": str(args.output)}, allow_nan=False))
    return 0 if args.mode == "freeze-family-totals" or result.get("complete") else 2


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, required=True, help="new evidence directory")
    parser.add_argument("--candidate-root", type=Path, default=ROOT)
    parser.add_argument("--reference-root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--reference-binary", type=Path, required=True)
    parser.add_argument("--baseline-ref", default=BASELINE)
    parser.add_argument("--device", choices=("cpu", "cuda"), default="cpu")
    parser.add_argument("--precision", choices=("fp32", "fp64"), default="fp32",
                        help="sampled-family arithmetic; legacy FFT retains its own strict-reference selection")
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--native-threads", type=int, default=1)
    parser.add_argument("--budget-seconds", type=float, default=1500.)
    parser.add_argument("--stopping-rule", choices=("decision", "precision"), default="decision",
                        help="routine default stops on the child per-test regression decision; precision requests interval-width sampling")
    parser.add_argument("--regression-profile", choices=("normal", "ci", "strict"), default="normal",
                        help="shared per-test pass/failure error budgets and slowdown threshold; CI selects ci explicitly")
    parser.add_argument("--anchor-directory", type=Path, required=True)
    parser.add_argument("--native-direct-case", action="append", default=[], metavar="CASE_ID",
                        help="explicitly measure this native full target directly without its duration forecast; repeatable")
    parser.add_argument("--family-route", choices=("paired", "staged"), default="paired")
    parser.add_argument("--family-mode", choices=("reuse", "direct"), default="reuse",
                        help="explicit direct full measurement bypasses forecasting; never retries an unsuccessful model automatically")
    parser.add_argument("--family-anchor-directory", type=Path,
                        help="exact paired-family anchor directory (required for paired route)")
    parser.add_argument("--staged-anchor-directory", type=Path,
                        help="exact independently qualified staged anchor directory")
    parser.add_argument("--identical-fft-case", action="append", default=[], metavar="CASE_ID",
                        help="route this CI FFT case to strict identity deduplication; never auto-qualifies")
    parser.add_argument("--identical-anchor-directory", type=Path)
    parser.add_argument("--fixture-cache", type=Path, help="retained FFT fixture cache shared by legacy and identical routes")
    parser.add_argument("--hardware-baseline", type=Path)
    parser.add_argument("--native-coverage", type=Path)
    parser.add_argument("--python-coverage", type=Path)
    parser.add_argument("--lifecycle-anchor-directory", type=Path,
                        help="enable source-bound cold/mixed-campaign historical reuse; never qualifies automatically")
    parser.add_argument("--lifecycle-mixed-method", choices=("modeled", "direct"), default="modeled",
                        help="direct enables complete CPU lifecycle measurements without a forecast anchor; modeled preserves historical reuse")
    parser.add_argument("--lifecycle-direct-cycles", default="6", metavar="auto|N",
                        help="CPU direct target cycles, 2..256; auto freezes a sustained target from a discarded pilot")
    parser.add_argument("--resource-anchor", type=Path,
                        help="enable direct full resource-boundary checks with independently qualified historical proof")
    parser.add_argument("--resource-direct", action="store_true",
                        help="measure both complete resource regimes directly; any supplied anchor is history only, never a qualified forecast")
    parser.add_argument("--planner-binary", type=Path, help="enable direct native planner/statistics timing")
    parser.add_argument("--planner-reference-binary", type=Path)
    parser.add_argument("--planner-bounds-workspace", type=Path,
                        help="retained sampled-statistics directory for separate complete Gaussian-bound timing")
    parser.add_argument("--functional-defaults", action="store_true")
    parser.add_argument("--functional-command", action="append", default=[], metavar="ARGV_JSON")
    args = parser.parse_args(argv)
    if not math.isfinite(args.budget_seconds) or not 0 < args.budget_seconds <= 28800:
        parser.error("budget-seconds must be in (0,28800]")
    if not 1 <= args.threads <= 256 or not 1 <= args.native_threads <= 256:
        parser.error("thread counts must be in [1,256]")
    if not re.fullmatch(r"[0-9a-f]{40}", args.baseline_ref):
        parser.error("baseline-ref must be a full immutable Git object ID")
    native_config = read_json(ROOT/"tools/triad-performance-native-cases.json")
    modeled_native = {row["id"] for row in native_config["cases"]
                      if row["id"] in native_config["profiles"]["ci"] and "training_seconds" in row}
    if len(args.native_direct_case) != len(set(args.native_direct_case)) or set(args.native_direct_case)-modeled_native:
        parser.error("native-direct-case requires unique modeled native IDs from the full CI profile")
    if args.family_mode == "reuse" and args.family_route == "paired" and args.family_anchor_directory is None:
        parser.error("paired family route requires --family-anchor-directory")
    if args.family_mode == "reuse" and args.family_route == "staged" and args.staged_anchor_directory is None:
        parser.error("staged family route requires --staged-anchor-directory")
    if args.identical_fft_case and (args.identical_anchor_directory is None or args.fixture_cache is None):
        parser.error("identical FFT selection requires --identical-anchor-directory and --fixture-cache")
    if bool(args.planner_binary) != bool(args.planner_reference_binary):
        parser.error("planner extension requires both --planner-binary and --planner-reference-binary")
    if args.planner_bounds_workspace and not args.planner_binary:
        parser.error("planner bounds require the planner extension binaries")
    if args.lifecycle_mixed_method == "direct" and args.device != "cpu":
        parser.error("direct mixed lifecycle measurement is currently scoped to CPU")
    if args.lifecycle_direct_cycles != "auto":
        if not re.fullmatch(r"[0-9]+", args.lifecycle_direct_cycles) or not 2 <= int(args.lifecycle_direct_cycles) <= 256:
            parser.error("lifecycle-direct-cycles must be auto or an integer in [2,256]")
        args.lifecycle_direct_cycles = int(args.lifecycle_direct_cycles)
    commands = []
    for value in args.functional_command:
        try:
            command = json.loads(value)
            if not isinstance(command, list) or not command or not all(isinstance(v, str) and v and "\x00" not in v for v in command):
                raise ValueError("expected a nonempty string argv array")
        except (ValueError, TypeError) as error:
            parser.error(f"invalid functional-command: {error}")
        commands.append(command)
    args.functional_command = commands
    return args


def main(argv=None):
    args = parse_args(argv)
    if os.name != "posix":
        raise ValueError("single-deadline descendant supervision requires POSIX")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    start = time.perf_counter()
    deadline = time.monotonic()+args.budget_seconds
    from triad_perf_adaptive import regression_profile
    candidate = args.candidate_root.resolve(strict=True)
    reference = args.reference_root.resolve(strict=True)
    binary = args.binary.resolve(strict=True)
    reference_binary = args.reference_binary.resolve(strict=True)
    extensions = extension_specs(args, candidate, reference, binary, reference_binary, output)
    extension_binaries = {str(path): digest(path) for path in
                         (args.planner_binary.resolve(strict=True), args.planner_reference_binary.resolve(strict=True))} if args.planner_binary else {}
    matrix_path = ROOT/"tools/triad-performance-coverage.json"
    family_path = ROOT/"tools/triad-performance-families.json"
    native_cases_path = ROOT/"tools/triad-performance-native-cases.json"
    fft_cases_path = ROOT/"tools/triad-performance-cases.json"
    matrix, families = read_json(matrix_path), read_json(family_path)
    route_plan = make_route_plan(read_json(native_cases_path), read_json(fft_cases_path), args.identical_fft_case)
    family_driver = ROOT/"tools"/("triad_perf_staged.py" if args.family_route == "staged" else "triad_perf_family_regression.py")
    family_anchors = args.staged_anchor_directory if args.family_route == "staged" else args.family_anchor_directory
    family_anchors = family_anchors.resolve() if family_anchors else None
    applicable = [case["id"] for case in families["cases"] if args.device in case["devices"]]
    excluded = [case["id"] for case in families["cases"] if args.device not in case["devices"]]
    identity = source_identity(candidate, matrix, families)
    protocol = {str(path.relative_to(ROOT)): digest(path) for path in
                (Path(__file__), matrix_path, family_path, native_cases_path, fft_cases_path,
                 ROOT/"tools/triad-performance.py", ROOT/"tools/triad_perf_adaptive.py", family_driver,
                 *([ROOT/"tools/triad_perf_identical_fft.py"] if args.identical_fft_case else []),
                 *[spec["driver"] for spec in extensions])}
    report = {"schema": SCHEMA, "status": "running", "device": args.device,
              "budget_seconds": args.budget_seconds, "routine_budget_target_seconds": 1500.,
              "source_sha256": identity, "protocol_sha256": protocol,
              "native_binary_sha256": {"candidate": digest(binary), "reference": digest(reference_binary)},
              "extension_binary_sha256": extension_binaries,
              "functional_commands": [], "functional_scope": "matrix-named focused tests" if args.functional_defaults else "operator-supplied command scope only",
              "functional_defaults_requested": args.functional_defaults, "required_default_functional_commands": [],
              "coverage_receipts": [], "children": {}, "errors": [], "applicable_sampled_cases": applicable,
              "extensions": {}, "configured_extensions": [spec["id"] for spec in extensions],
              "not_applicable_on_selected_backend": excluded,
              "route_plan": route_plan, "family_route": args.family_route, "family_mode": args.family_mode,
              "native_direct_case_ids": list(args.native_direct_case),
              "requested_stopping_rule": args.stopping_rule,
              "regression_profile": {"name": args.regression_profile, **regression_profile(args.regression_profile)},
              "core_decision_profile_active": args.stopping_rule == "decision",
              "anchor_directories": {"legacy": str(args.anchor_directory.resolve()), "sampled": str(family_anchors) if family_anchors else None,
                                     "identical_fft": str(args.identical_anchor_directory.resolve()) if args.identical_anchor_directory else None},
              "inventory_unresolved": inventory_gaps(matrix),
              "inventory_limitations": inventory_limitations(matrix),
              "ordinary_receiver_scope": "excluded", "routine_requalifies_models": False,
              "uncertainty_scope": "Each child owns the repeated-look handling and uncertainty calculation for the explicitly requested stopping rule. This aggregation does not create a joint confidence level across tests, harnesses, hardware screens or coverage outcomes."}
    legacy, sampled, identical = None, {}, {}
    env = child_environment(args.threads)
    write_json(output/"summary.json", report)
    try:
        report["fixture_cache"] = connect_fixture_cache(output, args.fixture_cache)
        for kind, path in (("native", args.native_coverage), ("python", args.python_coverage)):
            if path:
                expected = matrix["coverage_reports"][kind]["source_files"]
                dependencies = coverage_dependencies(candidate, matrix, expected, kind)
                report["coverage_receipts"].append(check_coverage(path, candidate, expected, dependencies))
        if args.functional_defaults:
            command = ["ctest", "--test-dir", str(binary.parent), "-N", "--show-only=json-v1", "-R", NATIVE_TESTS]
            receipt = execute(command, cwd=candidate, directory=output/"native-inventory", deadline=deadline, env=env)
            report["native_test_inventory"] = {**native_inventory(receipt), "command": receipt}
            if report["native_test_inventory"]["status"] != "passed":
                raise ValueError("the four required native functional tests are missing, duplicated, disabled or unenumerated")
        commands = functional_defaults(candidate, binary, matrix, args.device) if args.functional_defaults else []
        report["required_default_functional_commands"] = list(commands)
        commands += args.functional_command
        for i, command in enumerate(commands):
            receipt = execute(command, cwd=candidate, directory=output/f"functional-{i:02d}", deadline=deadline, env=env)
            required_methods = sorted({item.split("::", 1)[1] for row in matrix["rows"]
                for item in row["cheap_functional_tests"] if ".py::" in item and
                str((candidate/item.split("::", 1)[0]).resolve()) in command})
            receipt["functional_evidence"] = functional_evidence(command, receipt, args.device, required_methods)
            report["functional_commands"].append(receipt)
            write_json(output/"summary.json", report)
            if receipt["status"] != "passed" or receipt["functional_evidence"]["status"] != "passed":
                break
        legacy_output = output/"native-fft.json"
        remaining = deadline-time.monotonic()
        functional_failed = any(row["status"] != "passed" or row["functional_evidence"]["status"] != "passed"
                                for row in report["functional_commands"])
        if remaining > 1 and not functional_failed:
            command = [sys.executable, str(ROOT/"tools/triad-performance.py"), "--output", str(legacy_output),
                       "--binary", str(binary), "--reference-binary", str(reference_binary), "--candidate-root", str(candidate),
                       "--baseline-ref", args.baseline_ref, "--device", args.device, "--_backend-worker", args.device,
                       "--profile", "ci", "--validation-mode", "reuse", "--anchor-directory", str(args.anchor_directory.resolve()),
                       "--threads", str(args.threads), "--native-threads", str(args.native_threads), "--budget-seconds", str(remaining),
                       "--stopping-rule", args.stopping_rule, "--regression-profile", args.regression_profile]
            if args.hardware_baseline:
                command += ["--hardware-baseline", str(args.hardware_baseline.resolve())]
            if args.fixture_cache:
                command += ["--fixture-cache", str(args.fixture_cache.resolve())]
            for name in args.native_direct_case:
                command += ["--native-direct-case", name]
            if route_plan["identical_fft_case_ids"]:
                for name in route_plan["legacy_case_ids"]:
                    command += ["--case", name]
            report["children"]["native_fft"] = execute(command, cwd=ROOT, directory=output/"native-fft-process", deadline=deadline, env=env)
            if legacy_output.exists():
                legacy = read_json(legacy_output)
                report["errors"].extend("native_fft: " + str(error) for error in legacy.get("failures", []))
        write_json(output/"summary.json", report)
        remaining = deadline-time.monotonic()
        if remaining > 1 and not functional_failed and route_plan["identical_fft_case_ids"]:
            command = [sys.executable, str(ROOT/"tools/triad_perf_identical_fft.py"), "--output", str(output/"identical-fft"),
                       "--source-root", str(candidate), "--reference-root", str(reference), "--binary", str(binary),
                       "--fixture-cache", str(args.fixture_cache.resolve()), "--device", args.device, "--precision", "auto",
                       "--threads", str(args.threads), "--mode", "reuse",
                       "--anchor-directory", str(args.identical_anchor_directory.resolve()), "--budget-seconds", str(remaining),
                       "--stopping-rule", args.stopping_rule, "--regression-profile", args.regression_profile]
            for name in route_plan["identical_fft_case_ids"]:
                command += ["--case", name]
            report["children"]["identical_fft"] = execute(command, cwd=ROOT, directory=output/"identical-fft-process", deadline=deadline, env=env)
            if (output/"identical-fft/summary.json").exists():
                report["identical_fft_summary"] = read_json(output/"identical-fft/summary.json")
            for name in route_plan["identical_fft_case_ids"]:
                path = output/"identical-fft"/name/"result.json"
                failure_path = path.with_name("failure.json")
                if path.exists():
                    identical[name] = read_json(path)
                elif failure_path.exists():
                    report["errors"].append(f"identical FFT {name}: {read_json(failure_path).get('error')}")
            write_json(output/"summary.json", report)
        remaining = deadline-time.monotonic()
        if remaining > 1 and not functional_failed:
            command = [sys.executable, str(family_driver), "--output", str(output/"sampled"),
                       "--candidate-root", str(candidate), "--reference-root", str(reference), "--binary", str(binary),
                       "--reference-binary", str(reference_binary), "--device", args.device, "--precision", args.precision,
                       "--threads", str(args.threads), "--native-threads", str(args.native_threads), "--mode", args.family_mode,
                       "--budget-seconds", str(remaining), "--stopping-rule", args.stopping_rule,
                       "--regression-profile", args.regression_profile]
            if family_anchors:
                command += ["--anchor-directory", str(family_anchors)]
            for name in applicable:
                command += ["--case", name]
            report["children"]["sampled"] = execute(command, cwd=ROOT, directory=output/"sampled-process", deadline=deadline, env=env)
            if (output/"sampled/summary.json").exists():
                report["sampled_summary"] = read_json(output/"sampled/summary.json")
                for name, row in report["sampled_summary"].get("case_status", {}).items():
                    if row.get("status") == "error":
                        report["errors"].append(f"sampled {name}: {row.get('detail', 'unspecified child error')}")
            for name in applicable:
                path = output/"sampled"/name/"result.json"
                if path.exists():
                    sampled[name] = read_json(path)
        for spec in extensions:
            remaining = deadline-time.monotonic()
            command = [sys.executable, str(spec["driver"]), *spec["arguments"], "--output", str(spec["output"]),
                       "--budget-seconds", str(max(0., remaining)), "--regression-profile", args.regression_profile]
            receipt = {"context": spec["context"], "report_path": spec["context"]["report_path"]}
            report["extensions"][spec["id"]] = receipt
            if functional_failed:
                receipt["validation"] = {"valid": False, "status": "unqualified", "gap_ids": [],
                                         "errors": ["core functional checks did not pass; extension was not executed"]}
                continue
            receipt["command"] = execute(command, cwd=ROOT, directory=output/(spec["id"]+"-process"), deadline=deadline, env=env)
            path = Path(receipt["report_path"])
            if path.is_file():
                receipt["report_sha256"] = digest(path)
            receipt["validation"] = validate_extension_report(spec)
            if receipt["command"].get("exit_code") not in (0, 2, None):
                report["errors"].append(f"{spec['id']} extension process failed: {receipt['command'].get('exit_code')}")
            write_json(output/"summary.json", report)
        if source_identity(candidate, matrix, families) != identity:
            report["errors"].append("candidate runtime/header/test source changed during the suite")
        if any(digest(ROOT/name) != value for name, value in protocol.items()):
            report["errors"].append("measurement protocol changed during the suite")
        if digest(binary) != report["native_binary_sha256"]["candidate"] or digest(reference_binary) != report["native_binary_sha256"]["reference"]:
            report["errors"].append("native executable changed during the suite")
        if any(digest(path) != value for path, value in extension_binaries.items()):
            report["errors"].append("extension executable changed during the suite")
        if report["fixture_cache"]["status"] == "retained_link_verified" and Path(report["fixture_cache"]["legacy_alias"]).resolve(strict=True) != Path(report["fixture_cache"]["target"]):
            report["errors"].append("retained fixture-cache link changed during the suite")
    except (OSError, ValueError, KeyError, TypeError, ImportError, AttributeError) as error:
        report["errors"].append(f"{type(error).__name__}: {error}")
    finally:
        report.update(summarize(report, legacy, sampled, applicable, identical))
        report["wall_seconds"] = time.perf_counter()-start
        report["budget_exhausted"] = time.monotonic() >= deadline
        report["computational_savings"] = {"status": "not_established_by_this_run",
            "reason": "No matched direct-testing campaign at comparable assurance was executed; summed predicted full times are not that counterfactual."}
        write_json(output/"summary.json", report)
    print(f"Triad complete routine: {report['status']}; {report['wall_seconds']:.2f}s; {output/'summary.json'}")
    return report["exit_code"]


if __name__ == "__main__":
    try:
        raise SystemExit(method_cli(sys.argv[1:]) if len(sys.argv) > 1 and sys.argv[1] in ("assess-methods", "freeze-family-totals") else main())
    except (OSError, ValueError) as error:
        print(f"triad_perf_complete: {error}", file=sys.stderr)
        raise SystemExit(2)
