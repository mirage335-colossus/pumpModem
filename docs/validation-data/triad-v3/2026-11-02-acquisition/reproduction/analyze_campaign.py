#!/usr/bin/env python3
"""Audit frozen grouped-acquisition jobs and recount their actual observations.

No receiver inference or physical simulation is run. Numerical array reads are
restricted to score NPZ files. Omitted sampled arrays require a collector hash
inventory; this is receipt verification, not local verification of omitted bytes.

--evidence-root maps the frozen remote campaign directory containing workspaces/
and scores/. Inventory accepts [{path,sha256,size_bytes}] or {files:[...]}.
Build provenance requires source_file_sha256 and binary_sha256. --receipts
accepts combined {plan_sha256:{export:...,score:...},results:[{name,stage,argv,
runner,returncode}]} records, with argv_sha256 optional. Standard run_jobs.py
results can instead be supplied with --export-receipts/--score-receipts.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import re
import sys

import numpy as np
from scipy.special import logsumexp
from scipy.stats import beta


GATES = {"strict": 22.72106338958574, "relaxed": 17.422746023037703}
DOMAINS = ("key", "channel", "oscillator", "noise", "frontend", "path")
SYNC = ("arrival_seconds", "frequency_hz", "clock_ppm", "epoch_offset_seconds",
        "frequency_drift_hz_per_second")
INPUTS = ("manifest.json", "group_gram.c128", "group_observation.c128",
          "received_energy.f64", "intervals.u64", "valid_rows.u64")


class Incomplete(Exception):
    pass


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def key_hash(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()


def read(path):
    if not Path(path).is_file():
        raise Incomplete("missing " + str(path))
    return json.loads(Path(path).read_text())


def options(argv, skip=1):
    result, current = {}, None
    for value in argv[skip:]:
        if value.startswith("--"):
            require(value not in result, "duplicate option " + value)
            current = value
            result[current] = []
        else:
            require(current is not None, "value preceding options")
            result[current].append(value)
    return result


def one(argv, flag):
    return argv[argv.index(flag) + 1]


def finite(values, shape, label):
    result = np.asarray(values, dtype=np.float64)
    require(result.shape == shape and np.isfinite(result).all(), "invalid " + label)
    return result


def near(a, b, label):
    require(np.allclose(a, b, rtol=1e-12, atol=1e-10), label + " differs")


def summarize_decision(scores, control, truth, gate):
    winner = int(np.argmax(scores))
    accepted = bool(scores[winner] >= gate)
    result = {"winner_bit": winner, "accepted": accepted, "winner_score": float(scores[winner]),
              "opposite_bit_gap": float(scores[winner] - scores[1-winner])}
    if control == "signal":
        result["counts"] = {"correct_admitted": int(accepted and winner == truth),
                            "wrong_admitted": int(accepted and winner != truth), "missed": int(not accepted)}
        result["raw_correct"] = int(winner == truth)
    else:
        result["counts"] = {"false_admitted": int(accepted), "not_admitted": int(not accepted)}
    return result


def score_views(record, manifest, group_scores, bit_scores):
    """Recount full mixture and fixed zero-offset group using unchanged UCBs."""
    h = manifest["group_count"]
    group_scores = finite(group_scores, (h, 2), "NPZ group scores")
    bit_scores = finite(bit_scores, (2,), "NPZ bit scores")
    near(group_scores, finite(record["group_bit_log_likelihoods"], (h, 2), "JSON group scores"), "group JSON/NPZ")
    near(bit_scores, finite(record["bit_log_likelihoods"], (2,), "JSON bit scores"), "bit JSON/NPZ")
    near(bit_scores, logsumexp(group_scores, axis=0) - math.log(h), "full fixed-group mixture")
    public = record["geometry"]["public_group_parameters"]
    require(len(public) == h, "public group count differs")
    require(public == [g["sync_key"] for g in manifest["groups"]], "manifest/public group mapping differs")
    selected = []
    for group in manifest["groups"]:
        by_bit = {}
        for index, columns in zip(group["candidate_indices"], group["column_mapping"]):
            if len(columns) == 2:
                candidate = manifest["candidates"][index]
                bit = candidate["bit"]
                require(type(bit) is int and bit in (0, 1) and bit not in by_bit,
                        "group has ambiguous two-tap bit mapping")
                require(all(candidate[k] == group["sync_key"][k] for k in SYNC),
                        "candidate/group synchronization differs")
                by_bit[bit] = index
        require(set(by_bit) == {0, 1}, "group lacks both two-tap bit candidates")
        selected.append([by_bit[0], by_bit[1]])
    require(record["geometry"]["selected_candidates"] == selected, "score bit-column mapping differs")
    lead = float(manifest["capture_lead_seconds"])
    require(math.isfinite(lead), "invalid capture lead")
    arrivals = finite([g["arrival_seconds"] for g in public], (h,), "public arrivals")
    nominal = int(np.argmin(abs(arrivals - lead)))
    # Nominal is fixed from public source-coordinate zero, never from truth.
    winner = int(np.argmax(bit_scores))
    best = int(np.argmax(group_scores[:, winner]))
    require(record["best_scoring_group_for_winning_bit"] == best, "best group index differs")
    require(record["best_group_public_parameters"] == public[best], "best group parameters differ")
    calibration = record["calibration"]
    require(calibration["group_count"] == h and calibration["relaxed_uses_strict_calibration"] is True,
            "calibration family or relaxed policy differs")
    require(calibration["known_noise_variance_diagnostic"] is None
            and calibration["inter_group_independence_assumed"] is False
            and calibration["conditional_noise_resampling_supported"] is False, "unsupported noise calibration")
    upper = finite(calibration["variance_upper_over_reference"], (h,), "variance UCB")
    require(np.all(upper > 0), "nonpositive variance UCB")
    require(calibration["reference_variance"] == 1, "reference variance differs")
    delta = calibration["strict"]["delta_per_segment"]
    require(math.isclose(delta, 5e-5/(1440*8*2), rel_tol=1e-12, abs_tol=0),
            "strict calibration segment allocation differs")
    require(math.isclose(calibration["delta_per_group"], math.nextafter(delta/h, 0.), rel_tol=1e-12, abs_tol=0),
            "group calibration allocation differs")
    control, truth = manifest["control"], manifest["signal_bit_simulation_truth"]
    require(control in ("signal", "null", "wrong-key") and type(truth) is int and truth in (0, 1),
            "invalid evaluation-only truth/control")
    evaluation = record.get("simulation_evaluation_only")
    require(evaluation and evaluation["control"] == control and evaluation["signal_bit"] == truth,
            "evaluation metadata differs")
    views = {"full_bank": {}, "nominal_group_same_calibration": {}}
    for gate_name, threshold in GATES.items():
        near(calibration[gate_name]["fixed_window_log_threshold"], threshold, gate_name + " budget gate")
        observed = summarize_decision(bit_scores, control, truth, threshold)
        saved = record[gate_name]
        require(saved["bit"] == observed["winner_bit"] and saved["accepted"] == observed["accepted"],
                gate_name + " saved decision differs")
        near(saved["score"], observed["winner_score"], gate_name + " winner score")
        near(saved["opposite_bit_margin"], observed["opposite_bit_gap"], gate_name + " score gap")
        near(saved["log_threshold"], threshold, gate_name + " decision gate")
        require(evaluation["outcomes"][gate_name] == observed["counts"], gate_name + " evaluation counts differ")
        views["full_bank"][gate_name] = observed
        views["nominal_group_same_calibration"][gate_name] = summarize_decision(
            group_scores[nominal], control, truth, threshold)
    true_arrival = manifest.get("selected_truth_arrival_seconds_simulation_only")
    if true_arrival is not None:
        require(math.isfinite(true_arrival), "nonfinite evaluation-only arrival")
    return {"views": views, "nominal_group_index": nominal,
            "nominal_arrival_source_seconds": float(arrivals[nominal]-lead),
            "chosen_group_index": best, "chosen_arrival_source_seconds": float(arrivals[best]-lead),
            "true_arrival_source_seconds_evaluation_only": None if true_arrival is None else true_arrival-lead,
            "chosen_minus_true_arrival_seconds_evaluation_only": None if true_arrival is None else float(arrivals[best]-true_arrival),
            "arrival_coordinate": "capture-coordinate arrivals minus manifest capture_lead_seconds",
            "maximum_variance_ucb_over_reference": float(upper.max()),
            "nominal_variance_ucb_over_reference": float(upper[nominal]),
            "nominal_scope": "one publicly fixed group, retaining its full-family conservative calibration and the SAME gates; no noise recalibration or truth-selected alignment"}


class Evidence:
    def __init__(self, root, remote_prefix, inventory=None):
        self.root, self.remote = Path(root).resolve(), Path(remote_prefix)
        self.entries, self.hashes = {}, {}
        self.inventory_path = None
        if inventory and Path(inventory).is_file():
            self.inventory_path = str(inventory)
            value = read(inventory)
            for entry in value if isinstance(value, list) else value["files"]:
                relative = self.relative(entry["path"])
                require(relative not in self.entries, "duplicate inventory path " + str(relative))
                require(re.fullmatch("[0-9a-f]{64}", entry["sha256"]), "invalid inventory digest")
                self.entries[relative] = entry

    def relative(self, path):
        path = Path(path)
        if path.is_absolute():
            try:
                return path.relative_to(self.remote)
            except ValueError:
                return path.relative_to(self.root)
        require(".." not in path.parts, "parent traversal in evidence path")
        return path

    def local(self, path):
        return self.root / self.relative(path)

    def check(self, path, expected=None, size=None, bytes_required=True):
        relative = self.relative(path)
        local, entry = self.root / relative, self.entries.get(relative)
        if entry:
            if expected is not None:
                require(entry["sha256"] == expected, "inventory/receiver hash mismatch: " + str(relative))
            if size is not None:
                require(entry.get("size_bytes", entry.get("size")) == size, "inventory size mismatch: " + str(relative))
        if bytes_required:
            if not local.is_file():
                raise Incomplete("missing local artifact " + str(relative))
            if relative not in self.hashes:
                self.hashes[relative] = sha(local)
            actual = self.hashes[relative]
            if expected is not None:
                require(actual == expected, "artifact hash mismatch: " + str(relative))
            if entry:
                require(actual == entry["sha256"], "local/inventory hash mismatch: " + str(relative))
                require(local.stat().st_size == entry.get("size_bytes", entry.get("size")), "local/inventory size mismatch")
            return {"sha256": actual, "verification": "local_bytes"}
        if not entry:
            raise Incomplete("missing array hash inventory: " + str(relative))
        return {"sha256": entry["sha256"], "verification": "collector_hash_receipt_only; raw bytes not read locally"}


def load_plans(directory):
    docs = {name: read(Path(directory)/(name+"-plan.json")) for name in ("campaign", "export", "score")}
    master = docs["campaign"]
    require(master["jobs"] == docs["export"]["jobs"] and master["scoring_jobs"] == docs["score"]["jobs"],
            "component job plans differ from campaign")
    for stage in ("export", "score"):
        require(docs[stage]["captures"] == master["captures"], stage + " capture metadata differs")
        require(docs[stage]["source_file_sha256"] == master["source_file_sha256"], stage + " source binding differs")
    require(key_hash(master["source_file_sha256"]) == master["source_file_inventory_sha256"], "source inventory hash differs")
    require(len(master["jobs"]) == master["counts"]["exports_including_optional"]
            and len(master["scoring_jobs"]) == master["counts"]["score_jobs_including_optional"], "frozen job counts differ")
    captures = {c["name"]: c for c in master["captures"]}
    require(len(captures) == len(master["captures"]) == len(master["jobs"]), "duplicate/missing captures")
    for stage, jobs in (("export", master["jobs"]), ("score", master["scoring_jobs"])):
        require(len({j["name"] for j in jobs}) == len(jobs), "duplicate " + stage + " jobs")
        for job in jobs:
            name = job["name"] if stage == "export" else job["depends_on_export"]
            capture = captures[name]
            require(job["input_binding"] == capture["input_binding"], "job capture binding differs: " + job["name"])
            require(key_hash(capture["input_key"]) == capture["input_binding"]["capture_input_key_sha256"],
                    "capture input key differs")
    linked = sorted(n for c in captures.values() for n in c["score_jobs"])
    require(linked == sorted(j["name"] for j in master["scoring_jobs"]), "score/capture job coverage differs")
    return master, {name: sha(Path(directory)/(name+"-plan.json")) for name in docs}


def load_receipts(paths, stage, hashes, result):
    for path in paths:
        document = read(path)
        binding = document.get("plan_sha256")
        if binding:
            if isinstance(binding, str):
                require(stage and binding == hashes[stage], "receipt plan hash differs")
            else:
                for name, digest in binding.items():
                    require(name in hashes and hashes[name] == digest, "receipt plan hash differs")
        for row in document["results"]:
            actual_stage = stage or row["stage"]
            require(actual_stage in ("export", "score"), "unknown receipt stage")
            key = (actual_stage, row["name"])
            require(key not in result, "duplicate receipt " + str(key))
            result[key] = row


def check_receipt(job, stage, receipts):
    row = receipts.get((stage, job["name"]))
    if row is None:
        raise Incomplete("missing runner receipt")
    command = row.get("argv", row.get("command"))
    require(command == job["argv"], "runner argv differs from frozen job")
    if "argv_sha256" in row:
        require(row["argv_sha256"] == key_hash(command), "runner argv hash differs")
    run = row["runner"]
    require(run["schema"] == "triad-bounded-run-v1", "unknown runner schema")
    require(run["timeout_seconds"] == job["timeout_seconds"], "runner deadline differs from frozen job")
    if not (row.get("returncode", run.get("exit_code")) == 0 and run.get("exit_code") == 0
            and run.get("child_exit_code") == 0 and run.get("outcome") == "completed"
            and run.get("timed_out") is False and run.get("cancelled") is False
            and run.get("termination", {}).get("group_empty") is True):
        raise Incomplete("runner not successfully completed: " + json.dumps(run, sort_keys=True))
    return {"outcome": run["outcome"], "exit_code": run["exit_code"],
            "elapsed_seconds": run.get("elapsed_seconds"), "argv_sha256": key_hash(command)}


def verify_manifest(manifest, job, capture):
    require(manifest["format"] == "triad-group-block-stats-v1" and manifest["stats_noisy"] is True,
            "wrong grouped actual manifest")
    require(manifest["command_argv"] == job["argv"], "manifest command differs from frozen export")
    opt = options(manifest["command_argv"])
    opt.pop("--output")
    opt.pop("--mode")
    require(opt == capture["input_key"]["scientific_options"], "manifest scientific input key differs")
    truth = capture["input_binding"]["simulation_only_expected"]
    require(manifest["control"] == truth["control"] and manifest["signal_bit_simulation_truth"] == truth["source_bit"]
            and manifest["epoch"] == truth["epoch"], "manifest source identity differs")
    for domain in DOMAINS:
        observed = manifest["key_seed"] if domain == "key" else manifest["random_seed_roots"][domain]
        require(observed == truth[domain+"_seed"], domain + " seed differs")
    require(manifest["cn0_db_hz"] == capture["cn0_db_hz"] and manifest["block_samples"] == capture["block_samples"],
            "manifest C/N0 or base block differs")
    require(manifest["group_count"] == 109 and len(manifest["groups"]) == 109, "timing group count differs")
    config = manifest["resolved_configuration"]
    require(config["public_search"]["covers_initial_timing_prior"] is True, "initial timing prior not covered")
    require(config["selective_channel"]["model"] == "dispersive-dft-v1", "wrong selective model")


def analyze_score(payload, archive, manifest, job, source_hash):
    require(payload["format"] == "triad-group-acquisition-campaign-v1"
            and payload["receiver_source_sha256"] == source_hash, "receiver source/format differs")
    require(len(payload["captures"]) == 1, "expected one actual capture per score job")
    record = payload["captures"][0]
    require(record["input"] == job["argv"][2] and record["mode"] == "actual-noisy"
            and record["method"] == "group-local-gaussian-window-ucb", "receiver input or method differs")
    geometry = record["geometry"]
    require(geometry["reblock"] == job["reblock"] and geometry["base_block_samples"] == manifest["block_samples"]
            and geometry["block_samples"] == manifest["block_samples"]*job["reblock"]
            and geometry["sample_count"] == manifest["sample_count"]
            and geometry["sample_rate"] == manifest["sample_rate"], "score geometry differs")
    near(geometry["nominal_window_seconds"], .25, "receiver window")
    require(record["arithmetic"]["bulk"] == one(job["argv"], "--precision")
            and record["arithmetic"]["device"] == one(job["argv"], "--device")
            and record["arithmetic"]["tf32_allowed"] is False, "receiver arithmetic differs")
    require(set(archive.files) == {"group_scores_0", "bit_scores_0"}, "unexpected score NPZ members")
    result = score_views(record, manifest, archive["group_scores_0"], archive["bit_scores_0"])
    result.update({"input_sha256": record["input_sha256"], "receiver_seconds": record["timing"]["total_receiver_seconds"],
                   "score_geometry": {k: geometry[k] for k in ("group_count", "group_dimension", "base_block_samples",
                                                              "reblock", "block_samples", "block_count", "window_blocks")},
                   "source_elapsed_seconds": manifest["actual_elapsed_seconds"],
                   "capture_seconds": manifest["sample_count"]/manifest["sample_rate"]})
    return result


def cp95(success, trials):
    if not trials:
        return None
    return [float(beta.ppf(.025, success, trials-success+1)) if success else 0.,
            float(beta.ppf(.975, success+1, trials-success)) if success < trials else 1.]


def strata(rows):
    groups = defaultdict(list)
    fields = ("kind", "profile", "depth", "cn0_db_hz", "control", "base_block_samples", "reblock")
    for row in rows:
        groups[tuple(row[f] for f in fields)].append(row)
    output = []
    for key, records in sorted(groups.items()):
        good = [r for r in records if r["state"] == "verified"]
        item = dict(zip(fields, key), expected=len(records), verified=len(good),
                    complete=len(good) == len(records), views={})
        item["population"] = ("independent physical seeds within this stratum" if key[0] in ("independent", "control")
                              else "four-path paired development; C/N0, depth and block variants reuse physical roots")
        roots = [r["seed_vector"] for r in good]
        require(len(set(roots)) == len(roots), "duplicate seed vectors WITHIN a reported stratum")
        for view in ("full_bank", "nominal_group_same_calibration"):
            item["views"][view] = {}
            for gate in GATES:
                totals = Counter()
                bits = defaultdict(list)
                for row in good:
                    decision = row["analysis"]["views"][view][gate]
                    totals.update(decision["counts"])
                    if row["control"] == "signal":
                        totals["raw_correct"] += decision["raw_correct"]
                    bits[row["truth_bit"]].append(decision)
                intervals = {name: cp95(n, len(good)) for name, n in totals.items()} if item["complete"] else None
                per_bit = {}
                for bit, decisions in bits.items():
                    counts = Counter()
                    for d in decisions:
                        counts.update(d["counts"])
                    per_bit[str(bit)] = {"n": len(decisions), "counts": dict(counts),
                                        "pointwise_cp95": {n: cp95(v, len(decisions)) for n, v in counts.items()} if item["complete"] else None}
                item["views"][view][gate] = {"n": len(good), "counts": dict(totals),
                    "pointwise_common_probability_cp95": intervals, "per_source_bit": per_bit}
        output.append(item)
    return output


def run(args):
    master, plan_hashes = load_plans(args.plans)
    remote = args.remote_prefix or str(Path(one(master["jobs"][0]["argv"], "--output")).parents[1])
    evidence = Evidence(args.evidence_root, remote, args.inventory)
    issues, receipts = [], {}
    for paths, stage in ((args.receipts, None), (args.export_receipts, "export"), (args.score_receipts, "score")):
        try:
            load_receipts(paths, stage, plan_hashes, receipts)
        except Incomplete as error:
            issues.append({"state": "incomplete", "reason": str(error)})
    known_jobs = {(s, j["name"]) for s, jobs in (("export", master["jobs"]), ("score", master["scoring_jobs"])) for j in jobs}
    require(set(receipts) <= known_jobs, "receipt has job outside frozen plans")
    build = None
    try:
        build = read(args.build_provenance)
        for path, digest in master["source_file_sha256"].items():
            require(build["source_file_sha256"][path] == digest, "build source hash differs: " + path)
        require(re.fullmatch("[0-9a-f]{64}", build["binary_sha256"]), "missing/invalid built binary hash")
    except Incomplete as error:
        issues.append({"state": "incomplete", "reason": str(error)})
    captures = {c["name"]: c for c in master["captures"]}
    exported, export_rows = {}, []
    for job in master["jobs"]:
        row = {"name": job["name"], "stage": "export", "input_binding": job["input_binding"]}
        try:
            row["runner"] = check_receipt(job, "export", receipts)
            directory = one(job["argv"], "--output")
            manifest_path = str(Path(directory)/"manifest.json")
            row["manifest_binding"] = evidence.check(manifest_path)
            manifest = read(evidence.local(manifest_path))
            verify_manifest(manifest, job, captures[job["name"]])
            for name, size in manifest["file_sizes"].items():
                evidence.check(str(Path(directory)/name), size=size, bytes_required=False)
            row["state"] = "verified"
            exported[job["name"]] = (manifest, directory, row["manifest_binding"]["sha256"])
        except Incomplete as error:
            row.update(state="incomplete", reason=str(error))
        except (ValueError, KeyError, TypeError, OSError) as error:
            row.update(state="invalid", reason=str(error))
        export_rows.append(row)
    rows = []
    for job in master["scoring_jobs"]:
        capture = captures[job["depends_on_export"]]
        truth = capture["input_binding"]["simulation_only_expected"]
        row = {k: capture[k] for k in ("kind", "profile", "depth", "cn0_db_hz", "control")}
        row.update(name=job["name"], capture_id=capture["name"], base_block_samples=capture["block_samples"],
                   reblock=job["reblock"], truth_bit=truth["source_bit"],
                   seed_vector=":".join(str(truth[d+"_seed"]) for d in DOMAINS), input_binding=job["input_binding"])
        try:
            row["runner"] = check_receipt(job, "score", receipts)
            if capture["name"] not in exported:
                raise Incomplete("export dependency not verified")
            manifest, directory, manifest_hash = exported[capture["name"]]
            report_path, npz_path = one(job["argv"], "--output"), one(job["argv"], "--scores-output")
            row["report_binding"] = evidence.check(report_path)
            payload = read(evidence.local(report_path))
            row["score_binding"] = evidence.check(npz_path, payload["scores_npz_sha256"])
            with np.load(evidence.local(npz_path), allow_pickle=False) as archive:
                result = analyze_score(payload, archive, manifest, job, master["source_file_sha256"]["tools/triad_group_acquire.py"])
            hashes = result.pop("input_sha256")
            require(set(hashes) == set(INPUTS), "receiver input hash set differs")
            require(hashes["manifest.json"] == manifest_hash, "receiver manifest hash differs")
            row["input_hash_bindings"] = {name: evidence.check(str(Path(directory)/name), hashes[name],
                bytes_required=(name == "manifest.json")) for name in INPUTS}
            row.update(state="verified", analysis=result)
        except Incomplete as error:
            row.update(state="incomplete", reason=str(error))
        except (ValueError, KeyError, TypeError, OSError) as error:
            row.update(state="invalid", reason=str(error))
        rows.append(row)
    complete = not issues and all(r["state"] == "verified" for r in export_rows+rows)
    invalid = any(r["state"] == "invalid" for r in export_rows+rows)
    result = {"schema": "triad-acquisition-analysis-v1", "state": "complete" if complete else "invalid" if invalid else "incomplete",
              "scientific_qualification": False, "plan_sha256": plan_hashes,
              "analyzer_sha256": sha(__file__), "evidence_root": str(evidence.root), "remote_prefix": str(evidence.remote),
              "provenance": {"inventory_sha256": sha(args.inventory) if args.inventory and Path(args.inventory).is_file() else None,
                             "build_provenance_sha256": sha(args.build_provenance) if build else None,
                             "receipt_files": [{"path": str(p), "sha256": sha(p)} for p in
                                               args.receipts+args.export_receipts+args.score_receipts if p.is_file()],
                             "binary_sha256": build["binary_sha256"] if build else None,
                             "source_file_sha256": master["source_file_sha256"],
                             "scope": "collector source/binary and raw-array hash receipts; no claim omitted arrays or remote binary were locally rehashed"},
              "issues": issues, "export_status_counts": dict(Counter(r["state"] for r in export_rows)),
              "score_status_counts": dict(Counter(r["state"] for r in rows)), "exports": export_rows,
              "scores": rows, "strata": strata(rows),
              "limits": ["No dependent C/N0, amplitude, reblock or fine/coarse variants are pooled as independent trials.",
                         "Pointwise two-sided Clopper-Pearson 95% intervals assume independent Bernoulli trials with a common probability within the named population. Pooled intervals additionally assume common probability across balanced bit strata; per-bit intervals are also supplied.",
                         "Intervals are suppressed for incomplete strata. They are neither simultaneous nor corrected for selecting operating points or methods.",
                         "Nominal timing uses the fixed public group nearest source-coordinate zero, with the SAME already-conservative calibration and gates; it is not a separately calibrated receiver or truth-aligned oracle.",
                         "Null and wrong-key admission populations remain separate. Noise-only calibration supplies no general wrong-key rejection guarantee.",
                         "Completion is artifact/execution accounting, not 99% reliability or operational daily-tail qualification."]}
    return result


def markdown(result):
    lines = [f"Campaign analysis state: **{result['state']}**. Scientific qualification: **not established**.",
             "", "| Stratum | Verified / expected | Full strict correct / wrong / miss (or false / rejected) | Nominal strict |",
             "|---|---:|---|---|"]
    for row in result["strata"]:
        label = f"{row['kind']} {row['profile']} {row['depth']} C/N₀={row['cn0_db_hz']} {row['control']} B{row['base_block_samples']}×{row['reblock']}"
        counts = [row["views"][v]["strict"]["counts"] for v in ("full_bank", "nominal_group_same_calibration")]
        keys = ("correct_admitted", "wrong_admitted", "missed") if row["control"] == "signal" else ("false_admitted", "not_admitted")
        cells = [" / ".join(str(c.get(k, 0)) for k in keys) for c in counts]
        lines.append(f"| {label} | {row['verified']} / {row['expected']} | {cells[0]} | {cells[1]} |")
    lines += ["", "Strict and relaxed per-capture decisions, intervals, timing diagnostics, provenance and missing/invalid jobs are retained in the JSON. Paired variants are not independent receptions."]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plans", type=Path, required=True)
    parser.add_argument("--evidence-root", type=Path, required=True)
    parser.add_argument("--remote-prefix")
    parser.add_argument("--inventory", type=Path)
    parser.add_argument("--build-provenance", type=Path)
    parser.add_argument("--receipts", type=Path, nargs="*", default=[])
    parser.add_argument("--export-receipts", type=Path, nargs="*", default=[])
    parser.add_argument("--score-receipts", type=Path, nargs="*", default=[])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--markdown", type=Path)
    parser.add_argument("--allow-partial-diagnostic", action="store_true",
                        help="permit exit0 for incomplete accounting; never relabel it complete or waive invalid artifacts")
    args = parser.parse_args()
    args.inventory = args.inventory or args.evidence_root/"retained-inventory.json"
    args.build_provenance = args.build_provenance or args.evidence_root/"build-provenance.json"
    if not (args.receipts or args.export_receipts or args.score_receipts):
        args.receipts = [args.evidence_root/"campaign-results.json"]
    try:
        result = run(args)
    except (ValueError, KeyError, TypeError, OSError, Incomplete) as error:
        result = {"schema": "triad-acquisition-analysis-v1", "state": "invalid", "scientific_qualification": False,
                  "fatal_error": str(error), "analyzer_sha256": sha(__file__)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n")
    if args.markdown and "strata" in result:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        args.markdown.write_text(markdown(result))
    print(json.dumps({k: result[k] for k in ("state", "export_status_counts", "score_status_counts", "fatal_error") if k in result}))
    return 0 if result["state"] == "complete" or (args.allow_partial_diagnostic and result["state"] == "incomplete") else 2


if __name__ == "__main__":
    raise SystemExit(main())
