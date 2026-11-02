#!/usr/bin/env python3
"""Further fixed holdouts chosen from development, with new independent seeds."""
import argparse,json,pathlib
p=argparse.ArgumentParser();p.add_argument('--directory',type=pathlib.Path,required=True);a=p.parse_args();d=a.directory
ex=json.loads((d/'holdout-export-plan.json').read_text());sc=json.loads((d/'holdout-score-plan.json').read_text());template=next(j for j in ex['jobs'] if j['name']=='g001-m26');st=next(j for j in sc['jobs'] if j['name']=='g001-m26')
cases=[]
for cn,n in [(-27,300),(-25,600)]:cases.append(dict(name='g001-m'+str(-cn)+'-refine',cn0=cn,paths=n,rate=.036,fs=1,den=4,block=64,delay=.005,center=.001,width=.001))
for cn in (-24,-22,-20):cases.append(dict(name='g01-m'+str(-cn),cn0=cn,paths=300,rate=.36,fs=2,den=1,block=8,delay=.05,center=.005,width=.01))
exports=[];scores=[]
for c in cases:
 cmd=template['argv'].copy();old=cmd[cmd.index('--output')+1];out=str(pathlib.Path(old).parent/c['name'])
 values={'--output':out,'--paths':str(c['paths']),'--start-index':'10000','--cn0':str(c['cn0']),'--rate':str(c['rate']),'--sample-rate':str(c['fs']),'--sample-rate-denominator':str(c['den']),'--block-seconds':str(c['block']),'--spread-delay-ms':str(c['delay']),'--spread-doppler-hz':str(c['center']),'--tap-delay-ms':str(c['delay']),'--tap-doppler-hz':str(c['center']),'--ray-gaussian-width-hz':str(c['width']),'--jobs':'4'}
 for k,v in values.items():cmd[cmd.index(k)+1]=v
 exports.append(dict(name=c['name'],timeout_seconds=1800,argv=cmd))
 original=st['argv'];cmd=original[:2]+[str(pathlib.Path(out)/f'path-{i:05d}') for i in range(10000,10000+c['paths'])]+original[original.index('--output'):]
 cmd[cmd.index('--output')+1]=str(pathlib.Path(original[original.index('--output')+1]).parent/(c['name']+'.json'));cmd[cmd.index('--window-doppler-width')+1]=str(c['width'])
 scores.append(dict(name=c['name'],timeout_seconds=1800,argv=cmd))
meta=dict(schema='triad-spread-boundary-holdout-plan-v1',selection='New fixed-size independent channel/key/noise roots at indices10000+. 0.001Hz -25/-27 selected after first fixed holdout; preceding samples are not pooled for qualification. 0.01Hz points selected from development only. J64; no stopping/tuning on these scores.',cases=cases,targets=ex['targets'])
for n,jobs in [('boundary-export-plan.json',exports),('boundary-score-plan.json',scores)]: (d/n).write_text(json.dumps(dict(meta,jobs=jobs),indent=2)+'\n')
