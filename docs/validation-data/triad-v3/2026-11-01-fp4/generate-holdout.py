#!/usr/bin/env python3
"""Generate the predeclared independent sampled-waveform FP4 holdout.

Payload generation is intentionally explicit: use --plan to inspect its cases,
seeds, and source binding; omit --plan only after the coordinator freezes the
receiver policy and directs this generator to run. Existing paths are never
overwritten. --resume accepts only complete captures whose source, binary,
configuration, manifest, and payload hashes match the exact prior run.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import re
import resource
import subprocess
import sys
import time


FROZEN_HASHES = {
    "tools/triad-long.cpp": "c07003b3234543508a3ec1730ea6246902828bd6a1e3b6d7a4f2b37356a10566",
    "include/datapump/triad_modem_v3.hpp": "e625915b0c0d1f29925d6a95e3b49d89efe44c8482eb13a9de6836de4d6e9c92",
    "src/triad_simulation_stream.cpp": "b270448dc776ee6594c121e2b10a2f4f6eba5fa966897fc1f3a7fc06af501450",
    "build/dev/triad_long": "89b4af004e4f5fc564acdcbd6b7c3849afc5803b503ca45c43ed343b7af6c591",
}
PLAN_SEED = 982173
EXPECTED_COUNTS = {"h1_short": 256, "h1_target_point": 320, "h0_null": 128,
                   "wrong_key": 32, "hf_hour": 16, "flat_day": 1}
STAGE_LIMIT_SECONDS = 1800
CHILD_LIMIT_SECONDS = 120
BASE = ["--mode", "export-sampled", "--rate", "3.6", "--added", "10",
        "--sample-rate", "64", "--threads", "2", "--chunk", "4096",
        "--block-seconds", "4", "--phase-diffusion", ".00872665",
        "--white-fm", ".002", "--flicker-fm", ".002", "--white-pm", ".01",
        "--epoch-offsets", "0,3600", "--epoch", "1800000000"]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_sources(source_root: Path, binary: Path) -> dict[str, str]:
    actual = {}
    expected = dict(FROZEN_HASHES)
    expected.pop("build/dev/triad_long")
    for relative, digest in expected.items():
        path = source_root / relative
        if not path.is_file():
            raise FileNotFoundError(path)
        actual[relative] = sha256(path)
        if actual[relative] != digest:
            raise ValueError(f"frozen source hash mismatch: {relative} expected {digest}, got {actual[relative]}")
    if not binary.is_file():
        raise FileNotFoundError(binary)
    actual["binary"] = sha256(binary)
    if actual["binary"] != FROZEN_HASHES["build/dev/triad_long"]:
        raise ValueError(f"frozen binary hash mismatch: expected {FROZEN_HASHES['build/dev/triad_long']}, got {actual['binary']}")
    return actual


def cases() -> list[dict]:
    result = []

    def add(name, group, seconds, cn0, control="signal", channel="none", basis="flat"):
        result.append(dict(name=name, group=group, seconds=seconds, cn0=cn0,
                           control=control, channel=channel, basis=basis))

    for cn0 in (-12, -11, -10, -9):
        for i in range(64):
            add(f"h1-short-cn{cn0:+03d}-r{i:03d}", "h1_short", 256, cn0)
    for i in range(320):
        add(f"h1-target-cn-08-r{i:03d}", "h1_target_point", 256, -8)
    for i in range(128):
        add(f"h0-null-cn-10-r{i:03d}", "h0_null", 256, -10, control="null")
    for i in range(32):
        add(f"wrong-key-cn-10-r{i:03d}", "wrong_key", 256, -10, control="wrong-key")
    for cn0 in (-20, -24):
        for i in range(8):
            add(f"hf-hour-cn{cn0:+03d}-r{i:02d}", "hf_hour", 3600, cn0,
                channel="hf", basis="two-ray")
    add("flat-day-cn-30-r00", "flat_day", 86400, -30)
    if len(result) != sum(EXPECTED_COUNTS.values()):
        raise AssertionError("holdout case counts differ from the frozen plan")

    rng = random.Random(PLAN_SEED)
    used: set[int] = set()
    for index, case in enumerate(result):
        roots = {}
        for name in ("key", "channel", "oscillator", "noise", "frontend"):
            value = rng.getrandbits(64)
            if value in used:
                raise AssertionError("independent seed root collision")
            used.add(value)
            roots[name] = value
        case["index"] = index
        case["bit"] = index % 2
        case["seeds"] = roots
        case["epoch"] = 1800000000
        case["block_seconds"] = 4
        case["sample_rate"] = 64
        case["inner_rate"] = 3.6
        case["added_symbols"] = 10
        case["cfo_grid_hz"] = [-0.01, 0.0, 0.01]
        case["lags_samples"] = [0, 1, 2]
        case["epochs_seconds"] = [0, 3600]
        case["phase_states"] = 512
        case["receiver_policy"] = "NVFP4 E2M1xUE4M3 group16; BM/BN/BK=128; warps4; stages2"
        case["physical_channel"] = case["channel"]
        case["channel_bases"] = case["basis"]
    return result


def environment(binary: Path) -> dict:
    result = dict(system=platform.system(), release=platform.release(),
                  machine=platform.machine(), python=platform.python_version(),
                  libc=dict(zip(("name", "version"), platform.libc_ver())))
    try:
        openssl = subprocess.run(["openssl", "version", "-v"], text=True,
                                 capture_output=True, timeout=5, check=False)
        result["openssl"] = openssl.stdout.strip() if openssl.returncode == 0 else "unavailable"
    except (OSError, subprocess.TimeoutExpired):
        result["openssl"] = "unavailable"
    dependencies = {}
    if platform.system() == "Linux":
        try:
            checked = subprocess.run(["ldd", str(binary)], text=True, capture_output=True,
                                     timeout=10, check=False)
            for line in checked.stdout.splitlines():
                match = re.match(r"\s*(\S+)\s+=>\s+(\S+)", line)
                if match:
                    dependencies[match.group(1)] = Path(match.group(2)).name
                else:
                    direct = re.match(r"\s*(\S+)\s+\(0x", line)
                    if direct:
                        dependencies[Path(direct.group(1)).name] = Path(direct.group(1)).name
            result["ldd_returncode"] = checked.returncode
        except (OSError, subprocess.TimeoutExpired):
            result["ldd_returncode"] = None
    result["linked_dependencies_sanitized"] = dependencies
    return result


def command_for(binary: Path, output: Path, case: dict) -> list[str]:
    seeds = case["seeds"]
    return [str(binary), *BASE, "--seconds", str(case["seconds"]),
            "--cn0", str(case["cn0"]), "--channel", case["channel"],
            "--basis", case["basis"], "--control", case["control"],
            "--seed", str(seeds["channel"]), "--key-seed", str(seeds["key"]),
            "--channel-seed", str(seeds["channel"]),
            "--oscillator-seed", str(seeds["oscillator"]),
            "--noise-seed", str(seeds["noise"]),
            "--frontend-seed", str(seeds["frontend"]),
            "--signal-bit", str(case["bit"]), "--output", str(output)]


def payload_hashes(output: Path) -> dict[str, str]:
    return {name: sha256(output/name) for name in
            ("manifest.json", "basis.c128", "observation.c128", "validity.u8")}


def validate_manifest(manifest: dict, case: dict) -> None:
    expected = {"format": "triad-sampled-waveforms-v1", "sample_rate": case["sample_rate"],
                "cn0_db_hz": case["cn0"], "channel": case["channel"],
                "control": case["control"], "epoch": case["epoch"],
                "signal_bit_simulation_truth": case["bit"],
                "signal_enabled": case["control"] != "null"}
    for name, value in expected.items():
        if manifest.get(name) != value:
            raise ValueError(f"manifest {name} differs from frozen case {case['name']}")
    if manifest.get("random_seed_roots", {}).get("channel") != case["seeds"]["channel"]:
        raise ValueError("manifest channel seed differs from frozen case")
    for name in ("oscillator", "noise", "frontend"):
        if manifest.get("random_seed_roots", {}).get(name) != case["seeds"][name]:
            raise ValueError(f"manifest {name} seed differs from frozen case")
    if manifest.get("key_seed") != case["seeds"]["key"]:
        raise ValueError("manifest key seed differs from frozen case")
    if manifest.get("block_samples") != case["sample_rate"]*case["block_seconds"]:
        raise ValueError("manifest correlation block differs from frozen four-second calendar")
    if manifest.get("requested_active_seconds") != case["seconds"]:
        raise ValueError("manifest requested duration differs from frozen case")
    expected_noise = case["sample_rate"]/10**(case["cn0"]/10)
    if not math.isclose(float(manifest.get("noise_variance_pre_frontend", -1)),
                        expected_noise, rel_tol=1e-14, abs_tol=0):
        raise ValueError("manifest thermal noise variance differs from the requested C/N0")
    expected_candidates = 8 if case["basis"] == "two-ray" else 4
    metadata = manifest.get("candidates", [])
    if manifest.get("candidate_count") != expected_candidates or len(metadata) != expected_candidates:
        raise ValueError("candidate bank size differs from the complete bit/epoch/channel plan")
    if {(candidate.get("bit"), candidate.get("epoch_offset_seconds")) for candidate in metadata} != {
            (bit, epoch) for bit in (0, 1) for epoch in case["epochs_seconds"]}:
        raise ValueError("candidate bank omits a predeclared bit or epoch hypothesis")


def resume_case(output: Path, case: dict, hashes: dict, argv: list[str], cwd: Path) -> dict:
    record_path = output.parent/(output.name+".run.json")
    if not output.is_dir() or not record_path.is_file():
        raise FileExistsError(f"--resume requires a complete matching prior case: {output}")
    record = json.loads(record_path.read_text())
    expected_sources = {key: value for key, value in hashes.items() if key != "binary"}
    if (record.get("status") != "complete" or record.get("case") != case or
            record.get("source_hashes") != expected_sources or
            record.get("binary_sha256") != hashes["binary"] or
            record.get("argv") != argv or record.get("cwd") != str(cwd)):
        raise ValueError(f"prior case identity does not match frozen policy/input: {output}")
    actual = payload_hashes(output)
    expected_payload = {"manifest.json": record.get("manifest_sha256"),
                        **record.get("exported_file_hashes", {})}
    if actual != expected_payload:
        raise ValueError(f"prior case payload hash mismatch: {output}")
    manifest = json.loads((output/"manifest.json").read_text())
    validate_manifest(manifest, case)
    record["resumed"] = True
    return record


def execute_case(binary: Path, output: Path, source_root: Path, case: dict,
                 hashes: dict, start: float) -> dict:
    if time.perf_counter()-start >= STAGE_LIMIT_SECONDS:
        raise TimeoutError("holdout generation stage exceeded 1800 seconds")
    if output.exists() or output.parent.joinpath(output.name+".run.json").exists():
        raise FileExistsError(f"refusing to overwrite existing holdout path: {output}")
    argv = command_for(binary, output, case)
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    child_start = time.perf_counter()
    timed_out = None
    try:
        result = subprocess.run(argv, cwd=source_root, capture_output=True, text=True,
                                timeout=min(CHILD_LIMIT_SECONDS,
                                            STAGE_LIMIT_SECONDS-(child_start-start)),
                                check=False)
        stdout, stderr, returncode = result.stdout, result.stderr, result.returncode
    except subprocess.TimeoutExpired as exc:
        timed_out = True
        stdout = exc.stdout.decode(errors="replace") if isinstance(exc.stdout, bytes) else (exc.stdout or "")
        stderr = exc.stderr.decode(errors="replace") if isinstance(exc.stderr, bytes) else (exc.stderr or "")
        returncode = 124
    wall = time.perf_counter()-child_start
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    source_hashes = {key: value for key, value in hashes.items() if key != "binary"}
    record = dict(format="triad-fp4-holdout-case-v1", status="failed" if returncode else "complete",
                  case=case, argv=argv, source_hashes=source_hashes,
                  binary_sha256=hashes["binary"],
                  cwd=str(source_root),
                  wall_seconds=wall, child_user_cpu_seconds=after.ru_utime-before.ru_utime,
                  child_system_cpu_seconds=after.ru_stime-before.ru_stime,
                  timeout=bool(timed_out), returncode=returncode,
                  stdout_tail=stdout[-2000:], stderr_tail=stderr[-4000:])
    if not returncode:
        manifest = json.loads((output/"manifest.json").read_text())
        validate_manifest(manifest, case)
        hashes_by_file = payload_hashes(output)
        record["manifest_sha256"] = hashes_by_file.pop("manifest.json")
        record["exported_file_hashes"] = hashes_by_file
        record["manifest_path"] = (output/"manifest.json").as_posix()
        record["manifest_parameters"] = dict(sample_rate=manifest.get("sample_rate"),
                                             sample_count=manifest.get("sample_count"),
                                             candidate_count=manifest.get("candidate_count"),
                                             noise_variance_pre_frontend=manifest.get("noise_variance_pre_frontend"),
                                             signal_enabled=manifest.get("signal_enabled"),
                                             gaussian_projection_supported=manifest.get("gaussian_projection_supported"))
    else:
        # Preserve diagnostic logs with a unique case path. A partial exporter
        # directory remains untouched and must be inspected before any retry.
        (output.parent/(output.name+".stdout.txt")).write_text(stdout)
        (output.parent/(output.name+".stderr.txt")).write_text(stderr)
    (output.parent/(output.name+".run.json")).write_text(json.dumps(record, indent=2)+"\n")
    if returncode:
        if timed_out:
            raise TimeoutError(f"holdout child exceeded {CHILD_LIMIT_SECONDS} seconds: {case['name']}")
        raise subprocess.CalledProcessError(returncode, argv, stdout, stderr)
    return record


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--plan", action="store_true", help="print preflighted cases and seeds without generating inputs")
    parser.add_argument("--resume", action="store_true", help="reuse only complete cases with exact identity and payload hashes")
    args = parser.parse_args(argv)
    source_root, binary, output = args.source_root.resolve(), args.binary.resolve(), args.output.resolve()
    hashes = verify_sources(source_root, binary)
    case_list = cases()
    env = environment(binary)
    if args.plan:
        print(json.dumps(dict(format="triad-fp4-holdout-plan-v1", source_root="<supplied-root>",
                              binary="<supplied-binary>", hashes=hashes, environment=env,
                              seed_generator="Python random.Random(982173).getrandbits(64)",
                              group_counts=EXPECTED_COUNTS, case_count=len(case_list), cases=case_list),
                          indent=2))
        return
    output.mkdir(parents=True, exist_ok=True)
    start = time.perf_counter()
    summary_path = output/"holdout-summary.json"
    if summary_path.exists() and not args.resume:
        raise FileExistsError(f"refusing to overwrite {summary_path}")
    records = []
    for case in case_list:
        case_path = output/case["name"]
        if args.resume and case_path.exists():
            records.append(resume_case(case_path, case, hashes,
                                       command_for(binary, case_path, case), source_root))
        else:
            records.append(execute_case(binary, case_path, source_root, case, hashes, start))
    hypotheses_by_group = {}
    for case in case_list:
        candidate_count = 8 if case["channel_bases"] == "two-ray" else 4
        count = len(case["cfo_grid_hz"])*len(case["lags_samples"])*candidate_count
        previous = hypotheses_by_group.setdefault(case["group"], count)
        if previous != count:
            raise AssertionError("a holdout group mixes incompatible full-bank sizes")
    summary = dict(format="triad-fp4-holdout-v1", status="complete",
                   source_hashes={key: value for key, value in hashes.items() if key != "binary"},
                   binary_sha256=hashes["binary"], environment=env,
                   split="independent holdout",
                   policy="Paired experimental modes: NVFP4 E2M1 with UE4M3 group16, default M=N=K=128/warps4/stages2; and strict full-bank FP64 correlation with graph FP32 bulk/FP64 reductions. The strict-all final-decision mode is available separately when explicitly run; this generator does not imply it was used.",
                   search=dict(cfo_hz=[-.01, 0., .01], lag_samples=[0, 1, 2], epoch_offsets_seconds=[0, 3600],
                               phase_states=512,
                               exhaustive_hypotheses_per_capture_by_group=hypotheses_by_group),
                   group_counts=EXPECTED_COUNTS, case_count=len(records),
                   noise_admission_qualified=False,
                   statistical_note="Independent fresh physical inputs for policy validation; empirical count alone does not establish operational maximum-over-search tail probabilities.",
                   wall_seconds=time.perf_counter()-start, cases=records)
    summary_path.write_text(json.dumps(summary, indent=2)+"\n")
    print(json.dumps(dict(summary=str(summary_path), case_count=len(records),
                          wall_seconds=summary["wall_seconds"]), indent=2))


if __name__ == "__main__":
    main()
