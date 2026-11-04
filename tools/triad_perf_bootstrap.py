#!/usr/bin/env python3
"""Explicit CPU qualification producer; normal regression never invokes this.

Use a new output directory. Each artifact is replay-validated independently
before publication to the requested cache directories. A later incomplete
qualification does not invalidate earlier qualified records. This command is
not a complete-suite pass, and does not publish releases or run ordinary tests.
"""
from __future__ import annotations

import argparse
import hashlib
import math
from pathlib import Path
import re
import shutil
import sys
import time

import triad_perf_complete as complete

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "triad-performance-bootstrap-v1"
CLI_RECIPE = ROOT/"docs/validation-data/triad-v3/2026-11-03-performance-complete/coverage/native/followup-001/cli-modes.json"


def snapshot(candidate, reference, binaries):
    """Capture declared runtime/build/test inputs plus qualification protocols."""
    matrix = complete.read_json(ROOT/"tools/triad-performance-coverage.json")
    families = complete.read_json(ROOT/"tools/triad-performance-families.json")
    return {"candidate": complete.source_identity(candidate, matrix, families),
            "reference": {str(p.relative_to(reference)): complete.digest(p) for pattern in
                          ("src/triad*", "src/channel_*", "include/datapump/triad*", "tools/triad*.py")
                          for p in reference.glob(pattern) if p.is_file()},
            "binaries": {name: complete.digest(path) for name, path in binaries.items()},
            "protocols": {str(p.relative_to(ROOT)): complete.digest(p) for pattern in
                          ("tools/triad_perf*.py", "tools/triad-performance*.json", "tools/triad-performance.py")
                          for p in ROOT.glob(pattern)}, "cli_recipe": complete.digest(CLI_RECIPE)}


def coverage_valid(path, candidate, kind):
    matrix = complete.read_json(ROOT/"tools/triad-performance-coverage.json")
    expected = matrix["coverage_reports"][kind]["source_files"]
    dependencies = complete.coverage_dependencies(candidate, matrix, expected, kind)
    receipt = complete.check_coverage(path, candidate, expected, dependencies)
    if (receipt["historical_applicability"] != "declared_source_and_dependency_match" or
            receipt["missing_expected_sources"]):
        raise ValueError(kind+" coverage source/dependency/counter validation failed")
    raw = complete.read_json(path)
    if kind == "python":
        if raw.get("test_exit_code") != 0 or not raw.get("test_runs") or any(
                row.get("exit_code") != 0 or row.get("reported_checks", 0) <= 0 for row in raw["test_runs"]):
            raise ValueError("Python coverage checks were incomplete or failed")
    else:
        guard = raw.get("header_counter_applicability", {})
        if not all(guard.get(key) is True for key in (
                "prebuild_postcapture_header_inventory_match",
                "prebuild_postcapture_declared_dependency_inventory_match",
                "counter_translation_units_built_after_guard")):
            raise ValueError("native coverage lacks its before-build dependency guard")
        if "-fprofile-update=atomic" not in raw.get("compiler", {}).get("coverage_flags", {}).get("cxx", ""):
            raise ValueError("native coverage did not use atomic counters")
    return receipt


def fft_workload_valid(identity, row, configuration, manifest, case, limits):
    """Bind an internally valid proof to the selected current full workload."""
    import numpy as np
    from scipy import fft
    import triad_perf_cases as fixtures

    samples, rate = int(manifest["sample_count"]), float(manifest["sample_rate"])
    bounds = configuration["initial_arrival_bounds_seconds"]
    maximum = int(np.ceil(np.longdouble(float(bounds[1])-float(bounds[0]))*np.longdouble(rate)))
    frequencies = fixtures._frequency_grid(configuration, samples, rate, limits)
    precision = identity.get("precision")
    expected_geometry = {"sample_count": samples, "sample_rate": rate, "nfft": fft.next_fast_len(2*samples-1),
        "lags": maximum+1, "lag_first_sample": 0, "lag_last_sample": maximum, "lag_step_samples": 1,
        "group_tile": case.get("group_tile", 512), "precision": precision, "device": "cpu"}
    observed = row.get("configuration", {})
    qualification = row.get("precision_qualification", {})
    checks = qualification.get("alternatives", {}).get(precision, {}).get("checks", [])
    measured = row.get("measured_bank_numerical_checks", [])

    def agreement(check):
        error, allowed = check.get("maximum_absolute_score_error"), check.get("allowed_score_error")
        return (check.get("eligible") is True and not check.get("reasons") and
                isinstance(error, (int, float)) and math.isfinite(error) and
                isinstance(allowed, (int, float)) and math.isfinite(allowed) and 0 <= error <= allowed)

    return (precision in ("fp32", "fp64") and identity.get("device") == "cpu" and
        identity.get("frequency_offsets_hz") == frequencies and identity.get("geometry") == expected_geometry and
        row.get("full_cfo_count") == len(frequencies) and row.get("precision") == precision and
        observed.get("native_configure") == configuration and observed.get("sample_count") == samples and
        observed.get("sample_rate_hz") == rate and observed.get("lag_group_count") == maximum+1 and
        qualification.get("selected") == precision and qualification.get("selection_frozen_before_prediction_measurements") is True and
        qualification.get("qualification_cfo_count") == min(3, len(frequencies)) and len(checks) == 3 and
        all(agreement(check) for check in checks) and bool(measured) and
        row.get("coverage", {}).get("measured_bank_numerical_checks") == len(measured) and
        row.get("coverage", {}).get("full_sample_timing_geometry") == expected_geometry and
        all(isinstance(check.get("cfo_count"), int) and 1 <= check["cfo_count"] <= len(frequencies) and
            agreement(check.get("agreement", {})) for check in measured) and
        any(check["cfo_count"] == len(frequencies) for check in measured))


def fft_record_valid(value, row, report, args, configuration):
    """Replay proof and current captured worker, hardware, source and I/Q identities."""
    import triad_perf_cases as fixtures
    import triad_perf_fft_trace as fft
    from triad_perf_hardware import discover_hardware

    identity = row.get("anchors", {}).get("identity", {})
    if not fft.anchor_valid(value, identity) or identity.get("case") != row.get("id"):
        return False
    if identity.get("performance_environment") != discover_hardware("cpu", args.threads):
        return False
    if identity.get("performance_environment") != report.get("hardware_profile", {}).get("hardware"):
        return False
    for side, root in (("candidate", args.candidate_root), ("reference", args.reference_root)):
        observed = report.get("worker_startup", {}).get(side, {}).get("identity", {})
        fields = ("source_hashes", "python", "numpy", "torch", "threads", "tf32_allowed")
        if identity.get("worker_implementations", {}).get(side) != {key: observed.get(key) for key in fields}:
            return False
        hashes = observed.get("source_hashes", {})
        if not hashes or any(complete.digest(root/"tools"/name) != value for name, value in hashes.items()):
            return False
    config = fixtures._config()
    case = next(case for case in fixtures.scenarios("ci") if case["id"] == row["id"])
    _, selected = fixtures._plan(case, config)
    sources = fixtures._source_hashes(args.candidate_root, selected["path"])
    native_args = fixtures._native_argv(args.binary, case, selected, Path("{WORKSPACE}"), "export-sampled", config["limits"])
    fixture_identity = {"schema": config["schema"], "binary_sha256": complete.digest(args.binary),
        "native_args": fixtures._normalize_args(native_args), "source_hashes": sources,
        "source_case": selected["row"]["case"], "fixture_id": case["id"]}
    key = fixtures._json_hash(fixture_identity)
    metadata = row.get("source_bound_metadata", {})
    if metadata.get("cache_key") != key or metadata.get("source_hashes") != sources:
        return False
    cache = Path(metadata["cache_manifest"]).parent
    hashes = fixtures._valid_cache(cache, key, sources)
    return (hashes is not None and hashes == row.get("fixture_hashes") == identity.get("input_sha256") and
            fft_workload_valid(identity, row, configuration, complete.read_json(cache/"workspace"/"manifest.json"), case, config["limits"]))


class Bootstrap:
    def __init__(self, args):
        self.args = args
        self.started = time.perf_counter()
        self.deadline = time.monotonic()+args.budget_seconds
        self.env = complete.child_environment(args.threads)
        self.report = {"schema": SCHEMA, "qualification_only": True, "complete_suite_claim": False,
            "budget_seconds": args.budget_seconds, "commands": [], "phases": {}, "artifacts": [],
            "errors": [], "required_cache_categories": ["anchors", "coverage"] + (["lifecycle"] if args.qualify_lifecycle_model else []),
            "native_direct_exclusions": list(args.native_direct_case),
            "lifecycle_scope": "explicit modeled qualification" if args.qualify_lifecycle_model else
                "CPU routine uses direct full lifecycle work; no forecast qualification requested",
            "cache_ready": {"anchors": False, "coverage": False, "lifecycle": False}}
        self.initial = snapshot(args.candidate_root, args.reference_root,
                                {"candidate": args.binary, "reference": args.reference_binary})
        self.report["source_binding"] = self.initial

    def save(self):
        complete.write_json(self.args.output/"summary.json", self.report)

    def run(self, label, command, cwd=None):
        receipt = complete.execute(command, cwd=cwd or ROOT, directory=self.args.output/"process"/label,
                                   deadline=self.deadline, env=self.env)
        self.report["commands"].append({"label": label, **receipt})
        self.save()
        return receipt

    def checked(self, label, command, cwd=None):
        receipt = self.run(label, command, cwd)
        if receipt["status"] != "passed":
            raise ValueError(label+": command did not complete successfully")
        return receipt

    def publish(self, path, destination, category, validation):
        path, destination = Path(path), Path(destination)
        if destination.is_symlink():
            raise ValueError("refusing a symlinked cache artifact destination")
        if destination.exists() and complete.digest(destination) != complete.digest(path):
            previous = self.args.output/"previous-cache-inputs"/category/destination.name
            previous.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(destination, previous)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)
        self.report["artifacts"].append({"category": category, "path": str(destination),
            "sha256": complete.digest(destination), "source_report": str(path), "validation": validation})
        self.save()

    def native_coverage(self):
        args = self.args
        work = args.output/"native-coverage"
        work.mkdir()
        build = args.candidate_root/"build"/("triad-bootstrap-cov-"+hashlib.sha256(str(args.output).encode()).hexdigest()[:12])
        if build.exists():
            raise ValueError("native coverage requires a fresh instrumented build directory")
        collector = ROOT/"tools/triad_perf_coverage.py"
        guard = work/"before-build.json"
        self.checked("coverage-native-guard", [sys.executable, str(collector), "--source-root", str(args.candidate_root),
                                               "--write-header-guard", str(guard)])
        self.checked("coverage-native-configure", [str(args.candidate_root/"build.sh"), "configure", "--cli",
            "--build-dir", str(build), "--", "-DBUILD_TESTING=ON", "-DDATAPUMP_COMPILER_CACHE=OFF",
            "-DCMAKE_CXX_FLAGS=--coverage -fprofile-update=atomic", "-DCMAKE_EXE_LINKER_FLAGS=--coverage",
            "-DCMAKE_SHARED_LINKER_FLAGS=--coverage", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"], args.candidate_root)
        self.checked("coverage-native-build", ["cmake", "--build", str(build), "--config", "Release", "--target", "triad_long",
            "test_triad_statistics", "test_triad_v3_source", "test_triad_v3_channel", "test_triad_v3_receiver", "--parallel", "2"])
        ctest = ["ctest", "--test-dir", str(build), "-R", complete.NATIVE_TESTS]
        inventory = self.checked("coverage-native-inventory", [*ctest, "-N", "--show-only=json-v1"])
        if complete.native_inventory(inventory)["status"] != "passed":
            raise ValueError("native coverage test inventory was incomplete")
        tests = self.checked("coverage-native-tests", [*ctest, "--output-on-failure", "--no-tests=error", "-j", "1"])
        if complete.functional_evidence(ctest, tests, "cpu")["status"] != "passed":
            raise ValueError("not all native coverage checks passed")
        recipe = complete.read_json(CLI_RECIPE)
        if set(recipe) != {"plan", "export", "export-sampled", "export-stats", "export-group-stats"}:
            raise ValueError("native coverage CLI witness inventory changed")
        for mode, row in recipe.items():
            command = [str(build/"triad_long"), *row["command"][1:]]
            if "--output" in command:
                command[command.index("--output")+1] = str(work/mode)
            self.checked("coverage-cli-"+mode, command)
        path = work/"native.json"
        command = [sys.executable, str(collector), "--source-root", str(args.candidate_root), "--build-dir", str(build),
            "--raw-dir", str(work/"raw"), "--output", str(path), "--header-guard-check", str(guard),
            "--executed-check", "four exact native CTests"]
        for mode in recipe:
            command += ["--executed-check", "triad_long --mode "+mode]
        self.checked("coverage-native-collect", command)
        self.publish(path, args.coverage_directory/"native.json", "coverage", coverage_valid(path, args.candidate_root, "native"))

    def python_coverage(self):
        args = self.args
        path = args.output/"python-coverage"/"python.json"
        self.checked("coverage-python", [sys.executable, str(ROOT/"tools/triad_perf_python_coverage.py"),
            "--repo-root", str(args.candidate_root), "--coverage-matrix", str(args.candidate_root/"tools/triad-performance-coverage.json"),
            "--raw-dir", str(path.parent/"raw"), "--output", str(path)])
        self.publish(path, args.coverage_directory/"python.json", "coverage", coverage_valid(path, args.candidate_root, "python"))

    def native_fft(self):
        import triad_perf_native as native
        import triad_perf_cases as fixtures
        args = self.args
        path = args.output/"native-fft.json"
        anchors = args.output/"new-native-fft-anchors"
        cases = [case for case in native.scenarios("ci") if case["id"] not in args.native_direct_case]
        fft_ids = complete.read_json(ROOT/"tools/triad-performance-cases.json")["profiles"]["ci"]
        command = [sys.executable, str(ROOT/"tools/triad-performance.py"), "--_backend-worker", "cpu", "--device", "cpu",
            "--profile", "ci", "--validation-mode", "qualify", "--stopping-rule", "precision", "--output", str(path),
            "--binary", str(args.binary), "--reference-binary", str(args.reference_binary), "--candidate-root", str(args.candidate_root),
            "--baseline-ref", args.baseline_ref, "--anchor-directory", str(anchors), "--threads", str(args.threads),
            "--native-threads", str(args.native_threads), "--fixture-cache", str(args.fixture_cache),
            "--budget-seconds", str(max(.001, self.deadline-time.monotonic()))]
        for name in [case["id"] for case in cases]+fft_ids:
            command += ["--case", name]
        self.run("qualify-native-fft", command)
        report = complete.read_json(path)
        rows = {row["id"]: row for row in report.get("cases", [])}
        rejected = []
        for case in cases:
            source = anchors/(case["id"]+".native-anchor.json")
            if not source.is_file():
                continue
            value = complete.read_json(source)
            identities = {name: native.identity(binary, case, args.native_threads)
                          for name, binary in (("candidate", args.binary), ("reference", args.reference_binary))}
            if native.anchor_compatible(value, identities, case) and rows.get(case["id"], {}).get("anchors") == value:
                self.publish(source, args.anchor_directory/source.name, "anchors", {"id": case["id"], "proof": "native.anchor_compatible"})
            else:
                rejected.append(case["id"])
        for name in fft_ids:
            row = rows.get(name, {})
            declared = row.get("anchors", {}).get("path")
            if declared:
                source = Path(declared).resolve()
                if not source.is_relative_to(anchors.resolve()) or not source.is_file():
                    rejected.append(name)
                    continue
                config = fixtures._config()
                case = next(case for case in fixtures.scenarios("ci") if case["id"] == name)
                _, selected = fixtures._plan(case, config)
                command = fixtures._native_argv(args.binary, case, selected, Path("unused"), "configure", config["limits"])
                configured = self.checked("recheck-fft-work-"+name, command)
                configuration = complete.read_json(Path(configured["stdout"]))["configuration"]
                if fft_record_valid(complete.read_json(source), row, report, args, configuration):
                    self.publish(source, args.anchor_directory/"cpu"/source.name, "anchors", {"id": name, "proof": "FFT raw proof and current fixture/worker/hardware bindings"})
                else:
                    rejected.append(name)
        if report.get("failures"):
            raise ValueError("native/FFT qualification incomplete: " + "; ".join(map(str, report["failures"])))
        if rejected:
            raise ValueError("native/FFT proof or current full workload binding rejected: " + ", ".join(rejected))
        missing = [name for name in [case["id"] for case in cases]+fft_ids if not rows.get(name, {}).get("prediction_valid")]
        if missing:
            raise ValueError("native/FFT targets without qualified prediction/direct evidence: " + ", ".join(missing))

    def lifecycle(self):
        import triad_perf_lifecycle as lifecycle
        args = self.args
        output = args.output/"lifecycle"
        anchors = args.output/"new-lifecycle-anchors"
        command = [sys.executable, str(ROOT/"tools/triad_perf_lifecycle.py"), "--mode", "qualify", "--device", "cpu",
            "--case", "mixed-cpu",
            "--candidate-root", str(args.candidate_root), "--reference-root", str(args.reference_root),
            "--binary", str(args.binary), "--reference-binary", str(args.reference_binary), "--precision", "fp32",
            "--threads", str(args.threads), "--native-threads", str(args.native_threads), "--regression-profile", "ci",
            "--anchor-directory", str(anchors), "--output", str(output),
            "--budget-seconds", str(max(.001, self.deadline-time.monotonic()))]
        self.run("qualify-lifecycle", command)
        report = complete.read_json(output/"summary.json")
        config, families, known = lifecycle.load_manifest()
        for case in config["cases"]:
            if case["device"] != "cpu" or not case["routine"]:
                continue
            source = lifecycle._anchor_path(anchors, case["id"])
            if not source.is_file():
                continue
            value = complete.read_json(source)
            roots = {"candidate": args.candidate_root, "reference": args.reference_root}
            binaries = {"candidate": args.binary, "reference": args.reference_binary}
            current = lifecycle.static_binding(roots, binaries, case, families, known,
                device="cpu", precision="fp32", threads=args.threads, native_threads=args.native_threads)
            valid = lifecycle._validate_anchor(value) if case["kind"] == "mixed" else lifecycle._validate_cold_anchor(value)
            matching = [row for row in report.get("cases", []) if row.get("id") == case["id"]]
            if (valid and value.get("static_binding") == current and len(matching) == 1 and
                    matching[0].get("static_binding") == current and matching[0].get("prediction_valid") is True and
                    matching[0].get("comparison", {}).get("gate") == "pass"):
                self.publish(source, args.lifecycle_anchor_directory/source.name, "lifecycle",
                             {"id": case["id"], "proof": "raw lifecycle proof and current static source/build binding"})
        if report.get("errors"):
            raise ValueError("lifecycle qualification incomplete: " + "; ".join(map(str, report["errors"])))

    def finish(self):
        args = self.args
        unchanged = snapshot(args.candidate_root, args.reference_root,
                             {"candidate": args.binary, "reference": args.reference_binary}) == self.initial
        self.report["source_binding_unchanged"] = unchanged
        if not unchanged:
            self.report["errors"].append("source/build/protocol inputs changed during qualification; no cache publication is eligible")
        categories = {name: [row for row in self.report["artifacts"] if row["category"] == name]
                      for name in self.report["cache_ready"]}
        self.report["cache_ready"] = {"anchors": unchanged and bool(categories["anchors"]),
            "coverage": unchanged and bool(categories["coverage"]),
            "lifecycle": unchanged and any(row["validation"].get("id") == "mixed-cpu" for row in categories["lifecycle"])}
        self.report["wall_seconds"] = time.perf_counter()-self.started
        eligible = not self.report["errors"] and all(self.report["cache_ready"][name] for name in self.report["required_cache_categories"])
        self.report["status"] = "qualified_artifacts_produced" if eligible else "partial_or_unqualified"
        self.save()
        return 0 if eligible else 2


def publication_paths(report, candidate, reference, binaries, directories):
    """Publish only the independently validated records, never a restored glob."""
    if (report.get("schema") != SCHEMA or report.get("source_binding_unchanged") is not True or
            report.get("source_binding") != snapshot(candidate, reference, binaries)):
        raise ValueError("bootstrap publication source/build/protocol binding is stale")
    result = {name: [] for name in ("anchors", "coverage", "lifecycle")}
    for row in report.get("artifacts", []):
        category = row["category"]
        path = Path(row["path"]).resolve(strict=True)
        if category not in result or not path.is_relative_to(Path(directories[category]).resolve(strict=True)):
            raise ValueError("bootstrap artifact is outside its declared cache category")
        expected_name = path.name in ("native.json", "python.json") if category == "coverage" else path.name.endswith("anchor.json")
        if not expected_name or complete.digest(path) != row.get("sha256") or not row.get("validation"):
            raise ValueError("bootstrap artifact identity or validation receipt is invalid")
        if report.get("cache_ready", {}).get(category) is True:
            result[category].append(str(path))
    return {name: sorted(set(paths)) for name, paths in result.items()}


def prepare_publication_cache(report, candidate, reference, binaries, directories, quarantine):
    """Leave only indexed proofs under the same paths used by cache restore.

    Call after the qualification controller has exited. Unindexed restored
    artifacts remain in the run evidence, outside every publication directory.
    GitHub's cache version includes its path input, so saving an exact file-list
    while restoring a directory would not produce a reusable cache version.
    """
    if report.get("status") not in ("qualified_artifacts_produced", "partial_or_unqualified"):
        raise ValueError("bootstrap controller has not finalized its publication index")
    paths = publication_paths(report, candidate, reference, binaries, directories)
    quarantine = Path(quarantine).resolve()
    roots = {name: Path(path).resolve() for name, path in directories.items()}
    if any(quarantine.is_relative_to(root) or root.is_relative_to(quarantine) for root in roots.values()):
        raise ValueError("unpublished evidence must be outside publication directories")
    moves = []
    for category, files in paths.items():
        if not files:
            continue
        root = roots[category]
        if Path(directories[category]).is_symlink():
            raise ValueError("refusing a symlinked publication directory")
        allowed = {Path(path) for path in files}
        for entry in sorted(root.rglob("*")):
            if entry.is_symlink():
                raise ValueError("publication directory contains a symlink")
            if entry.is_file() and entry.resolve() not in allowed:
                target = quarantine/category/entry.relative_to(root)
                if target.exists():
                    raise ValueError("unpublished evidence destination already exists")
                moves.append((entry, target))
    for entry, target in moves:
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(entry), str(target))
    complete.write_json(quarantine.parent/"publication-index.json", {
        "schema": SCHEMA, "validated_paths": paths,
        "retained_unpublished_paths": [str(target) for _, target in moves],
        "source_binding": report["source_binding"], "complete_suite_claim": False})
    return paths


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("candidate-root", "reference-root", "binary", "reference-binary", "output", "anchor-directory",
                 "lifecycle-anchor-directory", "coverage-directory", "fixture-cache"):
        parser.add_argument("--"+name, type=Path, required=True)
    parser.add_argument("--baseline-ref", default=complete.BASELINE)
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--native-threads", type=int, default=1)
    parser.add_argument("--budget-seconds", type=float, default=1800.)
    parser.add_argument("--qualify-lifecycle-model", action="store_true",
                        help="explicit research qualification of the CPU mixed model; normal CPU CI uses direct full work")
    parser.add_argument("--native-direct-case", action="append", default=[], metavar="CASE_ID",
                        help="exclude this explicitly direct routine native target from unused model qualification; repeatable")
    args = parser.parse_args(argv)
    if not math.isfinite(args.budget_seconds) or not 0 < args.budget_seconds <= 3600:
        parser.error("qualification budget must be in (0,3600]")
    if args.threads != 2 or args.native_threads != 1:
        parser.error("this CPU bootstrap uses the declared two-worker/one-native-thread allocation")
    if not re.fullmatch(r"[0-9a-f]{40}", args.baseline_ref):
        parser.error("baseline-ref must be a full immutable commit ID")
    native_config = complete.read_json(ROOT/"tools/triad-performance-native-cases.json")
    modeled_native = {row["id"] for row in native_config["cases"]
                      if row["id"] in native_config["profiles"]["ci"] and "training_seconds" in row}
    if len(args.native_direct_case) != len(set(args.native_direct_case)) or set(args.native_direct_case)-modeled_native:
        parser.error("native-direct-case requires unique modeled native IDs from the CI profile")
    for name, value in vars(args).items():
        if isinstance(value, Path):
            setattr(args, name, value.resolve())
    return args


def main(argv=None):
    args = parse_args(argv)
    args.output.mkdir(parents=True, exist_ok=False)
    suite = Bootstrap(args)
    suite.save()
    complete.connect_fixture_cache(args.output, args.fixture_cache)
    phases = ["native_coverage", "python_coverage", "native_fft"]
    if args.qualify_lifecycle_model:
        phases.append("lifecycle")
    for name in phases:
        print("Explicit qualification stage: "+name, flush=True)
        try:
            if time.monotonic() >= suite.deadline:
                raise TimeoutError("shared qualification deadline exhausted")
            getattr(suite, name)()
            suite.report["phases"][name] = {"status": "completed"}
        except (OSError, ValueError, KeyError, TypeError, TimeoutError) as error:
            suite.report["errors"].append(name+": "+str(error))
            suite.report["phases"][name] = {"status": "incomplete", "error": str(error)}
        suite.save()
    code = suite.finish()
    print("Qualification artifact status: "+suite.report["status"]+"; "+str(args.output/"summary.json"), flush=True)
    return code


if __name__ == "__main__":
    raise SystemExit(main())
