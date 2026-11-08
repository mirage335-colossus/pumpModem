import hashlib
from pathlib import Path
import tempfile
import unittest

from tools.triad_cases import CaseParseError, FIELDS, ROOT, load_cases


SOURCE = ROOT / "_tests-RobustModem.md"
FIXTURE = '- Parser fixture\nNo\tEncryption\n0 W\tHelper Carrier Transmit Power\n1 mW\tTransmit Power\n200 dB\tPath loss\nDefault several seconds receiver search space\tClock sync / Region / Offset / Audio error\nGPSDO OCXO\tBase Oscillator\nBaseband clock\tShift Oscillator\nSub9 best 0.001Hz 0.005ms\tDoppler/delay spread\nOff\tAdditional frequency selective amplitude modulation\n1x\tMargin\nOff\tDSSS\n0x\tSpreading\nOff\tFHSS\nOff\tSymbol_burst\n0.1 Hz\tRate\n0 Hz\tShift\n'


class TriadCasesTests(unittest.TestCase):
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
