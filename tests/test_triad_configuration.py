#!/usr/bin/env python3
"""Focused isolated triad configuration/preset and physical-truth checks."""
import json
import math
import pathlib
import subprocess
import sys
import tempfile

BINARY = sys.argv[1]
COMMON = ["--rate", "36", "--added", "1", "--sample-rate", "64", "--seconds", "8",
          "--rf-hz", "500000", "--epoch", "1800000001", "--seed", "71231",
          "--base-oscillator", "gpsdo-xo", "--shift-oscillator", "gpsdo-xo",
          "--clock-sync", "GPS_10ms-1ms_region-0ms_offset", "--audio-error-ms", "70",
          "--margin", "1", "--spread-preset", "hf-best", "--selective-perturbation", "each-1"]


def invoke(*args, ok=True, timeout=90):
    result = subprocess.run([BINARY, *args], capture_output=True, text=True, timeout=timeout)
    if (result.returncode == 0) != ok:
        raise AssertionError(f"unexpected status {result.returncode}: {args}\n{result.stderr[-2000:]}")
    return result


def rows(result):
    return [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]


def configure(*extra):
    return rows(invoke("--mode", "configure", *COMMON, *extra))[0]["configuration"]


def near(actual, expected, message, tolerance=1e-10):
    if not math.isclose(actual, expected, rel_tol=tolerance, abs_tol=tolerance):
        raise AssertionError(f"{message}: got {actual}, expected {expected}")


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    c = configure("--symbol-burst", "3", "--start", "-.09375", ".09375", ".03125")
    near(c["base_nominal_relative_bound_ppm"], .0001, "base relative accuracy")
    near(c["shift_nominal_relative_bound_ppm"], .0001, "RF relative accuracy")
    near(c["relative_phase_diffusion_rad_sqrt_s"], math.sqrt(2)*.5*math.pi/180, "combined relative phase")
    near(c["spread_delay_ms"], .5, "HF best delay")
    near(c["spread_doppler_hz"], .1, "HF best Doppler")
    near(c["perturbation_slow_depth"], .01, "slow selective depth")
    near(c["perturbation_fast_depth"], .01, "fast selective depth")
    near(c["initial_arrival_center_seconds"], 0, "physical relative timing center")
    near(c["initial_arrival_half_width_seconds"], .0905, "two-station audio/clock/region bound")
    near(c["calendar_timestamp_center_seconds"], .035, "separate timestamp coordinate center", 2e-7)
    check(not c["calendar_timestamp_center_shift_applied_to_raw_samples"], "timestamp center was applied twice")
    check(c["public_search"]["covers_initial_timing_prior"], "covered physical timing bank marked uncovered")
    check(c["capture"]["feasible_for_deterministic_prior"], "disciplined 500-kHz capture marked infeasible")
    near(c["capture"]["fine_rate_per_carrier_hz"], 6, "GUI rate mapping")
    near(c["capture"]["nominal_triplet_bandwidth_hz"], 22.5, "three-carrier bandwidth")
    check(c["suggested_epoch_offsets"] == [-1, 0, 1], "clock-boundary epoch alternatives")
    search = c["search_estimates"]
    near(search["timing_loss_budget_db"], .5, "default timing loss budget")
    # Independently evaluated first-main-lobe roots of the ideal RC/triplet
    # autocorrelation, rather than the former fine-envelope-only 1/(4*6).
    near(search["timing_maximum_step_seconds"], .016844350713653217, "triplet timing loss root")
    check(search["timing_maximum_step_seconds"] < 1/(8*6), "recommendation ignores inter-carrier timing phase")
    near(search["timing_model_fine_rate_hz"], 6, "timing model fine rate")
    near(search["timing_model_carrier_spacing_hz"], 7.5, "timing model carrier spacing")
    near(search["timing_model_pulse_rolloff"], .25, "shared pulse rolloff")
    check(not search["timing_model_is_finite_waveform_certificate"], "approximate timing model claimed certification")
    tight = configure("--timing-loss-db", ".1")["search_estimates"]
    near(tight["timing_maximum_step_seconds"], .007570484365158795, "stricter timing loss root")
    check(tight["clock_maximum_step_ppm"] < search["clock_maximum_step_ppm"], "clock recommendation kept the envelope-only loss")
    near(configure("--rate", "72")["search_estimates"]["timing_maximum_step_seconds"],
         search["timing_maximum_step_seconds"]/2, "timing model must use actual waveform rate")

    scaled = configure("--margin", "3")
    near(scaled["spread_delay_ms"], 1.5, "spread margin once")
    near(scaled["spread_doppler_hz"], .3, "Doppler margin once")
    near(scaled["perturbation_slow_depth"], .01, "margin must not scale modulation depth")
    near(scaled["deterministic_oscillator_cfo_bound_hz"], c["deterministic_oscillator_cfo_bound_hz"], "margin must not widen oscillator truth")
    near(scaled["search_estimates"]["margin_frequency_half_width_hz"], 3*c["deterministic_oscillator_cfo_bound_hz"], "search margin")
    no_if = configure("--nominal-if-hz", "0")
    check(no_if["deterministic_oscillator_cfo_bound_hz"] < c["deterministic_oscillator_cfo_bound_hz"], "IF contribution omitted")

    burst = c["whole_bit_symbol_burst"]; rate = c["capture"]["sample_rate_hz"]
    support = burst["support_begin"]; end = burst["support_end"]
    check(support == {"epoch": 1800000030, "sample_phase": 0}, "burst support must ceil to UTC30")
    earliest = end["epoch"]+(end["sample_phase"]+3*burst["useful_samples"])/rate
    expected_next = 30*math.ceil(max(earliest, support["epoch"]+60)/30)
    check(burst["next_support_begin"] == {"epoch": expected_next, "sample_phase": 0}, "whole-bit successor calendar")
    check(burst["internal_wait_tiles"] == 0 and not burst["changes_inside_one_bit"], "whole-bit burst became internal tile waits")
    check(configure()["whole_bit_symbol_burst"]["useful_samples"] == burst["useful_samples"], "burst changed the useful one-bit duration")
    shifted = configure("--truth-epoch-offset", "-1")
    check(shifted["transmitter_pattern_epoch"] == shifted["receiver_pattern_epoch"]-1, "independent keyed epoch offset ignored")

    free = configure("--shift-oscillator", "crystal", "--rf-hz", "5800000000")
    check(free["deterministic_oscillator_cfo_bound_hz"] > 580000, "free-running microwave RF uncertainty missing")
    check(not free["capture"]["feasible_for_deterministic_prior"], "huge RF uncertainty silently aliased into 64 Hz")
    with tempfile.TemporaryDirectory(prefix="triad-config-") as directory:
        root = pathlib.Path(directory); impossible = root/"impossible"
        bad = invoke("--mode", "export-stats", *COMMON, "--shift-oscillator", "crystal", "--rf-hz", "5800000000", "--output", str(impossible), ok=False)
        check("cannot fit the sampled baseband" in bad.stderr and not impossible.exists(), "capture rejection came after sample generation")
        manifests = []
        for name, frequency in (("a", ("0", "0", "1")), ("b", ("-.01", ".01", ".01"))):
            destination = root/name
            invoke("--mode", "export-stats", *COMMON, "--phase-diffusion", "0", "--spread-preset", "none", "--selective-perturbation", "off", "--frequency", *frequency,
                   "--start", "-.09375", ".09375", ".09375",
                   "--block-seconds", "4", "--output", str(destination))
            manifests.append(json.loads((destination/"manifest.json").read_text()))
        check(manifests[0]["endpoint_truth"] == manifests[1]["endpoint_truth"], "public CFO bank selected physical oscillator truth")
        near(manifests[0]["selected_truth_frequency_hz_simulation_only"], manifests[1]["selected_truth_frequency_hz_simulation_only"], "public CFO bank recentered truth")
        check(abs(manifests[0]["endpoint_truth"]["tx_rf_ppm"]) <= .00005, "relative RF bound was doubled per endpoint")
        for manifest in manifests:
            pad = manifest["capture_lead_seconds"]
            near(pad, .14375, "capture lead must be explicit")
            configured = manifest["resolved_configuration"]
            bank_low, bank_high = manifest["public_timing_bounds_capture_seconds"]
            low, high = configured["public_search"]["timing_bounds_seconds"]
            near(bank_low, low+pad, "translated timing bank minimum")
            near(bank_high, high+pad, "translated timing bank maximum")
            check(all(bank_low <= candidate["arrival_seconds"] <= bank_high for candidate in manifest["candidates"]),
                  "candidate timing is outside the translated public bank")
            truth = manifest["selected_truth_arrival_seconds_simulation_only"]
            physical_low, physical_high = manifest["initial_physical_arrival_bounds_capture_seconds"]
            check(physical_low <= truth <= physical_high and bank_low <= truth <= bank_high,
                  "covered timing truth is outside capture-coordinate bounds")
            rx_scale = 1+manifest["endpoint_truth"]["rx_audio_ppm"]*1e-6
            relative_truth = truth/rx_scale-pad
            low, high = configured["initial_arrival_bounds_seconds"]
            check(low <= relative_truth <= high, "capture-coordinate truth does not translate to the physical prior")
            check(manifest["physical_arrival_bounds_include_rx_sample_clock"], "capture bounds omit endpoint clock scaling")

    # Actual two-end audio draws must cover both signs around physical offset,
    # not acquire an accidental +Audio/2 shift from timestamp coordinates.
    timing = rows(invoke("--mode", "run", *COMMON, "--spread-preset", "none", "--selective-perturbation", "off",
        "--clock-sync", "GPS_0ms-0ms_region-0ms_offset", "--phase-diffusion", "0", "--trials", "128", "--cn0", "-100", "--threads", "1", "--block-seconds", "4"))
    check(len(timing) == 128, "audio timing ensemble incomplete")
    offsets = [row["truth_arrival_seconds"]-row["candidates"][0]["arrival_seconds"] for row in timing]
    check(all(-.07000001 <= value <= .07000001 for value in offsets), "causal lag difference escaped physical bounds")
    check(abs(sum(offsets)/len(offsets)) < .0125 and min(offsets) < -.035 and max(offsets) > .035, "audio timing draws have a shifted mean or one-sided support")
    check(all("independent TX/RX" in row["sample_clock_reference"] for row in timing), "run mislabeled endpoint sample clocks")
    check(not configure()["public_search"]["covers_initial_timing_prior"], "known-alignment bank falsely claims timing coverage")
    for option, value in (("--base-oscillator", "ic-7100"), ("--selective-perturbation", "each-2"), ("--margin", ".5"), ("--symbol-burst", "4")):
        invoke("--mode", "configure", *COMMON, option, value, ok=False)
    for loss in ("0", "-1", "nan", "inf", "0.0000001", "61"):
        invoke("--mode", "configure", *COMMON, "--timing-loss-db", loss, ok=False)
    invoke("--mode", "configure", *COMMON, "--symbol-burst", "3", "--burst-wait-tiles", "3", ok=False)
    print(json.dumps({"configuration_checks": "passed", "audio_draws": len(offsets), "audio_mean_seconds": sum(offsets)/len(offsets),
                      "audio_min_seconds": min(offsets), "audio_max_seconds": max(offsets)}))


if __name__ == "__main__":
    main()
