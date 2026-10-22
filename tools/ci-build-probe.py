#!/usr/bin/env python3
"""Diagnostic-only cold/warm/changed-source compiler-cache probe."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import re
import shutil
import subprocess
import sys
import time


def run(argv: list[str], *, cwd: Path, env: dict[str, str] | None = None,
        check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(argv, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, check=check)


def source_identity(commit: str, dirty: str) -> dict:
    return {"source_commit": commit,
            "dirty_digest": hashlib.sha256(dirty.encode()).hexdigest(),
            "dirty_tracked_paths": dirty.splitlines(),
            "classification": "diagnostic only"}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_output(root: Path, *args: str) -> bytes:
    # Match the existing container diagnostics: scoped trust survives checkout's
    # temporary HOME without changing the host or repository configuration.
    command = ['git', '-c', 'safe.directory=' + str(root), *args]
    result = subprocess.run(command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(f'git {args[0]} failed ({result.returncode}): '
                           + result.stderr.decode(errors='replace')[-4000:])
    return result.stdout


def cache_stats(ccache: str, cwd: Path, env=None) -> dict:
    try:
        result = run([ccache, "--show-stats", "--format=json"], cwd=cwd, env=env, check=False)
    except FileNotFoundError:
        return {"available": False, "error": "ccache unavailable; CMake uses ordinary compilation"}
    if not result.returncode:
        try:
            return {"available": True, "format": "json", "stats": json.loads(result.stdout)}
        except json.JSONDecodeError:
            pass
    fallback = run([ccache, "--print-stats"], cwd=cwd, env=env, check=False)
    if fallback.returncode:
        return {"available": False, "error": fallback.stdout[-2000:]}
    summary = {}
    for line in fallback.stdout.splitlines():
        fields = line.split()
        if len(fields) == 2 and fields[1].isdigit():
            summary[fields[0]] = int(fields[1])
    if not summary:
        return {"available": False, "error": "ccache statistics output was not recognized",
                "raw": fallback.stdout[-2000:]}
    return {"available": True, "format": "machine", "summary": summary,
            "raw": fallback.stdout[-12_000:]}


def stage(name: str, source: Path, build: Path, env: dict[str, str], ccache: str,
          output: Path) -> dict:
    clean = run(["cmake", "--build", str(build), "--target", "clean"], cwd=source, env=env)
    try:
        run([ccache, "--zero-stats"], cwd=source, env=env, check=False)
    except FileNotFoundError:
        pass
    ninja_log = build / ".ninja_log"
    before_lines = len(ninja_log.read_text(errors="replace").splitlines()) if ninja_log.exists() else 0
    started = time.monotonic()
    cpu_started = resource.getrusage(resource.RUSAGE_CHILDREN)
    result = run(["cmake", "--build", str(build), "--target", "test_timing_continuation",
                  "--parallel", os.environ.get("BUILD_JOBS", "2")], cwd=source,
                 env=env, check=False)
    wall = time.monotonic() - started
    cpu_finished = resource.getrusage(resource.RUSAGE_CHILDREN)
    cpu = ((cpu_finished.ru_utime + cpu_finished.ru_stime) -
           (cpu_started.ru_utime + cpu_started.ru_stime))
    log_text = (clean.stdout + result.stdout)[-200_000:]
    (output / f"{name}.log").write_text(log_text, encoding="utf-8")
    if result.returncode:
        (output / f"{name}-failure.json").write_text(json.dumps({
            "stage": name, "returncode": result.returncode,
            "wall_seconds": wall, "process_cpu_seconds": cpu,
            "ccache": cache_stats(ccache, source, env)}, indent=2) + "\n")
        raise RuntimeError(f"{name} build failed ({result.returncode}); see {name}.log")
    binary = build / "test_timing_continuation"
    if not binary.is_file():
        # Multi-config generators place targets in a configuration directory.
        candidates = list(build.glob("*/test_timing_continuation"))
        if len(candidates) != 1:
            raise RuntimeError("could not identify the built test_timing_continuation executable")
        binary = candidates[0]
    entries = []
    if ninja_log.exists():
        for line in ninja_log.read_text(errors="replace").splitlines()[before_lines:]:
            cols = line.split("\t")
            if len(cols) >= 4 and cols[0].isdigit() and cols[1].isdigit():
                entries.append({"duration_ms": int(cols[1]) - int(cols[0]),
                                "output": cols[3][:300]})
    return {"name": name, "wall_seconds": wall, "process_cpu_seconds": cpu,
            "binary_sha256": sha256(binary), "ccache": cache_stats(ccache, source, env),
            "cache_bytes": sum(path.stat().st_size for path in (output / 'ccache').rglob('*') if path.is_file()),
            "ninja_log_compilation_link_entries": entries[-300:],
            "binary_bytes": binary.stat().st_size,
            "commands": command_inputs(build, env)}


def command_inputs(build: Path, env=None) -> list[dict[str, str]]:
    result = run(["ninja", "-C", str(build), "-t", "commands", "test_timing_continuation"],
                 cwd=build, env=env, check=False)
    if result.returncode:
        raise RuntimeError('could not retain compiler/link input commands')
    commands = []
    for line in result.stdout.splitlines()[:200]:
        commands.append({"sha256": hashlib.sha256(line.encode()).hexdigest(),
                         "command": line[:1536]})
    return commands


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-sha", required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--profile", choices=("native", "sdk"), required=True)
    parser.add_argument("--sdk", type=Path)
    args = parser.parse_args()
    root = Path.cwd()
    report = args.report.resolve()
    report.mkdir(parents=True, exist_ok=True)
    source = report / "source"
    build = report / "build"
    try:
        archive = git_output(root, 'archive', '--format=tar', args.source_sha)
    except RuntimeError as exc:
        (report / 'snapshot-failure.log').write_text(str(exc) + '\n', encoding='utf-8')
        raise
    import tarfile
    import io
    source.mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:") as tar:
        # The archive is produced from the verified local Git commit, not an external artifact.
        tar.extractall(source)
    commit = git_output(root, 'rev-parse', args.source_sha).decode().strip()
    dirty = git_output(root, 'status', '--porcelain=v1', '--untracked-files=all').decode()
    marker = source_identity(commit, dirty)
    marker['git_archive_sha256'] = hashlib.sha256(archive).hexdigest()
    (report / "source-snapshot.json").write_text(json.dumps(marker, indent=2) + "\n")
    ccache = shutil.which("ccache") or "ccache"
    env = os.environ.copy()
    config_path = report / "ccache.conf"
    config_path.write_text(
        f"cache_dir = {report / 'ccache'}\ncompiler_check = content\n"
        "max_size = 512M\n", encoding="utf-8")
    env.update({"CCACHE_CONFIGPATH": str(config_path)})
    if args.profile == "sdk":
        if not args.sdk:
            raise RuntimeError("SDK profile requires --sdk")
        sdk = args.sdk.resolve()
        env['PATH'] = str(sdk / 'bin') + os.pathsep + env['PATH']
        cmake = str(sdk / "bin/cmake")
        compiler_options = [f"-DCMAKE_TOOLCHAIN_FILE={source}/cmake/toolchains/source-sdk.cmake",
                            f"-DDATAPUMP_SDK_ROOT={sdk}"]
        extra = ["-DDATAPUMP_PORTABLE=ON", "-DDATAPUMP_TEST_NATIVE_GUI=OFF",
                 "-DDATAPUMP_BUILD_TUI=ON", "-DDATAPUMP_BUILD_FB=ON",
                 "-DDATAPUMP_CI_HOSTED_CLOCK_FUNCTIONAL=ON"]
    else:
        cmake = "cmake"
        compiler_options = ["-DCMAKE_C_COMPILER=gcc-11", "-DCMAKE_CXX_COMPILER=g++-11"]
        extra = ["-DDATAPUMP_SANITIZERS=ON", "-DDATAPUMP_PORTABLE=OFF",
                 "-DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE", "-DDART_TESTING_TIMEOUT=3600"]
    configure = [cmake, "-S", str(source), "-B", str(build), "-G", "Ninja",
                 *compiler_options, "-DCMAKE_BUILD_TYPE=" +
                 ("Debug" if args.profile == "native" else "Release"), "-DBUILD_TESTING=ON",
                 "-DDATAPUMP_COMPILER_CACHE=ON", *extra]
    config = run(configure, cwd=source, env=env, check=False)
    (report / "configure.log").write_text(config.stdout[-100_000:], encoding="utf-8")
    if config.returncode:
        raise RuntimeError(f"configure failed ({config.returncode}); see configure.log")
    stages = [stage("cold", source, build, env, ccache, report),
              stage("warm", source, build, env, ccache, report)]
    if stages[0]["binary_sha256"] != stages[1]["binary_sha256"]:
        raise RuntimeError("cold and warm executable hashes differ")
    changed = source / "src/pattern_correlator.cpp"
    original = changed.read_bytes()
    try:
        changed.write_bytes(original + b"\nstatic_assert(true, \"ci-cache-change\");\n")
        stages.append(stage("changed-source", source, build, env, ccache, report))
    finally:
        changed.write_bytes(original)
    # Reuse this configured profile for the cheap registration/prerequisite
    # check. Planning does not build or execute any additional test.
    scope = 'core' if args.profile == 'native' else 'contract'
    parts = ('0', '1') if scope == 'core' else ('general', 'preparation', 'live', 'calibration')
    identities = []
    for part in parts:
        path = report / f'plan-{part}.json'
        planned = run([sys.executable, str(root / 'tools/ci-test-plan.py'), 'plan',
                       '--build-dir', str(build), '--source-root', str(root),
                       '--scope', scope, '--part', part,
                       '--config', 'Debug' if args.profile == 'native' else 'Release',
                       '--ctest', 'ctest', '--output', str(path)], cwd=root, env=env, check=False)
        (report / f'plan-{part}.log').write_text(planned.stdout[-20_000:])
        if planned.returncode:
            raise RuntimeError(f'{scope} registration planning failed for {part}')
        identities.append(json.loads(path.read_text())['plan_sha256'])
    if len(set(identities)) != 1:
        raise RuntimeError('configured profile plans disagree on complete partition identity')
    result = {"profile": args.profile, "source": marker, "target": "test_timing_continuation",
              "stages": stages, "partition_model_sha256": identities[0],
              "strict_cache_policy": {"compiler_check": "content",
              "max_size": "512MiB", "sloppiness": None, "hash_dir_relaxed": False}}
    encoded = json.dumps(result, indent=2) + "\n"
    if len(encoded.encode()) > 900_000:
        raise RuntimeError("probe metadata exceeded 900KiB limit")
    (report / "result.json").write_text(encoded, encoding="utf-8")
    print(encoded)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"ci-build-probe: {exc}", file=sys.stderr)
        raise SystemExit(1)
