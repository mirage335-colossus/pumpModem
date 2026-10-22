#!/usr/bin/env python3
"""Run diagnostic-only matched Symbol Burst drain trials on existing Release ELFs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

BASELINE = "6ac28bca6187b6baec799abb9fb9d71cc112431c"
TRIAL_WALL_SECONDS = 900

TRIALS = (
    ("unobserved-A", "^(symbol_burst_live)$", 1, False),
    ("observed-A-1", "^(symbol_burst_live)$", 1, True),
    ("observed-B-1", "^(symbol_burst_live|weak_signal)$", 2, True),
    ("observed-B-2", "^(symbol_burst_live|weak_signal)$", 2, True),
    ("observed-A-2", "^(symbol_burst_live)$", 1, True),
    ("unobserved-B", "^(symbol_burst_live|weak_signal)$", 2, False),
)
SOURCE_FILES = (
    "CMakeLists.txt", "tests/test_symbol_burst_live.cpp", "tools/observe-native-test.py",
    "tests/test_weak_signal.cpp", "src/live.cpp", "src/search_parallel.cpp",
    "tools/run-ci-tests.py", ".github/workflows/native-linux.yml",
    "tools/release-drain-scheduling-diagnostic.py",
)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def capture(command, cwd, limit=16000):
    if command[0] == 'git':
        command = ['git', '-c', f'safe.directory={cwd}', *command[1:]]
    try:
        result = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, check=False)
        return {"command": command, "exit_code": result.returncode,
                "output": result.stdout[-limit:]}
    except OSError as error:
        return {"command": command, "error": f"{type(error).__name__}: {error}"}


def read_optional(path, limit=65536):
    try:
        raw = path.read_bytes()[:limit]
        return raw.decode("utf-8", "replace")
    except OSError as error:
        return f"unavailable: {type(error).__name__}: {error}"


def inventory(root, pattern, expected):
    result = subprocess.run(["ctest", "--test-dir", str(root / "build"),
                             "--show-only=json-v1", "-R", pattern], cwd=root,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    tests = json.loads(result.stdout).get("tests", [])
    names = [item["name"] for item in tests]
    if len(names) != len(expected) or set(names) != set(expected):
        raise RuntimeError(f"CTest inventory mismatch for {pattern}: {names!r} != {expected!r}")
    for test in tests:
        executable = "test_symbol_burst_live" if test["name"] == "symbol_burst_live" else "test_weak_signal"
        if test["command"] != [str(root / "build" / executable)]:
            raise RuntimeError(f"changed original no-argument registration: {test!r}")
    return json.loads(result.stdout)


def checked(command, root):
    if command[0] == 'git':
        command = ['git', '-c', f'safe.directory={root}', *command[1:]]
    return subprocess.run(command, cwd=root, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=True, timeout=30).stdout.strip()


def group_members(group):
    members = []
    for entry in Path('/proc').iterdir():
        if not entry.name.isdecimal():
            continue
        try:
            fields = (entry / 'stat').read_text().rsplit(')', 1)[1].split()
            if int(fields[2]) == group and fields[0] != 'Z':
                members.append(int(entry.name))
        except (FileNotFoundError, ProcessLookupError):
            continue
    return members


def execute(command, root, output):
    # Own a new process group; cleanup never targets a caller's group.
    child = subprocess.Popen(command, cwd=root, stdout=output,
                             stderr=subprocess.STDOUT, start_new_session=True)
    timed_out = False
    try:
        child.wait(timeout=TRIAL_WALL_SECONDS)
    except subprocess.TimeoutExpired:
        timed_out = True
    remaining = group_members(child.pid)
    if timed_out or remaining:
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
    return {"pid": child.pid, "exit_code": child.returncode,
            "driver_reaped": child.poll() is not None, "outer_timed_out": timed_out,
            "remaining_before_cleanup": remaining,
            "remaining_after_cleanup": group_members(child.pid)}


def process_boundary_preflight(root, report, source):
    """Check the installed observer/CTest chain with bounded Python-only work."""
    proof = report / 'process-boundary-preflight'
    proof.mkdir()
    script = proof / 'probe.py'
    script.write_text('import os,json,pathlib,time\n'
                      'pathlib.Path(__file__).with_name("child.json").write_text(json.dumps('
                      '{"pid":os.getpid(),"pgrp":os.getpgrp(),"sid":os.getsid(0)}))\n'
                      'time.sleep(.35)\n')
    (proof / 'CTestTestfile.cmake').write_text(
        f'add_test(process_boundary "{sys.executable}" "{script}")\n')
    command = [sys.executable, '-B', str(root / 'tools/observe-native-test.py'),
               '--binary', sys.executable, '--retained-binary', sys.executable,
               '--output', str(proof / 'observation'), '--source', source, '--',
               'ctest', '--test-dir', str(proof), '--output-on-failure', '--verbose',
               '--timeout', '5']
    with (proof / 'driver.log').open('xb') as output:
        process = execute(command, root, output)
    child = json.loads((proof / 'child.json').read_text())
    observation = json.loads((proof / 'observation/observation.json').read_text())
    valid = (process['exit_code'] == 0 and not process['outer_timed_out']
             and not process['remaining_before_cleanup'] and not process['remaining_after_cleanup']
             and child['pgrp'] == process['pid'] and child['sid'] == process['pid']
             and observation['diagnostics_complete'] and observation['driver_reaped']
             and observation['target_identity_count'] == 1)
    value = {'passed': valid, 'process': process, 'child': child,
             'ctest_version': capture(['ctest', '--version'], root),
             'scope': 'installed observer -> CTest -> Python child; original C++ test bodies do not create process sessions'}
    (proof / 'result.json').write_text(json.dumps(value, indent=2) + '\n')
    if not valid:
        raise RuntimeError('installed observer/CTest process group boundary not established; wave not launched')
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--source", required=True)
    args = parser.parse_args()
    root = args.root.resolve(strict=True)
    report = args.report.resolve()
    build = root / "build"
    binary = build / "test_symbol_burst_live"
    peer_binary = build / "test_weak_signal"
    if not binary.is_file() or not peer_binary.is_file():
        raise RuntimeError("both prebuilt diagnostic ELFs must exist")
    for target in (binary, peer_binary):
        with target.open('rb') as stream:
            if stream.read(4) != b"\x7fELF":
                raise RuntimeError("diagnostic targets must be ELF images")
    if report.exists():
        raise RuntimeError(f"report directory already exists: {report}")
    report.mkdir(parents=True)

    cache_path = build / "CMakeCache.txt"
    cache = {}
    for line in cache_path.read_text().splitlines():
        if "=" in line and not line.startswith(("#", "//")):
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    expected = {"CMAKE_BUILD_TYPE": "Release", "DATAPUMP_SANITIZERS": "OFF",
                "DATAPUMP_PORTABLE": "ON", "CMAKE_C_COMPILER": "/usr/bin/gcc-11",
                "CMAKE_CXX_COMPILER": "/usr/bin/g++-11"}
    mismatches = {key: cache.get(key) for key, value in expected.items() if cache.get(key) != value}
    if mismatches:
        raise RuntimeError(f"unexpected diagnostic CMake configuration: {mismatches!r}")
    if not (os.access(binary, os.X_OK) and os.access(peer_binary, os.X_OK)):
        raise RuntimeError("diagnostic ELF is not executable")
    if not re.fullmatch('[0-9a-f]{40}', args.source) or checked(['git', 'rev-parse', 'HEAD'], root) != args.source:
        raise RuntimeError('source must be the exact checkout commit')
    app_paths = ['src', 'include', 'tests', 'cmake', 'CMakeLists.txt']
    checked(['git', 'diff', '--exit-code', BASELINE, 'HEAD', '--', *app_paths], root)
    checked(['git', 'diff', '--exit-code', 'HEAD', '--', *app_paths,
             'tools/observe-native-test.py', 'tools/run-ci-tests.py',
             'tools/release-drain-scheduling-diagnostic.py'], root)
    registrations = inventory(root, "^(symbol_burst_live|weak_signal)$", ["symbol_burst_live", "weak_signal"])
    (report / 'inventory.json').write_text(json.dumps(registrations, indent=2) + '\n')
    (report / 'CMakeCache.txt').write_bytes(cache_path.read_bytes())
    retained = report / 'binaries'
    retained.mkdir()
    for target in (binary, peer_binary):
        shutil.copy2(target, retained / target.name)
        if sha256(target) != sha256(retained / target.name):
            raise RuntimeError('retained target hash mismatch')

    identity = {
        "schema": 1,
        "diagnostic_only": True,
        "qualification": False,
        "source_commit": args.source,
        "application_baseline": BASELINE,
        "application_trees": {name: checked(['git', 'rev-parse', f'HEAD:{name}'], root)
                              for name in app_paths},
        "source_tree": capture(["git", "rev-parse", "HEAD^{tree}"], root),
        "source_file_sha256": {name: sha256(root / name) for name in SOURCE_FILES},
        "build_cache_sha256": sha256(cache_path),
        "build_configuration": {key: cache.get(key) for key in expected},
        "build_cache_path": str(cache_path),
        "compiler": {
            "c": capture(["gcc-11", "--version"], root),
            "cxx": capture(["g++-11", "--version"], root),
        },
        "system": {
            "platform": platform.platform(),
            "uname": platform.uname()._asdict(),
            "cpuinfo": read_optional(Path("/proc/cpuinfo")),
            "cpu_online": read_optional(Path("/sys/devices/system/cpu/online")),
            "cgroup": {name: read_optional(Path("/sys/fs/cgroup") / name, 8192)
                       for name in ("cpu.max", "cpu.stat", "cpuset.cpus.effective", "memory.max")},
            "process_status": read_optional(Path("/proc/self/status")),
        },
        "targets": {
            "test_symbol_burst_live": {"path": str(binary), "sha256": sha256(binary), "bytes": binary.stat().st_size},
            "test_weak_signal": {"path": str(peer_binary), "sha256": sha256(peer_binary), "bytes": peer_binary.stat().st_size},
        },
        "source_interpretation": "source commit and tree describe checkout; per-file hashes bind this driver, workflow and original test source",
        "raw_pcm": "not independently captured; no noninvasive capture facility used",
        "historical_peer_state": "not reproduced; this pair starts weak_signal concurrently with the burst interval",
        "measurement_limit": "four observed trials and two unobserved controls are a limited repeated differential, not a statistical performance estimate; observer CPU is recorded and not subtracted",
        "trial_outer_limit_seconds": TRIAL_WALL_SECONDS,
        "outer_limit_interpretation": "diagnostic capture cutoff only; an outer timeout is inconclusive, never a measured fixture-budget failure or qualification",
    }
    identity['process_boundary_preflight'] = process_boundary_preflight(root, report, args.source)
    (report / "provenance.json").write_text(json.dumps(identity, indent=2) + "\n")
    results = []
    aborted = False
    for ordinal, (name, selector, jobs, observed) in enumerate(TRIALS, 1):
        trial = report / f"{ordinal:02d}-{name}"
        trial.mkdir()
        log = trial / "ctest.log"
        junit = trial / "junit.xml"
        ctest = ["ctest", "--test-dir", str(build), "--output-on-failure", "--verbose",
                 "--no-tests=error", "--parallel", str(jobs), "-R", selector,
                 "--timeout", "3600", "--output-junit", str(junit)]
        argv = ctest
        if observed:
            argv = [sys.executable, "-B", str(root / "tools/observe-native-test.py"),
                    "--binary", str(binary), "--output", str(trial / "observation"),
                    "--retained-binary", str(retained / binary.name),
                    "--source", args.source, "--log", str(log), "--", *ctest]
        started = time.monotonic_ns()
        with log.open("xb") as output:
            output.write(f"trial_start_monotonic_ns={started}\n".encode())
            output.flush()
            process = execute(argv, root, output)
            finished = time.monotonic_ns()
            output.write(f"trial_end_monotonic_ns={finished}\n".encode())
        result = {
            "ordinal": ordinal, "name": name, "selector": selector, "parallel": jobs,
            "observed": observed, "command": argv, "driver_exit_code": process['exit_code'],
            "process": process,
            "started_monotonic_ns": started, "finished_monotonic_ns": finished,
            "wall_seconds": (finished - started) / 1e9,
            "log": log.name, "log_sha256": sha256(log),
            "junit": junit.name, "junit_sha256": sha256(junit) if junit.is_file() else None,
        }
        errors = []
        if process['outer_timed_out'] or process['remaining_before_cleanup'] or process['remaining_after_cleanup']:
            errors.append('outer timeout or unclosed process group; diagnostic interval incomplete')
        for target in (binary, peer_binary):
            if sha256(target) != identity['targets'][target.name]['sha256']:
                errors.append('built target changed during trials')
        try:
            xml = ET.parse(junit).getroot()
            cases = list(xml.iter('testcase'))
            expected_names = {'symbol_burst_live'} | ({'weak_signal'} if jobs == 2 else set())
            if len(cases) != len(expected_names) or {c.attrib['name'] for c in cases} != expected_names:
                errors.append('JUnit test identities differ from original selected registrations')
            if list(xml.iter('skipped')):
                errors.append('original fixture was skipped')
            result['test_results'] = [{'name': c.attrib['name'], 'seconds': c.attrib.get('time'),
                                      'failed': c.find('failure') is not None or c.find('error') is not None}
                                     for c in cases]
        except (OSError, ET.ParseError, KeyError) as error:
            errors.append('JUnit unavailable or invalid: ' + str(error))
        if observed:
            summary_path = trial / "observation" / "observation.json"
            if summary_path.is_file():
                summary = json.loads(summary_path.read_text())
                result["observer"] = {
                    "diagnostics_complete": summary.get("diagnostics_complete"),
                    "observer_error": summary.get("observer_error"),
                    "observer_cpu_seconds": summary.get("observer_cpu_seconds"),
                    "wall_seconds": summary.get("wall_seconds"),
                    "target_sample_count": summary.get("target_sample_count"),
                    "target_identity_count": summary.get("target_identity_count"),
                    "target_binary_sha256": summary.get("target_binary_sha256"),
                    "metadata_sha256": sha256(trial / "observation" / "metadata.json")
                        if (trial / "observation" / "metadata.json").is_file() else None,
                }
                metadata = json.loads((trial / 'observation' / 'metadata.json').read_text())
                if not (summary.get('driver_reaped') and summary.get('target_identity_count') == 1
                        and summary.get('target_sample_count', 0) > 0
                        and summary.get('target_binary_sha256') == identity['targets'][binary.name]['sha256']
                        and metadata.get('source_commit') == args.source
                        and metadata.get('target_sha256') == identity['targets'][binary.name]['sha256']):
                    errors.append('observer source, target identity, samples or closure check failed')
            else:
                result["observer"] = {"diagnostics_complete": False,
                                      "observer_error": ["observer summary missing"]}
        result['measurement_errors'] = errors
        last = build / 'Testing/Temporary/LastTest.log'
        if last.is_file():
            shutil.copy2(last, trial / 'LastTest.log')
        results.append(result)
        state = {"schema": 1, "source_commit": args.source, "results": results,
                 "completed_trials": len(results), "total_trials": len(TRIALS)}
        (report / "results.json").write_text(json.dumps(state, indent=2) + "\n")
        print(json.dumps({"trial": name, "exit_code": process['exit_code'],
                          "observed": observed, "wall_seconds": result["wall_seconds"]}), flush=True)
        if process['remaining_after_cleanup']:
            aborted = True
            print('Wave stopped: prior process closure is unconfirmed; later trials would be contaminated', flush=True)
            break

    failures = [item for item in results if item["driver_exit_code"] != 0 or item['measurement_errors'] or
                any(t['failed'] for t in item.get('test_results', [])) or
                (item.get("observer") and not item["observer"].get("diagnostics_complete"))]
    summary = {"schema": 1, "diagnostic_only": True, "qualification": False,
               "status": "aborted-unclosed-processes" if aborted else "failed" if failures else "completed-diagnostic",
               "failed_trials": [item["name"] for item in failures],
               "results": results}
    (report / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"release-drain-scheduling: {type(error).__name__}: {error}", file=sys.stderr)
        sys.exit(2)
