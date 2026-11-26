"""Bounded comparisons with the preserved research results; no receiver runs."""
import importlib.util
import json
import math
from pathlib import Path
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class RecoveryChecks(unittest.TestCase):
    def compare(self, expected, actual, path=""):
        if isinstance(expected, dict):
            self.assertEqual(expected.keys(), actual.keys(), path)
            for key in expected:
                self.compare(expected[key], actual[key], path + "/" + key)
        elif isinstance(expected, list):
            self.assertEqual(len(expected), len(actual), path)
            for index, (left, right) in enumerate(zip(expected, actual)):
                self.compare(left, right, path + "/" + str(index))
        elif isinstance(expected, float):
            self.assertTrue(math.isclose(expected, actual, rel_tol=1e-11,
                                         abs_tol=2e-14), (path, expected, actual))
        else:
            self.assertEqual(expected, actual, path)

    def test_coverage_retained_report(self):
        module = load("broad_coverage_math", ROOT / "tools/triad_coverage_bounds.py")
        original = json.loads((HERE / "coverage-bounds.json").read_text())
        self.assertEqual(original, module.build_report(
            gaussian_widths=(.001, .005, .05, .1, 10., 55.)))

    def test_harmonic_retained_results(self):
        module = load("broad_harmonic_math", HERE / "harmonic-probe.py")
        original = json.loads((HERE / "harmonic-probe.json").read_text())
        current = module.build_report()
        current.pop("recovery_provenance")
        original.pop("elapsed_seconds")
        current.pop("elapsed_seconds")
        self.compare(original, current)

    def test_oscillator_retained_mathematics(self):
        module = load("broad_oscillator_math", HERE / "oscillator-coherence.py")
        original = json.loads((HERE / "oscillator-coherence.json").read_text())
        current = module.build_report()
        self.assertEqual(original["presets"], current["presets"])
        pairs = {
            "relative_cfo_bound_hz": "relative_cfo_bound_hz",
            "native_conservative_sample_rate_must_exceed_hz": "conservative_sample_rate_must_exceed_hz",
            "smallest_integer_rate_above_native_margin": "smallest_integer_rate_above_margin",
            "bare_850_day_samples_at_selected_rate": "bare_850_day_samples_at_selected_rate",
            "native_capture_margin_hz": "deterministic_margin_hz",
        }
        self.assertEqual(len(original["capture"]), len(current["capture_formula_reference"]))
        for old, new in zip(original["capture"], current["capture_formula_reference"]):
            self.assertEqual(old["oscillator"], new["oscillator"])
            for left, right in pairs.items():
                self.compare(old[left], new[right], left)


if __name__ == "__main__":
    unittest.main()
