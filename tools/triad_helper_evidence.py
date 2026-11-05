#!/usr/bin/env python3
"""Verify and summarize retained Helper campaigns without rerunning DSP."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import statistics

from triad_helper_statistics import signal_outcome_statistics, binomial_interval


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_campaign(folder):
    folder = Path(folder)
    protocol = json.loads((folder / 'protocol.json').read_text())
    gate = json.loads((folder / 'gate.json').read_text())
    original = json.loads((folder / 'summary.json').read_text())
    rows = [json.loads(line) for line in (folder / 'trials.jsonl').read_text().splitlines()]
    if [row['trial'] for row in rows] != protocol['trial_schedule']:
        raise ValueError('incomplete or changed frozen trial schedule')
    if original['protocol_sha256'] != digest(folder / 'protocol.json') or original['gate_sha256'] != digest(folder / 'gate.json'):
        raise ValueError('protocol/gate identity changed')
    calibration = [row for row in rows if row['trial']['phase'] == 'calibration']
    for arm in ('baseline', 'assisted'):
        if gate['scores'][arm] != [row[arm]['working_presence'] for row in calibration]:
            raise ValueError('frozen gate differs from calibration outcomes')
    for row in rows:
        if row['procedure_id'] != gate['procedure_id']:
            raise ValueError('receiver procedure changed')
        trial = folder / row['trial']['id']
        for filename, key in (('manifest.json', 'manifest_sha256'), ('evaluator-truth.json', 'truth_sha256')):
            if digest(trial / filename) != row[key]:
                raise ValueError(f'{filename} changed')
        manifest = json.loads((trial / 'manifest.json').read_text())
        truth = json.loads((trial / 'evaluator-truth.json').read_text())
        if not (row['evaluator']['source_bit'] == truth['source_bit'] == row['trial']['bit']):
            raise ValueError('stored evaluator, retained truth and scheduled source bit differ')
        configuration = protocol['configuration']
        if (manifest['helper_enabled'] != (configuration['helper'] == 'on') or
                manifest['data_enabled'] != row['trial']['data'] or
                manifest['case'] != configuration['case'] or
                manifest['control'] != configuration['control']):
            raise ValueError('capture calibration condition differs from frozen protocol')
        if row['trial']['phase'] == 'calibration':
            continue
        for arm in ('baseline', 'assisted'):
            p = (1 + sum(x >= row[arm]['working_presence'] for x in gate['scores'][arm])) / (len(calibration) + 1)
            decision = row['decisions'][arm]
            if decision['p_value'] != p or decision['accepted'] != (p <= gate['alpha']) or decision['bit'] != row[arm]['bit']:
                raise ValueError('stored admission differs from frozen conservative rank rule')
    signal = [row for row in rows if row['trial']['phase'] == 'signal']
    nulls = [row for row in rows if row['trial']['phase'] == 'null']
    outcomes = {}
    for arm in ('baseline', 'assisted'):
        outcomes[arm] = dict(signal=signal_outcome_statistics(rows, arm),
            false_admission=binomial_interval(sum(row['decisions'][arm]['accepted'] for row in nulls), len(nulls)))
    return dict(name=folder.name, protocol=protocol, gate=gate,
        raw_summary_sha256=digest(folder / 'summary.json'), trials_sha256=digest(folder / 'trials.jsonl'),
        outcomes=outcomes, timing=original['timing'],
        signal_frequency_banks=[row['assistance']['selected_count'] for row in signal],
        signal_helper_reasons=[row['assistance']['reason'] for row in signal],
        signal_static_cfo_intervals_covered=sum(row['evaluator']['static_cfo_in_expanded_interval'] for row in signal),
        signal_baseline_best_cfo_retained=sum(row['evaluator']['baseline_best_cfo_retained'] is True for row in signal),
        median_signal_score_seconds={arm: statistics.median(row[arm]['timing']['total_score_prepared_seconds'] for row in signal)
                                     for arm in ('baseline', 'assisted')} if signal else {},
        compact_trials=[dict(trial=row['trial'], decisions=row.get('decisions'),
            input_sha256=row['input_sha256'], manifest_sha256=row['manifest_sha256'],
            truth_sha256=row['truth_sha256'], validity_sha256=row['validity_sha256'],
            procedure_id=row['procedure_id'],
            working_presence={arm: row[arm]['working_presence'] for arm in ('baseline', 'assisted')},
            selected_frequencies=row['assistance']['selected_frequencies'],
            helper_reason=row['assistance']['reason'], evaluator=row['evaluator']) for row in rows],
        uncertainty_correction='Signal outcomes use fixed-bit stratification; old pooled signal CP intervals '
            'in raw v1 summaries are superseded. Null intervals remain binomial. Conditions/arms are not independent pooled trials.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('campaigns', nargs='+', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = dict(schema='triad-helper-verified-evidence-v1',
        report_source_sha256=digest(__file__),
        statistics_source_sha256=digest(Path(__file__).with_name('triad_helper_statistics.py')),
        campaigns=[read_campaign(folder) for folder in args.campaigns])
    with args.output.open('x') as stream:
        json.dump(result, stream, indent=2, allow_nan=False)
        stream.write('\n')


if __name__ == '__main__':
    main()
