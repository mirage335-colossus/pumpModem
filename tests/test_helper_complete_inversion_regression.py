#!/usr/bin/env python3
"""Matched deterministic regression for complete-study auxiliary inversion."""
import subprocess
import sys


binary = sys.argv[1]


def run(mode, loss, boundary):
    command = [binary, "short", str(loss), "1911000001", mode, "tone", "2", "hf", boundary]
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=90)
    if result.returncode:
        raise RuntimeError(f"{command}: {result.stderr[-2000:]}")
    rows = []
    for line in result.stdout.splitlines():
        parts = line.split()
        if parts:
            rows.append((parts[0], dict(item.split("=", 1) for item in parts[1:] if "=" in item)))
    summary = [values for tag, values in rows if tag == "helper_complete_study"]
    markers = [values for tag, values in rows if tag == "helper_complete_inversion"]
    all_stream_tone = {
        (values["region"], values["selection"]): values
        for tag, values in rows
        if tag == "helper_complete_tone" and values.get("scope") == "continuous_synthetic_clean_twin"
    }
    if len(summary) != 1:
        raise AssertionError(f"{command}: expected one study summary, got {len(summary)}")
    return summary[0], markers, all_stream_tone


def integer(row, field):
    return int(row[field])


def check_zero_auxiliary(mode):
    summary, markers, _ = run(mode, 120, "auxiliary")
    if markers or integer(summary, "inversion_markers") != 0 or integer(summary, "inversion_selected_boundaries") != 0:
        raise AssertionError(f"{mode}: auxiliary inversion was unexpectedly available")


nominal, nominal_markers, _ = run("no-pilot-ramps", 120, "nominal")
auxiliary, marker_rows, _ = run("no-pilot-ramps", 120, "auxiliary")
if len(marker_rows) != integer(auxiliary, "inversion_markers") or len(marker_rows) < 2:
    raise AssertionError("strong fixture did not publish at least two measured inversion markers")
if integer(auxiliary, "inversion_selected_boundaries") < 1:
    raise AssertionError("measured inversion evidence did not select a boundary")
if integer(auxiliary, "boundary_tracker_attempts") or integer(auxiliary, "inversion_usable") < 1:
    raise AssertionError("ramp-independent usable inversion evidence was not exercised")

previous_marker = -1
for marker in marker_rows:
    sample = int(marker["marker_sample"])
    first = int(marker["first_sample"])
    last = int(marker["last_sample"])
    if sample <= previous_marker:
        raise AssertionError("inversion marker dates are not strictly chronological")
    previous_marker = sample
    if not first <= sample <= last or last >= integer(auxiliary, "processed_captured_samples"):
        raise AssertionError("inversion marker has invalid or future support")
    radius = float(marker["marker_radius_samples"])
    measured_error = abs(float(marker["evaluator_error_us"])) * 0.024
    if measured_error > radius:
        raise AssertionError("strong-fixture inversion marker exceeded its reported uncertainty radius")

for row in (nominal, auxiliary):
    for field in ("timestamp_failures", "timestamp_unavailable", "tx_time_unavailable",
                  "reference_mismatches", "missing_samples"):
        if integer(row, field):
            raise AssertionError(f"{field}={row[field]}")
    if integer(row, "processed_captured_samples") != 96000:
        raise AssertionError("short study did not process the complete 96000-sample capture")
    if integer(row, "frequency36_available"):
        raise AssertionError("four-second fixture unexpectedly published a 36-second frequency estimate")

if integer(auxiliary, "physical_schedule_mismatches") >= integer(nominal, "physical_schedule_mismatches"):
    raise AssertionError("auxiliary timing did not reduce physical schedule mismatches in the reserved fixture")
if abs(integer(auxiliary, "output_samples") - integer(nominal, "output_samples")) > 16:
    raise AssertionError("nominal and auxiliary output sample counts differ beyond resampling tolerance")

check_zero_auxiliary("straight-auxiliary")
check_zero_auxiliary("no-auxiliary")

clean_nominal, clean_nominal_markers, clean_nominal_tone = run("clean", 180, "nominal")
clean_auxiliary, clean_auxiliary_markers, clean_auxiliary_tone = run("clean", 180, "auxiliary")
if clean_nominal_markers or clean_auxiliary_markers:
    raise AssertionError("clean weak fixture unexpectedly emitted inversion markers")
same_fields = (
    "timestamp_failures", "timestamp_unavailable", "tx_time_unavailable", "reference_mismatches",
    "missing_samples", "processed_captured_samples", "output_samples", "emitted_before_eof",
    "phase_applied", "clock_applied", "frequency36_available", "frequency36_error_hz",
    "frequency36_conditional_half_width_hz", "frequency36_interval_covered",
    "frequency36_thermal_sigma_hz", "frequency36_process_sigma_hz", "frequency36_model_half_width_hz",
    "later_frozen_frequency_count", "later_frozen_frequency_rms_hz", "helper_clock_error_max_us",
    "fresh_hop_phase_count", "fresh_hop_phase_rms_deg", "fresh_hop_phase_worst_deg",
    "heldout_fresh_hop_count", "heldout_fresh_hop_phase_rms_deg",
)
for field in same_fields:
    if clean_nominal[field] != clean_auxiliary[field]:
        raise AssertionError(f"clean nominal/auxiliary changed {field}: "
                             f"{clean_nominal[field]} != {clean_auxiliary[field]}")
if clean_nominal_tone.keys() != clean_auxiliary_tone.keys() or not clean_nominal_tone:
    raise AssertionError("clean nominal/auxiliary all-stream tone metrics are missing or differ in coverage")
for key in clean_nominal_tone:
    for field, value in clean_nominal_tone[key].items():
        if clean_auxiliary_tone[key].get(field) != value:
            raise AssertionError(f"clean nominal/auxiliary all-stream tone metric {key}/{field} changed: "
                                 f"{value} != {clean_auxiliary_tone[key].get(field)}")

print("helper complete inversion regression passed")
