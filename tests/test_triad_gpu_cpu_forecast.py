"""Native forecast applicability and statistical decision tests."""
from copy import deepcopy
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import triad_gpu_cpu_forecast as forecast
import triad_gpu_workload as workload
from tests.test_triad_gpu_workload import bulk, make_phase, resources, slot_receipt


def phase_record(value, demand=(100, 200)):
    return dict(service=value, geometry=("fixed", 2), demand=demand)


def observation(seed, value, demand=(100, 200)):
    return dict(config={"scope": forecast.SCOPE}, seed=seed, input_sha256=f"{seed:064x}",
                phases={name: phase_record(value, demand=demand) for name in forecast.PHASES})


def forecast_inputs():
    """Assemble synthetic current-schema receipts for model and output checks."""
    parsed = forecast.load_cases()
    case = parsed["cases"][0]
    config = dict(scope=forecast.SCOPE, actual_cn0_db_hz=55., target_cn0_db_hz=30.,
                  public_window_seconds=1., public_window_samples=100, sample_rate_hz=100.,
                  recurring_horizon=dict(complete_declared_horizon=True, shortest_interval_seconds=10.))

    def identity(backend, *, paired=False):
        return dict(implementation="native-production-receiver", source_manifest_sha256="1" * 64,
                    compiler="fixture", build_config="Release", build_options_sha256="2" * 64,
                    executable_sha256=("3" if backend == "cuda" or paired else "4") * 64,
                    backend=backend, backend_receipt=bulk() if backend == "cuda" else None)

    def envelope(command, backend="cpu", *, seed=0, phases=(), result=None):
        return dict(schema="datapump.triad.native-probe.v1", command=command, status="complete",
                    identity=identity(backend), allocation=resources(), configuration=config,
                    observation=dict(seed=seed, input_sha256=f"{seed:064x}"),
                    phases=list(phases), result=result or {})

    def slot(stratum, backend="cpu", *, seed=0, paired=False):
        declared = stratum in workload.DIAGNOSTIC_STRATA
        receipt = slot_receipt(backend, command="continuation" if declared else "slot", stratum=stratum)
        receipt["configuration"] = config
        receipt["identity"] = identity(backend, paired=paired)
        receipt["observation"].update(seed=seed, input_sha256=f"{seed:064x}")
        if stratum == "noise":
            receipt["result"]["score"].update(decision="quiet", bit=None)
        for name in ("fine_work", "fine_finish"):
            forecast.phase_once(receipt, name)["work"].update(finished=True, segment_rows=50, sample_rate_hz=100.)
        if declared:
            receipt["phases"] = [p for p in receipt["phases"] if p["name"].startswith("fine_")]
            receipt["observation"]["channel_scope"] = "independently_seeded_native_thermal_noise_at_local_crop"
            receipt["result"].update(coordinate_scope="declared_public_work_coordinate", complete_receiver_service=False)
        else:
            receipt["phases"] += [make_phase("audio_frontend", dict(input_rows=100, output_rows=100, halo_samples=5)),
                                  make_phase("input_validation", dict(input_rows=100)),
                                  make_phase("helper", dict(reason="disabled", narrowed=False, prefix_samples=0,
                                                            onset_apertures=0, retained_intervals=0))]
            forecast.phase_once(receipt, "coarse_prepare")["work"].update(
                template_bank=dict(source_render_rows=100, projected_columns=4), observation=dict(fir_input_rows=100))
            forecast.phase_once(receipt, "coarse_selection")["work"].update(
                selection=dict(retained_peaks=2, selection_peak_rows=2), proposals=1, refined_coordinates=1,
                search=dict(workspace_frequency_pairs=4, score_rows=100))
        return receipt

    def backlog(seed):
        control = make_phase("controller_service", dict(may_overlap_slot_dsp=True), seconds=10, wall=.02)
        control["scope"] = "controller_wall_excluding_instrumented_cv_wait"
        return envelope("backlog", seed=seed, phases=[control], result=dict(
            complete_receiver_service=True, configured_calendar_reacquired=True,
            backlog_interval=dict(complete=True, service_seconds_per_baseband_second=.2),
            stream=dict(peak_reserved_bytes=1 << 25, input_rows=1000)))

    def codec():
        return envelope("codec", phases=[make_phase("codec_receive", {}, wall=.01)], result=dict(
            scope="isolated_native_long_codec_receipts", decoded_after_two_quiet=True,
            wire_bits=2432, marker_intervals=2, source_bytes=180, fec="rs60"))

    def trace(backend, row):
        return dict(schema=workload.SCHEMA, device=backend, input_sha256=parsed["source_sha256"],
                    selected_case_indices=[1], reference_case_indices=[1] if backend == "cuda" else [],
                    host=forecast.environment(backend), identity_verified_at_completion=True,
                    status="bounded_native_work_observed", description=envelope("describe", backend), cases=[row])

    cpu_strata = {name: [slot(name, seed=seed) for seed in (1, 2, 3)] for name in workload.STRATA}
    cpu = dict(schema=forecast.CAPACITY, identity_verified_at_completion=True,
               input=dict(source_sha256=parsed["source_sha256"]), selection=dict(indices=[1]),
               environment=forecast.environment("cpu"), description=envelope("describe"),
               message_gate=dict(passed=True, receipt=envelope("message", result=dict(
                   complete_receiver_service=True, message_and_physical_quiet_verified=True, eof_control_verified=True))),
               cases=[dict(case=case, plan=envelope("plan"), measurements=dict(
                   strata={name: cpu_strata[name] for name in workload.ACQUISITION_STRATA},
                   work_only={name: cpu_strata[name] for name in workload.DIAGNOSTIC_STRATA},
                   codec=[codec() for _ in range(3)], backlog=[backlog(seed) for seed in (10, 11, 12)]))])
    refs = {name: dict(cpu=slot(name, seed=4, paired=True)) for name in workload.STRATA}
    for role in workload.CHALLENGES:
        stratum = "noise" if role == "scored-quiet" else "active"
        refs[role] = dict(cpu=slot(stratum, seed=4, paired=True), cuda=slot(stratum, "cuda", seed=4))
    gpu = trace("cuda", dict(case=case, references=refs,
                            strata={name: slot(name, "cuda", seed=4) for name in workload.STRATA}))
    held = trace("cpu", dict(case=case, plan=envelope("plan"), backlog=[backlog(13)], codec=[codec()],
                            strata={name: slot(name, seed=5) for name in workload.STRATA}))
    return cpu, gpu, parsed, held


class NativeForecastTests(unittest.TestCase):
    def test_old_cpu_calibration_fails_before_considering_transport_or_gpu(self):
        with self.assertRaisesRegex(forecast.ProbeError, "Fresh native CPU calibration"):
            forecast.estimate({"schema": "triad-manual-complete-capacity-v3"}, {}, {}, {}, holdout={})

    def test_native_build_options_changes_invalidate_local_calibration(self):
        identity = dict(implementation="native-production-receiver", source_manifest_sha256="1" * 64,
                        executable_sha256="2" * 64, build_options_sha256="3" * 64,
                        compiler="fixture", build_config="Release", backend="cpu")
        source = dict(schema="datapump.triad.native-probe.v1", identity=identity, allocation=resources())
        modified = deepcopy(source)
        modified["identity"]["build_options_sha256"] = "4" * 64
        with self.assertRaisesRegex(forecast.ProbeError, "Native receiver build changed"):
            forecast.validate_native(modified, source)
        modified["identity"]["source_manifest_sha256"] = "5" * 64
        with self.assertRaisesRegex(forecast.ProbeError, "source changed"):
            forecast.validate_native(modified, source, same_build=False)

    def test_native_gpu_wall_cannot_change_cpu_forecast(self):
        local = [observation(seed, .01) for seed in (1, 2, 3)]
        holdout = observation(5, .01)
        gpu = observation(4, 123456., demand=(103, 200))
        fit = forecast._fit_stratum(local, gpu, holdout, alpha=.025)
        second = forecast._fit_stratum(local, observation(4, .00001, demand=(103, 200)), holdout, alpha=.025)
        self.assertEqual(fit, second)
        self.assertAlmostEqual(fit["fine"]["point"], .01 * (1.03 ** .5))
        with self.assertRaisesRegex(forecast.ProbeError, "independent seed"):
            forecast._fit_stratum(local, gpu, observation(2, .01), alpha=.025)
        with self.assertRaisesRegex(forecast.ProbeError, "independent seed"):
            forecast._fit_stratum(local, gpu, observation(4, .01), alpha=.025)
        with self.assertRaisesRegex(forecast.ProbeError, "outside the calibrated cost regime"):
            forecast._fit_stratum(local, observation(4, .00001, demand=(107, 200)), holdout, alpha=.025)

    def test_frontend_charges_continuous_pcm_and_once_per_window_validation(self):
        cpu, _, _, _ = forecast_inputs()
        receipt = cpu["cases"][0]["measurements"]["strata"]["active"][0]
        # The 1 s receiver window produces 0.8 s of PCM in a 10 s cadence.
        audio = forecast.phase_once(receipt, "audio_frontend")
        audio["work"]["output_rows"] = 80
        audio["wall_seconds"] = .16
        forecast.phase_once(receipt, "input_validation")["wall_seconds"] = .02
        measured = forecast.observation(receipt, "active")
        self.assertAlmostEqual(measured["phases"]["frontend"]["service"], .16 / .8 + .02 / 10)

    def test_cancelling_holdout_errors_and_numeric_failures_are_distinct(self):
        def item(estimate, actual):
            return dict(point=estimate, interval=[estimate * .8, estimate * 1.2], holdout=actual)

        failed = forecast._compose([[item(.39, .3)], [item(.21, .3)]], forecast.HOLDOUT["nominal"])
        self.assertAlmostEqual(failed["local_holdout"]["signed_relative_error"], 0.)
        self.assertFalse(failed["local_holdout"]["accepted"])
        self.assertFalse(failed["point_failure"])
        failure = forecast._compose([[item(.55, .54)], [item(.46, .45)]], forecast.HOLDOUT["conservative"])
        self.assertTrue(failure["local_holdout"]["accepted"])
        self.assertTrue(failure["point_failure"])
        self.assertEqual(failure["capacity_status"], "fail")

    def test_incomplete_timing_look_has_point_but_no_capacity_interval(self):
        local = [observation(1, .01), observation(2, .01)]
        fit = forecast._fit_stratum(local, observation(4, None), observation(5, .01), alpha=.025)
        row = forecast._compose([[fit["fine"]]], forecast.HOLDOUT["nominal"])
        self.assertAlmostEqual(row["seconds_per_baseband_second"], .01)
        self.assertIsNone(row["interval"])
        self.assertEqual(row["capacity_status"], "unresolved")

    def test_complete_report_serializes_and_perturbations_withhold_qualification(self):
        cpu, gpu, parsed, held = forecast_inputs()

        def estimate(target=gpu, holdout=held):
            report = forecast.estimate(cpu, target, parsed, cpu["description"], holdout=holdout,
                                       gpu_reference=gpu, case_indices=[1], holdout_indices=[1])
            return json.loads(json.dumps(report, allow_nan=False))

        report = estimate()
        self.assertEqual(report["status"], "conditional_phase_estimates")
        self.assertEqual(report["cases"][0]["complete_receiver_capacity"], "conditional_pass")
        self.assertIn([None, workload.PRECISION[1]], report["paired_precision_classes"])
        self.assertIn([workload.PRECISION[1], workload.PRECISION[1]], report["paired_precision_classes"])
        incomplete = deepcopy(gpu)
        del incomplete["cases"][0]["strata"]["terminal"]
        row = estimate(target=incomplete)["cases"][0]
        self.assertIsNotNone(row["nominal_service"])
        self.assertIsNone(row["conservative_service"])
        self.assertEqual(row["complete_receiver_capacity"], "unresolved")
        reused = deepcopy(held)
        reused["cases"][0]["strata"]["active"]["observation"]["seed"] = 4
        row = estimate(holdout=reused)["cases"][0]
        self.assertIsNone(row["nominal_service"])
        self.assertIn("independent seed", " ".join(row["unresolved_work"]))
        slow = deepcopy(held)
        slow["cases"][0]["backlog"][0]["result"]["backlog_interval"]["service_seconds_per_baseband_second"] = 1.01
        row = estimate(holdout=slow)["cases"][0]
        self.assertEqual(row["complete_receiver_capacity"], "fail")
        self.assertTrue(row["same_stream_backlog"]["numeric_point_failure"])
        changed = deepcopy(gpu)
        changed["description"]["identity"]["build_options_sha256"] = "8" * 64
        with self.assertRaisesRegex(forecast.ProbeError, "Native receiver build changed"):
            estimate(target=changed)
        changed["description"]["identity"]["source_manifest_sha256"] = "9" * 64
        with self.assertRaisesRegex(forecast.ProbeError, "Native receiver source changed"):
            estimate(target=changed)


if __name__ == "__main__":
    unittest.main()
