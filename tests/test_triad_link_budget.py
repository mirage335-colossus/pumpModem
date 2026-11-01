import importlib.util
import math
from pathlib import Path
import unittest

from scipy.stats import norm


SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "triad-link-budget.py"
SPEC = importlib.util.spec_from_file_location("triad_link_budget", SCRIPT)
budget = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(budget)


class TriadLinkBudgetTests(unittest.TestCase):
    def test_default_power_and_noise_sweep_unit_conversions(self):
        report = budget.build_report()
        self.assertAlmostEqual(report["inputs"]["radiated_power_dbm"], 0.0)
        self.assertAlmostEqual(report["inputs"]["received_power_dbm"], -200.0)
        self.assertEqual([row["cn0_db_hz"] for row in report["noise_sweep"]],
                         [-36.0, -46.0, -56.0])
        for row in report["noise_sweep"]:
            expected = row["cn0_db_hz"] + 10 * math.log10(38 * 86400)
            self.assertAlmostEqual(row["nominal_energy_available_db"], expected)

    def test_tenfold_useful_duration_changes_energy_by_ten_db(self):
        short = budget.build_report(days=1.0)["noise_sweep"][0]
        long = budget.build_report(days=10.0)["noise_sweep"][0]
        self.assertAlmostEqual(long["nominal_energy_available_db"] -
                               short["nominal_energy_available_db"], 10.0)

    def test_known_waveform_probability_tail_and_family_count(self):
        target = budget.required_energy(.99, 1e-4, 86400, methods=11, durations=4)
        self.assertEqual(target["scheduled_attempts"], 1440)
        self.assertAlmostEqual(target["per_hypothesis_false_tail"],
                               (.5e-4) / (1440 * 11 * 4 * 2))
        expected = .5 * (norm.isf(target["per_hypothesis_false_tail"]) +
                         norm.ppf(.99)) ** 2
        self.assertAlmostEqual(target["required_known_waveform_E_N0_linear"], expected)
        achieved = norm.cdf(math.sqrt(2 * expected) -
                            norm.isf(target["per_hypothesis_false_tail"]))
        self.assertAlmostEqual(achieved, .99)

    def test_fraction_scales_useful_duration_and_elapsed_time(self):
        report = budget.build_report(days=38, useful_energy_fraction=.25)
        self.assertEqual(report["inputs"]["elapsed_seconds"], 38 * 86400)
        self.assertEqual(report["inputs"]["useful_full_power_equivalent_seconds"],
                         38 * 86400 * .25)
        row = report["noise_sweep"][0]
        self.assertAlmostEqual(row["nominal_energy_available_db"],
                               -36 + 10 * math.log10(38 * 86400 * .25))

    def test_invalid_inputs_rejected(self):
        for kwargs in ({"radiated_power_w": 0}, {"radiated_power_w": math.inf},
                       {"days": -1}, {"path_loss_db": math.nan},
                       {"useful_energy_fraction": 0},
                       {"useful_energy_fraction": 1.1},
                       {"noise_densities_dbm_hz": ()}, {"methods": 0},
                       {"durations": 1.5}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                budget.build_report(**kwargs)
        with self.assertRaises(ValueError):
            budget.required_energy(1.0, 1e-4, 100)

    def test_finite_inputs_that_overflow_or_underflow_derived_values_fail_closed(self):
        # Finite power itself is accepted in log-domain, but the resulting
        # nominal linear E/N0 is outside the representable range.
        for kwargs in ({"radiated_power_w": 1e308},
                       {"radiated_power_w": 5e-324},
                       {"path_loss_db": -1e308},
                       {"noise_densities_dbm_hz": (-1e308,)},
                       {"days": 1e308},
                       {"methods": 1e308},
                       {"methods": 10**1000},
                       {"methods": budget.MAX_FAMILY_ALTERNATIVES + 1}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                budget.build_report(**kwargs)
        with self.assertRaises(ValueError):
            budget.required_energy(.99, 5e-324, 1e308)
        with self.assertRaises(ValueError):
            budget.required_energy(.99, 1e-4, 1e308, 1, 1, 1, 1e-300)


if __name__ == "__main__":
    unittest.main()
