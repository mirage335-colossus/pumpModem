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


def export_group_stats(directory, *extra):
    invoke("--mode", "export-group-stats", *COMMON, "--channel", "none", "--threads", "1",
           "--chunk", "777", "--block-seconds", "4", "--output", str(directory),
           *extra, timeout=45)
    return json.loads((directory / "manifest.json").read_text())


def export_sampled(directory, *extra):
    invoke("--mode", "export-sampled", *COMMON, "--channel", "none", "--threads", "2",
           "--chunk", "512", "--output", str(directory), *extra, timeout=60)
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
    if not run["gaussian_projection_supported"] or run["sampled_frontend"]["active"]:
        raise AssertionError("default sampled frontend did not preserve the linear reference")
    frontend = one_json(invoke("--mode", "run", *COMMON, "--channel", "none",
                               "--cn0", "80", "--threads", "2", "--chunk", "512",
                               "--block-seconds", "1", "--dac-bits", "16", "--dac-enob", "14",
                               "--adc-bits", "16", "--adc-full-scale", ".25", "--adc-gain", "10",
                               "--interference-tone", "2", "210", "--rx-lowpass-hz", "100",
                               "--overload-recovery-seconds", ".02", timeout=45))
    if frontend["analytical_null_supported"] or frontend["accepted"] or frontend["gaussian_projection_supported"]:
        raise AssertionError("nonlinear sampled frontend inherited Gaussian analytic admission")
    if not frontend["frontend_report"]["adc_clipped_components"]:
        raise AssertionError("actual sampled ADC clipping was not reported")
    if frontend["sampled_frontend"]["adc"]["nominal_bits"] != 16:
        raise AssertionError("effective frontend configuration was omitted")
    run_ratio = (run["signal_energy"] / run["sample_rate"] /
                 run["nominal_payload_energy_seconds_lower"])
    if not 0.5 <= run_ratio <= 1.5:
        raise AssertionError(f"run sample energy has incorrect gain ({run_ratio:.3f})")
    if not run["complete_observation"] or not run["accepted"] or not run["correct"]:
        raise AssertionError(f"clean high-SNR end-to-end run did not decode correctly: {run}")
    multi_trial = invoke("--mode", "run", *COMMON, "--channel", "none", "--cn0", "80",
                         "--threads", "2", "--chunk", "512", "--block-seconds", "1",
                         "--trials", "2", "--oscillator-seed", "501", "--noise-seed", "601",
                         "--frontend-seed", "701", timeout=60)
    trial_rows = [json.loads(line) for line in multi_trial.stdout.splitlines() if line.startswith("{")]
    if len(trial_rows) != 2:
        raise AssertionError(f"two-trial seed-root run emitted {len(trial_rows)} results")
    for index, trial_row in enumerate(trial_rows):
        roots = trial_row["random_seed_roots"]
        expected = (int(COMMON[COMMON.index("--seed") + 1]) + index, 501 + index, 601 + index, 701 + index)
        actual = (roots["channel"], roots["oscillator"], roots["noise"], roots["frontend"])
        if actual != expected:
            raise AssertionError(f"trial seed roots did not increment exactly once: {actual} != {expected}")

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

        sampled = root / "fresh-sampled"
        sampled_manifest = export_sampled(sampled)
        sampled_shape = sampled_manifest["basis_shape"]
        if sampled_manifest["format"] != "triad-sampled-waveforms-v1" or sampled_shape != [2, base_manifest["basis_shape"][1], 1]:
            raise AssertionError(f"sampled export has an unexpected schema or basis shape: {sampled_manifest}")
        if sampled_manifest["observation_shape"] != [sampled_manifest["sample_count"]] or sampled_manifest["sampled_frontend"]["active"]:
            raise AssertionError("sampled waveform dimensions/frontend state are missing")
        if sampled_manifest["template_sample_count"] != sampled_manifest["observation_sample_count"] or sampled_manifest["template_sample_count"] != sampled_manifest["sample_count"]:
            raise AssertionError("sampled exporter did not state the shared template/observation calendar")
        default_roots = sampled_manifest["random_seed_roots"]
        if any(default_roots[name] != sampled_manifest["channel_seed"]
               for name in ("channel", "oscillator", "noise", "frontend")):
            raise AssertionError(f"default RNG roots did not fall back to channel seed: {default_roots}")
        if (sampled / "basis.c128").stat().st_size != 2 * sampled_shape[1] * 16:
            raise AssertionError("complex128 basis file size does not match its declared shape")
        if (sampled / "observation.c128").stat().st_size != sampled_manifest["sample_count"] * 16:
            raise AssertionError("complex128 observation file size does not match its declared shape")
        observed = complex128_values(sampled / "observation.c128")
        if not any(abs(value) > 0 for value in observed):
            raise AssertionError("actual sampled observation is unexpectedly all zero")
        if any(value != 1 for value in (sampled / "validity.u8").read_bytes()):
            raise AssertionError("un-erased sampled waveform has invalid sample flags")
        high_precision_basis = complex128_values(sampled / "basis.c128")
        old_basis = [complex(real, imag) for real, imag in struct.iter_unpack(
            "=ff", (first / "basis.c64").read_bytes())]
        if len(high_precision_basis) != len(old_basis) or any(
                abs(a - b) > 2e-7 * max(1.0, abs(a), abs(b))
                for a, b in zip(high_precision_basis, old_basis)):
            raise AssertionError("complex128 public basis differs from the established sampled basis")

        sampled_chunked = root / "fresh-sampled-chunked"
        export_sampled(sampled_chunked, "--chunk", "777")
        for file_name in ("basis.c128", "observation.c128", "validity.u8"):
            if (sampled / file_name).read_bytes() != (sampled_chunked / file_name).read_bytes():
                raise AssertionError(f"changing sampled-export chunk size changed {file_name}")

        sampled_erased = root / "fresh-sampled-erased"
        erased_manifest = export_sampled(sampled_erased, "--dropout-ms", "40", "--dropout-start-ms", "100")
        erased_flags = (sampled_erased / "validity.u8").read_bytes()
        if len(erased_flags) != erased_manifest["sample_count"] or not any(value == 0 for value in erased_flags):
            raise AssertionError("sampled exporter dropped or omitted the selected dwell erasure mask")

        sampled_null = root / "fresh-sampled-null"
        null_manifest = export_sampled(sampled_null, "--control", "null")
        null_observation = complex128_values(sampled_null / "observation.c128")
        if null_manifest["signal_enabled"] or not any(abs(value) > 0 for value in null_observation):
            raise AssertionError("noise-only sampled control did not retain its noise waveform")

        sampled_epochs = root / "fresh-sampled-epochs"
        sampled_epoch_manifest = export_sampled(sampled_epochs, "--epoch-offsets", "0,3600")
        sampled_epoch_shape = sampled_epoch_manifest["basis_shape"]
        if sampled_epoch_shape[0] != 4:
            raise AssertionError(f"sampled epoch bank has incorrect dimensions: {sampled_epoch_shape}")
        sampled_epoch_params = sampled_epoch_manifest["candidates"]
        sampled_epoch_index = {(row["bit"], row["epoch_offset_seconds"]): index
                               for index, row in enumerate(sampled_epoch_params)}
        sampled_epoch_basis = complex128_values(sampled_epochs / "basis.c128")
        sampled_row_values = sampled_epoch_shape[1] * sampled_epoch_shape[2]
        for bit in (0, 1):
            left = sampled_epoch_index[(bit, 0)] * sampled_row_values
            right = sampled_epoch_index[(bit, 3600)] * sampled_row_values
            if sampled_epoch_basis[left:left + sampled_row_values] == sampled_epoch_basis[right:right + sampled_row_values]:
                raise AssertionError(f"sampled encrypted basis did not change with epoch for bit {bit}")

        sampled_two_ray = root / "fresh-sampled-two-ray-chunk1"
        sampled_two_ray_manifest = export_sampled(sampled_two_ray, "--basis", "two-ray", "--chunk", "1")
        if sampled_two_ray_manifest["basis_shape"][2] != 2:
            raise AssertionError("sampled two-ray basis did not retain both coupled tap columns")
        two_ray_values = complex128_values(sampled_two_ray / "basis.c128")
        if not any(abs(two_ray_values[index]) > 0
                   for index in range(1, len(two_ray_values), 2)):
            raise AssertionError("sampled two-ray export lost the delayed tap")

        sampled_frontend = root / "fresh-sampled-frontend"
        frontend_manifest = export_sampled(sampled_frontend, "--dac-bits", "16", "--dac-enob", "14",
                                            "--adc-bits", "16", "--adc-enob", "12",
                                            "--adc-full-scale", "1", "--adc-gain", ".2",
                                            "--adc-dither-lsb", ".5", "--rx-lowpass-hz", "100")
        if not frontend_manifest["sampled_frontend"]["active"] or not (sampled_frontend / "observation.c128").exists():
            raise AssertionError("sampled exporter rejected or omitted active ADC/DAC/frontend samples")

        rooted = root / "fresh-sampled-independent-roots"
        rooted_manifest = export_sampled(rooted, "--channel-seed", "81", "--oscillator-seed", "82",
                                         "--noise-seed", "83", "--frontend-seed", "84")
        roots = rooted_manifest["random_seed_roots"]
        if (roots["channel"], roots["oscillator"], roots["noise"], roots["frontend"]) != (81, 82, 83, 84):
            raise AssertionError(f"explicit independent RNG roots were not applied/reported: {roots}")
        if not all(roots[f"{name}_explicit"] for name in ("oscillator", "noise", "frontend")):
            raise AssertionError("manifest omitted explicit independent-root flags")
        if (rooted / "basis.c128").read_bytes() != (sampled / "basis.c128").read_bytes():
            raise AssertionError("physical-channel seed overrides changed the separately keyed public basis")

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
        if (stats / "received_energy.f64").exists():
            raise AssertionError("received-energy output must require --stats-noisy")
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

        # The grouped export keeps each timing hypothesis in its own small
        # residual-calibration space while scoring the exact same sampled
        # observation. Compare every group with the corresponding principal
        # submatrix of the existing full-bank export.
        grouped_options = ("--start", "0", ".002", ".001", "--frequency", "0", ".1", ".1",
                           "--basis", "two-ray")
        full_bank = root / "full-bank"
        full_bank_manifest = export_stats(full_bank, *grouped_options)
        grouped = root / "grouped-bank"
        grouped_manifest = export_group_stats(grouped, *grouped_options)
        if grouped_manifest["format"] != "triad-group-block-stats-v1" or grouped_manifest["dimension"] != 4:
            raise AssertionError(f"unexpected grouped block-statistics schema: {grouped_manifest}")
        if grouped_manifest["group_count"] != 6 or len(grouped_manifest["groups"]) != 6:
            raise AssertionError(f"synchronization groups do not match the public timing/frequency grid: {grouped_manifest['group_count']}")
        if grouped_manifest["cross_group_gram_saved"] or grouped_manifest["conditional_noise_resampling_supported"]:
            raise AssertionError("grouped metadata overstates saved cross-group statistics")
        for file_name in ("intervals.u64", "valid_rows.u64", "signal_energy.f64"):
            if (grouped/file_name).read_bytes() != (full_bank/file_name).read_bytes():
                raise AssertionError(f"grouping changed shared capture calendar/statistic {file_name}")
        old_dimension = full_bank_manifest["dimension"]
        group_dimension = grouped_manifest["dimension"]
        old_gram = complex128_values(full_bank/"gram.c128")
        old_mean = complex128_values(full_bank/"mean.c128")
        new_gram = complex128_values(grouped/"group_gram.c128")
        new_mean = complex128_values(grouped/"group_mean.c128")
        if len(new_gram) != grouped_manifest["block_count"] * 6 * 16 or len(new_mean) != grouped_manifest["block_count"] * 6 * 4:
            raise AssertionError("grouped Gram/mean files differ from manifest extents")
        for group_index, group in enumerate(grouped_manifest["groups"]):
            candidates = [grouped_manifest["candidates"][i] for i in group["candidate_indices"]]
            key = group["sync_key"]
            for candidate in candidates:
                if any(candidate[field] != key[field] for field in key):
                    raise AssertionError("a group combines candidates with different public synchronization parameters")
            if len(candidates) != 4 or candidates[0]["bit"] != 0 or candidates[1]["bit"] != 1:
                raise AssertionError(f"group omitted one bit/channel alternative: {group['candidate_indices']}")
            if group["column_mapping"] != [[0], [1], [0, 2], [1, 3]]:
                raise AssertionError(f"two-ray group columns were not locally deduplicated: {group['column_mapping']}")
            # Select old global columns using each public candidate's mapping;
            # their unique order must agree with the group's local descriptors.
            old_local_columns = []
            for local_index in range(group_dimension):
                matching = [
                    global_column
                    for candidate_index, local_mapping in zip(group["candidate_indices"], group["column_mapping"])
                    for local_column, global_column in zip(local_mapping, full_bank_manifest["column_mapping"][candidate_index])
                    if local_column == local_index
                ]
                if not matching or any(value != matching[0] for value in matching):
                    raise AssertionError("local grouped column maps inconsistently to the full-bank projection")
                old_local_columns.append(matching[0])
            for block_index in range(grouped_manifest["block_count"]):
                for i, old_i in enumerate(old_local_columns):
                    new_mean_index = (block_index*6+group_index)*group_dimension+i
                    old_mean_index = block_index*old_dimension+old_i
                    if abs(new_mean[new_mean_index]-old_mean[old_mean_index]) > 1e-10*max(1.,abs(old_mean[old_mean_index])):
                        raise AssertionError("grouped projection differs from the existing full-bank observation")
                    for j, old_j in enumerate(old_local_columns):
                        new_gram_index = ((block_index*6+group_index)*group_dimension+i)*group_dimension+j
                        old_gram_index = (block_index*old_dimension+old_i)*old_dimension+old_j
                        if abs(new_gram[new_gram_index]-old_gram[old_gram_index]) > 1e-10*max(1.,abs(old_gram[old_gram_index])):
                            raise AssertionError("grouped Gram differs from the full-bank principal submatrix")

        grouped_noisy = root / "grouped-bank-noisy"
        grouped_noisy_manifest = export_group_stats(grouped_noisy, *grouped_options, "--stats-noisy")
        full_noisy = root / "full-bank-noisy"
        full_noisy_manifest = export_stats(full_noisy, *grouped_options, "--stats-noisy")
        if grouped_noisy_manifest["sample_count"] != full_noisy_manifest["sample_count"]:
            raise AssertionError("grouped noisy capture calendar differs from the full-bank capture")
        grouped_received_energy = struct.unpack("<"+"d"*(grouped_noisy_manifest["block_count"]), (grouped_noisy/"received_energy.f64").read_bytes())
        full_received_energy = struct.unpack("<"+"d"*(full_noisy_manifest["block_count"]), (full_noisy/"received_energy.f64").read_bytes())
        if any(abs(a-b) > 1e-10*max(1., abs(b)) for a, b in zip(grouped_received_energy, full_received_energy)):
            raise AssertionError("grouped actual-noisy scoring did not use one common received-energy stream")
        group_observation = complex128_values(grouped_noisy/"group_observation.c128")
        full_observation = complex128_values(full_noisy/"observation.c128")
        for group_index, group in enumerate(grouped_noisy_manifest["groups"]):
            local_old_columns = []
            for local_index in range(group_dimension):
                matching = [
                    full_noisy_manifest["column_mapping"][candidate_index][position]
                    for candidate_index, local_mapping in zip(group["candidate_indices"], group["column_mapping"])
                    for position, local_column in enumerate(local_mapping) if local_column == local_index
                ]
                if not matching or any(value != matching[0] for value in matching):
                    raise AssertionError("noisy grouped projection does not have a unique source column")
                local_old_columns.append(matching[0])
            for block_index in range(grouped_noisy_manifest["block_count"]):
                for local_index, old_column in enumerate(local_old_columns):
                    grouped_index = (block_index*6+group_index)*group_dimension+local_index
                    old_index = block_index*full_noisy_manifest["dimension"]+old_column
                    if abs(group_observation[grouped_index]-full_observation[old_index]) > 1e-10*max(1.,abs(full_observation[old_index])):
                        raise AssertionError("grouped projection differs from the shared actual noisy observation")

        direct_grouped = root / "grouped-direct-sampled"
        direct_grouped_manifest = export_sampled(direct_grouped, *grouped_options)
        direct_basis = complex128_values(direct_grouped/"basis.c128")
        direct_y = complex128_values(direct_grouped/"observation.c128")
        direct_valid = (direct_grouped/"validity.u8").read_bytes()
        candidate_count, sample_count, direct_columns = direct_grouped_manifest["basis_shape"]
        if sample_count != grouped_noisy_manifest["sample_count"] or candidate_count != grouped_noisy_manifest["candidate_count"]:
            raise AssertionError("sampled direct-basis fixture differs from grouped observation geometry")
        for group_index, group in enumerate(grouped_manifest["groups"]):
            local_basis = [None] * group_dimension
            for candidate_index, mapping in zip(group["candidate_indices"], group["column_mapping"]):
                for basis_column, local_index in enumerate(mapping):
                    if local_basis[local_index] is None:
                        local_basis[local_index] = (candidate_index, basis_column)
            if any(value is None for value in local_basis):
                raise AssertionError("sampled templates do not cover every grouped local column")
            for block_index, (first_sample, stop_sample) in enumerate(
                    struct.iter_unpack("<QQ", (grouped_noisy/"intervals.u64").read_bytes())):
                for i, (candidate_i, column_i) in enumerate(local_basis):
                    offset_i = (candidate_i*sample_count+first_sample)*direct_columns+column_i
                    template_i = direct_basis[offset_i:(candidate_i*sample_count+stop_sample)*direct_columns:direct_columns]
                    expected_observation = sum(a.conjugate()*y for a,y,valid in zip(
                        template_i,direct_y[first_sample:stop_sample],direct_valid[first_sample:stop_sample]) if valid)
                    grouped_index = (block_index*6+group_index)*group_dimension+i
                    if abs(expected_observation-group_observation[grouped_index]) > 2e-10*max(1.,abs(expected_observation)):
                        raise AssertionError("grouped actual observation projection differs from direct sampled templates")
                    for j, (candidate_j, column_j) in enumerate(local_basis):
                        offset_j = (candidate_j*sample_count+first_sample)*direct_columns+column_j
                        template_j = direct_basis[offset_j:(candidate_j*sample_count+stop_sample)*direct_columns:direct_columns]
                        expected_gram = sum(a.conjugate()*b for a,b,valid in zip(
                            template_i,template_j,direct_valid[first_sample:stop_sample]) if valid)
                        grouped_gram_index = ((block_index*6+group_index)*group_dimension+i)*group_dimension+j
                        if abs(expected_gram-new_gram[grouped_gram_index]) > 2e-10*max(1.,abs(expected_gram)):
                            raise AssertionError("grouped Gram differs from direct sampled templates")

        grouped_chunked = root / "grouped-bank-chunked"
        export_group_stats(grouped_chunked, *grouped_options, "--chunk", "511")
        for file_name in ("intervals.u64", "valid_rows.u64", "signal_energy.f64"):
            if (grouped/file_name).read_bytes() != (grouped_chunked/file_name).read_bytes():
                raise AssertionError(f"grouped chunking changed shared output {file_name}")
        for file_name in ("group_gram.c128", "group_mean.c128"):
            left, right = complex128_values(grouped/file_name), complex128_values(grouped_chunked/file_name)
            if len(left) != len(right) or any(abs(a-b) > 1e-11*max(1.,abs(a),abs(b)) for a,b in zip(left,right)):
                raise AssertionError(f"grouped chunking changed {file_name}")

        grouped_erased = root / "grouped-erased"
        erased_group_manifest = export_group_stats(grouped_erased, *grouped_options,
                                                   "--dropout-ms", "40", "--dropout-start-ms", "100")
        erased_valid = struct.unpack("<"+"Q"*erased_group_manifest["block_count"],
                                     (grouped_erased/"valid_rows.u64").read_bytes())
        if not any(value < stop-first for value,(first,stop) in zip(erased_valid,
                    struct.iter_unpack("<QQ",(grouped_erased/"intervals.u64").read_bytes()))):
            raise AssertionError("grouped export omitted configured erasure rows")

        over_budget = root / "grouped-over-budget"
        rejected_group = invoke("--mode", "export-group-stats", *COMMON, "--channel", "none",
                                "--basis", "two-ray", "--start", "0", "10", ".01",
                                "--export-max-mib", "1", "--output", str(over_budget), ok=False, timeout=20)
        if "disk budget" not in rejected_group.stderr or over_budget.exists():
            raise AssertionError(f"grouped disk preflight ran too late or failed unexpectedly: {rejected_group.stderr}")
        group_collision = invoke("--mode", "export-group-stats", *COMMON, "--basis", "two-ray",
                                 "--output", str(grouped), ok=False, timeout=20)
        if "fresh output directory" not in group_collision.stderr:
            raise AssertionError(f"grouped output collision failed unexpectedly: {group_collision.stderr}")

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

        # Match noisy block energies against the independent raw sampled export
        # on the same no-effect, nonzero-thermal-noise input and validity mask.
        received = root / "fresh-stats-received-energy"
        received_manifest = export_stats(received, "--stats-noisy")
        if received_manifest["noise_variance"] <= 0:
            raise AssertionError("received-energy fixture must use nonzero thermal noise")
        if received_manifest["received_energy_shape"] != [received_manifest["block_count"]]:
            raise AssertionError("received-energy shape differs from public block count")
        received_values = list(struct.iter_unpack("<d", (received / "received_energy.f64").read_bytes()))
        if len(received_values) != received_manifest["block_count"]:
            raise AssertionError("received-energy file length differs from declared shape")
        sampled_values = complex128_values(sampled / "observation.c128")
        sampled_validity = (sampled / "validity.u8").read_bytes()
        received_intervals = list(struct.iter_unpack("<QQ", (received / "intervals.u64").read_bytes()))
        for block_index, (first_sample, stop_sample) in enumerate(received_intervals):
            expected_energy = sum(value.real * value.real + value.imag * value.imag for value, valid in
                                  zip(sampled_values[first_sample:stop_sample],
                                      sampled_validity[first_sample:stop_sample]) if valid)
            actual_energy = received_values[block_index][0]
            if not math.isclose(actual_energy, expected_energy, rel_tol=2e-13, abs_tol=1e-12):
                raise AssertionError(f"received noisy block energy differs from raw sampled fixture: block={block_index}, got={actual_energy}, expected={expected_energy}")

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

        transformed = pathlib.Path(scratch) / "frontend-stats"
        transformed_manifest = export_stats(transformed, "--stats-noisy", "--cn0", "-4",
                                             "--dac-bits", "16", "--dac-enob", "14",
                                             "--adc-bits", "16", "--adc-enob", "12",
                                             "--adc-full-scale", "1", "--adc-gain", ".2",
                                             "--adc-dither-lsb", ".5", "--rx-lowpass-hz", "100")
        if transformed_manifest["gaussian_projection_supported"] or not transformed_manifest["stats_noisy"]:
            raise AssertionError("nonlinear actual observation export lost its projection restriction")
        if not transformed_manifest["frontend_report"]["adc_clipped_components"]:
            raise AssertionError("noisy transformed export did not report sampled clipping")
        if not (transformed / "observation.c128").exists():
            raise AssertionError("nonlinear export omitted actual sampled observation")
        rejected_export = invoke("--mode", "export", *COMMON, "--adc-bits", "16",
                                 "--output", str(pathlib.Path(scratch)/"invalid-frontend-raw"), ok=False)
        if "export-stats --stats-noisy" not in rejected_export.stderr:
            raise AssertionError("raw export allowed post-ADC Gaussian noise generation")

    for option in (("--signal-bit", "256"), ("--threads", "4294967296"),
                   ("--chunk", "-1"), ("--adc-bits", "1"),
                   ("--adc-full-scale", "0"), ("--dac-enob", "14")):
        invoke("--mode", "plan", *option, ok=False, timeout=10)
    invoke("--mode", "plan", "--trials", "2", "--oscillator-seed", str((1 << 64) - 1), ok=False, timeout=10)
    invoke("--mode", "plan", "--adc-bits", "16", ok=False, timeout=10)

    # Exercise the full 38-day public capture calendar without generating a
    # month of data: explicit sample preflight must reject before any output.
    # Pulse/tile/tail overhead is allowed beyond the requested active ceiling.
    with tempfile.TemporaryDirectory(prefix="triad-month-preflight-") as scratch:
        destination = pathlib.Path(scratch)/"month"
        rejected = invoke("--mode", "export-sampled", *COMMON,
                          "--rate", ".036", "--added", "1", "--sample-rate", "1",
                          "--seconds", "3283200", "--channel", "none", "--rf-hz", "8270",
                          "--export-max-samples", "1", "--output", str(destination), ok=False)
        if "exceeds --export-max-samples" not in rejected.stderr or destination.exists():
            raise AssertionError(f"month capture failed before the explicit export preflight: {rejected.stderr}")

    # All export paths must agree on actual reciprocal-rate coordinates and
    # C/N0 normalization. This renders one minimum private tile (~3k rows),
    # not a year of samples, and compares actual noisy block energy to I/Q.
    with tempfile.TemporaryDirectory(prefix="triad-subhz-exports-") as scratch:
        root = pathlib.Path(scratch)
        options = ("--rate", ".0036", "--added", "1", "--sample-rate", "1",
                   "--sample-rate-denominator", "64", "--block-seconds", "4096",
                   "--cn0", "-60", "--basis", "two-ray", "--tap-delay-ms", ".005",
                   "--tap-doppler-hz", ".001", "--spread-preset", "sub9-best")
        raw = export(root/"raw", *options)
        sampled = export_sampled(root/"sampled", *options)
        stats = export_stats(root/"stats", *options, "--stats-noisy")
        for manifest in (raw, sampled, stats):
            if (manifest["sample_rate"], manifest["sample_rate_numerator"], manifest["sample_rate_denominator"]) != (1/64, 1, 64):
                raise AssertionError("export lost reciprocal sample-rate provenance")
            noise = manifest.get("noise_variance", manifest.get("noise_variance_pre_frontend"))
            if not math.isclose(noise, (1/64)*1e6, rel_tol=1e-12):
                raise AssertionError("reciprocal sample noise no longer represents declared total C/N0")
        if raw["signal_shape"][0] != sampled["sample_count"] or sampled["sample_count"] != stats["sample_count"]:
            raise AssertionError("export calendars disagree at reciprocal Fs")
        if sampled["block_samples"] != 64 or stats["block_samples"] != 64:
            raise AssertionError("physical block duration was treated as a numerator-rate duration")
        iq = complex128_values(root/"sampled"/"observation.c128")
        energies = [v[0] for v in struct.iter_unpack("<d", (root/"stats"/"received_energy.f64").read_bytes())]
        expected = [sum(abs(z)**2 for z in iq[start:start+64]) for start in range(0, len(iq), 64)]
        if len(energies) != len(expected) or any(not math.isclose(a, b, rel_tol=2e-12, abs_tol=1e-8)
                                                for a, b in zip(energies, expected)):
            raise AssertionError("reciprocal actual block energies differ from sampled waveform")
        rejected = invoke("--mode", "export-stats", *COMMON, "--rate", ".000036", "--added", "1",
                          "--sample-rate", "1", "--sample-rate-denominator", "64", "--seconds", "86400000",
                          "--export-max-samples", "1", "--output", str(root/"years"), ok=False)
        if "exceeds --export-max-samples" not in rejected.stderr or (root/"years").exists():
            raise AssertionError(f"year reciprocal capture missed bounded preflight: {rejected.stderr}")

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
