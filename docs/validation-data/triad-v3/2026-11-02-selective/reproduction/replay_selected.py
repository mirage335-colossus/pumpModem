#!/usr/bin/env python3
"""Replay selected retained actual-noisy captures with CPU FP64.

This verifies the six diagnostic workspaces against both their saved diagnostic
hashes and the original scorer receipts, then reruns five selected captures.
Only the path list, output filename, device, and precision differ from the
frozen score-plan commands. No sampled waveform or channel is regenerated.

Run from the repository root with:
  OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1 \
  /tmp/triad-weak-env/bin/python docs/validation-data/triad-v3/2026-11-02-selective/reproduction/replay_selected.py
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

import numpy as np


REPO = Path(__file__).resolve().parents[5]
REPRO = Path(__file__).resolve().parent
EVIDENCE = REPO / "local-evidence/triad-selective/remote-final"
OUTPUT = REPO / "local-evidence/triad-selective/replay"
STRICT = 22.72106338958574
RELAXED = 17.422746023037703
HASH_FILES = ("manifest.json", "gram.c128", "observation.c128",
              "received_energy.f64", "intervals.u64", "valid_rows.u64")


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def read_json(path: Path):
    return json.loads(path.read_text())


def need(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def fresh_output_dir() -> Path:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    existing = [int(p.name[4:]) for p in OUTPUT.glob("run-[0-9][0-9][0-9]") if p.is_dir()]
    run = max(existing, default=0) + 1
    target = OUTPUT / f"run-{run:03d}"
    target.mkdir()
    return target


def original_prefix(stage: str, cell: str, batch: int, path_id: int) -> str:
    return f"/workspace/triad/local-evidence/selective/{stage}/{cell}/batch{batch:02d}/path-{path_id}"


def find_scored_path(stage: str, cell: str, batch: int, path_id: int):
    plan = read_json(REPRO / f"{stage}-score-plan.json")
    remote = original_prefix(stage, cell, batch, path_id)
    job = next((j for j in plan["jobs"] if remote in j["argv"]), None)
    need(job is not None, f"no frozen score-plan command for {remote}")
    report_path = EVIDENCE / f"{stage}-scores/{job['name']}.json"
    npz_path = report_path.with_name(report_path.stem + ".scores.npz")
    report = read_json(report_path)
    rows = [row for row in report["paths"] if row["input"] == remote]
    need(len(rows) == 1, f"expected one scorer row for {remote}")
    row = rows[0]
    need(sha(npz_path) == report["scores_sha256"], f"saved NPZ hash mismatch: {npz_path}")
    if stage == "followup":
        confidence = read_json(REPRO / "followup-confidence.json")
        confidence_row = next((r for r in confidence["reports"] if r["job"] == job["name"]), None)
        need(confidence_row is not None, f"missing frozen followup hash record: {job['name']}")
        need(sha(report_path) == confidence_row["report_sha256"], f"saved report hash mismatch: {report_path}")
        need(sha(npz_path) == confidence_row["score_npz_sha256"], f"saved scorer NPZ hash mismatch: {npz_path}")
    return job, report_path, npz_path, report, row


def local_workspace(stage: str, cell: str, batch: int, path_id: int) -> Path:
    return EVIDENCE / stage / cell / f"batch{batch:02d}" / f"path-{path_id}"


def verify_workspace(path: Path, expected: dict[str, str], label: str) -> dict[str, str]:
    need(path.is_dir(), f"workspace missing: {path}")
    observed = {}
    for name, expected_hash in expected.items():
        file = path / name
        need(file.is_file(), f"{label}: missing {name}")
        observed[name] = sha(file)
        need(observed[name] == expected_hash, f"{label}: hash mismatch for {name}")
    return observed


def verify_six_diagnostics():
    diagnostic = read_json(REPRO / "wrong-key-diagnostic.json")
    checked = []
    for item in diagnostic["raw_workspace_diagnostics"]:
        rel = Path(item["input"])
        need(rel.parts[0:3] == ("local-evidence", "triad-selective", "key-diagnostic"),
             f"unexpected diagnostic path {rel}")
        stage = rel.parts[3]
        cell = rel.parts[4]
        batch = int(rel.parts[5].removeprefix("batch"))
        path_id = int(rel.parts[6].removeprefix("path-"))
        diag_path = REPO / rel
        # Check all hashes in the saved diagnostic receipt, then bind the same
        # workspace to the original actual receiver's saved per-path receipt.
        diag_hashes = verify_workspace(diag_path, item["input_hashes"], "diagnostic")
        _, report_path, npz_path, report, row = find_scored_path(stage, cell, batch, path_id)
        score_hashes = row.get("input_hashes", {})
        need(set(score_hashes) == set(HASH_FILES), f"scorer receipt incomplete: {row['input']}")
        remote_path = local_workspace(stage, cell, batch, path_id)
        score_actual = verify_workspace(remote_path, score_hashes, "saved scorer")
        # The diagnostic snapshot and full score archive are copied from the
        # same workspace; require common files to be byte-identical.
        common = {k: v for k, v in diag_hashes.items() if k in score_actual}
        need(all(common[k] == score_actual[k] for k in common), f"snapshot differs from scorer input: {cell}/{path_id}")
        checked.append({"input": item["input"], "diagnostic_hashes_verified": len(diag_hashes),
                        "scorer_hashes_verified": len(score_actual), "score_report": str(report_path.relative_to(REPO)),
                        "score_report_sha256": sha(report_path), "score_npz_sha256": sha(npz_path),
                        "matched_common_hashes": len(common)})
    need(len(checked) == 6, f"expected six retained key diagnostics; found {len(checked)}")
    return checked


CASES = [
    ("g01slowrate-wrongkey-90001", "followup", "g01slowrate-each-65-cn0m24-wrong-key", 0, 90001),
    ("g01slowrate-signal-90001", "followup", "g01slowrate-each-65-cn0m24-signal", 0, 90001),
    ("g01-wrongkey-60001", "holdout", "g01-each-65-cn0m22-wrong-key", 0, 60001),
    ("g001-primary-miss-60146", "holdout", "g001-each-65-cn0m27-signal", 2, 60146),
    ("g001-null-60000", "holdout", "g001-each-65-cn0m26-null", 0, 60000),
]


def score_values(report: dict, npz_path: Path, row: dict):
    cond = row["conditions"][0]
    control = cond["control"]
    method = next(iter(cond["methods"].values()))
    values = np.load(npz_path)[method["score_array"]]
    need(values.shape == (1, 2), f"expected one pair of bit scores, got {values.shape}")
    scores = [float(v) for v in values[0]]
    best = max(scores)
    winner = int(np.argmax(scores))
    return {"control": control, "scores_by_bit": scores, "maximum_log_score": best,
            "winner_bit": winner, "strict_admitted": best >= STRICT,
            "relaxed_admitted": best >= RELAXED,
            "strict_threshold": STRICT, "relaxed_threshold": RELAXED,
            "counts_from_report": method.get("counts")}


def replay(case, index, output_dir):
    label, stage, cell, batch, path_id = case
    job, original_report_path, original_npz, original_report, original_row = find_scored_path(stage, cell, batch, path_id)
    input_hashes = original_row.get("input_hashes", {})
    need(set(input_hashes) == set(HASH_FILES), f"incomplete original scorer inputs for {label}")
    source = local_workspace(stage, cell, batch, path_id)
    verify_workspace(source, input_hashes, "replay input")

    argv = list(job["argv"])
    # Input positional paths precede --output; preserve the selected path and
    # every receiver/search setting from the frozen job.
    original_remote = original_prefix(stage, cell, batch, path_id)
    need(original_remote in argv, f"selected path absent from frozen command: {label}")
    argv = argv[:2] + [str(source)] + argv[argv.index("--output"):]
    output = output_dir / f"{index:02d}-{label}.json"
    argv[argv.index("--output") + 1] = str(output)
    argv[0] = sys.executable
    argv[1] = str(REPO / "tools/triad-conditional-sweep.py")
    device_idx = argv.index("--device") + 1
    precision_idx = argv.index("--precision") + 1
    need(argv[device_idx] == "cuda:0" and argv[precision_idx] == "fp32", "unexpected frozen precision/device")
    argv[device_idx] = "cpu"
    argv[precision_idx] = "fp64"
    command = argv
    env = os.environ.copy()
    env.update({"OMP_NUM_THREADS": "1", "OPENBLAS_NUM_THREADS": "1", "MKL_NUM_THREADS": "1", "NUMEXPR_NUM_THREADS": "1", "CUDA_VISIBLE_DEVICES": ""})
    start = time.monotonic()
    done = subprocess.run(command, cwd=REPO, env=env, text=True, capture_output=True, check=False)
    elapsed = time.monotonic() - start
    need(done.returncode == 0, f"replay failed for {label} rc={done.returncode}: {done.stderr[-2000:]}")
    replay_report_path = output
    replay_npz = output.with_name(output.stem + ".scores.npz")
    replay_report = read_json(replay_report_path)
    need(sha(replay_npz) == replay_report["scores_sha256"], f"replay NPZ hash mismatch: {label}")
    replay_rows = [r for r in replay_report["paths"] if r["input"] == str(source)]
    need(len(replay_rows) == 1, f"replay report missing selected physical path: {label}")
    before = score_values(original_report, original_npz, original_row)
    after = score_values(replay_report, replay_npz, replay_rows[0])
    need(before["control"] == after["control"], f"control changed in replay: {label}")
    return {"label": label, "stage": stage, "cell": cell, "path_id": path_id,
            "source_workspace": str(source.relative_to(REPO)), "verified_input_hashes": input_hashes,
            "original_report": str(original_report_path.relative_to(REPO)),
            "original_report_sha256": sha(original_report_path), "original_npz_sha256": sha(original_npz),
            "replay_command": command, "replay_report": str(replay_report_path.relative_to(REPO)),
            "replay_report_sha256": sha(replay_report_path), "replay_npz_sha256": sha(replay_npz),
            "replay_wall_seconds": elapsed, "stdout": done.stdout[-2000:], "stderr": done.stderr[-2000:],
            "original_precision_device": "GPU FP32", "replay_precision_device": "CPU FP64",
            "original_source_sha256": original_report.get("source_sha256"),
            "original_dependency_sha256": original_report.get("dependency_sha256"),
            "replay_source_sha256": replay_report.get("source_sha256"),
            "replay_dependency_sha256": replay_report.get("dependency_sha256"),
            "original": before, "replay": after,
            "score_delta_by_bit": [after["scores_by_bit"][i] - before["scores_by_bit"][i] for i in (0, 1)],
            "maximum_score_delta": after["maximum_log_score"] - before["maximum_log_score"],
            "maximum_absolute_score_delta": max(abs(after["scores_by_bit"][i] - before["scores_by_bit"][i]) for i in (0, 1)),
            "winner_unchanged": before["winner_bit"] == after["winner_bit"],
            "strict_admission_unchanged": before["strict_admitted"] == after["strict_admitted"],
            "relaxed_admission_unchanged": before["relaxed_admitted"] == after["relaxed_admitted"]}


def main():
    output_dir = fresh_output_dir()
    six = verify_six_diagnostics()
    results = [replay(case, i, output_dir) for i, case in enumerate(CASES, 1)]
    summary = {"schema": "triad-selective-cpu-fp64-replay-v1",
               "scope": "Five selected existing actual-noisy captures; only one-path selection, output path, device, and precision changed from frozen score plans. Original captures were not regenerated.",
               "archive_sha256": "6bcdc7ee6d210d4b57c5aad7abdbb15fd987e12680d3bd7055053e5aff56b3b4",
               "strict_relaxed_gate_source": "wrong-key-diagnostic.json operating points; zero log-score margin",
               "strict_gate": STRICT, "relaxed_gate": RELAXED,
               "replay_output_directory": str(output_dir.relative_to(REPO)),
               "verified_key_diagnostic_workspaces": six,
               "replays": results,
               "limitations": ["Original scores are GPU FP32; replays are CPU FP64, so differences include device/library arithmetic as well as precision.",
                               "Only selected individual paths were replayed; this is a reproducibility check, not a new performance sample.",
                               "Strict and relaxed admissions use the documented thresholds with zero margin; wrong-key controls are not Gaussian-null trials."]}
    target = REPRO / "replay-summary.json"
    target.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"summary": str(target.relative_to(REPO)), "verified_diagnostics": len(six),
                      "replays": len(results), "all_winners_unchanged": all(r["winner_unchanged"] for r in results),
                      "all_gates_unchanged": all(r["strict_admission_unchanged"] and r["relaxed_admission_unchanged"] for r in results)}, indent=2))


if __name__ == "__main__":
    main()
