#!/usr/bin/env python3
"""Prepared-design implementation of the triad Gaussian state likelihood.

For a fixed public Gram sequence and elapsed-time calendar, covariance
prediction, eigendecomposition, Cholesky, posterior covariance and log
determinant do not depend on the observed correlations. This module computes
those quantities once in CPU complex128 and reuses them for multiple
observations. The online mean and likelihood updates still consume every
observation. Finite-precision results are an implementation variant, not an
exactly equivalent numerical computation.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys
import time
from typing import Sequence

import torch

from triad_gaussian_state import GaussianPrior, GaussianStateFilter


class PreparedGaussianDesign:
    """FP64 public design tensors, independent of received data."""

    def __init__(self, grams: torch.Tensor, elapsed_seconds: Sequence[float],
                 prior: GaussianPrior, *, memory_limit_bytes: int = 4*1024**3):
        if not isinstance(grams, torch.Tensor) or not grams.is_complex():
            raise TypeError("grams must be a complex tensor")
        if grams.ndim < 3 or grams.shape[-1] != grams.shape[-2]:
            raise ValueError("grams must have shape [blocks,...,taps,taps]")
        if len(elapsed_seconds) != grams.shape[0]:
            raise ValueError("elapsed-time count must equal the Gram block count")
        if grams.shape[-1] != len(prior.stationary_power):
            raise ValueError("Gram tap extent differs from the prior")
        if memory_limit_bytes < 1:
            raise ValueError("memory limit must be positive")

        # Include the temporary posterior list and the stacked destination in
        # peak preparation memory, in addition to the retained Grams. Check
        # this before constructing outputs.
        blocks, taps = int(grams.shape[0]), int(grams.shape[-1])
        if blocks == 0:
            raise ValueError("prepared design requires at least one block")
        leading = math.prod(grams.shape[1:-2])
        complex_bytes = blocks * leading * taps * taps * 16
        scalar_bytes = blocks * leading * 8 + blocks * 16
        estimate = 3*complex_bytes + scalar_bytes
        if estimate > memory_limit_bytes:
            raise MemoryError(f"prepared design needs {estimate} owned bytes, over limit {memory_limit_bytes}")

        g_cpu = grams.detach().to(device="cpu", dtype=torch.complex128).contiguous()
        if not bool(torch.isfinite(g_cpu).all()):
            raise ValueError("Gram design contains nonfinite values")
        asym = (g_cpu-g_cpu.mH).abs().amax(dim=(-2, -1))
        scale = g_cpu.abs().amax(dim=(-2, -1)).clamp_min(1.)
        if bool((asym > 1e-10*scale).any()):
            raise ValueError("Gram design is not Hermitian")
        g_cpu = (g_cpu+g_cpu.mH)*.5
        eig = torch.linalg.eigvalsh(g_cpu)
        if bool((eig[..., 0] < -1e-10*scale).any()):
            raise ValueError("Gram design is not positive semidefinite")

        dts = tuple(float(value) for value in elapsed_seconds)
        if any(not math.isfinite(dt) or dt < 0 for dt in dts):
            raise ValueError("elapsed seconds must be finite and nonnegative")
        self.prior = prior
        self.dts = dts
        self.grams_cpu = g_cpu
        self.posterior_cpu = []
        self.log_determinants_cpu = []
        self.rotations = []
        self.covariance_minima = []
        k = taps
        power = torch.diag(torch.tensor(prior.stationary_power, dtype=torch.float64,
                                        device="cpu")).to(torch.complex128)
        covariance = power.expand(g_cpu.shape[1:]).clone()
        for index, (gram, dt) in enumerate(zip(g_cpu, dts)):
            if index:
                if math.isinf(prior.coherence_seconds):
                    decay, innovation = 1., 0.
                else:
                    decay = math.exp(-dt/prior.coherence_seconds)
                    innovation = -math.expm1(-2*dt/prior.coherence_seconds)
                covariance = covariance*(decay*decay)+power*innovation
            covariance = (covariance+covariance.mH)*.5
            values, vectors = torch.linalg.eigh(covariance)
            scale_cov = covariance.abs().amax(dim=(-2, -1)).clamp_min(1.)
            if bool((values[..., 0] < -1e-10*scale_cov).any()):
                raise ValueError("prepared prior covariance lost positive semidefiniteness")
            self.covariance_minima.append(float((values[..., 0]/scale_cov).amin()))
            factor = vectors*values.clamp_min(0.).sqrt()[..., None, :]
            eye = torch.eye(k, dtype=torch.complex128)
            info = eye+factor.mH@gram@factor
            info = (info+info.mH)*.5
            chol, status = torch.linalg.cholesky_ex(info, check_errors=False)
            if bool((status != 0).any()):
                raise ValueError("prepared innovation information is not positive definite")
            posterior = factor@torch.cholesky_solve(factor.mH, chol)
            posterior = (posterior+posterior.mH)*.5
            logdet = 2*chol.diagonal(dim1=-2, dim2=-1).real.log().sum(-1)
            self.posterior_cpu.append(posterior.contiguous())
            self.log_determinants_cpu.append(logdet.contiguous())
            phase_cycles = prior.frequency_hz*dt
            if not math.isfinite(phase_cycles):
                raise ValueError("frequency/time product overflow")
            self.rotations.append(_rotation(prior, dt))
            covariance = posterior

        self.posterior_cpu = torch.stack(self.posterior_cpu).contiguous()
        self.log_determinants_cpu = torch.stack(self.log_determinants_cpu).contiguous()
        self.rotations_cpu = torch.tensor(self.rotations, dtype=torch.complex128)
        self.memory_limit_bytes = memory_limit_bytes
        self.estimated_owned_bytes = int(self.grams_cpu.numel()*16 +
            self.posterior_cpu.numel()*16 + self.log_determinants_cpu.numel()*8 +
            self.rotations_cpu.numel()*16)
        if self.estimated_owned_bytes > memory_limit_bytes:
            raise MemoryError("prepared tensors exceed the declared memory limit")

    @property
    def blocks(self):
        return len(self.dts)

    @property
    def taps(self):
        return len(self.prior.stationary_power)

    @property
    def leading_shape(self):
        return tuple(self.grams_cpu.shape[1:-2])

    def to(self, device="cpu", dtype=torch.complex128, *, check_inputs=True) -> "PreparedGaussianRuntime":
        if dtype not in (torch.complex64, torch.complex128):
            raise ValueError("prepared Gaussian state must be complex64 or complex128")
        dev = torch.device(device)
        if dtype == torch.complex64 and dev.type == "cuda":
            if torch.backends.cuda.matmul.allow_tf32 or torch.get_float32_matmul_precision() != "highest":
                raise ValueError("FP32 mode requires TF32 disabled and highest matmul precision")
        if dev.type == "cuda" and dev.index is None:
            dev = torch.device("cuda", torch.cuda.current_device())
        return PreparedGaussianRuntime(self, dev, dtype, check_inputs=check_inputs)


def _rotation(prior: GaussianPrior, dt: float) -> complex:
    import cmath
    decay = math.exp(-dt/prior.coherence_seconds) if not math.isinf(prior.coherence_seconds) else 1.
    cycles = prior.frequency_hz*dt
    if not math.isfinite(cycles):
        raise ValueError("frequency/time product overflow")
    return cmath.rect(decay, 2*math.pi*math.remainder(cycles, 1.))


class PreparedGaussianRuntime:
    """Online observation state for one fixed prepared design."""

    def __init__(self, design: PreparedGaussianDesign, device: torch.device,
                 dtype: torch.dtype, *, check_inputs=True):
        self.design, self.device, self.dtype = design, device, dtype
        self.check_inputs = bool(check_inputs)
        self.grams = design.grams_cpu.to(device=device, dtype=dtype)
        self.posterior = design.posterior_cpu.to(device=device, dtype=dtype)
        self.logdet = design.log_determinants_cpu.to(device=device, dtype=torch.float64)
        self.rotation = design.rotations_cpu.to(device=device, dtype=dtype)
        power = torch.tensor(design.prior.stationary_power,
                             dtype=torch.float64 if dtype == torch.complex128 else torch.float32)
        self.mean = None
        self.score = None
        self.observation_shape = None
        self.blocks = 0
        self._power = power

    def reset(self):
        """Reset observation state while retaining resident prepared tensors."""
        self.mean = None
        self.score = None
        self.observation_shape = None
        self.blocks = 0

    def update(self, correlations: torch.Tensor):
        if not isinstance(correlations, torch.Tensor) or not correlations.is_complex():
            raise TypeError("correlations must be a complex tensor")
        expected_ndim = len(self.design.leading_shape)+2
        if correlations.ndim != expected_ndim or correlations.shape[0] < 1:
            raise ValueError("correlations require [batch, *design_leading, taps] with a positive batch")
        if correlations.shape[-1] != self.design.taps:
            raise ValueError("correlation tap extent differs from the design")
        if tuple(correlations.shape[1:-1]) != self.design.leading_shape:
            raise ValueError("correlation leading extents differ from the design")
        if correlations.device.type != self.device.type or correlations.device != self.device:
            raise ValueError("correlations must already be on the prepared device")
        if correlations.requires_grad:
            raise ValueError("autograd observations are unsupported")
        r = correlations.to(dtype=self.dtype)
        if self.check_inputs and not bool(torch.isfinite(r).all()):
            raise ValueError("correlations contain nonfinite values")
        if self.blocks >= self.design.blocks:
            raise ValueError("more observations than prepared blocks")
        i = self.blocks
        if self.observation_shape is None:
            self.observation_shape = tuple(r.shape)
        elif tuple(r.shape) != self.observation_shape:
            raise ValueError("correlation batch or leading shape changed")
        gram, posterior = self.grams[i], self.posterior[i]
        if self.mean is None:
            self.mean = torch.zeros_like(r)
            self.score = torch.zeros(r.shape[:-1], dtype=torch.float64, device=self.device)
        predicted = self.mean*self.rotation[i]
        g_mean = (gram @ predicted[..., None]).squeeze(-1)
        difference = r-g_mean
        adjustment = (posterior @ difference[..., None]).squeeze(-1)
        linear = 2*(predicted.conj()*r).sum(-1).real
        predicted_energy = (predicted.conj()*g_mean).sum(-1).real
        innovation_energy = (difference.conj()*adjustment).sum(-1).real
        # Match the reference's sensitive-reduction policy: cast each score
        # component to FP64 before subtracting/adding them.
        increment = (linear.to(torch.float64)-predicted_energy.to(torch.float64)+
                     innovation_energy.to(torch.float64)-self.logdet[i])
        self.mean = predicted+adjustment
        self.score = self.score+increment
        self.blocks += 1
        return self.score

    def scores(self):
        if self.score is None:
            raise ValueError("no correlations have been observed")
        return self.score

    def diagnostics(self):
        return {"blocks": self.blocks, "taps": self.design.taps,
                "state_dtype": str(self.dtype), "log_likelihood_dtype": "torch.float64",
                "design_dtype": "complex128 CPU preparation", "device": str(self.device),
                "design_owned_bytes": self.design.estimated_owned_bytes,
                "input_checks":self.check_inputs,
                "execution": "precomputed Gram/covariance factors; per-observation mean and likelihood update",
                "precision_scope": "finite precision; paired numerical validation required"}


def prepare(grams, elapsed_seconds, prior, *, memory_limit_bytes=4*1024**3):
    return PreparedGaussianDesign(grams, elapsed_seconds, prior,
                                  memory_limit_bytes=memory_limit_bytes)


def filter(design, correlations, *, device="cpu", dtype=torch.complex128,
           check_inputs=True):
    """Run [blocks,batch,*design_leading,taps], returning FP64 log evidence."""
    if correlations.shape[0] != design.blocks:
        raise ValueError("correlation block count differs from the design")
    runtime = design.to(device, dtype, check_inputs=check_inputs)
    for index in range(design.blocks):
        runtime.update(correlations[index:index+1])
    return runtime.scores(), runtime.diagnostics()


def _benchmark(args):
    if args.output.exists():
        raise ValueError("output must be fresh")
    if args.blocks < 1 or args.batch < 1:
        raise ValueError("blocks and batch must be positive")
    torch.set_num_threads(args.threads)
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    torch.set_float32_matmul_precision("highest")
    device = torch.device(args.device)
    if device.type == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("requested CUDA device unavailable")
    dtype = torch.complex128 if args.precision == "fp64" else torch.complex64
    generator = torch.Generator().manual_seed(args.seed)
    taps = args.taps
    prior = GaussianPrior(tuple([1/taps]*taps), args.coherence, args.frequency)
    # Public design Grams have rank one at every block, with an alternating
    # empty gap. This is a compute discriminator, not a physical capture.
    direction = torch.randn((args.blocks, taps), generator=generator, dtype=torch.float64)
    direction = torch.complex(direction, torch.randn(direction.shape, generator=generator, dtype=torch.float64))
    grams = direction[..., :, None] @ direction.conj()[..., None, :]
    grams[::args.gap_every] = 0
    dts = [0.] + [args.dt]*(args.blocks-1)
    correlations = torch.complex(torch.randn((args.blocks,args.batch,taps), generator=generator, dtype=torch.float64),
                                  torch.randn((args.blocks,args.batch,taps), generator=generator, dtype=torch.float64))*.1
    correlations[::args.gap_every] = 0
    started = time.perf_counter()
    design = prepare(grams, dts, prior, memory_limit_bytes=args.memory_mib*1024**2)
    prep_seconds = time.perf_counter()-started
    transfer_started = time.perf_counter()
    prepared_runtime = design.to(device, dtype, check_inputs=False)
    corr_device = correlations.to(device=device, dtype=dtype)
    reference_grams = grams.to(device=device, dtype=dtype)
    if device.type == "cuda": torch.cuda.synchronize(device)
    transfer_seconds = time.perf_counter()-transfer_started

    def run_reference():
        # Trial batching and input residency match the prepared implementation.
        state = GaussianStateFilter(prior, dtype=dtype, check_inputs=False)
        for i in range(args.blocks):
            state.update(corr_device[i], reference_grams[i], dts[i])
        return state.scores()

    def run_prepared():
        runtime = prepared_runtime
        runtime.reset()
        for i in range(args.blocks):
            runtime.update(corr_device[i])
        return runtime.scores()

    def sync():
        if device.type == "cuda":
            torch.cuda.synchronize(device)

    def timed(function):
        sync()
        start = time.perf_counter()
        scores = function()
        sync()
        return scores, time.perf_counter()-start

    # Cold values are each backend's first measured call after CUDA/context,
    # inputs, and prepared design transfers have completed. The order is
    # disclosed because first-call effects can still be order-sensitive.
    reference_cold_scores, reference_cold_seconds = timed(run_reference)
    prepared_cold_scores, prepared_cold_seconds = timed(run_prepared)

    # Warm both paths equally, alternating order to limit monotonic device/CPU
    # drift. No per-block synchronization is inserted into a sequence.
    warmup_seconds = {"reference": [], "prepared": []}
    for index in range(args.warmup):
        order = (("reference", run_reference), ("prepared", run_prepared))
        if index % 2:
            order = tuple(reversed(order))
        for name, function in order:
            _, elapsed = timed(function)
            warmup_seconds[name].append(elapsed)

    samples = {"reference": [], "prepared": []}
    latest = {}
    for index in range(args.repeats):
        order = (("reference", run_reference), ("prepared", run_prepared))
        if index % 2:
            order = tuple(reversed(order))
        for name, function in order:
            latest[name], elapsed = timed(function)
            samples[name].append(elapsed)

    verify_started = time.perf_counter()
    reference_scores = latest["reference"]
    prepared_scores = latest["prepared"]
    error = (reference_scores-prepared_scores).abs()
    max_error = float(error.max().cpu())
    max_reference = float(reference_scores.abs().max().cpu())
    finite = bool(torch.isfinite(prepared_scores).all().cpu())
    verification_seconds = time.perf_counter()-verify_started

    def summarize(values):
        ordered = sorted(values)
        return {"samples_seconds": values, "median_seconds": statistics.median(values),
                "minimum_seconds": min(values),
                "p95_seconds_nearest_rank": ordered[max(0, math.ceil(.95*len(ordered))-1)]}

    reference_summary, prepared_summary = summarize(samples["reference"]), summarize(samples["prepared"])
    reference_seconds = reference_summary["median_seconds"]
    prepared_seconds = prepared_summary["median_seconds"]
    # Keep exact host-to-device, design, recurrence, and verification stages
    # separate; the estimated total starts from public CPU tensors and does
    # not include synthetic data generation or framework startup.
    prepared_total = prep_seconds+transfer_seconds+prepared_seconds+verification_seconds
    result = {"schema":"triad-gaussian-prepared-benchmark-v1", "command_argv":sys.argv,
        "script_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "reference_module_sha256":hashlib.sha256(Path(__file__).with_name("triad_gaussian_state.py").read_bytes()).hexdigest(),
        "torch_version":torch.__version__, "device":str(device),
        "device_name":torch.cuda.get_device_name(device) if device.type=="cuda" else "CPU",
        "precision":args.precision, "tf32_allowed":False, "seed":args.seed,
        "split":"development-only synthetic public design/noise; not reception calibration",
        "workload":{"blocks":args.blocks,"batch":args.batch,"taps":taps,"rank":"one or zero with periodic empty gaps",
                    "elapsed_seconds":dts,"prior":{"coherence_seconds":args.coherence,"frequency_hz":args.frequency}},
        "stages_seconds":{"cpu_design_preparation":prep_seconds,"device_transfer_setup_common_inputs_and_design":transfer_seconds,
                           "reference_first_call_after_setup":reference_cold_seconds,
                           "prepared_first_call_after_setup":prepared_cold_seconds,
                           "reference_warm_resident_pipeline":reference_summary,
                           "prepared_warm_resident_pipeline":prepared_summary,
                           "reference_warmup_seconds":warmup_seconds["reference"],
                           "prepared_warmup_seconds":warmup_seconds["prepared"],
                           "score_verification":verification_seconds},
        "throughput":{"reference_sequences_per_second":args.batch/reference_seconds,
                       "prepared_sequences_per_second":args.batch/prepared_seconds,
                       "prepared_speedup_inference_only":reference_seconds/prepared_seconds,
                       "prepared_total_speedup_including_cpu_design_transfer_verification":
                           (transfer_seconds+reference_seconds)/(prepared_total),
                       "timing_basis":"medians of alternating-order synchronized host-wall sequence timings; no per-block event/sync"},
        "verification":{"max_absolute_score_error":max_error,"max_relative_to_reference":max_error/(max_reference+1e-300),
                         "finite":finite},
        "cost_note":"No cloud or cost estimate. Synthetic public Gram design and synthetic correlations; excludes waveform/export/simulation/RNG, source import/process startup and admission calibration. First-call timing occurs after common device/context and input transfer setup; compare it cautiously because order effects remain.",
        "diagnostics":prepared_runtime.diagnostics()}
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n")
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--blocks",type=int,default=64);parser.add_argument("--batch",type=int,default=16)
    parser.add_argument("--taps",type=int,choices=(1,2),default=2);parser.add_argument("--device",default="cpu")
    parser.add_argument("--precision",choices=("fp32","fp64"),default="fp32")
    parser.add_argument("--seed",type=int,default=88271);parser.add_argument("--dt",type=float,default=4.)
    parser.add_argument("--coherence",type=float,default=128.);parser.add_argument("--frequency",type=float,default=.003)
    parser.add_argument("--gap-every",dest="gap_every",type=int,default=11);parser.add_argument("--threads",type=int,default=1)
    parser.add_argument("--memory-mib",type=int,default=2048);parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--warmup",type=int,default=2);parser.add_argument("--repeats",type=int,default=5)
    args=parser.parse_args()
    if args.dt<0 or args.gap_every<1 or args.memory_mib<1 or args.coherence<=0 or not math.isfinite(args.frequency):
        parser.error("dt must be nonnegative; gap-every, memory and coherence must be positive")
    if args.warmup < 0 or args.repeats < 1:
        parser.error("warmup must be nonnegative and repeats must be positive")
    print(json.dumps(_benchmark(args),sort_keys=True))


if __name__=="__main__": main()
