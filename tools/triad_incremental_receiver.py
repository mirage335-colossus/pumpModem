#!/usr/bin/env python3
"""Bounded-memory blockwise Gaussian likelihood for a continuous triad input.

This component preserves likelihood state across the declared observation. It
does not repeatedly admit on prefixes and it does not implement the operational
threshold/search-family policy. Callers must keep the public candidate/model
bank fixed from the first sample through the endpoint.
"""
from __future__ import annotations

import math
import time

import numpy as np
import torch

from triad_dynamic_joint import BlockProjection
from triad_gaussian_state import GaussianPrior, GaussianStateFilter


def fixed_block_bounds(samples, block_samples, index):
    """Canonical finite DFT calendar, merging the terminal short remainder."""
    if (type(samples) is not int or type(block_samples) is not int or
            not 1 <= samples < 1 << 64 or not 1 <= block_samples < 1 << 64):
        raise ValueError('positive finite sample and block counts required')
    count = max(1, samples//block_samples)
    if type(index) is not int or not 0 <= index < count:
        raise IndexError('block index outside fixed observation')
    start = index*block_samples
    return start, samples if index == count-1 else start+block_samples


class _CandidateGaussianFilter:
    """Independent candidate slices with unchanged per-candidate recursion."""
    def __init__(self, prior, candidates, shards, dtype):
        self.parts = []
        for indices in np.array_split(np.arange(candidates), min(candidates, shards)):
            selected = slice(int(indices[0]), int(indices[-1])+1)
            self.parts.append((selected, GaussianStateFilter(prior, dtype=dtype, check_inputs=True)))

    def initialize_posterior(self, mean, covariance):
        for selected, runtime in self.parts:
            runtime.initialize_posterior(mean[:, selected], covariance[:, selected])

    def jobs(self, correlations, gram, elapsed):
        return [(runtime, correlations[:, selected], gram[:, selected], elapsed)
                for selected, runtime in self.parts]

    def scores(self):
        return torch.cat([runtime.scores() for _, runtime in self.parts], dim=-1)

    def posterior(self):
        states = [runtime.posterior() for _, runtime in self.parts]
        return tuple(torch.cat([state[index] for state in states], dim=1) for index in (0, 1))

    def finish(self):
        for _, runtime in self.parts:
            runtime.finish()
        return self.scores()


class IncrementalGaussianReceiver:
    """Consume one continuous observation with a fixed candidate bank.

    ``basis(start, stop)`` returns the public basis for exactly one block in
    ``[H0, bit0, bit1, candidate, tap, raw_sample]`` order. Only the current
    block basis and one bounded input remainder are retained. A final short
    block is merged with its predecessor just as in ``ProjectedJointReceiver``.

    ``push`` accepts contiguous chunks beginning at sample zero. Prefix scores
    are available for diagnostics only; this class never makes an admission.
    ``finish`` returns complete likelihood evidence only at the declared
    endpoint. Early EOF remains incomplete.

    ``block_center_seconds(start, stop)`` optionally supplies known physical
    block times for channel evolution after sample-clock conversion. Values
    must be finite, nonnegative and increasing; a segment-relative time origin
    avoids loss of precision in long observations. The default uses the
    original integer logical-sample calendar.
    """

    def __init__(self, *, sample_rate, samples, block_samples,
                 basis, required_bands, excluded_bands=(), noise_variance=1.,
                 stationary_power=(.5, .5), coherence_seconds=(.5, math.inf),
                 device="cpu", precision="fp32", memory_limit_bytes=512*1024**2,
                 maximum_candidates=4096, candidate_count=1,
                 projection_mode="dft", maximum_input_chunk_samples=1_048_576,
                 block_center_seconds=None, initial_channel_state=None,
                 first_block_elapsed_seconds=0., hypothesis_executor=None,
                 hypothesis_shards=1, quiet_sample_bounds=None):
        started = time.perf_counter()
        self.sample_rate = float(sample_rate)
        if not math.isfinite(self.sample_rate) or self.sample_rate <= 0:
            raise ValueError("sample rate must be finite and positive")
        if isinstance(samples, bool) or not isinstance(samples, int) or not 1 <= samples < (1 << 64):
            raise ValueError("sample count must be a positive integer")
        if isinstance(block_samples, bool) or not isinstance(block_samples, int) or not 1 <= block_samples < (1 << 64):
            raise ValueError("block size must be a positive integer")
        if isinstance(candidate_count, bool) or not isinstance(candidate_count, int) or not 1 <= candidate_count <= maximum_candidates:
            raise ValueError("candidate count exceeds the declared finite bank")
        if precision not in ("fp32", "fp64"):
            raise ValueError("precision must be fp32 or fp64")
        if projection_mode not in ("dft", "valid-rows"):
            raise ValueError("projection mode must be dft or valid-rows")
        if projection_mode == "valid-rows" and excluded_bands:
            raise ValueError("raw-row selection cannot exclude auxiliary bands")
        self.variance = float(noise_variance)
        if not math.isfinite(self.variance) or self.variance <= 0:
            raise ValueError("noise variance must be finite and positive")
        if not isinstance(maximum_input_chunk_samples, int) or not 1 <= maximum_input_chunk_samples <= 1_048_576:
            raise ValueError("input chunk limit must be in [1,1048576]")
        self.samples, self.block_samples = samples, block_samples
        bounds = (0, samples) if quiet_sample_bounds is None else quiet_sample_bounds
        if (not isinstance(bounds, (tuple, list)) or len(bounds) != 2 or
                any(isinstance(x, bool) or not isinstance(x, (int, np.integer)) for x in bounds) or
                not 0 <= bounds[0] <= bounds[1] <= samples):
            raise ValueError('quiet sample bounds must be an ordered local interval within the observation')
        self.quiet_sample_bounds = tuple(int(x) for x in bounds)
        powers = tuple(float(x) for x in stationary_power)
        self.candidates, self.taps = candidate_count, len(powers)
        if not 1 <= self.taps <= 16:
            raise ValueError("receiver needs 1..16 channel taps")
        coherences = tuple(float(x) for x in coherence_seconds)
        if not coherences or len(set(coherences)) != len(coherences) or any(not x > 0 or math.isnan(x) for x in coherences):
            raise ValueError("coherences must be unique and positive or infinite")
        for coherence in coherences:
            GaussianPrior(powers, coherence)
        self.coherences = coherences
        self.precision = precision
        self.dtype = torch.complex64 if precision == "fp32" else torch.complex128
        self.real_dtype = torch.float32 if precision == "fp32" else torch.float64
        self.device = torch.device(device)
        if self.device.type == "cuda":
            if not torch.cuda.is_available():
                raise RuntimeError("requested CUDA device is unavailable")
            if self.device.index is None:
                self.device = torch.device("cuda", torch.cuda.current_device())
            if precision == "fp32" and (torch.backends.cuda.matmul.allow_tf32 or
                    torch.get_float32_matmul_precision() != "highest"):
                raise ValueError("FP32 minimum requires TF32 disabled and highest matmul precision")
        self.projection_mode = projection_mode
        self.memory_limit_bytes = int(memory_limit_bytes)
        if hypothesis_executor is not None and not callable(getattr(hypothesis_executor, 'submit', None)):
            raise TypeError('hypothesis executor must support submit')
        self.hypothesis_executor = hypothesis_executor
        if (isinstance(hypothesis_shards, bool) or not isinstance(hypothesis_shards, int) or
                not 1 <= hypothesis_shards <= 1024):
            raise ValueError('hypothesis shards must be in[1,1024]')
        if hypothesis_shards > 1 and hypothesis_executor is None:
            raise ValueError('candidate shards require the shared hypothesis executor')
        self.hypothesis_shards = min(hypothesis_shards, candidate_count)
        if self.memory_limit_bytes <= 0:
            raise MemoryError("positive workspace memory budget required")
        self.maximum_input_chunk_samples = maximum_input_chunk_samples
        if block_center_seconds is not None and not callable(block_center_seconds):
            raise TypeError("physical block-center provider must be callable")
        self.block_center_seconds = block_center_seconds
        self.basis_callback = basis
        # Determine the public block calendar without retaining an O(duration)
        # layout table. A short terminal remainder is merged into its previous
        # full block, matching the batch receiver's DFT noise convention.
        full, remainder = divmod(samples, block_samples)
        if full == 0:
            self.block_count = 1
        elif remainder:
            self.block_count = full
        else:
            self.block_count = full
        self.merge_terminal_remainder = bool(full and remainder)
        if samples < block_samples:
            self.maximum_block_rows = samples
        elif self.merge_terminal_remainder:
            self.maximum_block_rows = block_samples + remainder
        else:
            self.maximum_block_rows = block_samples
        # Conservative preflight for input, complex basis, projected basis,
        # transform scratch and the one-block state. Do this before calling the
        # basis provider, which may synthesize keyed templates.
        hck = 3 * candidate_count * self.taps
        item = 8 if precision == "fp32" else 16
        block = self.maximum_block_rows
        estimate = (block * (item + 16) + hck * block * (item * 2 + 24) +
                    3 * candidate_count * self.taps * self.taps * (item * 4 + 64) +
                    len(coherences) * 3 * candidate_count * (self.taps * self.taps * item * 8 + self.taps * item * 4 + 64))
        if hypothesis_executor is not None:
            estimate += len(coherences)*3*candidate_count*(
                self.taps*self.taps*item*12+self.taps*item*12+64)
        if estimate > self.memory_limit_bytes:
            raise MemoryError("incremental Gaussian block workspace exceeds memory limit before basis generation")
        self.owned_bytes_estimate = estimate
        self.projection = None
        if projection_mode == "dft":
            # Every full DFT block has the same projection. For a merged final
            # block, construct that size only when it is reached.
            self.projection = BlockProjection.make(min(block_samples, samples), self.sample_rate,
                required_bands, excluded_bands)
        self.required_bands = tuple(tuple(x) for x in required_bands)
        self.excluded_bands = tuple(tuple(x) for x in excluded_bands)
        self.runtimes = [(_CandidateGaussianFilter(GaussianPrior(powers, coh),
            candidate_count, self.hypothesis_shards, self.dtype) if self.hypothesis_shards > 1
            else GaussianStateFilter(GaussianPrior(powers, coh), dtype=self.dtype, check_inputs=True))
            for coh in coherences]
        self.first_block_elapsed_seconds = float(first_block_elapsed_seconds)
        if not math.isfinite(self.first_block_elapsed_seconds) or self.first_block_elapsed_seconds < 0:
            raise ValueError("initial channel elapsed time must be finite and nonnegative")
        if initial_channel_state is not None:
            if len(initial_channel_state) != len(self.runtimes):
                raise ValueError("initial channel state must match the coherence alternatives")
            shape = (3, self.candidates, self.taps)
            for runtime, (mean, covariance) in zip(self.runtimes, initial_channel_state):
                mean = torch.as_tensor(mean, dtype=self.dtype, device=self.device)
                covariance = torch.as_tensor(covariance, dtype=self.dtype, device=self.device)
                if tuple(mean.shape) != shape:
                    raise ValueError("initial channel state must have [3,candidate,tap] shape")
                runtime.initialize_posterior(mean, covariance)
        self.next_input_sample = 0
        self.current_block_index = 0
        self.pending = np.empty(0, dtype=np.complex64 if precision == "fp32" else np.complex128)
        self.pending_valid = np.empty(0, dtype=np.uint8)
        self.previous_center_twice = None
        self.previous_physical_center = None
        self.processed_rows = 0
        self.scored_rows = 0
        self.observed_rows = 0
        self.erased_rows = 0
        self.block_updates = 0
        self.projection_modes = 0
        self.observed_projection_modes = 0
        self.projected_energy = 0.
        self.quiet_observed_projection_modes = 0
        self.quiet_projected_energy = 0.
        self.complete_observation = False
        self.push_cpu_seconds = 0.
        self.preparation_seconds = time.perf_counter() - started

    def _block_bounds(self, index):
        return fixed_block_bounds(self.samples, self.block_samples, index)

    def _score_block(self, start, stop, values, valid):
        rows = stop - start
        if values.shape != (rows,) or valid.shape != (rows,):
            raise ValueError("internal block extent mismatch")
        if self.projection_mode == "dft":
            any_valid = bool(np.any(valid))
            if any_valid and not bool(np.all(valid)):
                raise ValueError("partial erasures with auxiliary DFT projection require a covariance adapter")
            projection = (self.projection if rows == self.projection.samples else
                BlockProjection.make(rows, self.sample_rate, self.required_bands, self.excluded_bands))
            if any_valid:
                projected_y = projection.numpy(values)
            else:
                projected_y = np.zeros(len(projection.indices), dtype=values.dtype)
        else:
            projection = BlockProjection.valid_rows(self.sample_rate, valid.astype(bool, copy=False))
            projected_y = values[projection.indices]
        self.projection_modes += len(projection.indices)
        if np.any(valid):
            self.observed_projection_modes += len(projection.indices)
            normalized_energy = float(np.vdot(projected_y.astype(np.complex128),
                projected_y.astype(np.complex128)).real/self.variance)
            self.projected_energy += normalized_energy
            # A complete orthogonal block retains the known-noise Gamma law.
            # Cropping a DFT after projection would change that law. This
            # public aperture affects absence evidence only, never likelihood.
            if self.quiet_sample_bounds[0] <= start and stop <= self.quiet_sample_bounds[1]:
                self.quiet_observed_projection_modes += len(projection.indices)
                self.quiet_projected_energy += normalized_energy
        self.erased_rows += int(rows - np.count_nonzero(valid))
        self.scored_rows += rows
        if len(projection.indices) == 0 or not np.any(valid):
            r = torch.zeros((3, self.candidates, self.taps), dtype=self.dtype, device=self.device)
            gram = torch.zeros((3, self.candidates, self.taps, self.taps), dtype=self.dtype, device=self.device)
        else:
            raw_basis = np.asarray(self.basis_callback(start, stop), dtype=np.complex128)
            if (raw_basis.shape != (3, self.candidates, self.taps, rows) or
                    not np.isfinite(raw_basis).all()):
                raise ValueError("basis callback must return finite [3,candidates,taps,block_rows]")
            projected_basis = projection.numpy(raw_basis)
            if not np.isfinite(projected_basis).all() or not np.isfinite(projected_y).all():
                raise ValueError("projected observations or basis are nonfinite")
            gram_np = np.einsum("hctn,hcsn->hcts", projected_basis.conj(), projected_basis,
                                optimize=True) / self.variance
            # Same complex arithmetic floor and Gram/correlation definitions as
            # the batch receiver; accumulated state likelihood remains FP64.
            y = torch.as_tensor(np.asarray(projected_y).copy(), dtype=self.dtype, device=self.device)
            b = torch.as_tensor(projected_basis, dtype=self.dtype, device=self.device)
            flat = b.reshape(-1, b.shape[-1])
            r = (y @ flat.conj().T / self.variance).reshape(3, self.candidates, self.taps)
            gram = torch.as_tensor(gram_np, dtype=self.dtype, device=self.device)
        center_twice = start + stop - 1
        if self.block_center_seconds is None:
            elapsed = self.first_block_elapsed_seconds if self.previous_center_twice is None else (
                (center_twice - self.previous_center_twice) / (2 * self.sample_rate))
        else:
            center = float(self.block_center_seconds(start, stop))
            if (not math.isfinite(center) or center < 0 or
                (self.previous_physical_center is not None and center <= self.previous_physical_center)):
                raise ValueError("physical block centers must be finite, nonnegative and strictly increasing")
            elapsed = self.first_block_elapsed_seconds if self.previous_physical_center is None else center-self.previous_physical_center
            self.previous_physical_center = center
        if self.hypothesis_executor is None:
            for runtime in self.runtimes:
                runtime.update(r, gram, elapsed)
        else:
            # Models own independent channel states. They share only immutable
            # observations; global timing/model mixing still occurs once above
            # this component. Drain every task even when one update fails.
            jobs = [job for runtime in self.runtimes for job in
                (runtime.jobs(r, gram, elapsed) if isinstance(runtime, _CandidateGaussianFilter)
                 else [(runtime, r, gram, elapsed)])]
            futures, error = [], None
            for runtime, correlations, information, dt in jobs:
                try:
                    futures.append(self.hypothesis_executor.submit(
                        runtime.update, correlations, information, dt))
                except BaseException as exc:
                    error = exc
                    break
            for future in futures:
                try:
                    future.result()
                except BaseException as exc:
                    if error is None:
                        error = exc
            if error is not None:
                raise error
        self.previous_center_twice = center_twice
        self.block_updates += 1
        self.processed_rows += rows
        self.observed_rows += int(np.count_nonzero(valid))
        self.current_block_index += 1

    def push(self, first_sample, values, valid=None):
        started = time.process_time()
        if isinstance(first_sample, bool) or not isinstance(first_sample, (int, np.integer)):
            raise ValueError("first sample must be an integer")
        if int(first_sample) != self.next_input_sample:
            raise ValueError("input samples must be contiguous from sample zero")
        source = np.asarray(values)
        if source.ndim != 1 or source.size > self.maximum_input_chunk_samples:
            raise ValueError("input chunk must be a bounded one-dimensional vector")
        if valid is None:
            mask = np.ones(source.size, dtype=np.uint8)
        else:
            mask = np.asarray(valid)
            if mask.shape != source.shape or not np.isin(mask, (0, 1, False, True)).all():
                raise ValueError("validity must be a matching binary vector")
            mask = mask.astype(np.uint8, copy=False)
        if self.next_input_sample + source.size > self.samples:
            raise ValueError("input exceeds declared observation endpoint")
        if not np.isfinite(source).all():
            raise ValueError("input contains nonfinite samples")
        dtype = np.complex64 if self.precision == "fp32" else np.complex128
        source = np.asarray(source, dtype=dtype)
        if not np.isfinite(source).all():
            raise ValueError("input overflows the selected arithmetic precision")
        offset = 0
        while offset < len(source):
            start, stop = self._block_bounds(self.current_block_index)
            need = stop - start - len(self.pending)
            take = min(need, len(source) - offset)
            if take <= 0:
                raise RuntimeError("streaming block calendar did not advance")
            if len(self.pending):
                self.pending = np.concatenate((self.pending, source[offset:offset+take]))
                self.pending_valid = np.concatenate((self.pending_valid, mask[offset:offset+take]))
            elif take == len(source) - offset and take == stop-start:
                self._score_block(start, stop, source[offset:offset+take], mask[offset:offset+take])
            else:
                self.pending = source[offset:offset+take].copy()
                self.pending_valid = mask[offset:offset+take].copy()
            offset += take
            self.next_input_sample += take
            if len(self.pending) == stop-start:
                self._score_block(start, stop, self.pending, self.pending_valid)
                self.pending = np.empty(0, dtype=dtype)
                self.pending_valid = np.empty(0, dtype=np.uint8)
        if self.device.type == "cuda":
            torch.cuda.synchronize(self.device)
        self.push_cpu_seconds += time.process_time() - started

    def unmixed_log_evidence(self):
        """Return [model,H0/bit0/bit1,candidate] scores without any prior.

        Timing-state forward inference needs these columns before the fixed
        acquisition and model mixtures used by :meth:`progress`. The caller
        owns those prior weights; adding them here would count them twice.
        """
        if not self.block_updates:
            raise ValueError("no complete likelihood block has been observed")
        per_model = torch.stack([runtime.scores() for runtime in self.runtimes])
        if not bool(torch.isfinite(per_model).all()):
            raise FloatingPointError("incremental likelihood became nonfinite")
        return per_model.detach().cpu().numpy().copy()

    def progress(self):
        """Return diagnostic prefix scores; never an admission decision."""
        if not self.block_updates:
            raise ValueError("no complete likelihood block has been observed")
        per_model = torch.stack([runtime.scores() for runtime in self.runtimes])
        if not bool(torch.isfinite(per_model).all()):
            raise FloatingPointError("incremental likelihood became nonfinite")
        candidate_by_model = torch.logsumexp(per_model, dim=-1) - math.log(self.candidates)
        mixed = torch.logsumexp(candidate_by_model, dim=0) - math.log(len(self.runtimes))
        bit_scores = (mixed[1:] - mixed[:1]).detach().cpu().numpy().tolist()
        model_bit_scores = (candidate_by_model[:, 1:] - candidate_by_model[:, :1]).detach().cpu().numpy().tolist()
        return dict(status="complete" if self.complete_observation else "prefix_diagnostic",
            complete_observation=self.complete_observation, admission=None,
            bit_scores=bit_scores, model_bit_scores=model_bit_scores,
            hypothesis_log_evidence=candidate_by_model.detach().cpu().numpy().tolist(), consumed_samples=self.next_input_sample,
            processed_rows=self.processed_rows, scored_rows=self.scored_rows,
            observed_rows=self.observed_rows, erased_rows=self.erased_rows, blocks=self.block_updates,
            observed_projection_modes=self.observed_projection_modes,
            projected_energy=self.projected_energy,
            quiet_observed_projection_modes=self.quiet_observed_projection_modes,
            quiet_projected_energy=self.quiet_projected_energy,
            quiet_sample_bounds=self.quiet_sample_bounds,
            hypothesis_shards=self.hypothesis_shards,
            hypothesis_update_tasks_per_block=self.hypothesis_shards*len(self.runtimes),
            pending_rows=len(self.pending), working_bytes_estimate=self.owned_bytes_estimate,
            push_process_cpu_seconds=self.push_cpu_seconds)

    def finish(self):
        if self.next_input_sample != self.samples or self.current_block_index != self.block_count or len(self.pending):
            result = self.progress() if self.block_updates else dict(status="incomplete", admission=None,
                complete_observation=False, consumed_samples=self.next_input_sample,
                processed_rows=self.processed_rows, observed_rows=self.observed_rows, blocks=self.block_updates,
                pending_rows=len(self.pending), working_bytes_estimate=self.owned_bytes_estimate)
            result["status"] = "incomplete"
            result["complete_observation"] = False
            result["admission"] = None
            return result
        for runtime in self.runtimes:
            runtime.finish()
        self.complete_observation = True
        result = self.progress()
        result["status"] = "scored"
        result["complete_observation"] = True
        return result


# Concise compatibility name for frontends that already distinguish this
# bounded stream object from ProjectedJointReceiver's finite batch adapter.
IncrementalReceiver = IncrementalGaussianReceiver
