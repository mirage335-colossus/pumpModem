#!/usr/bin/env python3
"""A,B,A full-receiver validation/timing of a fixed-bank whole-scan workspace."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import time

import numpy as np
import torch

from triad_exhaustive_correlation import ExhaustiveCorrelationBank
from triad_scan_workspace import WholeScanWorkspace


def main():
    benchmark_started=time.perf_counter()
    extra=argparse.ArgumentParser(add_help=False)
    extra.add_argument('--workspace-memory-mib',type=int,default=32768)
    extra.add_argument('--second-capture',type=Path)
    own,rest=extra.parse_known_args()
    path=Path(__file__).with_name('triad-fp4-campaign.py')
    spec=importlib.util.spec_from_file_location('triad_scan_campaign',path)
    runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)
    args=runner.parse_args(rest)
    if not str(args.device).startswith('cuda') or args.correlation_only or args.precision not in ('fp32','fp64','native-fp4'):
        raise ValueError('whole-scan experiment requires CUDA, full inference and fp32/fp64/native-fp4 correlation')
    if args.strict_all:
        raise ValueError('run separate strict reference CLI; this wrapper compares the same declared inference implementation')
    if args.inference_precision not in (None,'fp32') or args.inference_implementation not in (None,'graph-fp64'):
        raise ValueError('workspace CLI requires explicit graph-fp64 inference with FP32 bulk; run strict FP64 inference separately')
    torch.backends.cuda.matmul.allow_tf32=False
    torch.set_float32_matmul_precision('highest')
    torch.set_num_threads(2)
    torch.cuda.set_device(torch.device(args.device))
    args.inference_implementation='graph-fp64'
    args.inference_precision='fp32'
    input_started=time.perf_counter()
    capture=runner.load_capture(args.capture,trim_zero_suffix=args.trim_zero_suffix)
    if args.sample_rate!=capture['manifest']['sample_rate']:
        raise ValueError('sample rate differs from sampled waveform manifest')
    input_hashes={path.name:runner.sha256(path) for path in capture['paths']}
    input_generation=runner.generation_provenance(capture['directory'],input_hashes)
    second_hashes=second_generation=None
    a=np.repeat(capture['observation'],args.repeat_observations,axis=0)
    if own.second_capture:
        other=runner.load_capture(own.second_capture,trim_zero_suffix=args.trim_zero_suffix)
        if (other['manifest']['sample_rate']!=args.sample_rate or
                other['manifest'].get('time_origin_seconds',0.)!=capture['manifest'].get('time_origin_seconds',0.)):
            raise ValueError('second capture changes sample/time coordinates')
        second_hashes={path.name:runner.sha256(path) for path in other['paths']}
        second_generation=runner.generation_provenance(other['directory'],second_hashes)
        if (other['basis'].shape!=capture['basis'].shape or
                not np.array_equal(other['basis'],capture['basis']) or
                other['manifest'].get('noise_variance',other['manifest'].get('noise_variance_pre_frontend')) !=
                capture['manifest'].get('noise_variance',capture['manifest'].get('noise_variance_pre_frontend'))):
            raise ValueError('second capture changes the fixed template bank or variance')
        b=np.repeat(other['observation'],args.repeat_observations,axis=0)
        b_kind='separately supplied sampled capture; independence requires its producer provenance'
    else:
        b=np.zeros_like(a)
        b_kind='zero-input stale-state falsifier, not an independent physical observation'
    input_load_hash_seconds=time.perf_counter()-input_started
    frequencies=args.cfo_hz
    lags=list(range(args.lag_start,args.lag_stop+1,args.lag_step))
    device=torch.device(args.device)
    variance=float(capture['manifest'].get('noise_variance',capture['manifest'].get('noise_variance_pre_frontend')))
    origin=float(capture['manifest'].get('time_origin_seconds',0.))
    hypotheses=len(frequencies)*len(capture['labels'])*len(lags)
    gates={name:runner._threshold(alpha,horizon,args.attempt_spacing,args.duration_alternatives,
                                  args.model_alternatives,hypotheses) for name,alpha,horizon in
           (('default',1e-4,86400.),('relaxed',.01,43200.))}
    def finish(scores):
        started=time.perf_counter()
        bits,_=runner._per_bit_scores(torch.from_numpy(scores),capture['labels'],len(a),len(frequencies),len(lags))
        values=bits.numpy();best=values.argmax(axis=1);maximum=values.max(axis=1)
        decisions={name:dict(best_bit=best.tolist(),accepted=(maximum>=gate['log_likelihood_threshold']).tolist())
                   for name,gate in gates.items()}
        return dict(bit_scores=values.tolist(),decisions=decisions,placement='CPU Torch FP64'),time.perf_counter()-started
    def new_pair():
        backend=None
        if args.precision=='native-fp4':
            from triad_fp4_native import NativeFp4Backend
            backend=NativeFp4Backend(format=args.native_format)
        bank=ExhaustiveCorrelationBank(capture['basis'],args.sample_rate,frequencies,lags,args.block_samples,
            device=device,precision=args.precision,backend=backend,candidate_tile=args.candidate_tile,
            frequency_tile=args.frequency_tile,column_tile=args.column_tile,
            template_cache_bytes=args.template_cache_mib*1024**2,
            gram_cache_bytes=args.gram_cache_mib*1024**2,
            fused_reconstruction=getattr(args,'fused_reconstruction',False))
        receiver=runner.GroupedLikelihood(len(a)*len(frequencies)*len(lags),capture,device,'fp32','graph-fp64',args)
        return bank,receiver
    baseline_bank,baseline_receiver=new_pair()
    def baseline(value):
        started=time.perf_counter()
        observed=torch.from_numpy(value).to(device)
        baseline_receiver.reset()
        for block in baseline_bank.iter_blocks(observed,variance,time_origin_seconds=origin,
                                              max_output_bytes=args.max_block_output_mib*1024**2):
            r=block.r.permute(0,1,3,2,4).reshape(baseline_receiver.batch,baseline_bank.candidates,baseline_bank.taps)
            baseline_receiver.update(r.to(baseline_receiver.complex_dtype),
                                     block.gram.to(baseline_receiver.complex_dtype),block.dt)
        scores=baseline_receiver.scores().cpu().numpy().copy()
        torch.cuda.synchronize(device)
        candidate_elapsed=time.perf_counter()-started
        finishing,finish_elapsed=finish(scores)
        return scores,dict(candidate_replay_seconds=candidate_elapsed,finish_seconds=finish_elapsed,
                           complete_receiver_compute_seconds=time.perf_counter()-started,finish=finishing)
    expected=[]
    baseline_times=[]
    for value in (a,b,a):
        scores,elapsed=baseline(value);expected.append(scores);baseline_times.append(elapsed)
    construction_started=time.perf_counter()
    bank,receiver=new_pair()
    bank_receiver_construction_seconds=time.perf_counter()-construction_started
    workspace=WholeScanWorkspace(bank,receiver,a.shape,noise_variance=variance,
        observation_dtype=torch.complex128,time_origin_seconds=origin,
        memory_limit_bytes=own.workspace_memory_mib*1024**2,
        identity={'manifest_sha256':hashlib.sha256((args.capture/'manifest.json').read_bytes()).hexdigest()})
    results=[]
    for label,value,reference in zip(('A','B','A'),(a,b,a),expected):
        replay_started=time.perf_counter()
        scores,timing=workspace.replay(value)
        finishing,finish_elapsed=finish(scores)
        timing['candidate_replay_seconds']=timing['complete_replay_seconds']
        timing['finish_seconds']=finish_elapsed
        timing['complete_receiver_compute_seconds']=time.perf_counter()-replay_started
        results.append(dict(label=label,timing=timing,maximum_absolute_score_error=float(np.max(np.abs(scores-reference))),
                            bit_identical=bool(np.array_equal(scores,reference)),scores=scores.tolist(),finish=finishing))
    timing=[]
    for _ in range(args.repeats):
        replay_started=time.perf_counter()
        scores,row=workspace.replay(a)
        finishing,finish_elapsed=finish(scores)
        row.update(candidate_replay_seconds=row['complete_replay_seconds'],finish_seconds=finish_elapsed,
                   complete_receiver_compute_seconds=time.perf_counter()-replay_started,finish=finishing)
        timing.append(row)
    sources={name:hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest() for name in
        ('triad-scan-workspace.py','triad_scan_workspace.py','triad_exhaustive_correlation.py',
         'triad_correlation_fusion.py','triad_fp4_native.py','triad_phase_frequency.py',
         'triad_phase_reduction.py','triad_phase_optimized.py','triad-fp4-campaign.py')}
    report=dict(schema='triad-whole-scan-workspace-v1',configuration={k:str(v) if isinstance(v,Path) else v for k,v in vars(args).items()},
        workspace_configuration={k:str(v) if isinstance(v,Path) else v for k,v in vars(own).items()},
        environment=dict(torch_version=torch.__version__,torch_num_threads=torch.get_num_threads(),
                         cuda_device=str(device),gpu_name=torch.cuda.get_device_name(device)),
        input_hashes=input_hashes,input_generation=input_generation,
        second_input_hashes=second_hashes,second_input_generation=second_generation,
        source_hashes=sources,signature=workspace.signature,signature_sha256=workspace.signature_sha256,
        second_observation_kind=b_kind,baseline_aba_timings=baseline_times,aba=results,timing_replays=timing,
        thresholds=gates,admission_status='uncalibrated experimental FP4' if args.precision=='native-fp4' else
            'strict arithmetic comparison; operational qualification remains separate',
        preparation_seconds=workspace.preparation_seconds,memory_estimate=workspace.estimate,
        input_load_hash_seconds=input_load_hash_seconds,
        bank_receiver_construction_seconds=bank_receiver_construction_seconds,
        fixed_workspace_preparation_seconds=bank_receiver_construction_seconds+workspace.preparation_seconds,
        complete_inside_main_benchmark_seconds=time.perf_counter()-benchmark_started,
        preparation_scope='File load/hash, bank/receiver construction and graph preparation are separate. Exporter key/epoch/AES/template generation and process startup are outside this script timing.',
        actual_peak_allocated_bytes=workspace.actual_peak_allocated_bytes,actual_peak_reserved_bytes=workspace.actual_peak_reserved_bytes,
        a_after_b_bit_identical=results[0]['scores']==results[2]['scores'],
        all_baseline_comparisons_within_2e_5=all(x['maximum_absolute_score_error']<=2e-5 for x in results),
        scope='Fixed key/epoch/basis/model/variance workspace. Copies and replays are timing/numerical tests, not independent reception or operational admission validation.')
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('preparation_seconds','a_after_b_bit_identical','all_baseline_comparisons_within_2e_5')}))
    workspace.close()


if __name__=='__main__':
    main()
