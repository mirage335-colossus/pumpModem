#!/usr/bin/env python3
"""Known-unit-gain shared-phase inference on exported three-carrier waveforms.

The finite-state phase model is a normalized Bayesian likelihood, not a GLRT
or a claim of optimal continuous-state inference. The sufficient sampler keeps
all candidate correlations within each disjoint raw block. Optional raw mode
projects independently generated noise samples through the actual templates.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import math
import pathlib
import sys
import time
import torch


def _raw_module():
    path=pathlib.Path(__file__).with_name("triad-gpu-study.py")
    spec=importlib.util.spec_from_file_location("triad_phase_raw_helpers",path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module


raw=_raw_module()
SCHEMA="triad-shared-phase-v1"
PHASE_SALT=0x50484153454C5231


def _phase_grid(text):
    values=[raw._integer(x.strip(),"phase states",8,4096) for x in text.split(",")]
    if not values or len(set(values))!=len(values):raise argparse.ArgumentTypeError("phase state counts must be unique")
    return values


def parser():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("manifest",type=pathlib.Path)
    p.add_argument("--device",default="cpu")
    p.add_argument("--trials",type=lambda x:raw._integer(x,"trials",1,100000),default=64)
    p.add_argument("--calibration-trials",type=lambda x:raw._integer(x,"calibration-trials",0,100000),default=0)
    p.add_argument("--batch",type=raw._positive_int,default=16)
    p.add_argument("--block-seconds",type=raw._finite_float,default=16.)
    p.add_argument("--phase-diffusion",type=raw._finite_float,required=True,help="public phase RMS rad/sqrt(second)")
    p.add_argument("--phases",type=_phase_grid,default=[64,128])
    p.add_argument("--noise-mode",choices=("sufficient","raw","both"),default="sufficient")
    p.add_argument("--cn0-grid",type=raw._parse_cn0_grid)
    p.add_argument("--seed",type=lambda x:raw._integer(x,"seed",0,(1<<64)-1),default=1)
    p.add_argument("--false-alarm-budget",type=raw._finite_float,default=.01)
    p.add_argument("--empirical-false-budget",type=raw._finite_float,default=.01,
                   help="separate one-exercised-bank predictive calibration target")
    p.add_argument("--family-hypotheses",type=raw._positive_int)
    p.add_argument("--duration-alternatives",type=raw._positive_int,default=1)
    p.add_argument("--memory-gib",type=raw._memory_gib,default=4.)
    p.add_argument("--allow-model-mismatch",action="store_true")
    p.add_argument("--output",type=pathlib.Path)
    return p


def _sync(device):
    if device.type=="cuda":torch.cuda.synchronize(device)


def _prepare(data,block_samples,memory_bytes):
    candidates,samples,columns=data["basis_shape"]
    if columns!=1:raise ValueError("phase diagnostic requires K1; no multipath nuisance fit is silently removed")
    if candidates>64:raise ValueError("this bounded phase diagnostic supports at most64 candidates; no bank truncation")
    if set(data["bitlabels"])!={0,1}:raise ValueError("binary phase study needs both bit hypotheses")
    blocks=[(first,min(samples,first+block_samples)) for first in range(0,samples,block_samples)]
    estimate=32*candidates*samples+64*samples+128*len(blocks)*candidates*candidates+4*1024**2
    if estimate>memory_bytes:raise ValueError("memory cap cannot fit block covariance preparation")
    signal=data["signal"]
    if not bool(torch.isfinite(signal).all()):raise ValueError("signal contains nonfinite samples")
    signal_energy=float(signal.abs().to(torch.float64).square().sum())
    if not math.isfinite(signal_energy) or signal_energy<=0:raise ValueError("signal must have positive finite energy")
    gram=[];means=[];energies=[];factors=[];maximum_error=0.;minimum_eigenvalue=0.
    for first,end in blocks:
        templates=data["basis"][:,first:end,0].to(torch.complex128)
        if not bool(torch.isfinite(templates).all()):raise ValueError("basis contains nonfinite samples")
        covariance=templates.conj()@templates.transpose(0,1)
        covariance=(covariance+covariance.conj().transpose(0,1))*.5
        values,vectors=torch.linalg.eigh(covariance)
        scale=max(1.,float(values.max()))
        minimum_eigenvalue=min(minimum_eigenvalue,float(values.min()))
        if float(values.min()) < -1e-10*scale:raise ValueError("block Gram has a substantive negative eigenvalue")
        factor=vectors*values.clamp_min(0).sqrt()[None,:]
        error=float(torch.linalg.vector_norm(factor@factor.conj().transpose(0,1)-covariance))/max(float(torch.linalg.vector_norm(covariance)),1e-300)
        maximum_error=max(maximum_error,error)
        gram.append(covariance);factors.append(factor)
        means.append(templates.conj()@signal[first:end].to(torch.complex128))
        energies.append(covariance.diagonal().real.clamp_min(0))
    if maximum_error>1e-9:raise ValueError("block covariance factorization failed")
    energy_table=torch.stack(energies)
    mean_table=torch.stack(means)
    block_projection=torch.where(energy_table>0,mean_table.abs().square()/energy_table.clamp_min(torch.finfo(torch.float64).tiny),0).sum(0)/signal_energy
    full_projection=mean_table.sum(0).abs().square()/(energy_table.sum(0).clamp_min(torch.finfo(torch.float64).tiny)*signal_energy)
    if bool((energy_table.sum(0)<=0).any()):raise ValueError("phase study contains an empty candidate waveform")
    return blocks,torch.stack(factors),mean_table,energy_table,{
        "preparation_workspace_bound_bytes":estimate,"maximum_covariance_relative_error":maximum_error,
        "minimum_block_covariance_eigenvalue":minimum_eigenvalue,"signal_energy_samples":signal_energy,
        "block_projection_fractions_by_candidate":block_projection.tolist(),
        "full_window_projection_fractions_by_candidate":full_projection.tolist(),
        "projection_fraction_scope":"noiseless evaluator diagnostics only; never used as receiver phase or gain information"}


def _kernel(states,dt,diffusion,device):
    step=2*math.pi/states
    frequency=torch.arange(states//2+1,dtype=torch.float64,device=device)
    multiplier=torch.exp(-(diffusion*diffusion)*dt*(1-torch.cos(2*math.pi*frequency/states))/(step*step))
    kernel=torch.fft.irfft(multiplier,n=states)
    minimum=float(kernel.min())
    if minimum < -1e-12:raise ValueError("discrete phase transition has a substantive negative probability")
    kernel=kernel.clamp_min(0);kernel/=kernel.sum()
    angle=torch.arange(states,dtype=torch.float64,device=device)*step
    first_moment=float((kernel*angle.cos()).sum())
    discrete=math.exp(-(diffusion*diffusion)*dt*(1-math.cos(step))/(step*step))
    continuous=math.exp(-.5*diffusion*diffusion*dt)
    return torch.fft.rfft(kernel),{"minimum_transition_probability_before_clamp":minimum,
        "transition_first_moment":first_moment,"discrete_generator_first_moment":discrete,
        "continuous_brownian_first_moment":continuous}


def _forward(correlations,energies,states,block_lengths,sample_rate,diffusion,kernels):
    trials,blocks,candidates=correlations.shape
    weights=torch.full((trials,candidates,states),1/states,dtype=torch.float64,device=correlations.device)
    total=torch.zeros((trials,candidates),dtype=torch.float64,device=correlations.device)
    phase=torch.arange(states,dtype=torch.float64,device=correlations.device)*(2*math.pi/states)
    cosine,sine=phase.cos(),phase.sin()
    next_progress=time.perf_counter()+30
    for block in range(blocks):
        if block%64==0 and time.perf_counter()>=next_progress:
            print(f"triad-phase-study: forward states={states} block={block}/{blocks}",file=sys.stderr,flush=True)
            next_progress=time.perf_counter()+30
        if block:
            key=block_lengths[block-1]+block_lengths[block]
            multiplier=kernels[key][0]
            weights=torch.fft.irfft(torch.fft.rfft(weights,dim=-1)*multiplier,n=states,dim=-1)
            weights=weights.clamp_min(0)
            weights/=weights.sum(-1,keepdim=True)
        correlation=correlations[:,block,:]
        emission=2*(correlation.real[:,:,None]*cosine+correlation.imag[:,:,None]*sine)-energies[block,:,None]
        logarithm=weights.log()+emission
        increment=torch.logsumexp(logarithm,dim=-1)
        weights=torch.softmax(logarithm,dim=-1)
        total+=increment
    if not bool(torch.isfinite(total).all()):raise ValueError("phase forward likelihood became nonfinite")
    return total


def _best(scores,thresholds):
    excess=scores-thresholds[None,:]
    value,candidate=excess.max(1)
    selected=scores.gather(1,candidate[:,None]).squeeze(1)
    attempt=torch.zeros_like(candidate)
    return value,selected,candidate,attempt


def _empirical_threshold(best,alpha):
    count=best.shape[2]
    if not count:return None,{"status":"disabled","calibration_trials":0}
    order=math.ceil((count+1)*(1-alpha))
    if order>count:return None,{"status":"insufficient_calibration_for_target","calibration_trials":count,
        "target_false_probability":alpha,"minimum_calibration_trials":math.ceil(1/alpha)-1}
    value=float(best[0,1].sort().values[order-1])
    return math.nextafter(value,math.inf),{"status":"exchangeable_order_statistic",
        "calibration_trials":count,"one_based_order":order,"target_false_probability":alpha,
        "marginal_predictive_false_bound":(count+1-order)/(count+1),
        "scope":"one exercised bank/window only; not larger unexercised family or adaptive algorithm selection"}


def run(args):
    directory,manifest=raw._load_manifest(args.manifest);data=raw._validate_manifest(directory,manifest)
    device=raw._resolve_device(args.device);raw._configure_torch()
    if not 0<args.false_alarm_budget<1 or not 0<args.empirical_false_budget<1:raise ValueError("false budgets must lie in(0,1)")
    if args.block_seconds<=0 or args.phase_diffusion<0:raise ValueError("invalid block duration or phase diffusion")
    channel=manifest.get("channel","unspecified")
    mismatch_reasons=[]
    if channel not in ("none","flat","off","unspecified"):
        mismatch_reasons.append(f"nonflat_channel:{channel}")
    signal_diffusion=manifest.get("relative_phase_diffusion_rad_sqrt_s")
    if signal_diffusion is None:
        mismatch_reasons.append("phase_diffusion_metadata_unavailable")
    elif not math.isclose(float(signal_diffusion),args.phase_diffusion,rel_tol=1e-6,abs_tol=1e-12):
        mismatch_reasons.append(f"public_diffusion:{args.phase_diffusion}!=signal_diffusion:{signal_diffusion}")
    for key in ("relative_white_fm_hz_sqrt_s","relative_random_walk_fm_hz_sqrt_s",
                "relative_flicker_fm_rms_hz","relative_white_pm_rms_rad"):
        if manifest.get(key,0):mismatch_reasons.append(f"additional_frequency_or_phase_noise:{key}={manifest[key]}")
    mismatch=bool(mismatch_reasons)
    if mismatch and not args.allow_model_mismatch:
        raise ValueError("phase model mismatch requires --allow-model-mismatch: "+";".join(mismatch_reasons))
    if data["signal_start_samples"]!=0:raise ValueError("phase study requires signal already positioned inside its single exported window")
    candidates,samples,columns=data["basis_shape"]
    block_samples=samples if args.block_seconds>=samples/data["sample_rate"] else max(1,round(args.block_seconds*data["sample_rate"]))
    if not math.isfinite(args.phase_diffusion*args.phase_diffusion):raise ValueError("phase diffusion is outside supported numeric range")
    memory=int(args.memory_gib*1024**3)
    blocks,factors_cpu,means_cpu,energies_cpu,model_info=_prepare(data,block_samples,memory)
    lengths=[end-first for first,end in blocks]
    factors=factors_cpu.to(device=device,dtype=torch.complex128)
    means=means_cpu.to(device=device);energy_base=energies_cpu.to(device=device)
    family=candidates*args.duration_alternatives
    if args.family_hypotheses is not None:
        if args.family_hypotheses<family:raise ValueError("family count omits candidates/durations")
        family=args.family_hypotheses
    log_threshold=math.log(family)-math.log(args.false_alarm_budget)
    ranks=(energies_cpu>0).sum(0).tolist()
    whole_threshold=raw._gamma_threshold(1,log_threshold,"exact")[0]
    block_thresholds=[raw._gamma_threshold(int(rank),log_threshold,"exact")[0] if rank else math.inf for rank in ranks]
    thresholds={"constant_phase_glrt":torch.full((candidates,),whole_threshold,dtype=torch.float64,device=device),
        "independent_block_energy":torch.tensor(block_thresholds,dtype=torch.float64,device=device)}
    kernels={};kernel_info={}
    for states in args.phases:
        kernels[states]={};kernel_info[states]=[]
        for key in sorted(set(lengths[i-1]+lengths[i] for i in range(1,len(lengths)))):
            item=_kernel(states,key/(2*data["sample_rate"]),args.phase_diffusion,device)
            kernels[states][key]=item;kernel_info[states].append(item[1])
        thresholds[f"shared_phase_{states}"]=torch.full((candidates,),log_threshold,dtype=torch.float64,device=device)
    methods=list(thresholds)
    modes=("sufficient","raw") if args.noise_mode=="both" else (args.noise_mode,)
    cases=[]
    if args.cn0_grid is None and "noise_variance" in manifest:cases=[(manifest.get("cn0_db_hz"),float(manifest["noise_variance"]))]
    else:
        for cn0 in args.cn0_grid if args.cn0_grid is not None else [float(manifest["cn0_db_hz"])]:
            if not -300<cn0/10<300:raise ValueError("C/N0 outside representable variance range")
            cases.append((cn0,data["noise_reference_power"]*data["sample_rate"]/10**(cn0/10)))
    batch=min(args.batch,max(args.trials,args.calibration_trials,1))
    bookkeeping=64*len(methods)*(args.trials+args.calibration_trials)+64*args.trials
    persistent=model_info["preparation_workspace_bound_bytes"]+bookkeeping
    def peak():return persistent+batch*(samples*32 if "raw" in modes else 0)+batch*len(blocks)*candidates*96+batch*candidates*max(args.phases)*64
    while batch>1 and peak()>memory:batch=max(1,batch//2)
    if peak()>memory:raise ValueError("memory cap cannot fit one phase inference trial")
    epoch_offsets=[p.get("epoch_offset_seconds",0) for p in data["candidateparams"]]
    script_hash=hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest()
    for mode in modes:
        for cn0,variance in cases:
            if variance<=0 or not math.isfinite(variance):raise ValueError("invalid complex sample variance")
            energies=energy_base/variance
            true_mean=means/variance
            calibration={}
            for split,total_trials in (("calibration",args.calibration_trials),("holdout",args.trials)):
                if total_trials==0:continue
                best={method:raw._new_best(total_trials) for method in methods}
                maximum_grid_difference=0.;grid_decision_differences=0
                started=time.perf_counter();next_progress=started+30
                for first in range(0,total_trials,batch):
                    ids=list(range(first,min(total_trials,first+batch)))
                    if mode=="sufficient":
                        white=[]
                        for index in ids:
                            generator=torch.Generator(device=device);generator.manual_seed(raw._seed(args.seed^PHASE_SALT,split,index,"null")&((1<<63)-1))
                            real=torch.randn((len(blocks),candidates),dtype=torch.float64,device=device,generator=generator)*math.sqrt(.5)
                            imag=torch.randn((len(blocks),candidates),dtype=torch.float64,device=device,generator=generator)*math.sqrt(.5)
                            white.append(torch.complex(real,imag))
                        noise=torch.einsum("tbr,bcr->tbc",torch.stack(white),factors)/math.sqrt(variance)
                    else:
                        raw_noise=torch.stack([raw._noise_horizon(samples,1.,raw._seed(args.seed^PHASE_SALT^0x524157,split,index,"null"),device) for index in ids])
                        noise=torch.empty((len(ids),len(blocks),candidates),dtype=torch.complex128,device=device)
                        for block,(start,end) in enumerate(blocks):
                            templates=data["basis"][:,start:end,0].to(device=device,dtype=torch.complex128)
                            noise[:,block,:]=(raw_noise[:,start:end].to(torch.complex128)@templates.conj().transpose(0,1))/math.sqrt(variance)
                        del raw_noise
                    for kind,observations in ((0,noise+true_mean),(1,noise)):
                        sums=observations.sum(1);totals=energies.sum(0)
                        scores={"constant_phase_glrt":sums.abs().square()/totals.clamp_min(torch.finfo(torch.float64).tiny),
                            "independent_block_energy":torch.where(energies[None,:,:]>0,observations.abs().square()/energies[None,:,:].clamp_min(torch.finfo(torch.float64).tiny),0).sum(1)}
                        previous=None
                        for states in args.phases:
                            score=_forward(observations,energies,states,lengths,data["sample_rate"],args.phase_diffusion,kernels[states])
                            if previous is not None:
                                maximum_grid_difference=max(maximum_grid_difference,float((score-previous).abs().max()))
                                grid_decision_differences+=int(((score.max(1).values>=log_threshold)!=(previous.max(1).values>=log_threshold)).sum())
                            previous=score;scores[f"shared_phase_{states}"]=score
                        for method,score in scores.items():raw._record_best(best[method],kind,first,len(ids),_best(score,thresholds[method]),0)
                    if time.perf_counter()>=next_progress:
                        print(f"triad-phase-study: {mode} {split} C/N0={cn0} trials={first+len(ids)}/{total_trials}",file=sys.stderr,flush=True)
                        next_progress=time.perf_counter()+30
                if device.type=="cuda":torch.cuda.synchronize(device)
                elapsed=time.perf_counter()-started
                if split=="calibration":
                    for method in methods:calibration[method]=_empirical_threshold(best[method],args.empirical_false_budget)
                for method in methods:
                    metrics=raw._metrics(best[method],data["signal_bit"],data["bitlabels"],data["true_candidate_index"],0,epoch_offsets)
                    empirical_metrics=None;empirical_cutoff=None;calibration_info={"status":"disabled"}
                    if method in calibration:
                        empirical_cutoff,calibration_info=calibration[method]
                        if split=="holdout" and empirical_cutoff is not None:
                            adjusted=best[method].clone();adjusted[0]-=empirical_cutoff
                            empirical_metrics=raw._metrics(adjusted,data["signal_bit"],data["bitlabels"],data["true_candidate_index"],0,epoch_offsets)
                    yield {"schema":SCHEMA,"source_hash":data["source_hash"],"script_sha256":script_hash,
                        "algorithm":method,"noise_mode":mode,"split":split,"trials":total_trials,"seed":args.seed,
                        "torch_version":torch.__version__,"inference_dtype":"FP64 states/complex128 correlations",
                        "raw_awgn_dtype":"complex64" if mode=="raw" else None,
                        "sufficient_awgn_dtype":"complex128" if mode=="sufficient" else None,
                        "device":str(device),"device_name":torch.cuda.get_device_name(device) if device.type=="cuda" else "CPU",
                        "sample_rate":data["sample_rate"],"sample_count":samples,"duration_seconds":samples/data["sample_rate"],
                        "block_samples":block_samples,"block_count":len(blocks),"candidate_count":candidates,
                        "cn0_db_hz":cn0,"complex_noise_variance":variance,"known_signal_amplitude":1.,
                        "amplitude_scope":"phase likelihood uses known unit gain; no unknown-gain or unknown-multipath phase-inference claim",
                        "method_nuisance_model":("normalized discrete shared-phase prior, known unit gain" if method.startswith("shared_phase_") else
                            "one unknown complex gain over the full window" if method=="constant_phase_glrt" else "independent unknown complex gains in each block"),
                        "comparison_information_asymmetry":"phase model has a unit-gain prior; GLRT comparison permits free gains",
                        "phase_diffusion_rad_per_sqrt_second":args.phase_diffusion,
                        "phase_grid_states":int(method.rsplit("_",1)[1]) if method.startswith("shared_phase_") else None,
                        "phase_transition_model":"finite circle-Laplacian continuous-time Markov chain, positive normalized transition",
                        "phase_transition_diagnostics":kernel_info,"maximum_adjacent_phase_grid_loglr_difference":maximum_grid_difference,
                        "adjacent_phase_grid_analytic_admission_differences":grid_decision_differences,
                        "analytic_threshold_scope":"phase:normalized predictive LR E0=1 plus finite-family Markov/union bound; GLRT:fixed white-noise Gamma",
                        "family_hypotheses":family,"empirically_exercised_family_hypotheses":candidates,
                        "false_alarm_budget":args.false_alarm_budget,"analytic_threshold_min":float(thresholds[method].min()),
                        "analytic_threshold_max":float(thresholds[method].max()),"selected":metrics,
                        "empirical_calibration":calibration_info,"empirical_excess_threshold":empirical_cutoff,
                        "empirical_holdout":empirical_metrics,
                        "shared_noise_across_methods_and_phase_grids":True,"positive_null_share_noise":True,
                        "common_noise_across_cn0_points":True,"raw_and_sufficient_seed_streams_are_not_paired":True,
                        "fixed_noiseless_channel_realizations":1,"model_mismatch_explicitly_allowed":mismatch,
                        "allow_model_mismatch_requested":args.allow_model_mismatch,"model_mismatch_reasons":mismatch_reasons,
                        "comparison_policy":"separate statistics on shared trials, individually budgeted; no OR or adaptive choice among methods/state counts",
                        "signal_phase_diffusion_metadata":manifest.get("relative_phase_diffusion_rad_sqrt_s"),
                        "signal_frequency_noise_metadata":{key:manifest.get(key) for key in
                            ("relative_white_fm_hz_sqrt_s","relative_random_walk_fm_hz_sqrt_s","relative_flicker_fm_rms_hz","relative_white_pm_rms_rad")},
                        "phase_prior_scope":"public scalar Wiener diffusion approximation; colored frequency noise is not silently fitted",
                        "statistic_observes_hidden_phase":False,
                        "experiment_scope":"conditional on one exported actual signal; sufficient mode analytically marginalizes independent raw blocks; raw mode generates all window noise samples",
                        "independent_block_rank_min":min(ranks),"independent_block_rank_max":max(ranks),
                        "wall_seconds_for_all_compared_methods":elapsed,"workspace_bound_bytes":peak(),"memory_cap_bytes":memory,
                        **model_info}


def main():
    args=parser().parse_args();stream=None
    try:
        if args.output is not None:args.output.parent.mkdir(parents=True,exist_ok=True);stream=args.output.open("w",encoding="utf-8")
        else:stream=sys.stdout
        for row in run(args):stream.write(json.dumps(row,sort_keys=True,separators=(",",":"),allow_nan=False)+"\n");stream.flush()
        return 0
    except (ValueError,OSError,RuntimeError) as error:print(f"triad-phase-study: {error}",file=sys.stderr);return 2
    finally:
        if stream is not None and stream is not sys.stdout:stream.close()


if __name__=="__main__":raise SystemExit(main())
