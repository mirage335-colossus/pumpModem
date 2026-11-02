#!/usr/bin/env python3
"""Execute a frozen local experiment command list through the bounded runner.

No provider operations. Jobs retain command, deadline, exit status and logs.
SIGTERM/INT propagates to every owning runner so it can reap its process group.
"""
import argparse, concurrent.futures, json, os, pathlib, signal, subprocess, sys, threading, time
p=argparse.ArgumentParser();p.add_argument('--root',type=pathlib.Path,required=True);p.add_argument('--plan',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--jobs',type=int,default=4)
a=p.parse_args()
if not 1<=a.jobs<=16: p.error('jobs must be1..16')
a.root=a.root.resolve();a.output.mkdir(parents=True,exist_ok=False)
plan=json.loads(a.plan.read_text());stop=threading.Event()
for s in (signal.SIGINT,signal.SIGTERM): signal.signal(s,lambda *_:stop.set())
runner=a.root/'tools/triad_bounded_run.py'
def run(job):
 name=job['name']; limit=job['timeout_seconds'];start=time.monotonic()
 if not name.replace('-','').replace('_','').isalnum() or not 0<limit<=1800:raise ValueError('invalid bounded job')
 if stop.is_set():return {'name':name,'outcome':'not_started_cancelled'}
 cmd=[sys.executable,str(runner),'--cwd',str(a.root),'--log',str(a.output/(name+'.log')),'--status',str(a.output/(name+'.status.json')),'--timeout',str(limit),'--max-seconds',str(limit),'--term-grace','10','--',*job['argv']]
 child=subprocess.Popen(cmd,stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True)
 sent=False
 try:
  while child.poll() is None:
   if stop.is_set() and not sent: child.terminate();sent=True
   try:child.wait(timeout=.1)
   except subprocess.TimeoutExpired:pass
 finally:
  if child.poll() is None:child.terminate();child.wait()
 err=child.stderr.read();child.stderr.close()
 result={'name':name,'returncode':child.returncode,'elapsed_seconds':time.monotonic()-start,'command':job['argv']}
 try:result['runner']=json.loads((a.output/(name+'.status.json')).read_text())
 except (OSError,ValueError) as exc:result['receipt_error']=str(exc)
 if err:result['stderr']=err[-4096:]
 print(json.dumps({k:result[k] for k in ('name','returncode','elapsed_seconds')}),flush=True)
 return result
with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool: results=list(pool.map(run,plan['jobs']))
(a.output/'results.json').write_text(json.dumps({'plan':str(a.plan),'cancelled':stop.is_set(),'results':results},indent=2)+'\n')
sys.exit(1 if stop.is_set() or any(r.get('returncode',1) for r in results) else 0)
