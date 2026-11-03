import importlib.util
import itertools
import json
import pathlib
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("triad_coverage", ROOT / "tools/triad-coverage.py")
coverage = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(coverage)


class BroadCoverageTests(unittest.TestCase):
    def test_full_cartesian_space_count_and_stable_lazy_ids(self):
        self.assertEqual(coverage.product_count(), 2_500_485_120)
        primary_axes = {name: values for name, values in coverage.AXES.items()
                        if name not in {"margin", "added_symbols", "fhss", "symbol_burst"}}
        self.assertEqual(coverage.product_count(primary_axes), 78_140_160)
        self.assertEqual(coverage.row_id(coverage.mixed_radix_row(0)), 0)
        last = coverage.product_count() - 1
        self.assertEqual(coverage.row_id(coverage.mixed_radix_row(last)), last)
        first = coverage.mixed_radix_row(0)
        self.assertEqual(first["rate_hz"], .001)
        self.assertEqual(first["shift_hz"], 0.0)
        self.assertEqual(coverage.mixed_radix_row(last)["cn0_db_hz"], -60.0)

    def test_slice_is_exact_and_does_not_materialize_the_space(self):
        plan = coverage.build_plan(123456, 4, False)
        self.assertEqual(plan["selected_count"], 4)
        self.assertEqual([row["row_id"] for row in plan["rows"]], list(range(123456, 123460)))
        self.assertEqual(plan["rows"][0]["requested"], coverage.mixed_radix_row(123456))
        with self.assertRaises(ValueError):
            coverage.build_plan(coverage.product_count(), 1, False)
        with self.assertRaises(ValueError):
            coverage.build_plan(0, coverage.MAX_MATERIALIZED_ROWS + 1, False)

    def test_pairwise_plan_explicitly_covers_every_axis_value_pair(self):
        rows = coverage.pairwise_rows()
        report = coverage.pair_coverage(rows)
        self.assertTrue(report["complete"], report["missing_by_axis_pair"])
        self.assertEqual(report["covered_pairs"], report["total_pairs"])
        self.assertEqual(report["total_pairs"], sum(
            len(a) * len(b) for a, b in itertools.combinations(coverage.AXES.values(), 2)))
        plan = coverage.build_plan(0, 3, True)
        self.assertEqual(plan["selected_count"], 3)
        self.assertEqual(plan["pairwise_universe_coverage"]["covered_pairs"], report["total_pairs"])
        self.assertLess(plan["selected_row_pair_coverage"]["covered_pairs"], report["total_pairs"])
        full_plan = coverage.build_plan(0, None, True)
        self.assertEqual(full_plan["selected_count"], len(rows))
        self.assertEqual(full_plan["row_count"], len(rows))
        self.assertEqual(full_plan["selected_row_pair_coverage"]["covered_pairs"], report["total_pairs"])

    def test_unimplemented_coordinates_are_retained_as_unsupported(self):
        row = coverage.mixed_radix_row(0)
        row["selective"] = "each-65"
        row["fhss"] = "envelope-only"
        row["symbol_burst"] = "3x"
        projected = coverage.project_configuration(row)
        unsupported = {item["dimension"]: item["reason"]
                       for item in projected["unsupported_dimensions"]}
        self.assertIn("shift_hz", unsupported)
        self.assertIn("baseband-clock", unsupported["shift_oscillator"])
        self.assertEqual(projected["frontend_interpretation"]["status"],
                         "approximate_reference_mapping_unvalidated_as_physical_carrier_placement")
        self.assertEqual(projected["configuration_probe_status"], "not_run")
        self.assertEqual(projected["reception_status"], "untested")
        self.assertNotIn("--rf-hz", projected["cli_projection"])
        self.assertEqual(projected["cli_projection"][projected["cli_projection"].index("--nominal-if-hz") + 1], "1725")
        self.assertNotIn("--shift-oscillator", projected["cli_projection"])
        row["baseband_carrier"] = "half-rate"
        self.assertEqual(coverage.baseband_hz(row), coverage.RATES[0] / 2)
        self.assertTrue(any("bypassed" in warning for warning in projected["configuration_warnings"]))
        self.assertTrue(any("missed RF hops" in warning for warning in projected["configuration_warnings"]))
        self.assertTrue(any("backlog" in warning for warning in projected["configuration_warnings"]))

    def test_representable_projection_preserves_every_mapped_value(self):
        row = coverage.mixed_radix_row(0)
        row.update({
            "rate_hz": 36.0,
            "baseband_carrier": "1725-Hz",
            "shift_hz": 30_000_000.0,
            "timing": "gps-10ms-1ms-0ms-audio70ms",
            "base_oscillator": "gpsdo-ocxo",
            "shift_oscillator": "ic-7100-0.5ppm",
            "spread": "hf-worst",
            "selective": "each-65",
            "cn0_db_hz": 8.0,
            "margin": 3.0,
            "added_symbols": 10,
            "fhss": "envelope-only",
            "symbol_burst": "3x",
        })
        args = coverage.project_configuration(row)["cli_projection"]
        for pair in (("--rate", "36"), ("--added", "10"), ("--rf-hz", "30000000"),
                     ("--base-oscillator", "gpsdo-ocxo"), ("--shift-oscillator", "ic-7100"),
                     ("--spread-preset", "hf-worst"), ("--selective-perturbation", "each-65"),
                     ("--margin", "3"), ("--nominal-if-hz", "1725"),
                     ("--audio-error-ms", "70"), ("--symbol-burst", "3")):
            self.assertEqual(args[args.index(pair[0]) + 1], pair[1])
        self.assertEqual(args[args.index("--start") + 1:args.index("--start") + 4],
                         ["-0.090500000001", "0.090500000001", "0.004166666666666667"])
        self.assertIn("--fhss", args)

    def test_probe_refuses_missing_native_binary_and_keeps_unsupported_rows(self):
        row = coverage.mixed_radix_row(0)
        item = {"row_id": 0, "requested": row}
        with self.assertRaises(ValueError):
            coverage.native_probe([item], pathlib.Path("/no/such/triad_long"), 1, 1)

    def test_default_search_maps_actual_arrival_and_candidate_bank(self):
        row = coverage.mixed_radix_row(0)
        row["timing"] = "default-plus-minus-7.035s-search"
        args = coverage.project_configuration(row)["cli_projection"]
        self.assertEqual(args[args.index("--start") + 1:args.index("--start") + 4],
                         ["-7.035", "7.035", "0.004166666666666667"])
        self.assertEqual(args[args.index("--arrival-prior-seconds") + 1:],
                         ["-7.035", "7.035"])

    def test_geometry_capture_and_search_results_are_separate(self):
        stdout = json.dumps({"mode": "configure", "configuration": {
            "capture": {"feasible_for_deterministic_prior": False},
            "public_search": {
                "covers_initial_timing_prior": False,
                "covers_static_oscillator_frequency_prior": False,
                "covers_audio_clock_prior": True,
                "covers_suggested_epochs": False,
                "timing_bounds_seconds": [-7.035, 7.035],
                "estimated_hypotheses": 12,
                "materialization_cap": 10,
            },
            "search_estimates": {
                "timing_maximum_step_seconds": 0.0041666667,
                "timing_loss_budget_db": 0.5,
                "timing_model_is_finite_waveform_certificate": False,
            },
        }})
        accepted = coverage.normalize_configure_result(0, stdout, "", 9, ["triad_long"])
        self.assertEqual(accepted["source_geometry_acceptance"], "accepted")
        self.assertEqual(accepted["capture_support"], "infeasible")
        self.assertFalse(accepted["capture_feasible_for_deterministic_prior"])
        self.assertFalse(accepted["public_search_coverage"]["covers_initial_timing_prior"])
        self.assertTrue(accepted["public_search_coverage"]["covers_audio_clock_prior"])
        self.assertEqual(accepted["search_materialization_status"], "exceeds_declared_cap")
        self.assertEqual(accepted["search_grid_diagnostics"]["timing_loss_budget_db"], 0.5)
        self.assertFalse(accepted["search_grid_diagnostics"]["timing_model_is_finite_waveform_certificate"])
        invalid = coverage.normalize_configure_result(0, "not-json", "", 10, ["triad_long"])
        self.assertEqual(invalid["status"], "invalid_output")
        self.assertEqual(invalid["source_geometry_acceptance"], "unknown")
        rejected = coverage.normalize_configure_result(2, "", "geometry error", 11, ["triad_long"])
        self.assertEqual(rejected["source_geometry_acceptance"], "rejected")

    def test_probe_invalidates_binary_changed_during_run(self):
        request = coverage.mixed_radix_row(0)
        request.update(shift_hz=500_000.0, shift_oscillator="gpsdo-ocxo")
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / "triad_long"
            binary.write_bytes(b"initial binary identity")
            output = json.dumps({"mode": "configure", "configuration": {
                "capture": {"feasible_for_deterministic_prior": True},
                "public_search": {"covers_initial_timing_prior": True},
            }})
            def mutate(*args, **kwargs):
                binary.write_bytes(b"changed while probing")
                return coverage.subprocess.CompletedProcess(args[0], 0, output, "")
            with mock.patch.object(coverage.subprocess, "run", side_effect=mutate):
                result = coverage.native_probe([{"row_id": 1, "requested": request}], binary, 2, 1)
        self.assertFalse(result["identity_stable"])
        self.assertFalse(result["comparison_valid"])
        self.assertNotEqual(result["binary_sha256_before"], result["binary_sha256_after"])

    def test_cli_refuses_to_overwrite_existing_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "existing.json"
            output.write_text("original\n")
            with self.assertRaises(SystemExit):
                coverage.main(["--output", str(output)])
            self.assertEqual(output.read_text(), "original\n")


if __name__ == "__main__":
    unittest.main()
