import json,subprocess
from pathlib import Path
root=Path(__file__).resolve().parent
for run in json.loads((root/'fixed-runs.json').read_text()):
 name=run['name']
 print('START',name,flush=True)
 with (root/(name+'.json')).open('x') as out,(root/(name+'.log')).open('x') as err:
  result=subprocess.run(run['cmd'],stdout=out,stderr=err,timeout=180)
 if result.returncode: raise RuntimeError((name,result.returncode))
 d=json.loads((root/(name+'.json')).read_text())
 print(json.dumps(dict(name=name,**{k:d[k] for k in ['exact','signal_seconds','physical_end_capture_seconds','rx_intervals','tx_intervals','ldpc_failed_frames','fifo_overflow','fifo_maximum_seconds','decode_status']})),flush=True)
 if d['fifo_overflow'] or d['capture_error'] or d['playback_error']:raise RuntimeError('Audio transport error')
