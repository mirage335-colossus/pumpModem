#!/usr/bin/env python3
"""Segmented complex FFT acquisition proposals for renewing channels.

The physical waveform and its full causal frontend are not segmented or reset.
Only the already-filtered pattern/observation likelihood is split into blocks,
with an independent complex two-ray gain fitted in each block. This deliberately
loses cross-block phase evidence. It is a proposal statistic, not an admission
test or an assertion that filtered noise and Helper residue are white.
"""
from __future__ import annotations

import hashlib
import math
from pathlib import Path

import numpy as np
from scipy import fft, signal

from triad_realtime_fft import (FFTScreen, _clock, _digest, _elapsed, _integer,
                                _number, _quadratic_scores)


def _correlation_fft_length(rows, policy):
    """Pad valid-only correlation without changing any requested lag.

    A complex ``next_fast_len`` can select repeated radix-11 lengths, which
    were substantially slower for this batched workload on the reference CPU.
    Real-fast lengths currently use the smaller 2/3/5 radices. A nearby power
    of two may be cheaper still; cap that extra padding at ten percent. This
    is a bounded portable heuristic, not a claim of universal FFT optimality.
    The original length remains available for paired numerical/timing checks.
    """
    if policy == 'legacy':
        return fft.next_fast_len(rows)
    smooth = fft.next_fast_len(rows, real=True)
    power_two = 1 << (rows-1).bit_length()
    return power_two if power_two <= smooth+smooth//10 else smooth


class SegmentedFFTScreen(FFTScreen):
    """Full arrival/epoch/clock bank, short coherent blocks and tiled FFTs.

    The frozen whole-bit screen supplies source geometry and full-tail filtered
    columns. Its whole-bit transforms are discarded; block transforms are made
    one block at a time. Constructor peak memory still includes that parent
    preparation. No giant [workspace, block, ray, FFT] cache is retained.
    """
    def __init__(self, manifest, raw_templates, epoch_offsets, *, block_seconds=.5,
                 workspace_batch_size=8, transform_batch_bytes=4*1024**2,
                 fft_length_policy='bounded-smooth', maximum_fft_products=250_000,
                 **kwargs):
        started = _clock()
        block_seconds = _number(block_seconds, 'block seconds', positive=True)
        self.workspace_batch_size = _integer(workspace_batch_size, 'workspace batch size')
        self.transform_batch_bytes = _integer(transform_batch_bytes, 'transform batch bytes')
        if fft_length_policy not in ('bounded-smooth', 'legacy'):
            raise ValueError('FFT length policy must be bounded-smooth or legacy')
        self.fft_length_policy = fft_length_policy
        super().__init__(manifest, raw_templates, epoch_offsets,
                         maximum_fft_products=maximum_fft_products, **kwargs)
        self.parent_procedure_id = self.procedure_id
        self.block_rows = max(1, int(round(block_seconds*self.output_fs)))
        self.block_seconds = self.block_rows/self.output_fs
        self.maximum_frequency_step = 1/(4*self.block_seconds)
        self.blocks = [(start, min(start+self.block_rows, self.kernel_rows))
                       for start in range(0, self.kernel_rows, self.block_rows)]
        self.block_nfft = tuple(_correlation_fft_length(stop-start+self.lag_count-1,
                                                       self.fft_length_policy)
                                for start, stop in self.blocks)
        self.columns = np.asarray([w['columns'] for w in self.workspaces], dtype=self.dtype)
        self.orthonormal_columns = np.zeros_like(self.columns)
        self.inverses = np.zeros((len(self.blocks), len(self.workspaces), len(self.rays),
                                  len(self.rays)), dtype=np.complex128)
        self.ranks = np.zeros((len(self.blocks), len(self.workspaces)), dtype=np.int64)
        self.explicit = np.zeros_like(self.ranks, dtype=bool)
        for wi, workspace in enumerate(self.workspaces):
            # Global floor rejects numerically empty filter tails; local rank
            # handles nearly dependent rays without an explicit inverse.
            global_floor = max(float(np.linalg.eigvalsh(workspace['gram'])[-1]), 0.)*1e-12
            for bi, (start, stop) in enumerate(self.blocks):
                columns = self.columns[wi, :, start:stop].astype(np.complex128)
                gram = columns.conj() @ columns.T
                values, vectors = np.linalg.eigh((gram+gram.conj().T)*.5)
                keep = values > max(global_floor, max(float(values[-1]), 0.)*1e-10)
                if keep.any():
                    self.inverses[bi, wi] = (vectors[:, keep]/values[keep]) @ vectors[:, keep].conj().T
                    self.ranks[bi, wi] = int(keep.sum())
                    # A=columns.T, G=A^H A and Q=A V Lambda^(-1/2).
                    # Stored rows Q.T therefore use V.T, not V^H. Whiten
                    # before the FP32 FFT: whitening noisy correlations after
                    # accumulation can cancel large almost-equal quantities.
                    basis = (vectors[:, keep]/np.sqrt(values[keep])).T @ columns
                    self.orthonormal_columns[wi, :int(keep.sum()), start:stop] = basis.astype(self.dtype)
                    self.explicit[bi, wi] = bool(len(self.rays) == 2 and keep.all()
                        and values[-1]/values[0] <= 1e6)
            workspace.pop('transform')
            # One contiguous column tensor owns the retained time-domain data.
            workspace['columns'] = self.columns[wi]
        if np.any(self.ranks.sum(axis=0) == 0):
            raise ValueError('segmented pattern has no observable energy')
        self.resident_estimate = (np.asarray(raw_templates).nbytes+self.columns.nbytes+self.orthonormal_columns.nbytes+
            self.inverses.nbytes+self.ranks.nbytes+self.explicit.nbytes)
        self.policy = dict(self.policy, schema='triad-segmented-fft-policy-v1',
            requested_block_seconds=block_seconds, block_seconds=self.block_seconds,
            block_rows=self.block_rows, block_count=len(self.blocks),
            maximum_frequency_step_hz=self.maximum_frequency_step,
            frequency_grid='zero-anchored integer multiples of 1/(4 resolved block seconds); interval endpoints rounded outward',
            workspace_batch_size=self.workspace_batch_size,
            transform_product_target_bytes=self.transform_batch_bytes,
            transform_tiling='workspace/frequency ceilings and byte target; at least one complete ray design',
            fft_length_policy=self.fft_length_policy,
            fft_padding='real-fast length, or next power of two when at most ten percent larger; legacy retains complex next_fast_len',
            statistic='sum of independent block complex two-ray GLRT energies; proposal only',
            score_arithmetic='FP64 block basis orthonormalization before selected-precision FFT; FP64 squared-correlation sums',
            rank_policy='eigenvalue > max(1e-12 whole-pattern maximum,1e-10 block maximum)',
            physical_filter_state='complete capture and complete source are filtered once before likelihood segmentation',
            noise_scope='Filtered noise covariance and Helper residue are not modeled by this score; no Gamma or false-admission guarantee',
            template_fft_cache='one block at a time; constructor transient includes parent whole-bit preparation')
        self.procedure_id = _digest(dict(policy=self.policy,
            parent_procedure_id=self.parent_procedure_id,
            source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()))
        self.preparation_timing = _elapsed(started)

    def _batch_shape(self, nfft, frequency_count):
        # Bound the product tensor rather than fixing a large transform count:
        # precision and FFT length both change its cache/memory footprint.
        # A complete ray design is indivisible; the public memory guard still
        # accounts for its actual size when it exceeds this cache target.
        design_bytes = len(self.rays)*nfft*np.dtype(self.dtype).itemsize
        designs = max(1, self.transform_batch_bytes//design_bytes)
        workspaces = min(len(self.workspaces), self.workspace_batch_size, designs)
        frequencies = min(frequency_count, self.batch_size, max(1, designs//workspaces))
        return workspaces, frequencies

    def _frequencies(self, frequencies_hz, frequency_intervals):
        if (frequencies_hz is None) == (frequency_intervals is None):
            raise ValueError('supply exactly one frequency bank or interval union')
        step = self.maximum_frequency_step
        if frequency_intervals is not None:
            indices = set()
            for interval in frequency_intervals:
                if len(interval) != 2:
                    raise ValueError('each frequency interval needs two endpoints')
                lo, hi = (_number(x, 'frequency interval') for x in interval)
                if lo > hi:
                    raise ValueError('frequency interval must be ordered')
                first, last = math.floor(lo/step+1e-12), math.ceil(hi/step-1e-12)
                if last-first+1 > self.maximum_frequency_points:
                    return None, 'frequency_work_limit'
                indices.update(range(first, last+1))
                if len(indices) > self.maximum_frequency_points:
                    return None, 'frequency_work_limit'
            if not indices:
                raise ValueError('frequency interval union must be nonempty')
            bank = np.asarray(sorted(indices), dtype=float)*step
        else:
            bank = np.asarray(tuple(frequencies_hz), dtype=float)
            if (bank.ndim != 1 or not len(bank) or not np.isfinite(bank).all()
                    or len(np.unique(bank)) != len(bank)):
                raise ValueError('frequency bank must be finite, nonempty and unique')
            bank.sort()
            if not np.allclose(bank/step, np.round(bank/step), atol=1e-10, rtol=0):
                return None, 'explicit_frequency_grid_not_zero_anchored'
            if len(bank) > 1 and np.diff(bank).max() > step*(1+1e-10):
                return None, 'explicit_frequency_grid_too_coarse'
        if len(bank) > self.maximum_frequency_points:
            return None, 'frequency_work_limit'
        if max(abs(bank))+self.source_half_width+abs(self.doppler) > self.pass_edge:
            return None, 'frequency_bank_outside_nominal_passband'
        if len(bank)*len(self.workspaces)*len(self.rays)*len(self.blocks) > self.maximum_fft_products:
            return None, 'fft_product_work_limit'
        return bank, None

    def work(self, frequency_count):
        """Array/work envelope; an engineering allocation estimate, not RSS."""
        frequency_count = _integer(frequency_count, 'frequency count')
        nw, nr, nf = len(self.workspaces), len(self.rays), max(self.block_nfft)
        wbatch, fbatch = self._batch_shape(nf, frequency_count)
        complex_bytes = np.dtype(self.dtype).itemsize
        tiles = [(length, *self._batch_shape(length, frequency_count))
                 for length in sorted(set(self.block_nfft))]
        product_bytes = max(w*f*nr*length*complex_bytes for length,w,f in tiles)
        maximum_fbatch = max(f for _,_,f in tiles)
        score_bytes = nw*frequency_count*self.lag_count*8
        transforms = (nw*nr+frequency_count)*nf*complex_bytes
        product_scratch = 3*product_bytes
        quadratic_scratch = 8*maximum_fbatch*self.lag_count*16
        peak = (self.resident_estimate+score_bytes+transforms+product_scratch+
                quadratic_scratch+64*self.samples+32*self.crop_rows)
        active_blocks = int(np.count_nonzero(np.any(self.ranks, axis=1)))
        return dict(epochs=len(self.epochs), clocks=len(self.clocks), bits=2, rays=nr,
            arrival_points=self.lag_count, frequency_points=frequency_count,
            synchronization_hypotheses=len(self.epochs)*len(self.clocks)*self.lag_count*frequency_count,
            block_count=len(self.blocks), block_rows=self.block_rows,
            last_block_rows=self.blocks[-1][1]-self.blocks[-1][0],
            fft_products=nw*nr*frequency_count*len(self.blocks),
            fft_lengths=list(self.block_nfft), template_workspaces=nw,
            template_ffts=nw*nr*len(self.blocks), observation_ffts=frequency_count*len(self.blocks),
            executed_fft_products=nw*nr*frequency_count*active_blocks,
            executed_template_ffts=nw*nr*active_blocks,
            executed_observation_ffts=frequency_count*active_blocks,
            skipped_zero_rank_blocks=len(self.blocks)-active_blocks,
            workspace_batch_size=wbatch, frequency_batch_size=fbatch,
            maximum_transform_product_bytes=product_bytes,
            fft_batch_shapes=[dict(fft_length=n, workspace_batch_size=w,
                                   frequency_batch_size=f) for n,w,f in tiles],
            fft_length_policy=self.fft_length_policy,
            active_block_designs=int(np.count_nonzero(self.ranks)),
            total_design_rank=int(self.ranks.sum()), score_array_bytes=score_bytes,
            estimated_peak_bytes=peak, source_support_complete=True, coarse_projection_only=True)

    def _score_grid(self, observation, frequencies):
        """Return [workspace,frequency,arrival] scores; bounded by public score()."""
        nw, nr = len(self.workspaces), len(self.rays)
        scores = np.zeros((nw, len(frequencies), self.lag_count), dtype=np.float64)
        for bi, ((start, stop), nfft) in enumerate(zip(self.blocks, self.block_nfft)):
            if not np.any(self.ranks[bi]):
                # These orthonormal columns are exactly zero. No observation,
                # timing hypothesis or nonzero filter tail is discarded.
                continue
            columns = self.orthonormal_columns[:, :, start:stop]
            transforms = fft.fft(columns, n=nfft, axis=-1, workers=self.workers)
            np.conjugate(transforms, out=transforms)
            segment = observation[start:stop+self.lag_count-1]
            times = np.arange(len(segment), dtype=float)/self.output_fs
            spectra = np.empty((len(frequencies), nfft), dtype=self.dtype)
            for fi, frequency in enumerate(frequencies):
                rotation = np.exp(-2j*np.pi*np.remainder(frequency*times, 1.)).astype(self.dtype)
                spectra[fi] = fft.fft(segment*rotation, n=nfft, workers=self.workers)
            wbatch, fbatch = self._batch_shape(nfft, len(frequencies))
            for wf in range(0, nw, wbatch):
                ws = min(wf+wbatch, nw)
                for ff in range(0, len(frequencies), fbatch):
                    fs = min(ff+fbatch, len(frequencies))
                    products = spectra[ff:fs, None, None, :]*transforms[None, wf:ws]
                    correlations = fft.ifft(products, axis=-1, workers=self.workers,
                                            overwrite_x=True)[..., :self.lag_count]
                    for local, wi in enumerate(range(wf, ws)):
                        c = correlations[:, local]
                        energy = np.sum(c.real.astype(np.float64)**2+c.imag.astype(np.float64)**2, axis=1)
                        scores[wi, ff:fs] += energy/self.reference_variance
        return scores

    def score(self, raw, frequencies_hz=None, *, frequency_intervals=None,
              valid=None, top_per_bit=4, minimum_arrival_separation_seconds=None):
        started = _clock()
        top_per_bit = _integer(top_per_bit, 'top candidates per bit')
        if top_per_bit > 64:
            raise ValueError('top candidates per bit exceeds64 proposal budget')
        separation = (.5/float(self.manifest['format'].get('fine_chip_rate_hz', 60.))
            if minimum_arrival_separation_seconds is None else
            _number(minimum_arrival_separation_seconds, 'candidate separation', positive=True))
        frequencies, failure = self._frequencies(frequencies_hz, frequency_intervals)
        result = dict(schema='triad-segmented-fft-proposals-v1', status='unresolved', reason=failure,
            candidates=[], candidates_by_bit=[[], []], procedure_id=self.procedure_id,
            policy=self.policy, preparation_timing=self.preparation_timing,
            admission='none; complete adaptive procedure requires independent qualification')
        if failure:
            result['timing'] = dict(total=_elapsed(started))
            return result
        work = self.work(len(frequencies))
        if work['estimated_peak_bytes'] > self.memory_limit_bytes:
            result.update(reason='segmented_fft_memory_limit', work=work,
                          timing=dict(total=_elapsed(started)))
            return result
        mark = _clock()
        observation = self.prepare_observation(raw, valid)
        filtering = _elapsed(mark)
        mark = _clock()
        scores = self._score_grid(observation, frequencies)
        correlation = _elapsed(mark)
        mark = _clock()
        retained = [[], []]
        sample_separation = max(1, math.ceil(separation*self.output_fs))
        for wi, workspace in enumerate(self.workspaces):
            local = []
            for fi, values in enumerate(scores[wi]):
                peaks = signal.find_peaks(values, distance=sample_separation)[0]
                if len(values) == 1:
                    peaks = np.array([0])
                else:
                    peaks = np.unique(np.r_[peaks, 0 if values[0] >= values[1] else [],
                        len(values)-1 if values[-1] >= values[-2] else []]).astype(int)
                if len(peaks) > top_per_bit:
                    peaks = peaks[np.argpartition(values[peaks], -top_per_bit)[-top_per_bit:]]
                for lag in peaks:
                    local.append(dict(bit=workspace['bit'], epoch=workspace['epoch'],
                        arrival=float(self.arrivals[lag]), frequency=float(frequencies[fi]),
                        clock_ppm=workspace['clock_ppm'], score=float(values[lag]),
                        rank=int(self.ranks[:, wi].sum())))
            local.sort(key=lambda x:x['score'], reverse=True)
            # One point per timing/frequency cell; separate CFO grid points are
            # retained even though their nuisance gains are partly confounded.
            unique = []
            for candidate in local:
                if any(abs(candidate['arrival']-old['arrival']) < separation and
                       candidate['frequency'] == old['frequency'] for old in unique):
                    continue
                unique.append(candidate)
                if len(unique) >= top_per_bit:
                    break
            retained[workspace['bit']].extend(unique)
        for bit in (0, 1):
            retained[bit] = sorted(retained[bit], key=lambda x:x['score'], reverse=True)[:top_per_bit]
        result.update(status='proposed', reason='bounded_segmented_complex_screen',
            candidates=retained[0]+retained[1], candidates_by_bit=retained,
            frequencies_hz=frequencies.tolist(), work=work,
            timing=dict(filtering=filtering, correlation=correlation,
                        selection=_elapsed(mark), total=_elapsed(started)))
        return result
