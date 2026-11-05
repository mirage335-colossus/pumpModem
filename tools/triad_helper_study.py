#!/usr/bin/env python3
"""Actual sampled Helper-to-three-carrier acquisition comparison.

The receiver consumes public manifests, keyed templates and noisy I/Q only.
Evaluator truth is read by the CLI only after both receiver outcomes exist.
Empirical admission must calibrate this entire adaptive procedure, not each
selected frequency grid separately.  No operational admission claim is made.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import time

import numpy as np

from triad_helper_assistance import estimate_frequency, tone_offsets
from triad_helper_receiver import HelperDataReceiver, data_fir_taps


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def public_geometry(m):
    """Exclude realization, on/off controls, timing costs and file locations."""
    return {"rate": m['sample_rate_hz'], "format": {k: v for k, v in m['format'].items()
             if k != 'requested_bit_seconds'}, "calendar": m['calendar'],
            "helper_plan": m['helper_plan'], "channel": m['channel'],
            "key_seed": m['key_seed'], "epoch": m['public_epoch_seconds'],
            "data_gain": m['power']['template_data_gain']}


def frequency_grid(bound, duration, *, max_points=4095):
    if not math.isfinite(bound) or bound <= 0 or not math.isfinite(duration) or duration <= 0:
        raise ValueError('positive finite public frequency bound/duration required')
    half = max(1, math.ceil(bound * 4 * duration))
    if max_points < 3 or 2 * half + 1 > max_points:
        raise ValueError('frequency bank exceeds bounded study work')
    return np.linspace(-bound, bound, 2 * half + 1)


def _inclusive_grid(bound, count, *, center=0.):
    """Public endpoint-inclusive nuisance grid; count=1 denotes a fixed value."""
    if count == 1:
        return np.asarray([center], dtype=np.float64)
    return np.linspace(center - bound, center + bound, count)


def public_clock_grids(manifest, clock_step_ppm=None, doppler_scale_count=1):
    """Construct bounded, endpoint-inclusive grids from public oscillator bounds."""
    step = 0. if clock_step_ppm is None else float(clock_step_ppm)
    if not math.isfinite(step) or step < 0:
        raise ValueError('--clock-step-ppm must be finite and nonnegative')
    if doppler_scale_count not in (1, 3, 5, 7):
        raise ValueError('--doppler-scale-count must be 1, 3, 5 or 7')
    channel = manifest['channel']
    margin = float(channel.get('spread_margin', 1.))
    pair = float(channel.get('sample_clock_pair_ppm',
                            channel.get('relative_pair_ppm_bound', 0.)))
    endpoint = float(channel.get('endpoint_uniform_half_bound_ppm',
                                .5 * channel.get('relative_pair_ppm_bound', 0.)))
    if (not all(math.isfinite(x) for x in (margin, pair, endpoint)) or
            margin <= 0 or pair < 0 or not 0 <= endpoint < 1e6):
        raise ValueError('invalid public oscillator bounds')
    count = 1 if step == 0 or pair == 0 else max(2, math.ceil(2 * pair * margin / step) + 1)
    if count > 4096:
        raise ValueError('public clock bank exceeds 4096-point study work cap')
    clocks = ((0.,) if count == 1 else _inclusive_grid(pair * margin, count))
    rate_bound = endpoint / (1 - endpoint * 1e-6) * margin
    scales = ((1.,) if doppler_scale_count == 1 or rate_bound == 0 else
              _inclusive_grid(rate_bound * 1e-6, doppler_scale_count, center=1.))
    return clocks, scales


def _bank_subset(bank, intervals):
    """Keep every public hypothesis inside intervals and one neighbor per edge."""
    bank = np.asarray(bank, dtype=np.float64)
    keep = np.zeros(len(bank), dtype=bool)
    order = np.argsort(bank)
    ordered = bank[order]
    for low, high in intervals:
        if low > high:
            raise ValueError('public selection interval must be increasing')
        first = int(np.searchsorted(ordered, low, side='left'))
        past = int(np.searchsorted(ordered, high, side='right'))
        keep[order[max(0, first - 1):min(len(bank), past + 1)]] = True
    return bank[keep]


def _frontend_support(manifest, profile, clock_ppm_values, doppler_scale_values):
    """Reject a nominal public DATA envelope outside the actual FIR passband.

    Source/channel spectral tails remain present. This check only establishes
    that the declared nominal band, selected line offsets and acquisition bank
    fit the chosen frontend; it is not a compact-support or sensitivity bound.
    """
    if profile not in ('legacy', 'wide'):
        raise ValueError('unknown DATA frontend profile')
    ch = manifest['channel']
    bandwidth = float(manifest['format']['occupied_bandwidth_hz'])
    margin = float(ch.get('spread_margin', 1.))
    cfo = float(ch['deterministic_cfo_bound_hz']) * margin
    pair_ppm = float(ch.get('sample_clock_pair_ppm',
                          ch.get('base_relative_pair_ppm_bound',
                                 ch.get('relative_pair_ppm_bound', 0.)))) * margin
    endpoint_default = pair_ppm / (2 * margin) if margin > 0 else 0.
    endpoint_ppm = float(ch.get('endpoint_uniform_half_bound_ppm', endpoint_default))
    numbers = (bandwidth, margin, cfo, pair_ppm, endpoint_ppm,
               *clock_ppm_values, *doppler_scale_values)
    if (not all(math.isfinite(value) for value in numbers) or bandwidth <= 0 or margin <= 0 or
            cfo < 0 or pair_ppm < 0 or not 0 <= endpoint_ppm < 1e6 or
            not clock_ppm_values or not doppler_scale_values):
        raise ValueError('finite positive public DATA frontend geometry required')
    source_scale = 1 + max(pair_ppm, max(clock_ppm_values)) * 1e-6
    channel_rate_bound_ppm = endpoint_ppm / (1 - endpoint_ppm * 1e-6) * margin
    channel_scale = max(1 + channel_rate_bound_ppm * 1e-6, max(doppler_scale_values))
    if min(doppler_scale_values) <= 0 or source_scale <= 0:
        raise ValueError('positive source and channel time scales required')
    offsets = tone_offsets(ch['spread_doppler_hz'], ch['selective_slow_depth'],
                           ch['selective_fast_depth'],
                           modulation_rate_hz=ch.get('selective_modulation_rate_hz'))
    half_source = bandwidth * .5 * source_scale
    line_edge = float(np.max(np.abs(offsets))) * channel_scale
    edge = half_source + line_edge + cfo
    pass_edge = 330. if profile == 'legacy' else 400.
    if edge > pass_edge:
        raise ValueError(
            f'unsupported DATA frontend geometry: public nominal band + channel lines + CFO '
            f'requires {edge:.6g} Hz, beyond {profile} pass edge {pass_edge:g} Hz; '
            'use a supported frontend/workload, not a sensitivity-failure classification')
    return dict(profile=profile, pass_edge_hz=pass_edge,
                nominal_source_half_width_hz=bandwidth * .5,
                maximum_source_clock_scale=source_scale,
                maximum_channel_time_scale=channel_scale,
                maximum_selected_line_offset_hz=float(np.max(np.abs(offsets))),
                cfo_half_bound_hz=cfo, required_nominal_edge_hz=edge,
                scope='Nominal source band and selected line/acquisition envelope only; finite spectral tails remain emitted.',
                compact_spectral_support=False, receiver_sensitivity_qualification=False)


class HelperComparison:
    def __init__(self, fixture_directory, template_directory=None, *, device='cpu',
                 precision='fp32', memory_limit_bytes=2 * 1024**3,
                 fft_backend='legacy', frontend_profile='legacy',
                 clock_ppm_values=(0.,), doppler_scale_values=(1.,),
                 frequency_batch_size=8, max_cached_hypotheses=1,
                 aligned_fft_grid=False, lag_step=1):
        start = time.perf_counter()
        folder = Path(fixture_directory)
        tfolder = Path(template_directory or folder)
        self.manifest = json.loads((folder / 'manifest.json').read_text())
        tm = json.loads((tfolder / 'manifest.json').read_text())
        m = self.manifest
        if m['schema'] != 'triad-helper-public-v1' or tm['schema'] != m['schema']:
            raise ValueError('unsupported native composite manifest')
        if m['format']['physical_rf_retuning']:
            raise ValueError('physical RF retuning is not qualified in this comparison')
        # Public templates are independent of the selected channel/noise. Their
        # source geometry and fixed calibration must match exactly.
        for key in ('format', 'key_seed', 'epoch', 'data_gain'):
            if public_geometry(m)[key] != public_geometry(tm)[key]:
                raise ValueError(f'template source mismatch: {key}')
        if not tm['templates']['enabled']:
            raise ValueError('explicit complete public source templates required')
        self.clock_ppm_values = tuple(map(float, clock_ppm_values))
        self.doppler_scale_values = tuple(map(float, doppler_scale_values))
        self.frontend_support = _frontend_support(
            m, frontend_profile, self.clock_ppm_values, self.doppler_scale_values)
        shape = tuple(tm['templates']['shape'])
        path = tfolder / tm['templates']['file']
        if path.stat().st_size != math.prod(shape) * 16:
            raise ValueError('public source template file size mismatch')
        self.template_hash = digest(path)
        templates = np.memmap(path, dtype='<c16', mode='r', shape=shape)
        self.original_bound = m['channel']['deterministic_cfo_bound_hz'] * m['channel']['spread_margin']
        max_points = 4095 if fft_backend == 'legacy' else 1_000_000
        self.frequencies = frequency_grid(self.original_bound, m['format']['bit_seconds'],
                                           max_points=max_points)
        c = m['calendar']
        capture_validity = np.memmap(folder / m['validity']['file'], dtype='u1', mode='r')
        if capture_validity.shape != (c['capture_samples'],) or np.any(capture_validity > 1):
            raise ValueError('binary capture validity must match capture size')
        self.capture_validity = np.array(capture_validity, copy=True)
        self.capture_validity.flags.writeable = False
        self.receiver = HelperDataReceiver(
            templates, tm['templates']['epoch_offsets_seconds'],
            source_offset_seconds=c['data_sample_offset'] / m['sample_rate_hz'],
            capture_lead_seconds=c['capture_lead_seconds'], capture_rows=c['capture_samples'],
            arrival_bounds_seconds=c['arrival_prior_seconds'],
            delay_seconds=m['channel']['spread_delay_ms'] * .001,
            doppler_hz=m['channel']['spread_doppler_hz'],
            source_half_bandwidth_hz=self.frontend_support['nominal_source_half_width_hz'],
            channel_line_half_width_hz=self.frontend_support['maximum_selected_line_offset_hz'],
            reference_variance=m['power']['complex_noise_variance'] * float(np.sum(data_fir_taps(frontend_profile)**2)),
            frequency_offsets=self.frequencies, lag_step=lag_step, precision=precision, device=device,
            fft_backend=fft_backend, frontend_profile=frontend_profile,
            clock_ppm_values=self.clock_ppm_values, doppler_scale_values=self.doppler_scale_values,
            frequency_batch_size=frequency_batch_size,
            max_cached_hypotheses=max_cached_hypotheses,
            memory_limit_bytes=memory_limit_bytes, capture_validity=self.capture_validity)
        if aligned_fft_grid:
            if fft_backend != 'batched':
                raise ValueError('aligned FFT lattice requires --fft-backend batched')
            self.frequencies = np.asarray(
                self.receiver.aligned_frequency_offsets(self.original_bound), dtype=np.float64)
            self.receiver = self.receiver.with_hypotheses(frequency_offsets=self.frequencies)
        self.fft_backend = fft_backend
        self.sources = {name: digest(Path(__file__).parent / name) for name in (
            'triad_helper_study.py', 'triad_helper_assistance.py', 'triad_helper_receiver.py')}
        identity = dict(geometry=public_geometry(m), template_sha256=self.template_hash,
                        raw_noise_variance=m['power']['complex_noise_variance'],
                        receiver_id=self.receiver.receiver_id, source_hashes=self.sources,
                        policy='rectangular-comb-public-36s-window-floor-.01Hz-v3-known-mask',
                        validity_sha256=hashlib.sha256(self.capture_validity.tobytes()).hexdigest(),
                        fft_backend=fft_backend, frontend_profile=frontend_profile,
                        frontend_support=self.frontend_support,
                        clock_ppm_values=self.clock_ppm_values,
                        doppler_scale_values=self.doppler_scale_values,
                        frequency_batch_size=frequency_batch_size,
                        max_cached_hypotheses=max_cached_hypotheses,
                        aligned_fft_grid=bool(aligned_fft_grid))
        self.procedure_id = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
        self.shared_capture_id = hashlib.sha256(json.dumps(dict(
            geometry=public_geometry(m), raw_noise_variance=m['power']['complex_noise_variance'],
            preprocessing_id=self.receiver.preprocessing_id,
            frequencies=self.frequencies.tolist(), clocks=self.clock_ppm_values,
            scales=self.doppler_scale_values, source_hashes=self.sources,
            validity_sha256=identity['validity_sha256']), sort_keys=True).encode()).hexdigest()
        self.preparation_seconds = time.perf_counter() - start

    def score(self, directory, *, include_baseline=True):
        return self.score_capture(self.prepare_capture(directory),
                                  include_baseline=include_baseline)

    def prepare_capture(self, directory):
        """Share Helper estimation and linear DATA preprocessing across lag policies."""
        start = time.perf_counter()
        directory = Path(directory)
        m = json.loads((directory / 'manifest.json').read_text())
        if public_geometry(m) != public_geometry(self.manifest):
            raise ValueError('capture geometry changed; prepare a new public comparison')
        if m['power']['complex_noise_variance'] != self.manifest['power']['complex_noise_variance']:
            raise ValueError('noise reference changed; new calibration required')
        shape = tuple(m['observation']['shape'])
        path = directory / m['observation']['file']
        if path.stat().st_size != math.prod(shape) * 16:
            raise ValueError('capture file size mismatch')
        raw = np.memmap(path, dtype='<c16', mode='r', shape=shape)
        valid = np.memmap(directory / m['validity']['file'], dtype='u1', mode='r')
        if valid.shape != raw.shape or not np.array_equal(valid, self.capture_validity):
            raise ValueError('capture validity differs from fixed mask; new calibration required')
        fs = m['sample_rate_hz']
        c, h, ch = m['calendar'], m['helper_plan'], m['channel']
        margin = float(ch.get('spread_margin', 1.))
        audio_bound = float(ch.get('sample_clock_pair_ppm',
                                   ch.get('base_relative_pair_ppm_bound',
                                          ch['relative_pair_ppm_bound']))) * margin
        endpoint_audio = float(ch.get('endpoint_uniform_half_bound_ppm',
                                      .5 * ch['relative_pair_ppm_bound']))
        channel_rate_bound = endpoint_audio / (1 - endpoint_audio * 1e-6) * margin
        # Fixed public window: every allowed source has started. Its delayed
        # availability is explicitly reported, never relabeled a 36-s frontier.
        begin_seconds = c['capture_lead_seconds'] + c['arrival_prior_seconds'][1] + .4
        first, rows = round(begin_seconds * fs), round(36 * fs)
        earliest_end = c['capture_lead_seconds'] + c['helper_end_seconds'] + c['arrival_prior_seconds'][0]
        if first + rows > len(raw) or (first + rows) / fs >= earliest_end - .02:
            raise ValueError('public timing uncertainty leaves insufficient guaranteed Helper support')
        if np.any(valid[first:first + rows] != 1):
            assistance = dict(status='fallback', reason='known erasure in Helper observation',
                selected_frequencies=self.frequencies.tolist(), selected_count=len(self.frequencies),
                original_count=len(self.frequencies), expanded_intervals_hz=[],
                available_at=first / fs + 36 - c['capture_lead_seconds'], observation_seconds=36.,
                wall_seconds=0., clock_status='fallback', presence_is_formal_pilot_lock=False)
        else:
            assistance = estimate_frequency(raw[first:first + rows], sample_rate=fs,
                data_if_hz=ch['nominal_conversion_if_hz'], low_pilot_hz=h['low_hz'],
                high_pilot_hz=h['high_hz'], original_frequencies=self.frequencies,
                prior_half_width_hz=self.original_bound,
                doppler_hz=ch['spread_doppler_hz'], slow_depth=ch['selective_slow_depth'],
                fast_depth=ch['selective_fast_depth'],
                modulation_rate_hz=ch.get('selective_modulation_rate_hz'),
                noise_variance=m['power']['complex_noise_variance'],
                first_sample_seconds=first / fs - c['capture_lead_seconds'],
                phase_diffusion_radians_per_sqrt_second=ch['relative_phase_diffusion_rad_sqrt_s'],
                audio_error_bound_ppm=audio_bound,
                channel_rate_error_bound_ppm=channel_rate_bound,
                pilot_dwell_gating=h['pilot_dwell_gating'])
        prepared = self.receiver.prepare_observation(raw, valid=valid)
        return dict(shared_capture_id=self.shared_capture_id, prepared=prepared,
                    assistance=assistance, input_sha256=digest(path),
                    shared_preparation_seconds=time.perf_counter() - start)

    def score_capture(self, capture, *, include_baseline=True):
        """Score previously prepared data only under the identical public policy."""
        start = time.perf_counter()
        if capture['shared_capture_id'] != self.shared_capture_id:
            raise ValueError('shared capture policy differs')
        prepared, assistance = capture['prepared'], capture['assistance']
        narrowed = assistance['selected_frequencies']
        selected_clocks = self.clock_ppm_values
        clock_intervals = assistance.get('expanded_clock_intervals_ppm')
        if assistance.get('clock_status') == 'narrowed' and clock_intervals:
            selected_clocks = tuple(_bank_subset(self.clock_ppm_values, clock_intervals))
        same_banks = (np.array_equal(narrowed, self.frequencies) and
                      selected_clocks == self.clock_ppm_values)
        baseline = self.receiver.score_prepared(prepared) if include_baseline else None
        if same_banks:
            if include_baseline:
                assisted = dict(baseline)
                assisted['score_reused_from_baseline'] = True
                added_scoring_seconds = 0.
            else:
                # A Helper fallback can select the full bank. In an assisted-only
                # run, score that bank once as the assisted statistic; never
                # execute or imply the unrequested baseline arm.
                mark = time.perf_counter()
                assisted = self.receiver.score_prepared(prepared)
                added_scoring_seconds = time.perf_counter() - mark
                assisted['score_reused_from_baseline'] = False
        else:
            mark = time.perf_counter()
            if self.fft_backend == 'batched':
                variant = self.receiver.with_hypotheses(frequency_offsets=narrowed,
                                                        clock_ppm_values=selected_clocks)
            elif list(narrowed) != list(self.frequencies):
                variant = self.receiver.with_frequency_offsets(narrowed)
            else:
                # Legacy workspaces have no clock dimension; a non-singleton
                # clock bank is rejected by their constructor.
                variant = self.receiver
            assisted = variant.score_prepared(prepared)
            added_scoring_seconds = time.perf_counter() - mark
            assisted['score_reused_from_baseline'] = False
        executed_arms = ['baseline', 'assisted'] if include_baseline else ['assisted']
        execution_policy_id = hashlib.sha256(json.dumps(dict(
            procedure_id=self.procedure_id, executed_arms=executed_arms),
            sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        # Stable identity is the entire adaptive procedure, including its fixed
        # selection rule; individual bank identities remain in each outcome.
        timing = dict(public_preparation_seconds=self.preparation_seconds,
            helper_seconds=assistance['wall_seconds'],
            additional_assisted_scoring_seconds=(added_scoring_seconds if include_baseline else None),
            shared_preparation_seconds=capture['shared_preparation_seconds'],
            score_only_seconds=time.perf_counter() - start,
            command_score_seconds=time.perf_counter() - start + capture['shared_preparation_seconds'])
        if not include_baseline:
            timing['assisted_score_seconds'] = added_scoring_seconds
        return dict(schema='triad-helper-comparison-v1', procedure_id=self.procedure_id,
            execution_policy_id=execution_policy_id, executed_arms=executed_arms,
            input_sha256=capture['input_sha256'], template_sha256=self.template_hash,
            source_hashes=self.sources, assistance=assistance,
            frontend_support=self.frontend_support,
            baseline=baseline, assisted=assisted,
            search_banks=dict(original_frequencies_hz=self.frequencies.tolist(),
                selected_frequencies_hz=list(narrowed),
                original_clock_ppm=list(self.clock_ppm_values),
                selected_clock_ppm=list(selected_clocks),
                doppler_scale_values=self.doppler_scale_values),
            timing=timing,
            limits=['Entire adaptive procedure needs independent Helper-present/DATA-absent calibration.',
                    'Static two-ray DATA receiver omits additional selective-AM structure.',
                    'Default arrival and thirteen epoch banks retained; fractional timing/clock not exhaustive.',
                    'No operational daily false-admission, radio-retune or phase-sensitivity qualification.'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fixture', required=True, type=Path)
    p.add_argument('--templates', type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--device', default='cpu')
    p.add_argument('--precision', choices=('fp32', 'fp64'), default='fp32')
    p.add_argument('--fft-backend', choices=('legacy', 'batched'), default='legacy')
    p.add_argument('--frontend-profile', choices=('legacy', 'wide'), default='legacy')
    p.add_argument('--clock-step-ppm', type=float, default=0.)
    p.add_argument('--lag-step', type=int, default=1, help='positive 900-Hz timing stride; 4 reproduces the historical coarse grid')
    p.add_argument('--doppler-scale-count', type=int, choices=(1, 3, 5, 7), default=1)
    p.add_argument('--frequency-batch-size', type=int, default=8)
    p.add_argument('--max-cached-hypotheses', type=int, default=1,
                   help='bound the resident clock/rate/epoch workspace LRU')
    p.add_argument('--aligned-fft-grid', action='store_true',
                   help='use the batched receiver exact resident-FFT CFO-shift lattice')
    p.add_argument('--assisted-only', action='store_true',
                   help='score only the Helper-selected arm; default also scores the full-bank baseline')
    p.add_argument('--memory-limit-bytes', type=int, default=2 * 1024**3)
    p.add_argument('--evaluate', action='store_true')
    args = p.parse_args()
    if args.output.exists():
        p.error('output already exists')
    manifest = json.loads((args.fixture / 'manifest.json').read_text())
    try:
        clock_values, scales = public_clock_grids(manifest, args.clock_step_ppm, args.doppler_scale_count)
        if args.lag_step < 1:
            raise ValueError('--lag-step must be positive')
    except ValueError as error:
        p.error(str(error))
    if args.fft_backend == 'legacy' and (len(clock_values) > 1 or len(scales) > 1):
        p.error('clock/rate grids require --fft-backend batched')
    study = HelperComparison(args.fixture, args.templates, device=args.device, precision=args.precision,
        memory_limit_bytes=args.memory_limit_bytes, fft_backend=args.fft_backend,
        frontend_profile=args.frontend_profile, clock_ppm_values=clock_values,
        doppler_scale_values=scales, frequency_batch_size=args.frequency_batch_size,
        max_cached_hypotheses=args.max_cached_hypotheses,
        aligned_fft_grid=args.aligned_fft_grid, lag_step=args.lag_step)
    result = study.score(args.fixture, include_baseline=not args.assisted_only)
    if args.evaluate:
        result['evaluator_truth'] = json.loads((args.fixture / 'evaluator-truth.json').read_text())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
    print(json.dumps({'output': str(args.output), 'timing': result['timing'],
                      'assistance': result['assistance']['status']}))


if __name__ == '__main__':
    main()
