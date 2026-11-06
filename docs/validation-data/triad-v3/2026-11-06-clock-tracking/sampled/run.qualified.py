import os,sys,json,time,hashlib,subprocess,math,resource,argparse
from pathlib import Path
import numpy as np
import torch
ROOT=Path.cwd();D=ROOT/'local-evidence/triad-tracking-200/sampled';sys.path.insert(0,str(ROOT/'tools'))
from triad_continuous import NativeStream,carrier_phase,source_basis,endpoint_decision,tracking_source_basis,make_timing_forward
from triad_incremental_receiver import IncrementalReceiver
from triad_tracking_receiver import TrackingReceiver
from triad_audio_mapping import mapped_phase
P=json.loads((D/'protocol.json').read_text());torch.set_num_threads(2)
a=argparse.ArgumentParser();a.add_argument('--seed',type=int,default=94001);a.add_argument('--helper',default='off');a.add_argument('--cap',type=int,default=200);a.add_argument('--tx-events',action='store_true');a.add_argument('--rx-events',action='store_true');a.add_argument('--channel',action='store_true');a.add_argument('--erase',action='store_true');a.add_argument('--methods',default='fixed_nominal,fixed_mapped,tracking');a.add_argument('--cpu-limit',type=float,default=280);a.add_argument('--seconds',type=float,default=60);a.add_argument('--cn0',type=float,default=8);a.add_argument('--long-events',action='store_true');a.add_argument('--tag',required=True);a=a.parse_args()
OUT=D/a.tag;OUT.mkdir(exist_ok=False)
report={'protocol_sha256':hashlib.sha256((D/'protocol.json').read_bytes()).hexdigest(),'arguments':vars(a),'results':[], 'identities':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in ['build/dev-cli/triad_helper_export','include/datapump/continuous_clock.hpp','src/resampler.cpp','tools/triad_continuous.py','tools/triad_tracking_receiver.py','tools/triad_timing_forward.py','tools/triad_incremental_receiver.py','tools/triad_audio_mapping.py','tools/triad_tracking_basis.py','local-evidence/triad-tracking-200/sampled/run.py','local-evidence/triad-tracking-200/sampled/warp.cpp','local-evidence/triad-tracking-200/sampled/warp']}}
clockticks=os.sysconf('SC_CLK_TCK')
def native_cpu(s):
 try:
  fields=Path(f'/proc/{s.process.pid}/stat').read_text().split();return (int(fields[13])+int(fields[14]))/clockticks
 except (FileNotFoundError,AttributeError):return 0.
def elapsed_cpu(s):
 child=resource.getrusage(resource.RUSAGE_CHILDREN);return time.process_time()+child.ru_utime+child.ru_stime+native_cpu(s)
def save():
 (OUT/'results.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
def warp(x,side,events):
 mp=OUT/f'{side}-map.f64'
 p=subprocess.run([str(D/'warp'),side,('events-long' if a.long_events else 'events') if events else 'stable',str(a.cap),str(mp) if side=='rx' else '-'],input=np.asarray(x,dtype='<f4').tobytes(),capture_output=True,check=True,timeout=30)
 return np.frombuffer(p.stdout,'<f4'),json.loads(p.stderr),np.fromfile(mp,'<f8') if side=='rx' else None
args=['--case','hf','--sample-rate','6000','--rate','36','--added-symbols','10','--seconds',str(a.seconds),'--key-seed',str(a.seed),'--epoch','1793850000','--base-oscillator','gpsdo-ocxo','--shift-oscillator','gpsdo-ocxo','--oscillator-effects','off','--arrival-seconds','0','--helper',a.helper]
if a.channel: args+=['--seed',str(a.seed),'--spread-preset','hf-worst','--selective-perturbation','each-65','--margin','3','--channel-evolution','stochastic','--random-doppler-width-hz','30','--delay-excursion-ms','0.001','--delay-knot-seconds','1000','--cn0','200']
else: args+=['--spread-preset','none','--selective-perturbation','off']
if a.channel and a.tx_events: raise ValueError('TX warp after channel is prohibited')
s=None;start=time.perf_counter()
try:
 with (OUT/'native.log').open('x') as log:
  s=NativeStream(ROOT/'build/dev-cli/triad_helper_export',args,kind='channel' if a.channel else 'emitted',log=log)
  begin_cpu=elapsed_cpu(s);d=s.descriptor;fs=int(d['sample_rate_hz']);n=int(d['total_samples']);chunks=[];masks=[]
  report.update(source_args=args,source_descriptor=d,source_plan=s.plan,extension_protocol_sha256=hashlib.sha256((D/'extension-protocol.json').read_bytes()).hexdigest())
  while s.next_sample<n:
   v,m=s.next(min(16384,n-s.next_sample));chunks.append(v);masks.append(m)
  clean=np.concatenate(chunks);source_valid=np.concatenate(masks)
  pcm=np.concatenate([np.sqrt(2)*np.real(clean*np.exp(1j*carrier_phase(0,n,d['data_if_hz'],fs))),np.zeros(2*fs)]).astype('<f4');N=n+fs
  if a.channel:
   # Negligible native channel noise is retained; explicit real thermal noise follows.
   noise=np.random.default_rng(a.seed+100000).normal(0,math.sqrt(fs/(2*10**(.1*a.cn0))),len(pcm)).astype('<f4')
   tx=pcm+noise;tr={'applied':False,'reason':'RX-only, channel precedes RX ASRC; no TX warp after propagation'};N=n
  else:
   tx,tr,_=warp(pcm,'tx',a.tx_events)
   noise=np.random.default_rng(a.seed+100000).normal(0,math.sqrt(fs/(2*10**(.1*a.cn0))),len(pcm)+100).astype('<f4')
   tx=tx+noise[:len(tx)]
  rx,rr,positions=warp(tx,'rx',a.rx_events)
  if len(rx)<N:raise RuntimeError('Insufficient real observed samples; padding not fabricated')
  report.update(tx=tr,rx=rr,source_waveform_sha256=hashlib.sha256(clean.tobytes()).hexdigest(),preparation_cpu_seconds=elapsed_cpu(s)-begin_cpu,preparation_wall_seconds=time.perf_counter()-start)
  report['source_energy']={'complex_full_waveform_energy_seconds':float(np.vdot(clean,clean).real/fs),'mean_complex_power':float(np.mean(np.abs(clean)**2)),'scope':'Complete emitted or channel DATA+Helper including native thermal noise; no per-realization normalization; not DATA-only when Helper on'}
  if a.channel:
   spectrum=np.fft.fft(clean,norm='ortho');selected=np.abs(np.fft.fftfreq(len(clean),1/fs))<=160
   report['source_energy']['whole_window_in_band_energy_seconds']=float(np.vdot(spectrum[selected],spectrum[selected]).real/fs)
   report['source_energy']['in_band_energy_scope']='Whole-window Fourier±160Hz, before IF reconstruction/ASRC; not exactly the receiver block projection'
   report['channel_pair_protocol_sha256']=hashlib.sha256((D/'channel-pair-protocol.json').read_bytes()).hexdigest()
   del spectrum,selected
  positions=positions[:N];rx=rx[:N];valid=np.ones(N,np.uint8)
  if a.channel:
   # Propagate native declared erasures to nearest input coordinate; native mask is
   # measured here to avoid claiming unavailable rows as observations.
   ids=np.minimum(np.floor(positions).astype(np.int64),n-1);valid=source_valid[ids]
  if a.erase:valid[(positions/fs>=67)&(positions/fs<69)]=0
  common=dict(sample_rate=fs,samples=N,block_samples=round(P['public_receiver']['block_seconds']*fs),required_bands=[[-160,160]],excluded_bands=[],noise_variance=fs/(10**(.1*a.cn0))+(2*float(d['noise_variance']) if a.channel else 0),stationary_power=[1.],coherence_seconds=[.5,2.,float('inf')],precision='fp32',projection_mode='dft')
  offset=d['data_offset_samples']/fs+(d.get('capture_lead_seconds',0) if a.channel else 0)
  for method in a.methods.split(','):
   bank={'coordinates':[[0,0,0,0]],'tracking':{'maximum_states':64,'maximum_proposals':256,'rate_step_ppm':50,'maximum_acceleration_ppm_per_second':20,'segment_seconds':4}}
   forward,seg=make_timing_forward(bank,d,3)
   provider=tracking_source_basis(s,bank['coordinates'],[[0,0]],offset,forward,d['data_if_hz'])
   if method=='tracking':r=TrackingReceiver(**common,segment_samples=round(seg*fs),basis_provider=provider,timing_forward=forward,maximum_input_chunk_samples=3000,require_physical_map=True)
   elif method=='fixed_nominal':r=IncrementalReceiver(**common,basis=source_basis(s,[[0,0,0,0]],[[0,0]],offset),candidate_count=1)
   elif method=='fixed_mapped':
    def basis(lo,hi):
     origin=int(math.floor(positions[lo]));return provider(forward.states,0,lo,hi,origin,positions[lo:hi]-origin)
    r=IncrementalReceiver(**common,basis=basis,candidate_count=1)
   else:raise ValueError(method)
   cpu0=elapsed_cpu(s);wall0=time.perf_counter();traces=[]
   for first in range(0,N,3000):
    stop=min(first+3000,N);origin=int(math.floor(positions[first]));off=positions[first:stop]-origin
    phase=carrier_phase(first,stop-first,d['data_if_hz'],fs) if method=='fixed_nominal' else mapped_phase(origin,off,d['data_if_hz'],fs)
    values=np.sqrt(2)*rx[first:stop]*np.exp(-1j*phase)
    if method=='tracking':r.push(first,values,valid[first:stop],physical_origin=origin,physical_offsets=off)
    else:r.push(first,values,valid[first:stop])
    if method=='tracking' and r.segment_updates>len(traces):
     traces.append(r.progress());print(json.dumps({'method':method,'segment':r.segment_updates,'bit_scores':traces[-1]['bit_scores'],'cpu':elapsed_cpu(s)-cpu0,'wall':time.perf_counter()-wall0}),flush=True)
    if elapsed_cpu(s)-begin_cpu>a.cpu_limit:
     report.update(stopped='first-ladder CPU budget exceeded',partial_method=method,partial_progress=r.progress(),partial_traces=traces);save();raise SystemExit(3)
   result=r.finish();
   if method!='fixed_nominal':result['basis_cache']=provider.cache_report()
   entry={'method':method,'receiver':result,'decision':endpoint_decision(result['bit_scores'],complete=True,simulated=False,helper_enabled=a.helper=='on',log_threshold=22),'receiver_cpu_seconds_including_native_basis':elapsed_cpu(s)-cpu0,'receiver_wall_seconds':time.perf_counter()-wall0,'traces':traces}
   report['results'].append(entry);save();print(json.dumps({k:entry[k] for k in ['method','decision','receiver_cpu_seconds_including_native_basis','receiver_wall_seconds']}),flush=True)
  report['total_cpu_seconds']=elapsed_cpu(s)-begin_cpu;report['total_wall_seconds']=time.perf_counter()-start;save()
finally:
 if s:s.close()
