#!/usr/bin/env python3
"""Noiseless signal/projection diagnostics; never receiver inputs or PD bounds.

Reads sampled G=A^H A, m=A^H s and signal energy from completed block-stat
exports. Each candidate is held fixed across the observation, while its fitted
complex coefficients may change at every retained block. This measures a
subspace's retained signal energy, not an achievable likelihood or decoder.

Usage: retention.py WORKSPACE... --output REPORT.json [--pair INDEX INDEX ...]
Paired metadata checks cover source/physical roots and documented configuration;
they do not establish equality of unavailable pre-modulation samples or noise.
"""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path

import numpy as np


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_json(path):
    def invalid(value):
        raise ValueError(f"nonfinite JSON token {value}: {path}")
    return json.loads(Path(path).read_text(), parse_constant=invalid)


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def positive_integer(value, name):
    require(type(value) is int and value > 0, f"{name} must be a positive integer")
    return value


def ratio(numerator, denominator):
    return numerator / denominator if denominator > 0 else None


def projection(gram, mean, signal_energy, valid_rows, tolerance=1e-10):
    """FP64 PSD Gram pseudoinverse, with a declared eigenvalue cutoff."""
    G = np.asarray(gram, dtype=np.complex128)
    m = np.asarray(mean, dtype=np.complex128)
    require(G.ndim == 2 and G.shape[0] == G.shape[1] and m.shape == (len(G),),
            "inconsistent Gram/mean shape")
    require(np.isfinite(G).all() and np.isfinite(m).all(), "nonfinite Gram/mean")
    require(math.isfinite(signal_energy) and signal_energy >= 0, "invalid signal energy")
    require(type(valid_rows) is int and valid_rows >= 0, "invalid valid-row count")
    require(math.isfinite(tolerance) and 0 < tolerance < 1, "invalid rank tolerance")
    require(signal_energy > 0 or not np.any(m), "zero signal energy has nonzero mean")
    scale = max(float(np.max(np.abs(G), initial=0)), np.finfo(float).tiny)
    require(float(np.max(abs(G - G.conj().T), initial=0)) <= 1e-9 * scale,
            "Gram is not Hermitian")
    values, vectors = np.linalg.eigh((G + G.conj().T) * .5)
    scale = max(float(values[-1]), np.finfo(float).tiny)
    require(float(values[0]) >= -1e-9 * scale, "Gram is not positive semidefinite")
    require(np.isfinite(values).all() and np.isfinite(vectors).all(),
            "nonfinite Gram eigensystem")
    keep = values > tolerance * scale
    # Only true/numerically nonpositive modes are range checks. Small positive
    # modes deliberately discarded by the rank policy remain legitimate.
    null = values <= 0
    if np.any(null):
        null_mean = vectors[:, null].conj().T @ m
        allowance = math.sqrt(1e-9 * scale) * math.sqrt(signal_energy) * math.sqrt(len(G))
        allowance += 64 * np.finfo(float).eps * len(G) * float(np.max(abs(m), initial=0))
        require(np.isfinite(null_mean).all() and float(np.max(abs(null_mean), initial=0)) <= allowance,
                "mean is inconsistent with the Gram nullspace")
    rank = int(keep.sum())
    require(rank <= valid_rows, "Gram rank exceeds valid rows")
    if rank:
        with np.errstate(over='ignore', invalid='ignore', divide='ignore'):
            z = vectors[:, keep].conj().T @ m
            # Divide before squaring: a finite energy can otherwise overflow
            # in |m|^2 even when division by a large eigenvalue resolves it.
            scaled = z / np.sqrt(values[keep])
            explained = float(np.sum(abs(scaled)**2))
        require(np.isfinite(z).all() and np.isfinite(scaled).all() and math.isfinite(explained),
                "projection arithmetic exceeds finite range")
    else:
        explained = 0.
    energy_tolerance = 2e-9 * max(signal_energy, explained, 1.)
    require(explained <= signal_energy + energy_tolerance,
            "projected mean energy exceeds sampled signal energy")
    require(valid_rows or signal_energy == 0, "erased block has nonzero signal energy")
    if not valid_rows:
        require(not np.any(G) and not np.any(m), "erased block has nonzero statistics")
    return min(explained, signal_energy), rank


def _array(directory, manifest, name, shape, dtype):
    filename = manifest[name + '_file']
    require(isinstance(filename, str) and Path(filename).name == filename,
            f"nonlocal {name} filename")
    require(manifest[name + '_shape'] == list(shape), f"{name} shape mismatch")
    path = directory / filename
    size = math.prod(shape) * np.dtype(dtype).itemsize
    require(path.stat().st_size == size, f"{name} byte count mismatch")
    require(manifest['file_sizes'][filename] == size, f"{name} declared size mismatch")
    return np.memmap(path, mode='r', dtype=dtype, shape=shape), path


def analyze_workspace(directory, tolerance=1e-10):
    directory = Path(directory).resolve()
    m = read_json(directory / 'manifest.json')
    require(m.get('format') == 'triad-block-stats-v1' and m.get('byte_order') == 'little'
            and m.get('complex_type') == 'complex128' and m.get('real_type') == 'float64'
            and m.get('integer_type') == 'uint64' and m.get('layout') == 'block,row,column',
            "unsupported sampled-statistics representation")
    blocks = positive_integer(m['block_count'], 'block_count')
    dimension = positive_integer(m['dimension'], 'dimension')
    samples = positive_integer(m['sample_count'], 'sample_count')
    block_samples = positive_integer(m['block_samples'], 'block_samples')
    require(dimension <= 64 and blocks == (samples + block_samples - 1) // block_samples,
            "invalid exported dimensions/calendar")
    fs = m['sample_rate']
    require(type(fs) in (int, float) and math.isfinite(fs) and fs > 0, "invalid sample rate")
    mapping = m['column_mapping']; candidates = m['candidates']
    require(isinstance(mapping, list) and len(mapping) == len(candidates) == m['candidate_count']
            and len(mapping) > 0, "candidate mapping count mismatch")
    for cols, candidate in zip(mapping, candidates):
        require(isinstance(cols, list) and cols and len(cols) == len(set(cols))
                and all(type(x) is int and 0 <= x < dimension for x in cols),
                "invalid candidate columns")
        require(type(candidate.get('bit')) is int and candidate['bit'] in (0, 1), "invalid candidate bit")
    require({c['bit'] for c in candidates} == {0, 1}, "both candidate bits are required")
    truth = m.get('signal_bit')
    require(type(truth) is int and truth in (0, 1), "sampled truth label is missing")
    specs = {'gram': ((blocks, dimension, dimension), '<c16'),
             'mean': ((blocks, dimension), '<c16'),
             'signal_energy': ((blocks,), '<f8'),
             'valid_rows': ((blocks,), '<u8'), 'intervals': ((blocks, 2), '<u8')}
    arrays = {}; paths = [directory / 'manifest.json']
    signatures = {str(paths[0]): (paths[0].stat().st_size, paths[0].stat().st_mtime_ns)}
    for name, (shape, dtype) in specs.items():
        arrays[name], path = _array(directory, m, name, shape, dtype)
        paths.append(path); signatures[str(path)] = (path.stat().st_size, path.stat().st_mtime_ns)
    totals = np.zeros(len(mapping), np.float64); candidate_ranks = [dict() for _ in mapping]
    full_energy = 0.; signal_energy = 0.; valid_total = 0; full_ranks = {}; zero_signal_blocks = 0
    for block in range(blocks):
        first, stop = map(int, arrays['intervals'][block])
        require((first, stop) == (block * block_samples, min((block + 1) * block_samples, samples)),
                "block intervals differ from exact calendar")
        valid = int(arrays['valid_rows'][block]); require(valid <= stop - first, "valid rows exceed interval")
        energy = float(arrays['signal_energy'][block])
        G = arrays['gram'][block]; mean = arrays['mean'][block]
        explained, rank = projection(G, mean, energy, valid, tolerance)
        full_energy += explained; signal_energy += energy; valid_total += valid
        zero_signal_blocks += energy == 0; full_ranks[rank] = full_ranks.get(rank, 0) + 1
        for i, cols in enumerate(mapping):
            explained, rank = projection(G[np.ix_(cols, cols)], mean[cols], energy, valid, tolerance)
            totals[i] += explained; candidate_ranks[i][rank] = candidate_ranks[i].get(rank, 0) + 1
    require(math.isfinite(signal_energy) and math.isfinite(full_energy) and np.isfinite(totals).all(),
            "accumulated signal energy is nonfinite")
    by_candidate = [{'index': i, 'bit': c['bit'], 'columns': mapping[i],
                     'projection_energy_samples': float(totals[i]),
                     'projection_energy_seconds': float(totals[i]) / fs,
                     'fraction_of_sampled_signal_energy': ratio(float(totals[i]), signal_energy),
                     'rank_histogram': candidate_ranks[i], 'public_candidate': c}
                    for i, c in enumerate(candidates)]
    best = {bit: max((i for i, c in enumerate(candidates) if c['bit'] == bit), key=lambda i: totals[i])
            for bit in (0, 1)}
    hashes = {}
    for path in paths:
        hashes[path.name] = sha256(path)
        require(signatures[str(path)] == (path.stat().st_size, path.stat().st_mtime_ns),
                "sampled evidence changed during analysis")
    result = {'status': 'completed', 'input': str(directory), 'input_sha256': hashes,
              'sample_rate_hz': fs, 'capture_seconds': samples / fs, 'sample_count': samples,
              'valid_rows': valid_total, 'blocks': blocks, 'zero_signal_blocks': zero_signal_blocks,
              'signal_energy_samples': signal_energy, 'signal_energy_seconds': signal_energy / fs,
              'full_bank_projection_energy_samples': full_energy,
              'full_bank_retained_fraction': ratio(full_energy, signal_energy),
              'full_bank_rank_histogram': full_ranks,
              'full_bank_noise_residual_complex_dof': valid_total - sum(int(k) * v for k, v in full_ranks.items()),
              'candidate_projections': by_candidate, 'truth_bit_simulation_only': truth,
              'best_fixed_correct_label_candidate': best[truth], 'best_fixed_wrong_label_candidate': best[1-truth],
              'correct_label_retained_fraction': ratio(float(totals[best[truth]]), signal_energy),
              'wrong_label_retained_fraction': ratio(float(totals[best[1-truth]]), signal_energy),
              'correct_minus_wrong_projection_fraction': ratio(float(totals[best[truth]] - totals[best[1-truth]]), signal_energy),
              'cn0_db_hz_simulation_only': m.get('cn0_db_hz'),
              'channel_model': m.get('channel_model'), 'source_format': m.get('source_format'),
              'requested_active_seconds': m.get('requested_active_seconds'),
              'actual_active_seconds': m.get('actual_elapsed_seconds'),
              'selective_stage_energy_simulation_only': selective_energy_telemetry(m)}
    return result, m



def selective_energy_telemetry(manifest):
    """Optional independently instrumented signal energy around the AM stage."""
    item = manifest.get('path_truth_simulation_only', {}).get('selective_perturbation')
    if item is None:
        return {'available': False, 'reason': 'older export has no pre/post-selective telemetry'}
    require(isinstance(item, dict) and type(item.get('enabled')) is bool,
            'invalid selective energy telemetry')
    if not item['enabled']:
        return {'available': False, 'enabled': False, 'reason': 'selective stage is an exact bypass'}
    fs = item.get('sample_rate_hz')
    pre = item.get('pre_selective_energy_samples'); post = item.get('post_selective_energy_samples')
    require(type(fs) in (int, float) and math.isfinite(fs) and fs > 0,
            'invalid physical selective-stage sample rate')
    require(all(type(v) in (int, float) and math.isfinite(v) and v >= 0 for v in (pre, post)),
            'invalid pre/post-selective signal energy')
    return {'available': True, 'enabled': True, 'physical_sample_rate_hz': fs,
            'pre_selective_energy_samples': pre, 'post_selective_energy_samples': post,
            'pre_selective_energy_seconds': pre / fs, 'post_selective_energy_seconds': post / fs,
            'post_over_pre_signal_energy_ratio': ratio(post, pre),
            'processed_rows': item.get('processed_rows'),
            'component_depths': item.get('component_depths'), 'component_rates_hz': item.get('component_rates_hz'),
            'stage': 'Actual signal immediately before/after selective filtering; before RX oscillator, thermal noise, sampled frontend and erasures; processed capture tails included.',
            'scope': 'Simulation-only stage instrumentation. This does not measure discarded analog/capture-band energy or equal-energy receiver sensitivity.'}


# Tail lengths/shapes and measured output counters can change when the AM
# stage is enabled. All other manifest fields are compared by default, so an
# unrecognized future scientific field is not silently omitted from pairing.
_PAIR_OUTPUT_FIELDS = {
    'block_count', 'sample_count', 'channel_samples', 'channel_tail_samples',
    'file_sizes', 'channel_signal_energy_samples', 'channel_observed_energy_samples',
    'frontend_report',
}
_AM_FIELDS = {'selective_perturbation', 'perturbation_slow_depth', 'perturbation_fast_depth'}
_AM_OBSERVED_FIELDS = {'component_depths', 'processed_rows',
                       'pre_selective_energy_samples', 'post_selective_energy_samples'}


def _normalized_command(argv):
    require(isinstance(argv, list) and all(isinstance(x, str) for x in argv),
            'invalid paired command argument array')
    result = []; index = 0
    while index < len(argv):
        token = argv[index]
        name, equal, value = token.partition('=')
        if name in ('--output', '--selective-perturbation'):
            if not equal:
                require(index + 1 < len(argv), 'paired command option has no value')
                index += 1
        else:
            result.append(token)
        index += 1
    return result


def _normalized_manifest(manifest, compare_active_am_metadata):
    result = copy.deepcopy(manifest)
    for key in _PAIR_OUTPUT_FIELDS:
        result.pop(key, None)
    for key in list(result):
        if key.endswith('_shape'):
            # Only the first (block-count) coordinate can depend on tails.
            shape = result[key]
            require(isinstance(shape, list) and shape, 'invalid paired array shape')
            result[key] = ['tail-dependent-block-count', *shape[1:]]
    if 'command_argv' in result:
        result['command_argv'] = _normalized_command(result['command_argv'])
    for container in (result.get('channel_model', {}), result.get('resolved_configuration', {})):
        for key in _AM_FIELDS:
            container.pop(key, None)
    capture = result.get('resolved_configuration', {}).get('capture', {})
    # These are explicit derived checks: enabled modulation adds its highest
    # sideband rate to capture preflight. All capture input settings stay bound.
    for key in ('deterministic_nyquist_margin_hz', 'feasible_for_deterministic_prior'):
        capture.pop(key, None)
    path = result.get('path_truth_simulation_only', {})
    if 'selective_perturbation' in path:
        if compare_active_am_metadata:
            for key in _AM_OBSERVED_FIELDS:
                path['selective_perturbation'].pop(key, None)
        else:
            # There is no filter state, phase or rate report on the exact Off
            # bypass. Enabling that stage necessarily introduces this metadata.
            path.pop('selective_perturbation')
    return result


def _differences(left, right, prefix=''):
    if isinstance(left, dict) and isinstance(right, dict):
        found = []
        for key in sorted(set(left) | set(right)):
            label = prefix + '.' + key if prefix else key
            if key not in left or key not in right:
                found.append(label)
            else:
                found.extend(_differences(left[key], right[key], label))
        return found
    return [] if left == right else [prefix]


def paired_metadata(left, right):
    """Compare all recorded science except explicit AM activation/depth/tails."""
    required = ('key_seed', 'epoch', 'signal_bit', 'sample_rate', 'noise_variance',
                'source_samples', 'source_format', 'random_seed_roots', 'endpoint_truth',
                'source_hash_binding', 'candidates', 'column_mapping', 'oscillator_model',
                'sampled_frontend', 'capture_lead_seconds', 'requested_active_seconds',
                'actual_elapsed_seconds', 'selected_truth_arrival_seconds_simulation_only',
                'selected_truth_frequency_hz_simulation_only', 'selected_truth_clock_ppm_simulation_only',
                'selected_truth_frequency_drift_hz_per_second_simulation_only',
                'resolved_configuration', 'channel_model')
    missing = [name for name in required if name not in left or name not in right]
    checks = {'required_scientific_fields_present': not missing}
    for label in ('channel', 'oscillator', 'noise', 'frontend', 'path'):
        l = left.get('random_seed_roots', {}).get(label); r = right.get('random_seed_roots', {}).get(label)
        checks['explicit_' + label + '_root'] = type(l) is int and type(r) is int and l == r
    active = [m.get('path_truth_simulation_only', {}).get('selective_perturbation', {}).get('enabled') is True
              for m in (left, right)]
    differences = _differences(_normalized_manifest(left, all(active)), _normalized_manifest(right, all(active)))
    checks['all_scientific_fields_except_documented_am_and_tail_outputs'] = not differences
    return {'recorded_pair_configuration_matches': all(checks.values()),
            'checks': checks, 'missing_fields': missing, 'unexpected_scientific_differences': differences,
            'same_sample_support': left.get('sample_count') == right.get('sample_count')
                and left.get('block_samples') == right.get('block_samples')
                and left.get('capture_lead_seconds') == right.get('capture_lead_seconds'),
            'allowed_differences': {'measured_tail_outputs': sorted(_PAIR_OUTPUT_FIELDS),
                'selective_depth_descriptors': sorted(_AM_FIELDS),
                'array_shapes': 'first block-count dimension only; remaining shape dimensions must match',
                'selective_instrumentation': 'depths and measured energy/row counters when both active; whole added-stage report only across exact Off/On bypass',
                'derived_capture_checks': ['deterministic_nyquist_margin_hz', 'feasible_for_deterministic_prior'],
                'command_options': ['--output', '--selective-perturbation']},
            'scope': 'Recorded source/physical metadata equality only, not a source-to-binary proof. source_hash_binding may itself be a placeholder; external hashes must bind exact source and binary. Modulation changes signal samples; equality of unavailable pre-selective waveform or continuous paths is not independently proved. Capture tails can differ.'}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs='+', type=Path); parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--pair', nargs=2, type=int, action='append', default=[])
    parser.add_argument('--rank-tolerance', type=float, default=1e-10)
    args = parser.parse_args(argv)
    results = []; manifests = []
    for path in args.inputs:
        try:
            result, manifest = analyze_workspace(path, args.rank_tolerance)
        except Exception as error:
            result = {'status': 'error', 'input': str(path), 'error_type': type(error).__name__, 'error': str(error)}
            manifest = None
        results.append(result); manifests.append(manifest)
    pairs = []
    for a, b in args.pair:
        require(0 <= a < len(results) and 0 <= b < len(results) and a != b, 'invalid paired indices')
        if manifests[a] is None or manifests[b] is None:
            pairs.append({'left': a, 'right': b, 'status': 'error', 'error': 'paired workspace analysis failed'})
        else:
            pairs.append({'left': a, 'right': b, 'status': 'completed', **paired_metadata(manifests[a], manifests[b]),
                          'post_channel_signal_energy_ratio': ratio(results[b]['signal_energy_samples'], results[a]['signal_energy_samples']),
                          'scope_energy_ratio': 'Full post-channel sampled energies; unequal tails and physical amplitude changes are retained. No pre-modulation, capture-band or equal-energy normalization claim.'})
    passed = all(r['status'] == 'completed' for r in results) and all(p.get('recorded_pair_configuration_matches', False) for p in pairs)
    output = {'schema': 'triad-selective-retention-v1', 'source_sha256': sha256(Path(__file__)),
              'passed': passed, 'scientific_qualification': False, 'rank_relative_gram_eigenvalue_cutoff': args.rank_tolerance,
              'workspaces': results, 'paired_comparisons': pairs,
              'limitations': ['Simulator-only noiseless means and signal energy are diagnostic inputs and must not feed a receiver.',
                  'Per-block unconstrained complex coefficients give projection-span retention, not an achievable detector, channel likelihood or reception probability.',
                  'Each alignment/model candidate stays fixed over the observation; label maxima are taken only after summing its blocks.',
                  'Older exports cannot identify pre-selective energy; optional new pre/post-selective telemetry is reported at its separately named stage. Analog capture-band or RF-filter loss remains unmeasured.',
                  'A small wrong-label projection is useful separation evidence but is not a false-admission probability.',
                  'Paired roots at equal sample rate support a declared discrete-model design; they do not establish sample-rate refinement or identical raw I/Q.']}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x') as stream:
        json.dump(output, stream, allow_nan=False, sort_keys=True, indent=2); stream.write('\n')
    print(json.dumps({'passed': passed, 'workspaces': len(results), 'pairs': len(pairs)}))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
