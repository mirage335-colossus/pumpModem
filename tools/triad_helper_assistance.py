#!/usr/bin/env python3
"""Conditional Helper CFO narrowing for the isolated phase-continuous study.

Only raw, noisy DATA-centered complex samples and public configuration enter
this estimator. The fitted finite line model has the selected sinusoidal
modulation of the two rays at 0 and +D; it is not a stochastic-channel model.
Fake FHSS may gate the source, but its gate harmonics are NOT fitted here.
Gated operation therefore needs separate validation across source gate phases.
The returned interval is an engineering allowance, not calibrated confidence,
and the Gaussian presence diagnostic is not a formal pilot-lock decision.
Broad audio-clock bounds select a bounded two-pilot proposal/refinement fit.
It keeps TX/RX source stretch distinct from the physical channel clock's
1/RX stretch; its global continuous-search completeness is not established.
"""
from __future__ import annotations

import math
import time

import numpy as np
from scipy import fft
from scipy.optimize import minimize
from scipy.special import gammainccinv

MAX_GRID_BINS = 65536
GRAM_RELATIVE_RANK_TOLERANCE = 1e-10
COMPETITIVE_SCORE_DROP = 18.0
FFT_DOT_RELATIVE_TOLERANCE = 0.003
MAX_JOINT_SEEDS = 256
JOINT_OPTIMIZER_XTOL_HZ = 2e-6
JOINT_OPTIMIZER_FTOL = 1e-7


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


def _joint_clock_score(spectrum, centers, offsets, cfo, clock_ppm,
                       doppler_scale, count, rate, variance):
    """Finite-line likelihood with independent source and propagation clocks.

    Source audio frequencies stretch by TX/RX. The simulated propagation and
    selective-AM clocks run in physical seconds and stretch by 1/RX instead.
    The common CFO cancels from the Gram, but no cross-pilot Gram terms are
    discarded. Unknown complex line gains are fitted, not their phases copied
    to DATA. Only the small interpolation, Gram and fit use FP64.
    """
    frequencies = (centers[:, None] * (1 + clock_ppm * 1e-6)
                   + offsets[None, :] * doppler_scale).reshape(-1)
    z = _interpolate_fft(spectrum, frequencies + cfo, rate, count)[None, :]
    score, rank = _projection_scores(z, frequencies, count, rate, variance)
    return float(score[0]), rank, frequencies, z[0]


def _estimate_joint_clock(values, *, rate, centers, offsets, bank, prior,
                          variance, duration, audio_ppm, channel_rate_ppm,
                          phase_diffusion, drift, model_allowance, result,
                          finish, check_halves=True):
    """Bounded pilot proposals followed by exact joint local refinement.

    Independent pilot profiles supply *all* competitive basin pairs compatible
    with the public clock/CFO bounds. They are proposals rather than a proof of
    continuous global maximization. The final fit includes the complete Gram.
    Multiple surviving basins, insufficient pilots or inconsistent half-window
    fits retain the original DATA bank. The returned allowances are engineering
    envelopes; they are not a confidence region or an operational lock test.
    """
    count = len(values)
    separation = float(centers[1] - centers[0])
    max_offset = float(np.max(np.abs(offsets)))
    max_center = float(np.max(np.abs(centers)))
    step_limit = 1 / (8 * duration)
    clock_extent = max_center * audio_ppm * 1e-6
    extent = prior + clock_extent
    intervals = max(1, math.ceil(2 * extent / step_limit))
    result.update(clock_status="fallback", clock_estimate_ppm=None,
                  expanded_clock_intervals_ppm=[[-audio_ppm, audio_ppm]],
                  doppler_scale_estimate=None,
                  expanded_doppler_scale_intervals=[
                      [1 - channel_rate_ppm * 1e-6, 1 + channel_rate_ppm * 1e-6]],
                  joint_clock_model="c + (pilot-IF)*(TXaudio/RXaudio) + offset/RXaudio",
                  joint_search="bounded independent-pilot proposals plus full-Gram local refinement",
                  joint_search_is_exhaustive=False,
                  channel_rate_error_bound_ppm=channel_rate_ppm,
                  optimizer_xtol_hz=JOINT_OPTIMIZER_XTOL_HZ,
                  optimizer_ftol=JOINT_OPTIMIZER_FTOL)
    if intervals + 1 > MAX_GRID_BINS:
        return finish("profile_grid_limit")
    if audio_ppm >= 1e6 or channel_rate_ppm >= 1e6:
        raise ValueError("joint clock bounds must be below one million ppm")
    grid = np.linspace(-extent, extent, intervals + 1)
    step = float(grid[1] - grid[0])
    # Doppler-grid spacing has the same <=1/(8T) worst-line frequency budget.
    scale_intervals = (max(1, math.ceil(2 * channel_rate_ppm * 1e-6
                                      * max_offset / step_limit))
                       if max_offset and channel_rate_ppm else 0)
    scales = (np.linspace(1 - channel_rate_ppm * 1e-6,
                          1 + channel_rate_ppm * 1e-6, scale_intervals + 1)
              if scale_intervals else np.array([1.]))
    if len(scales) > 129:
        return finish("channel_rate_grid_limit")
    edge = max_center * (1 + audio_ppm * 1e-6) + prior
    edge += max_offset * (1 + channel_rate_ppm * 1e-6)
    if edge >= rate / 2:
        raise ValueError("joint pilot/clock/CFO support must lie strictly inside Nyquist")
    allowance = (model_allowance + 6 * phase_diffusion
                 / (2 * np.pi * math.sqrt(duration)) + drift * duration + step)
    clock_allowance = 2 * allowance / separation * 1e6
    result.update(grid_step_hz=step, allowance_hz=allowance,
                  clock_allowance_ppm=clock_allowance,
                  coarse_doppler_scales=scales.tolist(),
                  peak_agreement_tolerance_hz=max(.04, 2 * allowance),
                  profile_grid_hz=grid.tolist())
    spectrum = fft.fft(np.asarray(values, dtype=np.complex64),
                       n=fft.next_fast_len(8 * count), workers=2)
    _, identifiable_rank = _whitener(
        (centers[:, None] + offsets[None, :]).reshape(-1), count, rate)
    result["nominal_joint_rank"] = identifiable_rank
    if identifiable_rank != len(centers) * len(offsets):
        return finish("joint_line_model_rank_deficient")
    seeds = []
    independent_best = np.full((len(scales), 2), -np.inf)
    independent_thresholds = np.zeros_like(independent_best)
    profile_maximum = np.full(len(grid), -np.inf)
    coarse_calls = 0
    for si, scale in enumerate(scales):
        profiles = []
        for pi, center in enumerate(centers):
            frequencies = center + scale * offsets
            z = _interpolate_fft(spectrum, grid[:, None] + frequencies,
                                 rate, count)
            scores, rank = _projection_scores(z, frequencies, count, rate, variance)
            coarse_calls += len(grid)
            independent_best[si, pi] = float(np.max(scores))
            independent_thresholds[si, pi] = gammainccinv(
                rank, .001 / (len(grid) * len(scales)))
            profiles.append((scores, _basins(grid, scores)))
        # This is a plotting diagnostic at equal pilot shift, not the joint
        # clock likelihood. Do not use it to prune clock-consistent pairs.
        profile_maximum = np.maximum(profile_maximum, profiles[0][0] + profiles[1][0])
        if np.any(independent_best[si] < independent_thresholds[si]):
            continue
        for a, b in profiles[0][1]:
            li = a + int(np.argmax(profiles[0][0][a:b + 1]))
            for d, e in profiles[1][1]:
                hi = d + int(np.argmax(profiles[1][0][d:e + 1]))
                clock = (grid[hi] - grid[li]) / separation * 1e6
                cfo = float((centers[1] * grid[li] - centers[0] * grid[hi]) / separation)
                uncertainty = 2 * step / separation * 1e6
                if abs(clock) <= audio_ppm + uncertainty and abs(cfo) <= prior + 2 * step:
                    seeds.append((cfo, clock, float(scale),
                                  float(grid[b] - grid[a]), float(grid[e] - grid[d])))
        if len(seeds) > MAX_JOINT_SEEDS:
            return finish("joint_seed_limit")
    result.update(profile_scores=profile_maximum.tolist(),
                  independent_pilot_max_scores=independent_best.tolist(),
                  independent_pilot_presence_thresholds=independent_thresholds.tolist(),
                  coarse_profile_evaluations=coarse_calls,
                  joint_seed_count=len(seeds))
    if not seeds:
        return finish("joint_no_clock_consistent_present_pilots")

    # Normalize all three optimizer coordinates to Hz-scale effects. Tight
    # local boxes preserve every coarse proposal and prevent a line-search
    # jump to a remote comb alias. Every seed is retained through refinement.
    clock_unit = separation * 1e-6
    scale_step = float(scales[1] - scales[0]) if len(scales) > 1 else 0.
    refined = []

    def refine(spec, rows, seed):
        cfo, clock, scale, lw, hw = seed
        local_step = 1 / (8 * (rows / rate))
        c_radius = max(4 * local_step, lw + hw)
        q_radius = max(4 * local_step / clock_unit,
                       (lw + hw) / clock_unit)
        r_radius = max(scale_step * 1.6, 1e-12)
        lower = np.array([max(-prior, cfo - c_radius),
                          max(-audio_ppm, clock - q_radius) * clock_unit,
                          max(1 - channel_rate_ppm * 1e-6, scale - r_radius) - 1])
        upper = np.array([min(prior, cfo + c_radius),
                          min(audio_ppm, clock + q_radius) * clock_unit,
                          min(1 + channel_rate_ppm * 1e-6, scale + r_radius) - 1])
        lower[2] *= max_offset if max_offset else 1
        upper[2] *= max_offset if max_offset else 1
        if not max_offset:
            lower[2] = upper[2] = 0.
        initial = np.array([cfo, clock * clock_unit,
                            (scale - 1) * (max_offset if max_offset else 1)])
        initial = np.clip(initial, lower, upper)
        free = np.flatnonzero(upper > lower)

        def unpack(x):
            parameters = initial.copy()
            parameters[free] = x
            return (float(parameters[0]), float(parameters[1] / clock_unit),
                    float(1 + parameters[2] / (max_offset if max_offset else 1)))

        def objective(x):
            return -_joint_clock_score(spec, centers, offsets, *unpack(x),
                                       rows, rate, variance)[0]

        fit = minimize(objective, initial[free], method="Powell",
                       bounds=list(zip(lower[free], upper[free])),
                       options={"xtol": JOINT_OPTIMIZER_XTOL_HZ,
                                "ftol": JOINT_OPTIMIZER_FTOL, "maxiter": 40})
        parameters = unpack(fit.x)
        score, rank, frequencies, dots = _joint_clock_score(
            spec, centers, offsets, *parameters, rows, rate, variance)
        return dict(cfo_hz=parameters[0], clock_ppm=parameters[1],
                    doppler_scale=parameters[2], score=score, rank=rank,
                    optimizer_success=bool(fit.success), evaluations=int(fit.nfev),
                    coarse_cfo_radius_hz=(abs(centers[1]) * lw
                                          + abs(centers[0]) * hw) / separation,
                    coarse_clock_radius_ppm=(lw + hw) / separation * 1e6,
                    frequencies=frequencies, dots=dots)

    for seed in seeds:
        refined.append(refine(spectrum, count, seed))
    refined.sort(key=lambda fit: fit["score"], reverse=True)
    best = refined[0]
    competitive = [fit for fit in refined if fit["score"] >= best["score"] - COMPETITIVE_SCORE_DROP]
    result.update(joint_refinement_evaluations=sum(fit["evaluations"] for fit in refined),
                  joint_candidates=[{k: v for k, v in fit.items()
                                     if k not in ("frequencies", "dots")}
                                    for fit in refined],
                  rank=best["rank"], presence_score=best["score"],
                  presence_threshold=float(gammainccinv(
                      best["rank"], .001 / max(1, coarse_calls))),
                  clock_estimate_ppm=best["clock_ppm"],
                  doppler_scale_estimate=best["doppler_scale"])
    result["peaks_hz"]["joint"] = best["cfo_hz"]
    if not all(math.isfinite(fit["score"]) for fit in refined):
        return finish("nonfinite_profile")
    if best["score"] < result["presence_threshold"]:
        return finish("insufficient_presence_diagnostic")
    # An unsuccessful refinement cannot eliminate its basin, even when its
    # last evaluated score is below the current winner.
    if any(not fit["optimizer_success"] for fit in refined):
        return finish("joint_refinement_failed")
    if any(abs(fit["cfo_hz"]) >= prior - step or
           abs(fit["clock_ppm"]) >= audio_ppm - 2 * step / clock_unit
           for fit in competitive):
        return finish("competitive_basin_at_prior_boundary")
    c_min = min(fit["cfo_hz"] for fit in competitive)
    c_max = max(fit["cfo_hz"] for fit in competitive)
    q_min = min(fit["clock_ppm"] for fit in competitive)
    q_max = max(fit["clock_ppm"] for fit in competitive)
    # Duplicate r-grid proposals normally converge onto the same basin. More
    # distant competitive solutions are unresolved aliases, not evidence to
    # average. Preserve all hypotheses by falling back.
    if c_max - c_min > 2 * allowance or q_max - q_min > 2 * clock_allowance:
        return finish("multiple_competitive_joint_basins")
    # Propagate the entire competitive coarse-pilot basin widths, rather than
    # representing each basin only by its best-fitting point. The full width
    # on either side is conservative for an asymmetrically sampled basin.
    c_low = min(fit["cfo_hz"] - fit["coarse_cfo_radius_hz"] for fit in competitive)
    c_high = max(fit["cfo_hz"] + fit["coarse_cfo_radius_hz"] for fit in competitive)
    q_low = min(fit["clock_ppm"] - fit["coarse_clock_radius_ppm"] for fit in competitive)
    q_high = max(fit["clock_ppm"] + fit["coarse_clock_radius_ppm"] for fit in competitive)
    expanded = [[max(-prior, c_low - allowance), min(prior, c_high + allowance)]]
    clock_intervals = [[max(-audio_ppm, q_low - clock_allowance),
                        min(audio_ppm, q_high + clock_allowance)]]
    result.update(competitive_intervals_hz=[[c_min, c_max]],
                  expanded_intervals_hz=expanded,
                  coarse_likelihood_region_widths_propagated=True)
    # Leave the channel-rate nuisance bank intact. Its line spacing is useful
    # for fitting but its engineering uncertainty has not been calibrated.
    result["doppler_scale_narrowing"] = False

    coordinates = np.arange(count, dtype=np.float64) / rate
    approximate = best["dots"]
    zero_line = int(np.flatnonzero(offsets == 0)[0])
    for i in (zero_line, len(offsets) + zero_line):
        frequency = best["frequencies"][i] + best["cfo_hz"]
        direct = np.dot(np.exp(-2j * np.pi * frequency * coordinates), values) / math.sqrt(count)
        error = float(abs(approximate[i] - direct) / max(abs(direct), math.sqrt(variance)))
        result["fft_dot_relative_errors"].append(error)
    if max(result["fft_dot_relative_errors"]) > FFT_DOT_RELATIVE_TOLERANCE:
        return finish("fft_direct_dot_disagreement")
    lines = len(offsets)
    for name, selection in (("low", slice(0, lines)), ("high", slice(lines, 2 * lines))):
        frequencies = best["frequencies"][selection]
        scores, rank = _projection_scores(best["dots"][None, selection], frequencies,
                                          count, rate, variance)
        threshold = float(gammainccinv(rank, .001 / max(1, coarse_calls)))
        result["crosschecks"][name] = dict(score=float(scores[0]), rank=rank,
                                            presence_threshold=threshold)
    if check_halves:
        midpoint = count // 2
        for name, half in (("first_half", values[:midpoint]), ("second_half", values[midpoint:])):
            # Independent full-public-prior acquisition is essential here.
            # A small local fit box can force a mismatched half's sidelobe to
            # remain inside the consistency tolerance and make it tautological.
            half_result = dict(status="fallback", clock_status="fallback",
                               peaks_hz={}, crosschecks={}, fft_dot_relative_errors=[])

            def finish_half(reason):
                half_result["reason"] = reason
                return half_result

            half_result = _estimate_joint_clock(
                half, rate=rate, centers=centers, offsets=offsets, bank=bank,
                prior=prior, variance=variance, duration=len(half) / rate,
                audio_ppm=audio_ppm, channel_rate_ppm=channel_rate_ppm,
                phase_diffusion=phase_diffusion, drift=drift,
                model_allowance=model_allowance, result=half_result,
                finish=finish_half, check_halves=False)
            result["crosschecks"][name] = dict(
                peak_hz=half_result["peaks_hz"].get("joint"),
                clock_ppm=half_result.get("clock_estimate_ppm"),
                score=half_result.get("presence_score", 0),
                rank=half_result.get("rank", 0),
                presence_threshold=half_result.get("presence_threshold", 0),
                status=half_result["status"], clock_status=half_result["clock_status"],
                reason=half_result["reason"],
                independent_full_prior=True)
            if half_result["clock_status"] != "narrowed":
                return finish("joint_half_acquisition_" + half_result["reason"])
            peak = half_result["peaks_hz"]["joint"]
            result["peaks_hz"][name] = peak
            if abs(peak - best["cfo_hz"]) > max(.04, allowance + half_result["allowance_hz"]):
                return finish("pilot_or_half_peak_disagreement")
            if abs(half_result["clock_estimate_ppm"] - best["clock_ppm"]) > (
                    clock_allowance + half_result["clock_allowance_ppm"]):
                return finish("pilot_or_half_clock_disagreement")
    if any(check["score"] < check["presence_threshold"] for check in result["crosschecks"].values()):
        return finish("crosscheck_insufficient_presence")
    selected = _bank_subset(bank, expanded)
    result.update(clock_status="narrowed",
                  expanded_clock_intervals_ppm=clock_intervals)
    if len(selected) >= len(bank):
        return finish("no_bank_shrink")
    result.update(status="narrowed", selected_frequencies=selected.tolist(), selected_count=len(selected))
    return finish("conditional_joint_clock_model_checks_passed")


def estimate_frequency(samples, *, sample_rate=18000, data_if_hz,
                       low_pilot_hz=550, high_pilot_hz, original_frequencies,
                       prior_half_width_hz, doppler_hz, slow_depth, fast_depth,
                       noise_variance, first_sample_seconds,
                       observation_seconds=36, audio_error_bound_ppm=.0001,
                       phase_diffusion_radians_per_sqrt_second,
                       drift_bound_hz_per_second=0, model_allowance_hz=.01,
                       pilot_dwell_gating=False, physical_rf_retuning=False,
                       pilot_phase_model="continuous", fit_audio_clock=None,
                       channel_rate_error_bound_ppm=None):
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
    ``fit_audio_clock=None`` enables joint fitting above a one-ppm public audio
    bound; explicit False retains the legacy fixed-clock estimator. The optional
    ``channel_rate_error_bound_ppm`` bounds 1/RXaudio separately from TX/RXaudio.
    Joint-fit CFO/clock intervals remain conditional engineering allowances.
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
    if fit_audio_clock is None:
        fit_audio_clock = audio_ppm > 1
    if not isinstance(fit_audio_clock, (bool, np.bool_)):
        raise ValueError("fit_audio_clock must be boolean or None")
    if channel_rate_error_bound_ppm is None:
        # A conservative independent-endpoint bound when only a pair envelope
        # is supplied. Callers with exact endpoint bounds should pass them.
        if audio_ppm >= 1e6:
            raise ValueError("audio clock bound must be below one million ppm")
        channel_rate_error_bound_ppm = audio_ppm / (2 - audio_ppm * 1e-6)
    channel_rate_ppm = _number(channel_rate_error_bound_ppm,
                               "channel_rate_error_bound_ppm", minimum=0)
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
        "arithmetic": {"fft_input_output": "complex64",
                       "interpolation_and_profile_products": "complex128",
                       "gram_rank_and_local_fit": "float64/complex128"},
    }

    def finish(reason):
        result["reason"] = reason
        result["wall_seconds"] = time.perf_counter() - started
        return result

    if physical_rf_retuning:
        return finish("physical_rf_retuning_unsupported")
    if pilot_phase_model != "continuous":
        return finish("unknown_or_noncontinuous_pilot_phase_model")
    if fit_audio_clock:
        return _estimate_joint_clock(
            values, rate=rate, centers=centers, offsets=offsets, bank=bank,
            prior=prior, variance=variance, duration=duration,
            audio_ppm=audio_ppm, channel_rate_ppm=channel_rate_ppm,
            phase_diffusion=phase_diffusion, drift=drift,
            model_allowance=model_allowance, result=result, finish=finish)
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
