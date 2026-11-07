#!/usr/bin/env python3
"""Fresh-key full-receiver jobs offered at a declared bit cadence.

This is a computational workload qualification. Independent saved windows are
not a continuous transmitted waveform, live ADC stream, or complete message.
Native template generation and per-key preparation are included in each job.
Run under an external process-group deadline (for example GNU timeout with a
kill-after grace); thread cancellation cannot interrupt blocked native reads.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import math
import os
from pathlib import Path
import resource
import threading
import time


ROOT=Path(__file__).resolve().parents[1]


def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb')as stream:
        while block:=stream.read(1<<20):h.update(block)
    return h.hexdigest()


def write_json(path,value):
    with Path(path).open('x')as stream:
        json.dump(value,stream,indent=2,allow_nan=False);stream.write('\n')


def child_cpu():
    r=resource.getrusage(resource.RUSAGE_CHILDREN)
    return r.ru_utime+r.ru_stime


def run_cadence(cases, callback, *, cadence_seconds, workers=1, queue_capacity=2):
    """Bound queued/running jobs, never delay the producer to hide overload.

    Callbacks own all mutable state. Records retain public input order, not
    completion order. Per-job process CPU would overlap and is not summed.
    """
    if not math.isfinite(cadence_seconds)or cadence_seconds<=0:
        raise ValueError('positive finite cadence required')
    for value in (workers,queue_capacity):
        if isinstance(value,bool)or not isinstance(value,int)or value<1:
            raise ValueError('positive integer workers and queue capacity required')
    cases=tuple(cases)
    if not cases:raise ValueError('at least one complete workload case required')
    lock=threading.Lock();slots=threading.BoundedSemaphore(workers+queue_capacity)
    records=[dict(index=i,status='pending',ready_seconds=i*cadence_seconds)for i in range(len(cases))]
    started=time.perf_counter();cpu=time.process_time();children=child_cpu()
    queued=running=highwater=0
    def execute(index,case):
        nonlocal queued,running
        with lock:
            queued-=1;running+=1
            records[index].update(status='running',started_seconds=time.perf_counter()-started)
        outcome=dict(status='error',error_type='BaseException',error='worker did not return normally')
        try:
            result=callback(index,case)
            outcome=dict(status='completed',result=result)
        except Exception as error:
            outcome=dict(status='error',error_type=type(error).__name__,error=str(error))
        finally:
            with lock:
                running-=1;record=records[index];record.update(outcome,finished_seconds=time.perf_counter()-started)
                record['queue_wait_seconds']=record['started_seconds']-record['submitted_seconds']
                record['completion_delay_seconds']=record['finished_seconds']-record['ready_seconds']
                record['over_one_bit_deadline_seconds']=max(0.,record['completion_delay_seconds']-cadence_seconds)
            slots.release()
    with ThreadPoolExecutor(max_workers=workers,thread_name_prefix='triad-cadence')as pool:
        futures=[]
        for index,case in enumerate(cases):
            delay=started+index*cadence_seconds-time.perf_counter()
            if delay>0:time.sleep(delay)
            if not slots.acquire(blocking=False):
                records[index].update(status='overload',finished_seconds=time.perf_counter()-started)
                continue
            with lock:
                queued+=1;highwater=max(highwater,queued)
                records[index].update(status='queued',submitted_seconds=time.perf_counter()-started)
            futures.append(pool.submit(execute,index,case))
        for future in futures:future.result()
    wall=time.perf_counter()-started;parent_cpu=time.process_time()-cpu;native_cpu=child_cpu()-children
    offered=len(cases)*cadence_seconds
    return dict(schema='triad-computational-cadence-v1',workers=workers,queue_capacity=queue_capacity,
        cadence_seconds=cadence_seconds,jobs=len(cases),offered_data_seconds=offered,
        parent_cpu_seconds=parent_cpu,waited_child_cpu_seconds=native_cpu,
        total_cpu_seconds=parent_cpu+native_cpu,wall_seconds=wall,
        cpu_seconds_per_offered_data_second=(parent_cpu+native_cpu)/offered,
        inflight_capacity=workers+queue_capacity,queue_highwater=highwater,
        queue_highwater_scope='Submitted not-yet-started jobs, including reserved worker slots during thread dispatch.',
        maximum_queue_wait_seconds=max((r.get('queue_wait_seconds',0.)for r in records),default=0.),
        maximum_completion_delay_seconds=max((r.get('completion_delay_seconds',0.)for r in records),default=0.),
        completed=sum(r['status']=='completed'for r in records),overloads=sum(r['status']=='overload'for r in records),
        errors=sum(r['status']=='error'for r in records),records=records,
        timing_scope='Aggregate process CPU plus one campaign-wide waited-child delta; includes fresh native bank and per-key preparation. Per-job child deltas can overlap and are not summed.',
        scope='Independent complete receiver windows offered at a bit cadence; no claim of continuous waveform, live audio, full-message completion or physical-null qualification.')


def _public_case(case, protocol, output, index):
    import numpy as np
    from triad_template_bank import generate_template_bank
    from triad_stream_slots import load_slots
    from triad_stream_frontend import StreamingAudioDDC
    from triad_stream_run import public_audio_windows
    from triad_weak_receiver import WeakReceiver
    name=case['name'];public_dir=Path(case['public_dir'])
    original=json.loads((public_dir/'manifest.json').read_text())
    bank,receipt=generate_template_bank(original,case['template_argv'],
        memory_limit_bytes=int(protocol.get('template_memory_limit_bytes',512*1024**2)))
    _,slots=load_slots(public_dir,templates=bank)
    frontend=StreamingAudioDDC(48000,int(original['sample_rate_hz']),
        original['channel']['nominal_conversion_if_hz'],
        undo_fixed_gain=float(protocol.get('fixed_gain',.002)))
    slots,_=public_audio_windows(slots,frontend,int(protocol.get('playback_offset_frames',48000)))
    if len(slots)!=1:raise ValueError('one complete public slot per workload case required')
    manifest,templates,epochs=slots[0]
    with np.load(case['scoring_input'],allow_pickle=False)as captured:
        raw=np.array(captured['raw'],copy=True);valid=np.array(captured['valid'],copy=True)
    if raw.shape!=(manifest['calendar']['capture_samples'],)or valid.shape!=raw.shape:
        raise ValueError('saved scoring window differs from public audio-adapted geometry')
    c,w=time.process_time(),time.perf_counter()
    receiver=WeakReceiver(manifest,templates,epochs,
        noise_inflation=float(case.get('noise_inflation',1.)),
        fft_workers=int(protocol.get('numerical_threads',2)),
        admission_policy=manifest.get('weak_admission_policy'))
    preparation=dict(process_cpu_observed_delta=time.process_time()-c,wall_seconds=time.perf_counter()-w,
        cpu_scope='Concurrent process delta; do not sum across jobs.')
    result=receiver.score(raw,valid)
    # Save the decision before opening the separate evaluation file.
    destination=output/(name+'-receiver.json')
    write_json(destination,result)
    return dict(name=name,file=destination.name,sha256=digest(destination),
        status=result.get('status'),bit=result.get('bit'),
        conditional_model_pass=bool(result.get('operational',{}).get('passes_conditional_bound',False)),
        native_bank=receipt,receiver_preparation=preparation,receiver_timing=result.get('timing'),
        physical_null_qualified=False,message_completed=False)


def main():
    startup_cpu,startup_wall=time.process_time(),time.perf_counter()
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('protocol',type=Path);parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args();protocol=json.loads(args.protocol.read_text())
    if protocol.get('schema')!='triad-weak-throughput-protocol-v1':raise ValueError('frozen throughput protocol required')
    threads=protocol.get('numerical_threads',2)
    if isinstance(threads,bool)or not isinstance(threads,int)or threads<1:raise ValueError('positive numerical threads required')
    for name in ('OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS'):os.environ[name]=str(threads)
    # Heavy numerical libraries are imported only after thread policy is set.
    import torch
    torch.set_num_threads(threads);torch.set_num_interop_threads(1);torch.set_float32_matmul_precision('highest')
    from triad_template_bank import native_identity
    cases=protocol.get('cases',[]);names=set();identities=[]
    for case in cases:
        name=case.get('name','')
        if not name or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_'for c in name)or name in names:
            raise ValueError('case names must be unique safe basenames')
        if 'expected_bit'in case:raise ValueError('expected bits belong in a separate evaluation_file')
        names.add(name)
        identities.append(dict(name=name,public_manifest_sha256=digest(Path(case['public_dir'])/'manifest.json'),
            scoring_input_sha256=digest(case['scoring_input']),native=native_identity(case['template_argv'])))
    source_names=('triad_weak_throughput.py','triad_template_bank.py','triad_stream_slots.py',
        'triad_weak_receiver.py','triad_weak_helper.py','triad_segmented_fft.py','triad_dynamic_joint.py',
        'triad_dynamic_variants.py','triad_dynamic_scan.py','triad_gaussian_prepared.py','triad_gaussian_state.py',
        'triad_operational_admission.py','triad_realtime_fft.py','triad_realtime_receiver.py',
        'triad_stream_frontend.py','triad_stream_run.py','triad_continuous.py')
    sources={name:digest(ROOT/'tools'/name)for name in source_names}
    args.output.mkdir(parents=True,exist_ok=False)
    write_json(args.output/'invocation.json',dict(protocol=protocol,protocol_sha256=digest(args.protocol),
        inputs=identities,sources=sources,evaluation_read_before_scoring=False))
    startup=dict(cpu_seconds=time.process_time()-startup_cpu,wall_seconds=time.perf_counter()-startup_wall,
        scope='CLI imports/thread configuration and frozen input/source identity checks; no keyed bank reused from startup.')
    report=run_cadence(cases,lambda i,case:_public_case(case,protocol,args.output,i),
        cadence_seconds=float(protocol['cadence_seconds']),workers=protocol.get('workers',1),
        queue_capacity=protocol.get('queue_capacity',2))
    report.update(numerical_threads=threads,source_hashes=sources,startup=startup,
        source_identity_unchanged=all(digest(ROOT/'tools'/name)==value for name,value in sources.items()),
        input_identity_unchanged=all(
            digest(Path(case['public_dir'])/'manifest.json')==identity['public_manifest_sha256']and
            digest(case['scoring_input'])==identity['scoring_input_sha256']
            for case,identity in zip(cases,identities)),
        native_identity_unchanged=all(native_identity(case['template_argv'])==identity['native']
            for case,identity in zip(cases,identities)),
        peak_process_rss_kib=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
        peak_rss_scope='Parent process high-water RSS, excluding native subprocess peaks.',
        physical_null_qualified=False,message_completed=False)
    write_json(args.output/'receiver-campaign.json',report)
    # The entire immutable receiver campaign now exists. Evaluation cannot
    # change candidate selection, likelihoods or which jobs were attempted.
    evaluation=None
    if protocol.get('evaluation_file'):
        truth=json.loads(Path(protocol['evaluation_file']).read_text())
        evaluation=dict(correct_admissions=0,wrong_admissions=0,misses=0,false_admissions=0,unscored=0)
        for record,case in zip(report['records'],cases):
            result=record.get('result',{});expected=truth[case['name']]
            if record['status']!='completed'or result.get('status')!='scored':evaluation['unscored']+=1;continue
            accepted=result['conditional_model_pass']
            if expected is None:
                evaluation['false_admissions']+=int(accepted)
            elif not accepted:evaluation['misses']+=1
            elif result['bit']==expected:evaluation['correct_admissions']+=1
            else:evaluation['wrong_admissions']+=1
        evaluation['scope']='Conditional model decisions on the frozen finite cases; not a physical false-admission or reliability qualification.'
        write_json(args.output/'evaluation.json',evaluation)
    print(json.dumps(dict(completed=report['completed'],overloads=report['overloads'],errors=report['errors'],
        cpu_seconds_per_data_second=report['cpu_seconds_per_offered_data_second'],wall_seconds=report['wall_seconds'],evaluation=evaluation)))
    return int(report['overloads']or report['errors']or not report['source_identity_unchanged']or
        not report['input_identity_unchanged']or not report['native_identity_unchanged']or
        any(r.get('result',{}).get('status')!='scored'for r in report['records']))


if __name__=='__main__':raise SystemExit(main())
