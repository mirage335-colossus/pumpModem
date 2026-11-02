#!/usr/bin/env python3
"""Bounded FP64 batched equivalent of the accepted scalar retention diagnostic.

Every exported block is checked and projected. Candidates stay fixed across the
full observation. No sampling, receiver likelihood or reception qualification is
introduced. Batched eigh and einsum may alter final rounding; rank/validation
policies and source schema remain those of the bound scalar reference.
"""
from __future__ import annotations
import argparse
import importlib.util
import json
import math
import os
from pathlib import Path
import time

import numpy as np

HERE = Path(__file__).resolve().parent
REFERENCE_PATH = HERE.parents[1] / '2026-11-02-selective' / 'reproduction' / 'retention.py'
spec = importlib.util.spec_from_file_location('retention_batch_scalar_reference', REFERENCE_PATH)
REFERENCE = importlib.util.module_from_spec(spec)
spec.loader.exec_module(REFERENCE)
# Pure helper aliases; the reference module and its globals are never mutated.
require = REFERENCE.require
read_json = REFERENCE.read_json
sha256 = REFERENCE.sha256
positive_integer = REFERENCE.positive_integer
ratio = REFERENCE.ratio
_array = REFERENCE._array
selective_energy_telemetry = REFERENCE.selective_energy_telemetry
paired_metadata = REFERENCE.paired_metadata
DEFAULT_BATCH_BLOCKS = 256
DEFAULT_MEMORY_BYTES = 128 * 1024 * 1024


def source_bindings():
    return {'batch_module_sha256': sha256(Path(__file__)),
            'scalar_reference_sha256': sha256(REFERENCE_PATH),
            'scalar_reference_path': str(REFERENCE_PATH),
            'arithmetic': 'NumPy complex128/float64 batched eigh and einsum; left-to-right FP64 block-energy accumulation',
            'scope': 'All blocks, fixed candidate over full observation; identical nominal Gram eigenvalue, nullspace, energy and rank policies; floating-point operation ordering can differ.'}


def project_batch(grams, means, signal_energy, valid_rows, tolerance=1e-10):
    """Same per-block projection policy as REFERENCE.projection, batched."""
    G = np.asarray(grams, dtype=np.complex128)
    m = np.asarray(means, dtype=np.complex128)
    E = np.asarray(signal_energy, dtype=np.float64)
    valid = np.asarray(valid_rows)
    require(G.ndim == 3 and G.shape[0] > 0 and G.shape[1] == G.shape[2] and G.shape[1] > 0,
            'inconsistent batched Gram shape')
    blocks, columns = G.shape[:2]
    require(m.shape == (blocks, columns) and E.shape == valid.shape == (blocks,),
            'inconsistent batched mean/energy/row shape')
    require(np.issubdtype(valid.dtype, np.integer) and np.all(valid >= 0), 'invalid valid-row count')
    require(np.isfinite(G).all() and np.isfinite(m).all(), 'nonfinite Gram/mean')
    require(np.isfinite(E).all() and np.all(E >= 0), 'invalid signal energy')
    require(math.isfinite(tolerance) and 0 < tolerance < 1, 'invalid rank tolerance')
    require(np.all((E > 0) | ~np.any(m != 0, axis=1)), 'zero signal energy has nonzero mean')
    scale = np.maximum(np.max(abs(G), axis=(1, 2)), np.finfo(float).tiny)
    adjoint = G.conj().swapaxes(1, 2)
    require(np.all(np.max(abs(G - adjoint), axis=(1, 2)) <= 1e-9 * scale), 'Gram is not Hermitian')
    values, vectors = np.linalg.eigh((G + adjoint) * .5)
    scale = np.maximum(values[:, -1], np.finfo(float).tiny)
    require(np.all(values[:, 0] >= -1e-9 * scale), 'Gram is not positive semidefinite')
    require(np.isfinite(values).all() and np.isfinite(vectors).all(), 'nonfinite Gram eigensystem')
    keep = values > tolerance * scale[:, None]
    null = values <= 0
    with np.errstate(over='ignore', invalid='ignore', divide='ignore'):
        coordinates = np.einsum('bim,bi->bm', vectors.conj(), m, optimize=False)
        if np.any(null):
            allowance = np.sqrt(1e-9 * scale) * np.sqrt(E) * math.sqrt(columns)
            allowance += 64 * np.finfo(float).eps * columns * np.max(abs(m), axis=1)
            require(np.all(np.isfinite(coordinates) | ~null)
                    and np.all(np.max(np.where(null, abs(coordinates), 0.), axis=1) <= allowance),
                    'mean is inconsistent with the Gram nullspace')
        rank = np.sum(keep, axis=1, dtype=np.int64)
        require(np.all(rank <= valid), 'Gram rank exceeds valid rows')
        scaled = np.where(keep, coordinates / np.sqrt(np.where(keep, values, 1.)), 0.)
        explained = np.sum(abs(scaled)**2, axis=1)
        require(np.all(np.isfinite(coordinates) | ~keep) and np.isfinite(scaled).all()
                and np.isfinite(explained).all(), 'projection arithmetic exceeds finite range')
    allowance = 2e-9 * np.maximum(np.maximum(E, explained), 1.)
    require(np.all(explained <= E + allowance), 'projected mean energy exceeds sampled signal energy')
    erased = valid == 0
    require(np.all(~erased | (E == 0)), 'erased block has nonzero signal energy')
    require(not np.any(G[erased] != 0) and not np.any(m[erased] != 0), 'erased block has nonzero statistics')
    return np.minimum(explained, E), rank


def _append_total(previous, values):
    # Prepending the preceding total preserves the scalar loop's left-to-right
    # additions across chunk boundaries, rather than summing chunk subtotals.
    return float(np.add.accumulate(np.r_[previous, values], dtype=np.float64)[-1])


def _histogram_add(histogram, ranks):
    values, counts = np.unique(ranks, return_counts=True)
    for value, count in zip(values, counts):
        key = int(value); histogram[key] = histogram.get(key, 0) + int(count)


def analyze_workspace(directory, tolerance=1e-10, *, batch_blocks=DEFAULT_BATCH_BLOCKS,
                      memory_limit_bytes=DEFAULT_MEMORY_BYTES):
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
    require(type(batch_blocks) is int and batch_blocks > 0, 'invalid batch block count')
    require(type(memory_limit_bytes) is int and memory_limit_bytes >= 4096, 'invalid batch memory bound')
    # Conservative owned-array allowance; excludes implementation-dependent
    # LAPACK/allocator caches and file-backed pages. Candidates run sequentially.
    bytes_per_block = 12 * dimension * dimension * 16 + 24 * dimension * 16 + 512
    require(memory_limit_bytes >= bytes_per_block, 'batch memory bound cannot hold one projection block')
    tile = min(batch_blocks, max(1, memory_limit_bytes // bytes_per_block))
    for first_block in range(0, blocks, tile):
        stop_block = min(blocks, first_block + tile)
        intervals = arrays['intervals'][first_block:stop_block]
        starts = np.asarray([b * block_samples for b in range(first_block, stop_block)], dtype=np.uint64)
        stops = np.asarray([min((b + 1) * block_samples, samples) for b in range(first_block, stop_block)], dtype=np.uint64)
        require(np.array_equal(intervals[:, 0], starts) and np.array_equal(intervals[:, 1], stops),
                'block intervals differ from exact calendar')
        valid = arrays['valid_rows'][first_block:stop_block]
        require(np.all(valid <= stops - starts), 'valid rows exceed interval')
        energy = arrays['signal_energy'][first_block:stop_block]
        G = arrays['gram'][first_block:stop_block]; mean = arrays['mean'][first_block:stop_block]
        explained, rank = project_batch(G, mean, energy, valid, tolerance)
        full_energy = _append_total(full_energy, explained)
        signal_energy = _append_total(signal_energy, energy)
        valid_total += int(np.sum(valid, dtype=np.uint64))
        zero_signal_blocks += int(np.sum(energy == 0))
        _histogram_add(full_ranks, rank)
        for i, cols in enumerate(mapping):
            selected_G = G[:, cols, :][:, :, cols]
            explained, rank = project_batch(selected_G, mean[:, cols], energy, valid, tolerance)
            totals[i] = _append_total(totals[i], explained)
            _histogram_add(candidate_ranks[i], rank)
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


def compare_reports(scalar, batched, *, relative_tolerance=5e-12, absolute_tolerance=5e-12):
    """Strict metadata/rank/hash equality; scale-aware tolerance for FP64 sums."""
    differences = []; maximum_absolute = 0.; maximum_scaled = 0.
    def visit(left, right, path):
        nonlocal maximum_absolute, maximum_scaled
        if type(left) is float and type(right) is float:
            difference = abs(left - right)
            allowance = absolute_tolerance + relative_tolerance * max(abs(left), abs(right))
            maximum_absolute = max(maximum_absolute, difference)
            maximum_scaled = max(maximum_scaled, difference / allowance if allowance else 0.)
            if not math.isfinite(left) or not math.isfinite(right) or difference > allowance:
                differences.append(path)
        elif isinstance(left, dict) and isinstance(right, dict):
            if set(left) != set(right):
                differences.append(path + ':keys'); return
            for key in left: visit(left[key], right[key], path + '.' + str(key))
        elif isinstance(left, list) and isinstance(right, list):
            if len(left) != len(right): differences.append(path + ':length'); return
            for index, (a, b) in enumerate(zip(left, right)): visit(a, b, path + '[' + str(index) + ']')
        elif type(left) is not type(right) or left != right:
            differences.append(path)
    visit(scalar, batched, 'report')
    return {'passed': not differences, 'different_paths': differences,
            'maximum_absolute_float_difference': maximum_absolute,
            'maximum_fraction_of_float_tolerance': maximum_scaled,
            'relative_tolerance': relative_tolerance, 'absolute_tolerance': absolute_tolerance,
            'scope': 'All report keys/metadata, integer ranks/histograms and hashes compare exactly; FP64 numerical fields use the stated scale-aware tolerance.'}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path); parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--batch-blocks', type=int, default=DEFAULT_BATCH_BLOCKS)
    args = parser.parse_args(argv)
    scalar = None; scalar_seconds = None
    if args.benchmark:
        begin = time.perf_counter(); scalar, _ = REFERENCE.analyze_workspace(args.directory)
        scalar_seconds = time.perf_counter() - begin
    begin = time.perf_counter(); result, _ = analyze_workspace(args.directory, batch_blocks=args.batch_blocks)
    seconds = time.perf_counter() - begin
    comparison = compare_reports(scalar, result) if scalar is not None else None
    report = {'schema': 'triad-retention-batch-check-v1', 'source_bindings': source_bindings(),
              'directory': str(args.directory.resolve()), 'batch_blocks': args.batch_blocks,
              'scalar_seconds': scalar_seconds, 'batch_seconds': seconds,
              'scalar_over_batch_wall_ratio': scalar_seconds / seconds if scalar_seconds is not None else None,
              'comparison': comparison, 'retention': result,
              'environment': {'numpy': np.__version__,
                  'cpu_affinity': sorted(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else None,
                  'thread_environment': {name: os.environ.get(name) for name in ('OMP_NUM_THREADS', 'OPENBLAS_NUM_THREADS', 'MKL_NUM_THREADS')}},
              'scientific_qualification': False,
              'performance_scope': 'Single-process sequential measurement includes parsing, every block projection, hashing and validation; not a repeated hardware qualification or full-campaign estimate.'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x') as stream:
        json.dump(report, stream, allow_nan=False, sort_keys=True, indent=2); stream.write('\n')
    print(json.dumps({k: report[k] for k in ('scalar_seconds', 'batch_seconds', 'scalar_over_batch_wall_ratio', 'comparison')}))
    return 0 if comparison is None or comparison['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
