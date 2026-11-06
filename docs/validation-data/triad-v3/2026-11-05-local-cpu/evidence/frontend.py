#!/usr/bin/env python3
"""Cost diagnostic only: synthetic PCM, maintained batch DDC, no live reception."""
import os
os.environ.update(OPENBLAS_NUM_THREADS='2', OMP_NUM_THREADS='2', MKL_NUM_THREADS='2')
import hashlib,json,sys,time
from pathlib import Path
import numpy as np
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tools'))
from triad_live_waveforms import from_audio
n=508066
pcm=np.random.default_rng(2417).normal(0,.05,n*8).astype(np.float32)
measurements=[]
for repetition in range(3):
    cpu=time.process_time(); wall=time.perf_counter()
    iq,valid,metadata=from_audio(pcm,48000,6000,1725,occupied_bandwidth_hz=2800,expected_samples=n)
    measurements.append(dict(cpu_seconds=time.process_time()-cpu,wall_seconds=time.perf_counter()-wall,finite=bool(np.isfinite(iq).all())))
result=dict(schema='triad-cpu-audio-conversion-diagnostic-v1',scope='Synthetic real PCM computational diagnostic, not live hardware or whole-sideband channel validation',audio_seconds=n/6000,measurements=measurements,receipt=metadata,source_sha256=hashlib.sha256((root/'tools/triad_live_waveforms.py').read_bytes()).hexdigest(),excluded=['device and driver servicing','unknown alignment','continuous cross-window filtering','whole stochastic sideband and real-audio image compatibility','Python/library import','synthetic input generation'])
Path(__file__).with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
