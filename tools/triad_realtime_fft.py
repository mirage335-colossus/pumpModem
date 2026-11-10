#!/usr/bin/env python3
"""Bounded FFT proposals for the isolated three-carrier dynamic receiver.

This is a static, complex two-ray GLRT *screen*, not calibrated admission.  It
retains both keyed bit patterns, all supplied epochs and the complete declared
integer-output-sample arrival lattice.  A fixed causal FIR projects observations
and templates to a declared coarse DATA band; full raw I/Q remains the input to
subsequent dynamic refinement.  Random AM sidebands outside that projection are
not claimed preserved.  Source/channel simulation is never changed here.

Complete source support and all-valid observations allow one small Gram per
epoch/clock/bit.  Erasures and clipped source support are explicitly unsupported.
Filtering and common-CFO modulation do not exactly commute: templates are
filtered at zero common CFO, and the screen requires every nominal shifted DATA
ray to remain inside the FIR passband.  This approximation needs sampled-channel
candidate-retention validation; it is not an exact raw-waveform likelihood.
"""
from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
from contextlib import nullcontext
import hashlib
import json
import math
from pathlib import Path
import time

import numpy as np
from scipy import fft, signal

from triad_dynamic_joint import _cubic_stencil, _sample_cubic, _sample_cubic_rows
from triad_acquisition import data_fir_taps


# Above this public geometry the whole-array estimate reserves at least512MiB
# of transient raw work. Use the same bounded FIR as the streaming entry point.
_STREAM_OBSERVATION_SAMPLES = 8*1024**2


def _number(x, name, *, positive=False):
    if isinstance(x, (bool, np.bool_)):
        raise ValueError(name + ' must be numeric')
    x = float(x)
    if not math.isfinite(x) or (positive and x <= 0):
        raise ValueError(name + ' must be finite' + (' and positive' if positive else ''))
    return x


def _integer(x, name, minimum=1):
    if isinstance(x, (bool, np.bool_)) or not isinstance(x, (int, np.integer)) or x < minimum:
        raise ValueError(name + ' must be an integer >= ' + str(minimum))
    return int(x)


def _digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, allow_nan=False).encode()).hexdigest()


def _clock():
    return time.process_time(), time.perf_counter()


def _elapsed(start):
    return dict(cpu_seconds=time.process_time()-start[0], wall_seconds=time.perf_counter()-start[1])


def _fir_decimate_chunks(chunks, taps, decimation, samples, dtype, chunk_samples, *, xp=np, fft_api=fft):
    """Exact finite causal FIR/downsample operator with bounded input tiles.

    Each tile's COMPLETE linear-convolution tail is added on the same global
    output lattice. No filter state, boundary samples or endpoint tail is
    discarded. The output covariance remains the FIR's colored covariance;
    downsampling alone does not whiten it.
    """
    count = (samples + len(taps) - 2)//decimation + 1
    output = xp.zeros(count, dtype=dtype)
    transform_rows = fft.next_fast_len(chunk_samples + len(taps) - 1)
    transformed_taps = fft_api.fft(xp.asarray(taps, dtype=dtype), n=transform_rows)
    consumed = 0
    for source in chunks:
        values = np.asarray(source)
        if (values.ndim != 1 or not np.iscomplexobj(values) or not len(values) or
                len(values) > chunk_samples or not np.isfinite(values).all()):
            raise ValueError('FIR chunks require bounded nonempty finite complex vectors')
        if consumed + len(values) > samples:
            raise ValueError('FIR stream exceeds the declared complete observation')
        filtered = fft_api.ifft(fft_api.fft(xp.asarray(values, dtype=dtype), n=transform_rows) * transformed_taps)
        filtered = filtered[:len(values) + len(taps) - 1]
        phase = (-consumed) % decimation
        first = (consumed + phase)//decimation
        kept = filtered[phase::decimation]
        output[first:first + len(kept)] += kept
        consumed += len(values)
    if consumed != samples:
        raise ValueError('FIR stream ended before the declared complete observation')
    return output


def _provider_cubic(provider, epoch, bit, positions, source_samples):
    """Apply the EXISTING raw-lattice cubic atom to bounded integer tiles."""
    first, stop = _cubic_source_bounds(positions, source_samples)
    if stop <= first:
        return np.zeros(len(positions), np.complex128)
    source = np.asarray(provider(epoch, bit, first, stop - first))
    if (source.shape != (stop - first,) or not np.iscomplexobj(source) or
            not np.isfinite(source).all()):
        raise ValueError('template provider must return the requested finite complex integer-source tile')
    return _sample_cubic(source, positions - first)


def _cubic_source_bounds(positions, source_samples):
    return (max(0, math.floor(float(np.min(positions))) - 1),
            min(source_samples, math.floor(float(np.max(positions))) + 3))


def _collapsed_cubic_fir(taps, step, downsample):
    """Compose coarse-grid cubic reconstruction with the ORIGINAL raw FIR.

    Polyphase filtering avoids materializing the upsampled raw lattice. The
    returned shift preserves raw output phase0, even when step does not divide
    the raw decimation. Only the coarse interpolation is approximate; this
    composition and its finite-boundary corrections implement that interpolant.
    """
    impulse = np.array([0., 0., 1., 0., 0.], complex)
    cardinal = _sample_cubic(impulse, np.arange(-2*step, 2*step+1)/step+2).real
    kernel = signal.convolve(taps, cardinal, mode='full')
    padding = (-2*step) % downsample
    return np.pad(kernel, (padding, 0)), (2*step+padding)//downsample


def _mapped_finite_source(provider, lanes, positions, source_samples, edge_cache):
    """Native mapped interior with the exact finite raw-cubic edge stencil."""
    result = np.zeros((len(lanes), len(positions)), np.complex128)
    low = (positions >= -2) & (positions < 1)
    high = (positions > source_samples-2) & (positions <= source_samples+1)
    interior = (positions >= 1) & (positions <= source_samples-2)
    def render(coordinates):
        values = np.asarray(provider(lanes, coordinates))
        if (values.shape != (len(lanes), len(coordinates)) or not np.iscomplexobj(values) or
                not np.isfinite(values).all()):
            raise ValueError('mapped template provider must return finite complex samples for each lane and position')
        return values
    if low.any() or high.any():
        if not edge_cache:
            edge_cache.extend((render(np.arange(4, dtype=float)),
                render(source_samples-4+np.arange(4, dtype=float))))
        first, last = edge_cache
        result[:, low] = _sample_cubic_rows(first, positions[low])
        result[:, high] = _sample_cubic_rows(last, positions[high]-(source_samples-4))
    if interior.any():
        result[:, interior] = render(positions[interior])
    return result


def _project_coarse_column(values, raw_samples, taps, step, downsample, kernel, shift, dtype,
                           *, xp=np, signal_api=signal, arithmetic=np.complex128):
    """Project the finite coarse interpolant, removing its exterior spill."""
    rows = (raw_samples+len(taps)-2)//downsample+1
    real = np.float32 if arithmetic == np.complex64 else np.float64
    filtered = signal_api.upfirdn(xp.asarray(kernel, dtype=real),
        xp.asarray(values, dtype=arithmetic), up=step, down=downsample)
    output = xp.zeros((*values.shape[:-1], rows), dtype=arithmetic)
    count = min(rows, max(0, filtered.shape[-1]-shift))
    output[..., :count] = filtered[..., shift:shift+count]
    # Cubic reconstruction has support outside the finite raw column. Those
    # samples were not part of the original operator and must be subtracted.
    output_coordinates = np.arange(rows)*downsample
    for first in (-2*step, raw_samples):
        # A caller may supply a final nonzero coarse knot AFTER raw_samples-1.
        # Its cubic support can reach almost three cells past the raw endpoint.
        coordinates = np.arange(first, first+(2 if first < 0 else 3)*step+2)
        if first < 0:
            coordinates = coordinates[coordinates < 0]
        outside = (_sample_cubic(values, coordinates/step) if values.ndim == 1 else
                   _sample_cubic_rows(values, coordinates/step))
        device_taps = xp.asarray(taps, dtype=real)
        if values.ndim == 1:
            correction = signal_api.oaconvolve(xp.asarray(outside, dtype=arithmetic), device_taps, mode='full')
        else:
            correction = signal_api.oaconvolve(xp.asarray(outside, dtype=arithmetic), device_taps[None],
                                               mode='full', axes=-1)
        indices = output_coordinates-first
        keep = np.flatnonzero((indices >= 0) & (indices < correction.shape[-1]))
        output[..., xp.asarray(keep)] -= correction[..., xp.asarray(indices[keep])]
    return output.astype(dtype)


def compare_template_projectors(exact, approximate):
    """Compare actual retained GLRT spaces for one matched local native bank.

    This diagnostic uses every supplied epoch/clock/bit and every retained ray,
    at the SAME1e-10 rank cutoff as the scorer. It is not a global interpolation
    error certificate or a communication-success estimate.
    """
    names = ('fs', 'output_fs', 'samples', 'crop_first', 'crop_rows', 'kernel_rows',
             'source_samples', 'template_source_identity', 'clocks', 'epochs', 'rays')
    if (any(getattr(exact, name) != getattr(approximate, name) for name in names) or
            not np.array_equal(exact.taps, approximate.taps)):
        raise ValueError('projector comparison requires matched source and finite projection geometry')
    def basis(workspace):
        values, vectors = np.linalg.eigh(workspace['gram'])
        keep = values > values[-1]*1e-10
        Q = workspace['columns'].T @ (vectors[:, keep]/np.sqrt(values[keep]))
        # Reorthogonalization preserves the retained span and avoids attributing
        # eigensolver normalization error to a tiny principal angle.
        return np.linalg.qr(Q, mode='reduced')[0]
    def distance(first, second):
        if first.shape[1] != second.shape[1]:
            return 1.
        singular = np.linalg.svd(first.conj().T @ second, compute_uv=False)
        return math.sqrt(max(0., 1-min(1., float(singular.min()))**2))
    if len(exact.workspaces) != len(approximate.workspaces):
        raise ValueError('projector comparison workspace coverage differs')
    banks = ({}, {})
    ranks, distances, operator_distances, column_errors, gram_errors = [], [], [], [], []
    for first, second in zip(exact.workspaces, approximate.workspaces):
        key = (first['epoch'], first['clock_ppm'], first['bit'])
        if key != (second['epoch'], second['clock_ppm'], second['bit']):
            raise ValueError('projector comparison alternative order differs')
        Q, R = basis(first), basis(second)
        banks[0][key], banks[1][key] = Q, R
        ranks.append(first['rank'] == second['rank'])
        distances.append(distance(Q, R))
        combined = np.column_stack((first['columns'].T, second['columns'].T))
        common = np.linalg.qr(combined, mode='reduced')[0]
        left = common.conj().T @ first['columns'].T
        right = common.conj().T @ second['columns'].T
        delta = (left @ first['inverse'] @ left.conj().T -
                 right @ second['inverse'] @ right.conj().T)
        operator_distances.append(float(np.linalg.norm((delta+delta.conj().T)*.5, ord=2)))
        column_errors.append(float(np.linalg.norm(first['columns']-second['columns'])/np.linalg.norm(first['columns'])))
        gram_errors.append(float(np.linalg.norm(first['gram']-second['gram'])/np.linalg.norm(first['gram'])))
    separations, reference_separations, candidate_separations = [], [], []
    for epoch in exact.epochs:
        for clock in exact.clocks:
            before = distance(banks[0][epoch, clock, 0], banks[0][epoch, clock, 1])
            after = distance(banks[1][epoch, clock, 0], banks[1][epoch, clock, 1])
            separations.append(after/max(before, 1e-30))
            reference_separations.append(before)
            candidate_separations.append(after)
    return dict(schema='triad-local-template-projectors-v1',
        alternatives=len(ranks), rank_agreement=all(ranks), rank_cutoff_relative=1e-10,
        maximum_projector_distance=max(distances),
        maximum_stored_scoring_operator_distance=max(operator_distances),
        maximum_column_relative_norm_error=max(column_errors),
        maximum_gram_relative_norm_error=max(gram_errors),
        minimum_wrong_bit_projector_separation_ratio=min(separations),
        minimum_reference_wrong_bit_projector_separation=min(reference_separations),
        minimum_candidate_wrong_bit_projector_separation=min(candidate_separations),
        scope='Matched local public bank only; all supplied alternatives/rays retained. No full-waveform interpolation, physical channel, or receiver-success qualification.')


def _quadratic_scores(correlations, inverse, *, explicit=True, xp=np):
    """Small Hermitian form without a strided BLAS contraction.

    FFT products remain FP32/FP64 as selected; the explicit two-ray arithmetic
    is FP64 in both modes.  Ill-conditioned/rank-deficient two-ray preparations
    retain the previous contraction order, rather than changing their rounding
    behavior without separate qualification.  No score clipping is introduced.
    """
    # NumPy's one-ray optimized contraction can form FP32 |c|² before its
    # FP64 scalar multiply.  Keep that existing rounding path unchanged.
    if not explicit or correlations.shape[-2] != 2:
        expression = ('brl,rs,bsl->bl' if correlations.ndim == 3 else
                      '...brl,...rs,...bsl->...bl')
        return xp.einsum(expression, correlations.conj(), inverse,
                         correlations, optimize=True).real
    a = xp.asarray(correlations[..., 0, :], dtype=np.complex128)
    result = inverse[..., 0, 0].real[..., None, None]*(a.real*a.real+a.imag*a.imag)
    b = xp.asarray(correlations[..., 1, :], dtype=np.complex128)
    result += inverse[..., 1, 1].real[..., None, None]*(b.real*b.real+b.imag*b.imag)
    result += 2*xp.real(a.conj()*inverse[..., 0, 1][..., None, None]*b)
    return result


def _candidate_peak_indices(score, sample_separation, maximum):
    """Canonical SciPy interior peaks plus independently eligible endpoints."""
    if len(score) == 1:
        return np.array([0], dtype=np.intp)
    peaks = signal.find_peaks(score, distance=sample_separation)[0]
    leading, trailing = score[0] >= score[1], score[-1] >= score[-2]
    if leading or trailing:
        # find_peaks returns sorted distinct INTERIOR indices. Endpoints can
        # be inserted in order without concatenation, sorting, or deduplication.
        offset = int(leading)
        selected = np.empty(len(peaks)+offset+int(trailing), dtype=np.intp)
        selected[offset:offset+len(peaks)] = peaks
        if leading:
            selected[0] = 0
        if trailing:
            selected[-1] = len(score)-1
        peaks = selected
    if len(peaks) > maximum:
        peaks = peaks[np.argpartition(score[peaks], -maximum)[-maximum:]]
    return peaks


def _append_candidate_rows(local, workspace, frequencies, scores, arrivals,
                           sample_separation, maximum):
    """Retain the canonical per-CFO peaks before across-frequency selection."""
    for frequency, score in zip(frequencies, scores):
        for lag in _candidate_peak_indices(score, sample_separation, maximum):
            local.append(_candidate_row(workspace, frequency, arrivals[lag], score[lag]))


def _candidate_row(workspace, frequency, arrival, score):
    return dict(bit=workspace['bit'], epoch=workspace['epoch'], arrival=float(arrival),
        frequency=float(frequency), clock_ppm=workspace['clock_ppm'], score=float(score),
        rank=workspace['rank'])


def _select_workspace_candidates(local, maximum, separation, frequency_separation):
    """Canonical stable ordering and NMS, shared by ordinary and tiled search."""
    local.sort(key=lambda candidate: candidate['score'], reverse=True)
    unique = []
    for candidate in local:
        if any(abs(candidate['arrival']-old['arrival']) < separation and
               abs(candidate['frequency']-old['frequency']) < frequency_separation for old in unique):
            continue
        unique.append(candidate)
        if len(unique) >= maximum:
            break
    return unique


def _select_bit_candidates(retained, maximum):
    return [sorted(rows, key=lambda candidate: candidate['score'], reverse=True)[:maximum]
            for rows in retained]


class FFTScreen:
    """Reusable public templates and Fourier factors, independent of observations.

    ``clock_ppm`` is a coarse covering bank, not a claim of exhaustive continuous
    clock acquisition.  Its residual distortion must be checked after arrival
    re-centering and refined by the caller.  ``score`` returns proposals for
    **both** bits; it does not commit a payload decision or an admission.
    """
    device_parallel = False

    def __init__(self, manifest, raw_templates, epoch_offsets, *,
                 clock_ppm=(-300., 0., 300.), output_sample_rate=1000,
                 precision='fp32', pass_edge_hz=400., stop_edge_hz=490.,
                 frequency_batch_size=8, workers=2,
                 memory_limit_bytes=1024**3, maximum_frequency_points=1024,
                 maximum_fft_products=32768, correlation_method='frequency', template_provider=None,
                 source_samples=None, template_identity=None, template_chunk_samples=16384,
                 mapped_template_provider=None, template_sampling_step=1,
                 mapped_template_batch_lanes=26):
        started = _clock()
        self.manifest = manifest
        self.fs = _integer(manifest['sample_rate_hz'], 'sample rate')
        self.output_fs = _integer(output_sample_rate, 'output sample rate')
        if self.fs % self.output_fs:
            raise ValueError('output sample rate must divide input sample rate')
        self.decimation = self.fs // self.output_fs
        if precision not in ('fp32', 'fp64'):
            raise ValueError('precision must be fp32 or fp64')
        self.dtype = np.complex64 if precision == 'fp32' else np.complex128
        self.real_dtype = np.float32 if precision == 'fp32' else np.float64
        self.precision = precision
        self.coarse_kernel = None
        self.workers = _integer(workers, 'workers')
        self.batch_size = _integer(frequency_batch_size, 'frequency batch size')
        self.memory_limit_bytes = _integer(memory_limit_bytes, 'memory limit')
        self.maximum_frequency_points = _integer(maximum_frequency_points, 'maximum frequency points')
        self.maximum_fft_products = _integer(maximum_fft_products, 'maximum FFT products')
        if correlation_method not in ('frequency', 'lag', 'auto'):
            raise ValueError("correlation_method must be 'frequency', 'lag', or 'auto'")
        self.correlation_method = correlation_method
        self.epochs = tuple(int(x) for x in epoch_offsets)
        if not self.epochs or len(set(self.epochs)) != len(self.epochs):
            raise ValueError('epoch offsets must be nonempty and unique')
        if template_provider is None and mapped_template_provider is None:
            if source_samples is not None or template_identity is not None:
                raise ValueError('provider metadata requires a template provider')
            source = np.asarray(raw_templates)
            if (source.ndim != 3 or source.shape[:2] != (len(self.epochs), 2) or
                    source.shape[2] < 4 or not np.iscomplexobj(source) or not np.isfinite(source).all()):
                raise ValueError('finite complex templates need [epochs,2,source samples]')
            source_samples = source.shape[-1]
            source_bytes = source.nbytes
            source_identity = hashlib.sha256(np.ascontiguousarray(source).view(np.uint8)).hexdigest()
        else:
            if (raw_templates is not None or
                    (template_provider is not None and mapped_template_provider is not None) or
                    not callable(template_provider if mapped_template_provider is None else mapped_template_provider)):
                raise ValueError('template provider requires raw_templates=None and a callable')
            source_samples = _integer(source_samples, 'source samples', minimum=4)
            if (not isinstance(template_identity, str) or len(template_identity) != 64 or
                    any(value not in '0123456789abcdef' for value in template_identity)):
                raise ValueError('template provider requires an explicit lowercase SHA256 source identity')
            source = None
            source_bytes = 0
            source_identity = template_identity
        template_chunk_samples = _integer(template_chunk_samples, 'template chunk samples')
        if template_chunk_samples > 65536:
            raise ValueError('template chunks exceed the bounded65536-sample limit')
        self.template_chunk_samples = template_chunk_samples
        self.source_samples = source_samples
        self.template_source_identity = source_identity
        self.template_sampling_step = _integer(template_sampling_step, 'template sampling step')
        if self.template_sampling_step > 65536:
            raise ValueError('template sampling step exceeds the bounded65536 raw samples')
        if mapped_template_provider is None and self.template_sampling_step != 1:
            raise ValueError('coarse template sampling requires a mapped template provider')
        self.clocks = tuple(_number(q, 'clock ppm') for q in clock_ppm)
        if (not self.clocks or len(set(self.clocks)) != len(self.clocks) or
                any(1+q*1e-6 <= 0 for q in self.clocks)):
            raise ValueError('clock bank must be nonempty, unique and have positive scales')
        cal, ch = manifest['calendar'], manifest['channel']
        self.samples = _integer(cal['capture_samples'], 'capture samples')
        self.lead = _number(cal['capture_lead_seconds'], 'capture lead')
        self.offset = _number(cal['data_sample_offset'], 'DATA sample offset')/self.fs
        self.lower, self.upper = map(float, cal['arrival_prior_seconds'])
        if not math.isfinite(self.lower+self.upper) or self.lower > self.upper:
            raise ValueError('finite ordered arrival prior required')
        self.delay = _number(ch['spread_delay_ms'], 'ray delay')*.001
        self.doppler = _number(ch['spread_doppler_hz'], 'ray Doppler')
        if self.delay < 0:
            raise ValueError('ray delay must be nonnegative')
        self.rays = ((0., 0.), (self.delay, self.doppler)) if self.delay or self.doppler else ((0., 0.),)
        self.pass_edge = _number(pass_edge_hz, 'pass edge', positive=True)
        self.taps = data_fir_taps('rate-aware', sample_rate=self.fs,
            output_sample_rate=self.output_fs, pass_edge_hz=self.pass_edge,
            stop_edge_hz=stop_edge_hz).astype(self.real_dtype)
        self.reference_variance = _number(manifest['power']['complex_noise_variance'],
            'raw noise variance', positive=True)*float(np.dot(self.taps.astype(float), self.taps.astype(float)))
        self.source_half_width = _number(manifest['format']['occupied_bandwidth_hz'],
            'nominal DATA bandwidth', positive=True)*.5
        self.duration = source_samples/self.fs
        self.maximum_frequency_step = 1/(4*self.duration)

        # Common basis origin aligned to the arrival lattice.  Cubic zero
        # extension can add two input samples on either side of the source.
        scales = np.asarray(self.clocks)*1e-6+1
        earliest = min((self.offset-2/self.fs)/scales)
        self.origin = (math.floor((earliest+self.lead+self.lower)*self.output_fs)/
                       self.output_fs-self.lead-self.lower)
        latest = max((self.offset+(source_samples+2)/self.fs)/scales)+self.delay
        self.raw_kernel_rows = math.ceil((latest-self.origin)*self.fs)+1
        self.kernel_rows = (self.raw_kernel_rows+len(self.taps)-2)//self.decimation+1
        self.lag_count = math.floor((self.upper-self.lower)*self.output_fs+1e-9)+1
        self.arrivals = self.lower+np.arange(self.lag_count)/self.output_fs
        self.crop_first = round((self.origin+self.lower+self.lead)*self.output_fs)
        self.crop_rows = self.kernel_rows+self.lag_count-1
        self.crop_stop = self.crop_first+self.crop_rows
        raw_support_last = ((self.origin+self.arrivals[-1]+self.lead)*self.fs+
                            self.raw_kernel_rows-1)
        if self.crop_first < 0 or raw_support_last > self.samples-1+1e-6:
            raise ValueError('complete source support does not fit every declared arrival/clock in capture')
        # Only complete-support lags 0..L-M are requested.  For these lags,
        # m+lag <= L-1 for every nonzero template index m < M, so a circular
        # correlation with NFFT >= L cannot wrap.  Padding to L+M-1 is needed
        # for all linear-correlation lags, not this valid-only subset.
        self.nfft = fft.next_fast_len(self.crop_rows)
        workspace_count = len(self.epochs)*len(self.clocks)*2
        self.resident_estimate = (workspace_count*len(self.rays)*(self.nfft+self.kernel_rows)*
                                  np.dtype(self.dtype).itemsize + source_bytes)
        batch_lanes = projection_workers = 1
        integer_lease = cubic_projector = None
        if template_provider is None and mapped_template_provider is None:
            scratch = 4*self.raw_kernel_rows*16+4*self.nfft*np.dtype(self.dtype).itemsize
        elif mapped_template_provider is not None:
            coarse_rows = math.ceil((self.raw_kernel_rows-1)/self.template_sampling_step)+1
            staging = 8*min(coarse_rows, template_chunk_samples)*16+4*self.nfft*np.dtype(self.dtype).itemsize
            projection_scratch = (8*self.kernel_rows*16 +
                16*(len(self.taps)+4*self.template_sampling_step)*16)
            # One coarse column per lane and the simultaneously live native
            # response, decoded values, finite extension and phase temporaries.
            lane_scratch = (coarse_rows+8*min(coarse_rows, template_chunk_samples)+8)*16
            lane_limit = min(2*len(self.epochs),
                _integer(mapped_template_batch_lanes, 'mapped template batch lanes'))
            available = self.memory_limit_bytes-self.resident_estimate-staging
            for projection_workers in range(min(self.workers, lane_limit), 0, -1):
                batch_lanes = min(lane_limit,
                    (available-projection_workers*projection_scratch)//lane_scratch)
                if batch_lanes >= projection_workers:
                    break
            else:
                raise MemoryError('coarse mapped bank has no workspace for one public lane')
            scratch = staging+batch_lanes*lane_scratch+projection_workers*projection_scratch
        else:
            transform_rows = fft.next_fast_len(template_chunk_samples + len(self.taps) - 1)
            provider_rows = min(source_samples, math.ceil(template_chunk_samples*float(max(scales))) + 4)
            scratch = (8*transform_rows*np.dtype(self.dtype).itemsize + 4*provider_rows*16 +
                       4*self.nfft*np.dtype(self.dtype).itemsize)
            lease = getattr(template_provider, 'lease', None)
            if callable(lease):
                jobs_per_lane = len(self.clocks)*len(self.rays)
                lane_limit = min(2*len(self.epochs),
                    _integer(mapped_template_batch_lanes, 'native template batch lanes'))
                # Immutable raw rows, native pipe responses and decoding,
                # batched postprocessing, and concurrent filter workers.
                # Projected destinations already belong to the resident bank.
                per_lane = (source_samples*16 + 4*min(source_samples, template_chunk_samples)*16 +
                            4*len(self.rays)*self.nfft*16)
                available = self.memory_limit_bytes-self.resident_estimate
                requested = 1 if self.device_parallel else self.workers
                for count in range(min(requested, lane_limit*jobs_per_lane), 0, -1):
                    admitted = min(lane_limit, (available-count*scratch)//per_lane)
                    if admitted > 0 and admitted*jobs_per_lane >= count:
                        batch_lanes, projection_workers, integer_lease = admitted, count, lease
                        scratch = batch_lanes*per_lane + projection_workers*scratch
                        break
                load_projector = getattr(template_provider, 'cubic_plan', None)
                if integer_lease is not None and not self.device_parallel and callable(load_projector):
                    tile_count = (self.raw_kernel_rows+template_chunk_samples-1)//template_chunk_samples
                    # Int64 centers, four FP64 weights and complex128 rotations,
                    # plus one setup tile's temporaries and plan metadata.
                    plan_bytes = (jobs_per_lane*(56*self.raw_kernel_rows+1024*tile_count) +
                                  128*template_chunk_samples)
                    if scratch+plan_bytes <= available:
                        cubic_projector = load_projector()
                        if cubic_projector is not None:
                            scratch += plan_bytes
        self.preparation_peak_estimate_bytes = self.resident_estimate+scratch
        if self.preparation_peak_estimate_bytes > self.memory_limit_bytes:
            raise MemoryError('coarse FFT template preparation exceeds memory limit')
        self.workspaces = [None]*workspace_count
        time_source = (self.origin+np.arange(self.raw_kernel_rows, dtype=float)/self.fs
                       if template_provider is None and mapped_template_provider is None else None)
        collapsed = (_collapsed_cubic_fir(self.taps, self.template_sampling_step, self.decimation)
                     if mapped_template_provider is not None else None)
        lanes = tuple((epoch, bit) for epoch in self.epochs for bit in (0, 1))
        cubic_plans = None if cubic_projector is None else self._integer_cubic_plans(scales)
        pool = self._projection_pool(projection_workers)
        with pool as projection:
            for lane_first in range(0, len(lanes), batch_lanes):
                batch = lanes[lane_first:lane_first+batch_lanes]
                if template_provider is not None:
                    columns = self._integer_bank_batch(batch, scales, template_provider,
                                                       integer_lease, projection, cubic_projector, cubic_plans)
                    for ci in range(len(scales)):
                        self._retain_bank_columns(batch, lane_first, ci, columns[ci])
                    del columns
                    continue
                mapped_edges = []
                for ci, scale in enumerate(scales):
                    bank_columns = self._allocate_bank_columns((len(batch), len(self.rays), self.kernel_rows))
                    for ray, (delay, doppler) in enumerate(self.rays):
                        if mapped_template_provider is not None:
                            step = self.template_sampling_step
                            coarse = np.zeros((len(batch), coarse_rows), np.complex128)
                            for first in range(0, coarse_rows, template_chunk_samples):
                                stop = min(coarse_rows, first+template_chunk_samples)
                                raw_indices = np.arange(first, stop)*step
                                times = self.origin+raw_indices/self.fs
                                positions = (((times-delay)*scale)-self.offset)*self.fs
                                keep = raw_indices < self.raw_kernel_rows
                                if keep.any():
                                    supplied = _mapped_finite_source(mapped_template_provider, batch,
                                        positions[keep], source_samples, mapped_edges)
                                    coarse[:, first:stop][:, keep] = supplied*np.exp(
                                        2j*np.pi*np.remainder(doppler*times[keep], 1.))
                            self._project_mapped_bank(bank_columns[:, ray], coarse, collapsed, projection)
                            del coarse
                        else:
                            _, bit = batch[0]
                            ei = lane_first//2
                            position = (((time_source-delay)*scale)-self.offset)*self.fs
                            values = _sample_cubic(source[ei, bit], position)
                            values *= np.exp(2j*np.pi*np.remainder(doppler*time_source, 1.))
                            column = self._filter_observation(values).astype(self.dtype)
                            self._write_bank_column(bank_columns[0, ray], column)
                    self._retain_bank_columns(batch, lane_first, ci, bank_columns)
        self.policy = dict(schema='triad-realtime-fft-policy-v1', precision=precision,
            output_sample_rate=self.output_fs, pass_edge_hz=self.pass_edge,
            stop_edge_hz=float(stop_edge_hz), epochs=self.epochs, clocks=self.clocks,
            original_arrival_prior=[self.lower, self.upper], arrival_step_seconds=1/self.output_fs,
            arrival_end_gap_seconds=self.upper-float(self.arrivals[-1]),
            maximum_frequency_step_hz=self.maximum_frequency_step,
            statistic='static complex matched-subspace GLRT; proposal only',
            score_arithmetic='FP64 explicit Hermitian2-ray form; original contraction if one-ray, rank-deficient or condition>1e6',
            filter_model='causal full-tail FIR, zero-CFO templates, fixed observed coarse projection',
            bounded_fir_chunk_samples=template_chunk_samples,
            maximum_frequency_points=self.maximum_frequency_points,
            maximum_fft_products=self.maximum_fft_products,
            correlation_method=self.correlation_method)
        if self._uses_streamed_observation():
            self.policy['observation_projection'] = 'bounded_cpu_fft_overlap_add'
        if template_provider is not None:
            self.policy.update(template_storage=('bounded exact integer-source lease; identical cubic/warp/FIR operator'
                if integer_lease is not None else 'bounded integer-source callback; identical cubic/warp/FIR operator'),
                integer_template_lease_lanes=batch_lanes if integer_lease is not None else 0,
                integer_template_projection='native_cubic_plan' if cubic_projector is not None else 'numpy_cubic',
                preparation_projection_workers=projection_workers,
                template_chunk_samples=template_chunk_samples, template_provider_identity=source_identity)
        if mapped_template_provider is not None:
            self.policy.update(template_storage='bounded mapped native samples; coarse cubic reconstruction plus exact original FIR polyphase composition',
                template_sampling_step=self.template_sampling_step,
                template_batch_lanes=batch_lanes,
                preparation_projection_workers=projection_workers,
                template_provider_identity=source_identity,
                approximation_scope='Template interpolation only; requires matched native fidelity checks for this source/configuration. Observation FIR/noise/effects are unchanged.')
        self.procedure_id = _digest(dict(policy=self.policy,
            source_sha256={name: hashlib.sha256((Path(__file__).parent/name).read_bytes()).hexdigest()
                for name in ('triad_realtime_fft.py', 'triad_dynamic_joint.py', 'triad_acquisition.py')},
            template_sha256=source_identity,
            geometry=dict(samples=self.samples, fs=self.fs, source_offset=self.offset,
                          lead=self.lead, delay=self.delay, doppler=self.doppler,
                          projected_noise_variance=self.reference_variance)))
        self.preparation_timing = _elapsed(started)

    def _projection_pool(self, workers):
        return (ThreadPoolExecutor(max_workers=workers, thread_name_prefix='triad-bank')
                if workers > 1 else nullcontext())

    def _allocate_bank_columns(self, shape):
        return np.empty(shape, dtype=self.dtype)

    def _write_bank_column(self, destination, column):
        destination[...] = column

    def _integer_cubic_plans(self, scales):
        plans = {}
        for ci, scale in enumerate(scales):
            for ri, (delay, doppler) in enumerate(self.rays):
                tiles = []
                for times, positions in self._integer_geometry(scale, delay):
                    rotation = np.exp(2j*np.pi*np.remainder(doppler*times, 1.))
                    first, stop = _cubic_source_bounds(positions, self.source_samples)
                    if stop <= first:
                        first = stop = 0
                        positions = np.zeros_like(positions)
                    center, weights = _cubic_stencil(positions-first)
                    weights = np.asarray(weights)
                    for array in (center, weights, rotation):
                        array.flags.writeable = False
                    tiles.append((first, stop, center, weights, rotation))
                plans[ci, ri] = tuple(tiles)
        return plans

    def _integer_geometry(self, scale, delay):
        chunk = self.template_chunk_samples
        for first in range(0, self.raw_kernel_rows, chunk):
            stop = min(self.raw_kernel_rows, first+chunk)
            times = self.origin + np.arange(first, stop, dtype=float)/self.fs
            positions = (((times-delay)*scale)-self.offset)*self.fs
            yield times, positions

    def _integer_bank_batch(self, batch, scales, provider, lease, projection, cubic_projector, cubic_plans):
        if lease is not None:
            samples = np.asarray(lease(batch, self.source_samples, self.template_chunk_samples))
            if (samples.shape != (len(batch), self.source_samples) or samples.dtype != np.complex128 or
                    samples.flags.writeable or not samples.flags.c_contiguous or
                    not samples.flags.aligned or not np.isfinite(samples).all()):
                raise ValueError('integer source lease requires finite aligned read-only contiguous complex128 rows')
            index = {lane: i for i, lane in enumerate(batch)}

            def provider(epoch, bit, first, count):
                return samples[index[epoch, bit], first:first+count]

        columns = self._allocate_bank_columns((len(scales), len(batch), len(self.rays), self.kernel_rows))
        jobs = ((ci, bi, ri) for ci in range(len(scales)) for bi in range(len(batch))
                for ri in range(len(self.rays)))

        def project(job):
            ci, bi, ri = job
            epoch, bit = batch[bi]
            scale = scales[ci]
            delay, doppler = self.rays[ri]

            def chunks():
                if cubic_projector is not None:
                    for first, stop, center, weights, rotation in cubic_plans[ci, ri]:
                        yield cubic_projector(samples[bi, first:stop], center, weights, rotation)
                else:
                    for times, positions in self._integer_geometry(scale, delay):
                        values = _provider_cubic(provider, epoch, bit, positions, self.source_samples)
                        yield values * np.exp(2j*np.pi*np.remainder(doppler*times, 1.))

            with fft.set_workers(1):
                self._write_bank_column(columns[ci, bi, ri], self._project_integer_column(chunks()))

        if projection is None:
            for job in jobs:
                project(job)
        else:
            # The controller completed the native lease before any task.
            # Workers read immutable samples and write disjoint columns.
            # The enclosing pool joins outstanding work even on failure.
            for _ in projection.map(project, jobs):
                pass
        return columns

    def _retain_bank_columns(self, batch, lane_first, ci, bank_columns):
        for bi, ((epoch, bit), (columns, gram, transformed)) in enumerate(
                zip(batch, self._bank_arrays(bank_columns), strict=True)):
            eigenvalues, eigenvectors = np.linalg.eigh((gram+gram.conj().T)*.5)
            keep = eigenvalues > max(float(eigenvalues[-1]), 0.)*1e-10
            if not keep.any():
                raise ValueError('coarse pattern has no observable energy')
            inverse = (eigenvectors[:, keep]/eigenvalues[keep]) @ eigenvectors[:, keep].conj().T
            lane = lane_first+bi
            index = (lane//2*len(self.clocks)+ci)*2+bit
            self.workspaces[index] = dict(epoch=epoch, clock_ppm=self.clocks[ci], bit=bit,
                columns=columns, transform=transformed, gram=gram, inverse=inverse,
                rank=int(keep.sum()), quadratic_explicit=bool(len(self.rays) == 2 and keep.all() and
                    eigenvalues[-1]/eigenvalues[0] <= 1e6))

    def _project_integer_column(self, chunks):
        return _fir_decimate_chunks(chunks, self.taps, self.decimation,
            self.raw_kernel_rows, self.dtype, self.template_chunk_samples)

    def _project_mapped_bank(self, destination, coarse, collapsed, projection):
        def project(index):
            destination[index] = _project_coarse_column(coarse[index], self.raw_kernel_rows,
                self.taps, self.template_sampling_step, self.decimation, *collapsed, self.dtype)
        if projection is None:
            for index in range(len(coarse)):
                project(index)
        else:
            # Join every disjoint lane before the next native pipe request.
            for _ in projection.map(project, range(len(coarse))):
                pass

    def _bank_arrays(self, bank_columns):
        for columns in bank_columns:
            gram = columns.astype(np.complex128).conj() @ columns.astype(np.complex128).T
            transformed = fft.fft(columns, n=self.nfft, axis=-1, workers=self.workers).conj()
            yield columns, gram, transformed

    def _frequencies(self, frequencies_hz, frequency_intervals, *, maximum_points=None):
        maximum_points = (self.maximum_frequency_points if maximum_points is None else
                          _integer(maximum_points, 'maximum complete frequency points'))
        if (frequencies_hz is None) == (frequency_intervals is None):
            raise ValueError('supply exactly one frequency bank or interval union')
        if frequency_intervals is not None:
            pieces = []
            for interval in frequency_intervals:
                if len(interval) != 2:
                    raise ValueError('each frequency interval needs two endpoints')
                lo, hi = (_number(x, 'frequency interval') for x in interval)
                if lo > hi:
                    raise ValueError('frequency interval must be ordered')
                count = math.ceil((hi-lo)/self.maximum_frequency_step)+1
                if count+sum(len(x) for x in pieces) > maximum_points:
                    return None, 'frequency_work_limit'
                pieces.append(np.linspace(lo, hi, count))
            if not pieces:
                raise ValueError('frequency interval union must be nonempty')
            bank = np.unique(np.concatenate(pieces))
        else:
            bank = np.asarray(tuple(frequencies_hz), dtype=float)
            if bank.ndim != 1 or not len(bank) or not np.isfinite(bank).all() or len(np.unique(bank)) != len(bank):
                raise ValueError('frequency bank must be finite, nonempty and unique')
            bank = np.sort(bank)
            if len(bank) > 1 and np.diff(bank).max() > self.maximum_frequency_step*(1+1e-10):
                return None, 'explicit_frequency_grid_too_coarse'
        if len(bank) > maximum_points:
            return None, 'frequency_work_limit'
        if max(abs(bank))+self.source_half_width+abs(self.doppler) > self.pass_edge:
            return None, 'frequency_bank_outside_nominal_passband'
        return bank, None

    def _frequency_spectra(self, observation, frequencies, *, workers=None):
        spectra = np.empty((len(frequencies), self.nfft), dtype=self.dtype)
        times = np.arange(len(observation), dtype=float)/self.output_fs
        for index, frequency in enumerate(frequencies):
            rotation = np.exp(-2j*np.pi*np.remainder(frequency*times, 1.)).astype(self.dtype)
            spectra[index] = fft.fft(observation*rotation, n=self.nfft,
                workers=self.workers if workers is None else workers)
        return spectra

    def enable_native_coarse(self, exporter):
        """Use only the current native exporter's colocated, versioned kernel."""
        from triad_coarse_kernel import CoarseRank2Kernel

        try:
            self.coarse_kernel = CoarseRank2Kernel(exporter)
        except (OSError, AttributeError):
            self.coarse_kernel = None
            receipt = dict(status='numpy_scipy_fallback',
                reason='native_rank2_kernel_unavailable_or_version_mismatch')
        else:
            receipt = dict(status='native_rank2_with_canonical_fallback',
                arithmetic='FP64 ordered quadratic form; configured FFT precision unchanged',
                fallback='Guarded/rank-deficient Grams, other ray counts, nonfinite or tied peak frontiers, and K>2 retain canonical arithmetic and selection.')
        if self.policy.get('coarse_score_kernel') == receipt:
            return
        if not hasattr(self, '_coarse_kernel_procedure_base'):
            self._coarse_kernel_procedure_base = self.procedure_id
        self.policy = dict(self.policy, coarse_score_kernel=receipt)
        self.procedure_id = _digest(dict(original=self._coarse_kernel_procedure_base,
            coarse_score_kernel=receipt))

    def _correlation_peak_rows(self, local, workspace, correlations, frequencies,
                               *, sample_separation, top_per_bit):
        kernel = self.coarse_kernel
        if (kernel is None or not workspace['quadratic_explicit'] or
                len(self.rays) != 2 or top_per_bit > 2):
            scores = _quadratic_scores(correlations, workspace['inverse'],
                explicit=workspace['quadratic_explicit'])/self.reference_variance
            _append_candidate_rows(local, workspace, frequencies, scores,
                self.arrivals, sample_separation, top_per_bit)
            return
        _, indices, counts = kernel(correlations, workspace['inverse'],
            self.reference_variance, sample_separation, top_per_bit)
        # Preserve canonical scores for global cross-CFO/workspace sorting too.
        # Native search only needs an approximation safely outside its local
        # comparison error envelope; publishing those values could split a tie
        # between rows. Recompute just the at-most-two selected coordinates with
        # the same NumPy arithmetic, including its platform SIMD contraction.
        gather = np.where(np.arange(2)[None, :] < counts[:, None], indices, 0)
        selected_correlations = np.take_along_axis(correlations, gather[:, None, :], axis=-1)
        selected_scores = _quadratic_scores(selected_correlations, workspace['inverse'],
            explicit=True)/self.reference_variance
        for i, frequency in enumerate(frequencies):
            if counts[i] < 0:
                # Recompute this exceptional row in canonical arithmetic too;
                # a numerical tie must not inherit a new rounding priority.
                reference = _quadratic_scores(correlations[i:i+1], workspace['inverse'],
                    explicit=True)[0]/self.reference_variance
                selected = _candidate_peak_indices(reference, sample_separation, top_per_bit)
                values = reference[selected]
            else:
                selected = indices[i, :counts[i]]
                values = selected_scores[i, :counts[i]]
            for lag, value in zip(selected, values, strict=True):
                local.append(_candidate_row(workspace, frequency,
                    self.arrivals[lag], value))

    def _frequency_peak_rows(self, workspace, spectra, frequencies, *,
                             sample_separation, top_per_bit, workers=None):
        """Unpruned per-CFO peaks; a tile boundary cannot change workspace NMS."""
        local = []
        for first in range(0, len(frequencies), self.batch_size):
            stop = min(first+self.batch_size, len(frequencies))
            products = spectra[first:stop, None, :]*workspace['transform'][None]
            correlations = fft.ifft(products, axis=-1,
                workers=self.workers if workers is None else workers)[..., :self.lag_count]
            self._correlation_peak_rows(local, workspace, correlations, frequencies[first:stop],
                sample_separation=sample_separation, top_per_bit=top_per_bit)
        return local

    def _frequency_workspace_rows(self, spectra, frequencies, *, sample_separation, top_per_bit):
        for workspace in self.workspaces:
            yield self._frequency_peak_rows(workspace, spectra, frequencies,
                sample_separation=sample_separation, top_per_bit=top_per_bit)

    def _retained_candidates(self, rows, *, top_per_bit, separation):
        retained = [[], []]
        for workspace, local in zip(self.workspaces, rows, strict=True):
            retained[workspace['bit']].extend(_select_workspace_candidates(local, top_per_bit,
                separation, 1/(2*self.duration)))
        return _select_bit_candidates(retained, top_per_bit)

    def tiled_frequency_plan(self, frequencies_hz=None, *, frequency_intervals=None,
                             top_per_bit=4, minimum_arrival_separation_seconds=None,
                             frequency_unit_points=4, maximum_total_frequency_points=65536,
                             workspace_workers=1):
        """Plan an exact full grid as independently bounded frequency kernels.

        The ordinary point/product ceilings still apply to EVERY kernel. The
        separate explicit full-grid ceiling and global raw-peak storage budget
        prevent an unbounded accumulated search. No partial tile is a proposal.
        """
        top_per_bit = _integer(top_per_bit, 'top candidates per bit')
        if top_per_bit > 64:
            raise ValueError('top candidates per bit exceeds64 proposal budget')
        unit = _integer(frequency_unit_points, 'frequency unit points')
        maximum = _integer(maximum_total_frequency_points, 'maximum complete frequency points')
        if maximum > 65536:
            raise ValueError('complete frequency grid exceeds65536-point local bound')
        workers = min(_integer(workspace_workers, 'workspace workers'), len(self.workspaces))
        separation = (.5/float(self.manifest['format'].get('fine_chip_rate_hz', 60.))
            if minimum_arrival_separation_seconds is None else
            _number(minimum_arrival_separation_seconds, 'candidate separation', positive=True))
        if frequency_intervals is None and frequencies_hz is not None:
            frequencies_hz = tuple(frequencies_hz)
        elif frequency_intervals is not None:
            frequency_intervals = tuple(tuple(interval) for interval in frequency_intervals)
        frequencies, failure = self._frequencies(frequencies_hz, frequency_intervals,
            maximum_points=maximum)
        result = dict(schema='triad-tiled-frequency-plan-v1', status='unresolved', reason=failure,
            frequency_unit_points=unit, maximum_total_frequency_points=maximum,
            workspace_workers=workers, top_per_bit=top_per_bit,
            minimum_arrival_separation_seconds=separation,
            per_kernel_maximum_frequency_points=self.maximum_frequency_points,
            per_kernel_maximum_fft_products=self.maximum_fft_products)
        if failure:
            return result
        size = min(unit, len(frequencies))
        products = size*len(self.workspaces)*len(self.rays)
        if size > self.maximum_frequency_points or products > self.maximum_fft_products:
            result['reason'] = 'frequency_tile_kernel_work_limit'
            return result
        sample_separation = max(1, math.ceil(separation*self.output_fs))
        peak_rows = min(top_per_bit, self.lag_count,
            math.ceil(self.lag_count/sample_separation)+2)
        candidate_rows = len(frequencies)*len(self.workspaces)*peak_rows
        candidate_bytes = candidate_rows*512
        item = np.dtype(self.dtype).itemsize
        active = min(size, self.batch_size)
        scratch = workers*(active*len(self.rays)*self.nfft*item*3 +
            active*self.lag_count*64)
        spectra_bytes = size*self.nfft*item
        # Includes the owned observation copy, times/modulation/FIR-related
        # coordinates, and simultaneous global sort/key buffers in the512-byte
        # per-row allowance. The caller must reserve its other input buffers.
        peak = (self.resident_estimate + 96*self.crop_rows +
            64*len(frequencies) + spectra_bytes + scratch + candidate_bytes)
        units = math.ceil(len(frequencies)/unit)
        result.update(frequencies_hz=frequencies.tolist(), frequency_points=len(frequencies),
            units=units, terminal_unit_frequency_points=len(frequencies)-(units-1)*unit,
            full_fft_products=len(frequencies)*len(self.workspaces)*len(self.rays),
            full_observation_ffts=len(frequencies), maximum_kernel_fft_products=products,
            raw_candidate_row_capacity=candidate_rows,
            raw_candidate_storage_estimate_bytes=candidate_bytes,
            parallel_kernel_scratch_estimate_bytes=scratch,
            estimated_peak_bytes=peak, template_workspaces=len(self.workspaces),
            arrival_points=self.lag_count, rays=len(self.rays), fft_length=self.nfft,
            full_correlation_work_butterfly_estimate=(len(frequencies)+
                len(frequencies)*len(self.workspaces)*len(self.rays))*self.nfft*math.log2(self.nfft),
            storage_scope='All per-workspace per-CFO peaks remain live until canonical global selection.512 bytes per peak includes dictionary/list/sort allowance; caller input/native buffers remain separate.',
            exactness_scope='Same canonical complete CFO lattice, per-CFO arrival peaks, stable workspace NMS and global per-bit selection. No tile-local across-frequency pruning.')
        if peak > self.memory_limit_bytes:
            result['reason'] = 'complete_tiled_frequency_memory_limit'
        else:
            result.update(status='ready', reason=None)
        return result

    def start_tiled_frequency(self, projected, frequencies_hz=None, *,
                              projection_receipt=None, executor=None, **arguments):
        """Create a resumable exact frequency search on one prepared input."""
        if projection_receipt is not None and projection_receipt != self.projection_receipt():
            raise ValueError('prepared projection receipt does not match this finite operator/source')
        return TiledFrequencySession(self, projected, frequencies_hz,
            executor=executor, **arguments)

    def _lag_frequency_groups(self, frequencies_hz, frequency_intervals):
        """Return the exact uniform grids supplied by the caller, if available.

        A union of independently sampled intervals is not itself one ZoomFFT
        grid. Keep its pieces separate, including any repeated endpoints, so
        the lag-domain implementation evaluates exactly the same CFO values.
        """
        if frequency_intervals is not None:
            groups = []
            for interval in frequency_intervals:
                lo, hi = map(float, interval)
                count = math.ceil((hi-lo)/self.maximum_frequency_step)+1
                groups.append(np.linspace(lo, hi, count))
            return groups
        values = np.sort(np.asarray(tuple(frequencies_hz), dtype=float))
        if len(values) == 1:
            return [values]
        steps = np.diff(values)
        step = float(steps[0])
        tolerance = 32*np.finfo(float).eps*max(1., float(np.max(np.abs(values))), abs(step))
        if step <= 0 or np.max(np.abs(steps-step)) > tolerance:
            return None
        return [values]

    def _correlation_plan(self, frequencies, groups, *, raw_input, retained_input_bytes=0, top_per_bit=4,
                          separation=None):
        """Choose an admitted implementation from bounded work and storage.

        Work estimates count radix-2-equivalent FFT butterflies. ZoomFFT uses
        Bluestein convolution: one cached kernel transform, then two transforms
        per input row at the padded convolution length. They are estimates, not
        measured seconds; both methods retain an independent product ceiling.
        """
        nf, nw, nr = len(frequencies), len(self.workspaces), len(self.rays)
        legacy_products = nf*nw*nr
        legacy_work = (nf + legacy_products)*self.nfft*math.log2(self.nfft)
        candidates = []
        if legacy_products <= self.maximum_fft_products:
            candidates.append(('frequency', legacy_products, legacy_work))

        zoom_groups = []
        zoom_products = 0
        zoom_fft_lanes = 0
        zoom_work = 0.
        zoom_lengths = []
        if groups is not None:
            for values in groups:
                if len(values) == 1:
                    # Singleton grids use the exact direct correlation below.
                    zoom_groups.append((values, 0, 0))
                    zoom_products += 2*nw*nr*self.lag_count
                    zoom_work += nw*nr*self.lag_count*self.kernel_rows
                    continue
                length = fft.next_fast_len(self.kernel_rows + len(values) - 1)
                zoom_lengths.append(length)
                zoom_groups.append((values, length, float(values[1]-values[0])))
                zoom_products += 2*nw*nr*self.lag_count+1
                zoom_fft_lanes += 2*nw*nr*self.lag_count+1
                zoom_work += length*math.log2(length)  # one cached Bluestein kernel
                zoom_work += 2*nw*nr*self.lag_count*length*math.log2(length)
            if zoom_products <= self.maximum_fft_products:
                candidates.append(('lag', zoom_products, zoom_work))
        selected = None
        admitted = []
        candidate_peaks = {}
        sample_separation = max(1, math.ceil((
            .5/float(self.manifest['format'].get('fine_chip_rate_hz', 60.))
            if separation is None else separation)*self.output_fs))
        for candidate in candidates:
            peak = self._correlation_peak_estimate(dict(selected=candidate,
                frequencies=frequencies, groups=zoom_groups,
                top_per_bit=top_per_bit, sample_separation=sample_separation),
                raw_input=raw_input)+retained_input_bytes
            candidate_peaks[candidate[0]] = peak
            if peak <= self.memory_limit_bytes:
                admitted.append(candidate)
        if self.correlation_method == 'frequency':
            selected = next((x for x in admitted if x[0] == 'frequency'), None)
        elif self.correlation_method == 'lag':
            selected = next((x for x in admitted if x[0] == 'lag'), None)
        else:
            selected = min(admitted, key=lambda x:x[2]) if admitted else None
        return dict(selected=selected, legacy_products=legacy_products,
            legacy_work_butterfly_estimate=legacy_work,
            lag_products=zoom_products, lag_work_butterfly_estimate=zoom_work,
            lag_fft_lengths=zoom_lengths, groups=zoom_groups,
            frequency_group_count=0 if groups is None else len(groups),
            lag_fft_transform_lanes=zoom_fft_lanes,
            available_methods=[x[0] for x in candidates],
            estimated_peak_bytes_by_method=candidate_peaks,
            work_scope='Radix-2-equivalent N log2(N) estimate. Lag method includes cached per-group Bluestein kernel FFTs and two padded FFTs per ray/workspace/arrival row. Singleton grids execute direct dot products and are conservatively charged two product-budget units per row, not two actual FFTs. Frequency mode preserves its existing correlation-product cap and separately reports observation FFTs. Does not include Python peak selection, pointwise products, or FIR filtering.')

    def _correlation_peak_estimate(self, plan, *, raw_input):
        """Conservative live-buffer estimate before any observation allocation."""
        item = np.dtype(self.dtype).itemsize
        base = self.resident_estimate+96*self.crop_rows
        if raw_input:
            base = max(base, self._observation_peak_estimate())
        if plan['selected'] is None:
            return None
        method = plan['selected'][0]
        # Each active workspace builds its own per-CFO peak list while the
        # already retained epoch/clock alternatives remain live. The actual
        # caller separation and top cap, including overrides, bound this list.
        peak_rows = min(plan['top_per_bit'], self.lag_count,
            math.ceil(self.lag_count/plan['sample_separation'])+2)
        candidates = (len(plan['frequencies'])*peak_rows +
            len(self.workspaces)*plan['top_per_bit'])*512
        if method == 'frequency':
            return (base + len(plan.get('frequencies', ())) * self.nfft * item +
                self.batch_size * len(self.rays) * self.nfft * item * 3 + candidates)
        groups = plan['groups']
        count = sum(len(group[0]) for group in groups)
        transformed = sum(group[1] for group in groups)
        largest = max((group[1] for group in groups), default=0)
        lag_batch = min(self.lag_count, self.batch_size)
        # ZoomFFT's complex chirps and outputs use complex128 even for an
        # fp32 template bank, so model these work arrays at their actual width.
        zoom_item = np.dtype(np.complex128).itemsize
        correlations = count*len(self.rays)*self.lag_count*zoom_item
        scores = count*self.lag_count*np.dtype(np.float64).itemsize
        quadratic_scratch = count*self.lag_count*64
        phases = count*self.lag_count*zoom_item
        gather = lag_batch*self.kernel_rows*zoom_item
        zoom_scratch = lag_batch*largest*zoom_item*4 if largest else 0
        zoom_plans = transformed*zoom_item*10
        return base+correlations+scores+quadratic_scratch+phases+gather+zoom_scratch+zoom_plans+candidates

    def _uses_streamed_observation(self):
        return not self.device_parallel and self.samples >= _STREAM_OBSERVATION_SAMPLES

    def _streamed_observation_peak_estimate(self):
        output_rows = (self.samples + len(self.taps) - 2)//self.decimation + 1
        transform_rows = fft.next_fast_len(self.template_chunk_samples + len(self.taps) - 1)
        # Complete decimated tail, cropped result, FFT buffers, and bounded
        # per-chunk conversion and validity scratch. The caller owns raw input.
        return (self.resident_estimate +
            (2*output_rows + 8*transform_rows)*np.dtype(self.dtype).itemsize +
            64*self.template_chunk_samples)

    def _observation_peak_estimate(self):
        if self._uses_streamed_observation():
            return self._streamed_observation_peak_estimate()
        return self.resident_estimate+64*self.samples+32*self.crop_rows

    @staticmethod
    def _check_observation_validity(valid):
        if not np.isin(valid, (0, 1)).all():
            raise ValueError('binary capture validity required')
        if not valid.all():
            raise ValueError('coarse shift-invariant Gram screen does not support erasures')

    def prepare_observation(self, raw, valid=None):
        """Filter the complete capture once, retaining the full finite FIR tail."""
        raw = np.asarray(raw)
        streamed = self._uses_streamed_observation()
        if (raw.shape != (self.samples,) or not np.iscomplexobj(raw) or
                (not streamed and not np.isfinite(raw).all())):
            raise ValueError('finite complex complete capture required')
        if valid is not None:
            valid = np.asarray(valid)
            if valid.shape != raw.shape:
                raise ValueError('binary capture validity required')
        if streamed:
            def chunks():
                unit = self.template_chunk_samples
                for first in range(0, self.samples, unit):
                    stop = min(self.samples, first+unit)
                    if valid is not None:
                        self._check_observation_validity(valid[first:stop])
                    yield raw[first:stop]
            return self.prepare_observation_stream(chunks())
        if valid is not None:
            self._check_observation_validity(valid)
        filtered = self._filter_observation(raw)
        if self.crop_stop > len(filtered):
            raise ValueError('complete filtered support outside capture')
        return filtered[self.crop_first:self.crop_stop].copy()

    def _filter_observation(self, raw):
        return signal.oaconvolve(raw.astype(self.dtype), self.taps, mode='full')[::self.decimation]

    def _coerce_projected(self, projected, *, copy=False):
        observation = np.asarray(projected)
        if (observation.shape != (self.crop_rows,) or not np.iscomplexobj(observation) or
                not np.isfinite(observation).all()):
            raise ValueError('finite prepared projection must match this screen crop')
        if copy:
            observation = np.array(observation, copy=True)
            self._mark_readonly(observation)
        return observation

    def _mark_readonly(self, values):
        values.flags.writeable = False

    def projection_receipt(self):
        """Exact finite complex-FIR coordinates; taps remain available as .taps.

        Cropped output row i uses raw convolution coordinate
        ``(cropped_output_start + i)*downsample``. Input outside the declared
        finite raw observation is numeric zero, never another noise sample.
        The receipt does not assert whitened or independent output noise.
        """
        taps = np.asarray(self.taps, dtype='<f4' if self.precision == 'fp32' else '<f8')
        return dict(schema='triad-complex-fir-projection-v1',
            raw_samples=self.samples, input_sample_rate_hz=self.fs,
            output_sample_rate_hz=self.output_fs, downsample=self.decimation,
            fir_taps_count=len(taps), fir_taps_dtype=taps.dtype.str,
            fir_taps_sha256=hashlib.sha256(taps.tobytes()).hexdigest(),
            template_source_samples=self.source_samples,
            template_source_identity_sha256=self.template_source_identity,
            template_sampling_step_raw_samples=self.template_sampling_step,
            decimation_phase_raw_samples=0, cropped_output_start=self.crop_first,
            cropped_output_count=self.crop_rows,
            first_cropped_convolution_raw_sample=self.crop_first*self.decimation,
            complete_filtered_output_count=(self.samples+len(taps)-2)//self.decimation+1,
            capture_lead_seconds=self.lead,
            finite_input_boundary='Exactly zero outside raw sample indices0..raw_samples-1; complete causal FIR tail retained before crop.',
            noise_scope='Shared finite FIR of raw noise; generally colored, with finite-edge variances. No whitening or new likelihood qualification.')

    def prepare_observation_stream(self, chunks):
        """Same complex FIR projection from consecutive bounded input chunks.

        All declared raw samples must be present. This entry point preserves
        the existing screen's all-valid contract; callers must not replace
        erasures with numeric zeros or treat numeric filter tails as observed.
        """
        if self._streamed_observation_peak_estimate() > self.memory_limit_bytes:
            raise MemoryError('streamed observation FIR exceeds the declared memory budget')
        filtered = _fir_decimate_chunks(chunks, self.taps, self.decimation,
            self.samples, self.dtype, self.template_chunk_samples)
        return np.array(filtered[self.crop_first:self.crop_stop], dtype=self.dtype, copy=True)

    def score_projected(self, projected, frequencies_hz=None, *, projection_receipt=None, **arguments):
        """Score the exact cropped output of this screen's preparation operator.

        This is an implementation entry point, not a claim that an arbitrary
        downsampled signal has the matching filter/noise/source identity.
        """
        if projection_receipt is not None and projection_receipt != self.projection_receipt():
            raise ValueError('prepared projection receipt does not match this finite operator/source')
        return self.score(None, frequencies_hz, projected_observation=projected, **arguments)

    def _lag_plans(self, frequencies, groups, zoomfft):
        zoom_plans = []
        for values, transform_length, _step in groups:
            transform = (zoomfft(self.kernel_rows, (float(values[0]), float(values[-1])),
                m=len(values), fs=self.output_fs, endpoint=True) if len(values) > 1 else None)
            positions = np.searchsorted(frequencies, values)
            if not np.array_equal(frequencies[positions], values):
                raise RuntimeError('lag-domain CFO grid differs from the admitted public bank')
            zoom_plans.append((values, positions, transform, transform_length))
        return zoom_plans

    def _lag_workspace_rows(self, observation, frequencies, groups, *, sample_separation, top_per_bit):
        kernel_indices = np.arange(self.kernel_rows)
        local_times = kernel_indices/self.output_fs
        with fft.set_workers(self.workers):
            zoom_plans = self._lag_plans(frequencies, groups, signal.ZoomFFT)
        for workspace in self.workspaces:
            local = []
            correlation_bank = np.empty((len(frequencies), len(self.rays), self.lag_count), dtype=np.complex128)
            with fft.set_workers(self.workers):
                for values, positions, transform, _transform_length in zoom_plans:
                    for first_lag in range(0, self.lag_count, self.batch_size):
                        stop_lag = min(first_lag+self.batch_size, self.lag_count)
                        lags = np.arange(first_lag, stop_lag)
                        windows = observation[lags[:, None]+kernel_indices[None, :]]
                        phase = np.exp(-2j*np.pi*values[:, None]*
                            (lags[None, :]/self.output_fs))
                        for ray_index, column in enumerate(workspace['columns']):
                            mixed = windows*column.conj()[None, :]
                            if transform is None:
                                modulation = np.exp(-2j*np.pi*values[0]*local_times)
                                transformed = np.sum(mixed*modulation[None, :], axis=-1, keepdims=True)
                            else:
                                transformed = transform(mixed, axis=-1)
                            correlation_bank[positions, ray_index, first_lag:stop_lag] = (
                                transformed.T*phase).astype(self.dtype, copy=False)
                            del mixed, transformed
                        del windows, phase
            scores = _quadratic_scores(correlation_bank, workspace['inverse'],
                explicit=workspace['quadratic_explicit'])/self.reference_variance
            _append_candidate_rows(local, workspace, frequencies, scores,
                self.arrivals, sample_separation, top_per_bit)
            # Release this workspace before allocating the next bank.
            del correlation_bank, scores
            yield local

    def score(self, raw, frequencies_hz=None, *, frequency_intervals=None,
              valid=None, top_per_bit=4, minimum_arrival_separation_seconds=None,
              projected_observation=None):
        started = _clock()
        top_per_bit = _integer(top_per_bit, 'top candidates per bit')
        if top_per_bit > 64:
            raise ValueError('top candidates per bit exceeds64 proposal budget')
        separation = (0.5/float(self.manifest['format'].get('fine_chip_rate_hz', 60.))
                      if minimum_arrival_separation_seconds is None else
                      _number(minimum_arrival_separation_seconds, 'candidate separation', positive=True))
        if frequency_intervals is None and frequencies_hz is not None:
            frequencies_hz = tuple(frequencies_hz)
        elif frequency_intervals is not None:
            frequency_intervals = tuple(tuple(interval) for interval in frequency_intervals)
        frequencies, failure = self._frequencies(frequencies_hz, frequency_intervals)
        groups = (None if frequencies is None or self.correlation_method == 'frequency' else
            self._lag_frequency_groups(frequencies_hz, frequency_intervals))
        retained_input = 0
        if projected_observation is None and self._uses_streamed_observation():
            # Standalone scoring does not have CompleteBitReceiver's separate
            # live input reservation. Bound conversion for array-like callers.
            retained_input = sum(value.nbytes if isinstance(value, np.ndarray) else 32*self.samples
                                 for value in (raw, valid) if value is not None)
        plan = (None if frequencies is None else
            self._correlation_plan(frequencies, groups, raw_input=projected_observation is None,
                retained_input_bytes=retained_input, top_per_bit=top_per_bit, separation=separation))
        if frequencies is not None and plan['selected'] is None:
            product_admitted = any(method == self.correlation_method or self.correlation_method == 'auto'
                for method in plan['available_methods'])
            failure = 'correlation_working_memory_limit' if product_admitted else 'fft_product_work_limit'
        result = dict(schema='triad-realtime-fft-proposals-v1', status='unresolved', reason=failure,
            candidates=[], candidates_by_bit=[[], []], procedure_id=self.procedure_id,
            policy=self.policy, preparation_timing=self.preparation_timing,
            admission='none; adaptive proposals require independent whole-procedure calibration')
        if failure:
            result['timing'] = dict(total=_elapsed(started))
            return result
        method = plan['selected'][0]
        if projected_observation is not None:
            if raw is not None or valid is not None:
                raise ValueError('prepared projection requires raw=None and no separate validity')
            observation = self._coerce_projected(projected_observation)
        peak = plan['estimated_peak_bytes_by_method'][method]
        if peak > self.memory_limit_bytes:
            result.update(reason='observation_fft_memory_limit', timing=dict(total=_elapsed(started)))
            return result
        mark = _clock()
        if projected_observation is None:
            observation = self.prepare_observation(raw, valid)
        filtering_timing = _elapsed(mark)
        mark = _clock()
        spectra = None
        if method == 'frequency':
            spectra = self._frequency_spectra(observation, frequencies)
        observation_fft_timing = _elapsed(mark)
        mark = _clock()
        sample_separation = max(1, math.ceil(separation*self.output_fs))
        if method == 'frequency':
            rows = self._frequency_workspace_rows(spectra, frequencies,
                sample_separation=sample_separation, top_per_bit=top_per_bit)
        else:
            rows = self._lag_workspace_rows(observation, frequencies, plan['groups'],
                sample_separation=sample_separation, top_per_bit=top_per_bit)
        retained = self._retained_candidates(rows, top_per_bit=top_per_bit,
            separation=separation)
        score_timing = _elapsed(mark)
        result.update(status='proposed', reason='bounded_static_coarse_screen',
            candidates=retained[0]+retained[1], candidates_by_bit=retained,
            frequencies_hz=frequencies.tolist(),
            work=dict(epochs=len(self.epochs), clocks=len(self.clocks), bits=2, rays=len(self.rays),
                arrival_points=self.lag_count, frequency_points=len(frequencies),
                synchronization_hypotheses=len(self.epochs)*len(self.clocks)*self.lag_count*len(frequencies),
                fft_products=plan['selected'][1],
                selected_correlation_method=method,
                correlation_work_butterfly_estimate=plan['selected'][2],
                correlation_method_work_scope=plan['work_scope'],
                frequency_grid_groups=plan['frequency_group_count'],
                lag_domain_fft_lengths=plan['lag_fft_lengths'] if method == 'lag' else [],
                lag_domain_cached_kernel_fft_count=(sum(size > 0 for size in plan['lag_fft_lengths'])
                    if method == 'lag' else 0),
                lag_domain_data_fft_count=(2*len(self.workspaces)*len(self.rays)*self.lag_count*
                    sum(size > 0 for size in plan['lag_fft_lengths']) if method == 'lag' else 0),
                lag_domain_correlation_products=plan['lag_products'] if method == 'lag' else 0,
                lag_domain_fft_transform_lanes=plan['lag_fft_transform_lanes'] if method == 'lag' else 0,
                fft_length=self.nfft, template_workspaces=len(self.workspaces),
                explicit_quadratic_workspaces=sum(w['quadratic_explicit'] for w in self.workspaces),
                guarded_quadratic_workspaces=sum(not w['quadratic_explicit'] for w in self.workspaces),
                observation_ffts=len(frequencies) if method == 'frequency' else 0,
                actual_fft_transform_count=(len(frequencies)+plan['legacy_products']
                    if method == 'frequency' else plan['lag_fft_transform_lanes']),
                estimated_peak_bytes=peak,
                source_support_complete=True, coarse_projection_only=True),
            timing=dict(filtering=filtering_timing, observation_fft=observation_fft_timing,
                        correlation_and_selection=score_timing, total=_elapsed(started)))
        return result


class TiledFrequencySession:
    """One exact full-CFO search, resumed by a single coordinating owner.

    Kernels keep their existing work ceilings. Raw arrival peaks are retained
    separately for each canonical workspace until every CFO has been scored;
    only then can the shared across-frequency NMS produce proposals. Executor
    jobs process immutable numerical inputs and return rows in workspace order.
    """

    def __init__(self, screen, projected, frequencies_hz=None, *, executor=None,
                 workspace_workers=1, **arguments):
        started = _clock()
        self.screen, self.executor = screen, executor
        if executor is None and workspace_workers != 1:
            raise ValueError('parallel workspace workers require an executor')
        self.work_plan = screen.tiled_frequency_plan(frequencies_hz,
            workspace_workers=workspace_workers, **arguments)
        if self.work_plan['status'] != 'ready':
            raise ValueError('exact tiled frequency search is unavailable: '+self.work_plan['reason'])
        self.observation = screen._coerce_projected(projected, copy=True)
        self.frequencies = np.asarray(self.work_plan['frequencies_hz'], dtype=float)
        self.frequencies.flags.writeable = False
        self.top_per_bit = self.work_plan['top_per_bit']
        self.separation = self.work_plan['minimum_arrival_separation_seconds']
        self.sample_separation = max(1, math.ceil(self.separation*screen.output_fs))
        self.workspace_workers = self.work_plan['workspace_workers']
        self._rows = [[] for _ in screen.workspaces]
        self._next_frequency = self._completed_units = 0
        self._failed = self._closed = False
        self._result = None
        self.procedure_id = _digest(dict(screen=screen.procedure_id,
            complete_frequency_plan=self.work_plan))
        zero = lambda: dict(cpu_seconds=0., wall_seconds=0.)
        self._timing = dict(filtering=zero(), observation_fft=zero(),
            correlation_and_selection=zero(), total=_elapsed(started))

    @staticmethod
    def _add(target, value):
        for key in ('cpu_seconds', 'wall_seconds'):
            target[key] += value[key]

    @property
    def complete(self):
        return not self._failed and self._next_frequency == self.work_plan['frequency_points']

    def _base_result(self):
        return dict(schema='triad-realtime-fft-proposals-v1', status='unresolved',
            reason='closed_frequency_tiles' if self._closed else
                'failed_frequency_tile' if self._failed else 'incomplete_frequency_tiles',
            candidates=[], candidates_by_bit=[[], []], procedure_id=self.procedure_id,
            policy=dict(self.screen.policy,
                complete_frequency_search='exact resumable tiles; ordinary ceilings apply per kernel'),
            preparation_timing=self.screen.preparation_timing,
            admission='none; adaptive proposals require independent whole-procedure calibration',
            frequency_tiles=dict(completed_units=self._completed_units,
                completed_frequency_points=self._next_frequency,
                complete_public_search=self.complete,
                raw_candidate_rows=sum(map(len, self._rows)), work_plan=self.work_plan),
            timing={key:dict(value) for key, value in self._timing.items()})

    def step(self, units=1):
        """Score at most64 bounded units; this never returns partial proposals."""
        units = _integer(units, 'frequency units per step')
        if units > 64:
            raise ValueError('frequency step exceeds64 bounded units')
        if self._closed or self._failed:
            raise RuntimeError('frequency tile session is not usable')
        if self._result is not None:
            raise RuntimeError('frequency tile session was finalized')
        started = _clock()
        fft_timing = dict(cpu_seconds=0., wall_seconds=0.)
        score_timing = dict(cpu_seconds=0., wall_seconds=0.)
        completed_before = self._completed_units
        first_frequency = self._next_frequency
        try:
            for _ in range(units):
                if self.complete:
                    break
                first = self._next_frequency
                stop = min(first+self.work_plan['frequency_unit_points'], len(self.frequencies))
                frequencies = self.frequencies[first:stop]
                mark = _clock()
                spectra = self.screen._frequency_spectra(self.observation, frequencies,
                    workers=1 if self.executor is not None else self.screen.workers)
                self.screen._mark_readonly(spectra)
                self._add(fft_timing, _elapsed(mark))
                mark = _clock()
                def group(indices):
                    return [(index, self.screen._frequency_peak_rows(
                        self.screen.workspaces[index], spectra, frequencies,
                        sample_separation=self.sample_separation, top_per_bit=self.top_per_bit,
                        workers=1 if self.executor is not None else self.screen.workers))
                        for index in indices]
                if self.executor is None:
                    rows = enumerate(self.screen._frequency_workspace_rows(spectra, frequencies,
                        sample_separation=self.sample_separation, top_per_bit=self.top_per_bit))
                else:
                    groups = np.array_split(np.arange(len(self.screen.workspaces)),
                        self.workspace_workers)
                    futures, error = [], None
                    for indices in groups:
                        try:
                            futures.append(self.executor.submit(group, indices))
                        except BaseException as exc:
                            error = exc
                            break
                    rows = []
                    # Join every job even after a failure before releasing the
                    # shared spectra or allowing an address/cache switch.
                    for future in futures:
                        try:
                            rows.extend(future.result())
                        except BaseException as exc:
                            if error is None:
                                error = exc
                    if error is not None:
                        raise error
                for index, local in rows:
                    self._rows[index].extend(local)
                self._next_frequency = stop
                self._completed_units += 1
                del spectra, rows
                self._add(score_timing, _elapsed(mark))
        except BaseException:
            self._failed = True
            raise
        finally:
            elapsed = _elapsed(started)
            self._add(self._timing['observation_fft'], fft_timing)
            self._add(self._timing['correlation_and_selection'], score_timing)
            self._add(self._timing['total'], elapsed)
        return dict(schema='triad-frequency-tile-step-v1', status='scored_tiles',
            complete_public_search=self.complete,
            completed_units=self._completed_units-completed_before,
            completed_frequency_points=self._next_frequency-first_frequency,
            cumulative_units=self._completed_units,
            cumulative_frequency_points=self._next_frequency,
            raw_candidate_rows=sum(map(len, self._rows)),
            raw_candidate_rows_by_workspace=list(map(len, self._rows)),
            proposals_available=False,
            timing=dict(observation_fft=fft_timing, correlation_and_peak_retention=score_timing,
                total=elapsed))

    def _select(self, *, copy_rows):
        retained = [[], []]
        for workspace, original in zip(self.screen.workspaces, self._rows):
            local = list(original) if copy_rows else original
            retained[workspace['bit']].extend(_select_workspace_candidates(local,
                self.top_per_bit, self.separation, 1/(2*self.screen.duration)))
        return _select_bit_candidates(retained, self.top_per_bit)

    def selection_probe(self):
        """Time current-row selection without producing admissible proposals.

        The full selection cost grows with retained rows and their score order;
        this diagnostic is not, by itself, a full-grid extrapolation guarantee.
        """
        if self._closed or self._failed:
            raise RuntimeError('frequency tile session is not usable')
        started = _clock()
        self._select(copy_rows=True)
        return dict(schema='triad-frequency-selection-probe-v1', status='diagnostic_only',
            complete_public_search=self.complete, proposals_available=False,
            raw_candidate_rows=sum(map(len, self._rows)),
            completed_frequency_points=self._next_frequency,
            included_in_receiver_service=False, timing=_elapsed(started))

    def finish(self):
        """Select proposals only after the exact full public grid completes."""
        if self._result is not None:
            return self._result
        if not self.complete or self._closed:
            return self._base_result()
        started = _clock()
        retained = self._select(copy_rows=False)
        elapsed = _elapsed(started)
        self._add(self._timing['correlation_and_selection'], elapsed)
        self._add(self._timing['total'], elapsed)
        result = self._base_result()
        screen, plan = self.screen, self.work_plan
        result.update(status='proposed', reason='bounded_exact_tiled_static_coarse_screen',
            candidates=retained[0]+retained[1], candidates_by_bit=retained,
            frequencies_hz=plan['frequencies_hz'],
            work=dict(epochs=len(screen.epochs), clocks=len(screen.clocks), bits=2,
                rays=len(screen.rays), arrival_points=screen.lag_count,
                frequency_points=plan['frequency_points'],
                synchronization_hypotheses=len(screen.epochs)*len(screen.clocks)*
                    screen.lag_count*plan['frequency_points'],
                fft_products=plan['full_fft_products'], selected_correlation_method='frequency',
                correlation_work_butterfly_estimate=plan['full_correlation_work_butterfly_estimate'],
                correlation_method_work_scope='Exact existing frequency kernel repeated over the full canonical grid; all per-CFO peaks precede shared canonical selection.',
                frequency_grid_groups=0, lag_domain_fft_lengths=[],
                lag_domain_cached_kernel_fft_count=0, lag_domain_data_fft_count=0,
                lag_domain_correlation_products=0, lag_domain_fft_transform_lanes=0,
                fft_length=screen.nfft, template_workspaces=len(screen.workspaces),
                explicit_quadratic_workspaces=sum(w['quadratic_explicit'] for w in screen.workspaces),
                guarded_quadratic_workspaces=sum(not w['quadratic_explicit'] for w in screen.workspaces),
                observation_ffts=plan['frequency_points'],
                coarse_score_kernel=screen.policy.get('coarse_score_kernel'),
                actual_fft_transform_count=plan['frequency_points']+plan['full_fft_products'],
                estimated_peak_bytes=plan['estimated_peak_bytes'],
                source_support_complete=True, coarse_projection_only=True,
                frequency_units=plan['units'],
                raw_candidate_rows=sum(map(len, self._rows)),
                raw_candidate_row_capacity=plan['raw_candidate_row_capacity']))
        self._result = result
        return result

    def close(self):
        """Release a completed or partial search without completing its grid."""
        self._rows.clear()
        self.observation = self.frequencies = None
        self._closed = True
