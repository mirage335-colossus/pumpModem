#!/usr/bin/env python3
"""Cheap statistical/orchestration contracts; no sampled campaign is run here."""
import json
from pathlib import Path
import sys
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import triad_helper_campaign as campaign
from triad_helper_campaign import (binomial_interval, decide, evaluator_metrics,
                                   freeze_gate, outcome_counts, schedule, write_json)
from triad_helper_statistics import calibration_condition_id


def score(value, bit=0, procedure='whole-adaptive-rule'):
    return dict(procedure_id=procedure, executed_arms=['baseline', 'assisted'],
                calibration_condition_id=calibration_condition_id(procedure, True),
                baseline=dict(working_presence=value, bit=bit, receiver_id='baseline-bank'),
                assisted=dict(working_presence=value + 1, bit=bit, receiver_id='selected-bank'))


class CampaignTests(unittest.TestCase):
    def test_export_shaping_options_are_explicit(self):
        command = ['exporter', '--case', 'eme']
        defaults = SimpleNamespace(shaping=None, shaping_tail_fraction=None, rate=None,
            added_symbols=None, fhss=None, burst_wait_tiles=None)
        self.assertEqual(campaign.append_export_shaping_options(command.copy(), defaults), command)

        defaults.shaping = 'legacy'
        self.assertEqual(campaign.append_export_shaping_options(command.copy(), defaults),
                         command + ['--shaping', 'legacy'])

        selected = SimpleNamespace(shaping='auto', shaping_tail_fraction=.001, rate=360.,
            rf_hz=500e3, base_oscillator='crystal', shift_oscillator='ic-7100',
            spread_preset='hf-worst', margin=3., selective_perturbation='each-65',
            selective_modulation_rate_hz=.05, timing_policy='bounded', data_power_w=4.,
            helper_power_w=40., added_symbols=10, fhss='on', burst_wait_tiles=3)
        self.assertEqual(campaign.append_export_shaping_options(command.copy(), selected),
            command + ['--shaping', 'auto', '--shaping-tail-fraction', '0.001',
                       '--rf-hz', '500000.0', '--base-oscillator', 'crystal',
                       '--shift-oscillator', 'ic-7100', '--spread-preset', 'hf-worst',
                       '--margin', '3.0', '--selective-perturbation', 'each-65',
                       '--selective-modulation-rate-hz', '0.05', '--timing-policy', 'bounded',
                       '--data-power-w', '4.0', '--helper-power-w', '40.0',
                       '--rate', '360.0', '--added-symbols', '10', '--fhss', 'on',
                       '--burst-wait-tiles', '3'])

    def test_schedule_is_disjoint_fixed_and_balanced(self):
        rows = schedule(103, 19, 8, 8)
        self.assertEqual(len({r['seed'] for r in rows}), 35)
        self.assertEqual(rows, schedule(103, 19, 8, 8))
        self.assertEqual([r['bit'] for r in rows if r['phase'] == 'signal'], [0, 1] * 4)
        self.assertTrue(all(r['data'] == (r['phase'] == 'signal') for r in rows))
        with self.assertRaises(ValueError):
            schedule(2**64 - 1, 2, 0, 0)

    def test_rank_gate_ties_and_complete_procedure_identity(self):
        gate = freeze_gate([score(i) for i in range(19)], 'whole-adaptive-rule', .05)
        self.assertEqual(gate['minimum_p'], .05)
        self.assertFalse(decide(score(18), gate, 'baseline')['accepted'])
        self.assertTrue(decide(score(19), gate, 'baseline')['accepted'])
        self.assertTrue(decide(score(19), gate, 'assisted')['accepted'])
        changed_bank = score(19)
        changed_bank['assisted']['receiver_id'] = 'another-data-dependent-bank'
        self.assertTrue(decide(changed_bank, gate, 'assisted')['accepted'])
        with self.assertRaisesRegex(ValueError, 'identity'):
            decide(score(19, procedure='changed-algorithm'), gate, 'assisted')
        with self.assertRaisesRegex(ValueError, 'identity'):
            freeze_gate([score(1, procedure='wrong')], 'whole-adaptive-rule', .05)

    def test_gate_declares_only_measured_arms(self):
        calibration = []
        for value in range(3):
            row = score(value)
            row['executed_arms'] = ['assisted']
            row['execution_policy_id'] = 'assisted-policy'
            row.pop('baseline')
            calibration.append(row)
        gate = freeze_gate(calibration, 'whole-adaptive-rule', .25, arms=('assisted',))
        self.assertEqual(gate['arms'], ['assisted'])
        self.assertEqual(set(gate['scores']), {'assisted'})
        row = score(4)
        row['executed_arms'] = ['assisted']
        row['execution_policy_id'] = 'assisted-policy'
        row.pop('baseline')
        self.assertTrue(decide(row, gate, 'assisted')['accepted'])
        wrong_policy = dict(row, execution_policy_id='paired-policy')
        with self.assertRaisesRegex(ValueError, 'execution policy identity'):
            decide(wrong_policy, gate, 'assisted')
        with self.assertRaisesRegex(ValueError, 'not calibrated'):
            decide(row, gate, 'baseline')
        with self.assertRaisesRegex(ValueError, 'not measured'):
            freeze_gate([row], 'whole-adaptive-rule', .25, arms=('baseline',))

    def test_gate_cannot_cross_helper_conditions(self):
        gate = freeze_gate([score(i) for i in range(19)], 'whole-adaptive-rule', .05)
        absent = score(19)
        absent['calibration_condition_id'] = calibration_condition_id(absent['procedure_id'], False)
        with self.assertRaisesRegex(ValueError, 'condition identity'):
            decide(absent, gate, 'assisted')
        with self.assertRaisesRegex(ValueError, 'condition identity'):
            freeze_gate([score(0), absent], 'whole-adaptive-rule', .05)

    def test_small_calibration_cannot_reach_smaller_alpha(self):
        gate = freeze_gate([score(0)], 'whole-adaptive-rule', .05)
        self.assertFalse(decide(score(1e6), gate, 'baseline')['accepted'])

    def test_intervals_include_endpoints_and_zero_trial_is_unqualified(self):
        zero = binomial_interval(0, 8)
        self.assertEqual(zero['two_sided'][0], 0)
        self.assertAlmostEqual(zero['one_sided_upper'], 1 - .05**(1 / 8), places=12)
        all_success = binomial_interval(8, 8)
        self.assertEqual(all_success['two_sided'][1], 1)
        self.assertAlmostEqual(all_success['two_sided'][0], .025**(1 / 8), places=12)
        self.assertIsNone(binomial_interval(0, 0)['two_sided'])

    def test_outcomes_keep_errors_misses_and_false_admissions_separate(self):
        rows = [dict(trial=dict(phase=phase), evaluator=dict(source_bit=truth),
                     decisions=dict(baseline=dict(accepted=accepted, bit=bit)))
                for phase, accepted, bit, truth in (
                    ('signal', True, 0, 0), ('signal', True, 0, 1),
                    ('signal', False, 1, 1), ('null', True, 0, 0), ('null', False, 1, 0))]
        rows.insert(0, dict(trial=dict(phase='calibration')))
        result = outcome_counts(rows, 'baseline')
        self.assertEqual(result['signal_trials'], 3)
        self.assertEqual(result['null_trials'], 2)
        self.assertEqual(result['counts'], dict(correct_admitted=1, wrong_admitted=1,
            missed=1, false_admitted=1, rejected_null=1, raw_bit_errors=1))
        self.assertNotIn('correct_admitted', result['intervals'])
        self.assertEqual(result['signal_uncertainty']['per_bit']['0']['trials'], 1)
        self.assertEqual(result['signal_uncertainty']['per_bit']['1']['trials'], 2)

    def test_evaluator_does_not_change_decisions(self):
        result = score(7)
        result['baseline']['best_group_public_parameters'] = dict(frequency_hz=.2)
        result['assistance'] = dict(selected_frequencies=[.1, .2, .3], expanded_intervals_hz=[[.1, .3]])
        before = json.dumps(result, sort_keys=True)
        truth = dict(tx=dict(audio_ppm=0., rf_ppm=.2), rx=dict(audio_ppm=0., rf_ppm=0.),
                     source_bit=0, arrival_seconds=.01, source_energy={}, channel_report={})
        manifest = dict(channel=dict(rf_hz=1e6, nominal_conversion_if_hz=1725.))
        diagnostic = evaluator_metrics(truth, manifest, result)
        self.assertTrue(diagnostic['baseline_best_cfo_retained'])
        self.assertTrue(diagnostic['static_cfo_in_expanded_interval'])
        self.assertAlmostEqual(diagnostic['nominal_static_cfo_hz'], .2)
        self.assertEqual(before, json.dumps(result, sort_keys=True))

    def test_assisted_only_compaction_and_evaluator_do_not_invent_baseline(self):
        measured = dict(receiver_id='selected', bit=0, working_presence=8.,
            bit_log_likelihoods=[8., 1.], opposite_bit_margin=7., best_epoch_offset_seconds=0.,
            best_group_public_parameters=None, timing={}, arithmetic={}, resources={},
            geometry=dict(epoch_offsets=[0.], lag_samples=[0], frequency_offsets_hz=[.1],
                          samples=10, effective_group_count=1))
        result = dict(score(7), schema='test', executed_arms=['assisted'],
            input_sha256='input', template_sha256='template', source_hashes={}, limits=[],
            timing={}, assistance=dict(selected_frequencies=[.1], expanded_intervals_hz=[[.1,.2]]),
            baseline=None, assisted=measured)
        compact = campaign.compact_result(result)
        self.assertEqual(compact['executed_arms'], ['assisted'])
        self.assertNotIn('baseline', compact)
        truth = dict(tx=dict(audio_ppm=0., rf_ppm=.2), rx=dict(audio_ppm=0., rf_ppm=0.),
                     source_bit=0, arrival_seconds=.01, source_energy={}, channel_report={})
        manifest = dict(channel=dict(rf_hz=1e6, nominal_conversion_if_hz=1725.))
        diagnostic = evaluator_metrics(truth, manifest, result)
        self.assertIsNone(diagnostic['baseline_best_cfo_hz'])
        self.assertIsNone(diagnostic['baseline_best_cfo_retained'])

    def test_frozen_output_cannot_be_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'gate.json'
            write_json(path, dict(frozen=True))
            with self.assertRaises(FileExistsError):
                write_json(path, dict(frozen=False))
            self.assertEqual(json.loads(path.read_text()), dict(frozen=True))

    def test_main_forwards_export_and_receiver_options_and_keeps_exported_identity(self):
        export_calls, study_options, score_options = [], [], []

        class FakeComparison:
            def __init__(self, fixture, templates, **kwargs):
                study_options.append(kwargs)
                self.procedure_id = 'mock-procedure'
                self.template_hash = 'mock-template-hash'
                self.sources = {'mock-source': 'hash'}
                self.preparation_seconds = .25

            def score(self, folder, *, include_baseline=True):
                score_options.append(include_baseline)
                manifest = json.loads((folder / 'manifest.json').read_text())
                self.asserted_metadata = (manifest['channel']['nominal_conversion_if_hz'],
                                          manifest['channel']['ramp_seconds'])
                baseline = {'receiver_id': 'baseline', 'bit': 0, 'working_presence': 1.,
                                 'bit_log_likelihoods': [1., 0.], 'opposite_bit_margin': 1.,
                                 'best_epoch_offset_seconds': 0,
                                 'best_group_public_parameters': {'frequency_hz': .1},
                                 'timing': {'total_score_prepared_seconds': .05},
                                 'arithmetic': {}, 'resources': {},
                                 'geometry': {'epoch_offsets': [0], 'lag_samples': [0],
                                              'frequency_offsets_hz': [.1, .2], 'samples': 10,
                                              'effective_group_count': 2}}
                return {
                    'schema': 'mock-comparison', 'procedure_id': self.procedure_id,
                    'executed_arms': ['baseline', 'assisted'] if include_baseline else ['assisted'],
                    'input_sha256': 'input-hash', 'template_sha256': self.template_hash,
                    'source_hashes': self.sources,
                    'assistance': {'status': 'selected', 'reason': 'mock', 'selected_count': 2,
                                   'selected_frequencies': [.1, .2], 'expanded_intervals_hz': [[.1, .2]]},
                    'timing': {'command_score_seconds': .1, 'helper_seconds': .02,
                               'additional_assisted_scoring_seconds': .01 if include_baseline else None,
                               **({} if include_baseline else {'assisted_score_seconds': .01})},
                    'limits': [],
                    'baseline': baseline if include_baseline else None,
                    'assisted': {'receiver_id': 'assisted', 'bit': 0, 'working_presence': 1.1,
                                 'bit_log_likelihoods': [1.1, 0.], 'opposite_bit_margin': 1.1,
                                 'best_epoch_offset_seconds': 0,
                                 'best_group_public_parameters': {'frequency_hz': .1},
                                 'timing': {'total_score_prepared_seconds': .06},
                                 'arithmetic': {}, 'resources': {},
                                 'geometry': {'epoch_offsets': [0], 'lag_samples': [0],
                                              'frequency_offsets_hz': [.1], 'samples': 10,
                                              'effective_group_count': 1}},
                }

        def mock_export(command, **kwargs):
            export_calls.append(command)
            output = Path(command[command.index('--output') + 1])
            output.mkdir(parents=True)
            data_if = float(command[command.index('--data-if') + 1])
            ramp = (float(command[command.index('--ramp-seconds') + 1])
                    if '--ramp-seconds' in command else .006)
            (output / 'obs.c64').write_bytes(b'fixture observation')
            (output / 'valid.u8').write_bytes(b'\x01')
            (output / 'manifest.json').write_text(json.dumps({
                'channel': {'nominal_conversion_if_hz': data_if, 'ramp_seconds': ramp,
                            'rf_hz': 5.8e9},
                'validity': {'file': 'valid.u8'},
                'observation': {'file': 'obs.c64'},
            }))
            (output / 'evaluator-truth.json').write_text(json.dumps({
                'tx': {'audio_ppm': 0., 'rf_ppm': 0.}, 'rx': {'audio_ppm': 0., 'rf_ppm': 0.},
                'source_bit': 0, 'arrival_seconds': 0., 'source_energy': {}, 'channel_report': {},
            }))
            return subprocess.CompletedProcess(command, 0)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            templates = root / 'templates'
            templates.mkdir()
            (templates / 'templates.bin').write_bytes(b'public templates')
            (templates / 'manifest.json').write_text(json.dumps({
                'templates': {'enabled': True, 'file': 'templates.bin'},
                'key_seed': 19, 'public_epoch_seconds': 0,
                'channel': {'nominal_conversion_if_hz': 4000.},
            }))
            exporter = root / 'mock-exporter'
            exporter.write_text('mock')
            output = root / 'campaign'
            with patch.object(campaign, 'public_clock_grids', return_value=((0.,), (1.,))) as grids:
                campaign.main([
                    '--case', 'eme', '--templates', str(templates), '--output', str(output),
                    '--exporter', str(exporter), '--seed-base', '94300', '--calibration-count', '1',
                    '--signal-count', '0', '--null-count', '0', '--data-if', '1725',
                    '--ramp-seconds', '.006', '--fft-backend', 'batched',
                    '--shaping-tail-fraction', '.001', '--rate', '360',
                    '--rf-hz', '500000', '--base-oscillator', 'crystal', '--shift-oscillator', 'ic-7100',
                    '--spread-preset', 'hf-worst', '--margin', '3', '--selective-perturbation', 'each-65',
                    '--selective-modulation-rate-hz', '0', '--timing-policy', 'bounded',
                    '--data-power-w', '4', '--helper-power-w', '40',
                    '--added-symbols', '10', '--fhss', 'on', '--burst-wait-tiles', '3',
                    '--frequency-batch-size', '4', '--max-cached-hypotheses', '13',
                    '--memory-limit-bytes', str(8 * 1024**3), '--frontend-profile', 'wide',
                    '--clock-step-ppm', '.1', '--doppler-scale-count', '3', '--aligned-fft-grid',
                    '--lag-step', '4',
                ], comparison_factory=FakeComparison, export_runner=mock_export)

            self.assertEqual(len(export_calls), 1)
            self.assertEqual(score_options, [True])
            command = export_calls[0]
            self.assertEqual(command[command.index('--data-if') + 1], '1725.0')
            self.assertEqual(command[command.index('--ramp-seconds') + 1], '0.006')
            self.assertEqual(command[command.index('--shaping') + 1], 'auto')
            self.assertEqual(command[command.index('--shaping-tail-fraction') + 1], '0.001')
            for flag, value in (
                ('--rf-hz', '500000.0'), ('--base-oscillator', 'crystal'),
                ('--shift-oscillator', 'ic-7100'), ('--spread-preset', 'hf-worst'),
                ('--margin', '3.0'), ('--selective-perturbation', 'each-65'),
                ('--selective-modulation-rate-hz', '0.0'), ('--timing-policy', 'bounded'),
                ('--data-power-w', '4.0'), ('--helper-power-w', '40.0')):
                self.assertEqual(command[command.index(flag) + 1], value)
            self.assertEqual(command[command.index('--rate') + 1], '360.0')
            self.assertEqual(command[command.index('--added-symbols') + 1], '10')
            self.assertEqual(command[command.index('--fhss') + 1], 'on')
            self.assertEqual(command[command.index('--burst-wait-tiles') + 1], '3')
            self.assertEqual(study_options, [{'device': 'cpu', 'precision': 'fp32',
                'fft_backend': 'batched', 'frequency_batch_size': 4,
                'max_cached_hypotheses': 13, 'memory_limit_bytes': 8 * 1024**3,
                'frontend_profile': 'wide', 'clock_ppm_values': (0.,),
                'doppler_scale_values': (1.,), 'aligned_fft_grid': True, 'lag_step': 4}])
            grids.assert_called_once_with(json.loads((output / 'calibration-0000' / 'manifest.json').read_text()),
                                          .1, 3)
            protocol = json.loads((output / 'protocol.json').read_text())
            self.assertEqual(protocol['public_overrides']['data_if_hz'], 1725.)
            self.assertEqual(protocol['public_overrides']['ramp_seconds'], .006)
            self.assertEqual(protocol['public_overrides']['shaping'], 'auto')
            self.assertEqual(protocol['public_overrides']['shaping_tail_fraction'], .001)
            for key, value in dict(rf_hz=500000., base_oscillator='crystal',
                    shift_oscillator='ic-7100', spread_preset='hf-worst', margin=3.,
                    selective_perturbation='each-65', selective_modulation_rate_hz=0.,
                    timing_policy='bounded', data_power_w=4., helper_power_w=40.).items():
                self.assertEqual(protocol['public_overrides'][key], value)
            self.assertEqual(protocol['public_overrides']['rate_hz'], 360.)
            self.assertEqual(protocol['public_overrides']['added_symbols'], 10)
            self.assertEqual(protocol['public_overrides']['fhss'], 'on')
            self.assertEqual(protocol['public_overrides']['burst_wait_tiles'], 3)
            row = json.loads((output / 'trials.jsonl').read_text())
            self.assertEqual(row['manifest_sha256'], campaign.digest(output / 'calibration-0000' / 'manifest.json'))
            self.assertEqual(row['export_command'], command)

            assisted_output = root / 'assisted-only'
            campaign.main([
                '--case', 'eme', '--templates', str(templates), '--output', str(assisted_output),
                '--exporter', str(exporter), '--seed-base', '94400', '--calibration-count', '1',
                '--signal-count', '1', '--null-count', '1', '--assisted-only', '--alpha', '.5',
            ], comparison_factory=FakeComparison, export_runner=mock_export)
            self.assertEqual(score_options, [True, False, False, False])
            assisted_protocol = json.loads((assisted_output / 'protocol.json').read_text())
            self.assertEqual(assisted_protocol['executed_arms'], ['assisted'])
            assisted_gate = json.loads((assisted_output / 'gate.json').read_text())
            self.assertEqual(assisted_gate['arms'], ['assisted'])
            self.assertEqual(set(assisted_gate['scores']), {'assisted'})
            assisted_summary = json.loads((assisted_output / 'summary.json').read_text())
            self.assertIsNone(assisted_summary['timing']['baseline_score_seconds'])
            self.assertIsNone(assisted_summary['timing']['extra_assisted_score_seconds'])
            self.assertEqual(assisted_summary['timing']['assisted_score_seconds'], .03)
            trial_rows = [json.loads(line) for line in
                          (assisted_output / 'trials.jsonl').read_text().splitlines()]
            self.assertTrue(all(row['executed_arms'] == ['assisted'] for row in trial_rows))
            self.assertTrue(all('baseline' not in row for row in trial_rows))
            self.assertTrue(all(set(row.get('decisions', {})) <= {'assisted'} for row in trial_rows))
            with self.assertRaisesRegex(ValueError, 'not calibrated'):
                decide(trial_rows[1], assisted_gate, 'baseline')


if __name__ == '__main__':
    unittest.main()
