"""Bounded absolute-capacity models over native Robust measurement receipts."""
from __future__ import annotations

import math
import statistics
import time

from triad_case_metrics import ALPHA, LOOKS, MODEL_LIMIT, PRECISION, arithmetic_interval, summarize_service, workspace_summary
from triad_native import (ProbeError, allocation, finite_number, is_sha256,
                          named_phases, phase_once, positive_int)

STRATA = ("active", "noise", "cold-maximum", "terminal", "erasure")
SCOPE = "configured_public_calendar_and_keyed_ordinal"
SUPPORTED = frozenset(("abbreviated_holdout_supported", "measured_fixed_geometry",
                       "measured_conservative_state_envelope"))
ACQUISITION = ("input_validation", "helper", "coarse_prepare", "coarse_tiles", "coarse_selection")
RATIO = "ratio_processing_seconds_per_baseband_second"
INTERVAL = RATIO + "_interval"


def measurement_policy(case_count, unit_seconds):
    return {
        "schema": "datapump.triad.native-capacity-policy.v1",
        "strata": list(STRATA), "unit_baseband_seconds": unit_seconds,
        "family_alpha": ALPHA, "family_size": max(1, case_count * 24),
        "complete_public_acquisition": True, "recurring_public_horizon_slots": 24,
        "nominal_fine": {"prefix_seconds": 2, "continuation_checkpoints_seconds": [1, 2, 4],
                         "looks": list(LOOKS), "maximum_observed_baseband_seconds_per_stratum": 54,
                         "state_relative_range_limit": 1.3,
                         "stop": "Precision at a declared look, failed work/holdout gate, or bounded limit"},
        "cold_maximum": "Two actual half-second segments with 256 states per lane and a transition",
        "pilot": "One bounded matched native stream per serial, physical and logical allocation. Inference uses fresh native observations.",
        "normalization": "Elapsed service per unique baseband second or per shortest declared public slot interval",
        "sustained_allocation_extra_observations": 3,
        "source_scope": SCOPE,
    }


def unqualified_blind_capacity(slot_seconds=None):
    return {"status": "unqualified", "unconfigured_epoch_ordinal_timing": True,
            "additional_hypothesis_work_measured": False, "configured_slot_seconds": slot_seconds}


def slot_costs(receipt):
    """Actual once-per-public-window costs. No partial-grid scale factor."""
    if receipt["status"] != "complete" or receipt["result"].get("work_complete") is not True:
        raise ProbeError(receipt.get("reason") or "Native slot work did not complete")
    selection = phase_once(receipt, "coarse_selection")["work"]
    planned = positive_int(selection.get("planned_tiles"), "planned coarse tiles")
    if selection.get("completed_tiles") != planned:
        raise ProbeError("Native receiver did not finish the full public coarse grid")
    tiles = named_phases(receipt, "coarse_tiles")
    progress = 0
    for tile in tiles:
        work = tile["work"]
        progress += positive_int(work.get("step_tiles"), "measured coarse step tiles")
        if work.get("completed_tiles") != progress or work.get("planned_tiles") != planned:
            raise ProbeError("Native coarse tile receipts are not consecutive")
    if progress != planned:
        raise ProbeError("Native coarse receipt omitted measured search work")
    costs = {name: phase_once(receipt, name)["wall_seconds"]
             for name in ACQUISITION if name != "coarse_tiles"}
    costs["coarse_tiles"] = sum(tile["wall_seconds"] for tile in tiles)
    frontend = phase_once(receipt, "audio_frontend")
    window = receipt["configuration"]["public_window_samples"]
    output_rows = positive_int(frontend["work"].get("output_rows"), "projected PCM rows")
    if frontend["work"].get("input_rows") != window or output_rows > window:
        raise ProbeError("Native PCM projection did not cover this public window")
    costs["audio_frontend"] = frontend["wall_seconds"]
    costs["audio_frontend_output_seconds"] = output_rows / receipt["configuration"]["sample_rate_hz"]
    return costs


def _acquisition_rate(costs, *, cadence):
    # The logical PCM frontend is fed continuously, including the public gap.
    # Its observed output-rate cost also charges native setup and FIR warmup.
    return (sum(costs[name] for name in ACQUISITION) / cadence
            + costs["audio_frontend"] / costs["audio_frontend_output_seconds"])


def partial_acquisition(receipt, *, cadence):
    """Preserve scoped native partial cost even when no proposal may be used."""
    preparation = named_phases(receipt, "coarse_prepare")
    tiles = named_phases(receipt, "coarse_tiles")
    selection = named_phases(receipt, "coarse_selection")
    if len(preparation) != 1 or not tiles or len(selection) > 1:
        return None
    plan = preparation[0]["work"]
    planned = positive_int(plan.get("planned_tiles"), "planned partial coarse tiles")
    completed = positive_int(tiles[-1]["work"].get("completed_tiles"), "completed partial coarse tiles")
    if completed > planned:
        raise ProbeError("Native partial coarse measurement exceeded its fixed public grid")
    observed = sum(phase["wall_seconds"] for phase in receipt["phases"]
                   if phase["name"] in ACQUISITION)
    selected = bool(selection and completed == planned and selection[0]["work"].get("completed_tiles") == planned)
    out = {"status": "partial_measured_work", "planned_tiles": planned, "completed_tiles": completed,
           "observed_acquisition_seconds": observed,
           "observed_partial_slot_ratio": observed / cadence,
           "full_selection_measured": selected, "nominal_coordinates_available": selected,
           "missing": ["nominal complete receiver work"],
           "conditional_tile_search": None}
    if selected:
        out["measured_complete_component_lower_bound"] = observed / cadence
        return out
    out["missing"].insert(0, "full-grid selection")
    if completed < planned:
        out["missing"].insert(0, "remaining fixed-grid search tiles")
    # This is a diagnostic point conditional on fixed repeated tile work.
    # It needs independent looks and a measured final selection before use in
    # a complete estimate. Short trailing tiles and grouped lag work are distinct.
    width = plan.get("tile_width")
    frequencies = plan.get("frequency_points")
    if (plan.get("route") != "frequency_tiles" or type(width) is not int or width < 1
            or type(frequencies) is not int or frequencies != width * planned or planned <= 4
            or [phase["work"].get("step_tiles") for phase in tiles[:3]] != [1, 1, 2]):
        return out
    operations = ("forward_ffts", "inverse_ffts", "fft_rows", "fft_row_stages",
                  "workspace_frequency_pairs", "score_rows", "singleton_dot_terms", "retained_peaks")
    previous = {name: plan["search"].get(name, 0) for name in operations}
    signatures = []
    for phase, count in zip(tiles[:3], (1, 1, 2)):
        search = phase["work"]["search"]
        increment = tuple(search[name] - previous[name] for name in operations)
        if any(value < 0 or value % count for value in increment):
            return out
        signatures.append(tuple(value // count for value in increment))
        previous = search
    if len(set(signatures)) != 1 or not signatures[0][operations.index("score_rows")]:
        return out
    elapsed = [phase["wall_seconds"] for phase in tiles[:3]]
    training = (elapsed[0] + elapsed[1]) / 2
    if training <= 0 or not 1 / (1 + MODEL_LIMIT) <= elapsed[2] / (2 * training) <= 1 + MODEL_LIMIT:
        return out
    out["conditional_tile_search"] = {
        "point_seconds": planned * training, "interval_seconds": None,
        "holdout_ratio": elapsed[2] / (2 * training),
        "model_status": "single_holdout_diagnostic", "usable_for_capacity": False,
        "scope": "Single fixed-width tile-work look. Final selection and independent timing uncertainty remain unresolved.",
    }
    return out


def fine_continuation(receipt):
    """Check one uninterrupted native session and return its measured rates."""
    result = receipt["result"]
    if receipt["status"] != "complete" or not (
            result.get("work_complete") and result.get("checkpoints_complete")
            and result.get("continued_cache_fill")
            and result.get("receiver_decision_eligible") is False):
        raise ProbeError(receipt.get("reason") or "Native fine continuation failed its cache or work gate")
    prefix = phase_once(receipt, "fine_prefix")
    continuation = named_phases(receipt, "fine_continuation")
    finish = phase_once(receipt, "fine_finish")
    checkpoints = [prefix, *continuation]
    if [phase["represented_seconds"] for phase in checkpoints] != [2, 1, 1, 2]:
        raise ProbeError("Native fine continuation must advance through 2/3/4/6 seconds")
    work = [phase["work"] for phase in checkpoints]
    if [row.get("checkpoint_seconds") for row in work] != [2, 3, 4, 6]:
        raise ProbeError("Native fine receipt is missing a declared cumulative checkpoint")
    rate = finite_number(work[0]["sample_rate_hz"], "sample rate", nonnegative=True)
    if rate <= 0 or not rate.is_integer():
        raise ProbeError("Native fine sample rate must be a positive integer")
    identity_keys = ("input_sha256", "observation_first_sample", "observation_rows",
                     "public_slot", "pattern_epoch", "symbol_ordinal", "workers",
                     "sample_rate_hz", "block_rows", "segment_rows", "tap_count",
                     "delay_step_seconds", "rate_step_ppm", "rate_radius")
    identity = {key: work[0][key] for key in identity_keys}
    if (not is_sha256(identity["input_sha256"]) or identity["observation_rows"] != 6 * rate
            or any(any(row.get(key) != value for key, value in identity.items()) for row in work)):
        raise ProbeError("Native fine continuation changed its input or fixed geometry")
    if any(row.get("processed_rows") != seconds * rate or not row.get("representative_work")
           for row, seconds in zip(work, (2, 3, 4, 6))):
        raise ProbeError("Native fine continuation omitted representative block work")
    final = finish["work"]
    if (not final.get("finished") or not final.get("observation_complete")
            or final.get("processed_rows") != 6 * rate
            or any(final.get(key) != value for key, value in identity.items())):
        raise ProbeError("Native fine continuation endpoint did not finish the measured session")
    caches = [row["cache"] for row in work]
    cache_geometry = ("tile_samples", "cache_limit_bytes", "dense_factor", "dense_sample_rate_hz")
    if any(cache.get("fallback_reason") or any(cache.get(key) != caches[0].get(key)
            for key in cache_geometry) for cache in caches):
        raise ProbeError("Native fine cache geometry or mapped path changed")
    for key in ("cache_misses", "native_preparation_calls", "native_preparation_samples"):
        if caches[-1].get(key, 0) <= caches[0].get(key, 0):
            raise ProbeError("Native continuation did not demonstrate new mapped cache fills")
    clock = abs(float(receipt["configuration"]["clock_bound_ppm"]))
    clock += abs(float(work[0]["rate_step_ppm"])) * work[0]["rate_radius"]
    minimum_scale = 1 - clock * 1e-6
    tile_width = positive_int(caches[-1]["tile_samples"], "mapped cache tile width")
    crossed_widths = 4 * rate * caches[-1]["dense_factor"] * minimum_scale / tile_width
    if minimum_scale <= 0 or crossed_widths <= 1:
        raise ProbeError("Native continued input did not span a complete public cache tile")
    states = {}
    for lane in ("initial_affine", "adaptive"):
        observations = [row[lane] for row in work]
        state_ids = [row.get("states_sha256") for row in observations]
        if (not all(is_sha256(value) for value in state_ids)
                or final[lane].get("states_sha256") != state_ids[-1]
                or (lane == "initial_affine" and len(set(state_ids)) != 1)):
            raise ProbeError(f"{lane} persistent native state identity changed")
        count_keys = ("states", "last_emission_states")
        counts = [positive_int(row[key], f"{lane} state count")
                  for row in observations for key in count_keys]
        max_ratio = 1.0 if lane == "initial_affine" else 1.3
        if max(counts) / min(counts) > max_ratio:
            raise ProbeError(f"{lane} state geometry drifted during native continuation")
        fixed = (observations[0]["state_cap"], observations[0]["proposal_cap"])
        if any((row["state_cap"], row["proposal_cap"]) != fixed for row in observations):
            raise ProbeError("Native fine state or proposal cap changed")
        energies = [row["projected_template_energy"] for row in observations]
        if not all(later > earlier for earlier, later in zip((0., *energies[:-1]), energies)):
            raise ProbeError("Native continuation checkpoint did not render positive template work")
        states[lane] = {"counts": counts, "caps": list(fixed)}
    durations = [row["wall_seconds"] for row in continuation]
    elapsed = 0.
    rates = []
    for seconds, duration in zip((1, 2, 4), durations):
        elapsed += duration
        rates.append(elapsed / seconds)
    if min(prefix["wall_seconds"], finish["wall_seconds"], *rates) <= 0:
        raise ProbeError("Native continuation did not measure positive elapsed costs")
    return {"prefix_seconds": prefix["wall_seconds"], "finish_seconds": finish["wall_seconds"],
            "continuation_rates": rates, "states": states, "cache_widths_crossed": crossed_widths,
            "fixed_geometry": {key: value for key, value in identity.items()
                               if key not in ("input_sha256", "observation_first_sample")}}


def direct_fine(receipt, stratum):
    work = phase_once(receipt, "fine_work")
    finish = phase_once(receipt, "fine_finish")
    result = receipt["result"]
    if stratum == "cold-maximum" and result.get("maximum_state_transition_covered") is not True:
        raise ProbeError("Native cold-state probe missed its 256-state transition")
    if stratum == "erasure" and result.get("explicit_erasure_covered") is not True:
        raise ProbeError("Native erasure probe missed its erased block")
    if not finish["work"].get("finished") or not finish["work"].get("observation_complete"):
        raise ProbeError("Native fine endpoint did not finish")
    return work["wall_seconds"] + finish["wall_seconds"], work["work"]


def _ratio(point=None, interval=None, *, model_status="unresolved", reason=None):
    if point is not None and point >= 1:
        status = "fail"
    elif interval is not None and interval[1] < 1 and model_status in SUPPORTED:
        status = "pass"
    else:
        status = "unresolved"
    return {RATIO: point, INTERVAL: interval,
            "margin_fraction": None if point is None else 1 - point,
            "margin_interval": None if interval is None else [1 - interval[1], 1 - interval[0]],
            "numeric_point_criterion": "unresolved" if point is None else "fail" if point >= 1 else "below_one",
            "model_status": model_status, "realtime_status": status, "reason": reason}


def measured_ratio(values, *, family_size, model_status="measured_fixed_geometry"):
    if not values:
        return _ratio(reason="no applicable native measurements")
    interval = None
    point = statistics.fmean(values)
    if len(values) >= 3:
        point, interval = arithmetic_interval(values, alpha=ALPHA / (family_size * len(LOOKS) * 3))
    return _ratio(point, interval, model_status=model_status)


def combine_components(components, *, complete_work_covered, workspace=None):
    """Sum sequential costs without crediting unmeasured work or concurrency."""
    usable = []
    for value in components.values():
        point = value.get(RATIO)
        bounds = value.get(INTERVAL)
        if point is not None:
            finite_number(point, "component ratio", nonnegative=True)
            usable.append(value)
        if bounds is not None:
            if not isinstance(bounds, (tuple, list)) or len(bounds) != 2 or bounds[0] > bounds[1]:
                raise ProbeError("Invalid native service interval")
            for bound in bounds:
                finite_number(bound, "component service bound", nonnegative=True)
    subtotal = sum(row[RATIO] for row in usable) if usable else None
    points_complete = bool(components) and len(usable) == len(components)
    interval = None
    if points_complete and all(row[INTERVAL] is not None for row in usable):
        interval = [sum(row[INTERVAL][index] for row in usable) for index in (0, 1)]
    qualified = points_complete and complete_work_covered and all(
        row.get("model_status") in SUPPORTED for row in usable)
    result = _ratio(subtotal if points_complete else None, interval,
                    model_status="measured_fixed_geometry" if qualified else "unresolved",
                    reason=None if qualified else "unmeasured work or unsupported native phase model")
    lower = sum(row.get("measured_complete_component_lower_bound", 0.) for row in components.values())
    if lower >= 1 or (workspace and workspace.get("status") == "failure"):
        result["realtime_status"] = "fail"
        result["reason"] = ("receiver workspace exceeds budget" if workspace and workspace["status"] == "failure"
                            else "measured required work alone reaches the real-time limit")
    return result | {"complete_work_covered": points_complete and complete_work_covered,
                     "covered_stage_subtotal_processing_seconds_per_baseband_second": subtotal,
                     "all_component_models_supported": qualified,
                     "measured_complete_component_lower_bound": lower, "components": components}


def _worst_component(rows):
    lower = max((row.get("measured_complete_component_lower_bound", 0.) for row in rows), default=0.)
    if not rows or any(row[RATIO] is None for row in rows):
        return _ratio(reason="missing applicable native work class") | {"measured_complete_component_lower_bound": lower}
    point = max(row[RATIO] for row in rows)
    intervals = [row[INTERVAL] for row in rows]
    interval = [max(row[index] for row in intervals) for index in (0, 1)] if all(intervals) else None
    model = ("measured_conservative_state_envelope"
             if all(row["model_status"] in SUPPORTED for row in rows) else "unresolved")
    return _ratio(point, interval, model_status=model) | {"measured_complete_component_lower_bound": lower}


def _continued_component(samples, *, window_seconds, cadence, family_size):
    if not samples or len(samples) not in LOOKS:
        return _ratio(reason="no complete declared native continuation look")
    summary = summarize_service([row["continuation_rates"] for row in samples], family_size=family_size)
    endpoints = measured_ratio([(row["prefix_seconds"] + row["finish_seconds"]) / cadence
                                for row in samples], family_size=family_size)
    multiplier = max(0., window_seconds - 2) / cadence
    rates = _ratio(summary[RATIO] * multiplier, [x * multiplier for x in summary[INTERVAL]],
                   model_status=summary["model_status"])
    return combine_components({"prefix_and_finish_once": endpoints, "continued": rates},
                              complete_work_covered=True) | {
        "continuation": summary, "continuation_remaining_window_per_slot": multiplier}


def _compatible_work(first, current):
    if first["fixed_geometry"] != current["fixed_geometry"]:
        return False
    for lane in ("initial_affine", "adaptive"):
        a, b = first["states"][lane], current["states"][lane]
        counts = a["counts"] + b["counts"]
        if a["caps"] != b["caps"] or max(counts) / min(counts) > (1 if lane == "initial_affine" else 1.3):
            return False
    return True


def _stream_ratios(receipt, expected_slots):
    """Actual same-stream service and optional ready-input backlog interval."""
    result = receipt["result"]
    stream = result.get("stream") or {}
    slots = stream.get("slots") or []
    digest = (receipt["observation"] or {}).get("input_sha256")
    if (receipt["status"] not in ("complete", "incomplete") or stream.get("state") != "input_ended"
            or stream.get("work_complete") is not True
            or not is_sha256(digest) or stream.get("verified_input_sha256") != digest
            or stream.get("committed_slots") != expected_slots or len(slots) != expected_slots
            or any(slot.get("complete") is not True for slot in slots)):
        raise ProbeError(receipt.get("reason") or "Native stream did not score the declared horizon")
    phase = phase_once(receipt, "receiver_service")
    if (phase["represented_seconds"] <= 0 or phase["wall_seconds"] <= 0
            or phase["represented_seconds"] != stream["unique_baseband_seconds"]):
        raise ProbeError("Native stream has no positive measured service denominator")
    backlog = result.get("backlog_interval") or {}
    wall = finite_number(backlog["wall_seconds"], "backlog wall time", nonnegative=True) if backlog.get("complete") else None
    seconds = (finite_number(backlog["unique_baseband_seconds"], "backlog input duration", nonnegative=True)
               if backlog.get("complete") else None)
    backlog_ratio = wall / seconds if wall and seconds else None
    return phase["wall_seconds"] / phase["represented_seconds"], backlog_ratio


def _stream_plan(invoke, plan, policy, workers, records):
    first = plan["configuration"].get("stream_plan") or {}
    minimum = positive_int(first.get("minimum_backlog_slots"), "minimum native backlog slots")
    explicit = policy["backlog_slots"]
    baseline_slots = explicit if explicit is not None else minimum

    def ask(slots):
        if first.get("requested_slots") == slots:
            return first
        receipt = invoke("plan", workers=workers, slots=slots)
        records.append(receipt)
        if receipt["status"] != "complete":
            raise ProbeError(receipt.get("reason") or "Native stream geometry planning failed")
        return receipt["configuration"]["stream_plan"]

    baseline = ask(baseline_slots)
    if (baseline_slots < minimum or baseline.get("fits_sample_budget") is not True
            or baseline.get("fits_spool_budget") is not True):
        raise ProbeError("The declared budgets cannot hold the smallest selected complete native replay")
    if explicit is not None:
        return baseline | {"selection": "explicit_native_horizon"}
    carryover = positive_int(baseline["maximum_startup_carryover_slots"], "native carryover bound",
                            allow_zero=True)
    # Use the smallest declared native interval longer than every possible
    # startup carryover when the sample/spool budget can support it.
    target = max(minimum, 3 * carryover)
    while 2 * target // 3 - target // 3 <= carryover:
        target += 1
    if target > minimum and target <= 4096:
        extended = ask(target)
        if extended["fits_sample_budget"] and extended["fits_spool_budget"]:
            return extended | {"selection": "smallest_horizon_beyond_maximum_carryover"}
    return baseline | {"selection": "smallest_complete_replay_with_observed_carryover_gate"}


def _pilot_ratios(receipt, counts, slots):
    digest = (receipt["observation"] or {}).get("input_sha256")
    if not is_sha256(digest):
        raise ProbeError("Native allocation pilot lacks a shared spool observation identity")
    result = {}
    replays = list(receipt["result"].get("allocation_pilots", []))
    worker = receipt["allocation"].get("workers")
    if worker in counts and all(item["allocation"]["workers"] != worker for item in replays):
        replays.append({"allocation": receipt["allocation"], "stream": receipt["result"]["stream"],
                        "backlog_interval": receipt["result"]["backlog_interval"]})
    seen = set()
    for replay in replays:
        worker = positive_int(replay["allocation"]["workers"], "pilot workers")
        if worker not in counts or worker in seen:
            raise ProbeError("Native pilot reported an unplanned allocation")
        seen.add(worker)
        stream, interval = replay["stream"], replay["backlog_interval"]
        if stream.get("verified_input_sha256") != digest:
            raise ProbeError("Native pilot allocation did not replay the identical spool")
        if (stream.get("work_complete") is True and stream.get("committed_slots") == slots
                and interval.get("complete") is True and interval.get("process_cpu_seconds") is not None):
            result[worker] = finite_number(interval["service_seconds_per_baseband_second"],
                                           "pilot ready-backlog service", nonnegative=True)
    return result


def _declared_fine_work(receipt, stratum):
    result = receipt["result"]
    observation = receipt["observation"] or {}
    if (receipt["status"] != "complete" or result.get("work_complete") is not True
            or result.get("coordinate_scope") != "declared_public_work_coordinate"
            or observation.get("channel_scope") != "independently_seeded_native_thermal_noise_at_local_crop"
            or result.get("receiver_decision_eligible") is not False
            or result.get("complete_receiver_service") is not False
            or named_phases(receipt, "coarse_selection")):
        raise ProbeError(receipt.get("reason") or "Native diagnostic did not cover its declared non-admitting fine work")
    if stratum == "noise":
        return fine_continuation(receipt), None
    return direct_fine(receipt, stratum)


def worker_plan(hardware, physical, *, threads=None, full_logical_inference=None):
    positive_int(hardware, "native effective hardware threads")
    if threads is not None:
        positive_int(threads, "explicit workers")
        if full_logical_inference is not None:
            raise ProbeError("--threads and --full-logical-inference are mutually exclusive")
        return {"pilots": [], "inference_workers": threads, "mode": "explicit"}
    known_physical = min(positive_int(physical, "physical cores"), hardware) if physical else None
    pilots = list(dict.fromkeys([1, *([known_physical] if known_physical else []), hardware]))
    workers = hardware if full_logical_inference is True else None if full_logical_inference is False else 2 * hardware
    return {"pilots": pilots, "physical_workers": known_physical, "inference_workers": workers,
            "mode": "full_logical" if full_logical_inference is True else
                    "explicit_pilot_selection" if full_logical_inference is False else "automatic_2H"}


def measure_complete_case(case, *, invoke, plan, worker_allocation, family_size,
                          policy, sustained_allocation_probe=False, calibration_stage=None):
    """Measure only declared native phases and preserve every launched receipt."""
    receipts = {"strata": {name: [] for name in STRATA}, "backlog": [], "codec": [], "stream_plans": [],
                "work_only": {name: [] for name in STRATA if name != "active"}}
    row = {"case": case, "plan": plan, "measurements": receipts,
           "nominal": _ratio(), "conservative": _ratio(), "same_stream": _ratio(),
           "status": "unresolved", "unresolved_work": [], "worker_allocation": dict(worker_allocation)}
    missing = row["unresolved_work"]
    if plan["status"] != "complete":
        missing.append(plan.get("reason") or "native profile unavailable")
        return row
    configuration = plan["configuration"]
    fixed = configuration.get("fixed_active_seconds", configuration.get("fixture_active_seconds"))
    if (fixed != policy.get("requested_fixed_active_seconds") or configuration.get("scope") != SCOPE
            or configuration.get("target_cn0_db_hz") != policy.get("target_cn0_db_hz")):
        missing.append("native plan is not the actual configured-calendar case")
        return row
    row["duration"] = {"policy": "native_target_model" if fixed is None else "explicit_fixed",
                       "requested_fixed_active_seconds": fixed,
                       "target_cn0_db_hz": configuration.get("target_cn0_db_hz"),
                       "planned_bit_seconds": configuration.get("bit_seconds")}
    horizon = configuration.get("recurring_horizon") or {}
    try:
        if horizon.get("slots") != policy["recurring_public_horizon_slots"]:
            raise ProbeError("native plan does not cover the declared recurring public horizon")
        cadence = finite_number(horizon.get("shortest_interval_seconds"), "shortest public cadence", nonnegative=True)
        largest = positive_int(horizon.get("largest_window_samples"), "largest public window")
        recurring = positive_int(horizon.get("largest_window_slot"), "largest window slot", allow_zero=True)
        rate = finite_number(configuration.get("sample_rate_hz"), "logical sample rate", nonnegative=True)
        if not cadence or not rate:
            raise ProbeError("native plan cadence and sample rate must be positive")
    except (KeyError, ValueError) as error:
        missing.append(str(error))
        return row
    window_seconds = largest / rate
    workers = worker_allocation["inference_workers"]
    stream_plan = None
    pilot_counts = worker_allocation["pilots"]
    allowed = allocation(plan)["slot_owners"] if policy["native_call_limits"].get("slot_owners") else 1
    eligible_pilots = [count for count in pilot_counts if count >= allowed]
    pilot_ratios = {}
    if calibration_stage is not None:
        stages = ("noise",)
    else:
        stages = STRATA
        try:
            stream_plan = _stream_plan(invoke, plan, policy, workers, receipts["stream_plans"])
            row["stream_plan"] = stream_plan
        except (KeyError, ProbeError) as error:
            missing.append(f"same-stream replay preflight: {error}")
        if pilot_counts and worker_allocation["physical_workers"] is None:
            missing.append("effective physical-core topology")
        if workers is None:
            try:
                if not stream_plan or not eligible_pilots:
                    raise ProbeError("Native pilot has no admissible replay horizon or allocations")
                slots = stream_plan["requested_slots"]
                pilot = invoke("backlog", seed=74021, slots=slots,
                               pilot_workers=",".join(map(str, eligible_pilots)))
                receipts["backlog"].append(pilot)
                pilot_ratios = _pilot_ratios(pilot, eligible_pilots, slots)
            except (KeyError, ProbeError) as error:
                missing.append(f"allocation pilot: {error}")
            if not pilot_ratios or set(pilot_ratios) != set(pilot_counts):
                missing.append("explicit pilot-winner allocation could not be selected")
                return row
            workers = min(pilot_ratios, key=pilot_ratios.get)
            row["worker_allocation"]["inference_workers"] = workers
    stages_seen = {}
    row["partial_acquisition"] = {}
    seed_origin = 81000
    for number, stratum in enumerate(stages):
        active_long = configuration.get("nominal_continuation_eligible") is True
        diagnostic = stratum in ("cold-maximum", "terminal", "erasure") or bool(
            calibration_stage and stratum == "noise" and window_seconds >= 6)
        continued = (stratum == "active" and active_long) or (stratum == "noise" and window_seconds >= 6)
        command = "slot" if stratum in ("active", "noise") and not continued else "continuation"
        stage = {"costs": [], "fine": [], "direct": [], "segments": None, "observations": 0}
        stages_seen[stratum] = stage
        for look in LOOKS if continued else LOOKS[:1]:
            failed = False
            while stage["observations"] < look:
                seed = seed_origin + number * 100 + stage["observations"]
                try:
                    receipt = invoke(command, workers=workers, seed=seed, slot_index=recurring,
                                     stratum=stratum, **({"work_only": True} if diagnostic else {}))
                    receipts["work_only" if diagnostic else "strata"][stratum].append(receipt)
                    if not diagnostic and receipt["status"] != "complete":
                        partial = partial_acquisition(receipt, cadence=cadence)
                        if partial is not None:
                            row["partial_acquisition"].setdefault(stratum, []).append(partial)
                    if receipt["configuration"].get("public_window_samples") != largest:
                        raise ProbeError(receipt.get("reason") or "native probe missed largest recurring geometry")
                    costs = None if diagnostic else slot_costs(receipt)
                    if continued:
                        fine = (_declared_fine_work(receipt, stratum)[0] if diagnostic
                                else fine_continuation(receipt))
                        if stage["fine"] and not _compatible_work(stage["fine"][0], fine):
                            raise ProbeError("native fine geometry changed across independent observations")
                        stage["fine"].append(fine)
                    else:
                        elapsed, work = (_declared_fine_work(receipt, stratum) if diagnostic
                                         else direct_fine(receipt, stratum))
                        if diagnostic:
                            segment_rows = positive_int(work["segment_rows"], "native fine segment size")
                            segments = math.ceil(largest / segment_rows)
                            if stage["segments"] is not None and stage["segments"] != segments:
                                raise ProbeError("native maximum-state segment geometry changed")
                            stage["segments"] = segments
                        stage["direct"].append(elapsed)
                    if costs is not None:
                        stage["costs"].append(costs)
                    stage["observations"] += 1
                except (KeyError, ProbeError) as error:
                    missing.append(f"{stratum}: {error}")
                    failed = True
                    break
            if failed:
                break
            if not continued:
                break
            model = summarize_service([fine["continuation_rates"] for fine in stage["fine"]], family_size=family_size)
            if model["observed_model_log_error_floor"] > math.log1p(MODEL_LIMIT):
                missing.append(f"{stratum}: native four-second cost holdout exceeded 30%")
                break
            if model["model_status"] in SUPPORTED and model["relative_precision_fraction"] <= PRECISION:
                break
        if stratum == "noise" and not diagnostic and not stage["observations"] and window_seconds >= 6:
            # A bounded non-admitting observation is still actionable cost
            # evidence after full public acquisition has been withheld.
            try:
                receipt = invoke("continuation", workers=workers, seed=seed_origin + 100,
                                 slot_index=recurring, stratum="noise", work_only=True)
                receipts["work_only"]["noise"].append(receipt)
                stage["noise_diagnostic"], _ = _declared_fine_work(receipt, "noise")
            except (KeyError, ProbeError) as error:
                missing.append(f"noise work-only diagnostic: {error}")
    row["phase_models"] = {}
    for name, stage in stages_seen.items():
        acquisition = measured_ratio([_acquisition_rate(cost, cadence=cadence)
                                      for cost in stage["costs"]], family_size=family_size) | {
            "scope": "Once-per-slot receiver phases plus continuous logical PCM projection at the measured output rate"}
        if stage["costs"]:
            acquisition["measured_complete_component_lower_bound"] = statistics.fmean(
                sum(cost[name] for name in ACQUISITION) / cadence for cost in stage["costs"])
        if stage["fine"]:
            fine = _continued_component(stage["fine"], window_seconds=window_seconds, cadence=cadence,
                                        family_size=family_size)
        else:
            multiplier = stage["segments"] or 1
            fine = measured_ratio([elapsed * multiplier / cadence for elapsed in stage["direct"]],
                                  family_size=family_size, model_status=(
                                      "measured_conservative_state_envelope" if stage["segments"]
                                      else "measured_fixed_geometry"))
            if name in ("active", "noise") and stage["direct"]:
                fine["measured_complete_component_lower_bound"] = statistics.fmean(stage["direct"]) / cadence
        row["phase_models"][name] = {"acquisition_and_audio": acquisition, "fine": fine}
        if "noise_diagnostic" in stage:
            row["phase_models"][name]["bounded_work_only"] = stage["noise_diagnostic"]
    if calibration_stage is not None:
        noise = row["phase_models"].get("noise", {}).get("fine")
        if noise and noise["model_status"] in SUPPORTED and not missing:
            row["status"] = "stage_calibration_measured"
        return row
    if "active" not in row["phase_models"] or not stages_seen["active"]["costs"]:
        missing.append("complete native stream, quiet/codec, recurrent and alternate fine work")
        actual_cost = max((entry.get("measured_complete_component_lower_bound", 0.)
                           for entry in row["partial_acquisition"].get("active", ())), default=0.)
        partial = _ratio() | {"measured_complete_component_lower_bound": actual_cost}
        row["nominal"] = combine_components({"public_acquisition": partial}, complete_work_covered=False)
        noise = row["phase_models"].get("noise")
        if noise:
            acquisition = _worst_component([noise["acquisition_and_audio"], partial])
            row["conservative"] = combine_components({"acquisition_and_audio": acquisition,
                "fine": noise["fine"], "unmeasured": _ratio()}, complete_work_covered=False)
        if any(row[key]["realtime_status"] == "fail" for key in ("nominal", "conservative")):
            row["status"] = "capacity_failure"
        return row
    whole_ratios, backlog_ratios, controller_ratios, observed_memory = [], [], [], []
    backlog_cpu, total_audio_ratios = [], []
    reacquisition = 0
    stream_looks = policy["stream_looks"] if stream_plan else 0
    for index in range(stream_looks):
        try:
            slots = stream_plan["requested_slots"]
            # One continuous native capture is replayed for all pilot counts.
            # Later independent looks time only the frozen inference allocation.
            pilot_options = ({"pilot_workers": ",".join(map(str, eligible_pilots))}
                             if index == 0 and eligible_pilots and not pilot_ratios else {})
            receipt = invoke("backlog", workers=workers, seed=85000 + index, slots=slots, **pilot_options)
            receipts["backlog"].append(receipt)
            if pilot_options:
                pilot_ratios = _pilot_ratios(receipt, eligible_pilots, slots)
            complete, backlog = _stream_ratios(receipt, slots)
            whole_ratios.append(complete)
            reacquisition += int(receipt["result"].get("configured_calendar_reacquired") is True)
            if backlog is not None:
                backlog_ratios.append(backlog)
                interval = receipt["result"]["backlog_interval"]
                cpu = interval.get("process_cpu_seconds")
                if cpu is not None:
                    backlog_cpu.append(finite_number(cpu, "backlog process CPU time", nonnegative=True)
                                       / interval["wall_seconds"] / allocation(receipt)["hardware_threads"])
            stream = receipt["result"]["stream"]
            observed_memory.append(positive_int(stream["peak_reserved_bytes"], "reserved stream peak"))
            pcm = named_phases(receipt, "audio_frontend")
            if (len(pcm) == 1 and pcm[0]["work"].get("input_rows") == stream["input_rows"]
                    and pcm[0]["represented_seconds"] > 0 and pcm[0]["wall_seconds"] > 0):
                total_audio_ratios.append(complete + pcm[0]["wall_seconds"] / pcm[0]["represented_seconds"])
            controller = named_phases(receipt, "controller_service")
            if (len(controller) == 1 and controller[0]["wall_seconds"] > 0
                    and controller[0]["represented_seconds"] > 0):
                controller_ratios.append(controller[0]["wall_seconds"] / controller[0]["represented_seconds"])
        except (KeyError, ProbeError) as error:
            missing.append(f"complete stream: {error}")
            break
    row["same_stream"] = measured_ratio(total_audio_ratios, family_size=family_size) | {
        "scope": "Measured complete native I/Q replay plus separate full-stream native logical PCM projection",
        "startup_fill_and_drain_included": True, "holdout_seeds": list(range(85000, 85000 + len(whole_ratios))),
        "native_iq_replay": measured_ratio(whole_ratios, family_size=family_size),
        "ready_backlog_interval": measured_ratio(backlog_ratios, family_size=family_size),
        "ready_backlog_hardware_busy_fractions": backlog_cpu,
        "reacquisition_observations": reacquisition,
    }
    row["worker_allocation"]["pilot_backlog_ratios"] = pilot_ratios
    if set(pilot_ratios) != set(pilot_counts):
        missing.append("matched serial, physical-core and full-logical same-stream work")
    if not backlog_ratios:
        missing.append("a predeclared ready-input backlog interval beyond startup carryover")
    if not backlog_cpu:
        missing.append("process CPU utilization during the measured backlog intervals")
    if not reacquisition:
        missing.append("scored quiet rearm and a subsequent configured-calendar message")
    if not controller_ratios:
        missing.append("native controller, ingress and codec service timing outside instrumented waits")
    if not total_audio_ratios:
        missing.append("native logical PCM projection through the complete stream including gaps")
    if total_audio_ratios and len(total_audio_ratios) < 3:
        missing.append("same-stream timing uncertainty beyond the bounded observed point")
    if sustained_allocation_probe and active_long:
        row["sustained_allocation"] = {}
        alternates = list(dict.fromkeys(count for count in (
            worker_allocation.get("physical_workers"), max(worker_allocation["pilots"], default=0))
            if count and count != workers))
        for count in alternates:
            observations = []
            for index in range(3):
                try:
                    receipt = invoke("continuation", workers=count, slot_index=recurring,
                                     seed=seed_origin + index, stratum="active")
                    observations.append(receipt)
                    fine_continuation(receipt)
                except ProbeError:
                    break
            row["sustained_allocation"][str(count)] = observations
    codec_ratios = []
    for index in range(3):
        try:
            codec = invoke("codec", workers=workers, seed=86000 + index)
            receipts["codec"].append(codec)
            result = codec["result"]
            if (codec["status"] != "complete" or result.get("decoded_after_two_quiet") is not True
                    or result.get("scope") != "isolated_native_long_codec_receipts"
                    or result.get("marker_intervals", 0) < 2):
                raise ProbeError(codec.get("reason") or "bounded long-codec measurement unavailable")
            receive = phase_once(codec, "codec_receive")
            wire = positive_int(result.get("wire_bits"), "long-codec wire bits")
            codec_ratios.append(receive["wall_seconds"] / ((wire + 2) * cadence))
        except ProbeError as error:
            missing.append(f"long-message codec: {error}")
            break
    phases = row["phase_models"]
    nominal = phases["active"]
    max_acquisition = _worst_component([phases.get(name, {}).get("acquisition_and_audio", _ratio())
                                        for name in ("active", "noise")])
    worst_fine = _worst_component([phases.get(name, {}).get("fine", _ratio()) for name in STRATA])
    if set(phases) != set(STRATA) or any(phases[name]["fine"]["model_status"] not in SUPPORTED for name in phases):
        missing.append("all five supported native fine strata")
    workspace = None
    if observed_memory:
        workspace_quotes = [configuration["receiver_slot_workspace_bytes"]]
        workspace_quotes.extend(receipt["configuration"]["receiver_slot_workspace_bytes"]
            for stratum in (*receipts["strata"].values(), *receipts["work_only"].values())
            for receipt in stratum if receipt["status"] == "complete")
        workspace = workspace_summary(max(workspace_quotes),
                                      allocation(plan)["memory_limit_bytes"], max(observed_memory))
        row["workspace"] = workspace
    else:
        missing.append("observed stream workspace")
    orchestration = measured_ratio(controller_ratios, family_size=family_size) | {
        "scope": "Native controller wall outside instrumented waits, charged separately in serialized phase costs"}
    row["nominal"] = combine_components(nominal | {"orchestration": orchestration},
                                       complete_work_covered=not missing, workspace=workspace)
    long_codec = measured_ratio(codec_ratios, family_size=family_size) | {
        "scope": "Isolated fixed native RS interval receive work per declared long-codec bit slot"}
    row["conservative"] = combine_components({"acquisition_and_audio": max_acquisition, "fine": worst_fine,
                                               "orchestration": orchestration, "long_codec": long_codec},
                                              complete_work_covered=not missing, workspace=workspace)
    decisions = [row[name]["realtime_status"] for name in ("nominal", "conservative", "same_stream")]
    stream_failed = any(row["same_stream"][key]["realtime_status"] == "fail"
                        for key in ("native_iq_replay", "ready_backlog_interval"))
    if "fail" in decisions or stream_failed:
        row["status"] = "capacity_failure"
    elif decisions == ["pass"] * 3 and not missing:
        row["status"] = "conditional_capacity_pass"
    return row
