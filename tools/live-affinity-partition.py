#!/usr/bin/env python3
"""ABBA affinity partition for the retained original symbol-burst ELF.

This is a diagnostic only. It never builds or modifies application sources.
The hosted invocation fetches one immutable GitHub artifact, verifies its
provenance and outer digest, then extracts only the pinned original ELF and
its manifest before four no-argument runs (2, 4, 4, 2 allowed CPUs).
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import shutil
import stat
import sys
import zipfile

RUN_ID = 39627328198
ARTIFACT_ID = 12357721944
ARTIFACT_NAME = "CI-Linux-Release-live-drain-diagnostic-all-attempt-1"
ARTIFACT_BYTES = 17620607
ARTIFACT_SHA256 = "5ff8adc935f23583a1a351ec5d5186313f1bf5e3419abdb55f19e66482ab1ea1"
SOURCE_COMMIT = "f2ff3a2309705cf4088417d0da6bb30a110ae6c0"
WORKFLOW_PATH = ".github/workflows/native-linux.yml"
PRODUCER_JOB_ID = 119315857529
ELF_SHA256 = "1f738eedf7d3c0ad086726ccbfe57cf8568879eebfc2dd186c2dcb84e9c04913"
ELF_BYTES = 11426072
SOURCE_SHA256 = "d53b9a13ed76bc2dffc366d6980b5d57303fec5955755a832ad4ec971b87de68"
MANIFEST_SHA256 = "5babcd6bd58c421337c2c5830a02d6fdb5a31cc8941ba91980876393af3c6eef"
MANIFEST_BYTES = 5522
OUTER_MEMBER = "__w/pumpModem/pumpModem/build/symbol-burst-live-qualification/live-drain/original-reproducer/"
ELF_MEMBER = OUTER_MEMBER + "test_symbol_burst_live"
MANIFEST_MEMBER = OUTER_MEMBER + "manifest.json"
KNOWN_FAILURE = "capture did not drain"
TRIALS = (("trial-1-2cpu", 2), ("trial-2-4cpu", 4),
          ("trial-3-4cpu", 4), ("trial-4-2cpu", 2))
MIB = 1024 * 1024
OUTPUT_LIMIT = 768 * MIB


def compatible_sha(path):
    """Ubuntu 22.04 ships Python 3.10, without hashlib.file_digest."""
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def module_from(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def cpu_environment():
    allowed = sorted(os.sched_getaffinity(0))
    topology = {}
    for cpu in allowed:
        base = Path("/sys/devices/system/cpu") / ("cpu" + str(cpu)) / "topology"
        try:
            topology[str(cpu)] = {
                "physical_package_id": (base / "physical_package_id").read_text().strip(),
                "core_id": (base / "core_id").read_text().strip(),
            }
        except OSError:
            topology[str(cpu)] = {"unavailable": True}
    quota = {}
    for name in ("cpu.max", "cpu.stat", "cpuset.cpus.effective"):
        path = Path("/sys/fs/cgroup") / name
        try:
            quota[name] = path.read_text().strip()
        except OSError:
            quota[name] = None
    return {
        "allowed_cpus": allowed,
        "allowed_cpu_count": len(allowed),
        "topology": topology,
        "cgroup_v2": quota,
        "cpu_count": os.cpu_count(),
        "sched_getaffinity_count": len(allowed),
        "libc": platform.libc_ver(),
    }


def available_memory():
    for line in Path("/proc/meminfo").read_text().splitlines():
        if line.startswith("MemAvailable:"):
            return int(line.split()[1]) * 1024
    raise RuntimeError("MemAvailable unavailable")


def verify_provenance(capture, scratch):
    base = "repos/" + os.environ["GITHUB_REPOSITORY"] + "/actions/"
    artifact = json.loads(capture.api(base + "artifacts/" + str(ARTIFACT_ID), scratch,
                                      "artifact-api").read_text())
    require(artifact.get("id") == ARTIFACT_ID and artifact.get("name") == ARTIFACT_NAME and
            artifact.get("size_in_bytes") == ARTIFACT_BYTES and artifact.get("expired") is False and
            artifact.get("digest") == "sha256:" + ARTIFACT_SHA256 and
            artifact.get("workflow_run", {}).get("id") == RUN_ID and
            artifact.get("workflow_run", {}).get("head_sha") == SOURCE_COMMIT,
            "GitHub artifact API identity/digest mismatch")
    run_info = json.loads(capture.api(base + "runs/" + str(RUN_ID), scratch,
                                      "producer-run-api").read_text())
    require(run_info.get("id") == RUN_ID and run_info.get("head_sha") == SOURCE_COMMIT and
            run_info.get("path") == WORKFLOW_PATH,
            "GitHub producer workflow identity mismatch")
    job = json.loads(capture.api(base + "jobs/" + str(PRODUCER_JOB_ID), scratch,
                                 "producer-job-api").read_text())
    require(job.get("run_id") == RUN_ID and job.get("head_sha") == SOURCE_COMMIT and
            job.get("status") == "completed" and job.get("conclusion") == "success",
            "original retained ELF producer job did not pass")
    return {
        "artifact": {key: artifact.get(key) for key in
                     ("id", "name", "size_in_bytes", "expired", "digest", "workflow_run")},
        "run": {key: run_info.get(key) for key in ("id", "head_sha", "path", "conclusion")},
        "producer_job": {key: job.get(key) for key in ("id", "run_id", "head_sha", "status", "conclusion", "name")},
    }


def verify_archive(archive, scratch, capture):
    require(archive.is_file() and archive.stat().st_size == ARTIFACT_BYTES and
            capture.sha(archive) == ARTIFACT_SHA256,
            "retained artifact ZIP size or SHA-256 mismatch")
    with zipfile.ZipFile(archive) as package:
        inventory = capture.zip_inventory(package)
        members = {item["name"]: item for item in inventory}
        require(ELF_MEMBER in members and MANIFEST_MEMBER in members,
                "pinned original reproducer members are missing")
        manifest_bytes = package.read(MANIFEST_MEMBER)
        require(len(manifest_bytes) == MANIFEST_BYTES and
                hashlib.sha256(manifest_bytes).hexdigest() == MANIFEST_SHA256,
                "original reproducer manifest size or SHA-256 mismatch")
        manifest = json.loads(manifest_bytes)
        require(manifest.get("schema") == "paced-symbol-burst-original-context-reproducer-v1" and
                manifest.get("classification") == "diagnostic artifact; not qualification or release content" and
                manifest.get("source_commit") == SOURCE_COMMIT and
                manifest.get("test_target") == "test_symbol_burst_live" and
                manifest.get("test_source_sha256") == SOURCE_SHA256 and
                manifest.get("retained_elf_sha256") == ELF_SHA256 and
                manifest.get("built_elf_sha256") == ELF_SHA256,
                "original reproducer manifest identity mismatch")
        pin = manifest.get("files", {}).get("test_symbol_burst_live", {})
        require(pin == {"sha256": ELF_SHA256, "size_bytes": ELF_BYTES},
                "manifest does not bind exact original ELF size/hash")
        require(manifest.get("runtime_dependencies") == [] and
                manifest.get("unresolved_dependencies") == [] and
                manifest.get("host_provided_glibc_abi_dependencies") ==
                ["ld-linux-x86-64.so.2", "libc.so.6", "libm.so.6"],
                "retained ELF runtime inventory differs from pinned original")
        elf_info = package.getinfo(ELF_MEMBER)
        require(elf_info.file_size == ELF_BYTES and
                stat.S_IMODE(elf_info.external_attr >> 16) & 0o111,
                "original ELF member size or executable mode mismatch")
        target = scratch / "original-reproducer"
        target.mkdir()
        for member, filename in ((MANIFEST_MEMBER, "manifest.json"),
                                 (ELF_MEMBER, "test_symbol_burst_live")):
            path = target / filename
            with package.open(member) as source, path.open("xb") as destination:
                shutil.copyfileobj(source, destination, 1024 * 1024)
            if member == ELF_MEMBER:
                path.chmod(stat.S_IMODE(package.getinfo(member).external_attr >> 16))
                require(path.read_bytes()[:4] == b"\x7fELF" and
                        path.stat().st_size == ELF_BYTES and capture.sha(path) == ELF_SHA256,
                        "extracted file is not the exact pinned ELF")
                require(os.access(path, os.X_OK), "extracted original ELF lost executable mode")
        require(hashlib.sha256(manifest_bytes).hexdigest() ==
                hashlib.sha256((target / "manifest.json").read_bytes()).hexdigest(),
                "extracted manifest differs from archive member")
    return target / "test_symbol_burst_live", manifest


def copy_hosted_elf(source, destination, capture):
    """Use this same copy/mode path in offline checks and hosted trials."""
    shutil.copyfile(source, destination)
    destination.chmod(stat.S_IMODE(source.stat().st_mode))
    require(destination.is_file() and os.access(destination, os.X_OK) and
            destination.stat().st_size == ELF_BYTES and capture.sha(destination) == ELF_SHA256,
            "hosted ELF copy does not preserve the pinned executable and mode")
    return destination


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path,
                        help="checkout containing tools/native-copy-smoke-capture.py")
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--scratch", required=True, type=Path)
    parser.add_argument("--local-archive", type=Path,
                        help="offline verification only; never launches the ELF")
    args = parser.parse_args()
    root, report, scratch = args.root.resolve(), args.report.resolve(), args.scratch.resolve()
    require(report in scratch.parents, "scratch must be inside the uploaded report directory")
    capture = module_from(root / "tools/native-copy-smoke-capture.py", "existing_native_capture")
    # Reuse established process-group caps, TERM/KILL and closure handling.
    capture.sha = compatible_sha
    require(not report.exists() and not scratch.exists(), "report and scratch paths must be new")
    report.mkdir(parents=True)
    scratch.mkdir(parents=True)
    if args.local_archive:
        elf, manifest = verify_archive(args.local_archive.resolve(), scratch, capture)
        hosted_copy = copy_hosted_elf(elf, report / "test_symbol_burst_live", capture)
        capture.save(report / "offline-archive-check.json", {
            "status": "verified_without_execution", "archive": capture.record(args.local_archive),
            "elf": capture.record(elf), "hosted_copy": capture.record(hosted_copy),
            "hosted_copy_executable": os.access(hosted_copy, os.X_OK), "manifest": manifest,
            "execution_performed": False,
        })
        print("Pinned artifact and selected ELF verified; no ELF execution performed.")
        return 0

    require(os.environ.get("GITHUB_REPOSITORY") == "mirage335-colossus/pumpModem",
            "unexpected GitHub repository")
    cpu = cpu_environment()
    if cpu["allowed_cpu_count"] < 4:
        capture.save(report / "decision.json", {
            "status": "diagnostic_unavailable",
            "reason": "runner exposes fewer than four allowed logical CPUs",
            "environment": cpu, "qualification": False,
        })
        print("Diagnostic unavailable: runner exposes fewer than four allowed logical CPUs.")
        return 75
    require(cpu["libc"][0] == "glibc" and cpu["libc"][1].startswith("2.35"),
            "hosted container must provide the retained Ubuntu 22.04 glibc 2.35 ABI")
    affinity2, affinity4 = cpu["allowed_cpus"][:2], cpu["allowed_cpus"][:4]
    capture.require(available_memory() >= 4 * 1024 ** 3,
                    "fresh MemAvailable below 4GiB launch guard")
    provenance = verify_provenance(capture, scratch)
    capture.save(report / "provenance.json", provenance)
    archive = capture.api("repos/" + os.environ["GITHUB_REPOSITORY"] + "/actions/artifacts/" +
                          str(ARTIFACT_ID) + "/zip", scratch, "retained-artifact-zip", ARTIFACT_BYTES)
    elf, manifest = verify_archive(archive, scratch, capture)
    trials = []
    capture.save(report / "plan.json", {
        "classification": "diagnostic only; not qualification",
        "source_commit": SOURCE_COMMIT, "artifact_id": ARTIFACT_ID,
        "artifact_sha256": ARTIFACT_SHA256, "original_elf_sha256": ELF_SHA256,
        "test_source_sha256": SOURCE_SHA256,
        "objective": "isolate default search affinity at 2 versus 4 allowed CPUs",
        "order": [label for label, _ in TRIALS], "affinity_2": affinity2,
        "affinity_4": affinity4, "nice": 10, "external_deadline_seconds": 600,
        "internal_test_budget_seconds": 25,
        "pcm_claim": "not directly captured for the original no-argument invocation; deterministic retained ELF/source identity binds application input",
        "environment": cpu, "manifest": manifest,
        "tooling": [capture.record(Path(__file__).resolve()),
                    capture.record(Path(__file__).resolve().parent / "observe-affinity.py"),
                    capture.record(root / "tools/native-copy-smoke-capture.py")],
    })
    hosted_copy = copy_hosted_elf(elf, report / "test_symbol_burst_live", capture)
    common_cwd = scratch / "same-working-directory"
    common_cwd.mkdir()
    for label, count in TRIALS:
        launch_memory = available_memory()
        capture.require(launch_memory >= 4 * 1024 ** 3,
                        "fresh MemAvailable below 4GiB pre-trial launch guard")
        affinity = affinity2 if count == 2 else affinity4
        trial_dir = report / label
        command = [sys.executable, str(Path(__file__).resolve().parent / "observe-affinity.py"),
                   str(hosted_copy), str(trial_dir),
                   ",".join(str(value) for value in affinity), str(common_cwd)]
        def watch():
            capture.require(available_memory() >= 1536 * 1024 ** 2,
                            "MemAvailable fell below 1.5GiB stop guard")
            capture.require(capture.bounded_tree(report) <= OUTPUT_LIMIT,
                            "diagnostic evidence exceeded 768MiB cap")
        result, _, stderr_path = capture.run(command, report, label, 600, watch=watch)
        observation_path = trial_dir / "observation.json"
        observation = json.loads(observation_path.read_text()) if observation_path.is_file() else None
        stderr = stderr_path.read_text(encoding="utf-8", errors="replace")
        known_failure = result["exit_code"] != 0 and KNOWN_FAILURE in stderr
        row = {"label": label, "requested_affinity": affinity, "requested_cpu_count": count,
               "memory_available_before_launch_bytes": launch_memory,
               "result": result, "observation": observation,
               "known_capture_did_not_drain_failure": known_failure}
        trials.append(row)
        capture.save(report / "results.json", trials)
        print(json.dumps({"label": label, "exit_code": result["exit_code"],
                          "known_failure": known_failure,
                          "wall_seconds": observation and observation.get("wall_seconds")}), flush=True)
        capture.require(result.get("capture_error") is None and result.get("process_group_closed"),
                        "capture or process-group closure failed; preserve evidence and stop")
        capture.require(observation and observation.get("observer_error") is None and
                        observation.get("child_reaped") and observation.get("affinity_verified") and
                        observation.get("nice_verified"),
                        "observer setup/affinity/nice verification failed; preserve evidence and stop")
        if result["exit_code"] != 0 and not known_failure:
            raise RuntimeError("unexpected assertion/setup failure; preserve evidence and stop")
    all_passed = all(row["result"]["exit_code"] == 0 for row in trials)
    capture.save(report / "decision.json", {
        "all_original_assertions_passed": all_passed,
        "all_runs_used_same_elf": all(row["observation"].get("binary_sha256") == ELF_SHA256 for row in trials),
        "comparison_order": [row["label"] for row in trials],
        "causal_conclusion": "pending strong-owner interpretation of ABBA results",
        "qualification": False,
    })
    files = [capture.record(path) for path in sorted(report.rglob("*")) if path.is_file()]
    capture.save(report / "capture-manifest.json", {
        "files": files, "trials": len(trials), "all_original_assertions_passed": all_passed,
        "qualification": False,
    })
    return 0 if all_passed else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print("live-affinity-partition: " + type(exc).__name__ + ": " + str(exc), file=sys.stderr)
        sys.exit(1)
