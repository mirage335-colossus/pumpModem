#!/usr/bin/env python3
"""Decision rules for the descriptive native ensemble, with no native DSP."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ensemble


def receipt(pair):
    return {"status": "completed", "hf_search_evaluation": {"expected_receiver_bit": 1},
            "hf_public_search": {"phase_refinement": {"combined_bit_log_e": pair},
                                 "presence_reference_threshold": 10, "bit_separation_reference": 5}}


class EnsembleChecks(unittest.TestCase):
    def test_confident_wrong_bit_is_failure(self):
        self.assertFalse(ensemble.screen_reference(receipt([100, 1]))["passes_reference"])
        self.assertTrue(ensemble.screen_reference(receipt([1, 100]))["passes_reference"])

    def test_outer_weight_and_separation_are_charged(self):
        self.assertFalse(ensemble.screen_reference(receipt([0, 11]))["passes_reference"])
        self.assertFalse(ensemble.screen_reference(receipt([99, 100]))["passes_reference"])
        self.assertFalse(ensemble.screen_reference(receipt([None, None]))["passes_reference"])
        self.assertEqual(ensemble.screen_reference(receipt([None, 100]))["separation_status"], "positive_infinity")

    def test_null_control_has_presence_but_no_bit_truth(self):
        null = receipt([100, 1])
        null["hf_search_evaluation"]["expected_receiver_bit"] = None
        result = ensemble.screen_reference(null)
        self.assertIsNone(result["passes_reference"])
        self.assertTrue(result["presence_reference_exceeded"])
        self.assertEqual(result["separation_status"], "not_applicable")

    def test_cli_uses_canonical_campaign_hash(self):
        campaign = ensemble.runner._read_campaign(ensemble.runner.HERE / "campaigns/hf-independent-screen.json")
        run = {"campaign_sha256": hashlib.sha256(ensemble.runner._json_bytes(campaign)).hexdigest(), "trials": []}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "campaign.json").write_text(json.dumps(campaign, indent=2))
            (path / "run.json").write_text(json.dumps(run))
            with patch.object(sys, "argv", ["ensemble", str(path / "campaign.json"), str(path / "run.json"),
                                            "--output", str(path / "out.json")]), patch("builtins.print"):
                ensemble.main()
            self.assertFalse((path / "out.json").exists())

    def test_order_censoring_and_fixed_operational_bracket(self):
        rows = [{"id": str(i), "control": "wanted", "cn0_db_hz": 0, "bit_seconds": 128} for i in range(4)]
        trials = [{"episode": e, "status": "time_limit" if i == 0 else "completed"} for i, e in enumerate(rows)]
        with patch.object(ensemble.runner, "_validate_completed_receipt"):
            generated, audit = ensemble.followups({"episodes": rows}, {"trials": trials},
                                                 {str(i): receipt([100, 1]) for i in range(1, 4)})
        self.assertEqual(len(generated["episodes"]), 8)
        self.assertEqual(audit[0]["status"], "time_limit")
        self.assertEqual([e["id"] for e in generated["episodes"]][::4], ["1-physical", "2-physical"])
        self.assertEqual([(e["bit_seconds"], e["cn0_db_hz"]) for e in generated["episodes"][:4]],
                         [(128, 0), (128, 3), (128, -3), (256, -3)])


if __name__ == "__main__":
    unittest.main()
