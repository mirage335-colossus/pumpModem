#!/usr/bin/env python3
"""Validate the retained ABBA samples and reproduce summary.csv on stdout."""
import argparse
import collections
import csv
import math
from pathlib import Path
import statistics
import sys


FIELDS = [
    "workload", "workers", "retained_bits", "attempts",
    "baseline_wall_seconds", "current_wall_seconds", "wall_change_percent",
    "baseline_cpu_seconds", "current_cpu_seconds",
    "pair1_change_percent", "pair2_change_percent",
    "baseline_attempts_per_second", "current_attempts_per_second",
    "baseline_equivalent_attempts_300s", "current_equivalent_attempts_300s",
]


def summarize(path):
    with path.open(newline="") as source:
        rows = list(csv.DictReader(source))
    if len(rows) != 17 * 4 * 7:
        raise ValueError("Expected 17 workloads, four runs and seven samples per run")
    groups = collections.defaultdict(list)
    order = {1: ("baseline", 1), 2: ("current", 1),
             3: ("current", 2), 4: ("baseline", 2)}
    workloads = list(dict.fromkeys(row["workload"] for row in rows))
    if len(workloads) != 17:
        raise ValueError("Expected 17 distinct workloads")
    for row in rows:
        version, run = row["version"], int(row["run"])
        if order.get(int(row["order"])) != (version, run):
            raise ValueError("The recorded acquisition order is not ABBA")
        if int(row["iterations"]) < 1:
            raise ValueError("Every sample needs a positive operation count")
        if float(row["wall_seconds"]) < 0.25 - 1e-9 or float(row["cpu_seconds"]) <= 0:
            raise ValueError("Every sample needs at least 250 ms wall time and positive process CPU time")
        groups[row["workload"], version, run].append(row)
    result = []
    metadata_fields = ["workers", "retained_bits", "attempts", "media_seconds", "result_signature"]
    for workload in workloads:
        metadata = set()
        medians = {}
        for version in ("baseline", "current"):
            for run in (1, 2):
                group = groups[workload, version, run]
                if sorted(int(row["sample"]) for row in group) != list(range(7)):
                    raise ValueError(f"{workload}: each version/run must contain samples 0 through 6 exactly once")
                metadata.update(tuple(row[field] for field in metadata_fields) for row in group)
                for clock in ("wall", "cpu"):
                    # The printed per-operation fields are rounded to integer
                    # nanoseconds. Divide sample totals by operation counts to
                    # retain precision for the 20 ns and 30 ns microcases.
                    values = [float(row[f"{clock}_seconds"]) / int(row["iterations"]) for row in group]
                    if not all(math.isfinite(value) and value > 0 for value in values):
                        raise ValueError(f"{workload}: invalid duration")
                    medians[version, run, clock] = statistics.median(values)
        if len(metadata) != 1:
            raise ValueError(f"{workload}: coverage, result signature or fixture metadata changed")
        workers, retained, attempts, _, _ = metadata.pop()
        attempts = int(attempts)
        means = {(version, clock): statistics.mean(medians[version, run, clock] for run in (1, 2))
                 for version in ("baseline", "current") for clock in ("wall", "cpu")}
        baseline, current = means["baseline", "wall"], means["current", "wall"]
        output = dict(zip(FIELDS[:4], (workload, int(workers), int(retained), attempts)))
        output.update(baseline_wall_seconds=baseline, current_wall_seconds=current,
                      wall_change_percent=100 * (current / baseline - 1),
                      baseline_cpu_seconds=means["baseline", "cpu"],
                      current_cpu_seconds=means["current", "cpu"])
        for run in (1, 2):
            output[f"pair{run}_change_percent"] = 100 * (
                medians["current", run, "wall"] / medians["baseline", run, "wall"] - 1)
        for version in ("baseline", "current"):
            rate = attempts / means[version, "wall"]
            output[f"{version}_attempts_per_second"] = rate
            output[f"{version}_equivalent_attempts_300s"] = 300 * rate
        result.append(output)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("samples", nargs="?", type=Path, default=Path(__file__).with_name("samples.csv"))
    args = parser.parse_args()
    rows = summarize(args.samples)
    writer = csv.DictWriter(sys.stdout, FIELDS, lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)


if __name__ == "__main__":
    main()
