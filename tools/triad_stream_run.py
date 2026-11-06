#!/usr/bin/env python3
"""Continuous public-calendar triad replay/raw-ALSA experiment.

This is a scheduled one-bit-train experiment, not generic live acquisition or
completed-message reception. It never reads evaluator truth. Real-audio image
folding, fixed frontend projection and hardware noise remain in observations;
the working Gaussian admission receipt is not a qualified physical-null bound.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import resource
import selectors
import shutil
import subprocess
import time

import numpy as np

from triad_live_alsa import parse_hw_format, pcm_s16, _stop
from triad_live_waveforms import to_audio
from triad_stream_frontend import StreamingAudioDDC
from triad_stream_scheduler import StreamingWindowScheduler, WindowRequest
from triad_stream_slots import load_slots
from triad_background_noise import add_arguments as add_background_noise_arguments
from triad_background_noise import apply as apply_background_noise
from triad_background_noise import enabled as background_noise_enabled
from triad_background_noise import validate as validate_background_noise


def digest(path):
    value=hashlib.sha256()
    with Path(path).open('rb')as stream:
        while block:=stream.read(1<<20):value.update(block)
    return value.hexdigest()


def write_json(path,value):
    with Path(path).open('x')as stream:json.dump(value,stream,indent=2,allow_nan=False);stream.write('\n')


def public_audio_windows(slots,frontend,playback_offset_frames):
    """Shift public windows by known digital offset/FIR delay, never fitted lag."""
    if isinstance(playback_offset_frames,bool)or int(playback_offset_frames)!=playback_offset_frames or playback_offset_frames<0:
        raise ValueError('nonnegative integer public playback offset required')
    total=int(playback_offset_frames)+frontend.group_delay_samples
    shift,remainder=divmod(total,frontend.decimation)
    residual=remainder/frontend.input_rate
    adjusted=[];requests=[]
    for index,(manifest,templates,epochs)in enumerate(slots):
        m=copy.deepcopy(manifest)
        m['calendar']['capture_lead_seconds']+=residual
        start=int(m['stream_window']['absolute_start_sample'])+shift
        length=int(m['calendar']['capture_samples'])
        m['stream_audio_adapter']=dict(public_playback_offset_frames=int(playback_offset_frames),
            integer_window_shift_samples=shift,residual_lead_adjustment_seconds=residual,
            combined_undo_gain=frontend.gain,
            scope='Known digital scheduling/FIR delay only; analog playback/capture latency remains in broad public arrival search. No measured payload alignment.')
        adjusted.append((m,templates,epochs));requests.append(WindowRequest('slot-'+str(index),start,length))
    return adjusted,requests


class StreamSession:
    """Persistent DDC and bounded scheduler; receiver construction runs on worker."""
    def __init__(self,original,slots,output,*,fixed_gain=.002,analog_gain=1.,
                 playback_offset_frames=0,receiver_factory=None,noise_inflation=1.,
                 queue_capacity=2,pass_edge_hz=1450.):
        self.output=Path(output);self.output.mkdir(parents=True,exist_ok=True)
        self.original=original
        self.frontend=StreamingAudioDDC(48000,int(original['sample_rate_hz']),
            original['channel']['nominal_conversion_if_hz'],pass_edge_hz=pass_edge_hz,
            undo_fixed_gain=fixed_gain*analog_gain)
        self.slots,requests=public_audio_windows(slots,self.frontend,playback_offset_frames)
        self.receiver_factory=receiver_factory
        self.noise_inflation=noise_inflation
        self.scheduler=StreamingWindowScheduler(requests,self._score,
            ring_capacity_samples=2*max(r.length for r in requests),queue_capacity=queue_capacity)
        self.ddc_cpu=0.;self.ddc_wall=0.;self.pcm_frames=0
        self.peaks=np.zeros(2);self.clips=np.zeros(2,np.int64)
        self.reads=[];self.read_count=0;self.started=time.perf_counter()

    def _score(self,index,raw,valid):
        c,w=time.process_time(),time.perf_counter()
        if self.receiver_factory is None:
            from triad_weak_receiver import WeakReceiver
            receiver=WeakReceiver(*self.slots[index],noise_inflation=self.noise_inflation,
                admission_policy=self.slots[index][0].get('weak_admission_policy'))
        else:receiver=self.receiver_factory(*self.slots[index])
        preparation=dict(cpu_seconds=time.process_time()-c,wall_seconds=time.perf_counter()-w)
        result=receiver.score(raw,valid)
        result['stream_receiver_preparation']=preparation
        result['stream_adapter']=self.slots[index][0]['stream_audio_adapter']
        result['stream_scope']='Public scheduled one-bit window; physical null and completed-message semantics not qualified'
        destination=self.output/('slot-'+str(index)+'-receiver.json')
        write_json(destination,result) # No evaluator truth is opened anywhere in this tool.
        return dict(file=destination.name,sha256=digest(destination),status=result.get('status'),
            bit=result.get('bit'),score=result.get('score'),margin=result.get('margin'),
            conditional_model_pass=bool(result.get('operational',{}).get('passes_conditional_bound',False)),
            physical_null_qualified=False,receiver_preparation=preparation,
            receiver_timing=result.get('timing'))

    def feed_stereo_s16(self,block):
        if len(block)%4:raise ValueError('partial stereo S16 frame')
        stereo=np.frombuffer(block,dtype='<i2').reshape(-1,2)
        if not len(stereo):return
        values=stereo.astype(np.float32)/np.float32(32768.)
        self.peaks=np.maximum(self.peaks,np.max(abs(values),axis=0))
        self.clips+=np.sum(abs(values)>=.98,axis=0)
        c,w=time.process_time(),time.perf_counter()
        iq,valid,receipt=self.frontend.process(values[:,0])
        self.ddc_cpu+=time.process_time()-c;self.ddc_wall+=time.perf_counter()-w
        if len(iq):
            first=receipt['first_output_input_sample']//self.frontend.decimation
            self.scheduler.append(iq,valid,start_sample=first)
        self.pcm_frames+=len(stereo);self.read_count+=1
        if len(self.reads)<10000:self.reads.append(dict(frames=len(stereo),frontier=self.pcm_frames,
            wall_seconds=time.perf_counter()-self.started))

    def finish(self,timeout=180.):
        # Preserve full numeric tails, but mark them unobserved/invalid through
        # the frontend and scheduler. They cannot complete absent-symbol tests.
        iq,valid,r=self.frontend.flush()
        if len(iq):self.scheduler.append(iq,valid,start_sample=r['first_output_input_sample']//self.frontend.decimation)
        schedule=self.scheduler.close(timeout)
        pending=[dict(slot=r['index'],bit=r['result']['bit'],conditional_model_pass=r['result']['conditional_model_pass'])
            for r in schedule['records']if r['status']=='scored'and r.get('result')and r['result'].get('status')=='scored']
        return dict(frontend=self.frontend.metadata(),ddc_timing=dict(process_cpu_seconds=self.ddc_cpu,
            wall_seconds=self.ddc_wall,scope='Process deltas overlap asynchronous receiver work; wall is frontend call duration.'),
            schedule=schedule,capture=dict(frames=self.pcm_frames,seconds=self.pcm_frames/48000,
                stereo_peak=self.peaks.tolist(),stereo_near_fullscale=self.clips.tolist(),
                read_events=self.read_count,retained_read_events=self.reads,
                read_events_truncated=self.read_count>len(self.reads)),pending_candidate_bits=pending,
            message_completed=False,physical_null_qualified=False)


def prepare_pcm(directory,original,destination,*,fixed_gain=.002,observation_file=None):
    directory=Path(directory);shape=tuple(original['observation']['shape'])
    path=Path(observation_file)if observation_file is not None else directory/original['observation']['file']
    if path.stat().st_size!=math.prod(shape)*16:raise ValueError('native observation size differs')
    validity=np.memmap(directory/original['validity']['file'],dtype='u1',mode='r',shape=shape)
    if not np.all(validity==1):raise ValueError('source train has erasures; this audio replay adapter requires complete source input')
    raw=np.memmap(path,dtype='<c16',mode='r',shape=shape)
    pcm,receipt=to_audio(raw,original['sample_rate_hz'],48000,
        original['channel']['nominal_conversion_if_hz'],occupied_bandwidth_hz=2900.,fixed_gain=fixed_gain)
    with Path(destination).open('xb')as stream:
        for start in range(0,len(pcm),65536):
            mono=pcm_s16(pcm[start:start+65536]);stream.write(np.repeat(mono[:,None],2,axis=1).tobytes())
    receipt.update(file=str(destination),sha256=digest(destination),stereo_frames=len(pcm),
        observation_file=str(path),observation_sha256=digest(path),
        whole_input_retained=True,
        limitation='Nominal2900Hz bridge geometry does not prefilter native6kHz complex white noise. Real-image folding and every actual sideband remain; low-pilot noise may differ from native analytic model.')
    return receipt


def replay_pcm(session,path,*,preroll_frames=0,postroll_frames=0,chunk_frames=2400,realtime=False):
    start=time.perf_counter();fed=0
    def feed(block):
        nonlocal fed
        if realtime:
            remaining=start+fed/48000-time.perf_counter()
            if remaining>0:time.sleep(remaining)
        session.feed_stereo_s16(block);fed+=len(block)//4
    for total in (preroll_frames,):
        while total:n=min(total,chunk_frames);feed(bytes(n*4));total-=n
    with Path(path).open('rb')as stream:
        while block:=stream.read(chunk_frames*4):feed(block)
    total=postroll_frames
    while total:n=min(total,chunk_frames);feed(bytes(n*4));total-=n
    return dict(mode='replay',realtime_pacing=realtime,frames=fed,
        scope='Single continuous prepared/captured PCM stream; source-padding zeros are synthetic, not hardware observations.')


def live_pcm(session,tx_path,output,*,device='hw:1,0',preroll_frames=48000,
             postroll_frames=96000,chunk_frames=2400,max_seconds=600.,record_pcm=False):
    if not re.fullmatch(r'hw:[A-Za-z0-9_-]+,[0-9]+(?:,[0-9]+)?',device):raise ValueError('explicit hw:C,D required')
    executables={name:shutil.which(name)for name in ('arecord','aplay')}
    if not all(executables.values()):raise ValueError('arecord and aplay are required')
    output=Path(output);capture_log=output/'capture.stderr.log';playback_log=output/'playback.stderr.log'
    frames=Path(tx_path).stat().st_size//4+preroll_frames+postroll_frames
    common=['-c','2','-r','48000','-f','S16_LE','-t','raw','--fatal-errors','-v',
        '--disable-resample','--disable-channels','--disable-format','--disable-softvol']
    capture_argv=[executables['arecord'],'-D',device,*common,'--samples',str(frames)]
    playback_argv=[executables['aplay'],'-D',device,*common,str(tx_path)]
    report=dict(mode='live',argv=dict(capture=capture_argv,playback=playback_argv),
        requested_frames=frames,status='incomplete',failure=None,discontinuity={},audio_format={},
        timing_scope='Consumed ADC frame count at playback Popen is a public scheduling estimate, not an analog timestamp or fitted lag.')
    capture=playback=None;selector=selectors.DefaultSelector();carry=b''
    started=time.perf_counter();deadline=started+max_seconds
    children_before=resource.getrusage(resource.RUSAGE_CHILDREN)
    with capture_log.open('xb')as cl,playback_log.open('xb')as pl:
        recording=(output/'capture.stereo.s16le').open('xb')if record_pcm else None
        try:
            capture=subprocess.Popen(capture_argv,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=cl)
            os.set_blocking(capture.stdout.fileno(),False);selector.register(capture.stdout,selectors.EVENT_READ)
            while True:
                if time.perf_counter()>deadline:raise TimeoutError('live capture exceeded declared deadline')
                if playback is None and session.pcm_frames>=preroll_frames:
                    report['audio_format']['capture']=parse_hw_format(capture_log.read_text(errors='replace'),48000,'CAPTURE')
                    report['playback_call']=dict(capture_frame_lower_bound=session.pcm_frames,
                        elapsed_seconds=time.perf_counter()-started)
                    playback=subprocess.Popen(playback_argv,stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=pl)
                if playback is not None and playback.poll()not in (None,0):raise RuntimeError('aplay failed: '+str(playback.returncode))
                events=selector.select(.25)
                if not events:
                    if capture.poll()is not None:break
                    continue
                limit=chunk_frames
                if playback is None:limit=min(limit,preroll_frames-session.pcm_frames)
                block=os.read(capture.stdout.fileno(),max(1,limit*4-len(carry)))
                if not block:break
                carry+=block;complete=len(carry)//4*4
                if complete:
                    value=carry[:complete];carry=carry[complete:]
                    if recording:recording.write(value)
                    session.feed_stereo_s16(value)
            if carry:raise ValueError('partial stereo capture frame')
            capture.wait(timeout=5)
            if playback is None:raise RuntimeError('capture ended before playback')
            playback.wait(timeout=max(1.,deadline-time.perf_counter()))
            if capture.returncode or playback.returncode:raise RuntimeError('audio process failed')
            if session.pcm_frames!=frames:raise RuntimeError('capture frame count differs from declaration')
            report['status']='captured'
        except Exception as error:report['failure']=type(error).__name__+': '+str(error)
        finally:
            selector.close()
            report['cleanup_errors']=[e for e in (_stop(playback),_stop(capture))if e]
            if capture is not None and capture.stdout is not None:capture.stdout.close()
            if recording:recording.close()
        for name,path,direction,proc in [('capture',capture_log,'CAPTURE',capture),('playback',playback_log,'PLAYBACK',playback)]:
            log=path.read_text(errors='replace')
            report['discontinuity'][name]=bool(re.search(r'\b(?:overrun|underrun|xrun)\b',log,re.I))
            report[name+'_returncode']=None if proc is None else proc.returncode
            try:report['audio_format'][name]=parse_hw_format(log,48000,direction)
            except ValueError as error:
                if report['failure']is None:report['failure']=str(error)
        if any(report['discontinuity'].values())and report['failure']is None:report['failure']='audio discontinuity reported'
        if report['cleanup_errors']and report['failure']is None:report['failure']='; '.join(report['cleanup_errors'])
        if report['failure']:report['status']='failed'
    if record_pcm:report['recording']=dict(file='capture.stereo.s16le',sha256=digest(output/'capture.stereo.s16le'))
    children_after=resource.getrusage(resource.RUSAGE_CHILDREN)
    report['child_cpu']=dict(user_seconds=children_after.ru_utime-children_before.ru_utime,
        system_seconds=children_after.ru_stime-children_before.ru_stime,
        scope='Waited arecord/aplay child process CPU; separate from parent receiver/ingress CPU, not device hardware processing time.')
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('train',type=Path,nargs='?');p.add_argument('--source-dir',type=Path)
    p.add_argument('--observation-file',type=Path);p.add_argument('--output',required=True,type=Path)
    p.add_argument('--mode',choices=('replay','live'),default='replay')
    p.add_argument('--exclusive',action='store_true');p.add_argument('--device',default='hw:1,0')
    p.add_argument('--replay-pcm',type=Path);p.add_argument('--tx-pcm',type=Path)
    p.add_argument('--fixed-gain',type=float,default=.002);p.add_argument('--analog-gain',type=float,default=1.)
    p.add_argument('--preroll-seconds',type=float,default=1.);p.add_argument('--postroll-seconds',type=float,default=2.)
    p.add_argument('--playback-offset-frames',type=int);p.add_argument('--realtime',action='store_true')
    p.add_argument('--record-pcm',action='store_true');p.add_argument('--chunk-frames',type=int,default=2400)
    p.add_argument('--max-seconds',type=float,default=600.);p.add_argument('--drain-timeout',type=float,default=180.)
    p.add_argument('--threads',type=int,default=2);p.add_argument('--noise-inflation',type=float,default=1.)
    add_background_noise_arguments(p)
    a=p.parse_args()
    if (a.train is None)==(a.source_dir is None):p.error('supply a public train directory, positional or --source-dir')
    a.train=a.train or a.source_dir
    if a.observation_file and (a.replay_pcm or a.tx_pcm):p.error('--observation-file applies only when preparing new PCM')
    if a.mode=='live'and not a.exclusive:p.error('live raw hardware requires --exclusive')
    if a.mode=='live'and a.replay_pcm:p.error('--replay-pcm is for replay mode')
    if a.replay_pcm and a.playback_offset_frames is None:p.error('recorded PCM requires its public --playback-offset-frames')
    try: validate_background_noise(a)
    except ValueError as exc: p.error(str(exc))
    if a.replay_pcm and background_noise_enabled(a):p.error('--replay-pcm is recorded receiver input; background noise applies to transmissions only')
    if a.chunk_frames<1 or a.chunk_frames>65536:p.error('chunk frames must be1..65536')
    for value in (a.fixed_gain,a.analog_gain,a.max_seconds,a.drain_timeout,a.noise_inflation):
        if not math.isfinite(value)or value<=0:p.error('gains, limits and noise inflation must be positive/finite')
    for value in (a.preroll_seconds,a.postroll_seconds):
        if not math.isfinite(value)or not 0<=value<=10:p.error('rolls must be0..10seconds')
    if a.mode=='live'and a.preroll_seconds<=0:p.error('live capture requires a nonzero preroll')
    if a.output.exists()and any(a.output.iterdir()):p.error('output directory must be new or empty')
    a.output.mkdir(parents=True,exist_ok=True)
    import torch
    torch.set_num_threads(a.threads);torch.set_num_interop_threads(1);torch.set_float32_matmul_precision('highest')
    c,w=time.process_time(),time.perf_counter()
    original,slots=load_slots(a.train)
    pre=round(a.preroll_seconds*48000);post=round(a.postroll_seconds*48000)
    offset=a.playback_offset_frames if a.playback_offset_frames is not None else pre
    tx=a.tx_pcm or a.output/'tx.stereo.s16le'
    if a.replay_pcm:bridge=dict(mode='retained-capture-replay',file=str(a.replay_pcm),sha256=digest(a.replay_pcm))
    elif a.tx_pcm:bridge=dict(mode='retained-transmit-PCM',file=str(tx),sha256=digest(tx))
    else:bridge=prepare_pcm(a.train,original,tx,fixed_gain=a.fixed_gain,observation_file=a.observation_file)
    if background_noise_enabled(a) and not a.replay_pcm:
        clean_tx=tx
        tx=a.output/'tx.background-noise.stereo.s16le'
        noise_meta=a.output/'tx.background-noise.json'
        noise_receipt=apply_background_noise(a,input_path=clean_tx,output_path=tx,
            metadata_path=noise_meta,sample_rate=48000,input_format='s16',
            output_format='s16',channels=2,timeout=a.max_seconds)
        bridge=dict(mode='fresh-transmit-background-noise',clean_file=str(clean_tx),
            clean_sha256=digest(clean_tx),file=str(tx),sha256=digest(tx),
            background_noise=noise_receipt,
            stage='digital post-simulation audio baseband before DAC codes; the propagation model is already in the clean fixture')
    preparation=dict(cpu_seconds=time.process_time()-c,wall_seconds=time.perf_counter()-w)
    session=StreamSession(original,slots,a.output,fixed_gain=a.fixed_gain,analog_gain=a.analog_gain,
        playback_offset_frames=offset,noise_inflation=a.noise_inflation)
    failure=None;transport={}
    try:
        if a.mode=='live':transport=live_pcm(session,tx,a.output,device=a.device,preroll_frames=pre,
            postroll_frames=post,chunk_frames=a.chunk_frames,max_seconds=a.max_seconds,record_pcm=a.record_pcm)
        else:transport=replay_pcm(session,a.replay_pcm or tx,
            preroll_frames=0 if a.replay_pcm else pre,postroll_frames=0 if a.replay_pcm else post,
            chunk_frames=a.chunk_frames,realtime=a.realtime)
    except Exception as error:failure=type(error).__name__+': '+str(error)
    finally:stream=session.finish(a.drain_timeout)
    report=dict(schema='triad-continuous-run-v1',mode=a.mode,train_manifest_sha256=digest(a.train/'manifest.json'),
        bridge=bridge,preparation=preparation,transport=transport,stream=stream,
        fixed_gain=a.fixed_gain,analog_gain=a.analog_gain,public_playback_offset_frames=offset,
        source_sha256={name:digest(Path(__file__).parent/name)for name in ('triad_stream_run.py','triad_stream_frontend.py','triad_stream_scheduler.py','triad_stream_slots.py','triad_background_noise.py')},
        total_cpu_seconds=time.process_time()-c,total_wall_seconds=time.perf_counter()-w,
        failure=failure or transport.get('failure'),message_completed=False,physical_null_qualified=False,
        scope='Continuous PCM/DDC/ring/worker test with fixed public source windows; no evaluator truth input; conditional bit candidates only, no source text decode or completed-message claim.')
    write_json(a.output/'report.json',report)
    print(json.dumps(dict(failure=report['failure'],pending_candidate_bits=stream['pending_candidate_bits'],
        drained=stream['schedule']['drained'],cpu_seconds=report['total_cpu_seconds'],wall_seconds=report['total_wall_seconds'])))
    return 1 if report['failure']or not stream['schedule']['drained']else 0


if __name__=='__main__':raise SystemExit(main())
