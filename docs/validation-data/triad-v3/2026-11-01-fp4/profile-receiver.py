#!/usr/bin/env python3
"""Bounded paired whole-receiver stage timings; use an external timeout.

An optional surrounding Nsight Systems invocation adds profiler overhead.
No provider operations. All passes consume the identical sampled capture.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--capture', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.output.exists():
        raise ValueError('fresh output directory required')
    a.output.mkdir(parents=True)
    sys.path.insert(0, str(a.root.resolve()/'tools'))
    spec = importlib.util.spec_from_file_location('campaign', a.root/'tools/triad-fp4-campaign.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    import torch
    torch.set_num_threads(2)
    rows = []
    for label, precision, inference, strict_all in (
        ('fp64-correlation-graph-inference', 'fp64', 'graph-fp64', False),
        ('fp32-correlation-graph-inference', 'fp32', 'graph-fp64', False),
        ('nvfp4-correlation-graph-inference', 'native-fp4', 'graph-fp64', False),
        ('fp64-correlation-fp64-inference', 'fp64', 'reference', False),
        ('nvfp4-plus-exhaustive-strict-verification', 'native-fp4', 'graph-fp64', True),
    ):
        argv = [str(a.capture), '--output', str(a.output/(label+'.json')), '--device', 'cuda:0',
                '--precision', precision, '--native-format', 'nvfp4', '--trim-zero-suffix',
                '--cfo-hz', '-.01', '0', '.01', '--lag-stop', '2', '--sample-rate', '64',
                '--block-samples', '256', '--phase-states', '512', '--repeats', '3',
                '--receiver-memory-mib', '8192', '--inference-implementation', inference,
                '--inference-precision', 'fp64' if inference == 'reference' else 'fp32',
                '--instrumented-timing', '--nvtx', '--save-scores',
                '--source-revision', 'ebfe649c1f9c0a5bace48594c982111f64c28f41+source-hashed-study']
        if strict_all:
            argv += ['--strict-all']
        start = time.perf_counter()
        result = module.run(module.parse_args(argv))
        rows.append(dict(label=label, argv=argv, driver_call_wall_seconds=time.perf_counter()-start,
                         scans=result['scans']))
        (a.output/'index.json').write_text(json.dumps(dict(status='running', cases=rows), indent=2))
        print(label, rows[-1]['driver_call_wall_seconds'], flush=True)
        torch.cuda.empty_cache()
    (a.output/'index.json').write_text(json.dumps(dict(status='complete', cases=rows), indent=2))


if __name__ == '__main__':
    main()
