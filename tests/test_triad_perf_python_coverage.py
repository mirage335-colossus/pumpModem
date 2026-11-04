"""Focused parser and orchestration checks for triad_perf_python_coverage.py."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "triad_perf_python_coverage.py"
SPEC = importlib.util.spec_from_file_location("triad_perf_python_coverage", SCRIPT)
coverage_runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(coverage_runner)


class CoverageReportTests(unittest.TestCase):
    def test_standalone_cli_check_count_accepts_multiline_json(self):
        self.assertEqual(coverage_runner.test_count('{\n  "passed": 7,\n  "checks": {"a": {}, "b": {}}\n}'), 7)

    def test_parser_binds_lines_and_arcs_to_exact_source_hash(self):
        source = ROOT / "tools" / "triad_gaussian_state.py"
        document = {"files": {str(source.resolve()): {
            "executed_lines": [1, 2],
            "missing_lines": [3],
            "executed_branches": [[2, 3]],
            "missing_branches": [[2, 4]],
            "summary": {"num_statements": 3, "covered_lines": 2,
                        "num_branches": 2, "covered_branches": 1},
        }}}
        result = coverage_runner.summarize_json(
            document, {"triad_gaussian_state": source}, ROOT)
        self.assertEqual(list(result), ["tools/triad_gaussian_state.py"])
        row = result["tools/triad_gaussian_state.py"]
        self.assertEqual(row["lines"], {"executable": 3, "covered": 2, "missing": [3]})
        self.assertEqual(row["branches"], {
            "executable_arcs": 2, "covered_arcs": 1, "missing_arcs": [[2, 4]]})
        self.assertEqual(row["sha256"], coverage_runner.sha256(source))

    def test_parser_rejects_source_identity_mismatch(self):
        source = ROOT / "tools" / "triad_gaussian_state.py"
        with self.assertRaisesRegex(coverage_runner.CoverageRunError, "exact source identity"):
            coverage_runner.summarize_json({"files": {}}, {"triad_gaussian_state": source}, ROOT)


class CoverageOrchestrationTests(unittest.TestCase):
    def fixture_repo(self, repo, test_name="test_fixture", test_body=None):
        (repo / "tools").mkdir()
        (repo / "tests").mkdir()
        (repo / "docs").mkdir()
        module = "triad-coverage-fixture"
        target = repo / "tools" / f"{module}.py"
        target.write_text("def select(value):\n    if value:\n        return 1\n    return 0\n", encoding="utf-8")
        (repo / "tools" / "triad-helper.py").write_text("HELPER_VERSION = 1\n", encoding="utf-8")
        (repo / "tools" / "triad-noise-study.py").write_text("STUDY_VERSION = 1\n", encoding="utf-8")
        (repo / "docs" / "guide.md").write_text("declared precision contract\n", encoding="utf-8")
        (repo / "tests" / "test_triad_noise_study.py").write_text("FIXTURE_VERSION = 1\n", encoding="utf-8")
        body = test_body or (
            "import importlib.util\nfrom pathlib import Path\nimport unittest\n"
            f"spec = importlib.util.spec_from_file_location('fixture', Path(__file__).resolve().parents[1] / 'tools' / '{module}.py')\n"
            "fixture = importlib.util.module_from_spec(spec)\nspec.loader.exec_module(fixture)\n"
            "class Example(unittest.TestCase):\n"
            "    def test_true_branch(self):\n"
            "        self.assertEqual(fixture.select(True), 1)\n"
            "if __name__ == '__main__':\n    unittest.main()\n")
        (repo / "tests" / f"{test_name}.py").write_text(body, encoding="utf-8")
        matrix = {
            "coverage_reports": {"python": {"source_files": [f"tools/{module}.py"]}},
            "rows": [{"source_files": [f"tools/{module}.py", "tools/triad-helper.py", "docs/guide.md"],
                      "cheap_functional_tests": [f"tests/{test_name}.py::Example::test_true_branch",
                                                 "tests/test_triad_noise_study.py::FIXTURE_VERSION"]}],
        }
        matrix_path = repo / "tools" / "coverage-matrix.json"
        matrix_path.write_text(json.dumps(matrix), encoding="utf-8")
        targets = coverage_runner.resolve_targets(repo, [f"{module}.py"])
        tests = coverage_runner.selected_tests(repo, [test_name])
        return module, target, matrix_path, targets, tests

    def test_runs_selected_unittest_and_retains_branch_json(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            module, target, matrix, targets, tests = self.fixture_repo(repo)
            result, exit_code, raw_json = coverage_runner.run_coverage(
                repo, repo / "raw", targets, tests, matrix_path=matrix)
            self.assertEqual(exit_code, 0, result["test_runs"])
            self.assertIn("Ran 1 test", result["test_runs"][0]["output"])
            raw = json.loads(Path(raw_json).read_text(encoding="utf-8"))
            row = result["sources"][f"tools/{module}.py"]
            self.assertGreater(row["branches"]["executable_arcs"], 0)
            self.assertGreater(len(row["branches"]["missing_arcs"]), 0)
            self.assertIn(str(target.resolve()), raw["files"])
            identity = result["input_identity"]
            self.assertEqual(identity["status"], "stable")
            self.assertEqual(identity["matrix_sha256_pre"], identity["matrix_sha256_post"])
            self.assertIn("tools/triad-helper.py", result["input_identity"]["dependency_sha256"])
            self.assertIn("docs/guide.md", result["input_identity"]["dependency_sha256"])
            self.assertIn("tests/test_triad_noise_study.py", identity["test_source_sha256_pre"])
            self.assertEqual(identity["test_source_sha256_pre"], identity["test_source_sha256_post"])

    def test_changed_declared_dependency_fails_capture_integrity(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            body = (
                "import importlib.util\nfrom pathlib import Path\nimport unittest\n"
                "root = Path(__file__).resolve().parents[1]\n"
                "spec = importlib.util.spec_from_file_location('fixture', root / 'tools' / 'triad-coverage-fixture.py')\n"
                "fixture = importlib.util.module_from_spec(spec)\nspec.loader.exec_module(fixture)\n"
                "class Example(unittest.TestCase):\n"
                "    def test_true_branch(self):\n"
                "        self.assertEqual(fixture.select(True), 1)\n"
                "        (root / 'tools' / 'triad-helper.py').write_text('HELPER_VERSION = 2\\n')\n"
                "if __name__ == '__main__':\n    unittest.main()\n")
            module, _, matrix, targets, tests = self.fixture_repo(repo, "test_mutates", body)
            result, exit_code, _ = coverage_runner.run_coverage(
                repo, repo / "raw", targets, tests, matrix_path=matrix)
            self.assertEqual(exit_code, 2)
            self.assertEqual(result["input_identity"]["status"], "changed_during_capture")
            self.assertIn("tools/triad-helper.py", result["input_identity"]["changed_inputs"])
            self.assertEqual(result["test_exit_code"], 2)

    def test_missing_module_fails_before_test_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            (repo / "tools").mkdir()
            (repo / "tests").mkdir()
            (repo / "tests" / "test_empty.py").write_text("", encoding="utf-8")
            with self.assertRaisesRegex(coverage_runner.CoverageRunError, "missing target module"):
                coverage_runner.resolve_targets(repo, ["missing_triad_runtime_module"])

            # The CLI also exits with a setup failure, never a misleading empty report.
            output = repo / "report.json"
            with patch("sys.stderr") as stderr:
                status = coverage_runner.main([
                    "--repo-root", str(repo), "--raw-dir", str(repo / "raw"),
                    "--output", str(output), "--module", "missing_triad_runtime_module",
                    "--test", "test_empty",
                ])
            self.assertEqual(status, 2)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
