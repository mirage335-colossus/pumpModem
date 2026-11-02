#!/usr/bin/env python3
"""Freeze paired legacy/corrected selective-channel campaigns; launch nothing.

This planner binds source bits, epochs, six seed domains, commands and a common
full-capture tail floor. It deliberately marks the millihertz-spaced dispersive
bank as a spacing-scaled synthetic stress law. Its memory is not a millisecond
propagation delay. See the accompanying plan README and selective-correction
study for physical interpretation and prior-model limitations.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import subprocess
import sys


HERE = Path(__file__).resolve().parent
ROOT_DEFAULT = HERE.parents[4]
CAMPAIGN_REL = Path("tools/triad-lowrate-campaign.py")
SCORER_REL = Path("tools/triad-conditional-sweep.py")
METHODS = ("static-fft-ucb", "gaussian-window-ucb")


def load_campaign(root: Path):
    path = root / CAMPAIGN_REL
    spec = importlib.util.spec_from_file_location("triad_lowrate_campaign", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import campaign helpers: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module, path


def profiles():
    # Rates and geometries match the earlier selective/spread studies. The
    # fast-AM arm changes ray separation (hence both AM rates) while holding
    # Gaussian width fixed: it is a coupled intervention.
    return (
        dict(name="g001", seconds=86400, rate=.036, fs=1, denominator=4,
             block=64, delay_ms=.005, doppler=.001, width=.001,
             cn0=(-26, -27)),
        dict(name="g01", seconds=86400, rate=.36, fs=2, denominator=1,
             block=8, delay_ms=.05, doppler=.005, width=.01,
             cn0=(-22, -23)),
        dict(name="g1", seconds=8640, rate=3.6, fs=16, denominator=1,
             block=1, delay_ms=.5, doppler=.1, width=.1,
             cn0=(-10, -11)),
        dict(name="g001-fastam", seconds=86400, rate=.036, fs=1, denominator=4,
             block=64, delay_ms=.005, doppler=.01, width=.001,
             cn0=(-26, -27)),
        dict(name="high10", seconds=120, rate=360, fs=2048, denominator=1,
             block=.015625, delay_ms=6, doppler=10, width=10,
             cn0=(6, 8)),
        dict(name="high30", seconds=120, rate=360, fs=2048, denominator=1,
             block=.015625, delay_ms=7, doppler=30, width=30,
             cn0=(8, 10)),
        dict(name="stable850-m60", seconds=850*86400, rate=.0036, fs=1, denominator=4,
             block=8192, delay_ms=5, doppler=.05, width=0,
             cn0=(-60,)),
    )


def bandwidth(rate: float) -> tuple[float, float]:
    fine = rate / 6
    return 1.25*fine, 3.75*fine


def tail_floor_seconds(profile) -> float:
    """Conservative common tail for old IIR and new 2-delay cubic FIR.

    The RX clock in these GPSDO cases is bounded by 0.5 ppm. Match the public
    planner's conservative support bounds plus one additional sample, then
    divide by the maximum RX sample rate. Thus the maximum-clock observation
    calendar has strictly identical rows for both models; the runtime actual
    tail may be shorter but the explicit floor preserves the rest.
    """
    spacing, _ = bandwidth(profile["rate"])
    fs = profile["fs"]/profile["denominator"] * (1 + .5e-6)
    dispersive_support = math.ceil(2*fs/(3*spacing))+2
    # The legacy planner uses decay_samples(Fsmax, BW)+1. Recover its rounded
    # integer bound from the same formula rather than relying on the seconds
    # helper's rounded value.
    _, occupied = bandwidth(profile["rate"])
    cutoff = min(occupied/6, fs*.24)
    k = math.tan(math.pi*cutoff/fs)
    pole = abs((k-1)/(k+1))
    decay = math.ceil(math.log(1e-6)/math.log(pole)) if pole else 0
    legacy_support = decay+1
    conservative_samples = max(legacy_support, dispersive_support)+1
    return conservative_samples/fs


def parser_args(campaign, root, binary, profile, model, modulation, stage, index,
                paths, cn0, split, control="signal", seconds_override=None):
    source = root / "local-evidence/triad-selective-correction" / stage / profile["name"] / model / modulation / control / f"path-{index:05d}"
    argv = [
        "--binary", str(binary), "--output", str(source),
        "--paths", str(paths), "--start-index", str(index),
        "--split", split, "--control", control,
        "--seconds", str(seconds_override if seconds_override is not None else profile["seconds"]),
        "--rate", str(profile["rate"]), "--sample-rate", str(profile["fs"]),
        "--sample-rate-denominator", str(profile["denominator"]),
        "--cn0", str(cn0), "--rf-hz", "8270",
        "--spread-preset", "sub9-best",
        "--spread-delay-ms", str(profile["delay_ms"]),
        "--spread-doppler-hz", str(profile["doppler"]),
        "--block-seconds", str(profile["block"]),
        "--tap-delay-ms", str(profile["delay_ms"]),
        "--tap-doppler-hz", str(profile["doppler"]),
        "--ray-gaussian-width-hz", str(profile["width"]),
        "--selective-perturbation", modulation,
        "--start", "0", "0", "1", "--epoch-offsets", "0",
    ]
    args = campaign.parser().parse_args(argv)
    args.binary = binary
    args.output = source
    cmd, truth = campaign.make_command(args, index, source)
    cmd += ["--selective-model", model,
            "--selective-scale-hz", "0",
            "--selective-center-hz", "0",
            "--selective-tail-floor-seconds", format(tail_floor_seconds(profile), ".17g")]
    return source, cmd, truth


def scoring_job(root, python, profile, sources, out, actual_noisy=False, timeout=1800):
    methods = ["static-fft-ucb"]
    if profile["width"] > 0:
        methods.append("gaussian-window-ucb")
    argv = [python, str(root / SCORER_REL), *(str(path) for path in sources),
            "--output", str(out), "--mode", "actual-noisy" if actual_noisy else "conditional",
            "--methods", *methods,
            "--frequency-bound", "1e-6",
            "--selection-methods", "16", "--duration-alternatives", "8",
            "--attempt-spacing", "60", "--calibration-alternatives", "2",
            "--horizon-seconds", "86400", "--false-probability", "1e-4",
            "--correct-reception-target", ".99",
            "--device", "cuda:0", "--precision", "fp32",
            "--threads", "1", "--memory-gib", "8", "--seed", "231207"]
    if actual_noisy:
        argv += ["--attempt-spacing", "60", "--calibration-alternatives", "2",
                 "--horizon-seconds", "86400", "--false-probability", "1e-4",
                 "--correct-reception-target", ".99"]
    else:
        argv += ["--cn0-grid=" + ",".join(map(str, profile["cn0"])),
                 "--noise-trials", "16", "--trial-batch", "4",
                 "--controls", "signal", "null"]
    if profile["width"] > 0:
        argv += ["--window-doppler-width", str(profile["width"]), "--window-blocks", "64"]
    return {"name": out.stem, "timeout_seconds": timeout, "argv": argv}


def resolve_source_revision(root: Path, explicit: str | None) -> tuple[str, str]:
    if explicit is not None:
        if not re.fullmatch(r"(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})", explicit):
            raise ValueError("--source-revision must be a full 40- or 64-digit hexadecimal revision")
        return explicit.lower(), "explicit-argument"
    try:
        revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root, text=True,
                                  check=True, capture_output=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as exc:
        raise ValueError("no Git revision is available; provide --source-revision with the archived source revision") from exc
    if not re.fullmatch(r"(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})", revision):
        raise ValueError("Git returned an invalid full source revision")
    return revision.lower(), "git-rev-parse"


def build_plan(root, python, binary, stage, paths, start_index, split, include_long,
               control="signal", cn0_override=None, explicit_revision=None):
    campaign, campaign_path = load_campaign(root)
    selected = [p for p in profiles() if include_long or not p["name"].startswith("stable850")]
    # Short sample-smoke uses two paths. The full development plan uses four.
    if stage == "smoke":
        paths = 2
        selected = [profiles()[0]]
        seconds_override = 16
    elif stage in ("development", "long-development", "long-holdout", "long-replay", "holdout"):
        seconds_override = None
    else:
        raise ValueError("unsupported stage")
    if stage in ("long-development", "long-holdout", "long-replay"):
        selected = [profiles()[-1]]
    if control != "signal" and stage not in ("holdout", "long-holdout", "long-replay"):
        raise ValueError("null/wrong-key sampled controls require an actual-noisy holdout stage")
    if stage == "holdout":
        split = "holdout"
    if stage == "long-holdout":
        split = "holdout"
    if stage == "long-replay":
        split = "holdout"
        start_index = 80000
    modes = ("each-25", "each-65")
    models = ("legacy-allpass", "dispersive-dft")
    jobs, scores, cells = [], [], []
    for profile in selected:
        sample_cn0 = profile["cn0"][0] if cn0_override is None else cn0_override
        active_modes = modes
        if stage == "smoke":
            active_modes = ("each-25", "each-65")
        for model in models:
            per_mode_sources = {}
            for mode in active_modes:
                source_paths, truths = [], []
                for offset in range(paths):
                    index = start_index + offset
                    source, cmd, truth = parser_args(
                        campaign, root, binary, profile, model, mode, stage, index,
                        paths, sample_cn0, split, control=control,
                        seconds_override=seconds_override)
                    source_paths.append(source)
                    truths.append({"index": index, **truth})
                    # Modulation mode is independent of channel model identity.
                    job_name = f"{profile['name']}-{model}-{mode}-p{index:05d}"
                    jobs.append({"name": job_name, "timeout_seconds": 1800, "argv": cmd,
                                 "simulation_only_expected": truths[-1]})
                per_mode_sources[mode] = source_paths
                scores.append(scoring_job(
                    root, python, profile, source_paths,
                    root / f"local-evidence/triad-selective-correction/{stage}/scores/{profile['name']}-cn0{sample_cn0:g}-{control}-{model}-{mode}.json",
                    actual_noisy=(stage in ("holdout", "long-holdout", "long-replay")), timeout=1200))
                cells.append({
                    "profile": profile["name"], "selective_model": model,
                    "modulation": mode, "paths": paths,
                    "balanced_bits": {str(b): sum(t["source_bit"] == b for t in truths) for b in (0, 1)},
                    "indices": [start_index, start_index+paths-1],
                    "seed_domains": list(campaign.SEED_DOMAINS),
                    "expected": truths,
                    "sources": [str(p) for p in source_paths],
                    "capture": {"requested_active_seconds": seconds_override or profile["seconds"],
                                "common_tail_floor_seconds": tail_floor_seconds(profile),
                                "carrier_spacing_hz": bandwidth(profile["rate"])[0],
                                "selective_scale_hz": bandwidth(profile["rate"])[0],
                                "ray_separation_hz": profile["doppler"],
                                "excess_delay_ms": profile["delay_ms"],
                                "cn0_db_hz": sample_cn0},
                    "limits": ["Paired model comparison uses same profile, index, model modulation, sample clock, source bit, epoch and channel/noise/key seed roots.",
                               "Conditional draws test a likelihood under the fixed sampled path; they are not independent physical paths.",
                               "A signal capture is reused for signal/null conditional draws; null represents noise-only with configured model variance, not an empirical null tail.",
                               "Corrected millihertz selectivity is an explicit synthetic stress law with 2/(3*spacing) seconds of dispersive memory; selected ray delay remains independent."],
                })
        if stage == "smoke":
            continue
        # Zero added modulation is exactly model-independent; one paired Off
        # capture per profile is sufficient to audit the bypass and controls.
        model = "legacy-allpass"
        mode = "off"
        off_sources = []
        for offset in range(paths):
            index = start_index+offset
            source, cmd, truth = parser_args(campaign, root, binary, profile,
                                              model, mode, stage, index, paths, sample_cn0,
                                              split, control=control,
                                              seconds_override=seconds_override)
            job_name=f"{profile['name']}-off-p{index:05d}"
            jobs.append({"name":job_name,"timeout_seconds":1800,"argv":cmd,
                         "simulation_only_expected":{"index":index,**truth}})
            off_sources.append(source)
        scores.append(scoring_job(
            root, python, profile, off_sources,
            root / f"local-evidence/triad-selective-correction/{stage}/scores/{profile['name']}-cn0{sample_cn0:g}-{control}-off.json",
            actual_noisy=(stage in ("holdout", "long-holdout", "long-replay")), timeout=1200))
        cells.append({"profile":profile["name"],"selective_model":"shared-off-bypass",
                      "modulation":"off","paths":paths,"sources":[str(p) for p in off_sources],
                      "limits":["Off is exported once because the selected additive stage must be an exact model-independent bypass."]})
    source_files = [
        root / "tools/triad-long.cpp", root / "tools/triad-lowrate-campaign.py",
        root / "tools/triad-conditional-sweep.py", root / "tools/triad_bounded_run.py",
        root / "src/channel_perturbation.hpp", root / "src/triad_simulation_stream.cpp",
        root / "include/datapump/triad_modem_v3.hpp", Path(__file__).resolve(),
    ]
    source_hashes = {str(path.relative_to(root)): campaign.sha256_file(path)
                     for path in source_files}
    source_bundle_hash = hashlib.sha256(json.dumps(source_hashes, sort_keys=True).encode()).hexdigest()
    revision, revision_origin = resolve_source_revision(root, explicit_revision)
    metadata = {
        "schema": "triad-selective-model-paired-plan-v1",
        "stage": stage,
        "status": "frozen command plan; no exports or scoring jobs have been run",
        "source": {"revision": revision, "revision_origin": revision_origin,
                   "source_file_sha256": source_hashes,
                   "source_bundle_sha256": source_bundle_hash,
                   "campaign_helper_sha256": campaign.sha256_file(campaign_path),
                   "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest() if binary.exists() else None},
        "scope": "Paired legacy-allpass-v1 versus dispersive-dft-v1, Each25/Each65, plus exact Off bypass.",
        "counts": {"sampled_exports": len(jobs), "scoring_jobs": len(scores), "physical_model_cells": len(cells)},
        "selection": {"paths_per_cell": paths, "start_index": start_index, "split": split,
                      "control": control,
                      "sample_cn0_db_hz": "profile first value" if cn0_override is None else cn0_override,
                      "pathwise_pairing": "same deterministic seed roots and full capture calendar within a model/depth pair"},
        "planner_targets": {"strict": {"correct_probability": .99, "noise_any_admission_per_day": 1e-4},
                            "relaxed": {"correct_probability": .9, "noise_any_admission_per_12_hours": .01},
                            "search_family": {"receivers": 16, "durations": 8, "calibrations": 2,
                                              "attempt_spacing_seconds": 60}},
        "source_defaults": {"encrypted": True, "rf_hz": 8270, "base_oscillator": "gpsdo-ocxo",
                            "shift_oscillator": "gpsdo-ocxo", "clock_sync": "GPS_10ms-1ms_region-0ms_offset",
                            "audio_error_ms": 70, "margin": 1, "added_symbols": 1,
                            "fhss": "off", "symbol_burst": "off",
                            "known_public_timing_approximation": [0,0,1],
                            "epoch_offsets": [0]},
        "receiver_limits": ["Existing receiver statistics project a declared two-ray basis; the new high-delay dispersive modulation is an intentional model mismatch until a receiver includes its sidebands/long memory.",
                            "Conditional results do not measure receiver-independent physical path variation or calibrated false-admission tails.",
                            "No ordinary Robust receiver integration is in scope."],
        "profiles": selected,
        "cells": cells,
        "jobs": jobs,
        "scoring_jobs": scores,
    }
    return metadata


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root",type=Path,default=ROOT_DEFAULT)
    p.add_argument("--python",default=sys.executable)
    p.add_argument("--binary",type=Path,required=True)
    p.add_argument("--source-revision",default=None,
                   help="full source Git revision for portable archives without a .git directory")
    p.add_argument("--output",type=Path,required=True)
    p.add_argument("--stage",choices=("smoke","development","long-development","long-holdout","long-replay","holdout"),default="development")
    p.add_argument("--paths",type=int,default=None)
    p.add_argument("--start-index",type=int,default=None)
    p.add_argument("--split",choices=("development","holdout"),default="development")
    p.add_argument("--control",choices=("signal","null","wrong-key"),default="signal")
    p.add_argument("--cn0",type=float,default=None,
                   help="override a profile's first C/N0 for sampled/actual-noisy runs")
    p.add_argument("--include-long",action="store_true")
    a=p.parse_args()
    root=a.root.resolve();binary=a.binary.expanduser().resolve()
    if not binary.is_file():
        raise SystemExit(f"binary does not exist: {binary}")
    if a.paths is None:
        a.paths = 2 if a.stage == "smoke" else (64 if a.stage in ("holdout", "long-holdout") and a.control == "signal" else (32 if a.stage in ("holdout", "long-holdout") else 4))
    if a.start_index is None:
        a.start_index = 80000 if a.stage == "long-replay" else (130000 if a.stage in ("holdout", "long-holdout") else 120000)
    if a.stage in ("holdout", "long-holdout", "long-replay"):
        a.split = "holdout"
    if a.paths<2 or a.paths%2: raise SystemExit("--paths must be an even number >=2")
    plan=build_plan(root,a.python,binary,a.stage,a.paths,a.start_index,a.split,a.include_long,
                    a.control,a.cn0,a.source_revision)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    if a.output.exists(): raise SystemExit(f"refusing to overwrite: {a.output}")
    a.output.write_text(json.dumps(plan,indent=2,allow_nan=False)+"\n")
    print(json.dumps({"output":str(a.output),"counts":plan["counts"],"source":plan["source"]},indent=2))


if __name__=="__main__":
    main()

