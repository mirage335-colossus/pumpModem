#!/usr/bin/env python3
"""Bounded CLI smoke contracts for actual Helper + triad sampled exports.

Run with the built exporter as the sole positional argument. This produces
eight minimal captures, checks their file/metadata contracts, and removes the
temporary arrays. It does not measure receiver success or statistical power.
"""

import argparse
import json
import math
from pathlib import Path
import shutil
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
    command = [str(binary)]
    if output is not None:
        command.extend(["--output", str(output)])
    command.extend(arguments)
    started = time.perf_counter()
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    elapsed = time.perf_counter() - started
    timings.append({"arguments": arguments, "wall_seconds": elapsed,
                    "returncode": result.returncode})
    require((result.returncode == 0) == succeeds,
            f"unexpected exporter status {result.returncode}: {command}\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}")
    return result


def check_capture(directory, scenario, *, data=True, helper=True, bit=0,
                  data_if=None, base_oscillator=None, shift_oscillator=None, ramp_seconds=None,
                  receiver_tail_seconds=0, shaping="auto"):
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
    close(calendar["receiver_tail_seconds"], receiver_tail_seconds, "additional public receiver tail")

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
    require(power["data_power_reference_policy"] == "whole-active" and
            power["data_power_reference_version"] == 1, "general-rate power calibration default")
    close(power["template_data_gain"] ** 2 * power["data_reference_mean_norm"], 1, "frozen nominal DATA power")
    close(plan["data_gain"] * power["post_plan_composite_gain"] * power["post_plan_data_calibration_gain"],
          power["template_data_gain"], "post-plan DATA-only gain")
    close(power["helper_preview_normalized_data_mean_norm"],
          power["template_data_gain"] ** 2 * power["reference_data_mean_norm"], "actual Helper-preview DATA power")
    require(power["data_reference_refinement_relative_difference"] <= .001 and
            65536 <= power["data_reference_nodes"] <= 262144, "bounded source-power refinement")
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
    expected_ramp = ((119 / fs if eme else .02) if shaping == "auto" else
                     (ramp_seconds if ramp_seconds is not None else (2 / 3600 if eme else .02)))
    close(fmt["ramp_seconds"], expected_ramp, "ramp")
    require(manifest["shaping_plan"]["enabled"] is (shaping == "auto"), "selected shaping mode")
    if shaping == "auto":
        require(manifest["shaping_plan"]["converged"] and not manifest["shaping_plan"]["fallback"],
                "default presets must use the qualified spectral plan")
        require(fmt["pulse_shape"] == "rrc-025-span8-c2-tail2", "default planned pulse")
    close(fmt["margin_seconds"], .005, "source margin")
    expected_if = data_if if data_if is not None else (4000 if eme and shaping == "legacy" else 1725)
    close(channel["nominal_conversion_if_hz"], expected_if, "DATA IF")
    require(channel["nominal_conversion_if_hz"] >= 1725, "DATA IF floor")
    close(channel["rf_hz"], 5.8e9 if eme else 30e6, "RF center")
    oscillator = "gpsdo-ocxo" if eme else "gpsdo-xo"
    expected_base = base_oscillator or oscillator
    expected_shift = shift_oscillator or expected_base
    require(channel["base_oscillator"] == expected_base and channel["shift_oscillator"] == expected_shift,
            "canonical oscillator selections")
    ppm = {"gpsdo-xo": .0001, "gpsdo-ocxo": .0001, "crystal": 100, "ic-7100": 1}
    base_ppm, shift_ppm = ppm[expected_base], ppm[expected_shift]
    close(channel["relative_pair_ppm_bound"], base_ppm, "legacy/base relative pair oscillator bound")
    close(channel["base_relative_pair_ppm_bound"], base_ppm, "base relative pair oscillator bound")
    close(channel["rf_relative_pair_ppm_bound"], shift_ppm, "RF relative pair oscillator bound")
    close(channel["endpoint_uniform_half_bound_ppm"], base_ppm / 2, "independent audio endpoint half bound")
    close(channel["rf_endpoint_uniform_half_bound_ppm"], shift_ppm / 2, "independent RF endpoint half bound")
    close(channel["sample_clock_pair_ppm"], base_ppm / (1 - base_ppm * .5e-6), "corrected sample clock pair")
    close(channel["nominal_shift_ppm_bound"], shift_ppm, "nominal RF shift bound")
    close(channel["deterministic_cfo_bound_hz"],
          (channel["rf_hz"] * shift_ppm + expected_if * base_ppm) * 1e-6 /
          (1 - base_ppm * .5e-6), "deterministic CFO bound")
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
    gate_width = (max(0, manifest["shaping_plan"]["placement_bandwidth_hz"] - 225)
                  if shaping == "auto" else 2 / expected_ramp)
    close(plan["placement_gate_bandwidth_hz"], gate_width, "selected placement allowance")
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
    if data and bit == 0:
        require(abs(energy["data_active_mean_norm"] - 1) <= .003,
                f"complete sampled bit0 source disagrees with nominal active power: {energy['data_active_mean_norm']}")
    if not data and not helper:
        close(energy["composite_energy_samples"], 0, "both-off emitted signal")
        close(report["signal_energy_samples"], 0, "both-off propagated signal")
    return manifest, truth


def power_qualification(binary, root, timings):
    """Occasional source-power checks, not a receiver Monte Carlo campaign.

    The emitted-energy sum traverses every integer source sample and is
    independent of the stratified fractional-node calibration. Arrays are
    removed after each check; no template bank or impaired receiver is run.
    """
    cases = [
        ("rate3p6", ["--case", "hf", "--rate", "3.6", "--added-symbols", "1"]),
        ("rate10", ["--case", "hf", "--rate", "10", "--added-symbols", "1"]),
        ("hf36", ["--case", "hf"]),
        ("eme360", ["--case", "eme"]),
        ("eme-burst", ["--case", "eme", "--seconds", "30", "--burst-wait-tiles", "3"]),
        ("rate3p6-bit1", ["--case", "hf", "--rate", "3.6", "--added-symbols", "1", "--bit", "1"]),
        ("rate10-key2", ["--case", "hf", "--rate", "10", "--added-symbols", "1", "--key-seed", "72002"]),
    ]
    results, references = [], {}
    for name, overrides in cases:
        directory = root / name
        invoke(binary, directory, ["--seconds", "1", "--control", "flat", "--helper", "off",
                                  "--cn0", "200", "--seed", "91011"] + overrides, timings)
        manifest = load_json(directory / "manifest.json")
        truth = load_json(directory / "evaluator-truth.json")
        power, fmt = manifest["power"], manifest["format"]
        expected_active = fmt["tile_count"] * fmt["tile_seconds"]
        if fmt["fhss_envelope_only"]:
            expected_active *= 1 - 2 * fmt["quiet_seconds"] / fmt["dwell_seconds"]
        close(power["data_reference_active_seconds"], expected_active, "active denominator excludes waits and quiet")
        energy = truth["source_energy"]["data_energy_samples"]
        sampled_mean = energy / (18000 * expected_active)
        close(sampled_mean, truth["source_energy"]["data_active_mean_norm"], "independent complete source accounting")
        if truth["source_bit"] == 0:
            require(abs(sampled_mean - 1) <= .003,
                    f"{name}: sampled active power {sampled_mean} violates whole-source calibration")
        if name == "rate3p6-bit1":
            require(power == references["rate3p6"], "source bit altered fixed bit0 calibration")
        references[name] = power
        results.append({"case": name, "source_bit": truth["source_bit"], "key_seed": manifest["key_seed"],
                        "active_seconds": expected_active, "elapsed_seconds": fmt["bit_seconds"],
                        "sampled_active_mean_norm": sampled_mean,
                        "sampled_calendar_mean_norm": truth["source_energy"]["data_calendar_mean_norm"],
                        "refinement_relative_difference": power["data_reference_refinement_relative_difference"],
                        "data_calibration_db": 20 * math.log10(power["post_plan_data_calibration_gain"]),
                        "source_samples": manifest["calendar"]["data_source_samples"]})
        shutil.rmtree(directory)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exporter", type=Path)
    parser.add_argument("--power-qualification", action="store_true",
                        help="occasional complete sampled source-power validation only; no receiver")
    args = parser.parse_args()
    binary = args.exporter.resolve()
    require(binary.is_file(), f"exporter does not exist: {binary}")
    timings = []
    started = time.perf_counter()
    with tempfile.TemporaryDirectory(prefix="triad-helper-export-smoke-") as temporary:
        root = Path(temporary)
        if args.power_qualification:
            results = power_qualification(binary, root, timings)
            print(json.dumps({"result": "passed", "power_qualification": results,
                              "wall_seconds": time.perf_counter() - started, "commands": timings}))
            return

        power_plans = []
        for policy in ("whole-active", "legacy-window"):
            result = invoke(binary, None, ["--case", "hf", "--rate", "3.6", "--added-symbols", "1",
                                          "--power-plan-only", "--power-reference", policy], timings)
            plan = json.loads(result.stdout)
            require(plan["schema"] == "triad-helper-power-plan-v1", "power-plan schema")
            require(plan["power"]["data_power_reference_policy"] == policy, "power policy selection")
            close(plan["power"]["template_data_gain"] ** 2 * plan["power"]["data_reference_mean_norm"],
                  1, "power-plan normalization")
            power_plans.append(plan)
        require(power_plans[0]["power"]["post_plan_data_calibration_gain"] < .1,
                "low-rate birth-ramp calibration defect was not corrected")
        close(power_plans[1]["power"]["post_plan_data_calibration_gain"], 1, "legacy reproduction correction")
        close(power_plans[0]["power"]["post_plan_composite_gain"],
              power_plans[1]["power"]["post_plan_composite_gain"], "preserved Helper composite gain")
        # Planning is deliberately available before source/capture allocation.
        # These cases cross baseline and bounded-layout paths without writing
        # multi-hour sampled fixtures.
        plan_cases = [
            ["--case", "eme", "--plan-only"],
            ["--case", "hf", "--plan-only", "--rate", "36",
             "--added-symbols", "10", "--fhss", "off"],
            ["--case", "eme", "--plan-only", "--shaping", "auto", "--rate", "360",
             "--added-symbols", "1", "--fhss", "on", "--burst-wait-tiles", "3"],
            ["--case", "eme", "--plan-only", "--shaping", "auto", "--data-if", "1775"],
            ["--case", "hf", "--plan-only", "--shaping", "auto", "--rate", "1000",
             "--added-symbols", "1", "--fhss", "on"],
        ]
        planned = []
        for index, command_args in enumerate(plan_cases):
            result = invoke(binary, None, command_args, timings)
            plan = json.loads(result.stdout)
            require(plan["schema"] == "triad-helper-shaping-plan-v1", "planner-only JSON schema")
            require("geometry_feasible" in plan, "planner-only result must classify geometry")
            require(not (root / f"plan-{index}").exists(), "planner-only invocation created an output directory")
            if plan["geometry_feasible"]:
                require(plan["format"]["inner_rate_hz"] > 0 and plan["format"]["added_symbols"] > 0,
                        "planner-only format controls")
                require(plan["spectral_certificate"]["placement_bandwidth_hz"] >=
                        plan["format"]["occupied_bandwidth_hz"], "placement width below nominal DATA width")
                require("phase_refinement_error_hz" in plan["spectral_certificate"] and
                        "sampling_refinement_error_hz" in plan["spectral_certificate"],
                        "spectral convergence diagnostics omitted")
                if "--burst-wait-tiles" in command_args:
                    require(plan["format"]["burst_wait_tiles"] == 3, "burst wait override ignored")
            else:
                require(plan["reason"], "infeasible geometry needs a reason")
            planned.append(plan)
        require(planned[0]["shaping"] == planned[1]["shaping"] == "auto",
                "omitted shaping mode must select the planner for both presets")
        require(planned[0]["geometry_feasible"], "standard EME auto shaping should have a bounded plan")
        require(planned[0]["helper_placement"]["requested_data_if_hz"] == 1725,
                "auto EME DATA IF did not use standardized low placement")
        require(planned[1]["geometry_feasible"] and not planned[1]["format"]["fhss"],
                "rate-aware non-FHSS plan was not preserved")
        require(planned[2]["geometry_feasible"] and planned[2]["format"]["burst_wait_tiles"] == 3,
                "FHSS burst plan was not preserved")
        require(planned[3]["helper_placement"]["requested_data_if_hz"] == 1775,
                "explicit standardized DATA IF override was ignored")
        require(planned[4]["scenario"] == "hf",
                "noncommensurate FHSS planning did not retain its request identity")

        # Resolve physics without synthesizing extra observations. These cases
        # exercise independent rate, clock topology, timing and power controls.
        physics_cases = [
            ["--rf-hz", "0", "--base-oscillator", "crystal", "--shift-oscillator", "cb-ssb",
             "--spread-preset", "none", "--selective-perturbation", "off", "--timing-policy", "bounded"],
            ["--rf-hz", "500000", "--base-oscillator", "crystal", "--shift-oscillator", "baseband-clock",
             "--spread-preset", "sub9-worst", "--margin", "1", "--selective-perturbation", "each-25",
             "--selective-modulation-rate-hz", ".5", "--timing-policy", "bounded",
             "--data-power-w", ".001", "--helper-power-w", "4", "--cn0", "6"],
            ["--control", "flat", "--spread-preset", "aurora-worst", "--margin", "3",
             "--selective-perturbation", "each-65", "--selective-modulation-rate-hz", "1",
             "--timing-policy", "broad"],
            ["--helper-power-w", "4e12", "--cn0", "-60"],
            ["--spread-preset", "none", "--selective-perturbation", "each-65"],
            ["--spread-preset", "none", "--selective-perturbation", "fast-1",
             "--selective-modulation-rate-hz", ".05"],
        ]
        resolved = []
        for overrides in physics_cases:
            result = invoke(binary, None, ["--case", "hf", "--plan-only"] + overrides, timings)
            resolved.append(json.loads(result.stdout)["physics"])
        zero, shared, flat, limited, zero_rate, independent_am = resolved
        require(not zero["shift_active"] and zero["rf_relative_pair_ppm_bound"] == 0,
                "zero RF must disable the independent RF error")
        close(zero["relative_phase_diffusion_rad_sqrt_s"], .5 * math.pi / 180,
              "zero RF must retain only base phase diffusion")
        close(zero["arrival_prior_seconds"][0], -.0555, "bounded arrival lower edge")
        close(zero["arrival_prior_seconds"][1], .1255, "bounded arrival upper edge")
        require(zero["message_epoch_count"] == 13, "bounded timing must retain thirteen epochs")
        require(shared["shared_shift_clock"] and shared["shift_oscillator"] == "baseband-clock",
                "shared-clock topology not retained")
        close(shared["spread_delay_ms"], 5, "selected delay margin")
        close(shared["spread_doppler_hz"], .05, "selected Doppler margin")
        close(shared["selective_slow_depth"], .25, "slow modulation selection")
        close(shared["selective_fast_depth"], .25, "fast modulation selection")
        close(shared["selective_modulation_rate_hz"], .5, "independent modulation rate")
        close(shared["effective_helper_cn0_db_hz"], 6 + 10 * math.log10(4000), "independent Helper power")
        require(flat["spread_delay_ms"] == flat["spread_doppler_hz"] == 0 and
                not flat["selective_enabled"] and flat["arrival_prior_seconds"] == [-7.035, 7.035],
                "flat control must bypass requested spread/modulation and preserve the selected prior")
        require(limited["helper_power_limited"] and limited["requested_helper_w"] == 4e12,
                "production Helper ratio cap must be explicit")
        close(limited["effective_helper_w"], 4e9, "effective production Helper cap")
        close(limited["effective_helper_cn0_db_hz"], 30, "capped Helper C/N0")
        require(not zero_rate["selective_enabled"] and zero_rate["selective_modulation_rate_hz"] == 0,
                "None plus AM without a timescale must report the zero-rate bypass")
        require(independent_am["selective_enabled"] and independent_am["spread_doppler_hz"] == 0,
                "None plus an explicit AM timescale must retain modulation")

        # Starting auto selection from an arbitrary longer ramp can silently
        # remove protected cells before the planner checks its invariants.
        for rate in (1200, 3600):
            formats = []
            for mode in ("legacy", "auto"):
                result = invoke(binary, None, ["--case", "eme", "--plan-only", "--shaping", mode,
                                              "--rate", str(rate)], timings)
                formats.append(json.loads(result.stdout)["format"])
            for field in ("fine_coefficients", "inner_coefficients", "payload_fine_cells_per_tile",
                          "tile_seconds", "tile_count", "elapsed_bit_seconds"):
                require(formats[0][field] == formats[1][field],
                        f"auto shaping changed valid input allocation at rate {rate}: {field}")

        invalid = root / "invalid-if"
        invoke(binary, invalid, ["--case", "hf", "--data-if", "500"], timings, succeeds=False)
        require(not invalid.exists(), "invalid IF created output")
        invalid_geometry = root / "invalid-eme-geometry"
        result = invoke(binary, invalid_geometry, ["--case", "eme", "--shaping", "legacy", "--data-if", "1725", "--seconds", "1"],
                        timings, succeeds=False)
        require("production Helper plan rejected" in result.stderr, "invalid EME geometry must fail in production placement")
        require(not invalid_geometry.exists(), "invalid Helper placement wrote output")
        invalid_ramp = root / "invalid-ramp"
        result = invoke(binary, invalid_ramp, ["--case", "eme", "--shaping", "legacy", "--data-if", "1725", "--ramp-seconds", ".01",
                                                "--seconds", "1"], timings, succeeds=False)
        require("no protected dwell cells" in result.stderr, "10 ms ramp must fail protected-cell placement")
        require(not invalid_ramp.exists(), "invalid ramp placement wrote output")
        invalid_hf_ramp = root / "invalid-hf-ramp"
        invoke(binary, invalid_hf_ramp, ["--case", "hf", "--ramp-seconds", ".006", "--seconds", "1"],
               timings, succeeds=False)
        require(not invalid_hf_ramp.exists(), "FHSS-only ramp option created a non-FHSS export")
        invalid_receiver_tail = root / "invalid-receiver-tail"
        invoke(binary, invalid_receiver_tail, ["--case", "hf", "--receiver-tail-seconds", "1.001"],
               timings, succeeds=False)
        require(not invalid_receiver_tail.exists(), "out-of-range receiver tail created an export")
        invalid_controls = [
            ["--rf-hz", "-1"], ["--margin", "0"], ["--spread-preset", "unknown"],
            ["--selective-perturbation", "each-2"], ["--selective-modulation-rate-hz", "-1"],
            ["--timing-policy", "unknown"], ["--data-power-w", "0"], ["--helper-power-w", "0"],
            ["--base-oscillator", "ic-7100"],
            ["--power-reference", "unknown"], ["--power-plan-only"],
        ]
        for overrides in invalid_controls:
            invoke(binary, None, ["--case", "hf", "--plan-only"] + overrides, timings, succeeds=False)

        invoke(binary, root / "hf", ["--case", "hf", "--seconds", "1", "--seed", "91001"], timings)
        hf, hf_truth = check_capture(root / "hf", "hf")
        invoke(binary, root / "eme", ["--case", "eme", "--seconds", "1", "--seed", "91002"], timings)
        check_capture(root / "eme", "eme")
        invoke(binary, root / "eme-6ms-low-if", ["--case", "eme", "--shaping", "legacy", "--seconds", "1", "--seed", "91004",
                                                   "--base-oscillator", "crystal", "--shift-oscillator", "ic-7100",
                                                   "--data-if", "1725", "--ramp-seconds", ".006"], timings)
        low_if, _ = check_capture(root / "eme-6ms-low-if", "eme", data_if=1725,
                                  base_oscillator="crystal", shift_oscillator="ic-7100", ramp_seconds=.006, shaping="legacy")
        close(low_if["helper_plan"]["high_hz"], 2654.1666666666665, "low-IF 6 ms production Helper placement")
        invoke(binary, root / "hf-receiver-tail", ["--case", "hf", "--seconds", "1", "--seed", "91001",
                                                    "--receiver-tail-seconds", ".25"], timings)
        tailed, tailed_truth = check_capture(root / "hf-receiver-tail", "hf", receiver_tail_seconds=.25)
        require(tailed["calendar"]["capture_samples"] == 1315201 + 4500,
                "explicit receiver tail did not extend the fixed public observation by exactly 0.25 s")
        require(tailed_truth["channel_report"]["observation_padding_samples"] >= 4500,
                "explicit receiver tail must extend the fixed capture past the natural physical endpoint")
        with (root / "hf-receiver-tail" / "observation.c128").open("rb") as stream:
            stream.seek((tailed["calendar"]["capture_samples"] - 1) * 16)
            last_noise = struct.unpack("<dd", stream.read(16))
        require(any(value != 0 for value in last_noise), "receiver tail must contain received noise samples")
        require((root / "hf-receiver-tail" / "validity.u8").read_bytes()[-1] == 1,
                "received tail noise is valid input, not a missing-sample marker")
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

        # Actual sampled stage reports verify the new controls are propagated,
        # rather than only copied into nominal plan metadata.
        for index in (0, 1, 5):
            overrides = physics_cases[index]
            directory = root / f"configured-{index}"
            invoke(binary, directory, ["--case", "hf", "--seconds", "1", "--seed", "91005"] + overrides, timings)
            manifest = load_json(directory / "manifest.json")
            truth = load_json(directory / "evaluator-truth.json")
            require(manifest["physics"] == resolved[index], "plan/export resolved physics differ")
            require(manifest["templates"]["epoch_offsets_seconds"] == list(range(-6, 7)),
                    "timing policy changed the message-epoch bank")
            require(manifest["calendar"]["data_calendar_start_seconds"] == 38,
                    "configuration changed the fixed Helper lead")
            if index == 0:
                require(truth["tx"]["rf_ppm"] == truth["rx"]["rf_ppm"] == 0,
                        "zero RF retained a realized RF clock error")
                require(truth["channel_report"]["selective_perturbation"] is None,
                        "None/Off must exactly bypass the selective stage")
            elif index == 1:
                for endpoint in ("tx", "rx"):
                    close(truth[endpoint]["rf_ppm"], truth[endpoint]["audio_ppm"], "shared endpoint realization")
                report = truth["channel_report"]["selective_perturbation"]
                require(report is not None and report["component_depths"] == [.25, .25],
                        "selected physical modulation was omitted")
                close(report["component_rates_hz"][0], .05, "actual slow modulation rate")
                close(report["component_rates_hz"][1], .5, "actual fast modulation rate")
            else:
                report = truth["channel_report"]["selective_perturbation"]
                require(report is not None and report["component_depths"] == [0, .01],
                        "independent AM timescale was dropped with None spread")
                close(report["component_rates_hz"][1], .05, "actual independent AM rate")
            samples = manifest["calendar"]["capture_samples"]
            require((directory / "observation.c128").stat().st_size == samples * 16 and
                    (directory / "validity.u8").stat().st_size == samples,
                    "configured export did not retain complete sampled rows")
            require(truth["channel_report"]["natural_complete_samples"] <= samples and
                    truth["channel_report"]["tail_samples"] > 0,
                    "configured export truncated physical tails")
    print(json.dumps({"result": "passed", "captures": 8, "rejected_invocations": 16,
                      "wall_seconds": time.perf_counter() - started, "commands": timings,
                      "scope": "CLI/export metadata, actual emitted controls and complete files; no receiver or statistical success claim"}))


if __name__ == "__main__":
    main()
