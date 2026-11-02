#!/usr/bin/env python3
"""Known-initial-time controls isolate delay/fading from missing timing acquisition."""
import argparse,importlib.util,json,pathlib
p=argparse.ArgumentParser();p.add_argument('--root',default='/workspace/triad');p.add_argument('--python',default='/workspace/triad-env/bin/python');p.add_argument('--screen',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();r=pathlib.Path(a.root)
spec=importlib.util.spec_from_file_location('campaign',pathlib.Path(__file__).resolve().parents[5]/'tools/triad-lowrate-campaign.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
screen=json.loads(a.screen.read_text());source={c['name']:c for c in screen['cases']};cases=[]
for name in ('delay-fast-0','delay-fast-6','delay-fast-7','delay-fast-7-mismatch','g10','g30'):
 c=source[name].copy();c['name']=name+'-aligned';c['cn0_grid']=([-15,-10,-5] if not c['gaussian_width_hz'] else [-10,-5,0,5] if name=='g10' else [0,5,10,15]);cases.append(c)
for name,fs,bs in [('g10',512,.015625),('g30',2048,.00390625)]:
 c=source[name].copy();c.update(name=name+'-aligned-fine',fs=fs,block_seconds=bs);c['cn0_grid']=([-5,0,5] if name=='g10' else [5,10,15]);cases.append(c)
exports=[];scores=[]
for c in cases:
 out=r/'local-evidence/spread/aligned'/c['name'];arg=mod.parser().parse_args(['--binary',str(r/'build/dev-cli/triad_long'),'--output',str(out),'--paths','4','--split','development','--seconds',str(c['seconds']),'--rate',str(c['rate']),'--sample-rate',str(c['fs']),'--sample-rate-denominator',str(c['den']),'--block-seconds',str(c['block_seconds']),'--cn0',str(c['cn0']),'--spread-preset','sub9-best','--spread-delay-ms',str(c['delay_ms']),'--spread-doppler-hz',str(c['center_hz']),'--tap-delay-ms',str(c['rxdelay_ms']),'--tap-doppler-hz',str(c['rxcenter_hz']),'--ray-gaussian-width-hz',str(c['gaussian_width_hz'])])
 for i in range(4):
  cmd,truth=mod.make_command(arg,i,out/f'path-{i:05d}');cmd[cmd.index('--clock-sync')+1]='GPS_0ms-0ms_region-0ms_offset';cmd[cmd.index('--audio-error-ms')+1]='0'
  exports.append(dict(name=c['name']+'-'+str(i),timeout_seconds=600,argv=cmd,simulation_only_expected=truth))
 methods=['gaussian-window-ucb'] if c['gaussian_width_hz'] else ['static-fft-ucb'];cmd=[a.python,str(r/'tools/triad-conditional-sweep.py'),*[str(out/f'path-{i:05d}') for i in range(4)],'--output',str(r/'local-evidence/spread/aligned-scores'/(c['name']+'.json')),'--cn0-grid='+','.join(map(str,c['cn0_grid'])),'--noise-trials','16','--trial-batch','16','--controls','signal','null','--methods',*methods,'--frequency-bound','1e-6','--device','cuda:0' if c['gaussian_width_hz'] else 'cpu','--precision','fp32' if c['gaussian_width_hz'] else 'fp64','--threads','1','--memory-gib','12','--selection-methods','16','--duration-alternatives','8','--seed','231103']
 if c['gaussian_width_hz']:cmd+=['--window-doppler-width',str(c['gaussian_width_hz']),'--window-blocks','32']
 scores.append(dict(name=c['name'],timeout_seconds=1800,argv=cmd))
a.output.mkdir(parents=True,exist_ok=True)
for n,jobs in [('aligned-export-plan.json',exports),('aligned-score-plan.json',scores)]:
 (a.output/n).write_text(json.dumps(dict(schema='triad-spread-aligned-screen-v1',scope='Artificial known initial time: GPS_0ms-0ms_region-0ms_offset, audio0, start0. Endpoint oscillator/clock models remain. Other physical cases/seed roots preserved; changedFs generates a different stochastic path. Not broad timing acquisition.',cases=cases,jobs=jobs),indent=2)+'\n')
