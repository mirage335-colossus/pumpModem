#!/usr/bin/env python3
"""Qualify one actual native memory transition; directly time its routine targets.

The target is a complete native grouped-statistics export, from process creation
through exit after output finalization. Validation hashes and scratch removal
are harness overhead, retained separately. No disk-cache flush or durable fsync
is requested. Paired/sequential channels are separate cost regimes: a fitted
duration law is never carried through their memory boundary.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict
import hashlib
import json
import math
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
CONFIG_PATH = Path(__file__).with_name("triad-performance-resource.json")
SCHEMA = "triad-performance-resource-v1"
ANCHOR_SCHEMA = "triad-performance-resource-anchor-v2"
CONTRACT = "complete-grouped-export-separate-memory-regimes-interpolation-v2"
REGIMES = ("paired", "sequential")
MIB = 1024 * 1024
FILES = ("group_gram.c128", "group_mean.c128", "intervals.u64", "valid_rows.u64",
         "signal_energy.f64", "group_observation.c128", "received_energy.f64")


class ResourcePerformanceError(RuntimeError):
    pass


class ResourceWorkRegimeError(ResourcePerformanceError):
    pass


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                     allow_nan=False).encode()).hexdigest()


def file_sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for part in iter(lambda: stream.read(MIB), b""):
            h.update(part)
    return h.hexdigest()


def sealed(value, field="record_sha256"):
    result = {k: v for k, v in value.items() if k != field}
    return {**result, field: digest(result)}


def verify_seal(value, field="record_sha256"):
    return isinstance(value, dict) and value.get(field) == digest({k: v for k, v in value.items() if k != field})


def load_config(path=CONFIG_PATH):
    value = json.loads(Path(path).read_text())
    if (value.get("schema") != "triad-performance-resource-config-v2" or
            value.get("training_seconds") != [32, 96] or value.get("target_seconds") != 64 or
            value.get("training_replicates") != 33 or value.get("qualification_looks") != [9, 17, 33, 65] or
            value.get("qualification_alpha") != .05 or value.get("model_relative_error_limit") != .10):
        raise ResourcePerformanceError("unknown resource qualification contract")
    return value


def implementation_identity():
    return {"measurement_contract": CONTRACT, "adapter_sha256": file_sha(__file__),
            "adaptive_sha256": file_sha(Path(__file__).with_name("triad_perf_adaptive.py")),
            "native_environment_helper_sha256": file_sha(Path(__file__).with_name("triad_perf_native.py")),
            "hardware_environment_helper_sha256": file_sha(Path(__file__).with_name("triad_perf_hardware.py")),
            "physical_input_helper_sha256": file_sha(Path(__file__).with_name("triad_perf_physical_bits.py")),
            "config_sha256": file_sha(CONFIG_PATH)}


def binary_identity(binary, source_root, threads=1):
    from triad_perf_native import environment_identity
    if threads != 1:
        raise ResourcePerformanceError("native grouped exporter is currently serial; use --threads 1")
    root = Path(source_root).resolve()
    paths = [root / "tools/triad-long.cpp"]
    paths += sorted((root / "src").glob("triad*.cpp"))
    paths += sorted((root / "include/datapump").glob("triad*.hpp"))
    return {"binary_sha256": file_sha(binary), "environment": environment_identity(threads),
            "declared_source_sha256": {str(p.relative_to(root)): file_sha(p) for p in paths if p.is_file()},
            "source_binding_scope": "Exact executable bytes; declared checkout hashes are provenance, not a reconstructed compiler dependency closure.",
            "arithmetic": "native complex128/float64", "backend": "cpu"}


def ensure_unchanged(implementation, identities, binaries, roots, threads):
    if implementation_identity() != implementation:
        raise ResourcePerformanceError("performance adapter/dependency changed during resource measurements")
    for name in identities:
        if binary_identity(binaries[name], roots[name], threads) != identities[name]:
            raise ResourcePerformanceError("native binary/source/environment changed during resource measurements: " + name)


def argv_for(binary, config, seconds, seed, memory_mib, output, threads=1):
    if (seconds not in (*config["training_seconds"], config["target_seconds"]) or
            isinstance(seed, bool) or not isinstance(seed, int) or seed < 0 or
            isinstance(memory_mib, bool) or not isinstance(memory_mib, int) or memory_mib < 1 or threads != 1):
        raise ResourcePerformanceError("invalid native resource workload")
    return [str(Path(binary).resolve()), *config["args"], "--seconds", str(seconds),
            "--seed", str(seed), "--threads", str(threads), "--memory-mib", str(memory_mib),
            "--output", str(output)]


def work_record(manifest, config, memory_mib, mode):
    ex = manifest.get("grouped_export_execution")
    if not isinstance(ex, dict):
        raise ResourceWorkRegimeError("native exporter lacks actual resource-branch counters")
    rows = manifest["sample_count"]
    block = manifest["block_samples"]
    if (manifest.get("format") != "triad-group-block-stats-v1" or not manifest.get("stats_noisy") or
            mode not in REGIMES or ex["channel_pass_mode"] != mode or
            ex["basis_synthesis_passes"] != (1 if mode == "paired" else 2) or
            ex["worker_threads_used"] != 1 or ex["bounded_workspace_bytes"] > memory_mib * MIB or
            rows < config["chunk_samples"] or manifest["channel_samples"] != rows or
            manifest["group_count"] != config["group_count"] or manifest["dimension"] != config["dimension"] or
            manifest["block_count"] != (rows + block - 1) // block):
        raise ResourceWorkRegimeError("native export did not execute the declared complete memory regime")
    regime = {"mode": mode, "basis_synthesis_passes": ex["basis_synthesis_passes"],
              "memory_mib": memory_mib, "bounded_workspace_bytes": ex["bounded_workspace_bytes"],
              "chunk_samples": config["chunk_samples"], "block_samples": block,
              "group_count": manifest["group_count"], "dimension": manifest["dimension"],
              "candidate_count": manifest["candidate_count"], "sample_rate": manifest["sample_rate"],
              "channel_tail_samples": manifest["channel_tail_samples"], "worker_threads_used": 1,
              "candidates": manifest["candidates"], "groups": manifest["groups"],
              "arithmetic": "complex128", "stats_noisy": True}
    # Derived work counters are distinguished from the native branch counters.
    work = {"sample_count": rows, "source_samples": manifest["source_samples"],
            "block_count": manifest["block_count"], "chunks_per_channel": math.ceil(rows / config["chunk_samples"]),
            "channel_passes": 2, "channel_rows": 2 * rows,
            "basis_values": rows * manifest["group_count"] * manifest["dimension"] * ex["basis_synthesis_passes"],
            "gram_terms": rows * manifest["group_count"] * manifest["dimension"] ** 2,
            "projection_terms": 2 * rows * manifest["group_count"] * manifest["dimension"],
            "file_bytes": sum(manifest["file_sizes"].values()),
            "derivation": "Counts follow emitted complete rows, group dimensions and native basis-pass counter; not CPU instruction counts."}
    return regime, work


def run_export(binary, config, seconds, seed, memory_mib, mode, directory, *, deadline=None, threads=1,
               failure_path=None, physical_mask_arguments=None):
    if physical_mask_arguments is not None:
        masks = list(physical_mask_arguments)
        if (len(masks) != 6 or masks[::2] != ["--performance-tx-masks", "--performance-rx-masks", "--performance-signal-bits"] or
                any(masks[j] not in ("0", "1") for j in (1, 3, 5))):
            raise ResourcePerformanceError("invalid resource physical input arguments")
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="export-", dir=directory)) / "output"
    argv = argv_for(binary, config, seconds, seed, memory_mib, output, threads)
    if physical_mask_arguments is not None:
        argv += masks
    timeout = 120. if deadline is None else min(120., deadline - time.monotonic())
    if timeout <= 0:
        shutil.rmtree(output.parent)
        raise TimeoutError("resource workload deadline exhausted")
    outer = time.perf_counter()
    result = None; manifest = None; elapsed = None
    try:
        start = time.perf_counter()
        result = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        elapsed = time.perf_counter() - start
        if result.returncode:
            error_type = (ResourceWorkRegimeError if "bounded scratch/source estimate exceeds --memory-mib" in result.stderr
                          else ResourcePerformanceError)
            raise error_type(f"native resource export exited {result.returncode}: {result.stderr[-1600:]}")
        if (output.is_symlink() or not output.is_dir() or
                (output / "manifest.json").is_symlink() or not (output / "manifest.json").is_file()):
            raise ResourcePerformanceError("resource export contains unfinished, unexpected or unsafe files")
        manifest = json.loads((output / "manifest.json").read_text())
        if physical_mask_arguments is not None:
            metadata = manifest.get("performance_physical_input", {})
            if (metadata.get("schema") != "triad-performance-physical-bits-v1" or
                    metadata.get("tx_mask") != int(masks[1]) or metadata.get("rx_mask") != int(masks[3]) or
                    metadata.get("signal_physical_bit") != int(masks[5]) or
                    metadata.get("signal_plaintext_bit") != (int(masks[5]) ^ int(masks[1]))):
                raise ResourcePerformanceError("resource export did not apply frozen physical maps")
        regime, work = work_record(manifest, config, memory_mib, mode)
        expected_files = {"manifest.json", *FILES}
        if ({path.name for path in output.iterdir()} != expected_files or
                any((output / name).is_symlink() or not (output / name).is_file() for name in expected_files)):
            raise ResourcePerformanceError("resource export contains unfinished, unexpected or unsafe files")
        hashes = {}
        if set(manifest["file_sizes"]) != set(FILES):
            raise ResourcePerformanceError("unknown grouped statistics file set")
        for name in FILES:
            path = output / name
            if path.stat().st_size != manifest["file_sizes"][name]:
                raise ResourcePerformanceError("grouped statistics size differs from native manifest")
            hashes[name] = file_sha(path)
        record = {"requested_seconds": seconds, "seed": seed, "argv": argv,
                  "argv_sha256": digest(argv), "full_test_wall_seconds": elapsed,
                  "timing_boundary": "Popen/run to complete process exit; includes native generation, two channel passes, grouped preparation, writes and native finalization; excludes validation hashes and scratch cleanup",
                  "regime": regime, "work": work, "file_sha256": hashes,
                  "file_sizes": manifest["file_sizes"], "manifest_sha256": file_sha(output / "manifest.json"),
                  "native_execution": manifest["grouped_export_execution"],
                  "output_semantics": "all emitted sufficient-statistic binary files retained by digest; no receiver scoring or GPU execution"}
        if physical_mask_arguments is not None:
            record["physical_input_binding"] = {
                "trial": 0, "seed": manifest["legacy_seed"], "key_seed": manifest["key_seed"],
                "channel_seed": manifest["channel_seed"], "capture_samples": manifest["sample_count"],
                **{name: manifest[name] for name in ("source_samples", "source_format", "random_seed_roots",
                    "sample_rate_numerator", "sample_rate_denominator", "performance_physical_input")}}
    except Exception as exc:
        if failure_path is not None:
            _save(failure_path, sealed({"status": "work_regime_invalid" if isinstance(exc, ResourceWorkRegimeError) else "error",
                  "argv": argv, "seed": seed, "requested_seconds": seconds,
                  "expected_mode": mode, "memory_mib": memory_mib, "full_test_wall_seconds": elapsed,
                  "returncode": None if result is None else result.returncode,
                  "stderr": None if result is None else result.stderr, "manifest": manifest, "error": str(exc)}))
        raise
    finally:
        cleanup_start = time.perf_counter()
        shutil.rmtree(output.parent)
        cleanup = time.perf_counter() - cleanup_start
    record["harness_cleanup_seconds"] = cleanup
    record["harness_wall_seconds"] = time.perf_counter() - outer
    return sealed(record)


def verify_counter_record(row, config, *, mode, memory_mib, geometry=None):
    """Recompute derived work from the retained actual native branch counters."""
    if not verify_seal(row):
        raise ResourceWorkRegimeError("resource raw record integrity mismatch")
    r, w, ex = row["regime"], row["work"], row["native_execution"]
    rows = w["sample_count"]; block = r["block_samples"]
    passes = 1 if mode == "paired" else 2
    if (r["mode"] != mode or r["memory_mib"] != memory_mib or
            ex["channel_pass_mode"] != mode or ex["basis_synthesis_passes"] != passes or
            r["basis_synthesis_passes"] != passes or ex["worker_threads_used"] != 1 or r["worker_threads_used"] != 1 or
            r["bounded_workspace_bytes"] != ex["bounded_workspace_bytes"] or r["bounded_workspace_bytes"] > memory_mib * MIB or
            r["chunk_samples"] != config["chunk_samples"] or r["group_count"] != config["group_count"] or
            r["dimension"] != config["dimension"] or rows < config["chunk_samples"] or
            w["block_count"] != (rows + block - 1) // block or
            w["chunks_per_channel"] != math.ceil(rows / config["chunk_samples"]) or
            w["channel_passes"] != 2 or w["channel_rows"] != 2 * rows or
            w["basis_values"] != rows * r["group_count"] * r["dimension"] * passes or
            w["gram_terms"] != rows * r["group_count"] * r["dimension"] ** 2 or
            w["projection_terms"] != 2 * rows * r["group_count"] * r["dimension"] or
            set(row["file_sizes"]) != set(FILES) or set(row["file_sha256"]) != set(FILES) or
            w["file_bytes"] != sum(row["file_sizes"].values())):
        raise ResourceWorkRegimeError("resource actual work counters or frozen memory regime changed")
    if geometry is not None:
        g = geometry["performance_geometry"]
        if (rows != g["capture_samples"] or w["source_samples"] != g["source_samples"] or
                w["block_count"] != g["block_count"] or block != g["block_samples"] or
                r["chunk_samples"] != g["maximum_chunk_samples"] or r["sample_rate"] != g["sample_rate_hz"]):
            raise ResourceWorkRegimeError("resource full export disagrees with its frozen full-calendar geometry")
    return True


def compare_outputs(a, b, *, across_regimes=False):
    if a["seed"] != b["seed"] or a["requested_seconds"] != b["requested_seconds"]:
        raise ResourcePerformanceError("output comparison requires matched physical seeds and duration")
    if a["file_sizes"] != b["file_sizes"] or a["file_sha256"] != b["file_sha256"]:
        raise ResourcePerformanceError("paired exports differ in actual sufficient-statistic bytes")
    ignore = {"mode", "basis_synthesis_passes", "memory_mib", "bounded_workspace_bytes"} if across_regimes else set()
    if ({k: v for k, v in a["regime"].items() if k not in ignore} !=
            {k: v for k, v in b["regime"].items() if k not in ignore}):
        raise ResourcePerformanceError("paired exports changed computational geometry")
    return {"byte_identical": True, "files": list(FILES), "across_memory_regimes": across_regimes}


def bracket(pilot):
    if pilot["regime"]["mode"] != "paired":
        raise ResourcePerformanceError("memory bracket needs a paired pilot")
    threshold = pilot["native_execution"]["bounded_workspace_bytes"]
    result = {"paired": math.ceil(threshold / MIB), "sequential": (threshold - 1) // MIB}
    if result["sequential"] < 1 or result["paired"] == result["sequential"]:
        raise ResourcePerformanceError("no integer-MiB memory bracket")
    return result


def forecast(training, target_rows, config):
    a, b = sorted(training, key=lambda r: r["work"]["sample_count"])
    if (a["regime"] != b["regime"] or a["requested_seconds"] != config["training_seconds"][0] or
            b["requested_seconds"] != config["training_seconds"][1]):
        raise ResourcePerformanceError("duration fit crossed a resource/algorithm regime")
    x0, x1 = a["work"]["sample_count"], b["work"]["sample_count"]
    if not 0 < x0 < target_rows < x1:
        raise ResourcePerformanceError("duration fit must interpolate the full target's actual row geometry")
    slope = (b["full_test_wall_seconds"] - a["full_test_wall_seconds"]) / (x1 - x0)
    estimate = b["full_test_wall_seconds"] + slope * (target_rows - x1)
    if slope < 0 or estimate <= 0 or not math.isfinite(estimate):
        raise ResourcePerformanceError("duration forecast is nonphysical or unresolved")
    return {"full_test_wall_seconds": estimate, "seconds_per_row": slope,
            "intercept_seconds": a["full_test_wall_seconds"] - slope * x0,
            "large_endpoint_weight": (target_rows - x0) / (x1 - x0),
            "target_rows": target_rows, "regime_sha256": digest(a["regime"]),
            "training_record_sha256": [a["record_sha256"], b["record_sha256"]]}


def arithmetic_interval(values, alpha):
    from scipy.stats import t
    if len(values) < 3 or any(not math.isfinite(x) or x <= 0 for x in values):
        raise ResourcePerformanceError("arithmetic interval requires three finite positive outer observations")
    point = statistics.mean(values)
    half = float(t.ppf(1 - alpha / 2, len(values) - 1)) * statistics.stdev(values) / math.sqrt(len(values))
    return {"point_seconds": point, "interval_seconds": [point - half, point + half],
            "sample_count": len(values), "alpha": alpha, "estimand": "arithmetic mean complete export wall time"}


def qualification(predictions, actual, config):
    """Rectangular component CIs retain training and holdout mean uncertainty."""
    if len(actual) not in config["qualification_looks"]:
        raise ResourcePerformanceError("qualification inspected outside its fixed look schedule")
    # Two regimes, each with one frozen training interval and four possible
    # holdout intervals. Count the shared training uncertainty once.
    training_alpha = config["qualification_alpha"] / (2 * 2)
    holdout_alpha = training_alpha / len(config["qualification_looks"])
    predicted = arithmetic_interval(predictions, training_alpha)
    observed = arithmetic_interval(actual, holdout_alpha)
    p0, p1 = predicted["interval_seconds"]
    a0, a1 = observed["interval_seconds"]
    bounds = [a0 / p1, a1 / p0] if p0 > 0 and a0 > 0 else [0., None]
    allowance = config["model_relative_error_limit"]
    if bounds[1] is None:
        status = "inconclusive"
    elif bounds[0] >= 1 - allowance and bounds[1] <= 1 + allowance:
        status = "validated"
    elif bounds[1] < 1 - allowance or bounds[0] > 1 + allowance:
        status = "invalid"
    else:
        status = "inconclusive"
    return {"status": status, "forecast_mean": predicted, "actual_mean": observed,
            "actual_over_frozen_forecast_mean_interval": bounds,
            "allowed_ratio_interval": [1 - allowance, 1 + allowance],
            "validation_can_resolve_without_new_training": p0 > 0 and p1 / p0 <= (1 + allowance) / (1 - allowance),
            "family_error_budget": config["qualification_alpha"],
            "conditional_assumptions": "Independent stationary outer arithmetic runtimes with approximately normal mean inference; separate uncertainty in frozen training and independent full holds. Not a future-run bound.",
            "ledger": {"regimes": 2, "frozen_training_alpha_per_regime": training_alpha,
                       "holdout_alpha_per_regime_look": holdout_alpha,
                       "holdout_looks": config["qualification_looks"],
                       "total_alpha": 2 * (training_alpha + len(config["qualification_looks"]) * holdout_alpha)}}


def _save(path, value):
    Path(path).write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n")


def target_geometry(binary, config, *, threads=1, deadline=None):
    from triad_perf_native import target_geometry as native_geometry
    result = native_geometry(binary, {"args": config["args"], "target_seconds": config["target_seconds"]},
                             threads=threads, deadline=deadline)
    _target_rows(result)
    return {**result, "scope": "Deterministic configure probe before timed training and holdouts; only calendar rows are used. Configure's ungrouped basis-work counts are not the grouped export's work counters."}


def _target_rows(configuration):
    # Private pattern tiles quantize the requested duration. Infer neither the
    # target sample count nor work from a linear requested-seconds ratio.
    geometry = configuration.get("performance_geometry")
    if not isinstance(geometry, dict):
        raise ResourceWorkRegimeError("native configure lacks explicit full-calendar sample counters")
    rows = geometry.get("capture_samples")
    if isinstance(rows, bool) or not isinstance(rows, int) or rows < 1:
        raise ResourcePerformanceError("invalid deterministic full-target row geometry")
    return rows


def qualify(binary, source_root, output, *, deadline=None, threads=1, config=None):
    config = load_config() if config is None else config
    out = Path(output); out.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    implementation = implementation_identity()
    identity = binary_identity(binary, source_root, threads)
    geometry = target_geometry(binary, config, threads=threads, deadline=deadline)
    _save(out / "target-geometry.json", geometry)
    pilot = run_export(binary, config, 32, config["seed"], config["pilot_memory_mib"], "paired", out,
                       deadline=deadline, threads=threads)
    _save(out / "pilot.json", pilot)
    budgets = bracket(pilot)
    training = {r: [] for r in REGIMES}
    comparisons = []
    for i in range(config["training_replicates"]):
        pair = {r: [] for r in REGIMES}
        durations = config["training_seconds"] if i % 2 == 0 else config["training_seconds"][::-1]
        modes = REGIMES if (i // 2) % 2 == 0 else REGIMES[::-1]
        for seconds in durations:
            current = {}
            for mode in modes:
                current[mode] = run_export(binary, config, seconds, config["seed"] + 1000 + i * 10 + seconds,
                                           budgets[mode], mode, out, deadline=deadline, threads=threads)
                pair[mode].append(current[mode])
            comparisons.append(compare_outputs(current["paired"], current["sequential"], across_regimes=True))
        for mode in REGIMES:
            training[mode].append(pair[mode])
        _save(out / "training.json", {"training": training, "output_comparisons": comparisons,
                                       "identity": identity, "implementation": implementation})
    ensure_unchanged(implementation, {"candidate": identity}, {"candidate": binary}, {"candidate": source_root}, threads)
    targets = {mode: _target_rows(geometry) for mode in REGIMES}
    predictions = {mode: [forecast(pair, targets[mode], config) for pair in training[mode]] for mode in REGIMES}
    frozen = sealed({"schema": SCHEMA, "contract": CONTRACT, "implementation": implementation,
                     "identity": identity, "config": config, "budgets_mib": budgets, "target_geometry": geometry,
                     "training": training, "predictions": predictions,
                     "timing_boundary": pilot["timing_boundary"],
                     "held_seed_base": config["seed"] + 100000,
                     "training_order": "fixed 33 outer replicates; duration order alternates every replicate, memory-regime order every two replicates; no adaptive training or outlier exclusion",
                     "frozen_before_independent_full_work": True})
    _save(out / "frozen.json", frozen)
    holds = {mode: [] for mode in REGIMES}
    looks = []
    for i in range(config["qualification_looks"][-1]):
        current = {}
        for mode in (REGIMES if i % 2 == 0 else REGIMES[::-1]):
            row = run_export(binary, config, config["target_seconds"], config["seed"] + 100000 + i,
                             budgets[mode], mode, out, deadline=deadline, threads=threads)
            holds[mode].append(row); current[mode] = row
            _save(out / "full-observations.json", {"frozen_sha256": frozen["record_sha256"], "holds": holds})
            if (row["work"]["sample_count"] != targets[mode] or
                    digest(row["regime"]) != predictions[mode][0]["regime_sha256"]):
                raise ResourcePerformanceError("full holdout changed the frozen row geometry or memory regime")
        comparisons.append(compare_outputs(current["paired"], current["sequential"], across_regimes=True))
        if i + 1 in config["qualification_looks"]:
            checks = {mode: qualification([p["full_test_wall_seconds"] for p in predictions[mode]],
                                         [r["full_test_wall_seconds"] for r in holds[mode]], config) for mode in REGIMES}
            looks.append({"count": i + 1, "regimes": checks})
            _save(out / "progress.json", {"frozen_sha256": frozen["record_sha256"], "holds": holds, "looks": looks})
            if (all(c["status"] == "validated" for c in checks.values()) or
                    any(c["status"] == "invalid" or not c["validation_can_resolve_without_new_training"] for c in checks.values())):
                break
    states = {r["status"] for r in looks[-1]["regimes"].values()}
    status = "validated" if states == {"validated"} else "invalid" if "invalid" in states else "inconclusive"
    ensure_unchanged(implementation, {"candidate": identity}, {"candidate": binary}, {"candidate": source_root}, threads)
    anchor = sealed({"schema": ANCHOR_SCHEMA, "status": status, "frozen": frozen,
                     "pilot": pilot, "full_holdouts": holds, "confirmation_looks": looks,
                     "output_comparisons": comparisons, "elapsed_seconds": time.monotonic() - start,
                     "scope": config["scope"], "current_regression_claim": False})
    _save(out / "anchor.json", anchor)
    return anchor


def historical_proof(anchor, config=None):
    """Recompute source-bound frozen predictions and independent confirmations."""
    config = load_config() if config is None else config
    try:
        frozen = anchor["frozen"]
        if (not verify_seal(anchor) or not verify_seal(frozen) or anchor["schema"] != ANCHOR_SCHEMA or
                anchor["status"] != "validated" or frozen["contract"] != CONTRACT or frozen["config"] != config or
                frozen["implementation"] != implementation_identity() or
                frozen["budgets_mib"] != bracket(anchor["pilot"]) or not frozen["frozen_before_independent_full_work"]):
            return False
        n = len(anchor["full_holdouts"]["paired"])
        if n not in config["qualification_looks"] or len(anchor["full_holdouts"]["sequential"]) != n:
            return False
        for mode in REGIMES:
            train = frozen["training"][mode]
            target = _target_rows(frozen["target_geometry"])
            expected = [forecast(pair, target, config) for pair in train]
            if len(train) != config["training_replicates"] or expected != frozen["predictions"][mode]:
                return False
            for i, pair in enumerate(train):
                if not all(verify_seal(row) and row["seed"] == config["seed"] + 1000 + i * 10 + row["requested_seconds"] for row in pair):
                    return False
            for i, row in enumerate(anchor["full_holdouts"][mode]):
                if (not verify_seal(row) or row["seed"] != config["seed"] + 100000 + i or
                        row["requested_seconds"] != config["target_seconds"] or row["work"]["sample_count"] != target or
                        digest(row["regime"]) != expected[0]["regime_sha256"]):
                    return False
        expected_looks = []
        for count in config["qualification_looks"]:
            if count > n:
                break
            checks = {mode: qualification([p["full_test_wall_seconds"] for p in frozen["predictions"][mode]],
                        [r["full_test_wall_seconds"] for r in anchor["full_holdouts"][mode][:count]], config) for mode in REGIMES}
            expected_looks.append({"count": count, "regimes": checks})
        if expected_looks != anchor["confirmation_looks"] or any(c["status"] != "validated" for c in expected_looks[-1]["regimes"].values()):
            return False
        for look in expected_looks[:-1]:
            checks = look["regimes"].values()
            if (all(c["status"] == "validated" for c in checks) or
                    any(c["status"] == "invalid" or not c["validation_can_resolve_without_new_training"] for c in checks)):
                return False
        for i in range(config["training_replicates"]):
            for j in range(2):
                compare_outputs(frozen["training"]["paired"][i][j], frozen["training"]["sequential"][i][j], across_regimes=True)
        for a, b in zip(anchor["full_holdouts"]["paired"], anchor["full_holdouts"]["sequential"], strict=True):
            compare_outputs(a, b, across_regimes=True)
        return True
    except (KeyError, ValueError, TypeError, IndexError, ResourcePerformanceError, OverflowError):
        return False


def routine(binary, reference_binary, source_root, reference_root, anchor, output, *, deadline=None,
            threads=1, profile="normal", config=None):
    from triad_perf_adaptive import AdaptivePolicy, regression_profile, summarize
    config = load_config() if config is None else config
    if not historical_proof(anchor, config):
        raise ResourcePerformanceError("resource anchor historical proof is absent, changed or unqualified")
    start = time.monotonic(); out = Path(output); out.mkdir(parents=True, exist_ok=True)
    implementation = implementation_identity()
    policy = AdaptivePolicy(stopping_rule="decision", **regression_profile(profile))
    identities = {name: binary_identity(path, root, threads) for name, path, root in
                  (("reference", reference_binary, reference_root), ("candidate", binary, source_root))}
    # Fresh pilots locate each binary's real branch. Historical brackets never
    # silently force a new executable into the wrong resource regime.
    pilots = {name: run_export(path, config, 32, config["seed"] + 200000, config["pilot_memory_mib"], "paired", out,
                               deadline=deadline, threads=threads)
              for name, path in (("reference", reference_binary), ("candidate", binary))}
    budgets = {name: bracket(row) for name, row in pilots.items()}
    cases = []; cross_checks = []
    for mode in REGIMES:
        pairs = []; looks = []
        for i in range(policy.looks[-1]):
            pair = {}
            for name in (("reference", "candidate") if i % 2 == 0 else ("candidate", "reference")):
                path = binary if name == "candidate" else reference_binary
                pair[name] = run_export(path, config, config["target_seconds"], config["seed"] + 300000 + i,
                                        budgets[name][mode], mode, out, deadline=deadline, threads=threads)
            compare_outputs(pair["reference"], pair["candidate"])
            pairs.append(pair)
            if i + 1 in policy.looks:
                summary = summarize([r["reference"]["full_test_wall_seconds"] for r in pairs],
                                    [r["candidate"]["full_test_wall_seconds"] for r in pairs], policy,
                                    model_validation="direct_full")
                looks.append(summary)
                if summary["stop_reason"] != "continue":
                    break
        cases.append({"id": f"native-grouped-memory-{mode}", "regime": mode,
                      "prediction_method": "direct_full_target", "raw_pairs": pairs, "looks": looks,
                      "summary": looks[-1], "current_model_extrapolation": False,
                      "computed_saved_seconds_against_direct": 0.})
        _save(out / "progress.json", {"cases": cases, "identities": identities})
    for i in range(min(len(c["raw_pairs"]) for c in cases)):
        for name in ("reference", "candidate"):
            cross_checks.append(compare_outputs(cases[0]["raw_pairs"][i][name], cases[1]["raw_pairs"][i][name], across_regimes=True))
    gates = {case["summary"]["performance_gate"] for case in cases}
    status = "pass" if gates == {"pass"} else "regression" if "performance_regression" in gates else "inconclusive"
    ensure_unchanged(implementation, identities, {"reference": reference_binary, "candidate": binary},
                     {"reference": reference_root, "candidate": source_root}, threads)
    return sealed({"schema": SCHEMA, "mode": "routine", "status": status,
                   "complete_scope": status == "pass", "finite_gap_ids": [config["gap_id"]],
                   "scope": config["scope"], "device": "cpu", "threads": threads,
                   "profile": profile, "policy": asdict(policy), "identities": identities,
                   "implementation": implementation, "config": config,
                   "historical_anchor_sha256": anchor["record_sha256"],
                   "historical_binary_sha256": anchor["frozen"]["identity"]["binary_sha256"],
                   "historical_prediction_validated": True,
                   "historical_model_applied_to_current_binary": False,
                   "pilots": pilots, "budgets_mib": budgets, "cases": cases,
                   "cross_regime_output_checks": cross_checks,
                   "outcomes": {"functional": "pass", "work_regime": "pass", "historical_prediction": "validated",
                                "performance_regression": status},
                   "routine_method_reason": config["routine_reason"],
                   "elapsed_seconds": time.monotonic() - start, "errors": [],
                   "limitations": ["No GPU memory, allocator fragmentation, paging, spill or OOM throughput claim.",
                                   "Direct routine measurements require no transfer of old duration models across source or hardware changes.",
                                   "Historical ±10% arithmetic mean equivalence is conditional and is not a future-error guarantee."]})


def history_note(anchor):
    """Integrity-only citation of optional history; never a timing-model proof."""
    if anchor is None:
        return {"status": "not_provided", "forecast_used": False, "proof_replayed": False}
    if not verify_seal(anchor) or anchor.get("status") not in ("validated", "inconclusive", "invalid", "error"):
        raise ResourcePerformanceError("optional historical resource record has invalid integrity/status")
    return {"status": anchor["status"] + "_not_applied", "recorded_status": anchor["status"],
            "record_sha256": anchor["record_sha256"], "schema": anchor.get("schema"),
            "forecast_used": False, "proof_replayed": False}


def direct(binary, reference_binary, source_root, reference_root, output, *, anchor=None,
           deadline=None, threads=1, profile="normal", config=None):
    """Measure full exports directly at one frozen shared memory bracket.

    Forecast qualification is unnecessary for a fully executed target. The
    optional historical record remains visibly unused, including failed or
    inconclusive forecasts. Threshold movement cannot select a different budget
    independently for each binary: it invalidates the declared work regime.
    """
    from triad_perf_adaptive import AdaptivePolicy, regression_profile, summarize
    config = load_config() if config is None else config
    start = time.monotonic(); out = Path(output); out.mkdir(parents=True, exist_ok=True)
    implementation = implementation_identity(); history = history_note(anchor)
    policy = AdaptivePolicy(stopping_rule="decision", **regression_profile(profile))
    binaries = {"reference": reference_binary, "candidate": binary}
    roots = {"reference": reference_root, "candidate": source_root}
    identities = {name: binary_identity(binaries[name], roots[name], threads) for name in binaries}
    # This one public workload is shared. In particular, a historical reference
    # may predate configure's explicit geometry fields; its actual completed
    # export still has to match the same frozen counters and physical bytes.
    geometry = target_geometry(binary, config, threads=threads, deadline=deadline)
    geometries = {name: geometry for name in binaries}
    pilot = run_export(binary, config, 32, config["seed"] + 200000, config["pilot_memory_mib"], "paired", out,
                       deadline=deadline, threads=threads, failure_path=out / "failed-pilot.json")
    verify_counter_record(pilot, config, mode="paired", memory_mib=config["pilot_memory_mib"])
    budgets = bracket(pilot)
    ensure_unchanged(implementation, identities, binaries, roots, threads)
    frozen = sealed({"schema": SCHEMA, "mode": "direct", "contract": "complete-grouped-export-direct-shared-memory-v1",
                     "implementation": implementation, "identities": identities, "config": config,
                     "policy": asdict(policy), "profile": profile, "target_geometries": geometries,
                     "candidate_pilot": pilot, "budgets_mib": budgets,
                     "target_seconds": config["target_seconds"], "seed_base": config["seed"] + 400000,
                     "memory_policy": "candidate paired pilot fixes both budgets; both binaries must execute both expected modes at those same budgets",
                     "geometry_policy": "candidate configure supplies one public expected calendar; actual complete exports from both binaries must match",
                     "frozen_before_full_measurements": True})
    _save(out / "frozen-direct.json", frozen)
    cases = []; observations = []; cross_checks = []
    for mode in REGIMES:
        pairs = []; looks = []
        for i in range(policy.looks[-1]):
            pair = {}
            for name in (("reference", "candidate") if i % 2 == 0 else ("candidate", "reference")):
                row = run_export(binaries[name], config, config["target_seconds"], config["seed"] + 400000 + i,
                                 budgets[mode], mode, out, deadline=deadline, threads=threads,
                                 failure_path=out / f"failed-{mode}-{i}-{name}.json")
                pair[name] = row
                observations.append({"regime": mode, "index": i, "source": name, "record": row})
                _save(out / "direct-observations.json", {"frozen_sha256": frozen["record_sha256"], "observations": observations})
                verify_counter_record(row, config, mode=mode, memory_mib=budgets[mode], geometry=geometries[name])
            compare_outputs(pair["reference"], pair["candidate"])
            pairs.append(pair)
            if i + 1 in policy.looks:
                summary = summarize([r["reference"]["full_test_wall_seconds"] for r in pairs],
                                    [r["candidate"]["full_test_wall_seconds"] for r in pairs], policy,
                                    model_validation="direct_full")
                looks.append(summary)
                if summary["stop_reason"] != "continue":
                    break
        cases.append({"id": f"native-grouped-memory-{mode}", "regime": mode,
                      "prediction_method": "direct_full_target", "measured_fraction": 1.,
                      "computed_saved_seconds_against_direct": 0., "forecast_used": False,
                      "raw_pairs": pairs, "looks": looks, "summary": looks[-1]})
        _save(out / "progress.json", {"frozen_sha256": frozen["record_sha256"], "cases": cases})
    for i in range(min(len(c["raw_pairs"]) for c in cases)):
        for name in binaries:
            cross_checks.append(compare_outputs(cases[0]["raw_pairs"][i][name], cases[1]["raw_pairs"][i][name], across_regimes=True))
    gates = {case["summary"]["performance_gate"] for case in cases}
    status = "pass" if gates == {"pass"} else "regression" if "performance_regression" in gates else "inconclusive"
    ensure_unchanged(implementation, identities, binaries, roots, threads)
    return sealed({"schema": SCHEMA, "mode": "direct", "status": status,
                   "complete_scope": status == "pass", "finite_gap_ids": [config["gap_id"]],
                   "scope": config["scope"], "device": "cpu", "threads": threads,
                   "profile": profile, "policy": asdict(policy), "identities": identities,
                   "implementation": implementation, "config": config, "frozen_direct": frozen,
                   "pilots": {"candidate": pilot}, "budgets_mib": budgets, "target_geometries": geometries,
                   "historical_model": history, "forecast_used": False, "prediction_method": "direct_full_target",
                   "measured_fraction": 1., "computed_saved_seconds_against_direct": 0.,
                   "cases": cases, "cross_regime_output_checks": cross_checks,
                   "outcomes": {"functional": "pass", "work_regime": "pass", "historical_prediction": "not_used_direct_full_target",
                                "performance_regression": status},
                   "routine_method_reason": config["routine_reason"], "elapsed_seconds": time.monotonic() - start,
                   "errors": [], "limitations": ["No current duration forecast or GPU memory-model qualification is claimed.",
                                                   "Complete native process wall time is directly measured; no abbreviation savings are claimed.",
                                                   "Historical failed/inconclusive models remain unchanged and are not applied."]})


def validate_report(report, *, anchor_path=None, binary, reference_binary, source_root=ROOT,
                    reference_root=ROOT, threads=1, profile="normal", device="cpu", mode="routine"):
    """Wrapper receipt guard: replay proof, raw decisions and current identities."""
    from triad_perf_adaptive import AdaptivePolicy, regression_profile, summarize
    issues = []
    try:
        config = load_config(); anchor = json.loads(Path(anchor_path).read_text()) if anchor_path else None
        if mode not in ("routine", "direct"):
            raise ResourcePerformanceError("unknown resource receipt mode")
        if mode == "routine" and not historical_proof(anchor, config):
            issues.append("historical resource proof invalid")
        if not verify_seal(report) or report.get("schema") != SCHEMA or report.get("mode") != mode:
            issues.append("resource report integrity/schema mismatch")
        policy = AdaptivePolicy(stopping_rule="decision", **regression_profile(profile))
        identities = {name: binary_identity(path, root, threads) for name, path, root in
                      (("reference", reference_binary, reference_root), ("candidate", binary, source_root))}
        if (report["implementation"] != implementation_identity() or report["config"] != config or
                report["identities"] != identities or digest(report["policy"]) != digest(asdict(policy)) or
                report["profile"] != profile or report["device"] != "cpu" or device != "cpu" or
                report["threads"] != threads):
            issues.append("resource current source/build/input/environment/profile identity mismatch")
        if mode == "routine" and report["historical_anchor_sha256"] != anchor["record_sha256"]:
            issues.append("resource historical anchor identity mismatch")
        if mode == "direct":
            frozen = report["frozen_direct"]
            if (not verify_seal(frozen) or frozen["mode"] != "direct" or
                    frozen["contract"] != "complete-grouped-export-direct-shared-memory-v1" or
                    frozen["implementation"] != report["implementation"] or frozen["identities"] != identities or
                    frozen["config"] != config or digest(frozen["policy"]) != digest(asdict(policy)) or
                    frozen["profile"] != profile or frozen["target_seconds"] != config["target_seconds"] or
                    frozen["seed_base"] != config["seed"] + 400000 or not frozen["frozen_before_full_measurements"] or
                    frozen["budgets_mib"] != report["budgets_mib"] or
                    frozen["candidate_pilot"] != report["pilots"]["candidate"] or
                    frozen["target_geometries"] != report["target_geometries"] or
                    report["target_geometries"]["reference"] != report["target_geometries"]["candidate"] or
                    report["historical_model"] != history_note(anchor) or report["forecast_used"] is not False or
                    report["prediction_method"] != "direct_full_target" or report["measured_fraction"] != 1 or
                    report["computed_saved_seconds_against_direct"] != 0):
                issues.append("resource frozen direct plan or full-measurement contract mismatch")
        if [c["regime"] for c in report["cases"]] != list(REGIMES):
            issues.append("resource regime inventory mismatch")
        for name in (("candidate",) if mode == "direct" else ("reference", "candidate")):
            pilot = report["pilots"][name]
            budgets = report["budgets_mib"] if mode == "direct" else report["budgets_mib"][name]
            if (not verify_seal(pilot) or pilot["requested_seconds"] != 32 or
                    pilot["seed"] != config["seed"] + 200000 or
                    pilot["regime"]["memory_mib"] != config["pilot_memory_mib"] or
                    budgets != bracket(pilot)):
                issues.append("resource pilot or memory bracket proof mismatch")
            verify_counter_record(pilot, config, mode="paired", memory_mib=config["pilot_memory_mib"])
        for case in report["cases"]:
            pairs = case["raw_pairs"]
            if len(pairs) not in policy.looks:
                issues.append("resource sample count is not a predeclared look")
            for i, pair in enumerate(pairs):
                if not all(verify_seal(row) for row in pair.values()):
                    issues.append("resource raw record integrity mismatch")
                compare_outputs(pair["reference"], pair["candidate"])
                if any(r["requested_seconds"] != config["target_seconds"] or
                       r["regime"]["mode"] != case["regime"] for r in pair.values()):
                    issues.append("resource routine did not directly execute the full target")
                if mode == "direct":
                    for name, row in pair.items():
                        verify_counter_record(row, config, mode=case["regime"], memory_mib=report["budgets_mib"][case["regime"]],
                                              geometry=report["target_geometries"][name])
                        if row["seed"] != config["seed"] + 400000 + i:
                            issues.append("resource direct seed schedule mismatch")
                        path = binary if name == "candidate" else reference_binary
                        expected_argv = argv_for(path, config, config["target_seconds"], config["seed"] + 400000 + i,
                                                 report["budgets_mib"][case["regime"]], row["argv"][-1], threads)
                        if row["argv"] != expected_argv or row["argv_sha256"] != digest(expected_argv):
                            issues.append("resource direct executable/input invocation mismatch")
                    if (case["prediction_method"] != "direct_full_target" or case["measured_fraction"] != 1 or
                            case["computed_saved_seconds_against_direct"] != 0 or case["forecast_used"] is not False):
                        issues.append("resource direct case is not completely measured")
            expected = []
            for count in policy.looks:
                if count > len(pairs):
                    break
                expected.append(summarize([r["reference"]["full_test_wall_seconds"] for r in pairs[:count]],
                                          [r["candidate"]["full_test_wall_seconds"] for r in pairs[:count]],
                                          policy, model_validation="direct_full"))
            if expected != case["looks"] or expected[-1] != case["summary"]:
                issues.append("resource adaptive proof mismatch")
            if any(look["stop_reason"] != "continue" for look in expected[:-1]):
                issues.append("resource measurements continued after a terminal decision")
        cross_checks = []
        for i in range(min(len(c["raw_pairs"]) for c in report["cases"])):
            for name in ("reference", "candidate"):
                cross_checks.append(compare_outputs(report["cases"][0]["raw_pairs"][i][name],
                                      report["cases"][1]["raw_pairs"][i][name], across_regimes=True))
        if cross_checks != report["cross_regime_output_checks"]:
            issues.append("resource cross-regime byte parity proof mismatch")
        gates = {c["summary"]["performance_gate"] for c in report["cases"]}
        status = "pass" if gates == {"pass"} else "regression" if "performance_regression" in gates else "inconclusive"
        expected_outcomes = {"functional": "pass", "work_regime": "pass", "historical_prediction": "validated" if mode == "routine" else "not_used_direct_full_target",
                             "performance_regression": status}
        if (report["status"] != status or report["complete_scope"] != (status == "pass") or
                report["outcomes"] != expected_outcomes or report["finite_gap_ids"] != [config["gap_id"]]):
            issues.append("resource decision or complete-scope receipt inconsistent with measured evidence")
    except (OSError, ValueError, TypeError, KeyError, IndexError, ResourcePerformanceError) as exc:
        issues.append(f"resource receipt invalid: {exc}")
    return {"valid": not issues, "issues": issues,
            "finite_gap_ids": ["resource.memory-thresholds"] if not issues and report.get("status") == "pass" else [],
            "scope": "one real native simulation/preparation memory transition; CPU only"}


def validate_routine_report(report, context):
    """Shared complete-suite extension receipt interface.

    This extension executes native CPU exports even in an outer GPU campaign;
    its finite-gap receipt never claims a GPU allocator transition.
    """
    checked = validate_report(report, anchor_path=context.get("anchor"), binary=context["binary"],
                              reference_binary=context["reference_binary"],
                              source_root=context["candidate_root"], reference_root=context["reference_root"],
                              threads=context["native_threads"], profile=context["regression_profile"], device="cpu",
                              mode=context.get("mode", "routine"))
    status = ({"pass": "passed", "regression": "failed", "inconclusive": "inconclusive"}.get(report.get("status"), "unqualified")
              if checked["valid"] else "unqualified")
    return {"valid": checked["valid"], "status": status, "errors": checked["issues"],
            "gap_ids": checked["finite_gap_ids"], "outcomes": report.get("outcomes", {}),
            "execution_backend": "cpu", "scope": checked["scope"]}


def cli(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("qualify", "routine", "direct"), required=True)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--reference-binary")
    parser.add_argument("--source-root", default=str(ROOT))
    parser.add_argument("--reference-root", default=str(ROOT))
    parser.add_argument("--anchor")
    parser.add_argument("--output", required=True)
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--budget-seconds", type=float, default=300.)
    parser.add_argument("--regression-profile", choices=("normal", "ci", "strict"), default="normal")
    args = parser.parse_args(argv)
    if args.threads != 1 or not math.isfinite(args.budget_seconds) or args.budget_seconds <= 0:
        parser.error("resource target requires one native thread and a finite positive budget")
    if args.mode == "routine" and (not args.reference_binary or not args.anchor):
        parser.error("routine requires --reference-binary and a qualified --anchor")
    if args.mode == "direct" and not args.reference_binary:
        parser.error("direct requires --reference-binary; --anchor is optional unused history")
    out = Path(args.output); out.mkdir(parents=True, exist_ok=True)
    start = time.monotonic(); deadline = start + args.budget_seconds
    try:
        if args.mode == "qualify":
            result = qualify(args.binary, args.source_root, out, deadline=deadline, threads=args.threads)
            code = 0 if result["status"] == "validated" else 2
        elif args.mode == "direct":
            anchor = json.loads(Path(args.anchor).read_text()) if args.anchor else None
            result = direct(args.binary, args.reference_binary, args.source_root, args.reference_root, out,
                            anchor=anchor, deadline=deadline, threads=args.threads, profile=args.regression_profile)
            code = 0 if result["status"] == "pass" else 1 if result["status"] == "regression" else 2
        else:
            anchor = json.loads(Path(args.anchor).read_text())
            result = routine(args.binary, args.reference_binary, args.source_root, args.reference_root,
                             anchor, out, deadline=deadline, threads=args.threads, profile=args.regression_profile)
            code = 0 if result["status"] == "pass" else 1 if result["status"] == "regression" else 2
    except (OSError, ValueError, ResourcePerformanceError, TimeoutError, subprocess.TimeoutExpired) as exc:
        result = sealed({"schema": SCHEMA, "mode": args.mode, "status": "error", "complete_scope": False,
                         "failure_kind": "work_regime_invalid" if isinstance(exc, ResourceWorkRegimeError) else "error",
                         "errors": [str(exc)], "elapsed_seconds": time.monotonic() - start})
        code = 2
    _save(out / "report.json", result)
    _save(out / "summary.json", result)
    print(json.dumps({"status": result["status"], "elapsed_seconds": result["elapsed_seconds"],
                      "report": str(out / "report.json")}, sort_keys=True))
    return code


if __name__ == "__main__":
    sys.exit(cli())
