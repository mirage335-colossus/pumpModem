"""Bounded full-bit coarse correct-bit sensitivity trials for manual cases.

This module exercises the complete native bit waveform and the public coarse
FFT proposal screen. It estimates conditional coarse correct-bit sensitivity;
it is not a Robust product receiver, PracticalTracking endpoint, fox-message
decoder, or operational admission test.
"""
from __future__ import annotations

import copy
import hashlib
import json
import math
from pathlib import Path
import time
from collections.abc import Mapping

import numpy as np
from scipy.fft import next_fast_len

from triad_case_noise import ProjectedNoise
from triad_case_receiver import EPOCHS, _manifest, _option
from triad_continuous import NativeStream
from triad_operational_admission import public_grid
from triad_realtime_fft import FFTScreen, compare_template_projectors
from triad_dynamic_joint import _sample_cubic
from triad_weak_helper import estimate_weak_helper, weak_helper_policy


_MAX_MAPPED_TEMPLATE_POINTS = 120_000_000
_MAX_INTEGER_TEMPLATE_WORK = 30_000_000
_MAX_HELPER_SAMPLES = 8_000_000
_CHUNK = 16_384


def helper_workspace_estimate(samples, sample_rate):
    """Conservative default-Helper transient estimate, not an allocator bound.

    Reserve 1024 bytes/raw sample for simultaneous complex mixing, spectral,
    taper, sort, mask, onset and sequential profile arrays; 256 bytes/padded FFT
    row for convolution scratch; and16 MiB for bounded profiles/diagnostics.
    The DATA bank remains resident separately while Helper runs.
    """
    if samples == 0:
        return 0
    if samples < 0 or sample_rate <= 0:
        raise ValueError('invalid Helper workspace geometry')
    policy = weak_helper_policy()
    taps = 2*math.ceil(policy['filter_half_support_seconds']*sample_rate)+1
    return 1024*samples + 256*next_fast_len(samples+taps-1) + 16*1024**2


def calibrated_noise_limit(receipt, taps, *, mapped_step, helper_prefix_samples,
                           memory_limit_bytes):
    """Admit a larger exact S3 factor only after one cheap current-geometry probe.

    The default100M operation budget remains unchanged for other geometries.
    This changes simulation work admission, never covariance or trial counts.
    """
    rows = receipt['cropped_output_count']
    bandwidth = (len(taps)-1)//receipt['downsample']
    work = rows*(bandwidth+1)*(bandwidth+2)//2
    record = dict(status='default_limit', factor_work=work, maximum_factor_work=100_000_000)
    if work <= 100_000_000 or mapped_step != 3 or helper_prefix_samples:
        return 100_000_000, record
    if work > 500_000_000:
        raise ValueError('S3 projected-noise factor exceeds the fixed500M work ceiling')
    probe = copy.deepcopy(receipt)
    probe['cropped_output_count'] = min(8192, rows)
    began = time.perf_counter()
    calibration = ProjectedNoise(probe, taps, memory_limit_bytes=memory_limit_bytes)
    factor_seconds = time.perf_counter()-began
    draws = []
    for seed in (86100, 86101, 86102):
        began = time.perf_counter()
        calibration.draw(seed)
        draws.append(time.perf_counter()-began)
    factor = rows/probe['cropped_output_count']
    factor_forecast = 2*factor*factor_seconds
    draw_forecast = 2*factor*max(draws)
    record.update(status='admitted' if factor_forecast <= 15 and draw_forecast <= 2 else 'cost_limit',
        probe_rows=probe['cropped_output_count'], bandwidth=bandwidth,
        probe_factor_preparation_seconds=factor_seconds, probe_simulator_draw_seconds=draws,
        conservative_factor_forecast_seconds=factor_forecast,
        conservative_simulator_draw_forecast_seconds=draw_forecast,
        maximum_factor_work=500_000_000,
        policy='One8192-row actual-FIR/phase probe, three draws; linear-row forecast with2x cache/model allowance. Factor<=15s and draw<=2s; fixed500M work ceiling and unchanged memory/row/band/boundary caps. Conditional cost estimate, not a wall-time guarantee.')
    return (500_000_000 if record['status'] == 'admitted' else 100_000_000), record


def _sha256(value):
    return hashlib.sha256(value).hexdigest()


def _bounded_basis(source, epoch, bit, positions):
    """Use public native mapped-basis tiles under both native API limits."""
    positions = np.asarray(positions, dtype=np.float64)
    result = np.empty(len(positions), dtype=np.complex128)
    at = 0
    while at < len(positions):
        first = max(0, math.floor(float(positions[at])))
        stop = at + 1
        while stop < len(positions) and stop-at < _CHUNK:
            if math.floor(float(positions[stop])) - first > 1_000_000:
                break
            stop += 1
        integer_first = max(0, math.floor(float(positions[at])))
        offsets = positions[at:stop] - integer_first
        values = source.basis_mapped(integer_first, offsets,
            [(bit, epoch, 0., 0., 0., 0., 0.)], 0.)[0]
        result[at:stop] = values
        at = stop
    return result


def _integer_basis(source, epoch, bit, first, count):
    """Read an integer-source template span in API-sized native tiles."""
    if isinstance(first, bool) or not isinstance(first, int) or first < 0:
        raise ValueError('integer source start must be a nonnegative integer')
    if isinstance(count, bool) or not isinstance(count, int) or count < 0:
        raise ValueError('integer source count must be nonnegative')
    blocks = []
    for at in range(0, count, _CHUNK):
        amount = min(_CHUNK, count-at)
        blocks.append(source.basis(first+at, amount,
            [(bit, epoch, 0., 0., 0., 0., 0.)])[0])
    return np.concatenate(blocks) if blocks else np.empty(0, np.complex128)


def _clean_options(executable, native_args, *, bit, seed):
    args = list(native_args)
    for flag in ('--output', '--templates', '--stream-server', '--stream-kind',
                 '--plan-only', '--power-plan-only', '--key-file', '--now',
                 '--diagnostic-iq-noise-bits', '--diagnostic-iq-noise-obscured-bits',
                 '--diagnostic-iq-noise-db', '--diagnostic-iq-noise-watts',
                 '--diagnostic-iq-noise-reference-watts', '--diagnostic-iq-noise-reference-rms'):
        if flag in args:
            raise ValueError('replay native arguments must omit ' + flag)
    _option(args, '--seed', seed)
    _option(args, '--bit', bit)
    _option(args, '--whole-bit-count', 1)
    _option(args, '--power-reference', 'bounded-tiles')
    _option(args, '--thermal-noise', 'off')
    for flag, value in (('--seconds', '.001'), ('--sample-rate', '6000'),
                        ('--data-if', '1725'), ('--epoch', '1793850000'),
                        ('--key-seed', '72001')):
        _option(args, flag, value)
    return [str(executable), *args]


def _mapped_identity(command, binary, descriptor):
    data = json.dumps(dict(command=command, binary_sha256=_sha256(Path(binary).read_bytes()),
        pattern_revision=descriptor.get('pattern_revision'),
        added_amplitude_depth=descriptor.get('added_amplitude_depth'),
        source_samples=descriptor['source_samples']),
        sort_keys=True, separators=(',', ':')).encode()
    return _sha256(data)


def _local_fidelity_manifest(manifest, public_address, clocks, *, source_window_samples, fs):
    """Make the bounded normalized calendar used only for profile admission."""
    local = copy.deepcopy(manifest)
    prior = local['calendar']['arrival_prior_seconds']
    lead = 8.
    tail = float(local['channel']['spread_delay_ms'])*.001
    maximum_clock = max(map(abs, clocks), default=0.)*1e-6
    capture = math.ceil((lead+2+source_window_samples/fs+max(0., float(prior[1]))+
                         tail+1)*fs/(1-maximum_clock))
    local['calendar'].update(capture_lead_seconds=lead, data_sample_offset=2*fs,
        capture_samples=capture, physical_origin_sample=0)
    local['public_epoch_seconds'] = public_address['pattern_epoch']
    local.setdefault('format', {})['symbol_ordinal'] = public_address['symbol_ordinal']
    return local


def _validate_mapped_profile(source, manifest, clocks, *, output_fs, pass_edge,
                             stop_edge, public_address, memory_limit_bytes, threads, sampling_step=60):
    """Cheap current-input admission; no heading or historical fixture whitelist."""
    if isinstance(sampling_step, bool) or sampling_step not in (3, 60):
        raise ValueError('local mapped-profile validation supports only fixed S=3 or S=60')
    fs = int(manifest['sample_rate_hz'])
    fmt = source.plan['format']
    first = int(source.plan['geometry']['source_calendar_origin_sample'] +
        .5*min(float(fmt['tile_seconds']), float(fmt['elapsed_bit_seconds']))*fs)
    count = min(2*fs, int(source.descriptor['source_samples'])-first)
    native_workspace = max(int(source.descriptor.get('fixed_workspace_bytes', 0)),
        int(source.descriptor.get('workspace_budget_bytes', 0)))
    # Native pipe bytes, decoded basis and contiguous/hash views may coexist.
    # Reserve all four template-sized copies before requesting any basis rows.
    template_reserve = count*26*16*4
    available = memory_limit_bytes-native_workspace-template_reserve
    if count < 256 or available <= 0:
        raise MemoryError('local mapped-profile validation exceeds its bounded public bank')
    rows = [(bit, epoch, 0., 0., 0., 0., 0.) for epoch in EPOCHS for bit in (0, 1)]
    templates = source.basis(first, count, rows).reshape(13, 2, count)
    # Only the short local fidelity diagnostic uses a normalized receive
    # calendar. Its eight-second pre-roll contains the complete -7.035 s prior.
    local = _local_fidelity_manifest(manifest, public_address, clocks,
        source_window_samples=count, fs=fs)
    lead = local['calendar']['capture_lead_seconds']
    capture = local['calendar']['capture_samples']
    tail = float(local['channel']['spread_delay_ms'])*.001
    identity = _sha256(np.ascontiguousarray(templates).view(np.uint8))
    options = dict(clock_ppm=clocks, output_sample_rate=output_fs,
        pass_edge_hz=pass_edge, stop_edge_hz=stop_edge, precision='fp64',
        workers=threads, memory_limit_bytes=available)
    exact = FFTScreen(local, templates, EPOCHS, **options)
    options['memory_limit_bytes'] = available-exact.resident_estimate
    if options['memory_limit_bytes'] <= 0:
        raise MemoryError('local mapped-profile pair exceeds preparation workspace')
    approximate = FFTScreen(local, None, EPOCHS, source_samples=count,
        template_identity=identity, template_sampling_step=sampling_step,
        mapped_template_provider=lambda epoch, bit, positions:
            _bounded_basis(source, epoch, bit, first+positions), **options)
    residents = exact.resident_estimate+approximate.resident_estimate
    # The comparison retains two orthonormal banks, plus the current QR/SVD
    # operands. Controls reserve cubic interpolation, phase, clean/noisy input
    # and RNG temporaries. These are separate phases, not simultaneous sums.
    comparison_scratch = (2*len(exact.workspaces)+16)*exact.kernel_rows*len(exact.rays)*16+65536
    control_scratch = 32*capture*16
    score_peaks = []
    for screen in (exact, approximate):
        frequencies = np.asarray([0.])
        groups = (None if screen.correlation_method == 'frequency' else
            screen._lag_frequency_groups([0.], None))
        score_plan = screen._correlation_plan(frequencies, groups, raw_input=True, top_per_bit=2)
        if score_plan['selected'] is None:
            raise MemoryError('local mapped-profile control score exceeds workspace or work limit')
        score_peaks.append(score_plan['estimated_peak_bytes_by_method'][score_plan['selected'][0]]-
                           screen.resident_estimate)
    local_peak = native_workspace+template_reserve+max(
        exact.preparation_peak_estimate_bytes,
        exact.resident_estimate+approximate.preparation_peak_estimate_bytes,
        residents+comparison_scratch,
        residents+control_scratch+max(score_peaks))
    if local_peak > memory_limit_bytes:
        raise MemoryError('local mapped-profile comparison/control peak exceeds workspace')
    comparison = compare_template_projectors(exact, approximate)
    # Matched clean and common-noise controls exercise the actual selector.
    # This is a local working-model discriminator, never a success-rate trial.
    times = np.arange(capture)/fs-lead
    arrival = float(exact.arrivals[np.argmin(abs(exact.arrivals))])
    clean = np.zeros(capture, complex)
    for gain, delay, doppler in ((.8+.2j, 0., 0.), (.3-.6j, tail,
            float(local['channel']['spread_doppler_hz']))):
        clean += gain*_sample_cubic(templates[6, 0],
            ((times-arrival-delay)-2)*fs)*np.exp(2j*np.pi*doppler*times)
    rng = np.random.default_rng(21733)
    controls = []
    for data in (clean, clean+(rng.normal(size=capture)+1j*rng.normal(size=capture))/math.sqrt(2)):
        before = exact.score(data, [0.], top_per_bit=2)
        after = approximate.score(data, [0.], top_per_bit=2)
        if not before.get('candidates') or not after.get('candidates'):
            controls.append(False)
            continue
        left = max(before['candidates'], key=lambda row:row['score'])
        right = max(after['candidates'], key=lambda row:row['score'])
        controls.append((left['bit'], left['epoch']) == (right['bit'], right['epoch']))
    comparison.update(source_window_first=first, source_window_samples=count,
        preparation_peak_estimate_bytes=local_peak,
        common_clean_noisy_winner_controls=controls, sampling_step=sampling_step,
        output_sample_rate_hz=output_fs, pass_edge_hz=pass_edge, stop_edge_hz=stop_edge)
    comparison['accepted'] = bool(comparison['rank_agreement'] and
        comparison['maximum_projector_distance'] <= .01 and
        comparison['maximum_stored_scoring_operator_distance'] <= .01 and
        comparison['maximum_column_relative_norm_error'] <= .01 and
        comparison['maximum_gram_relative_norm_error'] <= .02 and
        comparison['minimum_wrong_bit_projector_separation_ratio'] >= .9 and all(controls))
    if not comparison['accepted']:
        raise ValueError('current local mapped-template fidelity gate failed: '+
            json.dumps(comparison, sort_keys=True))
    return comparison


def _normalize_public_address(public_address):
    if not isinstance(public_address, Mapping) or set(public_address) != {
            'pattern_epoch', 'symbol_ordinal'}:
        raise ValueError('public address must map pattern_epoch and symbol_ordinal explicitly')
    epoch, ordinal = public_address['pattern_epoch'], public_address['symbol_ordinal']
    if (isinstance(epoch, bool) or not isinstance(epoch, int) or not 6 <= epoch <= 4_294_967_289 or
            isinstance(ordinal, bool) or not isinstance(ordinal, int) or not 0 <= ordinal < 2**64):
        raise ValueError('public address epoch or symbol ordinal is outside its native integer range')
    return {'pattern_epoch': epoch, 'symbol_ordinal': ordinal}


def _source_identity_digest(source_identity, address):
    try:
        serialized = json.dumps(dict(source_identity=source_identity,
            public_address=address), sort_keys=True, separators=(',', ':'), allow_nan=False)
    except (TypeError, ValueError) as error:
        raise ValueError('source identity must be finite JSON data') from error
    if source_identity is None or source_identity == '':
        raise ValueError('a nonempty public source identity is required')
    return _sha256(serialized.encode())


def build_public_screen(source, manifest, *, public_address, source_identity,
                        memory_limit_bytes, threads, mapped_step=None,
                        screen_factory=FFTScreen):
    """Build the public coarse bank for an explicit current native pattern.

    The source descriptor describes its original stream/calendar. The public
    template basis is independently addressed by ``public_address`` and bound
    into the returned template identity. Any local fidelity capture changes
    stay inside that diagnostic; the returned full-screen manifest retains the
    caller's receive geometry.
    """
    began = time.perf_counter()
    address = _normalize_public_address(public_address)
    if (isinstance(threads, bool) or not isinstance(threads, int) or threads < 1 or
            isinstance(memory_limit_bytes, bool) or not isinstance(memory_limit_bytes, int) or
            memory_limit_bytes < 1):
        raise ValueError('positive thread count and memory limit are required')
    address_started = time.perf_counter()
    address_receipt = source.set_basis_address(address['pattern_epoch'], address['symbol_ordinal'])
    address_seconds = time.perf_counter()-address_started
    if (address_receipt.get('basis_epoch') != address['pattern_epoch'] or
            address_receipt.get('basis_symbol_ordinal') != address['symbol_ordinal'] or
            address_receipt.get('template_cache_cleared') is not True):
        raise ValueError('native source did not acknowledge the requested public basis address')
    descriptor = source.descriptor
    plan = source.plan
    fs = int(descriptor['sample_rate_hz'])
    if fs != 6000:
        raise ValueError('whole-bit public screen currently requires 6000-Hz native input')
    raw_source_samples = int(descriptor['source_samples'])
    raw_capture_samples = int(descriptor['total_samples'])
    if raw_source_samples < 4 or raw_capture_samples < 1:
        raise ValueError('native full-bit source has incomplete finite geometry')
    public_manifest = copy.deepcopy(manifest)
    public_manifest['public_epoch_seconds'] = address['pattern_epoch']
    public_manifest.setdefault('format', {})['symbol_ordinal'] = address['symbol_ordinal']
    physics = plan['physics']
    grid = public_grid(physics, physics['arrival_prior_seconds'], EPOCHS)
    clock_bank = ((-grid.clock_bound, 0., grid.clock_bound) if grid.clock_bound else (0.,))
    required = (float(plan['format']['occupied_bandwidth_hz'])/2 +
        abs(float(physics['spread_doppler_hz'])) + grid.frequency_bound)
    output_fs = next((rate for rate in range(1, fs+1)
        if fs % rate == 0 and .4*rate > required), None)
    if output_fs is None:
        raise ValueError('full public DATA prior does not fit the projected Nyquist band')
    eligible = (float(plan['format']['inner_rate_hz']) <= 1 and
        not plan['format']['fhss'] and required < 4.)
    exact_work = (raw_source_samples*len(EPOCHS)*2*len(clock_bank)*
        (2 if physics['spread_delay_ms'] or physics['spread_doppler_hz'] else 1))
    eligible_fhss = (exact_work > _MAX_INTEGER_TEMPLATE_WORK and
        plan['format']['fhss'] and required < 240.)
    selected_step = (60 if eligible else 3 if eligible_fhss else 1) if mapped_step is None else mapped_step
    if isinstance(selected_step, bool) or selected_step not in (1, 3, 60):
        raise ValueError('only fixed S=3 or S=60 mapped templates are supported')
    if selected_step == 60 and not eligible:
        raise ValueError('S=60 templates require a locally validated slow non-hopping waveform')
    if selected_step == 3 and not eligible_fhss:
        raise ValueError('S=3 templates require an over-budget exact FHSS bank within the 240 Hz prior band')
    pass_edge, stop_edge = .4*output_fs, .49*output_fs
    profile_validation, profile_validation_seconds = None, 0.
    if selected_step > 1:
        output_fs, pass_edge, stop_edge = ((600, 240., 294.) if selected_step == 3 else
            (10, .4, 4.9) if required < .4 else
            (5, 2., 2.45) if required < 2. else (20, 4., 9.8))
        profile_started = time.perf_counter()
        profile_validation = _validate_mapped_profile(source, public_manifest, clock_bank,
            output_fs=output_fs, pass_edge=pass_edge, stop_edge=stop_edge,
            public_address=address, memory_limit_bytes=memory_limit_bytes,
            threads=threads, sampling_step=selected_step)
        profile_validation_seconds = time.perf_counter()-profile_started
    template_work = (math.ceil(raw_source_samples/selected_step)*len(EPOCHS)*2*len(clock_bank)*
        (2 if physics['spread_delay_ms'] or physics['spread_doppler_hz'] else 1))
    cap = _MAX_MAPPED_TEMPLATE_POINTS if selected_step > 1 else _MAX_INTEGER_TEMPLATE_WORK
    if template_work > cap:
        raise MemoryError('full public template preparation exceeds the bounded native-work limit')
    source_digest = _source_identity_digest(source_identity, address)
    mapped_provider = None
    raw_provider = None
    if selected_step > 1:
        mapped_provider = lambda epoch, bit, positions: _bounded_basis(source, epoch, bit, positions)
    else:
        def raw_provider(epoch, bit, first, count):
            return _integer_basis(source, epoch, bit, first, count)
    started = time.perf_counter()
    native_workspace = max(int(descriptor.get('fixed_workspace_bytes', 0)),
                           int(descriptor.get('workspace_budget_bytes', 0)))
    available = memory_limit_bytes-native_workspace
    screen = screen_factory(public_manifest, None, EPOCHS,
        clock_ppm=clock_bank, output_sample_rate=output_fs,
        pass_edge_hz=pass_edge, stop_edge_hz=stop_edge,
        workers=threads, precision='fp64', memory_limit_bytes=available,
        maximum_frequency_points=4096 if selected_step == 60 else 1024,
        maximum_fft_products=32768,
        correlation_method='auto' if selected_step == 60 else 'frequency',
        template_provider=raw_provider, source_samples=raw_source_samples,
        template_identity=source_digest,
        mapped_template_provider=mapped_provider,
        template_sampling_step=selected_step)
    screen_seconds = time.perf_counter()-started
    preparation_peak = max(screen.preparation_peak_estimate_bytes+native_workspace,
        (profile_validation or {}).get('preparation_peak_estimate_bytes', 0))
    profile = dict(schema='triad-public-coarse-bank-profile-v1',
        template_sampling_step=selected_step, projected_sample_rate_hz=output_fs,
        pass_edge_hz=pass_edge, stop_edge_hz=stop_edge,
        public_epochs=list(EPOCHS), public_clocks_ppm=list(clock_bank),
        public_arrival_prior_seconds=list(physics['arrival_prior_seconds']),
        public_frequency_bound_hz=grid.frequency_bound,
        current_input_fidelity_validation=profile_validation,
        address_bound_native_receipt=address_receipt,
        fidelity_scope='A mapped profile is admitted only by its current-source local bank discriminator; this is not a full-waveform error bound.')
    work = dict(raw_source_samples=raw_source_samples,
        raw_capture_samples=raw_capture_samples,
        public_template_lanes=len(EPOCHS)*2*len(clock_bank),
        native_basis_sample_work_estimate=template_work,
        native_basis_work_cap=cap,
        complete_projected_rows=screen.crop_rows,
        fft_workspace_bytes=screen.resident_estimate,
        preparation_peak_estimate_bytes=preparation_peak,
        mapped_template_profile_validation_seconds=profile_validation_seconds,
        fft_bank_preparation_seconds=screen_seconds,
        native_address_selection_seconds=address_seconds,
        scope='Fixed public path geometry and current native source identity; admission bounds materialization work before full-bank construction.')
    preparation = time.perf_counter()-began
    receipt = dict(native_address_selection_seconds=address_seconds,
        local_profile_validation_seconds=profile_validation_seconds,
        public_fft_bank_preparation_seconds=screen_seconds,
        native_source_sample_extent=raw_source_samples,
        native_channel_capture_extent=raw_capture_samples,
        complete_projected_output_rows=screen.crop_rows,
        profile_native_template_point_estimate=template_work,
        source_fft_workspace_estimate_bytes=screen.resident_estimate,
        preparation_peak_estimate_bytes=preparation_peak,
        scope='Bound to current native source, explicit pattern epoch/symbol ordinal, full receive geometry and public search. Local fidelity validation alone uses its normalized diagnostic calendar.')
    return dict(screen=screen, manifest=public_manifest, grid=grid,
        clock_bank=clock_bank, profile=profile, work=work,
        template_identity=source_digest, source_identity=source_identity,
        public_address=address, address_receipt=address_receipt,
        receiver_preparation_seconds=preparation,
        profile_validation_seconds=profile_validation_seconds,
        work_receipt=receipt)


class PreparedReplay:
    """A complete public bit observation and two-alternative coarse receiver."""
    def __init__(self, executable, native_args, *, memory_limit_bytes, threads,
                 seed, native_factory=NativeStream, screen_factory=FFTScreen,
                 mapped_step=None):
        if (isinstance(threads, bool) or not isinstance(threads, int) or threads < 1 or
                isinstance(memory_limit_bytes, bool) or not isinstance(memory_limit_bytes, int) or
                memory_limit_bytes < 1):
            raise ValueError('positive thread count and memory limit are required')
        self.executable = str(executable)
        self.threads, self.memory_limit = threads, memory_limit_bytes
        self.native_factory = native_factory
        began = time.perf_counter()
        command = _clean_options(executable, native_args, bit=0, seed=seed)
        self.arguments = command[1:]
        self.streams = []
        self.observations, self.helper_observations = {}, {}
        self._noise = None
        self._noise_factor_seconds = None
        try:
            setup_start = time.perf_counter()
            self.template_source = native_factory(executable, self.arguments, kind='channel')
            self.streams.append(self.template_source)
            self.plan = copy.deepcopy(self.template_source.plan)
            descriptor = copy.deepcopy(self.template_source.descriptor)
            if int(descriptor['sample_rate_hz']) != 6000:
                raise ValueError('whole-bit replay currently requires 6000-Hz complex channel input')
            self.descriptor = descriptor
            self.fs = int(descriptor['sample_rate_hz'])
            raw_source_samples = int(descriptor['source_samples'])
            raw_capture_samples = int(descriptor['total_samples'])
            if raw_capture_samples < 1 or raw_source_samples < 4:
                raise ValueError('native full-bit source has incomplete finite geometry')

            self.manifest = _manifest(self.plan, descriptor, raw_capture_samples, 0, 72001)
            self.manifest['power']['complex_noise_variance'] = 1.
            self.manifest['helper_enabled'] = bool(descriptor['helper_enabled'])
            public_address = {'pattern_epoch': int(descriptor['epoch']),
                              'symbol_ordinal': int(descriptor['symbol_ordinal'])}
            source_identity = _mapped_identity(command, executable, descriptor)
            bank = build_public_screen(self.template_source, self.manifest,
                public_address=public_address, source_identity=source_identity,
                memory_limit_bytes=self.memory_limit, threads=threads,
                mapped_step=mapped_step, screen_factory=screen_factory)
            self.manifest = bank['manifest']
            self.grid, self.clock_bank = bank['grid'], bank['clock_bank']
            self.screen = bank['screen']
            self.public_address = bank['public_address']
            self.public_bank_profile = bank['profile']
            self.public_bank_work = bank['work']
            self.public_bank_work_receipt = bank['work_receipt']
            self.output_sample_rate = self.public_bank_profile['projected_sample_rate_hz']
            self.mapped_profile_validation = self.public_bank_profile['current_input_fidelity_validation']
            self.profile_validation_seconds = bank['profile_validation_seconds']
            self.template_work_estimate = self.public_bank_work['native_basis_sample_work_estimate']
            self.receiver_preparation_seconds = bank['receiver_preparation_seconds']
            self.native_template_preparation_seconds = self.receiver_preparation_seconds
            self.native_source_setup_seconds = time.perf_counter()-setup_start-self.receiver_preparation_seconds
            self.screen_identity = bank['template_identity']
            self.native_clean_generation_seconds = 0.
            self.observation_samples = self.screen.samples
            self.represented_baseband_seconds = self.observation_samples/self.fs
            self.clean_projection_seconds = {}
            self.helper_prefix_samples = 0
            if descriptor['helper_enabled']:
                helper_prefix = min(raw_capture_samples,
                    math.ceil((float(descriptor['capture_lead_seconds']) +
                               int(descriptor['data_offset_samples'])/self.fs +
                               max(0., self.grid.arrival_upper)) * self.fs))
                if helper_prefix > _MAX_HELPER_SAMPLES:
                    raise MemoryError('public Helper aperture exceeds the bounded eight-million-sample input limit')
                self.helper_prefix_samples = helper_prefix
            self.coverage = dict(schema='triad-full-bit-coarse-replay-v1',
                endpoint='Static coarse complex FFT GLRT; conditional correct-bit sensitivity only.',
                epochs=list(EPOCHS), public_arrival_prior_seconds=list(self.plan['physics']['arrival_prior_seconds']),
                public_frequency_bound_hz=self.grid.frequency_bound,
                public_clock_bank_ppm=list(self.clock_bank),
                bit_template_count=len(EPOCHS)*2*len(self.clock_bank),
                source_sample_rate_hz=self.fs, projected_sample_rate_hz=self.output_sample_rate,
                raw_source_samples=raw_source_samples, raw_capture_samples=self.screen.samples,
                projected_observation_samples=self.screen.crop_rows,
                coarse_arrival_step_seconds=1/self.output_sample_rate,
                coarse_arrival_end_gap_seconds=self.grid.arrival_upper-float(self.screen.arrivals[-1]),
                projection_receipt=self.screen.projection_receipt(),
                complete_finite_filter_tail_retained=True,
                helper_enabled=bool(descriptor['helper_enabled']),
                helper_raw_prefix_samples=self.helper_prefix_samples,
                helper_frequency_future_horizon_seconds=self.represented_baseband_seconds,
                mapped_template_sampling_step=self.public_bank_profile['template_sampling_step'],
                mapped_template_current_input_validation=self.mapped_profile_validation,
                mapped_template_fidelity_scope=('Current native local-bank validation covers every epoch/bit/clock/ray with rank/projector checks and common clean/noise selectors; local checks are not a whole-waveform error bound.'
                    if self.public_bank_profile['template_sampling_step'] > 1 else 'Exact integer-source native basis provider, including cubic timing and original FIR.'),
                decision_rule='Unique largest public bit score. Empty, incomplete, invalid, or tied candidate results are failures.',
                truth_scope='The transmitted bit selects which clean observation to simulate. The evaluator label never selects the bank, search, or decision.',
                excluded='PracticalTracking, physical false acceptance, deployed GUI, multi-bit fox decoding, and persistent receiver state.')
            self.workspace = dict(native_fixed_workspace_bytes=int(descriptor.get('fixed_workspace_bytes', 0)),
                fft_resident_estimate_bytes=int(self.screen.resident_estimate),
                projection_noise_workspace_bytes=None,
                clean_projected_observation_bytes=2*self.screen.crop_rows*np.dtype(np.complex128).itemsize,
                clean_helper_prefix_bytes=2*self.helper_prefix_samples*np.dtype(np.complex128).itemsize,
                declared_memory_limit_bytes=self.memory_limit,
                qualification='Peak estimated using receiver-bank/workspace accounting; construction-time allocator peak is sampled only by the invoking process and is not a safety certificate.')
            self.preparation = dict(template_setup_seconds=time.perf_counter()-began,
                receiver_template_preparation_seconds=self.receiver_preparation_seconds,
                current_input_profile_validation_seconds=self.profile_validation_seconds,
                simulator_channel_generation_seconds=None,
                noise_factor_preparation_seconds=None,
                excluded_source='Noise-free channel/noise generation is simulator work; reusable public templates and local fidelity validation are receiver preparation, excluded from sustained service.')
            self.preparation['long_preparation_unavoidable'] = None
            # Templates are now self-contained; release the unused initial
            # native channel child before generating the two clean bit streams.
            self.template_source.close()
            self.streams.remove(self.template_source)
        except BaseException:
            self.close()
            raise

    def _generate_clean(self, bit):
        command = _clean_options(self.descriptor.get('executable', ''), self.arguments,
            bit=bit, seed=int(self.arguments[self.arguments.index('--seed')+1]))
        # Use the already selected canonical binary; the stored template source
        # command is the safest identity source, so take its executable.
        executable = self.executable
        args = command[1:]
        begin = time.perf_counter()
        source = self.native_factory(executable, args, kind='channel')
        self.streams.append(source)
        if (source.descriptor['total_samples'] != self.screen.samples or
                source.descriptor.get('pattern_revision') != self.descriptor.get('pattern_revision') or
                source.descriptor.get('added_amplitude_depth') != self.descriptor.get('added_amplitude_depth')):
            raise ValueError('clean observation differs from the full public screen geometry')
        helper_parts = []
        first = 0
        native_wall = 0.

        def chunks():
            nonlocal first, native_wall
            while first < self.screen.samples:
                count = min(_CHUNK, self.screen.samples-first)
                before = time.perf_counter()
                values, valid = source.next(count)
                native_wall += time.perf_counter()-before
                if len(values) != count or not np.all(valid):
                    raise ValueError('native clean channel stream is incomplete or has invalid capture rows')
                if first < self.helper_prefix_samples:
                    keep = min(count, self.helper_prefix_samples-first)
                    helper_parts.append(np.array(values[:keep], dtype=np.complex128, copy=True))
                first += count
                yield values

        filter_started = time.perf_counter()
        retained = sum(x.nbytes for x in self.observations.values()) + sum(
            x.nbytes for x in self.helper_observations.values())
        self.screen.memory_limit_bytes = self.memory_limit-retained-3*self.helper_prefix_samples*16
        projected = self.screen.prepare_observation_stream(chunks())
        projection_wall = time.perf_counter()-filter_started
        self.native_clean_generation_seconds += native_wall
        self.clean_projection_seconds[bit] = max(0., projection_wall-native_wall)
        helper = np.concatenate(helper_parts) if helper_parts else np.empty(0, np.complex128)
        source.close()
        if source in self.streams:
            self.streams.remove(source)
        return np.asarray(projected, dtype=np.complex128), helper

    def ensure_clean_pair(self):
        """Generate the paired bit-0/bit-1 clean observation gate once."""
        if len(self.observations) == 2 and len(self.helper_observations) == 2:
            return
        if self.observations or self.helper_observations:
            raise RuntimeError('paired clean simulation is incomplete and cannot be resumed with a mixed state')
        estimated_pair_storage = (self.screen.resident_estimate +
            2*self.screen.crop_rows*np.dtype(np.complex128).itemsize +
            2*self.helper_prefix_samples*np.dtype(np.complex128).itemsize +
            int(self.descriptor.get('fixed_workspace_bytes', 0)))
        if estimated_pair_storage > self.memory_limit:
            raise MemoryError('paired full-bit clean observations exceed the declared workspace envelope')
        for bit in (0, 1):
            clean, helper = self._generate_clean(bit)
            self.observations[bit] = clean
            self.helper_observations[bit] = helper
        self.workspace['clean_projected_observation_bytes'] = sum(x.nbytes for x in self.observations.values())
        self.workspace['clean_helper_prefix_bytes'] = sum(x.nbytes for x in self.helper_observations.values())
        if (self.workspace['clean_projected_observation_bytes'] +
                self.workspace['clean_helper_prefix_bytes'] +
                self.screen.resident_estimate > self.memory_limit):
            raise MemoryError('paired full-bit clean observations exceed the declared workspace envelope')
        self.preparation['simulator_channel_generation_seconds'] = self.native_clean_generation_seconds
        self.preparation['clean_projection_cpu_estimate_seconds_per_baseband_second'] = (
            max(self.clean_projection_seconds.values(), default=0.)/self.represented_baseband_seconds)

    def _decide(self, proposals):
        if proposals.get('status') != 'proposed':
            return None, proposals.get('reason') or proposals.get('status') or 'incomplete_screen'
        by_bit = proposals.get('candidates_by_bit')
        if not isinstance(by_bit, (list, tuple)) or len(by_bit) != 2 or not all(by_bit):
            return None, 'empty_public_bit_candidate_set'
        best = []
        for rows in by_bit:
            try:
                scores = [float(row['score']) for row in rows]
            except (KeyError, TypeError, ValueError):
                return None, 'invalid_public_bit_candidate_set'
            if not scores or not all(math.isfinite(score) for score in scores):
                return None, 'invalid_public_bit_candidate_set'
            best.append(max(scores))
        a, b = best
        if math.isclose(a, b, rel_tol=1e-12, abs_tol=1e-12):
            return None, 'tied_best_bit_scores'
        return (0 if a > b else 1), None

    def receive(self, bit, cn0_db_hz=None, noise_seed=1):
        if isinstance(bit, bool) or bit not in (0, 1):
            raise ValueError('evaluator bit must be zero or one')
        if cn0_db_hz is not None and (isinstance(cn0_db_hz, bool) or not isinstance(cn0_db_hz, (int, float)) or not math.isfinite(cn0_db_hz)):
            raise ValueError('C/N0 must be finite or None for the noiseless gate')
        self.ensure_clean_pair()
        helper_status = 'helper_disabled'
        helper_frequency = None
        unit_noise = np.zeros(self.screen.crop_rows, dtype=np.complex128)
        helper_noise = np.zeros(self.helper_prefix_samples, dtype=np.complex128)
        noise_storage = 0
        if cn0_db_hz is not None:
            if self._noise is None:
                noise_factor_begin = time.perf_counter()
                remaining = (self.memory_limit-self.screen.resident_estimate-
                    sum(x.nbytes for x in self.observations.values())-
                    sum(x.nbytes for x in self.helper_observations.values())-
                    2*(self.screen.crop_rows+self.helper_prefix_samples)*16)
                allowed_work, admission = calibrated_noise_limit(self.screen.projection_receipt(),
                    self.screen.taps, mapped_step=self.coverage.get('mapped_template_sampling_step', 1),
                    helper_prefix_samples=self.helper_prefix_samples, memory_limit_bytes=remaining)
                self.preparation['noise_cost_admission'] = admission
                if admission['status'] == 'cost_limit':
                    raise ValueError('current S3 projected-noise preparation/draw forecast exceeds bounded cost allowance')
                self._noise = ProjectedNoise(self.screen.projection_receipt(), self.screen.taps,
                    helper_prefix_samples=self.helper_prefix_samples,
                    memory_limit_bytes=remaining, maximum_factor_work=allowed_work)
                self._noise_factor_seconds = time.perf_counter()-noise_factor_begin
                self.preparation['noise_boundary_preparation'] = dict(
                    method=getattr(self._noise, 'boundary_method', None),
                    selected_work_estimate=getattr(self._noise, 'boundary_work_estimate', None),
                    direct_dot_work_estimate=getattr(self._noise, 'boundary_direct_dot_work_estimate', None),
                    cumulative_lags=getattr(self._noise, 'boundary_cumulative_lags', None),
                    scope='Exact finite covariance preparation; Helper/DATA raw-prefix coupling is retained.')
            draw = self._noise.draw(noise_seed)
            unit_noise = draw['projected_noise']
            helper_noise = draw['helper_raw_noise']
            noise_storage = self._noise.owned_workspace_bytes
            sigma = math.sqrt(self.fs*10**(-float(cn0_db_hz)/10))
            scale = 1/sigma
            data = self.observations[bit]*scale + unit_noise
            helper = self.helper_observations[bit]*scale + helper_noise
            self.workspace['projection_noise_workspace_bytes'] = noise_storage
            self.preparation['noise_factor_preparation_seconds'] = self._noise_factor_seconds
        else:
            data, helper = self.observations[bit], self.helper_observations[bit]
            scale = None
        simulator_live = (noise_storage + sum(x.nbytes for x in self.observations.values()) +
            sum(x.nbytes for x in self.helper_observations.values()) + len(data)*16 + len(helper)*16)
        self.screen.memory_limit_bytes = self.memory_limit-simulator_live
        helper_workspace = helper_workspace_estimate(len(helper), self.fs)
        helper_peak = self.screen.resident_estimate+helper_workspace
        if helper_peak > self.screen.memory_limit_bytes:
            raise MemoryError('Helper transient workspace estimate exceeds remaining memory budget')
        # Native generation, random-noise draws, and building the noisy sample
        # arrays are simulator work; receiver service begins with Helper/FFT.
        measurement_wall, measurement_cpu = time.perf_counter(), time.process_time()
        helper_reason = 'helper_disabled'
        if self.descriptor['helper_enabled']:
            helper_manifest = copy.deepcopy(self.manifest)
            helper_manifest['calendar']['capture_samples'] = len(helper)
            helper_manifest['power']['complex_noise_variance'] = 1.
            bank_size = max(1, 2*math.ceil(self.grid.frequency_bound/.5)+1)
            frequencies = np.linspace(-self.grid.frequency_bound, self.grid.frequency_bound, bank_size)
            helper_result = estimate_weak_helper(helper, helper_manifest, frequencies,
                config={'frequency_config': {'selection_geometry': 'continuous_interval',
                    'future_horizon_seconds': self.represented_baseband_seconds}})
            helper_status = helper_result.get('status', 'fallback')
            helper_reason = helper_result.get('reason')
            frequency = helper_result.get('frequency') or {}
            helper_frequency = dict(status=frequency.get('status'), reason=frequency.get('reason'),
                expanded_intervals_hz=frequency.get('expanded_intervals_hz', []),
                selection_geometry=frequency.get('selection_geometry'),
                retained_fraction=frequency.get('retained_fraction'),
                aperture_times=[dict(onset_cell_seconds=branch['onset_cell_seconds'],
                    available_at=branch['frequency'].get('available_at'),
                    applicable_horizon=branch['frequency'].get('applicable_horizon'))
                    for branch in helper_result.get('aperture_conditioning', {}).get('branches', [])],
                time_scope='Spectral aperture availability and frequency applicability, not whole Helper readiness.')
            intervals = (frequency['expanded_intervals_hz'] if frequency.get('status') == 'narrowed'
                else [[-self.grid.frequency_bound, self.grid.frequency_bound]])
        else:
            intervals = [[-self.grid.frequency_bound, self.grid.frequency_bound]]
        proposal = self.screen.score_projected(data, frequency_intervals=intervals,
            projection_receipt=self.screen.projection_receipt(), top_per_bit=2)
        decision, reason = self._decide(proposal)
        elapsed = time.perf_counter()-measurement_wall
        process_cpu = time.process_time()-measurement_cpu
        utilization = process_cpu/(elapsed*self.threads) if elapsed else None
        valid_decision = decision in (0, 1)
        score_peak = proposal.get('work', {}).get('estimated_peak_bytes', self.screen.resident_estimate)
        total_memory = max(score_peak, helper_peak)+simulator_live
        return dict(schema='triad-coarse-sensitivity-trial-v1',
            conditional_on='One canonical native transmission and one sampled public channel realization.',
            endpoint='Full finite DATA bit projected through the same public 13-epoch/arrival/clock FFT screen; coarse correct-bit choice only.',
            cn0_db_hz=cn0_db_hz, source_bit=bit, decision_bit=decision,
            correct_bit=bool(valid_decision and decision == bit),
            success=bool(valid_decision and decision == bit),
            status='ok' if valid_decision else 'no_decision', reason=reason,
            helper_status=helper_status, helper_reason=helper_reason, helper_frequency=helper_frequency,
            candidate_scores=proposal.get('candidates_by_bit'),
            candidate_status=proposal.get('status'), candidate_reason=proposal.get('reason'),
            full_public_frequency_points=len(proposal.get('frequencies_hz', [])),
            searched_frequency_intervals_hz=intervals, search_work=proposal.get('work'),
            raw_noise_seed=noise_seed if cn0_db_hz is not None else None,
            raw_noise_scope='One proper-complex finite raw variance-one Gaussian draw shared with Helper; its DATA projection uses the exact screen FIR/noise geometry.' if cn0_db_hz is not None else 'No noise; deterministic paired-bit gate.',
            receiver_service_seconds_per_baseband_second=elapsed/self.represented_baseband_seconds,
            ratio_processing_seconds_per_baseband_second=elapsed/self.represented_baseband_seconds,
            represented_baseband_seconds=self.represented_baseband_seconds,
            observed_cpu_utilization_fraction=utilization,
            native_child_cpu_in_utilization=False,
            utilization_scope='Python/numerical receiver service; channel generation and native template preparation are excluded, with no native child active during scoring.',
            whole_screen_frequency_covered=proposal.get('status') == 'proposed',
            workspace_bytes=int(total_memory),
            helper_transient_workspace_estimate_bytes=helper_workspace,
            data_scoring_workspace_estimate_bytes=score_peak,
            retained_simulator_bytes=simulator_live,
            raw_projection_included_in_receiver_ratio=False,
            preparation_scope='Native channel generation, clean projection, mapped template generation and noise-factor construction excluded; Helper estimator and FFT scoring included in receiver service.')

    def close(self):
        streams, self.streams = getattr(self, 'streams', []), []
        for stream in streams:
            try:
                stream.close()
            except Exception:
                pass
        self.observations = {}
        self.helper_observations = {}
        self._noise = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def prepare_replay(executable, native_args, *, memory_limit_bytes, threads=1,
                   seed=73019, mapped_step=None):
    """Prepare (but do not execute) the bounded two-bit public replay."""
    return PreparedReplay(executable, native_args, memory_limit_bytes=memory_limit_bytes,
        threads=threads, seed=seed, mapped_step=mapped_step)
