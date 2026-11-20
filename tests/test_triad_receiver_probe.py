"""Bounded subprocess contract checks for the production native receiver probe."""
import json
import re
import subprocess
import sys
from pathlib import Path


def main(executable):
    def run(command, *args, code=0, default_allocation=False, parse_error=None, deadline_ms=12000):
        allocation = [] if default_allocation else ["--workers", "1", "--slot-owners", "1"]
        completed = subprocess.run(
            [str(executable), command, *allocation, "--deadline-ms", str(deadline_ms), *args],
            check=False, capture_output=True, text=True, timeout=18,
        )
        receipt = json.loads(completed.stdout)
        assert completed.returncode == code, (command, receipt["reason"], completed.stderr)
        assert receipt["schema"] == "datapump.triad.native-probe.v1"
        if parse_error is not None:
            assert receipt["command"] is None
            assert receipt["status"] == "error"
            assert parse_error in receipt["reason"]
            return receipt
        assert receipt["command"] == command
        assert receipt["identity"]["implementation"] == "native-production-receiver"
        assert receipt["identity"]["cpu_fft_kernel"] in ("avx2", "scalar")
        assert re.fullmatch("[0-9a-f]{64}", receipt["identity"]["source_manifest_sha256"])
        return receipt

    description = run("describe")
    longest_admission = run("admission", "--max-samples", "1", deadline_ms=5_400_000, code=2)
    assert longest_admission["budget"]["deadline_ms"] == 5_400_000
    assert longest_admission["reason"] == "Admission capture exceeds the declared probe sample budget"
    run("admission", deadline_ms=5_400_001, code=1, parse_error="bounded positive sample, time and tile budgets")
    assert run("describe", deadline_ms=3_600_000)["budget"]["deadline_ms"] == 3_600_000
    run("message", deadline_ms=3_600_001, code=1, parse_error="bounded positive sample, time and tile budgets")
    assert description["allocation"]["workers"] == 1
    assert description["allocation"]["slot_owners"] == 1
    assert description["result"] == {"available": True, "actual_device_work": False}
    assert not description["phases"]
    assert description["identity"]["requested_native_coarse_precision"] == "automatic"
    assert description["identity"]["requested_tensor_coarse_math"] == "fft"
    assert description["identity"]["requested_gpu_fp32_minimum"] is False
    if description["identity"].get("configured_vulkan_backend") is True:
        # Vulkan describe serializes the shared BulkReceipt without doing DSP;
        # the CUDA matrix counters must therefore be present and zero.
        serialized = subprocess.run(
            [str(executable), "describe", "--backend", "vulkan", "--workers", "1",
             "--slot-owners", "1", "--deadline-ms", "12000"],
            check=False, capture_output=True, text=True, timeout=18,
        )
        vulkan_description = json.loads(serialized.stdout)
        assert serialized.returncode in (0, 2), vulkan_description
        bulk = vulkan_description.get("identity", {}).get("backend_receipt")
        if isinstance(bulk, dict):
            for field in ("cuda_matrix_prepared_b_groups", "cuda_matrix_prepared_b_bytes",
                          "cuda_matrix_prepared_b_values", "cuda_matrix_prepared_b_uses",
                          "cuda_matrix_lt_hmma_calls", "cuda_matrix_lt_imma_calls",
                          "cuda_matrix_unwitnessed_calls"):
                assert type(bulk.get(field)) is int and bulk[field] == 0, (field, bulk.get(field))
    forced = run("describe", "--native-coarse-precision", "fp64")
    assert forced["identity"]["requested_native_coarse_precision"] == "fp64"
    assert forced["identity"]["receiver_arithmetic"] == "native_fp64_coarse_fft_state_and_admission"
    q15 = run("describe", "--native-coarse-precision", "q15")
    assert q15["identity"]["requested_native_coarse_precision"] == "q15"
    assert q15["identity"]["receiver_arithmetic"] == "native_q15_spectral_products_fp32_fft_fp64_score_and_state"
    fp32 = run("describe", "--native-coarse-precision", "fp32")
    assert fp32["identity"]["requested_native_coarse_precision"] == "fp32"
    assert fp32["identity"]["receiver_arithmetic"] == "native_fp32_coarse_products_with_fp64_guards"
    run("describe", "--native-coarse-precision", "invalid", code=1,
        parse_error="--native-coarse-precision expects automatic, fp32, fp64 or q15")
    run("describe", "--backend", "vulkan", "--native-coarse-precision", "fp64", code=1,
        parse_error="Native coarse precision overrides require --backend cpu")
    tensor_on_cpu = run("describe", "--tensor-coarse-math", "int8", code=2)
    assert tensor_on_cpu["status"] == "unavailable" and not tensor_on_cpu["phases"]
    assert tensor_on_cpu["identity"]["requested_tensor_coarse_math"] == "int8"
    run("describe", "--backend", "vulkan", "--tensor-coarse-math", "int8", code=1,
        parse_error="Vulkan coarse search uses the full FFT")
    run("describe", "--tensor-coarse-math", "fp16-fft", code=1,
        parse_error="fp16-fft requires explicit --backend cuda")
    run("describe", "--backend", "tensor-cpu", "--tensor-coarse-math", "fp16-fft", code=1,
        parse_error="fp16-fft requires explicit --backend cuda")
    run("describe", "--backend", "vulkan", "--tensor-coarse-math", "fp16-fft", code=1,
        parse_error="Vulkan coarse search uses the full FFT")
    run("describe", "--backend", "gpu", "--tensor-coarse-math", "fp16-fft", code=1,
        parse_error="Automatic GPU selection does not accept pinned device, workspace or backend arithmetic overrides")
    run("describe", "--backend", "cpu", "--gpu-fp32-minimum", "on", code=1,
        parse_error="--gpu-fp32-minimum is valid only with --backend gpu")
    run("describe", "--backend", "vulkan", "--gpu-fp32-minimum", "on", code=1,
        parse_error="--gpu-fp32-minimum is valid only with --backend gpu")
    run("describe", "--backend", "gpu", "--gpu-fp32-minimum", "invalid", code=1,
        parse_error="--gpu-fp32-minimum expects on or off")
    automatic_gpu_floor = subprocess.run(
        [str(executable), "describe", "--backend", "gpu", "--gpu-fp32-minimum", "on",
         "--workers", "1", "--slot-owners", "1", "--deadline-ms", "12000"],
        check=False, capture_output=True, text=True, timeout=18,
    )
    automatic_gpu_receipt = json.loads(automatic_gpu_floor.stdout)
    assert automatic_gpu_floor.returncode in (0, 2), automatic_gpu_receipt
    assert automatic_gpu_receipt["identity"]["requested_gpu_fp32_minimum"] is True
    if automatic_gpu_floor.returncode == 0:
        assert automatic_gpu_receipt["result"]["actual_device_work"] is False
    explicit_half_fft = subprocess.run(
        [str(executable), "describe", "--backend", "cuda", "--tensor-coarse-math", "fp16-fft",
         "--workers", "1", "--slot-owners", "1", "--deadline-ms", "12000"],
        check=False, capture_output=True, text=True, timeout=18,
    )
    half_fft_description = json.loads(explicit_half_fft.stdout)
    assert explicit_half_fft.returncode in (0, 2), half_fft_description
    assert half_fft_description["identity"]["requested_tensor_coarse_math"] == "fp16-fft"
    run("describe", "--backend", "vulkan", "--tensor-precision", "fp64", code=1,
        parse_error="Vulkan coarse search uses its native FP32 path")
    run("describe", "--backend", "gpu", "--device-index", "1", code=1,
        parse_error="Automatic GPU selection does not accept pinned device")
    automatic = run("describe", default_allocation=True)["allocation"]
    assert automatic["hardware_threads"] > 0
    assert automatic["workers"] == 2 * automatic["hardware_threads"]

    plan = run("plan")
    assert plan["configuration"]["wrong_key_admission"] == "current"
    strict_plan = run("plan", "--wrong-key-admission", "strict")
    assert strict_plan["configuration"]["wrong_key_admission"] == "strict"
    for name in ("bit_seconds", "slot_plan", "receiver_slot_workspace_bytes", "coarse_full_prior"):
        assert strict_plan["configuration"][name] == plan["configuration"][name]
    run("plan", "--wrong-key-admission", "invalid", code=1,
        parse_error="Wrong Key Admission expects current, guarded, strict, very-strict, extreme or maximum")

    # Keep Rate and both oscillators fixed while the public search grows.
    # Clock-limited arithmetic uses the projected rate, and each score
    # covers all rays for one frequency, arrival and workspace.
    low_rate_options = ("--case", "hf", "--rate", "3.6", "--timing-policy", "broad", "--encrypt", "on",
                        "--base-oscillator", "gpsdo-xo", "--shift-oscillator", "gpsdo-xo",
                        "--rf-hz", "30000000", "--margin", "3", "--helper", "off",
                        "--data-power-w", "0.001995262314968879", "--path-loss-db", "90",
                        "--noise-dbm-hz", "-164", "--added-symbols", "1", "--added-interleave", "off")
    for spread, selective, target, doppler, projection, frequencies, arrivals, rays, score_rows in (
        ("none", "off", "32", "0", 3, 15, 43, 1, 50_310),
        ("hf-worst", "each-65", "32", "0", 500, 15, 7_036, 2, 8_232_120),
        ("none", "off", "-6", "1.5", 8, 6_132, 113, 1, 54_047_448),
    ):
        config = run("plan", *low_rate_options, "--spread-preset", spread, "--selective-perturbation", selective,
                     "--target-cn0", target, "--doppler-hz", doppler)["configuration"]
        prior = config["coarse_full_prior"]
        assert 0 < 2 * config["clock_bound_ppm"] <= .001
        assert prior["projected_sample_rate_hz"] == projection
        assert prior["frequency_points"] == frequencies and prior["arrival_points"] == arrivals
        assert prior["workspaces"] == 78 and prior["rays"] == rays
        assert prior["frequency_points"] * prior["arrival_points"] * prior["workspaces"] == score_rows

    contrast_options = ("--spread-preset", "hf-worst", "--selective-perturbation", "each-65",
                        "--data-if", "497", "--target-cn0", "16", "--cn0", "16", "--seconds", "8")
    full_stress = run("plan", *contrast_options, "--signal-gain-db", "0")["configuration"]
    strong_clean = run("plan", *contrast_options, "--signal-gain-db", "30", "--channel-severity", "0")["configuration"]
    for name in ("bit_seconds", "slot_plan", "quiet_first_sample", "quiet_stop_sample", "coarse_full_prior",
                 "receiver_slot_workspace_bytes", "spread_delay_seconds", "spread_doppler_hz",
                 "selective_slow_depth", "selective_fast_depth", "time_varying_common_doppler_hz"):
        assert strong_clean[name] == full_stress[name], name
    full_link = full_stress["fixed_receiver_link_contrast"]
    clean_link = strong_clean["fixed_receiver_link_contrast"]
    for name in ("receiver_reference_cn0_db_hz", "receiver_noise_variance", "receiver_data_basis_gain",
                 "channel_noise_variance"):
        assert clean_link[name] == full_link[name], name
    for name in ("channel_spread_delay_ms", "channel_spread_doppler_hz", "channel_selective_slow_depth",
                 "channel_selective_fast_depth"):
        assert clean_link[name] == 0 < full_link[name], name
    assert abs(strong_clean["actual_cn0_db_hz"] - full_stress["actual_cn0_db_hz"] - 30) < 1e-12
    run("plan", "--signal-gain-db", "101", code=1,
        parse_error="--signal-gain-db expects -100 to +100 dB")
    run("plan", "--channel-severity", "1.01", code=1,
        parse_error="--channel-severity expects 0 to 1")
    run("plan", "--channel-model", "bogus", code=1,
        parse_error="--channel-model expects gui or itu-disturbed")
    run("plan", "--channel-model", "itu-disturbed", code=1,
        parse_error="ITU disturbed reference requires HF Worst")
    reference_args = ("--case", "hf", "--helper", "off", "--spread-preset", "hf-worst",
                      "--margin", "3", "--selective-perturbation", "each-65", "--seconds", ".1",
                      "--rate", "120", "--rf-hz", "30000000", "--timing-policy", "fixture")
    for command, options in (
        ("plan", ("--backend", "cuda", "--channel-severity", ".5")),
        ("plan", ("--backend", "gpu", "--channel-severity", "0", "--signal-gain-db", "0")),
        ("message", ("--backend", "cuda", "--channel-severity", "0")),
        ("plan", ("--backend", "vulkan", "--channel-severity", "0")),
    ):
        run(command, *reference_args, *options, code=1, parse_error="A fixed receiver link contrast requires")
    run("plan", "--case", "hf", "--helper", "on", "--backend", "gpu", "--margin", "3",
        "--channel-severity", "0", code=1, parse_error="A fixed receiver link contrast requires")
    run("plan", "--case", "hf", "--helper", "off", "--backend", "cuda", "--spread-preset", "none",
        "--margin", "3", "--channel-severity", "0", code=1, parse_error="A fixed receiver link contrast requires")
    run("plan", *reference_args, "--channel-model", "itu-disturbed", "--channel-severity", "1",
        code=1, parse_error="ITU disturbed reference requires HF Worst")
    run("plan", *reference_args, "--backend", "tensor-cpu", "--channel-model", "itu-disturbed",
        code=1, parse_error="ITU disturbed reference requires HF Worst")
    run("plan", *reference_args, "--backend", "gpu", "--channel-model", "itu-disturbed",
        code=1, parse_error="automatic GPU with --require-accelerator on")
    run("plan", "--case", "hf", "--helper", "off", "--spread-preset", "hf-worst", "--margin", "3",
        "--rf-hz", "30000001", "--channel-model", "itu-disturbed", code=1,
        parse_error="ITU disturbed reference requires HF Worst")
    ordinary_reference = run("plan", *reference_args)["configuration"]
    gaussian_reference = run("plan", *reference_args, "--channel-model", "itu-disturbed")["configuration"]
    assert ordinary_reference["channel_model"] == "gui"
    assert gaussian_reference["channel_model"] == "itu-disturbed"
    for name in ("bit_seconds", "slot_plan", "quiet_first_sample", "quiet_stop_sample",
                 "actual_cn0_db_hz", "arrival_lower_seconds", "arrival_upper_seconds", "clock_bound_ppm",
                 "frequency_bound_hz", "spread_delay_seconds", "spread_doppler_hz",
                 "receiver_random_doppler_width_hz", "time_varying_atmospheric_hz", "time_varying_common_doppler_hz",
                 "coarse_full_prior", "receiver_slot_workspace_bytes",
                 "channel_selective_modulation_rate_hz", "selective_slow_depth", "selective_fast_depth"):
        assert gaussian_reference[name] == ordinary_reference[name], name
    assert gaussian_reference["channel_spread_delay_ms"] == 7
    assert gaussian_reference["channel_spread_doppler_hz"] == 0
    assert gaussian_reference["channel_random_doppler_width_hz"] == 30
    assert gaussian_reference["spread_doppler_hz"] == 90
    assert gaussian_reference["receiver_random_doppler_width_hz"] == 0
    assert gaussian_reference["channel_bounded_spread_knot_seconds"] == 0
    assert gaussian_reference["channel_common_doppler_wander_hz"] == 0
    reference = gaussian_reference["channel_reference"]
    assert reference["law"] == "fixed-grid-gaussian-v1"
    assert reference["tap_delays_ms"] == [0, 7] and reference["mean_tap_powers"] == [.5, .5]
    assert reference["tap_doppler_width_2sigma_hz"] == 30 and reference["tap_center_separation_hz"] == 0
    assert reference["receiver_model"] == "unchanged-gui-prior-intentionally-mismatched"
    assert reference["additional_selective_am"] and not reference["capture_allowance_is_hard_bandlimit"]
    no_selective_args = list(reference_args)
    no_selective_args[no_selective_args.index("--selective-perturbation") + 1] = "off"
    pure_reference = run("plan", *no_selective_args, "--channel-model", "itu-disturbed")["configuration"]
    assert not pure_reference["channel_reference"]["additional_selective_am"]

    # Exercise actual native channel generation without requiring a completed
    # expensive receiver search.  The explicit local lease admits the capture
    # but need not admit full scoring; either a finished score or a reported
    # lease/deadline stop is acceptable for this waveform-generation check.
    def reference_capture(*extra, memory_limit=83886080, workers=1):
        proc = subprocess.run([str(executable), "admission", "--workers", str(workers), "--slot-owners", "1",
                               "--deadline-ms", "12000", "--memory-limit-bytes", str(memory_limit),
                               *reference_args, *extra], check=False, capture_output=True, text=True, timeout=18)
        receipt = json.loads(proc.stdout)
        assert proc.returncode in (0, 2), receipt
        assert receipt["observation"] is not None, receipt
        assert any(row["name"] == "simulator" for row in receipt["phases"]), receipt
        return receipt

    faded = reference_capture("--channel-model", "itu-disturbed")
    faded_again = reference_capture("--channel-model", "itu-disturbed")
    stable = reference_capture("--channel-severity", "0")

    # A read size never changes the capture, so larger reads must not displace
    # a lent simulator worker: helper stacks are admitted first, and the two
    # read buffers grow only into what they leave. Place a two-worker limit
    # where two full-size read buffers would fit beside the capture but not
    # also beside a helper stack.
    def generation(receipt):
        return next(row for row in receipt["phases"] if row["name"] == "simulator")["work"]
    row_bytes, least_rows, full_rows = 17, 4096, 65536
    alone = generation(stable)
    assert alone["generation_helper_workers"] == 0, alone
    stack = alone["generation_stack_bytes_per_helper"]
    fixed = alone["generation_preflight_reserved_bytes"] - 2 * alone["generation_read_rows"] * row_bytes
    band_limit = fixed + stack + 2 * least_rows * row_bytes + 2**20
    lent_capture = reference_capture("--channel-severity", "0", memory_limit=band_limit, workers=2)
    lent = generation(lent_capture)
    assert lent_capture["observation"]["input_sha256"] == stable["observation"]["input_sha256"]
    assert lent["generation_helper_workers"] == 1, (band_limit, lent)
    assert least_rows <= lent["generation_read_rows"] < full_rows, (band_limit, lent)
    lent_fixed = (lent["generation_preflight_reserved_bytes"] - stack -
                  2 * lent["generation_read_rows"] * row_bytes)
    assert band_limit - lent_fixed < stack + 2 * full_rows * row_bytes, (band_limit, lent_fixed, lent)
    assert lent["generation_preflight_reserved_bytes"] <= band_limit, (band_limit, lent)
    for backend, discovery in (("cuda", explicit_half_fft), ("gpu", automatic_gpu_floor)):
        device_args = ("--backend", backend, "--require-accelerator", "on")
        options = (*device_args, "--channel-severity", "0")
        gaussian_options = (*device_args, "--channel-model", "itu-disturbed")
        planned = run("plan", *reference_args, *options, code=discovery.returncode)
        gaussian_plan = run("plan", *reference_args, *gaussian_options, code=discovery.returncode)
        if discovery.returncode == 0:
            # A reachable device runtime is part of the process footprint
            # that the lease charges before the canonical capture.
            device_limit = 268435456
            device = reference_capture(*options, memory_limit=device_limit)
            assert device["observation"]["input_sha256"] == stable["observation"]["input_sha256"]
            assert device["configuration"]["fixed_receiver_link_contrast"] == stable["configuration"]["fixed_receiver_link_contrast"]
            gaussian_capture = reference_capture(*gaussian_options, memory_limit=device_limit)
            assert gaussian_capture["observation"]["input_sha256"] == faded["observation"]["input_sha256"]
            assert gaussian_capture["configuration"]["channel_reference"] == faded["configuration"]["channel_reference"]
            for field in ("spread_doppler_hz", "spread_delay_seconds", "time_varying_common_doppler_hz",
                          "frequency_bound_hz", "actual_cn0_db_hz", "public_window_samples"):
                assert planned["configuration"][field] == stable["configuration"][field], (backend, field)
                assert gaussian_plan["configuration"][field] == gaussian_reference[field], (backend, field)
        else:
            assert planned["status"] == "unavailable"
            assert gaussian_plan["status"] == "unavailable" and not gaussian_plan["phases"]
    default_capture = reference_capture()
    explicit_default = reference_capture("--channel-model", "gui")
    assert faded["observation"]["input_sha256"] == faded_again["observation"]["input_sha256"]
    assert default_capture["observation"]["input_sha256"] == explicit_default["observation"]["input_sha256"]
    assert faded["observation"]["input_sha256"] != default_capture["observation"]["input_sha256"]
    assert faded["observation"]["bounded_spread_realization"] is None
    assert faded["observation"]["common_doppler_realization"] is None
    assert default_capture["observation"]["bounded_spread_realization"] is not None
    assert default_capture["observation"]["common_doppler_realization"] is not None
    assert faded["configuration"]["channel_reference"]["path_seed_root"] == faded["observation"]["seed"]
    run("admission", "--wrong-key-admission", "strict", code=1,
        parse_error="Admission diagnostics score Current once")
    run("admission", "--interferer-cn0", "20", code=1,
        parse_error="The admission interferer requires --encrypt on")
    interferer_args = ("--slot-index", "1", "--encrypt", "on", "--stratum", "noise",
        "--interferer-cn0", "20", "--interferer-seconds", ".1", "--interferer-seed", "1234",
        "--interferer-bit", "1", "--interferer-uniform-window", "on", "--interferer-frequency-offset-hz", "3")
    run("admission", *interferer_args, "--interferer-offset-seconds", "0", code=1,
        parse_error="Choose either uniform-window placement or a fixed interferer offset")
    interference = run("admission", *interferer_args)
    itx = interference["configuration"]["interferer"]
    iq = interference["observation"]
    assert re.fullmatch("[0-9a-f]{64}", itx["key_fingerprint_sha256"])
    assert itx["placement"] == "uniform_window_intersection" and not itx["helper_enabled"]
    assert itx["frequency_offset_hz"] == 3 and itx["configured_carrier_hz"] == 503
    assert itx["direct_center_hz_excluding_phase_noise_fhss_and_echo"] == 3
    assert itx["uniform_origin_count"] == (iq["samples"] + itx["nominal_support_stop_sample"] -
                                            itx["nominal_support_first_sample"] - 1)
    overlap = min(iq["stop_sample"], itx["nominal_support_stop_sample"]) - max(iq["first_sample"], itx["nominal_support_first_sample"])
    assert overlap > 0 and itx["nominal_window_overlap_seconds"] == overlap / 1500
    assert [p["name"] for p in interference["phases"]].count("native_slot") == 1
    result = interference["result"]
    assert result["one_native_score_for_all_policies"] and result["complete_public_slot"]
    assert not result["complete_receiver_service"] and result["interferer_present"] and not result["wanted_present"]
    policies = result["policies"]
    policy_names = ("current", "guarded", "strict", "very-strict", "extreme", "maximum")
    assert set(policies) == set(policy_names)
    for weaker, stronger in zip(policy_names, policy_names[1:]):
        assert (policies[stronger]["decision"] == "data") <= (policies[weaker]["decision"] == "data")
    for policy, multiplier in (("very-strict", 1.5), ("extreme", 2.75), ("maximum", 4.0)):
        assert policies[policy]["required_log_gap"] == multiplier * policies["strict"]["required_log_gap"]
    assert all((p["decision"] == "quiet") == (policies["current"]["decision"] == "quiet") for p in policies.values())
    for policy in policy_names[1:]:
        if policies[policy]["policy_rejected"]:
            assert policies[policy]["bit"] is None and policies[policy]["decision"] == "ambiguous"
    assert plan["result"]["planned"] and plan["observation"] is None
    assert plan["configuration"]["recurring_horizon"]["slots"] == 24
    assert plan["configuration"]["bit_seconds"] > 0
    stream_plan = plan["configuration"]["stream_plan"]
    assert stream_plan["minimum_backlog_slots"] == 2 * stream_plan["message_slots"] == 10
    assert stream_plan["spool_bytes"] == stream_plan["generated_samples"] * 17
    assert stream_plan["maximum_startup_carryover_slots"] == 1
    assert [row["name"] for row in plan["phases"]] == ["profile_preparation"]
    assert plan["configuration"]["coarse_full_prior"]["route"] == "lag_groups"
    assert plan["configuration"]["coarse_full_prior"]["correlation_fft_rows"] == 0
    assert plan["configuration"]["coarse_full_prior"]["bulk_frequency_admission"] is None
    large_options = ("--seconds", "670", "--max-samples", "1200000")
    large_auto = run("plan", *large_options)["configuration"]
    large_fp32 = run("plan", *large_options, "--native-coarse-precision", "fp32")["configuration"]
    assert large_auto["coarse_full_prior"]["route"] == "lag_groups"
    assert large_auto["coarse_full_prior"]["correlation_fft_rows"] == 0
    assert large_fp32["coarse_full_prior"]["correlation_fft_rows"] == 131072
    explicit_large_q15 = run("plan", *large_options, "--native-coarse-precision", "q15")
    large_q15 = explicit_large_q15["configuration"]
    if description["identity"]["cpu_fft_kernel"] == "avx2":
        assert large_q15["coarse_full_prior"]["correlation_fft_rows"] == 131072
        assert large_q15["coarse_full_prior"]["workspace_reserved_bytes"] < large_fp32["coarse_full_prior"]["workspace_reserved_bytes"]
    else:
        assert large_q15["coarse_full_prior"] is None and large_q15["receiver_slot_workspace_bytes"] is None
        assert "Q15 coarse products require" in large_q15["coarse_full_prior_reason"]
    assert explicit_large_q15["observation"] is None
    lag_options = ("--rate", "1", "--added-symbols", "2", "--timing-policy", "broad",
                   "--base-oscillator", "crystal", "--max-samples", "900000")
    lag = run("plan", *lag_options)
    assert lag["observation"] is None and lag["result"]["actual_device_work"] is False
    assert [row["name"] for row in lag["phases"]] == ["profile_preparation"]
    lag_plan = lag["configuration"]["coarse_full_prior"]
    assert lag_plan["route"] == "lag_groups" and lag_plan["frequency_groups"] == 1
    assert lag_plan["projected_sample_rate_hz"] == 5 and lag_plan["template_sampling_step"] == 60
    assert lag_plan["frequency_points"] == 213 and lag_plan["arrival_points"] == 71
    assert 16 <= lag_plan["projected_template_rows"] <= 8192
    assert lag_plan["workspaces"] == 78 and lag_plan["rays"] == 1
    mapped = run("slot", *lag_options, "--cn0", "0", "--coarse-tiles", "1", code=2)
    mapped_prepare = next(row["work"] for row in mapped["phases"] if row["name"] == "coarse_prepare")
    mapped_tile = next(row["work"] for row in mapped["phases"] if row["name"] == "coarse_tiles")
    assert mapped_prepare["route"] == "lag_groups"
    assert mapped_prepare["bulk_frequency_admission"] is None
    assert mapped_prepare["native_fallback_reason"] == ""
    assert mapped_prepare["mapped_fidelity"]["projected_columns"] > 0
    assert mapped_tile["search"]["score_rows"] == lag_plan["frequency_points"] * lag_plan["arrival_points"]
    assert mapped["result"]["acquisition"]["completed_tiles"] == 1
    assert mapped["result"]["acquisition"]["selection_completed"] is False
    bounded = run("slot", "--max-samples", "1", code=2)
    assert bounded["status"] == "incomplete" and bounded["observation"] is None
    assert not bounded["configuration"]["slot_plan"]["fits_sample_budget"]
    assert [row["name"] for row in bounded["phases"]] == ["profile_preparation"]
    spool_bounded = run("message", "--max-spool-bytes", "17", code=2)
    assert spool_bounded["observation"] is None
    assert not spool_bounded["configuration"]["stream_plan"]["fits_spool_budget"]
    wrong_backend = run("describe", "--require-accelerator", "on", code=2)
    assert wrong_backend["status"] == "unavailable" and not wrong_backend["phases"]

    partial = run("slot", "--coarse-tiles", "4", "--rf-hz", "1000000",
                  "--shift-oscillator", "ic-7100", code=2)
    assert partial["status"] == "incomplete"
    assert [row["work"]["step_tiles"] for row in partial["phases"] if row["name"] == "coarse_tiles"] == [1, 1, 2]
    acquisition = partial["result"]["acquisition"]
    assert acquisition["completed_tiles"] == 4
    assert acquisition["remaining_tiles"] == acquisition["planned_tiles"] - 4 > 0
    assert acquisition["selection_completed"] is False
    assert not any(row["name"] in ("coarse_selection", "fine_work") for row in partial["phases"])

    helper_options = ("--case", "hf", "--rate", "120", "--seconds", ".1", "--max-samples", "600000",
                      "--rf-hz", "1000000", "--shift-oscillator", "ic-7100", "--coarse-tiles", "1")
    broad = run("slot", *helper_options, code=2)
    assert broad["configuration"]["helper_enabled"] and broad["configuration"]["time_varying_channel"]
    phases = [row for row in broad["phases"] if row["name"] == "helper"]
    assert len(phases) == 1
    assert phases[0]["wall_seconds"] == phases[0]["process_cpu_seconds"] == 0
    assert phases[0]["work"] == {
        "reason": "common_motion_requires_full_data_frequency_prior", "narrowed": False,
        "prefix_samples": 0, "onset_apertures": 0, "workspace_peak_bytes": 0,
        "retained_intervals": 0, "observation_seconds": 0,
    }
    full_grid = run("plan", *helper_options)["configuration"]["coarse_full_prior"]
    prepared = next(row["work"] for row in broad["phases"] if row["name"] == "coarse_prepare")
    assert prepared["frequency_points"] == full_grid["frequency_points"]
    assert broad["result"]["acquisition"]["completed_tiles"] == 1
    for shift in ("0", ".5"):
        ordinary = run("slot", *helper_options, "--spread-preset", "none", "--doppler-hz", shift, code=2)
        assert ordinary["configuration"]["time_varying_channel"] == (shift != "0")
        helper = next(row for row in ordinary["phases"] if row["name"] == "helper")
        assert helper["work"]["reason"] != phases[0]["work"]["reason"]
        assert helper["wall_seconds"] > 0 and helper["work"]["prefix_samples"] > 0
        assert helper["work"]["workspace_peak_bytes"] > 0

    continuation = run("continuation", "--stratum", "active")
    phases = [row for row in continuation["phases"] if row["name"] in ("fine_prefix", "fine_continuation")]
    assert [row["represented_seconds"] for row in phases] == [2, 1, 1, 2]
    assert [row["work"]["checkpoint_seconds"] for row in phases] == [2, 3, 4, 6]
    assert [row["work"]["processed_rows"] for row in phases] == [3000, 4500, 6000, 9000]
    assert len({row["work"]["input_sha256"] for row in phases}) == 1
    assert continuation["result"]["checkpoints_complete"] and continuation["result"]["continued_cache_fill"]
    assert continuation["result"]["coordinate_scope"] == "complete_public_selection"
    assert continuation["result"]["receiver_decision_eligible"] is False

    work = run("continuation", "--work-only", "on", "--stratum", "erasure", "--slot-index", "23",
               "--sample-rate", "6005", "--data-if", "1725", "--max-samples", "6004")
    assert work["observation"]["first_sample"] > work["observation"]["generated_samples"] == 6004
    assert work["result"]["work_complete"] and work["result"]["explicit_erasure_covered"]
    assert work["result"]["receiver_decision_eligible"] is False
    measured = next(row["work"] for row in work["phases"] if row["name"] == "fine_work")
    assert measured["block_rows"] == 1501 and measured["segment_rows"] == 3002

    for stratum, expected in (("active", "data"), ("noise", "quiet")):
        receipt = run("regression", "--stratum", stratum, "--units", "2")
        slots = [row for row in receipt["phases"] if row["name"] == "native_slot"]
        assert [row["unit_index"] for row in slots] == [0, 1]
        assert all(row["work"]["decision"] == expected and row["work"]["complete"] for row in slots)
        assert slots[0]["work"]["work_identity_sha256"] == slots[1]["work"]["work_identity_sha256"]
        assert sum(row["name"] == "audio_frontend" for row in receipt["phases"]) == 2
        if stratum == "active":
            # The sequential control scores the same slot without overlap.
            control = run("regression", "--stratum", stratum, "--units", "1", "--helper-overlap", "off")
            unit = next(row["work"] for row in control["phases"] if row["name"] == "native_slot")
            assert unit["helper_overlapped"] is False and unit["decision"] == expected
            assert unit["work_identity_sha256"] == slots[0]["work"]["work_identity_sha256"]
    run("slot", "--helper-overlap", "off", code=1,
        parse_error="The Helper overlap lease applies only to regression")

    codec = run("codec", "--encrypt", "on")
    assert codec["result"]["decoded_after_two_quiet"]
    assert codec["result"]["marker_intervals"] > 1
    assert codec["result"]["complete_receiver_service"] is False

    stream = run("message", "--max-samples", "600000", "--slots", "5", "--seconds", ".1",
                 "--rate", "120", "--shift-oscillator", "crystal", "--rf-hz", "200000",
                 "--seed", "74207", "--cn0", "9", "--wrong-key-admission", "strict")
    result = stream["result"]
    assert result["message_and_physical_quiet_verified"] and result["eof_control_verified"]
    observation = stream["observation"]
    assert observation["retained_ram_rows"] == 0
    assert observation["spool_bytes"] == observation["samples"] * 17
    assert result["stream"]["verified_input_sha256"] == observation["input_sha256"]
    assert result["stream"]["work_complete"]
    reserved = observation["spool_residency_reserved_bytes"]
    if observation["spool_backing_kind"] == "known_disk":
        assert reserved == 0
    else:
        assert observation["spool_backing_kind"] in ("memory_filesystem", "unknown")
        assert reserved >= observation["spool_bytes"]
    actual = result["stream"]
    assert actual["receiver_memory_limit_bytes"] + reserved == stream["allocation"]["memory_limit_bytes"]
    assert actual["receiver_peak_reserved_bytes"] + reserved == actual["peak_reserved_bytes"]
    assert actual["peak_reserved_bytes"] <= stream["allocation"]["memory_limit_bytes"]
    generation = next(row for row in stream["phases"] if row["name"] == "simulator")
    assert reserved <= generation["work"]["generation_preflight_reserved_bytes"] <= stream["allocation"]["memory_limit_bytes"]
    rows = result["stream"]["slots"]
    assert [row["decision"] for row in rows] == ["data"] * 3 + ["quiet"] * 2
    assert all(row["wrong_key_admission"] == "strict" and not row["wrong_key_rejected"] for row in rows)
    assert all(row["wrong_key_required_log_separation"] > 0 for row in rows[:3])
    assert all(row["wrong_key_required_log_separation"] == 0 for row in rows[3:])
    assert all(row["coarse_precision_reason"] == "exact-one-arrival-chirp-z" and
               row["coarse_frequency_points"] == 1794 and row["coarse_workspaces"] == 26 and
               row["coarse_arrival_points"] == 1 for row in rows)
    assert [row["symbol_ordinal"] for row in rows] == list(range(rows[0]["symbol_ordinal"], rows[0]["symbol_ordinal"] + 5))
    assert [row["bit"] for row in rows[:3]] == [0, 0, 1]
    assert all(row["commit"]["input_stop"] >= row["stop_sample"] for row in rows)
    assert result["stream"]["controller_cv_wait_seconds"] >= 0
    service = next(row for row in stream["phases"] if row["name"] == "receiver_service")
    overhead = next(row for row in stream["phases"] if row["name"] == "controller_service")
    assert 0 <= overhead["wall_seconds"] <= service["wall_seconds"]
    assert overhead["work"]["may_overlap_slot_dsp"]
    assert overhead["work"]["spool_read_and_verification_seconds"] > 0


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve())
