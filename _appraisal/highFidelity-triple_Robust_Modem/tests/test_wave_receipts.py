from __future__ import annotations

import copy
import hashlib
import json
import math
import unittest
from pathlib import Path
import sys

APP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(APP))
import run


class WaveReceiptTests(unittest.TestCase):
    def _episode(self, mode="receive", **extra):
        episode = {"id": "wave", "scenario": "hf-wave-lens", "mode": mode,
                   "waveform": "reference", "seed": 3}
        episode.update(extra)
        run._validate_episode(episode, 0, set(), 60, 100_000_000)
        return episode

    def _receipt(self):
        fs = 1920
        crossing_end = 128 + 400000 / 299792458 + 32 / fs + 12800 / 120 + 84 / fs
        source_stop = math.ceil(crossing_end * fs)
        horizon = source_stop + 1000
        descriptor = {
            "carrier_hz": 10_000_000, "sample_rate_hz": fs, "occupied_half_band_hz": 384,
            "ambient_tecu": .015, "slab_length_m": 3000., "peak_delta_tecu": -.00857,
            "impact_y_m": 0., "drift_mps": 120., "closest_seconds": 128.,
            "sigma_x_m": 1600., "sigma_y_m": 2400., "incoming_distance_m": 200000.,
            "outgoing_distance_m": 400000., "effective_distance_m": 133333.33333333334,
            "first_lag_samples": 4, "last_lag_samples": 84, "taps": 81,
            "position_step_m": 100., "maximum_table_position_m": 12800.,
            "interpolation": "complex_cubic_hermite_at_output_time_with_analytic_far_field",
            "preparation_workspace_bound_bytes": 1048576, "maximum_rf_evaluations": 23000,
        }
        slots = [
            {"index": 0, "observation": {"first": 0, "stop": 100}, "evaluator_transmitting": True},
            {"index": 1, "observation": {"first": source_stop + 84, "stop": source_stop + 200}, "evaluator_transmitting": False},
            {"index": 2, "observation": {"first": source_stop + 200, "stop": horizon}, "evaluator_transmitting": False},
        ]
        plan = {
            "channel_renderer": "hf_wave_optics_screen", "wave_optics": descriptor,
            "sample_rate_hz": fs, "source_stop_sample": source_stop, "horizon_samples": horizon,
            "represented_seconds": horizon / fs, "channel_tail_seconds": 84 / fs,
            "channel_tail_seconds_scope": "complete causal translating-screen FIR support from first input through last retained lag",
            "cn0_reference_plane": "normalized incident DATA before scalar screen loss and fixed additive receiver noise",
            "wave_optics_source_band_checked": True, "wave_optics_table_built": True,
            "physical_kernel_supplied_to_receiver": False, "complete_screen_crossing_required": True,
            "complete_source_extent_checked": True, "screen_crossing_stop_seconds": crossing_end,
            "full_crossing_rendered": True,
            "screen_exposure_scope": "complete_declared_lens_support_and_FIR_tail",
            "path_count": None, "path_sample_products": None,
            "public_bounds": {"arrival_low_seconds": 4 / fs, "arrival_high_seconds": 84 / fs,
                              "receiver_filter_halo_samples": 80},
            "planned_uncontaminated_quiet_slots": 2, "wire_bits": 1,
            "first_uncontaminated_quiet_slot": 1, "scored_slot_horizon": 3,
            "public_slots": slots,
        }
        witness = {"absolute_error": .0001, "raw_amplitude": .001,
                   "displacement_m": 0., "offset_hz": 0.}
        channel_wave = dict(descriptor, table_sha256="a" * 64,
            table_digest_scope=("all 129 displacement knots, each retained lag, complex binary64 coefficient and "
                "cubic-Hermite slope per 100 m, little-endian; source identity binds the interpolation implementation"),
            qualification_scope=("sampled scalar cold-plasma response; occupied-band and off-grid/position witnesses, "
                "causal FIR energy accounting and analytic far-field bound; no evolving-Maxwell or field calibration"),
            table_qualified=True, rf_evaluations=21391, occupied_witnesses=16, unresolved_witnesses=0,
            maximum_discarded_energy_fraction=1e-7, maximum_raw_precursor_energy_fraction=1e-7,
            maximum_series_tail_bound=1e-14, far_field_error_bound=1e-9,
            minimum_sampled_carrier_power=.5, maximum_sampled_carrier_power=1.5, maximum_series_terms=20,
            maximum_frequency_witness_error=witness, maximum_position_witness_error=witness,
            minimum_raw_voltage_witness=witness, retarded_peak_error_bound=.001,
            table_workspace_bytes=500000, stream_workspace_bytes=1000, preparation_seconds=.01,
            source_minimum_frequency_hz=-300., source_maximum_frequency_hz=300.,
            incident_energy_sample_sum=1., selected_energy_sample_sum=1.3)
        channel = {"samples": horizon, "source_samples": source_stop, "hashed_samples": horizon,
                   "input_sha256": "b" * 64, "tail_seconds": math.nextafter(84 / fs, math.inf),
                   "maximum_delay_seconds": 4 / fs, "wave_optics": channel_wave,
                   "input_energy_sample_sum": 1., "signal_energy_sample_sum": 1.3,
                   "contains_source_energy": True, "contains_signal_energy": True}
        return {"plan": plan, "scenario": {"channel": {"renderer": "hf_wave_optics_screen",
                 "wave_optics": descriptor}}, "channel": channel,
                 "timing": {"channel_preparation_seconds": .02, "wall_seconds": .1}}

    def test_wave_scenario_admits_only_supported_modes(self):
        self._episode()
        self._episode("plan")
        self._episode("channel")
        self._episode("hf-search", hf_physical_diagnostic=True)
        for mode in ("plan", "channel", "receive"):
            self._episode(mode, fec="rs20")
        for mode in ("plan", "channel", "receive", "hf-search"):
            with self.assertRaises(run.CampaignError):
                self._episode(mode, fec="rs60", hf_physical_diagnostic=mode == "hf-search")
        with self.assertRaises(run.CampaignError):
            self._episode("hf-search", fec="rs20", hf_physical_diagnostic=True)
        with self.assertRaises(run.CampaignError):
            self._episode("hf-search")
        with self.assertRaises(run.CampaignError):
            self._episode("receive", helper=True)
        with self.assertRaises(run.CampaignError):
            self._episode("receive", duration_seconds=12)

    def test_phase_screen_high_bracket_keeps_original_wanted_inputs(self):
        baseline = run._read_campaign(APP / "campaigns/hf-phase-screens-receiver.json")
        bracket = run._read_campaign(APP / "campaigns/hf-phase-screens-bracket.json")
        primary, holdout = bracket["episodes"]
        self.assertEqual(primary, dict(baseline["episodes"][2], id=primary["id"], cn0_db_hz=20))
        self.assertEqual(holdout, dict(baseline["episodes"][4], id=holdout["id"], cn0_db_hz=10))
        for mutation in ({"scenario": "hf-phase-screens-holdout"}, {"control": "noise"},
                         {"mode": "plan"}, {"bit_seconds": 32}):
            with self.subTest(mutation=mutation), self.assertRaises(run.CampaignError):
                run._validate_episode(dict(primary, **mutation), 0, set(), 90, 3072000)

    def test_native_wave_chain_admits_only_its_frozen_cases(self):
        for scenario in run.HF_WAVE_CHAIN_SCENARIOS:
            episode = {"id": scenario, "scenario": scenario, "mode": "receive",
                       "waveform": "reference", "seed": 5, "bit_seconds": 64,
                       "cn0_db_hz": 0, "control": "wanted"}
            run._validate_episode(episode, 0, set(), 180, 12_000_000)
        for change in ({"mode": "channel"}, {"control": "wrong-ordinal"},
                       {"cn0_db_hz": 1}, {"bit_seconds": 32}, {"helper": True}):
            with self.subTest(change=change):
                episode = {"id": "chain", "scenario": "hf-wave-chain-off", "mode": "receive",
                           "waveform": "reference", "seed": 5, "bit_seconds": 64,
                           "cn0_db_hz": 0, "control": "wanted", **change}
                with self.assertRaises(run.CampaignError):
                    run._validate_episode(episode, 0, set(), 180, 12_000_000)

    def test_wave_receipt_accepts_ulp_tail_and_focusing_energy(self):
        episode = self._episode()
        receipt = self._receipt()
        run._validate_wave_optics(receipt, episode)
        self.assertGreater(receipt["channel"]["wave_optics"]["selected_energy_sample_sum"],
                           receipt["channel"]["wave_optics"]["incident_energy_sample_sum"])

    def test_wave_receipt_rejects_unqualified_or_incomplete_evidence(self):
        episode = self._episode()
        mutations = (
            lambda r: r["plan"].__setitem__("wave_optics_table_built", False),
            lambda r: r["channel"]["wave_optics"].__setitem__("table_sha256", "bad"),
            lambda r: r["channel"]["wave_optics"].__setitem__("maximum_discarded_energy_fraction", 1e-4),
            lambda r: r["channel"]["wave_optics"].__setitem__("maximum_series_terms", 0),
            lambda r: r["channel"]["wave_optics"].__setitem__("selected_energy_sample_sum", 1.),
            lambda r: r["channel"]["wave_optics"].__setitem__("maximum_position_witness_error",
                {"absolute_error": .0003, "raw_amplitude": .2, "displacement_m": 0., "offset_hz": 0.}),
            lambda r: r["channel"].__setitem__("hashed_samples", r["channel"]["samples"] - 1),
            lambda r: r["plan"].__setitem__("channel_tail_seconds", 84 / 1920 + 1e-8),
        )
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                receipt = copy.deepcopy(self._receipt())
                mutate(receipt)
                with self.assertRaises(run.CampaignError):
                    run._validate_wave_optics(receipt, episode)

    def _native_wave_receipt(self):
        path = APP / "tests" / "fixtures" / "hf_wave_lens_native_channel.json"
        raw = path.read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         "4e5507f0fd5fb8b08a179fbc1396b84ed17b624da6133f54ecf5f013059e64b4")
        receipt = json.loads(raw)
        episode = {"scenario": "hf-wave-lens", "mode": "channel",
                   "waveform": "reference", "control": "wanted", "seed": 69001,
                   "noise_seed": 79001, "bit_seconds": 64, "message": "A", "cn0_db_hz": 0}
        return receipt, episode

    def test_actual_native_wave_channel_receipt_contract(self):
        receipt, episode = self._native_wave_receipt()
        run._validate_completed_receipt(receipt, episode)
        operator = receipt["channel"]["wave_optics"]
        self.assertGreater(operator["selected_energy_sample_sum"], operator["incident_energy_sample_sum"])
        self.assertGreater(operator["maximum_sampled_carrier_power"], 1.)
        self.assertGreater(operator["maximum_raw_precursor_energy_fraction"], .1)

    def test_actual_native_off_receive_requires_zero_lens_series_work(self):
        raw = (APP / "tests" / "fixtures" / "hf_wave_lens_off_native_receive.json").read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         "e65d9fc5147cc52ae86512fdb094f7d8bbbaa56eb987f980c7a9bb977df500d8")
        receipt = json.loads(raw)
        episode = run._read_campaign(APP / "campaigns" / "hf-wave-lens-receiver.json")["episodes"][0]
        run._validate_completed_receipt(receipt, episode)
        self.assertTrue(receipt["outcomes"]["correct_complete"])
        receipt["channel"]["wave_optics"]["maximum_series_terms"] = 1
        with self.assertRaises(run.CampaignError):
            run._validate_completed_receipt(receipt, episode)

    def test_actual_native_phase_screen_off_preserves_empty_refinement_scope(self):
        raw = (APP / "tests" / "fixtures" / "hf_phase_screens_off_native_receive.json").read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         "8ea61d04f7587d178cfa1784f844ca9dfc975bcda77b81993738ff91885a1a7f")
        receipt = json.loads(raw)
        episode = run._read_campaign(APP / "campaigns" / "hf-phase-screens-receiver.json")["episodes"][0]
        run._validate_completed_receipt(receipt, episode)
        self.assertTrue(receipt["outcomes"]["correct_complete"])
        report = receipt["channel"]["phase_screens"]
        for change in ({"null_windows": [{}]}, {"deep_slab_witnesses": 1},
                       {"slab_minima": [dict(report["slab_minima"][0], offset_hz=-384), *report["slab_minima"][1:]]}):
            with self.subTest(change=change), self.assertRaises(run.CampaignError):
                receipt["channel"]["phase_screens"] = dict(report, **change)
                run._validate_completed_receipt(receipt, episode)

    def test_native_chain_noise_tone_stays_in_its_reported_energy_plane(self):
        propagation = {"samples": 100, "additive_noise_energy_sample_sum": 9.,
                       "input_energy_sample_sum": 0., "signal_energy_sample_sum": 0.,
                       "contains_source_energy": False, "contains_signal_energy": False,
                       "wave_optics": {"incident_energy_sample_sum": 0., "selected_energy_sample_sum": 0.}}
        # Retiming need not conserve exactly the APP stage's energy.
        receiver = {"signal_energy_sample_sum": 8., "contains_signal_energy": True}
        run._validate_wave_chain_energy(propagation, receiver, .3, True)
        with self.assertRaises(run.CampaignError):
            run._validate_wave_chain_energy(dict(propagation, additive_noise_energy_sample_sum=0.), receiver, .3, True)
        with self.assertRaises(run.CampaignError):
            run._validate_wave_chain_energy(propagation, dict(receiver, signal_energy_sample_sum=0.), .3, True)
        # Wanted DATA can coexist with the tone. The combined native RX
        # energy can include their coherent cross term.
        propagation.update(input_energy_sample_sum=1., signal_energy_sample_sum=2.,
                           contains_source_energy=True, contains_signal_energy=True,
                           wave_optics={"incident_energy_sample_sum": 1., "selected_energy_sample_sum": 2.})
        with self.assertRaises(run.CampaignError):
            run._validate_wave_chain_energy(propagation, receiver, .3, True)
        run._validate_wave_chain_energy(propagation, receiver, .3, False)

    def test_target_presence_requires_a_matched_native_active_aperture(self):
        receipt = json.loads((APP / "tests" / "fixtures" / "hf_wave_lens_off_native_receive.json").read_text())
        episode = run._read_campaign(APP / "campaigns" / "hf-wave-lens-receiver.json")["episodes"][0]
        episode = dict(episode, hf_target_presence_diagnostic=True, capture_digest_slot=2)
        run._validate_episode(episode, 0, set(), 90, 12_000_000)
        command = run._command(Path("/native/probe"), episode, 12_000_000, 90)
        self.assertIn("--hf-target-presence-diagnostic", command)
        for override in ({"scenario": "hf-wave-chain-off"}, {"scenario": "frontend-overload"},
                         {"control": "noise"}, {"mode": "hf-search"},
                         {"hf_target_presence_diagnostic": 1}):
            with self.subTest(override=override), self.assertRaises(run.CampaignError):
                run._validate_episode(dict(episode, **override), 0, set(), 90, 12_000_000)
        plan, channel = receipt["plan"], receipt["channel"]
        observation = plan["public_slots"][2]["observation"]
        capture_bytes = 16 * (observation["stop"] - observation["first"])
        plan.update(hf_target_presence_diagnostic_requested=True, capture_digest_first_sample=observation["first"],
                    capture_digest_stop_sample=observation["stop"], diagnostic_capture_bytes=capture_bytes,
                    outside_receiver_future_reserved_bytes=capture_bytes + 1000)
        channel["selected_observation_sha256"] = "a" * 64
        table = channel["wave_optics"]["table_sha256"]
        distance = {"distance_squared": 1920., "distance_over_noise_variance": 1.,
                    "observed_log_likelihood_ratio": 1.5}
        diagnostic = {
            "component_complete": True, "slot_index": 2, "observation": observation,
            "observation_sha256": "a" * 64, "source_samples_per_twin": plan["source_stop_sample"],
            "prefix_samples_rendered_per_twin": observation["stop"], "wanted_bit": 1,
            "pre_aperture_matches_wanted": True, "transmit_range_excludes_preroll": True,
            "muted_transmit_samples": {"first": 100, "stop": 190},
            "used_for_receiver_decisions": False, "field_sensitivity_bound": False,
            "public_phase_search_executed": False, "recording_noise_in_multiplier": False,
            "template_sha256_bit0_bit1_absent": ["b" * 64, "c" * 64, "d" * 64],
            "table_sha256_bit0_bit1_absent": [table] * 3, "noise_variance": 1920.,
            "binary_correct_vs_wrong": dict(distance), "target_wanted_vs_absent": dict(distance),
            "absent_received_prefix_energy": 1., "absent_aperture_energy": 0.,
            "total_memory_limit_bytes": plan["receiver_resources"]["total_memory_limit_bytes"],
            "process_baseline_bytes": 40_000,
            "future_reserved_bytes": 90_000, "per_channel_limit_bytes": 20_000,
            "channel_workspace_bytes": [1000, 1000, 1000], "channel_instances": 3,
            "preparation_seconds": .1, "render_seconds": .3, "wall_seconds": .4,
        }
        receipt["hf_target_presence_diagnostic"] = diagnostic
        run._validate_known_target(receipt, episode)
        for field, value in (
            ("pre_aperture_matches_wanted", False), ("observation_sha256", "f" * 64),
            ("table_sha256_bit0_bit1_absent", ["f" * 64] * 3), ("public_phase_search_executed", True),
            ("used_for_receiver_decisions", True), ("recording_noise_in_multiplier", True),
            ("future_reserved_bytes", 40_000), ("absent_aperture_energy", 2.),
            ("target_wanted_vs_absent", dict(distance, distance_over_noise_variance=2.)),
        ):
            with self.subTest(field=field), self.assertRaises(run.CampaignError):
                receipt["hf_target_presence_diagnostic"] = dict(diagnostic, **{field: value})
                run._validate_known_target(receipt, episode)

    def test_wave_shadow_uses_complete_aperture_without_counting_physical_quiet(self):
        # Only exercise the shared calendar check; the actual shadow
        # receipt additionally requires the generic search statistics.
        receipt, _ = self._native_wave_receipt()
        plan = receipt["plan"]
        plan["public_slots"] = plan["public_slots"][:3]
        selected = plan["public_slots"][-1]["observation"]
        plan.update(scored_slot_horizon=3, planned_uncontaminated_quiet_slots=0,
                    search_observation=selected, horizon_samples=selected["stop"])
        shadow = {"mode": "hf-search", "search_slot": 2}
        run._validate_wave_public_calendar(plan, shadow, selected["stop"], plan["source_stop_sample"] + 84)
        for field, value in (("planned_uncontaminated_quiet_slots", 2),
                             ("search_observation", {"first": selected["first"] + 1, "stop": selected["stop"]})):
            with self.subTest(field=field), self.assertRaises(run.CampaignError):
                run._validate_wave_public_calendar(dict(plan, **{field: value}), shadow,
                                                   selected["stop"], plan["source_stop_sample"] + 84)

    def test_causal_wave_shadow_counts_the_source_prefix_from_native_layout(self):
        lens, lens_episode = self._native_wave_receipt()
        phase = json.loads((APP / "tests/fixtures/hf_phase_screens_off_native_receive.json").read_bytes())
        phase_episode = run._read_campaign(APP / "campaigns/hf-phase-screens-receiver.json")["episodes"][0]
        for receipt, baseline_episode, validate in ((lens, lens_episode, run._validate_wave_optics),
                                                      (phase, phase_episode, run._validate_phase_screens)):
            with self.subTest(scenario=baseline_episode["scenario"]):
                validate(receipt, baseline_episode)
                episode = dict(baseline_episode, mode="hf-search", search_slot=0)
                plan, channel = receipt["plan"], receipt["channel"]
                plan["public_slots"] = plan["public_slots"][:1]
                aperture = plan["public_slots"][0]["observation"]
                horizon = aperture["stop"]
                source_stop = plan["source_stop_sample"]
                self.assertLess(horizon, source_stop)
                plan.update(horizon_samples=horizon, represented_seconds=horizon / 1920, search_observation=aperture,
                            scored_slot_horizon=1, planned_uncontaminated_quiet_slots=0)
                if validate is run._validate_wave_optics:
                    plan.update(full_crossing_rendered=False, screen_exposure_scope="partial_rendered_observation")
                else:
                    plan["screen_horizon_seconds"] = horizon / 1920
                channel.update(samples=horizon, hashed_samples=horizon, source_samples=horizon)
                validate(receipt, episode)
                for field, value in (("source_samples", source_stop), ("source_samples", horizon-1),
                                     ("hashed_samples", horizon-1), ("samples", horizon+1)):
                    with self.subTest(field=field, value=value), self.assertRaisesRegex(run.CampaignError, "source prefix"):
                        altered = dict(receipt, channel=dict(channel, **{field: value}))
                        validate(altered, episode)

    def test_actual_native_wave_receipt_malformed_controls(self):
        mutations = (
            lambda r: r["channel"]["wave_optics"].__setitem__("table_qualified", False),
            lambda r: r["channel"]["wave_optics"].__setitem__("table_sha256", "not-a-sha256"),
            lambda r: r["channel"]["wave_optics"].__setitem__("maximum_series_tail_bound", 1e-8),
            lambda r: r["channel"]["wave_optics"].__setitem__("maximum_frequency_witness_error",
                {"absolute_error": .0003, "raw_amplitude": .2, "displacement_m": 0., "offset_hz": 0.}),
            lambda r: r["channel"].__setitem__("hashed_samples", r["channel"]["samples"] - 1),
            lambda r: r["plan"]["public_slots"][ -1]["observation"].__setitem__("stop",
                r["plan"]["public_slots"][ -1]["observation"]["stop"] - 1),
        )
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                receipt, episode = self._native_wave_receipt()
                receipt = copy.deepcopy(receipt)
                mutate(receipt)
                with self.assertRaises(run.CampaignError):
                    run._validate_completed_receipt(receipt, episode)


if __name__ == "__main__":
    unittest.main()
