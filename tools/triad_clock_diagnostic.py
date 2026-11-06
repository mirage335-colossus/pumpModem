#!/usr/bin/env python3
"""Cheap clock-warp discrimination from paired native source windows.

The optional AWGN experiment preserves the exact sampled template Gram matrix.
It tests a flat, constant-unknown-complex-gain two-codeword detector, not the
maintained Gaussian-state receiver, propagation, acquisition or a live null.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re

import numpy as np
from scipy.stats import beta

MAX_ROWS = 500_000
MAX_TRIALS = 1_000_000
MAX_ENERGY_CASES = 32
_NAME = re.compile(r'^[A-Za-z0-9_.-]+$')
IDENTITY_FIELDS = ('seed', 'epoch', 'symbol_ordinal', 'encrypted', 'key_kind',
                   'sample_rate_hz', 'first_sample', 'rows', 'fine_rate_hz',
                   'carrier_spacing_hz', 'source_samples', 'calendar_origin_sample',
                   'first_calendar_seconds', 'window_seconds', 'format',
                   'geometry', 'shaping_policy', 'shaping_fallback')


def _positive(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
        raise ValueError(f'{name} must be finite and positive')
    return float(value)


def _identity(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        while chunk := stream.read(1 << 20):
            digest.update(chunk)
    return dict(path=str(Path(path).resolve()), sha256=digest.hexdigest())


def load_probe(directory):
    directory = Path(directory)
    m = json.loads((directory/'manifest.json').read_text())
    if not isinstance(m, dict) or m.get('schema') != 'triad-pattern-probe-v1':
        raise ValueError('native triad-pattern-probe-v1 manifest required')
    if m.get('source_only') is not True or m.get('channel_applied') is not False:
        raise ValueError('clock source diagnostic requires unpropagated source-only samples')
    n = m.get('rows')
    if isinstance(n, bool) or not isinstance(n, int) or not 1 <= n <= MAX_ROWS:
        raise ValueError('probe row count outside bounded diagnostic extent')
    if m.get('dtype') != '<c16' or m.get('composite_shape') != [2, n]:
        raise ValueError('probe must contain two complex128 codeword rows')
    _positive(m.get('sample_rate_hz'), 'sample rate')
    _positive(m.get('fine_rate_hz'), 'fine rate')
    for key in IDENTITY_FIELDS:
        if key not in m:
            raise ValueError(f'probe is missing identity field {key}')
    for key in ('first_sample', 'source_samples', 'calendar_origin_sample', 'epoch', 'symbol_ordinal'):
        value = m[key]
        if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value < 2**64:
            raise ValueError(f'invalid integer coordinate {key}')
    if m['first_sample'] + n > m['source_samples']:
        raise ValueError('source window exceeds declared source extent')
    if not isinstance(m['format'], dict) or not isinstance(m['geometry'], dict):
        raise ValueError('waveform format and geometry must be objects')
    trace = m.get('clock_follow')
    if not isinstance(trace, dict) or not isinstance(trace.get('enabled'), bool) or trace.get('law') != 'bounded-source-quintic-v1':
        raise ValueError('explicit supported clock-follow trace required')
    for key in ('target_offset_seconds', 'correction_at_calendar_seconds', 'transition_duration_seconds'):
        value = trace.get(key)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
            raise ValueError(f'invalid clock trace field {key}')
    if abs(trace['target_offset_seconds']) > 2 or trace['transition_duration_seconds'] < 0:
        raise ValueError('clock trace exceeds declared bounded source law')
    if not trace['enabled'] and trace['target_offset_seconds'] != 0:
        raise ValueError('disabled clock trace cannot have a nonzero target')
    filename = m.get('composite_file')
    if not isinstance(filename, str) or not _NAME.fullmatch(filename) or Path(filename).name != filename:
        raise ValueError('unsafe probe array filename')
    path = directory/filename
    if path.stat().st_size != 2*n*16:
        raise ValueError('probe array size disagrees with manifest')
    x = np.memmap(path, mode='r', dtype='<c16', shape=(2, n))
    if not np.isfinite(x).all():
        raise ValueError('nonfinite probe samples')
    return m, x, [_identity(directory/'manifest.json'), _identity(path)]


def paired_gram(nominal, warped):
    """Unit-template Gram plus raw energies; no signal-energy equalization."""
    nominal, warped = np.asarray(nominal), np.asarray(warped)
    if nominal.ndim != 2 or nominal.shape[0] != 2 or nominal.shape != warped.shape or not nominal.shape[1]:
        raise ValueError('paired arrays must have the same nonempty (2, samples) shape')
    if not np.isfinite(nominal).all() or not np.isfinite(warped).all():
        raise ValueError('finite sampled templates required')
    vectors = np.concatenate((nominal, warped)).astype(np.complex128, copy=False)
    energies = np.einsum('ij,ij->i', vectors.conj(), vectors).real
    if np.any(energies <= 0) or not np.isfinite(energies).all():
        raise ValueError('each codeword window must have nonzero finite energy')
    vectors = vectors/np.sqrt(energies)[:, None]
    gram = vectors.conj() @ vectors.T
    gram = (gram+gram.conj().T)/2
    return gram, energies


def gaussian_factor(gram):
    """Stable square root retaining dependence, including duplicate templates."""
    gram = np.asarray(gram, dtype=np.complex128)
    if gram.shape != (4, 4) or not np.isfinite(gram).all() or not np.allclose(gram, gram.conj().T, atol=1e-12, rtol=0):
        raise ValueError('finite Hermitian four-template Gram required')
    values, vectors = np.linalg.eigh(gram)
    tolerance = 64*np.finfo(float).eps*max(1., float(np.max(values)))
    if float(np.min(values)) < -tolerance:
        raise ValueError('template Gram is not positive semidefinite')
    kept = values > tolerance
    factor = vectors[:, kept]*np.sqrt(values[kept])[None, :]
    return factor, int(np.count_nonzero(kept))


def binomial_result(events, trials, confidence=.95):
    """Clopper-Pearson marginal interval; not a simultaneous coverage claim."""
    if not 0 <= events <= trials or trials < 1:
        raise ValueError('invalid binomial counts')
    tail = (1-confidence)/2
    low = 0. if events == 0 else float(beta.ppf(tail, events, trials-events+1))
    high = 1. if events == trials else float(beta.ppf(1-tail, events+1, trials-events))
    return dict(count=int(events), trials=int(trials), fraction=events/trials,
                confidence=confidence, interval=[low, high], interval_scope='marginal exact binomial, two-sided')


def _outcomes(scores, bit, threshold):
    admitted = np.max(scores, axis=1) >= threshold
    correct = np.argmax(scores, axis=1) == bit
    return np.array([np.count_nonzero(admitted & correct),
                     np.count_nonzero(admitted & ~correct),
                     np.count_nonzero(~admitted)], dtype=np.int64)


def awgn_experiment(gram, energies, *, ebn0_db, trials=16384, alpha=1e-4, seed=83101):
    """Paired Gaussian sufficient statistics, exactly induced by these windows.

    Nominal E/N0 is fixed per source bit before the warp. The warped source
    keeps its actual energy difference. Both receivers see the identical noise.
    """
    if isinstance(trials, bool) or not isinstance(trials, int) or not 1 <= trials <= MAX_TRIALS:
        raise ValueError('trials must be in [1,1000000]')
    if isinstance(seed, bool) or not isinstance(seed, int) or seed < 0:
        raise ValueError('nonnegative integer seed required')
    if not math.isfinite(alpha) or not 0 < alpha < 1:
        raise ValueError('alpha must be strictly between zero and one')
    levels = list(ebn0_db)
    if not 1 <= len(levels) <= MAX_ENERGY_CASES or any(not math.isfinite(x) or abs(x) > 100 for x in levels):
        raise ValueError('one to32 finite E/N0 values in[-100,100] dB required')
    factor, rank = gaussian_factor(gram)
    energies = np.asarray(energies, dtype=float)
    if energies.shape != (4,) or not np.isfinite(energies).all() or np.any(energies <= 0):
        raise ValueError('four positive template energies required')
    threshold = math.log(2/alpha)
    # One set of correlated noise projections serves every comparison and E/N0.
    rng = np.random.default_rng(seed)
    null = np.zeros(2, dtype=np.int64)
    counts = np.zeros((len(levels), 2, 3, 3), dtype=np.int64)
    means = np.empty((len(levels), 2, 2, 4), dtype=np.complex128)
    for j, db in enumerate(levels):
        for bit in (0, 1):
            for warped in (0, 1):
                index = 2*warped+bit
                means[j, bit, warped] = gram[:, index]*math.sqrt(10**(db/10)*energies[index]/energies[bit])
    for start in range(0, trials, 8192):
        n = min(8192, trials-start)
        raw = (rng.normal(size=(n, rank))+1j*rng.normal(size=(n, rank)))/math.sqrt(2)
        noise = raw @ factor.T
        for receiver in (0, 1):
            scores = np.abs(noise[:, 2*receiver:2*receiver+2])**2
            null[receiver] += np.count_nonzero(np.max(scores, axis=1) >= threshold)
        for j in range(len(levels)):
            for bit in (0, 1):
                original = noise+means[j, bit, 0]
                changed = noise+means[j, bit, 1]
                for case, projection in enumerate((original[:, :2], changed[:, :2], changed[:, 2:])):
                    counts[j, bit, case] += _outcomes(np.abs(projection)**2, bit, threshold)
    names = ('nominal_source_nominal_receiver', 'warped_source_nominal_receiver', 'warped_source_oracle_receiver')
    cases = []
    for j, db in enumerate(levels):
        bits = []
        for bit in (0, 1):
            row = dict(bit=bit, actual_warped_en0_db=db+10*math.log10(energies[bit+2]/energies[bit]))
            for case, name in enumerate(names):
                row[name] = {outcome: binomial_result(int(counts[j, bit, case, i]), trials)
                             for i, outcome in enumerate(('correct_admissions', 'wrong_admissions', 'misses'))}
            bits.append(row)
        cases.append(dict(nominal_en0_db=float(db), bits=bits))
    return dict(schema='triad-clock-window-awgn-v1', seed=seed, trials_per_bit=trials,
                nominal_noise_variance=1., threshold=threshold, alpha_per_two_codeword_bank=alpha,
                null_bound='P(max of two unit-template noise powers >= log(2/alpha)) <= alpha by union bound; no independence assumed',
                noise_only={name: binomial_result(int(null[i]), trials) for i, name in enumerate(('nominal_receiver', 'oracle_receiver'))},
                numerical_gram_rank=rank, cases=cases,
                scope='Flat AWGN, known noise variance, fixed alignment, constant unknown complex gain; source-window diagnostic only. Oracle receives the private warp. No channel, search, operational continuous null or maintained-receiver qualification.',
                energy_policy='Fixed amplitude chosen from each nominal source window, not the complete message bit; warped energy is not renormalized.',
                dependence='Common correlated noise used for all cases, levels and both bits; confidence intervals are marginal, not independent or simultaneous.')


def analyze(nominal_dir, warped_dir, *, ebn0_db=(), trials=16384, alpha=1e-4, seed=83101):
    a, original, a_id = load_probe(nominal_dir)
    b, changed, b_id = load_probe(warped_dir)
    for key in IDENTITY_FIELDS:
        if a[key] != b[key]:
            raise ValueError(f'paired probe identity mismatch: {key}')
    if a['clock_follow']['enabled'] or not b['clock_follow']['enabled']:
        raise ValueError('nominal trace must be disabled and warped trace enabled')
    gram, energies = paired_gram(original, changed)
    bits = []
    for bit in (0, 1):
        squared = min(1., max(0., float(abs(gram[bit, bit+2])**2)))
        wrong = min(1., max(0., float(abs(gram[1-bit, bit+2])**2)))
        bits.append(dict(bit=bit, nominal_sample_energy=float(energies[bit]),
            warped_sample_energy=float(energies[bit+2]),
            actual_energy_change_db=10*math.log10(energies[bit+2]/energies[bit]),
            squared_overlap_after_common_complex_gain=squared,
            shape_mismatch_loss_db=-10*math.log10(squared) if squared else None,
            loss_is_infinite=squared == 0,
            wrong_nominal_codeword_squared_overlap=wrong,
            noiseless_correct_minus_wrong_overlap=squared-wrong,
            nominal_codeword_squared_overlap=min(1., float(abs(gram[0, 1])**2)),
            oracle_codeword_squared_overlap=min(1., float(abs(gram[2, 3])**2))))
    result = dict(schema='triad-clock-diagnostic-v1', rows=a['rows'], sample_rate_hz=a['sample_rate_hz'],
        window_seconds=a['window_seconds'], first_calendar_seconds=a['first_calendar_seconds'],
        fine_rate_hz=a['fine_rate_hz'], carrier_spacing_hz=a['carrier_spacing_hz'],
        nominal_trace=a['clock_follow'], warped_trace=b['clock_follow'], bits=bits,
        input_identities=a_id+b_id, tool_identity=_identity(__file__),
        channel=dict(spread='none', additional_selectivity='off', oscillator_effects='off'),
        scope='Actual sampled source windows. Unknown common complex gain is fitted; timing/clock alternatives and physical channel are not. Window overlap is not a full-message or long-term synchronization guarantee.')
    levels = list(ebn0_db)
    if levels:
        result['awgn'] = awgn_experiment(gram, energies, ebn0_db=levels, trials=trials, alpha=alpha, seed=seed)
    return result


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--nominal', type=Path, required=True)
    p.add_argument('--warped', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--window-en0-db', '--ebn0-db', dest='ebn0_db', type=float, nargs='*', default=[],
        help='nominal sampled-window energy/noise-density in dB; not whole-message Eb/N0 for partial windows')
    p.add_argument('--trials', type=int, default=16384)
    p.add_argument('--alpha', type=float, default=1e-4)
    p.add_argument('--seed', type=int, default=83101)
    a = p.parse_args(argv)
    if a.output.exists():
        p.error('output already exists; choose a new report path')
    try:
        result = analyze(a.nominal, a.warped, ebn0_db=a.ebn0_db, trials=a.trials, alpha=a.alpha, seed=a.seed)
        a.output.parent.mkdir(parents=True, exist_ok=True)
        with a.output.open('x') as stream:
            json.dump(result, stream, indent=2, allow_nan=False)
            stream.write('\n')
    except (ValueError, OSError) as error:
        p.error(str(error))
    print(json.dumps(dict(output=str(a.output), bits=result['bits'])))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
