#!/usr/bin/env python3
"""Offline single-tone audio measurements; this module never opens audio devices.

analyze_tone(samples, sample_rate, frequency_hint_hz=997) and analyze_silence
accept a finite, normalized one-dimensional NumPy array. Results are JSON-safe
except for the separately nested ``psd`` arrays. Copy/remove that key before
JSON serialization, or retain its arrays in NPZ for plotting.

Power uses normalized sample units (FS**2), and PSD uses FS**2/Hz. Here RMS
0 dBFS means RMS=1: a full-scale peak-one sinusoid has RMS -3.0103 dBFS. Ratios
are unaffected by that convention. No converter resolution or calibration is
inferred. The default SNR removes fitted harmonic orders 2..6; higher orders,
sidebands and nonstationary distortion remain in the measured noise. SINAD
removes only the fitted fundamental and therefore includes harmonic distortion.
"""

import math
import numpy as np


DEFAULT_BANDS = ((300.0, 18300.0), (20.0, 20000.0))


def _samples_and_bands(samples, sample_rate, bands):
    values = np.asarray(samples, dtype=np.float64)
    if values.ndim != 1 or values.size < 128:
        raise ValueError("samples must be a one-dimensional array of at least 128 values")
    if not np.isfinite(values).all():
        raise ValueError("samples must be finite")
    fs = float(sample_rate)
    if not math.isfinite(fs) or fs <= 0:
        raise ValueError("sample_rate must be positive and finite")
    validated = []
    for low, high in bands:
        low, high = float(low), float(high)
        if not (math.isfinite(low) and math.isfinite(high) and 0 <= low < high <= fs / 2):
            raise ValueError("each analysis band must lie inside 0..Nyquist")
        validated.append((low, high))
    if not validated:
        raise ValueError("at least one analysis band is required")
    return values, fs, tuple(validated)


def _db(power):
    # JSON has no portable representation of +/-infinity. An exact zero floor
    # therefore has a null logarithmic result, not a fabricated finite SNR.
    return float(10 * math.log10(power)) if power > 0 else None


def _ratio_db(numerator, denominator):
    return _db(numerator / denominator) if numerator > 0 and denominator > 0 else None


def _level_metrics(values, near_full_scale):
    if not 0 < near_full_scale <= 1:
        raise ValueError("near_full_scale must lie in (0,1]")
    power = float(np.mean(values * values))
    near = int(np.count_nonzero(np.abs(values) >= near_full_scale))
    full = int(np.count_nonzero(np.abs(values) >= 32767 / 32768))
    return {
        "samples": int(values.size), "dc": float(np.mean(values)),
        "rms": math.sqrt(power), "rms_dbfs": _db(power),
        "peak": float(np.max(np.abs(values))),
        "near_full_scale_threshold": float(near_full_scale),
        "near_full_scale_samples": near,
        "near_full_scale_fraction": near / values.size,
        "s16_full_scale_threshold": 32767 / 32768,
        "s16_full_scale_samples": full,
        "outside_unit_range_samples": int(np.count_nonzero(np.abs(values) > 1)),
    }


def _periodogram(values, sample_rate, window):
    """One-sided Hann periodogram with window-energy power normalization."""
    transformed = np.fft.rfft(values * window)
    density = np.abs(transformed) ** 2 / (sample_rate * float(np.sum(window * window)))
    if values.size % 2 == 0:
        density[1:-1] *= 2
    else:
        density[1:] *= 2
    return density


def _band_power(frequencies, density, low, high, sample_rate, count):
    # Integrate each PSD bin's rectangular frequency cell, with fractional
    # edge cells. DC/Nyquist endpoints are irrelevant to the default bands.
    width = sample_rate / count
    left = np.maximum(0.0, frequencies - width / 2)
    right = np.minimum(sample_rate / 2, frequencies + width / 2)
    overlap = np.maximum(0.0, np.minimum(right, high) - np.maximum(left, low))
    # The endpoint periodogram bins already hold their entire undoubled power
    # in width Hz; preserve that normalization if a caller includes an endpoint.
    cell_width = right - left
    weights = np.divide(overlap, cell_width, out=np.zeros_like(overlap), where=cell_width > 0)
    return float(np.sum(density * weights) * width)


def _fit_fundamental(values, times, frequency, centered, centered_energy):
    phase = 2 * np.pi * frequency * times
    cosine, sine = np.cos(phase), np.sin(phase)
    mean_cos, mean_sin = float(np.mean(cosine)), float(np.mean(sine))
    cc = float(np.sum(cosine * cosine)) - values.size * mean_cos * mean_cos
    ss = float(np.sum(sine * sine)) - values.size * mean_sin * mean_sin
    cs = float(np.sum(cosine * sine)) - values.size * mean_cos * mean_sin
    yc, ys = float(np.sum(centered * cosine)), float(np.sum(centered * sine))
    determinant = cc * ss - cs * cs
    if determinant <= 0:
        raise ValueError("tone fit is degenerate at this frequency/duration")
    cosine_amplitude = (yc * ss - ys * cs) / determinant
    sine_amplitude = (ys * cc - yc * cs) / determinant
    residual_energy = max(0.0, centered_energy - cosine_amplitude * yc - sine_amplitude * ys)
    return residual_energy


def _frequency_fit(values, sample_rate, frequency_hint, search_hz, window):
    low, high = frequency_hint - search_hz, frequency_hint + search_hz
    if not (math.isfinite(frequency_hint) and math.isfinite(search_hz)
            and search_hz > 0 and 0 < low < high < sample_rate / 2):
        raise ValueError("frequency search must lie strictly inside 0..Nyquist")
    times = (np.arange(values.size, dtype=np.float64) - (values.size - 1) / 2) / sample_rate
    centered = values - np.mean(values)
    centered_energy = float(np.sum(centered * centered))
    # A zero-padded Hann FFT chooses the correct lobe before bounded refinement;
    # minimizing over the entire +/-3 Hz interval directly is not unimodal.
    nfft = 1 << (values.size * 8 - 1).bit_length()
    spectrum = np.abs(np.fft.rfft(centered * window, n=nfft)) ** 2
    fft_frequencies = np.fft.rfftfreq(nfft, 1 / sample_rate)
    allowed = np.flatnonzero((fft_frequencies >= low) & (fft_frequencies <= high))
    if not allowed.size:
        raise ValueError("frequency search is narrower than the padded FFT grid")
    peak_index = int(allowed[np.argmax(spectrum[allowed])])
    guess = float(fft_frequencies[peak_index])
    native_width = sample_rate / values.size
    left, right = max(low, guess - native_width), min(high, guess + native_width)
    ratio = (math.sqrt(5) - 1) / 2
    x1, x2 = right - ratio * (right - left), left + ratio * (right - left)
    objective = lambda f: _fit_fundamental(values, times, f, centered, centered_energy)
    y1, y2 = objective(x1), objective(x2)
    for _ in range(56):
        if y1 < y2:
            right, x2, y2 = x2, x1, y1
            x1 = right - ratio * (right - left)
            y1 = objective(x1)
        else:
            left, x1, y1 = x1, x2, y2
            x2 = left + ratio * (right - left)
            y2 = objective(x2)
    fitted = (left + right) / 2
    at_boundary = min(fitted - low, high - fitted) <= max(1e-5, native_width * 0.05)
    return fitted, times, at_boundary


def analyze_tone(samples, sample_rate, frequency_hint_hz=997.0, *,
                 bands=DEFAULT_BANDS, search_hz=3.0, max_harmonic=6,
                 near_full_scale=0.999):
    """Measure band SNR/SINAD from a stationary, known single-tone segment.

    Fit one frequency near the supplied fundamental. Harmonics have fixed
    integer-multiple frequencies, folded at Nyquist; their phases/amplitudes are fitted jointly
    with DC and the fundamental. No independent harmonic frequency or spectral
    peak searches occur. Residual sidebands remain in the PSD/noise estimates.
    This removes 2*harmonics+1 fitted coefficients, plus one frequency parameter;
    the small noise-projection bias is reported rather than hidden by arbitrary
    spectral-bin excisions. Clipped measurements are returned but unqualified.
    """
    values, fs, bands = _samples_and_bands(samples, sample_rate, bands)
    if isinstance(max_harmonic, bool) or int(max_harmonic) != max_harmonic or not 1 <= max_harmonic <= 64:
        raise ValueError("max_harmonic must be an integer in 1..64")
    max_harmonic = int(max_harmonic)
    window = np.hanning(values.size)
    frequency, times, at_boundary = _frequency_fit(values, fs, float(frequency_hint_hz), float(search_hz), window)
    highest_band = max(high for _, high in bands)
    harmonic_lines = [(1, frequency, frequency)]
    excluded = []
    for order in range(2, max_harmonic + 1):
        original_frequency = order * frequency
        folded_frequency = abs((original_frequency + fs / 2) % fs - fs / 2)
        if folded_frequency <= 1e-6 or folded_frequency > highest_band:
            excluded.append({"order": order, "frequency_hz": folded_frequency,
                             "reason": "outside_analysis_bands_or_at_dc"})
        elif any(abs(folded_frequency - existing[1]) <= 1e-6 for existing in harmonic_lines):
            excluded.append({"order": order, "frequency_hz": folded_frequency,
                             "reason": "coincident_with_another_fitted_line"})
        else:
            harmonic_lines.append((order, folded_frequency, original_frequency))
    columns = [np.ones(values.size)]
    for _, line_frequency, _ in harmonic_lines:
        phase = 2 * np.pi * line_frequency * times
        columns.extend((np.cos(phase), np.sin(phase)))
    design = np.column_stack(columns)
    coefficients = np.linalg.lstsq(design, values, rcond=None)[0]
    fitted = design @ coefficients
    fundamental = design[:, 1] * coefficients[1] + design[:, 2] * coefficients[2]
    without_fundamental = values - coefficients[0] - fundamental
    noise = values - fitted
    signal_power = float((coefficients[1] ** 2 + coefficients[2] ** 2) / 2)
    harmonics = []
    for position, (order, line_frequency, original_frequency) in enumerate(harmonic_lines[1:], 1):
        power = float((coefficients[2 * position + 1] ** 2 + coefficients[2 * position + 2] ** 2) / 2)
        harmonics.append({"order": order, "frequency_hz": line_frequency,
                          "unaliased_frequency_hz": original_frequency,
                          "aliased": original_frequency > fs / 2,
                          "power_fs2": power, "dbc": _ratio_db(power, signal_power)})
    frequencies = np.fft.rfftfreq(values.size, 1 / fs)
    input_psd = _periodogram(values - np.mean(values), fs, window)
    sinad_psd = _periodogram(without_fundamental, fs, window)
    noise_psd = _periodogram(noise, fs, window)
    levels = _level_metrics(values, near_full_scale)
    reasons = []
    if levels["near_full_scale_samples"]:
        reasons.append("near_full_scale_samples_present")
    if at_boundary:
        reasons.append("fitted_frequency_near_search_boundary")
    if values.size / fs < 0.25:
        reasons.append("segment_shorter_than_0.25_seconds")
    ac_power = float(np.var(values))
    if signal_power <= max(np.finfo(float).tiny, ac_power * 1e-4):
        reasons.append("weak_or_absent_expected_fundamental")
    band_results = []
    for low, high in bands:
        if not low <= frequency <= high:
            band_results.append({"low_hz": low, "high_hz": high, "fundamental_in_band": False,
                                 "snr_db": None, "sinad_db": None})
            continue
        noise_power = _band_power(frequencies, noise_psd, low, high, fs, values.size)
        noise_distortion_power = _band_power(frequencies, sinad_psd, low, high, fs, values.size)
        harmonic_power = math.fsum(h["power_fs2"] for h in harmonics if low <= h["frequency_hz"] <= high)
        band_results.append({
            "low_hz": low, "high_hz": high, "fundamental_in_band": True,
            "signal_power_fs2": signal_power, "signal_rms_dbfs": _db(signal_power),
            "noise_power_fs2": noise_power, "noise_rms_dbfs": _db(noise_power),
            "noise_and_distortion_power_fs2": noise_distortion_power,
            "noise_and_distortion_rms_dbfs": _db(noise_distortion_power),
            "fitted_harmonic_power_fs2": harmonic_power,
            "snr_db": _ratio_db(signal_power, noise_power),
            "sinad_db": _ratio_db(signal_power, noise_distortion_power),
            "thd_db": _ratio_db(harmonic_power, signal_power),
            "thd_percent": 100 * math.sqrt(harmonic_power / signal_power) if signal_power > 0 else None,
        })
    return {
        "analysis": "stationary_single_tone", "sample_rate": fs,
        "duration_seconds": values.size / fs, "levels": levels,
        "frequency_hint_hz": float(frequency_hint_hz), "frequency_hz": frequency,
        "frequency_offset_hz": frequency - float(frequency_hint_hz),
        "frequency_search_hz": float(search_hz), "max_harmonic_requested": max_harmonic,
        "harmonics": harmonics, "excluded_harmonics": excluded,
        "fitted_dc": float(coefficients[0]),
        "fitted_signal_power_fs2": signal_power,
        "fitted_parameter_count": len(coefficients) + 1,
        "fitted_parameter_fraction": (len(coefficients) + 1) / values.size,
        "noise_projection_bias_corrected": False,
        "qualified": not reasons, "qualification_reasons": reasons,
        "method": "Sub-bin fundamental fit; fixed harmonic multiples folded at Nyquist; Hann residual periodograms with window-energy normalization. SNR excludes fitted orders through max_harmonic; SINAD retains harmonic distortion. Separate silence is not substituted for in-tone noise.",
        "bands": band_results,
        "psd": {"frequency_hz": frequencies, "input": input_psd,
                "without_fundamental": sinad_psd,
                "without_fundamental_and_harmonics": noise_psd},
    }


def analyze_silence(samples, sample_rate, *, bands=DEFAULT_BANDS, near_full_scale=0.999):
    """Measure separate silence noise; do not call its ratio in-tone SNR."""
    values, fs, bands = _samples_and_bands(samples, sample_rate, bands)
    window = np.hanning(values.size)
    frequencies = np.fft.rfftfreq(values.size, 1 / fs)
    density = _periodogram(values - np.mean(values), fs, window)
    levels = _level_metrics(values, near_full_scale)
    reasons = ["near_full_scale_samples_present"] if levels["near_full_scale_samples"] else []
    if values.size / fs < 0.25:
        reasons.append("segment_shorter_than_0.25_seconds")
    result = []
    for low, high in bands:
        power = _band_power(frequencies, density, low, high, fs, values.size)
        result.append({"low_hz": low, "high_hz": high, "noise_power_fs2": power,
                       "noise_rms_dbfs": _db(power)})
    return {"analysis": "separate_silence", "sample_rate": fs,
            "duration_seconds": values.size / fs, "levels": levels,
            "qualified": not reasons, "qualification_reasons": reasons,
            "method": "DC-removed Hann periodogram with window-energy normalization; separate noise-floor measurement, not in-tone SNR.",
            "bands": result, "psd": {"frequency_hz": frequencies, "input": density}}


def analyze_multitone(samples, sample_rate, frequencies, *, bands=DEFAULT_BANDS,
                      clock_scale=1.0, reference_amplitudes=None,
                      reference_phases_rad=None, near_full_scale=0.999):
    """Fit DC and only commanded tones, retaining off-tone distortion/noise.

    All frequencies use one caller-supplied clock_scale; it is never optimized
    here. The default 1.0 explicitly assumes nominal coherent clocks. Phases use
    a cosine convention at the first sample of this analysis segment. Optional
    reference phases must refer to that same instant, including any deliberate
    source-segment offset; unknown propagation delay remains in transfer phase.

    Coherent IMD falling exactly on a commanded tone is inseparable from that
    tone's linear response. The band ratio is consequently a *linear multitone
    residual ratio*, not calibrated AWGN SNR, THD, or proof of modem reliability.
    """
    values, fs, bands = _samples_and_bands(samples, sample_rate, bands)
    commanded = np.asarray(frequencies, dtype=np.float64)
    scale = float(clock_scale)
    if commanded.ndim != 1 or not 1 <= commanded.size <= 64 or not np.isfinite(commanded).all():
        raise ValueError("frequencies must contain 1..64 finite commanded tones")
    if not math.isfinite(scale) or scale <= 0:
        raise ValueError("clock_scale must be positive and finite")
    actual = commanded * scale
    if np.any(actual <= 0) or np.any(actual >= fs / 2):
        raise ValueError("scaled tone frequencies must lie strictly inside 0..Nyquist")
    if np.any(np.diff(np.sort(actual)) <= 1e-6):
        raise ValueError("commanded tones must have distinct frequencies")
    parameter_count = 1 + 2 * commanded.size
    if values.size <= parameter_count:
        raise ValueError("not enough samples for the commanded tone set")

    def references(reference, name, positive=False):
        if reference is None:
            return None
        result = np.asarray(reference, dtype=np.float64)
        if result.shape != commanded.shape or not np.isfinite(result).all() or (positive and np.any(result <= 0)):
            raise ValueError(f"{name} must match the tone count and contain {'positive ' if positive else ''}finite values")
        return result

    reference_amplitudes = references(reference_amplitudes, "reference_amplitudes", positive=True)
    reference_phases_rad = references(reference_phases_rad, "reference_phases_rad")
    times = np.arange(values.size, dtype=np.float64) / fs
    columns = [np.ones(values.size)]
    for frequency in actual:
        phase = 2 * np.pi * frequency * times
        columns.extend((np.cos(phase), np.sin(phase)))
    design = np.column_stack(columns)
    coefficients, _, rank, singular = np.linalg.lstsq(design, values, rcond=None)
    residual = values - design @ coefficients
    condition = float(singular[0] / singular[-1]) if singular[-1] > 0 else None
    tones = []
    for index, (nominal, frequency) in enumerate(zip(commanded, actual)):
        cosine, sine = float(coefficients[2 * index + 1]), float(coefficients[2 * index + 2])
        amplitude = math.hypot(cosine, sine)
        phase = math.atan2(-sine, cosine)
        tone = {"commanded_frequency_hz": float(nominal), "frequency_hz": float(frequency),
                "peak_amplitude": amplitude, "rms_amplitude": amplitude / math.sqrt(2),
                "power_fs2": amplitude * amplitude / 2, "phase_rad": phase,
                "cosine_coefficient": cosine, "sine_coefficient": sine}
        if reference_amplitudes is not None:
            reference = float(reference_amplitudes[index])
            tone.update({"reference_peak_amplitude": reference,
                         "amplitude_gain": amplitude / reference,
                         "amplitude_gain_db": _db((amplitude / reference) ** 2)})
        if reference_phases_rad is not None:
            reference_phase = float(reference_phases_rad[index])
            tone.update({"reference_phase_rad": reference_phase,
                         "phase_difference_rad": (phase - reference_phase + math.pi) % (2 * math.pi) - math.pi})
        tones.append(tone)
    window = np.hanning(values.size)
    fft_frequencies = np.fft.rfftfreq(values.size, 1 / fs)
    input_psd = _periodogram(values - np.mean(values), fs, window)
    residual_psd = _periodogram(residual, fs, window)
    band_results = []
    for low, high in bands:
        selected = [tone for tone in tones if low <= tone["frequency_hz"] <= high]
        signal_power = math.fsum(tone["power_fs2"] for tone in selected)
        residual_power = _band_power(fft_frequencies, residual_psd, low, high, fs, values.size)
        band_results.append({
            "low_hz": low, "high_hz": high, "fitted_tones_in_band": len(selected),
            "fitted_signal_power_fs2": signal_power, "fitted_signal_rms_dbfs": _db(signal_power),
            "residual_power_fs2": residual_power, "residual_rms_dbfs": _db(residual_power),
            "linear_multitone_residual_ratio_db": _ratio_db(signal_power, residual_power),
        })
    levels = _level_metrics(values, near_full_scale)
    reasons = []
    if levels["near_full_scale_samples"]:
        reasons.append("near_full_scale_samples_present")
    if values.size / fs < 0.25:
        reasons.append("segment_shorter_than_0.25_seconds")
    if rank < parameter_count or condition is None or condition > 1e8:
        reasons.append("ill_conditioned_or_rank_deficient_tone_fit")
    fitted_power = math.fsum(tone["power_fs2"] for tone in tones)
    if fitted_power <= max(np.finfo(float).tiny, float(np.var(values)) * 1e-4):
        reasons.append("weak_or_absent_commanded_tones")
    return {
        "analysis": "linear_multitone_residual", "sample_rate": fs,
        "duration_seconds": values.size / fs, "levels": levels,
        "clock_scale": scale, "clock_scale_fitted": False,
        "clock_scale_is_nominal": scale == 1.0,
        "clock_assumption": "Every commanded frequency uses the same supplied scale; scale 1 assumes nominal coherent clocks. No independent tone-frequency or drift fitting is performed.",
        "phase_reference": "cosine phase at first sample of analysis segment",
        "tones": tones, "fitted_dc": float(coefficients[0]),
        "fitted_parameter_count": int(parameter_count),
        "fitted_parameter_fraction": parameter_count / values.size,
        "design_condition_number": condition,
        "noise_projection_bias_corrected": False,
        "qualified": not reasons, "qualification_reasons": reasons,
        "method": "DC and commanded tones only; summed fitted in-band tone power divided by Hann residual-PSD band power. Off-tone IMD, sidebands and noise remain in residual. No harmonic or residual-peak removal.",
        "caveat": "Coherent IMD at commanded lines is inseparable from their fitted response. This is a linear multitone residual ratio, not calibrated AWGN SNR or a modem success probability. Unknown delay remains in transfer phases.",
        "bands": band_results,
        "psd": {"frequency_hz": fft_frequencies, "input": input_psd,
                "without_commanded_tones": residual_psd},
    }
