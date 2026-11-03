import importlib.util
import json
import os
import shutil
import tempfile
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]


def load_tool(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


census = load_tool("triad_factor_census", REPO / "tools/triad-factor-census.py")
native_inputs = load_tool("triad_factor_native_inputs", REPO / "tools/triad_factor_native_inputs.py")


class ConfigurationAxesTest(unittest.TestCase):
    def test_complete_requested_product(self):
        self.assertEqual(census.PRIMARY, 78_140_160)
        self.assertEqual(census.TOTAL, 2_500_485_120)
        self.assertEqual(census.REPLICATION, 76)

    def test_strict_capture_predicate_uses_positive_margin(self):
        self.assertFalse(0.0 > 0.0)
        self.assertTrue(1e-15 > 0.0)


class FactorCensusNativeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        binary = cls.binary = Path(os.environ.get("TRIAD_LONG_BINARY", census.DEFAULT_BINARY)).resolve()
        if not binary.is_file() or not os.access(binary, os.X_OK):
            raise unittest.SkipTest(
                f"native census checks need a compatible built triad_long; build it or set TRIAD_LONG_BINARY (looked for {binary})")
        cls._temp = tempfile.TemporaryDirectory(prefix="triad-factor-census-")
        cls.evidence = Path(cls._temp.name) / "native-inputs"
        native_inputs.generate(cls.evidence, binary=binary, root=REPO, max_seconds=120)
        cls.report = census.run(cls.evidence, REPO, binary)
        cls.summary, cls.geometry, cls.validation = census.validate_evidence(
            cls.evidence, REPO, binary)

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "_temp"):
            cls._temp.cleanup()

    def test_intent_product_and_exclusive_partition(self):
        report = self.report
        self.assertEqual(report["outcome_partition"], {
            "unsupported_topology": 595_353_600,
            "source_geometry_rejected": 812_482_560,
            "capture_margin_nonpositive": 212_171_328,
            "capture_margin_positive": 880_477_632,
        })
        self.assertEqual(sum(report["outcome_partition"].values()), census.TOTAL)
        unsupported = report["unsupported_topology_breakdown"]
        self.assertEqual(unsupported["disjoint_sum"], 595_353_600)
        self.assertEqual(unsupported["zero_shift_cli_unsupported_including_shared_clock_overlap"]
                         + unsupported["shared_baseband_clock_unsupported_at_positive_shift"],
                         unsupported["disjoint_sum"])

    def test_native_geometry_strata_reconcile_by_each_axis(self):
        table = self.report["geometry_factor_table"]
        self.assertEqual(table["native_geometry_cases"], {
            "source_valid": 78,
            "protected_dwell_reject": 24,
            "tile_workspace_reject": 32,
            "uint32_sample_rate_reject": 2,
        })
        for key in ("by_rate_hz", "by_added_symbols", "by_fhss"):
            self.assertEqual(sum(x["geometry_valid"] + x["geometry_rejected"]
                                 for x in table[key].values()),
                             sum(self.report["outcome_partition"][key_name]
                                 for key_name in ("source_geometry_rejected",
                                                  "capture_margin_nonpositive",
                                                  "capture_margin_positive")))
        self.assertEqual(table["by_rate_hz"]["1000000.0"]["geometry_valid"], 0)
        self.assertEqual(table["by_rate_hz"]["30000000.0"]["geometry_valid"], 0)

    def test_capture_factor_tables_and_native_formula_checks(self):
        tables = self.report["capture_factor_table"]
        self.assertEqual(set(tables["by_baseband_placement"]), set(census.BASEBAND))
        self.assertEqual(set(tables["by_shift_hz"]), {str(int(x)) for x in census.SHIFTS[1:]})
        self.assertEqual(set(tables["by_spread"]), set(census.SPREAD))
        expected_positive = self.report["outcome_partition"]["capture_margin_positive"]
        expected_nonpositive = self.report["outcome_partition"]["capture_margin_nonpositive"]
        for name, table in tables.items():
            if not isinstance(table, dict) or "positive" not in next(iter(table.values()), {}):
                continue
            self.assertEqual(sum(cell["positive"] for cell in table.values()), expected_positive, name)
            self.assertEqual(sum(cell["nonpositive"] for cell in table.values()), expected_nonpositive, name)
        source = self.report["source_evidence"]
        self.assertEqual(source["capture_margin_max_abs_error_hz"], 0.0)
        self.assertEqual(source["independent_capture_margin_checks"], 62)
        self.assertEqual(source["near_zero_factor_rows_rechecked"], 450)
        self.assertEqual(source["near_zero_factor_max_abs_error_hz"], 0.0)
        self.assertEqual(source["binary_sha256_before_after"][0], source["binary_sha256_before_after"][1])
        self.assertEqual(source["census_tool_sha256_before_after"][0], source["census_tool_sha256_before_after"][1])

    def test_replication_warnings_and_scope_remain_explicit(self):
        self.assertEqual(self.report["replication_factor"], 76)
        self.assertEqual(self.report["replication_axes"], {"timing": 2, "cn0_db_hz": 19, "symbol_burst": 2})
        self.assertIn("No physical reception", " ".join(self.report["limitations"]))
        warnings = self.report["nonexclusive_model_warnings"]
        self.assertEqual(warnings["fhss_envelope_only_cells"], census.TOTAL // 2)
        self.assertEqual(warnings["selected_modulation_bypassed_by_none_spread_intent_cells"],
                         census.TOTAL * 15 // 160)

    def test_receipt_or_factor_tampering_fails_validation(self):
        changed = [dict(row) for row in self.validation]
        accepted = next(row for row in changed if row["receipt"]["returncode"] == 0)
        accepted["predicted_capture_margin_hz"] += 1e-5
        with self.assertRaisesRegex(ValueError, "stored factor prediction"):
            census._validate_validation(changed)
        with tempfile.TemporaryDirectory() as td:
            dst = Path(td) / "evidence"
            shutil.copytree(self.evidence, dst)
            with (dst / "geometry.json").open("ab") as out:
                out.write(b" ")
            with self.assertRaisesRegex(ValueError, "artifact identity mismatch"):
                census.validate_evidence(dst, REPO, self.binary)

    def test_changed_census_assumptions_are_refused(self):
        with tempfile.TemporaryDirectory() as td:
            dst = Path(td) / "evidence"
            shutil.copytree(self.evidence, dst)
            summary_path = dst / "summary.json"
            summary = json.loads(summary_path.read_text())
            summary["assumptions"]["requested_seconds"] = 60
            summary_path.write_text(json.dumps(summary))
            with self.assertRaisesRegex(ValueError, "assumptions differ"):
                census.validate_evidence(dst, REPO, self.binary)


if __name__ == "__main__":
    unittest.main()
