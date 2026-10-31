import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools/helper-evidence.py"
BUNDLE = ROOT / "docs/validation-data/helper/2026-10-31"


def cli(*args):
    return subprocess.run([sys.executable, str(SCRIPT), *map(str, args)], capture_output=True, text=True)


class HelperEvidenceTests(unittest.TestCase):
    def test_repository_bundle_and_summary(self):
        verified = cli("verify")
        self.assertEqual(verified.returncode, 0, verified.stderr)
        self.assertIn("222 cases", verified.stdout)
        summary = cli("summary")
        self.assertEqual(summary.returncode, 0, summary.stderr)
        data = json.loads(summary.stdout)
        self.assertEqual(data["screen_loss_seed_counts"], {"160": 8, "165": 8, "170": 8, "175": 8, "180": 8})
        self.assertEqual(data["negative_controls"]["arms"], 24)
        heldout = data["profile36"]["heldout/pooled-inversion/loss-180/mode-clean/wave-tone/delay-2/rf-hf"]
        self.assertEqual((heldout["cases"], heldout["available"], heldout["qualified"]), (32, 32, 32))
        frequency = data["frequency36"]["heldout/pooled-inversion/loss-180/mode-clean/wave-tone/delay-2/rf-hf"]
        self.assertEqual((frequency["cases"], frequency["available"]), (32, 31))
        group = "heldout/pooled-inversion/loss-180/mode-clean/wave-tone/delay-2/rf-hf"
        nominal = group.replace("pooled-inversion", "nominal")
        self.assertAlmostEqual(data["phase_weighted_clean_twin"][group]["rms_deg"], 6.85904373055, places=8)
        self.assertAlmostEqual(data["phase_weighted_clean_twin"][nominal]["rms_deg"], 8.84348093395, places=8)
        self.assertEqual(data["wrong_hop_assignment"][group]["complete_output_wrong_hop_samples"], 76553)
        self.assertEqual(data["wrong_hop_assignment"][nominal]["complete_output_wrong_hop_samples"], 166929)
        self.assertAlmostEqual(data["cpu"]["pooled-inversion"]["exact36_tracking_sum_median_s"], 0.348217)
        self.assertEqual(data["cpu"]["pooled-inversion"]["conditioned_input_seconds_at36"], [34])
        self.assertEqual(data["matched_nominal_vs_pooled_frequency_pairs"], 98)

    def test_bundle_hash_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            copied = Path(tmp) / "bundle"
            shutil.copytree(BUNDLE, copied)
            with (copied / "cases.jsonl").open("a", encoding="utf-8") as f:
                f.write("\n")
            result = cli("verify", "--bundle", copied)
            self.assertEqual(result.returncode, 2)
            self.assertIn("SHA-256 mismatch: cases.jsonl", result.stderr)

    def test_duplicate_reservation_key_rejected_after_hash_update(self):
        with tempfile.TemporaryDirectory() as tmp:
            copied = Path(tmp) / "bundle"
            shutil.copytree(BUNDLE, copied)
            path = copied / "reservations.json"
            data = json.loads(path.read_text(encoding="utf-8"))
            data["keys"][1] = data["keys"][0].copy()
            path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
            manifest_path = copied / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["files_sha256"]["reservations.json"] = hashlib.sha256(path.read_bytes()).hexdigest()
            manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
            result = cli("verify", "--bundle", copied)
            self.assertEqual(result.returncode, 2)
            self.assertIn("duplicate reservation key", result.stderr)

    def test_replay_refuses_existing_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "existing"
            output.mkdir()
            sentinel = output / "keep.txt"
            sentinel.write_text("preserve", encoding="utf-8")
            binary = Path(tmp) / "placeholder-binary"
            binary.write_text("not executed because output exists", encoding="utf-8")
            result = cli("run", "--case", "screen/000", "--binary", binary, "--output", output)
            self.assertEqual(result.returncode, 2)
            self.assertIn("refusing to overwrite", result.stderr)
            self.assertEqual(sentinel.read_text(encoding="utf-8"), "preserve")


if __name__ == "__main__":
    unittest.main()
