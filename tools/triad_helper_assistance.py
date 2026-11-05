#!/usr/bin/env python3
"""Conditional Helper CFO narrowing for the isolated phase-continuous study.

Only raw, noisy DATA-centered complex samples and public configuration enter
this estimator. The fitted finite line model has the selected sinusoidal
modulation of the two rays at 0 and +D; it is not a stochastic-channel model.
Fake FHSS may gate the source, but its gate harmonics are NOT fitted here.
Gated operation therefore needs separate validation across source gate phases.
The returned interval is an engineering allowance, not calibrated confidence,
and the Gaussian presence diagnostic is not a formal pilot-lock decision.
"""
from __future__ import annotations

import math
import time

import numpy as np
from scipy import fft
from scipy.special import gammainccinv

MAX_GRID_BINS = 65536
GRAM_RELATIVE_RANK_TOLERANCE = 1e-10
COMPETITIVE_SCORE_DROP = 18.0
FFT_DOT_RELATIVE_TOLERANCE = 0.003


def _number(value, name, *, minimum=None, positive=False):
    if isinstance(value, (bool, np.bool_)):
        raise ValueError(f"{name} must be a finite number")
    try:
        result = float(value)
    except (TypeError, ValueError, OverflowError) as error:
        raise ValueError(f"{name} must be a finite number") from error
    if not math.isfinite(result):
        raise ValueError(f"{name} must be a finite number")
    if positive and result <= 0:
        raise ValueError(f"{name} must be positive")
    if minimum is not None and result < minimum:
        raise ValueError(f"{name} must be >= {minimum}")
    return result


def tone_offsets(doppler_hz, slow_depth, fast_depth):
    """Public two-ray/modulation union, including coincident lines only once.

    Integer tenths avoid floating-point duplicate representations. With both
    sinusoidal components active and D > 0 there are exactly eight lines.
    Depth selects a component; its unknown complex coefficients are fitted.
    """
    doppler = _number(doppler_hz, "doppler_hz", minimum=0)
    slow = _number(slow_depth, "slow_depth", minimum=0)
    fast = _number(fast_depth, "fast_depth", minimum=0)
    sidebands = {0}
    if slow:
        sidebands.update((-1, 1))
    if fast:
        sidebands.update((-10, 10))
    if doppler == 0:
        return np.array([0.], dtype=np.float64)
    return np.array(sorted({ray + side for ray in (0, 10) for side in sidebands}),
                    dtype=np.float64) * (doppler / 10.)


def tone_gram(frequencies, sample_count, sample_rate):
    """Exact FP64 rectangular Gram of unit-norm complex exponential columns."""
    frequencies = np.asarray(frequencies, dtype=np.float64)
    delta = (frequencies[None, :] - frequencies[:, None]) / sample_rate
    return (np.exp(1j * np.pi * (sample_count - 1) * delta)
            * np.sinc(sample_count * delta) / np.sinc(delta))


def _whitener(frequencies, sample_count, sample_rate):
    gram = tone_gram(frequencies, sample_count, sample_rate)
    eigenvalues, vectors = np.linalg.eigh(gram)
    keep = eigenvalues > GRAM_RELATIVE_RANK_TOLERANCE * eigenvalues[-1]
    return vectors[:, keep] / np.sqrt(eigenvalues[keep]), int(np.count_nonzero(keep))


def _interpolate_fft(spectrum, frequencies, sample_rate, sample_count):
    """Four-point Lagrange interpolation after rectangular-window centering.

    Centering removes the known linear phase before interpolation and restores
    it at the requested frequency. The phase difference uses unwrapped knot
    coordinates even though lookup into the raw FFT is periodic.
    """
    coordinate = np.asarray(frequencies, dtype=np.float64) * (len(spectrum) / sample_rate)
    lower = np.floor(coordinate).astype(np.int64)
    fraction = coordinate - lower
    result = np.zeros(coordinate.shape, dtype=np.complex128)
    for offset in (-1, 0, 1, 2):
        weight = np.ones(coordinate.shape, dtype=np.float64)
        for other in (-1, 0, 1, 2):
            if other != offset:
                weight *= (fraction - other) / (offset - other)
        centered_phase = np.exp(1j * np.pi * (sample_count - 1)
                                * (offset - fraction) / len(spectrum))
        result += weight * spectrum[(lower + offset) % len(spectrum)] * centered_phase
    return result / math.sqrt(sample_count)


def _projection_scores(z, frequencies, sample_count, sample_rate, noise_variance):
    whitener, rank = _whitener(frequencies, sample_count, sample_rate)
    projected = z @ whitener.conj()
    return np.sum(np.abs(projected) ** 2, axis=1) / noise_variance, rank


def _profile(samples, frequencies, grid, sample_rate, noise_variance):
    # scipy.fft preserves complex64; only the small interpolation/Gram/projection
    # calculation is FP64. No N-by-grid or N-by-tone design matrix is allocated.
    spectrum = fft.fft(np.asarray(samples, dtype=np.complex64),
                       n=fft.next_fast_len(8 * len(samples)), workers=2)
    z = _interpolate_fft(spectrum, grid[:, None] + frequencies[None, :],
                         sample_rate, len(samples))
    scores, rank = _projection_scores(z, frequencies, len(samples), sample_rate, noise_variance)
    return scores, rank, z, spectrum


def _basins(grid, scores):
    """Retain every connected interval within the fixed competitive score drop."""
    competitive = scores >= np.max(scores) - COMPETITIVE_SCORE_DROP
    starts = np.flatnonzero(competitive & np.r_[True, ~competitive[:-1]])
    ends = np.flatnonzero(competitive & np.r_[~competitive[1:], True])
    return [(int(a), int(b)) for a, b in zip(starts, ends)]


def _bank_subset(bank, intervals):
    """Preserve original ordering and one original bank neighbor on each side."""
    order = np.argsort(bank)
    ordered = bank[order]
    keep = np.zeros(len(bank), dtype=bool)
    for low, high in intervals:
        first = int(np.searchsorted(ordered, low, side="left"))
        past = int(np.searchsorted(ordered, high, side="right"))
        keep[order[max(0, first - 1):min(len(bank), past + 1)]] = True
    return bank[keep]


def estimate_frequency(samples, *, sample_rate=18000, data_if_hz,
                       low_pilot_hz=550, high_pilot_hz, original_frequencies,
                       prior_half_width_hz, doppler_hz, slow_depth, fast_depth,
                       noise_variance, first_sample_seconds,
                       observation_seconds=36, audio_error_bound_ppm=.0001,
                       phase_diffusion_radians_per_sqrt_second,
                       drift_bound_hz_per_second=0, model_allowance_hz=.01,
                       pilot_dwell_gating=False, physical_rf_retuning=False,
                       pilot_phase_model="continuous"):
    """Return a JSON-serializable conditional CFO diagnostic and DATA subset.

    The input starts at ``first_sample_seconds`` on the capture calendar. Only
    the first ceil(observation_seconds * sample_rate) samples are consumed;
    they must cover at least 36 seconds. Availability is dated to their end,
    never to the start of DATA. ``original_frequencies`` is the unmodified
    public DATA CFO bank inside the symmetric physical prior. Its ordering is
    preserved. A failure to safely shrink returns that entire original bank.

    The known noise variance is per complex raw sample. The pilot centers are
    physical audio frequencies, translated by the public DATA IF. Arbitrary
    per-slot phases and physical RF retuning are unsupported and cause fallback.
    Unknown complex line coefficients, including arbitrary initial phase, are
    profiled jointly at every CFO; neither source bits nor truth are accepted.
    """
    started = time.perf_counter()
    rate = _number(sample_rate, "sample_rate", positive=True)
    data_if = _number(data_if_hz, "data_if_hz")
    low = _number(low_pilot_hz, "low_pilot_hz")
    high = _number(high_pilot_hz, "high_pilot_hz")
    if high <= low:
        raise ValueError("high_pilot_hz must exceed low_pilot_hz")
    prior = _number(prior_half_width_hz, "prior_half_width_hz", minimum=0)
    variance = _number(noise_variance, "noise_variance", positive=True)
    first_time = _number(first_sample_seconds, "first_sample_seconds")
    duration_requested = _number(observation_seconds, "observation_seconds", minimum=36)
    audio_ppm = _number(audio_error_bound_ppm, "audio_error_bound_ppm", minimum=0)
    phase_diffusion = _number(phase_diffusion_radians_per_sqrt_second,
                              "phase_diffusion_radians_per_sqrt_second", minimum=0)
    drift = _number(drift_bound_hz_per_second, "drift_bound_hz_per_second", minimum=0)
    model_allowance = _number(model_allowance_hz, "model_allowance_hz", minimum=0)
    offsets = tone_offsets(doppler_hz, slow_depth, fast_depth)
    for name, value in (("pilot_dwell_gating", pilot_dwell_gating),
                        ("physical_rf_retuning", physical_rf_retuning)):
        if not isinstance(value, (bool, np.bool_)):
            raise ValueError(f"{name} must be boolean")
    if not isinstance(pilot_phase_model, str):
        raise ValueError("pilot_phase_model must be a string")
    bank_input = np.asarray(original_frequencies)
    if bank_input.ndim != 1 or bank_input.dtype.kind not in "fiu" or not bank_input.size:
        raise ValueError("original_frequencies must be a nonempty real vector")
    bank = bank_input.astype(np.float64)
    if not np.isfinite(bank).all() or len(np.unique(bank)) != len(bank):
        raise ValueError("original_frequencies must be finite and unique")
    if np.any(np.abs(bank) > prior + 1e-12 * max(1., prior)):
        raise ValueError("original_frequencies must lie inside the physical prior")
    values = np.asarray(samples)
    if values.ndim != 1 or values.dtype.kind != "c" or not np.isfinite(values).all():
        raise ValueError("samples must be a finite complex vector")
    requested_rows = rate * duration_requested
    if not math.isfinite(requested_rows) or requested_rows > len(values):
        raise ValueError("samples do not cover the requested observation")
    count = int(math.ceil(requested_rows))
    values = values[:count]
    duration = count / rate
    centers = np.array([low - data_if, high - data_if])
    frequencies = (centers[:, None] + offsets).reshape(-1)
    if np.min(frequencies) - prior <= -rate / 2 or np.max(frequencies) + prior >= rate / 2:
        raise ValueError("pilot lines and physical CFO prior must lie strictly inside Nyquist")
    result = {
        "status": "fallback", "reason": "", "selected_frequencies": bank.tolist(),
        "selected_count": len(bank), "original_count": len(bank),
        "profile_grid_hz": [], "profile_scores": [], "peaks_hz": {}, "rank": 0,
        "competitive_intervals_hz": [], "expanded_intervals_hz": [],
        "available_at": first_time + duration, "observation_seconds": duration,
        "sample_count": count, "prior_half_width_hz": prior,
        "tone_offsets_hz": offsets.tolist(), "tone_frequencies_hz": frequencies.tolist(),
        "gram_relative_rank_tolerance": GRAM_RELATIVE_RANK_TOLERANCE,
        "competitive_score_drop": COMPETITIVE_SCORE_DROP,
        "interval_kind": "conditional engineering interval, not calibrated confidence",
        "model": "phase-continuous selected sinusoidal two-ray model only",
        "stochastic_channel_claim": False, "presence_is_formal_pilot_lock": False,
        "pilot_phase_model": pilot_phase_model,
        "pilot_dwell_gating": bool(pilot_dwell_gating),
        "physical_rf_retuning": bool(physical_rf_retuning),
        "gating_harmonics_modeled": False,
        "gate_phase_validation_required": bool(pilot_dwell_gating),
        "limitations": ["Source gating harmonics are unmodeled; gated operation needs independent validation across gate phases.",
                        "Auxiliary modulation and gating are outside the Gaussian presence diagnostic."],
        "crosschecks": {}, "fft_dot_relative_errors": [],
    }

    def finish(reason):
        result["reason"] = reason
        result["wall_seconds"] = time.perf_counter() - started
        return result

    if physical_rf_retuning:
        return finish("physical_rf_retuning_unsupported")
    if pilot_phase_model != "continuous":
        return finish("unknown_or_noncontinuous_pilot_phase_model")
    # Endpoint-inclusive coverage cannot coarsen the specified 1/(8T) step.
    intervals_count = math.ceil(2 * prior * 8 * duration)
    if intervals_count + 1 > MAX_GRID_BINS:
        return finish("profile_grid_limit")
    grid = np.linspace(-prior, prior, intervals_count + 1)
    step = float(grid[1] - grid[0]) if len(grid) > 1 else 0.
    allowance = (model_allowance + float(np.max(np.abs(frequencies))) * audio_ppm * 1e-6
                 + 6 * phase_diffusion / (2 * np.pi * math.sqrt(duration))
                 + drift * duration + step)
    result.update(grid_step_hz=step, allowance_hz=allowance,
                  peak_agreement_tolerance_hz=max(.04, 2 * allowance))
    scores, rank, z, spectrum = _profile(values, frequencies, grid, rate, variance)
    result.update(profile_grid_hz=grid.tolist(), profile_scores=scores.tolist(), rank=rank)
    best = int(np.argmax(scores))
    best_cfo = float(grid[best])
    result["peaks_hz"]["joint"] = best_cfo
    threshold = float(gammainccinv(rank, .001 / len(grid)))
    result["presence_threshold"] = threshold
    result["presence_score"] = float(scores[best])
    basins = _basins(grid, scores)
    result["competitive_intervals_hz"] = [[float(grid[a]), float(grid[b])] for a, b in basins]
    expanded = [[max(-prior, float(grid[a]) - allowance),
                 min(prior, float(grid[b]) + allowance)] for a, b in basins]
    result["expanded_intervals_hz"] = expanded
    if not np.isfinite(scores).all():
        return finish("nonfinite_profile")
    if scores[best] < threshold:
        return finish("insufficient_presence_diagnostic")
    if len(grid) == 1 or float(np.ptp(scores)) <= COMPETITIVE_SCORE_DROP:
        return finish("flat_profile")
    if any(a == 0 or b == len(grid) - 1 for a, b in basins):
        return finish("competitive_basin_at_prior_boundary")
    if len(basins) != 1:
        return finish("multiple_competitive_basins")

    # An actual direct dot guards interpolation at both unshifted pilot lines.
    # It is a numerical check only; these samples are the same noisy observation.
    coordinates = np.arange(count, dtype=np.float64) / rate
    approximate = _interpolate_fft(spectrum, centers + best_cfo, rate, count)
    for frequency, estimate in zip(centers + best_cfo, approximate):
        direct = np.dot(np.exp(-2j * np.pi * frequency * coordinates), values) / math.sqrt(count)
        error = float(abs(estimate - direct) / max(abs(direct), math.sqrt(variance)))
        result["fft_dot_relative_errors"].append(error)
    del spectrum, coordinates
    if max(result["fft_dot_relative_errors"]) > FFT_DOT_RELATIVE_TOLERANCE:
        return finish("fft_direct_dot_disagreement")

    profiles = []
    lines = len(offsets)
    for name, selection in (("low", slice(0, lines)), ("high", slice(lines, 2 * lines))):
        separate_scores, separate_rank = _projection_scores(
            z[:, selection], frequencies[selection], count, rate, variance)
        profiles.append((name, separate_scores, separate_rank))
    del z
    # Each disjoint half has its own rectangular window, FFT and exact Gram.
    midpoint = count // 2
    for name, half in (("first_half", values[:midpoint]), ("second_half", values[midpoint:])):
        half_scores, half_rank, _, half_spectrum = _profile(half, frequencies, grid, rate, variance)
        profiles.append((name, half_scores, half_rank))
        del half_spectrum
    for name, cross_scores, cross_rank in profiles:
        cross_best = int(np.argmax(cross_scores))
        peak = float(grid[cross_best])
        cross_threshold = float(gammainccinv(cross_rank, .001 / len(grid)))
        result["peaks_hz"][name] = peak
        result["crosschecks"][name] = {
            "peak_hz": peak, "rank": cross_rank, "score": float(cross_scores[cross_best]),
            "presence_threshold": cross_threshold,
        }
    if any(check["score"] < check["presence_threshold"] for check in result["crosschecks"].values()):
        return finish("crosscheck_insufficient_presence")
    peaks = list(result["peaks_hz"].values())
    if max(peaks) - min(peaks) > result["peak_agreement_tolerance_hz"]:
        return finish("pilot_or_half_peak_disagreement")
    selected = _bank_subset(bank, expanded)
    if len(selected) >= len(bank):
        return finish("no_bank_shrink")
    result.update(status="narrowed", selected_frequencies=selected.tolist(), selected_count=len(selected))
    return finish("conditional_model_checks_passed")
