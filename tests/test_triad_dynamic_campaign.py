#!/usr/bin/env python3
"""Cheap campaign contracts; no native simulation or sensitivity claim."""
from __future__ import annotations

import contextlib
import io
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
from scipy.special import logsumexp

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import triad_dynamic_campaign as campaign


class CampaignContracts(unittest.TestCase):
    def test_schedule_and_reserved_arguments(self):
        rows = campaign.schedule(13, 3, 4, 2)
        self.assertEqual([r['seed'] for r in rows], list(range(13, 22)))
        self.assertEqual([r['bit'] for r in rows if r['phase'] == 'signal'], [0, 1, 0, 1])
        self.assertTrue(all(r['data'] == (r['phase'] == 'signal') for r in rows))
        with self.assertRaises(ValueError):
            campaign.schedule(2**64-2, 3, 0, 0)
        for value in ('--seed 5', '--seed=5', '--templates', '--plan-only', '--whole-bit-count 2'):
            with self.assertRaises(ValueError):
                campaign.export_arguments([value], 'conditioned')
        args = campaign.export_arguments(['--case hf --helper off'], 'conditioned')
        self.assertEqual(args[-4:], ['--arrival-seconds', '0', '--oscillator-effects', 'off'])
        # This is one literal argument, never a shell command.
        self.assertEqual(campaign.export_arguments(["--case '$(not-a-shell)'"], 'public-bank'),
                         ['--case', '$(not-a-shell)'])

    def test_mixture_is_ratio_of_marginals(self):
        evidence = np.array([[[10., 11., 8.]], [[-10., -2., -9.]], [[3., 4., 2.]]])
        arms = campaign.paired_arms(evidence, (.1, .5, math.inf))[0]
        expected = logsumexp(evidence[:2, 0, :], axis=0) - math.log(2)
        np.testing.assert_allclose(arms['dynamic']['bit_log_likelihoods'], expected[1:] - expected[0])
        wrong_mean_of_ratios = logsumexp(evidence[:2, 0, 1] - evidence[:2, 0, 0]) - math.log(2)
        self.assertGreater(abs(arms['dynamic']['bit_log_likelihoods'][0]-wrong_mean_of_ratios), 3)
        self.assertEqual(arms['static']['bit_log_likelihoods'], [1., -1.])
        with self.assertRaises(ValueError):
            campaign.paired_arms(np.zeros((2, 1, 2)), (.1, math.inf))

    def test_rank_ties_identity_and_resolution(self):
        rows = [dict(trial={'phase': 'calibration'}, procedure_id='p', condition_id='c',
                     static={'working_presence': x}, dynamic={'working_presence': x}) for x in (1., 2., 3.)]
        gate = campaign.freeze_gate(rows, 'p', 'c', .25)
        row = dict(procedure_id='p', condition_id='c', static={'working_presence': 3., 'bit': 0})
        self.assertFalse(campaign.decide(row, gate, 'static')['accepted'])
        row['static']['working_presence'] = 3.00001
        self.assertTrue(campaign.decide(row, gate, 'static')['accepted'])
        row['condition_id'] = 'another-helper-policy'
        with self.assertRaises(ValueError):
            campaign.decide(row, gate, 'static')
        with self.assertRaises(ValueError):
            campaign.freeze_gate(rows, 'p', 'c', .1)

    def test_custom_factory_effective_identity_binds_the_calibrated_gate(self):
        # Same requested configuration and declared sources, different resolved
        # factory procedure (for example a different actual projection).
        configuration = dict(block_seconds=.25, coherences=[.1, 'inf'], pilot_use='both',
            acquisition='conditioned', arrivals=[0.], frequencies=[0.], clock_ppm=[0.],
            epoch_offsets=[0], device='cpu', precision='fp32', execution='serial',
            projection='full-envelope')
        def factory(resolved):
            class Receiver:
                procedure_id = resolved
            return Receiver()
        common = dict(geometry={'helper_enabled': True}, configuration=configuration,
                      template_sha256='fixed-template', sources={'factory.py': 'fixed-source'})
        a = campaign.calibrated_procedure_identity(factory('actual-full-bins'), **common)
        b = campaign.calibrated_procedure_identity(factory('actual-core-bins'), **common)
        self.assertNotEqual(a, b)
        self.assertEqual(a, campaign.calibrated_procedure_identity(factory('actual-full-bins'), **common))
        for missing in (None, '', ' '):
            with self.assertRaisesRegex(ValueError, 'effective procedure_id'):
                campaign.calibrated_procedure_identity(factory(missing), **common)
        rows = [dict(trial={'phase': 'calibration'}, procedure_id=a, condition_id='fixed-condition',
                     static={'working_presence': x}, dynamic={'working_presence': x}) for x in (1., 2., 3.)]
        gate = campaign.freeze_gate(rows, a, 'fixed-condition', .25)
        candidate = dict(procedure_id=b, condition_id='fixed-condition',
                         static={'working_presence': 100., 'bit': 0})
        with self.assertRaisesRegex(ValueError, 'gate identity'):
            campaign.decide(candidate, gate, 'static')

    def test_small_complete_mock_run_freezes_before_exports(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            binary = base / 'fake-exporter'
            binary.write_bytes(b'fake-test-only-binary')
            output = base / 'campaign'
            exported, scored, template_counts = {}, set(), []

            def exporter(command, **kwargs):
                self.assertTrue((output / 'protocol.json').is_file())
                self.assertTrue(kwargs['check'])
                self.assertGreater(kwargs['timeout'], 0)
                def value(flag):
                    return command[command.index(flag)+1]
                folder = Path(value('--output'))
                folder.mkdir()
                seed, bit = int(value('--seed')), int(value('--bit'))
                data = value('--data') == 'on'
                exported[folder] = seed
                if folder.name.startswith(('signal', 'null')):
                    self.assertTrue((output / 'gate.json').is_file())
                raw = np.zeros(8, np.complex128)
                raw[0], raw[1] = (bit+1 if data else 0), seed
                raw.tofile(folder / 'observation.c128')
                np.ones(8, np.uint8).tofile(folder / 'validity.u8')
                templates_enabled = '--templates' in command
                template_counts.append(templates_enabled)
                if templates_enabled:
                    np.ones((1, 2, 4), np.complex128).tofile(folder / 'templates.c128')
                manifest = dict(schema='triad-helper-public-v1', sample_rate_hz=8,
                    format={'bit_seconds': 1}, calendar={'capture_samples': 8}, helper_plan={},
                    channel={}, helper_enabled=True, data_enabled=data, public_epoch_seconds=1000, key_seed=77,
                    power={'template_data_gain': 1., 'data_cn0_db_hz': 8., 'complex_noise_variance': 1.},
                    observation={'shape': [8], 'file': 'observation.c128'},
                    validity={'file': 'validity.u8'}, evaluator_truth_file='evaluator-truth.json',
                    templates={'enabled': templates_enabled, 'file': 'templates.c128' if templates_enabled else None,
                               'shape': [1, 2, 4], 'epoch_offsets_seconds': [0]})
                (folder / 'manifest.json').write_text(json.dumps(manifest))
                truth = dict(source_bit=bit, arrival_seconds=0.,
                    source_energy={'data_energy_samples': 8 if data else 0, 'helper_energy_samples': 80},
                    channel_report={'samples': 8, 'signal_energy_samples': 88 if data else 80})
                (folder / 'evaluator-truth.json').write_text(json.dumps(truth))

            class Receiver:
                def __init__(self, manifest, templates, epochs, **kwargs):
                    self.coherences = kwargs['coherence_seconds']
                    self.valid = np.asarray(kwargs['valid'], bool)
                    self.procedure_id = 'fake-public-procedure'
                    self.calls = 0
                    self.asserted_template_shape = templates.shape
                def work(self):
                    return {'test_only': True}
                def score(self, raw):
                    self.calls += 1
                    scores = np.zeros((len(self.coherences), len(raw), 3))
                    for i, row in enumerate(raw):
                        code, seed = int(row[0].real), int(row[1].real)
                        scored.add(seed)
                        if code:
                            for model, tau in enumerate(self.coherences):
                                selected = 1 if math.isinf(tau) else code
                                scores[model, i, selected] = 10
                    return dict(hypothesis_log_evidence=scores,
                        model_bit_scores=scores[:, :, 1:] - scores[:, :, :1],
                        correlation_seconds=.001, state_seconds=.002, work={'fake': True})

            original_evaluator = campaign.evaluator_metrics
            def guarded_evaluator(folder, manifest):
                self.assertIn(exported[Path(folder)], scored)
                return original_evaluator(folder, manifest)

            arguments = ['--exporter', str(binary), '--output', str(output), '--seed-base', '10',
                '--calibration-count', '3', '--signal-count', '4', '--null-count', '2', '--alpha', '.25',
                '--coherences', '.5', 'inf', '--batch-size', '2', '--export-workers', '2', '--discard-iq']
            with patch.object(campaign, 'evaluator_metrics', guarded_evaluator), contextlib.redirect_stdout(io.StringIO()):
                summary = campaign.main(arguments, receiver_factory=Receiver, export_runner=exporter)
            self.assertEqual(summary['completed_trials'], 9)
            self.assertEqual(sum(template_counts), 1)
            self.assertEqual(summary['outcomes']['dynamic']['signal']['counts']['correct_admitted'], 4)
            self.assertEqual(summary['outcomes']['static']['signal']['counts']['wrong_admitted'], 2)
            self.assertEqual(summary['outcomes']['dynamic']['null']['false_admitted'], 0)
            self.assertEqual(summary['paired']['static_then_dynamic']['wrong / correct'], 2)
            prepared = json.loads((output/'prepared.json').read_text())
            protocol = json.loads((output/'protocol.json').read_text())
            self.assertEqual(prepared['receiver_procedure_id'], 'fake-public-procedure')
            self.assertEqual(prepared['identity_schema'], 'triad-dynamic-calibrated-procedure-v2')
            self.assertEqual(protocol['configuration']['execution'], 'serial')
            self.assertEqual(protocol['configuration']['projection'], 'full-envelope')
            self.assertIn('caller-supplied', protocol['receiver_factory'])
            self.assertIn('tools/triad_dynamic_variants.py', protocol['source_hashes'])
            self.assertIn('tools/triad_dynamic_scan.py', protocol['source_hashes'])
            self.assertTrue((output / 'summary.json').is_file())
            self.assertFalse(any(output.glob('*/observation.c128')))
            self.assertEqual(len(list(output.glob('*/templates.c128'))), 1)
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                campaign.main(arguments, receiver_factory=Receiver, export_runner=exporter)

    def test_geometry_binds_helper_and_noise_not_source_presence(self):
        m = {key: {} for key in ('schema', 'sample_rate_hz', 'format', 'calendar', 'helper_plan',
                                'channel', 'power', 'public_epoch_seconds', 'key_seed')}
        m.update(helper_enabled=True, data_enabled=False)
        expected = campaign.public_geometry(m)
        m['data_enabled'] = True
        self.assertEqual(expected, campaign.public_geometry(m))
        m['helper_enabled'] = False
        self.assertNotEqual(expected, campaign.public_geometry(m))
        m['helper_enabled'] = True
        m['helper_component_control'] = 'pilots'
        self.assertNotEqual(expected, campaign.public_geometry(m))

    def test_core_requires_explicit_scan_before_any_export(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary)/'not-used'
            binary.write_bytes(b'test-only')
            output = Path(temporary)/'not-created'
            arguments = ['--exporter', str(binary), '--output', str(output), '--seed-base', '10',
                         '--projection', 'core']
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                campaign.main(arguments)
            self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
