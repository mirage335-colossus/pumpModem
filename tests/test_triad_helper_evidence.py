#!/usr/bin/env python3
"""Tiny saved-file integrity checks; no native simulator, sampled arrays or DSP."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from triad_helper_evidence import digest, read_campaign


class HelperEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.rows = []
        for index, (phase, bit, score) in enumerate((
                ('calibration', 0, 0.), ('signal', 0, 2.),
                ('signal', 1, 3.), ('null', 0, -1.))):
            trial = dict(id=f'{phase}-{index}', phase=phase, bit=bit,
                         seed=7000 + index, data=phase == 'signal')
            directory = self.folder / trial['id']
            directory.mkdir()
            self.write_json(directory / 'manifest.json', dict(
                helper_enabled=True, data_enabled=trial['data'], case='hf', control='selected'))
            self.write_json(directory / 'evaluator-truth.json', dict(source_bit=bit))
            row = dict(trial=trial, procedure_id='frozen-adaptive-procedure',
                manifest_sha256=digest(directory / 'manifest.json'),
                truth_sha256=digest(directory / 'evaluator-truth.json'),
                input_sha256='synthetic-IQ-hash-no-array-required', validity_sha256='synthetic-validity-hash',
                evaluator=dict(source_bit=bit, static_cfo_in_expanded_interval=True,
                               baseline_best_cfo_retained=True),
                assistance=dict(selected_count=3, selected_frequencies=[-.01, 0., .01], reason='no_bank_shrink'))
            for arm in ('baseline', 'assisted'):
                row[arm] = dict(working_presence=score, bit=bit,
                                timing=dict(total_score_prepared_seconds=.01))
            if phase != 'calibration':
                p = .5 if score > 0 else 1.
                row['decisions'] = {arm: dict(p_value=p, accepted=p <= .5, bit=bit)
                                    for arm in ('baseline', 'assisted')}
            self.rows.append(row)
        self.protocol = dict(configuration=dict(helper='on', case='hf', control='selected'),
                             trial_schedule=[row['trial'] for row in self.rows])
        self.gate = dict(procedure_id='frozen-adaptive-procedure', alpha=.5,
                         scores=dict(baseline=[0.], assisted=[0.]))
        self.write_json(self.folder / 'protocol.json', self.protocol)
        self.write_json(self.folder / 'gate.json', self.gate)
        self.original = dict(protocol_sha256=digest(self.folder / 'protocol.json'),
                             gate_sha256=digest(self.folder / 'gate.json'),
                             timing=dict(campaign_wall_seconds=.1))
        self.write_json(self.folder / 'summary.json', self.original)
        self.write_rows()

    @staticmethod
    def write_json(path, value):
        path.write_text(json.dumps(value) + '\n')

    def write_rows(self):
        (self.folder / 'trials.jsonl').write_text(''.join(json.dumps(row) + '\n' for row in self.rows))

    def test_saved_campaign_verified_and_stratified_without_dsp(self):
        result = read_campaign(self.folder)
        self.assertEqual(len(result['compact_trials']), 4)
        for arm in ('baseline', 'assisted'):
            outcomes = result['outcomes'][arm]
            self.assertEqual(outcomes['signal']['signal_trials'], 2)
            self.assertEqual(outcomes['signal']['counts']['correct_admitted'], 2)
            self.assertEqual(outcomes['signal']['per_bit']['0']['trials'], 1)
            self.assertEqual(outcomes['signal']['per_bit']['1']['trials'], 1)
            self.assertTrue(outcomes['signal']['balanced_population']['correct_admitted']['qualified'])
            self.assertEqual(outcomes['false_admission']['count'], 0)
            self.assertEqual(outcomes['false_admission']['trials'], 1)
        self.assertEqual(result['signal_frequency_banks'], [3, 3])
        self.assertEqual(result['signal_baseline_best_cfo_retained'], 2)
        self.assertEqual(result['timing'], self.original['timing'])

    def test_mutated_evaluator_bit_disagrees_with_retained_truth(self):
        self.rows[1]['evaluator']['source_bit'] = 1
        self.write_rows()
        with self.assertRaisesRegex(ValueError, 'retained truth and scheduled source bit differ'):
            read_campaign(self.folder)

    def test_helper_condition_mismatch_rejected_even_with_updated_manifest_hash(self):
        row = self.rows[2]
        path = self.folder / row['trial']['id'] / 'manifest.json'
        manifest = json.loads(path.read_text())
        manifest['helper_enabled'] = False
        self.write_json(path, manifest)
        row['manifest_sha256'] = digest(path)
        self.write_rows()
        with self.assertRaisesRegex(ValueError, 'calibration condition differs'):
            read_campaign(self.folder)

    def test_changed_rank_decision_rejected(self):
        self.rows[1]['decisions']['assisted']['accepted'] = False
        self.write_rows()
        with self.assertRaisesRegex(ValueError, 'frozen conservative rank rule'):
            read_campaign(self.folder)

    def test_incomplete_schedule_rejected(self):
        self.rows.pop()
        self.write_rows()
        with self.assertRaisesRegex(ValueError, 'incomplete or changed frozen trial schedule'):
            read_campaign(self.folder)

    def test_protocol_hash_mismatch_rejected(self):
        # Semantics are unchanged; the retained full protocol identity still matters.
        with (self.folder / 'protocol.json').open('a') as stream:
            stream.write(' ')
        with self.assertRaisesRegex(ValueError, 'protocol/gate identity changed'):
            read_campaign(self.folder)

    def test_changed_gate_rejected_even_when_summary_hash_updated(self):
        self.gate['scores']['baseline'] = [1.]
        self.write_json(self.folder / 'gate.json', self.gate)
        self.original['gate_sha256'] = digest(self.folder / 'gate.json')
        self.write_json(self.folder / 'summary.json', self.original)
        with self.assertRaisesRegex(ValueError, 'gate differs from calibration outcomes'):
            read_campaign(self.folder)


if __name__ == '__main__':
    unittest.main()
