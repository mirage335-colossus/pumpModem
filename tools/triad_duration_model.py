"""Cheap, explicitly provisional duration selection for the weak triad receiver.

This is not the native fixed-rank projection-energy planner. It forecasts a
single retained true Gaussian-likelihood atom with its ORIGINAL search weight.
Acquisition inclusion, bit identification and a physical null remain unqualified.
"""
from __future__ import annotations

import math

import numpy as np
from scipy.special import gammainc
from scipy.stats import norm

from triad_operational_admission import (DEFAULT_COHERENCES, operational_threshold,
                                         public_grid)

_nodes, _weights = np.polynomial.legendre.leggauss(64)
JULIAN_YEAR_SECONDS = 365.25 * 86400.
SECONDS_PER_DAY = 86400.
FREQUENCY_MISMATCH_LOSS_BUDGET_DB = -20*math.log10(math.sin(math.pi/8)/(math.pi/8))
CLOCK_MISMATCH_FINE_CHIPS = .125


def _positive(value, name):
    if not math.isfinite(value) or value <= 0:
        raise ValueError(name + ' must be finite and positive')
    return float(value)


def effective_modes(seconds, width_hz, phase_diffusion, rays=1):
    """Trace-squared/trace-of-square of an ideal stationary ray covariance.

    r(t)=exp(-pi² B² t²/2 - sigma_phi² |t|/2). The integral includes
    finite observation edges and has the static-fading limit one mode/ray.
    Rays are assumed independently resolved. No carrier-label filtering occurs.
    """
    t = _positive(seconds, 'observation span')
    if min(width_hz, phase_diffusion) < 0 or not all(
            math.isfinite(x) for x in (width_hz, phase_diffusion)):
        raise ValueError('finite nonnegative covariance parameters required')
    if rays not in (1, 2):
        raise ValueError('one or two physical rays required')
    a, b = (math.pi * width_hz)**2, phase_diffusion**2
    rate = max(math.sqrt(a), b, 1/t)
    end = min(t*rate, 40.)
    x = (_nodes+1)*end/2
    integral = float(np.dot(_weights, (1-x/(rate*t)) *
        np.exp(-(a/rate/rate)*x*x-(b/rate)*x))) * end/2
    return max(float(rays), rays*t*rate/(2*integral))


def _x_minus_log1p(x):
    if x < 1e-4:
        return x*x*(.5-x/3+x*x/4-x*x*x/5)
    return x-math.log1p(x)


def gaussian_prediction(energy, modes, threshold):
    """Gamma equal-mode surrogate; its Chernoff bound is exact for this model.

    Under H1 L=lambda*Gamma(M,1)-M*log1p(lambda). Fractional M is an
    explicit moment surrogate, not actual finite-dimensional eigenvalues.
    """
    e, m = _positive(energy, 'energy'), _positive(modes, 'modes')
    lam = e/m
    mean = m*_x_minus_log1p(lam)
    delta = (mean-threshold)/e
    if delta <= 0:
        log_miss = 0.
    elif delta >= 1:
        log_miss = -math.inf
    else:
        # Avoid catastrophic cancellation in the inverse-square low-C/N0 regime.
        loss = (-delta*delta*(.5+delta/3+delta*delta/4+delta**3/5)
                if delta < 1e-4 else delta+math.log1p(-delta))
        log_miss = m*loss
    x = max(0., (threshold+m*math.log1p(lam))/lam)
    probability = float(1-gammainc(m, x))
    return dict(model='equal-mode Gaussian covariance likelihood', energy_n0=e,
        effective_modes=m, energy_per_mode=lam, mean_true_atom_log_lr=mean,
        modeled_detection_probability=probability,
        modeled_detection_lower_bound=-math.expm1(log_miss),
        log_miss_bound=None if log_miss == -math.inf else log_miss,
        tail_method='analytic Gamma lower-tail Chernoff; probability uses Gamma CDF',
        tail_scope='Bound for the equal-mode surrogate, not for the sampled channel/receiver')


def deterministic_prediction(energy, rank, threshold):
    """Constant unknown complex-gain basis, fixed actual energy, Gaussian prior.

    S=||Q^H y||²/N0 has noncentral complex-Gamma law. The receiver atom uses
    L=a*S-r*log1p(E/r); a=(E/r)/(1+E/r). No known-phase bonus is assumed.
    """
    e = _positive(energy, 'energy')
    r = _positive(rank, 'rank')
    lam = e/r
    scale = lam/(1+lam)
    x = (threshold+r*math.log1p(lam))/scale
    if x >= r+e:
        log_miss = 0.
    elif x <= 0:
        log_miss = -math.inf
    else:
        v = 2*x/(r+math.sqrt(r*r+4*e*x))
        log_miss = min(0., (1/v-1)*x+r*math.log(v)-e*(1-v))
    return dict(model='constant unknown-gain subspace, fixed actual energy',
        energy_n0=e, effective_modes=r, energy_per_mode=lam,
        mean_true_atom_log_lr=scale*(r+e)-r*math.log1p(lam),
        modeled_detection_probability=None,
        modeled_detection_lower_bound=-math.expm1(log_miss),
        log_miss_bound=None if log_miss == -math.inf else log_miss,
        tail_method='noncentral complex-Gamma lower-tail Chernoff',
        tail_scope='Fixed matched basis and fixed energy; no acquisition/model-mismatch guarantee')


def long_span_diagnostics(native, grid, *, active_seconds, elapsed_seconds,
                          message_bits, helper_lead_seconds, pulse_halo_seconds,
                          alpha_per_day, nominal_cn0_db_hz,
                          outage_fraction, retained_fraction):
    """Describe scaling beyond receiver qualification without changing its grid.

    Frequency and clock counts are design comparisons only. They never enter
    the selected likelihood threshold or imply long-span receiver support.
    """
    duration = _positive(float(elapsed_seconds), 'elapsed duration')
    active = _positive(float(active_seconds), 'active duration')
    if isinstance(message_bits, bool) or not isinstance(message_bits, int) or message_bits < 1:
        raise ValueError('positive integer source-bit count required')
    if not math.isfinite(helper_lead_seconds) or helper_lead_seconds < 0:
        raise ValueError('finite nonnegative Helper lead required')
    if not math.isfinite(pulse_halo_seconds) or pulse_halo_seconds < 0:
        raise ValueError('finite nonnegative pulse halo required')
    if not math.isfinite(alpha_per_day) or not 0 < alpha_per_day < 1:
        raise ValueError('alpha per day must be in (0,1)')

    whole_message_seconds = (helper_lead_seconds + message_bits*duration +
                             2*pulse_halo_seconds + .4)
    # Separate a contiguous serialization estimate from a conservative assumed
    # schedule with a fresh Helper lead and UTC-grid wait for every bit. Neither
    # is an implemented arbitrary-message calendar or unbounded operation.
    accounting_seconds = message_bits*(duration+helper_lead_seconds+30.+2*pulse_halo_seconds+.4)
    covered_days = max(1, math.ceil(accounting_seconds / SECONDS_PER_DAY))
    whole_message_alpha_bound = min(1., covered_days*alpha_per_day)

    # The public admission grid is endpoint-inclusive and expands its frequency
    # radius outward. Keep these existing points as an audit; do not replace or
    # feed the much finer design lattice into the present receiver.
    cfo_bound = float(grid.frequency_bound)
    current_freq_step = float(grid.frequency_step)
    current_freq_count = int(grid.counts[2])
    worst_frequency_error = min(cfo_bound, current_freq_step/2)
    current_clock_intervals = int(grid.clock_intervals) if grid.clock_bound else 0
    current_clock_step = (2*float(grid.clock_bound)/current_clock_intervals
                          if current_clock_intervals else 0.)
    current_clock_count = current_clock_intervals+1 if current_clock_intervals else 1
    current_clock_error = current_clock_step/2
    clock_error_seconds = current_clock_error*duration*1e-6

    fine_rate = native.get('format', {}).get('fine_chip_rate_hz')
    if fine_rate is None:
        clock_design = dict(status='unavailable', reason='native fine_chip_rate_hz is absent')
    else:
        fine_rate = _positive(float(fine_rate), 'fine-chip rate')
        clock_design_step = 2*CLOCK_MISMATCH_FINE_CHIPS*1e6/(fine_rate*duration)
        clock_design_count = (1 if grid.clock_bound == 0 else
            math.ceil(2*float(grid.clock_bound)/clock_design_step)+1)
        clock_error_chips = fine_rate*clock_error_seconds
        clock_design = dict(status='design_reference', fine_chip_rate_hz=fine_rate,
            allowed_nearest_grid_error_fine_chips=CLOCK_MISMATCH_FINE_CHIPS,
            required_step_ppm=clock_design_step,
            required_points_across_current_bound=clock_design_count,
            current_step_ppm=current_clock_step,
            current_grid_points=current_clock_count,
            current_half_step_time_error_seconds=clock_error_seconds,
            current_half_step_fine_chips=clock_error_chips,
            qualification='0.125 chip is a design assumption; no shaping or receiver loss bound is qualified.')

    windows = (.5, 2., duration)
    frequency_design = []
    for coherent_window in windows:
        required_step = 1/(4*coherent_window)
        required_count = (1 if cfo_bound == 0 else
            math.ceil(2*cfo_bound/required_step)+1)
        frequency_design.append(dict(coherent_window_seconds=coherent_window,
            required_step_hz=required_step,
            maximum_rectangular_bin_mismatch_loss_db=FREQUENCY_MISMATCH_LOSS_BUDGET_DB,
            required_points_across_current_bound=required_count))
    full_span_frequency_reference = frequency_design[-1]
    frequency_resolution_compatible = (worst_frequency_error <=
        full_span_frequency_reference['required_step_hz']/2)
    if fine_rate is None:
        clock_resolution_compatible = None
    else:
        clock_resolution_compatible = (clock_error_chips <=
            CLOCK_MISMATCH_FINE_CHIPS)
    full_span_grid_resolution_compatible = (None if clock_resolution_compatible is None
        else bool(frequency_resolution_compatible and clock_resolution_compatible))
    nominal_energy_n0 = (10**(nominal_cn0_db_hz/10) * active *
                         (1-outage_fraction) * retained_fraction)
    return dict(scope='Forecast-duration and search-resolution diagnostics only; no long-span receiver performance is asserted.',
        selected_active_seconds=active, selected_active_days=active/SECONDS_PER_DAY,
        selected_active_julian_years=active/JULIAN_YEAR_SECONDS,
        selected_elapsed_seconds=duration, selected_elapsed_days=duration/SECONDS_PER_DAY,
        selected_elapsed_julian_years=duration/JULIAN_YEAR_SECONDS,
        whole_message_elapsed_seconds=whole_message_seconds,
        whole_message_elapsed_days=whole_message_seconds/SECONDS_PER_DAY,
        whole_message_elapsed_julian_years=whole_message_seconds/JULIAN_YEAR_SECONDS,
        whole_message_elapsed_scope='Approximate contiguous serialization with one Helper lead; not a verified multi-bit export calendar.',
        admission_accounting_horizon_seconds=accounting_seconds,
        admission_accounting_horizon_scope='Assumed schedule with per-bit Helper lead, up to30s UTC alignment, pulse halos and0.4s endpoint allowance. Longer listening/search schedules require their own exposure bound.',
        modeled_active_data_energy_n0=nominal_energy_n0,
        nominal_data_transmit_energy_j=float(native['physics']['data_w'])*active,
        assumed_outage_fraction=outage_fraction,
        assumed_retained_energy_fraction=retained_fraction,
        whole_message_days_charged=covered_days,
        per_24h_alpha=alpha_per_day,
        whole_message_conditional_null_union_bound=whole_message_alpha_bound,
        null_bound_scope='Union over 24-hour periods covering the separately stated assumed accounting horizon, under the configured per-day conditional Gaussian-null bound; no independence assumed. Not a physical-null bound or an unlimited-operation guarantee.',
        current_receiver_long_span_qualified=False,
        receiver_search_scope='The charged likelihood grid remains the existing PublicGrid. Long-span frequency/clock requirements below are separate design references and are not added to that grid or its duration forecast.',
        current_search_grid=dict(candidate_count=int(grid.candidate_count),
            epochs=len(grid.epochs), arrival_intervals=int(grid.arrival_intervals),
            frequency_bound_hz=cfo_bound, frequency_step_hz=current_freq_step,
            frequency_radius=int(grid.frequency_radius),
            frequency_points=current_freq_count, frequency_worst_half_step_error_hz=worst_frequency_error,
            full_span_frequency_resolution_meets_design_reference=frequency_resolution_compatible,
            clock_bound_ppm=float(grid.clock_bound), clock_intervals=current_clock_intervals,
            clock_step_ppm=current_clock_step, clock_points=current_clock_count,
            clock_half_step_timing_error_seconds=clock_error_seconds,
            clock_resolution_meets_design_reference=clock_resolution_compatible),
        full_span_grid_resolution_compatible=full_span_grid_resolution_compatible,
        full_span_grid_compatibility_scope='Compares the current grid only with the stated fully coherent frequency and 0.125-chip clock design references; true means resolution compatible, not receiver or channel qualification. False identifies at least one inadequate grid spacing.',
        full_coherent_design_reference=dict(
            scope='Hypothetical rectangular matched integration over each listed span; assumes phase remains coherent over that full window.',
            frequency_mismatch_loss_budget_db=FREQUENCY_MISMATCH_LOSS_BUDGET_DB,
            frequency_requirements=frequency_design,
            clock_requirement=clock_design,
            limitations=['A stochastic or renewing channel may not support coherence over the full elapsed span.',
                         'Long-term oscillator/channel trajectory, reacquisition and implementation cost are unqualified.',
                         'Required point counts are analytic estimates; no long receiver arrays were constructed.']))


def plan_from_native(native, target_cn0_db_hz, *, correct_probability=.99,
                     message_bits=1, alpha_per_day=1e-4,
                     attempt_spacing_seconds=.4, duration_alternatives=1,
                     outage_fraction=0., retained_fraction=1.,
                     max_active_seconds=315576000., max_evaluations=128,
                     manual_seconds=None):
    """Recommend a legal active tile count; native shaping owns the lattice.

    Half the requested per-message failure budget goes to true-atom detection.
    The remainder is RESERVED, not assumed achieved, for acquisition/Helper/
    wrong-bit errors. Source/data/channel truth never enters this calculation.
    """
    if not math.isfinite(target_cn0_db_hz) or not -120 <= target_cn0_db_hz <= 200:
        raise ValueError('target C/N0 must be finite in [-120,200] dB-Hz')
    if not math.isfinite(correct_probability) or not 0 < correct_probability < 1:
        raise ValueError('correct reception probability must be in (0,1)')
    if isinstance(message_bits, bool) or not isinstance(message_bits, int) or message_bits < 1:
        raise ValueError('positive integer source-bit count required')
    if not 0 <= outage_fraction < 1 or not 0 < retained_fraction <= 1:
        raise ValueError('outage must be in [0,1); retained energy in (0,1]')
    if not all(math.isfinite(x) for x in (outage_fraction, retained_fraction)):
        raise ValueError('finite loss fractions required')
    limit = _positive(max_active_seconds, 'maximum active duration')
    if isinstance(max_evaluations, bool) or not isinstance(max_evaluations, int) or max_evaluations < 4:
        raise ValueError('at least four bounded planner evaluations required')
    f, p = native['format'], native['physics']
    tile = _positive(float(f['tile_seconds']), 'native tile duration')
    waits = int(f['burst_wait_tiles'])
    if waits < 0:
        raise ValueError('negative burst wait')
    # One public 30-second support-grid slot: initial Helper waiting <30 s.
    # This is a conservative pre-export origin-shift allowance, not fitted timing.
    budget = operational_threshold(alpha_per_day, attempt_spacing_seconds, duration_alternatives)
    model_count = len(DEFAULT_COHERENCES)
    helper_lead = float(native['export_constraints']['fixed_helper_lead_seconds'])
    halo = float(native['export_constraints'].get('pulse_halo_seconds', 0))
    def grid_at(elapsed):
        # Later message slots can have a larger origin/clock displacement.
        # Include a complete 30s support-grid rounding allowance at every slot.
        origin = 30.+(message_bits-1)*(elapsed+helper_lead+30.+2*halo)
        drift = origin*float(p['sample_clock_pair_ppm'])*float(p['spread_margin'])*1e-6
        lo, hi = p['arrival_prior_seconds']
        g = public_grid(p, (lo-drift, hi+drift), tuple(range(-6, 7)))
        q = budget['log_e_threshold']+math.log(2*g.candidate_count*model_count)
        return g, q, origin
    grid, threshold, origin_bound = grid_at(tile)
    beta = (1-correct_probability)/(2*message_bits)
    rho = 10**(target_cn0_db_hz/10)
    stochastic = p['channel_evolution'] != 'legacy'
    # Exporter spells its deterministic mode "legacy"; reject unknown models.
    if p['channel_evolution'] not in ('legacy', 'fixed-time-gaussian-v1'):
        raise ValueError('unsupported channel evolution: '+p['channel_evolution'])
    width = float(p.get('random_doppler_width_hz', 0)) if stochastic else 0.
    diffusion = float(p.get('relative_phase_diffusion_rad_sqrt_s', 0))
    rays = 2 if float(p['spread_delay_ms']) or float(p['spread_doppler_hz']) else 1
    trials = {}
    def evaluate(n):
        if n in trials:
            return trials[n]
        if len(trials) >= max_evaluations:
            raise RuntimeError('planner evaluation budget exhausted; feasibility unresolved')
        active = n*tile
        elapsed = (n+(n-1)*waits)*tile
        _, atom_threshold, origin = grid_at(elapsed)
        energy = rho*active*(1-outage_fraction)*retained_fraction
        modes = effective_modes(elapsed, width, diffusion, rays)
        if stochastic:
            prediction = gaussian_prediction(energy, modes, atom_threshold)
        else:
            # Phase uncertainty does not turn fixed-amplitude propagation into
            # Rayleigh amplitude outages. Retain the fixed-energy model, using
            # its coherence-dependent effective subspace dimension.
            prediction = deterministic_prediction(energy, modes, atom_threshold)
        prediction.update(tiles=n, active_seconds=active, elapsed_data_seconds=elapsed,
            true_atom_log_lr_threshold=atom_threshold, origin_shift_allowance_seconds=origin,
            meets_detection_allocation=prediction['modeled_detection_lower_bound'] >= 1-beta)
        trials[n] = prediction
        return prediction
    max_tiles = int(math.floor(limit/tile))
    chosen = None
    status = 'duration_limit'
    if manual_seconds is not None:
        seconds = _positive(manual_seconds, 'manual active seconds')
        n = max(1, math.ceil(seconds/tile-1e-12))
        if n*tile > limit:
            raise ValueError('manual duration exceeds declared duration limit after rounding')
        chosen = evaluate(n)
        status = 'manual_duration_override'
    elif max_tiles:
        lower, upper = 0, 1
        while True:
            point = evaluate(upper)
            if point['meets_detection_allocation']:
                break
            lower = upper
            if upper == max_tiles:
                upper = None
                break
            upper = min(2*upper, max_tiles)
        if upper is not None:
            while upper-lower > 1:
                middle = (upper+lower)//2
                if evaluate(middle)['meets_detection_allocation']:
                    upper = middle
                else:
                    lower = middle
            chosen = evaluate(upper)
            if upper > 1:
                evaluate(upper-1)
            status = 'provisional_model_plan'
    if chosen:
        grid, threshold, origin_bound = grid_at(chosen['elapsed_data_seconds'])
    assumptions = [
        'Target C/N0 is nominal active total DATA power/noise density, not post-fade truth.',
        'Original full grid and fixed model prior are charged; Helper pruning does not reduce this penalty.',
        'Correct candidate must survive adaptive acquisition; this inclusion probability is unqualified.',
        'Wrong-bit selection and Helper availability consume a reserved but unqualified error budget.',
        'Declared retained fraction and uniform outage loss are assumptions, not measured certificates.',
        'Equal-power independently resolved physical rays; pulse/constellation details are not covariance eigenvectors.',
        'Gaussian effective-mode spectrum is a moment surrogate; Chernoff is conservative only within that model.',
        'Deterministic-channel phase diffusion uses a fractional effective fixed-energy rank surrogate, not Rayleigh amplitude fading.',
        'Finite-burst mode count uses elapsed span with uniformly distributed useful energy.',
        'Real audio/Helper noise null and end-to-end correct reception remain unqualified.']
    unmodeled = []
    if p.get('selective_enabled'):
        unmodeled.append('Selected dispersive amplitude modulation and its non-Gaussian likelihood mismatch; no energy credit taken.')
    if float(p.get('delay_excursion_seconds', 0)):
        unmodeled.append('Moving-delay waveform/phase mismatch beyond the declared Gaussian width.')
    if rays == 2 and abs(float(p['spread_doppler_hz'])) < 4*width:
        unmodeled.append('Ray spectra overlap: independent-resolved-ray surrogate is not qualified.')
    if f.get('fhss'):
        unmodeled.append('FHSS ramp placement is native; receiver hop-loss distribution is not qualified.')
    if waits:
        unmodeled.append('Burst gaps: stationary elapsed-span approximation needs sampled validation.')
    maximum_tests = 2*grid.candidate_count*model_count*budget['attempts_per_24h_bound']*duration_alternatives
    ideal_energy = .5*(norm.isf(alpha_per_day/maximum_tests)+norm.isf(beta))**2
    helper_available = bool(native.get('helper_enabled', True)) and float(p.get('effective_helper_w', 0)) > 0
    placement_available = bool(native.get('helper_placement', {}).get('feasible_at_requested_if', True))
    if not helper_available:
        status = 'receiver_unavailable'
        unmodeled.append('Current weak receiver requires usable Helper and has no DATA-only acquisition fallback.')
    if not native['geometry_feasible'] or not placement_available:
        status = 'geometry_unavailable'
    result = dict(schema='triad-weak-duration-plan-v1', status=status,
        target_cn0_db_hz=target_cn0_db_hz, requested_correct_probability=correct_probability,
        message_bits=message_bits, per_bit_detection_miss_budget=beta,
        reserved_other_error_budget_per_bit=beta, correct_probability_qualified=False,
        physical_null_qualified=False, model_qualified=False,
        admission_policy=dict(alpha_per_day=alpha_per_day,
            minimum_attempt_spacing_seconds=attempt_spacing_seconds,
            duration_alternatives=duration_alternatives),
        operational_budget=budget, full_grid=grid.__dict__, full_grid_count=grid.candidate_count,
        origin_shift_allowance_seconds=origin_bound, model_count=model_count,
        true_atom_log_lr_threshold=threshold, native_geometry_feasible=bool(native['geometry_feasible']),
        selected=chosen, active_seconds=None if chosen is None else chosen['active_seconds'],
        previous_legal=None if chosen is None or chosen['tiles']==1 else trials.get(chosen['tiles']-1),
        evaluated=[trials[k] for k in sorted(trials)], evaluation_count=len(trials),
        selection_scope='Geometric bracket and integer refinement; smallest passing bracketed tile plus previous-neighbor check. Model monotonicity is tested, not a universal physical theorem.',
        tile_seconds=tile, minimum_inner_coefficients=int(f['inner_coefficients']/f['tile_count']),
        outage_fraction=outage_fraction, retained_fraction=retained_fraction,
        covariance=dict(random_doppler_width_hz=width, phase_diffusion_rad_sqrt_s=diffusion,
                        rays=rays, stochastic=stochastic),
        ideal_known_mean_energy_n0=float(ideal_energy),
        ideal_known_mean_seconds=float(ideal_energy/rho),
        ideal_scope='Known received waveform and declared search accounting; true-candidate detection reference only.',
        assumptions=assumptions, unmodeled_selected_effects=unmodeled,
        helper_present=helper_available, helper_acquisition_qualified=False,
        stream_geometry_supported=chosen is not None and bool(native['geometry_feasible']) and helper_available and placement_available,
        stream_support_scope='Public geometry preflight only; bounded source, clock and receiver budgets remain independent checks.',
        native_plan=native)
    result['forecast_horizon'] = dict(configured_max_active_seconds=limit,
        configured_max_active_days=limit/SECONDS_PER_DAY,
        configured_max_active_julian_years=limit/JULIAN_YEAR_SECONDS,
        bounded_tile_evaluations=max_evaluations,
        scope='Mathematical search horizon only. Native geometry verification and bounded receiver execution are separate limits.')
    if chosen:
        result['calendar'] = dict(active_data_seconds=chosen['active_seconds'],
            elapsed_data_seconds=chosen['elapsed_data_seconds'], helper_lead_seconds=helper_lead,
            one_bit_seconds_approx=helper_lead+chosen['elapsed_data_seconds']+2*halo+.4,
            message_seconds_approx=helper_lead+message_bits*chosen['elapsed_data_seconds']+2*halo+.4,
            nominal_data_transmit_energy_j=float(p['data_w'])*chosen['active_seconds'],
            scope='Native tile lattice plus fixed lead, two pulse halos and 0.4s endpoint allowance; use exported calendar for actual time. Message time is a serialization estimate, not implemented message reception.')
        result['long_span_diagnostics'] = long_span_diagnostics(native, grid,
            active_seconds=chosen['active_seconds'],
            elapsed_seconds=chosen['elapsed_data_seconds'], message_bits=message_bits,
            helper_lead_seconds=helper_lead, pulse_halo_seconds=halo,
            alpha_per_day=alpha_per_day, nominal_cn0_db_hz=target_cn0_db_hz,
            outage_fraction=outage_fraction, retained_fraction=retained_fraction)
    else:
        result['long_span_diagnostics'] = None
    return result
