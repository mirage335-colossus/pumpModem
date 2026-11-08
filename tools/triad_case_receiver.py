"""Bounded actual-native subwindow probes for the editable Robust case suite.

This adapter wires the retained FFT proposal and persistent tracking kernels.
It does not turn their sequential research stages into a qualified receiver.
The native channel advances continuously from its public capture origin; only
the receiver's explicitly declared local observation is abbreviated. No realized
arrival, oscillator error, channel state, or transmitted bit selects its bank.
"""
from __future__ import annotations

import copy
import math
import os
from pathlib import Path
import time

import numpy as np

from triad_continuous import NativeStream, tracking_source_basis
from triad_operational_admission import public_grid
from triad_realtime_fft import FFTScreen
from triad_timing_forward import SparseTimingForward, TimingState
from triad_tracking_receiver import PracticalTrackingReceiver
from triad_weak_helper import estimate_weak_helper


EPOCHS = tuple(range(-6, 7))
MAXIMUM_UNITS = 4


def _integer(value, name, minimum=1):
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise ValueError(name + ' must be a positive integer')
    return value


def _positive(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
        raise ValueError(name + ' must be finite and positive')
    return float(value)


def _option(arguments, flag, value):
    """Replace exactly one public fixture option, rejecting ambiguous argv."""
    count = arguments.count(flag)
    if count > 1:
        raise ValueError('duplicate native option: ' + flag)
    if count:
        at = arguments.index(flag)
        if at + 1 >= len(arguments):
            raise ValueError('missing native option value: ' + flag)
        arguments[at + 1] = str(value)
    else:
        arguments.extend((flag, str(value)))


def _native_cpu_seconds(source):
    """CPU of this adapter's still-running native child, never all children."""
    process = getattr(source, 'process', None)
    if process is None:
        return None
    try:
        fields = Path('/proc/' + str(process.pid) + '/stat').read_text().rsplit(')', 1)[1].split()
        return (int(fields[11]) + int(fields[12])) / os.sysconf('SC_CLK_TCK')
    except (OSError, ValueError, IndexError, AttributeError):
        return None


def _rss_snapshot(source):
    """Sample resident process storage; this is not an allocator peak proof."""
    def resident(pid):
        try:
            for line in Path('/proc/' + str(pid) + '/status').read_text().splitlines():
                if line.startswith('VmRSS:'):
                    return int(line.split()[1]) * 1024
        except (OSError, ValueError, IndexError):
            pass
        return None
    process = getattr(source, 'process', None)
    parent = resident(os.getpid())
    child = resident(process.pid) if process is not None else None
    return dict(parent_process_bytes=parent, native_child_bytes=child,
        sum_process_bytes=None if parent is None or child is None else parent + child,
        scope='Sampled current parent plus owned native-child RSS, including Python/numerical runtime and shared pages; not exclusive DSP storage or proof of transient peak.')


def _timing(wall, cpu, represented, threads, native_cpu_delta=None):
    elapsed = time.perf_counter() - wall
    process = time.process_time() - cpu
    return dict(represented_baseband_seconds=represented,
        ratio_processing_seconds_per_baseband_second=elapsed / represented,
        observed_cpu_utilization_fraction=(process + (native_cpu_delta or 0.)) / (elapsed * threads) if elapsed else None,
        native_child_cpu_in_utilization=native_cpu_delta is not None,
        utilization_scope=('Observed Python and owned native-child CPU / elapsed / allocated threads; Linux child counters have scheduler-tick resolution.' if native_cpu_delta is not None else
            'Observed Python-process CPU / elapsed / allocated threads; native child CPU unavailable, so this is partial utilization.'),
        ratio_scope='Single receiver elapsed service time / actual input baseband seconds; no thread-count division.')


def _manifest(plan, descriptor, samples, source_first, seed):
    physics = copy.deepcopy(plan['physics'])
    physics.setdefault('endpoint_uniform_half_bound_ppm',
        float(physics.get('base_relative_pair_ppm_bound', 0.)) / 2)
    physics['nominal_conversion_if_hz'] = float(descriptor['data_if_hz'])
    fmt = copy.deepcopy(plan['format'])
    fmt['bit_seconds'] = float(fmt['elapsed_bit_seconds'])
    fmt['physical_rf_retuning'] = False
    helper_start = 0.
    helper_end = int(descriptor['emitted_samples']) / descriptor['sample_rate_hz']
    if descriptor['helper_enabled']:
        calendar = plan.get('transmit_calendar', {})
        if 'first_helper_start_seconds' not in calendar or 'first_helper_end_seconds' not in calendar:
            raise ValueError('whole-bit Helper requires the native public transmit calendar')
        helper_start = float(calendar['first_helper_start_seconds'])
        helper_end = float(calendar['first_helper_end_seconds'])
    return dict(schema='triad-public-local-probe-v1',
        sample_rate_hz=int(descriptor['sample_rate_hz']), format=fmt, channel=physics,
        helper_enabled=bool(descriptor['helper_enabled']),
        public_epoch_seconds=int(descriptor['epoch']), key_seed=seed,
        calendar=dict(capture_samples=samples,
            capture_lead_seconds=float(descriptor['capture_lead_seconds']),
            data_sample_offset=int(descriptor['data_offset_samples']) + source_first,
            arrival_prior_seconds=list(physics['arrival_prior_seconds']),
            helper_start_seconds=helper_start,
            helper_end_seconds=helper_end,
            fixed_capture_tail_bound_seconds=float(physics['spread_delay_ms']) * .001),
        helper_plan=dict(low_hz=float(descriptor['helper_low_hz'] or 0.),
            high_hz=float(descriptor['helper_high_hz'] or 0.), auxiliary_hz=950.,
            helper_gain=0., pilot_dwell_gating=bool(descriptor['helper_enabled'] and fmt['fhss'])),
        power=dict(complex_noise_variance=float(descriptor['noise_variance']),
            post_plan_composite_gain=1.))


def _fallback_coordinates(manifest):
    """Public diagnostic subset used only after an explicit proposal failure.

    This is not a claim to cover the continuous original uncertainty volume.
    Keeping every epoch and its central coordinate makes the missing coverage
    inspectable while still measuring the real tracker rather than omitting it.
    """
    grid = public_grid(manifest['channel'], manifest['calendar']['arrival_prior_seconds'], EPOCHS)
    lo, hi = manifest['calendar']['arrival_prior_seconds']
    middle = (lo + hi) / 2
    coordinates = [(epoch, middle, 0., 0.) for epoch in EPOCHS]
    for arrival, frequency, clock in ((lo, 0., 0.), (hi, 0., 0.),
            (middle, -grid.frequency_bound, 0.), (middle, grid.frequency_bound, 0.),
            (middle, 0., -grid.clock_bound), (middle, 0., grid.clock_bound)):
        coordinates.append((0, arrival, frequency, clock))
    return tuple(dict.fromkeys(grid.snap(row) for row in coordinates))


class PreparedCase:
    """One owned native source and fixed, bounded public observation.

    Repeated measurements reuse the same observation and are timing replicates,
    never independent reception trials. Every measurement constructs fresh
    mutable tracker state. Construction and channel generation are preparation;
    on-demand keyed template rendering during push is timed receiver service.
    """
    def __init__(self, executable, native_args, *, measurement_seconds=1.,
                 max_probe_samples=16384, memory_limit_bytes=512*1024**2,
                 threads=1, seed=1, template_backend='auto', source_factory=NativeStream):
        began = time.perf_counter()
        seconds = _positive(measurement_seconds, 'measurement seconds')
        cap = _integer(max_probe_samples, 'probe sample cap')
        self.memory_limit = _integer(memory_limit_bytes, 'memory limit')
        self.threads = _integer(threads, 'threads')
        if template_backend not in ('auto', 'direct'):
            raise ValueError('tracking templates must use auto or direct')
        self.template_backend = template_backend
        seed = _integer(seed, 'seed', 0)
        if (not isinstance(native_args, (list, tuple)) or
                any(not isinstance(value, str) for value in native_args)):
            raise ValueError('native arguments must be a string list')
        arguments = list(native_args)
        for flag in ('--output', '--templates', '--stream-server', '--stream-kind',
                     '--plan-only', '--power-plan-only', '--key-file', '--now'):
            if flag in arguments:
                raise ValueError('case probe native arguments must omit ' + flag)
        _option(arguments, '--seed', seed)
        if '--key-seed' not in arguments:
            _option(arguments, '--key-seed', 1)
        if '--epoch' not in arguments:
            _option(arguments, '--epoch', 1793404800)
        _option(arguments, '--whole-bit-count', 1)
        _option(arguments, '--power-reference', 'bounded-tiles')
        self.native_arguments = arguments
        self.source = None
        try:
            source_began = time.perf_counter()
            self.source = source_factory(executable, arguments, kind='channel')
            self.native_source_setup_seconds = time.perf_counter() - source_began
            self.plan = copy.deepcopy(self.source.plan)
            self.descriptor = copy.deepcopy(self.source.descriptor)
            self.rss_before_preparation = _rss_snapshot(self.source)
            fs = int(self.descriptor['sample_rate_hz'])
            self.sample_rate = fs
            self.unit_samples = min(cap, max(4, round(seconds * fs)))
            self.measurement_seconds = self.unit_samples / fs
            calendar_origin = int(self.plan['geometry']['source_calendar_origin_sample'])
            tile_seconds = float(self.plan['format'].get('tile_seconds',
                self.plan['format']['elapsed_bit_seconds']))
            interior = .5 * min(tile_seconds, float(self.plan['format']['elapsed_bit_seconds']))
            self.source_first = calendar_origin + round(interior * fs)
            available = int(self.descriptor['source_samples']) - self.source_first
            if available < 2 * MAXIMUM_UNITS * self.unit_samples:
                self.unit_samples = available // (2 * MAXIMUM_UNITS)
                if self.unit_samples < 4:
                    raise ValueError('native source has insufficient complete probe support')
                self.measurement_seconds = self.unit_samples / fs
            self.probe_samples = MAXIMUM_UNITS * self.unit_samples
            self.data_origin = (round(float(self.descriptor['capture_lead_seconds']) * fs) +
                int(self.descriptor['data_offset_samples']) + self.source_first)
            physics = self.plan['physics']
            prior = list(map(float, physics['arrival_prior_seconds']))
            clock = float(physics['sample_clock_pair_ppm']) * float(physics['spread_margin']) * 1e-6
            if clock >= .1:
                raise ValueError('case probe clock envelope exceeds supported finite window')
            # Include the complete local source support for EVERY original
            # arrival/clock candidate, plus finite interpolation/filter slack.
            end = math.ceil((self.data_origin / fs + 2 * self.probe_samples / fs +
                max(0., prior[1])) * fs / (1 - clock))
            end += math.ceil((float(physics['spread_delay_ms']) * .001 + .1) * fs) + 8
            if end > int(self.descriptor['total_samples']):
                raise ValueError('complete public probe support exceeds native capture endpoint')
            # Retain a bounded local arrival-support window. The channel still
            # advances every preceding sample so no physical state is reset.
            start = max(0, math.floor((self.data_origin / fs + min(0., prior[0])) *
                fs / (1 + clock)) - math.ceil(.1 * fs) - 8)
            self.raw_first_sample = start
            helper_end = (min(end, math.ceil((float(self.descriptor['capture_lead_seconds']) +
                int(self.descriptor['data_offset_samples']) / fs + max(0., prior[1]) + 1) * fs))
                if self.descriptor['helper_enabled'] else 0)
            template_bytes = len(EPOCHS) * 2 * self.probe_samples * 16
            capture_bytes = (end - start + helper_end) * 17
            self.capture_bytes = capture_bytes
            # Reserve half the declared envelope for kernel workspaces and
            # protocol scratch before allocating any duration-sized array.
            if capture_bytes + template_bytes > self.memory_limit // 2:
                raise MemoryError('public case probe capture/templates exceed half workspace envelope')
            self.raw = np.empty(end - start, dtype=np.complex128)
            self.valid = np.empty(end - start, dtype=np.uint8)
            self.helper_raw = np.empty(helper_end, dtype=np.complex128)
            self.helper_valid = np.empty(helper_end, dtype=np.uint8)
            simulation_began = time.perf_counter()
            for first in range(0, end, 16384):
                count = min(16384, end - first)
                values, valid = self.source.next(count)
                if len(values) != count or len(valid) != count:
                    raise EOFError('native channel ended before complete public probe')
                if first < helper_end:
                    kept = min(count, helper_end - first)
                    self.helper_raw[first:first + kept] = values[:kept]
                    self.helper_valid[first:first + kept] = valid[:kept]
                kept_first, kept_end = max(start, first), min(end, first + count)
                if kept_end > kept_first:
                    source_slice = slice(kept_first - first, kept_end - first)
                    target_slice = slice(kept_first - start, kept_end - start)
                    self.raw[target_slice] = values[source_slice]
                    self.valid[target_slice] = valid[source_slice]
            self.simulation_generation_seconds = time.perf_counter() - simulation_began
            receiver_began = time.perf_counter()
            rows = [(bit, epoch, 0., 0., 0., 0., 0.) for epoch in EPOCHS for bit in (0, 1)]
            self.templates = np.empty((len(EPOCHS), 2, self.probe_samples), np.complex128)
            for first in range(0, self.probe_samples, 16384):
                count = min(16384, self.probe_samples - first)
                values = self.source.basis(self.source_first + first, count, rows)
                if values.shape != (26, count) or not np.isfinite(values).all():
                    raise ValueError('native public template substring has invalid shape/values')
                self.templates[:, :, first:first + count] = values.reshape(13, 2, count)
            key_seed = int(arguments[arguments.index('--key-seed') + 1])
            self.manifest = _manifest(self.plan, self.descriptor, end - start, self.source_first, key_seed)
            self.manifest['calendar']['capture_lead_seconds'] -= start / fs
            # Helper's guaranteed DATA-free aperture refers to the ORIGINAL
            # finite DATA support, never the later probe substring.
            self.helper_manifest = _manifest(self.plan, self.descriptor, helper_end, 0, key_seed)
            energies = np.sum(np.abs(self.templates)**2, axis=-1)
            differences = np.sum(np.abs(self.templates[:, 0] - self.templates[:, 1])**2, axis=-1)
            self.template_discrimination = dict(
                minimum_bit_template_energy=float(energies.min()),
                minimum_bit_pair_squared_difference=float(differences.min()),
                every_epoch_has_distinct_nonzero_bits=bool(np.all(energies > 0) and np.all(differences > 0)),
                scope='Finite public substring energies/differences only; distinct waveforms do not establish identifiability under unknown channel gain or correct-bit probability.')
            self.grid = public_grid(self.manifest['channel'], prior, EPOCHS)
            self.frequency_bound = self.grid.frequency_bound
            self.clocks = ((-self.grid.clock_bound, 0., self.grid.clock_bound)
                if self.grid.clock_bound else (0.,))
            required = (float(self.plan['format']['occupied_bandwidth_hz']) / 2 +
                abs(float(physics['spread_doppler_hz'])) + self.frequency_bound)
            output = next((rate for rate in range(1, fs + 1)
                if fs % rate == 0 and .4 * rate > required), None)
            if output is None:
                raise ValueError('full public nominal DATA prior does not fit raw Nyquist')
            self.screen = FFTScreen(self.manifest, self.templates, EPOCHS,
                clock_ppm=self.clocks, output_sample_rate=output,
                pass_edge_hz=.4 * output, stop_edge_hz=.49 * output,
                workers=self.threads, precision='fp32',
                memory_limit_bytes=self.memory_limit - capture_bytes,
                maximum_frequency_points=1024, maximum_fft_products=32768)
            self.receiver_preparation_seconds = time.perf_counter() - receiver_began
            self.workspace = dict(capture_bytes=capture_bytes, template_bytes=template_bytes,
                fft_resident_estimate_bytes=self.screen.resident_estimate,
                native_fixed_workspace_bytes=int(self.descriptor['fixed_workspace_bytes']),
                memory_limit_bytes=self.memory_limit,
                discarded_preceding_capture_samples=start,
                rss_before_preparation=self.rss_before_preparation,
                rss_after_preparation=_rss_snapshot(self.source),
                scope='Explicit array/source accounting; allocator/RSS, library and child scratch are separate. Native storage is added, never treated as part of Python owned bytes.')
            self.coverage = dict(schema='triad-case-local-probe-coverage-v1',
                channel='Actual canonical native streaming channel, continuously advanced from capture origin; all selected effects unchanged.',
                source='Native public keyed DATA substrings; all thirteen epochs and both bit alternatives.',
                epochs=list(EPOCHS), arrival_prior_seconds=prior,
                frequency_prior_hz=[-self.frequency_bound, self.frequency_bound],
                coarse_clock_ppm=list(self.clocks), full_grid_candidate_count=self.grid.candidate_count,
                probe_source_first_sample=self.source_first,
                probe_selection='Public midpoint of first native tile; independent of received samples and transmitted bit.',
                template_discrimination=self.template_discrimination,
                retained_capture_first_global_sample=start,
                acquisition_subwindow_seconds=self.probe_samples / fs,
                nominal_tracking_subwindow_start_sample=self.data_origin + self.probe_samples,
                tracking_window_selection='Observed proposal arrival midpoint; follows the selected template substring and may overlap the acquisition raw prefix. No simulation truth is used.',
                timing_replicates_share_input=True, whole_bit_covered=False,
                physical_rf_retuning=False,
                next_action='Independent-seed whole-bit or validated sufficient-statistic holdout at the forecast duration is required to estimate complete-bit success; local probes cannot qualify that extrapolation.')
            self.preparation_seconds = time.perf_counter() - began
        except BaseException:
            self.close()
            raise

    def _coordinates(self, coarse):
        candidates = coarse.get('candidates', ())
        if candidates:
            # Selection observes DATA; preserve its ORIGINAL full-grid mass
            # when reporting evidence below. Never use evaluator truth here.
            coordinates = tuple(dict.fromkeys(self.grid.snap((int(row['epoch']),
                float(row['arrival']), float(row['frequency']), float(row['clock_ppm'])))
                for row in candidates))
            self.grid.validate_selected(coordinates)
            return coordinates, 'observed_fft_proposals'
        return _fallback_coordinates(self.manifest), 'public_diagnostic_subset_after_acquisition_unresolved'

    def measure(self, units=1):
        if self.source is None:
            raise RuntimeError('case probe is closed')
        if isinstance(units, bool) or not isinstance(units, int) or units not in (1, 2, 4):
            raise ValueError('case probe units must be one, two, or four')
        # Helper and FFT filtering/scoring are per-observation service, not
        # reusable preparation. Their failures remain named stage outcomes.
        child_before = _native_cpu_seconds(self.source)
        wall, cpu = time.perf_counter(), time.process_time()
        frequencies = np.linspace(-self.frequency_bound, self.frequency_bound,
            2 * math.ceil(self.frequency_bound / .5) + 1) if self.frequency_bound else np.array([0.])
        helper = estimate_weak_helper(self.helper_raw, self.helper_manifest,
            frequencies, valid=self.helper_valid)
        frequency = helper.get('frequency') or {}
        intervals = (frequency['expanded_intervals_hz']
            if frequency.get('status') == 'narrowed' else [[-self.frequency_bound, self.frequency_bound]])
        coarse = self.screen.score(self.raw, frequency_intervals=intervals,
            valid=self.valid, top_per_bit=2)
        child_after = _native_cpu_seconds(self.source)
        child_delta = None if child_before is None or child_after is None else child_after - child_before
        acquisition = dict(_timing(wall, cpu, len(self.raw) / self.sample_rate, self.threads, child_delta),
            status=coarse['status'], reason=coarse.get('reason'),
            work_units=1, acquisition_subwindow_seconds=self.probe_samples / self.sample_rate,
            helper_observation_seconds=len(self.helper_raw) / self.sample_rate,
            helper_status=helper.get('status'), helper_reason=helper.get('reason'),
            helper_frequency_status=frequency.get('status'),
            candidates=coarse.get('candidates', []), procedure_id=self.screen.procedure_id,
            workspace_bytes=self.screen.resident_estimate,
            exposure_scope='Actual complete public prefix filtered/scored for one abbreviated DATA template subwindow; not a complete-bit acquisition result.')
        # Everything selected from this observation, including construction
        # of its mutable tracker, is per-observation service. Only the fixed
        # public FFT/template bank above is reusable pre-transmission work.
        child_before = _native_cpu_seconds(self.source)
        wall, cpu = time.perf_counter(), time.process_time()
        coordinates, bank_source = self._coordinates(coarse)
        samples = units * self.unit_samples
        # A broad arrival prior can put DATA seconds after the nominal probe.
        # Use the OBSERVED public proposals for the actual handoff, never the
        # channel's hidden arrival. This finite diagnostic may reuse samples
        # from the prefix inspected during acquisition; no independent or
        # whole-procedure probability/admission is inferred from that reuse.
        selected_arrival = float(np.median([row[1] for row in coordinates]))
        first = self.data_origin + self.probe_samples + round(selected_arrival * self.sample_rate)
        last = first + samples
        local_first, local_last = first - self.raw_first_sample, last - self.raw_first_sample
        if local_first < 0 or local_last > len(self.raw):
            raise ValueError('selected public tracking subwindow leaves prepared observation')
        values, valid = self.raw[local_first:local_last], self.valid[local_first:local_last]
        physics = self.plan['physics']
        delay, doppler = float(physics['spread_delay_ms']) * .001, float(physics['spread_doppler_hz'])
        taps = ((0., 0.), (delay, doppler)) if delay or doppler else ((0., 0.),)
        coherences = (.5, 2., math.inf)
        # The actual channel's sampling-clock error is static. This local
        # probe keeps the acquired public affine bank and channel persistence;
        # it makes no qualification claim for unmeasured clock-motion regimes.
        forward = SparseTimingForward([TimingState(group, 0, 0) for group in range(len(coordinates))],
            model_count=len(coherences), delay_step_seconds=.001,
            rate_step_ppm=1., maximum_rate_ppm=0., maximum_acceleration_ppm_per_second=0.,
            maximum_states=max(64, len(coordinates)), maximum_proposals=max(64, len(coordinates)))
        offset = (int(self.descriptor['data_offset_samples']) / self.sample_rate +
            float(self.descriptor['capture_lead_seconds']))
        # The canonical bounded cache starts empty for EACH trial. Its native
        # tile fills and interpolation remain timed service; it never receives
        # observation values. Unsupported geometry falls back explicitly to
        # the direct mapped native reference.
        cache_bytes = min(32 * 1024**2, self.memory_limit // 8) if self.template_backend == 'auto' else 0
        prepared = time.perf_counter()
        native_provider = tracking_source_basis(self.source, coordinates, taps, offset, forward,
            float(self.descriptor['data_if_hz']), template_backend=self.template_backend,
            cache_bytes=cache_bytes)
        template_work = dict(calls=0, energy=0.)
        def provider(*arguments):
            output = native_provider(*arguments)
            template_work['calls'] += 1
            template_work['energy'] += float(np.vdot(output, output).real)
            return output
        receiver = PracticalTrackingReceiver(sample_rate=self.sample_rate, samples=samples,
            block_samples=max(1, round(.25 * self.sample_rate)),
            segment_samples=max(1, round(.5 * self.sample_rate)),
            basis_provider=provider, timing_forward=forward,
            required_bands=[[-.49 * self.sample_rate, .49 * self.sample_rate]],
            noise_variance=float(self.descriptor['noise_variance']),
            stationary_power=tuple(1 / len(taps) for _ in taps),
            coherence_seconds=coherences, precision='fp32', projection_mode='valid-rows',
            memory_limit_bytes=self.memory_limit - self.capture_bytes -
                self.screen.resident_estimate - cache_bytes,
            maximum_input_chunk_samples=min(16384, samples), require_physical_map=True)
        tracker_preparation = time.perf_counter() - prepared
        rss_samples = [_rss_snapshot(self.source)]
        for at in range(0, samples, 16384):
            stop = min(samples, at + 16384)
            receiver.push(at, values[at:stop], valid[at:stop],
                physical_origin=first + at, physical_offsets=np.arange(stop - at, dtype=float))
            rss_samples.append(_rss_snapshot(self.source))
        result = receiver.finish()
        child_after = _native_cpu_seconds(self.source)
        child_delta = None if child_before is None or child_after is None else child_after - child_before
        timing = _timing(wall, cpu, samples / self.sample_rate, self.threads, child_delta)
        cache_report = native_provider.cache_report()
        cache_peak = int(cache_report.get('peak_cache_bytes', 0))
        rss_values = [row['sum_process_bytes'] for row in rss_samples if row['sum_process_bytes'] is not None]
        tracking = dict(timing,
            status=result['status'], reason=None,
            work_units=units,
            bit_scores=result['bit_scores'], complete_observation=result['complete_observation'],
            candidate_bit=int(np.argmax(result['bit_scores'])) if result['bit_scores'] is not None else None,
            work_counts=result['work_counts'], workspace_bytes=result['working_bytes_estimate'] + cache_peak,
            mapped_template_cache=cache_report,
            tracking_first_sample=first, selected_arrival_seconds=selected_arrival,
            observation_selection='Observed proposal arrival midpoint; overlaps acquisition prefix where applicable. Not evaluator truth or an independent follow-up trial.',
            rendered_template_energy=template_work['energy'],
            template_render_calls=template_work['calls'],
            representative_work=bool(self.template_discrimination['every_epoch_has_distinct_nonzero_bits'] and
                template_work['energy'] > 0 and
                result['work_counts'].get('candidate_block_updates', 0) > 0),
            rss_sampled_peak_sum_process_bytes=max(rss_values) if rss_values else None,
            rss_scope=rss_samples[0]['scope'],
            tracker_preparation_seconds=tracker_preparation,
            tracker_preparation_scope='Data-dependent tracker construction; INCLUDED in this stage processing ratio, not reusable pre-transmission readiness.',
            bank_source=bank_source, selected_coordinates=[list(row) for row in coordinates],
            full_grid_candidate_count=self.grid.candidate_count,
            selected_prior_mass=len(coordinates) / self.grid.candidate_count,
            original_grid_log_mass_penalty=math.log(self.grid.candidate_count / len(coordinates)),
            evidence_scope='Local diagnostic only; tracker normalizes its selected initial bank. Subtract original_grid_log_mass_penalty from bit_scores before any full-grid model comparison. No admission is asserted.',
            clock_motion_scope='Fixed public affine candidates; actual synthetic channel has static sample-clock ppm. Evolving clock timing branches are not covered.',
            native_template_rendering_timed=True)
        return dict(schema='triad-case-receiver-measurement-v1', units=units,
            measured_baseband_seconds=samples / self.sample_rate,
            preparation_seconds=self.preparation_seconds,
            receiver_preparation_seconds=self.receiver_preparation_seconds,
            native_source_setup_seconds=self.native_source_setup_seconds,
            simulation_generation_seconds=self.simulation_generation_seconds,
            stages=dict(acquisition=acquisition, tracking=tracking),
            outcome=dict(whole_bit_success_probability=None,
                scope='Public subwindow proposal/tracking diagnostic; repeated input supplies timing replicates only.',
                full_receiver_real_time_qualified=False,
                success_trials_independent=False))

    def close(self):
        source = getattr(self, 'source', None)
        self.source = None
        return source.close() if source is not None else None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def prepare_case(executable, native_args, *, measurement_seconds=1.,
                 max_probe_samples=16384, memory_limit_bytes=512*1024**2,
                 threads=1, seed=1, template_backend='auto'):
    return PreparedCase(executable, native_args, measurement_seconds=measurement_seconds,
        max_probe_samples=max_probe_samples, memory_limit_bytes=memory_limit_bytes,
        threads=threads, seed=seed, template_backend=template_backend)
