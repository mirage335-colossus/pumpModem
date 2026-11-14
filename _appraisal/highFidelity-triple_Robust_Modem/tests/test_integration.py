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

    # A plan must expose, rather than silently compress, the slow native bit.
    slow = invoke("--mode", "plan", "--scenario", "vlf-resonant",
                  "--waveform", "reference", "--max-samples", "1000")
    assert slow["plan"]["actual_bit_seconds"] >= 5760, slow
    assert not slow["plan"]["fits_sample_budget"], slow
    rejected = invoke("--mode", "receive", *common, "--max-samples", "1", code=3)
    assert rejected["status"] == "resource_limit", rejected
    lunar = invoke("--mode", "plan", "--scenario", "eme-disk-slot", "--waveform", "reference")
    assert not lunar["plan"]["fits_numerical_horizon"], lunar
    assert lunar["scenario"]["channel"]["path_count"] == 65536, lunar
    assert lunar["scenario"]["channel"]["paths_omitted"], lunar
    assert len(lunar["scenario"]["channel"]["path_parameters_sha256"]) == 64, lunar
    assert lunar["scenario"]["numerical_adequacy"]["sampled_projection_and_covariance_compared"], lunar
    assert not lunar["scenario"]["numerical_adequacy"]["full_message_qualified"], lunar
    limited = invoke("--mode", "receive", "--scenario", "eme-disk-slot", "--waveform", "reference", code=5)
    assert limited["status"] == "unavailable", limited
    late_oracle = invoke("--mode", "oracle", "--scenario", "eme-disk-slot", "--waveform", "reference",
                         "--oracle-slot", "1", code=5)
    assert late_oracle["status"] == "unavailable", late_oracle
    print("native source/channel/complete-message, noise, EOF, long-duration and budget controls passed")


if __name__ == "__main__":
    main()
