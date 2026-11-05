#!/usr/bin/env python3
"""Paired AM90 frequency diagnostic; freeze every estimator output before truth."""
from __future__ import annotations

import argparse
from collections import Counter
import gzip
import json
import math
from pathlib import Path
import sys
import time

from diagnostic import ROOT, digest, write_json
sys.path.insert(0, str(ROOT/'tools'))
import numpy as np
from scipy.stats import beta
from triad_dynamic_frequency import estimate_dynamic_frequency, frequency_policy


def interval(successes, count):
    if count == 0:
        return None
    return [0. if successes == 0 else float(beta.ppf(.025, successes, count-successes+1)),
            1. if successes == count else float(beta.ppf(.975, successes+1, count-successes))]


def run(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--captures', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    start = time.perf_counter()
    if args.output.exists():
        parser.error('output must be new; no retry or overwrite')
    plan_path = args.captures/'protocol.json'
    plan = json.loads(plan_path.read_text())
    generation = json.loads((args.captures/'generation.json').read_text())
    source = ROOT/'tools/triad_dynamic_frequency.py'
    if digest(source) != plan['estimator_source_sha256']:
        raise ValueError('estimator source changed since the preregistered AM90 plan')
    if generation['completed'] != 16 or generation['protocol_sha256'] != digest(plan_path):
        raise ValueError('complete source-bound16-capture generation is required')
    if generation['exports_sha256'] != digest(args.captures/'exports.jsonl'):
        raise ValueError('native export receipts changed')
    exports = [json.loads(line) for line in (args.captures/'exports.jsonl').read_text().splitlines()]
    by_id = {entry['trial']['id']: entry for entry in exports}
    loaded = []
    for trial in plan['schedule']:
        folder = args.captures/trial['id']
        m = json.loads((folder/'manifest.json').read_text())
        # No evaluator file is opened here. Public receipt checks precede all scoring.
        hashes = {name: digest(folder/name) for name in ('manifest.json', m['observation']['file'], m['validity']['file'])}
        if any(by_id[trial['id']]['input_hashes'][name] != value for name, value in hashes.items()):
            raise ValueError('capture input differs from frozen native receipt')
        loaded.append((trial, folder, m, hashes))
    configs = plan['frequency_policies']
    policies = {name: frequency_policy(config) for name, config in configs.items()}
    args.output.mkdir(parents=True)
    scoring_protocol = dict(schema='triad-am90-paired-frequency-diagnostic-v1',
        capture_protocol_sha256=digest(plan_path), generation_receipt_sha256=digest(args.captures/'generation.json'),
        source_hashes={'tools/triad_dynamic_frequency.py': digest(source),
            str(Path(__file__).resolve().relative_to(ROOT)): digest(__file__)},
        policies=policies, original_bank_rule='endpoint-inclusive +/-public deterministic CFO bound*margin; half-count ceil(bound/.5Hz)',
        trial_inputs=[dict(trial=trial, input_hashes=hashes) for trial, _, _, hashes in loaded],
        read_order='Write all32 complete frequency results, close profile file and freeze its hash before opening any evaluator truth.',
        limits=['Paired16-capture diagnostic, not independent frequency-region calibration or receiver qualification.',
            'Default and opt-in allowances are separate policies; no tuning after inspecting these outcomes.',
            'Coverage refers to static direct-ray CFO and its nearest original bank coordinate, not instantaneous Doppler or phase.',
            'Full-bank fallback counts separately; effective interval coverage credits the full public prior on fallback.',
            'Small-sample Clopper-Pearson intervals describe this fixed diagnostic distribution, not arbitrary field channels.'])
    write_json(args.output/'protocol.json', scoring_protocol)
    profiles_path = args.output/'profiles.jsonl.gz'
    results = []
    with gzip.open(profiles_path, 'xt') as stream:
        for trial, folder, m, hashes in loaded:
            raw = np.memmap(folder/m['observation']['file'], dtype='<c16', mode='r')
            valid = np.memmap(folder/m['validity']['file'], dtype='u1', mode='r')
            bound = m['channel']['deterministic_cfo_bound_hz']*m['channel']['spread_margin']
            bank = np.linspace(-bound, bound, 2*math.ceil(bound/.5)+1)
            for name, config in configs.items():
                value = estimate_dynamic_frequency(raw, m, bank, valid=valid, config=config)
                row = dict(trial_id=trial['id'], geometry=trial['timing_policy'], policy_name=name,
                    frequency_estimate=value)
                stream.write(json.dumps(row, allow_nan=False)+'\n')
                results.append(row)
            print(json.dumps(dict(frequency_outputs=len(results), total=32)), flush=True)
    if digest(source) != scoring_protocol['source_hashes']['tools/triad_dynamic_frequency.py']:
        raise ValueError('estimator source changed while scoring')
    freeze = dict(profile_sha256=digest(profiles_path), complete_outputs=len(results),
        scoring_protocol_sha256=digest(args.output/'protocol.json'), evaluator_truth_read=False,
        wall_seconds=time.perf_counter()-start)
    write_json(args.output/'outputs-frozen-before-truth.json', freeze)
    # Only after every policy/capture outcome is immutable do evaluator draws enter.
    truths = {}
    for trial, folder, m, _ in loaded:
        path = folder/m['evaluator_truth_file']
        truth = json.loads(path.read_text())
        tx, rx, ch = truth['tx'], truth['rx'], m['channel']
        cfo = ((tx['rf_ppm']-rx['rf_ppm'])*ch['rf_hz']+
               (tx['audio_ppm']-rx['audio_ppm'])*ch['nominal_conversion_if_hz'])*1e-6/(1+rx['audio_ppm']*1e-6)
        truths[trial['id']] = dict(truth_sha256=digest(path), static_cfo_hz=cfo)
    evaluation = []
    for row in results:
        r, truth = row['frequency_estimate'], truths[row['trial_id']]
        bank = np.asarray(r['original_frequencies_hz'])
        cfo = truth['static_cfo_hz']
        nearest = float(bank[np.argmin(abs(bank-cfo))])
        narrowed = r['status'] == 'narrowed'
        reported = any(lo <= cfo <= hi for lo, hi in r['expanded_intervals_hz'])
        effective = reported if narrowed else float(bank.min()) <= cfo <= float(bank.max())
        evaluation.append(dict(trial_id=row['trial_id'], geometry=row['geometry'], policy_name=row['policy_name'],
            **truth, nearest_original_frequency_hz=nearest,
            nearest_original_bin_retained=bool(np.any(np.asarray(r['selected_frequencies']) == nearest)),
            reported_expanded_interval_covers_static_cfo=reported,
            effective_search_interval_covers_static_cfo=bool(effective),
            status=r['status'], reason=r['reason'], selected_count=r['selected_count'], original_count=r['original_count'],
            estimator_seconds=r['wall_seconds'], possible_data_overlap=r.get('overlaps_possible_data'),
            allowances_hz=r.get('allowances_hz'), usable_pilots=r.get('usable_pilots', [])))
    write_json(args.output/'evaluator-results.json', evaluation)
    groups = []
    for geometry in ('bounded', 'broad'):
        for name in configs:
            rows = [r for r in evaluation if r['geometry'] == geometry and r['policy_name'] == name]
            narrow = [r for r in rows if r['status'] == 'narrowed']
            counts = dict(nearest_original_bin_retained=sum(r['nearest_original_bin_retained'] for r in rows),
                effective_search_interval_covered=sum(r['effective_search_interval_covers_static_cfo'] for r in rows))
            groups.append(dict(geometry=geometry, policy=name, captures=len(rows), narrowed=len(narrow), fallback=len(rows)-len(narrow),
                reasons=dict(Counter(r['reason'] for r in rows)), counts=counts,
                count_intervals95={key: interval(value, len(rows)) for key, value in counts.items()},
                narrowed_nearest_bin_retained=sum(r['nearest_original_bin_retained'] for r in narrow),
                narrowed_continuous_interval_covered=sum(r['reported_expanded_interval_covers_static_cfo'] for r in narrow),
                selected_count_range=[min(r['selected_count'] for r in rows), max(r['selected_count'] for r in rows)],
                original_count=rows[0]['original_count'], estimator_seconds=sum(r['estimator_seconds'] for r in rows)))
    summary = dict(groups=groups, elapsed_seconds=time.perf_counter()-start,
        frozen_profiles=freeze, receiver_scoring_performed=False,
        limitations=scoring_protocol['limits'])
    write_json(args.output/'summary.json', summary)
    print(json.dumps(summary, allow_nan=False))


if __name__ == '__main__':
    run()
