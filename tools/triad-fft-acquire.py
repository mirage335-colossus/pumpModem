#!/usr/bin/env python3
"""Exhaustive fixed-grid FFT acquisition of sampled triad epoch/bit templates.

This diagnostic uses one unknown constant shared complex gain per complete
capture. It is not a phase-tracking, streaming or arbitrary-channel receiver.
All nonnegative integer timing lags and CFO bins in the declared bank are scored.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import random
import sys
import time
from typing import Any

SCRIPT_SHA256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
HELPER_PATH=Path(__file__).with_name("triad-gpu-study.py")
HELPER_SHA256=hashlib.sha256(HELPER_PATH.read_bytes()).hexdigest()

try:
    import torch
except ImportError:
    raise SystemExit("triad-fft-acquire: an existing Torch installation is required")

_spec = importlib.util.spec_from_file_location("triad_fft_shared", HELPER_PATH)
assert _spec and _spec.loader
shared = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(shared)


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("manifest", type=Path)
    p.add_argument("--device", default="cpu")
    p.add_argument("--trials", type=shared._positive_int, default=4)
    p.add_argument("--batch", type=shared._positive_int, default=4)
    p.add_argument("--frequency-tile", type=shared._positive_int, default=4)
    p.add_argument("--cn0-grid", type=shared._parse_cn0_grid)
    p.add_argument("--lag-max-seconds", type=shared._finite_float, default=14.07)
    p.add_argument("--lag-step-samples", type=shared._positive_int, default=4)
    p.add_argument("--frequency-bound", type=shared._finite_float, default=.309)
    p.add_argument("--frequency-step", type=shared._finite_float,
                   help="maximum spacing; default min(0.01 Hz, 1/(4*template-support seconds))")
    p.add_argument("--attempts-per-horizon", type=shared._positive_int, default=1440)
    p.add_argument("--duration-alternatives", type=shared._positive_int, default=1)
    p.add_argument("--model-alternatives", type=shared._positive_int, default=1)
    p.add_argument("--horizon-seconds", type=shared._finite_float, default=86400.)
    p.add_argument("--false-per-horizon", type=shared._finite_float, default=1e-4)
    p.add_argument("--seed", type=lambda x: shared._integer(x,"seed",0,shared.MASK64), default=1949)
    p.add_argument("--reference-trials", type=lambda x: shared._integer(x,"reference-trials",0,1000000), default=8)
    p.add_argument("--reference-points", type=shared._positive_int, default=4)
    p.add_argument("--memory-gib", type=shared._memory_gib, default=4.)
    p.add_argument("--cpu-threads", type=shared._positive_int, default=min(4,os.cpu_count() or 1))
    p.add_argument("--trial-details", action="store_true")
    p.add_argument("--output", type=Path)
    return p


def synchronize(device):
    if device.type == "cuda":
        torch.cuda.synchronize(device)


def wilson(events, count):
    if not count:return None
    z=1.959963984540054
    probability=events/count
    denominator=1+z*z/count
    center=(probability+z*z/(2*count))/denominator
    radius=z*math.sqrt(probability*(1-probability)/count+z*z/(4*count*count))/denominator
    return [max(0.,center-radius),min(1.,center+radius)]


def select_templates(data):
    if data["basis_shape"][2] != 1:
        raise ValueError("this acquisition study requires rank-one exported templates")
    params=data["candidateparams"]
    if any("arrival_seconds" not in row or "frequency_hz" not in row or "epoch_offset_seconds" not in row for row in params):
        raise ValueError("candidate arrival, frequency and epoch metadata are required")
    minimum=min(float(row["arrival_seconds"]) for row in params)
    selected=[i for i,row in enumerate(params)
              if abs(float(row["arrival_seconds"])-minimum)<1e-12 and abs(float(row["frequency_hz"]))<1e-12]
    if not selected:
        raise ValueError("export must include zero-CFO candidates at its minimum arrival")
    varying={"bit","epoch_offset_seconds"}
    common={k:v for k,v in params[selected[0]].items() if k not in varying}
    identities=set()
    for index in selected:
        row=params[index]
        if {k:v for k,v in row.items() if k not in varying}!=common:
            raise ValueError("selected public bank must vary only in source bit and epoch")
        identity=(data["bitlabels"][index],row["epoch_offset_seconds"])
        if identity in identities:raise ValueError("duplicate source-bit/epoch template")
        identities.add(identity)
        if row.get("bit",data["bitlabels"][index])!=data["bitlabels"][index]:
            raise ValueError("candidate bit metadata disagrees with bitlabels")
    epochs={epoch for _,epoch in identities}
    if identities!={(bit,epoch) for epoch in epochs for bit in (0,1)}:
        raise ValueError("every selected epoch needs both source-bit alternatives")
    return selected,minimum


class Workspace:
    def __init__(self,data,manifest,args):
        self.device=shared._resolve_device(args.device)
        self.samples=data["basis_shape"][1]
        self.sample_rate=data["sample_rate"]
        self.indices,self.base_arrival=select_templates(data)
        self.params=[data["candidateparams"][i] for i in self.indices]
        self.labels=[data["bitlabels"][i] for i in self.indices]
        self.count=len(self.indices)
        self.nfft=1 << (2*self.samples-2).bit_length()
        self.templates_cpu=data["basis"][self.indices,:,0].to(dtype=torch.complex128,device="cpu").contiguous()
        if not bool(torch.isfinite(self.templates_cpu).all()):raise ValueError("nonfinite template samples")
        self.energies_cpu=self.templates_cpu.abs().square().sum(dim=1)
        if bool((self.energies_cpu<=0).any()):raise ValueError("zero-energy public template")
        support=[]
        for row in self.templates_cpu:
            nonzero=torch.nonzero(row!=0,as_tuple=False).flatten()
            support.append((int(nonzero[0]),int(nonzero[-1])))
        max_lag=round(args.lag_max_seconds*self.sample_rate)
        if max_lag<0:raise ValueError("negative lag extent")
        if max(last for _,last in support)+max_lag>=self.samples:
            raise ValueError("complete template support leaves the capture at the largest lag; export a longer capture")
        self.lags=list(range(0,max_lag+1,args.lag_step_samples))
        if self.lags[-1]!=max_lag:self.lags.append(max_lag)
        self.support=support
        self.support_seconds=max(last-first+1 for first,last in support)/self.sample_rate
        spacing=args.frequency_step if args.frequency_step is not None else min(.01,1/(4*self.support_seconds))
        if not math.isfinite(spacing) or spacing<=0:raise ValueError("frequency spacing must be positive")
        if args.frequency_bound<0:raise ValueError("frequency bound must be nonnegative")
        half=math.ceil(args.frequency_bound/spacing)
        self.frequency_step=args.frequency_bound/half if half else 0.
        self.frequencies=[k*self.frequency_step for k in range(-half,half+1)]
        self.hypotheses=self.count*len(self.lags)*len(self.frequencies)
        self.family=self.hypotheses*args.attempts_per_horizon*args.duration_alternatives*args.model_alternatives
        self.threshold,self.threshold_method,self.threshold_fallback=shared._gamma_threshold(1,math.log(self.family)-math.log(args.false_per_horizon),"exact")
        self.frequency_tile=min(args.frequency_tile,len(self.frequencies))
        # Conservative array/work reserve, including CPU FP64 public templates,
        # full cached CFO phasors and cuFFT product/output/workspace temporaries.
        self.fixed_bytes=self.count*self.samples*48+self.count*self.nfft*16+len(self.frequencies)*self.samples*48+self.samples*64
        per_trial=lambda tile:tile*(self.count*self.nfft*48+self.nfft*48+self.count*len(self.lags)*24)+self.samples*48
        capacity=int(args.memory_gib*1024**3)
        while self.frequency_tile>1 and self.fixed_bytes+per_trial(self.frequency_tile)>capacity:
            self.frequency_tile-=1
        self.batch=min(args.batch,(capacity-self.fixed_bytes)//max(1,per_trial(self.frequency_tile)))
        if self.batch<1:raise ValueError("complete FFT bank does not fit --memory-gib; no candidates were pruned")
        self.workspace_bytes=self.fixed_bytes+self.batch*per_trial(self.frequency_tile)
        self.energies=self.energies_cpu.to(self.device)
        template=self.templates_cpu.to(device=self.device,dtype=torch.complex64)
        self.template_fft_conjugate=torch.fft.fft(template,n=self.nfft,dim=-1).conj().contiguous()
        self.lag_tensor=torch.tensor(self.lags,dtype=torch.int64,device=self.device)
        times=torch.arange(self.samples,device=self.device,dtype=torch.float64)/self.sample_rate
        frequency=torch.tensor(self.frequencies,device=self.device,dtype=torch.float64)
        self.phasors=torch.exp((-2j*math.pi)*frequency[:,None]*times[None,:]).to(torch.complex64)
        self.signal=data["signal"].to(device=self.device,dtype=torch.complex64)
        if not bool(torch.isfinite(self.signal).all()) or float(self.signal.abs().square().sum())<=0:
            raise ValueError("export a finite positive signal; nulls are independently generated by this tool")
        self.random_reference_points=args.reference_points
        self.reference_points=args.reference_points+2
        self.reference_trials=args.reference_trials
        self.reference_seed=args.seed

    def scan(self,observations,point_specs):
        batch=observations.shape[0]
        best=torch.full((batch,),-torch.inf,dtype=torch.float64,device=self.device)
        best_code=torch.full((batch,),-1,dtype=torch.int64,device=self.device)
        random_scores=torch.full((batch,self.reference_points),torch.nan,dtype=torch.float64,device=self.device)
        for first in range(0,len(self.frequencies),self.frequency_tile):
            last=min(len(self.frequencies),first+self.frequency_tile)
            demodulated=observations[:,None,:]*self.phasors[None,first:last,:]
            observed_fft=torch.fft.fft(demodulated,n=self.nfft,dim=-1)
            correlation=torch.fft.ifft(observed_fft[:,:,None,:]*self.template_fft_conjugate[None,None,:,:],dim=-1)
            scores=correlation[:,:,:,self.lag_tensor].abs().square().to(torch.float64)/self.energies[None,None,:,None]
            value,index=scores.flatten(start_dim=1).max(dim=1)
            code=index+first*self.count*len(self.lags)
            better=value>best
            best=torch.where(better,value,best)
            best_code=torch.where(better,code,best_code)
            targets=[(b,p,f-first,c,l) for b,points in enumerate(point_specs)
                     for p,(f,c,l) in enumerate(points) if first<=f<last]
            if targets:
                b,p,f,c,l=map(list,zip(*targets))
                random_scores[b,p]=scores[b,f,c,l]
            del demodulated,observed_fft,correlation,scores
        if not bool(torch.isfinite(best).all()):raise ValueError("FFT acquisition produced nonfinite statistics")
        return best.cpu(),best_code.cpu(),random_scores.cpu()

    def decode_index(self,code):
        f,remainder=divmod(int(code),self.count*len(self.lags))
        c,l=divmod(remainder,len(self.lags))
        return f,c,l

    def reference(self,observation,f,c,l):
        lag=self.lags[l]
        indices=torch.arange(lag,self.samples,dtype=torch.float64)
        phase=torch.exp((-2j*math.pi*self.frequencies[f]/self.sample_rate)*indices)
        value=(self.templates_cpu[c,:self.samples-lag].conj()*observation[lag:].to(torch.complex128)*phase).sum()
        return float(value.abs().square()/self.energies_cpu[c])


def collect(work,data,args,kind,cn0=None):
    count=args.trials
    is_signal=kind=="positive"
    variance=data["noise_reference_power"]*work.sample_rate/10**(cn0/10) if is_signal else None
    scale=1/math.sqrt(variance) if is_signal else 0.
    if is_signal and (not math.isfinite(scale) or scale<=0):raise ValueError("unsupported C/N0 scale")
    seed=shared._splitmix64(args.seed^shared.POSITIVE_SALT) if is_signal else args.seed
    rows=[]
    timings={"noise_generation_seconds":0.,"resident_fft_seconds":0.,"reference_transfer_seconds":0.,"cpu_fp64_point_reference_seconds":0.}
    reference={"captures":0,"selected_points":0,"boundary_points":0,"random_points":0,"maximum_absolute_score_error":0.,
               "maximum_relative_score_error":0.,"admission_disagreements":0,"absolute_tolerance":1e-3,"relative_tolerance":5e-5}
    begun=time.perf_counter();next_progress=begun+30
    for first in range(0,count,work.batch):
        last=min(count,first+work.batch)
        synchronize(work.device);start=time.perf_counter()
        observations=torch.stack([shared._noise_horizon(work.samples,1.,shared._seed(seed,"holdout",i,"null"),work.device) for i in range(first,last)])
        if is_signal:observations+=work.signal[None,:]*scale
        synchronize(work.device);timings["noise_generation_seconds"]+=time.perf_counter()-start
        points=[]
        for i in range(first,last):
            rng=random.Random(shared._seed(args.seed^0x465446504F494E54,"holdout",i,"positive" if is_signal else "null"))
            points.append([(0,0,0),(len(work.frequencies)-1,work.count-1,len(work.lags)-1)]+
                          [(rng.randrange(len(work.frequencies)),rng.randrange(work.count),rng.randrange(len(work.lags)))
                           for _ in range(work.random_reference_points)] if i<work.reference_trials else [])
        start=time.perf_counter();best,codes,point_scores=work.scan(observations,points)
        synchronize(work.device);timings["resident_fft_seconds"]+=time.perf_counter()-start
        reference_count=max(0,min(last,work.reference_trials)-first)
        start=time.perf_counter()
        reference_observations=observations[:reference_count].cpu() if reference_count else None
        timings["reference_transfer_seconds"]+=time.perf_counter()-start
        start=time.perf_counter()
        for b in range(last-first):
            f,c,l=work.decode_index(codes[b])
            score=float(best[b]);accepted=score>=work.threshold
            row={"trial":first+b,"score":score,"accepted":accepted,"bit":work.labels[c],
                 "epoch_offset_seconds":work.params[c]["epoch_offset_seconds"],
                 "arrival_seconds":work.base_arrival+work.lags[l]/work.sample_rate,
                 "frequency_hz":work.frequencies[f],"template_index":c,"lag_samples":work.lags[l],"frequency_index":f}
            if is_signal:
                row["correct_bit"]=row["bit"]==data["signal_bit"]
                row["correct_epoch"]=row["epoch_offset_seconds"]==0
                if "truth_arrival_seconds" in data["manifest"]:row["timing_error_seconds"]=row["arrival_seconds"]-data["manifest"]["truth_arrival_seconds"]
                if "truth_frequency_hz" in data["manifest"]:row["frequency_error_hz"]=row["frequency_hz"]-data["manifest"]["truth_frequency_hz"]
            if b<reference_count:
                tests=[(f,c,l,score,"selected")]+[(pf,pc,pl,float(point_scores[b,p]),"boundary" if p<2 else "random") for p,(pf,pc,pl) in enumerate(points[b])]
                reference["captures"]+=1
                for pf,pc,pl,measured,label in tests:
                    exact=work.reference(reference_observations[b],pf,pc,pl)
                    error=abs(measured-exact)
                    reference[label+"_points"]+=1
                    reference["maximum_absolute_score_error"]=max(reference["maximum_absolute_score_error"],error)
                    reference["maximum_relative_score_error"]=max(reference["maximum_relative_score_error"],error/max(1e-12,abs(exact)))
                    reference["admission_disagreements"]+=int((measured>=work.threshold)!=(exact>=work.threshold))
                    if label=="selected":row["selected_cpu_fp64_score"]=exact
                    if error>reference["absolute_tolerance"]+reference["relative_tolerance"]*abs(exact):
                        raise ValueError(f"FFT/direct mismatch at {kind} trial {first+b}, {label} point: FFT={measured}, direct={exact}")
            rows.append(row)
        timings["cpu_fp64_point_reference_seconds"]+=time.perf_counter()-start
        if time.perf_counter()>next_progress:
            print(f"triad-fft-acquire: {kind} C/N0={cn0} trials={last}/{count}",file=sys.stderr,flush=True)
            next_progress=time.perf_counter()+30
    timings["wall_seconds"]=time.perf_counter()-begun
    admitted=sum(row["accepted"] for row in rows)
    summary={"trials":count,"admitted":admitted,"admission_wilson95":wilson(admitted,count),"timing":timings,"fp64_reference":reference}
    if is_signal:
        correct=sum(row["accepted"] and row["correct_bit"] for row in rows)
        scheduled=sum(row["accepted"] and row["correct_bit"] and row["correct_epoch"] for row in rows)
        summary.update(correct_accepted=correct,wrong_accepted=admitted-correct,missed=count-admitted,
                       raw_bit_errors=sum(not row["correct_bit"] for row in rows),
                       correct_bit_and_epoch_accepted=scheduled,
                       correct_accepted_wilson95=wilson(correct,count),
                       correct_bit_and_epoch_accepted_wilson95=wilson(scheduled,count),
                       sample_noise_variance=variance)
        for field in ("timing_error_seconds","frequency_error_hz"):
            values=[row[field] for row in rows if field in row]
            summary[field]={"rmse":math.sqrt(sum(x*x for x in values)/len(values)),"max_abs":max(abs(x) for x in values)} if values else None
    else:
        summary.update(null_admissions=admitted,zero_event_one_sided95_upper=-math.expm1(math.log(.05)/count) if admitted==0 else None)
    summary["maximum_score"]=max(row["score"] for row in rows)
    summary["minimum_score"]=min(row["score"] for row in rows)
    if args.trial_details:summary["trial_details"]=rows
    return summary


def evaluate_noiseless_projection(work,data):
    """Evaluator-only hidden-mean diagnostic; never informs acquisition."""
    synchronize(work.device)
    started=time.perf_counter()
    score,codes,_=work.scan(work.signal[None,:],[[]])
    synchronize(work.device)
    f,c,l=work.decode_index(codes[0])
    signal_cpu=work.signal.to(device="cpu",dtype=torch.complex128)
    sample_energy=float(signal_cpu.abs().square().sum())
    reference=work.reference(signal_cpu,f,c,l)
    if abs(float(score[0])-reference)>1e-3+5e-5*reference:
        raise ValueError("noiseless evaluator FFT/direct projection mismatch")
    return {"scope":"evaluator-only exhaustive mean-fit diagnostic; hidden noiseless signal does not select receiver templates, grids, thresholds or noisy winners",
            "sample_energy":sample_energy,"selected_projection_energy_samples_fp64":reference,
            "selected_projection_fraction":reference/sample_energy,
            "full_scan_selected_score_unit_noise_fp32":float(score[0]),
            "selected_score_absolute_fp32_fp64_difference":abs(float(score[0])-reference),
            "selected_template_index":c,"selected_global_grid_index":int(codes[0]),
            "selected_bit":work.labels[c],"selected_epoch_offset_seconds":work.params[c]["epoch_offset_seconds"],
            "selected_bit_matches_export":work.labels[c]==data["signal_bit"],
            "selected_epoch_is_nominal":work.params[c]["epoch_offset_seconds"]==0,
            "selected_lag_samples":work.lags[l],"selected_arrival_seconds":work.base_arrival+work.lags[l]/work.sample_rate,
            "selected_frequency_hz":work.frequencies[f],
            "precision_scope":"full-grid FP32 maximum, selected-point CPU FP64 projection; no full-grid FP64 ranking claim",
            "evaluation_seconds":time.perf_counter()-started}


def run(args):
    if args.horizon_seconds<=0 or not 0<args.false_per_horizon<1 or args.lag_max_seconds<0:
        raise ValueError("invalid horizon, false-admission target or lag extent")
    torch.set_num_threads(args.cpu_threads)
    shared._configure_torch()
    directory,manifest=shared._load_manifest(args.manifest)
    data=shared._validate_manifest(directory,manifest)
    if data["signal_bit"] is None or manifest.get("control") not in (None,"signal"):
        raise ValueError("positive signal export with known source bit required for scoring")
    started=time.perf_counter();work=Workspace(data,manifest,args);synchronize(work.device)
    prepare=time.perf_counter()-started
    noiseless=evaluate_noiseless_projection(work,data)
    cn0s=args.cn0_grid if args.cn0_grid is not None else [float(manifest.get("cn0_db_hz",8.))]
    null=collect(work,data,args,"null")
    for cn0 in cn0s:
        if not -300<cn0/10<300:raise ValueError("C/N0 numeric range exceeded")
        positive=collect(work,data,args,"positive",cn0)
        benchmark=dict(noiseless)
        noncentrality=noiseless["selected_projection_energy_samples_fp64"]/positive["sample_noise_variance"]
        benchmark.update(selected_energy_noncentrality=noncentrality,
                         total_available_energy_snr=noiseless["sample_energy"]/positive["sample_noise_variance"],
                         chi_square_degrees_of_freedom=2,chi_square_noncentrality=2*noncentrality,
                         chi_square_score_threshold=2*work.threshold,
                         reference_probability_scope="P(chi-square(df=2, noncentrality=2*lambda) >= 2*threshold) for this fixed deterministic mean-selected point; not whole-bank correct reception; probability evaluated externally")
        yield {"schema":"triad-fft-acquire-v1","source_hash":data["source_hash"],"source_manifest":str(args.manifest),
            "script_sha256":SCRIPT_SHA256,"shared_helper_sha256":HELPER_SHA256,
            "torch_version":torch.__version__,"device":str(work.device),
            "device_name":torch.cuda.get_device_name(work.device) if work.device.type=="cuda" else "CPU",
            "cn0_db_hz":cn0,"sample_rate":work.sample_rate,"capture_samples":work.samples,
            "capture_seconds":work.samples/work.sample_rate,"nfft":work.nfft,
            "template_count":work.count,"template_indices_in_export":work.indices,"template_params":work.params,
            "fixed_public_nuisance_parameters":{k:v for k,v in work.params[0].items() if k not in ("bit","epoch_offset_seconds","arrival_seconds","frequency_hz")},
            "template_support_samples":work.support,"maximum_template_support_seconds":work.support_seconds,
            "template_energy_samples":work.energies_cpu.tolist(),
            "selection_scope":"public minimum-arrival, zero-CFO epoch/bit templates; duplicate exported acquisition coordinates replaced by the complete declared FFT grid, without observation-dependent pruning",
            "lag_count":len(work.lags),"lag_min_samples":work.lags[0],"lag_max_samples":work.lags[-1],
            "lag_step_samples":args.lag_step_samples,"last_lag_interval_samples":work.lags[-1]-work.lags[-2] if len(work.lags)>1 else 0,
            "frequency_count":len(work.frequencies),"frequency_bound_hz":args.frequency_bound,"frequency_spacing_hz":work.frequency_step,
            "hypotheses_per_capture":work.hypotheses,"scheduled_attempts_per_horizon":args.attempts_per_horizon,
            "duration_alternatives":args.duration_alternatives,"model_alternatives":args.model_alternatives,
            "total_horizon_hypotheses":work.family,"horizon_seconds":args.horizon_seconds,
            "false_admission_probability_per_horizon":args.false_per_horizon,"threshold":work.threshold,
            "threshold_method":work.threshold_method,"threshold_fallback":work.threshold_fallback,
            "threshold_assumptions":"known white circular complex Gaussian sample noise, fixed deterministic full-support rank-one templates, prespecified finite grid/attempt/duration/model family; union bound does not require independent hypotheses or attempts",
            "receiver_model":"one unknown constant shared complex gain per full template, fixed exported clock/drift/tap parameters; frequency-selective and stochastic phase evolution are model mismatch",
            "linear_correlation":"nfft >= 2N-1; lag score=sum(conj(template[n])*demodulated_observation[n+lag]); complete support is checked for every lag",
            "grid_limit":"exhaustive over declared integer timing/CFO/epoch/bit grid; continuous parameter mismatch is not removed by this claim",
            "observed_null_scope":"independent single-capture null trials; operational family is analytically budgeted, not empirically simulated over a full horizon",
            "channel":manifest.get("channel"),"channel_realizations":1,"probability_conditioning":"fixed exported sampled signal/channel realization",
            "positive":positive,"null":null,"null_holdout_reused_across_cn0":True,"positive_noise_paired_across_cn0":True,
            "noiseless_projection_benchmark":benchmark,
            "strict_fp32_bulk":True,"tf32_enabled":False,"reference_scope":"CPU complex128 direct-dot scores on identical FP32 observations for selected maxima, both extreme lag/CFO boundaries and independently chosen grid points; a bounded capture subset, not full-grid FP64 ranking",
            "actual_batch":work.batch,"actual_frequency_tile":work.frequency_tile,"workspace_estimate_bytes":work.workspace_bytes,
            "memory_scope":"conservative array/workspace estimate including CPU FP64 templates; excludes framework runtime and allocator cache",
            "cached_template_ffts":True,"cached_cfo_phasors":True,
            "fft_plan_scope":"PyTorch/cuFFT shape-based plan cache reused across batches; final short tiles can use a separate cached plan",
            "prepare_seconds":prepare,"cpu_threads":args.cpu_threads,"seed":args.seed}


def main():
    args=parser().parse_args()
    try:
        if args.output:
            args.output.parent.mkdir(parents=True,exist_ok=True)
            stream=args.output.open("w",encoding="utf-8")
        else:stream=sys.stdout
        try:
            for result in run(args):
                stream.write(json.dumps(result,separators=(",",":"),sort_keys=True,allow_nan=False)+"\n")
                stream.flush()
        finally:
            if args.output:stream.close()
        return 0
    except (ValueError,RuntimeError,OSError,OverflowError) as error:
        print(f"triad-fft-acquire: {error}",file=sys.stderr,flush=True)
        return 2


if __name__=="__main__":raise SystemExit(main())
