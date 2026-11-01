#!/usr/bin/env python3
"""Recount paired short screens at the combined K9 gate from saved NPZ scores."""
import argparse, hashlib, json, math
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
gate=math.log(1440*3*9*2/.00005);rows={};identities={};sources={}
for dirname in ('gaussian-screens-pooled','phase-screens-pooled'):
 for f in sorted((a.root/dirname).glob('*.json')):
  d=json.loads(f.read_text())
  if not isinstance(d,dict) or d.get('schema') not in ('triad-noise-study-v1','triad-noise-batch-study-v1'):continue
  assert d['configuration']['selection_methods']==7 and d['configuration']['calibration_alternatives']==2
  zpath=f.with_suffix('.scores.npz');sources[str(f.relative_to(a.root))]=hashlib.sha256(f.read_bytes()).hexdigest();sources[str(zpath.relative_to(a.root))]=hashlib.sha256(zpath.read_bytes()).hexdigest()
  ids=[];row=rows.setdefault(f.stem,{})
  with np.load(zpath,allow_pickle=False) as z:
   for c in d['captures']:
    assert c['status']=='completed' and c['control']=='signal'
    ids.append((c['input_hashes']['manifest.json'],c['input_hashes']['observation.c128']))
    i=c['physical_path_index'];b=c['truth_bit_simulation_only']
    for method in c['methods']:
     x=z[f'path{i:05d}_signal_{method}'];assert x.shape==(1,2)
     t=row.setdefault(method,{'correct':0,'wrong':0,'missed':0});accepted=x.max()>=gate;winner=x.argmax()
     t['correct']+=int(accepted and winner==b);t['wrong']+=int(accepted and winner!=b);t['missed']+=int(not accepted)
  assert len(ids)==12 and len(set(ids))==12
  if f.stem in identities:assert identities[f.stem]==ids
  else:identities[f.stem]=ids
assert len(rows)==12 and all(len(v)==5 for v in rows.values())
a.output.write_text(json.dumps({'strict_K9_gate':gate,'input_report_and_score_sha256':sources,'same_12_actual_paths_paired_across_five_methods':True,'cases':rows},indent=2)+'\n')
