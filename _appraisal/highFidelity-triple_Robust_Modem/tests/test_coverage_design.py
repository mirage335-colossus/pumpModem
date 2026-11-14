#!/usr/bin/env python3
"""Checks for physical-parameter selection and honest finite coverage records."""
import ast
import copy
import hashlib
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import coverage_design as design
import run as runner


class CoverageDesignChecks(unittest.TestCase):
    def test_latin_strata_and_pair_bookkeeping(self):
        audit = design.design_audit()
        self.assertEqual(len(audit["axes"]), 9)
        for axis in audit["axes"]:
            self.assertEqual(sorted(axis["ordered_core_bins"]), list(range(8)))
            self.assertEqual(axis["core_bin_counts"], [1] * 8)
        self.assertEqual(len(audit["pair_coverage"]), 36)
        self.assertTrue(all(pair["occupied_core_bin_pairs"] == 8 and pair["possible_bin_pairs"] == 64
                            for pair in audit["pair_coverage"]))
        self.assertIn("not a natural-condition probability", audit["scope"])

    def test_points_match_declared_physical_bounds_and_transforms(self):
        for axis in design.AXES:
            self.assertEqual(axis.value(0), axis.low)
            self.assertEqual(axis.value(1), axis.high)
            if axis.scale == "log":
                self.assertAlmostEqual(axis.value(.5), math.sqrt(axis.low * axis.high), places=10)
            for row in design.latin_points():
                self.assertGreater(row[axis.name], axis.low)
                self.assertLess(row[axis.name], axis.high)
            for invalid in (-.01, 1.01, float("nan"), float("inf")):
                with self.assertRaises(ValueError):
                    axis.value(invalid)
        self.assertEqual({key[3:]: (low, high) for key, low, high in
                          [(axis.name, axis.low, axis.high) for axis in design.AXES[:-1]]}, runner.HF_DESIGN_BOUNDS)

    def test_frozen_reproducibility_and_native_manifest_schema(self):
        design.check_frozen()
        self.assertEqual(design.campaigns(), design.campaigns())
        for name, expected in design.campaigns().items():
            actual = runner._read_campaign(design.HERE / "campaigns" / (name + ".json"))
            self.assertEqual(actual, expected)
            self.assertNotIn("confirmatory_strata", actual)
            self.assertTrue(all(episode["scenario"] == design.SCENARIO for episode in actual["episodes"]))

    def test_holdout_and_anchors_are_distinct_and_fixed(self):
        generated = design.campaigns()
        screen = generated[design.SCREEN_NAME]["episodes"]
        receiver = generated[design.RECEIVER_NAME]["episodes"]
        self.assertEqual([episode["id"] for episode in screen[:8]], [f"domain-lhs-{i:02d}" for i in range(8)])
        self.assertEqual([episode["hf_trajectory_seed"] for episode in screen[:8]], list(range(80101, 80109)))
        self.assertEqual([episode["id"] for episode in screen[-2:]], ["domain-static-anchor", "domain-max-edge"])
        for key in ("hf_primary_amplitude_hz", "hf_secondary_amplitude_hz", "hf_knot_amplitude_hz"):
            self.assertEqual(screen[-2][key], 0)
            self.assertEqual(screen[-1][key], 20000)
        holdout = receiver[3]
        self.assertEqual((holdout["seed"], holdout["noise_seed"], holdout["hf_trajectory_seed"]), (90301, 90201, 90101))
        self.assertEqual((holdout["message"], holdout["capture_digest_slot"]), ("A", 3))
        for point in screen:
            self.assertNotEqual(tuple(holdout[axis.name] for axis in design.AXES),
                                tuple(point[axis.name] for axis in design.AXES))

    def test_component_and_message_endpoints_and_caps_stay_separate(self):
        generated = design.campaigns()
        screen, receive = generated[design.SCREEN_NAME], generated[design.RECEIVER_NAME]
        self.assertEqual((screen["max_trials"], screen["max_wall_seconds"], screen["per_trial_wall_seconds"]), (10, 900, 100))
        self.assertEqual((receive["max_trials"], receive["max_wall_seconds"], receive["per_trial_wall_seconds"]), (6, 1080, 180))
        for episode in screen["episodes"]:
            self.assertEqual((episode["mode"], episode["search_slot"], episode["search_timing"]), ("hf-search", 1, "selected"))
            self.assertNotIn("hf_public_phase_receiver", episode)
        for episode in receive["episodes"]:
            self.assertEqual(episode["mode"], "receive")
            self.assertTrue(episode["hf_public_phase_receiver"])
            self.assertNotIn("search_slot", episode)
            self.assertNotIn("search_timing", episode)
        self.assertEqual([episode["control"] for episode in receive["episodes"][-2:]], ["noise", "wrong-key"])

    def test_audit_preserves_censored_unavailable_and_unexecuted_rows(self):
        campaign = design.campaigns()[design.SCREEN_NAME]
        run = {"campaign_sha256": hashlib.sha256(design._canonical_bytes(campaign)).hexdigest(),
               "trials": [{"episode": episode, "status": status, "receipt_status": status}
                          for episode, status in zip(campaign["episodes"], ("completed", "time_limit", "unavailable"))]}
        report = design.audit_run(campaign, run)
        self.assertEqual(report["status_counts"], {"completed": 1, "time_limit": 1, "unavailable": 1, "not_executed": 7})
        self.assertEqual(len(report["rows"]), 10)
        self.assertTrue(all(row["endpoint_scope"] == "component only" for row in report["rows"]))
        self.assertTrue(all("success" not in row and "correct_complete" not in row for row in report["rows"]))
        for corruption in ("hash", "duplicate", "changed", "status"):
            bad = copy.deepcopy(run)
            if corruption == "hash":
                bad["campaign_sha256"] = "0" * 64
            elif corruption == "duplicate":
                bad["trials"].append(copy.deepcopy(bad["trials"][0]))
            elif corruption == "changed":
                bad["trials"][0]["episode"]["hf_primary_amplitude_hz"] += 1
            else:
                bad["trials"][0]["status"] = "pretend-pass"
            with self.assertRaises(ValueError):
                design.audit_run(campaign, bad)

    def test_write_refuses_overwrite_and_check_detects_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "campaigns").mkdir()
            with patch.object(design, "HERE", root), patch("builtins.print"):
                with patch.object(sys, "argv", ["coverage_design", "--write"]):
                    design.main()
                before = (root / "campaigns/hf-domain-screen.json").read_bytes()
                with patch.object(sys, "argv", ["coverage_design", "--write"]), self.assertRaises(FileExistsError):
                    design.main()
                self.assertEqual((root / "campaigns/hf-domain-screen.json").read_bytes(), before)
                (root / "campaigns/hf-domain-screen.json").write_bytes(before + b" ")
                with self.assertRaises(ValueError):
                    design.check_frozen()

    def test_native_limits_and_runner_failures_are_not_discarded(self):
        campaign = design.campaigns()[design.SCREEN_NAME]
        root = {"campaign_sha256": hashlib.sha256(design._canonical_bytes(campaign)).hexdigest()}
        for status in design.TRIAL_STATUSES - {"completed"}:
            run = {**root, "trials": [{"episode": campaign["episodes"][0], "status": status,
                                      "receipt_status": status if status in design.NATIVE_STATUSES else None}]}
            self.assertEqual(design.audit_run(campaign, run)["status_counts"], {status: 1, "not_executed": 9})
        for native in (None, "unavailable", "time_limit"):
            with self.assertRaises(ValueError):
                design.audit_run(campaign, {**root, "trials": [{"episode": campaign["episodes"][0],
                    "status": "completed", "receipt_status": native}]})

    def test_generator_has_no_signal_or_receiver_dependencies(self):
        tree = ast.parse(Path(design.__file__).read_text())
        imports = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                imports.update(alias.name.split(".")[0] for alias in node.names)
            elif isinstance(node, ast.ImportFrom):
                imports.add(node.module.split(".")[0])
        self.assertLessEqual(imports, {"__future__", "argparse", "collections", "dataclasses", "hashlib",
                                      "itertools", "json", "math", "pathlib", "typing"})


if __name__ == "__main__":
    unittest.main()
