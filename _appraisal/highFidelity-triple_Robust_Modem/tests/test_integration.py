#!/usr/bin/env python3
"""Bounded actual native source/channel/complete-receiver integration checks."""
from __future__ import annotations

import importlib.util
import json
import math
from pathlib import Path
import subprocess
import sys


def main() -> None:
    probe = sys.argv[1]

    def invoke(*args: str, code: int = 0, timeout: float = 35) -> dict:
        command = [probe, *args]
        run = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
        assert run.returncode == code, (command, run.returncode, run.stdout, run.stderr)
        result = json.loads(run.stdout)
        assert result["schema_version"] == 1, result
        return result

    listing = invoke("--list")
    assert len(listing["scenarios"]) >= 6
    scenarios = {scenario["id"]: scenario for scenario in listing["scenarios"]}
    assert scenarios["hf-gui-native"]["channel"]["renderer"] == "canonical_gui_streaming_channel"
    fullwave_args = ["--scenario", "hf-fullwave-reflection", "--bit-seconds", "64", "--message", "A"]
    fullwave = invoke("--mode", "plan", *fullwave_args)
    wave_plan = fullwave["plan"]
    reflection = wave_plan["fullwave_reflection"]
    assert reflection == scenarios["hf-fullwave-reflection"]["channel"]["fullwave_reflection"], fullwave
    assert wave_plan["channel_renderer"] == "hf_fullwave_reflection", fullwave
    assert wave_plan["fullwave_reflection_source_band_checked"] is False, fullwave
    assert wave_plan["fullwave_reflection_table_built"] is False, fullwave
    assert reflection["modelling_delay_samples"] == 32 and reflection["maximum_rf_solves"] == 32070, fullwave
    assert reflection["first_lag_samples"] == 1 and reflection["last_lag_samples"] == 129, fullwave
    assert wave_plan["public_bounds"]["receiver_filter_halo_samples"] == 128, fullwave
    assert wave_plan["public_bounds"]["arrival_low_seconds"] == 1 / 1920, fullwave
    assert wave_plan["public_bounds"]["arrival_high_seconds"] == 129 / 1920, fullwave
    assert math.isclose(wave_plan["channel_tail_seconds"], 129 / 1920), fullwave
    assert wave_plan["wire_bits"] == 13 and wave_plan["source_stop_sample"] >= (540 + .25) * 1920, fullwave
    first_quiet = wave_plan["first_uncontaminated_quiet_slot"]
    assert wave_plan["public_slots"][first_quiet]["observation"]["first"] >= wave_plan["source_stop_sample"] + 129, fullwave
    assert wave_plan["scored_slot_horizon"] == first_quiet + 2, fullwave
    truncated = invoke("--mode", "channel", *fullwave_args, "--max-samples", "1", code=3)
    assert truncated["plan"]["horizon_samples"] == wave_plan["horizon_samples"], truncated
    assert truncated["plan"]["fullwave_reflection_table_built"] is False and "channel" not in truncated, truncated
    noise = invoke("--mode", "receive", *fullwave_args, "--control", "noise", "--max-samples", "1", code=3)
    for field in ("horizon_samples", "public_bounds", "source_stop_sample"):
        assert noise["plan"][field] == wave_plan[field], (field, noise, wave_plan)
    assert not any(row["evaluator_transmitting"] for row in noise["plan"]["public_slots"]), noise
    assert noise["plan"]["fullwave_reflection_table_built"] is False and "channel" not in noise, noise
    # Requested bit duration alone does not determine incident source span.
    # Thirteen DATA slots still span the profile transition on the public
    # minute cadence even when each slot's waveform is much shorter.
    sparse = invoke("--mode", "plan", "--scenario", "hf-fullwave-reflection",
                    "--bit-seconds", "1", "--message", "A")["plan"]
    assert sparse["actual_bit_seconds"] < wave_plan["actual_bit_seconds"], sparse
    assert sparse["wire_bits"] == 13 and sparse["source_stop_sample"] >= (540 + .25) * 1920, sparse
    short_args = ["--mode", "plan", "--bit-seconds", "1", "--message", "e"]
    short_plan = invoke(*short_args, "--scenario", "awgn")["plan"]
    assert short_plan["wire_bits"] == 3 and short_plan["source_stop_sample"] < (540 + .25) * 1920, short_plan
    short = invoke(*short_args, "--scenario", "hf-fullwave-reflection", code=5)
    assert short["status"] == "unavailable" and "source must span" in short["reason"], short
    for extra in (["--helper"], ["--duration-seconds", "600"]):
        rejected = invoke("--mode", "plan", *fullwave_args, *extra, code=5)
        assert rejected["status"] == "unavailable", rejected
    gui_args = ["--scenario", "hf-gui-native", "--seed", "41", "--noise-seed", "52",
                "--gui-spread", "hf-worst", "--gui-selective", "each-65", "--gui-clock-edge",
                "--bit-seconds", "2", "--cn0", "40", "--gui-target-cn0-db-hz", "32"]
    gui = invoke("--mode", "plan", *gui_args)["plan"]
    declared = gui["gui_profile"]
    assert gui["channel_renderer"] == "canonical_gui_streaming_channel", gui
    assert declared["profile_source"] == "canonical_prepare_profile" and not declared["realization_passed_to_receiver"], gui
    assert declared["selective_slow_depth"] == declared["selective_fast_depth"] == .65, gui
    assert declared["spread_doppler_hz"] == 90 and math.isclose(declared["spread_delay_seconds"], .021), gui
    assert declared["endpoint_clock_bound_ppm"] == 150 and declared["realized"]["tx_audio_ppm"] == 50, gui
    assert declared["realized"]["rx_audio_ppm"] == -50 and declared["realized"]["tx_rf_ppm"] == .5, gui
    assert declared["actual_cn0_db_hz"] == 40 and declared["target_cn0_db_hz"] == 32, gui
    assert declared["fixture_bit_seconds"] == gui["requested_bit_seconds"] == 2, gui
    assert gui["capture_preroll_seconds"] == declared["capture_preroll_seconds"] == .2525, gui
    assert gui["receiver_resources"]["workers"] == 2 * gui["receiver_resources"]["hardware_threads"], gui
    gui_other_noise = invoke("--mode", "plan", *gui_args, "--noise-seed", "53")["plan"]
    assert gui_other_noise["gui_profile"]["realized"] == declared["realized"], gui_other_noise
    assert gui_other_noise["noise_seed"] == 53 and gui_other_noise["public_slots"] == gui["public_slots"], gui_other_noise
    gui_too_short = invoke("--mode", "receive", *gui_args, "--max-samples", "1", code=3)
    assert gui_too_short["status"] == "resource_limit" and gui_too_short["plan"]["channel_renderer"] == gui["channel_renderer"], gui_too_short
    gui_varying_args = ["--mode", "gui-varying", *gui_args, "--message", "e"]
    for extra in ([], ["--capture-digest-slot", "3"], ["--capture-digest-slot", "2", "--helper"]):
        rejected = invoke(*gui_varying_args, *extra, code=2)
        assert rejected["status"] == "error", rejected
    bounded_shadow = invoke(*gui_varying_args, "--capture-digest-slot", "2", "--control", "noise",
                            "--max-samples", "1", code=3)
    assert bounded_shadow["status"] == "resource_limit", bounded_shadow
    shadow_plan = bounded_shadow["plan"]
    receive_plan = invoke("--mode", "receive", *gui_args, "--message", "e",
                          "--capture-digest-slot", "2", "--control", "noise", "--max-samples", "1", code=3)["plan"]
    for field in ("horizon_samples", "public_slots", "public_bounds", "capture_digest_first_sample",
                  "capture_digest_stop_sample"):
        assert shadow_plan[field] == receive_plan[field], (field, bounded_shadow, receive_plan)
    assert shadow_plan["observation_scope"] == "shadow_canonical_varying_of_selected_complete_observation"
    assert shadow_plan["planned_uncontaminated_quiet_slots"] == 0
    unavailable = invoke("--mode", "slot", *gui_args, "--hf-public-phase-receiver", code=5)
    assert "phase scorer is unavailable" in unavailable["reason"], unavailable
    for args in (['--scenario', 'awgn', '--gui-margin', '3'],
                 [*gui_args, '--capture-lead-seconds', '.25'],
                 [*gui_args, '--gui-margin', '.9']):
        rejected = invoke("--mode", "plan", *args, code=2)
        assert rejected["status"] == "error", rejected
    design_args = ["--mode", "plan", "--scenario", "hf-ensemble-design", "--seed", "17",
                   "--hf-trajectory-seed", "27", "--hf-trajectory-offset-seconds", "19",
                   "--hf-primary-amplitude-hz", "8000", "--hf-primary-period-seconds", "180",
                   "--hf-primary-phase-radians", ".5", "--hf-secondary-amplitude-hz", "12000",
                   "--hf-secondary-period-seconds", "820", "--hf-secondary-phase-radians", "-.7",
                   "--hf-knot-amplitude-hz", "6000", "--hf-knot-seconds", "105"]
    design = invoke(*design_args)
    assert design["plan"]["hf_design"]["primary_amplitude_hz"] == 8000, design
    assert design["plan"]["hf_design_domain"]["bounds_are_independent_of_realization"], design
    different = invoke(*design_args, "--hf-primary-amplitude-hz", "18000")
    assert different["plan"]["public_bounds"] == design["plan"]["public_bounds"], different
    assert different["scenario"]["channel"]["path_parameters_sha256"] != design["scenario"]["channel"]["path_parameters_sha256"], different
    for args in (design_args[:-2], [*design_args, "--hf-primary-amplitude-hz", "20001"]):
        rejected = invoke(*args, code=2)
        assert rejected["status"] == "error", rejected
    assert scenarios["awgn"]["channel"]["path_hash_version"] == 1, scenarios["awgn"]
    ox = scenarios["hf-ox-cancellation"]["channel"]
    assert ox["path_hash_version"] == 2, ox
    assert all(path["hf"]["polarization"] is not None for path in ox["paths"]), ox
    assert ox["paths"][0]["hf"]["polarization"]["extraordinary"] is False, ox
    assert ox["paths"][1]["hf"]["polarization"]["extraordinary"] is True, ox
    assert any(path["hf"]["voltage_transitions"] for path in
               scenarios["hf-mode-transition"]["channel"]["paths"]), scenarios["hf-mode-transition"]
    eikonal = scenarios["hf-vertical-eikonal-motion"]["channel"]["paths"][0]["hf"]["parabolic_layer"]
    assert eikonal["critical_amplitude_hz"] > 0, eikonal
    # New physical laws have a separate hash version; unchanged old controls
    # retain their original hash versions and parameter encoding.
    assert scenarios["hf-vertical-eikonal-motion"]["channel"]["path_hash_version"] == 2
    assert scenarios["hf-vertical-eikonal-holdout"]["channel"]["path_hash_version"] == 3
    ensemble = scenarios["hf-ensemble-changing-rate"]["channel"]
    assert ensemble["path_hash_version"] == 4, ensemble
    law = ensemble["paths"][0]["hf"]["parabolic_layer"]
    assert law["critical_law_version"] == 1 and len(law["critical_sines"]) == 1, law
    assert law["critical_knots"]["knot_seconds"] == 113, law
    ensemble_args = ["--scenario", "hf-ensemble-changing-rate", "--waveform", "smoke", "--seed", "31"]
    default_ensemble = invoke("--mode", "plan", *ensemble_args)
    shifted_ensemble = invoke("--mode", "plan", *ensemble_args, "--hf-trajectory-offset-seconds", "83",
                              "--hf-trajectory-seed", "18446744073709551615", "--noise-seed", "41")
    ep = shifted_ensemble["plan"]
    assert ep["hf_trajectory_seed"] == 18446744073709551615 and ep["hf_trajectory_seed_override"], ep
    assert ep["hf_trajectory_offset_seconds"] == 83 and ep["hf_trajectory_offset_override"], ep
    assert ep["noise_seed"] == 41 and ep["noise_seed_override"], ep
    assert default_ensemble["plan"]["noise_seed"] == 31 and not default_ensemble["plan"]["noise_seed_override"], default_ensemble
    for field in ("public_bounds", "public_slots", "source_stop_sample", "epoch", "initial_ordinal",
                  "capture_preroll_seconds", "actual_bit_seconds"):
        assert ep[field] == default_ensemble["plan"][field], (field, ep, default_ensemble["plan"])
    assert shifted_ensemble["scenario"]["channel"]["path_parameters_sha256"] != ensemble["path_parameters_sha256"], shifted_ensemble
    for offset in ("-86400", "86400"):
        boundary = invoke("--mode", "plan", *ensemble_args, "--hf-trajectory-offset-seconds", offset)
        assert boundary["plan"]["hf_trajectory_offset_seconds"] == float(offset), boundary
    for args in (["--scenario", "awgn", "--hf-trajectory-seed", "1"],
                 ["--scenario", "hf-ensemble-tid", "--hf-trajectory-seed", "1"],
                 ["--scenario", "hf-ding-measured", "--hf-trajectory-offset-seconds", "1"],
                 ["--scenario", "awgn", "--hf-trajectory-offset-seconds", "0"],
                 ["--scenario", "hf-ensemble-tid", "--hf-trajectory-offset-seconds", "86401"],
                 ["--scenario", "hf-ensemble-tid", "--hf-trajectory-offset-seconds", "nan"],
                 ["--noise-seed", "18446744073709551616"]):
        rejected = invoke("--mode", "plan", *args, code=2)
        assert rejected["status"] == "error", rejected
    unavailable_helper = invoke("--mode", "plan", *ensemble_args, "--helper", code=5)
    assert "dispersion bandwidth" in unavailable_helper["reason"], unavailable_helper
    helper_interaction = invoke("--mode", "plan", "--scenario", "hf-ensemble-ox-helper", "--helper")
    assert helper_interaction["plan"]["helper_enabled"], helper_interaction
    # Zero-signal samples isolate the additive-noise stream: physical launch,
    # trajectory and stochastic-path seed changes cannot resample that noise.
    noise_a = invoke("--mode", "channel", *ensemble_args, "--control", "noise", "--noise-seed", "41", "--duration-seconds", "1")
    noise_b = invoke("--mode", "channel", *ensemble_args, "--control", "noise", "--noise-seed", "41", "--duration-seconds", "1",
                     "--seed", "32", "--hf-trajectory-seed", "73", "--hf-trajectory-offset-seconds", "-83")
    assert noise_a["channel"]["input_sha256"] == noise_b["channel"]["input_sha256"], (noise_a, noise_b)
    traces = [scenarios[name]["channel"]["paths"][0]["hf"]["measured_envelope"]
              for name in ("hf-ding-measured", "hf-ding-amplitude", "hf-ding-phase")]
    assert [trace["part"] for trace in traces] == ["measured-complex", "constructed-amplitude-only", "constructed-phase-only"], traces
    for trace in traces:
        assert trace["samples"] == 2048 and trace["sample_rate_hz"] == 50, trace
        assert trace["support_seconds"] == (trace["samples"] - 1) / trace["sample_rate_hz"], trace
        assert len(trace["source_sha256"]) == len(trace["serialized_iq_sha256"]) == 64, trace
    assert len({scenarios[name]["channel"]["path_parameters_sha256"] for name in
                ("hf-ding-measured", "hf-ding-amplitude", "hf-ding-phase")}) == 3, traces
    late_trace = scenarios["hf-ding-measured-late"]["channel"]["paths"][0]["hf"]["measured_envelope"]
    assert late_trace["record_offset_seconds"] == 20, late_trace
    assert late_trace["remaining_support_seconds"] == late_trace["support_seconds"] - 20, late_trace
    clock_trace = scenarios["hf-ding-clock41"]["channel"]["paths"][0]["hf"]["measured_envelope"]
    assert math.isclose(clock_trace["sample_rate_hz"], 2048 / 41, rel_tol=1e-15), clock_trace
    assert clock_trace["source_sha256"] == traces[0]["source_sha256"], clock_trace
    measured_helper = invoke("--mode", "plan", "--scenario", "hf-ding-measured", "--helper", code=5)
    assert measured_helper["status"] == "unavailable" and "Helper-off" in measured_helper["reason"], measured_helper
    measured_plan = invoke("--mode", "plan", "--scenario", "hf-ding-measured", "--waveform", "reference")["plan"]
    first_stop = measured_plan["public_slots"][0]["observation"]["stop"] / measured_plan["sample_rate_hz"]
    assert first_stop <= measured_plan["numerical_horizon_seconds"], measured_plan
    assert not measured_plan["fits_numerical_horizon"], measured_plan
    measured_full = invoke("--mode", "receive", "--scenario", "hf-ding-measured", "--waveform", "reference", code=5)
    assert measured_full["status"] == "unavailable" and "numerical qualification" in measured_full["reason"], measured_full
    measured_noise = invoke("--mode", "slot", "--scenario", "hf-ding-measured", "--waveform", "reference",
                            "--control", "noise", "--seed", "7201", "--cn0", "0", "--max-wall-seconds", "25")
    noise_outcomes = measured_noise["outcomes"]
    assert measured_noise["status"] == "completed" and noise_outcomes["first_public_observation_complete"], measured_noise
    assert not noise_outcomes["first_public_observation_correct_data"], measured_noise
    assert not noise_outcomes["correct_complete"] and not noise_outcomes["completion_evaluated"], measured_noise
    assert noise_outcomes["actual_scored_slots"] == 1 and noise_outcomes["physical_completions"] == 0, measured_noise
    assert noise_outcomes["false_data"] == int(measured_noise["slots"][0]["decision"] == "data"), measured_noise
    assert not measured_noise["channel"]["contains_source_energy"] and not measured_noise["channel"]["contains_signal_energy"], measured_noise
    rejected_slot_control = invoke("--mode", "slot", "--control", "eof", code=2)
    assert "wanted or noise" in rejected_slot_control["reason"], rejected_slot_control
    # Public-search shadow mode receives the selected aperture and receiver
    # key only. Its evaluator must not manufacture native receiver decisions.
    search = invoke("--mode", "hf-search", "--scenario", "awgn", "--waveform", "smoke",
                    "--search-slot", "1", "--cn0", "40", "--max-wall-seconds", "25")
    search_outcomes = search["outcomes"]
    assert search["hf_public_search"]["component_complete"] and search_outcomes["public_search_executed"], search
    assert not search_outcomes["receiver_executed"] and search_outcomes["component_only"], search
    assert not search_outcomes["completion_evaluated"] and not search_outcomes["correct_complete"], search
    assert search["slots"] == search["messages"] == [] and search_outcomes["actual_scored_slots"] == 0, search
    assert search["plan"]["search_slot"] == search["hf_search_evaluation"]["slot_index"] == 1, search
    observation = search["plan"]["search_observation"]
    assert observation["first"] > 0 and observation["stop"] == search["plan"]["horizon_samples"], search
    assert len(search["plan"]["search_observation_sha256"]) == 64, search
    families = search["hf_public_search"]["families"]
    assert {family["name"] for family in families} == {"constant_frequency", "linear_frequency_drift", "sinusoidal_phase"}, search
    for family in families:
        assert family["favored_bit"] == search["hf_search_evaluation"]["expected_receiver_bit"], search
        assert family["passes_presence_reference"] and family["passes_bit_separation_reference"], search
        assert family["bit_log_separation"] >= 5.29, search
    assert search["channel"]["selected_observation_sha256"] == search["plan"]["search_observation_sha256"], search
    physical_search = invoke("--mode", "hf-search", "--scenario", "awgn", "--waveform", "smoke",
                             "--search-slot", "1", "--cn0", "40", "--max-wall-seconds", "25",
                             "--hf-physical-diagnostic", "--chunk", "1021")
    physical = physical_search["hf_physical_diagnostic"]
    assert physical_search["channel"]["selected_observation_sha256"] == search["channel"]["selected_observation_sha256"], physical_search
    assert [family["bit_log_e"] for family in physical_search["hf_public_search"]["families"]] == [family["bit_log_e"] for family in families], physical_search
    assert physical["component_complete"] and not physical["truth_information_passed_to_search"], physical_search
    assert not physical["is_theoretical_upper_bound"] and not physical["receiver_executed"], physical_search
    assert physical["prefix_samples_rendered_per_twin"] == observation["stop"], physical_search
    known = physical["physical_templates"]
    assert known["block_count"] == 1 and known["noise_free_energy"] > 0, physical_search
    assert math.isclose(known["hypotheses"][known["wanted_bit"]]["clean_coherence_power"], 1, abs_tol=1e-12), physical_search
    binary = physical["known_binary"]
    assert binary["squared_distance"] > 0 and binary["distance_over_noise_variance"] > 0, binary
    assert binary["observed_correct_vs_wrong_log_likelihood_ratio"] > 0, binary
    assert math.isclose(binary["distance_over_noise_variance"], binary["squared_distance"] / physical_search["plan"]["noise_variance"], rel_tol=1e-14), binary
    assert math.isclose(binary["equal_prior_bit_error_probability"], .5 * math.erfc(math.sqrt(binary["distance_over_noise_variance"] / 4)), rel_tol=1e-14), binary
    assert binary["fixed_known_context"] and not binary["truth_information_passed_to_search"], binary
    assert not binary["includes_acquisition_presence_or_completion"], binary
    winner = physical["winning_public_cell"]
    partitions = physical["gain_partitions"]
    assert not partitions["used_for_receiver_decisions"] and partitions["template_selected_from_noisy_public_search"], partitions
    assert not partitions["fixed_template_null_law_applies_after_selection"], partitions
    assert partitions["refined_public_cell"] is None, partitions
    for name, full in (("physical_templates", known), ("winning_public_cell", winner["projection"])):
        rows = partitions[name]
        assert [row["requested_block_seconds"] for row in rows] == [.25, 1, 4, 16, None], partitions
        for row, hypothesis in zip(rows[-1]["hypotheses"], full["hypotheses"], strict=True):
            assert row["reference_energy"] == hypothesis["template_energy"], partitions
            assert row["noisy_log_likelihood_ratio"] == hypothesis["noisy_log_likelihood_ratio"], partitions
    assert abs(winner["single_cell_log_lr_replay_difference"]) <= 2e-9 * max(1, abs(winner["search_maximum_single_cell_log_lr"])), physical_search
    refined_search = invoke("--mode", "hf-search", "--scenario", "awgn", "--waveform", "smoke",
                            "--search-slot", "1", "--cn0", "40", "--max-wall-seconds", "25",
                            "--hf-physical-diagnostic", "--hf-phase-refinement", "--chunk", "1021")
    refined_diagnostic = refined_search["hf_public_search"]
    assert refined_diagnostic["refinement_enabled"] and not refined_diagnostic["affine_comparison_evaluated"], refined_search
    assert refined_search["channel"]["selected_observation_sha256"] == search["channel"]["selected_observation_sha256"], refined_search
    refined_physical = refined_search["hf_physical_diagnostic"]
    assert refined_physical["physical_templates"]["template_sha256"] == physical["physical_templates"]["template_sha256"], refined_search
    assert refined_physical["refinement_replay_available"], refined_search
    refined_cell = refined_physical["refined_public_cell"]
    assert len(refined_physical["gain_partitions"]["refined_public_cell"]) == 5, refined_physical
    assert abs(refined_cell["single_cell_log_lr_replay_difference"]) <= 2e-9 * max(1, abs(refined_cell["search_maximum_single_cell_log_lr"])), refined_search
    assert refined_physical["public_refinement_proposals"]["not_admitted_evidence"], refined_search
    assert not refined_physical["public_refinement_proposals"]["uses_physical_truth"], refined_search
    compound_search = invoke("--mode", "hf-search", "--scenario", "hf-eikonal-compound", "--waveform", "smoke",
                             "--cn0", "20", "--max-wall-seconds", "25", "--hf-physical-diagnostic")
    assert compound_search["plan"]["path_count"] == 2 and compound_search["plan"]["assumed_public_taps"] == 1, compound_search
    compound_physical = compound_search["hf_physical_diagnostic"]["physical_templates"]
    assert compound_physical["noise_free_energy"] > 0, compound_search
    assert math.isclose(compound_physical["hypotheses"][compound_physical["wanted_bit"]]["clean_coherence_power"], 1, abs_tol=1e-12), compound_search
    for arguments in (["--mode", "receive"], ["--control", "noise"], ["--search-slot", "3"]):
        rejected = invoke("--mode", "hf-search", "--scenario", "awgn", "--waveform", "smoke",
                          "--hf-physical-diagnostic", *arguments, code=2)
        assert "physical diagnostic" in rejected["reason"], rejected
    for arguments, code in ((["--helper"], 2), (["--backend", "cuda"], 2),
            (["--control", "capture-gap"], 2), (["--search-slot", "5"], 2),
            (["--scenario", "hf-ox-split"], 5), (["--scenario", "noise-bursts"], 5),
            (["--scenario", "frontend-overload"], 5), (["--history-reference-delay-seconds", "0"], 2),
            (["--search-slot", "1", "--capture-digest-slot", "0"], 2)):
        rejected = invoke("--mode", "hf-search", "--scenario", "awgn", "--waveform", "smoke", *arguments, code=code)
        assert rejected["status"] in ("error", "unavailable"), rejected
    for mode in ("receive", "slot", "oracle", "plan"):
        rejected = invoke("--mode", mode, "--scenario", "awgn", "--waveform", "smoke",
                          "--hf-phase-refinement", code=2)
        assert "phase refinement" in rejected["reason"], rejected
    for arguments, code in ((["--mode", "hf-search"], 2), (["--helper"], 2),
                            (["--backend", "cuda"], 2), (["--scenario", "hf-ox-split"], 5)):
        rejected = invoke("--mode", "receive", "--scenario", "awgn", "--waveform", "smoke",
                          "--hf-public-phase-receiver", *arguments, code=code)
        assert rejected["status"] in ("error", "unavailable"), rejected
    common = ["--scenario", "awgn", "--waveform", "smoke", "--seed", "5489",
              "--cn0", "40", "--max-wall-seconds", "25"]
    plan = invoke("--mode", "plan", *common)
    assert plan["plan"]["wire_bits"] == 3, plan
    assert plan["plan"]["represented_seconds"] >= 120, plan
    assert plan["plan"]["fits_sample_budget"], plan
    assert plan["plan"]["scored_slot_horizon"] == plan["plan"]["first_uncontaminated_quiet_slot"] + 2, plan
    assert plan["plan"]["fade_slots"] is None, plan
    assert plan["plan"]["capture_preroll_seconds"] == .25 and not plan["plan"]["capture_preroll_override"], plan
    helper_plan = invoke("--mode", "plan", *common, "--helper")["plan"]
    longer_helper_plan = invoke("--mode", "plan", *common, "--helper", "--capture-lead-seconds", "2")["plan"]
    assert longer_helper_plan["capture_preroll_seconds"] == 2 and longer_helper_plan["capture_preroll_override"], longer_helper_plan
    for field in ("actual_bit_seconds", "wire_bits", "epoch", "initial_ordinal", "first_data_nominal_seconds",
                  "scored_slot_horizon", "first_uncontaminated_quiet_slot", "public_bounds"):
        assert longer_helper_plan[field] == helper_plan[field], (field, helper_plan, longer_helper_plan)
    assert longer_helper_plan["first_data_nominal_capture_seconds"] - helper_plan["first_data_nominal_capture_seconds"] == 1.75, longer_helper_plan
    assert longer_helper_plan["source_stop_sample"] - helper_plan["source_stop_sample"] == int(1.75 * helper_plan["sample_rate_hz"]), longer_helper_plan
    for lead in (".25", "10"):
        boundary = invoke("--mode", "plan", *common, "--capture-lead-seconds", lead)["plan"]
        assert boundary["capture_preroll_seconds"] == float(lead) and boundary["capture_preroll_override"], boundary
    for lead in ("0", ".249", "10.001", "nan", "inf"):
        rejected_lead = invoke("--mode", "plan", *common, "--capture-lead-seconds", lead, code=2)
        assert "capture lead" in rejected_lead["reason"] or "finite number" in rejected_lead["reason"], rejected_lead
    # Hold only this plan comparison's memory admission fixed: automatic
    # available-memory estimates may change between independent invocations.
    fade_plan = ["--mode", "plan", *common, "--control", "interior-fade", "--memory-mib", "1024"]
    default_fade = invoke(*fade_plan)["plan"]
    one_fade = invoke(*fade_plan, "--fade-slots", "1")["plan"]
    two_fade = invoke(*fade_plan, "--fade-slots", "2")["plan"]
    assert default_fade == two_fade, (default_fade, two_fade)
    assert one_fade["fade_slots"] == 1 and two_fade["fade_slots"] == 2, (one_fade, two_fade)
    assert one_fade["public_slots"] == two_fade["public_slots"], (one_fade, two_fade)
    middle = one_fade["wire_bits"] // 2
    assert one_fade["capture_gap"] == one_fade["public_slots"][middle]["observation"], one_fade
    assert two_fade["capture_gap"]["first"] == one_fade["capture_gap"]["first"], two_fade
    assert two_fade["capture_gap"]["stop"] == two_fade["public_slots"][middle + 1]["observation"]["stop"], two_fade
    for bad_args in (("--control", "interior-fade", "--fade-slots", "0"),
                     ("--control", "interior-fade", "--fade-slots", "3"),
                     ("--control", "interior-fade", "--fade-slots", "1.0"),
                     ("--control", "capture-gap", "--fade-slots", "1"), ("--fade-slots", "1")):
        bad_fade = invoke("--mode", "plan", *common, *bad_args, code=2)
        assert "fade slots" in bad_fade["reason"] or "unsigned integer" in bad_fade["reason"], bad_fade

    wanted = invoke("--mode", "receive", *common, "--capture-digest-slot", "1")
    assert wanted["status"] == "completed", wanted
    assert wanted["outcomes"]["correct_complete"], wanted
    assert wanted["outcomes"]["correct_decoded_complete"], wanted
    assert wanted["plan"]["fec"] == "off" and wanted["plan"]["short_wire_bit_limit"] == 208, wanted
    assert wanted["outcomes"]["completion_evaluated"], wanted
    assert wanted["channel"]["hashed_samples"] == wanted["channel"]["samples"], wanted
    assert wanted["outcomes"]["premature_completions"] == 0, wanted
    assert wanted["outcomes"]["wrong_bits"] == 0, wanted
    assert wanted["outcomes"]["actual_scored_slots"] >= 5, wanted
    assert wanted["channel"]["selected_observation_sha256"] == search["channel"]["selected_observation_sha256"], (wanted, search)
    assert wanted["channel"]["selected_observation_complete"], wanted
    assert wanted["channel"]["selected_observation_hashed_samples"] == observation["stop"] - observation["first"], wanted
    phase_receive = invoke("--mode", "receive", *common, "--capture-digest-slot", "1", "--hf-public-phase-receiver")
    phase_outcomes = phase_receive["outcomes"]
    assert phase_outcomes["correct_complete"] and phase_outcomes["correct_decoded_complete"], phase_receive
    assert phase_outcomes["completion_evaluated"] and phase_outcomes["physical_completions"] == 1, phase_receive
    assert phase_outcomes["false_data"] == phase_outcomes["wrong_bits"] == phase_outcomes["premature_completions"] == 0, phase_receive
    decoded = [message for message in phase_receive["messages"] if message["decoded"]]
    assert len(decoded) == 1 and decoded[0]["physical_complete"], phase_receive
    assert decoded[0]["decoded_payload_bytes"] == list(wanted["plan"]["message"].encode("ascii")), phase_receive
    assert phase_receive["channel"]["selected_observation_sha256"] == wanted["channel"]["selected_observation_sha256"], phase_receive
    assert phase_receive["receiver"]["workers"] == wanted["receiver"]["workers"] == 2 * wanted["receiver"]["hardware_threads"], phase_receive
    assert phase_receive["plan"]["first_slot_workspace_bytes"] >= wanted["plan"]["first_slot_workspace_bytes"], phase_receive
    phase_report = phase_receive["hf_public_phase_receiver"]
    assert phase_report["experimental"] and phase_report["fixed_weight"] == .25, phase_receive
    assert phase_report["model_family"] == "sinusoidal_phase_97" and not phase_report["truth_information_passed_to_search"], phase_receive
    # Bright DATA can finish before the supplement. Physical quiet still
    # exercises its callback within the actual complete native stream.
    quiet_start = phase_receive["plan"]["first_uncontaminated_quiet_slot"]
    callbacks = {slot["slot_index"] for slot in phase_report["slots"] if slot["component_complete"]}
    assert {quiet_start, quiet_start + 1} <= callbacks, phase_receive
    assert phase_report["executed_slots"] == len(callbacks), phase_receive
    refined_receive = invoke("--mode", "receive", *common, "--capture-digest-slot", "1",
                             "--hf-public-phase-receiver", "--hf-phase-refinement")
    refined_outcomes = refined_receive["outcomes"]
    assert refined_outcomes["correct_complete"] and refined_outcomes["correct_decoded_complete"], refined_receive
    assert refined_outcomes["false_data"] == refined_outcomes["wrong_bits"] == refined_outcomes["premature_completions"] == 0, refined_receive
    assert refined_outcomes["physical_completions"] == 1 and not refined_outcomes["missed_completion"], refined_receive
    refined_messages = [message for message in refined_receive["messages"] if message["decoded"]]
    assert len(refined_messages) == 1 and refined_messages[0]["decoded_payload_bytes"] == list(wanted["plan"]["message"].encode("ascii")), refined_receive
    assert refined_receive["channel"]["selected_observation_sha256"] == wanted["channel"]["selected_observation_sha256"], refined_receive
    assert refined_receive["receiver"]["workers"] == wanted["receiver"]["workers"], refined_receive
    assert refined_receive["plan"]["evaluator_reserved_bytes_per_public_slot"] == 65536, refined_receive
    refined_report = refined_receive["hf_public_phase_receiver"]
    assert refined_report["refinement_enabled"] and refined_report["fixed_weight"] == .25, refined_receive
    refined_callbacks = {slot["slot_index"] for slot in refined_report["slots"] if slot["component_complete"]}
    assert {quiet_start, quiet_start + 1} <= refined_callbacks, refined_receive
    assert refined_report["executed_slots"] == len(refined_callbacks), refined_receive
    chunked = invoke("--mode", "receive", *common, "--chunk", "1021", "--capture-digest-slot", "1")
    assert chunked["channel"]["input_sha256"] == wanted["channel"]["input_sha256"], chunked
    assert chunked["channel"]["selected_observation_sha256"] == wanted["channel"]["selected_observation_sha256"], chunked
    assert chunked["outcomes"]["correct_complete"], chunked
    first_slot = invoke("--mode", "slot", *common)
    assert first_slot["status"] == "completed", first_slot
    assert first_slot["outcomes"]["component_only"], first_slot
    assert first_slot["outcomes"]["first_public_observation_complete"], first_slot
    assert first_slot["outcomes"]["first_public_observation_correct_data"], first_slot
    assert first_slot["outcomes"]["actual_scored_slots"] == 1, first_slot
    assert not first_slot["outcomes"]["correct_complete"], first_slot
    assert not first_slot["outcomes"]["completion_evaluated"], first_slot

    # Public RX calibration must not depend on the transmitted key or ordinal.
    for control in ("wrong-key", "wrong-ordinal"):
        negative = invoke("--mode", "receive", *common, "--control", control)
        assert negative["status"] == "completed", negative
        assert negative["source"]["receiver_data_basis_gain"] == wanted["source"]["receiver_data_basis_gain"], negative
        assert not negative["outcomes"]["correct_complete"], negative

    durations = [invoke("--mode", "plan", "--scenario", "awgn", "--waveform", "reference",
                        "--bit-seconds", duration)["plan"]["actual_bit_seconds"] for duration in ("16", "32")]
    assert durations[1] > durations[0], durations
    for fec, expected_bits, limit in (("off", 65, 208), ("rs20", 136, 304), ("rs60", 184, 400)):
        fec_plan = invoke("--mode", "plan", "--scenario", "awgn", "--message", "HELLO", "--fec", fec)["plan"]
        assert fec_plan["wire_bits"] == expected_bits and fec_plan["short_wire_bit_limit"] == limit, fec_plan
        assert fec_plan["fec"] == fec and fec_plan["content_limit_bytes"] == 16 and not fec_plan["long_form"], fec_plan
    invalid_fec = invoke("--mode", "plan", "--fec", "rs40", code=2)
    assert "FEC" in invalid_fec["reason"], invalid_fec
    # One fully observed physical quiet slot becomes a located erasure when
    # DATA resumes. An invalid capture gap instead invalidates the episode.
    repaired = invoke("--mode", "receive", *common, "--fec", "rs20", "--control", "interior-fade",
                      "--fade-slots", "1", "--max-samples", "10000000")
    outcome = repaired["outcomes"]
    assert repaired["status"] == "completed" and repaired["plan"]["wire_bits"] == 72, repaired
    assert not outcome["correct_complete"] and outcome["correct_decoded_complete"], repaired
    assert outcome["physical_completions"] == outcome["accepted_messages"] == 1, repaired
    assert outcome["actual_scored_slots"] == 74 and outcome["quiet_active_slots"] == 1, repaired
    assert outcome["wrong_bits"] == outcome["false_data"] == outcome["premature_completions"] == 0, repaired
    message = repaired["messages"][0]
    assert message["observed_bits"] == 71 and message["missing_slots"] == 1, message
    assert message["erasure_slot_indices"] == [36] and message["trailing_quiet_slots"] == [72, 73], message
    assert message["decoded_payload_bytes"] == [65] and message["decoded_endpoint_matches"], message
    # The erased bit happens to equal the native zero placeholder, so changed
    # bytes remain zero while the erased/repaired-byte union must still be one.
    assert message["corrected_bytes"] == 0, message
    integrity = message["fec_stats"]["integrity"]
    assert integrity["received_bits"] == 31 and integrity["missing_bits"] == 1, integrity
    assert integrity["erased_bytes"] == integrity["repaired_bytes"] == 1, integrity
    runner_spec = importlib.util.spec_from_file_location("appraisal_run", Path(__file__).resolve().parents[1] / "run.py")
    assert runner_spec and runner_spec.loader
    runner = importlib.util.module_from_spec(runner_spec)
    sys.modules[runner_spec.name] = runner
    runner_spec.loader.exec_module(runner)
    runner._validate_completed_receipt(refined_receive, {
        "id": "refinement-native-integration", "scenario": "awgn", "mode": "receive", "waveform": "smoke",
        "seed": 5489, "cn0_db_hz": 40, "capture_digest_slot": 1, "hf_public_phase_receiver": True,
        "hf_phase_refinement": True})
    runner._validate_completed_receipt(refined_search, {
        "id": "refinement-integration", "scenario": "awgn", "mode": "hf-search", "waveform": "smoke",
        "seed": 5489, "search_slot": 1, "cn0_db_hz": 40, "hf_physical_diagnostic": True,
        "hf_phase_refinement": True, "chunk": 1021})
    runner._validate_completed_receipt(compound_search, {
        "id": "compound-integration", "scenario": "hf-eikonal-compound", "mode": "hf-search", "waveform": "smoke",
        "seed": 5489, "cn0_db_hz": 20, "hf_physical_diagnostic": True})
    runner._validate_completed_receipt(physical_search, {
        "id": "physical-integration", "scenario": "awgn", "mode": "hf-search", "waveform": "smoke",
        "seed": 5489, "search_slot": 1, "cn0_db_hz": 40, "hf_physical_diagnostic": True, "chunk": 1021})
    runner._validate_completed_receipt(repaired, {
        "id": "native-rs20-one-fade", "mode": "receive", "scenario": "awgn", "waveform": "smoke",
        "seed": 5489, "cn0_db_hz": 40, "fec": "rs20", "control": "interior-fade", "fade_slots": 1})

    helper_supported = invoke("--mode", "receive", "--scenario", "awgn", "--waveform", "reference",
                              "--seed", "5489", "--cn0", "40", "--helper", "--capture-lead-seconds", "2",
                              "--max-wall-seconds", "25")
    helper_geometry = helper_supported["plan"]
    assert helper_supported["status"] == "completed" and helper_supported["outcomes"]["correct_complete"], helper_supported
    assert helper_geometry["helper_enabled"] and helper_geometry["wire_bits"] == 3, helper_geometry
    assert helper_geometry["capture_preroll_seconds"] == 2 and helper_geometry["capture_preroll_override"], helper_geometry
    assert helper_geometry["first_data_nominal_capture_seconds"] - helper_geometry["first_data_nominal_seconds"] == 2, helper_geometry
    assert helper_supported["source"]["generated_samples"] + 2 * helper_geometry["sample_rate_hz"] == helper_geometry["source_stop_sample"], helper_supported
    assert helper_supported["source"]["helper_energy_sample_sum"] > 0, helper_supported
    assert helper_supported["outcomes"]["accepted_samples"] == helper_geometry["horizon_samples"], helper_supported
    assert helper_supported["messages"][0]["trailing_quiet_slots"] == [3, 4], helper_supported
    # This zero-width prior deliberately uses the frequency fallback after
    # successful onset-noise support; it does not demonstrate frequency narrowing.
    assert helper_geometry["public_bounds"]["frequency_bound_hz"] == 0, helper_geometry
    assert [slot["helper_status"] for slot in helper_supported["slots"][:3]] == ["onset_branch_frequency_fallback"] * 3, helper_supported
    assert all(slot["complete"] and slot["all_valid"] and slot["decision"] == "data"
               for slot in helper_supported["slots"][:3]), helper_supported
    runner._validate_completed_receipt(helper_supported, {
        "id": "native-helper-supported-preroll", "mode": "receive", "scenario": "awgn", "waveform": "reference",
        "seed": 5489, "cn0_db_hz": 40, "helper": True, "capture_lead_seconds": 2})

    oracle = invoke("--mode", "oracle", "--scenario", "awgn", "--waveform", "reference",
                    "--bit-seconds", "32", "--cn0", "-4")
    presence = oracle["presence_oracle"]
    assert 0 < presence["false_alarm_target"] < 1e-8, presence
    assert 6 < presence["normal_upper_tail_quantile"] < 6.3, presence
    for label in ("hypothesis_0", "hypothesis_1"):
        point = presence[label]
        assert abs(point["probability_detection"] + point["probability_miss"] - 1) < 1e-14, point
        assert point["probability_miss"] > oracle["oracle"]["probability_error"], oracle

    # The HF history calculation is a private same-aperture diagnostic. It
    # retains actual white noise throughout that aperture, fixed gain priors,
    # and both bit templates; it never declares reception or completion.
    hf_args = ["--mode", "oracle", "--scenario", "hf-quiet", "--waveform", "reference",
               "--seed", "7201", "--cn0", "0", "--max-wall-seconds", "25"]
    hf = invoke(*hf_args)
    assert hf["oracle_protocol_eligible"] and hf["hf_history_applicable"], hf
    assert not hf["outcomes"]["receiver_executed"] and not hf["outcomes"]["correct_complete"], hf
    history = hf["hf_history"]
    aperture = hf["oracle"]["public_observation"]["aperture"]
    assert history["aperture"] == aperture, history
    assert history["samples"] == aperture["stop"] - aperture["first"], history
    assert not history["truth_information_passed_to_receiver"], history
    assert history["gain_prior_variance"] == 1, history
    assert history["noise_only_energy"] > 0, history
    assert [row["requested_block_seconds"] for row in history["blocks"]] == [.25, 1, 4, 16, None], history
    selected = f"hypothesis_{history['wanted_bit']}_energy_seconds"
    assert math.isclose(history["noise_free_energy_seconds"], hf["oracle"]["public_observation"][selected],
                        rel_tol=1e-12), history
    for row in history["blocks"]:
        for hypothesis in row["hypotheses"]:
            assert hypothesis["rank"] >= 1, row
            assert 0 <= hypothesis["retained_signal_fraction"] <= 1 + 1e-12, row
            assert math.isfinite(hypothesis["noisy_log_likelihood_ratio"]), row
            assert math.isfinite(hypothesis["noise_only_log_likelihood_ratio"]), row
    hf_chunked = invoke(*hf_args, "--chunk", "1021")
    assert hf_chunked["hf_history"]["blocks"] == history["blocks"], (hf_chunked, hf)
    assert hf_chunked["oracle"]["public_observation"] == hf["oracle"]["public_observation"], (hf_chunked, hf)
    # Selected later slots must advance every independent clean source through
    # the prefix rather than jumping a forward-only Emission cursor.
    hf_later = invoke(*hf_args, "--oracle-slot", "1")
    later_history = hf_later["hf_history"]
    assert hf_later["oracle"]["slot_index"] == 1 and later_history["aperture"]["first"] > 0, hf_later
    assert later_history["samples"] == later_history["aperture"]["stop"] - later_history["aperture"]["first"], hf_later
    assert later_history["reference_delay_seconds"] == 0 and not hf_later["outcomes"]["receiver_executed"], hf_later
    shifted = invoke(*hf_args, "--oracle-slot", "1", "--history-reference-delay-seconds", "0.002", "--hf-phase-bank")
    assert shifted["plan"]["history_reference_delay_seconds"] == shifted["hf_history"]["reference_delay_seconds"] == .002, shifted
    # Changing an evaluator template does not alter channel truth or the
    # exact known-channel distance/energy reference.
    assert shifted["oracle"]["public_observation"] == hf_later["oracle"]["public_observation"], shifted
    phase = shifted["hf_phase_bank"]
    assert shifted["plan"]["hf_phase_bank_requested"] and phase["model_count"] == 97, shifted
    assert phase["aperture"] == later_history["aperture"] and phase["samples"] == later_history["samples"], phase
    assert phase["reference_delay_seconds"] == .002 and phase["workspace_bytes"] > 0, phase
    assert phase["complex_multiply_accumulates"] > 0 and len(phase["models"]) == 97, phase
    assert phase["arithmetic"]["complex_accumulation"] == "std::complex<long double>", phase
    assert phase["arithmetic"]["oscillators_and_phase_rotations"] == "std::complex<double>", phase
    assert phase["arithmetic"]["long_double_significand_bits"] >= 53, phase
    assert phase["arithmetic"]["long_double_storage_bytes"] >= 8, phase
    assert not phase["truth_information_passed_to_phase_bank"] and not phase["truth_information_passed_to_receiver"], phase
    assert not phase["receiver_executed"] and not phase["completion_evaluated"] and not phase["capacity_qualified"], phase
    for observation in ("wanted_observation", "noise_only_observation"):
        assert math.isfinite(phase[observation]["mixture_log_likelihood_ratio"]), phase
        assert 0 <= phase[observation]["bit_one_probability_given_signal"] <= 1, phase
    for unsupported in (("--history-reference-delay-seconds", "-0.001"),
                        ("--history-reference-delay-seconds", "nan"),
                        ("--history-reference-delay-seconds", "1"),
                        ("--history-reference-delay-seconds", "0.002", "--mode", "receive"),
                        ("--history-reference-delay-seconds", "0", "--scenario", "awgn"),
                        ("--hf-phase-bank", "--mode", "channel")):
        rejected_diagnostic = invoke(*hf_args, *unsupported, code=2)
        assert rejected_diagnostic["status"] == "error", rejected_diagnostic
    for unsupported in (("--helper",), ("--control", "noise"), ("--scenario", "frontend-overload")):
        rejected_oracle = invoke(*hf_args, *unsupported, code=2)
        assert rejected_oracle["status"] == "error", rejected_oracle

    quiet = invoke("--mode", "receive", *common, "--control", "noise")
    assert quiet["status"] == "completed", quiet
    assert quiet["outcomes"]["false_data"] == 0, quiet
    assert quiet["outcomes"]["physical_completions"] == 0, quiet

    eof = invoke("--mode", "receive", *common, "--control", "eof")
    assert not eof["outcomes"]["correct_complete"], eof
    assert eof["outcomes"]["physical_completions"] == 0, eof

    transition = invoke("--mode", "receive", "--scenario", "hf-transition",
                        "--waveform", "reference", "--seed", "9204", "--cn0", "20",
                        "--message", "A", "--max-wall-seconds", "25")
    assert transition["status"] == "completed", transition
    transition_boundary = transition["outcomes"]["resumed_data_boundary_sample"]
    assert transition_boundary == 240 * transition["plan"]["sample_rate_hz"], transition
    assert transition["outcomes"]["resumed_data_boundary_source"] == "scenario_fade_stop", transition
    assert transition["outcomes"]["configured_outage"], transition
    assert not transition["outcomes"]["configured_mode_voltage_transitions"], transition
    expected_resumed = sum(
        slot["index"] < transition["plan"]["wire_bits"] and
        slot["first_sample"] >= transition_boundary and slot["decision"] == "data" and
        slot["complete"] and slot["all_valid"]
        for slot in transition["slots"])
    assert expected_resumed > 0, transition
    assert transition["outcomes"]["resumed_data_slots"] == expected_resumed, transition
    assert any(message.get("reacquisition_count", 0) > 0 for message in transition["messages"]), transition

    # Modal gates have a reporting boundary even without a global Fade. The
    # boundary identifies the last voltage change; it does not itself prove
    # recovery. Recount observed DATA and retain premature completion failures.
    mode_transition = invoke("--mode", "receive", "--scenario", "hf-mode-transition",
                             "--waveform", "reference", "--seed", "10106", "--cn0", "20",
                             "--message", "A", "--max-wall-seconds", "25")
    mode_outcomes = mode_transition["outcomes"]
    assert mode_transition["status"] == "completed", mode_transition
    mode_boundary = mode_outcomes["resumed_data_boundary_sample"]
    assert mode_boundary == 300 * mode_transition["plan"]["sample_rate_hz"], mode_transition
    assert mode_outcomes["resumed_data_boundary_source"] == "scenario_mode_transition_stop", mode_transition
    assert mode_outcomes["configured_mode_voltage_transitions"] and not mode_outcomes["configured_outage"], mode_transition
    expected_mode_resumed = sum(
        slot["index"] < mode_transition["plan"]["wire_bits"] and
        slot["first_sample"] >= mode_boundary and slot["decision"] == "data" and
        slot["complete"] and slot["all_valid"] and slot["bit"] is not None
        for slot in mode_transition["slots"])
    assert expected_mode_resumed > 0 and mode_outcomes["resumed_data_slots"] == expected_mode_resumed, mode_transition
    assert mode_outcomes["premature_completions"] == 1, mode_transition
    assert mode_outcomes["physical_completions"] == 2 and not mode_outcomes["correct_complete"], mode_transition

    # A plan must expose, rather than silently compress, the slow native bit.
    slow = invoke("--mode", "plan", "--scenario", "vlf-resonant",
                  "--waveform", "reference", "--max-samples", "1000")
    assert slow["plan"]["actual_bit_seconds"] >= 5760, slow
    assert not slow["plan"]["fits_sample_budget"], slow
    slow_tapered = invoke("--mode", "plan", "--scenario", "vlf-resonant",
                          "--waveform", "reference", "--max-samples", "1000",
                          "--pulse", "tapered-rrc")
    assert slow["plan"]["pulse"] == "rrc", slow
    assert slow_tapered["plan"]["pulse"] == "tapered-rrc", slow_tapered
    for field in ("sample_rate_hz", "requested_bit_seconds", "actual_bit_seconds",
                  "first_data_nominal_seconds", "wire_bits"):
        assert slow["plan"][field] == slow_tapered["plan"][field], (field, slow, slow_tapered)

    for unsupported in (("--control", "noise"), ("--helper",), ("--scenario", "awgn"),
                        ("--waveform", "smoke")):
        diagnostic_args = ["--mode", "diagnose", "--scenario", "vlf-resonant",
                           "--waveform", "reference", "--max-wall-seconds", "2", *unsupported]
        diagnostic_rejected = invoke(*diagnostic_args, code=2)
        assert diagnostic_rejected["status"] == "error", diagnostic_rejected

    # The spectral EME episode model supports the default message within its
    # declared 360-second numerical horizon. A selected later oracle bit can be
    # represented by a cheap plan-only request without rendering the lunar path.
    eme_common = ["--scenario", "eme-disk-episode", "--waveform", "reference",
                  "--max-samples", "100000000", "--max-wall-seconds", "20"]
    eme_default = invoke("--mode", "plan", *eme_common)
    assert eme_default["status"] == "completed", eme_default
    assert eme_default["plan"]["wire_bits"] == 3, eme_default
    assert eme_default["plan"]["numerical_horizon_seconds"] == 360, eme_default
    assert eme_default["plan"]["represented_seconds"] < 360, eme_default
    assert eme_default["plan"]["fits_numerical_horizon"], eme_default
    eme_selected_plan = invoke("--mode", "plan", *eme_common, "--oracle-slot", "1")
    assert eme_selected_plan["plan"]["fits_numerical_horizon"], eme_selected_plan
    assert eme_selected_plan["plan"]["public_slots"][1]["index"] == 1, eme_selected_plan
    assert 1 < eme_selected_plan["plan"]["wire_bits"], eme_selected_plan
    eme_long = invoke("--mode", "receive", *eme_common, "--message", "abcdefghijklmnop", code=5)
    assert eme_long["status"] == "unavailable", eme_long
    assert eme_long["plan"]["wire_bits"] > eme_default["plan"]["wire_bits"], eme_long
    lunar_helper = invoke("--mode", "plan", *eme_common, "--helper", code=5)
    assert lunar_helper["status"] == "unavailable", lunar_helper

    rejected = invoke("--mode", "receive", *common, "--max-samples", "1", code=3)
    assert rejected["status"] == "resource_limit", rejected
    late_slot = 6
    late_oracle = invoke("--mode", "oracle", *eme_common, "--message", "abcdefghijklmnop",
                         "--oracle-slot", str(late_slot), code=5)
    assert late_oracle["status"] == "unavailable", late_oracle
    assert late_oracle["plan"]["oracle_slot"] == late_slot < late_oracle["plan"]["wire_bits"], late_oracle
    assert late_oracle["plan"]["oracle_horizon_samples"] > 360 * late_oracle["plan"]["sample_rate_hz"], late_oracle
    assert "numerical qualification" in late_oracle["reason"], late_oracle
    print("native source/channel/complete-message, noise, EOF, long-duration and budget controls passed")


if __name__ == "__main__":
    main()
