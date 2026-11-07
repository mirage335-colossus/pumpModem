"""Small resource-contract controls; native qualification is an explicit command."""
from __future__ import annotations

import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import triad_perf_resource as resource


def manifest(seconds=32, mode="paired", *, memory_mib=7, seed=1):
    rows = seconds * 512 + 512
    return {"format": "triad-group-block-stats-v1", "stats_noisy": True,
            "sample_count": rows, "block_samples": 2048, "block_count": math.ceil(rows / 2048),
            "channel_samples": rows, "channel_tail_samples": 512, "source_samples": rows - 512,
            "group_count": 3, "dimension": 4, "candidate_count": 6, "sample_rate": 512,
            "candidates": [0, 1], "groups": [0, 1, 2],
            "grouped_export_execution": {"channel_pass_mode": mode, "basis_synthesis_passes": 1 if mode == "paired" else 2,
                 "worker_threads_used": 1, "bounded_workspace_bytes": int((6.5 if mode == "paired" else 4.5) * resource.MIB)},
            "file_sizes": {name: 8 for name in resource.FILES}}


def fake_record(binary, config, seconds, seed, memory_mib, mode, directory, **kwargs):
    meta = manifest(seconds, mode, memory_mib=memory_mib, seed=seed)
    regime, work = resource.work_record(meta, config, memory_mib, mode)
    elapsed = .025 + work["sample_count"] * (.000002 if mode == "paired" else .000003)
    argv = resource.argv_for(binary, config, seconds, seed, memory_mib, str(Path(directory) / "synthetic-output"))
    return resource.sealed({"requested_seconds": seconds, "seed": seed,
                            "argv": argv, "argv_sha256": resource.digest(argv),
                            "full_test_wall_seconds": elapsed, "regime": regime, "work": work,
                            "file_sizes": meta["file_sizes"],
                            "file_sha256": {name: resource.digest([name, seed, seconds]) for name in resource.FILES},
                            "native_execution": meta["grouped_export_execution"],
                            "timing_boundary": "synthetic complete target", "harness_wall_seconds": elapsed})


def fake_identity(binary, root, threads=1):
    return {"binary_sha256": resource.digest(str(binary)), "source": str(root), "threads": threads}


def fake_geometry(binary, config, **kwargs):
    m = manifest(config["target_seconds"])
    return {"performance_geometry": {"capture_samples": m["sample_count"], "source_samples": m["source_samples"],
            "block_count": m["block_count"], "block_samples": m["block_samples"],
            "sample_rate_hz": m["sample_rate"], "maximum_chunk_samples": config["chunk_samples"]}}


class ResourceTests(unittest.TestCase):
    def setUp(self):
        self.config = resource.load_config()

    def test_real_branch_counters_and_complete_work_required(self):
        paired, work = resource.work_record(manifest(), self.config, 7, "paired")
        seq, seq_work = resource.work_record(manifest(mode="sequential"), self.config, 6, "sequential")
        self.assertEqual(seq_work["basis_values"], 2 * work["basis_values"])
        self.assertEqual(seq_work["gram_terms"], work["gram_terms"])
        self.assertEqual(seq_work["channel_rows"], work["channel_rows"])
        self.assertEqual(resource.bracket({"regime": paired, "native_execution": manifest()["grouped_export_execution"]}),
                         {"paired": 7, "sequential": 6})
        for field, value in (("basis_synthesis_passes", 2), ("worker_threads_used", 2)):
            bad = manifest(); bad["grouped_export_execution"][field] = value
            with self.assertRaises(resource.ResourcePerformanceError):
                resource.work_record(bad, self.config, 7, "paired")
        bad = manifest(); bad["channel_samples"] -= 1
        with self.assertRaises(resource.ResourcePerformanceError):
            resource.work_record(bad, self.config, 7, "paired")
        with self.assertRaises(resource.ResourcePerformanceError):
            resource.work_record(manifest(), self.config, 6, "paired")

    def test_independent_affine_regimes_and_scope(self):
        a, b = [fake_record("b", self.config, n, 1, 7, "paired", ".") for n in (32, 96)]
        target = 64 * 512 + 512
        result = resource.forecast([a, b], target, self.config)
        self.assertAlmostEqual(result["full_test_wall_seconds"], .025 + target * .000002)
        bad = fake_record("b", self.config, 96, 1, 6, "sequential", ".")
        with self.assertRaisesRegex(resource.ResourcePerformanceError, "crossed"):
            resource.forecast([a, bad], target, self.config)
        with self.assertRaises(resource.ResourcePerformanceError):
            resource.forecast([a, b], target * 2, self.config)

    def test_byte_parity_is_not_nominal_metadata_parity(self):
        a = fake_record("b", self.config, 64, 7, 7, "paired", ".")
        b = fake_record("b", self.config, 64, 7, 6, "sequential", ".")
        self.assertTrue(resource.compare_outputs(a, b, across_regimes=True)["byte_identical"])
        b["file_sha256"][resource.FILES[0]] = "different"
        with self.assertRaisesRegex(resource.ResourcePerformanceError, "actual sufficient-statistic bytes"):
            resource.compare_outputs(a, b, across_regimes=True)

    def test_arithmetic_forecast_and_holdout_uncertainty_both_count(self):
        good = resource.qualification([1.] * 33, [1.] * 9, self.config)
        self.assertEqual(good["status"], "validated")
        self.assertEqual(good["actual_over_frozen_forecast_mean_interval"], [1., 1.])
        self.assertEqual(resource.qualification([1.] * 33, [1.3] * 9, self.config)["status"], "invalid")
        self.assertEqual(resource.qualification([.05, 2.] * 16 + [1.], [1.] * 9, self.config)["status"], "inconclusive")
        with self.assertRaisesRegex(resource.ResourcePerformanceError, "fixed look"):
            resource.qualification([1.] * 5, [1.] * 4, self.config)

    def _anchor(self, directory):
        full_calls = []
        def run(*args, **kwargs):
            if args[2] == 64:
                self.assertTrue((Path(directory) / "frozen.json").is_file())
                full_calls.append(args[3])
            return fake_record(*args, **kwargs)
        geometry = {"performance_geometry": {"capture_samples": 64 * 512 + 512}}
        with (patch.object(resource, "run_export", side_effect=run),
              patch.object(resource, "binary_identity", side_effect=fake_identity),
              patch.object(resource, "target_geometry", return_value=geometry)):
            anchor = resource.qualify("candidate", ROOT, directory, config=self.config)
        self.assertEqual(full_calls, [self.config["seed"] + 100000 + i for i in range(9) for _ in range(2)])
        return anchor

    def test_target_counter_comes_from_geometry_not_requested_seconds(self):
        # Actual observed private-tile rounding from the first native attempt.
        a, b = [fake_record("b", self.config, n, 1, 7, "paired", ".") for n in (32, 96)]
        for row, rows in ((a, 18000), (b, 53635)):
            row["work"]["sample_count"] = rows
            row["full_test_wall_seconds"] = .025 + .000002 * rows
        geometry = {"performance_geometry": {"capture_samples": 35818}}
        target = resource._target_rows(geometry)
        self.assertNotEqual(target, (b["work"]["sample_count"] + a["work"]["sample_count"]) / 2)
        predicted = resource.forecast([a, b], target, self.config)
        self.assertAlmostEqual(predicted["full_test_wall_seconds"], .025 + .000002 * 35818)

    def test_forecast_is_frozen_before_full_validation_and_proof_replays(self):
        with tempfile.TemporaryDirectory() as tmp:
            anchor = self._anchor(tmp)
            self.assertEqual(anchor["status"], "validated")
            self.assertTrue(resource.historical_proof(json.loads(json.dumps(anchor))))
            bad = copy.deepcopy(anchor)
            bad["full_holdouts"]["paired"][0]["full_test_wall_seconds"] *= 1.4
            bad["full_holdouts"]["paired"][0] = resource.sealed(bad["full_holdouts"]["paired"][0])
            bad = resource.sealed(bad)
            self.assertFalse(resource.historical_proof(bad))
            bad = copy.deepcopy(anchor)
            bad["frozen"]["identity"]["binary_sha256"] = "changed"
            self.assertFalse(resource.historical_proof(bad))

    def test_direct_routine_profiles_complete_target_and_wrapper_receipt(self):
        with tempfile.TemporaryDirectory() as tmp:
            anchor = self._anchor(tmp)
            anchor_path = Path(tmp) / "anchor.json"
            with patch.object(resource, "run_export", side_effect=fake_record), patch.object(resource, "binary_identity", side_effect=fake_identity):
                for profile in ("normal", "ci"):
                    result = resource.routine("candidate", "reference", ROOT, ROOT, anchor, tmp,
                                              profile=profile, config=self.config)
                    result = json.loads(json.dumps(result))
                    checked = resource.validate_routine_report(result, {"anchor": anchor_path,
                        "binary": "candidate", "reference_binary": "reference", "candidate_root": ROOT,
                        "reference_root": ROOT, "device": "cuda", "native_threads": 1,
                        "regression_profile": profile})
                    self.assertTrue(checked["valid"], checked)
                    self.assertEqual(checked["status"], "passed")
                    self.assertEqual(checked["execution_backend"], "cpu")
                    self.assertEqual(result["policy"]["pass_alpha"], .15 if profile == "normal" else .30)
                    self.assertEqual(result["policy"]["failure_alpha"], .05)
                    self.assertTrue(all(len(c["raw_pairs"]) == 5 for c in result["cases"]))
                    self.assertTrue(all(r["requested_seconds"] == 64 for c in result["cases"] for p in c["raw_pairs"] for r in p.values()))
                    bad = copy.deepcopy(result); bad["identities"]["candidate"]["binary_sha256"] = "bad"
                    bad = resource.sealed(bad)
                    self.assertFalse(resource.validate_report(bad, anchor_path=anchor_path,
                        binary="candidate", reference_binary="reference", source_root=ROOT, reference_root=ROOT,
                        profile=profile)["valid"])

    def test_full_only_and_outside_receiver_slowdown_are_measured(self):
        with tempfile.TemporaryDirectory() as tmp:
            anchor = self._anchor(tmp)
            def changed(*args, **kwargs):
                row = fake_record(*args, **kwargs)
                if args[0] == "candidate" and args[2] == 64:
                    row["full_test_wall_seconds"] *= 1.4
                return resource.sealed(row)
            with patch.object(resource, "run_export", side_effect=changed), patch.object(resource, "binary_identity", side_effect=fake_identity):
                normal = resource.routine("candidate", "reference", ROOT, ROOT, anchor, tmp,
                                          profile="normal", config=self.config)
                ci = resource.routine("candidate", "reference", ROOT, ROOT, anchor, tmp,
                                      profile="ci", config=self.config)
            self.assertEqual(normal["status"], "regression")
            self.assertEqual(ci["status"], "pass")
            self.assertFalse(normal["complete_scope"])

    def test_live_source_change_stops_before_anchor_publication(self):
        with (tempfile.TemporaryDirectory() as tmp,
              patch.object(resource, "run_export", side_effect=fake_record),
              patch.object(resource, "binary_identity", side_effect=fake_identity),
              patch.object(resource, "target_geometry", return_value={"performance_geometry": {"capture_samples": 33280}}),
              patch.object(resource, "implementation_identity", side_effect=[{"version": 1}, {"version": 2}])):
            with self.assertRaisesRegex(resource.ResourcePerformanceError, "changed during"):
                resource.qualify("candidate", ROOT, tmp, config=self.config)
            self.assertFalse((Path(tmp) / "anchor.json").exists())
            self.assertTrue((Path(tmp) / "training.json").exists())

    def test_wrapper_recomputes_cross_regime_outputs_not_only_each_pair(self):
        with tempfile.TemporaryDirectory() as tmp:
            anchor = self._anchor(tmp)
            with patch.object(resource, "run_export", side_effect=fake_record), patch.object(resource, "binary_identity", side_effect=fake_identity):
                report = resource.routine("candidate", "reference", ROOT, ROOT, anchor, tmp, config=self.config)
                # Equal reference/candidate modifications would pass within-pair
                # comparison; they must still fail physical parity across modes.
                for name in ("reference", "candidate"):
                    row = report["cases"][1]["raw_pairs"][0][name]
                    row["file_sha256"][resource.FILES[0]] = "changed-sequential-only"
                    report["cases"][1]["raw_pairs"][0][name] = resource.sealed(row)
                report = resource.sealed(report)
                check = resource.validate_report(report, anchor_path=Path(tmp) / "anchor.json", binary="candidate",
                                                 reference_binary="reference", source_root=ROOT, reference_root=ROOT)
                self.assertFalse(check["valid"])
                self.assertTrue(any("actual sufficient-statistic bytes" in text for text in check["issues"]))

    def test_process_boundary_includes_native_work_and_excludes_hashes(self):
        def fake_run(argv, **kwargs):
            target = Path(argv[argv.index("--output") + 1]); target.mkdir()
            for name in resource.FILES:
                (target / name).write_bytes(b"12345678")
            (target / "manifest.json").write_text(json.dumps(manifest()))
            time.sleep(.003)
            return subprocess.CompletedProcess(argv, 0, "", "")
        with tempfile.TemporaryDirectory() as tmp, patch.object(resource.subprocess, "run", side_effect=fake_run):
            result = resource.run_export("binary", self.config, 32, 3, 7, "paired", tmp)
            self.assertGreaterEqual(result["full_test_wall_seconds"], .003)
            self.assertGreaterEqual(result["harness_wall_seconds"], result["full_test_wall_seconds"])
            self.assertEqual(list(Path(tmp).iterdir()), [])
            self.assertTrue(resource.verify_seal(result))

    def test_physical_maps_are_applied_and_manifest_input_binding_is_retained(self):
        masks = ["--performance-tx-masks", "1", "--performance-rx-masks", "0", "--performance-signal-bits", "0"]
        metadata = {"schema": "triad-performance-physical-bits-v1", "tx_mask": 1, "rx_mask": 0,
                    "signal_physical_bit": 0, "signal_plaintext_bit": 1}
        def fake_run(argv, **kwargs):
            self.assertEqual(argv[-6:], masks)
            target = Path(argv[argv.index("--output") + 1]); target.mkdir()
            for name in resource.FILES:
                (target / name).write_bytes(b"12345678")
            m = manifest()
            m.update(legacy_seed=3, key_seed=3, channel_seed=3, source_format={"ramp_seconds": 1/1800},
                     random_seed_roots={"noise": 3}, sample_rate_numerator=512, sample_rate_denominator=1,
                     performance_physical_input=metadata)
            (target / "manifest.json").write_text(json.dumps(m))
            return subprocess.CompletedProcess(argv, 0, "", "")
        with tempfile.TemporaryDirectory() as tmp, patch.object(resource.subprocess, "run", side_effect=fake_run):
            result = resource.run_export("binary", self.config, 32, 3, 7, "paired", tmp,
                                         physical_mask_arguments=masks)
            self.assertEqual(result["physical_input_binding"]["performance_physical_input"], metadata)
            self.assertEqual(result["physical_input_binding"]["capture_samples"], manifest()["sample_count"])
            self.assertEqual(result["physical_input_binding"]["source_format"], {"ramp_seconds": 1/1800})
            metadata["signal_plaintext_bit"] = 0
            with self.assertRaisesRegex(resource.ResourcePerformanceError, "frozen physical maps"):
                resource.run_export("binary", self.config, 32, 3, 7, "paired", tmp,
                                    physical_mask_arguments=masks)
        with patch.object(resource.subprocess, "run") as run:
            for invalid in (masks[:-1], masks[:1]+["01"]+masks[2:], ["--seed", "1", *masks[2:]]):
                with self.assertRaisesRegex(resource.ResourcePerformanceError, "physical input arguments"):
                    resource.run_export("binary", self.config, 32, 3, 7, "paired", ".",
                                        physical_mask_arguments=invalid)
            run.assert_not_called()

    def test_generated_export_rejects_symlinks_nonregular_and_unfinished_files(self):
        for unsafe in ("manifest-link", "data-link", "directory", "partial", "unexpected", "missing"):
            with self.subTest(unsafe=unsafe), tempfile.TemporaryDirectory() as tmp:
                external=Path(tmp)/"external"; external.write_bytes(b"12345678")
                def fake_run(argv, **kwargs):
                    target=Path(argv[argv.index("--output")+1]); target.mkdir()
                    for name in resource.FILES:
                        (target/name).write_bytes(b"12345678")
                    (target/"manifest.json").write_text(json.dumps(manifest()))
                    if unsafe=="manifest-link":
                        external.write_text(json.dumps(manifest()))
                        (target/"manifest.json").unlink();(target/"manifest.json").symlink_to(external)
                    elif unsafe=="data-link":
                        (target/resource.FILES[0]).unlink();(target/resource.FILES[0]).symlink_to(external)
                    elif unsafe=="directory":
                        (target/resource.FILES[0]).unlink();(target/resource.FILES[0]).mkdir()
                    elif unsafe=="missing":(target/resource.FILES[0]).unlink()
                    else:(target/("unfinished.partial" if unsafe=="partial" else "other.txt")).write_text("unfinalized")
                    return subprocess.CompletedProcess(argv,0,"","")
                with patch.object(resource.subprocess,"run",side_effect=fake_run):
                    with self.assertRaisesRegex(resource.ResourcePerformanceError,"unsafe files"):
                        resource.run_export("binary",self.config,32,3,7,"paired",Path(tmp)/"exports")

    def test_direct_needs_no_forecast_anchor_and_freezes_shared_budgets(self):
        with tempfile.TemporaryDirectory() as tmp:
            calls = []
            def run(*args, **kwargs):
                calls.append((args[0], args[2], args[4], args[5]))
                if args[2] == 64:
                    self.assertTrue((Path(tmp) / "frozen-direct.json").is_file())
                return fake_record(*args, **kwargs)
            with (patch.object(resource, "run_export", side_effect=run),
                  patch.object(resource, "target_geometry", side_effect=fake_geometry),
                  patch.object(resource, "binary_identity", side_effect=fake_identity),
                  patch.object(resource, "historical_proof", side_effect=AssertionError("direct must not demand forecast qualification"))):
                result = resource.direct("candidate", "reference", ROOT, ROOT, tmp, config=self.config)
                check = resource.validate_routine_report(json.loads(json.dumps(result)), {"mode": "direct",
                    "binary": "candidate", "reference_binary": "reference", "candidate_root": ROOT,
                    "reference_root": ROOT, "native_threads": 1, "regression_profile": "normal"})
            self.assertTrue(check["valid"], check)
            self.assertEqual(check["status"], "passed")
            self.assertEqual(result["historical_model"]["status"], "not_provided")
            self.assertEqual(calls[0], ("candidate", 32, 256, "paired"))
            self.assertEqual(sum(c[1] == 32 for c in calls), 1)
            self.assertEqual({(c[2], c[3]) for c in calls[1:]}, {(7, "paired"), (6, "sequential")})
            self.assertEqual({c[0] for c in calls[1:]}, {"candidate", "reference"})
            self.assertEqual(result["measured_fraction"], 1.)
            self.assertEqual(result["computed_saved_seconds_against_direct"], 0.)

    def test_direct_keeps_inconclusive_history_unused_and_replays_counters(self):
        with tempfile.TemporaryDirectory() as tmp:
            history = resource.sealed({"schema": resource.ANCHOR_SCHEMA, "status": "inconclusive"})
            path = Path(tmp) / "unused-history.json"; resource._save(path, history)
            with (patch.object(resource, "run_export", side_effect=fake_record),
                  patch.object(resource, "target_geometry", side_effect=fake_geometry),
                  patch.object(resource, "binary_identity", side_effect=fake_identity)):
                result = resource.direct("candidate", "reference", ROOT, ROOT, tmp, anchor=history, config=self.config)
                self.assertEqual(result["historical_model"]["status"], "inconclusive_not_applied")
                check = resource.validate_report(result, mode="direct", anchor_path=path, binary="candidate",
                                                 reference_binary="reference", source_root=ROOT, reference_root=ROOT)
                self.assertTrue(check["valid"], check)
                for name in ("reference", "candidate"):
                    row = result["cases"][0]["raw_pairs"][0][name]
                    row["work"]["basis_values"] -= 1
                    result["cases"][0]["raw_pairs"][0][name] = resource.sealed(row)
                result = resource.sealed(result)
                check = resource.validate_report(result, mode="direct", anchor_path=path, binary="candidate",
                                                 reference_binary="reference", source_root=ROOT, reference_root=ROOT)
                self.assertFalse(check["valid"])
                self.assertTrue(any("actual work counters" in e for e in check["issues"]))

    def test_direct_threshold_change_is_invalid_not_rebudgeted(self):
        with tempfile.TemporaryDirectory() as tmp:
            calls = []
            def changed(*args, **kwargs):
                calls.append((args[0], args[2], args[4], args[5]))
                row = fake_record(*args, **kwargs)
                if args[0] == "reference" and args[2] == 64:
                    row["native_execution"]["channel_pass_mode"] = "sequential"
                    row["native_execution"]["basis_synthesis_passes"] = 2
                return resource.sealed(row)
            with (patch.object(resource, "run_export", side_effect=changed),
                  patch.object(resource, "target_geometry", side_effect=fake_geometry),
                  patch.object(resource, "binary_identity", side_effect=fake_identity)):
                with self.assertRaisesRegex(resource.ResourceWorkRegimeError, "actual work counters"):
                    resource.direct("candidate", "reference", ROOT, ROOT, tmp, config=self.config)
            self.assertEqual(calls, [("candidate", 32, 256, "paired"), ("reference", 64, 7, "paired")])
            raw = json.loads((Path(tmp) / "direct-observations.json").read_text())
            self.assertEqual(raw["observations"][0]["record"]["native_execution"]["channel_pass_mode"], "sequential")

    def test_failed_native_branch_preserves_actual_manifest(self):
        def fake_run(argv, **kwargs):
            output = Path(argv[argv.index("--output") + 1]); output.mkdir()
            (output / "manifest.json").write_text(json.dumps(manifest(mode="sequential")))
            return subprocess.CompletedProcess(argv, 0, "", "")
        with tempfile.TemporaryDirectory() as tmp, patch.object(resource.subprocess, "run", side_effect=fake_run):
            failed = Path(tmp) / "failed.json"
            with self.assertRaises(resource.ResourceWorkRegimeError):
                resource.run_export("binary", self.config, 32, 3, 7, "paired", tmp, failure_path=failed)
            raw = json.loads(failed.read_text())
            self.assertEqual(raw["status"], "work_regime_invalid")
            self.assertEqual(raw["manifest"]["grouped_export_execution"]["channel_pass_mode"], "sequential")
            self.assertGreater(raw["full_test_wall_seconds"], 0)

    def test_changed_minimum_memory_guard_is_a_work_regime_failure(self):
        message = "triad_long: export-group-stats bounded scratch/source estimate exceeds --memory-mib"
        with (tempfile.TemporaryDirectory() as tmp,
              patch.object(resource.subprocess, "run", return_value=subprocess.CompletedProcess([], 1, "", message))):
            failed = Path(tmp) / "failed.json"
            with self.assertRaises(resource.ResourceWorkRegimeError):
                resource.run_export("binary", self.config, 64, 3, 4, "sequential", tmp, failure_path=failed)
            raw = json.loads(failed.read_text())
            self.assertEqual(raw["status"], "work_regime_invalid")
            self.assertEqual(raw["stderr"], message)
            self.assertIsNone(raw["manifest"])


if __name__ == "__main__":
    unittest.main()
