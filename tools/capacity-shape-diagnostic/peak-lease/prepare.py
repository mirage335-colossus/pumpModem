#!/usr/bin/env python3
"""Prepare and run the private native peak lease CI diagnostic."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import re
import resource
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import time

PATCH_SHA256 = "183590a663eb40f67557046dfda3fb0a41778940fc57dd212106c38796403e48"
SOURCE = {
    "include/datapump/pattern_receiver.hpp": ("7da6a7ab2aa261fd65b5be1a31f81c3b22f6efc9517cd819dd2907fefe022b13", "483d6152db347f37ddbcce37fdc64511fdb7e77b37dfb45c3ac82dacf57405b6", False),
    "include/datapump/streaming_modem.hpp": ("8daf14164e75b427dae0f9a1337d00398b4b4164003a86d79d8aa8cb8492e20f", "2a4be887eebe97fbf7ed558b4bab2a59034cb22f0f5eb3a1ba0498eee92011b3", False),
    "src/pattern_receiver.cpp": ("e956fab251fa1e42981fada7f132d07f6beba53d36a8fc4ff5ace80b66a70f87", "44245fe4eb0268121a514a57e4dea380172eb3740a8f34414ece96060d36c899", False),
    "src/streaming_modem.cpp": ("12b73aebd464c03d0350b263b6bd35601977f5e769e6715498ada863f498a5b6", "4ae906771402d7a5479ad47c146ea0a5dca270a5254e277a5ec83d08988ce6c0", False),
    "src/native_peak_workspace.hpp": (None, "fa68cc3b7f44984179584ecc636f2f5b726656f2a8f5c6acd66d153d4e1b822c", True),
    "tests/test_pattern_receiver.cpp": ("78a1347c68b164b437ea46e6ad1fb17eb1219186588f3213b71e0e3dbf56934d", "aaa8ac9b16ba670e06373e202bd581cc1150ea631deabd7f5c901704e87b2838", False),
}
FOCUSED_CASES = {
    "native peak workspace ownership",
    "native peak receiver equivalence",
    "native peak quota equivalence",
    "native peak StreamingReceiver bridge",
    "qualified UTC FFT partial EOF threshold / native peak",
}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def git(root: Path, *args: str) -> list[str]:
    return ["git", "-c", "safe.directory=" + str(root), *args]


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def checked_extract(archive: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=False)
    base = destination.resolve()
    with tarfile.open(archive, "r:") as captured:
        for item in captured.getmembers():
            name = PurePosixPath(item.name)
            if name.is_absolute() or not name.parts or ".." in name.parts:
                raise ValueError("unsafe source archive member: " + item.name)
            target = destination.joinpath(*name.parts)
            if item.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif item.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                source = captured.extractfile(item)
                if source is None:
                    raise ValueError("unreadable source archive member: " + item.name)
                with source, target.open("xb") as output:
                    shutil.copyfileobj(source, output)
                target.chmod(item.mode & 0o777)
            elif item.issym():
                link = Path(item.linkname)
                if link.is_absolute() or not (target.parent / link).resolve().is_relative_to(base):
                    raise ValueError("source archive symlink escapes source: " + item.name)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.symlink_to(item.linkname)
            else:
                raise ValueError("unsupported source archive member type: " + item.name)


def apply_checked_patch(root: Path, patch: Path) -> dict[str, str]:
    patch_bytes = patch.read_bytes()
    patch_hash = sha(patch_bytes)
    if patch_hash != PATCH_SHA256:
        raise ValueError("frozen native peak patch hash mismatch")
    for relative, (before, _, new_file) in SOURCE.items():
        path = root / relative
        if new_file:
            if path.exists():
                raise ValueError("new patch path already exists: " + relative)
        elif not path.is_file() or sha(path.read_bytes()) != before:
            raise ValueError("native peak patch preimage mismatch: " + relative)
    subprocess.run(["git", "-c", "safe.directory=" + str(root), "apply", "--check", str(patch)], cwd=root, check=True)
    subprocess.run(["git", "-c", "safe.directory=" + str(root), "apply", str(patch)], cwd=root, check=True)
    postimages = {}
    for relative, (_, after, _) in SOURCE.items():
        actual = sha((root / relative).read_bytes())
        if actual != after:
            raise ValueError("native peak patch postimage mismatch: " + relative)
        postimages[relative] = actual

    reverse_tree = root.parent / (root.name + "-reverse-check")
    if reverse_tree.exists():
        raise ValueError("reverse-check tree already exists")
    shutil.copytree(root, reverse_tree, symlinks=True)
    try:
        subprocess.run(["git", "-c", "safe.directory=" + str(reverse_tree), "apply", "--reverse", "--check", str(patch)], cwd=reverse_tree, check=True)
        subprocess.run(["git", "-c", "safe.directory=" + str(reverse_tree), "apply", "--reverse", str(patch)], cwd=reverse_tree, check=True)
        for relative, (before, _, new_file) in SOURCE.items():
            path = reverse_tree / relative
            if new_file:
                if path.exists():
                    raise ValueError("reverse patch retained new path: " + relative)
            elif sha(path.read_bytes()) != before:
                raise ValueError("reverse patch preimage mismatch: " + relative)
    finally:
        shutil.rmtree(reverse_tree)
    return postimages


def prepare(root: Path, report: Path, patch: Path) -> dict:
    root = root.resolve(strict=True)
    report = report.resolve()
    patch = patch.resolve(strict=True)
    if report.exists():
        raise ValueError("diagnostic report path must be fresh")
    commit = subprocess.check_output(git(root, "rev-parse", "HEAD"), cwd=root, text=True).strip()
    if len(commit) != 40 or any(c not in "0123456789abcdef" for c in commit):
        raise ValueError("invalid launcher HEAD identity")
    report.mkdir(parents=True, exist_ok=False)
    archive = report / "source-archive.tar"
    with archive.open("xb") as output:
        subprocess.run(git(root, "archive", "HEAD"), cwd=root, stdout=output, check=True)
    archive_hash = sha(archive.read_bytes())
    source_root = root / "build/native-peak-lease-source"
    if source_root.exists():
        raise ValueError("private source directory already exists")
    checked_extract(archive, source_root)
    postimages = apply_checked_patch(source_root, patch)
    result = {
        "schema": 1,
        "status": "prepared_private_source",
        "classification": "private six-path native peak lease patch for focused receiver diagnosis; not Live integration or qualification",
        "launcher_head": commit,
        "source_archive_sha256": archive_hash,
        "patch_sha256": sha(patch.read_bytes()),
        "source_root": str(source_root),
        "patched_source_sha256": postimages,
        "apply_and_reverse_exact": True,
        "dirty_worktree_excluded_by_git_archive": True,
    }
    write_json(report / "prepare.json", result)
    archive.unlink()
    return result


def run_tests(root: Path, report: Path, binary: Path, compiler: str) -> dict:
    root, report, binary = root.resolve(strict=True), report.resolve(strict=True), binary.resolve(strict=True)
    manifest = json.loads((report / "prepare.json").read_text())
    head = subprocess.check_output(git(root, "rev-parse", "HEAD"), cwd=root, text=True).strip()
    if head != manifest["launcher_head"] or manifest["patch_sha256"] != PATCH_SHA256:
        raise ValueError("launcher or frozen patch differs from preparation")
    source = Path(manifest["source_root"]).resolve(strict=True)
    cache_path = root / "build/CMakeCache.txt"
    lines = cache_path.read_text().splitlines()
    config = {}
    for key in ("CMAKE_HOME_DIRECTORY", "CMAKE_BUILD_TYPE", "DATAPUMP_SANITIZERS", "DATAPUMP_PORTABLE", "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER", "CMAKE_CXX_FLAGS_DEBUG"):
        values = [line.split("=", 1)[1] for line in lines if line.startswith(key + ":")]
        if len(values) != 1:
            raise ValueError("missing or ambiguous CMake cache key: " + key)
        config[key] = values[0]
    if (config["CMAKE_BUILD_TYPE"], config["DATAPUMP_SANITIZERS"], config["DATAPUMP_PORTABLE"]) != ("Debug", "ON", "OFF") or Path(config["CMAKE_HOME_DIRECTORY"]).resolve(strict=True) != source:
        raise ValueError("configured source or Debug sanitizer/portable policy differs")
    versions = {}
    for key, name in (("CMAKE_C_COMPILER", "gcc-11"), ("CMAKE_CXX_COMPILER", "g++-11")):
        path = shutil.which(name)
        if not path or Path(config[key]).resolve() != Path(path).resolve():
            raise ValueError("configured compiler differs: " + name)
        probe = subprocess.run([path, "--version"], cwd=root, capture_output=True, check=True)
        if not probe.stdout.strip():
            raise ValueError("compiler version output missing: " + name)
        versions[name] = {"path": str(Path(path).resolve()), "version": probe.stdout.decode("utf-8", "strict").strip()}
    if compiler != "g++-11":
        raise ValueError("native peak route requires g++-11")
    retained = report / "compiled-source"
    retained.mkdir(exist_ok=False)
    for relative, (_, after, _) in SOURCE.items():
        src = source / relative
        if sha(src.read_bytes()) != after or manifest["patched_source_sha256"].get(relative) != after:
            raise ValueError("actual compiled source postimage differs: " + relative)
        dst = retained / relative
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dst)
    shutil.copyfile(cache_path, report / "CMakeCache.txt")
    test_source = (source / "tests/test_pattern_receiver.cpp").read_text().split("int main(int argc,char** argv)", 1)
    if len(test_source) != 2:
        raise ValueError("pinned test main signature missing")
    full_inventory = re.findall(r'\brun\(\s*"([^"]+)"', test_source[1])
    if len(full_inventory) != len(set(full_inventory)) or not FOCUSED_CASES.issubset(full_inventory):
        raise ValueError("full or focused registration inventory differs")
    keys = ("PATH", "LANG", "LC_ALL", "TZ", "ASAN_OPTIONS", "UBSAN_OPTIONS", "RUNNER_OS", "RUNNER_ARCH")
    env = {k: os.environ[k] for k in keys if k in os.environ}
    if env.get("ASAN_OPTIONS") != "detect_leaks=0:halt_on_error=1" or env.get("UBSAN_OPTIONS") != "halt_on_error=1:print_stacktrace=1":
        raise ValueError("sanitizer runtime options differ from authorized route")
    binary_hash = sha(binary.read_bytes())
    result = {"schema": 2, "launcher_head": head, "source_archive_sha256": manifest["source_archive_sha256"],
              "patch_sha256": PATCH_SHA256, "source_root": str(source), "compiled_postimages": manifest["patched_source_sha256"],
              "cache_sha256": sha(cache_path.read_bytes()), "config": config, "compiler_versions": versions,
              "binary": str(binary), "binary_sha256": binary_hash, "environment": env,
              "full_registration_inventory": full_inventory, "focused": None, "full": None,
              "interpretation": "receiver-only diagnostic; no Live integration, whole32MiB/120s or qualification claim"}
    def run_one(label, command, seconds, expected):
        start = datetime.now(timezone.utc).isoformat()
        begin, cpu = time.monotonic(), resource.getrusage(resource.RUSAGE_CHILDREN)
        timed_out = False
        try:
            child = subprocess.run(command, cwd=root, env=env, capture_output=True, check=False, timeout=seconds)
            stdout, stderr, code = child.stdout, child.stderr, child.returncode
        except subprocess.TimeoutExpired as error:
            stdout, stderr, code, timed_out = error.stdout or b"", error.stderr or b"", 124, True
        wall = time.monotonic() - begin
        end_cpu = resource.getrusage(resource.RUSAGE_CHILDREN)
        (report / (label + ".stdout")).write_bytes(stdout)
        (report / (label + ".stderr")).write_bytes(stderr)
        observed = [line[:-8] for line in stdout.decode("utf-8", "strict").splitlines() if line.endswith(": passed")]
        unchanged = sha(binary.read_bytes()) == binary_hash and all(sha((source / rel).read_bytes()) == row[1] for rel, row in SOURCE.items())
        item = {"argv": command, "timeout_seconds": seconds, "timed_out": timed_out, "started_utc": start,
                "finished_utc": datetime.now(timezone.utc).isoformat(), "wall_seconds": wall,
                "process_cpu_user_seconds": end_cpu.ru_utime - cpu.ru_utime,
                "process_cpu_system_seconds": end_cpu.ru_stime - cpu.ru_stime,
                "process_cpu_scope": "sequential RUSAGE_CHILDREN delta includes child threads; not calling-thread CPU",
                "required_registrations": sorted(expected), "observed_passed_registrations": sorted(observed),
                "exit_code": code, "binary_and_source_unchanged": unchanged,
                "stdout_sha256": sha(stdout), "stderr_sha256": sha(stderr),
                "stdout": label + ".stdout", "stderr": label + ".stderr",
                "pass": code == 0 and not timed_out and unchanged and len(observed) == len(set(observed)) and set(observed) == set(expected)}
        result[label] = item
        write_json(report / "run.json", result)
        return item
    # External diagnostic watchdogs do not change any test assertion or physical deadline.
    focused = run_one("focused", [str(binary), "native peak"], 900, FOCUSED_CASES)
    if not focused["pass"]:
        raise SystemExit(focused["exit_code"] or 1)
    full = run_one("full", [str(binary)], 1800, full_inventory)
    if not full["pass"]:
        raise SystemExit(full["exit_code"] or 1)
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    prepare_parser = sub.add_parser("prepare")
    prepare_parser.add_argument("--root", type=Path, required=True)
    prepare_parser.add_argument("--report", type=Path, required=True)
    prepare_parser.add_argument("--patch", type=Path, required=True)
    run_parser = sub.add_parser("run")
    run_parser.add_argument("--root", type=Path, required=True)
    run_parser.add_argument("--report", type=Path, required=True)
    run_parser.add_argument("--binary", type=Path, required=True)
    run_parser.add_argument("--compiler", default="g++-11")
    args = parser.parse_args()
    if args.command == "prepare":
        print(json.dumps(prepare(args.root, args.report, args.patch), sort_keys=True), flush=True)
    else:
        print(json.dumps(run_tests(args.root, args.report, args.binary, args.compiler), sort_keys=True), flush=True)


if __name__ == "__main__":
    main()
