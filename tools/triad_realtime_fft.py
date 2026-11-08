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
                 maximum_fft_products=32768):
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
        source = np.asarray(raw_templates)
        if (source.ndim != 3 or source.shape[:2] != (len(self.epochs), 2) or
                source.shape[2] < 4 or not np.iscomplexobj(source) or not np.isfinite(source).all()):
            raise ValueError('finite complex templates need [epochs,2,source samples]')
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
        self.duration = source.shape[-1]/self.fs
        self.maximum_frequency_step = 1/(4*self.duration)

        # Common basis origin aligned to the arrival lattice.  Cubic zero
        # extension can add two input samples on either side of the source.
        scales = np.asarray(self.clocks)*1e-6+1
        earliest = min((self.offset-2/self.fs)/scales)
        self.origin = (math.floor((earliest+self.lead+self.lower)*self.output_fs)/
                       self.output_fs-self.lead-self.lower)
        latest = max((self.offset+(source.shape[-1]+2)/self.fs)/scales)+self.delay
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
                                  np.dtype(self.dtype).itemsize + source.nbytes)
        scratch = 4*self.raw_kernel_rows*16+4*self.nfft*np.dtype(self.dtype).itemsize
        if self.resident_estimate+scratch > self.memory_limit_bytes:
            raise MemoryError('coarse FFT template preparation exceeds memory limit')
        self.workspaces = []
        time_source = self.origin+np.arange(self.raw_kernel_rows, dtype=float)/self.fs
        for ei, epoch in enumerate(self.epochs):
            for clock, scale in zip(self.clocks, scales):
                for bit in (0, 1):
                    columns = []
                    for delay, doppler in self.rays:
                        position = (((time_source-delay)*scale)-self.offset)*self.fs
                        values = _sample_cubic(source[ei, bit], position)
                        values *= np.exp(2j*np.pi*np.remainder(doppler*time_source, 1.))
                        column = signal.oaconvolve(values.astype(self.dtype), self.taps,
                                                   mode='full')[::self.decimation].astype(self.dtype)
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
            maximum_frequency_points=self.maximum_frequency_points,
            maximum_fft_products=self.maximum_fft_products)
        self.procedure_id = _digest(dict(policy=self.policy,
            source_sha256={name: hashlib.sha256((Path(__file__).parent/name).read_bytes()).hexdigest()
                for name in ('triad_realtime_fft.py', 'triad_dynamic_joint.py', 'triad_acquisition.py')},
            template_sha256=hashlib.sha256(np.ascontiguousarray(source).view(np.uint8)).hexdigest(),
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

    def score(self, raw, frequencies_hz=None, *, frequency_intervals=None,
              valid=None, top_per_bit=4, minimum_arrival_separation_seconds=None):
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
        peak = (self.resident_estimate+projected_count*np.dtype(self.dtype).itemsize+
                batch_scratch+64*self.samples+32*self.crop_rows)
        if peak > self.memory_limit_bytes:
            result.update(reason='observation_fft_memory_limit', timing=dict(total=_elapsed(started)))
            return result
        mark = _clock()
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
