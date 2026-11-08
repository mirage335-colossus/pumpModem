"""Case orchestration and claim boundaries, without running timing campaigns."""
from pathlib import Path
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_case_suite import (measure_finite_projection_service, measure_prepared,
    measure_sensitivity, paired_confirmation_at_point, render_report,
    select_cases, suite_policy, target_snr)
from tests.test_triad_duration_model import fixture


class Prepared:
    def __init__(self):
        self.calls = []

    def measure(self, units):
        self.calls.append(units)
        return {"stages": {name: {
            "status": "ok", "ratio_processing_seconds_per_baseband_second": .1,
            "observed_cpu_utilization_fraction": .5, "workspace_bytes": 100,
        } for name in ("acquisition", "tracking")}, "outcome": {"whole_bit_success_probability": None}}


class ProjectionScreen:
    def __init__(self, *, samples=10, memory_limit_bytes=1_000_000, resident_estimate=100):
        import numpy as np
        self.samples = samples
        self.template_chunk_samples = 4
        self.memory_limit_bytes = memory_limit_bytes
        self.resident_estimate = resident_estimate
        self.fs = 10
        self.decimation = 2
        self.dtype = np.dtype(np.complex128)
        self.taps = np.asarray([.2, .5, .2], dtype=np.float64)


class SuiteTests(unittest.TestCase):
    def test_paired_confirmation_counts_22_common_noise_and(self):
        calls = []

        def receive(bit, cn0, noise_seed):
            calls.append((bit, cn0, noise_seed))
            return dict(correct_bit=True)

        result = paired_confirmation_at_point(receive, 14.25, noise_seed=91000)
        self.assertEqual(len(calls), 44)
        self.assertEqual(result['maximum_receive_calls'], 44)
        self.assertEqual((result['planned_pairs'], result['observed_pairs'],
                          result['successful_pairs']), (22, 22, 22))
        self.assertTrue(result['supported'])
        self.assertEqual(result['tail_alpha'], .1)
        self.assertAlmostEqual(result['exact_one_sided_lower_bound'], .1**(1/22), places=14)
        for pair_index in range(22):
            first, second = calls[2*pair_index:2*pair_index+2]
            self.assertEqual((first[0], second[0]), (0, 1))
            self.assertEqual(first[1], second[1])
            self.assertEqual(first[2], second[2])
        seeds = [calls[2*index][2] for index in range(22)]
        self.assertEqual(seeds, list(range(91000, 91022)))
        self.assertEqual(len(set(seeds)), 22)

    def test_paired_confirmation_first_bit_failure_stops_without_bound(self):
        calls = []

        def receive(bit, cn0, noise_seed):
            calls.append((bit, cn0, noise_seed))
            return dict(correct_bit=False)

        result = paired_confirmation_at_point(receive, -1., noise_seed=92000)
        self.assertEqual(calls, [(0, -1., 92000)])
        self.assertEqual(result['observed_pairs'], 1)
        self.assertFalse(result['complete_fixed_budget'])
        self.assertIsNone(result['exact_one_sided_lower_bound'])
        self.assertFalse(result['supported'])
        second_fails = paired_confirmation_at_point(
            lambda bit, *_args: dict(correct_bit=bit == 0), -1.)
        self.assertEqual(second_fails['observed_pairs'], 1)
        self.assertEqual(second_fails['successful_pairs'], 0)
        self.assertEqual(len(second_fails['pairs'][0]['outcomes']), 2)
        self.assertIsNone(second_fails['exact_one_sided_lower_bound'])

    def test_paired_followup_is_opt_in_and_requires_both_cost_gates(self):
        class Replay:
            preparation, coverage, workspace = {}, {}, {}
            def __init__(self, ratio):
                self.ratio = ratio
                self.calls = []
            def receive(self, bit, cn0_db_hz, noise_seed):
                self.calls.append((bit, cn0_db_hz, noise_seed))
                return dict(correct_bit=True, ratio_processing_seconds_per_baseband_second=self.ratio,
                    represented_baseband_seconds=1.)

        target = dict(status='provisional_model_target',
                      single_bit=dict(target_cn0_db_hz=10.))
        # Standard84 forecast exceeds600, but the 44-call opt-in followup fits.
        replay = Replay(10.)
        result = measure_sensitivity(replay, target, paired_followup=True)
        self.assertEqual(result['confirmation_cost_admission']['forecast_receiver_service_seconds'], 840.)
        self.assertEqual(result['paired_confirmation_forecast_test_seconds'], 440.)
        self.assertEqual(len(replay.calls), 26+44)
        self.assertEqual(result['paired_point_confirmation']['cn0_db_hz'], 4.)
        self.assertEqual(result['paired_point_confirmation']['successful_pairs'], 22)
        self.assertEqual(result['status'], 'unresolved')
        self.assertIsNone(result['approximate_target_cn0_db_hz'])
        self.assertEqual(result['provisional_pilot_target_cn0_db_hz'], 4.)
        paired_calls = replay.calls[26:]
        self.assertEqual([row[2] for row in paired_calls[::2]], list(range(87100, 87122)))
        self.assertTrue(all(a[0] == 0 and b[0] == 1 and a[2] == b[2]
                            for a, b in zip(paired_calls[::2], paired_calls[1::2])))
        self.assertTrue(set(row[2] for row in replay.calls[:26]).isdisjoint(
            row[2] for row in paired_calls))

        # With the option off, the same budget block never adds a paired holdout.
        unopted = Replay(10.)
        without = measure_sensitivity(unopted, target, paired_followup=False)
        self.assertEqual(len(unopted.calls), 26)
        self.assertNotIn('paired_point_confirmation', without)
        too_expensive = Replay(14.)
        blocked = measure_sensitivity(too_expensive, target, paired_followup=True)
        self.assertEqual(blocked['paired_confirmation_forecast_test_seconds'], 616.)
        self.assertEqual(len(too_expensive.calls), 26)
        self.assertNotIn('paired_point_confirmation', blocked)

        # If standard84 fits, paired followup is not considered even when opted in.
        eligible_standard = Replay(6.)
        standard = measure_sensitivity(eligible_standard, target, paired_followup=True)
        self.assertEqual(standard['confirmation_cost_admission']['forecast_receiver_service_seconds'], 504.)
        self.assertEqual(len(eligible_standard.calls), 26+84)
        self.assertNotIn('paired_point_confirmation', standard)

    def test_finite_projection_uses_warmup_then_three_fixed_extent_repeats(self):
        import numpy as np
        from triad_case_metrics import arithmetic_interval

        screen = ProjectionScreen()
        chunks_seen, timings_seen = [], []
        ratios = iter((99., .1, .2, .15))  # warmup is deliberately an outlier

        def project(chunks, taps, decimation, samples, dtype, chunk_samples):
            lengths = [len(chunk) for chunk in chunks]
            chunks_seen.append((lengths, taps, decimation, samples, dtype, chunk_samples))
            return np.zeros((samples+len(taps)-2)//decimation+1, dtype=dtype)

        def timing(_wall, _cpu, represented, threads):
            timings_seen.append((represented, threads))
            ratio = next(ratios)
            return dict(ratio_processing_seconds_per_baseband_second=ratio,
                observed_cpu_utilization_fraction=.5)

        with (mock.patch('triad_realtime_fft._fir_decimate_chunks', side_effect=project) as fir,
              mock.patch('triad_case_receiver._timing', side_effect=timing)):
            result = measure_finite_projection_service(screen, threads=2, family_size=5)

        self.assertEqual(fir.call_count, 4)  # one excluded warmup plus three repeats
        self.assertTrue(all(row[0] == [4, 4, 2] and row[3] == 10 for row in chunks_seen))
        self.assertTrue(all(row[1] is screen.taps and row[2] == 2 and row[5] == 4
                            for row in chunks_seen))
        self.assertEqual(timings_seen, [(1., 2.)]*4)
        self.assertEqual(result['model_status'], 'fixed_extent_measured_no_extrapolation')
        self.assertEqual((result['outer_trials'], result['raw_samples']), (3, 10))
        self.assertEqual(len(result['observations']), 3)
        point, interval = arithmetic_interval([.1, .2, .15], alpha=.05/(5*6))
        self.assertAlmostEqual(result['ratio_processing_seconds_per_baseband_second'], point)
        np.testing.assert_allclose(result['ratio_processing_seconds_per_baseband_second_interval'], interval)

    def test_finite_projection_extent_and_memory_preflight_skip_all_fir_calls(self):
        for screen in (ProjectionScreen(samples=4_000_001, memory_limit_bytes=2**31),
                       ProjectionScreen(memory_limit_bytes=2_000, resident_estimate=1)):
            with self.subTest(samples=screen.samples, memory=screen.memory_limit_bytes):
                with mock.patch('triad_realtime_fft._fir_decimate_chunks') as fir:
                    result = measure_finite_projection_service(screen, threads=1, family_size=2)
                self.assertEqual(result['model_status'], 'unresolved')
                self.assertEqual(result['planned_repeats'], 3)
                self.assertEqual(fir.call_count, 0)
                if screen.samples > 4_000_000:
                    self.assertIn('raw-sample cap', result['reason'])
                else:
                    self.assertIn('memory budget', result['reason'])

    def test_finite_projection_elapsed_cap_between_repeats_remains_unresolved(self):
        import numpy as np

        screen = ProjectionScreen()
        calls = []

        def project(chunks, taps, decimation, samples, dtype, chunk_samples):
            calls.append([len(chunk) for chunk in chunks])
            return np.zeros((samples+len(taps)-2)//decimation+1, dtype=dtype)

        clock = [0., .01, .02, .03, 6.]
        with (mock.patch('triad_realtime_fft._fir_decimate_chunks', side_effect=project),
              mock.patch('triad_case_receiver._timing', return_value=dict(
                  ratio_processing_seconds_per_baseband_second=.1,
                  observed_cpu_utilization_fraction=.5)),
              mock.patch('triad_case_suite.time.perf_counter', side_effect=clock)):
            result = measure_finite_projection_service(screen, threads=1, family_size=2)

        self.assertEqual(len(calls), 2)  # warmup and one measured repeat
        self.assertEqual(result['model_status'], 'unresolved')
        self.assertEqual(result['reason'], 'finite FIR elapsed budget exhausted')
        self.assertEqual(len(result['observations']), 1)
        self.assertNotIn('realtime_status', result)

    def test_explicit_partial_selection_uses_current_indices_and_rejects_stale_rows(self):
        parsed = dict(cases=[dict(index=1), dict(index=2), dict(index=3)])
        self.assertIs(select_cases(parsed), parsed["cases"])
        self.assertEqual(select_cases(parsed, [3, 1, 3]), [dict(index=1), dict(index=3)])
        for invalid in ([], [4], [True], [1.0]):
            with self.assertRaises(ValueError):
                select_cases(parsed, invalid)

    def test_report_prominently_includes_full_coarse_preparation(self):
        result = dict(input=dict(source_sha256="fixture", unchanged_at_completion=True),
            provenance=dict(unchanged_at_completion=True), machine=dict(cpu_model="fixture CPU"),
            cases=[dict(case=dict(index=1, heading="fixture"), coarse_sensitivity=dict(
                status="unresolved", reason="fixture limit", preparation=dict(
                    receiver_template_preparation_seconds=80.09, simulator_channel_generation_seconds=224.4)))])
        report = render_report(result)
        self.assertIn("Full coarse receiver preparation: **80.09 s**", report)
        self.assertIn("**exceeds the five-second readiness target**", report)
        self.assertIn("No unavoidable minimum is established", report)

    def test_sensitivity_uses_paired_pilot_and_independent_fixed_confirmation(self):
        class Replay:
            preparation, coverage, workspace = {}, {}, {}
            def __init__(self):
                self.calls = []
            def receive(self, bit, cn0_db_hz, noise_seed):
                self.calls.append((bit, cn0_db_hz, noise_seed))
                return dict(decision_bit=bit, correct_bit=True)
        replay = Replay()
        result = measure_sensitivity(replay, dict(status="provisional_model_target",
            single_bit=dict(target_cn0_db_hz=10.)))
        self.assertEqual(result["status"], "conditional_coarse_target_supported")
        self.assertEqual(result["approximate_target_cn0_db_hz"], 4.)
        self.assertEqual(len(replay.calls), 110)
        self.assertEqual([r["trials"] for r in result["confirmation"]], [42, 42])
        self.assertTrue(all(r["exact_interval"][0] > .9 for r in result["confirmation"]))
        pilot_seeds = {r[2] for r in replay.calls[2:26]}
        self.assertEqual(len(pilot_seeds), 4)
        self.assertTrue(pilot_seeds.isdisjoint(r[2] for r in replay.calls[26:]))

    def test_noiseless_failure_and_confirmation_futility_never_claim_threshold(self):
        class Replay:
            preparation, coverage, workspace = {}, {}, {}
            def receive(self, bit, cn0_db_hz, noise_seed):
                return dict(decision_bit=None, correct_bit=noise_seed < 82000)
        target = dict(status="provisional_model_target", single_bit=dict(target_cn0_db_hz=0.))
        result = measure_sensitivity(Replay(), target)
        self.assertEqual([r["trials"] for r in result["confirmation"]], [1, 1])
        self.assertEqual(len(result["earlier_confirmation_rounds"]), 1)
        self.assertTrue(all(r["exact_interval"] is None for r in result["confirmation"]))
        self.assertIsNone(result["approximate_target_cn0_db_hz"])
        replay = Replay()
        replay.receive = lambda *a, **kw: dict(correct_bit=False)
        result = measure_sensitivity(replay, target)
        self.assertEqual(result["status"], "failed_noiseless_gate")
        self.assertFalse(result["pilot"])

    def test_expensive_confirmation_stops_after_pilot_without_a_probability_claim(self):
        class Replay:
            preparation, coverage, workspace = {}, {}, {}
            def receive(self, bit, cn0_db_hz, noise_seed):
                return dict(correct_bit=True, ratio_processing_seconds_per_baseband_second=.1,
                            represented_baseband_seconds=100.)
        result = measure_sensitivity(Replay(), dict(status='provisional_model_target',
            single_bit=dict(target_cn0_db_hz=0.)))
        self.assertEqual(result['confirmation'], [])
        self.assertIsNone(result['approximate_target_cn0_db_hz'])
        self.assertEqual(result['provisional_pilot_target_cn0_db_hz'], -6.)
        self.assertEqual(result['confirmation_cost_admission']['forecast_receiver_service_seconds'], 840.)

    def test_second_predeclared_candidate_uses_fresh_noise_and_retains_failure(self):
        class Replay:
            preparation, coverage, workspace = {}, {}, {}
            def receive(self, bit, cn0_db_hz, noise_seed):
                return dict(correct_bit=not 82100 <= noise_seed < 83100)
        result = measure_sensitivity(Replay(), dict(status="provisional_model_target",
            single_bit=dict(target_cn0_db_hz=0.)))
        self.assertEqual(result["status"], "conditional_coarse_target_supported")
        self.assertEqual(result["approximate_target_cn0_db_hz"], 0.)
        self.assertEqual(result["earlier_confirmation_rounds"][0]["cn0_db_hz"], -6.)
        self.assertEqual([r["trials"] for r in result["confirmation"]], [42, 42])

    def test_replay_stream_error_retains_partial_result_without_repeating_preparation(self):
        class Replay:
            preparation, coverage, workspace = {}, {}, {}
            receive = mock.Mock(side_effect=EOFError("native stream ended"))
        replay = Replay()
        result = measure_sensitivity(replay, {})
        self.assertEqual(result["status"], "unresolved")
        self.assertEqual(replay.receive.call_count, 1)
        self.assertEqual(result["noiseless_gate"][0]["reason"], "native stream ended")

    def test_every_included_sample_counts_for_validity_and_memory_peak(self):
        class Mixed(Prepared):
            def measure(self, units):
                sample = super().measure(units)
                stage = sample["stages"]["tracking"]
                stage["workspace_bytes"] = units * 100
                stage["rss_sampled_peak_sum_process_bytes"] = units * 1000
                if len(self.calls) == 2:
                    stage["representative_work"] = False
                return sample
        result = measure_prepared(Mixed(), family_size=18)
        tracking = result["stages"]["tracking"]
        self.assertEqual(tracking["realtime_status"], "unresolved")
        self.assertEqual(tracking["stage_coverage"]["invalid_samples"], 1)
        self.assertEqual(tracking["stage_coverage"]["workspace_bytes"], 400)
        self.assertEqual(tracking["stage_coverage"]["rss_sampled_peak_sum_process_bytes"], 4000)

    def test_bounded_hierarchy_and_single_stream_claim_limits(self):
        prepared = Prepared()
        result = measure_prepared(prepared, family_size=18)
        self.assertEqual(prepared.calls, [1, 2, 4, 2, 4, 1, 4, 1, 2])
        self.assertEqual(result["independent_success_trials"], 0)
        self.assertEqual(result["whole_message_realtime_status"], "unresolved")
        for stage in result["stages"].values():
            self.assertAlmostEqual(stage["margin_fraction"], .9)
            self.assertFalse(stage["allocation_high_utilization_observed"])
            self.assertFalse(stage["whole_receiver_cpu_ceiling_demonstrated"])
        self.assertEqual(result["stages"]["acquisition"]["model_status"],
                         "fixed_observation_no_duration_extrapolation")

    def test_probability_forecast_is_never_labeled_measured_success(self):
        plan = fixture(False)
        plan.update(geometry={"active_data_seconds": 11.6}, short_message={"encoded_bits": 22})
        result = target_snr(plan)
        self.assertFalse(result["whole_bit_or_message_probability_demonstrated"])
        self.assertEqual(result["status"], "provisional_model_target")
        self.assertEqual(result["fox"]["source_bits"], 22)
        self.assertGreater(result["fox"]["target_cn0_db_hz"], result["single_bit"]["target_cn0_db_hz"])
        self.assertLess(result["fox"]["bracket_db_hz"][1]-result["fox"]["bracket_db_hz"][0], .001)

    def test_probability_forecast_rejects_missing_or_infeasible_upper_endpoint(self):
        plan = fixture(False)
        plan.update(geometry={"active_data_seconds": 11.6}, short_message={"encoded_bits": 22})
        for prediction in ({"selected": None}, {"selected": {"meets_detection_allocation": False}}):
            with mock.patch("triad_duration_model.plan_from_native", return_value=prediction):
                result = target_snr(plan)
                self.assertEqual(result["status"], "unavailable")
                self.assertNotIn("single_bit", result)

    def test_case_count_reserves_family_budget_and_never_increases_work(self):
        one, many = suite_policy(1, .5), suite_policy(12, .5)
        self.assertEqual(one["maximum_baseband_units_per_unique_case"], many["maximum_baseband_units_per_unique_case"])
        self.assertEqual(many["family_size"], 24)


if __name__ == "__main__":
    unittest.main()
