from __future__ import annotations

import importlib.util
import itertools
import math
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("triad_helper_domain", ROOT / "tools/triad_helper_domain.py")
domain = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
import sys
sys.path.insert(0, str(ROOT / "tools"))
SPEC.loader.exec_module(domain)


class HelperDomainTests(unittest.TestCase):
    def test_full_catalog_is_exactly_partitioned_without_cartesian_iteration(self):
        report = domain.build_report()
        self.assertEqual(report["requested_cartesian_count"], 2_500_485_120)
        self.assertEqual(report["factorized_projection_count"], 17 * 3 * 4 * 2)
        self.assertEqual(sum(report["mutually_exclusive_cell_counts"].values()), 2_500_485_120)
        self.assertEqual(report["status"], "necessary-constraint-census-only")
        self.assertGreater(report["mutually_exclusive_cell_counts"][domain.RESIDUAL], 0)
        self.assertEqual(set(report["native_source_identities"]), {
            "src/triad_modem_v3.cpp", "src/helper_carrier.cpp",
            "tools/triad-helper-export.cpp", "tools/triad_helper_receiver.py"})

    def test_factored_census_matches_bruteforce_on_small_axis_subset(self):
        axes = {
            "rate_hz": (.01, 36., 24_000.),
            "data_carrier": ("1725-hz", "half-rate", "8270-hz"),
            "added_symbols": (1, 10),
            "symbol_burst": (0, 3),
            "margin": (1., 3.),  # Irrelevant to this classifier; tests weighting.
        }
        brute: dict[str, int] = {}
        rows = []
        for values in itertools.product(*axes.values()):
            row = dict(zip(axes, values))
            result = domain.diagnose(domain._complete_row(row))
            brute[result["classification"]] = brute.get(result["classification"], 0) + 1
            rows.append((row, result))
        factored = domain._weighted_census(axes)
        self.assertEqual(sum(brute.values()), math.prod(map(len, axes.values())))
        self.assertEqual(factored["mutually_exclusive_cell_counts"],
                         {**{name: brute.get(name, 0) for name in (*domain.RULES, domain.RESIDUAL)}})
        self.assertGreater(brute["whole-bit-symbol-burst-unsupported"], 0)
        self.assertGreater(brute[domain.RESIDUAL], 0)
        self.assertTrue(any(r["data_carrier"] == "half-rate" and
                            "data-center-below-helper-floor" in x["secondary_necessary_failures"]
                            for r, x in rows))

    def row(self, rate=36., carrier="1725-hz", added=1, burst=0):
        row = {axis: values[0] for axis, values in domain.AXES.items()}
        row.update(rate_hz=rate, data_carrier=carrier, added_symbols=added,
                   symbol_burst=burst)
        return row

    def test_boundary_comparisons_match_documented_strictness(self):
        # Fs >= 4*fine and Fs > 1.1*occupied. The latter is the tighter
        # condition because occupied = 3.75*fine.
        boundary = 18_000 * 6 / (1.1 * 3.75)
        row = self.row(rate=boundary * (1 + 1e-12))
        result = domain.geometry_diagnostics(row["rate_hz"], 1725., 1)
        self.assertIn("sample-rate-too-low-for-pattern", result["secondary_necessary_failures"])
        row = self.row(rate=boundary * (1 - 1e-12))
        self.assertNotIn("sample-rate-too-low-for-pattern",
                         domain.geometry_diagnostics(row["rate_hz"], 1725., 1)["secondary_necessary_failures"])

        # The source origin includes ceil(8*Fs/fine)+2 samples. Even an ideal
        # 38-second pulse halo therefore misses the fixed DATA lead by 2 samples.
        exact_rate = 8 * 6 / 38
        exact = domain.geometry_diagnostics(exact_rate, 1725., 1)
        self.assertEqual(exact["pulse_halo_samples_including_two_sample_guard"], 38 * 18_000 + 2)
        self.assertIn("pulse-halo-exceeds-helper-lead", exact["secondary_necessary_failures"])
        # Three fewer ideal samples leaves the +2 guard inside the lead.
        passing_fine = 8 * 18_000 / (38 * 18_000 - 3)
        passing_rate = passing_fine * 6
        passed = domain.geometry_diagnostics(passing_rate, 1725., 1)
        self.assertEqual(passed["pulse_halo_samples_including_two_sample_guard"], 38 * 18_000 - 1)
        self.assertNotIn("pulse-halo-exceeds-helper-lead", passed["secondary_necessary_failures"])

        # Exact nominal upper Nyquist is excluded, matching the Helper passband
        # planner's >= Nyquist rejection.
        at_nyquist_rate = 23280.0  # occupied/2 = 7275 Hz; 1725+7275 = 9000.
        self.assertIn("nominal-three-carrier-band-exceeds-nyquist",
                      domain.geometry_diagnostics(at_nyquist_rate, 1725., 1)["secondary_necessary_failures"])

        # The wide DATA frontend allows equality at 400 Hz nominal half-width.
        at_frontend = domain.geometry_diagnostics(1280., 1725., 1)
        self.assertAlmostEqual(at_frontend["nominal_data_half_bandwidth_hz"], 400.)
        self.assertNotIn("nominal-data-half-bandwidth-exceeds-wide-frontend",
                         at_frontend["secondary_necessary_failures"])
        self.assertIn("nominal-data-half-bandwidth-exceeds-wide-frontend",
                      domain.geometry_diagnostics(1280.0001, 1725., 1)["secondary_necessary_failures"])

    def test_energy_reference_is_explicitly_optimistic_and_scaling_is_correct(self):
        result = domain.energy_reference(-60, 4, 40)
        self.assertEqual(result["helper_cn0_db_hz"], -50)
        self.assertAlmostEqual(result["ideal_known_waveform_presence_seconds"]["pd90_fa01_rho"],
                               6_508_500.0)
        self.assertIn("no acquisition", result["assumptions"])
        with self.assertRaises(ValueError):
            domain.known_waveform_presence_duration(-60, 0)


if __name__ == "__main__":
    unittest.main()
