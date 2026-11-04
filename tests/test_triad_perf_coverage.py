#!/usr/bin/env python3
"""Focused parser and CLI contract checks for triad_perf_coverage.py."""

import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "triad_perf_coverage.py"
SPEC = importlib.util.spec_from_file_location("triad_perf_coverage", SCRIPT)
coverage = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(coverage)


class GcovParserTests(unittest.TestCase):
    def test_source_identity_is_sha256_of_exact_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.cpp"
            source.write_bytes(b"int value = 7;\n")
            self.assertEqual(coverage.sha256(source), hashlib.sha256(b"int value = 7;\n").hexdigest())

    def test_counts_source_lines_and_gcov_branch_outcomes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src" / "triad.cpp"
            source.parent.mkdir()
            source.write_text("fixture\n")
            raw = root / "source.gcov.json.gz"
            document = {
                "current_working_directory": str(root),
                "files": [{"file": "src/triad.cpp", "lines": [
                    {"line_number": 10, "count": 4, "branches": [
                        {"count": 4}, {"count": 0}, {"count": -1}]},
                    {"line_number": 11, "count": 0, "branches": []},
                    {"line_number": 12, "count": -3, "branches": []},
                ]}],
            }
            with gzip.open(raw, "wt", encoding="utf-8") as stream:
                json.dump(document, stream)
            result = coverage.parse_gcov_json(raw, source)
        self.assertEqual(result["lines"], {
            "instrumented": 2, "executed": 1, "uncovered_source_lines": [11],
            "invalid_negative_counts": {"12": [-3]},
            "coverage_status": "unqualified_negative_line_counters"})
        self.assertEqual(result["gcov_branches"], {
            "instrumented_outcomes": 3, "taken_outcomes": 1, "untaken_outcomes": 1,
            "invalid_negative_outcomes": 1,
            "coverage_status": "unqualified_negative_branch_counters"})

    def test_rejects_same_basename_from_another_checkout(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected = root / "checkout-a" / "src" / "one.cpp"
            wrong = root / "checkout-b" / "src" / "one.cpp"
            expected.parent.mkdir(parents=True)
            wrong.parent.mkdir(parents=True)
            expected.write_text("expected source\n")
            wrong.write_text("different checkout\n")
            raw = root / "ambiguous.gcov.json.gz"
            with gzip.open(raw, "wt", encoding="utf-8") as stream:
                json.dump({"current_working_directory": str(root),
                           "files": [{"file": str(wrong)}]}, stream)
            with self.assertRaisesRegex(ValueError, "resolving to .*checkout-a.*one.cpp, got 0"):
                coverage.parse_gcov_json(raw, expected)

    def test_branch_interpretation_names_compiler_edge_limit(self):
        self.assertIn("do not establish semantic", coverage.BRANCH_LIMITATION)
        self.assertIn("exception-related", coverage.BRANCH_LIMITATION)

    def test_shared_header_counters_merge_by_line_and_cfg_edge_across_records(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = root / "include" / "shared.hpp"
            header.parent.mkdir()
            header.write_text("inline int f(bool x) { return x ? 1 : 0; }\n")
            header_hash = coverage.sha256(header)
            def record(count, no_count, line_count):
                return {"files": [{"file": str(header), "lines": [
                    {"line_number": 1, "function_name": "f", "count": count,
                     "branches": [{"count": count, "source_block_id": 1,
                                   "destination_block_id": 2, "fallthrough": True,
                                   "throw": False},
                                  {"count": no_count, "source_block_id": 1,
                                   "destination_block_id": 3, "fallthrough": False,
                                   "throw": False}]},
                    {"line_number": 2, "function_name": "g", "count": line_count,
                     "branches": []},
                ]}]}

            result = coverage.aggregate_header_documents(root, [
                ("tu-a.cpp", record(2, 0, 0)), ("tu-b.cpp", record(0, -1, -2))])
        row = result["include/shared.hpp"]
        self.assertEqual(row["lines"], {
            "instrumented": 2, "executed": 1, "uncovered_source_lines": [2],
            "invalid_negative_counts": {"2": [-2]},
            "coverage_status": "unqualified_negative_line_counters"})
        self.assertEqual(row["gcov_branches"], {
            "instrumented_outcomes": 4, "taken_outcomes": 1, "untaken_outcomes": 2,
            "invalid_negative_outcomes": 1,
            "coverage_status": "unqualified_negative_branch_counters",
            "unit": "translation-unit-qualified gcov CFG edge outcomes; no cross-TU edge equivalence asserted"})
        self.assertEqual(row["gcov_records"], ["tu-a.cpp", "tu-b.cpp"])
        self.assertEqual(row["sha256_at_capture"], header_hash)

    def test_dependency_inventory_follows_native_rows_and_binds_tests_headers_and_cmake(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = {
                "src/native.cpp": "native\n",
                "src/helper.cpp": "helper\n",
                "src/native.hpp": "header\n",
                "tests/native_test.cpp": "test\n",
                "src/other.cpp": "other\n",
                "CMakeLists.txt": "project(test)\n",
                "cmake/native.cmake": "set(x 1)\n",
                "tools/triad-performance-coverage.json": json.dumps({
                    "coverage_reports": {"native": {"source_files": ["src/native.cpp"]}},
                    "rows": [
                        {"source_files": ["src/native.cpp", "src/helper.cpp"],
                         "cheap_functional_tests": ["tests/native_test.cpp::receiver"]},
                        {"source_files": ["src/other.cpp"], "cheap_functional_tests": []},
                    ],
                }),
            }
            for name, value in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(value)
            self.assertEqual(coverage.coverage_source_list(root), ("src/native.cpp",))
            first = coverage.dependency_inventory(root, ("src/native.cpp",))
            self.assertEqual(set(first), {
                "src/native.cpp", "src/helper.cpp", "src/native.hpp", "tests/native_test.cpp",
                "CMakeLists.txt", "cmake/native.cmake",
            })
            (root / "tests/native_test.cpp").write_text("changed test\n")
            second = coverage.dependency_inventory(root, ("src/native.cpp",))
            self.assertNotEqual(first["tests/native_test.cpp"], second["tests/native_test.cpp"])

    def test_final_guard_check_rejects_mutation_during_collection(self):
        guarded = {"src/native.cpp": "before", "include/native.hpp": "header-before",
                   "tests/native_test.cpp": "test-before"}
        after_gcov = dict(guarded)
        after_gcov["tests/native_test.cpp"] = "test-after"
        with self.assertRaisesRegex(ValueError, "tests/native_test.cpp"):
            coverage.require_inventory_match(guarded, after_gcov, "declared dependencies")
        after_gcov = dict(guarded)
        after_gcov["include/native.hpp"] = "header-after"
        with self.assertRaisesRegex(ValueError, "include/native.hpp"):
            coverage.require_inventory_match(guarded, after_gcov, "declared dependencies")


if __name__ == "__main__":
    unittest.main()
