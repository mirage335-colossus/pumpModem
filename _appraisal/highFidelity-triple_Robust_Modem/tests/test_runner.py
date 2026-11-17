#!/usr/bin/env python3
"""Focused bounded-runner tests; fake subprocesses never implement channel DSP."""
from __future__ import annotations

import importlib.util
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

RUNNER = Path(__file__).resolve().parents[1] / "run.py"
spec = importlib.util.spec_from_file_location("appraisal_run", RUNNER)
assert spec and spec.loader
runner = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = runner
spec.loader.exec_module(runner)


def valid_campaign() -> dict:
    return {
        "schema_version": 1, "name": "test", "max_wall_seconds": 2,
        "per_trial_wall_seconds": 10, "max_trials": 2, "max_samples": 1000,
        "episodes": [
            {"id": "one", "scenario": "awgn", "mode": "receive", "waveform": "smoke", "seed": 1,
             "cn0_db_hz": 12, "control": "wanted", "stratum": "awgn-fixed"},
            {"id": "two", "scenario": "awgn", "mode": "receive", "waveform": "smoke", "seed": 2,
             "cn0_db_hz": 12, "control": "wanted", "stratum": "awgn-fixed"},
        ],
        "confirmatory_strata": {"awgn-fixed": 2},
    }


def physical_design_episode() -> dict:
    return {"id": "design", "scenario": "hf-ensemble-design", "mode": "plan", "waveform": "reference", "seed": 1,
            "hf_primary_amplitude_hz": 12000, "hf_primary_period_seconds": 271,
            "hf_primary_phase_radians": .57, "hf_secondary_amplitude_hz": 13000,
            "hf_secondary_period_seconds": 1013, "hf_secondary_phase_radians": -1.11,
            "hf_knot_amplitude_hz": 8000, "hf_knot_seconds": 113}


def fake_predictive_diagnostic(row: dict) -> dict:
    result = {"attempted": False, "accepted": False, "training_status": "insufficient_prior_public_gaps",
              "status": "canonical_decision_retained", "legacy_modes": 0, "expanded_modes": 0,
              "frequency_coherent_nodes": 0, "frequency_band_nodes": 0}
    result.update({field: {"first": 0, "stop": 0} for field in
                   ("frequency_first", "frequency_second", "amplitude", "validation", "frequency_first_proposal",
                    "frequency_first_confirmation", "frequency_second_proposal", "frequency_second_confirmation")})
    result.update({field: 0. for field in ("frequency_hz", "frequency_lower_hz", "frequency_upper_hz",
        "frequency_confidence_exclusion_probability", "coherent_interpolation_norm", "band_interpolation_norm",
        "amplitude_magnitude", "validation_line_residual",
        "validation_energy", "validation_threshold", "line_direction_gain_squared", "coherent_residual", "coherent_threshold",
        "legacy_energy", "legacy_threshold", "expanded_energy", "expanded_threshold", "extra_seconds")})
    result.update(raw_quiet_energy=row["quiet_energy"], raw_quiet_threshold=row["quiet_threshold"])
    return result


def fake_refinement_trace(*, full: bool) -> dict:
    work = {field: 0 for field in ("fft_row_stages", "spectrum_cells", "table_lookups", "direct_sweeps", "direct_rows",
                                   "phase_rotations", "trigonometric_evaluations", "likelihood_cells")}
    old, new = [1., 4.], [2., 3.]
    trace = {"enabled": True, "requested": True, "executed": True, "unavailable_reason": "",
        "old_family_weight": .5, "new_family_weight": .5,
        "old_bit_log_e": old, "new_bit_log_e": new,
        "combined_bit_log_e": [max(a, b) + math.log(math.exp(a - max(a, b)) + math.exp(b - max(a, b))) - math.log(2)
                               for a, b in zip(old, new)],
        "old_top_timing_indices": [0], "segmented_top_timing_indices": [0], "selected_timing_indices": [0],
        "shape_count": 100, "nu_count": 2, "nu_step_hz": .001, "work": dict(work), "work_bound": dict(work)}
    if full:
        trace["timing_results"] = [{"timing_index": 0, "maximum_shape": [{"a": 1, "b": 0, "nu": 0}] * 2,
            "maximum_log_lr": [1., 4.], "maximum_frequency_hz": [0., 0.], "proposals": []}]
    return trace


def fake_unavailable_refinement_trace(*, full: bool) -> dict:
    trace = fake_refinement_trace(full=full)
    trace.update(executed=False, unavailable_reason="new_phase_direct_row_cap",
        new_bit_log_e=[None, None], combined_bit_log_e=[value - math.log(2) for value in trace["old_bit_log_e"]],
        old_top_timing_indices=[], segmented_top_timing_indices=[], selected_timing_indices=[],
        timing_ranks=[], timing_results=[], selected_shape_counts=[],
        retained_statistics_workspace_bytes=0, retained_new_prior_mass=0.,
        work_bound_scope="full public timing preflight; unavailable family was not admitted or executed")
    trace["work_bound"]["direct_rows"] = 105780000
    for field in ("shape_count", "nu_count", "nu_step_hz"):
        del trace[field]
    return trace


def fake_phase_filter(search: dict, samples: int, *, sticky=False, bridge=False) -> dict:
    def branch(values):
        high = max(values)
        presence = high + math.log(sum(math.exp(value - high) for value in values)) - math.log(2) + math.log(.25)
        separation = abs(values[1] - values[0])
        return {"bit_log_e": list(values), "weighted_presence_log_e": presence,
                "favored_bit": int(values[1] > values[0]), "bit_log_separation": separation,
                "passes_presence_reference": presence >= search["presence_reference_threshold"],
                "passes_bit_separation_reference": separation >= search["bit_separation_reference"]}
    baseline = search["phase_refinement"]["combined_bit_log_e"]
    walk = [5., 5.]  # A tie is a legitimate component result and favors bit zero.
    held = [-2., 12.]
    correlated = [max(a, b) + math.log1p(math.exp(-abs(a - b))) - math.log(2) for a, b in zip(walk, held)] if sticky else walk
    legacy, edge = correlated, [11., -3.]
    if bridge:
        correlated = [max(a, b) + math.log1p(math.exp(-abs(a - b))) - math.log(2) for a, b in zip(legacy, edge)]
    combined = [max(a, b) + math.log1p(math.exp(-abs(a - b))) - math.log(2) for a, b in zip(baseline, correlated)]
    groups, blocks = (samples + 1919) // 1920, (samples + 127) // 128
    result = {"enabled": True, "executed": True, "phase_bins": 32, "frequency_bins": 33,
        "frequency_step_hz": .0625, "group_seconds": 1, "groups": groups, "gain_radii": [.5, 1],
        "timing_atoms": 39, "timing_prior_mass": 1, "branch_weight_within_supplement": .5,
        "supplement_weight": .25, "input_samples": samples, "input_block_rows": 128, "input_blocks": blocks,
        "stage_wall_seconds": .1, "baseline": branch(baseline), "correlated": branch(correlated), "combined": branch(combined),
        "work": {"correlation_products": 39 * 2 * 33 * blocks, "emission_states": 39 * 4 * 33 * 32 * groups,
                 "transition_edges": 39 * 4 * 32 * 97 * (groups - 1), "workspace_reserved_bytes": search.get("stage_workers", 1) * 131072,
                 "summed_worker_wall_seconds": .2}}
    if sticky:
        result.update(sticky_enabled=True, walk=branch(walk), sticky=branch(held), correlated_prior_weights=[.5, .5],
            transition_probabilities={"walk": {"stay": .5, "step_each": .25, "edge_stay": .75},
                                      "sticky": {"stay": .96875, "step_each": .015625, "edge_stay": .984375}})
        for field in ("correlation_products", "emission_states", "transition_edges"):
            result["work"][field] *= 2
    if bridge:
        bridge_groups = (blocks + 59) // 60
        result.update(bridge_enabled=True, bridge=branch(edge), legacy_correlated=branch(legacy), bridge_prior_weights=[.5, .5],
            bridge_work={"group_seconds": 4, "groups": bridge_groups, "correlation_products": 39 * 2 * 97 * blocks,
                "edge_emissions": 39 * 4 * 97 * 32 * bridge_groups, "forward_terms": 39 * 4 * 97 * 32 * bridge_groups,
                "shared_rotator_values": 97 * 60, "lane_workspace_bytes": 131072, "shared_workspace_bytes": 98304,
                "workspace_reserved_bytes": search.get("stage_workers", 1) * 131072 + 98304,
                "transition_probabilities": {"stay": .5, "step_each": .25, "edge_stay": .75},
                "stage_wall_seconds": .15, "summed_worker_wall_seconds": .25})
    return result


def fake_gain_partitions(physical: dict, sample_rate: float, samples: int) -> dict:
    result = {"used_for_receiver_decisions": False, "template_selected_from_noisy_public_search": True,
              "fixed_template_null_law_applies_after_selection": False, "refined_public_cell": None}
    for name in ("physical_templates", "winning_public_cell", "refined_public_cell"):
        if name not in physical:
            continue
        projection = physical[name] if name == "physical_templates" else physical[name]["projection"]
        rows = []
        for requested in (.25, 1, 4, 16, None):
            block_samples = math.floor(requested * sample_rate + .5) if requested is not None else samples
            hypotheses = [{"rank": h["rank"], "reference_energy": h["template_energy"],
                           "projected_signal_energy": h["clean_projected_energy"],
                           "residual_signal_energy": h["clean_residual_energy"],
                           "retained_signal_fraction": h["clean_coherence_power"],
                           "noisy_projected_energy": h["clean_projected_energy"] + 1,
                           "noisy_log_likelihood_ratio": h["noisy_log_likelihood_ratio"],
                           "noise_only_projected_energy": None, "noise_only_log_likelihood_ratio": None}
                          for h in projection["hypotheses"]]
            wanted = projection["wanted_bit"]
            rows.append({"requested_block_seconds": requested, "block_samples": block_samples,
                         "actual_block_seconds": block_samples / sample_rate,
                         "block_count": (samples + block_samples - 1) // block_samples, "hypotheses": hypotheses,
                         "correct_minus_wrong_projected_signal_energy": hypotheses[wanted]["projected_signal_energy"] - hypotheses[1 - wanted]["projected_signal_energy"],
                         "correct_minus_wrong_noisy_log_likelihood_ratio": hypotheses[wanted]["noisy_log_likelihood_ratio"] - hypotheses[1 - wanted]["noisy_log_likelihood_ratio"],
                         "correct_minus_wrong_noise_only_log_likelihood_ratio": None})
        result[name] = rows
    return result


def gui_varying_episode(**overrides) -> dict:
    return {"id": "varying", "scenario": "hf-gui-native", "mode": "gui-varying",
            "waveform": "reference", "seed": 1, "capture_digest_slot": 2, **overrides}


def repeated_bit_episode(**overrides) -> dict:
    return {"id": "repeat", "scenario": "hf-gui-native", "mode": "gui-repeated-bit",
            "waveform": "reference", "seed": 62013, "noise_seed": 72013,
            "cn0_db_hz": 43, "control": "wanted", "backend": "cpu", "helper": False,
            "fec": "off", "bit_seconds": 2, "repeat_bit": 1,
            "repeat_path_scale": .5, "repeat_path_seed": 62032,
            "gui_spread": "hf-worst", "gui_selective": "each-65", "gui_arrival": "tight",
            "gui_margin": 3, "gui_audio_ppm": 100, "gui_rf_ppm": 1,
            "gui_audio_phase_degrees_sqrt_s": .5, "gui_rf_phase_degrees_sqrt_s": .05,
            "gui_target_cn0_db_hz": 32, "gui_clock_edge": True, **overrides}


def repeated_bit_receipt(episode: dict | None = None) -> tuple[dict, dict]:
    episode = episode or repeated_bit_episode()
    separate = episode.get("repeat_separate_attempts", False)
    q0, horizon = (16, 18) if separate else (6, 8)
    active_indices = {3 * index for index in range(6)} if separate else set(range(6))
    horizon_samples = max(1000, horizon * 100)
    public_slots, slots = [], []
    for index in range(horizon):
        first, stop = index * 100, index * 100 + 90
        quiet_first, quiet_stop = first + 10, stop - 10
        public = {"index": index, "pattern_epoch": 1_800_000_000 + index * 60,
                  "symbol_ordinal": index, "observation": {"first": first, "stop": stop},
                  "quiet_aperture": {"first": quiet_first, "stop": quiet_stop},
                  "evaluator_transmitting": index in active_indices}
        active = index in active_indices
        slots.append({"index": index, "pattern_epoch": public["pattern_epoch"], "symbol_ordinal": index,
                      "first_sample": first, "stop_sample": stop, "quiet_first_sample": quiet_first,
                      "quiet_stop_sample": quiet_stop, "decision": "data" if active else "quiet",
                      "bit": episode["repeat_bit"] if active else None, "complete": True, "all_valid": True,
                      "expected_bit": episode["repeat_bit"] if active else None,
                      "raw_bit_correct": True if active else None, "noise_statistics_valid": not active,
                      "zero_h0_verified": not active})
        public_slots.append(public)
    plan = {"repeated_bit": {"bit": episode["repeat_bit"], "active_slots": 6,
              "path_scale": episode["repeat_path_scale"], "path_seed": episode["repeat_path_seed"],
              "oscillator_seed": episode["seed"], "noise_seed": episode["noise_seed"],
              "thermal_noise_enabled": not episode.get("repeat_thermal_off", False),
              "public_key_fingerprint_sha256": "a" * 64, "used_for_receiver_decisions": False},
            "first_uncontaminated_quiet_slot": q0, "scored_slot_horizon": horizon,
            "sample_rate_hz": 1920, "horizon_samples": horizon_samples,
            "noise_variance": 1.0,
            "max_samples": 2_400 if separate else 1_200,
            "max_wall_seconds": 120, "represented_seconds": horizon_samples / 1920,
            "fits_sample_budget": True, "public_slots": public_slots}
    receipt = {"plan": plan, "slots": slots, "messages": [],
               "channel": {"samples": horizon_samples, "hashed_samples": horizon_samples, "input_sha256": "b" * 64},
               "outcomes": {"receiver_executed": True, "component_only": True,
                   "completion_evaluated": False, "communication_endpoint": "repeated_raw_bit",
                   "actual_scored_slots": horizon},
               "repeated_bit": {"active_slots": 6, "correct_data": 6, "wrong_data": 0,
                   "ambiguous_active_slots": 0, "quiet_active_slots": 0,
                   "longest_no_correct_run": 0, "trailing_quiet_slots": 2,
                   "physical_quiet_complete": True, "ordered_valid_complete": True}}
    if separate:
        plan["repeated_bit"]["separate_attempts"] = True
        plan["wire_bits"] = 1
        receipt["repeated_bit"].update({"separate_attempts": True, "inter_attempt_quiet_slots": 10,
                                        "completed_quiet_pairs": 6})
    return receipt, plan








def fake_gui_varying_diagnostic(selected: int, bounds: dict, *, drift_reserved=True, line_reserved=True) -> dict:
    """Receipt fixture only; all component DSP remains in the native probe."""
    def presence(values):
        peak = max(values)
        return peak + math.log(sum(math.exp(value - peak) for value in values)) - math.log(2)

    weight = 1 / (2 + drift_reserved)
    ordinary_weight = .5 if line_reserved else 1.
    coordinate = {"epoch_offset_seconds": 0., "arrival_seconds": 0., "frequency_hz": 0., "clock_ppm": 0.}
    stages = []
    for raw in ([1., 3.], [-1., 5.]):
        corrected = [value - math.log(4) + math.log(weight * ordinary_weight) for value in raw]
        stages.append({"raw_bit_log_evidence": raw, "prior_corrected_bit_log_evidence": corrected,
            "selected_log_prior_mass": -math.log(4), "public_grid_log_count": math.log(4), "public_frequency_cells": 3,
            "candidates": 1, "basis_rows": 80, "workspace_bytes": 64, "seconds": .01,
            "log_presence": presence(corrected)})
    stages[1].update(blocks=2, block_rows=40, frequency_states=1, status="short_block_partial_bandwidth",
                     diagnostic_admits_data=False, diagnostic_data_bit=None)
    combined = [presence([a, b]) + math.log(2) for a, b in zip(
        stages[0]["prior_corrected_bit_log_evidence"], stages[1]["prior_corrected_bit_log_evidence"])]
    return {"component_complete": True, "public_slot": selected, "observation_sha256": "c" * 64,
        "observation_first_sample": 20, "observation_stop_sample": 100, "observation_samples": 80,
        "all_valid": True, "used_for_receiver_decisions": False,
        "affine": stages[0], "varying": stages[1],
        "combined": {"prior_corrected_bit_log_evidence": combined, "log_presence": presence(combined)},
        "prior": {"branch_weight": weight, "drift_reserved": drift_reserved, "drift_scored": False,
            "line_reserved": line_reserved, "ordinary_weight": ordinary_weight,
            "coherent_gain_status": "plan_unavailable", "coherent_gain_available": False,
            "line_scored": False, "noise_variance": 5., "operational_threshold": 4., "minimum_bit_log_separation": 1.,
            "frequency_lower_hz": -bounds["frequency_bound_hz"], "frequency_upper_hz": bounds["frequency_bound_hz"],
            "atmospheric_allowance_hz": bounds["atmospheric_hz"], "clock_bound_ppm": bounds["clock_bound_ppm"],
            "arrival_lower_seconds": bounds["arrival_low_seconds"], "arrival_upper_seconds": bounds["arrival_high_seconds"]},
        "coarse": {"frequency_points": 2, "arrival_points": 1, "workspaces": 1, "total_tiles": 2,
            "completed_tiles": 2, "workspace_bytes": 64, "fft_precision": "fp32",
            "proposals": [{"coordinate": dict(coordinate), "score": 2., "bit": 1, "rank": 0}],
            "affine_coordinates": [dict(coordinate)], "varying_coordinates": [dict(coordinate)],
            "segmented_timing_coordinates": [], "epoch_timing_coordinates": []},
        "resources": {"workers": 2, "scratch_bound_bytes": 256, "worker_stack_bound_bytes": 64,
            "metadata_bound_bytes": 32, "render_row_bound": 1024}}


def fake_gui_clean_comparison(receipt: dict, chunk: int = 4096) -> dict:
    diagnostic = receipt["hf_gui_varying_diagnostic"]
    scores = {key: json.loads(json.dumps(diagnostic[key])) for key in ("affine", "varying", "combined")}
    keys = [{"role": "receiver", "coarse": None, "clean": scores, "noisy": None}]
    if receipt["control"] == "wrong-key":
        coarse = json.loads(json.dumps(diagnostic["coarse"]))
        # A different key is permitted to select different noisy coordinates.
        coarse["affine_coordinates"][0]["frequency_hz"] = .125
        coarse["varying_coordinates"][0]["frequency_hz"] = .125
        keys.append({"role": "transmitter", "coarse": coarse,
                     "clean": json.loads(json.dumps(scores)), "noisy": json.loads(json.dumps(scores))})
    count = len(keys)
    quiet = receipt["control"] == "noise"
    return {"schema_version": 1, "used_for_receiver_decisions": False,
        "transmitter_key_evaluator_only": True, "public_acquisition_input": "noisy_observation",
        "within_key_same_coordinates_for_clean_noisy": True, "coarse_searches": count,
        "clean_input_sha256": "a" * 64, "clean_observation_sha256": "b" * 64,
        "clean_hashed_samples": receipt["plan"]["horizon_samples"],
        "clean_observation_hashed_samples": diagnostic["observation_samples"],
        "clean_channel_noise_enabled": False, "noisy_clean_validity_equal": True,
        "quiet_control_clean_is_zero": True if quiet else None,
        "clean_energy_sample_sum": 0. if quiet else 2., "difference_energy_sample_sum": 80.,
        "wanted_bit": None if quiet else 1, "keys": keys,
        "resources": {"coarse_workspace_bound_bytes": 128, "fine_pair_count": 2 * count,
            "key_render_row_bounds": [1024] * count, "varying_render_row_bound": 2048 * count,
            "varying_render_rows": 2 * count * diagnostic["varying"]["basis_rows"],
            "selected_capture_bytes": 16 * diagnostic["observation_samples"], "extra_chunk_bytes": 17 * chunk,
            "clean_source_working_bytes": 1024, "native_source_channel_bound_bytes": 65 * 2**20}}


def fake_private_clean_trace(result: dict, physical: dict, search: dict, samples: int, frequency: list[int]) -> dict:
    """Small certificate fixture; this summarizes supplied states and performs no channel/DSP search."""
    groups, blocks = len(frequency), (samples + 127) // 128
    search["coordinates"][result["timing_index"]]["template_energy"] = [10., 10.]
    normalization = math.sqrt(physical["physical_templates"]["noise_free_energy"]) * math.sqrt(10.)
    real = result["optimal_clean_correlation_real"]
    phase = [0]
    for k in frequency[:-1]:
        phase.append((phase[-1] + 2 * k) % 32)
    steps = [b - a for a, b in zip(frequency, frequency[1:])]
    jumps = [x for x in steps if x]
    dwell = longest = 1
    for step in steps:
        dwell = 1 if step else dwell + 1
        longest = max(dwell, longest)
    row = {"real_correlation": real, "clean_coherence_power": min(1., (real / normalization) ** 2),
        "frequency_bins": frequency, "phase_bins": phase, "jumps": len(jumps), "up_jumps": steps.count(1),
        "down_jumps": steps.count(-1), "adjacent_reversals": sum(a * b == -1 for a, b in zip(steps, steps[1:])),
        "successive_jump_reversals": sum(a * b == -1 for a, b in zip(jumps, jumps[1:])),
        "edge_stays": sum(step == 0 and abs(k) == 16 for k, step in zip(frequency, steps)),
        "longest_dwell_groups": longest, "reversals_below_half_bin_carrier_change": 0,
        "effective_phase_rms_radians": .5, "effective_aligned_phase_rms_radians": .25,
        "carrier_phase_rms_radians": .5, "carrier_aligned_phase_rms_radians": .25,
        "carrier_frequency_rms_hz": .1, "carrier_frequency_4_group_rms_hz": .05 if samples // 1920 >= 4 else None}
    for prefix, stay, jump, edge in (("walk", .5, .25, .75), ("sticky", .96875, .015625, .984375)):
        row[prefix + "_log_prior"] = -math.log(1056) + sum(math.log(jump if step else edge if abs(k) == 16 else stay)
                                                           for k, step in zip(frequency, steps))
    penalty = normalization / (64 * groups)
    tolerance = 128 * sys.float_info.epsilon * (groups + 1) * normalization
    target, objective = math.sqrt(.99) * real, real - penalty * len(jumps)
    return {"schema_version": 1, "workspace_reserved_bytes": 2 * 1056 * (groups - 1) + 262144,
        "analytic_carrier_points": blocks + groups + 1, "extra_state_evaluations": 1056 * groups,
        "extra_transition_edges": 32 * 97 * (groups - 1), "predecessor_labels": 2 * 1056 * (groups - 1),
        "penalty_correlation_units": penalty, "certificate_tolerance_correlation": tolerance,
        "target_projection_power_fraction_of_best_real": .99, "target_real_correlation": target,
        "penalized_objective_correlation": objective,
        "minimum_jumps_lower_bound": max(0, math.ceil((target - objective - tolerance) / penalty)),
        "target_reached": True, "carrier_frequency_total_variation_hz": 0., "carrier_frequency_rms_step_hz": 0.,
        "carrier_reversal_count": 0, "carrier_reference_scope": "analytic simulator carrier; no envelope delay or noise",
        "maximum_projection": json.loads(json.dumps(row)), "penalized_projection": json.loads(json.dumps(row))}


def fake_private_bridge_support(blocks: int, correlations=(6., 8., 7.)) -> dict:
    groups = (blocks + 59) // 60
    return {"schema_version": 1, "used_for_receiver_decisions": False, "phase_invariant": "p-4*k modulo 8",
        "group_seconds": 4, "groups": groups, "workspace_reserved_bytes": 262144,
        "correlation_products": 159 * blocks, "max_plus_terms": (97 + 291 + 159) * 32 * groups,
        **{name: {"edge_terms_per_phase": edges, "real_correlation": value, "clean_coherence_power": min(1., value * value / 100)}
           for (name, edges), value in zip((("original", 97), ("phase_relaxed", 291), ("rate_relaxed", 159)), correlations)}}


def add_fake_fullwave_receipt(receipt: dict) -> None:
    """Native-shaped qualification metadata, with no electromagnetic or modem computation."""
    model = {"port": "reflection", "carrier_hz": 3400000, "sample_rate_hz": 1920, "occupied_half_band_hz": 768,
        "transition_first_seconds": 60, "transition_seconds": 120, "minimum_source_seconds": 540,
        "interpolation": "phase_factored_output_time_half_cosine", "modelling_delay_samples": 32,
        "taps": 129, "first_lag_samples": 1, "last_lag_samples": 129, "filter_halo_samples": 128,
        "maximum_rf_solves": 32070, "preparation_workspace_bound_bytes": 1048576}
    plan = receipt["plan"]
    source_stop = 480 + 13 * 64 * 1920
    first_quiet = source_stop + 129
    horizon = first_quiet + 2 * 64 * 1920
    slots = [{"index": i, "evaluator_transmitting": i < 13,
              "observation": {"first": 480 + i * 122880 if i < 13 else first_quiet + (i - 13) * 122880,
                              "stop": 480 + (i + 1) * 122880 if i < 13 else first_quiet + (i - 12) * 122880}}
             for i in range(15)]
    executed = receipt["mode"] != "plan"
    plan.update(channel_renderer="hf_fullwave_reflection", fullwave_reflection=dict(model),
        source_stop_sample=source_stop, horizon_samples=horizon, represented_seconds=horizon / 1920,
        channel_tail_seconds=129 / 1920, wire_bits=13, first_uncontaminated_quiet_slot=13,
        scored_slot_horizon=15, planned_uncontaminated_quiet_slots=2, public_slots=slots,
        public_bounds={"arrival_low_seconds": 1 / 1920, "arrival_high_seconds": 129 / 1920,
                       "receiver_filter_halo_samples": 128},
        cn0_reference_plane="normalized incident DATA before reflected-port loss and fixed additive receiver noise",
        noise_variance=1920 / 10 ** (plan["cn0_db_hz"] / 10),
        fullwave_reflection_source_band_checked=executed, fullwave_reflection_table_built=executed)
    receipt["scenario"] = {"channel": {"renderer": "hf_fullwave_reflection", "fullwave_reflection": dict(model)}}
    if not executed:
        return
    full = {**model, "table_sha256": "e" * 64,
        "table_digest_scope": "both ports in reflection/transmission order, ascending profile knots and integer lags, little-endian complex binary64 taps",
        "table_qualified": True, "witnesses_qualified": True, "rf_solves": 32070,
        "knot_profiles_checked": 65, "holdout_profiles_checked": 128,
        "maximum_profile_holdout_absolute_error": [.001, .002],
        "maximum_witness_frequency_absolute_error": [.001, .002],
        "maximum_witness_mesh_absolute_error": [.001, .002], "maximum_discarded_energy_fraction": [1e-8, 2e-8],
        "maximum_frozen_flux_excess": .001, "retarded_peak_error_bound": .003,
        "carrier_flight_phase_bound_radians": .15, "table_workspace_bytes": 300000,
        "stream_workspace_bytes": 4096, "preparation_seconds": 1.2,
        "source_minimum_frequency_hz": -384., "source_maximum_frequency_hz": 384.,
        "regions": [{"name": name, "samples": samples, "incident_energy_sample_sum": incident,
                     "reflected_energy_sample_sum": incident / 10}
                    for name, samples, incident in (("before", 115232, 10.), ("transition", 230400, 20.),
                                                    ("after", horizon - 345632, 70.))]}
    receipt.setdefault("channel", {}).update(samples=horizon, hashed_samples=horizon, source_samples=source_stop,
        contains_signal_energy=True, contains_source_energy=True, input_sha256="f" * 64,
        tail_seconds=129 / 1920, maximum_delay_seconds=1 / 1920, fullwave_reflection=full,
        workspace_bytes=1048576 + 500, input_energy_sample_sum=100., signal_energy_sample_sum=10.)
    receipt["timing"] = {"channel_preparation_seconds": 1.3, "wall_seconds": 2.,
                         "isolated_receiver_service_measured": False, "capacity_qualified": False}
    if receipt["control"] == "noise":
        for slot in slots:
            slot["evaluator_transmitting"] = False
        for region in full["regions"]:
            region.update(incident_energy_sample_sum=0., reflected_energy_sample_sum=0.)
        receipt["channel"].update(contains_source_energy=False, contains_signal_energy=False,
                                  input_energy_sample_sum=0., signal_energy_sample_sum=0.)


def add_fake_magnetoionic_receipt(receipt: dict) -> None:
    """Deterministic native-shaped receipt for strict model/energy gate tests."""
    scenario_id = receipt["requested_scenario"]
    component, transition_first, transition_seconds, spread = runner.MAGNETOIONIC_REFLECTION[scenario_id]
    parallel=scenario_id.startswith("hf-magnetoionic-parallel-")
    intervals,solves=(64,12039) if parallel else (16,3444)
    model = json.loads(json.dumps(runner.MAGNETOIONIC_DESCRIPTOR))
    if parallel:
        model.update(physical_path="parallel_upper",magnetic_field_tesla=[0.,0.,50e-6],
                     profile_intervals=intervals,maximum_rf_solves=solves)
    charge, mass = 1.602176634e-19, 9.1093837139e-31
    gyro = charge*50e-6/(2*math.pi*mass)
    cutoff = math.sqrt(1+(1 if parallel else -1)*gyro/3400000)*3400000
    for q, factor in (("profile_q0", .9999), ("profile_q1", 1.0001)):
        model[q].update(f_peak_plasma_frequency_hz=cutoff*factor)
    model.update(receive_component=component,
                 receive_polarization="fixed_laboratory_x" if component == 0 else "fixed_laboratory_y",
                 transition_first_seconds=transition_first, transition_seconds=transition_seconds,
                 minimum_complete_source_seconds=transition_first+transition_seconds)
    plan = receipt["plan"]
    fs = 1920
    bit_samples = 133632
    source_stop = max(math.ceil((transition_first+transition_seconds+.25)*fs), 17*bit_samples)
    quiet = max(13, math.ceil((source_stop+81-480)/bit_samples))
    count = quiet+2
    horizon = 480+count*bit_samples
    slots = [{"index": index, "evaluator_transmitting": index < 13,
              "observation": {"first": 480+index*bit_samples,
                              "stop": 480+(index+1)*bit_samples}}
             for index in range(count)]
    for slot in slots[quiet:]:
        slot["evaluator_transmitting"] = False
    executed = receipt["mode"] != "plan"
    plan.update(channel_renderer="hf_magnetoionic_reflection",
                magnetoionic_reflection=dict(model),
                magnetoionic_reflection_source_band_checked=executed,
                magnetoionic_reflection_table_built=executed,
                channel_tail_seconds=81/fs,
                channel_tail_seconds_scope="complete causal reflected Jones-column FIR support, from first input through the last retained lag",
                cn0_reference_plane="normalized incident DATA before physical reflection and fixed antenna projection; fixed additive receiver noise",
                receiver_dispersive_model="native_sparse_delay_anchors" if spread else "native_single_ray",
                physical_kernel_supplied_to_receiver=False,
                source_stop_sample=source_stop,horizon_samples=horizon,represented_seconds=horizon/fs,
                wire_bits=13,first_uncontaminated_quiet_slot=quiet,scored_slot_horizon=count,
                planned_uncontaminated_quiet_slots=2,public_slots=slots,
                public_bounds={"arrival_low_seconds":1/fs,"arrival_high_seconds":81/fs,
                               "receiver_filter_halo_samples":80,"spread_seconds":spread,
                               "time_varying":True,"frequency_bound_hz":0,"atmospheric_hz":1.},
                noise_variance=fs/10**(plan["cn0_db_hz"]/10))
    receipt["scenario"]={"channel":{"renderer":"hf_magnetoionic_reflection",
                                     "magnetoionic_reflection":json.loads(json.dumps(model))}}
    if not executed:
        return
    errors=[{"maximum":.001,"profile_q":.5,"offset_hz":-300.},
            {"maximum":.002,"profile_q":.75,"offset_hz":300.}]
    full={**model,
        "table_sha256":"c"*64,
        "table_digest_scope":"reflected laboratory x/y coefficients for fixed x launch, ascending component/profile knot/integer lag, little-endian complex binary64 taps; interpolation additionally bound by source identity",
        "qualification_scope":"sampled frozen vector RF/profile and mesh witnesses; finite-FIR retarded coefficient bound; no evolving-Maxwell, 3D scattering or field calibration",
        "table_qualified":True,"witnesses_qualified":True,"rf_solves":solves,
        "knot_profiles_checked":intervals+1,"holdout_profiles_checked":intervals*2,
        "knot_grid_error":errors,"knot_offgrid_error":errors,"profile_holdout_error":errors,
        "witness_frequency_error":errors,"witness_mesh_error":errors,
        "scale_diagnostic_scope":"occupied band raw independent solver amplitude versus approximation; no relative-error gate or gain normalization",
        "maximum_discarded_energy_fraction":[1e-8,2e-8],
        "maximum_frozen_reflected_power_excess":.001,"minimum_longitudinal_permittivity":.38,
        "maximum_solver_output_power":.98,
        "minimum_carrier_power_xy_total":[.1,.12,.3],
        "maximum_carrier_power_xy_total":[.4,.45,.8],
        "retarded_peak_error_bound":[.003,.004],
        "table_workspace_bytes":1536000,"stream_workspace_bytes":4096,
        "preparation_seconds":2.,"source_minimum_frequency_hz":-350.,"source_maximum_frequency_hz":350.}
    scale_row={"samples":40,"exact_zero_raw":1,"absolute_error_at_least_raw":2,
        "minimum_raw_amplitude":{"raw_amplitude":0.,"relative_error":None,"absolute_error":0.,"q":.5,"rf_offset_hz":-300.},
        "maximum_relative_error":{"raw_amplitude":.001,"relative_error":3.4,"absolute_error":.0034,"q":.5,"rf_offset_hz":-300.},
        "raw_at_maximum_absolute_error":{"raw_amplitude":.002,"relative_error":2.,"absolute_error":.004,"q":.5,"rf_offset_hz":-300.}}
    counts=(703,1395,27) if parallel else (187,352,33)
    for field,count in zip(("knot_offgrid_scale","profile_holdout_scale","witness_mesh_scale"),counts):
        scale= dict(scale_row,samples=count)
        full[field]=[json.loads(json.dumps(scale)),json.loads(json.dumps(scale))]
    if parallel:
        full["scale_diagnostic_scope"]="occupied band raw independent solver amplitude versus approximation; scale diagnostics add no relative-error gate or gain normalization; the separate original-null witness has a point-specific guard"
        guarded=.0001+.00001
        full["original_null_witness"]={"q":(1+math.sqrt(3)/2)/16,"rf_offset_hz":-118.7,
            "witness_meshes_m":[10.,5.,2.5],"fine_amplitude_xy":[.002,.2],"fir_amplitude_xy":[.00205,.2],
            "fine_vector_power":.002**2+.2**2,"fir_to_10m_absolute_error_xy":[.0001,.00001],
            "mesh_10m_to_2p5m_absolute_difference_xy":[.00002,.000001],
            "mesh_5m_to_2p5m_absolute_difference_xy":[.00001,.000001],
            "fir_to_2p5m_absolute_error_xy":[.0001,.00001],"x_guarded_absolute_error":guarded,
            "x_guarded_relative_error":guarded/.002,"x_guarded_power_interval":[(.002-guarded)**2,(.002+guarded)**2],
            "x_local_relative_tolerance":.1,"x_resolved":True,
            "scope":"separate original near-null witness; sampled mesh differences and a point-specific practical interpolation guard, not a uniform physical error bound"}
    first=transition_first*fs+32
    finish=first+transition_seconds*fs
    names=("before","transition","after")
    counts=(first,finish-first,horizon-finish)
    incident=(10.,20.,70.)
    x=(2.,3.,4.); y=(2.,3.,4.)
    full["regions"]=[]
    for i,name in enumerate(names):
        ex,ey=(x[i],y[i]) if receipt["control"]!="noise" else (0.,0.)
        en=incident[i] if receipt["control"]!="noise" else 0.
        full["regions"].append({"name":name,"samples":counts[i],
            "incident_energy_sample_sum":en,"reflected_x_energy_sample_sum":ex,
            "reflected_y_energy_sample_sum":ey,"reflected_vector_energy_sample_sum":ex+ey,
            "selected_antenna_energy_sample_sum":ex if component==0 else ey})
    signal=0. if receipt["control"]=="noise" else (sum(x) if component==0 else sum(y))
    input_energy=0. if receipt["control"]=="noise" else sum(incident)
    report={"samples":horizon,"hashed_samples":horizon,"source_samples":source_stop,
        "workspace_bytes":2097152+1,"contains_source_energy":input_energy>0,"contains_signal_energy":signal>0,
        "input_sha256":"d"*64,"input_energy_sample_sum":input_energy,"signal_energy_sample_sum":signal,
        "tail_seconds":81/fs,"maximum_delay_seconds":1/fs,"magnetoionic_reflection":full}
    receipt["channel"]=report
    receipt["timing"]={"channel_preparation_seconds":2.1,"wall_seconds":4.,
                       "isolated_receiver_service_measured":False,"capacity_qualified":False}
    if receipt["mode"]=="oracle":
        end=134112;rendered=end+81
        plan.update(complete_transition_span_required=False,oracle_physical_channel_instances=3,
            oracle_horizon_samples=rendered,
            magnetoionic_diagnostic_scope="isolated single DATA bit under the known vector-FIR operator; neither a same-input full-message replay nor a complete dynamic excursion")
        report["samples"]=rendered;report["source_samples"]=end
        del report["hashed_samples"];del report["input_sha256"]
        full["regions"]=[{"name":name,"samples":count,"incident_energy_sample_sum":en,
            "reflected_x_energy_sample_sum":ex,"reflected_y_energy_sample_sum":ey,
            "reflected_vector_energy_sample_sum":ex+ey,"selected_antenna_energy_sample_sum":ex if component==0 else ey}
            for name,count,en,ex,ey in (("before",min(first,rendered),100.,9.,9.),
                ("transition",max(0,min(finish,rendered)-first),0.,0.,0.),("after",max(0,rendered-finish),0.,0.,0.))]
        flags={"same_input_full_message_replay":False,"complete_dynamic_excursion_required":False,"field_sensitivity_bound":False}
        receipt["oracle"].update(flags,
            kind="isolated single DATA bit, equal-prior binary known vector-FIR operator and synthetic white Gaussian noise",
            includes_acquisition_or_completion=False,finite_blocklength_capacity_bound=False,realization_normalized=False,
            samples=rendered,source_stop_sample=end,signal_and_tail_horizon_samples=rendered,capture_preroll_samples=480,
            channel_clock_origin_sample=0,hypotheses_share_channel_seed=True,channel_tail_seconds=81/fs,
            physical_table_preparation_seconds=6.,physical_table_rf_solves=3*solves)
        for field in ("magnetoionic_channel_hypothesis_0","magnetoionic_channel_hypothesis_1","magnetoionic_channel_noisy"):
            receipt["oracle"][field]=json.loads(json.dumps(report))
        receipt["presence_oracle"]={**flags,"kind":"isolated single DATA bit under a known vector-FIR operator versus synthetic white Gaussian noise"}
        receipt["outcomes"]={"receiver_executed":False,"component_only":True,"completion_evaluated":False,"correct_complete":False}
        receipt["timing"]={"wall_seconds":7.,"capacity_qualified":False}
        del receipt["channel"]


def add_fake_gannon_receipt(receipt: dict) -> None:
    hour,offset=runner.GANNON_REPLAY[receipt["requested_scenario"]]
    measured=runner._gannon_trace_descriptor(hour,offset)
    measured.update(raw_mean_power=runner.GANNON_RECORDS[hour][3]/36000,
                    normalization_gain=math.sqrt(72000/sum(record[3] for record in runner.GANNON_RECORDS.values())))
    receipt["scenario"]={"physical_level_connected":False,
        "channel":{"rf_hz":10000000,"path_count":1,"paths":[{"power":1.,"delay_seconds":.001,
            "hf":{"measured_offset_seconds":offset,"measured_envelope":measured}}]},
        "numerical_adequacy":{"qualification_horizon_seconds":3599.9-offset,
            "empirical_validation":False,"empirical_record_provenance_verified":True,
            "empirical_channel_calibration":False,
            "scope":"finite observed-voltage Cartesian interpolation on the verified stored UTC grid; recorded noise becomes signal-proportional"}}
    receipt["plan"].update(channel_renderer="observed_voltage_multiplicative_replay",
        cn0_reference_plane="incident normalized modem waveform before jointly scaled observed-voltage multiplication; independent synthetic white receiver noise",
        measured_noise_is_multiplicative=True,field_noise_control_qualified=False)
    if receipt["mode"]=="oracle":
        flags={"same_input_full_message_replay":False,"physical_noise_free_channel_known":False,"field_sensitivity_bound":False}
        receipt["oracle"].update(flags,
            kind="isolated single DATA bit, equal-prior binary known observed-voltage multiplier and synthetic added white Gaussian noise",
            conditioning_scope="known deterministic observed-voltage multiplier with added synthetic white noise; the recorded beacon/noise/interference are inseparable",
            includes_acquisition_or_completion=False,finite_blocklength_capacity_bound=False,
            realization_normalized=False,samples=100)
        receipt["presence_oracle"]={**flags,
            "kind":"isolated single DATA bit under a known observed-voltage multiplier versus synthetic added white Gaussian noise",
            "conditioning_scope":"the recorded beacon/noise/interference are inseparable; no same-input full-message or physical noise-free channel claim"}
    elif receipt["mode"]!="plan":
        noise=receipt["control"]=="noise"
        receipt.setdefault("channel",{}).update(samples=100,contains_signal_energy=not noise,contains_source_energy=not noise,
            input_energy_sample_sum=0. if noise else 10.,signal_energy_sample_sum=0. if noise else 1.)
        physical=receipt.get("hf_physical_diagnostic")
        if physical:
            physical["known_binary"].update(recording_noise_in_multiplier=True,physical_noise_free_channel_known=False,
                field_sensitivity_bound=False)


def fake_completed_receipt(command, *, channel=None, outcomes=None, control=None) -> dict:
    def argument(option, default=None):
        try:
            return command[command.index(option) + 1]
        except ValueError:
            return default

    mode = argument("--mode", "receive")
    backend = argument("--backend", "cpu")
    requested_bit_seconds = float(argument("--bit-seconds", ".001"))
    cn0 = float(argument("--cn0", "32"))
    fec = argument("--fec", "off")
    receipt = {
        "schema_version": 1, "status": "completed",
        "requested_scenario": argument("--scenario", "awgn"),
        "mode": mode, "waveform": argument("--waveform", "smoke"),
        "seed": int(argument("--seed", "1")),
        "control": control or argument("--control", "wanted"),
        "source_identity": {"appraisal_source_sha256": "a" * 64, "native_source_sha256": "b" * 64},
        "plan": {"cn0_db_hz": cn0, "requested_bit_seconds": requested_bit_seconds, "noise_variance": 5.,
                 "fec": fec,
                 "fade_slots": int(argument("--fade-slots", "2")) if argument("--control", "wanted") == "interior-fade" else None,
                 "helper_enabled": "--helper" in command, "sample_rate_hz": 1920,
                 "capture_preroll_seconds": float(argument("--capture-lead-seconds", ".25")),
                 "capture_preroll_override": "--capture-lead-seconds" in command,
                 "hf_trajectory_seed": int(argument("--hf-trajectory-seed")) if "--hf-trajectory-seed" in command else None,
                 "hf_trajectory_seed_override": "--hf-trajectory-seed" in command,
                 "hf_trajectory_offset_seconds": float(argument("--hf-trajectory-offset-seconds", "0")),
                 "hf_trajectory_offset_override": "--hf-trajectory-offset-seconds" in command,
                 "noise_seed": int(argument("--noise-seed", argument("--seed", "1"))),
                 "noise_seed_override": "--noise-seed" in command,
                 "history_reference_delay_seconds": float(argument("--history-reference-delay-seconds", "0")),
                 "hf_phase_bank_requested": "--hf-phase-bank" in command,
                 "hf_public_phase_receiver_requested": "--hf-public-phase-receiver" in command,
                 "hf_physical_diagnostic_requested": "--hf-physical-diagnostic" in command,
                 "hf_phase_refinement_requested": "--hf-phase-refinement" in command,
                 "horizon_samples": 100, "pulse": argument("--pulse", "rrc"),
                 "requested_message": argument("--message", "e"),
                 "message": ("A" if argument("--control", "wanted") in {"interior-fade", "capture-gap"}
                             and argument("--message", "e") == "e" else argument("--message", "e")),
                 "represented_seconds": float(argument("--duration-seconds", "1"))},
        "backend": {"requested": backend, "device": backend if backend in {"cuda", "vulkan"} else "cpu",
                    "executed_gpu_work": backend in {"cuda", "vulkan"}},
    }
    if receipt["requested_scenario"] == "hf-ensemble-design":
        receipt["plan"]["hf_design"] = {field: float(argument("--hf-" + field.replace("_", "-")))
                                        for field in runner.HF_DESIGN_BOUNDS}
    if receipt["requested_scenario"] == "hf-gui-native":
        gui = {field.removeprefix("gui_"): ("--gui-clock-edge" in command if field == "gui_clock_edge" else
                float(argument("--" + field.replace("_", "-"), str(default))) if field in runner.GUI_NUMERIC_BOUNDS else
                argument("--" + field.replace("_", "-"), default)) for field, default in runner.GUI_DEFAULTS.items()}
        low, high = (-.0025, .0025) if gui["arrival"] == "tight" else (-7., 7.07)
        lead = max(.25, -low + .25)
        phase = math.hypot(gui["audio_phase_degrees_sqrt_s"], gui["rf_phase_degrees_sqrt_s"]) * math.pi / 180 / math.sqrt(2)
        realized = {"tx_audio_ppm": gui["audio_ppm"] / 2, "rx_audio_ppm": -gui["audio_ppm"] / 2,
                    "tx_rf_ppm": gui["rf_ppm"] / 2, "rx_rf_ppm": -gui["rf_ppm"] / 2,
                    "timing_offset_seconds": 0., "tx_phase_noise_rad_sqrt_s": phase, "rx_phase_noise_rad_sqrt_s": phase}
        realized["relative_audio_ppm"] = (realized["tx_audio_ppm"] - realized["rx_audio_ppm"]) / (1 + realized["rx_audio_ppm"] * 1e-6)
        gui.update({"realization_passed_to_receiver": False, "profile_source": "canonical_prepare_profile",
                    "fixture_bit_seconds": requested_bit_seconds if "--bit-seconds" in command else None,
                    "actual_cn0_db_hz": cn0, "capture_preroll_seconds": lead,
                    "selective_slow_depth": runner.GUI_SELECTIVE_DEPTHS[gui["selective"]][0],
                    "selective_fast_depth": runner.GUI_SELECTIVE_DEPTHS[gui["selective"]][1],
                    "endpoint_clock_bound_ppm": gui["audio_ppm"] * gui["margin"] / 2,
                    "spread_doppler_hz": runner.GUI_SPREAD_DOPPLER[gui["spread"]] * gui["margin"],
                    "realized": realized})
        receipt["plan"].update({"gui_profile": gui, "channel_renderer": "canonical_gui_streaming_channel",
                               "capture_preroll_seconds": lead,
                               "public_bounds": {"arrival_low_seconds": low, "arrival_high_seconds": high}})
    if mode == "receive":
        receipt["outcomes"] = outcomes or {
            "receiver_executed": True, "component_only": False, "completion_evaluated": True,
            "correct_complete": False, "physical_completions": 0, "actual_scored_slots": 1,
            "premature_completions": 0, "wrong_bits": 0, "false_data": 0,
            "ambiguous_active_slots": 0, "quiet_active_slots": 0, "missed_completion": False,
        }
        if fec != "off":
            receipt["outcomes"].update({"correct_decoded_complete": False,
                                       "communication_endpoint": "correct_decoded_complete", "accepted_messages": 0})
            receipt["messages"] = []
    elif mode == "slot":
        receipt["outcomes"] = outcomes or {
            "receiver_executed": True, "component_only": True, "actual_scored_slots": 1,
            "first_public_observation_complete": True,
            "first_public_observation_correct_data": receipt["control"] == "wanted",
            "correct_complete": False, "completion_evaluated": False,
        }
        receipt["slots"] = [{"index": 0, "decision": "data", "complete": True, "all_valid": True}]
    elif mode == "hf-search":
        selected = int(argument("--search-slot", "0"))
        receipt["plan"].update({"search_slot": selected, "search_timing": argument("--search-timing", "selected"),
            "search_observation": {"first": 20, "stop": 100}, "search_observation_sha256": "c" * 64})
        receipt["hf_public_search"] = {"component_complete": True, "slot_index": selected,
            "receiver_executed": False, "completion_evaluated": False, "capacity_qualified": False,
            "truth_information_passed_to_search": False, "phase_refinement": {"enabled": False}}
        receipt["outcomes"] = {"receiver_executed": False, "public_search_executed": True,
            "component_only": True, "completion_evaluated": False, "correct_complete": False,
            "correct_decoded_complete": False, "actual_scored_slots": 0,
            "physical_completions": 0, "accepted_messages": 0}
        receipt["hf_search_evaluation"] = {"slot_index": selected, "truth_information_passed_to_search": False,
            "transmitted_active": receipt["control"] != "noise",
            "expected_receiver_bit": 1 if receipt["control"] == "wanted" else None}
        receipt["slots"], receipt["messages"] = [], []
    elif mode == "diagnose":
        receipt["outcomes"] = {"component_only": True, "completion_evaluated": False,
                                "correct_complete": False}
        receipt["vlf_diagnostic"] = {"component_complete": True, "phase": "completed",
                                     "capacity_qualified": False, "complete_receiver_service": False}
    elif mode == "gui-varying":
        selected = int(argument("--capture-digest-slot"))
        scope = "shadow_canonical_varying_of_selected_complete_observation"
        receipt["plan"].update(observation_scope=scope, planned_uncontaminated_quiet_slots=0,
            wire_bits=3, horizon_samples=1000, public_slots=[{"index": selected,
                "observation": {"first": 20, "stop": 100}, "evaluator_transmitting": receipt["control"] != "noise"}])
        receipt["plan"]["public_bounds"].update(frequency_bound_hz=20., atmospheric_hz=10., clock_bound_ppm=300.)
        receipt["hf_gui_varying_diagnostic"] = fake_gui_varying_diagnostic(selected, receipt["plan"]["public_bounds"])
        receipt["outcomes"] = {"observation_scope": scope, "receiver_executed": False, "component_only": True,
            "completion_evaluated": False, "correct_complete": False, "correct_decoded_complete": False,
            "public_search_executed": False, "actual_scored_slots": 0, "physical_completions": 0,
            "accepted_messages": 0, "premature_completions": 0, "wrong_bits": 0, "false_data": 0}
        receipt["slots"], receipt["messages"] = [], []
        receipt["channel"] = {"samples": 1000, "hashed_samples": 1000, "input_sha256": "f" * 64}
        if "--hf-gui-clean-diagnostic" in command:
            receipt["plan"].update(hf_gui_clean_diagnostic_requested=True,
                                   native_source_channel_preflight_bound_bytes=2 * 65 * 2**20)
            receipt["hf_gui_varying_diagnostic"]["clean_comparison"] = fake_gui_clean_comparison(
                receipt, int(argument("--chunk", "4096")))
    if mode == "channel" or channel is not None:
        receipt["channel"] = channel or {"contains_signal_energy": True, "samples": 100}
    if mode == "oracle":
        selected = argument("--oracle-slot")
        if selected is not None:
            receipt["plan"]["oracle_slot"] = int(selected)
        receipt["oracle"] = {"probability_error": .5,
                              **({"slot_index": int(selected)} if selected is not None else {})}
        if "--history-reference-delay-seconds" in command or "--hf-phase-bank" in command:
            delay = receipt["plan"]["history_reference_delay_seconds"]
            receipt["hf_history_applicable"] = True
            receipt["hf_history"] = {"reference_delay_seconds": delay, "truth_information_passed_to_receiver": False}
        if "--hf-phase-bank" in command:
            receipt["hf_phase_bank"] = {"model_count": 97, "samples": 100,
                "reference_delay_seconds": receipt["plan"]["history_reference_delay_seconds"],
                "truth_information_passed_to_receiver": False, "truth_information_passed_to_phase_bank": False,
                "receiver_executed": False, "completion_evaluated": False, "capacity_qualified": False}
    if "--hf-physical-diagnostic" in command:
        projection = {"block_count": 1, "samples": 80, "wanted_bit": 1, "gain_prior_variance": 1,
            "noise_free_energy": 10, "hypotheses": [
                {"rank": 1, "template_energy": 10, "clean_projected_energy": 0, "clean_residual_energy": 10,
                 "clean_coherence_power": 0, "noisy_log_likelihood_ratio": 1},
                {"rank": 1, "template_energy": 10, "clean_projected_energy": 10, "clean_residual_energy": 0,
                 "clean_coherence_power": 1, "noisy_log_likelihood_ratio": 4}]}
        receipt["hf_physical_diagnostic"] = {"component_complete": True, "slot_index": int(argument("--search-slot", "0")),
            "observation": {"first": 20, "stop": 100}, "observation_sha256": "c" * 64,
            "truth_information_passed_to_search": False, "receiver_executed": False, "completion_evaluated": False,
            "capacity_qualified": False, "is_theoretical_upper_bound": False, "gain_prior_variance": 1,
            "prefix_samples_rendered_per_twin": 100,
            "physical_templates": {**projection, "template_sha256": ["d" * 64, "e" * 64]},
            "known_binary": {"wanted_bit": 1, "fixed_known_context": True,
                "truth_information_passed_to_search": False, "includes_acquisition_presence_or_completion": False,
                "squared_distance": 20., "distance_over_noise_variance": 4.,
                "observed_correct_vs_wrong_log_likelihood_ratio": 3.,
                "equal_prior_bit_error_probability": .5 * math.erfc(1)},
            "winning_public_cell": {"projection": json.loads(json.dumps(projection)),
                "search_maximum_single_cell_log_lr": 4, "single_cell_log_lr_replay_difference": 0},
            "admission": {"channel_instances": 2, "clean_capture_buffers": 0,
                "retained_public_result_in_process_baseline": True, "process_baseline_bytes": 100,
                "total_memory_limit_bytes": 1000, "future_reserved_bytes": 800}}
    if "--hf-public-phase-receiver" in command:
        maximum = 1 if mode == "slot" else 5
        receipt["plan"]["scored_slot_horizon"] = maximum
        receipt["hf_public_phase_receiver"] = {"experimental": True, "fixed_weight": .25,
            "model_family": "sinusoidal_phase_97", "truth_information_passed_to_search": False,
            "maximum_slot_reports": maximum, "executed_slots": 1,
            "slots": [{"slot_index": 0, "component_complete": True, "phase_refinement": {"enabled": False}}]}
    if "--hf-phase-refinement" in command:
        if mode == "hf-search":
            receipt["hf_public_search"].update(refinement_enabled=True, affine_comparison_evaluated=False,
                selected_timing_renormalized=False, evaluated_timing_atoms=1, phase_refinement=fake_refinement_trace(full=True))
            receipt["hf_public_search"]["families"] = [{}, {}, {"bit_log_e": [1., 4.]}]
        else:
            receipt["hf_public_phase_receiver"].update(refinement_enabled=True,
                model_family="equal_prior_fixed97_and_cartesian_refinement")
            receipt["hf_public_phase_receiver"]["slots"][0].update(evaluated_timing_atoms=1,
                phase_refinement=fake_refinement_trace(full=False))
            receipt["hf_public_phase_receiver"]["slots"][0]["bit_log_e"] = list(
                receipt["hf_public_phase_receiver"]["slots"][0]["phase_refinement"]["combined_bit_log_e"])
        if "--hf-physical-diagnostic" in command:
            physical = receipt["hf_physical_diagnostic"]
            physical.update(refinement_requested=True, refinement_replay_available=True,
                refined_public_cell={"shape_id": {"a": 1, "b": 0, "nu": 0}, "selected_bit": 1, "timing_index": 0, "frequency_hz": 0.,
                    "projection": json.loads(json.dumps(physical["winning_public_cell"]["projection"])),
                    "search_maximum_single_cell_log_lr": 4, "single_cell_log_lr_replay_difference": 0},
                public_refinement_proposals={"not_admitted_evidence": True, "uses_physical_truth": False, "timing_index": 0, "proposals": []})
    digest_slot = argument("--capture-digest-slot", argument("--search-slot", "0") if mode == "hf-search" else None)
    if digest_slot is not None:
        receipt["plan"].update({"capture_digest_slot": int(digest_slot),
            "capture_digest_slot_override": "--capture-digest-slot" in command,
            "capture_digest_first_sample": 20, "capture_digest_stop_sample": 100})
        receipt.setdefault("channel", {}).update({"selected_observation_complete": True,
            "selected_observation_hashed_samples": 80, "selected_observation_sha256": "c" * 64})
    if fec != "off":
        receipt["plan"].update({"wire_bits": 64 if fec == "rs20" else 80,
                                "short_wire_bit_limit": 304 if fec == "rs20" else 400,
                                "content_limit_bytes": 16, "long_form": False})
    if mode in {"receive", "slot", "plan"}:
        policy = "compare_admitted" if "--hf-compare-admitted-models" in command else "bounded_competition"
        receipt["plan"].update(hf_compare_admitted_models_requested="--hf-compare-admitted-models" in command,
                               receiver_evidence_policy=policy)
        if mode in {"receive", "slot"}:
            receipt.setdefault("receiver", {})["evidence_policy"] = policy
    if "--hf-phase-filter-sticky" in command:
        receipt["plan"]["hf_phase_filter_sticky_requested"] = True
    if "--hf-phase-bridge" in command:
        receipt["plan"]["hf_phase_bridge_requested"] = True
    if "--hf-phase-filter" in command:
        receipt["plan"]["hf_phase_filter_requested"] = True
        receipt["plan"].setdefault("public_bounds", {}).update(frequency_bound_hz=1, atmospheric_hz=0)
        search = receipt["hf_public_search"]
        search.update(evaluated_timing_atoms=39, block_rows=128, blocks=1, configured_frequency_bound_hz=1, stage_workers=1,
                      presence_reference_threshold=21.49337868826, bit_separation_reference=5.29330482472)
        search["phase_filter"] = fake_phase_filter(search, 80, sticky="--hf-phase-filter-sticky" in command,
                                                  bridge="--hf-phase-bridge" in command)
    if "--hf-phase-filter-paths" in command:
        receipt["plan"]["hf_phase_filter_paths_requested"] = True
        receipt["plan"]["public_bounds"].update(arrival_low_seconds=.001, arrival_high_seconds=.003, clock_bound_ppm=0)
        receipt["scenario"] = {"channel": {"paths": [{"delay_seconds": .0021, "delay_rate": 0,
            "residual_doppler_hz": 0, "residual_drift_hz_s": 0, "gaussian_width_2sigma_hz": 0}]}}
        receipt["hf_public_search"]["coordinates"] = [{"epoch_offset_seconds": epoch, "arrival_seconds": arrival, "clock_ppm": 0}
            for epoch in range(-6, 7) for arrival in (.001, .002, .003)]
        physical = receipt["hf_physical_diagnostic"]
        physical["admission"].update(pair_workspace_bytes=1024, future_reserved_bytes=200000, total_memory_limit_bytes=300000)
        physical["phase_filter_paths"] = {"used_for_receiver_decisions": False, "uses_physical_truth": True,
            "wanted_bit": 1, "timing_index": 19, "epoch_offset_seconds": 0, "clock_ppm": 0,
            "arrival_seconds": .002, "reference_path_delay_seconds": .0021, "observation_sha256": "c" * 64,
            "clean_coherence_power_lower_bound": .5, "optimal_clean_correlation_real": math.sqrt(50), "gain_radii": [.5, 1],
            "maximum_noisy_log_lr": [4., 5.], "maximum_prior_weighted_noisy_path_log_e": [-4., -3.],
            "conditional_noisy_log_e": [1., 2.], "work": {"correlation_products": 66, "emission_states": 7 * 33 * 32,
                "transition_edges": 0, "workspace_reserved_bytes": 128 * 1024, "block_statistics_reserved_bytes": 40,
                "public_pair_reserved_bytes": 1024}}
    if "--hf-phase-filter-receiver" in command:
        plan = receipt["plan"]
        plan["hf_phase_filter_receiver_requested"] = True
        plan.setdefault("public_bounds", {}).update(frequency_bound_hz=0, atmospheric_hz=1)
        plan["public_slots"] = [{"index": i, "observation": {"first": i * 5000 + 20, "stop": i * 5000 + 4021}}
                                for i in range(plan["scored_slot_horizon"])]
        plan["horizon_samples"] = plan["public_slots"][-1]["observation"]["stop"]
        phase = receipt["hf_public_phase_receiver"]
        phase.update(phase_filter_enabled=True, model_family="equal_prior_refined_baseline_and_correlated_phase_filter")
        if "--hf-phase-filter-sticky" in command:
            phase["phase_filter_sticky_enabled"] = True
        if "--hf-phase-bridge" in command:
            phase["phase_bridge_enabled"] = True
        callback = phase["slots"][0]
        callback.update(evaluated_timing_atoms=39, block_rows=128, blocks=32, configured_frequency_bound_hz=1, stage_workers=1,
                        presence_reference_threshold=21.49337868826, bit_separation_reference=5.29330482472)
        callback["phase_filter"] = fake_phase_filter(callback, 4001, sticky="--hf-phase-filter-sticky" in command,
                                                     bridge="--hf-phase-bridge" in command)
        callback["bit_log_e"] = list(callback["phase_filter"]["combined"]["bit_log_e"])
    if "--service-replay" in command or "--hf-predictive-quiet" in command:
        plan = receipt["plan"]
        episodes = int(argument("--service-episodes", "1"))
        wire = plan.setdefault("wire_bits", 3)
        count = episodes * (wire + 2) + 1
        horizon = count * 128
        intervals = [{"first_public_slot": i * (wire + 2), "stop_public_slot": (i + 1) * (wire + 2)} for i in range(episodes)]
        pattern = "".join("D" if receipt["control"] != "noise" and
            any(row["first_public_slot"] <= i < row["stop_public_slot"] - 2 for row in intervals) else "Q" for i in range(count))
        resources = {"hardware_threads": 2, "workers": 4, "slot_owners": int(argument("--owners", "4")), "total_memory_limit_bytes": 1000000,
                     "workers_explicit": False, "owners_explicit": "--owners" in command}
        public_slots = [{"index": i, "pattern_epoch": 100 + i, "symbol_ordinal": 3 * i,
                         "observation": {"first": i * 128, "stop": (i + 1) * 128}} for i in range(count)]
        begin = int(argument("--service-backlog-begin-slot")) if "--service-backlog-begin-slot" in command else None
        end = int(argument("--service-backlog-end-slot")) if "--service-backlog-end-slot" in command else None
        max_samples = int(argument("--max-samples"))
        spool_limit = int(argument("--service-spool-byte-limit", str(max_samples * 17)))
        target = receipt["control"] in {"wanted", "interior-fade", "capture-gap"}
        service_plan = {"schema_version": 1, "episodes": episodes, "wire_bits_per_episode": wire,
            "expected_message_count": episodes if target else 0, "expected_message_intervals": intervals,
            "slot_pattern": pattern, "maximum_spool_bytes": spool_limit, "planned_spool_bytes": horizon * 17,
            "fits_spool_budget": horizon * 17 <= spool_limit, "input_chunk_samples": int(argument("--chunk", "4096")),
            "backlog_begin_slot": begin, "backlog_end_slot": end,
            "backlog_unique_baseband_seconds": (end - begin) * 128 / 1920 if begin is not None else None}
        plan.update(service_replay=service_plan, max_samples=max_samples,
            max_wall_seconds=float(argument("--max-wall-seconds")), horizon_samples=horizon, scored_slot_horizon=count,
            public_slots=public_slots, receiver_resources=resources)
        if mode == "receive":
            receipt["receiver"].update(state="input_ended", next_public_slot=count)
            receipt["outcomes"].update(actual_scored_slots=count, accepted_samples=horizon,
                correct_decoded_complete=False, accepted_messages=0)
            receipt["slots"] = [{"index": i, "pattern_epoch": row["pattern_epoch"], "symbol_ordinal": row["symbol_ordinal"],
                "first_sample": row["observation"]["first"], "stop_sample": row["observation"]["stop"],
                "complete": True, "all_valid": True, "decision": "data" if pattern[i] == "D" else "quiet", "bit": 1 if pattern[i] == "D" else None,
                "commit_wall_seconds": .01 + i / 10, "commit_process_cpu_seconds": .02 + i / 10,
                "commit_input_stop": horizon, "commit_uncommitted_slots": 1, "commit_active_owners": 2,
                "commit_next_ready": i + 1 < count} for i, row in enumerate(public_slots)]
            receipt["messages"] = []
            receipt["channel"] = {"samples": horizon, "hashed_samples": horizon, "input_sha256": "f" * 64}
            replay = {**service_plan, "wall_seconds": 2., "process_cpu_seconds": 2.,
                "unique_baseband_seconds": horizon / 1920, "service_seconds_per_baseband_second": 2 * 1920 / horizon,
                "spool_read_and_verification_seconds": .2, "spool_read_and_verification_process_cpu_seconds": .3,
                "render_wall_seconds": 1., "render_process_cpu_seconds": .5, "spool_write_seconds": .1, "spool_write_process_cpu_seconds": .1,
                "captured_input_sha256": "f" * 64, "verified_input_sha256": "f" * 64, "digest_match": True,
                "replayed_samples": horizon, "work_complete": True, "communication_complete": not target,
                "peak_active_owners": 2, "mean_busy_hardware_fraction": .5, "receiver_peak_reserved_bytes": 100000,
                "outside_receiver_future_reserved_bytes": 10000, "spool_residency_reserved_bytes": 0, "peak_reserved_bytes": 110000,
                "spool_backing_kind": "known_disk", "spool_bytes": horizon * 17, "spool_working_bytes": 100,
                "total_application_memory_limit_bytes": 1000000, "receiver_memory_limit_bytes": 990000,
                "max_samples": max_samples, "max_wall_seconds": plan["max_wall_seconds"],
                "workers_explicit": False, "owners_explicit": "--owners" in command, "capacity_qualified": False, "backlog_interval": None}
            if begin is not None:
                wall = (end - begin) / 10
                represented = service_plan["backlog_unique_baseband_seconds"]
                replay["backlog_interval"] = {"start_after_public_slot": begin, "stop_after_public_slot": end,
                    "complete": True, "reason": "predeclared_ready_input_commit_interval", "process_cpu_seconds": wall,
                    "wall_seconds": wall, "unique_baseband_seconds": represented,
                    "service_seconds_per_baseband_second": wall / represented, "commits": end - begin,
                    "initial_uncommitted_slots": 1, "busy_hardware_fraction": .5}
            receipt["service_replay"] = replay
    if receipt["requested_scenario"] == "hf-fullwave-reflection":
        add_fake_fullwave_receipt(receipt)
    if receipt["requested_scenario"] in runner.MAGNETOIONIC_REFLECTION:
        add_fake_magnetoionic_receipt(receipt)
    if receipt["requested_scenario"] in runner.GANNON_REPLAY:
        add_fake_gannon_receipt(receipt)
    if "--hf-predictive-quiet" in command:
        receipt["plan"].update(hf_predictive_quiet_requested=True, hf_predictive_quiet_active=False)
        if mode == "receive":
            for row in receipt["slots"]:
                row.update(operational_log_e=30. if row["decision"] == "data" else -2., operational_threshold=21.,
                    bit_evidence=[-3., 31.] if row["decision"] == "data" else [-3., -1.], bit_separation=34.,
                    quiet_energy=9., quiet_threshold=10.)
            receipt["plan"]["predictive_training_reserved_bytes"] = 0
            receipt.setdefault("timing", {}).update(wall_seconds=3., wall_includes_shared_render_per_policy=True)
            receipt["receiver"].update(quiet_policy="canonical", predictive_training_seconds=0., predictive_training_reserved_bytes=0,
                predictive_training_scanned_samples=0, predictive_training_scheduling="sequential_before_receiver_executor")
            candidate = json.loads(json.dumps(receipt))
            candidate["plan"].update(hf_predictive_quiet_active=True, predictive_training_reserved_bytes=1024)
            candidate["receiver"].update(quiet_policy="independent_public_gap_line_prediction",
                predictive_training_seconds=.1, predictive_training_reserved_bytes=1024,
                predictive_training_scanned_samples=receipt["plan"]["horizon_samples"])
            for row in candidate["slots"]:
                row["predictive_quiet"] = fake_predictive_diagnostic(row)
            receipt["hf_predictive_quiet_comparison"] = {"experimental": True,
                "truth_information_passed_to_receiver": False, "rendered_once": True, "status": "completed",
                "common_render_wall_seconds": 1., "candidate": candidate}
    return receipt


def fake_fec_repaired_receipt(command) -> dict:
    """Framing/statistics fixture, not an encoder or decoder implementation."""
    receipt = fake_completed_receipt(command)
    plan = receipt["plan"]
    wire = plan["wire_bits"]
    receipt["outcomes"].update({"correct_complete": False, "correct_decoded_complete": True,
        "physical_completions": 1, "accepted_messages": 1, "actual_scored_slots": wire + 2,
        "wrong_bits": 1, "ambiguous_active_slots": 1})
    def area(received, corrected=0, missing=0, corrected_bytes=0, erased_bytes=0, repaired_bytes=0):
        return dict(received_bits=received, corrected_bits=corrected, missing_bits=missing,
                    corrected_bytes=corrected_bytes, erased_bytes=erased_bytes, repaired_bytes=repaired_bytes)
    receipt["messages"] = [{"first_public_slot": 0, "stop_public_slot": wire + 2,
        "physical_complete": True, "premature": False, "decoded": True, "matches_transmitted_message": True,
        "slots_consumed": wire + 2, "observed_bits": wire - 1, "missing_slots": 1,
        "erasure_slot_indices": [5], "trailing_quiet_slots": [wire, wire + 1], "quiet_slots": 2,
        "long_form": False, "message_ready_for_decode": True, "fec": plan["fec"],
        "decoded_payload_bytes": list(plan["message"].encode("ascii")), "decoded_endpoint_matches": True,
        "consumed_bytes": wire // 8, "corrected_bytes": 1, "authenticated": False,
        "fec_stats": {"data": area(15, 1, 1, 1, 1, 1), "integrity": area(32), "parity": area(wire - 48)}}]
    return receipt


def complete_service_messages(receipt: dict) -> None:
    """Populate strict two-message framing in a schema fixture, without a codec."""
    service, plan = receipt["service_replay"], receipt["plan"]
    wire = service["wire_bits_per_episode"]
    receipt["messages"] = [{**interval, "physical_complete": True, "decoded": True, "premature": False,
        "matches_transmitted_message": True, "decoded_endpoint_matches": True,
        "slots_consumed": wire + 2, "observed_bits": wire, "missing_slots": 0,
        "consumed_bytes": wire, "trailing_quiet_slots": [wire, wire + 1], "quiet_slots": 2,
        "long_form": False, "message_ready_for_decode": True, "fec": "off", "erasure_slot_indices": [],
        "decoded_payload_bytes": list(plan["message"].encode("ascii")),
        "wrong_bits": 0, "ambiguous_active_slots": 0, "quiet_active_slots": 0}
        for interval in service["expected_message_intervals"]]
    receipt["outcomes"].update(physical_completions=service["episodes"], accepted_messages=service["episodes"])
    service["communication_complete"] = True


class CampaignValidationTests(unittest.TestCase):
    def test_frozen_long_gui_holdout_accepts_one_bounded_replay(self):
        episode = repeated_bit_episode(seed=62014, noise_seed=72014, repeat_path_seed=62034,
            cn0_db_hz=34, bit_seconds=32, repeat_path_scale=1, repeat_separate_attempts=True,
            max_wall_seconds=300, max_samples=2_400_000)
        runner._validate_episode(episode, 0, set(), 300, 2_400_000)
        for invalid in (dict(episode, max_wall_seconds=300.01), dict(episode, seed=62015),
                        dict(episode, repeat_thermal_off=True), dict(episode, gui_clock_edge=False),
                        dict(episode, bit_seconds=2), dict(episode, max_samples=2_400_001)):
            with self.subTest(invalid=invalid), self.assertRaises(runner.CampaignError):
                runner._validate_episode(invalid, 0, set(), 300, 2_400_000)
        receipt, plan = repeated_bit_receipt(episode)
        plan.update(max_wall_seconds=299.5, max_samples=2_400_000)
        runner._validate_repeated_bit_receipt(receipt, episode, plan, native_max_wall_seconds=299.5,
                                              native_max_samples=2_400_000)
        with self.assertRaisesRegex(runner.CampaignError, "actual frozen invocation"):
            runner._validate_repeated_bit_receipt(receipt, episode, plan, native_max_wall_seconds=299.75)
        with self.assertRaisesRegex(runner.CampaignError, "full hashed capture"):
            runner._validate_repeated_bit_receipt(receipt, dict(episode, bit_seconds=2), plan)
        plan["max_wall_seconds"] = 300.01
        with self.assertRaisesRegex(runner.CampaignError, "full hashed capture"):
            runner._validate_repeated_bit_receipt(receipt, episode, plan)
        campaign = {"schema_version": 1, "name": "bounded long holdout", "max_trials": 1,
                    "max_wall_seconds": 300, "per_trial_wall_seconds": 300,
                    "max_samples": 2_400_000, "episodes": [episode]}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            path.write_text(json.dumps(campaign))
            self.assertEqual(runner._read_campaign(path)["episodes"], [episode])
            campaign.update(max_trials=2, episodes=[episode, dict(episode, id="duplicate-physics")])
            path.write_text(json.dumps(campaign))
            with self.assertRaisesRegex(runner.CampaignError, "one episode"):
                runner._read_campaign(path)

    def test_separate_raw_campaign_allows_mixed_thermal_conditions(self):
        episodes = [
            repeated_bit_episode(id="off-bit0", repeat_bit=0, repeat_path_scale=1,
                repeat_path_seed=62033, repeat_thermal_off=True, repeat_separate_attempts=True,
                capture_digest_slot=12, max_samples=2_400_000),
            repeated_bit_episode(id="on-bit1", repeat_bit=1, repeat_path_scale=1,
                repeat_path_seed=62033, repeat_thermal_off=False, repeat_separate_attempts=True,
                max_samples=2_400_000),
        ]
        campaign = {"schema_version": 1, "name": "mixed-thermal-separated-raw",
                    "max_trials": 2, "max_wall_seconds": 240,
                    "per_trial_wall_seconds": 120, "max_samples": 2_400_000,
                    "episodes": episodes}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            path.write_text(json.dumps(campaign))
            validated = runner._read_campaign(path)
        self.assertEqual([episode["repeat_thermal_off"] for episode in validated["episodes"]],
                         [True, False])

    def test_repeated_bit_episode_is_bounded_and_maps_native_flags(self):
        episode = repeated_bit_episode()
        runner._validate_episode(episode, 0, set(), 120, 1_200_000)
        command = runner._command(Path("/native/probe"), episode, 1_200_000, 120)
        for option, value in (("--mode", "gui-repeated-bit"), ("--repeat-bit", "1"),
                              ("--repeat-path-scale", "0.5"), ("--repeat-path-seed", "62032")):
            self.assertEqual(command[command.index(option) + 1], value)
        self.assertNotIn("--repeat-thermal-off", command)
        thermal_off = dict(episode, repeat_thermal_off=True)
        runner._validate_episode(thermal_off, 0, set(), 120, 1_200_000)
        self.assertIn("--repeat-thermal-off", runner._command(Path("/native/probe"), thermal_off, 1_200_000, 120))
        thermal_off_separate = dict(episode, repeat_separate_attempts=True,
                                    repeat_thermal_off=True, capture_digest_slot=12,
                                    max_samples=2_400_000)
        runner._validate_episode(thermal_off_separate, 0, set(), 120, 2_400_000)
        separate_command = runner._command(Path("/native/probe"), thermal_off_separate, 2_400_000, 120)
        self.assertIn("--repeat-separate-attempts", separate_command)
        self.assertIn("--repeat-thermal-off", separate_command)
        self.assertEqual(separate_command[separate_command.index("--capture-digest-slot") + 1], "12")
        thermal_receipt, thermal_plan = repeated_bit_receipt(thermal_off_separate)
        runner._validate_repeated_bit_receipt(thermal_receipt, thermal_off_separate, thermal_plan)
        thermal_plan["noise_variance"] = 0.0
        with self.assertRaisesRegex(runner.CampaignError, "positive receiver noise variance"):
            runner._validate_repeated_bit_receipt(thermal_receipt, thermal_off_separate, thermal_plan)
        for invalid in (dict(episode, repeat_bit=True), dict(episode, repeat_bit=2),
                        dict(episode, repeat_path_scale=.25), dict(episode, repeat_path_seed=True),
                        dict(episode, mode="receive"), dict(episode, helper=True),
                        dict(episode, max_samples=1_200_001), dict(episode, max_wall_seconds=120.01)):
            with self.subTest(invalid=invalid), self.assertRaises(runner.CampaignError):
                runner._validate_episode(invalid, 0, set(), 120, 1_200_000)



    def test_repeated_bit_receipt_validates_raw_counts_calendar_and_not_codec(self):
        episode = repeated_bit_episode()
        receipt, plan = repeated_bit_receipt(episode)
        runner._validate_repeated_bit_receipt(receipt, episode, plan)
        summary = runner._repeated_bit_summary(receipt)
        self.assertEqual(summary["correct_data"], 6)
        self.assertEqual(summary["longest_no_correct_run"], 0)
        self.assertTrue(summary["physical_quiet_complete"])
        self.assertAlmostEqual(summary["elapsed_calendar_seconds"], 1000 / 1920)
        broken = json.loads(json.dumps(receipt))
        broken["repeated_bit"]["correct_data"] = 5
        with self.assertRaisesRegex(runner.CampaignError, "counts"):
            runner._validate_repeated_bit_receipt(broken, episode, broken["plan"])
        broken = json.loads(json.dumps(receipt))
        broken["slots"][0]["raw_bit_correct"] = False
        with self.assertRaisesRegex(runner.CampaignError, "truth-aligned raw correctness"):
            runner._validate_repeated_bit_receipt(broken, episode, broken["plan"])



    def test_repeated_bit_separate_attempts_validates_18_slot_quiet_calendar(self):
        episode = repeated_bit_episode(repeat_separate_attempts=True, max_samples=2_400_000)
        runner._validate_episode(episode, 0, set(), 120, 2_400_000)
        command = runner._command(Path("/native/probe"), episode, 2_400_000, 120)
        self.assertIn("--repeat-separate-attempts", command)
        receipt, plan = repeated_bit_receipt(episode)
        runner._validate_repeated_bit_receipt(receipt, episode, plan)
        self.assertEqual([slot["index"] for slot in receipt["slots"] if slot["expected_bit"] is not None],
                         [0, 3, 6, 9, 12, 15])
        broken = json.loads(json.dumps(receipt))
        broken["repeated_bit"]["completed_quiet_pairs"] = 5
        with self.assertRaisesRegex(runner.CampaignError, "quiet accounting"):
            runner._validate_repeated_bit_receipt(broken, episode, broken["plan"])

        # A fully scored ambiguous internal aperture is still ordered-valid;
        # it prevents that attempt's quiet pair from completing without
        # invalidating the other scored slots or the entire raw-bit receipt.
        ambiguous_quiet = json.loads(json.dumps(receipt))
        ambiguous_quiet["slots"][8]["decision"] = "ambiguous"
        ambiguous_quiet["repeated_bit"].update({
            "inter_attempt_quiet_slots": 9,
            "completed_quiet_pairs": 5,
            "physical_quiet_complete": False,
        })
        runner._validate_repeated_bit_receipt(ambiguous_quiet, episode, ambiguous_quiet["plan"])
        forged_quiet = json.loads(json.dumps(ambiguous_quiet))
        forged_quiet["repeated_bit"]["inter_attempt_quiet_slots"] = 10
        with self.assertRaisesRegex(runner.CampaignError, "quiet counts"):
            runner._validate_repeated_bit_receipt(forged_quiet, episode, forged_quiet["plan"])
        forged_pairs = json.loads(json.dumps(ambiguous_quiet))
        forged_pairs["repeated_bit"]["completed_quiet_pairs"] = 6
        forged_pairs["repeated_bit"]["physical_quiet_complete"] = True
        with self.assertRaisesRegex(runner.CampaignError, "quiet counts"):
            runner._validate_repeated_bit_receipt(forged_pairs, episode, forged_pairs["plan"])

        # Separate-attempt captures legitimately exceed the six-contiguous-bit
        # cap. Exercise the receipt validator at the actual boundary, rather
        # than only checking tiny synthetic fixtures.
        above_contiguous_cap = json.loads(json.dumps(receipt))
        above_contiguous_cap["plan"]["max_samples"] = 1_200_001
        above_contiguous_cap["plan"]["horizon_samples"] = 1_200_001
        above_contiguous_cap["plan"]["represented_seconds"] = 1_200_001 / 1920
        above_contiguous_cap["channel"]["samples"] = 1_200_001
        above_contiguous_cap["channel"]["hashed_samples"] = 1_200_001
        runner._validate_repeated_bit_receipt(above_contiguous_cap, episode, above_contiguous_cap["plan"])

        above_separate_cap = json.loads(json.dumps(above_contiguous_cap))
        above_separate_cap["plan"]["max_samples"] = 2_400_001
        above_separate_cap["plan"]["horizon_samples"] = 2_400_001
        above_separate_cap["plan"]["represented_seconds"] = 2_400_001 / 1920
        above_separate_cap["channel"]["samples"] = 2_400_001
        above_separate_cap["channel"]["hashed_samples"] = 2_400_001
        with self.assertRaisesRegex(runner.CampaignError, "full hashed capture within its declared finite bounds"):
            runner._validate_repeated_bit_receipt(above_separate_cap, episode, above_separate_cap["plan"])

    def test_frozen_reception_requirement_is_receive_only_and_not_native_cli(self):
        base = {"id": "required", "scenario": "awgn", "mode": "receive",
                "waveform": "reference", "seed": 4, "control": "wanted",
                "reception_requirement": "required_complete"}
        runner._validate_episode(base, 0, set(), 30, 100_000)
        command = runner._command(Path("/native/probe"), base, 100_000, 30)
        self.assertNotIn("--reception-requirement", command)
        self.assertNotIn("required_complete", command)
        characterization = dict(base, reception_requirement="characterization")
        runner._validate_episode(characterization, 0, set(), 30, 100_000)
        negative = dict(base, id="negative", control="noise", reception_requirement="negative_control")
        runner._validate_episode(negative, 0, set(), 30, 100_000)
        for invalid in (
            dict(base, reception_requirement="invented"),
            dict(base, reception_requirement=[]),
            dict(base, mode="plan"),
            dict(base, control="wrong-key"),
            dict(base, control="noise", reception_requirement="characterization"),
            dict(base, control="wanted", reception_requirement="negative_control"),
        ):
            with self.subTest(invalid=invalid), self.assertRaises(runner.CampaignError):
                runner._validate_episode(invalid, 0, set(), 30, 100_000)
        # Historical episode dictionaries without the optional annotation remain valid.
        runner._validate_episode({k: v for k, v in base.items() if k != "reception_requirement"},
                                 0, set(), 30, 100_000)

    def test_vector_3d_descriptor_preserves_spatial_and_trajectory_scope(self):
        for scenario in (name for name in runner.HF_VECTOR_OBLIQUE_SCENARIOS if name.startswith("hf-vector-3d")):
            descriptor = runner._vector_oblique_spec(scenario)
            runner._validate_vector_oblique_descriptor(descriptor, scenario)
            self.assertEqual(descriptor["spatial_geometry_dimension"], 3)
            self.assertEqual(descriptor["transverse_periodic_dimensions"], 2)
            self.assertAlmostEqual(descriptor["drift_y_mps"] / descriptor["period_y_m"],
                descriptor["trajectory_winding_y"] * descriptor["drift_mps"] / descriptor["period_m"])
            self.assertNotIn("density_harmonic_x", descriptor)
            for mutation in ({"spatial_geometry_dimension": 2}, {"drift_y_mps": 0.},
                             {"density_harmonic_x": [0., 0.]}, {"transverse_density_modes": []}):
                with self.subTest(scenario=scenario, mutation=mutation), self.assertRaises(runner.CampaignError):
                    runner._validate_vector_oblique_descriptor(dict(descriptor, **mutation), scenario)
        planar = runner._vector_oblique_spec("hf-vector-3d-planar")
        in_plane = runner._vector_oblique_spec("hf-vector-3d-in-plane")
        self.assertTrue(all(mode["amplitude_x"] == 0 for mode in planar["transverse_density_modes"]))
        self.assertTrue(all(mode["amplitude_x"] == 0 for mode in in_plane["transverse_density_modes"] if mode["n"] != 0))
        self.assertGreater(in_plane["transverse_density_modes"][0]["amplitude_x"], 0.)

    def test_legacy_vector_descriptors_keep_independent_frozen_field_expectations(self):
        # These literals intentionally stand apart from _vector_oblique_spec and its scenario map.
        expected = {
            "hf-vector-oblique": (True, True, 0, 0, False),
            "hf-vector-oblique-off": (False, True, 0, 0, False),
            "hf-vector-oblique-no-b": (True, False, 0, 0, False),
            "hf-vector-oblique-holdout": (True, True, 1, 0, True),
            "hf-vector-oblique-cross": (True, True, 0, 1, False),
            "hf-vector-oblique-chain-off": (True, True, 0, 0, False),
            "hf-vector-oblique-chain-clocks": (True, True, 0, 0, False),
            "hf-vector-oblique-chain-clocks-tone": (True, True, 0, 0, False),
            "hf-vector-3d": (True, True, 2, 0, False),
            "hf-vector-3d-holdout": (True, True, 3, 0, True),
            "hf-vector-3d-planar": (False, True, 2, 0, False),
            "hf-vector-3d-in-plane": (True, True, 2, 0, False),
            "hf-vector-3d-chain-off": (True, True, 2, 0, False),
            "hf-vector-3d-chain-clocks-tone": (True, True, 2, 0, False),
        }
        legacy_ids = (set(runner.HF_VECTOR_OBLIQUE_SCENARIOS) - set(runner.HF_VECTOR_EVOLUTION_SCENARIOS)
                      - set(runner.HF_VECTOR_EVOLUTION_CHAIN_SCENARIOS) - {runner.HF_PROTECTED_OBLIQUE})
        self.assertEqual(set(expected), legacy_ids)
        primary = (50., 112000., 24000., 960., 6.55e6, 1300., 80., [18e-6, 17e-6, -43e-6], .10, 8., 600.)
        held = (53., 104000., 20500., 1200., 6.12e6, 2450., 100., [-11e-6, 25e-6, -46e-6], .23, 7.5, 520.)
        for name, (perturbations, magnetic, field, antenna, is_held) in expected.items():
            with self.subTest(scenario=name):
                descriptor = runner._vector_oblique_spec(name)
                runner._validate_vector_oblique_descriptor(descriptor, name)
                geometry = held if is_held else primary
                self.assertEqual((descriptor["perturbations"], descriptor["magnetic"], descriptor["field"], descriptor["antenna"]),
                                 (perturbations, magnetic, field, antenna))
                for key, value in zip(("incidence_deg", "height_m", "period_m", "thickness_m", "peak_plasma_hz",
                                       "collision_rate_per_second", "corrugated_slice_m", "magnetic_field_tesla",
                                       "launch_tm_fraction", "drift_mps", "epoch_seconds"), geometry):
                    if key == "magnetic_field_tesla" and not magnetic:
                        value = [0., 0., 0.]
                    self.assertEqual(descriptor[key], value)
                if field < 2:
                    self.assertEqual(descriptor["preparation_workspace_bound_bytes"], 16 * 1024**2)
                    self.assertNotIn("cross_plane", descriptor)
                    self.assertEqual(descriptor["density_harmonic_x"],
                                     ([.007, .0044] if is_held else [.008, .0038]) if perturbations else [0., 0.])
                    continue
                self.assertEqual(descriptor["spatial_geometry_dimension"], 3)
                self.assertEqual(descriptor["cross_plane"], name != "hf-vector-3d-in-plane")
                wide = name == "hf-vector-3d-holdout"
                self.assertEqual(descriptor["preparation_workspace_bound_bytes"], (256 if wide else 32) * 1024**2)
                self.assertEqual((descriptor["production_half_orders_each_axis"], descriptor["coarse_half_orders_each_axis"],
                                  descriptor["refined_half_orders_each_axis"], descriptor["maximum_admitted_spatial_modes"]),
                                 (7, 6, 8, 289) if wide else (3, 2, 4, 81))
                modes = descriptor["transverse_density_modes"]
                expected_modes = ([(1, 0, .0016, -1.05), (0, 1, .0019, .6), (1, -1, .0011, -.2)] if is_held else
                                  [(1, 0, .0018, .45), (0, 1, .0015, -.7), (1, 1, .0012, 1.1)])
                for actual, (m, n, amplitude, phase) in zip(modes, expected_modes):
                    self.assertEqual((actual["m"], actual["n"], actual["phase_rad"]), (m, n, phase))
                    expected_amplitude = amplitude if perturbations and (descriptor["cross_plane"] or n == 0) else 0.
                    self.assertEqual(actual["amplitude_x"], expected_amplitude)

    def test_evolving_vector_descriptors_bind_intrinsic_field_and_matched_rigid_controls(self):
        evolving = ("hf-vector-evolving", "hf-vector-evolving-holdout")
        rigid = ("hf-vector-evolving-rigid", "hf-vector-evolving-holdout-rigid")
        for name in (*evolving, *rigid, *runner.HF_VECTOR_EVOLUTION_CHAIN_SCENARIOS):
            descriptor = runner._vector_oblique_spec(name)
            runner._validate_vector_oblique_descriptor(descriptor, name)
            self.assertEqual(descriptor["spatial_geometry_dimension"], 3)
            self.assertEqual(descriptor["field"], 4 if "holdout" in name else 2)
            self.assertEqual(descriptor.get("intrinsic_evolution", False),
                             name in evolving or name in runner.HF_VECTOR_EVOLUTION_CHAIN_SCENARIOS)
            self.assertEqual(descriptor["preparation_workspace_bound_bytes"],
                             (48 if name in evolving or name in runner.HF_VECTOR_EVOLUTION_CHAIN_SCENARIOS else 32) * 1024**2)
            if name in rigid:
                self.assertNotIn("evolution", descriptor)
        primary, held = (runner._vector_oblique_spec(name) for name in evolving)
        self.assertEqual((primary["evolution"]["start_seconds"], primary["evolution"]["duration_seconds"]), (490., 192.))
        self.assertEqual((held["evolution"]["start_seconds"], held["evolution"]["duration_seconds"]), (838., 144.))
        self.assertEqual(held["drift_mps"], 7.)
        self.assertEqual(primary["evolution"]["intervals"], 16)
        self.assertEqual(primary["evolution"]["law"],
            "Each mode multiplier is 1+(a+ib)B(u)+(c+id)Q(u), B=64u^3(1-u)^3, Q=2(2u-1)B; identity outside u in [0,1].")
        self.assertEqual(primary["evolution"]["time_scope"],
            "T=(output_sample-32)/1920 seconds, on the physical output clock before the added FIR delay; independent of the receiver message calendar.")
        self.assertEqual(held["transverse_density_modes"], [
            {"m": 1, "n": 0, "amplitude_x": .0013, "phase_rad": -.65},
            {"m": 0, "n": 1, "amplitude_x": .0011, "phase_rad": .90},
            {"m": 1, "n": -1, "amplitude_x": .00085, "phase_rad": -1.40}])
        changed = dict(held, evolution=dict(held["evolution"], start_seconds=839.))
        with self.assertRaises(runner.CampaignError):
            runner._validate_vector_oblique_descriptor(changed, "hf-vector-evolving-holdout")

    def test_evolving_vector_report_requires_nested_temporal_witnesses_and_preserves_rigid_receipts(self):
        report = runner._vector_oblique_spec("hf-vector-evolving")
        report.update(
            table_qualified=True, table_sha256="0" * 64,
            table_digest_scope="all 257 spatial-position knots and 17 evolution-time residual knots across all 129 retained lags, binary64 complex coefficients/slopes, plus a versioned T/B/Q time-law tag, interval count, event start/duration and all mode factors in canonical little-endian metadata order",
            qualification_scope="finite-order bidirectional 3D cold collisional Maxwell scattering with doubly periodic transverse density, a bounded intrinsic density-mode event and fixed antenna projection; temporal checks cover 16 intervals, 16 midpoints, 4 direct FIR epochs, 3 refined epochs, 61 temporal RF pairs, and a 4097-point event-time minimum scan with five checked RF minima, but do not establish random 3D fields, field calibration or environmental probabilities",
            evolution_knot_count=17, rf_solves=270, output_half_orders=3, output_half_orders_y=3,
            output_trajectory_half_orders=6, checked_frequencies=13, checked_positions=2048,
            checked_table_assembly_positions=54, matrix_products=1, maximum_rf_dense_terms=100,
            rf_dense_terms_limit=1_200_000_000, maximum_admitted_spatial_modes=81,
            carrier_witnesses_qualified=True, unfolded_order_scope="two-dimensional transverse diffraction orders in x and y are retained through Maxwell solves; the trajectory-folded response combines those orders only at the declared receiver coordinates",
            spatial_l1_error_scope="mesh/order/refined-order errors are L1 sums of absolute fixed-antenna-projected complex-voltage differences across retained unfolded (m,n) orders, before trajectory folding; they are not polarization-resolved norms and do not bound orders omitted by finite truncation",
            carrier_power_minimum_scope="minimum and maximum carrier power are sampled over one declared commensurate receiver-trajectory period; the finite trajectory-series lower bound is not a global x-y minimum or a three-dimensional path-ensemble bound",
            out_of_plane_flux_scope="out-of-plane reflected and transmitted flux is independently reported from the mixed TE/TM launch for the retained nonzero-y diffraction orders",
            period_averaged_energy_scope="coherent average along the declared commensurate receiver trajectory after combining equal temporal indices; not an incoherent average over the full x-y spatial torus",
            maximum_edge_x_flux=1e-5, maximum_edge_y_flux=2e-5, maximum_edge_flux=3e-5,
            carrier_refined_edge_flux=3e-5, carrier_refined_edge_x_flux=1e-5, carrier_refined_edge_y_flux=2e-5,
            carrier_out_of_plane_reflected_flux=1e-3, carrier_out_of_plane_transmitted_flux=1e-3,
            carrier_reflected_flux=.7, carrier_transmitted_flux=.2, carrier_scattered_reflected_flux=.3,
            carrier_crosspolar_reflected_flux=.8, carrier_crosspolar_scattered_flux=.6,
            table_assembly_voltage_error=0., minimum_vacuum_cosine=.5, maximum_flux_excess=0., maximum_solve_residual=0.,
            mesh_voltage_error=0., order_voltage_error=0., refined_order_voltage_error=0., frequency_voltage_error=0.,
            fir_voltage_error=0., period_averaged_discarded_energy_fraction=0., interpolation_peak_error_bound=0.,
            retarded_peak_error_bound=0., relative_voltage_error_at_minimum=0.,
            minimum_carrier_power=.01, maximum_carrier_power=1., certified_minimum_power_lower_bound=.009,
            minimum_checked_band_power_lower_bound=.008, minimum_checked_band_power_witness_offset_hz=120.,
            period_averaged_raw_precursor_energy_fraction=0., minimum_carrier_position_m=1.,
            preparation_peak_workspace_bytes=20 * 1024**2, table_workspace_bytes=1024, stream_workspace_bytes=256,
            preparation_seconds=1., source_minimum_frequency_hz=-112.5, source_maximum_frequency_hz=112.5,
            incident_energy_sample_sum=100., selected_energy_sample_sum=50.)
        minimum_witnesses = [
            {"offset_hz": offset, "physical_seconds": 682., "assembled_power": assembled,
             "direct_power": direct, "voltage_error": error}
            for offset, assembled, direct, error in (
                (-384., .012, .011999, 1e-5), (-196.5, .0118, .011799, 1e-5),
                (0., .0116, .011599, 1e-5), (196.5, .0114, .011399, 1e-5),
                (384., .0112, .011199, 1e-5))]
        evolution = {
            "qualified": True, "intervals": 16, "scanned_time_points": 4097, "checked_midpoints": 16,
            "checked_direct_fir_epochs": 4, "checked_refined_epochs": 3, "checked_rf_pairs": 61,
            "checked_minimum_rf_pairs": 5, "minimum_scanned_assembled_power": .0112,
            "minimum_witnesses": minimum_witnesses,
            "minimum_slice_density_fraction": .8, "maximum_mode_magnitude_change": .01,
            "maximum_phase_closure_change_rad": .2, "maximum_rigid_departure_voltage": .01,
            "maximum_temporal_voltage_error": .001, "maximum_direct_fir_tap_error": .001,
            "maximum_frequency_voltage_error": .0005, "maximum_fir_voltage_error": .0005,
            "maximum_discarded_energy_fraction": 1e-7, "maximum_mesh_voltage_error": .003,
            "maximum_refined_order_voltage_error": .003, "maximum_refined_edge_flux": 4e-5,
            "minimum_checked_trajectory_power": .011199, "relative_voltage_error_at_minimum": 0.,
            "retarded_peak_error_bound": .003,
        }
        evolution["relative_voltage_error_at_minimum"] = (
            evolution["maximum_mesh_voltage_error"] + evolution["maximum_refined_order_voltage_error"] +
            max(evolution["maximum_temporal_voltage_error"],
                evolution["maximum_direct_fir_tap_error"] + evolution["maximum_fir_voltage_error"])) / math.sqrt(
                    min(evolution["minimum_checked_trajectory_power"], evolution["minimum_scanned_assembled_power"]))
        report["evolution_checks"] = evolution
        runner._validate_vector_oblique_report(report, "hf-vector-evolving")
        for bad in (
            dict(report, rf_solves=265),
            dict(report, evolution_checks={**evolution, "checked_rf_pairs": 60}),
            dict(report, evolution_checks={**evolution, "maximum_temporal_voltage_error": .002}),
            dict(report, evolution_checks={**evolution, "unexpected": 1}),
            dict(report, table_digest_scope="all 257 spatial-position knots and 17 evolution-time residual knots, across all 129 retained lags, as binary64 complex coefficients and slopes in canonical little-endian order; source identity binds generation and interpolation"),
            dict(report, evolution_checks={**evolution, "minimum_witnesses": minimum_witnesses[:-1]}),
            dict(report, evolution_checks={**evolution, "minimum_witnesses": [dict(minimum_witnesses[0], physical_seconds=681.99), *minimum_witnesses[1:]]}),
            dict(report, evolution_checks={**evolution, "minimum_witnesses": [dict(minimum_witnesses[0], offset_hz=-360.), *minimum_witnesses[1:]]}),
            dict(report, evolution_checks={**evolution, "minimum_scanned_assembled_power": .0113}),
            dict(report, evolution_checks={**evolution, "relative_voltage_error_at_minimum": .1}),
            {key: value for key, value in report.items() if key != "evolution_checks"},
        ):
            with self.subTest(bad=bad.get("evolution_checks")), self.assertRaises(runner.CampaignError):
                runner._validate_vector_oblique_report(bad, "hf-vector-evolving")
        rigid = runner._vector_oblique_spec("hf-vector-evolving-rigid")
        self.assertIs(rigid["intrinsic_evolution"], False)
        with self.assertRaises(runner.CampaignError):
            runner._validate_vector_oblique_report({**report, **rigid, "evolution_checks": evolution},
                                                   "hf-vector-evolving-rigid")

    def test_evolving_vector_receive_requirements_predeclare_anchors_and_characterization(self):
        def row(scenario, cn0, seconds=64, requirement="required_complete", control="wanted"):
            return {"id": scenario, "scenario": scenario, "mode": "receive", "waveform": "reference",
                    "seed": 1, "noise_seed": 2, "cn0_db_hz": cn0, "bit_seconds": seconds,
                    "message": "C", "control": control, "reception_requirement": requirement}
        valid = [
            row("hf-vector-evolving", 20), row("hf-vector-evolving-rigid", 20),
            row("hf-vector-evolving-holdout", 20), row("hf-vector-evolving-holdout-rigid", 20),
            row("hf-vector-evolving", 15, requirement="characterization"),
            dict(row("hf-vector-evolving", 17.5, requirement="characterization"), seed=72511, noise_seed=92511),
            row("hf-vector-evolving", 12, 128, "characterization"),
            row("hf-vector-evolving", 20, control="noise", requirement="negative_control"),
            row("hf-vector-evolving", 20, control="wrong-key", requirement="negative_control"),
            row("hf-vector-evolving-chain-off", 30), row("hf-vector-evolving-chain-clocks-tone", 30),
        ]
        for episode in valid:
            runner._validate_episode(episode, 0, set(), 900, 12_000_000)
        for invalid in (
            row("hf-vector-evolving", 20, requirement="characterization"),
            row("hf-vector-evolving", 15),
            row("hf-vector-evolving", 17.5, requirement="required_complete"),
            row("hf-vector-evolving-holdout", 17.5, requirement="characterization"),
            row("hf-vector-evolving", 17.5, 128, "characterization"),
            dict(row("hf-vector-evolving", 17.5, requirement="characterization"), seed=72511, noise_seed=92512),
            row("hf-vector-evolving", 12, 128, "required_complete"),
            row("hf-vector-evolving-rigid", 12, 128, "characterization"),
            row("hf-vector-evolving-chain-off", 30, requirement="characterization"),
            row("hf-vector-evolving-chain-clocks-tone", 20),
            row("hf-vector-evolving", 20, control="wrong-ordinal", requirement="negative_control"),
        ):
            with self.subTest(invalid=invalid), self.assertRaises(runner.CampaignError):
                runner._validate_episode(invalid, 0, set(), 900, 12_000_000)

    def test_vector_two_hop_descriptors_bind_selected_field_routes_and_frozen_axes(self):
        expected = {
            "hf-vector-two-hop": ("primary", "both", 11),
            "hf-vector-two-hop-only": ("primary", "two_hop", 11),
            "hf-vector-one-hop-only": ("primary", "one_hop", 11),
            "hf-vector-two-hop-holdout": ("holdout", "both", 14),
            "hf-vector-two-hop-chain-off": ("primary", "both", 11),
            "hf-vector-two-hop-chain-clocks-tone": ("primary", "both", 11),
        }
        self.assertEqual(runner.HF_VECTOR_TWO_HOP_SCENARIOS, expected)
        for scenario, (field, routes, axis) in expected.items():
            descriptor = runner._vector_two_hop_spec(scenario)
            self.assertEqual((descriptor["field"], descriptor["routes"], descriptor["receive_axis_index"]),
                             (field, routes, axis))
            self.assertEqual(descriptor["receive_axis_grid_degrees_per_index"], 5.)
            self.assertEqual(descriptor["receive_axis"],
                             [0., math.cos(math.radians(axis * 5)), math.sin(math.radians(axis * 5))])
            runner._validate_vector_two_hop_descriptor(descriptor, scenario)
        primary = runner._vector_two_hop_spec("hf-vector-two-hop")
        held = runner._vector_two_hop_spec("hf-vector-two-hop-holdout")
        self.assertEqual(held["receive_axis_index"], (primary["receive_axis_index"] + 3) % 36)
        self.assertEqual([c["lower_height_m"] for c in primary["es_columns"]], [111000., 103000.])
        self.assertEqual([c["lower_height_m"] for c in held["es_columns"]], [104000., 112000.])
        self.assertEqual([m["m"] for m in primary["es_columns"][0]["density_modes"]], [1, 0, 1])
        self.assertEqual([m["n"] for m in primary["es_columns"][0]["density_modes"]], [0, 1, 1])
        self.assertEqual(len(primary["f_column"]["density_modes"]), 3)
        self.assertTrue(all(mode == {"m": 0, "n": 0, "amplitude_x": 0., "phase_rad": 0.}
                            for mode in primary["f_column"]["density_modes"]))
        self.assertEqual(primary["preparation_workspace_bound_bytes"], 96 * 1024 * 1024)
        for bad in (
            {**primary, "receive_axis_index": 1},
            {**primary, "field": "holdout"},
            {**primary, "routes": "one_hop"},
            {**primary, "ground_conductivity_s_m": .02},
            {**primary, "two_hop_incidence_deg": 48.},
            {**primary, "receive_axis": [0., math.cos(math.radians(5)), math.sin(math.radians(5))]},
        ):
            with self.subTest(bad=bad), self.assertRaises(runner.CampaignError):
                runner._validate_vector_two_hop_descriptor(bad, "hf-vector-two-hop")

    def test_vector_two_hop_plan_requires_current_moving_angle_scope(self):
        scope = {
            "vector_two_hop_exposure_scope": "selected local planar two-Es ground-bounce and one-F return corridors; numerical support does not prove global path completeness, other arrivals, finite-beam coverage, Earth-curvature effects, or field occurrence probability",
            "vector_two_hop_physical_time_scope": "physical output time is (output_sample - 32)/1920; F-region retarded range uses two bounded substitutions with L(d)=hypot(X,2(hF+d)) and flight time L(d)/(2c), from the common receiver output time",
            "vector_two_hop_angular_interpolation_scope": "production F interaction uses nine-node uniform-height P/Q angular interpolation; fixed-angle carrier comparison remains a separate diagnostic, not the production response; moving-angle and staged errors are separately reported and gated at 0.0006 voltage",
        }
        runner._validate_vector_two_hop_plan_scopes(scope)
        stale = dict(scope)
        stale["vector_two_hop_angular_approximation_scope"] = stale.pop("vector_two_hop_angular_interpolation_scope")
        with self.assertRaises(runner.CampaignError):
            runner._validate_vector_two_hop_plan_scopes(stale)
        changed = dict(scope, vector_two_hop_angular_interpolation_scope="fixed-angle tensor approximation")
        with self.assertRaises(runner.CampaignError):
            runner._validate_vector_two_hop_plan_scopes(changed)

    def test_two_hop_chain_workspace_accounting_reconciles_peak_and_residency(self):
        prop_peak = 100_663_296
        table, stream = 8 * 1024 * 1024, 2 * 1024 * 1024
        prop_resident = prop_peak - 100_663_296 + table + stream
        fixed, bridge = 12 * 1024 * 1024, 4096
        tx_workspace, rx_workspace = 2 * 1024 * 1024, 20 * 1024 * 1024
        preparation_peak = prop_peak + fixed + bridge
        resident = rx_workspace + fixed
        aggregate = max(preparation_peak, resident)
        channel = {"workspace_bytes": aggregate, "effective_maximum_workspace_bytes": 192 * 1024 * 1024}
        chain = {"workspace_bytes": aggregate, "workspace_accounting": {
            "fixed_overhead_bytes": fixed, "tx_bridge_bytes": bridge,
            "propagation_bridge_bytes": bridge, "preparation_peak_bytes": preparation_peak,
            "resident_bytes": resident}}
        tx = {"workspace_bytes": tx_workspace}
        propagation = {"workspace_bytes": prop_peak, "resident_workspace_bytes": prop_resident}
        rx = {"workspace_bytes": rx_workspace}
        physical = {"table_workspace_bytes": table, "stream_workspace_bytes": stream}
        runner._validate_two_hop_chain_workspace(channel, chain, tx, propagation, rx, physical)

        def bad(**changes):
            mutated_channel = dict(channel)
            mutated_chain = {**chain, "workspace_accounting": dict(chain["workspace_accounting"])}
            mutated_tx, mutated_propagation, mutated_rx, mutated_physical = (
                dict(tx), dict(propagation), dict(rx), dict(physical))
            for key, value in changes.items():
                section, field = key.split(".", 1)
                target = {"channel": mutated_channel, "chain": mutated_chain,
                          "accounting": mutated_chain["workspace_accounting"],
                          "tx": mutated_tx, "propagation": mutated_propagation,
                          "rx": mutated_rx, "physical": mutated_physical}[section]
                target[field] = value
            return (mutated_channel, mutated_chain, mutated_tx, mutated_propagation,
                    mutated_rx, mutated_physical)

        corruptions = (
            {"propagation.resident_workspace_bytes": prop_resident + 1},
            {"accounting.tx_bridge_bytes": 4095},
            {"accounting.propagation_bridge_bytes": 4095},
            {"tx.workspace_bytes": prop_resident - bridge},
            {"rx.workspace_bytes": prop_resident + bridge},
            {"rx.workspace_bytes": 68_157_441},
            {"accounting.preparation_peak_bytes": preparation_peak + 1},
            {"accounting.resident_bytes": resident + 1},
            {"channel.workspace_bytes": aggregate + 1},
            {"chain.workspace_bytes": aggregate + 1},
            {"channel.effective_maximum_workspace_bytes": aggregate - 1},
            {"channel.effective_maximum_workspace_bytes": 201_326_593},
            {"accounting.fixed_overhead_bytes": True},
            {"physical.table_workspace_bytes": 0},
        )
        for mutation in corruptions:
            with self.subTest(mutation=mutation), self.assertRaises(runner.CampaignError):
                runner._validate_two_hop_chain_workspace(*bad(**mutation))

    def test_vector_two_hop_episode_gate_is_explicit_and_confined(self):
        def row(scenario, *, mode="receive", control="wanted", cn0=30, seconds=64, requirement="required_complete"):
            result = {"id": scenario, "scenario": scenario, "mode": mode, "waveform": "reference",
                      "seed": 7, "message": "C", "cn0_db_hz": cn0,
                      "bit_seconds": seconds}
            if mode == "receive":
                result["control"] = control
                result["reception_requirement"] = requirement
            return result
        for scenario in runner.HF_VECTOR_TWO_HOP_SCENARIOS:
            runner._validate_episode(row(scenario), 0, set(), 900, 12_000_000)
        for scenario in ("hf-vector-two-hop", "hf-vector-two-hop-only", "hf-vector-one-hop-only",
                         "hf-vector-two-hop-holdout"):
            runner._validate_episode(row(scenario, mode="plan", requirement=None), 0, set(), 900, 12_000_000)
        runner._validate_episode(row("hf-vector-two-hop", cn0=20, requirement="characterization"),
                                 0, set(), 900, 12_000_000)
        runner._validate_episode(row("hf-vector-two-hop", cn0=17, seconds=128, requirement="characterization"),
                                 0, set(), 900, 12_000_000)
        for cn0, seconds in ((0, 64), (-3, 128)):
            receive = row("hf-vector-two-hop", cn0=cn0, seconds=seconds,
                          requirement="characterization")
            receive.update(message="E", seed=73613, noise_seed=93613)
            runner._validate_episode(receive, 0, set(), 900, 12_000_000)
        for cn0, seconds in ((0, 64), (-3, 128)):
            plan = row("hf-vector-two-hop", mode="plan", cn0=cn0, seconds=seconds,
                       requirement=None)
            plan.update(message="E", seed=73613, noise_seed=93613)
            runner._validate_episode(plan, 0, set(), 900, 12_000_000)
        boundary_receives = {}
        for cn0, seconds in ((6, 64), (3, 128)):
            receive = row("hf-vector-two-hop", cn0=cn0, seconds=seconds,
                          requirement="characterization")
            receive.update(message="E", seed=73613, noise_seed=93613)
            runner._validate_episode(receive, 0, set(), 900, 12_000_000)
            boundary_receives[cn0] = receive
            plan = row("hf-vector-two-hop", mode="plan", cn0=cn0, seconds=seconds,
                       requirement=None)
            plan.update(message="E", seed=73613, noise_seed=93613)
            runner._validate_episode(plan, 0, set(), 900, 12_000_000)
        for control in ("noise", "wrong-key"):
            runner._validate_episode(row("hf-vector-two-hop", control=control, requirement="negative_control"),
                                     0, set(), 900, 12_000_000)
        runner._validate_episode(row("hf-vector-two-hop", control="noise", cn0=0,
                                     requirement="negative_control"), 0, set(), 900, 12_000_000)
        runner._validate_episode(row("hf-vector-two-hop", control="wrong-key", cn0=-3, seconds=128,
                                     requirement="negative_control"), 0, set(), 900, 12_000_000)
        invalid = (
            row("hf-vector-two-hop", cn0=20, requirement="required_complete"),
            row("hf-vector-two-hop", cn0=17, seconds=128, requirement="required_complete"),
            dict(row("hf-vector-two-hop", mode="channel", cn0=6, requirement=None), message="E", seed=73613,
                 noise_seed=93613),
            dict(row("hf-vector-two-hop", mode="channel", cn0=3, seconds=128, requirement=None), message="E",
                 seed=73613, noise_seed=93613),
            dict(row("hf-vector-two-hop", mode="channel", cn0=0, requirement=None), message="E", seed=73613,
                 noise_seed=93613),
            dict(row("hf-vector-two-hop", mode="channel", cn0=-3, seconds=128, requirement=None), message="E",
                 seed=73613, noise_seed=93613),
            dict(row("hf-vector-two-hop", mode="plan", cn0=0, requirement=None), message="B", seed=73613,
                 noise_seed=93613),
            dict(row("hf-vector-two-hop", mode="plan", cn0=-3, seconds=128, requirement=None), message="E",
                 seed=73613, noise_seed=93612),
            dict(row("hf-vector-two-hop", mode="plan", cn0=0, requirement=None), message="E", seed=7,
                 noise_seed=93613),
            dict(row("hf-vector-two-hop", mode="receive", cn0=0, requirement="characterization"),
                 message="B", seed=73613, noise_seed=93613),
            dict(row("hf-vector-two-hop", mode="plan", cn0=6, requirement=None), message="A", seed=73613,
                 noise_seed=93613),
            dict(row("hf-vector-two-hop", mode="plan", cn0=3, seconds=128, requirement=None), message="E",
                 seed=73612, noise_seed=93613),
            dict(row("hf-vector-two-hop", mode="plan", cn0=6, requirement=None), message="E", seed=73613,
                 noise_seed=93612),
            dict(boundary_receives[6], control="noise", reception_requirement="negative_control"),
            dict(boundary_receives[6], control="wrong-key", reception_requirement="negative_control"),
            dict(boundary_receives[3], control="noise", reception_requirement="negative_control"),
            dict(boundary_receives[3], control="wrong-key", reception_requirement="negative_control"),
            dict(boundary_receives[6], reception_requirement="required_complete"),
            row("hf-vector-two-hop", control="noise", cn0=0, requirement="characterization"),
            row("hf-vector-two-hop", control="noise", cn0=0, requirement="negative_control", seconds=128),
            row("hf-vector-two-hop", control="wrong-key", cn0=-3, seconds=64, requirement="negative_control"),
            row("hf-vector-two-hop", control="wrong-ordinal", cn0=0, requirement="negative_control"),
            row("hf-vector-two-hop-holdout", cn0=20, requirement="characterization"),
            row("hf-vector-two-hop-only", control="noise", requirement="negative_control"),
            row("hf-vector-two-hop", control="wrong-ordinal", requirement="negative_control"),
            row("hf-vector-two-hop-chain-off", cn0=20),
            row("hf-vector-two-hop", mode="hf-search"),
            dict(row("hf-vector-two-hop", mode="channel", requirement=None), control="noise"),
            row("hf-vector-two-hop", cn0=30, requirement="characterization"),
            row("hf-vector-two-hop", mode="plan", cn0=40, requirement=None),
        )
        for episode in invalid:
            with self.subTest(episode=episode), self.assertRaises(runner.CampaignError):
                runner._validate_episode(episode, 0, set(), 900, 12_000_000)

    def test_vector_two_hop_report_requires_hash_witnesses_and_numerical_error_budget(self):
        scenario = "hf-vector-two-hop"
        report = runner._vector_two_hop_spec(scenario)
        report.update({
            "table_sha256": "a" * 64,
            "table_digest_scope": "versioned moving-angle route-composed operator; exact binary64 complex coefficients for each retained lag and displacement knot in both route tables, plus all 9 x 9 x 98 x 3 moving-angle P/Q fit coefficients when F route is active and full selected geometry, reference range, retarded-time law, route and receive-axis metadata in canonical little-endian order",
            "qualification_scope": "finite-order vector Maxwell composition for the declared local two-Es ground-bounce and one-F corridors; finite RF, displacement, mesh/order, staged support and causal FIR witnesses qualify this fixed operator only, not all possible HF paths",
            "table_qualified": True, "carrier_qualified": True, "specified_fade_observed": False,
            "rf_solves": 502, "rf_points": 177, "matrix_products": 1,
            "two_hop_rf_points": 29, "one_hop_rf_points": 148,
            "moving_angle_fit_points": 81, "checked_moving_angle_witnesses": 32,
            "checked_moving_angle_refinement_points": 5,
            "checked_frequency_witnesses": 26, "checked_displacement_witnesses": 16,
            "scanned_displacements": 4097, "checked_fade_witnesses": 20,
            "checked_angular_witnesses": 0, "checked_staged_modes": 98,
            "checked_linear_velocity_witnesses": 18, "scanned_null_displacements": 4097,
            "checked_null_witnesses": 10,
            "null_witnesses": [{"offset_hz": f, "displacement_m": 0., "assembled_power": .02,
                                "direct_power": .02, "voltage_error": .0001}
                               for f in [-384., -360., -120., -112.5, -56.25, 0., 112.5, 120., 360., 384.]],
            "preparation_peak_workspace_bytes": 90 * 1024 * 1024, "maximum_rf_dense_terms": 1,
            "minimum_vacuum_cosine": .7, "maximum_flux_excess": 1e-10, "maximum_solve_residual": 1e-9,
            "maximum_ground_power": .8, "maximum_outer_order_flux": 1e-5,
            "one_hop_carrier_power": .2, "two_hop_carrier_power": .2,
            "two_hop_intermediate_nonspecular_flux": .01, "two_hop_intermediate_cross_plane_flux": .01,
            "maximum_mesh_voltage_error": .001, "maximum_refined_order_voltage_error": .001,
            "maximum_frequency_voltage_error": .0001, "maximum_fir_voltage_error": .0001,
            "two_hop_voltage_errors": {
                "mesh_voltage_error": .001, "staged_mesh_voltage_error": .001,
                "refined_order_voltage_error": .001, "staged_refined_order_voltage_error": .001,
                "frequency_voltage_error": .0001, "staged_frequency_voltage_error": 0.,
            },
            "one_hop_voltage_errors": {
                "mesh_voltage_error": .001, "staged_mesh_voltage_error": .001,
                "refined_order_voltage_error": .001, "staged_refined_order_voltage_error": .001,
                "frequency_voltage_error": .0001, "staged_frequency_voltage_error": .0001,
            },
            "physical_voltage_allowance": .0043,
            "temporal_fade": {
                "scope": "sampled compiled-FIR coefficient response over one output-time period; no between-sample duration guarantee",
                "evaluated": True, "sample_count": 4096, "sample_step_seconds": 540. / 4096,
                "voltage_allowance": .0056, "mean_incoherent_band_power": .1,
                "carrier": {"qualifying_samples": 100, "longest_run_samples": 10,
                            "longest_sample_span_seconds": 9 * 540. / 4096},
                "band": {"qualifying_samples": 50, "longest_run_samples": 5,
                         "longest_sample_span_seconds": 4 * 540. / 4096},
                "joint": {"qualifying_samples": 20, "longest_run_samples": 3,
                          "longest_sample_span_seconds": 2 * 540. / 4096},
            },
            "maximum_displacement_tap_error": .0001, "maximum_discarded_energy_fraction": 1e-8,
            "minimum_carrier_power": .02, "minimum_incoherent_carrier_power": .1,
            "minimum_band_power": .02, "minimum_band_incoherent_power": .1,
            "average_incoherent_band_power": .1, "carrier_fade_displacement_m": -100.,
            "band_fade_displacement_m": 100., "relative_voltage_error_at_witness_minimum": .01,
            "retarded_peak_error_bound": .001, "maximum_staged_support_error": .0001,
            "maximum_linear_velocity_voltage_error": .0005, "maximum_band_quadrature_power_error": .0001,
            "maximum_angular_freezing_voltage_error": .9,
            "maximum_moving_angle_voltage_error": .0001,
            "maximum_moving_angle_staged_voltage_error": .0001,
            "maximum_carrier_shift_voltage_error": .0005,
            "maximum_staged_frequency_voltage_error": .0001,
            "minimum_assembled_null_power": .02, "minimum_direct_null_power": .02,
            "maximum_longitudinal_footprint_offset_m": 1000., "maximum_cross_plane_footprint_offset_m": 1000.,
            "table_workspace_bytes": 1024, "stream_workspace_bytes": 1024,
            "moving_angle_fit_coefficient_count": 23814, "preparation_seconds": .5,
            "source_minimum_frequency_hz": -362.5, "source_maximum_frequency_hz": 362.5,
            "incident_energy_sample_sum": 100., "selected_energy_sample_sum": 1.,
        })
        runner._validate_vector_two_hop_report(report, scenario)
        held_temporal = dict(report["temporal_fade"], sample_step_seconds=470. / 4096)
        held_temporal["carrier"] = dict(report["temporal_fade"]["carrier"],
                                        longest_sample_span_seconds=9 * 470. / 4096)
        held_temporal["band"] = dict(report["temporal_fade"]["band"],
                                     longest_sample_span_seconds=4 * 470. / 4096)
        held_temporal["joint"] = dict(report["temporal_fade"]["joint"],
                                      longest_sample_span_seconds=2 * 470. / 4096)
        runner._validate_vector_two_hop_temporal_fade(
            held_temporal, "hf-vector-two-hop-holdout", .0056)
        with self.assertRaises(runner.CampaignError):
            runner._validate_vector_two_hop_temporal_fade(
                report["temporal_fade"], "hf-vector-two-hop-holdout", .0056)
        one_hop_scenario = "hf-vector-one-hop-only"
        one_hop_report = dict(report)
        one_hop_report.update(runner._vector_two_hop_spec(one_hop_scenario))
        one_hop_report.update(
            rf_points=148, rf_solves=444, two_hop_rf_points=0, one_hop_rf_points=148,
            checked_frequency_witnesses=13, maximum_rf_dense_terms=1_200_000_000,
            two_hop_voltage_errors={key: 0. for key in report["two_hop_voltage_errors"]},
            two_hop_carrier_power=0., two_hop_intermediate_nonspecular_flux=0.,
            two_hop_intermediate_cross_plane_flux=0., physical_voltage_allowance=.0022,
            temporal_fade={
                "scope": "sampled compiled-FIR coefficient response over one output-time period; no between-sample duration guarantee",
                "evaluated": False, "sample_count": 0, "sample_step_seconds": 0.,
                "voltage_allowance": 0., "mean_incoherent_band_power": 0.,
                "carrier": {"qualifying_samples": 0, "longest_run_samples": 0,
                            "longest_sample_span_seconds": 0.},
                "band": {"qualifying_samples": 0, "longest_run_samples": 0,
                         "longest_sample_span_seconds": 0.},
                "joint": {"qualifying_samples": 0, "longest_run_samples": 0,
                          "longest_sample_span_seconds": 0.},
            },
        )
        runner._validate_vector_two_hop_report(one_hop_report, one_hop_scenario)
        conservative_fade = dict(report, minimum_carrier_power=1e-5,
                                 minimum_incoherent_carrier_power=.2,
                                 minimum_band_power=.001,
                                 minimum_band_incoherent_power=.2,
                                 average_incoherent_band_power=.2,
                                 specified_fade_observed=True)
        runner._validate_vector_two_hop_report(conservative_fade, scenario)
        # The physical budget sums both enabled route errors. Charging each
        # category only once from global maxima would understate this case:
        # it would accept the carrier fade while the correct route sum rejects it.
        route_sum_case = dict(
            report,
            two_hop_voltage_errors={
                "mesh_voltage_error": .004, "staged_mesh_voltage_error": .004,
                "refined_order_voltage_error": .004, "staged_refined_order_voltage_error": .004,
                "frequency_voltage_error": .0006, "staged_frequency_voltage_error": 0.,
            },
            one_hop_voltage_errors={
                "mesh_voltage_error": .004, "staged_mesh_voltage_error": .004,
                "refined_order_voltage_error": .004, "staged_refined_order_voltage_error": .004,
                "frequency_voltage_error": .0006, "staged_frequency_voltage_error": .0006,
            },
            maximum_mesh_voltage_error=.004,
            maximum_refined_order_voltage_error=.004,
            maximum_frequency_voltage_error=.0006,
            maximum_staged_frequency_voltage_error=.0006,
            maximum_moving_angle_voltage_error=.0006,
            maximum_moving_angle_staged_voltage_error=.0006,
            maximum_displacement_tap_error=.0012,
            maximum_fir_voltage_error=.0006,
            maximum_staged_support_error=.0012,
            retarded_peak_error_bound=.004,
            maximum_linear_velocity_voltage_error=.004,
            maximum_carrier_shift_voltage_error=.004,
            minimum_carrier_power=.01,
            minimum_incoherent_carrier_power=1.,
            minimum_band_power=.01,
            minimum_band_incoherent_power=1.,
            average_incoherent_band_power=1.,
            physical_voltage_allowance=.0178,
            temporal_fade={
                **report["temporal_fade"],
                "voltage_allowance": .0248,
            },
            specified_fade_observed=False,
        )
        runner._validate_vector_two_hop_report(route_sum_case, scenario)
        # The first native trial emitted the UTF-8 multiplication sign as
        # a pair of byte escapes. Require the exact current ASCII scope.
        byte_escaped_scope = report["table_digest_scope"].replace(" x ", "\xc3\x97")
        with self.assertRaisesRegex(runner.CampaignError, "qualified, source-bound route-composed table"):
            runner._validate_vector_two_hop_report({**report, "table_digest_scope": byte_escaped_scope}, scenario)
        bad_reports = (
            {**report, "table_sha256": "not-a-digest"},
            {**report, "rf_solves": 180},
            {**report, "maximum_rf_dense_terms": 12_000_000_001},
            {**report, "maximum_frequency_voltage_error": .0007},
            {**report, "maximum_staged_frequency_voltage_error": .0007},
            {**report, "maximum_displacement_tap_error": .0013},
            {**report, "maximum_moving_angle_voltage_error": .0007},
            {**report, "maximum_moving_angle_staged_voltage_error": .0007},
            {**report, "physical_voltage_allowance": .0022},
            {**report, "two_hop_voltage_errors": {
                **report["two_hop_voltage_errors"], "frequency_voltage_error": .0002}},
            {**report, "one_hop_voltage_errors": {
                **report["one_hop_voltage_errors"], "staged_mesh_voltage_error": .0041}},
            {**report, "temporal_fade": {**report["temporal_fade"], "sample_count": 4095}},
            {**report, "temporal_fade": {**report["temporal_fade"], "sample_step_seconds": 540. / 4095}},
            {**report, "temporal_fade": {**report["temporal_fade"], "voltage_allowance": .0043}},
            {**report, "temporal_fade": {**report["temporal_fade"], "joint": {
                **report["temporal_fade"]["joint"], "longest_sample_span_seconds": .01}}},
            {**report, "temporal_fade": {**report["temporal_fade"], "mean_incoherent_band_power": 1e-8}},
            {**report, "temporal_fade": {**report["temporal_fade"], "joint": {
                **report["temporal_fade"]["joint"], "qualifying_samples": 101}}},
            {**one_hop_report, "temporal_fade": {**one_hop_report["temporal_fade"], "evaluated": True}},
            {**route_sum_case, "specified_fade_observed": True},
            {**one_hop_report, "maximum_rf_dense_terms": 1_200_000_001},
            {**report, "checked_moving_angle_witnesses": 31},
            {**report, "moving_angle_fit_coefficient_count": 0},
            {**report, "preparation_peak_workspace_bytes": 97 * 1024 * 1024},
            {**report, "source_maximum_frequency_hz": 384.},
            {**report, "null_witnesses": report["null_witnesses"][:-1]},
            {**report, "unknown_field": 1},
            {**report, "specified_fade_observed": True},
        )
        for bad in bad_reports:
            with self.subTest(bad=bad), self.assertRaises(runner.CampaignError):
                runner._validate_vector_two_hop_report(bad, scenario)

    def test_vector_two_hop_renderer_is_rejected_when_unrequested(self):
        receipt = {"plan": {"channel_renderer": "hf_vector_two_hop", "vector_two_hop": {}},
                   "scenario": {"channel": {"renderer": "hf_vector_two_hop", "vector_two_hop": {}}},
                   "channel": {"vector_two_hop": {}}}
        with self.assertRaisesRegex(runner.CampaignError, "unrequested coupled vector two-hop"):
            runner._validate_vector_two_hop(receipt, {"scenario": "awgn"})
        requested = {"plan": {"vector_two_hop": {}}, "scenario": {"channel": {}}}
        with self.assertRaises(runner.CampaignError):
            runner._validate_vector_two_hop(requested, {"scenario": "hf-vector-two-hop"})

    def test_evolving_vector_private_energy_is_complete_aligned_disjoint_and_conservative(self):
        plan = {"public_slots": [{"observation": {"first": 0, "stop": 4}},
                                 {"observation": {"first": 4, "stop": 9}},
                                 {"observation": {"first": 9, "stop": 13}}]}
        scope = ("Evaluator-only per-public-slot sum of the propagated wanted complex voltage after physical fading "
                 "and before additive noise or frontend processing; intervals are not supplied to the receiver or decoder.")
        channel = {"private_signal_energy_scope": scope, "signal_energy_sample_sum": 16.,
                   "private_signal_energy_outside_observations_sample_sum": 2.,
                   "private_signal_energy_observations": [
                       {"public_slot": 0, "first_sample": 0, "stop_sample": 4, "samples": 4, "energy_sample_sum": 3.},
                       {"public_slot": 1, "first_sample": 4, "stop_sample": 9, "samples": 5, "energy_sample_sum": 8.},
                       {"public_slot": 2, "first_sample": 9, "stop_sample": 13, "samples": 4, "energy_sample_sum": 3.}]}
        runner._validate_vector_private_signal_energy(channel, plan)
        bad_cases = []
        bad = json.loads(json.dumps(channel)); bad["private_signal_energy_observations"].pop(); bad_cases.append(bad)
        bad = json.loads(json.dumps(channel)); bad["private_signal_energy_observations"][1]["first_sample"] = 3; bad_cases.append(bad)
        bad = json.loads(json.dumps(channel)); bad["private_signal_energy_observations"][1]["energy_sample_sum"] = float("nan"); bad_cases.append(bad)
        bad = json.loads(json.dumps(channel)); bad["private_signal_energy_observations"][2]["first_sample"] = 8; bad_cases.append(bad)
        bad = json.loads(json.dumps(channel)); bad["signal_energy_sample_sum"] = 15.; bad_cases.append(bad)
        bad = json.loads(json.dumps(channel)); bad["private_signal_energy_outside_observations_sample_sum"] = -1.; bad_cases.append(bad)
        bad = json.loads(json.dumps(channel)); bad["private_signal_energy_outside_observations_sample_sum"] = 1.; bad_cases.append(bad)
        for malformed in bad_cases:
            with self.subTest(malformed=malformed), self.assertRaises(runner.CampaignError):
                runner._validate_vector_private_signal_energy(malformed, plan)

    def test_vector_oblique_scope_keeps_band_and_receiver_controls_honest(self):
        base = {"id": "vector", "scenario": "hf-vector-oblique", "mode": "receive",
                "waveform": "reference", "seed": 1, "control": "wanted", "bit_seconds": 64,
                "cn0_db_hz": 0, "message": "A"}
        for scenario in (name for name in runner.HF_VECTOR_OBLIQUE_SCENARIOS
                         if name not in runner.HF_VECTOR_EVOLUTION_SCENARIOS and
                         name not in runner.HF_VECTOR_EVOLUTION_CHAIN_SCENARIOS and name != runner.HF_PROTECTED_OBLIQUE):
            runner._validate_episode(dict(base, scenario=scenario), 0, set(), 600, 5_000_000)
        runner._validate_episode(dict(base, bit_seconds=128, cn0_db_hz=-3), 0, set(), 600, 5_000_000)
        for cn0 in (20, 30):
            runner._validate_episode(dict(base, cn0_db_hz=cn0), 0, set(), 600, 5_000_000)
        for scenario in (name for name in runner.HF_VECTOR_CHAIN_SCENARIOS
                         if name not in runner.HF_VECTOR_EVOLUTION_CHAIN_SCENARIOS and name != runner.HF_PROTECTED_OBLIQUE):
            runner._validate_episode(dict(base, scenario=scenario, cn0_db_hz=30), 0, set(), 600, 5_000_000)
        diagnostic = dict(base, hf_target_presence_diagnostic=True, capture_digest_slot=0, cn0_db_hz=10)
        runner._validate_episode(diagnostic, 0, set(), 600, 5_000_000)
        for change in ({"scenario": "hf-vector-oblique-chain-off"}, {"control": "noise"},
                       {"scenario": "hf-vector-oblique-holdout"}, {"helper": True}):
            with self.subTest(diagnostic_change=change), self.assertRaises(runner.CampaignError):
                runner._validate_episode(dict(diagnostic, **change), 0, set(), 600, 5_000_000)
        for change in ({"helper": True}, {"backend": "cuda"}, {"fec": "rs20"},
                       {"hf_correlated_gain_receiver": True}, {"hf_phase_filter_receiver": True},
                       {"duration_seconds": 30}, {"bit_seconds": 192}, {"cn0_db_hz": 40},
                       {"mode": "hf-search"}, {"control": "wrong-ordinal"},
                       {"scenario": "hf-vector-oblique-chain-clocks", "bit_seconds": 128},
                       {"scenario": "hf-vector-oblique-chain-clocks-tone", "cn0_db_hz": 10}):
            with self.subTest(change=change), self.assertRaises(runner.CampaignError):
                runner._validate_episode(dict(base, **change), 0, set(), 600, 5_000_000)

    def test_protected_oblique_only_admits_the_frozen_paired_short_profile(self):
        app = Path(__file__).resolve().parents[1]
        cases = []
        for name in ("discriminator", "preservation"):
            campaign = runner._read_campaign(app / f"campaigns/hf-protected-oblique-{name}.json")
            cases.extend(campaign["episodes"])
        self.assertEqual([(case["message"], case["control"], case["noise_seed"]) for case in cases],
                         [("e", "wanted", 80501), ("e", "wrong-key", 80501),
                          ("a", "wanted", 80547), ("e", "noise", 80501)])
        for change in ({"scenario": "hf-vector-oblique-chain-clocks-tone"}, {"hf_predictive_quiet": False},
                       {"fec": "off"}, {"fec": "rs60"}, {"message": "K"}, {"noise_seed": 80547},
                       {"bit_seconds": 64}, {"cn0_db_hz": 0}, {"helper": True}, {"seed": 70502}):
            with self.subTest(change=change), self.assertRaises(runner.CampaignError):
                runner._validate_episode(dict(cases[0], **change), 0, set(), 900, 12_000_000)

    def test_vector_report_rejects_unqualified_or_mislabeled_physics(self):
        # Schema fixture only; these invented values are not physical evidence.
        report = runner._vector_oblique_spec("hf-vector-oblique")
        for key in ("maximum_flux_excess", "maximum_solve_residual", "mesh_voltage_error",
                    "refined_order_voltage_error", "maximum_edge_flux", "frequency_voltage_error",
                    "fir_voltage_error", "period_averaged_discarded_energy_fraction",
                    "interpolation_peak_error_bound", "retarded_peak_error_bound",
                    "relative_voltage_error_at_minimum", "order_voltage_error",
                    "period_averaged_raw_precursor_energy_fraction", "minimum_carrier_position_m"):
            report[key] = 0.
        report.update(table_qualified=True, table_sha256="0" * 64,
            table_digest_scope="all 257 translating-position knots, all 129 retained lags, binary64 complex coefficients and knot slopes in canonical little-endian order; source identity binds generation and interpolation",
            qualification_scope="finite-order bidirectional 2D cold collisional Maxwell scattering with periodic transverse density, oblique incidence and fixed antenna projection; limited RF/position/order/FIR witnesses do not establish random 3D fields, field calibration or environmental probabilities",
            rf_solves=29, output_half_orders=24, checked_frequencies=13, checked_positions=2048,
            checked_table_assembly_positions=54, table_assembly_voltage_error=0.,
            matrix_products=1, minimum_vacuum_cosine=.5, minimum_carrier_power=.01,
            maximum_carrier_power=1., certified_minimum_power_lower_bound=.009,
            minimum_checked_band_power_lower_bound=.008, minimum_checked_band_power_witness_offset_hz=120.,
            carrier_reflected_flux=.7, carrier_transmitted_flux=.2, carrier_scattered_reflected_flux=.3,
            carrier_crosspolar_reflected_flux=.8, carrier_crosspolar_scattered_flux=.6,
            preparation_peak_workspace_bytes=12 * 1024**2, table_workspace_bytes=1024,
            stream_workspace_bytes=256, preparation_seconds=1., source_minimum_frequency_hz=-112.5,
            source_maximum_frequency_hz=112.5, incident_energy_sample_sum=100., selected_energy_sample_sum=50.)
        # Pure-TE cross-polar flux may exceed mixed-launch flux: different inputs.
        runner._validate_vector_oblique_report(report, "hf-vector-oblique")
        for key, value in (("table_qualified", False), ("checked_frequencies", 5),
                ("output_half_orders", 8), ("maximum_edge_flux", 6e-5),
                ("checked_table_assembly_positions", 0), ("table_assembly_voltage_error", 1e-8),
                ("period_averaged_discarded_energy_fraction", 2e-6),
                ("minimum_checked_band_power_witness_offset_hz", 960.),
                ("minimum_checked_band_power_lower_bound", .02),
                ("relative_voltage_error_at_minimum", .01), ("magnetic", False),
                ("source_maximum_frequency_hz", 385.),
                ("preparation_peak_workspace_bytes", 17 * 1024**2)):
            with self.subTest(field=key), self.assertRaises(runner.CampaignError):
                runner._validate_vector_oblique_report(dict(report, **{key: value}), "hf-vector-oblique")
        legacy = dict(report)
        legacy["maximum_discarded_energy_fraction"] = legacy.pop("period_averaged_discarded_energy_fraction")
        with self.assertRaises(runner.CampaignError):
            runner._validate_vector_oblique_report(legacy, "hf-vector-oblique")

    def test_campaigns_are_finite_and_well_formed(self) -> None:
        for path in sorted((RUNNER.parent / "campaigns").glob("*.json")):
            campaign = runner._read_campaign(path)
            self.assertLessEqual(len(campaign["episodes"]), campaign["max_trials"])
            self.assertGreaterEqual(campaign["max_wall_seconds"], 1)

    def test_vector_two_hop_campaigns_stop_on_precompletion_execution_failure(self) -> None:
        for name in ("hf-vector-two-hop-receiver.json", "hf-vector-two-hop-chain.json",
                     "hf-vector-two-hop-chain-off.json"):
            with self.subTest(name=name):
                campaign = runner._read_campaign(RUNNER.parent / "campaigns" / name)
                self.assertIs(campaign.get("stop_on_execution_failure"), True)

    def test_vector_two_hop_sensitivity_campaign_contains_only_frozen_new_cells(self) -> None:
        campaign = runner._read_campaign(RUNNER.parent / "campaigns" / "hf-vector-two-hop-sensitivity.json")
        cells = [(episode["control"], episode["cn0_db_hz"], episode["bit_seconds"],
                  episode.get("reception_requirement")) for episode in campaign["episodes"]]
        self.assertEqual(cells, [
            ("wanted", 0, 64, "characterization"),
            ("wanted", -3, 128, "characterization"),
            ("noise", 0, 64, "negative_control"),
            ("wrong-key", -3, 128, "negative_control"),
        ])

    def test_stop_on_execution_failure_is_optional_and_strict_boolean(self) -> None:
        campaign = valid_campaign()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            path.write_text(json.dumps(campaign))
            self.assertNotIn("stop_on_execution_failure", runner._read_campaign(path))
            campaign["stop_on_execution_failure"] = False
            path.write_text(json.dumps(campaign))
            self.assertIs(runner._read_campaign(path)["stop_on_execution_failure"], False)
            for invalid in (None, 0, 1, "true", []):
                campaign["stop_on_execution_failure"] = invalid
                path.write_text(json.dumps(campaign))
                with self.subTest(value=invalid), self.assertRaisesRegex(runner.CampaignError, "must be a boolean"):
                    runner._read_campaign(path)

    def test_stop_on_predictive_regression_is_optional_and_strict_boolean(self) -> None:
        campaign = valid_campaign()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            for value in (False, True):
                campaign["stop_on_predictive_regression"] = value
                path.write_text(json.dumps(campaign))
                self.assertIs(runner._read_campaign(path)["stop_on_predictive_regression"], value)
            for invalid in (None, 0, 1, "true", []):
                campaign["stop_on_predictive_regression"] = invalid
                path.write_text(json.dumps(campaign))
                with self.subTest(value=invalid), self.assertRaisesRegex(runner.CampaignError, "must be a boolean"):
                    runner._read_campaign(path)

    def test_service_replay_inputs_and_argv_are_explicitly_bounded(self) -> None:
        episode = {"id": "service", "scenario": "awgn", "mode": "receive", "waveform": "reference", "seed": 1,
                   "service_replay": True, "service_episodes": 2, "service_spool_byte_limit": 170000,
                   "service_backlog_begin_slot": 1, "service_backlog_end_slot": 5, "chunk": 1024, "owners": 2}
        for mode in ("receive", "plan"):
            for control in ("wanted", "noise", "wrong-key", "wrong-ordinal"):
                declared = dict(episode, mode=mode, control=control)
                runner._validate_episode(declared, 0, set(), 10, 10000)
                command = runner._command(Path("/native/probe"), declared, 10000, 2)
                for field in runner.SERVICE_FIELDS:
                    self.assertIn("--" + field.replace("_", "-"), command)
                runner._validate_completed_receipt(fake_completed_receipt(command), declared)
        for overrides in ({"service_replay": 1}, {"service_replay": False}, {"service_episodes": 3},
                {"service_episodes": True}, {"service_spool_byte_limit": 0}, {"service_spool_byte_limit": 2**64},
                {"service_backlog_begin_slot": -1}, {"service_backlog_end_slot": 1}, {"chunk": 4097}, {"owners": 0}, {"owners": 65537},
                {"mode": "slot"}, {"mode": "hf-search"}, {"control": "eof"}, {"control": "capture-gap"}):
            with self.subTest(overrides=overrides), self.assertRaises(runner.CampaignError):
                runner._validate_episode(dict(episode, **overrides), 0, set(), 10, 10000)
        unpaired = dict(episode)
        del unpaired["service_backlog_begin_slot"]
        with self.assertRaises(runner.CampaignError):
            runner._validate_episode(unpaired, 0, set(), 10, 10000)
        ordinary = {key: value for key, value in episode.items() if key not in runner.SERVICE_FIELDS}
        ordinary["service_episodes"] = 1
        with self.assertRaises(runner.CampaignError):
            runner._validate_episode(ordinary, 0, set(), 10, 10000)

    def test_predictive_quiet_input_scope_preserves_physical_bounds(self) -> None:
        episode = {"id": "predictive", "scenario": "awgn", "mode": "receive", "waveform": "reference", "seed": 1,
                   "hf_predictive_quiet": True, "service_spool_byte_limit": 170000, "owners": 2}
        for mode in ("receive", "plan"):
            declared = dict(episode, mode=mode)
            runner._validate_episode(declared, 0, set(), 10, 10000)
            command = runner._command(Path("/native/probe"), declared, 10000, 2)
            self.assertIn("--hf-predictive-quiet", command)
            self.assertNotIn("--service-replay", command)
            runner._validate_completed_receipt(fake_completed_receipt(command), declared)
        for override in ({"hf_predictive_quiet": 1}, {"helper": True}, {"backend": "cuda"},
                         {"mode": "slot"}, {"waveform": "smoke"}, {"fec": "rs20"}, {"service_episodes": 2},
                         {"service_replay": True}, {"hf_compare_admitted_models": True},
                         {"hf_correlated_gain_receiver": True}, {"chunk": 4097}):
            with self.subTest(override=override), self.assertRaises(runner.CampaignError):
                runner._validate_episode(dict(episode, **override), 0, set(), 10, 10000)
        physical = dict(episode, scenario="hf-vector-oblique-chain-clocks-tone", cn0_db_hz=30, bit_seconds=64)
        runner._validate_episode(physical, 0, set(), 10, 10000)
        with self.assertRaises(runner.CampaignError):
            runner._validate_episode(dict(physical, bit_seconds=128), 0, set(), 10, 10000)

    def test_predictive_quiet_pair_rejects_nonmatched_or_changed_data(self) -> None:
        episode = {"id": "predictive", "scenario": "awgn", "mode": "receive", "waveform": "reference", "seed": 1,
                   "hf_predictive_quiet": True}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 10000, 2))
        runner._validate_completed_receipt(receipt, episode)
        mutations = (((), "rendered_once", False), ((), "truth_information_passed_to_receiver", True),
                     ((), "status", "error"), ((), "common_render_wall_seconds", 2.), (("candidate",), "source_identity", {}),
                     (("candidate", "timing"), "wall_includes_shared_render_per_policy", False),
                     (("candidate", "receiver"), "predictive_training_scanned_samples", 0),
                     (("candidate", "receiver"), "predictive_training_scheduling", "concurrent_with_receiver"),
                     (("candidate", "channel"), "input_sha256", "e" * 64),
                     (("candidate", "plan"), "hf_predictive_quiet_active", False),
                     (("candidate", "plan"), "public_bounds", {"different": True}),
                     (("candidate", "slots", 0), "bit_evidence", [-3., 32.]),
                     (("candidate", "slots", 0), "decision", "quiet"),
                     (("candidate", "slots", 0), "predictive_quiet", None))
        for path, key, value in mutations:
            changed = json.loads(json.dumps(receipt))
            target = changed["hf_predictive_quiet_comparison"]
            for segment in path:
                target = target[segment]
            target[key] = value
            with self.subTest(path=path, key=key), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(changed, episode)
        missing = json.loads(json.dumps(receipt))
        del missing["hf_predictive_quiet_comparison"]
        with self.assertRaises(runner.CampaignError):
            runner._validate_completed_receipt(missing, episode)
        nested = json.loads(json.dumps(receipt))
        nested["hf_predictive_quiet_comparison"]["candidate"]["hf_predictive_quiet_comparison"] = {}
        with self.assertRaises(runner.CampaignError):
            runner._validate_completed_receipt(nested, episode)
        ordinary = dict(episode, hf_predictive_quiet=False)
        with self.assertRaises(runner.CampaignError):
            runner._validate_completed_receipt(receipt, ordinary)

    def test_predictive_quiet_acceptance_requires_causal_disjoint_guards(self) -> None:
        plan = {"public_slots": [{"observation": {"first": 200 * i, "stop": 200 * i + 100}} for i in range(4)]}
        baseline = {"index": 3, "first_sample": 600, "stop_sample": 700, "quiet_first_sample": 600,
                    "quiet_stop_sample": 700, "decision": "ambiguous", "complete": True, "all_valid": True,
                    "operational_log_e": 1., "operational_threshold": 21., "quiet_energy": 100., "quiet_threshold": 10.}
        proposed = dict(baseline, decision="quiet", quiet_energy=9.,
                        reason="scored_public_quiet_with_independent_line_prediction")
        diagnostic = fake_predictive_diagnostic(baseline)
        diagnostic.update(attempted=True, accepted=True, training_status="validated_prior_stationary_line",
            status="predictive_quiet_passed_both_bands", frequency_first={"first": 110, "stop": 190},
            frequency_second={"first": 310, "stop": 390}, amplitude={"first": 510, "stop": 564},
            validation={"first": 564, "stop": 590}, amplitude_magnitude=.3,
            frequency_first_proposal={"first": 110, "stop": 130}, frequency_first_confirmation={"first": 130, "stop": 190},
            frequency_second_proposal={"first": 310, "stop": 330}, frequency_second_confirmation={"first": 330, "stop": 390},
            frequency_lower_hz=-.0001, frequency_upper_hz=.0001, frequency_confidence_exclusion_probability=1e-6,
            frequency_coherent_nodes=9, frequency_band_nodes=9, coherent_interpolation_norm=.01, band_interpolation_norm=.01,
            validation_line_residual=1., validation_energy=9., validation_threshold=10.,
            line_direction_gain_squared=54 / 154, coherent_residual=1., coherent_threshold=21.,
            legacy_energy=9., legacy_threshold=10., legacy_modes=10,
            expanded_energy=9., expanded_threshold=11., expanded_modes=11)
        runner._validate_predictive_slot(diagnostic, baseline, proposed, plan, True)
        for key, value in (("accepted", False), ("attempted", False), ("training_status", "invalid_public_training_gap"),
                           ("amplitude", {"first": 380, "stop": 550}), ("validation", {"first": 550, "stop": 601}),
                           ("frequency_first", {"first": 50, "stop": 190}), ("line_direction_gain_squared", 0.),
                           ("coherent_residual", 22.), ("coherent_threshold", 22.),
                           ("legacy_threshold", 11.), ("expanded_energy", 12.), ("raw_quiet_energy", 90.),
                           ("validation_line_residual", 22.), ("legacy_modes", 0),
                           ("frequency_confidence_exclusion_probability", 1e-5), ("frequency_upper_hz", .003),
                           ("frequency_band_nodes", 8), ("frequency_coherent_nodes", 10), ("band_interpolation_norm", 4.),
                           ("frequency_first_proposal", {"first": 110, "stop": 131}),
                           ("frequency_second_confirmation", {"first": 329, "stop": 390})):
            with self.subTest(field=key), self.assertRaises(runner.CampaignError):
                runner._validate_predictive_slot(dict(diagnostic, **{key: value}), baseline, proposed, plan, True)

    def test_predictive_frequency_selection_is_fixed_by_public_confirmation_geometry(self) -> None:
        def geometry(period=200, width=100, fs=10):
            return {"sample_rate_hz": fs, "public_slots": [
                {"observation": {"first": period*i, "stop": period*i+width},
                 "public_profile_observation": {"first": period*i, "stop": period*i+width},
                 "previous_signal_stop": period*(i-1)+width if i else 0,
                 "next_signal_first": period*(i+1)} for i in range(6)]}
        plan = geometry()
        self.assertEqual(runner._predictive_frequency_selection(plan, 4), (2, "adjacent_gaps"))
        self.assertEqual(runner._predictive_frequency_selection(plan, 5), (1, "separated_gaps"))
        self.assertEqual(runner._predictive_frequency_selection(geometry(period=2000, width=1000), 5), (3, "adjacent_gaps"))
        missing_long_gap = json.loads(json.dumps(plan))
        missing_long_gap["public_slots"][1]["previous_signal_stop"] = 200
        self.assertEqual(runner._predictive_frequency_selection(missing_long_gap, 5), (3, "adjacent_gaps"))
        limited = geometry(period=200000, width=100000, fs=1920)
        self.assertEqual(runner._predictive_public_gap(limited, 1), {"first": 200000-65536, "stop": 200000})
        # Scored observations cannot replace the original PublicProfile bounds.
        distinct = json.loads(json.dumps(plan))
        distinct["public_slots"][0]["observation"] = {"first": 0, "stop": 150}
        self.assertEqual(runner._predictive_public_gap(distinct, 1), {"first": 100, "stop": 200})
        del distinct["public_slots"][0]["public_profile_observation"]
        with self.assertRaises(runner.CampaignError):
            runner._predictive_frequency_selection(distinct, 5)

    def test_predictive_separated_baseline_cannot_fall_back_after_failed_calibration(self) -> None:
        plan = {"sample_rate_hz": 10, "public_slots": [
            {"observation": {"first": i*200, "stop": i*200+100},
             "public_profile_observation": {"first": i*200, "stop": i*200+100},
             "previous_signal_stop": (i-1)*200+100 if i else 0, "next_signal_first": (i+1)*200}
            for i in range(6)]}
        baseline = {"index": 5, "first_sample": 1000, "stop_sample": 1100, "quiet_first_sample": 1000,
                    "quiet_stop_sample": 1100, "decision": "ambiguous", "complete": True, "all_valid": True,
                    "operational_log_e": 1., "operational_threshold": 21., "quiet_energy": 100., "quiet_threshold": 10.}
        proposed = dict(baseline, decision="quiet", quiet_energy=9.,
                        reason="scored_public_quiet_with_independent_line_prediction")
        diagnostic = fake_predictive_diagnostic(baseline)
        diagnostic.update(attempted=True, accepted=True, training_status="validated_prior_stationary_line",
            status="predictive_quiet_passed_both_bands", frequency_first={"first": 100, "stop": 200},
            frequency_second={"first": 700, "stop": 800}, amplitude={"first": 900, "stop": 967},
            validation={"first": 967, "stop": 1000}, amplitude_magnitude=.3,
            frequency_first_proposal={"first": 100, "stop": 125}, frequency_first_confirmation={"first": 125, "stop": 200},
            frequency_second_proposal={"first": 700, "stop": 725}, frequency_second_confirmation={"first": 725, "stop": 800},
            frequency_lower_hz=-.0001, frequency_upper_hz=.0001, frequency_confidence_exclusion_probability=1e-6,
            frequency_coherent_nodes=9, frequency_band_nodes=9, coherent_interpolation_norm=.01, band_interpolation_norm=.01,
            validation_line_residual=1., validation_energy=9., validation_threshold=10.,
            line_direction_gain_squared=67 / 167, coherent_residual=1., coherent_threshold=21.,
            legacy_energy=9., legacy_threshold=10., legacy_modes=10, expanded_energy=9., expanded_threshold=11., expanded_modes=11,
            frequency_first_gap_slot=1, frequency_second_gap_slot=4, frequency_baseline="separated_gaps",
            frequency_consistency="compatible_short_tube")
        rule = "public_confirmation_centroid_360s_v1"
        runner._validate_predictive_slot(diagnostic, baseline, proposed, plan, True, frequency_rule=rule)
        allowed_ambiguity = dict(diagnostic, frequency_consistency="no_unique_short_tube")
        runner._validate_predictive_slot(allowed_ambiguity, baseline, proposed, plan, True, frequency_rule=rule)
        for key, value in (("frequency_first_gap_slot", 3), ("frequency_baseline", "adjacent_gaps"),
                           ("frequency_consistency", "disjoint_short_tube"), ("frequency_consistency", "not_applicable")):
            with self.subTest(field=key, value=value), self.assertRaises(runner.CampaignError):
                runner._validate_predictive_slot(dict(diagnostic, **{key: value}), baseline, proposed, plan, True, frequency_rule=rule)
        fallback = dict(diagnostic, attempted=False, accepted=False, frequency_first_gap_slot=3,
                        frequency_baseline="adjacent_gaps", training_status="no_clean_coherent_frequency_gaps",
                        status="canonical_decision_retained", frequency_consistency="not_applicable")
        with self.assertRaisesRegex(runner.CampaignError, "score-dependent fallback"):
            runner._validate_predictive_slot(fallback, baseline, baseline, plan, False, frequency_rule=rule)
        with self.assertRaisesRegex(runner.CampaignError, "historical adjacent-only"):
            runner._validate_predictive_slot(diagnostic, baseline, proposed, plan, True)
        with self.assertRaisesRegex(runner.CampaignError, "unknown predictive"):
            runner._validate_predictive_slot(diagnostic, baseline, proposed, plan, True, frequency_rule="unknown")
        three = dict(diagnostic)
        del three["frequency_consistency"]
        three.update(frequency_interval_rule="fixed_three_witness_intersection_v1", frequency_middle_gap_slot=3,
            frequency_middle={"first": 500, "stop": 600}, frequency_middle_proposal={"first": 500, "stop": 525},
            frequency_middle_confirmation={"first": 525, "stop": 600}, frequency_witness_count=3,
            frequency_older_pair_intervals=2, frequency_adjacent_pair_intervals=2, frequency_joint_intervals=1)
        three_rule = "public_confirmation_centroid_360s_three_witness_v2"
        # Each pair may be ambiguous before their merged intersection becomes
        # unique. Requiring pair uniqueness would wrongly reject this case.
        runner._validate_predictive_slot(three, baseline, proposed, plan, True, frequency_rule=three_rule)
        for key, value in (("frequency_middle_gap_slot", 2), ("frequency_middle", {"first": 0, "stop": 0}),
                           ("frequency_middle_confirmation", {"first": 524, "stop": 600}), ("frequency_witness_count", 2),
                           ("frequency_older_pair_intervals", 0), ("frequency_adjacent_pair_intervals", 0),
                           ("frequency_joint_intervals", 2), ("frequency_older_pair_intervals", 129),
                           ("frequency_joint_intervals", 256), ("frequency_interval_rule", "unknown"),
                           ("frequency_consistency", "compatible_short_tube")):
            with self.subTest(three_witness_field=key), self.assertRaises(runner.CampaignError):
                runner._validate_predictive_slot(dict(three, **{key: value}), baseline, proposed, plan, True, frequency_rule=three_rule)
        incomplete = dict(three)
        del incomplete["frequency_middle_confirmation"]
        with self.assertRaises(runner.CampaignError):
            runner._validate_predictive_slot(incomplete, baseline, proposed, plan, True, frequency_rule=three_rule)
        with self.assertRaisesRegex(runner.CampaignError, "historical two-witness"):
            runner._validate_predictive_slot(three, baseline, proposed, plan, True, frequency_rule=rule)
        missing_middle_plan = json.loads(json.dumps(plan))
        missing_middle_plan["public_slots"][3]["previous_signal_stop"] = 600
        absent = dict(three, frequency_middle={"first": 0, "stop": 0},
                      frequency_middle_proposal={"first": 0, "stop": 0}, frequency_middle_confirmation={"first": 0, "stop": 0})
        with self.assertRaisesRegex(runner.CampaignError, "unique merged intersection"):
            runner._validate_predictive_slot(absent, baseline, proposed, missing_middle_plan, True, frequency_rule=three_rule)

    def test_predictive_three_witness_adjacent_and_unavailable_states_remain_explicit(self) -> None:
        diagnostic = {"frequency_interval_rule": "fixed_three_witness_intersection_v1", "frequency_middle_gap_slot": 0,
            "frequency_middle": {"first": 0, "stop": 0}, "frequency_middle_proposal": {"first": 0, "stop": 0},
            "frequency_middle_confirmation": {"first": 0, "stop": 0}, "frequency_witness_count": 2,
            "frequency_older_pair_intervals": 0, "frequency_adjacent_pair_intervals": 1, "frequency_joint_intervals": 1}
        runner._validate_predictive_three_witness(diagnostic, {}, 3, "adjacent_gaps", True)
        for key, value in (("frequency_witness_count", 3), ("frequency_older_pair_intervals", 1),
                           ("frequency_adjacent_pair_intervals", 2), ("frequency_middle_gap_slot", 1)):
            with self.subTest(field=key), self.assertRaises(runner.CampaignError):
                runner._validate_predictive_three_witness(dict(diagnostic, **{key: value}), {}, 3, "adjacent_gaps", True)
        unavailable = dict(diagnostic, frequency_witness_count=0, frequency_adjacent_pair_intervals=0, frequency_joint_intervals=0)
        runner._validate_predictive_three_witness(unavailable, {}, 0, "unavailable", False)
        episode = {"id": "predictive", "scenario": "awgn", "mode": "receive", "waveform": "reference", "seed": 1,
                   "hf_predictive_quiet": True}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 10000, 2))
        candidate = receipt["hf_predictive_quiet_comparison"]["candidate"]
        receipt["receiver"]["predictive_frequency_baseline_rule"] = "none"
        candidate["receiver"]["predictive_frequency_baseline_rule"] = "public_confirmation_centroid_360s_three_witness_v2"
        for slot in candidate["slots"]:
            slot["predictive_quiet"].update(unavailable, frequency_first_gap_slot=0,
                frequency_second_gap_slot=0, frequency_baseline="unavailable")
        runner._validate_completed_receipt(receipt, episode)
        candidate["receiver"]["predictive_frequency_baseline_rule"] = "public_confirmation_centroid_360s_v1"
        with self.assertRaises(runner.CampaignError):
            runner._validate_completed_receipt(receipt, episode)

    def test_service_receipt_binds_full_work_timing_hashes_and_reservations(self) -> None:
        episode = {"id": "service", "scenario": "awgn", "mode": "receive", "waveform": "reference", "seed": 1,
                   "service_replay": True, "service_episodes": 2}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 10000, 2))
        runner._validate_completed_receipt(receipt, episode)
        mutations = (
            (("plan", "service_replay", "episodes"), 1), (("plan", "service_replay", "slot_pattern"), "D" * 11),
            (("plan", "service_replay", "maximum_spool_bytes"), 169999),
            (("plan", "service_replay", "expected_message_intervals", 1, "first_public_slot"), 0),
            (("plan", "receiver_evidence_policy"), "compare_admitted"),
            (("receiver", "evidence_policy"), "compare_admitted"), (("receiver", "state"), "cancelled"),
            (("receiver", "next_public_slot"), 10), (("outcomes", "accepted_samples"), 1),
            (("slots", 1, "symbol_ordinal"), 0), (("slots", 1, "pattern_epoch"), 100),
            (("slots", 1, "all_valid"), False), (("slots", 1, "index"), 0),
            (("service_replay", "wall_seconds"), -1), (("service_replay", "process_cpu_seconds"), math.nan),
            (("service_replay", "spool_read_and_verification_seconds"), 3),
            (("service_replay", "spool_write_seconds"), 2),
            (("service_replay", "unique_baseband_seconds"), 1),
            (("service_replay", "service_seconds_per_baseband_second"), 0),
            (("service_replay", "verified_input_sha256"), "b" * 64),
            (("service_replay", "captured_input_sha256"), "b" * 64),
            (("service_replay", "digest_match"), False), (("service_replay", "replayed_samples"), 1),
            (("service_replay", "peak_active_owners"), 5), (("service_replay", "peak_reserved_bytes"), 1),
            (("service_replay", "receiver_memory_limit_bytes"), 1000000),
            (("service_replay", "spool_backing_kind"), "memory_filesystem"),
            (("service_replay", "workers_explicit"), True), (("service_replay", "capacity_qualified"), True),
            (("service_replay", "mean_busy_hardware_fraction"), 1),
            (("service_replay", "max_samples"), 9999), (("service_replay", "max_wall_seconds"), 1),
            (("service_replay", "communication_complete"), True), (("outcomes", "correct_complete"), True))
        for path, value in mutations:
            with self.subTest(path=path):
                bad = json.loads(json.dumps(receipt))
                parent = bad
                for key in path[:-1]:
                    parent = parent[key]
                parent[path[-1]] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(bad, episode)
        for kind in ("memory_filesystem", "unknown"):
            changed = json.loads(json.dumps(receipt))
            replay = changed["service_replay"]
            replay.update(spool_backing_kind=kind, spool_residency_reserved_bytes=65536,
                          receiver_memory_limit_bytes=990000 - 65536, peak_reserved_bytes=110000 + 65536)
            runner._validate_completed_receipt(changed, episode)
        unavailable_cpu = json.loads(json.dumps(receipt))
        for field in ("process_cpu_seconds", "spool_read_and_verification_process_cpu_seconds",
                      "render_process_cpu_seconds", "spool_write_process_cpu_seconds", "mean_busy_hardware_fraction"):
            unavailable_cpu["service_replay"][field] = None
        runner._validate_completed_receipt(unavailable_cpu, episode)

    def test_service_requires_both_global_message_endpoints(self) -> None:
        episode = {"id": "paired", "scenario": "awgn", "mode": "receive", "waveform": "reference", "seed": 1,
                   "service_replay": True, "service_episodes": 2}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 10000, 2))
        complete_service_messages(receipt)
        runner._validate_completed_receipt(receipt, episode)
        self.assertTrue(runner._communication_outcome(receipt, episode))
        self.assertFalse(receipt["outcomes"]["correct_complete"])
        for path, value in ((("messages",), receipt["messages"][:1]),
                (("messages", 1, "first_public_slot"), 0), (("messages", 1, "stop_public_slot"), 5),
                (("messages", 1, "decoded_payload_bytes"), [65]),
                (("messages", 1, "trailing_quiet_slots"), [8, 9]),
                (("messages", 1, "ambiguous_active_slots"), 1),
                (("outcomes", "correct_decoded_complete"), True), (("outcomes", "accepted_messages"), 1)):
            with self.subTest(path=path):
                bad = json.loads(json.dumps(receipt))
                parent = bad
                for key in path[:-1]:
                    parent = parent[key]
                parent[path[-1]] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(bad, episode)

    def test_service_backlog_requires_whole_replay_and_ready_input(self) -> None:
        episode = {"id": "backlog", "scenario": "awgn", "mode": "receive", "waveform": "reference", "seed": 1,
                   "service_replay": True, "service_episodes": 2, "service_backlog_begin_slot": 1, "service_backlog_end_slot": 5}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 10000, 2))
        runner._validate_completed_receipt(receipt, episode)
        for section, field, value in (("service_replay", "work_complete", False),
                ("first", "commit_input_stop", 128), ("first", "commit_uncommitted_slots", 4),
                ("last", "commit_wall_seconds", .01), ("backlog", "unique_baseband_seconds", 1),
                ("backlog", "process_cpu_seconds", 9), ("backlog", "busy_hardware_fraction", 1)):
            bad = json.loads(json.dumps(receipt))
            target = bad[section] if section == "service_replay" else bad["slots"][1 if section == "first" else 5] if section in {"first", "last"} else bad["service_replay"]["backlog_interval"]
            target[field] = value
            with self.subTest(section=section, field=field), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad, episode)
        partial = json.loads(json.dumps(receipt))
        partial["status"] = "time_limit"
        partial["outcomes"]["accepted_samples"] = 64
        partial["service_replay"].update(work_complete=False, communication_complete=False,
            replayed_samples=128, verified_input_sha256="b" * 64, digest_match=False,
            backlog_interval={"start_after_public_slot": 1, "stop_after_public_slot": 5, "complete": False,
                "reason": "whole_replay_incomplete", "process_cpu_seconds": None})
        runner._validate_service_replay(partial, episode)
        partial["service_replay"]["backlog_interval"]["wall_seconds"] = .1
        with self.assertRaises(runner.CampaignError):
            runner._validate_service_replay(partial, episode)
        del partial["service_replay"]["backlog_interval"]["wall_seconds"]
        partial["service_replay"].update(replayed_samples=0,
            verified_input_sha256=runner.hashlib.sha256(b"").hexdigest())
        partial["outcomes"]["accepted_samples"] = 0
        partial["slots"] = []
        runner._validate_service_replay(partial, episode)  # Capture remains complete; no replay work is claimed.

    def test_admitted_model_policy_is_explicit_and_receipt_bound(self) -> None:
        base = {"id": "comparison", "scenario": "hf-gui-native", "mode": "receive", "waveform": "reference", "seed": 1,
                "hf_compare_admitted_models": True}
        for mode in ("plan", "receive"):
            episode = dict(base, mode=mode)
            runner._validate_episode(episode, 0, set(), 10, 10000)
            command = runner._command(Path("/native/probe"), episode, 10000, 2)
            self.assertIn("--hf-compare-admitted-models", command)
            receipt = fake_completed_receipt(command)
            runner._validate_completed_receipt(receipt, episode)
            receipt["plan"]["receiver_evidence_policy"] = "fallback_only"
            with self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(receipt, episode)
        for override in ({"mode": "slot"}, {"mode": "gui-varying"}, {"backend": "cuda"},
                         {"hf_public_phase_receiver": True}, {"hf_compare_admitted_models": 1}):
            with self.subTest(override=override), self.assertRaises(runner.CampaignError):
                runner._validate_episode(dict(base, **override), 0, set(), 10, 10000)

    def test_phase_filter_inputs_and_fixed_mixture_receipts_are_strict(self) -> None:
        episode = {"id": "filter", "scenario": "awgn", "mode": "hf-search", "waveform": "reference", "seed": 1,
                   "hf_phase_refinement": True, "search_timing": "full", "hf_phase_filter": True}
        runner._validate_episode(episode, 0, set(), 10, 10000)
        command = runner._command(Path("/native/probe"), episode, 10000, 2)
        self.assertIn("--hf-phase-filter", command)
        receipt = fake_completed_receipt(command)
        runner._validate_completed_receipt(receipt, episode)
        for override in ({"mode": "receive"}, {"hf_phase_refinement": False}, {"search_timing": "selected"},
                         {"backend": "cuda"}, {"helper": True}, {"hf_phase_filter": 1}):
            with self.subTest(override=override), self.assertRaises(runner.CampaignError):
                runner._validate_episode(dict(episode, **override), 0, set(), 10, 10000)
        for path, value in ((("enabled",), False), (("executed",), False), (("timing_atoms",), 1),
                (("timing_prior_mass",), .5), (("phase_bins",), 64), (("gain_radii",), [1]),
                (("input_samples",), 79), (("input_blocks",), 2), (("stage_wall_seconds",), -1),
                (("branch_weight_within_supplement",), 1), (("supplement_weight",), 1),
                (("correlated", "favored_bit"), 1), (("correlated", "passes_bit_separation_reference"), True),
                (("baseline", "bit_log_e"), [1., 4.]), (("combined", "bit_log_e"), [5., 5.]),
                (("combined", "weighted_presence_log_e"), 5.), (("work", "correlation_products"), 1),
                (("work", "emission_states"), 1), (("work", "transition_edges"), 1),
                (("work", "workspace_reserved_bytes"), 0), (("work", "summed_worker_wall_seconds"), math.nan)):
            bad = json.loads(json.dumps(receipt))
            parent = bad["hf_public_search"]["phase_filter"]
            for key in path[:-1]:
                parent = parent[key]
            parent[path[-1]] = value
            with self.subTest(path=path), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad, episode)
        # Cross-check partial last groups/blocks without adding a DSP implementation.
        samples = 4001
        longer = json.loads(json.dumps(receipt))
        longer["plan"].update(horizon_samples=samples + 20, capture_digest_stop_sample=samples + 20,
                              search_observation={"first": 20, "stop": samples + 20})
        longer["channel"]["selected_observation_hashed_samples"] = samples
        longer["hf_public_search"]["blocks"] = (samples + 127) // 128
        longer["hf_public_search"]["phase_filter"] = fake_phase_filter(longer["hf_public_search"], samples)
        runner._validate_completed_receipt(longer, episode)
        unrequested = dict(episode, hf_phase_filter=False)
        receipt["plan"]["hf_phase_filter_requested"] = False
        with self.assertRaisesRegex(runner.CampaignError, "unrequested HF phase filter"):
            runner._validate_completed_receipt(receipt, unrequested)

    def test_phase_filter_uses_combined_public_frequency_prior(self) -> None:
        episode = {"id": "filter", "scenario": "awgn", "mode": "hf-search", "waveform": "reference", "seed": 1,
                   "hf_phase_refinement": True, "search_timing": "full", "hf_phase_filter": True}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 1000000, 2))
        # Sanitized geometry from the completed filter-edge-slot1 receipt:
        # its zero oscillator bound plus 1 Hz atmospheric allowance is the
        # native public search bound; FFT rounding is a separate quantity.
        first, stop, samples = 287981, 556338, 268357
        receipt["plan"].update(horizon_samples=stop, capture_digest_first_sample=first,
            capture_digest_stop_sample=stop, search_observation={"first": first, "stop": stop})
        receipt["channel"]["selected_observation_hashed_samples"] = samples
        search = receipt["hf_public_search"]
        search.update(blocks=2097, configured_frequency_bound_hz=1, actual_frequency_bound_hz=1.00067138671875)
        search["phase_filter"] = fake_phase_filter(search, samples)
        self.assertEqual(search["phase_filter"]["groups"], 140)
        for base, atmosphere in ((0, 1), (.25, .75), (1, 0)):
            receipt["plan"]["public_bounds"].update(frequency_bound_hz=base, atmospheric_hz=atmosphere)
            runner._validate_completed_receipt(receipt, episode)
        for base, atmosphere in ((1, 1), (0, 0), (-1, 2), (1, math.nan), (True, 0)):
            receipt["plan"]["public_bounds"].update(frequency_bound_hz=base, atmospheric_hz=atmosphere)
            with self.subTest(base=base, atmosphere=atmosphere), self.assertRaisesRegex(runner.CampaignError, "frequency geometry"):
                runner._validate_completed_receipt(receipt, episode)
        receipt["plan"]["public_bounds"].update(frequency_bound_hz=0, atmospheric_hz=1)
        search["configured_frequency_bound_hz"] = .5
        with self.assertRaisesRegex(runner.CampaignError, "frequency geometry"):
            runner._validate_completed_receipt(receipt, episode)

    def test_receiver_phase_filter_requires_explicit_compatible_receive_scope(self) -> None:
        episode = {"id": "filter-receiver", "scenario": "hf-ensemble-multiscale", "mode": "receive",
                   "waveform": "reference", "seed": 1, "hf_public_phase_receiver": True,
                   "hf_phase_refinement": True, "hf_phase_filter_receiver": True}
        runner._validate_episode(episode, 0, set(), 10, 1000000)
        command = runner._command(Path("/native/probe"), episode, 1000000, 2)
        self.assertIn("--hf-phase-filter-receiver", command)
        runner._validate_completed_receipt(fake_completed_receipt(command), episode)
        for override in ({"mode": "slot"}, {"mode": "plan"}, {"mode": "hf-search"},
                {"hf_public_phase_receiver": False}, {"hf_phase_refinement": False}, {"helper": True},
                {"backend": "cuda"}, {"hf_compare_admitted_models": True}, {"hf_phase_filter_receiver": 1}):
            with self.subTest(override=override), self.assertRaises(runner.CampaignError):
                runner._validate_episode(dict(episode, **override), 0, set(), 10, 1000000)
        disabled = dict(episode, hf_phase_filter_receiver=False)
        old = fake_completed_receipt(runner._command(Path("/native/probe"), disabled, 1000000, 2))
        runner._validate_completed_receipt(old, disabled)
        self.assertNotIn("phase_filter_enabled", old["hf_public_phase_receiver"])
        self.assertNotIn("phase_filter", old["hf_public_phase_receiver"]["slots"][0])

    def test_private_phase_paths_bind_truth_scope_timing_and_finite_lattice(self) -> None:
        episode = {"id": "paths", "scenario": "hf-ensemble-tid", "mode": "hf-search", "waveform": "reference", "seed": 1,
                   "search_timing": "full", "hf_phase_refinement": True, "hf_phase_filter": True,
                   "hf_physical_diagnostic": True, "hf_phase_filter_paths": True}
        runner._validate_episode(episode, 0, set(), 10, 10000)
        command = runner._command(Path("/native/probe"), episode, 10000, 2)
        self.assertIn("--hf-phase-filter-paths", command)
        receipt = fake_completed_receipt(command)
        runner._validate_completed_receipt(receipt, episode)
        for override in ({"hf_phase_filter_paths": 1}, {"hf_phase_filter": False},
                         {"hf_physical_diagnostic": False}, {"mode": "receive"}, {"control": "noise"}):
            with self.subTest(override=override), self.assertRaises(runner.CampaignError):
                runner._validate_episode(dict(episode, **override), 0, set(), 10, 10000)
        for path, value in ((("used_for_receiver_decisions",), True), (("uses_physical_truth",), False),
                (("wanted_bit",), 0), (("observation_sha256",), "d" * 64), (("timing_index",), 18),
                (("epoch_offset_seconds",), 1), (("clock_ppm",), .1), (("arrival_seconds",), .003),
                (("reference_path_delay_seconds",), .003), (("clean_coherence_power_lower_bound",), 1.1),
                (("optimal_clean_correlation_real",), -1), (("gain_radii",), [1]),
                (("maximum_noisy_log_lr",), [0., 0.]), (("maximum_prior_weighted_noisy_path_log_e",), [3., 3.]),
                (("conditional_noisy_log_e",), [math.nan, 1.]), (("work", "correlation_products"), 1),
                (("work", "emission_states"), 1), (("work", "transition_edges"), 1),
                (("work", "workspace_reserved_bytes"), 1024), (("work", "block_statistics_reserved_bytes"), 1),
                (("work", "public_pair_reserved_bytes"), 1)):
            bad = json.loads(json.dumps(receipt))
            parent = bad["hf_physical_diagnostic"]["phase_filter_paths"]
            for key in path[:-1]:
                parent = parent[key]
            parent[path[-1]] = value
            with self.subTest(path=path), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad, episode)
        unrequested = dict(episode, hf_phase_filter_paths=False)
        receipt["plan"]["hf_phase_filter_paths_requested"] = False
        with self.assertRaisesRegex(runner.CampaignError, "unrequested private phase"):
            runner._validate_completed_receipt(receipt, unrequested)

    def test_sticky_phase_filter_is_explicit_and_preserves_the_default(self) -> None:
        shadow = {"id": "sticky", "scenario": "hf-ensemble-tid", "mode": "hf-search", "waveform": "reference", "seed": 1,
                  "search_timing": "full", "hf_phase_refinement": True, "hf_phase_filter": True}
        receiver = {"id": "sticky-receiver", "scenario": "hf-ensemble-tid", "mode": "receive", "waveform": "reference", "seed": 1,
                    "hf_phase_refinement": True, "hf_public_phase_receiver": True, "hf_phase_filter_receiver": True}
        for base in (shadow, receiver):
            for requested in (None, False, True):
                episode = dict(base)
                if requested is not None:
                    episode["hf_phase_filter_sticky"] = requested
                runner._validate_episode(episode, 0, set(), 10, 1000000)
                command = runner._command(Path("/native/probe"), episode, 1000000, 2)
                self.assertEqual("--hf-phase-filter-sticky" in command, requested is True)
                receipt = fake_completed_receipt(command)
                runner._validate_completed_receipt(receipt, episode)
                parent = receipt["hf_public_search"] if base is shadow else receipt["hf_public_phase_receiver"]["slots"][0]
                self.assertEqual("sticky" in parent["phase_filter"], requested is True)
                if requested is True:
                    bad = json.loads(json.dumps(receipt))
                    del bad["plan"]["hf_phase_filter_sticky_requested"]
                    with self.assertRaisesRegex(runner.CampaignError, "sticky phase prior request"):
                        runner._validate_completed_receipt(bad, episode)
                    if base is receiver:
                        bad = json.loads(json.dumps(receipt))
                        del bad["hf_public_phase_receiver"]["phase_filter_sticky_enabled"]
                        with self.assertRaises(runner.CampaignError):
                            runner._validate_completed_receipt(bad, episode)
                else:
                    parent["phase_filter"]["sticky_enabled"] = True
                    with self.assertRaisesRegex(runner.CampaignError, "unrequested sticky"):
                        runner._validate_completed_receipt(receipt, episode)
        for override in ({"hf_phase_filter": False}, {"hf_phase_filter_sticky": 1},
                         {"hf_phase_filter_paths": True, "hf_physical_diagnostic": True}):
            episode = dict(shadow, hf_phase_filter_sticky=True)
            episode.update(override)
            with self.subTest(override=override), self.assertRaises(runner.CampaignError):
                runner._validate_episode(episode, 0, set(), 10, 1000000)

    def test_phase_bridge_is_opt_in_for_shadow_or_receiver_and_preserves_legacy(self) -> None:
        shadow = {"id": "bridge", "scenario": "hf-ensemble-tid", "mode": "hf-search", "waveform": "reference", "seed": 1,
                  "search_timing": "full", "hf_phase_refinement": True, "hf_phase_filter": True}
        receiver = {"id": "bridge-receiver", "scenario": "hf-ensemble-tid", "mode": "receive", "waveform": "reference", "seed": 1,
                    "hf_phase_refinement": True, "hf_public_phase_receiver": True, "hf_phase_filter_receiver": True}
        for base in (shadow, receiver):
            for sticky in (False, True):
                episode = dict(base, hf_phase_filter_sticky=sticky, hf_phase_bridge=True)
                runner._validate_episode(episode, 0, set(), 10, 1000000)
                command = runner._command(Path("/native/probe"), episode, 1000000, 2)
                self.assertIn("--hf-phase-bridge", command)
                receipt = fake_completed_receipt(command)
                runner._validate_completed_receipt(receipt, episode)
                parent = receipt["hf_public_search"] if base is shadow else receipt["hf_public_phase_receiver"]["slots"][0]
                result = parent["phase_filter"]
                old = fake_phase_filter(parent, result["input_samples"], sticky=sticky)
                self.assertEqual(result["work"], old["work"])
                self.assertEqual(result["legacy_correlated"], old["correlated"])
                for bit in (0, 1):
                    expected = math.log(.5 * math.exp(result["baseline"]["bit_log_e"][bit]) +
                                        .25 * math.exp(result["legacy_correlated"]["bit_log_e"][bit]) +
                                        .25 * math.exp(result["bridge"]["bit_log_e"][bit]))
                    self.assertAlmostEqual(result["combined"]["bit_log_e"][bit], expected)
                bad = json.loads(json.dumps(receipt))
                del bad["plan"]["hf_phase_bridge_requested"]
                with self.assertRaisesRegex(runner.CampaignError, "phase bridge request"):
                    runner._validate_completed_receipt(bad, episode)
                if base is receiver:
                    bad = json.loads(json.dumps(receipt))
                    del bad["hf_public_phase_receiver"]["phase_bridge_enabled"]
                    with self.assertRaises(runner.CampaignError):
                        runner._validate_completed_receipt(bad, episode)
                    self.assertEqual(parent["bit_log_e"], result["combined"]["bit_log_e"])
                disabled = dict(episode, hf_phase_bridge=False)
                old_command = runner._command(Path("/native/probe"), disabled, 1000000, 2)
                self.assertNotIn("--hf-phase-bridge", old_command)
                old_receipt = fake_completed_receipt(old_command)
                runner._validate_completed_receipt(old_receipt, disabled)
                target = old_receipt["hf_public_search"] if base is shadow else old_receipt["hf_public_phase_receiver"]["slots"][0]
                self.assertNotIn("bridge", target["phase_filter"])
                target["phase_filter"]["bridge_work"] = result["bridge_work"]
                with self.assertRaisesRegex(runner.CampaignError, "unrequested phase bridge"):
                    runner._validate_completed_receipt(old_receipt, disabled)
        for override in ({"hf_phase_filter": False}, {"hf_phase_bridge": 1},
                         {"hf_phase_filter_paths": True, "hf_physical_diagnostic": True}):
            episode = dict(shadow, hf_phase_bridge=True)
            episode.update(override)
            with self.subTest(override=override), self.assertRaises(runner.CampaignError):
                runner._validate_episode(episode, 0, set(), 10, 1000000)

    def test_phase_bridge_partial_groups_workspace_and_work_are_exact(self) -> None:
        episode = {"id": "bridge", "scenario": "hf-ensemble-tid", "mode": "hf-search", "waveform": "reference", "seed": 1,
                   "search_timing": "full", "hf_phase_refinement": True, "hf_phase_filter": True, "hf_phase_bridge": True}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 2000000, 2))
        parent, plan = receipt["hf_public_search"], receipt["plan"]
        for samples, expected_groups in ((1, 1), (7679, 1), (7680, 1), (7681, 2), (600 * 1920, 150)):
            for workers in (1, 39):
                parent.update(blocks=(samples + 127) // 128, stage_workers=workers)
                plan.update(horizon_samples=samples, search_observation={"first": 0, "stop": samples})
                parent["phase_filter"] = fake_phase_filter(parent, samples, bridge=True)
                work = parent["phase_filter"]["bridge_work"]
                self.assertEqual(work["groups"], expected_groups)
                self.assertEqual(work["workspace_reserved_bytes"], workers * 131072 + 98304)
                self.assertGreater(work["forward_terms"], 0)  # The final endpoint is retained even for one partial group.
                runner._validate_phase_filter_result(parent, plan, plan["search_observation"])
        changes = ((("bridge_enabled",), False), (("bridge_prior_weights",), [.25, .75]),
            (("bridge_work", "group_seconds"), 1), (("bridge_work", "groups"), 149),
            (("bridge_work", "correlation_products"), 1), (("bridge_work", "edge_emissions"), 1),
            (("bridge_work", "forward_terms"), 39 * 4 * 97 * 32 * 149),
            (("bridge_work", "shared_rotator_values"), 5821), (("bridge_work", "lane_workspace_bytes"), 65536),
            (("bridge_work", "shared_workspace_bytes"), 0),
            (("bridge_work", "workspace_reserved_bytes"), 39 * 131072),
            (("bridge_work", "transition_probabilities", "edge_stay"), .5),
            (("bridge_work", "stage_wall_seconds"), -1), (("bridge_work", "summed_worker_wall_seconds"), math.nan),
            (("bridge", "favored_bit"), 1), (("legacy_correlated", "bit_log_e"), [1., 2.]))
        for path, value in changes:
            bad = json.loads(json.dumps(parent))
            target = bad["phase_filter"]
            for key in path[:-1]:
                target = target[key]
            target[path[-1]] = value
            with self.subTest(path=path), self.assertRaises(runner.CampaignError):
                runner._validate_phase_filter_result(bad, plan, plan["search_observation"])

    def test_phase_bridge_rejects_missing_half_weight_with_consistent_summaries(self) -> None:
        episode = {"id": "bridge", "scenario": "hf-ensemble-tid", "mode": "hf-search", "waveform": "reference", "seed": 1,
                   "search_timing": "full", "hf_phase_refinement": True, "hf_phase_filter": True,
                   "hf_phase_filter_sticky": True, "hf_phase_bridge": True}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 1000000, 2))
        parent = receipt["hf_public_search"]
        result = parent["phase_filter"]

        def rewrite_summary(name, values):
            presence = math.log(sum(math.exp(value) for value in values) * .125)
            separation = abs(values[0] - values[1])
            result[name].update(bit_log_e=values, weighted_presence_log_e=presence,
                favored_bit=int(values[1] > values[0]), bit_log_separation=separation,
                passes_presence_reference=presence >= parent["presence_reference_threshold"],
                passes_bit_separation_reference=separation >= parent["bit_separation_reference"])

        rewrite_summary("correlated", [value + math.log(2) for value in result["correlated"]["bit_log_e"]])
        rewrite_summary("combined", [math.log((math.exp(a) + math.exp(b)) * .5)
                                    for a, b in zip(result["baseline"]["bit_log_e"], result["correlated"]["bit_log_e"])])
        with self.assertRaisesRegex(runner.CampaignError, "equal bridge/legacy mixture"):
            runner._validate_completed_receipt(receipt, episode)

    def test_private_bridge_support_is_additive_and_charges_both_new_workspaces(self) -> None:
        episode = {"id": "paths", "scenario": "hf-ensemble-tid", "mode": "hf-search", "waveform": "reference", "seed": 1,
                   "search_timing": "full", "hf_phase_refinement": True, "hf_phase_filter": True,
                   "hf_physical_diagnostic": True, "hf_phase_filter_paths": True}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 10000, 2))
        runner._validate_completed_receipt(receipt, episode)  # Both new diagnostic objects are optional for legacy receipts.
        physical, search = receipt["hf_physical_diagnostic"], receipt["hf_public_search"]
        result = physical["phase_filter_paths"]
        search["coordinates"][result["timing_index"]]["template_energy"] = [10., 10.]
        result["bridge_support"] = fake_private_bridge_support(1)
        physical["admission"].update(future_reserved_bytes=400000, total_memory_limit_bytes=500000)
        runner._validate_completed_receipt(receipt, episode)
        result["clean_trace"] = fake_private_clean_trace(result, physical, search, 80, [0])
        with self.assertRaisesRegex(runner.CampaignError, "buffers must fit"):
            runner._validate_completed_receipt(receipt, episode)
        physical["admission"].update(future_reserved_bytes=700000, total_memory_limit_bytes=800000)
        runner._validate_completed_receipt(receipt, episode)
        self.assertNotIn("hf_phase_bridge_requested", receipt["plan"])
        self.assertFalse(receipt["outcomes"]["receiver_executed"])

    def test_private_bridge_support_rejects_wrong_geometry_normalization_and_scope(self) -> None:
        result = {"timing_index": 0, "wanted_bit": 1, "bridge_support": fake_private_bridge_support(61)}
        physical, search = {"physical_templates": {"noise_free_energy": 10.}}, {"coordinates": [{"template_energy": [10., 10.]}]}
        runner._validate_private_bridge_support(result, physical, search, 61)
        changes = ((("schema_version",), True), (("schema_version",), 2), (("used_for_receiver_decisions",), True),
            (("phase_invariant",), "none"), (("group_seconds",), 1), (("groups",), 1),
            (("workspace_reserved_bytes",), 131072), (("correlation_products",), 97 * 61),
            (("max_plus_terms",), (97 + 291 + 159) * 32),
            (("original", "edge_terms_per_phase"), 159), (("phase_relaxed", "edge_terms_per_phase"), 97),
            (("rate_relaxed", "edge_terms_per_phase"), 291), (("original", "real_correlation"), math.nan),
            (("phase_relaxed", "clean_coherence_power"), .1), (("rate_relaxed", "clean_coherence_power"), 1.01))
        for path, value in changes:
            bad = json.loads(json.dumps(result))
            target = bad["bridge_support"]
            for key in path[:-1]:
                target = target[key]
            target[path[-1]] = value
            with self.subTest(path=path), self.assertRaises(runner.CampaignError):
                runner._validate_private_bridge_support(bad, physical, search, 61)
        for correlations in ((6., 5., 7.), (6., 8., 5.)):
            result["bridge_support"] = fake_private_bridge_support(61, correlations)
            with self.assertRaisesRegex(runner.CampaignError, "contain the original"):
                runner._validate_private_bridge_support(result, physical, search, 61)
        result["bridge_support"] = fake_private_bridge_support(61, (6., 11., 7.))
        with self.assertRaisesRegex(runner.CampaignError, "energy normalization"):
            runner._validate_private_bridge_support(result, physical, search, 61)

    def test_private_bridge_support_partial_groups_fixed_caps_and_zero_projection(self) -> None:
        physical, search = {"physical_templates": {"noise_free_energy": 10.}}, {"coordinates": [{"template_energy": [10., 10.]}]}
        result = {"timing_index": 0, "wanted_bit": 1}
        for blocks, groups in ((1, 1), (59, 1), (60, 1), (61, 2), (9000, 150)):
            for correlations in ((0., 0., 0.), (6., 6., 6.), (6., 8., 7.)):
                result["bridge_support"] = fake_private_bridge_support(blocks, correlations)
                self.assertEqual(result["bridge_support"]["groups"], groups)
                self.assertEqual(runner._validate_private_bridge_support(result, physical, search, blocks), 262144)
                self.assertLessEqual(result["bridge_support"]["correlation_products"], 1431000)
                self.assertLessEqual(result["bridge_support"]["max_plus_terms"], 2625600)
        # Keep harmless final arithmetic roundoff distinct from a support loss.
        result["bridge_support"] = fake_private_bridge_support(1, (6., 6. - 1e-10, 6.))
        runner._validate_private_bridge_support(result, physical, search, 1)
        for blocks in (0, 9001):
            result["bridge_support"] = fake_private_bridge_support(blocks)
            with self.subTest(blocks=blocks), self.assertRaisesRegex(runner.CampaignError, "aperture cap"):
                runner._validate_private_bridge_support(result, physical, search, blocks)

    def test_private_clean_trace_is_versioned_additive_and_charges_extra_workspace(self) -> None:
        episode = {"id": "paths", "scenario": "hf-ensemble-tid", "mode": "hf-search", "waveform": "reference", "seed": 1,
                   "search_timing": "full", "hf_phase_refinement": True, "hf_phase_filter": True,
                   "hf_physical_diagnostic": True, "hf_phase_filter_paths": True}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 10000, 2))
        runner._validate_completed_receipt(receipt, episode)  # Legacy receipts have no additive trace.
        physical, search = receipt["hf_physical_diagnostic"], receipt["hf_public_search"]
        result = physical["phase_filter_paths"]
        result["clean_trace"] = fake_private_clean_trace(result, physical, search, 80, [0])
        with self.assertRaisesRegex(runner.CampaignError, "buffers must fit"):
            runner._validate_completed_receipt(receipt, episode)
        physical["admission"].update(future_reserved_bytes=400000, total_memory_limit_bytes=500000)
        runner._validate_completed_receipt(receipt, episode)
        for version in (None, True, 0, 2):
            result["clean_trace"]["schema_version"] = version
            with self.subTest(version=version), self.assertRaisesRegex(runner.CampaignError, "schema version"):
                runner._validate_completed_receipt(receipt, episode)

    def test_private_clean_trace_checks_state_priors_certificate_and_partial_groups(self) -> None:
        result = {"timing_index": 0, "wanted_bit": 1, "optimal_clean_correlation_real": math.sqrt(50),
                  "clean_coherence_power_lower_bound": .5}
        physical = {"physical_templates": {"noise_free_energy": 10.}}
        search = {"coordinates": [{}]}
        samples, groups = 7 * 1920 + 1000, 8
        blocks = (samples + 127) // 128
        trace = fake_private_clean_trace(result, physical, search, samples, [15, 16, 16, 15, 15, 16, 15, 15])
        result["clean_trace"] = trace
        self.assertEqual(trace["maximum_projection"]["jumps"], 4)
        self.assertEqual(trace["maximum_projection"]["adjacent_reversals"], 1)
        self.assertEqual(trace["maximum_projection"]["successive_jump_reversals"], 3)
        self.assertEqual(trace["maximum_projection"]["edge_stays"], 1)
        self.assertEqual(trace["minimum_jumps_lower_bound"], 3)
        self.assertEqual(runner._validate_private_clean_trace(result, physical, search, groups, blocks, samples),
                         trace["workspace_reserved_bytes"])
        changes = ((("workspace_reserved_bytes",), 262144), (("analytic_carrier_points",), blocks + groups),
            (("predecessor_labels",), 0), (("extra_state_evaluations",), 1056), (("extra_transition_edges",), 0),
            (("penalty_correlation_units",), .5), (("certificate_tolerance_correlation",), 1.),
            (("target_projection_power_fraction_of_best_real",), 1.), (("target_real_correlation",), 1.),
            (("minimum_jumps_lower_bound",), 0), (("target_reached",), False),
            (("penalized_objective_correlation",), 0.), (("carrier_reversal_count",), groups),
            (("carrier_frequency_total_variation_hz",), -1.), (("carrier_reference_scope",), ""),
            (("maximum_projection", "frequency_bins"), [15, 17, 16, 15, 15, 16, 15, 15]),
            (("maximum_projection", "phase_bins"), [0] * groups), (("maximum_projection", "jumps"), 3),
            (("maximum_projection", "adjacent_reversals"), 3), (("maximum_projection", "longest_dwell_groups"), 1),
            (("maximum_projection", "walk_log_prior"), 0.), (("maximum_projection", "sticky_log_prior"), 0.),
            (("maximum_projection", "real_correlation"), 1.), (("maximum_projection", "clean_coherence_power"), .75),
            (("maximum_projection", "effective_phase_rms_radians"), 4.),
            (("maximum_projection", "carrier_frequency_4_group_rms_hz"), None),
            (("maximum_projection", "reversals_below_half_bin_carrier_change"), 4))
        for path, value in changes:
            bad = json.loads(json.dumps(result))
            target = bad["clean_trace"]
            for key in path[:-1]:
                target = target[key]
            target[path[-1]] = value
            with self.subTest(path=path), self.assertRaises(runner.CampaignError):
                runner._validate_private_clean_trace(bad, physical, search, groups, blocks, samples)
        # A smoother penalized path may miss the near-maximum target; the lower
        # certificate still applies, without calling that path a successful witness.
        smoother = fake_private_clean_trace(result, physical, search, samples, [16] * groups)["penalized_projection"]
        smoother["real_correlation"] -= .05
        smoother["clean_coherence_power"] = smoother["real_correlation"] ** 2 / 100
        trace.update(penalized_projection=smoother, penalized_objective_correlation=smoother["real_correlation"], target_reached=False)
        trace["minimum_jumps_lower_bound"] = math.ceil((trace["target_real_correlation"] - smoother["real_correlation"] -
            trace["certificate_tolerance_correlation"]) / trace["penalty_correlation_units"])
        runner._validate_private_clean_trace(result, physical, search, groups, blocks, samples)
        self.assertEqual(trace["minimum_jumps_lower_bound"], 1)
        # A nominal value inside the certificate's tolerance is not a certified
        # target-reaching witness. Preserve the conservative C - tolerance rule.
        smoother["real_correlation"] = trace["target_real_correlation"] + trace["certificate_tolerance_correlation"] / 2
        smoother["clean_coherence_power"] = smoother["real_correlation"] ** 2 / 100
        trace.update(penalized_objective_correlation=smoother["real_correlation"], minimum_jumps_lower_bound=0)
        runner._validate_private_clean_trace(result, physical, search, groups, blocks, samples)
        trace["target_reached"] = True
        with self.assertRaisesRegex(runner.CampaignError, "jump certificate"):
            runner._validate_private_clean_trace(result, physical, search, groups, blocks, samples)

    def test_sticky_phase_filter_fixed_prior_work_and_shared_scratch(self) -> None:
        episode = {"id": "sticky-receiver", "scenario": "hf-ensemble-tid", "mode": "receive", "waveform": "reference", "seed": 1,
                   "hf_phase_refinement": True, "hf_public_phase_receiver": True, "hf_phase_filter_receiver": True,
                   "hf_phase_filter_sticky": True}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 1000000, 2))
        parent = receipt["hf_public_phase_receiver"]["slots"][0]
        result = parent["phase_filter"]
        baseline = fake_phase_filter(parent, 4001)
        for field in ("correlation_products", "emission_states", "transition_edges"):
            self.assertEqual(result["work"][field], 2 * baseline["work"][field])
            self.assertGreater(baseline["work"][field], 0)
        self.assertEqual(result["work"]["workspace_reserved_bytes"], baseline["work"]["workspace_reserved_bytes"])
        # Independent closed-form accounting: baseline 1/2, each walk 1/4,
        # followed by the pre-existing 1/4 receiver supplement reservation.
        for bit in (0, 1):
            expected = math.log(.5 * math.exp(result["baseline"]["bit_log_e"][bit]) +
                                .25 * math.exp(result["walk"]["bit_log_e"][bit]) +
                                .25 * math.exp(result["sticky"]["bit_log_e"][bit]))
            self.assertAlmostEqual(result["combined"]["bit_log_e"][bit], expected)
        expected_presence = math.log(sum(math.exp(value) for value in result["combined"]["bit_log_e"]) * .125)
        self.assertAlmostEqual(result["combined"]["weighted_presence_log_e"], expected_presence)
        runner._validate_completed_receipt(receipt, episode)
        changes = ((("sticky_enabled",), False), (("correlated_prior_weights",), [.25, .75]),
                   (("transition_probabilities", "walk", "stay"), .6),
                   (("transition_probabilities", "sticky", "step_each"), .25),
                   (("transition_probabilities", "sticky", "edge_stay"), math.nan),
                   (("walk", "bit_log_e"), [5., math.inf]), (("sticky", "favored_bit"), 0),
                   (("correlated", "bit_log_e"), result["walk"]["bit_log_e"]),
                   (("work", "correlation_products"), baseline["work"]["correlation_products"]),
                   (("work", "emission_states"), baseline["work"]["emission_states"]),
                   (("work", "transition_edges"), baseline["work"]["transition_edges"]),
                   (("work", "workspace_reserved_bytes"), 2 * baseline["work"]["workspace_reserved_bytes"]))
        for path, value in changes:
            bad = json.loads(json.dumps(receipt))
            target = bad["hf_public_phase_receiver"]["slots"][0]["phase_filter"]
            for key in path[:-1]:
                target = target[key]
            target[path[-1]] = value
            with self.subTest(path=path), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad, episode)
        bad = json.loads(json.dumps(receipt))
        bad["hf_public_phase_receiver"]["slots"][0]["stage_workers"] = 2
        with self.assertRaisesRegex(runner.CampaignError, "per-worker workspace"):
            runner._validate_completed_receipt(bad, episode)

    def test_sticky_phase_filter_rejects_missing_inner_half_weight_with_consistent_summaries(self) -> None:
        episode = {"id": "sticky", "scenario": "hf-ensemble-tid", "mode": "hf-search", "waveform": "reference", "seed": 1,
                   "search_timing": "full", "hf_phase_refinement": True, "hf_phase_filter": True, "hf_phase_filter_sticky": True}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 1000000, 2))
        parent = receipt["hf_public_search"]
        result = parent["phase_filter"]

        def rewrite_summary(name, values):
            presence = math.log(sum(math.exp(value) for value in values) * .125)
            separation = abs(values[0] - values[1])
            result[name].update(bit_log_e=values, weighted_presence_log_e=presence,
                favored_bit=int(values[1] > values[0]), bit_log_separation=separation,
                passes_presence_reference=presence >= parent["presence_reference_threshold"],
                passes_bit_separation_reference=separation >= parent["bit_separation_reference"])

        # Coherently inflate both reported stages, so a check of each summary
        # alone cannot catch the lost reservation of half the correlated prior.
        rewrite_summary("correlated", [value + math.log(2) for value in result["correlated"]["bit_log_e"]])
        rewrite_summary("combined", [math.log((math.exp(a) + math.exp(b)) * .5)
                                    for a, b in zip(result["baseline"]["bit_log_e"], result["correlated"]["bit_log_e"])])
        with self.assertRaisesRegex(runner.CampaignError, "equal walk/sticky mixture"):
            runner._validate_completed_receipt(receipt, episode)

    def test_receiver_phase_filter_binds_callback_aperture_and_both_mixture_layers(self) -> None:
        episode = {"id": "filter-receiver", "scenario": "hf-ensemble-multiscale", "mode": "receive",
                   "waveform": "reference", "seed": 1, "hf_public_phase_receiver": True,
                   "hf_phase_refinement": True, "hf_phase_filter_receiver": True}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 1000000, 2))
        callback = receipt["hf_public_phase_receiver"]["slots"][0]
        baseline = callback["phase_refinement"]["combined_bit_log_e"]
        self.assertEqual(callback["phase_filter"]["baseline"]["bit_log_e"], baseline)
        self.assertNotEqual(callback["bit_log_e"], baseline)
        self.assertEqual(callback["phase_filter"]["groups"], 3)
        self.assertEqual(callback["phase_filter"]["input_blocks"], 32)
        runner._validate_completed_receipt(receipt, episode)
        paths = (
            (("plan", "hf_phase_filter_receiver_requested"), False),
            (("plan", "public_slots", 0, "observation", "stop"), 4020),
            (("plan", "public_slots", 0, "index"), 1),
            (("hf_public_phase_receiver", "phase_filter_enabled"), False),
            (("hf_public_phase_receiver", "fixed_weight"), .5),
            (("hf_public_phase_receiver", "model_family"), "equal_prior_fixed97_and_cartesian_refinement"),
            (("hf_public_phase_receiver", "executed_slots"), 2),
            (("callback", "bit_log_e"), baseline), (("callback", "slot_index"), 5),
            (("callback", "configured_frequency_bound_hz"), 0),
            (("callback", "phase_filter", "baseline", "bit_log_e"), callback["bit_log_e"]),
            (("callback", "phase_filter", "branch_weight_within_supplement"), 1),
            (("callback", "phase_filter", "supplement_weight"), 1),
            (("callback", "phase_filter", "input_samples"), 4000),
            (("callback", "phase_filter", "groups"), 601),
            (("callback", "phase_filter", "work", "transition_edges"), 0))
        for path, value in paths:
            bad = json.loads(json.dumps(receipt))
            parent = bad["hf_public_phase_receiver"]["slots"][0] if path[0] == "callback" else bad
            for key in path[1:-1] if path[0] == "callback" else path[:-1]:
                parent = parent[key]
            parent[path[-1]] = value
            with self.subTest(path=path), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad, episode)
        no_callbacks = json.loads(json.dumps(receipt))
        no_callbacks["hf_public_phase_receiver"].update(executed_slots=0, slots=[])
        runner._validate_completed_receipt(no_callbacks, episode)  # No fallback invocation is not filter evidence.
        disabled = dict(episode, hf_phase_filter_receiver=False)
        old = fake_completed_receipt(runner._command(Path("/native/probe"), disabled, 1000000, 2))
        old["hf_public_phase_receiver"]["slots"][0]["phase_filter"] = callback["phase_filter"]
        with self.assertRaisesRegex(runner.CampaignError, "unrequested receiver phase filter"):
            runner._validate_completed_receipt(old, disabled)

    def test_unknown_keys_and_duplicate_seeds_in_confirmatory_strata_fail(self) -> None:
        campaign = valid_campaign()
        campaign["surprise"] = True
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            path.write_text(json.dumps(campaign))
            with self.assertRaises(runner.CampaignError):
                runner._read_campaign(path)
            campaign = valid_campaign()
            campaign["episodes"][1]["seed"] = 1
            path.write_text(json.dumps(campaign))
            with self.assertRaisesRegex(runner.CampaignError, "distinct seeds"):
                runner._read_campaign(path)

    def test_confirmatory_stratum_must_be_homogeneous(self) -> None:
        campaign = valid_campaign()
        campaign["episodes"][1]["cn0_db_hz"] = 8
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            path.write_text(json.dumps(campaign))
            with self.assertRaisesRegex(runner.CampaignError, "mixes episode settings.*cn0_db_hz"):
                runner._read_campaign(path)

    def test_confirmatory_stratum_includes_added_episode_inputs(self) -> None:
        for field, first, second in (("chunk", 256, 512), ("message", "A", "B"),
                                     ("pulse", "rrc", "tapered-rrc"),
                                     ("capture_lead_seconds", .25, 2),
                                     ("hf_trajectory_seed", 101, 102),
                                     ("hf_trajectory_offset_seconds", 0, 137),
                                     ("max_samples", 800, 900),
                                     ("max_wall_seconds", .5, .6)):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as directory:
                campaign = valid_campaign()
                campaign["episodes"][0][field] = first
                campaign["episodes"][1][field] = second
                path = Path(directory) / "campaign.json"
                path.write_text(json.dumps(campaign))
                with self.assertRaisesRegex(runner.CampaignError, f"mixes episode settings.*{field}"):
                    runner._read_campaign(path)

    def test_slot_and_optional_episode_limits_validate_and_reject_bad_values(self) -> None:
        campaign = valid_campaign()
        campaign["confirmatory_strata"] = {}
        campaign["episodes"] = [{"id": "slot", "scenario": "awgn", "mode": "slot",
                                 "waveform": "reference", "seed": 1, "control": "wanted",
                                 "chunk": 16384, "message": "ABCDEFGHIJKLMN",
                                 "max_wall_seconds": .25, "max_samples": 1000}]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            path.write_text(json.dumps(campaign))
            self.assertEqual(runner._read_campaign(path)["episodes"][0]["mode"], "slot")
            noise = json.loads(json.dumps(campaign))
            noise["episodes"][0]["control"] = "noise"
            path.write_text(json.dumps(noise))
            self.assertEqual(runner._read_campaign(path)["episodes"][0]["control"], "noise")
            invalid_episodes = (
                {"oracle_slot": -1, "mode": "oracle"},
                {"oracle_slot": 1, "mode": "receive"},
                {"oracle_slot": True, "mode": "oracle"},
                {"chunk": 0}, {"chunk": 16385}, {"chunk": 1.5},
                {"pulse": "unknown"}, {"pulse": []},
                {"message": ""}, {"message": "x" * 17}, {"message": "a\n"}, {"message": "é"},
                {"max_wall_seconds": 10.01}, {"max_wall_seconds": 3}, {"max_samples": 1001},
                {"control": "eof", "mode": "slot"},
                {"control": "wrong-key", "mode": "slot"},
                {"control": "wrong-ordinal", "mode": "slot"},
                {"control": "capture-gap", "mode": "slot"},
                {"control": "interior-fade", "mode": "slot"},
            )
            for overrides in invalid_episodes:
                with self.subTest(overrides=overrides):
                    bad = json.loads(json.dumps(campaign))
                    bad["episodes"][0].update(overrides)
                    path.write_text(json.dumps(bad))
                    with self.assertRaises(runner.CampaignError):
                        runner._read_campaign(path)

    def test_diagnose_mode_inputs_are_generic_but_scoped(self) -> None:
        campaign = {"schema_version": 1, "name": "diagnose", "max_wall_seconds": 2,
                    "per_trial_wall_seconds": 2, "max_trials": 1, "max_samples": 1000,
                    "episodes": [{"id": "vlf-diagnose", "scenario": "vlf-phase-wander",
                                  "mode": "diagnose", "waveform": "reference", "seed": 1}]}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            path.write_text(json.dumps(campaign))
            self.assertEqual(runner._read_campaign(path)["episodes"][0]["scenario"], "vlf-phase-wander")
            for overrides in ({"control": "noise"}, {"helper": True}, {"waveform": "smoke"}):
                with self.subTest(overrides=overrides):
                    bad = json.loads(json.dumps(campaign))
                    bad["episodes"][0].update(overrides)
                    path.write_text(json.dumps(bad))
                    with self.assertRaises(runner.CampaignError):
                        runner._read_campaign(path)
            diagnostic_stratum = json.loads(json.dumps(campaign))
            diagnostic_stratum["episodes"][0]["stratum"] = "diagnostic"
            diagnostic_stratum["confirmatory_strata"] = {"diagnostic": 1}
            path.write_text(json.dumps(diagnostic_stratum))
            with self.assertRaisesRegex(runner.CampaignError, "wanted receive episodes"):
                runner._read_campaign(path)

    def test_hf_search_inputs_are_explicitly_scoped(self) -> None:
        episode = {"id": "search", "scenario": "hf-vertical-eikonal-motion", "mode": "hf-search",
                   "waveform": "reference", "seed": 1, "search_slot": 1, "search_timing": "full"}
        runner._validate_episode(episode, 0, set(), 5, 1000)
        command = runner._command(Path("/native/probe"), episode, 1000, 5)
        self.assertEqual(command[command.index("--search-slot") + 1], "1")
        self.assertEqual(command[command.index("--search-timing") + 1], "full")
        for overrides in ({"search_slot": True}, {"search_slot": -1}, {"search_slot": 2**64},
                          {"search_timing": []}, {"search_timing": "adapted"}, {"helper": True},
                          {"backend": "cuda"}, {"control": "capture-gap"}, {"control": "interior-fade"},
                          {"mode": "receive"}, {"oracle_slot": 0}, {"hf_phase_bank": True}):
            with self.subTest(overrides=overrides), self.assertRaises(runner.CampaignError):
                runner._validate_episode({**episode, **overrides}, 0, set(), 5, 1000)
        for control in ("wanted", "noise", "wrong-key", "wrong-ordinal"):
            runner._validate_episode({**episode, "control": control}, 0, set(), 5, 1000)

    def test_phase_refinement_flag_requires_an_explicit_public_search(self) -> None:
        episode = {"id": "refinement", "scenario": "awgn", "mode": "hf-search", "waveform": "smoke",
                   "seed": 1, "search_slot": 1, "hf_phase_refinement": True}
        runner._validate_episode(episode, 0, set(), 5, 1000)
        command = runner._command(Path("/native/probe"), episode, 1000, 5)
        self.assertIn("--hf-phase-refinement", command)
        for override in ({"hf_phase_refinement": 1}, {"mode": "receive"}, {"mode": "slot"},
                         {"mode": "oracle"}, {"mode": "plan"}):
            with self.subTest(override=override), self.assertRaises(runner.CampaignError):
                runner._validate_episode({**episode, **override}, 0, set(), 5, 1000)
        for mode in ("receive", "slot"):
            receiver = {**episode, "mode": mode, "hf_public_phase_receiver": True}
            del receiver["search_slot"]
            runner._validate_episode(receiver, 0, set(), 5, 1000)

    def test_refinement_receipts_preserve_family_mass_work_and_truth_separation(self) -> None:
        episode = {"id": "refinement", "scenario": "awgn", "mode": "hf-search", "waveform": "smoke",
                   "seed": 1, "hf_phase_refinement": True, "hf_physical_diagnostic": True}
        command = runner._command(Path("/native/probe"), episode, 1000, 5)
        runner._validate_completed_receipt(fake_completed_receipt(command), episode)
        for change in ("flag", "affine", "family_mass", "old_evidence", "weight", "union", "work", "shape", "cell", "truth", "new_replay"):
            bad = fake_completed_receipt(command)
            search, physical = bad["hf_public_search"], bad["hf_physical_diagnostic"]
            trace = search["phase_refinement"]
            if change == "flag":
                bad["plan"]["hf_phase_refinement_requested"] = False
            elif change == "affine":
                search["affine_comparison_evaluated"] = True
            elif change == "family_mass":
                trace["combined_bit_log_e"] = trace["old_bit_log_e"]
            elif change == "old_evidence":
                search["families"][2]["bit_log_e"] = [0., 0.]
            elif change == "weight":
                trace["new_family_weight"] = 1.
            elif change == "union":
                trace["selected_timing_indices"] = []
            elif change == "work":
                trace["work"]["direct_rows"] = 1
            elif change == "shape":
                physical["refined_public_cell"]["shape_id"] = {"a": 0, "b": 0, "nu": 0}
            elif change == "cell":
                physical["refined_public_cell"]["frequency_hz"] = .1
            elif change == "truth":
                physical["public_refinement_proposals"]["uses_physical_truth"] = True
            else:
                physical["refined_public_cell"]["single_cell_log_lr_replay_difference"] = .01
            with self.subTest(change=change), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad, episode)
        for mode in ("receive", "slot"):
            native_episode = {**episode, "mode": mode, "hf_public_phase_receiver": True}
            del native_episode["hf_physical_diagnostic"]
            native_command = runner._command(Path("/native/probe"), native_episode, 1000, 5)
            runner._validate_completed_receipt(fake_completed_receipt(native_command), native_episode)
            bad = fake_completed_receipt(native_command)
            bad["hf_public_phase_receiver"]["slots"][0]["bit_log_e"] = [0., 0.]
            with self.subTest(mode=mode), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad, native_episode)

    def test_refinement_unavailability_retains_old_prior_and_explicit_public_reason(self) -> None:
        episode = {"id": "fallback", "scenario": "awgn", "mode": "hf-search", "waveform": "smoke",
                   "seed": 1, "hf_phase_refinement": True, "hf_physical_diagnostic": True}
        command = runner._command(Path("/native/probe"), episode, 1000, 5)
        receipt = fake_completed_receipt(command)
        admitted = receipt["hf_public_search"]["phase_refinement"]
        runner._validate_refinement_trace(admitted, full=True, timing_count=1)
        legacy = json.loads(json.dumps(admitted))
        for field in ("requested", "executed", "unavailable_reason"):
            del legacy[field]
        runner._validate_refinement_trace(legacy, full=True, timing_count=1)
        partial = json.loads(json.dumps(admitted));del partial["executed"]
        with self.assertRaises(runner.CampaignError):
            runner._validate_refinement_trace(partial, full=True, timing_count=1)
        trace = fake_unavailable_refinement_trace(full=True)
        receipt["hf_public_search"]["phase_refinement"] = trace
        physical = receipt["hf_physical_diagnostic"]
        physical.update(refinement_replay_available=False, refinement_replay_reason=trace["unavailable_reason"])
        del physical["refined_public_cell"], physical["public_refinement_proposals"]
        runner._validate_completed_receipt(receipt, episode)
        for field, value in (("combined_bit_log_e", trace["old_bit_log_e"]), ("new_bit_log_e", [0., None]),
                ("unavailable_reason", "memory_limit"), ("unavailable_reason", ""), ("requested", False),
                ("executed", True), ("shape_count", 1), ("selected_timing_indices", [0]),
                ("timing_results", [{"timing_index": 0}]), ("retained_statistics_workspace_bytes", 1),
                ("retained_new_prior_mass", .1), ("work_bound_scope", "executed work")):
            bad = json.loads(json.dumps(receipt));bad["hf_public_search"]["phase_refinement"][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad, episode)
        bad = json.loads(json.dumps(receipt));bad["hf_public_search"]["phase_refinement"]["work"]["direct_rows"] = 1
        with self.assertRaises(runner.CampaignError):
            runner._validate_completed_receipt(bad, episode)
        for field, value in (("refinement_replay_reason", "new_phase_fft_work_cap"),
                             ("refinement_replay_available", True), ("refined_public_cell", {})):
            bad = json.loads(json.dumps(receipt));bad["hf_physical_diagnostic"][field] = value
            with self.subTest(field=field), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad, episode)
        # A planner can reject geometry before producing a numerical bound.
        unavailable = fake_unavailable_refinement_trace(full=True)
        unavailable["unavailable_reason"] = "new_phase_planner_unsupported"
        del unavailable["work_bound"], unavailable["work_bound_scope"]
        runner._validate_refinement_trace(unavailable, full=True, timing_count=1)
        for mode in ("receive", "slot"):
            native_episode = {**episode, "mode": mode, "hf_public_phase_receiver": True}
            del native_episode["hf_physical_diagnostic"]
            native = fake_completed_receipt(runner._command(Path("/native/probe"), native_episode, 1000, 5))
            callback = native["hf_public_phase_receiver"]["slots"][0]
            callback["phase_refinement"] = fake_unavailable_refinement_trace(full=False)
            callback["bit_log_e"] = callback["phase_refinement"]["combined_bit_log_e"]
            runner._validate_completed_receipt(native, native_episode)

    def test_staged_refinement_checks_public_geometry_and_separate_objectives(self) -> None:
        trace = fake_refinement_trace(full=True)
        trace.update(intermediate_group_rows=128, intermediate_group_count=17,
                     intermediate_iteration_limit=8, coherent_iteration_limit=8)
        proposal = {"intermediate_used": True, "intermediate_initial_log_lr": 20.,
                    "intermediate_final_log_lr": 24., "intermediate_accepted_steps": 3,
                    "initial_log_lr": -5., "continuous_log_lr": 2., "accepted_steps": 4}
        trace["timing_results"][0].update(proposal_count=1, proposals=[proposal])
        geometry = dict(full=True, timing_count=1, block_count=2049, block_rows=480,
                        sample_rate=1920., aperture_seconds=512.1)
        # A group objective can exceed the later coherent objective: only
        # monotonicity within each separately defined stage is meaningful.
        runner._validate_refinement_trace(trace, **geometry)
        runner._validate_refinement_trace(trace, **{**geometry, "full": False, "aperture_seconds": None})
        for field, value in (("intermediate_group_rows", 127), ("intermediate_group_count", 16),
                             ("intermediate_group_count", True), ("intermediate_iteration_limit", 9),
                             ("coherent_iteration_limit", 16)):
            bad = json.loads(json.dumps(trace));bad[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(runner.CampaignError):
                runner._validate_refinement_trace(bad, **geometry)
        bad = json.loads(json.dumps(trace));del bad["intermediate_group_rows"]
        with self.assertRaises(runner.CampaignError):
            runner._validate_refinement_trace(bad, **geometry)
        for field, value in (("intermediate_used", False), ("intermediate_initial_log_lr", math.nan),
                             ("intermediate_final_log_lr", 19.), ("intermediate_accepted_steps", 9),
                             ("accepted_steps", 9), ("continuous_log_lr", -6.)):
            bad = json.loads(json.dumps(trace));bad["timing_results"][0]["proposals"][0][field] = value
            with self.subTest(field=field), self.assertRaises(runner.CampaignError):
                runner._validate_refinement_trace(bad, **geometry)
        bad = json.loads(json.dumps(trace));bad["timing_results"][0]["proposal_count"] = 2
        with self.assertRaises(runner.CampaignError):
            runner._validate_refinement_trace(bad, **geometry)
        with self.assertRaises(runner.CampaignError):
            runner._validate_refinement_trace(trace, **{**geometry, "aperture_seconds": 179.9})

        short = json.loads(json.dumps(trace))
        short.update(intermediate_group_rows=0, intermediate_group_count=0,
                     intermediate_iteration_limit=0, coherent_iteration_limit=16)
        short["timing_results"][0]["proposals"][0].update(intermediate_used=False,
            intermediate_initial_log_lr=0., intermediate_final_log_lr=0., intermediate_accepted_steps=0,
            accepted_steps=16)
        short_geometry = {**geometry, "block_count": 400, "aperture_seconds": 100.}
        runner._validate_refinement_trace(short, **short_geometry)
        with self.assertRaises(runner.CampaignError):
            runner._validate_refinement_trace(short, **geometry)
        short["timing_results"][0]["proposals"][0]["intermediate_final_log_lr"] = 1.
        with self.assertRaises(runner.CampaignError):
            runner._validate_refinement_trace(short, **short_geometry)

    def test_physical_diagnostic_is_private_full_aperture_and_receipt_bound(self) -> None:
        episode = {"id": "physical", "scenario": "awgn", "mode": "hf-search", "waveform": "smoke",
                   "seed": 1, "search_slot": 1, "hf_physical_diagnostic": True}
        runner._validate_episode(episode, 0, set(), 5, 1000)
        command = runner._command(Path("/native/probe"), episode, 1000, 5)
        self.assertIn("--hf-physical-diagnostic", command)
        runner._validate_completed_receipt(fake_completed_receipt(command), episode)
        for override in ({"hf_physical_diagnostic": 1}, {"mode": "receive"}, {"control": "noise"},
                         {"control": "wrong-key"}, {"control": "wrong-ordinal"}):
            with self.subTest(override=override), self.assertRaises(runner.CampaignError):
                runner._validate_episode({**episode, **override}, 0, set(), 5, 1000)
        for field, value in (("component_complete", False), ("truth_information_passed_to_search", True),
                ("is_theoretical_upper_bound", True), ("observation_sha256", "f" * 64),
                ("prefix_samples_rendered_per_twin", 80)):
            bad = fake_completed_receipt(command);bad["hf_physical_diagnostic"][field] = value
            with self.subTest(field=field), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad, episode)
        for change in ("self_coherence", "replay", "allocation", "request"):
            bad = fake_completed_receipt(command)
            physical = bad["hf_physical_diagnostic"]
            if change == "self_coherence":
                physical["physical_templates"]["hypotheses"][1]["clean_coherence_power"] = .9
            elif change == "replay":
                physical["winning_public_cell"]["single_cell_log_lr_replay_difference"] = .01
            elif change == "allocation":
                physical["admission"]["future_reserved_bytes"] = 1000
            else:
                bad["plan"]["hf_physical_diagnostic_requested"] = False
            with self.subTest(change=change), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad, episode)

    def test_operational_phase_flag_is_scoped_and_receipt_bound(self) -> None:
        episode = {"id": "phase", "scenario": "awgn", "mode": "receive", "waveform": "smoke",
                   "seed": 1, "hf_public_phase_receiver": True}
        runner._validate_episode(episode, 0, set(), 5, 1000)
        command = runner._command(Path("/native/probe"), episode, 1000, 5)
        self.assertIn("--hf-public-phase-receiver", command)
        runner._validate_completed_receipt(fake_completed_receipt(command), episode)
        for control in ("wanted", "noise", "wrong-key", "wrong-ordinal", "interior-fade", "capture-gap"):
            runner._validate_episode({**episode, "control": control}, 0, set(), 5, 1000)
        for override in ({"hf_public_phase_receiver": 1}, {"mode": "hf-search"}, {"mode": "oracle"},
                         {"helper": True}, {"backend": "cuda"}):
            with self.subTest(override=override), self.assertRaises(runner.CampaignError):
                runner._validate_episode({**episode, **override}, 0, set(), 5, 1000)
        for section, field, value in (("plan", "hf_public_phase_receiver_requested", False),
                ("hf_public_phase_receiver", "experimental", False),
                ("hf_public_phase_receiver", "truth_information_passed_to_search", True),
                ("hf_public_phase_receiver", "fixed_weight", .5),
                ("hf_public_phase_receiver", "maximum_slot_reports", 6),
                ("hf_public_phase_receiver", "executed_slots", 2),
                ("hf_public_phase_receiver", "slots", [{"slot_index": 5, "component_complete": True}])):
            with self.subTest(section=section, field=field):
                bad = fake_completed_receipt(command)
                bad[section][field] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(bad, episode)
        no_callbacks = fake_completed_receipt(command)
        no_callbacks["hf_public_phase_receiver"].update(executed_slots=0, slots=[])
        runner._validate_completed_receipt(no_callbacks, episode)

    def test_capture_digest_is_evaluator_only_and_binds_complete_observation(self) -> None:
        episode = {"id": "digest", "scenario": "awgn", "mode": "receive", "waveform": "smoke",
                   "seed": 1, "capture_digest_slot": 1}
        runner._validate_episode(episode, 0, set(), 5, 1000)
        command = runner._command(Path("/native/probe"), episode, 1000, 5)
        self.assertEqual(command[command.index("--capture-digest-slot") + 1], "1")
        runner._validate_completed_receipt(fake_completed_receipt(command), episode)
        for override in ({"capture_digest_slot": True}, {"capture_digest_slot": -1},
                         {"capture_digest_slot": 2**64}, {"mode": "oracle"}, {"mode": "slot"},
                         {"mode": "hf-search", "search_slot": 0}):
            with self.subTest(override=override), self.assertRaises(runner.CampaignError):
                runner._validate_episode({**episode, **override}, 0, set(), 5, 1000)
        for section, field, value in (("plan", "capture_digest_slot", 0),
                ("plan", "capture_digest_slot_override", False), ("plan", "capture_digest_stop_sample", 101),
                ("channel", "selected_observation_complete", False),
                ("channel", "selected_observation_hashed_samples", 79),
                ("channel", "selected_observation_sha256", None)):
            with self.subTest(section=section, field=field):
                bad = fake_completed_receipt(command)
                bad[section][field] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(bad, episode)

    def test_command_passes_slot_and_episode_overrides_as_argv(self) -> None:
        episode = {"id": "oracle", "scenario": "awgn", "mode": "oracle", "waveform": "reference",
                   "seed": 8, "oracle_slot": 3, "chunk": 4096, "message": "AB",
                   "max_wall_seconds": 2, "max_samples": 700}
        episode["pulse"] = "tapered-rrc"
        command = runner._command(Path("/native/probe"), episode, episode["max_samples"],
                                  episode["max_wall_seconds"])
        for option, value in (("--oracle-slot", "3"), ("--chunk", "4096"), ("--message", "AB"),
                              ("--pulse", "tapered-rrc"),
                              ("--max-samples", "700"), ("--max-wall-seconds", "2")):
            self.assertEqual(command[command.index(option) + 1], value)
        runner._validate_completed_receipt(fake_completed_receipt(command), episode)

    def test_interior_fade_slots_are_scoped_and_bound_to_receipts(self) -> None:
        campaign = valid_campaign()
        campaign["confirmatory_strata"] = {}
        episode = {"id": "one-fade", "scenario": "awgn", "mode": "receive", "waveform": "smoke",
                   "seed": 3, "control": "interior-fade", "fade_slots": 1}
        campaign["episodes"] = [episode]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            for slots in (1, 2):
                episode["fade_slots"] = slots
                path.write_text(json.dumps(campaign))
                runner._read_campaign(path)
                command = runner._command(Path("/native/probe"), episode, 1000, 2)
                self.assertEqual(command[command.index("--fade-slots") + 1], str(slots))
                receipt = fake_completed_receipt(command)
                runner._validate_completed_receipt(receipt, episode)
                for value in (None, True, 3 - slots):
                    bad = json.loads(json.dumps(receipt))
                    bad["plan"]["fade_slots"] = value
                    with self.assertRaisesRegex(runner.CampaignError, "fade slots"):
                        runner._validate_completed_receipt(bad, episode)
                del receipt["plan"]["fade_slots"]
                with self.assertRaisesRegex(runner.CampaignError, "fade slots"):
                    runner._validate_completed_receipt(receipt, episode)
            for overrides in ({"fade_slots": 0}, {"fade_slots": 3}, {"fade_slots": True},
                              {"fade_slots": "1"}, {"fade_slots": 1.0}, {"control": "capture-gap"},
                              {"control": "wanted"}, {"control": "noise"}):
                bad = json.loads(json.dumps(campaign))
                bad["episodes"][0].update(overrides)
                path.write_text(json.dumps(bad))
                with self.assertRaises(runner.CampaignError):
                    runner._read_campaign(path)
        implicit = {key: value for key, value in episode.items() if key != "fade_slots"}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), implicit, 1000, 2))
        self.assertEqual(receipt["plan"]["fade_slots"], 2)
        runner._validate_completed_receipt(receipt, implicit)
        receipt["plan"]["fade_slots"] = 1
        with self.assertRaisesRegex(runner.CampaignError, "fade slots"):
            runner._validate_completed_receipt(receipt, implicit)
        del receipt["plan"]["fade_slots"]
        runner._validate_completed_receipt(receipt, implicit)  # Original two-slot receipts remain valid.

    def test_capture_lead_is_bounded_and_bound_to_receipts(self) -> None:
        campaign = valid_campaign()
        campaign["confirmatory_strata"] = {}
        episode = {"id": "helper-lead", "scenario": "awgn", "mode": "receive", "waveform": "smoke",
                   "seed": 3, "helper": True, "capture_lead_seconds": 2}
        campaign["episodes"] = [episode]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            for lead in (.25, 2, 10):
                episode["capture_lead_seconds"] = lead
                path.write_text(json.dumps(campaign))
                runner._read_campaign(path)
                command = runner._command(Path("/native/probe"), episode, 1000, 2)
                self.assertEqual(command[command.index("--capture-lead-seconds") + 1], str(lead))
                receipt = fake_completed_receipt(command)
                runner._validate_completed_receipt(receipt, episode)
                for value in (None, True, lead + .01):
                    bad = json.loads(json.dumps(receipt))
                    bad["plan"]["capture_preroll_seconds"] = value
                    with self.assertRaisesRegex(runner.CampaignError, "capture lead"):
                        runner._validate_completed_receipt(bad, episode)
                del receipt["plan"]["capture_preroll_seconds"]
                with self.assertRaisesRegex(runner.CampaignError, "capture lead"):
                    runner._validate_completed_receipt(receipt, episode)
            for value in (0, .249, 10.001, True, "2", None, float("nan"), float("inf")):
                episode["capture_lead_seconds"] = value
                path.write_text(json.dumps(campaign))
                with self.assertRaises(runner.CampaignError):
                    runner._read_campaign(path)
        implicit = {key: value for key, value in episode.items() if key != "capture_lead_seconds"}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), implicit, 1000, 2))
        runner._validate_completed_receipt(receipt, implicit)
        receipt["plan"]["capture_preroll_seconds"] = 2
        with self.assertRaisesRegex(runner.CampaignError, "capture lead"):
            runner._validate_completed_receipt(receipt, implicit)
        receipt["plan"]["capture_preroll_seconds"] = .25
        receipt["plan"]["capture_preroll_override"] = True
        with self.assertRaisesRegex(runner.CampaignError, "capture lead override"):
            runner._validate_completed_receipt(receipt, implicit)

    def test_gain_partitions_are_complete_but_not_selected_receiver_evidence(self) -> None:
        episode = {"id": "history", "scenario": "hf-ensemble-tid", "mode": "hf-search", "waveform": "reference",
                   "seed": 1, "hf_physical_diagnostic": True}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 4000, 2))
        observation = {"first": 20, "stop": 2020}
        receipt["plan"].update(horizon_samples=2020, search_observation=observation, capture_digest_stop_sample=2020)
        receipt["channel"]["selected_observation_hashed_samples"] = 2000
        physical = receipt["hf_physical_diagnostic"]
        physical.update(observation=observation, prefix_samples_rendered_per_twin=2020)
        physical["physical_templates"]["samples"] = 2000
        physical["winning_public_cell"]["projection"]["samples"] = 2000
        physical["gain_partitions"] = fake_gain_partitions(physical, 1920, 2000)
        runner._validate_completed_receipt(receipt, episode)
        self.assertEqual(physical["gain_partitions"]["winning_public_cell"][0]["block_count"], 5)
        for flag, invalid in (("used_for_receiver_decisions", True),
                              ("template_selected_from_noisy_public_search", False),
                              ("fixed_template_null_law_applies_after_selection", True)):
            bad = json.loads(json.dumps(receipt)); bad["hf_physical_diagnostic"]["gain_partitions"][flag] = invalid
            with self.assertRaisesRegex(runner.CampaignError, "gain partitions"):
                runner._validate_completed_receipt(bad, episode)
        for change in ("missing", "count", "rank", "energy", "sign", "full", "refined", "physical_missing", "physical_full"):
            bad = json.loads(json.dumps(receipt)); partitions = bad["hf_physical_diagnostic"]["gain_partitions"]
            rows = partitions["winning_public_cell"]
            if change == "missing": rows.pop(0)
            elif change == "count": rows[0]["block_count"] = 4
            elif change == "rank": rows[0]["hypotheses"][0]["rank"] = 6
            elif change == "energy": rows[0]["hypotheses"][1]["residual_signal_energy"] = 1
            elif change == "sign": rows[0]["correct_minus_wrong_noisy_log_likelihood_ratio"] *= -1
            elif change == "full": rows[-1]["hypotheses"][0]["reference_energy"] += 1
            elif change == "refined": partitions["refined_public_cell"] = rows
            elif change == "physical_missing": del partitions["physical_templates"]
            else: partitions["physical_templates"][-1]["hypotheses"][1]["reference_energy"] += 1
            with self.subTest(change=change), self.assertRaisesRegex(runner.CampaignError, "gain partitions"):
                runner._validate_completed_receipt(bad, episode)

    def test_physical_design_requires_one_complete_bounded_native_descriptor(self) -> None:
        episode = physical_design_episode()
        runner._validate_episode(episode, 0, set(), 10, 1000)
        command = runner._command(Path("/native/probe"), episode, 1000, 2)
        receipt = fake_completed_receipt(command)
        runner._validate_completed_receipt(receipt, episode)
        for field, (low, high) in runner.HF_DESIGN_BOUNDS.items():
            option = "hf_" + field
            self.assertEqual(command[command.index("--" + option.replace("_", "-")) + 1], str(episode[option]))
            for value in (low, high):
                runner._validate_episode(dict(episode, **{option: value}), 0, set(), 10, 1000)
            for value in (low - .001, high + .001, True, float("nan"), "1"):
                with self.assertRaises(runner.CampaignError):
                    runner._validate_episode(dict(episode, **{option: value}), 0, set(), 10, 1000)
            missing = dict(episode)
            del missing[option]
            with self.assertRaisesRegex(runner.CampaignError, "all eight"):
                runner._validate_episode(missing, 0, set(), 10, 1000)
            altered = json.loads(json.dumps(receipt))
            altered["plan"]["hf_design"][field] += .01
            with self.assertRaisesRegex(runner.CampaignError, "physical design"):
                runner._validate_completed_receipt(altered, episode)
        with self.assertRaisesRegex(runner.CampaignError, "require hf-ensemble-design"):
            runner._validate_episode(dict(episode, scenario="hf-ensemble-tid"), 0, set(), 10, 1000)

    def test_gui_settings_are_explicitly_scoped_and_passed_as_argv(self) -> None:
        episode = {"id": "gui", "scenario": "hf-gui-native", "mode": "receive", "waveform": "reference", "seed": 1,
                   **runner.GUI_DEFAULTS, "gui_clock_edge": True, "gui_arrival": "default",
                   "gui_target_cn0_db_hz": 20, "cn0_db_hz": 12, "bit_seconds": 16}
        runner._validate_episode(episode, 0, set(), 10, 1000)
        command = runner._command(Path("/native/probe"), episode, 1000, 2)
        for field in runner.GUI_DEFAULTS:
            option = "--" + field.replace("_", "-")
            self.assertIn(option, command)
            if field != "gui_clock_edge":
                self.assertEqual(command[command.index(option) + 1], str(episode[field]))
        for override in ({"scenario": "hf-quiet"}, {"waveform": "smoke"}, {"mode": "channel"},
                         {"control": "interior-fade"}, {"pulse": "tapered-rrc"}, {"capture_lead_seconds": 1},
                         {"hf_trajectory_offset_seconds": 0}, {"hf_public_phase_receiver": True},
                         {"gui_clock_edge": 1}, {"gui_selective": "65%"}, {"gui_spread": "worst"},
                         {"gui_margin": 0}, {"gui_audio_ppm": -1}, {"gui_rf_phase_degrees_sqrt_s": 361}):
            with self.subTest(override=override), self.assertRaises(runner.CampaignError):
                runner._validate_episode(dict(episode, **override), 0, set(), 10, 1000)

    def test_fullwave_inputs_preserve_physical_scope_without_fixing_payload(self) -> None:
        base = {"id": "fullwave", "scenario": "hf-fullwave-reflection", "waveform": "reference", "seed": 64001,
                "noise_seed": 74001, "bit_seconds": 64, "message": "A"}
        for mode in ("plan", "channel", "receive"):
            episode = dict(base, mode=mode)
            runner._validate_episode(episode, 0, set(), 300, 12000000)
            receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 12000000, 300))
            runner._validate_completed_receipt(receipt, episode)
        for message, duration in (("B", 80), ("e", 250)):
            # Admission uses native source span/band, not content-specific A64 rules.
            runner._validate_episode(dict(base, mode="receive", message=message, bit_seconds=duration), 0, set(), 300, 12000000)
        runner._validate_episode(dict(base, mode="receive", control="noise", helper=False, service_replay=False), 0, set(), 300, 12000000)
        for override in ({"mode": "slot"}, {"mode": "oracle"}, {"mode": "hf-search"}, {"mode": "gui-varying"},
                {"backend": "cuda"}, {"waveform": "smoke"}, {"pulse": "tapered-rrc"}, {"helper": True},
                {"control": "wrong-key"}, {"control": "wrong-ordinal"}, {"control": "eof"}, {"fec": "rs20"},
                {"service_replay": True}, {"hf_public_phase_receiver": True}, {"hf_compare_admitted_models": True},
                {"duration_seconds": 1}, {"capture_lead_seconds": .25}, {"hf_trajectory_seed": 64001},
                {"hf_trajectory_offset_seconds": 0}):
            episode = dict(base, mode="receive")
            episode.update(override)
            with self.subTest(override=override), self.assertRaises(runner.CampaignError):
                runner._validate_episode(episode, 0, set(), 300, 12000000)
        with self.assertRaisesRegex(runner.CampaignError, "control is valid only"):
            runner._validate_episode(dict(base, mode="channel", control="wanted"), 0, set(), 300, 12000000)

    def test_fullwave_receipt_binds_fixed_model_complete_span_and_public_tail(self) -> None:
        episode = {"id": "fullwave", "scenario": "hf-fullwave-reflection", "mode": "receive", "waveform": "reference",
                   "seed": 64001, "noise_seed": 74001, "bit_seconds": 64, "message": "A", "cn0_db_hz": 20}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 12000000, 300))
        runner._validate_completed_receipt(receipt, episode)
        changes = ((("plan", "channel_renderer"), "analytic_rays"),
            (("scenario", "channel", "renderer"), "analytic_rays"),
            (("scenario", "channel", "fullwave_reflection", "carrier_hz"), 3401000),
            (("plan", "fullwave_reflection", "transition_seconds"), 480),
            (("plan", "fullwave_reflection", "filter_halo_samples"), 127),
            (("plan", "fullwave_reflection", "first_lag_samples"), True),
            (("plan", "source_stop_sample"), math.ceil(540.25 * 1920) - 1),
            (("plan", "horizon_samples"), receipt["plan"]["source_stop_sample"] + 128),
            (("plan", "represented_seconds"), 1.), (("plan", "channel_tail_seconds"), 128 / 1920),
            (("plan", "public_bounds", "arrival_high_seconds"), 128 / 1920),
            (("plan", "public_bounds", "receiver_filter_halo_samples"), 16),
            (("plan", "public_slots", 13, "observation", "first"), receipt["plan"]["source_stop_sample"] + 128),
            (("plan", "public_slots", 14, "observation", "stop"), receipt["plan"]["horizon_samples"] - 1),
            (("plan", "fullwave_reflection_source_band_checked"), False),
            (("plan", "fullwave_reflection_table_built"), False), (("plan", "noise_variance"), .1),
            (("plan", "cn0_reference_plane"), "renormalized reflected signal"),
            (("channel", "samples"), receipt["plan"]["horizon_samples"] - 1),
            (("channel", "hashed_samples"), 1), (("channel", "source_samples"), 1),
            (("channel", "input_sha256"), "bad"), (("channel", "tail_seconds"), 128 / 1920),
            (("channel", "maximum_delay_seconds"), 129 / 1920),
            (("channel", "fullwave_reflection", "regions", 0, "samples"), 115200),
            (("channel", "fullwave_reflection", "regions", 1, "name"), "after"),
            (("channel", "fullwave_reflection", "regions", 2, "reflected_energy_sample_sum"), 70.))
        for path, value in changes:
            bad = json.loads(json.dumps(receipt))
            target = bad
            for key in path[:-1]:
                target = target[key]
            target[path[-1]] = value
            with self.subTest(path=path), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad, episode)

    def test_fullwave_qualification_never_treats_unknown_or_failed_errors_as_passes(self) -> None:
        episode = {"id": "fullwave", "scenario": "hf-fullwave-reflection", "mode": "channel", "waveform": "reference",
                   "seed": 1, "bit_seconds": 64, "message": "A"}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 12000000, 300))
        changes = ((("table_qualified",), False), (("witnesses_qualified",), False), (("table_sha256",), "bad"),
            (("table_digest_scope",), "one selected profile"), (("rf_solves",), 32069),
            (("knot_profiles_checked",), 64), (("holdout_profiles_checked",), 0),
            (("maximum_profile_holdout_absolute_error",), [-1, .001]),
            (("maximum_profile_holdout_absolute_error",), [None, .001]),
            (("maximum_witness_frequency_absolute_error",), [.0051, .001]),
            (("maximum_witness_mesh_absolute_error",), [math.nan, .001]),
            (("maximum_discarded_energy_fraction",), [2e-6, 1e-8]),
            (("maximum_frozen_flux_excess",), .02), (("retarded_peak_error_bound",), .0051),
            (("carrier_flight_phase_bound_radians",), math.nan), (("preparation_seconds",), -1),
            (("table_workspace_bytes",), 1048576), (("stream_workspace_bytes",), 0),
            (("source_minimum_frequency_hz",), -769), (("source_maximum_frequency_hz",), 769))
        for path, value in changes:
            bad = json.loads(json.dumps(receipt))
            target = bad["channel"]["fullwave_reflection"]
            target[path[-1]] = value
            with self.subTest(path=path), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad, episode)
        # The carrier-flight figure is explicitly a proxy, not a separate gate.
        receipt["channel"]["fullwave_reflection"]["carrier_flight_phase_bound_radians"] = 100.
        runner._validate_completed_receipt(receipt, episode)
        for field, value in (("channel_preparation_seconds", 1.), ("wall_seconds", 1.),
                             ("isolated_receiver_service_measured", True), ("capacity_qualified", True)):
            bad = json.loads(json.dumps(receipt))
            bad["timing"][field] = value
            with self.subTest(field=field), self.assertRaisesRegex(runner.CampaignError, "total wall time"):
                runner._validate_completed_receipt(bad, episode)

    def test_fullwave_plan_and_noise_control_keep_unexecuted_and_zero_signal_scopes(self) -> None:
        episode = {"id": "fullwave", "scenario": "hf-fullwave-reflection", "mode": "plan", "waveform": "reference",
                   "seed": 1, "bit_seconds": 64, "message": "A"}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 12000000, 300))
        runner._validate_completed_receipt(receipt, episode)
        self.assertNotIn("channel", receipt)
        for field in ("fullwave_reflection_source_band_checked", "fullwave_reflection_table_built"):
            bad = json.loads(json.dumps(receipt))
            bad["plan"][field] = True
            with self.assertRaisesRegex(runner.CampaignError, "unexecuted planning"):
                runner._validate_completed_receipt(bad, episode)
        noise = dict(episode, mode="receive", control="noise", cn0_db_hz=40)
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), noise, 12000000, 300))
        runner._validate_completed_receipt(receipt, noise)
        self.assertEqual(receipt["channel"]["signal_energy_sample_sum"], 0.)
        self.assertGreater(receipt["channel"]["source_samples"], 540 * 1920)
        bad = json.loads(json.dumps(receipt))
        bad["channel"]["fullwave_reflection"]["regions"][0]["incident_energy_sample_sum"] = .1
        bad["channel"]["input_energy_sample_sum"] = .1
        with self.assertRaisesRegex(runner.CampaignError, "exactly zero"):
            runner._validate_completed_receipt(bad, noise)
        old_episode = {"id": "old", "scenario": "awgn", "mode": "receive", "waveform": "reference", "seed": 1}
        old = fake_completed_receipt(runner._command(Path("/native/probe"), old_episode, 12000000, 300))
        old["plan"]["fullwave_reflection"] = receipt["plan"]["fullwave_reflection"]
        with self.assertRaisesRegex(runner.CampaignError, "unrequested full-wave"):
            runner._validate_completed_receipt(old, old_episode)

    def test_magnetoionic_scenarios_require_fixed_native_reception_scope(self) -> None:
        for scenario in runner.MAGNETOIONIC_REFLECTION:
            component, _, _, spread = runner.MAGNETOIONIC_REFLECTION[scenario]
            for mode, controls in (("plan", ("wanted",)), ("channel", ("wanted",)),
                                   ("receive", ("wanted", "noise", "wrong-key"))):
                for control in controls:
                    episode={"id":"magnetic","scenario":scenario,"mode":mode,"waveform":"reference",
                             "seed":64001,"bit_seconds":64,"message":"A","cn0_db_hz":40}
                    if mode == "receive": episode["control"]=control
                    runner._validate_episode(episode,0,set(),300,12_000_000)
                    receipt=fake_completed_receipt(runner._command(Path("/native/probe"),episode,12_000_000,300))
                    self.assertEqual(receipt["scenario"]["channel"]["magnetoionic_reflection"]["receive_component"],component)
                    self.assertEqual(receipt["plan"]["public_bounds"]["spread_seconds"],spread)
                    runner._validate_completed_receipt(receipt,episode)
        base={"id":"magnetic","scenario":"hf-magnetoionic-x","mode":"receive","waveform":"reference",
              "seed":1,"bit_seconds":64,"message":"A"}
        invalid=({"helper":True},{"backend":"cuda"},{"pulse":"gaussian"},{"fec":"rs20"},
                 {"mode":"plan","control":"wrong-key"},{"mode":"channel","control":"noise"},
                 {"duration_seconds":10},{"capture_lead_seconds":.5},{"hf_trajectory_seed":1})
        for override in invalid:
            with self.subTest(override=override), self.assertRaises(runner.CampaignError):
                runner._validate_episode(dict(base,**override),0,set(),300,12_000_000)

    def test_magnetoionic_tail_allows_roundoff_but_not_missing_samples(self) -> None:
        episode={"id":"magnetic","scenario":"hf-magnetoionic-x","mode":"receive",
                 "waveform":"reference","seed":1,"bit_seconds":64,"message":"A","cn0_db_hz":40}
        receipt=fake_completed_receipt(runner._command(Path("/native/probe"),episode,12_000_000,300))
        # The native tail is computed as first-lag/Fs + remaining-support/Fs.
        receipt["plan"]["channel_tail_seconds"]=1/1920+80/1920
        receipt["channel"]["tail_seconds"]=1/1920+80/1920
        runner._validate_completed_receipt(receipt,episode)
        for section, field in (("plan","channel_tail_seconds"),("channel","tail_seconds")):
            bad=json.loads(json.dumps(receipt));bad[section][field]=80/1920
            with self.subTest(section=section), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad,episode)

    def test_magnetoionic_scale_counts_cover_only_occupied_witnesses(self) -> None:
        episode={"id":"magnetic","scenario":"hf-magnetoionic-x","mode":"receive",
                 "waveform":"reference","seed":1,"bit_seconds":64,"message":"A","cn0_db_hz":40}
        receipt=fake_completed_receipt(runner._command(Path("/native/probe"),episode,12_000_000,300))
        # Native observe_scale keeps 11 of the 17 RF offsets: both occupied
        # edges, six inner anchors and the three moving-cutoff neighbors.
        counts=(("knot_offgrid_scale",187,289),("profile_holdout_scale",352,544),
                ("witness_mesh_scale",33,51))
        for field,occupied,all_offsets in counts:
            self.assertEqual([row["samples"] for row in receipt["channel"]["magnetoionic_reflection"][field]],
                             [occupied,occupied])
            for invalid_count in (occupied-1,occupied+1,all_offsets):
                bad=json.loads(json.dumps(receipt))
                bad["channel"]["magnetoionic_reflection"][field][0]["samples"]=invalid_count
                with self.subTest(field=field,count=invalid_count), self.assertRaisesRegex(runner.CampaignError,"witness counts"):
                    runner._validate_completed_receipt(bad,episode)
        self.assertEqual(receipt["channel"]["magnetoionic_reflection"]["rf_solves"],3444)
        runner._validate_completed_receipt(receipt,episode)

    def test_magnetoionic_zero_field_has_no_maximum_relative_witness(self) -> None:
        episode={"id":"magnetic","scenario":"hf-magnetoionic-x","mode":"receive",
                 "waveform":"reference","seed":1,"bit_seconds":64,"message":"A","cn0_db_hz":40}
        receipt=fake_completed_receipt(runner._command(Path("/native/probe"),episode,12_000_000,300))
        zero={"raw_amplitude":0.,"absolute_error":0.,"relative_error":None,"q":.5,"rf_offset_hz":-384.}
        fields=("knot_offgrid_scale","profile_holdout_scale","witness_mesh_scale")
        for field in fields:
            row=receipt["channel"]["magnetoionic_reflection"][field][0]
            row.update(exact_zero_raw=row["samples"],absolute_error_at_least_raw=row["samples"],
                       minimum_raw_amplitude=dict(zero),raw_at_maximum_absolute_error=dict(zero),
                       maximum_relative_error=None)
        runner._validate_completed_receipt(receipt,episode)
        for field in fields:
            for key,value in (("maximum_relative_error",dict(zero)),("exact_zero_raw",0),
                              ("absolute_error_at_least_raw",0)):
                bad=json.loads(json.dumps(receipt))
                bad["channel"]["magnetoionic_reflection"][field][0][key]=value
                with self.subTest(field=field,key=key), self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(bad,episode)
            bad=json.loads(json.dumps(receipt))
            bad["channel"]["magnetoionic_reflection"][field][1]["maximum_relative_error"]=None
            with self.subTest(field=field,case="nonzero witness omitted"), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad,episode)
            bad=json.loads(json.dumps(receipt))
            del bad["channel"]["magnetoionic_reflection"][field][0]["maximum_relative_error"]
            with self.subTest(field=field,case="zero-field null omitted"), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad,episode)

    def test_magnetoionic_passivity_uses_the_frozen_native_bounds(self) -> None:
        episode={"id":"magnetic","scenario":"hf-magnetoionic-x","mode":"receive",
                 "waveform":"reference","seed":1,"bit_seconds":64,"message":"A","cn0_db_hz":40}
        receipt=fake_completed_receipt(runner._command(Path("/native/probe"),episode,12_000_000,300))
        amplitude_bound=math.sqrt(1+1e-8)+math.sqrt(2)*.005
        reflected_limit=amplitude_bound*amplitude_bound-1
        full=receipt["channel"]["magnetoionic_reflection"]
        full["maximum_solver_output_power"]=1+1e-8
        full["maximum_frozen_reflected_power_excess"]=reflected_limit
        runner._validate_completed_receipt(receipt,episode)
        for field,limit in (("maximum_solver_output_power",1+1e-8),
                            ("maximum_frozen_reflected_power_excess",reflected_limit)):
            bad=json.loads(json.dumps(receipt))
            bad["channel"]["magnetoionic_reflection"][field]=math.nextafter(limit,math.inf)
            with self.subTest(field=field), self.assertRaisesRegex(runner.CampaignError,"frozen-passivity"):
                runner._validate_completed_receipt(bad,episode)

    def test_magnetoionic_native_energy_totals_match_regional_components(self) -> None:
        episode={"id":"magnetic","scenario":"hf-magnetoionic-x","mode":"receive",
                 "waveform":"reference","seed":1,"bit_seconds":64,"message":"A","cn0_db_hz":40}
        receipt=fake_completed_receipt(runner._command(Path("/native/probe"),episode,12_000_000,300))
        component_totals=("reflected_x_energy_sample_sum","reflected_y_energy_sample_sum",
                          "reflected_vector_energy_sample_sum")
        for field in component_totals:
            self.assertNotIn(field,receipt["channel"])
        runner._validate_completed_receipt(receipt,episode)
        for field in component_totals:
            redundant=json.loads(json.dumps(receipt))
            total=sum(region[field] for region in redundant["channel"]["magnetoionic_reflection"]["regions"])
            redundant["channel"][field]=total
            runner._validate_completed_receipt(redundant,episode)
            redundant["channel"][field]=total+1.
            with self.subTest(field=field), self.assertRaisesRegex(runner.CampaignError,"component energies"):
                runner._validate_completed_receipt(redundant,episode)
        for field in ("input_energy_sample_sum","signal_energy_sample_sum"):
            bad=json.loads(json.dumps(receipt));del bad["channel"][field]
            with self.subTest(field=field,case="required total missing"), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad,episode)

    def test_magnetoionic_actual_native_receipt_schema(self) -> None:
        fixture=json.loads((Path(__file__).parent/"fixtures"/"hf_magnetoionic_native_schema.json").read_text())
        # This unmodified field projection of the first native receipt catches
        # schema drift that a hand-written completed-receipt fixture can hide.
        # It validates the magnetic contract only, without replaying DSP work.
        receipt=fixture["receipt"]
        runner._validate_magnetoionic_reflection(receipt,fixture["episode"])
        self.assertEqual(receipt["plan"]["channel_tail_seconds"],1/1920+80/1920)
        self.assertNotEqual(receipt["plan"]["channel_tail_seconds"],81/1920)
        self.assertEqual([receipt["channel"]["magnetoionic_reflection"][field][0]["samples"]
                          for field in ("knot_offgrid_scale","profile_holdout_scale","witness_mesh_scale")],
                         [187,352,33])
        self.assertNotIn("reflected_vector_energy_sample_sum",receipt["channel"])

    def test_parallel_magnetic_path_requires_refinement_and_original_null_guard(self) -> None:
        episode={"id":"parallel","scenario":"hf-magnetoionic-parallel-x","mode":"receive",
                 "waveform":"reference","seed":66001,"bit_seconds":64,"message":"A"}
        receipt=fake_completed_receipt(runner._command(Path("/native/probe"),episode,12_000_000,600))
        runner._validate_completed_receipt(receipt,episode)
        full=receipt["channel"]["magnetoionic_reflection"]
        self.assertEqual((full["profile_intervals"],full["rf_solves"],full["knot_profiles_checked"],full["holdout_profiles_checked"]),
                         (64,12039,65,128))
        # Exact B-parallel symmetry decouples Ez, including epsilon_zz=0.
        full["minimum_longitudinal_permittivity"]=0.
        runner._validate_completed_receipt(receipt,episode)
        changes=((('plan','magnetoionic_reflection','physical_path'),'tilted_lower'),
                 (('plan','magnetoionic_reflection','magnetic_field_tesla'),[25e-6,0.,43.30127018922193e-6]),
                 (('plan','magnetoionic_reflection','profile_intervals'),32),
                 (('channel','magnetoionic_reflection','rf_solves'),12036),
                 (('channel','magnetoionic_reflection','knot_offgrid_scale',0,'samples'),357),
                 (('channel','magnetoionic_reflection','profile_holdout_scale',0,'samples'),698),
                 (('channel','magnetoionic_reflection','witness_mesh_scale',0,'samples'),33),
                 (('channel','magnetoionic_reflection','original_null_witness'),None),
                 (('channel','magnetoionic_reflection','original_null_witness','x_resolved'),False),
                 (('channel','magnetoionic_reflection','original_null_witness','x_local_relative_tolerance'),.3),
                 (('channel','magnetoionic_reflection','original_null_witness','q'),.5),
                 (('channel','magnetoionic_reflection','original_null_witness','witness_meshes_m'),[10.,5.,1.]),
                 (('channel','magnetoionic_reflection','original_null_witness','fir_to_2p5m_absolute_error_xy',0),.001),
                 (('channel','magnetoionic_reflection','original_null_witness','x_guarded_absolute_error'),0.),
                 (('channel','magnetoionic_reflection','original_null_witness','x_guarded_power_interval'),[0.,1.]))
        for path,value in changes:
            bad=json.loads(json.dumps(receipt));target=bad
            for key in path[:-1]:target=target[key]
            target[path[-1]]=value
            with self.subTest(path=path),self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad,episode)

    def test_magnetic_oracle_requires_three_qualified_matching_tables(self) -> None:
        for scenario in ('hf-magnetoionic-x','hf-magnetoionic-parallel-x'):
            episode={"id":"oracle","scenario":scenario,"mode":"oracle","control":"wanted",
                     "waveform":"reference","seed":65001,"bit_seconds":64,"message":"A","oracle_slot":0}
            runner._validate_episode(episode,0,set(),600,12_000_000)
            receipt=fake_completed_receipt(runner._command(Path('/native/probe'),episode,12_000_000,600))
            runner._validate_completed_receipt(receipt,episode)
            self.assertNotIn('channel',receipt)
            changes=((('plan','complete_transition_span_required'),True),
                     (('plan','oracle_physical_channel_instances'),2),
                     (('oracle','same_input_full_message_replay'),True),
                     (('presence_oracle','field_sensitivity_bound'),True),
                     (('oracle','physical_table_rf_solves'),3444),
                     (('oracle','physical_table_preparation_seconds'),2.),
                     (('oracle','signal_and_tail_horizon_samples'),134112),
                     (('oracle','magnetoionic_channel_hypothesis_1','magnetoionic_reflection','table_sha256'),'d'*64),
                     (('oracle','magnetoionic_channel_noisy','magnetoionic_reflection','witness_mesh_error',0,'maximum'),.006),
                     (('oracle','magnetoionic_channel_hypothesis_0','magnetoionic_reflection','regions',2,'samples'),1),
                     (('outcomes','receiver_executed'),True))
            for path,value in changes:
                bad=json.loads(json.dumps(receipt));target=bad
                for key in path[:-1]:target=target[key]
                target[path[-1]]=value
                with self.subTest(scenario=scenario,path=path),self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(bad,episode)

    def test_gannon_fixed_records_and_supported_modes(self) -> None:
        for scenario in runner.GANNON_REPLAY:
            for mode in ("plan","channel","receive","oracle"):
                controls=("wanted","noise","wrong-key") if mode in {"channel","receive"} else ("wanted",)
                for control in controls:
                    episode={"id":"gannon","scenario":scenario,"mode":mode,"waveform":"reference",
                             "seed":67002,"bit_seconds":64,"message":"B","cn0_db_hz":20}
                    if mode!="plan":episode["control"]=control
                    runner._validate_episode(episode,0,set(),600,12_000_000)
                    receipt=fake_completed_receipt(runner._command(Path("/native/probe"),episode,12_000_000,600))
                    runner._validate_completed_receipt(receipt,episode)
        campaign=runner._read_campaign(RUNNER.parent/"campaigns"/"hf-gannon-receiver.json")
        self.assertEqual(len(campaign["episodes"]),6)

    def test_gannon_rejects_changed_provenance_scale_or_physical_claims(self) -> None:
        episode={"id":"gannon","scenario":"hf-gannon-2000-late","mode":"receive","waveform":"reference",
                 "seed":67002,"bit_seconds":64,"message":"B"}
        receipt=fake_completed_receipt(runner._command(Path("/native/probe"),episode,12_000_000,600))
        changes={"samples":35999,"sample_rate_hz":50,"part":"constructed-amplitude-only",
            "record_offset_seconds":0.,"source_sha256":"0"*64,"serialized_iq_sha256":"f"*64,
            "source_archive_member":"different/hour/same-name.h5","stream_uuid":"another-stream",
            "source_subchannels":1,"zero_based_column":0,"nominal_center_frequency_hz":5000000,
            "capture_utc_first_sample_epoch_seconds":1715371200,"normalization_cohort_knots":36000,
            "normalization_cohort_utc_hours":[20],"normalization_gain":1.,"raw_mean_power":1.,
            "noise_free_channel_response":True,"wideband_channel_response_qualified":True,
            "field_false_alarm_or_availability_qualified":True,"absolute_link_level_qualified":True,
            "receiver_clock_discipline_qualified":True,"recorded_noise_becomes_signal_proportional":False}
        for key,value in changes.items():
            bad=json.loads(json.dumps(receipt))
            bad["scenario"]["channel"]["paths"][0]["hf"]["measured_envelope"][key]=value
            with self.subTest(field=key),self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad,episode)
        bad=json.loads(json.dumps(receipt));bad["channel"]["samples"]=int(1800*1920)+1
        with self.assertRaisesRegex(runner.CampaignError,"finite selected record"):
            runner._validate_completed_receipt(bad,episode)

    def test_gannon_full_prefix_shadow_remains_conditional(self) -> None:
        campaign=runner._read_campaign(RUNNER.parent/"campaigns"/"hf-gannon-recorded-failure.json")
        self.assertEqual(len(campaign["episodes"]),2)
        baseline,episode=campaign["episodes"]
        self.assertEqual(baseline["capture_digest_slot"],episode["search_slot"])
        for field in ("scenario","waveform","seed","noise_seed","bit_seconds","message","cn0_db_hz","control"):
            self.assertEqual(baseline[field],episode[field])
        for override in ({"control":"noise"},{"control":"wrong-key"},{"control":"wrong-ordinal"}):
            with self.subTest(override=override),self.assertRaises(runner.CampaignError):
                runner._validate_episode(dict(episode,**override),0,set(),90,3_000_000)
        receipt=fake_completed_receipt(runner._command(Path("/native/probe"),episode,3_000_000,90))
        runner._validate_completed_receipt(receipt,episode)
        for field in ("recording_noise_in_multiplier","physical_noise_free_channel_known","field_sensitivity_bound"):
            bad=json.loads(json.dumps(receipt))
            binary=bad["hf_physical_diagnostic"]["known_binary"]
            binary[field]=not binary[field]
            with self.subTest(field=field),self.assertRaisesRegex(runner.CampaignError,"observed-voltage noise"):
                runner._validate_completed_receipt(bad,episode)

    def test_gannon_target_absence_and_native_coherent_gain_receipt(self) -> None:
        campaign = runner._read_campaign(RUNNER.parent / "campaigns" / "hf-gannon-native-comparison.json")
        episode, = campaign["episodes"]
        command = runner._command(Path("/native/probe"), episode, 500_000, 45)
        self.assertIn("--hf-gannon-native-diagnostic", command)
        for override in ({"hf_gannon_native_diagnostic": 1}, {"hf_physical_diagnostic": False},
                         {"search_timing": "selected"}, {"scenario": "awgn"}):
            with self.subTest(override=override), self.assertRaises(runner.CampaignError):
                runner._validate_episode(dict(episode, **override), 0, set(), 45, 500_000)
        scores = {"coordinate_log_prior_mass": -4., "candidates": 2, "blocks": 3,
                  "frequency_states": 7, "basis_rows": 240, "seconds": .1,
                  "bit_log_e_before_coordinate_mass": [0., 7.], "bit_log_e_after_coordinate_mass": [-4., 3.]}
        physical = {
            "prefix_samples_rendered_per_twin": 100,
            "known_target_presence": {"fixed_known_context": True, "field_sensitivity_bound": False,
                "used_for_receiver_decisions": False, "transmit_range_excludes_preroll": True,
                "pre_aperture_matches_wanted": True,
                "prefix_samples_rendered": 100, "absent_template_sha256": "a" * 64,
                "muted_transmit_samples": {"first": 20, "stop": 80}, "absent_aperture_energy": 0.,
                "absent_received_prefix_energy": 4., "signal_difference_energy": 20.,
                "distance_over_noise_variance": 10., "observed_wanted_vs_absent_log_likelihood_ratio": 9.},
            "native_coherent_gain_comparison": {"same_noisy_selected_coordinate_bank": True,
                "private_clean_used_for_coordinate_selection": False, "used_for_receiver_decisions": False,
                "independent_and_persistent_likelihoods_reported_separately": False, "score_noise_variance": 2.,
                "block_rows": 24, "bounded_scratch_bytes": 20000, "workers": 2,
                "noisy": scores, "private_wanted_template": dict(scores)},
        }
        plan = {"source_stop_sample": 120, "noise_variance": 2.}
        runner._validate_gannon_native_comparison(physical, plan)
        for section, field, bad_value in (
            ("known_target_presence", "prefix_samples_rendered", 90),
            ("known_target_presence", "pre_aperture_matches_wanted", False),
            ("known_target_presence", "distance_over_noise_variance", 12.),
            ("native_coherent_gain_comparison", "same_noisy_selected_coordinate_bank", False),
            ("native_coherent_gain_comparison", "private_wanted_template", dict(scores, frequency_states=3)),
            ("native_coherent_gain_comparison", "noisy", dict(scores, bit_log_e_after_coordinate_mass=[0., 7.])),
        ):
            with self.subTest(field=field), self.assertRaises(runner.CampaignError):
                altered = json.loads(json.dumps(physical))
                altered[section][field] = bad_value
                runner._validate_gannon_native_comparison(altered, plan)

    def test_lens_correlated_gain_shadow_binds_full_work_and_normalized_evidence(self) -> None:
        app = RUNNER.parent
        frozen = runner._read_campaign(app / "campaigns/hf-lens-correlated-gain-shadow.json")
        episode, = frozen["episodes"]
        baseline = runner._read_campaign(app / "campaigns/hf-wave-lens-followup.json")["episodes"][0]
        for field in ("scenario", "waveform", "seed", "noise_seed", "bit_seconds", "cn0_db_hz", "message", "control"):
            self.assertEqual(episode[field], baseline[field])
        self.assertEqual(episode["search_slot"], baseline["capture_digest_slot"])
        self.assertIn("--hf-correlated-gain-shadow", runner._command(Path("/native/probe"), episode, 3_000_000, 90))
        for change in ({"search_timing": "selected"}, {"hf_physical_diagnostic": True}, {"fec": "rs20"},
                       {"mode": "receive"}, {"control": "noise"}, {"scenario": "awgn"}):
            with self.subTest(change=change), self.assertRaises(runner.CampaignError):
                runner._validate_episode(dict(episode, **change), 0, set(), 90, 3_000_000)

        digest = "a" * 64
        quote = {"public_timing_atoms": 559, "observation_samples": 134921,
                 "basis_products": 150841678, "full_fft_row_stage_charge": 3551268864, "scalar_innovations": 607221576}
        plan = {"public_bounds": {"arrival_low_seconds": 4/1920, "arrival_high_seconds": 84/1920,
                                 "clock_bound_ppm": 0, "spread_seconds": 0, "frequency_bound_hz": 0, "atmospheric_hz": 5},
                "hf_correlated_gain_shadow_quote": quote, "receiver_memory_limit_bytes": 1000000000,
                "receiver_resources": {"total_memory_limit_bytes": 1100000000, "workers": 24}, "noise_variance": 1920}
        bits = [16., 25.]
        presence = 25 + math.log1p(math.exp(-9)) - math.log(2)
        coherence, powers = [.5, 2., 8.], [1/256, 1/32, 1/4, 2.]
        report = dict(quote, model="stationary_single_tap_complex_ar1", all_valid=True, observation_sha256=digest,
            component_complete=True, receiver_executed=False, completion_evaluated=False, capacity_qualified=False,
            truth_information_passed_to_search=False,
            all_public_timing_atoms_evaluated=True, sample_rate_hz=1920, phase_atom_rows=30, gain_block_rows=960,
            gain_blocks=141, frequency_count=321, fft_rows=2048, coherence_seconds=coherence, gain_powers=powers,
            closed_band_frequency_prior="uniform cell widths with half mass at both endpoints", frequency_step_hz=1/32,
            frequency_bound_hz=5, noise_variance=1920, standalone_native_weight=.125,
            maximum_nominal_phase_atom_error_radians=math.pi*5*30/1920, canonical_selected_timing_atoms=64,
            bit_log_e=bits, canonical_selected_bit_log_contribution=[x+math.log(.2) for x in bits],
            complement_bit_log_contribution=[x+math.log(.8) for x in bits], bitwise_maximum_unweighted_component_log_lr=[19., 31.],
            per_law_evidence=[{"coherence_seconds": seconds, "gain_power": power, "weight": 1/12,
                               "bit_log_e_before_law_weight": bits} for seconds in coherence for power in powers],
            presence_log_e=presence, charged_presence_log_e=presence-math.log(8), bit_separation=9.,
            presence_reference_threshold=21.493378688261398, bit_separation_reference=math.log(199), favored_bit=1,
            passes_standalone_candidate_screen=True, memory_limit_bytes=1000000000, process_and_stacks_reserved_bytes=1000,
            diagnostic_workspace_bytes=1000, coarse_workspace_reserved_bytes=1000, workers=24, active_timing_lanes=24,
            canonical_preparation_seconds=2., scoring_seconds=12.,
            summed_worker_wall_seconds={"basis_rendering": 1., "correlation_and_projection": 1., "gain_innovations": 1.},
            actual_work={"basis_products": 150841678, "fft_prefix_calls": 157638, "scalar_innovations": 607221576,
                "basis_bank_calls": 78819, "basis_bank_column_samples": 150841678, "basis_fallback_calls": 0,
                "coordinate_guard_fallbacks": 0, "mapped_basis_calls": 0, "mapped_basis_direct_rows": 0,
                "mapped_basis_interpolated_rows": 0, "basis_cache_peak_bytes": 0})
        runner._validate_correlated_gain_shadow(report, plan, digest, 134921)
        for field, value in (("observation_sha256", "b" * 64), ("public_timing_atoms", 512),
                             ("standalone_native_weight", .25), ("passes_standalone_candidate_screen", False),
                             ("complement_bit_log_contribution", bits), ("per_law_evidence", []),
                             ("actual_work", dict(report["actual_work"], fft_prefix_calls=157637))):
            with self.subTest(field=field), self.assertRaises(runner.CampaignError):
                runner._validate_correlated_gain_shadow(dict(report, **{field: value}), plan, digest, 134921)

        integrated = runner._read_campaign(app / "campaigns/hf-correlated-gain-receiver.json")
        self.assertEqual(len(integrated["episodes"]), 9)
        candidate = integrated["episodes"][0]
        self.assertIn("--hf-correlated-gain-receiver", runner._command(Path("/native/probe"), candidate, 3_072_000, 300))
        for change in ({"fec": "rs20"}, {"hf_compare_admitted_models": True}, {"hf_target_presence_diagnostic": True},
                       {"helper": True}, {"scenario": "awgn"}, {"mode": "hf-search"}):
            with self.subTest(change=change), self.assertRaises(runner.CampaignError):
                runner._validate_episode(dict(candidate, **change), 0, set(), 300, 3_072_000)
        model = dict(report, slot_index=2)
        del model["canonical_preparation_seconds"]
        native = {"index": 2, "complete": True, "all_valid": True, "first_sample": 345524, "stop_sample": 480445,
                  "decision": "data", "fine_candidates": 4, "correlated_gain_executed": True,
                  "workers": 24, "workspace_peak_bytes": 10000, "reason": "bounded_supplemental_data_evidence"}
        telemetry = {"experimental": True, "fixed_weight": .25, "canonical_preparation_reused": True,
                     "telemetry_overflow": 0, "telemetry_duplicates": 0, "scored_components": [model]}
        plan["receiver_resources"].update(hardware_threads=12, workers_explicit=False)
        plan.update(scored_slot_horizon=15, correlated_gain_evaluator_reserved_bytes=15*65536)
        plan["public_slots"] = [{"index": i, "observation": {"first": i*172800, "stop": i*172800+134921}}
                                for i in range(15)]
        receiver = {"plan": plan, "slots": [native], "hf_correlated_gain_receiver": telemetry}
        runner._validate_correlated_gain_receiver(receiver, candidate)
        for changes in ({"fixed_weight": .5}, {"telemetry_overflow": 1}, {"scored_components": []},
                        {"scored_components": [dict(model, workers=12)]},
                        {"scored_components": [dict(model, canonical_preparation_seconds=2.)]}):
            with self.subTest(changes=list(changes)), self.assertRaises(runner.CampaignError):
                runner._validate_correlated_gain_receiver(dict(receiver, hf_correlated_gain_receiver=dict(telemetry, **changes)), candidate)
        quiet = dict(native, decision="quiet", reason="scored_public_quiet_aperture")
        failed = dict(model, passes_standalone_candidate_screen=False,
                      presence_log_e=model["presence_log_e"]-5, charged_presence_log_e=model["charged_presence_log_e"]-5)
        for field in ("bit_log_e", "canonical_selected_bit_log_contribution", "complement_bit_log_contribution"):
            failed[field] = [x-5 for x in model[field]]
        failed["per_law_evidence"] = [dict(law, bit_log_e_before_law_weight=[x-5 for x in law["bit_log_e_before_law_weight"]])
                                      for law in model["per_law_evidence"]]
        runner._validate_correlated_gain_receiver(dict(receiver, slots=[quiet],
            hf_correlated_gain_receiver=dict(telemetry, scored_components=[failed])), candidate)
        omitted = dict(telemetry, scored_components=[])
        for slot in (quiet, dict(quiet, correlated_gain_executed=False),
                     {key: value for key, value in quiet.items() if key != "correlated_gain_executed"}):
            with self.subTest(quiet_marker=slot.get("correlated_gain_executed")), self.assertRaises(runner.CampaignError):
                runner._validate_correlated_gain_receiver(dict(receiver, slots=[slot], hf_correlated_gain_receiver=omitted), candidate)
        retained_line = dict(quiet, decision="ambiguous", reason="coherent_line_in_public_data_aperture", correlated_gain_executed=False)
        runner._validate_correlated_gain_receiver(dict(receiver, slots=[retained_line], hf_correlated_gain_receiver=omitted), candidate)

    def test_correlated_gain_duration_preflights_entire_calendar(self) -> None:
        app = RUNNER.parent
        original = runner._read_campaign(app / "campaigns/hf-wave-lens-followup.json")["episodes"][3]
        manifest = runner._read_campaign(app / "campaigns/hf-correlated-gain-duration.json")
        wanted, noise = manifest["episodes"]
        self.assertEqual(wanted, dict(original, id=wanted["id"], hf_correlated_gain_receiver=True, capture_digest_slot=2))
        self.assertEqual(noise, dict(original, id=noise["id"], hf_correlated_gain_receiver=True, control="noise", noise_seed=81006))
        self.assertEqual((manifest["max_wall_seconds"], manifest["per_trial_wall_seconds"], manifest["max_samples"]),
                         (1200, 600, 5_000_000))

        quote = {"planned_public_slots": 2, "workers": 24, "maximum_aperture_samples": 268553,
                 "maximum_public_timing_atoms": 559, "maximum_basis_products": 300242254,
                 "maximum_full_fft_row_stage_charge": 7052165120, "maximum_scalar_innovations": 1205830080,
                 "maximum_additional_workspace_bytes": 189000000}
        public_slots = [{"index": 0, "observation": {"first": 0, "stop": 268477}},
                        {"index": 1, "observation": {"first": 575924, "stop": 844477}}]
        plan = {"public_bounds": {"arrival_low_seconds": 4/1920, "arrival_high_seconds": 84/1920,
                                 "clock_bound_ppm": 0, "spread_seconds": 0, "frequency_bound_hz": 0, "atmospheric_hz": 5},
                "public_slots": public_slots, "hf_correlated_gain_calendar_quote": quote}
        self.assertEqual(runner._validate_correlated_gain_calendar_quote(plan, 24, 2), 189000000)
        for field, value in (("planned_public_slots", 1), ("workers", 12), ("maximum_aperture_samples", 268477),
                             ("maximum_full_fft_row_stage_charge", 7052165120-1), ("maximum_additional_workspace_bytes", 0)):
            with self.subTest(field=field), self.assertRaisesRegex(runner.CampaignError, "full-calendar"):
                runner._validate_correlated_gain_calendar_quote(dict(plan, hf_correlated_gain_calendar_quote=dict(quote, **{field: value})), 24, 2)
        with self.assertRaisesRegex(runner.CampaignError, "full-calendar"):
            runner._validate_correlated_gain_calendar_quote({k:v for k,v in plan.items() if k != "hf_correlated_gain_calendar_quote"}, 24, 2)
        over_limit = [public_slots[0], {"index": 1, "observation": {"first": 575924, "stop": 575924+286226}}]
        with self.assertRaisesRegex(runner.CampaignError, "work envelope"):
            runner._validate_correlated_gain_calendar_quote(dict(plan, public_slots=over_limit), 24, 2)

    def test_gannon_gain_variance_witness_keeps_fixed_cells_and_likelihood_masses(self) -> None:
        campaign = runner._read_campaign(RUNNER.parent / "campaigns" / "hf-gannon-gain-variance.json")
        baseline = runner._read_campaign(RUNNER.parent / "campaigns" / "hf-gannon-native-comparison.json")
        episode, = campaign["episodes"]
        self.assertEqual(episode, dict(baseline["episodes"][0], id=episode["id"], hf_gannon_gain_variance_diagnostic=True))
        self.assertIn("--hf-gannon-gain-variance-diagnostic", runner._command(Path("/native/probe"), episode, 500_000, 45))
        for override in ({"hf_gannon_gain_variance_diagnostic": 1}, {"hf_gannon_native_diagnostic": False},
                         {"hf_phase_refinement": False}):
            with self.subTest(override=override), self.assertRaises(runner.CampaignError):
                runner._validate_episode(dict(episode, **override), 0, set(), 45, 500_000)

        fields = ("noisy", "clean", "same_noise_residual")
        scores = list(range(-5, 1))
        mixture = math.log(sum(map(math.exp, scores)) / len(scores))
        log_mass = -math.log(52 * 4951 * 97)
        digest = "a" * 64
        search = {"public_timing_atoms": 52, "evaluated_timing_atoms": 52, "frequency_cells": 4951,
                  "presence_reference_threshold": 21.493, "families": [{}, {}, {"phase_models": 97}]}
        partition = {"requested_block_seconds": .25, "block_samples": 480, "block_count": 281}
        partitions = [dict(partition, hypotheses=[dict.fromkeys(fields, scores)] * 2)] * 5
        retained = [dict(partition, hypotheses=[{"noisy_log_likelihood_ratio": 0.}] * 2)] * 5
        cell = {"partitions": partitions, "equal_weight_log_mixture": dict.fromkeys(fields, mixture)}
        witness = {"observation_sha256": digest, "prior_variances": [1/4096, 1/512, 1/256, 1/64, 1/16, 1.],
                   "used_for_receiver_decisions": False, "full_bank_evaluated_at_new_variances": False,
                   "independent_negative_control": False, "presence_reference_threshold": 21.493,
                   "fixed_coordinate_prior": {"timings": 52, "frequencies": 4951, "sinusoidal_models": 97, "log_mass": log_mass},
                   "winning_public_cell": dict(cell, charged_log_contribution=dict.fromkeys(fields, mixture + log_mass)),
                   "refined_public_cell": cell}
        physical = {"observation_sha256": digest, "gain_variance_witness": witness,
                    "gain_partitions": dict.fromkeys(("winning_public_cell", "refined_public_cell"), retained)}
        runner._validate_gannon_gain_variance({"hf_public_search": search}, physical)
        for section, field, value in (
            ("fixed_coordinate_prior", "timings", 51),
            ("winning_public_cell", "charged_log_contribution", dict.fromkeys(fields, mixture)),
            ("refined_public_cell", "charged_log_contribution", dict.fromkeys(fields, mixture + log_mass)),
            ("winning_public_cell", "equal_weight_log_mixture", dict.fromkeys(fields, sum(scores) / len(scores))),
        ):
            with self.subTest(section=section, field=field), self.assertRaises(runner.CampaignError):
                bad = json.loads(json.dumps(physical))
                bad["gain_variance_witness"][section][field] = value
                runner._validate_gannon_gain_variance({"hf_public_search": search}, bad)

    def test_gannon_noise_and_oracle_remain_conditional(self) -> None:
        base={"id":"gannon","scenario":"hf-gannon-1700","mode":"receive","waveform":"reference",
              "seed":1,"bit_seconds":64,"message":"A","control":"noise"}
        receipt=fake_completed_receipt(runner._command(Path("/native/probe"),base,12_000_000,600))
        receipt["channel"]["signal_energy_sample_sum"]=.1
        with self.assertRaisesRegex(runner.CampaignError,"exactly zero"):
            runner._validate_completed_receipt(receipt,base)
        oracle=dict(base,mode="oracle",control="wanted")
        receipt=fake_completed_receipt(runner._command(Path("/native/probe"),oracle,12_000_000,600))
        for section in ("oracle","presence_oracle"):
            for field in ("same_input_full_message_replay","physical_noise_free_channel_known","field_sensitivity_bound"):
                bad=json.loads(json.dumps(receipt));bad[section][field]=True
                with self.subTest(section=section,field=field),self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(bad,oracle)
        for override in ({"mode":"slot"},{"mode":"oracle","control":"noise"},{"service_replay":True},
                         {"hf_public_phase_receiver":True},{"hf_compare_admitted_models":True}):
            with self.subTest(override=override),self.assertRaises(runner.CampaignError):
                runner._validate_episode(dict(base,**override),0,set(),600,12_000_000)

    def test_magnetoionic_plan_does_not_claim_table_execution(self) -> None:
        episode={"id":"magnetic","scenario":"hf-magnetoionic-x-holdout","mode":"plan",
                 "waveform":"reference","seed":1,"bit_seconds":64,"message":"B"}
        receipt=fake_completed_receipt(runner._command(Path("/native/probe"),episode,12_000_000,300))
        runner._validate_completed_receipt(receipt,episode)
        for field in ("magnetoionic_reflection_source_band_checked","magnetoionic_reflection_table_built"):
            bad=json.loads(json.dumps(receipt));bad["plan"][field]=True
            with self.subTest(field=field), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad,episode)
        bad=json.loads(json.dumps(receipt));bad["channel"]={"magnetoionic_reflection":{}}
        with self.assertRaisesRegex(runner.CampaignError,"planning cannot claim"):
            runner._validate_completed_receipt(bad,episode)

    def test_magnetoionic_execution_rejects_forged_physics_numerics_and_energy(self) -> None:
        episode={"id":"magnetic","scenario":"hf-magnetoionic-y","mode":"receive",
                 "waveform":"reference","seed":1,"bit_seconds":64,"message":"A","cn0_db_hz":40}
        receipt=fake_completed_receipt(runner._command(Path("/native/probe"),episode,12_000_000,300))
        runner._validate_completed_receipt(receipt,episode)
        changes=(
            (("plan","channel_renderer"),"hf_fullwave_reflection"),
            (("scenario","channel","magnetoionic_reflection","magnetic_field_tesla",0),1e-3),
            (("plan","magnetoionic_reflection","profile_q1","f_peak_plasma_frequency_hz"),1.),
            (("plan","source_stop_sample"),1),
            (("plan","public_bounds","arrival_high_seconds"),80/1920),
            (("plan","magnetoionic_reflection_table_built"),False),
            (("channel","magnetoionic_reflection","rf_solves"),3443),
            (("channel","magnetoionic_reflection","table_sha256"),"bad"),
            (("channel","magnetoionic_reflection","witness_mesh_error",0,"maximum"),.0051),
            (("channel","magnetoionic_reflection","profile_holdout_error",1,"profile_q"),2.),
            (("channel","magnetoionic_reflection","maximum_discarded_energy_fraction"),[1e-6,1.1e-6]),
            (("channel","magnetoionic_reflection","knot_offgrid_scale",0,"samples"),3445),
            (("channel","magnetoionic_reflection","profile_holdout_scale",1,"exact_zero_raw"),41),
            (("channel","magnetoionic_reflection","witness_mesh_scale",0,"maximum_relative_error","relative_error"),.01),
            (("channel","magnetoionic_reflection","witness_mesh_scale",1,"raw_at_maximum_absolute_error","rf_offset_hz"),385.),
            (("channel","magnetoionic_reflection","retarded_peak_error_bound"),[.0051,.001]),
            (("channel","magnetoionic_reflection","minimum_longitudinal_permittivity"),0.),
            (("channel","magnetoionic_reflection","maximum_frozen_reflected_power_excess"),.1),
            (("channel","magnetoionic_reflection","maximum_carrier_power_xy_total"),[.1,.2,1.1]),
            (("channel","magnetoionic_reflection","source_maximum_frequency_hz"),385.),
            (("channel","magnetoionic_reflection","table_workspace_bytes"),2_097_153),
            (("channel","magnetoionic_reflection","retarded_peak_error_bound",0),.006),
            (("channel","magnetoionic_reflection","regions",1,"reflected_y_energy_sample_sum"),4.),
            (("channel","magnetoionic_reflection","regions",0,"selected_antenna_energy_sample_sum"),2.5),
            (("channel","signal_energy_sample_sum"),11.),
            (("timing","isolated_receiver_service_measured"),True),
        )
        for path,value in changes:
            bad=json.loads(json.dumps(receipt));target=bad
            for key in path[:-1]: target=target[key]
            target[path[-1]]=value
            with self.subTest(path=path),self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad,episode)
        # A wrong-key acceptance is a communications failure to report, not
        # an invalid receipt to discard.
        wrong=dict(episode,control="wrong-key")
        bad=fake_completed_receipt(runner._command(Path("/native/probe"),wrong,12_000_000,300),
            outcomes={"receiver_executed":True,"component_only":False,"completion_evaluated":True,
                      "correct_complete":False,"physical_completions":1,"actual_scored_slots":15,
                      "premature_completions":0,"wrong_bits":0,"false_data":0,
                      "ambiguous_active_slots":0,"quiet_active_slots":0,"missed_completion":False})
        runner._validate_completed_receipt(bad,wrong)

    def test_magnetoionic_noise_control_requires_zero_vector_signal_each_region(self) -> None:
        episode={"id":"magnetic-noise","scenario":"hf-magnetoionic-x","mode":"receive",
                 "waveform":"reference","seed":1,"bit_seconds":64,"message":"A","cn0_db_hz":40,
                 "control":"noise"}
        receipt=fake_completed_receipt(runner._command(Path("/native/probe"),episode,12_000_000,300))
        runner._validate_completed_receipt(receipt,episode)
        self.assertFalse(receipt["channel"]["contains_source_energy"])
        self.assertEqual(receipt["channel"]["signal_energy_sample_sum"],0.)
        for path,value in ((("channel","magnetoionic_reflection","regions",2,"reflected_x_energy_sample_sum"),.1),
                           (("channel","magnetoionic_reflection","regions",0,"incident_energy_sample_sum"),1.),
                           (("channel","contains_source_energy"),True),
                           (("channel","reflected_vector_energy_sample_sum"),.1)):
            bad=json.loads(json.dumps(receipt));target=bad
            for key in path[:-1]: target=target[key]
            target[path[-1]]=value
            with self.subTest(path=path),self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad,episode)

    def test_gui_varying_inputs_require_explicit_cpu_component_scope(self) -> None:
        episode = gui_varying_episode()
        for control in ("wanted", "noise", "wrong-key"):
            for selected in (0, 2):
                declared = dict(episode, control=control, capture_digest_slot=selected)
                runner._validate_episode(declared, 0, set(), 10, 1000)
                command = runner._command(Path("/native/probe"), declared, 1000, 2)
                self.assertEqual(command[command.index("--mode") + 1], "gui-varying")
                self.assertEqual(command[command.index("--capture-digest-slot") + 1], str(selected))
                runner._validate_completed_receipt(fake_completed_receipt(command), declared)
        missing = dict(episode)
        del missing["capture_digest_slot"]
        for invalid in [missing] + [dict(episode, **override) for override in (
                {"scenario": "awgn"}, {"waveform": "smoke"}, {"helper": True}, {"backend": "cuda"},
                {"control": "wrong-ordinal"}, {"control": "eof"},
                {"control": "interior-fade"}, {"capture_digest_slot": True}, {"capture_digest_slot": -1},
                {"search_slot": 2}, {"search_timing": "full"}, {"oracle_slot": 2}, {"duration_seconds": 1},
                {"hf_public_phase_receiver": False}, {"hf_phase_refinement": False}, {"pulse": "tapered-rrc"})]:
            with self.subTest(invalid=invalid), self.assertRaises(runner.CampaignError):
                runner._validate_episode(invalid, 0, set(), 10, 1000)

    def test_gui_clean_diagnostic_flag_scope_and_legacy_receipts(self) -> None:
        for control in ("wanted", "noise", "wrong-key"):
            episode = gui_varying_episode(control=control, hf_gui_clean_diagnostic=True, chunk=17)
            runner._validate_episode(episode, 0, set(), 10, 1000)
            command = runner._command(Path("/native/probe"), episode, 1000, 2)
            self.assertIn("--hf-gui-clean-diagnostic", command)
            receipt = fake_completed_receipt(command)
            runner._validate_completed_receipt(receipt, episode)
            clean = receipt["hf_gui_varying_diagnostic"]["clean_comparison"]
            self.assertEqual(clean["resources"]["extra_chunk_bytes"], 17 * 17)
            self.assertEqual(clean["coarse_searches"], 2 if control == "wrong-key" else 1)
            self.assertEqual(receipt["outcomes"]["actual_scored_slots"], 0)
            self.assertFalse(receipt["outcomes"]["correct_complete"])
            disabled = dict(episode, hf_gui_clean_diagnostic=False)
            old_command = runner._command(Path("/native/probe"), disabled, 1000, 2)
            self.assertNotIn("--hf-gui-clean-diagnostic", old_command)
            old = fake_completed_receipt(old_command)
            runner._validate_completed_receipt(old, disabled)
            self.assertNotIn("clean_comparison", old["hf_gui_varying_diagnostic"])
            old["hf_gui_varying_diagnostic"]["clean_comparison"] = clean
            with self.assertRaisesRegex(runner.CampaignError, "unrequested clean"):
                runner._validate_completed_receipt(old, disabled)
            del receipt["plan"]["hf_gui_clean_diagnostic_requested"]
            with self.assertRaisesRegex(runner.CampaignError, "clean GUI diagnostic request"):
                runner._validate_completed_receipt(receipt, episode)
        base = gui_varying_episode(hf_gui_clean_diagnostic=True)
        for override in ({"mode": "receive"}, {"mode": "plan"}, {"mode": "hf-search"}, {"scenario": "awgn"},
                         {"helper": True}, {"backend": "cuda"}, {"control": "wrong-ordinal"},
                         {"hf_gui_clean_diagnostic": 1}):
            with self.subTest(override=override), self.assertRaises(runner.CampaignError):
                runner._validate_episode(dict(base, **override), 0, set(), 10, 1000)

    def test_gui_clean_comparison_validates_capture_scope_and_resource_quotes(self) -> None:
        episode = gui_varying_episode(control="wrong-key", hf_gui_clean_diagnostic=True)
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 1000, 2))
        clean = receipt["hf_gui_varying_diagnostic"]["clean_comparison"]
        self.assertNotEqual(clean["keys"][1]["coarse"]["varying_coordinates"],
                            receipt["hf_gui_varying_diagnostic"]["coarse"]["varying_coordinates"])
        runner._validate_completed_receipt(receipt, episode)  # Per-key noisy banks need not match each other.
        changes = ((("schema_version",), 2), (("used_for_receiver_decisions",), True),
            (("transmitter_key_evaluator_only",), False), (("public_acquisition_input",), "clean_observation"),
            (("within_key_same_coordinates_for_clean_noisy",), False), (("coarse_searches",), 1),
            (("clean_input_sha256",), "bad"), (("clean_observation_sha256",), "bad"),
            (("clean_hashed_samples",), 999), (("clean_observation_hashed_samples",), 79),
            (("clean_channel_noise_enabled",), True), (("noisy_clean_validity_equal",), False),
            (("quiet_control_clean_is_zero",), True), (("wanted_bit",), None),
            (("clean_energy_sample_sum",), math.nan), (("difference_energy_sample_sum",), -1),
            (("resources", "fine_pair_count"), 2), (("resources", "key_render_row_bounds"), [1024]),
            (("resources", "varying_render_row_bound"), 2048), (("resources", "varying_render_rows"), 160),
            (("resources", "selected_capture_bytes"), 80), (("resources", "extra_chunk_bytes"), 4096),
            (("resources", "clean_source_working_bytes"), 66 * 2**20),
            (("resources", "native_source_channel_bound_bytes"), 64 * 2**20),
            (("resources", "coarse_workspace_bound_bytes"), 257),
            (("keys", 0, "role"), "transmitter"), (("keys", 0, "noisy"), {}),
            (("keys", 1, "coarse", "frequency_points"), 3),
            (("keys", 1, "coarse", "workspaces"), True),
            (("keys", 1, "coarse", "varying_coordinates", 0, "arrival_seconds"), math.inf),
            (("keys", 1, "clean", "varying", "frequency_states"), 0),
            (("keys", 1, "clean", "varying", "public_frequency_cells"), 1),
            (("keys", 1, "clean", "affine", "candidates"), 2),
            (("keys", 1, "clean", "affine", "prior_corrected_bit_log_evidence"), [1., 3.]))
        for path, value in changes:
            bad = json.loads(json.dumps(receipt))
            target = bad["hf_gui_varying_diagnostic"]["clean_comparison"]
            for key in path[:-1]:
                target = target[key]
            target[path[-1]] = value
            with self.subTest(path=path), self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(bad, episode)
        receipt["plan"]["native_source_channel_preflight_bound_bytes"] //= 2
        with self.assertRaisesRegex(runner.CampaignError, "reservations"):
            runner._validate_completed_receipt(receipt, episode)

    def test_gui_clean_comparison_zero_controls_and_evaluator_only_score_variation(self) -> None:
        episode = gui_varying_episode(control="noise", hf_gui_clean_diagnostic=True)
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 1000, 2))
        clean = receipt["hf_gui_varying_diagnostic"]["clean_comparison"]
        runner._validate_completed_receipt(receipt, episode)
        for field, value in (("clean_energy_sample_sum", .1), ("quiet_control_clean_is_zero", False), ("wanted_bit", 0)):
            bad = json.loads(json.dumps(receipt))
            bad["hf_gui_varying_diagnostic"]["clean_comparison"][field] = value
            with self.subTest(field=field), self.assertRaisesRegex(runner.CampaignError, "zero-signal"):
                runner._validate_completed_receipt(bad, episode)
        episode = gui_varying_episode(hf_gui_clean_diagnostic=True)
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 1000, 2))
        clean = receipt["hf_gui_varying_diagnostic"]["clean_comparison"]
        clean["clean_energy_sample_sum"] = 0.  # A complete lost signal is not a validator failure.
        clean["keys"][0]["clean"]["affine"]["basis_rows"] = 79
        runner._validate_completed_receipt(receipt, episode)
        self.assertFalse(receipt["outcomes"]["receiver_executed"])
        self.assertEqual(runner._negative_control_summary([{"status": "completed", "episode": episode, "receipt": receipt}])["completed_trials"], 0)

    def test_gui_varying_wrong_key_preserves_transmission_and_component_scope(self) -> None:
        episode = gui_varying_episode(control="wrong-key")
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 1000, 2))
        runner._validate_completed_receipt(receipt, episode)
        self.assertTrue(receipt["plan"]["public_slots"][0]["evaluator_transmitting"])
        self.assertFalse(receipt["outcomes"]["receiver_executed"])
        self.assertFalse(receipt["outcomes"]["correct_complete"])
        receipt["plan"]["public_slots"][0]["evaluator_transmitting"] = False
        with self.assertRaisesRegex(runner.CampaignError, "public slot and control"):
            runner._validate_completed_receipt(receipt, episode)
        receipt["plan"]["public_slots"][0]["evaluator_transmitting"] = True
        receipt["control"] = "wanted"
        with self.assertRaisesRegex(runner.CampaignError, "control does not match"):
            runner._validate_completed_receipt(receipt, episode)

    def test_gui_varying_receipts_bind_capture_priors_work_and_component_scope(self) -> None:
        episode = gui_varying_episode()
        command = runner._command(Path("/native/probe"), episode, 1000, 2)
        receipt = fake_completed_receipt(command)
        runner._validate_completed_receipt(receipt, episode)
        changes = (
            (("plan", "observation_scope"), "physical_message_completion"),
            (("plan", "planned_uncontaminated_quiet_slots"), 2), (("plan", "wire_bits"), 2),
            (("plan", "public_slots", 0, "observation", "first"), 21),
            (("plan", "public_slots", 0, "evaluator_transmitting"), False),
            (("channel", "samples"), 100), (("channel", "hashed_samples"), 999),
            (("channel", "input_sha256"), "missing"), (("backend", "executed_gpu_work"), True),
            (("outcomes", "receiver_executed"), True), (("outcomes", "component_only"), False),
            (("outcomes", "completion_evaluated"), True), (("outcomes", "correct_complete"), True),
            (("outcomes", "correct_decoded_complete"), True), (("outcomes", "actual_scored_slots"), 1),
            (("outcomes", "physical_completions"), 1), (("outcomes", "accepted_messages"), 1),
            (("outcomes", "false_data"), 1), (("slots",), [{"index": 2}]), (("messages",), [{}]),
        )
        diagnostic_changes = (
            (("component_complete",), False), (("public_slot",), 1), (("all_valid",), False),
            (("used_for_receiver_decisions",), True), (("observation_first_sample",), 19),
            (("observation_stop_sample",), 99), (("observation_samples",), True),
            (("observation_sha256",), "d" * 64), (("prior", "branch_weight"), .5),
            (("prior", "drift_reserved"), 1), (("prior", "drift_scored"), True),
            (("prior", "line_reserved"), 1), (("prior", "ordinary_weight"), 1),
            (("prior", "line_scored"), True), (("prior", "coherent_gain_available"), True),
            (("prior", "coherent_gain_status"), ""), (("prior", "noise_variance"), 6),
            (("prior", "frequency_lower_hz"), -1), (("prior", "frequency_upper_hz"), 1),
            (("prior", "atmospheric_allowance_hz"), 1), (("prior", "clock_bound_ppm"), 1),
            (("prior", "arrival_lower_seconds"), 0), (("prior", "arrival_upper_seconds"), 0),
            (("prior", "operational_threshold"), 0), (("prior", "minimum_bit_log_separation"), -1),
            (("coarse", "completed_tiles"), 1), (("coarse", "total_tiles"), 4097),
            (("coarse", "workspaces"), 2**20), (("coarse", "fft_precision"), ""),
            (("coarse", "proposals", 0, "bit"), True), (("coarse", "proposals", 0, "rank"), -1),
            (("coarse", "proposals", 0, "coordinate", "clock_ppm"), math.nan),
            (("coarse", "affine_coordinates"), []), (("coarse", "varying_coordinates"), []),
            (("resources", "workers"), 0), (("resources", "scratch_bound_bytes"), 512 * 2**20 + 1),
            (("resources", "render_row_bound"), 32 * 2**20 + 1), (("resources", "metadata_bound_bytes"), -1),
            (("affine", "selected_log_prior_mass"), 0), (("affine", "public_grid_log_count"), math.nan),
            (("affine", "raw_bit_log_evidence"), [1.]), (("affine", "prior_corrected_bit_log_evidence"), [1., 3.]),
            (("affine", "log_presence"), 0), (("varying", "seconds"), -1),
            (("varying", "basis_rows"), 1025), (("varying", "workspace_bytes"), 257),
            (("varying", "frequency_states"), -1), (("varying", "blocks"), True),
            (("varying", "block_rows"), 0), (("varying", "status"), ""),
            (("varying", "diagnostic_admits_data"), 1), (("varying", "diagnostic_data_bit"), 0),
            (("combined", "prior_corrected_bit_log_evidence"), [1., 3.]), (("combined", "log_presence"), 0),
        )
        for path, value in changes + tuple((("hf_gui_varying_diagnostic",) + path, value)
                                           for path, value in diagnostic_changes):
            with self.subTest(path=path):
                bad = json.loads(json.dumps(receipt))
                parent = bad
                for key in path[:-1]:
                    parent = parent[key]
                parent[path[-1]] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(bad, episode)
        oversized = json.loads(json.dumps(receipt))
        oversized["plan"].update(horizon_samples=40000, capture_digest_stop_sample=33000)
        oversized["channel"].update(samples=40000, hashed_samples=40000, selected_observation_hashed_samples=32980)
        oversized["plan"]["public_slots"][0]["observation"]["stop"] = 33000
        oversized["hf_gui_varying_diagnostic"].update(observation_stop_sample=33000, observation_samples=32980)
        with self.assertRaisesRegex(runner.CampaignError, "bounded complete"):
            runner._validate_completed_receipt(oversized, episode)
        unsolicited = fake_completed_receipt(runner._command(Path("/native/probe"),
            dict(episode, mode="receive"), 1000, 2))
        unsolicited["hf_gui_varying_diagnostic"] = receipt["hf_gui_varying_diagnostic"]
        with self.assertRaisesRegex(runner.CampaignError, "unrequested GUI"):
            runner._validate_completed_receipt(unsolicited, dict(episode, mode="receive"))

    def test_gui_varying_preserves_reserved_outer_line_mass(self) -> None:
        episode = gui_varying_episode()
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 1000, 2))
        for drift_reserved in (False, True):
            ordinary = fake_gui_varying_diagnostic(2, receipt["plan"]["public_bounds"],
                drift_reserved=drift_reserved, line_reserved=False)
            reserved = fake_gui_varying_diagnostic(2, receipt["plan"]["public_bounds"],
                drift_reserved=drift_reserved, line_reserved=True)
            for diagnostic in (ordinary, reserved):
                receipt["hf_gui_varying_diagnostic"] = diagnostic
                runner._validate_completed_receipt(receipt, episode)
            for stage in ("affine", "varying"):
                self.assertEqual(ordinary[stage]["raw_bit_log_evidence"], reserved[stage]["raw_bit_log_evidence"])
            for stage in ("affine", "varying", "combined"):
                for before, after in zip(ordinary[stage]["prior_corrected_bit_log_evidence"],
                                          reserved[stage]["prior_corrected_bit_log_evidence"]):
                    self.assertAlmostEqual(before - after, math.log(2))
                self.assertAlmostEqual(ordinary[stage]["log_presence"] - reserved[stage]["log_presence"], math.log(2))
            # Reproduce the original defect coherently: remove only the outer
            # half-weight everywhere, leaving raw evidence and all inner priors
            # intact. Cross-stage logaddexp identities alone would still pass.
            inflated = json.loads(json.dumps(reserved))
            for stage in ("affine", "varying", "combined"):
                inflated[stage]["prior_corrected_bit_log_evidence"] = [
                    value + math.log(2) for value in inflated[stage]["prior_corrected_bit_log_evidence"]]
                inflated[stage]["log_presence"] += math.log(2)
            receipt["hf_gui_varying_diagnostic"] = inflated
            with self.assertRaisesRegex(runner.CampaignError, "outer-line prior"):
                runner._validate_completed_receipt(receipt, episode)
        for field in ("line_reserved", "ordinary_weight"):
            receipt["hf_gui_varying_diagnostic"] = json.loads(json.dumps(reserved))
            del receipt["hf_gui_varying_diagnostic"]["prior"][field]
            with self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(receipt, episode)

    def test_gui_varying_noise_admission_remains_a_component_observation(self) -> None:
        episode = gui_varying_episode(control="noise")
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 1000, 2))
        for drift_reserved in (False, True):
            diagnostic = fake_gui_varying_diagnostic(2, receipt["plan"]["public_bounds"], drift_reserved=drift_reserved)
            receipt["hf_gui_varying_diagnostic"] = diagnostic
            diagnostic["varying"]["frequency_states"] = 0  # No tracking states when the native plan disables tracking.
            for bit in (0, 1):
                diagnostic["varying"].update(diagnostic_admits_data=True, diagnostic_data_bit=bit)
                runner._validate_completed_receipt(receipt, episode)
                self.assertFalse(receipt["outcomes"]["correct_complete"])
                self.assertEqual(receipt["outcomes"]["actual_scored_slots"], 0)
            for invalid_bit in (None, True, 2):
                diagnostic["varying"]["diagnostic_data_bit"] = invalid_bit
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(receipt, episode)

    def test_gui_receipts_bind_actual_errors_without_narrowing_public_priors(self) -> None:
        episode = {"id": "gui", "scenario": "hf-gui-native", "mode": "receive", "waveform": "reference", "seed": 1,
                   "gui_clock_edge": True, "gui_arrival": "default", "gui_target_cn0_db_hz": 20,
                   "cn0_db_hz": 12, "bit_seconds": 16}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 1000, 2))
        runner._validate_completed_receipt(receipt, episode)
        self.assertEqual(receipt["plan"]["capture_preroll_seconds"], 7.25)
        self.assertEqual(receipt["plan"]["gui_profile"]["endpoint_clock_bound_ppm"], 150)
        self.assertEqual(receipt["plan"]["gui_profile"]["realized"]["tx_audio_ppm"], 50)
        for field, value in (("margin", 1), ("selective_slow_depth", 0), ("selective_fast_depth", 0),
                             ("endpoint_clock_bound_ppm", 50), ("spread_doppler_hz", 30),
                             ("realization_passed_to_receiver", True), ("fixture_bit_seconds", None),
                             ("actual_cn0_db_hz", 20), ("capture_preroll_seconds", .25)):
            bad = json.loads(json.dumps(receipt)); bad["plan"]["gui_profile"][field] = value
            with self.subTest(field=field), self.assertRaisesRegex(runner.CampaignError, "native GUI"):
                runner._validate_completed_receipt(bad, episode)
        for field, value in (("tx_audio_ppm", 150), ("rx_audio_ppm", 50), ("tx_rf_ppm", True),
                             ("relative_audio_ppm", 100), ("timing_offset_seconds", 8),
                             ("tx_phase_noise_rad_sqrt_s", 0)):
            bad = json.loads(json.dumps(receipt)); bad["plan"]["gui_profile"]["realized"][field] = value
            with self.subTest(field=field), self.assertRaisesRegex(runner.CampaignError, "native GUI"):
                runner._validate_completed_receipt(bad, episode)
        bad = json.loads(json.dumps(receipt)); bad["plan"]["channel_renderer"] = "appraisal_channel"
        with self.assertRaisesRegex(runner.CampaignError, "canonical preparation"):
            runner._validate_completed_receipt(bad, episode)

    def test_gui_off_and_target_planned_receipts_do_not_invent_fixture_duration(self) -> None:
        episode = {"id": "off", "scenario": "hf-gui-native", "mode": "plan", "waveform": "reference", "seed": 1,
                   "gui_audio_ppm": 0, "gui_rf_ppm": 0, "gui_audio_phase_degrees_sqrt_s": 0,
                   "gui_rf_phase_degrees_sqrt_s": 0, "gui_selective": "off", "gui_spread": "none"}
        runner._validate_episode(episode, 0, set(), 10, 1000)
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 1000, 2))
        runner._validate_completed_receipt(receipt, episode)
        self.assertIsNone(receipt["plan"]["gui_profile"]["fixture_bit_seconds"])
        self.assertEqual(receipt["plan"]["gui_profile"]["realized"]["tx_phase_noise_rad_sqrt_s"], 0)
        for spread in runner.GUI_SPREAD_DOPPLER:
            for selective in ("slow-25", "fast-10", "each-1"):
                changed = dict(episode, gui_spread=spread, gui_selective=selective)
                runner._validate_episode(changed, 0, set(), 10, 1000)
                native = fake_completed_receipt(runner._command(Path("/native/probe"), changed, 1000, 2))
                runner._validate_completed_receipt(native, changed)

    def test_new_physical_and_gui_axes_are_not_pooled_in_confirmatory_strata(self) -> None:
        for setting, first, second in (("gui_audio_ppm", 50, 100), ("gui_selective", "off", "each-65"),
                                       ("hf_primary_period_seconds", 200, 300)):
            campaign = valid_campaign()
            for row in campaign["episodes"]:
                if setting.startswith("hf_"):
                    row.update({k: v for k, v in physical_design_episode().items() if k not in {"id", "seed", "mode"}})
                else:
                    row.update(scenario="hf-gui-native", waveform="reference")
            campaign["episodes"][0][setting] = first
            campaign["episodes"][1][setting] = second
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "campaign.json"; path.write_text(json.dumps(campaign))
                with self.assertRaisesRegex(runner.CampaignError, "mixes episode settings.*" + setting):
                    runner._read_campaign(path)

    def test_trajectory_and_noise_controls_are_separate_and_receipt_bound(self) -> None:
        episode = {"id": "independent", "scenario": "hf-ensemble-knots", "mode": "receive",
                   "waveform": "reference", "seed": 3, "hf_trajectory_seed": 2**64 - 1,
                   "hf_trajectory_offset_seconds": -137.25, "noise_seed": 0}
        runner._validate_episode(episode, 0, set(), 10, 1000)
        command = runner._command(Path("/native/probe"), episode, 1000, 2)
        receipt = fake_completed_receipt(command)
        runner._validate_completed_receipt(receipt, episode)
        for field in ("hf_trajectory_seed", "hf_trajectory_offset_seconds", "noise_seed"):
            self.assertEqual(command[command.index("--" + field.replace("_", "-")) + 1], str(episode[field]))
            for wrong in (None, True, episode[field] + 1):
                bad = json.loads(json.dumps(receipt))
                bad["plan"][field] = wrong
                with self.assertRaisesRegex(runner.CampaignError, field):
                    runner._validate_completed_receipt(bad, episode)
            bad = json.loads(json.dumps(receipt))
            bad["plan"][field.replace("_seconds", "") + "_override"] = False
            with self.assertRaisesRegex(runner.CampaignError, field):
                runner._validate_completed_receipt(bad, episode)
        for field, bad_values in (("hf_trajectory_seed", (-1, 2**64, True, 1.5)),
                                  ("noise_seed", (-1, 2**64, True, "3")),
                                  ("hf_trajectory_offset_seconds", (-86401, 86401, True, float("nan")))):
            for bad_value in bad_values:
                bad = dict(episode, **{field: bad_value})
                with self.assertRaises(runner.CampaignError):
                    runner._validate_episode(bad, 0, set(), 10, 1000)

    def test_known_binary_reference_keeps_distance_law_and_private_scope(self) -> None:
        episode = {"id": "binary", "scenario": "hf-quiet", "mode": "hf-search",
                   "waveform": "reference", "seed": 3, "hf_physical_diagnostic": True}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), episode, 1000, 2))
        runner._validate_completed_receipt(receipt, episode)
        for field, value in (("truth_information_passed_to_search", True),
                             ("includes_acquisition_presence_or_completion", True),
                             ("fixed_known_context", False), ("squared_distance", -1),
                             ("distance_over_noise_variance", -1),
                             ("equal_prior_bit_error_probability", .5), ("wanted_bit", 0)):
            bad = json.loads(json.dumps(receipt))
            bad["hf_physical_diagnostic"]["known_binary"][field] = value
            with self.assertRaisesRegex(runner.CampaignError, "known binary"):
                runner._validate_completed_receipt(bad, episode)

    def test_fixed_sample_rejects_reused_effective_noise(self) -> None:
        campaign = valid_campaign()
        for episode in campaign["episodes"]:
            episode["noise_seed"] = 99
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            path.write_text(json.dumps(campaign))
            with self.assertRaisesRegex(runner.CampaignError, "distinct seeds"):
                runner._read_campaign(path)
            for index, episode in enumerate(campaign["episodes"]):
                episode["noise_seed"] = 99 + index
            path.write_text(json.dumps(campaign))
            runner._read_campaign(path)

    def test_private_hf_diagnostic_options_are_strict_and_recorded(self) -> None:
        campaign = valid_campaign()
        campaign["confirmatory_strata"] = {}
        episode = {"id": "hf-diagnostic", "scenario": "hf-quiet", "mode": "oracle", "waveform": "reference",
                   "seed": 3, "history_reference_delay_seconds": .002, "hf_phase_bank": True}
        campaign["episodes"] = [episode]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            path.write_text(json.dumps(campaign))
            runner._read_campaign(path)
            for overrides in ({"history_reference_delay_seconds": -1}, {"history_reference_delay_seconds": True},
                              {"history_reference_delay_seconds": "0.002"}, {"history_reference_delay_seconds": float("inf")},
                              {"history_reference_delay_seconds": float("nan")}, {"hf_phase_bank": 1},
                              {"hf_phase_bank": "yes"}, {"mode": "receive"}, {"mode": "plan"}):
                with self.subTest(overrides=overrides):
                    bad = json.loads(json.dumps(campaign))
                    bad["episodes"][0].update(overrides)
                    path.write_text(json.dumps(bad))
                    with self.assertRaises(runner.CampaignError):
                        runner._read_campaign(path)
        command = runner._command(Path("/native/probe"), episode, 1000, 2)
        self.assertEqual(command[command.index("--history-reference-delay-seconds") + 1], "0.002")
        self.assertIn("--hf-phase-bank", command)
        receipt = fake_completed_receipt(command)
        runner._validate_completed_receipt(receipt, episode)
        for section, field, value in (("plan", "history_reference_delay_seconds", 0),
                                      ("hf_history", "reference_delay_seconds", 0),
                                      ("plan", "hf_phase_bank_requested", False),
                                      ("hf_phase_bank", "samples", 0),
                                      ("hf_phase_bank", "reference_delay_seconds", 0),
                                      ("hf_phase_bank", "truth_information_passed_to_phase_bank", True),
                                      ("hf_phase_bank", "receiver_executed", True)):
            with self.subTest(section=section, field=field):
                bad = json.loads(json.dumps(receipt))
                bad[section][field] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(bad, episode)
        disabled = dict(episode, hf_phase_bank=False)
        command = runner._command(Path("/native/probe"), disabled, 1000, 2)
        self.assertNotIn("--hf-phase-bank", command)
        disabled_receipt = fake_completed_receipt(command)
        runner._validate_completed_receipt(disabled_receipt, disabled)
        disabled_receipt["hf_phase_bank"] = receipt["hf_phase_bank"]
        with self.assertRaisesRegex(runner.CampaignError, "unrequested phase-bank"):
            runner._validate_completed_receipt(disabled_receipt, disabled)
        undeclared = {key: value for key, value in episode.items() if key not in {"history_reference_delay_seconds", "hf_phase_bank"}}
        command = runner._command(Path("/native/probe"), undeclared, 1000, 2)
        undeclared_receipt = fake_completed_receipt(command)
        undeclared_receipt["plan"]["history_reference_delay_seconds"] = .002
        with self.assertRaisesRegex(runner.CampaignError, "reference delay"):
            runner._validate_completed_receipt(undeclared_receipt, undeclared)

    def test_fec_options_and_native_identity_are_strict(self) -> None:
        campaign = valid_campaign()
        campaign["confirmatory_strata"] = {}
        campaign["episodes"] = [dict(campaign["episodes"][0], fec="rs20")]
        campaign["episodes"][0].pop("stratum")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            for fec in ("off", "rs20", "rs60"):
                campaign["episodes"][0]["fec"] = fec
                path.write_text(json.dumps(campaign))
                runner._read_campaign(path)
                command = runner._command(Path("/native/probe"), campaign["episodes"][0], 1000, 1)
                self.assertEqual(command[command.index("--fec") + 1], fec)
                runner._validate_completed_receipt(fake_completed_receipt(command), campaign["episodes"][0])
            for fec in ("RS20", "rs40", True, 20, None):
                campaign["episodes"][0]["fec"] = fec
                path.write_text(json.dumps(campaign))
                with self.assertRaises(runner.CampaignError):
                    runner._read_campaign(path)
        episode = dict(valid_campaign()["episodes"][0], fec="rs60")
        command = runner._command(Path("/native/probe"), episode, 1000, 1)
        for field, value in (("fec", "off"), ("wire_bits", 408), ("wire_bits", 79),
                             ("short_wire_bit_limit", 208), ("content_limit_bytes", 17), ("long_form", True)):
            with self.subTest(field=field, value=value):
                receipt = fake_completed_receipt(command)
                receipt["plan"][field] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(receipt, episode)

    def test_fec_decoded_success_requires_exact_frame_payload_and_repair_receipts(self) -> None:
        episode = dict(valid_campaign()["episodes"][0], fec="rs20")
        command = runner._command(Path("/native/probe"), episode, 1000, 1)
        receipt = fake_fec_repaired_receipt(command)
        runner._validate_completed_receipt(receipt, episode)
        self.assertFalse(receipt["outcomes"]["correct_complete"])
        self.assertTrue(receipt["outcomes"]["correct_decoded_complete"])
        for section, field, value in (
            ("outcomes", "accepted_messages", 2), ("outcomes", "accepted_messages", True),
            ("outcomes", "physical_completions", 2), ("outcomes", "premature_completions", 1),
            ("outcomes", "actual_scored_slots", 1),
            ("outcomes", "false_data", 1), ("outcomes", "wrong_bits", 0),
            ("outcomes", "ambiguous_active_slots", 0), ("outcomes", "correct_complete", True),
            ("outcomes", "communication_endpoint", "correct_complete"),
            ("message", "first_public_slot", 1), ("message", "stop_public_slot", 65),
            ("message", "slots_consumed", 65), ("message", "observed_bits", 64),
            ("message", "trailing_quiet_slots", [63, 64]), ("message", "long_form", True),
            ("message", "message_ready_for_decode", False), ("message", "consumed_bytes", 7),
            ("message", "erasure_slot_indices", [64]), ("message", "erasure_slot_indices", []),
            ("message", "erasure_slot_indices", [21]),
            ("message", "decoded_payload_bytes", [32]), ("message", "decoded_endpoint_matches", False),
            ("message", "fec", "rs60"), ("message", "fec_stats", None),
            ("message", "corrected_bytes", -1), ("message", "corrected_bytes", True),
            ("message", "corrected_bytes", 0), ("message", "corrected_bytes", 2),
            ("message", "authenticated", True), ("message", "authenticated", None)):
            with self.subTest(section=section, field=field):
                bad = json.loads(json.dumps(receipt))
                target = bad["messages"][0] if section == "message" else bad[section]
                target[field] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(bad, episode)
        bad = json.loads(json.dumps(receipt))
        bad["messages"][0]["fec_stats"]["data"]["repaired_bytes"] = 3
        with self.assertRaisesRegex(runner.CampaignError, "repair statistics"):
            runner._validate_completed_receipt(bad, episode)
        bad = json.loads(json.dumps(receipt))
        bad["messages"][0]["fec_stats"]["data"]["received_bits"] += 8
        bad["messages"][0]["fec_stats"]["integrity"]["received_bits"] -= 8
        with self.assertRaisesRegex(runner.CampaignError, "region extent"):
            runner._validate_completed_receipt(bad, episode)
        # An erased zero placeholder can already equal its decoded value:
        # it is repaired coverage without a changed-byte count.
        zero_erasure = json.loads(json.dumps(receipt))
        zero_erasure["outcomes"]["wrong_bits"] = 0
        zero_erasure["messages"][0]["corrected_bytes"] = 0
        zero_erasure["messages"][0]["fec_stats"]["data"]["corrected_bits"] = 0
        zero_erasure["messages"][0]["fec_stats"]["data"]["corrected_bytes"] = 0
        runner._validate_completed_receipt(zero_erasure, episode)
        for control in ("noise", "wrong-key", "wrong-ordinal", "eof"):
            negative = dict(episode, control=control)
            command = runner._command(Path("/native/probe"), negative, 1000, 1)
            with self.assertRaisesRegex(runner.CampaignError, "target or physical"):
                runner._validate_completed_receipt(fake_fec_repaired_receipt(command), negative)

    def test_fec_statistics_select_decoded_endpoint_without_relabeling_raw_success(self) -> None:
        campaign = valid_campaign()
        trials = []
        for episode in campaign["episodes"]:
            episode["fec"] = "rs20"
            command = runner._command(Path("/native/probe"), episode, 1000, 1)
            receipt = fake_fec_repaired_receipt(command)
            trials.append({"episode": episode, "status": "completed", "receipt": receipt})
        stats = runner._statistical_summary(campaign, trials)["awgn-fixed"]
        self.assertEqual(stats["successes"], 2)
        self.assertEqual(stats["communication_endpoint"], "correct_decoded_complete")
        for episode in campaign["episodes"]:
            episode.pop("fec")
        stats = runner._statistical_summary(campaign, trials)["awgn-fixed"]
        self.assertEqual(stats["successes"], 0)
        self.assertEqual(stats["communication_endpoint"], "correct_complete")

    def test_completed_receive_receipt_requires_identity_and_complete_outcome(self) -> None:
        episode = valid_campaign()["episodes"][0]
        command = runner._command(Path("/native/probe"), episode, 1000, 1)
        receipt = fake_completed_receipt(command)
        receipt.pop("mode")
        with self.assertRaisesRegex(runner.CampaignError, "identity fields: mode"):
            runner._validate_completed_receipt(receipt, episode)

        receipt = fake_completed_receipt(command)
        receipt["outcomes"].pop("correct_complete")
        with self.assertRaisesRegex(runner.CampaignError, "outcomes.correct_complete"):
            runner._validate_completed_receipt(receipt, episode)

    def test_completed_receipt_settings_must_match_episode(self) -> None:
        episode = valid_campaign()["episodes"][0]
        command = runner._command(Path("/native/probe"), episode, 1000, 1)
        receipt = fake_completed_receipt(command)
        receipt["plan"]["cn0_db_hz"] = 8
        with self.assertRaisesRegex(runner.CampaignError, "C/N0"):
            runner._validate_completed_receipt(receipt, episode)
        receipt = fake_completed_receipt(command)
        receipt["plan"]["pulse"] = "tapered-rrc"
        with self.assertRaisesRegex(runner.CampaignError, "pulse"):
            runner._validate_completed_receipt(receipt, episode)
        receipt = fake_completed_receipt(command)
        receipt["plan"]["requested_message"] = "wrong"
        with self.assertRaisesRegex(runner.CampaignError, "requested message"):
            runner._validate_completed_receipt(receipt, episode)

    def test_receive_receipt_requires_real_execution_and_coherent_success(self) -> None:
        episode = valid_campaign()["episodes"][0]
        command = runner._command(Path("/native/probe"), episode, 1000, 1)
        for field, value in (("receiver_executed", False), ("component_only", True),
                             ("completion_evaluated", False), ("actual_scored_slots", 0)):
            with self.subTest(field=field):
                receipt = fake_completed_receipt(command)
                receipt["outcomes"][field] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(receipt, episode)

        for field, value in (("physical_completions", 0), ("wrong_bits", 1),
                             ("false_data", 1), ("ambiguous_active_slots", 1),
                             ("quiet_active_slots", 1), ("missed_completion", True)):
            with self.subTest(success_mutant=field):
                receipt = fake_completed_receipt(command)
                receipt["outcomes"].update({"correct_complete": True,
                                            "physical_completions": 1,
                                            "missed_completion": False})
                receipt["outcomes"][field] = value
                with self.assertRaisesRegex(runner.CampaignError, "conflicts"):
                    runner._validate_completed_receipt(receipt, episode)

    def test_requested_and_effective_message_are_checked(self) -> None:
        episode = {"id": "transition", "scenario": "hf-transition", "mode": "receive",
                   "waveform": "reference", "seed": 9204, "message": "A"}
        command = runner._command(Path("/native/probe"), episode, 1000, 1)
        receipt = fake_completed_receipt(command)
        runner._validate_completed_receipt(receipt, episode)
        receipt["plan"]["requested_message"] = "e"
        with self.assertRaisesRegex(runner.CampaignError, "requested message"):
            runner._validate_completed_receipt(receipt, episode)

        fade_episode = {"id": "fade", "scenario": "awgn", "mode": "receive",
                        "waveform": "reference", "seed": 9205, "control": "interior-fade"}
        fade_command = runner._command(Path("/native/probe"), fade_episode, 1000, 1)
        fade_receipt = fake_completed_receipt(fade_command)
        runner._validate_completed_receipt(fade_receipt, fade_episode)
        fade_receipt["plan"]["message"] = "e"
        with self.assertRaisesRegex(runner.CampaignError, "effective message"):
            runner._validate_completed_receipt(fade_receipt, fade_episode)

    def test_channel_requested_duration_matches_reported_geometry(self) -> None:
        episode = {"id": "channel-duration", "scenario": "eme-disk-episode", "mode": "channel",
                   "waveform": "smoke", "seed": 4, "duration_seconds": .25}
        command = runner._command(Path("/native/probe"), episode, 1000, 1)
        receipt = fake_completed_receipt(command)
        receipt["channel"] = {"contains_signal_energy": True, "samples": 480}
        runner._validate_completed_receipt(receipt, episode)
        receipt["plan"]["represented_seconds"] = .5
        with self.assertRaisesRegex(runner.CampaignError, "duration"):
            runner._validate_completed_receipt(receipt, episode)

    def test_slot_receipt_validates_single_observation_without_message_success(self) -> None:
        episode = {"id": "slot", "scenario": "awgn", "mode": "slot", "waveform": "reference",
                   "seed": 3, "control": "wanted"}
        command = runner._command(Path("/native/probe"), episode, 1000, 2)
        receipt = fake_completed_receipt(command)
        runner._validate_completed_receipt(receipt, episode)
        self.assertFalse(receipt["outcomes"]["correct_complete"])

        for decision in ("quiet", "ambiguous"):
            with self.subTest(decision=decision):
                unresolved = fake_completed_receipt(command)
                unresolved["outcomes"]["first_public_observation_correct_data"] = False
                unresolved["slots"][0]["decision"] = decision
                runner._validate_completed_receipt(unresolved, episode)

        invalid_edits = (
            ("receiver_executed", False), ("component_only", False), ("actual_scored_slots", 2),
            ("first_public_observation_complete", False), ("first_public_observation_correct_data", None),
            ("correct_complete", True), ("completion_evaluated", True),
        )
        for field, value in invalid_edits:
            with self.subTest(field=field):
                bad = fake_completed_receipt(command)
                bad["outcomes"][field] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(bad, episode)
        bad_score = fake_completed_receipt(command)
        bad_score["slots"][0]["decision"] = "invalid"
        with self.assertRaisesRegex(runner.CampaignError, "invalid first slot decision"):
            runner._validate_completed_receipt(bad_score, episode)

    def test_noise_slot_receipt_preserves_false_data_without_message_success(self) -> None:
        episode = {"id": "noise-slot", "scenario": "awgn", "mode": "slot", "waveform": "reference",
                   "seed": 3, "control": "noise"}
        command = runner._command(Path("/native/probe"), episode, 1000, 2)
        for decision in ("quiet", "ambiguous", "data"):
            with self.subTest(decision=decision):
                receipt = fake_completed_receipt(command)
                receipt["slots"][0]["decision"] = decision
                receipt["outcomes"]["false_data"] = int(decision == "data")
                runner._validate_completed_receipt(receipt, episode)
                self.assertFalse(receipt["outcomes"]["first_public_observation_correct_data"])
                self.assertFalse(receipt["outcomes"]["correct_complete"])
                self.assertFalse(receipt["outcomes"]["completion_evaluated"])
        lying = fake_completed_receipt(command)
        lying["outcomes"]["first_public_observation_correct_data"] = True
        with self.assertRaisesRegex(runner.CampaignError, "noise-only slot.*correct DATA"):
            runner._validate_completed_receipt(lying, episode)
        for field in ("correct_complete", "completion_evaluated"):
            with self.subTest(field=field):
                lying = fake_completed_receipt(command)
                lying["outcomes"][field] = True
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(lying, episode)

    def test_oracle_selected_slot_must_match_requested_index(self) -> None:
        episode = {"id": "oracle-slot", "scenario": "awgn", "mode": "oracle",
                   "waveform": "reference", "seed": 4, "oracle_slot": 2}
        command = runner._command(Path("/native/probe"), episode, 1000, 2)
        receipt = fake_completed_receipt(command)
        runner._validate_completed_receipt(receipt, episode)
        receipt["oracle"]["slot_index"] = 1
        with self.assertRaisesRegex(runner.CampaignError, "selected oracle_slot"):
            runner._validate_completed_receipt(receipt, episode)

    def test_diagnose_receipt_is_component_only_and_not_complete_service(self) -> None:
        episode = {"id": "diagnose", "scenario": "vlf-phase-wander", "mode": "diagnose",
                   "waveform": "reference", "seed": 3}
        command = runner._command(Path("/native/probe"), episode, 1000, 2)
        receipt = fake_completed_receipt(command)
        runner._validate_completed_receipt(receipt, episode)
        for section, field, value in (("vlf_diagnostic", "component_complete", False),
                                      ("vlf_diagnostic", "phase", "fine_prefix"),
                                      ("vlf_diagnostic", "capacity_qualified", True),
                                      ("vlf_diagnostic", "complete_receiver_service", True),
                                      ("outcomes", "component_only", False),
                                      ("outcomes", "completion_evaluated", True),
                                      ("outcomes", "correct_complete", True)):
            with self.subTest(section=section, field=field):
                invalid = fake_completed_receipt(command)
                invalid[section][field] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(invalid, episode)

    def test_hf_search_receipts_bind_public_inputs_and_do_not_claim_reception(self) -> None:
        episode = {"id": "search", "scenario": "awgn", "mode": "hf-search", "waveform": "smoke",
                   "seed": 9, "search_slot": 1, "search_timing": "full"}
        command = runner._command(Path("/native/probe"), episode, 1000, 2)
        runner._validate_completed_receipt(fake_completed_receipt(command), episode)
        for section, field, value in (("plan", "search_slot", 0), ("plan", "search_timing", "selected"),
                ("plan", "search_observation", {"first": 20, "stop": 99}),
                ("plan", "search_observation_sha256", "missing"),
                ("hf_public_search", "slot_index", 0), ("hf_public_search", "component_complete", False),
                ("hf_public_search", "truth_information_passed_to_search", True),
                ("hf_public_search", "capacity_qualified", True), ("hf_public_search", "receiver_executed", True),
                ("outcomes", "receiver_executed", True), ("outcomes", "public_search_executed", False),
                ("outcomes", "correct_complete", True), ("outcomes", "correct_decoded_complete", True),
                ("outcomes", "completion_evaluated", True), ("outcomes", "actual_scored_slots", 1),
                ("outcomes", "physical_completions", 1), ("outcomes", "accepted_messages", 1),
                ("hf_search_evaluation", "truth_information_passed_to_search", True),
                ("hf_search_evaluation", "expected_receiver_bit", True)):
            with self.subTest(section=section, field=field):
                invalid = fake_completed_receipt(command)
                invalid[section][field] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(invalid, episode)
        for control in ("noise", "wrong-key", "wrong-ordinal"):
            negative = {**episode, "control": control}
            negative_command = runner._command(Path("/native/probe"), negative, 1000, 2)
            receipt = fake_completed_receipt(negative_command)
            runner._validate_completed_receipt(receipt, negative)
            receipt["hf_search_evaluation"]["expected_receiver_bit"] = 1
            with self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(receipt, negative)

    def test_oracle_component_completion_flag_does_not_override_oracle_result(self) -> None:
        episode = {"id": "oracle", "scenario": "awgn", "mode": "oracle",
                   "waveform": "reference", "seed": 7}
        command = runner._command(Path("/native/probe"), episode, 1000, 1)
        receipt = fake_completed_receipt(command)
        receipt["outcomes"] = {"correct_complete": False}
        runner._validate_completed_receipt(receipt, episode)

    def test_gpu_backend_validation_accepts_real_device_ordinals_and_rejects_fallback(self) -> None:
        for requested, actual in (("cuda", "cuda:0"), ("vulkan", "vulkan:0"),
                                  ("gpu", "cuda:0"), ("gpu", "vulkan:3")):
            with self.subTest(requested=requested, actual=actual):
                episode = {"id": "gpu", "scenario": "awgn", "mode": "receive",
                           "waveform": "reference", "seed": 9, "backend": requested}
                command = runner._command(Path("/native/probe"), episode, 1000, 1)
                receipt = fake_completed_receipt(command)
                receipt["backend"].update({"device": actual, "executed_gpu_work": True})
                runner._validate_completed_receipt(receipt, episode)

        for requested, actual, executed in (("cuda", "cpu", False), ("vulkan", "cpu", False),
                                             ("gpu", "gpu", True), ("gpu", "cpu", False)):
            with self.subTest(requested=requested, actual=actual, executed=executed):
                episode = {"id": "gpu", "scenario": "awgn", "mode": "receive",
                           "waveform": "reference", "seed": 9, "backend": requested}
                command = runner._command(Path("/native/probe"), episode, 1000, 1)
                receipt = fake_completed_receipt(command)
                receipt["backend"].update({"device": actual, "executed_gpu_work": executed})
                with self.assertRaisesRegex(runner.CampaignError, "executed GPU device work"):
                    runner._validate_completed_receipt(receipt, episode)

        episode = {"id": "diagnose-gpu", "scenario": "vlf-resonant", "mode": "diagnose",
                   "waveform": "reference", "seed": 9, "backend": "cuda"}
        command = runner._command(Path("/native/probe"), episode, 1000, 1)
        receipt = fake_completed_receipt(command)
        receipt["backend"].update({"device": "cuda:0", "executed_gpu_work": True})
        runner._validate_completed_receipt(receipt, episode)

    def test_command_arguments_remain_separate_and_allowlisted(self) -> None:
        injection = "awgn;touch /tmp/appraisal-runner-injection"
        episode = {"id": "safe", "scenario": injection, "mode": "receive", "waveform": "smoke",
                   "seed": 1, "control": "wanted"}
        command = runner._command(Path("/native/probe"), episode, 17, 1.5)
        self.assertEqual(command[0], "/native/probe")
        self.assertIn("--max-samples", command)
        self.assertIn("--max-wall-seconds", command)
        self.assertIn(injection, command)
        self.assertEqual(command[command.index("--scenario") + 1], injection)
        self.assertLess(float(command[command.index("--max-wall-seconds") + 1]), 2)

    def test_confidence_interval_and_fixed_n_edges(self) -> None:
        lower, upper = runner.clopper_pearson(0, 1)
        self.assertEqual(lower, 0)
        self.assertAlmostEqual(upper, .975, places=10)
        lower, upper = runner.clopper_pearson(1, 1)
        self.assertAlmostEqual(lower, .025, places=10)
        self.assertEqual(upper, 1)
        lower, upper = runner.clopper_pearson(0, 2)
        self.assertEqual(lower, 0)
        self.assertAlmostEqual(upper, 1 - .025 ** .5, places=10)
        lower, upper = runner.clopper_pearson(1, 2)
        self.assertAlmostEqual(lower, .012579117093425, places=10)
        self.assertAlmostEqual(upper, .987420882906575, places=10)
        with self.assertRaises(ValueError):
            runner.clopper_pearson(0, 101)


class CampaignExecutionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.evidence_root = RUNNER.parent / "local-evidence"
        self.evidence_root.mkdir(exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix=".test-runner-", dir=self.evidence_root)
        self.work = Path(self.tmp.name)
        self.probe = Path(sys.executable).resolve()
        self.campaign_file = self.work / "campaign.json"
        self.campaign_file.write_text(json.dumps(valid_campaign()))

    def tearDown(self) -> None:
        self.tmp.cleanup()

    @staticmethod
    def _fake_completed(command, **kwargs):
        # A completed but incorrect message is a legitimate measured outcome.
        kwargs["stdout"].write((json.dumps(fake_completed_receipt(command)) + "\n").encode())
        kwargs["stderr"].write(b"")
        return subprocess.CompletedProcess(command, 0)

    def test_predictive_regression_gate_stops_only_opted_in_valid_pairs(self) -> None:
        campaign = valid_campaign()
        campaign.pop("confirmatory_strata")
        campaign["max_wall_seconds"] = 10
        for episode in campaign["episodes"]:
            episode.pop("stratum")
            episode.update(waveform="reference", hf_predictive_quiet=True, control="wrong-key")
        def validated_trial(probe, episode, samples, timeout, directory, index):
            # A supplied validated-trial boundary isolates the batch rule; this
            # fixture is not a second implementation of the native receiver.
            receipt = fake_completed_receipt(runner._command(probe, episode, 10000, timeout))
            candidate = receipt["hf_predictive_quiet_comparison"]["candidate"]
            candidate["outcomes"].update(accepted_messages=1, premature_completions=0)
            candidate["messages"] = [{"decoded": True, "matches_transmitted_message": True}]
            return {"episode": episode, "receipt": receipt, "status": "completed", "elapsed_seconds": .01}
        for flag, expected_count in ((None, 2), (False, 2), (True, 1)):
            if flag is None:
                campaign.pop("stop_on_predictive_regression", None)
            else:
                campaign["stop_on_predictive_regression"] = flag
            self.campaign_file.write_text(json.dumps(campaign))
            with patch.object(runner, "_trial", side_effect=validated_trial) as trial:
                run = runner.run_campaign(self.probe, self.campaign_file, self.work / f"predictive-stop-{flag}")
            self.assertEqual(trial.call_count, expected_count)
            self.assertEqual(run["executed_trials"], expected_count)
            self.assertEqual(run["execution_failures"], 0)
            self.assertEqual(run["trials"][0]["status"], "completed")
            self.assertEqual(run["trials"][0]["predictive_regressions"], ["increased_accepted_incorrect_messages"])
            self.assertEqual(run["stop_reason"], "predictive_receiver_regression" if flag else None)
            self.assertEqual(run["status"], "incomplete_checks" if flag else "completed")
        trial = validated_trial(self.probe, campaign["episodes"][0], 10000, 2, self.work, 0)
        self.assertEqual(runner._predictive_regressions(dict(trial, status="invalid_receipt")), [])
        trial["receipt"]["hf_predictive_quiet_comparison"]["status"] = "time_limit"
        self.assertEqual(runner._predictive_regressions(trial), [])

    def test_predictive_regression_gate_covers_delivery_loss_and_premature_completion(self) -> None:
        episode = {"id": "predictive", "scenario": "awgn", "mode": "receive", "waveform": "reference", "seed": 1,
                   "hf_predictive_quiet": True}
        receipt = fake_completed_receipt(runner._command(self.probe, episode, 10000, 2))
        trial = {"episode": episode, "receipt": receipt, "status": "completed"}
        self.assertEqual(runner._predictive_regressions(trial), [])
        receipt["outcomes"]["correct_complete"] = True
        candidate = receipt["hf_predictive_quiet_comparison"]["candidate"]
        candidate["outcomes"]["premature_completions"] = 1
        self.assertEqual(runner._predictive_regressions(trial),
                         ["increased_premature_completions", "lost_required_complete_delivery"])
        candidate["outcomes"]["accepted_messages"] = 1
        candidate["messages"] = [{"decoded": True, "matches_transmitted_message": False}]
        self.assertEqual(runner._predictive_regressions(trial), ["increased_accepted_incorrect_messages",
                         "increased_premature_completions", "lost_required_complete_delivery"])

    def test_predictive_quiet_candidate_failure_is_not_a_completed_trial(self) -> None:
        episode = {"id": "predictive", "scenario": "awgn", "mode": "receive", "waveform": "reference", "seed": 1,
                   "hf_predictive_quiet": True}
        def failed_candidate(command, out, err, timeout, options):
            receipt = fake_completed_receipt(command)
            comparison = receipt["hf_predictive_quiet_comparison"]
            comparison["status"] = "time_limit"
            comparison["candidate"]["status"] = "time_limit"
            comparison["candidate"]["reason"] = "bounded candidate failure fixture"
            comparison["candidate"]["service_replay"].update(work_complete=False, communication_complete=False)
            out.write(json.dumps(receipt).encode())
            return 4, None
        with patch.object(runner, "_run_service_child", side_effect=failed_candidate):
            trial = runner._trial(self.probe, episode, 10000, 2, self.work, 0)
        self.assertEqual(trial["status"], "time_limit", trial.get("error"))
        self.assertEqual(trial["receipt"]["status"], "completed")
        summary = runner._predictive_comparison_summary([trial])
        self.assertEqual(len(summary["pairs"]), 1)
        self.assertEqual(summary["pairs"][0]["baseline"]["status"], "completed")
        self.assertEqual(summary["pairs"][0]["candidate"]["status"], "time_limit")
        self.assertEqual(runner._wanted_outcome_summary([trial])["completed_trials"], 0)
        self.assertEqual(trial["child_file_limit_bytes"], runner.MAX_OUTPUT_BYTES)
        for malformed in (None, [], {"candidate": None}, {"candidate": {"outcomes": None, "service_replay": None}}):
            invalid = {"episode": episode, "status": "invalid_receipt",
                       "receipt": {"hf_predictive_quiet_comparison": malformed}}
            summary = runner._predictive_comparison_summary([invalid])
            self.assertEqual(summary["pairs"][0]["trial_status"], "invalid_receipt")
            self.assertIsNone(summary["pairs"][0]["candidate"]["correct_complete"])

    @unittest.skipUnless(os.name == "posix" and runner.resource is not None, "POSIX file-size limit discriminator")
    def test_service_spool_larger_than_output_limit_has_its_own_bounded_file_cap(self) -> None:
        episode = {"id": "spool", "scenario": "awgn", "mode": "receive", "waveform": "reference", "seed": 1,
                   "service_replay": True}
        probe = self.work / "spool_probe.py"
        command = runner._command(probe, episode, 1000000, 3.5)
        receipt = fake_completed_receipt(command)
        probe.write_text(f"#!{sys.executable}\n" +
            "import json,pathlib,signal,sys\n"
            "signal.signal(signal.SIGXFSZ,signal.SIG_DFL)\n"
            "path=pathlib.Path(__file__).with_suffix('.spool')\n"
            "with path.open('wb') as target:\n"
            "    for _ in range(9): target.write(b'x'*(1024*1024))\n"
            "size=path.stat().st_size\npath.unlink()\n" +
            f"receipt=json.loads({json.dumps(receipt)!r})\n" +
            "receipt['fake_spool_bytes']=size\nsys.stdout.write(json.dumps(receipt))\n", encoding="utf-8")
        probe.chmod(0o700)

        def old_limit():
            runner.resource.setrlimit(runner.resource.RLIMIT_FSIZE, (runner.MAX_OUTPUT_BYTES, runner.MAX_OUTPUT_BYTES))

        baseline = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=4,
                                  preexec_fn=old_limit, check=False)
        self.assertEqual(baseline.returncode, -runner.signal.SIGXFSZ)
        self.assertEqual(baseline.stdout, b"")
        repaired = runner._trial(probe, episode, 1000000, 4, self.work, 0)
        self.assertEqual(repaired["status"], "completed", repaired.get("error"))
        self.assertEqual(repaired["child_file_limit_bytes"], 17000000)
        self.assertEqual(repaired["receipt"]["fake_spool_bytes"], 9 * 1024 * 1024)
        self.assertLess((self.work / repaired["raw_stdout"]).stat().st_size, runner.MAX_OUTPUT_BYTES)

    @unittest.skipUnless(os.name == "posix" and runner.resource is not None, "POSIX bounded boundary spool")
    def test_additive_boundary_internal_spool_exceeds_output_limit_without_raising_pipe_caps(self) -> None:
        episode = {"id": "boundary", "scenario": "hf-vector-two-hop", "mode": "receive",
                   "waveform": "reference", "seed": 73613, "two_hop_additive_boundary": True}
        probe = self.work / "boundary_probe.py"
        command = runner._command(probe, episode, 4_350_000, 3.5)
        receipt = fake_completed_receipt(command)
        probe.write_text(f"#!{sys.executable}\n" +
            "import json,pathlib,signal,sys\n"
            "signal.signal(signal.SIGXFSZ,signal.SIG_DFL)\n"
            "path=pathlib.Path(__file__).with_suffix('.spool')\n"
            "with path.open('wb') as target:\n"
            "    for _ in range(9): target.write(b'x'*(1024*1024))\n"
            "size=path.stat().st_size\npath.unlink()\n" +
            f"receipt=json.loads({json.dumps(receipt)!r})\n" +
            "receipt['fake_internal_spool_bytes']=size\nsys.stdout.write(json.dumps(receipt))\n", encoding="utf-8")
        probe.chmod(0o700)
        self.assertTrue(runner._native_child_uses_temporary_spool(episode))
        self.assertEqual(runner._native_child_file_limit(episode, 4_350_000), 73_950_000)
        with patch.object(runner, "_validate_completed_receipt"):
            repaired = runner._trial(probe, episode, 4_350_000, 4, self.work, 0)
        self.assertEqual(repaired["status"], "completed", repaired.get("error"))
        self.assertEqual(repaired["child_file_limit_bytes"], 73_950_000)
        self.assertEqual(repaired["receipt"]["fake_internal_spool_bytes"], 9 * 1024 * 1024)
        self.assertLess((self.work / repaired["raw_stdout"]).stat().st_size, runner.MAX_OUTPUT_BYTES)

    @unittest.skipUnless(os.name == "posix" and runner.resource is not None, "POSIX bounded pipe capture")
    def test_service_larger_file_cap_does_not_raise_stdout_or_stderr_caps(self) -> None:
        episode = {"id": "output", "scenario": "awgn", "mode": "receive", "waveform": "reference", "seed": 1,
                   "service_replay": True, "service_spool_byte_limit": 12 * 1024 * 1024}
        for stream in ("stdout", "stderr"):
            probe = self.work / (stream + "_probe.py")
            probe.write_text(f"#!{sys.executable}\nimport sys\n" +
                f"for _ in range(9): sys.{stream}.buffer.write(b'x'*(1024*1024))\n", encoding="utf-8")
            probe.chmod(0o700)
            with self.subTest(stream=stream):
                result = runner._trial(probe, episode, 1000000, 4, self.work, 0)
                self.assertEqual(result["status"], "invalid_receipt")
                self.assertIn(f"native {stream} exceeded", result["error"])
                self.assertEqual(result["child_file_limit_bytes"], 12 * 1024 * 1024)
                self.assertEqual((self.work / result["raw_" + stream]).stat().st_size, runner.MAX_OUTPUT_BYTES)
                self.assertLessEqual((self.work / result["raw_stderr" if stream == "stdout" else "raw_stdout"]).stat().st_size,
                                     runner.MAX_OUTPUT_BYTES)

    @unittest.skipUnless(os.name == "posix" and runner.resource is not None, "POSIX bounded service child")
    def test_service_file_cap_remains_sample_bounded_and_timeout_is_preserved(self) -> None:
        episode = {"id": "timeout", "scenario": "awgn", "mode": "receive", "waveform": "reference", "seed": 1,
                   "service_replay": True, "service_spool_byte_limit": 2**64 - 1}
        probe = self.work / "waiting_probe.py"
        probe.write_text(f"#!{sys.executable}\nimport time\ntime.sleep(2)\n", encoding="utf-8")
        probe.chmod(0o700)
        result = runner._trial(probe, episode, 1000000, .1, self.work, 0)
        self.assertEqual(result["status"], "timed_out")
        self.assertEqual(result["child_file_limit_bytes"], 17000000)

    def test_complete_failure_is_data_and_fixed_sample_interval_is_reported(self) -> None:
        campaign = json.loads(self.campaign_file.read_text())
        campaign["confirmatory_strata"] = {}
        self.campaign_file.write_text(json.dumps(campaign))
        output = self.work / "output"
        with patch.object(runner.subprocess, "run", side_effect=self._fake_completed) as launch:
            run = runner.run_campaign(self.probe, self.campaign_file, output)
        self.assertEqual(run["status"], "completed")
        self.assertEqual(run["executed_trials"], 2)
        self.assertEqual(run["trials"][0]["receipt"]["status"], "completed")
        self.assertFalse(run["trials"][0]["receipt"]["outcomes"]["correct_complete"])
        self.assertEqual(run["statistics"], {})
        self.assertLessEqual(launch.call_args.kwargs["timeout"], 2)
        command = launch.call_args.args[0]
        native_budget = float(command[command.index("--max-wall-seconds") + 1])
        self.assertLess(native_budget, launch.call_args.kwargs["timeout"])
        self.assertFalse(launch.call_args.kwargs["shell"])
        self.assertTrue((output / "frozen-input.json").exists())
        self.assertTrue((output / "report.md").exists())
        self.assertEqual(run["execution_failures"], 0)
        self.assertEqual(run["valid_communication_failures"], 2)

    def test_per_episode_wall_and_sample_caps_are_enforced(self) -> None:
        campaign = {"schema_version": 1, "name": "episode-caps", "max_wall_seconds": 4,
                    "per_trial_wall_seconds": 3, "max_trials": 1, "max_samples": 1000,
                    "episodes": [{"id": "small", "scenario": "awgn", "mode": "receive",
                                  "waveform": "smoke", "seed": 1, "max_wall_seconds": 1.25,
                                  "max_samples": 500}]}
        self.campaign_file.write_text(json.dumps(campaign))
        with patch.object(runner.subprocess, "run", side_effect=self._fake_completed) as launch:
            runner.run_campaign(self.probe, self.campaign_file, self.work / "episode-caps-output")
        self.assertLessEqual(launch.call_args.kwargs["timeout"], 1.25)
        command = launch.call_args.args[0]
        self.assertEqual(command[command.index("--max-samples") + 1], "500")
        self.assertLess(float(command[command.index("--max-wall-seconds") + 1]), 1.25)

    def test_slot_report_separates_data_decision_and_censors_timeouts(self) -> None:
        campaign = {"schema_version": 1, "name": "slot-summary", "max_wall_seconds": 8,
                    "per_trial_wall_seconds": 2, "max_trials": 4, "max_samples": 1000,
                    "episodes": [{"id": f"slot-{label}", "scenario": "awgn", "mode": "slot",
                                  "waveform": "smoke", "seed": seed}
                                 for label, seed in (("data", 1), ("quiet", 2),
                                                     ("ambiguous", 3), ("censored", 4))]}
        self.campaign_file.write_text(json.dumps(campaign))
        calls = 0

        def valid_then_censored(command, **kwargs):
            nonlocal calls
            calls += 1
            if calls == 4:
                raise subprocess.TimeoutExpired(command, kwargs["timeout"])
            if calls == 1:
                return self._fake_completed(command, **kwargs)
            decision = "quiet" if calls == 2 else "ambiguous"
            receipt = fake_completed_receipt(command)
            receipt["outcomes"]["first_public_observation_correct_data"] = False
            receipt["slots"][0]["decision"] = decision
            kwargs["stdout"].write((json.dumps(receipt) + "\n").encode())
            kwargs["stderr"].write(b"")
            return subprocess.CompletedProcess(command, 0)

        output = self.work / "slot-summary-output"
        with patch.object(runner.subprocess, "run", side_effect=valid_then_censored):
            run = runner.run_campaign(self.probe, self.campaign_file, output)
        report = (output / "report.md").read_text()
        self.assertEqual(run["valid_communication_failures"], 0)
        self.assertIn("one public slot; message completion not evaluated", report)
        self.assertIn("| data | yes |", report)
        self.assertIn("| quiet | no |", report)
        self.assertIn("| ambiguous | no |", report)
        self.assertIn("censored (timed_out)", report)
        self.assertIn("| censored | censored |", report)

    def test_diagnose_report_states_component_scope_without_communication_claim(self) -> None:
        campaign = {"schema_version": 1, "name": "diagnose-report", "max_wall_seconds": 2,
                    "per_trial_wall_seconds": 2, "max_trials": 1, "max_samples": 1000,
                    "episodes": [{"id": "diagnose", "scenario": "vlf-phase-wander",
                                  "mode": "diagnose", "waveform": "reference", "seed": 5}]}
        self.campaign_file.write_text(json.dumps(campaign))
        output = self.work / "diagnose-output"
        with patch.object(runner.subprocess, "run", side_effect=self._fake_completed):
            run = runner.run_campaign(self.probe, self.campaign_file, output)
        report = (output / "report.md").read_text()
        self.assertEqual(run["status"], "completed")
        self.assertEqual(run["valid_communication_failures"], 0)
        self.assertIn("component complete; no slot decision", report)
        self.assertNotIn("physical message complete", report)

    def test_gui_varying_report_does_not_count_component_admission_as_communication(self) -> None:
        campaign = {"schema_version": 1, "name": "gui-varying-report", "max_wall_seconds": 4,
                    "per_trial_wall_seconds": 2, "max_trials": 2, "max_samples": 1000,
                    "episodes": [gui_varying_episode(id=control, control=control) for control in ("wanted", "noise")]}
        self.campaign_file.write_text(json.dumps(campaign))
        output = self.work / "gui-varying-output"

        def admitted_component(command, **kwargs):
            receipt = fake_completed_receipt(command)
            receipt["hf_gui_varying_diagnostic"]["varying"].update(diagnostic_admits_data=True, diagnostic_data_bit=1)
            kwargs["stdout"].write((json.dumps(receipt) + "\n").encode())
            return subprocess.CompletedProcess(command, 0)

        with patch.object(runner.subprocess, "run", side_effect=admitted_component):
            run = runner.run_campaign(self.probe, self.campaign_file, output)
        report = (output / "report.md").read_text()
        self.assertEqual(run["status"], "completed")
        self.assertEqual(run["execution_failures"], 0)
        self.assertEqual(run["valid_communication_failures"], 0)
        self.assertEqual(run["statistics"], {})
        self.assertIn("canonical varying component complete; no receiver decision or message completion", report)
        self.assertNotIn("physical message complete", report)

    def test_paired_service_report_distinguishes_negative_controls_from_messages(self) -> None:
        episodes = [{"id": f"{control}-{state}", "scenario": "awgn", "mode": "receive", "waveform": "reference", "seed": i,
                     "service_replay": True, "service_episodes": 2, "control": control}
                    for i, (control, state) in enumerate((
                        ("wanted", "pass"), ("wanted", "fail"), ("noise", "pass"), ("noise", "fail"),
                        ("wrong-key", "pass"), ("wrong-ordinal", "pass")), 1)]
        campaign = {"schema_version": 1, "name": "service-report", "max_wall_seconds": 10,
                    "per_trial_wall_seconds": 2, "max_trials": len(episodes), "max_samples": 10000,
                    "episodes": episodes}
        self.campaign_file.write_text(json.dumps(campaign))
        output = self.work / "service-report-output"

        def replay(command, out, err, timeout, options):
            receipt = fake_completed_receipt(command)
            if receipt["seed"] == 1:
                complete_service_messages(receipt)
            elif receipt["seed"] == 4:
                receipt["service_replay"]["communication_complete"] = False
                receipt["outcomes"]["false_data"] = 1
            out.write((json.dumps(receipt) + "\n").encode())
            return 0, None

        with patch.object(runner, "_run_service_child", side_effect=replay):
            run = runner.run_campaign(self.probe, self.campaign_file, output)
        self.assertEqual(run["status"], "completed")
        self.assertEqual(run["valid_communication_failures"], 1)
        report = (output / "report.md").read_text()
        lines = {episode["id"]: next(line for line in report.splitlines() if line.startswith(f"| {episode['id']} |"))
                 for episode in episodes}
        self.assertIn("both physical messages complete", lines["wanted-pass"])
        self.assertIn("paired physical-message completion failed", lines["wanted-fail"])
        for name in ("noise-pass", "wrong-key-pass", "wrong-ordinal-pass"):
            self.assertIn("negative control passed; no accepted messages or false DATA", lines[name])
            self.assertNotIn("physical messages complete", lines[name])
        self.assertIn("negative control did not pass", lines["noise-fail"])
        self.assertIn("false DATA=1; accepted false messages=0", lines["noise-fail"])
        self.assertEqual(run["negative_controls"]["counted_trials"], 4)
        self.assertEqual(run["negative_controls"]["false_data_decisions"], 1)
        self.assertEqual(run["negative_controls"]["accepted_false_messages"], 0)
        self.assertEqual(run["wanted_outcomes"]["completed_trials"], 2)
        self.assertEqual(run["wanted_outcomes"]["trials_without_delivery"], 1)
        self.assertEqual(run["wanted_outcomes"]["accepted_messages"]["total"], 2)

    def test_wanted_report_counts_actual_incorrect_fragment_with_zero_wrong_bits(self) -> None:
        # The compact projection preserves the native MPS holdout's delivery counters and events.
        fixture = (RUNNER.parent / "tests/fixtures/hf_phase_screens_incorrect_delivery.json").read_bytes()
        self.assertEqual(hashlib.sha256(fixture).hexdigest(),
                         "b5bfe3ef01b95bcd3ffd54c7645909c2635c4e915377931423d53e4d5ef08a7c")
        recorded = json.loads(fixture)
        self.assertEqual(recorded["source_sha256"], "7aa1dc381be58b62758e9746821ab086a57909c06b5a9cb045141c440b929b8f")
        self.assertEqual(recorded["outcomes"]["wrong_bits"], 0)
        self.assertEqual([event["decoded_payload_bytes"] for event in recorded["messages"] if event["decoded"]], [[32]])
        names = ("fragment", "no-delivery", "matching-incomplete", "legacy", "mismatched-count", "censored")
        episodes = [{"id": name, "scenario": "awgn", "mode": "receive", "waveform": "reference", "seed": i,
                     "message": recorded["transmitted_message"]} for i, name in enumerate(names, 1)]
        campaign = {"schema_version": 1, "name": "wanted-report", "max_wall_seconds": 10,
                    "per_trial_wall_seconds": 2, "max_trials": len(episodes), "max_samples": 10000, "episodes": episodes}
        self.campaign_file.write_text(json.dumps(campaign))
        output = self.work / "wanted-report-output"

        def native(command, **kwargs):
            receipt = fake_completed_receipt(command)
            name = names[receipt["seed"] - 1]
            if name in {"fragment", "censored", "mismatched-count"}:
                receipt["outcomes"].update(recorded["outcomes"])
                receipt["messages"] = recorded["messages"]
                if name == "mismatched-count":
                    receipt["outcomes"]["accepted_messages"] = 2
            elif name == "no-delivery":
                receipt["outcomes"].update(accepted_messages=0, ambiguous_active_slots=1)
            elif name == "matching-incomplete":
                receipt["outcomes"].update(accepted_messages=1)
                receipt["messages"] = [{"decoded": True, "matches_transmitted_message": True}]
            if name == "censored":
                receipt["status"] = "time_limit"
            kwargs["stdout"].write((json.dumps(receipt) + "\n").encode())
            return subprocess.CompletedProcess(command, 4 if name == "censored" else 0)

        with patch.object(runner.subprocess, "run", side_effect=native):
            run = runner.run_campaign(self.probe, self.campaign_file, output)
        self.assertEqual(run["valid_communication_failures"], 5)
        self.assertEqual(run["execution_failures"], 1)
        wanted = run["wanted_outcomes"]
        self.assertEqual(wanted["completed_trials"], 5)
        self.assertEqual(wanted["trials_without_delivery"], 1)
        self.assertEqual(wanted["accepted_incorrect_messages"], {"total": 1, "trials_with_any": 1, "trials_unavailable": 2})
        self.assertEqual(wanted["premature_completions"]["total"], 4)
        self.assertEqual(wanted["active_slot_erasures"]["total"], 17)
        persisted = json.loads((output / "run.json").read_text())
        self.assertEqual(persisted["wanted_outcomes"], wanted)
        self.assertEqual(persisted["trials"][0]["wanted_outcomes"],
                         {"accepted_messages": 1, "accepted_incorrect_messages": 1,
                          "premature_completions": 2, "active_slot_erasures": 8})
        self.assertNotIn("wanted_outcomes", persisted["trials"][-1])
        report = (output / "report.md").read_text()
        lines = {name: next(line for line in report.splitlines() if line.startswith(f"| {name} |")) for name in names}
        self.assertIn("accepted incorrect payloads=1 of 1, premature completions=2, active-slot erasures=8", lines["fragment"])
        self.assertIn("no accepted message", lines["no-delivery"])
        self.assertIn("accepted matching payloads=1 with incomplete endpoint", lines["matching-incomplete"])
        self.assertIn("accepted message count unavailable", lines["legacy"])
        self.assertIn("accepted messages=2, payload match unavailable", lines["mismatched-count"])
        self.assertNotIn("accepted incorrect payloads", lines["censored"])
        del run["wanted_outcomes"]
        self.assertEqual(report, runner._markdown(campaign, run))

    def test_ordinary_negative_report_separates_false_data_from_accepted_messages(self) -> None:
        episodes = [{"id": control, "scenario": "awgn", "mode": "receive", "waveform": "reference", "seed": i,
                     "control": control} for i, control in enumerate(("wrong-key", "noise", "wrong-ordinal", "wanted"), 1)]
        campaign = {"schema_version": 1, "name": "negative-report", "max_wall_seconds": 10,
                    "per_trial_wall_seconds": 2, "max_trials": len(episodes), "max_samples": 10000, "episodes": episodes}
        self.campaign_file.write_text(json.dumps(campaign))
        output = self.work / "negative-report-output"

        def native(command, **kwargs):
            receipt = fake_completed_receipt(command)
            receipt["outcomes"].update(accepted_messages=0, resumed_data_slots=0)
            if receipt["control"] == "wrong-key":
                # Sanitized counters from the actual wrong-key causal follow-up:
                # three false DATA decisions, one rejected physical frame, no accepted message.
                receipt["outcomes"].update(false_data=3, actual_scored_slots=5,
                    physical_completions=1, integrity_rejections=1)
                receipt["messages"] = [{"decoded_content": None, "integrity_rejected": True}]
            elif receipt["control"] == "noise":
                receipt["outcomes"].update(false_data=6, actual_scored_slots=10,
                    physical_completions=2, accepted_messages=2)
                receipt["messages"] = [{"decoded_content": "e"}, {"decoded_content": "e"}]
            kwargs["stdout"].write((json.dumps(receipt) + "\n").encode())
            return subprocess.CompletedProcess(command, 0)

        with patch.object(runner.subprocess, "run", side_effect=native):
            run = runner.run_campaign(self.probe, self.campaign_file, output)
        self.assertEqual(run["status"], "completed")
        self.assertEqual(run["valid_communication_failures"], 1)  # Wanted semantics are unchanged.
        summary = run["negative_controls"]
        self.assertEqual(summary["completed_trials"], 3)
        self.assertEqual(summary["false_data_decisions"], 9)
        self.assertEqual(summary["trials_with_false_data"], 2)
        self.assertEqual(summary["accepted_false_messages"], 2)  # Do not add messages[] again.
        self.assertEqual(summary["trials_with_accepted_false_messages"], 1)
        persisted = json.loads((output / "run.json").read_text())
        self.assertEqual(persisted["negative_controls"], summary)
        report = (output / "report.md").read_text()
        lines = {episode["id"]: next(line for line in report.splitlines() if line.startswith(f"| {episode['id']} | awgn |"))
                 for episode in episodes}
        self.assertIn("false DATA=3; accepted false messages=0", lines["wrong-key"])
        self.assertIn("false DATA=6; accepted false messages=2", lines["noise"])
        self.assertIn("negative control passed", lines["wrong-ordinal"])
        self.assertIn("no correct physical message completion", lines["wanted"])
        self.assertNotIn("no correct physical message completion", lines["wrong-key"])
        # Old run metadata needs no mutation: rendering recomputes the new table
        # from its original classifications and rehydrated raw native receipts.
        del run["negative_controls"]
        self.assertEqual(report, runner._markdown(campaign, run))

    def test_negative_summary_excludes_censored_work_and_does_not_invent_missing_counts(self) -> None:
        def trial(control="noise", **changes):
            row = {"status": "completed", "episode": {"mode": "receive", "control": control},
                   "receipt": {"status": "completed", "outcomes": {"false_data": 0, "accepted_messages": 0}}}
            row.update(changes)
            return row

        rows = [trial()]
        # Actual service noise schema: a complete negative replay has no target
        # messages, not a successful wanted-message endpoint.
        rows.append(trial("wrong-key", episode={"mode": "receive", "control": "wrong-key", "service_replay": True},
            receipt={"status": "completed", "service_replay": {"work_complete": True, "communication_complete": True,
                "expected_message_count": 0}, "outcomes": {"false_data": 0, "accepted_messages": 0}}))
        for value in (None, True, -1, 1.5, math.inf):
            bad = trial("wrong-ordinal")
            bad["receipt"]["outcomes"]["accepted_messages"] = value
            rows.append(bad)
        missing = trial("wrong-ordinal")
        del missing["receipt"]["outcomes"]["false_data"]
        rows.append(missing)
        for status in ("invalid_receipt", "timed_out", "launch_error", "receipt_input_mismatch"):
            rows.append(trial(status=status))
        rows.append(trial(receipt={"status": "time_limit", "outcomes": {"false_data": 100, "accepted_messages": 100}}))
        rows.append(trial(episode={"mode": "slot", "control": "noise"}))
        rows.append(trial("wanted"))
        rows.append(trial(episode={"mode": "receive", "control": "noise", "service_replay": True},
            receipt={"status": "completed", "service_replay": {"work_complete": False},
                     "outcomes": {"false_data": 100, "accepted_messages": 100}}))
        summary = runner._negative_control_summary(rows)
        self.assertEqual(summary["completed_trials"], 8)
        self.assertEqual(summary["counted_trials"], 2)
        self.assertEqual(summary["unavailable_count_trials"], 6)
        self.assertEqual(summary["false_data_decisions"], 0)
        self.assertEqual(summary["accepted_false_messages"], 0)
        self.assertEqual(summary["by_control"]["wrong-ordinal"]["unavailable_count_trials"], 6)
        self.assertIn("false DATA=0; accepted false messages=unavailable", runner._negative_control_label(
            rows[2]["receipt"], rows[2]["episode"]))
        self.assertEqual(rows[-1]["status"], "completed")  # Summary never reclassifies source trials.

    def test_channel_episode_requires_nonempty_received_signal(self) -> None:
        campaign = {"schema_version": 1, "name": "channel", "max_wall_seconds": 2,
                    "per_trial_wall_seconds": 2, "max_trials": 1, "max_samples": 1000,
                    "episodes": [{"id": "empty", "scenario": "eme-disk-episode", "mode": "channel",
                                  "waveform": "smoke", "seed": 1, "duration_seconds": .25}]}
        self.campaign_file.write_text(json.dumps(campaign))

        def fake_empty(command, **kwargs):
            kwargs["stdout"].write((json.dumps(fake_completed_receipt(
                command, channel={"contains_signal_energy": False, "samples": 100})) + "\n").encode())
            kwargs["stderr"].write(b"")
            return subprocess.CompletedProcess(command, 0)

        with patch.object(runner.subprocess, "run", side_effect=fake_empty):
            run = runner.run_campaign(self.probe, self.campaign_file, self.work / "empty-output")
        self.assertEqual(run["status"], "incomplete_checks")
        self.assertEqual(run["trials"][0]["status"], "channel_empty")
        self.assertEqual(run["execution_failures"], 1)

    def test_channel_keeps_native_limits_and_errors_without_energy_report(self) -> None:
        campaign = {"schema_version": 1, "name": "channel-status", "max_wall_seconds": 8,
                    "per_trial_wall_seconds": 2, "max_trials": 4, "max_samples": 1000,
                    "episodes": [{"id": f"case-{status}", "scenario": "eme-disk-episode", "mode": "channel",
                                  "waveform": "smoke", "seed": index}
                                 for index, status in enumerate(("resource_limit", "time_limit",
                                                                 "unavailable", "error"), 1)]}
        self.campaign_file.write_text(json.dumps(campaign))
        codes = {"resource_limit": 3, "time_limit": 4, "unavailable": 5, "error": 2}
        calls = 0

        def fake_status(command, **kwargs):
            nonlocal calls
            status = ("resource_limit", "time_limit", "unavailable", "error")[calls]
            calls += 1
            kwargs["stdout"].write((json.dumps({"schema_version": 1, "status": status}) + "\n").encode())
            kwargs["stderr"].write(b"")
            return subprocess.CompletedProcess(command, codes[status])

        with patch.object(runner.subprocess, "run", side_effect=fake_status):
            run = runner.run_campaign(self.probe, self.campaign_file, self.work / "native-status-output")
        self.assertEqual([trial["status"] for trial in run["trials"]],
                         ["resource_limit", "time_limit", "unavailable", "error"])
        self.assertEqual(run["execution_failures"], 4)

    def test_exact_zero_energy_noise_control_is_preserved(self) -> None:
        campaign = {"schema_version": 1, "name": "zero-noise-control", "max_wall_seconds": 2,
                    "per_trial_wall_seconds": 2, "max_trials": 1, "max_samples": 1000,
                    "episodes": [{"id": "noise", "scenario": "awgn", "mode": "receive",
                                  "waveform": "smoke", "seed": 33, "control": "noise"}]}
        self.campaign_file.write_text(json.dumps(campaign))

        def fake_zero_noise(command, **kwargs):
            receipt = fake_completed_receipt(
                command, control="noise", channel={"contains_signal_energy": False,
                                                       "signal_energy_sample_sum": 0, "samples": 100})
            kwargs["stdout"].write((json.dumps(receipt) + "\n").encode())
            kwargs["stderr"].write(b"")
            return subprocess.CompletedProcess(command, 0)

        with patch.object(runner.subprocess, "run", side_effect=fake_zero_noise):
            run = runner.run_campaign(self.probe, self.campaign_file, self.work / "noise-zero-output")
        self.assertEqual(run["status"], "completed")
        self.assertEqual(run["trials"][0]["status"], "completed")
        self.assertFalse(run["trials"][0]["receipt"]["channel"]["contains_signal_energy"])
        self.assertEqual(run["valid_communication_failures"], 0)

    def test_native_timeout_marks_campaign_incomplete_but_keeps_later_trials(self) -> None:
        campaign = valid_campaign()
        campaign["confirmatory_strata"] = {}
        self.campaign_file.write_text(json.dumps(campaign))
        calls = 0

        def timeout_then_complete(command, **kwargs):
            nonlocal calls
            calls += 1
            if calls == 1:
                raise subprocess.TimeoutExpired(command, kwargs["timeout"])
            return self._fake_completed(command, **kwargs)

        with patch.object(runner.subprocess, "run", side_effect=timeout_then_complete):
            run = runner.run_campaign(self.probe, self.campaign_file, self.work / "timeout-output")
        self.assertEqual(run["status"], "incomplete_checks")
        self.assertEqual(run["stop_reason"], "trial_timeout")
        self.assertEqual(run["executed_trials"], 2)
        self.assertEqual(run["trials"][1]["status"], "completed")
        self.assertEqual(run["valid_communication_failures"], 1)

    def test_opt_in_stop_on_execution_failure_keeps_timeout_evidence_and_stops(self) -> None:
        campaign = valid_campaign()
        campaign["confirmatory_strata"] = {}
        campaign["stop_on_execution_failure"] = True
        self.campaign_file.write_text(json.dumps(campaign))
        calls = 0

        def timeout_then_complete(command, **kwargs):
            nonlocal calls
            calls += 1
            if calls == 1:
                raise subprocess.TimeoutExpired(command, kwargs["timeout"])
            return self._fake_completed(command, **kwargs)

        output = self.work / "stop-timeout-output"
        with patch.object(runner.subprocess, "run", side_effect=timeout_then_complete):
            run = runner.run_campaign(self.probe, self.campaign_file, output)
        self.assertEqual(run["status"], "incomplete_checks")
        self.assertEqual(run["stop_reason"], "trial_timeout")
        self.assertEqual(run["executed_trials"], 1)
        self.assertEqual(run["trials"][0]["status"], "timed_out")
        for field in ("raw_stdout", "raw_stderr"):
            self.assertTrue((output / run["trials"][0][field]).exists())

    def test_opt_in_stop_on_native_refusal_preserves_native_reason(self) -> None:
        campaign = valid_campaign()
        campaign["confirmatory_strata"] = {}
        campaign["stop_on_execution_failure"] = True
        self.campaign_file.write_text(json.dumps(campaign))

        def refuse_then_complete(command, **kwargs):
            receipt = fake_completed_receipt(command)
            receipt["status"] = "unavailable"
            receipt["reason"] = "frozen numerical qualification refused this case"
            kwargs["stdout"].write((json.dumps(receipt) + "\n").encode())
            kwargs["stderr"].write(b"")
            return subprocess.CompletedProcess(command, 5)

        output = self.work / "stop-refusal-output"
        with patch.object(runner.subprocess, "run", side_effect=refuse_then_complete) as launch:
            run = runner.run_campaign(self.probe, self.campaign_file, output)
        self.assertEqual(run["status"], "incomplete_checks")
        self.assertEqual(run["stop_reason"], "trial_unavailable")
        self.assertEqual(run["executed_trials"], 1)
        self.assertEqual(launch.call_count, 1)
        trial = run["trials"][0]
        self.assertEqual(trial["status"], "unavailable")
        self.assertEqual(trial["receipt"]["reason"], "frozen numerical qualification refused this case")
        raw = json.loads((output / trial["raw_stdout"]).read_text())
        self.assertEqual(raw["reason"], trial["receipt"]["reason"])
        persisted = json.loads((output / "run.json").read_text())
        self.assertEqual(persisted["trials"][0]["receipt_reason"], raw["reason"])

    def test_opt_in_stop_does_not_treat_valid_communication_miss_as_execution_failure(self) -> None:
        campaign = valid_campaign()
        campaign["confirmatory_strata"] = {}
        campaign["stop_on_execution_failure"] = True
        self.campaign_file.write_text(json.dumps(campaign))
        with patch.object(runner.subprocess, "run", side_effect=self._fake_completed) as launch:
            run = runner.run_campaign(self.probe, self.campaign_file, self.work / "valid-miss-output")
        self.assertEqual(run["status"], "completed")
        self.assertEqual(run["executed_trials"], 2)
        self.assertEqual(launch.call_count, 2)
        self.assertEqual(run["execution_failures"], 0)
        self.assertEqual(run["valid_communication_failures"], 2)

    def test_probe_hash_change_invalidates_current_episode_and_stops(self) -> None:
        campaign = valid_campaign()
        self.campaign_file.write_text(json.dumps(campaign))
        frozen_hash = "a" * 64
        with patch.object(runner, "_sha256", side_effect=(frozen_hash, frozen_hash, "b" * 64)), \
             patch.object(runner.subprocess, "run", side_effect=self._fake_completed) as launch:
            run = runner.run_campaign(self.probe, self.campaign_file, self.work / "probe-changed-output")
        self.assertEqual(run["status"], "incomplete_checks")
        self.assertEqual(run["stop_reason"], "probe_changed")
        self.assertEqual(run["executed_trials"], 1)
        self.assertEqual(launch.call_count, 1)
        self.assertEqual(run["trials"][0]["status"], "probe_changed")
        self.assertIsNotNone(run["trials"][0].get("receipt"))
        self.assertEqual(run["statistics"]["awgn-fixed"]["usable_n"], 0)
        self.assertIsNone(run["statistics"]["awgn-fixed"]["interval"])
        self.assertEqual(run["execution_failures"], 1)
        self.assertEqual(run["valid_communication_failures"], 0)

    def test_confirmatory_interval_is_withheld_when_fixed_n_is_incomplete(self) -> None:
        campaign = valid_campaign()
        campaign["episodes"] = campaign["episodes"][:1]
        campaign["max_trials"] = 1
        campaign["confirmatory_strata"] = {"awgn-fixed": 2}
        with self.assertRaisesRegex(runner.CampaignError, "exactly 2"):
            campaign_file = self.work / "invalid.json"
            campaign_file.write_text(json.dumps(campaign))
            runner._read_campaign(campaign_file)

    def test_output_directory_must_be_inside_local_evidence(self) -> None:
        outside = Path(tempfile.gettempdir()) / f"appraisal-evidence-outside-{self.work.name}"
        with self.assertRaisesRegex(runner.CampaignError, "output directory"):
            runner.run_campaign(self.probe, self.campaign_file, outside)

    def test_incomplete_fixed_sample_has_no_interval(self) -> None:
        result = runner._statistical_summary(valid_campaign(), [{
            "episode": valid_campaign()["episodes"][0], "status": "completed",
            "receipt": {"status": "completed", "outcomes": {"correct_complete": True}},
        }])
        self.assertFalse(result["awgn-fixed"]["complete_fixed_n"])
        self.assertIsNone(result["awgn-fixed"]["interval"])

    def test_component_channels_have_bounded_nonempty_signal_windows(self) -> None:
        campaign = runner._read_campaign(RUNNER.parent / "campaigns" / "integration-smoke.json")
        channels = [item for item in campaign["episodes"] if item["mode"] == "channel"]
        self.assertEqual(len(channels), 5)
        self.assertTrue(all(item["duration_seconds"] >= 10 for item in channels))
        lunar = next(item for item in channels if item["id"] == "eme-render")
        self.assertEqual(lunar["scenario"], "eme-disk-episode")
        self.assertEqual(lunar["waveform"], "reference")

    def test_duration_contrast_uses_distinct_reference_geometries(self) -> None:
        campaign = runner._read_campaign(RUNNER.parent / "campaigns" / "awgn-trend.json")
        short = next(item for item in campaign["episodes"] if item["id"] == "rx-b16-cn0m04-s6101")
        long = next(item for item in campaign["episodes"] if item["id"] == "rx-b32-cn0m04-s6101")
        self.assertEqual(short["scenario"], long["scenario"])
        self.assertEqual(short["waveform"], long["waveform"])
        self.assertEqual(short["waveform"], "reference")
        self.assertEqual(short["seed"], long["seed"])
        self.assertEqual(short["cn0_db_hz"], long["cn0_db_hz"])
        self.assertLess(short["bit_seconds"], long["bit_seconds"])


def matrix_plan_receipt() -> tuple[dict, dict]:
    campaign = json.loads((RUNNER.parent / "campaigns" / "two-hop-long-alarm-matrix.json").read_text())
    episode = dict(campaign["episodes"][0])
    episode["mode"] = "plan"
    episode.pop("reception_requirement")
    episode.pop("control", None)
    identity = {"appraisal_source_sha256": "a" * 64, "native_source_sha256": "b" * 64}
    rows = []
    fingerprint = "c" * 64
    cases = runner.TWO_HOP_ALARM_MATRIX_CASES
    for case_id, _cn0, _message, bit in cases:
        active = set(range(13)) if bit is None else {0, 3, 6, 9, 12}
        public_slots = [{"index": i, "symbol_ordinal": i, "pattern_epoch": 1_793_404_800 + 150 * i,
                         "evaluator_transmitting": i in active} for i in range(15)]
        plan = {"max_wall_seconds": 1200, "max_samples": 4_350_000, "cn0_db_hz": _cn0,
                "scored_slot_horizon": 15, "first_uncontaminated_quiet_slot": 13,
                "initial_ordinal": 0, "public_slots": public_slots}
        if bit is not None:
            plan["repeated_bit"] = {"bit": bit, "active_slots": 5, "attempts": 5,
                                    "requested_active_slots": 5, "separate_attempts": True,
                                    "used_for_receiver_decisions": False,
                                    "transmission_scope": "Five one-bit emissions advance the public ordinal. Each has two scored quiet windows through one continuous two-hop channel.",
                                    "public_key_fingerprint_sha256": fingerprint}
        child = {"status": "completed", "requested_scenario": "hf-vector-two-hop", "mode": "plan",
                 "waveform": "reference", "seed": 73613, "control": "wanted",
                 "source_identity": identity, "plan": plan}
        rows.append({"case_id": case_id, "receipt": child})
    return episode, {"status": "completed", "requested_scenario": "hf-vector-two-hop", "mode": "plan",
                     "waveform": "reference", "seed": 73613, "control": "wanted",
                     "source_identity": identity,
                     "two_hop_alarm_matrix": {"planned_rows": 6,
                         "aggregate_wall_limit_seconds": 1200,
                         "per_row_sample_limit": 4_350_000, "rows": rows}}


def matrix_receive_receipt() -> tuple[dict, dict]:
    episode, receipt = matrix_plan_receipt()
    episode.update(mode="receive", reception_requirement="characterization")
    matrix = receipt["two_hop_alarm_matrix"]
    matrix["aggregate_wall_limit_seconds"] = 1199.5
    matrix["wall_seconds"] = 1000.
    receipt["mode"] = "receive"
    components = {"scope": "Public candidate-averaged log evidence before global coordinate and branch priors.",
                  "fixed_affine": None, "short_varying": None, "short_fixed_gain": None,
                  "short_frequency_track": None, "medium_coherent_gain": None, "linear_drift": None,
                  "slow_gain_coherence_seconds": None,
                  "affine_selected_prior_mass": None, "short_frequency_log_prior_adjustment": None}
    for index, row in enumerate(matrix["rows"]):
        child = row["receipt"]
        child["mode"] = "receive"
        child["plan"]["max_wall_seconds"] = 1199.5
        child["timing"] = {"wall_seconds": 1.}
        child["channel"] = {
            "vector_two_hop": {"table_sha256": "e" * 64,
                               "incident_energy_sample_sum": 4., "selected_energy_sample_sum": 3.},
            "vector_two_hop_table_use": {"table_reused": index != 0,
                                         "table_sha256": "e" * 64,
                                         "preparation_seconds": 500. if index == 0 else 1.}}
        child["slots"] = [{"likelihood_components": dict(components)} for _ in range(15)]
        child["repeated_bit"] = {"correct_data": 0, "attempts": 5}
    return episode, receipt


def additive_boundary_plan_receipt() -> tuple[dict, dict]:
    episode = json.loads((RUNNER.parent / "campaigns" / "two-hop-additive-boundary.json").read_text())["episodes"][0]
    episode = dict(episode, mode="plan")
    episode.pop("reception_requirement")
    identity = {"appraisal_source_sha256": "a" * 64, "native_source_sha256": "b" * 64}
    rows = []
    for case_id, cn0, policy, _spool in runner.TWO_HOP_ADDITIVE_BOUNDARY_CASES:
        plan = {"max_wall_seconds": 899.5, "max_samples": 4_350_000,
                "cn0_db_hz": cn0, "affine_gain_evaluation": policy,
                "wire_bits": 13, "scored_slot_horizon": 15, "horizon_samples": 4_300_000,
                "sample_rate_hz": 31_000., "requested_bit_seconds": 128., "actual_bit_seconds": 139.2,
                "message": "E", "requested_message": "E", "fec": "off", "helper_enabled": False,
                "public_bounds": {"lo": 1}, "receiver_resources": {"hardware_threads": 12, "workers": 24,
                    "slot_owners": 24, "workers_explicit": False, "owners_explicit": False,
                    "total_memory_limit_bytes": 8_000_000_000},
                "channel_renderer": "vector_two_hop", "channel_tail_seconds": 5.,
                "seed": 73613, "noise_seed": 93613, "hf_trajectory_seed": None,
                "hf_trajectory_offset_seconds": 0.,
                "public_slots": [{"index": i, "symbol_ordinal": i, "pattern_epoch": 1_793_404_800 + i * 150,
                                  "observation": {"first": i * 100, "stop": (i + 1) * 100}}
                                 for i in range(15)]}
        child = {"status": "completed", "requested_scenario": "hf-vector-two-hop", "scenario": "hf-vector-two-hop",
                 "mode": "plan", "waveform": "reference", "seed": 73613, "control": "wanted",
                 "source_identity": dict(identity), "plan": plan}
        rows.append({"case_id": case_id, "receipt": child})
    receipt = {"status": "completed", "requested_scenario": "hf-vector-two-hop", "mode": "plan",
               "waveform": "reference", "seed": 73613, "control": "wanted", "source_identity": identity,
               "two_hop_additive_boundary": {"planned_rows": 3, "aggregate_wall_limit_seconds": 899.5,
                   "per_row_sample_limit": 4_350_000, "rows": rows}}
    return episode, receipt


def physical_repeat_receipt(bit: int = 0) -> tuple[dict, dict, dict]:
    active = {0, 3, 6, 9, 12}
    q0, horizon, horizon_samples = 13, 15, 1500
    public_slots, slots = [], []
    for index in range(horizon):
        first, stop = index * 100, index * 100 + 90
        quiet_first, quiet_stop = first + 10, stop - 10
        public = {"index": index, "pattern_epoch": 1_793_404_800 + 150 * index,
                  "symbol_ordinal": index, "observation": {"first": first, "stop": stop},
                  "quiet_aperture": {"first": quiet_first, "stop": quiet_stop},
                  "evaluator_transmitting": index in active}
        source = index in active
        public_slots.append(public)
        slots.append({"index": index, "pattern_epoch": public["pattern_epoch"],
                      "symbol_ordinal": index, "first_sample": first, "stop_sample": stop,
                      "quiet_first_sample": quiet_first, "quiet_stop_sample": quiet_stop,
                      "decision": "quiet", "bit": None, "complete": True, "all_valid": True,
                      "expected_bit": bit if source else None,
                      "raw_bit_correct": False if source else None,
                      "noise_statistics_valid": not source, "zero_h0_verified": not source})
    plan = {"repeated_bit": {"bit": bit, "active_slots": 5, "attempts": 5,
              "requested_active_slots": 5, "separate_attempts": True,
              "public_key_fingerprint_sha256": "c" * 64, "used_for_receiver_decisions": False,
              "transmission_scope": "Five one-bit emissions advance the public ordinal. Each has two scored quiet windows through one continuous changing-rate channel."},
            "first_uncontaminated_quiet_slot": q0, "scored_slot_horizon": horizon,
            "sample_rate_hz": 1920, "horizon_samples": horizon_samples, "noise_variance": 1.0,
            "max_samples": 4_350_000, "max_wall_seconds": 179.5,
            "represented_seconds": horizon_samples / 1920, "fits_sample_budget": True,
            "wire_bits": 1, "message": None, "requested_message": None,
            "initial_ordinal": 0, "public_slots": public_slots}
    receipt = {"status": "completed", "plan": plan, "slots": slots, "messages": [],
               "channel": {"samples": horizon_samples, "hashed_samples": horizon_samples,
                           "input_sha256": "d" * 64},
               "outcomes": {"receiver_executed": True, "component_only": True,
                            "completion_evaluated": False, "communication_endpoint": "repeated_raw_bit",
                            "actual_scored_slots": horizon},
               "repeated_bit": {"active_slots": 5, "correct_data": 0, "wrong_data": 0,
                   "ambiguous_active_slots": 0, "quiet_active_slots": 5, "longest_no_correct_run": 5,
                   "trailing_quiet_slots": 2, "physical_quiet_complete": True,
                   "ordered_valid_complete": True, "separate_attempts": True,
                   "inter_attempt_quiet_slots": 8, "completed_quiet_pairs": 5}}
    episode = {"id": "physical-bit", "scenario": "hf-ensemble-changing-rate", "mode": "receive",
               "waveform": "reference", "pulse": "rrc", "backend": "cpu", "seed": 30107,
               "noise_seed": 40107, "hf_trajectory_seed": 20107,
               "hf_trajectory_offset_seconds": 197, "bit_seconds": 128, "cn0_db_hz": 0,
               "control": "wanted", "fec": "off", "helper": False,
               "repeat_bit": bit, "repeat_separate_attempts": True,
               "reception_requirement": "characterization"}
    return receipt, episode, plan


class TwoHopAlarmMatrixRunnerTests(unittest.TestCase):
    def test_frozen_campaign_is_accepted_and_budget_mutations_are_rejected(self) -> None:
        campaign = runner._read_campaign(RUNNER.parent / "campaigns" / "two-hop-long-alarm-matrix.json")
        self.assertEqual(len(campaign["episodes"]), 1)
        for field, value in (("max_wall_seconds", 1201), ("per_trial_wall_seconds", 1201),
                             ("max_samples", 4_350_001)):
            mutated = dict(campaign)
            mutated[field] = value
            path = Path(tempfile.mkstemp(suffix=".json")[1])
            try:
                path.write_text(json.dumps(mutated))
                with self.assertRaises(runner.CampaignError, msg=field):
                    runner._read_campaign(path)
            finally:
                path.unlink(missing_ok=True)
        episode = dict(campaign["episodes"][0], cn0_db_hz=3)
        with self.assertRaises(runner.CampaignError):
            runner._validate_episode(episode, 0, set(), 1200, 4_350_000)

    def test_matrix_plan_binds_order_config_and_calendar(self) -> None:
        episode, receipt = matrix_plan_receipt()
        with patch.object(runner, "_validate_completed_receipt") as child_validator:
            runner._validate_two_hop_alarm_matrix_receipt(receipt, episode)
        self.assertEqual(child_validator.call_count, 6)
        calls = [call.args[1] for call in child_validator.call_args_list]
        self.assertEqual([(item["cn0_db_hz"], item["message"], item.get("two_hop_alarm_matrix_bit"))
                          for item in calls], [(-3, "E", None), (3, "E", None), (-3, None, 0),
                                               (3, None, 0), (-3, None, 1), (3, None, 1)])

    def test_matrix_rejects_row_order_truth_schedule_and_horizon_mutations(self) -> None:
        for mutate in (
            lambda rows: rows.reverse(),
            lambda rows: rows[2]["receipt"]["plan"]["public_slots"][3].update(evaluator_transmitting=False),
            lambda rows: rows[4]["receipt"]["plan"].update(scored_slot_horizon=14),
            lambda rows: rows[5]["receipt"]["plan"]["public_slots"][12].update(symbol_ordinal=11),
        ):
            episode, receipt = matrix_plan_receipt()
            mutate(receipt["two_hop_alarm_matrix"]["rows"])
            with patch.object(runner, "_validate_completed_receipt"):
                with self.assertRaises(runner.CampaignError):
                    runner._validate_two_hop_alarm_matrix_receipt(receipt, episode)

    def test_matrix_rejects_source_identity_and_key_mismatches(self) -> None:
        for target, field, value in ((0, "source_identity", {"appraisal_source_sha256": "d" * 64,
                                                                "native_source_sha256": "b" * 64}),
                                     (2, "public_key_fingerprint_sha256", "d" * 64)):
            episode, receipt = matrix_plan_receipt()
            child = receipt["two_hop_alarm_matrix"]["rows"][target]["receipt"]
            if field == "source_identity":
                child[field] = value
            else:
                child["plan"]["repeated_bit"][field] = value
            with patch.object(runner, "_validate_completed_receipt"):
                with self.assertRaises(runner.CampaignError):
                    runner._validate_two_hop_alarm_matrix_receipt(receipt, episode)

    def test_receive_matrix_accepts_zero_correct_bits_and_checks_shared_table_evidence(self) -> None:
        episode, receipt = matrix_receive_receipt()
        with patch.object(runner, "_validate_completed_receipt") as child_validator, \
             patch.object(runner, "_validate_repeated_bit_receipt") as raw_validator:
            runner._validate_two_hop_alarm_matrix_receipt(receipt, episode, 1199.5)
        self.assertEqual(child_validator.call_count, 6)
        self.assertEqual(raw_validator.call_count, 4)
        for mutate in (
            lambda rows: rows[1]["receipt"]["channel"]["vector_two_hop_table_use"].update(table_reused=False),
            lambda rows: rows[1]["receipt"]["channel"]["vector_two_hop_table_use"].update(table_sha256="f" * 64),
            lambda rows: rows[1]["receipt"]["channel"]["vector_two_hop"].update(selected_energy_sample_sum=3.1),
            lambda rows: rows[0]["receipt"]["slots"][0].pop("likelihood_components"),
        ):
            episode, broken = matrix_receive_receipt()
            mutate(broken["two_hop_alarm_matrix"]["rows"])
            with patch.object(runner, "_validate_completed_receipt"), \
                 patch.object(runner, "_validate_repeated_bit_receipt"):
                with self.assertRaises(runner.CampaignError):
                    runner._validate_two_hop_alarm_matrix_receipt(broken, episode, 1199.5)

    def test_matrix_summary_counts_text_failures_separately_from_quiet_raw_rows(self) -> None:
        episode, receipt = matrix_receive_receipt()
        for entry in receipt["two_hop_alarm_matrix"]["rows"]:
            child = entry["receipt"]
            if entry["case_id"].startswith("text-"):
                child["outcomes"] = {"correct_complete": False, "accepted_messages": 0,
                                     "wrong_bits": 0, "ambiguous_active_slots": 2,
                                     "quiet_active_slots": 3}
            else:
                child["repeated_bit"].update(correct_data=0, wrong_data=0,
                                             ambiguous_active_slots=0, quiet_active_slots=5,
                                             completed_quiet_pairs=5)
        trial = {"episode": episode, "receipt": receipt, "status": "completed", "elapsed_seconds": 1.}
        summary = runner._two_hop_alarm_matrix_campaign_summary([trial])
        matrix = summary["matrices"][0]
        self.assertEqual(matrix["status"], "completed")
        self.assertEqual(matrix["text_counts"], {
            "status": "receipts_complete",
            "scope": matrix["text_counts"]["scope"],
            "completed_rows": 2, "correct_complete_rows": 0,
            "rows_without_completed_receipts": 0, "communication_miss_rows": 2})
        text_rows = [row for row in matrix["rows"] if str(row["case_id"]).startswith("text-")]
        self.assertTrue(all(row["active_slot_erasures"] == 5 for row in text_rows))
        raw_rows = [row for row in matrix["rows"] if "raw_bit" in row]
        self.assertTrue(all(row["raw_bit"]["correct_data"] == 0 and
                            row["raw_bit"]["completed_quiet_pairs"] == 5 for row in raw_rows))
        report = runner._markdown({"name": "matrix-test", "episodes": [episode]}, {
            "campaign_sha256": "a" * 64, "probe_sha256": "b" * 64, "executed_trials": 1,
            "elapsed_seconds": 1., "execution_failures": 0, "valid_communication_failures": 0,
            "status": "completed", "stop_reason": None, "trials": [trial], "statistics": {},
            "repeated_bit_summary": None, "two_hop_alarm_matrix_summary": summary})
        self.assertIn("0/2 correct-complete, 2 message misses, 0 rows without completed receipts", report)
        self.assertIn("| text-weak | -3 | no | 0 | 5 |", report)
        self.assertIn("| bit0-weak | -3 | 0/5 | 0 | 0 |", report)

        incomplete_receipt = json.loads(json.dumps(receipt))
        incomplete_receipt["two_hop_alarm_matrix"]["rows"][1]["receipt"]["status"] = "error"
        incomplete_trial = {"episode": episode, "receipt": incomplete_receipt,
                            "status": "completed", "elapsed_seconds": 1.}
        incomplete = runner._two_hop_alarm_matrix_campaign_summary([incomplete_trial])["matrices"][0]["text_counts"]
        self.assertEqual((incomplete["status"], incomplete["completed_rows"],
                          incomplete["correct_complete_rows"], incomplete["rows_without_completed_receipts"],
                          incomplete["communication_miss_rows"]),
                         ("receipts_incomplete", 1, 0, 1, 1))

    def test_additive_boundary_plan_validates_fixed_order_and_external_parity_scope(self) -> None:
        episode, receipt = additive_boundary_plan_receipt()
        with patch.object(runner, "_validate_completed_receipt") as child_validator:
            runner._validate_two_hop_additive_boundary_receipt(receipt, episode, 899.5)
        self.assertEqual(child_validator.call_count, 3)
        self.assertEqual([call.args[1]["cn0_db_hz"] for call in child_validator.call_args_list], [3, 4, 4])
        for mutate in (
            lambda rows: rows.reverse(),
            lambda rows: rows[0]["receipt"]["plan"].update(affine_gain_evaluation="operational"),
            lambda rows: rows[2]["receipt"]["plan"].update(max_samples=4_350_001),
            lambda rows: rows[1]["receipt"]["source_identity"].update(native_source_sha256="c" * 64),
        ):
            episode, broken = additive_boundary_plan_receipt()
            mutate(broken["two_hop_additive_boundary"]["rows"])
            with patch.object(runner, "_validate_completed_receipt"):
                with self.assertRaises(runner.CampaignError):
                    runner._validate_two_hop_additive_boundary_receipt(broken, episode, 899.5)

    def test_additive_boundary_manifest_and_native_flag_stay_narrow(self) -> None:
        campaign = runner._read_campaign(RUNNER.parent / "campaigns" / "two-hop-additive-boundary.json")
        episode = campaign["episodes"][0]
        command = runner._command(Path("/native/probe"), episode, 4_350_000, 899.5)
        self.assertIn("--two-hop-additive-boundary", command)
        self.assertEqual(command[command.index("--cn0") + 1], "4")
        for changes in ({"cn0_db_hz": 5}, {"max_wall_seconds": 900.1},
                        {"hf_predictive_quiet": True}, {"noise_seed": 93614}):
            invalid = dict(episode, **changes)
            with self.subTest(changes=changes), self.assertRaises(runner.CampaignError):
                runner._validate_episode(invalid, 0, set(), 900, 4_350_000)

    def test_additive_boundary_receive_requires_complete_independently_verified_spool_replays(self) -> None:
        episode, receipt = additive_boundary_plan_receipt()
        episode.update(mode="receive", reception_requirement="characterization")
        receipt["mode"] = "receive"
        boundary = receipt["two_hop_additive_boundary"]
        boundary["wall_seconds"] = 35.
        table = "e" * 64
        capture = "f" * 64
        for index, entry in enumerate(boundary["rows"]):
            child = entry["receipt"]
            child["mode"] = "receive"
            child["plan"]["mode"] = "receive"
            child["timing"] = {"wall_seconds": 10. if index == 0 else 15.,
                               **({"wall_includes_shared_render_per_policy": True} if index else {})}
            child["slots"] = [{"index": i, "complete": True, "all_valid": True,
                               "affine_gain_model": "stationary_plus_ou" if index == 2 else "original"}
                              for i in range(15)]
            child["channel"] = {"vector_two_hop_table_use": {"table_reused": index != 0,
                                  "table_sha256": table, "preparation_seconds": 1. if index == 0 else .1},
                                "vector_two_hop": {"table_sha256": table}}
            if index:
                child["plan"]["service_replay"] = {"planned_spool_bytes": 73_109_724}
                child["service_replay"] = {"work_complete": True, "digest_match": True,
                    "replayed_samples": child["plan"]["horizon_samples"],
                    "captured_input_sha256": capture, "verified_input_sha256": capture,
                    "spool_bytes": 73_109_724, "render_wall_seconds": 5.}
        with patch.object(runner, "_validate_completed_receipt"):
            runner._validate_two_hop_additive_boundary_receipt(receipt, episode, 899.5)
            receipt["two_hop_additive_boundary"]["rows"][1]["receipt"]["service_replay"]["work_complete"] = False
            with self.assertRaises(runner.CampaignError):
                runner._validate_two_hop_additive_boundary_receipt(receipt, episode, 899.5)

    def test_additive_boundary_summary_keeps_four_pair_outcomes_conditional(self) -> None:
        expected = {(False, True): "endpoint_gain", (True, True): "both_recover_no_boundary_separation",
                    (False, False): "neither_recovers", (True, False): "candidate_regression"}
        for pair, name in expected.items():
            episode, receipt = additive_boundary_plan_receipt()
            episode.update(mode="receive", reception_requirement="characterization")
            rows = receipt["two_hop_additive_boundary"]["rows"]
            rows[1]["receipt"]["outcomes"] = {"correct_complete": pair[0]}
            rows[2]["receipt"]["outcomes"] = {"correct_complete": pair[1]}
            summary = runner._two_hop_additive_boundary_campaign_summary([{
                "episode": episode, "receipt": receipt, "status": "completed"}])
            comparison = summary["comparisons"][0]
            self.assertEqual(comparison["observed_plus4_pair_outcome"], name)
            self.assertEqual(comparison["interpretation_status"], "pending_external_plus3_historical_parity")

    def test_physical_raw_bit_validator_accepts_five_ordered_valid_misses(self) -> None:
        receipt, episode, plan = physical_repeat_receipt()
        runner._validate_physical_raw_bit_plan(plan, episode,
            native_max_wall_seconds=179.5, native_max_samples=4_350_000)
        runner._validate_repeated_bit_receipt(receipt, episode, plan,
                                              physical_attempt_count=5, expected_bit=0)
        with self.assertRaises(runner.CampaignError):
            runner._validate_physical_raw_bit_plan(plan, episode,
                native_max_wall_seconds=179.4, native_max_samples=4_350_000)
        components = {"scope": "Public candidate-averaged log evidence before global coordinate and branch priors.",
                      "fixed_affine": [None, 0.], "short_varying": None, "short_fixed_gain": [1., 2.],
                      "short_frequency_track": None, "medium_coherent_gain": None,
                      "linear_drift": None, "slow_gain_coherence_seconds": [None, None],
                      "affine_selected_prior_mass": None, "short_frequency_log_prior_adjustment": 0.}
        receipt["slots"] = [{"likelihood_components": dict(components)} for _ in range(15)]
        runner._validate_public_likelihood_components(receipt, 15)
        receipt["slots"][0]["likelihood_components"]["fixed_affine"] = [float("nan"), 0.]
        with self.assertRaises(runner.CampaignError):
            runner._validate_public_likelihood_components(receipt, 15)

    def test_frozen_physical_raw_campaign_and_native_flags(self) -> None:
        campaign_path = RUNNER.parent / "campaigns/physical-random-long-alarm.json"
        campaign = runner._read_campaign(campaign_path)
        self.assertEqual({item["repeat_bit"] for item in campaign["episodes"]}, {0, 1})
        for episode in campaign["episodes"]:
            runner._validate_episode(episode, 0, set(), 180, 4_350_000)
            command = runner._command(Path("/native/probe"), episode, 4_350_000, 180)
            self.assertIn("--repeat-bit", command)
            self.assertIn("--repeat-separate-attempts", command)
            self.assertNotIn("--repeat-path-scale", command)
        base = dict(campaign["episodes"][0])
        for changes in ({"seed": 30108}, {"hf_trajectory_offset_seconds": 198},
                        {"hf_phase_refinement": True}, {"max_wall_seconds": 180.01},
                        {"max_samples": 4_350_001}, {"repeat_separate_attempts": False}):
            invalid = dict(base, **changes)
            with self.subTest(changes=changes), self.assertRaises(runner.CampaignError):
                runner._validate_episode(invalid, 0, set(), 180, 4_350_000)

    def test_physical_raw_reporting_does_not_count_text_success_or_failure(self) -> None:
        trials = []
        for bit in (0, 1):
            receipt, episode, _plan = physical_repeat_receipt(bit)
            episode.update(control="wanted", reception_requirement="characterization")
            for index in (0, 3, 6, 9, 12):
                slot = receipt["slots"][index]
                slot.update(decision="data", bit=bit, raw_bit_correct=True)
            receipt["repeated_bit"].update(correct_data=5, quiet_active_slots=0,
                                           longest_no_correct_run=0)
            trials.append({"episode": episode, "receipt": receipt, "status": "completed"})
        self.assertEqual(runner._valid_communication_failure_count(trials), 0)
        self.assertTrue(all(runner._communication_outcome(trial["receipt"], trial["episode"]) is None
                            for trial in trials))
        self.assertTrue(all(runner._wanted_outcome_counts(trial) is None for trial in trials))
        self.assertEqual(runner._wanted_outcome_summary(trials)["completed_trials"], 0)
        summary = runner._repeated_bit_campaign_summary(trials)
        self.assertTrue(summary["physical_scenario"])
        self.assertIn("trajectory", summary["scope"])
        self.assertEqual([row["correct_data"] for row in summary["rows"]], [5, 5])
        self.assertTrue(all(row["completed_quiet_pairs"] == 5 for row in summary["rows"]))
        self.assertTrue(all(row["source_scope"] == "continuous changing-rate HF channel" for row in summary["rows"]))
        self.assertTrue(all("repeat_path_scale" not in row for row in summary["rows"]))

        missed_receipt, missed_episode, _plan = physical_repeat_receipt(0)
        missed_trial = {"episode": missed_episode, "receipt": missed_receipt, "status": "completed",
                        "elapsed_seconds": 1.0}
        missed_summary = runner._repeated_bit_summary(missed_receipt, missed_episode)
        self.assertEqual((missed_summary["correct_data"], missed_summary["longest_no_correct_run"],
                          missed_summary["completed_quiet_pairs"]), (0, 5, 5))
        report = runner._markdown({"name": "raw-test", "episodes": [missed_episode]}, {
            "campaign_sha256": "a" * 64, "probe_sha256": "b" * 64, "executed_trials": 1,
            "elapsed_seconds": 1., "execution_failures": 0, "valid_communication_failures": 0,
            "status": "completed", "stop_reason": None, "trials": [missed_trial], "statistics": {},
            "repeated_bit_summary": runner._repeated_bit_campaign_summary([missed_trial]),
            "two_hop_alarm_matrix_summary": None})
        self.assertIn("raw hf-ensemble-changing-rate bit 0: correct=0/5", report)
        self.assertIn("quiet pairs=5/5; text completion not evaluated", report)
        self.assertNotIn("no correct physical message completion", report)


class HfDisturbedRawBitRunnerTests(unittest.TestCase):
    def test_hf_disturbed_campaign_admits_only_the_fixed_matched_pair(self) -> None:
        path = RUNNER.parent / "campaigns" / "hf-disturbed-long-alarm.json"
        campaign = runner._read_campaign(path)
        self.assertEqual([episode["repeat_bit"] for episode in campaign["episodes"]], [0, 1])
        self.assertEqual({episode["seed"] for episode in campaign["episodes"]}, {9202})
        for episode in campaign["episodes"]:
            runner._validate_episode(episode, 0, set(), 180, 4_350_000)
            command = runner._command(Path("/native/probe"), episode, 4_350_000, 179.5)
            self.assertIn("--repeat-separate-attempts", command)
            self.assertIn("--chunk", command)
            self.assertEqual(command[command.index("--chunk") + 1], "4096")
            self.assertEqual(command[command.index("--seed") + 1], "9202")
            self.assertNotIn("--noise-seed", command)
            self.assertNotIn("--hf-trajectory-seed", command)

        base = dict(campaign["episodes"][0])
        for changes in ({"cn0_db_hz": 11.99}, {"seed": 9203}, {"noise_seed": 9202},
                        {"chunk": 4097}, {"helper": True}, {"fec": "rs20"},
                        {"hf_phase_refinement": True}, {"max_wall_seconds": 180.01},
                        {"max_samples": 4_350_001}, {"repeat_separate_attempts": False}):
            invalid = dict(base, **changes)
            with self.subTest(changes=changes), self.assertRaises(runner.CampaignError):
                runner._validate_episode(invalid, 0, set(), 180, 4_350_000)
        unpaired = json.loads(path.read_text(encoding="utf-8"))
        unpaired["episodes"][1]["seed"] = 9203
        with tempfile.TemporaryDirectory() as temporary:
            candidate = Path(temporary) / "unpaired.json"
            candidate.write_text(json.dumps(unpaired), encoding="utf-8")
            with self.assertRaises(runner.CampaignError):
                runner._read_campaign(candidate)

    def test_hf_disturbed_raw_receipt_accepts_valid_misses_but_binds_profile(self) -> None:
        receipt, episode, plan = physical_repeat_receipt(0)
        episode.update(scenario="hf-disturbed", seed=9202, noise_seed=None,
                       hf_trajectory_seed=None, hf_trajectory_offset_seconds=None,
                       cn0_db_hz=12, chunk=4096, bit_seconds=128,
                       max_wall_seconds=180, max_samples=4_350_000)
        episode.pop("noise_seed")
        plan["repeated_bit"]["transmission_scope"] = (
            "Five one-bit emissions advance the public ordinal. Each has two scored quiet windows "
            "through one continuous standard Gaussian hf-disturbed channel.")
        runner._validate_physical_raw_bit_plan(plan, episode, native_max_wall_seconds=179.5,
                                                native_max_samples=4_350_000)
        runner._validate_repeated_bit_receipt(receipt, episode, plan,
                                               physical_attempt_count=5, expected_bit=0)
        summary = runner._repeated_bit_summary(receipt, episode)
        self.assertEqual(summary["correct_data"], 0)
        self.assertEqual(summary["completed_quiet_pairs"], 5)
        self.assertEqual(summary["source_scope"], "continuous hf-disturbed standard Gaussian channel")

        wrong_channel = json.loads(json.dumps(plan))
        wrong_channel["repeated_bit"]["transmission_scope"] = wrong_channel["repeated_bit"]["transmission_scope"].replace(
            "standard Gaussian hf-disturbed", "changing-rate")
        with self.assertRaises(runner.CampaignError):
            runner._validate_physical_raw_bit_plan(wrong_channel, episode,
                native_max_wall_seconds=179.5, native_max_samples=4_350_000)
        with self.assertRaises(runner.CampaignError):
            runner._validate_episode(dict(episode, noise_seed=9202), 0, set(), 180, 4_350_000)

    def test_fresh_history_campaign_is_only_the_matched_seed_9601_pair(self) -> None:
        path = RUNNER.parent / "campaigns" / "hf-disturbed-long-alarm-fresh-history.json"
        campaign = runner._read_campaign(path)
        self.assertEqual([episode["repeat_bit"] for episode in campaign["episodes"]], [0, 1])
        self.assertEqual({episode["seed"] for episode in campaign["episodes"]}, {9601})
        for episode in campaign["episodes"]:
            runner._validate_episode(episode, 0, set(), 180, 4_350_000)
            command = runner._command(Path("/native/probe"), episode, 4_350_000, 179.5)
            self.assertEqual(command[command.index("--seed") + 1], "9601")
            self.assertNotIn("--noise-seed", command)

        for changes in ({"seed": 9203}, {"noise_seed": 9601}, {"cn0_db_hz": 18},
                        {"chunk": 2048}, {"max_wall_seconds": 180.01}):
            invalid = dict(campaign["episodes"][0], **changes)
            with self.subTest(changes=changes), self.assertRaises(runner.CampaignError):
                runner._validate_episode(invalid, 0, set(), 180, 4_350_000)
        unpaired = json.loads(path.read_text(encoding="utf-8"))
        unpaired["episodes"][1]["seed"] = 9202
        with tempfile.TemporaryDirectory() as temporary:
            candidate = Path(temporary) / "unpaired.json"
            candidate.write_text(json.dumps(unpaired), encoding="utf-8")
            with self.assertRaises(runner.CampaignError):
                runner._read_campaign(candidate)

    def test_post_pair_noise_control_is_one_unfiltered_ordinary_receive(self) -> None:
        path = RUNNER.parent / "campaigns" / "hf-disturbed-long-alarm-noise-control.json"
        campaign = runner._read_campaign(path)
        self.assertEqual((campaign["max_trials"], campaign["max_wall_seconds"],
                          campaign["per_trial_wall_seconds"], campaign["max_samples"]),
                         (1, 180, 180, 4_350_000))
        episode = campaign["episodes"][0]
        runner._validate_episode(episode, 0, set(), 180, 4_350_000)
        self.assertEqual((episode["scenario"], episode["seed"], episode["cn0_db_hz"],
                          episode["bit_seconds"], episode["message"], episode["control"]),
                         ("hf-disturbed", 9602, 12, 128, "e", "noise"))
        command = runner._command(Path("/native/probe"), episode, 4_350_000, 179.5)
        self.assertIn("--control", command)
        self.assertEqual(command[command.index("--control") + 1], "noise")
        self.assertIn("--message", command)
        self.assertEqual(command[command.index("--message") + 1], "e")
        self.assertFalse(any(argument.startswith("--repeat-") for argument in command))
        for changes in ({"repeat_bit": 0},
                        {"max_samples": 4_350_001}, {"max_wall_seconds": 180.01}):
            invalid = dict(episode, **changes)
            with self.subTest(changes=changes), self.assertRaises(runner.CampaignError):
                runner._validate_episode(invalid, 0, set(), 180, 4_350_000)

if __name__ == "__main__":
    unittest.main()
