#!/usr/bin/env python3
"""Collect bounded native Robust workload receipts for GPU-assisted CPU forecasts."""
from __future__ import annotations

import argparse
import math
from pathlib import Path
import re
import sys
import time

from triad_cases import DEFAULT_CASES, load_cases
from triad_native import (DEFAULT_PROBE, NativeProbe, ProbeError, allocation, environment,
                          is_sha256, named_phases, phase_once, positive_int, sha256_file, write_result)

SCHEMA = "datapump.triad.native-gpu-workload.v1"
ACQUISITION_STRATA = ("active", "noise")
DIAGNOSTIC_STRATA = ("cold-maximum", "terminal", "erasure")
STRATA = ACQUISITION_STRATA + DIAGNOSTIC_STRATA
CHALLENGES = ("near-decision", "high-snr", "scored-quiet")
SCORE_ATOL, SCORE_RTOL = 2e-3, 2e-4
ADAPTIVE_COST_RTOL = .05
CUDA_RUNTIME_FIELDS = ("device", "device_name", "library_version", "cuda_runtime_version",
                       "cuda_driver_version", "cuda_blas_version", "cuda_compute_major", "cuda_compute_minor",
                       "numerical_threads", "workspace_limit_bytes",
                       "total_device_bytes", "requested_coarse_math")
PRECISION = ("fp64", "fp32_bulk_fp64_coordinates_and_reductions")
COARSE_GEOMETRY = (
    "route", "rays", "clocks", "workspaces", "frequency_points", "frequency_groups",
    "arrival_points", "source_rows", "raw_template_rows", "projected_template_rows",
    "projected_observation_rows", "template_sampling_step", "fir_taps", "fir_fft_rows",
    "correlation_fft_rows", "maximum_frequency_step_hz", "projected_sample_rate_hz",
    "clock_gap_ppm", "arrival_lower_seconds", "arrival_upper_seconds", "planned_tiles",
)
FINE_GEOMETRY = (
    "observation_rows", "available_rows", "processed_rows", "blocks", "segments", "block_rows",
    "segment_rows", "last_block_rows", "requested_invalid_rows", "additional_discarded_rows",
    "erased_blocks", "tap_count", "delay_step_seconds", "rate_step_ppm", "rate_radius",
)
LANE_CAPS = ("initial_states", "state_cap", "proposal_cap")
LANE_COUNTS = (
    "states", "last_emission_states", "peak_emission_states", "peak_retained_states",
    "peak_transition_targets", "pruning_events", "states_dropped", "transition_edges",
    "merged_channel_components", "basis_calls", "basis_rows", "positive_basis_blocks",
    "projection_modes", "gaussian_updates",
)
CACHE_GEOMETRY = ("tile_samples", "cache_limit_bytes", "dense_factor", "dense_sample_rate_hz",
                  "oversampling_ratio", "fallback_reason")
CACHE_WORK = ("cache_misses", "native_preparation_calls", "native_preparation_samples",
              "evictions", "direct_rows", "large_phase_fallback_rows", "staging_fallback_rows")


def require(condition, reason):
    if not condition:
        raise ProbeError(reason)


def select_cases(parsed, indices):
    if not indices:
        return parsed["cases"]
    require(len(indices) == len(set(indices)), "Case indices must be distinct")
    selected = [case for case in parsed["cases"] if case["index"] in set(indices)]
    require(len(selected) == len(indices), "Case index is absent from the current manual list")
    return selected


def row_matches(recorded, current):
    return all(recorded.get(key) == current[key] for key in
               ("index", "id", "configuration_hash", "native_args"))


def same_fields(left, right, fields):
    return all(key in left and key in right and left[key] == right[key] for key in fields)


def public_configuration(configuration):
    return {key: value for key, value in configuration.items() if key != "receiver_slot_workspace_bytes"}


def cuda_runtime(bulk):
    require(isinstance(bulk, dict) and bulk.get("cuda") is True
            and re.fullmatch(r"cuda:[0-9]+", str(bulk.get("device"))),
            "Native receipt lacks a CUDA device")
    require(all(isinstance(bulk.get(key), str) and bulk[key]
                for key in ("device_name", "library_version")), "Missing CUDA device or library identity")
    for key in ("cuda_driver_version", "cuda_runtime_version", "workspace_limit_bytes", "total_device_bytes"):
        positive_int(bulk.get(key), key)
    positive_int(bulk.get("cuda_blas_version"), "CUDA BLAS version", allow_zero=True)
    positive_int(bulk.get("cuda_compute_major"), "CUDA compute capability major")
    positive_int(bulk.get("cuda_compute_minor"), "CUDA compute capability minor", allow_zero=True)
    plan_count = positive_int(bulk.get("fft_plan_cache_count"), "CUDA FFT plan cache count", allow_zero=True)
    plan_limit = positive_int(bulk.get("fft_plan_cache_limit"), "CUDA FFT plan cache limit", allow_zero=True)
    require(plan_count <= plan_limit, "CUDA FFT plan cache exceeds its bound")
    headroom = positive_int(bulk.get("runtime_headroom_bytes"), "CUDA runtime headroom")
    free = positive_int(bulk.get("initial_free_device_bytes"), "CUDA initial free memory")
    require(bulk["workspace_limit_bytes"] + headroom <= free <= bulk["total_device_bytes"],
            "CUDA device memory admission was not preserved")
    require(bulk.get("numerical_threads") == 1, "Unexpected ATen host numerical thread count")
    require(bulk.get("requested_coarse_math") in ("fft", "fp16", "int8"),
            "Missing CUDA coarse arithmetic policy")
    return {key: bulk[key] for key in CUDA_RUNTIME_FIELDS}


def accelerator_stage(work, stage, *, terminal=False):
    bulk = work.get("bulk")
    runtime = cuda_runtime(bulk)
    require(bulk.get("executed") is True and bulk.get("precision") in PRECISION,
            f"No measured CUDA {stage} work with declared precision")
    counters = ("synchronizations", "upload_bytes", "download_bytes",
                "peak_reserved_bytes", "allocator_peak_live_bytes", "allocator_peak_reserved_bytes")
    for key in counters:
        positive_int(bulk.get(key), f"CUDA {stage} {key}")
    require(bulk["peak_reserved_bytes"] <= bulk["workspace_limit_bytes"] and
            bulk["allocator_peak_reserved_bytes"] <= bulk["workspace_limit_bytes"],
            f"CUDA {stage} memory exceeded its admitted budget")
    fft_calls = positive_int(bulk.get("fft_calls"), "CUDA FFTs", allow_zero=True)
    dc_values = (positive_int(bulk.get("dc_reduction_values"), "CUDA DC reduction values", allow_zero=True)
                 if stage == "fine" else 0)
    require(fft_calls or dc_values, f"CUDA {stage} did not execute FFT or explicit DC reductions")
    for key in (("score_rows", "fir_output_contributions") if stage == "coarse" else
                ("statistics_blocks", "projected_values")):
        positive_int(bulk.get(key), f"CUDA {stage} {key}")
    peak_rows = positive_int(bulk.get("cuda_arrival_peak_rows"), "CUDA arrival peak rows", allow_zero=True)
    peak_values = positive_int(bulk.get("cuda_arrival_peak_values"), "CUDA arrival peak values", allow_zero=True)
    require(bool(peak_rows) == bool(peak_values), "CUDA arrival peak work has incomplete counters")
    if peak_rows:
        require(stage == "coarse" and work.get("route") == "frequency_tiles",
                "CUDA arrival peak work requires the coarse frequency-tile route")
        lags = positive_int(work.get("arrival_points"), "coarse arrival points")
        workspaces = positive_int(work.get("workspaces"), "coarse workspaces")
        frequencies = positive_int(work.get("frequency_points"), "coarse frequency points")
        search = work.get("search")
        require(isinstance(search, dict), "CUDA arrival peak work lacks logical search counts")
        pairs = positive_int(search.get("workspace_frequency_pairs"), "coarse workspace-frequency pairs")
        values = positive_int(search.get("score_rows"), "coarse logical score rows")
        require(peak_rows == pairs == workspaces * frequencies and
                peak_values == values == bulk["score_rows"] == peak_rows * lags,
                "CUDA arrival peak work does not cover the complete logical score grid")
    # Direct native rendering is a public exceptional route. It cannot establish
    # a normal mapped acceleration cost class, even when its FFT ran on device.
    if stage == "fine":
        positive_int(bulk.get("mapped_values"), "CUDA mapped values", allow_zero=terminal)
        reasons = bulk.get("direct_fallback_counts")
        require(isinstance(reasons, dict) and reasons
                and all(type(count) is int and count == 0 for count in reasons.values())
                and bulk.get("direct_columns") == 0 and work["cache"]["fallback_reason"] == "",
                "Fine CUDA receipt includes direct rendering fallback")
    return runtime, bulk["precision"]


def _phase_work(receipt, name):
    return phase_once(receipt, name)["work"]


def validate_work(receipt, backend, stratum, *, command="continuation", declared=False):
    result = receipt["result"]
    require(receipt["status"] == "complete" and receipt["command"] == command,
            f"Incomplete native {stratum} observation: {receipt.get('reason', '')}")
    require(result.get("work_complete") is True, f"Native {stratum} work gate failed")
    require(is_sha256(receipt["observation"].get("input_sha256")),
            f"Missing {stratum} observation digest")
    require(result.get("stratum") == stratum, "Probe changed the requested work stratum")
    fine = _phase_work(receipt, "fine_finish")
    representative = fine.get("representative_work") is True
    require(fine.get("observation_complete") is True and (representative or
            stratum == "terminal" and fine.get("blocks", 0) > 0) and is_sha256(fine.get("input_sha256")),
            "Fine receipt lacks a complete measured observation")
    coarse = None
    if declared:
        require(result.get("coordinate_scope") == "declared_public_work_coordinate" and
                receipt["observation"].get("channel_scope") == "independently_seeded_native_thermal_noise_at_local_crop"
                and result.get("receiver_decision_eligible") is False
                and result.get("complete_receiver_service") is False
                and not named_phases(receipt, "coarse_selection"),
                "Declared fine probe cannot stand in for public acquisition")
    else:
        require(result.get("coordinate_scope") == "complete_public_selection",
                "Probe did not use complete public selection")
        coarse = _phase_work(receipt, "coarse_selection")
        require(coarse["completed_tiles"] == coarse["planned_tiles"] > 0 and
                bool(coarse.get("refined_coordinates_detail")),
                "Public acquisition did not complete its full search and selection")
        tiles = named_phases(receipt, "coarse_tiles")
        steps = [phase["work"]["step_tiles"] for phase in tiles]
        progress = 0
        for tile in tiles:
            progress += positive_int(tile["work"].get("step_tiles"), "coarse checkpoint width")
            require(tile["work"].get("completed_tiles") == progress and
                    tile["work"].get("planned_tiles") == coarse["planned_tiles"],
                    "Native coarse checkpoints are not consecutive")
        require(progress == coarse["planned_tiles"], "Coarse checkpoints do not cover the public search")
        if coarse["planned_tiles"] >= 4:
            require(steps[:3] == [1, 1, 2], "Coarse probe omitted the bounded 1/1/2 tile checkpoints")
    if command == "continuation" and stratum in ("active", "noise"):
        checkpoints = [_phase_work(receipt, "fine_prefix")["checkpoint_seconds"]]
        checkpoints.extend(phase["work"]["checkpoint_seconds"]
                           for phase in named_phases(receipt, "fine_continuation"))
        require(checkpoints == [2, 3, 4, 6] and receipt["result"].get("checkpoints_complete") is True,
                "Fine receipt lacks the persistent 2-second prefix and cumulative 1/2/4 continuation")
        if stratum == "active":
            require(receipt["result"].get("continued_cache_fill") is True,
                    "Fine continuation did not fill new native cache tiles")
    if stratum == "cold-maximum":
        require(receipt["result"].get("maximum_state_transition_covered") is True,
                "Native maximum-state transition was not measured")
    if stratum == "erasure":
        require(receipt["result"].get("explicit_erasure_covered") is True,
                "Native interior erasure was not measured")
    if backend == "cuda":
        fine_runtime, fine_precision = accelerator_stage(fine, "fine", terminal=declared and stratum == "terminal")
        require(fine_runtime["requested_coarse_math"] == receipt["identity"].get("requested_tensor_coarse_math"),
                "Fine CUDA receipt differs from the requested coarse arithmetic policy")
        coarse_precision = None
        if coarse is not None:
            coarse_runtime, coarse_precision = accelerator_stage(coarse, "coarse")
            require(coarse_runtime == fine_runtime, "Coarse and fine executed on different CUDA runtimes")
        if receipt["identity"].get("requested_tensor_precision") == "fp64":
            require(fine_precision == "fp64" and coarse_precision in (None, "fp64"),
                    "Explicit FP64 CUDA precision was not honored")
        return (coarse_precision, fine_precision), fine_runtime
    require((coarse is None or coarse["bulk"] is None) and fine["bulk"] is None,
            "CPU reference must use the canonical native CPU backend")
    return None, None


def _near(a, b):
    return (type(a) in (int, float) and type(b) in (int, float)
            and math.isfinite(a) and math.isfinite(b)
            and abs(a - b) <= SCORE_ATOL + SCORE_RTOL * abs(a))


def _score_pair(a, b):
    return (isinstance(a, list) and isinstance(b, list) and len(a) == len(b) == 2
            and all(_near(x, y) for x, y in zip(a, b))
            and ((a[0] > a[1]) - (a[0] < a[1])) == ((b[0] > b[1]) - (b[0] < b[1])))


def _proposal_match(left, right):
    return (len(left) == len(right) and
            all(same_fields(a, b, ("coordinate", "bit", "rank")) and _near(a["score"], b["score"])
                for a, b in zip(left, right)))


def _cost_drift(left, right, fields):
    errors = []
    for key in fields:
        a = positive_int(left.get(key), key, allow_zero=True)
        b = positive_int(right.get(key), key, allow_zero=True)
        errors.append(abs(b - a) / max(1, a))
    return max(errors, default=0.)


def _same_count_fields(left, right, fields):
    return all(type(left.get(key)) is int and left[key] >= 0
               and type(right.get(key)) is int and right[key] == left[key] for key in fields)


def _same_coarse_cpu_demand(left, right):
    # Keep this contract aligned with triad_gpu_cpu_forecast.observation:
    # CPU search and selection costs are projected from these logical counts,
    # never from GPU elapsed time, transferred bytes or peak-kernel launches.
    return (same_fields(left, right, COARSE_GEOMETRY)
            and _same_count_fields(left.get("search", {}), right.get("search", {}),
                                   ("workspace_frequency_pairs", "score_rows"))
            and _same_count_fields(left.get("selection", {}), right.get("selection", {}),
                                   ("retained_peaks", "selection_peak_rows"))
            and _same_count_fields(left, right, ("proposals", "refined_coordinates")))


def compare_work(cpu, cuda, stratum, *, command="continuation", declared=False):
    """Same-input paired parity. Excludes backend dispatch, transfer and timing."""
    precision, runtime = validate_work(cuda, "cuda", stratum, command=command, declared=declared)
    validate_work(cpu, "cpu", stratum, command=command, declared=declared)
    same_input = (public_configuration(cpu["configuration"]) == public_configuration(cuda["configuration"]) and
                  allocation(cpu) == allocation(cuda) and
                  same_fields(cpu["observation"], cuda["observation"],
                              ("input_sha256", "seed", "public_slot", "pattern_epoch", "symbol_ordinal")))
    acquisition_match = None
    logical_coarse_cpu_work_match = None
    exact_arrival_mapping = None
    if not declared:
        coarse_a = _phase_work(cpu, "coarse_selection")
        coarse_b = _phase_work(cuda, "coarse_selection")
        logical_coarse_cpu_work_match = _same_coarse_cpu_demand(coarse_a, coarse_b)
        acquisition_match = (
            same_fields(coarse_a, coarse_b, COARSE_GEOMETRY) and
            same_fields(coarse_a.get("search", {}), coarse_b.get("search", {}),
                        ("workspace_frequency_pairs", "score_rows")) and
            same_fields(_phase_work(cpu, "coarse_prepare"), _phase_work(cuda, "coarse_prepare"),
                        ("frequency_grid_sha256",)) and
            coarse_a["refined_coordinates_detail"] == coarse_b["refined_coordinates_detail"] and
            _proposal_match(coarse_a["proposals_detail"], coarse_b["proposals_detail"])
        )
    score_a, score_b = (receipt["result"]["score"] for receipt in (cpu, cuda))
    decision_match = (same_fields(score_a, score_b, ("decision", "bit", "complete", "all_valid",
                                                    "zero_h0_verified", "quiet_modes",
                                                    "operational_passes_conditional_bound"))
                      and all(_score_pair(score_a.get(key), score_b.get(key)) for key in
                              ("bit_evidence", "adaptive_bit_evidence", "combined_bit_evidence")))
    checkpoints = ("fine_prefix", "fine_continuation", "fine_work", "fine_finish")
    fine_a, fine_b = ([phase["work"] for phase in receipt["phases"] if phase["name"] in checkpoints]
                      for receipt in (cpu, cuda))
    fixed_match = len(fine_a) == len(fine_b) and bool(fine_a)
    adaptive_cost_drift = 0.
    for a, b in zip(fine_a, fine_b):
        fixed_match &= (
            same_fields(a, b, FINE_GEOMETRY + ("input_sha256", "observation_first_sample", "quiet_modes"))
            and same_fields(a["initial_affine"], b["initial_affine"], LANE_CAPS + LANE_COUNTS + ("states_sha256",))
            and same_fields(a["adaptive"], b["adaptive"], LANE_CAPS)
            and same_fields(a["cache"], b["cache"], CACHE_GEOMETRY + CACHE_WORK)
            and all(_score_pair(a[lane]["bit_scores"], b[lane]["bit_scores"])
                    for lane in ("initial_affine", "adaptive"))
        )
        adaptive_cost_drift = max(adaptive_cost_drift, _cost_drift(a["adaptive"], b["adaptive"], LANE_COUNTS))
    # Matrix replacements still lack a qualified CPU-demand mapping. Exact
    # arrival reduction is different: it preserves FFT scoring and the complete
    # native search/selection demand consumed by observation(). Its GPU timing
    # and transfer savings must not enter a CPU forecast.
    unqualified_cost_leaves = {field
        for phase in cuda["phases"]
        for field in ("cuda_matrix_fp16_calls", "cuda_matrix_int8_calls")
        if (phase["work"].get("bulk") or {}).get(field, 0) != 0}
    same_native_source = (is_sha256(cpu["identity"].get("source_manifest_sha256"))
                          and cpu["identity"]["source_manifest_sha256"] ==
                              cuda["identity"].get("source_manifest_sha256"))
    if not declared and coarse_b["bulk"]["cuda_arrival_peak_rows"]:
        # validate_work already proved all W*F rows and W*F*L scores were
        # reduced, with a complete frequency-tile grid. Qualify only the FFT
        # policy and exact CPU logical demand, from a paired native source.
        exact_arrival_mapping = bool(
            coarse_b["route"] == "frequency_tiles"
            and cuda["identity"].get("requested_tensor_coarse_math") == "fft"
            and coarse_b["bulk"]["requested_coarse_math"] == "fft"
            and not unqualified_cost_leaves and same_native_source
            and logical_coarse_cpu_work_match and acquisition_match and same_input)
        if not exact_arrival_mapping:
            unqualified_cost_leaves.add("cuda_arrival_peak_rows")
    unqualified_cost_leaves = sorted(unqualified_cost_leaves)
    cpu_cost_work_match = not unqualified_cost_leaves and logical_coarse_cpu_work_match is not False
    numerical_match = (same_input and acquisition_match is not False and decision_match and fixed_match
                       and adaptive_cost_drift <= ADAPTIVE_COST_RTOL)
    accepted = numerical_match and cpu_cost_work_match
    return dict(accepted=accepted, numerical_parity_accepted=numerical_match,
                cpu_cost_work_match=cpu_cost_work_match, cpu_cost_unqualified_leaves=unqualified_cost_leaves,
                logical_coarse_cpu_work_match=logical_coarse_cpu_work_match,
                exact_arrival_cpu_demand_mapping=exact_arrival_mapping, same_native_source=same_native_source,
                same_input=same_input, acquisition_match=acquisition_match,
                decisions_and_scores_match=decision_match, fixed_work_match=fixed_match,
                maximum_relative_adaptive_cost_drift=adaptive_cost_drift,
                precision=list(precision), cuda_runtime=runtime)


def reference_challenge(cpu, cuda, role):
    stratum = "noise" if role == "scored-quiet" else "active"
    comparison = compare_work(cpu, cuda, stratum, command="slot")
    score = cpu["result"]["score"]
    if role == "near-decision":
        evidence, threshold = score["operational_log_e"], score["operational_threshold"]
        relevant = (type(evidence) in (float, int) and type(threshold) in (float, int)
                    and abs(evidence - threshold) <= max(3., .15 * abs(threshold)))
    elif role == "high-snr":
        config = cpu["configuration"]
        relevant = (config["actual_cn0_db_hz"] >= config["target_cn0_db_hz"] + 20 and
                    cpu["result"].get("evaluator_expected_decision_observed") is True)
    else:
        relevant = (score["complete"] is True and score["decision"] == "quiet" and
                    cpu["result"].get("evaluator_expected_decision_observed") is True)
    comparison["relevant_observation"] = relevant
    comparison["accepted"] &= relevant and cpu["result"].get("receiver_decision_eligible") is True
    return comparison


def _case_receipts(probe, case, options, seed, *, cpu_reference=None, near_cn0=None, backlog_slots=0,
                   row_deadline_ms):
    cutoff = time.monotonic() + row_deadline_ms / 1000
    cpu_options = {name: value for name, value in options.items()
                   if name not in ("device_index", "device_memory_limit_bytes", "tensor_precision", "tensor_coarse_math")}

    def call(command, *, receiver=probe, **kwargs):
        remaining = int(1000 * (cutoff - time.monotonic()))
        require(remaining > 0, "Declared native GPU workload row deadline reached")
        flags = options if receiver is probe else cpu_options
        return receiver.call(command, case["native_args"], **(flags | kwargs |
                             {"deadline_ms": min(remaining, options["deadline_ms"]), "seed": seed}))

    plan = call("plan")
    row = dict(case=case, plan=plan, strata={}, references={}, missing=[])
    if plan["status"] != "complete":
        row["missing"].append(f"plan: {plan['reason']}")
        return row
    config = plan["configuration"]
    recurring_slot = config["recurring_horizon"]["largest_window_slot"]
    for name in STRATA:
        declared = name in DIAGNOSTIC_STRATA
        short_window = config["recurring_horizon"]["largest_window_samples"] < 6 * config["sample_rate_hz"]
        short = (name == "active" and not config["nominal_continuation_eligible"]) or (name == "noise" and short_window)
        command = "slot" if short else "continuation"
        kwargs = dict(stratum=name, slot_index=recurring_slot, **({"work_only": True} if declared else {}))
        try:
            receipt = call(command, **kwargs)
            row["strata"][name] = receipt
            validate_work(receipt, probe.backend, name, command=command, declared=declared)
            if cpu_reference is not None:
                paired = call(command, receiver=cpu_reference, **kwargs)
                comparison = compare_work(paired, receipt, name, command=command, declared=declared)
                row["references"][name] = dict(cpu=paired, comparison=comparison)
                if not comparison["accepted"]:
                    row["missing"].append(f"{name}: CPU/CUDA parity")
        except (ProbeError, KeyError) as error:
            row["missing"].append(f"{name}: {error}")
            # The acquisition/setup prerequisite is common to the later probes.
            if name == "active" and row["strata"].get(name, {}).get("status") != "complete":
                break
    if cpu_reference is not None and not row["missing"]:
        target = plan["configuration"]["target_cn0_db_hz"]
        cn0_values = (near_cn0 if near_cn0 is not None else target, target + 24., None)
        for role, cn0 in zip(CHALLENGES, cn0_values):
            stratum = "noise" if role == "scored-quiet" else "active"
            try:
                receipt = call("slot", stratum=stratum, cn0=cn0)
                paired = call("slot", receiver=cpu_reference, stratum=stratum, cn0=cn0)
                row["references"][role] = dict(cpu=paired, cuda=receipt)
                comparison = reference_challenge(paired, receipt, role)
                row["references"][role]["comparison"] = comparison
                if not comparison["accepted"]:
                    row["missing"].append(f"{role}: paired score, work or operating-regime gate")
            except (ProbeError, KeyError) as error:
                row["missing"].append(f"{role}: {error}")
    if backlog_slots and not row["missing"]:
        try:
            backlog = call("backlog", slots=backlog_slots)
            row["backlog"] = [backlog]
            if (backlog["status"] != "complete" or
                    backlog["result"].get("backlog_interval", {}).get("complete") is not True):
                row["missing"].append(f"Same-stream holdout: {backlog.get('reason') or 'no ready-input interval'}")
        except ProbeError as error:
            row["missing"].append(f"Same-stream holdout: {error}")
        try:
            codec = call("codec")
            row["codec"] = [codec]
            if codec["status"] != "complete":
                row["missing"].append(f"Long codec holdout: {codec.get('reason') or 'incomplete'}")
        except ProbeError as error:
            row["missing"].append(f"Long codec holdout: {error}")
    return row


def run_probe(*, case_list=DEFAULT_CASES, case_indices=None, executable=DEFAULT_PROBE, workers=None,
              device="cuda", output, paired_reference=False, workspace_mib=None, reference_cases=None,
              slot_owners=None, device_memory_mib=None, seed=74021, max_samples=4_194_304,
              deadline_ms=30_000, coarse_tiles=4096, device_index=0, near_cn0=None,
              backlog_slots=0, tensor_precision="automatic", tensor_coarse_math="fft",
              fixed_active_seconds=None, row_deadline_ms=180_000):
    parsed = load_cases(case_list)
    selected = select_cases(parsed, case_indices)
    references = tuple(reference_cases or ())
    if paired_reference and not references:
        references = tuple(case["index"] for case in selected)
    require(len(references) == len(set(references)) <= 3 and
            set(references) <= {case["index"] for case in selected},
            "Declare at most three distinct paired reference rows within the selection")
    require(device in ("cpu", "cuda") and (device == "cuda" or not references),
            "Paired references require a CUDA workload")
    require(tensor_coarse_math in ("fft", "fp16", "int8") and
            (device == "cuda" or tensor_coarse_math == "fft"),
            "Tensor coarse arithmetic requires CUDA")
    require(near_cn0 is None or (math.isfinite(near_cn0) and len(references) == 1),
            "An explicit near-decision C/N0 requires one paired reference row")
    positive_int(seed, "seed", allow_zero=True)
    positive_int(row_deadline_ms, "row deadline milliseconds")
    require(fixed_active_seconds is None or (math.isfinite(fixed_active_seconds) and fixed_active_seconds > 0),
            "Fixed active seconds must be positive and finite")
    positive_int(backlog_slots, "backlog slots", allow_zero=True)
    require(backlog_slots == 0 or (device == "cpu" and 10 <= backlog_slots <= 1024),
            "Backlog holdout requires the local CPU and 10 to 1024 declared slots")
    positive_int(coarse_tiles, "coarse tile budget")
    positive_int(device_index, "CUDA device index", allow_zero=True)
    if workspace_mib is not None:
        positive_int(workspace_mib, "host workspace MiB")
    if device_memory_mib is not None:
        positive_int(device_memory_mib, "device workspace MiB")
        require(device == "cuda", "Device workspace limit requires CUDA")
    limits = dict(workers=workers, slot_owners=slot_owners, seconds=fixed_active_seconds,
                  memory_limit_bytes=None if workspace_mib is None else workspace_mib << 20,
                  max_samples=max_samples, deadline_ms=deadline_ms, coarse_tiles=coarse_tiles)
    options = limits | (dict(device_index=device_index, tensor_precision=tensor_precision, tensor_coarse_math=tensor_coarse_math,
                             device_memory_limit_bytes=None if device_memory_mib is None else device_memory_mib << 20)
                        if device == "cuda" else {})
    began = time.perf_counter()
    probe = NativeProbe(executable, backend=device)
    description = probe.call("describe", **options)
    report = dict(schema=SCHEMA, status="unresolved", device=device, description=description,
                  host=environment(device), input_sha256=parsed["source_sha256"],
                  selected_case_indices=[case["index"] for case in selected], case_count=len(parsed["cases"]),
                  reference_case_indices=list(references), independent_seed=seed,
                  partial=len(selected) != len(parsed["cases"]), cases=[],
                  policy=dict(strata=list(STRATA), challenges=list(CHALLENGES), per_command=options,
                              row_deadline_ms=row_deadline_ms,
                              maximum_commands=1 + (6 + 2 * bool(backlog_slots)) * len(selected) +
                                               (len(STRATA) + 2 * len(CHALLENGES)) * len(references),
                              score_atol=SCORE_ATOL, score_rtol=SCORE_RTOL,
                              adaptive_cost_rtol=ADAPTIVE_COST_RTOL,
                              stop_rule="One bounded native observation per stratum and one paired observation "
                                        "per declared reference stratum or challenge"))
    if description["status"] == "complete":
        resolved = allocation(description)
        options.update({name: resolved[name] for name in ("workers", "slot_owners", "memory_limit_bytes")})
        if device == "cuda":
            runtime = cuda_runtime(description["identity"].get("backend_receipt"))
            require(runtime["device"] == f"cuda:{device_index}" and
                    (device_memory_mib is None or runtime["workspace_limit_bytes"] == device_memory_mib << 20),
                    "Native CUDA device or explicit workspace differs from the request")
        cpu = NativeProbe(executable, backend="cpu") if references else None
        for case in selected:
            try:
                row = _case_receipts(probe, case, options, seed,
                    cpu_reference=cpu if case["index"] in references else None,
                    near_cn0=near_cn0, backlog_slots=backlog_slots, row_deadline_ms=row_deadline_ms)
            except ProbeError as error:
                row = dict(case=case, strata={}, references={}, missing=[str(error)])
            report["cases"].append(row)
        if all(not row["missing"] for row in report["cases"]):
            report["status"] = "bounded_native_work_observed"
        if cpu:
            cpu.verify_unchanged()
    else:
        report["reason"] = description.get("reason")
    probe.verify_unchanged()
    require(sha256_file(case_list) == parsed["source_sha256"] and report["host"] == environment(device),
            "Manual case list or host allocation changed during measurement")
    report["identity_verified_at_completion"] = True
    report["elapsed_seconds"] = time.perf_counter() - began
    write_result(output, report)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--case-list", type=Path, default=DEFAULT_CASES)
    parser.add_argument("--case-index", action="append", type=int)
    parser.add_argument("--probe", "--exporter", type=Path, default=DEFAULT_PROBE)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workers", type=int, help="default: twice effective logical hardware threads")
    parser.add_argument("--slot-owners", type=int)
    parser.add_argument("--device", choices=("cpu", "cuda"), default="cuda")
    parser.add_argument("--device-index", type=int, default=0)
    parser.add_argument("--device-memory-mib", type=int)
    parser.add_argument("--tensor-precision", choices=("automatic", "fp64"), default="automatic")
    parser.add_argument("--tensor-coarse-math", choices=("fft", "fp16", "int8"), default="fft")
    parser.add_argument("--workspace-mib", type=int)
    parser.add_argument("--seed", type=int, default=74021)
    parser.add_argument("--max-samples", type=int, default=4_194_304)
    parser.add_argument("--deadline-ms", type=int, default=30_000)
    parser.add_argument("--row-deadline-ms", type=int, default=180_000)
    parser.add_argument("--coarse-tiles", type=int, default=4096)
    parser.add_argument("--fixed-active-seconds", type=float)
    parser.add_argument("--paired-reference", action="store_true")
    parser.add_argument("--reference-case", action="append", type=int)
    parser.add_argument("--near-cn0", type=float, help="predeclared near-decision C/N0 for one reference row")
    parser.add_argument("--backlog-slots", type=int, default=0,
                        help="measure one fresh CPU stream and native long codec for each selected row")
    args = parser.parse_args(argv)
    try:
        report = run_probe(case_list=args.case_list, case_indices=args.case_index, executable=args.probe,
                           workers=args.workers, device=args.device, output=args.output,
                           paired_reference=args.paired_reference, workspace_mib=args.workspace_mib,
                           reference_cases=args.reference_case, slot_owners=args.slot_owners,
                           device_memory_mib=args.device_memory_mib, seed=args.seed,
                           max_samples=args.max_samples, deadline_ms=args.deadline_ms,
                           coarse_tiles=args.coarse_tiles, device_index=args.device_index, near_cn0=args.near_cn0,
                           backlog_slots=args.backlog_slots, tensor_precision=args.tensor_precision,
                           tensor_coarse_math=args.tensor_coarse_math,
                           fixed_active_seconds=args.fixed_active_seconds, row_deadline_ms=args.row_deadline_ms)
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    print(report["status"])
    return 0 if report["status"] == "bounded_native_work_observed" else 2


if __name__ == "__main__":
    sys.exit(main())
