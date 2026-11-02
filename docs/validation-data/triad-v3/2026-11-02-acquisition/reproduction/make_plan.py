#!/usr/bin/env python3
"""Freeze the grouped-acquisition campaign; never launch scientific jobs.

Each output plan uses the existing bounded run_jobs.py interface (jobs containing
name, argv and timeout_seconds). Export and scoring plans are separate so the
caller can finish exports before scoring their actual shared observations.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[5]
SEED_DOMAINS = ("key", "channel", "oscillator", "noise", "frontend", "path")
NAMESPACE = "triad-acquisition-independent-v1"
# The preset resolves its nominal .0905-second bound one ULP beyond the literal.
# Expand each public endpoint by an explicit 1e-12 s, preserving 109 positions.
START_GRID = (-.090500000001, .090500000001, .181000000002 / 108)
SOURCE_FILES = (
    "tools/triad-long.cpp", "tools/triad_group_acquire.py", "tools/triad_gaussian_window.py",
    "tools/triad_noise_likelihood.py", "tools/triad_state_workspace.py", "tools/triad_bounded_run.py",
    "src/triad_simulation_stream.cpp", "src/channel_perturbation.hpp", "src/triad_gaussian_fading.hpp",
    "src/triad_modem_v3.cpp", "include/datapump/triad_modem_v3.hpp",
)
ORACLE_DEFAULT = REPOSITORY / "local-evidence/triad-selective-correction/plans/gpu-boundary/oracle-timing-final/oracle-timing-plan.json"
PILOT_DEFAULT = REPOSITORY / "local-evidence/triad-acquisition-improvement/pilot/export-plan.json"


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def key_hash(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                     allow_nan=False).encode()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def options(argv):
    result = {}
    current = None
    for value in argv[1:]:
        if value.startswith("--"):
            require(value not in result, f"duplicate argument {value}")
            current = value
            result[value] = []
        else:
            require(current is not None, "value before option")
            result[current].append(value)
    return result


def set_option(argv, name, *values):
    if name in argv:
        first = argv.index(name)
        stop = first + 1
        while stop < len(argv) and not argv[stop].startswith("--"):
            stop += 1
        argv[first:stop] = [name, *map(str, values)]
    else:
        argv.extend([name, *map(str, values)])


def independent_identity(index, control):
    roots = {}
    for domain in SEED_DOMAINS:
        encoded = json.dumps([NAMESPACE, index, domain], separators=(",", ":")).encode()
        roots[domain + "_seed"] = int.from_bytes(hashlib.sha256(encoded).digest()[:8], "little")
    return {"index": index, "source_bit": index % 2, "epoch": 1_900_000_000 + 60 * index,
            "control": control, **roots}


def paired_identities(oracle):
    result = {}
    references = {}
    for profile in ("high10", "high30"):
        for index in range(120000, 120004):
            name = f"{profile}-dispersive-dft-each-65-p{index}"
            selected = [j for j in oracle["reference_jobs"] if j["name"] == name]
            require(len(selected) == 1, f"missing or duplicate parent reference {name}")
            job = selected[0]
            expected = job["simulation_only_expected"]
            opt = options(job["argv"])
            require(expected["index"] == index and expected["source_bit"] == index % 2,
                    f"parent index/bit mismatch: {name}")
            require(expected["control"] == "signal", f"parent control mismatch: {name}")
            for domain in SEED_DOMAINS:
                require(int(opt["--" + domain + "-seed"][0]) == expected[domain + "_seed"],
                        f"parent seed mismatch: {name}")
            require(int(opt["--epoch"][0]) == expected["epoch"], f"parent epoch mismatch: {name}")
            require(float(opt["--spread-doppler-hz"][0]) == int(profile[4:]),
                    f"parent physical profile mismatch: {name}")
            result[profile, index] = copy.deepcopy(expected)
            references[profile, index] = {"job_name": name, "job_sha256": key_hash(job)}
    return result, references


def export_argv(template, binary, output, profile, depth, cn0, block_samples, identity):
    argv = list(template)
    argv[0] = str(binary)
    width = int(profile[4:])
    delay = 6 if profile == "high10" else 7
    settings = {
        "--mode": ["export-group-stats"], "--seconds": [120], "--rate": [360],
        "--added": [1], "--sample-rate": [2048], "--sample-rate-denominator": [1],
        "--selective-model": ["dispersive-dft"], "--selective-perturbation": [depth],
        "--selective-scale-hz": [0], "--selective-center-hz": [0],
        "--cn0": [cn0], "--block-seconds": [block_samples / 2048],
        "--tap-delay-ms": [delay], "--tap-doppler-hz": [width],
        "--spread-delay-ms": [delay], "--spread-doppler-hz": [width],
        "--ray-gaussian-width-hz": [width], "--signal-bit": [identity["source_bit"]],
        "--epoch": [identity["epoch"]], "--control": [identity["control"]],
        "--output": [output], "--start": START_GRID, "--epoch-offsets": [0],
        "--memory-mib": [128], "--export-max-mib": [2048], "--export-max-samples": [300000],
    }
    for domain in SEED_DOMAINS:
        settings["--" + domain + "-seed"] = [identity[domain + "_seed"]]
    for flag, values in settings.items():
        set_option(argv, flag, *values)
    require("--stats-noisy" in argv, "actual observed statistics must be exported")
    return argv


def score_argv(root, python, source, output, width, reblock, group_tile):
    return [python, str(root / "tools/triad_group_acquire.py"), str(source),
            "--mode", "actual-noisy", "--doppler-width-hz", str(width),
            "--window-seconds", ".25", "--reblock", str(reblock),
            "--reference-variance", "1", "--tap-powers", ".5", ".5",
            "--group-tile", str(group_tile), "--projection-tile-blocks", "256",
            "--memory-mib", "1024", "--device", "cpu", "--precision", "fp32", "--threads", "1",
            "--attempts-per-day", "1440", "--duration-alternatives", "8",
            "--selection-methods", "16", "--calibration-segments", "2",
            "--evaluate-simulation", "--output", str(output),
            "--scores-output", str(output.with_suffix(".scores.npz"))]


def make_plan(root, binary, python, oracle_path, pilot_path):
    oracle = json.loads(oracle_path.read_text())
    pilot = json.loads(pilot_path.read_text())
    paired, references = paired_identities(oracle)
    template = pilot["argv"]
    pilot_options = options(template)
    required_defaults = {"--rf-hz": ["8270"], "--base-oscillator": ["gpsdo-ocxo"],
                         "--shift-oscillator": ["gpsdo-ocxo"], "--margin": ["1"],
                         "--clock-sync": ["GPS_10ms-1ms_region-0ms_offset"],
                         "--audio-error-ms": ["70"], "--basis": ["two-ray"]}
    for flag, values in required_defaults.items():
        require(pilot_options.get(flag) == values, f"pilot default changed: {flag}")
    require(pilot_options["--selective-model"] == ["dispersive-dft"], "pilot model is not corrected DFT")
    source_hashes = {name: sha256(REPOSITORY / name) for name in SOURCE_FILES}
    source_key = key_hash(source_hashes)
    input_artifacts = {"oracle_plan": {"path": str(oracle_path.resolve()), "sha256": sha256(oracle_path)},
                       "pilot_plan": {"path": str(pilot_path.resolve()), "sha256": sha256(pilot_path)}}
    exports = []
    scores = []
    captures = []

    def add_capture(kind, profile, depth, cn0, block_samples, identity, reblocks, optional=False):
        index = identity["index"]
        name = f"{kind}-{profile}-{depth}-cn{cn0}-p{index}-b{block_samples}-{identity['control']}"
        output = root / "local-evidence/triad-acquisition-improvement/campaign/workspaces" / name
        argv = export_argv(template, binary, output, profile, depth, cn0, block_samples, identity)
        scientific_options = options(argv)
        scientific_options.pop("--output")
        scientific_options.pop("--mode")
        input_key = {"scientific_options": scientific_options, "source_file_inventory_sha256": source_key}
        capture_key = key_hash(input_key)
        origin = references[profile, index] if kind.startswith("paired") else {
            "seed_namespace": NAMESPACE, "index": index,
            "seed_algorithm": "first8 SHA256 bytes, little-endian uint64, compact JSON [namespace,index,domain]"}
        binding = {"capture_id": name, "capture_input_key_sha256": capture_key,
                   "source_file_inventory_sha256": source_key, "simulation_only_expected": copy.deepcopy(identity),
                   "seed_origin": origin, "parent_artifact_sha256": {k: v["sha256"] for k, v in input_artifacts.items()}}
        common = {"timeout_seconds": 600, "optional_refinement": optional, "input_binding": binding}
        exports.append({"name": name, "argv": argv, **common})
        score_names = []
        for reblock in reblocks:
            score_name = f"{name}-r{reblock}"
            report = root / "local-evidence/triad-acquisition-improvement/campaign/scores" / f"{score_name}.json"
            scores.append({"name": score_name,
                           "argv": score_argv(root, python, output, report, int(profile[4:]), reblock,
                                              1 if optional else 4),
                           "depends_on_export": name, "reblock": reblock, **copy.deepcopy(common)})
            score_names.append(score_name)
        captures.append({"name": name, "kind": kind, "profile": profile, "depth": depth,
                         "cn0_db_hz": cn0, "control": identity["control"], "block_samples": block_samples,
                         "block_seconds": block_samples / 2048, "score_jobs": score_names,
                         "optional_refinement": optional, "input_key": input_key, "input_binding": binding})

    for profile, depths, cn0s in (("high30", ("each-25", "each-65"), (8, 20)),
                                  ("high10", ("each-65",), (6, 12))):
        for depth in depths:
            for cn0 in cn0s:
                for index in range(120000, 120004):
                    add_capture("paired", profile, depth, cn0, 16, paired[profile, index], (1, 2))
    for profile, cn0, first in (("high10", 8, 200000), ("high30", 12, 210000)):
        for index in range(first, first + 64):
            add_capture("independent", profile, "each-65", cn0, 16,
                        independent_identity(index, "signal"), (1,))
    for control, first in (("null", 220000), ("wrong-key", 230000)):
        for index in range(first, first + 8):
            add_capture("control", "high30", "each-65", 20, 16,
                        independent_identity(index, control), (1,))
    for index in range(120000, 120004):
        add_capture("paired-fine", "high30", "each-65", 8, 8, paired["high30", index], (1, 2), True)

    configure_jobs = []
    representative_names = [
        "paired-high10-each-65-cn6-p120000-b16-signal",
        "paired-fine-high30-each-65-cn8-p120000-b8-signal",
        "control-high30-each-65-cn20-p220000-b16-null",
        "control-high30-each-65-cn20-p230000-b16-wrong-key",
    ]
    for name in representative_names:
        original = next(job for job in exports if job["name"] == name)
        command = list(original["argv"])
        set_option(command, "--mode", "configure")
        configure_jobs.append({"name": "configure-" + name, "argv": command, "timeout_seconds": 30,
                               "input_binding": copy.deepcopy(original["input_binding"]),
                               "scientific_samples_generated": False})
    require(len(exports) == 172 and len(scores) == 200, "campaign count mismatch")
    require(len({job["name"] for job in exports}) == 172 and len({job["name"] for job in scores}) == 200,
            "duplicate campaign names")
    independent = [c for c in captures if c["kind"] in ("independent", "control")]
    vectors = {tuple(c["input_binding"]["simulation_only_expected"][d + "_seed"] for d in SEED_DOMAINS)
               for c in independent}
    paired_vectors = {tuple(v[d + "_seed"] for d in SEED_DOMAINS) for v in paired.values()}
    require(len(vectors) == 144 and not vectors & paired_vectors, "independent seed collision")
    return {
        "schema": "triad-grouped-acquisition-frozen-plan-v1",
        "stage": "fixed actual sampled-waveform campaign; execution and validation pending",
        "root": str(root), "binary": str(binary), "python": python,
        "input_artifacts": input_artifacts, "source_file_sha256": source_hashes,
        "source_file_inventory_sha256": source_key,
        "source_binding_scope": "Fingerprints listed generator/receiver/channel dependencies. Complete source bundle and built-binary provenance must be bound by the execution owner after code freezes; these hashes do not certify unlisted build inputs.",
        "counts": {"paired_primary_exports": 24, "independent_signal_exports": 128,
                   "independent_control_exports": 16, "optional_fine_exports": 4,
                   "exports_including_optional": 172, "score_jobs_including_optional": 200,
                   "configure_jobs": 4, "distinct_paired_seed_vectors": 8,
                   "distinct_new_seed_vectors": 144, "timing_groups_per_capture": 109},
        "source_defaults": {"requested_seconds": 120, "gui_rate_hz": 360, "sample_rate_hz": 2048,
                            "added_symbols": 1, "rf_hz": 8270, "base_oscillator": "gpsdo-ocxo",
                            "shift_oscillator": "gpsdo-ocxo", "clock_sync": "GPS_10ms-1ms_region-0ms_offset",
                            "audio_error_ms": 70, "margin": 1, "fhss": "off", "symbol_burst": "off",
                            "selective_model": "dispersive-dft", "selective_scale_hz": 0,
                            "selective_center_hz": 0, "selective_tail_floor_seconds": float(pilot_options["--selective-tail-floor-seconds"][0]),
                            "public_timing_grid": list(START_GRID), "epoch_offsets": [0],
                            "public_timing_rounding_correction_seconds_per_edge": 1e-12,
                            "primary_block_samples": 16, "encryption": True},
        "receiver": {"method": "group-local-gaussian-window-ucb", "fixed_group_count": 109,
                     "window_seconds": .25, "device": "cpu", "precision": "fp32", "threads": 1,
                     "group_tile": "4 for primary blocks/reblock2; 1 for optional block8 refinement",
                     "calibration": "complete local column union for every fixed group; union calibration allocation across groups; all groups retained in normalized bit mixture",
                     "strict": {"any_false_probability": 1e-4, "horizon_seconds": 86400, "attempts": 1440},
                     "relaxed": {"any_false_probability": .01, "horizon_seconds": 43200, "attempts": 720},
                     "duration_alternatives": 8, "selection_methods": 16, "calibration_segments": 2,
                     "known_noise_variance": None, "simulation_truth": "evaluation appended after receiver decisions only"},
        "limits": {"export_timeout_seconds": 600, "score_timeout_seconds": 600,
                   "export_max_samples": 300000, "export_max_mib_per_capture": 2048,
                   "export_memory_mib": 128, "score_memory_mib": 1024,
                   "estimated_total_export_gib": 110,
                   "estimate_scope": "Planning estimate only; actual captured rows/file sizes and peak resource use must be measured."},
        "retention_policy": {"all_captures": "Keep manifests, complete array SHA256 inventories, seed/command bindings, job receipts and every JSON/NPZ score output.",
                             "full_workspace_downloads": "Execution owner selects four representative complete workspaces for retrieval. Other raw arrays may be deleted after hash inventories are verified; reproducibility is not a claim that those arrays were retained.",
                             "generated_file_hashes": "Not knowable before execution. Hash all completed workspace files before removal and bind manifests/reports to the frozen capture input keys."},
        "interpretation_limits": [
            "The paired observations reuse the exact six roots and epochs of the matching high10 or high30 parent reference jobs. Across C/N0 the noise roots are paired; native AWGN amplitude changes with C/N0. Paired variants are not independent physical paths.",
            "Independent roots use SHA256 of compact JSON [triad-acquisition-independent-v1,index,domain] only, with the first eight bytes interpreted little-endian. C/N0, profile and control do not enter this derivation; disjoint index ranges establish distinct root vectors.",
            "Independent high10 indices 200000..200063 and high30 indices 210000..210063 each balance 32 source-bit cases per stratum. Null 220000..220007 and wrong-key 230000..230007 each balance 4 per stratum and use distinct roots. Their epoch is 1900000000+60*index.",
            "The full 109 timing groups cover the declared +/-90.5ms initial-arrival interval. Grid loss is not a certified continuous-timing bound, and other synchronization dimensions are not thereby exhaustively searched.",
            "The initial pilot used literal +/-0.0905-second endpoints, one ULP inside the preset's resolved prior, and native coverage reported false. This plan expands each edge by 1e-12 seconds and recomputes 108 equal intervals; native configure must report timing-prior coverage true. The original endpoint assertion failure is retained separately.",
            "A workspace is scored from its same actual observed correlations for each reblock choice. No inter-group Gram exists, so independently resampling group noise is unsupported and prohibited.",
            "Corrected dispersive-dft selection is explicit. Its auto scale is a spacing-scaled selective stress law, separate from Gaussian Doppler B and ray separation D; high10 uses B=D=10 Hz, delay 6 ms and high30 uses B=D=30 Hz, delay 7 ms.",
            "Each25/65 are amplitude excursions without per-realization energy normalization. In particular Each65 may reverse band polarity; nominal injected C/N0 and realized post-channel energy must remain distinct.",
            "The block8 refinement is explicitly optional but included in default counts. All outcomes, exclusions, timeouts and missing workspaces must remain visible; omit its jobs only with a recorded scope decision.",
            "64 signal trials per operating point and 8 per control cannot establish 99% reliability or empirically validate the operational false-admission tail. Fixed-stratum intervals and configured probability assumptions must be reported separately.",
        ],
        "captures": captures, "jobs": exports, "scoring_jobs": scores, "configure_jobs": configure_jobs,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("/workspace/triad"))
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--python", default="/workspace/triad-env/bin/python")
    parser.add_argument("--oracle-plan", type=Path, default=ORACLE_DEFAULT)
    parser.add_argument("--pilot-plan", type=Path, default=PILOT_DEFAULT)
    parser.add_argument("--output-dir", type=Path,
                        default=REPOSITORY / "local-evidence/triad-acquisition-improvement/campaign-plan")
    parser.add_argument("--configure-only", action="store_true", help="emit only the four cheap configure jobs")
    args = parser.parse_args()
    root = args.root.resolve()
    binary = (args.binary or root / "build/dev-cli/triad_long").resolve()
    plan = make_plan(root, binary, args.python, args.oracle_plan.resolve(), args.pilot_plan.resolve())
    args.output_dir.mkdir(parents=True, exist_ok=True)
    common = {k: v for k, v in plan.items() if k not in ("jobs", "scoring_jobs", "configure_jobs")}
    outputs = {"configure-plan.json": {**common, "operation": "configure-only", "jobs": plan["configure_jobs"]}}
    if not args.configure_only:
        outputs.update({"campaign-plan.json": plan,
                        "export-plan.json": {**common, "operation": "actual grouped export", "jobs": plan["jobs"]},
                        "score-plan.json": {**common, "operation": "actual grouped scoring", "jobs": plan["scoring_jobs"]}})
    for name, value in outputs.items():
        (args.output_dir / name).write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


if __name__ == "__main__":
    main()
