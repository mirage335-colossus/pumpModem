#!/usr/bin/env python3
"""Small CLI contract checks for the independent streamed triad study."""
import hashlib
import json
import math
import pathlib
import struct
import subprocess
import sys
import tempfile


BINARY = sys.argv[1]
COMMON = ["--rate", "36", "--added", "10", "--sample-rate", "512",
          "--seconds", "8", "--phase-diffusion", "0", "--seed", "71231",
          "--epoch", "1800000000"]


def invoke(*arguments, ok=True, timeout=30):
    command = [BINARY, *arguments]
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=timeout)
    if ok and result.returncode != 0:
        raise AssertionError(f"command failed ({result.returncode}): {command}\n{result.stderr[-2000:]}")
    if not ok and result.returncode == 0:
        raise AssertionError(f"invalid command unexpectedly succeeded: {command}")
    return result


def one_json(result):
    rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
    if len(rows) != 1:
        raise AssertionError(f"expected one JSON result, got {len(rows)}")
    return rows[0]


def export(directory, *extra):
    invoke("--mode", "export", *COMMON, "--channel", "none", "--threads", "2",
           "--chunk", "512", "--output", str(directory), *extra, timeout=45)
    return json.loads((directory / "manifest.json").read_text())


def file_digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def complex64_power(path):
    data = path.read_bytes()
    if len(data) % 8:
        raise AssertionError(f"complex64 export has a partial sample: {path}")
    return sum(real * real + imag * imag
               for real, imag in struct.iter_unpack("=ff", data))


def check_export_gain(manifest, directory):
    channel = manifest["channel_model"]
    if channel["signal_gain"] != 1:
        raise AssertionError("triad-long changed the source's total-power normalization")
    rows = manifest["signal_shape"][0]
    signal_path = directory / manifest["signal_file"]
    if signal_path.stat().st_size != rows * 8:
        raise AssertionError("signal export shape does not match its complex64 file")
    measured = complex64_power(signal_path)
    expected = manifest["channel_signal_energy_samples"]
    if not math.isclose(measured, expected, rel_tol=2e-5, abs_tol=1e-5):
        raise AssertionError(f"export gain/energy mismatch: file={measured}, report={expected}")
    energy_seconds = expected / manifest["sample_rate"]
    lower = manifest["nominal_payload_energy_seconds_lower"]
    ratio = energy_seconds / lower
    # The private three-carrier source is normalized to unit total power. The
    # finite pulse/edge integration is close to its allocation lower bound;
    # these broad limits catch a missing or duplicated sqrt(3) gain.
    if not 0.5 <= ratio <= 1.5:
        raise AssertionError(f"unexpected normalized sample energy ({ratio:.3f} of lower bound)")


def main():
    # Check CLI-reported channel gain on an inexpensive complete sample path.
    run = one_json(invoke("--mode", "run", *COMMON, "--channel", "none",
                          "--cn0", "80", "--threads", "2", "--chunk", "512",
                          "--block-seconds", "1", timeout=45))
    if run["channel_model"]["signal_gain"] != 1:
        raise AssertionError("run path does not apply unit total-power signal gain")
    run_ratio = (run["signal_energy"] / run["sample_rate"] /
                 run["nominal_payload_energy_seconds_lower"])
    if not 0.5 <= run_ratio <= 1.5:
        raise AssertionError(f"run sample energy has incorrect gain ({run_ratio:.3f})")
    if not run["complete_observation"] or not run["accepted"] or not run["correct"]:
        raise AssertionError(f"clean high-SNR end-to-end run did not decode correctly: {run}")

    with tempfile.TemporaryDirectory(prefix="triad-long-test-") as scratch:
        root = pathlib.Path(scratch)
        first = root / "fresh-flat"
        if first.exists():
            raise AssertionError("test output path was not fresh")
        base_manifest = export(first)
        if base_manifest["basis_shape"][0] != 2:
            raise AssertionError(f"one epoch should export two bit hypotheses: {base_manifest['basis_shape']}")
        if base_manifest["basis_shape"][1] != base_manifest["signal_shape"][0]:
            raise AssertionError("basis and signal sample calendars differ")
        check_export_gain(base_manifest, first)

        before = {name: file_digest(first / name)
                  for name in ("basis.c64", "signal.c64", "manifest.json")}
        rejected = invoke("--mode", "export", *COMMON, "--channel", "none",
                          "--threads", "2", "--chunk", "512", "--output", str(first),
                          ok=False, timeout=20)
        if "fresh output directory" not in rejected.stderr:
            raise AssertionError(f"export collision was rejected for an unexpected reason: {rejected.stderr}")
        after = {name: file_digest(first / name) for name in before}
        if after != before:
            raise AssertionError("a repeated export overwrote existing files")

        two_ray = root / "fresh-two-ray"
        two_ray_manifest = export(two_ray, "--basis", "two-ray")
        shape = two_ray_manifest["basis_shape"]
        if shape[2] != 2:
            raise AssertionError(f"coupled two-ray export should have two basis columns: {shape}")
        params = two_ray_manifest["candidateparams"]
        matrix = (two_ray / "basis.c64").read_bytes()
        candidate_bytes = shape[1] * shape[2] * 8
        for candidate, parameter in enumerate(params):
            candidate_start = candidate * candidate_bytes
            values = list(struct.iter_unpack("=ff", matrix[candidate_start:candidate_start + candidate_bytes]))
            delayed_column_energy = sum(values[sample * shape[2] + 1][0] ** 2 +
                                        values[sample * shape[2] + 1][1] ** 2
                                        for sample in range(shape[1]))
            if parameter["tap_count"] == 1 and delayed_column_energy != 0:
                raise AssertionError("flat basis did not zero-pad its second column")
            if parameter["tap_count"] == 2 and delayed_column_energy <= 0:
                raise AssertionError("coupled two-ray basis has an empty delayed column")

        epochs = root / "fresh-epochs"
        epoch_manifest = export(epochs, "--epoch-offsets", "0,3600")
        shape = epoch_manifest["basis_shape"]
        if shape[0] != 4:
            raise AssertionError(f"two bits by two epoch hypotheses should export K=4: {shape}")
        params = epoch_manifest["candidateparams"]
        indexed = {(row["bit"], row["epoch_offset_seconds"]): index
                   for index, row in enumerate(params)}
        if set(indexed) != {(0, 0), (1, 0), (0, 3600), (1, 3600)}:
            raise AssertionError(f"epoch candidates are not independently represented: {params}")
        rows, columns = shape[1], shape[2]
        basis = (epochs / "basis.c64").read_bytes()
        if len(basis) != shape[0] * rows * columns * 8:
            raise AssertionError("basis file length does not match candidate/sample/column dimensions")
        row_bytes = rows * columns * 8
        for bit in (0, 1):
            a = basis[indexed[(bit, 0)] * row_bytes:(indexed[(bit, 0)] + 1) * row_bytes]
            b = basis[indexed[(bit, 3600)] * row_bytes:(indexed[(bit, 3600)] + 1) * row_bytes]
            if a == b:
                raise AssertionError(f"encrypted basis did not change for bit {bit} at a different epoch")

    for option in (("--signal-bit", "256"), ("--threads", "4294967296"),
                   ("--chunk", "-1")):
        invoke("--mode", "plan", *option, ok=False, timeout=10)

    # A two-day coherent block is a planner input only; this short source case
    # proves it does not allocate a two-day waveform. Burst waits create a
    # source tile quantum that is distinct from a simple 400-ms dwell grid.
    plan = one_json(invoke("--mode", "plan", *COMMON, "--burst-wait-tiles", "2",
                           "--block-seconds", "172800", "--duration-alternatives", "3",
                           "--cn0", "40", "--correct", ".8", "--false", ".01",
                           "--horizon", "3600", "--attempt-spacing", "60",
                           "--assume-model-bounds", timeout=30))
    for field in ("duration_seconds", "duration_quantum_seconds", "duration_offset_seconds",
                  "duration_alternatives", "search_tests_per_horizon_saturated",
                  "estimated_operations", "estimated_workspace_bytes",
                  "requested_active_seconds_for_transmitter"):
        if field not in plan:
            raise AssertionError(f"duration planner omits {field}")
    # The source geometry is measured from the matching export. Burst waits
    # change the planner's calendar quantum, not the physical source tile.
    tile = base_manifest["source_format"]["tile_seconds"]
    quantum = plan["duration_quantum_seconds"]
    offset = plan["duration_offset_seconds"]
    if not math.isclose(quantum, 3 * tile, rel_tol=1e-12) or not math.isclose(offset, -2 * tile, rel_tol=1e-12):
        raise AssertionError(f"planner duration lattice lost its source quantum: tile={tile}, q={quantum}, offset={offset}")
    if not plan["duration_found"]:
        raise AssertionError(f"high-SNR supported planner case found no duration: {plan['status']}")
    coordinate = (plan["duration_seconds"] - offset) / quantum
    if not math.isclose(coordinate, round(coordinate), abs_tol=1e-10):
        raise AssertionError("planned duration is not on the source-tile lattice")
    print("triad-long CLI regression passed")


if __name__ == "__main__":
    main()
