#!/usr/bin/env python3
"""Bounded CLI smoke contracts for actual Helper + triad sampled exports.

Run with the built exporter as the sole positional argument. This produces
three minimal captures, checks their file/metadata contracts, and removes the
temporary arrays. It does not measure receiver success or statistical power.
"""

import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import time


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def close(actual, expected, name):
    require(math.isclose(actual, expected, rel_tol=1e-12, abs_tol=1e-12),
            f"{name}: {actual!r} != {expected!r}")


def all_keys(value):
    if isinstance(value, dict):
        for key, child in value.items():
            yield key
            yield from all_keys(child)
    elif isinstance(value, list):
        for child in value:
            yield from all_keys(child)


def load_json(path):
    def unique_fields(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, f"duplicate JSON field {key!r} in {path}")
            result[key] = value
        return result

    return json.loads(path.read_text(), object_pairs_hook=unique_fields)


def invoke(binary, output, arguments, timings, *, succeeds=True):
    command = [str(binary), "--output", str(output), *arguments]
    started = time.perf_counter()
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    elapsed = time.perf_counter() - started
    timings.append({"arguments": arguments, "wall_seconds": elapsed,
                    "returncode": result.returncode})
    require((result.returncode == 0) == succeeds,
            f"unexpected exporter status {result.returncode}: {command}\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}")
    return result


def check_capture(directory, scenario, *, data=True, helper=True, bit=0):
    manifest = load_json(directory / "manifest.json")
    truth = load_json(directory / "evaluator-truth.json")
    require(manifest["schema"] == "triad-helper-public-v1", "public schema")
    require(truth["schema"] == "triad-helper-evaluator-truth-v1", "truth schema")
    require(manifest["case"] == scenario and manifest["control"] == "selected", "case/control")
    require(manifest["data_enabled"] is data and manifest["helper_enabled"] is helper, "component controls")
    require(manifest["encrypted_source"] and manifest["encrypted_helper_metadata"], "encryption defaults")
    require(manifest["key_seed"] == 72001 and manifest["public_epoch_seconds"] == 1793850000,
            "fixed public fixture key and epoch")
    require(manifest["evaluator_truth_is_receiver_input"] is False and truth["receiver_input"] is False,
            "evaluator truth must remain a separate non-receiver artifact")
    public_keys = set(all_keys(manifest))
    require(not public_keys.intersection({"source_bit", "arrival_seconds", "tx", "rx", "audio_ppm", "rf_ppm",
                                         "random_seed_roots", "static_endpoint", "unmodified_arrival_draw_seconds"}),
            "sampled bit, arrival, endpoint or seed truth leaked into public metadata")
    require({key for key in public_keys if key.endswith("seed")} == {"key_seed"},
            "public key seed must remain independent of physical random roots")
    require(truth["source_bit"] == bit and truth["source_epoch_offset_seconds"] == 0, "evaluator source bit/epoch")
    seeds = truth["random_seed_roots"]
    roots = [seeds[name] for name in ("channel", "static_endpoint", "arrival", "oscillator", "noise", "frontend", "path")]
    require(len(set(roots)) == len(roots), "physical random streams need separate roots")

    fs = manifest["sample_rate_hz"]
    close(fs, 18000, "sample rate")
    calendar = manifest["calendar"]
    close(calendar["data_calendar_start_seconds"], 38, "fixed DATA/Helper lead")
    close(calendar["helper_start_seconds"], 0, "Helper start")
    require(calendar["data_sample_offset"] + calendar["data_calendar_origin_sample"] == 38 * fs,
            "padded DATA calendar must start at 38 s")
    require(calendar["composite_calendar_origin_sample"] == 0, "composite calendar origin")
    require(calendar["composite_source_samples"] >= calendar["data_sample_offset"] + calendar["data_source_samples"],
            "Helper endpoint truncates DATA support")
    require(calendar["composite_source_samples"] % (fs * 2 // 5) == 0, "Helper endpoint must align to a full dwell")
    close(calendar["helper_end_seconds"] * fs, calendar["composite_source_samples"], "Helper endpoint")

    fmt, channel, power, plan = (manifest[name] for name in ("format", "channel", "power", "helper_plan"))
    eme = scenario == "eme"
    expected = dict(data_w=40 if eme else 4, helper_w=15000 if eme else 40,
                    path_loss_db=200 if eme else 180)
    for name, value in expected.items():
        close(power[name], value, name)
    close(power["composite_w"], expected["data_w"] + expected["helper_w"], "composite power")
    close(power["noise_n0_dbm_hz"], -164, "physical thermal density")
    physical_cn0 = 10 * math.log10(expected["data_w"] * 1000) - expected["path_loss_db"] + 164
    close(power["physical_data_cn0_db_hz"], physical_cn0, "physical C/N0")
    close(power["data_cn0_db_hz"], physical_cn0, "default C/N0")
    require(power["cn0_override"] is False, "physical C/N0 must be default")
    close(power["planning_target_db_metadata_only"], 1.5 if eme else 8, "planning target remains metadata")
    close(power["complex_noise_variance"], fs / 10 ** (physical_cn0 / 10), "sample noise variance")
    require(power["reference_bit"] == 0 and power["reference_window_seconds"] == [39, 41], "public active calibration")
    close(power["reference_data_mean_real_power"] * 2, power["reference_data_mean_norm"], "analytic/real power convention")
    close(power["template_data_gain"] ** 2 * power["reference_data_mean_norm"], 1, "frozen nominal DATA power")
    close(plan["data_gain"] * power["post_plan_composite_gain"], power["template_data_gain"], "post-plan DATA gain")
    close(power["nominal_helper_to_data_ratio"], expected["helper_w"] / expected["data_w"], "nominal Helper ratio")
    require(power["reference_measured_helper_to_data_ratio"] > 0, "actual Helper reference power")

    close(fmt["inner_rate_hz"], 360 if eme else 36, "inner rate")
    require(fmt["added_symbols"] == (1 if eme else 10) and fmt["added_interleave"], "Added/interleave")
    close(fmt["requested_bit_seconds"], 1, "minimal requested duration")
    require(fmt["bit_seconds"] >= 1, "complete minimum source tile")
    close(fmt["occupied_bandwidth_hz"], 225, "DATA design band")
    close(fmt["carrier_spacing_hz"], 75, "DATA spacing")
    require(fmt["fhss_envelope_only"] is eme and fmt["fhss_center_aligned"] is eme, "case FHSS/center alignment")
    require(fmt["physical_rf_retuning"] is False, "fixture must identify envelope-only FHSS")
    close(fmt["quiet_seconds"], .055 if eme else 0, "quiet interval")
    close(fmt["ramp_seconds"], 2 / 3600 if eme else .02, "ramp")
    close(fmt["margin_seconds"], .005, "source margin")
    close(channel["nominal_conversion_if_hz"], 4000 if eme else 1725, "default DATA IF")
    require(channel["nominal_conversion_if_hz"] >= 1725, "DATA IF floor")
    close(channel["rf_hz"], 5.8e9 if eme else 30e6, "RF center")
    oscillator = "gpsdo-ocxo" if eme else "gpsdo-xo"
    require(channel["base_oscillator"] == oscillator and channel["shift_oscillator"] == oscillator,
            "canonical oscillator selections")
    close(channel["relative_pair_ppm_bound"], .0001, "relative pair oscillator bound")
    close(channel["endpoint_uniform_half_bound_ppm"], .00005, "independent endpoint half bound")
    require(channel["independent_base_and_shift"] is True, "base/shift independence")
    require(channel["spread_preset"] == ("eme-focused" if eme else "hf-worst"), "spread preset")
    close(channel["spread_margin"], 1 if eme else 3, "spread margin")
    close(channel["spread_delay_ms"], .5 if eme else 21, "effective spread delay")
    close(channel["spread_doppler_hz"], 6 if eme else 90, "effective spread Doppler")
    close(channel["selective_slow_depth"], .65, "slow Each65 depth")
    close(channel["selective_fast_depth"], .65, "fast Each65 depth")
    require(channel["selective_model"] == "dispersive-dft-v1", "selected dispersive channel")
    close(channel["selective_scale_hz"], 75, "public selective scale")
    close(channel["selective_center_hz"], 0, "public selective center")
    close(plan["placement_gate_bandwidth_hz"], 3600 if eme else 100, "complete gate allowance")
    close(plan["low_hz"], 550, "production low pilot")
    close(plan["auxiliary_hz"], 950, "production metadata carrier")
    require(plan["pilot_dwell_gating"] is eme, "production Helper dwell gating")
    upper_data = channel["nominal_conversion_if_hz"] + (225 + plan["placement_gate_bandwidth_hz"]) / 2
    require(plan["high_hz"] >= max(2400, upper_data + 450) and plan["high_hz"] <= fs / 2 - 250,
            "production high-pilot geometry")
    expected_prior = [-.055, .125] if eme else [-7.035, 7.035]
    require(calendar["arrival_prior_seconds"] == expected_prior, "public arrival study prior")
    require(expected_prior[0] <= truth["arrival_seconds"] <= expected_prior[1], "independent physical arrival")
    close(calendar["capture_lead_seconds"], .2 if eme else 8, "capture lead")
    close(calendar["capture_trail_seconds"], .3 if eme else 8, "capture trail")

    samples = calendar["capture_samples"]
    require(manifest["observation"]["shape"] == [samples] and manifest["validity"]["shape"] == [samples], "file shapes")
    require(manifest["observation"]["dtype"] == "<c16" and manifest["validity"]["dtype"] == "u1", "binary types")
    require((directory / "observation.c128").stat().st_size == samples * 16, "complete observation file")
    require((directory / "validity.u8").stat().st_size == samples, "complete validity file")
    require(set((directory / "validity.u8").read_bytes()) == {1}, "no missing input was configured")
    with (directory / "observation.c128").open("rb") as stream:
        for index in (0, samples // 2, samples - 1):
            stream.seek(index * 16)
            require(all(math.isfinite(value) for value in struct.unpack("<dd", stream.read(16))), "finite sampled output")
    require(manifest["templates"]["enabled"] is False and not (directory / "templates.c128").exists(), "bounded no-template smoke")
    require(manifest["templates"]["shape"][:2] == [13, 2], "complete public epoch/bit bank declaration")
    require(manifest["templates"]["epoch_offsets_seconds"] == list(range(-6, 7)), "thirteen public epochs")
    report = truth["channel_report"]
    require(report["samples"] == samples and report["source_samples"] == calendar["composite_source_samples"], "complete channel rows")
    require(report["tail_samples"] > 0 and report["natural_complete_samples"] <= samples, "full propagation tails")
    require(report["erased_samples"] == 0 and report["out_of_passband_samples"] == 0, "capture diagnostics")
    require(report["selective_perturbation"] is not None, "selected modulation actually instantiated")
    for name, expected_rate in zip((0, 1), (channel["spread_doppler_hz"] / 10, channel["spread_doppler_hz"])):
        close(report["selective_perturbation"]["component_rates_hz"][name], expected_rate, "actual AM rate")
    energy = truth["source_energy"]
    require(energy["rows"] == calendar["composite_source_samples"], "full source energy accounting")
    require((energy["data_energy_samples"] > 0) is data, "actual emitted DATA control")
    require((energy["helper_energy_samples"] > 0) is helper, "actual emitted Helper control")
    if not data and not helper:
        close(energy["composite_energy_samples"], 0, "both-off emitted signal")
        close(report["signal_energy_samples"], 0, "both-off propagated signal")
    return manifest, truth


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exporter", type=Path)
    args = parser.parse_args()
    binary = args.exporter.resolve()
    require(binary.is_file(), f"exporter does not exist: {binary}")
    timings = []
    started = time.perf_counter()
    with tempfile.TemporaryDirectory(prefix="triad-helper-export-smoke-") as temporary:
        root = Path(temporary)
        invalid = root / "invalid-if"
        invoke(binary, invalid, ["--case", "hf", "--data-if", "500"], timings, succeeds=False)
        require(not invalid.exists(), "invalid IF created output")
        invalid_geometry = root / "invalid-eme-geometry"
        result = invoke(binary, invalid_geometry, ["--case", "eme", "--data-if", "1725", "--seconds", "1"],
                        timings, succeeds=False)
        require("production Helper plan rejected" in result.stderr, "invalid EME geometry must fail in production placement")
        require(not invalid_geometry.exists(), "invalid Helper placement wrote output")

        invoke(binary, root / "hf", ["--case", "hf", "--seconds", "1", "--seed", "91001"], timings)
        hf, hf_truth = check_capture(root / "hf", "hf")
        invoke(binary, root / "eme", ["--case", "eme", "--seconds", "1", "--seed", "91002"], timings)
        check_capture(root / "eme", "eme")
        invoke(binary, root / "both-off", ["--case", "hf", "--seconds", "1", "--seed", "91003",
                                          "--bit", "1", "--data", "off", "--helper", "off"], timings)
        off, off_truth = check_capture(root / "both-off", "hf", data=False, helper=False, bit=1)
        require(off["power"] == hf["power"] and off["helper_plan"] == hf["helper_plan"],
                "off controls, changed channel seed or transmitted bit changed public calibration")
        require(off["calendar"] == hf["calendar"] and off["templates"] == hf["templates"],
                "controls or hidden channel realization changed the public observation/template calendar")
        require(off_truth["random_seed_roots"] != hf_truth["random_seed_roots"], "distinct fixture seeds were ignored")
        require(hf_truth["random_seed_roots"]["fixture"] == 91001 and off_truth["random_seed_roots"]["fixture"] == 91003,
                "fixture seed provenance")
    print(json.dumps({"result": "passed", "captures": 3, "rejected_invocations": 2,
                      "wall_seconds": time.perf_counter() - started, "commands": timings,
                      "scope": "CLI/export metadata, actual emitted controls and complete files; no receiver or statistical success claim"}))


if __name__ == "__main__":
    main()
