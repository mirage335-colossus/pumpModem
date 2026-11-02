#!/usr/bin/env python3
"""Source-bound summary of accepted long audits and two Fs diagnostics.

This consumes completed audits; it does not replace their raw-array, command,
source, seed or runner validation. Only the two unscored Fs-refinement
workspaces undergo a new noiseless projection calculation here.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

import numpy as np
from scipy.stats import beta


HERE = Path(__file__).resolve().parent
REPO = HERE.parents[4]


def require(value, message):
    if not value:
        raise ValueError(message)


def read(path):
    return json.loads(Path(path).read_text())


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def cp(k, n, alpha=.05):
    require(type(k) is int and type(n) is int and 0 <= k <= n and n > 0, "invalid CP count")
    return {"successes": k, "trials": n, "fraction": k/n,
            "lower_one_sided_95": float(beta.ppf(alpha, k, n-k+1)) if k else 0.,
            "upper_one_sided_95": float(beta.ppf(1-alpha, k+1, n-k)) if k < n else 1.}


def balanced(strata, event):
    ns = [strata[str(b)]["counts"]["trials"] for b in (0, 1)]
    ks = [strata[str(b)]["counts"][event] for b in (0, 1)]
    require(ns[0] == ns[1] > 0, "AM-GM formula requires equal nonzero bit strata")
    lower = (.05**(1/sum(ns)) if ks == ns else
             sum(float(beta.ppf(.025, k, n-k+1)) if k else 0. for k, n in zip(ks, ns))/2)
    upper = (1-.05**(1/sum(ns)) if not any(ks) else
             sum(float(beta.ppf(.975, k+1, n-k)) if k < n else 1. for k, n in zip(ks, ns))/2)
    return {"event": event, "estimate": sum(ks)/sum(ns), "lower_one_sided_95": lower,
            "upper_one_sided_95": upper,
            "lower_method": "equal-stratum all-success AM-GM" if ks == ns else "Bonferroni per-bit CP",
            "upper_method": "equal-stratum all-failure AM-GM complement" if not any(ks) else "Bonferroni per-bit CP",
            "per_bit": {str(b): cp(ks[b], ns[b]) for b in (0, 1)}}


def local_source(source, evidence):
    p = Path(source)
    if p.is_dir():
        return p
    prefix = Path('/workspace/triad')
    require(p.is_relative_to(prefix), f"unknown recorded source prefix: {p}")
    return evidence/'remote-analysis-root'/p.relative_to(prefix)


def geometry(directory):
    m = read(directory/'manifest.json')
    ep = m['observation_endpoint_report']
    require(ep['processed_samples'] == m['sample_count'] == m['channel_samples'], "incomplete observed rows")
    require(ep['natural_complete_samples'] + ep['public_calendar_padding_samples'] == m['sample_count'],
            "incomplete natural endpoint or padding")
    fs = m['sample_rate']
    return {"manifest_sha256": sha(directory/'manifest.json'), "sample_rate_hz": fs,
            "sample_count": m['sample_count'], "capture_seconds": m['sample_count']/fs,
            "requested_active_seconds": m['requested_active_seconds'],
            "actual_source_elapsed_seconds": m['actual_elapsed_seconds'],
            "source_samples": m['source_samples'], "source_support_seconds": m['source_samples']/fs,
            "channel_tail_samples": m['channel_tail_samples'],
            "natural_complete_samples": ep['natural_complete_samples'],
            "public_calendar_padding_samples": ep['public_calendar_padding_samples']}


def energy_entry(entry, evidence):
    source = local_source(entry['source'], evidence)
    telemetry = entry['selective_stage_energy']
    post = entry['sampled_signal_energy']
    return {"source": str(source), "geometry": geometry(source),
            "post_over_pre_signal_energy_ratio": telemetry.get('post_over_pre_signal_energy_ratio') if telemetry.get('available') else None,
            "sampled_signal_energy_samples": post,
            "sampled_signal_energy_seconds": post/entry['sample_count']*entry['capture_seconds'],
            "correct_projection_fraction": entry['nominal_correct_projection_over_sampled_energy'],
            "wrong_projection_fraction": entry['nominal_wrong_projection_over_sampled_energy'],
            "correct_projection_over_pre_selective_energy": entry.get('nominal_correct_projection_over_pre_energy'),
            "wrong_projection_over_pre_selective_energy": entry.get('nominal_wrong_projection_over_pre_energy')}


def ranges(rows):
    keys = ('post_over_pre_signal_energy_ratio', 'correct_projection_fraction', 'wrong_projection_fraction',
            'correct_projection_over_pre_selective_energy', 'wrong_projection_over_pre_selective_energy')
    return {key: [min(values), max(values)] for key in keys
            if (values := [r[key] for r in rows if r[key] is not None])}


def score_summary(item):
    report_path = Path(item['report']); report = read(report_path)
    require(sha(report_path) == item['report_sha256'], "accepted score report changed")
    npz_path = report_path.parent/report['scores_file']
    require(sha(npz_path) == item['score_archive_sha256'] == report['scores_sha256'], "accepted scores changed")
    require(item['observation_mode'] == 'actual-noisy' and not item['failed_physical_paths'], "long summary needs completed actual paths")
    require(item['all_input_hash_receipts_reverified'], "accepted audit did not verify score input receipts")
    scores = []; thresholds = set()
    with np.load(npz_path, allow_pickle=False) as bank:
        for path in report['paths']:
            require(path['status'] == 'completed' and len(path['conditions']) == 1, "unexpected actual condition")
            condition = path['conditions'][0]
            require(list(condition['methods']) == ['static-fft-ucb'], "unexpected long detector")
            method = condition['methods']['static-fft-ucb']
            x = bank[method['score_array']]
            require(x.shape == (1, 2) and x.dtype == np.float64 and np.isfinite(x).all(), "invalid actual FP64 score pair")
            threshold = method['threshold']; thresholds.add(threshold)
            scores.append({"input": path['input'], "truth_bit": path['truth_bit_simulation_only'],
                           "scores": x[0].tolist(), "strict_best_score_margin_nats": float(x.max()-threshold)})
    rows = []
    for row in item['counts']:
        count = row['counts']; event = 'correct_accepted' if row['control'] == 'signal' else 'false_admitted'
        clean = {key: value for key, value in count.items() if isinstance(value, (int, float))}
        events = ('correct_accepted', 'wrong_accepted', 'missed') if row['control'] == 'signal' else ('false_admitted', 'not_admitted')
        rows.append({"gate": row['gate'], "control": row['control'], "cn0_db_hz": row['cn0_db_hz'],
                     "counts": clean, "equal_bit_population_event_bounds": balanced(count['per_bit'], event),
                     "equal_bit_population_outcome_bounds": {e: balanced(count['per_bit'], e) for e in events}})
    return {"name": item['name'], "model": item['model'], "modulation": item['modulation'],
            "report_sha256": item['report_sha256'], "score_npz_sha256": item['score_archive_sha256'],
            "method": 'static-fft-ucb', "strict_threshold_nats": sorted(thresholds),
            "counts": rows, "actual_score_pairs": scores}


def normalize_refinement_argv(argv):
    # These four differences were predeclared: executable/host, Fs denominator,
    # corresponding complete-tail floor, and output location. No other option
    # may change; the source discretization and stochastic paths still differ.
    argv = list(argv); argv[0] = 'EXECUTABLE'
    for option in ('--sample-rate-denominator', '--selective-tail-floor-seconds', '--output'):
        argv[argv.index(option)+1] = option
    return argv


def verify_audit_helper_bindings(audits):
    """Refuse helper reuse when it differs from any accepted input audit."""
    for audit in audits.values():
        recorded = audit['retention_source_bindings']
        require(sha(HERE/'analyze.py') == audit['analyzer_sha256'], 'accepted analyzer source changed')
        require(sha(HERE/'retention_batch.py') == recorded['batch_module_sha256'],
                'accepted batch-retention source changed')
        require(sha(HERE.parents[1]/'2026-11-02-selective/reproduction/retention.py') == recorded['scalar_reference_sha256'],
                'accepted scalar-retention reference changed')


def verify_staged_members(directory, stage_root, stage_files):
    relative_dir = directory.relative_to(stage_root)
    expected_members = {name for name in stage_files if Path(name).parent == relative_dir}
    actual_members = {str(file.relative_to(stage_root)) for file in directory.iterdir()}
    require(expected_members and actual_members == expected_members,
            'refinement directory members differ from accepted staged archive')
    inventory = []
    for relative in sorted(expected_members):
        file = stage_root/relative
        require(file.is_file(), 'unexpected refinement subdirectory')
        expected = stage_files[relative]
        require(expected['size'] == file.stat().st_size and expected['sha256'] == sha(file),
                'refinement file differs from accepted staged archive')
        inventory.append({'path': relative, 'sha256': expected['sha256'], 'size': expected['size']})
    return inventory


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--evidence', type=Path, default=REPO/'local-evidence/triad-selective-correction')
    ap.add_argument('--output', type=Path, default=HERE/'long-summary.json')
    args = ap.parse_args(); evidence = args.evidence.resolve()
    names = ('long-replay', 'remote-long', 'remote-controls')
    audits = {}; summaries = {}
    for name in names:
        path = evidence/'analysis'/f'{name}-analysis.json'; a = read(path)
        require(a['execution_receipts_complete'], "accepted audit lacks complete execution views")
        audits[name] = a
        cells = []
        for cell in a['cells']:
            rows = [energy_entry(row, evidence) for row in cell['energy_and_projection']]
            cells.append({"model": cell['model'], "modulation": cell['modulation'],
                          "source_count": cell['source_count'], "paths": rows, "energy_projection_ranges": ranges(rows)})
        summaries[name] = {"accepted_audit": str(path), "accepted_audit_sha256": sha(path),
                           "analyzer_sha256": a['analyzer_sha256'], "retention_source_bindings": a['retention_source_bindings'],
                           "workspace_count": a['workspace_count'], "paired_path_count": a['paired_path_count'],
                           "cells": cells, "receiver_counts": [score_summary(s) for s in a['receiver_score_summaries']]}

    # Compare the same four replay roots, without pooling them as independent
    # trials across models/depths or crediting energy gain as sensitivity gain.
    replay = summaries['long-replay']['cells']
    keyed = {(c['model'], c['modulation']): c for c in replay}
    off = {Path(r['source']).name: r for r in keyed['shared-off-bypass', 'off']['paths']}
    comparisons = []
    for depth in ('each-25', 'each-65'):
        legacy = {Path(r['source']).name: r for r in keyed['legacy-allpass', depth]['paths']}
        corrected = {Path(r['source']).name: r for r in keyed['dispersive-dft', depth]['paths']}
        for name in sorted(legacy):
            old, new, base = legacy[name], corrected[name], off[name]
            nominal_energy = lambda row: row['sampled_signal_energy_seconds']*row['correct_projection_fraction']
            comparisons.append({"modulation": depth, "root_index": name,
                                "corrected_minus_legacy_retained_fraction": new['correct_projection_fraction']-old['correct_projection_fraction'],
                                "corrected_over_legacy_post_energy": new['sampled_signal_energy_seconds']/old['sampled_signal_energy_seconds'],
                                "corrected_over_legacy_correct_projected_energy": nominal_energy(new)/nominal_energy(old),
                                "corrected_over_off_correct_projected_energy": nominal_energy(new)/nominal_energy(base)})

    verify_audit_helper_bindings(audits)
    spec = importlib.util.spec_from_file_location('long_summary_analysis', HERE/'analyze.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    refinement = []
    ref_plan_path = evidence/'remote-final/plans/refinement-execution-plan.json'
    ref_receipt_path = evidence/'analysis/remote-refinement-two-export-view.json'
    replay_plan_path = Path(audits['long-replay']['plan'])
    ref_plan, replay_plan, receipt = read(ref_plan_path), read(replay_plan_path), read(ref_receipt_path)
    require(receipt['all_requested_jobs_successful'] and receipt['plan_sha256'] == sha(ref_plan_path),
            'refinement receipt/plan binding failed')
    source_old, source_new = replay_plan['source'], ref_plan['source']
    require(all(source_new['source_file_sha256'].get(k) == v for k, v in source_old['source_file_sha256'].items()),
            'refinement shared scientific sources differ from replay')
    stage_path = evidence/'remote-analysis-root/stage-report.json'; stage = read(stage_path)
    stage_files = {r['path']: r for r in stage['files']}
    replay65 = {Path(r['source']).name: r for r in keyed['dispersive-dft', 'each-65']['paths']}
    for index in (80000, 80001):
        root = f'path-{index}'
        directory = evidence/'remote-analysis-root/local-evidence/triad-selective-correction/independent-corrected-refinement/stable850-m60/dispersive-dft/each-65/signal'/root
        m = read(directory/'manifest.json'); ref = read(Path(replay65[root]['source'])/'manifest.json')
        job = [j for j in ref_plan['jobs'] if j['argv'] == m['command_argv']]
        require(len(job) == 1 and any(r['name'] == job[0]['name'] and r['returncode'] == 0 and
                r['command'] == m['command_argv'] for r in receipt['results']), 'refinement command/runner mismatch')
        raw_inventory = verify_staged_members(directory, evidence/'remote-analysis-root', stage_files)
        require(normalize_refinement_argv(m['command_argv']) == normalize_refinement_argv(ref['command_argv']), 'refinement scientific option drift')
        require(m['random_seed_roots'] == ref['random_seed_roots'] and m['key_seed'] == ref['key_seed'], 'refinement root mismatch')
        require(m['endpoint_truth'] == ref['endpoint_truth'], 'refinement static endpoint truth changed')
        require(m['sample_rate'] == .5 and ref['sample_rate'] == .25, 'unexpected Fs refinement')
        r = module._retention(directory); r['source'] = str(directory)
        high = energy_entry(r, evidence); low = replay65[root]
        refinement.append({"root_index": root, "same_six_integer_roots": True,
                           "same_static_endpoint_truth": True, "fs025": low, "fs050": high,
                           "refinement_file_inventory": raw_inventory,
                           "correct_projection_fraction_difference": high['correct_projection_fraction']-low['correct_projection_fraction'],
                           "post_selective_energy_seconds_ratio": high['sampled_signal_energy_seconds']/low['sampled_signal_energy_seconds'],
                           "scope": "Descriptive discretization/path comparison only; the stochastic continuous-time path and noise are not coupled across Fs, source support is rounded differently, and separately compiled host binaries differ. Not a numerical convergence proof or reception experiment."})

    output = {"schema": "triad-selective-corrected-long-summary-v1", "state": "accepted_audit_summary_with_two_noiseless_refinement_diagnostics",
              "reproducer_sha256": sha(Path(__file__)), "audits": summaries,
              "paired_energy_projection_comparisons": comparisons, "fs_refinement": refinement,
              "refinement_source_bindings": module.RETENTION.source_bindings(),
              "refinement_generation_bindings": {"plan_sha256": sha(ref_plan_path), "receipt_view_sha256": sha(ref_receipt_path),
                  "stage_report_sha256": sha(stage_path), "shared_scientific_source_files_equal": True,
                  "replay_binary_sha256": source_old['binary_sha256'], "refinement_binary_sha256": source_new['binary_sha256'],
                  "interpretation": "The source inventories have equal shared scientific files; the remote plan also binds its holdout planner. The two host/compiler binary hashes differ."},
              "confidence_reference_sha256": sha(HERE.parents[1]/'2026-11-02-selective/reproduction/followup_confidence.py'),
              "duration_assessment": {"cn0_db_hz": -60, "requested_active_days": 850,
                "demonstrated_scope": "Finite seeded sampled-waveform feasibility with fixed unit ray gains, 0.05-Hz two-ray separation, 5-ms excess delay, synthetic spacing-scaled Each25/65 selective modulation, GPSDO-OCXO endpoint spectra and nominal timing/epoch with a bounded static-frequency bank.",
                "not_demonstrated": ["90% or 99% population correct-reception probability", "operational 1e-4/day or 1%/12h empirical false-admission rate", "minimum sufficient duration", "850-day real-world oscillator/channel accuracy", "renewing high-spread channel feasibility", "full blind acquisition or real FHSS retuning"],
                "planner_implication": "Retain 850 days as a tested feasible diagnostic point for this nonrenewing model, not a calibrated duration recommendation. Energy gain and nominal-subspace retention must be modeled separately; the stronger modulation does not establish a sensitivity gain."},
              "limitations": ["Each probability endpoint is separately pointwise one-sided 95%; no simultaneous coverage across gates, outcomes, models or selected cells.",
                "Balanced bounds assume independent iid simulation path outcomes within each bit stratum; p0 and p1 may differ. Pseudorandom root distinction is not a proof of physical or PRNG independence.",
                "Replay roots are paired across models/depths; controls share roots with their corresponding signals. They cannot be pooled as extra independent trials or monitoring exposure.",
                "Null admissions measure noise-only controls; wrong-key admissions measure a distinct signal-present rejection distribution.",
                "Nominal noiseless projection includes legitimate public channel nuisance coefficients and is not a decoder success statistic or an achievable sensitivity bound.",
                "Prior whole-array legacy exports ended before complete tails; the current replay preserves the legacy operator with complete public observation support. It is not claimed to reproduce the old truncated whole array."]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2, allow_nan=False)+'\n')
    print(json.dumps({"output": str(args.output), "sha256": sha(args.output), "audits": len(summaries), "refinement_paths": len(refinement)}))


if __name__ == '__main__':
    main()
