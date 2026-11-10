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
                cuda=True, cuda_driver_version=12500, cuda_runtime_version=12400, numerical_threads=1,
                workspace_limit_bytes=1 << 26, runtime_headroom_bytes=1 << 25,
                initial_free_device_bytes=1 << 28, total_device_bytes=1 << 29,
                fft_plan_cache_count=2, fft_plan_cache_limit=4,
                precision="fp32_bulk_fp64_coordinates_and_reductions", executed=True,
                synchronizations=11, upload_bytes=12, download_bytes=13, fft_calls=14, dc_reduction_values=0,
                score_rows=15, fir_output_contributions=16, statistics_blocks=17,
                mapped_values=18, projected_values=19, direct_columns=0, direct_fallback_counts={"other": 0},
                peak_reserved_bytes=100, allocator_peak_live_bytes=75, allocator_peak_reserved_bytes=110)


def make_phase(name, work, *, seconds=1, wall=.1):
    return dict(name=name, work=work, represented_seconds=seconds, wall_seconds=wall,
                process_cpu_seconds=.1, scope="receiver")


def slot_receipt(backend="cuda", *, command="continuation", stratum="active"):
    accelerator = bulk() if backend == "cuda" else None
    coarse = dict.fromkeys(workload.COARSE_GEOMETRY, 1)
    coordinate = dict(epoch_offset_seconds=0, arrival_seconds=0, frequency_hz=0, clock_ppm=0)
    coarse.update(planned_tiles=4, completed_tiles=4, bulk=accelerator,
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
                identity=dict(backend=backend), phases=phases,
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
