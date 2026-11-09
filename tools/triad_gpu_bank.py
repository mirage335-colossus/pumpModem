"""Device execution for the canonical coarse FFTScreen.

The screen still owns the public grid, finite projection, rank cutoff, work
limits, and proposal merge. The GPU holds projected columns and transforms;
keyed native samples and the small FP64 rank decisions stay on the host.
"""
from __future__ import annotations

from contextlib import contextmanager, nullcontext
from functools import partial
import hashlib
import math
from pathlib import Path
import time

import numpy as np

from triad_realtime_fft import (FFTScreen, _append_candidate_rows, _candidate_row, _digest,
    _fir_decimate_chunks, _project_coarse_column, _quadratic_scores)


class _CudaBank:
    def __init__(self, device, workspace_bytes):
        import cupy as cp
        from cupyx.scipy import signal

        name = str(device)
        if name == 'cuda':
            index = cp.cuda.runtime.getDevice()
        elif name.startswith('cuda:') and name[5:].isdigit():
            index = int(name[5:])
        else:
            raise ValueError('coarse GPU device must be cuda or cuda:N')
        if type(workspace_bytes) is not int or workspace_bytes < 128*1024**2:
            raise ValueError('coarse GPU workspace must be at least 128 MiB')
        self.cp, self.signal, self.device = cp, signal, cp.cuda.Device(index)
        self.pool = cp.cuda.MemoryPool()
        self.workspace_bytes = workspace_bytes
        with self.device:
            free, total = cp.cuda.runtime.memGetInfo()
            if workspace_bytes > free:
                raise MemoryError('declared coarse GPU workspace exceeds available device memory')
            # CuPy allocates cuFFT work areas through the active array
            # allocator. They share the fixed pool with all live tensors.
            # A smaller retention limit would reject even a valid single
            # plan before the transform runs (CuPy PlanCache._check_plan_fit).
            self.pool.set_limit(size=workspace_bytes)
            cache = cp.fft.config.get_plan_cache()
            cache.clear()
            self.plan_cache_count = 4
            cache.set_size(self.plan_cache_count)
            cache.set_memsize(workspace_bytes)
            props = cp.cuda.runtime.getDeviceProperties(index)
            gpu = props['name']
            self.identity = dict(device='cuda:'+str(index), name=gpu.decode() if isinstance(gpu, bytes) else gpu,
                cupy=cp.__version__, cuda_runtime=cp.cuda.runtime.runtimeGetVersion(),
                total_device_bytes=int(total), available_device_bytes=int(free))

    @contextmanager
    def active(self):
        with self.device, self.cp.cuda.using_allocator(self.pool.malloc):
            yield

    def batch_width(self, count, per_lane_bytes, *, maximum=16):
        headroom = self.workspace_bytes-self.pool.used_bytes()
        width = min(count, maximum, headroom//per_lane_bytes)
        if width < 1:
            raise MemoryError('coarse GPU workspace cannot admit one correlation lane')
        return width


def make_gpu_screen_factory(*, device, workspace_bytes):
    """Create the optional factory accepted by the canonical public bank builder."""
    return partial(GpuFFTScreen, cuda=_CudaBank(device, workspace_bytes))


class GpuFFTScreen(FFTScreen):
    device_parallel = True

    def __init__(self, *args, cuda, **kwargs):
        self.cuda = cuda
        self.gpu_work = dict(mapped_projection_batches=0, integer_projection_columns=0,
            frequency_batches=0, lag_batches=0, gpu_peak_rows=0, host_peak_fallback_rows=0,
            projected_input_payload_bytes=0, mapped_template_payload_bytes=0)
        kwargs['precision'] = 'fp32'
        with cuda.active():
            started = time.perf_counter()
            super().__init__(*args, **kwargs)
            cuda.cp.cuda.get_current_stream().synchronize()
            self.gpu_preparation_wall_seconds = time.perf_counter()-started
        self.policy.update(coarse_device='cuda', preparation_projection_workers=0)
        self.procedure_id = _digest(dict(canonical=self.procedure_id,
            device_backend_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            runtime=cuda.identity))

    def gpu_workspace_receipt(self):
        with self.cuda.device:
            return dict(self.cuda.identity, workspace_limit_bytes=self.cuda.workspace_bytes,
                array_pool_limit_bytes=self.cuda.workspace_bytes,
                array_pool_live_bytes=self.cuda.pool.used_bytes(),
                array_pool_reserved_bytes=self.cuda.pool.total_bytes(),
                fft_plan_cache_limit_bytes=self.cuda.workspace_bytes,
                fft_plan_cache_limit_count=self.cuda.plan_cache_count,
                workspace_scope='Coarse array and cuFFT work-area allocations share one bounded pool. '
                                'Pool values are snapshots, excluding CUDA runtime and separate Torch allocations.',
                precision=dict(projection='complex64 with float32 FIR taps',
                    frequency_fft='complex64', lag_zoom_fft='CuPy complex128 chirps; complex64 correlations',
                    gram_and_rank='complex128', quadratic='complex128',
                    host_peak_fallback='exact ties, plateaus, and candidate caps above eight'),
                payload_scope='Normalized array size before device work; not a bus traffic measurement.',
                preparation_wall_seconds=self.gpu_preparation_wall_seconds,
                work=dict(self.gpu_work))

    def _projection_pool(self, workers):
        return nullcontext()

    def _allocate_bank_columns(self, shape):
        return self.cuda.cp.empty(shape, dtype=self.dtype)

    def _write_bank_column(self, destination, column):
        destination[...] = self.cuda.cp.asarray(column, dtype=self.dtype)

    def _project_mapped_bank(self, destination, coarse, collapsed, projection):
        self.gpu_work['mapped_projection_batches'] += 1
        self.gpu_work['mapped_template_payload_bytes'] += coarse.size*np.dtype(self.dtype).itemsize
        destination[...] = _project_coarse_column(coarse, self.raw_kernel_rows, self.taps,
            self.template_sampling_step, self.decimation, *collapsed, self.dtype,
            xp=self.cuda.cp, signal_api=self.cuda.signal, arithmetic=np.complex64)

    def _project_integer_column(self, chunks):
        self.gpu_work['integer_projection_columns'] += 1
        cp = self.cuda.cp
        return _fir_decimate_chunks(chunks, self.taps, self.decimation, self.raw_kernel_rows,
            self.dtype, self.template_chunk_samples, xp=cp, fft_api=cp.fft)

    def _bank_arrays(self, bank_columns):
        cp = self.cuda.cp
        values = bank_columns.astype(cp.complex128)
        grams = cp.asnumpy(values.conj() @ values.swapaxes(-1, -2))
        transformed = cp.fft.fft(bank_columns, n=self.nfft, axis=-1).conj()
        del values
        yield from zip(bank_columns, grams, transformed, strict=True)

    def prepare_observation(self, raw, valid=None):
        # The inherited final crop also allocates, so keep the selected device
        # and bounded allocator active until that copy has completed.
        with self.cuda.active():
            projected = super().prepare_observation(raw, valid)
            self.cuda.cp.cuda.get_current_stream().synchronize()
            return projected

    def _filter_observation(self, raw):
        with self.cuda.active():
            cp = self.cuda.cp
            self.gpu_work['projected_input_payload_bytes'] += raw.size*np.dtype(self.dtype).itemsize
            result = self.cuda.signal.upfirdn(cp.asarray(self.taps, dtype=cp.float32),
                cp.asarray(raw, dtype=self.dtype), down=self.decimation)
            cp.cuda.get_current_stream().synchronize()
            return result

    def _coerce_projected(self, projected, *, copy=False):
        with self.cuda.active():
            cp = self.cuda.cp
            observation = cp.asarray(projected)
            if (observation.shape != (self.crop_rows,) or observation.dtype.kind != 'c' or
                    not bool(cp.isfinite(observation).all())):
                raise ValueError('finite prepared projection must match this screen crop')
            observation = observation.astype(self.dtype, copy=False)
            return observation.copy() if copy else observation

    def _mark_readonly(self, values):
        # CuPy's array flags do not expose write protection. The canonical
        # device session owns the array and no scoring method writes to it.
        pass

    def _frequency_spectra(self, observation, frequencies, *, workers=None):
        with self.cuda.active():
            cp = self.cuda.cp
            spectra = cp.empty((len(frequencies), self.nfft), dtype=self.dtype)
            times = cp.arange(len(observation), dtype=cp.float64)/self.output_fs
            for first in range(0, len(frequencies), self.batch_size):
                stop = min(first+self.batch_size, len(frequencies))
                cycles = cp.remainder(cp.asarray(frequencies[first:stop])[:, None]*times, 1.)
                angles = (-2*math.pi*cycles).astype(cp.float32)
                rotation = cp.exp(1j*angles).astype(self.dtype, copy=False)
                spectra[first:stop] = cp.fft.fft(observation[None, :]*rotation,
                    n=self.nfft, axis=-1)
            cp.cuda.get_current_stream().synchronize()
            return spectra

    def _scores(self, workspaces, correlations):
        cp = self.cuda.cp
        inverses = cp.asarray(np.stack([row['inverse'] for row in workspaces]))
        explicit = np.array([row['quadratic_explicit'] for row in workspaces])
        shape = correlations.shape[:-2]+(correlations.shape[-1],)
        scores = cp.empty(shape, dtype=cp.float64)
        for mode in (True, False):
            selected = np.flatnonzero(explicit == mode)
            if len(selected):
                indices = cp.asarray(selected)
                scores[indices] = _quadratic_scores(correlations[indices], inverses[indices],
                    explicit=mode, xp=cp)/self.reference_variance
        return scores

    def _peak_rows(self, workspaces, frequencies, scores, separation, maximum):
        """Keep only the highest separated local maxima per lane/CFO on CUDA."""
        cp = self.cuda.cp
        if not bool(cp.isfinite(scores).all()):
            raise FloatingPointError('nonfinite coarse GPU scores')
        retained = [[] for _ in workspaces]
        if self.lag_count == 1:
            host = cp.asnumpy(scores[..., 0])
            for lane, (workspace, values) in enumerate(zip(workspaces, host, strict=True)):
                retained[lane].extend(_candidate_row(workspace, freq, self.arrivals[0], score)
                    for freq, score in zip(frequencies, values, strict=True))
            self.gpu_work['gpu_peak_rows'] += host.size
            return retained
        if maximum > 8:
            host = cp.asnumpy(scores)
            for lane, (workspace, values) in enumerate(zip(workspaces, host, strict=True)):
                _append_candidate_rows(retained[lane], workspace, frequencies, values,
                    self.arrivals, separation, maximum)
            self.gpu_work['host_peak_fallback_rows'] += math.prod(scores.shape[:-1])
            return retained
        # scipy.find_peaks resolves plateaus and exact priority ties by its own
        # ordering. Leave those rows to it. The highest remaining strict
        # interior peak suppresses only lower peaks within the distance.
        fallback = cp.any(scores[..., 1:] == scores[..., :-1], axis=-1)
        active = cp.full(scores.shape, -cp.inf, dtype=cp.float64)
        interior = (scores[..., 1:-1] > scores[..., :-2]) & (scores[..., 1:-1] > scores[..., 2:])
        active[..., 1:-1] = cp.where(interior, scores[..., 1:-1], -cp.inf)
        lags = cp.arange(self.lag_count)
        best_lags, best_scores = [], []
        for _ in range(maximum):
            peak = cp.argmax(active, axis=-1)
            value = cp.take_along_axis(active, peak[..., None], axis=-1)[..., 0]
            fallback |= cp.isfinite(value) & (cp.sum(active == value[..., None], axis=-1) > 1)
            best_lags.append(peak)
            best_scores.append(value)
            active = cp.where(cp.abs(lags-peak[..., None]) < separation, -cp.inf, active)
        best_lags.extend((cp.zeros(scores.shape[:-1], dtype=cp.int64),
                          cp.full(scores.shape[:-1], self.lag_count-1, dtype=cp.int64)))
        best_scores.extend((cp.where(scores[..., 0] >= scores[..., 1], scores[..., 0], -cp.inf),
                            cp.where(scores[..., -1] >= scores[..., -2], scores[..., -1], -cp.inf)))
        values = cp.stack(best_scores, axis=-1)
        indices = cp.stack(best_lags, axis=-1)
        order = cp.argsort(values, axis=-1)[..., ::-1]
        ordered = cp.take_along_axis(values, order, axis=-1)
        fallback |= cp.any(cp.isfinite(ordered[..., :-1]) &
                          (ordered[..., 1:] == ordered[..., :-1]), axis=-1)
        host_scores = cp.asnumpy(ordered[..., :maximum])
        host_lags = cp.asnumpy(cp.take_along_axis(indices, order[..., :maximum], axis=-1))
        host_fallback = cp.asnumpy(fallback)
        fallback_values = iter(cp.asnumpy(scores[fallback]))
        self.gpu_work['gpu_peak_rows'] += int((~host_fallback).sum())
        self.gpu_work['host_peak_fallback_rows'] += int(host_fallback.sum())
        for wi, workspace in enumerate(workspaces):
            for fi, frequency in enumerate(frequencies):
                if host_fallback[wi, fi]:
                    _append_candidate_rows(retained[wi], workspace, [frequency],
                        next(fallback_values)[None, :], self.arrivals, separation, maximum)
                else:
                    retained[wi].extend(_candidate_row(workspace, frequency, self.arrivals[lag], score)
                        for lag, score in zip(host_lags[wi, fi], host_scores[wi, fi], strict=True)
                        if math.isfinite(score))
        return retained

    def _frequency_workspace_rows(self, spectra, frequencies, *, sample_separation, top_per_bit):
        with self.cuda.active():
            cp = self.cuda.cp
            width = self.cuda.batch_width(len(self.workspaces),
                min(self.batch_size, len(frequencies))*len(self.rays)*self.nfft*8*8)
            for first in range(0, len(self.workspaces), width):
                workspaces = self.workspaces[first:first+width]
                transforms = cp.stack([row['transform'] for row in workspaces])
                retained = [[] for _ in workspaces]
                for f0 in range(0, len(frequencies), self.batch_size):
                    f1 = min(f0+self.batch_size, len(frequencies))
                    products = transforms[:, None]*spectra[None, f0:f1, None]
                    correlations = cp.fft.ifft(products, axis=-1)[..., :self.lag_count]
                    scores = self._scores(workspaces, correlations)
                    rows = self._peak_rows(workspaces, frequencies[f0:f1], scores,
                        sample_separation, top_per_bit)
                    for target, values in zip(retained, rows, strict=True):
                        target.extend(values)
                    self.gpu_work['frequency_batches'] += 1
                    del products, correlations, scores
                yield from retained

    def _lag_workspace_rows(self, observation, frequencies, groups, *, sample_separation, top_per_bit):
        with self.cuda.active():
            cp = self.cuda.cp
            plans = self._lag_plans(frequencies, groups, self.cuda.signal.ZoomFFT)
            indices = cp.arange(self.kernel_rows)
            lag_batch = min(self.lag_count, self.batch_size)
            zoom_rows = max((plan[3] for plan in plans), default=0)
            width = self.cuda.batch_width(len(self.workspaces),
                len(self.rays)*(len(frequencies)*self.lag_count*32+
                    lag_batch*(self.kernel_rows*16+max(zoom_rows, self.kernel_rows)*16*6)))
            for first in range(0, len(self.workspaces), width):
                workspaces = self.workspaces[first:first+width]
                columns = cp.stack([row['columns'] for row in workspaces]).conj()
                correlations = cp.empty((len(workspaces), len(frequencies),
                    len(self.rays), self.lag_count), dtype=self.dtype)
                for values, positions, transform, _ in plans:
                    values_device, positions_device = cp.asarray(values), cp.asarray(positions)
                    modulation = (cp.exp(-2j*math.pi*values[0]*indices/self.output_fs)
                                  if transform is None else None)
                    for lo in range(0, self.lag_count, lag_batch):
                        hi = min(lo+lag_batch, self.lag_count)
                        lags = cp.arange(lo, hi)
                        windows = observation[lags[:, None]+indices[None, :]]
                        mixed = columns[:, :, None, :]*windows[None, None]
                        transformed = (cp.sum(mixed*modulation, axis=-1, keepdims=True)
                                       if transform is None else transform(mixed, axis=-1))
                        phase = cp.exp(-2j*math.pi*values_device[:, None]*lags[None, :]/self.output_fs)
                        correlations[:, positions_device, :, lo:hi] = (
                            transformed.transpose(0, 3, 1, 2)*phase[None, :, None, :])
                        self.gpu_work['lag_batches'] += 1
                        del mixed, transformed
                yield from self._peak_rows(workspaces, frequencies, self._scores(workspaces, correlations),
                    sample_separation, top_per_bit)


def compare_gpu_bank(cpu, gpu):
    """Check all retained public operators without downloading the full device bank."""
    geometry = ('fs', 'output_fs', 'samples', 'crop_first', 'crop_rows', 'kernel_rows',
        'nfft', 'source_samples', 'template_source_identity', 'clocks', 'epochs', 'rays')
    if any(getattr(cpu, key) != getattr(gpu, key) for key in geometry):
        raise ValueError('coarse GPU bank has a different public geometry')
    if len(cpu.workspaces) != len(gpu.workspaces):
        raise ValueError('coarse GPU bank has a different public lane count')
    metrics = dict(alternatives=len(cpu.workspaces), rank_agreement=True,
        quadratic_branch_agreement=True, maximum_column_relative_error=0.,
        maximum_gram_relative_error=0., maximum_projector_residual=0.)
    with gpu.cuda.active():
        cp = gpu.cuda.cp
        for left, right in zip(cpu.workspaces, gpu.workspaces, strict=True):
            if any(left[key] != right[key] for key in ('epoch', 'bit', 'clock_ppm')):
                raise ValueError('coarse GPU bank changed public lane order')
            metrics['rank_agreement'] &= left['rank'] == right['rank']
            metrics['quadratic_branch_agreement'] &= left['quadratic_explicit'] == right['quadratic_explicit']
            a, b = cp.asarray(left['columns'], dtype=cp.complex128), right['columns'].astype(cp.complex128)
            relative = float(cp.linalg.norm(a-b)/cp.linalg.norm(a))
            gram_error = float(np.linalg.norm(left['gram']-right['gram'])/np.linalg.norm(left['gram']))
            metrics['maximum_column_relative_error'] = max(metrics['maximum_column_relative_error'], relative)
            metrics['maximum_gram_relative_error'] = max(metrics['maximum_gram_relative_error'], gram_error)
            if left['rank'] == right['rank']:
                spaces = []
                for workspace, columns in ((left, a), (right, b)):
                    eigenvalues, eigenvectors = np.linalg.eigh(
                        (workspace['gram']+workspace['gram'].conj().T)*.5)
                    rank = workspace['rank']
                    spaces.append(columns.T @ cp.asarray(eigenvectors[:, -rank:]/np.sqrt(eigenvalues[-rank:])))
                q, r = spaces
                residual = float(cp.linalg.norm(q-r @ (r.conj().T @ q)))
                metrics['maximum_projector_residual'] = max(metrics['maximum_projector_residual'], residual)
    limits = dict(maximum_column_relative_error=2e-4, maximum_gram_relative_error=2e-4,
                  maximum_projector_residual=1e-3)
    metrics['accepted'] = (metrics['rank_agreement'] and metrics['quadratic_branch_agreement'] and
                           all(metrics[key] <= limit for key, limit in limits.items()))
    metrics['limits'] = limits
    return metrics
