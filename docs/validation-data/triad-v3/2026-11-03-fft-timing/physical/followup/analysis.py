#!/usr/bin/env python3
"""Recount retained FFT physical campaigns; never launch a receiver or simulator."""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path

import numpy as np
from scipy.stats import beta, norm


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for part in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(part)
    return h.hexdigest()


def read(path):
    return json.loads(Path(path).read_text())


def interval(k, n):
    """Two-sided 95% Clopper-Pearson interval, conditional on IID Bernoulli trials."""
    return [0. if k == 0 else float(beta.ppf(.025, k, n-k+1)),
            1. if k == n else float(beta.ppf(.975, k+1, n-k))]


def wilson(k, n):
    z = float(norm.ppf(.975))
    denominator = 1 + z*z/n
    middle = (k/n + z*z/(2*n))/denominator
    half = z*math.sqrt((k/n)*(1-k/n)/n + z*z/(4*n*n))/denominator
    return [max(0., middle-half), min(1., middle+half)]


def threshold(attempts, score_probability):
    return math.nextafter(sum(map(math.log, (attempts, 8, 16, 2))) -
                          math.log(score_probability), math.inf)


GATES = {"strict": threshold(1440, 5e-5), "relaxed": threshold(720, .005)}


def grid(low, high, step):
    if low == high:
        return [low]
    if low < 0 < high:
        return np.concatenate((np.linspace(low, 0., math.ceil(-low/step)+1),
                               np.linspace(0., high, math.ceil(high/step)+1)[1:])).tolist()
    return np.linspace(low, high, math.ceil((high-low)/step)+1).tolist()


def outcomes(values, gate, bit, control):
    winner = int(np.argmax(values))
    accepted = max(values) >= gate
    if control == "signal":
        return dict(correct_admitted=int(accepted and winner == bit),
                    wrong_admitted=int(accepted and winner != bit), missed=int(not accepted))
    return dict(false_admitted=int(accepted), not_admitted=int(not accepted))


def summarize(directory, plan_path, root):
    directory, plan_path = Path(directory), Path(plan_path)
    result_path = directory / "results.json"
    result, plan = read(result_path), read(plan_path)
    assert result["plan_sha256"] == sha(plan_path)
    assert result["source_file_sha256"] == plan["source_file_sha256"]
    assert result["native_binary_sha256"] == plan["native_binary_sha256"]
    assert result["source_and_binary_unchanged"] and result["identity_error"] is None
    declared = {trial["id"]: (row, trial) for row in plan["rows"] for trial in row["trials"]}
    records = result["records"]
    assert len(records) == len(declared) == result["selected_captures"] == result["planned_captures"]
    assert {r["id"] for r in records} == set(declared)
    assert result["counts"] == dict(Counter(r["status"] for r in records))
    provenance = {str(result_path): sha(result_path), str(plan_path): sha(plan_path)}
    preflight_path = plan_path.parent / "preflight.json"
    configure_receipts = read(preflight_path)
    provenance[str(preflight_path)] = sha(preflight_path)
    cells, compact = defaultdict(list), []
    raw_bytes = 0
    for record in sorted(records, key=lambda r: r["id"]):
        ident = record["id"]
        row, trial = declared[ident]
        record_path = directory / "records" / (ident + ".json")
        assert read(record_path) == record
        provenance[str(record_path)] = sha(record_path)
        assert record["status"] == "completed" and record["input_files_unchanged"]
        for stage in ("export", "score"):
            receipt = record[stage]
            status_path = directory / "logs" / (ident + "-" + stage + ".status.json")
            log_path = directory / "logs" / (ident + "-" + stage + ".log")
            assert sha(status_path) == receipt["status_sha256"]
            assert sha(log_path) == receipt["log_sha256"]
            assert read(status_path) == receipt["bounded_status"]
            assert receipt["argv_sha256"] == hashlib.sha256(
                json.dumps(receipt["argv"], separators=(",", ":")).encode()).hexdigest()
            status = receipt["bounded_status"]
            assert receipt["status"] == status["outcome"] == "completed"
            assert status["exit_code"] == status["child_exit_code"] == 0
            assert not status["cancelled"] and not status["timed_out"]
            assert status["termination"]["group_empty"]
            provenance[str(status_path)] = sha(status_path)
            provenance[str(log_path)] = sha(log_path)
        manifest_path = directory / "workspaces" / ident / "manifest.json"
        score_path = directory / "scores" / (ident + ".json")
        m, score = read(manifest_path), read(score_path)
        inputs = {x["name"]: x["sha256"] for x in record["input_files"]}
        assert sha(manifest_path) == inputs["manifest.json"]
        assert manifest_path.stat().st_size == next(x["bytes"] for x in record["input_files"] if x["name"] == "manifest.json")
        assert score["input_sha256"] == inputs and sha(score_path) == record["score_report_sha256"]
        assert score["receiver_source_sha256"] == result["source_file_sha256"]["tools/triad_fft_acquire.py"]
        assert all(value == result["source_file_sha256"]["tools/" + name]
                   for name, value in score["dependency_sha256"].items())
        for item in record["input_files"]:
            if item["name"] != "manifest.json":
                assert item["bytes"] == m["file_sizes"][item["name"]]
                raw_bytes += item["bytes"]
        provenance[str(manifest_path)] = sha(manifest_path)
        provenance[str(score_path)] = sha(score_path)
        g, cal, guard = score["geometry"], score["calibration"], record["actual_guard"]
        argv = record["score"]["argv"]
        start = argv.index("--frequency-offset-grid") + 1
        expected_cfo = grid(*map(float, argv[start:start+3]))
        assert g["frequency_offsets_hz"] == expected_cfo
        assert g["lag_samples"] == list(range(guard["timing_count"]))
        assert g["group_count"] == guard["effective_groups"] == len(expected_cfo) * guard["timing_count"]
        assert g["sample_count"] == m["sample_count"] == guard["sample_count"]
        assert g["sample_rate"] == m["sample_rate"] == guard["sample_rate"]
        assert g["fixed_clock_ppm"] == 0 and g["fixed_epoch_offset_seconds"] == 0
        assert g["drift_offsets_hz_per_second"] == [0.]
        assert sum(cal["rank_histogram"]) == g["group_count"]
        delta = math.nextafter(math.nextafter(5e-5/(1440*8*2), 0.)/g["group_count"], 0.)
        assert cal["delta_per_group"] == delta and cal["known_noise_variance_diagnostic"] is None
        assert not cal["inter_group_independence_assumed"]
        assert score["group_bit_log_likelihoods"] is None
        values = score["bit_log_likelihoods"]
        assert values == record["bit_log_likelihoods"] and all(map(math.isfinite, values))
        bit, control = trial["bit"], trial["control"]
        assert bit == m["signal_bit_simulation_truth"] == record["bit"]
        assert control == m["control"] == record["control"]
        decisions = {}
        for gate, value in GATES.items():
            assert score[gate]["log_threshold"] == record["gates"][gate] == value
            decisions[gate] = outcomes(values, value, bit, control)
            assert decisions[gate] == record["outcomes"][gate] == score["simulation_evaluation_only"]["outcomes"][gate]
            assert score[gate]["bit"] == int(np.argmax(values))
            assert score[gate]["accepted"] == (max(values) >= value)
        export = record["export"]["argv"]
        assert all(export[export.index("--"+name)+1] == str(value) for name, value in trial["seed_roots"].items())
        cfg = m["resolved_configuration"]
        erasures = cfg["receiver_observation"]["missed_dwell_indices"]
        assert not m["sampled_frontend"]["active"]
        energy = dict(reported_pre_erasure_signal_energy_samples=m["channel_signal_energy_samples"],
                      noise_variance_pre_frontend=m["noise_variance_pre_frontend"],
                      post_erasure_energy_available=not erasures,
                      rho=None, known_waveform_benchmarks=None)
        if control == "signal" and not erasures:
            rho = m["channel_signal_energy_samples"] / m["noise_variance_pre_frontend"]
            energy["rho"] = rho
            energy["known_waveform_benchmarks"] = {}
            for gate, value in GATES.items():
                alpha = math.exp(-value)
                z = float(norm.isf(alpha))
                required = .5*(z + norm.ppf(.99))**2
                target = .99 if gate == "strict" else .90
                required_target = .5*(z + norm.ppf(target))**2
                energy["known_waveform_benchmarks"][gate] = dict(
                    allocated_alpha=alpha, conditional_presence_pd=float(norm.cdf(math.sqrt(2*rho)-z)),
                    target_probability=target, rho_required_target=float(required_target),
                    energy_margin_above_genie_target_db=10*math.log10(rho/required_target),
                    rho_required_99_percent=float(required),
                    energy_margin_above_genie99_db=10*math.log10(rho/required))
        entry = dict(id=ident, case=record["case"], control=control, bit=bit,
                     seed_roots=trial["seed_roots"], bit_log_likelihoods=values,
                     outcomes=decisions, raw_bit_correct=int(np.argmax(values)) == bit,
                     input_files=record["input_files"], score_sha256=sha(score_path),
                     manifest_sha256=sha(manifest_path), actual_guard=guard,
                     requested_active_seconds=m["requested_active_seconds"],
                     actual_elapsed_seconds=m["actual_elapsed_seconds"],
                     observation_seconds=m["sample_count"] / m["sample_rate"],
                     duration=cfg["duration"], source_format=m["source_format"],
                     whole_bit_symbol_burst=cfg["whole_bit_symbol_burst"],
                     receiver_observation=cfg["receiver_observation"],
                     oscillator_model=m["oscillator_model"], channel_model=m["channel_model"],
                     energy=energy, rank_histogram=cal["rank_histogram"],
                     variance_upper_range=cal["variance_upper_over_reference_range"],
                     export_wall_seconds=record["export"]["elapsed_seconds"],
                     score_cli_wall_seconds=record["score"]["elapsed_seconds"],
                     sampled_observation_wall_seconds=m["sampled_observation_wall_seconds"],
                     basis_generation_wall_seconds=m["basis_generation_and_write_wall_seconds"],
                     score_timing=score["timing"], resources=score["resources"],
                     numerical_refinement=score["numerical_refinement"])
        cells[record["case"]].append(entry)
        compact.append(entry)
    summaries = []
    for case, entries in sorted(cells.items()):
        signals = [r for r in entries if r["control"] == "signal"]
        counts = {}
        for gate in GATES:
            counts[gate] = {}
            for control in ("signal", "null", "wrong-key"):
                keys = ("correct_admitted", "wrong_admitted", "missed") if control == "signal" else ("false_admitted", "not_admitted")
                counts[gate][control] = {key: sum(r["outcomes"][gate][key] for r in entries if r["control"] == control) for key in keys}
        common_names = ("duration", "source_format", "whole_bit_symbol_burst",
                        "receiver_observation", "oscillator_model", "channel_model")
        common = {name: entries[0][name] for name in common_names}
        assert all(all(r[name] == common[name] for name in common_names) for r in entries)
        summaries.append(dict(case=case, configuration=declared[entries[0]["id"]][0]["case"],
            common_physical_metadata=common,
            counts=counts, signal_count=len(signals), null_count=sum(r["control"] == "null" for r in entries), wrong_key_count=sum(r["control"] == "wrong-key" for r in entries),
            correct_reception_cp95={gate: interval(counts[gate]["signal"]["correct_admitted"], len(signals)) for gate in GATES},
            correct_reception_wilson95={gate: wilson(counts[gate]["signal"]["correct_admitted"], len(signals)) for gate in GATES},
            zero_control_one_sided_95_upper={control: 1-.05**(1/sum(r["control"] == control for r in entries)) for control in ("null", "wrong-key")},
            signal_scores=[max(r["bit_log_likelihoods"]) for r in signals],
            signal_rho=[r["energy"]["rho"] for r in signals],
            elapsed_source_seconds=signals[0]["actual_elapsed_seconds"],
            observation_seconds=signals[0]["observation_seconds"],
            sample_rate=signals[0]["actual_guard"]["sample_rate"],
            sample_count=signals[0]["actual_guard"]["sample_count"],
            groups=signals[0]["actual_guard"]["effective_groups"],
            export_wall_seconds=[r["export_wall_seconds"] for r in entries],
            score_cli_wall_seconds=[r["score_cli_wall_seconds"] for r in entries]))
        for entry in entries:
            for name in common_names:
                entry.pop(name)
    return dict(name=directory.name, results_sha256=sha(result_path), plan_sha256=sha(plan_path),
        source_file_sha256=result["source_file_sha256"], native_binary_sha256=result["native_binary_sha256"],
        source_and_binary_unchanged_recorded=result["source_and_binary_unchanged"],
        current_local_source_hash_matches={name: sha(root/name) == digest for name, digest in result["source_file_sha256"].items()},
        runner_sha256=result["runner_sha256"], bounded_runner_sha256=result["bounded_runner_sha256"],
        counts=result["counts"], captures=len(records), workers=result["workers"],
        unique_signal_seed_tuples=len({tuple(sorted(r["seed_roots"].items())) for r in compact if r["control"] == "signal"}),
        elapsed_seconds=result["elapsed_seconds"], preflight_rows=[dict(case=r["case"], status=r["status"],
            preflight=r.get("preflight"), configure_receipt_index=r.get("configure_receipt_index"),
            native_configure_result={name: configure_receipts[r["configure_receipt_index"]][name] for name in ("returncode", "stderr", "argv")}) for r in plan["rows"]],
        raw_data_bytes_bound_but_not_rehashed=raw_bytes, cell_summaries=summaries, captures_detail=compact,
        provenance_sha256=provenance)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--campaign", nargs=2, action="append", metavar=("RESULT_DIRECTORY", "PLAN"), required=True)
    p.add_argument("--repo", type=Path, default=Path.cwd())
    p.add_argument("--archive", type=Path)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    campaigns = [summarize(directory, plan, args.repo) for directory, plan in args.campaign]
    initial_path = args.repo / "docs/validation-data/triad-v3/2026-11-03-fft-timing/physical/summary.json"
    initial = read(initial_path)
    roots = lambda entries: {tuple(sorted(r["seed_roots"].items())) for r in entries if r["control"] == "signal"}
    old_roots = roots([r for c in initial["campaigns"] for r in c["captures_detail"]])
    current = campaigns[0]["captures_detail"]
    by_case = {name: roots([r for r in current if r["case"] == name]) for name in ("A850-best", "B425-best", "C850-moderate")}
    assert len(by_case["A850-best"]) == 32 and len(by_case["B425-best"]) == 16
    assert by_case["B425-best"] == by_case["C850-moderate"] <= by_case["A850-best"]
    assert not (by_case["A850-best"] & old_roots)
    result = dict(schema="triad-fft-physical-independent-review-v1", campaigns=campaigns, gates=GATES,
        analysis_source_sha256=sha(__file__),
        pairing_review=dict(initial_summary_sha256=sha(initial_path), A_32_signal_roots_disjoint_from_initial=True,
            B_and_C_share_same_16_root_subset_of_A=True, changed_sampling_does_not_preserve_same_continuous_random_path=True),
        later_reporting_only_variant=dict(path="tools/triad_fft_acquire.py", sha256=sha(args.repo/"tools/triad_fft_acquire.py"),
            scope="Coordinator identifies one arithmetic-report field added after all campaigns finished; no arithmetic change or rescoring. Historical f912 source binding remains the experiment identity."),
        archive=None if args.archive is None else dict(path=str(args.archive), bytes=args.archive.stat().st_size, sha256=sha(args.archive)),
        confidence_scope="Exact binomial intervals are conditional per-cell summaries, not IID pooling across paired roots/cells or operational qualification.",
        energy_scope="Sampled export energy is pre-erasure. With inactive frontend and no erasures, rho=reported signal energy/noise variance. Missed-hop cells retain only pre-erasure energy and receive no post-erasure genie estimate.",
        benchmark_scope="Known complete waveform, arrival, phase, channel and white Gaussian noise; NP presence Pd=Phi(sqrt(2rho)-isf(alpha)) with alpha=exp(-the same allocated score gate). Targets are strict99% and relaxed90%; separately named99% fields remain comparison diagnostics. This is an optimistic conditional presence benchmark at the chosen operational allocation, not a universal one-completion minimum, achieved correct-bit probability or an unknown-channel bound.",
        verification_scope="All available results, records, bounded statuses/logs, score reports, manifest hashes, declared geometry and gate decisions independently checked. Raw basis/observation/validity bytes are absent from this compact archive: their recorded hashes are cross-bound, not independently re-read. Full group scores were omitted, so mixtures and residual UCBs cannot be recomputed from compact reports alone.",
        study_scope="Separate selected followup: independent32-signal A;16-signal half-duration B and higher-spread/sampling C paired on A subsets. Do not pool with initial four-signal cells. Historical source f912 remains bound; post-campaign local FFT metadata-only variant is a separately recorded hash.")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True, allow_nan=False)+"\n")
    print(json.dumps(dict(output=str(args.output), captures=sum(c["captures"] for c in campaigns),
                         sha256=sha(args.output)), indent=2))


if __name__ == "__main__":
    main()
