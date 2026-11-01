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


def _byte_count(value, name):
    if isinstance(value, bool) or not isinstance(value, (int,np.integer)) or value < 0:
        raise ValueError(f'{name} must return nonnegative integer bytes')
    return int(value)


def backend_contract_metadata(backend):
    """Snapshot arithmetic identity, excluding evolving execution evidence."""
    if backend is None:
        return None
    if hasattr(backend,'contract_metadata'):
        metadata=backend.contract_metadata()
        if not isinstance(metadata,dict):
            raise TypeError('backend contract_metadata must return a JSON object')
    else:
        metadata={name:getattr(backend,name,None) for name in
                  ('format','implementation','block_m','block_n','block_k','num_warps','num_stages','group_size')}
    # Copy nested lists/dicts too: later runtime mutations must not rewrite the
    # frozen workspace identity. Non-JSON arithmetic policy is rejected.
    return dict(type=type(backend).__module__+'.'+type(backend).__name__,
                contract=json.loads(json.dumps(metadata,sort_keys=True,allow_nan=False)))


def _packed_bytes(bank, rows, k):
    if bank.backend is None:
        return rows*k*(8 if bank.precision=='fp64' else 4)
    if hasattr(bank.backend,'estimated_packed_nbytes'):
        return _byte_count(bank.backend.estimated_packed_nbytes(rows,k),'estimated_packed_nbytes')
    if getattr(bank.backend,'format',None) not in ('nvfp4','mxfp4'):
        raise TypeError('packed backend must expose estimated_packed_nbytes')
    kp=math.ceil(k/bank.backend.block_k)*bank.backend.block_k
    return rows*(kp//2+kp//bank._half_alignment+20)


def _scratch_bytes(bank, operation, *shape):
    if bank.backend is None:
        return 0
    function=getattr(bank.backend,'estimated_'+operation+'_scratch_nbytes',None)
    if function is not None:
        return _byte_count(function(*shape),'estimated_'+operation+'_scratch_nbytes')
    if getattr(bank.backend,'format',None) in ('nvfp4','mxfp4'):
        # Existing fused native FP4 kernels allocate only their returned packs
        # and product. Register/shared-memory storage is not CUDA heap storage.
        return 0
    raise TypeError('packed backend must declare '+operation+' scratch bytes')


def _block_calendar(bank):
    for block in range(bank.block_count):
        first=block*bank.block_samples
        stop=min(first+bank.block_samples,bank.samples)
        center=(first+stop)/(2*bank.sample_rate)
        previous=(max(0,first-bank.block_samples)+first)/(2*bank.sample_rate)
        yield first,stop,0. if block==0 else center-previous


def preparation_estimate(bank, grouped, shape, dtype):
    """Retained schedule plus peak live blocks, not summed block temporaries.

    All block/tile temporary references have explicit helper lifetimes, and all
    work runs on one capture stream. The allocator may reuse dead temporaries
    in that graph's private pool. Reserve separate full envelopes for eager
    warmup and captured storage, plus immutable schedule/state and preparation
    staging. These are logical planning bounds, not an allocator-fragmentation
    guarantee: allocated/reserved peaks and setup guards remain authoritative.
    """
    batch, extent = shape
    columns = batch*len(bank.lags)
    scalar_bytes = 16 if dtype == torch.complex128 else 8
    schedule=0
    packed_a=sample_indices=grams=0
    a_staging=0
    b_live=0
    product_live=0
    calendar=list(_block_calendar(bank))
    for first,stop,dt in calendar:
        length=stop-first
        half = math.ceil(length/bank._half_alignment)*bank._half_alignment
        k = 2*half
        for f0 in range(0,len(bank.frequencies),bank.frequency_tile):
            ft=min(bank.frequency_tile,len(bank.frequencies)-f0)
            for c0 in range(0,bank.candidates,bank.candidate_tile):
                ct=min(bank.candidate_tile,bank.candidates-c0)
                rows=2*ft*ct*bank.taps
                size=_packed_bytes(bank,rows,k)
                packed_a+=size
                schedule+=size
                a_staging=max(a_staging,64*ft*ct*bank.taps*half+16*ct*bank.taps*length+
                              16*ft*length+8*length+8*ft+_scratch_bytes(bank,'pack',rows,k))
                n=min(columns,bank.column_tile)
                product_live=max(product_live,rows*n*8+_scratch_bytes(bank,'matmul',rows,n,k))
        n=min(columns,bank.column_tile)
        b_live=max(b_live,n*half*64+_packed_bytes(bank,n,k)+_scratch_bytes(bank,'pack',n,k))
        index_bytes=columns*length*8
        gram_bytes=bank.candidates*bank.taps**2*16
        sample_indices+=index_bytes
        grams+=gram_bytes
        schedule+=index_bytes+gram_bytes
    geometry = columns*(40+16*len(bank.frequencies))
    length=min(bank.block_samples,bank.samples)
    state_bound = sum(receiver.working_bytes_bound for _,_,receiver,_ in grouped.groups)
    exact_dts={dt for _,_,dt in calendar if dt}
    transition_bound = len(exact_dts)*sum(receiver.frequency_states**2*16+
        receiver.frequency_states*receiver.phase_states*24+receiver.phase_states*32
        for _,_,receiver,_ in grouped.groups)
    output = columns*len(bank.frequencies)*bank.candidates*bank.taps*16
    live_block=2*state_bound+6*output+b_live+product_live
    bank_cache=sum(getattr(bank,name,0) for name in ('_cache_bytes','_gram_cache_bytes','_geometry_cache_bytes'))
    raw_gram_cache=min(grams,getattr(bank,'gram_cache_bytes',0))
    grid=8*(2*len(bank.lags)+len(bank.frequencies))
    retained=schedule+geometry+transition_bound+batch*extent*scalar_bytes+2*state_bound+bank_cache+raw_gram_cache+grid
    return dict(host_bytes=bank._basis.nbytes+transition_bound+bank.block_count*2048+
                    64*bank.candidates*length*bank.taps,
                schedule_bytes=schedule+geometry+transition_bound,
                schedule_packed_a_bytes=packed_a,
                schedule_sample_index_bytes=sample_indices,
                schedule_normalized_gram_bytes=grams,
                public_geometry_bytes=geometry,
                preexisting_bank_cache_bytes=bank_cache,
                raw_gram_cache_bound_bytes=raw_gram_cache,
                public_grid_bytes=grid,
                exact_transition_count=len(exact_dts),
                exact_transition_residency_bound_bytes=transition_bound,
                template_preparation_transient_bound_bytes=a_staging,
                capture_transient_bound_bytes=live_block,
                eager_warm_pool_bound_bytes=live_block,
                graph_private_pool_bound_bytes=live_block,
                retained_device_bound_bytes=retained,
                device_total_bound_bytes=retained+a_staging+2*live_block,
                transient_lifetime='One block helper and one column-tile helper on one capture stream; no returned temporary survives to the next block.',
                estimate_scope='Logical retained tensors plus separate maximum-live eager/capture envelopes; excludes allocator fragmentation, driver/library allocations beyond declared backend scratch, and unrelated allocations. Actual allocated/reserved setup caps remain enforced.')


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
            device_compute_capability=list(torch.cuda.get_device_capability(self.device)),
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
        self.memory_observations=[]
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
            self._memory_guard('schedule prepared')
            self._capture_stream.wait_stream(self._construction_stream)
            self.graph = torch.cuda.CUDAGraph()
            with torch.cuda.stream(self._capture_stream):
                # Compile every shape and initialize BLAS handles outside capture.
                self._body(check_memory=True)
            self._capture_stream.synchronize()
            self._memory_guard('before capture',additional_reserved=self.estimate['graph_private_pool_bound_bytes'])
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
        return backend_contract_metadata(self.bank.backend)

    def _memory_guard(self,phase,additional_reserved=0):
        """Allocator metadata only, invoked during uncaptured setup, never replay."""
        allocated=torch.cuda.memory_allocated(self.device)
        reserved=torch.cuda.memory_reserved(self.device)
        self.memory_observations.append(dict(phase=phase,allocated_bytes=allocated,
                                             reserved_bytes=reserved,additional_reserved_bytes=additional_reserved))
        if max(allocated,reserved)+additional_reserved>self.memory_limit_bytes:
            raise MemoryError('whole-scan setup memory cap at '+phase)

    def _prepare_schedule(self):
        bank = self.bank
        columns = self.shape[0]*len(bank.lags)
        # Own these references once, independently of the bank's bounded LRU.
        geometries=[bank._column_geometry(columns,n0,min(columns,n0+bank.column_tile),self.origin)
                    for n0 in range(0,columns,bank.column_tile)]
        for block,(first,stop,dt) in enumerate(_block_calendar(bank)):
            self._memory_guard('before schedule block',additional_reserved=self.estimate['template_preparation_transient_bound_bytes'])
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

    def _body(self,check_memory=False):
        self._reset_state()
        for dt,gram,tiles in self.schedule:
            if check_memory:
                self._memory_guard('before warm block',additional_reserved=self.estimate['capture_transient_bound_bytes'])
            self._run_block(dt,gram,tiles)
            # _run_block returns None. Its dense/r arrays and all tile locals
            # are now dead, rather than carrying into the next loop iteration.
            if check_memory:
                self._memory_guard('after warm block')
        for indices,_,receiver,_ in self.grouped.groups:
            self.output[:,indices]=receiver._scores

    def _run_block(self,dt,gram,tiles):
        bank=self.bank
        dense=torch.empty((self.shape[0],len(bank.frequencies),bank.candidates,len(bank.lags),bank.taps),
                          dtype=torch.complex128,device=self.device)
        for tile in tiles:
            self._write_column_tile(dense,tile)
        r=dense.permute(0,1,3,2,4).reshape(self.grouped.batch,bank.candidates,bank.taps)
        for indices,taps,receiver,_ in self.grouped.groups:
            if dt:
                receiver._transition_cache.clear()
                receiver._transition_cache[dt]=self._transitions[id(receiver)][dt]
            receiver._graph_r.copy_(r[:,indices,:taps].to(receiver.complex_dtype))
            receiver._graph_g.copy_(gram[indices,:taps,:taps].to(receiver.complex_dtype))
            receiver._graph_body(dt)  # Same finite-model body and operation order.

    def _write_column_tile(self,dense,tile):
        bank=self.bank
        trials,lag_indices,residual,sample_indices,apacks=tile
        windows=self.observation[trials[:,None],sample_indices]
        rows=observation_rows(windows,half_alignment=bank._half_alignment).to(bank._dtype).contiguous()
        bpack=bank._pack(rows)  # ALWAYS recomputed from current captured input.
        destination=dense.permute(0,3,1,2,4)
        for f0,f1,c0,c1,apack in apacks:
            self._write_product(destination,trials,lag_indices,residual,apack,bpack,f0,f1,c0,c1)

    def _write_product(self,destination,trials,lag_indices,residual,apack,bpack,f0,f1,c0,c1):
        bank=self.bank
        product=apack@bpack.T if bank.backend is None else bank.backend.matmul(apack,bpack)
        if bank.fused_reconstruction:
            value=reconstruct_correlations(product,residual[f0:f1],self.reciprocal_variance,
                                           candidates=c1-c0,taps=bank.taps)
        else:
            value=reconstruction_reference(product,residual[f0:f1],self.reciprocal_variance,
                                           candidates=c1-c0,taps=bank.taps)
        destination[trials,lag_indices,f0:f1,c0:c1]=value

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

