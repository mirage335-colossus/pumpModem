#!/usr/bin/env python3
"""Forecast local native Robust receiver costs from calibrated GPU work demand."""
from __future__ import annotations

import argparse
import math
from pathlib import Path

from triad_case_capacity import direct_fine, fine_continuation, slot_costs
from triad_case_metrics import ALPHA, LOOKS, arithmetic_interval
from triad_cases import DEFAULT_CASES, load_cases
from triad_gpu_workload import (ACQUISITION_STRATA, ADAPTIVE_COST_RTOL, CACHE_GEOMETRY, CHALLENGES,
                                COARSE_GEOMETRY, DIAGNOSTIC_STRATA,
                                FINE_GEOMETRY, LANE_CAPS, STRATA, SCHEMA as WORKLOAD,
                                compare_work, cuda_runtime, public_configuration, reference_challenge, require, row_matches,
                                same_fields, select_cases, validate_work)
from triad_native import (DEFAULT_PROBE, NativeProbe, ProbeError, allocation, environment, is_sha256,
                          named_phases, phase_once, read_report, sha256_file, write_result)

SCHEMA = "datapump.triad.native-gpu-cpu-forecast.v1"
CAPACITY = "datapump.triad.native-capacity.v1"
SCOPE = "configured_public_calendar_and_keyed_ordinal"
PHASES = ("frontend", "helper", "coarse_prepare", "coarse_search", "coarse_selection", "fine")
HOLDOUT = {"nominal": (.15, .10), "conservative": (.20, .15)}
ALLOCATION_KEYS = ("workers", "slot_owners", "memory_limit_bytes", "receiver_numerical_threads_per_worker")


def source_sha(receipt):
    identity = receipt["identity"]
    value = identity.get("source_manifest_sha256")
    require(is_sha256(value), "Missing compiled native source manifest SHA-256")
    return value


def build_identity(receipt):
    identity = {key: value for key, value in receipt["identity"].items() if key != "backend_receipt"}
    source_sha(receipt)
    require(identity.get("implementation") == "native-production-receiver" and
            is_sha256(identity.get("executable_sha256")) and is_sha256(identity.get("build_options_sha256"))
            and identity.get("compiler") and identity.get("build_config"),
            "Missing native executable hash or compiler identity")
    return identity


def validate_native(receipt, description, *, same_build=True, same_hardware=True, match_allocation=True):
    require(receipt.get("schema") == description.get("schema") == "datapump.triad.native-probe.v1",
            "Expected current native receiver receipts")
    require(source_sha(receipt) == source_sha(description), "Native receiver source changed")
    if same_build:
        require(build_identity(receipt) == build_identity(description), "Native receiver build changed")
    fields = (ALLOCATION_KEYS if match_allocation else ()) + (("hardware_threads",) if same_hardware else ())
    require(same_fields(allocation(receipt), allocation(description), fields),
            "Native receiver allocation differs from the calibrated allocation")


def _row_map(report, parsed):
    source = report.get("input_sha256", report.get("input", {}).get("source_sha256"))
    require(source == parsed["source_sha256"], "Manual case file changed since the measurement")
    rows = {}
    cases = {case["index"]: case for case in parsed["cases"]}
    for row in report["cases"]:
        index = row["case"]["index"]
        require(index in cases and index not in rows and row_matches(row["case"], cases[index]),
                "Native measurement rows differ from the current ordered manual cases")
        rows[index] = row
    indices = report.get("selected_case_indices", report.get("selection", {}).get("indices"))
    require(list(rows) == indices == sorted(rows), "Native receipt coverage or row order changed")
    return rows


def validate_workload(report, parsed, backend, *, allow_partial_status=False):
    require(report.get("schema") == WORKLOAD and report.get("device") == backend,
            f"Expected current native {backend} workload, with no legacy calibration")
    require(report.get("identity_verified_at_completion") is True,
            "Native workload ended without verifying its build, host and input identity")
    rows = _row_map(report, parsed)
    require(allow_partial_status or report.get("status") == "bounded_native_work_observed",
            "Native workload has unresolved observations")
    description = report["description"]
    require(description["status"] == "complete" and description["identity"]["backend"] == backend,
            f"No current native {backend} runtime")
    build_identity(description)
    return rows


def _runtime_for(receipt):
    return cuda_runtime(receipt["identity"].get("backend_receipt"))


def reference_classes(report, rows, requested):
    declared = report["reference_case_indices"]
    require(len(declared) == len(set(declared)) <= 3 and set(declared) <= rows.keys(),
            "Invalid paired reference coverage")
    if requested:
        require(len(requested) == len(set(requested)) and set(requested) <= set(declared),
                "Requested CUDA reference rows were not predeclared")
    classes = set()
    for index in requested or declared:
        row = rows[index]
        expected = set(STRATA + CHALLENGES)
        require(set(row["references"]) == expected, f"CUDA row {index} lacks paired precision strata")
        precisions = set()
        for name in STRATA + CHALLENGES:
            entry = row["references"][name]
            gpu = entry["cuda"] if name in CHALLENGES else row["strata"][name]
            cpu = entry["cpu"]
            validate_native(gpu, report["description"])
            require(cpu["identity"]["backend"] == "cpu" and
                    source_sha(cpu) == source_sha(gpu) and
                    cpu["identity"]["executable_sha256"] == gpu["identity"]["executable_sha256"],
                    "CUDA reference must compare the same native executable and source")
            result = (reference_challenge(cpu, gpu, name) if name in CHALLENGES
                      else compare_work(cpu, gpu, name, command=gpu["command"],
                                        declared=name in DIAGNOSTIC_STRATA))
            require(result["accepted"] and result["cuda_runtime"] == _runtime_for(report["description"]),
                    f"CUDA paired precision/work evidence failed for row {index} {name}")
            precisions.add(tuple(result["precision"]))
        require(any(coarse is not None for coarse, _ in precisions),
                f"CUDA row {index} lacks full public precision qualification")
        classes.update(precisions)
    require(classes, "At least one qualified paired CUDA reference row is required")
    return classes


def _counts(data, keys):
    values = tuple(data[key] for key in keys)
    require(all(type(x) is int and x >= 0 for x in values), "Malformed native useful-work counters")
    return values


def _wall(receipt, name):
    wall = phase_once(receipt, name)["wall_seconds"]
    require(type(wall) in (int, float) and math.isfinite(wall) and wall > 0,
            f"Invalid native {name} elapsed time")
    return wall


def _select(data, keys):
    return tuple(data[key] for key in keys)


def _fine_cost(receipt, stratum, window):
    if receipt["command"] == "continuation" and stratum in ("active", "noise"):
        model = fine_continuation(receipt)
        one, two, check = model["continuation_rates"]
        fit = (one + 2 * two) / 3
        require(1 / 1.30 <= fit / check <= 1.30,
                "Native two/four-second continuation cost holdout failed")
        rate = fit if stratum == "active" else max(fit, check)
        return model["prefix_seconds"] + rate * max(0., window - 2) + model["finish_seconds"]
    elapsed, work = direct_fine(receipt, stratum)
    if stratum in DIAGNOSTIC_STRATA:
        # Repeat the measured upper bound for this fine stratum at each segment.
        segment = work["segment_rows"] / work["sample_rate_hz"]
        require(segment > 0, "Invalid native fine segment geometry")
        elapsed *= math.ceil(window / segment)
    return elapsed


def observation(receipt, stratum, *, measure_timing=True):
    config = receipt["configuration"]
    require(config.get("scope") == SCOPE, "Native receipt lacks the configured-calendar scope")
    horizon = config["recurring_horizon"]
    require(horizon.get("complete_declared_horizon") is True, "Native recurring calendar horizon is incomplete")
    interval = horizon["shortest_interval_seconds"]
    window = config["public_window_seconds"]
    require(interval > 0 and window > 0, "Native public slot has invalid duration")
    result = dict(config=public_configuration(config), seed=receipt["observation"]["seed"],
                  input_sha256=receipt["observation"]["input_sha256"], phases={})
    fine = phase_once(receipt, "fine_finish")["work"]
    fine_geometry = (_select(fine, FINE_GEOMETRY) + _select(fine["cache"], CACHE_GEOMETRY)
                     + _select(fine["initial_affine"], LANE_CAPS) + _select(fine["adaptive"], LANE_CAPS))
    timed = {
        "fine": (_fine_cost(receipt, stratum, window) if measure_timing else 0., fine_geometry,
                 _counts(fine["initial_affine"], ("basis_rows", "gaussian_updates"))
                 + _counts(fine["adaptive"], ("basis_rows", "transition_edges", "gaussian_updates"))
                 + _counts(fine["cache"], ("cache_misses", "native_preparation_samples", "evictions"))),
    }
    if stratum in ACQUISITION_STRATA:
        costs = slot_costs(receipt)
        frontend = phase_once(receipt, "audio_frontend")["work"]
        input_check = phase_once(receipt, "input_validation")["work"]
        helper = phase_once(receipt, "helper")["work"]
        prepare = phase_once(receipt, "coarse_prepare")["work"]
        coarse = phase_once(receipt, "coarse_selection")["work"]
        selection = coarse["selection"]
        grid = _select(coarse, COARSE_GEOMETRY)
        # Express continuous PCM projection as a cost for the whole cadence.
        # Input validation is still charged once per scored window.
        audio_per_slot = interval * (costs["audio_frontend"] / costs["audio_frontend_output_seconds"])
        timed.update({
            "frontend": (audio_per_slot + costs["input_validation"],
                         (_select(frontend, ("input_rows", "output_rows", "halo_samples")),
                          tuple(sorted(input_check))), _counts(frontend, ("input_rows", "output_rows"))),
            "helper": (costs["helper"], _select(helper, ("reason", "narrowed")),
                       _counts(helper, ("prefix_samples", "onset_apertures", "retained_intervals"))),
            "coarse_prepare": (costs["coarse_prepare"], grid,
                               _counts(prepare["template_bank"], ("source_render_rows", "projected_columns"))
                               + _counts(prepare["observation"], ("fir_input_rows",))),
            "coarse_search": (costs["coarse_tiles"], grid,
                              _counts(coarse["search"], ("workspace_frequency_pairs", "score_rows"))),
            "coarse_selection": (costs["coarse_selection"], grid,
                                 _counts(selection, ("retained_peaks", "selection_peak_rows"))
                                 + _counts(coarse, ("proposals", "refined_coordinates"))),
        })
    for name, (wall, geometry, demand) in timed.items():
        result["phases"][name] = dict(service=wall / interval if measure_timing else None,
                                      geometry=geometry, demand=demand)
    return result


def demand_scale(calibration, target):
    require(calibration["geometry"] == target["geometry"],
            "Native phase geometry changed across work observations")
    a, b = calibration["demand"], target["demand"]
    require(len(a) == len(b) and all((x == 0) == (y == 0) for x, y in zip(a, b)),
            "Required phase work appeared or disappeared")
    ratios = [y / x for x, y in zip(a, b) if x]
    lower, upper = min(ratios + [1.]), max(ratios + [1.])
    require(lower >= 1 - ADAPTIVE_COST_RTOL and upper <= 1 + ADAPTIVE_COST_RTOL,
            "Native useful work is outside the calibrated cost regime")
    # Wall time is attributable to a mixture of these work and fixed costs.
    # Retain their complete range because a unique phase weighting is unknown.
    return lower, upper


def _matching_config(a, b):
    return a["config"] == b["config"]


def _fit_stratum(local, gpu, holdout, *, alpha):
    require(local and all(_matching_config(row, gpu) and _matching_config(row, holdout) for row in local),
            "Local/GPU public profiles or selected effects differ")
    require(all(row["seed"] != holdout["seed"] and row["input_sha256"] != holdout["input_sha256"]
                for row in [*local, gpu]), "Fresh local holdout must use an independent seed and observation")
    require(len(local) <= max(LOOKS), "CPU calibration exceeds the predeclared sequential looks")
    measured_phases = gpu["phases"].keys()
    require(all(row["phases"].keys() == measured_phases for row in [*local, holdout]),
            "Native measured phases differ from the declared workload")
    modeled = {}
    for name in measured_phases:
        points, lowers, uppers = [], [], []
        observed = holdout["phases"][name]
        remote = gpu["phases"][name]
        for sample in local:
            calibrated = sample["phases"][name]
            lo, hi = demand_scale(calibrated, remote)
            demand_scale(calibrated, observed)
            seconds = calibrated["service"]
            require(math.isfinite(seconds) and seconds > 0, "Nonpositive CPU phase timing")
            points.append(seconds * math.sqrt(lo * hi))
            lowers.append(seconds * lo)
            uppers.append(seconds * hi)
        estimate = sum(points) / len(points)
        interval = None
        if len(local) in LOOKS:
            _, lower = arithmetic_interval(lowers, alpha=alpha / 2)
            _, upper = arithmetic_interval(uppers, alpha=alpha / 2)
            interval = [lower[0], upper[1]]
        modeled[name] = dict(point=estimate, interval=interval, holdout=observed["service"],
                             local_observations=len(local),
                             gpu_useful_work=list(remote["demand"]))
    return modeled


def _compose(groups, limit):
    overall, per_stage = limit
    estimates = [max(row["point"] for row in group) for group in groups]
    observed = sum(max(row["holdout"] for row in group) for group in groups)
    point = sum(estimates)
    errors = [max(abs(row["point"] - row["holdout"]) for row in group) / observed for group in groups]
    interval = None
    if all(row["interval"] is not None for group in groups for row in group):
        low = sum(max(row["interval"][0] for row in group) for group in groups)
        high = sum(max(row["interval"][1] for row in group) for group in groups)
        interval = [low / (1 + overall), high / (1 - overall)]
    holdout_passed = (interval is not None and interval[0] <= observed <= interval[1]
                      and sum(errors) <= overall and max(errors) <= per_stage)
    return dict(seconds_per_baseband_second=point, interval=interval,
                margin=1 - point, margin_interval=[1 - interval[1], 1 - interval[0]] if interval else None,
                point_failure=point >= 1,
                capacity_status="fail" if point >= 1 else "unresolved",
                local_holdout=dict(accepted=holdout_passed, observed_s_per_s=observed,
                                   signed_relative_error=point / observed - 1,
                                   stage_absolute_error_shares=errors,
                                   combined_uncancelled_error=sum(errors),
                                   combined_limit=overall, stage_limit=per_stage))


def _qualify_backlog(receipts, description):
    rows = []
    for receipt in receipts:
        if receipt["status"] != "complete":
            continue
        validate_native(receipt, description)
        result = receipt["result"]
        interval = result.get("backlog_interval", {})
        if (receipt["command"] == "backlog" and result.get("complete_receiver_service") is True
                and result.get("configured_calendar_reacquired") is True and interval.get("complete") is True):
            if not is_sha256(receipt["observation"].get("input_sha256")):
                continue
            peak = result["stream"]["peak_reserved_bytes"]
            require(type(peak) is int and 0 < peak <= allocation(receipt)["memory_limit_bytes"],
                    "Native same-stream peak exceeded its allocated workspace")
            rows.append(receipt)
    return rows


def _fixed_controller(receipt):
    phase = phase_once(receipt, "controller_service")
    require(phase["scope"] == "controller_wall_excluding_instrumented_cv_wait_and_spool_io" and
            phase["work"].get("may_overlap_slot_dsp") is True and phase["represented_seconds"] > 0,
            "Native controller cost is unavailable")
    return _wall(receipt, "controller_service") / phase["represented_seconds"]


def _fixed_codec(receipt, description):
    validate_native(receipt, description)
    result = receipt["result"]
    require(receipt["command"] == "codec" and receipt["status"] == "complete" and
            result.get("scope") == "isolated_native_long_codec_receipts" and
            result.get("decoded_after_two_quiet") is True and result["marker_intervals"] > 1,
            "Native multi-interval codec cost is unavailable")
    cadence = receipt["configuration"]["recurring_horizon"]["shortest_interval_seconds"]
    return _wall(receipt, "codec_receive") / ((result["wire_bits"] + 2) * cadence)


def _fixed_component(samples, observed, *, alpha):
    require(len(samples) in LOOKS, "Fixed cost lacks a complete declared local timing look")
    point, interval = arithmetic_interval(samples, alpha=alpha)
    return dict(point=point, interval=interval, holdout=observed, local_observations=len(samples), gpu_useful_work=[])


def _backlog_summary(receipt):
    result = receipt["result"]
    stream = result["stream"]
    return dict(result["backlog_interval"], effective_allocation=allocation(receipt),
                stream_peak_reserved_bytes=stream["peak_reserved_bytes"],
                whole_stream_mean_busy_hardware_fraction=stream.get("mean_busy_hardware_fraction"))


def _fixed_costs(cpu, holdout, description, local, held, *, alpha):
    require(len(local) in LOOKS and len(held) == 1,
            "Required same-stream calibration or fresh ready-input backlog interval is missing")
    fresh = held[0]["observation"]
    require(all(fresh["seed"] != sample["observation"]["seed"] and
                fresh["input_sha256"] != sample["observation"]["input_sha256"] and
                public_configuration(sample["configuration"]) == public_configuration(held[0]["configuration"]) and
                sample["result"]["stream"]["input_rows"] == held[0]["result"]["stream"]["input_rows"]
                for sample in local), "Backlog holdout changed geometry or reused the calibration observation")
    controllers = _fixed_component([_fixed_controller(r) for r in local], _fixed_controller(held[0]), alpha=alpha)
    long = cpu["measurements"]["codec"]
    fresh_long = holdout.get("codec", [])
    require(len(fresh_long) == 1 and all(same_fields(r["result"], fresh_long[0]["result"],
            ("wire_bits", "marker_intervals", "source_bytes", "fec")) for r in long),
            "Matched native long-message codec measurement is missing")
    codec = _fixed_component([_fixed_codec(r, description) for r in long],
                             _fixed_codec(fresh_long[0], description), alpha=alpha)
    return {"controller_serial_charge": controllers, "isolated_long_codec_charge": codec}


def _fit_row(cpu, gpu, holdout, cpu_description, gpu_description, precision_classes, *, alpha, message_gate):
    validate_native(cpu["plan"], cpu_description, match_allocation=False)
    validate_native(holdout["plan"], cpu["plan"])
    cpu_description = cpu["plan"]
    fits = {}
    missing = []
    for stratum in STRATA:
        try:
            declared = stratum in DIAGNOSTIC_STRATA
            remote = gpu["strata"][stratum]
            held = holdout["strata"][stratum]
            validate_native(remote, gpu_description)
            validate_native(remote, cpu_description, same_build=False, same_hardware=False)
            validate_native(held, cpu_description)
            require(remote["command"] == held["command"], "Holdout changed the native probe mode")
            precision, runtime = validate_work(remote, "cuda", stratum, command=remote["command"], declared=declared)
            require(precision in precision_classes and runtime == _runtime_for(gpu_description),
                    "CUDA work lacks an applicable paired precision class")
            validate_work(held, "cpu", stratum, command=held["command"], declared=declared)
            local = cpu["measurements"]["work_only" if declared else "strata"][stratum]
            for measured in local:
                validate_native(measured, cpu_description)
                validate_work(measured, "cpu", stratum, command=remote["command"], declared=declared)
            fits[stratum] = _fit_stratum([observation(r, stratum) for r in local],
                                        observation(remote, stratum, measure_timing=False),
                                        observation(held, stratum), alpha=alpha)
        except (ProbeError, KeyError, TypeError, ValueError) as error:
            missing.append(f"{stratum}: {error}")
    local_backlog = _qualify_backlog(cpu["measurements"]["backlog"], cpu_description)
    held_backlog = _qualify_backlog(holdout.get("backlog", []), cpu_description)
    fixed = {}
    try:
        fixed = _fixed_costs(cpu, holdout, cpu_description, local_backlog, held_backlog, alpha=alpha)
    except (ProbeError, KeyError) as error:
        missing.append(str(error))
    if not message_gate:
        missing.append("Native message, physical quiet and EOF correctness gate")
    fixed_groups = [[row] for row in fixed.values()]
    nominal = None
    conservative = None
    if "active" in fits:
        nominal = _compose([[fits["active"][phase]] for phase in PHASES] + fixed_groups, HOLDOUT["nominal"])
    if all(name in fits for name in STRATA):
        grouped = [[fits[stratum][phase] for stratum in ACQUISITION_STRATA] for phase in PHASES if phase != "fine"]
        grouped.append([fits[stratum]["fine"] for stratum in STRATA])
        conservative = _compose(grouped + fixed_groups, HOLDOUT["conservative"])
    phase_gates = (nominal is not None and conservative is not None
                   and nominal["local_holdout"]["accepted"] and conservative["local_holdout"]["accepted"])
    if not phase_gates:
        missing.append("Independent native CPU holdout accuracy or local timing uncertainty")
    backlog_rate = [row["result"]["backlog_interval"]["service_seconds_per_baseband_second"]
                    for row in local_backlog + held_backlog]
    require(all(type(rate) in (int, float) and math.isfinite(rate) and rate > 0 for rate in backlog_rate),
            "Native backlog service requires positive measured time and represented input")
    backlog_failed = any(rate >= 1 for rate in backlog_rate)
    for service in (nominal, conservative):
        if service is not None:
            service["complete_work_covered"] = not missing
            if not missing and service["interval"][1] < 1 and not backlog_failed:
                service["capacity_status"] = "conditional_pass"
    # The controller wall excluding CV wait may overlap DSP. Charging it in
    # full gives the serialized cost model no credit for that overlap.
    return dict(status="conditional_phase_estimates" if phase_gates else "unresolved",
                nominal_service=nominal, conservative_service=conservative, phase_costs=fits,
                fixed_costs=fixed, local_allocation=allocation(cpu_description),
                same_stream_backlog=dict(calibration=[_backlog_summary(r) for r in local_backlog],
                                         fresh_holdout=[_backlog_summary(r) for r in held_backlog],
                                         numeric_point_failure=backlog_failed),
                complete_receiver_capacity=("fail" if backlog_failed else
                                            conservative["capacity_status"] if conservative else "unresolved"),
                unresolved_work=missing,
                outside_scope=["Hardware-rate resampling where required", "GUI presentation",
                               "Unconfigured waveform epoch, ordinal and timing acquisition"])


def _message_gate_passed(cpu, description):
    gate = cpu.get("message_gate", {})
    if gate.get("passed") is not True:
        return False
    receipt = gate["receipt"]
    validate_native(receipt, description, match_allocation=False)
    result = receipt["result"]
    return (receipt["command"] == "message" and receipt["status"] == "complete" and
            result.get("complete_receiver_service") is True and
            result.get("message_and_physical_quiet_verified") is True and result.get("eof_control_verified") is True)


def estimate(cpu, gpu, parsed, description, *, holdout, holdout_indices=(), reference_indices=(),
             gpu_reference=None, case_indices=()):
    require(cpu.get("schema") == CAPACITY,
            "Fresh native CPU calibration is required. Python receiver calibration is obsolete")
    require(cpu.get("identity_verified_at_completion") is True,
            "Native CPU calibration did not finish with verified inputs and build identity")
    require(cpu.get("environment") == environment("cpu"),
            "CPU calibration must match this machine and its effective allocation")
    require(cpu["description"]["status"] == "complete" and description["status"] == "complete",
            "Native local CPU receiver is unavailable")
    validate_native(cpu["description"], description)
    cpu_rows = _row_map(cpu, parsed)
    gpu_rows = validate_workload(gpu, parsed, "cuda", allow_partial_status=True)
    reference = gpu_reference or gpu
    reference_rows = validate_workload(reference, parsed, "cuda", allow_partial_status=True)
    validate_native(reference["description"], gpu["description"])
    require(_runtime_for(reference["description"]) == _runtime_for(gpu["description"]),
            "CUDA reference device, library, runtime or workspace changed")
    precisions = reference_classes(reference, reference_rows, reference_indices)
    local_rows = validate_workload(holdout, parsed, "cpu", allow_partial_status=True)
    validate_native(holdout["description"], description, match_allocation=False)
    require(holdout["host"] == environment("cpu"), "Holdout was measured on a different CPU runtime")
    selected = select_cases(parsed, case_indices)
    indices = [case["index"] for case in selected]
    declared = list(holdout_indices) or list(local_rows)
    require(len(declared) == len(set(declared)) and set(declared) == set(local_rows) <= set(indices),
            "Local holdout coverage must match its predeclared selected indices")
    rows = []
    alpha = ALPHA / (len(parsed["cases"]) * (len(STRATA) * len(PHASES) + 2) * len(LOOKS))
    message_gate = _message_gate_passed(cpu, description)
    for case in selected:
        index = case["index"]
        row = dict(case=case, status="unresolved", nominal_service=None, conservative_service=None,
                   complete_receiver_capacity="unresolved")
        try:
            require(index in cpu_rows and index in gpu_rows and index in local_rows,
                    "Missing native CPU calibration, CUDA workload or independent CPU holdout for this row")
            row.update(_fit_row(cpu_rows[index], gpu_rows[index], local_rows[index], description,
                                gpu["description"], precisions, alpha=alpha, message_gate=message_gate))
        except (ProbeError, KeyError, TypeError, ValueError) as error:
            row["reason"] = str(error)
        rows.append(row)
    status = ("conditional_phase_estimates" if all(row["status"] == "conditional_phase_estimates" for row in rows)
              else "unresolved")
    return dict(schema=SCHEMA, status=status, capacity_scope=SCOPE, input_sha256=parsed["source_sha256"],
                selected_case_indices=indices, case_count=len(parsed["cases"]),
                partial=len(indices) != len(parsed["cases"]), cases=rows,
                local_native_build=build_identity(description), local_allocation=allocation(description),
                gpu_native_build=build_identity(gpu["description"]), gpu_runtime=_runtime_for(gpu["description"]),
                paired_precision_classes=sorted(precisions, key=repr),
                statistical_alpha_per_phase=alpha, holdout_budgets=HOLDOUT,
                blind_fresh_start_capacity="unresolved")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--cpu-report", type=Path, required=True)
    parser.add_argument("--gpu-report", type=Path, required=True)
    parser.add_argument("--gpu-reference-report", type=Path)
    parser.add_argument("--local-holdout-report", type=Path, required=True)
    parser.add_argument("--holdout-case-index", type=int, action="append")
    parser.add_argument("--reference-case-index", type=int, action="append")
    parser.add_argument("--case-index", type=int, action="append")
    parser.add_argument("--case-list", type=Path, default=DEFAULT_CASES)
    parser.add_argument("--probe", "--exporter", type=Path, default=DEFAULT_PROBE)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        cpu = read_report(args.cpu_report)
        require(cpu.get("schema") == CAPACITY, "Fresh native CPU calibration is required")
        limits = allocation(cpu["description"])
        probe = NativeProbe(args.probe)
        description = probe.call("describe", workers=limits["workers"], slot_owners=limits["slot_owners"],
                                 memory_limit_bytes=limits["memory_limit_bytes"])
        inputs = dict(cpu=args.cpu_report, gpu=args.gpu_report, holdout=args.local_holdout_report)
        if args.gpu_reference_report:
            inputs["gpu_reference"] = args.gpu_reference_report
        fingerprints = {name: sha256_file(path) for name, path in inputs.items()}
        reports = {name: read_report(path) for name, path in inputs.items()}
        result = estimate(reports["cpu"], reports["gpu"], load_cases(args.case_list), description,
                          holdout=reports["holdout"], holdout_indices=args.holdout_case_index or (),
                          reference_indices=args.reference_case_index or (),
                          gpu_reference=reports.get("gpu_reference"), case_indices=args.case_index or ())
        require(fingerprints == {name: sha256_file(path) for name, path in inputs.items()},
                "A native measurement report changed during forecasting")
        probe.verify_unchanged()
        result["input_reports"] = fingerprints
        write_result(args.output, result)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    print(result["status"])
    return 0 if result["status"] == "conditional_phase_estimates" else 2


if __name__ == "__main__":
    raise SystemExit(main())
