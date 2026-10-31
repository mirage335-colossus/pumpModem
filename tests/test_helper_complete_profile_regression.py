#!/usr/bin/env python3
"""Matched complete-stream regression for pooled Helper inversion profiles."""
import math
import subprocess
import sys


binary = sys.argv[1]
seed = "1930000001"


def run(duration, path_loss, mode, boundary):
    command = [binary, duration, str(path_loss), seed, mode, "tone", "2", "hf", boundary]
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=120)
    if result.returncode:
        raise RuntimeError(f"{command}: {result.stderr[-2000:]}")
    summaries = []
    for line in result.stdout.splitlines():
        parts = line.split()
        if parts and parts[0] == "helper_complete_study":
            summaries.append(dict(item.split("=", 1) for item in parts[1:] if "=" in item))
    if len(summaries) != 1:
        raise AssertionError(f"{command}: expected one study summary, got {len(summaries)}")
    return summaries[0]


def integer(row, field):
    return int(row[field])


def number(row, field):
    return float(row[field])


def check_capture(row, duration):
    expected = 960000 if duration == "full" else 96000
    if integer(row, "processed_captured_samples") != expected:
        raise AssertionError(f"{duration}: captured input count changed")
    output = integer(row, "output_samples")
    if not expected - 1000 <= output <= expected + 1000:
        raise AssertionError(f"{duration}: output count outside resampling range: {output}")
    for field in ("timestamp_failures", "timestamp_unavailable",
                  "reference_mismatches", "missing_samples"):
        if integer(row, field):
            raise AssertionError(f"{duration}: {field}={row[field]}")


def check_clean_authority(row, duration):
    check_capture(row, duration)
    output = integer(row, "output_samples")
    for field in ("phase_applied", "clock_applied"):
        if integer(row, field) != output:
            raise AssertionError(f"{duration}: {field} did not cover every captured output")


def check_frozen_frequency_equal(nominal, profiled):
    for field in ("frequency36_available", "frequency36_error_hz",
                  "frequency36_conditional_half_width_hz", "frequency36_interval_covered",
                  "frequency36_thermal_sigma_hz", "frequency36_process_sigma_hz",
                  "frequency36_model_half_width_hz"):
        if nominal[field] != profiled[field]:
            raise AssertionError(f"clean profile changed frozen frequency {field}: "
                                 f"{nominal[field]} != {profiled[field]}")


def check_profile_gain(nominal, profiled, duration):
    if integer(profiled, "profile_selected_boundaries") <= 0:
        raise AssertionError(f"{duration}: pooled profile selected no boundaries")
    if integer(profiled, "physical_schedule_mismatches") >= integer(nominal, "physical_schedule_mismatches"):
        raise AssertionError(f"{duration}: pooled profile did not improve physical schedule mismatches")


full_nominal = run("full", 180, "clean", "nominal")
full_profile = run("full", 180, "clean", "pooled-inversion")
for row in (full_nominal, full_profile):
    check_clean_authority(row, "full")
check_frozen_frequency_equal(full_nominal, full_profile)
# Relative source-time qualification is independent of capture timestamps.
# The timing detector must not manufacture source-clock authority.
if full_nominal["tx_time_unavailable"] != full_profile["tx_time_unavailable"]:
    raise AssertionError("inversion profile changed relative source-time qualification")
if integer(full_profile, "profile36_qualified") != 1:
    raise AssertionError("full pooled profile did not qualify at 36 seconds")
error = abs(number(full_profile, "profile36_error_us"))
radius = number(full_profile, "profile36_radius_us")
if not math.isfinite(error) or not math.isfinite(radius) or not error <= radius < 8000:
    raise AssertionError(f"invalid 36-second conditional interval: error={error}, radius={radius}")
if number(full_profile, "profile36_latest_observation_end_sample") > 36 * 24000:
    raise AssertionError("36-second estimate used observations beyond its capture frontier")
check_profile_gain(full_nominal, full_profile, "full")
if integer(full_profile, "profile_stored") <= integer(full_profile, "profile_local_qualified"):
    raise AssertionError("pooled profile did not retain unqualified local observations")

short_nominal = run("short", 160, "clean", "nominal")
short_profile = run("short", 160, "clean", "pooled-inversion")
for row in (short_nominal, short_profile):
    check_clean_authority(row, "short")
if integer(short_profile, "profile_eligible_local_boundaries") <= 0:
    raise AssertionError("short pooled profile did not exercise eligible local fallback")
if integer(short_profile, "profile_selected_boundaries") <= 0:
    raise AssertionError("short pooled profile selected no local fallback boundaries")
check_profile_gain(short_nominal, short_profile, "short")

for mode in ("straight-auxiliary", "no-auxiliary"):
    control = run("full", 180, mode, "pooled-inversion")
    check_capture(control, "full")
    if integer(control, "profile_selected_boundaries") or integer(control, "profile36_qualified"):
        raise AssertionError(f"{mode}: pooled inversion unexpectedly selected or qualified")

print("helper complete profile regression passed")
