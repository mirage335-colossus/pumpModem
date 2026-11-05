#!/usr/bin/env python3
"""Occasional native-cohort calibration; truth is confined to this evaluator."""
import argparse
import gzip
import hashlib
import json
import math
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
import numpy as np
from triad_dynamic_frequency import estimate_dynamic_frequency, frequency_policy

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--cohort', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    start = time.perf_counter()
    source = ROOT/'tools/triad_dynamic_frequency.py'
    sources = {str(source.relative_to(ROOT)): digest(source), 'calibrate.py': digest(__file__)}
    plan = json.loads((a.cohort/'protocol.json').read_text())
    trials = [row for row in plan['trial_schedule'] if row['phase'] == 'frequency-calibration']
    assert len(trials) == 99
    config = {'competitive_score_drop': 18., 'data_frequency_aperture_seconds': .25}
    (a.output/'protocol.json').write_text(json.dumps(dict(source_hashes=sources,
        input_protocol_sha256=digest(a.cohort/'protocol.json'), beta=.01,
        training_count=99, original_step_requested_hz=.5, initial_config=config,
        quantile='ceil((n+1)*(1-beta))-th ordered nonconformity; retain at least engineering cutoff18',
        scope='Marginal nearest-original-CFO retention under matching exchangeable seed distribution; not conditional-on-narrowing coverage or an instantaneous-frequency guarantee.'), indent=2)+'\n')
    receipts, scores = [], []
    with gzip.open(a.output/'profiles.jsonl.gz', 'wt') as profiles:
        for i, row in enumerate(trials):
            folder = a.cohort/row['id']
            m = json.loads((folder/'manifest.json').read_text())
            raw = np.memmap(folder/m['observation']['file'], dtype='<c16', mode='r')
            valid = np.memmap(folder/m['validity']['file'], dtype='u1', mode='r')
            bound = m['channel']['deterministic_cfo_bound_hz']*m['channel']['spread_margin']
            bank = np.linspace(-bound, bound, 2*math.ceil(bound/.5)+1)
            result = estimate_dynamic_frequency(raw, m, bank, valid=valid, config=config)
            # Estimator receives no truth. This training-only evaluator reads it
            # after that capture's complete fixed-policy profile was computed.
            truth_path = folder/m['evaluator_truth_file']
            truth = json.loads(truth_path.read_text())
            tx, rx, ch = truth['tx'], truth['rx'], m['channel']
            cfo = ((tx['rf_ppm']-rx['rf_ppm'])*ch['rf_hz']+
                (tx['audio_ppm']-rx['audio_ppm'])*ch['nominal_conversion_if_hz'])*1e-6/(1+rx['audio_ppm']*1e-6)
            nearest = int(np.argmin(abs(bank-cfo)))
            score = result['original_bank_nonconformity'][nearest]
            if score is None or not math.isfinite(score):
                raise ValueError('Nonfinite training score; cannot silently discard a capture')
            scores.append(score)
            receipt = dict(id=row['id'], manifest_sha256=digest(folder/'manifest.json'),
                observation_sha256=digest(folder/m['observation']['file']), truth_sha256=digest(truth_path),
                cfo_evaluator_only=cfo, nearest_original_frequency_hz=float(bank[nearest]),
                nonconformity=score, selected_count=result['selected_count'],
                status=result['status'], reason=result['reason'], estimator_seconds=result['wall_seconds'])
            receipts.append(receipt)
            profiles.write(json.dumps(dict(receipt=receipt, estimator=result), allow_nan=False)+'\n')
            if (i+1)%10 == 0:
                print(json.dumps(dict(completed=i+1, wall_seconds=time.perf_counter()-start)), flush=True)
    rank = math.ceil((len(scores)+1)*.99)
    quantile = sorted(scores)[rank-1]
    config['competitive_score_drop'] = max(18., quantile)
    if digest(source) != sources[str(source.relative_to(ROOT))]:
        raise RuntimeError('estimator source changed during calibration')
    (a.output/'frequency-config.json').write_text(json.dumps(config, indent=2)+'\n')
    report = dict(schema='triad-helper-frequency-region-calibration-v1', count=len(scores),
        rank=rank, beta=.01, empirical_quantile=quantile, config=config,
        policy=frequency_policy(config), sources=sources,
        calibration_protocol_sha256=digest(a.output/'protocol.json'),
        profiles_sha256=digest(a.output/'profiles.jsonl.gz'), receipts=receipts,
        wall_seconds=time.perf_counter()-start,
        scope='Frequency-coverage training only; separate subsequent admission calibration and held-out validation required. No reception results.')
    (a.output/'cutoff.json').write_text(json.dumps(report, indent=2, allow_nan=False)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('receipts','policy')}, indent=2))

if __name__ == '__main__':
    main()
