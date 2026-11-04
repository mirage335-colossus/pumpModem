#!/usr/bin/env python3
"""Isolated three-carrier audio bridge and known-alignment diagnostics.

This module never opens an audio device.  It consumes public native waveform
exports without changing their Gaussian/admission metadata.  Its receiver is a
fixed-bank projection diagnostic, not the operational Robust receiver and not
an admission test.  Noise redraws below are conditional on one retained capture.
"""
from __future__ import annotations

from dataclasses import dataclass
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
from typing import Sequence

import numpy as np
from scipy import signal
from scipy import fft as scipy_fft
from scipy.stats import norm


DEFAULT_RATES = (.001, .01, .1, 1., 3.6, 10., 3600., 12000., 18000., 24000.)
DEFAULT_ADDED = (1, 100, 1000)
COMMON_NATIVE_ARGS = (
    "--seconds", "8", "--channel", "none", "--phase-diffusion", "0",
    "--rf-hz", "0", "--nominal-if-hz", "0", "--basis", "flat", "--threads", "1",
    "--seed", "82001", "--epoch", "1793790000",
)


def _positive(value, name):
    value = float(value)
    if not math.isfinite(value) or value <= 0:
        raise ValueError(f"{name} must be finite and positive")
    return value


def _integer(value, name, minimum=1):
    if isinstance(value, (bool, np.bool_)) or int(value) != value or value < minimum:
        raise ValueError(f"{name} must be an integer >= {minimum}")
    return int(value)


def audio_geometry(occupied_bandwidth_hz, carrier_hz, audio_fs, *,
                   transition_hz=None, audio_band_hz=None):
    """Nominal image/Nyquist checks, distinct from measured analog support."""
    width = _positive(occupied_bandwidth_hz, "occupied bandwidth")
    fs = _positive(audio_fs, "audio sample rate")
    fc = float(carrier_hz)
    transition = .05 * width if transition_hz is None else float(transition_hz)
    if not math.isfinite(fc) or not math.isfinite(transition) or transition < 0:
        raise ValueError("carrier and nonnegative transition must be finite")
    half = width / 2
    low, high = fc - half, fc + half
    reasons = []
    if low <= transition:
        reasons.append("real-audio conjugate images overlap or lack the declared DC transition")
    if high + transition >= fs / 2:
        reasons.append("occupied band plus transition reaches audio Nyquist")
    analog_supported = None
    if audio_band_hz is not None:
        lo, hi = map(float, audio_band_hz)
        if not (0 <= lo < hi <= fs / 2):
            raise ValueError("audio_band_hz must be within [0, audio Nyquist]")
        analog_supported = bool(low >= lo and high <= hi)
        if not analog_supported:
            reasons.append("occupied band extends outside the supplied measured audio band")
    return dict(carrier_hz=fc, occupied_bandwidth_hz=width,
                nominal_audio_band_hz=[low, high], transition_hz=transition,
                real_image_separated=bool(low > transition),
                below_audio_nyquist=bool(high + transition < fs / 2),
                measured_audio_band_supported=analog_supported,
                supported=not reasons, reasons=reasons,
                scope="nominal support calculation; finite shaped spectra have nonzero tails")


def geometry_census(binary, *, rates=DEFAULT_RATES, added_values=DEFAULT_ADDED,
                    carrier_modes=("half_rate", 1725., "adaptive"),
                    fhss_values=(False, True), audio_fs=48000,
                    audio_band_hz=None, maximum_full_seconds=3000.,
                    native_extra_args=(), timeout_seconds=10.):
    """Query each distinct native geometry once, then factor carrier checks.

    No samples are generated.  Native invalidity, analog-band exclusion, and a
    complete-bit duration outside the budget remain separate results.
    """
    audio_fs = _positive(audio_fs, "audio sample rate")
    maximum_full_seconds = _positive(maximum_full_seconds, "full duration budget")
    rows = []
    for rate in rates:
        rate = _positive(rate, "GUI rate")
        for added in added_values:
            added = _integer(added, "Added Symbols")
            for fhss in fhss_values:
                argv = [str(binary), "--mode", "configure", *COMMON_NATIVE_ARGS,
                        "--rate", str(rate), "--added", str(added), *map(str, native_extra_args)]
                if fhss:
                    argv.append("--fhss")
                try:
                    result = subprocess.run(argv, capture_output=True, text=True,
                                            timeout=timeout_seconds, check=False)
                    if result.returncode:
                        native = dict(status="rejected", error=result.stderr.strip(),
                                      exit_code=result.returncode)
                        configuration = None
                    else:
                        configuration = json.loads(result.stdout)
                        native = dict(status="configured", configuration=configuration)
                except (subprocess.TimeoutExpired, OSError, ValueError) as exc:
                    native = dict(status="unavailable", error=str(exc))
                    configuration = None
                width = .625 * rate * added
                full_seconds = None
                native_fs = None
                if configuration is not None:
                    c = configuration["configuration"]
                    width = float(c["capture"]["nominal_triplet_bandwidth_hz"])
                    native_fs = float(c["capture"]["sample_rate_hz"])
                    full_seconds = (configuration["performance_geometry"]["capture_samples"]
                                    / native_fs)
                for mode in carrier_modes:
                    if mode == "half_rate":
                        fc = rate / 2
                    elif mode == "adaptive":
                        low = 0. if audio_band_hz is None else float(audio_band_hz[0])
                        fc = max(1725., .55 * width + low + 1.)
                    else:
                        fc = float(mode)
                    geometry = audio_geometry(width, fc, audio_fs, audio_band_hz=audio_band_hz)
                    within = full_seconds is not None and full_seconds <= maximum_full_seconds
                    if native["status"] != "configured":
                        evidence = "native_geometry_unavailable"
                    elif not geometry["supported"]:
                        evidence = "unsupported_audio_geometry"
                    elif not within:
                        evidence = "actual_rate_component_and_model_prediction_only"
                    else:
                        evidence = "eligible_for_full_physical_capture_not_yet_measured"
                    rows.append(dict(rate_hz=rate, added_symbols=added, fhss_envelope_only=bool(fhss),
                        carrier_mode=str(mode), native=native, audio=geometry,
                        native_sample_rate=native_fs, full_capture_seconds=full_seconds,
                        minimum_useful_seconds_lower=384. / rate,
                        full_capture_within_declared_budget=within, evidence=evidence,
                        real_rf_retuning_tested=False))
    return rows


def _shape(value, dimensions, name):
    if not isinstance(value, list) or len(value) != dimensions:
        raise ValueError(f"invalid {name}")
    return tuple(_integer(v, name) for v in value)


def _array_file(directory, name, shape, dtype, max_bytes):
    if not isinstance(name, str) or Path(name).name != name:
        raise ValueError("array file must be a local basename")
    size = math.prod(shape) * np.dtype(dtype).itemsize
    if size > max_bytes:
        raise ValueError("export exceeds diagnostic array byte budget")
    path = directory / name
    if not path.is_file() or path.stat().st_size != size:
        raise ValueError(f"array byte count does not match shape: {name}")
    return np.memmap(path, mode="r", dtype=dtype, shape=shape)


def load_export(path, *, max_bytes=1024 * 1024 * 1024):
    """Load native public arrays without asserting a hardware noise law."""
    directory = Path(path)
    raw = (directory / "manifest.json").read_bytes()
    m = json.loads(raw)
    kind = m.get("format")
    if kind not in ("triad-long-basis-v1", "triad-source-window-v1", "triad-sampled-waveforms-v1"):
        raise ValueError("unsupported native waveform export schema")
    source_window = kind == "triad-source-window-v1"
    if m.get("layout") != ("candidate,sample" if source_window else "candidate,sample,column"):
        raise ValueError("unsupported native basis layout")
    order = m.get("byte_order", "native")
    if order not in ("little", "native") or (order == "native" and sys.byteorder != "little"):
        raise ValueError("only little-endian native exports are supported")
    dtype = {"complex64": "<c8", "complex128": "<c16"}.get(m.get("complex_type"))
    if dtype is None:
        raise ValueError("unsupported native complex dtype")
    bshape = _shape(m.get("source_candidates_shape" if source_window else "basis_shape"),
                    2 if source_window else 3, "basis shape")
    signal_name = "observation" if kind == "triad-sampled-waveforms-v1" else "signal"
    sshape = _shape(m.get(signal_name + "_shape"), 1, "signal shape")
    if bshape[1] != sshape[0]:
        raise ValueError("signal and public bank sample counts differ")
    basis = _array_file(directory, m["source_candidates_file" if source_window else "basis_file"],
                        bshape, dtype, max_bytes)
    if source_window:
        basis = basis[..., None]
    values = _array_file(directory, m[signal_name + "_file"], sshape, dtype, max_bytes)
    bits = m.get("bitlabels", m.get("candidate_bits"))
    if bits is None:
        candidates = m.get("candidates", m.get("candidateparams", []))
        bits = [c.get("source_bit", c.get("bit")) for c in candidates]
    bits = _bits(bits, bshape[0])
    valid = np.ones(sshape[0], dtype=bool)
    if "validity_file" in m:
        v = _array_file(directory, m["validity_file"], sshape, "u1", max_bytes)
        if not np.isin(v, [0, 1]).all():
            raise ValueError("validity array is not binary")
        valid = np.asarray(v, bool)
    return dict(signal=values, basis=basis, validity=valid, manifest=m,
                manifest_sha256=hashlib.sha256(raw).hexdigest(), candidate_bits=bits,
                sample_rate=_positive(m["sample_rate"], "native sample rate"),
                observation_kind=signal_name,
                admission_contract="diagnostic only; native Gaussian metadata is not changed")


def _ratio(input_fs, output_fs):
    a, b = _positive(input_fs, "input sample rate"), _positive(output_fs, "output sample rate")
    ratio = (Fraction(str(b)) / Fraction(str(a))).limit_denominator(100000)
    if abs(float(ratio) - b / a) > 1e-12 * b / a:
        raise ValueError("sample-rate ratio is not representable within the declared tolerance")
    if max(ratio.numerator, ratio.denominator) > 100000:
        raise ValueError("resampling ratio exceeds bounded polyphase implementation")
    return ratio.numerator, ratio.denominator


def _resample(x, input_fs, output_fs):
    up, down = _ratio(input_fs, output_fs)
    if up == down:
        return np.asarray(x).copy(), 0., [1, 1]
    maximum = max(up, down)
    # Explicitly longer than SciPy's default.  Native v3 automatic grids can
    # place the nominal shaped band at 89.3% of their Nyquist frequency.
    half = 64 * maximum
    taps = signal.firwin(2 * half + 1, 1. / maximum, window=("kaiser", 8.6)).astype(np.float32)
    out = signal.resample_poly(x, up, down, window=taps, padtype="constant")
    return out, half / (float(input_fs) * up), [up, down]


def _finite_vector(x, *, complex_required=False):
    a = np.asarray(x)
    if a.ndim != 1 or not a.size or not np.isfinite(a).all():
        raise ValueError("samples must be a nonempty finite vector")
    if complex_required and not np.iscomplexobj(a):
        raise ValueError("complex baseband samples are required")
    return a


def _energy(x):
    """FP64 energy reduction without a full-duration complex-double copy."""
    total = 0.
    for first in range(0, len(x), 65536):
        part = np.asarray(x[first:first + 65536], np.complex128 if np.iscomplexobj(x) else np.float64)
        total += float(np.vdot(part, part).real)
    return total


def to_audio(x, native_fs, audio_fs, carrier_hz, *, occupied_bandwidth_hz,
             fixed_gain=1., source_start_seconds=0., max_peak=.98,
             max_audio_samples=100000000):
    """Map the complete summed I/Q to real PCM with one fixed, recorded gain.

    Returned samples share time zero with x; symmetric resampling transients
    are explicitly reported.  No peak or per-block energy normalization occurs.
    """
    x = _finite_vector(x, complex_required=True)
    native_fs = _positive(native_fs, "native sample rate")
    audio_fs = _positive(audio_fs, "audio sample rate")
    geometry = audio_geometry(occupied_bandwidth_hz, carrier_hz, audio_fs)
    if not geometry["supported"]:
        raise ValueError("; ".join(geometry["reasons"]))
    if occupied_bandwidth_hz >= .95 * native_fs:
        raise ValueError("native grid lacks the bridge's declared low-pass transition")
    gain = _positive(fixed_gain, "fixed gain")
    start = float(source_start_seconds)
    if not math.isfinite(start):
        raise ValueError("source time origin must be finite")
    count = math.ceil(len(x) * audio_fs / native_fs)
    if count > max_audio_samples:
        raise ValueError("audio array exceeds declared sample budget")
    base, edge_seconds, ratio = _resample(np.asarray(x, np.complex64), native_fs, audio_fs)
    pcm = np.empty(len(base), np.float32)
    # Chunking avoids full-duration phase and complex-double temporaries.
    for first in range(0, len(base), 65536):
        end = min(first + 65536, len(base))
        t = start + np.arange(first, end, dtype=np.float64) / audio_fs
        ph = np.exp(2j * np.pi * np.remainder(float(carrier_hz) * t, 1.)).astype(np.complex64)
        pcm[first:end] = (math.sqrt(2.) * gain * (base[first:end] * ph).real).astype(np.float32)
    peak = float(np.max(np.abs(pcm)))
    if not math.isfinite(peak) or peak > max_peak:
        raise ValueError(f"fixed-gain audio peak {peak:.6g} exceeds {max_peak}; choose an explicit lower gain")
    return pcm, dict(schema="triad-real-audio-bridge-v1", native_sample_rate=native_fs,
        audio_sample_rate=audio_fs, carrier_hz=float(carrier_hz), fixed_gain=gain,
        source_start_seconds=start, native_samples=len(x), audio_samples=len(pcm),
        resample_ratio=ratio, resample_half_support_seconds=edge_seconds,
        nominal_audio_geometry=geometry, measured_peak=peak,
        source_energy_sample_seconds=_energy(x) / native_fs,
        audio_energy_sample_seconds=_energy(pcm) / audio_fs,
        scaling="sqrt(2) * fixed_gain * real(composite_IQ * exp(+j*2*pi*fc*t))",
        arithmetic="FP32 polyphase bulk and PCM; FP64 phase coordinates and diagnostic reductions",
        alignment="resample_poly zero-phase alignment; constant zero extension; edge support is not hidden",
        window_boundary="returns the input time extent, not extra resampling-filter tails; exclude TX plus RX support at finite-window boundaries",
        continuous_cross_window_filter_state=False)


def from_audio(pcm, audio_fs, native_fs, carrier_hz, *, occupied_bandwidth_hz,
               source_start_seconds=0., expected_samples=None, undo_fixed_gain=1.):
    """Known-aligned complex direct conversion, retaining hardware attenuation.

    ``undo_fixed_gain`` may undo only the declared transmitter bridge gain.  It
    must never be estimated from the received payload.  The ordinary fixed
    polyphase downconverter optionally applies a fixed image-rejection FIR.
    Very narrow image transitions requiring more than 65537 taps are rejected;
    they need a separately qualified long-support path.
    """
    pcm = _finite_vector(pcm)
    if np.iscomplexobj(pcm):
        raise ValueError("real audio samples are required")
    audio_fs, native_fs = _positive(audio_fs, "audio sample rate"), _positive(native_fs, "native sample rate")
    geometry = audio_geometry(occupied_bandwidth_hz, carrier_hz, audio_fs)
    if not geometry["supported"]:
        raise ValueError("; ".join(geometry["reasons"]))
    half = float(occupied_bandwidth_hz) / 2
    if occupied_bandwidth_hz >= .95 * min(audio_fs, native_fs):
        raise ValueError("sample grid lacks the bridge's declared low-pass transition")
    # The negative-frequency image is periodic on the sampled audio grid.
    image_distance = min(2 * float(carrier_hz), audio_fs - 2 * float(carrier_hz))
    image_edge = image_distance - half
    extra_taps = None
    if native_fs >= audio_fs or image_edge <= native_fs / 2 + .025 * native_fs:
        stop = min(image_edge, native_fs / 2, audio_fs / 2)
        transition = stop - half
        if transition <= 0:
            raise ValueError("no finite transition separates the conjugate image")
        taps_count, beta = signal.kaiserord(80., transition / (audio_fs / 2))
        taps_count = max(3, taps_count | 1)
        if taps_count > 65537:
            raise ValueError("image rejection requires more than 65537 FIR taps; a narrower validated filter is required")
        extra_taps = signal.firwin(taps_count, (half + stop) / 2,
                                  fs=audio_fs, window=("kaiser", beta)).astype(np.float32)
    gain = _positive(undo_fixed_gain, "declared transmit gain")
    start = float(source_start_seconds)
    if not math.isfinite(start):
        raise ValueError("source time origin must be finite")
    mixed = np.empty(len(pcm), np.complex64)
    for first in range(0, len(pcm), 65536):
        end = min(first + 65536, len(pcm))
        t = start + np.arange(first, end, dtype=np.float64) / audio_fs
        ph = np.exp(-2j * np.pi * np.remainder(float(carrier_hz) * t, 1.)).astype(np.complex64)
        mixed[first:end] = (math.sqrt(2.) / gain * pcm[first:end] * ph).astype(np.complex64)
    image_support = 0.
    if extra_taps is not None:
        mixed = signal.oaconvolve(mixed, extra_taps, mode="same").astype(np.complex64)
        image_support = (len(extra_taps) - 1) / (2 * audio_fs)
    iq, edge_seconds, ratio = _resample(mixed, audio_fs, native_fs)
    edge_seconds += image_support
    original_count = len(iq)
    if expected_samples is not None:
        expected_samples = _integer(expected_samples, "expected samples")
        if len(iq) < expected_samples:
            raise ValueError("audio capture is shorter than the declared native support")
        iq = iq[:expected_samples]
    valid = np.ones(len(iq), bool)
    edge = min(len(iq), math.ceil(edge_seconds * native_fs))
    if edge:
        valid[:edge] = False
        valid[max(0, len(iq) - edge):] = False
    return iq, valid, dict(schema="triad-real-audio-ddc-v1", audio_sample_rate=audio_fs,
        native_sample_rate=native_fs, carrier_hz=float(carrier_hz),
        source_start_seconds=start, declared_transmit_gain_undone=gain,
        audio_samples=len(pcm), converted_samples=original_count, retained_samples=len(iq),
        resample_ratio=ratio, edge_samples_each_side=edge,
        image_rejection_taps=0 if extra_taps is None else len(extra_taps),
        image_rejection_half_support_seconds=image_support,
        resample_half_support_seconds=edge_seconds, valid_samples=int(valid.sum()),
        scaling="sqrt(2) * real_audio * exp(-j*2*pi*fc*t), then fixed low-pass/resampling",
        scope="known-alignment baseband conversion; no payload-fitted gain, delay or phase",
        valid_mask_scope="excludes RX support only; caller must also exclude finite TX-window support",
        continuous_cross_window_filter_state=False)


def _bits(candidate_bits, count):
    if candidate_bits is None:
        if count != 2:
            raise ValueError("candidate_bits is required for banks other than two candidates")
        return [0, 1]
    if len(candidate_bits) != count or any(type(x) not in (int, np.int32, np.int64) or x not in (0, 1) for x in candidate_bits):
        raise ValueError("candidate_bits must identify every public candidate as bit 0 or 1")
    if set(candidate_bits) != {0, 1}:
        raise ValueError("the diagnostic bank must contain both bits")
    return [int(x) for x in candidate_bits]


@dataclass
class _ProjectionBank:
    columns: np.ndarray
    slices: list[slice]
    bits: list[int]
    ranks: list[int]
    valid: np.ndarray
    basis: np.ndarray
    singular_values: list[list[float]]
    identifiability: dict
    rank_policy: dict

    def correlations(self, y):
        y = _finite_vector(y, complex_required=True)
        if len(y) != len(self.valid):
            raise ValueError("observation and bank sample counts differ")
        return self.columns.conj().T @ np.asarray(y[self.valid], np.complex128)

    def scores(self, correlations):
        return np.stack([np.sum(np.abs(correlations[..., s]) ** 2, axis=-1)
                         for s in self.slices], axis=-1)

    def bit_choices(self, scores):
        by_bit = np.stack([np.max(scores[..., np.asarray(self.bits) == bit], axis=-1)
                           for bit in (0, 1)], axis=-1)
        return np.argmax(by_bit, axis=-1), by_bit


def _prepare_bank(basis, mask=None, candidate_bits=None, rank_rtol=1e-10):
    a = np.asarray(basis)
    if a.ndim == 2:
        a = a[..., None]
    if a.ndim != 3 or min(a.shape) < 1 or not np.iscomplexobj(a) or not np.isfinite(a).all():
        raise ValueError("basis must be a finite complex [candidate,sample,column] array")
    if a.shape[0] * a.shape[2] > 128 or a.nbytes > 1024 * 1024 * 1024:
        raise ValueError("diagnostic bank exceeds bounded fixed-bank workspace")
    requested_rtol = float(rank_rtol)
    if not math.isfinite(requested_rtol) or not 0 <= requested_rtol < 1:
        raise ValueError("rank_rtol must be finite and in [0,1)")
    # FP64 SVD cannot recover information discarded in an FP32 public export.
    # Nearly dependent columns may acquire O(eps) singular modes solely from
    # independently rounded sample components.  The eight-epsilon safety
    # factor (times sqrt(column count)) is an explicit engineering floor,
    # not a statistical confidence claim.  High-precision input retains its
    # own smaller floor; converting an FP32 bank to FP64 beforehand cannot
    # restore its original precision and must not be used to evade this policy.
    source_eps = max(float(np.finfo(a.real.dtype).eps), float(np.finfo(np.float64).eps))
    precision_floor = 8 * source_eps * math.sqrt(a.shape[2])
    effective_rtol = max(requested_rtol, precision_floor)
    rank_policy = dict(source_dtype=str(a.dtype), reduction_dtype="complex128",
        requested_relative_threshold=requested_rtol,
        source_precision_relative_floor=precision_floor,
        effective_relative_threshold=effective_rtol,
        floor_rule="8 * source_component_epsilon * sqrt(number_of_columns)",
        interpretation="engineering floor for rounded public columns; modes below the floor are unresolved, not independently fitted channel dimensions")
    valid = np.ones(a.shape[1], bool) if mask is None else np.asarray(mask, bool)
    if valid.shape != (a.shape[1],) or not valid.any():
        raise ValueError("mask must retain at least one sample on the fixed sample grid")
    bits = _bits(candidate_bits, a.shape[0])
    q, ranks, slices, singular_values = [], [], [], []
    at = 0
    for candidate in a:
        u, s, _ = np.linalg.svd(np.asarray(candidate[valid], np.complex128), full_matrices=False)
        rank = int(np.sum(s > (s[0] * effective_rtol if s.size and s[0] else math.inf)))
        if not rank:
            raise ValueError("a candidate has no observable template mode")
        q.append(u[:, :rank])
        ranks.append(rank)
        slices.append(slice(at, at + rank))
        at += rank
        singular_values.append(s.tolist())
    pairs, aliases = [], []
    for i in range(len(q)):
        for j in range(i + 1, len(q)):
            if bits[i] == bits[j]:
                continue
            cosines = np.clip(np.linalg.svd(q[i].conj().T @ q[j], compute_uv=False), 0., 1.)
            overlap = float(np.sum(cosines ** 2))
            # Symmetric projector distance, normalized to [0,1].
            distance = math.sqrt(max(0., (ranks[i] + ranks[j] - 2 * overlap) / (ranks[i] + ranks[j])))
            identical = ranks[i] == ranks[j] and distance < 1e-6
            row = dict(candidates=[i, j], principal_cosines=cosines.tolist(),
                       normalized_projector_distance=distance, identical_subspace=identical)
            pairs.append(row)
            if identical:
                aliases.append([i, j])
    identifiability = dict(cross_bit_pairs=pairs, ambiguous_cross_bit_pairs=aliases,
        all_cross_bit_subspaces_distinct=not aliases,
        minimum_normalized_projector_distance=min(p["normalized_projector_distance"] for p in pairs),
        interpretation="identical unknown-complex-gain subspaces cannot distinguish bits; nonidentity alone is not a sensitivity guarantee")
    return _ProjectionBank(np.concatenate(q, axis=1), slices, bits, ranks, valid,
                           a, singular_values, identifiability, rank_policy)


def project_diagnostic(y, basis, mask=None, noise_psd=None, *, candidate_bits=None, rank_rtol=1e-10):
    """Fixed known-alignment unknown-gain projection; no operational admission."""
    if noise_psd is not None:
        raise NotImplementedError("colored-noise whitening is not qualified by this diagnostic; supply separately validated transformed data/bank")
    bank = _prepare_bank(basis, mask, candidate_bits, rank_rtol)
    correlations = bank.correlations(y)
    scores = bank.scores(correlations)
    choice, bit_scores = bank.bit_choices(scores)
    energy = _energy(np.asarray(y)[bank.valid])
    return dict(schema="triad-live-projection-diagnostic-v1", chosen_bit=int(choice),
        candidate_scores=scores.tolist(), bit_scores=bit_scores.tolist(),
        candidate_bits=bank.bits, candidate_ranks=bank.ranks,
        rank_policy=bank.rank_policy,
        template_singular_values=bank.singular_values, identifiability=bank.identifiability,
        valid_samples=int(bank.valid.sum()), observation_energy_samples=energy,
        best_projection_energy_samples=float(np.max(scores)),
        best_residual_energy_samples=max(0., energy - float(np.max(scores))),
        best_retained_observation_fraction=float(np.max(scores) / energy) if energy > 0 else None,
        score_difference=float(bit_scores[1] - bit_scores[0]),
        alignment="fixed by independent public measurement markers",
        statistic="maximum per-bit orthogonal projection energy over the declared candidate bank",
        arithmetic="complex FP64 SVD and reductions",
        operational_admission=None, false_admission_probability=None,
        limits="raw observation energy includes capture noise; ranking is not authenticated or probability-calibrated reception")


def bounded_timing_projection(y, basis, sample_rate, arrival_bounds_seconds, *,
                              fractional_phases=4, mask=None, candidate_bits=None,
                              interpolation_half_support=32,
                              maximum_fft_work_points=64000000):
    """Fixed public timing search of single-column complex candidate patterns.

    A positive delay means ``candidate[n-delay*Fs]``.  Bounds are relative to
    the existing public template coordinates, never centered on hidden truth
    or a payload-derived arrival.  Four polyphase branches mean a quarter-
    sample grid; exact prior endpoints are added even when off that lattice.

    Each complete template is interpolated with a zero-extended 65-tap Kaiser
    windowed sinc (by default).  Its full interpolation tails are retained.
    Cross-correlations are linear, FFT-zero-padded, and normalized by the
    shifted template energy under the SAME fixed observation validity mask.
    There is no per-dwell cut, tail reset, or circular wrap.  Only one fractional
    branch is resident at once, not a dense timing/template bank.

    This is a finite sampled timing diagnostic, not continuous-time maximum
    likelihood or an operational admission test.  It does not search frequency,
    clock rate, keyed epochs or multiple channel columns.  A frozen LTI audio
    model commutes with time shifts in this diagnostic; nonlinear or time-
    varying hardware requires a separate validation.
    """
    fs = _positive(sample_rate, "sample rate")
    phases = _integer(fractional_phases, "fractional phases")
    half = _integer(interpolation_half_support, "interpolation half support")
    work_limit = _integer(maximum_fft_work_points, "FFT work point limit")
    if phases > 64 or half > 256:
        raise ValueError("timing diagnostic exceeds bounded interpolation policy")
    low, high = map(float, arrival_bounds_seconds)
    if not math.isfinite(low) or not math.isfinite(high) or low > high:
        raise ValueError("arrival bounds must be finite and ordered")
    a = np.asarray(basis)
    if a.ndim == 3:
        if a.shape[2] != 1:
            raise NotImplementedError("bounded timing diagnostic currently supports one column per candidate")
        a = a[:, :, 0]
    if a.ndim != 2 or min(a.shape) < 1 or not np.iscomplexobj(a) or not np.isfinite(a).all():
        raise ValueError("timing basis must be finite complex [candidate,sample,1]")
    if a.shape[0] > 32 or a.nbytes > 1024*1024*1024:
        raise ValueError("timing bank exceeds bounded candidate/byte workspace")
    y = _finite_vector(y, complex_required=True)
    if a.shape[1] != len(y):
        raise ValueError("observation and timing bank sample counts differ")
    bits = _bits(candidate_bits, len(a))
    valid = np.ones(len(y), bool) if mask is None else np.asarray(mask, bool)
    if valid.shape != y.shape or not valid.any():
        raise ValueError("timing mask must retain samples on the fixed observation grid")
    first = math.ceil(low * fs * phases - 1e-10)
    last = math.floor(high * fs * phases + 1e-10)
    if last - first > 200000:
        raise ValueError("timing grid exceeds 200000 hypotheses")
    # Build/freeze the entire declared grid before examining correlations.
    delays = [float(k) / phases for k in range(first, last + 1)]
    delays.extend([low * fs, high * fs])
    delays = np.asarray(sorted(set(round(d, 12) for d in delays)), np.float64)
    groups = {}
    for index, delay in enumerate(delays):
        integer = math.floor(delay + 1e-11)
        fraction = round(delay - integer, 12)
        if abs(fraction) < 1e-11:
            fraction = 0.
        groups.setdefault(fraction, []).append((index, integer))
    # Full convolution has physical origin -half; no tails are cropped before
    # the linear correlation.  FFT size covers both complete finite supports.
    branch_samples = len(y) + 2 * half
    transform_samples = scipy_fft.next_fast_len(len(y) + branch_samples - 1)
    work_points = transform_samples * (2 + 4*len(groups)*len(a))
    if transform_samples > 4000000 or work_points > work_limit:
        raise ValueError("timing FFT shape/work exceeds the declared diagnostic resource cap")
    fy = scipy_fft.fft(np.asarray(y, np.complex128) * valid, transform_samples, workers=1)
    fm = scipy_fft.fft(valid.astype(np.float64), transform_samples, workers=1)
    scores = np.zeros((len(a), len(delays)), np.float64)
    energies = np.zeros_like(scores)
    observation_energy = _energy(y[valid])
    offsets = np.arange(-half, half + 1, dtype=np.float64)
    window = np.kaiser(2 * half + 1, 8.6)
    for fraction, positions in groups.items():
        kernel = np.sinc(offsets - fraction) * window
        kernel /= kernel.sum()
        indices = np.asarray([p[0] for p in positions], int)
        # correlate(y, branch) at lag integer-half represents the desired
        # branch[n-integer+half] = template[n-integer-fraction].
        lags = np.asarray([p[1] - half for p in positions], np.int64)
        wrapped = np.mod(lags, transform_samples)
        in_support = (lags >= -(branch_samples - 1)) & (lags <= len(y) - 1)
        for ci, candidate in enumerate(a):
            branch = signal.oaconvolve(np.asarray(candidate, np.complex128), kernel, mode="full")
            ft = scipy_fft.fft(branch, transform_samples, workers=1)
            correlation = scipy_fft.ifft(fy * ft.conj(), workers=1)[wrapped]
            fe = scipy_fft.fft(np.abs(branch) ** 2, transform_samples, workers=1)
            energy = scipy_fft.ifft(fm * fe.conj(), workers=1)[wrapped].real
            energy = np.where(in_support, np.maximum(energy, 0.), 0.)
            # Numerical FFT residuals at zero support must not become large
            # normalized scores through a nearly-zero denominator.
            active = energy > max(_energy(branch) * 1e-12, np.finfo(float).tiny)
            local = np.zeros(len(positions))
            local[active] = np.abs(correlation[active]) ** 2 / energy[active]
            scores[ci, indices], energies[ci, indices] = local, energy
    candidate_index, timing_index = np.unravel_index(int(np.argmax(scores)), scores.shape)
    per_bit = []
    bit_curves = []
    for bit in (0, 1):
        candidates = np.flatnonzero(np.asarray(bits) == bit)
        sub = scores[candidates]
        ci, ti = np.unravel_index(int(np.argmax(sub)), sub.shape)
        ci = int(candidates[ci])
        value = float(scores[ci, ti])
        per_bit.append(dict(bit=bit, candidate_index=ci,
            delay_seconds=float(delays[ti] / fs), projection_energy_samples=value,
            template_energy_samples=float(energies[ci, ti]),
            retained_observation_fraction=value / observation_energy if observation_energy else None))
        bit_curves.append(np.max(sub, axis=0))
    best = float(scores[candidate_index, timing_index])
    zero = np.flatnonzero(np.abs(delays) < 1e-11)
    return dict(schema="triad-live-bounded-timing-v1", chosen_bit=bits[candidate_index],
        candidate_index=int(candidate_index), selected_delay_seconds=float(delays[timing_index] / fs),
        positive_delay_convention="shifted_template[n]=template[n-delay_seconds*sample_rate]",
        public_arrival_bounds_seconds=[low, high], search_center_derived_from_payload=False,
        sample_rate=fs, regular_grid_step_seconds=1. / (fs * phases),
        timing_hypotheses=len(delays), candidate_hypotheses=len(a),
        fractional_regular_branches=phases, fractional_actual_branches=len(groups),
        includes_both_prior_endpoints=bool(abs(delays[0]/fs-low)<1e-11 and abs(delays[-1]/fs-high)<1e-11),
        valid_samples=int(valid.sum()), observation_energy_samples=observation_energy,
        best_projection_energy_samples=best,
        best_template_energy_samples=float(energies[candidate_index, timing_index]),
        best_residual_energy_samples=max(0., observation_energy-best),
        best_retained_observation_fraction=best / observation_energy if observation_energy else None,
        per_bit=per_bit, grid_delays_seconds=(delays/fs).tolist(),
        bit_projection_energy_curves=[x.tolist() for x in bit_curves],
        zero_delay_best_projection_energy_samples=float(np.max(scores[:, zero[0]])) if len(zero) else None,
        interpolation=dict(kind="zero-extended normalized Kaiser-windowed sinc", taps=2*half+1,
                           beta=8.6, full_filter_tails_retained=True),
        arithmetic="complex FP64 FFT correlations and FP64 masked template-energy reductions",
        fft_samples=transform_samples, dense_timing_bank_materialized=False,
        counted_main_fft_work_points=work_points, maximum_fft_work_points=work_limit,
        work_counter_scope="sum transform lengths for two cached observation/mask FFTs plus four main FFTs per candidate/fraction; excludes interpolation convolution",
        normalization="absolute squared masked correlation / shifted-template masked energy",
        validity_policy="same fixed observation mask for every candidate and delay; never shifted or payload-selected",
        operational_admission=None, false_admission_probability=None,
        limitations="single-column timing diagnostic; finite interpolation, fixed keyed epoch, frequency and clock; searches increase noise maxima; no operational acceptance calibration")


def _wilson(successes, trials, confidence):
    z = float(norm.ppf((1 + confidence) / 2))
    p = successes / trials
    den = 1 + z * z / trials
    center = (p + z * z / (2 * trials)) / den
    radius = z / den * math.sqrt(p * (1 - p) / trials + z * z / (4 * trials * trials))
    return [max(0., center - radius), min(1., center + radius)]


def _noise_factor(bank):
    covariance = bank.columns.conj().T @ bank.columns
    covariance = (covariance + covariance.conj().T) / 2
    values, vectors = np.linalg.eigh(covariance)
    tolerance = max(float(values[-1]), 1.) * 1e-12
    if values[0] < -tolerance:
        raise ValueError("joint projection-noise covariance is not positive semidefinite")
    active = values > tolerance
    return vectors[:, active] * np.sqrt(np.maximum(values[active], 0)), covariance


def validate_noise_projection(y, basis, *, seed=17, trials=3, sample_variance=1.,
                              mask=None, candidate_bits=None):
    """Independent full-sample least-squares check of retained linear statistics."""
    bank = _prepare_bank(basis, mask, candidate_bits)
    y = np.asarray(_finite_vector(y, complex_required=True), np.complex128)
    if len(y) != len(bank.valid):
        raise ValueError("observation and bank lengths differ")
    trials = _integer(trials, "sampled validation trials")
    variance = _positive(sample_variance, "sample variance")
    rng = np.random.default_rng(seed)
    errors = []
    ranking_equal = True
    for _ in range(trials):
        noise = math.sqrt(variance / 2) * (rng.standard_normal(len(y)) + 1j * rng.standard_normal(len(y)))
        sample = y + noise
        compressed = bank.scores(bank.correlations(y) + bank.correlations(noise))
        full = []
        target = sample[bank.valid]
        for candidate in bank.basis:
            a = np.asarray(candidate[bank.valid], np.complex128)
            coefficients = np.linalg.lstsq(a, target,
                rcond=bank.rank_policy["effective_relative_threshold"])[0]
            fitted = a @ coefficients
            full.append(float(np.vdot(fitted, fitted).real))
        full = np.asarray(full)
        errors.append(float(np.max(np.abs(compressed - full)) / max(float(np.max(full)), 1e-30)))
        ranking_equal &= bool(bank.bit_choices(compressed)[0] == bank.bit_choices(full)[0])
    return dict(sampled_trials=trials, maximum_relative_score_error=max(errors),
                bit_rankings_equal=ranking_equal, sample_variance=variance,
                rank_policy=bank.rank_policy,
                scope="same explicit full-sample noise compared with independent least-squares projections; not an operational detection calibration")


def noise_margin_comparison(real, pred, basis, cn0_grid, seed, trials=256, *,
                            sample_rate, duration_seconds, reference_power,
                            truth_bit, candidate_bits=None, mask=None,
                            confidence=.95, target_correct=.9, sampled_validation_trials=3):
    """Cheap paired additive-white-noise classification curves.

    C/N0 means ``reference_power / N0``.  Complex sampled noise has variance
    ``N0 * sample_rate``.  The SAME reference power and noise draw are used for
    real and predicted observations; hardware loss is not divided away.
    Existing physical-capture residuals remain fixed in these conditional trials.
    """
    fs = _positive(sample_rate, "sample rate")
    duration = _positive(duration_seconds, "useful reference duration")
    power = _positive(reference_power, "common reference power")
    trials = _integer(trials, "noise trials")
    if trials > 1000000 or not 0 < confidence < 1 or not .5 < target_correct < 1:
        raise ValueError("invalid Monte Carlo count/confidence/target")
    if truth_bit not in (0, 1):
        raise ValueError("truth_bit is only the evaluation label 0 or 1")
    grid = [float(x) for x in cn0_grid]
    if not grid or len(set(grid)) != len(grid) or not np.isfinite(grid).all():
        raise ValueError("C/N0 grid must be finite, nonempty and distinct")
    bank = _prepare_bank(basis, mask, candidate_bits)
    real, pred = _finite_vector(real, complex_required=True), _finite_vector(pred, complex_required=True)
    r_real, r_pred = bank.correlations(real), bank.correlations(pred)
    result = dict(schema="triad-live-conditional-noise-margin-v1", operational_admission=None,
        false_admission_probability=None, independent_hardware_observations=1,
        conditional_noise_trials=trials, pointwise_confidence=confidence,
        confidence_scope="pointwise Wilson intervals for conditional independent generated-noise trials; grids share draws and are not independent hardware replications",
        noise_model="proper complex white additive noise after the measured capture/DDC path",
        cn0_definition="10*log10(common_reference_power/N0); complex sample variance=N0*sample_rate",
        reference_power=power, reference_duration_seconds=duration,
        sample_rate=fs, observation_duration_seconds=len(real) / fs,
        valid_observation_seconds=float(bank.valid.sum()) / fs,
        real_observation_energy_seconds=_energy(real[bank.valid]) / fs,
        predicted_observation_energy_seconds=_energy(pred[bank.valid]) / fs,
        fixed_capture_residual_is_reused=True, truth_bit=int(truth_bit),
        identifiability=bank.identifiability, candidate_ranks=bank.ranks,
        rank_policy=bank.rank_policy,
        target_correct=target_correct, rows=[])
    if bank.identifiability["ambiguous_cross_bit_pairs"]:
        result.update(status="unidentifiable_bank", limitation="cross-bit nuisance subspaces are identical; no sensitivity curve is claimed")
        return result
    factor, covariance = _noise_factor(bank)
    rng = np.random.default_rng(seed)
    z = (rng.standard_normal((trials, factor.shape[1])) + 1j * rng.standard_normal((trials, factor.shape[1]))) / math.sqrt(2)
    joint_noise = z @ factor.T
    result["joint_noise_covariance_rank"] = factor.shape[1]
    result["joint_noise_covariance_real"] = covariance.real.tolist()
    result["joint_noise_covariance_imag"] = covariance.imag.tolist()
    for cn0 in sorted(grid):
        variance = power * fs * 10 ** (-cn0 / 10)
        if not math.isfinite(variance) or variance <= 0:
            raise ValueError("C/N0 produces unrepresentable noise variance")
        nr = math.sqrt(variance) * joint_noise
        live_bits, _ = bank.bit_choices(bank.scores(r_real[None] + nr))
        model_bits, _ = bank.bit_choices(bank.scores(r_pred[None] + nr))
        live_correct, model_correct = live_bits == truth_bit, model_bits == truth_bit
        a, b = int(live_correct.sum()), int(model_correct.sum())
        result["rows"].append(dict(cn0_db_hz=cn0, complex_sample_noise_variance=variance,
            reference_energy_to_noise_db=cn0 + 10 * math.log10(duration),
            real_correct=a, real_wrong=trials - a, real_correct_fraction=a / trials,
            real_correct_interval=_wilson(a, trials, confidence),
            predicted_correct=b, predicted_wrong=trials - b, predicted_correct_fraction=b / trials,
            predicted_correct_interval=_wilson(b, trials, confidence),
            real_only_correct=int(np.count_nonzero(live_correct & ~model_correct)),
            predicted_only_correct=int(np.count_nonzero(model_correct & ~live_correct)),
            misses=None, admissions=None))
    brackets = {}
    for side in ("real", "predicted"):
        below = [r["cn0_db_hz"] for r in result["rows"] if r[side + "_correct_interval"][1] < target_correct]
        above = [r["cn0_db_hz"] for r in result["rows"] if r[side + "_correct_interval"][0] >= target_correct]
        brackets[side] = dict(last_grid_point_confidently_below_target=max(below) if below else None,
                              first_grid_point_confidently_at_target=min(above) if above else None)
    result["grid_brackets"] = brackets
    result["bracket_scope"] = "pointwise sampled grid statements; no interpolation, monotonicity proof or simultaneous crossing confidence"
    result["status"] = "conditional_diagnostic_complete"
    if sampled_validation_trials:
        result["sampled_noise_crosscheck"] = validate_noise_projection(real, basis, seed=seed ^ 0x51A7,
            trials=sampled_validation_trials, sample_variance=power * fs * 10 ** (-sorted(grid)[len(grid) // 2] / 10),
            mask=mask, candidate_bits=candidate_bits)
    return result
