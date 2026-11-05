import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "triad_helper_ramp_diagnostic", ROOT / "tools/triad_helper_ramp_diagnostic.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class RampDiagnosticTests(unittest.TestCase):
    def test_band_integral_and_validation(self):
        f = np.arange(-5, 6, dtype=float)
        p = np.ones_like(f)
        self.assertEqual(MODULE.band_power(f, p, -2, 2), 3.0)
        with self.assertRaises(ValueError):
            MODULE.band_power(f, p, 2, -2)
        with self.assertRaises(ValueError):
            MODULE.band_power(f, p, 0.1, 0.2)

    def test_six_ms_ramp_preserves_one_centered_symbol(self):
        geometry = MODULE.protected_dwell_geometry({
            "fine_chip_rate_hz": 60, "dwell_seconds": .4, "quiet_seconds": .055,
            "ramp_seconds": .006, "margin_seconds": .005,
            "fine_coefficients": 64, "inner_coefficients": 64, "bit_seconds": 26.4,
        })
        self.assertEqual(geometry["fine_center_indices_per_dwell"], [12])
        self.assertEqual(geometry["protected_centers_per_dwell"], 1)
        self.assertEqual(geometry["payload_coefficients"], 64)

    def test_matched_sampled_exports_recover_data_component(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            fs, n = 1024.0, 8192
            rng = np.random.default_rng(14)
            noise = (rng.normal(size=n) + 1j * rng.normal(size=n)) * .01
            t = np.arange(n) / fs
            helper = np.exp(2j * np.pi * 300 * t)
            data = .2 * np.exp(2j * np.pi * 100 * t)
            rows = []
            for name, data_on, helper_on, observed in (
                    ("selected", True, True, data + helper + noise),
                    ("data", True, False, data + noise),
                    ("helper", False, True, helper + noise)):
                folder = base / name
                folder.mkdir()
                (folder / "observation.c128").write_bytes(observed.astype("<c16").tobytes())
                (folder / "validity.u8").write_bytes(bytes([1]) * n)
                manifest = {
                    "schema": "triad-helper-public-v1", "case": "eme", "sample_rate_hz": fs,
                    "public_epoch_seconds": 1700000000, "key_seed": 11,
                    "observation": {"file": "observation.c128", "dtype": "<c16", "shape": [n]},
                    "validity": {"file": "validity.u8", "shape": [n]},
                    "data_enabled": data_on, "helper_enabled": helper_on,
                    "format": {"inner_rate_hz": 360, "added_symbols": 1, "fine_chip_rate_hz": 60,
                               "carrier_spacing_hz": 75, "occupied_bandwidth_hz": 100,
                               "fine_coefficients": 64, "inner_coefficients": 64,
                               "bit_seconds": 26.4, "dwell_seconds": .4,
                               "quiet_seconds": .055, "ramp_seconds": .006, "margin_seconds": .005},
                    "helper_plan": {"high_hz": 650, "low_hz": 550},
                    "channel": {"nominal_conversion_if_hz": 400, "rf_hz": 5800000000,
                                "spread_preset": "eme-focused", "spread_margin": 1,
                                "spread_delay_ms": .5, "spread_doppler_hz": 6,
                                "selective_model": "dispersive-dft-v1", "selective_slow_depth": .65,
                                "selective_fast_depth": .65},
                    "power": {"complex_noise_variance": .0002 * fs},
                    "calendar": {"capture_lead_seconds": .2, "capture_trail_seconds": .3},
                }
                (folder / "manifest.json").write_text(json.dumps(manifest))
                (folder / "evaluator-truth.json").write_text(json.dumps({
                    "random_seed_roots": {"noise": 1},
                    "source_energy": {"helper_energy_samples": float(n),
                                      "data_energy_samples": float(np.vdot(data, data).real if data_on else 0)},
                }))
                rows.append(folder)
            result = MODULE.describe_triplet(*rows)
            self.assertAlmostEqual(result["actual_energy_samples"]["matched_subtraction_data_component_full_tail"],
                                   float(np.vdot(data, data).real), places=8)
            self.assertEqual(result["helper_lines_relative_to_data_center_hz"], {"low": 150.0, "high": 250.0})
            self.assertEqual(result["waveform_geometry"]["ramp_seconds"], .006)

    def test_rejects_unmatched_seed_roots(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            fs, n = 1024, 4096
            t = np.arange(n) / fs
            rows = []
            for name, data_on, helper_on, seed in (
                    ("selected", True, True, 1), ("data", True, False, 1), ("helper", False, True, 2)):
                folder = base / name
                folder.mkdir()
                (folder / "observation.c128").write_bytes(np.zeros(n, dtype="<c16").tobytes())
                (folder / "validity.u8").write_bytes(bytes([1]) * n)
                manifest = {"case": "eme", "sample_rate_hz": fs, "public_epoch_seconds": 1,
                            "key_seed": 1, "observation": {"file": "observation.c128", "dtype": "<c16", "shape": [n]},
                            "validity": {"file": "validity.u8", "shape": [n]}, "data_enabled": data_on,
                            "helper_enabled": helper_on, "format": {"inner_rate_hz": 360, "added_symbols": 1,
                            "fine_chip_rate_hz": 60, "carrier_spacing_hz": 75, "occupied_bandwidth_hz": 100,
                            "fine_coefficients": 64, "inner_coefficients": 64,
                            "bit_seconds": 26.4, "dwell_seconds": .4,
                            "quiet_seconds": .055, "ramp_seconds": .006, "margin_seconds": .005},
                            "helper_plan": {"high_hz": 650, "low_hz": 550},
                            "channel": {"nominal_conversion_if_hz": 400, "rf_hz": 5800000000,
                                        "spread_preset": "eme-focused", "spread_margin": 1,
                                        "spread_delay_ms": .5, "spread_doppler_hz": 6,
                                        "selective_model": "dispersive-dft-v1", "selective_slow_depth": .65,
                                        "selective_fast_depth": .65},
                            "power": {"complex_noise_variance": 0.1}, "calendar": {}}
                truth = {"random_seed_roots": {"noise": seed}, "source_energy":
                         {"helper_energy_samples": 1, "data_energy_samples": 1}}
                (folder / "manifest.json").write_text(json.dumps(manifest))
                (folder / "evaluator-truth.json").write_text(json.dumps(truth))
                rows.append(folder)
            with self.assertRaisesRegex(ValueError, "stochastic roots"):
                MODULE.describe_triplet(*rows)


if __name__ == "__main__":
    unittest.main()
