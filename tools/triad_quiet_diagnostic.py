"""Conditional DATA-to-QUIET projection bound for one fixed waveform capture.

This evaluator-only streaming diagnostic reuses the receiver's orthogonal DFT
projection and exact per-segment block calendar. It estimates the probability
that a CLEAN, fixed channel realization falls below the scored quiet Gamma
threshold under proper-complex white Gaussian noise. It is not a modem success
rate, a population fading result, or a false-end guarantee.
"""
from __future__ import annotations

import math

import numpy as np
from scipy.stats import gamma, ncx2

from triad_dynamic_joint import BlockProjection
from triad_incremental_receiver import fixed_block_bounds


def quiet_probability_bound(projected_energy, modes, noise_variance, *, alpha=.001):
    """Upper bound DATA→QUIET for a clean projection at an alternate variance.

    ``projected_energy`` is unnormalized sum |P s|² in the retained unitary DFT
    modes. ``modes`` counts independent complex modes. DATA admission takes
    precedence over QUIET, so the statistic also bounds false termination at
    this quiet decision under the fixed waveform and Gaussian noise model.
    """
    if (isinstance(projected_energy, bool) or not isinstance(projected_energy, (int, float)) or
            not math.isfinite(projected_energy) or projected_energy < 0):
        raise ValueError('projected signal energy must be finite and nonnegative')
    if isinstance(modes, bool) or not isinstance(modes, int) or not 0 <= modes < (1 << 64):
        raise ValueError('mode count must be a nonnegative unsigned 64-bit integer')
    if isinstance(noise_variance, bool) or not isinstance(noise_variance, (int, float)) or not math.isfinite(noise_variance) or noise_variance <= 0:
        raise ValueError('proper-complex raw noise variance must be finite and positive')
    if isinstance(alpha, bool) or not isinstance(alpha, (int, float)) or not math.isfinite(alpha) or not 0 < alpha < 1:
        raise ValueError('quiet false-alarm probability must lie strictly between zero and one')
    if modes == 0:
        return dict(status='unavailable', bound=None, reason='no_complete_projected_modes')
    scaled_energy = float(projected_energy)/float(noise_variance)
    if not math.isfinite(scaled_energy) or scaled_energy > float.fromhex('0x1.fffffffffffffp+1023')/2:
        return dict(status='unavailable', bound=None,
            reason='noncentrality_outside_finite_range',
            projected_signal_energy=float(projected_energy),
            raw_complex_noise_variance=float(noise_variance), modes=modes,
            scope='Fixed-channel Gaussian statistic could not be represented as a finite noncentral chi-square parameter.')
    threshold = float(gamma.ppf(1-alpha, modes))
    noncentrality = 2*scaled_energy
    doubled_threshold = 2*threshold
    if not math.isfinite(threshold) or not math.isfinite(doubled_threshold):
        return dict(status='unavailable', bound=None,
            reason='central_threshold_outside_finite_range', modes=modes,
            projected_signal_energy=float(projected_energy),
            raw_complex_noise_variance=float(noise_variance))
    bound = float(ncx2.cdf(doubled_threshold, 2*modes, noncentrality))
    if not math.isfinite(bound):
        return dict(status='unavailable', bound=None, reason='noncentral_chi_square_tail_unresolved',
                    gamma_energy_threshold=threshold, noncentrality=noncentrality)
    return dict(status='conditional_fixed_channel_bound', bound=min(1., max(0., bound)), alpha=float(alpha),
        modes=modes, projected_signal_energy=float(projected_energy),
        raw_complex_noise_variance=float(noise_variance), gamma_energy_threshold=threshold,
        noncentrality=noncentrality,
        scope='Fixed clean waveform/channel, complete orthogonal projection blocks and proper-complex white Gaussian noise. No acquisition, wrong-bit, population-fade or multi-attempt bound.')


class QuietEnergyAccumulator:
    """Bounded contiguous-stream accumulator for canonical quiet projections.

    Segment boundaries are processed before block boundaries. A terminal
    short block merges within its segment according to ``fixed_block_bounds``.
    Only complete valid blocks wholly inside the declared local quiet aperture
    contribute. Input is copied into at most one segment; no full capture is
    retained. Call ``finish`` exactly once with all declared samples.
    """
    def __init__(self, *, samples, sample_rate, block_samples, segment_samples,
                 quiet_sample_bounds, noise_variance, required_bands,
                 excluded_bands=(), memory_limit_bytes=64*1024**2,
                 maximum_input_chunk_samples=16384, alpha=.001):
        integers = (samples, block_samples, segment_samples, memory_limit_bytes,
                    maximum_input_chunk_samples)
        if any(isinstance(value, bool) or not isinstance(value, int) for value in integers):
            raise ValueError('sample, segment, chunk and memory geometry must be integers')
        if (samples < 1 or block_samples < 1 or segment_samples < 1 or
                memory_limit_bytes < 1 or not 1 <= maximum_input_chunk_samples <= 1_048_576):
            raise ValueError('sample, segment, chunk and memory bounds must be positive and bounded')
        self.fs = float(sample_rate)
        if not math.isfinite(self.fs) or self.fs <= 0:
            raise ValueError('sample rate must be finite and positive')
        if (not isinstance(quiet_sample_bounds, (tuple, list)) or len(quiet_sample_bounds) != 2 or
                any(isinstance(value, bool) or not isinstance(value, int) for value in quiet_sample_bounds) or
                not 0 <= quiet_sample_bounds[0] <= quiet_sample_bounds[1] <= samples):
            raise ValueError('local quiet aperture must be an ordered interval within the capture')
        if (isinstance(noise_variance, bool) or not isinstance(noise_variance, (int, float)) or
                not math.isfinite(noise_variance) or noise_variance <= 0):
            raise ValueError('proper-complex raw noise variance must be finite and positive')
        if (isinstance(alpha, bool) or not isinstance(alpha, (int, float)) or
                not math.isfinite(alpha) or not 0 < alpha < 1):
            raise ValueError('quiet false-alarm probability must lie strictly between zero and one')
        self.samples, self.block_samples = samples, block_samples
        self.segment_samples = min(samples, segment_samples)
        self.bounds = tuple(quiet_sample_bounds)
        self.noise_variance, self.alpha = float(noise_variance), float(alpha)
        self.required_bands = tuple(tuple(row) for row in required_bands)
        self.excluded_bands = tuple(tuple(row) for row in excluded_bands)
        self.max_chunk = maximum_input_chunk_samples
        self.memory_limit_bytes = memory_limit_bytes
        # The terminal segment may have a different remainder than a regular
        # segment: e.g.16-row segments/8-row blocks can end in one13-row block.
        max_block = min(self.segment_samples, 2*self.block_samples-1)
        # Covers pending+incoming concatenation, input normalization, FFT work,
        # projection indices/cache and energy temporaries before any sample copy.
        self.workspace_estimate_bytes = 64*(self.segment_samples+self.max_chunk) + 192*max_block
        if self.workspace_estimate_bytes > memory_limit_bytes:
            raise MemoryError('quiet diagnostic segment and projection scratch exceed memory budget')
        self.received = 0
        self.pending = np.empty(0, dtype=np.complex128)
        self.pending_valid = np.empty(0, dtype=np.uint8)
        self.projections = {}
        self.modes = 0
        self.energy = 0.
        self.invalid_rows = 0
        self._closed = False
        self._segments = 0
        self._aperture_blocks = 0

    def _projection(self, rows):
        projection = self.projections.get(rows)
        if projection is None:
            projection = BlockProjection.make(rows, self.fs,
                self.required_bands, self.excluded_bands)
            self.projections[rows] = projection
        return projection

    def _score_segment(self, first, values, valid):
        segment_length = len(values)
        if not segment_length:
            return
        count = max(1, segment_length//self.block_samples)
        for index in range(count):
            local_first, local_stop = fixed_block_bounds(segment_length, self.block_samples, index)
            block_first, block_stop = first+local_first, first+local_stop
            if block_first < self.bounds[0] or block_stop > self.bounds[1]:
                continue
            if not valid[local_first:local_stop].all():
                continue
            projection = self._projection(local_stop-local_first)
            projected = projection.numpy(values[local_first:local_stop])
            vector = np.asarray(projected, dtype=np.complex128)
            self.energy += float(np.vdot(vector, vector).real)
            self.modes += len(projection.indices)
            self._aperture_blocks += 1
        self._segments += 1

    def push(self, first_sample, values, valid=None):
        if self._closed:
            raise RuntimeError('quiet diagnostic is already finished')
        if isinstance(first_sample, bool) or not isinstance(first_sample, int) or first_sample != self.received:
            raise ValueError('quiet diagnostic input must be contiguous from local sample zero')
        source = np.asarray(values)
        if (source.ndim != 1 or len(source) > self.max_chunk or
                len(source) > self.samples-self.received or not np.iscomplexobj(source) or
                not np.isfinite(source).all()):
            raise ValueError('quiet diagnostic requires a finite bounded complex sample chunk')
        if valid is None:
            mask = np.ones(len(source), dtype=np.uint8)
        else:
            mask = np.asarray(valid)
            if mask.shape != source.shape or not np.isin(mask, (0, 1, False, True)).all():
                raise ValueError('quiet diagnostic validity must be matching binary rows')
            mask = mask.astype(np.uint8, copy=False)
        cursor = 0
        while cursor < len(source):
            room = self.segment_samples-len(self.pending)
            take = min(room, len(source)-cursor)
            joined = np.concatenate((self.pending, source[cursor:cursor+take].astype(np.complex128, copy=False)))
            joined_valid = np.concatenate((self.pending_valid, mask[cursor:cursor+take]))
            self.pending, self.pending_valid = joined, joined_valid
            cursor += take
            if len(self.pending) == self.segment_samples:
                segment_first = first_sample+cursor-len(self.pending)
                self._score_segment(segment_first, self.pending, self.pending_valid)
                self.pending = np.empty(0, dtype=np.complex128)
                self.pending_valid = np.empty(0, dtype=np.uint8)
        self.invalid_rows += int(len(mask)-np.count_nonzero(mask))
        self.received += len(source)
        return self.progress()

    def progress(self):
        """Return work coverage without making an endpoint decision."""
        return dict(schema='triad-fixed-channel-quiet-energy-v1',
            complete=False, samples_received=self.received,
            projected_modes=self.modes, projected_signal_energy=self.energy,
            invalid_rows=self.invalid_rows, complete_quiet_blocks=self._aperture_blocks,
            complete_segments=self._segments,
            workspace_estimate_bytes=self.workspace_estimate_bytes,
            scope='Partial projection diagnostic only; probability bound unavailable until a complete all-valid capture is finished.')

    def finish(self):
        if self._closed:
            raise RuntimeError('quiet diagnostic finish can be called only once')
        if self.pending.size:
            segment_first = self.received-len(self.pending)
            self._score_segment(segment_first, self.pending, self.pending_valid)
        self.pending = np.empty(0, dtype=np.complex128)
        self.pending_valid = np.empty(0, dtype=np.uint8)
        self._closed = True
        complete = self.received == self.samples and self.invalid_rows == 0
        report = dict(self.progress(), complete=complete,
            status='complete' if complete else 'incomplete_or_erased_input',
            noise_variance=self.noise_variance, alpha=self.alpha,
            decision_scope='Fixed-channel quiet-energy statistic only; complete input is required for a conditional DATA→QUIET bound.')
        bound = (quiet_probability_bound(self.energy, self.modes, self.noise_variance,
                    alpha=self.alpha) if complete else
                 dict(status='unavailable', bound=None,
                      reason='complete_all_valid_capture_required'))
        report['conditional_data_to_quiet'] = bound
        report['probability_scope'] = 'Conditional on this exact clean waveform/channel and proper-complex white Gaussian noise. DATA admission is checked first. This is not complete-message success probability or population false-end probability.'
        return report
