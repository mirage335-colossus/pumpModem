#!/usr/bin/env python3
"""Frozen development screen; exports independently sampled paths, not receiver claims."""
import argparse,json,pathlib
p=argparse.ArgumentParser();p.add_argument('--root',default='/workspace/triad');p.add_argument('--python',default='/workspace/triad-env/bin/python');p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args()
r=pathlib.Path(a.root);base=r/'local-evidence/spread/screen'
cases=[]
def add(name,*,seconds,rate,fs,den=1,block,delay,center,width=0,paths=4,cn0=-25,grid=None,rxdelay=None,rxcenter=None):
 cases.append(dict(name=name,seconds=seconds,rate=rate,fs=fs,den=den,block_seconds=block,delay_ms=delay,center_hz=center,gaussian_width_hz=width,paths=paths,cn0=cn0,cn0_grid=grid,rxdelay_ms=delay if rxdelay is None else rxdelay,rxcenter_hz=center if rxcenter is None else rxcenter))
for center,delay,den in [(.005,.05,32),(.05,5,8),(.1,.5,4)]:
 add('stable-long-'+str(center),seconds=850*86400,rate=.0036,fs=1,den=den,block=8192,delay=delay,center=center,cn0=-60,grid=[-66,-63,-60,-57],paths=8)
for center,delay,fs in [(1,.5,4),(10,6,32),(30,7,64)]:
 add('stable-day-'+str(center),seconds=86400,rate=.036,fs=fs,block=256,delay=delay,center=center,cn0=-30,grid=[-40,-35,-30,-25],paths=8)
for delay in (0,6,7):
 add('delay-fast-'+str(delay),seconds=600,rate=360,fs=1024,block=1,delay=delay,center=10,cn0=-10,grid=[-20,-15,-10,-5],paths=4)
add('delay-fast-7-mismatch',seconds=600,rate=360,fs=1024,block=1,delay=7,center=10,rxdelay=.005,cn0=-10,grid=[-20,-15,-10,-5],paths=4)
add('g001',seconds=86400,rate=.036,fs=1,den=4,block=64,delay=.005,center=.001,width=.001,grid=[-35,-32,-30,-28,-26,-24,-22])
add('g001-fine',seconds=86400,rate=.036,fs=1,den=4,block=32,delay=.005,center=.001,width=.001,grid=[-30,-28,-26,-24])
add('g01',seconds=86400,rate=.36,fs=2,block=8,delay=.05,center=.005,width=.01,grid=[-30,-28,-26,-24,-22,-20,-18])
add('g01-slowrate',seconds=86400,rate=.036,fs=2,block=8,delay=.05,center=.005,width=.01,grid=[-30,-26,-22,-18])
add('g1',seconds=8640,rate=3.6,fs=16,block=1,delay=.5,center=.1,width=.1,grid=[-25,-22,-20,-18,-16,-14,-12])
add('g1-coarse',seconds=8640,rate=3.6,fs=16,block=4,delay=.5,center=.1,width=.1,grid=[-22,-18,-14])
add('g1-fine',seconds=8640,rate=3.6,fs=16,block=.5,delay=.5,center=.1,width=.1,grid=[-22,-18,-14])
add('g10',seconds=864,rate=36,fs=128,block=.0625,delay=6,center=1,width=10,grid=[-15,-10,-5,0,5])
add('g30',seconds=120,rate=360,fs=512,block=.015625,delay=7,center=10,width=30,grid=[-5,0,5,10,15])
jobs=[]
for c in cases:
 out=base/c['name'];cmd=[a.python,str(r/'tools/triad-lowrate-campaign.py'),'--binary',str(r/'build/dev-cli/triad_long'),'--output',str(out),'--split','development','--paths',str(c['paths']),'--jobs','2','--timeout','900','--seconds',str(c['seconds']),'--rate',str(c['rate']),'--sample-rate',str(c['fs']),'--sample-rate-denominator',str(c['den']),'--block-seconds',str(c['block_seconds']),'--cn0',str(c['cn0']),'--spread-preset','sub9-best','--spread-delay-ms',str(c['delay_ms']),'--spread-doppler-hz',str(c['center_hz']),'--tap-delay-ms',str(c['rxdelay_ms']),'--tap-doppler-hz',str(c['rxcenter_hz'])]
 if c['gaussian_width_hz']:cmd+=['--ray-gaussian-width-hz',str(c['gaussian_width_hz'])]
 jobs.append({'name':c['name'].replace('.','p'),'timeout_seconds':1800,'argv':cmd})
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps({'schema':'triad-spread-screen-plan-v1','stage':'development; not holdout qualification','assumptions':'RF8270 GPSDOOCXO, margin1, Added1, FHSSoff, burstOff, selectiveOff, tight10ms/1ms/0/70ms; physical center separation and stochastic width are independent; receiver geometry public and explicit. Constant-noise UCB family K16/D8, attempts60s; exact command is authoritative. Durations round upward to waveform geometry.','cases':cases,'jobs':jobs},indent=2)+'\n')
