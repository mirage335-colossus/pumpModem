#!/usr/bin/env python3
"""Generate deterministic development captures for the isolated FP4 study.

This invokes the shared sampled triad channel. It does not run a receiver or
claim that its seeded captures establish operational sensitivity/admission.
The optional AES mask comparison is only a preparation-time control; input
hashes are compared for equality and the observation/template export timing is
not an isolated AES benchmark.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import subprocess
import time


ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / "local-evidence/triad-fp4-20261101/inputs"
BINARY = ROOT / "build/dev/triad_long"
MASK = "~0x200000000000000:~0x20000000000:~0x0:~0x0:~0x0"
BASE = ["--mode", "export-sampled", "--rate", "3.6", "--added", "10",
        "--sample-rate", "64", "--threads", "2", "--chunk", "4096",
        "--phase-diffusion", ".00872665", "--white-fm", ".002",
        "--flicker-fm", ".002", "--white-pm", ".01"]


def sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def scenarios():
    rows = []
    index = 0
    for cn0 in (-14, -12, -10, -8, -6):
        for realization in range(4):
            rows.append((f"boundary-cn{cn0:+03d}-r{realization}", 256, cn0,
                         "none", "flat", [], realization % 2))
    rows.append(("sanity-high-cn10", 256, 10, "none", "flat", [], 0))
    for index_hour, (cn0, channel, basis) in enumerate((
            (-24, "none", "flat"), (-20, "none", "flat"),
            (-24, "hf", "two-ray"), (-20, "hf", "two-ray"))):
        rows.append((f"hour-{channel}-cn{cn0:+03d}-r{index_hour}", 3600, cn0,
                     channel, basis, [], index_hour % 2))
    frontend = [
        ("adc-dac16-nominal", ["--adc-bits", "16", "--dac-bits", "16",
                               "--adc-full-scale", "512", "--dac-full-scale", "4"]),
        ("adc-headroom-overload", ["--adc-bits", "16", "--adc-full-scale", "16"]),
        ("strong-tone", ["--interference-tone", "50", "5"]),
        ("impulsive", ["--interference-impulse", "100", ".2"]),
    ]
    for i, (name, options) in enumerate(frontend):
        rows.append(("frontend-"+name, 256, -10, "none", "flat", options, i % 2))
    # At low chip rates, the protocol's fixed hop guards consume every 400-ms
    # dwell. This higher rate/sample-rate pair gives the shared v3 source
    # protected cells in each hop while keeping the FHSS path physically valid.
    fhss = ["--fhss", "--burst-wait-tiles", "1", "--rate", "60",
            "--sample-rate", "640"]
    rows.append(("fhss-flat-burstwait", 256, -10, "none", "flat", fhss, 0))
    rows.append(("fhss-hf-tworay-burstwait", 256, -10, "hf", "two-ray", fhss, 1))
    return rows


def command(name, seconds, cn0, channel, basis, extra, bit, seed_base, out):
    seed = seed_base + 3
    return [str(BINARY), *BASE, "--seconds", str(seconds), "--cn0", str(cn0),
            "--epoch", "1800000000",
            "--channel", channel, "--basis", basis,
            "--epoch-offsets", "0,3600", "--seed", str(seed),
            "--key-seed", str(seed_base+1), "--channel-seed", str(seed_base+2),
            "--signal-bit", str(bit), "--output", str(out), *extra]


def run_case(name, seconds, cn0, channel, basis, extra, bit, seed_base,
             binary_hash, source_hash, started, env_override=None):
    out = OUT / name
    if out.exists():
        raise FileExistsError(f"refusing to overwrite existing capture: {out}")
    result_path = OUT / (name+".run.json")
    if result_path.exists():
        previous = json.loads(result_path.read_text())
        if previous.get("returncode"):
            result_path.rename(OUT / (name+".prior-failure.run.json"))
        else:
            raise FileExistsError(f"refusing to overwrite previous successful run: {result_path}")
    argv = command(name, seconds, cn0, channel, basis, extra, bit, seed_base, out)
    env = os.environ.copy()
    if env_override is None:
        env.pop("OPENSSL_ia32cap", None)
    else:
        env["OPENSSL_ia32cap"] = env_override
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    wall_start = time.perf_counter()
    result = subprocess.run(argv, cwd=ROOT, env=env, capture_output=True, text=True,
                            timeout=60, check=False)
    wall = time.perf_counter()-wall_start
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    record = dict(name=name, argv=argv, seed=seed_base+3, key_seed=seed_base+1,
                  channel_seed=seed_base+2, noise_seed=seed_base+2, truth_bit=bit,
                  source_sha256=source_hash, binary_sha256=binary_hash,
                  openssl_ia32cap=("unset/default dispatch" if env_override is None else env_override),
                  returncode=result.returncode, wall_seconds=wall,
                  child_user_cpu_seconds=after.ru_utime-before.ru_utime,
                  child_system_cpu_seconds=after.ru_stime-before.ru_stime,
                  started_from_campaign_monotonic_seconds=wall_start-started)
    (OUT / (name+".stdout.txt")).write_text(result.stdout)
    (OUT / (name+".stderr.txt")).write_text(result.stderr)
    if result.returncode:
        record["failure"] = result.stderr[-4000:]
    else:
        manifest_path = out / "manifest.json"
        manifest = json.loads(manifest_path.read_text())
        record.update(manifest=manifest_path.relative_to(ROOT).as_posix(),
                      manifest_sha256=sha(manifest_path),
                      exported_simulation_wall_seconds=manifest.get("sampled_observation_wall_seconds"),
                      template_generation_and_write_wall_seconds=manifest.get("basis_generation_and_write_wall_seconds"),
                      exported_file_hashes={name: sha(out/name) for name in
                                            ("basis.c128", "observation.c128", "validity.u8")})
    (OUT / (name+".run.json")).write_text(json.dumps(record, indent=2)+"\n")
    if result.returncode:
        raise subprocess.CalledProcessError(result.returncode, argv, result.stdout, result.stderr)
    if time.perf_counter()-started > 900:
        raise TimeoutError("generation stage exceeded 900-second bound")
    return record


def main():
    global BINARY, OUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--split", choices=("development", "holdout"), default="development")
    parser.add_argument("--binary", type=Path, default=BINARY)
    parser.add_argument("--output-root", type=Path, default=OUT)
    args = parser.parse_args()
    BINARY, OUT = args.binary.resolve(), args.output_root.resolve()
    if args.split == "holdout":
        raise SystemExit("holdout generation is intentionally disabled until the receiver policy is frozen")
    if not BINARY.is_file() or sha(ROOT/"tools/triad-long.cpp") != "99a268643103cac3e5fb351f1179dc8a0fa3a22dc00c909461b57db1e2315ccc":
        raise SystemExit("triad_long binary or reviewed exporter source hash differs")
    OUT.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    source_hash, binary_hash = sha(ROOT/"tools/triad-long.cpp"), sha(BINARY)
    results = []
    base_seed = 50000
    for index, (name, seconds, cn0, channel, basis, extra, bit) in enumerate(scenarios()):
        run_path = OUT/(name+".run.json")
        capture_path = OUT/name
        if run_path.exists() and capture_path.exists() and (capture_path/"manifest.json").exists():
            old = json.loads(run_path.read_text())
            if old.get("returncode") == 0 and old.get("source_sha256") == source_hash and old.get("binary_sha256") == binary_hash:
                results.append(old)
                continue
        results.append(run_case(name, seconds, cn0, channel, basis, extra, bit,
                                base_seed+index*100, binary_hash, source_hash, started))
    # AES-NI/VAES capability masking is a process-level OpenSSL dispatch control.
    # Same key/channel/noise inputs produce byte-identical sampled outputs.
    aes_rows = []
    for pair in range(4):
        pair_rows = []
        for index, label in enumerate(("default", "masked", "masked", "default")):
            name = f"aes-abba-pair{pair}-{index}-{label}"
            row = run_case(name, 256, -10, "none", "flat", [], pair % 2,
                           90000+pair*100, binary_hash, source_hash, started,
                           None if label == "default" else MASK)
            aes_rows.append(row)
            pair_rows.append(row)
        fingerprints = [row["exported_file_hashes"] for row in pair_rows]
        if any(row != fingerprints[0] for row in fingerprints[1:]):
            raise RuntimeError(f"AES dispatch pair {pair} changed deterministic sampled exports")
    elapsed = time.perf_counter()-started
    summary = dict(format="triad-fp4-inputs-v1", split=args.split,
                   source_sha256=source_hash, binary_sha256=binary_hash,
                   scenario_count=len(results), aes_mask_process_count=len(aes_rows),
                   aes_abba_pair_count=4, aes_input_hashes_equal=True, aes_measurement_limit=
                   "whole sampled export timings, not isolated AES; dispatch capability mask only",
                   wall_seconds=elapsed, cases=results, aes_mask_cases=aes_rows)
    path = OUT/"development-summary.json"
    if path.exists():
        raise FileExistsError(path)
    path.write_text(json.dumps(summary, indent=2)+"\n")
    print(json.dumps(dict(summary=path.as_posix(), cases=len(results),
                          aes_abba_pairs=4, wall_seconds=elapsed), indent=2))


if __name__ == "__main__":
    main()
