#!/usr/bin/env python3
"""Regression checks for externally decoded records, without modem execution."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("competitor_runner", Path(__file__).with_name("run.py"))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class ComparatorRecords(unittest.TestCase):
    def test_ft8_exact_record(self):
        result = runner.outcomes("ft8", "000001 -14  0.0 1500 ~  HELLO   \n<DecodeFinished> 0 1 0\n", "HELLO", True)
        self.assertTrue(result["correct_payload_found"])
        self.assertEqual(result["decoded_records"], ["HELLO"])

    def test_ft8_extended_or_debug_text_is_not_success(self):
        for text in ("000001 -14 0.0 1500 ~ HELLOX\n", "debug expected HELLO\n", "<DecodeFinished> HELLO\n"):
            with self.subTest(text=text):
                self.assertFalse(runner.outcomes("ft8", text, "HELLO", True)["correct_payload_found"])

    def test_wspr_exact_record(self):
        text = "0001 -26 0.1 10.001500 0 K2HFA FN31 30\n<DecodeFinished>\n"
        self.assertTrue(runner.outcomes("wspr", text, "K2HFA FN31 30", True)["correct_payload_found"])
        self.assertFalse(runner.outcomes("wspr", text.replace("30", "33"), "K2HFA FN31 30", True)["correct_payload_found"])

    def test_block_noise_records_count_without_success(self):
        result = runner.outcomes("ft8", "000001 -14 0.0 1500 ~ HELLO\n", "HELLO", False)
        self.assertFalse(result["correct_payload_found"])
        self.assertEqual(result["false_decoded_records_on_noise"], 1)

    def test_stream_payload_and_extra_characters_remain_distinct(self):
        result = runner.outcomes("olivia8-250", "junkHELLO\nmore", "HELLO", True)
        self.assertTrue(result["correct_payload_found"])
        self.assertEqual(result["extra_nonwhitespace_stream_characters"], 8)
        self.assertFalse(result["physical_completion_evaluated"])

    def test_status_only_is_not_a_decode(self):
        result = runner.outcomes("wspr", "<DecodeFinished>\n", "K2HFA FN31 30", False)
        self.assertEqual(result["false_decoded_records_on_noise"], 0)


if __name__ == "__main__":
    unittest.main()
