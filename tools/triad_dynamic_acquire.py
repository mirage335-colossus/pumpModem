#!/usr/bin/env python3
"""Bounded-memory execution tiling of the existing dynamic acquisition bank.

Every selected Cartesian candidate is scored exactly once. A public timing guard
first selects DATA-only reception when a continuous arrival grid cannot resolve
the requested pilot/DATA phase relationship. This deterministic selection needs
its own procedure calibration; it never consults received samples. All tiles use a common projection planned
from the COMPLETE frequency/clock bank. Tile log means are converted back to
weighted sums before combining H0, bit0 and bit1 separately. No candidate is
pruned using its DATA score or evaluator truth. ``TiledAcquisition`` defaults to
the complete bank. ``HelperFrequencyAcquisition`` independently estimates a
frequency subset for each capture, while retaining that complete bank's common
projection and the independent pilot phase guard. Its entire adaptive procedure
requires its own admission calibration.

Preparation is amortized over every supplied trial batch before a tile is
released. A subsequent ``score`` call prepares the tiles again. Keep independent
captures in one call where memory permits; do not call once per capture and
mistake repeated preparation for intrinsic receiver cost. The wrapper does not
calibrate admission or assert that a finite bank covers continuous uncertainty.
"""
from __future__ import annotations

import copy
from dataclasses import dataclass
import hashlib
import itertools
import json
import math
from pathlib import Path
import time

import numpy as np
from scipy.special import logsumexp
import torch

from triad_dynamic_joint import DynamicJointReceiver, public_spectral_bands
from triad_dynamic_variants import ScanJointReceiver, core_projection_bands


def _bank(values, name, *, integral=False):
    original = tuple(values)
    if not original:
        raise ValueError(f'{name} must be nonempty')
    if integral:
        if any(isinstance(x, (bool, np.bool_)) or not isinstance(x, (int, np.integer)) for x in original):
            raise ValueError(f'{name} requires integer coordinates')
        result = tuple(int(x) for x in original)
    else:
        result = tuple(float(x) for x in original)
        if not all(math.isfinite(x) for x in result):
            raise ValueError(f'{name} requires finite coordinates')
    if len(set(result)) != len(result):
        raise ValueError(f'{name} must contain unique coordinates')
    return result


def _digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, allow_nan=False,
                                    separators=(',', ':')).encode()).hexdigest()


def projection_digest(receiver):
    """Physical block boundaries, operator kind and exact retained rows/bins."""
    value = hashlib.sha256()
    for start, stop, projection, *_ in receiver.blocks:
        value.update(np.asarray([start, stop], dtype='<i8').tobytes())
        value.update(projection.kind.encode())
        value.update(np.asarray(projection.indices, dtype='<i8').tobytes())
    return value.hexdigest()


def pilot_timing_guard(manifest, arrivals, *, pilot_use='both', arrival_model='continuous',
                       loss_budget_db=.5, clock_ppm=(0.,)):
    """Public engineering guard, not a universal sensitivity-loss theorem.

    Two equal-power tones separated by W retain cos²(pi W delta) after the
    best common phase correction. The first-lobe radius for the declared loss
    is used as a conservative planning scale for this joint pilot/DATA model.
    Unequal powers, fading, selective channels, template interpolation, arbitrary
    DATA spectra and likelihood marginalization do not obey that formula in
    general. Passing it is not physical sensitivity qualification.

    ``discrete`` means the arrival prior is exactly the supplied coordinates,
    including an explicitly conditioned known arrival. Public clock uncertainty
    and unrepresented delay motion still apply. Missing uncertainty metadata is
    unresolved, not zero. This does not certify off-grid arrivals and must not
    silently describe a continuous experiment.
    """
    if pilot_use not in ('both', 'low', 'high', 'none'):
        raise ValueError('pilot_use must be both/low/high/none')
    if arrival_model not in ('continuous', 'discrete'):
        raise ValueError('arrival_model must be continuous/discrete')
    if not math.isfinite(loss_budget_db) or loss_budget_db <= 0:
        raise ValueError('pilot timing loss budget must be positive and finite')
    ordered = sorted(_bank(arrivals, 'arrival bank'))
    clocks = _bank(clock_ppm, 'clock bank')
    prior = tuple(float(value) for value in manifest['calendar']['arrival_prior_seconds'])
    if len(prior) != 2 or not all(math.isfinite(value) for value in prior) or prior[0] > prior[1]:
        raise ValueError('public arrival prior must have two finite ordered endpoints')
    if ordered[0] < prior[0] or ordered[-1] > prior[1]:
        raise ValueError('arrival bank leaves public arrival prior')
    left, right = ordered[0]-prior[0], prior[1]-ordered[-1]
    gap = max((b-a for a, b in zip(ordered, ordered[1:])), default=0.)
    arrival_cover_radius = max(left, right, gap/2)
    result = dict(schema='triad-joint-pilot-timing-guard-v1', arrival_model=arrival_model,
        requested_pilot_use=pilot_use, effective_pilot_use=pilot_use,
        loss_budget_db=float(loss_budget_db), public_arrival_prior_seconds=prior,
        continuous_cover_radius_seconds=arrival_cover_radius, maximum_grid_gap_seconds=gap,
        uncovered_left_seconds=left, uncovered_right_seconds=right,
        guard_applied=False, reason='helper_disabled', pilot_data_span_hz=None,
        allowed_cover_radius_seconds=None, maximum_relative_phase_error_radians=None,
        total_timing_radius_seconds=None, missing_uncertainty_fields=[],
        planning_model='Equal-tone first-lobe cos^2(pi*W*delta); W spans nominal DATA band and selected pilots.',
        limitation='Engineering phase-resolution policy only, not a bound on actual stochastic-channel sensitivity loss.')
    if not bool(manifest.get('helper_enabled', True)):
        return result
    if pilot_use == 'none':
        result['reason'] = 'pilots_not_requested'
        return result
    channel = manifest['channel']
    missing = [name for name in ('sample_clock_pair_ppm', 'delay_excursion_seconds') if name not in channel]
    clock_radius = clock_timing_radius = motion = None
    public_clock_bound = elapsed = None
    if 'sample_clock_pair_ppm' in channel:
        public_clock_bound = float(channel['sample_clock_pair_ppm'])
        if not math.isfinite(public_clock_bound) or public_clock_bound < 0:
            raise ValueError('public sample-clock pair bound must be finite and nonnegative')
        clock_ordered = sorted(clocks)
        # Endpoints and in-interval Voronoi boundaries cover nonuniform banks,
        # including deliberately wider-than-prior search coordinates.
        points_to_check = [-public_clock_bound, public_clock_bound]
        points_to_check += [(a+b)/2 for a, b in zip(clock_ordered, clock_ordered[1:])
                           if -public_clock_bound <= (a+b)/2 <= public_clock_bound]
        clock_radius = max(min(abs(point-value) for value in clock_ordered) for point in points_to_check)
    if 'delay_excursion_seconds' in channel:
        motion = float(channel['delay_excursion_seconds'])
        if not math.isfinite(motion) or motion < 0:
            raise ValueError('public delay excursion must be finite and nonnegative')
    if clock_radius == 0:
        clock_timing_radius = 0.
    elif clock_radius is not None:
        calendar = manifest['calendar']
        required_time = ('capture_samples', 'capture_lead_seconds')
        missing += ['calendar.'+name for name in required_time if name not in calendar]
        if 'sample_rate_hz' not in manifest:
            missing.append('sample_rate_hz')
        if 'spread_delay_ms' not in channel:
            missing.append('spread_delay_ms')
        if not missing:
            fs, samples = float(manifest['sample_rate_hz']), int(calendar['capture_samples'])
            lead, delay = float(calendar['capture_lead_seconds']), float(channel['spread_delay_ms'])*.001
            if not math.isfinite(fs) or fs <= 0 or samples < 1 or not all(map(math.isfinite, (lead, delay))):
                raise ValueError('finite public capture time coordinates required')
            elapsed = max(abs(-lead), abs((samples-1)/fs-lead))+max(map(abs, prior))+abs(delay)+motion
            clock_timing_radius = elapsed*clock_radius*1e-6
    arrival_radius = arrival_cover_radius if arrival_model == 'continuous' else 0.
    radius = None if missing else arrival_radius+clock_timing_radius+motion
    result.update(arrival_timing_radius_seconds=arrival_radius,
        public_sample_clock_pair_bound_ppm=public_clock_bound,
        clock_bank_cover_radius_ppm=clock_radius, clock_time_bound_seconds=elapsed,
        clock_timing_radius_seconds=clock_timing_radius,
        unrepresented_delay_motion_radius_seconds=motion,
        total_timing_radius_seconds=radius, missing_uncertainty_fields=missing,
        clock_time_bound_scope='Maximum absolute nominal source coordinate over public capture, arrival and excess-delay bounds.')
    data_half = float(manifest['format']['occupied_bandwidth_hz'])/2
    data_if = float(manifest['channel']['nominal_conversion_if_hz'])
    if not math.isfinite(data_half) or data_half <= 0 or not math.isfinite(data_if):
        raise ValueError('finite positive nominal DATA band required for pilot timing guard')
    points = [-data_half, data_half]
    for name in ('low', 'high'):
        if pilot_use in ('both', name):
            tone = float(manifest['helper_plan'][name+'_hz'])-data_if
            if not math.isfinite(tone):
                raise ValueError('pilot frequencies must be finite')
            points.append(tone)
    # Arrival acts before source-clock scaling. A symmetric public clock
    # allowance bounds that phase slope across every declared clock candidate.
    clock_scale = 1+max(map(abs, clocks))*1e-6
    span = (max(points)-min(points))*clock_scale
    allowed = math.acos(10**(-loss_budget_db/20))/(math.pi*span)
    result.update(pilot_data_span_hz=span, nominal_frequency_span_endpoints_hz=[min(points), max(points)],
        maximum_source_clock_scale=clock_scale, allowed_cover_radius_seconds=allowed,
        maximum_relative_phase_error_radians=None if radius is None else 2*math.pi*span*radius)
    if missing:
        result.update(guard_applied=True, reason='helper_phase_timing_unresolved', effective_pilot_use='none',
                      uncertainty_status='required public clock or delay-motion metadata unavailable')
        return result
    if arrival_model == 'discrete' and clock_timing_radius == 0 and motion == 0:
        result['reason'] = 'explicit_discrete_arrival_prior'
        return result
    result['guard_applied'] = True
    result['reason'] = 'pilot_timing_resolved' if radius <= allowed else 'helper_phase_timing_unresolved'
    if radius > allowed:
        result['effective_pilot_use'] = 'none'
    return result


@dataclass(frozen=True)
class AcquisitionTile:
    epoch: int
    arrivals: tuple[float, ...]
    frequency: float
    clock_ppm: float

    @property
    def candidates(self):
        return len(self.arrivals)

    def coordinates(self):
        return tuple((self.epoch, arrival, self.frequency, self.clock_ppm) for arrival in self.arrivals)


class TiledAcquisition:
    """Public bank descriptor; one bounded receiver workspace lives at a time.

    ``memory_limit_bytes`` is an explicit-array engineering budget covering the
    supplied raw input, public template array, validity, result aggregation and
    one receiver workspace. It is not an OS RSS, CUDA allocator-cache or library
    scratch guarantee. Inputs are borrowed without a full copy; the raw buffer
    is nevertheless reserved in this total accounting. The input may be a NumPy
    memmap. CPU and CUDA source arithmetic follow the existing receiver controls.
    """

    def __init__(self, manifest, raw_templates, epoch_offsets, *,
                 arrival_seconds=(0.,), frequencies_hz=(0.,), clock_ppm=(0.,),
                 selected_epochs=None, timing_tile_size=8, block_seconds=.25,
                 coherence_seconds=(.5, math.inf), pilot_use='both',
                 arrival_model='continuous', pilot_timing_loss_budget_db=.5,
                 execution='scan', projection='full-envelope',
                 auxiliary_half_width_hz=200., device='cpu', precision='fp32',
                 memory_limit_bytes=2*1024**3, maximum_candidates=1_000_000):
        started = time.perf_counter()
        self.manifest = copy.deepcopy(manifest)
        self.templates = np.asarray(raw_templates)
        self.epochs = _bank(epoch_offsets, 'template epochs', integral=True)
        if (self.templates.ndim != 3 or self.templates.shape[:2] != (len(self.epochs), 2) or
                not np.iscomplexobj(self.templates) or self.templates.shape[-1] < 1):
            raise ValueError('complex public templates need [epochs,2,samples]')
        self.selected_epochs = _bank(self.epochs if selected_epochs is None else selected_epochs,
                                     'selected epochs', integral=True)
        if any(epoch not in self.epochs for epoch in self.selected_epochs):
            raise ValueError('selected epoch missing from public template bank')
        self.arrivals = _bank(arrival_seconds, 'arrival bank')
        self.frequencies = _bank(frequencies_hz, 'frequency bank')
        self.clocks = _bank(clock_ppm, 'clock bank')
        if any(1+clock*1e-6 <= 0 for clock in self.clocks):
            raise ValueError('clock scales must be positive')
        prior = self.manifest['calendar']['arrival_prior_seconds']
        if min(self.arrivals) < prior[0] or max(self.arrivals) > prior[1]:
            raise ValueError('arrival bank leaves public arrival prior')
        if (not isinstance(timing_tile_size, int) or isinstance(timing_tile_size, bool) or
                not 1 <= timing_tile_size <= 4096):
            raise ValueError('timing tile size must be an integer in [1,4096]')
        if (not isinstance(memory_limit_bytes, int) or isinstance(memory_limit_bytes, bool) or
                memory_limit_bytes < 1):
            raise ValueError('positive integer memory limit required')
        if not isinstance(maximum_candidates, int) or maximum_candidates < 1:
            raise ValueError('positive integer candidate cap required')
        if not math.isfinite(block_seconds) or block_seconds <= 0:
            raise ValueError('positive finite block duration required')
        self.coherences = tuple(float(x) for x in coherence_seconds)
        if (not self.coherences or len(set(self.coherences)) != len(self.coherences) or
                any(math.isnan(x) or x <= 0 for x in self.coherences)):
            raise ValueError('distinct positive coherence values required')
        if execution not in ('serial', 'scan') or projection not in ('full-envelope', 'core'):
            raise ValueError('execution must be serial/scan; projection must be full-envelope/core')
        if precision not in ('fp32', 'fp64'):
            raise ValueError('precision must be fp32/fp64')
        self.candidates = math.prod(map(len, (self.selected_epochs, self.arrivals,
                                            self.frequencies, self.clocks)))
        if self.candidates > maximum_candidates:
            raise ValueError('public Cartesian bank exceeds declared total candidate cap')
        if self.templates.nbytes >= memory_limit_bytes:
            raise MemoryError('public template input alone exceeds total memory budget')
        self.tile_size, self.block_seconds = timing_tile_size, float(block_seconds)
        self.execution, self.projection = execution, projection
        self.device, self.precision = device, precision
        self.requested_pilot_use = pilot_use
        self.timing_guard = pilot_timing_guard(self.manifest, self.arrivals, pilot_use=pilot_use,
            arrival_model=arrival_model, loss_budget_db=pilot_timing_loss_budget_db, clock_ppm=self.clocks)
        self.pilot_use = self.timing_guard['effective_pilot_use']
        self.memory_limit_bytes = memory_limit_bytes
        self.auxiliary_half_width_hz = float(auxiliary_half_width_hz)
        # Never plan from the current tile's smaller bank. Likelihoods using
        # different observation projections are not interchangeable tile terms.
        if projection == 'core':
            required, excluded = core_projection_bands(self.manifest, pilot_use=self.pilot_use,
                frequencies_hz=self.frequencies, clock_ppm=self.clocks,
                auxiliary_half_width_hz=auxiliary_half_width_hz)
        else:
            required, excluded = public_spectral_bands(self.manifest, pilot_use=self.pilot_use,
                cfo_bound_hz=max(map(abs, self.frequencies)),
                clock_bound_ppm=max(map(abs, self.clocks)),
                auxiliary_half_width_hz=auxiliary_half_width_hz)
        self.required_bands_hz, self.excluded_bands_hz = required, excluded
        self.tile_count = (len(self.selected_epochs)*len(self.frequencies)*len(self.clocks)*
                           math.ceil(len(self.arrivals)/self.tile_size))
        sources = {name: hashlib.sha256((Path(__file__).parent/name).read_bytes()).hexdigest()
                   for name in ('triad_dynamic_acquire.py', 'triad_dynamic_joint.py',
                                'triad_dynamic_variants.py', 'triad_dynamic_scan.py',
                                'triad_gaussian_prepared.py', 'triad_gaussian_state.py')}
        self.policy = dict(schema='triad-dynamic-tiled-acquisition-v2',
            selected_epochs=self.selected_epochs, arrival_seconds=self.arrivals,
            frequencies_hz=self.frequencies, clock_ppm=self.clocks,
            timing_tile_size=self.tile_size, total_candidates=self.candidates, tile_count=self.tile_count,
            block_seconds=self.block_seconds, coherences=[str(x) for x in self.coherences],
            requested_pilot_use=pilot_use, pilot_use=self.pilot_use,
            arrival_model=arrival_model, pilot_timing_guard=self.timing_guard,
            execution=execution, projection=projection,
            required_bands_hz=required, auxiliary_exclusion_hz=excluded,
            device=str(device), precision=precision, source_hashes=sources,
            template_sha256=hashlib.sha256(np.ascontiguousarray(self.templates).view(np.uint8)).hexdigest(),
            mixture='Equal selected-Cartesian candidate prior under H0 and both bits; '
                    'the complete bank is used when no per-trial subset is supplied.',
            projection_scope='One full-bank public projection shared by every tile; core is explicitly lossy.'
                if projection == 'core' else 'One full-bank public envelope projection shared by every tile.',
            scope='Finite public timing/frequency/clock/epoch bank. No continuous-grid or operational admission guarantee.')
        public = {name: self.manifest[name] for name in ('format', 'calendar', 'channel', 'helper_plan', 'power')}
        public['helper_enabled'] = bool(self.manifest.get('helper_enabled', True))
        self.procedure_id = _digest(dict(policy=self.policy, public_geometry=public))
        self.planning_seconds = time.perf_counter()-started

    def tiles(self):
        """Disjoint rectangles; nonuniform final timing tiles are intentional."""
        for epoch, frequency, clock in itertools.product(self.selected_epochs, self.frequencies, self.clocks):
            for first in range(0, len(self.arrivals), self.tile_size):
                yield AcquisitionTile(epoch, self.arrivals[first:first+self.tile_size], frequency, clock)

    def _receiver(self, tile, valid, memory_budget):
        cls = ScanJointReceiver if self.execution == 'scan' else DynamicJointReceiver
        return cls(self.manifest, self.templates, self.epochs,
            selected_epochs=(tile.epoch,), arrival_seconds=tile.arrivals,
            frequencies_hz=(tile.frequency,), clock_ppm=(tile.clock_ppm,),
            block_seconds=self.block_seconds, coherence_seconds=self.coherences,
            pilot_use=self.pilot_use, required_bands_hz=self.required_bands_hz,
            auxiliary_exclusion_hz=self.excluded_bands_hz,
            auxiliary_half_width_hz=self.auxiliary_half_width_hz, valid=valid,
            device=self.device, precision=self.precision, memory_limit_bytes=memory_budget)

    def score(self, raw, valid=None, *, trial_batch_size=None, progress=None,
              selected_frequency_banks=None):
        """Score full banks, or declared per-trial subsets with shared preparation.

        A frequency tile is prepared once when at least one trial needs it.
        Only those trials are scored. Subsets never change the public projection,
        clock bank, phase guard or timing/epoch alternatives. The caller owns
        calibration of any data-dependent subset rule; this method alone does
        not grant calibrated admission to a reduced search.
        """
        started = time.perf_counter()
        data = np.asarray(raw)
        if data.ndim == 1:
            data = data[None, :]
        samples = int(self.manifest['calendar']['capture_samples'])
        if (data.ndim != 2 or data.shape[0] < 1 or data.shape[1] != samples or
                not np.iscomplexobj(data)):
            raise ValueError('complex raw input needs [trials,public capture samples]')
        trials = data.shape[0]
        if selected_frequency_banks is None:
            selected_banks = [self.frequencies] * trials
        else:
            supplied_banks = tuple(selected_frequency_banks)
            if len(supplied_banks) != trials:
                raise ValueError('one selected frequency bank is required per trial')
            selected_banks = []
            for supplied in supplied_banks:
                chosen = _bank(supplied, 'selected frequency bank')
                if any(value not in self.frequencies for value in chosen):
                    raise ValueError('selected frequencies must belong to the original public bank')
                # A selection is a set; canonical public order makes execution
                # and identities independent of caller ordering.
                selected_banks.append(tuple(value for value in self.frequencies if value in chosen))
        selected_sets = [set(bank) for bank in selected_banks]
        per_frequency_candidates = len(self.selected_epochs)*len(self.arrivals)*len(self.clocks)
        selected_counts = np.asarray([len(bank)*per_frequency_candidates for bank in selected_banks],
                                     dtype=np.int64)
        visited_counts = np.zeros(trials, np.int64)
        selected_union = set().union(*selected_sets)
        expected_tiles = (len(self.selected_epochs)*len(selected_union)*len(self.clocks)*
                          math.ceil(len(self.arrivals)/self.tile_size))
        if trial_batch_size is None:
            trial_batch_size = trials
        if (not isinstance(trial_batch_size, int) or isinstance(trial_batch_size, bool) or
                trial_batch_size < 1):
            raise ValueError('trial batch size must be a positive integer')
        valid = np.ones(samples, bool) if valid is None else np.asarray(valid)
        if valid.shape != (samples,) or not np.isin(valid, (0, 1)).all():
            raise ValueError('public validity must be a binary raw-sample vector')
        valid = valid.astype(bool, copy=True)
        accumulator_bytes = len(self.coherences)*trials*3*8
        # Three evidence arrays allow accumulator, tile result and logaddexp
        # scratch; input/template borrowing is still reserved in total cost.
        reserved = (data.nbytes+self.templates.nbytes+valid.nbytes+3*accumulator_bytes+
                    selected_counts.nbytes+visited_counts.nbytes)
        tile_budget = self.memory_limit_bytes-reserved
        if tile_budget <= 0:
            raise MemoryError('raw/template/result inputs leave no memory for a tile workspace')
        combined = np.full((len(self.coherences), trials, 3), -np.inf, np.float64)
        receipt = []
        projection_hash = None
        candidate_count = 0
        preparation = correlation = state = scoring_wall = 0.
        peak_owned = peak_preparation = 0
        cls_device = torch.device(self.device)
        for tile_index, tile in enumerate(self.tiles()):
            active_trials = [index for index, bank in enumerate(selected_sets) if tile.frequency in bank]
            if not active_trials:
                continue
            mark = time.perf_counter()
            receiver = self._receiver(tile, valid, tile_budget)
            if cls_device.type == 'cuda':
                torch.cuda.synchronize(cls_device)
            prep_seconds = time.perf_counter()-mark
            preparation += prep_seconds
            resolved = projection_digest(receiver)
            if projection_hash is None:
                projection_hash = resolved
            elif projection_hash != resolved:
                raise RuntimeError('resolved observation projection changed across acquisition tiles')
            if receiver.candidates != tile.candidates:
                raise RuntimeError('tile receiver candidate count differs from public partition')
            tile_correlation = tile_state = tile_wall = 0.
            fallback = []
            # Consecutive slices avoid copying a potentially large raw I/Q
            # batch through NumPy advanced indexing. Nonconsecutive selected
            # trials still share this prepared receiver workspace.
            batches = []
            for index in active_trials:
                if (batches and index == batches[-1][1] and
                        batches[-1][1]-batches[-1][0] < trial_batch_size):
                    batches[-1][1] += 1
                else:
                    batches.append([index, index+1])
            for first, stop in batches:
                mark = time.perf_counter()
                result = receiver.score(data[first:stop])
                if cls_device.type == 'cuda':
                    torch.cuda.synchronize(cls_device)
                elapsed = time.perf_counter()-mark
                evidence = np.asarray(result['hypothesis_log_evidence'], np.float64)
                if evidence.shape != (len(self.coherences), stop-first, 3) or not np.isfinite(evidence).all():
                    raise ValueError('tile returned invalid hypothesis evidence')
                # A tile result is log(mean candidate likelihood), not a sum.
                # Weight by its actual count (including the short last tile).
                combined[:, first:stop] = np.logaddexp(combined[:, first:stop],
                                                       evidence+math.log(tile.candidates))
                tile_correlation += float(result['correlation_seconds'])
                tile_state += float(result['state_seconds'])
                tile_wall += elapsed
                for backend in result.get('scan_backend', ()):
                    if backend.get('fallback_reason') and backend['fallback_reason'] not in fallback:
                        fallback.append(backend['fallback_reason'])
            work = receiver.work()
            peak_owned = max(peak_owned, int(work['owned_bytes_estimate']))
            peak_preparation = max(peak_preparation, int(work['preparation_peak_bytes_estimate']))
            receipt.append(dict(index=tile_index, epoch=tile.epoch, arrivals=tile.arrivals,
                frequency_hz=tile.frequency, clock_ppm=tile.clock_ppm, candidates=tile.candidates,
                trial_indices=active_trials, candidate_trial_evaluations=tile.candidates*len(active_trials),
                projection_sha256=resolved, receiver_procedure_id=receiver.procedure_id,
                preparation_seconds=prep_seconds, correlation_seconds=tile_correlation,
                state_seconds=tile_state, score_wall_seconds=tile_wall,
                owned_bytes_estimate=work['owned_bytes_estimate'],
                preparation_peak_bytes_estimate=work['preparation_peak_bytes_estimate'],
                scan_fallback_reasons=fallback))
            if progress is not None:
                progress(dict(receipt[-1]))
            candidate_count += tile.candidates
            visited_counts[active_trials] += tile.candidates
            correlation += tile_correlation
            state += tile_state
            scoring_wall += tile_wall
            # Drop the workspace before constructing the next one. CUDA's
            # allocator may cache freed blocks; that is not a live second tile.
            del receiver, result
        if (candidate_count != per_frequency_candidates*len(selected_union) or
                len(receipt) != expected_tiles or not np.array_equal(visited_counts, selected_counts)):
            raise RuntimeError('tiled bank did not visit exactly the declared candidate count')
        combined -= np.log(selected_counts)[None, :, None]
        mixed = logsumexp(combined, axis=0)-math.log(len(self.coherences))
        validity_hash = hashlib.sha256(valid.tobytes()).hexdigest()
        execution = dict(base=self.procedure_id, valid_sha256=validity_hash,
                         resolved_projection_sha256=projection_hash)
        # The unchanged full-bank procedure must not acquire a different
        # identity merely because captures are scored in a larger batch.
        # Explicit per-trial selection remains a realized execution detail;
        # HelperFrequencyAcquisition supplies its stable outer policy ID.
        if selected_frequency_banks is not None:
            execution['selected_frequency_banks'] = selected_banks
        return dict(hypothesis_log_evidence=combined,
            model_bit_scores=combined[:, :, 1:]-combined[:, :, :1],
            bit_scores=mixed[:, 1:]-mixed[:, :1],
            procedure_id=_digest(execution),
            policy=self.policy, tiles=receipt,
            work=dict(trials=trials, samples=samples, candidates=candidate_count, tiles=len(receipt),
                original_candidates=self.candidates, original_tiles=self.tile_count,
                candidates_per_trial=selected_counts.tolist(),
                frequencies_per_trial=[len(bank) for bank in selected_banks],
                selected_frequency_banks=selected_banks,
                maximum_candidates_per_tile=min(len(self.arrivals), self.tile_size),
                trial_batch_size=trial_batch_size, total_candidate_trial_evaluations=int(selected_counts.sum()),
                input_template_result_reserved_bytes=reserved, per_tile_memory_budget=tile_budget,
                peak_tile_owned_bytes_estimate=peak_owned,
                peak_tile_preparation_bytes_estimate=peak_preparation,
                total_preparation_peak_bytes_estimate=reserved+peak_preparation,
                projection_sha256=projection_hash, validity_sha256=validity_hash,
                requested_pilot_use=self.requested_pilot_use, effective_pilot_use=self.pilot_use,
                pilot_timing_guard=self.timing_guard,
                device=str(self.device), precision=self.precision),
            timing=dict(planning_seconds=self.planning_seconds, preparation_seconds=preparation,
                correlation_seconds=correlation, state_seconds=state, score_wall_seconds=scoring_wall,
                total_score_call_seconds=time.perf_counter()-started,
                scope='Complete tiled score call includes every tile preparation and trial batch. '
                      'Native generation, input loading and object planning are separate.'))


class HelperFrequencyAcquisition(TiledAcquisition):
    """Independent Helper frequency selection followed by tiled DATA acquisition.

    The stable outer ``procedure_id`` describes the complete selection, fallback
    and phase-guard policy. The realized subset identity is returned separately
    as ``execution_id``. Calibrate the entire procedure, including its full-bank
    fallbacks, rather than reusing a gate from any individual selected bank.
    Frequency inference never grants cross-frequency coherent phase authority.
    """

    def __init__(self, manifest, raw_templates, epoch_offsets, *, frequency_config=None, **kwargs):
        from triad_dynamic_frequency import frequency_policy
        super().__init__(manifest, raw_templates, epoch_offsets, **kwargs)
        if frequency_config is not None and not isinstance(frequency_config, dict):
            raise ValueError('frequency config must be a dictionary')
        self.frequency_config = copy.deepcopy(frequency_config or {})
        self.frequency_config.setdefault('data_frequency_aperture_seconds', self.block_seconds)
        estimator_policy = frequency_policy(self.frequency_config)
        if estimator_policy['data_frequency_aperture_seconds'] > self.block_seconds:
            raise ValueError('frequency allowance aperture must not exceed the acquisition block duration')
        self.acquisition_policy = self.policy
        self.acquisition_procedure_id = self.procedure_id
        self.policy = dict(schema='triad-helper-frequency-acquisition-v1',
            acquisition=self.acquisition_policy, frequency=estimator_policy,
            frequency_source_sha256=hashlib.sha256(
                (Path(__file__).parent/'triad_dynamic_frequency.py').read_bytes()).hexdigest(),
            frequency_phase_independent=True,
            clock_policy='Preserve the complete public clock bank; frequency selection grants no clock estimate.',
            projection_policy='Preserve the complete original frequency/clock bank projection for every capture.',
            horizon_policy='A narrowed frequency region must cover the entire scored capture; '
                           'expired regions revert to the complete bank.',
            calibration_scope='Complete adaptive frequency selection, fallback and independent phase guard; '
                              'no inherited gate or operational false-admission guarantee.')
        self.procedure_id = _digest(dict(acquisition=self.acquisition_procedure_id, policy=self.policy))

    def score(self, raw, valid=None, *, trial_batch_size=None, progress=None):
        from triad_dynamic_frequency import estimate_dynamic_frequency
        started = time.perf_counter()
        data = np.asarray(raw)
        if data.ndim == 1:
            data = data[None, :]
        samples = int(self.manifest['calendar']['capture_samples'])
        if (data.ndim != 2 or data.shape[0] < 1 or data.shape[1] != samples or
                not np.iscomplexobj(data)):
            raise ValueError('complex raw input needs [trials,public capture samples]')
        validity = np.ones(samples, bool) if valid is None else np.asarray(valid)
        if validity.shape != (samples,) or not np.isin(validity, (0, 1)).all():
            raise ValueError('public validity must be a binary raw-sample vector')
        mark = time.perf_counter()
        assistance = [estimate_dynamic_frequency(row, self.manifest, self.frequencies,
            valid=validity, config=self.frequency_config) for row in data]
        estimator_seconds = time.perf_counter()-mark
        capture_end = (samples-1)/float(self.manifest['sample_rate_hz'])-float(
            self.manifest['calendar']['capture_lead_seconds'])
        for result in assistance:
            if result.get('status') not in ('narrowed', 'fallback'):
                raise ValueError('frequency estimator returned an unsupported status')
            chosen = _bank(result['selected_frequencies'], 'estimated frequency bank')
            if result['status'] == 'fallback' and set(chosen) != set(self.frequencies):
                raise ValueError('frequency fallback must retain the complete original bank')
            if result['status'] == 'narrowed':
                horizon = result.get('applicable_horizon')
                if horizon is None or not math.isfinite(horizon):
                    raise ValueError('narrowed frequency estimate requires a finite applicability horizon')
                if horizon+1e-12 < capture_end:
                    result.update(status='fallback', reason='frequency_horizon_does_not_cover_capture',
                        selected_frequencies=list(self.frequencies), selected_count=len(self.frequencies))
        result = super().score(data, validity, trial_batch_size=trial_batch_size, progress=progress,
                              selected_frequency_banks=[row['selected_frequencies'] for row in assistance])
        result['execution_id'] = result['procedure_id']
        result['procedure_id'] = _digest(dict(base=self.procedure_id,
            valid_sha256=result['work']['validity_sha256'],
            resolved_projection_sha256=result['work']['projection_sha256']))
        result['frequency_assistance'] = assistance
        result['phase_assistance'] = copy.deepcopy(self.timing_guard)
        result['timing']['frequency_estimation_seconds'] = estimator_seconds
        result['timing']['total_score_call_seconds'] = time.perf_counter()-started
        result['timing']['scope'] = ('Complete adaptive call includes Helper frequency estimation, '
            'selected tile preparation and all selected trial scoring. Native generation, '
            'input loading and object planning are separate.')
        return result
