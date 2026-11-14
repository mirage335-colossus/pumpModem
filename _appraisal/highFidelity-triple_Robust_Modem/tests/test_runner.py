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
    receipt = {
        "schema_version": 1, "status": "completed",
        "requested_scenario": argument("--scenario", "awgn"),
        "mode": mode, "waveform": argument("--waveform", "smoke"),
        "seed": int(argument("--seed", "1")),
        "control": control or argument("--control", "wanted"),
        "source_identity": {"appraisal_source_sha256": "a" * 64, "native_source_sha256": "b" * 64},
        "plan": {"cn0_db_hz": cn0, "requested_bit_seconds": requested_bit_seconds,
                 "helper_enabled": "--helper" in command, "sample_rate_hz": 1920,
                 "horizon_samples": 100},
        "backend": {"requested": backend, "device": backend if backend in {"cuda", "vulkan"} else "cpu",
                    "executed_gpu_work": backend in {"cuda", "vulkan"}},
    }
    if mode == "receive":
        receipt["outcomes"] = outcomes or {"correct_complete": False, "physical_completions": 0}
    if mode == "channel" or channel is not None:
        receipt["channel"] = channel or {"contains_signal_energy": True, "samples": 100}
    if mode == "oracle":
        receipt["oracle"] = {"probability_error": .5}
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

    def test_channel_episode_requires_nonempty_received_signal(self) -> None:
        campaign = {"schema_version": 1, "name": "channel", "max_wall_seconds": 2,
                    "per_trial_wall_seconds": 2, "max_trials": 1, "max_samples": 1000,
                    "episodes": [{"id": "empty", "scenario": "eme-disk", "mode": "channel",
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
                    "episodes": [{"id": f"case-{status}", "scenario": "eme-disk", "mode": "channel",
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
                                                       "signal_energy_sample_sum": 0, "samples": 100},
                outcomes={"correct_complete": False, "physical_completions": 0})
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

    def test_duration_contrast_uses_distinct_reference_geometries(self) -> None:
        campaign = runner._read_campaign(RUNNER.parent / "campaigns" / "weak-signal-pilot.json")
        short = next(item for item in campaign["episodes"] if item["id"] == "awgn-duration-short")
        long = next(item for item in campaign["episodes"] if item["id"] == "awgn-duration-long")
        self.assertEqual(short["scenario"], long["scenario"])
        self.assertEqual(short["waveform"], long["waveform"])
        self.assertEqual(short["waveform"], "reference")
        self.assertEqual(short["seed"], long["seed"])
        self.assertLess(short["bit_seconds"], long["bit_seconds"])

if __name__ == "__main__":
    unittest.main()
