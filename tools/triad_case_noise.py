"""Exact finite projected proper-complex Gaussian noise for FFTScreen.

The DATA projection and optional Helper raw prefix share one noise draw. The
remaining raw noise is integrated out into a finite, banded covariance; it is
not regenerated per hypothesis and is not whitened.
"""
from __future__ import annotations

import hashlib
import math

import numpy as np
from scipy.linalg import cholesky_banded
from scipy.signal import upfirdn


_MAX_OUTPUT_ROWS = 1_000_000
_MAX_FACTOR_WORK = 100_000_000
_MAX_FACTOR_WORK_HARD = 500_000_000
_MAX_BOUNDARY_DOT_WORK = 100_000_000


def _integer(value, name, *, minimum=0):
    if isinstance(value, (bool, np.bool_)) or not isinstance(value, (int, np.integer)):
        raise ValueError(name + ' must be an integer')
    value = int(value)
    if value < minimum:
        raise ValueError(name + ' is out of range')
    return value


def _ceil_div(value, divisor):
    return -((-value)//divisor)


def _lag_ranges(p0, prefix, raw_samples, tap_count, decimation, count, delta):
    """Return nonzero and full-support column ranges for one covariance lag."""
    valid_lo = max(0, min(count, _ceil_div(prefix-p0, decimation)))
    valid_hi = max(valid_lo, min(count,
        (raw_samples+tap_count-2-delta-p0)//decimation+1))
    full_lo = max(valid_lo, min(valid_hi,
        _ceil_div(prefix+tap_count-1-p0, decimation)))
    full_hi = max(full_lo, min(valid_hi,
        (raw_samples-1-delta-p0)//decimation+1))
    return valid_lo, full_lo, full_hi, valid_hi


class ProjectedNoise:
    """Draw unit-variance complex noise after an FFTScreen finite FIR.

    ``receipt`` is the unchanged result of ``FFTScreen.projection_receipt``.
    ``taps`` must be that screen's exact tap vector. If a Helper prefix is
    requested, the returned raw prefix and projected DATA noise contain the
    same prefix realization; only the remaining DATA contribution is drawn
    from its exact finite covariance.
    """

    def __init__(self, receipt, taps, *, helper_prefix_samples=0,
                 memory_limit_bytes=512*1024**2, maximum_bandwidth=256,
                 maximum_factor_work=_MAX_FACTOR_WORK):
        if not hasattr(receipt, 'get'):
            raise ValueError('projection receipt must be a mapping')
        self.receipt = receipt
        self.raw_samples = _integer(receipt.get('raw_samples'), 'raw sample count', minimum=1)
        self.decimation = _integer(receipt.get('downsample'), 'downsample', minimum=1)
        input_rate = _integer(receipt.get('input_sample_rate_hz'), 'input sample rate', minimum=1)
        output_rate = _integer(receipt.get('output_sample_rate_hz'), 'output sample rate', minimum=1)
        self.crop_first = _integer(receipt.get('cropped_output_start'), 'cropped output start')
        self.output_count = _integer(receipt.get('cropped_output_count'), 'cropped output count')
        self.helper_prefix_samples = _integer(helper_prefix_samples, 'Helper prefix samples')
        if self.helper_prefix_samples > self.raw_samples:
            raise ValueError('Helper prefix exceeds finite raw observation')
        self.memory_limit_bytes = _integer(memory_limit_bytes, 'memory limit', minimum=1)
        self.maximum_bandwidth = _integer(maximum_bandwidth, 'maximum bandwidth')
        self.maximum_factor_work = _integer(maximum_factor_work, 'maximum factor work', minimum=1)
        if self.maximum_factor_work > _MAX_FACTOR_WORK_HARD:
            raise ValueError('maximum factor work exceeds hard operation limit')
        if receipt.get('schema') != 'triad-complex-fir-projection-v1':
            raise ValueError('unsupported finite FIR projection receipt')
        if receipt.get('decimation_phase_raw_samples') != 0:
            raise ValueError('projected noise requires decimation phase zero')
        if (input_rate % output_rate or input_rate//output_rate != self.decimation or
                receipt.get('finite_input_boundary') !=
                'Exactly zero outside raw sample indices0..raw_samples-1; complete causal FIR tail retained before crop.'):
            raise ValueError('projection receipt does not describe the supported finite zero-boundary rate mapping')
        p0 = self.crop_first*self.decimation
        if (receipt.get('first_cropped_convolution_raw_sample') != p0 or
                receipt.get('complete_filtered_output_count') !=
                (self.raw_samples + int(receipt.get('fir_taps_count', 0)) - 2)//self.decimation + 1):
            raise ValueError('inconsistent finite FIR projection coordinates')
        tap_count = _integer(receipt.get('fir_taps_count'), 'FIR tap count', minimum=1)
        tap_dtype = receipt.get('fir_taps_dtype')
        if tap_dtype not in ('<f4', '<f8'):
            raise ValueError('unsupported FIR tap dtype')
        wanted_dtype = np.dtype(tap_dtype)
        tap_array = np.asarray(taps)
        if tap_array.ndim != 1 or len(tap_array) != tap_count or tap_array.dtype != wanted_dtype:
            raise ValueError('FIR taps do not match projection receipt shape or dtype')
        digest = hashlib.sha256(tap_array.tobytes()).hexdigest()
        if digest != receipt.get('fir_taps_sha256'):
            raise ValueError('FIR tap hash does not match projection receipt')
        if not np.isfinite(tap_array).all():
            raise ValueError('FIR taps must be finite')
        self.taps = tap_array
        self.bandwidth = (len(tap_array)-1)//self.decimation
        if self.bandwidth > self.maximum_bandwidth:
            raise ValueError('finite covariance bandwidth exceeds configured maximum')
        if self.output_count > _MAX_OUTPUT_ROWS:
            raise ValueError('projected output exceeds bounded row limit')
        if self.crop_first + self.output_count > receipt['complete_filtered_output_count']:
            raise ValueError('cropped output lies outside complete finite FIR output')

        # Account for simultaneous covariance/factor arrays, output temporaries,
        # and the explicitly retained Helper prefix before allocating any of them.
        complex_bytes = np.dtype(np.complex128).itemsize
        band_cells = (self.bandwidth+1)*self.output_count
        # Three band arrays cover the live covariance, its active-row copy, and
        # SciPy's possible Fortran-order factorization copy at peak.
        self.owned_workspace_bytes = (3*band_cells*complex_bytes +
            8*self.output_count*complex_bytes + 4*self.helper_prefix_samples*complex_bytes +
            len(tap_array)*complex_bytes)
        if self.owned_workspace_bytes > self.memory_limit_bytes:
            raise MemoryError('projected noise workspace exceeds declared memory limit')
        factor_work = self.output_count*(self.bandwidth+1)*(self.bandwidth+2)//2
        if self.helper_prefix_samples != self.raw_samples and factor_work > self.maximum_factor_work:
            raise ValueError('banded Cholesky work exceeds bounded operation limit')
        if self.helper_prefix_samples != self.raw_samples:
            boundary_work = 0
            p0 = self.crop_first*self.decimation
            for lag in range(self.bandwidth+1):
                count = self.output_count-lag
                if count <= 0:
                    break
                delta = lag*self.decimation
                valid_lo, full_lo, full_hi, valid_hi = _lag_ranges(
                    p0, self.helper_prefix_samples, self.raw_samples, len(tap_array),
                    self.decimation, count, delta)
                for start, stop in ((valid_lo, full_lo), (full_hi, valid_hi)):
                    for j in range(start, stop):
                        p = p0+j*self.decimation
                        r0 = max(self.helper_prefix_samples, p+delta-len(tap_array)+1)
                        r1 = min(self.raw_samples, p+1)
                        boundary_work += max(0, r1-r0)
            self.boundary_work_estimate = boundary_work
            if boundary_work > _MAX_BOUNDARY_DOT_WORK:
                raise ValueError('finite covariance boundary work exceeds bounded operation limit')
        else:
            self.boundary_work_estimate = 0

        self._factor = None
        self._active_start = 0
        self._active_stop = self.output_count
        if self.helper_prefix_samples < self.raw_samples and self.output_count:
            covariance = self._rest_covariance()
            row_power = covariance[0].real
            nonzero = np.flatnonzero(row_power != 0)
            if not len(nonzero):
                self._active_start = self._active_stop = 0
            else:
                self._active_start, self._active_stop = int(nonzero[0]), int(nonzero[-1])+1
                if np.any(row_power[self._active_start:self._active_stop] == 0):
                    raise np.linalg.LinAlgError('non-contiguous zero variance in projected noise')
                covariance = np.array(covariance[:, self._active_start:self._active_stop], copy=True)
                try:
                    self._factor = cholesky_banded(covariance, lower=True, overwrite_ab=True,
                                                    check_finite=False)
                except np.linalg.LinAlgError as exc:
                    raise np.linalg.LinAlgError(
                        'finite projected-noise covariance is singular or not positive definite') from exc

    @property
    def covariance_bandwidth(self):
        return self.bandwidth

    def _rest_covariance(self):
        """Lower-band C where C[i-j,j] is covariance[i,j]."""
        n, b, d = self.output_count, self.bandwidth, self.decimation
        p0, raw_n, prefix = self.crop_first*d, self.raw_samples, self.helper_prefix_samples
        h = self.taps.astype(np.complex128, copy=False)
        ab = np.zeros((b+1, n), dtype=np.complex128)
        for lag in range(b+1):
            count = n-lag
            if count <= 0:
                break
            delta = lag*d
            # Fully interior windows have the same full FIR autocorrelation.
            valid_lo, full_lo, full_hi, valid_hi = _lag_ranges(
                p0, prefix, raw_n, len(h), d, count, delta)
            if full_hi > full_lo:
                ab[lag, full_lo:full_hi] = np.dot(h[delta:], h[:len(h)-delta].conj())
            # Boundary windows use direct clipped dot products. This avoids
            # subtracting a nearly equal prefix covariance from a full one.
            for j in range(valid_lo, full_lo):
                p = p0+j*d
                r0 = max(prefix, p+delta-len(h)+1)
                r1 = min(raw_n, p+1)
                if r1 <= r0:
                    continue
                left = h[p+delta-r1+1:p+delta-r0+1][::-1]
                right = h[p-r1+1:p-r0+1][::-1]
                ab[lag, j] = np.dot(left, right.conj())
            for j in range(full_hi, valid_hi):
                p = p0+j*d
                r0 = max(prefix, p+delta-len(h)+1)
                r1 = min(raw_n, p+1)
                if r1 <= r0:
                    continue
                left = h[p+delta-r1+1:p+delta-r0+1][::-1]
                right = h[p-r1+1:p-r0+1][::-1]
                ab[lag, j] = np.dot(left, right.conj())
        return ab

    def draw(self, seed):
        """Return projected DATA noise and the coupled raw Helper prefix."""
        rng = seed if isinstance(seed, np.random.Generator) else np.random.default_rng(seed)
        prefix = (rng.standard_normal(self.helper_prefix_samples) +
                  1j*rng.standard_normal(self.helper_prefix_samples))/math.sqrt(2)
        prefix = np.asarray(prefix, dtype=np.complex128)
        projected = np.zeros(self.output_count, dtype=np.complex128)
        if self.helper_prefix_samples:
            full = upfirdn(self.taps.astype(np.float64), prefix, down=self.decimation)
            lo = self.crop_first
            hi = min(lo+self.output_count, len(full))
            if hi > lo:
                projected[:hi-lo] = full[lo:hi]
        if self._factor is not None:
            n = self._active_stop-self._active_start
            z = (rng.standard_normal(n)+1j*rng.standard_normal(n))/math.sqrt(2)
            rest = np.zeros(n, dtype=np.complex128)
            for lag in range(min(self.bandwidth, n-1)+1):
                rest[lag:] += self._factor[lag, :n-lag]*z[:n-lag]
            projected[self._active_start:self._active_stop] += rest
        return dict(projected_noise=projected, helper_raw_noise=prefix)
