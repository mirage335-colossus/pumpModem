"""Deterministic admission tests. These fixtures do not execute CUDA."""
from copy import deepcopy
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import triad_gpu_workload as workload


def resources():
    return dict(hardware_threads=4, workers=8, slot_owners=8, memory_limit_bytes=1 << 30,
                receiver_numerical_threads_per_worker=1)


def bulk():
    return dict(device="cuda:0", device_name="fixture-device", library_version="fixture-library",
                cuda=True, cuda_driver_version=12500, cuda_runtime_version=12400, cuda_blas_version=12400,
                cuda_compute_major=12, cuda_compute_minor=0, numerical_threads=1,
                workspace_limit_bytes=1 << 26, runtime_headroom_bytes=1 << 25,
                initial_free_device_bytes=1 << 28, total_device_bytes=1 << 29,
                fft_plan_cache_count=2, fft_plan_cache_limit=4,
                precision="fp32_bulk_fp64_coordinates_and_reductions", requested_coarse_math="fft", executed=True,
                synchronizations=11, upload_bytes=12, download_bytes=13, fft_calls=14, dc_reduction_values=0,
                score_rows=60, cuda_arrival_peak_rows=0, cuda_arrival_peak_values=0,
                fir_output_contributions=16, statistics_blocks=17,
                mapped_values=18, projected_values=19, direct_columns=0, direct_fallback_counts={"other": 0},
                peak_reserved_bytes=100, allocator_peak_live_bytes=75, allocator_peak_reserved_bytes=110)


def make_phase(name, work, *, seconds=1, wall=.1):
    return dict(name=name, work=work, represented_seconds=seconds, wall_seconds=wall,
                process_cpu_seconds=.1, scope="receiver")


def slot_receipt(backend="cuda", *, command="continuation", stratum="active"):
    accelerator = bulk() if backend == "cuda" else None
    coarse = dict.fromkeys(workload.COARSE_GEOMETRY, 1)
    coordinate = dict(epoch_offset_seconds=0, arrival_seconds=0, frequency_hz=0, clock_ppm=0)
    coarse.update(planned_tiles=4, completed_tiles=4, bulk=deepcopy(accelerator),
                  route="frequency_tiles", workspaces=3, frequency_points=5, arrival_points=4,
                  search=dict(workspace_frequency_pairs=15, score_rows=60),
                  selection=dict(retained_peaks=2, selection_peak_rows=2), proposals=1, refined_coordinates=1,
                  refined_coordinates_detail=[coordinate],
                  proposals_detail=[dict(coordinate=coordinate, bit=0, rank=0, score=2.)])
    lane = dict.fromkeys(workload.LANE_CAPS + workload.LANE_COUNTS, 20)
    lane.update(states_sha256="2" * 64, bit_scores=[1., 0.])
    fine = dict.fromkeys(workload.FINE_GEOMETRY, 1)
    fine.update(observation_complete=True, representative_work=True, input_sha256="3" * 64,
                observation_first_sample=0, quiet_modes=10, initial_affine=lane, adaptive=deepcopy(lane),
                cache=dict.fromkeys(workload.CACHE_GEOMETRY + workload.CACHE_WORK, 10),
                bulk=accelerator)
    fine["cache"]["fallback_reason"] = ""
    phases = [make_phase("coarse_prepare", {"frequency_grid_sha256": "1" * 64})]
    phases += [make_phase("coarse_tiles", dict(step_tiles=step, completed_tiles=done, planned_tiles=4))
               for step, done in ((1, 1), (1, 2), (2, 4))]
    phases.append(make_phase("coarse_selection", coarse))
    if command == "continuation" and stratum in ("active", "noise"):
        phases.append(make_phase("fine_prefix", fine | {"checkpoint_seconds": 2}, seconds=2))
        phases += [make_phase("fine_continuation", fine | {"checkpoint_seconds": seconds}, seconds=1)
                   for seconds in (3, 4, 6)]
    else:
        phases.append(make_phase("fine_work", fine))
    phases.append(make_phase("fine_finish", fine))
    score = dict(decision="data" if command == "slot" else "ambiguous", bit=0 if command == "slot" else None,
                 complete=command == "slot", all_valid=True, zero_h0_verified=True, quiet_modes=10,
                 operational_passes_conditional_bound=True, operational_log_e=20., operational_threshold=21.)
    score.update({key: [1., 0.] for key in
                  ("bit_evidence", "adaptive_bit_evidence", "combined_bit_evidence")})
    return dict(schema="datapump.triad.native-probe.v1", command=command, status="complete",
                configuration=dict(receiver_slot_workspace_bytes=10 if accelerator else 5,
                                   actual_cn0_db_hz=55., target_cn0_db_hz=30.),
                allocation=resources(), observation=dict(input_sha256="4" * 64, seed=123, public_slot=0,
                                                           pattern_epoch=12, symbol_ordinal=23),
                identity=dict(backend=backend, requested_tensor_coarse_math="fft", source_manifest_sha256="1" * 64), phases=phases,
                result=dict(work_complete=True, checkpoints_complete=True, continued_cache_fill=True,
                            coordinate_scope="complete_public_selection",
                            maximum_state_transition_covered=True, explicit_erasure_covered=True,
                            receiver_decision_eligible=command == "slot",
                            evaluator_expected_decision_observed=True, stratum=stratum, score=score))


class NativeWorkloadTests(unittest.TestCase):
    def test_cuda_only_accepts_actual_device_work_and_bounded_allocator(self):
        work = slot_receipt()["phases"][-1]["work"]
        work["bulk"]["fft_calls"] = 0
        work["bulk"]["dc_reduction_values"] = 9
        self.assertEqual(workload.accelerator_stage(work, "fine")[1], workload.PRECISION[1])
        work["bulk"]["dc_reduction_values"] = 0
        with self.assertRaisesRegex(workload.ProbeError, "FFT or explicit DC"):
            workload.accelerator_stage(work, "fine")
        work["bulk"]["fft_calls"] = 1
        work["bulk"]["allocator_peak_reserved_bytes"] = work["bulk"]["workspace_limit_bytes"] + 1
        with self.assertRaisesRegex(workload.ProbeError, "memory exceeded"):
            workload.accelerator_stage(work, "fine")
        work["bulk"].update(allocator_peak_reserved_bytes=100, cuda=False, device="cpu")
        with self.assertRaisesRegex(workload.ProbeError, "CUDA device"):
            workload.accelerator_stage(work, "fine")

    def test_cuda_runtime_binds_blas_and_hardware_but_not_work_shape(self):
        receipt = bulk()
        expected = workload.cuda_runtime(receipt)
        for field, value in (("cuda_blas_version", 12804), ("cuda_compute_major", 9),
                             ("cuda_compute_minor", 1)):
            changed = receipt | {field: value}
            self.assertNotEqual(workload.cuda_runtime(changed), expected)
        changed = receipt | dict(cuda_matrix_maximum_rows=4096, cuda_matrix_maximum_columns=256,
                                 cuda_matrix_maximum_inner=512)
        self.assertEqual(workload.cuda_runtime(changed), expected)
        for field in ("cuda_blas_version", "cuda_compute_major", "cuda_compute_minor"):
            changed = deepcopy(receipt)
            del changed[field]
            with self.subTest(field=field), self.assertRaises(workload.ProbeError):
                workload.cuda_runtime(changed)

    def test_each_cuda_stage_requires_its_own_requested_policy_receipt(self):
        for phase_name in ("coarse_selection", "fine_finish"):
            for field_state in ("missing", "different"):
                receipt = slot_receipt()
                work = next(phase["work"] for phase in receipt["phases"] if phase["name"] == phase_name)
                work["bulk"] = deepcopy(work["bulk"])
                if field_state == "missing":
                    del work["bulk"]["requested_coarse_math"]
                else:
                    work["bulk"]["requested_coarse_math"] = "int8"
                with self.subTest(phase=phase_name, field_state=field_state), self.assertRaises(workload.ProbeError):
                    workload.validate_work(receipt, "cuda", "active")
        receipt = slot_receipt()
        receipt["identity"]["requested_tensor_coarse_math"] = "int8"
        with self.assertRaisesRegex(workload.ProbeError, "differs from the requested"):
            workload.validate_work(receipt, "cuda", "active")
        # A shared device/runtime does not authorize borrowing another backend's
        # requested arithmetic policy. Consistent explicit annotations do pass.
        for phase in receipt["phases"]:
            if phase["work"].get("bulk") is not None:
                phase["work"]["bulk"]["requested_coarse_math"] = "int8"
        workload.validate_work(receipt, "cuda", "active")

    def test_cuda_arrival_reduction_requires_complete_logical_grid(self):
        original = slot_receipt()
        coarse = next(phase["work"] for phase in original["phases"] if phase["name"] == "coarse_selection")
        coarse["bulk"].update(cuda_arrival_peak_rows=15, cuda_arrival_peak_values=60)
        workload.validate_work(original, "cuda", "active")
        for field, value in (("cuda_arrival_peak_rows", 14), ("cuda_arrival_peak_values", 59),
                             ("cuda_arrival_peak_rows", 0), ("cuda_arrival_peak_values", -1),
                             ("cuda_arrival_peak_values", True), ("score_rows", 59)):
            receipt = deepcopy(original)
            work = next(phase["work"] for phase in receipt["phases"] if phase["name"] == "coarse_selection")
            work["bulk"][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(workload.ProbeError):
                workload.validate_work(receipt, "cuda", "active")
        for field, value in (("arrival_points", 5), ("workspaces", 4), ("route", "lag_groups")):
            receipt = deepcopy(original)
            work = next(phase["work"] for phase in receipt["phases"] if phase["name"] == "coarse_selection")
            work[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(workload.ProbeError):
                workload.validate_work(receipt, "cuda", "active")
        # Full-score fallback is still valid with both reduction counters zero.
        workload.validate_work(slot_receipt(), "cuda", "active")

    def test_exact_arrival_reduction_maps_unchanged_logical_cpu_demand(self):
        cpu, cuda = slot_receipt("cpu"), slot_receipt()
        coarse = next(phase["work"] for phase in cuda["phases"] if phase["name"] == "coarse_selection")
        coarse["bulk"].update(cuda_arrival_peak_rows=15, cuda_arrival_peak_values=60)
        comparison = workload.compare_work(cpu, cuda, "active")
        self.assertTrue(comparison["numerical_parity_accepted"])
        self.assertTrue(comparison["acquisition_match"])
        self.assertTrue(comparison["accepted"])
        self.assertTrue(comparison["cpu_cost_work_match"])
        self.assertTrue(comparison["exact_arrival_cpu_demand_mapping"])
        self.assertEqual(comparison["cpu_cost_unqualified_leaves"], [])
        cpu_coarse = next(phase["work"] for phase in cpu["phases"] if phase["name"] == "coarse_selection")
        cpu_coarse["search"]["score_rows"] = 59
        comparison = workload.compare_work(cpu, cuda, "active")
        self.assertFalse(comparison["acquisition_match"])
        self.assertFalse(comparison["numerical_parity_accepted"])

    def test_complete_arrival_grid_does_not_hide_changed_selection_or_source(self):
        for change in ("retained_peaks", "selection_peak_rows", "proposals", "source", "requested_math"):
            cpu, cuda = slot_receipt("cpu"), slot_receipt()
            coarse = next(phase["work"] for phase in cuda["phases"] if phase["name"] == "coarse_selection")
            coarse["bulk"].update(cuda_arrival_peak_rows=15, cuda_arrival_peak_values=60)
            if change in ("retained_peaks", "selection_peak_rows"):
                coarse["selection"][change] += 1
            elif change == "proposals":
                coarse[change] += 1
            elif change == "source":
                cuda["identity"]["source_manifest_sha256"] = "2" * 64
            else:
                cuda["identity"]["requested_tensor_coarse_math"] = "int8"
                for phase in cuda["phases"]:
                    if phase["work"].get("bulk") is not None:
                        phase["work"]["bulk"]["requested_coarse_math"] = "int8"
            comparison = workload.compare_work(cpu, cuda, "active")
            with self.subTest(change=change):
                self.assertFalse(comparison["accepted"])
                self.assertFalse(comparison["cpu_cost_work_match"])
                self.assertFalse(comparison["exact_arrival_cpu_demand_mapping"])

    def test_cuda_direct_native_fallback_cannot_qualify_mapped_path(self):
        work = slot_receipt()["phases"][-1]["work"]
        work["bulk"]["direct_fallback_counts"]["other"] = 1
        with self.assertRaisesRegex(workload.ProbeError, "direct rendering fallback"):
            workload.accelerator_stage(work, "fine")

    def test_paired_same_input_work_and_scores(self):
        cpu, cuda = slot_receipt("cpu"), slot_receipt()
        self.assertTrue(workload.compare_work(cpu, cuda, "active")["accepted"])
        cuda["observation"]["input_sha256"] = "b" * 64
        self.assertFalse(workload.compare_work(cpu, cuda, "active")["same_input"])
        cuda["observation"]["input_sha256"] = cpu["observation"]["input_sha256"]
        cuda["phases"][-1]["work"]["adaptive"]["transition_edges"] = 22
        self.assertGreater(workload.compare_work(cpu, cuda, "active")["maximum_relative_adaptive_cost_drift"], .05)

    def test_matrix_parity_does_not_qualify_fft_cpu_cost_forecast(self):
        for mode in ("fp16", "int8"):
            with self.subTest(mode=mode):
                cpu, cuda = slot_receipt("cpu"), slot_receipt()
                cuda["identity"]["requested_tensor_coarse_math"] = mode
                for phase in cuda["phases"]:
                    accelerator = phase["work"].get("bulk")
                    if accelerator is not None:
                        accelerator["requested_coarse_math"] = mode
                        accelerator[f"cuda_matrix_{mode}_calls"] = 2
                comparison = workload.compare_work(cpu, cuda, "active")
                self.assertTrue(comparison["numerical_parity_accepted"])
                self.assertFalse(comparison["cpu_cost_work_match"])
                self.assertFalse(comparison["accepted"])

    def test_gpu_cli_propagates_coarse_arithmetic(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(workload, "run_probe") as run:
            run.return_value = {"status": "bounded_native_work_observed"}
            self.assertEqual(workload.main(["--output", str(Path(directory) / "result.json"),
                                            "--tensor-coarse-math", "int8"]), 0)
            self.assertEqual(run.call_args.kwargs["tensor_coarse_math"], "int8")
        with self.assertRaisesRegex(workload.ProbeError, "requires CUDA"):
            workload.run_probe(case_indices=[1], output="unused.json", device="cpu", tensor_coarse_math="int8")

    def test_paired_threshold_challenge_checks_observation_regime(self):
        cpu, cuda = (slot_receipt(backend, command="slot") for backend in ("cpu", "cuda"))
        self.assertTrue(workload.reference_challenge(cpu, cuda, "near-decision")["accepted"])
        self.assertTrue(workload.reference_challenge(cpu, cuda, "high-snr")["accepted"])
        cpu["result"]["score"]["operational_log_e"] = 100
        self.assertFalse(workload.reference_challenge(cpu, cuda, "near-decision")["accepted"])

    def test_partial_coarse_or_missing_fine_checkpoint_rejected(self):
        receipt = slot_receipt()
        receipt["phases"][2]["work"]["completed_tiles"] = 1
        with self.assertRaisesRegex(workload.ProbeError, "not consecutive"):
            workload.validate_work(receipt, "cuda", "active")
        receipt = slot_receipt()
        receipt["phases"] = [p for p in receipt["phases"] if p["name"] != "fine_continuation"]
        with self.assertRaisesRegex(workload.ProbeError, "persistent"):
            workload.validate_work(receipt, "cuda", "active")

    def test_terminal_work_only_credits_cuda_statistics_without_claiming_acquisition(self):
        receipt = slot_receipt(stratum="terminal")
        receipt["phases"] = [p for p in receipt["phases"] if p["name"].startswith("fine_")]
        receipt["observation"]["channel_scope"] = "independently_seeded_native_thermal_noise_at_local_crop"
        receipt["result"].update(coordinate_scope="declared_public_work_coordinate", complete_receiver_service=False)
        fine = receipt["phases"][-1]["work"]
        fine["representative_work"] = False
        fine["bulk"]["mapped_values"] = 0
        precision, _ = workload.validate_work(receipt, "cuda", "terminal", declared=True)
        self.assertEqual(precision, (None, workload.PRECISION[1]))
        with self.assertRaisesRegex(workload.ProbeError, "complete public selection"):
            workload.validate_work(receipt, "cuda", "terminal")
        fine["bulk"]["statistics_blocks"] = 0
        with self.assertRaisesRegex(workload.ProbeError, "statistics_blocks"):
            workload.validate_work(receipt, "cuda", "terminal", declared=True)

    def test_unavailable_device_writes_receipt_and_stops_before_case_plan(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(workload, "NativeProbe") as native:
            probe = native.return_value
            probe.call.return_value = {"status": "unavailable", "reason": "No native CUDA device"}
            report = workload.run_probe(case_indices=[1], output=Path(temp) / "trace.json")
            self.assertEqual(report["status"], "unresolved")
            self.assertEqual(report["reason"], "No native CUDA device")
            self.assertEqual(report["cases"], [])
            self.assertEqual(probe.call.call_count, 1)


if __name__ == "__main__":
    unittest.main()
