#!/usr/bin/env python3
"""Bounded, reproducible sampled-waveform exports for the low-rate triad study.

Every path goes through triad_long's shared sampled channel, including its
selected oscillator, spread, path evolution and complete tails. This driver
does not decode, infer noise density, or claim that a nominal timing branch is
known alignment. Its independently seeded physical paths are simulation cases,
not calibrated sub-9-kHz propagation measurements.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import threading
import time


ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "tools" / "triad_bounded_run.py"
MAX_SECONDS = 315576000  # Ten Julian years; rounded private tiles/tails may extend it.
MAX_INDEX = 1_000_000_000
SEED_DOMAINS = ("key", "channel", "oscillator", "noise", "frontend", "path")
# Canonical IDs in src/tuning_presets.cpp; arbitrary values use the explicit
# nominal physical overrides below, never receiver tap-basis parameters.
SPREAD_PRESETS = ("none", "sub9-best", "sub9-moderate", "sub9-worst", "hf-best",
                  "hf-moderate", "hf-worst", "aurora-best", "aurora-worst", "eme-focused")
EXPORT_FILES = ("gram.c128", "mean.c128", "intervals.u64", "valid_rows.u64",
                "signal_energy.f64", "observation.c128", "received_energy.f64")


def finite(text):
    value = float(text)
    if not math.isfinite(value):
        raise argparse.ArgumentTypeError("expected a finite number")
    return value


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path, help="fresh campaign directory")
    p.add_argument("--paths", type=int, required=True, help="positive even count; source bits are balanced")
    p.add_argument("--start-index", type=int, default=0)
    p.add_argument("--split", choices=("development", "holdout"), required=True)
    p.add_argument("--control", choices=("signal", "null", "wrong-key"), default="signal",
                   help="sampled signal, noise-only, or mismatched receive-key control; seeds stay paired")
    p.add_argument("--seconds", type=finite, required=True)
    p.add_argument("--rate", type=finite, default=.036)
    p.add_argument("--sample-rate", type=int, default=1)
    p.add_argument("--sample-rate-denominator", type=int, default=1,
                   help="N>1 requires sample-rate 1 and gives one sample per N physical seconds")
    p.add_argument("--cn0", type=finite, required=True, help="total triplet C/N0, dB-Hz")
    p.add_argument("--rf-hz", type=finite, default=8270,
                   help="physical RF frequency in (0,10e9] Hz; oscillator effects scale with it")
    p.add_argument("--spread-preset", choices=SPREAD_PRESETS, default="sub9-best")
    p.add_argument("--spread-delay-ms", type=finite,
                   help="optional nominal physical excess delay override; independent of receiver tap basis")
    p.add_argument("--spread-doppler-hz", type=finite,
                   help="optional nominal physical Doppler separation override; independent of receiver tap basis")
    p.add_argument("--block-seconds", type=finite, default=1024)
    p.add_argument("--jobs", type=int, default=1)
    p.add_argument("--timeout", type=finite, default=1800, help="per-path wall time; cleanup has an additional bounded allowance")
    p.add_argument("--term-grace", type=finite, default=2)
    p.add_argument("--tap-delay-ms", type=finite, default=.005,
                   help="receiver delayed-tap basis; default matches physical sub9-best")
    p.add_argument("--tap-doppler-hz", type=finite, default=.001,
                   help="receiver delayed-tap Doppler basis; default matches physical sub9-best")
    p.add_argument("--ray-coherence-seconds", type=finite, default=0)
    p.add_argument("--ray-gaussian-width-hz", type=finite, default=0,
                   help="physical Gaussian ray width B=2*sigma in Hz; 0 disables; exclusive with positive ray coherence")
    p.add_argument("--diurnal-phase-radians", type=finite, default=0)
    p.add_argument("--diurnal-period-seconds", type=finite, default=86400)
    p.add_argument("--outage-seconds", type=finite, nargs=2, default=(-1., -1.), metavar=("START", "END"))
    p.add_argument("--outage-gain", type=finite, default=0)
    p.add_argument("--outage-ramp-seconds", type=finite, default=60)
    p.add_argument("--selective-perturbation", choices=("off", "slow-1", "fast-1", "each-1", "slow-5", "fast-5", "each-5"), default="off")
    p.add_argument("--start", type=finite, nargs=3, default=(0., 0., 1.), metavar=("MIN", "MAX", "STEP"), help="public timing bank; default single nominal branch is not known alignment")
    p.add_argument("--epoch-offsets", default="0", help="comma-separated public integer epoch offsets, e.g. use --epoch-offsets=-1,0,1")
    return p


def validate(args):
    if args.control not in ("signal", "null", "wrong-key"):
        raise ValueError("unsupported sampled control")
    if args.spread_preset not in SPREAD_PRESETS:
        raise ValueError("unsupported physical spread preset")
    if not math.isfinite(args.rf_hz) or not 0 < args.rf_hz <= 10e9:
        raise ValueError("RF frequency must be finite and in (0,10e9] Hz")
    for name in ("spread_delay_ms", "spread_doppler_hz"):
        value = getattr(args, name)
        if value is not None and (not math.isfinite(value) or value < 0):
            raise ValueError(name + " must be nonnegative and finite")
    if not 2 <= args.paths <= 10000 or args.paths % 2:
        raise ValueError("paths must be even and in [2,10000]")
    if not 0 <= args.start_index <= MAX_INDEX or args.start_index + args.paths - 1 > MAX_INDEX:
        raise ValueError("path indices exceed supported range")
    if not 1 <= args.jobs <= 32:
        raise ValueError("jobs must be in [1,32]")
    for name in ("seconds", "rate", "cn0", "block_seconds", "timeout", "term_grace",
                 "ray_coherence_seconds", "ray_gaussian_width_hz", "diurnal_phase_radians", "diurnal_period_seconds",
                 "outage_gain", "outage_ramp_seconds", "tap_delay_ms", "tap_doppler_hz"):
        if not math.isfinite(getattr(args, name)):
            raise ValueError(name + " must be finite")
    if not 8 <= args.seconds <= MAX_SECONDS or args.rate <= 0:
        raise ValueError("seconds must be in [8,315576000] and rate positive")
    if not 1 <= args.sample_rate_denominator <= (1 << 32)-1 or (args.sample_rate_denominator > 1 and args.sample_rate != 1):
        raise ValueError("sample rate denominator must be positive uint32; denominator >1 requires numerator 1")
    fs = args.sample_rate / args.sample_rate_denominator
    if not 1 <= args.sample_rate <= 10_000_000 or fs <= 4.125 * args.rate / 6:
        raise ValueError("sample rate fails the Added=1 shaped-band geometry guard")
    if args.block_seconds <= 0 or args.block_seconds * fs < 1:
        raise ValueError("a block must contain at least one sample")
    if not 0 < args.timeout <= 7 * 86400 or not 0 < args.term_grace <= 30:
        raise ValueError("timeout or cleanup grace exceeds bounded-runner limits")
    if args.ray_coherence_seconds < 0 or args.diurnal_phase_radians < 0 or args.diurnal_period_seconds <= 0:
        raise ValueError("invalid path evolution parameters")
    if args.ray_gaussian_width_hz < 0:
        raise ValueError("Gaussian ray width must be nonnegative")
    if args.ray_gaussian_width_hz > 0 and args.ray_coherence_seconds > 0:
        raise ValueError("Gaussian ray width and ray coherence cannot both be positive")
    if args.tap_delay_ms < 0 or args.tap_doppler_hz < 0:
        raise ValueError("receiver tap delay and Doppler must be nonnegative")
    if not math.isfinite(args.diurnal_phase_radians / args.diurnal_period_seconds):
        raise ValueError("diurnal instantaneous frequency exceeds numeric range")
    lo, hi = args.outage_seconds
    if not all(math.isfinite(v) for v in (lo, hi)) or not 0 <= args.outage_gain <= 1 or args.outage_ramp_seconds < 0:
        raise ValueError("invalid outage parameters")
    if (lo, hi) != (-1., -1.) and not (0 <= lo < hi and args.outage_ramp_seconds <= (hi-lo)/2):
        raise ValueError("outage needs ordered endpoints and ramps inside its interval")
    low, high, step = args.start
    if not all(math.isfinite(v) for v in args.start) or not low <= high or step <= 0:
        raise ValueError("invalid public timing bank")
    try:
        epochs = [int(v) for v in args.epoch_offsets.split(",")]
    except ValueError as exc:
        raise ValueError("epoch offsets must be comma-separated integers") from exc
    if not epochs or len(epochs) != len(set(epochs)) or any(abs(v) > 86400 for v in epochs):
        raise ValueError("epoch offsets must be distinct and within one day")
    timing_ratio = (high-low)/step
    if not math.isfinite(timing_ratio) or timing_ratio > 64:
        raise ValueError("public timing bank exceeds supported range")
    timing_count = math.floor(timing_ratio + 1e-9) + 1
    if 4 * timing_count * len(epochs) > 64:
        raise ValueError("public timing/epoch bank exceeds the export's 64-column cap")
    args.epoch_offsets = ",".join(map(str, epochs))
    args.binary = args.binary.expanduser().resolve(strict=True)
    if not args.binary.is_file() or not os.access(args.binary, os.X_OK):
        raise ValueError("binary must be an executable file")
    args.output = args.output.expanduser().resolve()
    if args.output.exists():
        raise FileExistsError("campaign output already exists: " + str(args.output))


def seed_for(args, index, domain):
    # Omit projection partition and physical stress parameters: paired block or
    # model comparisons, including RF and spread overrides, use the same roots.
    # This pairs random draws, not necessarily equal physical paths. Fs participates;
    # equal seeds at different Fs would not couple continuous stochastic paths.
    identity = ["triad-lowrate-campaign-v1", args.split, args.seconds, args.rate,
                args.sample_rate, args.cn0, index, index % 2, domain]
    # Preserve every existing integer-Fs fixture. Reciprocal-Fs roots have a
    # separate explicit identity; matching seeds do not couple sampled paths.
    if args.sample_rate_denominator != 1:
        identity += ["sample-rate-denominator", args.sample_rate_denominator]
    return int.from_bytes(hashlib.sha256(json.dumps(identity, separators=(",", ":")).encode()).digest()[:8], "little")


def make_command(args, index, output):
    seeds = {name + "_seed": seed_for(args, index, name) for name in SEED_DOMAINS}
    epoch = 1_800_000_000 + 60 * index
    cmd = [str(args.binary), "--mode", "export-stats", "--seconds", str(args.seconds),
           "--rate", str(args.rate), "--added", "1", "--sample-rate", str(args.sample_rate),
           "--sample-rate-denominator", str(args.sample_rate_denominator),
           "--rf-hz", format(args.rf_hz, ".17g"), "--base-oscillator", "gpsdo-ocxo", "--shift-oscillator", "gpsdo-ocxo",
           "--spread-preset", args.spread_preset, "--selective-perturbation", args.selective_perturbation,
           "--margin", "1", "--clock-sync", "GPS_10ms-1ms_region-0ms_offset", "--audio-error-ms", "70",
           "--basis", "two-ray", "--cn0", str(args.cn0), "--block-seconds", str(args.block_seconds),
           "--tap-delay-ms", str(args.tap_delay_ms), "--tap-doppler-hz", str(args.tap_doppler_hz),
           "--chunk", "4096", "--threads", "1", "--signal-bit", str(index % 2), "--epoch", str(epoch),
           "--control", args.control,
           "--stats-noisy", "--output", str(output), "--start", *map(str, args.start),
           "--epoch-offsets", args.epoch_offsets,
           "--ray-coherence-seconds", str(args.ray_coherence_seconds),
           "--diurnal-phase-radians", str(args.diurnal_phase_radians),
           "--diurnal-period-seconds", str(args.diurnal_period_seconds),
           "--outage-seconds", *map(str, args.outage_seconds),
           "--outage-gain", str(args.outage_gain), "--outage-ramp-seconds", str(args.outage_ramp_seconds)]
    if args.spread_delay_ms is not None:
        cmd += ["--spread-delay-ms", format(args.spread_delay_ms, ".17g")]
    if args.spread_doppler_hz is not None:
        cmd += ["--spread-doppler-hz", format(args.spread_doppler_hz, ".17g")]
    if args.ray_gaussian_width_hz > 0:
        cmd += ["--ray-gaussian-width-hz", format(args.ray_gaussian_width_hz, ".17g")]
    for domain in SEED_DOMAINS:
        cmd += ["--" + domain + "-seed", str(seeds[domain + "_seed"])]
    return cmd, {"source_bit": index % 2, "epoch": epoch, "control": args.control, **seeds}


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024**2), b""):
            digest.update(chunk)
    return digest.hexdigest()


def source_fingerprint():
    files = [ROOT / "tools" / "triad-long.cpp", Path(__file__), RUNNER]
    for directory in (ROOT / "src", ROOT / "include"):
        files += [p for p in directory.rglob("*") if p.is_file() and p.suffix in (".c", ".cpp", ".h", ".hpp")]
    entries = {str(p.relative_to(ROOT)): sha256_file(p) for p in sorted(set(files))}
    return {"files": entries, "sha256": hashlib.sha256(json.dumps(entries, sort_keys=True).encode()).hexdigest(),
            "scope": "driver, bounded runner, and src/include C/C++ inputs; binary hash is separate; no claim these sources built the supplied binary"}


def export_manifest(dataset):
    path = dataset / "manifest.json"
    manifest = json.loads(path.read_text())
    if manifest.get("format") != "triad-block-stats-v1" or manifest.get("stats_noisy") is not True:
        raise ValueError("missing actual noisy block-statistics export")
    sizes = manifest.get("file_sizes", {})
    for name in EXPORT_FILES:
        size = sizes.get(name)
        if isinstance(size, bool) or not isinstance(size, int) or size < 0 or (dataset / name).stat().st_size != size:
            raise ValueError("incomplete or inconsistent export file: " + name)
    return manifest


def one_path(args, index, cancelled):
    dataset = args.output / f"path-{index:05d}"
    log = args.output / f"path-{index:05d}.log"
    status = args.output / f"path-{index:05d}.status.json"
    command, seeds = make_command(args, index, dataset)
    result = {"path_index": index, "directory": dataset.name, "command": command,
              "runner_started": False, **seeds}
    if cancelled.is_set():
        return {**result, "returncode": 130, "outcome": "not_started_cancelled"}
    runner_command = [sys.executable, str(RUNNER), "--cwd", str(ROOT), "--log", str(log),
                      "--status", str(status), "--timeout", str(args.timeout), "--max-seconds", str(args.timeout),
                      "--term-grace", str(args.term_grace), "--", *command]
    started = time.monotonic()
    try:
        process = subprocess.Popen(runner_command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
    except OSError as exc:
        return {**result, "returncode": 125, "outcome": "runner_not_started", "error": str(exc)}
    result["runner_started"] = True
    signalled = False
    while True:
        if cancelled.is_set() and not signalled and process.poll() is None:
            process.terminate()  # runner performs bounded group cleanup and records status
            signalled = True
        try:
            stdout, stderr = process.communicate(timeout=.1)
            break
        except subprocess.TimeoutExpired:
            continue
    result.update(returncode=process.returncode, elapsed_seconds=time.monotonic()-started)
    if stdout or stderr:
        result["runner_output"] = (stdout + stderr)[-8192:]
    try:
        receipt = json.loads(status.read_text())
        result["runner_status"] = receipt
        result["outcome"] = receipt["outcome"]
        if process.returncode == 0:
            if receipt.get("exit_code") != 0 or not receipt.get("termination", {}).get("group_empty"):
                raise ValueError("runner did not confirm successful cleanup")
            result["export_manifest"] = export_manifest(dataset)
    except (OSError, ValueError, KeyError) as exc:
        result.update(returncode=1, validation_error=str(exc))
    return result


def run(args):
    validate(args)
    fingerprint = source_fingerprint()
    binary_hash = sha256_file(args.binary)
    all_seeds = [seed_for(args, i, domain) for i in range(args.start_index, args.start_index + args.paths) for domain in SEED_DOMAINS]
    if len(set(all_seeds)) != len(all_seeds):
        raise RuntimeError("seed collision; refusing campaign")
    args.output.mkdir(parents=True, exist_ok=False)
    cancelled = threading.Event()
    previous = {}
    def cancel(_signum, _frame):
        cancelled.set()
    for sig in (signal.SIGINT, signal.SIGTERM):
        previous[sig] = signal.signal(sig, cancel)
    results = []
    next_index, end = args.start_index, args.start_index + args.paths
    failed = False
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            active = {}
            while active or (next_index < end and not failed and not cancelled.is_set()):
                while next_index < end and len(active) < args.jobs and not failed and not cancelled.is_set():
                    active[pool.submit(one_path, args, next_index, cancelled)] = next_index
                    next_index += 1
                if not active:
                    break
                done, _ = concurrent.futures.wait(active, timeout=.1, return_when=concurrent.futures.FIRST_COMPLETED)
                for future in done:
                    index = active.pop(future)
                    try:
                        result = future.result()
                    except Exception as exc:
                        result = {"path_index": index, "returncode": 1, "error": repr(exc)}
                    results.append(result)
                    failed |= result.get("returncode") != 0
    finally:
        for sig, handler in previous.items():
            signal.signal(sig, handler)
    started = sum(r.get("runner_started") is True for r in results)
    unknown_start = sum("runner_started" not in r for r in results)
    report = {"format": "triad-lowrate-campaign-v1", "binary_sha256": binary_hash, "control": args.control,
              "source_inputs": fingerprint, "arguments": {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
              "paths_requested": args.paths, "paths_dispatched": len(results), "paths_attempted": started,
              "paths_start_unknown": unknown_start,
              "paths_succeeded": sum(r.get("returncode") == 0 for r in results),
              "paths_not_started": args.paths-started-unknown_start, "cancelled": cancelled.is_set(),
              "attempt_accounting": "Attempted means bounded runner subprocess successfully launched; cancellation before launch is not started. Unexpected worker exceptions with unavailable launch evidence are counted separately as start unknown.",
              "seed_derivation": "SHA256(split, duration, rate, Fs numerator, CN0, global path index, bit, domain; append labeled denominator only when !=1); integer-Fs legacy roots preserved; independent key/channel/oscillator/noise/frontend/path roots; block size, RF, spread preset/overrides, controls and path stress settings excluded to pair random roots, not necessarily identical physical paths",
              "acquisition_scope": "Public nominal or explicit timing/epoch bank; configured timing, oscillator and sample-clock truth remain independently drawn. A single nominal branch is not known alignment and does not cover all declared uncertainties.",
              "model_scope": "Encrypted one-bit sampled source; Added Symbols=1, FHSS off, burst off, GPSDO OCXO endpoints, margin 1. Defaults: RF 8270 Hz and sub9-best two rays; selected RF, spread preset and nominal physical overrides are in arguments/export metadata. Receiver tap basis independently defaults to 0.005 ms/0.001 Hz and is never changed from physical truth. Explicit illustrative models, not field calibration. C/N0 supplied externally; no antenna efficiency assumed.",
              "results": sorted(results, key=lambda r: r["path_index"])}
    with (args.output / "campaign.json").open("x") as stream:
        json.dump(report, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write("\n")
    print(args.output / "campaign.json")
    return int(failed or cancelled.is_set() or len(results) != args.paths)


def main():
    args = parser().parse_args()
    try:
        return run(args)
    except (OSError, ValueError, RuntimeError) as exc:
        print("triad-lowrate-campaign: " + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
