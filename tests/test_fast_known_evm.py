"""Independent mapping, misleading-decision-EVM and geometry controls."""
import importlib.util
import json
from pathlib import Path
import unittest

import numpy as np

SPEC = importlib.util.spec_from_file_location("fast_known_evm", Path(__file__).resolve().parents[1] / "tools/fast_known_evm.py")
ANALYSIS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYSIS)


def fixture(order=256, intervals=8):
    bits = np.random.default_rng(703).integers(0, 2, intervals * 2048, dtype=np.uint8)
    report = dict(format="capacity", waveform="single-carrier", apsk=order, mode="raw",
                  tx_intervals=intervals, rx_intervals=intervals, acquired=True,
                  physical_end=True, raw_erased_bits=0, alignment_offsets={"0": intervals})
    return report, bits, ANALYSIS.expected_symbols(bits, order).ravel()


class KnownEvmTests(unittest.TestCase):
    def test_independent_gray_points(self):
        labels = np.array([0, 1, 2, 3, 4, 15])
        expected = np.array([-3-3j, -3-1j, -3+3j, -3+1j, -1-3j, 1+1j]) / np.sqrt(10)
        np.testing.assert_allclose(ANALYSIS.labels_to_points(labels, 16), expected, atol=1e-15)
        np.testing.assert_allclose(ANALYSIS.labels_to_points([4194303], 4194304),
                                   [(683+683j)/np.sqrt(2796202)], atol=1e-15)

    def test_interval_padding_does_not_bleed_into_next_interval(self):
        bits = np.zeros(4096, np.uint8)
        bits[2046:2048] = 1
        bits[2048] = 1
        expected = ANALYSIS.expected_symbols(bits, 64)
        self.assertEqual(expected.shape, (2, 342))
        np.testing.assert_allclose(expected[0, -1], ANALYSIS.labels_to_points([48], 64)[0])
        np.testing.assert_allclose(expected[1, 0], ANALYSIS.labels_to_points([32], 64)[0])

    def test_padding_errors_are_not_payload_bit_errors(self):
        report, bits, expected = fixture(order=64, intervals=2)
        actual = expected.reshape(2, 342).copy()
        # The final 6-bit label contains only two source bits. Change exactly
        # one of its four padding bits, leaving the source untouched.
        labels = ANALYSIS._nearest_labels(actual[:, -1], 64)
        actual[:, -1] = ANALYSIS.labels_to_points(labels ^ 1, 64)
        result, rows = ANALYSIS.analyze(report, bits, actual.ravel())
        self.assertEqual(result["summary"]["nearest_label_mismatches"], 2)
        self.assertEqual(result["summary"]["known_payload_bit_errors"], 0)
        self.assertTrue(all(row["payload_bit_errors"] == 0 for row in rows))

    def test_rotation_shows_why_nearest_decision_evm_is_not_snr(self):
        report, bits, expected = fixture()
        result, _ = ANALYSIS.analyze(report, bits, 1j*expected)
        metrics = result["summary"]
        self.assertAlmostEqual(metrics["known_evm"], np.sqrt(2), places=13)
        self.assertLess(metrics["nearest_decision_evm"], 1e-14)
        self.assertEqual(metrics["nearest_label_mismatches"], expected.size)
        self.assertLess(metrics["after_global_complex_gain_evm"], 1e-14)
        self.assertAlmostEqual(metrics["phase_degrees"], 90, places=12)
        json.dumps(result, allow_nan=False)

    def test_gain_fit_and_heldout_noise_scale(self):
        report, bits, expected = fixture(intervals=24)
        random = np.random.default_rng(707)
        noise = .025*(random.normal(size=expected.size)+1j*random.normal(size=expected.size))/np.sqrt(2)
        gain = .91*np.exp(.07j)
        result, rows = ANALYSIS.analyze(report, bits, gain*expected+noise)
        self.assertAlmostEqual(result["summary"]["gain_magnitude"], .91, delta=.001)
        self.assertAlmostEqual(result["heldout_global_gain"]["evm"], .025/.91, delta=.001)
        self.assertEqual(len(rows), 24)

    def test_nonlinearity_survives_one_gain_fit(self):
        report, bits, expected = fixture(order=1024)
        compressed = .4*np.tanh(expected.real/.4)+.4j*np.tanh(expected.imag/.4)
        result, _ = ANALYSIS.analyze(report, bits, compressed)
        self.assertGreater(result["summary"]["after_global_complex_gain_evm"], .15)
        self.assertGreater(result["heldout_global_gain"]["evm"], .15)

    def test_missing_observations_and_bad_geometry_rejected(self):
        report, bits, expected = fixture()
        for name, value in (("raw_erased_bits", 1), ("rx_intervals", 7),
                            ("physical_end", False), ("fifo_overflow", True),
                            ("waveform", "ofdm"), ("alignment_offsets", {"1": 8})):
            with self.subTest(name=name), self.assertRaises(ValueError):
                ANALYSIS.analyze(dict(report, **{name: value}), bits, expected)
        with self.assertRaises(ValueError):
            ANALYSIS.analyze(report, bits, expected[:-1])
        with self.assertRaises(ValueError):
            ANALYSIS.analyze(report, bits[:-1], expected)


if __name__ == "__main__":
    unittest.main()
