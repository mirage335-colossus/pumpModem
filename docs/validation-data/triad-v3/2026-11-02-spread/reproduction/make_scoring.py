#!/usr/bin/env python3
"""Conditional receiver screen on frozen sampled designs; no reliability qualification."""
import argparse,json,math,pathlib
p=argparse.ArgumentParser();p.add_argument('--root',default='/workspace/triad');p.add_argument('--python',default='/workspace/triad-env/bin/python');p.add_argument('--screen',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();r=pathlib.Path(a.root);screen=json.loads(a.screen.read_text());jobs=[]
def add(c,suffix,methods,grid=None,J=32,widthfactor=1,device='cuda:0',precision='fp32',trials=32):
 inputs=[str(r/'local-evidence/spread/screen'/c['name']/f'path-{i:05d}') for i in range(c['paths'])]
 name=(c['name']+'-'+suffix).replace('.','p');output=r/'local-evidence/spread/scores'/(name+'.json');B=c['gaussian_width_hz'];tau=math.sqrt(2)/(math.pi*B) if B else 'inf'
 cmd=[a.python,str(r/'tools/triad-conditional-sweep.py'),*inputs,'--output',str(output),'--cn0-grid='+','.join(map(str,grid or c['cn0_grid'])),'--noise-trials',str(trials),'--trial-batch',str(trials),'--controls','signal','null','--methods',*methods,'--coherence',str(tau),'--frequency-bound','1e-6','--particles','128','--device',device,'--precision',precision,'--threads','2','--memory-gib','12','--selection-methods','16','--duration-alternatives','8','--seed','231101']
 if 'gaussian-window-ucb' in methods:cmd+=['--window-doppler-width',str(B*widthfactor),'--window-blocks',str(J)]
 jobs.append({'name':name,'timeout_seconds':1800,'argv':cmd})
for c in screen['cases']:
 if not c['gaussian_width_hz']:add(c,'fft',['static-fft-ucb'],device='cpu',precision='fp64',trials=32)
 else:
  methods=['gaussian-window-ucb']
  if c['name'] in ('g001','g01','g1'):methods+=['gaussian-ou-ucb']
  add(c,'j32',methods)
  if c['name'] in ('g001','g01','g1'):
   for j in (16,64):add(c,'j'+str(j),['gaussian-window-ucb'],J=j)
   for f in (.5,2):add(c,'width'+str(f),['gaussian-window-ucb'],widthfactor=f)
# Matched workloads for actual CPU/GPU acceleration and precision; same paths/draws.
c=next(x for x in screen['cases'] if x['name']=='g001').copy();c['paths']=2
for dev in ('cpu','cuda:0'):
 add(c,'cfo-'+dev.replace(':',''),['cfo-ou-ucb'],grid=[-28],device=dev,trials=32)
 add(c,'window-'+dev.replace(':',''),['gaussian-window-ucb'],grid=[-28],device=dev,trials=256)
add(c,'window-cpu-fp64',['gaussian-window-ucb'],grid=[-28],device='cpu',precision='fp64',trials=256)
a.output.write_text(json.dumps({'schema':'triad-spread-score-screen-v1','scope':'Conditional signal/null resampling; fixed physical paths. All methods consume same joint draw and full-bank observed UCB; exact source/configuration hashes recorded by tool. 16-method family includes window lengths and width alternatives.','jobs':jobs},indent=2)+'\n')
