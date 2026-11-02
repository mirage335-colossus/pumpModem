#!/usr/bin/env python3
"""Fixed independent physical-path tests selected before observing their results."""
import argparse,json,pathlib
p=argparse.ArgumentParser();p.add_argument('--root',default='/workspace/triad');p.add_argument('--python',default='/workspace/triad-env/bin/python');p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();r=pathlib.Path(a.root)
cases=[dict(name='stable-005-m60',paths=300,seconds=850*86400,rate=.0036,fs=1,den=8,block=8192,delay=5,center=.05,width=0,cn0=-60,export_jobs=12,method='static-fft-ucb',window_blocks=None)]
for cn in (-28,-26,-24):cases.append(dict(name='g001-m'+str(-cn),paths=300,seconds=86400,rate=.036,fs=1,den=4,block=64,delay=.005,center=.001,width=.001,cn0=cn,export_jobs=2,method='gaussian-window-ucb',window_blocks=64))
exports=[];scores=[]
for c in cases:
 out=r/'local-evidence/spread/holdout'/c['name'];cmd=[a.python,str(r/'tools/triad-lowrate-campaign.py'),'--binary',str(r/'build/dev-cli/triad_long'),'--output',str(out),'--split','holdout','--paths',str(c['paths']),'--start-index','0','--jobs',str(c['export_jobs']),'--timeout','900','--seconds',str(c['seconds']),'--rate',str(c['rate']),'--sample-rate',str(c['fs']),'--sample-rate-denominator',str(c['den']),'--block-seconds',str(c['block']),'--cn0',str(c['cn0']),'--spread-preset','sub9-best','--spread-delay-ms',str(c['delay']),'--spread-doppler-hz',str(c['center']),'--tap-delay-ms',str(c['delay']),'--tap-doppler-hz',str(c['center'])]
 if c['width']:cmd+=['--ray-gaussian-width-hz',str(c['width'])]
 exports.append(dict(name=c['name'],timeout_seconds=1800,argv=cmd))
 inputs=[str(out/f'path-{i:05d}') for i in range(c['paths'])]
 sc=[a.python,str(r/'tools/triad-conditional-sweep.py'),*inputs,'--output',str(r/'local-evidence/spread/holdout-scores'/(c['name']+'.json')),'--mode','actual-noisy','--methods',c['method'],'--reference-variance','1','--frequency-bound','1e-6','--device','cpu','--precision','fp64','--threads','1','--memory-gib','4','--selection-methods','16','--duration-alternatives','8','--seed','231102']
 if c['width']:sc+=['--window-doppler-width',str(c['width']),'--window-blocks',str(c['window_blocks'])]
 scores.append(dict(name=c['name'],timeout_seconds=1800,argv=sc))
meta=dict(schema='triad-spread-holdout-plan-v1',selection='Frozen before these independent sampled/noisy holdouts. Stable model tested at .05Hz center; Gaussian width .001Hz uses J64 after two-path local development. Fixed N=300 balanced bits; no optional stopping or tuning on validation.',targets=dict(strict_correct=.99,strict_noise_any_per_day=1e-4,relaxed_correct=.9,relaxed_noise_any_per_12hours=.01,method_alternatives=16,duration_alternatives=8,attempt_seconds=60),cases=cases)
a.output.mkdir(parents=True,exist_ok=True)
for name,jobs in [('holdout-export-plan.json',exports),('holdout-score-plan.json',scores)]:
 (a.output/name).write_text(json.dumps(dict(meta,jobs=jobs),indent=2)+'\n')
