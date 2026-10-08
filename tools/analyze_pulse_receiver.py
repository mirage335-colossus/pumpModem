#!/usr/bin/env python3
"""Analyze paired real-PCM pulse-receiver CSVs without third-party packages.

Bootstrap units are complete capture IDs, paired across receiver variants and
C/N0 points. Absolute point uncertainty and uncertainty in the paired difference
are reported separately. A coarse grid or an unbracketed crossing is explicitly
inconclusive; zero observed discordance is not a rare false-alarm measurement.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import random
import statistics
from collections import defaultdict
from pathlib import Path


CONFIG_FIELDS = (
    "case", "carrier_hz", "bandwidth_hz", "sample_rate", "chip_samples",
    "symbol_samples", "workspace_bytes", "keys", "epochs", "frequency_offset_hz",
    "clock_ppm", "phase_diffusion", "start_uncertainty_seconds",
    "interference_amplitude", "interference_frequency", "waveform_seed", "noise_only",
)


def quantile(values, probability):
    ordered = sorted(values)
    if not ordered:
        return None
    position = probability * (len(ordered) - 1)
    lower = int(position)
    return ordered[lower] + (ordered[min(lower + 1, len(ordered) - 1)] - ordered[lower]) * (position - lower)


def wilson(successes, trials, z=1.959963984540054):
    if not trials:
        return [0.0, 1.0]
    probability = successes / trials
    denominator = 1 + z * z / trials
    center = (probability + z * z / (2 * trials)) / denominator
    radius = z * math.sqrt(probability * (1 - probability) / trials + z * z / (4 * trials * trials)) / denominator
    return [max(0.0, center - radius), min(1.0, center + radius)]


def crossing(grid, probabilities, target):
    """Linear interpolation only inside a measured monotone bracket."""
    if any(left > right + 1e-12 for left, right in zip(probabilities, probabilities[1:])):
        return None
    if probabilities[0] >= target or probabilities[-1] < target:
        return None
    for i in range(1, len(grid)):
        if probabilities[i] >= target:
            fraction = (target - probabilities[i - 1]) / (probabilities[i] - probabilities[i - 1])
            return grid[i - 1] + fraction * (grid[i] - grid[i - 1])
    return None


def configuration(key):
    return dict(zip(CONFIG_FIELDS, key))


def pairing(rows):
    paired = defaultdict(dict)
    for row in rows:
        identity = tuple(row.get(name, "") for name in CONFIG_FIELDS) + (row["mode"], row["seed"], row["repeat"], row["input_cn0_db_hz"], row["instrumented"])
        variant = row["variant"]
        if variant not in ("raw_reference", "automatic"):
            raise ValueError(f"unknown receiver variant {variant!r}")
        if variant in paired[identity]:
            raise ValueError("duplicate paired capture row; do not count repeated files as independent evidence")
        paired[identity][variant] = row
    for pair in paired.values():
        if set(pair) != {"raw_reference", "automatic"}:
            raise ValueError("missing receiver variant in paired capture")
        raw, optimized = pair["raw_reference"], pair["automatic"]
        for name in ("hypotheses", "phase_groups", "drift_sections", "differential_window_samples", "samples", "capture_begin_sample"):
            if raw[name] != optimized[name]:
                raise ValueError(f"paired receiver coverage/input differs: {name}")
        if raw["pcm_samples_identical"] != "1" or optimized["pcm_samples_identical"] != "1":
            raise ValueError("input PCM identity was not asserted by capture producer")
    return paired


def performance(paired):
    groups = defaultdict(list)
    for identity, pair in paired.items():
        if identity[len(CONFIG_FIELDS)] == "performance":
            groups[(identity[:len(CONFIG_FIELDS)], identity[-1])].append(pair)
    result = []
    for (key, instrumented), pairs in sorted(groups.items()):
        output = {"configuration": configuration(key), "instrumented": instrumented == "1", "paired_runs": len(pairs)}
        for variant in ("raw_reference", "automatic"):
            samples = [pair[variant] for pair in pairs]
            values = {}
            for name in ("wall_seconds", "cpu_seconds", "construction_seconds", "frontend_seconds", "search_seconds", "kernel_seconds",
                         "frontend_cpu_seconds", "search_cpu_seconds", "kernel_cpu_seconds", "media_seconds", "max_push_seconds",
                         "acquisition_wall_seconds", "acquisition_media_seconds", "progress_latency_seconds", "progress_latency_wall_seconds"):
                numbers = [float(row[name]) for row in samples]
                available = [value for value in numbers if value >= 0]
                values[name] = statistics.median(available) if available else None
            for name in ("peak_workspace_bytes", "process_peak_rss_bytes", "progress_latency_samples", "hypotheses", "lattices", "cells", "segments"):
                values[name] = max(int(row[name]) for row in samples)
            values["backend"] = sorted({row["backend"] for row in samples})
            values["throughput_samples_per_wall_second"] = statistics.median(float(row["samples"]) / float(row["wall_seconds"]) for row in samples)
            values["throughput_media_per_wall_second"] = statistics.median(float(row["media_seconds"]) / float(row["wall_seconds"]) for row in samples)
            output[variant] = values
        ratios = [float(pair["raw_reference"]["wall_seconds"]) / float(pair["automatic"]["wall_seconds"]) for pair in pairs]
        output["paired_wall_speedup_median"] = statistics.median(ratios)
        output["paired_wall_speedup_range"] = [min(ratios), max(ratios)]
        output["score_maximum_relative_difference"] = max(float(pair["automatic"]["maximum_score_relative_difference"]) for pair in pairs)
        output["limits"] = ["These are repeated fixture timings, not extrapolated full-symbol timings.",
                            "Process peak RSS includes immutable input capture and is a process-lifetime high-water mark.",
                            "Acquisition -1 means the finite measured capture did not complete acquisition coverage."]
        if instrumented == "1":
            output["limits"].append("Component instrumentation is enabled; compare primary total timings with the uninstrumented paired run.")
        result.append(output)
    return result


def curves(paired, bootstrap_count, random_seed, numeric_bound, maximum_loss):
    groups = defaultdict(dict)
    for identity, pair in paired.items():
        if identity[len(CONFIG_FIELDS)] == "curve":
            key = identity[:len(CONFIG_FIELDS)]
            seed, repeat, cn0 = identity[len(CONFIG_FIELDS) + 1:len(CONFIG_FIELDS) + 4]
            capture_id = (seed, repeat)
            groups[key].setdefault(capture_id, {})[float(cn0)] = pair
    result = []
    family = max(1, 2 * len(groups))
    alpha = 0.05 / family
    generator = random.Random(random_seed)
    for key, captures in sorted(groups.items()):
        capture_ids = sorted(captures)
        grid = sorted(next(iter(captures.values())))
        if len(grid) < 2 or any(sorted(captures[identity]) != grid for identity in capture_ids):
            raise ValueError("each independent capture must have the same complete C/N0 grid")
        trials = len(capture_ids)
        outcomes = {variant: [[int(captures[identity][point][variant]["correct"]) for point in grid] for identity in capture_ids]
                    for variant in ("raw_reference", "automatic")}
        probabilities = {variant: [sum(row[i] for row in values) / trials for i in range(len(grid))] for variant, values in outcomes.items()}
        output = {"configuration": configuration(key), "independent_paired_captures": trials,
                  "grid_cn0_db_hz": grid, "criterion": "all requested bits exact in the matched bank after complete sampled observation",
                  "false_alarm_reference": "unchanged nominal receiver alpha and finite-search penalties; no measured rare-event rate",
                  "points": []}
        for i, point in enumerate(grid):
            values = {"cn0_db_hz": point}
            for variant in outcomes:
                successes = sum(row[i] for row in outcomes[variant])
                rows = [captures[identity][point][variant] for identity in capture_ids]
                values[variant] = {"successes": successes, "probability": successes / trials,
                                   "probability_95pct_wilson": wilson(successes, trials),
                                   "bit_errors": sum(int(row["bit_errors"]) for row in rows),
                                   "accepted_bits": sum(int(row["accepted_bits"]) for row in rows),
                                   "wrong_bank_bits": sum(int(row["wrong_bank_bits"]) for row in rows)}
            discordance = sum(outcomes["raw_reference"][j][i] != outcomes["automatic"][j][i] for j in range(trials))
            values["paired_discordances"] = discordance
            values["discordance_95pct_wilson"] = wilson(discordance, trials)
            output["points"].append(values)
        output["maximum_score_relative_difference"] = max(float(captures[identity][point]["automatic"]["maximum_score_relative_difference"])
                                                           for identity in capture_ids for point in grid)
        output["per_capture_nonmonotone_success"] = {variant: sum(any(row[i] > row[i + 1] for i in range(len(grid) - 1)) for row in values)
                                                     for variant, values in outcomes.items()}
        if key[-1] == "1":
            output["limits"] = ["Noise-only accepts are feasible controls, not a measurement of an extremely rare false-accept rate.",
                                "Zero events gives approximately3/n as a95% upper event-probability bound, not zero risk."]
            result.append(output)
            continue
        thresholds = []
        bootstraps = {target: {"raw": [], "optimized": [], "difference": []} for target in (.9, .99)}
        for _ in range(bootstrap_count):
            selected = [generator.randrange(trials) for _ in range(trials)]
            bootstrap_probabilities = {variant: [sum(values[j][i] for j in selected) / trials for i in range(len(grid))]
                                       for variant, values in outcomes.items()}
            for target, samples in bootstraps.items():
                raw = crossing(grid, bootstrap_probabilities["raw_reference"], target)
                optimized = crossing(grid, bootstrap_probabilities["automatic"], target)
                if raw is not None and optimized is not None:
                    samples["raw"].append(raw);samples["optimized"].append(optimized);samples["difference"].append(optimized - raw)
        grid_width = max(right - left for left, right in zip(grid, grid[1:]))
        for target, samples in bootstraps.items():
            raw = crossing(grid, probabilities["raw_reference"], target)
            optimized = crossing(grid, probabilities["automatic"], target)
            estimate = optimized - raw if raw is not None and optimized is not None else None
            bounded = len(samples["difference"]) >= (1 - alpha / 2) * bootstrap_count
            interval = [quantile(samples["difference"], alpha / 2), quantile(samples["difference"], 1 - alpha / 2)] if bounded else [None, None]
            # Both receiver thresholds can move within their sampled C/N0 bin.
            # A validated external numerical-equivalence bound may be tighter,
            # but this script cannot derive or validate that bound from counts.
            interpolation_bound = min(grid_width, numeric_bound) if numeric_bound is not None else grid_width
            conservative_upper = interval[1] + interpolation_bound if interval[1] is not None else None
            point = {"detection_probability": target, "raw_cn0_db_hz": raw, "optimized_cn0_db_hz": optimized,
                     "additional_required_cn0_db": estimate, "paired_bootstrap_difference_interval_db": interval,
                     "simultaneous_family_confidence": .95, "individual_interval_confidence": 1 - alpha,
                     "bootstrap_count": bootstrap_count, "bracketed_bootstrap_count": len(samples["difference"]),
                     "raw_absolute_bootstrap_interval_db": [quantile(samples["raw"], alpha / 2), quantile(samples["raw"], 1 - alpha / 2)] if bounded else [None, None],
                     "optimized_absolute_bootstrap_interval_db": [quantile(samples["optimized"], alpha / 2), quantile(samples["optimized"], 1 - alpha / 2)] if bounded else [None, None],
                     "maximum_grid_spacing_db": grid_width, "external_equivalence_bound_db": numeric_bound,
                     "additional_interpolation_or_external_bound_db": interpolation_bound, "conservative_upper_loss_db": conservative_upper,
                     "loss_below_requested_limit": conservative_upper is not None and conservative_upper < maximum_loss}
            if target == .99 and trials < 20000:
                point["tail_sampling_limit"] = f"At99% probability,{trials} captures give about{trials / 100:g} upper-tail failures; the absolute99% point may be poorly resolved."
            thresholds.append(point)
        output["thresholds"] = thresholds
        output["limits"] = ["Bootstrap resamples paired independent capture IDs together, retaining their whole C/N0 curves.",
                            "The reported difference is the difference of probability-curve crossings, not the quantile of per-seed differences.",
                            "Interpolation and bootstrap intervals are conditional on the measured impairment/key geometry.",
                            "No rare-event rate or unmeasured geometry is qualified by these curves."]
        result.append(output)
    return result


def statistics_crossings(paired, bootstrap_count, random_seed, maximum_loss):
    """Paired quantiles of continuous admission roots, with no grid smoothing."""
    groups = defaultdict(list)
    for identity, pair in paired.items():
        if identity[len(CONFIG_FIELDS)] == "statistics_crossing":
            groups[identity[:len(CONFIG_FIELDS)]].append(pair)
    result = []
    alpha = .05 / max(1, 2 * len(groups))
    generator = random.Random(random_seed)
    for key, pairs in sorted(groups.items()):
        if any(row["bracketed"] != "1" or row["monotone"] != "1" or
               row.get("numerical_enclosure_valid", "1") != "1" for pair in pairs for row in pair.values()):
            result.append({"configuration": configuration(key), "status": "inconclusive",
                           "reason": "Unbracketed or nonmonotone paired roots retained; no seeds may be omitted."})
            continue
        pairs.sort(key=lambda pair: float(pair["raw_reference"]["threshold_cn0_db_hz"]))
        raw = [float(pair["raw_reference"]["threshold_cn0_db_hz"]) for pair in pairs]
        optimized = [float(pair["automatic"]["threshold_cn0_db_hz"]) for pair in pairs]
        same_order = all(a <= b for a, b in zip(optimized, optimized[1:]))
        trials = len(pairs)
        root_error = max(float(row["root_error_db"]) for pair in pairs for row in pair.values())
        numerical_error = max(float(row.get("numerical_root_error_bound_db") or 0) for pair in pairs for row in pair.values())
        boot = {target: {"raw": [], "optimized": [], "difference": []} for target in (.9, .99)}
        for _ in range(bootstrap_count):
            indices = sorted(generator.choices(range(trials), k=trials))
            raw_draw = [raw[i] for i in indices]
            opt_draw = [optimized[i] for i in indices]
            if not same_order:
                opt_draw.sort()
            for target, values in boot.items():
                position = target * (trials - 1)
                lo = int(position); hi = min(lo + 1, trials - 1); fraction = position - lo
                r = raw_draw[lo] + (raw_draw[hi] - raw_draw[lo]) * fraction
                o = opt_draw[lo] + (opt_draw[hi] - opt_draw[lo]) * fraction
                values["raw"].append(r); values["optimized"].append(o); values["difference"].append(o-r)
        output = {"configuration": configuration(key), "status": "measured_conditional_statistics",
                  "independent_paired_noise_draws": trials, "thresholds": [], "points": [],
                  "criterion": "Correct private bit admitted by the complete single coherent branch, fixed impaired waveform",
                  "numerical_root_error_per_variant_db": root_error,
                  "conservative_ieee_numerical_allowance_per_variant_db": numerical_error,
                  "maximum_paired_root_difference_db": max(abs(o-r) for r, o in zip(raw, optimized)),
                  "limits": ["Joint Gaussian template projections and residual chi-square energy represent the same PCM noise realization.",
                             "These conditional single-branch statistics do not independently qualify timing/clock/key/epoch acquisition search.",
                             "Bootstrap resamples the same capture IDs for both variants; it reports differences of quantiles.",
                             "Root bracketing error is explicit; float PCM replay discrepancy is reported separately.",
                             "The analytical AWGN union bound is not an empirical rare-event rate or an interference guarantee."]}
        for name in ("maximum_template_relative_error", "maximum_gram_relative_error", "joint_noise_covariance_error",
                     "awgn_false_accept_union_bound", "actual_pcm_maximum_score_relative_error",
                     "numerical_root_error_bound_db", "actual_pcm_verified_pairs", "noise_span_rank"):
            available = [float(row[name]) for pair in pairs for row in pair.values() if row.get(name)]
            if available:
                output[name] = max(available)
        for target, samples in boot.items():
            r = quantile(raw, target); o = quantile(optimized, target)
            interval = [quantile(samples["difference"], alpha/2), quantile(samples["difference"], 1-alpha/2)]
            # Each root is within its stated bracket. Add both errors, even
            # when observed differences round to zero in every paired draw.
            conservative_upper = interval[1] + 2*(root_error+numerical_error)
            output["thresholds"].append({"detection_probability": target, "raw_cn0_db_hz": r,
                "optimized_cn0_db_hz": o, "additional_required_cn0_db": o-r,
                "paired_bootstrap_difference_interval_db": interval,
                "raw_absolute_bootstrap_interval_db": [quantile(samples["raw"], alpha/2), quantile(samples["raw"], 1-alpha/2)],
                "optimized_absolute_bootstrap_interval_db": [quantile(samples["optimized"], alpha/2), quantile(samples["optimized"], 1-alpha/2)],
                "simultaneous_family_confidence": .95, "individual_interval_confidence": 1-alpha,
                "bootstrap_count": bootstrap_count, "conservative_upper_statistic_loss_db": conservative_upper,
                "numerical_allowance_scope": "Conservative IEEE rounding estimate, checked against actual PCM replays; not an interval-libm proof",
                "statistic_loss_below_requested_limit": conservative_upper < maximum_loss})
        # Fixed half-dB lattice; choosing only the display extent from roots
        # avoids presenting an empirical quantile as a preselected C/N0 point.
        lower = math.floor(2*quantile(raw, .1))
        upper = math.ceil(2*quantile(raw, .995))
        for step in range(lower, upper+1):
            cn0 = step/2
            row = {"cn0_db_hz": cn0}
            for name, roots in (("raw_reference", raw), ("automatic", optimized)):
                successes = sum(root <= cn0 for root in roots)
                row[name] = {"successes": successes, "probability": successes/trials,
                             "probability_95pct_wilson": wilson(successes, trials)}
            output["points"].append(row)
        result.append(output)
    return result


def self_test():
    assert quantile([1, 2, 3], .5) == 2
    assert crossing([0, 1], [.8, 1], .9) == .5
    assert crossing([0, 1], [.95, 1], .9) is None
    assert crossing([0, 1, 2], [.1, .8, .7], .75) is None
    lower, upper = wilson(0, 100)
    assert abs(lower) < 1e-15 and .03 < upper < .04
    assert crossing([0, .1, .2], [.8, .9, 1], .99) > .1
    print("pulse receiver analysis self-check passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", nargs="*", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--bootstrap", type=int, default=2000)
    parser.add_argument("--seed", type=int, default=71891)
    parser.add_argument("--external-equivalence-bound-db", type=float,
                        help="independently validated bound for these exact geometries; cannot be inferred from zero discrepancies")
    parser.add_argument("--maximum-loss-db", type=float, default=.1)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.csv or args.bootstrap < 100 or args.maximum_loss_db <= 0:
        parser.error("provide CSV files, at least100 bootstrap draws and a positive loss limit")
    if args.external_equivalence_bound_db is not None and args.external_equivalence_bound_db < 0:
        parser.error("equivalence bound must be nonnegative")
    rows = []
    for path in args.csv:
        with path.open(newline="") as source:
            rows.extend(csv.DictReader(source))
    paired = pairing(rows)
    output = {"paired_rows": len(paired), "performance": performance(paired),
              "curves": curves(paired, args.bootstrap, args.seed, args.external_equivalence_bound_db, args.maximum_loss_db),
              "statistics_crossings": statistics_crossings(paired, args.bootstrap, args.seed, args.maximum_loss_db)}
    rendered = json.dumps(output, indent=2, allow_nan=False) + "\n"
    if args.output:
        args.output.write_text(rendered)
    else:
        print(rendered, end="")


if __name__ == "__main__":
    main()
