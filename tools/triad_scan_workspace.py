"""Opt-in fixed-bank whole-capture CUDA graph workspace for triad studies.

The workspace exclusively owns its supplied bank and grouped receiver. Keys,
epoch/clock-derived templates, variance, grids, shapes and model stay fixed.
Every replay validates and copies new sampled IQ, resets the full posterior,
and recomputes every input-dependent window, FP4 pack and likelihood update.
This is not arbitrary-window streaming or changing-key preparation caching.
"""
from dataclasses import asdict, is_dataclass
import hashlib
import json
import math
import time

import numpy as np
import torch

from triad_exhaustive_correlation import observation_rows
from triad_correlation_fusion import reconstruct_correlations, reconstruction_reference


def validate_observation(observation, shape, dtype, *, fp32_input):
    """Validate fresh IQ outside capture; no data-dependent host query in graph."""
    if tuple(observation.shape) != tuple(shape):
        raise ValueError("observation shape changed; rebuild the workspace")
    if isinstance(observation, torch.Tensor):
        if observation.dtype != dtype or not observation.is_complex():
            raise ValueError("observation dtype changed; rebuild the workspace")
        finite = bool(torch.isfinite(observation).all())
        in_range = not fp32_input or not bool((observation.abs() > torch.finfo(torch.float32).max).any())
    else:
        value = np.asarray(observation)
        expected = np.complex128 if dtype == torch.complex128 else np.complex64
        if value.dtype != expected:
            raise ValueError("observation dtype changed; rebuild the workspace")
        finite = bool(np.isfinite(value).all())
        in_range = not fp32_input or not bool((np.abs(value) > np.finfo(np.float32).max).any())
    if not finite or not in_range:
        raise ValueError("fresh observation must be finite and representable at product input")


def preparation_estimate(bank, grouped, shape, dtype):
    """Conservative bounds before allocating schedule or capture buffers.

    The capture estimate charges two complete receiver workspace bounds for
    EVERY block, without assuming graph allocator reuse. Actual pool peaks are
    also checked. Driver/library allocations are separately visible in free
    device memory, outside Torch allocator accounting.
    """
    batch, extent = shape
    columns = batch*len(bank.lags)
    scalar_bytes = 16 if dtype == torch.complex128 else 8
    schedule = 0  # observation windows are never retained
    for first in range(0, bank.samples, bank.block_samples):
        length = min(bank.block_samples, bank.samples-first)
        half = math.ceil(length/bank._half_alignment)*bank._half_alignment
        k = 2*half
        rows = 2*len(bank.frequencies)*bank.candidates*bank.taps
        if bank.backend is None:
            a_bytes = rows*k*(8 if bank.precision == 'fp64' else 4)
        else:
            bk = bank.backend.block_k
            kp = math.ceil(k/bk)*bk
            a_bytes = rows*(kp//2+kp//bank._half_alignment+20)
        schedule += a_bytes+columns*length*8+bank.candidates*bank.taps**2*16
    geometry = columns*(40+16*len(bank.frequencies))
    length=min(bank.block_samples,bank.samples)
    half=math.ceil(length/bank._half_alignment)*bank._half_alignment
    ft=min(bank.frequency_tile,len(bank.frequencies));ct=min(bank.candidate_tile,bank.candidates)
    a_staging=(64*ft*ct*bank.taps*half+16*ct*bank.taps*length+
               16*ft*length+8*length+8*ft)
    state_bound = sum(receiver.working_bytes_bound for _,_,receiver,_ in grouped.groups)
    transition_bound = bank.block_count*sum(receiver.frequency_states**2*16+
        receiver.frequency_states*receiver.phase_states*24+receiver.phase_states*32
        for _,_,receiver,_ in grouped.groups)
    output = columns*len(bank.frequencies)*bank.candidates*bank.taps*16
    per_block = 2*state_bound+6*output+columns*half*64
    return dict(host_bytes=bank._basis.nbytes+transition_bound+bank.block_count*2048+
                    64*bank.candidates*length*bank.taps,
                schedule_bytes=schedule+geometry+transition_bound,
                exact_transition_residency_bound_bytes=transition_bound,
                template_preparation_transient_bound_bytes=a_staging,
                capture_transient_bound_bytes=bank.block_count*per_block,
                device_total_bound_bytes=schedule+geometry+transition_bound+a_staging+bank.block_count*per_block+
                    batch*extent*scalar_bytes+2*state_bound,
                estimate_scope='Conservative owned schedule plus two receiver bounds and six correlation outputs per block; excludes pre-existing unrelated CUDA allocations.')


class WholeScanWorkspace:
    """Fixed-signature exclusive workspace; no CPU/native-arithmetic fallback.

    Owned graph preparation uses normal no-grad tensors, including when called
    under ambient inference_mode. Mutable receiver buffers must have been
    created outside inference_mode. Use a fresh process if an earlier external
    inference-mode capture created incompatible shared CUDA-generator state;
    this wrapper never replaces or reseeds process-global generators.
    """
    @torch.inference_mode(False)
    @torch.no_grad()
    def __init__(self, bank, grouped, observation_shape, *, noise_variance,
                 observation_dtype=torch.complex128, time_origin_seconds=0.,
                 memory_limit_bytes=32*1024**3, host_memory_limit_bytes=256*1024**2,
                 identity=None):
        if bank.device.type != 'cuda' or not torch.cuda.is_available():
            raise RuntimeError('whole-scan workspace requires CUDA; no CPU fallback')
        if observation_dtype not in (torch.complex64, torch.complex128):
            raise ValueError('whole-scan input must be complex64 or complex128')
        if len(observation_shape) != 2 or min(observation_shape) < 1:
            raise ValueError('invalid observation shape')
        if bank.samples+int(bank.lags.max()) > observation_shape[1]:
            raise ValueError('complete shifted template support leaves capture')
        variance = float(noise_variance)
        origin = float(time_origin_seconds)
        if not math.isfinite(variance) or variance <= 0 or not math.isfinite(origin):
            raise ValueError('invalid variance or time origin')
        if bank.precision == 'diagnostic-fp64':
            raise ValueError('CPU diagnostic products cannot be CUDA captured')
        expected_batch = observation_shape[0]*len(bank.frequencies)*len(bank.lags)
        resolved_device=torch.device('cuda',torch.cuda.current_device() if bank.device.index is None else bank.device.index)
        if grouped.batch != expected_batch or grouped.count != bank.candidates:
            raise ValueError('grouped receiver and exhaustive bank shapes disagree')
        all_indices=[]
        for indices,taps,receiver,initial in grouped.groups:
            if not hasattr(receiver, '_graph_body') or receiver.updates:
                raise ValueError('whole-scan capture requires a fresh graph-capable receiver')
            if any(torch.is_inference(value) for value in (receiver.weights,receiver._scores,
                    receiver._graph_r,receiver._graph_g,receiver._graph_initial_weights,initial)):
                raise ValueError('mutable receiver/prior tensors were created in inference_mode; recreate receiver outside that context')
            if indices.ndim!=1 or indices.dtype!=torch.int64 or not indices.numel():
                raise ValueError('each candidate group needs nonempty fixed int64 indices')
            all_indices.extend(indices.cpu().tolist())
            if (receiver.batch_size!=expected_batch or receiver.bits!=indices.numel() or
                    receiver.taps!=taps or initial.shape!=receiver.weights.shape or
                    receiver.weights.device!=resolved_device or indices.device!=resolved_device or
                    initial.device!=resolved_device or initial.dtype!=receiver.weights.dtype):
                raise ValueError('candidate group receiver shapes/device disagree')
        if sorted(all_indices)!=list(range(bank.candidates)):
            raise ValueError('candidate groups must cover every candidate exactly once')
        self.bank, self.grouped = bank, grouped
        self.shape, self.dtype = tuple(observation_shape), observation_dtype
        self.variance, self.origin = variance, origin
        self.device = resolved_device
        self.memory_limit_bytes = int(memory_limit_bytes)
        self.estimate = preparation_estimate(bank, grouped, self.shape, self.dtype)
        if self.estimate['host_bytes'] > host_memory_limit_bytes:
            raise MemoryError('whole-scan host preparation estimate exceeds limit')
        if self.estimate['device_total_bound_bytes'] > self.memory_limit_bytes:
            raise MemoryError('whole-scan conservative device estimate exceeds limit')
        free, total = torch.cuda.mem_get_info(self.device)
        if self.estimate['device_total_bound_bytes'] > free:
            raise MemoryError('whole-scan conservative estimate exceeds currently free device memory')
        self._backend_identity = self._backend_signature()
        self.signature = dict(public_basis_sha256=hashlib.sha256(bank._basis.tobytes()).hexdigest(),
            frequencies_hz=bank.frequencies.tolist(),lags=bank.lags.tolist(),sample_rate=bank.sample_rate,
            block_samples=bank.block_samples,shape=list(self.shape),dtype=str(self.dtype),
            variance=variance,time_origin_seconds=origin,precision=bank.precision,
            fused_reconstruction=bank.fused_reconstruction,
            backend=self._backend_identity,device=str(self.device),
            caller_identity=identity or {},groups=[dict(taps=taps,candidate_indices=indices.cpu().tolist(),
                bulk_dtype=str(r.dtype),receiver_type=type(r).__module__+'.'+type(r).__name__,
                reduction_block_size=getattr(r,'reduction_block_size',None),
                model=asdict(r.model) if is_dataclass(r.model) else repr(r.model))
                for indices,taps,r,_ in grouped.groups])
        self.signature_sha256 = hashlib.sha256(json.dumps(self.signature,sort_keys=True).encode()).hexdigest()
        self._construction_stream = torch.cuda.current_stream(self.device)
        self._capture_stream = torch.cuda.Stream(device=self.device)
        self.schedule = []
        self._transitions={id(receiver):{} for _,_,receiver,_ in grouped.groups}
        self.graph = None
        self.replays = 0
        self.prepared = False
        started = time.perf_counter()
        torch.cuda.reset_peak_memory_stats(self.device)
        self.observation = torch.empty(self.shape,dtype=self.dtype,device=self.device)
        self.observation.zero_()
        self.reciprocal_variance = torch.full((),1/variance,dtype=torch.float64,device=self.device)
        self.output = torch.empty((expected_batch,bank.candidates),dtype=torch.float64,device=self.device)
        self._initial = [(r,initial) for _,_,r,initial in grouped.groups]
        self._state_references=[tensor for _,_,r,_ in grouped.groups for tensor in
                                (r.weights,r._scores,r._graph_r,r._graph_g)]
        self._state_pointers=tuple(tensor.data_ptr() for tensor in self._state_references)
        try:
            self._prepare_schedule()
            self._capture_stream.wait_stream(self._construction_stream)
            self.graph = torch.cuda.CUDAGraph()
            with torch.cuda.stream(self._capture_stream):
                # Compile every shape and initialize BLAS handles outside capture.
                self._body()
            self._capture_stream.synchronize()
            with torch.cuda.graph(self.graph,stream=self._capture_stream):
                self._body()
            self._capture_stream.synchronize()
            self.actual_peak_allocated_bytes = torch.cuda.max_memory_allocated(self.device)
            self.actual_peak_reserved_bytes = torch.cuda.max_memory_reserved(self.device)
            if self.actual_peak_allocated_bytes > self.memory_limit_bytes or self.actual_peak_reserved_bytes > self.memory_limit_bytes:
                raise MemoryError('actual CUDA allocator capture peak exceeds workspace limit')
            self.prepared = True
        except Exception:
            self.close()
            raise
        finally:
            self._construction_stream.wait_stream(self._capture_stream)
            self._reset_state()
        self.preparation_seconds = time.perf_counter()-started

    def _backend_signature(self):
        backend = self.bank.backend
        return None if backend is None else dict(type=type(backend).__module__+'.'+type(backend).__name__,
            **{name:getattr(backend,name,None) for name in ('format','block_m','block_n','block_k','num_warps','num_stages')})

    def _prepare_schedule(self):
        bank = self.bank
        columns = self.shape[0]*len(bank.lags)
        # Own these references once, independently of the bank's bounded LRU.
        geometries=[bank._column_geometry(columns,n0,min(columns,n0+bank.column_tile),self.origin)
                    for n0 in range(0,columns,bank.column_tile)]
        for block in range(bank.block_count):
            first,stop = block*bank.block_samples,min((block+1)*bank.block_samples,bank.samples)
            center=(first+stop)/(2*bank.sample_rate)
            previous=(max(0,first-bank.block_samples)+first)/(2*bank.sample_rate)
            dt=0. if block==0 else center-previous
            for _,_,receiver,_ in self.grouped.groups:
                if dt and dt not in self._transitions[id(receiver)]:
                    self._transitions[id(receiver)][dt]=receiver._transition(dt)
            gram=bank._gram(first,stop,self.variance)
            # A must not be duplicated if a tiny bank LRU evicts between N tiles.
            apacks=[]
            for f0 in range(0,len(bank.frequencies),bank.frequency_tile):
                f1=min(f0+bank.frequency_tile,len(bank.frequencies))
                for c0 in range(0,bank.candidates,bank.candidate_tile):
                    c1=min(c0+bank.candidate_tile,bank.candidates)
                    apacks.append((f0,f1,c0,c1,bank._template_pack(block,first,stop,f0,f1,c0,c1)))
            tiles=[]
            for geometry in geometries:
                col,trials,lag_indices,lags,lag_seconds,residual=geometry
                sample_indices=lags[:,None]+torch.arange(first,stop,device=self.device)[None,:]
                tiles.append((trials,lag_indices,residual,sample_indices,apacks))
            self.schedule.append((dt,gram,tiles))
        self.signature['exact_dt_hex']=[dt.hex() for dt,_,_ in self.schedule]
        self.signature_sha256=hashlib.sha256(json.dumps(self.signature,sort_keys=True).encode()).hexdigest()

    def _reset_state(self):
        for receiver,initial in self._initial:
            receiver.weights.copy_(initial)
            receiver._scores.zero_()

    def _body(self):
        bank=self.bank
        self._reset_state()
        for dt,gram,tiles in self.schedule:
            dense=torch.empty((self.shape[0],len(bank.frequencies),bank.candidates,len(bank.lags),bank.taps),
                              dtype=torch.complex128,device=self.device)
            destination=dense.permute(0,3,1,2,4)
            for trials,lag_indices,residual,sample_indices,apacks in tiles:
                windows=self.observation[trials[:,None],sample_indices]
                rows=observation_rows(windows,half_alignment=bank._half_alignment).to(bank._dtype).contiguous()
                bpack=bank._pack(rows)  # ALWAYS recomputed from current captured input.
                for f0,f1,c0,c1,apack in apacks:
                    product=apack@bpack.T if bank.backend is None else bank.backend.matmul(apack,bpack)
                    if bank.fused_reconstruction:
                        value=reconstruct_correlations(product,residual[f0:f1],self.reciprocal_variance,
                                                       candidates=c1-c0,taps=bank.taps)
                    else:
                        value=reconstruction_reference(product,residual[f0:f1],self.reciprocal_variance,
                                                       candidates=c1-c0,taps=bank.taps)
                    destination[trials,lag_indices,f0:f1,c0:c1]=value
            r=dense.permute(0,1,3,2,4).reshape(self.grouped.batch,bank.candidates,bank.taps)
            for indices,taps,receiver,_ in self.grouped.groups:
                if dt:
                    # Exact keys may exceed the ordinary per-block LRU (e.g.
                    # nonbinary Fs). Install the already-owned transition for
                    # this block. Never round dt or allocate on a cache miss
                    # during capture. Python bookkeeping runs only at prepare.
                    receiver._transition_cache.clear()
                    receiver._transition_cache[dt]=self._transitions[id(receiver)][dt]
                receiver._graph_r.copy_(r[:,indices,:taps].to(receiver.complex_dtype))
                receiver._graph_g.copy_(gram[indices,:taps,:taps].to(receiver.complex_dtype))
                receiver._graph_body(dt)  # Exact existing implementation; no nested graph preparation/replay.
        for indices,_,receiver,_ in self.grouped.groups:
            self.output[:,indices]=receiver._scores

    @torch.inference_mode(False)
    @torch.no_grad()
    def replay(self, observation):
        if not self.prepared:
            raise RuntimeError('workspace is not prepared or was closed')
        if torch.cuda.current_stream(self.device).cuda_stream != self._construction_stream.cuda_stream:
            raise RuntimeError('workspace replay must use its construction stream')
        if self._backend_signature() != self._backend_identity:
            raise RuntimeError('native backend configuration changed; rebuild workspace')
        current=tuple(tensor.data_ptr() for _,_,r,_ in self.grouped.groups for tensor in
                      (r.weights,r._scores,r._graph_r,r._graph_g))
        if current!=self._state_pointers:
            raise RuntimeError('exclusively owned receiver storage was replaced')
        started=time.perf_counter()
        validate_observation(observation,self.shape,self.dtype,fp32_input=self.bank.precision!='fp64')
        validated=time.perf_counter()
        source=observation.detach() if isinstance(observation,torch.Tensor) else torch.from_numpy(np.asarray(observation))
        self.observation.copy_(source)
        copied=time.perf_counter()
        begin,end=torch.cuda.Event(enable_timing=True),torch.cuda.Event(enable_timing=True)
        with torch.cuda.device(self.device):
            begin.record(self._construction_stream)
            self.graph.replay()
            end.record(self._construction_stream)
        torch.cuda.synchronize(self.device)
        completed=time.perf_counter()
        scores=self.output.detach().cpu().numpy().copy()
        finished=time.perf_counter()
        self.replays+=1
        return scores,dict(validation_seconds=validated-started,copy_enqueue_seconds=copied-validated,
            graph_and_sync_wall_seconds=completed-copied,graph_device_seconds=begin.elapsed_time(end)/1000.,
            score_transfer_seconds=finished-completed,complete_replay_seconds=finished-started,
            copies_are_independent_trials=False,signature_sha256=self.signature_sha256)

    def close(self):
        if hasattr(self,'_capture_stream'):
            self._capture_stream.synchronize()
        self.graph=None
        self.schedule=[]
        self._transitions={}
        self.prepared=False
        self.observation=None
        self.output=None
        self.reciprocal_variance=None

