"""Complete finite-bit DSP backend for the headless Robust stream receiver.

The finite window is PUBLIC local search geometry, never a received length.
This module does not infer a message endpoint from a recording or transport EOF.
Helper/coarse statistics select hypotheses only; persistent fine likelihoods
consume the complete input once and own the final bit/null decision.
"""
from __future__ import annotations

import copy
from concurrent.futures import ThreadPoolExecutor
from dataclasses import replace
import hashlib
import json
import math
import os
from pathlib import Path
import time
import threading
import weakref

import numpy as np
from scipy.stats import gamma

from triad_acquisition import refinement_coordinates
from triad_continuous import file_identity, tracking_source_basis
from triad_case_replay import helper_workspace_estimate
from triad_cpu import initialize_numerical_worker
from triad_dynamic_variants import core_projection_bands
from triad_operational_admission import (DEFAULT_COHERENCES, operational_decision,
    operational_threshold, public_grid)
from triad_realtime_fft import FFTScreen
from triad_timing_forward import SparseTimingForward, TimingState
from triad_tracking_basis import mapped_kernel_path
from triad_tracking_receiver import PracticalTrackingReceiver
from triad_weak_helper import estimate_weak_helper


_DEFAULT = dict(top_per_bit=2, maximum_fine_candidates=64,
    block_seconds=.25, segment_seconds=.5, chunk_samples=16384,
    maximum_states=64, maximum_proposals=256, residual_rate_step_ppm=5.,
    maximum_acceleration_ppm_per_second=10.,
    fine_delay_subdivisions=2, cache_bytes=32*1024**2,
    noise_upper_tail_probability=.001, precision='fp64')
_APERTURE_NAMES = ('quiet_aperture_first_sample', 'quiet_aperture_stop_sample')
_CUDA_LINALG_LOCK = threading.Lock()
_CUDA_LINALG_READY = set()


def _positive_integer(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, np.integer)) or value < 1:
        raise ValueError(name+' must be a positive integer')
    return int(value)


def _stage(began, represented, work=None):
    return dict(included_in_service=True,
        ratio_processing_seconds_per_baseband_second=(time.perf_counter()-began)/represented,
        represented_baseband_seconds=represented, work=work or {})


def fine_public_grid(manifest, epochs, coarse_frequency_step):
    grid = public_grid(manifest['channel'], manifest['calendar']['arrival_prior_seconds'], epochs)
    # Preserve long-window CFO resolution when the legacy .01 Hz grid is too coarse.
    return replace(grid, frequency_step=min(grid.frequency_step, coarse_frequency_step))


def _initialize_cuda_worker(allocation, device):
    import torch
    initialize_numerical_worker(allocation)
    torch.cuda.set_device(device)
    torch.cuda.set_stream(torch.cuda.default_stream(device))


def _require_default_cuda_stream(device):
    import torch
    if (torch.cuda.current_device() != device or
            torch.cuda.current_stream(device) != torch.cuda.default_stream(device)):
        raise ValueError('CUDA fine work requires the selected device and its default stream')


def _prepare_cuda_linalg(device):
    """Initialize ATen's lazy CUDA linalg dispatch before concurrent updates."""
    import torch

    _require_default_cuda_stream(device)
    key = (os.getpid(), device)
    with _CUDA_LINALG_LOCK:
        if key in _CUDA_LINALG_READY:
            return dict(device=device, initialized_here=False)
        # Even workers sharing a GPU stream can concurrently enter ATen's
        # first-call registration on the host. Exercise the actual complex128
        # operators serially before creating our executor (PyTorch #90613).
        began = time.perf_counter()
        free_before, total = torch.cuda.mem_get_info(device)
        allocated_before = torch.cuda.memory_allocated(device)
        reserved_before = torch.cuda.memory_reserved(device)
        with torch.inference_mode():
            sample = torch.eye(2, dtype=torch.complex128, device=f'cuda:{device}')
            eigenvalues, vectors = torch.linalg.eigh(sample)
            factor, info = torch.linalg.cholesky_ex(sample, check_errors=False)
            if bool((info != 0).any()):
                raise RuntimeError('CUDA fine linalg preflight failed Cholesky factorization')
            solution = torch.cholesky_solve(sample, factor)
            if not (torch.allclose(eigenvalues, sample.diagonal().real) and
                    bool(torch.isfinite(vectors).all()) and torch.allclose(solution, sample)):
                raise RuntimeError('CUDA fine linalg preflight returned invalid complex128 results')
        torch.cuda.synchronize(device)
        del sample, eigenvalues, vectors, factor, info, solution
        result = dict(device=device, initialized_here=True, startup_wall_seconds=time.perf_counter()-began,
            device_total_bytes=total, device_free_before_bytes=free_before,
            device_free_after_bytes=torch.cuda.mem_get_info(device)[0],
            torch_allocated_before_bytes=allocated_before, torch_allocated_after_bytes=torch.cuda.memory_allocated(device),
            torch_reserved_before_bytes=reserved_before, torch_reserved_after_bytes=torch.cuda.memory_reserved(device),
            scope='Serial CUDA linalg setup before work timers. Device-free snapshots include other device users.')
        _CUDA_LINALG_READY.add(key)
        return result


def _public_metrics(value):
    """Keep elapsed/baseband ratios as the only public processing-time metric."""
    private = {'cpu_seconds', 'wall_seconds', 'push_cpu_seconds',
        'push_process_cpu_seconds', 'segment_process_cpu_seconds',
        'maximum_segment_cpu_seconds', 'maximum_segment_wall_seconds',
        'maximum_segment_timing_scope', 'preparation_seconds'}
    if isinstance(value, dict):
        return {key: _public_metrics(item) for key, item in value.items() if key not in private}
    if isinstance(value, (list, tuple)):
        return [_public_metrics(item) for item in value]
    return value


class FineSession:
    """One fresh full or abbreviated persistent fine-likelihood observation.

    Creation, native cache fills, segment preparation and finish are all
    data-dependent receiver service. The owner must include creation in timing.
    Absolute physical coordinates preserve source phase for interior probes.
    """
    def __init__(self, owner, coordinates, samples, physical_origin, work_probe=False):
        owner._check_fine_context()
        self.owner = owner
        self.receiver = None
        self.provider = None
        self.samples = _positive_integer(samples, 'fine sample count')
        if isinstance(physical_origin, bool) or not isinstance(physical_origin, (int, np.integer)) or physical_origin < 0:
            raise ValueError('physical origin must be a nonnegative integer')
        self.origin = int(physical_origin)
        owner.grid.validate_selected(coordinates)
        if len(coordinates) > owner.policy['maximum_fine_candidates']:
            raise ValueError('fine coordinate bank exceeds its public cap')
        self.coordinates = tuple(coordinates)
        if not isinstance(work_probe, bool):
            raise ValueError('work probe must be boolean')
        self.work_probe_only = work_probe
        p, fs = owner.policy, owner.sample_rate
        extent = owner.initial_rate_half_width_ppm
        radius = max(1, math.ceil(extent/p['residual_rate_step_ppm'])) if extent else 0
        if radius > 32:
            raise ValueError('public residual clock lattice exceeds the bounded radius32')
        rate_step = extent/radius if radius else p['residual_rate_step_ppm']
        rates = (-radius, 0, radius) if radius else (0,)
        half = p['fine_delay_subdivisions']//2
        count = len(coordinates)*(2*half+1)*len(rates)
        if count > p['maximum_proposals']:
            raise ValueError('public initial coarse-cell lattice exceeds fine proposal cap')
        states = [TimingState(index, delay, rate) for index in range(len(coordinates))
            for delay in range(-half, half+1) for rate in rates]
        if work_probe:
            # A work-only ceiling fixture executes the same 256-state kernels
            # instead of scaling a small beam's timing. Extra public legal
            # states never become actual receiver hypotheses or success data.
            present = set(states)
            delay_radius = half
            while len(states) < p['maximum_proposals']:
                delays = (range(-half, half+1) if delay_radius == half
                          else (-delay_radius, delay_radius))
                for group in range(len(coordinates)):
                    for delay in delays:
                        for rate in range(-radius, radius+1):
                            state = TimingState(group, delay, rate)
                            if state not in present:
                                present.add(state)
                                states.append(state)
                                if len(states) == p['maximum_proposals']:
                                    break
                        if len(states) == p['maximum_proposals']:
                            break
                    if len(states) == p['maximum_proposals']:
                        break
                delay_radius += 1
        self.forward = SparseTimingForward(states,
            model_count=len(owner.coherences), delay_step_seconds=owner.initial_delay_step_seconds,
            rate_step_ppm=rate_step, maximum_rate_ppm=extent,
            maximum_acceleration_ppm_per_second=p['maximum_acceleration_ppm_per_second'],
            maximum_states=p['maximum_states'], maximum_proposals=p['maximum_proposals'])
        ch = owner.manifest['channel']
        taps = ((0., 0.), (float(ch['spread_delay_ms'])*.001,
            float(ch['spread_doppler_hz'])))
        if taps[0] == taps[1]:
            taps = taps[:1]
        calendar = owner.manifest['calendar']
        offset = float(calendar['capture_lead_seconds'])+owner.capture_origin/fs
        source_offset = float(calendar['data_sample_offset'])/fs
        self.provider = owner.basis_factory(owner.basis_source, self.coordinates,
            taps, offset, self.forward, float(ch['nominal_conversion_if_hz']),
            template_backend='auto' if p['cache_bytes'] else 'direct', cache_bytes=p['cache_bytes'],
            source_offset_seconds=source_offset, template_executor=owner.executor,
            template_workers=owner.workers)
        self.template_calls = self.template_rows = 0
        self._template_energy = 0.
        bulk_statistics = getattr(self.provider, 'bulk_statistics', None)
        reference = weakref.ref(self)
        def counted(provider):
            def render(*arguments):
                current = reference()
                if current is None:
                    raise RuntimeError('fine session no longer owns its basis callback')
                result = provider(*arguments)
                current.template_calls += 1
                if bulk_statistics is None:
                    current.template_rows += result.size
                    current._template_energy += float(np.vdot(result, result).real)
                else:
                    current.template_rows += result.numel()
                    flat = result.reshape(-1)
                    current._template_energy = current._template_energy+flat.vdot(flat).real
                return result
            statistics = getattr(provider, 'block_statistics', None)
            if statistics is not None and p['precision'] == 'fp64' and owner.fine_cuda_index is None:
                def block_statistics(states, anchor, requests):
                    current = reference()
                    if current is None:
                        raise RuntimeError('fine session no longer owns its basis callback')
                    result = statistics(states, anchor, requests)
                    if result is None:
                        return None
                    prepared = []
                    for (first, stop, *_), (r, gram, energy) in zip(requests, result, strict=True):
                        current.template_calls += 1
                        # Logical raw extent includes the exact common null.
                        current.template_rows += 3*len(states)*len(taps)*(stop-first)
                        current._template_energy += energy
                        prepared.append((r, gram))
                    return prepared
                render.block_statistics = block_statistics
            return render
        render = counted(self.provider)
        affine_render = counted(getattr(self.provider, 'initial_affine', self.provider))
        available = (owner.memory_limit_bytes-owner.coarse_reserved_bytes-
            owner.input_workspace_bytes-owner.native_workspace_bytes-p['cache_bytes'])
        if available < 1:
            raise MemoryError('fine stage has no remaining combined workspace')
        self.receiver = PracticalTrackingReceiver(sample_rate=fs, samples=self.samples,
            block_samples=max(1, round(p['block_seconds']*fs)),
            segment_samples=max(1, round(p['segment_seconds']*fs)),
            basis_provider=render, timing_forward=self.forward,
            initial_affine_basis_provider=affine_render,
            required_bands=owner.required_bands, excluded_bands=owner.excluded_bands,
            noise_variance=float(owner.manifest['power']['complex_noise_variance']),
            stationary_power=tuple(1/len(taps) for _ in taps),
            coherence_seconds=owner.coherences, device=owner.fine_execution_device, precision=p['precision'],
            audio_center_hz=float(ch['nominal_conversion_if_hz']),
            projection_mode='dft', memory_limit_bytes=available,
            shared_basis_workspace_bytes=getattr(self.provider, 'staging_workspace_bytes', 0),
            bulk_statistics=bulk_statistics,
            native_gaussian=(getattr(self.provider, 'native_gaussian', None)
                if p['precision'] == 'fp64' and owner.fine_cuda_index is None else None),
            maximum_input_chunk_samples=p['chunk_samples'], require_physical_map=True,
            hypothesis_executor=owner.executor,
            # The CPU's small state solves work best batched per coherence.
            # The larger independent mapped-basis work uses the full pool.
            hypothesis_shards=(1 if owner.fine_cuda_index is None else
                               max(1, math.ceil(owner.workers/len(owner.coherences)))),
            basis_projection_workers=(owner.workers if bulk_statistics is None and
                owner.fine_cuda_index is None and p['precision'] == 'fp64' else 1),
            quiet_sample_bounds=tuple(max(0, min(self.samples, x-self.origin))
                for x in owner.quiet_aperture))
        self.initial_lattice = dict(states=len(states),
            delay_half_width_seconds=.5/owner.projected_sample_rate,
            delay_step_seconds=owner.initial_delay_step_seconds,
            rate_half_width_ppm=extent, rate_step_ppm=rate_step,
            initial_rate_indices=list(rates),
            work_probe_only=work_probe,
            initial_delay_index_min=min(state.delay_index for state in states),
            initial_delay_index_max=max(state.delay_index for state in states),
            scope='Public finite covering lattice for coarse cells; not a continuous-parameter certificate.')

    @property
    def template_energy(self):
        return float(self._template_energy)

    def push(self, first, values, valid=None):
        if self.receiver is None:
            raise RuntimeError('fine session is closed')
        self.owner._check_fine_context()
        self.receiver.push(first, values, valid,
            physical_origin=self.origin+int(first),
            physical_offsets=np.arange(len(values), dtype=float))

    def finish(self):
        if self.receiver is None:
            raise RuntimeError('fine session is closed')
        self.owner._check_fine_context()
        result = self.receiver.finish()
        result['native_template_work'] = dict(calls=self.template_calls,
            returned_elements=self.template_rows, energy=self.template_energy,
            representative_work=bool(self.template_calls and self.template_energy > 0),
            cache=self.provider.cache_report() if hasattr(self.provider, 'cache_report') else {})
        result['coordinates'] = self.coordinates
        result['initial_lattice'] = self.initial_lattice
        result['work_probe_only'] = self.work_probe_only
        result['arithmetic'] = dict(precision=self.owner.policy['precision'], device=self.owner.fine_execution_device,
            likelihood_accumulation='fp64',
            selection='Predeclared before observing samples; no data-dependent precision fallback.')
        if hasattr(self.provider, 'bulk_arithmetic'):
            result['arithmetic']['bulk_statistics'] = self.provider.bulk_arithmetic
        plan = self.receiver.nominal_work_plan
        taps = len(self.receiver.receiver_arguments['stationary_power'])
        states = self.receiver.timing.maximum_proposals
        result['public_work_ceiling'] = dict(samples=self.samples,
            segments=plan['segments'], blocks=plan['blocks'],
            nominal_initial_states=plan['initial_states'], adaptive_emission_states=states,
            coherence_models=len(self.owner.coherences), taps=taps,
            basis_rows_upper=2*self.samples*taps*(plan['initial_states']+states),
            candidate_block_updates_upper=plan['blocks']*3*len(self.owner.coherences)*
                (plan['initial_states']+states),
            scope='Fixed public caps and geometry; attaining their cost requires an actual matching work probe.')
        return result

    def close(self):
        """Release private lane/cache state before preparing another unit."""
        if self.receiver is not None:
            for lane in (self.receiver.nominal, self.receiver.adaptive):
                lane.basis_provider = None
            self.receiver = None
        self.provider = None
        self.forward = None


class FineReceiverContext:
    """Canonical persistent fine search and classification context.

    A complete receiver supplies the geometry and workspace reservation of its
    validated resident FFT screen. The fine search holds the same native public
    basis address and per-receiver allocation across each complete slot.
    """
    def __init__(self, manifest, basis_source, *, epochs, clocks, projected_sample_rate,
                 coarse_frequency_step, coarse_reserved_bytes, coarse_procedure_id,
                 coarse_reference_variance, workers, memory_limit_bytes, policy=None,
                 coherences=DEFAULT_COHERENCES, admission_policy=None,
                 basis_factory=tracking_source_basis, fine_device='cpu'):
        began = time.perf_counter()
        self.workers = _positive_integer(workers, 'hypothesis workers')
        self.memory_limit_bytes = _positive_integer(memory_limit_bytes, 'memory limit')
        self.manifest = copy.deepcopy(manifest)
        self.basis_source, self.basis_factory = basis_source, basis_factory
        self.projected_sample_rate = _positive_integer(projected_sample_rate, 'projected sample rate')
        self.coarse_reserved_bytes = _positive_integer(coarse_reserved_bytes, 'coarse workspace reservation')
        descriptor = getattr(basis_source, 'descriptor', {})
        self.native_fixed_workspace_bytes = int(descriptor.get('fixed_workspace_bytes', 0))
        self.native_workspace_bytes = max(self.native_fixed_workspace_bytes,
            int(descriptor.get('workspace_budget_bytes', 0)))
        self.sample_rate = int(manifest['sample_rate_hz'])
        self.samples = int(manifest['calendar']['capture_samples'])
        self.capture_origin = manifest['calendar'].get('physical_origin_sample', 0)
        if (isinstance(self.capture_origin, bool) or not isinstance(self.capture_origin, int) or
                self.capture_origin < 0):
            raise ValueError('public physical window origin must be a nonnegative integer')
        calendar = manifest['calendar']
        present = [name in calendar for name in _APERTURE_NAMES]
        if any(present) and not all(present):
            raise ValueError('both public quiet aperture endpoints are required')
        self.quiet_aperture_declared = all(present)
        self.quiet_aperture = tuple(calendar[name] for name in _APERTURE_NAMES) if all(present) else (
            self.capture_origin, self.capture_origin)
        if (any(isinstance(x, bool) or not isinstance(x, int) for x in self.quiet_aperture) or
                not self.capture_origin <= self.quiet_aperture[0] <= self.quiet_aperture[1] <= self.capture_origin+self.samples):
            raise ValueError('public quiet aperture must lie within the physical search window')
        variance = float(manifest['power']['complex_noise_variance'])
        if not math.isfinite(variance) or variance <= 0:
            raise ValueError('raw fine-noise variance must be finite and positive')
        self.policy = dict(_DEFAULT)
        unknown = set(policy or ())-set(self.policy)
        if unknown:
            raise ValueError('unknown complete receiver policy: '+', '.join(sorted(unknown)))
        self.policy.update(policy or {})
        self.policy['operational_evidence'] = 'fixed_initial_affine'
        for name in ('top_per_bit', 'maximum_fine_candidates', 'chunk_samples',
                     'maximum_states', 'maximum_proposals', 'fine_delay_subdivisions'):
            self.policy[name] = _positive_integer(self.policy[name], name)
        for name in ('block_seconds', 'segment_seconds',
                     'residual_rate_step_ppm'):
            if not math.isfinite(float(self.policy[name])) or self.policy[name] <= 0:
                raise ValueError(name+' must be finite and positive')
        for name in ('cache_bytes', 'maximum_acceleration_ppm_per_second'):
            if isinstance(self.policy[name], bool) or not math.isfinite(float(self.policy[name])) or self.policy[name] < 0:
                raise ValueError(name+' must be finite and nonnegative')
        if self.policy['maximum_fine_candidates'] > 64 or self.policy['top_per_bit'] > 64:
            raise ValueError('complete receiver fine/proposal caps exceed64')
        if self.policy['fine_delay_subdivisions'] < 2 or self.policy['fine_delay_subdivisions'] % 2:
            raise ValueError('fine delay subdivisions must be even and at least2')
        if not 0 < self.policy['noise_upper_tail_probability'] < 1:
            raise ValueError('noise tail probability must lie in (0,1)')
        if self.policy['precision'] not in ('fp32', 'fp64'):
            raise ValueError('complete receiver precision must be fp32 or fp64')
        if fine_device not in ('cpu', 'cuda'):
            raise ValueError('fine device must be cpu or cuda')
        self.fine_cuda_index = None
        self.fine_execution_device = fine_device
        if fine_device == 'cuda':
            import torch
            if self.policy['precision'] != 'fp64' or not torch.cuda.is_available():
                raise ValueError('experimental CUDA fine work requires available CUDA and FP64')
            self.fine_cuda_index = torch.cuda.current_device()
            self.fine_execution_device = f'cuda:{self.fine_cuda_index}'
            _prepare_cuda_linalg(self.fine_cuda_index)
        self.coherences = tuple(coherences)
        self.admission_policy = dict(admission_policy or {})
        operational_threshold(**self.admission_policy)
        self.grid = fine_public_grid(manifest, epochs, coarse_frequency_step)
        clocks = np.sort(clocks)
        self.coarse_clock_gap_ppm = float(np.max(np.diff(clocks))) if len(clocks) > 1 else 0.
        self.initial_rate_half_width_ppm = self.coarse_clock_gap_ppm/4
        self.initial_delay_step_seconds = 1/(projected_sample_rate*self.policy['fine_delay_subdivisions'])
        self.required_bands, self.excluded_bands = core_projection_bands(manifest,
            pilot_use='none', frequencies_hz=(-self.grid.frequency_bound, self.grid.frequency_bound),
            clock_ppm=(-self.grid.clock_bound, self.grid.clock_bound))
        mapped_kernel_digest = None
        if (basis_factory is tracking_source_basis and self.policy['cache_bytes'] and
                hasattr(basis_source, 'command')):
            mapped_kernel_digest = file_identity(mapped_kernel_path(basis_source.command[0]))['sha256']
        self.input_workspace_bytes = 0
        self.executor = None
        if self.workers > 1:
            allocation = {'threads': 1, 'linear_algebra_threads': 1}
            initializer, initargs = (initialize_numerical_worker, (allocation,))
            if self.fine_cuda_index is not None:
                # Parent inputs and each dedicated worker share one explicit
                # stream. Other streams require an event handoff protocol.
                initializer, initargs = (_initialize_cuda_worker, (allocation, self.fine_cuda_index))
            self.executor = ThreadPoolExecutor(max_workers=self.workers,
                initializer=initializer, initargs=initargs)
            try:
                barrier = threading.Barrier(self.workers)
                def ready():
                    barrier.wait(timeout=30.)
                for future in [self.executor.submit(ready) for _ in range(self.workers)]:
                    future.result()
            except BaseException:
                self.close()
                raise
        self.procedure_id = hashlib.sha256(json.dumps(dict(policy=self.policy, fine_device=self.fine_execution_device,
            grid=self.grid.__dict__, workers=self.workers, coherences=[str(x) for x in self.coherences],
            screen=coarse_procedure_id, admission=self.admission_policy,
            raw_noise_variance=float(self.manifest['power']['complex_noise_variance']),
            public_quiet_aperture=self.quiet_aperture,
            public_quiet_aperture_declared=self.quiet_aperture_declared,
            coarse_reference_noise_variance=coarse_reference_variance,
            mapped_kernel_sha256=mapped_kernel_digest,
            source={name: hashlib.sha256((Path(__file__).parent/name).read_bytes()).hexdigest()
                for name in ('triad_receiver.py','triad_tracking_receiver.py','triad_incremental_receiver.py',
                             'triad_dynamic_joint.py','triad_gaussian_state.py','triad_gaussian_native.py','triad_continuous.py',
                             'triad_tracking_basis.py')}),
            sort_keys=True).encode()).hexdigest()
        self.receiver_preparation_seconds = time.perf_counter()-began

    def _check_fine_context(self):
        if self.fine_cuda_index is not None:
            _require_default_cuda_stream(self.fine_cuda_index)

    def close(self):
        if self.executor is not None:
            self.executor.shutdown(wait=True, cancel_futures=True)
            self.executor = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def create_fine_session(self, coordinates, *, samples=None, physical_origin=None, work_probe=False):
        return FineSession(self, coordinates, self.samples if samples is None else samples,
            self.capture_origin if physical_origin is None else physical_origin, work_probe)

    def classify(self, tracked, coordinates, *, all_input_valid):
        """The same bounded endpoint code for complete reception and cost probes.

        A short FineSession remains incomplete for this full public window;
        measuring its endpoint work never turns it into a reception success.
        """
        nominal = tracked['lanes']['initial_affine']
        # This lane has immutable public atoms and no timing-history pruning
        # or merging. Adaptive moment matching depends on the selected bank,
        # so its K/N subset bound is not established; it remains diagnostic.
        scores = np.asarray(nominal['bit_scores'], dtype=float)
        if scores.shape != (2,) or not np.isfinite(scores).all():
            raise ValueError('two finite refined bit scores required')
        templates = tracked['native_template_work']
        complete = bool(tracked['complete_observation'] and tracked['processed_rows'] == self.samples and
            templates['representative_work'] and not tracked.get('work_probe_only', False))
        zero_h0 = bool(templates['calls'])  # every call's H0 was checked by _TimingStream
        modes, energy = nominal['quiet_observed_projection_modes'], nominal['quiet_projected_energy']
        noise_valid = bool(self.quiet_aperture_declared and modes > 0 and
            math.isfinite(energy) and energy >= 0 and all_input_valid)
        threshold = float(gamma.ppf(1-self.policy['noise_upper_tail_probability'], modes)) if noise_valid else None
        operational = (operational_decision(scores, coordinates, self.grid,
            zero_h0_verified=True, **self.admission_policy) if zero_h0 else None)
        if operational is not None:
            operational['evidence_source'] = 'fixed_initial_affine'
            operational['adaptive_combined_evidence_diagnostic_only'] = True
            operational['passes_conditional_bound'] = bool(complete and
                operational['passes_conditional_bound'])
        unique = scores[0] != scores[1]
        bit = int(np.argmax(scores)) if unique else None
        data = bool(complete and all_input_valid and unique and operational and
                    operational['passes_conditional_bound'])
        quiet = bool(complete and zero_h0 and noise_valid and energy <= threshold)
        return dict(status='scored' if complete else 'incomplete',
            slot_classification='DATA' if data else 'QUIET' if quiet else 'AMBIGUOUS',
            decision_bit=bit if data else None, candidate_bit=bit, bit_scores=scores.tolist(),
            operational=operational,
            quiet=dict(noise_consistent=quiet, projected_energy=energy,
                observed_projection_modes=modes, threshold=threshold,
                public_physical_aperture_samples=list(self.quiet_aperture),
                public_aperture_declared=self.quiet_aperture_declared,
                projection_scope='Only complete orthogonal likelihood blocks wholly inside the public uncontaminated aperture; all input remains in fine likelihoods.',
                noise_upper_tail_probability=self.policy['noise_upper_tail_probability'],
                false_end_scope='DATA-to-quiet probability requires channel/fade validation; no universal signal-absence guarantee.'),
            coverage=dict(complete_observation=complete, all_input_valid=bool(all_input_valid),
                consumed_samples=tracked['consumed_samples'], expected_samples=self.samples,
                zero_h0_verified=zero_h0, valid_noise_statistics=noise_valid,
                fine_candidates=len(coordinates),
                selected_grid_prior_mass=len(coordinates)/self.grid.candidate_count,
                coarse_evidence_used_only_for_selection=True,
                global_timing_pruning_once=True, physical_null_qualified=False))


class CompleteBitReceiver(FineReceiverContext):
    """Prepared one-stream Helper, coarse, fine and bit/null backend.

    Numerical libraries must be configured by the controller before workers
    start, with one numerical thread per hypothesis worker. The same executor
    first scores independent coarse workspaces, then independent coherence
    models; source-pipe rendering and global timing/pruning stay single-owner.
    """
    def __init__(self, manifest, screen, basis_source, *, workers=1,
                 memory_limit_bytes, policy=None, coherences=DEFAULT_COHERENCES,
                 admission_policy=None, basis_factory=tracking_source_basis, fine_device='cpu'):
        began = time.perf_counter()
        if not isinstance(screen, FFTScreen):
            raise TypeError('complete bit backend currently requires the canonical whole FFTScreen')
        if (int(manifest['sample_rate_hz']) != screen.fs or
                int(manifest['calendar']['capture_samples']) != screen.samples):
            raise ValueError('screen and public input window geometry differ')
        declared, proposal = copy.deepcopy(manifest), copy.deepcopy(screen.manifest)
        declared['power'].pop('complex_noise_variance', None)
        proposal['power'].pop('complex_noise_variance', None)
        for name in _APERTURE_NAMES:
            declared['calendar'].pop(name, None)
            proposal['calendar'].pop(name, None)
        if declared != proposal:
            raise ValueError('only noise normalization and declared quiet aperture may differ between proposal and fine manifests')
        self.screen = screen
        command = getattr(basis_source, 'command', None)
        if (not getattr(screen, 'device_parallel', False) and
                isinstance(command, (list, tuple)) and command):
            screen.enable_native_coarse(command[0])
        super().__init__(manifest, basis_source, epochs=screen.epochs, clocks=screen.clocks,
            projected_sample_rate=screen.output_fs, coarse_frequency_step=screen.maximum_frequency_step,
            coarse_reserved_bytes=screen.resident_estimate, coarse_procedure_id=screen.procedure_id,
            coarse_reference_variance=screen.reference_variance, workers=workers,
            memory_limit_bytes=memory_limit_bytes, policy=policy, coherences=coherences,
            admission_policy=admission_policy, basis_factory=basis_factory, fine_device=fine_device)
        self.helper_peak_bytes = 0
        self.receiver_preparation_seconds = time.perf_counter()-began

    def _input(self, raw, valid):
        values = np.asarray(raw)
        if values.shape != (self.samples,) or not np.iscomplexobj(values) or not np.isfinite(values).all():
            raise ValueError('one complete finite complex public search window is required')
        mask = np.ones(self.samples, np.uint8) if valid is None else np.asarray(valid)
        if mask.shape != values.shape or not np.isin(mask, (0, 1)).all():
            raise ValueError('matching binary input validity required')
        self.input_workspace_bytes = values.nbytes+mask.nbytes
        if self.input_workspace_bytes+self.screen.resident_estimate+self.native_workspace_bytes >= self.memory_limit_bytes:
            raise MemoryError('input and reusable receiver bank exceed combined workspace')
        return values, mask

    def _tiled_coarse(self, raw, valid, intervals, unit_limit):
        remaining = self.memory_limit_bytes-self.input_workspace_bytes-self.native_workspace_bytes
        screen = copy.copy(self.screen)
        screen.memory_limit_bytes = remaining
        unit = min(4, screen.maximum_frequency_points,
            screen.maximum_fft_products//(len(screen.workspaces)*len(screen.rays)))
        if unit < 1:
            return dict(status='unresolved', reason='frequency_tile_kernel_work_limit', candidates=[])
        device_parallel = getattr(screen, 'device_parallel', False)
        workers = 1 if device_parallel else self.workers
        plan = screen.tiled_frequency_plan(frequency_intervals=intervals,
            top_per_bit=self.policy['top_per_bit'], frequency_unit_points=unit,
            workspace_workers=workers)
        if plan['status'] != 'ready':
            return dict(status='unresolved', reason=plan['reason'], candidates=[], work=plan)
        filter_peak = screen._observation_peak_estimate()
        if filter_peak > remaining:
            return dict(status='unresolved', reason='complete_input_filter_working_memory_limit', candidates=[])
        projected = screen.prepare_observation(raw, valid)
        session = screen.start_tiled_frequency(projected, frequency_intervals=intervals,
            projection_receipt=screen.projection_receipt(), executor=None if device_parallel else self.executor,
            workspace_workers=workers, frequency_unit_points=unit,
            top_per_bit=self.policy['top_per_bit'])
        if unit_limit is not None:
            try:
                if unit_limit:
                    session.step(unit_limit)
                result = session.finish()
                if not session.complete:
                    result['coarse_session'] = session
                    return result
            except BaseException:
                session.close()
                raise
        else:
            try:
                while not session.complete:
                    session.step(64)
                result = session.finish()
            except BaseException:
                session.close()
                raise
        session.close()
        if 'work' in result:
            result['work']['estimated_peak_bytes'] = (max(filter_peak,
                result['work']['estimated_peak_bytes'])+
                self.input_workspace_bytes+self.native_workspace_bytes)
            result['work']['receiver_input_native_included'] = True
        return result

    def _coarse(self, raw, valid, intervals, unit_limit=None):
        if not bool(valid.all()):
            return dict(status='unresolved', reason='coarse_erasures_unsupported', candidates=[])
        frequencies, failure = self.screen._frequencies(None, intervals)
        if failure:
            if failure == 'frequency_work_limit':
                return self._tiled_coarse(raw, valid, intervals, unit_limit)
            return dict(status='unresolved', reason=failure, candidates=[])
        groups = self.screen._lag_frequency_groups(None, intervals)
        original_limit = self.screen.memory_limit_bytes
        remaining = self.memory_limit_bytes-self.input_workspace_bytes-self.native_workspace_bytes
        master = self.screen._correlation_plan(frequencies, groups, raw_input=True,
            top_per_bit=self.policy['top_per_bit'])
        if master['selected'] is None:
            return self._tiled_coarse(raw, valid, intervals, unit_limit)
        method = master['selected'][0]
        input_filter_peak = (master['estimated_peak_bytes_by_method'][method]+
            self.input_workspace_bytes+self.native_workspace_bytes)
        if input_filter_peak > self.memory_limit_bytes:
            return dict(status='unresolved', reason='complete_input_filter_working_memory_limit', candidates=[])
        device_parallel = getattr(self.screen, 'device_parallel', False)
        count = 1 if device_parallel else min(self.workers, len(self.screen.workspaces))
        parts = []
        while count:
            parts = []
            total_scratch = total_work = 0
            for indices in np.array_split(np.arange(len(self.screen.workspaces)), count):
                part = copy.copy(self.screen)
                part.workspaces = [self.screen.workspaces[int(i)] for i in indices]
                part.workers, part.correlation_method = 1, method
                part.memory_limit_bytes = remaining
                plan = part._correlation_plan(frequencies, groups, raw_input=False,
                    top_per_bit=self.policy['top_per_bit'])
                if plan['selected'] is None:
                    break
                total_scratch += plan['estimated_peak_bytes_by_method'][method]-part.resident_estimate
                total_work += plan['selected'][1]
                parts.append(part)
            peak = (self.input_workspace_bytes+self.native_workspace_bytes+
                self.screen.resident_estimate+total_scratch+16*self.screen.crop_rows)
            if len(parts) == count and peak <= self.memory_limit_bytes and total_work <= self.screen.maximum_fft_products:
                break
            count -= 1
        if not count:
            return dict(status='unresolved', reason='parallel_coarse_combined_work_or_memory_limit', candidates=[])
        self.screen.memory_limit_bytes = remaining
        try:
            projected = self.screen.prepare_observation(raw, valid)
        finally:
            self.screen.memory_limit_bytes = original_limit
        shared_spectra = None
        if method == 'frequency' and not device_parallel:
            # Every workspace partition uses the same observation/CFO lattice.
            # Prepare it once; workers own only products and correlation scratch.
            # The existing admission reserves a spectra copy per partition and
            # therefore conservatively covers this one shared immutable bank.
            shared_spectra = self.screen._frequency_spectra(projected, frequencies,
                workers=self.workers)
            self.screen._mark_readonly(shared_spectra)
        separation = .5/float(self.screen.manifest['format'].get('fine_chip_rate_hz', 60.))
        def score(part):
            if shared_spectra is not None:
                rows = part._frequency_workspace_rows(shared_spectra, frequencies,
                    sample_separation=max(1, math.ceil(separation*part.output_fs)),
                    top_per_bit=self.policy['top_per_bit'])
                retained = part._retained_candidates(rows,
                    top_per_bit=self.policy['top_per_bit'], separation=separation)
                return dict(status='proposed', candidates=retained[0]+retained[1])
            return part.score_projected(projected, frequency_intervals=intervals,
                projection_receipt=part.projection_receipt(), top_per_bit=self.policy['top_per_bit'])
        if self.executor is None or device_parallel:
            results = [score(part) for part in parts]
        else:
            futures, error = [], None
            for part in parts:
                try:
                    futures.append(self.executor.submit(score, part))
                except BaseException as exc:
                    error = exc
                    break
            results = []
            for future in futures:
                try:
                    results.append(future.result())
                except BaseException as exc:
                    if error is None:
                        error = exc
            if error is not None:
                raise error
        if any(r['status'] != 'proposed' for r in results):
            return dict(status='unresolved', reason='coarse_partition_unresolved', candidates=[])
        retained = [sorted((row for r in results for row in r['candidates'] if row['bit'] == bit),
            key=lambda row: row['score'], reverse=True)[:self.policy['top_per_bit']] for bit in (0, 1)]
        return dict(status='proposed', reason='complete_public_workspace_search',
            candidates=retained[0]+retained[1], candidates_by_bit=retained,
            work=dict(workspaces=len(self.screen.workspaces), workers=count,
                frequency_points=len(frequencies), arrival_points=self.screen.lag_count,
                shared_observation_spectra=shared_spectra is not None,
                observation_ffts=len(frequencies) if method == 'frequency' else 0,
                coarse_score_kernel=self.screen.policy.get('coarse_score_kernel'),
                fft_products=total_work, estimated_peak_bytes=max(peak, input_filter_peak),
                correlation_method=method))

    def acquire(self, raw, valid=None, *, coarse_unit_limit=None):
        """Run acquisition, or expose a bounded resumable broad-prior session.

        A limit0 prepares but does not score the overflow frequency tiles.
        Partial sessions have no coordinates and must be closed by their owner.
        The normal receive path uses None and completes the exact full search.
        """
        if coarse_unit_limit is not None and (isinstance(coarse_unit_limit, bool) or
                not isinstance(coarse_unit_limit, int) or not 0 <= coarse_unit_limit <= 64):
            raise ValueError('coarse unit limit must be None or an integer in[0,64]')
        values, mask = self._input(raw, valid)
        represented = self.samples/self.sample_rate
        began = time.perf_counter()
        cal = self.manifest['calendar']
        prefix = max(0, min(self.samples, math.ceil((float(cal['capture_lead_seconds'])+
            float(cal['data_sample_offset'])/self.sample_rate+
            max(0., self.grid.arrival_upper)+1)*self.sample_rate)))
        helper_manifest = copy.deepcopy(self.manifest)
        helper_manifest['calendar']['capture_samples'] = prefix
        helper_scratch = (helper_workspace_estimate(prefix, self.sample_rate)
            if self.manifest.get('helper_enabled', True) else 0)
        self.helper_peak_bytes = (self.input_workspace_bytes+self.native_workspace_bytes+
            self.screen.resident_estimate+helper_scratch)
        if self.helper_peak_bytes > self.memory_limit_bytes:
            return dict(status='unresolved', reason='complete_helper_transient_memory_limit',
                stages={}, coordinates=())
        helper = estimate_weak_helper(values[:prefix], helper_manifest,
            np.linspace(-self.grid.frequency_bound, self.grid.frequency_bound,
                2*math.ceil(self.grid.frequency_bound/.5)+1) if self.grid.frequency_bound else np.array([0.]),
            valid=mask[:prefix], config={'frequency_config': dict(
                selection_geometry='continuous_interval', future_horizon_seconds=represented)})
        stages = dict(helper=_stage(began, represented))
        frequency = helper.get('frequency') or {}
        intervals = (frequency['expanded_intervals_hz'] if frequency.get('status') == 'narrowed'
            else [[-self.grid.frequency_bound, self.grid.frequency_bound]])
        began = time.perf_counter()
        coarse = self._coarse(values, mask, intervals, coarse_unit_limit)
        stages['coarse'] = _stage(began, represented, coarse.get('work'))
        if not coarse.get('candidates') or {row['bit'] for row in coarse['candidates']} != {0, 1}:
            result = dict(status='unresolved', reason=coarse.get('reason'), stages=stages,
                helper=helper, coarse=coarse, coordinates=())
            if 'coarse_session' in coarse:
                result['coarse_session'] = coarse.pop('coarse_session')
                result['status'] = 'partial_acquisition'
            return result
        began = time.perf_counter()
        planning = copy.deepcopy(self.manifest)
        planning['format']['bit_seconds'] = self.screen.source_samples/self.sample_rate
        coordinates = refinement_coordinates(coarse['candidates'], planning,
            clock_half_step_ppm=self.coarse_clock_gap_ppm/2, timing_step_seconds=0.,
            maximum_candidates=self.policy['maximum_fine_candidates'])
        coordinates = tuple(dict.fromkeys(self.grid.snap(row) for row in coordinates))
        self.grid.validate_selected(coordinates)
        stages['refinement_selection'] = _stage(began, represented, dict(candidates=len(coordinates)))
        return dict(status='acquired', stages=stages, helper=helper, coarse=coarse,
            coordinates=coordinates, searched_frequency_intervals_hz=intervals)

    def receive(self, raw, valid=None, *, physical_origin=None):
        began = time.perf_counter()
        physical_origin = self.capture_origin if physical_origin is None else physical_origin
        if physical_origin != self.capture_origin:
            raise ValueError('complete window origin must match its public calendar; use FineSession for interior probes')
        represented = self.samples/self.sample_rate
        values, mask = self._input(raw, valid)
        result = dict(schema='triad-complete-bit-receiver-v1', status='unresolved',
            slot_classification='AMBIGUOUS', decision_bit=None, candidate_bit=None,
            procedure_id=self.procedure_id, represented_baseband_seconds=represented,
            sample_rate_hz=self.sample_rate,
            processing_ratio_scope='One complete public search-window exposure; adjacent windows may overlap. Sustained/message capacity must use public slot stride or unique physical sample span.',
            stages={}, coverage=dict(complete_observation=False, all_input_valid=bool(mask.all()),
                consumed_samples=0, expected_samples=self.samples,
                physical_first_sample=int(physical_origin),
                physical_stop_sample=int(physical_origin)+self.samples,
                zero_h0_verified=False, valid_noise_statistics=False))
        acquired = self.acquire(values, mask)
        result['stages'] = acquired['stages']
        result['acquisition'] = acquired
        if acquired['status'] != 'acquired':
            result['reason'] = acquired.get('reason')
        else:
            mark = time.perf_counter()
            fine = self.create_fine_session(acquired['coordinates'], physical_origin=physical_origin)
            result['stages']['fine_preparation'] = _stage(mark, represented,
                dict(candidates=len(acquired['coordinates'])))
            mark = time.perf_counter()
            try:
                for first in range(0, self.samples, self.policy['chunk_samples']):
                    stop = min(self.samples, first+self.policy['chunk_samples'])
                    fine.push(first, values[first:stop], mask[first:stop])
                tracked = fine.finish()
            finally:
                fine.close()
            result['stages']['tracking'] = _stage(mark, represented, tracked['work_counts'])
            mark = time.perf_counter()
            decision = self.classify(tracked, acquired['coordinates'], all_input_valid=bool(mask.all()))
            coverage = decision.pop('coverage')
            result.update(decision)
            result.update(tracking=tracked,
                workspace=dict(estimated_peak_bytes=max(acquired['coarse']['work']['estimated_peak_bytes'],
                    self.helper_peak_bytes, self.input_workspace_bytes+self.native_workspace_bytes+
                    self.screen.resident_estimate+self.policy['cache_bytes']+
                    tracked['working_bytes_estimate']), memory_limit_bytes=self.memory_limit_bytes,
                    native_fixed_workspace_bytes=self.native_fixed_workspace_bytes,
                    native_reserved_workspace_bytes=self.native_workspace_bytes,
                    helper_combined_peak_bytes=self.helper_peak_bytes),
                work=dict(full_input_rows=self.samples, fine=tracked['work_counts'],
                    native_templates=tracked['native_template_work']))
            result['coverage'].update(coverage)
            result['stages']['decision'] = _stage(mark, represented)
        result = _public_metrics(result)
        result['ratio_processing_seconds_per_baseband_second'] = (time.perf_counter()-began)/represented
        return result
