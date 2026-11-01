#!/usr/bin/env python3
"""Paired fixed-band three-carrier receiver diagnostics, not field qualification.

All detector statistics consume the same raw complex AWGN realization. Fixed
DFT masks are circular finite-window projectors, not a streaming channelizer.
The paired GLRTs use the same retained Fourier bins, where white sample noise
remains white. Product and block methods use independently calibrated full-bank
maxima; their dependent pairs/blocks are never assigned independent chi-squares.
"""
from __future__ import annotations
import argparse
import importlib.util
import json
import math
import pathlib
import sys
import time
from typing import Any
import torch

_spec = importlib.util.spec_from_file_location("triad_gpu_shared", pathlib.Path(__file__).with_name("triad-gpu-study.py"))
assert _spec and _spec.loader
shared = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(shared)


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("manifest", type=pathlib.Path)
    p.add_argument("--opposite-manifest", type=pathlib.Path,
                   help="opposite source bit independently sampled with the identical channel recipe/seed/calendar")
    p.add_argument("--device", default="cpu")
    p.add_argument("--trials", type=shared._positive_int, default=2048)
    p.add_argument("--calibration-trials", type=shared._positive_int, default=4096)
    p.add_argument("--batch", type=shared._positive_int, default=32)
    p.add_argument("--seed", type=lambda x: shared._integer(x,"seed",0,shared.MASK64), default=1)
    p.add_argument("--cn0-grid", type=shared._parse_cn0_grid, default=[0.,4.,8.,12.])
    p.add_argument("--block-seconds", type=shared._finite_float, default=.4)
    p.add_argument("--false-admission", type=shared._finite_float, default=.01)
    p.add_argument("--spacing-hz", type=shared._finite_float)
    p.add_argument("--padding-factor", type=lambda x: shared._integer(x,"padding-factor",1,4), default=1)
    p.add_argument("--memory-gib", type=shared._memory_gib, default=4.)
    p.add_argument("--output", type=pathlib.Path)
    return p


def wilson(events: int, n: int) -> list[float] | None:
    if n == 0: return None
    z=1.959963984540054
    q=events/n; d=1+z*z/n
    center=(q+z*z/(2*n))/d
    radius=z*math.sqrt(q*(1-q)/n+z*z/(4*n*n))/d
    return [max(0.,center-radius),min(1.,center+radius)]


def summarize(scores: torch.Tensor, winners: torch.Tensor, threshold: float,
              labels: list[int], truth: int | None, kind: str) -> dict[str,Any]:
    accepted=scores>threshold
    n=scores.numel(); admitted=int(accepted.sum())
    out={"trials":n,"admitted":admitted,"admission_wilson95":wilson(admitted,n)}
    if kind=="null":
        out.update(null_admissions=admitted,
                   zero_event_one_sided95_upper=-math.expm1(math.log(.05)/n) if admitted==0 and n else None)
    elif truth is not None:
        correct=torch.tensor(labels,dtype=torch.int64)[winners]==truth
        ca=int((accepted&correct).sum());wa=int((accepted&~correct).sum())
        out.update(correct_accepted=ca,wrong_accepted=wa,missed=n-admitted,
                   raw_bit_errors=int((~correct).sum()),
                   correct_accepted_wilson95=wilson(ca,n),wrong_accepted_wilson95=wilson(wa,n))
    else:
        out.update(correct_accepted=None,wrong_accepted=None,missed=n-admitted,raw_bit_errors=None)
    return out


def empirical_threshold(maxima: torch.Tensor, alpha: float) -> tuple[float,int]:
    # Split-conformal order statistic, with strict > admission to handle ties.
    # Exchangeability gives marginal coverage over training+new draw; it does
    # not certify a conditional 1% tail for this particular trained threshold.
    n=maxima.numel(); order=math.ceil((n+1)*(1-alpha))
    if order>n: raise ValueError("insufficient calibration trials for requested order-statistic tail")
    return float(maxima.kthvalue(order).values),order


def _pad(values: torch.Tensor, total: int, offset: int) -> torch.Tensor:
    if values.shape[-1]==total: return values
    result=torch.zeros((*values.shape[:-1],total),dtype=values.dtype,device=values.device)
    result[...,offset:offset+values.shape[-1]]=values
    return result


def validate_opposite_manifest(original:dict[str,Any],opposite:dict[str,Any])->None:
    """Refuse to call different channel recipes a known-channel codeword pair."""
    required=("source_format","channel_model","sample_rate","signal_shape","signal_bit",
              "seed","epoch","channel","control","command_argv","truth_arrival_seconds",
              "truth_frequency_hz","truth_clock_ppm","truth_frequency_drift_hz_per_second")
    for key in required:
        if key not in original or key not in opposite:raise ValueError(f"opposite-bit benchmark requires {key} in both manifests")
    if original["signal_bit"] not in (0,1) or opposite["signal_bit"]!=1-original["signal_bit"]:
        raise ValueError("opposite manifest must describe the other source bit")
    if original["control"]!="signal" or opposite["control"]!="signal":
        raise ValueError("known-channel codewords must both be positive signal controls")
    varying={"signal_bit","true_candidate_index","true_candidate_note","command_argv",
             "basis_file","signal_file","carrier_basis_file",
             "channel_signal_energy_pre_erasure_samples","channel_signal_energy_samples",
             "channel_observed_energy_samples"}
    first={k:v for k,v in original.items() if k not in varying}
    second={k:v for k,v in opposite.items() if k not in varying}
    if first!=second:
        different=sorted(k for k in first.keys()|second.keys() if first.get(k)!=second.get(k))
        raise ValueError("opposite-bit manifest configuration differs: "+", ".join(different))
    def normalized_argv(argv:Any)->list[str]:
        if not isinstance(argv,list) or not argv or not all(isinstance(x,str) for x in argv):
            raise ValueError("opposite-bit benchmark requires complete command_argv provenance")
        result=[];i=1
        while i<len(argv):
            if argv[i] in ("--output","--signal-bit"):
                if i+1==len(argv):raise ValueError("truncated export command provenance")
                i+=2
            else:result.append(argv[i]);i+=1
        return result
    if normalized_argv(original["command_argv"])!=normalized_argv(opposite["command_argv"]):
        raise ValueError("opposite-bit export commands may differ only in executable path, --output and --signal-bit")


def verified_projector(basis:torch.Tensor)->tuple[torch.Tensor,torch.Tensor,dict[str,Any]]:
    """Verify retained-basis FP32 MGS against CPU FP64, one candidate at a time.

    Small Gram/overlap matrices compare QQ^H without allocating sample-square
    matrices. The FP64 reference starts from the SAME retained FP32 Fourier
    coefficients: this checks the channel solve, not end-to-end FFT precision.
    """
    q,ranks=shared._modified_gram_schmidt(basis)
    tolerance=3.2e-5
    stats={"reference":"CPU FP64 MGS of the same retained FP32 Fourier coefficients",
           "rank_relative_tolerance":1e-6,"projector_distance_tolerance":tolerance,
           "rank_fallback_candidates":0,"same_rank_fallback_candidates":0,
           "maximum_projector_distance_before_fallback":0.,
           "maximum_projector_distance_after_fallback":0.,
           "maximum_orthogonality_error":0.}
    for c in range(basis.shape[0]):
        reference,reference_rank=shared._modified_gram_schmidt(basis[c:c+1].to(device="cpu",dtype=torch.complex128))
        selected=q[c:c+1].to(device="cpu",dtype=torch.complex128)
        distance=float(shared._subspace_distances(reference,selected)[0])
        rank_mismatch=int(ranks[c])!=int(reference_rank[0])
        stats["maximum_projector_distance_before_fallback"]=max(stats["maximum_projector_distance_before_fallback"],distance)
        if rank_mismatch or distance>tolerance:
            stats["rank_fallback_candidates" if rank_mismatch else "same_rank_fallback_candidates"]+=1
            q[c:c+1]=reference.to(device=q.device,dtype=torch.complex64)
            ranks[c]=int(reference_rank[0])
            selected=q[c:c+1].to(device="cpu",dtype=torch.complex128)
        final_distance=float(shared._subspace_distances(reference,selected)[0])
        stats["maximum_projector_distance_after_fallback"]=max(stats["maximum_projector_distance_after_fallback"],final_distance)
        if final_distance>tolerance:
            raise ValueError("FP32 projector cannot retain the CPU FP64 subspace within tolerance")
        gram=selected.conj().transpose(1,2)@selected
        diagonal=torch.diag_embed((selected.abs().square().sum(dim=1)>.5).to(torch.complex128))
        orthogonality=float((gram-diagonal).abs().amax())
        stats["maximum_orthogonality_error"]=max(stats["maximum_orthogonality_error"],orthogonality)
        if orthogonality>1e-4:raise ValueError("FP32 projector is numerically non-orthogonal")
    return q,ranks,stats


class Workspace:
    def __init__(self,data:dict[str,Any],manifest:dict[str,Any],args:argparse.Namespace):
        self.device=shared._resolve_device(args.device);self.labels=data["bitlabels"]
        if set(self.labels)!={0,1}:raise ValueError("paired bit comparison requires both source-bit alternatives")
        if manifest.get("control") not in (None,"signal"):
            raise ValueError("export a positive signal control; this study generates its own independent null captures")
        self.truth=data["signal_bit"];self.sample_rate=data["sample_rate"]
        self.candidates,self.original_samples,self.columns=data["basis_shape"]
        self.samples=self.original_samples*args.padding_factor
        self.offset=(self.samples-self.original_samples)//2
        self.block=max(1,round(args.block_seconds*self.sample_rate))
        self.blocks=(self.samples+self.block-1)//self.block
        self.block_extent=self.blocks*self.block
        self.spacing=args.spacing_hz if args.spacing_hz is not None else manifest.get("carrier_spacing_hz")
        if self.spacing is None and "fine_rate_hz" in manifest:self.spacing=1.25*manifest["fine_rate_hz"]
        if self.spacing is None or not math.isfinite(self.spacing) or self.spacing<=0:
            raise ValueError("manifest carrier_spacing_hz or --spacing-hz is required")
        if 1.5*self.spacing>=self.sample_rate/2:
            raise ValueError("three disjoint carrier bands do not fit the sample bandwidth")
        # Conservatively includes FFT work, all immutable candidate columns,
        # two receiver banks, pair templates and a batched observation. This
        # study rejects oversized banks rather than silently pruning them.
        self.workspace_fixed_bytes=self.candidates*self.samples*(128+64*self.columns)+self.candidates*self.block_extent*80
        self.bytes_per_trial=self.samples*192+self.candidates*self.blocks*96
        capacity=int(args.memory_gib*1024**3)
        self.bookkeeping_bytes=512*args.trials+256*args.calibration_trials
        available=capacity-self.workspace_fixed_bytes-self.bookkeeping_bytes
        self.batch=min(args.batch,available//max(1,self.bytes_per_trial))
        if self.batch<1:raise ValueError("candidate bank exceeds the declared detector workspace; reduce the bank or increase --memory-gib")
        self.workspace_estimate_bytes=self.workspace_fixed_bytes+self.batch*self.bytes_per_trial+self.bookkeeping_bytes
        freq=torch.fft.fftfreq(self.samples,d=1/self.sample_rate,device=self.device)
        masks=[]
        for c in (-1,0,1):masks.append((freq>=c*self.spacing-self.spacing/2)&(freq<c*self.spacing+self.spacing/2))
        self.masks=torch.stack(masks)
        if bool((self.masks.sum(dim=0)>1).any()):raise ValueError("carrier masks overlap")
        self.retained=self.masks.any(dim=0)
        if int(self.retained.sum())<self.columns:raise ValueError("too few retained Fourier bins for channel basis")
        raw=data["basis"].to(device=self.device,dtype=torch.complex64).permute(0,2,1)
        raw=_pad(raw,self.samples,self.offset)
        if not bool(torch.isfinite(raw).all()):raise ValueError("basis contains nonfinite values")
        transformed=torch.fft.fft(raw,dim=-1,norm="ortho")
        retained_basis=transformed[...,self.retained].permute(0,2,1)
        q_coupled,ranks,coupled_check=verified_projector(retained_basis)
        q_flat,flat_ranks,flat_check=verified_projector(retained_basis[:,:,:1])
        self.projector_checks={"flat_masked_glrt":flat_check,"coupled_masked_glrt":coupled_check}
        self.q_coupled=q_coupled.conj().permute(1,0,2).reshape(int(self.retained.sum()),-1).contiguous()
        self.q_flat=q_flat.conj().permute(1,0,2).reshape(int(self.retained.sum()),-1).contiguous()
        self.rank_min=int(ranks.min());self.rank_max=int(ranks.max())
        self.flat_rank_min=int(flat_ranks.min())
        # All pair/block comparators use the same DIRECT-tap template. Their
        # shared-gain assumption is explicitly K1-mismatched on two-ray data.
        band=torch.fft.ifft(transformed[:,0,None,:]*self.masks[None,:,:],dim=-1,norm="ortho")
        band=_pad(band,self.block_extent,0).reshape(self.candidates,3,self.blocks,self.block)
        self.band_weights=band.conj().contiguous()
        self.band_energy=band.abs().square().sum(dim=-1)
        # Unknown carrier/block noise offset is removed by a zero-mean power
        # template. Only actual observations enter means; the last block's
        # deterministic storage padding must not become an amplitude feature.
        valid_samples=(torch.arange(self.block_extent,device=self.device)<self.samples).reshape(self.blocks,self.block)
        block_counts=valid_samples.sum(dim=-1).clamp_min(1)
        power=band.abs().square()
        centered_power=power-power.sum(dim=-1,keepdim=True)/block_counts[None,None,:,None]*valid_samples[None,None,:,:]
        amplitude_energy=centered_power.square().sum(dim=-1)
        amplitude_floor=amplitude_energy.sum(dim=(1,2))[:,None,None]*1e-10
        valid_amplitude=amplitude_energy>amplitude_floor
        self.amplitude_weights=centered_power/amplitude_energy.sqrt().clamp_min(torch.finfo(torch.float32).tiny)[...,None]
        self.amplitude_weights*=valid_amplitude[...,None]
        energy_floor=self.band_energy.sum(dim=(1,2))[:,None,None]*1e-10
        self.valid_carrier=self.band_energy>energy_floor
        self.band_denominator=self.band_energy.clamp_min(torch.finfo(torch.float32).tiny)
        joint_energy=self.band_energy.sum(dim=1)
        self.valid_joint=joint_energy>energy_floor[:,0,:]
        self.joint_denominator=joint_energy.clamp_min(torch.finfo(torch.float32).tiny)
        self.pair_indices=((0,1),(0,2),(1,2))
        pair=torch.stack([band[:,a]*band[:,b].conj() for a,b in self.pair_indices],dim=1)
        pair_energy=pair.abs().square().sum(dim=-1)
        pair_floor=pair_energy.sum(dim=(1,2))[:,None,None]*1e-10
        self.valid_pair=pair_energy>pair_floor
        self.pair_weights=(pair.conj()/pair_energy.sqrt().clamp_min(torch.finfo(torch.float32).tiny)[...,None])
        self.pair_weights*=self.valid_pair[...,None]
        signal=data["signal"].to(device=self.device,dtype=torch.complex64)
        self.signal=_pad(signal,self.samples,self.offset)
        signal_fft=torch.fft.fft(self.signal,norm="ortho")
        self.signal_energy=float(self.signal.abs().to(torch.float64).square().sum())
        if not math.isfinite(self.signal_energy) or self.signal_energy<=0:
            raise ValueError("positive exported signal must have finite nonzero energy")
        self.retained_signal_energy=float(signal_fft[self.retained].abs().to(torch.float64).square().sum())
        signal_spectrum=signal_fft.abs().to(torch.float64).square()
        retained_spectrum=signal_spectrum[self.retained]
        self.spectral_metrics={
            "scope":"finite-window noiseless sampled channel output; not transmitter emission or interception qualification",
            "outside_retained_band_energy_fraction":max(0.,1-self.retained_signal_energy/self.signal_energy),
            "carrier_band_energy_samples":[float(signal_spectrum[mask].sum()) for mask in self.masks],
            "carrier_band_widths_hz":[int(mask.sum())*self.sample_rate/self.samples for mask in self.masks],
            "retained_periodogram_spectral_flatness":float(retained_spectrum.log().mean().exp()/retained_spectrum.mean()) if float(retained_spectrum.mean())>0 else 0.,
            "spectral_flatness_definition":"geometric/arithmetic mean of single-capture retained Fourier-bin powers; zero bins give zero flatness"}
        basis_energy=transformed.abs().to(torch.float64).square().sum(dim=-1)
        retained_energy=retained_basis.abs().to(torch.float64).square().sum(dim=1)
        ratios=(retained_energy/basis_energy.clamp_min(torch.finfo(torch.float64).tiny))
        self.basis_retained_fraction_min=float(ratios.min())
        self.reference_distance=self._known_flat_distance(raw[:,0],manifest)
        self.oracle_directions=None
        self.oracle_distances=None
        if args.opposite_manifest:
            opposite_directory,opposite_manifest=shared._load_manifest(args.opposite_manifest)
            validate_opposite_manifest(manifest,opposite_manifest)
            opposite_data=shared._validate_manifest(opposite_directory,opposite_manifest)
            opposite=_pad(opposite_data["signal"].to(device=self.device,dtype=torch.complex64),self.samples,self.offset)
            if not bool(torch.isfinite(opposite).all()):raise ValueError("opposite codeword contains nonfinite samples")
            difference=opposite.to(torch.complex128)-self.signal.to(torch.complex128)
            spectral=torch.fft.fft(difference,norm="ortho")
            masked=torch.fft.ifft(spectral*self.retained,norm="ortho")
            distances=torch.stack([difference.abs().square().sum(),masked.abs().square().sum()])
            if not bool(torch.isfinite(distances).all()) or bool((distances<=0).any()):
                raise ValueError("known codewords must have finite positive full-band and retained-band separation")
            self.oracle_distances=distances.sqrt().to(torch.float64)
            self.oracle_directions=(torch.stack([difference,masked],dim=1)/self.oracle_distances[None,:]).conj().to(torch.complex64)
            self.reference_distance={"available":True,"method":"independently sampled opposite-bit codeword pair",
                "assumptions":"known deterministic channel trajectory, clock, arrival, epoch and complex gain for each codeword; binary decision without admission gate",
                "squared_distance_samples":float(distances[0]),"retained_squared_distance_samples":float(distances[1]),
                "opposite_source_bit":opposite_data["signal_bit"],"opposite_manifest":str(args.opposite_manifest),
                "configuration_and_command_provenance_matched":True,
                "numerics":"FP64 distance/DFT preparation; FP32 directions and identical FP32 sampled observations for empirical decisions"}
            del opposite_data,opposite,difference,spectral,masked,distances
        self.methods=["product_first_three_pairs","block_first_shared_gain","block_first_per_carrier",
                      "amplitude_only_positive_gain","flat_masked_glrt","coupled_masked_glrt"]
        self.thresholds={}
        self.threshold_metadata={}
        for name,rank in (("flat_masked_glrt",1),("coupled_masked_glrt",self.columns)):
            value,method,reason=shared._gamma_threshold(rank,math.log(self.candidates/args.false_admission),"exact")
            self.thresholds[name]=value;self.threshold_metadata[name]={"method":method,"fallback":reason,"rank_bound":rank}
        del raw,transformed,retained_basis,q_coupled,q_flat,band,pair,pair_energy,signal_fft
        del valid_samples,block_counts,power,centered_power,amplitude_energy,valid_amplitude,signal_spectrum,retained_spectrum

    def _known_flat_distance(self,basis:torch.Tensor,manifest:dict[str,Any])->dict[str,Any]:
        if self.truth is None:return {"available":False,"reason":"source bit unspecified"}
        # A strict fit check certifies only this finite noiseless realization.
        # Arbitrary multipath/colored channel fits are not called a bound.
        if manifest.get("channel") not in (None,"none"):
            return {"available":False,"reason":"opposite-bit sampled channel realization was not exported"}
        energy=basis.abs().to(torch.float64).square().sum(dim=1)
        r=(basis.conj()*self.signal[None,:]).sum(dim=1)
        h=r/energy.clamp_min(torch.finfo(torch.float64).tiny)
        residual=((basis*h[:,None]-self.signal[None,:]).abs().to(torch.float64).square().sum(dim=1)/max(self.signal_energy,1e-300))
        allowed=torch.tensor([x==self.truth for x in self.labels],device=self.device)
        residual=torch.where(allowed,residual,torch.full_like(residual,torch.inf))
        best=int(residual.argmin())
        if float(residual[best])>1e-9:
            return {"available":False,"reason":"no exact finite shared-gain template match","best_relative_residual":float(residual[best])}
        params=manifest["candidateparams"]
        identity={k:v for k,v in params[best].items() if k!="bit"}
        opposite=next((i for i,p in enumerate(params) if self.labels[i]!=self.truth and {k:v for k,v in p.items() if k!="bit"}==identity),None)
        if opposite is None:return {"available":False,"reason":"matching opposite-bit nuisance hypothesis absent"}
        difference=(basis[best]-basis[opposite])*h[best]
        distance=float(difference.abs().to(torch.float64).square().sum())
        spectral=torch.fft.fft(difference,norm="ortho")
        retained=float(spectral[self.retained].abs().to(torch.float64).square().sum())
        return {"available":True,"assumptions":"finite fitted shared-gain realization, exact-shape residual <=1e-9; known gain/timing oracle",
                "squared_distance_samples":distance,"retained_squared_distance_samples":retained,
                "relative_fit_residual":float(residual[best]),"true_candidate":best,"opposite_candidate":opposite}

    def score(self,observations:torch.Tensor)->dict[str,torch.Tensor]:
        freq=torch.fft.fft(observations,dim=-1,norm="ortho")
        selected=freq[:,self.retained]
        flat=(selected@self.q_flat).reshape(observations.shape[0],self.candidates,1).abs().square().sum(dim=-1)
        coupled=(selected@self.q_coupled).reshape(observations.shape[0],self.candidates,self.columns).abs().square().sum(dim=-1)
        bands=torch.fft.ifft(freq[:,None,:]*self.masks[None,:,:],dim=-1,norm="ortho")
        bands=_pad(bands,self.block_extent,0).reshape(observations.shape[0],3,self.blocks,self.block)
        # Block is an actual physical-time interval; no logical deinterleave
        # ever reorders channel timestamps before either operation.
        r=torch.einsum("bcln,kcln->bkcl",bands,self.band_weights)
        shared_score=(r.sum(dim=2).abs().square()/self.joint_denominator[None,:,:]*self.valid_joint[None,:,:]).sum(dim=-1)
        independent=(r.abs().square()/self.band_denominator[None,:,:,:]*self.valid_carrier[None,:,:,:]).sum(dim=(2,3))
        pairs=torch.stack([bands[:,a]*bands[:,b].conj() for a,b in self.pair_indices],dim=1)
        pair_r=torch.einsum("bpln,kpln->bkpl",pairs,self.pair_weights)
        product=pair_r.abs().square().sum(dim=(2,3))
        # Nonnegative power gain is the physical constraint: negative template
        # correlation is evidence against that gain model, never squared into
        # positive evidence. This projection score is not an exact Rice or
        # independent chi-square likelihood; full-bank null calibration is used.
        amplitude_r=torch.einsum("bcln,kcln->bkcl",bands.abs().square(),self.amplitude_weights)
        amplitude=amplitude_r.clamp_min(0).square().sum(dim=(2,3))
        result={"product_first_three_pairs":product,"block_first_shared_gain":shared_score,
                "block_first_per_carrier":independent,"amplitude_only_positive_gain":amplitude,
                "flat_masked_glrt":flat,"coupled_masked_glrt":coupled}
        if any(not bool(torch.isfinite(value).all()) for value in result.values()):raise ValueError("nonfinite detector score")
        return result

    def oracle_errors(self,observations:torch.Tensor,signal_scale:float)->torch.Tensor|None:
        if self.oracle_directions is None:return None
        # Equivalent to comparing distances to both known means. Centering on
        # the transmitted mean avoids subtracting two large squared norms.
        noise=observations-self.signal[None,:]*signal_scale
        margin=(noise@self.oracle_directions).real.to(torch.float64)-.5*signal_scale*self.oracle_distances[None,:]
        return (margin>0).sum(dim=0).cpu()


def noise_batch(work:Workspace,ids:range,seed:int,split:str)->torch.Tensor:
    return torch.stack([shared._noise_horizon(work.samples,1.,shared._seed(seed,split,i,"null"),work.device) for i in ids])


def collect(work:Workspace,count:int,seed:int,split:str,signal_scale:float=0.)->tuple[dict[str,torch.Tensor],dict[str,torch.Tensor],float,list[int]|None]:
    maxima={name:torch.empty(count,dtype=torch.float64) for name in work.methods}
    winners={name:torch.empty(count,dtype=torch.int64) for name in work.methods}
    oracle_counts=torch.zeros(2,dtype=torch.int64) if signal_scale and work.oracle_directions is not None else None
    started=time.perf_counter();next_log=started+30
    for first in range(0,count,work.batch):
        last=min(count,first+work.batch)
        observations=noise_batch(work,range(first,last),seed,split)
        if signal_scale:observations+=work.signal[None,:]*signal_scale
        scores=work.score(observations)
        if oracle_counts is not None:oracle_counts+=work.oracle_errors(observations,signal_scale)
        for name,value in scores.items():
            best,index=value.max(dim=1);maxima[name][first:last]=best.detach().cpu();winners[name][first:last]=index.detach().cpu()
        if time.perf_counter()>next_log:
            print(f"triad-detector-study: {split} scale={signal_scale:.6g} trials={last}/{count}",file=sys.stderr,flush=True);next_log=time.perf_counter()+30
    if work.device.type=="cuda":torch.cuda.synchronize(work.device)
    return maxima,winners,time.perf_counter()-started,oracle_counts.tolist() if oracle_counts is not None else None


def run(args:argparse.Namespace):
    if not 0<args.false_admission<1 or args.block_seconds<=0:raise ValueError("invalid false-admission target or block duration")
    if math.ceil((args.calibration_trials+1)*(1-args.false_admission))>args.calibration_trials:
        raise ValueError("insufficient calibration trials for requested false-admission tail")
    directory,manifest=shared._load_manifest(args.manifest)
    data=shared._validate_manifest(directory,manifest);shared._configure_torch()
    prepare=time.perf_counter();work=Workspace(data,manifest,args)
    if work.device.type=="cuda":torch.cuda.synchronize(work.device)
    prepare=time.perf_counter()-prepare
    calibration,_,calibration_seconds,_oracle=collect(work,args.calibration_trials,args.seed,"calibration")
    for name in work.methods:
        if name not in work.thresholds:
            threshold,order=empirical_threshold(calibration[name],args.false_admission)
            work.thresholds[name]=threshold
            work.threshold_metadata[name]={"method":"independent full-bank null order statistic","order":order,
                "training_trials":args.calibration_trials,"marginal_tail_bound":(args.calibration_trials+1-order)/(args.calibration_trials+1),
                "conditional_tail_claim":False}
    del calibration,_
    null_max,null_winner,null_seconds,_oracle=collect(work,args.trials,args.seed,"holdout")
    null_metrics={name:summarize(null_max[name],null_winner[name],work.thresholds[name],work.labels,work.truth,"null") for name in work.methods}
    for cn0 in args.cn0_grid:
        if not -300<cn0/10<300:raise ValueError("C/N0 numeric range exceeded")
        variance=data["noise_reference_power"]*work.sample_rate/10**(cn0/10)
        scale=1/math.sqrt(variance)
        if not math.isfinite(scale) or scale<=0:raise ValueError("invalid physical noise variance")
        # Independent positive vs null streams; repeated C/N0 values reuse the
        # SAME positive noise draws, enabling paired sensitivity comparisons.
        positive_seed=shared._splitmix64(args.seed^shared.POSITIVE_SALT)
        positive,winner,positive_seconds,oracle_counts=collect(work,args.trials,positive_seed,"holdout",scale)
        results={}
        for name in work.methods:
            results[name]={"threshold":work.thresholds[name],"threshold_metadata":work.threshold_metadata[name],
                "positive":summarize(positive[name],winner[name],work.thresholds[name],work.labels,work.truth,"positive"),
                "null":null_metrics[name]}
        known=dict(work.reference_distance)
        if known.get("available"):
            known["known_channel_binary_error"] = .5*math.erfc(.5*math.sqrt(known["squared_distance_samples"]/variance))
            known["same_band_known_channel_binary_error"] = .5*math.erfc(.5*math.sqrt(known["retained_squared_distance_samples"]/variance))
        if oracle_counts is not None:
            known["empirical_binary_decisions"]={name:{"trials":args.trials,"raw_bit_errors":count,
                "raw_bit_error_wilson95":wilson(count,args.trials),"admission_gate":False}
                for name,count in zip(("full_band","same_retained_band"),oracle_counts)}
        yield {"schema":"triad-detector-study-v1","source_hash":data["source_hash"],"seed":args.seed,
            "torch_version":torch.__version__,"device":str(work.device),
            "device_name":torch.cuda.get_device_name(work.device) if work.device.type=="cuda" else "CPU",
            "cn0_db_hz":cn0,"sample_noise_variance":variance,"sample_rate":work.sample_rate,
            "source_observation_samples":work.original_samples,"source_observation_seconds":work.original_samples/work.sample_rate,
            "processed_observation_samples":work.samples,"processed_observation_seconds":work.samples/work.sample_rate,
            "padding_factor":args.padding_factor,"padding_description":"zero transmitted signal plus independently observed AWGN guard samples",
            "carrier_spacing_hz":work.spacing,"carrier_mask_half_width_hz":work.spacing/2,
            "channelizer":"disjoint circular finite-window unitary-DFT masks; no streaming/analog channelizer equivalence claimed",
            "signal_energy_samples":work.signal_energy,"retained_signal_energy_samples":work.retained_signal_energy,
            "retained_signal_fraction":work.retained_signal_energy/work.signal_energy if work.signal_energy else None,
            "minimum_template_retained_fraction":work.basis_retained_fraction_min,
            "spectral_metrics":work.spectral_metrics,
            "minimum_template_block_energy_fraction":1e-10,
            "block_samples":work.block,"block_seconds":work.block/work.sample_rate,"blocks":work.blocks,
            "candidate_count":work.candidates,"candidate_columns":work.columns,"rank_min":work.rank_min,"rank_max":work.rank_max,
            "source_bit":work.truth,"channel":manifest.get("channel"),"candidateparams":data["candidateparams"],
            "block_shared_model":"one common complex gain over three carriers per physical block; model mismatch if channel is frequency selective",
            "block_independent_model":"three independent carrier gains per block; higher nuisance dimension can reduce distinguishability",
            "product_model":"three correlated pair-product patterns, noncoherent block energy; empirically calibrated full-bank maximum",
            "amplitude_model":"carrier/block centered-power templates, independent nonnegative power gain per carrier/block; sum(max(0, correlation)^2/template_variation_energy), empirically calibrated full-bank maximum; no exact Rice likelihood claim",
            "raw_reference_model":"flat rank1 and exported coupled-tap subspaces in the same retained white Fourier-bin observations",
            "projector_checks":work.projector_checks,
            "known_channel_benchmark":known,"methods":results,
            "nominal_false_admission_per_method":args.false_admission,
            "false_admission_scope":"one complete declared candidate bank; comparison methods are separate detectors, not an operational combined admission policy",
            "calibration_trials":args.calibration_trials,"holdout_trials":args.trials,
            "channel_realizations":1,"probability_conditioning":"conditional on the exported noiseless waveform/channel and declared fixed candidate bank",
            "calibration_and_holdout_independent":True,"cn0_sweep_noise_paired":True,
            "prepare_seconds":prepare,"calibration_seconds":calibration_seconds,"null_holdout_seconds":null_seconds,
            "positive_seconds":positive_seconds,"actual_batch":work.batch,"workspace_estimate_bytes":work.workspace_estimate_bytes,
            "tf32_enabled":False,"selected_dtype":"complex64"}


def main()->int:
    args=parser().parse_args()
    try:
        if args.output:
            args.output.parent.mkdir(parents=True,exist_ok=True);stream=args.output.open("w",encoding="utf-8")
        else:stream=sys.stdout
        try:
            for result in run(args):stream.write(json.dumps(result,separators=(",",":"),sort_keys=True,allow_nan=False)+"\n");stream.flush()
        finally:
            if args.output:stream.close()
        return 0
    except (ValueError,RuntimeError,OSError) as error:
        print(f"triad-detector-study: {error}",file=sys.stderr);return 2

if __name__=="__main__":raise SystemExit(main())
