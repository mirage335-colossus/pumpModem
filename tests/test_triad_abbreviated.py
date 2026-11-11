"""Cheap inference and refusal checks; no performance campaign is executed."""
from pathlib import Path
import copy
import contextlib
import io
import json
import subprocess
import tempfile
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
import triad_abbreviated as abbreviated
from triad_abbreviated import (analyze, log_interval, policy, validate_baseline, stream_unit,
                              stream_batch, regression_batch_list, KERNELS, SCHEMA)
from triad_native import NativeProbe, ProbeError, runtime_identity, validate_receipt


def device_receipt():
    return {"schema": "datapump.triad.native-probe.v1", "command": "continuation", "status": "complete",
            "configuration": {}, "budget": {}, "observation": {"input_sha256": "1" * 64},
            "allocation": {"hardware_threads": 2, "workers": 4, "slot_owners": 4,
                           "memory_limit_bytes": 2**30, "receiver_numerical_threads_per_worker": 1},
            "identity": {
                "backend": "cuda", "implementation": "native-production-receiver",
                "source_manifest_sha256": "2" * 64, "build_options_sha256": "3" * 64, "compiler": "GNU-14",
                "build_config": "Release", "configured_tensor_backend": True,
                "configured_vulkan_backend": False, "require_accelerator": True,
                "receiver_arithmetic": "native_fp64_state_and_admission", "requested_tensor_precision": "automatic",
                "requested_tensor_coarse_math": "fft",
                "backend_receipt": {"device": "cuda:0", "device_name": "Fixture GPU",
                                    "library_version": "2.15", "cuda_runtime_version": 13000,
                                    "cuda_driver_version": 13000, "cuda_blas_version": 12804,
                                    "cuda_compute_major": 12, "cuda_compute_minor": 0,
                                    "cuda": True, "numerical_threads": 1,
                                    "precision": "automatic", "requested_coarse_math": "fft", "executed": True, "mapped_values": 0,
                                    "statistics_blocks": 4, "fft_calls": 0, "dc_reduction_values": 12,
                                    "score_rows": 0}},
            "phases": [{"name": "fine_work", "work": {}, "wall_seconds": .01, "represented_seconds": 1}],
            "result": {"coordinate_scope": "declared_public_work_coordinate", "stratum": "terminal"}}


def cpu_receipt(command):
    result = device_receipt()
    result.update(command=command, configuration={}, observation=None, phases=[], result={})
    result["identity"].update(backend="cpu", configured_tensor_backend=False,
                              configured_vulkan_backend=False, require_accelerator=False, backend_receipt=None,
                              executable_sha256="4" * 64)
    return result


def vulkan_bulk_receipt():
    return {"device": "vulkan:0", "device_name": "AMD Radeon Graphics",
            "library_version": "Vulkan 1.4 / Mesa", "precision": "fp32",
            "vulkan": True, "cuda": False, "numerical_threads": 1,
            "vulkan_api_version": 1, "vulkan_driver_version": 1,
            "vulkan_vendor_id": 4098, "vulkan_device_id": 5688,
            "vulkan_subgroup_size": 64, "vulkan_float16": True,
            "vulkan_int8": True, "vulkan_integer_dot8_accelerated": False,
            "unified_memory": True, "workspace_scope": "per_coarse_session",
            "workspace_limit_bytes": 1 << 29, "vulkan_peak_owner_bytes": 1 << 28,
            "vulkan_active_sessions": 2, "reserved_bytes": 1 << 30,
            "allocator_peak_reserved_bytes": 1 << 30,
            "host_cache_limit_bytes": 8 << 20, "host_cache_bytes": 2 << 20,
            "host_cache_entries": 2, "vulkan_fft_cache_hits": 1, "vulkan_fft_cache_misses": 1,
            "total_device_bytes": 6 << 30, "executed": True,
            "vulkan_dispatches": 12, "vulkan_arrival_peak_rows": 4,
            "fft_calls": 4, "score_rows": 8}


def message_receipt():
    result = cpu_receipt("message")
    resources = result["allocation"]
    origin, ordinal, rows = 1793850000, 7000037, 500
    result["configuration"] = {"calendar_epoch": origin, "initial_symbol_ordinal": ordinal,
                               "sample_rate_hz": 1500}
    result["observation"] = {"input_sha256": "1" * 64, "first_sample": 0, "stop_sample": rows,
                             "samples": rows, "generated_samples": rows, "channel_erased_rows": 0,
                             "expected_episodes": 1, "wire_bits_per_episode": 3,
                             "public_slot": 0, "pattern_epoch": origin, "symbol_ordinal": ordinal,
                             "spool_residency_reserved_bytes": 100}
    slots = []
    for index in range(5):
        slots.append({"index": index, "first_sample": 100 * index, "stop_sample": 100 * (index + 1),
                      "pattern_epoch": origin + 60 * index, "symbol_ordinal": ordinal + index,
                      "complete": True, "all_valid": True, "zero_h0_verified": True,
                      "decision": "data" if index < 3 else "quiet",
                      "bit": (0, 0, 1)[index] if index < 3 else None, "workers": 2,
                      "coarse_workspaces": 13, "coarse_frequency_points": 1, "coarse_arrival_points": 3,
                      "fine_candidates": 2, "affine_blocks": 2, "adaptive_blocks": 2,
                      "adaptive_peak_states": 2, "adaptive_transition_edges": 0,
                      "basis_rows": 100, "quiet_modes": 5, "commit": {"input_stop": rows}})
    stream = {"state": "input_ended", "input_closed": True, "work_complete": True,
              "input_rows": rows, "committed_slots": 5, "next_public_slot": 5,
              "verified_input_sha256": "1" * 64, "slots": slots,
              "messages": [{"first_public_slot": 0, "stop_public_slot": 5, "physical_complete": True,
                            "expected_source_matches": True, "long_form": False, "observed_bits": 3,
                            "missing_slots": 0, "trailing_quiet_slots": 2}],
              "spool_residency_reserved_bytes": 100, "receiver_peak_reserved_bytes": 10000,
              "receiver_memory_limit_bytes": resources["memory_limit_bytes"] - 100,
              "total_application_memory_limit_bytes": resources["memory_limit_bytes"],
              "peak_reserved_bytes": 10100, "peak_active_owners": 2,
              "peak_running_owners": 2, "peak_executor_workers": 4, "wall_seconds": .125}
    result["result"] = {"stream": stream, "expected_message_count": 1,
                        "complete_receiver_service": True, "message_and_physical_quiet_verified": True,
                        "eof_control_verified": True, "scope": "configured_public_iq_stream"}
    result["phases"] = [
        {"name": "simulator", "wall_seconds": .75, "represented_seconds": rows / 1500, "work": {}},
        {"name": "receiver_service", "unit_index": 0, "wall_seconds": .125,
         "represented_seconds": rows / 1500,
         "scope": "native_iq_replay_including_bounded_spool_reads_and_verification",
         "work": {"committed_slots": 5, "input_rows": rows, "verified_input_sha256": "1" * 64,
                  "allocation": copy.deepcopy(resources)}},
        {"name": "eof_control", "wall_seconds": .025, "represented_seconds": 100 / 1500,
         "work": {"work_complete": True, "committed_slots": 1, "messages": [], "slots": [slots[0]]}},
    ]
    return result


class FixtureProbe:
    calls = []

    def __init__(self, executable, *, backend):
        assert backend == "cpu"

    def call(self, command, **options):
        self.calls.append((command, options))
        if command == "describe":
            return cpu_receipt(command)
        if command == "message":
            assert options["slots"] == 5 and options["max_samples"] <= 600000
            return message_receipt()
        result = cpu_receipt(command)
        units, stratum = options["units"], options["stratum"]
        result["observation"] = {"input_sha256": "5" * 64, "samples": 100}
        result["result"] = {"units": units, "stratum": stratum, "work_complete": True,
                            "evaluator_expected_decision_observed": True}
        for index in range(units):
            result["phases"].extend((
                {"name": "native_slot", "unit_index": index, "wall_seconds": .125, "represented_seconds": 1,
                 "work": {"work_identity_sha256": "6" * 64, "complete": True,
                          "decision": "quiet" if stratum == "noise" else "data",
                          "coarse_seconds": .03125, "fine_seconds": .0625}},
                {"name": "audio_frontend", "unit_index": index, "wall_seconds": .015625,
                 "represented_seconds": 1, "work": {"input_rows": 100, "output_rows": 100}}))
        return result

    def verify_unchanged(self):
        pass


class AbbreviatedTests(unittest.TestCase):
    def test_exact_additive_work_and_large_projection_execute_no_target(self):
        result = analyze([[.01, .02, .04]]*3, 10**9)
        self.assertEqual(result["model_status"], "abbreviated_holdout_supported")
        self.assertEqual(result["measured_kernel_units"], 21)
        self.assertAlmostEqual(result["measured_seconds"], .21)
        self.assertAlmostEqual(result["projected_seconds"], 10**7)
        self.assertEqual(result["comparison"], "uncompared")
        self.assertTrue(result["stop"])

    def test_holdout_detects_nonadditive_work_and_refuses_projection(self):
        result = analyze([[.01, .02, .08]]*3, 1000)
        self.assertEqual(result["model_status"], "unresolved")
        self.assertIsNone(result["projected_seconds"])
        self.assertFalse(result["stop"])
        final = analyze([[.01, .02, .08]]*9, 1000)
        self.assertEqual(final["stop_reason"], "bounded_limit")
        self.assertIsNone(final["projected_seconds"])

    def test_model_error_floor_does_not_shrink_with_more_repetitions(self):
        short = analyze([[.01, .02, .044]]*3, 1000)
        longer = analyze([[.01, .02, .044]]*9, 1000)
        self.assertEqual(short["observed_model_log_error_floor"], longer["observed_model_log_error_floor"])
        self.assertEqual(short["conditional_projection_interval_seconds"], longer["conditional_projection_interval_seconds"])

    def test_material_slowdown_negative_control_and_clear_comparison(self):
        baseline = analyze([[.01, .02, .04]]*3, 1000)
        slow = analyze([[.02, .04, .08]]*3, 1000, baseline=baseline)
        self.assertEqual(slow["comparison"], "material_slowdown")
        same = analyze([[.01, .02, .04]]*3, 1000, baseline=baseline)
        self.assertEqual(same["comparison"], "no_material_slowdown_for_measured_kernels")
        invalid = analyze([[.01, .02, .08]]*3, 1000, baseline=baseline)
        self.assertEqual(invalid["comparison"], "unresolved_model")

    def test_uncertain_timing_cannot_be_reported_as_decision(self):
        baseline = analyze([[.01, .02, .04]]*3, 1000)
        varying = analyze([[.005, .01, .02], [.01, .02, .04], [.025, .05, .1]], 1000, baseline=baseline)
        self.assertEqual(varying["comparison"], "inconclusive")
        self.assertFalse(varying["stop"])

    def test_invalid_baseline_model_fields_cannot_produce_a_verdict(self):
        baseline = analyze([[.01, .02, .04]]*3, 1000)
        for value in (-1, float("nan"), float("inf"), True, "0"):
            with self.subTest(error_floor=value), self.assertRaises(ValueError):
                analyze([[.02, .04, .08]]*3, 1000,
                        baseline={**baseline, "observed_model_log_error_floor": value})
        with self.assertRaises(ValueError):
            analyze([[.02, .04, .08]]*3, 1000,
                    baseline={**baseline, "model_status": "qualified"})

    def test_undeclared_look_bad_target_and_invalid_observations_rejected(self):
        for rows, target in (([[1, 2, 4]]*4, 100), ([[1, 2, 4]]*3, 3),
                             ([[1, 2, 4]]*3, True), ([[0, 2, 4]]*3, 100),
                             ([[1, float("nan"), 4]]*3, 100)):
            with self.assertRaises(ValueError):
                analyze(rows, target)
        with self.assertRaises(ValueError):
            log_interval([1, 1], alpha=.05)

    def test_baseline_must_match_inputs_device_environment_policy_and_coverage(self):
        baseline = {"schema": SCHEMA, "geometry": {"input": "abc"},
                    "environment": {"device": "cpu"}, "policy": policy(),
                    "kernels": {k: {} for k in KERNELS}}
        validate_baseline(baseline, baseline["geometry"], baseline["environment"])
        for geometry, environment in (({"input": "other"}, {"device": "cpu"}),
                                      ({"input": "abc"}, {"device": "cuda"})):
            with self.assertRaises(ValueError):
                validate_baseline(baseline, geometry, environment)
        baseline["kernels"].pop(KERNELS[0])
        with self.assertRaises(ValueError):
            validate_baseline(baseline, baseline["geometry"], baseline["environment"])

    def test_native_invocation_binds_requested_coarse_arithmetic(self):
        receipt = device_receipt()
        receipt.update(command="describe", phases=[], observation=None)
        receipt["identity"]["requested_tensor_coarse_math"] = "int8"
        receipt["identity"]["backend_receipt"]["requested_coarse_math"] = "int8"
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "probe"
            binary.write_bytes(b"\x7fELFfixture")
            probe = NativeProbe(binary, backend="cuda")
            with patch("triad_native.subprocess.run") as execute:
                execute.return_value = subprocess.CompletedProcess([], 0, json.dumps(receipt).encode(), b"")
                probe.call("describe", tensor_coarse_math="int8")
                arguments = execute.call_args.args[0]
                self.assertEqual(arguments[arguments.index("--tensor-coarse-math") + 1], "int8")
                with self.assertRaisesRegex(ProbeError, "altered the requested coarse"):
                    probe.call("describe", tensor_coarse_math="fp16")
        missing = copy.deepcopy(receipt)
        del missing["identity"]["requested_tensor_coarse_math"]
        with self.assertRaisesRegex(ProbeError, "arithmetic identity"):
            validate_receipt(missing, command="describe", backend="cuda", returncode=0)
        receipt["identity"]["backend_receipt"]["requested_coarse_math"] = "fft"
        with self.assertRaisesRegex(ProbeError, "differs from the requested"):
            validate_receipt(receipt, command="describe", backend="cuda", returncode=0)

    def test_regression_cli_propagates_coarse_policy_and_rejects_cpu_use(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(abbreviated, "run") as run:
            run.return_value = {"status": "baseline_established"}
            output = str(Path(directory) / "result.json")
            self.assertEqual(abbreviated.main(["--output", output, "--device", "cuda",
                                              "--tensor-coarse-math", "fp16"]), 0)
            self.assertEqual(run.call_args.kwargs["tensor_coarse_math"], "fp16")
            with self.assertRaises(SystemExit):
                abbreviated.main(["--output", output, "--tensor-coarse-math", "int8"])

    def test_native_runtime_comparison_binds_build_and_nested_device_fields(self):
        original = device_receipt()
        expected = runtime_identity(original)
        for field, value in (("build_config", "Debug"), ("build_options_sha256", "f" * 64),
                             ("requested_tensor_precision", "fp64"), ("requested_tensor_coarse_math", "int8")):
            changed = copy.deepcopy(original)
            changed["identity"][field] = value
            self.assertNotEqual(runtime_identity(changed), expected)
        for field, value in (("library_version", "2.14"), ("device_name", "Other GPU"),
                             ("cuda_driver_version", 12800), ("cuda_blas_version", 12805),
                             ("cuda_compute_major", 10), ("cuda_compute_minor", 1), ("precision", "fp64"), ("requested_coarse_math", "fp16")):
            changed = copy.deepcopy(original)
            changed["identity"]["backend_receipt"][field] = value
            self.assertNotEqual(runtime_identity(changed), expected)
        changed = copy.deepcopy(original)
        changed["identity"]["source_manifest_sha256"] = "f" * 64
        changed["identity"]["backend_receipt"]["dc_reduction_values"] += 8
        changed["identity"]["backend_receipt"]["cuda_matrix_maximum_rows"] = 4096
        changed["identity"]["backend_receipt"]["cuda_matrix_maximum_columns"] = 256
        changed["identity"]["backend_receipt"]["cuda_matrix_maximum_inner"] = 512
        self.assertEqual(runtime_identity(changed), expected)

    def test_terminal_zero_mapping_requires_actual_accelerated_reductions(self):
        receipt = device_receipt()
        validate_receipt(receipt, command="continuation", backend="cuda", returncode=0)
        for change in ("wrong_scope", "no_projection", "wrong_stratum", "fallback"):
            wrong = copy.deepcopy(receipt)
            if change == "wrong_scope":
                wrong["result"]["coordinate_scope"] = "complete_public_selection"
            elif change == "no_projection":
                wrong["identity"]["backend_receipt"]["dc_reduction_values"] = 0
            elif change == "wrong_stratum":
                wrong["result"]["stratum"] = "cold-maximum"
            else:
                wrong["identity"]["backend_receipt"]["executed"] = False
            with self.subTest(change=change), self.assertRaises(ProbeError):
                validate_receipt(wrong, command="continuation", backend="cuda", returncode=0)

    def test_vulkan_required_work_is_a_physical_full_frequency_dispatch(self):
        receipt = device_receipt()
        receipt["identity"].update(backend="vulkan", configured_vulkan_backend=True,
                                  backend_receipt=vulkan_bulk_receipt())
        receipt["phases"][0]["name"] = "coarse_tiles"
        receipt["phases"][0]["work"] = {"route": "frequency_tiles", "native_fallback_reason": ""}
        validate_receipt(receipt, command="continuation", backend="vulkan", returncode=0,
                         expected_require_accelerator=True)
        mutations = (lambda x: x["identity"]["backend_receipt"].update(vulkan=False),
                     lambda x: x["identity"]["backend_receipt"].update(vulkan_dispatches=0),
                     lambda x: x["identity"]["backend_receipt"].update(vulkan_arrival_peak_rows=0),
                     lambda x: x["identity"]["backend_receipt"].update(fft_calls=0),
                     lambda x: x["identity"]["backend_receipt"].update(score_rows=0),
                     lambda x: x["identity"]["backend_receipt"].update(vulkan_peak_owner_bytes=1 << 30),
                     lambda x: x["identity"]["backend_receipt"].update(host_cache_bytes=9 << 20),
                     lambda x: x["identity"]["backend_receipt"].update(host_cache_entries=9),
                     lambda x: x["identity"].update(require_accelerator=False),
                     lambda x: x["identity"].pop("require_accelerator"),
                     lambda x: x["phases"][0]["work"].update(native_fallback_reason="native-lag-groups"))
        for mutate in mutations:
            wrong = copy.deepcopy(receipt)
            mutate(wrong)
            with self.subTest(mutation=mutate), self.assertRaises(ProbeError):
                validate_receipt(wrong, command="continuation", backend="vulkan", returncode=0,
                                 expected_require_accelerator=True)

    def test_vulkan_stream_receipt_uses_production_receiver_work_shape(self):
        receipt = message_receipt()
        receipt["identity"].update(backend="vulkan", configured_vulkan_backend=True,
                                  require_accelerator=True, backend_receipt=vulkan_bulk_receipt())
        validate_receipt(receipt, command="message", backend="vulkan", returncode=0,
                         expected_require_accelerator=True)
        wrong = copy.deepcopy(receipt)
        wrong["result"]["complete_receiver_service"] = False
        with self.assertRaises(ProbeError):
            validate_receipt(wrong, command="message", backend="vulkan", returncode=0,
                             expected_require_accelerator=True)

    def test_vulkan_regression_receipt_uses_native_slot_work_shape(self):
        receipt = device_receipt()
        receipt["command"] = "regression"
        receipt["identity"].update(backend="vulkan", configured_vulkan_backend=True,
                                  backend_receipt=vulkan_bulk_receipt())
        receipt["result"] = {"units": 1, "stratum": "active", "work_complete": True,
                              "evaluator_expected_decision_observed": True,
                              "complete_receiver_service": False}
        receipt["phases"] = [{"name": "native_slot", "unit_index": 0,
                              "wall_seconds": .1, "represented_seconds": 1,
                              "work": {"complete": True, "all_valid": True, "decision": "data",
                                       "coarse_workspaces": 4, "coarse_frequency_points": 16,
                                       "coarse_arrival_points": 8,
                                       "work_identity_sha256": "5" * 64}}]
        validate_receipt(receipt, command="regression", backend="vulkan", returncode=0,
                         expected_require_accelerator=True)
        wrong = copy.deepcopy(receipt)
        wrong["phases"][0]["work"]["coarse_frequency_points"] = 0
        with self.assertRaises(ProbeError):
            validate_receipt(wrong, command="regression", backend="vulkan", returncode=0,
                             expected_require_accelerator=True)

    def test_legacy_non_vulkan_receipts_normalize_missing_vulkan_configuration(self):
        for backend, receipt in (("cpu", cpu_receipt("describe")), ("cuda", device_receipt())):
            with self.subTest(backend=backend):
                receipt["command"] = "describe"
                receipt["identity"].pop("configured_vulkan_backend", None)
                receipt["identity"].pop("require_accelerator", None)
                validate_receipt(receipt, command="describe", backend=backend, returncode=0)
                self.assertFalse(runtime_identity(receipt)["configured_vulkan_backend"])
                self.assertNotIn("require_accelerator", runtime_identity(receipt))

    def test_stream_cost_uses_only_actual_receiver_service(self):
        receipt = message_receipt()
        validate_receipt(receipt, command="message", backend="cpu", returncode=0)
        seconds, geometry = stream_unit(receipt)
        self.assertEqual(seconds, .125)
        self.assertEqual(stream_batch([receipt] * 4, 4), ({"native_stream": .5}, geometry))
        self.assertEqual(len(KERNELS), 9)
        self.assertEqual(SCHEMA, "datapump.triad.native-regression.v2")

    def test_stream_refuses_mutated_completion_bits_addresses_input_and_memory(self):
        mutations = (
            lambda x: x["result"].update(complete_receiver_service=False),
            lambda x: x["result"]["stream"]["slots"][2].update(bit=0),
            lambda x: x["result"]["stream"]["slots"][2].update(symbol_ordinal=7000037),
            lambda x: x["result"]["stream"]["slots"][2].update(pattern_epoch=1),
            lambda x: x["result"]["stream"]["slots"][1].update(pattern_epoch=1793850000),
            lambda x: x["result"]["stream"]["slots"][2].update(pattern_epoch=1793850121),
            lambda x: x["result"]["stream"]["slots"][3].update(complete=False),
            lambda x: x["result"]["stream"]["slots"][3].update(basis_rows=0),
            lambda x: x["result"]["stream"]["slots"][3].update(quiet_modes=0),
            lambda x: x["result"]["stream"]["slots"][4].update(bit=1),
            lambda x: x["result"]["stream"]["messages"][0].update(trailing_quiet_slots=1),
            lambda x: x["result"]["stream"]["messages"][0].update(expected_source_matches=False),
            lambda x: x["result"]["stream"].update(verified_input_sha256="9" * 64),
            lambda x: x["result"]["stream"].update(receiver_peak_reserved_bytes=2**31),
            lambda x: x["result"]["stream"].update(peak_executor_workers=5),
            lambda x: x["phases"][1]["work"].update(input_rows=499),
            lambda x: x["phases"][2]["work"].update(messages=[{}]),
        )
        for index, mutate in enumerate(mutations):
            wrong = message_receipt()
            mutate(wrong)
            with self.subTest(index=index), self.assertRaises(ProbeError):
                stream_unit(wrong)

    def test_stream_work_identity_excludes_timing_worker_allocation_and_optional_pool_peaks(self):
        original = message_receipt()
        other = copy.deepcopy(original)
        stream = other["result"]["stream"]
        del stream["peak_running_owners"], stream["peak_executor_workers"]
        stream["peak_active_owners"] = 1
        for row in stream["slots"]:
            row["workers"] = 1
            row["commit"]["wall_seconds"] = .01
        self.assertEqual(stream_unit(original), stream_unit(other))
        other["result"]["stream"]["slots"][1]["basis_rows"] += 1
        with self.assertRaises(ProbeError):
            stream_batch([original, other], 2)
        with self.assertRaises(ProbeError):
            stream_batch([original], 2)

    def test_stream_preflight_precedes_looks_and_v2_compares_all_nine_kernels(self):
        FixtureProbe.calls = []
        with patch.object(abbreviated, "NativeProbe", FixtureProbe):
            baseline = abbreviated.run()
            self.assertEqual([call[0] for call in FixtureProbe.calls[:2]], ["describe", "message"])
            self.assertEqual(sum(command == "message" for command, _ in FixtureProbe.calls), 22)
            slot_calls = [options for command, options in FixtureProbe.calls if command == "regression"]
            self.assertEqual(len(slot_calls), 42)
            self.assertTrue(all(options["units"] == 1 for options in slot_calls))
            self.assertEqual(baseline["kernels"]["native_stream"]["batch_seconds"], [[.125, .25, .5]] * 3)
            self.assertEqual(baseline["status"], "baseline_established")
            compared = abbreviated.run(baseline=baseline)
            self.assertEqual(compared["status"], "no_material_slowdown_for_measured_kernels")
            self.assertEqual(set(compared["kernels"]), set(KERNELS))
            old = copy.deepcopy(baseline)
            old["schema"] = "datapump.triad.native-regression.v1"
            with self.assertRaises(ProbeError):
                abbreviated.run(baseline=old)
            missing = copy.deepcopy(baseline)
            missing["batches"]["native_stream"][1]["receipts"].pop()
            with self.assertRaises(ProbeError):
                abbreviated.run(baseline=missing)
            previous_policy = copy.deepcopy(baseline)
            del previous_policy["policy"]["native_slot_unit"]
            with self.assertRaises(ProbeError):
                abbreviated.run(baseline=previous_policy)
            previous_batches = copy.deepcopy(baseline)
            sample = previous_batches["batches"]["active"][0]
            sample["receipt"] = sample.pop("receipts")[0]
            with self.assertRaises(ProbeError):
                abbreviated.run(baseline=previous_batches)

    def test_slot_batches_require_one_homogeneous_native_call_per_unit(self):
        probe = FixtureProbe("fixture", backend="cpu")
        unit = probe.call("regression", units=1, stratum="active")
        times, geometry = regression_batch_list([unit] * 4, "active", 4)
        self.assertEqual(times, {"native_slot": .5, "coarse": .125, "fine": .25, "audio_frontend": .0625})
        self.assertEqual(geometry["work_identity_sha256"], "6" * 64)
        with self.assertRaises(ProbeError):
            regression_batch_list([unit], "active", 2)
        repeated = probe.call("regression", units=2, stratum="active")
        with self.assertRaises(ProbeError):
            regression_batch_list([unit, repeated], "active", 2)
        for field in ("source_manifest_sha256", "executable_sha256", "build_options_sha256"):
            changed = copy.deepcopy(unit)
            changed["identity"][field] = "f" * 64
            with self.subTest(field=field), self.assertRaises(ProbeError):
                regression_batch_list([unit, changed], "active", 2)
        changed = copy.deepcopy(unit)
        changed["phases"][0]["work"]["work_identity_sha256"] = "e" * 64
        with self.assertRaises(ProbeError):
            regression_batch_list([unit, changed], "active", 2)

    def test_overall_deadline_cannot_report_partial_comparison(self):
        FixtureProbe.calls = []
        with patch.object(abbreviated, "NativeProbe", FixtureProbe), patch.object(
                abbreviated.time, "monotonic", side_effect=[0., 0., 31., 31.]):
            result = abbreviated.run(benchmark_deadline_ms=30000)
        self.assertEqual(result["status"], "incomplete")
        self.assertNotIn("kernels", result)
        self.assertEqual(result["run_budget"]["native_calls"], 1)
        self.assertLessEqual(FixtureProbe.calls[0][1]["deadline_ms"], 15000)
        self.assertEqual(result["deadline_last_call"]["command"], "describe")

    def test_deadline_preserves_completed_work_and_its_native_identity(self):
        FixtureProbe.calls = []
        completed_checks = 0

        def clock():
            nonlocal completed_checks
            if len(FixtureProbe.calls) >= 3:
                completed_checks += 1
                if completed_checks > 1:
                    return 31.
            return 0.

        with patch.object(abbreviated, "NativeProbe", FixtureProbe), patch.object(
                abbreviated.time, "monotonic", side_effect=clock):
            result = abbreviated.run(benchmark_deadline_ms=30000)
        self.assertEqual(result["status"], "incomplete")
        self.assertEqual(result["native_identity"]["source_manifest_sha256"], "2" * 64)
        self.assertEqual(result["stream_preflight"]["command"], "message")
        self.assertEqual(result["batches"]["active"][0]["receipts"][0]["command"], "regression")
        self.assertEqual(result["kernels"], {})


def component_description():
    result = device_receipt()
    result.update(command="describe", phases=[], observation=None)
    result["identity"]["backend_receipt"].update(total_device_bytes=16 * 2**30,
        initial_free_device_bytes=15 * 2**30, workspace_limit_bytes=2 * 2**30)
    return result


def component_identity():
    description = component_description()["identity"]
    return {"schema": abbreviated.COMPONENT_IDENTITY_SCHEMA,
            **{name: description[name] for name in abbreviated.COMPONENT_BUILD},
            **{name: description["backend_receipt"][name] for name in abbreviated.COMPONENT_RUNTIME},
            "fixture_source_sha256": "9" * 64, "initial_free_device_bytes": 15 * 2**30,
            "workspace_limit_bytes": 2 * 2**30, "actual_device_work": False}


def component_rows(component="fp16", case=0):
    matrix = component != "peaks"
    k, f, a, w, taps, nfft = abbreviated.MATRIX_SHAPES[case] if matrix else (8989, 32, 10553, 78, 2, 32768)
    modes = ("fft", component) if matrix else ("full_scores_host_nms", "cuda_frontier")
    rows = []
    for look in (1, 2, 3):
        for index, mode in enumerate(modes):
            row = {"schema": "native_coarse_bulk_benchmark_v1" if matrix else "native_arrival_peak_benchmark_v1",
                   "scope": "post_channelization_CoarseBulk" if matrix else "synthetic_native_bulk_geometry",
                   "kernel": k, "frequencies": f, "arrivals": a, "workspaces": w, "taps": taps,
                   "correlation_fft_rows": nfft, "look": look, "input_fnv1a64": "1234",
                   "initialized_device": True, "fresh_owner_excludes_teardown": True,
                   "owner_seconds": .001, "prepare_seconds": .002,
                   "first_lag_seconds" if matrix else "search_seconds": .003 if index else .009,
                   "fresh_owner_seconds": .006 if index else .012,
                   "workspace_limit_bytes": 2 * 2**30, "peak_reserved_bytes": 1000,
                   "allocator_peak_reserved_bytes": 2000, "upload_bytes": 3000, "download_bytes": 4000,
                   "synchronizations": 5, "score_rows": w * f * a * (2 if matrix else 1)}
            if matrix:
                row.update(math=mode, comparison_math=component, candidate_first=look % 2 == 0,
                    benchmark_case=case, resident_reuses_immutable_input=True, independent_dft_checks=19,
                    input_hash_encoding="fnv1a64_le_geometry_observation_templates_and_lag_plan_v1",
                    resident_lag_seconds=.002 if index else .008, normalized_score_error=0.,
                    matrix_calls=4 if index else 0, matrix_inner_chunks=4 if index else 0,
                    matrix_fp16_calls=4 if mode == "fp16" else 0, matrix_int8_calls=4 if mode == "int8" else 0,
                    matrix_fft_fallback_counts=[0] * 6, matrix_saturated_values=0, matrix_nonfinite_values=0,
                    matrix_output_values=2 * taps * w * f * a if index else 0,
                    matrix_maximum_rows=4096 if index else 0, matrix_maximum_columns=f if index else 0,
                    matrix_maximum_inner=k if index else 0, matrix_padded_real_macs=100 if index else 0,
                    matrix_packed_values=100 if index else 0, fft_calls=1 if index else 10)
                identity = component_identity()
                row.update({name: identity[name] for name in ("cuda_runtime_version", "cuda_driver_version",
                    "cuda_blas_version", "cuda_compute_major", "cuda_compute_minor")})
            else:
                row.update(mode=mode, compressed_first=look % 2 == 0, separation=17, independent_dft_points=16,
                    forward_ffts=f, inverse_ffts=w * f * taps,
                    fft_row_stages=(f + w * f * taps) * nfft * (nfft.bit_length() - 1),
                    cuda_arrival_peak_rows=w * f if index else 0,
                    cuda_arrival_peak_values=w * f * a if index else 0,
                    download_bytes=w * f * 40 if index else w * f * a * 8)
            rows.append(row)
    return rows


class ComponentFixtureProbe:
    changed = False

    def __init__(self, executable, *, backend):
        self.path = Path(executable)
        self.executable_sha256 = "8" * 64 if "test_triad_tensor" in str(executable) else "7" * 64

    def call(self, command, **options):
        assert command == "describe"
        return component_description()

    def verify_unchanged(self):
        if self.changed:
            raise ProbeError("Native executable changed during measurement")


def component_process(argv, **kwargs):
    identity = component_identity()
    if "--component-describe" in argv:
        identity["workspace_limit_bytes"] = 128 * 2**20
        rows = [identity]
    else:
        choice = "peaks" if "--peak-benchmark" in argv else argv[argv.index("--matrix-math") + 1]
        case = int(argv[argv.index("--matrix-benchmark-case") + 1]) if choice != "peaks" else 0
        rows = [identity, *component_rows(choice, case)]
    return subprocess.CompletedProcess(argv, 0, b"\n".join(json.dumps(row).encode() for row in rows) + b"\n", b"")


class ComponentTests(unittest.TestCase):
    def validate(self, rows, component="fp16", case=0):
        return abbreviated.validate_component_rows(rows, component, case, 2 * 2**30, component_identity())

    def test_fixed_matrix_and_peak_pairs_keep_separate_claim_scope(self):
        for choice in ("fp16", "int8", "peaks"):
            for case in (0, 1) if choice != "peaks" else (0,):
                result = self.validate(component_rows(choice, case), choice, case)
                self.assertEqual(result["status"], "component_checks_passed")
                self.assertEqual(len(result["rows"]), 6)
                self.assertAlmostEqual(result["comparisons"]["fresh_owner_seconds"]["geometric_mean_ratio"], .5)
                self.assertEqual(result["tensor_core_instruction_evidence"], "not_collected_by_this_timing_test")

    def test_missing_duplicate_look_input_geometry_and_runtime_drift_rejected(self):
        for key, value in (("look", 1), ("input_fnv1a64", "777"), ("frequencies", 128),
                           ("cuda_compute_major", 8), ("candidate_first", False), ("benchmark_case", True)):
            rows = component_rows()
            rows[3][key] = value
            with self.subTest(key=key), self.assertRaises(ProbeError):
                self.validate(rows)
        for rows in (component_rows()[:-1], component_rows() + [component_rows()[0]]):
            with self.assertRaises(ProbeError):
                self.validate(rows)

    def test_nonfinite_nonpositive_and_partial_cost_timings_rejected(self):
        for key, value in (("fresh_owner_seconds", float("nan")), ("resident_lag_seconds", 0),
                           ("prepare_seconds", -1), ("fresh_owner_seconds", .003),
                           ("normalized_score_error", .0021)):
            rows = component_rows()
            rows[1][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ProbeError):
                self.validate(rows)

    def test_matrix_request_is_not_evidence_of_executed_arithmetic(self):
        for key, value in (("matrix_calls", 0), ("matrix_int8_calls", 1),
                           ("matrix_fft_fallback_counts", [0, 0, 0, 1, 0, 0]),
                           ("matrix_fft_fallback_counts", [0]), ("matrix_output_values", 1),
                           ("matrix_saturated_values", 1), ("matrix_nonfinite_values", 1),
                           ("matrix_maximum_rows", 1024), ("score_rows", 1),
                           ("allocator_peak_reserved_bytes", 2 * 2**30 + 1)):
            rows = component_rows()
            rows[1][key] = value
            with self.subTest(key=key), self.assertRaises(ProbeError):
                self.validate(rows)

    def test_peak_reduction_preserves_full_grid_and_exact_frontier_payload(self):
        for key in ("score_rows", "inverse_ffts", "fft_row_stages", "cuda_arrival_peak_rows",
                    "cuda_arrival_peak_values", "download_bytes"):
            rows = component_rows("peaks")
            rows[1][key] -= 1
            with self.subTest(key=key), self.assertRaises(ProbeError):
                self.validate(rows, "peaks")

    def test_identity_binds_fixture_and_actual_device_without_free_memory_equality(self):
        identity = component_identity()
        changed_free = dict(identity, initial_free_device_bytes=12 * 2**30, workspace_limit_bytes=128 * 2**20)
        abbreviated.component_identity(changed_free, component_description(), identity)
        for key, value in (("source_manifest_sha256", "f" * 64), ("fixture_source_sha256", "f" * 64),
                           ("cuda", False), ("device_name", "Another GPU"), ("actual_device_work", True)):
            changed = dict(identity, **{key: value})
            with self.subTest(key=key), self.assertRaises(ProbeError):
                abbreviated.component_identity(changed, component_description(), identity)

    def run_fake(self, *, process=component_process, **options):
        ComponentFixtureProbe.changed = False
        with patch.object(abbreviated, "NativeProbe", ComponentFixtureProbe), patch.object(
                abbreviated.subprocess, "run", side_effect=process):
            return abbreviated.run_components(executable=Path("build/probe"), **options)

    def test_bounded_all_mode_runs_only_two_describes_and_three_native_comparisons(self):
        result = self.run_fake()
        self.assertEqual(result["schema"], abbreviated.COMPONENT_SCHEMA)
        self.assertEqual(result["status"], "component_checks_passed")
        self.assertEqual(result["run_budget"]["native_calls"], 5)
        self.assertEqual([entry["component"] for entry in result["components"]], ["fp16", "int8", "peaks"])
        self.assertFalse(result["complete_receiver_capacity_qualified"])
        self.assertFalse(result["cpu_forecast_qualified"])
        self.assertFalse(result["relative_commit_regression_pass"])

    def test_failure_preserves_partial_evidence_and_does_not_retry(self):
        def fail(argv, **kwargs):
            if "--matrix-math" in argv and argv[argv.index("--matrix-math") + 1] == "int8":
                return subprocess.CompletedProcess(argv, 1, b'{"partial":true}\n', b"frozen numerical gate failed")
            return component_process(argv, **kwargs)
        result = self.run_fake(process=fail)
        self.assertEqual(result["status"], "error")
        self.assertEqual(result["run_budget"]["native_calls"], 4)
        self.assertEqual(len(result["components"]), 1)
        self.assertIn("frozen numerical gate failed", result["calls"][-1]["stderr"])
        self.assertNotIn("identity_verified_at_completion", result)

    def test_timeout_and_binary_drift_cannot_report_component_pass(self):
        def expired(argv, **kwargs):
            raise subprocess.TimeoutExpired(argv, kwargs["timeout"], output=b'{"partial":true}\n')
        result = self.run_fake(process=expired)
        self.assertEqual(result["status"], "incomplete")
        self.assertTrue(result["calls"][-1]["timed_out"])
        self.assertEqual(result["run_budget"]["native_calls"], 2)
        def drift(argv, **kwargs):
            result = component_process(argv, **kwargs)
            ComponentFixtureProbe.changed = True
            return result
        result = self.run_fake(process=drift)
        self.assertEqual(result["status"], "error")
        self.assertIn("changed", result["reason"])
        result = self.run_fake(benchmark_deadline_ms=1)
        self.assertEqual(result["status"], "incomplete")
        self.assertEqual(result["run_budget"]["native_calls"], 0)

    def test_unavailable_receiver_stops_before_component_execution(self):
        unavailable = component_description()
        unavailable.update(status="unavailable", reason="CUDA not available in this runtime")
        with patch.object(ComponentFixtureProbe, "call", return_value=unavailable), patch.object(
                abbreviated, "NativeProbe", ComponentFixtureProbe), patch.object(abbreviated.subprocess, "run") as launch:
            ComponentFixtureProbe.changed = False
            result = abbreviated.run_components(executable=Path("build/probe"))
        launch.assert_not_called()
        self.assertEqual(result["status"], "unavailable")
        self.assertEqual(result["run_budget"]["native_calls"], 1)
        self.assertEqual(result["components"], [])

    def test_changed_compiled_source_and_malformed_json_stop_before_benchmark(self):
        def mismatch(argv, **kwargs):
            identity = component_identity()
            identity["source_manifest_sha256"] = "f" * 64
            return subprocess.CompletedProcess(argv, 0, json.dumps(identity).encode() + b"\n", b"")
        result = self.run_fake(process=mismatch)
        self.assertEqual(result["status"], "error")
        self.assertIn("identity mismatch", result["reason"])
        self.assertEqual(result["run_budget"]["native_calls"], 2)
        def duplicate(argv, **kwargs):
            return subprocess.CompletedProcess(argv, 0, b'{"schema":"first","schema":"second"}\n', b"")
        result = self.run_fake(process=duplicate)
        self.assertEqual(result["status"], "error")
        self.assertIn("Duplicate JSON", result["reason"])

    def test_all_arithmetic_comparisons_share_input_and_deadline_preserves_prior_rows(self):
        def changed_input(argv, **kwargs):
            result = component_process(argv, **kwargs)
            if "--matrix-math" in argv and argv[argv.index("--matrix-math") + 1] == "int8":
                rows = [json.loads(line) for line in result.stdout.splitlines()]
                for row in rows[1:]:
                    row["input_fnv1a64"] = "9876"
                result.stdout = b"\n".join(json.dumps(row).encode() for row in rows) + b"\n"
            return result
        result = self.run_fake(process=changed_input)
        self.assertEqual(result["status"], "error")
        self.assertIn("changed the fixed input", result["reason"])
        self.assertEqual(len(result["components"]), 1)
        def expired_second(argv, **kwargs):
            if "--matrix-math" in argv and argv[argv.index("--matrix-math") + 1] == "int8":
                raise subprocess.TimeoutExpired(argv, kwargs["timeout"], output=b"partial second arithmetic")
            return component_process(argv, **kwargs)
        result = self.run_fake(process=expired_second)
        self.assertEqual(result["status"], "incomplete")
        self.assertEqual(len(result["components"]), 1)
        self.assertEqual(result["run_budget"]["native_calls"], 4)
        self.assertEqual(result["calls"][-1]["stdout"], "partial second arithmetic")

    def test_component_cli_rejects_cpu_forecast_and_ignored_options(self):
        invalid = (("--device", "cpu"), ("--baseline", "old.json"), ("--cases",),
                   ("--target-units", "1000"), ("--device-index", "0"), ("--threads", "24"))
        for options in invalid:
            with self.subTest(options=options), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                abbreviated.main(["--output", "unused.json", "--device", "cuda", "--components", "all", *options])
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            abbreviated.main(["--output", "unused.json", "--device", "cuda", "--components", "peaks", "--matrix-case", "0"])
        with self.assertRaises(ProbeError):
            abbreviated.run_components(components="peaks", workspace_mib=1024)


if __name__ == "__main__":
    unittest.main()
