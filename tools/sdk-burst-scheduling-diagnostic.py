#!/usr/bin/env python3
"""Run matched diagnostic trials for the original SDK Symbol Burst fixtures.

This is a scheduling diagnostic only. Its CTest commands retain the registered
no-argument fixtures and their 3600 second properties; results never qualify a
release or change test budgets.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import resource
import signal
import subprocess
import sys
import time
import xml.etree.ElementTree as ET


BASELINE = "1cda521e3b5938fa7c9a62cd70c5f887ecc8d373"
SDK_MANIFEST_SHA256 = "4fdcc4b8d57e0a67eeb2ce7fac70667b75f40f01215105de95b771c77c5303c3"
TRIAL_WALL_SECONDS = 900
APP_PATHS = ("src", "include", "tests", "cmake", "CMakeLists.txt")
SOURCE_FILES = ("tests/test_symbol_burst_live.cpp", "tests/test_weak_signal.cpp",
                "src/live.cpp", "src/search_parallel.cpp",
                "tools/observe-native-test.py", "tools/sdk-burst-scheduling-diagnostic.py",
                ".github/workflows/sdk-application.yml")
TRIALS = (
    ("alone-observed-1", "^(symbol_burst_live)$", 1, True),
    ("paired-observed-1", "^(symbol_burst_live|weak_signal)$", 2, True),
    ("alone-observed-2", "^(symbol_burst_live)$", 1, True),
    ("paired-observed-2", "^(symbol_burst_live|weak_signal)$", 2, True),
    ("alone-unobserved", "^(symbol_burst_live)$", 1, False),
    ("paired-unobserved", "^(symbol_burst_live|weak_signal)$", 2, False),
)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def is_elf(path):
    with path.open("rb") as stream:
        return stream.read(4) == b"\x7fELF"


def checked(command, cwd, timeout=30):
    if command[0] == "git":
        command = ["git", "-c", f"safe.directory={cwd}", *command[1:]]
    return subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=True, timeout=timeout).stdout.strip()


def capture(command, cwd, limit=12000):
    try:
        result = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, check=False, timeout=15)
        return {"command": command, "exit_code": result.returncode,
                "output": result.stdout[-limit:]}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"command": command, "error": f"{type(error).__name__}: {error}"}


def parse_cache(path):
    values = {}
    for line in path.read_text(errors="replace").splitlines():
        if "=" in line and not line.startswith(("#", "//")):
            key, value = line.split("=", 1)
            values[key.split(":", 1)[0]] = value
    return values


def validate_inventory(data, build):
    tests = data.get("tests", [])
    expected = {"symbol_burst_live": build / "test_symbol_burst_live",
                "weak_signal": build / "test_weak_signal"}
    if {item.get("name") for item in tests} != set(expected) or len(tests) != 2:
        raise RuntimeError(f"CTest inventory is not the exact two fixtures: {tests!r}")
    for item in tests:
        name = item["name"]
        command = item.get("command")
        if command != [str(expected[name])]:
            raise RuntimeError(f"{name} registration must remain the original no-argument ELF: {command!r}")
        props = {p.get("name"): p.get("value") for p in item.get("properties", [])}
        if props.get("TIMEOUT") is not None and str(props["TIMEOUT"]) != "3600":
            raise RuntimeError(f"{name} has a conflicting registered CTest timeout: {props!r}")
    return data


def inventory(root, build, ctest):
    result = subprocess.run([str(ctest), "--test-dir", str(build), "--show-only=json-v1",
                             "-R", "^(symbol_burst_live|weak_signal)$"], cwd=root,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            check=True, timeout=30)
    return validate_inventory(json.loads(result.stdout), build)


def validate_profile(cache, sdk):
    expected = {"CMAKE_BUILD_TYPE": "Release", "DATAPUMP_GUI_BACKEND": "fltk",
                "DATAPUMP_PORTABLE": "ON", "DATAPUMP_CI_HOSTED_CLOCK_FUNCTIONAL": "ON",
                "DATAPUMP_SANITIZERS": "OFF", "DATAPUMP_BUILD_TUI": "ON",
                "DATAPUMP_BUILD_FB": "ON", "DATAPUMP_TEST_NATIVE_GUI": "OFF"}
    mismatches = {key: cache.get(key) for key, value in expected.items() if cache.get(key) != value}
    compilers = {kind: Path(cache.get(f"CMAKE_{kind}_COMPILER", "")).resolve()
                 for kind in ("C", "CXX")}
    outside = {kind: str(path) for kind, path in compilers.items()
               if sdk not in path.parents or not path.is_file()}
    if mismatches or outside:
        raise RuntimeError(f"unexpected SDK application configuration/compiler: {mismatches!r}, {outside!r}")
    return expected, compilers


def group_members(group):
    found = []
    for entry in Path("/proc").iterdir():
        if not entry.name.isdecimal():
            continue
        try:
            fields = (entry / "stat").read_text().rsplit(")", 1)[1].split()
            if int(fields[2]) == group and fields[0] != "Z":
                found.append(int(entry.name))
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            continue
    return found


def execute(command, root, output):
    child = subprocess.Popen(command, cwd=root, stdout=output, stderr=subprocess.STDOUT,
                             start_new_session=True)
    timed_out = False
    interrupted = None
    try:
        child.wait(timeout=TRIAL_WALL_SECONDS)
    except subprocess.TimeoutExpired:
        timed_out = True
    except BaseException as error:
        interrupted = error
    before = group_members(child.pid)
    if timed_out or interrupted is not None or before:
        try:
            os.killpg(child.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        until = time.monotonic() + 10
        while group_members(child.pid) and time.monotonic() < until:
            time.sleep(.05)
        if group_members(child.pid):
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
    child.wait(timeout=5)
    until = time.monotonic() + 5
    while group_members(child.pid) and time.monotonic() < until:
        time.sleep(.05)
    if interrupted is not None:
        raise interrupted
    return {"pid": child.pid, "exit_code": child.returncode, "driver_reaped": child.poll() is not None,
            "outer_timed_out": timed_out, "remaining_before_cleanup": before,
            "remaining_after_cleanup": group_members(child.pid)}


def process_boundary_preflight(root, report, ctest, observer, source):
    proof = report / "process-boundary-preflight"
    proof.mkdir()
    script = proof / "probe.py"
    script.write_text("import json,os,pathlib,time\n"
                      "pathlib.Path(__file__).with_name('child.json').write_text(json.dumps({"
                      "'pid':os.getpid(),'pgrp':os.getpgrp(),'sid':os.getsid(0)}))\n"
                      "time.sleep(.25)\n")
    (proof / "CTestTestfile.cmake").write_text(
        f'add_test(process_boundary "{sys.executable}" "{script}")\n')
    command = [sys.executable, "-B", str(observer), "--binary", sys.executable,
               "--retained-binary", sys.executable, "--output", str(proof / "observation"),
               "--source", source, "--", str(ctest), "--test-dir", str(proof),
               "--output-on-failure", "--verbose", "--timeout", "5"]
    with (proof / "driver.log").open("xb") as output:
        process = execute(command, root, output)
    child = json.loads((proof / "child.json").read_text())
    observation = json.loads((proof / "observation/observation.json").read_text())
    valid = (process["exit_code"] == 0 and not process["outer_timed_out"]
             and not process["remaining_before_cleanup"] and not process["remaining_after_cleanup"]
             and child["pgrp"] == process["pid"] and child["sid"] == process["pid"]
             and observation.get("diagnostics_complete") and observation.get("driver_reaped")
             and observation.get("target_identity_count") == 1)
    result = {"passed": valid, "process": process, "child": child,
              "observer": {key: observation.get(key) for key in
                           ("diagnostics_complete", "driver_reaped", "target_identity_count")},
              "scope": "installed observer -> SDK CTest -> Python child; no fixture body is modified"}
    (proof / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    if not valid:
        raise RuntimeError("observer/CTest process-group boundary was not established; no trial started")
    return result


def verify_source(root, source):
    if not re.fullmatch(r"[0-9a-f]{40}", source):
        raise RuntimeError("--source must be a full Git commit")
    if checked(["git", "rev-parse", "HEAD"], root) != source:
        raise RuntimeError("--source must identify this workflow checkout")
    if checked(["git", "cat-file", "-t", BASELINE], root) != "commit":
        raise RuntimeError(f"pinned application baseline is unavailable: {BASELINE}")
    checked(["git", "diff", "--exit-code", "HEAD", "--", *APP_PATHS,
             "build.sh", "tools/observe-native-test.py",
             "tools/sdk-burst-scheduling-diagnostic.py", ".github/workflows/sdk-application.yml"], root)
    pathspecs = [*APP_PATHS, ":(exclude)tests/test_sdk_burst_scheduling_diagnostic.py"]
    mismatch = subprocess.run(["git", "-c", f"safe.directory={root}", "diff", "--quiet",
                               BASELINE, source, "--", *pathspecs], cwd=root, check=False)
    if mismatch.returncode != 0:
        raise RuntimeError("application/test source differs from pinned diagnostic baseline 1cda521")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--ctest", required=True, type=Path)
    parser.add_argument("--source", required=True)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    root = args.root.resolve(strict=True)
    build = args.build_dir.resolve(strict=True)
    ctest = args.ctest.resolve(strict=True)
    report = args.report.resolve()
    sdk = (root / "build/sdk").resolve(strict=True)
    if report.exists():
        raise RuntimeError(f"report path already exists: {report}")
    report.mkdir(parents=True)
    verify_source(root, args.source)
    observer = root / "tools/observe-native-test.py"
    if not observer.is_file():
        raise RuntimeError("bounded native test observer is missing")
    cache_path = build / "CMakeCache.txt"
    cache = parse_cache(cache_path)
    expected, compilers = validate_profile(cache, sdk)
    manifest_path = sdk / "share/datapump-sdk/manifest.json"
    if sha256(manifest_path) != SDK_MANIFEST_SHA256:
        raise RuntimeError("prepared SDK manifest differs from failing run39797576002")
    if ctest != (sdk / "bin/ctest").resolve(strict=True):
        raise RuntimeError("CTest must come from the same prepared SDK")
    manifest = json.loads(manifest_path.read_text())
    for kind, field in (("C", "c_compiler"), ("CXX", "cxx_compiler")):
        target_compiler = manifest.get("target", {}).get(field)
        if not isinstance(target_compiler, str) or (sdk / target_compiler).resolve() != compilers[kind]:
            raise RuntimeError(f"configured {kind} compiler does not match the installed prepared SDK manifest")
    compiler_version = checked([str(compilers["CXX"]), "-dumpfullversion"], root)
    if compiler_version != "15.3.0":
        raise RuntimeError(f"SDK compiler differs from failing run GNU15.3.0: {compiler_version}")
    binaries = {name: build / name for name in ("test_symbol_burst_live", "test_weak_signal")}
    for binary in binaries.values():
        if not binary.is_file() or not os.access(binary, os.X_OK) or not is_elf(binary):
            raise RuntimeError(f"diagnostic target is not an executable ELF: {binary}")
    selected_inventory = inventory(root, build, ctest)
    (report / "inventory.json").write_text(json.dumps(selected_inventory, indent=2) + "\n")
    hashes = {name: sha256(path) for name, path in binaries.items()}
    provenance = {
        "schema": 1, "diagnostic_only": True, "qualification": False,
        "source_commit": args.source,
        "application_baseline": BASELINE,
        "driver_revision": checked(["git", "rev-parse", "HEAD"], root),
        "application_trees": {name: checked(["git", "rev-parse", f"{args.source}:{name}"], root)
                               for name in APP_PATHS},
        "source_file_sha256": {name: sha256(root / name) for name in SOURCE_FILES},
        "build_cache_sha256": sha256(cache_path), "build_configuration": expected,
        "build_compilers": {kind: str(path) for kind, path in compilers.items()},
        "sdk_manifest_sha256": sha256(manifest_path),
        "sdk_manifest": manifest, "compiler_version": compiler_version, "ctest": str(ctest),
        "ctest_version": capture([str(ctest), "--version"], root),
        "system": {"platform": platform.platform(), "uname": platform.uname()._asdict()},
        "targets": {name: {"path": str(path), "sha256": hashes[name], "bytes": path.stat().st_size}
                    for name, path in binaries.items()},
        "peer_phase": "weak_signal starts concurrently with symbol_burst_live; historical 183-second phase is not recreated",
        "measurement_limit": "two observed trials and one unobserved control per variant are a bounded differential; observer CPU is recorded separately and not subtracted from wall-time budget",
        "trial_outer_limit_seconds": TRIAL_WALL_SECONDS,
        "outer_limit_interpretation": "diagnostic cutoff only; timeout is incomplete measurement, not fixture-budget failure or qualification",
    }
    provenance["process_boundary_preflight"] = process_boundary_preflight(
        root, report, ctest, observer, args.source)
    (report / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    results = []
    aborted = False
    for ordinal, (name, selector, jobs, observed) in enumerate(TRIALS, 1):
        trial = report / f"{ordinal:02d}-{name}"
        trial.mkdir()
        log, junit = trial / "ctest.log", trial / "junit.xml"
        ctest_command = [str(ctest), "--test-dir", str(build), "--output-on-failure",
                         "--verbose", "--no-tests=error", "--parallel", str(jobs),
                         "--timeout", "3600", "-R", selector, "--output-junit", str(junit)]
        command = ctest_command
        if observed:
            command = [sys.executable, "-B", str(observer), "--binary", str(binaries["test_symbol_burst_live"]),
                       "--output", str(trial / "observation"),
                       "--retained-binary", str(binaries["test_symbol_burst_live"]),
                       "--source", args.source, "--log", str(log), "--", *ctest_command]
        usage_before = resource.getrusage(resource.RUSAGE_CHILDREN)
        start = time.monotonic_ns()
        with log.open("xb") as output:
            output.write(f"trial_start_monotonic_ns={start}\n".encode())
            output.flush()
            process = execute(command, root, output)
            finish = time.monotonic_ns()
            output.write(f"trial_end_monotonic_ns={finish}\n".encode())
        usage_after = resource.getrusage(resource.RUSAGE_CHILDREN)
        result = {"ordinal": ordinal, "name": name, "selector": selector, "parallel": jobs,
                  "observed": observed, "command": command, "driver_exit_code": process["exit_code"],
                  "process": process, "started_monotonic_ns": start, "finished_monotonic_ns": finish,
                  "wall_seconds": (finish - start) / 1e9,
                  "all_reaped_child_cpu_seconds": ((usage_after.ru_utime + usage_after.ru_stime) -
                                                    (usage_before.ru_utime + usage_before.ru_stime)),
                  "log": log.name, "log_sha256": sha256(log),
                  "junit": junit.name, "junit_sha256": sha256(junit) if junit.is_file() else None}
        errors = []
        if process["outer_timed_out"] or process["remaining_before_cleanup"] or process["remaining_after_cleanup"]:
            errors.append("outer timeout or unclosed process group; trial incomplete")
        if any(sha256(path) != hashes[name] for name, path in binaries.items()):
            errors.append("diagnostic target ELF changed during trials")
        try:
            xml = ET.parse(junit).getroot()
            cases = list(xml.iter("testcase"))
            names = {"symbol_burst_live"} | ({"weak_signal"} if jobs == 2 else set())
            if len(cases) != len(names) or {case.attrib.get("name") for case in cases} != names:
                errors.append("JUnit identities differ from the original selected CTest registrations")
            if list(xml.iter("skipped")):
                errors.append("original fixture was skipped")
            result["test_results"] = [{"name": case.attrib.get("name"), "seconds": case.attrib.get("time"),
                                        "failed": case.find("failure") is not None or case.find("error") is not None}
                                       for case in cases]
        except (OSError, ET.ParseError) as error:
            errors.append("JUnit unavailable or invalid: " + str(error))
        if observed:
            summary_path = trial / "observation/observation.json"
            if summary_path.is_file():
                summary = json.loads(summary_path.read_text())
                meta_path = trial / "observation/metadata.json"
                metadata = json.loads(meta_path.read_text()) if meta_path.is_file() else {}
                result["observer"] = {key: summary.get(key) for key in
                                       ("diagnostics_complete", "observer_error", "observer_cpu_seconds",
                                        "wall_seconds", "target_sample_count", "target_identity_count",
                                        "target_binary_sha256", "source_progress")}
                events_path = trial / "observation/events.jsonl"
                progress_markers = []
                if events_path.is_file():
                    for line in events_path.read_text(errors="replace").splitlines():
                        try:
                            event = json.loads(line)
                        except json.JSONDecodeError:
                            continue
                        if event.get("kind") == "log_marker":
                            progress_markers.append(event.get("line", ""))
                result["observer"]["progress_marker_count"] = len(progress_markers)
                result["observer"]["progress_markers"] = progress_markers[:128]
                result["observer"]["metadata_sha256"] = sha256(meta_path) if meta_path.is_file() else None
                if not (summary.get("driver_reaped") and summary.get("target_identity_count") == 1
                        and summary.get("target_sample_count", 0) > 0
                        and summary.get("target_binary_sha256") == hashes["test_symbol_burst_live"]
                        and metadata.get("source_commit") == args.source
                        and metadata.get("target_sha256") == hashes["test_symbol_burst_live"]):
                    errors.append("observer source, target identity, samples or closure check failed")
            else:
                result["observer"] = {"diagnostics_complete": False, "observer_error": ["summary missing"]}
                errors.append("observer summary is missing")
        result["measurement_errors"] = errors
        results.append(result)
        state = {"schema": 1, "source_commit": args.source, "results": results,
                 "completed_trials": len(results), "total_trials": len(TRIALS)}
        (report / "results.json").write_text(json.dumps(state, indent=2) + "\n")
        print(json.dumps({"trial": name, "exit_code": process["exit_code"],
                          "observed": observed, "wall_seconds": result["wall_seconds"]}), flush=True)
        if process["remaining_after_cleanup"]:
            aborted = True
            print("Wave stopped: process closure is unconfirmed; later trials would be contaminated", flush=True)
            break
    failures = [item for item in results if item["driver_exit_code"] != 0 or item["measurement_errors"]
                or any(test["failed"] for test in item.get("test_results", []))
                or (item.get("observer") and not item["observer"].get("diagnostics_complete"))]
    summary = {"schema": 1, "diagnostic_only": True, "qualification": False,
               "status": "aborted-unclosed-processes" if aborted else "failed" if failures else "completed-diagnostic",
               "failed_trials": [item["name"] for item in failures], "results": results}
    (report / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    return 1 if failures else 0


def interrupt(signum, frame):
    raise KeyboardInterrupt(f"diagnostic interrupted by signal {signum}")


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, interrupt)
    signal.signal(signal.SIGINT, interrupt)
    try:
        sys.exit(main())
    except Exception as error:
        print(f"sdk-burst-scheduling-diagnostic: {type(error).__name__}: {error}", file=sys.stderr)
        sys.exit(2)
