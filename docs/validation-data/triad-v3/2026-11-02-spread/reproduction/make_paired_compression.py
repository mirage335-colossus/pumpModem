#!/usr/bin/env python3
"""Re-export identical Fs/seed paths with coarser blocks; hold window seconds fixed."""
import argparse,json,pathlib
p=argparse.ArgumentParser();p.add_argument('--directory',type=pathlib.Path,required=True);a=p.parse_args();d=a.directory;e=json.loads((d/'aligned-export-plan.json').read_text());s=json.loads((d/'aligned-score-plan.json').read_text());ej=[];sj=[]
for base,bs in [('g10-aligned-fine',.0625),('g30-aligned-fine',.015625)]:
 target=base+'-paired-coarse'
 for item in e['jobs']:
  if item['name'].rsplit('-',1)[0]!=base:continue
  q=dict(item);q['name']=item['name'].replace(base,target);cmd=item['argv'].copy();cmd[cmd.index('--block-seconds')+1]=str(bs);cmd[cmd.index('--output')+1]=cmd[cmd.index('--output')+1].replace(base,target);q['argv']=cmd;ej.append(q)
 item=next(x for x in s['jobs'] if x['name']==base);q=dict(item);q['name']=target;cmd=[x.replace(base,target) for x in item['argv']];cmd[cmd.index('--window-blocks')+1]='8';q['argv']=cmd;sj.append(q)
meta=dict(schema='triad-spread-paired-compression-v1',scope='SamephysicalFs/source/key/clock/channel/path/noise seeds and initialtiming/capturebounds as the aligned-fine reference. Coarserexport combines4fineblocks; J8coarse vsJ32fine retains identical elapsed covariance-window boundaries. Residualcalibrationrankcanchange; modelcompression ispartofreceiver. Array-sum comparison checks pairedobservation identity, not newindependentpaths.')
for name,jobs in [('compression-export-plan.json',ej),('compression-score-plan.json',sj)]: (d/name).write_text(json.dumps(dict(meta,jobs=jobs),indent=2)+'\n')
