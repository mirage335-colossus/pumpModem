"""Optional FP64 posterior reduction without a full-sized FP64 input copy.

The CUDA reducer loads FP32 or FP64 values, converts each loaded value to FP64,
performs both reduction levels in FP64 and writes small FP64 partials/output.
It changes reduction order, not accumulation precision. The graph subclass
retains every state, the existing log-domain update and FP64 log evidence.
CUDA support is required; the separately named CPU reference is not a fallback.
"""
from __future__ import annotations

from functools import lru_cache
import math

import torch

from triad_phase_optimized import GraphPhaseFrequencyLikelihood

triton = None
tl = None


@lru_cache(maxsize=1)
def _kernels():
    global triton, tl
    try:
        import triton as compiler
        import triton.language as language
    except ImportError as error:
        raise RuntimeError("FP64 fused reduction requires Triton; no fallback executed") from error
    triton, tl = compiler, language

    @triton.jit
    def partial_sum(source, partials, LENGTH: tl.constexpr, PARTS: tl.constexpr,
                    BLOCK: tl.constexpr):
        row, part = tl.program_id(0), tl.program_id(1)
        index = part*BLOCK+tl.arange(0, BLOCK)
        # Conversion remains in registers; no LENGTH-sized FP64 tensor is
        # written. Both the tree values and the partial result are FP64.
        values = tl.load(source+row*LENGTH+index, index < LENGTH, other=0).to(tl.float64)
        total = tl.sum(values, axis=0)
        tl.store(partials+row*PARTS+part, total)

    @triton.jit
    def final_sum(partials, output, PARTS: tl.constexpr, BLOCK: tl.constexpr):
        row = tl.program_id(0)
        index = tl.arange(0, BLOCK)
        values = tl.load(partials+row*PARTS+index, index < PARTS, other=0).to(tl.float64)
        tl.store(output+row, tl.sum(values, axis=0))

    return partial_sum, final_sum


def reduction_layout(shape, *, block_size=1024, input_dtype=torch.float32):
    """Shape/scratch and dtype-aware logical estimates, not hardware counters."""
    shape = tuple(shape)
    if len(shape) < 3 or any(isinstance(x, bool) or not isinstance(x, int) or x < 1 for x in shape):
        raise ValueError("positive integer shape with at least three dimensions required")
    if block_size not in (256, 512, 1024, 2048):
        raise ValueError("block_size must be256,512,1024 or2048")
    if input_dtype not in (torch.float32, torch.float64):
        raise ValueError("FP32 or FP64 input dtype required")
    conversion_copy = input_dtype == torch.float32
    rows, length = math.prod(shape[:-3]), math.prod(shape[-3:])
    parts = (length+block_size-1)//block_size
    if parts > 32768:
        raise ValueError("bounded FP64 final reduction supports at most32768 partials per row")
    return {"rows": rows, "values_per_row": length, "parts_per_row": parts,
            "block_size": block_size, "scratch_fp64_bytes": rows*parts*8,
            "output_fp64_bytes": rows*8,
            "input_dtype": str(input_dtype),
            "baseline_full_fp64_conversion_copy_expected": conversion_copy,
            "avoided_full_fp64_intermediate_bytes": rows*length*8 if conversion_copy else 0,
            "logical_intermediate_write_and_read_bytes_avoided": rows*length*16 if conversion_copy else 0,
            "byte_accounting_scope": "gross logical intermediate-byte estimates; excludes new partial traffic and is not measured DRAM/cache traffic",
            "final_shape": shape[:-3]+(1, 1, 1)}


def chunked_cpu_reference(values, *, block_size=1024):
    """FP64 two-level mathematical reference for tests; never selected by CUDA."""
    if values.device.type != "cpu" or values.dtype not in (torch.float32, torch.float64):
        raise ValueError("CPU FP32/FP64 reference input required")
    layout = reduction_layout(values.shape, block_size=block_size, input_dtype=values.dtype)
    flat = values.reshape(layout["rows"], layout["values_per_row"])
    partials = [flat[:, first:first+block_size].sum(-1, dtype=torch.float64)
                for first in range(0, layout["values_per_row"], block_size)]
    return torch.stack(partials, -1).sum(-1, dtype=torch.float64).reshape(layout["final_shape"])


def sum_last_three_fp64(values, *, block_size=1024):
    """Native CUDA FP64 accumulation with no implicit layout/device fallback."""
    if values.device.type != "cuda":
        raise RuntimeError("native FP64 reduction requires CUDA; CPU fallback is prohibited")
    if values.dtype not in (torch.float32, torch.float64) or not values.is_contiguous():
        raise ValueError("contiguous FP32/FP64 CUDA input required")
    layout = reduction_layout(values.shape, block_size=block_size, input_dtype=values.dtype)
    first, last = _kernels()
    partials = torch.empty((layout["rows"], layout["parts_per_row"]),
                           dtype=torch.float64, device=values.device)
    output = torch.empty(layout["final_shape"], dtype=torch.float64, device=values.device)
    first[(layout["rows"], layout["parts_per_row"])](
        values, partials, LENGTH=layout["values_per_row"], PARTS=layout["parts_per_row"],
        BLOCK=block_size, num_warps=4, enable_fp_fusion=False)
    last[(layout["rows"],)](partials, output, PARTS=layout["parts_per_row"],
        BLOCK=triton.next_power_of_2(layout["parts_per_row"]),
        num_warps=4, enable_fp_fusion=False)
    return output


class Fp64ReductionGraphLikelihood(GraphPhaseFrequencyLikelihood):
    """Experimental graph variant retaining FP64 posterior accumulation."""

    def __init__(self, *args, reduction_block_size=1024, **kwargs):
        self.reduction_block_size = reduction_block_size
        super().__init__(*args, **kwargs)
        self._reduction_layout = reduction_layout(self.weights.shape,
            block_size=reduction_block_size, input_dtype=self.dtype)
        _kernels()

    def _graph_body(self, dt):
        # Deliberately mirror the existing graph update: only the total's
        # implementation changes. Reference and original graph remain intact.
        r, gram = self._graph_r, self._graph_g
        predicted = self._predict(self.weights, dt)
        correlation = torch.einsum("qk,nbk->nbq", self.gain_vectors.conj(), r)
        energy = torch.einsum("qk,nbkl,ql->nbq", self.gain_vectors.conj(), gram,
                              self.gain_vectors).real
        emission = 2*(correlation.real[..., None]*self.phase_cosine+
                      correlation.imag[..., None]*self.phase_sine)-energy[..., None]
        logits = predicted.log()+emission[..., None, :]
        maximum = logits.amax(dim=(-3, -2, -1), keepdim=True)
        unnormalized = (logits-maximum).exp()
        total = sum_last_three_fp64(unnormalized, block_size=self.reduction_block_size)
        increment = maximum[..., 0, 0, 0].to(torch.float64)+total[..., 0, 0, 0].log()
        posterior = unnormalized/total.to(self.dtype)
        empty = (gram.abs().amax(dim=(-2, -1)) == 0) & (r.abs().amax(dim=-1) == 0)
        self.weights.copy_(torch.where(empty[..., None, None, None], predicted, posterior))
        self._scores.add_(torch.where(empty, torch.zeros_like(increment), increment))

    def diagnostics(self):
        result = super().diagnostics()
        result.update({"implementation": "experimental_graph_fp64_register_reduction_v1",
            "posterior_normalizer_input_dtype": str(self.dtype),
            "posterior_normalizer_accumulation_dtype": "float64 at both levels",
            "posterior_normalizer_partial_storage_dtype": "float64",
            "posterior_normalizer_full_input_fp64_copy": False,
            "posterior_normalizer_reduction_order": "chunked pairwise tree then FP64 partial tree; differs from torch.sum",
            "posterior_normalizer_layout": self._reduction_layout,
            "normalizer_hardware_counter_or_isa_verification": False})
        return result


def main():
    """Paired native reducer and optional full exported-hour graph screen."""
    import argparse
    import hashlib
    import json
    from pathlib import Path
    import statistics
    import sys
    import time

    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("input", type=Path, nargs="?")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--device", default="cuda:0")
    p.add_argument("--batch", type=int, default=8)
    p.add_argument("--precision", choices=("fp32", "fp64"), default="fp32")
    p.add_argument("--repeats", type=int, default=3)
    p.add_argument("--block-size", type=int, choices=(256,512,1024,2048), default=1024)
    p.add_argument("--max-blocks", type=int, default=0)
    p.add_argument("--order", choices=("reference-first", "variant-first"), default="reference-first")
    p.add_argument("--cn0", type=float, default=-18.)
    p.add_argument("--seed", type=int, default=771319)
    p.add_argument("--memory-gib", type=float, default=12.)
    a = p.parse_args()
    if (a.output.exists() or not 1 <= a.batch <= 64 or not 1 <= a.repeats <= 10 or
            a.max_blocks < 0 or not math.isfinite(a.cn0) or abs(a.cn0) > 100 or
            not math.isfinite(a.memory_gib) or a.memory_gib <= 0):
        p.error("fresh output, bounded positive geometry/resource settings and finite C/N0 required")
    device = torch.device(a.device)
    if device.type != "cuda" or not torch.cuda.is_available():
        p.error("native CUDA required; no CPU fallback")
    started = time.perf_counter()
    torch.set_num_threads(2)
    torch.set_float32_matmul_precision("highest")
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    torch.cuda.set_device(device)
    torch.empty(1, device=device)
    torch.cuda.synchronize(device)
    context_seconds = time.perf_counter()-started
    dtype = torch.float32 if a.precision == "fp32" else torch.float64
    complex_dtype = torch.complex64 if a.precision == "fp32" else torch.complex128

    def measured(fn):
        torch.cuda.synchronize(device)
        first, last = torch.cuda.Event(enable_timing=True), torch.cuda.Event(enable_timing=True)
        tick = time.perf_counter()
        first.record()
        value = fn()
        last.record()
        last.synchronize()
        return value, {"wall_seconds":time.perf_counter()-tick,
                       "cuda_event_seconds":first.elapsed_time(last)/1000}

    # Actual phase-state shape; this is a numerical reduction benchmark only.
    layout = reduction_layout((a.batch,2,24,43,512), block_size=a.block_size, input_dtype=dtype)
    generator = torch.Generator().manual_seed(a.seed)
    if (layout["rows"]*layout["values_per_row"]*8+layout["scratch_fp64_bytes"] >
            .25*torch.cuda.mem_get_info(device)[0]):
        raise MemoryError("reducer benchmark would exceed25%free VRAM")
    value = torch.rand((a.batch,2,24,43,512), generator=generator, dtype=dtype).to(device)
    operations = {"torch_sum_fp64":lambda:value.sum((-3,-2,-1),keepdim=True,dtype=torch.float64),
                  "register_fp64_sum":lambda:sum_last_three_fp64(value,block_size=a.block_size)}
    for fn in operations.values():
        fn()
    torch.cuda.synchronize(device)
    reduction_times = {name:[] for name in operations}
    totals = {}
    for repeat in range(max(5,a.repeats)):
        order = list(operations) if repeat%2 == 0 else list(reversed(operations))
        for name in order:
            totals[name], elapsed = measured(operations[name])
            reduction_times[name].append(elapsed)
    reduction_error = float((totals["torch_sum_fp64"]-totals["register_fp64_sum"]).abs().max())
    if not torch.allclose(totals["torch_sum_fp64"],totals["register_fp64_sum"],rtol=4e-15,atol=2e-11):
        raise AssertionError("native FP64 normalizers differ beyond paired numerical tolerance")
    reducer_report = {"layout":layout,"timings":reduction_times,
        "maximum_absolute_error":reduction_error,
        "median_wall_speedup":statistics.median(t["wall_seconds"] for t in reduction_times["torch_sum_fp64"])/
            statistics.median(t["wall_seconds"] for t in reduction_times["register_fp64_sum"]),
        "scope":"standalone resident positive-input reduction; not receiver or end-to-end throughput"}
    del value, totals
    full = None
    input_hashes = None
    if a.input is not None:
        from triad_prepared_observation import PreparedGaussianObservation
        from triad_state_workspace import BlockWorkspace
        from triad_phase_frequency import PhaseFrequencyModel
        tick = time.perf_counter()
        w = BlockWorkspace(a.input)
        stop = w.block_count if a.max_blocks == 0 else min(a.max_blocks,w.block_count)
        selected = [i for i,m in enumerate(w.column_mapping) if len(m)==2]
        labels = torch.tensor([w.candidates[i]["bit"] for i in selected])
        if set(labels.tolist()) != {0,1}:
            raise ValueError("complete selected two-tap bank with both bits required")
        import numpy as np
        mapping = np.asarray([w.column_mapping[i] for i in selected])
        if stop*a.batch*len(selected)*2*2*16 > 512*1024**2:
            raise MemoryError("paired exported statistic buffers exceed512MiB")
        prepared = PreparedGaussianObservation(w,seed=a.seed,batch=a.batch,cn0_db_hz=a.cn0,stop=stop)
        host = [[],[]]
        for pair in prepared.iter_paired():
            for kind, block in enumerate((pair.null,pair.signal)):
                rr = np.ascontiguousarray(block.r[:,mapping])
                gg = np.ascontiguousarray(block.gram[mapping[:,:,None],mapping[:,None,:]])
                host[kind].append((rr,gg,block.dt_seconds))
        preparation_seconds = time.perf_counter()-tick

        def transfer():
            return [[(torch.as_tensor(r,device=device,dtype=complex_dtype),
                      torch.as_tensor(g,device=device,dtype=complex_dtype),dt) for r,g,dt in rows]
                    for rows in host]
        inputs, transfer_time = measured(transfer)
        model = PhaseFrequencyModel(phase_states=512,relative_phase_states=8,
            nominal_dt_seconds=w.block_samples/w.sample_rate,
            phase_diffusion_rad_per_sqrt_second=.00872665)
        setup = {}
        # Each class owns its conservative graph-cache guard. Test both
        # receivers serially; release the original before creating the variant.
        outputs = {}
        rows = {}
        organizations = [("graph",GraphPhaseFrequencyLikelihood),
                         ("graph_fp64_register_reduction",Fp64ReductionGraphLikelihood)]
        if a.order == "variant-first":
            organizations.reverse()
        for name,cls in organizations:
            tick = time.perf_counter()
            receiver = cls(a.batch,len(selected),2,model,device=device,dtype=dtype,
                memory_limit_bytes=int(a.memory_gib*1024**3),validate_inputs=False,
                **({"reduction_block_size":a.block_size} if cls is Fp64ReductionGraphLikelihood else {}))
            seen = set()
            for r,g,dt in inputs[0]:
                if dt not in seen:
                    receiver.update(r,g,dt)
                    seen.add(dt)
            receiver.reset()
            torch.cuda.synchronize(device)
            setup[name] = time.perf_counter()-tick
            times = []
            score = []
            reset_time = []
            score_copy = []
            for repeat in range(a.repeats):
                this_score = []
                elapsed = []
                for kind in (0,1):
                    _, reset = measured(receiver.reset)
                    reset_time.append(reset["wall_seconds"])
                    def update_all():
                        for r,g,dt in inputs[kind]:receiver.update(r,g,dt)
                    _, infer = measured(update_all)
                    elapsed.append(infer)
                    result, copied = measured(lambda:receiver.scores().cpu().clone())
                    score_copy.append(copied["wall_seconds"])
                    this_score.append(result)
                times.append(elapsed)
                score = torch.stack(this_score)
            outputs[name] = score
            rows[name] = {"setup_capture_seconds":setup[name],"repeated_null_signal_inference":times,
                "reset_wall_seconds":reset_time,"score_d2h_wall_seconds":score_copy,
                "median_pair_inference_wall_seconds":statistics.median(sum(t["wall_seconds"] for t in pair) for pair in times),
                "diagnostics":receiver.diagnostics()}
            del receiver
            torch.cuda.synchronize(device)
        ref,actual = outputs["graph"],outputs["graph_fp64_register_reduction"]
        if not bool(torch.isfinite(ref).all()) or not bool(torch.isfinite(actual).all()):
            raise AssertionError("nonfinite paired graph score")
        error = float((ref-actual).abs().max())
        if error > (5e-4 if dtype==torch.float32 else 1e-9):
            raise AssertionError(f"full paired graph score error{error}exceeds numerical guard")
        decision = {}
        bits = []
        for scores in (ref,actual):
            bits.append(torch.stack([torch.logsumexp(scores[...,labels==b],-1)-
                                     math.log(int((labels==b).sum())) for b in (0,1)],-1))
        for name,threshold in (("default",math.log(2880/1e-4)),("relaxed",math.log(1440/.01))):
            decision[name] = {"threshold_nats":threshold,
                "admission_differences":int(((bits[0].max(-1).values>=threshold)!=
                                             (bits[1].max(-1).values>=threshold)).sum())}
        full = {"scored_blocks":stop,"full_blocks":w.block_count,"batch":a.batch,
            "physical_capture_seconds":w.sample_count/w.sample_rate,
            "independent_noise_trials_per_kind":a.batch,"timing_repeats_not_additional_trials":a.repeats,
            "candidate_labels":labels.tolist(),"same_represented_input_for_both":True,
            "prepared_gaussian_diagnostics":prepared.diagnostics(),"cpu_preparation_seconds":preparation_seconds,
            "shared_h2d":transfer_time,"organizations":rows,"maximum_absolute_log_lr_error":error,
            "candidate_rank_differences":int((ref.argmax(-1)!=actual.argmax(-1)).sum()),
            "bit_rank_differences":int((bits[0].argmax(-1)!=bits[1].argmax(-1)).sum()),
            "decisions":decision,
            "resident_pair_inference_speedup":rows["graph"]["median_pair_inference_wall_seconds"]/
                rows["graph_fp64_register_reduction"]["median_pair_inference_wall_seconds"],
            "operationally_qualified":False,
            "scope":"known fixed exported physical design, same selected candidates/nuisance model and paired Gaussian observations; source export and full acquisition excluded"}
        files = [w.manifest_path]+[a.input/name for name in sorted(w.manifest["file_sizes"])]
        input_hashes = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    source = [Path(__file__),Path(__file__).with_name("triad_phase_optimized.py"),
              Path(__file__).with_name("triad_phase_frequency.py")]
    if a.input is not None:
        source += [Path(__file__).with_name("triad_prepared_observation.py"),
                   Path(__file__).with_name("triad_state_workspace.py")]
    report = {"schema":"triad-fp64-reduction-screen-v1","status":"numerical_screening_only",
        "seed_scope":"development numerical screening; prepared simulator derives per-block RNG from [root_seed,block_index]; timing repeats reuse one paired observation",
        "command_argv":sys.argv,"configuration":{k:str(v) if isinstance(v,Path) else v for k,v in vars(a).items()},
        "environment":{"torch":torch.__version__,"cuda":torch.version.cuda,"device":str(device),
                       "device_name":torch.cuda.get_device_name(device),"tf32_allowed":False,
                       "triton":triton.__version__},
        "source_sha256":{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in source},
        "input_sha256":input_hashes,"cuda_context_initialization_seconds":context_seconds,
        "reducer":reducer_report,"full_graph_comparison":full,
        "complete_driver_wall_seconds_before_json":time.perf_counter()-started,
        "limitations":["FP64 reduction order differs from torch.sum; no universal roundoff certificate.",
            "Global-byte savings are operation-count estimates, not measured DRAM/cache traffic.",
            "No SM/occupancy/tensor/DRAM hardware counter or ISA claim.",
            "No sensitivity-loss, independent-channel or tiny operational false-admission qualification."]}
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2,allow_nan=False)+"\n")
    print(json.dumps({"output":str(a.output),"reducer_speedup":reducer_report["median_wall_speedup"],
                      "full_graph_comparison":None if full is None else
                          {k:full[k] for k in ("resident_pair_inference_speedup","maximum_absolute_log_lr_error")}}))


if __name__=="__main__":
    with torch.inference_mode():main()
