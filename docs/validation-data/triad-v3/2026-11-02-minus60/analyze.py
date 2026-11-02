#!/usr/bin/env python3
"""Recount frozen minus-60 studies: --root EVIDENCE --output analysis.json.

Requires NumPy and SciPy. Only unique chunks of a predeclared holdout cohort and
method are joined. Missing arrays are explicit; hashes and counts must agree.
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

SCHEMAS = {"triad-static-frequency-study-v1", "triad-cfo-batch-study-v1",
           "triad-rbpf-study-v1", "triad-noise-study-v1"}
STRICT = 22.721063389585737
RELAXED = 17.4227460230377
SEEDS = tuple(x + "_seed" for x in ("key", "channel", "oscillator", "noise", "frontend", "path"))


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for data in iter(lambda: f.read(1024 * 1024), b""):
            h.update(data)
    return h.hexdigest()


def canonical_sha(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def read(path):
    def reject(x):
        raise ValueError(f"nonfinite JSON: {path.name}: {x}")
    return json.loads(path.read_text(), parse_constant=reject)


def resolve(source, keys):
    parts = Path(source).parts
    matches = [k for k in keys if parts[-len(Path(k).parts):] == Path(k).parts]
    if not matches:
        return None
    longest = max(len(Path(k).parts) for k in matches)
    matches = [k for k in matches if len(Path(k).parts) == longest]
    require(len(matches) == 1, f"ambiguous workspace: {source}")
    return matches[0]


def cp(k, n, alpha=.05):
    require(0 <= k <= n, "invalid CP counts")
    return {"successes": k, "trials": n, "fraction": k/n if n else None,
            "lower": float(beta.ppf(alpha, k, n-k+1)) if k else 0.,
            "upper": float(beta.ppf(1-alpha, k+1, n-k)) if k < n else 1.}


def classify(values, truth, control, threshold):
    scores = np.asarray(values, dtype=np.float64)
    require(scores.shape == (1, 2) and np.isfinite(scores).all(), "expected one actual two-bit score row")
    winner = int(np.argmax(scores[0])); admitted = bool(scores[0, winner] >= threshold)
    if control == "signal":
        require(type(truth) is int and truth in (0, 1), "invalid signal evaluation truth")
        return {"captures": 1, "correct_accepted": int(admitted and winner == truth),
                "wrong_accepted": int(admitted and winner != truth), "missed": int(not admitted),
                "raw_correct": int(winner == truth)}
    require(control in ("null", "wrong-key"), f"unsupported control: {control}")
    return {"captures": 1, "false_admitted": int(admitted), "not_admitted": int(not admitted)}


def summarize(rows, gate, control, allow_qualification=True):
    completed = [r for r in rows if not r["failed"]]
    failed = [r for r in rows if r["failed"]]
    counts = Counter()
    for row in completed:
        counts.update(row[gate])
    result = {"completed_counts": dict(counts), "failed_attempts": len(failed)}
    if control != "signal":
        result["per_capture_false_admission_cp_95"] = cp(counts["false_admitted"], len(completed))
        result["operational_false_admission_empirically_qualified"] = False
        return result
    qualification = counts.copy(); qualification["captures"] += len(failed); qualification["missed"] += len(failed)
    result["qualification_counts_failed_as_missed"] = dict(qualification)
    result["pooled_correct_cp_95_if_iid"] = cp(counts["correct_accepted"], len(rows))
    strata = {}
    for bit in (0, 1):
        subset = [r for r in rows if r["truth"] == bit]
        success = sum(not r["failed"] and r[gate]["correct_accepted"] for r in subset)
        wrong = sum(not r["failed"] and r[gate]["wrong_accepted"] for r in subset)
        strata[str(bit)] = {"correct_cp_95": cp(success, len(subset)), "wrong_cp_95": cp(wrong, len(subset)),
                            "failed_as_missed": sum(r["failed"] for r in subset)}
    result["per_bit"] = strata
    n0, n1 = [strata[str(b)]["correct_cp_95"]["trials"] for b in (0, 1)]
    unknown = sum(r["truth"] not in (0, 1) for r in rows)
    balanced = n0 == n1 and n0 > 0 and not unknown
    established = allow_qualification and bool(rows) and all(r["independent_holdout"] for r in rows) and not unknown
    lower = None; rule = None
    if n0 and n1 and not unknown:
        if balanced and counts["correct_accepted"] == len(rows):
            lower = .05**(1/len(rows)); rule = "fixed-balanced all-success AM-GM"
        else:
            lower = sum(cp(strata[str(b)]["correct_cp_95"]["successes"],
                           strata[str(b)]["correct_cp_95"]["trials"], .025)["lower"] for b in (0, 1))/2
            rule = "equal-weight average of per-stratum 97.5% lower bounds (Bonferroni)"
    result["fixed_holdout_mean"] = {"balanced_bits": balanced, "unknown_truth_attempts": unknown,
                                    "six_seed_independence_and_holdout_split_verified": established,
                                    "pointwise_lower_95": lower, "rule": rule,
                                    "qualified_99": bool(established and lower is not None and lower >= .99),
                                    "qualified_90": bool(established and lower is not None and lower >= .9)}
    return result


def load_plan(root, explicit=None):
    paths = [Path(explicit)] if explicit else sorted(root.rglob("holdout_plan.json"))
    if not paths:
        fallback = Path(__file__).parent / "reproduction" / "holdout_plan.json"
        paths = [fallback] if fallback.is_file() else []
    if not paths:
        return None
    plans = [read(p) for p in paths]
    require(len({canonical_sha(p) for p in plans}) == 1, "conflicting holdout plans")
    plan = plans[0]
    require(plan["family"]["models"] == 16 and plan["family"]["durations"] == 8,
            "holdout plan family mismatch")
    require(len({j["name"] for j in plan["jobs"]}) == len(plan["jobs"]), "duplicate planned cohort")
    return {"document": plan, "sha256": sha(paths[0]), "filename": paths[0].name}


def metadata_file(root, name):
    paths = sorted(root.rglob(name))
    if not paths:
        path = Path(__file__).parent / "reproduction" / name
        paths = [path] if path.is_file() else []
    if not paths:
        return None
    documents = [read(p) for p in paths]
    require(len({canonical_sha(d) for d in documents}) == 1, f"conflicting {name}")
    return {"document": documents[0], "sha256": sha(paths[0]), "filename": name}


def argv_options(argv):
    require(isinstance(argv, list) and all(isinstance(x, str) for x in argv), "invalid command argv")
    options = {}; leading = []; current = None
    for token in argv:
        if token.startswith("--"):
            flag, equal, value = token.partition("=")
            require(flag not in options, f"duplicate command option {flag}")
            options[flag] = [value] if equal else []; current = flag
        elif current is None:
            leading.append(token)
        else:
            options[current].append(token)
    return leading, options


def numeric_equal(a, b):
    if isinstance(a, bool) or isinstance(b, bool):
        return type(a) is type(b) and a == b
    if isinstance(a, int) and isinstance(b, int):
        return a == b
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return math.isclose(a, b, rel_tol=1e-12, abs_tol=1e-12)
    return a == b


def option_equal(a, b):
    if len(a) != len(b):
        return False
    for x, y in zip(a, b):
        try:
            if Decimal(x) != Decimal(y):
                return False
        except InvalidOperation:
            if x != y:
                return False
    return True


def planned_seed(options, index, domain):
    one = lambda name: options[name][0]
    identity = ["triad-lowrate-campaign-v1", "holdout", float(one("--seconds")), float(one("--rate")),
                int(one("--sample-rate")), float(one("--cn0")), index, index % 2, domain]
    denominator = int(one("--sample-rate-denominator"))
    if denominator != 1:
        identity += ["sample-rate-denominator", denominator]
    return int.from_bytes(hashlib.sha256(json.dumps(identity, separators=(",", ":")).encode()).digest()[:8], "little")


def generation_binding(key, row, campaign, manifest, manifest_verified, job, receipt, generator):
    issues = []
    if not manifest_verified or manifest is None:
        return ["actual manifest is unavailable or not hash-verified against a receiver input receipt"]
    try:
        _, expected = argv_options(receipt["generator_export_command_template"])
        _, planned = argv_options(job["argv"])
        if planned.get("--split") != ["holdout"]: issues.append("generation plan is not the holdout split")
        for flag in set(planned) & set(expected) - {"--output"}:
            if not option_equal(planned[flag], expected[flag]): issues.append("export template differs from frozen generation plan at "+flag)
        index = row["path_index"]
        if Path(key).name != f"path-{index:05d}": issues.append("source path/index mismatch")
        expected["--signal-bit"] = [str(index % 2)]; expected["--epoch"] = [str(1_800_000_000 + 60*index)]
        for seed in SEEDS:
            value = planned_seed(expected, index, seed[:-5]); expected["--"+seed.replace("_", "-")] = [str(value)]
            if row.get(seed) != value: issues.append(f"source row {seed} is not the frozen split-derived root")
        if row.get("source_bit") != index % 2 or row.get("epoch") != 1_800_000_000+60*index:
            issues.append("source bit/epoch differs from frozen index scheme")
        if row.get("export_manifest") != manifest: issues.append("retained manifest differs from original export_manifest receipt")
        for label, command in (("source row command", row.get("command")), ("manifest command_argv", manifest.get("command_argv"))):
            try:
                leading, actual = argv_options(command)
                if len(leading) != 1 or Path(leading[0]).name != Path(receipt["generator_export_command_template"][0]).name:
                    issues.append(label+" executable does not match frozen generator")
                if set(actual) != set(expected): issues.append(label+" option set differs from complete frozen export command")
                for flag in expected:
                    if flag == "--output":
                        if flag not in actual or len(actual[flag]) != 1 or resolve(actual[flag][0], {key}) != key:
                            issues.append(label+" output does not identify this physical path")
                    elif flag not in actual or not option_equal(actual[flag], expected[flag]):
                        issues.append(label+" differs at "+flag)
            except (ValueError, KeyError, TypeError) as error:
                issues.append(label+": "+str(error))
        files = campaign.get("source_inputs", {}).get("files")
        expected_files = generator["source_inputs"]["files"]
        if files != expected_files: issues.append("generator source fingerprint differs from binding receipt")
        if files is None or campaign.get("source_inputs", {}).get("sha256") != hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest():
            issues.append("generator source fingerprint digest unavailable or inconsistent")
        if expected_files.get("tools/triad-lowrate-campaign.py") != generator.get("driver_sha256"):
            issues.append("frozen generation driver hash is not bound to source fingerprint")
        if not generator.get("binary_sha256") or campaign.get("binary_sha256") != generator["binary_sha256"]:
            issues.append("export binary hash is unavailable or differs from bound binary")
        value = lambda flag: float(expected[flag][0])
        integer = lambda flag: int(expected[flag][0])
        fs = value("--sample-rate")/value("--sample-rate-denominator")
        fields = {
            "requested_active_seconds": value("--seconds"), "cn0_db_hz": value("--cn0"),
            "sample_rate": fs, "sample_rate_numerator": integer("--sample-rate"),
            "sample_rate_denominator": integer("--sample-rate-denominator"),
            "block_samples": round(value("--block-seconds")*fs),
            "noise_variance": fs/10**(value("--cn0")/10), "signal_bit": index % 2,
            "epoch": integer("--epoch"), "key_seed": integer("--key-seed"),
            "channel_seed": integer("--channel-seed"), "control": expected["--control"][0],
            "channel": expected["--spread-preset"][0], "signal_enabled": True, "stats_noisy": True,
            "gaussian_projection_supported": True,
            "source_format.inner_rate_hz": value("--rate"), "source_format.added_symbols": integer("--added"),
            "source_format.encrypted": True, "source_format.added_interleave": True,
            "source_format.dwell_seconds": .4, "source_format.quiet_seconds": 0.,
            "source_format.ramp_seconds": .02, "source_format.margin_seconds": .005,
            "source_format.minimum_inner_coefficients": 64,
            "source_format.fine_rate_hz": value("--rate")/6,
            "channel_model.rf_hz": value("--rf-hz"), "channel_model.signal_gain": 1.,
            "channel_model.spread_delay_ms": .005, "channel_model.spread_doppler_hz": .001,
            "channel_model.perturbation_slow_depth": 0., "channel_model.perturbation_fast_depth": 0.,
            "channel_model.physical_rf_retuning": False, "sampled_frontend.active": False,
            "resolved_configuration.base_oscillator": expected["--base-oscillator"][0],
            "resolved_configuration.shift_oscillator": expected["--shift-oscillator"][0],
            "resolved_configuration.clock_sync": expected["--clock-sync"][0],
            "resolved_configuration.audio_error_seconds_per_station": value("--audio-error-ms")/1000,
            "resolved_configuration.margin": value("--margin"),
            "resolved_configuration.selective_perturbation": expected["--selective-perturbation"][0],
            "resolved_configuration.receiver_pattern_epoch": integer("--epoch"),
            "resolved_configuration.transmitter_pattern_epoch": integer("--epoch"),
            "resolved_configuration.whole_bit_symbol_burst.enabled": False,
            "oscillator_model.relative_phase_diffusion_rad_sqrt_s": .00012341341494884352,
            "oscillator_model.relative_white_fm_hz_sqrt_s": 0.,
            "oscillator_model.relative_random_walk_fm_hz_sqrt_s": 0.,
            "oscillator_model.relative_flicker_fm_rms_hz": 0., "oscillator_model.relative_white_pm_rms_rad": 0.}
        for domain in ("channel", "oscillator", "noise", "frontend", "path"):
            fields["random_seed_roots."+domain] = integer("--"+domain+"-seed")
        for name, flag in (("ray_coherence_seconds", "--ray-coherence-seconds"),
                           ("diurnal_phase_amplitude_radians", "--diurnal-phase-radians"),
                           ("diurnal_period_seconds", "--diurnal-period-seconds"),
                           ("outage_gain", "--outage-gain"), ("outage_ramp_seconds", "--outage-ramp-seconds")):
            for parent in ("channel_model", "resolved_configuration"):
                fields[parent+".path_evolution."+name] = value(flag)
        for i, name in enumerate(("outage_start_seconds", "outage_end_seconds")):
            for parent in ("channel_model", "resolved_configuration"):
                fields[parent+".path_evolution."+name] = float(expected["--outage-seconds"][i])
        for dotted, wanted in fields.items():
            actual = manifest
            for part in dotted.split("."):
                actual = actual.get(part) if isinstance(actual, dict) else None
            if not numeric_equal(actual, wanted): issues.append("actual manifest field differs: "+dotted)
        starts = [float(x) for x in expected["--start"]]
        epochs = [int(x) for x in expected["--epoch-offsets"][0].split(",")]
        for bit in (0, 1):
            for epoch in epochs:
                for i in range(math.floor((starts[1]-starts[0])/starts[2]+1e-9)+1):
                    arrival = manifest["capture_lead_seconds"]+starts[0]+i*starts[2]
                    candidates = [c for c in manifest.get("candidates", []) if c.get("bit") == bit and c.get("epoch_offset_seconds") == epoch
                                  and numeric_equal(c.get("arrival_seconds"), arrival) and c.get("tap_count") == 2]
                    wanted = [{"delay_seconds": 0, "doppler_hz": 0},
                              {"delay_seconds": value("--tap-delay-ms")/1000, "doppler_hz": value("--tap-doppler-hz")}]
                    if not any(len(c.get("taps", [])) == 2 and all(numeric_equal(t.get(k), w[k]) for t,w in zip(c["taps"],wanted) for k in w)
                               and all(numeric_equal(c.get(k), 0) for k in ("frequency_hz", "clock_ppm", "frequency_drift_hz_per_second")) for c in candidates):
                        issues.append("actual manifest lacks frozen two-ray timing/epoch/bit candidate")
    except (KeyError, TypeError, ValueError, IndexError) as error:
        issues.append("generation binding unavailable: "+str(error))
    return sorted(set(issues))


def runtime_bindings(root, plan, binding, manifests, source_rows, campaign_documents, report_documents, file_checks):
    result = {"generation": {}, "receivers": {}, "cohorts": {}}
    if plan is None or binding is None:
        return result
    bound = binding["document"]
    if bound.get("schema") != "triad-minus60-receiver-bindings-v1":
        return result
    for job in plan["document"]["jobs"]:
        name = job["name"]; receipt = bound.get("cohorts", {}).get(name)
        if receipt is None:
            result["cohorts"][name] = ["cohort has no receiver binding receipt"]; continue
        issues = []
        if receipt.get("generation_plan_sha256") != plan["sha256"]: issues.append("generation plan hash differs from binding receipt")
        score_plan = metadata_file(root, name+"_score_plan.json")
        if score_plan is None or receipt.get("score_plan_sha256") != score_plan["sha256"]:
            issues.append("frozen score plan unavailable or hash differs")
        result["cohorts"][name] = issues
        _, options = argv_options(job["argv"])
        cohort = resolve(options["--output"][0], set(campaign_documents))
        for key, (source, _) in source_rows.items():
            if str(Path(key).parent) != cohort: continue
            manifest = manifests[key][1] if key in manifests else None
            verified = file_checks.get((key, "manifest.json"), {}).get("state") == "verified"
            result["generation"][key] = generation_binding(key, source, campaign_documents[cohort], manifest,
                                                            verified, job, receipt, bound.get("generator", {}))
        for report_name, document in report_documents.items():
            report_inputs = [resolve(c["input"], set(source_rows)) for c in document["captures"]]
            if not any(key is not None and str(Path(key).parent) == cohort for key in report_inputs): continue
            errors = list(issues)
            for field, expected in receipt.get("expected_report_fields", {}).items():
                if document.get(field) != expected: errors.append("receiver differs from frozen field: "+field)
            if not receipt.get("expected_report_fields", {}).get("source_sha256"):
                errors.append("receiver source hash binding unavailable")
            jobs = []
            if score_plan:
                for score_job in score_plan["document"]["jobs"]:
                    leading, score_options = argv_options(score_job["argv"])
                    if Path(score_options["--output"][0]).name == Path(report_name).name:
                        jobs.append((leading, score_options))
            if len(jobs) != 1:
                errors.append("report does not identify one frozen score-plan command")
            else:
                leading, score_options = jobs[0]
                expected_inputs = [resolve(p, set(source_rows)) for p in leading[2:]]
                if None in expected_inputs or report_inputs != expected_inputs:
                    errors.append("report physical input slice differs from frozen score command")
                tool_names = {"triad-static-frequency-study-v1": "triad-static-frequency-study.py",
                              "triad-cfo-batch-study-v1": "triad-cfo-batch-study.py",
                              "triad-rbpf-study-v1": "triad-rbpf-study.py", "triad-noise-study-v1": "triad-noise-study.py"}
                if len(leading) < 2 or Path(leading[1]).name != tool_names.get(document.get("schema")):
                    errors.append("frozen scoring command tool differs from report schema")
                for flag, values in score_options.items():
                    if flag == "--output": continue
                    key = flag[2:].replace("-", "_")
                    config = document.get("configuration", {})
                    if key not in config:
                        errors.append("unresolved frozen scoring command option: "+flag); continue
                    actual = config[key]
                    actual_values = [] if actual is True and not values else [str(x) for x in actual] if isinstance(actual, list) else [str(actual)]
                    # Receiver configuration serializes infinite coherence as text.
                    actual_values = ["inf" if v == "infinity" else v for v in actual_values]
                    if not option_equal(values, actual_values):
                        errors.append("receiver configuration differs from score command at "+flag)
            result["receivers"][(name, report_name)] = sorted(set(errors))
    return result


def holdout_totals(plan, collected, campaigns, source_rows, runtime=None):
    if plan is None:
        return []
    results = []; runtime = runtime or {"generation": {}, "receivers": {}, "cohorts": {}}
    for job in plan["document"]["jobs"]:
        argv = job["argv"]
        option = lambda flag, default=None: argv[argv.index(flag)+1] if flag in argv else default
        count = int(option("--paths")); start = int(option("--start-index", "0"))
        require(count > 0 and count % 2 == 0, "holdout plan must declare positive balanced path count")
        cohort = resolve(option("--output"), set(campaigns) | {k[0] for k in collected})
        campaign = campaigns.get(cohort, {})
        expected = {str(Path(cohort)/f"path-{i:05d}") for i in range(start, start+count)} if cohort is not None else set()
        parameter_mismatches = []
        for flag in ("--paths", "--seconds", "--rate", "--sample-rate", "--sample-rate-denominator",
                     "--cn0", "--ray-coherence-seconds", "--block-seconds"):
            declared = option(flag)
            if declared is not None:
                actual = campaign.get("arguments", {}).get(flag[2:].replace("-", "_"))
                if actual is None or not math.isclose(float(actual), float(declared), rel_tol=1e-12, abs_tol=1e-12):
                    parameter_mismatches.append({"parameter": flag, "declared": declared, "actual": actual})
        candidates = {k: v for k, v in collected.items() if k[0] == cohort and k[1] == "holdout"}
        methods = {(k[2], k[3]) for k in candidates}
        methods.add((job["primary_method"], "signal"))
        for method, control in sorted(methods):
            rows = candidates.get((cohort, "holdout", method, control), [])
            occurrences = defaultdict(list)
            for row in rows:
                occurrences[row["input"]].append(row)
            duplicates = [{"input": key, "attempts": [{"report": r["report"], "failed": r["failed"]} for r in values]}
                          for key, values in sorted(occurrences.items()) if len(values) > 1]
            observed = set(occurrences)
            missing = sorted(expected-observed) if cohort is not None else []
            extra = sorted(observed-expected)
            coverage = cohort is not None and observed == expected and len(rows) == count and not duplicates
            signatures = {r["receiver_signature"] for r in rows}
            source_bits = [source_rows.get(key, ({}, {}))[0].get("source_bit") for key in expected]
            balanced = (len(source_bits) == count and source_bits.count(0) == source_bits.count(1) == count//2)
            complete = (coverage and balanced and not parameter_mismatches and len(signatures) == 1
                        and campaign.get("independence_verified", False) and campaign.get("complete_export_population", False))
            cohort_binding_errors = runtime["cohorts"].get(job["name"], ["frozen runtime binding unavailable"])
            generation_errors = [{"input": key, "issues": runtime["generation"].get(key, ["verified generation binding unavailable"])}
                                 for key in sorted(observed) if runtime["generation"].get(key, ["unavailable"])]
            receiver_errors = [{"report": report, "issues": runtime["receivers"].get((job["name"], report), ["frozen receiver binding unavailable"])}
                               for report in sorted({r["report"] for r in rows})
                               if runtime["receivers"].get((job["name"], report), ["unavailable"])]
            complete = complete and not cohort_binding_errors and not generation_errors and not receiver_errors
            blockers = []
            if not coverage: blockers.append("incomplete or duplicate predeclared physical path set")
            if not balanced: blockers.append("predeclared source-bit balance unavailable or invalid")
            if parameter_mismatches: blockers.append("campaign parameters do not match frozen plan")
            if len(signatures) != 1: blockers.append("receiver configuration/source differs across chunks or no receiver rows")
            if not campaign.get("independence_verified", False): blockers.append("six-root seed independence not verified")
            if not campaign.get("complete_export_population", False): blockers.append("source export population incomplete or unavailable")
            if cohort_binding_errors: blockers.append("frozen binding plan unavailable or inconsistent")
            if generation_errors: blockers.append("actual generation command/manifest/source binding incomplete or mismatched")
            if receiver_errors: blockers.append("receiver report is not bound to frozen scoring command/configuration/source")
            primary = method == job["primary_method"]
            if not primary: blockers.append("not the predeclared primary method; pointwise diagnostic only")
            results.append({"plan_cohort": job["name"], "cohort": cohort, "method": method, "control": control,
                            "predeclared_primary_method": method == job["primary_method"],
                            "expected_paths": count, "receiver_attempt_records": len(rows), "unique_physical_paths": len(observed),
                            "full_predeclared_population_verified": complete, "source_bits_balanced": balanced,
                            "missing_paths": missing, "extra_paths": extra, "duplicate_inputs": duplicates,
                            "plan_parameter_mismatches": parameter_mismatches, "qualification_blockers": blockers,
                            "cohort_binding_errors": cohort_binding_errors, "generation_binding_failures": generation_errors,
                            "receiver_binding_failures": receiver_errors,
                            "source_reports": sorted({r["report"] for r in rows}),
                            "strict": summarize(rows, "strict", control, complete and primary) if not duplicates else None,
                            "relaxed": summarize(rows, "relaxed", control, complete and primary) if not duplicates else None})
    return results


def analyze(root, plan_path=None):
    root = root.resolve(); rel = lambda p: p.relative_to(root).as_posix()
    docs = {p: read(p) for p in sorted(root.rglob("*.json"))}
    plan = load_plan(root, plan_path)
    binding = metadata_file(root, "receiver_bindings.json")
    manifests = {}; campaigns = {}; source_rows = {}; campaign_outputs = []
    campaign_documents = {}; report_documents = {}
    for path, doc in docs.items():
        if not isinstance(doc, dict):
            continue
        if doc.get("format") == "triad-block-stats-v1":
            manifests[rel(path.parent)] = (path, doc)
        if doc.get("format") == "triad-lowrate-campaign-v1":
            key = rel(path.parent); rows = doc["results"]
            campaign_documents[key] = doc
            ids = [r.get("path_index") for r in rows]
            duplicate_ids = len(ids)-len(set(ids))
            seed_stats = {s: {"present": sum(s in r for r in rows),
                              "unique": len({r[s] for r in rows if s in r})} for s in SEEDS}
            all_seeds = [r[s] for r in rows for s in SEEDS if s in r]
            valid_bits = all(type(r.get("source_bit")) is int and r["source_bit"] in (0, 1) for r in rows)
            independent = (not duplicate_ids and all(v["present"] == v["unique"] == len(rows) for v in seed_stats.values())
                           and len(all_seeds) == len(set(all_seeds)) and valid_bits)
            audit = {"campaign": rel(path), "sha256": sha(path), "split": doc["arguments"].get("split", "unknown"),
                     "arguments": {k: v for k, v in doc["arguments"].items() if k not in ("binary", "output")},
                     "result_rows": len(rows), "duplicate_path_ids": duplicate_ids, "six_seed_roots": seed_stats,
                     "cross_domain_seed_duplicates": len(all_seeds)-len(set(all_seeds)), "independence_verified": independent,
                     "binary_source_bits_verified": valid_bits,
                     "complete_export_population": (doc.get("paths_requested") == doc.get("paths_succeeded") == len(rows)
                                                     and not doc.get("paths_not_started") and not doc.get("paths_start_unknown")),
                     "source_bit_counts": dict(Counter(str(r.get("source_bit")) for r in rows)),
                     "accounting": {k: doc.get(k) for k in ("paths_requested", "paths_attempted", "paths_succeeded", "paths_not_started", "paths_start_unknown")}}
            campaign_outputs.append(audit); campaigns[key] = audit
            for row in rows:
                directory = row.get("directory", f"path-{row['path_index']:05d}")
                workspace = str(Path(key)/directory)
                require(workspace not in source_rows, f"duplicate campaign workspace: {workspace}")
                source_rows[workspace] = (row, audit)
    workspace_keys = set(manifests) | set(source_rows)
    file_checks = {}; expectations = {}; energies = {}; reports = []; failures = []
    collected = defaultdict(list); unknown_schemas = []

    def verify_input(key, hashes, display):
        require(isinstance(hashes, dict), "invalid input hashes")
        states = {}
        for name, expected in hashes.items():
            require(isinstance(name, str) and Path(name).name == name and name not in (".", ".."), "invalid input filename")
            require(isinstance(expected, str) and len(expected) == 64 and all(c in "0123456789abcdef" for c in expected), "invalid expected hash")
            ident = (key if key is not None else display, name)
            require(ident not in expectations or expectations[ident] == expected, f"conflicting input hashes: {ident}")
            expectations[ident] = expected
            if ident not in file_checks:
                if key is None:
                    file_checks[ident] = {"state": "workspace_unavailable_not_verified", "expected_sha256": expected}
                    states[name] = file_checks[ident]
                    continue
                path = root/key/name
                require(path.resolve().is_relative_to(root), f"input escapes root: {ident}")
                if path.is_file():
                    actual = sha(path); require(actual == expected, f"input hash mismatch: {ident}")
                    file_checks[ident] = {"state": "verified", "sha256": actual, "bytes": path.stat().st_size}
                else:
                    file_checks[ident] = {"state": "missing_not_verified", "expected_sha256": expected}
            states[name] = file_checks[ident]
        return states

    def energy(key):
        if key in energies or key not in manifests:
            return
        path, manifest = manifests[key]; fs = manifest["sample_rate"]
        entry = {k: manifest.get(k) for k in ("requested_active_seconds", "actual_elapsed_seconds", "nominal_payload_energy_seconds_lower", "cn0_db_hz")}
        entry["sampled_seconds"] = manifest["sample_count"]/fs
        noise = manifest.get("noise_variance")
        entry["noise_basis"] = "manifest noise_variance"
        if noise is None and "cn0_db_hz" in manifest:
            noise = fs/10**(manifest["cn0_db_hz"]/10)
            entry["noise_basis"] = "Fs / total-triplet Cref, Cref=10^(manifest C/N0 /10), normalized total transmit power=1"
        require(noise is None or math.isfinite(noise) and noise > 0, "invalid energy noise variance")
        epath = path.parent/manifest.get("signal_energy_file", "signal_energy.f64")
        actual = None
        if epath.is_file():
            require(epath.resolve().is_relative_to(root), "energy file escapes root")
            array = np.fromfile(epath, dtype="<f8")
            require(len(array) == manifest["block_count"] and np.isfinite(array).all() and (array >= 0).all(), "invalid signal energy array")
            actual = math.fsum(map(float, array)); entry["signal_energy_array"] = {"state": "present_evaluation_only", "actual_sha256": sha(epath)}
        else:
            entry["signal_energy_array"] = {"state": "missing_not_verified"}
        if actual is None:
            actual = manifest.get("channel_signal_energy_samples"); entry["energy_basis"] = "manifest channel_signal_energy_samples; array unavailable"
        else:
            entry["energy_basis"] = "sum of retained per-block signal_energy; evaluation only"
        entry["signal_energy_samples"] = actual
        entry["signal_energy_seconds"] = actual/fs if actual is not None else None
        entry["signal_energy_over_n0"] = actual/noise if actual is not None and noise else None
        energies[key] = entry

    for path, doc in docs.items():
        if not isinstance(doc, dict) or doc.get("schema") not in SCHEMAS:
            if isinstance(doc, dict) and doc.get("schema") and doc["schema"] != "triad-minus60-analysis-v1":
                unknown_schemas.append({"report": rel(path), "schema": doc["schema"], "sha256": sha(path),
                                        "capture_records": len(doc.get("captures", [])), "not_scored": True})
            continue
        schema = doc["schema"]; config = doc["configuration"]
        report_documents[rel(path)] = doc
        require(schema != "triad-noise-study-v1" or config.get("mode") == "actual", "projected trials cannot qualify")
        for field, expected in {"selection_methods": 16, "duration_alternatives": 8, "calibration_alternatives": 2, "false_probability": .0001,
                                "horizon_seconds": 86400, "attempt_spacing": 60}.items():
            require(math.isclose(config[field], expected, rel_tol=0, abs_tol=1e-10), f"wrong declared family {field}: {rel(path)}")
        methods = ["cfo-grid-ucb"] if schema == "triad-cfo-batch-study-v1" else config["methods"]
        semantic_config = {k: v for k, v in config.items() if k not in
                           ("device", "threads", "memory_gib", "memory_limit_bytes", "batch_size", "projection_tile_blocks")}
        signature = canonical_sha({"configuration": semantic_config, "source": doc.get("source_sha256"),
                                   "dependencies": doc.get("dependency_sha256"),
                                   "workspace_source": doc.get("workspace_source_sha256"),
                                   "noise_likelihood_source": doc.get("noise_likelihood_source_sha256")})
        scorepath = path.parent/Path(doc["scores_file"]).name
        archive = None
        if scorepath.is_file():
            require(sha(scorepath) == doc["scores_sha256"], f"score archive hash mismatch: {rel(path)}")
            archive = np.load(scorepath, allow_pickle=False)
        groups = defaultdict(list); seen = set(); seen_indices = set(); used = set(); input_fingerprints = []; scored = 0; failed = 0
        try:
            for capture in doc["captures"]:
                key = resolve(capture["input"], workspace_keys)
                display = key if key is not None else capture["input"]
                require(display not in seen, f"duplicate physical input within report: {display}"); seen.add(display)
                require(capture["physical_path_index"] not in seen_indices, "duplicate physical_path_index within report")
                seen_indices.add(capture["physical_path_index"])
                manifest = manifests[key][1] if key in manifests else None
                source, campaign = source_rows.get(key, ({}, {}))
                control = capture["control"]; require(control in ("signal", "null", "wrong-key"), "invalid control")
                if manifest:
                    require(manifest.get("control", "signal") == control, f"control mismatch: {display}")
                    for seed in SEEDS:
                        if seed in manifest and seed in source:
                            require(manifest[seed] == source[seed], f"manifest/campaign seed mismatch: {display} {seed}")
                is_failed = capture["status"] != "completed"
                hashes = capture.get("input_hashes", {}).copy()
                if capture.get("manifest_sha256"):
                    require("manifest.json" not in hashes or hashes["manifest.json"] == capture["manifest_sha256"], "manifest hash aliases conflict")
                    hashes["manifest.json"] = capture["manifest_sha256"]
                require(is_failed or "manifest.json" in hashes, f"completed capture missing manifest hash: {display}")
                states = verify_input(key, hashes, display)
                input_fingerprints.append({"input": display, "input_hashes": hashes})
                truth = None
                if control == "signal":
                    truth = capture.get("truth_bit_simulation_only")
                    expected_truth = source.get("source_bit", manifest.get("signal_bit_simulation_truth", manifest.get("signal_bit")) if manifest else None)
                    if is_failed:
                        truth = expected_truth if type(expected_truth) is int and expected_truth in (0, 1) else None
                    elif expected_truth is not None:
                        require(truth == expected_truth, f"evaluation truth mismatch: {display}")
                if key is not None:
                    energy(key)
                if is_failed:
                    require(not capture["methods"], f"partial failed scores require reconciliation: {display}")
                    failed += 1; failures.append({"report": rel(path), "input": display, "control": control,
                                                "error_type": capture.get("error_type"), "error": capture.get("error"),
                                                "manifest_hash_state": states.get("manifest.json", {"state": "unavailable"}),
                                                "signal_qualification": "missed" if control == "signal" else "excluded_unscored_control"})
                else:
                    require(set(capture["methods"]) == set(methods), f"incomplete method results: {display}")
                    require(capture.get("conditional_noise_trials", 1) == 1, "conditional replicas cannot qualify")
                    scored += 1
                cohort = str(Path(key).parent) if key is not None else str(Path(display).parent)
                split = campaign.get("split", "holdout_unverified" if "holdout" in display else "development_or_unknown")
                for method in methods:
                    row = {"input": display, "truth": truth, "failed": is_failed,
                           "report": rel(path), "receiver_signature": signature,
                           "independent_holdout": (split == "holdout" and campaign.get("independence_verified", False)
                                                   and campaign.get("complete_export_population", False) and bool(source))}
                    if not is_failed:
                        detail = capture["methods"][method]; gate = detail["threshold"]
                        if isinstance(gate, dict):
                            gate = gate["threshold_log_likelihood_ratio"]
                        require(math.isclose(gate, STRICT, rel_tol=0, abs_tol=1e-10), "incorrect frozen strict gate")
                        archive_key = f"path{capture['physical_path_index']:05d}_{control}_{method}"
                        inline = detail.get("bit_log_scores")
                        if archive is not None:
                            require(archive_key in archive, f"missing archived score: {archive_key}")
                            values = archive[archive_key]; used.add(archive_key)
                            if inline is not None:
                                require(np.array_equal(values, np.asarray([inline])), "inline/archive score disagreement")
                        else:
                            require(inline is not None, "neither inline nor archived scores available")
                            values = np.asarray([inline])
                        row["strict"] = classify(values, truth, control, STRICT)
                        row["relaxed"] = classify(values, truth, control, RELAXED)
                        require(row["strict"] == detail["counts"], f"reported count disagreement: {display} {method}")
                    groups[(cohort, split, method, control)].append(row)
                    collected[(cohort, split, method, control)].append(row)
            if archive is not None:
                require(set(archive.files) == used, "unaccounted archived scores")
        finally:
            if archive is not None:
                archive.close()
        reports.append({"report": rel(path), "sha256": sha(path), "schema": schema, "configuration": config,
                        "source_sha256": doc.get("source_sha256"), "scores_sha256": doc["scores_sha256"],
                        "score_archive_state": "verified" if scorepath.is_file() else "missing_inline_only",
                        "input_hashes_fingerprint": canonical_sha(input_fingerprints),
                        "completed_capture_records": scored, "failed_capture_records": failed,
                        "groups": [{"cohort": k[0], "split": k[1], "method": k[2], "control": k[3],
                                    "qualification_scope": "chunk diagnostic only; use holdout_cohorts for fixed-plan qualification",
                                    **{g: summarize(v, g, k[3], False) for g in ("strict", "relaxed")}}
                                   for k, v in sorted(groups.items())]})
    require(reports, "no actual receiver reports found")
    runtime = runtime_bindings(root, plan, binding, manifests, source_rows, campaign_documents, report_documents, file_checks)
    return {"schema": "triad-minus60-analysis-v1", "analyzer_sha256": sha(Path(__file__)),
            "gates": {"strict": STRICT, "relaxed": RELAXED, "methods": 16, "durations": 8,
                      "bit_alternatives": 2, "attempt_spacing_seconds": 60,
                      "strict_alpha_horizon": [.0001, 86400], "relaxed_alpha_horizon": [.01, 43200],
                      "score_alpha_fraction": .5, "calibration": "Retains original strict UCB; relaxed re-gating does not refit calibration."},
            "confidence_scope": "Each CP endpoint is separately one-sided 95%. Per-bit bounds permit different bit probabilities; balanced all-success AM-GM and Bonferroni mean bounds assume fixed independent strata. Pointwise only: no method/cohort selection or sequential-stopping guarantee. Only unique scoring chunks of the same predeclared holdout cohort/method/control are joined. Full declared path coverage, bit balance, source/configuration consistency and six independent root-seed domains are required. Development variants and methods are never pooled. Failed signal attempts count as misses; unknown bits block qualification.",
            "campaign_scope": "Per-report and nonprimary results are diagnostics. Primary holdout qualification requires full fixed-plan coverage plus hash-verified actual manifests, matching per-path generation commands/seeds/physical fields/source/binary, and frozen scoring-command/configuration/source bindings. Missing raw arrays remain explicit and need not block manifest/runtime binding. No operational false-admission claim from sparse controls.",
            "energy_scope": "Signal energy and truth are evaluation-only; they do not modify frozen scores. Noise density uses manifest variance or the explicitly labeled total-triplet normalized-power derivation. Missing arrays are never hash-verified.",
            "reports": reports, "campaigns": campaign_outputs, "failed_capture_records": failures,
            "holdout_plan": {k: v for k, v in plan.items() if k != "document"} if plan else None,
            "receiver_binding_receipt": ({"sha256": binding["sha256"], "filename": binding["filename"],
                                          "scope": binding["document"].get("scope")} if binding else None),
            "holdout_cohorts": holdout_totals(plan, collected, campaigns, source_rows, runtime),
            "unknown_schemas": unknown_schemas,
            "workspace_energy": energies,
            "input_file_checks": [{"input": k[0], "filename": k[1], **v} for k, v in sorted(file_checks.items())],
            "summary": {"receiver_reports": len(reports), "completed_capture_records": sum(r["completed_capture_records"] for r in reports),
                        "unknown_schema_documents": len(unknown_schemas),
                        "failed_capture_records": len(failures), "verified_input_files": sum(v["state"] == "verified" for v in file_checks.values()),
                        "missing_input_files": sum(v["state"] != "verified" for v in file_checks.values())}}


def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--root", required=True, type=Path); parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--plan", type=Path, help="frozen holdout_plan.json; default: evidence tree, then adjacent reproduction directory")
    args = parser.parse_args(); require(args.root.is_dir(), "evidence root missing")
    if args.output.exists():
        require(read(args.output).get("schema") == "triad-minus60-analysis-v1", "refusing to overwrite another artifact")
    result = analyze(args.root, args.plan); args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, sort_keys=True, separators=(",", ":"), allow_nan=False)+"\n")
    print(json.dumps(result["summary"]))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError) as error:
        print(f"analysis failed: {error}", file=sys.stderr); sys.exit(1)
