#!/usr/bin/env python3
"""Batched matched-subspace calibration for exported triad long captures.

This measures a finite raw-sample basis test. It is not the modem receiver and
does not establish field sensitivity. See --help for the input contract.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import pathlib
import platform
import struct
import sys
import time
from typing import Any, Iterable

import torch


SCHEMA = "triad-long-basis-v1"
OUTPUT_SCHEMA = "triad-gpu-study-v2"
NEAR_BOUNDARY = 0.05
PROJECTOR_WORKSPACE_FACTOR = 16
CALIBRATION_SALT = 0x43414C4942524154
HOLDOUT_SALT = 0x484F4C444F555421
POSITIVE_SALT = 0x504F534954495645
NULL_SALT = 0x4E554C4C4E4F4953
MASK64 = (1 << 64) - 1


def _splitmix64(value: int) -> int:
    value = (value + 0x9E3779B97F4A7C15) & MASK64
    value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & MASK64
    return (value ^ (value >> 31)) & MASK64


def _integer(value: str, label: str, low: int, high: int) -> int:
    if not value or value[0] == "-" or not value.isdecimal():
        raise argparse.ArgumentTypeError(f"{label} must be a base-10 integer")
    result = int(value)
    if result < low or result > high:
        raise argparse.ArgumentTypeError(f"{label} must be in [{low}, {high}]")
    return result


def _positive_int(text: str) -> int:
    return _integer(text, "value", 1, (1 << 63) - 1)


def _memory_gib(text: str) -> float:
    value = _finite_float(text)
    if value <= 0 or value > 1024:
        raise argparse.ArgumentTypeError("memory-gib must be in (0, 1024]")
    return value


def _finite_float(text: str) -> float:
    try:
        value = float(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("expected a number") from exc
    if not math.isfinite(value):
        raise argparse.ArgumentTypeError("value must be finite")
    return value


def _parse_cn0_grid(text: str) -> list[float]:
    values = [_finite_float(part.strip()) for part in text.split(",")]
    if not values or any(not math.isfinite(value) for value in values):
        raise argparse.ArgumentTypeError("--cn0-grid must be a comma-separated finite list")
    if len(set(values)) != len(values):
        raise argparse.ArgumentTypeError("--cn0-grid values must be unique")
    return values


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Run raw shaped-waveform matched-subspace Monte Carlo on CPU or CUDA.",
        epilog=(
            "Input directory: manifest.json, basis.c64 (complex64 row-major [C,S,K]), and "
            "signal.c64 (complex64 [S]). The manifest uses schema triad-long-basis-v1 and "
            "contains sample_rate, noise_reference_power, cn0_db_hz, bitlabels, candidateparams, "
            "optional true_candidate_index, source_hash and signal_start_samples. Optional "
            "carrier_basis.c64 has complex64 [2,3,N]. --cn0-grid sets sample variance from "
            "noise_reference_power*sample_rate/10^(C/N0/10); measured signal power is never "
            "used to normalize fading or outages away. Calibration and holdout use disjoint "
            "seed domains. GPU execution is optional; no cloud instance is created."
        ),
    )
    p.add_argument("manifest", type=pathlib.Path, help="artifact directory or manifest.json")
    p.add_argument("--device", default="cpu", help="cpu, cuda, or cuda:N (default cpu)")
    p.add_argument("--trials", type=lambda s: _integer(s, "trials", 1, 10_000_000), default=128,
                   help="independent horizons per split before sharding")
    p.add_argument("--batch", type=_positive_int, default=8, help="Monte Carlo trial batch, reduced to fit memory cap")
    p.add_argument("--seed", type=lambda s: _integer(s, "seed", 0, MASK64), default=1)
    p.add_argument("--cn0-grid", type=_parse_cn0_grid, help="comma-separated C/N0 dB-Hz overrides")
    p.add_argument("--duration-label", default="manifest", help="label copied to results, e.g. 14.4s or 1h")
    p.add_argument("--output", type=pathlib.Path, default=None, help="JSONL path (default stdout)")
    p.add_argument("--benchmark", action="store_true", help="include timing and throughput fields")
    p.add_argument("--reference-trials", type=lambda s: _integer(s, "reference-trials", 0, 10_000_000), default=32,
                   help="first global trial indices checked with CPU FP64 per split; set to --trials for full parity")
    p.add_argument("--noise-device", choices=("selected", "cpu"), default="selected",
                   help="generate seeded AWGN on the selected device (default) or CPU")
    p.add_argument("--rank-relative-tolerance", type=_finite_float, default=1e-6,
                   help="fixed relative rank tolerance, independent of observation length")
    p.add_argument("--duration-alternatives", type=_positive_int, default=1,
                   help="predeclared duration alternatives included in the analytic family budget")
    p.add_argument("--family-hypotheses", type=_positive_int,
                   help="optional larger complete operational family count; never less than the exercised bank")
    p.add_argument("--memory-gib", type=_memory_gib, default=1.0, help="workspace ceiling, GiB (default 1)")
    p.add_argument("--attempts", type=lambda s: _integer(s, "attempts", 1, 100_000), default=1,
                   help="sliding capture starts per independent horizon")
    p.add_argument("--stride-samples", type=lambda s: _integer(s, "stride-samples", 1, 1_000_000_000_000),
                   help="attempt spacing; defaults to non-overlap at S samples")
    p.add_argument("--false-alarm-budget", type=_finite_float, default=0.01,
                   help="family-wise null admission budget for the finite candidate×attempt family")
    p.add_argument("--threshold", choices=("exact", "conservative"), default="exact",
                   help="fixed-rank Gamma tail quantile or conservative concentration bound (default exact)")
    p.add_argument("--shard-index", type=lambda s: _integer(s, "shard-index", 0, (1 << 31) - 1), default=0)
    p.add_argument("--shard-count", type=_positive_int, default=1,
                   help="independent process shards; trial indices are shard-index + k*shard-count")
    return p


def _load_manifest(path: pathlib.Path) -> tuple[pathlib.Path, dict[str, Any]]:
    directory = path if path.is_dir() else path.parent
    manifest_path = directory / "manifest.json" if path.is_dir() else path
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"), parse_constant=_reject_json_constant)
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read manifest {manifest_path}: {exc}") from exc
    if not isinstance(manifest, dict) or manifest.get("format") != SCHEMA:
        raise ValueError(f"manifest format must be {SCHEMA!r}")
    if manifest.get("byte_order", "native") not in ("native", sys.byteorder):
        raise ValueError("basis byte order does not match this host")
    if manifest.get("complex_type", "complex64") != "complex64":
        raise ValueError("only interleaved complex64 input is supported")
    return directory, manifest


def _reject_json_constant(value: str) -> None:
    raise ValueError(f"manifest contains non-standard JSON number {value}")


def _shape(manifest: dict[str, Any], key: str, rank: int) -> tuple[int, ...]:
    value = manifest.get(key)
    if not isinstance(value, list) or len(value) != rank:
        raise ValueError(f"manifest {key} must be an array of {rank} positive integers")
    dims: list[int] = []
    for dim in value:
        if isinstance(dim, bool) or not isinstance(dim, int) or dim <= 0 or dim > (1 << 40):
            raise ValueError(f"manifest {key} contains an invalid dimension")
        dims.append(dim)
    return tuple(dims)


def _complex64_memmap(path: pathlib.Path, shape: tuple[int, ...]) -> torch.Tensor:
    if struct.pack("=I", 1) != b"\x01\0\0\0":
        raise ValueError("native complex64 arrays require a little-endian host")
    count = math.prod(shape)
    expected = count * 8
    try:
        actual = path.stat().st_size
    except OSError as exc:
        raise ValueError(f"cannot stat array {path}: {exc}") from exc
    if actual != expected:
        raise ValueError(f"{path} has {actual} bytes; shape {shape} requires {expected}")
    try:
        raw = torch.from_file(str(path), shared=False, size=count * 2, dtype=torch.float32)
    except (RuntimeError, OSError) as exc:
        raise ValueError(f"cannot map complex64 array {path}: {exc}") from exc
    return raw.view(torch.complex64).reshape(shape)


def _validate_manifest(directory: pathlib.Path, m: dict[str, Any]) -> dict[str, Any]:
    if m.get("format") != SCHEMA:
        raise ValueError(f"manifest format must be {SCHEMA!r}")
    basis_shape = _shape(m, "basis_shape", 3)
    signal_shape = _shape(m, "signal_shape", 1)
    candidates, samples, columns = basis_shape
    if signal_shape[0] != samples:
        raise ValueError("signal sample length must equal basis_shape[1]")
    if columns > samples:
        raise ValueError("basis column count cannot exceed the sample count")
    sample_rate = m.get("sample_rate")
    if isinstance(sample_rate, bool) or not isinstance(sample_rate, (int, float)) or not math.isfinite(sample_rate) or sample_rate <= 0:
        raise ValueError("manifest sample_rate must be finite and positive")
    noise_reference = m.get("noise_reference_power", 1.0)
    if isinstance(noise_reference, bool) or not isinstance(noise_reference, (int, float)) or not math.isfinite(noise_reference) or noise_reference <= 0:
        raise ValueError("noise_reference_power must be finite and positive")
    if "cn0_db_hz" not in m and "noise_variance" not in m:
        raise ValueError("manifest must contain cn0_db_hz or noise_variance")
    if "cn0_db_hz" in m and (isinstance(m["cn0_db_hz"], bool) or
            not isinstance(m["cn0_db_hz"], (int, float)) or not math.isfinite(m["cn0_db_hz"])):
        raise ValueError("manifest cn0_db_hz must be finite")
    if "noise_variance" in m and (not isinstance(m["noise_variance"], (int, float)) or not math.isfinite(m["noise_variance"]) or m["noise_variance"] <= 0):
        raise ValueError("manifest noise_variance must be finite and positive")
    labels, params = m.get("bitlabels"), m.get("candidateparams")
    if not isinstance(labels, list) or len(labels) != candidates:
        raise ValueError("bitlabels must contain one label per candidate")
    if any(isinstance(label, bool) or label not in (0, 1) for label in labels):
        raise ValueError("bitlabels must contain binary source-bit values")
    if not isinstance(params, list) or len(params) != candidates or any(not isinstance(x, dict) for x in params):
        raise ValueError("candidateparams must contain one object per candidate")
    true_index = m.get("true_candidate_index")
    if true_index is not None and (isinstance(true_index, bool) or not isinstance(true_index, int) or not 0 <= true_index < candidates):
        raise ValueError("true_candidate_index is outside candidate bank")
    signal_bit = m.get("signal_bit", labels[true_index] if true_index is not None else None)
    if signal_bit is not None and (isinstance(signal_bit, bool) or signal_bit not in (0, 1)):
        raise ValueError("signal_bit must be a binary source-bit value")
    signal_start = m.get("signal_start_samples", 0)
    if isinstance(signal_start, bool) or not isinstance(signal_start, int) or signal_start < 0:
        raise ValueError("signal_start_samples must be a nonnegative integer")
    basis_name = m.get("basis_file", "basis.c64")
    signal_name = m.get("signal_file", "signal.c64")
    if not isinstance(basis_name, str) or not basis_name or not isinstance(signal_name, str) or not signal_name:
        raise ValueError("basis_file and signal_file must be nonempty relative paths")
    basis_path, signal_path = (directory / basis_name).resolve(), (directory / signal_name).resolve()
    if directory.resolve() not in basis_path.parents or directory.resolve() not in signal_path.parents:
        raise ValueError("array paths must remain inside the artifact directory")
    result: dict[str, Any] = {
        "basis": _complex64_memmap(basis_path, basis_shape),
        "signal": _complex64_memmap(signal_path, signal_shape),
        "basis_shape": basis_shape,
        "sample_rate": float(sample_rate),
        "noise_reference_power": float(noise_reference),
        "bitlabels": labels,
        "candidateparams": params,
        "true_candidate_index": true_index,
        "signal_bit": signal_bit,
        "signal_start_samples": signal_start,
    }
    carrier_name = m.get("carrier_basis_file")
    carrier_shape = m.get("carrier_basis_shape")
    if carrier_name is not None or carrier_shape is not None:
        if not isinstance(carrier_name, str) or not carrier_name:
            raise ValueError("carrier_basis_file is required with carrier_basis_shape")
        carrier_dims = _shape(m, "carrier_basis_shape", 3)
        if carrier_dims[0:2] != (2, 3):
            raise ValueError("carrier basis must have shape [2,3,N]")
        carrier_path = (directory / carrier_name).resolve()
        if directory.resolve() not in carrier_path.parents:
            raise ValueError("carrier basis path must remain inside the artifact directory")
        result["carrier_basis"] = _complex64_memmap(carrier_path, carrier_dims)
        result["carrier_basis_shape"] = carrier_dims
    result["source_hash"] = m.get("source_hash")
    result["manifest"] = m
    return result


def _resolve_device(name: str) -> torch.device:
    if name == "cpu":
        return torch.device("cpu")
    if name == "cuda":
        index = 0
    elif name.startswith("cuda:"):
        try:
            index = int(name.split(":", 1)[1])
        except ValueError as exc:
            raise ValueError("CUDA device must be cuda or cuda:N") from exc
    else:
        raise ValueError("device must be cpu, cuda or cuda:N")
    if not torch.cuda.is_available():
        raise ValueError("CUDA was requested but PyTorch reports no available CUDA device")
    if index < 0 or index >= torch.cuda.device_count():
        raise ValueError(f"CUDA device index {index} is unavailable")
    return torch.device("cuda", index)


def _configure_torch() -> None:
    if hasattr(torch.backends, "cuda") and hasattr(torch.backends.cuda, "matmul"):
        torch.backends.cuda.matmul.allow_tf32 = False
    if hasattr(torch.backends, "cudnn"):
        torch.backends.cudnn.allow_tf32 = False
    if hasattr(torch, "set_float32_matmul_precision"):
        torch.set_float32_matmul_precision("highest")


def _device_warmup(device: torch.device) -> float:
    start = time.perf_counter()
    if device.type == "cuda":
        basis = torch.ones((1, 32, 2), dtype=torch.complex64, device=device)
        q, _ = _modified_gram_schmidt(basis)
        sample = torch.ones((1, 32), dtype=torch.complex64, device=device)
        _ = torch.matmul(q.conj().transpose(1, 2).unsqueeze(0), sample[:, None, :, None])
        torch.cuda.synchronize(device)
    return time.perf_counter() - start


def _modified_gram_schmidt(basis: torch.Tensor, relative_tolerance: float = 1e-6) -> tuple[torch.Tensor, torch.Tensor]:
    """Reorthogonalized batched MGS for [candidate,sample,column] matrices."""
    if not bool(torch.isfinite(basis.real).all().item()) or not bool(torch.isfinite(basis.imag).all().item()):
        raise ValueError("basis contains NaN or infinity")
    count, sample_count, columns = basis.shape
    q = torch.zeros_like(basis)
    ranks = torch.zeros(count, dtype=torch.int64, device=basis.device)
    original_norm = torch.linalg.vector_norm(basis, dim=1)
    scale = original_norm.amax(dim=1).clamp_min(torch.finfo(basis.real.dtype).tiny)
    # Rank is a relative property of the few channel columns, not of the
    # number of raw observations. eps*N made every FP32 column vanish for
    # observations longer than 2**23 samples. FP64 uses the SAME declared
    # numerical rank policy; rank disagreement triggers an explicit fallback.
    tolerance = max(relative_tolerance, 8 * torch.finfo(basis.real.dtype).eps) * scale
    for j in range(columns):
        v = basis[:, :, j].clone()
        if j:
            previous = q[:, :, :j]
            for _ in range(2):
                coeff = torch.sum(previous.conj() * v[:, :, None], dim=1)
                v -= torch.sum(previous * coeff[:, None, :], dim=2)
        norm = torch.linalg.vector_norm(v, dim=1)
        valid = norm > tolerance
        q[:, :, j] = torch.where(valid[:, None], v / norm.clamp_min(torch.finfo(v.real.dtype).tiny)[:, None], 0)
        ranks += valid.to(torch.int64)
    return q, ranks


def _subspace_distances(reference: torch.Tensor, selected: torch.Tensor) -> torch.Tensor:
    """Frobenius distances between fixed QQ^H projectors, without N-by-N arrays.

    Both inputs are complex128 on one device. This detects a wrong but still
    orthonormal FP32 subspace when numerical ranks happen to agree.
    """
    reference_gram = reference.conj().transpose(1, 2) @ reference
    selected_gram = selected.conj().transpose(1, 2) @ selected
    overlap = reference.conj().transpose(1, 2) @ selected
    squared = (reference_gram.abs().square().sum(dim=(1, 2)) +
               selected_gram.abs().square().sum(dim=(1, 2)) -
               2 * overlap.abs().square().sum(dim=(1, 2)))
    return squared.clamp_min(0).sqrt()


def _candidate_batch_size(samples: int, columns: int, horizon: int, memory_bytes: int, requested_batch: int) -> tuple[int, int]:
    # Reserve conservative workspace for simultaneous FP64 CPU reference and
    # selected-device MGS matrices plus a batched noise/observation horizon.
    q_per_candidate = max(1, samples * columns * 16 * PROJECTOR_WORKSPACE_FACTOR)
    obs_per_trial = max(1, horizon * 8 * 8)
    half = memory_bytes // 2
    candidate_batch = max(1, half // q_per_candidate)
    trial_batch = max(1, min(requested_batch, half // obs_per_trial))
    def required_bytes() -> int:
        # GEMM output, energies and reduction scratch depend on BOTH tiling
        # axes; omitting this term is unsafe for many short candidates.
        return (candidate_batch * q_per_candidate + trial_batch * obs_per_trial +
                candidate_batch * trial_batch * columns * 32)
    while required_bytes() > memory_bytes and (candidate_batch > 1 or trial_batch > 1):
        if candidate_batch > trial_batch and candidate_batch > 1:
            candidate_batch = max(1, candidate_batch // 2)
        else:
            trial_batch = max(1, trial_batch // 2)
    required = required_bytes()
    if required > memory_bytes:
        raise ValueError("memory cap is too small for one candidate and one trial workspace")
    return int(candidate_batch), int(trial_batch)


def _noise_horizon(length: int, variance: float, seed: int, device: torch.device) -> torch.Tensor:
    generator = torch.Generator(device=device)
    generator.manual_seed(seed & ((1 << 63) - 1))
    scale = math.sqrt(variance / 2.0)
    real = torch.randn(length, dtype=torch.float32, generator=generator, device=device) * scale
    imag = torch.randn(length, dtype=torch.float32, generator=generator, device=device) * scale
    return torch.complex(real, imag)


def _seed(base: int, split: str, trial_index: int, noise_kind: str) -> int:
    split_salt = CALIBRATION_SALT if split == "calibration" else HOLDOUT_SALT
    kind_salt = POSITIVE_SALT if noise_kind == "positive" else NULL_SALT
    return _splitmix64(base ^ split_salt ^ _splitmix64(trial_index) ^ kind_salt)


def _make_observations(
    trial_indices: list[int], split: str, base_seed: int, horizon: int,
    signal: torch.Tensor, signal_start: int, variance: float,
) -> tuple[torch.Tensor, torch.Tensor]:
    positive_rows: list[torch.Tensor] = []
    null_rows: list[torch.Tensor] = []
    for index in trial_indices:
        noise_pos = _noise_horizon(horizon, variance, _seed(base_seed, split, index, "positive"), signal.device)
        noise_null = _noise_horizon(horizon, variance, _seed(base_seed, split, index, "null"), signal.device)
        positive = noise_pos
        # The noise tensor owns storage; clone before inserting the noiseless
        # sample signal so overlapping starts observe the same physical noise.
        positive = positive.clone()
        positive[signal_start:signal_start + signal.numel()] += signal
        positive_rows.append(positive)
        null_rows.append(noise_null)
    return torch.stack(positive_rows), torch.stack(null_rows)


def _score_windows(
    q_t: torch.Tensor, observations: torch.Tensor, starts: list[int],
    sample_count: int, variance: float, threshold: float,
) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor]:
    batch = observations.shape[0]
    best_stat = torch.full((batch,), -torch.inf, dtype=torch.float64 if observations.dtype == torch.complex128 else torch.float32, device=observations.device)
    best_energy = torch.zeros_like(best_stat)
    best_candidate = torch.full((batch,), -1, dtype=torch.int64, device=observations.device)
    best_attempt = torch.full((batch,), -1, dtype=torch.int64, device=observations.device)
    candidates, columns, _ = q_t.shape
    # One GEMM shares template reads across the trial batch. Broadcasting a
    # [candidate,column,sample] matrix for each trial instead dispatched GEMVs.
    matrix = q_t.reshape(candidates * columns, sample_count).transpose(0, 1)
    for attempt, start in enumerate(starts):
        window = observations[:, start:start + sample_count]
        projected = (window @ matrix).reshape(batch, candidates, columns)
        energy = torch.sum(projected.real.square() + projected.imag.square(), dim=-1) / variance
        stat = energy - threshold
        value, candidate = stat.max(dim=1)
        update = value > best_stat
        best_stat = torch.where(update, value, best_stat)
        chosen_energy = energy.gather(1, candidate[:, None]).squeeze(1)
        best_energy = torch.where(update, chosen_energy, best_energy)
        best_candidate = torch.where(update, candidate, best_candidate)
        best_attempt = torch.where(update, attempt, best_attempt)
    return best_stat, best_energy, best_candidate, best_attempt


def _carrier_diagnostic(data: dict[str, Any]) -> dict[str, Any] | None:
    if "carrier_basis" not in data:
        return None
    carrier: torch.Tensor = data["carrier_basis"].reshape(6, data["carrier_basis_shape"][2])
    count = carrier.shape[1]
    gram = torch.zeros((6, 6), dtype=torch.complex128)
    norm2 = torch.zeros(6, dtype=torch.float64)
    # Stream this optional diagnostic so a long carrier reference does not
    # allocate a second full 2×3×N workspace.
    for start in range(0, count, 65_536):
        block = carrier[:, start:min(count, start + 65_536)].to(torch.complex128)
        if not bool(torch.isfinite(block.real).all().item()) or not bool(torch.isfinite(block.imag).all().item()):
            raise ValueError("carrier basis contains NaN or infinity")
        gram += block @ block.conj().transpose(0, 1)
        norm2 += block.abs().square().sum(dim=1)
    norms = torch.sqrt(norm2)
    gram = gram / (norms[:, None] * norms[None, :]).clamp_min(torch.finfo(torch.float64).tiny)
    correlations = torch.abs(gram).real.clamp(0, 1).tolist()
    rank = int(torch.linalg.matrix_rank(gram).item())
    return {"carrier_basis_rank": rank, "carrier_basis_abs_gram": correlations,
            "carrier_basis_norms": norms.real.tolist(),
            "carrier_pair_products_counted_independently": False}


def _count_results(rows: list[dict[str, Any]], truth: int | None, labels: list[int]) -> dict[str, Any]:
    correct = wrong = missed = null_admissions = 0
    positive_admissions = positive_no_admission = 0
    for row in rows:
        if row["kind"] == "null":
            null_admissions += int(row["admitted"])
        elif row["admitted"]:
            positive_admissions += 1
            if truth is not None and labels[row["candidate"]] == labels[truth]:
                correct += 1
            elif truth is not None:
                wrong += 1
        else:
            positive_no_admission += 1
            missed += 1
    positive_n = sum(row["kind"] == "positive" for row in rows)
    null_n = sum(row["kind"] == "null" for row in rows)
    return {
        "positive_trials": positive_n,
        "correct_accepted": correct if truth is not None else None,
        "wrong_accepted": wrong if truth is not None else None,
        "missed": missed if truth is not None else None,
        "positive_admissions_unclassified": positive_admissions if truth is None else 0,
        "positive_no_admission": positive_no_admission,
        "null_trials": null_n,
        "null_admissions": null_admissions,
        "zero_event_95_upper_wrong_accept": (1 - 0.05 ** (1 / positive_n)) if truth is not None and wrong == 0 and positive_n else None,
        "zero_event_95_upper_null_admission": (1 - 0.05 ** (1 / null_n)) if null_admissions == 0 and null_n else None,
    }


def _new_best(count: int) -> torch.Tensor:
    # [statistic, energy, candidate, attempt] × [positive,null] × trials.
    # Scalar trial bookkeeping is explicitly charged to the memory budget.
    result = torch.zeros((4, 2, count), dtype=torch.float64)
    result[0].fill_(-torch.inf)
    result[2:].fill_(-1)
    return result


def _record_best(best: torch.Tensor, kind: int, first: int, count: int,
                 result: tuple[torch.Tensor, ...], candidate_offset: int) -> None:
    values = torch.stack(tuple(x.to(torch.float64) for x in result)).detach().cpu()
    if not bool(torch.isfinite(values[:2]).all()):
        raise ValueError("whitened projection statistics overflowed or became nonfinite")
    values[2] += candidate_offset
    previous = best[:, kind, first:first + count]
    previous.copy_(torch.where((values[0] > previous[0])[None, :], values, previous))


def _metrics(best: torch.Tensor, signal_bit: int | None, labels: list[int],
             true_index: int | None, true_attempt: int | None = None,
             epoch_offsets: list[int] | None = None) -> dict[str, Any]:
    count = best.shape[2]
    positive_admitted, null_admitted = best[0, 0] >= 0, best[0, 1] >= 0
    selected = best[2, 0].to(torch.int64)
    label_tensor = torch.tensor(labels, dtype=torch.int64)
    known = signal_bit is not None
    correct = int((positive_admitted & (label_tensor[selected] == signal_bit)).sum()) if known else None
    wrong = int(positive_admitted.sum()) - correct if known else None
    on_attempt = best[3, 0].to(torch.int64) == true_attempt if true_attempt is not None else None
    right_epoch = torch.tensor(epoch_offsets, dtype=torch.int64)[selected] == 0 if epoch_offsets is not None else None
    located = positive_admitted & on_attempt & right_epoch if on_attempt is not None and right_epoch is not None else None
    null_events = int(null_admitted.sum())
    zero_upper = -math.expm1(math.log(.05) / count) if count else None
    return {
        "positive_trials": count,
        "correct_accepted": correct,
        "correct_accepted_scope": "bit label only; use same_attempt_epoch_correct_accepted for scheduled acquisition",
        "same_attempt_epoch_correct_accepted": int((located & (label_tensor[selected] == signal_bit)).sum()) if known and located is not None else None,
        "off_attempt_admissions": int((positive_admitted & ~on_attempt).sum()) if on_attempt is not None else None,
        "wrong_epoch_admissions": int((positive_admitted & ~right_epoch).sum()) if right_epoch is not None else None,
        "wrong_accepted": wrong,
        "missed": count - int(positive_admitted.sum()) if known else None,
        "positive_admissions_unclassified": int(positive_admitted.sum()) if not known else 0,
        "positive_no_admission": count - int(positive_admitted.sum()),
        "exact_nuisance_candidate_selected": int((selected == true_index).sum()) if true_index is not None else None,
        "exact_nuisance_candidate_accepted": int((positive_admitted & (selected == true_index)).sum()) if true_index is not None else None,
        "null_trials": count,
        "null_admissions": null_events,
        "zero_event_95_upper_wrong_accept": zero_upper if known and wrong == 0 else None,
        "zero_event_95_upper_null_admission": zero_upper if null_events == 0 else None,
    }


def _timed_score(device: torch.device, q_t: torch.Tensor, observations: torch.Tensor,
                 starts: list[int], samples: int, threshold: float) -> tuple[tuple[torch.Tensor, ...], float]:
    if device.type == "cuda":
        start, end = torch.cuda.Event(enable_timing=True), torch.cuda.Event(enable_timing=True)
        start.record()
        result = _score_windows(q_t, observations, starts, samples, 1., threshold)
        end.record()
        end.synchronize()
        return result, start.elapsed_time(end) / 1000.
    start_wall = time.perf_counter()
    result = _score_windows(q_t, observations, starts, samples, 1., threshold)
    return result, time.perf_counter() - start_wall


def _gamma_threshold(rank: int, log_tail_reciprocal: float,
                     mode: str) -> tuple[float, str, str | None]:
    """Upper quantile for Gamma(rank,1), with an explicit conservative fallback.

    `exact` names the fixed Gaussian statistical law, not exact arithmetic.
    Non-unit ranks use CPU FP64 gammaincc with bounded upper-bracket bisection.
    The returned endpoint is the side whose computed survival is <= target.
    """
    if rank < 1 or not math.isfinite(log_tail_reciprocal) or log_tail_reciprocal <= 0:
        raise ValueError("invalid Gamma rank or log tail")
    x = log_tail_reciprocal
    upper_bound = math.nextafter(rank + math.sqrt(2 * rank * x) + x, math.inf)
    if not math.isfinite(upper_bound):
        raise ValueError("Gamma threshold is outside the supported numeric range")
    if mode == "conservative":
        return upper_bound, "gamma_concentration_bound", None
    if mode != "exact":
        raise ValueError("threshold must be exact or conservative")
    if rank == 1:
        return math.nextafter(x, math.inf), "exact_gamma_rank_one", None
    target = math.exp(-x)
    if target == 0 or rank > (1 << 53):
        return upper_bound, "gamma_concentration_fallback", "FP64 target or rank is not representable"
    try:
        shape = torch.tensor(float(rank), dtype=torch.float64, device="cpu")

        def survival(value: float) -> float:
            result = float(torch.special.gammaincc(
                shape, torch.tensor(value, dtype=torch.float64, device="cpu")))
            if not math.isfinite(result) or not 0 <= result <= 1:
                raise ValueError("gammaincc returned an invalid survival probability")
            return result

        low, high = 0., upper_bound
        if survival(high) > target:
            raise ValueError("Gamma upper bracket did not cover the requested tail")
        for _ in range(100):
            midpoint = (low + high) * .5
            tail = survival(midpoint)
            if tail > target:
                low = midpoint
            else:
                high = midpoint
            if high - low <= 1e-12 * max(1., high):
                break
        if survival(high) > target or survival(low) < target:
            raise ValueError("Gamma tail bracket failed its final consistency check")
        return high, "numerical_gamma_tail_fp64", None
    except (AttributeError, RuntimeError, ValueError, OverflowError) as error:
        return upper_bound, "gamma_concentration_fallback", str(error)


def run(args: argparse.Namespace) -> Iterable[dict[str, Any]]:
    directory, manifest = _load_manifest(args.manifest)
    data = _validate_manifest(directory, manifest)
    device = _resolve_device(args.device)
    _configure_torch()
    if not 0 < args.rank_relative_tolerance < .1:
        raise ValueError("rank-relative-tolerance must lie in (0, .1)")
    if not 0 < args.false_alarm_budget < 1:
        raise ValueError("false-alarm-budget must lie in (0,1)")
    device_warmup_seconds = _device_warmup(device)
    candidates, samples, columns = data["basis_shape"]
    if candidates > (1 << 31):
        raise ValueError("candidate count exceeds supported scalar bookkeeping")
    signal = data["signal"].to(torch.complex64)
    if not bool(torch.isfinite(signal).all()):
        raise ValueError("signal contains NaN or infinity")
    signal_power = float(signal.abs().to(torch.float64).square().mean())
    if not math.isfinite(signal_power) or signal_power <= 0:
        raise ValueError("noiseless signal must have finite positive energy")
    truth, signal_bit = data["true_candidate_index"], data["signal_bit"]
    attempts = args.attempts
    stride = args.stride_samples if args.stride_samples is not None else samples
    signal_start = data["signal_start_samples"]
    horizon = samples + (attempts - 1) * stride
    if signal_start + samples > horizon:
        raise ValueError("signal_start_samples places the signal outside the searched horizon")
    if args.shard_index >= args.shard_count:
        raise ValueError("shard-index must be less than shard-count")
    trial_ids = range(args.shard_index, args.trials, args.shard_count)
    reference_ids = range(args.shard_index, min(args.trials, args.reference_trials), args.shard_count)
    if not len(trial_ids):
        raise ValueError("this shard has no trial indices")
    bookkeeping_bytes = 64 * (len(trial_ids) + (2 if args.benchmark else 1) * len(reference_ids))
    memory_bytes = int(args.memory_gib * 1024 ** 3)
    # Includes resident signal, scalar result arrays, and both FP64-reference
    # and FP32-selected projection preparation (even when --reference-trials=0).
    fixed_bytes = bookkeeping_bytes + 32 * samples
    if fixed_bytes >= memory_bytes:
        raise ValueError("memory cap cannot hold bounded trial bookkeeping and signal")
    candidate_batch, trial_batch = _candidate_batch_size(
        samples, columns, horizon, memory_bytes - fixed_bytes, args.batch)
    candidate_batch = min(candidates, candidate_batch)
    starts = [i * stride for i in range(attempts)]
    exercised_family = candidates * attempts
    family = exercised_family * args.duration_alternatives
    if args.family_hypotheses is not None:
        if args.family_hypotheses < family:
            raise ValueError("family-hypotheses omits declared candidates/attempts/durations")
        family = args.family_hypotheses
    alpha = args.false_alarm_budget
    x = math.log(family) - math.log(alpha)
    rank_bound = min(samples, columns)
    threshold, threshold_method, threshold_fallback = _gamma_threshold(rank_bound, x, args.threshold)
    cn0_cases: list[tuple[float | None, float]] = []
    if args.cn0_grid is None and "noise_variance" in manifest:
        cn0_cases.append((manifest.get("cn0_db_hz"), float(manifest["noise_variance"])))
    else:
        for cn0 in args.cn0_grid if args.cn0_grid is not None else [float(manifest["cn0_db_hz"])]:
            if not -300 < cn0 / 10 < 300:
                raise ValueError("C/N0 is outside representable variance range")
            cn0_cases.append((cn0, data["noise_reference_power"] * data["sample_rate"] / 10 ** (cn0 / 10)))
    carrier_info = _carrier_diagnostic(data)
    noise_device = device if args.noise_device == "selected" else torch.device("cpu")
    cpu_reference_warmed = cpu_fp32_warmed = False
    for cn0, variance in cn0_cases:
        if not math.isfinite(variance) or variance <= 0:
            raise ValueError("noise variance must be finite and positive")
        # Work in whitened units to avoid overflow in high/low physical powers.
        # Both selected and reference see exactly these same generated samples.
        whitened_signal = (signal.to(torch.complex128) / math.sqrt(variance)).to(device=noise_device, dtype=torch.complex64)
        if not bool(torch.isfinite(whitened_signal).all()) or not bool((whitened_signal.abs() > 0).any()):
            raise ValueError("whitened signal cannot be represented in FP32")
        for split in ("calibration", "holdout"):
            selected_best, reference_best = _new_best(len(trial_ids)), _new_best(len(reference_ids))
            cpu_fp32_best = _new_best(len(reference_ids)) if args.benchmark else None
            selected_projector_seconds = reference_projector_seconds = 0.
            selected_kernel_seconds = reference_kernel_seconds = subset_kernel_seconds = cpu_fp32_kernel_seconds = 0.
            cpu_kernel_warmup_seconds = 0.
            transfer_seconds = noise_seconds = 0.
            rank_fallbacks = subspace_fallbacks = 0
            maximum_same_rank_subspace_distance = maximum_final_subspace_distance = 0.
            subspace_tolerance = max(1e-5, 32 * args.rank_relative_tolerance)
            min_actual_rank, max_actual_rank = columns, 0
            max_orthogonality_error = 0.
            total_start = time.perf_counter()
            next_progress = total_start + 30
            for c0 in range(0, candidates, candidate_batch):
                c1 = min(candidates, c0 + candidate_batch)
                basis_cpu = data["basis"][c0:c1]
                begin = time.perf_counter()
                q_reference, ranks_reference = _modified_gram_schmidt(
                    basis_cpu.to(torch.complex128), args.rank_relative_tolerance)
                q_reference_t = q_reference.conj().transpose(1, 2).contiguous()
                reference_projector_seconds += time.perf_counter() - begin
                begin = time.perf_counter()
                q_selected, ranks_selected = _modified_gram_schmidt(
                    basis_cpu.to(device=device, dtype=torch.complex64), args.rank_relative_tolerance)
                mismatch = ranks_selected.cpu() != ranks_reference
                reference_on_device = q_reference.to(device=device, dtype=torch.complex128)
                selected_for_distance = q_selected.to(torch.complex128)
                distances = _subspace_distances(reference_on_device, selected_for_distance)
                same_rank = ~mismatch.to(device)
                if bool(same_rank.any()):
                    maximum_same_rank_subspace_distance = max(
                        maximum_same_rank_subspace_distance, float(distances[same_rank].max()))
                subspace_mismatch = same_rank & (distances > subspace_tolerance)
                rank_fallbacks += int(mismatch.sum())
                subspace_fallbacks += int(subspace_mismatch.sum())
                fallback = mismatch.to(device) | subspace_mismatch
                if bool(fallback.any()):
                    # The accurate fixed subspace is cast to FP32 for bulk
                    # GEMM. Rank equality alone did not certify its geometry.
                    q_selected[fallback] = reference_on_device[fallback].to(torch.complex64)
                    ranks_selected[fallback] = ranks_reference.to(device)[fallback]
                    del selected_for_distance
                    selected_for_distance = q_selected.to(torch.complex128)
                final_distances = _subspace_distances(reference_on_device, selected_for_distance)
                maximum_final_subspace_distance = max(
                    maximum_final_subspace_distance, float(final_distances.max()))
                if bool((final_distances > subspace_tolerance).any()):
                    raise ValueError("FP32 projector cannot retain the reference subspace within tolerance")
                del reference_on_device, selected_for_distance, distances, final_distances, same_rank, fallback
                # Numerical orthogonality is a diagnostic, not a claim that
                # finite-precision Gaussian simulation proves exact tails.
                q_for_check = q_selected.to(torch.complex128)
                gram = q_for_check.conj().transpose(1, 2) @ q_for_check
                diagonal = torch.diag_embed((q_for_check.abs().square().sum(dim=1) > .5).to(torch.complex128))
                orthogonality = float((gram - diagonal).abs().amax())
                max_orthogonality_error = max(max_orthogonality_error, orthogonality)
                if orthogonality > max(1e-4, 32 * args.rank_relative_tolerance):
                    raise ValueError("selected projector is numerically non-orthogonal; reduce conditioning or force a higher-precision basis")
                q_selected_t = q_selected.conj().transpose(1, 2).contiguous()
                # Identical FP32 projector for a pure device/kernel comparison.
                q_cpu_fp32_t = q_selected_t.to(device="cpu", dtype=torch.complex64) if args.benchmark and len(reference_ids) else None
                if device.type == "cuda":
                    torch.cuda.synchronize(device)
                selected_projector_seconds += time.perf_counter() - begin
                ranks = ranks_reference.tolist()
                min_actual_rank, max_actual_rank = min(min_actual_rank, min(ranks)), max(max_actual_rank, max(ranks))
                del q_reference, q_selected, q_for_check, gram, diagonal
                for first in range(0, len(trial_ids), trial_batch):
                    ids = trial_ids[first:first + trial_batch]
                    begin = time.perf_counter()
                    positive, null = _make_observations(ids, split, args.seed, horizon,
                                                       whitened_signal, signal_start, 1.)
                    if noise_device.type == "cuda":
                        torch.cuda.synchronize(noise_device)
                    noise_seconds += time.perf_counter() - begin
                    begin = time.perf_counter()
                    positive = positive.to(device=device, dtype=torch.complex64)
                    null = null.to(device=device, dtype=torch.complex64)
                    if device.type == "cuda":
                        torch.cuda.synchronize(device)
                    transfer_seconds += time.perf_counter() - begin
                    ref_count = max(0, min(len(ids), len(reference_ids) - first))
                    for kind, observations in enumerate((positive, null)):
                        selected_result, elapsed_kernel = _timed_score(device, q_selected_t, observations, starts, samples, threshold)
                        selected_kernel_seconds += elapsed_kernel
                        _record_best(selected_best, kind, first, len(ids), selected_result, c0)
                        if ref_count:
                            begin = time.perf_counter()
                            reference_observations = observations[:ref_count].to(device="cpu", dtype=torch.complex128)
                            transfer_seconds += time.perf_counter() - begin
                            if not cpu_reference_warmed:
                                _, warmup_elapsed = _timed_score(torch.device("cpu"), q_reference_t,
                                    reference_observations, starts, samples, threshold)
                                cpu_kernel_warmup_seconds += warmup_elapsed
                                cpu_reference_warmed = True
                            reference_result, elapsed_reference = _timed_score(
                                torch.device("cpu"), q_reference_t, reference_observations, starts, samples, threshold)
                            reference_kernel_seconds += elapsed_reference
                            _record_best(reference_best, kind, first, ref_count, reference_result, c0)
                            if args.benchmark:
                                begin = time.perf_counter()
                                cpu_fp32_observations = observations[:ref_count].to(device="cpu", dtype=torch.complex64)
                                transfer_seconds += time.perf_counter() - begin
                                if not cpu_fp32_warmed:
                                    _, warmup_elapsed = _timed_score(torch.device("cpu"), q_cpu_fp32_t,
                                        cpu_fp32_observations, starts, samples, threshold)
                                    cpu_kernel_warmup_seconds += warmup_elapsed
                                    cpu_fp32_warmed = True
                                cpu_fp32_result, elapsed_cpu_fp32 = _timed_score(torch.device("cpu"), q_cpu_fp32_t,
                                    cpu_fp32_observations, starts, samples, threshold)
                                cpu_fp32_kernel_seconds += elapsed_cpu_fp32
                                _record_best(cpu_fp32_best, kind, first, ref_count, cpu_fp32_result, c0)
                                del cpu_fp32_result, cpu_fp32_observations
                                _, elapsed_subset = _timed_score(device, q_selected_t, observations[:ref_count], starts, samples, threshold)
                                subset_kernel_seconds += elapsed_subset
                            del reference_observations, reference_result
                        del selected_result
                    del positive, null
                    if time.perf_counter() >= next_progress:
                        print(f"triad-gpu-study: {split} C/N0={cn0} tile={c0}:{c1}/{candidates} "
                              f"trials={first + len(ids)}/{len(trial_ids)}", file=sys.stderr, flush=True)
                        next_progress = time.perf_counter() + 30
                del q_reference_t, q_selected_t, q_cpu_fp32_t, ranks_selected, ranks_reference
            elapsed = time.perf_counter() - total_start
            true_attempt = starts.index(data["signal_start_samples"]) if data["signal_start_samples"] in starts else None
            epochs = [c.get("epoch_offset_seconds", 0) for c in data["candidateparams"]]
            selected_metrics = _metrics(selected_best, signal_bit, data["bitlabels"], truth, true_attempt, epochs)
            reference_metrics = _metrics(reference_best, signal_bit, data["bitlabels"], truth, true_attempt, epochs) if len(reference_ids) else None
            cpu_fp32_metrics = _metrics(cpu_fp32_best, signal_bit, data["bitlabels"], truth, true_attempt, epochs) if args.benchmark and len(reference_ids) else None
            subset = selected_best[:, :, :len(reference_ids)]
            ranks_differ = subset[2] != reference_best[2]
            admissions_differ = (subset[0] >= 0) != (reference_best[0] >= 0)
            near = torch.minimum(subset[0].abs(), reference_best[0].abs()) <= NEAR_BOUNDARY
            row: dict[str, Any] = {
                "schema": OUTPUT_SCHEMA, "input_format": SCHEMA, "source_hash": data["source_hash"],
                "device": str(device),
                "device_name": torch.cuda.get_device_name(device) if device.type == "cuda" else platform.processor() or "CPU",
                "device_warmup_seconds": device_warmup_seconds, "torch_version": torch.__version__,
                "python_version": platform.python_version(), "split": split, "seed": args.seed,
                "shard_index": args.shard_index, "shard_count": args.shard_count,
                "trial_indices": [trial_ids[0], trial_ids[-1], len(trial_ids)],
                "reference_trial_count": len(reference_ids), "reference_global_index_limit": args.reference_trials,
                "cn0_db_hz": cn0, "noise_variance": variance, "noise_reference_power": data["noise_reference_power"],
                "measured_signal_power": signal_power, "measured_signal_sample_energy": signal_power * samples,
                "measured_signal_energy_seconds": signal_power * samples / data["sample_rate"],
                "sample_rate": data["sample_rate"], "sample_count": samples,
                "duration_seconds": samples / data["sample_rate"], "horizon_duration_seconds": horizon / data["sample_rate"],
                "projection_observation_seconds": samples / data["sample_rate"],
                "statistic": "one fixed raw-waveform subspace per complete acquisition window; no unmodeled block resets",
                "duration_label": args.duration_label, "candidate_count": candidates, "candidate_columns": columns,
                "rank_min_observed": min_actual_rank, "rank_max_observed": max_actual_rank,
                "rank_relative_tolerance": args.rank_relative_tolerance, "projector_fp64_fallback_candidates": rank_fallbacks,
                "maximum_projector_orthogonality_error": max_orthogonality_error,
                "projector_same_rank_fp64_fallback_candidates": subspace_fallbacks,
                "maximum_same_rank_projector_distance_before_fallback": maximum_same_rank_subspace_distance,
                "maximum_projector_distance_after_fallback": maximum_final_subspace_distance,
                "projector_subspace_distance_tolerance": subspace_tolerance,
                "family_hypotheses": family, "empirically_exercised_family_hypotheses": exercised_family,
                "duration_alternatives_budgeted": args.duration_alternatives,
                "unexercised_family_hypotheses": family - exercised_family,
                "false_alarm_budget": alpha,
                "false_alarm_budget_scope": "analytic declared finite family; empirical counts cover only the sampled overlapping horizon",
                "threshold_rank_bound": rank_bound,
                "threshold_rank_policy": "common column-count upper-bound rank; conservative for deficient candidates", "threshold_x": x, "threshold_normalized_energy": threshold,
                "threshold_requested": args.threshold, "threshold_method": threshold_method,
                "threshold_fallback_reason": threshold_fallback,
                "threshold_sample_energy": threshold * variance,
                "attempts": attempts, "stride_samples": stride, "horizon_samples": horizon,
                "overlap_samples": max(0, samples - stride), "truth_available": signal_bit is not None,
                "signal_bit": signal_bit, "true_candidate_index": truth,
                "candidateparams": data["candidateparams"], "bitlabels": data["bitlabels"],
                "selected": selected_metrics, "cpu_fp64_reference": reference_metrics,
                "cpu_fp32_matched_subset": cpu_fp32_metrics,
                "cpu_fp32_vs_selected_admission_deltas": int(((subset[0] >= 0) != (cpu_fp32_best[0] >= 0)).sum()) if args.benchmark else None,
                "cpu_fp32_vs_selected_ranking_deltas": int((subset[2] != cpu_fp32_best[2]).sum()) if args.benchmark else None,
                "fp32_vs_fp64_comparisons": 2 * len(reference_ids),
                "fp32_vs_fp64_ranking_deltas": int(ranks_differ.sum()),
                "fp32_vs_fp64_admission_deltas": int(admissions_differ.sum()),
                "near_boundary_band_normalized": NEAR_BOUNDARY,
                "near_boundary_ranking_deltas": int((ranks_differ & near).sum()),
                "near_boundary_admission_deltas": int((admissions_differ & near).sum()),
                "selected_projector_seconds": selected_projector_seconds,
                "cpu_fp64_projector_seconds": reference_projector_seconds,
                "selected_resident_kernel_seconds": selected_kernel_seconds,
                "cpu_fp64_reference_seconds": reference_kernel_seconds,
                "cpu_fp32_matched_subset_kernel_seconds": cpu_fp32_kernel_seconds if args.benchmark else None,
                "cpu_kernel_warmup_seconds": cpu_kernel_warmup_seconds,
                "selected_reference_subset_kernel_seconds": subset_kernel_seconds if args.benchmark else None,
                "matched_subset_kernel_speedup": reference_kernel_seconds / subset_kernel_seconds if subset_kernel_seconds > 0 else None,
                "mixed_precision_kernel_speedup_cpu_fp64_over_selected_fp32": reference_kernel_seconds / subset_kernel_seconds if subset_kernel_seconds > 0 else None,
                "same_precision_kernel_speedup_cpu_fp32_over_selected_fp32": cpu_fp32_kernel_seconds / subset_kernel_seconds if subset_kernel_seconds > 0 else None,
                "kernel_speedup_scope": "resident matched trial subset, identical FP32 projector/data for same-precision comparison; excludes QR, RNG, transfers and stream receiver",
                "cpu_thread_count": torch.get_num_threads(), "cpu_interop_thread_count": torch.get_num_interop_threads(),
                "host_logical_cpu_count": os.cpu_count(),
                "transfer_inclusive_seconds": elapsed,
                "transfer_inclusive_includes_reference": bool(len(reference_ids)),
                "observation_generation_seconds": noise_seconds, "observation_transfer_seconds": transfer_seconds,
                "noise_generation_device": str(noise_device),
                "requested_trial_batch": args.batch, "actual_trial_batch": trial_batch, "candidate_tile": candidate_batch,
                "workspace_estimate_bytes": fixed_bytes + candidate_batch * samples * columns * 16 * PROJECTOR_WORKSPACE_FACTOR + trial_batch * horizon * 8 * 8 +
                    candidate_batch * trial_batch * columns * 32,
                "scalar_bookkeeping_bytes": bookkeeping_bytes, "memory_cap_bytes": memory_bytes,
                "benchmark": args.benchmark, "tf32_enabled": False,
                "cpu_reference_dtype": "complex128", "selected_dtype": "complex64",
                "cpu_reference_scope": "FP64 arithmetic on exported complex64 basis and identical generated complex64 observations; not the C++ streamed block detector",
                "noise_model": "independent complex AWGN per horizon; shared across overlapping windows; whitened simulation",
                "noise_seed_compatibility": "same backend, version, seeds, trial IDs; CPU and CUDA RNG streams may differ",
                "common_noise_across_cn0_points": True,
                "cn0_points_are_not_independent_null_campaigns": True,
                "threshold_model": "finite-family Gamma tail under known white Gaussian noise and fixed orthonormal subspaces; numerical method/fallback and finite precision reported separately",
            }
            if carrier_info is not None:
                row.update(carrier_info)
            yield row

def main() -> int:
    args = parser().parse_args()
    if not math.isfinite(args.memory_gib) or args.memory_gib <= 0:
        parser().error("--memory-gib must be finite and positive")
    if not math.isfinite(args.false_alarm_budget) or not 0 < args.false_alarm_budget < 1:
        parser().error("--false-alarm-budget must be strictly between zero and one")
    if args.shard_index >= args.shard_count:
        parser().error("--shard-index must be less than --shard-count")
    try:
        if args.output is None:
            stream = sys.stdout
            close = False
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            stream = args.output.open("w", encoding="utf-8")
            close = True
        try:
            for row in run(args):
                stream.write(json.dumps(row, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n")
                stream.flush()
        finally:
            if close:
                stream.close()
        return 0
    except (ValueError, OSError, RuntimeError, torch.cuda.OutOfMemoryError) as exc:
        print(f"triad-gpu-study: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
