#!/usr/bin/env python3
"""Identical large exhaustive correlation workload for each GPU partition."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--fused-reconstruction', action='store_true')
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    sys.path.insert(0, str(a.root.resolve()/'tools'))
    spec = importlib.util.spec_from_file_location('campaign', a.root/'tools/triad-fp4-campaign.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    import torch
    torch.set_num_threads(2)
    rows = []
    for precision in ('fp32', 'native-fp4'):
        argv = [str(a.root/'inputs/large-bank'), '--output', str(a.output/(precision+'.json')),
                '--device', 'cuda:0', '--precision', precision, '--correlation-only',
                '--trim-zero-suffix', '--cfo-hz', *[str(i/100) for i in range(-32,33)],
                '--lag-stop', '32', '--sample-rate', '64', '--block-samples', '256',
                '--candidate-tile', '32', '--frequency-tile', '65', '--column-tile', '4096',
                '--template-cache-mib', '2048', '--repeat-observations', '32', '--repeats', '5',
                '--source-revision', 'ebfe649c1f9c0a5bace48594c982111f64c28f41+source-hashed-study']
        if a.fused_reconstruction:
            argv += ['--fused-reconstruction']
        start = time.perf_counter()
        result = module.run(module.parse_args(argv))
        rows.append(dict(precision=precision, argv=argv, driver_call_wall_seconds=time.perf_counter()-start,
                         scans=result['scans']))
        (a.output/'index.json').write_text(json.dumps(dict(status='running', cases=rows), indent=2))
        print(precision, rows[-1]['driver_call_wall_seconds'], flush=True)
        torch.cuda.empty_cache()
    (a.output/'index.json').write_text(json.dumps(dict(status='complete', cases=rows), indent=2))


if __name__ == '__main__':
    main()
