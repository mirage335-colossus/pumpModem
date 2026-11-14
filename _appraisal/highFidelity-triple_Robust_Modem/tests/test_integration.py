#!/usr/bin/env python3
"""Bounded actual native source/channel/complete-receiver integration checks."""
from __future__ import annotations

import json
import subprocess
import sys


def main() -> None:
    probe = sys.argv[1]

    def invoke(*args: str, code: int = 0) -> dict:
        command = [probe, *args]
        run = subprocess.run(command, capture_output=True, text=True, timeout=35)
        assert run.returncode == code, (command, run.returncode, run.stdout, run.stderr)
        result = json.loads(run.stdout)
        assert result["schema_version"] == 1, result
        return result

    listing = invoke("--list")
    assert len(listing["scenarios"]) >= 6
    common = ["--scenario", "awgn", "--waveform", "smoke", "--seed", "5489",
              "--cn0", "40", "--max-wall-seconds", "25"]
    plan = invoke("--mode", "plan", *common)
    assert plan["plan"]["wire_bits"] == 3, plan
    assert plan["plan"]["represented_seconds"] >= 120, plan
    assert plan["plan"]["fits_sample_budget"], plan
    assert plan["plan"]["scored_slot_horizon"] == plan["plan"]["first_uncontaminated_quiet_slot"] + 2, plan

    wanted = invoke("--mode", "receive", *common)
    assert wanted["status"] == "completed", wanted
    assert wanted["outcomes"]["correct_complete"], wanted
    assert wanted["outcomes"]["completion_evaluated"], wanted
    assert wanted["channel"]["hashed_samples"] == wanted["channel"]["samples"], wanted
    assert wanted["outcomes"]["premature_completions"] == 0, wanted
    assert wanted["outcomes"]["wrong_bits"] == 0, wanted
    assert wanted["outcomes"]["actual_scored_slots"] >= 5, wanted
    chunked = invoke("--mode", "receive", *common, "--chunk", "1021")
    assert chunked["channel"]["input_sha256"] == wanted["channel"]["input_sha256"], chunked
    assert chunked["outcomes"]["correct_complete"], chunked
    first_slot = invoke("--mode", "slot", *common)
    assert first_slot["status"] == "completed", first_slot
    assert first_slot["outcomes"]["component_only"], first_slot
    assert first_slot["outcomes"]["first_public_observation_complete"], first_slot
    assert first_slot["outcomes"]["first_public_observation_correct_data"], first_slot
    assert first_slot["outcomes"]["actual_scored_slots"] == 1, first_slot
    assert not first_slot["outcomes"]["correct_complete"], first_slot
    assert not first_slot["outcomes"]["completion_evaluated"], first_slot

    # Public RX calibration must not depend on the transmitted key or ordinal.
    for control in ("wrong-key", "wrong-ordinal"):
        negative = invoke("--mode", "receive", *common, "--control", control)
        assert negative["status"] == "completed", negative
        assert negative["source"]["receiver_data_basis_gain"] == wanted["source"]["receiver_data_basis_gain"], negative
        assert not negative["outcomes"]["correct_complete"], negative

    durations = [invoke("--mode", "plan", "--scenario", "awgn", "--waveform", "reference",
                        "--bit-seconds", duration)["plan"]["actual_bit_seconds"] for duration in ("16", "32")]
    assert durations[1] > durations[0], durations
    oracle = invoke("--mode", "oracle", "--scenario", "awgn", "--waveform", "reference",
                    "--bit-seconds", "32", "--cn0", "-4")
    presence = oracle["presence_oracle"]
    assert 0 < presence["false_alarm_target"] < 1e-8, presence
    assert 6 < presence["normal_upper_tail_quantile"] < 6.3, presence
    for label in ("hypothesis_0", "hypothesis_1"):
        point = presence[label]
        assert abs(point["probability_detection"] + point["probability_miss"] - 1) < 1e-14, point
        assert point["probability_miss"] > oracle["oracle"]["probability_error"], oracle

    quiet = invoke("--mode", "receive", *common, "--control", "noise")
    assert quiet["status"] == "completed", quiet
    assert quiet["outcomes"]["false_data"] == 0, quiet
    assert quiet["outcomes"]["physical_completions"] == 0, quiet

    eof = invoke("--mode", "receive", *common, "--control", "eof")
    assert not eof["outcomes"]["correct_complete"], eof
    assert eof["outcomes"]["physical_completions"] == 0, eof

    transition = invoke("--mode", "receive", "--scenario", "hf-transition",
                        "--waveform", "reference", "--seed", "9204", "--cn0", "20",
                        "--message", "A", "--max-wall-seconds", "25")
    assert transition["status"] == "completed", transition
    transition_boundary = transition["outcomes"]["resumed_data_boundary_sample"]
    assert transition_boundary == 240 * transition["plan"]["sample_rate_hz"], transition
    assert transition["outcomes"]["resumed_data_boundary_source"] == "scenario_fade_stop", transition
    expected_resumed = sum(
        slot["index"] < transition["plan"]["wire_bits"] and
        slot["first_sample"] >= transition_boundary and slot["decision"] == "data" and
        slot["complete"] and slot["all_valid"]
        for slot in transition["slots"])
    assert expected_resumed > 0, transition
    assert transition["outcomes"]["resumed_data_slots"] == expected_resumed, transition
    assert any(message.get("reacquisition_count", 0) > 0 for message in transition["messages"]), transition

    # A plan must expose, rather than silently compress, the slow native bit.
    slow = invoke("--mode", "plan", "--scenario", "vlf-resonant",
                  "--waveform", "reference", "--max-samples", "1000")
    assert slow["plan"]["actual_bit_seconds"] >= 5760, slow
    assert not slow["plan"]["fits_sample_budget"], slow
    slow_tapered = invoke("--mode", "plan", "--scenario", "vlf-resonant",
                          "--waveform", "reference", "--max-samples", "1000",
                          "--pulse", "tapered-rrc")
    assert slow["plan"]["pulse"] == "rrc", slow
    assert slow_tapered["plan"]["pulse"] == "tapered-rrc", slow_tapered
    for field in ("sample_rate_hz", "requested_bit_seconds", "actual_bit_seconds",
                  "first_data_nominal_seconds", "wire_bits"):
        assert slow["plan"][field] == slow_tapered["plan"][field], (field, slow, slow_tapered)

    for unsupported in (("--control", "noise"), ("--helper",), ("--scenario", "awgn"),
                        ("--waveform", "smoke")):
        diagnostic_args = ["--mode", "diagnose", "--scenario", "vlf-resonant",
                           "--waveform", "reference", "--max-wall-seconds", "2", *unsupported]
        diagnostic_rejected = invoke(*diagnostic_args, code=2)
        assert diagnostic_rejected["status"] == "error", diagnostic_rejected

    # The spectral EME episode model supports the default message within its
    # declared 360-second numerical horizon. A selected later oracle bit can be
    # represented by a cheap plan-only request without rendering the lunar path.
    eme_common = ["--scenario", "eme-disk-episode", "--waveform", "reference",
                  "--max-samples", "100000000", "--max-wall-seconds", "20"]
    eme_default = invoke("--mode", "plan", *eme_common)
    assert eme_default["status"] == "completed", eme_default
    assert eme_default["plan"]["wire_bits"] == 3, eme_default
    assert eme_default["plan"]["numerical_horizon_seconds"] == 360, eme_default
    assert eme_default["plan"]["represented_seconds"] < 360, eme_default
    assert eme_default["plan"]["fits_numerical_horizon"], eme_default
    eme_selected_plan = invoke("--mode", "plan", *eme_common, "--oracle-slot", "1")
    assert eme_selected_plan["plan"]["fits_numerical_horizon"], eme_selected_plan
    assert eme_selected_plan["plan"]["public_slots"][1]["index"] == 1, eme_selected_plan
    assert 1 < eme_selected_plan["plan"]["wire_bits"], eme_selected_plan
    eme_long = invoke("--mode", "receive", *eme_common, "--message", "abcdefghijklmnop", code=5)
    assert eme_long["status"] == "unavailable", eme_long
    assert eme_long["plan"]["wire_bits"] > eme_default["plan"]["wire_bits"], eme_long
    lunar_helper = invoke("--mode", "plan", *eme_common, "--helper", code=5)
    assert lunar_helper["status"] == "unavailable", lunar_helper

    rejected = invoke("--mode", "receive", *common, "--max-samples", "1", code=3)
    assert rejected["status"] == "resource_limit", rejected
    late_slot = 6
    late_oracle = invoke("--mode", "oracle", *eme_common, "--message", "abcdefghijklmnop",
                         "--oracle-slot", str(late_slot), code=5)
    assert late_oracle["status"] == "unavailable", late_oracle
    assert late_oracle["plan"]["oracle_slot"] == late_slot < late_oracle["plan"]["wire_bits"], late_oracle
    assert late_oracle["plan"]["oracle_horizon_samples"] > 360 * late_oracle["plan"]["sample_rate_hz"], late_oracle
    assert "numerical qualification" in late_oracle["reason"], late_oracle
    print("native source/channel/complete-message, noise, EOF, long-duration and budget controls passed")


if __name__ == "__main__":
    main()
