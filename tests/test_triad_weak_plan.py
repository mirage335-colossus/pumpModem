import json
import contextlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import triad_weak_plan as wrapper


class WeakPlanCliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.exporter = self.root / 'exporter'
        self.exporter.write_bytes(b'mock exporter')
        self.native = ['--case', 'hf', '--cn0', '-7', '--sample-rate', '6000']
        self.probe = {'schema': 'triad-helper-shaping-plan-v1', 'geometry_feasible': True,
            'format': {'tile_seconds': 2., 'tile_count': 4, 'elapsed_bit_seconds': 8.,
                       'inner_coefficients': 100,
                       'burst_wait_tiles': 0},
            'physics': {'spread_margin': 1., 'deterministic_cfo_bound_hz': 1.,
                'sample_clock_pair_ppm': 0., 'arrival_prior_seconds': [-1., 1.],
                'channel_evolution': 'legacy', 'spread_delay_ms': 0., 'spread_doppler_hz': 0.,
                'relative_phase_diffusion_rad_sqrt_s': 0., 'data_w': 1.,
                'selective_enabled': False, 'data_cn0_db_hz': 5.25},
            'export_constraints': {'fixed_helper_lead_seconds': 38.}}
        self.probe['helper_enabled'] = True
        self.probe['helper_placement'] = {'feasible_at_requested_if': True}
        self.plan = {'schema': 'triad-weak-duration-plan-v1', 'status': 'manual_duration_override',
            'selected': {'tiles': 2, 'active_seconds': 4., 'elapsed_data_seconds': 4.},
            'active_seconds': 4., 'export_supported': True, 'full_grid_count': 1_000_000_000,
            'admission_policy': {'alpha_per_day': 1e-4,
                'minimum_attempt_spacing_seconds': .4, 'duration_alternatives': 1,
                'log_null_expectation_penalty': 0.}}

    def args(self, *extra):
        return ['--exporter', str(self.exporter), *extra, '--', *self.native]

    def probe_for(self, command, *, cwd):
        value = self.probe
        if '--seconds' in command and command[command.index('--seconds') + 1] == '4':
            value = {**self.probe, 'format': {**self.probe['format'], 'tile_count': 2,
                                               'elapsed_bit_seconds': 4.}}
        return value, {'command': command, 'stdout': '{}'}

    def test_requires_target_for_automatic_plan(self):
        with self.assertRaisesRegex(ValueError, '--target-cn0'):
            wrapper.execute(self.args())

    def test_manual_plan_only_writes_exclusive_report(self):
        report = self.root / 'plan.json'
        with patch.object(wrapper, 'run_json', side_effect=self.probe_for) as run, \
             patch.object(wrapper, 'plan_from_native', return_value=self.plan) as planner:
            result = wrapper.execute(self.args('--seconds', '4', '--report', str(report)))
        self.assertEqual(run.call_args_list[0].args[0][-3:], ['--seconds', '8', '--plan-only'])
        self.assertEqual(planner.call_args.args[1], 5.25)
        self.assertEqual(result['report_path'], str(report))
        self.assertTrue(report.exists())
        self.assertEqual(result['target_cn0_forecast_source'],
            'native probe physics.data_cn0_db_hz (physical-noise forecast; may include native --cn0 override)')
        with self.assertRaises(FileExistsError):
            wrapper.execute(self.args('--seconds', '4', '--report', str(report)))

    def test_native_control_flags_are_rejected_but_cn0_is_preserved(self):
        with self.assertRaisesRegex(ValueError, 'reserved native options'):
            wrapper.execute(['--target-cn0', '8', '--exporter', str(self.exporter), '--',
                             *self.native, '--seconds=8'])
        with patch.object(wrapper, 'run_json', side_effect=self.probe_for) as run, \
             patch.object(wrapper, 'plan_from_native', return_value={**self.plan, 'export_supported': False}):
            wrapper.execute(self.args('--target-cn0', '8', '--report', str(self.root / 'another.json')))
        self.assertIn('--cn0', run.call_args.args[0])
        self.assertIn('-7', run.call_args.args[0])

    def test_export_freezes_plan_then_validates_manifest(self):
        output = self.root / 'capture'
        report = self.root / 'capture-plan.json'
        calls = []
        original_manifest_hash = []

        def native_json(command, *, cwd):
            calls.append(command)
            if '--plan-only' in command:
                value = self.probe
                if command[command.index('--seconds') + 1] == '4':
                    value = {**self.probe, 'format': {**self.probe['format'], 'tile_count': 2,
                                                       'elapsed_bit_seconds': 4.}}
                return value, {'command': command, 'stdout': '{}'}
            return self.probe, {'command': command, 'stdout': '{}'}

        def subprocess_run(command, **kwargs):
            calls.append(command)
            self.assertTrue(report.exists(), 'plan sidecar must exist before export')
            output.mkdir()
            manifest = {'format': {'tile_count': 2, 'bit_seconds': 4.},
                'sample_rate_hz': 6000,
                'power': {'data_reference_active_seconds': 4.},
                'channel': {'spread_margin': 1., 'deterministic_cfo_bound_hz': 1.,
                    'sample_clock_pair_ppm': 100.},
                'calendar': {'arrival_prior_seconds': [-1., 1.]},
                'whole_bit_calendar': {'slots': [{'helper_begin_sample': 0}]},
                'helper_placement': {'feasible_at_requested_if': True}}
            (output / 'manifest.json').write_text(json.dumps(manifest))
            original_manifest_hash.append(wrapper.sha256(output / 'manifest.json'))
            class Completed:
                returncode = 0
                stdout = 'ok'
                stderr = ''
            return Completed()

        custom_plan = dict(self.plan, admission_policy={'alpha_per_day': .02,
            'minimum_attempt_spacing_seconds': .8, 'duration_alternatives': 2,
            'log_null_expectation_penalty': 0.})
        with patch.object(wrapper, 'run_json', side_effect=native_json), \
             patch.object(wrapper, 'plan_from_native', return_value=custom_plan) as planner, \
             patch.object(wrapper.subprocess, 'run', side_effect=subprocess_run):
            result = wrapper.execute(['--exporter', str(self.exporter), '--seconds', '4',
                                      '--alpha-per-day', '.02', '--attempt-spacing-seconds', '.8',
                                      '--duration-alternatives', '2', '--output', str(output),
                                      '--report', str(report), '--templates',
                                      '--', *self.native])
        export_command = calls[-1]
        self.assertIn('--whole-bit-count', export_command)
        self.assertEqual(export_command[export_command.index('--whole-bit-count') + 1], '1')
        self.assertIn('--templates', export_command)
        self.assertEqual(planner.call_args.kwargs['alpha_per_day'], .02)
        self.assertEqual(planner.call_args.kwargs['duration_alternatives'], 2)
        self.assertGreater(result['export']['public_grid_count'], 13 * 2001 * 201)
        saved = json.loads(report.read_text())
        self.assertEqual(saved['export']['manifest_sha256'], wrapper.sha256(output / 'manifest.json'))
        self.assertEqual(saved['export']['native_manifest_sha256'], original_manifest_hash[0])
        self.assertTrue(saved['weak_admission_policy_applied'])
        enriched = json.loads((output / 'manifest.json').read_text())
        self.assertEqual(enriched['weak_admission_policy'], custom_plan['admission_policy'])

    def test_burst_elapsed_duration_is_checked_separately_from_active_duration(self):
        report = self.root / 'burst-plan.json'
        burst_plan = dict(self.plan, selected={'tiles': 2, 'active_seconds': 4.,
            'elapsed_data_seconds': 6.}, active_seconds=4.)

        def burst_probe(command, *, cwd):
            value = self.probe
            if '--seconds' in command and command[command.index('--seconds') + 1] == '4':
                value = {**self.probe, 'format': {**self.probe['format'], 'tile_count': 2,
                                                   'elapsed_bit_seconds': 6.}}
            return value, {'command': command, 'stdout': '{}'}

        with patch.object(wrapper, 'run_json', side_effect=burst_probe), \
             patch.object(wrapper, 'plan_from_native', return_value=burst_plan):
            result = wrapper.execute(self.args('--seconds', '4', '--report', str(report)))
        self.assertEqual(result['duration_plan']['selected']['active_seconds'], 4.)
        self.assertEqual(result['duration_plan']['selected']['elapsed_data_seconds'], 6.)
        selected_command = json.loads(report.read_text())['native']['selected_duration_probe']['command']
        self.assertEqual(selected_command[-3:], ['--seconds', '4', '--plan-only'])

    def test_helper_off_or_unsupported_export_fails_and_retains_report(self):
        cases = (
            ('helper-off', {'helper_enabled': False}),
            ('placement-unavailable', {'helper_placement': {'feasible_at_requested_if': False}}),
            ('geometry-unavailable', {'geometry_feasible': False}),
        )
        for name, changes in cases:
            with self.subTest(name=name):
                report = self.root / f'{name}.json'
                output = self.root / name
                plan = dict(self.plan, status='geometry_unavailable', export_supported=False,
                            selected=self.plan['selected'])
                probe = {**self.probe, **changes}
                native_args = self.native + (['--helper', 'off'] if name == 'helper-off' else [])
                def unavailable_probe(command, *, cwd):
                    value = probe
                    if '--seconds' in command and command[command.index('--seconds') + 1] == '4':
                        value = {**probe, 'format': {**probe['format'], 'tile_count': 2,
                                                     'elapsed_bit_seconds': 4.}}
                    return value, {'command': command, 'stdout': '{}'}
                def unavailable_plan(native, target, **kwargs):
                    if name == 'helper-off':
                        self.assertFalse(native['helper_enabled'])
                    if name == 'placement-unavailable':
                        self.assertFalse(native['helper_placement']['feasible_at_requested_if'])
                    if name == 'geometry-unavailable':
                        self.assertFalse(native['geometry_feasible'])
                    return plan
                with patch.object(wrapper, 'run_json', side_effect=unavailable_probe), \
                     patch.object(wrapper, 'plan_from_native', side_effect=unavailable_plan), \
                     patch.object(wrapper.sys, 'argv', ['triad_weak_plan.py', '--exporter', str(self.exporter),
                        '--target-cn0', '8', '--seconds', '4', '--output', str(output), '--report', str(report),
                        '--', *native_args]), contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(wrapper.main(), 2)
                self.assertTrue(report.is_file())
                self.assertFalse(output.exists())
                retained = json.loads(report.read_text())
                self.assertEqual(retained['duration_plan']['status'], 'geometry_unavailable')

    def test_existing_output_rejected_before_native_probe(self):
        output = self.root / 'already-there'
        output.mkdir()
        with patch.object(wrapper, 'run_json') as run:
            with self.assertRaises(FileExistsError):
                wrapper.execute(['--exporter', str(self.exporter), '--target-cn0', '8',
                                 '--output', str(output), '--', *self.native])
        run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
