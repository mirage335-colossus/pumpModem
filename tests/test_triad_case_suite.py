"""Shared manual-case helper contracts; complete-runner orchestration is tested separately."""
from pathlib import Path
from contextlib import ExitStack
import hashlib
import json
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_cases import DEFAULT_CASES
from triad_case_suite import run_cases, select_cases, target_snr, write_result
from tests.test_triad_duration_model import complete_receipt, fixture


class SuiteHelperTests(unittest.TestCase):
    def test_explicit_selection_uses_fresh_indices_and_rejects_stale_rows(self):
        parsed = dict(cases=[dict(index=1), dict(index=2), dict(index=3)])
        self.assertIs(select_cases(parsed), parsed["cases"])
        self.assertEqual(select_cases(parsed, [3, 1, 3]), [dict(index=1), dict(index=3)])
        for invalid in ([], [4], [True], [1.0]):
            with self.assertRaises(ValueError):
                select_cases(parsed, invalid)

    def test_default_complete_case_wrapper_rereads_default_path_in_new_runner(self):
        with mock.patch("triad_case_capacity.run_complete_cases", return_value="received") as runner:
            result = run_cases(executable="exporter", output="capacity.json")
        self.assertEqual(result, "received")
        runner.assert_called_once_with(
            executable="exporter", output="capacity.json", case_list=DEFAULT_CASES,
            measurement_seconds=1., threads=None, case_indices=None,
            paired_followup=False, message_gate=True, sustained_allocation_probe=False,
            workspace_mib=None, calibration_receipt_prefix=None, allow_conditional_calibration_identity=False,
        )

    def test_wrapper_forwards_partial_measurement_and_message_gate(self):
        with mock.patch("triad_case_capacity.run_complete_cases") as runner:
            run_cases(executable=Path("exporter"), output=Path("out.json"),
                case_list=Path("edited.md"), measurement_seconds=.5, threads=6,
                case_indices=[2, 4], message_gate=False)
        runner.assert_called_once_with(
            executable=Path("exporter"), output=Path("out.json"), case_list=Path("edited.md"),
            measurement_seconds=.5, threads=6, case_indices=[2, 4],
            paired_followup=False, message_gate=False, sustained_allocation_probe=False,
            workspace_mib=None, calibration_receipt_prefix=None, allow_conditional_calibration_identity=False,
        )

    def test_suite_saves_the_same_canonical_measurement_once_with_private_identity(self):
        import triad_case_capacity as capacity
        import triad_case_suite as suite
        import triad_cases
        import triad_nonfine_calibration as calibration

        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            root = Path(directory)
            evidence = root / 'local-evidence'; evidence.mkdir()
            tools = root / 'tools'; tools.mkdir()
            (tools / 'build_capacity.py').write_text('fixture source')
            source = root / 'manual.md'; source.write_text('fixture list')
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            cases = [dict(index=index, heading=f'case {index}', id=f'id-{index}',
                configuration_hash='shared', native_args=[]) for index in (1, 2)]
            provenance = dict(revision='rev', sources={}, native_binary_sha256='exporter')
            identity = dict(native_binary_sha256='exporter', sources={'tools/build_capacity.py':
                hashlib.sha256((tools/'build_capacity.py').read_bytes()).hexdigest()}, private='identity')
            measurement = dict(status='measured_complete_work_estimate', signal_work={'private': 'signal'},
                complete_capacity={'realtime_status': 'fail'})
            receipt = dict(schema=calibration.SCHEMA, status='recorded_unqualified', measurement=measurement,
                identity_before={'identity': identity})
            stack.enter_context(mock.patch.object(triad_cases, 'ROOT', root))
            stack.enter_context(mock.patch.object(triad_cases, 'load_cases',
                return_value=dict(cases=cases, source_sha256=digest)))
            stack.enter_context(mock.patch('triad_case_metrics.machine_resources', return_value={
                'cpu': {'receiver_worker_candidates': [1, 2], 'available_cpus': 2}, 'memory_budget_bytes': 2**33}))
            stack.enter_context(mock.patch('triad_cpu.resolve_cpu_budget'))
            stack.enter_context(mock.patch('triad_cpu.configure_numerical_threads'))
            stack.enter_context(mock.patch.object(suite, 'identity', side_effect=lambda _: provenance.copy()))
            stack.enter_context(mock.patch.object(suite, 'native_plan', return_value={'geometry_feasible': True}))
            stack.enter_context(mock.patch.object(suite, 'target_snr', return_value={'status': 'checked'}))
            stack.enter_context(mock.patch.object(capacity, 'render_capacity_report', return_value='markdown'))
            gate = stack.enter_context(mock.patch('triad_message_gate.run_native_message_gate',
                return_value={'status': 'passed', 'passed': True}))
            prepare = stack.enter_context(mock.patch.object(capacity, 'PreparedCapacityCase'))
            prepared = prepare.return_value.__enter__.return_value
            prepared.receiver_preparation_seconds = prepared.simulator_generation_seconds = 1.
            prepared.public_geometry = {'fine_unit_seconds': .5}
            prepared.quiet_energy_diagnostic.return_value = {'status': 'observed'}
            stack.enter_context(mock.patch.object(capacity, 'sustained_allocation_targets', return_value=[2]))
            direct = stack.enter_context(mock.patch.object(capacity, 'measure_complete_case'))

            def capture(*args, **kwargs):
                gate.assert_called_once()
                prepared.quiet_energy_diagnostic.assert_called_once_with({'status': 'checked'})
                return receipt

            fit = stack.enter_context(mock.patch.object(calibration, 'capture_nonfine_calibration', side_effect=capture))
            report_path = evidence / 'suite.json'
            prefix = evidence / 'fit'
            def run():
                return capacity.run_complete_cases(executable='exporter', output=report_path,
                    case_list=source, workspace_mib=4096, calibration_receipt_prefix=prefix,
                    allow_conditional_calibration_identity=True, sustained_allocation_probe=True,
                    measurement_seconds=.9)

            result = run()
            direct.assert_not_called()
            prepare.assert_called_once()
            self.assertEqual(prepare.call_args.kwargs['memory_limit_bytes'], 4096*2**20)
            fit.assert_called_once()
            self.assertEqual(fit.call_args.kwargs['family_size'], 16)
            self.assertEqual(fit.call_args.kwargs['sustained_targets'], [2])
            self.assertEqual(fit.call_args.kwargs['suite_list_sha256'], digest)
            self.assertTrue(fit.call_args.kwargs['allow_conditional'])
            self.assertEqual(result['policy']['unit_baseband_seconds'], 1.)
            self.assertIn('possibly shorter', result['policy']['unit_baseband_seconds_scope'])
            self.assertEqual(result['cases'][0]['public_geometry']['fine_unit_seconds'], .5)
            self.assertEqual(result['status'], 'capacity_failures')
            self.assertEqual(result['cases'][1]['reused_measurement_from'], cases[0]['id'])
            self.assertEqual(result['cases'][0]['private_replay_fit']['status'], 'recorded_unqualified')
            self.assertEqual(result['cases'][1]['private_replay_fit']['reason'],
                'ordered_duplicate_reuses_absolute_measurement')
            self.assertNotIn('signal_work', report_path.read_text())
            self.assertNotIn('identity_before', report_path.read_text())
            fit_path = evidence / 'fit-1.json'
            self.assertEqual(fit_path.stat().st_mode & 0o777, 0o600)
            saved_fit = json.loads(fit_path.read_text())
            self.assertEqual(saved_fit['measurement'], measurement)
            self.assertEqual(saved_fit['originating_suite_message_gate'], {'status': 'passed', 'passed': True})
            self.assertEqual(json.loads((evidence/'fit-2.json').read_text())['status'], 'unresolved')
            with self.assertRaisesRegex(ValueError, 'already exists'):
                run()
            fit.assert_called_once()

            fit.side_effect = None
            fit.return_value = receipt.copy()
            failed_gate_report = capacity.run_complete_cases(executable='exporter', output=report_path,
                case_list=source, workspace_mib=4096, calibration_receipt_prefix=evidence/'debug-fit',
                message_gate=False)
            self.assertEqual(failed_gate_report['cases'][0]['measurement']['complete_capacity']['realtime_status'], 'fail')
            self.assertEqual(failed_gate_report['cases'][0]['private_replay_fit'], {
                'status':'unresolved', 'reason':'originating_suite_message_gate_did_not_pass'})
            gate.assert_called_once()
            self.assertEqual(fit.call_count, 2)
            direct.assert_not_called()

    def test_legacy_coarse_paired_followup_is_rejected_by_complete_runner(self):
        from triad_case_capacity import run_complete_cases

        with self.assertRaisesRegex(ValueError, "coarse-only paired confirmation"):
            run_complete_cases(executable="unused", output="unused.json", case_list=DEFAULT_CASES,
                               paired_followup=True)

    def test_write_result_replaces_json_without_leaving_a_temporary_file(self):
        import json

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "result.json"
            write_result(path, {"status": "complete", "rows": [1, 2]})
            self.assertEqual(json.loads(path.read_text()), {"status": "complete", "rows": [1, 2]})
            self.assertFalse(path.with_suffix(".json.tmp").exists())

    def test_duration_model_is_reported_only_as_a_provisional_target(self):
        plan = fixture(False)
        plan.update(geometry={"active_data_seconds": 11.6}, short_message={"encoded_bits": 22})
        receipt = complete_receipt(plan)
        result = target_snr(plan, receipt)
        self.assertFalse(result["whole_bit_or_message_probability_demonstrated"])
        self.assertEqual(result["status"], "provisional_model_target")
        self.assertEqual(result["complete_admission_receipt"], receipt)
        self.assertEqual(result["fox"]["source_bits"], 22)
        self.assertGreater(result["fox"]["target_cn0_db_hz"], result["single_bit"]["target_cn0_db_hz"])
        self.assertLess(result["fox"]["bracket_db_hz"][1]-result["fox"]["bracket_db_hz"][0], .001)

    def test_duration_target_rejects_missing_or_infeasible_upper_endpoint(self):
        plan = fixture(False)
        plan.update(geometry={"active_data_seconds": 11.6}, short_message={"encoded_bits": 22})
        receipt = complete_receipt(plan)
        self.assertEqual(target_snr(plan)["status"], "unavailable")
        for prediction in ({"selected": None}, {"selected": {"meets_detection_allocation": False}}):
            with mock.patch("triad_duration_model.plan_from_native", return_value=prediction):
                result = target_snr(plan, receipt)
                self.assertEqual(result["status"], "unavailable")
                self.assertNotIn("single_bit", result)

if __name__ == "__main__":
    unittest.main()
