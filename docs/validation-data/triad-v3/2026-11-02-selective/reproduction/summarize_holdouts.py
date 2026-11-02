#!/usr/bin/env python3
"""Retain compact results from the complete plan-bound actual-observation audits."""
import argparse
import hashlib
import json
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--audit', type=Path, action='append', required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    studies = []
    for path in a.audit:
        report = json.loads(path.read_text())
        assert report['integrity_complete']
        cells = []
        for cell in report['cells']:
            assert cell['integrity']['export_complete'] and cell['integrity']['score_complete']
            summary = {k: cell[k] for k in ['cell', 'control', 'attempted',
                       'gates', 'scientific_status', 'scope']}
            if cell['control'] == 'signal':
                summary['bit_allocation'] = cell['bit_allocation']
            cells.append(summary)
        studies.append({'audit_file': str(path), 'audit_sha256': digest(path),
                        'integrity_complete': True, 'plans': report['plans'], 'cells': cells})
    output = {'schema': 'triad-selective-complete-holdout-summary-v1',
              'source_sha256': digest(Path(__file__)), 'studies': studies,
              'scope': 'Counts and pointwise bounds copied from accepted complete audits. '
                       'Correct-reception confidence does not establish noise-tail or wrong-key qualification.'}
    with a.output.open('x') as stream:
        json.dump(output, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write('\n')


if __name__ == '__main__':
    main()
