import hashlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
from contextlib import redirect_stdout

from tools.triad_cases import CaseParseError, FIELDS, ROOT, load_cases

sys.path.insert(0, str(ROOT / "tools"))
import triad_abbreviated
import triad_manual_cases


SOURCE = ROOT / "_tests-RobustModem.md"
FIXTURE = '- Parser fixture\nNo\tEncryption\n0 W\tHelper Carrier Transmit Power\n1 mW\tTransmit Power\n200 dB\tPath loss\nDefault several seconds receiver search space\tClock sync / Region / Offset / Audio error\nGPSDO OCXO\tBase Oscillator\nBaseband clock\tShift Oscillator\nSub9 best 0.001Hz 0.005ms\tDoppler/delay spread\nOff\tAdditional frequency selective amplitude modulation\n1x\tMargin\nOff\tDSSS\n0x\tSpreading\nOff\tFHSS\nOff\tSymbol_burst\n0.1 Hz\tRate\n0 Hz\tShift\n'


class TriadCasesTests(unittest.TestCase):
    @mock.patch("triad_case_suite.run_cases")
    def test_dedicated_manual_cli_forwards_absolute_capacity_arguments_once(self, run_cases):
        code = triad_manual_cases.main([
            "--case-list", "current-manual.md", "--output", "capacity.json",
            "--exporter", "exporter", "--measurement-seconds", "1.5",
            "--threads", "6", "--case-index", "3", "--case-index", "5",
            "--skip-message-gate", "--workspace-mib", "4096",
        ])
        self.assertEqual(code, 2)  # No current-source functional gate was requested.
        run_cases.assert_called_once_with(
            executable=Path("exporter"), output=Path("capacity.json"),
            case_list=Path("current-manual.md"), measurement_seconds=1.5,
            threads=6, case_indices=[3, 5], message_gate=False, sustained_allocation_probe=False,
            workspace_mib=4096, full_logical_inference=None,
        )

    @mock.patch("triad_case_suite.run_cases")
    def test_full_logical_opt_in_preserves_full_list_and_message_gate(self, run_cases):
        triad_manual_cases.main(["--output", "capacity.json", "--full-logical-inference",
                                 "--sustained-allocation-probe"])
        args = run_cases.call_args.kwargs
        self.assertTrue(args["full_logical_inference"])
        self.assertTrue(args["message_gate"])
        self.assertTrue(args["sustained_allocation_probe"])
        self.assertIsNone(args["case_indices"])

    @mock.patch("triad_case_suite.run_cases")
    def test_pilot_winner_remains_an_explicit_opt_out(self, run_cases):
        triad_manual_cases.main(['--output','capacity.json','--no-full-logical-inference'])
        self.assertIs(run_cases.call_args.kwargs['full_logical_inference'],False)

    def test_manual_cli_help_labels_capacity_and_separates_kernel_regression(self):
        output = io.StringIO()
        with redirect_stdout(output), self.assertRaises(SystemExit) as raised:
            triad_manual_cases.main(["--help"])
        self.assertEqual(raised.exception.code, 0)
        help_text = " ".join(output.getvalue().split())
        self.assertIn("Absolute local CPU and workspace capacity", help_text)
        self.assertIn("not the kernel degradation regression suite", help_text)
        self.assertIn("--case-list", help_text)
        self.assertIn("--threads", help_text)
        self.assertIn("--sustained-allocation-probe", help_text)
        self.assertNotIn("--baseline", help_text)

    @mock.patch("triad_case_suite.run_cases",return_value={'status':'stage_calibration_measured'})
    def test_manual_cli_can_request_only_a_selected_noise_calibration(self, run):
        result = triad_manual_cases.main(['--output','noise.json','--calibration-stage','noise-only-fine',
                                          '--case-index','7','--workspace-mib','4608'])
        self.assertEqual(result,0)
        self.assertEqual(run.call_args.kwargs['calibration_stage'],'noise-only-fine')
        self.assertEqual(run.call_args.kwargs['case_indices'],[7])
        self.assertEqual(run.call_args.kwargs['workspace_mib'],4608)

    def test_noise_calibration_cannot_opt_out_of_its_full_logical_budget(self):
        with redirect_stdout(io.StringIO()),self.assertRaises(SystemExit) as raised:
            triad_manual_cases.main(['--output','noise.json','--calibration-stage','noise-only-fine',
                '--case-index','7','--workspace-mib','4608','--no-full-logical-inference'])
        self.assertEqual(raised.exception.code,2)

    @mock.patch("triad_manual_cases.main", return_value=2)
    def test_legacy_cases_option_forwards_once_to_dedicated_entrypoint(self, manual_main):
        code = triad_abbreviated.main([
            "--output", "capacity.json", "--cases", "current-manual.md",
            "--exporter", "exporter", "--probe-seconds", "1.5", "--threads", "6",
            "--case-index", "3", "--skip-message-gate",
        ])
        self.assertEqual(code, 2)
        manual_main.assert_called_once_with([
            "--case-list", "current-manual.md", "--output", "capacity.json",
            "--exporter", "exporter", "--measurement-seconds", "1.5",
            "--threads", "6", "--case-index", "3", "--skip-message-gate",
        ])

    def test_legacy_manual_mode_rejects_explicit_kernel_regression_options(self):
        for option, value in (("--baseline", "baseline.json"),
                              ("--device", "cpu"),
                              ("--target-units", "1000"),
                              ("--target-units=1000", None),
                              ("--dev", "cpu"),
                              ("--target-u=1000", None)):
            args = ["--output", "capacity.json", "--cases", "manual.md", option]
            if value is not None:
                args.append(value)
            with self.subTest(option=option), self.assertRaises(SystemExit) as raised:
                triad_abbreviated.main(args)
            self.assertEqual(raised.exception.code, 2)

    @mock.patch("triad_abbreviated.run", return_value={"status": "baseline_established", "kernels": {}})
    def test_default_invocation_remains_the_kernel_regression_entrypoint(self, regression):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "result.json"
            code = triad_abbreviated.main(["--output", str(output)])
            self.assertEqual(code, 0)
        regression.assert_called_once_with(target_units=1000, device="cpu", baseline=None)
    def test_current_manual_cases_are_consumed_without_frozen_inventory(self):
        result = load_cases()
        self.assertTrue(result["cases"])
        self.assertEqual(result["source_sha256"], hashlib.sha256(SOURCE.read_bytes()).hexdigest())
        self.assertEqual(len({c["id"] for c in result["cases"]}), len(result["cases"]))

    def test_duplicate_entries_are_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manual.md"
            path.write_text(FIXTURE + "\n" + FIXTURE, encoding="utf-8")
            cases = load_cases(path)["cases"]
        self.assertEqual(len(cases), 2)
        self.assertEqual(cases[0]["configuration_hash"], cases[1]["configuration_hash"])
        self.assertNotEqual(cases[0]["id"], cases[1]["id"])

    def test_parser_reads_edited_file_each_call_and_accepts_space_delimiter(self):
        original = FIXTURE
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manual.md"
            path.write_text(original, encoding="utf-8")
            first = load_cases(path)
            edited = original.replace("0.1 Hz\tRate", "2 Hz  Rate", 1)
            path.write_text(edited, encoding="utf-8")
            second = load_cases(path)
            self.assertNotEqual(first["source_sha256"], second["source_sha256"])
            self.assertEqual(second["cases"][0]["fields"]["Rate"], 2)

    def test_unit_conversions_and_argument_mapping(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "eme.md"
            path.write_text(FIXTURE.replace("0 W\tHelper", "15 kW\tHelper")
                .replace("1 mW\tTransmit", "40 W\tTransmit")
                .replace("0 Hz\tShift", "5.8 GHz\tShift")
                .replace("Sub9 best 0.001Hz 0.005ms", "EME focused 6Hz 0.5ms")
                .replace("Off\tAdditional", "Each - 65%\tAdditional"))
            case = load_cases(path)["cases"][0]
        fields = case["fields"]
        args = case["native_args"]
        self.assertEqual(fields["Helper Carrier Transmit Power"], 15000)
        self.assertEqual(fields["Transmit Power"], 40)
        self.assertEqual(fields["Shift"], 5.8e9)
        self.assertEqual(fields["Doppler/delay spread"], "eme-focused")
        self.assertEqual(fields["Additional frequency selective amplitude modulation"], "each-65")
        self.assertEqual(args[args.index("--rf-hz") + 1], "5800000000")
        self.assertEqual(args[args.index("--helper-power-w") + 1], "15000")
        self.assertEqual(args[args.index("--added-symbols") + 1], "1")
        self.assertEqual(args[args.index("--added-interleave") + 1], "off")

    def test_all_fields_required_and_raw_source_lines_preserved(self):
        original = FIXTURE
        line = "Off\tSymbol_burst\n"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manual.md"
            path.write_text(original.replace(line, "", 1), encoding="utf-8")
            with self.assertRaisesRegex(CaseParseError, r"line 1: missing fields: Symbol_burst"):
                load_cases(path)
        result = load_cases()
        self.assertEqual(len(FIELDS), 16)  # The current source format has sixteen labels.
        self.assertEqual(set(result["cases"][0]["raw_fields"]), set(FIELDS))
        self.assertIn("Encryption", result["cases"][0]["field_lines"])

    def test_unknown_duplicate_and_invalid_values_fail_with_line_numbers(self):
        original = FIXTURE
        mutations = (
            (original.replace("No\tEncryption", "Maybe\tEncryption", 1), "Encryption must be Yes or No"),
            (original.replace("No\tEncryption", "No\tMystery", 1), "unknown field: 'Mystery'"),
            (original.replace("No\tEncryption", "No\tEncryption\nYes\tEncryption", 1), "duplicate field: Encryption"),
            (original.replace("0.1 Hz\tRate", "nan Hz\tRate", 1), "invalid Rate"),
            (original.replace("Off\tSymbol_burst", "On\tSymbol_burst", 1), "unsupported Symbol_burst"),
            (original.replace("Off\tFHSS", "FCC\tFHSS", 1), "unknown FHSS setting"),
            (original.replace("GPSDO OCXO\tBase Oscillator", "mystery\tBase Oscillator", 1), "unknown Base Oscillator"),
            (original.replace("0 W\tHelper Carrier Transmit Power", "inf W\tHelper Carrier Transmit Power", 1), "invalid Helper Carrier"),
            (original.replace("1x\tMargin", "0x\tMargin", 1), "Margin must be"),
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manual.md"
            for contents, message in mutations:
                path.write_text(contents, encoding="utf-8")
                with self.subTest(message=message), self.assertRaisesRegex(CaseParseError, r"line \d+: .*" + message):
                    load_cases(path)

    def test_unknown_non_field_content_is_not_silently_skipped(self):
        original = FIXTURE
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manual.md"
            path.write_text(original.replace("No\tEncryption", "unexpected text\nNo\tEncryption", 1), encoding="utf-8")
            with self.assertRaisesRegex(CaseParseError, r"line 2: field line must"):
                load_cases(path)


if __name__ == "__main__":
    unittest.main()
