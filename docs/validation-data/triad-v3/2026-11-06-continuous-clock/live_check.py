import sys,json,time,subprocess,threading,resource,hashlib,os
from pathlib import Path
import numpy as np
import torch
root=Path.cwd();sys.path.insert(0,str(root/'tools'))
from triad_continuous import NativeStream,carrier_phase,source_basis,read_exact
from triad_incremental_receiver import IncrementalReceiver
p=root/'local-evidence/continuous-audio-clock'/os.environ.get('TRIAD_LIVE_TAG','live-continuous');p.mkdir(exist_ok=True);torch.set_num_threads(2)
identities={str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [root/'build/dev-cli/triad_audio_stream',root/'build/dev-cli/triad_helper_export',root/'tools/triad_incremental_receiver.py',root/'tools/triad_continuous.py']}
args=['--case','hf','--sample-rate','6000','--rate','36','--added-symbols','10','--seconds','32','--key-seed','73005','--epoch','1793850000','--spread-preset','none','--selective-perturbation','off','--base-oscillator','gpsdo-ocxo','--shift-oscillator','gpsdo-ocxo','--oscillator-effects','off','--arrival-seconds','0','--helper','on']
log=(p/'live.stderr.log').open('x');tx=NativeStream(root/'build/dev-cli/triad_helper_export',args,kind='emitted',log=log);rx=NativeStream(root/'build/dev-cli/triad_helper_export',args,kind='emitted',log=log)
d=tx.descriptor;fs=int(d['sample_rate_hz']);n=int(d['total_samples']);total=n+2*fs;gain=.003
coordinates=[[0,.95+.025*k,0,0] for k in range(13)]
receiver=IncrementalReceiver(sample_rate=fs,samples=total,block_samples=3000,basis=source_basis(rx,coordinates,[[0,0]],d['data_offset_samples']/fs),required_bands=[[-150,150]],excluded_bands=[[-1500,-1100]],noise_variance=1.,stationary_power=[1.],coherence_seconds=[.5,float('inf')],candidate_count=len(coordinates),precision='fp32',projection_mode='dft')
audio=str(root/'build/dev-cli/triad_audio_stream');clock_flags=['--clock-discipline',os.environ.get('TRIAD_CLOCK_MODE','continuous')];capture=playback=None;thread=None;failure=[];sent=[0];received=0;peak=0.;started=time.perf_counter();c=time.process_time();before=resource.getrusage(resource.RUSAGE_CHILDREN)
def send():
 try:
  while sent[0]<n:
   values,_=tx.next(min(4096,n-sent[0]));phase=carrier_phase(sent[0],len(values),d['data_if_hz'],fs)
   pcm=(np.sqrt(2)*gain*(values*np.exp(1j*phase)).real).astype('<f4');playback.stdin.write(pcm.tobytes());sent[0]+=len(values)
  playback.stdin.close()
 except BaseException as e:failure.append(repr(e))
try:
 capture=subprocess.Popen([audio,'--mode','capture','--sample-rate',str(fs),'--frames',str(total),'--device','hw:1,0','--exclusive',*clock_flags],stdout=subprocess.PIPE,stderr=log)
 while received<total:
  count=min(1500,total-received);pcm=np.frombuffer(read_exact(capture.stdout,count*4),'<f4');peak=max(peak,float(abs(pcm).max()))
  values=pcm*np.sqrt(2)/gain*np.exp(-1j*carrier_phase(received,count,d['data_if_hz'],fs));receiver.push(received,values);received+=count
  if playback is None and received>=fs:
   playback=subprocess.Popen([audio,'--mode','play','--sample-rate',str(fs),'--frames',str(n),'--device','hw:1,0','--exclusive','--background-noise-bits','14',*clock_flags],stdin=subprocess.PIPE,stderr=log)
   thread=threading.Thread(target=send);thread.start()
 if thread:thread.join(10)
 for proc in (capture,playback):
  if proc and proc.wait(timeout=10):failure.append('audio exit '+str(proc.returncode))
 r=receiver.finish()
finally:
 for proc in (capture,playback):
  if proc and proc.poll() is None:proc.terminate();proc.wait(timeout=3)
 if thread:thread.join(3)
 endtx=tx.close();endrx=rx.close();log.close()
used=resource.getrusage(resource.RUSAGE_CHILDREN)
report=dict(identities=identities,schema='triad-live-stream-check-v1',configuration=args,clock_mode=os.environ.get('TRIAD_CLOCK_MODE','continuous'),source=d,coordinates=coordinates,receiver=r,frames_sent=sent[0],frames_received=received,peak_capture=peak,errors=failure,wall_seconds=time.perf_counter()-started,python_cpu_seconds=time.process_time()-c,child_cpu_seconds=used.ru_utime+used.ru_stime-before.ru_utime-before.ru_stime,source_completion=endtx,physical_null_qualified=False,expected_bit_evaluation_only=0,scope='Paired full-audio clock mode cable check with unchanged predeclared timing bank and Helper; no injected OS clock changes, not marginal sensitivity or multi-day uptime; operational admission withheld.')
(p/'live.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k]for k in ('frames_sent','frames_received','errors','wall_seconds','python_cpu_seconds','child_cpu_seconds')}));print(r['bit_scores'])
