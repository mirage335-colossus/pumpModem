#!/usr/bin/env python3
"""Calibrate a small illustrative cable model from Helper/preroll only.

This offline analysis knows the emitted stimulus.  It is not part of the
receiver and does not supply probe synchronization.  The two straight Helper
tones fit gain, a one-pole AC-coupling high-pass and capture latency; the
separate 1600 Hz probe is held out until the final validation.  Two tones do
not uniquely identify an analog circuit or its transfer at other frequencies.

Requires NumPy, but not SciPy.  The default windows reproduce the 2026-10-30
48 kHz recorded-cable analysis.  Noise parameters use only silent preroll.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path

try:
    import numpy as np
except ImportError as exc:
    raise SystemExit("NumPy is required for this optional calibration tool") from exc


def read_pcm(path, rate):
    limit = rate * 120 * 4
    with Path(path).open("rb") as source:
        blob = source.read(limit + 1)
    if not blob or len(blob) % 4 or len(blob) > limit:
        raise ValueError("PCM must be nonempty mono f32le, at most 120 seconds")
    values = np.frombuffer(blob, dtype="<f4").astype(np.float64)
    if not np.all(np.isfinite(values)):
        raise ValueError("PCM contains nonfinite samples")
    return values, hashlib.sha256(blob).hexdigest()


def interval(values, rate, begin, end):
    first, stop = round(begin * rate), round(end * rate)
    if first < 0 or stop <= first or stop > len(values):
        raise ValueError("requested analysis window is outside PCM")
    return values[first:stop], first


def phasor(values, rate, frequency, begin, end):
    data, first = interval(values, rate, begin, end)
    window = np.hanning(len(data))
    times = (first + np.arange(len(data))) / rate
    return 2 * np.dot(data * window, np.exp(-2j * np.pi * frequency * times)) / window.sum()


def interpolate(values, positions):
    # An explicit independent interpolation model, not the correction resampler.
    k = np.floor(positions).astype(np.int64)
    t = positions - k
    valid = (k >= 1) & (k + 2 < len(values))
    result = np.zeros(len(positions))
    j, q = k[valid], t[valid]
    p0, p1, p2, p3 = values[j - 1], values[j], values[j + 1], values[j + 2]
    result[valid] = p1 + .5 * q * (p2 - p0 + q * (
        2 * p0 - 5 * p1 + 4 * p2 - p3 + q * (3 * (p1 - p2) + p3 - p0)))
    return result


def startup_delay(stimulus, capture, rate, lower, upper):
    # Search a Helper-only early window, retaining the true startup envelope.
    # Scalar real gain is profiled at each delay.  This selects the delay
    # branch; the subsequent two-tone fit supplies the fine AC-coupling model.
    stride = max(1, round(rate / 12000))
    first = max(0, round((lower - .005) * rate))
    stop = min(len(capture), round((upper + .040) * rate))
    indices = np.arange(first, stop, stride)
    times, observed = indices / rate, capture[indices]

    def score(delay):
        model = interpolate(stimulus, (times - delay) * rate)
        energy = np.dot(model, model)
        if energy <= 0:
            return float("inf"), 0.0
        gain = np.dot(model, observed) / energy
        residual = observed - gain * model
        return float(np.dot(residual, residual)), float(gain)

    candidates = np.arange(lower, upper + .5 * stride / rate, stride / rate)
    rows = [score(float(t)) for t in candidates]
    winner = min(range(len(rows)), key=lambda j: rows[j][0])
    if winner in (0, len(rows) - 1):
        raise ValueError("startup delay optimum is at the search boundary")
    best = float(candidates[winner])
    # Refine only after a coarse startup branch was selected.  There is no
    # claim that this local optimizer supplies a confidence interval.
    lo, hi = best - stride / rate, best + stride / rate
    golden = (math.sqrt(5) - 1) / 2
    a, b = hi - golden * (hi - lo), lo + golden * (hi - lo)
    fa, fb = score(a)[0], score(b)[0]
    for _ in range(40):
        if fa > fb:
            lo, a, fa = a, b, fb
            b = lo + golden * (hi - lo)
            fb = score(b)[0]
        else:
            hi, b, fb = b, a, fa
            a = hi - golden * (hi - lo)
            fa = score(a)[0]
    best = (lo + hi) / 2
    residual, gain = score(best)
    return best, gain, residual


def pilot_frequency(values, rate, frequency, begin, end):
    centers, coefficients = [], []
    for start in np.arange(begin, end - .5 + 1e-8, .5):
        centers.append(float(start + .25))
        coefficients.append(phasor(values, rate, frequency, float(start), float(start + .5)))
    times, z = np.asarray(centers), np.asarray(coefficients)
    if len(times) < 4:
        raise ValueError("too few pilot windows")
    angles = np.unwrap(np.angle(z))
    t = times - times.mean()
    slope = float(np.dot(t, angles - angles.mean()) / np.dot(t, t))
    residual = angles - angles.mean() - slope * t
    sigma = math.sqrt(float(np.dot(residual, residual)) / (len(t) - 2) / float(np.dot(t, t)))
    return {
        "expected_hz": float(frequency),
        "observed_hz": float(frequency + slope / (2 * np.pi)),
        "frequency_difference_hz": slope / (2 * np.pi),
        "conditional_frequency_fit_standard_error_hz": sigma / (2 * np.pi),
        "phase_rms_about_line_rad": float(np.sqrt(np.mean(residual ** 2))),
        "amplitude_mean": float(np.mean(abs(z))),
        "amplitude_min": float(min(abs(z))),
        "amplitude_max": float(max(abs(z))),
        "windows": len(t),
    }


def highpass(frequency, cutoff, rate):
    pole = math.exp(-2 * math.pi * cutoff / rate)
    z = np.exp(-2j * np.pi * frequency / rate)
    return .5 * (1 + pole) * (1 - z) / (1 - pole * z)


def fit_transfer(frequencies, transfer, delay, rate):
    phases = np.angle(transfer * np.exp(2j * np.pi * frequencies * delay))
    linear = np.stack([1 / frequencies, -2 * np.pi * frequencies], axis=1)
    cutoff, extra_delay = np.linalg.solve(linear, phases)
    if not 0 < cutoff < 500:
        raise ValueError("two-tone transfer does not support this small high-pass model")
    for _ in range(8):
        phase = np.angle(highpass(frequencies, cutoff, rate))
        residual = phase - 2 * np.pi * frequencies * extra_delay - phases
        h = max(.00001, cutoff * 1e-5)
        derivative = np.angle(highpass(frequencies, cutoff + h, rate) /
                              highpass(frequencies, cutoff - h, rate)) / (2 * h)
        jacobian = np.stack([derivative, -2 * np.pi * frequencies], axis=1)
        cutoff, extra_delay = np.asarray([cutoff, extra_delay]) - np.linalg.solve(jacobian, residual)
        if not 0 < cutoff < 500 or not np.isfinite(extra_delay):
            raise ValueError("high-pass transfer fit left the supported range")
    gains = abs(transfer) / abs(highpass(frequencies, cutoff, rate))
    return float(cutoff), float(delay + extra_delay), gains


def noise_model(capture, rate, begin, end):
    data, _ = interval(capture, rate, begin, end)
    mean = float(data.mean())
    residual = data - mean
    nfft = 1 << (2 * len(data) - 1).bit_length()
    spectrum = np.fft.rfft(residual, nfft)
    covariance = np.fft.irfft(abs(spectrum) ** 2, nfft)[:len(data)] / (len(data) - np.arange(len(data)))
    lags = np.arange(round(.001 * rate), round(.040 * rate) + 1, max(1, round(.0005 * rate)))
    time, observed = lags / rate, covariance[lags]
    best = None
    for tau in np.geomspace(.001, .2, 500):
        basis = np.exp(-time / tau)
        variance = max(0., float(np.dot(observed, basis) / np.dot(basis, basis)))
        error = float(np.mean((observed - variance * basis) ** 2))
        if best is None or error < best[0]:
            best = error, float(tau), variance
    _, tau, variance = best
    window = np.hanning(len(data))
    density = 2 * abs(np.fft.rfft(residual * window)) ** 2 / (rate * np.dot(window, window))
    frequencies = np.fft.rfftfreq(len(data), 1 / rate)
    bands = []
    for lo, hi in [(0, 5), (5, 20), (20, 50), (50, 100), (100, 500),
                   (500, 3000), (3000, 8000), (8000, 20000)]:
        selected = (frequencies >= lo) & (frequencies < hi)
        bands.append({"lower_hz": lo, "upper_hz": hi,
                      "rms": float(np.sqrt(sum(density[selected]) * rate / len(data)))})
    return {"dc_offset": mean, "total_rms": float(np.sqrt(np.mean(data ** 2))),
            "demeaned_rms": float(np.sqrt(covariance[0])),
            "white_rms_including_quantization": math.sqrt(max(0., float(covariance[0]) - variance)),
            "colored_stationary_rms": math.sqrt(variance), "colored_tau_seconds": tau,
            "colored_pole": math.exp(-1 / (rate * tau)), "bands": bands}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stimulus")
    parser.add_argument("capture")
    parser.add_argument("--rate", type=int, default=48000)
    parser.add_argument("--audio-ppm", type=float, default=100)
    parser.add_argument("--common-hz", type=float, default=4)
    parser.add_argument("--fit-start", type=float, default=2)
    parser.add_argument("--fit-end", type=float, default=37.5)
    parser.add_argument("--noise-start", type=float, default=.1)
    parser.add_argument("--noise-end", type=float, default=.65)
    parser.add_argument("--onset-min", type=float, default=.70)
    parser.add_argument("--onset-max", type=float, default=.90)
    parser.add_argument("--probe-start", type=float, default=38,
                        help="known source-clock probe start from the stimulus configuration")
    args = parser.parse_args()
    if args.rate != 48000 or not (0 <= args.noise_start < args.noise_end < args.onset_min < args.onset_max):
        raise ValueError("this calibration requires 48 kHz and silent preroll before the startup search")
    scale = 1 + args.audio_ppm * 1e-6
    if not (math.isfinite(scale) and scale > 0 and math.isfinite(args.common_hz)):
        raise ValueError("invalid known stimulus clock or common frequency")
    probe_file_begin = args.probe_start / scale
    if not (args.fit_start < args.fit_end < probe_file_begin):
        raise ValueError("all fitted Helper data must precede the source probe")
    if args.onset_max - args.onset_min > .5:
        raise ValueError("startup delay search must span at most 0.5 seconds")
    if args.onset_max + .040 >= probe_file_begin:
        raise ValueError("all startup and noise calibration data must precede the source probe")
    stimulus, input_hash = read_pcm(args.stimulus, args.rate)
    capture, capture_hash = read_pcm(args.capture, args.rate)
    frequencies = np.asarray([550 * scale + args.common_hz, 2400 * scale + args.common_hz])
    if not (0 < frequencies[0] < frequencies[1] < args.rate / 2):
        raise ValueError("known Helper frequencies are outside the real capture band")
    delay, initial_gain, startup_residual = startup_delay(
        stimulus, capture, args.rate, args.onset_min, args.onset_max)
    if args.fit_end >= delay + probe_file_begin:
        raise ValueError("capture fitting window includes the held-out probe")
    transfer = np.asarray([phasor(capture, args.rate, f, args.fit_start, args.fit_end) /
                           phasor(stimulus, args.rate, f, args.fit_start, args.fit_end) for f in frequencies])
    cutoff, delay, gains = fit_transfer(frequencies, transfer, delay, args.rate)
    gain = float(np.mean(gains))
    pilots = [pilot_frequency(capture, args.rate, f, args.fit_start, args.fit_end) for f in frequencies]
    # This validation is deliberately after all model fitting.  It cannot
    # alter gain, high-pass cutoff, delay, clock or receiver synchronization.
    probe_frequency = 1600 * scale + args.common_hz
    pstart, pend = args.probe_start + .05, args.probe_start + 1.95
    file_start, file_end = pstart / scale, pend / scale
    actual = phasor(capture, args.rate, probe_frequency, delay + file_start, delay + file_end) / \
        phasor(stimulus, args.rate, probe_frequency, file_start, file_end)
    predicted = gain * highpass(probe_frequency, cutoff, args.rate) * np.exp(-2j * np.pi * probe_frequency * delay)
    result = {
        "schema": "helper-cable-calibration-v1", "numpy_version": np.__version__,
        "sample_rate_hz": args.rate, "sample_format": "mono-f32le",
        "stimulus_sha256": input_hash, "capture_sha256": capture_hash,
        "fit_windows": {"helper_stimulus_and_capture_file_seconds": [args.fit_start, args.fit_end],
                        "silent_capture_seconds": [args.noise_start, args.noise_end],
                        "startup_capture_search_seconds": [args.onset_min, args.onset_max]},
        "known_stimulus": {"audio_ppm": args.audio_ppm, "common_hz": args.common_hz},
        "model": {"gain": gain, "highpass_cutoff_hz": cutoff,
                  "highpass_pole": math.exp(-2 * math.pi * cutoff / args.rate),
                  "absolute_capture_latency_seconds": delay,
                  "extra_latency_after_750ms_preroll_ms": (delay - .75) * 1000,
                  "highpass_recurrence": "y[n]=a*y[n-1]+(1+a)/2*(x[n]-x[n-1])",
                  "per_pilot_gain_after_highpass": gains.tolist(),
                  "startup_profile_gain": initial_gain, "startup_squared_residual": startup_residual},
        "pilots": pilots,
        "preroll_noise": noise_model(capture, args.rate, args.noise_start, args.noise_end),
        "heldout_probe_validation": {"source_seconds": [pstart, pend],
                                     "stimulus_file_seconds": [file_start, file_end],
                                     "frequency_hz": probe_frequency,
                                     "gain_error_db": float(20 * np.log10(abs(actual / predicted))),
                                     "phase_error_degrees": float(np.angle(actual / predicted) * 180 / np.pi)},
        "limits": ["Empirical two-tone transfer fit is not a unique identification of the physical circuit.",
                   "Only preroll and Helper data fit the model; the probe validates it without refitting.",
                   "The source probe start is supplied from the known stimulus configuration.",
                   "Noise covariance comes from one short preroll and does not establish stationarity.",
                   "Conditional pilot phase-fit standard errors exclude model error and autocorrelation.",
                   "No actual hardware noise realization is regenerated by this model."],
    }
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as exc:
        raise SystemExit(str(exc)) from exc
