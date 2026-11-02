#!/usr/bin/env python3
"""Audit frozen selective holdout exports and score reports at cell level.

This tool binds every planned path to its expected deterministic identity,
campaign command and manifest; then independently invokes the frozen screen
analyzer to validate score-plan inputs, reports, arrays, thresholds and counts.
It reports each declared cell separately. It does not pool confidence across
cells or qualify field performance / an operational false-alarm tail.

Example (paths in the plans are rooted at /workspace/triad):
  python audit_holdout.py --root local-evidence --source-root /workspace/triad \
    --export-plan holdout-export-plan.json --score-plan holdout-score-plan.json \
    --output holdout-audit.json
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import importlib.util
import json
import math
import sys
import tempfile
from pathlib import Path
from typing import Any


HERE = Path(__file__).resolve().parent
CAMPAIGN_PATH = HERE.parents[4] / "tools" / "triad-lowrate-campaign.py"
SCREEN_PATH = HERE / "analyze_screen.py"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load audit dependency: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


CAMPAIGN = load_module("triad_holdout_campaign", CAMPAIGN_PATH)
SCREEN = load_module("triad_holdout_screen", SCREEN_PATH)
if str(CAMPAIGN_PATH.parent) not in sys.path:
    sys.path.insert(0, str(CAMPAIGN_PATH.parent))
SCORER = load_module("triad_holdout_actual_receiver", CAMPAIGN_PATH.with_name("triad-conditional-sweep.py"))
require = SCREEN.require
sha256 = SCREEN.sha256


def read_json(path: Path) -> dict[str, Any]:
    value = SCREEN.read_json(path)
    require(isinstance(value, dict), f"expected JSON object: {path}")
    return value


class PathMap:
    def __init__(self, root: Path, source_root: Path):
        self.root = root.resolve()
        self.source_root = source_root
        require(source_root.is_absolute() and source_root != Path("/"),
                "source root must be a non-root absolute path")

    def resolve(self, original: str) -> Path:
        source = Path(original)
        require(source.is_absolute() and ".." not in source.parts,
                f"planned path is not canonical absolute path: {original}")
        try:
            relative = source.relative_to(self.source_root)
        except ValueError as exc:
            raise ValueError(f"planned path outside source root: {original}") from exc
        local = (self.root / relative).resolve()
        require(local.is_relative_to(self.root), f"mapped path escaped evidence root: {original}")
        return local


def _options(argv: list[str]) -> dict[str, list[str]]:
    """Parse the campaign's frozen argv without mutating or validating paths."""
    require(len(argv) >= 3 and Path(argv[1]).name == "triad-lowrate-campaign.py",
            "export job is not a triad-lowrate-campaign invocation")
    args = CAMPAIGN.parser().parse_args(argv[2:])
    return vars(args)


def _expected_child(args, index: int, output: Path):
    if isinstance(args, dict):
        args = argparse.Namespace(**args)
    command, truth = CAMPAIGN.make_command(args, index, output)
    return command, truth


def _seed_roots(manifest: dict[str, Any]) -> dict[str, int] | None:
    roots = manifest.get("random_seed_roots")
    if not isinstance(roots, dict):
        return None
    names = ("channel", "oscillator", "noise", "frontend", "path")
    if any(type(roots.get(name)) is not int for name in names):
        return None
    if type(manifest.get("key_seed")) is not int:
        return None
    return {"key_seed": manifest["key_seed"], **{name + "_seed": roots[name] for name in names}}


def _manifest_checks(manifest: dict[str, Any], expected: dict[str, Any], args,
                     actual_child_command: list[str]) -> list[str]:
    errors: list[str] = []
    if isinstance(args, dict):
        args = argparse.Namespace(**args)
    for field in ("source_bit", "epoch", "control"):
        actual = {"source_bit": manifest.get("signal_bit"), "epoch": manifest.get("epoch"),
                  "control": manifest.get("control")}[field]
        if actual != expected[field]:
            errors.append(f"manifest {field} differs from frozen expected identity")
    if _seed_roots(manifest) != {k: expected[k] for k in expected if k.endswith("_seed")}:
        errors.append("manifest six seed roots differ from frozen expected identity")
    if manifest.get("stats_noisy") is not True:
        errors.append("manifest is not an actual sampled-noise export")
    source_format = manifest.get("source_format")
    if not isinstance(source_format, dict) or source_format.get("encrypted") is not True:
        errors.append("manifest does not establish the planned encrypted source format")
    if isinstance(source_format, dict):
        if source_format.get("added_symbols") != 1 or source_format.get("added_interleave") is not True:
            errors.append("manifest Added Symbols configuration differs from plan")
        if not math.isclose(float(source_format.get("inner_rate_hz", math.nan)), args.rate, rel_tol=0, abs_tol=1e-12):
            errors.append("manifest configured inner rate differs from export plan")
    if manifest.get("sample_rate_numerator") != args.sample_rate or \
            manifest.get("sample_rate_denominator") != args.sample_rate_denominator:
        errors.append("manifest sample-rate rational differs from export plan")
    if not math.isclose(float(manifest.get("sample_rate", math.nan)),
                        args.sample_rate / args.sample_rate_denominator, rel_tol=0, abs_tol=1e-12):
        errors.append("manifest sample rate differs from export plan")
    if manifest.get("requested_active_seconds") != args.seconds:
        errors.append("manifest requested duration differs from export plan")
    if manifest.get("block_samples") != math.ceil(args.block_seconds * args.sample_rate / args.sample_rate_denominator):
        errors.append("manifest block sample count differs from export plan")
    resolved = manifest.get("resolved_configuration", {})
    expected_resolved = {
        "base_oscillator": "gpsdo-ocxo", "shift_oscillator": "gpsdo-ocxo",
        "rf_center_hz": args.rf_hz, "margin": 1,
        "clock_sync": "GPS_10ms-1ms_region-0ms_offset", "audio_error_seconds_per_station": .07,
        "spread_preset": args.spread_preset,
        "selective_perturbation": args.selective_perturbation,
        "spread_delay_ms": args.spread_delay_ms,
        "spread_doppler_hz": args.spread_doppler_hz,
    }
    for key, value in expected_resolved.items():
        if resolved.get(key) != value:
            errors.append(f"resolved configuration {key} differs from export plan")
    burst = resolved.get("whole_bit_symbol_burst", {})
    if not isinstance(burst, dict) or burst.get("enabled") is not False:
        errors.append("manifest does not establish Symbol Burst off")
    if manifest.get("channel_model", {}).get("physical_rf_retuning") is not False:
        errors.append("manifest physical RF retuning state differs from FHSS-off plan")
    if not math.isclose(float(manifest.get("cn0_db_hz", math.nan)), args.cn0, rel_tol=0, abs_tol=1e-12):
        errors.append("manifest C/N0 differs from export plan")
    fs = args.sample_rate / args.sample_rate_denominator
    expected_noise = fs / 10 ** (args.cn0 / 10)
    if not math.isclose(float(manifest.get("noise_variance", math.nan)), expected_noise,
                        rel_tol=1e-12, abs_tol=0):
        errors.append("manifest noise variance differs from Fs/CN0")
    model = manifest.get("channel_model", {})
    mode = args.selective_perturbation
    if mode == "off":
        depths = (0., 0.)
    else:
        kind, percentage = mode.rsplit("-", 1); depth = int(percentage) / 100
        depths = (depth if kind != "fast" else 0., depth if kind != "slow" else 0.)
    physical = {"spread_delay_ms": args.spread_delay_ms, "spread_doppler_hz": args.spread_doppler_hz,
                "perturbation_slow_depth": depths[0], "perturbation_fast_depth": depths[1],
                "rf_hz": args.rf_hz, "signal_gain": 1.}
    for key, value in physical.items():
        if value is None or not math.isclose(float(model.get(key, math.nan)), value, rel_tol=1e-12, abs_tol=1e-15):
            errors.append(f"effective channel {key} differs from frozen physical settings")
    for key, value in (("perturbation_slow_depth", depths[0]), ("perturbation_fast_depth", depths[1])):
        if not math.isclose(float(resolved.get(key, math.nan)), value, rel_tol=0, abs_tol=1e-15):
            errors.append(f"resolved channel {key} differs from selected AM depth")
    path = model.get("path_evolution", {})
    for key, value in {"gaussian_doppler_width_hz": args.ray_gaussian_width_hz,
                       "ray_coherence_seconds": args.ray_coherence_seconds,
                       "diurnal_phase_amplitude_radians": args.diurnal_phase_radians,
                       "diurnal_period_seconds": args.diurnal_period_seconds,
                       "outage_start_seconds": args.outage_seconds[0], "outage_end_seconds": args.outage_seconds[1],
                       "outage_gain": args.outage_gain, "outage_ramp_seconds": args.outage_ramp_seconds}.items():
        if not math.isclose(float(path.get(key, math.nan)), value, rel_tol=1e-12, abs_tol=1e-15):
            errors.append(f"effective path {key} differs from frozen physical settings")
    if isinstance(source_format, dict):
        for key, value in {"fine_rate_hz": args.rate / 6, "carrier_spacing_hz": 1.25 * args.rate / 6,
                           "occupied_bandwidth_hz": 3.75 * args.rate / 6, "quiet_seconds": 0.}.items():
            if not math.isclose(float(source_format.get(key, math.nan)), value, rel_tol=1e-12, abs_tol=1e-15):
                errors.append(f"source format {key} differs from frozen waveform/FHSS-off settings")
    if manifest.get("sampled_frontend", {}).get("active") is not False:
        errors.append("sampled frontend differs from the declared disabled baseline")
    oscillator = manifest.get("oscillator_model", {})
    for key in ("relative_white_fm_hz_sqrt_s", "relative_random_walk_fm_hz_sqrt_s",
                "relative_flicker_fm_rms_hz", "relative_white_pm_rms_rad"):
        if oscillator.get(key) != 0:
            errors.append(f"oscillator {key} differs from frozen defaults")
    if oscillator.get("relative_phase_diffusion_rad_sqrt_s") != resolved.get("relative_phase_diffusion_rad_sqrt_s"):
        errors.append("oscillator phase diffusion and selected endpoint model disagree")
    required_files = set(CAMPAIGN.EXPORT_FILES)
    sizes = manifest.get("file_sizes", {})
    if not isinstance(sizes, dict) or set(sizes) != required_files or any(type(n) is not int or n < 0 for n in sizes.values()):
        errors.append("completed export file inventory is missing or differs from the exact required file set")
    blocks, dimension, samples, block_samples = (manifest.get(k) for k in ("block_count", "dimension", "sample_count", "block_samples"))
    if not all(type(value) is int and value > 0 for value in (blocks, dimension, samples, block_samples)) or dimension > 64:
        errors.append("manifest completed array/calendar dimensions are invalid")
    else:
        if blocks != (samples + block_samples - 1) // block_samples:
            errors.append("manifest block count differs from exact sampled calendar")
        shapes = {"gram": ([blocks, dimension, dimension], 16), "mean": ([blocks, dimension], 16),
                  "observation": ([blocks, dimension], 16), "intervals": ([blocks, 2], 8),
                  "valid_rows": ([blocks], 8), "signal_energy": ([blocks], 8), "received_energy": ([blocks], 8)}
        for field, (shape, width) in shapes.items():
            filename = manifest.get(field + "_file")
            expected_size = math.prod(shape) * width
            if manifest.get(field + "_shape") != shape or filename not in required_files or sizes.get(filename) != expected_size:
                errors.append(f"manifest completed {field} shape/byte inventory is inconsistent")
    if manifest.get("command_argv") != actual_child_command:
        errors.append("manifest child command differs from campaign receipt")
    return errors



SCORER_DEPENDENCIES = {"triad-noise-study.py", "triad-rbpf-study.py", "triad-cfo-batch-study.py",
    "triad_gaussian_window.py", "triad-static-frequency-study.py", "triad_gaussian_state.py",
    "triad_rbpf.py", "triad_noise_likelihood.py", "triad_state_workspace.py"}
ACTUAL_RECEIVER_FILES = {"manifest.json", "gram.c128", "observation.c128", "received_energy.f64",
                         "intervals.u64", "valid_rows.u64"}


def _valid_sha(value):
    return isinstance(value, str) and len(value) == 64 and all(c in "0123456789abcdef" for c in value)


def _valid_inventory(files):
    return isinstance(files, dict) and bool(files) and all(
        isinstance(name, str) and name and not Path(name).is_absolute()
        and ".." not in Path(name).parts and str(Path(name)) == name and _valid_sha(digest)
        for name, digest in files.items())


def _serialized_args(values):
    return json.loads(json.dumps({name: str(value) if isinstance(value, Path) else value for name, value in values.items()}, allow_nan=False))

def _actual_exports(export_plan: dict[str, Any], mapping: PathMap,
                    provenance: dict[str, Any] | None, verify_arrays: bool):
    expected_generation = CAMPAIGN.source_fingerprint()
    expected_paths: dict[str, dict[str, Any]] = {}
    per_cell_roots: dict[str, dict[int, set[tuple[int, ...]]]] = {}
    per_cell_root_counts: dict[str, Counter] = {}
    binary_hashes: set[str] = set()
    job_audits = []
    errors_by_path: dict[str, list[str]] = {}
    for job in export_plan["jobs"]:
        argv = job["argv"]
        args = _options(argv)
        require(args["split"] == "holdout", f"{job['name']}: export split is not holdout")
        output_original = str(args["output"])
        output = mapping.resolve(output_original)
        campaign_path = output / "campaign.json"
        doc = read_json(campaign_path) if campaign_path.is_file() else {}
        job_errors = []
        if not doc:
            job_errors.append("campaign receipt missing")
        else:
            if doc.get("format") != "triad-lowrate-campaign-v1":
                job_errors.append("campaign receipt schema mismatch")
            if doc.get("paths_requested") != job["path_count"] or \
                    doc.get("paths_succeeded") != job["path_count"] or \
                    doc.get("paths_attempted") != job["path_count"]:
                job_errors.append("campaign receipt does not show every planned path succeeded")
            if doc.get("control") != job["control"]:
                job_errors.append("campaign receipt control differs from plan")
            recorded_args = doc.get("arguments", {})
            if recorded_args != _serialized_args(args):
                job_errors.append("campaign complete argument/default map differs from frozen export command")
            digest = doc.get("binary_sha256")
            if not _valid_sha(digest):
                job_errors.append("campaign binary hash missing/invalid")
            else:
                binary_hashes.add(digest)
                if provenance.get("binary_sha256") != digest:
                    job_errors.append("campaign binary hash differs from supplied source provenance")
            source_inputs = doc.get("source_inputs", {})
            campaign_files = source_inputs.get("files", {})
            if not _valid_inventory(campaign_files):
                job_errors.append("campaign source inventory is empty or malformed")
            else:
                calculated = hashlib.sha256(json.dumps(campaign_files, sort_keys=True).encode()).hexdigest()
                if source_inputs.get("sha256") != calculated:
                    job_errors.append("campaign source inventory digest is missing or inconsistent")
                if campaign_files != expected_generation["files"]:
                    job_errors.append("campaign generation source inventory differs from exact current frozen generator/source set")
                if any(provenance["source_file_sha256"].get(name) != value for name, value in campaign_files.items()):
                    job_errors.append("campaign source files disagree with supplied bundle provenance")
            if doc.get("cancelled") is not False or doc.get("paths_not_started") != 0 or doc.get("paths_start_unknown") != 0 or doc.get("paths_dispatched") != job["path_count"]:
                job_errors.append("campaign cancellation/dispatch accounting is incomplete")
        records = doc.get("results", [])
        receipts = {r.get("path_index"): r for r in records if isinstance(r, dict)}
        wanted_indices = {value["index"] for value in job["simulation_only_expected"]}
        if len(receipts) != len(records) or set(receipts) != wanted_indices:
            job_errors.append("campaign results do not cover each planned path index exactly once")
        for expected in job["simulation_only_expected"]:
            index = expected["index"]
            source_original = f"{output_original}/path-{index:05d}"
            source = mapping.resolve(source_original)
            path_errors = list(job_errors)
            receipt = receipts.get(index)
            if receipt is None:
                path_errors.append("per-path campaign result missing")
                command, _ = _expected_child(args, index, Path(output_original) / f"path-{index:05d}")
            else:
                command = receipt.get("command")
                expected_command, child_truth = _expected_child(args, index, Path(output_original) / f"path-{index:05d}")
                if command != expected_command:
                    path_errors.append("campaign child argv differs from reconstructed frozen command")
                if any(receipt.get(k) != v for k, v in child_truth.items()):
                    path_errors.append("campaign child identity differs from frozen expected seed/bit/epoch")
                if receipt.get("returncode") != 0 or receipt.get("outcome") != "completed":
                    path_errors.append("campaign child did not complete successfully")
                if receipt.get("runner_started") is not True:
                    path_errors.append("bounded child runner start not confirmed")
                runner = receipt.get("runner_status", {})
                if not isinstance(runner, dict) or runner.get("outcome") != "completed" or runner.get("exit_code") != 0 or \
                               runner.get("termination", {}).get("group_empty") is not True:
                    path_errors.append("bounded runner receipt does not confirm clean zero exit")
            manifest_path = source / "manifest.json"
            manifest = None
            if manifest_path.is_file():
                try:
                    manifest = read_json(manifest_path)
                    if receipt is None or receipt.get("export_manifest") != manifest:
                        path_errors.append("campaign embedded export_manifest differs from actual retained manifest")
                    path_errors.extend(_manifest_checks(manifest, expected, args, command or []))
                    if manifest.get("format") != "triad-block-stats-v1":
                        path_errors.append("unsupported manifest format")
                    if verify_arrays:
                        for name, size in manifest.get("file_sizes", {}).items():
                            file = source / name
                            if not file.is_file() or file.stat().st_size != size:
                                path_errors.append(f"manifest file inventory mismatch: {name}")
                except Exception as exc:
                    path_errors.append(f"manifest parse/validation error: {type(exc).__name__}: {exc}")
            else:
                path_errors.append("manifest missing")
            path = {**expected, "cell": job["cell"], "source_original": source_original,
                    "source_local": str(source), "export_errors": path_errors,
                    "manifest_sha256": sha256(manifest_path) if manifest_path.is_file() else None,
                    "export_ok": not path_errors}
            if manifest is not None:
                roots = _seed_roots(manifest)
                if roots:
                    vector = tuple(roots[k] for k in ("key_seed", "channel_seed", "oscillator_seed",
                                                      "noise_seed", "frontend_seed", "path_seed"))
                    per_cell_roots.setdefault(job["cell"], {0: set(), 1: set()})[expected["source_bit"]].add(vector)
                    per_cell_root_counts.setdefault(job["cell"], Counter())[expected["source_bit"]] += 1
            errors_by_path[source_original] = path_errors
            expected_paths[source_original] = path
        job_audits.append({"name": job["name"], "cell": job["cell"], "planned_paths": job["path_count"],
                           "errors": job_errors, "campaign_sha256": sha256(campaign_path) if campaign_path.is_file() else None,
                           "campaign_binary_sha256": doc.get("binary_sha256") if doc else None,
                           "campaign_receipt_present": bool(doc), "status": "complete" if not job_errors else "incomplete"})
    cell_seed_audits = {}
    for cell, strata in per_cell_roots.items():
        cell_seed_audits[cell] = {
            "distinct_full_seed_vectors_by_source_bit": {str(bit): len(vectors) for bit, vectors in strata.items()},
            "manifest_vector_counts_by_source_bit": {str(bit): per_cell_root_counts[cell][bit] for bit in (0, 1)},
            "all_vectors_distinct_within_each_bit": all(len(strata[b]) == per_cell_root_counts[cell][b] for b in (0, 1)),
            "scope": "Distinctness of six recorded root values only; it is not proof of PRNG independence or physical-path independence."}
    return expected_paths, job_audits, cell_seed_audits, sorted(binary_hashes)


def _validate_plan_pair(export_plan: dict[str, Any], score_plan: dict[str, Any]) -> dict[str, Any]:
    export_jobs = {job["name"]: job for job in export_plan["jobs"]}
    score_jobs = {job["name"]: job for job in score_plan["jobs"]}
    require(len(export_jobs) == len(export_plan["jobs"]), "duplicate export job names")
    require(len(score_jobs) == len(score_plan["jobs"]), "duplicate score job names")
    require(set(export_jobs) == set(score_jobs), "export/score job names differ")
    expected_source_paths = set()
    expected_vectors: dict[str, dict[int, list[tuple[int, ...]]]] = {}
    for name, ejob in export_jobs.items():
        sjob = score_jobs[name]
        for field in ("cell", "control", "path_count", "start_index"):
            require(ejob.get(field) == sjob.get(field), f"{name}: export/score {field} differs")
        expected = ejob.get("simulation_only_expected")
        require(isinstance(expected, list) and len(expected) == ejob["path_count"], f"{name}: expected path metadata count mismatch")
        args = _options(ejob["argv"])
        out = str(args["output"])
        intended_inputs, score_options, _ = SCREEN.parse_job(sjob)
        wanted_inputs = [f"{out}/path-{i:05d}" for i in range(ejob["start_index"], ejob["start_index"] + ejob["path_count"])]
        require(intended_inputs == wanted_inputs, f"{name}: scorer input order/paths differ from export batch")
        receiver = score_plan["receiver"]
        cell = next(c for c in score_plan["cells"] if c["name"] == ejob["cell"])
        cell_options = {"seconds": "requested_active_seconds", "rate": "gui_rate_hz", "cn0": "sampled_cn0_db_hz",
            "sample_rate": "sample_rate_numerator", "sample_rate_denominator": "sample_rate_denominator",
            "block_seconds": "block_seconds", "spread_delay_ms": "excess_delay_ms",
            "spread_doppler_hz": "ray_center_separation_hz", "ray_gaussian_width_hz": "ray_gaussian_twice_rms_width_hz",
            "selective_perturbation": "selective_perturbation", "control": "control"}
        for option, field in cell_options.items():
            require(args[option] == cell[field], f"{name}: export option {option} differs from frozen cell")
        require(args["paths"] == ejob["path_count"] and args["start_index"] == ejob["start_index"] and args["control"] == ejob["control"] and args["split"] == "holdout", f"{name}: export batch identity differs from plan")
        fs = cell["sample_rate_hz"]
        width = cell["ray_gaussian_twice_rms_width_hz"]
        expected_score_options = {
            "--mode": [receiver["mode"]], "--methods": receiver["methods"],
            "--window-blocks": [str(receiver["window_blocks"])],
            "--window-doppler-width": [str(width)],
            "--coherence": [str(math.sqrt(2) / (math.pi * width))],
            "--frequency-bound": [str(receiver["frequency_bound_hz"])],
            "--device": [receiver["device"]], "--precision": [receiver["precision"]],
            "--threads": [str(score_plan["bounds"]["score_threads_per_batch"])],
            "--memory-gib": [str(score_plan["bounds"]["score_memory_gib_per_batch"])],
            "--selection-methods": [str(receiver["selection_methods"])],
            "--duration-alternatives": [str(receiver["duration_alternatives"])],
            "--attempt-spacing": [str(receiver["attempt_spacing_seconds"])],
            "--calibration-alternatives": [str(receiver["calibration_alternatives"])],
            "--horizon-seconds": [str(receiver["false_admission_horizon_seconds"])],
            "--false-probability": [str(receiver["false_admission_probability_target"])],
            "--correct-reception-target": [str(receiver["correct_reception_target"])],
        }
        for flag, values in expected_score_options.items():
            require(flag in score_options and SCREEN.REFERENCE.option_equal(score_options[flag], values),
                    f"{name}: scorer option differs from frozen receiver/cell contract: {flag}")
        require(fs > 0 and width > 0, f"{name}: invalid physical sample-rate or channel width")
        for offset, item in enumerate(expected):
            index = ejob["start_index"] + offset
            require(item.get("index") == index, f"{name}: expected indices are not exact contiguous plan range")
            require(item.get("source_bit") == index % 2, f"{name}: frozen source bit does not match generator identity")
            require(item.get("epoch") == 1_800_000_000 + 60 * index, f"{name}: frozen epoch does not match generator identity")
            require(item.get("control") == ejob["control"], f"{name}: frozen control mismatch")
            _, reconstructed = _expected_child(args, index, Path(out) / f"path-{index:05d}")
            require(item == {"index": index, **reconstructed}, f"{name}: frozen expected bit/epoch/control/seed roots differ from reconstructed generator")
            vector = tuple(item.get(k) for k in ("key_seed", "channel_seed", "oscillator_seed", "noise_seed", "frontend_seed", "path_seed"))
            require(all(type(value) is int for value in vector), f"{name}: missing frozen seed root")
            expected_vectors.setdefault(ejob["cell"], {0: [], 1: []})[item["source_bit"]].append(vector)
            source = wanted_inputs[offset]
            require(source not in expected_source_paths, f"duplicate planned physical path: {source}")
            expected_source_paths.add(source)
    seed_summary = {}
    for cell, strata in expected_vectors.items():
        distinct = {str(bit): len(set(vectors)) for bit, vectors in strata.items()}
        totals = {str(bit): len(vectors) for bit, vectors in strata.items()}
        seed_summary[cell] = {"planned_vector_counts_by_source_bit": totals,
                              "distinct_planned_vectors_by_source_bit": distinct,
                              "all_planned_vectors_distinct_within_bit": all(distinct[b] == totals[b] for b in ("0", "1"))}
    return {"job_sets_identical": True, "planned_path_count": len(expected_source_paths),
            "expected_seed_distinctness_by_cell": seed_summary}


def _score_inventory_audit(score_plan: dict[str, Any], mapping: PathMap,
                           verify_arrays: bool) -> tuple[list[dict[str, Any]], dict[str, list[str]]]:
    receipts = []
    errors_by_path: dict[str, list[str]] = {}
    for job in score_plan["jobs"]:
        inputs, _, output = SCREEN.parse_job(job)
        report_path = mapping.resolve(output)
        if not report_path.is_file():
            continue  # the independent screen analyzer reports the missing report
        try:
            report = read_json(report_path)
        except Exception:
            continue
        for record in report.get("paths", []):
            source = record.get("input")
            if source not in inputs or record.get("status") != "completed":
                continue
            issues = []
            hashes = record.get("input_hashes", record.get("sampled_input_hashes", {}))
            sampled = record.get("sampled_input_hashes", {})
            local = mapping.resolve(source)
            try:
                manifest = read_json(local / "manifest.json")
            except Exception as exc:
                issues.append(f"cannot read export manifest for scorer inventory: {exc}")
                manifest = {}
            inventory = manifest.get("file_sizes", {})
            if not isinstance(hashes, dict) or set(hashes) != ACTUAL_RECEIVER_FILES:
                issues.append("scorer actual input inventory differs from the exact required receiver file set")
                hashes = hashes if isinstance(hashes, dict) else {}
            if not isinstance(inventory, dict) or set(inventory) != set(CAMPAIGN.EXPORT_FILES):
                issues.append("export retained-file inventory differs from exact required completed set")
                inventory = inventory if isinstance(inventory, dict) else {}
            if isinstance(hashes, dict):
                for filename, digest in hashes.items():
                    if filename != "manifest.json" and filename not in inventory:
                        issues.append(f"scorer receipt not listed in exported retained-file inventory: {filename}")
                    if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
                        issues.append(f"invalid scorer SHA256 receipt: {filename}")
            if isinstance(sampled, dict):
                for filename, digest in sampled.items():
                    if filename in hashes and hashes[filename] != digest:
                        issues.append(f"sampled and actual scorer hash receipts disagree: {filename}")
                    if filename != "manifest.json" and filename not in inventory:
                        issues.append(f"sampled scorer receipt absent from exported retained-file inventory: {filename}")
            if verify_arrays:
                combined = dict(hashes)
                if isinstance(sampled, dict):
                    for filename, digest in sampled.items():
                        if filename not in combined:
                            combined[filename] = digest
                for filename, digest in combined.items():
                    file = local / filename
                    if not file.is_file() or (filename != "manifest.json" and file.stat().st_size != inventory.get(filename)) or sha256(file) != digest:
                        issues.append(f"scorer raw input bytes do not match receipt: {filename}")
            if issues:
                errors_by_path.setdefault(source, []).extend(issues)
            receipts.append({"input": source, "scorer_receipt_files": sorted(hashes),
                             "sampled_receipt_files": sorted(sampled) if isinstance(sampled, dict) else [],
                             "export_inventory_files": sorted(["manifest.json", *inventory.keys()]),
                             "receipt_inventory_consistent": not issues,
                             "raw_bytes_rehashed": verify_arrays and not issues,
                             "raw_verification_scope": "Actual receiver files only; mean/signal_energy remain simulator-only. Metadata mode does not verify omitted raw bytes."})
    return receipts, errors_by_path


def cell_counts(expected, scored, control: str):
    """Fixed-denominator strict/relaxed cell count; missing signal paths miss."""
    signal = control == "signal"
    result: dict[str, Any] = {"attempted": len(expected), "control": control,
                              "gates": {}, "per_bit": {"0": {}, "1": {}} if signal else None}
    for gate in ("strict", "relaxed"):
        counts = Counter(attempted=0, correct_accepted=0, wrong_accepted=0, missed=0,
                         false_admitted=0, not_admitted=0, unscored=0)
        by_bit = {0: Counter(trials=0, correct_accepted=0, wrong_accepted=0, missed=0),
                  1: Counter(trials=0, correct_accepted=0, wrong_accepted=0, missed=0)}
        for path in expected:
            counts["attempted"] += 1
            item = scored.get(path["index"])
            if item is None:
                counts["unscored"] += 1
                if signal:
                    bit = path["source_bit"]
                    counts["missed"] += 1
                    by_bit[bit]["trials"] += 1; by_bit[bit]["missed"] += 1
                else:
                    counts["false_admitted"] += 1  # worst-case for the unseen control
                continue
            outcome = item[gate]
            if signal:
                bit = path["source_bit"]
                by_bit[bit]["trials"] += 1
                if outcome["correct_accepted"]:
                    counts["correct_accepted"] += 1; by_bit[bit]["correct_accepted"] += 1
                elif outcome["wrong_accepted"]:
                    counts["wrong_accepted"] += 1; by_bit[bit]["wrong_accepted"] += 1
                else:
                    counts["missed"] += 1; by_bit[bit]["missed"] += 1
            elif outcome.get("false_admitted", 0) > 0:
                counts["false_admitted"] += 1
            else:
                counts["not_admitted"] += 1
        gate_result = dict(counts)
        if signal:
            per_bit = {}
            for bit, c in by_bit.items():
                n = c["trials"]; k = c["correct_accepted"]
                per_bit[str(bit)] = {**dict(c), "pointwise_one_sided_95_cp_lower": cp_lower(k, n, .05) if n else None}
            gate_result["per_bit"] = per_bit
            if all(by_bit[b]["trials"] > 0 for b in (0, 1)):
                n0, n1 = by_bit[0]["trials"], by_bit[1]["trials"]
                k0, k1 = by_bit[0]["correct_accepted"], by_bit[1]["correct_accepted"]
                if n0 == n1 and k0 == n0 and k1 == n1:
                    lower = .05 ** (1 / (n0 + n1))
                    method = "all-success equal-stratum AM-GM pointwise 95% bound"
                else:
                    lower = (cp_lower(k0, n0, .025) + cp_lower(k1, n1, .025)) / 2
                    method = "mean of per-bit one-sided 97.5% exact binomial lower bounds (Bonferroni pointwise 95%)"
                gate_result["balanced_mean_correct_probability_lower_95"] = lower
                gate_result["balanced_mean_bound_method"] = method
            else:
                gate_result["balanced_mean_correct_probability_lower_95"] = None
                gate_result["balanced_mean_bound_method"] = "unavailable: one or both bit-stratum denominators empty"
        result["gates"][gate] = gate_result
    if signal:
        result["bit_allocation"] = {str(bit): sum(p["source_bit"] == bit for p in expected) for bit in (0, 1)}
        result["bit_balanced_as_planned"] = result["bit_allocation"] == {"0": len(expected) // 2, "1": len(expected) // 2}
    result["scope"] = "Conditional only on this fixed simulated cell and its declared iid physical-path/bit-stratum assumptions. Bounds are pointwise and are not pooled across cells or a guarantee for real-world fading or continuous-operation false admissions."
    return result


def cp_lower(successes: int, trials: int, alpha: float) -> float:
    require(type(successes) is int and type(trials) is int and 0 <= successes <= trials,
            "invalid exact-binomial counts")
    if trials == 0 or successes == 0:
        return 0.0
    from scipy.stats import beta
    return float(beta.ppf(alpha, successes, trials - successes + 1))


def audit(export_plan_path: Path, score_plan_path: Path, root: Path, source_root: Path,
          output_path: Path | None = None, source_provenance_path: Path | None = None,
          verify_arrays: bool = False) -> dict[str, Any]:
    export_plan = read_json(export_plan_path); score_plan = read_json(score_plan_path)
    require(export_plan.get("schema") == score_plan.get("schema") == "triad-selective-actual-holdout-plan-v1",
            "unsupported export/score plan schema")
    require(export_plan.get("operation") == "actual sampled-waveform export" and
            score_plan.get("operation") == "actual sampled-noise receiver validation", "plan operation mismatch")
    require({k: v for k, v in export_plan.items() if k not in ("jobs", "operation")} ==
            {k: v for k, v in score_plan.items() if k not in ("jobs", "operation")},
            "export/score frozen common contract differs")
    require(len(export_plan["jobs"]) == len(score_plan["jobs"]), "export/score job count differs")
    planned_binding = _validate_plan_pair(export_plan, score_plan)
    frozen_driver_hash = export_plan.get("seed_generation_source", {}).get("sha256")
    current_driver_hash = sha256(CAMPAIGN_PATH)
    require(source_provenance_path is not None, "source provenance is required for a binding audit")
    provenance = read_json(source_provenance_path)
    require(provenance.get("schema") == "triad-selective-build-provenance-v1" and
            provenance.get("all_extracted_source_files_match_bundle") is True,
            "source provenance does not establish extracted source bundle integrity")
    require(_valid_sha(provenance.get("binary_sha256")), "source provenance binary SHA256 missing/invalid")
    require(_valid_sha(provenance.get("source_bundle_sha256")), "source provenance bundle SHA256 missing/invalid")
    provenance_files = provenance.get("source_file_sha256", {})
    require(_valid_inventory(provenance_files), "source provenance file inventory empty/malformed")
    required_source_files = {"tools/triad-conditional-sweep.py", "tools/triad-lowrate-campaign.py", "tools/triad-long.cpp", "tools/triad_bounded_run.py"} | {"tools/" + n for n in SCORER_DEPENDENCIES}
    require(required_source_files <= set(provenance_files), "source provenance misses required generation/receiver dependencies")
    for filename in {"triad-conditional-sweep.py", *SCORER_DEPENDENCIES}:
        require(provenance_files["tools/" + filename] == sha256(CAMPAIGN_PATH.with_name(filename)),
                f"receiver provenance differs from the current frozen parser/dependency: {filename}")
    require(current_driver_hash == frozen_driver_hash, "current seed generator differs from frozen plan")
    mapping = PathMap(root, source_root)
    expected_paths, export_jobs, seed_audits, binary_hashes = _actual_exports(export_plan, mapping, provenance, verify_arrays)
    screen_result = SCREEN.analyze(root, score_plan_path, source_root=source_root, verify_arrays=verify_arrays)
    score_inventory_receipts, score_inventory_errors = _score_inventory_audit(score_plan, mapping, verify_arrays)
    score_cases = {case.get("name"): case for case in screen_result["cases"]}
    require(set(score_cases) == {job["name"] for job in score_plan["jobs"]}, "scorer case set differs from frozen plan")
    job_by_name = {j["name"]: j for j in score_plan["jobs"]}
    scored_by_cell: dict[str, dict[int, dict[str, Any]]] = {}
    observed_by_cell: dict[str, dict[int, dict[str, Any]]] = {}
    score_jobs = []
    for name, case in score_cases.items():
        job = job_by_name[name]
        job_errors = []
        if case.get("status") != "completed":
            job_errors.append(f"screen analyzer status {case.get('status')}: {case.get('error', '')}")
        if case.get("planned_inputs") != SCREEN.parse_job(job)[0]:
            job_errors.append("screen analysis input binding differs from planned ordered paths")
        declared_source = provenance_files.get("tools/triad-conditional-sweep.py")
        if declared_source != case.get("source_sha256"):
            job_errors.append("scorer source SHA256 differs from supplied source provenance")
        dependencies = case.get("dependency_sha256", {})
        if not isinstance(dependencies, dict) or set(dependencies) != SCORER_DEPENDENCIES:
            job_errors.append("scorer dependency inventory differs from exact nonempty dependency set")
            dependencies = dependencies if isinstance(dependencies, dict) else {}
        for dependency, digest in dependencies.items():
            if provenance_files.get(f"tools/{dependency}") != digest:
                job_errors.append(f"scorer dependency SHA256 differs from supplied source provenance: {dependency}")
        try:
            # The real parser enforces fresh output files. Substitute only
            # the non-scientific output destination during reconstruction;
            # outputs are excluded from the configuration comparison below.
            parse_argv = list(job["argv"][2:])
            with tempfile.TemporaryDirectory(prefix="triad-audit-parser-") as temporary:
                parse_argv[parse_argv.index("--output") + 1] = str(Path(temporary) / "fresh-report.json")
                parsed = SCORER.parse_args(parse_argv)
            expected_config = {k: v for k, v in vars(parsed).items()
                if k not in ("inputs", "output", "budget", "tap_prior", "phase_prior")}
            expected_config["coherence"] = "infinity" if math.isinf(parsed.coherence) else parsed.coherence
            expected_config["tap_powers"] = list(parsed.tap_prior.stationary_power)
            if expected_config != case.get("configuration"):
                job_errors.append("complete receiver configuration/defaults differ from reconstructed frozen parser")
            _, _, original_report = SCREEN.parse_job(job)
            if read_json(mapping.resolve(original_report)).get("family") != parsed.budget.diagnostics():
                job_errors.append("receiver family/calibration accounting differs from frozen parser")
        except (OSError, ValueError, KeyError, TypeError, SystemExit) as error:
            job_errors.append(f"cannot bind complete receiver configuration: {error}")
        cell = job["cell"]
        for source in SCREEN.parse_job(job)[0]:
            job_errors.extend(f"{source}: {error}" for error in score_inventory_errors.get(source, []))
        scored = scored_by_cell.setdefault(cell, {})
        observed = observed_by_cell.setdefault(cell, {})
        pending = {}
        for group in case.get("groups", []):
            if group.get("control") != job["control"]:
                continue
            for row in group.get("physical_path_clusters", []):
                index = row.get("truth_bit")  # overwritten from frozen index map below
                source = row.get("input")
                expected = expected_paths.get(source)
                if expected is None:
                    job_errors.append(f"scored path absent from export plan: {source}"); continue
                path_index = expected["index"]
                if path_index in observed or path_index in pending:
                    job_errors.append(f"duplicate scored path index: {path_index}"); continue
                pending[path_index] = {"strict": row["strict"], "relaxed": row["relaxed"]}
        observed.update(pending)
        if not job_errors:
            scored.update({index: values for index, values in pending.items()
                if next(p for p in expected_paths.values() if p["cell"] == cell and p["index"] == index)["export_ok"]})
        score_jobs.append({"name": name, "cell": cell, "status": "complete" if not job_errors else "incomplete",
                           "report_sha256": case.get("report_sha256"), "scores_sha256": case.get("scores_sha256"),
                           "planned_paths": job["path_count"], "scored_paths": sum(1 for i in range(job["start_index"], job["start_index"] + job["path_count"]) if i in scored),
                           "errors": job_errors})
        for path in SCREEN.parse_job(job)[0]:
            if path in expected_paths and job_errors:
                expected_paths[path].setdefault("score_errors", []).extend(job_errors)
    cells_out = []
    for cell in export_plan["cells"]:
        name = cell["name"]
        expected = [value for value in expected_paths.values() if value.get("cell") == name]
        expected.sort(key=lambda x: x["index"])
        scored = scored_by_cell.get(name, {})
        control = cell["control"]
        counts = cell_counts(expected, scored, control)
        export_bad = [p for p in expected if not p["export_ok"]]
        roots = seed_audits.get(name, {})
        cell_export_jobs = [j for j in export_jobs if j["cell"] == name]
        cell_score_jobs = [j for j in score_jobs if j["cell"] == name]
        complete_score = len(scored) == len(expected) and all(not j["errors"] for j in cell_score_jobs)
        complete_export = not export_bad and len(expected) == cell["path_count"] and all(not j["errors"] for j in cell_export_jobs) and roots.get("all_vectors_distinct_within_each_bit") is True
        observed_counts = cell_counts(expected, observed_by_cell.get(name, {}), control)
        for gate in ("strict", "relaxed"):
            for key in ("balanced_mean_correct_probability_lower_95", "balanced_mean_bound_method"):
                observed_counts["gates"][gate].pop(key, None)
            for per_bit in observed_counts["gates"][gate].get("per_bit", {}).values():
                per_bit.pop("pointwise_one_sided_95_cp_lower", None)
        counts["observed_scoring_diagnostic_counts"] = observed_counts
        if not (complete_score and complete_export):
            for gate in ("strict", "relaxed"):
                counts["gates"][gate]["balanced_mean_correct_probability_lower_95"] = None
                counts["gates"][gate]["balanced_mean_bound_method"] = "unavailable: incomplete scientific generation/source/score binding"
                for per_bit in counts["gates"][gate].get("per_bit", {}).values():
                    per_bit["pointwise_one_sided_95_cp_lower"] = None
        counts["integrity"] = {"planned_paths": cell["path_count"], "expected_manifest_paths": len(expected),
                                "export_failures_or_binding_errors": len(export_bad),
                                "scored_paths": len(scored), "export_complete": complete_export,
                                "score_complete": complete_score,
                                "export_job_errors": [j for j in export_jobs if j["cell"] == name and j["errors"]],
                                "score_job_errors": [j for j in score_jobs if j["cell"] == name and j["errors"]],
                                "seed_root_audit": roots}
        counts["scientific_status"] = "complete-cell-counts" if complete_score and complete_export else "incomplete; missing paths counted as misses / worst-case false admissions"
        cells_out.append({"cell": cell, **counts})
    result = {
        "schema": "triad-selective-holdout-cell-audit-v1",
        "scientific_qualification": False,
        "integrity_complete": all(c["integrity"]["export_complete"] and c["integrity"]["score_complete"] for c in cells_out),
        "plans": {"export": {"path": str(export_plan_path), "sha256": sha256(export_plan_path)},
                  "score": {"path": str(score_plan_path), "sha256": sha256(score_plan_path)},
                  "screen_analyzer_sha256": sha256(SCREEN_PATH), "campaign_driver_sha256": current_driver_hash,
                  "frozen_seed_generator_sha256": frozen_driver_hash,
                  "seed_generator_hash_matches_frozen_plan": current_driver_hash == frozen_driver_hash,
                  "export_score_pairing": planned_binding},
        "evidence": {"root": str(root.resolve()), "source_root": str(source_root),
                     "binary_hashes_seen": binary_hashes,
                     "source_provenance": ({"path": str(source_provenance_path), "sha256": sha256(source_provenance_path),
                                            "binary_sha256": provenance.get("binary_sha256"),
                                            "source_bundle_sha256": provenance.get("source_bundle_sha256")}
                                           if provenance else None),
                     "verify_raw_arrays": verify_arrays,
                     "screen_analysis_integrity": screen_result["summary"],
                     "screen_analysis_score_plan_sha256": screen_result["score_plan"]["sha256"],
                     "scorer_manifest_inventory_receipts": score_inventory_receipts,
                     "raw_score_input_bytes_rehashed": verify_arrays},
        "export_jobs": export_jobs, "score_jobs": score_jobs, "cells": cells_out,
        "scope": "Every cell is reported separately. Exact one-sided binomial bounds apply only to the declared fixed simulated cell under independent physical-path outcomes within each source-bit stratum. No pooling across C/N0, perturbation, rate or control cells; no field-performance or continuous-operation false-admission qualification. Missing signal paths are counted as misses; missing control paths are counted as worst-case false admissions and prevent complete status.",
    }
    if output_path:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with output_path.open("x") as stream:
            json.dump(result, stream, allow_nan=False, sort_keys=True, indent=2)
            stream.write("\n")
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True, help="local evidence root corresponding to --source-root")
    parser.add_argument("--source-root", type=Path, required=True, help="absolute root prefix used in frozen plan paths")
    parser.add_argument("--export-plan", type=Path, required=True)
    parser.add_argument("--score-plan", type=Path, required=True)
    parser.add_argument("--source-provenance", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--verify-arrays", action="store_true", help="rehash every scorer-receipted retained input array")
    args = parser.parse_args(argv)
    result = audit(args.export_plan, args.score_plan, args.root, args.source_root,
                   args.output, args.source_provenance, args.verify_arrays)
    complete = result["integrity_complete"]
    print(json.dumps({"cells": len(result["cells"]), "complete": complete,
                      "binary_hashes": result["evidence"]["binary_hashes_seen"]}))
    return 0 if complete else 1


if __name__ == "__main__":
    raise SystemExit(main())
