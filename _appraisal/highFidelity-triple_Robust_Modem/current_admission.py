#!/usr/bin/env python3
"""Run the frozen native comparison; Python performs no modem processing."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parent
CASES = {
    "core": [(name, ["--case", name, "--admission-replay"]) for name in
             ("clean", "fade-byte", "fade-boundary", "noise", "wrong-key",
              "wrong-key-join", "separate")],
    "holdout": [(name, ["--case", name, "--admission-replay"]) for name in
                ("fade-holdout", "wrong-key-holdout")],
    "weak": [
        ("clean-8s-v192", ["--case", "clean", "--admission-replay", "--noise-variance", "192"]),
        ("clean-16s-v384", ["--case", "clean", "--admission-replay", "--noise-variance", "384",
                             "--bit-seconds", "16"]),
    ],
    "boundary": [("clean-8s-v768", ["--case", "clean", "--admission-replay", "--noise-variance", "768"])],
    "parity": [
        ("clean-direct-strict", ["--case", "clean", "--wrong-key-admission", "strict"]),
        ("wrong-key-direct-strict", ["--case", "wrong-key", "--wrong-key-admission", "strict"]),
    ],
}


def direct_parity(receipt, output):
    """Compare observations, not a Python implementation of admission or FEC."""
    baseline = json.loads((output / (receipt["case"] + ".json")).read_text())
    replay = next(row for row in baseline["admission_replay"]["policies"] if row["policy"] == "strict")
    unchanged = (receipt["input_iq_sha256"] == baseline["input_iq_sha256"]
                 and receipt["source_identity"] == baseline["source_identity"]
                 and receipt["wrong_key_admission"] == "strict")
    fields = ("index", "decision", "bit", "wrong_key_rejected")
    actual = [tuple(row[key] for key in fields) for row in receipt["slots"]]
    predicted = [tuple(row[key] for key in fields) for row in replay["slots"]]
    counts = ("canonical_correct_messages", "canonical_incorrect_messages",
              "canonical_premature_completions", "consumer_correct_messages",
              "consumer_incorrect_messages", "wrong_data_bits", "false_data_slots",
              "lost_active_slots", "quiet_active_slots")
    def messages(rows):
        return [{k: v for k, v in row.items() if k not in
                 ("message_service_seconds", "correct_against_source")} for row in rows]
    return (unchanged and actual == predicted and all(receipt[k] == replay[k] for k in counts)
            and messages(receipt["native_messages"]) == messages(replay["native_messages"])
            and messages(receipt["consumer_candidates"]) == messages(replay["consumer_candidates"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, default=ROOT / ".build/bin/high_fidelity_protected_reassembly_probe")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "local-evidence/hf-current-admission")
    parser.add_argument("--stage", choices=CASES, required=True)
    args = parser.parse_args()
    probe = args.probe.resolve(strict=True)
    output = args.output_dir.resolve()
    if not output.is_relative_to(ROOT / "local-evidence"):
        parser.error("output must remain in this appraisal's local-evidence directory")
    output.mkdir(parents=True, exist_ok=True)
    paths = [output / (name + ".json") for name, _ in CASES[args.stage]]
    summary_path = output / (args.stage + "-summary.json")
    if any(p.exists() for p in paths + [summary_path]):
        parser.error("refusing to replace retained receipts; use a fresh output directory")
    binary_sha = hashlib.sha256(probe.read_bytes()).hexdigest()
    rows = []
    identity = None
    def save_summary():
        summary_path.write_text(json.dumps({"stage": args.stage, "cases": rows,
            "complete": len(rows) == len(paths) and all(r["execution_and_replay_valid"] for r in rows),
            "scope": "fixed constructed captures; policies are dependent native downstream replays; no population reliability claim"},
            indent=2) + "\n", encoding="utf-8")
    for (name, options), path in zip(CASES[args.stage], paths):
        command = [str(probe), *options]
        began = time.monotonic()
        print("running", name, flush=True)
        try:
            run = subprocess.run(command, capture_output=True, text=True, timeout=135, check=False)
        except subprocess.TimeoutExpired as error:
            (output / (name + ".timeout.txt")).write_text(str(error), encoding="utf-8")
            for suffix, data in (("stdout", error.stdout), ("stderr", error.stderr)):
                data = data or b""
                (output / (name + ".partial." + suffix + ".txt")).write_bytes(
                    data if isinstance(data, bytes) else data.encode("utf-8"))
            rows.append({"id": name, "command": command, "binary_sha256": binary_sha,
                         "execution_and_replay_valid": False, "execution_error": "subprocess timeout",
                         "elapsed_seconds": time.monotonic() - began})
            save_summary()
            raise SystemExit("native case exceeded its 120-second deadline plus 15-second exit allowance") from error
        path.write_text(run.stdout, encoding="utf-8")
        (output / (name + ".stderr.txt")).write_text(run.stderr, encoding="utf-8")
        receipt = json.loads(run.stdout)
        source = receipt.get("source_identity")
        if identity is None:
            identity = source
        replay = receipt.get("admission_replay", {})
        complete = (run.returncode in (0, 1) and "error" not in receipt and source == identity
                    and source and all(value != "unavailable" for value in source.values())
                    and receipt.get("accepted_samples") == receipt.get("rendered_samples")
                    and receipt.get("input_open_at_completion") is True
                    and not receipt.get("observed_native_recovery")
                    and ("--admission-replay" not in options or
                         replay.get("executed") is True and replay.get("baseline_semantic_parity") is True))
        parity = direct_parity(receipt, output) if args.stage == "parity" and complete else None
        if parity is False:
            complete = False
        post_sha = hashlib.sha256(probe.read_bytes()).hexdigest() if probe.exists() else None
        binary_unchanged = post_sha == binary_sha
        complete = complete and binary_unchanged
        rows.append({"id": name, "command": command, "binary_sha256": binary_sha,
                     "binary_sha256_after_case": post_sha, "binary_unchanged": binary_unchanged,
                     "source_identity": source, "exit_code": run.returncode,
                     "elapsed_seconds": time.monotonic() - began, "execution_and_replay_valid": bool(complete),
                     "native_requirements_status": receipt.get("status"), "receipt": path.name,
                     "direct_native_matches_downstream_replay": parity})
        save_summary()
        print(name, receipt.get("status"), "valid=" + str(bool(complete)),
              "false_DATA=" + str(receipt.get("false_data_slots")), flush=True)
        if not complete:
            raise SystemExit("incomplete or invalid native result; preserve it and stop dependent cases")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
