#!/usr/bin/env python3
"""Run a reproducible, sharded triad export campaign under the bounded runner.

Launch this driver directly through ``triad_bounded_run.py``. Workers stay in
the same owned process group so timeout/cancellation covers the whole campaign.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import time
from typing import Any


VERSION = "triad-diversity-export-v1"
EPOCH_BASE = 1_800_000_000
MAX_PATH_INDEX = 1_000_000_000
MASK64 = (1 << 64) - 1
FORCED_OPTIONS = {
    "--mode", "--stats-noisy", "--threads", "--output", "--seed",
    "--key-seed", "--channel-seed", "--oscillator-seed", "--noise-seed",
    "--frontend-seed", "--epoch", "--signal-bit", "--now", "--no-stats-noisy",
    "--no-encrypt",
}


class CampaignError(RuntimeError):
    pass


def positive_int(text: str) -> int:
    try:
        value = int(text, 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("expected a positive integer") from exc
    if value <= 0:
        raise argparse.ArgumentTypeError("expected a positive integer")
    return value


def nonnegative_int(text: str) -> int:
    try:
        value = int(text, 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("expected a nonnegative integer") from exc
    if value < 0 or value > MAX_PATH_INDEX:
        raise argparse.ArgumentTypeError(f"expected an integer in [0, {MAX_PATH_INDEX}]")
    return value


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", required=True, type=pathlib.Path)
    p.add_argument("--output", required=True, type=pathlib.Path,
                   help="fresh output directory for campaign metadata, logs, and path exports")
    p.add_argument("--paths", required=True, type=positive_int,
                   help="even number of independent path realizations")
    p.add_argument("--start-index", type=nonnegative_int, default=0,
                   help="stable global index for sharding")
    p.add_argument("--jobs", type=positive_int, default=1,
                   help="concurrent exports, maximum 32")
    p.add_argument("--split", required=True, choices=("development", "holdout"))
    p.add_argument("--case", required=True,
                   help="stable scenario label; use the same value for paired projections")
    p.add_argument("extra", nargs=argparse.REMAINDER,
                   help="triad_long options after --; cannot override campaign-owned arguments")
    return p


def seed_for(split: str, case: str, index: int, bit: int, domain: str) -> int:
    """Derive a stable independent seed; configuration/projection knobs are excluded."""
    fixed_tuple = [VERSION, split, case, index, bit, domain]
    payload = json.dumps(fixed_tuple, ensure_ascii=True,
                         separators=(",", ":")).encode("ascii")
    return int.from_bytes(hashlib.sha256(payload).digest()[:8], "little") & MASK64


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def source_hash(repository: pathlib.Path) -> str:
    excluded = {".git", "build", "local-evidence", "__pycache__", ".venv"}
    suffixes = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
    digest = hashlib.sha256()
    for path in sorted((p for p in repository.rglob("*")
                        if p.is_file() and p.suffix.lower() in suffixes
                        and not any(part in excluded for part in p.relative_to(repository).parts)),
                       key=lambda p: p.relative_to(repository).as_posix()):
        relative = path.relative_to(repository).as_posix().encode("utf-8")
        digest.update(len(relative).to_bytes(8, "little"))
        digest.update(relative)
        digest.update(bytes.fromhex(sha256_file(path)))
    return digest.hexdigest()


def clean_extra(extra: list[str]) -> list[str]:
    if extra and extra[0] == "--":
        extra = extra[1:]
    for arg in extra:
        if not arg.startswith("-"):
            continue
        option = arg.split("=", 1)[0]
        if option in FORCED_OPTIONS:
            raise CampaignError(f"{option} is controlled by the campaign driver")
    return extra


def process_group_is_owned() -> bool:
    """Require execution as the session/process-group leader created by runner."""
    if not sys.platform.startswith("linux"):
        return False
    try:
        group = os.getpgrp()
        session = os.getsid(0)
        return group == session == os.getpid()
    except (OSError, ProcessLookupError):
        return False


def forced_values(split: str, case: str, index: int) -> dict[str, Any]:
    bit = index % 2
    values = {domain: seed_for(split, case, index, bit, domain)
              for domain in ("seed", "key", "channel", "oscillator", "noise", "frontend")}
    return {"path_index": index, "signal_bit": bit, "epoch": EPOCH_BASE + 60 * index,
            "seeds": values}


def make_command(binary: pathlib.Path, dataset: pathlib.Path,
                 extra: list[str], forced: dict[str, Any]) -> list[str]:
    seeds = forced["seeds"]
    command = [str(binary), "--mode", "export-stats", "--stats-noisy", "--threads", "1",
               "--output", str(dataset), "--seed", str(seeds["seed"]),
               "--key-seed", str(seeds["key"]), "--channel-seed", str(seeds["channel"]),
               "--oscillator-seed", str(seeds["oscillator"]), "--noise-seed", str(seeds["noise"]),
               "--frontend-seed", str(seeds["frontend"]), "--epoch", str(forced["epoch"]),
               "--signal-bit", str(forced["signal_bit"])]
    command.extend(extra)
    return command


def atomic_json(path: pathlib.Path, value: dict[str, Any]) -> None:
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def validate_manifest(dataset: pathlib.Path, forced: dict[str, Any]) -> tuple[str, str | None]:
    try:
        manifest = json.loads((dataset / "manifest.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return "manifest_error", str(exc)
    if manifest.get("format") != "triad-block-stats-v1" or manifest.get("stats_noisy") is not True:
        return "manifest_mismatch", "missing expected noisy triad-block-stats manifest"
    provenance = manifest.get("command_argv")
    if not isinstance(provenance, list):
        return "manifest_mismatch", "manifest has no command_argv provenance"
    expected = forced["seeds"]
    pairs = {provenance[i]: provenance[i + 1]
             for i in range(len(provenance) - 1) if provenance[i].startswith("--")}
    expected_args = {"--seed": expected["seed"], "--key-seed": expected["key"],
                     "--channel-seed": expected["channel"], "--oscillator-seed": expected["oscillator"],
                     "--noise-seed": expected["noise"], "--frontend-seed": expected["frontend"],
                     "--epoch": forced["epoch"], "--signal-bit": forced["signal_bit"]}
    try:
        matches = all(int(pairs[name]) == value for name, value in expected_args.items())
    except (KeyError, ValueError, TypeError):
        matches = False
    if not matches:
        return "manifest_mismatch", "manifest command provenance differs from forced path metadata"
    roots = manifest.get("random_seed_roots", {})
    root_expectations = {"channel": expected["channel"], "oscillator": expected["oscillator"],
                         "noise": expected["noise"], "frontend": expected["frontend"]}
    if any(roots.get(name) != value for name, value in root_expectations.items()):
        return "manifest_mismatch", "manifest random seed roots differ from forced path metadata"
    return "completed", None


def run_one(binary: pathlib.Path, output: pathlib.Path, extra: list[str],
            base: dict[str, Any], provenance: dict[str, str]) -> dict[str, Any]:
    index = base["path_index"]
    dataset = output / f"path-{index:05d}"
    log_path = output / f"path-{index:05d}.log"
    command = make_command(binary, dataset, extra, base)
    started = time.monotonic()
    result: dict[str, Any] = {**base, "dataset_directory": dataset.name,
                              "log_file": log_path.name, "command": command,
                              **provenance, "status": "running"}
    with log_path.open("x", encoding="utf-8") as log:
        log.write("command: " + json.dumps(command, ensure_ascii=True) + "\n")
        log.write(json.dumps(provenance, sort_keys=True) + "\n\n")
        log.flush()
        try:
            completed = subprocess.run(command, stdin=subprocess.DEVNULL,
                                       stdout=log, stderr=subprocess.STDOUT,
                                       check=False, close_fds=True)
            result["returncode"] = completed.returncode
            if completed.returncode == 0:
                result["status"], result["error"] = validate_manifest(dataset, base)
            else:
                result["status"] = "export_failed"
        except OSError as exc:
            result["returncode"] = 127
            result["status"] = "launch_failed"
            result["error"] = f"{type(exc).__name__}: {exc}"
        result["elapsed_seconds"] = time.monotonic() - started
        log.write(f"\nelapsed_seconds: {result['elapsed_seconds']:.9f}\n")
        log.write(f"returncode: {result['returncode']}\nstatus: {result['status']}\n")
    return result


def run(args: argparse.Namespace) -> int:
    started_monotonic = time.monotonic()
    extra = clean_extra(list(args.extra))
    if not process_group_is_owned():
        raise CampaignError("launch this driver directly through triad_bounded_run.py")
    if args.paths % 2:
        raise CampaignError("--paths must be even so source bits are balanced")
    if args.jobs > 32:
        raise CampaignError("--jobs must be at most 32")
    if args.start_index + args.paths - 1 > MAX_PATH_INDEX:
        raise CampaignError(f"global path indices must not exceed {MAX_PATH_INDEX}")
    if EPOCH_BASE + 60 * (args.start_index + args.paths - 1) > MASK64:
        raise CampaignError("path epoch exceeds uint64 range")
    binary = args.binary.expanduser().resolve(strict=True)
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise CampaignError("--binary must name an executable file")
    output = args.output.expanduser().resolve()
    if output.exists():
        raise CampaignError(f"campaign output already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.mkdir()
    repository = pathlib.Path(__file__).resolve().parents[1]
    tool_hash = sha256_file(pathlib.Path(__file__).resolve())
    source_inputs_hash = source_hash(repository)
    provenance = {"binary_sha256": sha256_file(binary), "driver_sha256": tool_hash,
                  "source_inputs_sha256": source_inputs_hash}
    indices = list(range(args.start_index, args.start_index + args.paths))
    results: dict[int, dict[str, Any]] = {
        index: {**forced_values(args.split, args.case, index), "status": "pending"}
        for index in indices
    }
    metadata: dict[str, Any] = {
        "format": VERSION, "split": args.split, "case": args.case,
        "start_index": args.start_index, "paths_requested": args.paths,
        "jobs": args.jobs, "binary": str(binary), "extra_arguments": extra,
        "seed_derivation": "SHA256(version, split, case, global path index, bit, domain); excludes C/N0, rate, block partition",
        **provenance, "started_unix": time.time(), "status": "running",
        "paths": [results[index] for index in indices],
    }
    partial_path = output / "campaign.json.partial"
    final_path = output / "campaign.json"
    atomic_json(partial_path, metadata)

    completed_indices: set[int] = set()
    failed = False
    executor = concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs)
    futures: dict[concurrent.futures.Future[dict[str, Any]], int] = {}
    next_to_submit = 0
    try:
        while next_to_submit < len(indices) and len(futures) < args.jobs:
            index = indices[next_to_submit]
            futures[executor.submit(run_one, binary, output, extra,
                                    forced_values(args.split, args.case, index), provenance)] = index
            results[index]["status"] = "running"
            next_to_submit += 1
        while futures:
            done, _ = concurrent.futures.wait(futures,
                                               return_when=concurrent.futures.FIRST_COMPLETED)
            for future in done:
                index = futures.pop(future)
                try:
                    result = future.result()
                except BaseException as exc:
                    result = {**forced_values(args.split, args.case, index),
                              "status": "worker_error", "error": repr(exc),
                              "returncode": 125}
                results[index] = result
                completed_indices.add(index)
                if result["status"] != "completed":
                    failed = True
            metadata["paths"] = [results[index] for index in indices]
            metadata["completed_paths"] = len(completed_indices)
            metadata["status"] = "failed" if failed else "running"
            atomic_json(partial_path, metadata)
            if not failed:
                while next_to_submit < len(indices) and len(futures) < args.jobs:
                    index = indices[next_to_submit]
                    futures[executor.submit(run_one, binary, output, extra,
                                            forced_values(args.split, args.case, index), provenance)] = index
                    results[index]["status"] = "running"
                    next_to_submit += 1
        if failed:
            for index in indices[next_to_submit:]:
                results[index]["status"] = "not_started_after_failure"
    finally:
        # Direct workers are in this process group; the outer bounded runner owns
        # cancellation. Join submitted workers before writing final campaign state.
        executor.shutdown(wait=True, cancel_futures=False)

    metadata["paths"] = [results[index] for index in indices]
    metadata["completed_paths"] = len(completed_indices)
    metadata["missing_paths"] = [index for index in indices if results[index]["status"] != "completed"]
    metadata["status"] = "completed" if not metadata["missing_paths"] else "incomplete"
    metadata["elapsed_seconds"] = time.monotonic() - started_monotonic
    atomic_json(final_path, metadata)
    partial_path.unlink()
    return 0 if metadata["status"] == "completed" else 1


def main() -> int:
    args = parser().parse_args()
    try:
        return run(args)
    except (CampaignError, FileExistsError, OSError) as exc:
        print(f"triad-diversity-export: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
