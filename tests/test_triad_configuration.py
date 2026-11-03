#!/usr/bin/env python3
"""Focused isolated triad configuration/preset and physical-truth checks."""
import json
import math
import pathlib
import struct
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


def observation_control_checks():
    wide = configure("--clock-sync", "default", "--arrival-prior-seconds", "-7.035", "7.035",
                     "--start", "-7.035", "7.035", "1.005")
    check(wide["explicit_arrival_prior"], "explicit physical arrival prior omitted")
    check(wide["initial_arrival_bounds_seconds"] == [-7.035, 7.035], "wide arrival bounds changed")
    near(wide["initial_arrival_half_width_seconds"], 7.035, "wide arrival prior collapsed")
    check(wide["public_search"]["covers_initial_timing_prior"], "covered wide prior marked uncovered")
    check(not configure("--arrival-prior-seconds", "-7.035", "7.035")["public_search"]["covers_initial_timing_prior"],
          "known-alignment search falsely covers wide physical arrival prior")
    zero = configure("--arrival-prior-seconds", "0", "0")
    check(zero["public_search"]["covers_initial_timing_prior"], "degenerate known arrival prior rejected")
    near(zero["calendar_timestamp_audio_center_shift_seconds"], 0, "overridden audio lag still shifted calendar")
    check(not configure("--clock-sync", "default")["explicit_arrival_prior"], "legacy default changed silently")
    for invalid in (("--arrival-prior-seconds", "1", "-1"), ("--arrival-prior-seconds", "nan", "1"),
                    ("--arrival-prior-seconds", "0", "inf"), ("--minimum-observed-fraction", "-0.01"),
                    ("--minimum-observed-fraction", "0"),
                    ("--minimum-observed-fraction", "1.01"), ("--missed-dwells", "0"),
                    ("--fhss", "--missed-dwells", "1,1"), ("--fhss", "--missed-dwells", "2,1"),
                    ("--fhss", "--missed-dwells", "-1"), ("--fhss", "--missed-dwells", "0,"),
                    ("--fhss", "--missed-dwells", ",0")):
        rejected = invoke("--mode", "configure", *COMMON, *invalid, ok=False)
        if "--missed-dwells" in invalid:
            check("missed receiver dwell" in rejected.stderr, "invalid erasure list failed for an unrelated reason")
    rejected = invoke("--mode", "configure", *COMMON, "--fhss", "--missed-dwells", ",".join(map(str, range(4097))), ok=False)
    check("missed receiver dwell" in rejected.stderr, "oversized erasure list failed for an unrelated reason")
    check(len(configure("--rate", "1200", "--sample-rate", "960", "--fhss", "--missed-dwells", ",".join(map(str, range(4096))))
              ["receiver_observation"]["missed_dwell_indices"]) == 4096, "declared erasure-list limit is not usable")

    physical = (*COMMON, "--clock-sync", "default", "--spread-preset", "none",
                "--selective-perturbation", "off", "--phase-diffusion", "0", "--threads", "1")
    with tempfile.TemporaryDirectory() as root:
        manifests = []
        for label, search in (("nominal", ("0", "0", "1")), ("wide", ("-3", "3", "3"))):
            destination = pathlib.Path(root)/label
            invoke("--mode", "export-stats", *physical, "--arrival-prior-seconds", "-2.5", "-2.5",
                   "--truth-frequency-hz", ".2", "--truth-frequency-drift", ".01", "--start", *search,
                   "--output", str(destination))
            manifests.append(json.loads((destination/"manifest.json").read_text()))
        check(manifests[0]["endpoint_truth"] == manifests[1]["endpoint_truth"],
              "explicit arrival prior or search bank changed endpoint oscillator draws")
        for manifest in manifests:
            rx_scale = 1+manifest["endpoint_truth"]["rx_audio_ppm"]*1e-6
            truth = manifest["selected_truth_arrival_seconds_simulation_only"]
            near(truth/rx_scale-manifest["capture_lead_seconds"], -2.5,
                 "public search selected the explicit physical arrival")
            endpoint = manifest["endpoint_truth"]
            tx_scale = 1+endpoint["tx_audio_ppm"]*1e-6
            cfg = manifest["resolved_configuration"]
            expected_cfo = .2 + (cfg["rf_center_hz"]*(endpoint["tx_rf_ppm"]-endpoint["rx_rf_ppm"])*1e-6 +
                                 cfg["nominal_conversion_if_hz"]*(tx_scale-rx_scale))/rx_scale
            near(manifest["selected_truth_frequency_hz_simulation_only"], expected_cfo,
                 "arrival override moved drift reference")
            low, high = manifest["initial_physical_arrival_bounds_capture_seconds"]
            check(low <= truth <= high, "physical arrival is outside exported conservative bounds")
            check(truth >= 0, "negative physical prior was clipped at capture start")

        erased_args = (*physical, "--rate", "1200", "--sample-rate", "960", "--fhss", "--arrival-prior-seconds", "0", "0", "--cn0", "80",
                       "--block-seconds", "4", "--missed-dwells", "0,1,7,19")
        gated = rows(invoke("--mode", "run", *erased_args, "--minimum-observed-fraction", "1"))[0]
        admitted = rows(invoke("--mode", "run", *erased_args, "--minimum-observed-fraction", ".5"))[0]
        check(gated["complete_observation"] and admitted["complete_observation"], "erasure policy changed complete-calendar handling")
        check(gated["missing_samples"] > 0 and gated["missing_samples"] == admitted["missing_samples"], "coverage gate changed erasures")
        near(gated["score"], admitted["score"], "coverage gate changed evidence")
        near(gated["threshold"], admitted["threshold"], "coverage gate changed threshold")
        check(not gated["accepted"] and admitted["correct"], "coverage gate was ignored or silently changed likelihood acceptance")
        for mode in ("export-sampled", "export-stats", "export-group-stats"):
            destination = pathlib.Path(root)/mode
            extra = ("--stats-noisy",) if mode != "export-sampled" else ()
            invoke("--mode", mode, *erased_args, *extra, "--output", str(destination))
            manifest = json.loads((destination/"manifest.json").read_text())
            controls = manifest["resolved_configuration"]["receiver_observation"]
            check(controls["missed_dwell_indices"] == [0, 1, 7, 19], "export lost receiver erasure schedule")
            check(not controls["physical_rf_retuning"], "envelope experiment claimed RF retuning")
            if mode == "export-sampled":
                validity = (destination/manifest["validity_file"]).read_bytes()
                valid_count = sum(validity)
                observation = (destination/manifest["observation_file"]).read_bytes()
                check(all(valid or struct.unpack_from("<dd", observation, 16*i) == (0., 0.)
                          for i, valid in enumerate(validity)), "missing I/Q is not zero in sampled export")
            else:
                raw = (destination/manifest["valid_rows_file"]).read_bytes()
                valid_count = sum(value[0] for value in struct.iter_unpack("<Q", raw))
            check(manifest["sample_count"]-valid_count == gated["missing_samples"],
                  "sampled/statistics modes disagree on erased row count")
        invoke("--mode", "export", *erased_args, "--output", str(pathlib.Path(root)/"unsupported"), ok=False)


def main():
    observation_control_checks()
    configured_selective = configure()["selective_channel"]
    check(configured_selective["model"] == "dispersive-dft-v1",
          "isolated v3 does not default to corrected selective channel model")
    check(configured_selective["geometry_applied"], "corrected model geometry is not applied")
    near(configured_selective["resolved_correlation_scale_hz"],
         rows(invoke("--mode", "configure", *COMMON))[0]["configuration"]["search_estimates"]["timing_model_carrier_spacing_hz"],
         "automatic selective scale is not carrier spacing")
    check("never recentered" in configured_selective["frequency_frame"],
          "selective channel frequency frame may use hidden truth")
    legacy_selective = configure("--selective-model", "legacy-allpass")["selective_channel"]
    check(legacy_selective["model"] == "legacy-allpass-v1",
          "explicit legacy selective model was not retained")
    check(not legacy_selective["geometry_applied"], "legacy replay misleadingly claims corrected geometry")
    legacy_geometry = configure("--selective-model", "legacy-allpass", "--selective-scale-hz", ".00075",
                                "--selective-center-hz", ".025")["selective_channel"]
    check(not legacy_geometry["geometry_applied"],
          "explicit replay arguments changed the historical model geometry")
    custom_selective = configure("--selective-scale-hz", ".00075", "--selective-center-hz", ".025",
                                 "--selective-tail-floor-seconds", "900")["selective_channel"]
    near(custom_selective["resolved_correlation_scale_hz"], .00075, "explicit selective scale")
    near(custom_selective["center_hz"], .025, "explicit selective frame center")
    near(custom_selective["selective_tail_floor_seconds"], 900, "common selective-tail floor")
    for invalid in (("--selective-model", "unknown"), ("--selective-scale-hz", "-1"),
                    ("--selective-scale-hz", "nan"), ("--selective-center-hz", "inf"),
                    ("--selective-tail-floor-seconds", "-1")):
        invoke("--mode", "configure", *COMMON, *invalid, ok=False)

    low_rate = ("--rate", ".036", "--sample-rate", "1", "--rf-hz", "8270",
                "--spread-preset", "sub9-best")
    month = configure(*low_rate, "--seconds", "3283200")
    check(month["capture"]["sub_64_hz_model"], "low-rate model scope omitted")
    check("no high-rate integration/decimation equivalence" in month["capture"]["sampling_model_limits"],
          "low-rate model claims unvalidated sample-rate equivalence")
    near(month["duration"]["maximum_requested_active_seconds"], 315576000, "ten-year request ceiling")
    near(month["duration"]["requested_active_seconds"], 3283200, "38-day request lost")
    check(month["duration"]["source_support_seconds"] > 3283200, "pulse/tile overhead was clipped to request ceiling")
    near(month["spread_doppler_hz"], .001, "low-rate Doppler preset lost")
    near(month["spread_delay_ms"], .005, "low-rate fractional delay lost")
    near(month["perturbation_slow_depth"], .01, "low-rate selected perturbation lost")
    invoke("--mode", "configure", *COMMON, *low_rate, "--seconds", "315576001", ok=False)
    years = configure(*low_rate, "--rate", ".0036", "--sample-rate-denominator", "64",
                      "--seconds", "86400000", "--symbol-burst", "3")
    near(years["capture"]["sample_rate_hz"], 1/64, "effective reciprocal sample rate")
    check((years["capture"]["sample_rate_numerator"], years["capture"]["sample_rate_denominator"]) == (1, 64),
          "reciprocal rate provenance missing")
    near(years["duration"]["requested_active_seconds"], 86400000, "year request lost")
    near(years["calendar_timestamp_center_seconds"], .035, "sub-Hz calendar audio center", 2e-7)
    near(years["initial_arrival_half_width_seconds"], .0905, "sub-Hz physical timing prior")
    # Independent physical-second corner of the shared timestamp uncertainty:
    # clock drift accumulates over support, while its .01-sample guard is now
    # .64 seconds. The helper's rounding guard is small but deliberately kept.
    fractional_clock = years["exact_relative_audio_ratio_bound_ppm"]*1e-6
    expected_half = ((.0905+.64) + fractional_clock*(years["duration"]["source_support_seconds"]+.035))/(1-fractional_clock)
    near(years["full_observation_calendar_half_width_seconds"], expected_half,
         "sub-Hz calendar scaling lost a physical-second bound", 2e-7)
    calendar = years["whole_bit_symbol_burst"]
    check(all(calendar[name]["sample_phase"] == 0 for name in
              ("support_begin", "payload_begin", "payload_end", "support_end", "next_support_begin")),
          "reciprocal sample UTC calendar has fractional seconds")
    check(calendar["payload_begin"]["epoch"]-calendar["support_begin"]["epoch"] ==
          calendar["pulse_padding_samples_each_side"]*64, "sub-Hz halo UTC coordinate")
    check(calendar["payload_end"]["epoch"]-calendar["payload_begin"]["epoch"] ==
          calendar["useful_samples"]*64, "sub-Hz useful UTC extent")
    after_wait = max(calendar["support_end"]["epoch"]+3*calendar["useful_samples"]*64,
                     calendar["support_begin"]["epoch"]+60)
    check(calendar["next_support_begin"]["epoch"] == ((after_wait+29)//30)*30,
          "sub-Hz whole-bit burst waiting rule")
    for invalid in (("--sample-rate-denominator", "0"), ("--sample-rate-denominator", "4294967296"),
                    ("--sample-rate", "2", "--sample-rate-denominator", "2"),
                    ("--sample-rate", "0", "--sample-rate-denominator", "64")):
        invoke("--mode", "configure", *COMMON, *low_rate, *invalid, ok=False)
    invoke("--mode", "configure", *COMMON, *low_rate, "--sample-rate", "1.5", ok=False)
    invoke("--mode", "configure", *COMMON, *low_rate, "--rate", "3.6", ok=False)
    for impairment in (("--fhss",), ("--dropout-ms", "10"), ("--white-fm", ".001"),
                       ("--flicker-fm", ".001"), ("--random-walk-fm", ".001"),
                       ("--white-pm", ".001"), ("--adc-bits", "16"),
                       ("--rx-lowpass-hz", ".1")):
        rejected = invoke("--mode", "configure", *COMMON, *low_rate, *impairment, ok=False)
        check("sub-64-Hz" in rejected.stderr, f"low-rate impairment was not explicitly rejected: {impairment}")
        if impairment != ("--fhss",):
            check(not configure(*impairment)["capture"]["sub_64_hz_model"],
                  "legacy 64-Hz impairment support changed")
    check(configure("--sample-rate", "0")["capture"]["sample_rate_hz"] == 960,
          "automatic legacy sample-rate selection changed")
    c = configure("--symbol-burst", "3", "--start", "-.09375", ".09375", ".03125")
    path_off = c["path_evolution"]
    check(not path_off["active"], "physical path stress is not off by default")
    near(path_off["ray_coherence_seconds"], 0, "default ray coherence")
    near(path_off["gaussian_doppler_width_hz"], 0, "Gaussian ray model must default off")
    check("fixed unit ray gains" in path_off["ray_model"] and "OU" not in path_off["ray_model"],
          "default fixed rays were mislabeled as stochastic OU gains")
    near(path_off["diurnal_phase_amplitude_radians"], 0, "default diurnal phase")
    near(path_off["diurnal_period_seconds"], 86400, "default diurnal period")
    check(path_off["outage_start_seconds"] == path_off["outage_end_seconds"] == -1,
          "physical outage is not disabled by both default endpoints")
    path_args = ("--ray-coherence-seconds", "1000", "--diurnal-phase-radians", "864",
                 "--diurnal-period-seconds", "86400", "--outage-seconds", "100", "1000",
                 "--outage-gain", ".1", "--outage-ramp-seconds", "60", "--path-seed", "99")
    path_config = configure(*path_args)
    path = path_config["path_evolution"]
    check(path["active"], "explicit physical path controls were ignored")
    near(path["ray_coherence_seconds"], 1000, "OU coherence")
    near(path["maximum_diurnal_instantaneous_frequency_hz"], .01, "diurnal frequency derivative")
    near(path_config["capture"]["deterministic_nyquist_margin_hz"],
         c["capture"]["deterministic_nyquist_margin_hz"]-.02, "diurnal excursion missing from both capture edges")
    check("unbounded analog spectrum" in path["scope"] and "refinement" in path["scope"],
          "OU path spectrum qualification omitted")
    near(path["outage_start_seconds"], 100, "physical outage start")
    near(path["outage_end_seconds"], 1000, "physical outage end")
    near(path["outage_gain"], .1, "physical outage attenuation")
    near(path["outage_ramp_seconds"], 60, "physical outage ramps")
    gaussian = configure("--ray-gaussian-width-hz", ".001")
    gp = gaussian["path_evolution"]
    check(gp["active"], "Gaussian ray width did not activate physical path evolution")
    near(gp["ray_coherence_seconds"], 0, "Gaussian rays silently enabled OU")
    near(gp["gaussian_doppler_width_hz"], .001, "Gaussian target width")
    near(gp["gaussian_doppler_standard_deviation_hz"], .0005, "Gaussian width is not two sigma")
    near(gp["gaussian_capture_total_allowance_hz"], .006, "Gaussian six-sigma capture edges")
    near(gaussian["capture"]["deterministic_nyquist_margin_hz"],
         c["capture"]["deterministic_nyquist_margin_hz"]-.006, "Gaussian capture allowance omitted or doubled")
    near(gaussian["spread_doppler_hz"], c["spread_doppler_hz"], "ray width changed independent Doppler centers")
    check("FIR" in gp["ray_model"] and "B=2*sigma" in gp["gaussian_target_correlation"],
          "Gaussian teacher conventions missing")
    for invalid in (("--ray-coherence-seconds", "-1"), ("--ray-gaussian-width-hz", "-1"),
                    ("--ray-gaussian-width-hz", "1e308"),
                    ("--ray-gaussian-width-hz", ".001", "--ray-coherence-seconds", "1000"),
                    ("--diurnal-phase-radians", "-1"),
                    ("--diurnal-period-seconds", "0"), ("--diurnal-period-seconds", "nan"),
                    ("--diurnal-phase-radians", "1e308", "--diurnal-period-seconds", "1e-100"),
                    ("--outage-seconds", "-1", "100"), ("--outage-seconds", "10", "10"),
                    ("--outage-seconds", "10", "100"), ("--outage-gain", "1.1"),
                    ("--outage-ramp-seconds", "-1"),
                    ("--trials", "2", "--path-seed", str((1 << 64)-1))):
        invoke("--mode", "configure", *COMMON, *invalid, ok=False)
    for nonfinite in ("inf", "-inf", "nan"):
        for option in ("--ray-coherence-seconds", "--ray-gaussian-width-hz", "--diurnal-phase-radians",
                       "--diurnal-period-seconds", "--outage-gain", "--outage-ramp-seconds"):
            result = invoke("--mode", "configure", *COMMON, option, nonfinite, ok=False)
            check(not rows(result), "nonfinite path scalar emitted configuration JSON")
        for endpoints in ((nonfinite, "1000"), ("0", nonfinite)):
            result = invoke("--mode", "configure", *COMMON, "--outage-seconds", *endpoints, ok=False)
            check(not rows(result), "nonfinite outage endpoint emitted configuration JSON")
    invoke("--mode", "plan", "--ray-coherence-seconds", "1000", ok=False)
    invoke("--mode", "plan", "--ray-gaussian-width-hz", ".001", ok=False)
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
    check(scaled["spread_margin_applied_once"], "preset margin application not reported")
    near(scaled["perturbation_slow_depth"], .01, "margin must not scale modulation depth")
    near(scaled["deterministic_oscillator_cfo_bound_hz"], c["deterministic_oscillator_cfo_bound_hz"], "margin must not widen oscillator truth")
    near(scaled["search_estimates"]["margin_frequency_half_width_hz"], 3*c["deterministic_oscillator_cfo_bound_hz"], "search margin")
    explicit_same = configure("--margin", "3", "--spread-delay-ms", ".5", "--spread-doppler-hz", ".1")
    for field in ("spread_delay_ms", "spread_doppler_hz", "perturbation_slow_depth", "perturbation_fast_depth"):
        near(explicit_same[field], scaled[field], "nominal override parity: "+field)
    delay_only = configure("--margin", "3", "--spread-delay-ms", "2")
    near(delay_only["spread_delay_ms"], 6, "delay override was not nominal")
    near(delay_only["spread_doppler_hz"], .3, "delay override changed untouched preset Doppler")
    doppler_only = configure("--margin", "3", "--spread-doppler-hz", ".2")
    near(doppler_only["spread_delay_ms"], 1.5, "Doppler override changed untouched preset delay")
    near(doppler_only["spread_doppler_hz"], .6, "Doppler override margin applied twice")
    paired = configure("--spread-delay-ms", ".2", "--spread-doppler-hz", ".05",
                       "--spread-preset", "sub9-best", "--margin", "3")
    near(paired["spread_delay_ms"], .6, "preset order erased explicit delay")
    near(paired["spread_doppler_hz"], .15, "preset order erased explicit Doppler")
    inputs = paired["spread_inputs"]
    check(inputs["selected_preset"] == "sub9-best", "override replaced selected preset provenance")
    near(inputs["delay_ms_override_nominal"], .2, "delay nominal override provenance")
    near(inputs["doppler_hz_override_nominal"], .05, "Doppler nominal override provenance")
    near(inputs["applied_margin"], 3, "override effective margin provenance")
    check(c["spread_inputs"]["delay_ms_override_nominal"] is None and
          c["spread_inputs"]["doppler_hz_override_nominal"] is None, "unset overrides acquired defaults")
    legacy_args = ("--mode", "configure", "--rate", "36", "--added", "1", "--sample-rate", "64",
                   "--seconds", "8", "--channel", "hf", "--margin", "3")
    legacy = rows(invoke(*legacy_args))[0]["configuration"]
    near(legacy["spread_delay_ms"], 1.5, "legacy manual effective delay changed without override")
    near(legacy["spread_doppler_hz"], .3, "legacy manual effective Doppler changed without override")
    check(not legacy["spread_margin_applied_once"] and legacy["spread_inputs"]["applied_margin"] == 1,
          "legacy manual channel falsely reports applying margin")
    fallback = rows(invoke(*legacy_args, "--spread-delay-ms", "2"))[0]["configuration"]
    near(fallback["spread_delay_ms"], 6, "manual fallback override delay")
    near(fallback["spread_doppler_hz"], .9, "manual fallback nominal base Doppler")
    check(fallback["spread_margin_applied_once"], "explicit override margin application not reported")
    zero_delay = configure("--spread-preset", "none", "--spread-delay-ms", ".5")
    near(zero_delay["spread_delay_ms"], .5, "delay-only physical channel")
    near(zero_delay["spread_doppler_hz"], 0, "delay-only physical channel added Doppler")
    zero_doppler = configure("--spread-preset", "none", "--spread-doppler-hz", ".1")
    near(zero_doppler["spread_delay_ms"], 0, "Doppler-only physical channel added delay")
    near(zero_doppler["spread_doppler_hz"], .1, "Doppler-only physical channel")
    for flag in ("--spread-delay-ms", "--spread-doppler-hz"):
        for value in ("-1", "nan", "inf", "-inf"):
            invoke("--mode", "configure", *COMMON, flag, value, ok=False)
        invoke("--mode", "configure", *COMMON, "--margin", "1000", flag, "1e308", ok=False)
        invoke("--mode", "plan", flag, "0", ok=False)
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
        diurnal_impossible = root/"diurnal-impossible"
        bad = invoke("--mode", "export-stats", *COMMON, *low_rate,
                     "--diurnal-phase-radians", "1", "--diurnal-period-seconds", "1",
                     "--output", str(diurnal_impossible), ok=False)
        check("cannot fit the sampled baseband" in bad.stderr and not diurnal_impossible.exists(),
              "diurnal instantaneous frequency escaped capture preflight")
        gaussian_impossible = root/"gaussian-impossible"
        bad = invoke("--mode", "export-stats", *COMMON, "--ray-gaussian-width-hz", "100",
                     "--output", str(gaussian_impossible), ok=False)
        check("cannot fit the sampled baseband" in bad.stderr and not gaussian_impossible.exists(),
              "Gaussian capture allowance escaped preflight")
        gaussian_output = root/"gaussian-teacher"
        invoke("--mode", "export-stats", *COMMON, "--ray-gaussian-width-hz", ".5",
               "--block-seconds", "4", "--output", str(gaussian_output))
        gm = json.loads((gaussian_output/"manifest.json").read_text())
        teacher = gm["path_truth_simulation_only"]["gaussian_ray_teacher"]
        coefficients = teacher["fir_coefficients"]
        check(1 < len(coefficients) <= 8193 and len(coefficients)%2 == 1,
              "actual Gaussian FIR shape was not serialized")
        near(sum(v*v for v in coefficients), 1, "actual Gaussian FIR variance normalization")
        rx_scale = 1+gm["endpoint_truth"]["rx_audio_ppm"]*1e-6
        near(teacher["sample_interval_seconds"], 1/(64*rx_scale), "actual Gaussian teacher physical timestep")
        near(gm["channel_model"]["path_evolution"]["gaussian_doppler_width_hz"], .5,
             "export lost Gaussian width input")
        # Selective modulation rates follow effective ray separation, not the
        # independently chosen Gaussian width. These fields are oracle-only.
        selective_outputs = []
        for mode, depths in (("slow-65", (.65, 0)), ("fast-65", (0, .65)), ("each-65", (.65, .65))):
            destination = root/("selective-"+mode)
            invoke("--mode", "export-stats", *COMMON, "--selective-perturbation", mode,
                   "--selective-model", "legacy-allpass",
                   "--spread-doppler-hz", ".2", "--margin", "3", "--ray-gaussian-width-hz", ".5",
                   "--block-seconds", "4", "--output", str(destination))
            manifest = json.loads((destination/"manifest.json").read_text())
            selective = manifest["path_truth_simulation_only"]["selective_perturbation"]
            endpoint = manifest["observation_endpoint_report"]
            check(endpoint["processed_samples"] == manifest["sample_count"] == manifest["channel_samples"],
                  "export did not process the complete public observation calendar")
            check(endpoint["natural_complete_samples"] + endpoint["public_calendar_padding_samples"] ==
                  manifest["sample_count"], "public calendar truncated the natural complete channel endpoint")
            check(selective["enabled"], "selected selective teacher was not reported active")
            check(selective["component_order"] == ["slow", "fast"] and
                  selective["band_order"] == ["low", "middle", "high"], "selective array ordering lost")
            check(selective["component_depths"] == list(depths), "selective depths incorrectly margin-scaled")
            near(selective["component_rates_hz"][0], .06, "selective slow rate is not effective Doppler/10")
            near(selective["component_rates_hz"][1], .6, "selective fast rate is not effective Doppler")
            physical_fs = 64*(1+manifest["endpoint_truth"]["rx_audio_ppm"]*1e-6)
            near(selective["sample_rate_hz"], physical_fs, "selective report lost actual RX sample clock")
            width = manifest["source_format"]["occupied_bandwidth_hz"]/3
            near(selective["band_width_hz"], width, "selective nominal band width")
            for actual, expected in zip(selective["band_centers_hz"], (-width, 0, width)):
                near(actual, expected, "selective band center")
            check(selective["seed"] == manifest["random_seed_roots"]["channel"] ^ 0x3254494154504552,
                  "selective derived seed is not the actual constructor seed")
            phases = selective["initial_phases_radians"]
            check(len(phases) == 3 and all(len(row) == 2 and all(0 <= p < 2*math.pi for p in row)
                                          for row in phases), "selective phase shape/range")
            cutoff = min(width/2, physical_fs*.24)
            k = math.tan(math.pi*cutoff/physical_fs)
            pole = abs((k-1)/(k+1))
            expected_tail = math.ceil(math.log(1e-6)/math.log(pole)) if pole else 0
            check(selective["decay_tail_samples"] == expected_tail, "selective IIR decay extent")
            check(selective["processed_rows"] == manifest["channel_samples"],
                  "selective energy rows do not cover the processed capture")
            check(selective["pre_selective_energy_samples"] > 0 and selective["post_selective_energy_samples"] > 0,
                  "active selective stage omitted accumulated signal energy")
            near(selective["post_selective_energy_samples"], manifest["channel_signal_energy_samples"],
                 "post-selective energy differs from the later unit-modulus RX phase stage")
            check("index 0" in selective["phase_origin"] and "not calendar time zero" in selective["phase_origin"],
                  "selective phases have an ambiguous time origin")
            selective_outputs.append((destination, manifest, selective))
        check(all(entry[2]["initial_phases_radians"] == selective_outputs[0][2]["initial_phases_radians"]
                  for entry in selective_outputs[1:]), "changing active depth changed seeded initial phases")
        check(all((entry[0]/"gram.c128").read_bytes() == (selective_outputs[0][0]/"gram.c128").read_bytes()
                  for entry in selective_outputs[1:]), "hidden selective realization changed public receiver templates")
        # A fresh noisy pass and different read chunks reproduce the same
        # signal-only instrumentation and the same noiseless projections.
        chunk_output = root/"selective-each-65-noisy-chunk"
        invoke("--mode", "export-stats", *COMMON, "--selective-perturbation", "each-65",
               "--selective-model", "legacy-allpass",
               "--spread-doppler-hz", ".2", "--margin", "3", "--ray-gaussian-width-hz", ".5",
               "--block-seconds", "4", "--stats-noisy", "--chunk", "17", "--output", str(chunk_output))
        chunk_manifest = json.loads((chunk_output/"manifest.json").read_text())
        check(chunk_manifest["path_truth_simulation_only"]["selective_perturbation"] == selective_outputs[-1][2],
              "selective instrumentation changed with read chunks or the fresh noisy pass")
        for filename in ("gram.c128", "mean.c128", "signal_energy.f64"):
            check((chunk_output/filename).read_bytes() == (selective_outputs[-1][0]/filename).read_bytes(),
                  "selective signal changed with read chunks/noisy second pass: "+filename)
        # Selected depths with D=0 and Off share the exact inactive sample path,
        # including independently generated thermal noise and no filter tail.
        bypass_outputs = []
        for mode in ("off", "each-65"):
            destination = root/("selective-bypass-"+mode)
            invoke("--mode", "export-stats", *COMMON, "--selective-perturbation", mode,
                   "--selective-model", "legacy-allpass",
                   "--spread-doppler-hz", "0", "--stats-noisy", "--block-seconds", "4",
                   "--output", str(destination))
            manifest = json.loads((destination/"manifest.json").read_text())
            check(manifest["path_truth_simulation_only"]["selective_perturbation"] == {"enabled": False},
                  "inactive selective stage retained realization metadata")
            bypass_outputs.append(destination)
        for filename in ("gram.c128", "mean.c128", "signal_energy.f64", "observation.c128", "received_energy.f64"):
            check((bypass_outputs[0]/filename).read_bytes() == (bypass_outputs[1]/filename).read_bytes(),
                  "zero-Doppler selective bypass changed actual sampled/noisy values: "+filename)
        # Equalized tail support creates the same total calendar/noise rows
        # across historical and corrected model identities. Null captures
        # isolate the indexed noise stream from model-dependent waveform energy.
        paired_nulls = []
        for model in ("legacy-allpass", "dispersive-dft"):
            destination = root/("paired-tail-"+model)
            invoke("--mode", "export-stats", *COMMON, "--control", "null",
                   "--spread-preset", "none", "--spread-doppler-hz", ".2",
                   "--selective-perturbation", "each-1", "--selective-model", model,
                   "--selective-scale-hz", ".1", "--selective-tail-floor-seconds", "8",
                   "--stats-noisy", "--block-seconds", "4", "--output", str(destination))
            manifest = json.loads((destination/"manifest.json").read_text())
            selective = manifest["path_truth_simulation_only"]["selective_perturbation"]
            check(selective["model"] == ("legacy-allpass-v1" if model == "legacy-allpass" else "dispersive-dft-v1"),
                  "sampled manifest lost the selected selective model")
            check(selective["channel_tail_samples"] >= selective["decay_tail_samples"],
                  "common tail floor truncated selected filter response")
            check(selective["total_channel_samples"] == manifest["channel_samples"],
                  "selective report and public capture calendar disagree")
            endpoint = manifest["observation_endpoint_report"]
            check(endpoint["natural_complete_samples"] + endpoint["public_calendar_padding_samples"] ==
                  manifest["sample_count"] == endpoint["processed_samples"],
                  "paired calendar truncated the selected model's natural complete endpoint")
            paired_nulls.append(destination)
        manifest_a = json.loads((paired_nulls[0]/"manifest.json").read_text())
        manifest_b = json.loads((paired_nulls[1]/"manifest.json").read_text())
        check(manifest_a["channel_samples"] == manifest_b["channel_samples"],
              "paired selective models do not have equal total sample calendars")
        check((paired_nulls[0]/"observation.c128").read_bytes() ==
              (paired_nulls[1]/"observation.c128").read_bytes(),
              "paired model null captures did not reuse identical indexed noise")
        too_large = root/"selective-memory-rejected"
        rejected = invoke("--mode", "export-stats", *COMMON,
                          "--selective-scale-hz", ".0001", "--memory-mib", "1",
                          "--output", str(too_large), ok=False)
        check("public channel workspace exceeds --memory-mib before channel allocation" in rejected.stderr
              and not too_large.exists(),
              "dispersive history escaped the memory budget or created partial output before rejection")
        # CLI path seeds change the actual propagated samples while public
        # templates and independent endpoint draws stay fixed.
        path_outputs = []
        for name, seed in (("path-a", "101"), ("path-b", "102")):
            destination = root/name
            invoke("--mode", "export-stats", *COMMON, "--phase-diffusion", "0",
                   "--spread-preset", "none", "--selective-perturbation", "off",
                   "--ray-coherence-seconds", "3", "--diurnal-phase-radians", ".1",
                   "--diurnal-period-seconds", "100", "--path-seed", seed,
                   "--block-seconds", "4", "--output", str(destination))
            manifest = json.loads((destination/"manifest.json").read_text())
            check(manifest["random_seed_roots"]["path"] == int(seed) and
                  manifest["random_seed_roots"]["path_explicit"], "export path seed provenance lost")
            near(manifest["channel_model"]["path_evolution"]["ray_coherence_seconds"], 3,
                 "export path configuration differs from actual input")
            phase = manifest["path_truth_simulation_only"]["diurnal_initial_phase_radians"]
            check(0 <= phase <= 2*math.pi, "seeded diurnal truth missing or invalid")
            path_outputs.append((destination, manifest))
        check(path_outputs[0][1]["endpoint_truth"] == path_outputs[1][1]["endpoint_truth"],
              "path seed changed independent endpoint truth")
        check((path_outputs[0][0]/"gram.c128").read_bytes() == (path_outputs[1][0]/"gram.c128").read_bytes(),
              "hidden path seed changed public templates")
        check((path_outputs[0][0]/"mean.c128").read_bytes() != (path_outputs[1][0]/"mean.c128").read_bytes(),
              "CLI path seed did not reach actual propagated samples")
        spread_outputs = []
        for name, extra in (("spread-legacy", ()),
                            ("spread-same", ("--spread-delay-ms", ".5", "--spread-doppler-hz", ".1")),
                            ("spread-changed", ("--spread-doppler-hz", ".2"))):
            destination = root/name
            invoke("--mode", "export-stats", *COMMON, "--phase-diffusion", "0", "--margin", "3",
                   "--basis", "two-ray", "--tap-delay-ms", ".05", "--tap-doppler-hz", ".02",
                   "--block-seconds", "4", "--output", str(destination), *extra)
            manifest = json.loads((destination/"manifest.json").read_text())
            near(manifest["channel_model"]["spread_inputs"]["applied_margin"], 3, "export omitted spread margin")
            check(manifest["channel_model"]["spread_inputs"]["selected_preset"] == "hf-best",
                  "export override lost selected preset")
            for candidate in manifest["candidates"]:
                if len(candidate["taps"]) > 1:
                    near(candidate["taps"][1]["delay_seconds"], .00005, "physical override changed receive delay basis")
                    near(candidate["taps"][1]["doppler_hz"], .02, "physical override changed receive Doppler basis")
            spread_outputs.append(destination)
        for filename in ("gram.c128", "mean.c128", "signal_energy.f64"):
            check((spread_outputs[0]/filename).read_bytes() == (spread_outputs[1]/filename).read_bytes(),
                  "explicit matching spread override changed legacy waveform: "+filename)
        check((spread_outputs[0]/"gram.c128").read_bytes() == (spread_outputs[2]/"gram.c128").read_bytes(),
              "physical spread override changed public receive templates")
        check((spread_outputs[0]/"mean.c128").read_bytes() != (spread_outputs[2]/"mean.c128").read_bytes(),
              "physical Doppler override did not reach propagated samples")
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
