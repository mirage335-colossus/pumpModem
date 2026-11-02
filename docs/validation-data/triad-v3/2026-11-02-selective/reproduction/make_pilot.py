#!/usr/bin/env python3
"""Cheap paired selective-depth screen using the shared sampled channel."""
import argparse,json,pathlib
p=argparse.ArgumentParser();p.add_argument('--root',type=pathlib.Path,required=True);p.add_argument('--python',required=True);p.add_argument('--binary',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();r=a.root.resolve();a.output.mkdir(parents=True,exist_ok=True)
exports=[];scores=[]
for mode in ('off','each-5','each-25','each-65','slow-65','fast-65'):
 name='g001-'+mode;out=r/'local-evidence/triad-selective/pilot'/name
 cmd=[a.python,str(r/'tools/triad-lowrate-campaign.py'),'--binary',str(a.binary.resolve()),'--output',str(out),'--split','development','--paths','2','--start-index','30000','--jobs','2','--timeout','120','--seconds','86400','--rate','.036','--sample-rate','1','--sample-rate-denominator','4','--block-seconds','64','--cn0','-25','--spread-preset','sub9-best','--spread-delay-ms','.005','--spread-doppler-hz','.001','--tap-delay-ms','.005','--tap-doppler-hz','.001','--ray-gaussian-width-hz','.001','--selective-perturbation',mode]
 exports.append(dict(name=name,timeout_seconds=180,argv=cmd))
 cmd=[a.python,str(r/'tools/triad-conditional-sweep.py'),*[str(out/f'path-{i:05d}') for i in (30000,30001)],'--output',str(r/'local-evidence/triad-selective/pilot-scores'/(name+'.json')),'--cn0-grid=-30,-28,-26,-25,-24','--noise-trials','16','--trial-batch','16','--controls','signal','null','--methods','gaussian-window-ucb','--coherence','450.158158078553','--device','cpu','--precision','fp64','--threads','2','--memory-gib','4','--selection-methods','16','--duration-alternatives','8','--window-doppler-width','.001','--window-blocks','64','--seed','231202']
 scores.append(dict(name=name,timeout_seconds=180,argv=cmd))
for name,jobs in [('pilot-export-plan.json',exports),('pilot-score-plan.json',scores)]:
 (a.output/name).write_text(json.dumps(dict(schema='triad-selective-pilot-plan-v1',scope='Paired roots at fixed Fs/rate. Off and On have different filter tails, so not whole-array identity. Conditional noise draws are not independent physical paths.',jobs=jobs),indent=2)+'\n')
