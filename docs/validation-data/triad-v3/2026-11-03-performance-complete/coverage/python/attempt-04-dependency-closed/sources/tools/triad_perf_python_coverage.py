#!/usr/bin/env python3
"""Measure branch and line coverage of maintained triad Python runtime code.

Coverage is collected from focused unit tests, not from performance harnesses.
The report describes Python source arcs only; it cannot observe code inside
native extensions, NumPy, Torch, or CUDA kernels.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from typing import Any, Iterable


SCHEMA_VERSION = 1
DEFAULT_MODULES = (
    "triad_gaussian_state",
    "triad_state_workspace",
    "triad_prepared_observation",
    "triad_gaussian_window",
    "triad_noise_likelihood",
    "triad_rbpf",
    "triad_phase_frequency",
    "triad_phase_optimized",
    "triad_fft_acquire",
    "triad_group_acquire",
    "triad_gaussian_prepared",
    "triad-state-study.py",
    "triad-static-frequency-study.py",
    "triad-rbpf-study.py",
    "triad-cfo-batch-study.py",
    "triad-ray-frequency-study.py",
)
DEFAULT_TESTS = (
    "test_triad_gaussian_state",
    "test_triad_prepared_observation",
    "test_triad_gaussian_window",
    "test_triad_noise_likelihood",
    "test_triad_rbpf",
    "test_triad_phase_frequency",
    "test_triad_phase_optimized",
    "test_triad_fft_acquire",
    "test_triad_group_acquire",
    "test_triad_gaussian_prepared",
    "test_triad_state_workspace",
    "test_triad_state_study",
    "test_triad_static_frequency_study",
    "test_triad_rbpf_study",
    "test_triad_cfo_batch",
    "test_triad_ray_frequency",
)
DEFAULT_EXTRA_DECLARED_DEPENDENCIES = (
    "tools/triad-noise-study.py",
    "tests/test_triad_noise_study.py",
)
LIMITATIONS = [
    "coverage.py records Python line execution and control-flow arcs; these are not a proof that every semantic condition or receiver behavior is covered.",
    "C/C++ extensions and internal NumPy, SciPy, Torch, and CUDA implementation branches are outside this Python source report.",
    "CUDA availability is recorded as environment context. A CPU-only or unavailable CUDA run does not cover CUDA execution paths.",
    "This is a focused regression measurement for the listed maintained runtime modules, not coverage of every triad Python tool or performance harness.",
]


class CoverageRunError(ValueError):
    """Invalid coverage setup or incomplete source report."""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def resolve_targets(repo_root: Path, module_names: Iterable[str]) -> dict[str, Path]:
    """Resolve dotted module names or explicit .py filenames under tools/."""
    tools_dir = (repo_root / "tools").resolve()
    resolved: dict[str, Path] = {}
    for name in module_names:
        if not name:
            raise CoverageRunError(f"invalid Python module name: {name!r}")
        if name in resolved:
            raise CoverageRunError(f"duplicate Python source: {name}")
        if name.endswith(".py"):
            relative = Path(name)
            if relative.is_absolute() or ".." in relative.parts:
                raise CoverageRunError(f"source filename must stay under tools/: {name}")
            source = tools_dir.joinpath(relative).resolve()
        else:
            if any(not part.isidentifier() for part in name.split(".")):
                raise CoverageRunError(f"invalid Python module name: {name!r}")
            source = tools_dir.joinpath(*name.split(".")).with_suffix(".py").resolve()
        if not source.is_relative_to(tools_dir):
            raise CoverageRunError(f"module escapes tools directory: {name}")
        if not source.is_file():
            raise CoverageRunError(f"missing target module {name}: {source}")
        resolved[name] = source
    if not resolved:
        raise CoverageRunError("at least one target module is required")
    return resolved


def summarize_json(document: dict[str, Any], targets: dict[str, Path], repo_root: Path) -> dict[str, Any]:
    """Map coverage.py JSON records back to exact allowlisted source files."""
    by_path: dict[Path, dict[str, Any]] = {}
    for key, value in document.get("files", {}).items():
        candidate = Path(key)
        try:
            candidate = candidate.resolve()
        except OSError:
            continue
        by_path[candidate] = value

    sources: dict[str, Any] = {}
    for module, source in targets.items():
        record = by_path.get(source.resolve())
        if record is None:
            raise CoverageRunError(f"coverage JSON has no exact source identity for {module}: {source}")
        summary = record.get("summary", {})
        lines = record.get("executed_lines", [])
        missing_lines = record.get("missing_lines", [])
        branches = record.get("executed_branches", [])
        missing_branches = record.get("missing_branches", [])
        sources[str(source.relative_to(repo_root))] = {
            "module": module,
            "sha256": sha256(source),
            "lines": {
                "executable": summary.get("num_statements", 0),
                "covered": summary.get("covered_lines", len(lines)),
                "missing": sorted(missing_lines),
            },
            "branches": {
                "executable_arcs": summary.get("num_branches", 0),
                "covered_arcs": summary.get("covered_branches", len(branches)),
                "missing_arcs": missing_branches,
            },
        }
    return sources


def selected_tests(repo_root: Path, test_names: Iterable[str]) -> list[Path]:
    tests_dir = (repo_root / "tests").resolve()
    selected = []
    for name in test_names:
        if not name or Path(name).name != name or not name.isidentifier():
            raise CoverageRunError(f"invalid test module name: {name!r}")
        path = tests_dir / f"{name}.py"
        if not path.is_file():
            raise CoverageRunError(f"missing focused test module {name}: {path}")
        selected.append(path)
    if not selected:
        raise CoverageRunError("at least one focused test module is required")
    return selected


def matrix_contract(repo_root: Path, matrix_path: Path, targets: dict[str, Path],
                    tests: list[Path], extra_dependencies: Iterable[str] = DEFAULT_EXTRA_DECLARED_DEPENDENCIES
                    ) -> dict[str, Any]:
    """Resolve the matrix dependency union used by triad_perf_complete.check_coverage."""
    matrix_path = matrix_path.resolve(strict=True)
    try:
        matrix = json.loads(matrix_path.read_text(encoding="utf-8"))
        expected_sources = set(matrix["coverage_reports"]["python"]["source_files"])
        rows = matrix["rows"]
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        raise CoverageRunError(f"invalid coverage matrix {matrix_path}: {error}") from error
    target_sources = {str(path.resolve().relative_to(repo_root.resolve())) for path in targets.values()}
    if expected_sources != target_sources:
        missing = sorted(expected_sources - target_sources)
        extra = sorted(target_sources - expected_sources)
        raise CoverageRunError(f"target source set does not match matrix Python rows; missing={missing}, extra={extra}")

    row_dependencies: set[str] = set()
    matrix_test_sources: set[str] = set()
    for row in rows:
        row_sources = set(row.get("source_files", []))
        if row_sources & expected_sources:
            row_dependencies.update(row_sources)
            for test in row.get("cheap_functional_tests", []):
                path = test.split("::", 1)[0]
                if path.startswith(("tests/", "tools/")):
                    matrix_test_sources.add(path)

    selected_test_sources = {str(path.resolve().relative_to(repo_root.resolve())) for path in tests}
    explicit_support = set(extra_dependencies)
    explicit_support_tests = {name for name in explicit_support if name.startswith("tests/")}
    declared_tests = matrix_test_sources | selected_test_sources | explicit_support_tests
    all_inputs = row_dependencies | declared_tests | explicit_support | target_sources
    matrix_relative = str(matrix_path.relative_to(repo_root.resolve()))
    all_inputs.add(matrix_relative)
    collector = Path(__file__).resolve()
    resolved: dict[str, Path] = {}
    for relative in sorted(all_inputs):
        path = (repo_root / relative).resolve()
        if not path.is_relative_to(repo_root.resolve()) or not path.is_file():
            raise CoverageRunError(f"declared dependency is missing or outside repository: {relative}")
        resolved[relative] = path
    resolved[f"collector:{collector}"] = collector
    if any(not name.startswith("tests/") for name in matrix_test_sources | selected_test_sources | explicit_support_tests):
        raise CoverageRunError("declared test source must be under tests/")
    return {
        "matrix_path": matrix_relative,
        "matrix_sha256": sha256(matrix_path),
        "expected_python_source_files": sorted(expected_sources),
        "matrix_row_dependency_files": sorted(row_dependencies),
        "matrix_test_source_files": sorted(matrix_test_sources),
        "selected_test_source_files": sorted(selected_test_sources),
        "extra_declared_dependency_files": sorted(explicit_support),
        "test_identity_files": sorted(declared_tests),
        "identity_paths": resolved,
        # The matrix hash is captured as provenance and pre/post guarded, but
        # is not an enduring dependency receipt: its status/evidence fields may
        # be updated after capture without changing the measured DSP sources.
        "dependency_paths": sorted(row_dependencies | declared_tests | explicit_support),
    }


def hash_inputs(paths: dict[str, Path]) -> dict[str, str]:
    return {name: sha256(path) for name, path in sorted(paths.items())}


def test_count(output: str) -> int:
    """Require a positive unittest count or the explicit check count of a CLI test."""
    match = re.search(r"Ran\s+(\d+)\s+tests?\s+in\s+", output)
    if match:
        return int(match.group(1))
    try:
        whole_document = json.loads(output.strip())
    except json.JSONDecodeError:
        whole_document = None
    if isinstance(whole_document, dict):
        for key in ("passed", "checks"):
            if isinstance(whole_document.get(key), int):
                return int(whole_document[key])
            if isinstance(whole_document.get(key), dict):
                return len(whole_document[key])
        if whole_document.get("scope") and any(key != "scope" for key in whole_document):
            return 1
    for line in reversed(output.splitlines()):
        try:
            document = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(document, dict):
            for key in ("passed", "checks"):
                if isinstance(document.get(key), int):
                    return int(document[key])
                if isinstance(document.get(key), dict):
                    return len(document[key])
            # A standalone JSON CLI test can report named numerical checks
            # without a summary count; require a nonempty result object.
            if document.get("scope") and any(key != "scope" for key in document):
                return 1
    return 0


def cuda_context() -> dict[str, Any]:
    try:
        import torch
    except ImportError:
        return {"torch_installed": False, "cuda_available": None}
    try:
        return {"torch_installed": True, "torch_version": torch.__version__,
                "cuda_available": bool(torch.cuda.is_available()),
                "cuda_device_count": int(torch.cuda.device_count()) if torch.cuda.is_available() else 0}
    except Exception as error:  # pragma: no cover - environment-specific backend failures
        return {"torch_installed": True, "torch_version": torch.__version__,
                "cuda_available": False, "cuda_probe_error": f"{type(error).__name__}: {error}"}


def run_coverage(repo_root: Path, raw_dir: Path, targets: dict[str, Path], tests: list[Path],
                 per_test_timeout: float = 60., matrix_path: Path | None = None,
                 extra_dependencies: Iterable[str] = DEFAULT_EXTRA_DECLARED_DEPENDENCIES
                 ) -> tuple[dict[str, Any], int, str]:
    try:
        import coverage
    except ImportError as error:
        raise CoverageRunError("coverage.py v7 is required in the selected Python environment") from error
    raw_dir.mkdir(parents=True, exist_ok=True)
    data_file = raw_dir / ".coverage"
    json_file = raw_dir / "coverage.json"
    # File includes avoid coverage.py importing module-name source targets
    # during setup, which would leave those modules outside the measured span.
    # Each selected test file is run as __main__ in an isolated child process.
    # Some maintained checks are CLI assertions rather than unittest suites.
    if data_file.exists():
        data_file.unlink()
    contract = None
    identity_pre = {}
    if matrix_path is not None:
        contract = matrix_contract(repo_root, matrix_path, targets, tests, extra_dependencies)
        identity_pre = hash_inputs(contract["identity_paths"])
    include = ",".join(str(path) for path in targets.values())
    child_env = os.environ.copy()
    extra_path = [str((repo_root / "tools").resolve()), str((repo_root / "tests").resolve()), str(repo_root)]
    child_env["PYTHONPATH"] = os.pathsep.join(extra_path + ([child_env["PYTHONPATH"]] if child_env.get("PYTHONPATH") else []))
    started = time.monotonic()
    runs = []
    test_exit = 0
    for index, test_path in enumerate(tests):
        command = [sys.executable, "-m", "coverage", "run", "--branch",
                   f"--data-file={data_file}", f"--include={include}"]
        if index:
            command.append("--append")
        command.append(str(test_path))
        try:
            completed = subprocess.run(command, cwd=repo_root, text=True,
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       timeout=per_test_timeout, env=child_env)
            output = completed.stdout
            status = completed.returncode
            timeout = False
        except subprocess.TimeoutExpired as error:
            output = (error.stdout or b"").decode(errors="replace") if isinstance(error.stdout, bytes) else (error.stdout or "")
            status = 124
            timeout = True
        count = test_count(output)
        if status != 0 or count <= 0:
            test_exit = 1
        runs.append({"test": str(test_path.relative_to(repo_root)), "exit_code": status,
                     "timeout": timeout, "timeout_seconds": per_test_timeout,
                     "reported_checks": count, "test_source_sha256": sha256(test_path),
                     "command": command, "output": output})
    cov = coverage.Coverage(data_file=str(data_file), branch=True,
                            include=[str(path) for path in targets.values()], config_file=False)
    cov.load()
    cov.json_report(outfile=str(json_file), pretty_print=True)
    document = json.loads(json_file.read_text(encoding="utf-8"))
    sources = summarize_json(document, targets, repo_root)
    identities = {}
    identity_status = "not_collected"
    identity_mismatches = []
    dependency_sha256 = {}
    if contract is not None:
        identity_post = hash_inputs(contract["identity_paths"])
        identity_mismatches = [name for name, before in identity_pre.items()
                               if identity_post.get(name) != before]
        identity_status = "stable" if not identity_mismatches else "changed_during_capture"
        dependency_sha256 = {name: identity_pre[name] for name in contract["dependency_paths"]}
        identities = {
            "status": identity_status,
            "capture_scope": "declared matrix row source union, matrix test source paths, selected test source paths, explicit support fixture/dependencies, Python runtime rows, and collector source; not discovered dynamic import closure",
            "matrix_path": contract["matrix_path"],
            "matrix_sha256_pre": identity_pre[contract["matrix_path"]],
            "matrix_sha256_post": identity_post[contract["matrix_path"]],
            "expected_python_source_files": contract["expected_python_source_files"],
            "matrix_row_dependency_files": contract["matrix_row_dependency_files"],
            "matrix_test_source_files": contract["matrix_test_source_files"],
            "selected_test_source_files": contract["selected_test_source_files"],
            "extra_declared_dependency_files": contract["extra_declared_dependency_files"],
            "test_identity_files": contract["test_identity_files"],
            "test_source_sha256_pre": {name: identity_pre[name] for name in contract["test_identity_files"]},
            "test_source_sha256_post": {name: identity_post[name] for name in contract["test_identity_files"]},
            "pre_sha256": identity_pre,
            "post_sha256": identity_post,
            "changed_inputs": identity_mismatches,
            "dependency_sha256": dependency_sha256,
        }
        if identity_mismatches:
            test_exit = 2
        for name, source in sources.items():
            if identity_pre.get(name) != source["sha256"]:
                identity_mismatches.append(name)
        if identity_mismatches:
            identities["status"] = "changed_during_capture"
            identities["changed_inputs"] = sorted(set(identity_mismatches))
            test_exit = 2
    return ({"coverage_version": coverage.__version__, "elapsed_seconds": time.monotonic() - started,
             "test_exit_code": test_exit, "test_runs": runs, "sources": sources,
             "input_identity": identities},
            test_exit, str(json_file))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--coverage-matrix", type=Path,
                        help="declared Python coverage matrix used to capture row dependency identities")
    parser.add_argument("--raw-dir", type=Path, required=True,
                        help="retain coverage.py data and raw JSON here")
    parser.add_argument("--output", type=Path, required=True, help="write the normalized summary JSON")
    parser.add_argument("--module", action="append", dest="modules",
                        help="target module name or .py filename under tools/ (repeatable)")
    parser.add_argument("--test", action="append", dest="tests",
                        help="unittest module under tests/ (repeatable; defaults to focused triad suite)")
    parser.add_argument("--per-test-timeout", type=float, default=60.,
                        help="maximum seconds for each test file (default: 60)")
    args = parser.parse_args(argv)
    try:
        repo_root = args.repo_root.resolve(strict=True)
        if not (args.per_test_timeout > 0):
            raise CoverageRunError("per-test timeout must be positive")
        targets = resolve_targets(repo_root, args.modules or DEFAULT_MODULES)
        tests = selected_tests(repo_root, args.tests or DEFAULT_TESTS)
        matrix_path = args.coverage_matrix or (repo_root / "tools" / "triad-performance-coverage.json")
        result, test_exit, raw_json = run_coverage(repo_root, args.raw_dir.resolve(), targets, tests,
                                                   args.per_test_timeout, matrix_path)
        try:
            revision = subprocess.run(["git", "-C", str(repo_root), "rev-parse", "HEAD"],
                                      check=True, text=True, stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            revision = None
        payload = {
            "schema": "datapump.triad.python-branch-coverage",
            "schema_version": SCHEMA_VERSION,
            "scope": "focused isolated triad Python runtime modules exercised by selected regression tests",
            "source_root": str(repo_root),
            "source_revision": revision,
            "coverage_interpretation": {
                "line_metric": "coverage.py executable Python source statements",
                "branch_metric": "coverage.py executed and missing Python control-flow arcs",
                "limitations": LIMITATIONS,
            },
            "cuda_environment": cuda_context(),
            "tests": [str(path.relative_to(repo_root)) for path in tests],
            "test_exit_code": test_exit,
            "test_runs": result["test_runs"],
            "input_identity": result["input_identity"],
            "dependency_sha256": result["input_identity"].get("dependency_sha256", {}),
            "coverage_version": result["coverage_version"],
            "elapsed_seconds": result["elapsed_seconds"],
            "raw_coverage_json": os.path.relpath(raw_json, args.output.parent.resolve()),
            "sources": result["sources"],
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except (OSError, ValueError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"triad_perf_python_coverage.py: {error}", file=sys.stderr)
        return 2
    print(args.output)
    return test_exit


if __name__ == "__main__":
    raise SystemExit(main())
