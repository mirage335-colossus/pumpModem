#!/usr/bin/env python3
"""Public DATA adaptation for the isolated Helper comparison.

This is the existing EmpiricalReceiver likelihood, uniformly mixed over public
epoch templates. It is not an operational admission rule. The whole complex
capture passes through one fixed public causal FIR before DATA cropping and
decimation. Historical profiles retain 18-kHz input and 900-Hz output; the
rate-aware profile explicitly declares both clocks. Caller-supplied templates contain the complete source waveform,
including FHSS ramps, cover and pulse tails, but no channel or arrival truth.

Example: ``HelperDataReceiver(templates, range(-6, 7),
source_offset_seconds=38-waveform_halo, capture_lead_seconds=lead,
capture_rows=rows, arrival_bounds_seconds=(-7.035, 7.035),
delay_seconds=.021, doppler_hz=90, reference_variance=raw_variance *
np.sum(data_fir_taps()**2), frequency_offsets=(-.58, 0, .58))``.
Use ``prepare_observation`` once and ``score_prepared`` on the original and
``with_frequency_offsets`` variants to share filtering and public FFT caches.
"""
from __future__ import annotations

import copy
from collections import OrderedDict
from dataclasses import dataclass, replace
import hashlib
import json
import math
from pathlib import Path
import time
import threading

import numpy as np
from scipy import fft, signal
from scipy.special import logsumexp

from triad_fft_acquire import lag_grid, phase
from triad_live_receiver import EmpiricalReceiver, freeze_calibration

RAW_SAMPLE_RATE = 18000
DATA_SAMPLE_RATE = 900
DECIMATION = 20


def _number(value, name, *, positive=False):
    if isinstance(value, (bool, np.bool_)):
        raise ValueError(f"{name} must be a finite number")
    try:
        result = float(value)
    except (ValueError, TypeError) as error:
        raise ValueError(f"{name} must be a finite number") from error
    if not math.isfinite(result) or (positive and result <= 0):
        raise ValueError(f"{name} must be finite" + (" and positive" if positive else ""))
    return result


def _integer(value, name, minimum=1):
    if isinstance(value, (bool, np.bool_)) or not isinstance(value, (int, np.integer)) or value < minimum:
        raise ValueError(f"{name} must be an integer >= {minimum}")
    return int(value)


def _frequencies(values, profile="legacy", *, source_half_bandwidth_hz=112.5,
                 channel_line_half_width_hz=180., pass_edge_hz=None):
    result = tuple(_number(v, "frequency offset") for v in values)
    maximum = 4096 if profile == "legacy" else 1_000_000
    if not result or len(result) > maximum or len(set(result)) != len(result):
        raise ValueError(f"frequency_offsets must be a nonempty unique bank of at most {maximum} entries")
    if profile not in ("legacy", "wide", "rate-aware"):
        raise ValueError("frontend profile must be legacy, wide or rate-aware")
    # Public nominal support only; complete waveform/filter tails remain present.
    edge = (330 if profile == "legacy" else 400) if pass_edge_hz is None else pass_edge_hz
    if profile == "rate-aware" and pass_edge_hz is None:
        raise ValueError("rate-aware frequency bank requires its declared pass edge")
    if max(map(abs, result)) + source_half_bandwidth_hz + channel_line_half_width_hz > edge:
        raise ValueError("frequency bank exceeds the fixed DATA FIR public passband allowance")
    return result


def _channel_support(doppler_hz, source_half_bandwidth_hz, channel_line_half_width_hz,
                     output_sample_rate=DATA_SAMPLE_RATE):
    doppler = _number(doppler_hz, "doppler_hz")
    source = _number(source_half_bandwidth_hz, "source_half_bandwidth_hz", positive=True)
    lines = 2 * abs(doppler) if channel_line_half_width_hz is None else _number(
        channel_line_half_width_hz, "channel_line_half_width_hz")
    if abs(doppler) >= output_sample_rate / 2 or lines < abs(doppler):
        raise ValueError("Doppler must be below DATA Nyquist and channel line support must cover it")
    return doppler, source, lines


def frontend_plan(profile="legacy", *, sample_rate=RAW_SAMPLE_RATE,
                  output_sample_rate=DATA_SAMPLE_RATE, pass_edge_hz=None,
                  stop_edge_hz=None):
    """Public sample clock and causal anti-alias design; no capture tuning.

    The historical profiles keep their exact clocks and coefficients. The
    rate-aware profile accepts a frozen public pass/stop pair independent of
    integer decimation. Without a pair it uses 0.4/0.49 of output rate, suitable
    only when no nearer rejected band is declared. The Helper study supplies
    its public DATA/Helper separation pair. Finite skirts are retained.
    """
    raw = _integer(sample_rate, "sample_rate")
    output = _integer(output_sample_rate, "output_sample_rate")
    if raw % output or output > raw:
        raise ValueError("output sample rate must be an integer divisor of input sample rate")
    if profile in ("legacy", "wide"):
        if pass_edge_hz is not None or stop_edge_hz is not None:
            raise ValueError("explicit FIR edges require rate-aware profile")
        if raw != RAW_SAMPLE_RATE or output != DATA_SAMPLE_RATE:
            raise ValueError("legacy/wide frontend requires 18000-Hz input and 900-Hz output")
        passed, stopped = (330., 430.) if profile == "legacy" else (400., 440.)
    elif profile == "rate-aware":
        if (pass_edge_hz is None) != (stop_edge_hz is None):
            raise ValueError("explicit FIR pass/stop edges must be supplied together")
        passed = .4 * output if pass_edge_hz is None else _number(pass_edge_hz, "pass_edge_hz", positive=True)
        stopped = .49 * output if stop_edge_hz is None else _number(stop_edge_hz, "stop_edge_hz", positive=True)
        if not 0 < passed < stopped < output / 2:
            raise ValueError("FIR requires 0 < pass edge < stop edge < output Nyquist")
    else:
        raise ValueError("frontend profile must be legacy, wide or rate-aware")
    return dict(profile=profile, input_sample_rate=raw, output_sample_rate=output,
                decimation=raw // output, pass_edge_hz=passed, stop_edge_hz=stopped,
                design_attenuation_db=96.)


def rate_aware_frontend(sample_rate, required_nominal_edge_hz, *, rejected_bands_hz=()):
    """Select the cheapest integer-decimated clock containing a public band.

    This is a necessary nominal-support planner, not an interference or
    sensitivity certificate. It never adjusts DATA IF or Helper placement.
    """
    raw = _integer(sample_rate, "sample_rate")
    edge = _number(required_nominal_edge_hz, "required_nominal_edge_hz", positive=True)
    if edge > .4 * raw:
        raise ValueError("nominal DATA envelope exceeds rate-aware input passband")
    divisors = set()
    for divisor in range(1, math.isqrt(raw) + 1):
        if raw % divisor == 0:
            divisors.update((divisor, raw // divisor))
    output = min(rate for rate in divisors if .4 * rate >= edge)
    bands = []
    nearest = math.inf
    for band in rejected_bands_hz:
        if len(band) != 2:
            raise ValueError("rejected bands require two endpoints")
        lower, upper = (_number(x, "rejected band endpoint") for x in band)
        if lower > upper:
            raise ValueError("rejected band endpoints must be increasing")
        distance = 0. if lower <= 0 <= upper else min(abs(lower), abs(upper))
        nearest = min(nearest, distance)
        bands.append([lower, upper])
        if len(bands) > 32:
            raise ValueError("at most 32 public rejected bands are supported")
    stopped = min(.49 * output, nearest)
    if stopped <= edge:
        raise ValueError("fixed DATA frontend public passband overlaps a declared Helper/rejected band")
    result = frontend_plan("rate-aware", sample_rate=raw, output_sample_rate=output,
                           pass_edge_hz=edge, stop_edge_hz=stopped)
    result["required_nominal_edge_hz"] = edge
    result["rejected_bands_hz"] = bands
    result["nearest_rejected_band_edge_hz"] = nearest if bands else None
    result["scope"] = "Public nominal envelope only; complete waveform skirts and causal filter tails remain."
    return result


def data_fir_taps(profile="legacy", *, sample_rate=RAW_SAMPLE_RATE,
                  output_sample_rate=DATA_SAMPLE_RATE, pass_edge_hz=None,
                  stop_edge_hz=None):
    """Public real causal lowpass, with complete finite impulse response.

    Kaiser design targets 96 dB, with margin over the required 80-dB stopband.
    No automatic frequency shift, energy normalization or observation tuning.
    """
    plan = frontend_plan(profile, sample_rate=sample_rate, output_sample_rate=output_sample_rate,
                         pass_edge_hz=pass_edge_hz, stop_edge_hz=stop_edge_hz)
    passed, stopped = plan["pass_edge_hz"], plan["stop_edge_hz"]
    count, beta = signal.kaiserord(96., (stopped - passed) / (sample_rate / 2))
    count += 1 - count % 2
    if count > 1_000_001:
        raise ValueError("frontend FIR exceeds the explicit 1000001-tap design budget")
    return signal.firwin(count, (passed + stopped) / 2, window=("kaiser", beta), fs=sample_rate)


def clock_warp(values, clock_ppm):
    """Finite source-coordinate warp, with explicit cubic interpolation support.

    This operates at the input sample clock before filtering. Source
    zero stays fixed. A 0-ppm warp is exact. Four-point Lagrange interpolation
    is an explicit approximation, whose source-specific loss needs validation.
    """
    values = np.asarray(values)
    rate = 1 + _number(clock_ppm, "clock ppm") * 1e-6
    if values.ndim != 1 or values.dtype.kind != "c" or not np.isfinite(values).all() or rate <= 0:
        raise ValueError("clock warp requires finite complex samples and positive rate")
    if rate == 1:
        return np.asarray(values, np.complex128)
    rows = math.ceil((len(values) + 2) / rate) + 2
    output = np.zeros(rows, np.complex128)
    for first in range(0, rows, 32768):
        past = min(rows, first + 32768)
        positions = np.arange(first, past, dtype=np.float64) * rate
        integer = np.floor(positions).astype(np.int64)
        fraction = positions - integer
        for offset in (-1, 0, 1, 2):
            ids = integer + offset
            valid = (ids >= 0) & (ids < len(values))
            weight = np.ones(len(ids))
            for other in (-1, 0, 1, 2):
                if other != offset:
                    weight *= (fraction - other) / (offset - other)
            output[first:past][valid] += values[ids[valid]] * weight[valid]
    return output


def fractional_delay(values, delay_samples):
    """Bounded causal four-point cubic Lagrange delay, exact at integer delays.

    Returns the complete finite support. For fractional d=m+u, output[n] is
    sum_k L_k(u)*input[n-m-k], k=0..3, with zero outside the declared source.
    This is an interpolation model, not an exact bandlimited reconstruction.
    """
    values = np.asarray(values)
    if values.ndim != 1 or values.dtype.kind != "c" or not np.isfinite(values).all():
        raise ValueError("delay input must be a finite complex vector")
    delay_samples = _number(delay_samples, "delay_samples")
    if delay_samples < 0:
        raise ValueError("delay_samples must be nonnegative")
    nearest = round(delay_samples)
    if abs(delay_samples - nearest) <= 1e-9:
        result = np.zeros(len(values) + nearest, np.complex128)
        result[nearest:] = values
        return result
    integer = math.floor(delay_samples)
    fraction = delay_samples - integer
    result = np.zeros(len(values) + integer + 3, np.complex128)
    for k in range(4):
        weight = math.prod((fraction - j) / (k - j) for j in range(4) if j != k)
        result[integer + k:integer + k + len(values)] += weight * values
    return result


@dataclass(frozen=True)
class PreparedDataObservation:
    """Owned filtered DATA crop; reusable only with the matching public FIR."""
    samples: np.ndarray
    preprocessing_id: str
    filtering_seconds: float
    validity_id: str = "all-valid"


class HelperDataReceiver:
    """Cached epoch bank around the actual maintained empirical FFT receiver.

    ``raw_templates`` has shape [epochs, two bits, complete source samples].
    ``source_offset_seconds`` maps source sample zero onto the transmit calendar
    (normally 38 seconds minus source halo), independently of epoch addressing.
    ``arrival_bounds_seconds`` is a public prior; the lower bound is the one
    fractional anchor. The lattice is integer output samples and includes the
    ceil-rounded upper endpoint. The default scores every output timing sample;
    a coarser explicit lag_step remains available for historical reproduction.
    No capture-derived alignment or truth is read.
    ``reference_variance`` is positive public per-complex-sample variance AFTER
    the FIR. White raw noise gives raw_variance*sum(taps**2); filtering colors
    noise, so only a separately calibrated empirical gate may admit a bit.
    ``capture_validity`` freezes known missing ADC rows. Every output whose
    complete FIR support touches a gap is excluded from the likelihood. Masks
    are receiver identity inputs, not inferred from payload or sample values.
    ``source_half_bandwidth_hz`` and ``channel_line_half_width_hz`` declare the
    public nominal spectrum for support checks. The latter defaults to 2*|D|
    for the historical Doppler-plus-fast-modulation envelope. They never clip
    samples or alter the two-ray scoring model.
    """
    def __init__(self, raw_templates, epoch_offsets, *, source_offset_seconds,
                 capture_lead_seconds, capture_rows, arrival_bounds_seconds,
                 delay_seconds, doppler_hz, reference_variance,
                 frequency_offsets=(0.,), sample_rate=RAW_SAMPLE_RATE,
                 output_sample_rate=DATA_SAMPLE_RATE, lag_step=1,
                 precision="fp32", device="cpu",
                 memory_limit_bytes=512 * 1024 * 1024,
                 fft_backend="legacy", frontend_profile="legacy",
                 clock_ppm_values=(0.,), doppler_scale_values=(1.,),
                 frequency_batch_size=8, max_cached_hypotheses=1,
                 capture_validity=None, source_half_bandwidth_hz=112.5,
                 channel_line_half_width_hz=None, pass_edge_hz=None, stop_edge_hz=None):
        self._configure_frontend(frontend_profile, sample_rate, output_sample_rate,
                                 pass_edge_hz, stop_edge_hz)
        if fft_backend == "batched":
            self._init_batched(raw_templates, epoch_offsets,
                source_offset_seconds=source_offset_seconds, capture_lead_seconds=capture_lead_seconds,
                capture_rows=capture_rows, arrival_bounds_seconds=arrival_bounds_seconds,
                delay_seconds=delay_seconds, doppler_hz=doppler_hz, reference_variance=reference_variance,
                frequency_offsets=frequency_offsets, sample_rate=sample_rate, output_sample_rate=output_sample_rate,
                lag_step=lag_step, precision=precision, device=device, memory_limit_bytes=memory_limit_bytes,
                frontend_profile=frontend_profile, clock_ppm_values=clock_ppm_values,
                doppler_scale_values=doppler_scale_values, frequency_batch_size=frequency_batch_size,
                max_cached_hypotheses=max_cached_hypotheses, capture_validity=capture_validity,
                source_half_bandwidth_hz=source_half_bandwidth_hz,
                channel_line_half_width_hz=channel_line_half_width_hz)
            return
        if fft_backend != "legacy" or tuple(clock_ppm_values) != (0.,) or tuple(doppler_scale_values) != (1.,):
            raise ValueError("clock/rate hypotheses require fft_backend='batched'")
        self.fft_backend = "legacy"
        self.frontend_profile = frontend_profile
        started = time.perf_counter()
        source = np.asarray(raw_templates)
        if source.ndim != 3 or source.shape[1] != 2 or source.shape[2] <= 4 or source.dtype.kind != "c":
            raise ValueError("raw_templates must have complex shape [E, 2, N>4]")
        epochs = tuple(epoch_offsets)
        if len(epochs) != source.shape[0] or not 1 <= len(epochs) <= 13:
            raise ValueError("provide one to thirteen epoch offsets matching raw_templates")
        if any(isinstance(e, (bool, np.bool_)) or not isinstance(e, (int, np.integer)) or abs(e) > 6 for e in epochs):
            raise ValueError("epoch offsets must be exact integers in [-6, 6]")
        self.epoch_offsets = tuple(map(int, epochs))
        if len(set(self.epoch_offsets)) != len(epochs):
            raise ValueError("epoch offsets must be unique")
        self.capture_rows = _integer(capture_rows, "capture_rows")
        self.memory_limit_bytes = _integer(memory_limit_bytes, "memory_limit_bytes")
        self.reference_variance = _number(reference_variance, "reference_variance", positive=True)
        self.source_offset_seconds = _number(source_offset_seconds, "source_offset_seconds")
        self.capture_lead_seconds = _number(capture_lead_seconds, "capture_lead_seconds")
        if self.capture_lead_seconds < 0:
            raise ValueError("capture_lead_seconds must be nonnegative")
        if len(arrival_bounds_seconds) != 2:
            raise ValueError("arrival bounds must contain two public endpoints")
        lower, upper = (_number(v, "arrival bound") for v in arrival_bounds_seconds)
        if upper < lower:
            raise ValueError("arrival bounds must be increasing")
        self.arrival_bounds_seconds = (lower, upper)
        self.delay_seconds = _number(delay_seconds, "delay_seconds")
        self.doppler_hz, self.source_half_bandwidth_hz, self.channel_line_half_width_hz = _channel_support(
            doppler_hz, source_half_bandwidth_hz, channel_line_half_width_hz, self.output_sample_rate)
        if self.delay_seconds < 0:
            raise ValueError("public channel requires nonnegative delay")
        self.frequency_offsets = _frequencies(frequency_offsets, self.frontend_profile,
            source_half_bandwidth_hz=self.source_half_bandwidth_hz,
            channel_line_half_width_hz=self.channel_line_half_width_hz, pass_edge_hz=self.pass_edge_hz)
        self.lags = lag_grid(math.ceil((upper - lower) * self.output_sample_rate),
                             _integer(lag_step, "lag_step"))
        if len(self.lags) * len(self.frequency_offsets) * len(epochs) > 1_000_000:
            raise ValueError("complete epoch/timing/frequency bank exceeds 1000000 groups")
        guard = (len(self.taps) - 1) / self.sample_rate
        earliest = self.capture_lead_seconds + self.source_offset_seconds + lower
        if earliest < 0:
            raise ValueError("earliest public source arrival precedes the capture")
        # Start on the capture's original decimation phase. A full FIR-history
        # guard is retained even though the observation is filtered before crop.
        self.crop_start_raw = max(0, math.floor((earliest - guard) * self.sample_rate / self.decimation) * self.decimation)
        self.crop_start_output = self.crop_start_raw // self.decimation
        self.samples = (self.capture_rows - 1) // self.decimation + 1 - self.crop_start_output
        if self.samples <= 4 or self.lags[-1] >= self.samples:
            raise ValueError("capture is too short for the public DATA arrival bank")
        self._configure_validity(capture_validity)
        anchor_samples = earliest * self.sample_rate - self.crop_start_raw
        delay_samples = self.delay_seconds * self.sample_rate
        # Worst-case cubic tail, complete causal FIR tail, and largest lag.
        support_stop = math.ceil(anchor_samples + delay_samples) + source.shape[2] + 3 + len(self.taps) - 1
        if (support_stop - 1) // self.decimation + int(self.lags[-1]) >= self.samples:
            raise ValueError("capture does not retain complete source/channel/FIR tail at every public lag")
        nfft = fft.next_fast_len(2 * self.samples - 1)
        per_workspace = self.samples * (81 + 320) + nfft * 320 + len(self.lags) * 320 + 65536
        if device.startswith("cuda"):
            per_workspace += nfft * 192 + self.samples * 96
        score_reserve = min(64, len(self.lags)) * 128 * 1024 + 1024 * 1024
        # Conservative explicit arrays only, not Python/Torch/process RSS.
        self.frontend_array_bound_bytes = max(source.nbytes + support_stop * 160,
                                              self.capture_rows * 64) + self.samples * 64 + self.taps.nbytes + self.validity_array_bound_bytes + self._filter_scratch_bytes(max(self.capture_rows, support_stop))
        estimate = self.frontend_array_bound_bytes + len(epochs) * per_workspace + score_reserve
        if estimate >= self.memory_limit_bytes:
            raise MemoryError("complete epoch bank and frontend exceed explicit memory limit")
        if not np.isfinite(source).all():
            raise ValueError("raw_templates must be finite")
        passed, stopped = self.pass_edge_hz, self.stop_edge_hz
        self.preprocessing = dict(
            format="triad-helper-data-frontend-v1", input_sample_rate=self.sample_rate,
            output_sample_rate=self.output_sample_rate, decimation=self.decimation,
            profile=self.frontend_profile, pass_edge_hz=passed, stop_edge_hz=stopped, design_attenuation_db=96.,
            tap_count=len(self.taps), filter_tail_seconds=guard,
            filter_group_delay_seconds=guard / 2,
            noise_variance_gain=float(np.sum(self.taps**2)),
            crop_start_raw_sample=self.crop_start_raw, crop_output_samples=self.samples,
            capture_rows=self.capture_rows, source_offset_seconds=self.source_offset_seconds,
            capture_lead_seconds=self.capture_lead_seconds,
            fractional_anchor_arrival_seconds=lower,
            arrival_upper_lattice_seconds=lower + int(self.lags[-1]) / self.output_sample_rate,
            delay_seconds=self.delay_seconds, doppler_hz=self.doppler_hz,
            source_half_bandwidth_hz=self.source_half_bandwidth_hz,
            channel_line_half_width_hz=self.channel_line_half_width_hz,
            delay_raw_samples=delay_samples,
            delay_is_integer=abs(delay_samples - round(delay_samples)) <= 1e-9,
            anchor_raw_samples_from_crop=anchor_samples,
            delay_interpolation="integer exact; fractional causal four-point cubic Lagrange with full three-sample tail",
            observation_filtering="continuous whole capture before crop; output restricted to actually observed capture extent",
            cfo_order="FIR first, existing FFT CFO hypotheses second; finite-FIR noncommutation remains a declared model mismatch",
            erasure_support=self._validity_metadata)
        if self.frontend_profile == "rate-aware":
            self.preprocessing["filter_engine"] = self._filter_metadata()
        identity = hashlib.sha256(json.dumps(self.preprocessing, sort_keys=True).encode())
        identity.update(self.taps.tobytes())
        self.preprocessing_id = identity.hexdigest()
        self._receivers = []
        self.template_energy = []
        child_limit = self.memory_limit_bytes - self.frontend_array_bound_bytes - (len(epochs) - 1) * per_workspace
        for epoch, two_bits in zip(self.epoch_offsets, source):
            basis = np.zeros((2, self.samples, 2), np.complex128)
            energy = []
            for bit in (0, 1):
                bit_energy = []
                for column, offset in enumerate((anchor_samples, anchor_samples + delay_samples)):
                    wide = fractional_delay(two_bits[bit], offset)
                    if column:
                        physical_times = (np.arange(len(wide), dtype=np.longdouble) + self.crop_start_raw) / self.sample_rate - self.capture_lead_seconds
                        wide *= phase(self.doppler_hz, physical_times)
                    filtered = self._filter(wide)
                    basis[bit, :len(filtered), column] = filtered
                    before = float(np.vdot(wide, wide).real)
                    after = float(np.vdot(filtered, filtered).real) * self.decimation
                    bit_energy.append(dict(wide_energy=before, retained_equivalent_wide_energy=after,
                                           ratio=after / before if before else None))
                energy.append(bit_energy)
            self.template_energy.append(energy)
            self._receivers.append(EmpiricalReceiver(
                basis, self.output_sample_rate, self.lags,
                key=dict(arrival_seconds=lower, epoch_offset_seconds=epoch),
                frequencies=[0., self.doppler_hz, 0., self.doppler_hz],
                valid=self.output_validity,
                reference_variance=self.reference_variance, frequency_offsets=self.frequency_offsets,
                precision=precision, device=device, memory_limit_bytes=child_limit,
                preprocessing_identity=self.preprocessing_id))
        self.explicit_array_bound_bytes = (self.frontend_array_bound_bytes +
            sum(r._workspace.array_bound for r in self._receivers) + score_reserve)
        if self.explicit_array_bound_bytes >= self.memory_limit_bytes:
            raise MemoryError("prepared complete epoch bank exceeds explicit memory limit")
        self.taps.flags.writeable = False
        self.lags.flags.writeable = False
        self.preparation_seconds = time.perf_counter() - started
        self._bind_identity()

    def _configure_frontend(self, profile, sample_rate, output_sample_rate,
                             pass_edge_hz, stop_edge_hz):
        plan = frontend_plan(profile, sample_rate=sample_rate, output_sample_rate=output_sample_rate,
                             pass_edge_hz=pass_edge_hz, stop_edge_hz=stop_edge_hz)
        self.sample_rate = plan["input_sample_rate"]
        self.output_sample_rate = plan["output_sample_rate"]
        self.decimation = plan["decimation"]
        self.pass_edge_hz = plan["pass_edge_hz"]
        self.stop_edge_hz = plan["stop_edge_hz"]
        self.taps = data_fir_taps(profile, sample_rate=self.sample_rate,
                                  output_sample_rate=self.output_sample_rate,
                                  pass_edge_hz=pass_edge_hz, stop_edge_hz=stop_edge_hz)
        self._overlap_add_eligible = profile == "rate-aware" and len(self.taps) >= 257

    def _filter_metadata(self):
        return dict(policy="public-shape-polyphase-or-scipy-overlap-add-v1",
            overlap_add_minimum_taps=257, overlap_add_minimum_input_to_taps_ratio=4,
            scope="Rate-aware only; complete causal full convolution then original [::decimation] phase; historical profiles retain upfirdn.",
            scratch_accounting="Additional 96*(input_rows+tap_count) bytes for overlap-add arrays; library/runtime overhead is separate, not an RSS bound.")

    def _filter_scratch_bytes(self, rows):
        return 96 * (int(rows) + len(self.taps)) if self._overlap_add_eligible and rows >= 4 * len(self.taps) else 0

    def _filter(self, values):
        # A long narrow-transition FIR can require billions of direct MACs.
        # The public shape chooses overlap-add, independently of sample values.
        # Retain the complete finite tail and original decimation phase. Copy
        # the decimated result so it cannot retain the full-rate temporary.
        if self._overlap_add_eligible and len(values) >= 4 * len(self.taps):
            return np.array(signal.oaconvolve(values, self.taps, mode="full")[::self.decimation],
                            dtype=np.complex128, copy=True)
        return signal.upfirdn(self.taps, values, down=self.decimation)

    @staticmethod
    def _raw_validity(values, rows):
        mask = np.asarray(values)
        if mask.shape != (rows,) or mask.dtype.kind not in "bui" or np.any((mask != 0) & (mask != 1)):
            raise ValueError("capture validity must be a binary vector of capture_rows samples")
        return mask

    def _configure_validity(self, capture_validity):
        """Freeze known ADC gaps and conservatively propagate complete FIR support.

        Samples outside the finite capture before index zero are known source
        zeros, not missing ADC rows. Only actually observed output rows are used.
        A prefix count avoids a convolution per mask or a per-tap Python loop.
        """
        raw = None if capture_validity is None else self._raw_validity(capture_validity, self.capture_rows)
        self.capture_validity = self.output_validity = None
        self.validity_id = "all-valid"
        self.validity_array_bound_bytes = 0
        raw_lost = output_lost = 0
        raw_digest = output_digest = "all-valid"
        if raw is not None and not np.all(raw == 1):
            whole_rows = (self.capture_rows - 1) // self.decimation + 1
            # Owned masks plus simultaneous prefix/index/count construction.
            self.validity_array_bound_bytes = self.capture_rows * 10 + 8 + whole_rows * 40 + self.samples
            if self.validity_array_bound_bytes >= self.memory_limit_bytes:
                raise MemoryError("known-erasure masks and prefix workspace exceed memory limit")
            raw = np.array(raw, dtype=np.uint8, copy=True)
            missing = np.empty(self.capture_rows + 1, dtype=np.int64)
            missing[0] = 0
            np.cumsum(raw == 0, dtype=np.int64, out=missing[1:])
            indices = np.arange(self.samples, dtype=np.int64) * self.decimation + self.crop_start_raw
            earliest = np.maximum(0, indices - len(self.taps) + 1)
            output = np.asarray(missing[indices + 1] == missing[earliest], dtype=np.uint8)
            raw_lost = int(missing[-1])
            output_lost = self.samples - int(output.sum())
            if self.samples - output_lost <= 4:
                raise ValueError("known-erasure mask leaves insufficient valid DATA outputs")
            raw_digest = hashlib.sha256(raw.tobytes()).hexdigest()
            output_digest = hashlib.sha256(output.tobytes()).hexdigest()
            self.validity_id = hashlib.sha256((raw_digest + ":" + output_digest).encode()).hexdigest()
            raw.flags.writeable = output.flags.writeable = False
            self.capture_validity, self.output_validity = raw, output
        self._validity_metadata = dict(
            policy="fixed-known-ADC-mask-causal-FIR-support-v1", validity_id=self.validity_id,
            raw_mask_sha256=raw_digest, output_mask_sha256=output_digest,
            raw_missing_rows=raw_lost, output_missing_rows=output_lost,
            output_valid_rows=self.samples - output_lost,
            fir_support_raw_samples=len(self.taps),
            support_rule=f"Output j is valid only when every captured raw row in [max(0,{self.decimation}*j-L+1),{self.decimation}*j] is valid; j uses the original capture decimation phase.",
            missing_samples="Zero-filled before continuous FIR; affected output rows are excluded from correlations, Grams and residual energy.",
            calibration="Fixed mask is part of receiver identity; changed or random mask policies require matching full-procedure calibration.")

    def _bind_identity(self):
        if self.fft_backend == "batched":
            identity = hashlib.sha256()
            for name in ("triad_helper_receiver.py", "triad_helper_fft.py", "triad_fft_acquire.py",
                         "triad_live_receiver.py", "triad_group_acquire.py", "triad_gaussian_window.py",
                         "triad_noise_likelihood.py", "triad_gaussian_state.py", "triad_state_workspace.py"):
                identity.update((Path(__file__).parent / name).read_bytes())
            identity.update(json.dumps(dict(preprocessing=self.preprocessing_id,
                epochs=self.epoch_offsets, frequency_offsets=self.frequency_offsets,
                clocks=self.clock_ppm_values, doppler_scales=self.doppler_scale_values,
                source_sha256=self._source_hash, precision=self._precision, device=self._device,
                reference_variance=self.reference_variance,
                lag_samples=self.lags.tolist(), batch_size=self._batch_size), sort_keys=True).encode())
            self.receiver_id = identity.hexdigest()
            return
        identity = hashlib.sha256()
        identity.update(Path(__file__).read_bytes())
        identity.update(json.dumps(dict(preprocessing=self.preprocessing_id,
            epochs=self.epoch_offsets, frequency_offsets=self.frequency_offsets,
            epoch_receiver_ids=[r.receiver_id for r in self._receivers]), sort_keys=True).encode())
        self.receiver_id = identity.hexdigest()

    def with_frequency_offsets(self, frequency_offsets):
        """Reuse all epoch templates/Grams/FFTs; change only the public CFO bank.

        Instance scores share the child workspace locks. The returned identity
        changes, requiring a separate empirical calibration for the new bank.
        """
        if self.fft_backend == "batched":
            return self.with_hypotheses(frequency_offsets=frequency_offsets)
        result = copy.copy(self)
        result.frequency_offsets = _frequencies(frequency_offsets, self.frontend_profile,
            source_half_bandwidth_hz=self.source_half_bandwidth_hz,
            channel_line_half_width_hz=self.channel_line_half_width_hz, pass_edge_hz=self.pass_edge_hz)
        if len(self.lags) * len(result.frequency_offsets) * len(self.epoch_offsets) > 1_000_000:
            raise ValueError("complete epoch/timing/frequency bank exceeds 1000000 groups")
        result._receivers = []
        for receiver in self._receivers:
            child = copy.copy(receiver)
            child._config = replace(receiver._config, frequency_offsets_hz=result.frequency_offsets)
            result._receivers.append(child)
        result._bind_identity()
        return result

    def prepare_observation(self, raw_observation, *, valid=None):
        """Filter once with the frozen known ADC mask and complete FIR state."""
        raw = np.asarray(raw_observation)
        if raw.shape != (self.capture_rows,) or raw.dtype.kind != "c":
            raise ValueError("raw_observation must be a complex vector of capture_rows samples")
        if valid is not None:
            valid = self._raw_validity(valid, self.capture_rows)
            if ((self.capture_validity is None and not np.all(valid == 1)) or
                    (self.capture_validity is not None and not np.array_equal(valid, self.capture_validity))):
                raise ValueError("incoming validity differs from the fixed capture mask")
        started = time.perf_counter()
        if self.capture_validity is not None:
            raw = np.array(raw, copy=True)
            raw[self.capture_validity == 0] = 0
        if not np.isfinite(raw).all():
            raise ValueError("valid raw_observation rows must be finite complex samples")
        whole = self._filter(raw)
        cropped = np.array(whole[self.crop_start_output:self.crop_start_output + self.samples], dtype=np.complex128, copy=True)
        if self.output_validity is not None:
            cropped[self.output_validity == 0] = 0
        cropped.flags.writeable = False
        return PreparedDataObservation(cropped, self.preprocessing_id, time.perf_counter() - started, self.validity_id)

    def score(self, raw_observation, *, valid=None):
        return self.score_prepared(self.prepare_observation(raw_observation, valid=valid))

    def score_prepared(self, prepared):
        """Uniform epoch log-mean-exp; presence is max of the two bit mixtures."""
        if self.fft_backend == "batched":
            return self._score_batched(prepared)
        if (not isinstance(prepared, PreparedDataObservation) or prepared.preprocessing_id != self.preprocessing_id or
                prepared.validity_id != self.validity_id):
            raise ValueError("prepared observation has a different public preprocessing identity")
        if prepared.samples.shape != (self.samples,) or prepared.samples.flags.writeable:
            raise ValueError("prepared observation must retain its owned immutable complete DATA crop")
        started = time.perf_counter()
        outcomes = [receiver.score(prepared.samples, retain_group_scores=False) for receiver in self._receivers]
        epoch_scores = np.asarray([outcome["bit_log_likelihoods"] for outcome in outcomes])
        scores = logsumexp(epoch_scores, axis=0) - math.log(len(outcomes))
        winner = int(np.argmax(scores))
        best_epoch = int(np.argmax(epoch_scores[:, winner]))
        best = outcomes[best_epoch]
        # The existing compact result exposes timing/CFO for its local winner
        # only. Never mislabel that diagnostic as the aggregate winning bit.
        parameters = best["best_group_public_parameters"] if best["bit"] == winner else None
        return dict(format="triad-helper-data-working-likelihood-v1", receiver_id=self.receiver_id,
            gate_id=self.receiver_id, bit=winner, score=float(scores[winner]),
            working_presence=float(scores[winner]), bit_log_likelihoods=scores.tolist(),
            epoch_bit_log_likelihoods=epoch_scores.tolist(),
            opposite_bit_margin=float(scores[winner] - scores[1 - winner]),
            best_epoch_offset_seconds=self.epoch_offsets[best_epoch],
            best_group_public_parameters=parameters,
            best_group_scope="epoch maximizes winning-bit epoch mixture; timing/CFO are that epoch's local-winner diagnostic, unavailable on bit disagreement; never used for selection",
            geometry=dict(epoch_offsets=list(self.epoch_offsets), lag_samples=self.lags.tolist(),
                frequency_offsets_hz=list(self.frequency_offsets), samples=self.samples,
                effective_group_count=len(self.epoch_offsets) * len(self.lags) * len(self.frequency_offsets),
                mixture="uniform over every declared epoch, timing and CFO group; logsumexp minus log epoch count"),
            preprocessing=self.preprocessing, template_energy=self.template_energy,
            calibration=dict(analytic_admission_validated=False, empirical_gate_required=True,
                scope="working colored-noise statistic only; independent matched empirical null calibration required",
                presence_statistic="max of the two epoch-marginalized bit log likelihoods"),
            arithmetic=best["arithmetic"],
            timing=dict(total_preparation_seconds=self.preparation_seconds,
                whole_capture_filter_seconds=prepared.filtering_seconds,
                epoch_receiver_seconds=sum(o["timing"]["total_receiver_seconds"] for o in outcomes),
                total_score_prepared_seconds=time.perf_counter() - started,
                shared_preparation="with_frequency_offsets shares all public epoch FFT/Gram workspaces; score_prepared reuses the same filtered capture"),
            resources=dict(explicit_array_bound_bytes=self.explicit_array_bound_bytes,
                memory_limit_bytes=self.memory_limit_bytes, retained_group_scores=False,
                scope="conservative explicit frontend/epoch-cache/score arrays; excludes runtime/allocator overhead, not process RSS"),
            limits=["Static direct plus delayed +Doppler columns; selected selective-AM channel remains a model mismatch.",
                "One fractional anchor and integer output-sample lag lattice; continuous timing and clock mismatch unqualified.",
                "Finite FHSS gates have noncompact spectral skirts; FIR loss is reported per public template/column.",
                "Whole-capture filtering may retain Helper leakage; matched Helper-only and null controls are required.",
                "CFO rotation follows FIR filtering; passband ripple and noncommutation are model approximations.",
                "The existing residual normalizer rejects unresolved zero/near-zero residuals; no fallback is added.",
                "No analytic false-admission, physical completion or ordinary-receiver integration claim."])

    def _init_batched(self, raw_templates, epoch_offsets, **options):
        started = time.perf_counter()
        source = np.asarray(raw_templates)
        epochs = tuple(epoch_offsets)
        if (source.ndim != 3 or source.shape[1] != 2 or source.shape[2] <= 4 or
                source.dtype.kind != "c" or not np.isfinite(source).all()):
            raise ValueError("raw_templates must have finite complex shape [E,2,N>4]")
        if (len(epochs) != len(source) or not 1 <= len(epochs) <= 13 or
                any(isinstance(e, (bool, np.bool_)) or not isinstance(e, (int, np.integer)) or abs(e) > 6 for e in epochs) or
                len(set(epochs)) != len(epochs)):
            raise ValueError("provide unique integer epoch offsets in [-6,6]")
        self.fft_backend = "batched"
        self.frontend_profile = options["frontend_profile"]
        self.epoch_offsets = tuple(map(int, epochs))
        self.clock_ppm_values = self._real_bank(options["clock_ppm_values"], "clock ppm", -10000, 10000)
        self.doppler_scale_values = self._real_bank(options["doppler_scale_values"], "Doppler scale", .99, 1.01)
        self.capture_rows = _integer(options["capture_rows"], "capture_rows")
        self.memory_limit_bytes = _integer(options["memory_limit_bytes"], "memory_limit_bytes")
        self.reference_variance = _number(options["reference_variance"], "reference_variance", positive=True)
        self.source_offset_seconds = _number(options["source_offset_seconds"], "source_offset_seconds")
        self.capture_lead_seconds = _number(options["capture_lead_seconds"], "capture_lead_seconds")
        self.delay_seconds = _number(options["delay_seconds"], "delay_seconds")
        self.doppler_hz, self.source_half_bandwidth_hz, self.channel_line_half_width_hz = _channel_support(
            options["doppler_hz"], options["source_half_bandwidth_hz"], options["channel_line_half_width_hz"], self.output_sample_rate)
        self.frequency_offsets = _frequencies(options["frequency_offsets"], self.frontend_profile,
            source_half_bandwidth_hz=self.source_half_bandwidth_hz,
            channel_line_half_width_hz=self.channel_line_half_width_hz, pass_edge_hz=self.pass_edge_hz)
        bounds = options["arrival_bounds_seconds"]
        if len(bounds) != 2:
            raise ValueError("arrival bounds must contain two public endpoints")
        self.arrival_bounds_seconds = tuple(_number(v, "arrival bound") for v in bounds)
        lower, upper = self.arrival_bounds_seconds
        if upper < lower or self.capture_lead_seconds < 0 or self.delay_seconds < 0:
            raise ValueError("invalid public arrival/delay/Doppler geometry")
        self._precision, self._device = options["precision"], options["device"]
        if self._precision not in ("fp32", "fp64") or (self._device != "cpu" and not self._device.startswith("cuda")):
            raise ValueError("unsupported bulk precision/device")
        self._batch_size = _integer(options["frequency_batch_size"], "frequency_batch_size")
        self.requested_cache_capacity = _integer(options["max_cached_hypotheses"], "max_cached_hypotheses")
        # There can never be more resident workspace keys than the public
        # epoch × clock × RX-rate Cartesian bank. Subset variants share this
        # cache and cannot introduce keys outside that original bank, so keep
        # this effective capacity unchanged on them (even when a subset is
        # scored). Preserve the requested value for diagnostics.
        self.reachable_cache_key_count = (len(self.epoch_offsets) * len(self.clock_ppm_values) *
                                          len(self.doppler_scale_values))
        self._cache_capacity = min(self.requested_cache_capacity, self.reachable_cache_key_count)
        self._cache = OrderedDict()
        self._cache_lock = threading.RLock()
        self._diagnostic_limit = 1024
        if source.size * 16 >= self.memory_limit_bytes:
            raise MemoryError("public source templates exceed memory limit")
        self._source = np.array(source, np.complex128, copy=True)
        self._source.flags.writeable = False
        self._source_hash = hashlib.sha256(memoryview(self._source).cast("B")).hexdigest()
        scales = [1 + q * 1e-6 for q in self.clock_ppm_values]
        earliest = min((self.capture_lead_seconds + lower) / r + self.source_offset_seconds / a
                       for a in scales for r in self.doppler_scale_values)
        if earliest < 0:
            raise ValueError("earliest public source arrival precedes capture")
        guard = (len(self.taps) - 1) / self.sample_rate
        self.crop_start_raw = max(0, math.floor((earliest - guard) * self.output_sample_rate) * self.decimation)
        self.crop_start_output = self.crop_start_raw // self.decimation
        self.samples = (self.capture_rows - 1) // self.decimation + 1 - self.crop_start_output
        self.lags = lag_grid(math.ceil((upper - lower) / min(self.doppler_scale_values) * self.output_sample_rate),
                             _integer(options["lag_step"], "lag_step"))
        if self.samples <= 4 or self.lags[-1] >= self.samples:
            raise ValueError("capture is too short for public timing bank")
        self._configure_validity(options["capture_validity"])
        # Complete support at every slow-clock, late-arrival and delay endpoint.
        longest = max((self.capture_lead_seconds + upper + self.delay_seconds) / r +
                      (self.source_offset_seconds + (source.shape[2] + 4) / self.sample_rate) / a
                      for a in scales for r in self.doppler_scale_values)
        if math.ceil(longest * self.sample_rate) + len(self.taps) + 4 >= self.capture_rows:
            raise ValueError("capture does not retain complete slow-clock source/channel/FIR tail")
        passed, stopped = self.pass_edge_hz, self.stop_edge_hz
        self.preprocessing = dict(format="triad-helper-data-frontend-v2", profile=self.frontend_profile,
            input_sample_rate=self.sample_rate, output_sample_rate=self.output_sample_rate, decimation=self.decimation,
            pass_edge_hz=passed, stop_edge_hz=stopped, design_attenuation_db=96., tap_count=len(self.taps),
            filter_tail_seconds=guard, filter_group_delay_seconds=guard / 2,
            noise_variance_gain=float(np.sum(self.taps**2)), crop_start_raw_sample=self.crop_start_raw,
            crop_output_samples=self.samples, capture_rows=self.capture_rows,
            source_offset_seconds=self.source_offset_seconds, capture_lead_seconds=self.capture_lead_seconds,
            arrival_bounds_seconds=self.arrival_bounds_seconds, delay_seconds=self.delay_seconds, doppler_hz=self.doppler_hz,
            source_half_bandwidth_hz=self.source_half_bandwidth_hz,
            channel_line_half_width_hz=self.channel_line_half_width_hz,
            clock_interpolation="Input-clock four-point cubic Lagrange, complete explicit zero extension; source-specific error unqualified",
            clock_coordinate="source_offset/(1+q) then x((1+q)*t); capture lead/arrival/delay divided by Doppler scale=1/RX clock",
            observation_filtering="continuous whole capture before crop; actually observed extent only",
            cfo_order="FIR first, CFO second; finite-FIR noncommutation remains declared approximation",
            erasure_support=self._validity_metadata)
        if self.frontend_profile == "rate-aware":
            self.preprocessing["filter_engine"] = self._filter_metadata()
        identity = hashlib.sha256(json.dumps(self.preprocessing, sort_keys=True).encode())
        identity.update(self.taps.tobytes())
        self.preprocessing_id = identity.hexdigest()
        self._minimum_fft_size = math.ceil(4 * source.shape[2] / min(scales) / self.decimation)
        nfft = fft.next_fast_len(max(2 * self.samples - 1, self._minimum_fft_size))
        self.cfo_grid_step_hz = self.output_sample_rate / nfft
        workspace_bound = (self.samples * (81 + 320) + nfft * 320 + len(self.lags) * 320 + 65536)
        if self._device.startswith("cuda"):
            workspace_bound += nfft * 192 + self.samples * 96
        factor_bound = len(self.lags) * 4096 + nfft * 64 + self.samples * 48
        self._batch_transient_bound = self._batch_size * (nfft * 80 + self.samples * 48 + len(self.lags) * 2048)
        self.frontend_array_bound_bytes = (self._source.nbytes + self.capture_rows * 64 +
                                           self.samples * 128 + self.taps.nbytes + self.validity_array_bound_bytes + self._filter_scratch_bytes(self.capture_rows))
        self._child_limit = ((self.memory_limit_bytes - self.frontend_array_bound_bytes - self._batch_transient_bound) //
                             self._cache_capacity + self._batch_transient_bound)
        self.explicit_array_bound_bytes = (self.frontend_array_bound_bytes +
            self._cache_capacity * (workspace_bound + factor_bound) + self._batch_transient_bound)
        self._base_explicit_array_bound_bytes = self.explicit_array_bound_bytes
        if self.explicit_array_bound_bytes >= self.memory_limit_bytes:
            raise MemoryError("bounded clock/epoch cache and frontend exceed explicit memory limit")
        self.template_energy = []
        self.taps.flags.writeable = False
        self.lags.flags.writeable = False
        self.preparation_seconds = time.perf_counter() - started
        self._validate_batched_bank()
        self._bind_identity()

    @staticmethod
    def _real_bank(values, name, lower, upper):
        bank = tuple(_number(v, name) for v in values)
        if not bank or len(bank) > 4096 or len(set(bank)) != len(bank) or min(bank) < lower or max(bank) > upper:
            raise ValueError(f"{name} bank requires unique finite values in [{lower},{upper}]")
        return bank

    def _validate_batched_bank(self):
        groups = (len(self.epoch_offsets) * len(self.clock_ppm_values) * len(self.doppler_scale_values) *
                  len(self.lags) * len(self.frequency_offsets))
        if groups > 2**53:
            raise ValueError("full public bank exceeds exact group-count representation")
        pass_edge = self.preprocessing["pass_edge_hz"]
        edge = (self.source_half_bandwidth_hz * max(1 + q * 1e-6 for q in self.clock_ppm_values) +
                self.channel_line_half_width_hz * max(self.doppler_scale_values) + max(map(abs, self.frequency_offsets)))
        if edge > pass_edge:
            raise ValueError("clock/Doppler/CFO bank exceeds fixed DATA FIR public passband")
        # Include returned Python numeric metadata conservatively, as well as
        # arrays. Per-hypothesis records are explicitly capped while scoring.
        metadata = (128 * len(self.frequency_offsets) +
                    256 * (len(self.clock_ppm_values) + len(self.doppler_scale_values)) +
                    self._diagnostic_limit * 8192)
        self.explicit_array_bound_bytes = self._base_explicit_array_bound_bytes + metadata
        if self.explicit_array_bound_bytes >= self.memory_limit_bytes:
            raise MemoryError("bank metadata and bounded diagnostics exceed explicit memory limit")
        self._child_limit = ((self.memory_limit_bytes - self.frontend_array_bound_bytes - metadata - self._batch_transient_bound) //
                             self._cache_capacity + self._batch_transient_bound)
        self.effective_group_count = groups

    def with_hypotheses(self, *, frequency_offsets=None, clock_ppm_values=None, doppler_scale_values=None):
        """Select subsets of the original source-matched public hypothesis banks.

        The observation crop and complete support remain fixed. Variants share
        a serialized bounded LRU of public workspaces, never observation scores.
        """
        if self.fft_backend != "batched":
            raise ValueError("clock/rate variants require batched backend")
        result = copy.copy(self)
        if frequency_offsets is not None:
            result.frequency_offsets = _frequencies(frequency_offsets, self.frontend_profile,
                source_half_bandwidth_hz=self.source_half_bandwidth_hz,
                channel_line_half_width_hz=self.channel_line_half_width_hz, pass_edge_hz=self.pass_edge_hz)
        for name, values in (("clock_ppm_values", clock_ppm_values), ("doppler_scale_values", doppler_scale_values)):
            if values is not None:
                bank = tuple(_number(v, name) for v in values)
                if not bank or len(set(bank)) != len(bank) or not set(bank).issubset(getattr(self, name)):
                    raise ValueError("clock/rate variants must be nonempty subsets of prepared public bank")
                setattr(result, name, bank)
        result._validate_batched_bank()
        result._bind_identity()
        return result

    def with_clock_ppm_values(self, values):
        return self.with_hypotheses(clock_ppm_values=values)

    def aligned_frequency_offsets(self, prior_half_width_hz):
        """Endpoint-inclusive CFO lattice with exact resident FFT-shift reuse.

        Spacing is no larger than 1/(4*complete slow-clock source duration).
        Prior endpoints are retained as two possible non-bin-aligned values.
        """
        if self.fft_backend != "batched":
            raise ValueError("aligned frequency lattice requires batched backend")
        bound = _number(prior_half_width_hz, "prior_half_width_hz")
        if bound <= 0:
            raise ValueError("prior_half_width_hz must be positive")
        half = math.floor(bound / self.cfo_grid_step_hz)
        if 2 * half + 3 > 1_000_000:
            raise ValueError("aligned CFO bank exceeds bounded representation")
        grid = np.arange(-half, half + 1, dtype=np.float64) * self.cfo_grid_step_hz
        if bound - half * self.cfo_grid_step_hz > 1e-12 * max(1., bound):
            grid = np.r_[-bound, grid, bound]
        return tuple(map(float, grid))

    def _batched_child(self, epoch_index, clock, scale):
        from triad_helper_fft import BatchedFFTReceiver
        cache_key = (epoch_index, clock, scale)
        if cache_key in self._cache:
            self._cache.move_to_end(cache_key)
            return self._cache[cache_key], 0.
        # Evict BEFORE allocating another workspace; variants share this bound.
        while len(self._cache) >= self._cache_capacity:
            self._cache.popitem(last=False)
        started = time.perf_counter()
        rate = 1 + clock * 1e-6
        lower = self.arrival_bounds_seconds[0]
        anchor = ((self.capture_lead_seconds + lower) / scale + self.source_offset_seconds / rate) * self.sample_rate - self.crop_start_raw
        delay = self.delay_seconds / scale * self.sample_rate
        basis = np.zeros((2, self.samples, 2), np.complex128)
        energy = []
        for bit in (0, 1):
            warped = clock_warp(self._source[epoch_index, bit], clock)
            bit_energy = []
            for column, offset in enumerate((anchor, anchor + delay)):
                wide = fractional_delay(warped, offset)
                if column:
                    times = ((np.arange(len(wide), dtype=np.longdouble) + self.crop_start_raw) / self.sample_rate -
                             self.capture_lead_seconds / scale)
                    wide *= phase(self.doppler_hz * scale, times)
                filtered = self._filter(wide)
                if len(filtered) > self.samples:
                    raise ValueError("complete clocked source/FIR support leaves capture")
                basis[bit, :len(filtered), column] = filtered
                before = float(np.vdot(wide, wide).real)
                after = float(np.vdot(filtered, filtered).real) * self.decimation
                bit_energy.append(dict(wide_energy=before, retained_equivalent_wide_energy=after,
                                       ratio=after / before if before else None))
            energy.append(bit_energy)
        child = BatchedFFTReceiver(basis, self.output_sample_rate, self.lags,
            key=dict(arrival_seconds=lower / scale, epoch_offset_seconds=self.epoch_offsets[epoch_index], clock_ppm=clock),
            frequencies=[0., self.doppler_hz * scale, 0., self.doppler_hz * scale],
            reference_variance=self.reference_variance, precision=self._precision, device=self._device,
            memory_limit_bytes=self._child_limit, preprocessing_identity=self.preprocessing_id,
            frequency_batch_size=self._batch_size, minimum_fft_size=self._minimum_fft_size,
            valid=self.output_validity)
        entry = (child, energy)
        self._cache[cache_key] = entry
        return entry, time.perf_counter() - started

    def _score_batched(self, prepared):
        if (not isinstance(prepared, PreparedDataObservation) or prepared.preprocessing_id != self.preprocessing_id or
                prepared.validity_id != self.validity_id or
                prepared.samples.shape != (self.samples,) or prepared.samples.flags.writeable):
            raise ValueError("prepared observation has a different public preprocessing identity or extent")
        started = time.perf_counter()
        mixture = np.full(2, -np.inf)
        best_scores = np.full(2, -np.inf)
        best_parameters = [None, None]
        outcomes = []
        energies = []
        preparation = 0.
        epoch_mixture = np.full((len(self.epoch_offsets), 2), -np.inf)
        total_receiver = fft_seconds = likelihood_seconds = 0.
        aligned_cfo_evaluations = total_hypotheses = 0
        # Serializes shared public LRU/workspaces across all subset variants.
        with self._cache_lock:
            for ei, epoch in enumerate(self.epoch_offsets):
                for clock in self.clock_ppm_values:
                    for scale in self.doppler_scale_values:
                        (child, energy), elapsed = self._batched_child(ei, clock, scale)
                        preparation += elapsed
                        result = child.score(prepared.samples, self.frequency_offsets,
                                             family_groups=self.effective_group_count)
                        scores = np.asarray(result["bit_log_likelihoods"])
                        mixture = np.logaddexp(mixture, scores)
                        epoch_mixture[ei] = np.logaddexp(epoch_mixture[ei], scores)
                        total_receiver += result["timing"]["total_receiver_seconds"]
                        fft_seconds += result["timing"]["observation_fft_seconds"]
                        likelihood_seconds += result["timing"]["residual_and_likelihood_seconds"]
                        aligned_cfo_evaluations += result["resources"]["aligned_cfo_count"]
                        total_hypotheses += 1
                        for bit in (0, 1):
                            if scores[bit] > best_scores[bit]:
                                best_scores[bit] = scores[bit]
                                if bit == result["bit"]:
                                    parameters = dict(result["best_group_public_parameters"])
                                    parameters.update(doppler_scale=scale,
                                        arrival_physical_seconds=parameters["arrival_seconds"] * scale)
                                    best_parameters[bit] = parameters
                                else:
                                    best_parameters[bit] = None
                        if len(outcomes) < self._diagnostic_limit:
                            outcomes.append(dict(epoch=epoch, clock_ppm=clock, doppler_scale=scale,
                                bit_log_likelihoods=scores.tolist(), timing=result["timing"], resources=result["resources"]))
                            energies.append(dict(epoch=epoch, clock_ppm=clock, doppler_scale=scale, bits=energy))
                        # Do not keep the previous LRU victim alive while the
                        # next workspace is constructed (capacity one matters).
                        del child
        mixture -= math.log(total_hypotheses)
        winner = int(np.argmax(mixture))
        epoch_scores = (epoch_mixture - math.log(len(self.clock_ppm_values) * len(self.doppler_scale_values))).tolist()
        return dict(format="triad-helper-data-working-likelihood-v2", receiver_id=self.receiver_id,
            gate_id=self.receiver_id, bit=winner, score=float(mixture[winner]), working_presence=float(mixture[winner]),
            bit_log_likelihoods=mixture.tolist(), opposite_bit_margin=float(mixture[winner] - mixture[1-winner]),
            epoch_bit_log_likelihoods=epoch_scores,
            best_group_public_parameters=best_parameters[winner],
            best_epoch_offset_seconds=(best_parameters[winner] or {}).get("epoch_offset_seconds"),
            best_group_scope="diagnostic winner within strongest epoch/clock/rate bit-mixture; not used for selection",
            geometry=dict(epoch_offsets=list(self.epoch_offsets), lag_samples=self.lags.tolist(), samples=self.samples,
                frequency_offsets_hz=list(self.frequency_offsets), clock_ppm_values=list(self.clock_ppm_values),
                doppler_scale_values=list(self.doppler_scale_values), effective_group_count=self.effective_group_count,
                mixture="uniform over every declared epoch, clock, RX-rate, CFO and arrival; no score pruning"),
            hypothesis_bit_log_likelihoods=outcomes, preprocessing=self.preprocessing, template_energy=energies,
            arithmetic=result["arithmetic"],
            calibration=dict(analytic_admission_validated=False, empirical_gate_required=True,
                residual_family_groups=self.effective_group_count,
                residual_allocation="complete epoch/clock/RX-rate/CFO/arrival family; legacy allocated each epoch separately",
                scope="working colored-noise statistic only; independent full-procedure matched null calibration required"),
            timing=dict(total_preparation_seconds=self.preparation_seconds,
                hypothesis_preparation_seconds=preparation, whole_capture_filter_seconds=prepared.filtering_seconds,
                epoch_receiver_seconds=total_receiver, observation_fft_seconds=fft_seconds,
                residual_and_likelihood_seconds=likelihood_seconds,
                total_score_prepared_seconds=time.perf_counter() - started,
                shared_preparation="bounded LRU public factors across CFO/subset variants; capture filtering shared"),
            resources=dict(explicit_array_bound_bytes=self.explicit_array_bound_bytes,
                memory_limit_bytes=self.memory_limit_bytes, retained_group_scores=False,
                requested_maximum_cached_hypotheses=self.requested_cache_capacity,
                maximum_cached_hypotheses=self._cache_capacity,
                reachable_cache_key_count=self.reachable_cache_key_count,
                aligned_cfo_evaluations=aligned_cfo_evaluations,
                retained_hypothesis_diagnostics=len(outcomes), suppressed_hypothesis_diagnostics=total_hypotheses-len(outcomes),
                hypothesis_diagnostic_limit=self._diagnostic_limit,
                scope="bounded explicit source/frontend/cache/batch arrays; excludes runtime allocator reserve, not process RSS"),
            limits=["Static two-ray DATA model omits selected selective AM; source-specific mismatch remains.",
                "Clock lattice, four-point raw-source interpolation, fractional timing and FIR noncommutation require source-specific loss checks.",
                "Finite gates have noncompact skirts; reported FIR retention covers public templates only.",
                "No analytic admission, physical-completion or ordinary receiver integration claim."])

    def freeze_calibration(self, scores, *, alpha=.05):
        return freeze_calibration(scores, receiver_id=self.receiver_id, alpha=alpha)
