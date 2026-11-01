#!/usr/bin/env python3
"""Generate a small, fresh-root sampled corpus for consumer tensor screening.

This prepares physical inputs before GPU selection. --plan performs source
preflight and emits cases, roots, and exact exporter commands without creating
captures. Existing output paths are never replaced; resume verifies identity
and every exported-file digest.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import platform
import random
import resource
import subprocess
import time


SOURCE_HASHES = {
    "tools/triad-long.cpp": "c07003b3234543508a3ec1730ea6246902828bd6a1e3b6d7a4f2b37356a10566",
    "include/datapump/triad_modem_v3.hpp": "e625915b0c0d1f29925d6a95e3b49d89efe44c8482eb13a9de6836de4d6e9c92",
    "src/triad_simulation_stream.cpp": "b270448dc776ee6594c121e2b10a2f4f6eba5fa966897fc1f3a7fc06af501450",
    "build/dev/triad_long": "89b4af004e4f5fc564acdcbd6b7c3849afc5803b503ca45c43ed343b7af6c591",
}
ROOT_SEED = 4_729_019
STAGE_LIMIT_SECONDS = 900
CHILD_LIMIT_SECONDS = 120
EXPORT_OPTIONS = ["--mode", "export-sampled", "--rate", "3.6", "--added", "10",
                  "--sample-rate", "64", "--threads", "2", "--chunk", "4096",
                  "--block-seconds", "4", "--phase-diffusion", ".00872665",
                  "--white-fm", ".002", "--flicker-fm", ".002", "--white-pm", ".01",
                  "--epoch-offsets", "0,3600", "--epoch", "1800000000",
                  "--seconds", "256", "--channel", "none", "--basis", "flat"]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify(source_root: Path, binary: Path) -> dict[str, str]:
    actual = {}
    for relative, expected in SOURCE_HASHES.items():
        path = source_root / relative
        if not path.is_file():
            raise FileNotFoundError(path)
        value = sha256(path)
        if value != expected:
            raise ValueError(f"frozen source mismatch {relative}: expected {expected}, got {value}")
        actual[relative] = value
    if not binary.is_file():
        raise FileNotFoundError(binary)
    value = sha256(binary)
    if value != SOURCE_HASHES["build/dev/triad_long"]:
        raise ValueError(f"frozen exporter mismatch: expected {SOURCE_HASHES['build/dev/triad_long']}, got {value}")
    actual["exporter"] = value
    return actual


def cases() -> list[dict]:
    items = []
    for cn0 in (-12, -11, -10, -9):
        for ordinal in range(16):
            items.append(dict(name=f"boundary-cn{cn0:+03d}-r{ordinal:03d}",
                              stratum="signal-boundary", control="signal", cn0_db_hz=cn0,
                              signal_bit=ordinal % 2))
    for ordinal in range(32):
        items.append(dict(name=f"target-cn-08-r{ordinal:03d}", stratum="target",
                          control="signal", cn0_db_hz=-8, signal_bit=ordinal % 2))
    for ordinal in range(32):
        items.append(dict(name=f"null-cn-10-r{ordinal:03d}", stratum="noise-only",
                          control="null", cn0_db_hz=-10, signal_bit=ordinal % 2))

    rng = random.Random(ROOT_SEED)
    seen = set()
    for index, item in enumerate(items):
        roots = {}
        for domain in ("key", "channel", "oscillator", "noise", "frontend"):
            root = rng.getrandbits(64)
            if root in seen:
                raise AssertionError("root collision")
            seen.add(root)
            roots[domain] = root
        item.update(index=index, roots=roots, sample_rate=64, duration_seconds=256,
                    channel="none", basis="flat", epoch_seconds=1_800_000_000,
                    epoch_offsets_seconds=[0, 3600], block_seconds=4,
                    inner_rate_hz=3.6, added_symbols=10,
                    frequencies_hz=[-.01, 0., .01], lags_samples=[0, 1, 2],
                    search_candidates=4, hypotheses=36, phase_states=512)
    assert len(items) == 128 and len(seen) == 640
    return items


def exporter_argv(binary: Path, destination: Path, case: dict) -> list[str]:
    roots = case["roots"]
    return [str(binary), *EXPORT_OPTIONS, "--cn0", str(case["cn0_db_hz"]),
            "--control", case["control"], "--signal-bit", str(case["signal_bit"]),
            "--seed", str(roots["channel"]), "--key-seed", str(roots["key"]),
            "--channel-seed", str(roots["channel"]),
            "--oscillator-seed", str(roots["oscillator"]),
            "--noise-seed", str(roots["noise"]), "--frontend-seed", str(roots["frontend"]),
            "--output", str(destination)]


def file_hashes(folder: Path) -> dict[str, str]:
    return {name: sha256(folder/name) for name in
            ("manifest.json", "basis.c128", "observation.c128", "validity.u8")}


def validate(manifest: dict, case: dict) -> None:
    expected = {"format": "triad-sampled-waveforms-v1",
                "sample_rate": 64, "cn0_db_hz": case["cn0_db_hz"],
                "channel": "none", "control": case["control"],
                "epoch": case["epoch_seconds"],
                "signal_bit_simulation_truth": case["signal_bit"],
                "signal_enabled": case["control"] == "signal",
                "candidate_count": 4, "block_samples": 256,
                "requested_active_seconds": 256}
    for key, value in expected.items():
        if manifest.get(key) != value:
            raise ValueError(f"manifest {key} does not match declared input {case['name']}")
    roots = manifest.get("random_seed_roots", {})
    if manifest.get("key_seed") != case["roots"]["key"]:
        raise ValueError("manifest key root differs")
    for domain in ("channel", "oscillator", "noise", "frontend"):
        if roots.get(domain) != case["roots"][domain]:
            raise ValueError(f"manifest {domain} root differs")
    expected_variance = 64/10**(case["cn0_db_hz"]/10)
    if not math.isclose(float(manifest.get("noise_variance_pre_frontend", -1)),
                        expected_variance, rel_tol=1e-14):
        raise ValueError("exported noise level differs from requested C/N0")
    candidates = manifest.get("candidates", [])
    if len(candidates) != 4 or {(c.get("bit"), c.get("epoch_offset_seconds")) for c in candidates} != {
            (bit, epoch) for bit in (0, 1) for epoch in (0, 3600)}:
        raise ValueError("sampled public bank is missing a bit/epoch candidate")
    shape = manifest.get("basis_shape")
    samples = manifest.get("sample_count")
    taps = manifest.get("candidates", [{}])[0].get("tap_count")
    if shape is None or len(shape) != 3 or shape[0] != 4 or shape[2] != taps:
        raise ValueError("basis dimensions are absent/inconsistent")
    if (case["name"].startswith("null") and
            manifest.get("signal_enabled") is not False):
        raise ValueError("noise control unexpectedly contains a transmitted signal")
    if samples <= 0 or float(manifest.get("actual_elapsed_seconds", 0)) < 256:
        raise ValueError("sampled exporter did not record physical samples/duration")


def run_case(binary, source_root, out, case, sources, started):
    target = out / case["name"]
    record_path = out / (case["name"] + ".run.json")
    argv = exporter_argv(binary, target, case)
    if time.perf_counter()-started > STAGE_LIMIT_SECONDS:
        raise TimeoutError("generation exceeded 900-second stage budget")
    if target.exists() or record_path.exists():
        raise FileExistsError(f"refusing to overwrite prior/partial case: {target}")
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    tick = time.perf_counter()
    timeout = False
    try:
        remaining = STAGE_LIMIT_SECONDS - (time.perf_counter()-started)
        child = subprocess.run(argv, cwd=source_root, capture_output=True, text=True,
                              timeout=min(CHILD_LIMIT_SECONDS, remaining), check=False)
        stdout, stderr, code = child.stdout, child.stderr, child.returncode
    except subprocess.TimeoutExpired as exc:
        timeout = True
        stdout = exc.stdout.decode(errors="replace") if isinstance(exc.stdout, bytes) else (exc.stdout or "")
        stderr = exc.stderr.decode(errors="replace") if isinstance(exc.stderr, bytes) else (exc.stderr or "")
        code = 124
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    row = dict(format="consumer-gpu-input-v1", status="failed" if code else "complete",
               case=case, argv=argv, source_hashes=sources,
               wall_seconds=time.perf_counter()-tick, returncode=code, timeout=timeout,
               child_user_cpu_seconds=after.ru_utime-before.ru_utime,
               child_system_cpu_seconds=after.ru_stime-before.ru_stime,
               stdout_tail=stdout[-2000:], stderr_tail=stderr[-4000:])
    if code == 0:
        manifest = json.loads((target/"manifest.json").read_text())
        validate(manifest, case)
        hashes = file_hashes(target)
        row["input_file_hashes"] = hashes
        row["exporter_manifest"] = manifest
    else:
        (out/(case["name"]+".stdout.txt")).write_text(stdout)
        (out/(case["name"]+".stderr.txt")).write_text(stderr)
    record_path.write_text(json.dumps(row, indent=2)+"\n")
    if code:
        raise subprocess.CalledProcessError(code, argv, stdout, stderr)
    return row


def resume_case(out, case, sources):
    target, record_path = out/case["name"], out/(case["name"]+".run.json")
    if not target.is_dir() or not record_path.is_file():
        raise FileExistsError(f"--resume requires a complete prior record for {target}")
    row = json.loads(record_path.read_text())
    if row.get("status") != "complete" or row.get("case") != case or row.get("source_hashes") != sources:
        raise ValueError(f"resume identity mismatch for {target}")
    if file_hashes(target) != row.get("input_file_hashes"):
        raise ValueError(f"resume payload hash mismatch for {target}")
    validate(json.loads((target/"manifest.json").read_text()), case)
    return row


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--plan", action="store_true")
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args(argv)
    root, binary, out = args.source_root.resolve(), args.binary.resolve(), args.output.resolve()
    hashes, plan = verify(root, binary), cases()
    if args.plan:
        print(json.dumps(dict(schema="consumer-gpu-input-plan-v1", seed_generator=
                              f"Python random.Random({ROOT_SEED}).getrandbits(64)",
                              root_count_per_domain=len(plan), unique_roots=640,
                              group_counts={name: sum(row["stratum"] == name for row in plan)
                                            for name in ("signal-boundary", "target", "noise-only")},
                              source_hashes=hashes, cases=plan), indent=2))
        return 0
    out.mkdir(parents=True, exist_ok=True)
    started, records = time.perf_counter(), []
    index = out/"index.json"
    if index.exists() and not args.resume:
        raise FileExistsError(f"refusing to overwrite {index}")
    def save(status):
        index.write_text(json.dumps(dict(schema="consumer-gpu-input-index-v1", status=status,
            source_hashes=hashes, generator_seed=ROOT_SEED, case_count=len(plan),
            completed=sum(r["status"] == "complete" for r in records),
            failed=sum(r["status"] != "complete" for r in records),
            holdout_or_calibration=False,
            statistical_note="128 fresh screening inputs: 64 boundary, 32 target, 32 null; not enough noise captures to establish either operational false-admission tail.",
            wall_seconds=time.perf_counter()-started, cases=records), indent=2)+"\n")
    save("running")
    for case in plan:
        row = resume_case(out, case, hashes) if args.resume and (out/case["name"]).exists() else \
              run_case(binary, root, out, case, hashes, started)
        records.append(row)
        save("running")
        print(json.dumps(dict(case=case["name"], status=row["status"],
                              wall_seconds=row.get("wall_seconds"))), flush=True)
    save("complete")
    print(json.dumps(dict(index=str(index), completed=len(records),
                          elapsed_seconds=time.perf_counter()-started), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
