#!/usr/bin/env python3
"""Run receivers on independent sampled channel paths in one batched stream.

Every workspace is an independently keyed/channel-seeded sampled path. One
thermal-noise realization per path is the unit of the reported confidence
interval; the paired H0/H1 simulations reuse its thermal seed. The detector
receives only normalized projected observations and public Gram matrices.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import itertools
import json
import math
from pathlib import Path
import platform
import sys

import numpy as np
import torch

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
from triad_state_workspace import BlockWorkspace, DetectorBlock, WorkspaceError

_SPEC = importlib.util.spec_from_file_location('triad_state_study', TOOLS/'triad-state-study.py')
_STUDY = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_STUDY)


def _parse():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('inputs', type=Path, nargs='+', help='one independent sampled channel export per path')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cn0', type=float, nargs='+', default=[-24.])
    p.add_argument('--seed', type=int, default=83021)
    p.add_argument('--split', choices=['development', 'calibration', 'holdout'], default='development')
    p.add_argument('--device', default='cpu')
    p.add_argument('--precision', choices=['fp32', 'fp64'], default='fp64')
    p.add_argument('--threads', type=int, default=2)
    p.add_argument('--methods', nargs='+', choices=['gaussian', 'phase-frequency', 'block-energy'],
                   default=['gaussian', 'block-energy'])
    p.add_argument('--coherences', type=float, nargs='+', default=[16., 64., 256., 1024.])
    p.add_argument('--taps', type=int, choices=[1, 2], default=2)
    p.add_argument('--phase-states', type=int, default=512)
    p.add_argument('--relative-phase-states', type=int, default=8)
    p.add_argument('--frequency-bound', type=float, default=.02)
    p.add_argument('--frequency-step', type=float, default=None)
    p.add_argument('--frequency-std', type=float, default=.003)
    p.add_argument('--frequency-coherence', type=float, default=1000.)
    p.add_argument('--phase-diffusion', type=float, default=.0153)
    p.add_argument('--memory-limit-mib', type=int, default=32768,
                   help='guard for HMM and per-path tensor working set (MiB)')
    p.add_argument('--false-probability', type=float, default=1e-4)
    p.add_argument('--horizon-seconds', type=float, default=86400.)
    p.add_argument('--attempt-spacing', type=float, default=60.)
    p.add_argument('--duration-alternatives', type=int, default=1)
    p.add_argument('--selection-methods', type=int)
    p.add_argument('--actual-observation', action='store_true',
                   help='score the independent sampled noisy projection in every export')
    a = p.parse_args()
    if a.output.exists():
        p.error('output must be fresh')
    if not 1 <= len(a.inputs) <= 2000 or a.threads < 1 or a.duration_alternatives < 1 or a.memory_limit_mib < 1:
        p.error('invalid input count, thread count or duration count')
    if (not math.isfinite(a.false_probability) or not 0 < a.false_probability < 1 or
            not math.isfinite(a.horizon_seconds) or a.horizon_seconds <= 0 or
            not math.isfinite(a.attempt_spacing) or a.attempt_spacing <= 0):
        p.error('invalid operational false-admission budget')
    if not a.cn0 or any(not math.isfinite(x) for x in a.cn0) or any(not math.isfinite(x) or x <= 0 for x in a.coherences):
        p.error('invalid C/N0 or coherence values')
    if not a.methods or len(set(a.methods)) != len(a.methods):
        p.error('receiver methods must be distinct')
    a.selection_methods = a.selection_methods or len(a.methods)
    if a.selection_methods < len(a.methods):
        p.error('selection-methods omits an exercised receiver')
    return a


def _seed(root: int, split: str, index: int) -> int:
    salts = {'development': 0x444556, 'calibration': 0x43414C, 'holdout': 0x484F4C44}
    return int(np.random.SeedSequence([root, salts[split], index]).generate_state(1, dtype=np.uint64)[0])


def _path_seed(root, split, manifest):
    identity = [manifest['channel_seed'], manifest['key_seed'], manifest['epoch']]
    digest = hashlib.sha256(json.dumps(['triad-path-noise-v1', root, split, identity],
                                      separators=(',', ':')).encode()).digest()
    return int.from_bytes(digest[:8], 'little')


def _selected(workspaces, taps):
    first = workspaces[0]
    chosen = [i for i, mapping in enumerate(first.column_mapping) if len(mapping) == taps]
    if not chosen:
        raise WorkspaceError('no candidates have the requested tap dimension')
    signature = [(first.candidates[i]['bit'], first.column_mapping[i], first.candidates[i]['tap_count'])
                 for i in chosen]
    for ws in workspaces[1:]:
        other = [i for i, mapping in enumerate(ws.column_mapping) if len(mapping) == taps]
        other_signature = [(ws.candidates[i]['bit'], ws.column_mapping[i], ws.candidates[i]['tap_count'])
                           for i in other]
        if other_signature != signature:
            raise WorkspaceError('independent paths do not share the same public candidate bank')
        if ws.dimension != first.dimension:
            raise WorkspaceError('independent paths have different full Gram extents')
        for field in ('channel_model', 'oscillator_model', 'source_format', 'channel', 'control'):
            if ws.manifest.get(field) != first.manifest.get(field):
                raise WorkspaceError(f'independent paths have different generative {field}')
        left_argv, right_argv = first.manifest.get('command_argv'), ws.manifest.get('command_argv')
        if (left_argv is None) != (right_argv is None):
            raise WorkspaceError('path manifests disagree about generative command provenance')
        if left_argv is not None:
            ignored_values = {'--key-seed', '--channel-seed', '--epoch', '--signal-bit',
                              '--output', '--seed', '--cn0'}
            def normalized(argv):
                output, skip = [], False
                for item in argv:
                    if skip:
                        skip = False
                        continue
                    if item in ignored_values:
                        skip = True
                    elif item != '--now':
                        output.append(item)
                return output
            if normalized(left_argv) != normalized(right_argv):
                raise WorkspaceError('independent paths use different simulation/configuration arguments')
    labels = np.asarray([first.candidates[i]['bit'] for i in chosen], dtype=np.int64)
    if set(labels.tolist()) != {0, 1}:
        raise WorkspaceError('public candidate bank must include both bits')
    return chosen, labels


def _validate_calendar(workspaces):
    ref = workspaces[0]
    for ws in workspaces[1:]:
        for field in ('block_count', 'dimension', 'sample_rate', 'sample_count', 'block_samples'):
            if getattr(ws, field) != getattr(ref, field):
                raise WorkspaceError(f'independent paths differ in public block calendar: {field}')
        if not np.array_equal(next(ws.iter_public()).intervals,
                              next(ref.iter_public()).intervals):
            raise WorkspaceError('independent paths have different initial sample intervals')
        if ws.candidates != ref.candidates or ws.manifest['column_descriptors'] != ref.manifest['column_descriptors']:
            raise WorkspaceError('independent paths do not share identical public acquisition hypotheses')
    return ref


def _memory_guard(args, workspace, selected):
    """Conservative HMM tensor estimate before state allocation."""
    dimension = workspace.dimension
    gram_and_noise = len(args.inputs)*(dimension*dimension*16*4 + dimension*16*4)
    if 'phase-frequency' in args.methods:
        dt = workspace.block_samples/workspace.sample_rate
        spacing = getattr(args, 'frequency_step', None) or (2/args.phase_states)/dt
        if not math.isfinite(spacing) or spacing <= 0:
            raise ValueError('invalid explicit frequency grid')
        frequency_states = 2*math.ceil(args.frequency_bound/spacing)+1
        quadrature = len((.5, 1., 2.))*args.relative_phase_states
        elements = len(args.inputs)*len(selected)*quadrature*frequency_states*args.phase_states
        hmm_bytes = elements*(4 if args.precision == 'fp32' else 8)*16 + 4*(frequency_states*frequency_states*16+
                     frequency_states*args.phase_states*24+args.phase_states*32)+65536
    else:
        hmm_bytes = 0
    if gram_and_noise+hmm_bytes > args.memory_limit_mib*1024*1024:
        raise MemoryError(f'path batch working-set estimate {gram_and_noise+hmm_bytes} bytes exceeds --memory-limit-mib')
    args.memory_limit_bytes = args.memory_limit_mib*1024*1024
    return dict(path_statistics_bytes=gram_and_noise, phase_frequency_bytes=hmm_bytes,
                total_bytes=gram_and_noise+hmm_bytes, limit_bytes=args.memory_limit_bytes)


def _validate_path_diversity(workspaces):
    bits = np.asarray([int(ws.manifest['signal_bit']) for ws in workspaces])
    if set(bits.tolist()) != {0, 1} or abs(int(bits.sum())-(len(bits)-int(bits.sum()))) > 1:
        raise WorkspaceError('physical paths must be balanced across transmitted bits')
    if len({ws.manifest['channel_seed'] for ws in workspaces}) != len(workspaces):
        raise WorkspaceError('physical channel seeds must be unique across independent paths')
    if len({ws.manifest['key_seed'] for ws in workspaces}) != len(workspaces):
        raise WorkspaceError('keystream seeds must be unique across independent paths')
    return bits


def _batched_blocks(workspaces, seeds, cn0, *, signal, actual):
    streams = []
    for ws, seed in zip(workspaces, seeds):
        if actual:
            physical_cn0 = 10*math.log10(ws.sample_rate/ws.noise_variance)
            if not math.isclose(cn0, physical_cn0, rel_tol=0, abs_tol=1e-9):
                raise ValueError('actual-observation C/N0 must match the physical export')
            streams.append(ws.iter_observation())
        else:
            streams.append(ws.iter_simulator(seed=seed, batch=1, cn0_db_hz=cn0, signal=signal))
    sentinel = object()
    consumed = 0
    try:
        for index, group in enumerate(itertools.zip_longest(*streams, fillvalue=sentinel)):
            if any(block is sentinel for block in group):
                raise WorkspaceError(f'path stream ended early at block {index}')
            if any(block.index != index for block in group):
                raise WorkspaceError('path streams are not aligned by block index')
            ref = group[0]
            if any(block.interval != ref.interval or block.dt_seconds != ref.dt_seconds for block in group[1:]):
                raise WorkspaceError('path streams have inconsistent sample timestamps')
            yield DetectorBlock(
                r=np.concatenate([block.r for block in group], axis=0),
                gram=np.stack([block.gram for block in group], axis=0),
                dt_seconds=ref.dt_seconds, interval=ref.interval,
                valid_rows=tuple(block.valid_rows for block in group), index=index,
                center_seconds=ref.center_seconds)
            consumed = index+1
        if consumed != workspaces[0].block_count:
            raise WorkspaceError(f'path streams ended after {consumed} of {workspaces[0].block_count} blocks')
    except ValueError as exc:
        raise WorkspaceError(f'cannot batch the sampled paths: {exc}') from exc


def _hoeffding_interval(successes, total, alpha=.05):
    """One-sided 95% lower/upper bounds for a mean of independent Bernoulli trials."""
    if total <= 0:
        raise ValueError('at least one independent path is required')
    empirical = successes/total
    epsilon = math.sqrt(math.log(1/alpha)/(2*total))
    return [max(0., empirical-epsilon), min(1., empirical+epsilon)]


def _chernoff_interval(successes, total, alpha=.05):
    """Separate one-sided bounds on mean success for independent Bernoullis.

    Concavity of log(1-p+p*exp(t)) bounds the joint MGF by the binomial
    MGF at average p, even when the individual path probabilities differ.
    Invert exp(-n*D(observed_fraction || p))=alpha; no iid assumption.
    """
    if not 0 <= successes <= total or total < 1 or not 0 < alpha < 1:
        raise ValueError('invalid independent Bernoulli count or tail probability')
    q, target = successes/total, -math.log(alpha)/total
    if successes == 0:
        return [0., -math.expm1(math.log(alpha)/total)]
    if successes == total:
        return [math.exp(math.log(alpha)/total), 1.]
    def divergence(p):
        return q*math.log(q/p)+(1-q)*math.log((1-q)/(1-p))
    lo, hi = 0., q
    for _ in range(60):
        mid = (lo+hi)/2
        if divergence(mid) > target: lo = mid
        else: hi = mid
    lower = lo
    lo, hi = q, 1.
    for _ in range(60):
        mid = (lo+hi)/2
        if mid == 1. or divergence(mid) > target: hi = mid
        else: lo = mid
    return [lower, hi]


def _bit_intervals(path_rows, bits):
    result = {}
    for bit in (0, 1):
        indices = np.flatnonzero(bits == bit)
        n = len(indices)
        correct = sum(path_rows[i]['signal']['correct_accepted'] for i in indices)
        wrong = sum(path_rows[i]['signal']['wrong_accepted'] for i in indices)
        null = sum(path_rows[i]['null_admitted'] for i in indices)
        result[str(bit)] = dict(paths=n, correct_accepted=correct, wrong_accepted=wrong,
                                missed=n-correct-wrong, correct_probability_iid_cp95=_STUDY.interval(correct,n),
                                null_admitted=null, null_probability_iid_cp95=_STUDY.interval(null,n),
                                interval_assumption='iid sampled keys and channel realizations conditional on this bit and fixed profile')
    return result


def _run_one(args, workspaces, ref, selected, labels, cn0, seeds, *, signal, actual=False):
    if actual:
        blocks = _batched_blocks(workspaces, seeds, cn0, signal=signal, actual=True)
    else:
        blocks = _batched_blocks(workspaces, seeds, cn0, signal=signal, actual=False)
    return _STUDY.evaluate(ref, blocks, args, selected, labels, len(workspaces))


def main():
    args = _parse()
    file_descriptor_limit = None
    try:
        import resource
        file_descriptor_limit = resource.getrlimit(resource.RLIMIT_NOFILE)[0]
        required = 6*len(args.inputs)+128
        if file_descriptor_limit != resource.RLIM_INFINITY and file_descriptor_limit < required:
            raise ValueError(f'path batch needs at most {required} open files; process limit is {file_descriptor_limit}. Use a smaller batch or raise this process soft limit within the host hard limit before launch.')
    except ImportError:
        pass  # Platforms without POSIX resource limits retain OS error reporting.
    torch.set_num_threads(args.threads)
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    workspaces = [BlockWorkspace(path) for path in args.inputs]
    ref = _validate_calendar(workspaces)
    selected, labels = _selected(workspaces, args.taps)
    bits = _validate_path_diversity(workspaces)
    memory = _memory_guard(args, ref, selected)
    if args.actual_observation and any(not ws.has_observation for ws in workspaces):
        raise ValueError('every path must include its independently sampled noisy observation')
    attempts = math.ceil(args.horizon_seconds/args.attempt_spacing)
    family = attempts*args.duration_alternatives*args.selection_methods
    rows, score_arrays = [], {}
    manifest_hashes = [hashlib.sha256(ws.manifest_path.read_bytes()).hexdigest() for ws in workspaces]
    independent_seeds = [_path_seed(args.seed, args.split, ws.manifest) for ws in workspaces]
    if len(set(independent_seeds)) != len(workspaces):
        raise ValueError('derived thermal noise seeds collided')
    for ci, cn0 in enumerate(args.cn0):
        positive, positive_diag, positive_wall = _run_one(
            args, workspaces, ref, selected, labels, cn0, independent_seeds, signal=True)
        null, null_diag, null_wall = _run_one(
            args, workspaces, ref, selected, labels, cn0, independent_seeds, signal=False)
        actual, actual_diag, actual_wall = (None, None, None)
        if args.actual_observation:
            actual, actual_diag, actual_wall = _run_one(
                args, workspaces, ref, selected, labels, cn0, independent_seeds,
                signal=True, actual=True)
        for method in args.methods:
            hypotheses = len(selected) if method == 'block-energy' else 2
            threshold = math.log(family*hypotheses/args.false_probability)
            rows_for_method = []
            for i, ws in enumerate(workspaces):
                row = dict(schema='triad-path-study-v1', path=str(args.inputs[i]),
                           manifest_sha256=manifest_hashes[i], channel_seed=ws.manifest['channel_seed'],
                           key_seed=ws.manifest['key_seed'], epoch=ws.manifest['epoch'],
                           transmitted_bit=int(bits[i]), independent_path_index=i,
                           cn0_db_hz=cn0, split=args.split, noise_seed=independent_seeds[i],
                           elapsed_seconds=ws.sample_count/ws.sample_rate,
                           admission_threshold=threshold, attempts_per_horizon=attempts,
                           horizon_seconds=args.horizon_seconds, false_probability=args.false_probability,
                           duration_alternatives=args.duration_alternatives,
                           selection_methods=args.selection_methods,
                           signal=_STUDY.describe(positive[method][i:i+1], int(bits[i]), threshold),
                           null_admitted=bool(np.max(null[method][i]) >= threshold),
                           actual_sampled_noise=None if actual is None else
                               _STUDY.describe(actual[method][i:i+1], int(bits[i]), threshold))
                rows_for_method.append(row)
            successes = int(sum(row['signal']['correct_accepted'] for row in rows_for_method))
            false_count = int(sum(row['null_admitted'] for row in rows_for_method))
            summary = dict(schema='triad-path-method-summary-v1', method=method,
                           cn0_db_hz=cn0, independent_physical_paths=len(workspaces),
                           balanced_signal_bits={"0": int(np.sum(bits == 0)), "1": int(np.sum(bits == 1))},
                           correct_accepted=successes, wrong_accepted=int(sum(
                               row['signal']['wrong_accepted'] for row in rows_for_method)),
                           missed=len(workspaces)-successes-int(sum(
                               row['signal']['wrong_accepted'] for row in rows_for_method)),
                           correct_probability_independent_hoeffding95=_hoeffding_interval(successes,len(workspaces)),
                           correct_probability_independent_chernoff95=_chernoff_interval(successes,len(workspaces)),
                           false_admitted=false_count,
                           false_probability_independent_hoeffding95=_hoeffding_interval(false_count,len(workspaces)),
                           false_probability_independent_chernoff95=_chernoff_interval(false_count,len(workspaces)),
                           per_bit=_bit_intervals(rows_for_method,bits),
                           threshold=threshold, family_tests_per_horizon=family*hypotheses,
                           elapsed_seconds=ref.sample_count/ref.sample_rate,
                           joint_methods_wall_seconds={"signal_and_null": positive_wall+null_wall,
                                                       "actual_sampled_noise": actual_wall})
            rows.append(dict(summary=summary, paths=rows_for_method,
                             receiver_diagnostics={"signal": positive_diag[method], "null": null_diag[method],
                                                   "actual": None if actual_diag is None else actual_diag[method]}))
            score_arrays[f'cn{ci}_{method}_signal'] = positive[method]
            score_arrays[f'cn{ci}_{method}_null'] = null[method]
            if actual is not None:
                score_arrays[f'cn{ci}_{method}_actual'] = actual[method]
            print(json.dumps(summary), flush=True)
    report = dict(schema='triad-path-study-report-v1',
                  configuration={key: [str(x) for x in value] if key == 'inputs' else
                                 str(value) if isinstance(value, Path) else value
                                 for key, value in vars(args).items()},
                  environment={"python": platform.python_version(), "torch": torch.__version__,
                               "numpy": np.__version__, "device": args.device,
                               "precision": args.precision, "open_file_soft_limit": file_descriptor_limit},
                  independent_path_manifest_hashes=manifest_hashes,
                  independent_noise_seeds=independent_seeds,
                  independent_path_count=len(workspaces), rows=rows,
                  memory_estimate=memory,
                  limitations=[
                      'The independent trial is one independently generated keyed/channel path and one thermal-noise realization.',
                      'Null and signal are paired using identical deterministic noise seeds on each path.',
                      'Thresholds use a union bound across declared repeated attempts, duration choices, receiver methods, and candidate or bit hypotheses.',
                      'Empirical confidence intervals apply only to the finite sampled path distribution and listed C/N0 values.',
                      'No field, all-oscillator, timing/acquisition, or universal optimality claim follows from these sampled paths.'])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False)+'\n')
    np.savez_compressed(args.output.with_suffix('.scores.npz'), **score_arrays)


if __name__ == '__main__':
    main()
