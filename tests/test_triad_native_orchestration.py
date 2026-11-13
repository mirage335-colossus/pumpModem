"""Run the Python report consumers against the compiled receiver probe."""
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_abbreviated import regression_batch
from triad_case_suite import SCHEMA, run_cases
from triad_native import NativeProbe, allocation, is_sha256, read_report


def main(executable):
    probe = NativeProbe(executable)
    description = probe.call("describe", deadline_ms=2000, max_samples=1)
    resolved = allocation(description)
    assert resolved["workers"] == 2 * resolved["hardware_threads"]
    receipt = probe.call("regression", workers=1, slot_owners=1, units=1,
                         deadline_ms=6000, max_samples=100_000)
    costs, geometry = regression_batch(receipt, "active", 1)
    assert all(cost > 0 for cost in costs.values())
    assert is_sha256(geometry["observation"]["input_sha256"])
    assert receipt["identity"]["executable_sha256"] == probe.executable_sha256
    probe.verify_unchanged()

    # Full high-SNR functionality check through the compiled profile, streaming
    # channel and complete receiver. This is not a sensitivity claim. Keep a
    # repeatable channel seed and one owner's explicit bounded work allocation.
    dynamic = probe.call("message", workers=2, slot_owners=1,
                         memory_limit_bytes=1024 ** 3, seed=101,
                         sample_rate=1500, data_if=500, rate=120,
                         spread_preset="hf-worst", selective_perturbation="each-65",
                         doppler_hz=8, target_cn0=80, cn0=80,
                         deadline_ms=15_000, max_samples=600_000)
    config = dynamic["configuration"]
    assert config["time_varying_channel"] is True
    assert abs(config["actual_cn0_db_hz"] - 80) < 1e-10
    assert config["spread_preset"] == "hf-worst" and config["spread_delay_seconds"] == .007
    assert config["spread_doppler_hz"] == 30
    assert config["selective_perturbation"] == "each-65"
    assert config["time_varying_atmospheric_hz"] == 30
    assert config["time_varying_common_doppler_hz"] == 38
    assert config["channel_bounded_spread_knot_seconds"] > 0
    assert config["channel_common_doppler_wander_hz"] == 38
    assert config["channel_selective_coefficient_law"] == "gaussian-erf"
    assert config["channel_selective_modulation_rate_hz"] == 3
    allocation_info = allocation(dynamic)
    assert allocation_info["workers"] == 2 and allocation_info["slot_owners"] == 1
    assert allocation_info["memory_limit_bytes"] == 1024 ** 3
    spread = dynamic["observation"]["bounded_spread_realization"]
    common = dynamic["observation"]["common_doppler_realization"]
    assert spread["processed_rows"] > 0 and spread["excess_delay_seconds"] > 0
    assert 0 < spread["excess_delay_seconds"] < config["spread_delay_seconds"]
    assert 0 <= spread["minimum_echo_ratio"] < spread["maximum_echo_ratio"] <= 1
    assert -spread["doppler_bound_hz"] <= spread["minimum_doppler_hz"] < 0
    assert 0 < spread["maximum_doppler_hz"] <= spread["doppler_bound_hz"]
    assert common["processed_rows"] > 0 and common["bound_hz"] == 38
    assert -common["bound_hz"] <= common["minimum_hz"] < 0
    assert 0 < common["maximum_hz"] <= common["bound_hz"]
    message = dynamic["result"]["stream"]["messages"]
    slots = dynamic["result"]["stream"]["slots"]
    assert len(message) == 1 and message[0]["expected_source_matches"]
    assert message[0]["physical_complete"] and message[0]["observed_bits"] == 3
    assert message[0]["trailing_quiet_slots"] == 2 and message[0]["missing_slots"] == 0
    assert [row["decision"] for row in slots] == ["data", "data", "data", "quiet", "quiet"]
    assert [row["bit"] for row in slots[:3]] == [0, 0, 1]
    assert dynamic["result"]["message_and_physical_quiet_verified"] is True
    probe.verify_unchanged()

    # Exercise real manual-file parsing and native plan/admission without timing a
    # capacity campaign. A one-sample lease cannot provide any valid observation.
    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory) / "manual.json"
        report = run_cases(executable=executable, output=output, case_indices=[1], threads=1,
                           message_gate=False, deadline_ms=4000, row_deadline_ms=6000,
                           max_samples=1, coarse_tiles=4)
        assert report["schema"] == SCHEMA and report["identity_verified_at_completion"] is True
        assert report["selection"]["indices"] == [1] and not report["selection"]["complete_current_list"]
        assert report["status"] == "measured_with_unresolved_margins"
        row = report["cases"][0]
        assert row["plan"]["identity"]["executable_sha256"] == probe.executable_sha256
        assert row["status"] == "unresolved" and row["unresolved_work"]
        assert read_report(output)["status"] == report["status"]


if __name__ == "__main__":
    main(Path(sys.argv[1]))
