import sys,json,hashlib,time,os
from pathlib import Path
import numpy as np
ROOT=Path.cwd();D=ROOT/'local-evidence/triad-tracking-200/sampled';sys.path.insert(0,str(ROOT/'tools'))
from triad_continuous import NativeStream,tracking_source_basis,make_timing_forward
from triad_timing_forward import TimingState
tag=os.environ.get('TRIAD_CACHE_TAG','cache-check');p=json.loads((D/'cache-protocol.json').read_text());old=json.loads((D/'rxonly-first/results.json').read_text());pos=np.fromfile(D/'rxonly-first/rx-map.f64','<f8')
r={'protocol':p,'identities':{f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in ['tools/triad_tracking_basis.py','tools/triad_continuous.py','build/dev-cli/triad_helper_export']},'windows':[]}
with (D/(tag+'-native.log')).open('x') as log:
 s=NativeStream(ROOT/'build/dev-cli/triad_helper_export',old['source_args'],kind='emitted',log=log)
 try:
  d=s.descriptor;fs=d['sample_rate_hz'];f,_=make_timing_forward({'coordinates':[[0,0,0,0]]},d,3);states=[TimingState(0,x,v) for x in [-12,0,9] for v in [-4,0,4]]
  auto=tracking_source_basis(s,[[0,0,0,0]],[[0,0]],d['data_offset_samples']/fs,f,d['data_if_hz'])
  direct=tracking_source_basis(s,[[0,0,0,0]],[[0,0]],d['data_offset_samples']/fs,f,d['data_if_hz'],template_backend='direct')
  for t in p['physical_window_start_seconds']:
   first=int(t*fs);stop=first+p['window_samples'];origin=int(np.floor(pos[first]));offsets=pos[first:stop]-origin;anchor=(origin+.5*(offsets[0]+offsets[-1]))/fs
   start=time.perf_counter();truth=direct(states,anchor,first,stop,origin,offsets);dt=time.perf_counter()-start
   start=time.perf_counter();got=auto(states,anchor,first,stop,origin,offsets);at=time.perf_counter()-start
   rel=float(np.linalg.norm(got-truth)/max(np.linalg.norm(truth),1e-30));mx=float(np.max(np.abs(got-truth))/max(np.max(np.abs(truth)),1e-30))
   r['windows'].append(dict(start_seconds=t,relative_l2_error=rel,normalized_maximum_complex_error=mx,direct_wall_seconds=dt,cache_wall_seconds=at))
  r['cache']=auto.cache_report();r['passed']=all(w['relative_l2_error']<p['criteria']['relative_l2_error'] and w['normalized_maximum_complex_error']<p['criteria']['normalized_maximum_complex_error'] for w in r['windows']) and r['cache']['fallback_reason'] is None
 finally:s.close()
(D/(tag+'-results.json')).write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2));sys.exit(0 if r['passed'] else 1)
