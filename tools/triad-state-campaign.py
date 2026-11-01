#!/usr/bin/env python3
"""Run reproducible CPU campaigns that export triad block statistics.

This driver creates independent per-path datasets for downstream analysis. It
does not decode the waveform or make claims about radio hardware.
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


PROFILES = ("flat", "hf-wiener", "hf-colored", "hf-erased", "hf-fhss",
            "hf-acquisition", "hf-burst")
PROFILE_OFFSETS = {name: index * 10_000 for index, name in enumerate(PROFILES)}
EPOCH_BASE = 1_800_000_000
MASK64 = (1 << 64) - 1
MAX_PATH_INDEX = 1_000_000_000


def positive_int(text: str) -> int:
    try:
        value = int(text, 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("expected a positive integer") from exc
    if value <= 0:
        raise argparse.ArgumentTypeError("expected a positive integer")
    return value


def nonnegative_index(text: str) -> int:
    try:
        value = int(text, 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("expected a nonnegative path index") from exc
    if value < 0 or value > MAX_PATH_INDEX:
        raise argparse.ArgumentTypeError(f"path index must be in [0, {MAX_PATH_INDEX}]")
    return value


def finite_float(text: str) -> float:
    try:
        value = float(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("expected a finite number") from exc
    if not (value == value and abs(value) != float("inf")):
        raise argparse.ArgumentTypeError("expected a finite number")
    return value


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", required=True, type=pathlib.Path, help="built triad_long executable")
    p.add_argument("--output", required=True, type=pathlib.Path, help="new campaign directory; must not exist")
    p.add_argument("--paths", required=True, type=positive_int, help="even number of paths, balanced across source bits")
    p.add_argument("--start-index", type=nonnegative_index, default=0,
                   help="first stable global path index for sharding (default 0)")
    p.add_argument("--seconds", required=True, type=finite_float)
    p.add_argument("--jobs", type=positive_int, default=1, help="concurrent exports, at most 32")
    p.add_argument("--profile", required=True, choices=PROFILES,
                   help="hf-acquisition varies start/frequency/epoch hypotheses; hf-burst adds one wait tile between active tiles, so elapsed time approaches 2x active duration for long runs (single-tile runs have no inter-tile wait); hf-fhss requires compatible protected-dwell and sample-rate geometry, e.g. --rate 72 --sample-rate 1024 (3.6/64 and 36/640 can fail)")
    p.add_argument("--split", required=True, choices=("development", "holdout"))
    p.add_argument("--block-seconds", type=finite_float, default=4.0)
    p.add_argument("--sample-rate", type=positive_int, default=64)
    p.add_argument("--rate", type=finite_float, default=3.6)
    p.add_argument("--cn0", type=finite_float, default=-24.0)
    p.add_argument("--stats-noisy", dest="stats_noisy", action="store_true", default=True,
                   help="write actual noisy projections (default)")
    p.add_argument("--no-stats-noisy", dest="stats_noisy", action="store_false",
                   help="omit the second actual noisy channel pass")
    return p


def seed_for(split: str, profile: str, seconds: float, sample_rate: int,
             rate: float, cn0: float, path_index: int,
             source_bit: int, domain: str) -> int:
    # JSON's deterministic encoding avoids Python's process-randomized hash().
    # Block duration changes the projection partition, not the physical sample
    # realization, so it deliberately does not participate in seed identity.
    fixed_tuple = ["triad-state-campaign-v1", split, profile, seconds,
                   sample_rate, rate, cn0, path_index,
                   source_bit, domain]
    payload = json.dumps(fixed_tuple, ensure_ascii=True, separators=(",", ":")).encode("ascii")
    return int.from_bytes(hashlib.sha256(payload).digest()[:8], "little") & MASK64


def profile_options(profile: str) -> list[str]:
    if profile == "flat":
        return ["--channel", "none", "--phase-diffusion", "0", "--white-fm", "0",
                "--random-walk-fm", "0", "--flicker-fm", "0", "--white-pm", "0"]
    if profile == "hf-wiener":
        return ["--channel", "hf", "--phase-diffusion", "0.05"]
    colored = ["--channel", "hf", "--phase-diffusion", "0.00872665",
               "--white-fm", "0.002", "--random-walk-fm", "0.000001",
               "--flicker-fm", "0.002", "--white-pm", "0.01"]
    if profile == "hf-erased":
        return colored + ["--dropout-ms", "10", "--dropout-start-ms", "100"]
    if profile == "hf-fhss":
        return colored + ["--fhss"]
    if profile == "hf-acquisition":
        return colored + ["--start", "-0.04", "0.04", "0.04",
                          "--frequency", "-0.01", "0.01", "0.02",
                          "--epoch-offsets", "0,1"]
    if profile == "hf-burst":
        return colored + ["--burst-wait-tiles", "1"]
    return colored


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def make_command(args: argparse.Namespace, binary: pathlib.Path, output_dir: pathlib.Path,
                 path_index: int) -> tuple[list[str], dict[str, Any]]:
    bit = path_index % 2
    key_seed = seed_for(args.split, args.profile, args.seconds, args.sample_rate,
                        args.rate, args.cn0, path_index, bit, "key")
    channel_seed = seed_for(args.split, args.profile, args.seconds, args.sample_rate,
                            args.rate, args.cn0, path_index, bit, "channel")
    epoch = EPOCH_BASE + 60 * path_index + PROFILE_OFFSETS[args.profile]
    command = [str(binary), "--mode", "export-stats", "--seconds", format(args.seconds, ".17g"),
               "--rate", format(args.rate, ".17g"), "--sample-rate", str(args.sample_rate),
               "--added", "10", "--rf-hz", "30000000", "--basis", "two-ray",
               "--cn0", format(args.cn0, ".17g"), "--block-seconds", format(args.block_seconds, ".17g"),
               "--chunk", "4096", "--threads", "1", "--signal-bit", str(bit), "--epoch", str(epoch),
               "--key-seed", str(key_seed), "--channel-seed", str(channel_seed),
               "--output", str(output_dir)]
    command.extend(profile_options(args.profile))
    if args.stats_noisy:
        command.append("--stats-noisy")
    return command, {"source_bit": bit, "key_seed": key_seed, "channel_seed": channel_seed, "epoch": epoch}


def one_path(args: argparse.Namespace, binary: pathlib.Path, driver_cpp_hash: str,
             source_inputs_hash: str,
             binary_hash: str, index: int, output_root: pathlib.Path) -> dict[str, Any]:
    dataset = output_root / f"path-{index:05d}"
    log_path = output_root / f"path-{index:05d}.log"
    command, seed_info = make_command(args, binary, dataset, index)
    started = time.monotonic()
    with log_path.open("x", encoding="utf-8") as log:
        log.write("command: " + json.dumps(command, ensure_ascii=True) + "\n")
        log.write(f"driver_cpp_sha256: {driver_cpp_hash}\nsource_inputs_sha256: {source_inputs_hash}\nbinary_sha256: {binary_hash}\n\n")
        log.flush()
        error = None
        try:
            completed = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
            returncode = completed.returncode
        except OSError as exc:
            returncode = 127
            error = repr(exc)
        elapsed = time.monotonic() - started
        log.write(f"\nelapsed_seconds: {elapsed:.9f}\nreturncode: {returncode}\n")
        if error is not None:
            log.write(f"error: {error}\n")
        log.flush()
    result: dict[str, Any] = {
        "path_index": index, **seed_info, "dataset_directory": dataset.name,
        "log_file": log_path.name, "command": command,
        "driver_cpp_sha256": driver_cpp_hash, "source_inputs_sha256": source_inputs_hash,
        "binary_sha256": binary_hash,
        "elapsed_seconds": elapsed, "returncode": returncode,
    }
    if error is not None:
        result["error"] = error
    if returncode == 0:
        try:
            manifest = json.loads((dataset / "manifest.json").read_text(encoding="utf-8"))
            result["export_manifest"] = manifest
        except (OSError, json.JSONDecodeError) as exc:
            result["manifest_error"] = str(exc)
            result["returncode"] = 1
    return result


def run(args: argparse.Namespace) -> int:
    if args.paths % 2:
        raise ValueError("--paths must be even so source bits are balanced")
    if args.start_index + args.paths - 1 > MAX_PATH_INDEX:
        raise ValueError(f"shard path indices must not exceed {MAX_PATH_INDEX}")
    last_epoch = EPOCH_BASE + 60 * (args.start_index + args.paths - 1) + PROFILE_OFFSETS[args.profile]
    if last_epoch > MASK64:
        raise ValueError("shard epoch exceeds uint64 range")
    if args.jobs > 32:
        raise ValueError("--jobs must be at most 32")
    if args.seconds < 8 or args.seconds > 604800 or args.block_seconds <= 0:
        raise ValueError("--seconds must be in [8, 604800] and block duration positive")
    if args.rate <= 0 or args.sample_rate < 64 or args.sample_rate > 10_000_000:
        raise ValueError("rate and sample rate are outside triad_long bounds")
    binary = args.binary.resolve(strict=True)
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise ValueError("--binary must name an executable file")
    output = args.output.resolve()
    if output.exists():
        raise FileExistsError(f"campaign output already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.mkdir()
    repository = pathlib.Path(__file__).resolve().parents[1]
    driver_cpp = repository / "tools" / "triad-long.cpp"
    driver_cpp_hash, binary_hash = sha256_file(driver_cpp), sha256_file(binary)
    excluded = {".git", "build", "local-evidence", "__pycache__", ".venv"}
    source_suffixes = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
    source_files = sorted(
        (p for p in repository.rglob("*")
         if p.is_file() and p.suffix.lower() in source_suffixes and not any(part in excluded for part in p.relative_to(repository).parts)),
        key=lambda p: p.relative_to(repository).as_posix())
    source_entries = []
    bundle = hashlib.sha256()
    for path in source_files:
        relative = path.relative_to(repository).as_posix()
        file_hash = sha256_file(path)
        source_entries.append({"path": relative, "sha256": file_hash})
        bundle.update(relative.encode("utf-8") + b"\0" + bytes.fromhex(file_hash))
    source_inputs_hash = bundle.hexdigest()

    path_indices = range(args.start_index, args.start_index + args.paths)
    seeds = [make_command(args, binary, output / f"path-{i:05d}", i)[1]
             for i in path_indices]
    key_seeds = [item["key_seed"] for item in seeds]
    channel_seeds = [item["channel_seed"] for item in seeds]
    if len(set(key_seeds + channel_seeds)) != 2 * args.paths:
        raise RuntimeError("derived key/channel seeds collided; refusing a non-independent campaign")
    bits = [item["source_bit"] for item in seeds]

    results: list[dict[str, Any]] = []
    next_index = args.start_index
    end_index = args.start_index + args.paths
    failed = False
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        running: dict[concurrent.futures.Future[dict[str, Any]], int] = {}
        while next_index < end_index and len(running) < args.jobs:
            future = pool.submit(one_path, args, binary, driver_cpp_hash, source_inputs_hash, binary_hash,
                                 next_index, output)
            running[future] = next_index
            next_index += 1
        while running:
            done, _ = concurrent.futures.wait(running, return_when=concurrent.futures.FIRST_COMPLETED)
            for future in done:
                index = running.pop(future)
                try:
                    result = future.result()
                except Exception as exc:  # retain path failure as campaign evidence
                    result = {"path_index": index, "returncode": 1, "error": repr(exc)}
                results.append(result)
                if result.get("returncode") != 0:
                    failed = True
            if not failed:
                while next_index < end_index and len(running) < args.jobs:
                    future = pool.submit(one_path, args, binary, driver_cpp_hash, source_inputs_hash, binary_hash,
                                         next_index, output)
                    running[future] = next_index
                    next_index += 1

    results.sort(key=lambda result: result["path_index"])
    campaign = {
        "format": "triad-state-campaign-v1", "binary": str(binary),
        "driver_cpp_file": str(driver_cpp), "driver_cpp_sha256": driver_cpp_hash,
        "source_inputs": {"sha256": source_inputs_hash, "file_count": len(source_entries), "files": source_entries},
        "binary_sha256": binary_hash, "profile": args.profile, "split": args.split,
        "seconds": args.seconds, "rate": args.rate, "sample_rate": args.sample_rate,
        "cn0_db_hz": args.cn0, "block_seconds": args.block_seconds,
        "paths_requested": args.paths, "start_index": args.start_index,
        "paths_completed": len(results),
        "jobs": args.jobs, "stats_noisy": args.stats_noisy,
        "source_bits_balanced": bits.count(0) == bits.count(1),
        "key_seeds_unique": len(set(key_seeds)) == args.paths,
        "channel_seeds_unique": len(set(channel_seeds)) == args.paths,
        "all_key_and_channel_seeds_unique": len(set(key_seeds + channel_seeds)) == 2 * args.paths,
        "seed_derivation": "SHA-256 over split/profile/duration/rate/sample-rate/CN0/path/bit/domain; block duration is excluded so projection partitions reuse the same physical realization",
        "profile_settings": profile_options(args.profile),
        "profile_note": ("one silent tile is inserted between adjacent active source tiles; elapsed time approaches twice active duration as tile count grows, while a single-tile request has no inter-tile wait; triad_long's seven-day calendar guard still applies"
                         if args.profile == "hf-burst" else
                         "public start/frequency/epoch bank; no truth-dependent candidate filtering"
                         if args.profile == "hf-acquisition" else
                         "FHSS is only a source envelope here, not an RF retuning model. Geometry requires floor((0.4 - 2*(0.055 + 2/3600 + 0.005 + 8/fine_rate_hz))*fine_rate_hz) >= 1 protected fine cell per dwell, plus sample_rate >= 4*fine_rate_hz and > 1.1*occupied_bandwidth_hz. With --added 10, --rate 72 gives fine_rate_hz=120; --sample-rate 1024 satisfies the guards. The default 3.6/64 and 36/640 pairs can be rejected."
                         if args.profile == "hf-fhss" else None),
        "results": results,
    }
    partial = output / "campaign.json.partial"
    partial.write_text(json.dumps(campaign, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(partial, output / "campaign.json")
    if failed or len(results) != args.paths:
        print(f"campaign incomplete; see {output / 'campaign.json'}", file=sys.stderr)
        return 1
    print(output / "campaign.json")
    return 0


def main() -> int:
    args = parser().parse_args()
    try:
        return run(args)
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"triad-state-campaign: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
