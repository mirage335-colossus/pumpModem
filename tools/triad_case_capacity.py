"""Actual native inputs and complete-receiver work for manual capacity checks.

Evaluator-only payload/channel choices stay here. The signal receiver receives
public configuration, samples and a public keyed-basis service. Timing probes
are not message-success trials. Public calendar intervals, rather than summed
overlapping search-window durations, normalize sustained capacity forecasts.
"""
from __future__ import annotations

import copy
import math
import time

import numpy as np

from triad_case_receiver import _native_cpu_seconds, _rss_snapshot
from triad_case_replay import PreparedReplay, _CHUNK, _clean_options
from triad_continuous import NativeStream
from triad_realtime_fft import FFTScreen
from triad_receiver import CompleteBitReceiver


SUPPORTED_MODEL_STATUSES = frozenset(('abbreviated_holdout_supported',
    'fixed_extent_measured_no_extrapolation', 'exact_geometry_measured',
    'declared_geometry_holdout_supported'))


def unqualified_blind_capacity(slot_seconds=None):
    """Keep configured-calendar timings distinct from blind listening."""
    result=dict(status='unqualified',ratio_processing_seconds_per_baseband_second=None,
        reason='fresh_start_discovery_and_occupied_input_competition_not_qualified',
        missing_work=['Independent transmitter fresh-start discovery',
                      'New-start admission against an already active DATA waveform'],
        scope='A configured-public-calendar capacity result does not qualify a blind continuously listening receiver.')
    if slot_seconds is not None:
        result.update(naive_full_window_attempt_density_multiplier=slot_seconds/30.,
            naive_density_scope='Hypothetical full-window search at every 30-second public start, relative to one attempt per configured slot. This rejected strategy is not implemented, is not a CPU estimate, and does not supply a valid occupied-input admission rule.')
    return result


def public_calendar(source, *, slots=24):
    """Exact public intervals for the local 22-bit reference and two quiet slots.

    This is a fixed benchmark profile, never a received length. Successor
    intervals vary with the native calendar, so no constant stride is inferred
    from a message's aggregate transmit time. Use the shortest observed public
    interval for the conservative sustained service forecast.
    """
    if isinstance(slots, bool) or not isinstance(slots, int) or not 2 <= slots <= 418:
        raise ValueError('local calendar profile must contain 2..418 slots')
    plans = [source.slot_plan(index) for index in range(slots)]
    rate = plans[0]['sample_rate_hz']
    intervals = []
    for index, plan in enumerate(plans):
        if plan['sample_rate_hz'] != rate or plan['index'] != index:
            raise ValueError('inconsistent public slot calendar')
        stride = plan['next_source_begin_samples']-plan['source_begin_samples']
        if stride <= 0 or (index and plans[index-1]['next_source_begin_samples'] != plan['source_begin_samples']):
            raise ValueError('public slot calendar does not advance continuously')
        intervals.append(stride/rate)
    return dict(plans=plans, intervals_seconds=intervals,
        minimum_interval_seconds=min(intervals), maximum_interval_seconds=max(intervals),
        scope='Exact native public slot plans for a locally fixed benchmark profile; no received payload count.')


def _recurring_window(calendar):
    plans = calendar['plans']
    def input_span(index):
        plan = plans[index]
        return plan['receiver_window_stop_sample'] - min(
            plan['receiver_window_first_sample'], plans[index-1]['receiver_window_stop_sample'])
    index = max(range(1, len(plans)), key=lambda i: (
        plans[i]['receiver_window_samples'], input_span(i), i))
    return dict(index=index, window_samples=plans[index]['receiver_window_samples'],
        ingress_samples=input_span(index), maximum_ingress_samples=max(
            input_span(i) for i in range(1, len(plans))))


def _production_stream_workspace(calendar, capture_samples, memory_limit_bytes):
    from triad_stream_receiver import _MAXIMUM_SLOTS, _RECEIPT_BYTES

    maximum_window = max(plan['receiver_window_samples'] for plan in calendar['plans'])
    # CompleteStreamReceiver's default limit is one quarter of the budget
    # for retained input. At dispatch a sliced leading block can still own
    # its skipped samples and the final chunk may extend past this window.
    if maximum_window > memory_limit_bytes//(4*17):
        raise MemoryError('public window exceeds the production stream input buffer limit')
    retained = 17*(maximum_window+2*(_CHUNK-1))
    receipts = _MAXIMUM_SLOTS*_RECEIPT_BYTES
    inflight = 17*_CHUNK
    reserve = retained+receipts+inflight
    receiver = memory_limit_bytes-reserve
    assembled = 17*max(capture_samples, maximum_window)
    if receiver <= assembled:
        raise MemoryError('production stream reserve leaves no assembled-window receiver workspace')
    return dict(maximum_window_samples=maximum_window,
        retained_input_bound_bytes=retained, receipt_reserve_bytes=receipts,
        inflight_chunk_bound_bytes=inflight, simultaneous_outer_reserve_bytes=reserve,
        receiver_limit_bytes=receiver, assembled_input_bound_bytes=assembled,
        bank_limit_bytes=receiver-assembled,
        scope='Production complex128/uint8 input profile, declared native windows and one maximum-size inflight chunk. Retained blocks, assembled input, and bank/DSP can coexist.')


def _receipt(began, cpu_began, source, child_before, seconds, workers):
    elapsed = time.perf_counter()-began
    timing = _cpu_timing(elapsed, cpu_began, source, child_before, workers)
    return dict(ratio_processing_seconds_per_baseband_second=elapsed/seconds,
        represented_baseband_seconds=seconds,
        observed_cpu_utilization_fraction=timing['utilization_fraction'],
        native_child_cpu_in_utilization=timing['native_cpu_seconds'] is not None,
        cpu_timing=timing,
        utilization_scope='Python process and owned native basis service; CPU counters diagnose utilization, never substitute for elapsed service.',
        workers=workers, numerical_threads_per_worker=1)


def _cpu_timing(elapsed, cpu_began, source, child_before, workers):
    process_cpu = time.process_time()-cpu_began
    child_after = _native_cpu_seconds(source)
    child = (None if child_before is None or child_after is None
             else max(0., child_after-child_before))
    busy = (process_cpu+(child or 0.))/elapsed if elapsed else None
    return dict(observed_elapsed_seconds=elapsed, process_cpu_seconds=process_cpu, native_cpu_seconds=child,
        allocated_workers=workers, busy_cores=busy,
        utilization_fraction=busy/workers if busy is not None else None)


def _aggregate_cpu(samples):
    timing = [row['cpu_timing'] for row in samples if 'cpu_timing' in row]
    if not timing:
        return None
    wall = sum(row['observed_elapsed_seconds'] for row in timing)
    available = sum(row['observed_elapsed_seconds']*row['allocated_workers'] for row in timing)
    process = sum(row['process_cpu_seconds'] for row in timing)
    native = sum(row['native_cpu_seconds'] or 0. for row in timing)
    return dict(observed_elapsed_seconds=wall, process_cpu_seconds=process, observed_native_cpu_seconds=native,
        native_cpu_samples=sum(row['native_cpu_seconds'] is not None for row in timing),
        samples=len(timing), busy_cores=(process+native)/wall if wall else None,
        utilization_fraction=(process+native)/available if available else None)


def acquisition_work_class(result):
    """Algorithmic acquisition demand, independent of worker dispatch and scores."""
    helper = result.get('helper', {})
    coarse_helper = helper.get('coarse_frequency', {})
    frequency = helper.get('frequency') or {}
    coarse = result.get('coarse', {})
    work = coarse.get('work') or {}
    return dict(status=result['status'], helper_procedure_id=helper.get('procedure_id'),
        helper_reason=helper.get('reason'), helper_original_count=helper.get('original_count'),
        helper_selected_count=helper.get('selected_count'),
        helper_frequency_status=frequency.get('status'),
        helper_frequency_apertures=frequency.get('aperture_count'),
        helper_coarse_seconds=coarse_helper.get('observation_seconds'),
        helper_coarse_targets=len(coarse_helper.get('targets_hz', ())),
        helper_profile_points={name:row.get('profile_points')
            for name,row in coarse_helper.get('pilot_diagnostics', {}).items()},
        helper_fit_iterations=[row['evaluations'] for row in coarse_helper.get('core_refinements', ())],
        coarse={key:work[key] for key in ('workspaces','frequency_points','arrival_points',
            'fft_products','correlation_method') if key in work},
        proposed_candidates=len(coarse.get('candidates', ())),
        fine_candidates=len(result.get('coordinates', ())))


def active_probe_origin(plan, descriptor, fs, samples, coordinates, *, required_samples):
    """Choose an interior public sample using an observed coarse proposal."""
    source_first = int(plan['geometry']['source_calendar_origin_sample'])
    interior = .5*min(float(plan['format']['tile_seconds']),
                      float(plan['format']['elapsed_bit_seconds']))
    # The public arrival prior can exceed the short HF waveform's length.
    # A fixed capture midpoint may therefore contain no candidate template.
    # Use an observed coarse proposal, never the simulator's true arrival.
    if not coordinates:
        raise ValueError('an observed public proposal is required for active fine work')
    _, arrival, _, clock = coordinates[0]
    first = round((float(descriptor['capture_lead_seconds'])+arrival)*fs+
        (int(descriptor['data_offset_samples'])+source_first+interior*fs)/
        (1+clock*1e-6))
    return min(max(0, first), samples-required_samples)


class PreparedCapacityCase:
    """One fixed actual channel/input and public complete-bit receiver geometry.

    Coarse template construction reuses the canonical validated replay builder.
    It is recorded as recurring work for each new public epoch/ordinal, even
    when a first bank can be prepared before transmission. Native observation
    generation and injected noise are excluded simulator work.
    """
    def __init__(self, executable, case, plan, *, memory_limit_bytes,
                 measurement_seconds=1., channel_seed=74021, noise_seed=88200,
                 source_bit=0, cn0_db_hz=None, bank_workers=1, screen_factory=FFTScreen,
                 noise_only=False):
        if (isinstance(source_bit, bool) or not isinstance(source_bit, int) or
                source_bit not in (0, 1)):
            raise ValueError('evaluator source bit must be zero or one')
        if (isinstance(measurement_seconds, bool) or
                not isinstance(measurement_seconds, (int, float)) or
                not math.isfinite(measurement_seconds) or not .1 <= measurement_seconds <= 2):
            raise ValueError('measurement seconds must be in[.1,2]')
        self.executable, self.case, self.plan = executable, case, plan
        self.channel_seed, self.noise_seed, self.source_bit = channel_seed, noise_seed, source_bit
        self.memory_limit_bytes = memory_limit_bytes
        self.bank = self.basis_source = None
        self.raw = self.valid = None
        try:
            self.bank = PreparedReplay(executable, case['native_args'],
                memory_limit_bytes=memory_limit_bytes, threads=bank_workers, seed=channel_seed,
                screen_factory=screen_factory, bank_memory_limit=self._reserve_stream_workspace)
            self.screen = self.bank.screen
            # CompleteBitReceiver dispatches explicit scoring partitions.
            # Keep a serial FFT fallback so a one-worker pilot cannot inherit
            # a larger allocation from resident bank construction.
            self.screen.workers = 1
            self.bank_workers = bank_workers
            self.fs, self.samples = self.bank.fs, self.screen.samples
            self.observation_first_sample = 0
            self.window_seconds = self.samples/self.fs
            self.unit_samples = max(1, round(measurement_seconds*2))*round(.5*self.fs)
            self.unit_samples = min(self.unit_samples, self.samples//4)
            if self.unit_samples < 4:
                raise ValueError('public window cannot contain the fixed fine holdout')
            if self.screen.resident_estimate+17*self.samples >= self.receiver_memory_limit_bytes:
                raise MemoryError('complete raw observation and public bank exceed local workspace budget')
            cn0 = float(plan['physics']['physical_data_cn0_db_hz'] if cn0_db_hz is None else cn0_db_hz)
            if not math.isfinite(cn0):
                raise ValueError('finite injected DATA C/N0 required')
            self.noise_variance = self.fs*10**(-cn0/10)
            if not math.isfinite(self.noise_variance) or self.noise_variance <= 0:
                raise ValueError('injected raw noise variance is outside finite range')
            self.manifest = copy.deepcopy(self.bank.manifest)
            self.manifest['power']['complex_noise_variance'] = self.noise_variance
            self.cn0_db_hz = cn0
            self.arguments = list(self.bank.arguments)
            source = None
            if not noise_only:
                command = _clean_options(executable, self.arguments, bit=source_bit, seed=channel_seed)
                source = NativeStream(executable, command[1:], kind='channel')
            began = time.perf_counter()
            try:
                if source is not None and source.descriptor['total_samples'] != self.samples:
                    raise ValueError('native observation and public search geometry differ')
                self.raw = np.empty(self.samples, dtype=np.complex128)
                self.valid = np.empty(self.samples, dtype=np.uint8)
                if source is None:
                    noise_only_observation(self)
                else:
                    random = np.random.default_rng(noise_seed)
                    sigma = math.sqrt(self.noise_variance/2)
                    for first in range(0, self.samples, _CHUNK):
                        count = min(_CHUNK, self.samples-first)
                        values, valid = source.next(count)
                        if len(values) != count or len(valid) != count:
                            raise ValueError('native simulation ended before public window')
                        noise = random.standard_normal((count, 2))
                        self.raw[first:first+count] = values+sigma*(noise[:, 0]+1j*noise[:, 1])
                        self.valid[first:first+count] = valid
            finally:
                if source is not None:
                    source.close()
            self.simulator_generation_seconds = time.perf_counter()-began
            # This independent service exposes public keyed alternatives only.
            # Its chosen emitted bit is irrelevant: observation reads are never
            # used as receiver templates or as receiver initialization.
            basis_command = _clean_options(executable, self.arguments, bit=0, seed=channel_seed)
            began = time.perf_counter()
            self.basis_source = NativeStream(executable, basis_command[1:], kind='channel')
            # The full native bounds remain in public_geometry's slot plans.
            # Only these statistic-only fields may differ from the bank that
            # was prepared before the independent basis service was opened.
            for key in ('quiet_aperture_first_sample', 'quiet_aperture_stop_sample'):
                self.manifest['calendar'][key] = self.calendar['plans'][0][key]
            self.slot_seconds = self.calendar['minimum_interval_seconds']
            self.basis_service_preparation_seconds = time.perf_counter()-began
            self.receiver_preparation_seconds = (
                self.bank.receiver_preparation_seconds+self.basis_service_preparation_seconds)
            self.public_geometry = dict(
                sample_rate_hz=self.fs, search_window_samples=self.samples,
                search_window_seconds=self.window_seconds,
                conservative_public_slot_interval_seconds=self.slot_seconds,
                public_calendar=self.calendar,
                recurring_window=self.recurring_window,
                production_stream_workspace=self.stream_workspace,
                maximum_capacity_window_seconds=self.maximum_window_seconds,
                fine_unit_samples=self.unit_samples,
                fine_unit_seconds=self.unit_samples/self.fs,
                source_samples=self.screen.source_samples,
                coarse=self.bank.coverage,
                denominator_scope='Sustained forecast uses the shortest exact public interval in the declared local profile. Ingesting all intervening samples is required; search-window exposures are never summed as unique received baseband.')
        except BaseException:
            self.close()
            raise

    def receiver(self, workers):
        return CompleteBitReceiver(self.manifest, self.screen, self.basis_source,
            workers=workers, memory_limit_bytes=self.receiver_memory_limit_bytes)

    def _reserve_stream_workspace(self, source):
        self.calendar = public_calendar(source)
        self.recurring_window = _recurring_window(self.calendar)
        descriptor = source.descriptor
        samples = int(descriptor['total_samples'])
        self.stream_workspace = _production_stream_workspace(self.calendar, samples, self.memory_limit_bytes)
        self.receiver_memory_limit_bytes = self.stream_workspace['receiver_limit_bytes']
        self.maximum_window_seconds = max(samples, self.recurring_window['window_samples'])/float(
            descriptor['sample_rate_hz'])
        return self.stream_workspace['bank_limit_bytes']

    def admission_model_receipt(self):
        """Public normal-lane mass, never the inflated timing work-probe prior."""
        from triad_operational_admission import operational_threshold
        with self.receiver(1) as receiver:
            residual=(receiver.policy['fine_delay_subdivisions']+1)*(
                3 if receiver.initial_rate_half_width_ppm>0 else 1)
            return dict(schema='triad-complete-admission-model-v1',
                operational_evidence='fixed_initial_affine',
                full_grid=dict(receiver.grid.__dict__),full_grid_identity=receiver.grid.identity(),
                full_grid_count=receiver.grid.candidate_count,
                operational_budget=operational_threshold(**receiver.admission_policy),
                model_count=len(receiver.coherences),residual_state_count=residual,
                affine_lane_weight=1.,
                public_slot_indices=[row['index'] for row in self.calendar['plans']],
                source_duration_seconds=receiver.screen.duration,
                fine_delay_step_seconds=receiver.initial_delay_step_seconds,
                residual_rate_half_width_ppm=receiver.initial_rate_half_width_ppm,
                scope='Fixed native source duration, public bounds and normal residual lattice across the declared local slot profile; no work-probe priors or selected-bank mass renormalization.')

    def quiet_energy_diagnostic(self, target):
        """Evaluator-only fixed-channel bound; excluded from receiver timing."""
        from triad_quiet_diagnostic import QuietEnergyAccumulator, quiet_probability_bound
        remaining=(self.memory_limit_bytes-self.raw.nbytes-self.valid.nbytes-
            self.screen.resident_estimate-max(self.basis_source.descriptor['fixed_workspace_bytes'],
                self.basis_source.descriptor['workspace_budget_bytes']))
        if remaining <= 0:
            raise MemoryError('quiet projection diagnostic has no remaining workspace')
        with self.receiver(1) as receiver:
            options=dict(samples=self.samples,sample_rate=self.fs,
                block_samples=round(receiver.policy['block_seconds']*self.fs),
                segment_samples=round(receiver.policy['segment_seconds']*self.fs),
                quiet_sample_bounds=receiver.quiet_aperture,
                noise_variance=self.noise_variance,required_bands=receiver.required_bands,
                excluded_bands=receiver.excluded_bands,alpha=receiver.policy['noise_upper_tail_probability'],
                maximum_input_chunk_samples=_CHUNK,memory_limit_bytes=remaining//2)
            clean=QuietEnergyAccumulator(**options)
            noisy=QuietEnergyAccumulator(**options)
            if clean.workspace_estimate_bytes+noisy.workspace_estimate_bytes+128*_CHUNK>remaining:
                raise MemoryError('paired quiet projections and noise replay exceed workspace')
            random=np.random.default_rng(self.noise_seed)
            sigma=math.sqrt(self.noise_variance/2)
            for first in range(0,self.samples,_CHUNK):
                count=min(_CHUNK,self.samples-first)
                noise=random.standard_normal((count,2))
                values=self.raw[first:first+count]
                reconstructed=values-sigma*(noise[:,0]+1j*noise[:,1])
                mask=self.valid[first:first+count]
                clean.push(first,reconstructed,mask)
                noisy.push(first,values,mask)
            report=clean.finish();observed=noisy.finish()
            report['observed_noisy_projected_energy_normalized']=(
                observed['projected_signal_energy']/self.noise_variance)
            report['observed_noisy_modes']=observed['projected_modes']
            report['model_target_points']={}
            if report['complete']:
                for name in ('single_bit','fox'):
                    cn0=target.get(name,{}).get('target_cn0_db_hz')
                    if cn0 is not None:
                        report['model_target_points'][name]=dict(cn0_db_hz=cn0,
                            **quiet_probability_bound(report['projected_signal_energy'],
                                report['projected_modes'],self.fs*10**(-cn0/10),
                                alpha=receiver.policy['noise_upper_tail_probability']))
            report['evaluator_provenance']=dict(channel_seed=self.channel_seed,
                noise_seed=self.noise_seed,source_bit=self.source_bit,
                noise_chunk_samples=_CHUNK,
                scope='Clean fixed-channel samples reconstructed by exact seeded noise replay in FP64. Projection-only diagnostic is outside all receiver service measurements; it cannot initialize or admit a receiver hypothesis.')
            return report

    def acquisition(self, receiver):
        before = _native_cpu_seconds(self.basis_source)
        began, cpu = time.perf_counter(), time.process_time()
        result = receiver.acquire(self.raw, self.valid, coarse_unit_limit=0)
        timing = _receipt(began, cpu, self.basis_source, before,
            self.slot_seconds, receiver.workers)
        return dict(timing, result=result,
            acquisition_work_class=acquisition_work_class(result),
            receiver_peak_estimate_bytes=max(receiver.helper_peak_bytes,
                result.get('coarse',{}).get('work',{}).get('estimated_peak_bytes',0)),
            rss=_rss_snapshot(self.basis_source),
            search_window_seconds=receiver.samples/self.fs,
            ratio_scope='Actual complete Helper/filter/coarse/refinement-selection work per public slot cadence.')

    def probe_origin(self, stratum, coordinates, *, required_samples=None):
        length = 4*self.unit_samples if required_samples is None else required_samples
        if stratum == 'terminal':
            return self.samples-length
        if stratum != 'active':
            raise ValueError('unknown public fine-work stratum')
        return active_probe_origin(self.plan, self.bank.descriptor, self.fs,
            self.samples, coordinates, required_samples=length)

    def fine_probe(self, receiver, coordinates, *, units, stratum='active', work_probe=False,
                   interior_erasure=False):
        if isinstance(units, bool) or not isinstance(units, int) or units not in (1, 2, 4):
            raise ValueError('fine probe units must be one, two or four')
        segment_samples = max(1,round(receiver.policy['segment_seconds']*self.fs))
        # Two segments execute an actual posterior transition/phase transport
        # and merge after the first capped emission. The forecast charges this
        # entire two-segment unit at every real segment boundary.
        unit_samples = 2*segment_samples if work_probe else self.unit_samples
        samples = units*unit_samples
        first = self.probe_origin(stratum, coordinates, required_samples=4*unit_samples)
        before = _native_cpu_seconds(self.basis_source)
        began, cpu = time.perf_counter(), time.process_time()
        completed=[]
        for unit in range(units if work_probe else 1):
            address=self.calendar['plans'][0]
            reset=self.basis_source.set_basis_address(address['pattern_epoch'],address['symbol_ordinal'])
            if (reset.get('basis_epoch')!=address['pattern_epoch'] or
                    reset.get('basis_symbol_ordinal')!=address['symbol_ordinal'] or
                    reset.get('template_cache_cleared') is not True):
                raise ValueError('fine work requires an acknowledged cold native basis cache')
            origin = first+unit*unit_samples if work_probe else first
            count_samples = unit_samples if work_probe else samples
            mask=self.valid[origin:origin+count_samples]
            if interior_erasure:
                mask=mask.copy()
                boundary=max(1,round(receiver.policy['block_seconds']*self.fs))
                mask[max(0,boundary-1):min(count_samples,boundary+1)]=0
            session = receiver.create_fine_session(coordinates, samples=count_samples,
                physical_origin=origin, work_probe=work_probe)
            try:
                for offset in range(0,count_samples,receiver.policy['chunk_samples']):
                    count=min(receiver.policy['chunk_samples'],count_samples-offset)
                    session.push(offset,self.raw[origin+offset:origin+offset+count],
                        mask[offset:offset+count])
                result=session.finish()
                result['native_cache_reset_receipt']=reset
                if work_probe:
                    result['capacity_transition_coverage']=cap_transition_coverage(result)
                    if not result['capacity_transition_coverage']['supported']:
                        raise ValueError('fine state-ceiling probe did not execute the declared capped transition regime')
                endpoint=receiver.classify(result,coordinates,
                    all_input_valid=bool(mask.all()))
            finally:
                session.close()
            completed.append(result)
        result=copy.deepcopy(completed[-1]) if work_probe else completed[-1]
        if work_probe:
            result['repeated_work_units']=[dict(state_work_regime=r.get('state_work_regime'),
                work_counts=r.get('work_counts'),native_template_work=r['native_template_work'],
                segments=r['segments'],initial_states=r.get('initial_lattice',{}).get('states'),
                timing=r.get('timing'),capacity_transition_coverage=r['capacity_transition_coverage'],
                native_cache_reset_receipt=r['native_cache_reset_receipt']) for r in completed]
            result['consumed_samples']=result['processed_rows']=samples
            result['complete_observation']=all(r['complete_observation'] for r in completed)
            for key in ('blocks','segments'):
                result[key]=sum(r[key] for r in completed)
            for key in result['work_counts']:
                result['work_counts'][key]=sum(r['work_counts'][key] for r in completed)
            native=result['native_template_work']
            for key in ('calls','returned_elements','energy'):
                native[key]=sum(r['native_template_work'][key] for r in completed)
            native['representative_work']=all(r['native_template_work']['representative_work'] for r in completed)
            result['working_bytes_estimate']=max(r['working_bytes_estimate'] for r in completed)
        timing = _receipt(began, cpu, self.basis_source, before,
            samples/self.fs, receiver.workers)
        return dict(timing, stratum=stratum, units=units,
            interior_erasure_control=interior_erasure,
            public_first_sample=first, result=result, endpoint=endpoint,
            work_unit_samples=unit_samples,
            receiver_peak_estimate_bytes=(receiver.input_workspace_bytes+
                receiver.native_workspace_bytes+receiver.screen.resident_estimate+
                receiver.policy['cache_bytes']+result['working_bytes_estimate']),
            rss=_rss_snapshot(self.basis_source),
            ratio_scope='Actual fine setup, native cache fills and complete persistent segment work per probed input second; extrapolation requires the declared fixed geometry and holdout.')

    def close(self):
        if self.basis_source is not None:
            self.basis_source.close()
            self.basis_source = None
        if self.bank is not None:
            self.bank.close()
            self.bank = None
        self.raw = self.valid = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def measurement_policy(case_count, unit_seconds):
    """Predeclared small-sample contract shared with the regression machinery."""
    from triad_case_metrics import LOOKS, BATCHES
    return dict(schema='triad-complete-capacity-policy-v2', looks=list(LOOKS),
        batch_units=list(BATCHES), unit_baseband_seconds=unit_seconds,
        allocation_selection='One matched same-stream acquisition and one fine unit per supported worker count; fresh samples after selection.',
        allocation_counts='1,2,4,known physical cores,full visible logical CPU budget; deduplicated and bounded.',
        strata=['complete public acquisition', 'sustained nominal fine search and persistent tracking',
                'terminal fine processing', 'noise-only full-prior acquisition and fine work',
                'recurring public ordinal bank preparation', 'largest recurring-window reacquisition and ingress',
                'stream ingest and quiet completion with canonical codec'],
        family_size=max(1,case_count*9), family_alpha=.05,
        holdout='One and two units train, four units hold out; unchanged source/geometry and recorded state/cache regime required.',
        stopping='First declared3/5/9 look resolving the conditional service decision; stop at9 with unresolved uncertainty. No full heavy search as a fallback.',
        nominal_continuation=dict(prefix_baseband_seconds=2, checkpoints_baseband_seconds=[1, 2, 4],
            maximum_actual_probe_seconds_per_row=54, maximum_outer_observations_per_row=9,
            adaptive_state_relative_range_limit=1.3, complete_public_acquisition_required=True,
            stopping='Stop at the first material state, cache, cost holdout or coverage failure. Seek supported relative precision at the declared 3/5/9 looks.'),
        nominal_stopping_scope='Continuation rate uses the search-window interval. Stop for supported relative precision, the bounded 3/5/9 limit or a failed work/holdout check; decide real time only after slot normalization and stage combination.',
        sustained_allocation_probe=dict(opt_in=True,
            scopes=['pilot-selected worker count', 'visible physical cores within quota', 'full logical CPU budget'],
            observations_per_allocation=3, maximum_additional_repeated_baseband_probe_seconds_per_row=36,
            family_size=3*max(1,case_count*8),
            stopping='Reuse the selected first three valid observations; try each distinct alternate at most once. Stop an alternate on missing complete acquisition, work drift or its first declared look.',
            decision='Exploratory resident fine work only; never selects inference allocation or qualifies capacity.'),
        fine_forecast='Charge the measured two-second prefix and endpoint once per slot. Apply the continued-session rate to the remaining window. Terminal, listening and cold-state ceiling probes remain separate.',
        fine_state_ceiling='Fresh two-segment units start256 states per lane and execute a real transition/merge. Charge each entire unit once per full-window segment; this conservative geometry estimate includes repeated startup and double emission work.',
        recurring_window='Use the existing second recurring bank for actual H0 acquisition and full copy/assembly at the largest native public recurring window, including its public input gap. Use the largest declared window for separate fine-state costs.',
        physical_denominator='Shortest exact native successor interval in the fixed local24-slot reference; observed complete streams use unique actual received samples through the physical end.',
        preparation='First bank readiness is separate; the same bank work for every later epoch/ordinal is recurring receiver service.',
        cpu_metric='Prepared elapsed processing seconds per received baseband second; thread CPU counters diagnose utilization only.',
        success='Timing repeats are not independent communication trials. Full-message success and its SNR probability are separate claims.')


def cap_transition_coverage(result):
    """Require actual two-lane boundary work before extrapolating its cost."""
    adaptive=result['lanes']['adaptive']; nominal=result['lanes']['initial_affine']
    state=adaptive['state_work_regime']; fixed=nominal['state_work_regime']
    checks=dict(two_segments=result['segments']==2,
        adaptive_observations=adaptive['timing']['observations']==2,
        adaptive_prediction=adaptive['timing']['predictions']==1,
        adaptive_initial=state['initial_states']==256,
        adaptive_retained=state['peak_post_observation_states']==64,
        adaptive_edges=adaptive['work_counts']['transition_edges']>=128,
        nominal_initial=fixed['initial_states']==256,
        nominal_retained=fixed['terminal_post_observation_states']==256,
        nominal_prediction=nominal['timing']['predictions']==1,
        nominal_edges=nominal['work_counts']['transition_edges']==256)
    return dict(supported=all(checks.values()),checks=checks,
        adaptive_state_regime=state,nominal_state_regime=fixed,
        adaptive_work=adaptive['work_counts'],nominal_work=nominal['work_counts'],
        scope='Measured maximum retained-source-count boundary under this fixed public stencil; not a proof over every possible posterior arrangement.')


def combine_components(components, *, complete_work_covered, workspace=None):
    """Sum sequential service ratios, preserving uncertainty and missing work."""
    for row in components.values():
        value = row.get('ratio_processing_seconds_per_baseband_second')
        if value is not None and (isinstance(value, bool) or
                not isinstance(value, (int,float)) or not math.isfinite(value) or value < 0):
            raise ValueError('component service ratio must be finite and nonnegative')
        bounds = row.get('ratio_processing_seconds_per_baseband_second_interval')
        if bounds is not None and (len(bounds) != 2 or
                any(not math.isfinite(x) or x < 0 for x in bounds) or bounds[0] > bounds[1]):
            raise ValueError('component service interval must be finite, nonnegative and ordered')
    usable = [row for row in components.values() if
        isinstance(row.get('ratio_processing_seconds_per_baseband_second'), (float,int))]
    points_complete = bool(components) and len(usable) == len(components)
    subtotal = sum(row['ratio_processing_seconds_per_baseband_second'] for row in usable)
    point = subtotal if points_complete else None
    intervals = [row.get('ratio_processing_seconds_per_baseband_second_interval') for row in usable]
    interval = ([sum(pair[i] for pair in intervals) for i in (0,1)]
                if points_complete and all(pair is not None for pair in intervals) else None)
    covered = points_complete and complete_work_covered
    models = points_complete and all(row.get('model_status') in SUPPORTED_MODEL_STATUSES
                                    for row in usable)
    # A measured lower bound alone can reject a configuration, even when other
    # work is still unresolved. An omitted component can never make a pass.
    lower_bound = sum(row.get('measured_complete_component_lower_bound', 0.) for row in components.values())
    if lower_bound >= 1:
        status, reason = 'fail', 'measured_complete_component_alone_exceeds_realtime'
    elif covered and models and point >= 1:
        status, reason = 'fail', 'complete_service_estimate_at_or_above_realtime'
    elif covered and models and interval is not None and interval[1] < 1:
        status, reason = 'pass', 'complete_conditional_service_interval_below_realtime'
    else:
        status, reason = 'unresolved', 'missing_work_or_unsupported_extrapolation_or_uncertainty'
    if workspace is not None and workspace.get('status') == 'failure':
        status, reason = 'fail', 'receiver_workspace_exceeds_budget'
    numeric_point_criterion = 'unresolved' if point is None else 'fail' if point >= 1 else 'below_one'
    return dict(ratio_processing_seconds_per_baseband_second=point,
        covered_stage_subtotal_processing_seconds_per_baseband_second=subtotal if usable else None,
        ratio_processing_seconds_per_baseband_second_interval=interval,
        margin_fraction=1-point if point is not None else None,
        numeric_point_criterion=numeric_point_criterion,
        margin_interval=[1-interval[1],1-interval[0]] if interval is not None else None,
        realtime_status=status, reason=reason, complete_work_covered=bool(covered),
        all_component_models_supported=models,
        measured_complete_component_lower_bound=lower_bound, components=components)


def _probe_work(sample):
    result = sample['result']
    native = result.get('native_template_work', {})
    timing = result.get('timing', {})
    endpoint = sample['endpoint']
    return dict(units=sample['units'], stratum=sample['stratum'],
        physical_first_sample=sample['public_first_sample'],
        ratio_processing_seconds_per_baseband_second=sample['ratio_processing_seconds_per_baseband_second'],
        observed_cpu_utilization_fraction=sample['observed_cpu_utilization_fraction'],
        **({'cpu_timing': sample['cpu_timing']} if 'cpu_timing' in sample else {}),
        complete_observation=result.get('complete_observation', False),
        consumed_samples=result.get('consumed_samples'), blocks=result.get('blocks'),
        segments=result.get('segments'),
        initial_states=result.get('initial_lattice',{}).get('states'),
        requested_invalid_rows=result.get('requested_invalid_rows',0),
        additional_discarded_rows=result.get('additional_discarded_rows',0),
        coordinates=result.get('coordinates'),
        probe_endpoint=dict(status=endpoint['status'], classification=endpoint['slot_classification'],
            bit=endpoint['decision_bit'], quiet_projection_modes=endpoint['quiet']['observed_projection_modes']),
        state_count=timing.get('state_count'), peak_states=timing.get('peak_states'),
        peak_transition_states=timing.get('peak_transition_states'),
        work_counts=result.get('work_counts', {}),
        lanes={name:dict(work_counts=lane['work_counts'], state_work_regime=lane['state_work_regime'],
            pruning_events=lane['timing']['pruning_events'], states_dropped=lane['timing']['states_dropped'])
            for name,lane in result['lanes'].items()},
        native_template_work=native,
        native_cache_reset_receipt=result.get('native_cache_reset_receipt'),
        repeated_work_units=result.get('repeated_work_units',[]),
        work_unit_samples=sample['work_unit_samples'],
        work_probe_only=result.get('work_probe_only',False),
        state_work_regime=result.get('state_work_regime',{}),
        public_work_ceiling=result.get('public_work_ceiling',{}),
        workspace_bytes=sample['receiver_peak_estimate_bytes'], rss=sample['rss'])


def measure_fine_stratum(prepared, receiver, coordinates, *, stratum, family_size, work_probe=False):
    """Small actual fine sessions; report all state/cache coverage limits."""
    from triad_case_metrics import LOOKS, BATCHES, summarize_service
    rows, work = [], []
    for look in LOOKS:
        while len(rows) < look:
            values = [None]*len(BATCHES)
            for index in ((len(rows)+offset)%len(BATCHES) for offset in range(len(BATCHES))):
                sample = prepared.fine_probe(receiver, coordinates,
                    units=BATCHES[index], stratum=stratum, work_probe=work_probe)
                values[index] = sample['ratio_processing_seconds_per_baseband_second']
                work.append(_probe_work(sample))
            rows.append(values)
        summary = summarize_service(rows, family_size=family_size)
        if summary['stop']:
            break
    completed = all(item['complete_observation'] for item in work)
    representative = all(item['native_template_work'].get('representative_work') for item in work)
    summary.update(stratum=stratum, work_probe_only=work_probe, work_samples=work,
        all_observations_complete=completed,
        nonzero_templates_in_every_probe=representative,
        source_geometry=prepared.public_geometry,
        endpoint_work_included=True,
        full_window_forecast_multiplier=(
            2*math.ceil(prepared.maximum_window_seconds/receiver.policy['segment_seconds'])*receiver.policy['segment_seconds']/prepared.slot_seconds
            if work_probe else prepared.maximum_window_seconds/prepared.slot_seconds),
        scope='Actual canonical fine initialization, native basis/cache, nominal and adaptive tracking, pruning and endpoint evaluation. Segment endpoint is deliberately incomplete as a whole-bit reception claim.')
    if not completed or (stratum == 'active' and not representative):
        summary['service_only_realtime_status'] = summary['realtime_status']
        summary['realtime_status'] = 'unresolved'
        summary['model_status'] = 'unresolved'
        summary['reason'] = 'incomplete_or_empty_template_fine_probe'
    return summary


def measure_sustained_fine(prepared, receiver, coordinates, *, family_size, maximum_look=None):
    """One native stream, one fixed prefix, then cumulative 1/2/4 s work.

    Each outer observation resets the native cache and creates ONE FineSession.
    Its two-second prefix is charged once per slot, followed without reset by
    another one, one and two seconds. Finish, endpoint evaluation and close are
    charged once. Diagnostic snapshots are outside the receiver-service timers.
    This normal-lane measurement is not a maximum-state qualification.
    """
    midpoint, origin, total_samples = continued_probe_extent(
        prepared.plan, prepared.bank.descriptor, coordinates)
    if origin is None:
        return dict(status='unresolved', reason='public_window_cannot_hold_six_second_probe')
    return measure_continued_fine(receiver, coordinates,
        raw=prepared.raw[origin:origin+total_samples],
        valid=prepared.valid[origin:origin+total_samples], origin=origin, midpoint=midpoint,
        address=prepared.calendar['plans'][0], slot_seconds=prepared.slot_seconds,
        family_size=family_size, maximum_look=maximum_look)


def continued_probe_extent(plan, descriptor, coordinates):
    """Locate the six-second public capacity observation without reading IQ."""
    fs, samples = int(descriptor['sample_rate_hz']), int(descriptor['total_samples'])
    total_samples = 6*fs
    if total_samples > samples:
        return None, None, total_samples
    midpoint = active_probe_origin(plan, descriptor, fs, samples, coordinates,
        required_samples=total_samples)
    origin = min(max(0, midpoint-3*fs), samples-total_samples)
    return midpoint, origin, total_samples


def continuation_look(summary, *, maximum_look=None):
    """Use component precision without deciding real time on the wrong duration."""
    from triad_case_metrics import LOOKS, PRECISION
    if (summary['model_status'] == 'abbreviated_holdout_supported' and
            summary['relative_precision_fraction'] <= PRECISION):
        return 'relative_precision'
    if summary['outer_trials'] == (LOOKS[-1] if maximum_look is None else maximum_look):
        return 'bounded_limit'
    return None


def measure_continued_fine(receiver, coordinates, *, raw, valid, origin, midpoint, address,
                           slot_seconds, family_size, maximum_look=None):
    """Canonical abbreviated fine service from a bounded actual native IQ slice."""
    from triad_case_metrics import LOOKS, summarize_service

    if maximum_look is not None and (type(maximum_look) is not int or maximum_look not in LOOKS):
        raise ValueError('maximum fine look must be a declared sampling look')
    stop_at = LOOKS[-1] if maximum_look is None else maximum_look

    prefix_seconds, checkpoints = 2., (1, 2, 4)
    fs = receiver.sample_rate
    prefix_samples, total_samples = round(prefix_seconds*fs), round(6*fs)
    window_seconds = receiver.samples/fs
    if (origin < receiver.capture_origin or
            origin+total_samples > receiver.capture_origin+receiver.samples or
            np.shape(raw) != (total_samples,) or np.shape(valid) != (total_samples,)):
        return dict(status='unresolved', reason='public_window_cannot_hold_six_second_probe')
    if not bool(np.asarray(valid).all()):
        return dict(status='unresolved', reason='continued_probe_contains_invalid_input')

    def cpu_start():
        return time.process_time(), _native_cpu_seconds(receiver.basis_source)

    def utilization(before, elapsed):
        timing = _cpu_timing(elapsed, before[0], receiver.basis_source, before[1],
                             receiver.workers)
        return dict(observed_allocated_cpu_utilization_fraction=(
            timing['utilization_fraction']), native_child_included=timing['native_cpu_seconds'] is not None,
            cpu_timing=timing)

    def snapshot(fine):
        progress = fine.receiver.progress()
        scores = {name:row['bit_scores'] for name,row in progress['lanes'].items()}
        if not all(np.asarray(values).shape == (2,) and np.isfinite(values).all()
                for values in scores.values()):
            raise ValueError('fine checkpoint requires finite lane scores')
        return dict(consumed_samples=progress['consumed_samples'],
            processed_rows=progress['processed_rows'], blocks=progress['blocks'], segments=progress['segments'],
            requested_invalid_rows=progress['requested_invalid_rows'],
            additional_discarded_rows=progress['additional_discarded_rows'],
            work_counts=copy.deepcopy(progress['work_counts']),
            retained_states={name:[[state.group,state.delay_index,state.rate_index]
                for state in lane.timing.states]
                for name,lane in (('adaptive',fine.receiver.adaptive),('initial_affine',fine.receiver.nominal))},
            lanes={name:dict(state_work_regime=copy.deepcopy(row['state_work_regime']),
                bit_scores=list(scores[name]),
                timing={key:row['timing'][key] for key in (
                    'state_count','observations','predictions','peak_states','peak_transition_states',
                    'pruning_events','states_dropped')},
                work_counts=copy.deepcopy(row['work_counts']))
                for name,row in progress['lanes'].items()},
            native_templates=dict(calls=fine.template_calls,
                returned_elements=fine.template_rows, energy=fine.template_energy,
                cache=copy.deepcopy(fine.provider.cache_report())))

    def delta(before, after):
        native_before, native_after = before['native_templates'], after['native_templates']
        cache_before, cache_after = native_before['cache'], native_after['cache']
        keys = ('cache_hits','cache_misses','native_preparation_calls',
                'native_preparation_samples','evictions','interpolated_rows','direct_rows')
        return dict(samples=after['consumed_samples']-before['consumed_samples'],
            template_calls=native_after['calls']-native_before['calls'],
            template_elements=native_after['returned_elements']-native_before['returned_elements'],
            template_energy=native_after['energy']-native_before['energy'],
            cache={key:cache_after[key]-cache_before[key]
                   for key in keys if key in cache_after and key in cache_before},
            work_counts={key:after['work_counts'][key]-before['work_counts'][key]
                         for key in after['work_counts']})

    def geometry(snapshots):
        baseline = snapshots[0]
        checks, drift = {}, {}
        cache_keys = ('method','tile_samples','dense_factor','cache_limit_bytes','fallback_reason')
        cache0 = baseline['native_templates']['cache']
        checks['cached_native_backend'] = all(key in cache0 for key in (
            'cache_misses','native_preparation_calls','tile_samples','dense_factor')) and not cache0.get('fallback_reason')
        checks['cache_geometry_unchanged'] = all(
            all(item['native_templates']['cache'].get(key) == cache0.get(key) for key in cache_keys)
            for item in snapshots)
        for name in ('adaptive','initial_affine'):
            regimes = [item['lanes'][name]['state_work_regime'] for item in snapshots]
            first = regimes[0]
            fixed = ('state_cap','proposal_cap','coherence_models','requested_candidate_shards',
                     'maximum_hypothesis_tasks_per_block')
            checks[name+'_fixed_work_geometry'] = all(
                all(item[key] == first[key] for key in fixed) for item in regimes)
            pre = [item['terminal_pre_emission_states'] for item in regimes]
            post = [item['terminal_post_observation_states'] for item in regimes]
            if name == 'initial_affine':
                checks[name+'_state_count_stable'] = len(set(pre+post)) == 1
            else:
                checks[name+'_pre_state_range'] = min(pre) > 0 and max(pre)/min(pre) <= 1.3
                checks[name+'_post_state_range'] = (
                    all(value == first['state_cap'] for value in post)
                    if post[0] == first['state_cap'] else
                    min(post) > 0 and max(post)/min(post) <= 1.3)
            drift[name] = dict(pre_emission_counts=pre, post_observation_counts=post,
                relative_pre_state_range=max(pre)/min(pre) if min(pre)>0 else None,
                allowed_relative_range=1.3)
        return dict(supported=all(checks.values()), checks=checks, state_drift=drift)

    def push_until(fine, first, stop):
        for at in range(first, stop, receiver.policy['chunk_samples']):
            end = min(stop, at+receiver.policy['chunk_samples'])
            fine.push(at, raw[at:end], valid[at:end])

    rows, observations, prefix_ratios, finish_ratios = [], [], [], []
    stop_reason, summary, failure = None, None, None
    for look in LOOKS[:LOOKS.index(stop_at)+1]:
        while len(rows) < look:
            observation = dict(index=len(observations)+1, checkpoints=[])
            fine = None
            try:
                before = cpu_start()
                began = time.perf_counter()
                reset = receiver.basis_source.set_basis_address(address['pattern_epoch'],address['symbol_ordinal'])
                if (reset.get('template_cache_cleared') is not True or
                        reset.get('basis_epoch') != address['pattern_epoch'] or
                        reset.get('basis_symbol_ordinal') != address['symbol_ordinal']):
                    raise ValueError('native public-address cache reset was not acknowledged')
                fine = receiver.create_fine_session(coordinates, samples=total_samples,
                    physical_origin=origin)
                push_until(fine, 0, prefix_samples)
                elapsed = time.perf_counter()-began
                prefix_utilization = utilization(before,elapsed)
                prefix = snapshot(fine)
                observation['prefix'] = dict(
                    ratio_processing_seconds_per_baseband_second=elapsed/prefix_seconds,
                    represented_baseband_seconds=prefix_seconds,
                    native_cache_reset_receipt=reset, counters=prefix, **prefix_utilization)
                if prefix['processed_rows'] != prefix_samples:
                    raise ValueError('fixed prefix did not process its complete declared samples')
                if prefix['native_templates']['energy'] <= 0:
                    raise ValueError('fixed prefix has no representative DATA template work')
                snapshots = [prefix]
                guard = geometry(snapshots)
                if not guard['supported']:
                    observation['geometry'] = guard
                    raise ValueError('prefix cache or state geometry is unsupported')
                continuation_elapsed, previous_samples = 0., prefix_samples
                continuation_ratios = []
                for seconds in checkpoints:
                    stop = prefix_samples+round(seconds*fs)
                    before = cpu_start()
                    began = time.perf_counter()
                    push_until(fine, previous_samples, stop)
                    interval_elapsed = time.perf_counter()-began
                    interval_utilization = utilization(before,interval_elapsed)
                    continuation_elapsed += interval_elapsed
                    current = snapshot(fine)
                    changes = delta(snapshots[-1], current)
                    snapshots.append(current)
                    guard = geometry(snapshots)
                    checkpoint = dict(cumulative_continuation_baseband_seconds=seconds,
                        ratio_processing_seconds_per_baseband_second=continuation_elapsed/seconds,
                        interval_ratio_processing_seconds_per_baseband_second=(
                            interval_elapsed/((stop-previous_samples)/fs)),
                        counters=current, deltas=changes, **interval_utilization)
                    observation['checkpoints'].append(checkpoint)
                    if current['processed_rows'] != stop or changes['template_energy'] <= 0:
                        raise ValueError('continuation is incomplete or has zero DATA template work')
                    if not guard['supported']:
                        observation['geometry'] = guard
                        raise ValueError('material state or cache geometry drift during continuation')
                    continuation_ratios.append(continuation_elapsed/seconds)
                    previous_samples = stop
                fills = delta(prefix, snapshots[-1])
                cache = snapshots[-1]['native_templates']['cache']
                minimum_scale = 1-(receiver.grid.clock_bound+receiver.initial_rate_half_width_ppm)*1e-6
                tile_exposure = 4*fs*cache['dense_factor']*minimum_scale/cache['tile_samples']
                crossings = dict(continued_cache_misses=fills['cache'].get('cache_misses',0),
                    continued_native_tile_fills=fills['cache'].get('native_preparation_calls',0),
                    continued_native_samples=fills['cache'].get('native_preparation_samples',0),
                    canonical_tile_samples=cache['tile_samples'], dense_factor=cache['dense_factor'],
                    minimum_public_fixed_clock_scale=minimum_scale,
                    continuation_minimum_tile_widths=tile_exposure,
                    scope='Actual canonical cache-miss/native-fill counters plus a public fixed-affine travel envelope; no future templates are prefetched.')
                crossings['supported'] = (crossings['continued_cache_misses'] > 0 and
                    crossings['continued_native_tile_fills'] > 0 and
                    crossings['continued_native_samples'] > 0 and minimum_scale > 0 and tile_exposure > 1)
                observation.update(geometry=guard, continued_cache_boundary_work=crossings)
                if not crossings['supported']:
                    raise ValueError('continued observation did not demonstrate canonical cache-boundary fills')
                before = cpu_start()
                began = time.perf_counter()
                try:
                    tracked = fine.finish()
                    endpoint = receiver.classify(tracked, coordinates, all_input_valid=True)
                finally:
                    fine.close()
                elapsed = time.perf_counter()-began
                fine = None
                observation['finish_classify_close'] = dict(
                    ratio_processing_seconds_per_baseband_second=elapsed/slot_seconds,
                    represented_baseband_seconds=slot_seconds,
                    complete_six_second_probe=bool(tracked['complete_observation']),
                    endpoint=endpoint['slot_classification'],
                    whole_bit_admission_expected=False, **utilization(before,elapsed))
                if not tracked['complete_observation'] or tracked['processed_rows'] != total_samples:
                    raise ValueError('six-second FineSession did not finish complete')
                observation['workspace'] = dict(estimated_peak_bytes=(
                    receiver.input_workspace_bytes+receiver.native_workspace_bytes+
                    receiver.coarse_reserved_bytes+receiver.policy['cache_bytes']+
                    tracked['working_bytes_estimate']), rss=_rss_snapshot(receiver.basis_source))
                rows.append(continuation_ratios)
                prefix_ratios.append(observation['prefix']['ratio_processing_seconds_per_baseband_second'])
                finish_ratios.append(observation['finish_classify_close']['ratio_processing_seconds_per_baseband_second'])
                observation['status'] = 'complete_work_observation'
            except (ValueError, RuntimeError, MemoryError, EOFError, OSError) as error:
                observation.update(status='unresolved', error=type(error).__name__+': '+str(error))
                failure = observation['error']
            finally:
                if fine is not None:
                    fine.close()
            observations.append(observation)
            if failure:
                break
        if failure:
            stop_reason = 'first_incomplete_or_material_geometry_or_cache_failure'
            break
        summary = summarize_service(rows, family_size=family_size)
        summary.pop('margin_fraction', None)
        summary.pop('margin_interval', None)
        summary.update(realtime_status='not_applicable_to_continuation_rate',
            reason='Use the slot-normalized complete receiver service.')
        if summary['observed_model_log_error_floor'] > math.log1p(.3):
            failure = 'observed four-second continuation holdout differs by more than30%'
            stop_reason = 'first_material_cost_holdout_drift'
            summary.update(stop=True, stop_reason=stop_reason)
            break
        stop_reason = continuation_look(summary, maximum_look=stop_at)
        summary.update(stop=stop_reason is not None,
            stop_reason=stop_reason or 'next_abbreviated_look')
        if stop_reason:
            break
    output = dict(schema='triad-sustained-fine-measurement-v1',
        status='unresolved' if failure else 'measured_continuation',
        reason=failure, stop_reason=stop_reason, continuation=summary,
        observations=observations, successful_outer_observations=len(rows), maximum_outer_observations=stop_at,
        public_origin_sample=origin, requested_center_sample=midpoint,
        public_probe_samples=total_samples, prefix_baseband_seconds=prefix_seconds,
        continuation_checkpoints_seconds=list(checkpoints), workers=receiver.workers,
        procedure_id=receiver.procedure_id,
        scope='Normal persistent FineSession only. One cold2s prefix and one finish per slot; continued1/2/4s service is observed, not inferred by subtracting a fitted startup intercept. Not full-bit success or worst-state capacity.')
    if summary is not None:
        output['fine_component'] = _continued_fine_component(summary, prefix_ratios, finish_ratios,
            prefix_seconds=prefix_seconds, window_seconds=window_seconds, slot_seconds=slot_seconds,
            family_size=family_size, supported=not failure)
    return output


def _continued_fine_component(summary, prefix_ratios, finish_ratios, *, prefix_seconds,
                              window_seconds, slot_seconds, family_size, supported=True):
    from triad_case_metrics import LOOKS, arithmetic_interval

    alpha = .05/(family_size*len(LOOKS)*3)
    used = summary['outer_trials']
    prefix_point,prefix_interval = arithmetic_interval(prefix_ratios[:used],alpha=alpha)
    finish_point,finish_interval = arithmetic_interval(finish_ratios[:used],alpha=alpha)
    prefix_scale = prefix_seconds/slot_seconds
    continued_scale = (window_seconds-prefix_seconds)/slot_seconds
    continued_point = summary['ratio_processing_seconds_per_baseband_second']
    continued_interval = summary['ratio_processing_seconds_per_baseband_second_interval']
    return dict(
        ratio_processing_seconds_per_baseband_second=prefix_point*prefix_scale+continued_point*continued_scale+finish_point,
        ratio_processing_seconds_per_baseband_second_interval=[
            prefix_interval[index]*prefix_scale+continued_interval[index]*continued_scale+
            finish_interval[index] for index in (0,1)],
        model_status=summary['model_status'] if supported else 'unresolved',
        prefix=dict(ratio_processing_seconds_per_baseband_second=prefix_point,
            ratio_processing_seconds_per_baseband_second_interval=prefix_interval,
            represented_baseband_seconds=prefix_seconds),
        finish_classify_close=dict(ratio_processing_seconds_per_baseband_second=finish_point,
            ratio_processing_seconds_per_baseband_second_interval=finish_interval,
            represented_baseband_seconds=slot_seconds),
        prefix_per_slot_multiplier=prefix_scale,
        continuation_per_slot_multiplier=continued_scale,
        scope='Conditional full-window nominal forecast: measured prefix once + observed continuation rate for remaining input + finish/classify/close once. The cold-state capacity envelope is separate.')


def sustained_allocation_targets(cpu):
    """Predeclare physical and full logical worker counts from the visible budget."""
    logical = cpu['available_cpus']
    topology = cpu.get('topology') or {}
    physical = topology.get('physical_core_count') if topology.get('status') == 'complete' else None
    return dict(physical_workers=min(physical,logical) if type(physical) is int and physical > 0 else None,
        logical_workers=logical)


def manual_worker_plan(cpu, *, threads=None, full_logical_inference=None, calibration_stage=None):
    """Choose a receiver allocation without changing the sampling policy."""
    from triad_cpu import resolve_receiver_workers

    if (full_logical_inference is not None and type(full_logical_inference) is not bool or
            full_logical_inference is True and threads is not None):
        raise ValueError('full-logical inference requires a worker comparison')
    targets = sustained_allocation_targets(cpu)
    hardware = targets['logical_workers']
    automatic = resolve_receiver_workers(hardware_threads=hardware)
    if calibration_stage is not None and (threads is not None or full_logical_inference is False):
        raise ValueError('stage calibration requires the automatic or explicit full-logical allocation')
    if threads is not None:
        inference = resolve_receiver_workers(threads, hardware_threads=hardware)
        allocations, mode = [inference], 'explicit'
    elif full_logical_inference is False:
        allocations, inference, mode = list(cpu['receiver_worker_candidates']), None, 'pilot_selected'
    else:
        inference = hardware if full_logical_inference is True else automatic
        mode = 'full_logical' if full_logical_inference is True else 'automatic_receiver'
        allocations = ([inference] if calibration_stage is not None else
            sorted({1, targets['physical_workers'] or hardware, hardware, inference}))
    return dict(allocations=allocations, inference_workers=inference, inference_mode=mode,
        hardware_threads=hardware, automatic_receiver_workers=automatic)


def _allocation_fine_sample(measurement, prepared, family_size):
    """Compare the first three uninterrupted nominal samples under a shared work key."""
    from triad_case_metrics import arithmetic_interval, summarize_service

    observations = measurement.get('observations', [])[:3]
    if (measurement.get('status') != 'measured_continuation' or len(observations) != 3 or
            any(row['status'] != 'complete_work_observation' or not row['geometry']['supported'] or
                not row['continued_cache_boundary_work']['supported'] for row in observations)):
        raise ValueError('three matched complete continued fine observations required')
    ratio_key = 'ratio_processing_seconds_per_baseband_second'
    continued, prefix, finish, work, scores, utilization = [], [], [], [], [], []
    for observation in observations:
        prefix.append(observation['prefix'][ratio_key])
        finish.append(observation['finish_classify_close'][ratio_key])
        continued.append([row[ratio_key] for row in observation['checkpoints']])
        if (len(continued[-1]) != 3 or not observation['finish_classify_close']['complete_six_second_probe'] or
                observation['finish_classify_close']['endpoint'] != 'AMBIGUOUS'):
            raise ValueError('continued fine checkpoint coverage differs')
        for stage in (observation['prefix'], *observation['checkpoints']):
            counters = copy.deepcopy(stage['counters'])
            # Dispatch counts change with the worker geometry. Preserve all
            # state, DSP and native-cache work after removing just those counts.
            counters['work_counts'].pop('hypothesis_update_tasks')
            counters['native_templates'].pop('energy')
            cache = counters['native_templates']['cache']
            for key in ('mapped_worker_tasks', 'maximum_mapped_workers'):
                cache.pop(key, None)
            for name in ('adaptive','initial_affine'):
                lane = counters['lanes'][name]
                scores.append(lane.pop('bit_scores'))
                lane['work_counts'].pop('hypothesis_update_tasks')
                regime = lane['state_work_regime']
                for key in ('requested_candidate_shards','maximum_hypothesis_tasks_per_block'):
                    regime.pop(key, None)
            work.append(counters)
        for stage in (observation['prefix'],*observation['checkpoints'],observation['finish_classify_close']):
            value = stage.get('observed_allocated_cpu_utilization_fraction')
            if value is not None:
                utilization.append(value)
    summary = summarize_service(continued, family_size=family_size)
    component = _continued_fine_component(summary, prefix, finish, prefix_seconds=2.,
        window_seconds=prepared.window_seconds, slot_seconds=prepared.slot_seconds, family_size=family_size)
    observed = [(p*2+c[-1]*4+f*prepared.slot_seconds)/6 for p,c,f in zip(prefix,continued,finish)]
    point, bounds = arithmetic_interval(observed,alpha=.05/family_size)
    evidence = dict(observations=3, measured_six_second_fine_service={ratio_key:point,
        ratio_key+'_interval':bounds, 'samples':observed},
        conditional_slot_fine_component=component if component['model_status']=='abbreviated_holdout_supported' else None,
        slot_model_status=component['model_status'],
        observed_allocated_cpu_utilization_fraction_range=[min(utilization),max(utilization)] if utilization else None,
        cpu_timing_summary=_aggregate_cpu(stage for observation in observations
            for stage in (observation['prefix'], *observation['checkpoints'],
                          observation['finish_classify_close'])),
        workspace_peak_estimate_bytes=max(row['workspace']['estimated_peak_bytes'] for row in observations),
        workspace_observed_rss_bytes=max((row['workspace']['rss']['sum_process_bytes'] for row in observations
            if row['workspace']['rss']['sum_process_bytes'] is not None),default=None))
    identity = dict(origin=measurement['public_origin_sample'], samples=measurement['public_probe_samples'],
        prefix=measurement['prefix_baseband_seconds'], checkpoints=measurement['continuation_checkpoints_seconds'], work=work)
    return evidence, identity, np.asarray(scores)


def measure_sustained_allocation_probe(prepared, allocations, selected, coordinates, active, *, targets, family_size):
    """Exploratory actual resident-bank comparison, never an allocation or capacity gate."""
    if (type(targets) is not dict or set(targets) != {'physical_workers','logical_workers'} or
            type(selected) is not int or type(targets['logical_workers']) is not int or
            not 0 < selected <= targets['logical_workers'] or
            (targets['physical_workers'] is not None and (type(targets['physical_workers']) is not int or
                not 0 < targets['physical_workers'] <= targets['logical_workers']))):
        raise ValueError('expected predeclared bounded physical and logical worker targets')
    requested = dict(selected_workers=selected, **targets)
    workers = list(dict.fromkeys(value for value in requested.values() if value is not None))
    rows = []
    report = dict(schema='triad-sustained-allocation-probe-v1', status='unresolved', allocations=requested,
        comparisons=rows, extra_observations_maximum=3*(len(workers)-1),
        extra_repeated_baseband_probe_seconds_maximum=18*(len(workers)-1), extra_observations_attempted=0,
        extra_acquisition_attempts_maximum=len(workers)-1, extra_acquisition_attempts=0,
        family_size=family_size,
        scope='Exploratory repeated fine work on one actual resident-bank observation. No complete service, best allocation or CPU ceiling decision.')
    if not coordinates:
        report['reason'] = 'complete_public_acquisition_required'
        return report
    try:
        selected_cost, identity, scores = _allocation_fine_sample(active, prepared, family_size)
    except (KeyError,TypeError,ValueError,OverflowError):
        report['reason'] = 'selected_continued_work_unresolved'
        return report
    rows.append(dict(workers=selected, status='selected_first_three_observations', **selected_cost))
    for count in workers[1:]:
        row = dict(workers=count, status='unresolved')
        rows.append(row)
        if count not in allocations:
            row['reason'] = 'requested_allocation_not_in_original_pilot'
            continue
        try:
            with prepared.receiver(count) as receiver:
                report['extra_acquisition_attempts'] += 1
                acquired = prepared.acquisition(receiver)['result']
                session = acquired.pop('coarse_session',None)
                if session is not None:
                    session.close()
                if acquired['status'] != 'acquired' or tuple(acquired['coordinates']) != coordinates:
                    row['reason'] = 'actual_complete_acquisition_or_proposals_differ'
                    continue
                sample = measure_sustained_fine(prepared,receiver,coordinates,family_size=family_size,maximum_look=3)
            report['extra_observations_attempted'] += len(sample.get('observations',()))
            cost, actual_identity, actual_scores = _allocation_fine_sample(sample,prepared,family_size)
            if actual_identity != identity or not np.allclose(actual_scores,scores,atol=1e-7,rtol=1e-10):
                row['reason'] = 'continued_state_cache_or_dsp_work_differs'
                continue
            row.update(status='matched_exploratory_measurement', **cost)
        except (KeyError,TypeError,ValueError,RuntimeError,MemoryError,EOFError,OSError,OverflowError):
            row['reason'] = 'allocation_continued_work_unresolved'
    if all(value is not None for value in requested.values()) and all(row['status']!='unresolved' for row in rows):
        report['status']='matched_exploratory_measurements'
    elif targets['physical_workers'] is None:
        report['reason']='physical_core_topology_unavailable'
    indexed = {row['workers']: row['measured_six_second_fine_service'] for row in rows
               if 'measured_six_second_fine_service' in row}
    physical, logical = targets['physical_workers'], targets['logical_workers']
    if physical != logical and physical in indexed and logical in indexed:
        key = 'ratio_processing_seconds_per_baseband_second'
        base, candidate = indexed[physical], indexed[logical]
        low_base, high_base = base[key+'_interval']
        low_candidate, high_candidate = candidate[key+'_interval']
        report['logical_over_physical_speedup'] = dict(
            point=base[key]/candidate[key],
            conditional_interval=[low_base/high_candidate, high_base/low_candidate]
                if low_candidate > 0 else None)
    return report


def choose_allocation(prepared, allocations):
    """Explore one workload on this CPU; fresh inference samples follow."""
    rows = []
    for workers in allocations:
        with prepared.receiver(workers) as receiver:
            acquisition = prepared.acquisition(receiver)
            row = dict(workers=workers, acquisition=acquisition)
            session = acquisition['result'].pop('coarse_session',None)
            if session is not None:
                try:
                    began = time.perf_counter()
                    work = session.step(1)
                    row['selection_ratio'] = acquisition['ratio_processing_seconds_per_baseband_second']+(
                        time.perf_counter()-began)*session.work_plan['units']/prepared.slot_seconds
                    row['selection_scope'] = 'Exact broad coarse work only; partial search supplies no fine proposals.'
                    row['tiled_work_plan'] = copy.deepcopy(session.work_plan)
                finally:
                    session.close()
            if acquisition['result']['status'] == 'acquired' or row.get('selection_ratio') is not None:
                fine = prepared.fine_probe(receiver, public_work_coordinates(receiver,
                    acquisition['result'].get('coordinates',())), units=1,
                    work_probe=True)
                row['fine'] = _probe_work(fine)
                if fine['result']['native_template_work']['representative_work']:
                    row['selection_ratio'] = (
                        row.get('selection_ratio',acquisition['ratio_processing_seconds_per_baseband_second'])+
                        fine['ratio_processing_seconds_per_baseband_second']*
                        2*prepared.window_seconds/prepared.slot_seconds)
                    row['selection_scope'] = 'Complete acquisition cost or exact tiled forecast, plus actual two-segment fine transition ceiling work. Work-only coordinates never admit reception.'
            rows.append(row)
    usable = [row for row in rows if row.get('selection_ratio') is not None]
    return dict(selected_workers=min(usable,key=lambda row:row['selection_ratio'])['workers'] if usable else None,
        samples=rows, inference_samples_reused=False,
        scope='Matched one-stream exploratory allocation choice; one sample per allocation is not a speedup confidence claim. Full visible logical allocation is measured, never assumed fastest.')


def _measure_recurring_window(prepared, bank, plan, *, workers, family_size):
    """Measure reacquisition and stream copies in the largest recurring slot."""
    from triad_orchestration_cost import _full_window_assembly
    samples = plan['receiver_window_samples']
    screen = bank['screen']
    native = max(prepared.basis_source.descriptor['fixed_workspace_bytes'],
                 prepared.basis_source.descriptor['workspace_budget_bytes'])
    if samples != screen.samples or 17*samples+screen.resident_estimate+native >= prepared.receiver_memory_limit_bytes:
        raise MemoryError('largest recurring public window exceeds its receiver workspace')
    noise_only_observation(prepared, seed=96342, samples=samples)
    manifest = copy.deepcopy(bank['manifest'])
    manifest['power']['complex_noise_variance'] = prepared.noise_variance
    with CompleteBitReceiver(manifest, screen, prepared.basis_source, workers=workers,
                             memory_limit_bytes=prepared.receiver_memory_limit_bytes) as receiver:
        reacquisition = measure_acquisition(prepared, receiver, family_size=family_size)
    orchestration = _full_window_assembly(prepared, recurring_slot=plan['index'],
                                          resident_bank_bytes=screen.resident_estimate)
    return dict(slot_index=plan['index'], noise_only_acquisition=reacquisition,
        full_window_assembly=orchestration, sample_count=samples,
        ingress_samples=orchestration['physical_ingested_samples'],
        scope='Largest native recurring window with synthetic H0 input. Actual Helper, complete or tiled public search, and separately timed production stream copies; no reception trial.')


def build_recurring_public_bank(prepared, plan, *, workers, screen_factory=FFTScreen):
    """Canonical public bank builder for a declared recurring slot."""
    from triad_case_replay import build_public_screen

    manifest = copy.deepcopy(prepared.manifest)
    manifest['power']['complex_noise_variance'] = 1.
    manifest['public_epoch_seconds'] = plan['pattern_epoch']
    manifest['format']['symbol_ordinal'] = plan['symbol_ordinal']
    manifest['calendar'].update(capture_samples=plan['receiver_window_samples'],
        physical_origin_sample=plan['receiver_window_first_sample'],
        capture_lead_seconds=plan['window_capture_lead_seconds'],
        data_sample_offset=plan['source_begin_samples'],
        helper_start_seconds=plan['helper_begin_samples']/prepared.fs,
        helper_end_seconds=plan['helper_end_samples']/prepared.fs)
    for key in ('receiver_data_first_sample','receiver_data_stop_sample',
                'receiver_previous_signal_stop_sample','receiver_next_signal_first_sample',
                'quiet_aperture_first_sample','quiet_aperture_stop_sample'):
        manifest['calendar'][key]=plan[key]
    input_bytes = 17*max(len(prepared.raw), plan['receiver_window_samples'])
    bank = build_public_screen(prepared.basis_source, manifest,
        public_address={key:plan[key] for key in ('pattern_epoch','symbol_ordinal')},
        source_identity=prepared.bank.screen_identity,
        memory_limit_bytes=prepared.receiver_memory_limit_bytes-input_bytes,
        threads=workers, screen_factory=screen_factory)
    bank['input_reservation_bytes'] = input_bytes
    return bank


def measure_recurring_banks(prepared, *, workers, family_size):
    """Two fresh public ordinals check recurring preparation's fixed geometry.

    This is a geometry-conditional cost allowance, not an IID timing confidence
    interval. It includes the complete canonical builder and profile gate.
    Must run after every probe using the original prepared bank.
    """
    original = prepared.bank.public_bank_work
    original_profile = prepared.bank.public_bank_profile
    # Real stream reception retires an old bank before preparing its successor.
    # Release both references instead of silently charging two resident banks.
    prepared.screen = None
    prepared.bank.screen = None
    rows = []
    recurring = {}
    last = prepared.recurring_window['index']
    first = 2 if last == 1 else 1
    for index in (first,last):
        plan = prepared.calendar['plans'][index]
        child_before = _native_cpu_seconds(prepared.basis_source)
        cpu_began = time.process_time()
        bank = build_recurring_public_bank(prepared, plan, workers=workers)
        elapsed = bank['receiver_preparation_seconds']
        timing = _cpu_timing(elapsed, cpu_began, prepared.basis_source, child_before, workers)
        profile, work = bank['profile'],bank['work']
        same = all(profile[key] == original_profile[key] for key in
            ('template_sampling_step','projected_sample_rate_hz','public_epochs','public_clocks_ppm'))
        same = same and all(work[key] == original[key] for key in
            ('raw_source_samples','public_template_lanes','native_basis_sample_work_estimate'))
        row=dict(public_slot_index=index, public_address=bank['public_address'],
            identical_native_work_geometry=bool(same), projected_rows=work['complete_projected_rows'],
            native_work_geometry={key:work[key] for key in ('raw_source_samples',
                'public_template_lanes', 'native_basis_sample_work_estimate')},
            input_reservation_bytes=bank['input_reservation_bytes'],
            preparation_peak_estimate_bytes=work['preparation_peak_estimate_bytes'],
            fft_bank_geometry=work.get('fft_bank_geometry'),
            template_identity=bank['template_identity'], rss=_rss_snapshot(prepared.basis_source))
        if index == last:
            try:
                recurring = _measure_recurring_window(prepared, bank, plan,
                    workers=workers, family_size=family_size)
            except (ValueError, RuntimeError, MemoryError, EOFError, OSError) as error:
                recurring = dict(status='unresolved', slot_index=index, reason=str(error))
        retired, retired_cpu = time.perf_counter(), time.process_time()
        del bank
        elapsed += time.perf_counter()-retired
        timing['observed_elapsed_seconds'] = elapsed
        timing['process_cpu_seconds'] += time.process_time()-retired_cpu
        timing['busy_cores'] = (timing['process_cpu_seconds']+(timing['native_cpu_seconds'] or 0.))/elapsed
        timing['utilization_fraction'] = timing['busy_cores']/workers
        row['cpu_timing'] = timing
        row['ratio_processing_seconds_per_baseband_second']=elapsed/prepared.slot_seconds
        rows.append(row)
    values = [row['ratio_processing_seconds_per_baseband_second'] for row in rows]
    spread = max(values)/min(values) if min(values) > 0 else float('inf')
    supported = all(row['identical_native_work_geometry'] for row in rows) and spread <= 1.3
    return dict(ratio_processing_seconds_per_baseband_second=max(values),
        ratio_processing_seconds_per_baseband_second_interval=[min(values),1.3*max(values)],
        model_status='declared_geometry_holdout_supported' if supported else 'unresolved',
        holdout_cost_ratio=spread, allowed_geometry_cost_ratio=1.3,
        interval_scope='Observed ordinal range with30% fixed geometry allowance, not a statistical confidence interval; source work and profile must match and both distinct ordinal measurements must agree within30%.',
        samples=rows, recurring=True, recurring_window=recurring,
        scope='Complete actual native/public FFT bank preparation at two new public ordinals, including current-input fidelity gates. Included for every subsequent bit/listening slot, even if look-ahead preparation is possible.')


def measure_tiled_acquisition(prepared, receiver, *, family_size):
    """Measure bounded exact frequency kernels, never drain a broad search."""
    from triad_case_metrics import LOOKS, BATCHES, summarize_service, arithmetic_interval
    rows, setup, samples, setup_work = [], [], [], []
    plan = None
    work_class = None
    for look in LOOKS:
        while len(rows) < look:
            row = [None]*len(BATCHES)
            for index in ((len(rows)+offset)%len(BATCHES) for offset in range(len(BATCHES))):
                acquired = prepared.acquisition(receiver)
                session = acquired['result'].pop('coarse_session',None)
                if session is None:
                    raise ValueError('broad-grid work fixture requires a resumable exact search')
                try:
                    current = session.work_plan
                    if plan is not None and current != plan:
                        raise ValueError('public tiled geometry changed inside a timing look')
                    plan = copy.deepcopy(current)
                    current_class = acquired['acquisition_work_class']
                    if work_class is not None and current_class != work_class:
                        raise ValueError('public tiled acquisition demand changed inside a timing look')
                    work_class = current_class
                    setup.append(acquired['ratio_processing_seconds_per_baseband_second'])
                    setup_work.append({key:value for key,value in acquired.items() if key!='result'})
                    units = BATCHES[index]
                    began, cpu = time.perf_counter(),time.process_time()
                    before = _native_cpu_seconds(prepared.basis_source)
                    work = session.step(units)
                    timing = _receipt(began,cpu,prepared.basis_source,before,
                        prepared.slot_seconds,receiver.workers)
                    row[index] = timing['ratio_processing_seconds_per_baseband_second']*plan['units']/units
                    samples.append(dict(units=units,completed_frequency_points=work['completed_frequency_points'],
                        measured_partial_ratio_processing_seconds_per_baseband_second=timing['ratio_processing_seconds_per_baseband_second'],
                        forecast_complete_ratio_processing_seconds_per_baseband_second=row[index],
                        observed_cpu_utilization_fraction=timing['observed_cpu_utilization_fraction'],
                        cpu_timing=timing['cpu_timing'],
                        raw_candidate_rows=work['raw_candidate_rows'],
                        scope='Actual exact frequency-tile kernel; partial proposals are never used for reception.'))
                finally:
                    session.close()
            rows.append(row)
        summary=summarize_service(rows,family_size=family_size)
        if summary['stop']:
            break
    alpha=.05/(family_size*len(LOOKS)*3)
    setup_point,setup_interval=arithmetic_interval(setup,alpha=alpha)
    selection=measure_full_selection_metadata(prepared,receiver,plan,family_size=family_size)
    components=dict(
        helper_projection_setup=dict(ratio_processing_seconds_per_baseband_second=setup_point,
            ratio_processing_seconds_per_baseband_second_interval=setup_interval,
            model_status='fixed_extent_measured_no_extrapolation'),
        exact_frequency_tiles=summary,global_candidate_selection=selection)
    result=combine_components(components,complete_work_covered=True)
    result.update(model_status=('abbreviated_holdout_supported' if summary['model_status']=='abbreviated_holdout_supported' else 'unresolved'),
        work_plan=plan,acquisition_work_class=work_class,partial_kernel_samples=samples,samples=setup_work,
        no_complete_search_executed=True,
        scope='Complete exact search cost forecast from bounded same-engine frequency kernels plus actual full-count candidate-selection metadata work. No partial search yields DATA or QUIET.')
    return result


def measure_full_selection_metadata(prepared, receiver, plan, *, family_size):
    """Execute canonical final NMS at its full declared row count, without FFTs."""
    from triad_realtime_fft import _select_workspace_candidates,_select_bit_candidates
    from triad_case_metrics import arithmetic_interval
    screen=receiver.screen
    if (plan['estimated_peak_bytes']+receiver.input_workspace_bytes+
            receiver.native_workspace_bytes > receiver.memory_limit_bytes):
        raise MemoryError('full candidate metadata probe exceeds receiver workspace budget')
    frequency=plan['frequencies_hz']
    per_frequency=plan['raw_candidate_row_capacity']//(len(frequency)*len(screen.workspaces))
    rows=[]
    for workspace in screen.workspaces:
        rows.append([dict(bit=workspace['bit'],epoch=workspace['epoch'],
            arrival=float(screen.arrivals[min(len(screen.arrivals)-1,
                k*max(1,len(screen.arrivals)-1))]),frequency=float(f),
            clock_ppm=workspace['clock_ppm'],rank=workspace['rank'],score=0.)
            for f in frequency for k in range(per_frequency)])
    rng=np.random.default_rng(93287)
    timings=[]
    for order in ('monotone','alternating','shuffled'):
        # Input scores model sort run order only. They are not signal evidence.
        for group in rows:
            values=np.arange(len(group),dtype=float)
            if order=='alternating':values[::2]*=-1
            elif order=='shuffled':rng.shuffle(values)
            for item,value in zip(group,values):item['score']=float(value)
        began=time.perf_counter()
        retained=[[],[]]
        for workspace,group in zip(screen.workspaces,rows):
            retained[workspace['bit']].extend(_select_workspace_candidates(group,
                plan['top_per_bit'],plan['minimum_arrival_separation_seconds'],1/(2*screen.duration)))
        _select_bit_candidates(retained,plan['top_per_bit'])
        timings.append((time.perf_counter()-began)/prepared.slot_seconds)
    rss=_rss_snapshot(prepared.basis_source)
    # These are deliberately different public score-order strata, not IID
    # samples. Reserve a fixed30% allowance above their measured maximum.
    return dict(ratio_processing_seconds_per_baseband_second=max(timings),
        ratio_processing_seconds_per_baseband_second_interval=[min(timings),1.3*max(timings)],
        model_status='exact_geometry_measured',work_only=True,
        score_order_strata=['monotone','alternating','shuffled'],samples=timings,
        exact_candidate_rows=sum(map(len,rows)),rss=rss,
        scope='Actual canonical per-workspace full-count sort/NMS and final merge on synthetic public metadata. No FFT, signal or sensitivity claim;30% fixed score-order allowance is not a statistical confidence bound.')


def public_work_coordinates(receiver, observed=()):
    """A declared cost-only bank, never an acquisition or bit-decision shortcut."""
    coordinates=list(dict.fromkeys(observed))[:12]
    grid=receiver.grid
    arrival=coordinates[0][1] if coordinates else (grid.arrival_lower+grid.arrival_upper)/2
    for epoch in grid.epochs:
        for sign in (-1,1):
            atom=grid.snap((epoch,arrival,
                sign*grid.frequency_bound,0.))
            if atom not in coordinates:coordinates.append(atom)
            if len(coordinates)==12:
                grid.validate_selected(coordinates)
                return tuple(coordinates)
    grid.validate_selected(coordinates)
    return tuple(coordinates)


def measure_acquisition(prepared, receiver, *, family_size):
    from triad_case_metrics import LOOKS,arithmetic_interval
    samples=[]
    representative=None
    work_class=None
    for look in LOOKS:
        while len(samples)<look:
            measured=prepared.acquisition(receiver)
            result=measured['result']
            session=result.pop('coarse_session',None)
            if session is not None:
                session.close()
                return measure_tiled_acquisition(prepared,receiver,family_size=family_size)
            if result['status']!='acquired':
                return dict(model_status='unresolved',reason=result.get('reason'),
                    realtime_status='unresolved',coverage=result)
            current_class=measured.get('acquisition_work_class')
            if representative is not None and (current_class != work_class or any(
                    result.get(key) != representative.get(key)
                    for key in ('coordinates', 'searched_frequency_intervals_hz'))):
                return dict(model_status='unresolved', reason='acquisition_work_changed_between_looks',
                            realtime_status='unresolved', samples=samples)
            representative=result
            work_class=current_class
            samples.append({key:value for key,value in measured.items() if key!='result'})
        point,interval=arithmetic_interval([
            row['ratio_processing_seconds_per_baseband_second'] for row in samples],
            alpha=.05/(family_size*len(LOOKS)*3))
        status='fail' if point>=1 else 'pass' if interval[1]<1 else 'unresolved'
        if status!='unresolved' or look==LOOKS[-1]:break
    return dict(ratio_processing_seconds_per_baseband_second=point,
        ratio_processing_seconds_per_baseband_second_interval=interval,
        measured_complete_component_lower_bound=min(
            row['ratio_processing_seconds_per_baseband_second'] for row in samples),
        model_status='fixed_extent_measured_no_extrapolation',realtime_status=status,
        samples=samples,coverage=representative,acquisition_work_class=work_class,
        scope='Actual complete Helper, filtering, public coarse search and fine-coordinate selection; repeated fixed observation is timing evidence only.')


def noise_only_observation(prepared,seed=96341, *, samples=None):
    """Independent proper-complex H0 work stratum, not a message-end trial."""
    if samples is not None and samples != len(prepared.raw):
        prepared.raw = prepared.valid = None
        prepared.raw = np.empty(samples, dtype=np.complex128)
        prepared.valid = np.empty(samples, dtype=np.uint8)
    random=np.random.default_rng(seed)
    sigma=math.sqrt(prepared.noise_variance/2)
    for first in range(0,len(prepared.raw),_CHUNK):
        count=min(_CHUNK,len(prepared.raw)-first)
        values=random.standard_normal((count,2))
        prepared.raw[first:first+count]=sigma*(values[:,0]+1j*values[:,1])
    prepared.valid.fill(1)
    prepared.observation_work_stratum='proper_complex_noise_only'


def measure_noise_only_fine_calibration(prepared, workers, *, family_size):
    """Use public H0 acquisition to calibrate only its bounded fine work."""
    if prepared.observation_work_stratum!='proper_complex_noise_only':
        raise ValueError('noise-only fine calibration requires the declared H0 observation')
    with prepared.receiver(workers) as receiver:
        acquired = prepared.acquisition(receiver)['result']
        session = acquired.pop('coarse_session',None)
        if session is not None:
            session.close()
        if acquired['status']=='acquired' and acquired.get('coordinates'):
            coordinates = tuple(acquired['coordinates'])
        elif acquired['status']=='partial_acquisition' and session is not None:
            coordinates = public_work_coordinates(receiver)
        else:
            raise ValueError('noise-only fine calibration could not establish its public work coordinates')
        fine = measure_fine_stratum(prepared,receiver,coordinates,stratum='active',family_size=family_size)
    if not (fine['all_observations_complete'] and fine['nonzero_templates_in_every_probe']):
        raise ValueError('noise-only fine calibration did not execute complete bounded work')
    peak = max(row['workspace_bytes'] for row in fine['work_samples']) + prepared.stream_workspace[
        'simultaneous_outer_reserve_bytes']
    if peak>prepared.memory_limit_bytes:
        raise MemoryError('noise-only fine work exceeds the production workspace budget')
    return dict(status='stage_calibration_measured',inference_workers=workers,
        inference_allocation_mode='full_logical',noise_only_fine=fine,
        workspace_margin=dict(status='within_budget',estimated_peak_bytes=peak,
                              budget_bytes=prepared.memory_limit_bytes))


def _scaled_component(summary,multiplier):
    row=copy.deepcopy(summary)
    for key in ('ratio_processing_seconds_per_baseband_second',
                'measured_complete_component_lower_bound'):
        if row.get(key) is not None:row[key]*=multiplier
    key='ratio_processing_seconds_per_baseband_second_interval'
    if row.get(key) is not None:row[key]=[value*multiplier for value in row[key]]
    row['physical_denominator_multiplier']=multiplier
    return row


def _larger_component(rows):
    rows=tuple(rows)
    lower=max((row.get('measured_complete_component_lower_bound',0.) for row in rows),default=0.)
    usable=[row for row in rows if row.get('ratio_processing_seconds_per_baseband_second') is not None]
    if not usable:return dict(model_status='unresolved',reason='no_stage_measurements',
                              measured_complete_component_lower_bound=lower)
    chosen=max(usable,key=lambda row:row['ratio_processing_seconds_per_baseband_second'])
    result=copy.deepcopy(chosen)
    bounds=[row.get('ratio_processing_seconds_per_baseband_second_interval') for row in rows]
    if all(bounds):
        result['ratio_processing_seconds_per_baseband_second_interval']=[
            max(pair[0] for pair in bounds),max(pair[1] for pair in bounds)]
    else:
        result['ratio_processing_seconds_per_baseband_second_interval']=None
    result['combination']='Maximum covered path-stratum service, not their sum.'
    result['measured_complete_component_lower_bound']=lower
    result['model_status']=chosen.get('model_status','unresolved') if len(usable)==len(rows) and all(
        row.get('model_status') in SUPPORTED_MODEL_STATUSES for row in usable) else 'unresolved'
    return result


def measure_complete_case(prepared, allocations, *, family_size, progress=print,
                          sustained_targets=None, inference_workers=None, inference_mode='full_logical'):
    """Measure complete nominal reception and the separate ceiling strata."""
    from triad_case_metrics import workspace_summary
    from triad_orchestration_cost import measure_orchestration

    progress('  comparing same-stream CPU worker allocations')
    allocation=choose_allocation(prepared,allocations)
    selected=allocation['selected_workers']
    if selected is None:
        return dict(status='unresolved',allocation=allocation,
            reason='no_complete_or_bounded_tiled_work_for_worker_selection')
    workers=selected
    mode='pilot_selected'
    if inference_workers is not None:
        if inference_mode not in ('full_logical', 'automatic_receiver', 'explicit'):
            raise ValueError('unknown declared receiver inference allocation')
        if (type(inference_workers) is not int or inference_workers not in allocations or
                not any(row['workers']==inference_workers and row.get('selection_ratio') is not None
                        for row in allocation['samples'])):
            return dict(status='unresolved',allocation=allocation,
                reason=('declared_full_logical_allocation_not_measured' if inference_mode == 'full_logical'
                        else 'declared_receiver_allocation_not_measured'))
        workers=inference_workers
        mode=inference_mode
    progress(f'  fresh measurements with {workers} workers')
    lifecycle_samples=[]
    for _ in range(2):
        began=time.perf_counter()
        with prepared.receiver(workers):
            pass
        lifecycle_samples.append((time.perf_counter()-began)/prepared.slot_seconds)
    lifecycle=dict(ratio_processing_seconds_per_baseband_second=max(lifecycle_samples),
        ratio_processing_seconds_per_baseband_second_interval=[min(lifecycle_samples),1.3*max(lifecycle_samples)],
        model_status='exact_geometry_measured',samples=lifecycle_samples,
        scope='Actual receiver construction, worker startup/barrier and shutdown each public slot. Two fixed-geometry measurements with30% allowance, not a confidence interval.')
    with prepared.receiver(workers) as receiver:
        nominal_acquisition = measure_acquisition(prepared, receiver, family_size=family_size)
        result = nominal_acquisition.get('coverage', {})
        observed = tuple(result.get('coordinates', ())) if result.get('status') == 'acquired' else ()
        if observed:
            progress('  measuring sustained nominal fine tracking')
            active = measure_sustained_fine(prepared, receiver, observed, family_size=family_size)
        else:
            active = dict(schema='triad-sustained-fine-measurement-v1', status='unresolved',
                          reason='complete_public_acquisition_required', observations=[])
        work_coordinates = observed or public_work_coordinates(receiver)
        cap=measure_fine_stratum(prepared,receiver,public_work_coordinates(receiver,work_coordinates),
            stratum='active',family_size=family_size,work_probe=True)
        erasure=prepared.fine_probe(receiver,work_coordinates,units=2,stratum='active',
            interior_erasure=True)
        erasure=_probe_work(erasure)
        cap_upper=cap.get('ratio_processing_seconds_per_baseband_second_interval')
        erasure_supported=bool(cap_upper and erasure['requested_invalid_rows']==2 and
            erasure['additional_discarded_rows']>0 and
            erasure['ratio_processing_seconds_per_baseband_second']<=2*cap_upper[1])
        if not erasure_supported:
            cap.update(model_status='unresolved',reason='erasure_branch_cost_holdout_unresolved')
        terminal=measure_fine_stratum(prepared,receiver,work_coordinates,
            stratum='terminal',family_size=family_size)
    sustained_probe = None
    if sustained_targets is not None:
        progress('  comparing bounded sustained fine work at physical and logical allocations')
        sustained_probe = measure_sustained_allocation_probe(prepared, allocations, workers, observed, active,
            targets=sustained_targets, family_size=3*family_size)
    progress('  measuring actual input/quiet-state/codec orchestration work')
    orchestration=measure_orchestration(prepared,family_size=family_size)
    progress('  measuring noise-only listening and full-prior fallback')
    noise_only_observation(prepared)
    with prepared.receiver(workers) as receiver:
        noise_acquisition=measure_acquisition(prepared,receiver,family_size=family_size)
        noise_coordinates=(tuple(noise_acquisition.get('coverage',{}).get('coordinates',()))
                           or public_work_coordinates(receiver))
        noise_fine=measure_fine_stratum(prepared,receiver,noise_coordinates,
            stratum='active',family_size=family_size)
    # close() joins work, but the Python receiver still owns its screen.
    # Release that last reference before retiring the original bank below.
    del receiver
    input_bytes=prepared.raw.nbytes+prepared.valid.nbytes
    progress('  measuring two fresh recurring public ordinal banks')
    banks=measure_recurring_banks(prepared, workers=workers, family_size=family_size)
    recurring=banks.pop('recurring_window', {})
    recurring_acquisition=recurring.get('noise_only_acquisition', {})
    recurring_assembly=recurring.get('full_window_assembly')
    recurring_covered=bool(recurring_acquisition and recurring_assembly and
        recurring['sample_count']==prepared.recurring_window['window_samples'] and
        recurring['ingress_samples']>=prepared.recurring_window['maximum_ingress_samples'])
    full=orchestration['full_window_assembly']
    assemblies=(full,) if recurring_assembly is None else (full, recurring_assembly)
    # Charge the slower full-size copy/assembly observation. Keep the fixed30%
    # allowance distinct from the timing sample confidence intervals.
    controller_ratio=max(row['nonoverlap_stream_orchestration_seconds_per_baseband_second']*
        row['represented_baseband_seconds']/prepared.slot_seconds for row in assemblies)
    controller=dict(ratio_processing_seconds_per_baseband_second=controller_ratio,
        ratio_processing_seconds_per_baseband_second_interval=[controller_ratio,1.3*controller_ratio],
        model_status='exact_geometry_measured',work_only=True,
        scope='Slower observed full-size production copy/assembly/dispatch; inner state/planner costs measured separately.30% allowance is not a statistical confidence bound.')
    message=copy.deepcopy(orchestration['message_state_and_codec'])
    planning=message.pop('public_slot_plan_service')
    message['model_status']=planning['model_status']='fixed_extent_measured_no_extrapolation'
    common=dict(recurring_bank_preparation=banks,stream_controller=controller,
        quiet_state_and_canonical_codec=message,public_slot_planning=planning,
        receiver_worker_lifecycle=lifecycle)
    normal_fine = active.get('fine_component') or dict(model_status='unresolved',
        reason=active.get('reason') or 'missing_sustained_fine_estimate')
    nominal=combine_components(dict(common,acquisition=nominal_acquisition,
        complete_fine_search=normal_fine),complete_work_covered=True)
    nominal['window_scope']='Measured initial DATA capture; recurring fallback and larger work geometry are separate capacity strata.'
    noise_fine_component=_scaled_component(noise_fine,noise_fine['full_window_forecast_multiplier'])
    terminal_component=_scaled_component(terminal,terminal['full_window_forecast_multiplier'])
    cap_component=_scaled_component(cap,cap['full_window_forecast_multiplier'])
    all_fine=(normal_fine,noise_fine_component,terminal_component,cap_component)
    acquisition_summaries = [nominal_acquisition, noise_acquisition, recurring_acquisition]
    ceiling=combine_components(dict(common, acquisition=_larger_component(acquisition_summaries),
        complete_fine_search=_larger_component(all_fine)),complete_work_covered=recurring_covered)
    nominal['capacity_scope']=ceiling['capacity_scope']='configured-public-calendar'
    if not recurring_covered and ceiling['realtime_status']=='unresolved':
        ceiling['reason']='recurring_public_window_work_not_covered'
    # A deliberately overcounted256+256-state cold-cache/transition ceiling can prevent a
    # pass, but cannot prove the actual ordinary receiver is slower than real time.
    if nominal['realtime_status'] == 'fail':
        ceiling.update(realtime_status='fail', reason='nominal_service_failure',
                       nominal_failure_reason=nominal['reason'])
    elif ceiling['realtime_status'] == 'fail' and ceiling['measured_complete_component_lower_bound'] < 1:
        ceiling['realtime_status']='unresolved'
        ceiling['reason']='conservative_work_ceiling_does_not_establish_positive_margin'
    probes=[row for summary in (cap,terminal,noise_fine) for row in summary['work_samples']]+[erasure]
    ceiling_work = dict(complete_capacity=ceiling, fine_public_state_ceiling=cap, fine_terminal=terminal,
        erasure_branch_holdout=dict(sample=erasure,supported_by_state_ceiling=erasure_supported,
            scope='One actual invalid interval crossing a DFT block boundary; cost checked against the conservative complete-state ceiling, not a separate communication trial or universal erasure timing bound.'),
        noise_only_fine=noise_fine, noise_only_acquisition=noise_acquisition,
        recurring_window=dict(recurring, covers_longest_public_geometry=recurring_covered))
    active_observations = active.get('observations', ())
    active_workspace = [row['workspace'] for row in active_observations if 'workspace' in row]
    estimates=[row['workspace_bytes'] for row in probes]
    estimates.extend(row['estimated_peak_bytes'] for row in active_workspace)
    estimates.extend(row['preparation_peak_estimate_bytes']+row.get('input_reservation_bytes', input_bytes)
        for row in banks['samples'])
    rss=[row['rss'].get('sum_process_bytes') for row in probes]
    rss.extend(row['rss'].get('sum_process_bytes') for row in active_workspace)
    rss.extend(row.get('rss',{}).get('sum_process_bytes') for row in banks['samples'])
    rss.extend((row.get(key) or {}).get('sum_process_bytes') for row in assemblies
        for key in ('resident_sample_before','resident_sample_after'))
    for summary in acquisition_summaries:
        estimates.extend(sample['receiver_peak_estimate_bytes']
            for sample in summary.get('samples',())
            if sample.get('receiver_peak_estimate_bytes') is not None)
        rss.extend(sample.get('rss',{}).get('sum_process_bytes')
            for sample in summary.get('samples',()))
        coverage=summary.get('coverage',{})
        peak=coverage.get('coarse',{}).get('work',{}).get('estimated_peak_bytes')
        if peak is not None:estimates.append(peak)
        if summary.get('work_plan'):
            estimates.append(summary['work_plan']['estimated_peak_bytes']+
                max(input_bytes, 17*prepared.recurring_window['window_samples'])+
                max(prepared.basis_source.descriptor['fixed_workspace_bytes'],
                    prepared.basis_source.descriptor['workspace_budget_bytes']))
            selection=summary.get('components',{}).get('global_candidate_selection',{})
            rss.append(selection.get('rss',{}).get('sum_process_bytes'))
    stream_reserve = prepared.stream_workspace['simultaneous_outer_reserve_bytes']
    workspace=workspace_summary(max(max(estimates)+stream_reserve,
        *(row['estimated_peak_bytes'] for row in assemblies)),prepared.memory_limit_bytes,
        max((value for value in rss if value is not None),default=None))
    workspace['measurement_scope'] = 'full_capacity'
    workspace['production_stream_reservation'] = prepared.stream_workspace
    workspace['peak_bank_or_dsp_with_stream_reserve_bytes'] = max(estimates)+stream_reserve
    if workspace['status']=='failure':
        ceiling.update(realtime_status='fail',reason='workspace_budget_exceeded')
    utilization = [row['observed_cpu_utilization_fraction'] for row in probes]
    for observation in active_observations:
        phases = (observation.get('prefix', {}), *observation.get('checkpoints', ()),
                  observation.get('finish_classify_close', {}))
        utilization.extend(phase.get('observed_allocated_cpu_utilization_fraction') for phase in phases)
    utilization = [value for value in utilization if value is not None]
    def acquisition_cpu(row):
        return _aggregate_cpu((*row.get('samples', ()), *row.get('partial_kernel_samples', ())))
    stage_cpu = dict(
        nominal_acquisition=acquisition_cpu(nominal_acquisition),
        continued_fine=_aggregate_cpu(stage for observation in active_observations
            for stage in (observation.get('prefix', {}), *observation.get('checkpoints', ()),
                          observation.get('finish_classify_close', {}))),
        recurring_bank_preparation=_aggregate_cpu(banks['samples']))
    stage_cpu.update(maximum_state_fine=_aggregate_cpu(cap['work_samples']),
        terminal_fine=_aggregate_cpu(terminal['work_samples']),
        noise_only_fine=_aggregate_cpu(noise_fine['work_samples']),
        noise_only_acquisition=acquisition_cpu(noise_acquisition),
        recurring_reacquisition=acquisition_cpu(recurring_acquisition))
    return dict(status='measured_complete_work_estimate', measurement_scope='full_capacity',
        capacity_scope='configured-public-calendar',
        blind_fresh_start_capacity=unqualified_blind_capacity(prepared.slot_seconds),
        selected_workers=selected, inference_workers=workers, inference_allocation_mode=mode,
        allocation=allocation,nominal_service=nominal,
        **ceiling_work, fine_active=active,
        **({'sustained_allocation_probe':sustained_probe} if sustained_probe is not None else {}),
        nominal_acquisition=nominal_acquisition,orchestration=orchestration,
        recurring_banks=banks,workspace_margin=workspace,
        observed_stage_cpu=stage_cpu,
        observed_allocated_cpu_utilization_fraction_range=[min(utilization),max(utilization)] if utilization else None,
        full_local_cpu_ceiling_demonstrated=False,
        utilization_scope='Actual same-stream supported allocations were compared. Idle/serial portions are retained in elapsed service; no hypothetical scaling to unused cores.',
        reception_success_scope='This case has actual native input and complete implemented receiver work. Short work probes do not establish whole-bit or message success; the separate current-source native gate is configuration-specific.',
        provisional_sensitivity_scope='Use the reported duration-model SNR target as a candidate, not a measured complete-message90% threshold.')


def render_capacity_report(report):
    def number(value):
        return 'unresolved' if value is None else f'{value:.4g}'
    scope=report.get('capacity_scope','historical-unspecified')
    scope_description=('complete receiver work on a configured public calendar' if
        scope=='configured-public-calendar' else scope)
    lines=['# Robust manual configuration capacity', '',
        f"Result: **{report['status']}**.", '',
        'This is the absolute capacity suite for the freshly read manual list. Relative degradation regression is a separate command.', '',
        f'**Scope: {scope_description}. Blind discovery of independently restarted transmitters is unqualified and has no complete CPU estimate here. A scoped capacity pass does not establish ordinary blind-listening feasibility.**', '',
        f"Local CPU: {report['machine'].get('cpu_model')}; supported worker comparisons: {report['worker_allocations']}.",
        'CPU values are prepared elapsed processing seconds per received baseband second. A ratio of1 has no real-time margin;0.1 corresponds to90% margin. No core-count division or independent-stream scaling is used.', '',
        f"Current-source complete-message functional gate: **{report.get('message_gate',{}).get('status','not_run')}**. This one short high-SNR gate is not an all-configuration success certificate.", '',
        '| Current entry | DATA / Helper SNR in1Hz (dB-Hz) | Sustained nominal CPU estimate (s/s) | Nominal point criterion | Conservative receiver-work estimate (s/s) | Capacity decision | DSP workspace |',
        '|---|---:|---:|---|---:|---|---:|']
    warnings=[]
    if report['status']=='invalidated_source_or_configuration_changed':
        warnings.append('Source, binary, numerical runtime or manual configuration changed during measurement. These results cannot qualify the current receiver.')
    if not report.get('selection',{}).get('complete_current_list',False):
        warnings.append('Only a selected subset of the current manual list was measured; this cannot qualify the entire list.')
    if not report.get('selection',{}).get('complete_worker_comparison',False):
        warnings.append('The full declared local CPU worker comparison was not performed; this is a diagnostic allocation run.')
    if warnings:
        lines[4:4]=['**Qualification limits:** '+' '.join(warnings),'']
    for item in report['cases']:
        plan=item.get('native_plan',{});physics=plan.get('physics',{})
        result=item.get('measurement',{});nominal=result.get('nominal_service',{})
        ceiling=result.get('complete_capacity',{});workspace=result.get('workspace_margin',{})
        helper=physics.get('effective_helper_cn0_db_hz')
        # Native metadata owns the link-budget terminology. Do not report
        # nominal configured C/N0 as a measured particular fading realization.
        peak=workspace.get('conservative_peak_bytes')
        point_decision = {'below_one':'below 1', 'fail':'fail'}.get(nominal.get('numeric_point_criterion'), 'unresolved')
        lines.append(f"| {item['case']['index']}. {item['case']['heading']} | {number(physics.get('physical_data_cn0_db_hz'))} / {number(helper) if helper is not None else 'off'} | {number(nominal.get('ratio_processing_seconds_per_baseband_second'))} | {point_decision} | {number(ceiling.get('ratio_processing_seconds_per_baseband_second'))} | {ceiling.get('realtime_status',item.get('status','unresolved'))} | {number(peak/2**20) if peak is not None else 'unresolved'} MiB |")
    lines.extend(['','SNR values above are nominal received link-budget references for total triple-DATA power and effective Helper power in1Hz. They are not measured particular fading-realization powers.'])
    for item in report['cases']:
        lines.extend(['',f"## {item['case']['index']}. {item['case']['heading']}",''])
        if item.get('error'):
            lines.append('Unresolved: '+item['error'])
            continue
        result=item.get('measurement',{});nominal=result.get('nominal_service',{})
        ceiling=result.get('complete_capacity',{});workspace=result.get('workspace_margin',{})
        lines.append(f"Actual inference allocation: {result.get('inference_workers','unresolved')} workers; exploratory pilot winner: {result.get('selected_workers','unresolved')}. Nominal margin estimate: {number(100*nominal['margin_fraction'])+'%' if nominal.get('margin_fraction') is not None else 'unresolved'}. Conservative receiver-work margin: {number(100*ceiling['margin_fraction'])+'%' if ceiling.get('margin_fraction') is not None else 'unresolved'}. Decision: {ceiling.get('reason','unresolved')}.")
        stages = {name:timing for name,timing in result.get('observed_stage_cpu', {}).items() if timing}
        if stages:
            measured = ', '.join(f"{name.replace('_', ' ')} {number(timing['busy_cores'])}"
                                 for name,timing in stages.items())
            lines.append(f"Observed CPU use in busy-core equivalents, weighted by measured elapsed time: {measured}. Native child counters are included where available.")
        lines.append(f"The conditional nominal service decision is {nominal.get('realtime_status', 'unresolved')}. The estimate charges the two-second prefix and finish once per slot and uses the observed continuation rate for the rest of the window.")
        blind=result.get('blind_fresh_start_capacity',{})
        if blind.get('naive_full_window_attempt_density_multiplier') is not None:
            lines.append(f"Blind fresh-start discovery remains unqualified. Naively repeating a complete search every 30 seconds would require {number(blind['naive_full_window_attempt_density_multiplier'])}× as many window attempts as this configured slot cadence. This is a rejected work-density comparison, not a CPU forecast or a valid active-signal admission method.")
        recurring=result.get('recurring_window', {})
        if recurring.get('covers_longest_public_geometry'):
            lines.append(f"Recurring slot {recurring['slot_index']} exercised reacquisition on {recurring['sample_count']} samples and assembly across {recurring['ingress_samples']} ingress samples, including any public gap.")
        elif recurring:
            lines.append('The recurring public window coverage is unresolved. ' + recurring.get('reason', ''))
        comparison=result.get('sustained_allocation_probe')
        if comparison is not None:
            measured=', '.join(f"{row['workers']} workers: {number(row.get('measured_six_second_fine_service',{}).get('ratio_processing_seconds_per_baseband_second'))} s/s at {number((row.get('cpu_timing_summary') or {}).get('busy_cores'))} busy cores"
                for row in comparison['comparisons'])
            lines.append(f"Exploratory sustained fine work over six-second observations: {measured or 'unresolved'}. This comparison does not select the inference allocation or establish a CPU ceiling.")
            speedup=comparison.get('logical_over_physical_speedup')
            if speedup is not None:
                lines.append(f"Full-logical over physical-core elapsed speedup on matched fine work: {number(speedup['point'])}×.")
        lines.append('The state-ceiling probe starts256 adaptive and256 fixed-lane states, executes two segments including actual transition/merge work, and charges that entire cold-cache unit at every real segment. Its conservative estimate can withhold a pass; exceeding1 does not by itself prove ordinary receiver failure.')
        if workspace.get('estimated_margin_fraction') is not None:
            lines.append(f"Workspace budget {workspace['budget_bytes']/2**20:.1f} MiB; estimated headroom {100*workspace['estimated_margin_fraction']:.1f}%. Explicit peak buffers and sampled parent/native RSS are retained in JSON; this is not an allocation certificate.")
        prep=item.get('preparation',{})
        lines.append(f"Observed receiver preparation: {number(prep.get('receiver_preparation_seconds'))} seconds. "+
            ('This exceeds the five-second readiness target; no unavoidable minimum is established.' if prep.get('receiver_preparation_seconds',0)>5 else 'No long preparation requirement was demonstrated.'))
        target=item.get('target_snr',{});fox=target.get('fox',{});single=target.get('single_bit',{})
        lines.append(f"Approximate duration-model candidate for>90% success: bit {number(single.get('target_cn0_db_hz'))}, literal22-bit `fox` {number(fox.get('target_cn0_db_hz'))} dB-Hz. This is not a measured complete-receiver success threshold; acquisition retention, fades and false ends remain unqualified.")
        quiet=item.get('quiet_energy_diagnostic',{})
        bound=quiet.get('conditional_data_to_quiet',{}).get('bound')
        lines.append(f"Actual fixed-channel projection diagnostic: {quiet.get('projected_modes','unresolved')} quiet-statistic modes; nominal DATA-to-QUIET probability upper estimate {number(bound)} under the declared Gaussian noise model. DATA admission takes precedence over the quiet check. A bound near 1 leaves the false-termination rate unresolved. A displayed zero may reflect numerical underflow. The diagnostic covers one fixed waveform and channel.")
        calendar=item.get('native_plan',{}).get('transmit_calendar',{})
        lines.append(f"Native high-SNR transmit calendar: one bit {number(calendar.get('single_bit_seconds'))} seconds; literal `fox` {number(calendar.get('short_message_seconds'))} seconds. Physical quiet confirmation occurs afterward and is not an on-air length header.")
        lines.append('All measured stages and uncertainty/model limits are in JSON, including Helper fallback, fine/terminal/listening work, recurring keyed banks, actual large-window copies, public planning, quiet state and canonical codec. High utilization of all local CPUs is not assumed from the worker count.')
    lines.extend(['',f"Result: **{report['status']}**. Configuration SHA-256: `{report['input']['source_sha256']}`."])
    return '\n'.join(lines)+'\n'


def capacity_status(report):
    """A subset, skipped correctness gate or debug allocation cannot pass all cases."""
    if (not report['provenance'].get('unchanged_at_completion') or
            report.get('runtime',{}).get('unchanged_at_completion') is False or
            not report['input'].get('unchanged_at_completion')):
        return 'invalidated_source_or_configuration_changed'
    outcomes=[item.get('measurement',{}).get('complete_capacity',{}).get('realtime_status')
        for item in report['cases']]
    if 'fail' in outcomes:
        return 'capacity_failures'
    selection=report['selection']
    if (outcomes and all(value=='pass' for value in outcomes) and
            report['message_gate'].get('passed') and selection['complete_current_list'] and
            selection['complete_worker_comparison']):
        return 'conditional_capacity_pass'
    return 'measured_with_unresolved_margins'


def run_complete_cases(*,executable,output,case_list,measurement_seconds=1.,threads=None,
                       case_indices=None,paired_followup=False,message_gate=True,sustained_allocation_probe=False,
                       workspace_mib=None,
                       full_logical_inference=None,calibration_stage=None):
    """Distinct absolute-capacity entry point; always reread the edited list."""
    import hashlib
    import json
    from pathlib import Path
    import subprocess
    from triad_abbreviated import environment
    from triad_cases import load_cases
    from triad_case_suite import identity,native_plan,same_measured_source,target_snr,select_cases,write_result
    from triad_case_metrics import machine_resources
    from triad_cpu import configure_numerical_threads,resolve_cpu_budget
    from triad_receiver import _public_metrics
    suite_started = time.perf_counter()
    if paired_followup:
        raise ValueError('coarse-only paired confirmation is not a complete-receiver capacity test')
    if calibration_stage is not None and (calibration_stage!='noise-only-fine' or not case_indices or
            workspace_mib is None or threads is not None or sustained_allocation_probe or full_logical_inference is False):
        raise ValueError('noise-only fine calibration requires selected rows, a fixed workspace and a declared receiver allocation')
    parsed=load_cases(case_list)
    selected=select_cases(parsed,case_indices)
    if not math.isfinite(measurement_seconds) or not .1<=measurement_seconds<=2:
        raise ValueError('measurement seconds must be in[0.1,2]')
    machine=machine_resources()
    if machine['cpu'] is None or machine['available_memory_bytes'] is None:
        raise RuntimeError('visible local CPU and memory budgets are required')
    workspace_limit=machine['memory_budget_bytes']
    if workspace_mib is None:
        if workspace_limit is None:
            raise RuntimeError('insufficient available physical RAM for the receiver operating reserve')
    else:
        if (type(workspace_mib) is not int or
                not 0 < workspace_mib*2**20 <= (machine['available_memory_bytes'] or 0)):
            raise ValueError('requested stable workspace exceeds visible available physical/cgroup RAM')
        workspace_limit=workspace_mib*2**20
    if type(sustained_allocation_probe) is not bool or (sustained_allocation_probe and threads is not None):
        raise ValueError('sustained allocation comparison requires the canonical full worker pilot')
    sustained_targets=sustained_allocation_targets(machine['cpu']) if sustained_allocation_probe else None
    allocation_plan=manual_worker_plan(machine['cpu'], threads=threads,
        full_logical_inference=full_logical_inference, calibration_stage=calibration_stage)
    allocations=allocation_plan['allocations']
    inference_workers=allocation_plan['inference_workers']
    configure_numerical_threads(resolve_cpu_budget(jobs=1,workers=max(allocations),threads=1))
    executable=Path(executable).resolve()
    provenance=identity(executable)
    runtime=environment('cpu')
    report=dict(schema=('triad-manual-complete-capacity-v3' if calibration_stage is None
                        else 'triad-manual-stage-calibration-v1'),status='running',
        capacity_scope='configured-public-calendar',
        blind_fresh_start_capacity=unqualified_blind_capacity(),
        input={key:value for key,value in parsed.items() if key!='cases'},
        provenance=provenance,machine=machine,runtime=runtime,worker_allocations=allocations,
        workspace_limit_bytes=workspace_limit,
        policy=measurement_policy(len(parsed['cases']),max(1,round(measurement_seconds*2))*.5),
        receiver_allocation=allocation_plan,
        sustained_allocation_targets=sustained_targets,
        selection=dict(indices=[case['index'] for case in selected],
            complete_current_list=calibration_stage is None and len(selected)==len(parsed['cases']),
            complete_worker_comparison=calibration_stage is None and threads is None,
            current_list_entries=len(parsed['cases'])),cases=[])
    report['policy']['unit_baseband_seconds_scope'] = (
        'Rounded requested unit. Each row records its actual, possibly shorter fine unit in public_geometry.')
    if inference_workers is not None and calibration_stage is None:
        report['policy'].update(
            allocation_selection='Fresh inference at the declared receiver allocation after its supported same-stream pilot observation.',
            allocation_counts='Automatic:1, known physical cores, visible logical CPUs and receiver target; deduplicated. Explicit controls preserve their declared allocation.',
            receiver_inference_mode=allocation_plan['inference_mode'],
            receiver_inference_workers=inference_workers)
        if allocation_plan['inference_mode'] == 'full_logical':
            report['policy']['full_logical_inference_workers']=inference_workers
    if calibration_stage is not None:
        report['calibration_stage'] = calibration_stage
    write_result(output,report)
    if calibration_stage is not None:
        report['message_gate']=dict(status='stage_calibration_only',passed=False)
    elif message_gate:
        from triad_message_gate import run_native_message_gate
        print('current-source configured-calendar native message/quiet/rearm/EOF gate',flush=True)
        report['message_gate']=run_native_message_gate(executable,workspace_limit,
            workers=min(2,machine['cpu']['available_cpus']),case_list=case_list,continuous=True,
            progress=lambda line:print('  '+line,flush=True))
    else:
        report['message_gate']=dict(status='explicitly_skipped',passed=False)
    measured={}
    for case in selected:
        print(f"case {case['index']}/{len(parsed['cases'])}: {case['heading']}",flush=True)
        if case['configuration_hash'] in measured:
            item=copy.deepcopy(measured[case['configuration_hash']])
            item['reused_measurement_from']=item['case']['id'];item['case']=case
        else:
            item=dict(case=case,status='unresolved')
            try:
                if workspace_mib is not None and (machine_resources()['available_memory_bytes'] or 0) < workspace_limit:
                    raise ValueError('predeclared workspace exceeds current available physical/cgroup RAM')
                plan=native_plan(executable,case['native_args'])
                item.update(native_plan=plan)
                if not plan.get('geometry_feasible'):raise ValueError('native public geometry is infeasible')
                options = {'noise_only':True} if calibration_stage is not None else {}
                with PreparedCapacityCase(executable,case,plan,
                        memory_limit_bytes=workspace_limit,measurement_seconds=measurement_seconds,
                        bank_workers=inference_workers or 1, **options) as prepared:
                    item['preparation']=dict(receiver_preparation_seconds=prepared.receiver_preparation_seconds,
                        simulator_generation_seconds=prepared.simulator_generation_seconds,
                        long_preparation_unavoidable=None)
                    item['calibration_inputs']=dict(channel_seed=prepared.channel_seed,
                        noise_seed=prepared.noise_seed, source_bit=prepared.source_bit,
                        initial_noise_only_seed=96341, recurring_noise_only_seed=96342)
                    item['public_geometry']=prepared.public_geometry
                    family_size=report['policy']['family_size']
                    if calibration_stage is not None:
                        item['preparation']['observation_kind']='seeded_noise_only'
                        item['measurement']=measure_noise_only_fine_calibration(prepared,inference_workers,
                            family_size=family_size)
                        item['measurement']['inference_allocation_mode']=allocation_plan['inference_mode']
                    else:
                        item['target_snr']=target_snr(plan,complete_admission=prepared.admission_model_receipt())
                        print('  fixed-channel quiet-energy diagnostic',flush=True)
                        item['quiet_energy_diagnostic']=prepared.quiet_energy_diagnostic(item['target_snr'])
                        item['measurement']=measure_complete_case(prepared,allocations,
                            inference_workers=inference_workers,family_size=family_size,
                            inference_mode=allocation_plan['inference_mode'],
                            progress=lambda line:print(line,flush=True),sustained_targets=sustained_targets)
                    item['status']=item['measurement']['status']
            except (ValueError,RuntimeError,MemoryError,EOFError,OSError,subprocess.SubprocessError) as error:
                item['error']=str(error)
            measured[case['configuration_hash']]=item
        report['cases'].append(item)
        write_result(output,_public_metrics(report))
    try:
        final=identity(executable)
        report['provenance']['revision_at_completion']=final['revision']
        report['provenance']['unchanged_at_completion']=same_measured_source(provenance,final)
    except (OSError,subprocess.SubprocessError) as error:
        report['provenance']['unchanged_at_completion']=False
        report['provenance']['final_read_error_type']=type(error).__name__
    report['runtime']['unchanged_at_completion']=(runtime==environment('cpu') and
        machine['cpu']==machine_resources()['cpu'])
    report['input']['unchanged_at_completion']=hashlib.sha256(Path(case_list).read_bytes()).hexdigest()==parsed['source_sha256']
    report['status']=capacity_status(report)
    if calibration_stage is not None and report['status']!='invalidated_source_or_configuration_changed':
        report['status']=('stage_calibration_measured' if all(
            row['status']=='stage_calibration_measured' for row in report['cases']) else 'unresolved_stage_calibration')
    report['suite_observed_elapsed_seconds']=time.perf_counter()-suite_started
    report=_public_metrics(report)
    write_result(output,report)
    summary=dict(status=report['status'],output=str(output),cases=len(report['cases']))
    if calibration_stage is None:
        path=Path(output).with_suffix('.md')
        path.write_text(render_capacity_report(report))
        summary['report']=str(path)
    print(json.dumps(summary))
    return report
