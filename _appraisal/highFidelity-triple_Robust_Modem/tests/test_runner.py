#!/usr/bin/env python3
"""Focused bounded-runner tests; fake subprocesses never implement channel DSP."""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

RUNNER = Path(__file__).resolve().parents[1] / "run.py"
spec = importlib.util.spec_from_file_location("appraisal_run", RUNNER)
assert spec and spec.loader
runner = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = runner
spec.loader.exec_module(runner)


def valid_campaign() -> dict:
    return {
        "schema_version": 1, "name": "test", "max_wall_seconds": 2,
        "per_trial_wall_seconds": 10, "max_trials": 2, "max_samples": 1000,
        "episodes": [
            {"id": "one", "scenario": "awgn", "mode": "receive", "waveform": "smoke", "seed": 1,
             "cn0_db_hz": 12, "control": "wanted", "stratum": "awgn-fixed"},
            {"id": "two", "scenario": "awgn", "mode": "receive", "waveform": "smoke", "seed": 2,
             "cn0_db_hz": 12, "control": "wanted", "stratum": "awgn-fixed"},
        ],
        "confirmatory_strata": {"awgn-fixed": 2},
    }


def fake_completed_receipt(command, *, channel=None, outcomes=None, control=None) -> dict:
    def argument(option, default=None):
        try:
            return command[command.index(option) + 1]
        except ValueError:
            return default

    mode = argument("--mode", "receive")
    backend = argument("--backend", "cpu")
    requested_bit_seconds = float(argument("--bit-seconds", ".001"))
    cn0 = float(argument("--cn0", "32"))
    fec = argument("--fec", "off")
    receipt = {
        "schema_version": 1, "status": "completed",
        "requested_scenario": argument("--scenario", "awgn"),
        "mode": mode, "waveform": argument("--waveform", "smoke"),
        "seed": int(argument("--seed", "1")),
        "control": control or argument("--control", "wanted"),
        "source_identity": {"appraisal_source_sha256": "a" * 64, "native_source_sha256": "b" * 64},
        "plan": {"cn0_db_hz": cn0, "requested_bit_seconds": requested_bit_seconds,
                 "fec": fec,
                 "fade_slots": int(argument("--fade-slots", "2")) if argument("--control", "wanted") == "interior-fade" else None,
                 "helper_enabled": "--helper" in command, "sample_rate_hz": 1920,
                 "capture_preroll_seconds": float(argument("--capture-lead-seconds", ".25")),
                 "capture_preroll_override": "--capture-lead-seconds" in command,
                 "history_reference_delay_seconds": float(argument("--history-reference-delay-seconds", "0")),
                 "hf_phase_bank_requested": "--hf-phase-bank" in command,
                 "hf_public_phase_receiver_requested": "--hf-public-phase-receiver" in command,
                 "horizon_samples": 100, "pulse": argument("--pulse", "rrc"),
                 "requested_message": argument("--message", "e"),
                 "message": ("A" if argument("--control", "wanted") in {"interior-fade", "capture-gap"}
                             and argument("--message", "e") == "e" else argument("--message", "e")),
                 "represented_seconds": float(argument("--duration-seconds", "1"))},
        "backend": {"requested": backend, "device": backend if backend in {"cuda", "vulkan"} else "cpu",
                    "executed_gpu_work": backend in {"cuda", "vulkan"}},
    }
    if mode == "receive":
        receipt["outcomes"] = outcomes or {
            "receiver_executed": True, "component_only": False, "completion_evaluated": True,
            "correct_complete": False, "physical_completions": 0, "actual_scored_slots": 1,
            "premature_completions": 0, "wrong_bits": 0, "false_data": 0,
            "ambiguous_active_slots": 0, "quiet_active_slots": 0, "missed_completion": False,
        }
        if fec != "off":
            receipt["outcomes"].update({"correct_decoded_complete": False,
                                       "communication_endpoint": "correct_decoded_complete", "accepted_messages": 0})
            receipt["messages"] = []
    elif mode == "slot":
        receipt["outcomes"] = outcomes or {
            "receiver_executed": True, "component_only": True, "actual_scored_slots": 1,
            "first_public_observation_complete": True,
            "first_public_observation_correct_data": receipt["control"] == "wanted",
            "correct_complete": False, "completion_evaluated": False,
        }
        receipt["slots"] = [{"index": 0, "decision": "data", "complete": True, "all_valid": True}]
    elif mode == "hf-search":
        selected = int(argument("--search-slot", "0"))
        receipt["plan"].update({"search_slot": selected, "search_timing": argument("--search-timing", "selected"),
            "search_observation": {"first": 20, "stop": 100}, "search_observation_sha256": "c" * 64})
        receipt["hf_public_search"] = {"component_complete": True, "slot_index": selected,
            "receiver_executed": False, "completion_evaluated": False, "capacity_qualified": False,
            "truth_information_passed_to_search": False}
        receipt["outcomes"] = {"receiver_executed": False, "public_search_executed": True,
            "component_only": True, "completion_evaluated": False, "correct_complete": False,
            "correct_decoded_complete": False, "actual_scored_slots": 0,
            "physical_completions": 0, "accepted_messages": 0}
        receipt["hf_search_evaluation"] = {"slot_index": selected, "truth_information_passed_to_search": False,
            "transmitted_active": receipt["control"] != "noise",
            "expected_receiver_bit": 1 if receipt["control"] == "wanted" else None}
        receipt["slots"], receipt["messages"] = [], []
    elif mode == "diagnose":
        receipt["outcomes"] = {"component_only": True, "completion_evaluated": False,
                                "correct_complete": False}
        receipt["vlf_diagnostic"] = {"component_complete": True, "phase": "completed",
                                     "capacity_qualified": False, "complete_receiver_service": False}
    if mode == "channel" or channel is not None:
        receipt["channel"] = channel or {"contains_signal_energy": True, "samples": 100}
    if mode == "oracle":
        selected = argument("--oracle-slot")
        if selected is not None:
            receipt["plan"]["oracle_slot"] = int(selected)
        receipt["oracle"] = {"probability_error": .5,
                              **({"slot_index": int(selected)} if selected is not None else {})}
        if "--history-reference-delay-seconds" in command or "--hf-phase-bank" in command:
            delay = receipt["plan"]["history_reference_delay_seconds"]
            receipt["hf_history_applicable"] = True
            receipt["hf_history"] = {"reference_delay_seconds": delay, "truth_information_passed_to_receiver": False}
        if "--hf-phase-bank" in command:
            receipt["hf_phase_bank"] = {"model_count": 97, "samples": 100,
                "reference_delay_seconds": receipt["plan"]["history_reference_delay_seconds"],
                "truth_information_passed_to_receiver": False, "truth_information_passed_to_phase_bank": False,
                "receiver_executed": False, "completion_evaluated": False, "capacity_qualified": False}
    if "--hf-public-phase-receiver" in command:
        maximum = 1 if mode == "slot" else 5
        receipt["plan"]["scored_slot_horizon"] = maximum
        receipt["hf_public_phase_receiver"] = {"experimental": True, "fixed_weight": .25,
            "model_family": "sinusoidal_phase_97", "truth_information_passed_to_search": False,
            "maximum_slot_reports": maximum, "executed_slots": 1,
            "slots": [{"slot_index": 0, "component_complete": True}]}
    digest_slot = argument("--capture-digest-slot", argument("--search-slot", "0") if mode == "hf-search" else None)
    if digest_slot is not None:
        receipt["plan"].update({"capture_digest_slot": int(digest_slot),
            "capture_digest_slot_override": "--capture-digest-slot" in command,
            "capture_digest_first_sample": 20, "capture_digest_stop_sample": 100})
        receipt.setdefault("channel", {}).update({"selected_observation_complete": True,
            "selected_observation_hashed_samples": 80, "selected_observation_sha256": "c" * 64})
    if fec != "off":
        receipt["plan"].update({"wire_bits": 64 if fec == "rs20" else 80,
                                "short_wire_bit_limit": 304 if fec == "rs20" else 400,
                                "content_limit_bytes": 16, "long_form": False})
    return receipt


def fake_fec_repaired_receipt(command) -> dict:
    """Framing/statistics fixture, not an encoder or decoder implementation."""
    receipt = fake_completed_receipt(command)
    plan = receipt["plan"]
    wire = plan["wire_bits"]
    receipt["outcomes"].update({"correct_complete": False, "correct_decoded_complete": True,
        "physical_completions": 1, "accepted_messages": 1, "actual_scored_slots": wire + 2,
        "wrong_bits": 1, "ambiguous_active_slots": 1})
    def area(received, corrected=0, missing=0, corrected_bytes=0, erased_bytes=0, repaired_bytes=0):
        return dict(received_bits=received, corrected_bits=corrected, missing_bits=missing,
                    corrected_bytes=corrected_bytes, erased_bytes=erased_bytes, repaired_bytes=repaired_bytes)
    receipt["messages"] = [{"first_public_slot": 0, "stop_public_slot": wire + 2,
        "physical_complete": True, "premature": False, "decoded": True, "matches_transmitted_message": True,
        "slots_consumed": wire + 2, "observed_bits": wire - 1, "missing_slots": 1,
        "erasure_slot_indices": [5], "trailing_quiet_slots": [wire, wire + 1], "quiet_slots": 2,
        "long_form": False, "message_ready_for_decode": True, "fec": plan["fec"],
        "decoded_payload_bytes": list(plan["message"].encode("ascii")), "decoded_endpoint_matches": True,
        "consumed_bytes": wire // 8, "corrected_bytes": 1, "authenticated": False,
        "fec_stats": {"data": area(15, 1, 1, 1, 1, 1), "integrity": area(32), "parity": area(wire - 48)}}]
    return receipt


class CampaignValidationTests(unittest.TestCase):
    def test_campaigns_are_finite_and_well_formed(self) -> None:
        for path in sorted((RUNNER.parent / "campaigns").glob("*.json")):
            campaign = runner._read_campaign(path)
            self.assertLessEqual(len(campaign["episodes"]), campaign["max_trials"])
            self.assertGreaterEqual(campaign["max_wall_seconds"], 1)

    def test_unknown_keys_and_duplicate_seeds_in_confirmatory_strata_fail(self) -> None:
        campaign = valid_campaign()
        campaign["surprise"] = True
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            path.write_text(json.dumps(campaign))
            with self.assertRaises(runner.CampaignError):
                runner._read_campaign(path)
            campaign = valid_campaign()
            campaign["episodes"][1]["seed"] = 1
            path.write_text(json.dumps(campaign))
            with self.assertRaisesRegex(runner.CampaignError, "distinct seeds"):
                runner._read_campaign(path)

    def test_confirmatory_stratum_must_be_homogeneous(self) -> None:
        campaign = valid_campaign()
        campaign["episodes"][1]["cn0_db_hz"] = 8
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            path.write_text(json.dumps(campaign))
            with self.assertRaisesRegex(runner.CampaignError, "mixes episode settings.*cn0_db_hz"):
                runner._read_campaign(path)

    def test_confirmatory_stratum_includes_added_episode_inputs(self) -> None:
        for field, first, second in (("chunk", 256, 512), ("message", "A", "B"),
                                     ("pulse", "rrc", "tapered-rrc"),
                                     ("capture_lead_seconds", .25, 2),
                                     ("max_samples", 800, 900),
                                     ("max_wall_seconds", .5, .6)):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as directory:
                campaign = valid_campaign()
                campaign["episodes"][0][field] = first
                campaign["episodes"][1][field] = second
                path = Path(directory) / "campaign.json"
                path.write_text(json.dumps(campaign))
                with self.assertRaisesRegex(runner.CampaignError, f"mixes episode settings.*{field}"):
                    runner._read_campaign(path)

    def test_slot_and_optional_episode_limits_validate_and_reject_bad_values(self) -> None:
        campaign = valid_campaign()
        campaign["confirmatory_strata"] = {}
        campaign["episodes"] = [{"id": "slot", "scenario": "awgn", "mode": "slot",
                                 "waveform": "reference", "seed": 1, "control": "wanted",
                                 "chunk": 16384, "message": "ABCDEFGHIJKLMN",
                                 "max_wall_seconds": .25, "max_samples": 1000}]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            path.write_text(json.dumps(campaign))
            self.assertEqual(runner._read_campaign(path)["episodes"][0]["mode"], "slot")
            noise = json.loads(json.dumps(campaign))
            noise["episodes"][0]["control"] = "noise"
            path.write_text(json.dumps(noise))
            self.assertEqual(runner._read_campaign(path)["episodes"][0]["control"], "noise")
            invalid_episodes = (
                {"oracle_slot": -1, "mode": "oracle"},
                {"oracle_slot": 1, "mode": "receive"},
                {"oracle_slot": True, "mode": "oracle"},
                {"chunk": 0}, {"chunk": 16385}, {"chunk": 1.5},
                {"pulse": "unknown"}, {"pulse": []},
                {"message": ""}, {"message": "x" * 17}, {"message": "a\n"}, {"message": "é"},
                {"max_wall_seconds": 10.01}, {"max_wall_seconds": 3}, {"max_samples": 1001},
                {"control": "eof", "mode": "slot"},
                {"control": "wrong-key", "mode": "slot"},
                {"control": "wrong-ordinal", "mode": "slot"},
                {"control": "capture-gap", "mode": "slot"},
                {"control": "interior-fade", "mode": "slot"},
            )
            for overrides in invalid_episodes:
                with self.subTest(overrides=overrides):
                    bad = json.loads(json.dumps(campaign))
                    bad["episodes"][0].update(overrides)
                    path.write_text(json.dumps(bad))
                    with self.assertRaises(runner.CampaignError):
                        runner._read_campaign(path)

    def test_diagnose_mode_inputs_are_generic_but_scoped(self) -> None:
        campaign = {"schema_version": 1, "name": "diagnose", "max_wall_seconds": 2,
                    "per_trial_wall_seconds": 2, "max_trials": 1, "max_samples": 1000,
                    "episodes": [{"id": "vlf-diagnose", "scenario": "vlf-phase-wander",
                                  "mode": "diagnose", "waveform": "reference", "seed": 1}]}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            path.write_text(json.dumps(campaign))
            self.assertEqual(runner._read_campaign(path)["episodes"][0]["scenario"], "vlf-phase-wander")
            for overrides in ({"control": "noise"}, {"helper": True}, {"waveform": "smoke"}):
                with self.subTest(overrides=overrides):
                    bad = json.loads(json.dumps(campaign))
                    bad["episodes"][0].update(overrides)
                    path.write_text(json.dumps(bad))
                    with self.assertRaises(runner.CampaignError):
                        runner._read_campaign(path)
            diagnostic_stratum = json.loads(json.dumps(campaign))
            diagnostic_stratum["episodes"][0]["stratum"] = "diagnostic"
            diagnostic_stratum["confirmatory_strata"] = {"diagnostic": 1}
            path.write_text(json.dumps(diagnostic_stratum))
            with self.assertRaisesRegex(runner.CampaignError, "wanted receive episodes"):
                runner._read_campaign(path)

    def test_hf_search_inputs_are_explicitly_scoped(self) -> None:
        episode = {"id": "search", "scenario": "hf-vertical-eikonal-motion", "mode": "hf-search",
                   "waveform": "reference", "seed": 1, "search_slot": 1, "search_timing": "full"}
        runner._validate_episode(episode, 0, set(), 5, 1000)
        command = runner._command(Path("/native/probe"), episode, 1000, 5)
        self.assertEqual(command[command.index("--search-slot") + 1], "1")
        self.assertEqual(command[command.index("--search-timing") + 1], "full")
        for overrides in ({"search_slot": True}, {"search_slot": -1}, {"search_slot": 2**64},
                          {"search_timing": []}, {"search_timing": "adapted"}, {"helper": True},
                          {"backend": "cuda"}, {"control": "capture-gap"}, {"control": "interior-fade"},
                          {"mode": "receive"}, {"oracle_slot": 0}, {"hf_phase_bank": True}):
            with self.subTest(overrides=overrides), self.assertRaises(runner.CampaignError):
                runner._validate_episode({**episode, **overrides}, 0, set(), 5, 1000)
        for control in ("wanted", "noise", "wrong-key", "wrong-ordinal"):
            runner._validate_episode({**episode, "control": control}, 0, set(), 5, 1000)

    def test_operational_phase_flag_is_scoped_and_receipt_bound(self) -> None:
        episode = {"id": "phase", "scenario": "awgn", "mode": "receive", "waveform": "smoke",
                   "seed": 1, "hf_public_phase_receiver": True}
        runner._validate_episode(episode, 0, set(), 5, 1000)
        command = runner._command(Path("/native/probe"), episode, 1000, 5)
        self.assertIn("--hf-public-phase-receiver", command)
        runner._validate_completed_receipt(fake_completed_receipt(command), episode)
        for control in ("wanted", "noise", "wrong-key", "wrong-ordinal", "interior-fade", "capture-gap"):
            runner._validate_episode({**episode, "control": control}, 0, set(), 5, 1000)
        for override in ({"hf_public_phase_receiver": 1}, {"mode": "hf-search"}, {"mode": "oracle"},
                         {"helper": True}, {"backend": "cuda"}):
            with self.subTest(override=override), self.assertRaises(runner.CampaignError):
                runner._validate_episode({**episode, **override}, 0, set(), 5, 1000)
        for section, field, value in (("plan", "hf_public_phase_receiver_requested", False),
                ("hf_public_phase_receiver", "experimental", False),
                ("hf_public_phase_receiver", "truth_information_passed_to_search", True),
                ("hf_public_phase_receiver", "fixed_weight", .5),
                ("hf_public_phase_receiver", "maximum_slot_reports", 6),
                ("hf_public_phase_receiver", "executed_slots", 2),
                ("hf_public_phase_receiver", "slots", [{"slot_index": 5, "component_complete": True}])):
            with self.subTest(section=section, field=field):
                bad = fake_completed_receipt(command)
                bad[section][field] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(bad, episode)
        no_callbacks = fake_completed_receipt(command)
        no_callbacks["hf_public_phase_receiver"].update(executed_slots=0, slots=[])
        runner._validate_completed_receipt(no_callbacks, episode)

    def test_capture_digest_is_evaluator_only_and_binds_complete_observation(self) -> None:
        episode = {"id": "digest", "scenario": "awgn", "mode": "receive", "waveform": "smoke",
                   "seed": 1, "capture_digest_slot": 1}
        runner._validate_episode(episode, 0, set(), 5, 1000)
        command = runner._command(Path("/native/probe"), episode, 1000, 5)
        self.assertEqual(command[command.index("--capture-digest-slot") + 1], "1")
        runner._validate_completed_receipt(fake_completed_receipt(command), episode)
        for override in ({"capture_digest_slot": True}, {"capture_digest_slot": -1},
                         {"capture_digest_slot": 2**64}, {"mode": "oracle"}, {"mode": "slot"},
                         {"mode": "hf-search", "search_slot": 0}):
            with self.subTest(override=override), self.assertRaises(runner.CampaignError):
                runner._validate_episode({**episode, **override}, 0, set(), 5, 1000)
        for section, field, value in (("plan", "capture_digest_slot", 0),
                ("plan", "capture_digest_slot_override", False), ("plan", "capture_digest_stop_sample", 101),
                ("channel", "selected_observation_complete", False),
                ("channel", "selected_observation_hashed_samples", 79),
                ("channel", "selected_observation_sha256", None)):
            with self.subTest(section=section, field=field):
                bad = fake_completed_receipt(command)
                bad[section][field] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(bad, episode)

    def test_command_passes_slot_and_episode_overrides_as_argv(self) -> None:
        episode = {"id": "oracle", "scenario": "awgn", "mode": "oracle", "waveform": "reference",
                   "seed": 8, "oracle_slot": 3, "chunk": 4096, "message": "AB",
                   "max_wall_seconds": 2, "max_samples": 700}
        episode["pulse"] = "tapered-rrc"
        command = runner._command(Path("/native/probe"), episode, episode["max_samples"],
                                  episode["max_wall_seconds"])
        for option, value in (("--oracle-slot", "3"), ("--chunk", "4096"), ("--message", "AB"),
                              ("--pulse", "tapered-rrc"),
                              ("--max-samples", "700"), ("--max-wall-seconds", "2")):
            self.assertEqual(command[command.index(option) + 1], value)
        runner._validate_completed_receipt(fake_completed_receipt(command), episode)

    def test_interior_fade_slots_are_scoped_and_bound_to_receipts(self) -> None:
        campaign = valid_campaign()
        campaign["confirmatory_strata"] = {}
        episode = {"id": "one-fade", "scenario": "awgn", "mode": "receive", "waveform": "smoke",
                   "seed": 3, "control": "interior-fade", "fade_slots": 1}
        campaign["episodes"] = [episode]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            for slots in (1, 2):
                episode["fade_slots"] = slots
                path.write_text(json.dumps(campaign))
                runner._read_campaign(path)
                command = runner._command(Path("/native/probe"), episode, 1000, 2)
                self.assertEqual(command[command.index("--fade-slots") + 1], str(slots))
                receipt = fake_completed_receipt(command)
                runner._validate_completed_receipt(receipt, episode)
                for value in (None, True, 3 - slots):
                    bad = json.loads(json.dumps(receipt))
                    bad["plan"]["fade_slots"] = value
                    with self.assertRaisesRegex(runner.CampaignError, "fade slots"):
                        runner._validate_completed_receipt(bad, episode)
                del receipt["plan"]["fade_slots"]
                with self.assertRaisesRegex(runner.CampaignError, "fade slots"):
                    runner._validate_completed_receipt(receipt, episode)
            for overrides in ({"fade_slots": 0}, {"fade_slots": 3}, {"fade_slots": True},
                              {"fade_slots": "1"}, {"fade_slots": 1.0}, {"control": "capture-gap"},
                              {"control": "wanted"}, {"control": "noise"}):
                bad = json.loads(json.dumps(campaign))
                bad["episodes"][0].update(overrides)
                path.write_text(json.dumps(bad))
                with self.assertRaises(runner.CampaignError):
                    runner._read_campaign(path)
        implicit = {key: value for key, value in episode.items() if key != "fade_slots"}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), implicit, 1000, 2))
        self.assertEqual(receipt["plan"]["fade_slots"], 2)
        runner._validate_completed_receipt(receipt, implicit)
        receipt["plan"]["fade_slots"] = 1
        with self.assertRaisesRegex(runner.CampaignError, "fade slots"):
            runner._validate_completed_receipt(receipt, implicit)
        del receipt["plan"]["fade_slots"]
        runner._validate_completed_receipt(receipt, implicit)  # Original two-slot receipts remain valid.

    def test_capture_lead_is_bounded_and_bound_to_receipts(self) -> None:
        campaign = valid_campaign()
        campaign["confirmatory_strata"] = {}
        episode = {"id": "helper-lead", "scenario": "awgn", "mode": "receive", "waveform": "smoke",
                   "seed": 3, "helper": True, "capture_lead_seconds": 2}
        campaign["episodes"] = [episode]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            for lead in (.25, 2, 10):
                episode["capture_lead_seconds"] = lead
                path.write_text(json.dumps(campaign))
                runner._read_campaign(path)
                command = runner._command(Path("/native/probe"), episode, 1000, 2)
                self.assertEqual(command[command.index("--capture-lead-seconds") + 1], str(lead))
                receipt = fake_completed_receipt(command)
                runner._validate_completed_receipt(receipt, episode)
                for value in (None, True, lead + .01):
                    bad = json.loads(json.dumps(receipt))
                    bad["plan"]["capture_preroll_seconds"] = value
                    with self.assertRaisesRegex(runner.CampaignError, "capture lead"):
                        runner._validate_completed_receipt(bad, episode)
                del receipt["plan"]["capture_preroll_seconds"]
                with self.assertRaisesRegex(runner.CampaignError, "capture lead"):
                    runner._validate_completed_receipt(receipt, episode)
            for value in (0, .249, 10.001, True, "2", None, float("nan"), float("inf")):
                episode["capture_lead_seconds"] = value
                path.write_text(json.dumps(campaign))
                with self.assertRaises(runner.CampaignError):
                    runner._read_campaign(path)
        implicit = {key: value for key, value in episode.items() if key != "capture_lead_seconds"}
        receipt = fake_completed_receipt(runner._command(Path("/native/probe"), implicit, 1000, 2))
        runner._validate_completed_receipt(receipt, implicit)
        receipt["plan"]["capture_preroll_seconds"] = 2
        with self.assertRaisesRegex(runner.CampaignError, "capture lead"):
            runner._validate_completed_receipt(receipt, implicit)
        receipt["plan"]["capture_preroll_seconds"] = .25
        receipt["plan"]["capture_preroll_override"] = True
        with self.assertRaisesRegex(runner.CampaignError, "capture lead override"):
            runner._validate_completed_receipt(receipt, implicit)

    def test_private_hf_diagnostic_options_are_strict_and_recorded(self) -> None:
        campaign = valid_campaign()
        campaign["confirmatory_strata"] = {}
        episode = {"id": "hf-diagnostic", "scenario": "hf-quiet", "mode": "oracle", "waveform": "reference",
                   "seed": 3, "history_reference_delay_seconds": .002, "hf_phase_bank": True}
        campaign["episodes"] = [episode]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            path.write_text(json.dumps(campaign))
            runner._read_campaign(path)
            for overrides in ({"history_reference_delay_seconds": -1}, {"history_reference_delay_seconds": True},
                              {"history_reference_delay_seconds": "0.002"}, {"history_reference_delay_seconds": float("inf")},
                              {"history_reference_delay_seconds": float("nan")}, {"hf_phase_bank": 1},
                              {"hf_phase_bank": "yes"}, {"mode": "receive"}, {"mode": "plan"}):
                with self.subTest(overrides=overrides):
                    bad = json.loads(json.dumps(campaign))
                    bad["episodes"][0].update(overrides)
                    path.write_text(json.dumps(bad))
                    with self.assertRaises(runner.CampaignError):
                        runner._read_campaign(path)
        command = runner._command(Path("/native/probe"), episode, 1000, 2)
        self.assertEqual(command[command.index("--history-reference-delay-seconds") + 1], "0.002")
        self.assertIn("--hf-phase-bank", command)
        receipt = fake_completed_receipt(command)
        runner._validate_completed_receipt(receipt, episode)
        for section, field, value in (("plan", "history_reference_delay_seconds", 0),
                                      ("hf_history", "reference_delay_seconds", 0),
                                      ("plan", "hf_phase_bank_requested", False),
                                      ("hf_phase_bank", "samples", 0),
                                      ("hf_phase_bank", "reference_delay_seconds", 0),
                                      ("hf_phase_bank", "truth_information_passed_to_phase_bank", True),
                                      ("hf_phase_bank", "receiver_executed", True)):
            with self.subTest(section=section, field=field):
                bad = json.loads(json.dumps(receipt))
                bad[section][field] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(bad, episode)
        disabled = dict(episode, hf_phase_bank=False)
        command = runner._command(Path("/native/probe"), disabled, 1000, 2)
        self.assertNotIn("--hf-phase-bank", command)
        disabled_receipt = fake_completed_receipt(command)
        runner._validate_completed_receipt(disabled_receipt, disabled)
        disabled_receipt["hf_phase_bank"] = receipt["hf_phase_bank"]
        with self.assertRaisesRegex(runner.CampaignError, "unrequested phase-bank"):
            runner._validate_completed_receipt(disabled_receipt, disabled)
        undeclared = {key: value for key, value in episode.items() if key not in {"history_reference_delay_seconds", "hf_phase_bank"}}
        command = runner._command(Path("/native/probe"), undeclared, 1000, 2)
        undeclared_receipt = fake_completed_receipt(command)
        undeclared_receipt["plan"]["history_reference_delay_seconds"] = .002
        with self.assertRaisesRegex(runner.CampaignError, "reference delay"):
            runner._validate_completed_receipt(undeclared_receipt, undeclared)

    def test_fec_options_and_native_identity_are_strict(self) -> None:
        campaign = valid_campaign()
        campaign["confirmatory_strata"] = {}
        campaign["episodes"] = [dict(campaign["episodes"][0], fec="rs20")]
        campaign["episodes"][0].pop("stratum")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campaign.json"
            for fec in ("off", "rs20", "rs60"):
                campaign["episodes"][0]["fec"] = fec
                path.write_text(json.dumps(campaign))
                runner._read_campaign(path)
                command = runner._command(Path("/native/probe"), campaign["episodes"][0], 1000, 1)
                self.assertEqual(command[command.index("--fec") + 1], fec)
                runner._validate_completed_receipt(fake_completed_receipt(command), campaign["episodes"][0])
            for fec in ("RS20", "rs40", True, 20, None):
                campaign["episodes"][0]["fec"] = fec
                path.write_text(json.dumps(campaign))
                with self.assertRaises(runner.CampaignError):
                    runner._read_campaign(path)
        episode = dict(valid_campaign()["episodes"][0], fec="rs60")
        command = runner._command(Path("/native/probe"), episode, 1000, 1)
        for field, value in (("fec", "off"), ("wire_bits", 408), ("wire_bits", 79),
                             ("short_wire_bit_limit", 208), ("content_limit_bytes", 17), ("long_form", True)):
            with self.subTest(field=field, value=value):
                receipt = fake_completed_receipt(command)
                receipt["plan"][field] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(receipt, episode)

    def test_fec_decoded_success_requires_exact_frame_payload_and_repair_receipts(self) -> None:
        episode = dict(valid_campaign()["episodes"][0], fec="rs20")
        command = runner._command(Path("/native/probe"), episode, 1000, 1)
        receipt = fake_fec_repaired_receipt(command)
        runner._validate_completed_receipt(receipt, episode)
        self.assertFalse(receipt["outcomes"]["correct_complete"])
        self.assertTrue(receipt["outcomes"]["correct_decoded_complete"])
        for section, field, value in (
            ("outcomes", "accepted_messages", 2), ("outcomes", "accepted_messages", True),
            ("outcomes", "physical_completions", 2), ("outcomes", "premature_completions", 1),
            ("outcomes", "actual_scored_slots", 1),
            ("outcomes", "false_data", 1), ("outcomes", "wrong_bits", 0),
            ("outcomes", "ambiguous_active_slots", 0), ("outcomes", "correct_complete", True),
            ("outcomes", "communication_endpoint", "correct_complete"),
            ("message", "first_public_slot", 1), ("message", "stop_public_slot", 65),
            ("message", "slots_consumed", 65), ("message", "observed_bits", 64),
            ("message", "trailing_quiet_slots", [63, 64]), ("message", "long_form", True),
            ("message", "message_ready_for_decode", False), ("message", "consumed_bytes", 7),
            ("message", "erasure_slot_indices", [64]), ("message", "erasure_slot_indices", []),
            ("message", "erasure_slot_indices", [21]),
            ("message", "decoded_payload_bytes", [32]), ("message", "decoded_endpoint_matches", False),
            ("message", "fec", "rs60"), ("message", "fec_stats", None),
            ("message", "corrected_bytes", -1), ("message", "corrected_bytes", True),
            ("message", "corrected_bytes", 0), ("message", "corrected_bytes", 2),
            ("message", "authenticated", True), ("message", "authenticated", None)):
            with self.subTest(section=section, field=field):
                bad = json.loads(json.dumps(receipt))
                target = bad["messages"][0] if section == "message" else bad[section]
                target[field] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(bad, episode)
        bad = json.loads(json.dumps(receipt))
        bad["messages"][0]["fec_stats"]["data"]["repaired_bytes"] = 3
        with self.assertRaisesRegex(runner.CampaignError, "repair statistics"):
            runner._validate_completed_receipt(bad, episode)
        bad = json.loads(json.dumps(receipt))
        bad["messages"][0]["fec_stats"]["data"]["received_bits"] += 8
        bad["messages"][0]["fec_stats"]["integrity"]["received_bits"] -= 8
        with self.assertRaisesRegex(runner.CampaignError, "region extent"):
            runner._validate_completed_receipt(bad, episode)
        # An erased zero placeholder can already equal its decoded value:
        # it is repaired coverage without a changed-byte count.
        zero_erasure = json.loads(json.dumps(receipt))
        zero_erasure["outcomes"]["wrong_bits"] = 0
        zero_erasure["messages"][0]["corrected_bytes"] = 0
        zero_erasure["messages"][0]["fec_stats"]["data"]["corrected_bits"] = 0
        zero_erasure["messages"][0]["fec_stats"]["data"]["corrected_bytes"] = 0
        runner._validate_completed_receipt(zero_erasure, episode)
        for control in ("noise", "wrong-key", "wrong-ordinal", "eof"):
            negative = dict(episode, control=control)
            command = runner._command(Path("/native/probe"), negative, 1000, 1)
            with self.assertRaisesRegex(runner.CampaignError, "target or physical"):
                runner._validate_completed_receipt(fake_fec_repaired_receipt(command), negative)

    def test_fec_statistics_select_decoded_endpoint_without_relabeling_raw_success(self) -> None:
        campaign = valid_campaign()
        trials = []
        for episode in campaign["episodes"]:
            episode["fec"] = "rs20"
            command = runner._command(Path("/native/probe"), episode, 1000, 1)
            receipt = fake_fec_repaired_receipt(command)
            trials.append({"episode": episode, "status": "completed", "receipt": receipt})
        stats = runner._statistical_summary(campaign, trials)["awgn-fixed"]
        self.assertEqual(stats["successes"], 2)
        self.assertEqual(stats["communication_endpoint"], "correct_decoded_complete")
        for episode in campaign["episodes"]:
            episode.pop("fec")
        stats = runner._statistical_summary(campaign, trials)["awgn-fixed"]
        self.assertEqual(stats["successes"], 0)
        self.assertEqual(stats["communication_endpoint"], "correct_complete")

    def test_completed_receive_receipt_requires_identity_and_complete_outcome(self) -> None:
        episode = valid_campaign()["episodes"][0]
        command = runner._command(Path("/native/probe"), episode, 1000, 1)
        receipt = fake_completed_receipt(command)
        receipt.pop("mode")
        with self.assertRaisesRegex(runner.CampaignError, "identity fields: mode"):
            runner._validate_completed_receipt(receipt, episode)

        receipt = fake_completed_receipt(command)
        receipt["outcomes"].pop("correct_complete")
        with self.assertRaisesRegex(runner.CampaignError, "outcomes.correct_complete"):
            runner._validate_completed_receipt(receipt, episode)

    def test_completed_receipt_settings_must_match_episode(self) -> None:
        episode = valid_campaign()["episodes"][0]
        command = runner._command(Path("/native/probe"), episode, 1000, 1)
        receipt = fake_completed_receipt(command)
        receipt["plan"]["cn0_db_hz"] = 8
        with self.assertRaisesRegex(runner.CampaignError, "C/N0"):
            runner._validate_completed_receipt(receipt, episode)
        receipt = fake_completed_receipt(command)
        receipt["plan"]["pulse"] = "tapered-rrc"
        with self.assertRaisesRegex(runner.CampaignError, "pulse"):
            runner._validate_completed_receipt(receipt, episode)
        receipt = fake_completed_receipt(command)
        receipt["plan"]["requested_message"] = "wrong"
        with self.assertRaisesRegex(runner.CampaignError, "requested message"):
            runner._validate_completed_receipt(receipt, episode)

    def test_receive_receipt_requires_real_execution_and_coherent_success(self) -> None:
        episode = valid_campaign()["episodes"][0]
        command = runner._command(Path("/native/probe"), episode, 1000, 1)
        for field, value in (("receiver_executed", False), ("component_only", True),
                             ("completion_evaluated", False), ("actual_scored_slots", 0)):
            with self.subTest(field=field):
                receipt = fake_completed_receipt(command)
                receipt["outcomes"][field] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(receipt, episode)

        for field, value in (("physical_completions", 0), ("wrong_bits", 1),
                             ("false_data", 1), ("ambiguous_active_slots", 1),
                             ("quiet_active_slots", 1), ("missed_completion", True)):
            with self.subTest(success_mutant=field):
                receipt = fake_completed_receipt(command)
                receipt["outcomes"].update({"correct_complete": True,
                                            "physical_completions": 1,
                                            "missed_completion": False})
                receipt["outcomes"][field] = value
                with self.assertRaisesRegex(runner.CampaignError, "conflicts"):
                    runner._validate_completed_receipt(receipt, episode)

    def test_requested_and_effective_message_are_checked(self) -> None:
        episode = {"id": "transition", "scenario": "hf-transition", "mode": "receive",
                   "waveform": "reference", "seed": 9204, "message": "A"}
        command = runner._command(Path("/native/probe"), episode, 1000, 1)
        receipt = fake_completed_receipt(command)
        runner._validate_completed_receipt(receipt, episode)
        receipt["plan"]["requested_message"] = "e"
        with self.assertRaisesRegex(runner.CampaignError, "requested message"):
            runner._validate_completed_receipt(receipt, episode)

        fade_episode = {"id": "fade", "scenario": "awgn", "mode": "receive",
                        "waveform": "reference", "seed": 9205, "control": "interior-fade"}
        fade_command = runner._command(Path("/native/probe"), fade_episode, 1000, 1)
        fade_receipt = fake_completed_receipt(fade_command)
        runner._validate_completed_receipt(fade_receipt, fade_episode)
        fade_receipt["plan"]["message"] = "e"
        with self.assertRaisesRegex(runner.CampaignError, "effective message"):
            runner._validate_completed_receipt(fade_receipt, fade_episode)

    def test_channel_requested_duration_matches_reported_geometry(self) -> None:
        episode = {"id": "channel-duration", "scenario": "eme-disk-episode", "mode": "channel",
                   "waveform": "smoke", "seed": 4, "duration_seconds": .25}
        command = runner._command(Path("/native/probe"), episode, 1000, 1)
        receipt = fake_completed_receipt(command)
        receipt["channel"] = {"contains_signal_energy": True, "samples": 480}
        runner._validate_completed_receipt(receipt, episode)
        receipt["plan"]["represented_seconds"] = .5
        with self.assertRaisesRegex(runner.CampaignError, "duration"):
            runner._validate_completed_receipt(receipt, episode)

    def test_slot_receipt_validates_single_observation_without_message_success(self) -> None:
        episode = {"id": "slot", "scenario": "awgn", "mode": "slot", "waveform": "reference",
                   "seed": 3, "control": "wanted"}
        command = runner._command(Path("/native/probe"), episode, 1000, 2)
        receipt = fake_completed_receipt(command)
        runner._validate_completed_receipt(receipt, episode)
        self.assertFalse(receipt["outcomes"]["correct_complete"])

        for decision in ("quiet", "ambiguous"):
            with self.subTest(decision=decision):
                unresolved = fake_completed_receipt(command)
                unresolved["outcomes"]["first_public_observation_correct_data"] = False
                unresolved["slots"][0]["decision"] = decision
                runner._validate_completed_receipt(unresolved, episode)

        invalid_edits = (
            ("receiver_executed", False), ("component_only", False), ("actual_scored_slots", 2),
            ("first_public_observation_complete", False), ("first_public_observation_correct_data", None),
            ("correct_complete", True), ("completion_evaluated", True),
        )
        for field, value in invalid_edits:
            with self.subTest(field=field):
                bad = fake_completed_receipt(command)
                bad["outcomes"][field] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(bad, episode)
        bad_score = fake_completed_receipt(command)
        bad_score["slots"][0]["decision"] = "invalid"
        with self.assertRaisesRegex(runner.CampaignError, "invalid first slot decision"):
            runner._validate_completed_receipt(bad_score, episode)

    def test_noise_slot_receipt_preserves_false_data_without_message_success(self) -> None:
        episode = {"id": "noise-slot", "scenario": "awgn", "mode": "slot", "waveform": "reference",
                   "seed": 3, "control": "noise"}
        command = runner._command(Path("/native/probe"), episode, 1000, 2)
        for decision in ("quiet", "ambiguous", "data"):
            with self.subTest(decision=decision):
                receipt = fake_completed_receipt(command)
                receipt["slots"][0]["decision"] = decision
                receipt["outcomes"]["false_data"] = int(decision == "data")
                runner._validate_completed_receipt(receipt, episode)
                self.assertFalse(receipt["outcomes"]["first_public_observation_correct_data"])
                self.assertFalse(receipt["outcomes"]["correct_complete"])
                self.assertFalse(receipt["outcomes"]["completion_evaluated"])
        lying = fake_completed_receipt(command)
        lying["outcomes"]["first_public_observation_correct_data"] = True
        with self.assertRaisesRegex(runner.CampaignError, "noise-only slot.*correct DATA"):
            runner._validate_completed_receipt(lying, episode)
        for field in ("correct_complete", "completion_evaluated"):
            with self.subTest(field=field):
                lying = fake_completed_receipt(command)
                lying["outcomes"][field] = True
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(lying, episode)

    def test_oracle_selected_slot_must_match_requested_index(self) -> None:
        episode = {"id": "oracle-slot", "scenario": "awgn", "mode": "oracle",
                   "waveform": "reference", "seed": 4, "oracle_slot": 2}
        command = runner._command(Path("/native/probe"), episode, 1000, 2)
        receipt = fake_completed_receipt(command)
        runner._validate_completed_receipt(receipt, episode)
        receipt["oracle"]["slot_index"] = 1
        with self.assertRaisesRegex(runner.CampaignError, "selected oracle_slot"):
            runner._validate_completed_receipt(receipt, episode)

    def test_diagnose_receipt_is_component_only_and_not_complete_service(self) -> None:
        episode = {"id": "diagnose", "scenario": "vlf-phase-wander", "mode": "diagnose",
                   "waveform": "reference", "seed": 3}
        command = runner._command(Path("/native/probe"), episode, 1000, 2)
        receipt = fake_completed_receipt(command)
        runner._validate_completed_receipt(receipt, episode)
        for section, field, value in (("vlf_diagnostic", "component_complete", False),
                                      ("vlf_diagnostic", "phase", "fine_prefix"),
                                      ("vlf_diagnostic", "capacity_qualified", True),
                                      ("vlf_diagnostic", "complete_receiver_service", True),
                                      ("outcomes", "component_only", False),
                                      ("outcomes", "completion_evaluated", True),
                                      ("outcomes", "correct_complete", True)):
            with self.subTest(section=section, field=field):
                invalid = fake_completed_receipt(command)
                invalid[section][field] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(invalid, episode)

    def test_hf_search_receipts_bind_public_inputs_and_do_not_claim_reception(self) -> None:
        episode = {"id": "search", "scenario": "awgn", "mode": "hf-search", "waveform": "smoke",
                   "seed": 9, "search_slot": 1, "search_timing": "full"}
        command = runner._command(Path("/native/probe"), episode, 1000, 2)
        runner._validate_completed_receipt(fake_completed_receipt(command), episode)
        for section, field, value in (("plan", "search_slot", 0), ("plan", "search_timing", "selected"),
                ("plan", "search_observation", {"first": 20, "stop": 99}),
                ("plan", "search_observation_sha256", "missing"),
                ("hf_public_search", "slot_index", 0), ("hf_public_search", "component_complete", False),
                ("hf_public_search", "truth_information_passed_to_search", True),
                ("hf_public_search", "capacity_qualified", True), ("hf_public_search", "receiver_executed", True),
                ("outcomes", "receiver_executed", True), ("outcomes", "public_search_executed", False),
                ("outcomes", "correct_complete", True), ("outcomes", "correct_decoded_complete", True),
                ("outcomes", "completion_evaluated", True), ("outcomes", "actual_scored_slots", 1),
                ("outcomes", "physical_completions", 1), ("outcomes", "accepted_messages", 1),
                ("hf_search_evaluation", "truth_information_passed_to_search", True),
                ("hf_search_evaluation", "expected_receiver_bit", True)):
            with self.subTest(section=section, field=field):
                invalid = fake_completed_receipt(command)
                invalid[section][field] = value
                with self.assertRaises(runner.CampaignError):
                    runner._validate_completed_receipt(invalid, episode)
        for control in ("noise", "wrong-key", "wrong-ordinal"):
            negative = {**episode, "control": control}
            negative_command = runner._command(Path("/native/probe"), negative, 1000, 2)
            receipt = fake_completed_receipt(negative_command)
            runner._validate_completed_receipt(receipt, negative)
            receipt["hf_search_evaluation"]["expected_receiver_bit"] = 1
            with self.assertRaises(runner.CampaignError):
                runner._validate_completed_receipt(receipt, negative)

    def test_oracle_component_completion_flag_does_not_override_oracle_result(self) -> None:
        episode = {"id": "oracle", "scenario": "awgn", "mode": "oracle",
                   "waveform": "reference", "seed": 7}
        command = runner._command(Path("/native/probe"), episode, 1000, 1)
        receipt = fake_completed_receipt(command)
        receipt["outcomes"] = {"correct_complete": False}
        runner._validate_completed_receipt(receipt, episode)

    def test_gpu_backend_validation_accepts_real_device_ordinals_and_rejects_fallback(self) -> None:
        for requested, actual in (("cuda", "cuda:0"), ("vulkan", "vulkan:0"),
                                  ("gpu", "cuda:0"), ("gpu", "vulkan:3")):
            with self.subTest(requested=requested, actual=actual):
                episode = {"id": "gpu", "scenario": "awgn", "mode": "receive",
                           "waveform": "reference", "seed": 9, "backend": requested}
                command = runner._command(Path("/native/probe"), episode, 1000, 1)
                receipt = fake_completed_receipt(command)
                receipt["backend"].update({"device": actual, "executed_gpu_work": True})
                runner._validate_completed_receipt(receipt, episode)

        for requested, actual, executed in (("cuda", "cpu", False), ("vulkan", "cpu", False),
                                             ("gpu", "gpu", True), ("gpu", "cpu", False)):
            with self.subTest(requested=requested, actual=actual, executed=executed):
                episode = {"id": "gpu", "scenario": "awgn", "mode": "receive",
                           "waveform": "reference", "seed": 9, "backend": requested}
                command = runner._command(Path("/native/probe"), episode, 1000, 1)
                receipt = fake_completed_receipt(command)
                receipt["backend"].update({"device": actual, "executed_gpu_work": executed})
                with self.assertRaisesRegex(runner.CampaignError, "executed GPU device work"):
                    runner._validate_completed_receipt(receipt, episode)

        episode = {"id": "diagnose-gpu", "scenario": "vlf-resonant", "mode": "diagnose",
                   "waveform": "reference", "seed": 9, "backend": "cuda"}
        command = runner._command(Path("/native/probe"), episode, 1000, 1)
        receipt = fake_completed_receipt(command)
        receipt["backend"].update({"device": "cuda:0", "executed_gpu_work": True})
        runner._validate_completed_receipt(receipt, episode)

    def test_command_arguments_remain_separate_and_allowlisted(self) -> None:
        injection = "awgn;touch /tmp/appraisal-runner-injection"
        episode = {"id": "safe", "scenario": injection, "mode": "receive", "waveform": "smoke",
                   "seed": 1, "control": "wanted"}
        command = runner._command(Path("/native/probe"), episode, 17, 1.5)
        self.assertEqual(command[0], "/native/probe")
        self.assertIn("--max-samples", command)
        self.assertIn("--max-wall-seconds", command)
        self.assertIn(injection, command)
        self.assertEqual(command[command.index("--scenario") + 1], injection)
        self.assertLess(float(command[command.index("--max-wall-seconds") + 1]), 2)

    def test_confidence_interval_and_fixed_n_edges(self) -> None:
        lower, upper = runner.clopper_pearson(0, 1)
        self.assertEqual(lower, 0)
        self.assertAlmostEqual(upper, .975, places=10)
        lower, upper = runner.clopper_pearson(1, 1)
        self.assertAlmostEqual(lower, .025, places=10)
        self.assertEqual(upper, 1)
        lower, upper = runner.clopper_pearson(0, 2)
        self.assertEqual(lower, 0)
        self.assertAlmostEqual(upper, 1 - .025 ** .5, places=10)
        lower, upper = runner.clopper_pearson(1, 2)
        self.assertAlmostEqual(lower, .012579117093425, places=10)
        self.assertAlmostEqual(upper, .987420882906575, places=10)
        with self.assertRaises(ValueError):
            runner.clopper_pearson(0, 101)


class CampaignExecutionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.evidence_root = RUNNER.parent / "local-evidence"
        self.evidence_root.mkdir(exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix=".test-runner-", dir=self.evidence_root)
        self.work = Path(self.tmp.name)
        self.probe = Path(sys.executable).resolve()
        self.campaign_file = self.work / "campaign.json"
        self.campaign_file.write_text(json.dumps(valid_campaign()))

    def tearDown(self) -> None:
        self.tmp.cleanup()

    @staticmethod
    def _fake_completed(command, **kwargs):
        # A completed but incorrect message is a legitimate measured outcome.
        kwargs["stdout"].write((json.dumps(fake_completed_receipt(command)) + "\n").encode())
        kwargs["stderr"].write(b"")
        return subprocess.CompletedProcess(command, 0)

    def test_complete_failure_is_data_and_fixed_sample_interval_is_reported(self) -> None:
        campaign = json.loads(self.campaign_file.read_text())
        campaign["confirmatory_strata"] = {}
        self.campaign_file.write_text(json.dumps(campaign))
        output = self.work / "output"
        with patch.object(runner.subprocess, "run", side_effect=self._fake_completed) as launch:
            run = runner.run_campaign(self.probe, self.campaign_file, output)
        self.assertEqual(run["status"], "completed")
        self.assertEqual(run["executed_trials"], 2)
        self.assertEqual(run["trials"][0]["receipt"]["status"], "completed")
        self.assertFalse(run["trials"][0]["receipt"]["outcomes"]["correct_complete"])
        self.assertEqual(run["statistics"], {})
        self.assertLessEqual(launch.call_args.kwargs["timeout"], 2)
        command = launch.call_args.args[0]
        native_budget = float(command[command.index("--max-wall-seconds") + 1])
        self.assertLess(native_budget, launch.call_args.kwargs["timeout"])
        self.assertFalse(launch.call_args.kwargs["shell"])
        self.assertTrue((output / "frozen-input.json").exists())
        self.assertTrue((output / "report.md").exists())
        self.assertEqual(run["execution_failures"], 0)
        self.assertEqual(run["valid_communication_failures"], 2)

    def test_per_episode_wall_and_sample_caps_are_enforced(self) -> None:
        campaign = {"schema_version": 1, "name": "episode-caps", "max_wall_seconds": 4,
                    "per_trial_wall_seconds": 3, "max_trials": 1, "max_samples": 1000,
                    "episodes": [{"id": "small", "scenario": "awgn", "mode": "receive",
                                  "waveform": "smoke", "seed": 1, "max_wall_seconds": 1.25,
                                  "max_samples": 500}]}
        self.campaign_file.write_text(json.dumps(campaign))
        with patch.object(runner.subprocess, "run", side_effect=self._fake_completed) as launch:
            runner.run_campaign(self.probe, self.campaign_file, self.work / "episode-caps-output")
        self.assertLessEqual(launch.call_args.kwargs["timeout"], 1.25)
        command = launch.call_args.args[0]
        self.assertEqual(command[command.index("--max-samples") + 1], "500")
        self.assertLess(float(command[command.index("--max-wall-seconds") + 1]), 1.25)

    def test_slot_report_separates_data_decision_and_censors_timeouts(self) -> None:
        campaign = {"schema_version": 1, "name": "slot-summary", "max_wall_seconds": 8,
                    "per_trial_wall_seconds": 2, "max_trials": 4, "max_samples": 1000,
                    "episodes": [{"id": f"slot-{label}", "scenario": "awgn", "mode": "slot",
                                  "waveform": "smoke", "seed": seed}
                                 for label, seed in (("data", 1), ("quiet", 2),
                                                     ("ambiguous", 3), ("censored", 4))]}
        self.campaign_file.write_text(json.dumps(campaign))
        calls = 0

        def valid_then_censored(command, **kwargs):
            nonlocal calls
            calls += 1
            if calls == 4:
                raise subprocess.TimeoutExpired(command, kwargs["timeout"])
            if calls == 1:
                return self._fake_completed(command, **kwargs)
            decision = "quiet" if calls == 2 else "ambiguous"
            receipt = fake_completed_receipt(command)
            receipt["outcomes"]["first_public_observation_correct_data"] = False
            receipt["slots"][0]["decision"] = decision
            kwargs["stdout"].write((json.dumps(receipt) + "\n").encode())
            kwargs["stderr"].write(b"")
            return subprocess.CompletedProcess(command, 0)

        output = self.work / "slot-summary-output"
        with patch.object(runner.subprocess, "run", side_effect=valid_then_censored):
            run = runner.run_campaign(self.probe, self.campaign_file, output)
        report = (output / "report.md").read_text()
        self.assertEqual(run["valid_communication_failures"], 0)
        self.assertIn("one public slot; message completion not evaluated", report)
        self.assertIn("| data | yes |", report)
        self.assertIn("| quiet | no |", report)
        self.assertIn("| ambiguous | no |", report)
        self.assertIn("censored (timed_out)", report)
        self.assertIn("| censored | censored |", report)

    def test_diagnose_report_states_component_scope_without_communication_claim(self) -> None:
        campaign = {"schema_version": 1, "name": "diagnose-report", "max_wall_seconds": 2,
                    "per_trial_wall_seconds": 2, "max_trials": 1, "max_samples": 1000,
                    "episodes": [{"id": "diagnose", "scenario": "vlf-phase-wander",
                                  "mode": "diagnose", "waveform": "reference", "seed": 5}]}
        self.campaign_file.write_text(json.dumps(campaign))
        output = self.work / "diagnose-output"
        with patch.object(runner.subprocess, "run", side_effect=self._fake_completed):
            run = runner.run_campaign(self.probe, self.campaign_file, output)
        report = (output / "report.md").read_text()
        self.assertEqual(run["status"], "completed")
        self.assertEqual(run["valid_communication_failures"], 0)
        self.assertIn("component complete; no slot decision", report)
        self.assertNotIn("physical message complete", report)

    def test_channel_episode_requires_nonempty_received_signal(self) -> None:
        campaign = {"schema_version": 1, "name": "channel", "max_wall_seconds": 2,
                    "per_trial_wall_seconds": 2, "max_trials": 1, "max_samples": 1000,
                    "episodes": [{"id": "empty", "scenario": "eme-disk-episode", "mode": "channel",
                                  "waveform": "smoke", "seed": 1, "duration_seconds": .25}]}
        self.campaign_file.write_text(json.dumps(campaign))

        def fake_empty(command, **kwargs):
            kwargs["stdout"].write((json.dumps(fake_completed_receipt(
                command, channel={"contains_signal_energy": False, "samples": 100})) + "\n").encode())
            kwargs["stderr"].write(b"")
            return subprocess.CompletedProcess(command, 0)

        with patch.object(runner.subprocess, "run", side_effect=fake_empty):
            run = runner.run_campaign(self.probe, self.campaign_file, self.work / "empty-output")
        self.assertEqual(run["status"], "incomplete_checks")
        self.assertEqual(run["trials"][0]["status"], "channel_empty")
        self.assertEqual(run["execution_failures"], 1)

    def test_channel_keeps_native_limits_and_errors_without_energy_report(self) -> None:
        campaign = {"schema_version": 1, "name": "channel-status", "max_wall_seconds": 8,
                    "per_trial_wall_seconds": 2, "max_trials": 4, "max_samples": 1000,
                    "episodes": [{"id": f"case-{status}", "scenario": "eme-disk-episode", "mode": "channel",
                                  "waveform": "smoke", "seed": index}
                                 for index, status in enumerate(("resource_limit", "time_limit",
                                                                 "unavailable", "error"), 1)]}
        self.campaign_file.write_text(json.dumps(campaign))
        codes = {"resource_limit": 3, "time_limit": 4, "unavailable": 5, "error": 2}
        calls = 0

        def fake_status(command, **kwargs):
            nonlocal calls
            status = ("resource_limit", "time_limit", "unavailable", "error")[calls]
            calls += 1
            kwargs["stdout"].write((json.dumps({"schema_version": 1, "status": status}) + "\n").encode())
            kwargs["stderr"].write(b"")
            return subprocess.CompletedProcess(command, codes[status])

        with patch.object(runner.subprocess, "run", side_effect=fake_status):
            run = runner.run_campaign(self.probe, self.campaign_file, self.work / "native-status-output")
        self.assertEqual([trial["status"] for trial in run["trials"]],
                         ["resource_limit", "time_limit", "unavailable", "error"])
        self.assertEqual(run["execution_failures"], 4)

    def test_exact_zero_energy_noise_control_is_preserved(self) -> None:
        campaign = {"schema_version": 1, "name": "zero-noise-control", "max_wall_seconds": 2,
                    "per_trial_wall_seconds": 2, "max_trials": 1, "max_samples": 1000,
                    "episodes": [{"id": "noise", "scenario": "awgn", "mode": "receive",
                                  "waveform": "smoke", "seed": 33, "control": "noise"}]}
        self.campaign_file.write_text(json.dumps(campaign))

        def fake_zero_noise(command, **kwargs):
            receipt = fake_completed_receipt(
                command, control="noise", channel={"contains_signal_energy": False,
                                                       "signal_energy_sample_sum": 0, "samples": 100})
            kwargs["stdout"].write((json.dumps(receipt) + "\n").encode())
            kwargs["stderr"].write(b"")
            return subprocess.CompletedProcess(command, 0)

        with patch.object(runner.subprocess, "run", side_effect=fake_zero_noise):
            run = runner.run_campaign(self.probe, self.campaign_file, self.work / "noise-zero-output")
        self.assertEqual(run["status"], "completed")
        self.assertEqual(run["trials"][0]["status"], "completed")
        self.assertFalse(run["trials"][0]["receipt"]["channel"]["contains_signal_energy"])
        self.assertEqual(run["valid_communication_failures"], 0)

    def test_native_timeout_marks_campaign_incomplete_but_keeps_later_trials(self) -> None:
        campaign = valid_campaign()
        campaign["confirmatory_strata"] = {}
        self.campaign_file.write_text(json.dumps(campaign))
        calls = 0

        def timeout_then_complete(command, **kwargs):
            nonlocal calls
            calls += 1
            if calls == 1:
                raise subprocess.TimeoutExpired(command, kwargs["timeout"])
            return self._fake_completed(command, **kwargs)

        with patch.object(runner.subprocess, "run", side_effect=timeout_then_complete):
            run = runner.run_campaign(self.probe, self.campaign_file, self.work / "timeout-output")
        self.assertEqual(run["status"], "incomplete_checks")
        self.assertEqual(run["stop_reason"], "trial_timeout")
        self.assertEqual(run["executed_trials"], 2)
        self.assertEqual(run["trials"][1]["status"], "completed")
        self.assertEqual(run["valid_communication_failures"], 1)

    def test_probe_hash_change_invalidates_current_episode_and_stops(self) -> None:
        campaign = valid_campaign()
        self.campaign_file.write_text(json.dumps(campaign))
        frozen_hash = "a" * 64
        with patch.object(runner, "_sha256", side_effect=(frozen_hash, frozen_hash, "b" * 64)), \
             patch.object(runner.subprocess, "run", side_effect=self._fake_completed) as launch:
            run = runner.run_campaign(self.probe, self.campaign_file, self.work / "probe-changed-output")
        self.assertEqual(run["status"], "incomplete_checks")
        self.assertEqual(run["stop_reason"], "probe_changed")
        self.assertEqual(run["executed_trials"], 1)
        self.assertEqual(launch.call_count, 1)
        self.assertEqual(run["trials"][0]["status"], "probe_changed")
        self.assertIsNotNone(run["trials"][0].get("receipt"))
        self.assertEqual(run["statistics"]["awgn-fixed"]["usable_n"], 0)
        self.assertIsNone(run["statistics"]["awgn-fixed"]["interval"])
        self.assertEqual(run["execution_failures"], 1)
        self.assertEqual(run["valid_communication_failures"], 0)

    def test_confirmatory_interval_is_withheld_when_fixed_n_is_incomplete(self) -> None:
        campaign = valid_campaign()
        campaign["episodes"] = campaign["episodes"][:1]
        campaign["max_trials"] = 1
        campaign["confirmatory_strata"] = {"awgn-fixed": 2}
        with self.assertRaisesRegex(runner.CampaignError, "exactly 2"):
            campaign_file = self.work / "invalid.json"
            campaign_file.write_text(json.dumps(campaign))
            runner._read_campaign(campaign_file)

    def test_output_directory_must_be_inside_local_evidence(self) -> None:
        outside = Path(tempfile.gettempdir()) / f"appraisal-evidence-outside-{self.work.name}"
        with self.assertRaisesRegex(runner.CampaignError, "output directory"):
            runner.run_campaign(self.probe, self.campaign_file, outside)

    def test_incomplete_fixed_sample_has_no_interval(self) -> None:
        result = runner._statistical_summary(valid_campaign(), [{
            "episode": valid_campaign()["episodes"][0], "status": "completed",
            "receipt": {"status": "completed", "outcomes": {"correct_complete": True}},
        }])
        self.assertFalse(result["awgn-fixed"]["complete_fixed_n"])
        self.assertIsNone(result["awgn-fixed"]["interval"])

    def test_component_channels_have_bounded_nonempty_signal_windows(self) -> None:
        campaign = runner._read_campaign(RUNNER.parent / "campaigns" / "integration-smoke.json")
        channels = [item for item in campaign["episodes"] if item["mode"] == "channel"]
        self.assertEqual(len(channels), 5)
        self.assertTrue(all(item["duration_seconds"] >= 10 for item in channels))
        lunar = next(item for item in channels if item["id"] == "eme-render")
        self.assertEqual(lunar["scenario"], "eme-disk-episode")
        self.assertEqual(lunar["waveform"], "reference")

    def test_duration_contrast_uses_distinct_reference_geometries(self) -> None:
        campaign = runner._read_campaign(RUNNER.parent / "campaigns" / "awgn-trend.json")
        short = next(item for item in campaign["episodes"] if item["id"] == "rx-b16-cn0m04-s6101")
        long = next(item for item in campaign["episodes"] if item["id"] == "rx-b32-cn0m04-s6101")
        self.assertEqual(short["scenario"], long["scenario"])
        self.assertEqual(short["waveform"], long["waveform"])
        self.assertEqual(short["waveform"], "reference")
        self.assertEqual(short["seed"], long["seed"])
        self.assertEqual(short["cn0_db_hz"], long["cn0_db_hz"])
        self.assertLess(short["bit_seconds"], long["bit_seconds"])

if __name__ == "__main__":
    unittest.main()
