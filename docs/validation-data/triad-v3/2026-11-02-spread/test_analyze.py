#!/usr/bin/env python3
"""Focused schema/integrity tests; no receiver, GPU, or cloud execution."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

SPEC = importlib.util.spec_from_file_location("spread_analyzer", Path(__file__).with_name("analyze.py"))
analysis = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(analysis)


class AnalyzerTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="spread-analysis-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.config = {"false_probability": .0001, "horizon_seconds": 86400., "attempt_spacing": 60.,
                       "selection_methods": 16, "duration_alternatives": 8,
                       "methods": ["rbpf-ucb"], "receiver_methods": ["static-fft-ucb"],
                       "mode": "actual", "cn0": -60., "seed": 1}
        self.gate = analysis.gates(self.config)["strict"]
        self.manifests = []
        for i in range(2):
            path = self.root/"data"/f"path-{i:05d}"; path.mkdir(parents=True)
            manifest = {"format": "triad-block-stats-v1", "control": "signal", "signal_bit": i,
                        "sample_rate": 1., "sample_count": 64, "requested_active_seconds": 64.,
                        "actual_elapsed_seconds": 64., "cn0_db_hz": -60.,
                        "source_format": {"inner_rate_hz": .0036, "fine_rate_hz": .0006, "carrier_spacing_hz": .00075},
                        "channel_model": {"spread_delay_ms": .005, "spread_doppler_hz": .001,
                                          "path_evolution": {"ray_coherence_seconds": 10000.}},
                        "random_seed_roots": {name: 1000*(j+1)+i for j,name in enumerate(analysis.PHYSICAL_ROOTS)}}
            self.write(path/"manifest.json", manifest); self.manifests.append(path/"manifest.json")

    def write(self, path, value):
        path.write_text(json.dumps(value))

    def hashes(self, i):
        return {"manifest.json": analysis.sha256(self.manifests[i]), "gram.c128": "b"*64}

    def conditional(self, actual=False, two_paths=True):
        records = []; arrays = {}; method = "static-fft-ucb"
        for i in range(2 if two_paths else 1):
            record = {"input": f"/original/data/path-{i:05d}", "physical_path_index": i, "status": "completed",
                      "input_hashes" if actual else "sampled_input_hashes": self.hashes(i), "conditions": []}
            for control in (["signal"] if actual else ["signal", "null"]):
                values = np.array([[self.gate+1, 0], [0, self.gate+1], [0, 0.]])
                if actual: values = values[i:i+1]
                key = f"path{i}_{control}_{method}"; arrays[key] = values
                record["conditions"].append({"control": control, "cn0_db_hz": None if actual else -62.,
                    "noise_trials": len(values), "methods": {method: {
                        "score_array": key, "threshold": self.gate,
                        "counts": analysis.recount(values, i if control == "signal" else None, control, self.gate, "trials")}}})
            records.append(record)
        np.savez_compressed(self.root/"scores.npz", **arrays)
        report = {"schema": "triad-conditional-sweep-v1", "observation_mode": "actual-noisy" if actual else "conditional",
                  "configuration": copy.deepcopy(self.config), "paths": records, "sampled_physical_paths": len(records),
                  "failed_physical_path_records": 0, "scores_file": "scores.npz", "scores_sha256": analysis.sha256(self.root/"scores.npz")}
        self.write(self.root/"report.json", report)
        return report, arrays

    def actual_report(self, schema="triad-static-frequency-study-v1", method="static-frequency-ucb"):
        captures = []; arrays = {}
        for i in range(2):
            values = np.array([[self.gate+1, 0]]) if i == 0 else np.array([[0, self.gate+1]])
            key = f"path{i:05d}_signal_{method}"; arrays[key] = values
            threshold = {"threshold_log_likelihood_ratio": self.gate} if schema == "triad-noise-study-v1" else self.gate
            captures.append({"input": f"/original/data/path-{i:05d}", "physical_path_index": i, "status": "completed",
                             "control": "signal", "truth_bit_simulation_only": i, "input_hashes": self.hashes(i),
                             "methods": {method: {"threshold": threshold, "bit_log_scores": values[0].tolist(),
                                                   "counts": analysis.recount(values, i, "signal", self.gate, "captures")}}})
        np.savez_compressed(self.root/"actual.npz", **arrays)
        config = copy.deepcopy(self.config); config["methods"] = [method]; config.pop("receiver_methods")
        report = {"schema": schema, "configuration": config, "captures": captures,
                  "failed_physical_capture_records": 0, "scores_file": "actual.npz", "scores_sha256": analysis.sha256(self.root/"actual.npz")}
        self.write(self.root/"report.json", report)
        return report, arrays

    def test_conditional_trials_are_not_physical_paths(self):
        self.conditional()
        result = analysis.analyze(self.root)
        self.assertEqual(result["summary"]["distinct_observed_physical_input_paths"], 2)
        self.assertEqual(result["summary"]["conditional_method_noise_trials"], 12)
        signal = next(g for g in result["reports"][0]["groups"] if g["control"] == "signal")
        self.assertEqual(signal["noise_trials"], 6)
        self.assertEqual(signal["independent_physical_paths_by_recorded_roots"], 2)
        self.assertIsNone(signal["strict"]["pooled_cp"])
        self.assertFalse(signal["scientific_qualification"])
        row = result["reports"][0]["rows"][0]
        self.assertEqual(row["strict"]["counts"]["correct_accepted"], 1)
        self.assertEqual(row["strict"]["counts"]["wrong_accepted"], 1)
        self.assertEqual(row["strict"]["counts"]["missed"], 1)
        self.assertEqual(row["cn0_db_hz"], -62)
        self.assertEqual(row["physical_configuration"]["design_cn0_db_hz"], -60)

    def test_actual_wrapper_counts_one_observation_per_path(self):
        self.conditional(actual=True)
        result = analysis.analyze(self.root)
        self.assertEqual(result["summary"]["actual_method_path_observations"], 2)
        self.assertEqual(result["summary"]["conditional_method_noise_trials"], 0)
        group = result["reports"][0]["groups"][0]
        self.assertEqual(group["strict"]["pooled_cp_if_iid"]["correct_accepted"]["trials"], 2)
        self.assertEqual(group["cn0_db_hz"], -60)

    def test_existing_actual_receiver_schemas(self):
        for schema, method in [("triad-static-frequency-study-v1", "static-frequency-ucb"),
                               ("triad-rbpf-study-v1", "rbpf-ucb"), ("triad-noise-study-v1", "gaussian-ou")]:
            with self.subTest(schema=schema):
                self.actual_report(schema, method)
                result = analysis.analyze(self.root)
                self.assertEqual(result["reports"][0]["groups"][0]["strict"]["counts"]["correct_accepted"], 2)

    def test_exact_pointwise_cp_and_tie_rule(self):
        self.assertAlmostEqual(analysis.cp(0, 32)["upper_one_sided_95"], 1-.05**(1/32))
        self.assertAlmostEqual(analysis.cp(300, 300)["lower_one_sided_95"], .05**(1/300))
        counts = analysis.recount([[self.gate, self.gate]], 1, "signal", self.gate, "trials")
        self.assertEqual(counts["wrong_accepted"], 1)
        self.assertEqual(analysis.recount([[self.gate, 0]], {"not": "a bit"}, "null", self.gate, "trials")["false_admitted"], 1)

    def test_strict_and_relaxed_recount_are_separate(self):
        report, arrays = self.actual_report()
        key = next(iter(arrays)); arrays[key][0] = [(self.gate+analysis.gates(self.config)["relaxed"])/2, 0]
        detail = report["captures"][0]["methods"]["static-frequency-ucb"]
        detail["bit_log_scores"] = arrays[key][0].tolist()
        detail["counts"] = analysis.recount(arrays[key], 0, "signal", self.gate, "captures")
        np.savez_compressed(self.root/"actual.npz", **arrays); report["scores_sha256"] = analysis.sha256(self.root/"actual.npz")
        self.write(self.root/"report.json", report)
        row = analysis.analyze(self.root)["reports"][0]["rows"][0]
        self.assertEqual(row["strict"]["counts"]["missed"], 1)
        self.assertEqual(row["relaxed"]["counts"]["correct_accepted"], 1)

    def test_missing_score_archive_fails_without_inline(self):
        self.conditional(); (self.root/"scores.npz").unlink()
        with self.assertRaisesRegex(ValueError, "missing score archive"):
            analysis.analyze(self.root)

    def test_missing_conditional_signal_manifest_fails(self):
        self.conditional(); self.manifests[0].unlink()
        with self.assertRaisesRegex(ValueError, "conditional signal requires"):
            analysis.analyze(self.root)

    def test_duplicate_path_and_repeated_report_fail(self):
        report, _ = self.conditional(); report["paths"][1] = copy.deepcopy(report["paths"][0])
        self.write(self.root/"report.json", report)
        with self.assertRaisesRegex(ValueError, "duplicate physical path"):
            analysis.analyze(self.root)
        self.conditional(); (self.root/"clone.json").write_bytes((self.root/"report.json").read_bytes())
        with self.assertRaisesRegex(ValueError, "duplicate input/configuration"):
            analysis.analyze(self.root)

    def test_wrong_reported_counts_fail(self):
        report, _ = self.conditional(); report["paths"][0]["conditions"][0]["methods"]["static-fft-ucb"]["counts"]["correct_accepted"] += 1
        self.write(self.root/"report.json", report)
        with self.assertRaisesRegex(ValueError, "reported counts disagree"):
            analysis.analyze(self.root)

    def test_completed_actual_missing_method_and_score_cannot_drop_attempt(self):
        report, arrays = self.actual_report()
        report["captures"][1]["methods"] = {}
        arrays.pop("path00001_signal_static-frequency-ucb")
        np.savez_compressed(self.root/"actual.npz", **arrays)
        report["scores_sha256"] = analysis.sha256(self.root/"actual.npz")
        self.write(self.root/"report.json", report)
        with self.assertRaisesRegex(ValueError, "completed condition missing declared receiver"):
            analysis.analyze(self.root)

    def test_completed_conditional_requires_all_declared_methods(self):
        report, arrays = self.conditional()
        missing = report["paths"][0]["conditions"][0]["methods"].pop("static-fft-ucb")
        arrays.pop(missing["score_array"])
        np.savez_compressed(self.root/"scores.npz", **arrays)
        report["scores_sha256"] = analysis.sha256(self.root/"scores.npz")
        self.write(self.root/"report.json", report)
        with self.assertRaisesRegex(ValueError, "completed condition missing declared receiver"):
            analysis.analyze(self.root)

    def test_completed_actual_wrapper_uses_receiver_methods_declaration(self):
        report, arrays = self.conditional(actual=True)
        missing = report["paths"][1]["conditions"][0]["methods"].pop("static-fft-ucb")
        arrays.pop(missing["score_array"])
        np.savez_compressed(self.root/"scores.npz", **arrays)
        report["scores_sha256"] = analysis.sha256(self.root/"scores.npz")
        self.write(self.root/"report.json", report)
        with self.assertRaisesRegex(ValueError, "completed condition missing declared receiver"):
            analysis.analyze(self.root)

    def test_undeclared_method_result_rejected(self):
        report, _ = self.actual_report()
        report["captures"][0]["methods"]["unexpected-receiver"] = copy.deepcopy(report["captures"][0]["methods"]["static-frequency-ucb"])
        self.write(self.root/"report.json", report)
        with self.assertRaisesRegex(ValueError, "undeclared receiver method"):
            analysis.analyze(self.root)

    def test_partial_failed_path_is_preserved_but_excluded(self):
        report, _ = self.conditional(); report["paths"][1].update(status="error", error="later condition failed")
        report["failed_physical_path_records"] = 1; self.write(self.root/"report.json", report)
        result = analysis.analyze(self.root)["reports"][0]
        self.assertEqual(len(result["rows"]), 2)
        self.assertEqual(len(result["excluded_partial_score_rows"]), 2)
        self.assertEqual(result["failures"][0]["partial_score_rows"], 2)

    def test_failed_actual_signal_counted_conservatively(self):
        report, arrays = self.actual_report()
        report["captures"][1].update(status="error", methods={}, error="load failed")
        report["failed_physical_capture_records"] = 1
        arrays.pop("path00001_signal_static-frequency-ucb")
        np.savez_compressed(self.root/"actual.npz", **arrays); report["scores_sha256"] = analysis.sha256(self.root/"actual.npz")
        self.write(self.root/"report.json", report)
        result = analysis.analyze(self.root)["reports"][0]
        counts = result["conservative_actual_signal_attempt_counts"]["static-frequency-ucb"]["strict"]
        self.assertEqual(counts["attempts"], 2); self.assertEqual(counts["missed"], 1)
        self.assertEqual(result["groups"][0]["strict"]["counts"]["captures"], 1)

    def test_unknown_failed_actual_wrapper_cannot_reduce_signal_attempts(self):
        report, arrays = self.conditional(actual=True)
        report["paths"][1].update(status="error", conditions=[], error="manifest unavailable")
        self.manifests[1].unlink(); report["failed_physical_path_records"] = 1
        arrays.pop("path1_signal_static-fft-ucb")
        np.savez_compressed(self.root/"scores.npz", **arrays)
        report["scores_sha256"] = analysis.sha256(self.root/"scores.npz")
        self.write(self.root/"report.json", report)
        analysis_result = analysis.analyze(self.root); result = analysis_result["reports"][0]
        self.assertEqual(result["observed_physical_path_records"], 2)
        self.assertEqual(result["failed_physical_path_records"], 1)
        self.assertIsNone(result["failures"][0]["control"])
        self.assertEqual(result["unclassified_failed_actual_physical_path_records"], 1)
        self.assertFalse(result["actual_signal_attempt_denominator_established"])
        self.assertEqual(analysis_result["summary"]["unclassified_failed_actual_path_records"], 1)
        for budget in ("strict", "relaxed"):
            counts = result["conservative_actual_signal_attempt_counts"]["static-fft-ucb"][budget]
            self.assertEqual(counts["attempts"], 2); self.assertEqual(counts["missed"], 1)
            self.assertEqual(counts["known_signal_attempts"], 1)
            self.assertEqual(counts["unknown_control_failures_counted_as_missed"], 1)
            self.assertFalse(counts["signal_denominator_established"])

    def test_explicit_failed_actual_controls_are_preserved(self):
        original_manifest = self.manifests[1].read_text()
        for control in ("null", "wrong-key"):
            with self.subTest(control=control):
                report, arrays = self.conditional(actual=True)
                report["paths"][1].update(status="error", conditions=[], control=control, error="manifest unavailable")
                self.manifests[1].unlink(missing_ok=True); report["failed_physical_path_records"] = 1
                arrays.pop("path1_signal_static-fft-ucb")
                np.savez_compressed(self.root/"scores.npz", **arrays)
                report["scores_sha256"] = analysis.sha256(self.root/"scores.npz")
                self.write(self.root/"report.json", report)
                result = analysis.analyze(self.root)["reports"][0]
                self.assertEqual(result["failures"][0]["control"], control)
                self.assertEqual(result["unclassified_failed_actual_physical_path_records"], 0)
                counts = result["conservative_actual_signal_attempt_counts"]["static-fft-ucb"]["strict"]
                self.assertEqual(counts["attempts"], 1); self.assertEqual(counts["missed"], 0)
                self.assertEqual(counts["unknown_control_failures_counted_as_missed"], 0)
                self.assertTrue(counts["signal_denominator_established"])
                # Recreate the manifest for the next original two-path fixture.
                self.manifests[1].write_text(original_manifest)

    def test_failed_actual_none_control_recovers_retained_manifest_label(self):
        report, arrays = self.conditional(actual=True)
        manifest = json.loads(self.manifests[1].read_text()); manifest["control"] = "null"
        self.write(self.manifests[1], manifest)
        report["paths"][1].update(status="error", conditions=[], control=None, input_hashes={}, error="array validation failed")
        report["failed_physical_path_records"] = 1; arrays.pop("path1_signal_static-fft-ucb")
        np.savez_compressed(self.root/"scores.npz", **arrays)
        report["scores_sha256"] = analysis.sha256(self.root/"scores.npz")
        self.write(self.root/"report.json", report)
        result = analysis.analyze(self.root)["reports"][0]
        self.assertEqual(result["failures"][0]["control"], "null")
        self.assertEqual(result["unclassified_failed_actual_physical_path_records"], 0)
        counts = result["conservative_actual_signal_attempt_counts"]["static-fft-ucb"]["strict"]
        self.assertEqual(counts["attempts"], 1); self.assertEqual(counts["missed"], 0)

    def test_index_disambiguates_without_scanning_all_keys(self):
        index = analysis.WorkspaceIndex(self.root)
        self.assertEqual(index.resolve("/moved/data/path-00001"), "data/path-00001")
        self.assertEqual(index.resolve("/moved/data/path-00001"), "data/path-00001")
        self.assertEqual(len(index.cache), 1)

    def test_plan_checks_missing_paths_hashes_and_commands(self):
        report, _ = self.actual_report()
        argv = ["python", "triad-static-frequency-study.py", "/old/data/path-00000", "/old/data/path-00001",
                "--cn0", "-60", "--output", "/old/report.json"]
        plan = {"jobs": [{"argv": argv, "report_sha256": analysis.sha256(self.root/"report.json")} ]}
        p = self.root/"plan.json"; self.write(p, plan)
        self.assertEqual(analysis.analyze(self.root, p)["plan_binding"]["checks"][0]["planned_paths_accounted"], 2)
        plan["jobs"][0]["argv"].insert(4, "/old/data/path-00002"); self.write(p, plan)
        with self.assertRaisesRegex(ValueError, "missing or changed planned paths"):
            analysis.analyze(self.root, p)
        plan["jobs"][0]["argv"].pop(4); plan["jobs"][0]["argv"][5] = "-50"; self.write(p, plan)
        with self.assertRaisesRegex(ValueError, "command/configuration mismatch"):
            analysis.analyze(self.root, p)

    def test_unrecorded_explicit_plan_option_fails_closed(self):
        self.actual_report()
        plan = {"jobs": [{"argv": ["python", "triad-static-frequency-study.py", "/old/data/path-00000",
                                   "/old/data/path-00001", "--unrecorded-scientific-option", "777",
                                   "--output", "/old/report.json"]}]}
        path = self.root/"plan.json"; self.write(path, plan)
        with self.assertRaisesRegex(ValueError, "unrecorded explicit plan option.*--unrecorded-scientific-option"):
            analysis.analyze(self.root, path)


if __name__ == "__main__":
    unittest.main()
