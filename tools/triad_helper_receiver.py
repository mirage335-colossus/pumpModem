#!/usr/bin/env python3
"""Public DATA adaptation for the isolated Helper comparison.

This is the existing EmpiricalReceiver likelihood, uniformly mixed over public
epoch templates. It is not an operational admission rule. The whole complex
18-kHz capture passes through one fixed causal FIR before DATA cropping and
decimation. Caller-supplied templates contain the complete source waveform,
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
from dataclasses import dataclass, replace
import hashlib
import json
import math
from pathlib import Path
import time

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


def _frequencies(values):
    result = tuple(_number(v, "frequency offset") for v in values)
    if not result or len(result) > 4096 or len(set(result)) != len(result):
        raise ValueError("frequency_offsets must be a nonempty unique bank of at most 4096 entries")
    # Fixed frontend: nominal HF occupied half-width is 202.5 Hz, plus 90 Hz
    # spread. Preserve a public allowance rather than inspecting noisy energy.
    if max(map(abs, result)) + 292.5 > 330:
        raise ValueError("frequency bank exceeds the fixed DATA FIR public passband allowance")
    return result


def data_fir_taps():
    """Fixed real causal lowpass: 330-Hz pass edge, 430-Hz stop edge.

    Kaiser design targets 96 dB, with margin over the required 80-dB stopband.
    No automatic frequency shift, energy normalization or observation tuning.
    """
    count, beta = signal.kaiserord(96., (430. - 330.) / (RAW_SAMPLE_RATE / 2))
    count += 1 - count % 2
    return signal.firwin(count, 380., window=("kaiser", beta), fs=RAW_SAMPLE_RATE)


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


class HelperDataReceiver:
    """Cached epoch bank around the actual maintained empirical FFT receiver.

    ``raw_templates`` has shape [epochs, two bits, complete source samples].
    ``source_offset_seconds`` maps source sample zero onto the transmit calendar
    (normally 38 seconds minus source halo), independently of epoch addressing.
    ``arrival_bounds_seconds`` is a public prior; the lower bound is the one
    fractional anchor. The lattice is integer 900-Hz samples and includes the
    ceil-rounded upper endpoint. No capture-derived alignment or truth is read.
    ``reference_variance`` is positive public per-complex-sample variance AFTER
    the FIR. White raw noise gives raw_variance*sum(taps**2); filtering colors
    noise, so only a separately calibrated empirical gate may admit a bit.
    """
    def __init__(self, raw_templates, epoch_offsets, *, source_offset_seconds,
                 capture_lead_seconds, capture_rows, arrival_bounds_seconds,
                 delay_seconds, doppler_hz, reference_variance,
                 frequency_offsets=(0.,), sample_rate=RAW_SAMPLE_RATE,
                 output_sample_rate=DATA_SAMPLE_RATE, lag_step=4,
                 precision="fp32", device="cpu",
                 memory_limit_bytes=512 * 1024 * 1024):
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
        if sample_rate != RAW_SAMPLE_RATE or output_sample_rate != DATA_SAMPLE_RATE:
            raise ValueError("this public frontend requires 18000-Hz input and 900-Hz output")
        self.capture_rows = _integer(capture_rows, "capture_rows")
        self.memory_limit_bytes = _integer(memory_limit_bytes, "memory_limit_bytes")
        self.frequency_offsets = _frequencies(frequency_offsets)
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
        self.doppler_hz = _number(doppler_hz, "doppler_hz")
        if self.delay_seconds < 0 or abs(self.doppler_hz) > 90:
            raise ValueError("public channel requires nonnegative delay and |Doppler| <= 90 Hz")
        self.lags = lag_grid(math.ceil((upper - lower) * DATA_SAMPLE_RATE),
                             _integer(lag_step, "lag_step"))
        if len(self.lags) * len(self.frequency_offsets) * len(epochs) > 1_000_000:
            raise ValueError("complete epoch/timing/frequency bank exceeds 1000000 groups")
        self.taps = data_fir_taps()
        guard = (len(self.taps) - 1) / RAW_SAMPLE_RATE
        earliest = self.capture_lead_seconds + self.source_offset_seconds + lower
        if earliest < 0:
            raise ValueError("earliest public source arrival precedes the capture")
        # Start on the capture's original decimation phase. A full FIR-history
        # guard is retained even though the observation is filtered before crop.
        self.crop_start_raw = max(0, math.floor((earliest - guard) * RAW_SAMPLE_RATE / DECIMATION) * DECIMATION)
        self.crop_start_output = self.crop_start_raw // DECIMATION
        self.samples = (self.capture_rows - 1) // DECIMATION + 1 - self.crop_start_output
        if self.samples <= 4 or self.lags[-1] >= self.samples:
            raise ValueError("capture is too short for the public DATA arrival bank")
        anchor_samples = earliest * RAW_SAMPLE_RATE - self.crop_start_raw
        delay_samples = self.delay_seconds * RAW_SAMPLE_RATE
        # Worst-case cubic tail, complete causal FIR tail, and largest lag.
        support_stop = math.ceil(anchor_samples + delay_samples) + source.shape[2] + 3 + len(self.taps) - 1
        if (support_stop - 1) // DECIMATION + int(self.lags[-1]) >= self.samples:
            raise ValueError("capture does not retain complete source/channel/FIR tail at every public lag")
        nfft = fft.next_fast_len(2 * self.samples - 1)
        per_workspace = self.samples * (81 + 320) + nfft * 320 + len(self.lags) * 320 + 65536
        if device.startswith("cuda"):
            per_workspace += nfft * 192 + self.samples * 96
        score_reserve = min(64, len(self.lags)) * 128 * 1024 + 1024 * 1024
        # Conservative explicit arrays only, not Python/Torch/process RSS.
        self.frontend_array_bound_bytes = max(source.nbytes + support_stop * 160,
                                              self.capture_rows * 64) + self.samples * 64 + self.taps.nbytes
        estimate = self.frontend_array_bound_bytes + len(epochs) * per_workspace + score_reserve
        if estimate >= self.memory_limit_bytes:
            raise MemoryError("complete epoch bank and frontend exceed explicit memory limit")
        if not np.isfinite(source).all():
            raise ValueError("raw_templates must be finite")
        self.preprocessing = dict(
            format="triad-helper-data-frontend-v1", input_sample_rate=RAW_SAMPLE_RATE,
            output_sample_rate=DATA_SAMPLE_RATE, decimation=DECIMATION,
            pass_edge_hz=330., stop_edge_hz=430., design_attenuation_db=96.,
            tap_count=len(self.taps), filter_tail_seconds=guard,
            filter_group_delay_seconds=guard / 2,
            noise_variance_gain=float(np.sum(self.taps**2)),
            crop_start_raw_sample=self.crop_start_raw, crop_output_samples=self.samples,
            capture_rows=self.capture_rows, source_offset_seconds=self.source_offset_seconds,
            capture_lead_seconds=self.capture_lead_seconds,
            fractional_anchor_arrival_seconds=lower,
            arrival_upper_lattice_seconds=lower + int(self.lags[-1]) / DATA_SAMPLE_RATE,
            delay_seconds=self.delay_seconds, doppler_hz=self.doppler_hz,
            delay_raw_samples=delay_samples,
            delay_is_integer=abs(delay_samples - round(delay_samples)) <= 1e-9,
            anchor_raw_samples_from_crop=anchor_samples,
            delay_interpolation="integer exact; fractional causal four-point cubic Lagrange with full three-sample tail",
            observation_filtering="continuous whole capture before crop; output restricted to actually observed capture extent",
            cfo_order="FIR first, existing FFT CFO hypotheses second; finite-FIR noncommutation remains a declared model mismatch")
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
                        physical_times = (np.arange(len(wide), dtype=np.longdouble) + self.crop_start_raw) / RAW_SAMPLE_RATE - self.capture_lead_seconds
                        wide *= phase(self.doppler_hz, physical_times)
                    filtered = signal.upfirdn(self.taps, wide, down=DECIMATION)
                    basis[bit, :len(filtered), column] = filtered
                    before = float(np.vdot(wide, wide).real)
                    after = float(np.vdot(filtered, filtered).real) * DECIMATION
                    bit_energy.append(dict(wide_energy=before, retained_equivalent_wide_energy=after,
                                           ratio=after / before if before else None))
                energy.append(bit_energy)
            self.template_energy.append(energy)
            self._receivers.append(EmpiricalReceiver(
                basis, DATA_SAMPLE_RATE, self.lags,
                key=dict(arrival_seconds=lower, epoch_offset_seconds=epoch),
                frequencies=[0., self.doppler_hz, 0., self.doppler_hz],
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

    def _bind_identity(self):
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
        result = copy.copy(self)
        result.frequency_offsets = _frequencies(frequency_offsets)
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
        """Filter all capture rows once; erasures are explicitly unsupported."""
        raw = np.asarray(raw_observation)
        if raw.shape != (self.capture_rows,) or raw.dtype.kind != "c" or not np.isfinite(raw).all():
            raise ValueError("raw_observation must be a finite complex vector of capture_rows samples")
        if valid is not None:
            valid = np.asarray(valid)
            if valid.shape != raw.shape or valid.dtype.kind not in "bui" or not np.all(valid == 1):
                raise ValueError("this continuous FIR adapter rejects erasures; all capture rows must be valid")
        started = time.perf_counter()
        whole = signal.upfirdn(self.taps, raw, down=DECIMATION)
        cropped = np.array(whole[self.crop_start_output:self.crop_start_output + self.samples], dtype=np.complex128, copy=True)
        cropped.flags.writeable = False
        return PreparedDataObservation(cropped, self.preprocessing_id, time.perf_counter() - started)

    def score(self, raw_observation, *, valid=None):
        return self.score_prepared(self.prepare_observation(raw_observation, valid=valid))

    def score_prepared(self, prepared):
        """Uniform epoch log-mean-exp; presence is max of the two bit mixtures."""
        if not isinstance(prepared, PreparedDataObservation) or prepared.preprocessing_id != self.preprocessing_id:
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
                "One fractional anchor and integer 900-Hz lag lattice; continuous timing and clock mismatch unqualified.",
                "Finite FHSS gates have noncompact spectral skirts; FIR loss is reported per public template/column.",
                "Whole-capture filtering may retain Helper leakage; matched Helper-only and null controls are required.",
                "CFO rotation follows FIR filtering; passband ripple and noncommutation are model approximations.",
                "The existing residual normalizer rejects unresolved zero/near-zero residuals; no fallback is added.",
                "No analytic false-admission, physical completion or ordinary-receiver integration claim."])

    def freeze_calibration(self, scores, *, alpha=.05):
        return freeze_calibration(scores, receiver_id=self.receiver_id, alpha=alpha)
