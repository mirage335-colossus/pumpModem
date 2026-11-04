"""Explicit qualification orchestration and cache publication guards; no DSP run."""
import copy
import json
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"tools"))
import triad_perf_bootstrap as bootstrap
import triad_perf_native as native


def arguments(root):
    return SimpleNamespace(candidate_root=ROOT, reference_root=ROOT, binary=root/"candidate",
        reference_binary=root/"reference", output=root/"out", anchor_directory=root/"anchors",
        lifecycle_anchor_directory=root/"lifecycle", coverage_directory=root/"coverage",
        fixture_cache=root/"fixtures", threads=2, native_threads=1, budget_seconds=1800.,
        baseline_ref=bootstrap.complete.BASELINE, qualify_lifecycle_model=False, native_direct_case=[])


class BootstrapTests(unittest.TestCase):
    def test_budget_and_baseline_validation_before_execution(self):
        command = [item for option in ("candidate-root", "reference-root", "binary", "reference-binary", "output",
                   "anchor-directory", "lifecycle-anchor-directory", "coverage-directory", "fixture-cache")
                   for item in ("--"+option, "/tmp/not-executed")]
        args = bootstrap.parse_args(command)
        self.assertEqual(args.budget_seconds, 1800.)
        self.assertFalse(args.qualify_lifecycle_model)
        self.assertEqual(args.native_direct_case, [])
        self.assertEqual(bootstrap.parse_args(command+["--native-direct-case", "native-vlf-low-cn0"]).native_direct_case,
                         ["native-vlf-low-cn0"])
        for change in (["--budget-seconds", "3601"], ["--baseline-ref", "HEAD"], ["--threads", "4"],
                       ["--native-direct-case", "not-a-case"], ["--native-direct-case", "native-vlf-low-cn0"]*2):
            with patch.object(sys, "stderr"), self.assertRaises(SystemExit):
                bootstrap.parse_args(command+change)

    def test_fft_work_binds_current_frequency_grid_geometry_and_numerical_precision(self):
        import triad_perf_cases as fixtures
        from scipy import fft
        config = {"initial_arrival_bounds_seconds": [0, .1], "deterministic_oscillator_cfo_bound_hz": .5,
                  "duration": {"elapsed_source_calendar_seconds": 8.}}
        limits = {"frequency_loss_db": .25, "max_frequency_offsets": 1000}
        manifest = {"sample_count": 1000, "sample_rate": 100.}
        frequencies = fixtures._frequency_grid(config, 1000, 100., limits)
        geometry = {"sample_count": 1000, "sample_rate": 100., "nfft": fft.next_fast_len(1999), "lags": 12,
            "lag_first_sample": 0, "lag_last_sample": 11, "lag_step_samples": 1, "group_tile": 512,
            "precision": "fp32", "device": "cpu"}
        identity = {"frequency_offsets_hz": frequencies, "geometry": geometry, "precision": "fp32", "device": "cpu"}
        agreement = {"eligible": True, "maximum_absolute_score_error": 0., "allowed_score_error": .001, "reasons": []}
        row = {"full_cfo_count": len(frequencies), "precision": "fp32", "configuration": {
            "native_configure": config, "sample_count": 1000, "sample_rate_hz": 100., "lag_group_count": 12},
            "precision_qualification": {"selected": "fp32", "selection_frozen_before_prediction_measurements": True,
                "qualification_cfo_count": 3, "alternatives": {"fp32": {"checks": [agreement]*3}}},
            "measured_bank_numerical_checks": [{"cfo_count": len(frequencies), "agreement": agreement}],
            "coverage": {"measured_bank_numerical_checks": 1, "full_sample_timing_geometry": geometry}}
        self.assertTrue(bootstrap.fft_workload_valid(identity, row, config, manifest, {}, limits))
        for mutation in ("narrow", "geometry", "precision", "numerical", "configure"):
            altered_identity, altered_row = copy.deepcopy(identity), copy.deepcopy(row)
            if mutation == "narrow":
                altered_identity["frequency_offsets_hz"] = frequencies[:3]
                altered_row["full_cfo_count"] = 3
                altered_row["measured_bank_numerical_checks"][0]["cfo_count"] = 3
            elif mutation == "geometry":
                altered_identity["geometry"]["lag_last_sample"] -= 1
            elif mutation == "precision":
                altered_row["precision_qualification"]["selected"] = "fp64"
            elif mutation == "numerical":
                altered_row["measured_bank_numerical_checks"][0]["agreement"]["maximum_absolute_score_error"] = 1.
            else:
                altered_row["configuration"]["native_configure"]["deterministic_oscillator_cfo_bound_hz"] /= 2
            self.assertFalse(bootstrap.fft_workload_valid(altered_identity, altered_row, config, manifest, {}, limits), mutation)

    def test_partial_native_failure_keeps_only_replayed_qualified_record(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(bootstrap, "snapshot", return_value={"fixed": True}):
            root = Path(tmp)
            args = arguments(root)
            args.native_direct_case = ["direct"]
            args.output.mkdir()
            suite = bootstrap.Bootstrap(args)
            suite.save()
            cases = [{"id": "good"}, {"id": "bad"}]
            def run(label, command, cwd=None):
                self.assertEqual(command[command.index("--validation-mode")+1], "qualify")
                self.assertEqual(command[command.index("--stopping-rule")+1], "precision")
                selected = [command[i+1] for i, value in enumerate(command[:-1]) if value == "--case"]
                self.assertNotIn("direct", selected)
                self.assertNotIn("--native-direct-case", command)
                self.assertEqual(command[command.index("--fixture-cache")+1], str(args.fixture_cache))
                anchors = args.output/"new-native-fft-anchors"
                anchors.mkdir()
                rows = []
                for case in cases:
                    value = {"valid": case["id"] == "good"}
                    bootstrap.complete.write_json(anchors/(case["id"]+".native-anchor.json"), value)
                    rows.append({"id": case["id"], "anchors": value, "prediction_valid": value["valid"]})
                bootstrap.complete.write_json(args.output/"native-fft.json", {"cases": rows, "failures": ["later case failed"]})
                return {"status": "failed", "exit_code": 2}
            with patch.object(suite, "run", side_effect=run), patch.object(native, "scenarios", return_value=cases+[{"id": "direct"}]), \
                 patch.object(native, "identity", return_value={}), \
                 patch.object(native, "anchor_compatible", side_effect=lambda value, *_: value["valid"]):
                with self.assertRaisesRegex(ValueError, "later case failed"):
                    suite.native_fft()
            self.assertTrue((args.anchor_directory/"good.native-anchor.json").is_file())
            self.assertFalse((args.anchor_directory/"bad.native-anchor.json").exists())
            suite.report["errors"].append("qualification was partial")
            self.assertEqual(suite.finish(), 2)
            self.assertTrue(suite.report["cache_ready"]["anchors"])
            self.assertEqual(suite.report["native_direct_exclusions"], ["direct"])
            self.assertFalse(suite.report["complete_suite_claim"])

    def test_publication_uses_exact_validated_files_not_restored_directory_contents(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(bootstrap, "snapshot", return_value={"fixed": True}):
            root = Path(tmp)
            dirs = {name: root/name for name in ("anchors", "coverage", "lifecycle")}
            for path in dirs.values():
                path.mkdir()
            good = dirs["anchors"]/"good.native-anchor.json"
            good.write_text("qualified record")
            (dirs["anchors"]/"unvalidated.native-anchor.json").write_text("not checked")
            report = {"schema": bootstrap.SCHEMA, "source_binding_unchanged": True, "source_binding": {"fixed": True},
                "cache_ready": {"anchors": True}, "artifacts": [{"category": "anchors", "path": str(good),
                    "sha256": bootstrap.complete.digest(good), "validation": {"proof": "replayed"}}]}
            paths = bootstrap.publication_paths(report, ROOT, ROOT, {}, dirs)
            self.assertEqual(paths["anchors"], [str(good)])
            for mutate in ("hash", "scope", "binding"):
                bad = copy.deepcopy(report)
                if mutate == "hash": bad["artifacts"][0]["sha256"] = "changed"
                elif mutate == "scope": bad["artifacts"][0]["category"] = "coverage"
                else: bad["source_binding"] = {"changed": True}
                with self.assertRaises(ValueError):
                    bootstrap.publication_paths(bad, ROOT, ROOT, {}, dirs)

    def test_source_change_blocks_publication_but_keeps_evidence(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(bootstrap, "snapshot", return_value={"old": True}) as snapshot:
            args = arguments(Path(tmp)); args.output.mkdir()
            suite = bootstrap.Bootstrap(args)
            source = args.output/"record.json"; source.write_text("record")
            suite.publish(source, args.anchor_directory/"good.native-anchor.json", "anchors", {"proof": "valid"})
            snapshot.return_value = {"new": True}
            self.assertEqual(suite.finish(), 2)
            self.assertFalse(any(suite.report["cache_ready"].values()))
            self.assertTrue((args.anchor_directory/"good.native-anchor.json").is_file())

    def test_cache_publication_retains_unindexed_files_outside_compatible_directory(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(bootstrap, "snapshot", return_value={"fixed": True}):
            root = Path(tmp)
            dirs = {name: root/name for name in ("anchors", "coverage", "lifecycle")}
            for path in dirs.values(): path.mkdir()
            good = dirs["anchors"]/"cpu"/"good.fft-anchor.json"
            good.parent.mkdir(); good.write_text("qualified")
            unqualified = dirs["anchors"]/"old.native-anchor.json"
            unqualified.write_text("unvalidated restored evidence")
            report = {"schema": bootstrap.SCHEMA, "status": "partial_or_unqualified",
                "source_binding_unchanged": True, "source_binding": {"fixed": True},
                "cache_ready": {"anchors": True}, "artifacts": [{"category": "anchors", "path": str(good),
                    "sha256": bootstrap.complete.digest(good), "validation": {"proof": "replayed"}}]}
            quarantine = root/"output"/"unpublished"
            for changed in ({**report, "status": "running"}, {**report, "source_binding": {"stale": True}}):
                with self.assertRaises(ValueError):
                    bootstrap.prepare_publication_cache(changed, ROOT, ROOT, {}, dirs, quarantine)
                self.assertTrue(unqualified.exists())
            paths = bootstrap.prepare_publication_cache(report, ROOT, ROOT, {}, dirs, quarantine)
            self.assertEqual(paths["anchors"], [str(good)])
            self.assertTrue(good.exists())
            self.assertFalse(unqualified.exists())
            self.assertEqual((quarantine/"anchors"/unqualified.name).read_text(), "unvalidated restored evidence")
            self.assertEqual([p for p in dirs["anchors"].rglob("*") if p.is_file()], [good])
            self.assertFalse(json.loads((quarantine.parent/"publication-index.json").read_text())["complete_suite_claim"])

    def test_native_coverage_requires_atomic_prebuild_guard(self):
        accepted = {"historical_applicability": "declared_source_and_dependency_match", "missing_expected_sources": []}
        with tempfile.TemporaryDirectory() as tmp, patch.object(bootstrap.complete, "check_coverage", return_value=accepted):
            path = Path(tmp)/"native.json"
            data = {"compiler": {"coverage_flags": {"cxx": "--coverage -fprofile-update=atomic"}},
                "header_counter_applicability": {"prebuild_postcapture_header_inventory_match": True,
                    "prebuild_postcapture_declared_dependency_inventory_match": True,
                    "counter_translation_units_built_after_guard": True}}
            path.write_text(json.dumps(data))
            self.assertEqual(bootstrap.coverage_valid(path, ROOT, "native"), accepted)
            data["header_counter_applicability"]["counter_translation_units_built_after_guard"] = False
            path.write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError, "before-build"):
                bootstrap.coverage_valid(path, ROOT, "native")

    def test_separate_commands_receive_one_qualification_deadline(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(bootstrap, "snapshot", return_value={}):
            args = arguments(Path(tmp)); args.output.mkdir()
            suite = bootstrap.Bootstrap(args)
            with patch.object(bootstrap.complete, "execute", return_value={"status": "passed", "exit_code": 0}) as execute:
                suite.checked("first", ["never-run-one"])
                suite.checked("second", ["never-run-two"])
            self.assertEqual(execute.call_args_list[0].kwargs["deadline"], execute.call_args_list[1].kwargs["deadline"])


if __name__ == "__main__":
    unittest.main()
