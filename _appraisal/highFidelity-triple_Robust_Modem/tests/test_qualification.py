"""Finite evidence-reader checks; synthetic receipts here are logic fixtures only."""
from __future__ import annotations

import copy
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

APP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(APP))
import qualification as q
import analyze_vector_presence as presence
import analyze_evolving as evolving
import run


IDENTITY = {
    "native_source_sha256": "1" * 64,
    "appraisal_source_sha256": "2" * 64,
    "build_options_sha256": "3" * 64,
    "build_config": "Release",
    "compiler": "test compiler",
    "canonical_pattern_revision": "independent-apsk-v2",
    "openssl_runtime": "test runtime",
}


def receipt_for(episode: dict, *, correct: bool = True, false_data: int = 0,
                receiver: bool = True, accepted_samples: int = 8) -> dict:
    return {
        "schema_version": 1, "status": "completed",
        "requested_scenario": episode["scenario"], "mode": "receive",
        "waveform": episode["waveform"], "seed": episode["seed"],
        "control": episode.get("control", "wanted"), "source_identity": dict(IDENTITY),
        "plan": {"max_samples": 100, "horizon_samples": 8, "scored_slot_horizon": 1,
                 "public_slots": [{"index": 0, "symbol_ordinal": 4, "pattern_epoch": 0}]},
        "outcomes": {"accepted_samples": accepted_samples, "actual_scored_slots": 1,
                     "accepted_messages": 1 if episode.get("control", "wanted") == "wanted" else 0,
                     "false_data": false_data,
                     "correct_complete": bool(correct), "correct_decoded_complete": bool(correct)},
        "messages": ([{"decoded": True, "matches_transmitted_message": correct}]
                     if episode.get("control", "wanted") == "wanted" else []),
        "slots": [{"index": 0, "symbol_ordinal": 4, "pattern_epoch": 0,
                   "complete": True, "all_valid": True}],
        "receiver": ({"input_closed": True, "input_stop": 8, "next_public_slot": 1,
                      "state": "input_ended", "memory_limit_bytes": 4096,
                      "peak_reserved_bytes": 1024} if receiver else None),
    }


def episode(control: str = "wanted") -> dict:
    return {"id": "trial-a", "scenario": "hf-wave-lens-off", "mode": "receive",
            "waveform": "reference", "seed": 11,
            **({} if control == "wanted" else {"control": control})}


def gates(role: str = "wanted", **extra) -> dict:
    value = {"numerics": False, "delivery": role == "wanted", "integrity": True,
             "bounded_execution": True, "false_data_max": 0}
    if role == "wanted":
        value["delivery_requirement_basis"] = "declared_operational_anchor"
    value.update(extra)
    return value


class QualificationReaderTests(unittest.TestCase):
    def test_completed_run_does_not_imply_wanted_message_success(self):
        ep = episode()
        item = {"episode": ep, "control_role": "wanted", "gates": gates(),
                "max_samples": 100, "max_wall_seconds": 10,
                "_evidence_status": "passed", "_evidence_reason": "test fixture"}
        report = q._cell_gates(receipt_for(ep, correct=False), item, 1.0)
        self.assertEqual(report["delivery"]["status"], "failed")
        self.assertEqual(report["integrity"]["status"], "failed")
        self.assertEqual(report["bounded_execution"]["status"], "passed")

    def test_negative_control_false_data_and_accepted_false_message_are_independent(self):
        ep = episode("noise")
        item = {"episode": ep, "control_role": "negative", "gates": gates("negative"),
                "max_samples": 100, "max_wall_seconds": 10,
                "_evidence_status": "passed", "_evidence_reason": "test fixture"}
        r = receipt_for(ep, false_data=1)
        r["outcomes"]["accepted_messages"] = 0
        report = q._cell_gates(r, item, 1.0)
        self.assertEqual(report["integrity"]["status"], "passed")
        self.assertEqual(report["false_data"]["status"], "failed")
        r["outcomes"]["accepted_messages"] = 1
        report = q._cell_gates(r, item, 1.0)
        self.assertEqual(report["integrity"]["status"], "failed")

    def test_full_horizon_and_finished_receiver_state_are_required(self):
        ep = episode()
        item = {"episode": ep, "control_role": "wanted", "gates": gates(),
                "max_samples": 100, "max_wall_seconds": 10,
                "_evidence_status": "passed", "_evidence_reason": "test fixture"}
        self.assertEqual(q._receive_work(receipt_for(ep, accepted_samples=7), item)[0], "failed")
        self.assertEqual(q._receive_work(receipt_for(ep, receiver=False), item)[0], "missing")
        reordered = receipt_for(ep)
        reordered["slots"][0]["index"] = 1
        self.assertEqual(q._receive_work(reordered, item)[0], "failed")
        self.assertEqual(q._cell_gates(receipt_for(ep), item, 11.0)["bounded_execution"]["status"], "failed")

    def test_numerical_admission_requires_actual_qualified_table_hash(self):
        r = {"channel": {"vector": {"table_qualified": True, "table_sha256": "a" * 64}},
             "plan": {"table_built": True}}
        spec = {"physical_report_path": "channel.vector", "required_plan_true": ["table_built"]}
        self.assertEqual(q._numerical_gate(r, spec)[0], "passed")
        r["channel"]["vector"]["table_qualified"] = False
        self.assertEqual(q._numerical_gate(r, spec)[0], "failed")
        r["channel"]["vector"]["table_qualified"] = True
        r["channel"]["vector"]["table_sha256"] = "bad"
        self.assertEqual(q._numerical_gate(r, spec)[0], "failed")

    def _cell(self, cell_id="cell-a", receipt_sha="4" * 64, episode_id="ep-a"):
        ep = {"id": episode_id, "scenario": "hf-wave-lens-off", "mode": "receive",
              "waveform": "reference", "seed": 11, "reception_requirement": "required_complete"}
        return {"id": cell_id, "campaign_path": "campaign.json", "campaign_file_sha256": "5" * 64,
                "campaign_sha256": "6" * 64, "episode_id": episode_id, "episode": ep,
                "run_directory": "run-a", "receipt_sha256": receipt_sha, "probe_sha256": "7" * 64,
                "source_identity": dict(IDENTITY), "control_role": "wanted", "gates": gates(),
                "max_wall_seconds": 10.0, "max_samples": 100}

    def _spec(self, cells):
        return {"schema_version": 1, "qualification_domain": {
            "id": "finite-test", "scope": "logic fixture", "excluded_strata": ["all unmodeled strata"],
            "required_missing_strata": [], "target_source_identity": dict(IDENTITY),
            "target_probe_sha256": "8" * 64, "target_probe_path": "synthetic-probe.bin"}, "cells": cells}

    def test_spec_rejects_duplicate_cell_ids_raw_receipts_and_episode_recounting(self):
        a = self._cell()
        with self.assertRaises(q.QualificationError):
            q._validate_spec(self._spec([a, copy.deepcopy(a)]))
        b = self._cell("cell-b", receipt_sha="4" * 64, episode_id="ep-b")
        with self.assertRaises(q.QualificationError):
            q._validate_spec(self._spec([a, b]))
        b = self._cell("cell-b", receipt_sha="9" * 64)
        with self.assertRaises(q.QualificationError):
            q._validate_spec(self._spec([a, b]))

    def test_spec_requires_bounded_work_and_valid_required_missing_strata(self):
        a = self._cell()
        a["gates"]["bounded_execution"] = False
        with self.assertRaises(q.QualificationError):
            q._validate_spec(self._spec([a]))
        a = self._cell()
        spec = self._spec([a])
        spec["qualification_domain"]["required_missing_strata"] = [None]
        with self.assertRaises(q.QualificationError):
            q._validate_spec(spec)

    def test_delivery_anchor_must_be_predeclared_and_cannot_be_downgraded(self):
        cell = self._cell()
        q._validate_spec(self._spec([cell]))
        downgraded = copy.deepcopy(cell)
        downgraded["gates"].update(delivery=False, delivery_requirement_basis="characterization")
        with self.assertRaisesRegex(q.QualificationError, "contradict"):
            q._validate_spec(self._spec([downgraded]))
        retrospective = copy.deepcopy(cell)
        retrospective["episode"].pop("reception_requirement")
        with self.assertRaisesRegex(q.QualificationError, "predeclared"):
            q._validate_spec(self._spec([retrospective]))
        promoted = copy.deepcopy(cell)
        promoted["episode"]["reception_requirement"] = "characterization"
        promoted["gates"].update(delivery=False, delivery_requirement_basis="characterization")
        q._validate_spec(self._spec([promoted]))
        promoted["gates"].update(delivery=True, delivery_requirement_basis="declared_operational_anchor")
        with self.assertRaisesRegex(q.QualificationError, "predeclared"):
            q._validate_spec(self._spec([promoted]))

    def test_negative_control_requirement_matches_frozen_noise_and_zero_false_data_gate(self):
        cell = self._cell()
        cell["episode"].update(control="noise", reception_requirement="negative_control")
        cell.update(control_role="negative")
        cell["gates"] = gates("negative")
        q._validate_spec(self._spec([cell]))
        cell["gates"]["false_data_max"] = 1
        with self.assertRaisesRegex(q.QualificationError, "contradict"):
            q._validate_spec(self._spec([cell]))
        cell["gates"]["false_data_max"] = 0
        cell["episode"]["control"] = "wanted"
        with self.assertRaises(q.QualificationError):
            q._validate_spec(self._spec([cell]))

    def test_required_unqualified_strata_prevent_domain_pass_even_when_finite_cells_pass(self):
        cell = self._cell()
        spec = self._spec([cell])
        passed = {"receipt_validation": {"status": "passed"},
                  "current_implementation_applicability": {"status": "passed"},
                  "bounded_execution": {"status": "passed"}, "delivery": {"status": "passed"},
                  "integrity": {"status": "passed"}, "numerics": {"status": "not_applicable"}}
        result = {"id": cell["id"], "control_role": "wanted", "gates": passed,
                  "current_implementation_applicability": passed["current_implementation_applicability"]}
        with patch.object(q, "_load_cell", return_value=(result, 1.0, dict(IDENTITY))), \
                patch.object(q, "_observe_target", return_value={"status": "passed"}):
            report = q.evaluate(spec, APP / "qualification-spec.json")
        self.assertEqual(report["finite_case_status"], "passed_for_declared_cases")
        self.assertEqual(report["status"], "passed_for_declared_cases")
        self.assertEqual(report["cells"][0]["case_status"], "passed_for_declared_cell")

        spec["qualification_domain"]["required_missing_strata"] = ["dynamic multi-hop magnetoionic scattering"]
        with patch.object(q, "_load_cell", return_value=(result, 1.0, dict(IDENTITY))), \
                patch.object(q, "_observe_target", return_value={"status": "passed"}):
            report = q.evaluate(spec, APP / "qualification-spec.json")
        self.assertEqual(report["finite_case_status"], "passed_for_declared_cases")
        self.assertEqual(report["status"], "incomplete")
        self.assertIsNone(report["qualified_reliability_probability"])

    def test_changed_or_missing_target_binary_cannot_reuse_a_passing_spec(self):
        cell = self._cell()
        spec = self._spec([cell])
        passed = {name: {"status": "passed"} for name in
                  ("receipt_validation", "bounded_execution", "delivery", "integrity")}
        result = {"id": cell["id"], "control_role": "wanted", "gates": passed,
                  "current_implementation_applicability": {"status": "passed"}}
        with tempfile.TemporaryDirectory(dir=APP / "local-evidence") as tmp:
            target = Path(tmp) / "synthetic-target.bin"
            target.write_bytes(b"synthetic target before rebuild")
            spec["qualification_domain"].update(target_probe_path=str(target), target_probe_sha256=q._sha(target))
            def evaluate():
                with patch.object(q, "_load_cell", return_value=(copy.deepcopy(result), 1., dict(IDENTITY))):
                    return q.evaluate(spec, APP / "qualification-spec.json")
            self.assertEqual(evaluate()["status"], "passed_for_declared_cases")
            target.write_bytes(b"synthetic changed target after rebuild")
            changed = evaluate()
            self.assertEqual(changed["status"], "incomplete")
            self.assertIn("stale", changed["target_binary_observation"]["reason"])
            self.assertEqual(changed["cells"][0]["gates"]["receipt_validation"]["status"], "passed")
            target.unlink()
            missing = evaluate()
            self.assertEqual(missing["status"], "incomplete")
            self.assertIsNone(missing["target_binary_observation"]["observed_sha256"])

    def test_characterization_miss_remains_visible_and_missing_anchors_prevent_pass(self):
        cell = self._cell()
        cell["episode"]["reception_requirement"] = "characterization"
        cell["gates"].update(delivery=False, delivery_requirement_basis="characterization")
        spec = self._spec([cell])
        dimensions = {name: {"status": "passed"} for name in
                      ("receipt_validation", "bounded_execution", "integrity")}
        dimensions["delivery"] = {"status": "failed"}
        result = {"id": cell["id"], "control_role": "wanted", "gates": dimensions,
                  "current_implementation_applicability": {"status": "passed"}}
        def evaluate():
            with patch.object(q, "_load_cell", return_value=(copy.deepcopy(result), 1., dict(IDENTITY))), \
                    patch.object(q, "_observe_target", return_value={"status": "passed"}):
                return q.evaluate(spec, APP / "qualification-spec.json")
        report = evaluate()
        self.assertEqual(report["finite_case_status"], "passed_for_declared_cases")
        self.assertEqual(report["status"], "incomplete")
        self.assertEqual(report["delivery_qualification_status"], "incomplete")
        self.assertEqual(report["all_wanted_delivery"]["failed_delivery_observations"], 1)
        dimensions["integrity"] = {"status": "failed"}
        self.assertEqual(evaluate()["status"], "failed")

    def test_optional_source_commit_is_preserved_and_compared_when_present(self):
        with_commit = dict(IDENTITY, source_commit_at_configure="a" * 40)
        normalized = q._identity(with_commit, "test identity")
        self.assertEqual(normalized["source_commit_at_configure"], "a" * 40)
        without_commit = q._identity(IDENTITY, "test identity")
        self.assertNotEqual(normalized, without_commit)

    def test_component_only_episode_is_rejected(self):
        cell = self._cell()
        cell["episode"]["mode"] = "hf-search"
        with self.assertRaises(q.QualificationError):
            q._validate_spec(self._spec([cell]))

    def test_metadata_amendment_is_one_hashed_audited_report_flag_only(self):
        with tempfile.TemporaryDirectory(dir=APP / "local-evidence") as tmp:
            root = Path(tmp)
            raw_path = root / "raw.json"
            audit_path = root / "audit.json"
            raw = {"source_identity": dict(IDENTITY), "outcomes": {"correct_complete": False},
                   "channel": {"native_chain": {"propagation_band_declared_not_analytically_proved": False}}}
            raw_path.write_text(json.dumps(raw), encoding="utf-8")
            audit = {"kind": "explicit_native_receipt_metadata_amendment_audit",
                     "original_receipt": str(raw_path.resolve()),
                     "original_receipt_sha256": q._sha(raw_path),
                     "original_trial_status": "invalid_receipt", "original_campaign_status": "incomplete_checks",
                     "original_raw_unchanged": True, "outcomes_unchanged": True,
                     "original_source_identity": dict(IDENTITY),
                     "amendments": [{"json_pointer": q.METADATA_POINTER, "old": False, "new": True}]}
            audit_path.write_text(json.dumps(audit), encoding="utf-8")
            cell = {"episode": episode(), "source_identity": dict(IDENTITY),
                    "raw_stdout": raw_path.name,
                    "metadata_amendment": {"path": str(audit_path), "sha256": q._sha(audit_path)}}
            with patch.object(run, "_validate_completed_receipt") as validator:
                amended = q._metadata_amended_receipt(raw, cell, root)
            validator.assert_called_once()
            self.assertIs(amended["channel"]["native_chain"]["propagation_band_declared_not_analytically_proved"], True)
            self.assertEqual(amended["outcomes"], raw["outcomes"])
            self.assertEqual(raw["channel"]["native_chain"]["propagation_band_declared_not_analytically_proved"], False)

            audit["amendments"].append({"json_pointer": "/outcomes/correct_complete", "old": False, "new": True})
            audit_path.write_text(json.dumps(audit), encoding="utf-8")
            cell["metadata_amendment"]["sha256"] = q._sha(audit_path)
            with self.assertRaises(q.EvidenceInvalid):
                q._metadata_amended_receipt(raw, cell, root)

    def test_synthetic_loader_exercises_hash_and_source_bindings_only(self):
        # This temporary record is deliberately synthetic: it tests parser/binding logic,
        # and is deleted here. It is never evidence for modem performance.
        campaign_path = APP / "campaigns" / "hf-wave-lens-receiver.json"
        campaign = run._read_campaign(campaign_path)
        ep = campaign["episodes"][0]
        campaign_sha = hashlib.sha256(run._json_bytes(campaign)).hexdigest()
        with tempfile.TemporaryDirectory(dir=APP / "local-evidence") as tmp:
            root = Path(tmp)
            run_dir = root / "run-a"
            run_dir.mkdir()
            probe = root / "probe.bin"
            probe.write_bytes(b"synthetic probe hash fixture")
            probe_sha = q._sha(probe)
            receipt = receipt_for(ep)
            receipt.update({"requested_scenario": ep["scenario"], "source_identity": dict(IDENTITY)})
            receipt["plan"]["max_samples"] = campaign["max_samples"]
            raw = run_dir / "trial.json"
            raw.write_text(json.dumps(receipt), encoding="utf-8")
            cell = self._cell(receipt_sha=q._sha(raw))
            cell.update({"campaign_path": str(campaign_path), "campaign_file_sha256": q._sha(campaign_path),
                         "campaign_sha256": campaign_sha, "episode_id": ep["id"], "episode": ep,
                         "run_directory": str(run_dir), "probe_sha256": probe_sha,
                         "source_identity": dict(IDENTITY), "max_samples": campaign["max_samples"]})
            frozen = {"campaign": campaign, "campaign_sha256": campaign_sha,
                      "probe": str(probe), "probe_sha256": probe_sha}
            (run_dir / "frozen-input.json").write_text(json.dumps(frozen), encoding="utf-8")
            command = [str(probe), "--mode", "receive", "--scenario", ep["scenario"],
                       "--max-samples", str(campaign["max_samples"]), "--max-wall-seconds", "5"]
            trial = {"episode": ep, "status": "completed", "return_code": 0, "timed_out": False,
                     "elapsed_seconds": 1.0, "raw_stdout": raw.name, "command": command}
            run_record = {"campaign_sha256": campaign_sha, "probe_sha256": probe_sha,
                          "status": "incomplete_checks", "stop_reason": "one unrelated later row failed",
                          "trials": [trial]}
            (run_dir / "run.json").write_text(json.dumps(run_record), encoding="utf-8")
            with patch.object(run, "_validate_completed_receipt"):
                result, _, _ = q._load_cell(cell, APP, dict(IDENTITY), probe_sha)
            self.assertEqual(result["gates"]["receipt_validation"]["status"], "passed", result)
            self.assertEqual(result["current_implementation_applicability"]["status"], "passed")
            self.assertEqual(result["parent_campaign_status"], "incomplete_checks")

            run_record["probe_sha256"] = "f" * 64
            (run_dir / "run.json").write_text(json.dumps(run_record), encoding="utf-8")
            result, _, _ = q._load_cell(cell, APP, dict(IDENTITY), probe_sha)
            self.assertEqual(result["gates"]["receipt_validation"]["status"], "failed")
            run_record["probe_sha256"] = probe_sha
            cell["source_identity"]["appraisal_source_sha256"] = "f" * 64
            (run_dir / "run.json").write_text(json.dumps(run_record), encoding="utf-8")
            result, _, _ = q._load_cell(cell, APP, dict(IDENTITY), probe_sha)
            self.assertEqual(result["gates"]["receipt_validation"]["status"], "failed")
            cell["source_identity"] = dict(IDENTITY)
            run_record["probe_sha256"] = probe_sha
            run_record["trials"][0]["status"] = "timed_out"
            (run_dir / "run.json").write_text(json.dumps(run_record), encoding="utf-8")
            result, _, _ = q._load_cell(cell, APP, dict(IDENTITY), probe_sha)
            self.assertEqual(result["gates"]["receipt_validation"]["status"], "missing")
            self.assertEqual(result["parent_campaign_status"], "incomplete_checks")
            run_record["trials"][0]["status"] = "completed"
            run_record["trials"][0]["episode"]["seed"] = 12
            (run_dir / "run.json").write_text(json.dumps(run_record), encoding="utf-8")
            result, _, _ = q._load_cell(cell, APP, dict(IDENTITY), probe_sha)
            self.assertEqual(result["gates"]["receipt_validation"]["status"], "failed")


class PresenceReferenceTests(unittest.TestCase):
    """Guard the evidence scale, not an implementation of modem DSP."""

    @staticmethod
    def observations():
        slots = [{"index": i, "first_sample": 10*i, "stop_sample": 10*i+8,
                  "complete": True, "all_valid": True} for i in range(13)]
        plan = {"horizon_samples": 150, "public_slots": [
            {"index": s["index"], "evaluator_transmitting": True,
             "observation": {"first": s["first_sample"], "stop": s["stop_sample"]}}
            for s in slots]}
        return plan, slots

    def test_disjoint_energy_scale(self):
        plan, slots = self.observations()
        result = presence.observation_bound(plan, slots, 151.02573227616068)
        self.assertEqual(result["active_observation_count"], 13)
        self.assertAlmostEqual(result["at_least_one_observation_energy_over_noise_upper_bound"],
                               11.617364021243129)
        self.assertAlmostEqual(result["at_least_one_known_mean_observation_miss_probability_lower_bound"],
                               0.880515, places=6)
        self.assertLess(presence.known_mean_miss(151.02573227616068), 3e-30)

    def test_overlap_prevents_partition_claim(self):
        plan, slots = self.observations()
        slots[1]["first_sample"] = 7
        plan["public_slots"][1]["observation"]["first"] = 7
        with self.assertRaisesRegex(ValueError, "overlap"):
            presence.observation_bound(plan, slots, 151.)

    def test_incomplete_or_mismatched_observation_rejected(self):
        plan, slots = self.observations()
        with self.assertRaises(ValueError):
            presence.observation_bound(plan, slots[:-1], 151.)
        slots[0]["complete"] = False
        with self.assertRaises(ValueError):
            presence.observation_bound(plan, slots, 151.)
        slots[0]["complete"] = True
        slots[0]["stop_sample"] += 1
        with self.assertRaises(ValueError):
            presence.observation_bound(plan, slots, 151.)

    def test_noise_only_limit_and_invalid_energy(self):
        self.assertAlmostEqual(presence.known_mean_miss(0), 1-presence.ALPHA)
        for energy in (-1., float("nan"), float("inf")):
            with self.assertRaises(ValueError):
                presence.known_mean_miss(energy)


class DecisionWindowReferenceTests(unittest.TestCase):
    @staticmethod
    def receipt():
        return {"plan": {"channel_renderer": "hf_vector_oblique", "helper_enabled": False,
            "noise_variance": 2., "horizon_samples": 25,
            "public_slots": [{"index": i, "evaluator_transmitting": True,
                "observation": {"first": 10*i, "stop": 10*i+8}} for i in range(2)]},
            "channel": {"signal_energy_sample_sum": 65., "private_signal_energy_observations": [
                {"public_slot": i, "first_sample": 10*i, "stop_sample": 10*i+8,
                 "samples": 8, "energy_sample_sum": 20.*(i+1)} for i in range(2)]},
            "scenario": {"channel": {"noise": {"white_variance": 2., "colored_variance": 0,
                "impulse_rate_hz": 0, "impulse_rms": 0, "tone_amplitude": 0},
                "frontend": {"adc_bits": 0, "agc_target": 0, "clip_magnitude": 0, "lowpass_hz": 0},
                "transmitter": {"enabled": False}}},
            "slots": [{"index": i, "first_sample": 10*i, "stop_sample": 10*i+8,
                "complete": True, "all_valid": True, "decision": "quiet", "bit": None}
                for i in range(2)]}

    def test_uses_exact_observation_energy_with_unobserved_gaps(self):
        rows = evolving.decision_references(self.receipt())
        self.assertEqual([r["energy_over_noise_variance"] for r in rows], [10., 20.])
        self.assertLess(rows[1]["conditional_known_mean_presence_miss"],
                        rows[0]["conditional_known_mean_presence_miss"])

    def test_rejects_incomplete_or_excess_energy(self):
        receipt = self.receipt()
        receipt["channel"]["private_signal_energy_observations"][0]["samples"] = 7
        with self.assertRaises(ValueError):
            evolving.decision_references(receipt)
        receipt = self.receipt()
        receipt["channel"]["signal_energy_sample_sum"] = 59.
        with self.assertRaises(ValueError):
            evolving.decision_references(receipt)

    def test_event_exposure_uses_propagation_clock_and_model_delay(self):
        receipt = self.receipt()
        receipt["plan"]["sample_rate_hz"] = 2.
        receipt["channel"]["vector_oblique"] = {"modelling_delay_samples": 2,
            "evolution": {"start_seconds": 1., "duration_seconds": 3.}}
        rows = evolving.decision_references(receipt)
        self.assertEqual([r["intrinsic_event_overlap_samples"] for r in rows], [4, 0])
        self.assertEqual(rows[0]["intrinsic_event_overlap_fraction"], .5)

    def test_rejects_other_noise_planes_and_window_mismatch(self):
        receipt = self.receipt()
        receipt["scenario"]["channel"]["noise"]["tone_amplitude"] = .01
        with self.assertRaises(ValueError):
            evolving.decision_references(receipt)
        receipt = self.receipt()
        receipt["slots"][0]["stop_sample"] = 9
        with self.assertRaises(ValueError):
            evolving.decision_references(receipt)


if __name__ == "__main__":
    unittest.main()
