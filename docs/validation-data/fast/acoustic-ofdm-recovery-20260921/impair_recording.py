"""Offline controlled time-delay step in an existing real acoustic capture."""
import argparse,json
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);p.add_argument('--at',type=float,default=21.);p.add_argument('--delay',type=int,default=16)
a=p.parse_args()
if a.output.exists(): raise RuntimeError('Refusing to overwrite capture')
x=np.fromfile(a.source,dtype='<f4');point=int(a.at*48000)
y=x.copy();y[point+a.delay:]=x[point:-a.delay];y[point:point+a.delay]=x[point]
y.astype('<f4').tofile(a.output)
a.output.with_suffix('.impairment.json').write_text(json.dumps(dict(source=str(a.source),note='Original live capture with an artificial permanent delay step; not an untouched live failure.',sample_rate=48000,at_seconds=a.at,delay_samples=a.delay),indent=2)+'\n')
