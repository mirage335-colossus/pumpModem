#!/usr/bin/env python3
"""Small executed-path witnesses for maintained isolated v3 receiver helpers.

These are deterministic algebraic shared-observation fixtures, not sampled
propagation. A separate untimed profile pass proves Python entry-point
execution; the timed repetition is only a diagnostic, never a full-simulation
runtime prediction or a precision-selection qualification.
"""
from __future__ import annotations

from dataclasses import replace
import argparse
import hashlib
import importlib
import importlib.util
import json
import math
from pathlib import Path
import sys
import tempfile
import time
from types import SimpleNamespace

import numpy as np

CONTRACT = Path(__file__).with_name("triad-performance-paths.json")


class PathCoverageError(RuntimeError):
    pass


def _sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def _cn(rng, shape):
    return (rng.standard_normal(shape) + 1j*rng.standard_normal(shape))/math.sqrt(2.)


def _jsonable(value):
    if isinstance(value, dict):
        return {str(k): _jsonable(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [_jsonable(v) for v in value]
    if isinstance(value, np.ndarray):
        return value.tolist()
    if isinstance(value, np.generic):
        return value.item()
    if hasattr(value, "detach"):
        return value.detach().cpu().numpy().tolist()
    return value


class _Modules:
    def __init__(self, root):
        self.directory = (Path(root).resolve(strict=True)/"tools").resolve(strict=True)
        sys.path.insert(0, str(self.directory))
        self.loaded = {}

    def verify_origins(self):
        for name, module in tuple(sys.modules.items()):
            if not name.startswith('triad_') or name.startswith('triad_perf'):
                continue
            filename = getattr(module, '__file__', None)
            if filename and Path(filename).suffix == '.py' and Path(filename).resolve().parent != self.directory:
                raise PathCoverageError('receiver dependency loaded from another source root: ' + name)

    def get(self, filename):
        self.verify_origins()
        if filename in self.loaded:
            return self.loaded[filename]
        path = self.directory/filename
        if path.is_symlink() or not path.is_file():
            raise PathCoverageError("missing or symlinked receiver module: " + filename)
        if "-" in filename:
            name = "_triad_perf_path_" + hashlib.sha256(str(path).encode()).hexdigest()[:12]
            spec = importlib.util.spec_from_file_location(name, path)
            module = importlib.util.module_from_spec(spec)
            sys.modules[name] = module
            spec.loader.exec_module(module)
        else:
            module = importlib.import_module(path.stem)
        if Path(module.__file__).resolve() != path:
            raise PathCoverageError("module loaded from another source root; use a separate process")
        self.loaded[filename] = module
        self.verify_origins()
        return module


class _Witness:
    def __init__(self, directory):
        self.directory = str(directory)
        self.calls = {}

    def __call__(self, frame, event, arg):
        if event != "call":
            return
        filename = frame.f_code.co_filename
        # Avoid allocating a Path for every standard-library/import call.
        if filename.startswith(self.directory + '/') and '/' not in filename[len(self.directory)+1:]:
            key = filename[len(self.directory)+1:] + ":" + frame.f_code.co_qualname
            self.calls[key] = self.calls.get(key, 0) + 1


def _assert_close(actual, expected, *, tolerance=2e-5):
    a, b = np.asarray(_jsonable(actual)), np.asarray(_jsonable(expected))
    if a.shape != b.shape or not np.isfinite(a).all() or not np.isfinite(b).all():
        raise PathCoverageError("nonfinite or mismatched receiver results")
    error = float(np.max(np.abs(a-b))) if a.size else 0.
    if not np.allclose(a, b, rtol=tolerance, atol=tolerance):
        raise PathCoverageError(f"receiver comparison exceeded {tolerance}: {error}")
    return error


def _state_inputs(torch, device, blocks=4):
    rng = np.random.default_rng(51231)
    design = .12*_cn(rng, (blocks, 2, 5, 2))
    # Each path supplies one observation shared by both candidate-bit designs.
    observation = _cn(rng, (blocks, 2, 5))
    gram = np.einsum('bcni,bcnj->bcij', design.conj(), design)
    r = np.einsum('bcni,bpn->bpci', design.conj(), observation)
    gram[2] = 0.; r[2] = 0.
    return torch.as_tensor(r, device=device), torch.as_tensor(gram, device=device), [0., .4, .7, .4]


def _gaussian(mods, torch, device):
    m = mods.get('triad_gaussian_state.py')
    r, gram, dts = _state_inputs(torch, device)
    results = []
    for dtype in (torch.complex128, torch.complex64):
        state = m.GaussianStateFilter(m.GaussianPrior((.4, .6), coherence_seconds=3., frequency_hz=.01), dtype=dtype)
        before_gap = None
        for j, dt in enumerate(dts):
            state.update(r[j], gram[j], dt)
            if j == 1:
                before_gap = state.scores().clone()
            if j == 2 and not torch.equal(before_gap, state.scores()):
                raise PathCoverageError('empty Gaussian block changed evidence')
        results.append(state.finish())
    return {'scores': results[0], 'fp32_fp64_max_error': _assert_close(*results),
            'geometry': {'blocks': 4, 'paths': 2, 'bits': 2, 'taps': 2}, 'empty_block_evidence_unchanged': True}


def _phase_model(mods):
    return mods.get('triad_phase_frequency.py').PhaseFrequencyModel(
        phase_states=16, frequency_grid_hz=(-.03125, 0., .03125),
        frequency_model='ou', frequency_std_hz=.02, frequency_correlation_seconds=30.,
        phase_diffusion_rad_per_sqrt_second=.03, amplitude_nodes=(.5, 1.),
        amplitude_weights=(.5, .5), relative_phase_states=2)


def _phase_run(cls, mods, torch, device, dtype, **kwargs):
    r, gram, dts = _state_inputs(torch, device)
    state = cls(2, 2, 2, _phase_model(mods), device=device, dtype=dtype, **kwargs)
    for j, dt in enumerate(dts):
        state.update(r[j], gram[j], dt)
    # Repeat a cached transition, exercising replay where the class supports it.
    state.update(r[3], gram[3], dts[3])
    scores = state.scores().clone()
    diagnostic = state.diagnostics()
    if hasattr(state, 'reset'):
        state.reset()
        if not torch.equal(state.scores(), torch.zeros_like(state.scores())):
            raise PathCoverageError('phase reset failed')
    return scores, diagnostic


def _phase(mods, torch, device, kind):
    reference = mods.get('triad_phase_frequency.py').PhaseFrequencyLikelihood
    classes = {
        'phase-reference': reference,
        'phase-optimized': mods.get('triad_phase_optimized.py').OptimizedPhaseFrequencyLikelihood,
        'phase-graph': mods.get('triad_phase_optimized.py').GraphPhaseFrequencyLikelihood,
        'phase-reduction': mods.get('triad_phase_reduction.py').Fp64ReductionGraphLikelihood,
        'phase-tensor': mods.get('triad_phase_tensor.py').TensorPhaseFrequencyLikelihood,
    }
    ref, _ = _phase_run(reference, mods, torch, device, torch.float32)
    modes = ('strict-padded', 'tf32x3') if kind == 'phase-tensor' else (None,)
    errors, diagnostics = {}, {}
    for mode in modes:
        kwargs = dict(tensor_mode=mode, tensor_fuse_advection=True) if mode else {}
        actual, diagnostic = _phase_run(classes[kind], mods, torch, device, torch.float32, **kwargs)
        errors[mode or 'fp32'] = _assert_close(actual, ref, tolerance=3e-5)
        diagnostics[mode or 'fp32'] = diagnostic
    if kind == 'phase-reference':
        fp64, _ = _phase_run(reference, mods, torch, device, torch.float64)
        errors['fp32_vs_fp64'] = _assert_close(ref, fp64, tolerance=3e-5)
    return {'scores': ref, 'maximum_score_errors': errors, 'diagnostics': diagnostics,
            'geometry': {'blocks': 5, 'paths': 2, 'bits': 2, 'taps': 2,
                         'phase_states': 16, 'frequency_states': 3, 'relative_phase_states': 2},
            'tested_tensor_modes': [m for m in modes if m]}


def _rbpf(mods, torch, device):
    rb = mods.get('triad_rbpf.py'); gs = mods.get('triad_gaussian_state.py')
    prior = gs.GaussianPrior((.4, .6), coherence_seconds=3.)
    r, gram, dts = _state_inputs(torch, device)
    state = rb.RBPhaseFilter(prior, rb.RBPhasePrior(.04), particles=3,
                            frequency_initialization='midpoint-grid', seed=719)
    references = [gs.GaussianStateFilter(prior) for _ in range(3)]
    frequencies = .04*(2*(np.arange(3)+.5)/3-1)
    elapsed = 0.
    for j, dt in enumerate(dts):
        elapsed += dt
        state.update(r[j], gram[j], dt)
        for f, reference in zip(frequencies, references):
            reference.update(r[j]*np.exp(-2j*np.pi*f*elapsed), gram[j], dt)
    expected = torch.logsumexp(torch.stack([f.finish() for f in references]), 0)-math.log(3)
    error = _assert_close(state.finish(), expected, tolerance=2e-11)
    stochastic = rb.RBPhaseFilter(gs.GaussianPrior((1.,), coherence_seconds=10.),
        rb.RBPhasePrior(.1, .8), particles=7, seed=631, resampling='systematic', ess_fraction=1.)
    for j in range(4):
        stochastic.update(torch.tensor([[1+.2j], [.3-.4j]], device=device),
                          torch.tensor([[1.]], dtype=torch.complex128, device=device), float(j>0))
    diagnostic = stochastic.diagnostics()
    if diagnostic['resampling_events_total'] < 1:
        raise PathCoverageError('resampling path was not exercised')
    _assert_close(stochastic.finish(), stochastic.finish())
    return {'scores': state.finish(), 'midpoint_mixture_error': error, 'stochastic': diagnostic,
            'geometry': {'blocks': 4, 'paths': 2, 'bits': 2, 'taps': 2, 'particles': 3},
            'stochastic_geometry': {'particles': 7, 'taps': 1, 'resampling': 'systematic'}}


def _write_stat_fixture(directory, *, seed=172, groups=False):
    """Public sufficient statistics from common actual algebraic observations."""
    directory.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(seed)
    blocks, h, rows, d = 6, (3 if groups else 1), 9, 4
    A = .3*_cn(rng, (blocks, h, rows, d)); y = _cn(rng, (blocks, rows))
    valid = np.full(blocks, rows, dtype=np.uint64)
    # Both members of the first reblock pair retain the same dependent column.
    A[:2, :, :, 3] = (.4+.2j)*A[:2, :, :, 2]
    A[2] = 0.; y[2] = 0.; valid[2] = 0
    A[-1, :, -2:] = 0.; y[-1, -2:] = 0.; valid[-1] -= 2
    G = np.einsum('bhni,bhnj->bhij', A.conj(), A)
    r = np.einsum('bhni,bn->bhi', A.conj(), y)
    E = np.einsum('bn,bn->b', y.conj(), y).real
    intervals = np.column_stack((np.arange(blocks)*rows, np.arange(1, blocks+1)*rows)).astype('<u8')
    if not groups:
        # Static/relative frequency helpers explicitly support a partial final block.
        intervals[-1, 1] -= 2
    candidates, descriptors, group_rows = [], [], []
    for gi in range(h):
        key = dict(arrival_seconds=.01*gi, frequency_hz=0., clock_ppm=0., epoch_offset_seconds=0,
                   frequency_drift_hz_per_second=0.)
        ids, maps, desc = [], [], []
        for bit in (0, 1):
            ci = len(candidates)
            taps = [dict(delay_seconds=0., doppler_hz=0.), dict(delay_seconds=.007, doppler_hz=30.)]
            candidates.append(dict(key, bit=bit, tap_count=2, taps=taps))
            ids.append(ci); maps.append([2*bit, 2*bit+1])
            for tap in taps:
                desc.append(dict(key, candidate_index=ci, source_bit=bit,
                    tap_delay_seconds=tap['delay_seconds'], tap_doppler_hz=tap['doppler_hz'],
                    linear_gain=False, gain_column=0))
        descriptors.extend(desc)
        group_rows.append(dict(sync_key=key, candidate_indices=ids, column_mapping=maps, column_descriptors=desc))
    arrays = {'received_energy.f64': E.astype('<f8'), 'intervals.u64': intervals,
              'valid_rows.u64': valid.astype('<u8')}
    m = dict(byte_order='little', complex_type='complex128', real_type='float64', integer_type='uint64',
             stats_noisy=True, gaussian_projection_supported=True, noise_variance=1., block_count=blocks, dimension=d,
             sample_count=int(intervals[-1, 1]), block_samples=rows, sample_rate=128.,
             candidate_count=len(candidates), candidates=candidates, control='null',
             received_energy_file='received_energy.f64', received_energy_shape=[blocks],
             intervals_file='intervals.u64', intervals_shape=[blocks, 2],
             valid_rows_file='valid_rows.u64', valid_rows_shape=[blocks])
    if groups:
        arrays.update({'group_gram.c128': G.astype('<c16'), 'group_observation.c128': r.astype('<c16')})
        m.update(format='triad-group-block-stats-v1', layout='block,group,row,column', group_count=h,
            groups=group_rows, cross_group_gram_saved=False, conditional_noise_resampling_supported=False,
            gram_file='group_gram.c128', gram_shape=list(G.shape),
            observation_file='group_observation.c128', observation_shape=list(r.shape))
    else:
        arrays.update({'gram.c128': G[:, 0].astype('<c16'), 'observation.c128': r[:, 0].astype('<c16'),
                       'mean.c128': np.zeros((blocks, d), dtype='<c16'), 'signal_energy.f64': np.zeros(blocks, dtype='<f8')})
        m.update(format='triad-block-stats-v1', layout='block,row,column', column_mapping=[[0, 1], [2, 3]],
            column_descriptors=descriptors, gram_file='gram.c128', gram_shape=[blocks, d, d],
            observation_file='observation.c128', observation_shape=[blocks, d],
            mean_file='mean.c128', mean_shape=[blocks, d], signal_energy_file='signal_energy.f64',
            signal_energy_shape=[blocks], received_energy_semantics='per-block sum |y|^2 of actual noisy samples over valid rows from the same second channel pass; simulation evaluation only')
    m['file_sizes'] = {name: value.nbytes for name, value in arrays.items()}
    for name, value in arrays.items():
        value.tofile(directory/name)
    (directory/'manifest.json').write_text(json.dumps(m))
    return m


def _grouped(mods, torch, device, directory):
    m = mods.get('triad_group_acquire.py')
    _write_stat_fixture(directory, groups=True)
    workspace = m.GroupedWorkspace(directory)
    config = m.AcquisitionConfig(.2, window_blocks=2, reblock=2, group_tile=2,
        projection_tile_blocks=2, device=device, precision='fp64', frequency_offsets_hz=(-.01, 0., .01))
    a = m.score_workspace(workspace, config)
    b = m.score_workspace(workspace, replace(config, group_tile=3))
    error = _assert_close(a['bit_log_likelihoods'], b['bit_log_likelihoods'], tolerance=3e-11)
    if not any(sum(histogram[:-1]) > 0 for histogram in a['calibration']['rank_histograms']):
        raise PathCoverageError('grouped rank-deficiency path was not exercised')
    if a['geometry']['reblock'] != 2 or a['geometry']['block_count'] <= config.window_blocks:
        raise PathCoverageError('grouped reblock or multiple-window path was not exercised')
    return {'scores': a['bit_log_likelihoods'], 'tile_agreement_error': error,
            'geometry': a['geometry'], 'calibration': a['calibration']}


def _static(mods, torch, device, directory):
    m = mods.get('triad-static-frequency-study.py')
    _write_stat_fixture(directory)
    ws = mods.get('triad_state_workspace.py').BlockWorkspace(directory)
    raw, projected, grams, centers, upper, calibration = m.prepare(ws, np.asarray(ws.column_mapping), 1., 1e-4)
    scores, errors = [], []
    for rr, gg, variance in ((raw, grams[0], 1.), (projected, grams[1], upper)):
        whitened, determinant = m.whiten_channel(rr/variance, gg/variance)
        nfft, bins, _ = m.grid_for(ws.block_count, ws.block_samples/ws.sample_rate, ws.sample_count/ws.sample_rate, .65)
        fft = m.frequency_correlations(whitened, centers, ws.block_samples/ws.sample_rate, nfft, bins, device=device, precision='fp32')
        direct = m.frequency_correlations(whitened, centers, ws.block_samples/ws.sample_rate, nfft, bins, implementation='direct', device=device, precision='fp64')
        a = m.scores_from_correlations(fft, determinant, np.asarray([0, 1]))
        b = m.scores_from_correlations(direct, determinant, np.asarray([0, 1]))
        errors.append(_assert_close(a, b, tolerance=3e-5)); scores.extend(a.ravel())
    return {'scores': scores, 'fft_direct_score_errors': errors, 'calibration': calibration,
            'geometry': {'blocks': ws.block_count, 'samples': ws.sample_count, 'nfft': nfft, 'frequency_bins': len(bins), 'partial_last_block': True}}


def _ray(mods, torch, device, directory):
    m = mods.get('triad-ray-frequency-study.py')
    _write_stat_fixture(directory)
    ws = mods.get('triad_state_workspace.py').BlockWorkspace(directory)
    rr, gg, centers, upper, calibration = m.prepare(ws, np.asarray(ws.column_mapping), 1., 1e-4, 4)
    f = np.arange(-2, 3); delta = np.arange(-1, 2); results = []
    errors = []
    for i, variance in enumerate((1., upper)):
        a, timings = m.joint_evidence(rr[i]/variance, gg[i]/variance, centers,
            ws.block_samples/ws.sample_rate, 32, f, delta, device=device, precision='fp32', frequency_tile=2, ray_tile=2)
        b, _ = m.joint_evidence(rr[i]/variance, gg[i]/variance, centers,
            ws.block_samples/ws.sample_rate, 32, f, delta, device=device, precision='fp64', frequency_tile=5, ray_tile=3)
        errors.append(_assert_close(a, b, tolerance=3e-5))
        results.extend(_jsonable(m.mix_candidates(a, np.asarray([0, 1]))))
    return {'scores': results, 'fp32_fp64_tile_errors': errors, 'geometry': timings, 'calibration': calibration}


def _batch(mods, torch, device, directory):
    m = mods.get('triad-cfo-batch-study.py')
    workspace_cls = mods.get('triad_state_workspace.py').BlockWorkspace
    workspaces = []
    for index in range(2):
        _write_stat_fixture(directory/str(index), seed=172+index)
        workspaces.append(workspace_cls(directory/str(index)))
    gs, rb = mods.get('triad_gaussian_state.py'), mods.get('triad_rbpf.py')
    args = SimpleNamespace(reference_variance=1., cn0=0., projection_tile_blocks=4,
        budget=SimpleNamespace(delta_per_segment=1e-4), tap_prior=gs.GaussianPrior((.4, .6), coherence_seconds=3.),
        phase_prior=rb.RBPhasePrior(.04), particles=3, seed=721, precision='fp32', device=device)
    r, g, dt, metadata = m.prepare_batch(workspaces, np.asarray([[0, 1], [2, 3]]), args)
    score, diagnostics, timings = m.score_batch(r, g, dt, args, 64*1024**2)
    single = []
    for i in range(2):
        values, _, _ = m.score_batch(r[:, i:i+1], g[:, i:i+1], dt, args, 64*1024**2)
        single.append(values)
    error = _assert_close(score, np.concatenate(single, axis=0), tolerance=3e-5)
    return {'scores': score, 'batch_single_error': error, 'calibration': metadata,
            'geometry': {'blocks': len(dt), 'paths': 2, 'candidates': 2, 'particles': 3},
            'diagnostics': diagnostics, 'stages': timings}


def collect_paths(source_root, device='cpu', threads=2, deadline=None):
    """Execute the contract once with profiling and once without profiling.

    Call in a dedicated source process if comparing different source roots.
    Ordinary CPU paths are required; CUDA-only paths are explicitly unavailable
    on CPU. Unexpected runtime/numerical failures are failures, never fallback.
    """
    import torch
    if isinstance(threads, bool) or not isinstance(threads, int) or threads < 1:
        raise ValueError('threads must be a positive integer')
    if device != 'cpu' and not device.startswith('cuda'):
        raise ValueError('device must be cpu or cuda[:index]')
    if sys.getprofile() is not None:
        raise PathCoverageError('path witness profiler may not replace an existing profiler')
    torch.set_num_threads(threads)
    torch.set_float32_matmul_precision('highest')
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    contract = json.loads(CONTRACT.read_text())
    mods = _Modules(source_root)
    start = time.perf_counter(); records = []
    backend = 'cuda' if device.startswith('cuda') else 'cpu'
    if backend == 'cuda' and not torch.cuda.is_available():
        raise PathCoverageError('requested CUDA device unavailable; CPU fallback prohibited')
    with tempfile.TemporaryDirectory(prefix='triad-perf-paths-') as tmp:
        for spec in contract['families']:
            if deadline is not None and time.monotonic() >= deadline:
                raise TimeoutError('path coverage deadline exhausted')
            name = spec['id']
            record = {'id': name, 'required_witnesses': spec['required_witnesses'], 'declared_branches': spec['branches']}
            if backend not in spec['devices']:
                record.update(status='unsupported', reason='CUDA-only implementation; no CPU substitution', applicable=False)
                records.append(record); continue
            directory = Path(tmp)/name
            if name == 'gaussian-state': fn = lambda: _gaussian(mods, torch, device)
            elif name.startswith('phase-'): fn = lambda name=name: _phase(mods, torch, device, name)
            elif name == 'rbpf': fn = lambda: _rbpf(mods, torch, device)
            elif name == 'grouped-acquisition': fn = lambda: _grouped(mods, torch, device, directory)
            elif name == 'static-frequency': fn = lambda: _static(mods, torch, device, directory)
            elif name == 'ray-frequency': fn = lambda: _ray(mods, torch, device, directory)
            elif name == 'cfo-batch': fn = lambda: _batch(mods, torch, device, directory)
            else: raise PathCoverageError('unimplemented contract family ' + name)
            witness = _Witness(mods.directory)
            try:
                profiled_start = time.perf_counter()
                sys.setprofile(witness)
                try: result = fn()
                finally: sys.setprofile(None)
                if backend == 'cuda': torch.cuda.synchronize(device)
                profiled_seconds = time.perf_counter()-profiled_start
                missing = set(spec['required_witnesses'])-set(witness.calls)
                if missing:
                    raise PathCoverageError('required execution witnesses missing: ' + ', '.join(sorted(missing)))
                if backend == 'cuda': torch.cuda.synchronize(device)
                t0 = time.perf_counter(); repeated = fn()
                if backend == 'cuda': torch.cuda.synchronize(device)
                elapsed = time.perf_counter()-t0
                _assert_close(result['scores'], repeated['scores'], tolerance=2e-10)
                source_names = {key.split(':', 1)[0] for key in witness.calls}
                record.update(status='complete', applicable=True, execution_witnesses=witness.calls,
                    source_sha256={f: _sha(mods.directory/f) for f in sorted(source_names)},
                    profiled_coverage_seconds=profiled_seconds, unprofiled_diagnostic_seconds=elapsed,
                    diagnostic_timing_scope='complete tiny helper recipe including numerical comparator, file fixture setup where applicable; not full simulation throughput',
                    result=_jsonable(result), full_runtime_prediction_qualified=False)
            except Exception as error:
                sys.setprofile(None)
                record.update(status='failed', applicable=True, error_type=type(error).__name__, error=str(error),
                              execution_witnesses=witness.calls)
            records.append(record)
    complete = all(r['status']=='complete' for r in records if r['applicable'])
    return {'schema': contract['schema'], 'scope': contract['scope'], 'contract_sha256': _sha(CONTRACT),
            'device': device, 'threads': threads, 'records': records,
            'complete_applicable_coverage': complete, 'elapsed_seconds': time.perf_counter()-start,
            'full_runtime_prediction_qualified': False, 'prediction_scope': contract['prediction_scope'],
            'deadline_scope': 'cooperative checks between families; caller must supervise process lifetime for native/kernel stalls',
            'unsupported_ids': [r['id'] for r in records if r['status']=='unsupported'],
            'failed_ids': [r['id'] for r in records if r['status']=='failed']}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--device', default='cpu')
    parser.add_argument('--threads', type=int, default=2)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--deadline-seconds', type=float, default=120.)
    args = parser.parse_args(argv)
    if not math.isfinite(args.deadline_seconds) or args.deadline_seconds <= 0:
        parser.error('deadline-seconds must be positive finite')
    if args.output.exists():
        parser.error('refusing to overwrite previous coverage evidence')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    report = collect_paths(args.source_root, args.device, args.threads,
                           time.monotonic()+args.deadline_seconds)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False)+'\n')
    print(json.dumps({'complete_applicable_coverage': report['complete_applicable_coverage'],
                      'failed_ids': report['failed_ids'], 'unsupported_ids': report['unsupported_ids'],
                      'elapsed_seconds': report['elapsed_seconds']}))
    return 0 if report['complete_applicable_coverage'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
