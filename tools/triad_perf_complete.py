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
    return {"outcomes": outcomes, "timing_cases": timing,
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
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"triad_perf_complete: {error}", file=sys.stderr)
        raise SystemExit(2)
