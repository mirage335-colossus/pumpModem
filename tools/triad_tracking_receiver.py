#!/usr/bin/env python3
"""Canonical persistent nominal/adaptive timing receiver.

The two internal lanes share stream geometry and transport channel posteriors
using normalized Gaussian moment matching at converging histories. This is a
declared interacting-model approximation, not exact switching likelihood.
This module makes no admission decisions; physical ADC/ASRC covariance and
endpoint policy remain the caller's scope.
"""
from __future__ import annotations

import math
import time
import weakref

import numpy as np
import torch

from triad_incremental_receiver import IncrementalReceiver
from triad_timing_forward import SparseTimingForward, _logsumexp


def _integer(value, name, low=1, high=(1 << 64)-1):
    if isinstance(value, bool) or not isinstance(value, (int, np.integer)) or not low <= int(value) <= high:
        raise ValueError(f"{name} must be an integer in [{low},{high}]")
    return int(value)


class _TimingStream:
    """Consume a contiguous observation using a public sparse timing model.

    ``basis_provider(states, anchor_seconds, first, stop, origin, offsets)``
    returns ``[H0/bit0/bit1,state,tap,sample]`` complex templates. ``first`` and
    ``stop`` are the original logical sample indices, never a restarted pattern
    clock. Physical sample coordinates are ``origin + offsets`` in units of
    ``sample_rate``. Keeping the integer origin separate preserves fractional
    coordinates at large indices. The provider must include the audio-center
    time-warp phase and return an exactly zero H0 basis.

    The supplied ``timing_forward`` owns all timing and global-model priors;
    no candidate prior is applied again to the segment emissions. Initial
    timing states refer to the first segment's physical midpoint. A single
    zero-rate initial state is a public calibrated-start assumption.

    Mapped input may be required with ``require_physical_map``. Once a nonempty
    mapped chunk is supplied, every later chunk must carry its map. Uniform
    input is supported for explicit sampled simulations. An omitted map never
    silently replaces a previously used map.

    DFT projection conservatively discards each complete likelihood block
    containing any invalid input row, including a merged terminal block. This
    preserves the maintained projection's Gaussian covariance convention;
    erased samples are never treated as observed zeros. A contiguous outage
    can discard additional valid rows in at most its two edge blocks. With
    fragmented loss, every intersected block can be discarded. ``valid-rows``
    projection instead retains every valid row without this extra loss.
    """

    def __init__(self, *, sample_rate, samples, block_samples, segment_samples,
                 basis_provider, timing_forward, required_bands,
                 excluded_bands=(), noise_variance=1., stationary_power=(1.,),
                 coherence_seconds=(.5, math.inf), device="cpu", precision="fp32",
                 projection_mode="dft", memory_limit_bytes=512*1024**2,
                 maximum_input_chunk_samples=1_048_576,
                 maximum_segment_samples=1_048_576,
                 require_physical_map=False, maximum_map_step_samples=2.,
                 hypothesis_executor=None, hypothesis_shards=1,
                 basis_projection_workers=1,
                 quiet_sample_bounds=None, bulk_statistics=None, native_gaussian=None):
        began = time.perf_counter()
        self.sample_rate = float(sample_rate)
        if not math.isfinite(self.sample_rate) or self.sample_rate <= 0:
            raise ValueError("sample rate must be finite and positive")
        self.samples = _integer(samples, "sample count")
        bounds = (0, self.samples) if quiet_sample_bounds is None else quiet_sample_bounds
        if not isinstance(bounds, (tuple, list)) or len(bounds) != 2:
            raise ValueError('quiet sample bounds need two local endpoints')
        self.quiet_sample_bounds = tuple(_integer(x, 'quiet sample bound', low=0,
            high=self.samples) for x in bounds)
        if self.quiet_sample_bounds[0] > self.quiet_sample_bounds[1]:
            raise ValueError('quiet sample bounds must be ordered')
        self.block_samples = _integer(block_samples, "block sample count")
        self.segment_samples = _integer(segment_samples, "segment sample count")
        self.maximum_input_chunk_samples = _integer(maximum_input_chunk_samples,
            "input chunk limit", high=1_048_576)
        cap = _integer(maximum_segment_samples, "segment storage limit", high=1_048_576)
        self.segment_capacity = min(self.samples, self.segment_samples)
        if self.segment_capacity > cap:
            raise MemoryError("segment exceeds the declared bounded sample storage")
        if not callable(basis_provider):
            raise TypeError("mapped timing basis provider must be callable")
        if not isinstance(timing_forward, SparseTimingForward):
            raise TypeError("tracking requires a public SparseTimingForward instance")
        if timing_forward.observations or timing_forward.predictions:
            raise ValueError("tracking requires an unused timing-forward prior")
        self.coherences = tuple(float(value) for value in coherence_seconds)
        if len(self.coherences) != timing_forward.model_count:
            raise ValueError("global timing-model count must match coherence alternatives")
        self.timing = timing_forward
        self.basis_provider = basis_provider
        self.require_physical_map = bool(require_physical_map)
        self.maximum_map_step_samples = float(maximum_map_step_samples)
        if not math.isfinite(self.maximum_map_step_samples) or not 1 <= self.maximum_map_step_samples <= 16:
            raise ValueError("maximum physical sample step must be in [1,16]")
        self.precision = precision
        self.dtype = np.dtype(np.complex64 if precision == "fp32" else np.complex128)
        self.memory_limit_bytes = _integer(memory_limit_bytes, "workspace bytes")
        # One segment plus an independently supplied/conversion-sized input
        # chunk; this bound is independent of total observation duration.
        buffer_bytes = (self.segment_capacity + self.maximum_input_chunk_samples) * (self.dtype.itemsize + 9)
        # Conservative Python/dict/array allowance for the pure forward step.
        # This is a workspace estimate, not an allocator/RSS certificate.
        transition_cap = 2 * timing_forward.maximum_states * (2*timing_forward.rate_index_bound+1)
        timing_bytes = transition_cap * (448 + 16*timing_forward.model_count)
        if buffer_bytes + timing_bytes >= self.memory_limit_bytes:
            raise MemoryError("tracking buffers and timing transition workspace exceed memory limit")
        self.receiver_arguments = dict(sample_rate=self.sample_rate,
            block_samples=self.block_samples, required_bands=tuple(required_bands),
            excluded_bands=tuple(excluded_bands), noise_variance=noise_variance,
            stationary_power=tuple(stationary_power), coherence_seconds=self.coherences,
            device=device, precision=precision, projection_mode=projection_mode,
            memory_limit_bytes=self.memory_limit_bytes-buffer_bytes-timing_bytes,
            maximum_candidates=timing_forward.maximum_proposals,
            maximum_input_chunk_samples=self.segment_capacity,
            hypothesis_executor=hypothesis_executor, hypothesis_shards=hypothesis_shards,
            basis_projection_workers=basis_projection_workers,
            bulk_statistics=bulk_statistics, native_gaussian=native_gaussian)
        # The maintained receiver validates arithmetic, bands, prior and its
        # worst candidate workspace without synthesizing any source template.
        probe = IncrementalReceiver(samples=self.segment_capacity,
            candidate_count=timing_forward.maximum_proposals,
            basis=lambda start, stop: None, **self.receiver_arguments)
        self.owned_bytes_estimate = buffer_bytes + timing_bytes + probe.owned_bytes_estimate
        self._values = np.empty(self.segment_capacity, dtype=self.dtype)
        self._valid = np.empty(self.segment_capacity, dtype=np.uint8)
        self._offsets = np.empty(self.segment_capacity, dtype=np.float64)
        self._pending = 0
        self._segment_origin = None
        self._last_physical_point = None
        self._previous_anchor = None
        self._previous_last_point = None
        self._map_mode = None
        self._failed = False
        self.next_input_sample = 0
        self.processed_rows = self.observed_rows = self.erased_rows = 0
        self.requested_invalid_rows = self.processed_requested_invalid_rows = 0
        self.additional_discarded_rows = self.expanded_erasure_blocks = 0
        self.segment_updates = self.block_updates = 0
        self.projected_energy = 0.
        self.observed_projection_modes = 0
        self.quiet_projected_energy = 0.
        self.quiet_observed_projection_modes = 0
        self.initial_state_count = len(timing_forward.states)
        self.peak_emission_states = self.terminal_emission_states = 0
        self.peak_retained_states = 0
        self.hypothesis_update_tasks = self.maximum_hypothesis_tasks_per_block = 0
        self.complete_observation = False
        self.push_cpu_seconds = self.segment_cpu_seconds = 0.
        self.maximum_segment_cpu_seconds = self.maximum_segment_wall_seconds = 0.
        self.peak_pending_rows = 0
        self.mapped_samples = 0
        self.preparation_seconds = time.perf_counter()-began

    def _map(self, origin, offsets, count):
        supplied = origin is not None or offsets is not None
        if supplied and (origin is None or offsets is None):
            raise ValueError("physical origin and offsets must be supplied together")
        mode = "mapped" if supplied else "uniform"
        if self.require_physical_map and mode != "mapped":
            raise ValueError("this receiver requires an explicit physical sample map")
        if self._map_mode is not None and self._map_mode != mode:
            raise ValueError("physical sample map cannot be omitted or introduced mid-observation")
        if supplied:
            origin = _integer(origin, "physical sample origin", low=0)
            offsets = np.asarray(offsets, dtype=np.float64)
            if offsets.shape != (count,) or not np.isfinite(offsets).all() or (offsets < 0).any():
                raise ValueError("physical sample offsets must be a matching finite nonnegative vector")
            if (offsets > self.maximum_map_step_samples*self.maximum_input_chunk_samples).any():
                raise ValueError("physical sample offsets must be bounded; rebase their integer origin")
        else:
            origin = self.next_input_sample
            offsets = np.arange(count, dtype=np.float64)
        if count:
            differences = np.diff(offsets)
            if ((differences <= 0).any() or (differences > self.maximum_map_step_samples).any() or
                offsets[-1]-offsets[0] > self.maximum_map_step_samples*self.maximum_input_chunk_samples):
                raise ValueError("physical sample map must increase within the declared step bound")
            first_whole = math.floor(float(offsets[0]))
            last_whole = math.floor(float(offsets[-1]))
            if origin+last_whole >= (1 << 64):
                raise OverflowError("physical sample coordinate exhausted")
            first_point = (origin+first_whole, float(offsets[0])-first_whole)
            last_point = (origin+last_whole, float(offsets[-1])-last_whole)
            if self._last_physical_point is not None:
                previous_whole, previous_fraction = self._last_physical_point
                whole_step = first_point[0]-previous_whole
                # Integer subtraction precedes conversion; never subtract two
                # rounded absolute FP64 source coordinates.
                step = whole_step + first_point[1]-previous_fraction
                if not 0 < step <= self.maximum_map_step_samples:
                    raise ValueError("physical sample map is discontinuous or exceeds its step bound")
        else:
            last_point = self._last_physical_point
        return origin, offsets, mode, last_point

    def _score_segment(self, rows):
        started, cpu = time.perf_counter(), time.process_time()
        first = self.processed_rows
        origin = self._segment_origin
        offsets = self._offsets[:rows]
        midpoint = .5*(float(offsets[0])+float(offsets[-1]))
        if self._previous_anchor is not None:
            previous_origin, previous_offset = self._previous_anchor
            elapsed = ((origin-previous_origin)+(midpoint-previous_offset))/self.sample_rate
            if not math.isfinite(elapsed) or elapsed <= 0:
                raise ValueError("physical segment anchors must strictly increase")
            last_origin, last_offset = self._previous_last_point
            boundary_from_previous_anchor = .5*((last_origin-previous_origin)+(last_offset-previous_offset) +
                (origin-previous_origin)+(float(offsets[0])-previous_offset))/self.sample_rate
            self._predict_timing(elapsed, boundary_from_previous_anchor)
        states = self.timing.states
        self.terminal_emission_states = len(states)
        self.peak_emission_states = max(self.peak_emission_states, len(states))
        # This scalar is a convenience for source basis generation. The
        # authoritative map remains integer-origin plus relative offsets, and
        # elapsed times above never subtract large absolute floating values.
        anchor_seconds = (np.longdouble(origin)+np.longdouble(midpoint))/np.longdouble(self.sample_rate)

        def render(lo, hi):
            basis = self.basis_provider(states, anchor_seconds,
                first+lo, first+hi, origin, offsets[lo:hi])
            if self.receiver_arguments['bulk_statistics'] is None:
                basis = np.asarray(basis, dtype=np.complex128)
            if basis.shape != (3, len(states), len(self.receiver_arguments["stationary_power"]), hi-lo):
                raise ValueError("tracking basis must return [3,state,tap,block_rows]")
            if self.receiver_arguments['bulk_statistics'] is None and np.any(basis[0] != 0):
                raise ValueError("tracking's common Gaussian noise null requires an exactly zero H0 basis")
            return basis

        statistics = getattr(self.basis_provider, 'block_statistics', None)
        if statistics is not None:
            def block_statistics(requests):
                return statistics(states, anchor_seconds, [
                    (first+lo, first+hi, origin, offsets[lo:hi], projection, observation, variance)
                    for lo, hi, projection, observation, variance in requests])
            render.block_statistics = block_statistics

        receiver = self._make_segment_receiver(rows, states, render, origin, offsets)
        requested_invalid = rows-int(np.count_nonzero(self._valid[:rows]))
        extra_discarded = expanded_blocks = 0
        if self.receiver_arguments["projection_mode"] == "dft" and requested_invalid:
            # Reuse the actual receiver calendar rather than assuming every
            # block has block_samples rows: short terminal remainders merge.
            # The segment mask is disposable after scoring, so no additional
            # duration-dependent workspace or copy is needed.
            for index in range(receiver.block_count):
                lo, hi = receiver._block_bounds(index)
                valid_count = int(np.count_nonzero(self._valid[lo:hi]))
                if 0 < valid_count < hi-lo:
                    self._valid[lo:hi] = 0
                    extra_discarded += valid_count
                    expanded_blocks += 1
        receiver.push(0, self._values[:rows], self._valid[:rows])
        result = receiver.finish()
        if not result["complete_observation"]:
            raise RuntimeError("complete tracking segment was not fully scored")
        unmixed = receiver.unmixed_log_evidence()
        # H0 templates are identically zero, hence every H0 column shares the
        # same known noise law and is exactly zero in this likelihood ratio.
        if np.any(unmixed[:, 0] != 0):
            raise FloatingPointError("tracking segment's common null acquired nonzero evidence")
        self._observe_segment(receiver, unmixed, origin, offsets, rows)
        self.peak_retained_states = max(self.peak_retained_states, len(self.timing.states))
        self.hypothesis_update_tasks += result['hypothesis_update_tasks']
        self.maximum_hypothesis_tasks_per_block = max(self.maximum_hypothesis_tasks_per_block,
            result['hypothesis_update_tasks_per_block'])
        self._previous_anchor = (origin, midpoint)
        self._previous_last_point = (origin, float(offsets[-1]))
        self.segment_updates += 1
        self.block_updates += result["blocks"]
        self.projected_energy += result['projected_energy']
        self.observed_projection_modes += result['observed_projection_modes']
        self.quiet_projected_energy += result['quiet_projected_energy']
        self.quiet_observed_projection_modes += result['quiet_observed_projection_modes']
        self.processed_rows += rows
        self.observed_rows += result["observed_rows"]
        self.erased_rows += result["erased_rows"]
        self.processed_requested_invalid_rows += requested_invalid
        self.additional_discarded_rows += extra_discarded
        self.expanded_erasure_blocks += expanded_blocks
        elapsed_cpu = time.process_time()-cpu
        self.segment_cpu_seconds += elapsed_cpu
        self.maximum_segment_cpu_seconds = max(self.maximum_segment_cpu_seconds, elapsed_cpu)
        self.maximum_segment_wall_seconds = max(self.maximum_segment_wall_seconds, time.perf_counter()-started)

    def _predict_timing(self, elapsed, boundary_from_previous_anchor=None):
        raise NotImplementedError("timing stream requires a persistent lane")

    def _make_segment_receiver(self, rows, states, render, origin, offsets):
        raise NotImplementedError("timing stream requires a persistent lane")

    def _observe_segment(self, receiver, unmixed, origin, offsets, rows):
        raise NotImplementedError("timing stream requires a persistent lane")

    def push(self, first_sample, values, valid=None, *, physical_origin=None, physical_offsets=None):
        started = time.process_time()
        if self._failed or self.complete_observation:
            raise RuntimeError("cannot push after tracking failure or completed finish")
        first = _integer(first_sample, "first sample", low=0)
        if first != self.next_input_sample:
            raise ValueError("tracking input must be contiguous from sample zero")
        source = np.asarray(values)
        if source.ndim != 1 or source.size > self.maximum_input_chunk_samples:
            raise ValueError("tracking input chunk must be a bounded one-dimensional vector")
        if first+source.size > self.samples:
            raise ValueError("tracking input exceeds the declared observation endpoint")
        if not np.isfinite(source).all():
            raise ValueError("tracking input contains nonfinite samples")
        if valid is None:
            mask = np.ones(source.size, dtype=np.uint8)
        else:
            mask = np.asarray(valid)
            if mask.shape != source.shape or not np.isin(mask, (0, 1, False, True)).all():
                raise ValueError("tracking validity must be a matching binary vector")
            mask = mask.astype(np.uint8, copy=False)
        origin, offsets, mode, last_point = self._map(physical_origin, physical_offsets, len(source))
        source = np.asarray(source, dtype=self.dtype)
        if not np.isfinite(source).all():
            raise ValueError("tracking input overflows the selected arithmetic precision")
        consumed = 0
        try:
            while consumed < len(source):
                rows = min(self.segment_samples, self.samples-self.processed_rows)
                take = min(rows-self._pending, len(source)-consumed)
                if take <= 0:
                    raise RuntimeError("tracking segment calendar did not advance")
                if self._pending == 0:
                    self._segment_origin = origin+math.floor(float(offsets[consumed]))
                lo, hi = self._pending, self._pending+take
                self._values[lo:hi] = source[consumed:consumed+take]
                self._valid[lo:hi] = mask[consumed:consumed+take]
                self.requested_invalid_rows += take-int(np.count_nonzero(mask[consumed:consumed+take]))
                self._offsets[lo:hi] = (origin-self._segment_origin)+offsets[consumed:consumed+take]
                self._pending += take
                self.peak_pending_rows = max(self.peak_pending_rows, self._pending)
                self.next_input_sample += take
                consumed += take
                if self._pending == rows:
                    self._score_segment(rows)
                    self._pending = 0
            if len(source):
                self._map_mode = mode
                self._last_physical_point = last_point
                self.mapped_samples += len(source) if mode == "mapped" else 0
        except BaseException:
            # A failed segment can already have advanced the timing prior.
            # Continuing would silently omit or double-count observations.
            self._failed = True
            raise
        finally:
            self.push_cpu_seconds += time.process_time()-started

    def progress(self):
        """Bounded diagnostic state; no data-dependent admission or restart."""
        return dict(status="failed" if self._failed else "prefix_diagnostic",
            complete_observation=self.complete_observation, admission=None,
            bit_scores=self.timing.scores().tolist(), timing=self.timing.report(),
            consumed_samples=self.next_input_sample, processed_rows=self.processed_rows,
            scored_rows=self.processed_rows, observed_rows=self.observed_rows,
            erased_rows=self.erased_rows, blocks=self.block_updates, segments=self.segment_updates,
            projected_energy=self.projected_energy,
            observed_projection_modes=self.observed_projection_modes,
            quiet_projected_energy=self.quiet_projected_energy,
            quiet_observed_projection_modes=self.quiet_observed_projection_modes,
            quiet_sample_bounds=self.quiet_sample_bounds,
            state_work_regime=dict(initial_states=self.initial_state_count,
                peak_pre_emission_states=self.peak_emission_states,
                terminal_pre_emission_states=self.terminal_emission_states,
                peak_post_observation_states=self.peak_retained_states,
                terminal_post_observation_states=len(self.timing.states),
                state_cap=self.timing.maximum_states, proposal_cap=self.timing.maximum_proposals,
                attained_state_cap=self.peak_retained_states >= self.timing.maximum_states,
                attained_proposal_cap=self.peak_emission_states >= self.timing.maximum_proposals,
                coherence_models=len(self.coherences),
                requested_candidate_shards=self.receiver_arguments['hypothesis_shards'],
                maximum_hypothesis_tasks_per_block=self.maximum_hypothesis_tasks_per_block),
            requested_invalid_rows=self.requested_invalid_rows,
            processed_requested_invalid_rows=self.processed_requested_invalid_rows,
            additional_discarded_rows=self.additional_discarded_rows,
            expanded_erasure_blocks=self.expanded_erasure_blocks,
            erasure_policy=("Discard complete DFT likelihood blocks intersecting invalid rows; at most two extra edge blocks per contiguous outage" if
                self.receiver_arguments["projection_mode"] == "dft" else "Retain each valid raw row; no erasure expansion"),
            pending_rows=self._pending, peak_pending_rows=self.peak_pending_rows,
            working_bytes_estimate=self.owned_bytes_estimate,
            preparation_seconds=self.preparation_seconds,
            push_process_cpu_seconds=self.push_cpu_seconds,
            segment_process_cpu_seconds=self.segment_cpu_seconds,
            maximum_segment_cpu_seconds=self.maximum_segment_cpu_seconds,
            maximum_segment_wall_seconds=self.maximum_segment_wall_seconds,
            physical_coordinate_mode=self._map_mode or "not_observed",
            mapped_samples=self.mapped_samples,
            channel_model="Independent Gaussian segment priors; maintained Gaussian evolution within each segment; global bit/coherence choice",
            timing_model="Public piecewise-affine source offset/rate; retained subprobability path sum without pruning renormalization",
            noise_scope="Known logical-row Gaussian null only; physical/ASRC colored-noise covariance remains unqualified")

    def finish(self):
        self.complete_observation = (not self._failed and self.next_input_sample == self.samples and
            self.processed_rows == self.samples and self._pending == 0)
        result = self.progress()
        result["status"] = "scored" if self.complete_observation else "failed" if self._failed else "incomplete"
        return result



class _PersistentTimingLane(_TimingStream):
    """Carry normalized channel posteriors through public timing updates.

    Normalized predictive likelihoods refer to a bank fixed independently of
    its scored samples. With full-input adaptive selection, global pruning
    before moment matching can change an atom's predictor with the other
    selected groups; a selected/full-grid correction alone is not established
    for this lane. The fixed initial-affine lane remains separable.

    Incoming channel distributions at the same timing state are replaced by a
    proper Gaussian with their exact conditional mean and covariance (an
    interacting-model approximation).  Absolute likelihood weights are retained.
    Each subsequent predictive density is normalized conditional on past data;
    pruning only deletes mass.  Consequently the retained score remains a
    model-conditional null e-value.  It is NOT a pointwise lower bound on the
    exact, unmerged switching-channel likelihood, nor physical-noise calibration.

    ``audio_center_hz`` must match the basis provider's full audio-center warp
    phase.  Delay-grid rounding changes the slow envelope, while the posterior
    phase integrates the old/new public rates at the actual segment boundary. This
    avoids interpreting a fractional delay cell as a discontinuous carrier-phase
    jump.  No private transmitter clock trace is accepted.
    """

    def __init__(self, *, audio_center_hz=0., **arguments):
        self.audio_center_hz = float(audio_center_hz)
        if not math.isfinite(self.audio_center_hz):
            raise ValueError("audio center must be finite")
        super().__init__(**arguments)
        self._posteriors = None
        self._next_posteriors = None
        self._last_block_point = None
        self.transition_edges = self.merged_channel_components = 0
        self.basis_rows = self.candidate_block_updates = self.projection_mode_updates = 0
        self.maximum_channel_mixing_variance = 0.
        self.channel_state_bytes = self.peak_channel_state_bytes = 0
        item = self.dtype.itemsize
        states = self.timing.maximum_proposals
        models = self.timing.model_count
        taps = len(self.receiver_arguments['stationary_power'])
        # Old, transported, seeded and active states plus conditional edge
        # moments/weights.  Includes FP64 mixing weights and Python edge tuples.
        edges = self.timing.transition_workspace_state_bound
        self.posterior_workspace_bytes = (8*models*3*states*(taps+taps*taps)*item +
            edges*(160+models*2*(8+(6*taps+8*taps*taps)*item)))
        if self.owned_bytes_estimate+self.posterior_workspace_bytes > self.memory_limit_bytes:
            raise MemoryError("persistent timing/channel transport exceeds workspace limit")
        self.owned_bytes_estimate += self.posterior_workspace_bytes

    def _predict_timing(self, elapsed, boundary_from_previous_anchor=None):
        old_states = self.timing.states
        old_weights = self.timing.component_log_weights()
        self.timing.predict(elapsed)
        if self._posteriors is None:
            raise RuntimeError("persistent timing prediction has no preceding channel posterior")
        target_indices = {state: index for index, state in enumerate(self.timing.states)}
        sources, targets, logs, phases = [], [], [], []
        boundary = .5*elapsed if boundary_from_previous_anchor is None else float(boundary_from_previous_anchor)
        if not math.isfinite(boundary) or not 0 <= boundary <= elapsed:
            raise ValueError("timing segment boundary must lie between physical anchors")
        for source, state in enumerate(old_states):
            for target, probability in self.timing.transitions(state, elapsed):
                # The forward-weight prediction already traversed this same
                # public stencil once; include both passes in measured work.
                self.transition_edges += 2
                if not probability or target not in target_indices:
                    continue
                # Subtract integer delay indices before conversion, preserving
                # the local fractional displacement after long lattice travel.
                unrounded_minus_grid = ((state.delay_index-target.delay_index)*self.timing.delay_step_seconds +
                    (state.rate_index*boundary+target.rate_index*(elapsed-boundary))*self.timing.rate_step_ppm*1e-6)
                cycles = self.audio_center_hz*unrounded_minus_grid
                if not math.isfinite(cycles):
                    raise ValueError("timing carrier-gauge phase overflows")
                sources.append(source); targets.append(target_indices[target])
                logs.append(math.log(probability))
                phases.append(complex(math.cos(2*math.pi*math.remainder(cycles, 1.)),
                                      math.sin(2*math.pi*math.remainder(cycles, 1.))))
        self.merged_channel_components += max(0, len(sources)-len(self.timing.states))
        source_indices = np.asarray(sources, dtype=np.int64)
        target_indices_array = np.asarray(targets, dtype=np.int64)
        log_terms = old_weights[:, :, source_indices]+np.asarray(logs)[None, None, :]
        target_weights = self.timing.component_log_weights()[:, :, target_indices_array]
        finite = np.isfinite(log_terms) & np.isfinite(target_weights)
        weights = np.zeros_like(log_terms)
        np.exp(np.subtract(log_terms, target_weights, out=np.full_like(log_terms, -np.inf), where=finite),
               out=weights, where=finite)
        next_states = []
        for model, (mean, covariance) in enumerate(self._posteriors):
            device = mean.device
            src = torch.as_tensor(source_indices, dtype=torch.long, device=device)
            dst = torch.as_tensor(target_indices_array, dtype=torch.long, device=device)
            weight = torch.as_tensor(weights[model], dtype=mean.real.dtype, device=device)
            phase = torch.as_tensor(phases, dtype=mean.dtype, device=device)
            incoming_mean = mean[1:, src]*phase[None, :, None]
            incoming_covariance = covariance[1:, src]
            count, taps = len(self.timing.states), mean.shape[-1]
            mixed_mean = torch.zeros((2, count, taps), dtype=mean.dtype, device=device)
            mixed_mean.index_add_(1, dst, weight[..., None]*incoming_mean)
            # Center first: E[P + (m-E[m])(m-E[m])^H] avoids cancellation in
            # a well-learned channel and includes between-history uncertainty.
            difference = incoming_mean-mixed_mean[:, dst]
            spread = difference[..., :, None]*difference.conj()[..., None, :]
            mixed_covariance = torch.zeros((2, count, taps, taps), dtype=mean.dtype, device=device)
            mixed_covariance.index_add_(1, dst,
                weight[..., None, None]*(incoming_covariance+spread))
            mixed_covariance = (mixed_covariance+mixed_covariance.mH)*.5
            # A zero-prior model has no finite incoming weight.  Its arbitrary
            # normalized state cannot contribute evidence now or in the future.
            present = torch.zeros((2, count), dtype=mean.real.dtype, device=device)
            present.index_add_(1, dst, weight)
            power = torch.diag(torch.tensor(self.receiver_arguments['stationary_power'],
                dtype=mean.real.dtype, device=device)).to(mean.dtype)
            mixed_covariance = torch.where((present > 0)[..., None, None], mixed_covariance, power)
            h0_mean = torch.zeros((1, count, taps), dtype=mean.dtype, device=device)
            h0_covariance = power.expand(1, count, taps, taps)
            next_states.append((torch.cat((h0_mean, mixed_mean)),
                                torch.cat((h0_covariance, mixed_covariance))))
            if spread.numel():
                self.maximum_channel_mixing_variance = max(self.maximum_channel_mixing_variance,
                    float((weight[..., None, None]*spread).diagonal(dim1=-2, dim2=-1).real.sum(-1).amax()))
        self._next_posteriors = next_states

    def _make_segment_receiver(self, rows, states, render, origin, offsets):
        elapsed = 0.
        if self._last_block_point is not None:
            # Use this segment receiver's real terminal-merge convention.
            full = rows//self.block_samples
            first_stop = rows if full <= 1 else self.block_samples
            midpoint = .5*(float(offsets[0])+float(offsets[first_stop-1]))
            previous_origin, previous_offset = self._last_block_point
            elapsed = ((origin-previous_origin)+(midpoint-previous_offset))/self.sample_rate
            if not math.isfinite(elapsed) or elapsed <= 0:
                raise ValueError("persistent channel block centers must strictly increase")
        def counted_render(lo, hi):
            self.basis_rows += (hi-lo)*len(states)*2*len(self.receiver_arguments['stationary_power'])
            return render(lo, hi)
        statistics = getattr(render, 'block_statistics', None)
        if statistics is not None:
            def counted_statistics(requests):
                result = statistics(requests)
                if result is not None:
                    self.basis_rows += sum(hi-lo for lo, hi, *_ in requests)*len(states)*2*len(
                        self.receiver_arguments['stationary_power'])
                return result
            counted_render.block_statistics = counted_statistics
        return IncrementalReceiver(samples=rows, candidate_count=len(states), basis=counted_render,
            block_center_seconds=lambda lo, hi: .5*(float(offsets[lo])+float(offsets[hi-1]))/self.sample_rate,
            initial_channel_state=self._next_posteriors,
            quiet_sample_bounds=tuple(max(0, min(rows, x-self.processed_rows))
                for x in self.quiet_sample_bounds),
            first_block_elapsed_seconds=elapsed, **self.receiver_arguments)

    def _observe_segment(self, receiver, unmixed, origin, offsets, rows):
        states = self.timing.states
        self.timing.observe(unmixed[:, 1:])
        index = {state: column for column, state in enumerate(states)}
        chosen = [index[state] for state in self.timing.states]
        self._posteriors = []
        for runtime in receiver.runtimes:
            mean, covariance = runtime.posterior()
            self._posteriors.append((mean[:, chosen].clone(), covariance[:, chosen].clone()))
        self._next_posteriors = None
        self.channel_state_bytes = sum(value.numel()*value.element_size()
            for state in self._posteriors for value in state)
        self.peak_channel_state_bytes = max(self.peak_channel_state_bytes, self.channel_state_bytes)
        self.candidate_block_updates += receiver.block_updates*len(states)*3*self.timing.model_count
        self.projection_mode_updates += receiver.projection_modes
        lo, hi = receiver._block_bounds(receiver.block_count-1)
        self._last_block_point = (origin, .5*(float(offsets[lo])+float(offsets[hi-1])))

    def progress(self):
        result = super().progress()
        result.update(channel_model='Persistent proper Gaussian channel per timing state; conditional moment matching at history merges',
            timing_model='Public piecewise-affine envelope timing with integrated rate-path audio phase; bounded interacting-model approximation',
            evidence_scope='Normalized predictive Gaussian likelihood; no pruning-mass renormalization; not an exact unmerged path likelihood',
            audio_center_hz=self.audio_center_hz,
            channel_state_bytes=self.channel_state_bytes,
            peak_channel_state_bytes=self.peak_channel_state_bytes,
            posterior_workspace_bytes=self.posterior_workspace_bytes,
            work_counts=dict(transition_edges=self.transition_edges,
                merged_channel_components=self.merged_channel_components,
                basis_rows=self.basis_rows, candidate_block_updates=self.candidate_block_updates,
                projection_mode_updates=self.projection_mode_updates,
                hypothesis_update_tasks=self.hypothesis_update_tasks),
            maximum_channel_mixing_variance=self.maximum_channel_mixing_variance)
        return result


class _InitialAffineReceiver(_PersistentTimingLane):
    """Persistent initial public affine bank with one immutable time anchor."""

    def __init__(self, *, basis_provider, **arguments):
        self.initial_affine_anchor = None
        self.affine_elapsed_seconds = 0.
        initial_states = arguments['timing_forward'].states
        reference = weakref.ref(self)

        def anchored(provider):
            def evaluate(states, anchor, *arguments):
                current = reference()
                if current is None:
                    raise RuntimeError('initial affine lane no longer owns its basis callback')
                if tuple(states) != initial_states:
                    raise RuntimeError('fixed affine bank changed its public state coordinates')
                if current.initial_affine_anchor is None:
                    raise RuntimeError('fixed affine time anchor was not initialized before scoring')
                return provider(initial_states, current.initial_affine_anchor, *arguments)
            return evaluate

        affine_basis = anchored(basis_provider)
        statistics = getattr(basis_provider, 'block_statistics', None)
        if statistics is not None:
            affine_basis.block_statistics = anchored(statistics)
        super().__init__(basis_provider=affine_basis, **arguments)

    def _make_segment_receiver(self, rows, states, render, origin, offsets):
        if self.initial_affine_anchor is None:
            # Establish the public epoch even when every row in this segment
            # is erased and the numerical engine never requests a template.
            midpoint = .5*(float(offsets[0])+float(offsets[rows-1]))
            self.initial_affine_anchor = ((np.longdouble(origin)+np.longdouble(midpoint)) /
                                          np.longdouble(self.sample_rate))
        return super()._make_segment_receiver(rows, states, render, origin, offsets)

    def _predict_timing(self, elapsed, boundary_from_previous_anchor=None):
        # These coordinates describe the same initial affine waveform forever.
        # No stochastic delay rounding, phase-gauge change or channel reset is
        # introduced merely because a segment boundary was reached.
        self.timing.predict(0.)
        self._next_posteriors = self._posteriors
        self.affine_elapsed_seconds += elapsed
        self.transition_edges += len(self.timing.states)

    def progress(self):
        result = super().progress()
        result['timing'] = dict(result['timing'], elapsed_seconds=self.affine_elapsed_seconds)
        result.update(timing_model='Fixed initial public affine bank; original physical anchor preserved',
            initial_affine_anchor_seconds=(None if self.initial_affine_anchor is None
                                          else str(self.initial_affine_anchor)))
        return result


class PracticalTrackingReceiver:
    """Fixed half/half mixture of adaptive and initial-affine persistence.

    Slow DATA can tolerate the entire correction-induced envelope error without
    an evolving timing search.  That inexpensive public affine lane protects
    weak evidence when a broad adaptive prior loses most of its probability to
    pruning.  Both lanes consume all samples and their fixed normalized mixture
    is used from the first sample: this is never a data-selected maximum.

    For either bit, combined_log_LR >= lane_log_LR - log(2).  This is a bound on
    evidence, not a universal dB sensitivity penalty or reception guarantee.
    Public acquisition/group and global channel-model priors occur once in each
    lane, then the two lane likelihoods are mixed with their half/half prior.
    Physical ADC/ASRC covariance and endpoint policy remain the caller's scope.
    """

    def __init__(self, *, timing_forward, memory_limit_bytes=512*1024**2,
                 initial_affine_basis_provider=None, shared_basis_workspace_bytes=0, **arguments):
        began = time.perf_counter()
        if not isinstance(timing_forward, SparseTimingForward):
            raise TypeError('practical tracking requires a public timing-forward prior')
        if timing_forward.observations or timing_forward.predictions:
            raise ValueError('practical tracking requires an unused timing-forward prior')
        self.memory_limit_bytes = _integer(memory_limit_bytes, 'workspace bytes')
        shared_basis_workspace_bytes = _integer(shared_basis_workspace_bytes,
            'shared basis workspace bytes', low=0)
        lane_budget = self.memory_limit_bytes-shared_basis_workspace_bytes
        if lane_budget < 1:
            raise MemoryError('shared basis staging exhausts tracking workspace')
        weights = timing_forward.component_log_weights()
        model_probabilities = np.exp(_logsumexp(weights[:, 0, :], axis=-1))
        initial_probabilities = np.exp(_logsumexp(weights[:, 0, :], axis=0))
        count = len(timing_forward.states)
        initial_rate_bound = max(abs(state.rate_index) for state in timing_forward.states)*timing_forward.rate_step_ppm
        if initial_rate_bound:
            # The original public bound already admitted these integer rate
            # indices. Do not lose one by flooring a rounded-down product such
            # as 3*.7/.7 when narrowing this deterministic bank's workspace.
            initial_rate_bound = math.nextafter(initial_rate_bound, math.inf)
        nominal_forward = SparseTimingForward(timing_forward.states, initial_probabilities,
            model_count=timing_forward.model_count, model_probabilities=model_probabilities,
            delay_step_seconds=timing_forward.delay_step_seconds,
            rate_step_ppm=timing_forward.rate_step_ppm,
            maximum_rate_ppm=initial_rate_bound,
            maximum_acceleration_ppm_per_second=0., maximum_states=count, maximum_proposals=count)
        # Construct the small bank first. Neither constructor renders a source
        # template; each preflights its bounded storage before receiving data.
        # Its complete declared allocation is subtracted from the adaptive
        # budget, so combined owned storage never has two full-budget owners.
        nominal_arguments = dict(arguments)
        if initial_affine_basis_provider is not None:
            if not callable(initial_affine_basis_provider):
                raise TypeError('initial affine basis provider must be callable')
            nominal_arguments['basis_provider'] = initial_affine_basis_provider
        self.nominal = _InitialAffineReceiver(timing_forward=nominal_forward,
            memory_limit_bytes=lane_budget, **nominal_arguments)
        remaining = lane_budget-self.nominal.owned_bytes_estimate
        if remaining <= 0:
            raise MemoryError('initial affine lane exhausts combined tracking workspace')
        self.adaptive = _PersistentTimingLane(timing_forward=timing_forward,
            memory_limit_bytes=remaining, **arguments)
        self.owned_bytes_estimate = (shared_basis_workspace_bytes+
            self.nominal.owned_bytes_estimate+self.adaptive.owned_bytes_estimate)
        for name in ('sample_rate','samples','block_samples','segment_samples','precision',
                     'receiver_arguments','timing'):
            setattr(self, name, getattr(self.adaptive, name))
        self.preparation_seconds = time.perf_counter()-began
        self.nominal_work_plan = dict(initial_states=count, maximum_states=count,
            maximum_proposals=count, rate_index_bound=0, models=timing_forward.model_count,
            taps=len(self.receiver_arguments['stationary_power']),
            semantics='Fixed initial affine states; no stochastic rate/offset search',
            workspace_bytes=self.nominal.owned_bytes_estimate)
        full_segments, terminal_rows = divmod(self.samples, self.segment_samples)
        segments = full_segments+bool(terminal_rows)
        blocks = full_segments*max(1,self.segment_samples//self.block_samples)
        if terminal_rows:
            blocks += max(1,terminal_rows//self.block_samples)
        self.nominal_work_plan.update(segments=segments, blocks=blocks,
            transition_edges_exact_if_complete=max(0,segments-1)*count,
            candidate_block_updates_exact_if_complete=blocks*3*count*self.timing.model_count,
            basis_rows_upper=2*self.samples*count*len(self.receiver_arguments['stationary_power']))
        self._failed = False
        self.push_cpu_seconds = 0.

    def push(self, first_sample, values, valid=None, *, physical_origin=None, physical_offsets=None):
        began = time.process_time()
        if self._failed:
            raise RuntimeError('cannot push after practical tracking failure')
        try:
            self.nominal.push(first_sample, values, valid,
                physical_origin=physical_origin, physical_offsets=physical_offsets)
            self.adaptive.push(first_sample, values, valid,
                physical_origin=physical_origin, physical_offsets=physical_offsets)
        except BaseException:
            # A partial update of either lane must never be retried or treated
            # as a complete combined observation.
            self._failed = True
            raise
        finally:
            self.push_cpu_seconds += time.process_time()-began

    def _combine(self, adaptive, nominal):
        diverged = (adaptive['processed_rows'] != nominal['processed_rows'] or
                    adaptive['consumed_samples'] != nominal['consumed_samples'])
        if diverged and not self._failed:
            raise RuntimeError('practical tracking lanes diverged in sample coverage')
        mixed = np.logaddexp(adaptive['bit_scores'], nominal['bit_scores'])-math.log(2.)
        result = dict(adaptive)
        counters = set(adaptive['work_counts']) | set(nominal['work_counts'])
        result.update(bit_scores=mixed.tolist(), admission=None,
            complete_observation=(not self._failed and adaptive['complete_observation'] and nominal['complete_observation']),
            working_bytes_estimate=self.owned_bytes_estimate,
            channel_state_bytes=adaptive['channel_state_bytes']+nominal['channel_state_bytes'],
            peak_channel_state_bytes=adaptive['peak_channel_state_bytes']+nominal['peak_channel_state_bytes'],
            posterior_workspace_bytes=adaptive['posterior_workspace_bytes']+nominal['posterior_workspace_bytes'],
            preparation_seconds=self.preparation_seconds,
            push_process_cpu_seconds=self.push_cpu_seconds,
            segment_process_cpu_seconds=adaptive['segment_process_cpu_seconds']+nominal['segment_process_cpu_seconds'],
            maximum_segment_cpu_seconds=adaptive['maximum_segment_cpu_seconds']+nominal['maximum_segment_cpu_seconds'],
            maximum_segment_wall_seconds=adaptive['maximum_segment_wall_seconds']+nominal['maximum_segment_wall_seconds'],
            maximum_segment_timing_scope='Sum of per-lane maxima; conservative bound for their sequential combined segment, not a directly measured single-segment maximum',
            work_counts={key:adaptive['work_counts'].get(key,0)+nominal['work_counts'].get(key,0) for key in counters},
            channel_model='Fixed half/half mixture of adaptive and initial-affine persistent Gaussian likelihoods',
            evidence_scope='Normalized two-lane mixture; combined bit log-LR is at least either lane minus ln(2); no max-of-lanes decision',
            lane_probabilities=[.5,.5], nominal_work_plan=self.nominal_work_plan,
            lanes=dict(adaptive=adaptive, initial_affine=nominal))
        if self._failed:
            result.update(status='failed', complete_observation=False,bit_scores=None,
                processed_rows=min(adaptive['processed_rows'],nominal['processed_rows']),
                scored_rows=min(adaptive['processed_rows'],nominal['processed_rows']),
                consumed_samples=min(adaptive['consumed_samples'],nominal['consumed_samples']))
        return result

    def progress(self):
        return self._combine(self.adaptive.progress(), self.nominal.progress())

    def finish(self):
        result = self._combine(self.adaptive.finish(), self.nominal.finish())
        result['status'] = 'failed' if self._failed else 'scored' if result['complete_observation'] else 'incomplete'
        return result
