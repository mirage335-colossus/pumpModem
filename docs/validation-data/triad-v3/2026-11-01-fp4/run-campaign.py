#!/usr/bin/env python3
"""Reproduce bounded paired receiver experiments in one persistent CUDA process.

Run under an external timeout. Timing copies are not physical replications.
This script performs no provider/resource operations.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import time
import traceback


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, default=Path('.'))
    p.add_argument('--inputs', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--stage', choices=['screen', 'diagnostic', 'performance', 'holdout'], required=True)
    p.add_argument('--max-seconds', type=float, default=800)
    p.add_argument('--phase-states', type=int, default=512)
    p.add_argument('--pattern', default='*')
    p.add_argument('--fused-reconstruction', action='store_true')
    a = p.parse_args()
    started = time.perf_counter()
    if a.output.exists():
        raise ValueError('fresh output directory required')
    a.output.mkdir(parents=True)
    sys.path.insert(0, str(a.root.resolve()/'tools'))
    spec = importlib.util.spec_from_file_location('campaign', a.root/'tools/triad-fp4-campaign.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    import torch
    torch.set_num_threads(2)
    records = []
    manifests = sorted(a.inputs.glob(a.pattern+'/manifest.json'))
    manifests = [m for m in manifests if not (m.parent.name.startswith('aes') or m.parent.name in ('smoke', 'large-bank'))]
    if a.stage == 'performance':
        manifests = [m for m in manifests if m.parent.name == 'boundary-cn-10-r0']
    if a.stage == 'diagnostic':
        manifests = [m for m in manifests if m.parent.name in ('boundary-cn-10-r0', 'boundary-cn-12-r0') or m.parent.name.startswith('frontend')]
    if not manifests:
        raise ValueError('no eligible sampled captures matched the requested stage')
    expected = len(manifests) * (2 if a.stage == 'holdout' else 4) * (2 if a.stage == 'performance' else 1)

    def write_index(status):
        (a.output/'index.json').write_text(json.dumps(dict(stage=a.stage, status=status,
            phase_states=a.phase_states, expected_records=expected,
            completed_records=sum(row['status'] == 'completed' for row in records),
            failed_records=sum(row['status'] == 'failed' for row in records),
            process_elapsed_seconds=time.perf_counter()-started, captures=records), indent=2))

    write_index('running')
    for manifest in manifests:
        name = manifest.parent.name
        m = json.loads(manifest.read_text())
        fs = m['sample_rate']
        policies = [('fp64', 'fp64', None), ('fp32', 'fp32', None),
                    ('nvfp4', 'native-fp4', None), ('mxfp4', 'native-fp4', None)]
        if a.stage == 'diagnostic':
            policies = [(mode, 'diagnostic-fp64', mode)
                        for mode in ('strict', 'template-only', 'observation-only', 'both')]
        if a.stage == 'holdout':
            policies = [('fp64', 'fp64', None), ('nvfp4', 'native-fp4', None)]
        copies = (1, 8) if a.stage == 'performance' else (1,)
        for copy_count in copies:
            for label, precision, diagnostic in policies:
                if time.perf_counter()-started >= a.max_seconds:
                    raise TimeoutError('stage time bound reached between captures')
                destination = a.output/f'{name}-{label}-b{copy_count}.json'
                argv = [str(manifest.parent), '--output', str(destination), '--device', 'cuda:0',
                        '--precision', precision, '--native-format', 'mxfp4' if label == 'mxfp4' else 'nvfp4',
                        '--trim-zero-suffix', '--cfo-hz', '-.01', '0', '.01', '--lag-stop', '2',
                        '--sample-rate', str(fs), '--block-samples', str(round(4*fs)),
                        '--phase-states', str(a.phase_states), '--repeat-observations', str(copy_count),
                        '--receiver-memory-mib', '8192',
                        '--repeats', '3' if a.stage == 'performance' else '1', '--save-scores',
                        '--source-revision', 'ebfe649c1f9c0a5bace48594c982111f64c28f41+source-hashed-study']
                if diagnostic:
                    argv += ['--representation-mode', diagnostic]
                if a.fused_reconstruction:
                    argv += ['--fused-reconstruction']
                t = time.perf_counter()
                row = dict(input=name, policy=label, copies=copy_count, argv=argv,
                           policy_scope="correlation precision; same FP32 bulk phase model with FP64 reductions",
                           decision_scope="paired experimental comparison; native FP4 admission is uncalibrated")
                try:
                    result = module.run(module.parse_args(argv))
                    row.update(status='completed', decisions=result['decisions'],
                               scans=[{k:r[k] for k in ('precision', 'complete_resident_seconds', 'bit_scores')}
                                      for r in result['scans']])
                except Exception as error:
                    row.update(status='failed', error=str(error), traceback=traceback.format_exc())
                    destination.with_suffix('.failure.json').write_text(json.dumps(row, indent=2))
                row['driver_call_wall_seconds'] = time.perf_counter()-t
                records.append(row)
                write_index('running')
                print(json.dumps({k:row[k] for k in ('input','policy','copies','status','driver_call_wall_seconds')}), flush=True)
                torch.cuda.empty_cache()
    failed = any(row['status'] != 'completed' for row in records)
    write_index('completed_with_failures' if failed else 'complete')
    if failed:
        raise RuntimeError('one or more capture-policy runs failed; see retained index and failure records')


if __name__ == '__main__':
    main()
