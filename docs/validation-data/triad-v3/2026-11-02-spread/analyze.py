#!/usr/bin/env python3
"""Recount spread-study evidence without treating noise replicas as new paths.

Usage: python analyze.py --root EVIDENCE --output analysis.json [--plan PLAN]
Requires NumPy/SciPy. An optional jobs[].argv plan checks complete scoring-job
coverage and supplied hash/config bindings; scientific qualification is deferred
until a complete frozen generation/receiver matrix contract is supplied.
"""
import argparse
from collections import Counter, defaultdict
from decimal import Decimal, InvalidOperation
import hashlib
import json
import math
from pathlib import Path
import sys

import numpy as np
from scipy.stats import beta


SCHEMAS = {"triad-conditional-sweep-v1", "triad-static-frequency-study-v1",
           "triad-noise-study-v1", "triad-rbpf-study-v1", "triad-cfo-batch-study-v1"}
TOOL_SCHEMAS = {name: "triad-"+name.removeprefix("triad-").removesuffix(".py")+"-v1"
                for name in ("triad-conditional-sweep.py", "triad-static-frequency-study.py",
                             "triad-noise-study.py", "triad-rbpf-study.py", "triad-cfo-batch-study.py")}
OUTPUT_SCHEMA = "triad-spread-analysis-v1"
PHYSICAL_ROOTS = ("channel", "oscillator", "noise", "frontend", "path")
CONTROLS = ("signal", "null", "wrong-key")


def require(ok, message):
    if not ok:
        raise ValueError(message)


def read_json(path):
    def reject(value):
        raise ValueError(f"nonfinite JSON in {path.name}: {value}")
    return json.loads(path.read_text(), parse_constant=reject)


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(data)
    return h.hexdigest()


def fingerprint(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def cp(k, n):
    require(0 <= k <= n, "invalid binomial counts")
    return {"successes": k, "trials": n, "fraction": k/n if n else None,
            "lower_one_sided_95": float(beta.ppf(.05, k, n-k+1)) if k else 0.,
            "upper_one_sided_95": float(beta.ppf(.95, k+1, n-k)) if k < n else 1.}


class WorkspaceIndex:
    """Build suffix lookups once; each lookup is O(source path depth), not O(N)."""
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.workspaces = {}; self.suffixes = {}; self.cache = {}; self.file_checks = {}
        for path in sorted(self.root.rglob("manifest.json")):
            manifest = read_json(path)
            if not isinstance(manifest, dict) or manifest.get("format") != "triad-block-stats-v1":
                continue
            key = path.parent.relative_to(self.root).as_posix()
            self.workspaces[key] = (path, manifest)
            parts = Path(key).parts or (self.root.name,)
            for length in range(1, len(parts)+1):
                suffix = parts[-length:]
                if suffix in self.suffixes and self.suffixes[suffix] != key:
                    self.suffixes[suffix] = None
                else:
                    self.suffixes[suffix] = key

    def resolve(self, source):
        if source in self.cache:
            return self.cache[source]
        parts = Path(source).parts
        ambiguous = False
        for length in range(len(parts), 0, -1):
            suffix = parts[-length:]
            if suffix not in self.suffixes:
                continue
            value = self.suffixes[suffix]
            if value is None:
                ambiguous = True; continue
            self.cache[source] = value
            return value
        require(not ambiguous, f"ambiguous workspace suffix: {source}")
        self.cache[source] = None
        return None

    def verify(self, key, hashes, source):
        require(isinstance(hashes, dict), "invalid input hash map")
        for name, expected in hashes.items():
            require(isinstance(name, str) and Path(name).name == name and name not in ("", ".", ".."), "invalid workspace filename")
            require(isinstance(expected, str) and len(expected) == 64 and all(x in "0123456789abcdef" for x in expected), "invalid expected SHA256")
            identity = (key if key is not None else source, name)
            if identity in self.file_checks:
                require(self.file_checks[identity]["expected_sha256"] == expected, f"conflicting input hashes: {identity}")
                continue
            entry = {"expected_sha256": expected, "state": "workspace_unavailable_not_verified"}
            if key is not None:
                path = self.workspaces[key][0].parent/name
                require(path.resolve().is_relative_to(self.root), "input escapes evidence root")
                if path.is_file():
                    actual = sha256(path); require(actual == expected, f"input hash mismatch: {identity}")
                    entry.update(state="verified", actual_sha256=actual, bytes=path.stat().st_size)
                else:
                    entry["state"] = "missing_not_verified"
            self.file_checks[identity] = entry


def manifest_metadata(manifest):
    if manifest is None:
        return {"state": "manifest_unavailable"}
    model = manifest.get("channel_model", {})
    fmt = manifest.get("source_format", {})
    fs = manifest.get("sample_rate")
    return {"state": "manifest_present", "design_cn0_db_hz": manifest.get("cn0_db_hz"),
            "requested_active_seconds": manifest.get("requested_active_seconds"),
            "actual_elapsed_seconds": manifest.get("actual_elapsed_seconds"),
            "sampled_seconds": manifest["sample_count"]/fs if fs else None,
            "sample_rate_hz": fs, "inner_rate_hz": fmt.get("inner_rate_hz"),
            "fine_rate_hz": fmt.get("fine_rate_hz"), "carrier_spacing_hz": fmt.get("carrier_spacing_hz"),
            "spread_delay_ms": model.get("spread_delay_ms"), "spread_doppler_hz": model.get("spread_doppler_hz"),
            "selective_slow_depth": model.get("perturbation_slow_depth"),
            "selective_fast_depth": model.get("perturbation_fast_depth"),
            "path_evolution": {k: v for k, v in model.get("path_evolution", {}).items()
                               if isinstance(v, (int, float, bool))}}


def physical_seeds(manifest):
    if manifest is None:
        return None
    seeds = manifest.get("random_seed_roots", {})
    values = {name: seeds.get(name, manifest.get(name+"_seed")) for name in PHYSICAL_ROOTS}
    return values if all(type(v) is int for v in values.values()) else None


def gates(config):
    require(math.isclose(config["false_probability"], 1e-4, rel_tol=0, abs_tol=1e-15)
            and config["horizon_seconds"] == 86400, "report is not the declared strict daily budget")
    spacing = config["attempt_spacing"]; models = config["selection_methods"]; durations = config["duration_alternatives"]
    require(spacing > 0 and models >= 1 and durations >= 1, "invalid search family")
    def gate(alpha, horizon):
        return math.log(math.ceil(horizon/spacing)*models*durations*2/(alpha/2))
    return {"strict": gate(1e-4, 86400), "relaxed": gate(.01, 43200),
            "models": models, "durations": durations, "attempt_spacing_seconds": spacing,
            "score_alpha_fraction": .5, "relaxed_calibration": "Original strict calibration retained; scores are only re-gated."}


def recount(scores, truth, control, threshold, denominator):
    scores = np.asarray(scores, dtype=np.float64)
    require(scores.ndim == 2 and scores.shape[1] == 2 and len(scores) > 0 and np.isfinite(scores).all(), "invalid frozen score array")
    winner = scores.argmax(axis=1); admitted = scores[np.arange(len(scores)), winner] >= threshold
    if control == "signal":
        require(type(truth) is int and truth in (0, 1), "signal evaluation truth unavailable or nonbinary")
        return {denominator: len(scores), "correct_accepted": int(np.sum(admitted & (winner == truth))),
                "wrong_accepted": int(np.sum(admitted & (winner != truth))), "missed": int(np.sum(~admitted)),
                "raw_correct": int(np.sum(winner == truth))}
    require(control in ("null", "wrong-key"), f"unsupported control {control}")
    return {denominator: len(scores), "false_admitted": int(np.sum(admitted)), "not_admitted": int(np.sum(~admitted))}


def outcome(counts, control, denominator):
    fields = ("correct_accepted", "wrong_accepted", "missed") if control == "signal" else ("false_admitted", "not_admitted")
    return {"counts": counts, "pointwise_cp": {field: cp(counts[field], counts[denominator]) for field in fields}}


def group_rows(rows, failures):
    groups = defaultdict(list)
    for row in rows:
        key = (row["observation_mode"], row["receiver"], row["control"], row["cn0_db_hz"], fingerprint(row["physical_configuration"]))
        groups[key].append(row)
    result = []
    for key, selected in sorted(groups.items(), key=lambda item: str(item[0])):
        paths = {r["input"] for r in selected}
        seeds = [r["physical_seed_roots"] for r in selected]
        independent = (len(paths) == len(selected) and all(s is not None for s in seeds)
                       and all(len({s[name] for s in seeds}) == len(seeds) for name in PHYSICAL_ROOTS))
        entry = {"observation_mode": key[0], "receiver": key[1], "control": key[2], "cn0_db_hz": key[3],
                 "physical_configuration": selected[0]["physical_configuration"],
                 "observed_physical_paths": len(paths), "independent_seed_domains_verified": independent,
                 "independent_physical_paths_by_recorded_roots": len(paths) if independent else None,
                 "noise_trials": sum(r["noise_trials"] for r in selected),
                 "scientific_qualification": False}
        for gate in ("strict", "relaxed"):
            counts = Counter()
            for row in selected:
                counts.update(row[gate]["counts"])
            denominator = selected[0]["denominator"]
            summary = {"counts": dict(counts)}
            if key[0] == "actual-noisy":
                summary["pooled_cp_if_iid"] = outcome(counts, key[2], denominator)["pointwise_cp"]
                if key[2] == "signal":
                    summary["per_bit_cp"] = {}
                    for bit in (0, 1):
                        subset = [r for r in selected if r["truth_bit_evaluation_only"] == bit]
                        summary["per_bit_cp"][str(bit)] = cp(sum(r[gate]["counts"]["correct_accepted"] for r in subset), len(subset))
            else:
                summary["pooled_cp"] = None
                summary["interval_scope"] = "No pooled binomial interval: replicas from different fixed paths are clustered; use each path's conditional CP."
            entry[gate] = summary
        result.append(entry)
    # Failures remain separate: there is no fabricated score or successful trial.
    return result


def parse_argv(argv):
    leading = []; options = {}; current = None
    for token in argv:
        if token.startswith("--"):
            name, equal, value = token.partition("="); require(name not in options, "duplicate planned option")
            options[name] = [value] if equal else []; current = name
        elif current is None:
            leading.append(token)
        else:
            options[current].append(token)
    return leading, options


def option_equal(left, right):
    if len(left) != len(right): return False
    for a, b in zip(left, right):
        try:
            if Decimal(a) != Decimal(b): return False
        except InvalidOperation:
            if a != b: return False
    return True


def check_plan(path, index, reports, documents):
    if path is None:
        return {"state": "not_supplied", "scientific_qualification": False}
    plan = read_json(path); checks = []; skipped = []; used_reports = set()
    for job in plan.get("jobs", []):
        leading, options = parse_argv(job["argv"])
        tool = Path(leading[1]).name if len(leading) > 1 else ""
        if tool not in TOOL_SCHEMAS:
            skipped.append({"name": job.get("name"), "tool": tool, "state": "not_a_supported_scoring_job"}); continue
        require("--output" in options and len(options["--output"]) == 1, "planned scoring output missing")
        name = Path(options["--output"][0]).name
        matches = [r for r in reports if Path(r["report"]).name == name]
        require(len(matches) == 1, f"missing or ambiguous planned report: {name}")
        report = matches[0]; doc = documents[report["report"]]
        require(report["report"] not in used_reports, "duplicate planned scoring report")
        used_reports.add(report["report"])
        planned = [index.resolve(p) or p for p in leading[2:]]
        require(len(planned) == len(set(planned)), f"duplicate planned input: {name}")
        require(planned == report["input_paths"], f"missing or changed planned paths: {name}")
        require(doc["schema"] == TOOL_SCHEMAS[tool], "planned receiver tool/schema mismatch")
        unresolved = []
        for flag, values in options.items():
            if flag == "--output": continue
            field = flag[2:].replace("-", "_")
            if field == "methods" and doc["schema"] == "triad-conditional-sweep-v1": field = "receiver_methods"
            config = doc["configuration"]
            require(field in config, f"unrecorded explicit plan option: {name} {flag}")
            actual = config[field]
            target = [] if actual is True and not values else [str(v) for v in actual] if isinstance(actual, list) else [str(actual)]
            if flag == "--cn0-grid": values = values[0].split(",")
            target = ["inf" if x == "infinity" else x for x in target]
            require(option_equal(values, target), f"planned command/configuration mismatch: {name} {flag}")
        for field, expected in job.get("expected_report_fields", {}).items():
            require(doc.get(field) == expected, f"frozen report field mismatch: {name} {field}")
        if "report_sha256" in job:
            require(job["report_sha256"] == report["sha256"], f"frozen report hash mismatch: {name}")
        for source, hashes in job.get("input_hashes", {}).items():
            key = index.resolve(source); require(key is not None, f"planned workspace missing: {source}")
            index.verify(key, hashes, source)
            require(all(index.file_checks[(key, f)]["state"] == "verified" for f in hashes), "planned hash-bound file missing")
        checks.append({"report": report["report"], "planned_paths_accounted": len(planned),
                       "failed_paths": report["failed_physical_path_records"],
                       "expected_report_fields_checked": sorted(job.get("expected_report_fields", {})),
                       "unresolved_command_options": unresolved,
                       "report_hash_checked": "report_sha256" in job})
    require(checks, "plan contains no supported scoring jobs")
    return {"state": "supplied_command_coverage_and_hashes_checked", "sha256": sha256(path),
            "checks": checks, "skipped_jobs": skipped, "scientific_qualification": False,
            "limit": "Complete frozen scientific generation/default/source binding is deferred; supplied coverage/hash checks alone do not qualify a campaign."}


def analyze(root, plan=None):
    root = Path(root).resolve(); index = WorkspaceIndex(root)
    reports = []; documents = {}; unknown = []; identities = set(); physical_inputs = set()
    for path in sorted(root.rglob("*.json")):
        if path.name == "manifest.json": continue
        doc = read_json(path)
        if not isinstance(doc, dict) or doc.get("schema") not in SCHEMAS:
            if isinstance(doc, dict) and doc.get("schema") and doc["schema"] != OUTPUT_SCHEMA:
                unknown.append({"report": path.relative_to(root).as_posix(), "schema": doc["schema"], "not_scored": True})
            continue
        name = path.relative_to(root).as_posix(); documents[name] = doc
        conditional_schema = doc["schema"] == "triad-conditional-sweep-v1"
        mode = doc.get("observation_mode", "conditional") if conditional_schema else "actual-noisy"
        require(mode in ("conditional", "actual-noisy"), "unsupported observation mode")
        if doc["schema"] == "triad-noise-study-v1": require(doc["configuration"]["mode"] == "actual", "legacy projected noise mode unsupported; use conditional sweep schema")
        declared_methods = (["cfo-grid-ucb"] if doc["schema"] == "triad-cfo-batch-study-v1" else
                            doc["configuration"].get("receiver_methods") if conditional_schema else
                            doc["configuration"].get("methods"))
        require(isinstance(declared_methods, list) and declared_methods
                and all(isinstance(m, str) and m for m in declared_methods)
                and len(set(declared_methods)) == len(declared_methods), "invalid declared receiver method set")
        expected_methods = set(declared_methods)
        gate = gates(doc["configuration"]); config_key = fingerprint(doc["configuration"])
        scorepath = path.parent/Path(doc["scores_file"]).name; archive = None
        if scorepath.is_file():
            require(sha256(scorepath) == doc["scores_sha256"], f"score archive hash mismatch: {name}")
            archive = np.load(scorepath, allow_pickle=False)
        rows = []; partial = []; failures = []; used = set(); seen = set(); seen_indices = set(); input_paths = []
        records = doc["paths"] if conditional_schema else doc["captures"]
        try:
            for record in records:
                source = record["input"]; key = index.resolve(source); display = key or source
                require(display not in seen, f"duplicate physical path in {name}: {display}"); seen.add(display)
                require(record["physical_path_index"] not in seen_indices, f"duplicate path index in {name}")
                seen_indices.add(record["physical_path_index"]); input_paths.append(display); physical_inputs.add(display)
                manifest = index.workspaces[key][1] if key is not None else None
                hashes = record.get("sampled_input_hashes", record.get("input_hashes", {})).copy()
                if record.get("manifest_sha256"):
                    require(hashes.get("manifest.json", record["manifest_sha256"]) == record["manifest_sha256"], "conflicting manifest hashes")
                    hashes["manifest.json"] = record["manifest_sha256"]
                failed = record["status"] != "completed"
                require(failed or "manifest.json" in hashes, "completed path missing manifest hash")
                index.verify(key, hashes, source)
                metadata = manifest_metadata(manifest)
                if failed:
                    failed_control = record.get("control")
                    if failed_control is None and manifest is not None:
                        failed_control = manifest.get("control")
                    if failed_control not in CONTROLS: failed_control = None
                    failures.append({"input": display, "control": failed_control,
                                     "error_type": record.get("error_type"), "error": record.get("error"),
                                     "partial_score_rows": 0, "excluded_from_scored_denominators": True})
                conditions = record.get("conditions", []) if conditional_schema else [{
                    "control": record["control"], "noise_trials": record.get("conditional_noise_trials", 1),
                    "cn0_db_hz": manifest.get("cn0_db_hz") if manifest else doc["configuration"].get("cn0"),
                    "methods": record["methods"]}]
                require(failed or conditions, "completed path has no conditions")
                if mode == "actual-noisy" and not failed:
                    require(len(conditions) == 1 and conditions[0]["noise_trials"] == 1, "actual-noisy path must have exactly one observation")
                seen_conditions = set()
                for condition in conditions:
                    require(isinstance(condition["methods"], dict), "invalid method result map")
                    returned_methods = set(condition["methods"])
                    require(returned_methods <= expected_methods, "undeclared receiver method result")
                    require(failed or returned_methods == expected_methods,
                            "completed condition missing declared receiver method results")
                    control = condition["control"]; n = condition["noise_trials"]
                    require(type(n) is int and n > 0, "invalid noise trial count")
                    cn0 = condition["cn0_db_hz"] if mode == "conditional" else (manifest.get("cn0_db_hz") if manifest else doc["configuration"].get("cn0"))
                    require((cn0, control) not in seen_conditions, "duplicate path condition"); seen_conditions.add((cn0, control))
                    if mode == "actual-noisy":
                        require(n == 1 and not condition.get("draws_simulation_only"), "actual data cannot contain generated noise replicas")
                        if manifest: require(manifest.get("control", "signal") == control, "actual control/manifest mismatch")
                    truth = None
                    if control == "signal":
                        if mode == "conditional": require(manifest is not None, "conditional signal requires retained evaluation manifest")
                        truth = record.get("truth_bit_simulation_only")
                        if manifest:
                            expected = manifest.get("signal_bit_simulation_truth", manifest.get("signal_bit"))
                            require(truth is None or truth == expected, "evaluation truth mismatch")
                            truth = expected
                    for method, detail in condition["methods"].items():
                        identity = (mode, display, config_key, cn0, control, method)
                        require(identity not in identities, f"duplicate input/configuration/condition/method across reports: {display} {method}")
                        identities.add(identity)
                        threshold = detail["threshold"]
                        if isinstance(threshold, dict): threshold = threshold["threshold_log_likelihood_ratio"]
                        require(math.isclose(threshold, gate["strict"], abs_tol=1e-10, rel_tol=0), "wrong frozen strict threshold")
                        score_key = detail.get("score_array", f"path{record['physical_path_index']:05d}_{control}_{method}")
                        inline = detail.get("bit_log_scores")
                        if archive is not None:
                            require(score_key in archive and score_key not in used, "missing or reused score-array key")
                            scores = archive[score_key]; used.add(score_key)
                            if inline is not None: require(np.array_equal(scores, np.asarray([inline])), "inline/archive scores disagree")
                        else:
                            require(inline is not None, "missing score archive and inline scores")
                            scores = np.asarray([inline])
                        require(len(scores) == n, "noise-trial/score count mismatch")
                        denominator = "trials" if conditional_schema else "captures"
                        counts = recount(scores, truth, control, gate["strict"], denominator)
                        require(counts == detail["counts"], f"reported counts disagree: {name} {display} {method}")
                        row = {"input": display, "observation_mode": mode, "receiver": method, "control": control,
                               "cn0_db_hz": cn0, "noise_trials": n, "denominator": denominator,
                               "truth_bit_evaluation_only": truth, "physical_configuration": metadata,
                               "physical_seed_roots": physical_seeds(manifest), "scientific_qualification": False,
                               "interval_scope": "conditional noise on one fixed physical path" if mode == "conditional" else "one actual sampled physical-path observation",
                               "strict": outcome(counts, control, denominator),
                               "relaxed": outcome(recount(scores, truth, control, gate["relaxed"], denominator), control, denominator)}
                        (partial if failed else rows).append(row)
                        if failed: failures[-1]["partial_score_rows"] += 1
            if archive is not None: require(set(archive.files) == used, "unaccounted score arrays")
        finally:
            if archive is not None: archive.close()
        declared_failed = doc.get("failed_physical_path_records", doc.get("failed_physical_capture_records"))
        if declared_failed is not None: require(declared_failed == len(failures), "reported failure count disagrees")
        if "sampled_physical_paths" in doc: require(doc["sampled_physical_paths"] == len(records), "reported physical path count disagrees")
        conservative = {}
        unknown_actual_failures = sum(f["control"] not in CONTROLS for f in failures) if mode == "actual-noisy" else 0
        if mode == "actual-noisy":
            for method in expected_methods:
                method_rows = [r for r in rows if r["receiver"] == method and r["control"] == "signal"]
                signal_failures = sum(f["control"] == "signal" for f in failures)
                conservative[method] = {}
                for budget in ("strict", "relaxed"):
                    values = Counter()
                    for row in method_rows: values.update({k:v for k,v in row[budget]["counts"].items() if k not in ("trials", "captures")})
                    values["missed"] += signal_failures+unknown_actual_failures
                    conservative[method][budget] = {"attempts": len(method_rows)+signal_failures+unknown_actual_failures,
                                                    "known_signal_attempts": len(method_rows)+signal_failures,
                                                    "signal_denominator_established": unknown_actual_failures == 0,
                                                    "failed_signal_paths_counted_as_missed": signal_failures,
                                                    "unknown_control_failures_counted_as_missed": unknown_actual_failures,
                                                    "scope": "Known signal failures count as misses. Any unknown-control actual failures are pessimistically included as signal misses and leave their true signal/control denominator unestablished.",
                                                    **dict(values)}
        reports.append({"report": name, "sha256": sha256(path), "schema": doc["schema"], "observation_mode": mode,
                        "source_sha256": doc.get("source_sha256"), "configuration": doc["configuration"], "gates": gate,
                        "scores_sha256": doc["scores_sha256"], "score_archive_state": "verified" if scorepath.is_file() else "missing_inline_only",
                        "input_paths": input_paths, "observed_physical_path_records": len(records),
                        "completed_physical_path_records": len(records)-len(failures), "failed_physical_path_records": len(failures),
                        "unclassified_failed_actual_physical_path_records": unknown_actual_failures,
                        "actual_signal_attempt_denominator_established": unknown_actual_failures == 0 if mode == "actual-noisy" else None,
                        "rows": rows, "groups": group_rows(rows, failures), "failures": failures,
                        "conservative_actual_signal_attempt_counts": conservative,
                        "excluded_partial_score_rows": partial, "scientific_qualification": False})
    require(reports, "no supported receiver reports found")
    binding = check_plan(plan, index, reports, documents)
    return {"schema": OUTPUT_SCHEMA, "analyzer_sha256": sha256(Path(__file__)), "reports": reports,
            "plan_binding": binding, "unknown_schemas": unknown,
            "input_file_checks": [{"input": k[0], "filename": k[1], **v} for k, v in sorted(index.file_checks.items())],
            "summary": {"reports": len(reports), "distinct_observed_physical_input_paths": len(physical_inputs),
                        "failed_path_records": sum(r["failed_physical_path_records"] for r in reports),
                        "unclassified_failed_actual_path_records": sum(r["unclassified_failed_actual_physical_path_records"] for r in reports),
                        "conditional_method_noise_trials": sum(x["noise_trials"] for r in reports for x in r["rows"] if x["observation_mode"] == "conditional"),
                        "actual_method_path_observations": sum(x["noise_trials"] for r in reports for x in r["rows"] if x["observation_mode"] == "actual-noisy"),
                        "unknown_schema_documents": len(unknown)},
            "scope": "Diagnostic recount only. Exact CP endpoints are separately pointwise one-sided 95%, not a joint 95% interval. Conditional replicas are not additional physical paths; conditional path groups receive no pooled binomial interval or qualification. Actual pooled CP is conditional on iid paths; per-bit intervals distinguish fixed strata. Root-seed uniqueness checks recorded independence assumptions, not field validation. Failed and partial paths are explicit and excluded from scored denominators. Conservative actual signal attempts count known signal failures and pessimistically unknown-control failures as misses; unknown controls mark the actual signal denominator unestablished. No daily false-admission or scientific qualification is established by this summarizer."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path); parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--plan", type=Path)
    args = parser.parse_args(); require(args.root.is_dir(), "evidence root missing")
    if args.output.exists(): require(read_json(args.output).get("schema") == OUTPUT_SCHEMA, "refusing to overwrite another artifact")
    result = analyze(args.root, args.plan); args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, sort_keys=True, separators=(",", ":"), allow_nan=False)+"\n")
    print(json.dumps(result["summary"]))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError) as error:
        print(f"spread analysis failed: {error}", file=sys.stderr); sys.exit(1)
