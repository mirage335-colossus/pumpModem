"""Explicit source-only physical-bit alignment for cipher-migration measurements.

A receipt is collected before sampled observations. It compares raw clear and
fully encrypted source coefficients, never noise, receiver scores or admissions.
Timed invocations still construct both encrypted alternatives and the full bank.
"""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import subprocess
import tempfile
import time

SCHEMA = "triad-performance-physical-input-pair-v1"
MAP_SCHEMA = "triad-performance-source-bit-map-v1"


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                    allow_nan=False).encode()).hexdigest()


def _triplet(value):
    if (not isinstance(value, list) or len(value) != 3 or
            any(not isinstance(pair, list) or len(pair) != 2 or
                any(type(x) not in (int, float) or not math.isfinite(x) for x in pair)
                for pair in value)):
        raise ValueError("source-only coefficient receipt is malformed")
    return value


def coefficient_map(cells):
    """Verify a constant bijection from raw coefficient evidence, not a declaration."""
    if not isinstance(cells, list) or not cells or len(cells) > 32:
        raise ValueError("source-only map has invalid probe count")
    seen, distinct, masks, clear = set(), 0, set(), []
    for row in cells:
        if not isinstance(row, dict) or set(row) != {"cell", "clear0", "clear1", "encrypted0", "encrypted1"}:
            raise ValueError("source-only coefficient fields changed")
        cell = row["cell"]
        if type(cell) is not int or cell < 0 or cell in seen:
            raise ValueError("source-only cell coordinate is invalid or duplicated")
        seen.add(cell)
        c0, c1, e0, e1 = (_triplet(row[k]) for k in ("clear0", "clear1", "encrypted0", "encrypted1"))
        clear.append({"cell": cell, "clear0": c0, "clear1": c1})
        if c0 == c1:
            if e0 != c0 or e1 != c0:
                raise ValueError("shared source coefficient changed under input masking")
            continue
        distinct += 1
        if e0 == c0 and e1 == c1:
            masks.add(0)
        elif e0 == c1 and e1 == c0:
            masks.add(1)
        else:
            raise ValueError("encrypted source alternatives are not the clear permutation")
    if distinct < 8 or len(masks) != 1:
        raise ValueError("source-only map lacks eight consistent distinguishing cells")
    return {"mask": masks.pop(), "distinct_cells": distinct, "probed_cells": len(cells),
            "clear_coefficients_sha256": digest(clear), "clear_coefficients": clear}


def validate_rows(rows, trials):
    if not isinstance(rows, list) or len(rows) != trials:
        raise ValueError("source-only map trial count differs")
    maps = []
    for i, row in enumerate(rows):
        if (row.get("schema") != MAP_SCHEMA or row.get("trial") != i or
                row.get("encrypted") is not True or type(row.get("key_seed")) is not int or
                type(row.get("epoch")) is not int or
                not isinstance(row.get("rx"), list) or len(row["rx"]) != 1 or
                row["rx"][0].get("epoch_offset_seconds") != 0):
            raise ValueError("physical alignment requires encrypted zero-epoch source receipts")
        maps.append({"trial": i, "key_seed": row["key_seed"], "epoch": row["epoch"],
                     "tx": coefficient_map(row["tx"]),
                     "rx": coefficient_map(row["rx"][0]["cells"])})
    return maps


def _receipt_directory(output, prefix):
    if output is None:
        return None
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    return Path(tempfile.mkdtemp(prefix=prefix,dir=output))


def _retain_command(directory, source, argv, result, elapsed):
    if directory is None:
        return None
    path=directory/(source+"-raw.json")
    value={"argv":argv,"returncode":result.returncode,"stdout":result.stdout,
           "stderr":result.stderr,"wall_seconds":elapsed}
    with path.open("x") as out:
        json.dump(value,out,sort_keys=True,allow_nan=False);out.write("\n")
    return str(path)


def prepare_pair(binaries, args, seed, trials, deadline, *, output=None,
                 input_bits=None, order=("reference", "candidate"), separate_captures=False):
    """Freeze per-trial input maps for each binary before either timed pipeline."""
    if type(trials) is not int or not 1 <= trials <= 100000 or type(seed) is not int or seed < 0:
        raise ValueError("invalid source-only mapping trial geometry")
    if len(order)!=2 or set(order)!={"reference","candidate"}:
        raise ValueError("source preflight order must contain both sources once")
    if separate_captures and trials>1 and "--key-seed" in args:
        raise ValueError("explicit per-capture key-seed requires individually mapped commands")
    input_bits=[0]*trials if input_bits is None else list(input_bits)
    if len(input_bits)!=trials or any(type(x) is not int or x not in (0,1) for x in input_bits):
        raise ValueError("original reference input bits are invalid")
    started = time.perf_counter()
    directory=_receipt_directory(output,"source-map-")
    sources, verified = {}, {}
    for name in order:
        binary = Path(binaries[name]).resolve(strict=True)
        argv = [str(binary), *map(str, args), "--mode", "source-bit-map", "--seed", str(seed),
                "--trials", str(trials), "--threads", "1"]
        left = deadline - time.monotonic()
        if not math.isfinite(left) or left <= 0:
            raise TimeoutError("physical source mapping exhausted the guard deadline")
        one = time.perf_counter()
        result = subprocess.run(argv, text=True, capture_output=True, timeout=left)
        raw_receipt=_retain_command(directory,name,argv,result,time.perf_counter()-one)
        if result.returncode:
            raise ValueError("source-only map command failed: " + result.stderr[-1600:])
        try:
            rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
        except ValueError as exc:
            raise ValueError("source-only map returned invalid JSON") from exc
        verified[name] = validate_rows(rows, trials)
        sources[name] = {"argv": argv, "raw_receipt":raw_receipt, "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                         "stdout_sha256": hashlib.sha256(result.stdout.encode()).hexdigest(),
                         "rows": rows, "wall_seconds": time.perf_counter() - one,
                         "tx_masks": [row["tx"]["mask"] for row in verified[name]],
                         "rx_masks": [row["rx"]["mask"] for row in verified[name]]}
    for a, b in zip(verified["reference"], verified["candidate"], strict=True):
        if any(a[k] != b[k] for k in ("trial", "key_seed", "epoch")):
            raise ValueError("paired source-only map coordinates differ")
        for role in ("tx", "rx"):
            if a[role]["clear_coefficients"] != b[role]["clear_coefficients"]:
                raise ValueError("paired clear source alternatives differ")
    physical_bits=[bit^mapping["tx"]["mask"] for bit,mapping in zip(input_bits,verified["reference"],strict=True)]
    return {"schema": SCHEMA, "seed": seed, "trials": trials, "sources": sources,
            "order":list(order), "reference_plaintext_bits":input_bits, "physical_signal_bits":physical_bits,
            "verified_maps": verified, "wall_seconds": time.perf_counter() - started,
            "scope": "pre-observation source-coefficient bijection; full encryption and receiver banks retained; no score-based selection"}


def arguments(receipt, source, trial=None):
    if receipt.get("schema") != SCHEMA or source not in ("reference", "candidate"):
        raise ValueError("invalid frozen physical source receipt")
    row = receipt["sources"][source]
    tx, rx, bits = row["tx_masks"], row["rx_masks"], receipt["physical_signal_bits"]
    if trial is not None:
        tx, rx, bits = [tx[trial]], [rx[trial]], [bits[trial]]
    for values in (tx, rx, bits):
        if not values or any(type(x) is not int or x not in (0, 1) for x in values):
            raise ValueError("invalid frozen physical source mask")
    return ["--performance-tx-masks", "".join(map(str, tx)),
            "--performance-rx-masks", "".join(map(str, rx)),
            "--performance-signal-bits", "".join(map(str,bits))]


def validate_native_receipt(result, receipt, source):
    expected = receipt["sources"][source]
    rows = result["native_results"]
    if len(rows) != receipt["trials"]:
        raise ValueError("timed native trial count differs from source receipt")
    for i, row in enumerate(rows):
        actual = row.get("performance_physical_input", {})
        tx, rx = expected["tx_masks"][i], expected["rx_masks"][i]
        if (actual.get("schema") != "triad-performance-physical-bits-v1" or
                actual.get("tx_mask") != tx or actual.get("rx_mask") != rx or
                row["truth_bit"] != receipt["physical_signal_bits"][i] or
                actual.get("signal_physical_bit") != row["truth_bit"] or
                actual.get("signal_plaintext_bit") != (row["truth_bit"] ^ tx) or
                row.get("receiver_plaintext_bit") != (row["decoded_bit"] ^ rx)):
            raise ValueError("timed native physical input did not use its frozen source map")


def complete_native_inputs(binaries, args, seed, alignment, deadline, *, output=None):
    """Hash complete sampled inputs in separate, untimed generation-only passes."""
    started = time.perf_counter()
    directory=_receipt_directory(output,"complete-input-")
    receipts = {}
    for name in alignment["order"]:
        argv = [str(Path(binaries[name]).resolve(strict=True)), *map(str, args),
                "--seed", str(seed), "--trials", str(alignment["trials"]), "--threads", "1",
                *arguments(alignment, name), "--mode", "capture-hash"]
        left = deadline-time.monotonic()
        if left <= 0:
            raise TimeoutError("complete input preflight exhausted guard deadline")
        one = time.perf_counter()
        result = subprocess.run(argv, capture_output=True, text=True, timeout=left)
        raw_receipt=_retain_command(directory,name,argv,result,time.perf_counter()-one)
        if result.returncode:
            raise ValueError("complete input preflight failed: " + result.stderr[-1600:])
        rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
        if len(rows) != alignment["trials"]:
            raise ValueError("complete input preflight trial count differs")
        for i, row in enumerate(rows):
            if (row.get("schema") != "triad-performance-complete-input-v1" or row.get("trial") != i or
                    row.get("complete") is not True or row.get("seed") != seed+i or
                    type(row.get("capture_samples")) is not int or row["capture_samples"] <= 0 or
                    not isinstance(row.get("sha256"), str) or len(row["sha256"]) != 64 or
                    any(c not in "0123456789abcdef" for c in row["sha256"])):
                raise ValueError("complete input preflight returned invalid completion evidence")
            masks = alignment["sources"][name]
            metadata = row.get("performance_physical_input", {})
            if (metadata.get("tx_mask") != masks["tx_masks"][i] or
                    metadata.get("rx_mask") != masks["rx_masks"][i]):
                raise ValueError("complete input preflight did not apply frozen maps")
        receipts[name] = {"argv": argv, "raw_receipt":raw_receipt, "rows": rows, "wall_seconds": time.perf_counter()-one,
                          "binary_sha256": hashlib.sha256(Path(binaries[name]).read_bytes()).hexdigest()}
    exact = ("seed", "key_seed", "channel_seed", "capture_samples", "source_samples", "chunk_samples",
             "sha256", "source_format", "random_seed_roots", "sample_rate_numerator", "sample_rate_denominator")
    for a, b in zip(receipts["reference"]["rows"], receipts["candidate"]["rows"], strict=True):
        if any(a.get(k) != b.get(k) for k in exact):
            raise ValueError("complete paired received inputs differ")
    return {"schema": "triad-performance-complete-input-pair-v1", "sources": receipts,
            "wall_seconds": time.perf_counter()-started, "order":alignment["order"],
            "scope": "separate full source/channel/noise/validity preflight; no receiver, scores or timed-pipeline hashing"}


def validate_native_input_binding(result, complete, source):
    rows = complete["sources"][source]["rows"]
    if len(result["native_results"]) != len(rows):
        raise ValueError("timed native count differs from complete input preflight")
    for actual, expected in zip(result["native_results"], rows, strict=True):
        for key in ("trial", "seed", "key_seed", "channel_seed", "capture_samples", "source_samples",
                    "source_format", "random_seed_roots", "sample_rate_numerator", "sample_rate_denominator",
                    "performance_physical_input"):
            if actual.get(key) != expected.get(key):
                raise ValueError("timed native input binding differs from preflight: " + key)


def validate_fixture_inputs(pair):
    """Compare every finalized binary receiver-input file; manifests retain raw labels."""
    a, b = (pair[name]["fixture"] for name in ("reference", "candidate"))
    if len(a) != len(b) or not a:
        raise ValueError("paired complete fixture counts differ")
    identities = []
    for old, new in zip(a, b, strict=True):
        hashes = [{k: v for k, v in entry["sha256"].items() if k != "manifest.json"} for entry in (old, new)]
        if not hashes[0] or hashes[0] != hashes[1]:
            raise ValueError("paired complete sampled receiver-input bytes differ")
        identities.append(hashes[0])
    return {"binary_input_sha256": identities,
            "scope": "all finalized non-manifest input files, after pipeline timers; raw manifests and commands separately retained"}
