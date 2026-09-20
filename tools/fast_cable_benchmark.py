#!/usr/bin/env python3
"""Plan or run real default-device Fast cable transfers, with exact file checks.

Examples (decimal bytes; a plan never opens an audio device):
  python3 tools/fast_cable_benchmark.py --sizes 4096 --trials 1
  python3 tools/fast_cable_benchmark.py --live --output /tmp/cable-screen \
      --sizes 4096 --trials 1 --candidate 256:7/8:high-rate:64
  python3 tools/fast_cable_benchmark.py --live --output /tmp/cable-long \
      --sizes 100000 5000000 50000000 --trials 14 \
      --candidate 256:7/8:high-rate:64

The tool uses the existing public/checksum-protected CLI. No simulation or WAV
fallback is allowed. Source fixtures are deterministic; production bootstrap
salts remain random. Device settings, cable, noise and independent errors must
be established by the operator. A file checksum does not measure raw BER/SNR.
Output routing follows the selected executable's profile default unless
--stereo (both channels) or --mono (right only) is supplied. Effective routing
is recorded separately from an explicit override when fast-info exposes it.
"""

import argparse
import csv
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import signal
import subprocess
import sys
import time


DEFAULT_CANDIDATES = (
    "16:3/4:robust:16", "64:3/4:robust:16", "256:3/4:robust:16",
    "256:7/8:robust:16", "256:7/8:high-rate:16", "256:7/8:high-rate:64",
    "256:7/8:high-rate:62",
)


@dataclass(frozen=True)
class Candidate:
    apsk: int
    code_rate: str
    rs: str
    interleave: int

    @property
    def name(self):
        return f"apsk{self.apsk}-r{self.code_rate.replace('/', '_')}-{self.rs}-d{self.interleave}"

    def options(self):
        return ["--apsk", str(self.apsk), "--code-rate", self.code_rate,
                "--rs", self.rs, "--interleave", str(self.interleave)]


def parse_candidate(text):
    try:
        apsk, rate, rs, depth = text.split(":")
        value = Candidate(int(apsk), rate, rs, int(depth))
        if (value.apsk not in (4, 16, 64, 256) or rate not in ("1/2", "3/4", "7/8")
                or rs not in ("robust", "high-rate") or not 1 <= value.interleave <= 64):
            raise ValueError()
        return value
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "candidate must be APSK:RATE:RS:DEPTH, e.g. 256:7/8:high-rate:64") from error


def bounded_integer(minimum, maximum):
    def parse(text):
        try:
            result = int(text)
        except ValueError as error:
            raise argparse.ArgumentTypeError("expected decimal integer") from error
        if not minimum <= result <= maximum:
            raise argparse.ArgumentTypeError(f"expected {minimum}..{maximum}")
        return result
    return parse


def finite_positive(text):
    try:
        result = float(text)
    except ValueError as error:
        raise argparse.ArgumentTypeError("expected positive finite number") from error
    if not math.isfinite(result) or result <= 0:
        raise argparse.ArgumentTypeError("expected positive finite number")
    return result


def airtime(info, candidate, source_bytes):
    """Current wire geometry, independently cross-checkable with TX result."""
    capacity = candidate.interleave * info["source_bytes_per_group"] * 8
    cycles = 1 + (9 * (source_bytes + 1) + capacity - 1) // capacity
    intervals = cycles * info["cycle_intervals"]
    symbols = 128 + intervals * info["interval_symbols"] - 1 + 16
    samples = math.floor(symbols * info["sample_rate"] / info["symbol_rate"]) + 1
    samples += info["sample_rate"] * 25 // 4
    return {"intervals": intervals, "seconds": samples / info["sample_rate"],
            "source_bps": 8 * source_bytes * info["sample_rate"] / samples}


def routing_options(args):
    return ["--stereo"] if args.stereo else ["--mono"] if args.mono else []


def routing_provenance(args, info):
    requested = "both-channels" if args.stereo else "right-only" if args.mono else "profile-default"
    mono = info.get("mono")
    if isinstance(mono, bool):
        effective, evidence = "right-only" if mono else "both-channels", "fast-info"
    elif args.stereo or args.mono:
        effective, evidence = requested, "explicit-option"
    else:
        # Old binaries did not expose this field. Omission of --stereo alone
        # cannot identify the effective route across changing profile defaults.
        effective, evidence = None, "unreported"
    return {"requested": requested, "effective": effective, "evidence": evidence}


def binomial_tail(n, k, probability, upper):
    if probability == 0:
        return float(k == 0) if upper else 1.0
    if probability == 1:
        return 1.0 if upper else float(k == n)
    indices = range(k, n + 1) if upper else range(k + 1)
    terms = [math.lgamma(n + 1) - math.lgamma(j + 1) - math.lgamma(n - j + 1)
             + j * math.log(probability) + (n - j) * math.log1p(-probability)
             for j in indices]
    largest = max(terms)
    return min(1.0, math.exp(largest) * math.fsum(math.exp(t - largest) for t in terms))


def exact_bounds(successes, trials, alpha=0.05):
    """Separate exact one-sided 95% bounds; not a joint 95% interval."""
    if trials == 0:
        return None, None
    if not 0 <= successes <= trials:
        raise ValueError("invalid successes/trials")
    def solve(upper):
        lo, hi = 0.0, 1.0
        for _ in range(60):
            midpoint = (lo + hi) / 2
            tail = binomial_tail(trials, successes, midpoint, upper)
            if (tail < alpha) == upper:
                lo = midpoint
            else:
                hi = midpoint
        return (lo + hi) / 2
    return (0.0 if successes == 0 else solve(True),
            1.0 if successes == trials else solve(False))


def hash_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while block := handle.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def write_fixture(path, size, seed):
    digest = hashlib.sha256()
    domain = b"DataPump/fast-cable-benchmark/v1/" + seed.to_bytes(8, "big")
    with path.open("xb") as handle:
        for block_number, start in enumerate(range(0, size, 65536)):
            block = hashlib.shake_256(domain + block_number.to_bytes(8, "big")).digest(min(65536, size - start))
            handle.write(block)
            digest.update(block)
    return digest.hexdigest()


def load_report(path):
    try:
        value = json.loads(path.read_text())
        return value if isinstance(value, dict) else {"error": "CLI JSON is not an object"}
    except (ValueError, OSError) as error:
        return {"error": f"No valid CLI report: {error}"}


def stop_process(process):
    if process is None or process.poll() is not None:
        return
    process.send_signal(signal.SIGINT)
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def run_trial(args, candidate, size, trial, info, source, source_hash):
    directory = args.output / f"{candidate.name}-bytes{size}-trial{trial:04d}"
    directory.mkdir()
    received = directory / "received.bin"
    estimate = airtime(info, candidate, size)
    deadline_seconds = args.startup_seconds + estimate["seconds"] * args.timeout_factor + args.timeout_margin
    common = ["--profile", "wire", "--no-encryption", *candidate.options(),
              "--sample-rate", str(args.sample_rate), "--json", *routing_options(args)]
    rx_command = [str(args.pump), "fast-listen", *common, "--device", args.input_device,
                  "--save", str(received), "--seconds", str(math.ceil(deadline_seconds))]
    tx_command = [str(args.pump), "fast-tx", *common, "--device", args.output_device,
                  "--input", str(source), "--seconds", str(math.ceil(deadline_seconds))]
    record = {"candidate": candidate.name, **asdict(candidate), "source_bytes": size,
              "trial": trial, "source_sha256": source_hash, "estimated_seconds": estimate["seconds"],
              "expected_intervals": estimate["intervals"], "started_utc": datetime.now(timezone.utc).isoformat(),
              "rx_command": rx_command, "tx_command": tx_command,
              "output_routing": routing_provenance(args, info),
              "attempted": False, "timeout": False, "exact": False,
              "aborted_on_transfer_error": False, "premature_receive_completion": False,
              "audio_recovery_counters": None, "raw_ber": None, "measured_snr_db": None}
    processes = []
    started = time.monotonic()
    tx_started = None
    tx_finished = None
    rx_finished = None
    try:
        with (directory / "rx.stdout.json").open("w") as rx_out, (directory / "rx.stderr.txt").open("w") as rx_err, \
                (directory / "tx.stdout.json").open("w") as tx_out, (directory / "tx.stderr.txt").open("w") as tx_err:
            rx = subprocess.Popen(rx_command, stdout=rx_out, stderr=rx_err)
            processes.append(rx)
            # The existing CLI has no device-ready event. Preserve a documented
            # startup allowance and check early failures before starting output.
            while time.monotonic() - started < args.startup_seconds and rx.poll() is None:
                time.sleep(0.05)
            if rx.poll() is not None:
                record["setup_error"] = "receiver exited before transmission"
            else:
                tx_started = time.monotonic()
                tx = subprocess.Popen(tx_command, stdout=tx_out, stderr=tx_err)
                processes.append(tx)
                record["attempted"] = True
                while True:
                    now = time.monotonic()
                    if rx_finished is None and rx.poll() is not None:
                        rx_finished = now
                    if tx_finished is None and tx.poll() is not None:
                        tx_finished = now
                    if rx_finished is not None and rx.returncode != 0:
                        record["aborted_on_transfer_error"] = True
                        record["abort_reason"] = "receiver exited unsuccessfully; stopped remaining playback"
                        break
                    if tx_finished is not None and tx.returncode != 0:
                        record["aborted_on_transfer_error"] = True
                        record["abort_reason"] = "transmitter exited unsuccessfully; stopped receiver"
                        break
                    # Receiver can legitimately finish ~0.25s before TX's
                    # 6.25s silence drains. A larger lead over the exact sample
                    # estimate indicates another reception or physical gap.
                    if rx_finished is not None and rx_finished - tx_started < estimate["seconds"] - 1.0:
                        record["premature_receive_completion"] = True
                        record["abort_reason"] = "receiver completed before this waveform could finish"
                        break
                    if rx_finished is not None and tx_finished is not None:
                        break
                    if now - started >= deadline_seconds:
                        record["timeout"] = True
                        break
                    time.sleep(0.05)
    finally:
        for process in reversed(processes):
            stop_process(process)
    record["wall_seconds"] = time.monotonic() - started
    record["tx_wall_seconds"] = None if tx_started is None or tx_finished is None else tx_finished - tx_started
    record["receive_delivery_seconds"] = None if tx_started is None or rx_finished is None else rx_finished - tx_started
    record["rx_returncode"] = processes[0].returncode if processes else None
    record["tx_returncode"] = processes[1].returncode if len(processes) == 2 else None
    record["rx"] = load_report(directory / "rx.stdout.json")
    record["tx"] = load_report(directory / "tx.stdout.json")
    reported_estimate = record["tx"].get("estimated_seconds")
    record["airtime_estimate_matches_cli"] = (math.isclose(reported_estimate, estimate["seconds"], rel_tol=1e-5, abs_tol=1e-4)
                                               if isinstance(reported_estimate, (int, float)) else None)
    record["rx_stderr"] = (directory / "rx.stderr.txt").read_text(errors="replace")
    record["tx_stderr"] = (directory / "tx.stderr.txt").read_text(errors="replace")
    record["received_bytes"] = received.stat().st_size if received.exists() else None
    record["received_sha256"] = hash_file(received) if received.exists() else None
    record["exact"] = (record["received_bytes"] == size and record["received_sha256"] == source_hash
                       and record["rx"].get("complete") is True and record["rx"].get("physical_complete") is True
                       and record["rx_returncode"] == 0 and record["tx_returncode"] == 0 and not record["timeout"]
                       and not record["premature_receive_completion"] and record["airtime_estimate_matches_cli"] is True)
    duration = record["receive_delivery_seconds"]
    record["verified_goodput_bps"] = 8 * size / duration if record["exact"] and duration and duration > 0 else 0.0
    if received.exists() and not args.keep_received:
        received.unlink()
    record["received_retained"] = received.exists()
    (directory / "trial.json").write_text(json.dumps(record, indent=2) + "\n")
    return record


def summarize(records, candidates, sizes):
    result = []
    for candidate in candidates:
        for size in sizes:
            matching = [r for r in records if r["candidate"] == candidate.name and r["source_bytes"] == size]
            attempted = [r for r in matching if r["attempted"]]
            passed = [r for r in attempted if r["exact"]]
            low, high = exact_bounds(len(passed), len(attempted))
            duration = math.fsum(r["wall_seconds"] for r in attempted)
            result.append({"candidate": candidate.name, "source_bytes": size, "attempted_trials": len(attempted),
                           "setup_failures": len(matching) - len(attempted), "exact_transfers": len(passed),
                           "empirical_success_probability": len(passed) / len(attempted) if attempted else None,
                           "success_probability_95pct_lower_one_sided": low,
                           "success_probability_95pct_upper_one_sided": high,
                           "meets_80pct_at_95pct_confidence": low is not None and low >= 0.8,
                           "verified_bytes_per_wall_second": size * len(passed) / duration if duration else None})
    return result


def write_summary(args, records, candidates):
    summary = summarize(records, candidates, args.sizes)
    (args.output / "summary.json").write_text(json.dumps({
        "method": "Independent binomial file outcomes; stationarity/independence are assumptions, not measured facts. Each lower/upper bound is separately one-sided 95%; together they form a 90% interval.",
        "limitations": "No extrapolation between file sizes. No raw BER, SNR, or driver recovery counters are exposed by the CLI. Startup uses a timed allowance, not a device-ready signal. These runs alone cannot prove a physical route or rule out audio processing.",
        "results": summary}, indent=2) + "\n")
    with (args.output / "summary.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pump", type=Path, default=Path(__file__).resolve().parents[1] / "build/pump")
    parser.add_argument("--live", action="store_true", help="actually open audio and transmit; otherwise print a plan")
    parser.add_argument("--output", type=Path, help="new directory for live trial artifacts; must not already exist")
    parser.add_argument("--sizes", type=bounded_integer(1, 100000000), nargs="+", default=[100000, 5000000, 50000000], help="source sizes in decimal bytes")
    parser.add_argument("--trials", type=bounded_integer(1, 1000), default=1)
    parser.add_argument("--candidate", type=parse_candidate, action="append", help="repeatable APSK:RATE:RS:DEPTH")
    parser.add_argument("--sample-rate", type=bounded_integer(44100, 192000), default=48000)
    parser.add_argument("--input-device", default="default")
    parser.add_argument("--output-device", default="default")
    routing = parser.add_mutually_exclusive_group()
    routing.add_argument("--stereo", action="store_true", help="explicitly send both output channels; omission uses the executable's profile default")
    routing.add_argument("--mono", action="store_true", help="explicitly send the right output channel only; omission uses the executable's profile default")
    parser.add_argument("--seed", type=bounded_integer(0, 2**64 - 1), default=417)
    parser.add_argument("--startup-seconds", type=finite_positive, default=1.5)
    parser.add_argument("--timeout-factor", type=finite_positive, default=1.5)
    parser.add_argument("--timeout-margin", type=finite_positive, default=30.0)
    parser.add_argument("--keep-received", action="store_true", help="retain received files after SHA-256 validation")
    args = parser.parse_args(argv)
    args.pump = args.pump.resolve()
    candidates = list(dict.fromkeys(args.candidate or [parse_candidate(text) for text in DEFAULT_CANDIDATES]))
    args.sizes = list(dict.fromkeys(args.sizes))
    if args.live and args.output is None:
        parser.error("--live requires --output pointing to a new artifact directory")
    information = {}
    plan = []
    for candidate in candidates:
        process = subprocess.run([str(args.pump), "fast-info", "--profile", "wire", "--no-encryption",
                                  *candidate.options(), "--sample-rate", str(args.sample_rate), *routing_options(args)],
                                 capture_output=True, text=True, timeout=15, check=True)
        info = json.loads(process.stdout)
        information[candidate.name] = info
        for size in args.sizes:
            plan.append({"candidate": candidate.name, "source_bytes": size, "trials": args.trials,
                         **airtime(info, candidate, size)})
    metadata = {"mode": "live" if args.live else "plan", "pump": str(args.pump),
                "pump_sha256": hash_file(args.pump), "input_device": args.input_device,
                "output_device": args.output_device, "explicit_stereo": args.stereo,
                "explicit_mono": args.mono, "sample_rate": args.sample_rate,
                "output_routing": {name: routing_provenance(args, info) for name, info in information.items()},
                "seed": args.seed, "candidates": [asdict(c) for c in candidates], "geometry": information,
                "trials": plan, "minimum_playback_hours": sum(p["seconds"] * p["trials"] for p in plan) / 3600}
    if not args.live:
        print(json.dumps(metadata, indent=2))
        return 0
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / "plan.json").write_text(json.dumps(metadata, indent=2) + "\n")
    records = []
    # Rotate candidate order per trial to reduce a fixed time-order confound.
    # A round uses identical deterministic source bytes across all candidates.
    for trial in range(1, args.trials + 1):
        order = candidates[(trial - 1) % len(candidates):] + candidates[:(trial - 1) % len(candidates)]
        for size in args.sizes:
            source = args.output / f"source-{size}-trial{trial:04d}.bin"
            source_hash = write_fixture(source, size, (args.seed + trial - 1) % 2**64)
            try:
                for candidate in order:
                    print(f"Live trial {trial}/{args.trials}: {candidate.name}, {size} bytes", file=sys.stderr, flush=True)
                    record = run_trial(args, candidate, size, trial, information[candidate.name], source, source_hash)
                    records.append(record)
                    with (args.output / "trials.jsonl").open("a") as handle:
                        handle.write(json.dumps(record) + "\n")
                    write_summary(args, records, candidates)
                    print(f"  exact={record['exact']} wall={record['wall_seconds']:.2f}s error={record['rx'].get('error', '')}", file=sys.stderr, flush=True)
                    if not record["attempted"]:
                        print("Receiver failed before playback; stopping sweep. See retained device errors.", file=sys.stderr)
                        return 2
            finally:
                source.unlink()
    print(str(args.output / "summary.json"))
    return 0 if all(record["exact"] for record in records) else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"fast_cable_benchmark: {error}", file=sys.stderr)
        sys.exit(2)
