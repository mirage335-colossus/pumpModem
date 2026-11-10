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
