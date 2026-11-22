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

    def test_vulkan_attribution_durations_are_measurements(self):
        receipt = {"identity": {"backend_receipt": {
            "vulkan_drift_device_seconds": 0.5,
            "vulkan_drift_device_calls": 2,
            "vulkan_drift_download_bytes": 128,
        }}}
        flat = compare.flatten(receipt, (), {})
        by_name = {path[-1][1]: compare.classify(path) for path in flat}
        self.assertEqual(by_name["vulkan_drift_device_seconds"], "measured")
        self.assertEqual(by_name["vulkan_drift_device_calls"], "compared")
        self.assertEqual(by_name["vulkan_drift_download_bytes"], "compared")

    def test_old_stage_receipt_can_be_compared_with_additive_attribution(self):
        old = {"identity": {"backend_receipt": {"vulkan_drift_planned_calls": 1,
                                                  "vulkan_drift_sessions": 1}}}
        current = {"identity": {"backend_receipt": {
            "vulkan_drift_planned_calls": 1,
            "vulkan_drift_sessions": 1,
            **{f"vulkan_drift_{name}": 0 for name in compare.VULKAN_STAGE_ATTRIBUTION_FIELDS},
        }}}
        with tempfile.TemporaryDirectory() as directory:
            left = Path(directory) / "old.json"
            right = Path(directory) / "current.json"
            output = io.StringIO()

            def compare_receipts(a, b):
                left.write_text(json.dumps(a))
                right.write_text(json.dumps(b))
                output.seek(0)
                output.truncate()
                with contextlib.redirect_stdout(output):
                    status = compare.main([str(left), str(right)])
                return status, output.getvalue()

            status, text = compare_receipts(old, current)
            self.assertEqual(status, 0)
            self.assertIn("RESULT: IDENTICAL", text)

            changed = json.loads(json.dumps(current))
            changed["identity"]["backend_receipt"]["vulkan_drift_download_bytes"] = 64
            status, text = compare_receipts(current, changed)
            self.assertEqual(status, 1)
            self.assertIn("/identity/backend_receipt/vulkan_drift_download_bytes", text)

            partial = json.loads(json.dumps(old))
            partial["identity"]["backend_receipt"]["vulkan_drift_device_calls"] = 0
            status, text = compare_receipts(partial, current)
            self.assertEqual(status, 1)
            self.assertIn("<missing>", text)

            partial = json.loads(json.dumps(current))
            del partial["identity"]["backend_receipt"]["vulkan_drift_plan_seconds"]
            status, text = compare_receipts(partial, current)
            self.assertEqual(status, 1)
            self.assertIn("/identity/backend_receipt/vulkan_drift_plan_seconds", text)

    def test_slot_scope_keeps_empty_native_slot_work(self):
        receipt = {"phases": [{"name": "native_slot", "work": {}}], "result": {}}
        flattened = compare.flatten(receipt, (), {})
        selected = compare.scoped(flattened, "slot")
        self.assertIn((("key", "phases"), ("phase", "native_slot", None),
                       ("key", "work"), ("empty", "object")), selected)


if __name__ == "__main__":
    unittest.main()
