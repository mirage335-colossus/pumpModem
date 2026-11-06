import os,sys,json,time,hashlib,subprocess,math
from pathlib import Path
import numpy as np
import torch
ROOT=Path.cwd();D=ROOT/'local-evidence/continuous-audio-clock/receiver-pair/gentle-delay7';sys.path.insert(0,str(ROOT/'tools'))
from triad_continuous import NativeStream,carrier_phase,source_basis,endpoint_decision
from triad_incremental_receiver import IncrementalReceiver
torch.set_num_threads(2)
protocol=dict(seeds=[94002,94003],configurations=['helper-on'],cn0_db_hz=8,coherences=[.1,.5,1.],clock_events={'tx':[[45,.1],[55,2],[75,-2],[85,-.1]],'rx':[[48,-.1],[60,-2],[80,2],[95,.1]]},coordinates=[[0,-.012,0,0]],taps=[[.004*i,0] for i in range(7)],delay_scope='Fixed +/-12ms covers phase-only independent50ppm endpoints for110s; not physical-rate-feedforward or general1000ppm bound',channel='None/Off explicit flat control; no oscillator error',receiver='fixed nominal native basis, no private clock trajectory',null_scope='Unqualified ASRC colored noise and Helper/real-PCM; model scores diagnostics only',noise_order='white real PCM noise between TX and RX ASRC; paired pre-RX noise samples identical',controller='new gentle defaults: phase-only cap50ppm, total cap1000ppm, slew10ppm/s; ideal exactly dated device frontier, no timestamp jitter',source_padding_seconds=2,receiver_trailing_seconds=1)
(D/'protocol.json').write_text(json.dumps(protocol,indent=2)+'\n')
identities={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in ['build/dev-cli/triad_helper_export','include/datapump/continuous_clock.hpp','src/resampler.cpp','tools/triad_incremental_receiver.py','tools/triad_continuous.py','local-evidence/continuous-audio-clock/receiver-pair/gentle-phase/warp']}
report=dict(protocol=protocol,identities=identities,results=[]);start=time.perf_counter()
def warp(x,side,events):
 p=subprocess.run([str(D.parent/'gentle-phase/warp'),side,'events' if events else 'stable'],input=np.asarray(x,dtype='<f4').tobytes(),capture_output=True,check=True,timeout=30)
 return np.frombuffer(p.stdout,'<f4'),json.loads(p.stderr)
for helper,seed in [('on',94002),('on',94003)]:
 args=['--case','hf','--sample-rate','6000','--rate','36','--added-symbols','10','--seconds','60','--key-seed',str(seed),'--epoch','1793850000','--spread-preset','none','--selective-perturbation','off','--base-oscillator','gpsdo-ocxo','--shift-oscillator','gpsdo-ocxo','--oscillator-effects','off','--arrival-seconds','0','--helper',helper]
 with (D/f'source-{helper}-{seed}.log').open('w') as log:
  s=NativeStream(ROOT/'build/dev-cli/triad_helper_export',args,kind='emitted',log=log);d=s.descriptor;fs=int(d['sample_rate_hz']);n=int(d['total_samples']);chunks=[]
  while s.next_sample<n:chunks.append(s.next(min(16384,n-s.next_sample))[0])
  clean=np.concatenate(chunks);pcm=np.concatenate([np.sqrt(2)*np.real(clean*np.exp(1j*carrier_phase(0,n,d['data_if_hz'],fs))),np.zeros(2*fs)]).astype('<f4');N=n+fs
  # A unit average DATA power and two-sided real-noise density N0/2.
  noise=np.random.default_rng(seed+100000).normal(0,math.sqrt(fs/(2*10**(.1*protocol['cn0_db_hz']))),len(pcm)+100).astype('<f4')
  for events in (False,True):
   tx,tr=warp(pcm,'tx',events);rx,rr=warp(tx+noise[:len(tx)],'rx',events)
   if len(rx)<N: raise RuntimeError('padding/window shortened; must not silently fabricate observed noise')
   values=np.sqrt(2)*rx[:N]*np.exp(-1j*carrier_phase(0,N,d['data_if_hz'],fs))
   r=IncrementalReceiver(sample_rate=fs,samples=N,block_samples=3000,basis=source_basis(s,protocol['coordinates'],protocol['taps'],d['data_offset_samples']/fs),required_bands=[[-160,160]],excluded_bands=[],noise_variance=fs/(10**(.1*protocol['cn0_db_hz'])),stationary_power=[1/7]*7,coherence_seconds=[.1,.5,1.],candidate_count=1,precision='fp32',projection_mode='dft')
   cpu=time.process_time();wall=time.perf_counter()
   for first in range(0,N,3000):r.push(first,values[first:first+3000])
   result=r.finish();scores=result['bit_scores']
   entry=dict(seed=seed,helper=helper,events=events,source_descriptor=d,source_args=args,tx=tr,rx=rr,receiver=result,decision=endpoint_decision(scores,complete=True,simulated=False,helper_enabled=helper=='on',log_threshold=22),receiver_wall_seconds=time.perf_counter()-wall,receiver_cpu_seconds=time.process_time()-cpu,receiver_expected_bit=0,pcm_peak=float(abs(pcm).max()),noisy_output_samples=len(rx))
   report['results'].append(entry);print(json.dumps({k:entry[k] for k in ['seed','helper','events','decision','receiver_cpu_seconds']}),flush=True)
   (D/'results.partial.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
  s.close()
report['wall_seconds']=time.perf_counter()-start
(D/'results.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
(D/'results.partial.json').unlink()
