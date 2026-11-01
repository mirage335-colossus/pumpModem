#!/usr/bin/env python3
"""Actual-observation static Gaussian-channel / bounded constant-CFO likelihood.

The FFT accumulates complex evidence BEFORE fitting/squaring. CFO phase is
constant within each exported block, evaluated at its physical center; this is
an explicit signal-model approximation, not raw-sample coherent acquisition.
All frequency and acquisition alternatives are normalized mixtures. Noise UCB
uses only actual received energy and residuals orthogonal to the complete bank.
"""
from __future__ import annotations

import argparse
from contextlib import ExitStack
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import platform
import time

import numpy as np
import torch
from scipy.stats import beta

from triad_noise_likelihood import FalseAdmissionBudget, variance_upper_confidence, whole_bank_residual
from triad_state_workspace import BlockWorkspace, WorkspaceError

METHODS = ("static-frequency", "static-frequency-ucb")


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024**2), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_args(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("inputs", nargs="+", type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--cn0", type=float, required=True, help="declared reference C/N0 in dB-Hz; never inferred from simulator noise")
    p.add_argument("--reference-variance", type=float)
    p.add_argument("--methods", choices=METHODS, nargs="+", default=list(METHODS))
    p.add_argument("--taps", type=int, choices=(1,2), default=2)
    p.add_argument("--frequency-bound", type=float, default=.65)
    p.add_argument("--frequency-step", type=float, help="maximum FFT-grid spacing; default 1/(4*sampled duration)")
    p.add_argument("--implementation", choices=("fft","direct"), default="fft")
    p.add_argument("--device", default="cpu")
    p.add_argument("--precision", choices=("fp32","fp64"), default="fp64")
    p.add_argument("--threads", type=int, default=2)
    p.add_argument("--memory-gib", type=float, default=4.)
    p.add_argument("--false-probability", type=float, default=1e-4)
    p.add_argument("--horizon-seconds", type=float, default=86400.)
    p.add_argument("--attempt-spacing", type=float, default=60.)
    p.add_argument("--duration-alternatives", type=int, default=3)
    p.add_argument("--selection-methods", type=int, default=9)
    p.add_argument("--calibration-alternatives", type=int, default=2)
    a = p.parse_args(argv)
    if a.output.exists() or a.output.with_suffix(".scores.npz").exists():
        p.error("report and score paths must both be fresh")
    if not math.isfinite(a.cn0) or not math.isfinite(a.frequency_bound) or a.frequency_bound < 0:
        p.error("C/N0 must be finite and frequency bound nonnegative finite")
    for name in ("reference_variance","frequency_step"):
        value = getattr(a,name)
        if value is not None and (not math.isfinite(value) or value <= 0):
            p.error(name+" must be finite and positive")
    for name in ("memory_gib","horizon_seconds","attempt_spacing"):
        value = getattr(a,name)
        if not math.isfinite(value) or value <= 0:
            p.error(name+" must be finite and positive")
    if not 1 <= a.threads <= 2 or len(a.methods) != len(set(a.methods)):
        p.error("use one or two CPU threads and distinct methods")
    if a.selection_methods < len(a.methods) or a.duration_alternatives < 1 or a.calibration_alternatives < 1:
        p.error("family counts must include all selected alternatives")
    a.memory_limit_bytes = int(a.memory_gib*1024**3)
    a.budget = FalseAdmissionBudget(false_probability=a.false_probability,
        attempts=math.ceil(a.horizon_seconds/a.attempt_spacing),
        duration_alternatives=a.duration_alternatives, model_alternatives=a.selection_methods,
        candidates=2, calibration_segments=a.calibration_alternatives)
    return a


def grid_shape(block_count, dt, sampled_seconds, bound, maximum_step=None):
    if block_count < 1 or not all(math.isfinite(v) and v > 0 for v in (dt,sampled_seconds)):
        raise ValueError("invalid observation geometry")
    if not math.isfinite(bound) or not 0 <= bound < .5/dt:
        raise ValueError("CFO bound must be below the block-center Nyquist frequency; retain shorter blocks")
    step = maximum_step if maximum_step is not None else 1/(4*sampled_seconds)
    if not math.isfinite(step) or step <= 0:
        raise ValueError("frequency spacing must be positive finite")
    required = max(block_count, math.ceil(1/(dt*step)))
    nfft = 1 << (required-1).bit_length()
    if nfft > 2**26:
        raise MemoryError("FFT length exceeds the explicit 2^26 supported limit")
    spacing = 1/(nfft*dt)
    half = math.ceil(bound/spacing) if bound else 0
    # A short capture may need extra padding so both outward endpoints are
    # strictly inside Nyquist. Padding only refines the requested grid.
    while half >= nfft/2:
        nfft *= 2
        if nfft > 2**26:
            raise MemoryError("outward CFO endpoint resolution exceeds the explicit 2^26 FFT limit")
        spacing = 1/(nfft*dt)
        half = math.ceil(bound/spacing) if bound else 0
    return nfft, half


def grid_for(block_count, dt, sampled_seconds, bound, maximum_step=None):
    """Convenience numerical helper; the CLI preflights grid_shape first."""
    nfft,half=grid_shape(block_count,dt,sampled_seconds,bound,maximum_step)
    bins=np.arange(-half,half+1,dtype=np.int64)
    return nfft,bins,bins.astype(np.float64)/(nfft*dt)


def memory_bound(blocks, candidates, taps, dimension, nfft, frequency_count, precision):
    cb = 16 if precision == "fp64" else 8
    # CPU original/projected/whitened arrays + conversion/packing copies;
    # padded FFT workspace/output/crop, direct GEMM tile, FP64 score reductions.
    resident = 5*blocks*candidates*taps*16
    fft = 5*nfft*candidates*taps*cb
    scores = 4*frequency_count*candidates*8
    direct = 64*blocks*16 + 64*candidates*taps*cb
    preparation = 8*dimension*dimension*16 + 8*candidates*taps*taps*16 + 2**20
    return resident+fft+scores+direct+preparation


def _blocks(workspace, reference):
    """Sequential actual-only reads avoid descriptor or mapped-page growth."""
    d = workspace.dimension
    names = ("gram.c128","observation.c128","received_energy.f64","intervals.u64","valid_rows.u64")
    with ExitStack() as stack:
        files = {name:stack.enter_context((workspace.directory/name).open("rb")) for name in names}
        for j in range(workspace.block_count):
            G = np.fromfile(files["gram.c128"],dtype="<c16",count=d*d)
            r = np.fromfile(files["observation.c128"],dtype="<c16",count=d)
            E = np.fromfile(files["received_energy.f64"],dtype="<f8",count=1)
            interval = np.fromfile(files["intervals.u64"],dtype="<u8",count=2)
            valid = np.fromfile(files["valid_rows.u64"],dtype="<u8",count=1)
            if (G.size,r.size,E.size,interval.size,valid.size)!=(d*d,d,1,2,1):
                raise WorkspaceError("statistics file changed or ended during read")
            expected = (j*workspace.block_samples,min((j+1)*workspace.block_samples,workspace.sample_count))
            if tuple(map(int,interval)) != expected or int(valid[0]) > expected[1]-expected[0]:
                raise WorkspaceError("block calendar or valid-row count differs from public geometry")
            center = (int(interval[0])+int(interval[1]))/(2*workspace.sample_rate)
            yield r/reference, G.reshape(d,d)/reference, float(E[0])/reference, int(valid[0]), center


def prepare(workspace, mapping, reference, delta):
    b,c,k = workspace.block_count,*mapping.shape
    raw = np.empty((b,c,k),np.complex128)
    projected = np.empty_like(raw)
    sums = np.zeros((2,c,k,k),np.complex128)
    corrections = np.zeros_like(sums)
    centers = np.empty(b,np.float64)
    residual = residual_correction = 0.
    dof = valid_total = 0
    for j,(r,G,E,valid,center) in enumerate(_blocks(workspace,reference)):
        result = whole_bank_residual(torch.from_numpy(r[None,:]),torch.from_numpy(G),E,valid)
        pr,pg = result.projected_correlations.numpy()[0],result.projected_gram.numpy()
        raw[j],projected[j],centers[j] = r[mapping],pr[mapping],center
        item = np.stack((G[mapping[:,:,None],mapping[:,None,:]],pg[mapping[:,:,None],mapping[:,None,:]]))
        adjusted = item-corrections
        updated = sums+adjusted
        corrections = (updated-sums)-adjusted
        sums = updated
        adjusted_e = float(result.energy[0])-residual_correction
        updated_e = residual+adjusted_e
        residual_correction = (updated_e-residual)-adjusted_e
        residual = updated_e
        dof += int(result.complex_dof)
        valid_total += valid
    upper = float(variance_upper_confidence(residual,dof,delta))
    return raw,projected,sums,centers,upper,{"pooled_residual_energy_over_reference":residual,
        "pooled_complex_dof":dof,"variance_upper_over_reference":upper,"valid_complex_samples":valid_total}


def whiten_channel(r, gram, prior_powers=None):
    """CPU FP64 Cholesky preparation; r is [blocks,candidates,taps]."""
    r = torch.as_tensor(np.asarray(r),dtype=torch.complex128)
    G = torch.as_tensor(np.asarray(gram),dtype=torch.complex128)
    if r.ndim != 3 or G.shape != (r.shape[1],r.shape[2],r.shape[2]):
        raise ValueError("channel statistic shapes differ")
    if not bool(torch.isfinite(r).all()) or not bool(torch.isfinite(G).all()):
        raise ValueError("channel statistics must be finite")
    k = r.shape[-1]
    powers = torch.full((k,),1/k,dtype=torch.float64) if prior_powers is None else torch.as_tensor(prior_powers,dtype=torch.float64)
    if powers.shape != (k,) or not bool(torch.isfinite(powers).all()) or bool((powers<=0).any()):
        raise ValueError("channel prior powers must be positive finite")
    scale = G.abs().amax().clamp_min(torch.finfo(torch.float64).tiny)
    if bool(((G-G.mH).abs().amax()>1e-10*scale)):
        raise ValueError("channel Gram is not Hermitian")
    G = (G+G.mH)*.5
    if bool((torch.linalg.eigvalsh(G)[...,0]<-1e-10*scale).any()):
        raise ValueError("channel Gram is not PSD")
    L = torch.linalg.cholesky(G+torch.diag(1/powers))
    w = torch.linalg.solve_triangular(L,r.permute(1,2,0),upper=False).permute(2,0,1).contiguous()
    determinant = 2*L.diagonal(dim1=-2,dim2=-1).real.log().sum(-1)+powers.log().sum()
    return w.numpy(),determinant.numpy()


def frequency_correlations(r, centers, dt, nfft, bins, *, implementation="fft",device="cpu",precision="fp64"):
    """FFT or independent direct sums on EXACTLY the same public CFO grid."""
    r = np.asarray(r)
    centers = np.asarray(centers,dtype=np.float64)
    if r.ndim != 3 or centers.shape != (len(r),) or not np.isfinite(centers).all():
        raise ValueError("invalid correlation/center geometry")
    expected = (np.arange(len(r),dtype=np.float64)+.5)*dt
    tolerance = 32*np.finfo(float).eps*max(1.,float(expected[-1]))
    if not np.allclose(centers[:-1],expected[:-1],rtol=0,atol=tolerance) or not expected[-1]-dt/2 <= centers[-1] <= expected[-1]+tolerance:
        raise ValueError("FFT requires regular block centers except a shorter final block")
    dtype = torch.complex128 if precision=="fp64" else torch.complex64
    x = torch.as_tensor(r,dtype=dtype,device=device)
    frequencies = np.asarray(bins,dtype=np.float64)/(nfft*dt)
    if implementation == "fft":
        result = torch.fft.fft(x,n=nfft,dim=0)[torch.as_tensor(np.asarray(bins)%nfft,dtype=torch.long,device=device)]
        phase = np.exp(-2j*np.pi*frequencies*(dt/2))
        result *= torch.as_tensor(phase,dtype=dtype,device=device)[:,None,None]
        if centers[-1] != expected[-1]:
            correction = np.exp(-2j*np.pi*frequencies*centers[-1])-np.exp(-2j*np.pi*frequencies*expected[-1])
            result += torch.as_tensor(correction,dtype=dtype,device=device)[:,None,None]*x[-1]
    elif implementation == "direct":
        chunks=[]
        for start in range(0,len(frequencies),64):
            phase=np.exp(-2j*np.pi*frequencies[start:start+64,None]*centers[None,:])
            chunks.append((torch.as_tensor(phase,dtype=dtype,device=device)@x.reshape(len(x),-1)).reshape(-1,*x.shape[1:]))
        result=torch.cat(chunks,dim=0)
    else:
        raise ValueError("unknown frequency implementation")
    if not bool(torch.isfinite(result).all()):
        raise ValueError("frequency accumulation became nonfinite")
    return result


def scores_from_correlations(correlations, determinant, labels):
    # Bulk FFT width is explicit; the sensitive square/sum and all evidence
    # mixtures are evaluated in FP64, including when bulk FFT is complex64.
    value=correlations.to(torch.complex128)
    likelihood=value.abs().square().sum(-1)-torch.as_tensor(determinant,dtype=torch.float64,device=value.device)[None,:]
    candidate=torch.logsumexp(likelihood,dim=0)-math.log(likelihood.shape[0])
    labels=torch.as_tensor(labels,dtype=torch.long,device=value.device)
    scores=torch.stack([torch.logsumexp(candidate[labels==bit],dim=0)-math.log(int((labels==bit).sum())) for bit in (0,1)])
    if not bool(torch.isfinite(scores).all()):
        raise ValueError("static frequency evidence became nonfinite")
    return scores.detach().cpu().numpy()[None,:]


def classify(scores, truth, threshold, control):
    winner=int(np.argmax(scores[0]));accepted=bool(scores[0,winner]>=threshold)
    if control=="signal":
        return {"captures":1,"correct_accepted":int(accepted and winner==truth),"wrong_accepted":int(accepted and winner!=truth),"missed":int(not accepted),"raw_correct":int(winner==truth)}
    return {"captures":1,"false_admitted":int(accepted),"not_admitted":int(not accepted)}


def summary(captures,method,control):
    rows=[c for c in captures if c["control"]==control]
    good=[c for c in rows if c["status"]=="completed"]
    count={key:sum(c["methods"][method]["counts"].get(key,0) for c in good) for key in ("captures","correct_accepted","wrong_accepted","missed","false_admitted")}
    failures=len(rows)-len(good)
    n=len(rows) if control=="signal" else len(good)
    if control=="signal":count["captures"]=n;count["missed"]+=failures
    success=count["correct_accepted"] if control=="signal" else count["false_admitted"]
    lower=float(beta.ppf(.05,success,n-success+1)) if success else 0.
    upper=float(beta.ppf(.95,success+1,n-success)) if n and success<n else 1.
    return {"attempted_physical_paths":len(rows),"completed_physical_paths":len(good),"failed_physical_paths":failures,
        "aggregate_counts":count,"pointwise_one_sided_95_lower":lower,"pointwise_one_sided_95_upper":upper,
        "confidence_scope":"independent actual paths; separate one-sided intervals, not a simultaneous two-sided interval",
        "null_qualification_complete":failures==0 if control=="null" else None}


def run(args):
    torch.set_num_threads(args.threads);torch.set_float32_matmul_precision("highest")
    if args.device.startswith("cuda") and not torch.cuda.is_available():raise ValueError("CUDA unavailable")
    if args.device.startswith("cuda"):
        torch.backends.cuda.matmul.allow_tf32=False
        torch.cuda.reset_peak_memory_stats(args.device)
    paths=[str(p.resolve()) for p in args.inputs]
    if len(set(paths))!=len(paths):raise WorkspaceError("duplicate physical path")
    seen=set();captures=[];arrays={};started=time.perf_counter()
    for path_index,path in enumerate(args.inputs):
        ws=BlockWorkspace(path,max_tile_blocks=1,memory_limit_bytes=args.memory_limit_bytes)
        control=ws.manifest.get("control","signal")
        row={"input":str(path),"physical_path_index":path_index,"control":control,"methods":{}}
        try:
            identity=tuple(ws.manifest.get(k) for k in ("channel_seed","key_seed","epoch"))
            if all(x is not None for x in identity):
                if identity in seen:raise WorkspaceError("duplicate channel/key/epoch identity")
                seen.add(identity)
            if control not in ("signal","null"):raise WorkspaceError("only actual signal/null controls are supported")
            if not ws.has_observation or not ws.has_received_energy:raise WorkspaceError("actual projections and actual received energy are required")
            if not ws.gaussian_projection_supported:raise WorkspaceError("sampled frontend does not support this Gaussian null")
            indices=[i for i,v in enumerate(ws.column_mapping) if len(v)==args.taps]
            mapping=np.asarray([ws.column_mapping[i] for i in indices],dtype=np.int64)
            labels=np.asarray([ws.candidates[i]["bit"] for i in indices],dtype=np.int64)
            if mapping.ndim!=2 or set(labels.tolist())!={0,1}:raise WorkspaceError("bank must contain both bits at the requested tap dimension")
            dt=ws.block_samples/ws.sample_rate
            nfft,half=grid_shape(ws.block_count,dt,ws.sample_count/ws.sample_rate,args.frequency_bound,args.frequency_step)
            bound=memory_bound(ws.block_count,len(indices),args.taps,ws.dimension,nfft,2*half+1,args.precision)
            if bound>args.memory_limit_bytes:raise MemoryError(f"FFT workspace estimate {bound} exceeds limit {args.memory_limit_bytes}")
            bins=np.arange(-half,half+1,dtype=np.int64)
            frequencies=bins.astype(np.float64)/(nfft*dt)
            reference=args.reference_variance or ws.sample_rate/10**(args.cn0/10)
            if not math.isfinite(reference) or reference<=0:raise ValueError("reference variance is unresolved")
            t0=time.perf_counter();raw,projected,G,centers,upper,calibration=prepare(ws,mapping,reference,args.budget.delta_per_segment)
            preparation=time.perf_counter()-t0
            truth=int(ws.manifest.get("signal_bit_simulation_truth",ws.manifest.get("signal_bit",0)))
            if truth not in (0,1):raise WorkspaceError("evaluation truth bit must be binary")
            for method in args.methods:
                t0=time.perf_counter()
                r,g=(projected/upper,G[1]/upper) if method.endswith("-ucb") else (raw,G[0])
                w,determinant=whiten_channel(r,g)
                whitening=time.perf_counter()-t0
                if args.device.startswith("cuda"):torch.cuda.synchronize(args.device)
                t0=time.perf_counter()
                R=frequency_correlations(w,centers,dt,nfft,bins,implementation=args.implementation,device=args.device,precision=args.precision)
                scores=scores_from_correlations(R,determinant,labels)
                if args.device.startswith("cuda"):torch.cuda.synchronize(args.device)
                inference=time.perf_counter()-t0
                arrays[f"path{path_index:05d}_{control}_{method}"]=scores
                row["methods"][method]={"counts":classify(scores,truth,args.budget.log_threshold,control),"threshold":args.budget.log_threshold,"bit_log_scores":scores[0].tolist(),"cpu_fp64_whitening_seconds":whitening,"transfer_fft_and_scoring_seconds":inference}
            row.update(status="completed",truth_bit_simulation_only=truth if control=="signal" else None,
                reference_variance=reference,calibration=calibration,cpu_fp64_preparation_seconds=preparation,
                memory_bound_bytes=bound,sampled_seconds=ws.sample_count/ws.sample_rate,block_count=ws.block_count,
                selected_candidate_count=len(indices),full_bank_dimension=ws.dimension,
                frequency_grid={"nfft":nfft,"count":len(bins),"spacing_hz":1/(nfft*dt),"requested_bound_hz":args.frequency_bound,"actual_endpoints_hz":[float(frequencies[0]),float(frequencies[-1])],"prior":"uniform finite-grid mixture; endpoints rounded outward","partial_last_center_corrected":bool(centers[-1]!=(ws.block_count-.5)*dt)},
                input_hashes={name:sha256(ws.directory/name) for name in ("manifest.json","gram.c128","observation.c128","received_energy.f64","intervals.u64","valid_rows.u64")})
        except Exception as error:
            row.update(status="error",error_type=type(error).__name__,error=str(error))
        finally:
            for array in ws._maps.values():array._mmap.close()
            ws._maps.clear()
        captures.append(row)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    scores_path=args.output.with_suffix(".scores.npz");np.savez_compressed(scores_path,**arrays)
    configuration={k:v for k,v in vars(args).items() if k not in ("inputs","output","budget")}
    result={"schema":"triad-static-frequency-study-v1","source_sha256":sha256(__file__),"workspace_source_sha256":sha256(Path(__file__).with_name("triad_state_workspace.py")),"noise_likelihood_source_sha256":sha256(Path(__file__).with_name("triad_noise_likelihood.py")),"configuration":configuration,"family":args.budget.diagnostics(),"captures":captures,
        "summary_by_control":{control:{method:summary(captures,method,control) for method in args.methods} for control in ("signal","null") if any(c["control"]==control for c in captures)},
        "failed_physical_capture_records":sum(c["status"]!="completed" for c in captures),"scores_file":scores_path.name,"scores_sha256":sha256(scores_path),"wall_seconds_joint":time.perf_counter()-started,
        "host":{"python":platform.python_version(),"numpy":np.__version__,"torch":torch.__version__,"device":args.device,"threads":torch.get_num_threads()},
        "arithmetic":{"rank_residual_energy_gram_cholesky_whitening":"CPU FP64; whitening includes all retained block correlations","fft_gemm_bulk":args.precision,"phase_arguments":"FP64 then cast to bulk width","quadratic_and_evidence_reductions":"FP64","tf32_allowed":False},
        "limitations":["static Gaussian tap coefficients and constant CFO, approximated as one common phase per exported block","actual observations only; one constant unknown white proper-complex noise variance for pooled UCB","frequency, timing, epoch and bit alternatives are normalized finite mixtures; no continuous-grid or arbitrary channel guarantee","K9 score family is separate from earlier K7 results unless their score gates are corrected; the shared calibration allocation does not depend on K","CPU preparation streams full-bank statistics; retained selected correlations and FFT workspace grow with observation length within the declared memory cap"]}
    if args.device.startswith("cuda"):result["cuda_memory"]={"maximum_allocated_bytes":torch.cuda.max_memory_allocated(args.device),"maximum_reserved_bytes":torch.cuda.max_memory_reserved(args.device)}
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+"\n")
    return result


def main(argv=None):
    args=parse_args(argv)
    result=run(args)
    print(json.dumps({"output":str(args.output),"captures":len(result["captures"]),"failed":result["failed_physical_capture_records"],"wall_seconds_joint":result["wall_seconds_joint"]}))
    return int(bool(result["failed_physical_capture_records"]))


if __name__=="__main__":raise SystemExit(main())
