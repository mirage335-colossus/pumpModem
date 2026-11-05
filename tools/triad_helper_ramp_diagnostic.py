#!/usr/bin/env python3
"""Measure DATA/Helper energy and spectral overlap in paired sampled exports.

The exporter's public I/Q contains the complete composite channel output and
noise. A matched Helper-only export uses identical seeds and public timing so
``selected - helper_only`` isolates the linearly propagated DATA waveform
(including its channel tails). The Helper-only observation still contains
noise; use its declared N0 and an empirical off-line Welch floor when quoting
band energy. This is a diagnostic, not a decoder or an admission test.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

import numpy as np
from scipy import signal

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))
from triad_helper_receiver import DATA_SAMPLE_RATE, DECIMATION, data_fir_taps


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def read_capture(directory: Path):
    manifest = json.loads((directory / "manifest.json").read_text())
    truth = json.loads((directory / "evaluator-truth.json").read_text())
    meta = manifest["observation"]
    shape = tuple(meta["shape"])
    if len(shape) != 1 or meta["dtype"] != "<c16":
        raise ValueError(f"unsupported capture array {directory}")
    path = directory / meta["file"]
    if path.stat().st_size != math.prod(shape) * 16:
        raise ValueError(f"observation file size does not match manifest: {path}")
    obs = np.memmap(path, mode="r", dtype="<c16", shape=shape)
    validity = np.memmap(directory / manifest["validity"]["file"], mode="r", dtype="u1",
                         shape=tuple(manifest["validity"]["shape"]))
    return manifest, truth, obs, validity, sha256(path)


def welch(values: np.ndarray, fs: float, nperseg: int = 32768):
    nperseg = min(nperseg, len(values))
    if nperseg < 16:
        raise ValueError("capture too short for spectrum diagnostic")
    return signal.welch(values, fs=fs, window="hann", nperseg=nperseg,
                        noverlap=nperseg // 2, detrend=False,
                        return_onesided=False, scaling="density")


def band_power(freq: np.ndarray, psd: np.ndarray, low: float, high: float) -> float:
    """Integrate a two-sided PSD over a half-open physical frequency interval."""
    if not low < high:
        raise ValueError("band edges must be increasing")
    order = np.argsort(freq)
    freq, psd = freq[order], psd[order]
    mask = (freq >= low) & (freq < high)
    if np.count_nonzero(mask) < 2:
        raise ValueError("band has fewer than two Welch bins")
    return float(np.trapezoid(psd[mask], freq[mask]))


def protected_dwell_geometry(fmt):
    rate = float(fmt["fine_chip_rate_hz"])
    dwell = float(fmt["dwell_seconds"])
    clearance = (float(fmt["quiet_seconds"]) + float(fmt["ramp_seconds"]) +
                 float(fmt["margin_seconds"]) + 8 / rate)
    first, last = math.ceil(clearance * rate), math.floor((dwell-clearance) * rate)
    return {"edge_clearance_seconds_including_full_pulse_support": clearance,
            "fine_center_indices_per_dwell": list(range(first, last+1)),
            "protected_centers_per_dwell": max(0, last-first+1),
            "payload_coefficients": fmt["fine_coefficients"],
            "inner_coefficients": fmt["inner_coefficients"],
            "actual_tile_seconds": fmt["bit_seconds"]}


def template_metrics(directory: Path):
    manifest = json.loads((directory / "manifest.json").read_text())
    meta = manifest["templates"]
    path = directory / meta["file"]
    shape = tuple(meta["shape"])
    if meta["dtype"] != "<c16" or len(shape) != 3 or shape[:2] != (13, 2):
        raise ValueError(f"unsupported template array: {path}")
    templates = np.memmap(path, mode="r", dtype="<c16", shape=shape)
    index = meta["epoch_offsets_seconds"].index(0)
    bit0, bit1 = templates[index]
    e0, e1 = float(np.vdot(bit0, bit0).real), float(np.vdot(bit1, bit1).real)
    delta = bit1 - bit0
    d2 = float(np.vdot(delta, delta).real)
    fs = float(manifest["sample_rate_hz"])
    freq = np.fft.fftfreq(len(bit0), d=1 / fs)
    density = np.abs(np.fft.fft(bit0)) ** 2 / (fs * len(bit0))
    df = fs / len(bit0)
    if_freq = float(manifest["channel"]["nominal_conversion_if_hz"])
    low = float(manifest["helper_plan"]["low_hz"] - if_freq)
    high = float(manifest["helper_plan"]["high_hz"] - if_freq)

    def source_band(low_hz, high_hz):
        mask = (freq >= low_hz) & (freq < high_hz)
        return float(np.sum(density[mask]) * df)

    source_spectrum = {
        "data_passband_energy": source_band(-112.5, 112.5),
        "helper_low_neighborhood_energy": source_band(low - 30, low + 30),
        "helper_high_neighborhood_energy": source_band(high - 30, high + 30),
        "full_finite_waveform_energy_per_sample": float(np.sum(density) * df),
        "frequency_bin_spacing_hz": df,
    }
    return {
        "template_sha256": sha256(path), "shape": list(shape),
        "source_bit0_energy_samples": e0, "source_bit1_energy_samples": e1,
        "source_bit_difference_energy_samples": d2,
        "source_bit_difference_energy_over_average_energy": d2 / ((e0 + e1) / 2),
        "symmetric_half_difference_energy_fraction": d2 / (2 * (e0 + e1)),
        "bit0_waveform_spectrum_at_actual_helper_frequencies": source_spectrum,
        "protected_dwell_geometry": protected_dwell_geometry(manifest["format"]),
    }


def compare_designs(old_directory: Path, new_directory: Path):
    old = json.loads((old_directory / "manifest.json").read_text())
    new = json.loads((new_directory / "manifest.json").read_text())
    stable_paths = (
        ("format", "inner_rate_hz"), ("format", "added_symbols"), ("format", "fine_chip_rate_hz"),
        ("format", "carrier_spacing_hz"), ("format", "occupied_bandwidth_hz"),
        ("format", "fine_coefficients"), ("format", "bit_seconds"), ("format", "dwell_seconds"),
        ("format", "quiet_seconds"), ("format", "margin_seconds"),
        ("channel", "rf_hz"), ("channel", "spread_preset"), ("channel", "spread_margin"),
        ("channel", "spread_delay_ms"), ("channel", "spread_doppler_hz"),
        ("channel", "selective_model"), ("channel", "selective_slow_depth"),
        ("channel", "selective_fast_depth"), ("power", "data_w"), ("power", "helper_w"),
        ("power", "path_loss_db"), ("encrypted_source",), ("encrypted_helper_metadata",),
    )

    def value(manifest, path):
        node = manifest
        for key in path:
            node = node[key]
        return node

    mismatches = {".".join(path): [value(old, path), value(new, path)] for path in stable_paths
                  if value(old, path) != value(new, path)}
    if mismatches:
        raise ValueError(f"old/new comparison changed fixed design fields: {mismatches}")
    return {
        "fixed_fields_match": True,
        "fixed_fields_checked": [".".join(path) for path in stable_paths],
        "intended_changes": {
            "data_if_hz": [old["channel"]["nominal_conversion_if_hz"], new["channel"]["nominal_conversion_if_hz"]],
            "data_ramp_seconds": [old["format"]["ramp_seconds"], new["format"]["ramp_seconds"]],
            "helper_high_hz": [old["helper_plan"]["high_hz"], new["helper_plan"]["high_hz"]],
        },
        "source_power_calibration": {
            "old": {k: old["power"].get(k) for k in
                    ("template_data_gain", "post_plan_composite_gain", "reference_measured_helper_to_data_ratio",
                     "data_w", "helper_w", "path_loss_db")},
            "new": {k: new["power"].get(k) for k in
                    ("template_data_gain", "post_plan_composite_gain", "reference_measured_helper_to_data_ratio",
                     "data_w", "helper_w", "path_loss_db")},
            "note": "The exporter recalibrates gain to its fixed public DATA power reference; these values expose that scaling for review.",
        },
        "control": [old["control"], new["control"]],
    }


def describe_triplet(selected_dir: Path, data_dir: Path, helper_dir: Path):
    sm, st, selected, sv, shash = read_capture(selected_dir)
    dm, dt, data_only, dv, dhash = read_capture(data_dir)
    hm, ht, helper_only, hv, hhash = read_capture(helper_dir)
    for key in ("sample_rate_hz", "public_epoch_seconds", "key_seed", "case"):
        if sm[key] != dm[key] or sm[key] != hm[key]:
            raise ValueError(f"matched captures differ in {key}")
    # The three exports may only differ in which component is switched on.
    # Require all public DSP/channel/power settings and geometry to match.
    for label, metadata in (("format", "format"), ("calendar", "calendar"),
                            ("channel", "channel"), ("power", "power"),
                            ("helper_plan", "helper_plan")):
        selected_config, data_config, helper_config = (dict(manifest[metadata]) for manifest in (sm, dm, hm))
        if metadata == "format":
            # API request duration is metadata; this fixed interleaved tile is
            # always 26.4 s and its actual waveform is independently compared.
            for config in (selected_config, data_config, helper_config):
                config.pop("requested_bit_seconds", None)
        if metadata == "calendar":
            # Zero-valued new exporter option; absent in pre-option captures.
            for config in (selected_config, data_config, helper_config):
                if config.get("receiver_tail_seconds") == 0:
                    config.pop("receiver_tail_seconds")
        if metadata == "channel":
            # New exporter adds explicit oscillator bound fields. The older
            # stored selected/data controls predate these additive descriptors.
            additive = {"base_relative_pair_ppm_bound", "rf_relative_pair_ppm_bound",
                        "rf_endpoint_uniform_half_bound_ppm", "sample_clock_pair_ppm",
                        "nominal_shift_ppm_bound", "nominal_shift_hz_bound"}
            for config in (selected_config, data_config, helper_config):
                for key in additive:
                    config.pop(key, None)
        if selected_config != data_config or selected_config != helper_config:
            raise ValueError(f"matched captures differ in public {label} configuration")
    for m, mask, truth in ((dm, dv, dt), (hm, hv, ht)):
        if sm["observation"]["shape"] != m["observation"]["shape"]:
            raise ValueError("matched observations have different sample calendars")
        if st["random_seed_roots"] != truth["random_seed_roots"]:
            raise ValueError("matched captures do not share the same stochastic roots")
        if not np.array_equal(sv, mask):
            raise ValueError("matched captures have different validity masks")
    if not sm["data_enabled"] or not sm["helper_enabled"] or not dm["data_enabled"] or dm["helper_enabled"] or hm["data_enabled"] or not hm["helper_enabled"]:
        raise ValueError("expected DATA+Helper, DATA-only and Helper-only captures")

    fs = float(sm["sample_rate_hz"])
    # Subtraction cancels shared noise. Two complementary single-component
    # exports recover the actual propagated DATA and Helper waveforms. Their
    # sum minus the composite should reconstruct the shared noise realization.
    data = np.asarray(selected) - np.asarray(helper_only)
    helper = np.asarray(selected) - np.asarray(data_only)
    residual_noise = np.asarray(data_only) + np.asarray(helper_only) - np.asarray(selected)
    f, psel = welch(np.asarray(selected), fs)
    _, pdata = welch(data, fs)
    _, phelper = welch(helper, fs)
    _, phelper_only = welch(np.asarray(helper_only), fs)
    bw = float(sm["format"]["occupied_bandwidth_hz"])
    half = bw / 2
    data_band = (-half, half)
    # The Data IF coordinate is zero after export's nominal derotation.
    # Report energy in the actual passband and pilot neighborhoods, not only
    # nominal band-center metadata. Pilot neighborhoods use ±30 Hz around each
    # emitted line; all skirts are also represented by the full Welch result.
    high = float(sm["helper_plan"]["high_hz"] - sm["channel"]["nominal_conversion_if_hz"])
    low = float(sm["helper_plan"]["low_hz"] - sm["channel"]["nominal_conversion_if_hz"])
    # Noise density in the exported complex baseband, as declared by exporter.
    n0 = float(sm["power"]["complex_noise_variance"]) / fs
    # Estimate sample noise floor from bins away from DATA and Helper lines.
    clean = ((np.abs(f) > half + 120) & (np.abs(f-low) > 100) &
             (np.abs(f-high) > 100) & (np.abs(f) < fs/2 - 100))
    noise_floor = float(np.median(psel[clean])) if np.any(clean) else math.nan

    def excess(power, width):
        return max(0.0, power - noise_floor * width)

    d_total = float(np.vdot(data, data).real)
    h_total = float(st["source_energy"]["helper_energy_samples"])
    d_declared = float(st["source_energy"]["data_energy_samples"])
    bins = {
        "data_passband_hz": list(data_band),
        "helper_low_neighborhood_hz": [low-30, low+30],
        "helper_high_neighborhood_hz": [high-30, high+30],
    }
    spectral = {}
    for label, band in bins.items():
        lowf, highf = band
        spectral[label] = {
            "selected_composite_welch_power": band_power(f, psel, lowf, highf),
            "data_component_welch_power": band_power(f, pdata, lowf, highf),
            "isolated_helper_component_welch_power": band_power(f, phelper, lowf, highf),
            "helper_only_welch_power_including_noise": band_power(f, phelper_only, lowf, highf),
            "estimated_noise_floor_density": noise_floor,
            "helper_only_estimated_excess_over_noise": excess(
                band_power(f, phelper_only, lowf, highf), highf-lowf),
        }

    taps = data_fir_taps("legacy")

    def filtered_energy(values):
        decimated = signal.upfirdn(taps, values, down=DECIMATION)
        return float(np.vdot(decimated, decimated).real), len(decimated)

    data_fir_energy, fir_rows = filtered_energy(data)
    helper_fir_energy, _ = filtered_energy(helper)
    noise_fir_energy, _ = filtered_energy(residual_noise)
    expected_noise_variance_out = float(sm["power"]["complex_noise_variance"]) * float(np.sum(taps ** 2))
    return {
        "selected_capture": str(selected_dir), "helper_only_capture": str(helper_dir),
        "observation_sha256": {"selected": shash, "helper_only": hhash},
        "case": sm["case"], "sample_rate_hz": fs,
        "waveform_geometry": {k: sm["format"][k] for k in
                              ("inner_rate_hz", "added_symbols", "fine_chip_rate_hz", "carrier_spacing_hz",
                               "occupied_bandwidth_hz", "fine_coefficients", "bit_seconds", "dwell_seconds",
                               "quiet_seconds", "ramp_seconds", "margin_seconds")},
        "rf_hz": sm["channel"]["rf_hz"], "data_if_hz": sm["channel"]["nominal_conversion_if_hz"],
        "helper_lines_relative_to_data_center_hz": {"low": low, "high": high},
        "full_capture_including_channel_tail_samples": len(selected),
        "capture_calendar": {k: sm["calendar"].get(k) for k in
                             ("capture_lead_seconds", "capture_trail_seconds", "fixed_capture_tail_bound_seconds",
                              "natural_complete_samples", "tail_samples", "observation_padding_samples")},
        "valid_sample_count": int(np.count_nonzero(sv)),
        "actual_energy_samples": {
            "selected_composite_observed_including_noise": float(np.vdot(selected, selected).real),
            "matched_subtraction_data_component_full_tail": d_total,
            "helper_component_full_tail_after_matched_subtraction": float(np.vdot(helper, helper).real),
            "helper_declared_emitted_source": h_total,
            "matched_residual_noise_energy_samples": float(np.vdot(residual_noise, residual_noise).real),
            "helper_only_export_total_observed_energy_including_noise": float(np.vdot(helper_only, helper_only).real),
            "data_only_export_total_observed_energy_including_noise": float(np.vdot(data_only, data_only).real),
            "data_declared_emitted_source": d_declared,
            "isolated_data_fraction_of_composite_observation_energy": d_total / max(float(np.vdot(selected, selected).real), 1e-300),
            "helper_to_data_source_energy_ratio": h_total / max(d_declared, 1e-300),
            "subtraction_vs_declared_data_source_ratio": d_total / max(d_declared, 1e-300),
        },
        "noise_density_declared_per_hz": n0,
        "receiver_fir_leakage": {
            "filter": "triad_helper_receiver.data_fir_taps(legacy), continuous 18 kHz input then 900 Hz decimation",
            "tap_count": int(len(taps)), "output_samples": fir_rows,
            "isolated_data_output_energy": data_fir_energy,
            "isolated_helper_output_energy": helper_fir_energy,
            "matched_noise_output_energy": noise_fir_energy,
            "expected_white_noise_output_variance": expected_noise_variance_out,
            "isolated_helper_to_measured_noise_energy_db": 10 * math.log10(max(helper_fir_energy, 1e-300) /
                                                                              max(noise_fir_energy, 1e-300)),
            "isolated_helper_to_expected_noise_energy_db": 10 * math.log10(max(helper_fir_energy, 1e-300) /
                                                                               max(expected_noise_variance_out * fir_rows, 1e-300)),
        },
        "spectral_bands": spectral,
        "limits": ["Matched subtraction is valid only because the sampled channel is linear and stochastic roots are identical; that identity is asserted.",
                   "Welch spectra describe this finite noisy realization; integrated helper excess uses an off-line median floor and is diagnostic, not a confidence bound.",
                   "Component power in the production legacy DATA FIR is measured on isolated sampled waveforms; it is specific to this FIR and fixture."],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selected", type=Path, required=True, help="DATA+Helper export directory")
    parser.add_argument("--data-only", type=Path, required=True, help="matched DATA-only export directory")
    parser.add_argument("--helper-only", type=Path, required=True, help="matched Helper-only export directory")
    parser.add_argument("--old-templates", type=Path, help="optional old-format public template export")
    parser.add_argument("--new-templates", type=Path, help="optional new-format public template export")
    parser.add_argument("--reference-case", type=Path, help="old design export; checks fixed fields and intended changes")
    parser.add_argument("--output", type=Path, required=True, help="new JSON result path")
    args = parser.parse_args(argv)
    if args.output.exists():
        parser.error("output file already exists")
    report = describe_triplet(args.selected, args.data_only, args.helper_only)
    if args.reference_case:
        report["design_comparison"] = compare_designs(args.reference_case, args.selected)
    if bool(args.old_templates) != bool(args.new_templates):
        parser.error("--old-templates and --new-templates must be supplied together")
    if args.old_templates:
        report["source_template_comparison"] = {
            "old": template_metrics(args.old_templates), "new": template_metrics(args.new_templates),
            "scope": "Public shaped encrypted source templates at the zero epoch; compares DATA energy and keyed bit-distance before channel/noise.",
        }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(json.dumps(report, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
