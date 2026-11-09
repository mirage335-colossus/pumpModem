"""Shared manual-case helper contracts; complete-runner orchestration is tested separately."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_cases import DEFAULT_CASES
from triad_case_suite import run_cases, select_cases, target_snr, write_result
from tests.test_triad_duration_model import complete_receipt, fixture


class SuiteHelperTests(unittest.TestCase):
    def test_explicit_selection_uses_fresh_indices_and_rejects_stale_rows(self):
        parsed = dict(cases=[dict(index=1), dict(index=2), dict(index=3)])
        self.assertIs(select_cases(parsed), parsed["cases"])
        self.assertEqual(select_cases(parsed, [3, 1, 3]), [dict(index=1), dict(index=3)])
        for invalid in ([], [4], [True], [1.0]):
            with self.assertRaises(ValueError):
                select_cases(parsed, invalid)

    def test_default_complete_case_wrapper_rereads_default_path_in_new_runner(self):
        with mock.patch("triad_case_capacity.run_complete_cases", return_value="received") as runner:
            result = run_cases(executable="exporter", output="capacity.json")
        self.assertEqual(result, "received")
        runner.assert_called_once_with(
            executable="exporter", output="capacity.json", case_list=DEFAULT_CASES,
            measurement_seconds=1., threads=None, case_indices=None,
            paired_followup=False, message_gate=True,
        )

    def test_wrapper_forwards_partial_measurement_and_message_gate(self):
        with mock.patch("triad_case_capacity.run_complete_cases") as runner:
            run_cases(executable=Path("exporter"), output=Path("out.json"),
                case_list=Path("edited.md"), measurement_seconds=.5, threads=6,
                case_indices=[2, 4], message_gate=False)
        runner.assert_called_once_with(
            executable=Path("exporter"), output=Path("out.json"), case_list=Path("edited.md"),
            measurement_seconds=.5, threads=6, case_indices=[2, 4],
            paired_followup=False, message_gate=False,
        )

    def test_legacy_coarse_paired_followup_is_rejected_by_complete_runner(self):
        from triad_case_capacity import run_complete_cases

        with self.assertRaisesRegex(ValueError, "coarse-only paired confirmation"):
            run_complete_cases(executable="unused", output="unused.json", case_list=DEFAULT_CASES,
                               paired_followup=True)

    def test_write_result_replaces_json_without_leaving_a_temporary_file(self):
        import json

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "result.json"
            write_result(path, {"status": "complete", "rows": [1, 2]})
            self.assertEqual(json.loads(path.read_text()), {"status": "complete", "rows": [1, 2]})
            self.assertFalse(path.with_suffix(".json.tmp").exists())

    def test_duration_model_is_reported_only_as_a_provisional_target(self):
        plan = fixture(False)
        plan.update(geometry={"active_data_seconds": 11.6}, short_message={"encoded_bits": 22})
        receipt = complete_receipt(plan)
        result = target_snr(plan, receipt)
        self.assertFalse(result["whole_bit_or_message_probability_demonstrated"])
        self.assertEqual(result["status"], "provisional_model_target")
        self.assertEqual(result["complete_admission_receipt"], receipt)
        self.assertEqual(result["fox"]["source_bits"], 22)
        self.assertGreater(result["fox"]["target_cn0_db_hz"], result["single_bit"]["target_cn0_db_hz"])
        self.assertLess(result["fox"]["bracket_db_hz"][1]-result["fox"]["bracket_db_hz"][0], .001)

    def test_duration_target_rejects_missing_or_infeasible_upper_endpoint(self):
        plan = fixture(False)
        plan.update(geometry={"active_data_seconds": 11.6}, short_message={"encoded_bits": 22})
        receipt = complete_receipt(plan)
        self.assertEqual(target_snr(plan)["status"], "unavailable")
        for prediction in ({"selected": None}, {"selected": {"meets_detection_allocation": False}}):
            with mock.patch("triad_duration_model.plan_from_native", return_value=prediction):
                result = target_snr(plan, receipt)
                self.assertEqual(result["status"], "unavailable")
                self.assertNotIn("single_bit", result)

if __name__ == "__main__":
    unittest.main()
