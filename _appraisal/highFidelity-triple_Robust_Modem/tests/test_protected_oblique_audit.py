"""Finite logic and provenance checks for protected-oblique retrospective audit."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

APP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(APP))
import protected_oblique_audit as audit
import run


SOURCE = {
    "native_source_sha256": "1" * 64,
    "appraisal_source_sha256": "2" * 64,
    "build_options_sha256": "3" * 64,
}
PROBE = "4" * 64


def _tier(policy: str, wanted: bool, *, candidate: bool) -> dict:
    premature = int(candidate and wanted)
    return {
        "policy": policy,
        "canonical_correct_messages": 0,
        "canonical_incorrect_messages": 0,
        "canonical_premature_completions": premature,
        "consumer_correct_messages": int(candidate and wanted),
        "consumer_incorrect_messages": 0,
        "consumer_decode_attempts": int(candidate and wanted),
        "false_data": 0,
    }


def _receipt(episode: dict) -> dict:
    wanted = episode.get("control", "wanted") == "wanted"
    baseline_tiers = [_tier(name, wanted, candidate=False) for name in audit.POLICIES]
    candidate_tiers = [_tier(name, wanted, candidate=True) for name in audit.POLICIES]
    consumer_frames = ([{"first_public_slot": 0, "stop_public_slot": 66,
                         "bit_positions": 64, "decoded_payload_bytes": [ord(episode["message"])],
                         "matches_transmitted_complete_message": True}] if wanted else [])
    candidate = {
        "schema_version": 1,
        "status": "completed",
        "source_identity": dict(SOURCE),
        "outcomes": {"accepted_samples": 500, "accepted_messages": 0,
                     "correct_decoded_complete": False, "false_data": 0,
                     "premature_completions": int(wanted)},
        "messages": [],
        "hf_protected_reassembly": {
            "correct_complete_messages": int(wanted),
            "incorrect_messages": 0,
            "decode_attempts": int(wanted),
            "candidates": consumer_frames,
        },
        "hf_protected_admission_replay": {"tiers": candidate_tiers},
    }
    return {
        "schema_version": 1,
        "status": "completed",
        "source_identity": dict(SOURCE),
        "plan": {"max_samples": 12_000_000, "horizon_samples": 500},
        "outcomes": {"accepted_samples": 500, "accepted_messages": 0,
                     "correct_decoded_complete": False, "false_data": 0,
                     "premature_completions": 0},
        "messages": [],
        "hf_protected_reassembly": {
            "correct_complete_messages": 0,
            "incorrect_messages": 0,
            "decode_attempts": 0,
        },
        "hf_protected_admission_replay": {"tiers": baseline_tiers},
        "hf_predictive_quiet_comparison": {"candidate": candidate},
    }


class ProtectedObliqueAuditTests(unittest.TestCase):
    def setUp(self):
        (APP / "local-evidence").mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="test-protected-oblique-audit-",
                                                dir=APP / "local-evidence")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.current = self._make_run("discriminator")
        self.preservation = self._make_run("preservation")

    def _make_run(self, key: str, label: str | None = None, *, longer=False) -> Path:
        campaign_path = (audit.LONGER_CAMPAIGNS if longer else audit.CAMPAIGNS)[key]
        campaign = run._read_campaign(campaign_path)
        campaign_sha = hashlib.sha256(run._json_bytes(campaign)).hexdigest()
        directory = self.root / (label or (audit.LONGER_RUN_NAMES if longer else audit.RUN_NAMES)[key])
        directory.mkdir()
        probe = str((self.root / "probe.bin").resolve())
        run_records = []
        for index, episode in enumerate(campaign["episodes"]):
            receipt = _receipt(episode)
            raw_name = f"{index:03d}-{episode['id']}.json"
            (directory / raw_name).write_text(json.dumps(receipt), encoding="utf-8")
            run_records.append({
                "episode": episode,
                "status": "completed",
                "return_code": 0,
                "timed_out": False,
                "elapsed_seconds": 1.0,
                "raw_stdout": raw_name,
                "command": [probe, "--max-samples", "12000000",
                            "--max-wall-seconds", "899.5"],
            })
        frozen = {"campaign": campaign, "campaign_sha256": campaign_sha,
                  "probe": probe, "probe_sha256": PROBE}
        (directory / "frozen-input.json").write_text(json.dumps(frozen), encoding="utf-8")
        record = {"status": "completed", "campaign_sha256": campaign_sha,
                  "probe_sha256": PROBE, "planned_trials": len(run_records), "executed_trials": len(run_records),
                  "execution_failures": 0, "trials": run_records}
        (directory / "run.json").write_text(json.dumps(record), encoding="utf-8")
        return directory

    def build(self):
        with mock.patch.object(run, "_validate_completed_receipt", return_value=None):
            return audit.build_audit(self.current, self.preservation)

    def test_missing_and_invalid_provenance_are_rejected(self):
        (self.current / "frozen-input.json").unlink()
        with self.assertRaises(audit.AuditError):
            self.build()
        self.current = self._make_run("discriminator", "discriminator-replacement")
        record_path = self.preservation / "run.json"
        record = json.loads(record_path.read_text())
        record["probe_sha256"] = "5" * 64
        record_path.write_text(json.dumps(record))
        with self.assertRaises(audit.AuditError):
            self.build()

    def _edit_receipt(self, run_dir: Path, receipt_index: int, edit):
        record = json.loads((run_dir / "run.json").read_text())
        raw_path = run_dir / record["trials"][receipt_index]["raw_stdout"]
        receipt = json.loads(raw_path.read_text())
        edit(receipt)
        raw_path.write_text(json.dumps(receipt), encoding="utf-8")

    def test_wrong_content_fails_integrity_even_when_wanted_delivery_succeeds(self):
        self._edit_receipt(self.current, 1, lambda receipt: receipt[
            "hf_predictive_quiet_comparison"]["candidate"]["hf_protected_admission_replay"]["tiers"][2].update(
                canonical_incorrect_messages=1, consumer_incorrect_messages=1))
        report = self.build()
        wrong_key = next(case for case in report["cases"]
                         if case["id"] == "oblique-protected-foreign-key")
        strict = next(tier for tier in wrong_key["tiers"] if tier["policy"] == "strict")
        self.assertFalse(strict["gates"]["no_native_or_consumer_incorrect_content"])
        self.assertEqual(strict["consumer_incorrect_messages"], 1)
        self.assertEqual(report["status"], "failed_for_frozen_cases")

    def test_missing_candidate_consumer_delivery_fails_wanted_gate(self):
        self._edit_receipt(self.current, 0, lambda receipt: receipt[
            "hf_predictive_quiet_comparison"]["candidate"]["hf_protected_admission_replay"]["tiers"][2].update(
                consumer_correct_messages=0, consumer_decode_attempts=0))
        report = self.build()
        wanted = next(case for case in report["cases"] if case["id"] == "oblique-protected-e")
        strict = next(tier for tier in wanted["tiers"] if tier["policy"] == "strict")
        self.assertFalse(strict["direct_native_complete"])
        self.assertEqual(strict["consumer_correct_messages"], 0)
        self.assertFalse(strict["gates"]["exact_wanted_combined_delivery_one_path"])

    def test_negative_control_rejects_any_accepted_message_even_if_labeled_correct(self):
        for field in ("canonical_correct_messages", "consumer_correct_messages"):
            with self.subTest(field=field):
                self._edit_receipt(self.current, 1, lambda receipt: receipt[
                    "hf_predictive_quiet_comparison"]["candidate"]["hf_protected_admission_replay"]["tiers"][2].update(
                        canonical_correct_messages=int(field == "canonical_correct_messages"),
                        consumer_correct_messages=int(field == "consumer_correct_messages")))
                report = self.build()
                wrong_key = next(case for case in report["cases"]
                                 if case["id"] == "oblique-protected-foreign-key")
                strict = next(tier for tier in wrong_key["tiers"] if tier["policy"] == "strict")
                self.assertFalse(strict["gates"]["no_native_or_consumer_incorrect_content"])

    def test_baseline_zero_candidate_consumer_one_is_one_exact_delivery_path(self):
        report = self.build()
        wanted = next(case for case in report["cases"] if case["id"] == "oblique-protected-e")
        strict = next(tier for tier in wanted["tiers"] if tier["policy"] == "strict")
        self.assertEqual(wanted["baseline"]["native_correct_messages"], 0)
        self.assertEqual(wanted["baseline"]["consumer_correct_messages"], 0)
        self.assertEqual(strict["native_correct_messages"], 0)
        self.assertEqual(strict["consumer_correct_messages"], 1)
        self.assertTrue(strict["gates"]["exact_wanted_combined_delivery_one_path"])
        self.assertFalse(strict["gates"]["no_premature_physical_completion"])

    def test_both_correct_paths_are_duplicate_delivery_and_fail_exactly_one_gate(self):
        self._edit_receipt(self.current, 0, lambda receipt: receipt[
            "hf_predictive_quiet_comparison"]["candidate"]["hf_protected_admission_replay"]["tiers"][2].update(
                canonical_correct_messages=1))
        report = self.build()
        wanted = next(case for case in report["cases"] if case["id"] == "oblique-protected-e")
        strict = next(tier for tier in wanted["tiers"] if tier["policy"] == "strict")
        self.assertEqual(strict["native_correct_messages"] + strict["consumer_correct_messages"], 2)
        self.assertFalse(strict["gates"]["exact_wanted_combined_delivery_one_path"])

    def test_false_data_fails_negative_gate_even_when_no_content_is_accepted(self):
        self._edit_receipt(self.current, 1, lambda receipt: receipt[
            "hf_predictive_quiet_comparison"]["candidate"]["hf_protected_admission_replay"]["tiers"][2].update(false_data=1))
        report = self.build()
        wrong_key = next(case for case in report["cases"]
                         if case["id"] == "oblique-protected-foreign-key")
        strict = next(tier for tier in wrong_key["tiers"] if tier["policy"] == "strict")
        self.assertEqual(strict["consumer_incorrect_messages"], 0)
        self.assertFalse(strict["gates"]["negative_control_false_data_zero"])

    def test_report_names_dependent_replays_and_does_not_claim_qualification(self):
        report = self.build()
        self.assertEqual(len(report["cases"]), 4)
        self.assertEqual(report["original_rendered_capture_count"], 4)
        self.assertIn("dependent replay", report["capture_scope"])
        self.assertIn("share physical trajectories", report["capture_scope"])
        self.assertFalse(report["operational_qualification"])
        self.assertFalse(report["threshold_selection"])
        self.assertEqual(len(report["cases"][0]["tiers"]), 6)
        self.assertEqual(report["predeclared_primary_policy"], "strict")
        self.assertEqual([item["policy"] for item in report["fixed_policy_assessments"]], list(audit.POLICIES))
        self.assertFalse(report["primary_policy_all_four_case_gates_pass"])
        for item in report["fixed_policy_assessments"]:
            self.assertEqual(item["failed_gates"],
                             [gate for gate in report["failed_gates"] if gate["policy"] == item["policy"]])

    def test_longer_profile_requires_its_exact_one_plus_three_campaigns(self):
        longer_current = self._make_run("discriminator", longer=True)
        longer_preservation = self._make_run("preservation", longer=True)
        with mock.patch.object(run, "_validate_completed_receipt", return_value=None):
            report = audit.build_audit(longer_current, longer_preservation, longer=True)
            self.assertEqual(report["requested_bit_seconds"], 16)
            self.assertEqual([case["id"] for case in report["cases"]],
                             list(audit.LONGER_EXPECTED_IDS["discriminator"] +
                                  audit.LONGER_EXPECTED_IDS["preservation"]))
            with self.assertRaises(audit.AuditError):
                audit.build_audit(longer_current, self.preservation, longer=True)


if __name__ == "__main__":
    unittest.main()
