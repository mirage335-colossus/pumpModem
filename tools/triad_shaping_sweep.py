#!/usr/bin/env python3
"""Bounded native shaping census; no waveform generation or receiver campaign.

Keep planner feasibility separate from demonstrated channel/receiver support.
This is an occasional qualification tool, not a routine CI timing gate.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import time


def cases():
    result = []
    for rate in (.001, .01, .1, 1, 3.6, 10, 36, 100, 360, 1200, 3600, 12000, 24000):
        result.append(dict(name=f"continuous-{rate:g}", rate=rate, added=1, fhss=False, burst=0))
    for rate in (100, 360, 1000, 1200, 3600):
        result.append(dict(name=f"fhss-{rate:g}", rate=rate, added=1, fhss=True, burst=0))
    for rate, added in ((.36, 1000), (3.6, 100), (36, 10)):
        for fhss in (False, True):
            result.append(dict(name=f"fine60-{rate:g}-x{added}-fhss{int(fhss)}",
                               rate=rate, added=added, fhss=fhss, burst=0))
    result.extend([
        dict(name="continuous-burst3", rate=36, added=10, fhss=False, burst=3),
        dict(name="fhss-burst3", rate=360, added=1, fhss=True, burst=3),
    ])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exporter", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--data-if", type=float, default=1725)
    parser.add_argument("--tail-fraction", type=float, default=.001)
    parser.add_argument("--timeout-seconds", type=float, default=60)
    args = parser.parse_args()
    if (not math.isfinite(args.timeout_seconds) or args.timeout_seconds <= 0 or
            not math.isfinite(args.data_if) or args.data_if < 1725 or
            not math.isfinite(args.tail_fraction) or not 0 < args.tail_fraction <= .005):
        parser.error("positive finite timeout, DATA IF >=1725 and tail fraction in (0,.005] required")
    if args.output.exists():
        parser.error("output exists; retain the original census")
    binary = args.exporter.resolve(strict=True)
    binary_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
    script_hash = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    frozen = cases()
    rows = []
    start = time.perf_counter()
    for case in frozen:
        command = [str(binary), "--case", "eme", "--plan-only", "--shaping", "auto",
                   "--rate", str(case["rate"]), "--added-symbols", str(case["added"]),
                   "--fhss", "on" if case["fhss"] else "off", "--burst-wait-tiles", str(case["burst"]),
                   "--seconds", "40", "--data-if", str(args.data_if),
                   "--shaping-tail-fraction", str(args.tail_fraction)]
        mark = time.perf_counter()
        try:
            process = subprocess.run(command, capture_output=True, text=True, timeout=args.timeout_seconds)
            try:
                plan = json.loads(process.stdout)
            except json.JSONDecodeError:
                plan = None
            row = dict(case=case, command=command, returncode=process.returncode,
                       plan=plan, stdout=process.stdout if plan is None else None, stderr=process.stderr)
        except subprocess.TimeoutExpired:
            row = dict(case=case, command=command, status="planner-timeout-not-qualified")
        row["wall_seconds"] = time.perf_counter() - mark
        rows.append(row)
        print(json.dumps(dict(case=case["name"], seconds=row["wall_seconds"],
                              returncode=row.get("returncode"))), flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    identities_stable = (binary_hash == hashlib.sha256(binary.read_bytes()).hexdigest() and
                         script_hash == hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    with args.output.open("x") as stream:
        json.dump(dict(schema="triad-shaping-planner-census-v1", cases=frozen, results=rows,
                       executable_sha256=binary_hash, script_sha256=script_hash,
                       source_identity_stable=identities_stable,
                       configuration=dict(data_if_hz=args.data_if, tail_fraction=args.tail_fraction),
                       total_wall_seconds=time.perf_counter() - start,
                       scope="Planner/geometry only. No sampled channel, reception, physical bandwidth or sensitivity qualification."),
                  stream, indent=2, allow_nan=False)
        stream.write("\n")
    if not identities_stable:
        raise SystemExit("census invalid: executable or script changed during measurement")


if __name__ == "__main__":
    main()
