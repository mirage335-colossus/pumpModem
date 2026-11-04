"""Cheap guards of full-target adapters; no benchmark campaign in unit tests."""
from __future__ import annotations

import copy
import json
import math
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import triad_perf_guard_extras as extras
import triad_perf_planner as planner
import triad_perf_resource as resource


def identity(kind):
    if kind == "native-planner":
        return {"sources": {side: {"driver_sha256": "driver", "production_sha256": side}
                            for side in extras.SIDES}}
    if kind == "gaussian-bounds":
        return {"workspace": {"files": {"manifest.json": "manifest"}, "sample_count": 100,
                    "sample_rate": 10, "blocks": 5, "candidates": 2, "pairs": 1,
                    "selected_candidates": [{"bit": 0}, {"bit": 1}]},
                "sources": {side: {name: side + name for name in planner.BOUNDS_DEPENDENCIES}
                            for side in extras.SIDES}, "libraries": {"torch": "fixture"}}
    return {"implementation": "fixture", "sources": {side: side for side in extras.SIDES}}


def native_observation(binary, case, source, deadline, receipt_path=None):
    row = {"schema": "triad-planner-perf-native-v1", "case": case["id"], "complete": True,
           "driver_sha256": source["driver_sha256"], "production_sha256": source["production_sha256"],
           "repetitions": case["repetitions"], "scenario_evaluations": case["repetitions"] * case["scenarios_per_repetition"],
           "arithmetic": "fp64_and_native_long_double_bounds", "checksum": 3.,
           "internal_iterations_instrumented": False, "operation_seconds": .1,
           "outputs": [{"name": name, "status": "complete", "value": 1., "coordinate": 7}
                       for name in case["outputs"]]}
    return {"argv": [str(binary)], "wall_seconds": .4, "native": row}


def bound_observation(root, workspace, case, source, side, deadline, receipt_path=None):
    w = source["workspace"]
    row = {"schema": "triad-gaussian-fixed-bank-bounds-v1", "wall_seconds": .1,
           "source_manifest_sha256": w["files"]["manifest.json"], "observation_blocks": w["blocks"],
           "candidate_count": w["candidates"], "pair_designs": w["pairs"],
           "selected_candidates": w["selected_candidates"], "chernoff_s_grid": case["scales"],
           "null_calibration": case["null_calibration"], "hidden_channel_means_or_signal_energies_read": False,
           "torch_version": "fixture", "stationary_coefficient_powers": [.5, .5],
           "full_exported_capture_seconds": 10.}
    for key, name in (("script_sha256", "triad_gaussian_bounds.py"),
                       ("gaussian_state_sha256", "triad_gaussian_state.py"),
                       ("workspace_module_sha256", "triad_state_workspace.py")):
        row[key] = source["sources"][side][name]
    return {"argv": [str(root)], "wall_seconds": .5, "bounds": row}


def resource_manifest(seconds, mode):
    rows = seconds * 512 + 512
    return {"format": "triad-group-block-stats-v1", "stats_noisy": True,
            "sample_count": rows, "block_samples": 2048, "block_count": math.ceil(rows / 2048),
            "channel_samples": rows, "channel_tail_samples": 512, "source_samples": rows - 512,
            "group_count": 3, "dimension": 4, "candidate_count": 6, "sample_rate": 512,
            "candidates": [0, 1], "groups": [0, 1, 2],
            "grouped_export_execution": {"channel_pass_mode": mode,
                "basis_synthesis_passes": 1 if mode == "paired" else 2, "worker_threads_used": 1,
                "bounded_workspace_bytes": int((4.5 if mode == "paired" else 3.5) * resource.MIB)},
            "file_sizes": {name: 8 for name in resource.FILES}}


def export(binary, config, seconds, seed, memory_mib, mode, directory, **kwargs):
    m = resource_manifest(seconds, mode)
    regime, work = resource.work_record(m, config, memory_mib, mode)
    argv = resource.argv_for(binary, config, seconds, seed, memory_mib, Path(directory) / "synthetic")
    return resource.sealed({"requested_seconds": seconds, "seed": seed, "argv": argv,
        "argv_sha256": resource.digest(argv), "full_test_wall_seconds": .7, "regime": regime,
        "work": work, "file_sizes": m["file_sizes"],
        "file_sha256": {name: resource.digest([name, seed, seconds]) for name in resource.FILES},
        "native_execution": m["grouped_export_execution"], "timing_boundary": "synthetic complete target"})


def geometry(binary, config, **kwargs):
    m = resource_manifest(config["target_seconds"], "paired")
    return {"performance_geometry": {"capture_samples": m["sample_count"], "source_samples": m["source_samples"],
        "block_count": m["block_count"], "block_samples": m["block_samples"],
        "sample_rate_hz": m["sample_rate"], "maximum_chunk_samples": config["chunk_samples"]}}


class ExtrasTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.tmp = Path(temporary.name)
        self.context = {"candidate_root": ROOT, "reference_root": ROOT,
            "binary": self.tmp / "candidate", "reference_binary": self.tmp / "reference",
            "planner_binary": self.tmp / "planner-candidate", "planner_reference_binary": self.tmp / "planner-reference",
            "planner_bounds_workspace": self.tmp, "output": self.tmp / "output",
            "deadline": time.monotonic() + 30, "native_threads": 1}
        for name in ("binary", "reference_binary", "planner_binary", "planner_reference_binary"):
            self.context[name].write_text("fixture")
        p = patch.object(extras.ExtraWorkloads, "_read_identity", side_effect=identity)
        p.start(); self.addCleanup(p.stop)

    def test_inventory_retains_ten_targets_without_running_or_generating(self):
        with patch.object(resource, "target_geometry") as configure, patch.object(resource, "run_export") as run:
            adapter = extras.ExtraWorkloads(self.context)
            descriptors = adapter.descriptors()
        self.assertEqual(len(descriptors), 10)
        self.assertEqual(len({d["id"] for d in descriptors}), 10)
        self.assertTrue(all(d["available"] and d["method"] == "direct_full" and not d["model_used"] for d in descriptors))
        configure.assert_not_called(); run.assert_not_called()
        self.assertFalse(adapter.output.exists())

    def test_omitted_optional_inputs_are_explicit_not_silent_skips(self):
        for key in ("planner_binary", "planner_reference_binary", "planner_bounds_workspace"):
            self.context.pop(key)
        adapter = extras.ExtraWorkloads(self.context)
        unavailable = [d for d in adapter.descriptors() if d["availability"] == "unavailable"]
        self.assertEqual(len(unavailable), 8)
        self.assertTrue(all(d["reason"] for d in unavailable))
        with self.assertRaises(extras.ExtraUnavailable):
            adapter.measure_pair("planner:early-calendar", "missing", 1)
        self.context["planner_binary"] = self.tmp / "bad-provided-path"
        with self.assertRaisesRegex(ValueError, "does not exist"):
            extras.ExtraWorkloads(self.context)

    def test_all_eight_planner_bundles_run_full_work_only_process_timer(self):
        adapter = extras.ExtraWorkloads(self.context)
        with patch.object(planner, "invoke", side_effect=native_observation) as invoke, \
             patch.object(planner, "invoke_bound", side_effect=bound_observation) as bounds:
            for d in adapter.descriptors():
                if d["kind"] == "native-resource":
                    continue
                result = adapter.measure_pair(d["id"], "screen-0", 100)
                expected = .4 if d["kind"] == "native-planner" else .5
                self.assertEqual(result["reference_seconds"], expected)
                self.assertEqual(result["candidate_seconds"], expected)
                self.assertEqual(result["details"]["operation_seconds_diagnostic"]["reference"], .1)
                self.assertEqual(result["details"]["performance_hypothesis"], "complete_process_only")
        self.assertEqual(invoke.call_count, 12); self.assertEqual(bounds.call_count, 4)
        for call in invoke.call_args_list:
            case = call.args[1]
            self.assertIn(case, planner.manifest()["cases"])
        adapter.close()

    def test_wrong_native_counts_numeric_outputs_and_bounds_paths_fail(self):
        for failure in ("count", "numeric", "bounds"):
            context = dict(self.context, output=self.tmp / failure)
            adapter = extras.ExtraWorkloads(context)
            def broken(binary, case, source, deadline, receipt_path=None):
                row = native_observation(binary, case, source, deadline, receipt_path)
                if "candidate" in str(binary):
                    if failure == "count":
                        row["native"]["scenario_evaluations"] -= 1
                    else:
                        row["native"]["outputs"][0]["value"] += 1
                return row
            def broken_bound(*args):
                row = bound_observation(*args)
                row["bounds"]["pair_designs"] += 1
                return row
            with patch.object(planner, "invoke", side_effect=broken), \
                 patch.object(planner, "invoke_bound", side_effect=broken_bound):
                case = "planner:gaussian-design-markov" if failure == "bounds" else "planner:early-calendar"
                with self.assertRaises(ValueError):
                    adapter.measure_pair(case, "bad", 1)
            self.assertEqual(len(list(adapter.output.glob("*/error.json"))), 1)

    def test_resource_fixed_budgets_lazy_geometry_full_work_and_cross_parity(self):
        adapter = extras.ExtraWorkloads(self.context)
        def candidate_only(binary, *args, **kwargs):
            self.assertEqual(binary, self.context["binary"])
            return geometry(binary, *args, **kwargs)
        with patch.object(resource, "target_geometry", side_effect=candidate_only) as configure, \
             patch.object(resource, "run_export", side_effect=export) as run:
            for mode in resource.REGIMES:
                adapter.measure_pair("resource:" + mode, "screen", 100)
            result = adapter.measure_pair("resource:paired", "fresh", 101,
                                          order=("candidate", "reference"), phase="confirmation")
        self.assertEqual(configure.call_count, 1)  # Historical reference need not support this probe.
        self.assertEqual(run.call_count, 6)  # No old pilot, training fit or companion exports.
        for call in run.call_args_list:
            self.assertEqual(call.args[2], 64)
            self.assertEqual(call.args[4], extras.BUDGETS_MIB[call.args[5]])
        self.assertEqual(result["reference_seconds"], .7)
        contexts = result["details"]["confirmation_context"]
        self.assertEqual(contexts["creation_order"], ["candidate", "reference"])
        self.assertEqual(contexts["protocol"], "fresh-source-context-pair-v1")
        self.assertEqual(contexts["warmup_policy"], "fresh-process-target-includes-startup")
        self.assertTrue(contexts["target_boundary_preserved"])
        self.assertEqual(len(set(contexts["context_ids"].values())), 2)
        for path in contexts["context_ids"].values():
            self.assertTrue(Path(path).is_file())
        self.assertEqual(adapter.summary()["resource_cross_regime_functional"], "passed")
        self.assertEqual(result["details"]["raw"]["reference"]["work"]["channel_passes"], 2)
        self.assertEqual(result["details"]["expected_geometry_source"], "candidate_configure")
        self.assertEqual(result["details"]["actual_geometry_source"], "each_complete_export_native_counters")
        receipt = json.loads((adapter.output / "resource-geometry.json").read_text())
        self.assertEqual(receipt["expected_geometry_source"], "candidate_configure")
        adapter.close()

    def test_actual_reference_and_candidate_geometry_mismatch_is_still_rejected(self):
        def candidate_only(binary, *args, **kwargs):
            self.assertEqual(binary, self.context["binary"])
            return geometry(binary, *args, **kwargs)
        for source in ("reference_binary", "binary"):
            with self.subTest(changed_source=source):
                adapter = extras.ExtraWorkloads(dict(self.context, output=self.tmp / source))
                def bad_export(*args, **kwargs):
                    row = export(*args, **kwargs)
                    if args[0] == self.context[source]:
                        row["work"]["source_samples"] -= 1
                    return resource.sealed(row)
                with patch.object(resource, "target_geometry", side_effect=candidate_only), \
                     patch.object(resource, "run_export", side_effect=bad_export):
                    with self.assertRaisesRegex(ValueError, "full-calendar geometry"):
                        adapter.measure_pair("resource:paired", "bad-geometry", 100)

    def test_resource_changed_geometry_budget_counter_and_bytes_fail(self):
        for failure in ("geometry", "budget", "counter", "bytes"):
            adapter = extras.ExtraWorkloads(dict(self.context, output=self.tmp / failure))
            def broken(*args, **kwargs):
                row = export(*args, **kwargs)
                if failure == "geometry":
                    row["work"]["source_samples"] -= 1
                elif failure == "budget":
                    row["regime"]["bounded_workspace_bytes"] = 3 * resource.MIB
                    row["native_execution"]["bounded_workspace_bytes"] = 3 * resource.MIB
                elif failure == "counter":
                    row["work"]["basis_values"] += 1
                elif "candidate" in str(args[0]):
                    row["file_sha256"][resource.FILES[0]] = "wrong"
                return resource.sealed(row)
            with patch.object(resource, "target_geometry", side_effect=geometry), \
                 patch.object(resource, "run_export", side_effect=broken):
                with self.assertRaises(ValueError):
                    adapter.measure_pair("resource:paired", "bad", 1)

    def test_unmatched_resource_seeds_do_not_claim_cross_parity(self):
        adapter = extras.ExtraWorkloads(self.context)
        with patch.object(resource, "target_geometry", side_effect=geometry), \
             patch.object(resource, "run_export", side_effect=export):
            adapter.measure_pair("resource:paired", "screen", 100)
            adapter.measure_pair("resource:sequential", "screen", 101)
        self.assertEqual(adapter.summary()["resource_cross_regime_functional"], "unverified")

    def test_pair_identity_deadline_source_changes_and_thread_contract(self):
        adapter = extras.ExtraWorkloads(self.context)
        with patch.object(planner, "invoke", side_effect=native_observation):
            adapter.measure_pair("planner:early-calendar", "screen", 1)
            with self.assertRaisesRegex(ValueError, "already consumed"):
                adapter.measure_pair("planner:early-calendar", "screen", 2, phase="confirmation")
            adapter.deadline = time.monotonic() - 1
            with self.assertRaises(TimeoutError):
                adapter.measure_pair("planner:early-calendar", "expired", 3)
        with patch.object(adapter, "_read_identity", return_value={"changed": True}):
            with self.assertRaisesRegex(ValueError, "changed during measurements"):
                adapter.close()
        with self.assertRaisesRegex(ValueError, "native_threads=1"):
            extras.ExtraWorkloads(dict(self.context, native_threads=2))


if __name__ == "__main__":
    unittest.main()
