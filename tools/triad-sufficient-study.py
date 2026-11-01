#!/usr/bin/env python3
"""Fixed-bank Gaussian projection study for DISJOINT scheduled windows.

This analytically marginalizes raw white-noise samples. It is a distributionally
matched sufficient-statistic experiment, not an independently sampled waveform
receiver run. Within-bank dependence is retained through the full complex Gram.
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


def _load_raw_tool():
    path=pathlib.Path(__file__).with_name("triad-gpu-study.py")
    spec=importlib.util.spec_from_file_location("triad_raw_gpu_reference",path)
    module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


raw=_load_raw_tool()
SCHEMA="triad-gaussian-sufficient-v1"
SUFFICIENT_SALT=0x5355464649434945


def parser():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("manifest",type=pathlib.Path)
    p.add_argument("--device",default="cpu")
    p.add_argument("--trials",type=lambda x:raw._integer(x,"trials",1,10_000_000),default=128)
    p.add_argument("--batch",type=raw._positive_int,default=8)
    p.add_argument("--attempt-tile",type=raw._positive_int,default=64)
    p.add_argument("--attempts",type=lambda x:raw._integer(x,"attempts",1,1_000_000),default=1440)
    p.add_argument("--stride-samples",type=raw._positive_int)
    p.add_argument("--signal-attempt",type=lambda x:raw._integer(x,"signal-attempt",0,1_000_000),default=0)
    p.add_argument("--seed",type=lambda x:raw._integer(x,"seed",0,(1<<64)-1),default=1)
    p.add_argument("--cn0-grid",type=raw._parse_cn0_grid)
    p.add_argument("--threshold",choices=("exact","conservative"),default="exact")
    p.add_argument("--false-alarm-budget",type=raw._finite_float,default=1e-4)
    p.add_argument("--family-hypotheses",type=raw._positive_int)
    p.add_argument("--duration-alternatives",type=raw._positive_int,default=1)
    p.add_argument("--rank-relative-tolerance",type=raw._finite_float,default=1e-6)
    p.add_argument("--precision",choices=("fp32","fp64"),default="fp32")
    p.add_argument("--memory-gib",type=raw._memory_gib,default=4.)
    p.add_argument("--shard-index",type=lambda x:raw._integer(x,"shard-index",0,(1<<31)-1),default=0)
    p.add_argument("--shard-count",type=raw._positive_int,default=1)
    p.add_argument("--output",type=pathlib.Path)
    return p


def _synchronize(device):
    if device.type=="cuda":torch.cuda.synchronize(device)


def _covariance_model(data,tolerance,memory_bytes):
    candidates,samples,columns=data["basis_shape"]
    dimension=candidates*columns
    if dimension>4096:
        raise ValueError("this dense diagnostic supports at most4096 projected dimensions; no bank truncation is performed")
    # Bound simultaneous MGS input/output/temporaries and Hermitian factors.
    preparation=128*dimension*samples+128*dimension*dimension+64*samples+4*1024**2
    if preparation>memory_bytes:
        raise ValueError("memory cap cannot fit fixed-bank Gram preparation")
    begin=time.perf_counter()
    if not bool(torch.isfinite(data["signal"]).all()):raise ValueError("signal contains nonfinite samples")
    basis=data.pop("basis").to(torch.complex128)
    q,ranks=raw._modified_gram_schmidt(basis,tolerance)
    qrows=q.permute(0,2,1).contiguous().reshape(dimension,samples)
    signal=data["signal"].to(torch.complex128)
    signal_energy=float(signal.abs().square().sum())
    if not math.isfinite(signal_energy) or signal_energy<=0:raise ValueError("signal must have finite positive energy")
    means=qrows.conj()@signal
    gram=qrows.conj()@qrows.transpose(0,1)
    gram=(gram+gram.conj().transpose(0,1))*.5
    eigenvalues,eigenvectors=torch.linalg.eigh(gram)
    maximum=float(eigenvalues.max())
    minimum=float(eigenvalues.min())
    if not math.isfinite(maximum) or maximum<=0:
        raise ValueError("candidate bank has no usable fixed subspace")
    if minimum < -1e-10*max(1.,maximum):
        raise ValueError("fixed-bank covariance has a substantive negative eigenvalue")
    positive=eigenvalues>0
    factor=eigenvectors[:,positive]*eigenvalues[positive].sqrt()[None,:]
    reconstruction=factor@factor.conj().transpose(0,1)
    norm=float(torch.linalg.vector_norm(gram))
    error=float(torch.linalg.vector_norm(reconstruction-gram))/max(norm,1e-300)
    if not math.isfinite(error) or error>1e-9:
        raise ValueError("covariance eigendecomposition failed reconstruction")
    truth=data["true_candidate_index"]
    bit=data["signal_bit"]
    wrong_fraction=eta=None
    orthogonality=0.
    for index in range(candidates):
        block=gram[index*columns:(index+1)*columns,index*columns:(index+1)*columns]
        expected=torch.diag((block.diagonal().real>.5).to(torch.complex128))
        orthogonality=max(orthogonality,float((block-expected).abs().max()))
    if orthogonality>1e-8:raise ValueError("FP64 candidate subspaces are not orthonormal")
    energies=means.abs().square().reshape(candidates,columns).sum(1)
    if truth is not None:
        true_rows=slice(truth*columns,(truth+1)*columns)
        eta=0.
        for index,label in enumerate(data["bitlabels"]):
            if label==data["bitlabels"][truth]:continue
            cross=gram[true_rows,index*columns:(index+1)*columns]
            eta=max(eta,min(1.,float(torch.linalg.svdvals(cross).max().square())))
    if bit is not None:
        indices=[i for i,label in enumerate(data["bitlabels"]) if label!=bit]
        wrong_fraction=float(energies[indices].max())/signal_energy if indices else None
    block=gram[:columns,:columns]
    info={"preparation_seconds":time.perf_counter()-begin,"preparation_workspace_bound_bytes":preparation,
          "projected_dimension":dimension,"covariance_factor_dimension":factor.shape[1],
          "minimum_covariance_eigenvalue":minimum,"clipped_negative_eigenvalues":int((eigenvalues<0).sum()),
          "fp64_covariance_relative_reconstruction_error":error,
          "rank_min":int(ranks.min()),"rank_max":int(ranks.max()),
          "maximum_fp64_projector_orthogonality_error":orthogonality,
          "signal_energy_samples":signal_energy,
          "true_candidate_projected_signal_fraction":float(energies[truth])/signal_energy if truth is not None else None,
          "strongest_wrong_bit_signal_projection_fraction":wrong_fraction,
          "canonical_wrong_subspace_projection_upper":eta,
          "canonical_bound_scope":"uniform within the fixed true subspace only; channel mismatch/outages require separate bounds",
          "first_candidate_null_energy_mean_theoretical":float(block.diagonal().real.sum()),
          "first_candidate_null_energy_variance_theoretical":float(block.abs().square().sum())}
    del basis,q,qrows,signal,eigenvectors,eigenvalues,reconstruction
    return factor,means,gram,info


def _new_device_best(batch,device,dtype):
    return (torch.full((batch,),-torch.inf,dtype=dtype,device=device),
            torch.zeros(batch,dtype=dtype,device=device),
            torch.full((batch,),-1,dtype=torch.int64,device=device),
            torch.full((batch,),-1,dtype=torch.int64,device=device))


def _update_best(best,energies,offset,threshold):
    batch,attempts,candidates=energies.shape
    value,flat=energies.reshape(batch,attempts*candidates).max(1)
    statistic=value-threshold
    update=statistic>best[0]
    candidate=flat%candidates
    attempt=flat//candidates+offset
    return tuple(torch.where(update,new,old) for old,new in zip(best,(statistic,value,candidate,attempt)))


def run(args):
    directory,manifest=raw._load_manifest(args.manifest)
    data=raw._validate_manifest(directory,manifest)
    device=raw._resolve_device(args.device)
    raw._configure_torch()
    if not 0<args.false_alarm_budget<1 or not 0<args.rank_relative_tolerance<.1:
        raise ValueError("invalid false-admission budget or rank tolerance")
    if args.shard_index>=args.shard_count or args.signal_attempt>=args.attempts:
        raise ValueError("invalid shard or signal attempt")
    candidates,samples,columns=data["basis_shape"]
    stride=args.stride_samples if args.stride_samples is not None else samples
    if stride<samples:
        raise ValueError("sufficient-statistic backend requires disjoint windows; overlapping attempts need their cross-time covariance")
    if data["signal_start_samples"] not in (0,args.signal_attempt*stride):
        raise ValueError("manifest signal start does not match the declared single signal attempt")
    trials=range(args.shard_index,args.trials,args.shard_count)
    if not len(trials):raise ValueError("this shard has no trials")
    family=candidates*args.attempts*args.duration_alternatives
    if args.family_hypotheses is not None:
        if args.family_hypotheses<family:raise ValueError("family budget omits exercised candidates/attempts/durations")
        family=args.family_hypotheses
    threshold,threshold_method,threshold_fallback=raw._gamma_threshold(columns,math.log(family)-math.log(args.false_alarm_budget),args.threshold)
    memory_bytes=int(args.memory_gib*1024**3)
    factor_cpu,physical_mean_cpu,gram_cpu,model_info=_covariance_model(data,args.rank_relative_tolerance,memory_bytes)
    dtype=torch.complex64 if args.precision=="fp32" else torch.complex128
    real_dtype=torch.float32 if args.precision=="fp32" else torch.float64
    factor=factor_cpu.to(device=device,dtype=dtype)
    gram_device=gram_cpu.to(device=device,dtype=torch.complex128)
    factor_check=factor.to(torch.complex128)
    device_error=float(torch.linalg.vector_norm(factor_check@factor_check.conj().transpose(0,1)-gram_device))/max(float(torch.linalg.vector_norm(gram_device)),1e-300)
    if not math.isfinite(device_error) or device_error>1e-5:
        raise ValueError("selected-precision covariance factor failed reconstruction")
    dimension,latent=factor.shape
    persistent=64*len(trials)+80*dimension*dimension+32*samples+4*1024**2
    attempt_tile=min(args.attempt_tile,args.attempts)
    batch=min(args.batch,len(trials))
    def working():return persistent+96*batch*attempt_tile*max(dimension,latent)+32*batch*attempt_tile*candidates
    while working()>memory_bytes and (batch>1 or attempt_tile>1):
        if batch>1:batch=max(1,batch//2)
        else:attempt_tile=max(1,attempt_tile//2)
    if working()>memory_bytes:raise ValueError("memory cap cannot fit one trial/attempt sufficient-statistic tile")
    workspace=max(working(),model_info["preparation_workspace_bound_bytes"])
    del gram_device,factor_check,gram_cpu,factor_cpu
    factor_transpose=factor.transpose(0,1).contiguous()
    cn0_cases=[]
    if args.cn0_grid is None and "noise_variance" in manifest:
        cn0_cases.append((manifest.get("cn0_db_hz"),float(manifest["noise_variance"])))
    else:
        for cn0 in args.cn0_grid if args.cn0_grid is not None else [float(manifest["cn0_db_hz"])]:
            if not -300<cn0/10<300:raise ValueError("C/N0 is outside representable variance range")
            cn0_cases.append((cn0,data["noise_reference_power"]*data["sample_rate"]/10**(cn0/10)))
    manifest_path=args.manifest/"manifest.json" if args.manifest.is_dir() else args.manifest
    manifest_hash=hashlib.sha256(manifest_path.read_bytes()).hexdigest()
    script_hash=hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest()
    raw_script_hash=hashlib.sha256(pathlib.Path(raw.__file__).read_bytes()).hexdigest()
    epoch_offsets=[p.get("epoch_offset_seconds",0) for p in data["candidateparams"]]
    for cn0,variance in cn0_cases:
        if not math.isfinite(variance) or variance<=0:raise ValueError("invalid complex sample variance")
        mean=(physical_mean_cpu/math.sqrt(variance)).to(device=device,dtype=dtype)
        if not bool(torch.isfinite(mean).all()):raise ValueError("whitened projection mean is not representable")
        for split in ("calibration","holdout"):
            best=raw._new_best(len(trials))
            moments=torch.zeros(2,dtype=torch.float64,device=device)
            moment_count=0
            begin=time.perf_counter()
            if device.type=="cuda":torch.cuda.reset_peak_memory_stats(device)
            for first in range(0,len(trials),batch):
                ids=trials[first:first+batch]
                generators=[]
                for index in ids:
                    generator=torch.Generator(device=device)
                    generator.manual_seed(raw._seed(args.seed^SUFFICIENT_SALT,split,index,"null")&((1<<63)-1))
                    generators.append(generator)
                null_best=_new_device_best(len(ids),device,real_dtype)
                positive_best=_new_device_best(len(ids),device,real_dtype)
                for attempt_first in range(0,args.attempts,attempt_tile):
                    count=min(attempt_tile,args.attempts-attempt_first)
                    white=[]
                    for generator in generators:
                        real=torch.randn((count,latent),dtype=real_dtype,device=device,generator=generator)*math.sqrt(.5)
                        imag=torch.randn((count,latent),dtype=real_dtype,device=device,generator=generator)*math.sqrt(.5)
                        white.append(torch.complex(real,imag))
                    white=torch.stack(white)
                    # z_col=L w_col, hence z_row=w_row L.T, without conjugation.
                    projected=white@factor_transpose
                    energy=projected.abs().square().reshape(len(ids),count,candidates,columns).sum(-1)
                    first_energy=energy[:,:,0].to(torch.float64)
                    moments[0]+=first_energy.sum();moments[1]+=first_energy.square().sum()
                    moment_count+=len(ids)*count
                    null_best=_update_best(null_best,energy,attempt_first,threshold)
                    if attempt_first<=args.signal_attempt<attempt_first+count:
                        projected[:,args.signal_attempt-attempt_first,:]+=mean
                        energy=projected.abs().square().reshape(len(ids),count,candidates,columns).sum(-1)
                    positive_best=_update_best(positive_best,energy,attempt_first,threshold)
                raw._record_best(best,0,first,len(ids),positive_best,0)
                raw._record_best(best,1,first,len(ids),null_best,0)
            _synchronize(device)
            elapsed=time.perf_counter()-begin
            empirical_mean=float(moments[0])/moment_count
            empirical_variance=max(0.,float(moments[1])/moment_count-empirical_mean*empirical_mean)
            metrics=raw._metrics(best,data["signal_bit"],data["bitlabels"],data["true_candidate_index"],args.signal_attempt,epoch_offsets)
            yield {"schema":SCHEMA,"method":"exact_linear_gaussian_projection_sufficient_statistics",
                "sampled_waveform_execution":False,"distribution_scope":"known white Gaussian noise, fixed exported basis, disjoint scheduled windows; numerical covariance reconstruction reported",
                "not_stream_receiver_or_independent_raw_waveform_validation":True,
                "same_fixed_exported_bank_reused_at_all_attempts":True,
                "clock_epoch_changes_between_attempts_exercised":False,
                "source_hash":data["source_hash"],"manifest_sha256":manifest_hash,"script_sha256":script_hash,"raw_helper_sha256":raw_script_hash,
                "device":str(device),"device_name":torch.cuda.get_device_name(device) if device.type=="cuda" else "CPU",
                "torch_version":torch.__version__,"precision":args.precision,"split":split,"seed":args.seed,
                "shard_index":args.shard_index,"shard_count":args.shard_count,
                "trial_indices":[trials[0],trials[-1],len(trials)],"trials":len(trials),
                "cn0_db_hz":cn0,"complex_sample_noise_variance":variance,
                "noise_reference_power":data["noise_reference_power"],"sample_rate":data["sample_rate"],
                "projection_window_samples":samples,"projection_window_seconds":samples/data["sample_rate"],
                "attempts":args.attempts,"stride_samples":stride,"attempt_spacing_seconds":stride/data["sample_rate"],
                "last_window_end_seconds":((args.attempts-1)*stride+samples)/data["sample_rate"],
                "scheduled_period_seconds":args.attempts*stride/data["sample_rate"],
                "signal_attempt":args.signal_attempt,"candidate_count":candidates,"candidate_columns":columns,
                "family_hypotheses":family,"empirically_exercised_family_hypotheses":candidates*args.attempts,
                "unexercised_family_hypotheses":family-candidates*args.attempts,
                "duration_alternatives_budgeted":args.duration_alternatives,"false_alarm_budget":args.false_alarm_budget,
                "threshold_rank_bound":columns,"threshold_rank_policy":"common column-count bound; conservative for deficient candidates",
                "threshold_normalized_energy":threshold,"threshold_method":threshold_method,"threshold_fallback_reason":threshold_fallback,
                "selected_covariance_relative_reconstruction_error":device_error,
                "first_candidate_null_energy_mean_empirical":empirical_mean,"first_candidate_null_energy_variance_empirical":empirical_variance,
                "first_candidate_null_moment_observations":moment_count,"selected":metrics,
                "positive_null_share_horizon_noise":True,"common_noise_across_cn0_points":True,
                "rng_sequence_depends_on_attempt_tiling":True,"raw_seed_streams_are_not_paired":True,
                "wall_seconds":elapsed,"horizons_per_second":len(trials)/elapsed,
                "trial_batch":batch,"attempt_tile":attempt_tile,"workspace_bound_bytes":workspace,"memory_cap_bytes":memory_bytes,
                "cuda_peak_allocated_bytes":torch.cuda.max_memory_allocated(device) if device.type=="cuda" else None,
                **model_info}


def main():
    args=parser().parse_args()
    stream=None
    try:
        if args.output is not None:
            args.output.parent.mkdir(parents=True,exist_ok=True);stream=args.output.open("w",encoding="utf-8")
        else:stream=sys.stdout
        for row in run(args):
            stream.write(json.dumps(row,sort_keys=True,separators=(",",":"),allow_nan=False)+"\n");stream.flush()
        return 0
    except (ValueError,OSError,RuntimeError) as error:
        print(f"triad-sufficient-study: {error}",file=sys.stderr);return 2
    finally:
        if stream is not None and stream is not sys.stdout:stream.close()


if __name__=="__main__":raise SystemExit(main())
