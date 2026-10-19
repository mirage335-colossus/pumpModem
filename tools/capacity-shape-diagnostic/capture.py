#!/usr/bin/env python3
"""Private runner helper for the one-process capacity-shape diagnostic."""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import resource
import runpy
import shutil
import subprocess
import tarfile
import time

BASE_COMMIT = "81c7da84121be60fe0ce1e7252e94cdf952773e8"
# Root must replace these only after the producer and its accepted read dependencies settle.
# The helper refuses preparation while any required hash remains unbound.
REQUIRED_PINS = {'capacity_shape_prepare.py': 'fe7fc20ab1bae5c6b1f693bb1ef29dcf408d9426a991305feb972949e33bf8b4', 'owner_census_prepare.py': 'd627abbccbddf9ead52279ff970e8a06c4a540c25936108f4af651d52bebedfd', 'owner_census_extension.py': '733194a043848d5a3492539b1fd8443b191f246c0f46edea29fdb3cfa7e141aa', 'owner_census_probe.hpp': '5b22ccbd23aebdb9e7ca3296348c8d4c32b6607e3270fb0f2f0d7f96597068e6', 'private_owner_census_probe.hpp': '74dda392f90d2b8f5b968e727bb228dcdf81a1bbc1b7ca357f946dc3af50b3bb', 'capacity_shape_probe.hpp': '354cc86b5c1e82a2c2393e74a9c0121ca286c6440124c2ba88ef49af6eb871a3', 'capacity_cpu_probe.hpp': '94af50a6a3fda897e845fe9eba7ffe20c8624f3bd092827b27a4b79393c8be78', 'cpu-contract.json': 'a483f4b9566861fb47cee5d184da64139cddbf61d20fd1515735149be04dfb94'}
EXPECTED_CPU_PARTITION_CONTRACT = {'schema': 1, 'mode': 'exact_coarse', 'profile_environment': {'DATAPUMP_CAPACITY_CPU_DIAGNOSTIC': '1', 'DATAPUMP_RX_CPU_PROFILE': 'coarse'}, 'stages_added': ['ordinary_acquisition', 'ordinary_continuation', 'ordinary_source_generation'], 'worker_stage': 'submitting active frame stage captured alongside collector; serial submissions inherit current frame', 'events': ['receiver_component_cpu', 'capacity_cpu_case_cpu', 'capacity_cpu_work', 'capacity_cpu_summary'], 'case_snapshots': 'cumulative explicit begin/end markers; active_contexts and quiescent flags authoritative; nonquiescent differences are not exact per-case deltas', 'final_cpu_scope': 'existing Session destructor after task joins; cumulative exclusive native-thread CPU across caller and pool workers; includes clock-read overhead', 'work_scope': 'original native fft_work counters from active post-push snapshots plus exact destruction snapshots; final all-destroyed totals; partial execution CPU is included but interrupted jobs may not increment original completion counters; failed-constructor counters unavailable; threshold trials can roll back at EOF', 'work_owner_cap': 1024, 'work_point_cap': 512, 'pcm_byte_cap': 1048576, 'pcm_partition_cap': 8192, 'pcm_environment': 'DATAPUMP_CAPACITY_CPU_PCM', 'pcm_path': 'explicit runtime output path', 'pcm_partitions_suffix': '.partitions.json', 'pcm_scope': 'generated channel output before RX delivery; partitions preserve source coordinates and normal/partial read outcome; no claim every lane committed', 'calibration': '6 batches of10000 native thread CPU clock reads after Session joins; median cost times actual profiler clock_reads is an estimate, not a bound', 'overhead_limit': 'natural origin differs from prior runs; no matched overall overhead claim; observer subsets and clock calibration exclude trace writer and other context overhead', 'interpretation': 'diagnostic only; original512MiB fixture and120s assertions unchanged; no32MiB or application qualification', 'pcm_partition_fields': {'first': 'u64 source coordinate', 'count': 'u64 generated count', 'file_sample_offset': 'u64 sample offset in concatenated PCM file', 'captured_count': 'u64 actual file samples written', 'case_id': '0 before raw,1 raw,2 short,3 framed', 'case_sequence': 'u64 most recent test begin/end marker at capture', 'noise': 'bool channel noise read', 'partial': 'bool original read threw after generating prefix'}, 'work_complete_semantics': 'all observed fully constructed native owner lifetimes closed without map/overflow/fallback omissions; not a guarantee that counters enumerate every interrupted arithmetic operation', 'pcm_path_character_cap': 1024, 'pcm_channel_identity': 'one positive Session continuous channel; no configure between raw/short/framed; case/source coordinates join existing capacity_shape_channel_origin events'}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n")


def git(root: Path, *args: str) -> list[str]:
    return ["git", "-c", "safe.directory=" + str(root), *args]


def extract_checked(archive: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=False)
    with tarfile.open(archive) as captured:
        for member in captured.getmembers():
            path = Path(member.name)
            if path.is_absolute() or ".." in path.parts:
                raise ValueError("unsafe source archive member")
            if member.issym() or member.islnk():
                target = (destination / path.parent / member.linkname).resolve()
                if not target.is_relative_to(destination.resolve()):
                    raise ValueError("source archive link escapes source")
        captured.extractall(destination)


def prepare(root: Path, report: Path) -> dict:
    root = root.resolve(strict=True)
    report = report.resolve()
    if any(value == "PENDING_ROOT_REVIEW" for value in REQUIRED_PINS.values()):
        raise ValueError("required producer pins are pending final root review")
    if report.exists():
        raise ValueError("diagnostic report path must be fresh")
    commit = subprocess.check_output(git(root, "rev-parse", "HEAD"), cwd=root, text=True).strip()
    if len(commit) != 40 or any(character not in "0123456789abcdef" for character in commit):
        raise ValueError("invalid launcher commit identity")
    subprocess.run(git(root, "merge-base", "--is-ancestor", BASE_COMMIT, commit), cwd=root, check=True)
    changed_paths = subprocess.check_output(git(root, "diff", "--name-only", BASE_COMMIT + ".." + commit), cwd=root, text=True).splitlines()
    if not changed_paths or any(path != ".github/workflows/native-linux.yml" and
                                 not path.startswith("tools/capacity-shape-diagnostic/") for path in changed_paths):
        raise ValueError("launcher diff includes files outside approved CI wiring/helper scope")
    status = subprocess.check_output(git(root, "status", "--porcelain=v1", "--untracked-files=all"), cwd=root, text=True)
    if status:
        raise ValueError("capacity-shape workflow requires a clean launcher checkout")

    report.mkdir(parents=True, exist_ok=False)
    source = root / "build/capacity-shape-source"
    if source.exists():
        raise ValueError("private compiled source directory already exists")
    archive_hash = hashlib.sha256()
    archive_path = report / ".source-archive.tar"
    with archive_path.open("xb") as output:
        result = subprocess.run(git(root, "archive", "HEAD"), cwd=root, stdout=output, check=False)
        if result.returncode:
            raise subprocess.CalledProcessError(result.returncode, result.args)
    archive_hash = sha(archive_path.read_bytes())
    extract_checked(archive_path, source)
    archive_path.unlink()

    evidence = report / "overlay-evidence"
    producer = source / "tools/capacity-shape-diagnostic/prepare.py"
    subprocess.run(["python3", "-B", str(producer), "--source-root", str(source), "--output-dir", str(evidence)],
                   cwd=root, check=True)
    manifest_path = evidence / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("schema") != 1 or manifest.get("status") != "private_preparation_only" or manifest.get("base_commit") != BASE_COMMIT:
        raise ValueError("capacity-shape producer manifest identity rejected")
    if manifest.get("diagnostic_environment") != {
            "DATAPUMP_CAPACITY_SHAPE_DIAGNOSTIC": "1", "DATAPUMP_OWNER_CENSUS_DIAGNOSTIC": "1",
            **EXPECTED_CPU_PARTITION_CONTRACT["profile_environment"]}:
        raise ValueError("producer diagnostic environment contract differs")
    if manifest.get("settings") != {"settings_dsp_workspace_bytes": 536870912, "transfer_dsp_workspace_bytes": 536870912}:
        raise ValueError("producer workspace fixture differs")
    if manifest.get("runner_argv") != ["test_live", "--simulation-media-clock-trace", "<trace-path>"]:
        raise ValueError("producer test invocation differs")
    if manifest.get("fixture_invariants") != [
            "original natural epoch clock and key", "original geometry/search/inputs/assertions",
            "instrumented sampled-work budget remains 120s", "generated samples are not transmitted samples"]:
        raise ValueError("producer fixture invariants differ")
    if manifest.get("timing_scope") != "capacity-shape observer wall and calling-thread CPU only; census separately scoped; trace writer and whole-fixture overhead not measured":
        raise ValueError("producer timing scope differs")
    if manifest.get("interpretation_limit") != "512MiB changes strategy/backend/cache/bit grants; natural-origin result is a diagnostic capacity-shape wave, not matched performance comparison or qualification":
        raise ValueError("producer interpretation boundary differs")
    if manifest.get("capture_contract") != {
            "aggregate_event": "capacity_shape_point", "summary_event": "capacity_shape_summary",
            "first_refusal": "existing schema-2 owner_census producer, semantics unchanged",
            "owner_cap": 512, "journal_cap": 512, "aggregate_event_cap": 512,
            "signature_cap": 32, "truncation_explicit": True, "peak_and_end": "owner/bytes peaks plus per-case endpoint"}:
        raise ValueError("producer capture limits or semantics differ")
    partition_contract = manifest.get("cpu_partition_contract")
    if partition_contract != EXPECTED_CPU_PARTITION_CONTRACT:
        raise ValueError("CPU partition contract differs from the reviewed exact contract")
    source_closure = manifest.get("source_closure")
    required_closure = {"src/live.cpp", "tests/test_live.cpp",
                        "src/scheduled_work_profile.hpp", "src/search_parallel.cpp"}
    if not isinstance(source_closure, dict) or not required_closure.issubset(source_closure):
        raise ValueError("producer source closure is missing required fixture or CPU-profile sources")
    for relative, digest in source_closure.items():
        item_path = Path(relative)
        if item_path.is_absolute() or ".." in item_path.parts or item_path.parts[0] not in ("src", "tests"):
            raise ValueError("unsafe or unexpected source closure path: " + relative)
    canonical_root = source / "tools/owner-census-diagnostic"
    evidence_files = {
        "capacity_shape_prepare.py": source / "tools/capacity-shape-diagnostic/prepare.py",
        "owner_census_prepare.py": canonical_root / "prepare.py",
        "owner_census_extension.py": canonical_root / "ledger_extension.py",
        "owner_census_probe.hpp": canonical_root / "owner_census_probe.hpp",
        "private_owner_census_probe.hpp": evidence / "prepared-source/src/owner_census_probe.hpp",
        "capacity_shape_probe.hpp": source / "tools/capacity-shape-diagnostic/capacity_shape_probe.hpp",
        "capacity_cpu_probe.hpp": evidence / "prepared-source/src/capacity_cpu_probe.hpp",
        "cpu-contract.json": source / "tools/capacity-shape-diagnostic/cpu-contract.json",
    }
    canonical_hashes = manifest.get("canonical_tool_hashes")
    if manifest.get("canonical_tool_root") != "tools/owner-census-diagnostic" or not isinstance(canonical_hashes, dict):
        raise ValueError("canonical owner-census tool contract differs")
    if manifest.get("owner_cap_derivation") != "canonical cap changed 128->512 and row cap 2400->11000; same producer code":
        raise ValueError("owner-census capacity derivation differs")
    for name, path in evidence_files.items():
        digest = sha(path.read_bytes())
        if digest != REQUIRED_PINS[name]:
            raise ValueError("required producer pin mismatch: " + name)
        if name == "capacity_shape_prepare.py" and manifest.get("tool_sha256") != digest:
            raise ValueError("producer tool hash in manifest differs")
        canonical_manifest_names = {
            "owner_census_prepare.py": "prepare.py",
            "owner_census_extension.py": "ledger_extension.py",
            "owner_census_probe.hpp": "owner_census_probe.hpp",
        }
        canonical_name = canonical_manifest_names.get(name)
        if canonical_name and canonical_hashes.get(canonical_name) != digest:
            raise ValueError("canonical manifest tool hash differs: " + canonical_name)
    if manifest.get("owner_census_support_sha256") != REQUIRED_PINS["private_owner_census_probe.hpp"]:
        raise ValueError("derived private owner-census support pin differs")
    if manifest.get("capacity_shape_support_sha256") != REQUIRED_PINS["capacity_shape_probe.hpp"]:
        raise ValueError("capacity-shape support pin differs")
    if manifest.get("cpu_contract_sha256") != REQUIRED_PINS["cpu-contract.json"]:
        raise ValueError("capacity-CPU contract byte identity differs")
    if manifest.get("capacity_cpu_support_sha256") != REQUIRED_PINS["capacity_cpu_probe.hpp"]:
        raise ValueError("capacity-CPU support pin differs")
    for relative, digest in source_closure.items():
        if sha((source / relative).read_bytes()) != digest:
            raise ValueError("archived application source closure changed: " + relative)
    source_changes = manifest.get("sources")
    required_overlay_sources = {"src/live.cpp", "tests/test_live.cpp",
                                "src/scheduled_work_profile.hpp", "src/search_parallel.cpp"}
    if not isinstance(source_changes, dict) or not required_overlay_sources.issubset(source_changes):
        raise ValueError("producer source overlay inventory is missing required fixture or CPU-profile sources")
    if not set(source_changes).issubset(source_closure):
        raise ValueError("producer overlay source is absent from pinned source closure")
    reverse_paths = {}
    for relative in source_changes:
        item_path = Path(relative)
        if item_path.is_absolute() or ".." in item_path.parts or item_path.parts[0] not in ("src", "tests"):
            raise ValueError("unsafe or unexpected overlay source path: " + relative)
        filename = item_path.name
        stem, suffix = (filename.rsplit(".", 1) + [""])[:2] if "." in filename else (filename, "")
        reverse_paths[relative] = stem + ".reversed" + ("." + suffix if suffix else "")
    for relative in sorted(source_changes):
        item = source_changes[relative]
        if item.get("before_sha256") != source_closure[relative]:
            raise ValueError("producer overlay preimage differs from source closure: " + relative)
        prepared = evidence / "prepared-source" / relative
        reverse = evidence / reverse_paths[relative]
        if sha(prepared.read_bytes()) != item.get("after_sha256"):
            raise ValueError("prepared source hash differs: " + relative)
        if sha(reverse.read_bytes()) != item.get("reverse_sha256") or item.get("reverse_sha256") != item.get("before_sha256"):
            raise ValueError("reversible source hash differs: " + relative)
        shutil.copyfile(prepared, source / relative)
    for name, manifest_key in (("owner_census_probe.hpp", "owner_census_support_sha256"),
                               ("capacity_shape_probe.hpp", "capacity_shape_support_sha256"),
                               ("capacity_cpu_probe.hpp", "capacity_cpu_support_sha256")):
        support = evidence / "prepared-source/src" / name
        expected_hash = manifest.get(manifest_key)
        if not support.is_file() or sha(support.read_bytes()) != expected_hash:
            raise ValueError("prepared diagnostic support hash differs: " + name)
        shutil.copyfile(support, source / "src" / name)
    compiled_paths = set(source_changes) | {"src/owner_census_probe.hpp", "src/capacity_shape_probe.hpp", "src/capacity_cpu_probe.hpp"}
    compiled = {relative: sha((source / relative).read_bytes()) for relative in sorted(compiled_paths)}
    for relative in sorted(source_changes):
        if compiled[relative] != source_changes[relative]["after_sha256"]:
            raise ValueError("compiled overlay does not match producer output: " + relative)
    if (compiled["src/owner_census_probe.hpp"] != REQUIRED_PINS["private_owner_census_probe.hpp"] or
            compiled["src/capacity_shape_probe.hpp"] != REQUIRED_PINS["capacity_shape_probe.hpp"] or
            compiled["src/capacity_cpu_probe.hpp"] != REQUIRED_PINS["capacity_cpu_probe.hpp"]):
        raise ValueError("compiled diagnostic support differs from accepted pins")
    write_json(report / "source-archive.json", {
        "launcher_source_commit": commit,
        "application_source_commit": BASE_COMMIT,
        "base_commit_is_ancestor": True,
        "checked_launcher_diff_paths": changed_paths,
        "source_closure_sha256": dict(source_closure),
        "archive_sha256": archive_hash,
        "compiled_source_directory": str(source),
        "compiled_overlay_sha256": compiled,
        "classification": "private reversible Symbol_Burst capacity-shape and CPU-partition diagnostic overlay; not qualification",
    })
    return manifest


def cache_value(lines: list[str], key: str) -> str:
    matches = [line.split("=", 1)[1] for line in lines if line.startswith(key + ":")]
    if len(matches) != 1:
        raise ValueError("missing or duplicate CMake cache key: " + key)
    return matches[0]


def capture_artifact(path: Path, max_bytes: int | None = None) -> dict:
    result = {"path": str(path), "status": "absent", "size_bytes": None, "sha256": None}
    try:
        try:
            size = path.stat().st_size
        except FileNotFoundError:
            return result
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        result.update({"status": "overflow" if max_bytes is not None and size > max_bytes else "present",
                       "size_bytes": size, "sha256": digest.hexdigest()})
    except OSError as error:
        result.update({"status": "error", "error": type(error).__name__ + ": " + str(error)})
    return result


def capture(root: Path, report: Path, timeout_seconds: int) -> dict:
    root, report = root.resolve(strict=True), report.resolve(strict=True)
    manifest = json.loads((report / "overlay-evidence/manifest.json").read_text())
    archive_record = json.loads((report / "source-archive.json").read_text())
    current_commit = subprocess.check_output(git(root, "rev-parse", "HEAD"), cwd=root, text=True).strip()
    if archive_record["application_source_commit"] != BASE_COMMIT or archive_record["launcher_source_commit"] != current_commit:
        raise ValueError("compiled source or launcher identity mismatch")
    if manifest.get("source_closure") != archive_record.get("source_closure_sha256"):
        raise ValueError("producer source closure changed after archive preparation")
    source = Path(archive_record["compiled_source_directory"]).resolve(strict=True)
    binary = root / "build/test_live"
    cache_path = root / "build/CMakeCache.txt"
    cache_lines = cache_path.read_text().splitlines()
    config = {key: cache_value(cache_lines, key) for key in (
        "CMAKE_BUILD_TYPE", "DATAPUMP_SANITIZERS", "DATAPUMP_PORTABLE", "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER",
        "CMAKE_HOME_DIRECTORY")}
    if Path(config["CMAKE_HOME_DIRECTORY"]).resolve() != source:
        raise ValueError("configured CMake source is not the compiled private archive")
    overlay_inventory = json.loads((report / "overlay-evidence/manifest.json").read_text())["sources"]
    if set(overlay_inventory) != set(manifest["sources"]):
        raise ValueError("producer source inventory changed after preparation")
    expected_compiled_paths = set(overlay_inventory) | {"src/owner_census_probe.hpp", "src/capacity_shape_probe.hpp", "src/capacity_cpu_probe.hpp"}
    recorded_overlay = archive_record["compiled_overlay_sha256"]
    if set(recorded_overlay) != expected_compiled_paths:
        raise ValueError("recorded compiled overlay inventory differs from producer")
    recomputed_overlay = {relative: sha((source / relative).read_bytes()) for relative in sorted(expected_compiled_paths)}
    if recomputed_overlay != recorded_overlay:
        raise ValueError("compiled overlay bytes changed since archive preparation")
    for relative, item in overlay_inventory.items():
        if recomputed_overlay[relative] != item.get("after_sha256"):
            raise ValueError("compiled source differs from producer postimage: " + relative)
    if (recomputed_overlay["src/owner_census_probe.hpp"] != manifest.get("owner_census_support_sha256") or
            recomputed_overlay["src/capacity_shape_probe.hpp"] != manifest.get("capacity_shape_support_sha256") or
            recomputed_overlay["src/capacity_cpu_probe.hpp"] != manifest.get("capacity_cpu_support_sha256")):
        raise ValueError("compiled support headers differ from producer manifest")
    if config["CMAKE_BUILD_TYPE"] != "Debug" or config["DATAPUMP_SANITIZERS"] != "ON" or config["DATAPUMP_PORTABLE"] != "OFF":
        raise ValueError("capacity-shape diagnostic requires Debug, ASan/UBSan, Portable OFF")
    c_compiler = shutil.which("gcc-11")
    compiler = shutil.which("g++-11")
    if (not compiler or not c_compiler or
            Path(config["CMAKE_CXX_COMPILER"]).resolve() != Path(compiler).resolve() or
            Path(config["CMAKE_C_COMPILER"]).resolve() != Path(c_compiler).resolve()):
        raise ValueError("compiler cache does not select gcc-11/g++-11")
    c_compiler_version = subprocess.run([c_compiler, "--version"], cwd=root, capture_output=True, text=True)
    compiler_version = subprocess.run([compiler, "--version"], cwd=root, capture_output=True, text=True)
    environment_keys = ("PATH", "LANG", "LC_ALL", "TZ", "ASAN_OPTIONS", "UBSAN_OPTIONS", "RUNNER_OS", "RUNNER_ARCH")
    environment = {key: os.environ[key] for key in environment_keys if key in os.environ}
    environment["DATAPUMP_CAPACITY_SHAPE_DIAGNOSTIC"] = "1"
    environment["DATAPUMP_OWNER_CENSUS_DIAGNOSTIC"] = "1"
    environment["DATAPUMP_CAPACITY_CPU_DIAGNOSTIC"] = "1"
    environment["DATAPUMP_RX_CPU_PROFILE"] = "coarse"
    outputs = report / "outputs"
    outputs.mkdir(parents=True, exist_ok=True)
    pcm_path = (outputs / "source-input.f32").resolve()
    partitions_path = Path(str(pcm_path) + EXPECTED_CPU_PARTITION_CONTRACT["pcm_partitions_suffix"])
    environment["DATAPUMP_CAPACITY_CPU_PCM"] = str(pcm_path)
    trace_path = (report / "outputs/capacity-shape.jsonl").resolve()
    command = [str(binary), "--simulation-media-clock-trace", str(trace_path)]
    child_environment = environment.copy()
    trace_seal = runpy.run_path(str(root / "tools/gui-stop-differential.py"))["trace_seal"]
    started_utc = datetime.now(timezone.utc).isoformat()
    started = time.monotonic()
    cpu_started = resource.getrusage(resource.RUSAGE_CHILDREN)
    timed_out = False
    try:
        completed = subprocess.run(command, cwd=root, capture_output=True, timeout=timeout_seconds, env=child_environment)
        stdout, stderr, actual_exit_code = completed.stdout, completed.stderr, completed.returncode
    except subprocess.TimeoutExpired as error:
        stdout, stderr, actual_exit_code, timed_out = error.stdout or b"", error.stderr or b"", 124, True
    elapsed = time.monotonic() - started
    cpu_finished = resource.getrusage(resource.RUSAGE_CHILDREN)
    finished_utc = datetime.now(timezone.utc).isoformat()
    (outputs / "capacity-shape.stdout.log").write_bytes(stdout)
    (outputs / "capacity-shape.stderr.log").write_bytes(stderr)
    validation = validation_error = None
    try:
        validation = trace_seal(trace_path, stderr.decode("utf-8", errors="strict"))
    except (OSError, UnicodeError, ValueError, KeyError, TypeError) as error:
        validation_error = type(error).__name__ + ": " + str(error)
    trace_complete = validation is not None and not timed_out
    trace_status = {
        "path": str(trace_path), "exists": trace_path.is_file(),
        "sha256": sha(trace_path.read_bytes()) if trace_path.is_file() else None,
        "validator": "tools/gui-stop-differential.py::trace_seal",
        "validation": validation, "validation_error": validation_error,
        "timed_out": timed_out, "trace_complete": trace_complete,
        "original_exit_authoritative": True,
    }
    write_json(report / "trace-status.json", trace_status)
    source_files = {name: sha((source / name).read_bytes()) for name in manifest["source_closure"]}
    source_files.update(archive_record["compiled_overlay_sha256"])
    source_files["tests/test_live.cpp"] = sha((source / "tests/test_live.cpp").read_bytes())
    binary_hash = sha(binary.read_bytes())
    pcm_artifact = capture_artifact(pcm_path, max_bytes=EXPECTED_CPU_PARTITION_CONTRACT["pcm_byte_cap"])
    partitions_artifact = capture_artifact(partitions_path)
    config_hash = sha(cache_path.read_bytes())
    env_hash = sha("\0".join(key + "=" + value for key, value in sorted(environment.items())).encode())
    identity = {
        "scope": "simulation-capacity-shape-diagnostic",
        "classification": "one original test_live simulation-media-clock trace process with private Symbol_Burst capacity-shape and CPU-partition instrumentation; diagnostic only, not qualification",
        "fixture": "one test_live --simulation-media-clock-trace subprocess; original assertions, natural origin, fixed input, wire geometry, and per-case deadlines retained",
        "source_commit": BASE_COMMIT, "launcher_source_commit": current_commit,
        "base_commit_is_ancestor": True,
        "checked_launcher_diff_paths": archive_record["checked_launcher_diff_paths"],
        "runner_helper_path": str(Path(__file__).resolve()),
        "runner_helper_sha256": sha(Path(__file__).read_bytes()),
        "overlay_manifest_path": str(report / "overlay-evidence/manifest.json"),
        "overlay_manifest_sha256": sha((report / "overlay-evidence/manifest.json").read_bytes()),
        "source_archive": archive_record, "overlay_manifest": manifest,
        "cpu_partition_contract": EXPECTED_CPU_PARTITION_CONTRACT,
        "cpu_partition_contract_sha256": sha(json.dumps(EXPECTED_CPU_PARTITION_CONTRACT, sort_keys=True,
                                                           separators=(",", ":")).encode()),
        "compiled_source_directory": str(source), "source_files_sha256": source_files,
        "configuration_path": str(cache_path), "configuration_sha256": config_hash,
        "configuration": config, "c_compiler_path": c_compiler, "compiler_path": compiler,
        "c_compiler_version": c_compiler_version.stdout.strip(),
        "c_compiler_version_exit": c_compiler_version.returncode,
        "compiler_version": compiler_version.stdout.strip(), "compiler_version_exit": compiler_version.returncode,
        "binary_path": str(binary), "binary_sha256": binary_hash,
        "build_command": ["cmake", "--build", "build", "--target", "test_live"],
        "command": command, "cwd": str(root), "environment": environment, "environment_sha256": env_hash,
        "started_utc": started_utc, "finished_utc": finished_utc,
        "timeout_seconds": timeout_seconds, "timed_out": timed_out,
        "actual_exit_code": actual_exit_code, "wall_seconds": elapsed,
        "process_cpu_user_seconds": cpu_finished.ru_utime - cpu_started.ru_utime,
        "process_cpu_system_seconds": cpu_finished.ru_stime - cpu_started.ru_stime,
        "process_cpu_seconds": cpu_finished.ru_utime + cpu_finished.ru_stime - cpu_started.ru_utime - cpu_started.ru_stime,
        "process_cpu_scope": "RUSAGE_CHILDREN difference around the one subprocess, includes child threads",
        "stdout_path": str(outputs / "capacity-shape.stdout.log"), "stdout_sha256": sha(stdout),
        "stderr_path": str(outputs / "capacity-shape.stderr.log"), "stderr_sha256": sha(stderr),
        "trace_path": str(trace_path), "trace_complete": trace_complete,
        "capacity_cpu_input": {"pcm": pcm_artifact, "partitions": partitions_artifact,
                               "pcm_limit_bytes": EXPECTED_CPU_PARTITION_CONTRACT["pcm_byte_cap"],
                               "pcm_partition_cap": EXPECTED_CPU_PARTITION_CONTRACT["pcm_partition_cap"],
                               "status": "captured_within_limit" if pcm_artifact["status"] == partitions_artifact["status"] == "present" else "absent" if pcm_artifact["status"] == partitions_artifact["status"] == "absent" else "incomplete_capture" if "absent" in (pcm_artifact["status"], partitions_artifact["status"]) else "capture_error_or_overflow"},
        "preservation": "original assertion outcome and exit code remain authoritative even when trace seals; 120-second instrumented workload and physical deadlines are unchanged",
    }
    write_json(report / "run.json", identity)
    print(json.dumps(identity), flush=True)
    if actual_exit_code:
        raise SystemExit(actual_exit_code)
    if not trace_complete:
        raise SystemExit(1)
    if pcm_artifact["status"] == "overflow" or pcm_artifact["status"] == "error" or partitions_artifact["status"] == "error":
        raise SystemExit(1)
    raise SystemExit(0)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    prepare_parser = sub.add_parser("prepare")
    prepare_parser.add_argument("--root", required=True, type=Path)
    prepare_parser.add_argument("--report", required=True, type=Path)
    capture_parser = sub.add_parser("capture")
    capture_parser.add_argument("--root", required=True, type=Path)
    capture_parser.add_argument("--report", required=True, type=Path)
    capture_parser.add_argument("--timeout-seconds", type=int, default=600)
    args = parser.parse_args()
    if args.command == "prepare":
        print(json.dumps(prepare(args.root, args.report), indent=2), flush=True)
    else:
        if args.timeout_seconds != 600:
            raise ValueError("the external process-closure limit is fixed at 600 seconds")
        capture(args.root, args.report, args.timeout_seconds)


if __name__ == "__main__":
    main()
