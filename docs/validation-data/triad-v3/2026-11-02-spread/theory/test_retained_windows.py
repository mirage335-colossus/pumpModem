"""Reproduce the two surviving public-design J64 results, without a receiver."""
import json
import math
from pathlib import Path
import unittest

import actual_window_information as calculation


HERE = Path(__file__).resolve().parent


class RetainedWindowChecks(unittest.TestCase):
    def compare_result(self, expected, actual, location):
        if isinstance(expected, bool):
            self.assertEqual(expected, actual, location)
        elif isinstance(expected, (int, float)):
            self.assertTrue(math.isclose(expected, actual, rel_tol=5e-12,
                                         abs_tol=1e-280), location)
        elif isinstance(expected, dict):
            self.assertEqual(expected.keys(), actual.keys(), location)
            for key in expected:
                self.compare_result(expected[key], actual[key], location + "/" + key)
        elif isinstance(expected, list):
            self.assertEqual(len(expected), len(actual), location)
            for index, (left, right) in enumerate(zip(expected, actual)):
                self.compare_result(left, right, location + "/" + str(index))
        else:
            self.assertEqual(expected, actual, location)

    def test_two_retained_public_designs_match_original_j64_results(self):
        reference = json.loads((HERE / "fixtures/information/g001.json").read_text())
        for name in ("path-00000", "path-00001"):
            with self.subTest(design=name):
                expected = next(row for row in reference["workspaces"]
                    if Path(row["public_design"]["workspace"]).name == name)
                actual = calculation.analyze(
                    HERE / "fixtures/screen/g001" / name,
                    reference["cn0_grid_db_hz"], [64],
                    reference["doppler_width_2sigma_hz"],
                    powers=reference["tap_powers"],
                    inversion=tuple(reference["inversion_interval_db_hz"]),
                    comparison_cn0=reference["comparison_cn0_db_hz_external"])
                self.assertEqual(expected["public_design"]["public_source_sha256"],
                                 actual["public_design"]["public_source_sha256"])
                old_rows = next(row for row in expected["window_results"]
                                if row["window_blocks"] == 64)["candidate_results"]
                new_rows = actual["window_results"][0]["candidate_results"]
                self.assertEqual(len(old_rows), len(new_rows))
                for index, (old, new) in enumerate(zip(old_rows, new_rows)):
                    # Solver rounding may change an eigenvalue byte hash; compare
                    # its trace and every KL value/crossing rather than that hash.
                    for key in ("candidate", "covariance_eigenvalue_count",
                                "covariance_trace_reference_variance_one",
                                "cn0_evaluations", "necessary_cn0_crossings"):
                        self.compare_result(old[key], new[key], f"{name}/{index}/{key}")


if __name__ == "__main__":
    unittest.main()
