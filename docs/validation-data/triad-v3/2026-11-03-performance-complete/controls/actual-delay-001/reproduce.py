#!/usr/bin/env python3
"""One real sampled-family delay-inclusion diagnostic; no performance gate claim."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import time

p = argparse.ArgumentParser()
p.add_argument('--root', type=Path, required=True)
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--output-directory', type=Path, required=True)
a = p.parse_args()
root = a.root.resolve(strict=True)
binary = a.binary.resolve(strict=True)
out = a.output_directory.resolve()
out.mkdir(parents=True, exist_ok=False)
sys.path.insert(0, str(root / 'tools'))
import torch
from triad_perf_decision_controls import DelayLedger, DelayRule, digest_file
from triad_perf_family_state import run_case

torch.set_num_threads(1)
fixture = out / 'sampled-stats'
command = [str(binary), '--mode', 'export-stats', '--seconds', '16', '--rate', '36',
    '--added', '1', '--sample-rate', '256', '--channel', 'hf', '--phase-diffusion', '0',
    '--basis', 'two-ray', '--block-seconds', '.5', '--cn0', '8', '--stats-noisy',
    '--threads', '1', '--memory-mib', '64', '--export-max-samples', '100000',
    '--export-max-mib', '16', '--seed', '931751', '--output', str(fixture)]
case = {'id': 'gaussian-actual-delay-control', 'family': 'gaussian-state', 'unit': 'blocks',
        'options': {'coherences': [4.], 'taps': 2, 'memory_limit_bytes': 32 * 1024**2}}
paths = ['tools/triad_perf_decision_controls.py', 'tools/triad_perf_family_state.py',
         'tools/triad_perf_families.py', 'tools/triad-state-study.py',
         'tools/triad_state_workspace.py', 'tools/triad_gaussian_state.py']
sources = {name: digest_file(root / name) for name in paths}
plan = {'command': command, 'case': case, 'requested_external_delay_seconds': .05,
        'source_sha256': sources, 'binary_sha256': digest_file(binary),
        'scope': 'actual noisy native exporter plus full Gaussian-state adapter; one paired numerical/timer diagnostic, no confidence-qualified performance gate'}
(out / 'plan.json').write_text(json.dumps(plan, indent=2))
started = time.perf_counter()
export = subprocess.run(command, capture_output=True, text=True, timeout=45, check=True)
(out / 'export.stdout').write_text(export.stdout)
(out / 'export.stderr').write_text(export.stderr)
export_seconds = time.perf_counter() - started
baseline = run_case(root, fixture, case, 'cpu', 'fp64')
ledger = DelayLedger({'external-finalization': DelayRule(extra_seconds=.05)}, maximum_delay_seconds=.1)
started = time.perf_counter()
injected = ledger.call('external-finalization', run_case, root, fixture, case, 'cpu', 'fp64')
external = time.perf_counter() - started
trace = ledger.report()
assert baseline['result']['scores'] == injected['result']['scores'], 'delay changed actual receiver scores'
assert baseline['work'] == injected['work'], 'delay changed actual work'
assert baseline['regime'] == injected['regime'], 'delay changed computational regime'
internal = injected['seconds']['load'] + injected['seconds']['initialize_update_transfer_and_finalize']
assert external - internal >= .045, 'complete enclosing timer missed outside-receiver delay'
assert trace['actual_total_seconds'] >= .045, 'delay was not actually executed'
assert sources == {name: digest_file(root / name) for name in paths}, 'source changed during diagnostic'
inputs = {str(path.relative_to(fixture)): digest_file(path) for path in fixture.iterdir() if path.is_file()}
report = {'status': 'complete', 'plan': plan, 'export_wall_seconds': export_seconds,
          'fixture_sha256': inputs, 'baseline': baseline, 'injected': injected,
          'external_wrapped_seconds': external, 'internal_receiver_seconds': internal,
          'outside_receiver_seconds': external - internal, 'delay_ledger': trace,
          'numeric_and_work_unchanged': True, 'performance_regression_qualified': False,
          'scope': 'one actual full-family execution validates timer inclusion and unchanged values, not regression detection power or future performance'}
(out / 'result.json').write_text(json.dumps(report, indent=2, allow_nan=False))
print(json.dumps({k: report[k] for k in ('status', 'external_wrapped_seconds', 'internal_receiver_seconds',
                                      'outside_receiver_seconds', 'numeric_and_work_unchanged')}))
