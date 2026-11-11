"""Bounded subprocess contract checks for the production native receiver probe."""
import json
import re
import subprocess
import sys
from pathlib import Path


def main(executable):
    def run(command, *args, code=0, default_allocation=False, parse_error=None):
        allocation = [] if default_allocation else ["--workers", "1", "--slot-owners", "1"]
        completed = subprocess.run(
            [str(executable), command, *allocation, "--deadline-ms", "12000", *args],
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
    assert description["allocation"]["workers"] == 1
    assert description["allocation"]["slot_owners"] == 1
    assert description["result"] == {"available": True, "actual_device_work": False}
    assert not description["phases"]
    assert description["identity"]["requested_tensor_coarse_math"] == "fft"
    tensor_on_cpu = run("describe", "--tensor-coarse-math", "int8", code=2)
    assert tensor_on_cpu["status"] == "unavailable" and not tensor_on_cpu["phases"]
    assert tensor_on_cpu["identity"]["requested_tensor_coarse_math"] == "int8"
    run("describe", "--backend", "vulkan", "--tensor-coarse-math", "int8", code=1,
        parse_error="Vulkan coarse search uses the full FFT")
    run("describe", "--backend", "vulkan", "--tensor-precision", "fp64", code=1,
        parse_error="Vulkan coarse search uses its native FP32 path")
    automatic = run("describe", default_allocation=True)["allocation"]
    assert automatic["hardware_threads"] > 0
    assert automatic["workers"] == 2 * automatic["hardware_threads"]

    plan = run("plan")
    assert plan["result"]["planned"] and plan["observation"] is None
    assert plan["configuration"]["recurring_horizon"]["slots"] == 24
    assert plan["configuration"]["bit_seconds"] > 0
    stream_plan = plan["configuration"]["stream_plan"]
    assert stream_plan["minimum_backlog_slots"] == 2 * stream_plan["message_slots"] == 10
    assert stream_plan["spool_bytes"] == stream_plan["generated_samples"] * 17
    assert stream_plan["maximum_startup_carryover_slots"] == 1
    assert [row["name"] for row in plan["phases"]] == ["profile_preparation"]
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

    codec = run("codec", "--encrypt", "on")
    assert codec["result"]["decoded_after_two_quiet"]
    assert codec["result"]["marker_intervals"] > 1
    assert codec["result"]["complete_receiver_service"] is False

    stream = run("message", "--max-samples", "600000", "--slots", "5")
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
