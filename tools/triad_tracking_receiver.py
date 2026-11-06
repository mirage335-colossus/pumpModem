#!/usr/bin/env python3
"""Bounded segment likelihoods for the triad public timing-state model.

Channel coefficients are independently marginalized at declared segment
boundaries. Within a segment the maintained Gaussian receiver retains its
channel state. The timing model, bit and global coherence choice continue
across segments; Gaussian posteriors from different timing histories are never
moment-merged or passed off as an exact switching-channel likelihood.

This module makes no admission decisions. Its Gaussian noise null is still a
conditional model, especially after physical asynchronous sample conversion.
"""
from __future__ import annotations

import math
import time

import numpy as np

from triad_incremental_receiver import IncrementalReceiver
from triad_timing_forward import SparseTimingForward


def _integer(value, name, low=1, high=(1 << 64)-1):
    if isinstance(value, bool) or not isinstance(value, (int, np.integer)) or not low <= int(value) <= high:
        raise ValueError(f"{name} must be an integer in [{low},{high}]")
    return int(value)


class TrackingReceiver:
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
                 require_physical_map=False, maximum_map_step_samples=2.):
        began = time.perf_counter()
        self.sample_rate = float(sample_rate)
        if not math.isfinite(self.sample_rate) or self.sample_rate <= 0:
            raise ValueError("sample rate must be finite and positive")
        self.samples = _integer(samples, "sample count")
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
            maximum_input_chunk_samples=self.segment_capacity)
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
        self._map_mode = None
        self._failed = False
        self.next_input_sample = 0
        self.processed_rows = self.observed_rows = self.erased_rows = 0
        self.requested_invalid_rows = self.processed_requested_invalid_rows = 0
        self.additional_discarded_rows = self.expanded_erasure_blocks = 0
        self.segment_updates = self.block_updates = 0
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
            self.timing.predict(elapsed)
        states = self.timing.states
        # This scalar is a convenience for source basis generation. The
        # authoritative map remains integer-origin plus relative offsets, and
        # elapsed times above never subtract large absolute floating values.
        anchor_seconds = (np.longdouble(origin)+np.longdouble(midpoint))/np.longdouble(self.sample_rate)

        def render(lo, hi):
            basis = np.asarray(self.basis_provider(states, anchor_seconds,
                first+lo, first+hi, origin, offsets[lo:hi]), dtype=np.complex128)
            if basis.shape != (3, len(states), len(self.receiver_arguments["stationary_power"]), hi-lo):
                raise ValueError("tracking basis must return [3,state,tap,block_rows]")
            if np.any(basis[0] != 0):
                raise ValueError("tracking's common Gaussian noise null requires an exactly zero H0 basis")
            return basis

        receiver = IncrementalReceiver(samples=rows, candidate_count=len(states), basis=render,
            block_center_seconds=lambda lo, hi: .5*(float(offsets[lo])+float(offsets[hi-1]))/self.sample_rate,
            **self.receiver_arguments)
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
        self.timing.observe(unmixed[:, 1:])
        self._previous_anchor = (origin, midpoint)
        self.segment_updates += 1
        self.block_updates += result["blocks"]
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


SegmentedTimingReceiver = TrackingReceiver
