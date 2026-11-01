#!/usr/bin/env python3
"""Exactly combine adjacent triad block sufficient statistics."""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import sys

import numpy as np

from triad_state_workspace import BlockWorkspace, WorkspaceError


BASE_FILES = {
    "gram.c128": ("<c16", "gram_shape"),
    "mean.c128": ("<c16", "mean_shape"),
    "intervals.u64": ("<u8", "intervals_shape"),
    "valid_rows.u64": ("<u8", "valid_rows_shape"),
    "signal_energy.f64": ("<f8", "signal_energy_shape"),
}
OPTIONAL_FILES = {
    "observation.c128": ("<c16", "observation_shape"),
    "received_energy.f64": ("<f8", "received_energy_shape"),
}


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def estimate_peak_bytes(dimension: int) -> int:
    # Gram validation eigensolve + one source block, accumulators, vectors,
    # projection and energy fields. Reblock factor does not increase RAM.
    d = dimension
    return 4*d*d*16 + 2*d*d*16 + 16*d*16 + 65536


def reblock(input_dir: str | Path, output_dir: str | Path, *, factor: int,
            memory_mib: int = 256) -> dict:
    src = Path(input_dir).resolve()
    out = Path(output_dir).resolve()
    if isinstance(factor, bool) or not isinstance(factor, int) or factor < 1:
        raise ValueError("factor must be a positive integer")
    if isinstance(memory_mib, bool) or not isinstance(memory_mib, int) or memory_mib < 1:
        raise ValueError("memory_mib must be a positive integer")
    if src == out or src in out.parents or out in src.parents:
        raise ValueError("input and output workspaces must be separate directories")
    if out.exists():
        if not out.is_dir() or any(out.iterdir()):
            raise ValueError("output must be a fresh empty directory")
    else:
        out.mkdir(parents=True)

    try:
        source_manifest = json.loads((src/"manifest.json").read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise WorkspaceError(f"cannot read input manifest: {exc}") from exc
    dimension = source_manifest.get("dimension")
    if isinstance(dimension, bool) or not isinstance(dimension, int) or dimension < 1:
        raise WorkspaceError("input dimension must be a positive integer")
    peak = estimate_peak_bytes(dimension)
    if peak > memory_mib*1024*1024:
        raise WorkspaceError(f"dimension {dimension} needs about {peak} bytes, exceeding --memory-mib {memory_mib}")
    ws = BlockWorkspace(src, max_tile_blocks=1, memory_limit_bytes=memory_mib*1024*1024)
    if ws.has_observation and not ws.has_received_energy:
        # Older sampled-observation exports remain readable, but this tool
        # preserves that format and does not fabricate received energy.
        pass
    manifest = copy.deepcopy(ws.manifest)
    old_blocks, old_block_samples = ws.block_count, ws.block_samples
    if factor > ((1 << 64) - 1) // old_block_samples:
        raise ValueError("factor makes block_samples exceed uint64")
    new_blocks = (old_blocks + factor - 1) // factor
    new_block_samples = old_block_samples * factor
    d = ws.dimension
    output_files = dict(BASE_FILES)
    if ws.has_observation:
        output_files["observation.c128"] = OPTIONAL_FILES["observation.c128"]
    if ws.has_received_energy:
        output_files["received_energy.f64"] = OPTIONAL_FILES["received_energy.f64"]

    source_hashes = {name: sha256_file(src/name) for name in output_files}
    source_hashes["manifest.json"] = sha256_file(src/"manifest.json")
    tool_path = Path(__file__).resolve()

    # Read through memmaps and write each output group directly to a temporary
    # file. No raw sample stream is reconstructed or re-read.
    source_arrays = {}
    for name, (dtype, _) in output_files.items():
        shape = tuple(ws.manifest[{
            "gram.c128":"gram_shape", "mean.c128":"mean_shape",
            "intervals.u64":"intervals_shape", "valid_rows.u64":"valid_rows_shape",
            "signal_energy.f64":"signal_energy_shape", "observation.c128":"observation_shape",
            "received_energy.f64":"received_energy_shape",
        }[name]])
        source_arrays[name] = np.memmap(src/name, mode="r", dtype=np.dtype(dtype), shape=shape)

    output_arrays = {}
    output_shapes = {
        "gram.c128": (new_blocks,d,d), "mean.c128": (new_blocks,d),
        "intervals.u64": (new_blocks,2), "valid_rows.u64": (new_blocks,),
        "signal_energy.f64": (new_blocks,), "observation.c128": (new_blocks,d),
        "received_energy.f64": (new_blocks,),
    }
    for name, (dtype, _) in output_files.items():
        output_arrays[name] = np.memmap(out/(name+".partial"), mode="w+",
                                        dtype=np.dtype(dtype), shape=output_shapes[name])

    previous_stop = None
    for out_i in range(new_blocks):
        first = out_i * factor
        stop = min(old_blocks, first+factor)
        gsum = np.zeros((d,d), dtype=np.complex128)
        msum = np.zeros(d, dtype=np.complex128)
        osum = np.zeros(d, dtype=np.complex128) if ws.has_observation else None
        valid_sum = 0
        signal_energy_sum = 0.0
        received_energy_sum = 0.0
        first_sample = None
        last_sample = None
        for source_i in range(first, stop):
            Grow = np.asarray(source_arrays["gram.c128"][source_i:source_i+1])
            interval = np.asarray(source_arrays["intervals.u64"][source_i:source_i+1])
            valid = np.asarray(source_arrays["valid_rows.u64"][source_i:source_i+1])
            ws._check_tile(Grow, interval, valid, source_i, previous_stop)
            previous_stop = int(interval[0,1])
            if first_sample is None:
                first_sample = int(interval[0,0])
            last_sample = int(interval[0,1])
            gsum += source_arrays["gram.c128"][source_i]
            mrow = source_arrays["mean.c128"][source_i]
            if not np.isfinite(mrow).all():
                raise WorkspaceError(f"nonfinite hidden mean in source block {source_i}")
            msum += mrow
            valid_sum += int(valid[0])
            energy = float(source_arrays["signal_energy.f64"][source_i])
            if not math.isfinite(energy) or energy < 0:
                raise WorkspaceError(f"invalid signal energy in source block {source_i}")
            signal_energy_sum += energy
            if ws.has_observation:
                obs = source_arrays["observation.c128"][source_i]
                if not np.isfinite(obs).all():
                    raise WorkspaceError(f"nonfinite actual observation in source block {source_i}")
                osum += obs
            if ws.has_received_energy:
                e = float(source_arrays["received_energy.f64"][source_i])
                if not math.isfinite(e) or e < 0:
                    raise WorkspaceError(f"invalid received energy in source block {source_i}")
                received_energy_sum += e
        output_arrays["gram.c128"][out_i] = gsum
        output_arrays["mean.c128"][out_i] = msum
        output_arrays["intervals.u64"][out_i] = (first_sample, last_sample)
        output_arrays["valid_rows.u64"][out_i] = valid_sum
        output_arrays["signal_energy.f64"][out_i] = signal_energy_sum
        if ws.has_observation:
            output_arrays["observation.c128"][out_i] = osum
        if ws.has_received_energy:
            output_arrays["received_energy.f64"][out_i] = received_energy_sum

    for array in output_arrays.values():
        array.flush()
    output_arrays.clear()
    source_arrays.clear()

    # Update only block geometry and file layouts. Physical-time, RF, channel,
    # seed, noise, key and acquisition metadata remain byte-for-byte values.
    manifest["block_count"] = new_blocks
    manifest["block_samples"] = new_block_samples
    manifest["gram_shape"] = [new_blocks,d,d]
    manifest["mean_shape"] = [new_blocks,d]
    manifest["intervals_shape"] = [new_blocks,2]
    manifest["valid_rows_shape"] = [new_blocks]
    manifest["signal_energy_shape"] = [new_blocks]
    if ws.has_observation:
        manifest["observation_shape"] = [new_blocks,d]
    if ws.has_received_energy:
        manifest["received_energy_shape"] = [new_blocks]
    manifest["file_sizes"] = {name: int(np.prod(output_shapes[name], dtype=np.int64)) * np.dtype(dtype).itemsize
                              for name,(dtype,_) in output_files.items()}
    provenance = manifest.get("transform_provenance", [])
    if not isinstance(provenance, list):
        provenance = [provenance]
    provenance.append({
        "operation": "exact-linear-reblock",
        "factor": factor,
        "source_block_samples": old_block_samples,
        "source_block_count": old_blocks,
        "source_manifest_sha256": source_hashes.pop("manifest.json"),
        "source_file_sha256": source_hashes,
        "transformer_sha256": sha256_file(tool_path),
    })
    manifest["transform_provenance"] = provenance

    for name in output_files:
        (out/(name+".partial")).replace(out/name)
    partial_manifest = out/"manifest.json.partial"
    with partial_manifest.open("w", encoding="utf-8") as f:
        json.dump(manifest, f, sort_keys=True, separators=(",",":"))
        f.write("\n")
        f.flush()
        os.fsync(f.fileno())
    partial_manifest.replace(out/"manifest.json")
    return manifest


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--factor", required=True, type=int)
    parser.add_argument("--memory-mib", type=int, default=256)
    args = parser.parse_args(argv)
    try:
        result = reblock(args.input, args.output, factor=args.factor,
                         memory_mib=args.memory_mib)
    except (WorkspaceError, ValueError, OSError) as exc:
        parser.error(str(exc))
    print(json.dumps({"mode":"reblock","input":str(args.input),"output":str(args.output),
                      "factor":args.factor,"block_count":result["block_count"],
                      "block_samples":result["block_samples"],
                      "physical_span_seconds":result["sample_count"]/result["sample_rate"]},
                     sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
