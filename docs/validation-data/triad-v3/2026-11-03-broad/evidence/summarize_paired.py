#!/usr/bin/env python3
"""Compact a previously source-bound, independently recounted paired analysis.

This does not rerun receivers or replace rescore/analyze.py's raw-score checks.
Heterogeneous cells and reused conditional draws are never reliability pools.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path


def summarize(path):
    raw = path.read_bytes()
    data = json.loads(raw)
    if len({(r['variant'], r['job_id'], r['control'], r['target'])
            for r in data['outcomes']}) != len(data['outcomes']):
        raise ValueError('duplicate outcome identity')
    cells = defaultdict(Counter)
    diagnostics = defaultdict(Counter)
    residual = []
    for row in data['outcomes']:
        key = tuple(row[k] for k in ('variant', 'stage', 'case', 'method',
                                    'cn0_db_hz', 'control', 'target'))
        cells[key].update(row['counts'])
        if row['control'] == 'null':
            diagnostics[(row['variant'], row['stage'], row['method'], row['target'])].update(row['counts'])
        if row['variant'] == 'numerical-fix' and row['target'] == 'strict' and (
                row['counts'].get('wrong_accepted', 0) or
                (row['control'] == 'wrong-key' and row['counts'].get('false_admitted', 0))):
            residual.append({k: row[k] for k in ('job_id', 'case', 'stage', 'method',
                            'cn0_db_hz', 'control', 'bit', 'counts', 'score_min', 'score_max')})
    fields = ('variant', 'stage', 'case', 'method', 'cn0_db_hz', 'control', 'target')
    result = dict(schema='triad-broad-paired-summary-v1',
        input_analysis_sha256=hashlib.sha256(raw).hexdigest(),
        analysis_source_sha256=data['analysis_sha256'],
        summarizer_source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        baseline_plan_sha256=data['baseline_plan_sha256'],
        manifest_sha256=data['manifest_sha256'],
        source_closure=data['source_closure'], baseline_source_closure=data['baseline_source_closure'],
        status_transitions=data['status_transitions'], failures=data['failures'],
        noise_only_diagnostic_counts=[dict(zip(('variant','stage','method','target'), key), counts=dict(value))
            for key, value in sorted(diagnostics.items())],
        cell_results=[dict(zip(fields, key), counts=dict(value))
            for key, value in sorted(cells.items(), key=lambda item: str(item[0]))],
        remaining_strict_wrong_admissions=residual,
        numerical_fix_decision_changes={name: sum(p.get(name, 0) for j in data['job_pairs']
            for p in j['paired_raw_scores']) for name in
            ('winner_changes','strict_admission_changes','relaxed_admission_changes')},
        precision_comparisons=data['precision_comparisons'],
        limitations=data['limitations'] + [
            'This compaction trusts the retained rescore analysis raw-array validation; its SHA binds the exact analysis used.',
            'Diagnostic totals repeat noise draws across C/N0, algorithms and arithmetic; they are not independent captures or a rare-event confidence bound.',
            'Cell sums retain heterogeneous bit seeds. Small completed counts do not qualify99% reception.',
            'Completed conditions in failed jobs remain in these cells. The enclosing failures remain failures; missing conditions were not silently removed.'])
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('analysis', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    a.output.write_text(json.dumps(summarize(a.analysis), indent=2, sort_keys=True, allow_nan=False)+'\n')
