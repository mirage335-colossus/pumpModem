#!/usr/bin/env python3
"""Tiny metadata/array regressions; these never generate or score physical paths."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('test_oracle_auditor', HERE / 'audit_oracle_timing.py')
AUDIT = importlib.util.module_from_spec(spec)
spec.loader.exec_module(AUDIT)


def fixture(root):
    nominal_dir, oracle_dir = root / 'nominal', root / 'oracle'
    nominal_dir.mkdir(); oracle_dir.mkdir()
    identity = {'index': 7, 'source_bit': 0, 'epoch': 1807200000, 'control': 'signal',
                **{key + '_seed': i + 11 for i, key in enumerate(
                    ('key', 'channel', 'oscillator', 'noise', 'frontend', 'path'))}}
    argv = [str(root / 'binary'), '--mode', 'export-stats', '--output', str(nominal_dir),
            '--start', '0.0', '0.0', '1.0', '--frequency', '0.0', '--clock', '0.0']
    nominal_job = {'name': 'nominal', 'timeout_seconds': 30, 'argv': argv,
                   'simulation_only_expected': identity}
    oracle_job = copy.deepcopy(nominal_job)
    oracle_job['name'] = 'oracle'
    oracle_job['argv'][oracle_job['argv'].index('--output') + 1] = str(oracle_dir)
    oracle_job['argv'][oracle_job['argv'].index('--start') + 1:oracle_job['argv'].index('--start') + 3] = ['.015625'] * 2
    candidates = [dict(bit=bit, epoch_offset_seconds=0, arrival_seconds=.125,
                       frequency_hz=0, clock_ppm=0, frequency_drift_hz_per_second=0,
                       tap_count=taps) for taps in (1, 2) for bit in (0, 1)]
    m = {'format': 'triad-block-stats-v1', 'byte_order': 'little', 'complex_type': 'complex128',
         'real_type': 'float64', 'integer_type': 'uint64', 'layout': 'block,row,column',
         'block_count': 2, 'dimension': 4, 'sample_count': 64, 'sample_rate': 2048,
         'block_samples': 32, 'candidate_count': 4, 'candidates': candidates,
         'column_mapping': [[0], [1], [0, 2], [1, 3]],
         'column_descriptors': [dict(c) for c in candidates], 'signal_bit': 0,
         'capture_lead_seconds': .125, 'selected_truth_arrival_seconds_simulation_only': .140625,
         'command_argv': argv, 'public_timing_bounds_capture_seconds': [.125, .125],
         'initial_physical_arrival_bounds_capture_seconds': [.025, .225],
         'source_format': {'encrypted': True, 'inner_rate_hz': 360},
         'resolved_configuration': {'initial_arrival_bounds_seconds': [-.1, .1],
                                    'public_search': {'timing_bounds_seconds': [0., 0.], 'frequency_bound': 1e-6}},
         'endpoint_truth': {'tx_audio_ppm': .00001}, 'channel_model': {'physical_rf_retuning': False},
         'path_truth_simulation_only': {'gaussian_ray_teacher': {'fir_coefficients': [0., 1., 0.]},
                                      'selective_perturbation': {'enabled': False}},
         'oscillator_model': {'relative_phase_diffusion_rad_sqrt_s': .001},
         'file_sizes': {}}
    arrays = {'gram': (np.repeat(np.eye(4, dtype=complex)[None], 2, axis=0), '<c16'),
              'mean': (np.array([[1., .1, .2, .1]] * 2), '<c16'),
              'signal_energy': (np.array([10., 10.]), '<f8'),
              'received_energy': (np.array([100., 110.]), '<f8'),
              'intervals': (np.array([[0, 32], [32, 64]]), '<u8'),
              'valid_rows': (np.array([32, 32]), '<u8')}
    for name, (values, dtype) in arrays.items():
        filename = name + ('.c128' if dtype == '<c16' else '.f64' if dtype == '<f8' else '.u64')
        values = values.astype(dtype)
        for directory in (nominal_dir, oracle_dir): values.tofile(directory / filename)
        m[name + '_file'] = filename; m[name + '_shape'] = list(values.shape)
        m['file_sizes'][filename] = values.nbytes
    pair = {'nominal_source': str(nominal_dir), 'oracle_source': str(oracle_dir),
            'capture_lead_seconds': .125, 'exact_nominal_capture_arrival_seconds': .140625,
            'oracle_start_seconds': .015625, 'physical_start_prior_seconds': [-.1, .1],
            'expected_identity': identity}
    oracle = copy.deepcopy(m)
    oracle['command_argv'] = oracle_job['argv']
    for field in ('candidates', 'column_descriptors'):
        for c in oracle[field]: c['arrival_seconds'] = .140625
    oracle['public_timing_bounds_capture_seconds'] = [.140625] * 2
    oracle['resolved_configuration']['public_search']['timing_bounds_seconds'] = [.015625] * 2
    (nominal_dir / 'manifest.json').write_text(json.dumps(m))
    (oracle_dir / 'manifest.json').write_text(json.dumps(oracle))
    pair['nominal_manifest_sha256'] = AUDIT.sha256(nominal_dir / 'manifest.json')
    return pair, nominal_job, oracle_job, m, oracle


def runner_row(job):
    return {'name': job['name'], 'command': job['argv'], 'returncode': 0,
            'runner': {'schema': 'triad-bounded-run-v1', 'outcome': 'completed',
                       'cancelled': False, 'timed_out': False, 'cancel_signal': None,
                       'exit_code': 0, 'child_exit_code': 0,
                       'declared_max_seconds': job['timeout_seconds'], 'timeout_seconds': job['timeout_seconds'],
                       'termination': {'group_empty': True}}}


class OracleTimingTests(unittest.TestCase):
    def test_exact_oracle_manifest_and_all_physical_array_receipts(self):
        with tempfile.TemporaryDirectory() as tmp:
            pair, nj, oj, nominal, oracle = fixture(Path(tmp))
            self.assertEqual(AUDIT.audit_manifest_pair(pair, nj, oj, nominal, oracle), .140625)
            receipts = AUDIT.audit_unchanged_arrays(Path(pair['nominal_source']), Path(pair['oracle_source']), nominal, oracle)
            self.assertEqual(set(receipts), set(AUDIT.UNCHANGED_ARRAYS))
            self.assertTrue(all(r['byte_identical'] for r in receipts.values()))

    def test_hidden_cfo_clock_channel_or_physical_metadata_changes_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            pair, nj, oj, nominal, oracle = fixture(Path(tmp))
            mutations = [lambda m: m['candidates'][0].update(frequency_hz=1e-4),
                         lambda m: m['candidates'][0].update(clock_ppm=.1),
                         lambda m: m['endpoint_truth'].update(tx_audio_ppm=0),
                         lambda m: m['oscillator_model'].update(relative_phase_diffusion_rad_sqrt_s=0),
                         lambda m: m['path_truth_simulation_only']['gaussian_ray_teacher'].update(fir_coefficients=[1]),
                         lambda m: m['resolved_configuration']['public_search'].update(frequency_bound=1),
                         lambda m: m.update(sample_count=65),
                         lambda m: m['source_format'].update(encrypted=False)]
            for mutate in mutations:
                changed = copy.deepcopy(oracle); mutate(changed)
                with self.assertRaisesRegex(ValueError, 'changed beyond'): AUDIT.audit_manifest_pair(pair, nj, oj, nominal, changed)

    def test_only_two_arrival_endpoints_and_output_may_change(self):
        with tempfile.TemporaryDirectory() as tmp:
            pair, nj, oj, nominal, oracle = fixture(Path(tmp))
            for flag, offset, value in (('--frequency', 1, '1e-4'), ('--clock', 1, '.1'),
                                        ('--start', 3, '.5'), ('--start', 1, '.015')):
                changed = copy.deepcopy(oj)
                changed['argv'][changed['argv'].index(flag) + offset] = value
                with self.assertRaises(ValueError): AUDIT.audit_command_pair(pair, nj, changed, nominal)
            changed = copy.deepcopy(oj); changed['simulation_only_expected']['noise_seed'] += 1
            with self.assertRaisesRegex(ValueError, 'seed identity'): AUDIT.audit_command_pair(pair, nj, changed, nominal)

    def test_exact_truth_and_original_physical_prior_are_mandatory(self):
        with tempfile.TemporaryDirectory() as tmp:
            pair, nj, oj, nominal, oracle = fixture(Path(tmp))
            for key, value in (('oracle_start_seconds', .015), ('capture_lead_seconds', 0),
                               ('exact_nominal_capture_arrival_seconds', .15),
                               ('physical_start_prior_seconds', [-1, 1])):
                changed = copy.deepcopy(pair); changed[key] = value
                with self.assertRaises(ValueError): AUDIT.audit_command_pair(changed, nj, oj, nominal)
            changed = copy.deepcopy(nominal)
            changed['resolved_configuration']['initial_arrival_bounds_seconds'] = [-.01, .01]
            with self.assertRaisesRegex(ValueError, 'outside'): AUDIT.audit_command_pair(pair, nj, oj, changed)

    def test_energy_and_calendar_byte_changes_fail_even_with_equal_metadata(self):
        for filename in AUDIT.UNCHANGED_ARRAYS:
            with self.subTest(filename=filename), tempfile.TemporaryDirectory() as tmp:
                pair, nj, oj, nominal, oracle = fixture(Path(tmp))
                target = Path(pair['oracle_source']) / filename
                raw = bytearray(target.read_bytes()); raw[0] ^= 1; target.write_bytes(raw)
                with self.assertRaisesRegex(ValueError, 'array changed'):
                    AUDIT.audit_unchanged_arrays(Path(pair['nominal_source']), Path(pair['oracle_source']), nominal, oracle)

    def test_projection_selects_fixed_richest_truth_arrival_without_frequency_or_clock_fix(self):
        analyzer = AUDIT.load_analyzer()
        with tempfile.TemporaryDirectory() as tmp:
            pair, nj, oj, nominal, oracle = fixture(Path(tmp))
            result = AUDIT.projection_summary(analyzer, Path(pair['oracle_source']), oracle, .140625)
            self.assertAlmostEqual(result['correct_projection_energy_samples'], 2.08)
            self.assertAlmostEqual(result['wrong_projection_energy_samples'], .04)
            self.assertEqual(result['sampled_signal_energy_samples'], 20.)
            self.assertEqual(result['correct_rank_histogram'], {2: 2})
            with self.assertRaisesRegex(ValueError, 'missing requested'):
                AUDIT.projection_summary(analyzer, Path(pair['oracle_source']), oracle, .125)

    def test_runner_success_requires_complete_exact_commands_cleanup_and_timeout(self):
        job = {'name': 'a', 'argv': ['python', 'x'], 'timeout_seconds': 12}
        row = runner_row(job); AUDIT.audit_runner([row], [job])
        mutations = [lambda r: r.update(returncode=1), lambda r: r.update(command=['other']),
                     lambda r: r['runner'].update(timed_out=True),
                     lambda r: r['runner']['termination'].update(group_empty=False),
                     lambda r: r['runner'].update(timeout_seconds=13),
                     lambda r: r['runner'].pop('child_exit_code')]
        for mutate in mutations:
            bad = copy.deepcopy(row); mutate(bad)
            with self.assertRaises(ValueError): AUDIT.audit_runner([bad], [job])
        for bad in ([], [row, row]):
            with self.assertRaises(ValueError): AUDIT.audit_runner(bad, [job])

    def test_source_inventory_generator_binary_and_bundle_are_nonvacuous_and_exact(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            class Paths:
                evidence_root = root
                def local(self, p): return Path(p)
            inventory = {}
            for i, name in enumerate(sorted(AUDIT.GENERATION_FILES)):
                p = root / name; p.parent.mkdir(parents=True, exist_ok=True); p.write_text(str(i))
                inventory[name] = AUDIT.sha256(p)
            binary = root / 'binary'; binary.write_bytes(b'compiled reference')
            generator = root / 'generator'; generator.write_text('frozen generator')
            source = {'source_file_sha256': inventory,
                      'source_bundle_sha256': hashlib.sha256(json.dumps(inventory, sort_keys=True).encode()).hexdigest(),
                      'campaign_helper_sha256': inventory['tools/triad-lowrate-campaign.py'],
                      'binary_sha256': AUDIT.sha256(binary)}
            plan = {'source': source, 'generator_sha256': AUDIT.sha256(generator),
                    'reference_jobs': [{'argv': [str(binary)]}], 'jobs': [{'argv': [str(binary)]}]}
            AUDIT.audit_source(plan, Paths(), generator)
            for mutate in (lambda p: p['source'].update(source_file_sha256={}),
                           lambda p: p['source'].update(source_bundle_sha256='0' * 64),
                           lambda p: p['source'].update(binary_sha256='0' * 64),
                           lambda p: p.update(generator_sha256='0' * 64)):
                changed = copy.deepcopy(plan); mutate(changed)
                with self.assertRaises(ValueError): AUDIT.audit_source(changed, Paths(), generator)

    def test_original_parent_plan_sha_job_and_profile_bindings(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            class Paths:
                def local(self, p): return Path(p)
            job = {'name': 'original', 'argv': ['binary', '--output', str(root / 'workspace')]}
            parent = {'source': {'revision': 'frozen'}, 'jobs': [job], 'profiles': [{'name': 'high10', 'width': 10}]}
            parent_path = root / 'parent.json'; parent_path.write_text(json.dumps(parent))
            score_path = root / 'score.json'; score_path.write_text(json.dumps({'scoring_jobs': []}))
            plan = {'source': parent['source'], 'reference_jobs': [copy.deepcopy(job)], 'profiles': parent['profiles'],
                    'parent_export_plan': {'path': str(parent_path), 'sha256': AUDIT.sha256(parent_path)},
                    'parent_score_plan': {'path': str(score_path), 'sha256': AUDIT.sha256(score_path)}}
            AUDIT.audit_parent_plans(plan, Paths())
            for mutate in (lambda p: p['parent_export_plan'].update(sha256='0' * 64),
                           lambda p: p['reference_jobs'][0].update(name='different'),
                           lambda p: p.update(profiles=[{'name': 'high10', 'width': 30}]),
                           lambda p: p['source'].update(revision='different')):
                bad = copy.deepcopy(plan); mutate(bad)
                with self.assertRaises(ValueError): AUDIT.audit_parent_plans(bad, Paths())


if __name__ == '__main__':
    unittest.main()
