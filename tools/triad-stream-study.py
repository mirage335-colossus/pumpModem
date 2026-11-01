#!/usr/bin/env python3
"""Bounded reproducible campaigns for the isolated triad_long executable."""

import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def seed_for(group, base, trial):
    payload = f"triad-stream-study-v1\0{base}\0{group}\0{trial}".encode()
    return int.from_bytes(hashlib.sha256(payload).digest()[:8], "big") & ((1 << 63) - 1)


def grid(dimension, values):
    return [dimension, *(format(value, ".12g") for value in values)]


def make_cases(binary, suite, trials, base_seed):
    cases = []

    def add(name, group, options, count=None, expected="run", note=""):
        for trial in range(trials if count is None else count):
            cases.append({
                "index": len(cases), "case": name, "group": group,
                "trial": trial, "expected": expected, "note": note,
                "expected_stderr": ("stream channel sample clock aliases" if
                                     expected == "cli_rejection" else None),
                "argv": [str(binary), "--mode", "run", "--trials", "1",
                         "--threads", "1", "--seed", str(seed_for(group, base_seed, trial)),
                         *options],
            })

    acquisition = [*grid("--start", (-.2, .2, .2)),
                   *grid("--frequency", (-.25, .25, .25))]
    if suite == "short":
        for duration in (16, 60, 240):
            for cn0 in (8, 4, 0, -4):
                for channel, basis in (("none", "flat"), ("hf", "two-ray")):
                    add(f"main_t{duration}_cn{cn0}_{channel}",
                        f"main_t{duration}_{channel}",
                        ["--seconds", str(duration), "--cn0", str(cn0),
                         "--channel", channel, "--basis", basis, "--block", "1536",
                         ],
                        note="Paired seeds across SNR points within the same duration/channel family.")
        for block_seconds in (.1, .2, .4, .8, 3.2, 6.4):
            add(f"block_{block_seconds:g}s", "block_sweep_hf_t60_cn4",
                ["--seconds", "60", "--cn0", "4", "--channel", "hf",
                 "--basis", "two-ray", "--block", str(round(block_seconds * 960)),
                 ],
                note="Block duration converted to sample rows; seeds paired across block sizes.")
        for control in ("null", "wrong-key"):
            add(f"control_{control}", f"control_{control}",
                ["--seconds", "60", "--cn0", "4", "--channel", "hf",
                 "--basis", "two-ray", "--control", control, "--block", "1536",
                 *acquisition], count=min(trials, 4),
                note="Null admissions and wrong-key admissions are counted separately.")
        add("rate360_fhss_envelope", "rate360_fhss_envelope",
            ["--rate", "360", "--seconds", "16", "--cn0", "20", "--fhss",
             "--block-seconds", ".4", "--channel", "none", "--basis", "flat"],
            note="Transmitter envelope only; v3 API does not retune a physical hop calendar.")
        add("burst_wait_tile", "burst_wait_tile",
            ["--seconds", "60", "--cn0", "4", "--burst-wait-tiles", "1",
             "--block", "1536"],
            note="One silent wait tile is included in the one-bit observation.")
        add("dwell_dropout_10ms", "dwell_dropout_10ms",
            ["--seconds", "60", "--cn0", "4", "--dropout-ms", "10",
             "--dropout-start-ms", "200", "--dropout-every-dwells", "1",
             "--block", "1536"])
        add("clock_free_30ppm", "clock_free_30ppm",
            ["--seconds", "60", "--cn0", "4", "--clock-ppm", "-30,0,30",
             "--block", "1536"])
        add("microwave_out_of_capture", "microwave_out_of_capture",
            ["--seconds", "16", "--cn0", "8", "--rf-hz", "5800000000",
             "--sample-rate", "960", "--frequency", "-6000", "6000", "100",
             "--block", "1536"], count=min(trials, 2),
            expected="cli_rejection",
            note="Expected rejection: RF offset prior exceeds the 960-Hz baseband capture.")
        add("blind_timing_frequency_epoch", "blind_timing_frequency_epoch",
            ["--seconds", "16", "--cn0", "8", "--channel", "hf",
             "--basis", "two-ray", "--start", "-.01", ".01", ".005",
             "--frequency", "-.3", ".3", ".1", "--epoch-offsets", "-1,0,1",
             "--memory-mib", "512", "--block", "1536"],
            count=min(trials, 4), note="Separate small blind acquisition bank; not paired with known-alignment cases.")
    else:
        colored = ["--phase-diffusion", "0.00872665", "--white-fm", "0.002",
                   "--random-walk-fm", "0.000001", "--flicker-fm", "0.002",
                   "--flicker-tau", "0.1", "86400", "--white-pm", "0.01",
                   "--channel", "hf", "--basis", "two-ray",
                   "--sample-rate", "64", "--rate", "3.6", "--block", "1536"]
        for duration in (3600, 14400, 43200, 172800):
            for cn0 in (-14, -20, -24):
                add(f"long_t{duration}_cn{cn0}_colored_hf", f"long_colored_hf_t{duration}",
                    ["--seconds", str(duration), "--cn0", str(cn0), *colored],
                    note="Actual sample coordinates with continuous finite-band synthetic oscillator/channel state.")
        for duration in (3600, 43200, 172800):
            add(f"long_t{duration}_cn-20_flat_no_fm", f"long_flat_no_fm_t{duration}",
                ["--seconds", str(duration), "--cn0", "-20", "--phase-diffusion", "0",
                 "--channel", "none", "--basis", "flat", "--sample-rate", "64",
                 "--rate", "3.6", "--block", "1536"], count=min(trials, 2),
                note="Flat-channel, no-phase-noise model control; not calibrated device data.")
    return cases


def parse_jsonl(text):
    records, errors = [], []
    for line_number, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            continue
        try:
            item = json.loads(line)
            if not isinstance(item, dict):
                raise ValueError("expected an object")
            records.append(item)
        except (json.JSONDecodeError, ValueError) as exc:
            errors.append(f"line {line_number}: {exc}")
    return records, errors


def run_case(case, output, timeout_seconds):
    directory = output / f"case-{case['index']:05d}"
    directory.mkdir()
    started = time.time()
    try:
        proc = subprocess.run(case["argv"], stdin=subprocess.DEVNULL,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True, encoding="utf-8", errors="replace",
                              shell=False, check=False, timeout=timeout_seconds)
        stdout, stderr, returncode, timed_out = proc.stdout, proc.stderr, proc.returncode, False
    except subprocess.TimeoutExpired as exc:
        def as_text(value):
            if isinstance(value, bytes):
                return value.decode("utf-8", "replace")
            return value or ""
        stdout, stderr, returncode, timed_out = as_text(exc.stdout), as_text(exc.stderr), None, True
    except OSError as exc:
        stdout, stderr, returncode, timed_out = "", f"launch error: {exc}", None, False
    (directory / "stdout.txt").write_text(stdout, encoding="utf-8")
    (directory / "stderr.txt").write_text(stderr, encoding="utf-8")
    records, parse_errors = parse_jsonl(stdout)
    for item in records:
        item["campaign_case"] = case["case"]
        item["campaign_group"] = case["group"]
        item["campaign_trial"] = case["trial"]
    if records:
        lines = [json.dumps(item, sort_keys=True, allow_nan=False) for item in records]
        (directory / "results.jsonl").write_text("\n".join(lines) + "\n", encoding="utf-8")
    expected = case["expected"]
    if expected == "cli_rejection":
        satisfied = (returncode not in (None, 0) and not records and
                     case["expected_stderr"] in stderr)
    elif expected == "no_reception":
        satisfied = (returncode == 0 and not parse_errors and bool(records) and
                     all(item.get("complete_observation") is True and
                         item.get("accepted") is False for item in records))
    else:
        satisfied = (returncode == 0 and not timed_out and not parse_errors and
                     len(records) == 1 and records[0].get("complete_observation") is True)
    correct = sum(item.get("correct") is True for item in records)
    wrong = sum(item.get("wrong_accepted") is True for item in records)
    missed = sum(item.get("missed") is True for item in records)
    null_false = sum(item.get("control") == "null" and
                     item.get("false_admission") is True for item in records)
    wrong_key_false = sum(item.get("control") == "wrong-key" and
                          item.get("false_admission") is True for item in records)
    incomplete = sum(item.get("complete_observation") is not True for item in records)
    classification = ("timeout" if timed_out else "expected_outcome" if satisfied else
                      "incomplete" if incomplete else "failed_or_unexpected")
    return {
        "index": case["index"], "case": case["case"], "group": case["group"],
        "trial": case["trial"], "expected": expected, "note": case["note"],
        "argv": case["argv"], "return_code": returncode,
        "timeout_seconds": timeout_seconds, "timed_out": timed_out,
        "started_unix": started, "elapsed_seconds": time.time() - started,
        "stdout_records": len(records), "parse_errors": parse_errors,
        "correct_accepted": correct, "wrong_accepted": wrong, "missed": missed,
        "null_false_admissions": null_false, "wrong_key_false_admissions": wrong_key_false,
        "incomplete_observations": incomplete,
        "expected_outcome_satisfied": satisfied,
        "classification": classification,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--source", type=Path,
                        help="source file to hash (default: neighboring triad-long.cpp)")
    parser.add_argument("--output", required=True, type=Path,
                        help="new output directory; existing paths are rejected")
    parser.add_argument("--workers", type=int, default=2)
    parser.add_argument("--trials", type=int, default=16)
    parser.add_argument("--base-seed", type=int, default=20261031)
    parser.add_argument("--timeout", type=float, default=600,
                        help="maximum seconds per subprocess (default: 600)")
    parser.add_argument("--suite", choices=("short", "long"), default="short")
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    script = Path(__file__).resolve()
    source = args.source.resolve(strict=True) if args.source else script.with_name("triad-long.cpp")
    if not binary.is_file() or not source.is_file():
        parser.error("--binary and --source must identify regular files")
    if not 1 <= args.workers <= 64 or not 1 <= args.trials <= 10000 or args.timeout <= 0:
        parser.error("--workers must be 1..64, --trials 1..10000, and --timeout positive")
    output = args.output.resolve()
    if output.exists():
        parser.error(f"output path already exists: {output}")
    output.mkdir(parents=True)
    cases = make_cases(binary, args.suite, args.trials, args.base_seed)
    manifest = {
        "schema": "triad-stream-study-v1", "suite": args.suite,
        "trials_requested_per_case": args.trials, "workers": args.workers,
        "timeout_seconds": args.timeout, "base_seed": args.base_seed, "binary_path": str(binary),
        "binary_sha256": sha256(binary), "source_path": str(source),
        "source_sha256": sha256(source), "script_path": str(script),
        "script_sha256": sha256(script), "case_count": len(cases),
        "campaign_note": "Only cases in the same named group reuse deterministic seed indices. Candidate scans are correlated and do not count as independent trials; this runner does not establish operational FAP calibration.",
        "cases": cases,
    }
    (output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True, allow_nan=False) + chr(10),
        encoding="utf-8")
    print(f"prepared {len(cases)} runs ({args.suite}) in {output}", file=sys.stderr)

    outcomes = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = [pool.submit(run_case, case, output, args.timeout) for case in cases]
        for future in concurrent.futures.as_completed(futures):
            result = future.result()
            outcomes.append(result)
            print(f"{result['index'] + 1}/{len(cases)} {result['case']} "
                  f"trial={result['trial']} {result['classification']} "
                  f"rc={result['return_code']}", file=sys.stderr)
    outcomes.sort(key=lambda row: row["index"])
    with (output / "outcomes.jsonl").open("w", encoding="utf-8") as stream:
        for item in outcomes:
            stream.write(json.dumps(item, sort_keys=True, allow_nan=False) + chr(10))
    summary = {
        "schema": "triad-stream-study-summary-v1", "suite": args.suite,
        "runs": len(outcomes),
        "expected_outcomes_satisfied": sum(x["expected_outcome_satisfied"] for x in outcomes),
        "failed_or_unexpected": sum(not x["expected_outcome_satisfied"] for x in outcomes),
        "incomplete_observations": sum(x["incomplete_observations"] for x in outcomes),
        "correct_accepted": sum(x["correct_accepted"] for x in outcomes),
        "wrong_accepted": sum(x["wrong_accepted"] for x in outcomes),
        "missed": sum(x["missed"] for x in outcomes),
        "null_false_admissions": sum(x["null_false_admissions"] for x in outcomes),
        "wrong_key_false_admissions": sum(x["wrong_key_false_admissions"] for x in outcomes),
        "candidate_hypotheses_are_not_independent_trials": True,
    }
    (output / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + chr(10), encoding="utf-8")
    print(json.dumps(summary, sort_keys=True))
    return 0 if summary["failed_or_unexpected"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())

