"""Exact structural comparisons for native receiver receipts."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import triad_receipt_compare as compare


class ReceiptCompareTests(unittest.TestCase):
    def test_empty_containers_differ_from_missing_fields(self):
        for present in ({"phases": []}, {"backend_receipt": {}}, {"values": []}):
            with self.subTest(present=present):
                self.assertNotEqual(compare.flatten(present, (), {}), compare.flatten({}, (), {}))

    def test_flattened_paths_preserve_json_structure(self):
        structured = compare.flatten({"x": [1], "a/b": 2, "a": {"b": 3}}, (), {})
        self.assertEqual(len(structured), 3)
        self.assertEqual(len(set(structured)), 3)
        self.assertEqual(compare.flatten({"x[0]": 1}, (), {}), {(("key", "x[0]"),): 1})
        literal_path = compare.flatten({"x[0]": 1}, (), {})
        array_path = compare.flatten({"x": [1]}, (), {})
        self.assertNotEqual(literal_path, array_path)
        self.assertNotEqual(compare.render_path(next(iter(literal_path))),
                            compare.render_path(next(iter(array_path))))

    def test_cli_fails_for_empty_receipt_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            left = Path(directory) / "left.json"
            right = Path(directory) / "right.json"
            left.write_text(json.dumps({"schema": "probe", "phases": []}))
            right.write_text(json.dumps({"schema": "probe"}))
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                status = compare.main([str(left), str(right)])
            self.assertEqual(status, 1)
            self.assertIn("/phases<empty array>", output.getvalue())

    def test_empty_measurement_field_is_a_structural_difference(self):
        flat = compare.flatten({"identity": {"setup_seconds": []}}, (), {})
        self.assertEqual(compare.classify(next(iter(flat))), "compared")

    def test_slot_scope_keeps_empty_native_slot_work(self):
        receipt = {"phases": [{"name": "native_slot", "work": {}}], "result": {}}
        flattened = compare.flatten(receipt, (), {})
        selected = compare.scoped(flattened, "slot")
        self.assertIn((("key", "phases"), ("phase", "native_slot", None),
                       ("key", "work"), ("empty", "object")), selected)


if __name__ == "__main__":
    unittest.main()
