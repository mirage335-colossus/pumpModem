#!/usr/bin/env python3
"""Engineering spectral Helper frequency assistance, independent of pilot phase.

One public 36-second Hann periodogram supplies a working Whittle statistic.
Gaussian ray cores have independent nonnegative powers and a fitted background
for each pilot. Periodogram dependence, finite-window leakage, random selective
modulation and model selection are NOT a calibrated confidence calculation.
The complete adaptive receiver needs independent Helper-present null calibration.
No phase correction, clock narrowing, channel truth or legacy line fit is used.
"""
from __future__ import annotations

import math
import time

import numpy as np
from scipy import fft

_DEFAULT = dict(observation_seconds=36., edge_guard_seconds=.02,
    competitive_score_drop=18., minimum_presence_score=18.,
    minimum_peak_background_ratio=2., maximum_retained_fraction=.8,
    profile_iterations=28, maximum_profile_points=16385, profile_batch_size=128,
    maximum_fit_bins=300000, maximum_fft_samples=8000000,
    ray_width_allowance=3., modulation_width_allowance=1.,
    model_allowance_hz=.02, drift_bound_hz_per_second=0.,
    data_frequency_aperture_seconds=.25, oscillator_tail_probability=.001,
    future_horizon_seconds=None, pilot_use='both', require_data_free=False,
    selection_geometry='discrete_bank')


def frequency_policy(config=None):
    """Stable public policy; changing any value requires new empirical validation."""
    if config is not None and not isinstance(config, dict):
        raise ValueError('frequency config must be a dictionary')
    unknown = set(config or ())-set(_DEFAULT)
    if unknown:
        raise ValueError('unknown frequency configuration: '+', '.join(sorted(unknown)))
    p = dict(_DEFAULT)
    p.update(config or {})
    for name, value in p.items():
        if name == 'require_data_free':
            if not isinstance(value, bool):
                raise ValueError('require_data_free must be boolean')
        elif name == 'pilot_use':
            if value not in ('both', 'low', 'high'):
                raise ValueError('pilot_use must be both, low or high')
        elif name == 'selection_geometry':
            if value not in ('discrete_bank', 'continuous_interval'):
                raise ValueError('selection_geometry must be discrete_bank or continuous_interval')
        elif name == 'future_horizon_seconds' and value is None:
            continue
        elif isinstance(value, (bool, np.bool_)) or not isinstance(value, (int, float, np.number)) or not math.isfinite(value) or value < 0:
            raise ValueError(name+' must be finite and nonnegative')
    for name in ('profile_iterations', 'maximum_profile_points', 'profile_batch_size', 'maximum_fit_bins', 'maximum_fft_samples'):
        if int(p[name]) != p[name] or p[name] < 1:
            raise ValueError(name+' must be a positive integer')
        p[name] = int(p[name])
    if p['observation_seconds'] < 36 or p['data_frequency_aperture_seconds'] <= 0:
        raise ValueError('at least 36 s observation and positive future frequency aperture required')
    if not 0 < p['maximum_retained_fraction'] < 1 or not 0 < p['oscillator_tail_probability'] < 1:
        raise ValueError('retained fraction and oscillator tail probability must lie in (0,1)')
    return dict(schema='triad-dynamic-frequency-policy-v1', **p,
        statistic='Hann periodogram working Whittle profile; independent nonnegative Gaussian ray powers and background per pilot',
        interval_kind='engineering competitive regions plus declared allowances; not calibrated confidence',
        clock_narrowing=False, phase_correction=False,
        drift_scope='Default zero drift matches the native exporter constant-frequency oscillator model; configure nonzero drift for other sources.',
        future_scope='Wiener average frequency over declared finite apertures, union allowance over future horizon; no instantaneous-frequency bound.',
        calibration='Freeze and calibrate complete selection/fallback procedure, including one-pilot use, on independent matching captures.')


def _number(value, name, minimum=0.):
    if isinstance(value, (bool, np.bool_)):
        raise ValueError(name+' must be numeric')
    result = float(value)
    if not math.isfinite(result) or result < minimum:
        raise ValueError(name+' is outside its finite range')
    return result


def _regions(grid, scores, drop):
    keep = scores >= np.max(scores)-drop
    starts = np.flatnonzero(keep & np.r_[True, ~keep[:-1]])
    ends = np.flatnonzero(keep & np.r_[~keep[1:], True])
    return [(float(grid[a]), float(grid[b])) for a, b in zip(starts, ends)]


def _merge(intervals):
    result = []
    for lo, hi in sorted(intervals):
        if result and lo <= result[-1][1]:
            result[-1][1] = max(result[-1][1], hi)
        else:
            result.append([float(lo), float(hi)])
    return result


def _subset(bank, intervals):
    order = np.argsort(bank)
    ordered = bank[order]
    keep = np.zeros(len(bank), bool)
    for lo, hi in intervals:
        a, b = np.searchsorted(ordered, [lo, hi], side='left')
        b = np.searchsorted(ordered, hi, side='right')
        keep[order[max(0, a-1):min(len(bank), b+1)]] = True
    return bank[keep]


def _continuous_intervals(bank, intervals):
    """Clip/merge interval union to a nonzero continuous prior and measure it."""
    prior_low, prior_high = float(bank.min()), float(bank.max())
    prior_width = prior_high-prior_low
    if prior_width <= 0:
        return [], None
    clipped = _merge([(max(prior_low, lo), min(prior_high, hi))
        for lo, hi in intervals if max(prior_low, lo) <= min(prior_high, hi)])
    return clipped, sum(hi-lo for lo, hi in clipped)/prior_width


def _whittle_profile(power, frequencies, grid, separation, sigma, policy):
    """Fixed-window score with exact background contribution outside local cores.

    Six-sigma Gaussian truncation bounds preparation work; all fixed-window bins
    still contribute to background fitting and the objective. No data-dependent
    bins or hypothesis-dependent discarded observations are used.
    """
    n, total = len(power), float(power.sum())
    mean = max(total/n, np.finfo(float).tiny)
    observed = power/mean
    total /= mean
    df = float(frequencies[1]-frequencies[0])
    radius = int(math.ceil(6*sigma/df))+1
    offsets = np.unique(np.r_[np.arange(-radius, radius+1),
        np.arange(round(separation/df)-radius, round(separation/df)+radius+1)])
    scores, peaks = np.empty(len(grid)), np.empty(len(grid))
    floor = 1e-6
    null_cost = n  # sum(log(mean-normalized variance=1) + I)
    for first in range(0, len(grid), policy['profile_batch_size']):
        g = grid[first:first+policy['profile_batch_size']]
        centers = np.rint((g-frequencies[0])/df).astype(np.int64)
        indices = centers[:, None]+offsets[None, :]
        if np.any(indices < 0) or np.any(indices >= n):
            raise ValueError('fixed fitting window does not contain every Gaussian core')
        x = frequencies[indices]-g[:, None]
        kernels = np.stack((np.exp(-.5*(x/sigma)**2),
                            np.exp(-.5*((x-separation)/sigma)**2)), axis=-1)
        y = observed[indices]
        outside = n-len(offsets)
        outside_sum = total-y.sum(axis=1)
        weights = np.full((len(g), 2), .5)
        background = np.ones(len(g))
        for _ in range(policy['profile_iterations']):
            variance = background[:, None]+np.einsum('gkj,gj->gk', kernels, weights)
            inv = 1/np.maximum(variance, floor)
            weights *= np.einsum('gkj,gk->gj', kernels, y*inv*inv)/np.maximum(np.einsum('gkj,gk->gj', kernels, inv), floor)
            numerator = (y*inv*inv).sum(axis=1)+outside_sum/background**2
            denominator = inv.sum(axis=1)+outside/background
            background = np.maximum(floor, background*numerator/np.maximum(denominator, floor))
        variance = background[:, None]+np.einsum('gkj,gj->gk', kernels, weights)
        cost = (np.log(variance)+y/variance).sum(axis=1)+outside*np.log(background)+outside_sum/background
        scores[first:first+len(g)] = null_cost-cost
        peaks[first:first+len(g)] = 1+weights.max(axis=1)/background
    return scores, peaks


def estimate_dynamic_frequency(raw, manifest, original_frequencies, *, valid=None, config=None):
    """Return a JSON-safe frequency-only selection, or the unmodified full bank.

    Dates are nominal capture seconds minus capture lead. The fixed aperture is
    after the latest public Helper start and before the earliest Helper end,
    with endpoint-clock, excess-delay and settling allowances. DATA may already
    be present; disjoint pilot fit windows retain its actual spectral leakage.
    Optional require_data_free needs 36 seconds before earliest DATA support.
    """
    started = time.perf_counter()
    policy = frequency_policy(config)
    bank = np.asarray(original_frequencies, dtype=float)
    if bank.ndim != 1 or not len(bank) or not np.isfinite(bank).all() or len(np.unique(bank)) != len(bank):
        raise ValueError('original frequency bank must be finite, nonempty and unique')
    result = dict(schema='triad-dynamic-frequency-v1', status='fallback', reason='',
        selected_frequencies=bank.tolist(), selected_count=len(bank), original_count=len(bank),
        expanded_intervals_hz=[], competitive_intervals_hz=[], pilot_diagnostics={},
        original_frequencies_hz=bank.tolist(), original_bank_nonconformity=[0.]*len(bank),
        available_at=None, applicable_horizon=None, time_reference=None,
        clock_status='unchanged', phase_reference_usable=False, policy=policy,
        nonconformity_definition='Minimum over presence-qualified pilots of max(profile)-max(profile mapped to each original CFO by full public allowances); independent of competitive cutoff and later fallback gates. No presence-qualified pilot returns zeros because the full bank remains selected.')

    def finish(reason):
        result['reason'] = reason
        result['wall_seconds'] = time.perf_counter()-started
        return result

    if not isinstance(manifest.get('helper_enabled', True), bool):
        raise ValueError('helper_enabled must be boolean')
    if not manifest.get('helper_enabled', True):
        return finish('helper_disabled')
    hp, ch, calendar = manifest['helper_plan'], manifest['channel'], manifest['calendar']
    if hp.get('pilot_dwell_gating', False) or manifest.get('format', {}).get('physical_rf_retuning', False):
        return finish('gated_or_retuned_pilots_unsupported')
    fs = _number(manifest['sample_rate_hz'], 'sample rate', np.finfo(float).tiny)
    y = np.asarray(raw)
    if y.ndim != 1 or not np.iscomplexobj(y) or len(y) != int(calendar['capture_samples']):
        raise ValueError('raw complex capture must match public capture_samples')
    if valid is not None:
        valid = np.asarray(valid)
        if valid.shape != y.shape or not np.all((valid == 0) | (valid == 1)):
            raise ValueError('valid must be a binary capture mask')
    required = ('sample_clock_pair_ppm', 'endpoint_uniform_half_bound_ppm',
                'random_doppler_width_hz', 'delay_excursion_seconds',
                'relative_phase_diffusion_rad_sqrt_s')
    if any(name not in ch for name in required):
        result['missing_fields'] = [name for name in required if name not in ch]
        return finish('public_uncertainty_metadata_missing')
    margin = _number(ch.get('spread_margin', 1.), 'margin', np.finfo(float).tiny)
    audio = _number(ch['sample_clock_pair_ppm'], 'audio clock')*margin*1e-6
    endpoint = _number(ch['endpoint_uniform_half_bound_ppm'], 'endpoint clock')*margin*1e-6
    if max(audio, endpoint) >= .1:
        return finish('clock_envelope_unsupported')
    lead = _number(calendar['capture_lead_seconds'], 'capture lead')
    arrivals = np.asarray(calendar['arrival_prior_seconds'], float)
    if arrivals.shape != (2,) or not np.isfinite(arrivals).all() or arrivals[0] > arrivals[1]:
        raise ValueError('finite ordered arrival prior required')
    delay = _number(ch['spread_delay_ms'], 'excess delay')*.001+_number(ch['delay_excursion_seconds'], 'delay excursion')
    settling = max(delay, _number(calendar.get('fixed_capture_tail_bound_seconds', delay), 'channel settling tail'))
    helper_start, helper_end = float(calendar.get('helper_start_seconds', 0.)), float(calendar['helper_end_seconds'])
    source_start = float(calendar['data_sample_offset'])/fs
    guard = policy['edge_guard_seconds']
    begin = (arrivals[1]+helper_start/(1-endpoint)+settling+guard)*(1+endpoint)
    data_start_bound = (arrivals[0]+source_start/(1+endpoint)-guard)*(1-endpoint)
    end_limit = (arrivals[0]+helper_end/(1+endpoint)-guard)*(1-endpoint)
    if policy['require_data_free']:
        end_limit = min(end_limit, data_start_bound)
    first = max(0, math.ceil((begin+lead)*fs))
    count = math.ceil(policy['observation_seconds']*fs)
    end = first+count
    if count > policy['maximum_fft_samples']:
        return finish('fft_work_limit')
    if end > len(y) or end/fs-lead > end_limit:
        return finish('insufficient_guaranteed_data_free_helper_support' if policy['require_data_free'] else
                      'insufficient_guaranteed_helper_support')
    if valid is not None and np.any(valid[first:end] != 1):
        return finish('known_erasure_in_helper_aperture')
    aperture = y[first:end]
    if not np.isfinite(aperture).all():
        raise ValueError('nonfinite observed Helper aperture')
    start_time, stop_time = first/fs-lead, end/fs-lead
    horizon = (max(0., (len(y)-1)/fs-lead-stop_time) if policy['future_horizon_seconds'] is None
               else policy['future_horizon_seconds'])
    result.update(available_at=stop_time, applicable_horizon=stop_time+horizon,
        time_reference=dict(first_sample=first, end_sample=end, start_seconds=start_time,
            end_seconds=stop_time, center_seconds=(start_time+stop_time)/2,
            convention='nominal n/Fs minus capture_lead_seconds'),
        observation_seconds=count/fs, future_horizon_seconds=horizon,
        overlaps_possible_data=bool(stop_time > data_start_bound),
        earliest_data_support_seconds=data_start_bound, channel_settling_allowance_seconds=settling,
        leakage_scope='Complete observed DATA/Helper retained; disjoint engineering fit envelopes do not imply zero finite-window leakage.')
    width = _number(ch['random_doppler_width_hz'], 'random Doppler width')
    separation = _number(ch['spread_doppler_hz'], 'ray separation')
    motion = _number(ch.get('delay_motion_doppler_bound_hz', 0.), 'delay motion')
    am_rate = _number(ch.get('selective_modulation_rate_hz', separation), 'modulation rate')
    slow = _number(ch.get('selective_slow_depth', 0.), 'slow depth')
    fast = _number(ch.get('selective_fast_depth', 0.), 'fast depth')
    am_width = am_rate*(1. if fast else .1 if slow else 0.)
    sigma = math.hypot(width/2, .75/(count/fs))
    # The core fit is not an exact Gaussian-window convolution. The 0.75/T
    # term supplies an explicit Hann resolution floor; independent capture
    # validation must assess mismatch including AM sidebands and finite tails.
    window = np.hanning(count)
    spectrum = fft.fft(aperture.astype(np.complex64)*window.astype(np.float32), workers=2)
    periodogram = np.abs(spectrum.astype(np.complex128))**2/np.dot(window, window)
    frequencies = fft.fftfreq(count, 1/fs)
    order = np.argsort(frequencies)
    frequencies, periodogram = frequencies[order], periodogram[order]
    q = _number(ch['relative_phase_diffusion_rad_sqrt_s'], 'phase diffusion')
    block = policy['data_frequency_aperture_seconds']
    looks = max(1., math.ceil(horizon/block))
    z = math.sqrt(2*math.log(2*looks/policy['oscillator_tail_probability']))
    oscillator = 6*q/(2*math.pi*math.sqrt(count/fs))+z*q/(2*math.pi*math.sqrt(block))
    common = (policy['model_allowance_hz']+policy['ray_width_allowance']*width+
        policy['modulation_width_allowance']*am_width+motion+
        separation*endpoint/(1-endpoint)+oscillator+
        policy['drift_bound_hz_per_second']*(horizon+count/fs))
    result['allowances_hz'] = dict(random_ray=policy['ray_width_allowance']*width,
        selective_modulation=policy['modulation_width_allowance']*am_width,
        delay_motion=motion, rx_doppler_scale=separation*endpoint/(1-endpoint),
        oscillator=oscillator, drift=policy['drift_bound_hz_per_second']*(horizon+count/fs),
        model=policy['model_allowance_hz'])
    data_if = float(ch['nominal_conversion_if_hz'])
    usable = []
    boundary_failure = False
    flat_failure = False
    mapped_deficits = []
    occupied = _number(manifest.get('format', {}).get('occupied_bandwidth_hz', 0.), 'occupied DATA width')
    if occupied <= 0 and result['overlaps_possible_data']:
        return finish('public_data_spectral_support_missing')
    source_skirt = _number(ch.get('source_spectral_skirt_allowance_hz', 0.), 'source skirt')
    data_extent = (occupied*.5*(1+audio)+source_skirt+separation+3*width+
                   10*am_width+motion+max(abs(bank.min()), abs(bank.max())))
    result['public_data_envelope_hz'] = [-data_extent, data_extent]
    for name in ('low', 'high'):
        if policy['pilot_use'] not in ('both', name):
            continue
        center = float(hp[name+'_hz'])-data_if
        audio_extent = abs(center)*audio
        lo, hi = float(bank.min()-audio_extent), float(bank.max()+audio_extent)
        step = max(1/(count/fs), sigma/4)
        points = max(2, math.ceil((hi-lo)/step)+1)
        if points > policy['maximum_profile_points']:
            return finish('profile_work_limit')
        grid = np.linspace(lo, hi, points)
        pad = 6*sigma+3*fs/count
        fit_low, fit_high = center+lo-pad, center+hi+separation+pad
        if fit_low <= -fs/2 or fit_high >= fs/2:
            result['pilot_diagnostics'][name] = dict(usable=False, reason='pilot_support_outside_capture')
            continue
        if result['overlaps_possible_data'] and max(fit_low, -data_extent) <= min(fit_high, data_extent):
            result['pilot_diagnostics'][name] = dict(usable=False, reason='pilot_fit_window_overlaps_data_envelope',
                fit_window_hz=[fit_low, fit_high])
            continue
        mask = (frequencies >= fit_low) & (frequencies <= fit_high)
        if mask.sum() > policy['maximum_fit_bins']:
            return finish('fit_window_work_limit')
        f, power = frequencies[mask]-center, periodogram[mask]
        score, peak = _whittle_profile(power, f, grid, separation, sigma, policy)
        regions = _regions(grid, score, policy['competitive_score_drop'])
        best = int(np.argmax(score))
        diag = dict(grid_hz=grid.tolist(), profile_scores=score.tolist(),
            competitive_intervals_hz=regions, best_shift_hz=float(grid[best]),
            presence_score=float(score[best]), peak_background_ratio=float(peak[best]),
            fit_bin_count=len(f), fit_window_hz=[fit_low, fit_high],
            audio_clock_allowance_hz=audio_extent, usable=False)
        result['pilot_diagnostics'][name] = diag
        if score[best] < policy['minimum_presence_score'] or peak[best] < policy['minimum_peak_background_ratio']:
            diag['reason'] = 'weak_or_absent_pilot'
            continue
        # Cutoff-independent diagnostic for external calibration: compute from
        # every presence-qualified pilot BEFORE cutoff/boundary/consistency gates.
        # Returning a full bank at the current cutoff must not zero this value,
        # since a changed cutoff can alter the subsequent fallback branch.
        allowance = common+audio_extent+step
        mapped = []
        for frequency in bank:
            eligible = np.abs(grid-frequency) <= allowance
            mapped.append(float(max(0., score.max()-score[eligible].max())) if eligible.any() else None)
        diag['original_bank_nonconformity'] = mapped
        mapped_deficits.append(mapped)
        result['original_bank_nonconformity'] = [min(values) if values else None for values in
            ([row[i] for row in mapped_deficits if row[i] is not None] for i in range(len(bank)))]
        if float(np.ptp(score)) <= policy['competitive_score_drop']:
            diag['reason'] = 'flat_profile'
            flat_failure = True
            continue
        if any(a <= lo+step or b >= hi-step for a, b in regions):
            diag['reason'] = 'competitive_profile_at_prior_boundary'
            boundary_failure = True
            continue
        allowance = common+audio_extent+step
        expanded = _merge([(a-allowance, b+allowance) for a, b in regions])
        diag.update(usable=True, reason='engineering_profile_passed', expanded_intervals_hz=expanded)
        usable.append((name, expanded))
    if flat_failure:
        return finish('flat_profile')
    if boundary_failure:
        return finish('competitive_profile_at_prior_boundary')
    if not usable:
        return finish('no_usable_pilot')
    if len(usable) == 2 and not any(max(a,c) <= min(b,d)
        for a,b in usable[0][1] for c,d in usable[1][1]):
        return finish('pilot_frequency_regions_inconsistent')
    # Union protects both usable pilot regions; no independent-fading or shared
    # phase assumption authorizes an intersection or multiplying their scores.
    expanded = _merge([interval for _, regions in usable for interval in regions])
    selected = _subset(bank, expanded)
    admitted = expanded
    if policy['selection_geometry'] == 'continuous_interval':
        admitted, retained_fraction = _continuous_intervals(bank, expanded)
        if retained_fraction is None:
            return finish('profile_or_allowance_too_broad')
        # The case-suite caller consumes these continuous intervals. Measure
        # their union against the continuous public prior, rather than letting
        # coarse diagnostic points and neighbor padding veto a valid interval.
    else:
        retained_fraction = len(selected)/len(bank)
    result.update(expanded_intervals_hz=admitted,
        competitive_intervals_hz=_merge([interval for name, _ in usable
            for interval in result['pilot_diagnostics'][name]['competitive_intervals_hz']]),
        usable_pilots=[name for name, _ in usable],
        selection_geometry=policy['selection_geometry'],
        retained_fraction=retained_fraction,
)
    if not admitted or retained_fraction >= policy['maximum_retained_fraction']:
        return finish('profile_or_allowance_too_broad')
    result.update(status='narrowed', selected_frequencies=selected.tolist(), selected_count=len(selected))
    return finish('engineering_frequency_regions_selected')
