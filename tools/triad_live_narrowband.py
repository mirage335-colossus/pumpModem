#!/usr/bin/env python3
"""Optional fixed causal low-pass for isolated cable receiver workspaces.

Apply the same operator to the public basis and observations, after the existing
audio DDC. This is a new receiver/preprocessing configuration and requires new
null calibration. No gain, filter or mask is fitted to received payload samples.
The fixed-window interface starts from zero state and excludes its startup
support. ``process`` instead retains state across consecutive streaming chunks.
Neither interface changes the transmitter or the simulated propagation channel.
"""
from __future__ import annotations

import hashlib
import math

import numpy as np
from scipy import signal


class NarrowbandFilter:
    """80-dB Kaiser FIR with pass edge B/2 and stop edge 0.6 B.

    ``filter_basis(basis, valid, max_lag=0)`` returns the filtered [2,N,2]
    public bank, its fixed validity mask and an auditable metadata dictionary.
    ``filter_observation`` has identical zero-state, causal, same-length
    semantics. Its samples before the returned mask are not receiver evidence.
    ``process``/``reset`` provide a separate persistent 1-D stream state.

    Both arithmetic modes use the same FP32-quantized taps. ``fp64`` is a
    higher-arithmetic reference for those taps, not another filter design.
    """

    def __init__(self, sample_rate, occupied_bandwidth_hz, *, precision="fp32"):
        self.sample_rate = self._positive(sample_rate, "sample rate")
        self.bandwidth = self._positive(occupied_bandwidth_hz, "occupied bandwidth")
        if precision not in ("fp32", "fp64"):
            raise ValueError("precision must be fp32 or fp64")
        self.precision = precision
        self.dtype = np.dtype(np.complex64 if precision == "fp32" else np.complex128)
        self.passband_edge_hz = self.bandwidth / 2.
        self.stopband_edge_hz = self.bandwidth * .6
        if self.stopband_edge_hz >= self.sample_rate / 2:
            raise ValueError("stopband edge must be strictly below Nyquist")
        transition = (self.stopband_edge_hz-self.passband_edge_hz)/(self.sample_rate/2)
        if not math.isfinite(transition) or transition < 1e-6:
            raise ValueError("narrowband filter requires more than 65537 taps")
        count, beta = signal.kaiserord(80., transition)
        count = max(3, int(count) | 1)
        if count > 65537:
            raise ValueError("narrowband filter requires more than 65537 taps")
        taps = signal.firwin(count, (self.passband_edge_hz+self.stopband_edge_hz)/2,
            fs=self.sample_rate, window=("kaiser", beta)).astype(np.float32)
        taps.flags.writeable = False
        self.taps = taps
        self._arithmetic_taps = taps if precision == "fp32" else taps.astype(np.float64)
        self.reset()
        # Dense-grid measurements include the declared boundaries. They are
        # diagnostics of these quantized taps, not continuous-frequency proofs.
        frequencies = np.unique(np.r_[np.linspace(0., self.sample_rate/2, 32769),
                                      self.passband_edge_hz, self.stopband_edge_hz])
        _, response = signal.freqz(taps.astype(np.float64), worN=frequencies,
                                   fs=self.sample_rate)
        magnitude = abs(response)
        passed = magnitude[frequencies <= self.passband_edge_hz]
        stopped = magnitude[frequencies >= self.stopband_edge_hz]
        self._response = dict(grid_points=len(frequencies),
            passband_min_gain=float(passed.min()), passband_max_gain=float(passed.max()),
            passband_max_abs_gain_error_db=float(np.max(abs(20*np.log10(passed)))),
            stopband_max_gain=float(stopped.max()),
            stopband_max_gain_db=float(20*np.log10(stopped.max())),
            scope="dense frequency-grid measurement, including declared edges; not an analytic supremum bound")

    @staticmethod
    def _positive(value, name):
        value = float(value)
        if not math.isfinite(value) or value <= 0:
            raise ValueError(f"{name} must be finite and positive")
        return value

    @property
    def support_samples(self):
        return len(self.taps)-1

    def metadata(self):
        return dict(schema="triad-live-causal-narrowband-v1", sample_rate=self.sample_rate,
            occupied_bandwidth_hz=self.bandwidth, passband_edge_hz=self.passband_edge_hz,
            stopband_edge_hz=self.stopband_edge_hz, design_attenuation_db=80.,
            taps=len(self.taps), taps_sha256=hashlib.sha256(self.taps.astype("<f4").tobytes()).hexdigest(),
            causal_group_delay_samples=self.support_samples/2,
            causal_history_samples=self.support_samples,
            bulk_arithmetic=self.precision, tap_arithmetic="fp32",
            preparation_and_energy_reductions="fp64", response=dict(self._response),
            fitted_to_payload=False, calibrated_admission=False,
            scope="same fixed operator on public templates and physical/model observations; new null gate required")

    def _samples(self, values, *, allow_empty=False):
        values = np.asarray(values)
        if values.ndim != 1 or not np.iscomplexobj(values) or (not allow_empty and not len(values)):
            raise ValueError("a nonempty one-dimensional complex vector is required")
        if not np.isfinite(values).all():
            raise ValueError("finite complex samples required")
        converted = np.asarray(values, dtype=self.dtype)
        if not np.isfinite(converted).all():
            raise ValueError("samples overflow selected arithmetic")
        return converted

    def reset(self):
        self._state = np.zeros(self.support_samples, dtype=self.dtype)

    def process(self, chunk):
        """Causal streaming operation; retains all FIR state between calls."""
        chunk = self._samples(chunk, allow_empty=True)
        if not len(chunk):
            return chunk.copy()
        result, self._state = signal.lfilter(self._arithmetic_taps,
            np.ones(1, dtype=self._arithmetic_taps.dtype), chunk, zi=self._state)
        if not np.isfinite(result).all() or not np.isfinite(self._state).all():
            raise ValueError("nonfinite FIR result/state")
        return np.asarray(result, dtype=self.dtype)

    def filter_observation(self, observation):
        """Stateless causal filtering; does not read or modify ``process`` state."""
        observation = self._samples(observation)
        result = signal.lfilter(self._arithmetic_taps,
            np.ones(1, dtype=self._arithmetic_taps.dtype), observation)
        if not np.isfinite(result).all():
            raise ValueError("nonfinite FIR result")
        return np.asarray(result, dtype=self.dtype)

    def valid_mask(self, valid):
        """Exclude missing preceding support; reject interior erasures explicitly."""
        valid = np.asarray(valid)
        if valid.ndim != 1 or not len(valid) or not np.isin(valid, (0,1)).all():
            raise ValueError("a one-dimensional binary validity mask is required")
        indices = np.flatnonzero(valid)
        if not len(indices) or np.any(np.diff(indices) != 1):
            raise ValueError("narrowband finite-window validity must be nonempty and contiguous")
        start, stop = int(indices[0])+self.support_samples, int(indices[-1])+1
        if start >= stop:
            raise ValueError("no valid samples remain after FIR support erosion")
        result = np.zeros(len(valid), bool)
        result[start:stop] = True
        return result

    def filter_basis(self, basis, valid, *, max_lag=0):
        """Filter all public columns, with complete tail/search support checks."""
        basis = np.asarray(basis)
        if basis.ndim != 3 or basis.shape[0] != 2 or basis.shape[2] != 2 or not np.iscomplexobj(basis):
            raise ValueError("public basis must be complex [2, N, 2]")
        if not np.isfinite(basis).all():
            raise ValueError("finite public basis required")
        if isinstance(max_lag, (bool, np.bool_)) or not isinstance(max_lag, (int, np.integer)) or max_lag < 0:
            raise ValueError("max_lag must be a nonnegative integer")
        if np.asarray(valid).shape != (basis.shape[1],):
            raise ValueError("basis and fixed validity lengths differ")
        usable = self.valid_mask(valid)
        indices = np.flatnonzero(usable)
        lo, hi = int(indices[0]), int(indices[-1])+1
        count = basis.shape[1]
        filtered = np.empty(basis.shape, dtype=self.dtype)
        details = []
        for bit in range(2):
            for column in range(2):
                source = basis[bit,:,column]
                support = np.flatnonzero(source != 0)
                if not len(support):
                    raise ValueError("every declared public basis column must have nonzero support")
                final = int(support[-1])+self.support_samples
                if final+int(max_lag) >= count:
                    raise ValueError("complete public template FIR tail leaves capture at maximum lag")
                result = self.filter_observation(source)
                filtered[bit,:,column] = result
                power = abs(result.astype(np.complex128))**2
                prefix = np.r_[0., np.cumsum(power, dtype=np.float64)]
                total = float(prefix[-1])
                original = float(np.sum(abs(source.astype(np.complex128))**2, dtype=np.float64))
                if total <= 0 or not math.isfinite(total) or not math.isfinite(original):
                    raise ValueError("public basis energy is not representable")
                shifts = np.arange(int(max_lag)+1)
                retained = prefix[np.clip(hi-shifts,0,count)]-prefix[np.clip(lo-shifts,0,count)]
                details.append(dict(bit=bit,column=column,input_first_nonzero=int(support[0]),
                    input_last_nonzero=int(support[-1]),filtered_last_possible_nonzero=final,
                    input_energy=original,filtered_energy=total,
                    nominal_valid_energy=float(retained[0]),
                    nominal_valid_fraction_of_filtered=float(retained[0]/total),
                    minimum_valid_fraction_over_integer_lags=float(retained.min()/total),
                    maximum_valid_fraction_over_integer_lags=float(retained.max()/total),
                    nominal_valid_energy_over_input=float(retained[0]/original)))
        info = self.metadata()
        info.update(samples=count,valid_samples=int(usable.sum()),max_lag=int(max_lag),
                    complete_tail_guard_passed=True,public_template_energy=details)
        return filtered, usable, info
