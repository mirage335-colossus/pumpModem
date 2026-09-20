#!/usr/bin/env python3
"""Offline acoustic sounder generation and held-out channel analysis; never opens audio.

Requires NumPy. ``generate --output DIR`` writes mono/stereo float32le and
metadata. Record the mono stimulus with production cable_audio_capture, then
``analyze --input DIR --capture DIR/capture-mono.f32 --channels 1``.

Capacity integrals are conditional engineering models, not statistical lower
bounds or delivery predictions. They assume a stationary linear channel with
Gaussian-equivalent additive residual at the measured level. Changing power
loading can change loudspeaker distortion; water filling does not model that.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np


def _smooth(values, width):
    if width <= 1:
        return values.copy()
    padded = np.pad(values, (width // 2, width - 1 - width // 2), mode="edge")
    summed = np.concatenate([[0.], np.cumsum(padded)])
    return (summed[width:] - summed[:-width]) / width


def _welch(values, fs, count):
    window = np.hanning(count)
    spectra = []
    for start in range(0, values.size - count + 1, max(1, count // 2)):
        piece = values[start:start + count]
        transformed = np.fft.rfft((piece - piece.mean()) * window)
        density = np.abs(transformed) ** 2 / (fs * np.sum(window ** 2))
        density[1:-1] *= 2
        spectra.append(density)
    return np.fft.rfftfreq(count, 1 / fs), np.mean(spectra, axis=0)


def _db(power):
    return float(10 * np.log10(power)) if power > 0 else None


def _levels(values):
    power = float(np.mean(values * values))
    return dict(samples=int(values.size), rms=math.sqrt(power), rms_dbfs=_db(power),
                peak=float(np.max(np.abs(values))), dc=float(np.mean(values)),
                near_full_scale_samples=int(np.count_nonzero(np.abs(values) >= .999)),
                outside_unit_range_samples=int(np.count_nonzero(np.abs(values) > 1)))


def generate_stimulus(amplitude=.08, sample_rate=48000, period_samples=16384,
                      blocks=8, repeats=4, low_hz=150., high_hz=18000., seed=417):
    """Independent random phases per block; equal spectrum, four exact repeats.

    One cyclic half-period precedes each block. This makes the first scored
    period stationary for channels shorter than that prefix; later repeats
    independently expose a prefix that was too short. All blocks have exactly
    the same RMS and spectral magnitudes. Peak normalization is shared.
    """
    if not (0 < amplitude <= .8 and 8000 <= sample_rate <= 192000
            and 1024 <= period_samples <= 65536 and period_samples % 2 == 0
            and 4 <= blocks <= 16 and blocks % 2 == 0 and 3 <= repeats <= 8
            and 0 < low_hz < high_hz < sample_rate / 2):
        raise ValueError("invalid bounded sounder geometry or level")
    rng = np.random.default_rng(seed)
    frequencies = np.fft.rfftfreq(period_samples, 1 / sample_rate)
    selected = (frequencies >= low_hz) & (frequencies <= high_hz)
    spectra = np.zeros((blocks, frequencies.size), complex)
    spectra[:, selected] = np.exp(2j * np.pi * rng.random((blocks, int(selected.sum()))))
    periods = np.fft.irfft(spectra, n=period_samples, axis=1)
    periods *= amplitude / np.max(np.abs(periods))
    prefix = period_samples // 2
    chunks, segments, markers = [], [], []
    position = 0

    def append(kind, values, **extra):
        nonlocal position
        values = np.asarray(values, dtype="<f4")
        segments.append(dict(kind=kind, start_sample=position, samples=len(values), **extra))
        chunks.append(values)
        position += len(values)

    def silence(seconds):
        append("silence", np.zeros(round(seconds * sample_rate)))

    def chirp():
        count = round(.3 * sample_rate)
        time = np.arange(count) / sample_rate
        initial = max(300., low_hz)
        logarithm = np.log(high_hz / initial)
        duration = count / sample_rate
        sweep = np.cos(2 * np.pi * initial * duration / logarithm * np.expm1(logarithm * time / duration))
        ramp = round(count * .07)
        window = np.ones(count)
        window[:ramp] = np.sin(np.linspace(0, np.pi / 2, ramp)) ** 2
        window[-ramp:] = window[:ramp][::-1]
        sweep *= window * amplitude * .5
        markers.append(dict(start_sample=position, samples=count))
        append("sync", sweep)

    silence(.75)
    chirp()
    silence(.35)
    for index, period in enumerate(periods):
        if index == blocks // 2:
            silence(.15)
            chirp()
            silence(.35)
        append("periodic", np.concatenate([period[-prefix:], np.tile(period, repeats)]),
               block=index, prefix_samples=prefix, period_samples=period_samples, repeats=repeats)
    silence(.15)
    chirp()
    silence(1.)
    samples = np.concatenate(chunks)
    if samples.size > 90 * sample_rate:
        raise ValueError("sounder exceeds 90 seconds")
    metadata = dict(format="datapump-acoustic-sounder-v1", sample_rate=sample_rate,
                    pcm_format="float32le", amplitude=amplitude, seed=seed,
                    period_samples=period_samples, prefix_samples=prefix, blocks=blocks,
                    repeats=repeats, low_hz=low_hz, high_hz=high_hz,
                    transmit_seconds=samples.size / sample_rate, segments=segments,
                    markers=markers, transmit_levels=_levels(samples),
                    active_period_levels=_levels(periods.ravel()))
    return samples, metadata


def _marker_peak(capture, template, low, high):
    low = max(0, int(low))
    high = min(capture.size - template.size, int(high))
    if high < low:
        raise ValueError("capture does not contain the expected sync search window")
    piece = capture[low:high + template.size]
    fft_size = 1 << (piece.size + template.size - 2).bit_length()
    convolution = np.fft.irfft(np.fft.rfft(piece, fft_size) * np.fft.rfft(template[::-1], fft_size), fft_size)
    correlation = convolution[template.size - 1:piece.size]
    peak = int(np.argmax(np.abs(correlation)))
    # Three-point interpolation of the native-rate peak has a phase-dependent
    # bias large enough to masquerade as sub-ppm clock drift. Interpolate the
    # band-limited correlation first, then refine its finely sampled maximum.
    offsets = np.linspace(-1, 1, 129)
    fine = np.abs(_sample_at(convolution, template.size - 1 + peak + offsets))
    best = int(np.argmax(fine))
    fraction = float(offsets[best])
    if 0 < best < fine.size - 1:
        left, middle, right = fine[best - 1:best + 2]
        denominator = left - 2 * middle + right
        if denominator < 0:
            fraction += float(.5 * (left - right) / denominator / 64)
    energy = float(np.dot(piece[peak:peak + template.size], piece[peak:peak + template.size]))
    score = float(fine[best] / math.sqrt(max(energy * np.dot(template, template), 1e-300)))
    return low + peak + fraction, score


def _sample_at(values, coordinates):
    """64-tap Kaiser-windowed sinc; interpolated 4096-phase coefficient table.

    Unlike linear interpolation, this retains the upper audio passband under a
    fractional clock correction. Fixed endpoint margins are required.
    """
    radius, phases = 32, 4096
    offsets = np.arange(-radius + 1, radius + 1)
    fraction = np.arange(phases + 1)[:, None] / phases
    distance = offsets[None, :] - fraction
    window = np.i0(8.6 * np.sqrt(np.maximum(0, 1 - (distance / radius) ** 2))) / np.i0(8.6)
    table = np.sinc(distance) * window
    table /= table.sum(axis=1)[:, None]
    output = np.empty(coordinates.size)
    for start in range(0, coordinates.size, 4096):
        at = coordinates[start:start + 4096]
        integer = np.floor(at).astype(np.int64)
        indices = integer[:, None] + offsets
        if indices.min() < 0 or indices.max() >= values.size:
            raise ValueError("capture lacks interpolation margins")
        fractional_index = (at - integer) * phases
        first = np.minimum(fractional_index.astype(int), phases - 1)
        weight = fractional_index - first
        kernel = table[first] * (1 - weight[:, None]) + table[first + 1] * weight[:, None]
        output[start:start + at.size] = np.sum(values[indices] * kernel, axis=1)
    return output


def capacity_integral(gain_power, noise_psd, tx_psd, bin_hz):
    """One-sided real-channel integral, same measured total source power.

    All selected frequency cells consume width ``bin_hz``. No arbitrary noise
    epsilon creates a finite answer for a perfectly noiseless simulation.
    """
    gain, noise, source = [np.asarray(a, float) for a in (gain_power, noise_psd, tx_psd)]
    if gain.ndim != 1 or gain.size == 0 or gain.shape != noise.shape or gain.shape != source.shape:
        raise ValueError("capacity arrays must have matching nonempty one-dimensional shapes")
    if not all(np.isfinite(a).all() for a in (gain, noise, source)) or np.any(gain < 0) or np.any(noise < 0) or np.any(source < 0) or bin_hz <= 0:
        raise ValueError("capacity inputs must be finite and nonnegative")
    if np.any((noise == 0) & (gain > 0)):
        return dict(qualified=False, reason="zero measured noise; finite capacity not identifiable")
    power = float(source.sum() * bin_hz)
    bandwidth = gain.size * bin_hz
    snr_per_power = np.divide(gain, noise, out=np.zeros_like(gain), where=noise > 0)
    uniform = power / bandwidth
    uniform_rate = float(bin_hz * np.log2(1 + uniform * snr_per_power).sum())
    actual_rate = float(bin_hz * np.log2(1 + source * snr_per_power).sum())
    active = snr_per_power > 0
    allocation = np.zeros_like(gain)
    if active.any() and power > 0:
        floor = 1 / snr_per_power[active]
        lower, upper = float(floor.min()), float(floor.max() + power / (active.sum() * bin_hz))
        for _ in range(80):
            middle = (lower + upper) / 2
            if np.maximum(middle - floor, 0).sum() * bin_hz > power:
                upper = middle
            else:
                lower = middle
        allocation[active] = np.maximum((lower + upper) / 2 - floor, 0)
    water_rate = float(bin_hz * np.log2(1 + allocation * snr_per_power).sum())
    return dict(qualified=True, bandwidth_hz=float(bandwidth), source_power_fs2=power,
                uniform_power_bps=uniform_rate, measured_loading_bps=actual_rate,
                water_filling_bps=water_rate, water_filling_used_power_fs2=float(allocation.sum() * bin_hz),
                water_filling_active_hz=float(np.count_nonzero(allocation) * bin_hz))


def _fit_gain_delay(predicted, training, radians_per_sample):
    """Fit only complex gain and fractional delay, never a per-frequency H."""
    delays = np.linspace(-2., 2., 129)
    product = np.conj(predicted) * training
    correlation = np.exp(1j * delays[:, None] * radians_per_sample[None, :]) @ product
    score = np.abs(correlation) ** 2
    best = int(np.argmax(score))
    delay = float(delays[best])
    if 0 < best < len(delays) - 1:
        left, middle, right = score[best - 1:best + 2]
        denominator = left - 2 * middle + right
        if denominator < 0:
            delay += float(.5 * (left - right) / denominator / 32)
    phased = predicted * np.exp(-1j * radians_per_sample * delay)
    gain = np.vdot(phased, training) / max(float(np.vdot(phased, phased).real), 1e-300)
    return complex(gain), delay, best in (0, len(delays) - 1)


def analyze_capture(transmit, capture, metadata):
    """Analyze one channel without using test periods to fit their own response."""
    tx, rx = np.asarray(transmit, float), np.asarray(capture, float)
    if tx.ndim != 1 or rx.ndim != 1 or not np.isfinite(tx).all() or not np.isfinite(rx).all():
        raise ValueError("transmit and capture must be finite mono arrays")
    fs, count = int(metadata["sample_rate"]), int(metadata["period_samples"])
    if (metadata.get("format") != "datapump-acoustic-sounder-v1" or not 8000 <= fs <= 192000
            or not 1024 <= count <= 65536 or rx.size > 120 * fs or tx.size > 90 * fs):
        raise ValueError("invalid or oversized sounder/capture")
    markers = metadata["markers"]
    if len(markers) != 3:
        raise ValueError("exactly three sync markers are required")
    found, scores, expected = [], [], []
    for marker in markers:
        position, length = marker["start_sample"], marker["samples"]
        template = tx[position:position + length]
        if len(template) != length or length < 128:
            raise ValueError("invalid sync marker")
        if not found:
            low, high = 0, min(6 * fs, rx.size - length)
        else:
            prediction = found[0] + position - expected[0]
            low, high = prediction - .2 * fs, prediction + .2 * fs
        location, score = _marker_peak(rx, template, low, high)
        found.append(location)
        expected.append(position)
        scores.append(score)
    slope, offset = np.polyfit(expected, found, 1)
    if not .995 < slope < 1.005:
        raise ValueError("clock estimate exceeds the sounder's +/-5000 ppm range")
    # A nominal chirp's correlation peak shifts toward its energy centroid when
    # clocks differ. Refit against the once-estimated stretched chirp so that
    # this within-marker shift is not mislabeled as propagation delay.
    for _ in range(2):
        for i, marker in enumerate(markers):
            position, length = marker["start_sample"], marker["samples"]
            padded = np.pad(tx[position:position + length], (128, 128))
            template = _sample_at(padded, 128 + np.arange(round(length * slope)) / slope)
            found[i], scores[i] = _marker_peak(rx, template, found[i] - 8, found[i] + 8)
        slope, offset = np.polyfit(expected, found, 1)
    marker_errors = np.asarray(found) - (offset + slope * np.asarray(expected))
    blocks, originals = [], []
    periodic = [s for s in metadata["segments"] if s["kind"] == "periodic"]
    if len(periodic) != metadata["blocks"] or len(periodic) % 2 or not 4 <= len(periodic) <= 16:
        raise ValueError("invalid independent sounder block count")
    repeats = int(metadata["repeats"])
    if not 3 <= repeats <= 8:
        raise ValueError("invalid repeat count")
    for block in periodic:
        start = block["start_sample"] + block["prefix_samples"]
        original = tx[start:start + count]
        if original.size != count:
            raise ValueError("truncated source period")
        coordinates = offset + slope * (start + np.arange(count * repeats))
        measured = _sample_at(rx, coordinates).reshape(repeats, count)
        blocks.append(measured - measured.mean(axis=1)[:, None])
        originals.append(original - original.mean())
    y = np.fft.rfft(np.asarray(blocks), axis=2)
    x = np.fft.rfft(np.asarray(originals), axis=1)
    frequency = np.fft.rfftfreq(count, 1 / fs)
    selected = (frequency >= metadata["low_hz"]) & (frequency <= metadata["high_hz"])
    magnitude = np.abs(x) ** 2
    normalization = np.full(frequency.size, 2 / (fs * count))
    normalization[[0, -1]] /= 2
    source_psd = magnitude.mean(axis=0) * normalization
    response = np.zeros_like(x)
    response[:, selected] = y.mean(axis=1)[:, selected] / x[:, selected]
    even, odd = response[::2].mean(axis=0), response[1::2].mean(axis=0)
    transfer = (even + odd) / 2
    predicted = np.empty_like(y)
    for i in range(len(blocks)):
        predicted[i] = (odd if i % 2 == 0 else even)[None, :] * x[i][None, :]
    heldout_psd = np.mean(np.abs(y - predicted) ** 2, axis=(0, 1)) * normalization
    # A separate diagnostic asks whether smooth gain/delay motion explains the
    # cross-phase residual. Even repeats fit three real parameters; odd repeats
    # alone score the result. H still comes from other random-phase blocks.
    drift_blocks, drift_errors, baseline_errors = [], [], []
    fitting_bins = selected & (frequency >= 300)
    for i in range(len(blocks)):
        baseline = predicted[i, 0]
        gain, delay, boundary = _fit_gain_delay(baseline[fitting_bins], y[i, ::2].mean(axis=0)[fitting_bins],
                                               2 * np.pi * frequency[fitting_bins] / fs)
        revised = baseline * gain * np.exp(-2j * np.pi * frequency * delay / fs)
        drift_errors.append(np.mean(np.abs(y[i, 1::2] - revised) ** 2, axis=0))
        baseline_errors.append(np.mean(np.abs(y[i, 1::2] - baseline) ** 2, axis=0))
        drift_blocks.append(dict(block=i, gain_magnitude=abs(gain), phase_rad=float(np.angle(gain)),
                                 delay_samples=delay, delay_fit_at_search_boundary=boundary))
    drift_psd = np.mean(drift_errors, axis=0) * normalization
    drift_baseline_psd = np.mean(baseline_errors, axis=0) * normalization
    repeat_psd = np.mean(np.abs(y - y.mean(axis=1)[:, None, :]) ** 2,
                         axis=(0, 1)) * repeats / (repeats - 1) * normalization
    input_power = magnitude.mean(axis=0)
    output_power = np.mean(np.abs(y) ** 2, axis=(0, 1))
    cross = np.mean(y * np.conj(x[:, None, :]), axis=(0, 1))
    coherence = np.divide(np.abs(cross) ** 2, input_power * output_power,
                          out=np.zeros_like(input_power), where=input_power * output_power > 0)
    coherence = np.clip(coherence, 0, 1)
    # Independent pre/post silence remains separate from in-signal residual.
    quiet = []
    for segment in (metadata["segments"][0], metadata["segments"][-1]):
        start = segment["start_sample"] + round(.25 * fs)
        end = segment["start_sample"] + segment["samples"] - round(.05 * fs)
        if end - start >= 128:
            quiet.append(_sample_at(rx, offset + slope * np.arange(start, end)))
    if not quiet:
        raise ValueError("capture lacks independent quiet intervals")
    silence = np.concatenate(quiet)
    silence_frequency, silence_density = _welch(silence, fs, min(count, silence.size))
    silence_psd = np.interp(frequency, silence_frequency, silence_density)
    # Smooth powers (not H) over ~47 Hz. Subtract fold disagreement from H power
    # to avoid interpreting a pure noisy response estimate as useful gain.
    width = max(1, round(47 / (fs / count)))
    smooth = lambda a: _smooth(a, width)
    gain_power = smooth(np.maximum(np.abs(transfer) ** 2 - np.abs(even - odd) ** 2 / 4, 0))
    noise_psd = np.maximum.reduce([smooth(heldout_psd), smooth(repeat_psd), smooth(silence_psd)])
    gain_power[~selected] = 0
    bin_hz = fs / count
    bands = []
    for low, high in ((300, 4000), (300, 8000), (300, 12000), (300, 18000),
                      (metadata["low_hz"], metadata["high_hz"])):
        mask = selected & (frequency >= low) & (frequency <= high)
        if not mask.any():
            continue
        result = capacity_integral(gain_power[mask], noise_psd[mask], source_psd[mask], bin_hz)
        signal_power = float(np.sum(source_psd[mask] * gain_power[mask]) * bin_hz)
        residual_power = float(np.sum(noise_psd[mask]) * bin_hz)
        result.update(low_hz=float(low), high_hz=float(high),
                      signal_to_heldout_residual_db=_db(signal_power / residual_power) if residual_power > 0 else None,
                      median_coherence=float(np.median(coherence[mask])),
                      coherence_10th_percentile=float(np.quantile(coherence[mask], .1)))
        bands.append(result)
    subbands = []
    for low, high in ((300, 2000), (2000, 4000), (4000, 8000), (8000, 12000), (12000, 18000)):
        mask = selected & (frequency >= low) & (frequency < high)
        if not mask.any():
            continue
        useful = source_psd[mask] * gain_power[mask]
        residual = noise_psd[mask]
        per_bin = 10 * np.log10(np.maximum(useful / np.maximum(residual, 1e-300), 1e-300))
        subbands.append(dict(low_hz=low, high_hz=high,
                             signal_to_heldout_residual_db=_db(float(useful.sum() / residual.sum())),
                             median_bin_signal_to_residual_db=float(np.median(per_bin)),
                             tenth_percentile_bin_signal_to_residual_db=float(np.quantile(per_bin, .1)),
                             median_gain_db=_db(float(np.median(gain_power[mask]))),
                             median_coherence=float(np.median(coherence[mask])),
                             heldout_residual_power_fs2=float(heldout_psd[mask].sum() * bin_hz),
                             repeat_residual_power_fs2=float(repeat_psd[mask].sum() * bin_hz),
                             separate_silence_power_fs2=float(silence_psd[mask].sum() * bin_hz),
                             independent_gain_delay_test_baseline_power_fs2=float(drift_baseline_psd[mask].sum() * bin_hz),
                             independent_gain_delay_test_adjusted_power_fs2=float(drift_psd[mask].sum() * bin_hz)))
    def extrema(largest):
        mask = selected & (frequency >= 300)
        candidates = np.flatnonzero(mask)
        candidates = candidates[np.argsort(gain_power[candidates])]
        if largest:
            candidates = candidates[::-1]
        chosen = []
        for index in candidates:
            if all(abs(frequency[index] - item["frequency_hz"]) >= 300 for item in chosen):
                chosen.append(dict(frequency_hz=float(frequency[index]), gain_db=_db(float(gain_power[index])),
                                   coherence=float(coherence[index])))
            if len(chosen) == 5:
                break
        return chosen
    impulse = np.fft.irfft(transfer, n=count)
    peak = int(np.argmax(np.abs(impulse)))
    impulse = np.roll(impulse, count // 4 - peak)
    impulse_time = (np.arange(count) - count // 4) / fs
    energy = impulse ** 2
    impulse_uncertainty = np.roll(np.fft.irfft((even - odd) / 2, n=count), count // 4 - peak)
    taper = np.zeros_like(frequency)
    taper[selected] = 1
    for edge, sign in ((metadata["low_hz"], 1), (metadata["high_hz"], -1)):
        edge_distance = (frequency - edge) * sign
        edge_bins = selected & (edge_distance < 300)
        taper[edge_bins] *= np.sin(np.pi / 2 * edge_distance[edge_bins] / 300) ** 2
    tapered = np.roll(np.fft.irfft(transfer * taper, n=count), count // 4 - peak)
    tapered_uncertainty = np.roll(np.fft.irfft((even - odd) / 2 * taper, n=count), count // 4 - peak)
    coherent_power = tapered ** 2 - tapered_uncertainty ** 2
    cp_energy = []
    # Both centered spans and post-strongest-path windows are shown: a practical
    # cyclic prefix can be shifted to include an early precursor.
    for cp in (2048, 4096):
        duration = cp / fs
        for kind, mask in (("centered", np.abs(impulse_time) > duration / 2),
                           ("after_peak_with_1ms_precursor", (impulse_time < -.001) | (impulse_time > duration - .001))):
            cp_energy.append(dict(prefix_samples=cp, prefix_seconds=duration, window=kind,
                                  raw_tapered_energy_outside_fraction=float(np.sum(tapered[mask] ** 2) / max(np.sum(tapered ** 2), 1e-300)),
                                  signed_coherent_energy_outside_fraction=float(np.sum(coherent_power[mask]) / max(np.sum(coherent_power), 1e-300))))
    # Independent phase-fold disagreement estimates uncertainty in averaged H.
    # Compare in 1 ms cells rather than calling every noisy isolated tap an echo.
    cell = max(1, round(.001 * fs))
    usable = count // cell * cell
    time_cells = impulse_time[:usable].reshape(-1, cell).mean(axis=1)
    cell_energy = energy[:usable].reshape(-1, cell).sum(axis=1)
    uncertainty_energy = (impulse_uncertainty[:usable] ** 2).reshape(-1, cell).sum(axis=1)
    coherent_excess = cell_energy - uncertainty_energy
    above_floor = (coherent_excess > 4 * uncertainty_energy) & (coherent_excess > energy.sum() * 1e-6)
    late = above_floor & (np.abs(time_cells) > .020)
    late_times = time_cells[late]
    cumulative = np.cumsum(energy) / max(energy.sum(), np.finfo(float).tiny)
    quantiles = {str(p): float(impulse_time[min(np.searchsorted(cumulative, p), count - 1)])
                 for p in (.005, .05, .5, .95, .995)}
    block_metrics = []
    for i, (observed, original) in enumerate(zip(blocks, originals)):
        block_metrics.append(dict(block=i, fold="even" if i % 2 == 0 else "odd",
                                  capture_levels=_levels(observed.ravel()), transmit_levels=_levels(original),
                                  repeat_difference_rms=float(np.sqrt(np.mean(np.diff(observed, axis=0) ** 2) / 2))))
    reasons = []
    if _levels(rx)["near_full_scale_samples"]:
        reasons.append("capture_near_full_scale")
    if min(scores) < .05:
        reasons.append("weak_chirp_correlation")
    if np.max(np.abs(marker_errors)) > .25:
        reasons.append("nonlinear_delay_or_clock_motion_exceeds_quarter_sample")
    if not selected.any() or np.median(coherence[selected]) < .2:
        reasons.append("weak_broadband_coherence")
    summary = dict(analysis="independent_phase_crossfit_acoustic_sounder", sample_rate=fs,
                   qualified=not reasons, qualification_reasons=reasons,
                   capture_levels=_levels(rx), active_capture_levels=_levels(np.asarray(blocks).ravel()),
                   silence_levels=_levels(silence), silence_segment_levels=[_levels(piece) for piece in quiet],
                   delay_samples=float(offset), delay_seconds=float(offset / fs),
                   capture_samples_per_source_sample=float(slope), clock_error_ppm=float((slope - 1) * 1e6),
                   marker_scores=scores, marker_delay_residual_samples=marker_errors.tolist(),
                   period_seconds=count / fs, cyclic_prefix_seconds=metadata["prefix_samples"] / fs,
                   independent_phase_blocks=len(blocks), repeats_per_block=repeats,
                   heldout_method="Even phase blocks fit odd blocks and vice versa; no per-test response/phase fit. Repeat differences and independent silence PSD remain separate.",
                   noise_model="Maximum of smoothed held-out prediction residual, unbiased within-block repeat residual, and independent silence PSD. Includes channel motion, distortion, clock/interpolation error and finite training noise.",
                   gain_delay_variation_diagnostic=dict(method="Opposite random-phase-fold H remains fixed. Repeats 0,2,... fit one complex gain and delay within +/-2 samples; repeats 1,3,... alone evaluate it. This diagnostic does not change primary capacity/noise estimates, and cannot uniquely separate nonlinearity from motion.",
                                                        fitted_real_parameters_per_block=3, blocks=drift_blocks),
                   capacity_caveat="Conditional stationary linear/Gaussian-equivalent model at tested RMS; not a confidence bound, Shannon measurement, modem rate, or success probability. Water filling assumes unchanged residual under different power loading.",
                   impulse=dict(reference="Time zero is strongest band-limited path after global delay/clock removal; circular window, not absolute acoustic distance.",
                                quantile_seconds=quantiles,
                                energy_90_percent_span_seconds=quantiles["0.95"] - quantiles["0.05"],
                                energy_99_percent_span_seconds=quantiles["0.995"] - quantiles["0.005"],
                                energy_outside_20ms_fraction=float(energy[np.abs(impulse_time) > .02].sum() / max(energy.sum(), 1e-300)),
                                fold_uncertainty_total_to_response_energy_ratio=float(np.sum(impulse_uncertainty ** 2) / max(energy.sum(), 1e-300)),
                                late_cells_above_fold_noise_floor=int(late.sum()),
                                latest_positive_late_cell_seconds=float(late_times[late_times > 0].max()) if np.any(late_times > 0) else None,
                                late_cells_above_floor_energy_fraction=float(np.maximum(coherent_excess[late], 0).sum() / max(energy.sum(), 1e-300)),
                                cyclic_prefix_energy=cp_energy,
                                cyclic_prefix_energy_method="300 Hz cosine tapers at both excited-band edges; squared mean-fold impulse minus squared half-fold difference estimates coherent energy without clipping negative samples. Finite-period alias and band limitation remain; not a confidence bound.",
                                noise_floor_method="1 ms cells; coherent excess = squared mean-fold impulse minus squared half-fold difference. Listed late cells exceed 4x estimated fold-noise energy and one millionth of total impulse energy. Heuristic detectability, not a confidence interval.",
                                window_seconds=count / fs,
                                caveat="Band limitation and noisy H broaden impulse energy; paths exceeding the cyclic prefix/period can alias. This is not a certified reverberation time."),
                   blocks=block_metrics, bands=bands, subbands=subbands,
                   strongest_response_regions=extrema(True), weakest_response_regions=extrema(False))
    arrays = dict(frequency_hz=frequency, transfer_real=transfer.real, transfer_imag=transfer.imag,
                  transfer_even_real=even.real, transfer_even_imag=even.imag,
                  transfer_odd_real=odd.real, transfer_odd_imag=odd.imag,
                  coherence=coherence, source_psd=source_psd, heldout_residual_psd=heldout_psd,
                  repeat_residual_psd=repeat_psd, silence_psd=silence_psd,
                  capacity_noise_psd=noise_psd, capacity_gain_power=gain_power,
                  gain_delay_diagnostic_baseline_psd=drift_baseline_psd, gain_delay_diagnostic_adjusted_psd=drift_psd,
                  impulse_time_seconds=impulse_time, impulse_response=impulse,
                  impulse_fold_uncertainty=impulse_uncertainty,
                  impulse_cell_time_seconds=time_cells, impulse_cell_coherent_excess=coherent_excess,
                  impulse_cell_uncertainty_energy=uncertainty_energy)
    return summary, arrays


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    generate = commands.add_parser("generate")
    generate.add_argument("--output", required=True, type=Path)
    generate.add_argument("--amplitude", type=float, default=.08)
    generate.add_argument("--seed", type=int, default=417)
    generate.add_argument("--right-only", action="store_true", help="Write zeros on left playback channel; mono reference stays unchanged")
    analyze = commands.add_parser("analyze")
    analyze.add_argument("--input", required=True, type=Path)
    analyze.add_argument("--capture", required=True, type=Path)
    analyze.add_argument("--channels", type=int, choices=(1, 2), default=1)
    arguments = parser.parse_args()
    if arguments.command == "generate":
        samples, metadata = generate_stimulus(arguments.amplitude, seed=arguments.seed)
        arguments.output.mkdir(parents=True, exist_ok=False)
        samples.tofile(arguments.output / "transmit-mono.f32")
        stereo = np.repeat(samples[:, None], 2, axis=1)
        if arguments.right_only:
            stereo[:, 0] = 0
        stereo.tofile(arguments.output / "transmit-stereo.f32")
        metadata["playback_routing"] = "right_only" if arguments.right_only else "identical_both_channels"
        metadata["transmit_mono_sha256"] = hashlib.sha256(samples.tobytes()).hexdigest()
        (arguments.output / "metadata.json").write_text(json.dumps(metadata, indent=2, allow_nan=False) + "\n")
        print(json.dumps(dict(output=str(arguments.output), transmit_seconds=metadata["transmit_seconds"],
                              active_levels=metadata["active_period_levels"])))
        return
    metadata = json.loads((arguments.input / "metadata.json").read_text())
    source_path = arguments.input / "transmit-mono.f32"
    fs = metadata.get("sample_rate")
    if not isinstance(fs, int) or not 8000 <= fs <= 192000:
        raise ValueError("invalid metadata sample rate")
    if source_path.stat().st_size > 90 * fs * 4 or arguments.capture.stat().st_size > 120 * fs * 4 * arguments.channels:
        raise ValueError("PCM file exceeds analysis bound")
    source = np.fromfile(source_path, dtype="<f4")
    if hashlib.sha256(source.tobytes()).hexdigest() != metadata["transmit_mono_sha256"]:
        raise ValueError("transmit fixture hash mismatch")
    capture = np.fromfile(arguments.capture, dtype="<f4")
    if arguments.capture.stat().st_size % (4 * arguments.channels):
        raise ValueError("partial capture sample/frame")
    capture = capture.reshape(-1, arguments.channels)
    inputs = {f"channel_{i}": capture[:, i] for i in range(arguments.channels)}
    if arguments.channels == 2:
        inputs["arithmetic_mean"] = capture.mean(axis=1)
    output = dict(capture_path=str(arguments.capture), capture_sha256=hashlib.sha256(capture.astype("<f4").tobytes()).hexdigest(),
                  channels=arguments.channels, measurements={})
    for name, values in inputs.items():
        summary, arrays = analyze_capture(source, values, metadata)
        output["measurements"][name] = summary
        np.savez_compressed(arguments.input / f"spectral-analysis-{name}.npz", **arrays)
        np.savetxt(arguments.input / f"frequency-response-{name}.csv",
                   np.column_stack([arrays[k] for k in ("frequency_hz", "transfer_real", "transfer_imag", "coherence", "source_psd", "heldout_residual_psd", "repeat_residual_psd", "silence_psd")]),
                   delimiter=",", header="frequency_hz,H_real,H_imag,coherence,source_psd,heldout_residual_psd,repeat_residual_psd,silence_psd", comments="")
    (arguments.input / "analysis.json").write_text(json.dumps(output, indent=2, allow_nan=False) + "\n")
    print(json.dumps(output, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
