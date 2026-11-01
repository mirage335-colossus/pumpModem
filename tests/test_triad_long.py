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


def export_stats(directory, *extra):
    invoke("--mode", "export-stats", *COMMON, "--channel", "none", "--threads", "2",
           "--chunk", "512", "--block-seconds", "4", "--output", str(directory),
           *extra, timeout=45)
    return json.loads((directory / "manifest.json").read_text())


def complex128_values(path):
    data = path.read_bytes()
    if len(data) % 16:
        raise AssertionError(f"complex128 export has a partial value: {path}")
    return [complex(real, imag) for real, imag in struct.iter_unpack("<dd", data)]


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

        # Block statistics must match direct full-basis projection on identical
        # public rows. The raw basis is FP32, so compare with a small tolerance.
        direct = root / "fresh-direct"
        direct_manifest = export(direct, "--block-seconds", "4")
        stats = root / "fresh-stats"
        stats_manifest = export_stats(stats)
        if stats_manifest["format"] != "triad-block-stats-v1" or stats_manifest["dimension"] != 2:
            raise AssertionError(f"unexpected block-statistics format/dimension: {stats_manifest}")
        if stats_manifest["epoch"] != int(COMMON[COMMON.index("--epoch") + 1]) or "relative_white_fm_hz_sqrt_s" not in stats_manifest["oscillator_model"]:
            raise AssertionError("statistics manifest omits explicit epoch or oscillator settings")
        if "not included" not in stats_manifest["source_hash_binding"] or not stats_manifest["oscillator_model"]["truth_fields_simulation_only"]:
            raise AssertionError("statistics manifest misstates hash binding or simulation-only truth")
        if stats_manifest["column_mapping"] != [[0], [1]]:
            raise AssertionError(f"flat candidate column mapping is wrong: {stats_manifest['column_mapping']}")
        gram = complex128_values(stats / "gram.c128")
        means = complex128_values(stats / "mean.c128")
        direct_shape = direct_manifest["basis_shape"]
        direct_basis = [complex(real, imag) for real, imag in struct.iter_unpack(
            "=ff", (direct / "basis.c64").read_bytes())]
        direct_signal = [complex(real, imag) for real, imag in struct.iter_unpack(
            "=ff", (direct / "signal.c64").read_bytes())]
        block_rows = stats_manifest["block_samples"]
        expected = []
        intervals = list(struct.iter_unpack("<QQ", (stats / "intervals.u64").read_bytes()))
        if len(intervals) != stats_manifest["block_count"]:
            raise AssertionError("block interval count differs from manifest")
        for block_index, (first_sample, stop_sample) in enumerate(intervals):
            expected_gram = []
            for i in range(2):
                ai = direct_basis[i * direct_shape[1]:(i + 1) * direct_shape[1]]
                for j in range(2):
                    aj = direct_basis[j * direct_shape[1]:(j + 1) * direct_shape[1]]
                    expected_gram.append(sum(a.conjugate() * b for a, b in
                                             zip(ai[first_sample:stop_sample], aj[first_sample:stop_sample])))
            for element, want in enumerate(expected_gram):
                got = gram[block_index * 4 + element]
                if not math.isclose(got.real, want.real, rel_tol=2e-5, abs_tol=2e-5) or not math.isclose(got.imag, want.imag, rel_tol=2e-5, abs_tol=2e-5):
                    raise AssertionError(f"block {block_index} Gram differs from direct basis: got={got}, expected={want}")
            for i in range(2):
                ai = direct_basis[i * direct_shape[1]:(i + 1) * direct_shape[1]]
                want = sum(a.conjugate() * y for a, y in zip(ai[first_sample:stop_sample], direct_signal[first_sample:stop_sample]))
                got = means[block_index * 2 + i]
                if not math.isclose(got.real, want.real, rel_tol=2e-5, abs_tol=2e-5) or not math.isclose(got.imag, want.imag, rel_tol=2e-5, abs_tol=2e-5):
                    raise AssertionError(f"block {block_index} projection differs from direct basis: got={got}, expected={want}")

        chunked = root / "fresh-stats-chunked"
        chunked_manifest = export_stats(chunked, "--chunk", "777")
        for file_name in ("intervals.u64", "valid_rows.u64"):
            if (stats / file_name).read_bytes() != (chunked / file_name).read_bytes():
                raise AssertionError(f"changing chunk size changed {file_name}")
        for file_name in ("gram.c128", "mean.c128"):
            left, right = complex128_values(stats / file_name), complex128_values(chunked / file_name)
            if len(left) != len(right) or any(abs(a - b) > 1e-11 * max(1.0, abs(a), abs(b)) for a, b in zip(left, right)):
                raise AssertionError(f"changing chunk size materially changed {file_name}")

        rays = root / "fresh-stats-rays"
        ray_manifest = export_stats(rays, "--basis", "two-ray")
        if ray_manifest["dimension"] != 4 or ray_manifest["column_mapping"] != [[0], [1], [0, 2], [1, 3]]:
            raise AssertionError(f"two-ray columns were not uniquely deduplicated: {ray_manifest['column_mapping']}")

        erased = root / "fresh-stats-erased"
        erased_manifest = export_stats(erased, "--dropout-ms", "40", "--dropout-start-ms", "100")
        erased_valid = struct.unpack("<" + "Q" * erased_manifest["block_count"], (erased / "valid_rows.u64").read_bytes())
        if not any(value < stop - start for value, (start, stop) in zip(erased_valid, struct.iter_unpack("<QQ", (erased / "intervals.u64").read_bytes()))):
            raise AssertionError("configured dropout did not remove rows from statistics")

        noisy = root / "fresh-stats-noisy"
        noisy_manifest = export_stats(noisy, "--stats-noisy", "--channel", "hf",
                                       "--phase-diffusion", ".02", "--white-fm", ".1",
                                       "--random-walk-fm", ".01", "--flicker-fm", ".02",
                                       "--flicker-tau", ".1", "10", "--flicker-bands", "3",
                                       "--white-pm", ".01")
        observations = complex128_values(noisy / "observation.c128")
        if len(observations) != noisy_manifest["block_count"] * noisy_manifest["dimension"] or not all(math.isfinite(x.real) and math.isfinite(x.imag) for x in observations):
            raise AssertionError("actual noisy projection output has invalid dimensions or values")
        if observations == complex128_values(noisy / "mean.c128"):
            raise AssertionError("noisy projection unexpectedly equals noiseless mean")

        # Channel seed changes actual samples while leaving the public design
        # matrix fixed; key seed changes the encrypted public basis.
        seed_a, seed_b, seed_key = (root / "seed-a", root / "seed-b", root / "seed-key")
        seed_extra = ("--start", "0", ".01", ".01", "--frequency", "0", ".1", ".1", "--clock-ppm", "0,100")
        export_stats(seed_a, *seed_extra, "--channel-seed", "811")
        export_stats(seed_b, *seed_extra, "--channel-seed", "812")
        export_stats(seed_key, *seed_extra, "--channel-seed", "811", "--key-seed", "999")
        if (seed_a / "gram.c128").read_bytes() != (seed_b / "gram.c128").read_bytes():
            raise AssertionError("channel seed changed the public Gram matrix")
        if (seed_a / "mean.c128").read_bytes() == (seed_b / "mean.c128").read_bytes():
            raise AssertionError("channel seed did not change the actual-signal mean")
        if (seed_a / "gram.c128").read_bytes() == (seed_key / "gram.c128").read_bytes():
            raise AssertionError("key seed did not change the encrypted public Gram matrix")

        collision = invoke("--mode", "export-stats", *COMMON, "--channel", "none",
                           "--threads", "2", "--chunk", "512", "--block-seconds", "4",
                           "--output", str(stats), ok=False, timeout=20)
        if "fresh output directory" not in collision.stderr:
            raise AssertionError(f"stats export collision had unexpected failure: {collision.stderr}")

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
