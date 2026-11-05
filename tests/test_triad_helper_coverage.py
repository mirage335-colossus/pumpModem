import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "triad_helper_coverage", ROOT / "tools/triad_helper_coverage.py")
coverage = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(coverage)


class HelperCoverageTests(unittest.TestCase):
    def test_requested_axes_and_cartesian_count_match_prompt(self):
        self.assertEqual(len(coverage.RATES_HZ), 17)
        self.assertEqual(len(coverage.BASEBAND_CARRIERS), 3)
        self.assertEqual(len(coverage.RF_SHIFTS_HZ), 9)
        self.assertEqual(len(coverage.TIMING_POLICIES), 2)
        self.assertEqual(len(coverage.BASE_OSCILLATORS), 4)
        self.assertEqual(len(coverage.SHIFT_OSCILLATORS), 7)
        self.assertEqual(len(coverage.SPREAD_PRESETS), 10)
        self.assertEqual(len(coverage.SELECTIVE_MODULATION), 16)
        self.assertEqual(len(coverage.CN0_DB_HZ), 19)
        self.assertEqual(len(coverage.MARGINS), 2)
        self.assertEqual(len(coverage.ADDED_SYMBOLS), 4)
        self.assertEqual(len(coverage.FHSS), 2)
        self.assertEqual(len(coverage.SYMBOL_BURST), 2)
        self.assertEqual(coverage.cartesian_count(), 2_500_485_120)

    def test_curated_protocol_covers_every_marginal_and_key_interactions(self):
        protocol = coverage.build_protocol()
        design = protocol["factorized_representative_plan"]
        self.assertLess(design["row_count"], 150)
        self.assertTrue(all(design["complete_marginal_values"].values()))
        self.assertEqual(protocol["schema"], "triad-helper-broad-coverage-protocol-v2")
        baseline = design["one_factor_design"]["baseline"]
        self.assertEqual(design["one_factor_design"]["supplementary_rate_rows"], 17)
        rows = design["rows"]
        for axis, levels in coverage.AXES.items():
            for value in levels:
                self.assertTrue(any(
                    row[axis] == value and
                    all(row[key] == baseline[key] for key in coverage.AXES if key != axis) and
                    not row.get("case_name", "").startswith(("rate-at-added-symbols-1", "vlf-", "minimum-", "none-", "hf-", "aurora-", "eme-", "high-rate-"))
                    for row in rows), f"{axis}={value!r} must have a one-factor witness")
        rate_add1 = [row for row in rows if row.get("case_name") == "rate-at-added-symbols-1"]
        self.assertEqual({row["added_symbols"] for row in rate_add1}, {1})
        self.assertEqual({row["rate_hz"] for row in rate_add1}, set(coverage.RATES_HZ))
        names = {row.get("case_name") for row in design["rows"]}
        self.assertIn("vlf-maximum-margin", names)
        self.assertIn("minimum-spacing-faster-ray", names)
        self.assertIn("hf-70ms-arrival-dispersive", names)
        self.assertIn("aurora-each65-fhss-burst", names)
        self.assertEqual(protocol["status"], "protocol-frozen-no-native-or-receiver-work-run")
        self.assertEqual(protocol["scope_partition"]["reception_status_initial"], "untested")
        self.assertGreater(protocol["requested_cartesian_count"], design["row_count"] * 1_000_000)

    def test_helper_power_policy_shows_weak_pilot_at_minus60_and_cap(self):
        hf, fixed, capped = coverage.helper_power_conditions(-60.)
        self.assertAlmostEqual(hf["helper_minus_data_db"], 10.)
        self.assertAlmostEqual(hf["helper_cn0_db_hz"], -50.)
        self.assertAlmostEqual(fixed["helper_minus_data_db"], 10 * __import__('math').log10(375))
        self.assertAlmostEqual(fixed["helper_cn0_db_hz"], -34.2596873227, places=8)
        self.assertEqual(capped["helper_minus_data_db"], 90.)
        self.assertEqual(capped["helper_cn0_db_hz"], 30.)
        self.assertIn("not-independent-reception", capped["status"])

    def test_missed_hop_pattern_inventory_keeps_cycles_and_random_case(self):
        cases = {row["id"] for row in coverage.missed_hop_cases()}
        self.assertIn("end-of-cycle-plus-next-start", cases)
        self.assertIn("adjacent-middle-pair", cases)
        self.assertIn("four-dispersed-hops-one-symbol", cases)
        self.assertIn("first-hop", cases)
        self.assertIn("last-hop", cases)

    def test_necessary_bounds_do_not_claim_general_support(self):
        row = coverage._base_row()
        row.update(rate_hz=.001, data_carrier="half-rate", fhss="off", added_symbols=1)
        result = coverage.nominal_bound_diagnostics(row)
        self.assertAlmostEqual(result["fine_rate_hz"], .001 / 6.)
        self.assertAlmostEqual(result["pulse_halo_seconds"], 48_000.)
        self.assertIn("pulse-halo-exceeds-fixed-38s-helper-lead",
                      result["necessary_rejection_codes"])
        self.assertIn("helper-standard-data-center-floor-violation", result["necessary_rejection_codes"])
        self.assertIn("helper-standard-data-center-floor-violation", result["necessary_rejection_codes"])
        self.assertEqual(result["overall_support_status"], "unsupported-by-at-least-one-necessary-bound")
        row.update(data_carrier="8270-hz", rate_hz=36.)
        result = coverage.nominal_bound_diagnostics(row)
        self.assertLess(result["nominal_composite_audio_edge_hz"], 9000.)
        self.assertTrue(result["nominal_capture_support_necessary_check"])
        self.assertGreater(result["post_ddc_required_halfwidth_hz"], 400.)
        self.assertNotIn("nominal-composite-audio-spectrum-outside-18khz-capture",
                         result["necessary_rejection_codes"])
        self.assertEqual(result["complete_data_pilot_sideband_status"], "native-plan-only")

    def test_projection_freezes_controls_and_preserves_baseband_clock(self):
        row = coverage._base_row()
        row.update(rate_hz=36., data_carrier="1725-hz", rf_shift_hz=0.,
                   shift_oscillator="baseband-clock", timing_policy="bounded-10ms-1ms-0ms-audio70ms")
        args = coverage.projection(row, coverage.helper_power_conditions(20.)[0])
        for option, expected in (("--rate", "36"), ("--rf-hz", "0"),
                                 ("--shift-oscillator", "baseband-clock"),
                                 ("--timing-policy", "bounded"), ("--data-if", "1725"),
                                 ("--spread-preset", "hf-best"),
                                 ("--selective-perturbation", "off")):
            self.assertEqual(args[args.index(option) + 1], expected)
        self.assertIn("--plan-only", args)

    def test_modulation_only_and_dwell_wait_are_explicit_separate_inputs(self):
        row = coverage._base_row()
        row.update(spread_preset="none", selective_modulation="each-65",
                   selective_modulation_rate_hz=1., burst_wait_tiles=3)
        args = coverage.projection(row, coverage.helper_power_conditions(20.)[1])
        self.assertEqual(args[args.index("--selective-modulation-rate-hz") + 1], "1")
        self.assertEqual(args[args.index("--burst-wait-tiles") + 1], "3")
        self.assertNotIn("--symbol-burst", args,
                         "complete-bit Symbol Burst remains unimplemented by this exporter")

    def test_native_plan_budget_binary_identity_and_output_exclusive(self):
        row = coverage._base_row()
        with tempfile.TemporaryDirectory() as tmp:
            exporter = Path(tmp) / "native"
            exporter.write_text("native stub")
            exporter.chmod(0o755)
            with self.assertRaisesRegex(ValueError, "cap"):
                coverage.native_plan_probe([row] * (coverage.MAX_PLAN_PROBES + 1), exporter)
            skip = coverage.native_plan_probe([row, row], exporter, maximum_total_seconds=1e-9)
            self.assertEqual(skip["attempted_rows"], 0)
            self.assertEqual(skip["not_run_rows"], 2)
            self.assertEqual([entry["status"] for entry in skip["results"]],
                             ["not-run-total-runtime-budget-exhausted"] * 2)
            output = Path(tmp) / "result.json"
            output.write_text("keep\n")
            with self.assertRaises(SystemExit):
                coverage.main(["--output", str(output)])
            self.assertEqual(output.read_text(), "keep\n")

    def test_native_marginals_distinguish_geometry_from_helper_placement(self):
        row = coverage._base_row()
        stress_row = dict(row, case_name="named-stress-with-different-context")
        with tempfile.TemporaryDirectory() as tmp:
            exporter = Path(tmp) / "native"
            exporter.write_text("native stub")
            exporter.chmod(0o755)
            completed = [
                subprocess.CompletedProcess(
                    args=[str(exporter)], returncode=0,
                    stdout=json.dumps({"geometry_feasible": True,
                                       "helper_placement": {"feasible_at_requested_if": False},
                                       "export_constraints": {"pulse_halo_fits_lead": True}}),
                    stderr=""),
                subprocess.CompletedProcess(
                    args=[str(exporter)], returncode=0,
                    stdout=json.dumps({"geometry_feasible": True,
                                       "helper_placement": {"feasible_at_requested_if": True},
                                       "export_constraints": {"pulse_halo_fits_lead": True}}),
                    stderr=""),
            ]
            with mock.patch.object(coverage.subprocess, "run", side_effect=completed):
                result = coverage.native_plan_probe([row, stress_row], exporter,
                                                    maximum_total_seconds=1.)
        outcomes = result["marginal_outcomes"]["rate_hz"]["36.0"]
        self.assertEqual(outcomes["attempted_rows"], 1,
                         "named interactions must not leak into one-factor marginals")
        self.assertEqual(outcomes["geometry_feasible_rows"], 1)
        self.assertEqual(outcomes["helper_placement_feasible_rows"], 0)
        self.assertEqual(outcomes["geometry_and_helper_feasible_rows"], 0)
        self.assertEqual(outcomes["geometry_helper_and_lead_feasible_rows"], 0)
        self.assertFalse(outcomes["supported_in_sample"])

    def test_native_marginals_mark_unrepresented_symbol_burst_untested(self):
        row = dict(coverage._base_row(), symbol_burst=3)
        with tempfile.TemporaryDirectory() as tmp:
            exporter = Path(tmp) / "native"
            exporter.write_text("native stub")
            exporter.chmod(0o755)
            completed = subprocess.CompletedProcess(
                args=[str(exporter)], returncode=0,
                stdout=json.dumps({"geometry_feasible": True,
                                   "helper_placement": {"feasible_at_requested_if": True},
                                   "export_constraints": {"pulse_halo_fits_lead": True}}),
                stderr="")
            with mock.patch.object(coverage.subprocess, "run", return_value=completed):
                result = coverage.native_plan_probe([row], exporter,
                                                    maximum_total_seconds=1.)
        outcomes = result["marginal_outcomes"]["symbol_burst"]["3"]
        self.assertEqual(outcomes["unrepresented_requested_rows"], 1)
        self.assertEqual(outcomes["represented_requested_rows"], 0)
        self.assertIsNone(outcomes["supported_in_sample"])

    def test_native_marginals_distinguish_exporter_rejection_from_no_result(self):
        row = dict(coverage._base_row(), data_carrier="half-rate")
        with tempfile.TemporaryDirectory() as tmp:
            exporter = Path(tmp) / "native"
            exporter.write_text("native stub")
            exporter.chmod(0o755)
            completed = subprocess.CompletedProcess(args=[str(exporter)], returncode=1,
                                                    stdout="", stderr="DATA IF below minimum")
            with mock.patch.object(coverage.subprocess, "run", return_value=completed):
                result = coverage.native_plan_probe([row], exporter, maximum_total_seconds=1.)
        outcomes = result["marginal_outcomes"]["data_carrier"]["half-rate"]
        self.assertEqual(outcomes["native_plan_rejected_rows"], 1)
        self.assertFalse(outcomes["supported_in_sample"])
        self.assertEqual(outcomes["support_classification"],
                         "exporter-rejected-in-sampled-configuration")


if __name__ == "__main__":
    unittest.main()
