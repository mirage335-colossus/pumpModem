#!/usr/bin/env python3
"""Occasional multi-day qualification, never a routine regression dependency.

Freeze the protocol JSON before running. Each named case exports every sampled
row, scores it, retains input hashes and a compact Gram, then releases generated
sample files. --retain-samples is an explicit disk-space tradeoff.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import subprocess
import sys
import time


def run(a):
    protocol=json.loads(Path(a.protocol).read_text())
    root=Path(a.output).resolve(); root.mkdir(parents=True,exist_ok=True)
    cases=protocol['cases']
    if a.case:
        cases=[c for c in cases if c['name'] in a.case]
        if len(cases)!=len(set(a.case)):
            raise ValueError('unknown or duplicate requested case')
    env=dict(os.environ,OPENBLAS_NUM_THREADS='1',OMP_NUM_THREADS='1',MKL_NUM_THREADS='1')
    for case in cases:
        directory=root/case['name']; directory.mkdir()
        sources={p:hashlib.sha256(Path(__file__).with_name(p).read_bytes()).hexdigest()
            for p in ('triad_multiday_campaign.py','triad_multiday_study.py',
                      'triad_multiday_likelihood.py','triad_gaussian_state.py')}
        before=resource.getrusage(resource.RUSAGE_CHILDREN); began=time.perf_counter()
        commands=[]
        command=[str(Path(a.exporter).resolve()),'--output',str(directory/'samples'),*case['export']]
        commands.append(command)
        result=subprocess.run(command,capture_output=True,text=True,env=env,timeout=a.timeout)
        (directory/'export.log').write_text(result.stdout+result.stderr)
        generation=time.perf_counter()-began
        if result.returncode:
            raise RuntimeError(f'{case["name"]}: native export failed; see export.log')
        shutil.copyfile(directory/'samples'/'manifest.json',directory/'manifest.json')
        for cn0 in case['cn0']:
            command=[sys.executable,str(Path(__file__).with_name('triad_multiday_study.py')),
                '--input',str(directory/'samples'),'--output',str(directory/f'result-{cn0}.json'),
                '--cn0',str(cn0),'--seed',str(case['noise_seed']),
                '--trials',str(protocol.get('conditional_trials_per_bit',4096)),
                '--null-trials',str(protocol.get('conditional_null_trials',32768)),
                '--sampled-trials',str(protocol.get('sampled_trials_per_condition',4)),
                '--device',a.device]
            commands.append(command)
            result=subprocess.run(command,capture_output=True,text=True,env=env,timeout=a.timeout)
            (directory/f'receiver-{cn0}.log').write_text(result.stdout+result.stderr)
            if result.returncode:
                raise RuntimeError(f'{case["name"]}: reception failed; see receiver log')
            print(result.stdout.strip(),flush=True)
        after=resource.getrusage(resource.RUSAGE_CHILDREN)
        receipt=dict(case=case,commands=commands,generation_wall_seconds=generation,
            source_sha256_at_launch=sources,
            source_changed_during_case=any(hashlib.sha256(Path(__file__).with_name(p).read_bytes()).hexdigest()!=h for p,h in sources.items()),
            total_wall_seconds=time.perf_counter()-began,
            child_cpu_seconds=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime,
            lifetime_max_child_rss_kib=after.ru_maxrss,
            samples_retained=a.retain_samples,device=a.device)
        (directory/'execution.json').write_text(json.dumps(receipt,indent=2)+'\n')
        if not a.retain_samples:
            shutil.rmtree(directory/'samples')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--protocol',required=True); p.add_argument('--exporter',required=True)
    p.add_argument('--output',required=True); p.add_argument('--case',action='append')
    p.add_argument('--device',default='cpu'); p.add_argument('--timeout',type=float,default=1800)
    p.add_argument('--retain-samples',action='store_true')
    run(p.parse_args())
