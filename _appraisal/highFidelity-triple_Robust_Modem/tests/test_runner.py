#!/usr/bin/env python3
"""Focused bounded-runner tests; fake subprocesses never implement channel DSP."""
from __future__ import annotations

import importlib.util
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
            "selected_log_prior_mass": -math.log(4), "public_grid_log_count": math.log(4),
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
                "observation": {"first": 20, "stop": 100}, "evaluator_transmitting": receipt["control"] == "wanted"}])
        receipt["plan"]["public_bounds"].update(frequency_bound_hz=20., atmospheric_hz=10., clock_bound_ppm=300.)
        receipt["hf_gui_varying_diagnostic"] = fake_gui_varying_diagnostic(selected, receipt["plan"]["public_bounds"])
        receipt["outcomes"] = {"observation_scope": scope, "receiver_executed": False, "component_only": True,
            "completion_evaluated": False, "correct_complete": False, "correct_decoded_complete": False,
            "public_search_executed": False, "actual_scored_slots": 0, "physical_completions": 0,
            "accepted_messages": 0, "premature_completions": 0, "wrong_bits": 0, "false_data": 0}
        receipt["slots"], receipt["messages"] = [], []
        receipt["channel"] = {"samples": 1000, "hashed_samples": 1000, "input_sha256": "f" * 64}
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


class CampaignValidationTests(unittest.TestCase):
    def test_campaigns_are_finite_and_well_formed(self) -> None:
        for path in sorted((RUNNER.parent / "campaigns").glob("*.json")):
            campaign = runner._read_campaign(path)
            self.assertLessEqual(len(campaign["episodes"]), campaign["max_trials"])
            self.assertGreaterEqual(campaign["max_wall_seconds"], 1)

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

    def test_gui_varying_inputs_require_explicit_cpu_component_scope(self) -> None:
        episode = gui_varying_episode()
        for control in ("wanted", "noise"):
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
                {"control": "wrong-key"}, {"control": "wrong-ordinal"}, {"control": "eof"},
                {"control": "interior-fade"}, {"capture_digest_slot": True}, {"capture_digest_slot": -1},
                {"search_slot": 2}, {"search_timing": "full"}, {"oracle_slot": 2}, {"duration_seconds": 1},
                {"hf_public_phase_receiver": False}, {"hf_phase_refinement": False}, {"pulse": "tapered-rrc"})]:
            with self.subTest(invalid=invalid), self.assertRaises(runner.CampaignError):
                runner._validate_episode(invalid, 0, set(), 10, 1000)

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

if __name__ == "__main__":
    unittest.main()
