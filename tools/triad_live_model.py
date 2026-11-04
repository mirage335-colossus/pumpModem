"""Small, explicit audio-loopback identification model for the isolated triad study.

This module never opens hardware.  The fitted operator describes the *combined*
playback/cable/capture path on the transmit sample grid.  It is not an ADC or RF
calibration and it supplies no operational detection/admission probability.
"""
from __future__ import annotations

import hashlib
import math
from functools import lru_cache
from typing import Any

import numpy as np
from scipy import optimize, signal

SCHEMA = "triad-live-audio-profile-v1"


def _samples(x):
    a = np.asarray(x)
    if a.ndim != 1 or not len(a) or not np.all(np.isfinite(a)):
        raise ValueError("nonempty finite mono samples are required")
    return a.astype(np.complex128 if np.iscomplexobj(a) else np.float64, copy=False)


def _rate(fs):
    if not math.isfinite(float(fs)) or fs < 1000 or int(fs) != fs:
        raise ValueError("sample rate must be an integer at least 1000 Hz")
    return int(fs)


def _sync(fs, seed=0):
    n = round(.3 * fs)
    t = np.arange(n) / fs
    # Public measurement marker.  This is not part of the modem wire format.
    phi = float(np.random.default_rng(seed).uniform(-np.pi, np.pi))
    x = .12 * signal.chirp(t, f0=min(600, .08 * fs),
                          f1=min(10000, .40 * fs), t1=n / fs, phi=phi * 180 / np.pi)
    return x * signal.windows.tukey(n, alpha=.15)


def wrap_with_markers(payload, fs, *, seed=0, leading_quiet=.5, trailing_quiet=.5):
    """Wrap a payload in independent, public synchronization markers.

    ``markers`` always refer to the returned transmit array.  Alignment never
    searches the payload or uses its bit label.  Quiet intervals retain tails.
    """
    fs = _rate(fs)
    payload = _samples(payload)
    if np.iscomplexobj(payload):
        raise ValueError("physical probe payload must be real mono PCM")
    if leading_quiet < .1 or trailing_quiet < .1:
        raise ValueError("marker quiet intervals must be at least 0.1 seconds")
    q0 = np.zeros(round(leading_quiet * fs))
    q1 = np.zeros(round(trailing_quiet * fs))
    marker = _sync(fs, seed)
    # Distinguishable sweeps prevent short payloads from putting two equivalent
    # peaks in the first marker's transport-delay acquisition interval.
    end_marker = _sync(fs, seed + 1)[::-1]
    pre = np.concatenate((q0, marker, q1))
    start = len(pre)
    last = start + len(payload) + len(q1)
    x = np.concatenate((pre, payload, q1, end_marker, q1)).astype(np.float32)
    meta = {"schema": "triad-live-probe-v1", "sample_rate": fs,
            "seed": int(seed), "samples": len(x),
            "markers": [{"start": len(q0), "samples": len(marker)},
                        {"start": last, "samples": len(marker)}],
            "payload": {"start": start, "samples": len(payload)},
            "segments": [], "measurement_markers_not_modem_symbols": True}
    return x, meta


def generate_probe(fs, seed, levels=(.03, .1, .3), period_seconds=4, repeats=4):
    """Create 57.6 seconds of periodic broad-band identification at defaults.

    Every fourth Fourier bin is omitted as a distortion/leakage diagnostic.
    First/last periods at each level are not used in the transfer estimator.
    Replicate counts are periods, not Fourier-bin or overlapping-window counts.
    """
    fs = _rate(fs)
    levels = tuple(float(x) for x in levels)
    if not levels or any(not 0 < x <= .35 for x in levels):
        raise ValueError("probe levels must be in (0, .35]")
    if not 4 <= repeats <= 32 or not .25 <= period_seconds <= 8:
        raise ValueError("probe requires 4..32 periods of .25..8 seconds")
    n = int(round(period_seconds * fs))
    f = np.fft.rfftfreq(n, 1 / fs)
    bins = np.arange(len(f))
    excited = (f >= max(.25, 1 / period_seconds)) & (f <= min(21000, .44 * fs)) & (bins % 4 != 0)
    spectrum = np.zeros(len(f), np.complex128)
    rng = np.random.default_rng(seed)
    spectrum[excited] = np.exp(1j * rng.uniform(-np.pi, np.pi, int(excited.sum())))
    period = np.fft.irfft(spectrum, n)
    period /= np.max(np.abs(period))
    chunks = [np.zeros(fs)]
    segments = [{"kind": "quiet", "start": 0, "samples": fs}]
    position = fs
    for level in levels:
        block = np.tile(period, repeats) * level
        ramp = min(round(.02 * fs), n // 8)
        window = np.sin(np.linspace(0, np.pi / 2, ramp)) ** 2
        block[:ramp] *= window
        block[-ramp:] *= window[::-1]
        chunks.append(block)
        segments.append({"kind": "periodic", "start": position,
                         "samples": len(block), "period_samples": n,
                         "repeats": repeats, "level": level,
                         "usable_periods": list(range(1, repeats - 1))})
        position += len(block)
        chunks.append(np.zeros(fs))
        segments.append({"kind": "quiet", "start": position, "samples": fs})
        position += fs
    t = np.arange(2 * fs) / fs
    tone = max(levels) * .001 * np.sin(2 * np.pi * min(997, fs / 8) * t)
    tone *= signal.windows.tukey(len(tone), alpha=.05)
    chunks.extend((tone, np.zeros(fs)))
    segments.extend(({"kind": "low-tone", "start": position, "samples": len(tone)},
                     {"kind": "quiet", "start": position + len(tone), "samples": fs}))
    x, meta = wrap_with_markers(np.concatenate(chunks), fs, seed=seed)
    shift = meta["payload"]["start"]
    meta["segments"] = [{**s, "start": s["start"] + shift} for s in segments]
    meta.update(period_samples=n, excited_bins=bins[excited].tolist(),
                period_seconds=n / fs, levels=list(levels), repeats=repeats,
                sample_sha256=hashlib.sha256(x.astype("<f4").tobytes()).hexdigest())
    return x, meta


def generate_low_frequency_probe(fs, seed, *, training=True, level=.20, validation_variant=0):
    """Concentrated LF probe: 128 s training or independent 64 s validation.

    Five-second quiet margins retain low-frequency settling/tails. Validation
    uses different frequencies, a different amplitude and a burst/step after
    its periodic block. Frequencies below .5 Hz remain model extrapolation.
    """
    fs = _rate(fs)
    if not 0 < level <= .30:
        raise ValueError("low-frequency probe peak must be in (0, .30]")
    if validation_variant not in (0, 1) or (training and validation_variant != 0):
        raise ValueError("validation_variant must be 0 or 1 and is only for validation")
    seconds = 32 if training else 16
    frequencies = ([.5, .625, .75, .9375, 1.25, 1.5, 2., 2.5, 3., 4., 5., 7.,
                    10., 15., 20., 30., 45., 60., 80.]
                   if training else [.5625, .8125, 1., 1.8125, 2.8125,
                                     4.8125, 8.8125, 12.8125, 24.8125, 39.8125, 64.8125, 79.8125])
    if not training and validation_variant == 1:
        frequencies = [.6875, .875, 1., 1.6875, 2.6875, 4.6875,
                       8.6875, 12.6875, 24.6875, 39.6875, 64.6875, 79.6875]
    n = seconds * fs
    bins = np.rint(np.asarray(frequencies) * seconds).astype(int)
    spectrum = np.zeros(n // 2 + 1, complex)
    spectrum[bins] = np.exp(1j * np.random.default_rng(seed).uniform(-np.pi, np.pi, len(bins)))
    period = np.fft.irfft(spectrum, n)
    period *= level / np.max(abs(period))
    block = np.tile(period, 4)
    ramp = round(.02 * fs)
    window = np.sin(np.linspace(0, np.pi / 2, ramp)) ** 2
    block[:ramp] *= window; block[-ramp:] *= window[::-1]
    quiet = np.zeros(5 * fs)
    payload = np.r_[quiet, block, quiet]
    if not training:
        t = np.arange(4 * fs) / fs
        burst = level * .3 * np.sin(2 * np.pi * 2.3125 * t) * (t % 1. < .5)
        burst += level * .1 * ((t > 1.) & (t < 2.))
        payload = np.r_[payload, burst, quiet]
    x, meta = wrap_with_markers(payload, fs, seed=seed)
    start = meta["payload"]["start"]
    meta.update(period_samples=n, period_seconds=seconds, excited_bins=bins.tolist(),
                levels=[level], repeats=4, low_frequency_role="training" if training else "validation",
                validation_variant=validation_variant if not training else None,
                below_frequency_hz_is_extrapolation=.5,
                sample_sha256=hashlib.sha256(x.astype("<f4").tobytes()).hexdigest())
    meta["segments"] = [dict(kind="quiet", start=start, samples=len(quiet)),
                        dict(kind="periodic", start=start + len(quiet), samples=len(block),
                             period_samples=n, repeats=4, level=level, usable_periods=[1, 2]),
                        dict(kind="quiet", start=start + len(quiet) + len(block), samples=len(quiet))]
    return x, meta


def _marker_delay(tx, rx, fs, marker, expected=None, max_ppm=2000):
    start, n = int(marker["start"]), int(marker["samples"])
    if start < 0 or n < 32 or start + n > len(tx):
        raise ValueError("invalid public marker support")
    template = tx[start:start + n]
    if expected is None:
        lo, hi = max(0, start - fs), min(len(rx) - n + 1, start + 5 * fs)
    else:
        radius = max(round(.15 * fs), round(abs(start) * max_ppm * 1e-6))
        center = round(start + expected)
        lo, hi = max(0, center - radius), min(len(rx) - n + 1, center + radius + 1)
    if hi - lo < 3:
        raise ValueError("capture does not contain the public marker search interval")
    observed = rx[lo:hi + n - 1]
    corr = signal.correlate(observed, template, mode="valid", method="fft")
    index = int(np.argmax(np.abs(corr)))
    if index == 0 or index == len(corr) - 1:
        raise ValueError("public marker alignment optimum at search boundary")
    mag = np.abs(corr[index - 1:index + 2])
    denom = mag[0] - 2 * mag[1] + mag[2]
    fraction = float(.5 * (mag[0] - mag[2]) / denom) if denom else 0.
    fraction = float(np.clip(fraction, -.5, .5))
    center = lo + index
    data = rx[center:center + n]
    coherence = float(abs(np.vdot(template, data)) ** 2 /
                      max(float(np.vdot(template, template).real * np.vdot(data, data).real), 1e-300))
    if coherence < .3:
        raise ValueError(f"public marker correlation is too weak ({coherence:.3g})")
    return {"tx_start": start, "rx_start": center + fraction,
            "delay_samples": center + fraction - start,
            "squared_correlation": coherence,
            "peak_fraction_samples": fraction}


@lru_cache(maxsize=2)
def _sinc_table(half_width):
    phases = np.arange(1025) / 1024
    offsets = np.arange(-half_width, half_width + 1)
    distance = phases[:, None] - offsets[None, :]
    window = np.i0(7.5 * np.sqrt(np.maximum(0., 1 - (distance / (half_width + 1)) ** 2))) / float(np.i0(7.5))
    table = np.sinc(distance) * window
    return table / np.sum(table, axis=1)[:, None]


def _sinc_sample(x, positions, half_width=16):
    """Bounded-memory, windowed-sinc fractional clock resampling.

    Four-point interpolation noticeably attenuates high-audio-frequency test
    cases as the fractional position changes.  The 33-tap kernel preserves the
    declared probe band much better; the Nyquist edge remains unqualified.
    """
    k = np.floor(positions).astype(np.int64)
    q = positions - k
    valid = (k >= half_width) & (k + half_width < len(x))
    y = np.zeros(len(k), dtype=x.dtype)
    offsets = np.arange(-half_width, half_width + 1)
    table = _sinc_table(half_width)
    # At most about 18 MiB of scratch, independent of recording duration.
    for begin in range(0, len(k), 32768):
        end = min(begin + 32768, len(k))
        ids = np.flatnonzero(valid[begin:end]) + begin
        if not len(ids):
            continue
        phase = q[ids] * 1024
        index = np.minimum(1023, phase.astype(int))
        fraction = (phase - index)[:, None]
        weights = table[index] * (1 - fraction) + table[index + 1] * fraction
        y[ids] = np.sum(x[k[ids, None] + offsets[None, :]] * weights, axis=1)
    return y, valid


def align_capture(tx, rx, fs, markers):
    """Public-marker affine alignment; retain a causal guard in the output.

    The fixed 256/48000-second guard keeps the identified residual filter causal
    after marker peak alignment.  It is part of the model, not removed later.
    Relative clock evidence is conditional on the markers and chosen audio path;
    a shared codec does not characterize independent-radio clock accuracy.
    """
    fs = _rate(fs)
    tx, rx = _samples(tx), _samples(rx)
    if len(markers) < 2:
        raise ValueError("at least two independent public markers are required")
    results = []
    for marker in markers:
        results.append(_marker_delay(tx, rx, fs, marker,
                                     None if not results else results[0]["delay_samples"]))
    centers = np.array([m["tx_start"] for m in results], float)
    delays = np.array([m["delay_samples"] for m in results], float)
    if np.ptp(centers) < fs:
        raise ValueError("marker separation must exceed one second")
    slope, intercept = np.polyfit(centers, delays, 1)
    if abs(slope) > .002:
        raise ValueError("relative sample-clock fit exceeds the declared 2000-ppm domain")
    guard = max(8, round(fs * 256 / 48000))
    positions = np.arange(len(tx), dtype=float) * (1 + slope) + intercept - guard
    aligned, valid = _sinc_sample(rx, positions)
    residual = delays - (intercept + slope * centers)
    report = {"schema": "triad-live-marker-alignment-v1", "markers": results,
              "offset_samples": float(intercept), "sample_rate_ratio": float(1 + slope),
              "relative_rate_ppm": float(slope * 1e6), "causal_guard_samples": guard,
              "marker_residual_rms_samples": float(np.sqrt(np.mean(residual ** 2))),
              "rate_uncertainty_ppm": None,
              "uncertainty_note": "Two marker locations identify an affine map but do not provide independent residual degrees of freedom; drift beyond this interval is unmeasured.",
              "valid_samples": int(valid.sum()), "interpolator": "33-tap Kaiser-windowed sinc, linearly interpolated 1024-phase table, 32768-sample tiles",
              "alignment_uses_payload": False, "absolute_clock_accuracy_measured": False}
    if len(results) > 2:
        variance = float(np.sum(residual ** 2) / (len(results) - 2))
        report["rate_uncertainty_ppm"] = math.sqrt(variance / np.sum((centers - centers.mean()) ** 2)) * 1e6
        report["uncertainty_note"] = "Conditional affine-fit standard error; correlated marker errors and long-term clock changes are not bounded."
    return aligned, valid, report


def _hp_coefficients(cutoff, fs):
    if cutoff <= 0:
        return np.array([1.]), np.array([1.])
    pole = math.exp(-2 * math.pi * cutoff / fs)
    return np.array([.5 * (1 + pole), -.5 * (1 + pole)]), np.array([1., -pole])


def _hp_response(f, cutoff, fs):
    if cutoff <= 0:
        return np.ones(len(np.atleast_1d(f)), complex)
    p = math.exp(-2 * math.pi * cutoff / fs)
    z = np.exp(-2j * np.pi * np.asarray(f) / fs)
    return .5 * (1 + p) * (1 - z) / (1 - p * z)


def _fit_hp(f, h, coherence, fs, guard):
    use = (f >= max(f[1], .25)) & (f <= min(200, .05 * fs)) & (coherence > .8)
    ids = np.flatnonzero(use)
    if len(ids) < 6:
        return 0., {"status": "unidentified", "reason": "too few coherent low-frequency probe bins"}
    targets = np.geomspace(f[ids[0]], f[ids[-1]], min(64, len(ids)))
    ids = np.unique(ids[np.argmin(abs(f[ids, None] - targets), axis=0)])
    ff, hh = f[ids], h[ids]
    gain0 = float(np.median(abs(hh[-max(3, len(hh) // 4):])))

    def fun(p):
        gain, cutoff, delay = p
        model = gain * _hp_response(ff, cutoff, fs) * np.exp(-2j * np.pi * ff * delay)
        error = (model - hh) / max(gain0, 1e-12)
        return np.r_[error.real, error.imag]

    result = optimize.least_squares(fun, [max(gain0, 1e-9), 10., guard / fs],
                                    bounds=([1e-12, 1e-5, guard / fs - .05],
                                            [max(gain0 * 10, 10), min(1000, .1 * fs), guard / fs + .05]),
                                    max_nfev=100)
    cutoff = float(result.x[1])
    rms = float(np.sqrt(np.mean(fun(result.x) ** 2)))
    return cutoff, {"status": "fitted" if result.success else "fit_not_converged",
                    "cutoff_hz": cutoff, "conditional_complex_fit_rms": rms,
                    "bins": int(len(ids)), "frequency_low_hz": float(ff[0]),
                    "frequency_high_hz": float(ff[-1]),
                    "note": "A one-pole combined-path approximation; frequencies below measured support are extrapolation."}


def quantize_production_playback(x):
    """Exact native playback PCM contract, expressed in signed-16 sample units.

    Unit-gain native playback truncates float32 ``clamp(x)*32767`` into int16.
    The source format and multiplication rounding are part of this contract;
    non-unit playback gain follows a different native branch.  The returned
    float is that integer divided by 32768; it does not claim ADC resolution.
    Quantize the complete real composite, never individual carrier labels.
    """
    x = _samples(x)
    if np.iscomplexobj(x):
        raise ValueError("native physical playback requires real PCM")
    pcm = np.trunc(np.clip(x.astype(np.float32), -1., 1.) * np.float32(32767.))
    return pcm.astype(np.float64) / 32768.


def _cascade_sos(real_pole_hz, biquad_hz, q, fs, second_real_pole_hz=None):
    """Unity-HF one/two real HP poles followed by a stable HP biquad."""
    if second_real_pole_hz is None:
        b1, a1 = signal.bilinear([1., 0.], [1., 2 * np.pi * real_pole_hz], fs=fs)
        first = np.r_[b1, 0., a1, 0.]
    else:
        denominator = np.polymul([1., 2*np.pi*real_pole_hz], [1., 2*np.pi*second_real_pole_hz])
        b1, a1 = signal.bilinear([1., 0., 0.], denominator, fs=fs)
        first = np.r_[b1, a1]
    w = 2 * np.pi * biquad_hz
    b2, a2 = signal.bilinear([1., 0., 0.], [1., w / q, w * w], fs=fs)
    return np.array([first, np.r_[b2, a2]])


def _validated_section_sos(section, fs):
    if section.get("sample_rate") != fs or section.get("schema") != "triad-live-low-frequency-section-v1":
        raise ValueError("low-frequency section schema/sample rate mismatch")
    order = section.get("model_order", 3)
    expected_law = {3: "one_real_hp_pole_plus_hp_biquad", 4: "two_real_hp_poles_plus_hp_biquad"}
    if order not in expected_law or section.get("model_law", expected_law[order]) != expected_law[order]:
        raise ValueError("LF model order and law metadata disagree")
    sos = np.asarray(section["sos"], float)
    if sos.shape != (2, 6) or not np.all(np.isfinite(sos)) or not np.all(sos[:, 3] == 1):
        raise ValueError("low-frequency section requires two finite normalized SOS rows")
    for row in sos:
        if np.any(abs(np.roots(row[3:])) >= 1):
            raise ValueError("low-frequency SOS poles must be strictly inside the unit circle")
    hf = np.prod((sos[:, 0] - sos[:, 1] + sos[:, 2]) /
                 (sos[:, 3] - sos[:, 4] + sos[:, 5]))
    if not np.isclose(hf, 1., rtol=1e-8, atol=1e-10):
        raise ValueError("low-frequency SOS high-frequency gain must be one")
    return sos


def fit_low_frequency_section(tx, rx, fs, manifest, *, input_pcm="s16-native", model_order=3):
    """Fit a bounded stable third/fourth-order HP using training periods only.

    Period scatter/coherence determine engineering weights, not independent
    hardware confidence. The held-out validation probe is never accepted here.
    """
    fs = _rate(fs)
    if model_order not in (3, 4):
        raise ValueError("LF model_order must be 3 or 4")
    if manifest.get("low_frequency_role") != "training" or manifest.get("sample_rate") != fs:
        raise ValueError("dedicated low-frequency training manifest is required")
    tx, rx = _samples(tx), _samples(rx)
    if input_pcm not in ("float", "s16-native"):
        raise ValueError("input_pcm must be float or s16-native")
    source_hash = hashlib.sha256(tx.astype("<f4").tobytes()).hexdigest()
    if input_pcm == "s16-native":
        tx = quantize_production_playback(tx)
    y, valid, alignment = align_capture(tx, rx, fs, manifest["markers"])
    n = int(manifest["period_samples"])
    bins = np.asarray(manifest["excited_bins"], int)
    if n < 64 or len(bins) < 8 or np.any(bins <= 0) or np.any(bins >= n // 2):
        raise ValueError("insufficient low-frequency training geometry")
    xx, yy = [], []
    for segment in manifest["segments"]:
        if segment["kind"] != "periodic":
            continue
        for period in segment["usable_periods"]:
            a = int(segment["start"] + period * n); b = a + n
            if b > len(y) or not np.all(valid[a:b]):
                raise ValueError("low-frequency training period is not fully captured")
            xx.append(np.fft.rfft(tx[a:b])[bins]); yy.append(np.fft.rfft(y[a:b])[bins])
    if len(xx) < 2:
        raise ValueError("two disjoint LF training periods are required")
    xx, yy = np.asarray(xx), np.asarray(yy)
    cross = np.sum(yy * xx.conj(), axis=0)
    px, py = np.sum(abs(xx) ** 2, axis=0), np.sum(abs(yy) ** 2, axis=0)
    h = cross / np.maximum(px, 1e-300)
    coherence = np.clip(abs(cross) ** 2 / np.maximum(px * py, 1e-300), 0, 1)
    period_h = yy / np.where(abs(xx) > 1e-20, xx, complex(1e-20))
    scatter = np.sqrt(np.mean(abs(period_h - period_h.mean(axis=0)) ** 2, axis=0))
    f = bins * fs / n
    use = (f >= .5) & (f <= 80.) & (coherence >= .8)
    if use.sum() < 8 or not np.any(f[use] <= 1.5) or not np.any(f[use] >= 40):
        raise ValueError("insufficient coherent LF support to identify the selected HP model")
    gain = max(float(np.median(abs(h[use][-4:]))), 1e-4)
    weight = np.sqrt(coherence[use]) / np.sqrt(scatter[use] ** 2 + (.01 * gain) ** 2)
    guard_seconds = alignment["causal_guard_samples"] / fs

    def response(parameters, frequencies):
        if model_order == 3:
            g, pole, corner, q, delay = parameters
            second = None
        else:
            g, pole, second, corner, q, delay = parameters
        _, shape = signal.sosfreqz(_cascade_sos(pole, corner, q, fs, second), worN=2 * np.pi * frequencies / fs)
        return g * shape * np.exp(-2j * np.pi * frequencies * delay)

    def objective(parameters):
        e = (response(parameters, f[use]) - h[use]) * weight
        return np.r_[e.real, e.imag]

    lower = np.array([gain * .2, .05, .05, .25, max(0., guard_seconds - .002)])
    upper = np.array([gain * 5, 80., 40., 1.5, guard_seconds + .002])
    initial = [gain, 8., 2., .707, guard_seconds]
    if model_order == 4:
        lower = np.insert(lower, 2, .01)
        upper = np.insert(upper, 2, 5.)
        initial.insert(2, .3)
    fit = optimize.least_squares(objective, initial,
                                 bounds=(lower, upper), max_nfev=200)
    prediction = response(fit.x, f)
    difference = prediction[use] - h[use]
    gain_error = 20 * np.log10(np.maximum(abs(prediction[use]), 1e-300) / np.maximum(abs(h[use]), 1e-300))
    phase_error = np.angle(prediction[use] * h[use].conj())
    on_boundary = bool(np.any((fit.x - lower) < .001 * (upper - lower)) or
                       np.any((upper - fit.x) < .001 * (upper - lower)))
    flags = []
    if not fit.success: flags.append("fit_not_converged")
    if on_boundary: flags.append("fit_parameter_at_boundary")
    if np.max(abs(gain_error)) > 2 or np.max(abs(phase_error)) > .2:
        flags.append("training_complex_response_mismatch")
    poles = sorted(fit.x[1:2 if model_order == 3 else 3], reverse=True)
    corner, q, delay = fit.x[-3:]
    section = dict(schema="triad-live-low-frequency-section-v1", sample_rate=fs,
        model_order=model_order,
        model_law="one_real_hp_pole_plus_hp_biquad" if model_order == 3 else "two_real_hp_poles_plus_hp_biquad",
        real_poles_hz_descending=[float(p) for p in poles],
        real_pole_symmetry="real-pole factors commute; descending corners label the same physical operator uniquely",
        sos=_cascade_sos(poles[0], corner, q, fs, poles[1] if model_order == 4 else None).tolist(),
        real_pole_hz=float(poles[0]), biquad_frequency_hz=float(corner), biquad_q=float(q),
        residual_gain=float(fit.x[0]), residual_delay_seconds=float(delay),
        measured_frequency_support_hz=[float(f[use].min()), float(f[use].max())],
        residual_join_hz=80., residual_blend_end_hz=160., input_pcm_contract=input_pcm,
        alignment=alignment, source_sample_sha256=source_hash,
        training_sample_sha256=hashlib.sha256(tx.astype("<f4").tobytes()).hexdigest(),
        disjoint_period_count=len(xx), independent_hardware_captures=1,
        measured_transfer=dict(frequency_hz=f.tolist(), real=h.real.tolist(), imag=h.imag.tolist(),
                               coherence=coherence.tolist(), period_scatter_rms=scatter.tolist(),
                               fit_used=use.tolist()),
        fit=dict(converged=bool(fit.success), complex_rms=float(np.sqrt(np.mean(abs(difference) ** 2))),
                 maximum_gain_error_db=float(np.max(abs(gain_error))),
                 maximum_phase_error_radians=float(np.max(abs(phase_error))),
                 weighting="sqrt(coherence)/sqrt(period_scatter^2+(0.01*high_band_gain)^2); engineering weights, not confidence"),
        training_flags=flags, validation_status="training_fit_only_new_independent_holdout_required",
        limits=["Below 0.5 Hz is always extrapolation; no identified millihertz sensitivity.",
                "Selected real HP pole factor(s) and one stable HP biquad are a combined-path model assumption.",
                "Disjoint periods are not independent hardware captures; no operational admission calibration."])
    _validated_section_sos(section, fs)
    return section


def fit_audio_profile(tx, rx, fs, manifest, *, input_pcm="float", low_frequency_section=None):
    """Fit only dedicated training-probe periods; return a frozen JSON profile."""
    fs = _rate(fs)
    tx, rx = _samples(tx), _samples(rx)
    source_hash = hashlib.sha256(tx.astype("<f4").tobytes()).hexdigest()
    if input_pcm not in ("float", "s16-native"):
        raise ValueError("input_pcm must be float or s16-native")
    if input_pcm == "s16-native":
        tx = quantize_production_playback(tx)
    if manifest.get("sample_rate") != fs:
        raise ValueError("probe sample rate mismatch")
    y, valid, alignment = align_capture(tx, rx, fs, manifest["markers"])
    n = int(manifest["period_samples"])
    bins = np.asarray(manifest["excited_bins"], int)
    if n < 64 or np.any(bins <= 0) or np.any(bins >= n // 2 + 1):
        raise ValueError("invalid probe Fourier geometry")
    f = np.fft.rfftfreq(n, 1 / fs)
    cross = np.zeros(len(f), complex)
    px = np.zeros(len(f)); py = np.zeros(len(f))
    level_responses, period_responses = [], []
    used_periods = 0
    for s in manifest["segments"]:
        if s["kind"] != "periodic":
            continue
        xs, ys = [], []
        for j in s["usable_periods"]:
            a = int(s["start"] + j * n); b = a + n
            if b > len(y) or not np.all(valid[a:b]):
                raise ValueError("training period is not fully captured")
            xx, yy = np.fft.rfft(tx[a:b]), np.fft.rfft(y[a:b])
            cross += yy * xx.conj(); px += abs(xx) ** 2; py += abs(yy) ** 2
            xs.append(xx); ys.append(yy); used_periods += 1
        xx, yy = np.asarray(xs), np.asarray(ys)
        h = np.sum(yy * xx.conj(), axis=0) / np.maximum(np.sum(abs(xx) ** 2, axis=0), 1e-300)
        level_responses.append((float(s["level"]), h))
        period_responses.extend([v[bins] / xx[k, bins] for k, v in enumerate(yy)])
    if used_periods < 2:
        raise ValueError("at least two disjoint training periods are required")
    h = cross / np.maximum(px, 1e-300)
    coherence = np.clip(abs(cross) ** 2 / np.maximum(px * py, 1e-300), 0, 1)
    if low_frequency_section is None:
        cutoff, hp_fit = _fit_hp(f[bins], h[bins], coherence[bins], fs,
                                  alignment["causal_guard_samples"])
        low_response = _hp_response(f[bins], cutoff, fs)
    else:
        sos = _validated_section_sos(low_frequency_section, fs)
        cutoff = 0.
        hp_fit = dict(status="dedicated_low_frequency_section", note="Frozen separate LF training; old one-pole filter is bypassed.")
        _, low_response = signal.sosfreqz(sos, worN=2 * np.pi * f[bins] / fs)
    residual = h[bins] / low_response
    guard_seconds = alignment["causal_guard_samples"] / fs
    # Remove the *known* alignment guard while interpolating/extrapolating the
    # smooth residual.  Holding a delayed complex value constant above the last
    # measured bin would manufacture a noncausal high-frequency component.
    residual *= np.exp(2j * np.pi * f[bins] * guard_seconds)
    # Interpolate complex response, never magnitude alone.  Smoothly constrain
    # the unmeasured upper edge; its response is explicitly unqualified.
    full = np.interp(f, f[bins], residual.real) + 1j * np.interp(f, f[bins], residual.imag)
    if low_frequency_section is not None:
        section = low_frequency_section
        join, blend_end = float(section["residual_join_hz"]), float(section["residual_blend_end_hz"])
        if not 80 <= join < blend_end < fs / 2:
            raise ValueError("LF residual join must be at least 80 Hz with a finite upper blend")
        low_gain = float(section["residual_gain"])
        relative_delay = float(section["residual_delay_seconds"]) - guard_seconds
        if not math.isfinite(low_gain) or low_gain <= 0 or not math.isfinite(relative_delay):
            raise ValueError("invalid LF-supported residual gain/delay")
        low = low_gain * np.exp(-2j * np.pi * f * relative_delay)
        weight = .5 - .5 * np.cos(np.pi * np.clip((f - join) / (blend_end - join), 0., 1.))
        # No old noisy broad-band LF bin is reintroduced below the join.
        full = (1 - weight) * low + weight * full
    end = f[bins[-1]]
    above = f > end
    full[above] *= .5 * (1 + np.cos(np.pi * (f[above] - end) / (fs / 2 - end)))
    full[0] = full[1].real
    full *= np.exp(-2j * np.pi * f * guard_seconds)
    impulse = np.fft.irfft(full, n)
    taps = min(1025, max(65, round(fs * .021333) | 1), n // 4)
    fir = impulse[:taps].copy()
    taper = max(8, taps // 8)
    fir[-taper:] *= .5 * (1 + np.cos(np.linspace(0, np.pi, taper)))
    total_ir_energy = float(np.dot(impulse, impulse))
    omitted_ir = float(np.dot(impulse[taps:], impulse[taps:]) / max(total_ir_energy, 1e-300))
    quiet = []
    for s in manifest["segments"]:
        if s["kind"] == "quiet" and s["samples"] >= fs:
            a = s["start"] + round(.5 * fs); b = s["start"] + s["samples"]
            if np.all(valid[a:b]):
                quiet.append(y[a:b])
    if not quiet:
        raise ValueError("independent quiet intervals are required for the noise description")
    q = np.concatenate(quiet)
    nf, npsd = signal.welch(q, fs=fs, nperseg=min(4096, len(q)), noverlap=0,
                            detrend="constant", scaling="density")
    # Compact frequency-noise description; integration uses density, not FFT-bin power.
    pick = np.unique(np.r_[0, np.round(np.geomspace(1, len(nf) - 1, min(512, len(nf) - 1))).astype(int)])
    pass_bins = bins[(coherence[bins] >= .95) & (abs(h[bins]) > 1e-10)]
    mid = bins[(f[bins] >= min(300, .05 * fs)) & (f[bins] <= min(18000, .4 * fs))]
    gains = [float(np.median(abs(v[mid]))) for _, v in level_responses]
    gain_db = [20 * math.log10(max(v, 1e-300)) for v in gains]
    variation = float(np.ptp(gain_db))
    omitted = (f > max(20, f[bins[0]])) & (f < end) & (np.arange(len(f)) % 4 == 0)
    detection_ratio = float(np.sum(py[omitted]) / max(np.sum(py[bins]), 1e-300))
    period_h = np.asarray(period_responses)
    spread = np.sqrt(np.mean(abs(period_h - period_h.mean(axis=0)) ** 2, axis=0))
    # Retain dense low-frequency evidence and a compact broad-band view.  The
    # fit above used every excited bin; the causal FIR retains its response.
    view = np.unique(np.r_[np.flatnonzero(f[bins] <= 100),
                           np.round(np.linspace(0, len(bins) - 1, min(1024, len(bins)))).astype(int)])
    viewed_bins = bins[view]
    flags = []
    clipping_count = int(np.sum(abs(rx) >= .98))
    if clipping_count:
        flags.append("capture_near_fullscale")
    if variation > .5:
        flags.append("level_dependent_response")
    if omitted_ir > .05:
        flags.append("causal_fir_representation_loss")
    if hp_fit["status"] not in ("fitted", "dedicated_low_frequency_section"):
        flags.append("low_frequency_section_unidentified")
    if low_frequency_section is not None:
        flags.extend("low_frequency_" + flag for flag in low_frequency_section.get("training_flags", []))
    profile: dict[str, Any] = {
        "schema": SCHEMA, "sample_rate": fs,
        "input_pcm_contract": input_pcm,
        "calibration_kind": "combined audio path, marker-aligned transmit grid",
        "highpass_cutoff_hz": cutoff, "highpass_fit": hp_fit,
        "fir": fir.tolist(), "alignment": alignment,
        "noise": {"frequency_hz": nf[pick].tolist(), "psd_fs_squared_per_hz": npsd[pick].tolist(),
                  "quiet_samples": len(q), "quiet_rms": float(np.sqrt(np.mean(q ** 2))),
                  "effective_independent_hardware_captures": 1,
                  "spectral_resolution_hz": float(nf[1] - nf[0]),
                  "measurement_domain": "post-ADC sampled quiet-path residual, including output quantization",
                  "simulation_surrogate": "stationary real colored Gaussian draw on a finite observation; Gaussianity and long-term stationarity are not measured",
                  "note": "Quiet-path noise may differ from signal-dependent residuals. Welch bins are not independent hardware trials; PSD below the spectral resolution is unidentified."},
        "measured_transfer": {"frequency_hz": f[viewed_bins].tolist(), "real": h[viewed_bins].real.tolist(),
                              "imag": h[viewed_bins].imag.tolist(), "coherence": coherence[viewed_bins].tolist(),
                              "period_scatter_rms": spread[view].tolist(), "disjoint_period_count": used_periods,
                              "fitted_bin_count": int(len(bins)), "retained_bin_count": int(len(view))},
        "measured_frequency_support_hz": [float(f[bins[0]]), float(end)],
        "coherent_frequency_extent_hz": [float(f[pass_bins[0]]), float(f[pass_bins[-1]])] if len(pass_bins) else None,
        "coherent_excited_bin_fraction": float(len(pass_bins) / len(bins)),
        "coherent_extent_is_contiguous_band_guarantee": False,
        "level_response": [{"requested_peak": level, "median_gain": gain, "gain_db": db}
                           for (level, _), gain, db in zip(level_responses, gains, gain_db)],
        "level_gain_span_db": variation, "level_dependence_flag": variation > .5,
        "omitted_bin_output_power_ratio": detection_ratio,
        "unretained_impulse_energy_fraction": omitted_ir,
        "capture_near_fullscale_samples": clipping_count,
        "training_flags": flags,
        "linear_training_checks_pass": not flags,
        "training_sample_sha256": hashlib.sha256(tx.astype("<f4").tobytes()).hexdigest(),
        "source_sample_sha256": source_hash,
        "validation_status": "training_fit_only_holdout_required",
        "operational_admission_calibrated": False,
        "limits": ["No independent DAC/ADC separation or absolute voltage/RF calibration.",
                   "Response outside measured frequency support is model extrapolation.",
                   "Shared-clock sample-rate ratio does not establish independent-radio clock stability.",
                   "No simulated channel effects are inferred from a cable measurement.",
                   "A single physical capture cannot establish rare-event reception or false-admission rates."],
    }
    if low_frequency_section is not None:
        profile["low_frequency_section"] = low_frequency_section
        profile["model_extrapolation_below_hz"] = max(.5, low_frequency_section["measured_frequency_support_hz"][0])
        profile["residual_low_frequency_constraint"] = {
            "through_hz": low_frequency_section["residual_join_hz"],
            "blend_end_hz": low_frequency_section["residual_blend_end_hz"],
            "source": "dedicated LF gain/delay; noisy broadband LF bins excluded before FIR construction",
            "finite_fir_approximation_requires_validation": True}
        profile["limits"].append("Below 0.5 Hz remains extrapolation even with the dedicated LF section.")
    profile["training_prediction"] = assess_prediction(tx, y, profile, valid)
    return profile


def simulate_audio_profile(x, profile, *, seed=0, include_noise=True,
                           production_s16=None, capture_lattice=False,
                           full_tail=False, max_tail_seconds=60.):
    """Declared surrogate: exact TX PCM, fixed path, measured post-ADC noise PSD.

    The colored Gaussian residual is a simulation assumption, not a property
    established by measuring its PSD.  It is added *after* the deterministic
    model; the measured PSD already contains output quantization.  Optional
    ``capture_lattice`` rounds the result to an ideal S16 lattice and therefore
    adds an approximation not identified by the API or the training capture.
    Operational admission calibration is deliberately absent.
    """
    x = _samples(x)
    if production_s16 is None:
        production_s16 = profile.get("input_pcm_contract") == "s16-native"
    if production_s16:
        x = quantize_production_playback(x)
    result = apply_audio_profile(x, profile, full_tail=full_tail,
                                 max_tail_seconds=max_tail_seconds)
    if include_noise:
        if np.iscomplexobj(result):
            raise ValueError("measured real-audio noise cannot be silently interpreted as complex I/Q noise")
        fs = _rate(profile["sample_rate"])
        noise = profile["noise"]
        f = np.asarray(noise["frequency_hz"], float)
        p = np.asarray(noise["psd_fs_squared_per_hz"], float)
        if len(f) < 2 or len(f) != len(p) or np.any(np.diff(f) <= 0) or np.any(~np.isfinite(p)) or np.any(p < 0):
            raise ValueError("invalid measured post-ADC noise PSD")
        n = len(result)
        bins = np.fft.rfftfreq(n, 1 / fs)
        density = np.interp(bins, f, p)
        rng = np.random.default_rng(seed)
        spectrum = (rng.normal(size=len(bins)) + 1j * rng.normal(size=len(bins))) * np.sqrt(density * fs * n / 4)
        spectrum[0] = rng.normal() * math.sqrt(density[0] * fs * n)
        if n % 2 == 0:
            spectrum[-1] = rng.normal() * math.sqrt(density[-1] * fs * n)
        result = result + np.fft.irfft(spectrum, n)
    if capture_lattice:
        if np.iscomplexobj(result):
            raise ValueError("capture lattice requires real PCM")
        result = np.clip(np.rint(result * 32768), -32768, 32767) / 32768
    return result


def _validate_profile(profile):
    if profile.get("schema") != SCHEMA:
        raise ValueError("unknown audio model schema")
    fs = _rate(profile["sample_rate"])
    cutoff = float(profile["highpass_cutoff_hz"])
    fir = _samples(profile["fir"])
    if np.iscomplexobj(fir) or len(fir) > 8193 or not 0 <= cutoff < fs / 4:
        raise ValueError("invalid causal model coefficients")
    if "low_frequency_section" in profile:
        if cutoff != 0:
            raise ValueError("dedicated LF SOS must not be combined with the old one-pole filter")
        _validated_section_sos(profile["low_frequency_section"], fs)
    return fs, cutoff, fir


def _profile_sos(profile, fs, cutoff):
    if "low_frequency_section" in profile:
        return _validated_section_sos(profile["low_frequency_section"], fs)
    b, a = _hp_coefficients(cutoff, fs)
    return np.array([np.r_[np.pad(b, (0, 3 - len(b))), np.pad(a, (0, 3 - len(a)))]])


def apply_audio_profile(x, profile, *, full_tail=False, max_tail_seconds=60.):
    """Apply the fixed real operator to the entire composite, without normalization.

    Alignment removes transport offset/rate error before this operator is used.
    The retained causal guard remains in ``fir``.  No payload-specific fit occurs.
    ``full_tail`` drains the FIR plus twice the IIR's 1e-6 pole-decay span,
    allowing cascade settling; this is not mathematical zero. An unresolved tiny cutoff must not silently allocate
    hours of samples: tails beyond the explicit bound raise; AudioModelState can
    instead stream zeros for a caller-selected duration without truncation.
    """
    x = _samples(x)
    fs, cutoff, fir = _validate_profile(profile)
    sos = _profile_sos(profile, fs, cutoff)
    if full_tail:
        if not math.isfinite(max_tail_seconds) or max_tail_seconds <= 0:
            raise ValueError("max_tail_seconds must be finite and positive")
        tail = len(fir) - 1
        poles = np.concatenate([np.roots(row[3:]) for row in sos])
        radius = float(np.max(abs(poles))) if len(poles) else 0.
        if radius > 0:
            # Cascade state may have repeated-pole polynomial factors. A factor
            # of two is an explicit conservative finite drain allowance.
            tail += math.ceil(2 * math.log(1e-6) / math.log(radius))
        if tail > max_tail_seconds * fs:
            raise ValueError(f"model tail {tail / fs:.6g} seconds exceeds explicit {max_tail_seconds:g}-second bound; use continuous AudioModelState or increase the bound")
        x = np.pad(x, (0, tail))
    filtered = signal.sosfilt(sos, x)
    # Long physical captures and short causal FIRs have strongly unequal sizes.
    # Overlap-add avoids a transform sized to the complete recording while
    # preserving the identical continuous linear convolution and complete tail.
    convolution = signal.oaconvolve if len(filtered) > 8 * len(fir) else signal.fftconvolve
    return convolution(filtered, fir, mode="full")[:len(x)]


class AudioModelState:
    """Bounded streaming state; zeros/guards never reset the filter history."""
    def __init__(self, profile):
        self.fs, self.cutoff, self.fir = _validate_profile(profile)
        self.sos = _profile_sos(profile, self.fs, self.cutoff)
        self.hp_state = np.zeros((len(self.sos), 2))
        self.history = np.zeros(len(self.fir) - 1)

    def process(self, x):
        x = _samples(x)
        if np.iscomplexobj(x) and not np.iscomplexobj(self.hp_state):
            self.hp_state = self.hp_state.astype(complex)
            self.history = self.history.astype(complex)
        hp, self.hp_state = signal.sosfilt(self.sos, x, zi=self.hp_state)
        joined = np.r_[self.history, hp]
        result = signal.fftconvolve(joined, self.fir, mode="valid")
        if len(self.history):
            self.history = joined[-len(self.history):].copy()
        return result


def spectral_response(profile, frequencies):
    fs, cutoff, fir = _validate_profile(profile)
    f = np.asarray(frequencies, dtype=float)
    if np.any(~np.isfinite(f)) or np.any(abs(f) > fs / 2):
        raise ValueError("response frequencies must be finite and within Nyquist")
    _, h = signal.freqz(fir, worN=2 * np.pi * f / fs)
    _, low = signal.sosfreqz(_profile_sos(profile, fs, cutoff), worN=2 * np.pi * f / fs)
    return h * low


def assess_low_frequency_validation(tx, aligned_rx, fs, manifest, profile, mask=None,
                                    *, input_pcm="s16-native"):
    """Independent LF complex-response checks against the frozen complete model.

    Input must already be public-marker aligned on the full transmit grid.
    Period scatter and neighboring unexcited bins provide a diagnostic floor,
    not a confidence bound. No observation-dependent model parameter is fitted.
    """
    fs = _rate(fs)
    tx, y = _samples(tx), _samples(aligned_rx)
    if len(tx) != len(y) or manifest.get("sample_rate") != fs or profile["sample_rate"] != fs:
        raise ValueError("LF validation arrays, manifest and profile must share a sample grid")
    if manifest.get("low_frequency_role") != "validation":
        raise ValueError("an independent LF validation manifest is required")
    if input_pcm == "s16-native":
        tx = quantize_production_playback(tx)
    elif input_pcm != "float":
        raise ValueError("input_pcm must be float or s16-native")
    valid = np.ones(len(tx), bool) if mask is None else np.asarray(mask, bool)
    if valid.shape != tx.shape:
        raise ValueError("invalid LF validation support mask")
    n = int(manifest["period_samples"])
    bins = np.asarray(manifest["excited_bins"], int)
    neighbors = np.array([[b - 1, b + 1] for b in bins])
    if np.any(neighbors < 1) or np.any(neighbors >= n // 2) or np.any(np.isin(neighbors, bins)):
        raise ValueError("validation requires unexcited adjacent diagnostic bins")
    xs, ys, floors = [], [], []
    for segment in manifest["segments"]:
        if segment["kind"] != "periodic": continue
        for period in segment["usable_periods"]:
            a = int(segment["start"] + period * n); b = a + n
            if b > len(y) or not np.all(valid[a:b]):
                raise ValueError("LF validation period is not fully captured")
            xfft, yfft = np.fft.rfft(tx[a:b]), np.fft.rfft(y[a:b])
            xs.append(xfft[bins]); ys.append(yfft[bins])
            floors.append(np.mean(abs(yfft[neighbors]) ** 2, axis=1))
    if len(xs) < 2:
        raise ValueError("two disjoint LF validation periods are required")
    xs, ys = np.asarray(xs), np.asarray(ys)
    cross = np.sum(ys * xs.conj(), axis=0)
    px, py = np.sum(abs(xs) ** 2, axis=0), np.sum(abs(ys) ** 2, axis=0)
    h = cross / np.maximum(px, 1e-300)
    coherence = np.clip(abs(cross) ** 2 / np.maximum(px * py, 1e-300), 0, 1)
    noise_floor = np.sqrt(np.mean(floors, axis=0) / np.maximum(px / len(xs), 1e-300))
    f = bins * fs / n
    predicted = spectral_response(profile, f)
    rows = []
    for ff, hh, pp, cc, floor in zip(f, h, predicted, coherence, noise_floor):
        resolved = bool(cc >= .95 and abs(hh) > 3 * floor)
        gain_error = float(20 * np.log10(max(abs(hh), 1e-300) / max(abs(pp), 1e-300)))
        phase_error = float(np.angle(hh * pp.conjugate()))
        rows.append(dict(frequency_hz=float(ff), measured_real=float(hh.real), measured_imag=float(hh.imag),
                         predicted_real=float(pp.real), predicted_imag=float(pp.imag),
                         coherence=float(cc), diagnostic_response_floor=float(floor),
                         resolved=resolved, energy_gain_error_db=gain_error, phase_error_radians=phase_error,
                         within_envelope=bool(abs(gain_error) <= 2. and abs(phase_error) <= .2) if resolved else None))
    resolved = [row for row in rows if row["resolved"]]
    failures = [row for row in resolved if not row["within_envelope"]]
    one_hz = [row for row in rows if row["frequency_hz"] == 1.]
    return dict(schema="triad-live-low-frequency-validation-v1", rows=rows,
                status="failed" if failures else ("validated_resolved_bins" if resolved and one_hz and one_hz[0]["resolved"] else "unresolved"),
                resolved_count=len(resolved), unresolved_count=len(rows)-len(resolved),
                gain_envelope_db=2., phase_envelope_radians=.2, confidence_interval=None,
                independent_hardware_observations=1, disjoint_period_count=len(xs),
                frozen_model_refitted=False, below_frequency_hz_is_extrapolation=.5,
                limits="Per-bin engineering envelope; no simultaneous statistical guarantee or operational admission claim. Burst/tail behavior requires separate continuous-waveform residual checks.")


def assess_prediction(tx, actualaligned, profile, mask=None, *, prediction=None):
    """Score a fixed prediction without fitting the observed payload.

    A supplied prediction must cover the original complete sample grid; masks
    select the scored support without resetting continuous filter history.
    This permits several diagnostics to reuse one previously computed model.
    """
    tx, actual = _samples(tx), _samples(actualaligned)
    if len(tx) != len(actual):
        raise ValueError("prediction and aligned observation must share the sample grid")
    valid = np.ones(len(tx), bool) if mask is None else np.asarray(mask, bool)
    if valid.shape != tx.shape or valid.sum() < 32:
        raise ValueError("insufficient valid comparison support")
    prediction = apply_audio_profile(tx, profile) if prediction is None else _samples(prediction)
    if prediction.shape != tx.shape:
        raise ValueError("supplied prediction must share the complete observation sample grid")
    x, y, p = tx[valid], actual[valid], prediction[valid]
    input_energy = float(np.vdot(x, x).real)
    actual_energy = float(np.vdot(y, y).real)
    predicted_energy = float(np.vdot(p, p).real)
    residual_energy = float(np.vdot(y - p, y - p).real)
    gain = np.vdot(p, y) / max(predicted_energy, 1e-300)
    explained = abs(np.vdot(p, y)) ** 2 / max(predicted_energy * actual_energy, 1e-300)
    return {"samples": int(valid.sum()), "actual_energy": actual_energy,
            "predicted_energy": predicted_energy, "input_energy": input_energy,
            "actual_energy_gain_db": 10 * math.log10(max(actual_energy / max(input_energy, 1e-300), 1e-300)),
            "predicted_energy_gain_db": 10 * math.log10(max(predicted_energy / max(input_energy, 1e-300), 1e-300)),
            "energy_prediction_error_db": 10 * math.log10(max(actual_energy / max(predicted_energy, 1e-300), 1e-300)),
            "fixed_model_residual_to_signal_db": 10 * math.log10(max(residual_energy / max(actual_energy, 1e-300), 1e-300)),
            "projection_fraction_diagnostic": float(min(1., explained)),
            "diagnostic_fitted_gain_real": float(np.real(gain)),
            "diagnostic_fitted_gain_imag": float(np.imag(gain)),
            "gain_fit_applied_to_prediction": False,
            "operational_admission_claimed": False}
