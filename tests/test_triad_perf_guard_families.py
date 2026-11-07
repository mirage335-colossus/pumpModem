"""Cheap boundary/integrity tests; no full sampled receiver campaign is run."""
from copy import deepcopy
import json
import subprocess
from pathlib import Path
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch, Mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import triad_perf_guard_families as guard
import triad_perf_guard as suite_guard


def small_manifest():
    cases = []
    for name, captures, fixture in (("single", 1, "shared"), ("batch", 3, "shared"),
                                    ("other", 1, "separate")):
        cases.append({"id": name, "family": "gaussian-state", "fixture": fixture,
                      "captures": captures, "devices": ["cpu", "cuda"],
                      "dependencies": [], "unit": "blocks", "options": {}})
    return {"fixtures": {name: {"mode": "export-stats", "args": ["--seconds", seconds]}
                         for name, seconds in (("shared", "8"), ("separate", "16"))}, "cases": cases}


class FakeWorker:
    external_startup_seconds = .01
    identity = {"threads": 2}

    def __init__(self, root, **kwargs):
        self.root = Path(root)
        self.name = kwargs.get("name")
        self.closed = False

    def request(self, request):
        return {"source_sha256": guard.sample.stage_sources(self.root, request["case"]),
                "environment": {"threads": 2}, "environment_matches_setup": True,
                "prepare_seconds": .01}

    def close(self):
        self.closed = True


class FamilyGuardTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        self.context = {
            "candidate_root": ROOT, "reference_root": ROOT,
            "binary": self.directory / "candidate", "reference_binary": self.directory / "reference",
            "fixture_cache": self.directory / "unused-cache", "device": "cpu", "precision": "fp32",
            "threads": 2, "native_threads": 1, "deadline": time.monotonic() + 30,
            "output": self.directory / "output",
            # Existing cache/composition tests explicitly exercise the retained
            # qualification diagnostic rather than the routine default.
            "family_screen_method": "shared-composed-diagnostic",
        }
        for name in ("binary", "reference_binary"):
            self.context[name].write_bytes(b"mock native binary")
        self.exports, self.receives, self.jobs, self.workers = [], [], [], []
        self.bad_scores = False
        self.bad_count = False
        self.bad_source = False
        self.regime = 4
        self.regime_extra = [{}, {}]
        self.environment_ok = True
        self.manifest = small_manifest()

        def worker_factory(*args, **kwargs):
            worker = FakeWorker(*args, **kwargs)
            original = worker.request
            def request(value):
                result = original(value)
                result["environment_matches_setup"] = self.environment_ok
                return result
            worker.request = request
            self.workers.append(worker)
            return worker

        self.patchers = [
            patch.object(guard.sample, "manifest", lambda: deepcopy(self.manifest)),
            patch.object(guard.sample, "load_source", return_value=SimpleNamespace(ReceiverWorker=worker_factory)),
            patch.object(guard.sample, "export_fixtures", self.export),
            patch.object(guard.sample, "receive", self.receive),
            patch.object(guard.sample, "measure_job", self.job),
        ]
        for patcher in self.patchers:
            patcher.start()
            self.addCleanup(patcher.stop)

    def adapter(self):
        adapter = guard.FamilyWorkloads(self.context)
        # Tests exercising rejected mutations close explicitly after restoring them.
        self.addCleanup(lambda: adapter.close() if not adapter.closed else None)
        return adapter

    def export(self, binary, profile, seed, threads, directory, count, deadline):
        self.exports.append((Path(binary).name, seed, count, deepcopy(profile)))
        paths = []
        for index in range(count):
            path = directory / f"capture-{index}"
            path.mkdir(parents=True)
            guard.sample.write_json(path / "manifest.json", {
                "sample_count": 16, "channel_samples": 16, "stats_noisy": True,
                "source_samples": 14, "block_count": 4, "channel_tail_samples": 2,
                "sample_rate": 2, "block_samples": 4, "seed": seed + index,
            })
            (path / "data.bin").write_bytes(str(seed + index).encode())
            paths.append(path)
        times = [float(i + 1) for i in range(count)]
        return paths, {"commands": [[str(binary), str(seed + i)] for i in range(count)],
                       "invocation_wall_seconds": times, "wall_seconds": sum(times),
                       "seed": seed, "count": count}

    def receive(self, worker, paths, case, device, precision, limit=None):
        self.assertIsNone(limit)
        self.receives.append((case["id"], len(paths), worker, precision))
        scores = [0., 1.]
        if self.bad_scores and worker.name == "candidate":
            scores = [1., 0.]
        return {"receiver": {
                    "seconds": {"load": .1, "process": .3},
                    "work": {"count": 3 if self.bad_count else 4, "target_count": 4,
                             "unit": case["unit"], "resampling_events": len(self.receives)},
                    "regime": {"blocks": self.regime, "precision": precision,
                             **deepcopy(self.regime_extra[0 if worker.name == "reference" else 1])},
                    "result": {"scores": scores, "winner": 1}},
                "complete_receiver": True, "request_wall_seconds": .5,
                "receiver_wall_seconds": .4,
                "source_sha256": {} if self.bad_source else guard.sample.stage_sources(worker.root, case)}

    def job(self, worker, binary, profile, case, seed, threads, directory, device,
            precision, deadline, *, limits=(None,)):
        self.jobs.append((case["id"], seed, tuple(limits), Path(binary).name))
        paths, generation = self.export(binary, profile, seed, threads, directory,
                                         case["captures"], deadline)
        return {"generation": generation,
                "receiver_runs": [self.receive(worker, paths, case, device, precision)],
                "complete_pipeline_wall_seconds": generation["wall_seconds"] + .75,
                "fixture": guard.sample.fixture_evidence(paths)}

    def test_physical_alignment_keeps_direct_jobs_and_strict_input_comparison(self):
        self.context.update(physical_bit_alignment=True,family_screen_method="direct-full")
        adapter=self.adapter();events=[]
        alignment={"schema":guard.physical.SCHEMA,"trials":3,"physical_signal_bits":[1,1,1],"sources":{
            "reference":{"tx_masks":[0,1,0],"rx_masks":[0,1,0]},
            "candidate":{"tx_masks":[1,0,1],"rx_masks":[1,0,1]}}}
        def prepare(*args,**kwargs):events.append("source-only");return alignment
        def job(worker,*args,**kwargs):
            events.append(worker.name)
            expected=[guard.physical.arguments(alignment,worker.name,i) for i in range(3)]
            self.assertEqual(kwargs.pop("physical_mask_arguments"),expected)
            return self.job(worker,*args,**kwargs)
        with patch.object(guard.physical,"prepare_pair",side_effect=prepare),patch.object(
                guard.sample,"measure_job",side_effect=job),patch.object(
                guard.physical,"validate_fixture_inputs",return_value={"all_binary_files":"equal"}) as inputs:
            result=adapter.measure_pair("family:batch","aligned",123,order=("candidate","reference"))
        self.assertEqual(events,["source-only","candidate","reference"])
        self.assertEqual(len(self.jobs),2);inputs.assert_called_once()
        self.assertEqual(result["details"]["metadata"]["physical_source_alignment"],alignment)
        self.assertEqual(result["details"]["metadata"]["complete_input_agreement"],{"all_binary_files":"equal"})

    def test_descriptors_are_lazy_and_complete_full_geometry(self):
        adapter = self.adapter()
        self.assertEqual(len(adapter.descriptors()), 3)
        self.assertTrue(all(d["id"].startswith("family:") for d in adapter.descriptors()))
        self.assertFalse(self.workers or self.exports or self.receives)
        self.assertFalse(adapter.output.exists())
        self.assertEqual(adapter.descriptors()[1]["full_geometry"]["captures"], 3)

    def test_full_manifest_has_nine_cpu_twelve_cuda_cases(self):
        manifest = guard.sample.MANIFEST.read_text()
        import json
        self.manifest = json.loads(manifest)
        self.assertEqual(len(guard.FamilyWorkloads(self.context).descriptors()), 9)
        self.context["device"] = "cuda:0"
        descriptors = guard.FamilyWorkloads(self.context).descriptors()
        self.assertEqual(len(descriptors), 12)
        tensor = next(row for row in descriptors if row["id"] == "family:phase-tensor-hour")
        self.assertEqual(tensor["full_geometry"]["options"]["tensor_mode"], "tf32x3")
        self.assertTrue(all(row["available"] for row in descriptors))
        self.assertFalse(self.workers or self.exports)

    def test_cuda_fp64_tensor_is_explicitly_unavailable_without_execution(self):
        import json
        self.manifest = json.loads(guard.sample.MANIFEST.read_text())
        self.context.update(device="cuda:0", precision="fp64")
        adapter = self.adapter()
        descriptors = adapter.descriptors()
        self.assertEqual(len(descriptors), 12)
        unavailable = [row for row in descriptors if not row["available"]]
        self.assertEqual([row["id"] for row in unavailable], ["family:phase-tensor-hour"])
        self.assertIn("FP32", unavailable[0]["reason"])
        with self.assertRaisesRegex(ValueError, "unavailable.*FP32"):
            adapter.measure_pair("family:phase-tensor-hour", "unsupported", 100)
        self.assertFalse(self.workers or self.exports)

    def test_default_screen_measures_actual_full_pipeline_without_warmup_or_model(self):
        self.context.pop("family_screen_method")
        adapter=self.adapter()
        self.assertTrue(all(row["screen_method"]=="direct-full" for row in adapter.descriptors()))
        one=adapter.measure_pair("family:single","screen-one",100,order=("candidate","reference"))
        batch=adapter.measure_pair("family:batch","screen-batch",100)
        other=adapter.measure_pair("family:other","screen-other",100)
        self.assertEqual(len(self.workers),2)
        self.assertEqual(len(self.jobs),6)
        self.assertEqual([row[2] for row in self.jobs],[(None,)]*6)
        self.assertEqual(one["reference_seconds"],1.75)  # Actual outer job, not 1.0+.5 composition.
        self.assertEqual(batch["reference_seconds"],6.75)
        for row in [one,batch,other]:
            self.assertEqual(row["details"]["timing_field"],"complete_pipeline_wall_seconds")
            self.assertTrue(row["details"]["full_fresh"])
            self.assertEqual(row["details"]["metadata"]["method"],"direct_full")
            self.assertTrue(row["details"]["metadata"]["direct_complete_pipeline_observation"])
            self.assertFalse(row["details"]["metadata"]["statistical_independence_claim"])
            self.assertFalse(row["details"]["metadata"]["extrapolation"])
            self.assertFalse(row["details"]["metadata"]["generation_shared"])
            self.assertNotIn("confirmation_context",row["details"])
            self.assertNotIn("screen_estimated_full_seconds",row["details"]["pair"]["reference"])
        self.assertFalse(adapter.groups)
        summary=adapter.close()
        accounting=summary["direct_screen_accounting"]
        self.assertEqual(accounting["paired_complete_pipeline_observations"],3)
        self.assertEqual(accounting["source_complete_pipeline_observations"],6)
        self.assertEqual(accounting["native_export_invocations"],10)
        self.assertEqual(accounting["actual_generation_wall_seconds"],16.)
        self.assertEqual(accounting["actual_complete_pipeline_wall_seconds"],20.5)
        self.assertFalse(accounting["prediction_model_used"])
        self.assertEqual(summary["shared_generation_accounting"]["estimated_native_invocation_seconds_saved"],0.)
        self.assertTrue(all(worker.closed for worker in self.workers))

    def test_default_all_nine_case_claims_are_namespaced_but_confirmations_remain_global(self):
        self.context.pop("family_screen_method")
        self.manifest=json.loads(guard.sample.MANIFEST.read_text())
        adapter=self.adapter()
        for index,descriptor in enumerate(adapter.descriptors()):
            adapter.measure_pair(descriptor["id"],f"screen-{index}",1000)
        self.assertEqual(len(self.workers),2)
        self.assertEqual(len(self.jobs),18)
        self.assertEqual(sum(row[2] for row in self.exports),22)
        self.assertFalse(adapter.groups)
        claims=adapter.ledger["generation_seed_claims"]
        self.assertEqual(len(claims),9)
        self.assertEqual({row["screen_case_id"] for row in claims},set(adapter.cases))
        self.assertTrue(all(row["profile_sha256"]==guard.sample.digest(adapter.manifest["fixtures"][
            adapter.cases[row["screen_case_id"]]["fixture"]]) for row in claims))
        with self.assertRaisesRegex(ValueError,"overlaps"):
            adapter.measure_pair("family:gaussian-hour","duplicate-screen-seed",1000)
        with self.assertRaisesRegex(ValueError,"overlaps"):
            adapter.measure_pair("family:state-sampled-smoke","bad-confirm",1002,phase="confirmation")

    def test_default_direct_screen_keeps_numerical_and_work_validation(self):
        self.context.pop("family_screen_method")
        adapter=self.adapter();self.bad_count=True
        with self.assertRaisesRegex(ValueError,"full endpoint"):
            adapter.measure_pair("family:single","bad-direct-count",100)
        self.assertTrue(list(adapter.output.glob("pair-*.json")))
        self.assertEqual(len(self.jobs),2)

    def test_unknown_family_screen_method_is_not_an_implicit_fallback(self):
        self.context["family_screen_method"]="automatic-unqualified-model"
        with self.assertRaisesRegex(ValueError,"family_screen_method"):
            guard.FamilyWorkloads(self.context)
        self.assertFalse(self.workers or self.exports)

    def test_screen_shares_generation_prefix_but_rebuilds_full_receiver(self):
        adapter = self.adapter()
        one = adapter.measure_pair("family:single", "screen-1", 100,
                                   order=("candidate", "reference"))
        batch = adapter.measure_pair("family:batch", "screen-2", 200)
        again = adapter.measure_pair("family:single", "screen-3", 300)
        self.assertEqual(len(self.workers), 2)
        self.assertEqual([(name, seed, count) for name, seed, count, _ in self.exports],
                         [("candidate", 100, 3), ("reference", 100, 3)])
        self.assertEqual([row[1] for row in self.receives], [1, 1, 3, 3, 1, 1])
        self.assertEqual(one["reference_seconds"], 1.5)
        self.assertEqual(batch["reference_seconds"], 6.5)
        self.assertFalse(batch["details"]["metadata"]["generation_group_created"])
        self.assertFalse(again["details"]["full_fresh"])
        self.assertEqual(again["details"]["metadata"]["actual_generation_seed"], 100)
        self.assertNotIn("complete_pipeline_wall_seconds", again["details"]["pair"]["reference"])
        summary = adapter.close()
        self.assertEqual(len(summary["shared_generation"]), 1)
        accounting = summary["shared_generation_accounting"]
        self.assertEqual(accounting["measured_native_invocation_seconds"], 12.)
        self.assertEqual(accounting["estimated_repeated_generation_seconds_for_completed_screens"], 16.)
        self.assertEqual(accounting["estimated_native_invocation_seconds_saved"], 4.)
        self.assertGreater(summary["adapter_active_wall_seconds"], 0.)
        self.assertEqual(len(summary["operations"]), 3)
        self.assertTrue(all(worker.closed for worker in self.workers))

    def test_confirmations_always_fresh_full_pipeline_not_screen_estimate(self):
        adapter = self.adapter()
        adapter.measure_pair("family:single", "screen", 100)
        result = adapter.measure_pair("family:single", "confirmation", 500,
                                      order=("candidate", "reference"), phase="confirmation")
        self.assertEqual([(row[0], row[2], row[3]) for row in self.jobs],
                         [("single", (None,), "candidate"), ("single", (None,), "reference"),
                          ("single", (None,), "candidate"), ("single", (None,), "reference")])
        self.assertEqual(self.jobs[0][1], self.jobs[1][1])
        self.assertNotEqual(self.jobs[0][1], 500)
        self.assertEqual([row[1] for row in self.jobs[2:]], [500, 500])
        self.assertEqual(result["reference_seconds"], 1.75)
        self.assertTrue(result["details"]["full_fresh"])
        self.assertTrue(result["details"]["metadata"]["independent_complete_pipeline_observation"])
        self.assertFalse(result["details"]["metadata"]["generation_shared"])
        context = result["details"]["confirmation_context"]
        self.assertEqual(context["protocol"], "fresh-source-context-pair-v1")
        self.assertEqual(context["creation_order"], ["candidate", "reference"])
        self.assertTrue(context["target_boundary_preserved"])
        self.assertEqual(context["warmup_policy"], "full-target-outside-timer")
        self.assertEqual(context["teardown"]["reason"], "confirmation_period_complete")
        self.assertEqual(len(set(context["context_ids"].values())), 2)
        self.assertTrue(all(row["warmup"]["complete_pipeline_wall_seconds"] > 0
                            for row in context["workers"].values()))
        self.assertEqual(result["details"]["identity_before"], result["details"]["identity_after"])
        # No fresh fixture remains after the complete-pipeline receipt is retained.
        self.assertFalse(list(Path(adapter.scratch.name).glob("fresh-*")))
        self.assertTrue(Path(result["details"]["raw_receipt"]).is_file())
        raw = json.loads(Path(result["details"]["raw_receipt"]).read_text())
        self.assertEqual(raw["confirmation_context"]["context_id"], context["context_id"])
        self.assertEqual(set(raw["confirmation_context"]["isolation_evidence"]), {"reference", "candidate"})
        accepted, reason = suite_guard._context_receipt(result, ("candidate", "reference"), set())
        self.assertIsNone(reason)
        self.assertEqual(set(accepted), set(context["context_ids"].values()))
        raw = result["details"]["pair"]["reference"]
        self.assertLessEqual(raw["invocation_started"]["monotonic_seconds"],
                             raw["invocation_finished"]["monotonic_seconds"])

    def test_reused_run_ids_and_any_overlapping_capture_seed_rejected(self):
        adapter = self.adapter()
        adapter.measure_pair("family:single", "screen", 100)
        with self.assertRaisesRegex(ValueError, "run_id"):
            adapter.measure_pair("family:single", "screen", 200)
        with self.assertRaisesRegex(ValueError, "overlaps"):
            adapter.measure_pair("family:single", "fresh", 102, phase="confirmation")
        self.assertFalse(self.jobs)

    def test_complete_cpu_screen_schedule_accepts_common_seed_across_profiles(self):
        import json
        self.manifest = json.loads(guard.sample.MANIFEST.read_text())
        adapter = self.adapter()
        descriptors = adapter.descriptors()
        self.assertEqual(len(descriptors), 9)
        for index, descriptor in enumerate(descriptors):
            result = adapter.measure_pair(descriptor["id"], f"screen-{index}", 1000)
            self.assertEqual(result["details"]["metadata"]["actual_generation_seed"], 1000)
            self.assertFalse(result["details"]["full_fresh"])
        self.assertEqual(len(adapter.groups), 4)
        self.assertEqual(len(self.exports), 8)  # Two revisions, four exact profiles.
        self.assertEqual(len(self.receives), 18)
        self.assertEqual(len(adapter.ledger["generation_seed_claims"]), 4)
        # CFO's third capture seed remains forbidden to every confirmation,
        # including a different generation profile.
        with self.assertRaisesRegex(ValueError, "overlaps"):
            adapter.measure_pair("family:state-sampled-smoke", "bad-confirm", 1002,
                                 phase="confirmation")
        result = adapter.measure_pair("family:state-sampled-smoke", "fresh-confirm", 2000,
                                      phase="confirmation")
        self.assertTrue(result["details"]["full_fresh"])

    def test_screen_cannot_reuse_an_earlier_confirmation_seed(self):
        adapter = self.adapter()
        adapter.measure_pair("family:single", "confirm-first", 100, phase="confirmation")
        with self.assertRaisesRegex(ValueError, "overlaps"):
            adapter.measure_pair("family:other", "screen-after", 100)

    def test_bad_scores_count_source_and_changed_regime_are_integrity_failures(self):
        adapter = self.adapter()
        for flag, message in (("bad_scores", "scores disagree"),
                              ("bad_count", "full endpoint"),
                              ("bad_source", "source differs")):
            setattr(self, flag, True)
            with self.assertRaisesRegex(ValueError, message):
                adapter.measure_pair("family:single", flag, 100)
            setattr(self, flag, False)
        adapter.measure_pair("family:single", "valid", 200)
        self.regime = 5
        with self.assertRaisesRegex(ValueError, "regime changed"):
            adapter.measure_pair("family:single", "changed", 300)
        self.assertEqual(len(list(adapter.output.glob("pair-*.json"))), 5)

    def test_environment_change_is_integrity_failure(self):
        adapter = self.adapter()
        self.environment_ok = False
        with self.assertRaisesRegex(ValueError, "environment changed"):
            adapter.measure_pair("family:single", "environment", 100)

    def test_private_allocation_improvement_allowed_but_same_side_drift_rejected(self):
        self.regime_extra = [
            {"resolved": {"states": 2, "state_dtype": "float32", "owned_state_bytes": 256}},
            {"resolved": {"states": 2, "state_dtype": "float32", "owned_state_bytes": 128}},
        ]
        adapter = self.adapter()
        result = adapter.measure_pair("family:single", "screen", 100)
        detail = result["details"]
        self.assertNotEqual(detail["work_signatures"]["reference"], detail["work_signatures"]["candidate"])
        self.assertEqual(detail["comparison_work_signatures"]["reference"],
                         detail["comparison_work_signatures"]["candidate"])
        self.assertEqual(detail["pair"]["candidate"]["receiver_runs"][0]["receiver"]
                         ["regime"]["resolved"]["owned_state_bytes"], 128)
        self.assertEqual(detail["cross_revision_private_allocations"]["candidate"],
                         [{"owned_state_bytes": 128}])
        adapter.measure_pair("family:single", "fresh", 1000, phase="confirmation")
        self.regime_extra[1]["resolved"]["owned_state_bytes"] = 64
        with self.assertRaisesRegex(ValueError, "regime changed"):
            adapter.measure_pair("family:single", "drift", 2000, phase="confirmation")

    def test_working_estimate_only_is_relaxed_not_public_regime_or_numerics(self):
        self.regime_extra = [
            {"resolved": {"states": 2, "state_dtype": "float32", "working_bytes_bound": 2048},
             "device": "cpu", "memory_limit_bytes": 8192, "family": "gaussian-state"},
            {"resolved": {"states": 2, "state_dtype": "float32", "working_bytes_bound": 1024},
             "device": "cpu", "memory_limit_bytes": 8192, "family": "gaussian-state"},
        ]
        adapter = self.adapter()
        adapter.measure_pair("family:single", "valid", 100)
        for field, value in (("device", "cuda"), ("memory_limit_bytes", 16384),
                              ("family", "phase-optimized"), ("loader_tile_blocks", 1)):
            original = deepcopy(self.regime_extra[1])
            self.regime_extra[1][field] = value
            with self.assertRaisesRegex(ValueError, "regimes differ"):
                adapter.measure_pair("family:single", field, 100)
            self.regime_extra[1] = original
        for field, value in (("states", 3), ("state_dtype", "float64")):
            original = deepcopy(self.regime_extra[1])
            self.regime_extra[1]["resolved"][field] = value
            with self.assertRaisesRegex(ValueError, "regimes differ"):
                adapter.measure_pair("family:single", field, 100)
            self.regime_extra[1] = original
        self.bad_scores = True
        with self.assertRaisesRegex(ValueError, "scores disagree"):
            adapter.measure_pair("family:single", "numerics", 100)

    def test_allocation_metadata_validation_and_other_family_regime_stays_strict(self):
        adapter = self.adapter()
        self.regime_extra = [{"resolved": {"owned_state_bytes": 256}},
                             {"resolved": {"owned_state_bytes": 128}}]
        result = adapter.measure_pair("family:single", "valid", 100)
        for index, invalid in enumerate((-1, float("nan"), 1.5, True)):
            self.regime_extra[1]["resolved"]["owned_state_bytes"] = invalid
            # Non-finite JSON is rejected at the raw receipt boundary; the
            # remaining malformed values fail the explicit allocation check.
            with self.assertRaises(ValueError):
                adapter.measure_pair("family:single", f"invalid-{index}", 100)
        self.regime_extra[1]["resolved"] = {}
        with self.assertRaisesRegex(ValueError, "different presence"):
            adapter.measure_pair("family:single", "missing", 100)
        other_pair, ignored = guard.comparison_pair(result["details"]["pair"], {"family": "rbpf"})
        self.assertEqual(ignored, {"reference": [], "candidate": []})
        with self.assertRaisesRegex(ValueError, "regimes differ"):
            guard.numerical_agreement(other_pair, "fp32")

    def test_library_enumeration_order_is_not_a_regime_change(self):
        a = {"filepath": "/numpy/libblas.so", "version": "0.3.30", "num_threads": 2,
             "internal_api": "openblas", "user_api": "blas"}
        b = {"filepath": "/scipy/libblas.so", "version": "0.3.29", "num_threads": 2,
             "internal_api": "openblas", "user_api": "blas"}
        self.regime_extra = [deepcopy({"threads": {"library_pools": [a, b]}}),
                             deepcopy({"threads": {"library_pools": [b, a]}})]
        adapter = self.adapter()
        result = adapter.measure_pair("family:single", "different-order", 100)
        detail = result["details"]
        self.assertNotEqual(detail["work_signatures"]["reference"], detail["work_signatures"]["candidate"])
        self.assertEqual(detail["frozen_work_signatures"]["reference"],
                         detail["frozen_work_signatures"]["candidate"])
        # Original order remains in the raw receipt, even after comparison.
        raw = detail["pair"]["candidate"]["receiver_runs"][0]["receiver"]
        self.assertEqual(raw["regime"]["threads"]["library_pools"], [b, a])
        self.regime_extra[0]["threads"]["library_pools"].reverse()
        self.regime_extra[1]["threads"]["library_pools"].reverse()
        adapter.measure_pair("family:single", "same-side-reorder", 1000, phase="confirmation")
        for field, value in (("num_threads", 3), ("version", "0.3.31"),
                              ("filepath", "/different/libblas.so")):
            original = deepcopy(self.regime_extra[1])
            self.regime_extra[1]["threads"]["library_pools"][0][field] = value
            with self.assertRaisesRegex(ValueError, "regimes differ"):
                adapter.measure_pair("family:single", field, 100)
            self.regime_extra[1] = original
        self.regime_extra[1]["threads"]["library_pools"].append(deepcopy(a))
        with self.assertRaisesRegex(ValueError, "regimes differ"):
            adapter.measure_pair("family:single", "duplicate-entry", 100)

    def test_same_side_library_metadata_drift_is_still_rejected(self):
        pool = {"filepath": "/libblas.so", "version": "0.3.30", "num_threads": 2}
        self.regime_extra = [deepcopy({"threads": {"library_pools": [pool]}}) for _ in range(2)]
        adapter = self.adapter()
        adapter.measure_pair("family:single", "initial", 100)
        for side in self.regime_extra:
            side["threads"]["library_pools"][0]["version"] = "0.3.31"
        # Cross-revision parity alone passes, but the frozen same-side regime
        # rejects an actual environment change during the guard.
        with self.assertRaisesRegex(ValueError, "regime changed"):
            adapter.measure_pair("family:single", "version-drift", 1000, phase="confirmation")

    def test_native_binary_change_detected_before_any_work(self):
        adapter = self.adapter()
        self.context["binary"].write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "binary changed"):
            adapter.measure_pair("family:single", "changed", 100)
        self.assertFalse(self.workers or self.exports)
        with self.assertRaisesRegex(ValueError, "binary changed"):
            adapter.close()
        self.assertTrue(adapter.closed)

    def test_close_protocol_failure_is_retained_after_cleanup(self):
        adapter=self.adapter();worker=Mock();alive={"value":True}
        worker.process.pid=123
        worker.process.poll.side_effect=lambda:None if alive["value"] else 0
        worker.request.side_effect=RuntimeError("close protocol rejected")
        worker.process.stdin.closed=False;worker.process.stdout.closed=False;worker.log.closed=False
        worker.reader.is_alive.return_value=False
        def close():
            alive["value"]=False
            worker.process.stdin.closed=True;worker.process.stdout.closed=True;worker.log.closed=True
        worker.close.side_effect=close
        adapter.workers={"reference":worker};adapter._worker_context={"id":"broken-protocol"}
        adapter.ledger["worker_contexts"].append({"id":"broken-protocol"})
        with self.assertRaisesRegex(RuntimeError,"close protocol failure"):
            adapter._close_worker_context("test")
        self.assertFalse(alive["value"])
        self.assertFalse(adapter.workers)
        record=adapter.ledger["worker_contexts"][-1]["teardown"]["workers"]["reference"]
        self.assertTrue(record["cleanup_verified"])
        self.assertIn("close protocol rejected",record["close_protocol_error"])

    def test_close_ack_timeout_is_owned_reaping_not_functional_failure(self):
        adapter=self.adapter();worker=Mock();alive={"value":True}
        worker.process.pid=123
        worker.process.poll.side_effect=lambda:None if alive["value"] else -15
        worker.request.return_value={"closed":True}
        worker.process.wait.side_effect=subprocess.TimeoutExpired("worker",2.)
        worker.process.stdin.closed=False;worker.process.stdout.closed=False;worker.log.closed=False
        worker.reader.is_alive.return_value=False
        def close():
            self.assertLess(worker.deadline,time.monotonic())
            alive["value"]=False
            worker.process.stdin.closed=True;worker.process.stdout.closed=True;worker.log.closed=True
        worker.close.side_effect=close
        adapter.workers={"reference":worker};adapter._worker_context={"id":"slow-shutdown"}
        adapter.ledger["worker_contexts"].append({"id":"slow-shutdown"})
        result=adapter._close_worker_context("test")
        self.assertFalse(result["cleanup_errors"])
        self.assertEqual(result["workers"]["reference"]["cleanup_mode"],"owned_forced_after_timeout")
        self.assertTrue(result["workers"]["reference"]["cleanup_verified"])
        self.assertEqual(worker.request.call_count,1)

    def test_shared_fixture_mutation_detected_and_workers_closed(self):
        adapter = self.adapter()
        adapter.measure_pair("family:single", "screen", 100)
        path = adapter.groups["shared"]["paths"]["candidate"][0] / "data.bin"
        path.write_bytes(b"corrupted")
        with self.assertRaisesRegex(ValueError, "fixture changed"):
            adapter.close()
        self.assertTrue(all(worker.closed for worker in self.workers))
        self.assertTrue(adapter.closed)

    def test_deadline_has_no_generation_or_worker_side_effect(self):
        self.context["deadline"] = time.monotonic() - 1
        adapter = self.adapter()
        with self.assertRaises(TimeoutError):
            adapter.measure_pair("family:single", "expired", 100)
        self.assertFalse(self.workers or self.exports)

    def test_native_timeout_translated_and_can_close(self):
        import subprocess
        adapter = self.adapter()
        with patch.object(guard.sample, "export_fixtures",
                          side_effect=subprocess.TimeoutExpired("native", 1)):
            with self.assertRaises(TimeoutError):
                adapter.measure_pair("family:single", "timeout", 100)
        adapter.close()
        self.assertTrue(all(worker.closed for worker in self.workers))

    def test_invalid_inputs_are_rejected_without_work(self):
        adapter = self.adapter()
        for kwargs in ({"case_id": "missing"}, {"phase": "approximate"},
                       {"order": ("candidate", "candidate")}, {"run_id": ""}):
            arguments = {"case_id": "family:single", "run_id": "invalid", "seed": 10, **kwargs}
            with self.assertRaises(ValueError):
                adapter.measure_pair(**arguments)
        self.assertFalse(self.exports or self.workers)


if __name__ == "__main__":
    unittest.main()
