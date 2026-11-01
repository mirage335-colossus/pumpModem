"""Optional positive-stencil implementation of the finite triad phase model.

The reference implementation and its public finite model are unchanged.  This
backend specializes integral advection, uses batched frequency matrix products,
and fuses the second advection with the *same* positive circular phase kernel.
It keeps the reference log-domain emission and FP64 evidence reduction.  CPU
execution is a mathematical implementation for paired checks, not a GPU fallback
or a GPU performance result.  CUDA requires Triton; unavailable support fails
explicitly rather than silently executing the reference on another device.
"""
from __future__ import annotations

from collections import OrderedDict
from functools import lru_cache
import math
import time

import torch

from triad_phase_frequency import PhaseFrequencyLikelihood


# Populated only by the CUDA path.  Keeping these globals allows Triton's source
# compiler to resolve the language symbols without importing it for CPU tests.
triton = None
tl = None


@lru_cache(maxsize=1)
def _cuda_stencil_kernel():
    global triton, tl
    try:
        import triton as triton_module
        import triton.language as language
    except ImportError as error:
        raise RuntimeError("optimized CUDA phase backend requires Triton; no fallback was executed") from error
    triton, tl = triton_module, language

    @triton.jit
    def stencil(source, index0, index1, fraction, output,
                PHASES: tl.constexpr, FREQUENCIES: tl.constexpr,
                OFFSETS: tl.constexpr, PROBABILITIES: tl.constexpr,
                ALIGNED: tl.constexpr, FP64: tl.constexpr,
                BLOCK: tl.constexpr):
        row = tl.program_id(0)
        frequency = row % FREQUENCIES
        phase = tl.arange(0, BLOCK)
        mask = phase < PHASES
        dtype = tl.float64 if FP64 else tl.float32
        result = tl.full((BLOCK,), 0., dtype)
        if not ALIGNED:
            part = tl.load(fraction + frequency).to(dtype)
        for k in tl.static_range(len(OFFSETS)):
            shifted = (phase - OFFSETS[k] + PHASES) % PHASES
            first = tl.load(index0 + frequency * PHASES + shifted, mask, other=0)
            value = tl.load(source + row * PHASES + first, mask, other=0).to(dtype)
            if not ALIGNED:
                second = tl.load(index1 + frequency * PHASES + shifted, mask, other=0)
                other = tl.load(source + row * PHASES + second, mask, other=0).to(dtype)
                value = value * (1. - part) + other * part
            probability = tl.full((), PROBABILITIES[k], dtype)
            result = result + value * probability
        tl.store(output + row * PHASES + phase, result, mask)

    return stencil


class OptimizedPhaseFrequencyLikelihood(PhaseFrequencyLikelihood):
    """Same observation/state API, with an explicitly experimental backend.

    No phase, frequency, gain or acquisition state is pruned.  Actual inference
    remains sequential over physical blocks.  CUDA kernels use ordinary FP32
    or FP64 products and accumulation; no tensor-core/reduced-input mode is
    enabled.  Library frequency multiplication retains the reference strict
    FP32 constructor guard.
    """

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._optimized_transition_preparations = 0
        self._optimized_stencil_calls = 0
        self._optimized_integral_advections = 0
        self._optimized_fractional_advections = 0
        self._optimized_maximum_support = 0
        self._optimized_cuda_ready = False
        if self.device.type not in ("cpu", "cuda"):
            raise ValueError("optimized phase backend supports only CPU checks or CUDA")
        if self.device.type == "cuda":
            _cuda_stencil_kernel()

    def _transition(self, dt):
        transition = super()._transition(dt)
        if "optimized_integral" not in transition:
            # One preparation read per cached dt, never one per block.  The
            # reference constructs all transitions in FP64 on the CPU first.
            transition["optimized_integral"] = bool((transition["fraction"] == 0).all().item())
            self._optimized_transition_preparations += 1
            self._optimized_maximum_support = max(self._optimized_maximum_support,
                                                  len(transition["phase_offsets"]))
        return transition

    def _advect(self, weights, transition):
        if transition["optimized_integral"]:
            shape = (1, 1, 1, self.frequency_states, self.phase_states)
            self._optimized_integral_advections += 1
            return torch.gather(weights, -1, transition["index0"].view(shape).expand_as(weights))
        self._optimized_fractional_advections += 1
        return super()._advect(weights, transition)

    def _cpu_second_advection_and_phase(self, weights, transition):
        """Direct positive indexed sum; CPU correctness path, not fused code."""
        result = torch.zeros_like(weights)
        phase = torch.arange(self.phase_states, device=self.device)
        shape = (1, 1, 1, self.frequency_states, self.phase_states)
        aligned = transition["optimized_integral"]
        fraction = transition["fraction"].view(1, 1, 1, self.frequency_states, 1)
        for offset, probability in zip(transition["phase_offsets"], transition["phase_probabilities"]):
            shifted = (phase-offset) % self.phase_states
            index0 = transition["index0"][:, shifted].view(shape).expand_as(weights)
            value = torch.gather(weights, -1, index0)
            if not aligned:
                index1 = transition["index1"][:, shifted].view(shape).expand_as(weights)
                value = value*(1-fraction)+torch.gather(weights, -1, index1)*fraction
            result.add_(value, alpha=probability)
        return result

    def _second_advection_and_phase(self, weights, transition):
        self._optimized_stencil_calls += 1
        if self.device.type == "cpu":
            return self._cpu_second_advection_and_phase(weights, transition)
        if weights.dtype not in (torch.float32, torch.float64):
            raise ValueError("optimized CUDA stencil requires FP32 or FP64")
        contiguous = weights.contiguous()
        output = torch.empty_like(contiguous)
        rows = contiguous.numel() // self.phase_states
        kernel = _cuda_stencil_kernel()
        kernel[(rows,)](contiguous, transition["index0"], transition["index1"],
                       transition["fraction"], output,
                       PHASES=self.phase_states, FREQUENCIES=self.frequency_states,
                       OFFSETS=transition["phase_offsets"],
                       PROBABILITIES=transition["phase_probabilities"],
                       ALIGNED=transition["optimized_integral"],
                       FP64=weights.dtype == torch.float64,
                       BLOCK=triton.next_power_of_2(self.phase_states),
                       num_warps=4 if self.phase_states <= 1024 else 8,
                       enable_fp_fusion=False)
        self._optimized_cuda_ready = True
        return output

    def _predict(self, weights, dt):
        if dt == 0:
            return weights
        if self.device.type == "cuda" and self.dtype == torch.float32:
            if torch.backends.cuda.matmul.allow_tf32 or torch.get_float32_matmul_precision() != "highest":
                raise ValueError("optimized strict FP32 requires TF32 disabled and highest matmul precision")
        transition = self._transition(dt)
        predicted = self._advect(weights, transition)
        predicted = torch.matmul(transition["frequency"].transpose(0, 1), predicted)
        predicted = self._second_advection_and_phase(predicted, transition)
        return predicted/predicted.sum(dim=(-3, -2, -1), keepdim=True)

    def diagnostics(self):
        result = super().diagnostics()
        result.update({
            "implementation": "experimental_positive_stencil_v1",
            "backend": "triton_cuda" if self.device.type == "cuda" else "torch_cpu_mathematical_check",
            "gpu_optimized_execution": self._optimized_cuda_ready,
            "fallback_executed": False,
            "frequency_contraction": "batched_T_transpose_matmul",
            "phase_transition": "same_positive_support_fused_with_second_advection",
            "emission_update": "reference_log_domain",
            "stencil_product_precision": str(self.dtype),
            "stencil_accumulation_precision": str(self.dtype),
            "cuda_stencil_fp_fusion_enabled": False if self.device.type == "cuda" else None,
            "triton_version": triton.__version__ if self.device.type == "cuda" else None,
            "posterior_normalizer_dtype": "torch.float64",
            "optimized_transition_preparations": self._optimized_transition_preparations,
            "optimized_stencil_calls": self._optimized_stencil_calls,
            "optimized_integral_first_advections": self._optimized_integral_advections,
            "optimized_fractional_first_advections": self._optimized_fractional_advections,
            "maximum_phase_stencil_support": self._optimized_maximum_support,
            "implementation_scope": "same finite state/model and exhaustive states; numerical equivalence and sensitivity require paired validation",
        })
        return result


class GraphPhaseFrequencyLikelihood(OptimizedPhaseFrequencyLikelihood):
    """CUDA-graph replay of the same fused, log-domain finite-state update.

    Graphs consume persistent r/G buffers and update persistent posterior and
    score storage.  Each distinct ``dt`` has its own bounded cached graph; no
    observation is replayed twice while warming or capturing.  ``reset`` is
    explicit and preserves the prepared graphs.  Calls must stay on the stream
    used at construction, and callers must not replace public state storage.
    This class deliberately has no CPU fallback.
    """

    def __init__(self, *args, **kwargs):
        device = torch.device(kwargs.get("device", "cpu"))
        if device.type != "cuda":
            raise RuntimeError("CUDA graph phase backend is unsupported on CPU; no fallback was executed")
        super().__init__(*args, **kwargs)
        self._graph_limit_bytes = int(kwargs.get("memory_limit_bytes", 4*1024**3))
        self._graph_max_entries = self.model.transition_cache_entries
        self._graph_cache = OrderedDict()
        self._graph_r = torch.empty((self.batch_size, self.bits, self.taps),
                                    dtype=self.complex_dtype, device=self.device)
        self._graph_g = torch.empty((self.batch_size, self.bits, self.taps, self.taps),
                                    dtype=self.complex_dtype, device=self.device)
        self._graph_initial_weights = self.weights.clone()
        self._graph_state_pointers = (self.weights.data_ptr(), self._scores.data_ptr())
        self._graph_stream = torch.cuda.current_stream(self.device)
        self._graph_execution_stream_id = self._graph_stream.cuda_stream
        self._graph_capture_seconds = 0.
        self._graph_warmup_seconds = 0.
        self._graph_captures = 0
        self._graph_replays = 0
        self._graph_evictions = 0
        self._graph_resets = 0
        self._graph_peak_estimated_bytes = self.working_bytes_bound

    def _check_graph_storage_and_stream(self):
        if (self.weights.data_ptr(), self._scores.data_ptr()) != self._graph_state_pointers:
            raise RuntimeError("CUDA graph state storage was replaced; construct a new receiver or use reset()")
        if torch.cuda.current_stream(self.device).cuda_stream != self._graph_execution_stream_id:
            raise RuntimeError("CUDA graph receiver updates/reset must use its construction stream")
        if self.dtype == torch.float32 and (torch.backends.cuda.matmul.allow_tf32 or
                torch.get_float32_matmul_precision() != "highest"):
            raise ValueError("CUDA graph strict FP32 requires TF32 disabled and highest matmul precision")

    def _graph_body(self, dt):
        # Same operation order as the reference update, with persistent output
        # copies instead of replacing state tensors.  Input checks happen once
        # outside replay, and no score clone is retained inside the graph.
        r, gram = self._graph_r, self._graph_g
        predicted = self._predict(self.weights, dt)
        correlation = torch.einsum("qk,nbk->nbq", self.gain_vectors.conj(), r)
        energy = torch.einsum("qk,nbkl,ql->nbq", self.gain_vectors.conj(), gram, self.gain_vectors).real
        emission = 2*(correlation.real[..., None]*self.phase_cosine+
                      correlation.imag[..., None]*self.phase_sine)-energy[..., None]
        logits = predicted.log()+emission[..., None, :]
        maximum = logits.amax(dim=(-3, -2, -1), keepdim=True)
        unnormalized = (logits-maximum).exp()
        total = unnormalized.sum(dim=(-3, -2, -1), keepdim=True, dtype=torch.float64)
        increment = maximum[..., 0, 0, 0].to(torch.float64)+total[..., 0, 0, 0].log()
        posterior = unnormalized/total.to(self.dtype)
        empty = (gram.abs().amax(dim=(-2, -1)) == 0) & (r.abs().amax(dim=-1) == 0)
        self.weights.copy_(torch.where(empty[..., None, None, None], predicted, posterior))
        self._scores.add_(torch.where(empty, torch.zeros_like(increment), increment))

    def _graph_memory_estimate(self, entries):
        # Each private graph pool can retain the reference's temporary-array
        # bound.  Keep one additional bound for live state/capture scratch,
        # plus persistent inputs and initial-prior storage.  CUDA allocator
        # caching/runtime bytes are reported by the caller, not asserted here.
        fixed = (self._graph_r.numel()*self._graph_r.element_size()+
                 self._graph_g.numel()*self._graph_g.element_size()+
                 self._graph_initial_weights.numel()*self._graph_initial_weights.element_size())
        return (entries+1)*self.working_bytes_bound+fixed

    def _prepare_graph(self, dt):
        existing = self._graph_cache.get(dt)
        if existing is not None:
            self._graph_cache.move_to_end(dt)
            return existing["graph"]
        # LRU eviction is rare preparation work and waits for owned stream work
        # before releasing a graph/pool.  It never resets inference state.
        if len(self._graph_cache) == self._graph_max_entries:
            self._graph_stream.synchronize()
            self._graph_cache.popitem(last=False)
            self._graph_evictions += 1
        estimate = self._graph_memory_estimate(len(self._graph_cache)+1)
        if estimate > self._graph_limit_bytes:
            raise MemoryError(f"CUDA graph cache estimate {estimate} bytes exceeds memory_limit_bytes; no graph was captured")
        self._graph_peak_estimated_bytes = max(self._graph_peak_estimated_bytes, estimate)
        transition = self._transition(dt) if dt else None
        # Graph preparation must be observationally inert.  The current input
        # and posterior can be arbitrary, including an empty block or a highly
        # concentrated posterior.  Restore the exact saved state after warmup
        # and capture, before the one intended replay below.
        saved_weights, saved_scores = self.weights.clone(), self._scores.clone()
        counter_names = ("_optimized_stencil_calls", "_optimized_integral_advections",
                         "_optimized_fractional_advections")
        saved_counters = {name: getattr(self, name) for name in counter_names}
        capture_stream = torch.cuda.Stream(device=self.device)
        capture_stream.wait_stream(self._graph_stream)
        graph = torch.cuda.CUDAGraph()
        try:
            started = time.perf_counter()
            with torch.cuda.stream(capture_stream):
                for _ in range(3):
                    self._graph_body(dt)
            capture_stream.synchronize()
            self._graph_warmup_seconds += time.perf_counter()-started
            # Restore on the capture stream, ordered before capture begins.
            with torch.cuda.stream(capture_stream):
                self.weights.copy_(saved_weights)
                self._scores.copy_(saved_scores)
            capture_stream.synchronize()
            started = time.perf_counter()
            with torch.cuda.graph(graph, stream=capture_stream):
                self._graph_body(dt)
            capture_stream.synchronize()
            self._graph_capture_seconds += time.perf_counter()-started
        finally:
            self._graph_stream.wait_stream(capture_stream)
            self.weights.copy_(saved_weights)
            self._scores.copy_(saved_scores)
            for name, value in saved_counters.items():
                setattr(self, name, value)
        # Retain transition tensor lifetimes even if the independent parent
        # transition cache evicts its dt.  The graph owns raw device addresses.
        self._graph_cache[dt] = {"graph": graph, "transition": transition,
                                 "capture_stream": capture_stream}
        self._graph_captures += 1
        return graph

    def update(self, r, gram, dt=0.):
        dt = float(dt)
        if not math.isfinite(dt) or dt < 0:
            raise ValueError("block-center separation must be finite and >= 0")
        self._check_graph_storage_and_stream()
        r, gram = self._inputs(r, gram)
        self._graph_r.copy_(r)
        self._graph_g.copy_(gram)
        graph = self._prepare_graph(dt)
        graph.replay()
        self._graph_replays += 1
        if dt:
            self._optimized_stencil_calls += 1
            transition = self._graph_cache[dt]["transition"]
            if transition["optimized_integral"]:
                self._optimized_integral_advections += 1
            else:
                self._optimized_fractional_advections += 1
        if self.validate_inputs and (not bool(torch.isfinite(self.weights).all()) or
                                     not bool(torch.isfinite(self._scores).all())):
            raise ValueError("CUDA graph phase inference became nonfinite")
        self.updates += 1
        self.elapsed_seconds += dt
        return self.scores()

    def reset(self):
        """Explicitly restore the initial prior; cached graphs stay prepared."""
        self._check_graph_storage_and_stream()
        self.weights.copy_(self._graph_initial_weights)
        self._scores.zero_()
        self.updates = 0
        self.elapsed_seconds = 0.
        self._graph_resets += 1

    def diagnostics(self):
        result = super().diagnostics()
        result.update({
            "implementation": "experimental_positive_stencil_cuda_graph_v1",
            "backend": "triton_cuda_graph",
            "graph_captures": self._graph_captures,
            "graph_replays": self._graph_replays,
            "graph_cache_entries": len(self._graph_cache),
            "graph_cache_maximum_entries": self._graph_max_entries,
            "graph_cached_dt_seconds": list(self._graph_cache),
            "graph_cache_evictions": self._graph_evictions,
            "graph_resets": self._graph_resets,
            "graph_warmup_synchronized_wall_seconds": self._graph_warmup_seconds,
            "graph_capture_synchronized_wall_seconds": self._graph_capture_seconds,
            "graph_peak_working_bytes_estimate": self._graph_peak_estimated_bytes,
            "graph_memory_scope": "conservative private-pool and state/temporary estimate; excludes allocator cache and CUDA runtime",
            "graph_state_policy": "persistent storage; preparation restores exact pre-call state; each update applies one replay; reset is explicit",
            "graph_stream_policy": "updates/reset require the construction CUDA stream",
        })
        return result
