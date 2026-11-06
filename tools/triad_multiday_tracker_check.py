#!/usr/bin/env python3
"""Occasional full-span checks of the maintained streaming receiver classes.

The default isolates channel-prior renewal with one public zero-rate clock
state. An explicit coarse timing HMM uses exact public native candidates; it
does not qualify arbitrary timing trajectories or blind acquisition. The native export supplies a moving-clock signal and independently
generated public nominal templates; private clock coordinates are never read.
"""
import argparse
from contextlib import ExitStack
import hashlib
import json
import math
import os
from pathlib import Path
import resource
import select
import signal
import subprocess
import tempfile
import time

import numpy as np
import torch
from scipy.special import logsumexp

from triad_incremental_receiver import IncrementalReceiver
from triad_timing_forward import SparseTimingForward, TimingState
from triad_tracking_receiver import TrackingReceiver


class NativePublicTimingBasis:
    """Exact canonical native source, batched over public timing states.

    Legacy RRC has finite-support discontinuities; interpolation is not an
    accepted substitute. A single persistent process retains the keyed source
    cache. Only public format/key identity and candidate coordinates are sent.
    This diagnostic accepts uniform physical samples, never a private TX map.
    """
    def __init__(self, binary, manifest, *, delay_step=10., rate_step_ppm=100., timeout=30.):
        if manifest.get('source_calendar')!='canonical-8chips-v1' or not manifest.get('test_key_only'):
            raise ValueError('canonical test-key public source calendar required')
        if not hasattr(os,'wait4'):
            raise RuntimeError('native diagnostic CPU accounting requires POSIX wait4')
        self.binary=Path(binary).resolve()
        self.binary_sha256_at_start=sha(self.binary)
        self.delay_step=float(delay_step);self.rate_step_ppm=float(rate_step_ppm)
        self.timeout=float(timeout)
        self.requests=self.rendered_samples=self.bytes_received=0
        self.request_wall_seconds=0.;self.closed=False
        self.log=tempfile.TemporaryFile(mode='w+b')
        command=[str(self.binary),'--basis-server','--clock-profile','off','--spread','none',
            '--rate',str(manifest['inner_rate_hz']),'--added',str(manifest['added_symbols']),
            '--sample-rate',str(manifest['sample_rate_numerator']),
            '--sample-rate-denominator',str(manifest['sample_rate_denominator']),
            '--seconds',str(manifest['requested_bit_seconds']),
            '--data-if',str(manifest['data_if_hz']),'--epoch',str(manifest['public_epoch_seconds']),
            '--key-seed',str(manifest['key_seed'])]
        self.command=command
        try:
            self.process=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                stderr=self.log,bufsize=0)
            self.startup=json.loads(self._line(time.perf_counter()+self.timeout))
            if (self.startup.get('schema')!='triad-tracking-basis-server-v1' or
                self.startup.get('dtype')!='<c8' or self.startup.get('layout')!='bit,state,sample' or
                self.startup.get('bits')!=2):
                raise RuntimeError('unsupported native public basis protocol')
            for name in ('sample_rate_hz','sample_rate_numerator','sample_rate_denominator',
                         'inner_rate_hz','added_symbols','data_if_hz','pattern_revision',
                         'public_epoch_seconds','key_seed','source_calendar','canonical_payload_origin_seconds',
                         'source_samples','bit_seconds','requested_bit_seconds'):
                if self.startup.get(name)!=manifest.get(name):
                    raise RuntimeError('native public basis format differs from capture: '+name)
        except BaseException:
            self.close()
            raise

    def _read(self,count,deadline):
        result=bytearray()
        while len(result)<count:
            remaining=deadline-time.perf_counter()
            if remaining<=0 or not select.select([self.process.stdout],[],[],remaining)[0]:
                raise TimeoutError('native public basis response timed out')
            part=self.process.stdout.read(count-len(result))
            if not part:
                self.log.seek(0)
                raise RuntimeError('native public basis closed: '+self.log.read(4096).decode(errors='replace'))
            result.extend(part)
        return bytes(result)

    def _line(self,deadline):
        result=bytearray()
        while len(result)<16384:
            value=self._read(1,deadline)
            if value==b'\n':return result.decode('utf-8')
            result.extend(value)
        raise RuntimeError('oversized native public basis header')

    def __call__(self,states,anchor,first,stop,origin,offsets):
        if self.closed:raise RuntimeError('native public basis is closed')
        count=stop-first
        offsets=np.asarray(offsets,dtype=np.float64)
        if (not 1<=count<=16384 or not 1<=len(states)<=256 or
            offsets.shape!=(count,) or not np.array_equal(offsets,np.arange(count)+(first-origin))):
            raise ValueError('bounded contiguous uniform sample coordinates required')
        fields=['basis',str(first),str(count),str(len(states)),str(anchor)]
        for state in states:
            if state.group!=0:raise ValueError('this diagnostic has one public epoch group')
            fields.extend((str(state.delay_index*self.delay_step),str(state.rate_index*self.rate_step_ppm)))
        started=time.perf_counter();deadline=started+self.timeout
        payload=memoryview((' '.join(fields)+'\n').encode())
        while payload:
            written=self.process.stdin.write(payload)
            if not written:raise RuntimeError('native public basis input closed')
            payload=payload[written:]
        self.process.stdin.flush()
        size=2*len(states)*count*8
        header=self._line(deadline)
        if header!=f'ok {size}':raise RuntimeError('invalid native public basis response: '+header)
        values=np.frombuffer(self._read(size,deadline),dtype='<c8').reshape(2,len(states),count)
        if not np.isfinite(values).all():raise RuntimeError('nonfinite native public basis response')
        result=np.zeros((3,len(states),1,count),np.complex128)
        result[1:,:,0,:]=values
        self.requests+=1;self.rendered_samples+=values.size;self.bytes_received+=size
        self.request_wall_seconds+=time.perf_counter()-started
        return result

    def close(self):
        if self.closed:return
        self.closed=True
        process=getattr(self,'process',None)
        self.exit_code=None
        self.child_cpu_seconds=0.
        if process is not None:
            if process.stdin:process.stdin.close()
            deadline=time.perf_counter()+5.
            while True:
                pid,status,usage=os.wait4(process.pid,os.WNOHANG)
                if pid:break
                if time.perf_counter()>=deadline:
                    os.kill(process.pid,signal.SIGKILL)
                    _,status,usage=os.wait4(process.pid,0)
                    break
                time.sleep(.001)
            self.exit_code=process.returncode=os.waitstatus_to_exitcode(status)
            self.child_cpu_seconds=usage.ru_utime+usage.ru_stime
            if process.stdout:process.stdout.close()
        self.log.close()

    def report(self):
        if not self.closed:raise RuntimeError('close native public basis before reporting final CPU cost')
        final_hash=sha(self.binary)
        return dict(command=self.command,binary_sha256=final_hash,
            binary_sha256_at_start=self.binary_sha256_at_start,
            binary_unchanged_during_run=(final_hash==self.binary_sha256_at_start),startup=self.startup,
            requests=self.requests,rendered_complex_samples=self.rendered_samples,
            bytes_received=self.bytes_received,request_wall_seconds=self.request_wall_seconds,
            child_cpu_seconds=self.child_cpu_seconds,exit_code=self.exit_code,
            scope='Exact public source synthesis; no private clock/channel input. CPU child includes startup and finalization.')

    def __enter__(self):return self
    def __exit__(self,*unused):self.close()


def sha(path):
    h=hashlib.sha256()
    with open(path,'rb') as f:
        for data in iter(lambda:f.read(1048576),b''):
            h.update(data)
    return h.hexdigest()


def run(a,resources):
    began,cpu=time.perf_counter(),time.process_time()
    dependencies=('triad_multiday_tracker_check.py','triad_tracking_receiver.py','triad_incremental_receiver.py',
        'triad_gaussian_state.py','triad_timing_forward.py','triad_dynamic_joint.py')
    source_begin={name:sha(Path(__file__).with_name(name)) for name in dependencies}
    torch.set_num_threads(1)
    torch.backends.cuda.matmul.allow_tf32=False
    torch.set_float32_matmul_precision('highest')
    root=Path(a.input)
    manifest_start=sha(root/'manifest.json')
    manifest=json.loads((root/'manifest.json').read_text())
    if manifest['mode']!='continuous-shared-channel':
        raise ValueError('complete native shared-channel export required')
    if 0 not in manifest['template_ppm']:
        raise ValueError('public zero-ppm template required')
    index=manifest['template_ppm'].index(0)
    original_samples=int(manifest['samples'])
    samples=original_samples if a.limit_samples is None else min(original_samples,a.limit_samples)
    fs=float(manifest['sample_rate_hz'])
    variance=fs/10**(a.cn0/10)
    filenames=[f'basis_bit{bit}_ppm{index}.c64' for bit in (0,1)]
    if a.true_bit!='null':
        filenames.append(f'signal_bit{a.true_bit}.c64')
    data={name:np.memmap(root/name,dtype='<c8',mode='r',shape=(original_samples,)) for name in filenames}
    hashes={name:sha(root/name) for name in filenames}
    templates=[data[name] for name in filenames[:2]]
    def basis(first,stop):
        output=np.zeros((3,1,1,stop-first),np.complex128)
        for bit in (0,1):
            output[bit+1,0,0]=templates[bit][first:stop]
        return output
    def tracking_basis(states,anchor,first,stop,origin,offsets):
        if tuple(states)!=(TimingState(0,0,0),):
            raise AssertionError('diagnostic public clock state unexpectedly changed')
        return basis(first,stop)
    native_provider=None
    if a.dynamic_timing:
        if not a.basis_exporter:
            raise ValueError('dynamic diagnostic requires --basis-exporter for exact native candidates')
        if (manifest['inner_rate_hz']>.001 or manifest['added_symbols']!=1 or
            manifest['data_if_hz']>manifest['inner_rate_hz']/2):
            raise ValueError('dynamic diagnostic scope is rate<=.001, added1, IF<=rate/2')
        native_provider=resources.enter_context(NativePublicTimingBasis(a.basis_exporter,manifest))
        tracking_basis=native_provider
    timing=SparseTimingForward([TimingState(0,0,0)],model_count=1,
        delay_step_seconds=10.,rate_step_ppm=100.,
        maximum_rate_ppm=200 if a.dynamic_timing else 0,
        maximum_acceleration_ppm_per_second=10 if a.dynamic_timing else 0,
        maximum_states=a.timing_states if a.dynamic_timing else 1,
        maximum_proposals=a.timing_proposals if a.dynamic_timing else 1)
    segment=max(1,round(a.segment_seconds*fs))
    common=dict(sample_rate=fs,samples=samples,required_bands=[[-.49*fs,.49*fs]],
        noise_variance=variance,stationary_power=(1.,),
        precision='fp64',projection_mode='valid-rows',maximum_input_chunk_samples=a.chunk,
        memory_limit_bytes=32*1024**2)
    tracker=TrackingReceiver(**common,block_samples=segment,segment_samples=segment,
        basis_provider=tracking_basis,timing_forward=timing,coherence_seconds=(math.inf,))
    persistent=IncrementalReceiver(**common,block_samples=min(a.chunk,samples),
        basis=basis,candidate_count=1,maximum_candidates=1,coherence_seconds=(a.coherence_seconds,))
    preparation=time.perf_counter()-began
    rng=np.random.default_rng(a.seed)
    r=np.zeros(2,np.complex128);g=np.zeros(2,np.float64)
    noise_cpu=tracker_cpu=persistent_cpu=0.
    for first in range(0,samples,a.chunk):
        stop=min(samples,first+a.chunk)
        t=time.process_time()
        y=(rng.standard_normal(stop-first)+1j*rng.standard_normal(stop-first))*math.sqrt(variance/2)
        if a.true_bit!='null':
            y+=data[f'signal_bit{a.true_bit}.c64'][first:stop]
        b=np.stack([x[first:stop] for x in templates]).astype(np.complex128)
        r+=b.conj()@y/variance
        g+=np.sum(abs(b)**2,axis=1)/variance
        noise_cpu+=time.process_time()-t
        t=time.process_time();persistent.push(first,y);persistent_cpu+=time.process_time()-t
        t=time.process_time();tracker.push(first,y);tracker_cpu+=time.process_time()-t
    tracked=tracker.finish();coherent=persistent.finish()
    native_report=None
    if native_provider is not None:
        native_provider.close();native_report=native_provider.report()
        if native_report['exit_code']!=0:raise RuntimeError('native public basis did not close successfully')
    reference=abs(r)**2/(1+g)-np.log1p(g)
    mismatch=float(np.max(abs(reference-np.asarray(coherent['bit_scores'])))) if math.isinf(a.coherence_seconds) else None
    if mismatch is not None and mismatch>1e-8*max(1.,float(np.max(abs(reference)))):
        raise AssertionError(f'persistent maintained receiver disagrees with full-window scalar Gaussian reference: {mismatch}')
    for result in (tracked,coherent):
        if not result['complete_observation'] or result['processed_rows']!=samples:
            raise AssertionError('incomplete maintained receiver observation')
        score=float(logsumexp(result['bit_scores'])-math.log(2))
        selected=int(np.argmax(result['bit_scores']))
        passed=score>=math.log(1/a.alpha)
        result['endpoint_diagnostic']=dict(log_e=score,selected_bit=selected,
            outcome=('false_admission' if passed else 'null_rejection') if a.true_bit=='null' else
                ('miss' if not passed else 'correct_admission' if selected==int(a.true_bit) else 'wrong_admission'))
    source_end={name:sha(Path(__file__).with_name(name)) for name in dependencies}
    input_end={name:sha(root/name) for name in filenames}
    samples_unchanged=(input_end==hashes)
    manifest_end=sha(root/'manifest.json')
    dependencies_unchanged=(source_begin==source_end and samples_unchanged and manifest_start==manifest_end and
        (native_report is None or native_report['binary_unchanged_during_run']))
    report=dict(schema='triad-maintained-multiday-tracker-v1',configuration=vars(a),manifest=manifest,
        input_sha256=hashes,input_sha256_at_end=input_end,input_samples_unchanged=samples_unchanged,
        manifest_sha256=manifest_end,manifest_sha256_at_start=manifest_start,
        source_sha256=source_end,source_sha256_at_start=source_begin,
        runtime_dependencies_unchanged=dependencies_unchanged,
        complete_export_observation=(samples==original_samples),samples=samples,
        simulated_seconds=samples/fs,reset_segment_seconds=segment/fs,
        persistent=coherent,reset_tracker=tracked,full_window_reference_scores=reference.tolist(),
        maximum_persistent_reference_error=mismatch,noise_variance=variance,
        native_public_basis=native_report,
        public_timing_model=dict(dynamic=a.dynamic_timing,delay_step_seconds=10.,
            rate_step_ppm=100.,maximum_rate_ppm=200 if a.dynamic_timing else 0,
            maximum_acceleration_ppm_per_second=10 if a.dynamic_timing else 0,
            interpolation='none-exact-native-canonical-source-plus-full-IF-phase' if a.dynamic_timing else 'exact-public-nominal-template'),
        timing=dict(preparation_wall_seconds=preparation,noise_and_reference_cpu_seconds=noise_cpu,
            persistent_cpu_seconds=persistent_cpu,reset_tracker_cpu_seconds=tracker_cpu,
            native_basis_child_cpu_seconds=0. if native_report is None else native_report['child_cpu_seconds'],
            total_cpu_seconds=time.process_time()-cpu,
            total_cpu_seconds_including_native=time.process_time()-cpu+(0. if native_report is None else native_report['child_cpu_seconds']),
            total_wall_seconds=time.perf_counter()-began),
        peak_rss_kib=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
        admission_scope='Per-branch endpoint diagnostics at alpha; selecting either branch requires a shared budget or fixed normalized model mixture. No combined operational admission rule is implemented here.',
        scope='One scheduled endpoint; public nominal persistent branch and optional coarse public timing HMM, known white Gaussian noise, None/Off or explicitly declared native channel. Finite coherence uses the maintained Gaussian OU prior, not an exact Brownian phase likelihood. The whole-window formula is an equality reference only for infinite coherence. Dynamic HMM mode is a nondefault coarse diagnostic; it does not qualify arbitrary/default trajectories or high-IF tracking. Prefix limits are diagnostics only. No physical admission qualification.')
    output=Path(a.output);output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(report,indent=2)+'\n')
    if not dependencies_unchanged:
        raise RuntimeError('runtime dependency, sampled input or manifest changed; retained result is not source-qualified')
    print(json.dumps(dict(samples=samples,simulated_seconds=samples/fs,complete=report['complete_export_observation'],
        persistent=coherent['endpoint_diagnostic'],reset_tracker=tracked['endpoint_diagnostic'],timing=report['timing'])))


def main(a):
    with ExitStack() as resources:
        return run(a,resources)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input',required=True);p.add_argument('--output',required=True)
    p.add_argument('--cn0',type=float,required=True);p.add_argument('--true-bit',choices=['0','1','null'],default='0')
    p.add_argument('--seed',type=int,default=527031);p.add_argument('--alpha',type=float,default=1e-4)
    p.add_argument('--chunk',type=int,default=4096);p.add_argument('--limit-samples',type=int)
    p.add_argument('--segment-seconds',type=float,default=4.)
    p.add_argument('--coherence-seconds',type=float,default=math.inf)
    p.add_argument('--dynamic-timing',action='store_true')
    p.add_argument('--basis-exporter',help='native triad_tracking_export for exact public dynamic templates')
    p.add_argument('--timing-states',type=int,default=8)
    p.add_argument('--timing-proposals',type=int,default=32)
    a=p.parse_args()
    if not math.isfinite(a.cn0) or not 0<a.alpha<1 or not 0<a.chunk<=1048576 or not math.isfinite(a.segment_seconds) or a.segment_seconds<=0 or not a.coherence_seconds>0 or math.isnan(a.coherence_seconds) or (a.limit_samples is not None and a.limit_samples<1) or not 1<=a.timing_states<=64 or not a.timing_states<=a.timing_proposals<=256:
        p.error('invalid finite C/N0, alpha, or sample limit')
    main(a)
