#!/usr/bin/env python3
"""Bidirectional interference metrology for matched, already-propagated exports.

This diagnostic never resynthesizes a carrier or gates a received waveform.
Three controls recover the DATA, Helper and shared-noise components by linear
subtraction. An optional both-off control independently checks that assumption.
The supplied 36-second Helper aperture must overlap received DATA; measuring
only the mandatory Helper lead cannot test DATA-to-Helper interference.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import time

import numpy as np
from scipy import signal

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from triad_helper_assistance import estimate_frequency, tone_offsets
from triad_helper_receiver import RAW_SAMPLE_RATE, DECIMATION, data_fir_taps
from triad_helper_ramp_diagnostic import read_capture


TARGETS = {
    "useful_energy_loss_db": .1,
    "power_based_noise_equivalent_penalty_db": .2,
    "helper_frequency_or_phase_error_increase_fraction": .05,
    "scope": "Provisional engineering targets, not confidence-qualified population limits.",
}


def _hash(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def _energy(values):
    return float(np.vdot(values, values).real)


def _db_ratio(numerator, denominator):
    if denominator <= 0:
        return None
    return 10 * math.log10(max(float(numerator), 1e-300) / float(denominator))


def template_energy_metrics(directory):
    """Measure source energy/discrimination without a fixed-rate spectral band."""
    folder = Path(directory)
    m = json.loads((folder / "manifest.json").read_text())
    meta = m["templates"]
    shape = tuple(meta["shape"])
    path = folder / meta["file"]
    if (not meta.get("enabled", False) or meta["dtype"] != "<c16" or len(shape) != 3 or
            shape[1] != 2 or path.stat().st_size != math.prod(shape) * 16):
        raise ValueError("complete two-bit public complex templates required")
    index = meta["epoch_offsets_seconds"].index(0)
    values = np.memmap(path, mode="r", dtype="<c16", shape=shape)
    a, b = values[index]
    ea, eb, difference = _energy(a), _energy(b), _energy(a - b)
    overlap = abs(np.vdot(a, b))
    fs = float(m["sample_rate_hz"])
    return {
        "template_sha256": _hash(path), "shape": list(shape), "epoch_offset_seconds": 0,
        "nominal_occupied_bandwidth_hz": m["format"]["occupied_bandwidth_hz"],
        "source_bit_energy_samples": [ea, eb], "source_bit_energy_seconds": [ea / fs, eb / fs],
        "source_bit_difference_energy_samples": difference,
        "unknown_global_phase_minimum_difference_energy_samples": max(0., ea + eb - 2 * overlap),
        "squared_normalized_template_overlap": float(overlap ** 2 / (ea * eb)) if ea > 0 and eb > 0 else None,
        "scope": "Source-only finite templates at nominal epoch; global-phase comparison is not a multipath/channel-subspace or unknown-timing sensitivity bound.",
    }


def matched_components(selected_directory, data_directory, helper_directory,
                       noise_directory=None):
    """Require identical physical configuration and stochastic roots.

    Component energies are evaluator quantities, not receiver inputs. With
    only three captures, linearity is an explicit model assumption. A fourth
    DATA-off/Helper-off capture makes superposition independently checkable.
    """
    directories = [Path(selected_directory), Path(data_directory), Path(helper_directory)]
    if noise_directory is not None:
        directories.append(Path(noise_directory))
    controls = [read_capture(path) for path in directories]
    m, truth, selected, validity, _ = controls[0]
    expected_switches = [(True, True), (True, False), (False, True), (False, False)]
    for index, (folder, control) in enumerate(zip(directories, controls)):
        cm, ct, observed, valid, _ = control
        for field in ("schema", "case", "control", "sample_rate_hz", "public_epoch_seconds",
                      "key_seed", "format", "calendar", "channel", "power", "helper_plan"):
            if cm.get(field) != m.get(field):
                raise ValueError(f"matched controls differ in {field}: {folder}")
        if (cm.get("data_enabled"), cm.get("helper_enabled")) != expected_switches[index]:
            raise ValueError("expected composite, DATA-only, Helper-only, then optional both-off controls")
        if ct.get("random_seed_roots") != truth.get("random_seed_roots"):
            raise ValueError("matched controls have different stochastic roots")
        # Equal seeds do not establish equal realizations when explicit
        # arrival/oscillator overrides are allowed. These are evaluator checks
        # only; none of the descriptors enters the receiver measurement.
        for field in ("arrival_seconds", "tx", "rx"):
            if (field in ct or field in truth) and (field not in ct or field not in truth or
                                                   ct[field] != truth[field]):
                raise ValueError(f"matched controls differ in actual {field}: {folder}")
        if index <= 1:  # Disabled DATA may carry an immaterial source-bit label.
            for field in ("source_bit", "source_epoch_offset_seconds"):
                if (field in ct or field in truth) and (field not in ct or field not in truth or
                                                       ct[field] != truth[field]):
                    raise ValueError(f"DATA-bearing controls differ in {field}: {folder}")
        if len(observed) != len(selected) or not np.array_equal(valid, validity):
            raise ValueError("matched controls have different observation calendars or validity masks")
        if np.any(valid != 1):
            raise ValueError("this metrology requires complete captured I/Q; missing-input inference is unsupported")
    data_observed, helper_observed = controls[1][2], controls[2][2]
    data = np.asarray(selected) - np.asarray(helper_observed)
    helper = np.asarray(selected) - np.asarray(data_observed)
    noise = np.asarray(data_observed) + np.asarray(helper_observed) - np.asarray(selected)
    linearity = {"independently_checked": noise_directory is not None,
                 "actual_physical_descriptors_checked_when_present": ["arrival_seconds", "tx", "rx"],
                 "data_bearing_symbol_descriptors_checked_when_present": ["source_bit", "source_epoch_offset_seconds"],
                 "assumption": "Linear shared sampled channel, identical random streams and sample calendar."}
    if noise_directory is not None:
        residual = noise - np.asarray(controls[3][2])
        scale = max(float(np.max(np.abs(selected))), 1.)
        relative = float(np.max(np.abs(residual))) / scale
        linearity.update(maximum_absolute_residual=float(np.max(np.abs(residual))),
                         maximum_relative_residual=relative,
                         relative_tolerance=2e-11, passed=relative <= 2e-11)
        if not linearity["passed"]:
            raise ValueError("independent both-off control fails linear superposition check")
    identities = {}
    for name, folder, control in zip(("composite", "data_only", "helper_only", "both_off"),
                                     directories, controls):
        identities[name] = {
            "directory": str(folder), "observation_sha256": control[4],
            "manifest_sha256": _hash(folder / "manifest.json"),
            "truth_sha256": _hash(folder / "evaluator-truth.json"),
            "validity_sha256": _hash(folder / control[0]["validity"]["file"]),
        }
    return {"manifest": m, "truth": truth, "selected": np.asarray(selected),
            "data_only": np.asarray(data_observed), "helper_only": np.asarray(helper_observed),
            "data": data, "helper": helper, "noise": noise,
            "linearity": linearity, "identities": identities}


def frontend_filter(sample_rate, occupied_bandwidth_hz, profile="legacy",
                    pass_hz=None, stop_hz=None):
    """Use the actual DATA receiver FIR, or an explicitly named custom probe.

    A custom FIR is metrology only and does not qualify an existing DATA
    receiver at that bandwidth. No filter edge is inferred from observed I/Q.
    """
    fs, bandwidth = float(sample_rate), float(occupied_bandwidth_hz)
    if not math.isfinite(fs) or fs <= 0 or not math.isfinite(bandwidth) or bandwidth <= 0:
        raise ValueError("positive finite sample rate and DATA bandwidth required")
    if profile in ("legacy", "wide"):
        if fs != RAW_SAMPLE_RATE or pass_hz is not None or stop_hz is not None:
            raise ValueError("production DATA FIR profiles require 18 kHz and no custom edges")
        passed, stopped = (330., 430.) if profile == "legacy" else (400., 440.)
        if bandwidth / 2 >= passed:
            raise ValueError("DATA band exceeds the selected receiver FIR; use explicit custom metrology edges")
        taps, down = data_fir_taps(profile), DECIMATION
        implementation = f"triad_helper_receiver.data_fir_taps({profile})"
    elif profile == "custom":
        if pass_hz is None or stop_hz is None:
            raise ValueError("custom metrology FIR requires explicit pass and stop edges")
        passed, stopped = float(pass_hz), float(stop_hz)
        if not 0 < bandwidth / 2 < passed < stopped < fs / 2:
            raise ValueError("custom edges must contain nominal DATA and lie below Nyquist")
        count, beta = signal.kaiserord(96., (stopped - passed) / (fs / 2))
        count += 1 - count % 2
        if count > 131071:
            raise ValueError("custom metrology FIR exceeds bounded tap budget")
        taps = signal.firwin(count, (passed + stopped) / 2,
                            window=("kaiser", beta), fs=fs)
        down = max(1, int(fs // (2.2 * stopped)))
        implementation = "explicit 96 dB Kaiser metrology FIR; not an implemented DATA receiver"
    else:
        raise ValueError("frontend profile must be legacy, wide or custom")
    frequencies, response = signal.freqz(taps, worN=16384, fs=fs)
    pass_mask, stop_mask = frequencies <= passed, frequencies >= stopped
    return taps, down, {
        "profile": profile, "implementation": implementation,
        "is_existing_receiver_frontend": profile != "custom", "receiver_qualification_established": False,
        "sample_rate_hz": fs,
        "pass_edge_hz": passed, "stop_edge_hz": stopped, "decimation": down,
        "tap_count": len(taps), "taps_sha256": hashlib.sha256(taps.astype("<f8").tobytes()).hexdigest(),
        "measured_passband_minimum_gain": float(np.min(np.abs(response[pass_mask]))),
        "measured_stopband_maximum_gain": float(np.max(np.abs(response[stop_mask]))),
        "frequency_response_grid_points": len(frequencies),
        "tail_policy": "Complete causal convolution including the FIR tail, then fixed decimation.",
    }


def helper_to_data(data, helper, noise, *, taps, decimation, noise_variance):
    """Full-tail actual-filter energy diagnostic, including coherent terms."""
    filtered = [signal.upfirdn(taps, values, down=decimation)
                for values in (data, helper, noise)]
    d, h, n = filtered
    ed, eh, en = map(_energy, filtered)
    # Exact finite-input white-noise expectation: avoids charging steady-state
    # variance to filter rows whose convolution has only partial input support.
    expected_noise = float(noise_variance) * float(np.sum(signal.upfirdn(
        np.square(taps), np.ones(len(data)), down=decimation)))
    ratio = eh / en if en > 0 else None
    projection = abs(np.vdot(d, h)) ** 2 / ed if ed > 0 else None
    return {
        "output_samples_including_filter_tail": len(d),
        "isolated_data_output_energy": ed, "isolated_helper_output_energy": eh,
        "shared_noise_output_energy": en, "expected_white_noise_output_energy": expected_noise,
        "helper_to_measured_noise_energy_db": _db_ratio(eh, en),
        "helper_to_expected_noise_energy_db": _db_ratio(eh, expected_noise),
        "helper_to_data_output_energy_db": _db_ratio(eh, ed),
        "data_energy_retained_by_filter_fraction": ed * decimation / max(_energy(data), 1e-300),
        "helper_projection_onto_realized_data_energy": projection,
        "helper_projection_to_data_energy_db": _db_ratio(projection or 0., ed),
        "power_based_noise_equivalent_penalty_db": 10 * math.log10(1 + ratio) if ratio is not None else None,
        "coherent_noise_norm_envelope_db": 20 * math.log10(1 + math.sqrt(ratio)) if ratio is not None else None,
        "actual_helper_plus_noise_to_noise_energy_db": _db_ratio(_energy(h + n), en),
        "interpretation": "Power-equivalent noise penalty is a screening calculation, not a bound on receiver sensitivity. The coherent envelope is the deterministic L2 triangle bound for this complete filtered observation. Realized DATA projection is one known-waveform diagnostic, not a full unknown-channel/search likelihood.",
    }


def _peak(result):
    """Retain the actual estimator peak and expose sub-grid diagnostic only."""
    peak = result.get("peaks_hz", {}).get("joint")
    if peak is None:
        return None, "estimator_returned_no_peak"
    if result.get("joint_search"):
        return float(peak), "estimator_joint_local_refinement"
    grid = np.asarray(result.get("profile_grid_hz", []), dtype=float)
    scores = np.asarray(result.get("profile_scores", []), dtype=float)
    if len(grid) < 3 or scores.shape != grid.shape:
        return float(peak), "estimator_grid_peak_only"
    index = int(np.argmax(scores))
    if index == 0 or index == len(grid) - 1:
        return float(peak), "estimator_boundary_grid_peak_only"
    denominator = scores[index - 1] - 2 * scores[index] + scores[index + 1]
    if denominator >= 0 or not np.isfinite(denominator):
        return float(peak), "estimator_grid_peak_only"
    fraction = .5 * (scores[index - 1] - scores[index + 1]) / denominator
    fraction = float(np.clip(fraction, -.5, .5))
    return float(grid[index] + fraction * (grid[index + 1] - grid[index])), "three_bin_quadratic_diagnostic_not_receiver_narrowing"


def phase_probe(values, reference, *, sample_rate, first_sample_seconds,
                pilot_relative_hz, estimate_hz, reference_estimate_hz,
                reference_pilot_relative_hz=None,
                dwell_seconds=.4, aperture_seconds=.30,
                minimum_reference_fraction=1e-8):
    """Dated direct pilot projections; frequency bias remains in phase errors.

    All complete consecutive fixed apertures are measured, including those
    falling near source ramps. No truth timing, fade erasure or waveform gate
    is applied. Rows with a nearly zero clean reference are retained as
    unresolved rather than converted into misleading phase-error percentages.
    """
    fs = float(sample_rate)
    if not 0 < aperture_seconds <= dwell_seconds or fs <= 0:
        raise ValueError("positive phase aperture no longer than a dwell required")
    count, stride = int(round(aperture_seconds * fs)), int(round(dwell_seconds * fs))
    if count < 4 or stride < count or len(values) != len(reference):
        raise ValueError("phase probes require matched arrays and at least four samples")
    taper = signal.windows.hann(count, sym=False)
    local = np.arange(count, dtype=np.float64) / fs
    reference_pilot = (pilot_relative_hz if reference_pilot_relative_hz is None
                       else reference_pilot_relative_hz)
    rows = []
    centered_times = local - .5 * count / fs
    for first in range(0, len(values) - count + 1, stride):
        times = first_sample_seconds + first / fs + local
        measured = np.dot(taper * values[first:first + count],
                          np.exp(-2j * np.pi * (pilot_relative_hz + estimate_hz) * times))
        clean = np.dot(taper * reference[first:first + count],
                       np.exp(-2j * np.pi * (reference_pilot + reference_estimate_hz) * times))
        measured_centered = np.dot(taper * values[first:first + count],
            np.exp(-2j * np.pi * (pilot_relative_hz + estimate_hz) * centered_times))
        clean_centered = np.dot(taper * reference[first:first + count],
            np.exp(-2j * np.pi * (reference_pilot + reference_estimate_hz) * centered_times))
        rows.append({"center_seconds": first_sample_seconds + (first + .5 * count) / fs,
                     "reference_power": float(abs(clean) ** 2),
                     "measured_power": float(abs(measured) ** 2),
                     "phase_error_radians": float(np.angle(measured * np.conj(clean))),
                     "centered_aperture_phase_error_radians": float(np.angle(
                         measured_centered * np.conj(clean_centered)))})
    if not rows:
        raise ValueError("phase interval has no complete aperture")
    floor = max(row["reference_power"] for row in rows) * minimum_reference_fraction
    usable = [row for row in rows if row["reference_power"] > floor]
    errors = np.array([row["phase_error_radians"] for row in usable])
    centered_errors = np.array([row["centered_aperture_phase_error_radians"] for row in usable])
    for row in rows:
        row["reference_resolved"] = row["reference_power"] > floor
    return {
        "aperture_seconds": count / fs, "stride_seconds": stride / fs,
        "pilot_relative_hz": float(pilot_relative_hz), "cfo_hz": float(estimate_hz),
        "clean_reference_pilot_relative_hz": float(reference_pilot),
        "clean_reference_cfo_hz": float(reference_estimate_hz),
        "frequency_bias_removed": False, "phase_offset_fitted_away": False,
        "phase_reference_origin_seconds": 0.,
        "transported_reference_horizon_seconds": [row["center_seconds"] for row in rows],
        "apertures": len(rows), "resolved_apertures": len(usable),
        "unresolved_apertures": len(rows) - len(usable),
        "minimum_reference_fraction_of_maximum_power": minimum_reference_fraction,
        "rms_error_degrees": float(np.sqrt(np.mean(errors ** 2)) * 180 / np.pi) if len(errors) else None,
        "circular_bias_degrees": float(np.angle(np.mean(np.exp(1j * errors))) * 180 / np.pi) if len(errors) else None,
        "maximum_absolute_error_degrees": float(np.max(np.abs(errors)) * 180 / np.pi) if len(errors) else None,
        "centered_aperture_fresh_phase": {
            "reference_origin": "each dated aperture center; no phase offset or slope fitted away",
            "rms_error_degrees": float(np.sqrt(np.mean(centered_errors ** 2)) * 180 / np.pi) if len(centered_errors) else None,
            "circular_bias_degrees": float(np.angle(np.mean(np.exp(1j * centered_errors))) * 180 / np.pi) if len(centered_errors) else None,
            "maximum_absolute_error_degrees": float(np.max(np.abs(centered_errors)) * 180 / np.pi) if len(centered_errors) else None,
        },
        "rows": rows,
        "interpretation": "Top-level phase errors transport the estimated frequency reference to calendar zero and retain frequency-error times absolute horizon. The separate centered-aperture projection measures fresh local phase and retains only within-aperture frequency error. Low/high pilots and DATA may have different physical channel phases; neither metric qualifies transferring pilot phase to DATA.",
    }


def helper_measurement(components, *, start_seconds, observation_seconds=36.,
                       phase_aperture_seconds=.30):
    m = components["manifest"]
    fs, calendar, channel, plan = (float(m["sample_rate_hz"]), m["calendar"],
                                   m["channel"], m["helper_plan"])
    if not math.isfinite(start_seconds) or observation_seconds != 36:
        raise ValueError("explicit finite Helper start and exactly 36 s observation required")
    lead = float(calendar["capture_lead_seconds"])
    first = int(round((start_seconds + lead) * fs))
    count = int(round(observation_seconds * fs))
    end = first + count
    if first < 0 or end > len(components["data"]):
        raise ValueError("requested Helper window exceeds captured samples")
    data_window = components["data"][first:end]
    # Reject a lead-only aperture even when filter roundoff leaves tiny tails.
    overlap_energy = _energy(data_window)
    full_energy = _energy(components["data"])
    if overlap_energy <= max(1e-20, full_energy * 1e-10):
        raise ValueError("Helper window has no measurable DATA overlap; lead-only measurement is invalid here")
    prior = float(channel["deterministic_cfo_bound_hz"]) * float(channel.get("spread_margin", 1.))
    # This is only an estimator return bank, not a new exhaustive DATA search.
    bank = np.linspace(-prior, prior, 65) if prior > 0 else np.array([0.])
    margin = float(channel.get("spread_margin", 1.))
    pair_ppm = float(channel.get("sample_clock_pair_ppm",
                                channel.get("base_relative_pair_ppm_bound",
                                            channel.get("relative_pair_ppm_bound", .0001)))) * margin
    endpoint = float(channel.get("endpoint_uniform_half_bound_ppm", pair_ppm / (2 * margin)))
    rate_ppm = endpoint / (1 - endpoint * 1e-6) * margin
    arrival_low, arrival_high = map(float, calendar["arrival_prior_seconds"])
    latest_start = float(calendar.get("helper_start_seconds", 0)) + arrival_high + .4
    earliest_end = (float(calendar["helper_end_seconds"]) / (1 + pair_ppm * 1e-6)
                    + arrival_low - .02)
    if first / fs - lead < latest_start or end / fs - lead >= earliest_end:
        raise ValueError("public timing/clock bounds do not leave complete Helper support for this window")
    kwargs = dict(sample_rate=fs, data_if_hz=channel["nominal_conversion_if_hz"],
                  low_pilot_hz=plan["low_hz"], high_pilot_hz=plan["high_hz"],
                  original_frequencies=bank, prior_half_width_hz=prior,
                  doppler_hz=channel["spread_doppler_hz"],
                  slow_depth=channel["selective_slow_depth"], fast_depth=channel["selective_fast_depth"],
                  noise_variance=m["power"]["complex_noise_variance"],
                  first_sample_seconds=first / fs - lead,
                  observation_seconds=observation_seconds,
                  audio_error_bound_ppm=pair_ppm, channel_rate_error_bound_ppm=rate_ppm,
                  phase_diffusion_radians_per_sqrt_second=channel["relative_phase_diffusion_rad_sqrt_s"],
                  pilot_dwell_gating=bool(plan["pilot_dwell_gating"]),
                  physical_rf_retuning=bool(m["format"].get("physical_rf_retuning", False)))
    observations = {"composite": components["selected"][first:end],
                    "helper_only_noisy": components["helper_only"][first:end],
                    "helper_clean_reference": components["helper"][first:end]}
    estimates, peaks, methods = {}, {}, {}
    for name, values in observations.items():
        estimates[name] = estimate_frequency(values, **kwargs)
        peaks[name], methods[name] = _peak(estimates[name])
    result = {
        "first_sample": first, "sample_count": count,
        "start_seconds": first / fs - lead, "end_seconds": end / fs - lead,
        "data_energy_in_window": overlap_energy,
        "fraction_of_full_received_data_energy_in_window": overlap_energy / full_energy,
        "frequency_estimators": estimates, "diagnostic_cfo_hz": peaks,
        "diagnostic_cfo_method": methods,
        "estimator_return_bank_scope": "65 public CFO probes only for estimator subset diagnostics, not the actual DATA acquisition bank.",
        "reference_scope": "Clean isolated propagated Helper on the same finite aperture; not channel truth or a universal phase reference.",
    }
    if any(value is None for value in peaks.values()):
        result.update(status="unresolved_no_frequency_peak", phase={})
        return result
    checked_reasons = {"conditional_model_checks_passed", "no_bank_shrink"}
    comparison_valid = all(value.get("status") == "narrowed" or
                           value.get("reason") in checked_reasons for value in estimates.values())
    reference_cfo = peaks["helper_clean_reference"]
    baseline_error = peaks["helper_only_noisy"] - reference_cfo
    combined_error = peaks["composite"] - reference_cfo
    result.update(status=("measured_conditional_on_finite_line_model" if comparison_valid else
                          "diagnostic_only_estimator_checks_unresolved"),
                  frequency_metrics_passed_estimator_model_checks=comparison_valid,
                  composite_minus_helper_only_cfo_hz=peaks["composite"] - peaks["helper_only_noisy"],
                  helper_only_noise_reference_error_hz=baseline_error,
                  composite_noise_reference_error_hz=combined_error,
                  absolute_frequency_error_ratio=(abs(combined_error) / abs(baseline_error)
                                                  if abs(baseline_error) > 1e-9 else None),
                  frequency_ratio_denominator_floor_hz=1e-9)
    phases = {}
    for name in ("composite", "helper_only_noisy"):
        phases[name] = {}
        for pilot in ("low", "high"):
            relative = float(plan[pilot + "_hz"]) - float(channel["nominal_conversion_if_hz"])
            arm_clock = float(estimates[name].get("clock_estimate_ppm") or 0.)
            reference_clock = float(estimates["helper_clean_reference"].get("clock_estimate_ppm") or 0.)
            phases[name][pilot] = phase_probe(
                observations[name], observations["helper_clean_reference"],
                sample_rate=fs, first_sample_seconds=result["start_seconds"],
                pilot_relative_hz=relative * (1 + arm_clock * 1e-6), estimate_hz=peaks[name],
                reference_pilot_relative_hz=relative * (1 + reference_clock * 1e-6),
                reference_estimate_hz=reference_cfo,
                dwell_seconds=float(m["format"]["dwell_seconds"]),
                aperture_seconds=phase_aperture_seconds)
    result["phase"] = phases
    result["phase_rms_error_ratios"] = {}
    result["centered_aperture_phase_rms_error_ratios"] = {}
    for pilot in ("low", "high"):
        baseline = phases["helper_only_noisy"][pilot]["rms_error_degrees"]
        combined = phases["composite"][pilot]["rms_error_degrees"]
        result["phase_rms_error_ratios"][pilot] = (combined / baseline if baseline and baseline > 1e-6
                                                  and combined is not None else None)
        fresh_baseline = phases["helper_only_noisy"][pilot]["centered_aperture_fresh_phase"]["rms_error_degrees"]
        fresh_combined = phases["composite"][pilot]["centered_aperture_fresh_phase"]["rms_error_degrees"]
        result["centered_aperture_phase_rms_error_ratios"][pilot] = (
            fresh_combined / fresh_baseline if fresh_baseline and fresh_baseline > 1e-6
            and fresh_combined is not None else None)
    result["phase_ratio_denominator_floor_degrees"] = 1e-6
    return result


def describe_interference(selected_directory, data_directory, helper_directory, *,
                          helper_start_seconds, noise_directory=None,
                          frontend_profile="legacy", pass_hz=None, stop_hz=None,
                          phase_aperture_seconds=.30, template_directory=None):
    started = time.perf_counter()
    components = matched_components(selected_directory, data_directory, helper_directory, noise_directory)
    m, truth = components["manifest"], components["truth"]
    taps, down, frontend = frontend_filter(m["sample_rate_hz"], m["format"]["occupied_bandwidth_hz"],
                                           frontend_profile, pass_hz, stop_hz)
    channel = m["channel"]
    offsets = tone_offsets(channel["spread_doppler_hz"], channel["selective_slow_depth"],
                           channel["selective_fast_depth"])
    required_edge = (float(m["format"]["occupied_bandwidth_hz"]) / 2 +
                     float(channel["deterministic_cfo_bound_hz"]) * float(channel.get("spread_margin", 1.)) +
                     float(np.max(np.abs(offsets))))
    frontend["public_nominal_band_plus_cfo_and_line_offsets_edge_hz"] = required_edge
    frontend["design_band_is_not_compact_spectral_support"] = True
    if required_edge >= frontend["pass_edge_hz"]:
        raise ValueError("DATA channel/CFO design envelope exceeds frontend passband; choose wider explicit edges")
    result = {
        "schema": "triad-helper-bidirectional-interference-v1",
        "source_sha256": _hash(__file__),
        "dependency_sha256": {name: _hash(Path(__file__).with_name(name)) for name in
                              ("triad_helper_assistance.py", "triad_helper_receiver.py",
                               "triad_helper_ramp_diagnostic.py")},
        "input_identities": components["identities"],
        "linearity": components["linearity"],
        "sample_rate_hz": m["sample_rate_hz"], "format": m["format"],
        "helper_plan": m["helper_plan"], "power_reference": m["power"],
        "channel": m["channel"], "calendar": m["calendar"],
        "provisional_engineering_targets": TARGETS,
        "actual_component_energies": {"received_data_full_tail": _energy(components["data"]),
                                      "received_helper_full_tail": _energy(components["helper"]),
                                      "shared_noise_full_capture": _energy(components["noise"]),
                                      "emitted_source": truth.get("source_energy", {})},
        "data_frontend": frontend,
        "helper_to_data": helper_to_data(components["data"], components["helper"], components["noise"],
                                          taps=taps, decimation=down,
                                          noise_variance=m["power"]["complex_noise_variance"]),
        "data_to_helper": helper_measurement(components, start_seconds=helper_start_seconds,
                                             phase_aperture_seconds=phase_aperture_seconds),
        "limitations": [
            "Finite waveform/channel metrology, not a physical-radio measurement or population-confidence result.",
            "Source and received energy changes, filter retention, coherent interference and detector-model mismatch are different quantities.",
            "Pilot phase projections measure their own propagated channel reference; no cross-frequency phase transfer is established.",
            "The Helper finite-line estimator omits FHSS gate harmonics and arbitrary stochastic Doppler; preserve its fallback statuses.",
            "A tiny power-equivalent penalty does not alone prove a fixed-gate receiver sensitivity/admission margin.",
        ],
    }
    if template_directory is not None:
        result["public_template_metrics"] = template_energy_metrics(template_directory)
    result["wall_seconds"] = time.perf_counter() - started
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selected", required=True, type=Path)
    parser.add_argument("--data-only", required=True, type=Path)
    parser.add_argument("--helper-only", required=True, type=Path)
    parser.add_argument("--both-off", type=Path)
    parser.add_argument("--helper-start-seconds", required=True, type=float,
                        help="explicit nominal capture time; its 36 s window must overlap received DATA")
    parser.add_argument("--frontend-profile", choices=("legacy", "wide", "custom"), default="legacy")
    parser.add_argument("--pass-hz", type=float)
    parser.add_argument("--stop-hz", type=float)
    parser.add_argument("--phase-aperture-seconds", type=float, default=.30)
    parser.add_argument("--templates", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    result = describe_interference(args.selected, args.data_only, args.helper_only,
        noise_directory=args.both_off, helper_start_seconds=args.helper_start_seconds,
        frontend_profile=args.frontend_profile, pass_hz=args.pass_hz, stop_hz=args.stop_hz,
        phase_aperture_seconds=args.phase_aperture_seconds, template_directory=args.templates)
    with args.output.open("x") as stream:
        json.dump(result, stream, indent=2, allow_nan=False)
        stream.write("\n")


if __name__ == "__main__":
    main()
