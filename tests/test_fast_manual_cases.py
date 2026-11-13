from contextlib import redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import fast_manual_cases as cases  # noqa: E402


HF = """Yes\tEncryption
40 W\tTransmit Power
130 dB\tPath loss
Free-running crystal\tBase Oscillator
IC-7100 +/-0.5 ppm/radio\tShift Oscillator
HF worst 30Hz 7ms\tDoppler/delay spread
Each - 65%\tAdditional frequency selective amplitude modulation
3x\tMargin
30 MHz\tShift
Channel profile IC-7100 SSB 2.4 kHz
"""
EME = """Yes\tEncryption
150 kW\tTransmit Power
180 dB\tPath loss
GPSDO OCXO\tBase Oscillator
GPSDO OCXO\tShift Oscillator
EME focused 6Hz 0.5ms\tDoppler/delay spread
Each - 65%\tAdditional frequency selective amplitude modulation
1x\tMargin
5.8 GHz\tShift
Channel profile IC-7100 SSB 2.4 kHz
"""


class FastManualCaseParserTests(unittest.TestCase):
    def test_cases_normalize_in_order_and_list_preserves_duplicate_rows(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "cases.md"
            path.write_text(HF + "\n" + EME + "\n" + HF, encoding="utf-8")
            text = io.StringIO()
            with redirect_stdout(text):
                self.assertEqual(cases.main(["--case-list", str(path), "--list"]), 0)
            parsed = json.loads(text.getvalue())["selected_cases"]
        self.assertEqual([row["index"] for row in parsed], [1, 2, 3])
        self.assertEqual([row["base_oscillator"] for row in parsed], ["crystal", "gpsdo-ocxo", "crystal"])
        self.assertEqual([row["shift_oscillator"] for row in parsed], ["ic-7100", "gpsdo-ocxo", "ic-7100"])
        self.assertEqual([row["spread"] for row in parsed], ["hf-worst", "eme-focused", "hf-worst"])
        self.assertEqual([row["shift_hz"] for row in parsed], [30_000_000, 5_800_000_000, 30_000_000])
        self.assertEqual([row["noise_dbm_hz"] for row in parsed], [-174, -174, -174])
        self.assertEqual([row["margin"] for row in parsed], [3, 1, 3])
        self.assertTrue(all(row["encryption"] and row["selective_slow_depth"] == row["selective_fast_depth"] == .65
                            for row in parsed))

    def test_duplicate_field_is_rejected(self):
        text = HF.replace("130 dB\tPath loss", "130 dB\tTransmit Power", 1)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "cases.md"
            path.write_text(text, encoding="utf-8")
            with self.assertRaises(cases.CaseParseError):
                cases.load_cases(path)

    def test_unknown_or_missing_field_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "cases.md"
            path.write_text(HF.replace("Each - 65%", "Each - 105%", 1), encoding="utf-8")
            with self.assertRaises(cases.CaseParseError):
                cases.load_cases(path)
            path.write_text(HF.replace("30 MHz\tShift\n", "", 1), encoding="utf-8")
            with self.assertRaises(cases.CaseParseError):
                cases.load_cases(path)

    def test_nonfinite_loss_and_shift_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "cases.md"
            for old, new in (("130 dB", "nan dB"), ("30 MHz", "inf MHz")):
                with self.subTest(value=new):
                    path.write_text(HF.replace(old, new), encoding="utf-8")
                    with self.assertRaises(cases.CaseParseError):
                        cases.load_cases(path)

    def test_process_timeout_preserves_other_case_outcome(self):
        good = {"schema": "fast_channel_probe.v1", "status": "complete", "live": False,
                "complete": True, "physical_complete": True, "encrypted": True,
                "authenticated": True, "exact_bytes": True, "receiver_backend": "resilient-ssb"}
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "cases.md"
            output = Path(temp) / "report.json"
            probe = Path(temp) / "probe"
            probe.write_bytes(b"native-probe fixture")
            path.write_text(HF + "\n" + EME, encoding="utf-8")
            with patch.object(subprocess, "run", side_effect=(
                subprocess.TimeoutExpired("probe", 1),
                subprocess.CompletedProcess("probe", 0, json.dumps(good), ""),
            )) as run:
                report = cases.run_cases(probe=probe, case_list=path, output=output)
            self.assertEqual(report, json.loads(output.read_text(encoding="utf-8")))
            self.assertEqual(report["probe_path"], str(probe.resolve()))
            self.assertEqual(report["probe_sha256"], hashlib.sha256(probe.read_bytes()).hexdigest())
            self.assertEqual([row["argv"] for row in report["results"]], [call.args[0] for call in run.call_args_list])
            self.assertTrue(all("--resilient-receiver" not in row["argv"] for row in report["results"]))
            for row in report["results"]:
                self.assertEqual(row["argv"][row["argv"].index("--max-audio-seconds") + 1], "75.0")
                requested = row["requested_case"]
                declaration = row["requested_receiver_assumptions"]
                effects = row["requested_channel_effects"]
                self.assertNotIn("noise_dbm_hz", declaration)
                self.assertEqual(effects["noise_dbm_hz"], -174)
                self.assertNotIn("--expected-snr", row["argv"])
                for name in ("base_oscillator", "shift_oscillator", "spread", "selective_slow_depth",
                             "selective_fast_depth", "margin", "shift_hz"):
                    self.assertEqual(declaration[name], requested[name])
                    self.assertEqual(effects[name], 1 if name == "margin" else requested[name])
                    actual_option = "--" + name.replace("_", "-")
                    assumed_option = "--assume-" + name.replace("_", "-")
                    self.assertEqual(row["argv"].count(assumed_option), 1)
                    self.assertEqual(row["argv"][row["argv"].index(assumed_option) + 1], str(declaration[name]))
                    self.assertEqual(row["argv"][row["argv"].index(actual_option) + 1], str(effects[name]))
            self.assertIn("receiver-only", report["results"][0]["margin_semantics"])
            self.assertNotIn("margin_semantics", report["results"][1])
        self.assertEqual(report["status"], "partial")
        self.assertFalse(report["selected_cases_complete"])
        self.assertEqual([row["status"] for row in report["results"]], ["process_timeout", "complete"])

    def test_independent_selective_depths_are_declared_without_changing_effects(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "cases.md"
            path.write_text(HF.replace("Each - 65%", "Slow - 65%") + "\n" +
                            EME.replace("Each - 65%", "Fast - 65%"), encoding="utf-8")
            with patch.object(cases, "_run_probe", side_effect=lambda *args: {"status": "incomplete"}):
                report = cases.run_cases(probe=Path(sys.executable), case_list=path,
                                         output=Path(temp) / "report.json")
        for row, slow, fast in zip(report["results"], (.65, 0), (0, .65)):
            declaration = row["requested_receiver_assumptions"]
            self.assertEqual(declaration["selective_slow_depth"], slow)
            self.assertEqual(declaration["selective_fast_depth"], fast)
            for prefix in ("--", "--assume-"):
                argv = row["argv"]
                self.assertEqual(float(argv[argv.index(prefix + "selective-slow-depth") + 1]), slow)
                self.assertEqual(float(argv[argv.index(prefix + "selective-fast-depth") + 1]), fast)

    def test_receiver_bound_rejection_is_preserved_without_legacy_retry(self):
        rejected = {"schema": "fast_channel_probe.v1", "status": "error",
                    "reason": "Fast receiver assumptions exceed the fixed frontend envelope"}
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "cases.md"
            path.write_text(HF, encoding="utf-8")
            with patch.object(subprocess, "run", return_value=
                              subprocess.CompletedProcess("probe", 2, json.dumps(rejected), "")) as run:
                report = cases.run_cases(probe=Path(sys.executable), case_list=path,
                                         output=Path(temp) / "report.json")
        self.assertEqual(run.call_count, 1)
        self.assertEqual(report["status"], "incomplete")
        self.assertFalse(report["selected_cases_complete"])
        row = report["results"][0]
        self.assertEqual((row["status"], row["returncode"], row["reason"]),
                         ("error", 2, rejected["reason"]))
        self.assertEqual(row["requested_receiver_assumptions"]["margin"], 3)
        self.assertEqual(row["requested_case"]["margin"], 3)
        self.assertEqual(row["requested_channel_effects"]["margin"], 1)
        self.assertEqual(row["argv"][row["argv"].index("--assume-margin") + 1], "3")
        self.assertEqual(row["argv"][row["argv"].index("--margin") + 1], "1")

    def test_missing_probe_preserves_case_error_and_argv(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "cases.md"
            probe = Path(temp) / "missing"
            path.write_text(HF, encoding="utf-8")
            report = cases.run_cases(probe=probe, case_list=path, output=Path(temp) / "report.json")
        self.assertIsNone(report["probe_sha256"])
        self.assertIn("probe_sha256_error", report)
        self.assertEqual(report["results"][0]["status"], "probe_start_error")
        self.assertEqual(report["results"][0]["argv"][0], str(probe.resolve()))

    def test_short_ssb_gate_checks_geometry_and_validated_receipt(self):
        with tempfile.TemporaryDirectory() as temp:
            case_list = Path(temp) / "cases.md"
            case_list.write_text(HF, encoding="utf-8")
            settings = {"probe": Path(sys.executable), "case_list": case_list,
                        "output": Path(temp) / "report.json", "bootstrap_gate": True}
            with self.assertRaises(cases.CaseParseError):
                cases.run_cases(**settings, profile_overrides={"ldpc_bits": 64800})
            with self.assertRaises(cases.CaseParseError):
                cases.run_cases(**settings, profile_overrides={"ldpc_bits": 1944})
            with patch.object(cases, "_run_probe", return_value={"status": "complete",
                              "receiver_backend": "resilient-ssb"}):
                report = cases.run_cases(**settings, profile_overrides={"ldpc_bits": 16200})
        row = report["results"][0]
        self.assertTrue(report["bootstrap_gate"])
        self.assertFalse(report["selected_cases_complete"])
        self.assertEqual(row["status"], "bootstrap_mismatch")
        self.assertIn("--bootstrap-gate", row["argv"])
        self.assertIn("--profile-override", row["argv"])
        self.assertEqual(row["argv"][row["argv"].index("--ldpc-bits") + 1], "16200")

    def test_retention_is_single_live_case_and_separate_from_report(self):
        with tempfile.TemporaryDirectory() as temp:
            case_list = Path(temp) / "cases.md"
            case_list.write_text(HF + "\n" + EME, encoding="utf-8")
            report_path = Path(temp) / "report.json"
            capture_path = Path(temp) / "capture.f32le"
            settings = {"probe": Path(sys.executable), "case_list": case_list, "output": report_path}
            with self.assertRaises(cases.CaseParseError):
                cases.run_cases(**settings, retain_capture=capture_path, case_indices=[2])
            with self.assertRaises(cases.CaseParseError):
                cases.run_cases(**settings, live_device="default", retain_capture=capture_path)
            with self.assertRaises(cases.CaseParseError):
                cases.run_cases(**settings, live_device="default", retain_capture=report_path, case_indices=[2])
            with patch.object(cases, "_run_probe", return_value={"status": "audio_error"}):
                report = cases.run_cases(**settings, live_device="default", retain_capture=capture_path, case_indices=[2])
        self.assertEqual(report["retain_capture"], str(capture_path.resolve()))
        row = report["results"][0]
        self.assertEqual(row["status"], "audio_error")
        self.assertEqual(row["argv"][row["argv"].index("--retain-capture") + 1], str(capture_path.resolve()))

    def test_verified_bootstrap_is_a_diagnostic_result(self):
        with tempfile.TemporaryDirectory() as temp:
            case_list = Path(temp) / "cases.md"
            case_list.write_text(HF, encoding="utf-8")
            settings = {"probe": Path(sys.executable), "case_list": case_list,
                        "output": Path(temp) / "report.json", "bootstrap_gate": True,
                        "bootstrap_only": True}
            receipt = {"status": "bootstrap_verified", "returncode": 0, "live": False, "encrypted": True,
                       "receiver_backend": "resilient-ssb", "bootstrap_only": True, "bootstrap_validated": True,
                       "intervals": 8, "ldpc_frames": 1, "ldpc_failed_frames": 0, "bootstrap_nonfinite_llrs": 0,
                       "capture_nonfinite_samples": 0, "digital_nonfinite_samples": 0,
                       "complete": False, "physical_complete": False}
            with patch.object(cases, "_run_probe", return_value=receipt.copy()):
                report = cases.run_cases(**settings)
            with patch.object(cases, "_run_probe", return_value={**receipt, "bootstrap_nonfinite_llrs": 1}):
                failed = cases.run_cases(**settings)
        self.assertTrue(report["selected_bootstraps_validated"])
        self.assertFalse(report["selected_cases_complete"])
        self.assertIn("--bootstrap-only", report["results"][0]["argv"])
        self.assertFalse(failed["selected_bootstraps_validated"])
        self.assertEqual(failed["results"][0]["status"], "bootstrap_mismatch")


if __name__ == "__main__":
    unittest.main()
