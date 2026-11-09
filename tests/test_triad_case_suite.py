"""Shared manual-case helper contracts; complete-runner orchestration is tested separately."""
from pathlib import Path
from contextlib import ExitStack
import ast
import hashlib
import json
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_cases import DEFAULT_CASES
from triad_case_suite import GPU_ONLY_SOURCES, run_cases, same_measured_source, select_cases, target_snr, write_result
from tests.test_triad_duration_model import complete_receipt, fixture


class SuiteHelperTests(unittest.TestCase):
    def test_gpu_only_source_exclusions_remain_outside_manual_cpu_imports(self):
        root = Path(__file__).resolve().parents[1]
        tools = root / 'tools'
        pending = [tools / 'triad_manual_cases.py']
        seen = set()
        while pending:
            path = pending.pop()
            if path in seen:
                continue
            seen.add(path)
            self.assertNotIn(path.relative_to(root).as_posix(), GPU_ONLY_SOURCES)
            for node in ast.walk(ast.parse(path.read_text(), filename=str(path))):
                if isinstance(node, ast.Import):
                    names = [alias.name for alias in node.names]
                elif isinstance(node, ast.ImportFrom) and node.module == 'tools':
                    names = [alias.name for alias in node.names]
                elif isinstance(node, ast.ImportFrom) and node.module is not None:
                    names = [node.module]
                else:
                    continue
                for name in names:
                    dep = tools / (name.removeprefix('tools.').split('.')[0]+'.py')
                    self.assertNotIn(dep.relative_to(root).as_posix(), GPU_ONLY_SOURCES)
                    if dep.is_file():
                        pending.append(dep)

    def test_identity_compares_loaded_artifacts_and_records_revision_as_context(self):
        original = dict(revision='one', sources={'receiver': 'source'}, native_binary_sha256='binary',
                        native_artifacts={'mapped': 'kernel'})
        self.assertTrue(same_measured_source(original, dict(original, revision='two')))
        self.assertFalse(same_measured_source(original, dict(original, native_artifacts={'mapped': 'changed'})))
        self.assertFalse(same_measured_source(original, dict(original, sources={'receiver': 'changed'})))
        self.assertFalse(same_measured_source(original, {'sources': original['sources']}))

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
            workspace_mib=None, full_logical_inference=False,
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
            workspace_mib=None, full_logical_inference=False,
        )

    def test_full_logical_suite_keeps_ordered_duplicate_rows_and_runs_one_canonical_measurement(self):
        import triad_case_capacity as capacity
        import triad_case_suite as suite

        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            root = Path(directory)
            source = root / 'manual.md'
            source.write_text('fixture list')
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            cases = [dict(index=index, heading=f'case {index}', id=f'id-{index}',
                configuration_hash='shared', native_args=[]) for index in (1, 2)]
            provenance = dict(revision='rev', sources={}, native_binary_sha256='exporter', native_artifacts={})
            measurement = dict(status='measured_complete_work_estimate',
                complete_capacity={'realtime_status': 'pass'})
            stack.enter_context(mock.patch('triad_cases.load_cases',
                return_value=dict(cases=cases, source_sha256=digest)))
            stack.enter_context(mock.patch('triad_case_metrics.machine_resources', return_value={
                'cpu': {'receiver_worker_candidates': [1,2,4,6,12], 'available_cpus': 12,
                        'topology': {'status': 'complete', 'physical_core_count': 6}},
                'memory_budget_bytes': 2**33}))
            stack.enter_context(mock.patch('triad_cpu.resolve_cpu_budget'))
            stack.enter_context(mock.patch('triad_cpu.configure_numerical_threads'))
            stack.enter_context(mock.patch('triad_abbreviated.environment',
                side_effect=lambda _: dict(torch_threads=1)))
            stack.enter_context(mock.patch.object(suite, 'identity', side_effect=lambda _: provenance.copy()))
            stack.enter_context(mock.patch.object(suite, 'native_plan', return_value={'geometry_feasible': True}))
            stack.enter_context(mock.patch.object(suite, 'target_snr', return_value={'status': 'checked'}))
            stack.enter_context(mock.patch.object(capacity, 'render_capacity_report', return_value='markdown'))
            gate = stack.enter_context(mock.patch('triad_message_gate.run_native_message_gate',
                return_value={'status': 'passed', 'passed': True}))
            prepare = stack.enter_context(mock.patch.object(capacity, 'PreparedCapacityCase'))
            prepared = prepare.return_value.__enter__.return_value
            prepared.receiver_preparation_seconds = prepared.simulator_generation_seconds = 1.
            prepared.channel_seed, prepared.noise_seed, prepared.source_bit = 74021, 88200, 0
            prepared.public_geometry = {'fine_unit_seconds': .5}
            prepared.quiet_energy_diagnostic.return_value = {'status': 'observed'}

            def measure(*args, **kwargs):
                gate.assert_called_once()
                prepared.quiet_energy_diagnostic.assert_called_once_with({'status': 'checked'})
                return measurement

            direct = stack.enter_context(mock.patch.object(capacity, 'measure_complete_case',
                                                            side_effect=measure))
            result = capacity.run_complete_cases(executable='exporter', output=root/'suite.json',
                case_list=source, workspace_mib=4096, sustained_allocation_probe=True,
                full_logical_inference=True)

            prepare.assert_called_once()
            self.assertEqual(prepare.call_args.kwargs['bank_workers'], 12)
            self.assertEqual(prepare.call_args.kwargs['memory_limit_bytes'], 4096*2**20)
            direct.assert_called_once()
            self.assertEqual(direct.call_args.args[1], [1,6,12])
            self.assertEqual(direct.call_args.kwargs['inference_workers'], 12)
            self.assertEqual(result['status'], 'conditional_capacity_pass')
            self.assertEqual(result['cases'][1]['reused_measurement_from'], cases[0]['id'])
            self.assertEqual(result['policy']['full_logical_inference_workers'], 12)

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
