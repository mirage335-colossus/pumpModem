#!/usr/bin/env python3
"""Verify and replay the repository-retained Helper study evidence."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys
from collections import Counter, defaultdict


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUNDLE = ROOT / "docs/validation-data/helper/2026-10-31"
EXPECTED_COUNTS = {"screen": 80, "heldout": 64, "mismatch": 50,
                   "strong-transients": 6, "variants": 16, "cpu": 6}
FREQUENCY_FIELDS = ("frequency36_available", "frequency36_error_hz",
                    "frequency36_conditional_half_width_hz",
                    "frequency36_interval_covered", "later_frozen_frequency_count",
                    "later_frozen_frequency_rms_hz")


class EvidenceError(Exception):
    pass


def pair_key(row):
    # CPU repetitions are deliberately repeated captures; compare each pair,
    # rather than silently replacing two of them with the last repetition.
    return row["suite"], tuple(row["argv"][:7]), row.get("repetition")


def fail(message: str) -> None:
    raise EvidenceError(message)


def load_json(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot read {path}: {exc}")


def finite_tree(value, where="value"):
    if isinstance(value, float) and not math.isfinite(value):
        fail(f"non-finite number at {where}")
    if isinstance(value, dict):
        for key, item in value.items():
            finite_tree(item, f"{where}.{key}")
    elif isinstance(value, list):
        for i, item in enumerate(value):
            finite_tree(item, f"{where}[{i}]")


def get_rows(bundle: Path):
    manifest = load_json(bundle / "manifest.json")
    finite_tree(manifest, "manifest")
    if manifest.get("schema_version") != 1:
        fail("unsupported manifest schema_version")
    hashes = manifest.get("files_sha256", {})
    if set(hashes) != {"cases.jsonl", "reservations.json", "strict-inversion.json"}:
        fail("manifest file hash list differs from the bundle schema")
    for name, digest in hashes.items():
        path = bundle / name
        if not path.is_file():
            fail(f"manifest file missing: {name}")
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != digest:
            fail(f"SHA-256 mismatch: {name}")
    try:
        rows = [json.loads(line) for line in (bundle / "cases.jsonl").read_text(encoding="utf-8").splitlines() if line]
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot read cases.jsonl: {exc}")
    for i, row in enumerate(rows):
        finite_tree(row, f"cases[{i}]")
    return manifest, rows


def verify(bundle: Path):
    manifest, rows = get_rows(bundle)
    ids = [r.get("id") for r in rows]
    if None in ids or len(ids) != len(set(ids)):
        fail("case ids are missing or duplicated")
    counts = Counter(r.get("suite") for r in rows)
    if dict(counts) != EXPECTED_COUNTS:
        fail(f"suite counts differ: {dict(counts)}")
    if manifest.get("case_counts") != EXPECTED_COUNTS:
        fail("manifest case_counts differ from the supported schema")
    for row in rows:
        for field in ("id", "suite", "argv", "binary_sha256", "summary", "metrics", "profile_reason_counts"):
            if field not in row:
                fail(f"{row.get('id', '<unknown>')}: missing {field}")
        argv, summary = row["argv"], row["summary"]
        if not isinstance(argv, list) or len(argv) != 8 or not all(isinstance(x, str) for x in argv):
            fail(f"{row['id']}: argv must contain eight strings")
        try:
            seed, loss = int(argv[2]), int(argv[1])
        except ValueError:
            fail(f"{row['id']}: invalid seed/loss argv")
        if summary.get("seed") != seed or summary.get("loss_db") != loss:
            fail(f"{row['id']}: seed/loss disagree with argv")
        if summary.get("mode") != argv[3] or summary.get("boundary_method") != argv[7]:
            fail(f"{row['id']}: mode/method disagree with argv")
        if summary.get("noise_seed") != seed + 20_000_000:
            fail(f"{row['id']}: noise_seed relationship is invalid")
        if not isinstance(row["binary_sha256"], str) or len(row["binary_sha256"]) != 64:
            fail(f"{row['id']}: invalid binary hash")
        expected_binary = (manifest["binary_provenance"]["accounting_sha256"] if row["suite"] == "cpu"
                           else manifest["binary_provenance"]["campaign_sha256"])
        if row["binary_sha256"] != expected_binary:
            fail(f"{row['id']}: binary hash does not match its provenance group")
        for f in FREQUENCY_FIELDS:
            if f not in summary:
                fail(f"{row['id']}: missing frequency field {f}")
        latest = summary.get("profile36_latest_observation_end_sample")
        if latest is not None and latest / 24000 > 36.0 + 1e-9:
            fail(f"{row['id']}: latest profile observation exceeds 36 seconds")
    # Reservation keys are 24 fixed bytes, then uint32 little-endian seed and four zero bytes.
    reservations = load_json(bundle / "reservations.json")
    finite_tree(reservations, "reservations")
    keys = reservations.get("keys", [])
    encoded = set()
    reserved_seed_partition = {}
    for item in keys:
        try:
            raw = bytes.fromhex(item["key"])
        except (KeyError, ValueError):
            fail("invalid reservation key encoding")
        seed = item.get("seed")
        if len(raw) != 32 or raw[:24] != b"HH-HELPER-FHSS-TEST-V3!!" or raw[28:] != b"\0\0\0\0":
            fail("reservation key must have the exact 24-byte prefix and 32-byte layout")
        if int.from_bytes(raw[24:28], "little") != seed or item.get("noise_seed") != seed + 20_000_000:
            fail("reservation key seed/noise_seed relationship is invalid")
        if item["key"] in encoded:
            fail("duplicate reservation key")
        encoded.add(item["key"])
        reserved_seed_partition[seed] = item.get("partition")
    if len(keys) != 48:
        fail(f"expected 48 reservation keys, found {len(keys)}")
    for row in rows:
        if row["suite"] != "cpu" and row["summary"]["seed"] not in reserved_seed_partition:
            fail(f"{row['id']}: seed is absent from reservations")
        expected_partition = {"screen": "feasibility", "variants": "feasibility",
                              "heldout": "heldout", "mismatch": "mismatch",
                              "strong-transients": "mismatch", "cpu": "heldout"}[row["suite"]]
        if reserved_seed_partition.get(row["summary"]["seed"]) != expected_partition:
            fail(f"{row['id']}: seed reservation partition does not match suite")
    screen = defaultdict(lambda: defaultdict(set))
    for row in rows:
        if row["suite"] == "screen":
            screen[int(row["argv"][1])][int(row["argv"][2])].add(row["summary"]["boundary_method"])
    if len(screen) != 5 or any(len(seeds) != 8 for seeds in screen.values()):
        fail("screen data must contain five loss levels with eight matched seeds each")
    if any(methods != {"nominal", "pooled-inversion"} for seeds in screen.values() for methods in seeds.values()):
        fail("screen loss/seed arms are not paired across both methods")
    heldout_by_seed = defaultdict(set)
    for row in rows:
        if row["suite"] == "heldout":
            seed = row["summary"]["seed"]
            heldout_by_seed[seed].add(row["summary"]["boundary_method"])
            if reserved_seed_partition.get(seed) != "heldout":
                fail(f"{row['id']}: heldout seed reservation partition mismatch")
    if len(heldout_by_seed) != 32 or any(v != {"nominal", "pooled-inversion"} for v in heldout_by_seed.values()):
        fail("heldout must contain 32 distinct paired reserved seeds")
    arm_ids = Counter((r["suite"], tuple(r["argv"]), r["summary"]["boundary_method"]) for r in rows)
    if any(n > 1 and key[0] != "cpu" for key, n in arm_ids.items()):
        fail("duplicate matched arm outside intentional CPU repeats")
    if sorted(r.get("repetition") for r in rows if r["suite"] == "cpu") != [0, 0, 1, 1, 2, 2]:
        fail("CPU rows must retain three paired intentional repeats")
    negative_modes = {"straight-auxiliary", "no-auxiliary", "noise"}
    negative = [r for r in rows if r["suite"] == "mismatch" and r["summary"].get("mode") in negative_modes]
    if len(negative) != 24:
        fail(f"expected 24 negative-control arms, found {len(negative)}")
    negative_groups = Counter((r["summary"]["mode"], r["summary"]["loss_db"]) for r in negative)
    if len(negative_groups) != 6 or any(n != 4 for n in negative_groups.values()):
        fail("negative controls must cover four keys at each of two losses and three modes")
    # Matched method arms share the same frozen 36-second frequency result.
    pairs = defaultdict(dict)
    for row in rows:
        s = row["summary"]
        key = pair_key(row)
        pairs[key][s["boundary_method"]] = s
    matched = 0
    for methods in pairs.values():
        if "nominal" in methods and "pooled-inversion" in methods:
            matched += 1
            for f in FREQUENCY_FIELDS:
                if methods["nominal"][f] != methods["pooled-inversion"][f]:
                    fail(f"matched frozen frequency differs for {f}")
    strict = load_json(bundle / "strict-inversion.json")
    if len(strict.get("runs", [])) != 80:
        fail("strict-inversion must contain 80 earlier marker arms")
    for run in strict["runs"]:
        finite_tree(run, f"strict.{run.get('id', '?')}")
        summary = run.get("summary", {})
        if len(run.get("markers", [])) != int(summary.get("inversion_markers", -1)):
            fail(f"{run.get('id')}: marker array/count mismatch")
        errors = [float(m["evaluator_error_us"]) for m in run.get("markers", [])]
        if errors:
            rms = math.sqrt(sum(x*x for x in errors) / len(errors))
            if abs(rms - float(summary["inversion_rms_us"])) > 1e-5:
                fail(f"{run.get('id')}: marker RMS mismatch")
            if abs(max(map(abs, errors)) - float(summary["inversion_worst_us"])) > 1e-5:
                fail(f"{run.get('id')}: marker worst error mismatch")
        radii = [float(m["marker_radius_samples"]) for m in run.get("markers", [])]
        if radii and abs(max(radii) * 1_000_000 / 24000 - float(summary["inversion_max_radius_us"])) > 1e-3:
            fail(f"{run.get('id')}: marker radius mismatch")
    return manifest, rows, matched


def rms(values):
    values = list(values)
    return math.sqrt(sum(v*v for v in values) / len(values)) if values else None


def summarize(manifest, rows, bundle):
    def key_for(row):
        s = row["summary"]
        if row["suite"] in ("mismatch", "variants", "strong-transients"):
            return row["id"]
        if row["suite"] == "cpu":
            return f"cpu/{s['boundary_method']}"
        return (f"{row['suite']}/{s['boundary_method']}/loss-{s['loss_db']}/mode-{s['mode']}"
                f"/wave-{row['argv'][4]}/delay-{row['argv'][5]}/rf-{row['argv'][6]}")

    output = {"case_counts": dict(Counter(r["suite"] for r in rows)),
              "matched_nominal_vs_pooled_frequency_pairs": 0, "screen_loss_seed_counts": {},
              "frequency36": {}, "profile36": {}, "fresh_hop_phase": {}, "phase_weighted_clean_twin": {},
              "wrong_hop_assignment": {}, "negative_controls": {}, "profile_failures": {},
              "rolling_timing_tolerance_counts": {}, "cpu": {}, "strict_markers_by_loss": {},
              "rolling_timing_note": "Counts are correlated accepted calendar updates through 40 s, not independent captures or frozen 36 s results.",
              "comparison_note": "Each capture contributes one frozen observation; paired method rows share data and are correlated.",
              "source_binding_limits": manifest.get("source_provenance", {}).get("limits", [])}
    grouped = defaultdict(list)
    paired = defaultdict(set)
    loss_seeds = defaultdict(set)
    phase_acc = defaultdict(lambda: defaultdict(lambda: [0.0, 0]))
    wrong = defaultdict(Counter)
    for r in rows:
        s = r["summary"]
        group = key_for(r)
        grouped[group].append(r)
        paired[pair_key(r)].add(s["boundary_method"])
        if r["suite"] == "screen":
            loss_seeds[s["loss_db"]].add(s["seed"])
        if r["suite"] != "cpu":
            for m in r.get("metrics", {}).get("helper_complete_tone", []):
                if (m.get("scope") == "continuous_synthetic_clean_twin" and m.get("selection") == "all"
                        and m.get("region") in ("beginning", "interior", "end")):
                    a = phase_acc[group][m["region"]]
                    a[0] += m["clean_phase_rms_deg"] ** 2 * m["phase_samples"]
                    a[1] += m["phase_samples"]
            for metric_name, entries in r.get("metrics", {}).items():
                if isinstance(entries, list):
                    for m in entries:
                        if isinstance(m, dict) and "wrong_hop_assignment" in m:
                            detail = f"{metric_name}/{m.get('scope', '?')}/{m.get('region', m.get('bin', '?'))}/{m.get('selection', '?')}"
                            wrong[group][detail] += m["wrong_hop_assignment"]
    output["matched_nominal_vs_pooled_frequency_pairs"] = sum(
        {"nominal", "pooled-inversion"}.issubset(v) for v in paired.values())
    output["screen_loss_seed_counts"] = {str(k): len(v) for k, v in sorted(loss_seeds.items())}

    neg_modes = {"straight-auxiliary", "no-auxiliary", "noise"}
    negative = [r for r in rows if r["suite"] == "mismatch" and r["summary"]["mode"] in neg_modes]
    neg_fields = ("inversion_markers", "profile_local_qualified",
                  "profile_calendar_qualified_updates", "profile_selected_boundaries")
    output["negative_controls"] = {
        "arms": len(negative),
        "by_mode_and_loss": {f"{mode}/{loss}": sum(r["summary"]["mode"] == mode and r["summary"]["loss_db"] == loss for r in negative)
                             for mode, loss in sorted({(r["summary"]["mode"], r["summary"]["loss_db"]) for r in negative})},
        "nonzero_counts": {f: sum(bool(r["summary"].get(f, 0)) for r in negative) for f in neg_fields},
        "false_positive_ids": [r["id"] for r in negative if any(r["summary"].get(f, 0) for f in neg_fields)]}

    for key, group in sorted(grouped.items()):
        summaries = [r["summary"] for r in group]
        favail = [s for s in summaries if s.get("frequency36_available")]
        if any("frequency36_available" in s for s in summaries):
            output["frequency36"][key] = {
                "cases": len(summaries), "available": len(favail), "unavailable": len(summaries)-len(favail),
                "error_rms_hz": rms(s["frequency36_error_hz"] for s in favail),
                "worst_abs_error_hz": max((abs(s["frequency36_error_hz"]) for s in favail), default=None),
                "interval_covered": sum(bool(s["frequency36_interval_covered"]) for s in favail),
                "conditional_half_width_median_hz": statistics.median(s["frequency36_conditional_half_width_hz"] for s in favail) if favail else None}
        pavail = [s for s in summaries if s.get("profile36_available")]
        if any("profile36_available" in s for s in summaries):
            qualified = [s for s in pavail if s.get("profile36_qualified")]
            output["profile36"][key] = {
                "cases": len(summaries), "available": len(pavail), "unavailable": len(summaries)-len(pavail),
                "qualified": len(qualified), "available_unqualified": len(pavail)-len(qualified),
                "error_rms_us": rms(s["profile36_error_us"] for s in pavail),
                "worst_abs_error_us": max((abs(s["profile36_error_us"]) for s in pavail), default=None),
                "interval_covered": sum(abs(s["profile36_error_us"]) <= s["profile36_radius_us"] for s in pavail),
                "radius_median_us": statistics.median(s["profile36_radius_us"] for s in pavail) if pavail else None,
                "interpretation": "Top-level error and interval statistics include all available results, including unqualified ones.",
                "qualified_result": {
                    "error_rms_us": rms(s["profile36_error_us"] for s in qualified),
                    "worst_abs_error_us": max((abs(s["profile36_error_us"]) for s in qualified), default=None),
                    "interval_covered": sum(abs(s["profile36_error_us"]) <= s["profile36_radius_us"] for s in qualified),
                    "radius_median_us": statistics.median(s["profile36_radius_us"] for s in qualified) if qualified else None}}
        hop = [s for s in summaries if s.get("fresh_hop_phase_count", 0)]
        if hop:
            n = sum(s["fresh_hop_phase_count"] for s in hop)
            output["fresh_hop_phase"][key] = {
                "cases": len(hop), "phase_count": n,
                "error_rms_deg": math.sqrt(sum(s["fresh_hop_phase_rms_deg"]**2 * s["fresh_hop_phase_count"] for s in hop)/n),
                "worst_deg": max(s["fresh_hop_phase_worst_deg"] for s in hop)}
        if key in phase_acc:
            regions = phase_acc[key]
            total_sq, total_n = sum(v[0] for v in regions.values()), sum(v[1] for v in regions.values())
            output["phase_weighted_clean_twin"][key] = {
                "rms_deg": math.sqrt(total_sq/total_n) if total_n else None,
                "phase_samples": total_n,
                "disjoint_region_phase_samples": {region: values[1] for region, values in regions.items()},
                "note": "all-selection continuous clean twin; uncertain outputs included; boundary excluded"}
        threshold_fields = ("profile_within_25us", "profile_within_125us", "profile_within_1ms", "profile_within_10ms")
        threshold_counts = {f: sum(s.get(f, 0) for s in summaries) for f in threshold_fields}
        if any(threshold_counts.values()):
            output["rolling_timing_tolerance_counts"][key] = threshold_counts
    # The held-out interval is inside the continuous scope. Never sum both.
    for group, details in sorted(wrong.items()):
        scope_totals = Counter()
        for detail, count in details.items():
            metric, scope, _region, selection = detail.split("/")
            scope_totals[f"{metric}/{scope}/{selection}"] += count
        output["wrong_hop_assignment"][group] = {
            "by_metric_scope": dict(scope_totals),
            "by_metric_scope_region": dict(details),
            "complete_output_wrong_hop_samples": sum(r["summary"]["physical_schedule_mismatches"] for r in grouped[group]),
            "note": "Scopes overlap and must not be added. Complete-output count comes from the receiver/evaluator summary."}
    for suite in ("mismatch", "strong-transients"):
        ss = [r["summary"] for r in rows if r["suite"] == suite]
        available = sum(bool(s.get("profile36_available")) for s in ss)
        qualified = sum(bool(s.get("profile36_available") and s.get("profile36_qualified")) for s in ss)
        output["profile_failures"][suite] = {"cases": len(ss), "unavailable": len(ss)-available,
                                            "available_unqualified": available-qualified, "qualified": qualified}

    cpu_groups = defaultdict(list)
    for r in rows:
        if r["suite"] == "cpu":
            cpu_groups[r["summary"]["boundary_method"]].append(r["summary"])
    for method, ss in sorted(cpu_groups.items()):
        output["cpu"][method] = {
            "repeats": len(ss),
            "receiver_cpu36_median_s": statistics.median(s["receiver_cpu36_including_output_instrumentation_seconds"] for s in ss),
            "receiver_capture_drain_40s_median_s": statistics.median(s["receiver_cpu_capture_drain_including_output_instrumentation_seconds"] for s in ss),
            "exact36_components_median_s": {f: statistics.median(s[f] for s in ss) for f in
                ("receiver_measurement_and_history_cpu36_seconds", "profile_observe_cpu36_seconds")},
            "exact36_tracking_sum_median_s": statistics.median(s["receiver_measurement_and_history_cpu36_seconds"] + s["profile_observe_cpu36_seconds"] for s in ss),
            "conditioned_input_seconds_at36": sorted({s["conditioned_input_seconds_at36"] for s in ss}),
            "conditioner_cpu36_median_s": statistics.median(s["conditioner_cpu36_seconds"] for s in ss),
            "40s_conditioner_median_s": statistics.median(s["conditioner_push_release_cpu_seconds"] for s in ss),
            "40s_generation_median_s": statistics.median(s["generation_cpu_seconds"] for s in ss),
            "40s_metrology_median_s": statistics.median(s["metrology_cpu_seconds"] for s in ss),
            "accounting": "36-second tracking sum excludes conditioning. Receiver totals overlap; 40s counters are separate and not added."}
    strict = load_json(bundle / "strict-inversion.json")
    strict_groups = defaultdict(list)
    for run in strict["runs"]:
        strict_groups[int(run["summary"]["loss_db"])].append(run)
    for loss, runs in sorted(strict_groups.items()):
        markers = [m for r in runs for m in r["markers"]]
        errors = [float(m["evaluator_error_us"]) for m in markers]
        output["strict_markers_by_loss"][str(loss)] = {
            "arms": len(runs), "markers": len(markers), "error_rms_us": rms(errors),
            "worst_abs_error_us": max(map(abs, errors)) if errors else None,
            "interval_covered": sum(abs(float(m["evaluator_error_us"])) <= float(m["marker_radius_samples"])*1_000_000/24000 for m in markers),
            "max_radius_us": max((float(m["marker_radius_samples"])*1_000_000/24000 for m in markers), default=None)}
    return output


def run_case(bundle: Path, case_id: str, binary: Path, output: Path):
    _, rows, _ = verify(bundle)
    row = next((r for r in rows if r["id"] == case_id), None)
    if row is None:
        fail(f"unknown case id: {case_id}")
    if not binary.is_file():
        fail(f"binary does not exist: {binary}")
    if output.exists():
        fail(f"output path already exists; refusing to overwrite: {output}")
    if not os.access(binary, os.X_OK):
        fail(f"binary is not executable: {binary}")
    try:
        digest = hashlib.sha256(binary.read_bytes()).hexdigest()
        output.mkdir(parents=True)
        proc = subprocess.run([str(binary), *row["argv"]], capture_output=True, text=True,
                              encoding="utf-8", errors="replace", check=False)
        (output / "stdout.txt").write_text(proc.stdout, encoding="utf-8")
        (output / "stderr.txt").write_text(proc.stderr, encoding="utf-8")
        record = {"case_id": case_id, "argv": row["argv"], "binary": str(binary.resolve()),
                  "binary_sha256": digest, "expected_historical_binary_sha256": row["binary_sha256"],
                  "historical_binary_match": digest == row["binary_sha256"],
                  "exit_code": proc.returncode,
                  "provenance_note": "Current replay only; partial source hashes do not establish complete historical source/compiler/environment identity."}
        (output / "replay.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    except OSError as exc:
        fail(f"replay or evidence write failed: {exc}")
    print(json.dumps(record, indent=2))
    return proc.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", nargs="?", choices=("verify", "summary", "run"), default="verify")
    parser.add_argument("--bundle", type=Path, default=DEFAULT_BUNDLE, help="bundle directory (default: repository bundle)")
    parser.add_argument("--case", help="case id for run")
    parser.add_argument("--binary", type=Path, help="explicit executable for run")
    parser.add_argument("--output", type=Path, help="new output directory for run; must not exist")
    args = parser.parse_args()
    try:
        if args.command == "run":
            if not args.case or not args.binary or not args.output:
                fail("run requires --case, --binary, and --output")
            return run_case(args.bundle, args.case, args.binary.resolve(), args.output.resolve())
        manifest, rows, matched = verify(args.bundle)
        if args.command == "verify":
            print(f"verified {len(rows)} cases, 48 reservation keys, 80 strict marker arms; {matched} matched frequency pairs")
        else:
            report = summarize(manifest, rows, args.bundle)
            print(json.dumps(report, indent=2, sort_keys=True))
        return 0
    except EvidenceError as exc:
        print(f"helper-evidence: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
