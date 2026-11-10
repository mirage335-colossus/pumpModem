"""Case selection and native suite boundaries without timing a calibration."""
from pathlib import Path
import ast
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_case_suite import SCHEMA, _format, run_cases, select_cases
from triad_native import ProbeError, read_report
from tests.test_triad_cases import FIXTURE


class SuiteTests(unittest.TestCase):
    def test_markdown_preserves_a_measured_numeric_failure_with_missing_work(self):
        measured = {"realtime_status": "fail", "measured_complete_component_lower_bound": 1.2}
        self.assertEqual(_format(measured), "≥1.2 measured required work (fail, total unresolved)")

    def test_selected_indices_preserve_source_order_and_parse_every_row(self):
        parsed = {"cases": [{"index": 1}, {"index": 2}, {"index": 3}]}
        self.assertEqual(select_cases(parsed, [3, 1, 3]), [parsed["cases"][0], parsed["cases"][2]])
        for invalid in ([], [4], [True], [1.0]):
            with self.assertRaises(ProbeError):
                select_cases(parsed, invalid)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cases = root / "cases.md"
            cases.write_text(FIXTURE + "\n- Invalid unselected row\n", encoding="utf-8")
            with self.assertRaises(ValueError), patch("triad_case_suite.NativeProbe") as native:
                run_cases(executable="native", output=root / "result.json", case_list=cases, case_indices=[1])
            native.assert_not_called()

    def test_ordered_duplicate_reuse_is_opt_in_and_marked(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cases = root / "cases.md"
            cases.write_text(FIXTURE + "\n" + FIXTURE, encoding="utf-8")
            description = {"status": "complete", "identity": {"backend": "cpu"},
                           "allocation": {"hardware_threads": 3, "workers": 6, "slot_owners": 6,
                                          "receiver_numerical_threads_per_worker": 1,
                                          "memory_limit_bytes": 2**30}}
            for reuse, measurements in ((False, 2), (True, 1)):
                with patch("triad_case_suite.NativeProbe") as native, \
                        patch("triad_case_suite.measure_complete_case") as measure:
                    native.return_value.backend = "cpu"
                    native.return_value.call.return_value = description
                    measure.side_effect = lambda case, **unused: {"case": case, "status": "unresolved"}
                    result = run_cases(executable="native", output=root / "result.json", case_list=cases,
                                       message_gate=False, reuse_identical=reuse)
                self.assertEqual(measure.call_count, measurements)
                self.assertEqual([row["case"]["index"] for row in result["cases"]], [1, 2])
                self.assertEqual(result["schema"], SCHEMA)
                self.assertEqual(result["status"], "measured_with_unresolved_margins")
                self.assertEqual("reused_measurement_from" in result["cases"][1], reuse)
                self.assertEqual(read_report(root / "result.json")["status"], result["status"])

    def test_cpu_cli_import_closure_contains_only_current_orchestration(self):
        tools = Path(__file__).resolve().parents[1] / "tools"
        allowed = {"triad_manual_cases", "triad_abbreviated", "triad_case_suite", "triad_case_capacity",
                   "triad_case_metrics", "triad_cases", "triad_native", "triad_cpu"}
        pending = ["triad_manual_cases", "triad_abbreviated"]
        visited = set()
        while pending:
            module = pending.pop()
            if module in visited:
                continue
            self.assertIn(module, allowed)
            visited.add(module)
            tree = ast.parse((tools / (module + ".py")).read_text(encoding="utf-8"))
            for node in ast.walk(tree):
                if isinstance(node, ast.Import):
                    names = [alias.name for alias in node.names]
                elif isinstance(node, ast.ImportFrom) and node.module:
                    names = [node.module]
                else:
                    continue
                for name in names:
                    base = name.split(".")[0]
                    self.assertNotIn(base, {"numpy", "scipy", "torch", "cupy", "triton"})
                    if base.startswith("triad_"):
                        pending.append(base)


if __name__ == "__main__":
    unittest.main()
