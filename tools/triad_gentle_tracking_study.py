#!/usr/bin/env python3
"""Focused complete-native-capture qualification of the default timing receiver.

The default receiver, reset reference and nominal persistent reference consume
identical actual sampled Gaussian noise. Private TX timing/phase is not read.
This is scheduled conditional simulation, not blind or physical ADC admission.
"""
from __future__ import annotations
import argparse
from contextlib import ExitStack
import hashlib
import json
import math
from pathlib import Path
import time
import numpy as np
import torch
from scipy.special import logsumexp
from triad_continuous import make_timing_forward, tracking_receiver_class, tracking_resource_report
from triad_incremental_receiver import IncrementalReceiver
from triad_multiday_tracker_check import NativePublicTimingBasis
from triad_operational_admission import operational_threshold


def digest(path):
    h=hashlib.sha256()
    with open(path,'rb') as f:
        for part in iter(lambda:f.read(1<<20),b''): h.update(part)
    return h.hexdigest()


def run(a):
    torch.set_num_threads(1)
    torch.backends.cuda.matmul.allow_tf32=False
    torch.set_float32_matmul_precision('highest')
    root=Path(__file__).resolve().parents[1]
    dependencies=[root/'tools'/name for name in ('triad_gentle_tracking_study.py','triad_continuous.py',
        'triad_tracking_receiver.py','triad_incremental_receiver.py','triad_gaussian_state.py',
        'triad_timing_forward.py','triad_dynamic_joint.py','triad_multiday_tracker_check.py')]
    sources={str(p.relative_to(root)):digest(p) for p in dependencies}
    m=json.loads((a.input/'manifest.json').read_text());fs=float(m['sample_rate_hz']);n=int(m['samples'])
    if m['mode']!='continuous-shared-channel' or 0 not in m['template_ppm']:
        raise ValueError('complete native waveform and public zero-rate basis required')
    idx=m['template_ppm'].index(0)
    paths=[a.input/f'basis_bit{b}_ppm{idx}.c64' for b in (0,1)]
    if a.bit!='null':paths.append(a.input/f'signal_bit{a.bit}.c64')
    identities={str(p):digest(p) for p in [a.input/'manifest.json',*paths]}
    data=[np.memmap(p,mode='r',dtype='<c8',shape=(n,)) for p in paths]
    descriptor=dict(nominal_triplet_bandwidth_hz=m['occupied_bandwidth_hz'],data_if_hz=m['data_if_hz'],
        sample_rate_hz=fs, fine_chip_rate_hz=m['inner_rate_hz']*m['added_symbols']/6)
    bank=dict(coordinates=[[0,0,0,0]],tracking={})
    if a.segment_seconds is not None:bank['tracking']['segment_seconds']=a.segment_seconds
    if a.state_cap is not None:
        bank['tracking'].update(maximum_states=a.state_cap,maximum_proposals=4*a.state_cap)
    coherences=tuple(float(x) for x in a.coherences.split(','))
    forward,seconds=make_timing_forward(bank,descriptor,len(coherences))
    segment=max(1,round(seconds*fs));block=min(segment,max(1,round(a.block_seconds*fs)))
    variance=fs/10**(a.cn0/10)
    def nominal(lo,hi):
        value=np.zeros((3,1,1,hi-lo),np.complex128)
        for b in (0,1):value[b+1,0,0]=data[b][lo:hi]
        return value
    common=dict(sample_rate=fs,samples=n,block_samples=block,
        required_bands=[[-.49*fs,.49*fs]],noise_variance=variance,stationary_power=(1.,),
        precision=a.precision,projection_mode='valid-rows',coherence_seconds=coherences,
        maximum_input_chunk_samples=a.chunk,memory_limit_bytes=256*1024**2)
    records=[];began=time.perf_counter();cpu=time.process_time()
    with ExitStack() as stack:
        provider=stack.enter_context(NativePublicTimingBasis(a.exporter,m,
            delay_step=forward.delay_step_seconds,rate_step_ppm=forward.rate_step_ppm))
        for trial in range(a.trials):
            prepared=time.perf_counter();models={};work_plans={}
            for name in a.receivers.split(','):
                if name == 'nominal': continue
                f,_=make_timing_forward(bank,descriptor,len(coherences))
                extra=dict(audio_center_hz=m['data_if_hz']) if name!='tracking-reset' else {}
                models[name]=tracking_receiver_class(name)(**common,segment_samples=segment,
                    basis_provider=provider,timing_forward=f,**extra)
                work_plans[name]=tracking_resource_report(models[name],descriptor,
                    template_backend='direct',memory_limit_bytes=256*1024**2)
            if 'nominal' in a.receivers.split(','):
                models['nominal']=IncrementalReceiver(**common,basis=nominal,candidate_count=1,maximum_candidates=1)
            preparation=time.perf_counter()-prepared
            rng=np.random.default_rng(a.seed+trial);timers={name:0. for name in models}
            for first in range(0,n,a.chunk):
                stop=min(n,first+a.chunk)
                values=(rng.standard_normal(stop-first)+1j*rng.standard_normal(stop-first))*math.sqrt(variance/2)
                if a.bit!='null':values+=data[2][first:stop]
                for name,rx in models.items():
                    before=time.process_time();rx.push(first,values);timers[name]+=time.process_time()-before
            outcomes={}
            for name,rx in models.items():
                report=rx.finish()
                if not report['complete_observation'] or report['processed_rows']!=n:raise RuntimeError('incomplete observation')
                scores=np.asarray(report['bit_scores']);score=float(logsumexp(scores)-math.log(2));bit=int(scores.argmax())
                outcome=('false_admission' if a.bit=='null' else 'correct_admission' if bit==int(a.bit) else 'wrong_admission') if score>=math.log(1/a.alpha) else ('null_rejection' if a.bit=='null' else 'miss')
                outcomes[name]=dict(log_e=score,selected_bit=bit,outcome=outcome,receiver_cpu_seconds=timers[name],
                    default_policy_outcome=(('false_admission' if a.bit=='null' else
                        'correct_admission' if bit==int(a.bit) else 'wrong_admission')
                        if score>=operational_threshold()['log_e_threshold'] else
                        ('null_rejection' if a.bit=='null' else 'miss')),
                    cpu_seconds_per_received_second=timers[name]/(n/fs),receiver=report,
                    resource_plan=work_plans.get(name))
                if name in work_plans and 'work_counts' in report:
                    bound=work_plans[name]['work'];actual=report['work_counts']
                    for observed,planned in (('basis_rows','basis_row_samples'),
                            ('candidate_block_updates','gaussian_updates'),('transition_edges','transition_edges')):
                        if actual[observed]>bound[planned]:raise RuntimeError('work forecast undercounts '+observed)
            records.append(dict(trial=trial,seed=a.seed+trial,preparation_wall_seconds=preparation,outcomes=outcomes))
        provider.close();native=provider.report()
    source_after={str(p.relative_to(root)):digest(p) for p in dependencies}
    input_after={str(p):digest(p) for p in [a.input/'manifest.json',*paths]}
    return dict(schema='triad-gentle-tracking-study-v1',manifest=m,bank=bank,
        requested_segment_seconds=seconds,actual_segment_seconds=segment/fs,block_samples=block,
        cn0_db_hz=a.cn0,alpha_fixed_endpoint=a.alpha,true_bit=a.bit,trials=a.trials,
        default_admission_policy=operational_threshold(),
        precision=a.precision,records=records,native_provider=native,
        wall_seconds=time.perf_counter()-began,python_cpu_seconds=time.process_time()-cpu,
        source_hashes=sources,input_hashes=identities,
        qualification_valid=sources==source_after and identities==input_after,
        scope='Complete sampled scheduled capture; each branch is a separate diagnostic decision, not an unbudgeted choose-any rule; no private TX trace, no physical/ASRC null qualification')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--exporter',type=Path,default=Path('build/dev-cli/triad_tracking_export'))
    p.add_argument('--cn0',type=float,default=-45.);p.add_argument('--bit',choices=('0','1','null'),default='0')
    p.add_argument('--trials',type=int,default=1);p.add_argument('--seed',type=int,default=614051)
    p.add_argument('--alpha',type=float,default=1e-4);p.add_argument('--segment-seconds',type=float)
    p.add_argument('--block-seconds',type=float,default=.25)
    p.add_argument('--receivers',default='tracking,tracking-reset,nominal',
        help='Separate diagnostic alternatives; tracking is the practical default')
    p.add_argument('--coherences',default='0.5,2,inf');p.add_argument('--state-cap',type=int)
    p.add_argument('--precision',choices=('fp32','fp64'),default='fp32');p.add_argument('--chunk',type=int,default=1024)
    a=p.parse_args()
    if a.output.exists():raise FileExistsError(a.output)
    if (not 1<=a.trials<=256 or not 1<=a.chunk<=8192 or not 0<a.alpha<1 or not math.isfinite(a.cn0) or
        not set(a.receivers.split(',')) <= {'tracking','tracking-persistent','tracking-reset','nominal'} or
        len(set(a.receivers.split(','))) != len(a.receivers.split(','))):
        raise ValueError('invalid bounded study settings')
    a.output.parent.mkdir(parents=True,exist_ok=True)
    result=run(a);a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    if not result['qualification_valid']:raise RuntimeError('sources or inputs changed during study')
    print(json.dumps(dict(wall_seconds=result['wall_seconds'],results=[{name:(x['outcome'],x['log_e']) for name,x in row['outcomes'].items()} for row in result['records']])))

if __name__=='__main__':main()
