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

import hashlib
import json
import math
from pathlib import Path
import time

import numpy as np
from scipy import fft, signal

from triad_dynamic_joint import _sample_cubic
from triad_acquisition import data_fir_taps


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


def _fir_decimate_chunks(chunks, taps, decimation, samples, dtype, chunk_samples):
    """Exact finite causal FIR/downsample operator with bounded input tiles.

    Each tile's COMPLETE linear-convolution tail is added on the same global
    output lattice. No filter state, boundary samples or endpoint tail is
    discarded. The output covariance remains the FIR's colored covariance;
    downsampling alone does not whiten it.
    """
    count = (samples + len(taps) - 2)//decimation + 1
    output = np.zeros(count, dtype=dtype)
    transform_rows = fft.next_fast_len(chunk_samples + len(taps) - 1)
    transformed_taps = fft.fft(np.asarray(taps, dtype=dtype), n=transform_rows)
    consumed = 0
    for source in chunks:
        values = np.asarray(source)
        if (values.ndim != 1 or not np.iscomplexobj(values) or not len(values) or
                len(values) > chunk_samples or not np.isfinite(values).all()):
            raise ValueError('FIR chunks require bounded nonempty finite complex vectors')
        if consumed + len(values) > samples:
            raise ValueError('FIR stream exceeds the declared complete observation')
        filtered = fft.ifft(fft.fft(values.astype(dtype), n=transform_rows) * transformed_taps)
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
    first = max(0, math.floor(float(np.min(positions))) - 1)
    stop = min(source_samples, math.floor(float(np.max(positions))) + 3)
    if stop <= first:
        return np.zeros(len(positions), np.complex128)
    source = np.asarray(provider(epoch, bit, first, stop - first))
    if (source.shape != (stop - first,) or not np.iscomplexobj(source) or
            not np.isfinite(source).all()):
        raise ValueError('template provider must return the requested finite complex integer-source tile')
    return _sample_cubic(source, positions - first)


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


def _mapped_finite_source(provider, epoch, bit, positions, source_samples, edge_cache):
    """Native mapped interior with the exact finite raw-cubic edge stencil."""
    result = np.zeros(len(positions), np.complex128)
    low = (positions >= -2) & (positions < 1)
    high = (positions > source_samples-2) & (positions <= source_samples+1)
    interior = (positions >= 1) & (positions <= source_samples-2)
    def render(coordinates):
        values = np.asarray(provider(epoch, bit, coordinates))
        if (values.shape != coordinates.shape or not np.iscomplexobj(values) or
                not np.isfinite(values).all()):
            raise ValueError('mapped template provider must return finite complex samples matching positions')
        return values
    if low.any() or high.any():
        key = (epoch, bit)
        if key not in edge_cache:
            edge_cache[key] = (render(np.arange(4, dtype=float)),
                render(source_samples-4+np.arange(4, dtype=float)))
        first, last = edge_cache[key]
        result[low] = _sample_cubic(first, positions[low])
        result[high] = _sample_cubic(last, positions[high]-(source_samples-4))
    if interior.any():
        result[interior] = render(positions[interior])
    return result


def _project_coarse_column(values, raw_samples, taps, step, downsample, kernel, shift, dtype):
    """Project the finite coarse interpolant, removing its exterior spill."""
    rows = (raw_samples+len(taps)-2)//downsample+1
    filtered = signal.upfirdn(kernel, values, up=step, down=downsample)
    output = np.zeros(rows, np.complex128)
    count = min(rows, max(0, len(filtered)-shift))
    output[:count] = filtered[shift:shift+count]
    # Cubic reconstruction has support outside the finite raw column. Those
    # samples were not part of the original operator and must be subtracted.
    output_coordinates = np.arange(rows)*downsample
    for first in (-2*step, raw_samples):
        # A caller may supply a final nonzero coarse knot AFTER raw_samples-1.
        # Its cubic support can reach almost three cells past the raw endpoint.
        coordinates = np.arange(first, first+(2 if first < 0 else 3)*step+2)
        if first < 0:
            coordinates = coordinates[coordinates < 0]
        outside = _sample_cubic(values, coordinates/step)
        correction = signal.oaconvolve(outside, taps, mode='full')
        indices = output_coordinates-first
        keep = (indices >= 0) & (indices < len(correction))
        output[keep] -= correction[indices[keep]]
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


def _quadratic_scores(correlations, inverse, *, explicit=True):
    """Small Hermitian form without a strided BLAS contraction.

    FFT products remain FP32/FP64 as selected; the explicit two-ray arithmetic
    is FP64 in both modes.  Ill-conditioned/rank-deficient two-ray preparations
    retain the previous contraction order, rather than changing their rounding
    behavior without separate qualification.  No score clipping is introduced.
    """
    # NumPy's one-ray optimized contraction can form FP32 |c|² before its
    # FP64 scalar multiply.  Keep that existing rounding path unchanged.
    if not explicit or correlations.shape[1] != 2:
        return np.einsum('brl,rs,bsl->bl', correlations.conj(), inverse,
                         correlations, optimize=True).real
    a = np.asarray(correlations[:, 0], dtype=np.complex128)
    result = inverse[0, 0].real*(a.real*a.real+a.imag*a.imag)
    if correlations.shape[1] == 2:
        b = np.asarray(correlations[:, 1], dtype=np.complex128)
        result += inverse[1, 1].real*(b.real*b.real+b.imag*b.imag)
        result += 2*np.real(a.conj()*inverse[0, 1]*b)
    return result


class FFTScreen:
    """Reusable public templates and Fourier factors, independent of observations.

    ``clock_ppm`` is a coarse covering bank, not a claim of exhaustive continuous
    clock acquisition.  Its residual distortion must be checked after arrival
    re-centering and refined by the caller.  ``score`` returns proposals for
    **both** bits; it does not commit a payload decision or an admission.
    """

    def __init__(self, manifest, raw_templates, epoch_offsets, *,
                 clock_ppm=(-300., 0., 300.), output_sample_rate=1000,
                 precision='fp32', pass_edge_hz=400., stop_edge_hz=490.,
                 frequency_batch_size=8, workers=2,
                 memory_limit_bytes=1024**3, maximum_frequency_points=1024,
                 maximum_fft_products=32768, template_provider=None,
                 source_samples=None, template_identity=None, template_chunk_samples=16384,
                 mapped_template_provider=None, template_sampling_step=1):
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
        self.workers = _integer(workers, 'workers')
        self.batch_size = _integer(frequency_batch_size, 'frequency batch size')
        self.memory_limit_bytes = _integer(memory_limit_bytes, 'memory limit')
        self.maximum_frequency_points = _integer(maximum_frequency_points, 'maximum frequency points')
        self.maximum_fft_products = _integer(maximum_fft_products, 'maximum FFT products')
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
        if template_provider is None and mapped_template_provider is None:
            scratch = 4*self.raw_kernel_rows*16+4*self.nfft*np.dtype(self.dtype).itemsize
        elif mapped_template_provider is not None:
            coarse_rows = math.ceil((self.raw_kernel_rows-1)/self.template_sampling_step)+1
            scratch = (8*coarse_rows*16 + 16*(len(self.taps)+4*self.template_sampling_step)*16 +
                4*self.nfft*np.dtype(self.dtype).itemsize)
        else:
            transform_rows = fft.next_fast_len(template_chunk_samples + len(self.taps) - 1)
            provider_rows = min(source_samples, math.ceil(template_chunk_samples*float(max(scales))) + 4)
            scratch = (8*transform_rows*np.dtype(self.dtype).itemsize + 4*provider_rows*16 +
                       4*self.nfft*np.dtype(self.dtype).itemsize)
        if self.resident_estimate+scratch > self.memory_limit_bytes:
            raise MemoryError('coarse FFT template preparation exceeds memory limit')
        self.workspaces = []
        time_source = (self.origin+np.arange(self.raw_kernel_rows, dtype=float)/self.fs
                       if template_provider is None and mapped_template_provider is None else None)
        collapsed = (_collapsed_cubic_fir(self.taps, self.template_sampling_step, self.decimation)
                     if mapped_template_provider is not None else None)
        mapped_edges = {}
        for ei, epoch in enumerate(self.epochs):
            for clock, scale in zip(self.clocks, scales):
                for bit in (0, 1):
                    columns = []
                    for delay, doppler in self.rays:
                        if template_provider is None and mapped_template_provider is None:
                            position = (((time_source-delay)*scale)-self.offset)*self.fs
                            values = _sample_cubic(source[ei, bit], position)
                            values *= np.exp(2j*np.pi*np.remainder(doppler*time_source, 1.))
                            column = signal.oaconvolve(values.astype(self.dtype), self.taps,
                                                       mode='full')[::self.decimation].astype(self.dtype)
                        elif mapped_template_provider is not None:
                            step = self.template_sampling_step
                            coarse = np.zeros(math.ceil((self.raw_kernel_rows-1)/step)+1, np.complex128)
                            for first in range(0, len(coarse), template_chunk_samples):
                                stop = min(len(coarse), first+template_chunk_samples)
                                raw_indices = np.arange(first, stop)*step
                                times = self.origin+raw_indices/self.fs
                                positions = (((times-delay)*scale)-self.offset)*self.fs
                                keep = raw_indices < self.raw_kernel_rows
                                if keep.any():
                                    supplied = _mapped_finite_source(mapped_template_provider, epoch, bit,
                                        positions[keep], source_samples, mapped_edges)
                                    coarse[first:stop][keep] = supplied*np.exp(
                                        2j*np.pi*np.remainder(doppler*times[keep], 1.))
                            column = _project_coarse_column(coarse, self.raw_kernel_rows, self.taps,
                                step, self.decimation, *collapsed, self.dtype)
                        else:
                            def chunks():
                                for first in range(0, self.raw_kernel_rows, template_chunk_samples):
                                    stop = min(self.raw_kernel_rows, first + template_chunk_samples)
                                    times = self.origin + np.arange(first, stop, dtype=float)/self.fs
                                    position = (((times-delay)*scale)-self.offset)*self.fs
                                    values = _provider_cubic(template_provider, epoch, bit, position, source_samples)
                                    yield values * np.exp(2j*np.pi*np.remainder(doppler*times, 1.))
                            column = _fir_decimate_chunks(chunks(), self.taps, self.decimation,
                                self.raw_kernel_rows, self.dtype, template_chunk_samples)
                        columns.append(column)
                    columns = np.asarray(columns, dtype=self.dtype)
                    if columns.shape != (len(self.rays), self.kernel_rows):
                        raise RuntimeError('coarse source support accounting changed')
                    gram = columns.astype(np.complex128).conj() @ columns.astype(np.complex128).T
                    eigenvalues, eigenvectors = np.linalg.eigh((gram+gram.conj().T)*.5)
                    keep = eigenvalues > max(float(eigenvalues[-1]), 0.)*1e-10
                    if not keep.any():
                        raise ValueError('coarse pattern has no observable energy')
                    inverse = (eigenvectors[:, keep]/eigenvalues[keep]) @ eigenvectors[:, keep].conj().T
                    transformed = fft.fft(columns, n=self.nfft, axis=-1, workers=self.workers).conj()
                    self.workspaces.append(dict(epoch=epoch, clock_ppm=clock, bit=bit,
                        columns=columns, transform=transformed, gram=gram, inverse=inverse,
                        rank=int(keep.sum()), quadratic_explicit=bool(len(self.rays) == 2 and keep.all() and
                            eigenvalues[-1]/eigenvalues[0] <= 1e6)))
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
            maximum_fft_products=self.maximum_fft_products)
        if template_provider is not None:
            self.policy.update(template_storage='bounded integer-source callback; identical cubic/warp/FIR operator',
                template_chunk_samples=template_chunk_samples, template_provider_identity=source_identity)
        if mapped_template_provider is not None:
            self.policy.update(template_storage='bounded mapped native samples; coarse cubic reconstruction plus exact original FIR polyphase composition',
                template_sampling_step=self.template_sampling_step,
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

    def _frequencies(self, frequencies_hz, frequency_intervals):
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
                if count+sum(len(x) for x in pieces) > self.maximum_frequency_points:
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
        if len(bank) > self.maximum_frequency_points:
            return None, 'frequency_work_limit'
        if max(abs(bank))+self.source_half_width+abs(self.doppler) > self.pass_edge:
            return None, 'frequency_bank_outside_nominal_passband'
        if len(bank)*len(self.workspaces)*len(self.rays) > self.maximum_fft_products:
            return None, 'fft_product_work_limit'
        return bank, None

    def prepare_observation(self, raw, valid=None):
        """Filter the complete capture once, retaining the full finite FIR tail."""
        raw = np.asarray(raw)
        if raw.shape != (self.samples,) or not np.iscomplexobj(raw) or not np.isfinite(raw).all():
            raise ValueError('finite complex complete capture required')
        if valid is not None:
            valid = np.asarray(valid)
            if valid.shape != raw.shape or not np.isin(valid, (0, 1)).all():
                raise ValueError('binary capture validity required')
            if not valid.all():
                raise ValueError('coarse shift-invariant Gram screen does not support erasures')
        filtered = signal.oaconvolve(raw.astype(self.dtype), self.taps, mode='full')[::self.decimation]
        if self.crop_stop > len(filtered):
            raise ValueError('complete filtered support outside capture')
        return np.array(filtered[self.crop_first:self.crop_stop], dtype=self.dtype, copy=True)

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
        output_rows = (self.samples + len(self.taps) - 2)//self.decimation + 1
        transform_rows = fft.next_fast_len(self.template_chunk_samples + len(self.taps) - 1)
        peak = (self.resident_estimate +
            (2*output_rows + 8*transform_rows)*np.dtype(self.dtype).itemsize +
            self.template_chunk_samples*16)
        if peak > self.memory_limit_bytes:
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
        frequencies, failure = self._frequencies(frequencies_hz, frequency_intervals)
        result = dict(schema='triad-realtime-fft-proposals-v1', status='unresolved', reason=failure,
            candidates=[], candidates_by_bit=[[], []], procedure_id=self.procedure_id,
            policy=self.policy, preparation_timing=self.preparation_timing,
            admission='none; adaptive proposals require independent whole-procedure calibration')
        if failure:
            result['timing'] = dict(total=_elapsed(started))
            return result
        projected_count = len(frequencies)*self.nfft
        batch_scratch = self.batch_size*len(self.rays)*self.nfft*np.dtype(self.dtype).itemsize*3
        if projected_observation is not None:
            if raw is not None or valid is not None:
                raise ValueError('prepared projection requires raw=None and no separate validity')
            observation = np.asarray(projected_observation)
            if (observation.shape != (self.crop_rows,) or not np.iscomplexobj(observation) or
                    not np.isfinite(observation).all()):
                raise ValueError('finite prepared projection must match this screen crop')
        peak = (self.resident_estimate+projected_count*np.dtype(self.dtype).itemsize+
                batch_scratch+64*(self.samples if projected_observation is None else self.crop_rows)+32*self.crop_rows)
        if peak > self.memory_limit_bytes:
            result.update(reason='observation_fft_memory_limit', timing=dict(total=_elapsed(started)))
            return result
        mark = _clock()
        if projected_observation is None:
            observation = self.prepare_observation(raw, valid)
        filtering_timing = _elapsed(mark)
        mark = _clock()
        spectra = np.empty((len(frequencies), self.nfft), dtype=self.dtype)
        times = np.arange(len(observation), dtype=float)/self.output_fs
        for i, frequency in enumerate(frequencies):
            rotation = np.exp(-2j*np.pi*np.remainder(frequency*times, 1.)).astype(self.dtype)
            spectra[i] = fft.fft(observation*rotation, n=self.nfft, workers=self.workers)
        observation_fft_timing = _elapsed(mark)
        mark = _clock()
        retained = [[], []]
        sample_separation = max(1, math.ceil(separation*self.output_fs))
        frequency_separation = 1/(2*self.duration)
        for workspace in self.workspaces:
            bit = workspace['bit']
            # A bounded per-epoch/clock list avoids one strong event erasing all
            # alternatives before the other clock/epoch workspaces are visited.
            local = []
            for first in range(0, len(frequencies), self.batch_size):
                stop = min(first+self.batch_size, len(frequencies))
                products = spectra[first:stop, None, :]*workspace['transform'][None]
                correlations = fft.ifft(products, axis=-1, workers=self.workers)[..., :self.lag_count]
                scores = _quadratic_scores(correlations, workspace['inverse'],
                    explicit=workspace['quadratic_explicit'])/self.reference_variance
                for fi, score in enumerate(scores):
                    peaks = signal.find_peaks(score, distance=sample_separation)[0]
                    if len(score) == 1:
                        peaks = np.array([0])
                    else:
                        peaks = np.unique(np.r_[peaks, 0 if score[0] >= score[1] else [],
                            len(score)-1 if score[-1] >= score[-2] else []]).astype(int)
                    if len(peaks) > top_per_bit:
                        peaks = peaks[np.argpartition(score[peaks], -top_per_bit)[-top_per_bit:]]
                    for lag in peaks:
                        local.append(dict(bit=bit, epoch=workspace['epoch'],
                            arrival=float(self.arrivals[lag]), frequency=float(frequencies[first+fi]),
                            clock_ppm=workspace['clock_ppm'], score=float(score[lag]), rank=workspace['rank']))
            local.sort(key=lambda x: x['score'], reverse=True)
            unique = []
            for candidate in local:
                if any(abs(candidate['arrival']-old['arrival']) < separation and
                       abs(candidate['frequency']-old['frequency']) < frequency_separation for old in unique):
                    continue
                unique.append(candidate)
                if len(unique) >= top_per_bit:
                    break
            retained[bit].extend(unique)
        for bit in (0, 1):
            retained[bit] = sorted(retained[bit], key=lambda x: x['score'], reverse=True)[:top_per_bit]
        score_timing = _elapsed(mark)
        result.update(status='proposed', reason='bounded_static_coarse_screen',
            candidates=retained[0]+retained[1], candidates_by_bit=retained,
            frequencies_hz=frequencies.tolist(),
            work=dict(epochs=len(self.epochs), clocks=len(self.clocks), bits=2, rays=len(self.rays),
                arrival_points=self.lag_count, frequency_points=len(frequencies),
                synchronization_hypotheses=len(self.epochs)*len(self.clocks)*self.lag_count*len(frequencies),
                fft_products=len(self.workspaces)*len(self.rays)*len(frequencies),
                fft_length=self.nfft, template_workspaces=len(self.workspaces),
                explicit_quadratic_workspaces=sum(w['quadratic_explicit'] for w in self.workspaces),
                guarded_quadratic_workspaces=sum(not w['quadratic_explicit'] for w in self.workspaces),
                observation_ffts=len(frequencies), estimated_peak_bytes=peak,
                source_support_complete=True, coarse_projection_only=True),
            timing=dict(filtering=filtering_timing, observation_fft=observation_fft_timing,
                        correlation_and_selection=score_timing, total=_elapsed(started)))
        return result
