#!/usr/bin/env python3
"""Uncontended cold-process latency; public base configuration for all captures."""
import os
os.environ.update(OPENBLAS_NUM_THREADS='2', OMP_NUM_THREADS='2', MKL_NUM_THREADS='2')
import argparse, hashlib, json, pathlib, resource, subprocess, sys, time
ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = pathlib.Path(__file__).resolve().parent
BASE = ROOT/'local-evidence/triad-cpu-ceiling/capture'
def sha(p): return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def save(p, x): pathlib.Path(p).write_text(json.dumps(x, indent=2, allow_nan=False)+'\n')
def child(capture, destination):
    start_cpu, start_wall = time.process_time(), time.perf_counter()
    sys.path.insert(0, str(ROOT/'tools'))
    import torch
    from triad_dynamic_joint import load_public
    from triad_realtime_receiver import RealtimeReceiver
    torch.set_num_threads(2); torch.set_num_interop_threads(1)
    torch.set_float32_matmul_precision('highest')
    imports = dict(cpu_seconds=time.process_time()-start_cpu, wall_seconds=time.perf_counter()-start_wall)
    c,w=time.process_time(),time.perf_counter()
    m,t,e,_,_=load_public(BASE)
    _,_,_,raw,valid=load_public(capture,BASE)
    loading=dict(cpu_seconds=time.process_time()-c,wall_seconds=time.perf_counter()-w)
    receiver=RealtimeReceiver(m,t,e)
    result=receiver.score(raw,valid)
    result['latency_boundary']=dict(imports=imports,loading=loading,
        child_body=dict(cpu_seconds=time.process_time()-start_cpu,wall_seconds=time.perf_counter()-start_wall),
        maximum_rss_kib=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
        input_sha256={p.name:sha(p) for p in pathlib.Path(capture).iterdir() if p.is_file()},
        scope='Fresh Python process, imports, public input loading, FFT constructor, Helper/coarse/refinement; raw template synthesis and ADC/DDC excluded')
    save(destination,result)
def main():
    p=argparse.ArgumentParser();p.add_argument('--child',type=pathlib.Path);p.add_argument('--output',type=pathlib.Path);a=p.parse_args()
    if a.child: child(a.child,a.output);return
    protocol=json.loads((OUT/'protocol.json').read_text())
    assert all(sha(ROOT/'tools'/n)==h for n,h in protocol['sources'].items())
    rows=[]
    for i,capture in enumerate([BASE,OUT/'captures/signal-00',OUT/'captures/signal-15']):
        output=OUT/('latency-'+str(i)+'.json')
        before=resource.getrusage(resource.RUSAGE_CHILDREN);w=time.perf_counter()
        cmd=[sys.executable,str(pathlib.Path(__file__).resolve()),'--child',str(capture),'--output',str(output)]
        subprocess.run(cmd,check=True,cwd=ROOT,timeout=120)
        after=resource.getrusage(resource.RUSAGE_CHILDREN)
        rows.append(dict(command=cmd,result_sha256=sha(output),wall_seconds=time.perf_counter()-w,
            cpu_seconds=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime))
        save(OUT/'latency-summary.json',dict(rows=rows,threads=2,source_sha256=protocol['sources']))
        print('cold',i,rows[-1]['wall_seconds'],rows[-1]['cpu_seconds'],flush=True)
if __name__=='__main__': main()
