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


VARIANTS = ("raw_reference", "preceding_6330e94", "automatic")
CONTRASTS = {"cumulative": ("raw_reference", "automatic"),
             "incremental": ("preceding_6330e94", "automatic")}


def comparison_mode(pair):
    modes = {row.get("comparison_mode") or "paired" for row in pair.values()}
    if len(modes) != 1:
        raise ValueError("mixed comparison schemas within a capture")
    return modes.pop()


def active_contrasts(pair):
    mode = comparison_mode(pair)
    return dict(CONTRASTS) if mode == "triplet" else {"cumulative":CONTRASTS["cumulative"]} if mode == "paired" else {}


def pairing(rows):
    paired = defaultdict(dict)
    for row in rows:
        identity = tuple(row.get(name, "") for name in CONFIG_FIELDS) + (row["mode"], row["seed"], row["repeat"], row["input_cn0_db_hz"])
        if row.get("comparison_mode") == "single":
            identity += (row["variant"],row.get("implementation_id",""))
        identity += (row["instrumented"],)
        variant = row["variant"]
        if variant not in VARIANTS:
            raise ValueError(f"unknown receiver variant {variant!r}")
        if variant in paired[identity]:
            raise ValueError("duplicate capture row; do not count repeated files as independent evidence")
        paired[identity][variant] = row
    schemas = {}
    for identity, pair in paired.items():
        mode = comparison_mode(pair)
        expected = set(VARIANTS) if mode == "triplet" else {"raw_reference", "automatic"} if mode == "paired" else set(pair) if mode == "single" else None
        if expected is None or set(pair) != expected or mode == "single" and len(pair) != 1:
            raise ValueError("missing receiver variant or unsupported comparison schema")
        if mode == "single" and identity[len(CONFIG_FIELDS)] != "performance":
            raise ValueError("single rows are independent performance evidence only")
        group = identity[:len(CONFIG_FIELDS)] + (identity[len(CONFIG_FIELDS)], identity[-1])
        if mode != "single" and group in schemas and schemas[group] != mode:
            raise ValueError("mixed pair/triplet/single schemas within a configuration")
        if mode != "single":schemas[group] = mode
        reference = next(iter(pair.values()))
        for row in pair.values():
            for name in ("hypotheses", "phase_groups", "drift_sections", "differential_window_samples", "samples", "capture_begin_sample"):
                if reference[name] != row[name]:
                    raise ValueError(f"receiver coverage/input differs: {name}")
            if row["pcm_samples_identical"] != "1":
                raise ValueError("input PCM identity was not asserted by capture producer")
            if mode == "triplet":
                if not row.get("implementation_id"):
                    raise ValueError("triplet requires explicit implementation identity")
                if row["mode"] == "statistics_crossing":
                    names = ("joint_noise_id", "joint_span_sha256")
                    if not row.get("numerical_enclosure_valid") or not row.get("numerical_enclosure_method") or not row.get("numerical_enclosure_scope"):
                        raise ValueError("triplet requires explicit numerical enclosure provenance")
                else:
                    names = ("pcm_sha256", "capture_manifest_sha256", "hypothesis_bank_sha256")
                for name in names:
                    if not row.get(name) or row[name] != reference.get(name) or len(row[name]) != 64 or any(c not in "0123456789abcdef" for c in row[name]):
                        raise ValueError(f"triplet shared input identity differs or is missing: {name}")
    # A receiver variant denotes one fixed implementation within a comparison
    # configuration. Pooling changed sources would change the estimand.
    profiles = defaultdict(set)
    spans = defaultdict(set)
    noise_draws = defaultdict(dict)
    for identity,pair in paired.items():
        if identity[len(CONFIG_FIELDS)] == "statistics_crossing":
            key = identity[:len(CONFIG_FIELDS)]+(identity[len(CONFIG_FIELDS)],identity[-1])
            row = pair["raw_reference"];draw_id = row.get("joint_noise_id") or "seed:"+row["seed"]
            if draw_id in noise_draws[key] and noise_draws[key][draw_id] != identity:
                raise ValueError("identical statistics noise draw repeated under different capture IDs")
            noise_draws[key][draw_id] = identity
        if comparison_mode(pair) != "triplet":continue
        key = identity[:len(CONFIG_FIELDS)]+(identity[len(CONFIG_FIELDS)],identity[-1])
        for name,row in pair.items():profiles[(key,name)].add(row["implementation_id"])
        if identity[len(CONFIG_FIELDS)] == "statistics_crossing":spans[key].add(pair["raw_reference"]["joint_span_sha256"])
    if any(len(values) != 1 for values in profiles.values()) or any(len(values) != 1 for values in spans.values()):
        raise ValueError("triplet configuration mixes source profiles or conditioned joint spans")
    return paired


def sensitivity_family_size(paired):
    groups = {}
    for identity, pair in paired.items():
        if identity[len(CONFIG_FIELDS)] in ("statistics_crossing", "curve") and identity[len(CONFIG_FIELDS)-1] != "1":
            key = identity[:len(CONFIG_FIELDS)] + (identity[len(CONFIG_FIELDS)], identity[-1])
            groups[key] = len(active_contrasts(pair))
    # Every declared configuration remains in the family, even if inconclusive.
    return max(1, 2 * sum(groups.values()))


def performance(paired):
    groups = defaultdict(list)
    for identity, pair in paired.items():
        if identity[len(CONFIG_FIELDS)] == "performance":
            groups[(identity[:len(CONFIG_FIELDS)], identity[-1], (next(iter(pair)),next(iter(pair.values())).get("implementation_id","")) if comparison_mode(pair)=="single" else None)].append(pair)
    result = []
    for (key, instrumented, single_profile), pairs in sorted(groups.items(),key=lambda item:repr(item[0])):
        output = {"configuration": configuration(key), "instrumented": instrumented == "1", "paired_runs": len(pairs), "timing_scope": "independent_single_variant" if single_profile else "paired_same_capture"}
        for variant in (name for name in VARIANTS if name in pairs[0]):
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
        output["comparison_mode"] = comparison_mode(pairs[0])
        output["variant_implementation_ids"] = {name:sorted({pair[name].get("implementation_id","") for pair in pairs}) for name in pairs[0]}
        if active_contrasts(pairs[0]):
            output["contrasts"] = {}
            for name, (left, right) in active_contrasts(pairs[0]).items():
                ratios = [float(pair[left]["wall_seconds"]) / float(pair[right]["wall_seconds"]) for pair in pairs]
                output["contrasts"][name] = {"reference": left, "candidate": right,
                    "paired_wall_speedup_median": statistics.median(ratios), "paired_wall_speedup_range": [min(ratios), max(ratios)]}
            cumulative = output["contrasts"]["cumulative"]
            output["paired_wall_speedup_median"] = cumulative["paired_wall_speedup_median"]
            output["paired_wall_speedup_range"] = cumulative["paired_wall_speedup_range"]
            output["score_maximum_relative_difference"] = max(float(pair["automatic"]["maximum_score_relative_difference"]) for pair in pairs)
        output["limits"] = ["These are repeated fixture timings, not extrapolated full-symbol timings.",
                            "Process peak RSS includes immutable input capture and is a process-lifetime high-water mark.",
                            "Acquisition -1 means the finite measured capture did not complete acquisition coverage."]
        if instrumented == "1":
            output["limits"].append("Component instrumentation is enabled; compare primary total timings with the uninstrumented paired run.")
        result.append(output)
    return result


def curves(paired, bootstrap_count, random_seed, numeric_bound, maximum_loss, family=None):
    groups = defaultdict(dict)
    for identity, pair in paired.items():
        if identity[len(CONFIG_FIELDS)] == "curve":
            key = (identity[:len(CONFIG_FIELDS)], identity[-1])
            seed, repeat, cn0 = identity[len(CONFIG_FIELDS)+1:len(CONFIG_FIELDS)+4]
            groups[key].setdefault((seed,repeat), {})[float(cn0)] = pair
    result = []
    family = sensitivity_family_size(paired) if family is None else family
    alpha = .05/family
    generator = random.Random(random_seed)
    for (key,instrumented), captures in sorted(groups.items()):
        capture_ids = sorted(captures); grid = sorted(next(iter(captures.values())))
        if len(grid) < 2 or any(sorted(captures[identity]) != grid for identity in capture_ids):
            raise ValueError("each independent capture must have the same complete C/N0 grid")
        first = captures[capture_ids[0]][grid[0]]; names = [name for name in VARIANTS if name in first]; contrasts = active_contrasts(first)
        trials = len(capture_ids)
        outcomes = {variant:[[int(captures[identity][point][variant]["correct"]) for point in grid] for identity in capture_ids] for variant in names}
        probabilities = {variant:[sum(row[i] for row in values)/trials for i in range(len(grid))] for variant,values in outcomes.items()}
        output = {"configuration":configuration(key),"instrumented":instrumented=="1","comparison_mode":comparison_mode(first),
                  "independent_paired_captures":trials,"grid_cn0_db_hz":grid,"points":[],
                  "variant_implementation_ids":{name:sorted({captures[identity][point][name].get("implementation_id","") for identity in capture_ids for point in grid}) for name in names},
                  "criterion":"all requested bits exact in the matched bank after complete sampled observation",
                  "false_alarm_reference":"unchanged nominal receiver alpha and finite-search penalties; no measured rare-event rate",
                  "simultaneous_family_size":family,"bootstrap_endpoint_expected_tail_draws":bootstrap_count*alpha/2}
        if bootstrap_count*alpha/2 < 10:
            output["bootstrap_resolution_warning"] = "Fewer than10 bootstrap draws are expected in each adjusted endpoint tail."
        for i,point in enumerate(grid):
            values = {"cn0_db_hz":point,"contrasts":{}}
            for variant in outcomes:
                successes = sum(row[i] for row in outcomes[variant]); rows = [captures[identity][point][variant] for identity in capture_ids]
                values[variant] = {"successes":successes,"probability":successes/trials,"probability_95pct_wilson":wilson(successes,trials),
                    "bit_errors":sum(int(row["bit_errors"]) for row in rows),"accepted_bits":sum(int(row["accepted_bits"]) for row in rows),
                    "wrong_bank_bits":sum(int(row["wrong_bank_bits"]) for row in rows)}
            for name,(left,right) in contrasts.items():
                discordance = sum(outcomes[left][j][i] != outcomes[right][j][i] for j in range(trials))
                values["contrasts"][name] = {"paired_discordances":discordance,"discordance_95pct_wilson":wilson(discordance,trials)}
            values.update(values["contrasts"]["cumulative"]);output["points"].append(values)
        output["maximum_score_relative_difference"] = max(float(captures[identity][point]["automatic"]["maximum_score_relative_difference"]) for identity in capture_ids for point in grid)
        output["per_capture_nonmonotone_success"] = {variant:sum(any(row[i]>row[i+1] for i in range(len(grid)-1)) for row in values) for variant,values in outcomes.items()}
        if key[-1] == "1":
            output["limits"] = ["Noise-only accepts are controls, not a measurement of an extremely rare false-accept rate.","Zero events gives approximately3/n as a95% upper probability bound."]
            result.append(output);continue
        bootstraps = {target:{name:[] for name in names+list(contrasts)} for target in (.9,.99)}
        for _ in range(bootstrap_count):
            selected = [generator.randrange(trials) for _ in range(trials)]
            drawn = {variant:[sum(values[j][i] for j in selected)/trials for i in range(len(grid))] for variant,values in outcomes.items()}
            for target,samples in bootstraps.items():
                values = {name:crossing(grid,drawn[name],target) for name in names}
                # A bootstrap replicate is complete only when every declared variant brackets.
                if all(value is not None for value in values.values()):
                    for name,value in values.items():samples[name].append(value)
                    for name,(left,right) in contrasts.items():samples[name].append(values[right]-values[left])
        grid_width = max(right-left for left,right in zip(grid,grid[1:]));output["thresholds"] = []
        for target,samples in bootstraps.items():
            estimates = {name:crossing(grid,probabilities[name],target) for name in names}
            bounded = len(samples["cumulative"]) >= (1-alpha/2)*bootstrap_count
            absolute = {name:[quantile(samples[name],alpha/2),quantile(samples[name],1-alpha/2)] if bounded else [None,None] for name in names}
            point = {"detection_probability":target,"variant_cn0_db_hz":estimates,"variant_absolute_bootstrap_interval_db":absolute,"contrasts":{},
                "simultaneous_family_confidence":.95,"individual_interval_confidence":1-alpha,"simultaneous_family_size":family,
                "bootstrap_count":bootstrap_count,"bracketed_bootstrap_count":len(samples["cumulative"]),"maximum_grid_spacing_db":grid_width,
                "external_equivalence_bound_db":numeric_bound}
            for name,(left,right) in contrasts.items():
                interval = [quantile(samples[name],alpha/2),quantile(samples[name],1-alpha/2)] if bounded else [None,None]
                # Each unobserved crossing can move within its own sampled bin.
                interpolation = min(2*grid_width,numeric_bound) if numeric_bound is not None else 2*grid_width
                upper = interval[1]+interpolation if interval[1] is not None else None
                point["contrasts"][name] = {"reference":left,"candidate":right,
                    "additional_required_cn0_db":estimates[right]-estimates[left] if estimates[right] is not None and estimates[left] is not None else None,
                    "paired_bootstrap_difference_interval_db":interval,"additional_interpolation_or_external_bound_db":interpolation,
                    "conservative_upper_loss_db":upper,"loss_below_requested_limit":upper is not None and upper<maximum_loss}
            point.update(point["contrasts"]["cumulative"])
            point.update(raw_cn0_db_hz=estimates["raw_reference"],optimized_cn0_db_hz=estimates["automatic"],
                raw_absolute_bootstrap_interval_db=absolute["raw_reference"],optimized_absolute_bootstrap_interval_db=absolute["automatic"])
            if target == .99 and trials < 20000:point["tail_sampling_limit"] = f"{trials} captures give about{trials/100:g} upper-tail failures at99% probability."
            output["thresholds"].append(point)
        output["limits"] = ["Bootstrap resamples common independent capture IDs, retaining entire curves and every declared variant.",
            "Differences are differences of crossings, conditional on the measured geometry; no unmeasured search bank is qualified.",
            "Bonferroni allocation uses the common family of configurations, contrasts and quantiles; bootstrap coverage is approximate."]
        result.append(output)
    return result


def statistics_crossings(paired, bootstrap_count, random_seed, maximum_loss, family=None):
    """Differences of admission-root quantiles, resampling complete capture IDs."""
    groups = defaultdict(list)
    for identity, pair in paired.items():
        if identity[len(CONFIG_FIELDS)] == "statistics_crossing":
            groups[(identity[:len(CONFIG_FIELDS)], identity[-1])].append(pair)
    result = []
    family = sensitivity_family_size(paired) if family is None else family
    alpha = .05 / family
    generator = random.Random(random_seed)
    for (key, instrumented), pairs in sorted(groups.items()):
        contrasts = active_contrasts(pairs[0])
        output = {"configuration": configuration(key), "instrumented": instrumented == "1",
                  "comparison_mode": comparison_mode(pairs[0]), "independent_paired_noise_draws": len(pairs),
                  "variant_implementation_ids": {name:sorted({pair[name].get("implementation_id","") for pair in pairs}) for name in pairs[0]},
                  "simultaneous_family_size": family, "simultaneous_family_confidence": .95,
                  "individual_interval_confidence": 1-alpha,
                  "confidence_method": "paired percentile bootstrap with Bonferroni allocation across all declared configurations, contrasts and quantiles",
                  "bootstrap_endpoint_expected_tail_draws": bootstrap_count*alpha/2}
        if bootstrap_count*alpha/2 < 10:
            output["bootstrap_resolution_warning"] = "Fewer than10 bootstrap draws are expected in each adjusted endpoint tail; increase bootstrap count for stable endpoint estimates."
        invalid = any(row["bracketed"] != "1" or row["monotone"] != "1" or
                      row.get("numerical_enclosure_valid", "1") != "1" for pair in pairs for row in pair.values())
        for pair in pairs:
            for row in pair.values():
                for name in ("root_error_db", "numerical_root_error_bound_db"):
                    value = row.get(name)
                    if value:
                        number = float(value)
                        if not math.isfinite(number) or number < 0:
                            raise ValueError("root and numerical error allowances must be finite and nonnegative")
                    elif comparison_mode(pair) == "triplet" and row.get("numerical_enclosure_valid") == "1":
                        raise ValueError("numerically valid triplet requires explicit error allowances")
        if invalid:
            output.update(status="inconclusive", reason="Unbracketed, nonmonotone or numerically unenclosed roots retained; no captures omitted.")
            result.append(output)
            continue
        pairs.sort(key=lambda pair: float(pair["raw_reference"]["threshold_cn0_db_hz"]))
        names = [name for name in VARIANTS if name in pairs[0]]
        roots = {name: [float(pair[name]["threshold_cn0_db_hz"]) for pair in pairs] for name in names}
        if any(not math.isfinite(root) for values in roots.values() for root in values):
            raise ValueError("qualified root must be finite")
        same_order = {name: all(a <= b for a, b in zip(values, values[1:])) for name, values in roots.items()}
        trials = len(pairs)
        root_errors = {name: max(float(pair[name]["root_error_db"]) for pair in pairs) for name in names}
        numerical_errors = {name: max(float(pair[name].get("numerical_root_error_bound_db") or 0) for pair in pairs) for name in names}
        allowances = {name: max(float(pair[name]["root_error_db"])+float(pair[name].get("numerical_root_error_bound_db") or 0) for pair in pairs) for name in names}
        boot = {target: {name: [] for name in names+list(contrasts)} for target in (.9, .99)}
        for _ in range(bootstrap_count):
            indices = sorted(generator.choices(range(trials), k=trials))
            draws = {name: [roots[name][i] for i in indices] for name in names}
            for name, values in draws.items():
                if not same_order[name]:
                    values.sort()
            for target, samples in boot.items():
                position = target*(trials-1); lo = int(position); hi = min(lo+1, trials-1); fraction = position-lo
                values = {name: draw[lo]+(draw[hi]-draw[lo])*fraction for name, draw in draws.items()}
                for name, value in values.items():
                    samples[name].append(value)
                for name, (left, right) in contrasts.items():
                    samples[name].append(values[right]-values[left])
        output.update(status="measured_conditional_statistics", thresholds=[], points=[],
            criterion="Correct private bit admitted by the complete single coherent branch, fixed impaired waveform",
            numerical_root_error_per_variant_db=max(root_errors.values()),
            conservative_ieee_numerical_allowance_per_variant_db=max(numerical_errors.values()),
            variant_error_allowances_db=allowances,
            maximum_paired_root_difference_db=max(abs(o-r) for r,o in zip(roots["raw_reference"],roots["automatic"])),
            limits=["Joint Gaussian projections and residual chi-square energy represent one shared PCM noise realization.",
                    "These conditional single-branch statistics do not qualify an unmeasured timing/clock/key/epoch search bank.",
                    "Every declared pair or triplet is retained; bootstrap resamples common capture IDs and reports differences of quantiles.",
                    "Numerical allowances are conservative IEEE estimates checked by selected actual PCM replays, not interval-libm proofs.",
                    "The analytical AWGN union bound is not an empirical rare-event rate or an interference guarantee.",
                    "Bonferroni allocation controls multiplicity conditional on the bootstrap interval approximation; it is not an exact finite-sample coverage theorem."])
        for name in ("maximum_template_relative_error", "maximum_gram_relative_error", "joint_noise_covariance_error",
                     "awgn_false_accept_union_bound", "actual_pcm_maximum_score_relative_error", "numerical_root_error_bound_db",
                     "actual_pcm_verified_pairs", "actual_pcm_verified_triplets", "noise_span_rank"):
            available = [float(row[name]) for pair in pairs for row in pair.values() if row.get(name)]
            if available:
                output[name] = max(available)
        for target, samples in boot.items():
            estimates = {name: quantile(values, target) for name, values in roots.items()}
            point = {"detection_probability": target, "variant_cn0_db_hz": estimates, "contrasts": {},
                     "variant_absolute_bootstrap_interval_db": {name: [quantile(samples[name],alpha/2),quantile(samples[name],1-alpha/2)] for name in names},
                     "simultaneous_family_confidence": .95, "individual_interval_confidence": 1-alpha,
                     "simultaneous_family_size": family, "bootstrap_count": bootstrap_count,
                     "numerical_allowance_scope": "Conservative IEEE estimate with actual PCM sanity checks; not interval-libm proof"}
            for name, (left, right) in contrasts.items():
                interval = [quantile(samples[name],alpha/2),quantile(samples[name],1-alpha/2)]
                allowance = allowances[left]+allowances[right]
                inflated = [interval[0]-allowance,interval[1]+allowance]
                point["contrasts"][name] = {"reference": left, "candidate": right,
                    "additional_required_cn0_db": estimates[right]-estimates[left],
                    "paired_bootstrap_difference_interval_db": interval,
                    "additional_numerical_allowance_db": allowance,
                    "numerically_inflated_difference_interval_db": inflated,
                    "conservative_upper_statistic_loss_db": inflated[1],
                    "statistic_loss_below_requested_limit": inflated[1] < maximum_loss}
            # Preserve existing cumulative fields for paired consumers.
            cumulative = point["contrasts"]["cumulative"]
            point.update(cumulative)
            point.update(raw_cn0_db_hz=estimates["raw_reference"],optimized_cn0_db_hz=estimates["automatic"],
                raw_absolute_bootstrap_interval_db=point["variant_absolute_bootstrap_interval_db"]["raw_reference"],
                optimized_absolute_bootstrap_interval_db=point["variant_absolute_bootstrap_interval_db"]["automatic"])
            output["thresholds"].append(point)
        lower = math.floor(2*quantile(roots["raw_reference"],.1)); upper = math.ceil(2*quantile(roots["raw_reference"],.995))
        for step in range(lower,upper+1):
            row = {"cn0_db_hz": step/2}
            for name, values in roots.items():
                successes = sum(root <= step/2 for root in values)
                row[name] = {"successes": successes,"probability":successes/trials,"probability_95pct_wilson":wilson(successes,trials)}
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
    def fixture(case="triplet", mode="triplet", reverse=False):
        rows = []
        for trial in range(20):
            for variant in VARIANTS if mode == "triplet" else ("raw_reference","automatic"):
                row = {name:"0" for name in CONFIG_FIELDS}
                row.update(case=case,mode="statistics_crossing",comparison_mode=mode,variant=variant,seed=str(trial),repeat=str(trial),
                    input_cn0_db_hz="",instrumented="0",hypotheses="1",phase_groups="1",drift_sections="1",differential_window_samples="0",
                    samples="200",capture_begin_sample="0",pcm_samples_identical="1",implementation_id="source-"+variant,
                    joint_noise_id=f"{trial+1:064x}",joint_span_sha256="a"*64,bracketed="1",monotone="1",
                    numerical_enclosure_valid="1",numerical_enclosure_method="ieee_roundoff_estimate",numerical_enclosure_scope="entire_lower_bracket",
                    numerical_root_error_bound_db="0",actual_pcm_verified_triplets="2",actual_pcm_verified_seed="1")
                row["root_error_db"] = {"raw_reference":".03","preceding_6330e94":".09","automatic":".13"}[variant]
                value = trial if variant == "raw_reference" else trial+1 if variant == "preceding_6330e94" else (19-trial if reverse else trial)-.25
                row["threshold_cn0_db_hz"] = str(value); rows.append(row)
        return rows
    def rejected(rows):
        try:pairing(rows)
        except ValueError:return
        raise AssertionError("invalid capture collection was accepted")
    rows = fixture(); random.Random(41).shuffle(rows); complete = pairing(rows)
    assert len(complete) == 20
    for variant in VARIANTS:
        rejected([row for row in rows if not (row["variant"] == variant and row["repeat"] == "0")])
    rejected(rows+[rows[0]])
    cloned = [dict(row) for row in rows if row["repeat"] == "0"]
    for row in cloned:row["repeat"] = "100"
    rejected(rows+cloned)
    for field,value in (("variant","unknown"),("pcm_samples_identical","0"),("samples","201"),("joint_noise_id","another-draw")):
        bad = [dict(row) for row in rows];bad[0][field] = value;rejected(bad)
    mixed = fixture(case="triplet",mode="paired")
    for row in mixed:row["seed"] = row["repeat"] = str(int(row["repeat"])+100)
    rejected(fixture()+mixed)
    changed = [dict(row) for row in rows];changed[0]["implementation_id"] = "changed-source";rejected(changed)
    changed = [dict(row) for row in rows]
    for row in changed:
        if row["repeat"] == "0":row["joint_span_sha256"] = "b"*64
    rejected(changed)
    singles = []
    for variant in VARIANTS:
        row = dict(rows[0]);row.update(mode="performance",comparison_mode="single",variant=variant,implementation_id="source-"+variant)
        singles.append(row)
    assert len(pairing(singles)) == 3
    for row in singles:
        row.update({name:"1" for name in ("wall_seconds","cpu_seconds","construction_seconds","frontend_seconds","search_seconds","kernel_seconds",
            "frontend_cpu_seconds","search_cpu_seconds","kernel_cpu_seconds","media_seconds","max_push_seconds","acquisition_wall_seconds",
            "acquisition_media_seconds","progress_latency_seconds","progress_latency_wall_seconds","peak_workspace_bytes","process_peak_rss_bytes",
            "progress_latency_samples","lattices","cells","segments")})
        row["backend"] = "raw"
    instrumented_single = dict(singles[0]);instrumented_single["instrumented"] = "1";singles.append(instrumented_single)
    reports = performance(pairing(singles))
    assert len(reports) == 4 and all(report["comparison_mode"] == "single" and "contrasts" not in report for report in reports)
    assert {report["instrumented"] for report in reports if "raw_reference" in report} == {False,True}
    legacy = pairing(fixture(case="legacy",mode="paired"))
    legacy_output = statistics_crossings(legacy,100,17,.1)[0]
    assert legacy_output["comparison_mode"] == "paired" and legacy_output["simultaneous_family_size"] == 2
    assert all(abs(point["additional_required_cn0_db"]+.25)<1e-12 for point in legacy_output["thresholds"])
    output = statistics_crossings(complete,100,17,.1)[0]
    for point in output["thresholds"]:
        for name,estimate,allowance in (("incremental",-1.25,.22),("cumulative",-.25,.16)):
            contrast = point["contrasts"][name]
            assert abs(contrast["additional_required_cn0_db"]-estimate)<1e-12
            assert all(abs(value-estimate)<1e-12 for value in contrast["paired_bootstrap_difference_interval_db"])
            assert abs(contrast["additional_numerical_allowance_db"]-allowance)<1e-12
            assert abs(contrast["numerically_inflated_difference_interval_db"][0]-(estimate-allowance))<1e-12
            assert abs(contrast["numerically_inflated_difference_interval_db"][1]-(estimate+allowance))<1e-12
    reverse = statistics_crossings(pairing(fixture(reverse=True)),100,17,.1)[0]
    assert all(abs(point["additional_required_cn0_db"]+.25)<1e-12 for point in reverse["thresholds"])
    oracle = {.9:[],.99:[]};generator = random.Random(17)
    for _ in range(100):
        selected = generator.choices(range(20),k=20)
        for target,values in oracle.items():
            values.append(quantile([19-i-.25 for i in selected],target)-quantile(selected,target))
    for point in reverse["thresholds"]:
        values = oracle[point["detection_probability"]]
        expected = [quantile(values,.05/4/2),quantile(values,1-.05/4/2)]
        assert all(abs(a-b)<1e-12 for a,b in zip(point["paired_bootstrap_difference_interval_db"],expected))
    for bad_error in ("-1","nan","inf"):
        invalid = fixture();invalid[0]["root_error_db"] = bad_error
        try:statistics_crossings(pairing(invalid),100,17,.1)
        except ValueError:pass
        else:raise AssertionError("invalid numerical allowance accepted")
    combined_rows = fixture(case="a")+fixture(case="b")+fixture(case="c",mode="paired")
    for row in combined_rows:
        if row["case"] == "b" and row["repeat"] == "0":row["bracketed"] = "0"
    combined = pairing(combined_rows);assert sensitivity_family_size(combined) == 10
    reports = statistics_crossings(combined,100,17,.1)
    assert all(report["simultaneous_family_size"] == 10 for report in reports)
    assert sum(report["status"] == "inconclusive" for report in reports) == 1
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
    family = sensitivity_family_size(paired)
    output = {"paired_rows": len(paired), "performance": performance(paired),
              "curves": curves(paired, args.bootstrap, args.seed, args.external_equivalence_bound_db, args.maximum_loss_db, family),
              "statistics_crossings": statistics_crossings(paired, args.bootstrap, args.seed, args.maximum_loss_db, family), "simultaneous_sensitivity_family_size": family}
    rendered = json.dumps(output, indent=2, allow_nan=False) + "\n"
    if args.output:
        args.output.write_text(rendered)
    else:
        print(rendered, end="")


if __name__ == "__main__":
    main()
