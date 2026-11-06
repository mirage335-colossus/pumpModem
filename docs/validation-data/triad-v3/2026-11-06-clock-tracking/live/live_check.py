"""One predeclared default-clock cable check; no host-clock or mixer edits."""
import argparse
import hashlib
import json
import os
import resource
import subprocess
import sys
import threading
import time
from pathlib import Path

import numpy as np
import torch

ROOT = Path.cwd()
if not (ROOT/'tools/triad_continuous.py').is_file():
    raise RuntimeError('run this evidence script from the repository root')
sys.path.insert(0, str(ROOT/'tools'))
from triad_audio_mapping import MappedCaptureReader, mapped_phase, rescale_coordinates
from triad_continuous import NativeStream, carrier_phase, make_timing_forward, tracking_source_basis
from triad_tracking_receiver import TrackingReceiver

OUT = Path(__file__).parent
ARGS = ['--case','hf','--sample-rate','6000','--rate','36','--added-symbols','10',
        '--seconds','32','--key-seed','73005','--epoch','1793850000',
        '--spread-preset','none','--selective-perturbation','off',
        '--base-oscillator','gpsdo-ocxo','--shift-oscillator','gpsdo-ocxo',
        '--oscillator-effects','off','--arrival-seconds','0','--helper','on']
BANK = {'coordinates': [[0,.95+.025*k,0,0] for k in range(13)],
        'taps': [[0,0]], 'tracking': {'segment_seconds': 4., 'maximum_states': 64,
        'maximum_proposals': 256}}
GAIN = .003

def build_receiver(source, total):
    d = source.descriptor
    forward, segment_seconds = make_timing_forward(BANK, d, 2)
    fs = int(d['sample_rate_hz'])
    return TrackingReceiver(sample_rate=fs, samples=total, block_samples=3000,
        segment_samples=round(segment_seconds*fs), timing_forward=forward,
        basis_provider=tracking_source_basis(source, BANK['coordinates'], BANK['taps'],
            d['data_offset_samples']/fs, forward, d['data_if_hz']),
        required_bands=[[-150,150]], excluded_bands=[[-1500,-1100]], noise_variance=1.,
        stationary_power=[1.], coherence_seconds=[.5,float('inf')], precision='fp32',
        projection_mode='dft', memory_limit_bytes=224*1024**2,
        maximum_input_chunk_samples=1500, require_physical_map=True)

def usage(*live_processes):
    r = resource.getrusage(resource.RUSAGE_CHILDREN)
    total=r.ru_utime+r.ru_stime
    for p in live_processes:
        if p is not None and p.poll() is None:
            fields=Path(f'/proc/{p.pid}/stat').read_text().rsplit(')',1)[1].split()
            total+=(int(fields[11])+int(fields[12]))/os.sysconf('SC_CLK_TCK')
    return total

def main():
    global OUT
    cli=argparse.ArgumentParser();cli.add_argument('--preflight-only',action='store_true')
    cli.add_argument('--tag',required=True);a=cli.parse_args()
    if not a.tag or Path(a.tag).name!=a.tag:raise ValueError('one output directory basename required')
    OUT=OUT/a.tag;OUT.mkdir(exist_ok=False)
    torch.set_num_threads(2)
    identities={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
        [ROOT/'build/dev-cli/triad_audio_stream',ROOT/'build/dev-cli/triad_helper_export',
         *(ROOT/'tools'/p for p in ('triad_continuous.py','triad_audio_mapping.py',
             'triad_tracking_receiver.py','triad_timing_forward.py','triad_incremental_receiver.py',
             'triad_tracking_basis.py')),
         Path(__file__)]}
    (OUT/'protocol.json').write_text(json.dumps(dict(configuration=ARGS,bank=BANK,gain=GAIN,
        expected_bit_evaluation_only=0, source_build_identities=identities,
        audio_clock_flags='omitted: default auto',physical_null_qualified=False,
        scope='Functional default full-audio/map/tracking cable check, not marginal sensitivity or multi-day uptime.'),indent=2)+'\n')
    log=(OUT/'native.stderr.log').open('x')
    rx=NativeStream(ROOT/'build/dev-cli/triad_helper_export',ARGS,kind='emitted',log=log)
    tx=None;capture=None;playback=None;thread=None
    d=rx.descriptor;fs=int(d['sample_rate_hz']);n=int(d['total_samples']);total=n+2*fs
    try:
        # Four real DATA-template segments exercise proposal growth, without
        # touching hardware or using payload evidence to choose the bank.
        probe=build_receiver(rx,16*fs);start=time.perf_counter();cpu=time.process_time();child=usage(rx.process)
        for first in range(0,16*fs,1500):
            probe.push(first,np.zeros(1500,np.complex64),physical_origin=50*fs+first,
                       physical_offsets=np.arange(1500,dtype=np.float64))
            if (first+1500)%(4*fs)==0:
                progress=probe.progress()
                (OUT/'preflight-progress.json').write_text(json.dumps(progress,indent=2)+'\n')
                print('preflight segment',progress['segments'],progress['maximum_segment_wall_seconds'],flush=True)
        preflight=dict(receiver=probe.finish(),template_cache=probe.basis_provider.cache_report(),wall_seconds=time.perf_counter()-start,
            python_cpu_seconds=time.process_time()-cpu,child_cpu_seconds=usage(rx.process)-child)
        (OUT/'preflight.json').write_text(json.dumps(preflight,indent=2)+'\n')
        print('preflight',json.dumps({k:v for k,v in preflight.items() if k!='receiver'}),flush=True)
        if preflight['receiver']['maximum_segment_wall_seconds']>=4:
            raise RuntimeError('preflight cannot sustain declared four-second processing segments')
        if a.preflight_only:return
        receiver=build_receiver(rx,total)
        tx=NativeStream(ROOT/'build/dev-cli/triad_helper_export',ARGS,kind='emitted',log=log)
        audio=str(ROOT/'build/dev-cli/triad_audio_stream')
        failure=[];sent=[0];received=0;peak=0.;started=time.perf_counter();cpu=time.process_time();child=usage(rx.process,tx.process)
        first_origin=last_origin=None;last_offsets=None;map_rate=None
        def send():
            try:
                while sent[0]<n:
                    values,_=tx.next(min(4096,n-sent[0]))
                    phase=carrier_phase(sent[0],len(values),d['data_if_hz'],fs)
                    pcm=(np.sqrt(2)*GAIN*(values*np.exp(1j*phase)).real).astype('<f4')
                    playback.stdin.write(pcm.tobytes());sent[0]+=len(values)
                playback.stdin.close()
            except BaseException as e:failure.append(repr(e))
        try:
            capture=subprocess.Popen([audio,'--mode','capture','--sample-rate',str(fs),
                '--frames',str(total),'--device','hw:1,0','--exclusive','--capture-format','mapped-v1'],
                stdout=subprocess.PIPE,stderr=log)
            reader=MappedCaptureReader(capture.stdout)
            while received<total:
                count=min(1500,total-received)
                pcm,origin,offsets,map_rate=reader.read(count)
                if first_origin is None:first_origin=origin
                last_origin,last_offsets=origin,offsets
                peak=max(peak,float(abs(pcm).max()))
                values=pcm*np.sqrt(2)/GAIN*np.exp(-1j*mapped_phase(origin,offsets,d['data_if_hz'],map_rate))
                rebased,relative=rescale_coordinates(origin,offsets,map_rate,fs)
                receiver.push(received,values,physical_origin=rebased,physical_offsets=relative)
                received+=count
                if playback is None and received>=fs:
                    playback=subprocess.Popen([audio,'--mode','play','--sample-rate',str(fs),
                        '--frames',str(n),'--device','hw:1,0','--exclusive','--background-noise-bits','14'],
                        stdin=subprocess.PIPE,stderr=log)
                    thread=threading.Thread(target=send);thread.start()
                if received%(12*fs)==0:
                    print('capture',received,receiver.progress()['bit_scores'],flush=True)
            if thread:thread.join(10)
            for proc in (capture,playback):
                if proc and proc.wait(timeout=10):failure.append('audio exit '+str(proc.returncode))
        except BaseException as e:
            failure.append(repr(e))
        finally:
            for proc in (capture,playback):
                if proc and proc.poll() is None:proc.terminate();proc.wait(timeout=3)
            if thread:thread.join(3)
        result=receiver.finish()
        report=dict(schema='triad-clock-tracking-cable-v1',identities=identities,configuration=ARGS,
            bank=BANK,source=d,receiver=result,frames_sent=sent[0],frames_received=received,
            peak_capture=peak,errors=failure,wall_seconds=time.perf_counter()-started,
            template_cache=receiver.basis_provider.cache_report(),
            python_cpu_seconds=time.process_time()-cpu,child_cpu_seconds=usage(rx.process,tx.process)-child,
            native_mapping=dict(first_origin=first_origin,last_origin=last_origin,
                last_offset=None if last_offsets is None else float(last_offsets[-1]),
                native_rate=map_rate,packets=reader.packets),
            physical_null_qualified=False,operational_admission=False,expected_bit_evaluation_only=0,
            scope='Default full-audio clock/map/tracking cable check with public historical latency bank; no host-clock injection or inference of true TX warp; not a marginal or multi-day qualification.')
        report['identities_end']={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in identities}
        (OUT/'live.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps({k:report[k] for k in ('frames_sent','frames_received','errors','wall_seconds','python_cpu_seconds','child_cpu_seconds')}),flush=True)
        print(result['bit_scores'],flush=True)
        if failure:raise RuntimeError('live failures retained in live.json')
    finally:
        if tx:tx.close()
        rx.close();log.close()

if __name__=='__main__':main()
