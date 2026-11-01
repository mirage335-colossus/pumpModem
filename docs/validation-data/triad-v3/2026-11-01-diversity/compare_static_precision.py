#!/usr/bin/env python3
"""Compare saved CPU FP64 / CUDA FP32 static-frequency scores, never rerun DSP."""
import argparse, json
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
r=a.root/'static-frequency-final';out=[]
for cpu,gpu in [('short-microwave-fp64','short-microwave-gpu'),('dense-ocxo-twoday','dense-ocxo-gpu')]:
 d1=json.loads((r/(cpu+'.json')).read_text());d2=json.loads((r/(gpu+'.json')).read_text())
 refs={c['input_hashes']['manifest.json']:c for c in d1['captures']}
 pairs=[]
 for c2 in d2['captures']:
  c1=refs[c2['input_hashes']['manifest.json']]
  assert c1['input_hashes']==c2['input_hashes']
  for method,v2 in c2['methods'].items():
   v1=c1['methods'][method];x=np.array(v1['bit_log_scores']);y=np.array(v2['bit_log_scores'])
   pairs.append({'manifest_sha256':c2['input_hashes']['manifest.json'],'method':method,'maximum_absolute_log_score_error':float(abs(x-y).max()),'same_winner':bool(x.argmax()==y.argmax()),'same_admission':bool((x.max()>=v1['threshold'])==(y.max()>=v2['threshold']))})
 out.append({'cpu':cpu,'gpu':gpu,'pairs':pairs,'max_error':max(x['maximum_absolute_log_score_error'] for x in pairs),'all_same_winner':all(x['same_winner'] for x in pairs),'all_same_admission':all(x['same_admission'] for x in pairs)})
a.output.write_text(json.dumps(out,indent=2)+'\n');print([{k:v for k,v in x.items() if k!='pairs'} for x in out])
