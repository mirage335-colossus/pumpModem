"""Public acquisition geometry and rate-aware FIR planning for Robust.

These helpers define no receiver or standalone experiment. Acquisition and
tracking share the canonical source and likelihood stages.
"""
from __future__ import annotations
import math
import numpy as np
from scipy import signal

def public_geometry(manifest):
    """Calibration identity; deliberately excludes DATA on/off and realized truth."""
    return {key: manifest[key] for key in ('schema', 'sample_rate_hz', 'format', 'calendar',
        'helper_plan', 'channel', 'power', 'helper_enabled', 'public_epoch_seconds', 'key_seed')} | {
        'helper_component_control': manifest.get('helper_component_control', 'full')}

def refinement_coordinates(candidates, manifest, *, clock_half_step_ppm,
                           timing_step_seconds, maximum_candidates=64):
    """Preserve DATA-midpoint alignment when refining correlated clock/arrival.

    The Cartesian product of unrelated candidate coordinates is intentionally
    never constructed. Both coarse bit lists must be supplied by the caller.
    """
    prior = manifest['calendar']['arrival_prior_seconds']
    channel, fmt = manifest['channel'], manifest['format']
    limit = float(channel['sample_clock_pair_ppm'])*float(channel.get('spread_margin', 1))
    center = (float(manifest['calendar']['data_sample_offset'])/manifest['sample_rate_hz']
              +float(fmt['bit_seconds'])/2)
    result, seen = [], set()
    for candidate in candidates:
        epoch = int(candidate['epoch'])
        arrival = float(candidate['arrival'])
        frequency = float(candidate['frequency'])
        old_q = float(candidate['clock_ppm'])
        old_scale = 1+old_q*1e-6
        midpoint = arrival+center/old_scale
        for delta_q in (-clock_half_step_ppm, 0., clock_half_step_ppm):
            q = min(limit, max(-limit, old_q+delta_q))
            adjusted = midpoint-center/(1+q*1e-6)
            for delta_t in (-timing_step_seconds, 0., timing_step_seconds):
                a = min(float(prior[1]), max(float(prior[0]), adjusted+delta_t))
                coordinate = (epoch, a, frequency, q)
                token = (epoch, round(a, 12), round(frequency, 12), round(q, 9))
                if token not in seen:
                    seen.add(token)
                    result.append(coordinate)
                    if len(result) > maximum_candidates:
                        raise ValueError('sparse refinement exceeds configured work budget')
    if not result:
        raise ValueError('no coarse candidates to refine')
    return tuple(result)

def _number(value, name, *, positive=False):
    if isinstance(value, (bool, np.bool_)):
        raise ValueError(f"{name} must be a finite number")
    try:
        result = float(value)
    except (ValueError, TypeError) as error:
        raise ValueError(f"{name} must be a finite number") from error
    if not math.isfinite(result) or (positive and result <= 0):
        raise ValueError(f"{name} must be finite" + (" and positive" if positive else ""))
    return result

def _integer(value, name, minimum=1):
    if isinstance(value, (bool, np.bool_)) or not isinstance(value, (int, np.integer)) or value < minimum:
        raise ValueError(f"{name} must be an integer >= {minimum}")
    return int(value)

def frontend_plan(profile="rate-aware", *, sample_rate=18000,
                  output_sample_rate=900, pass_edge_hz=None,
                  stop_edge_hz=None):
    """Public sample clock and causal anti-alias design; no capture tuning.

    The rate-aware profile accepts a frozen public pass/stop pair independent of
    integer decimation. Without a pair it uses 0.4/0.49 of output rate, suitable
    only when no nearer rejected band is declared. The Helper study supplies
    its public DATA/Helper separation pair. Finite skirts are retained.
    """
    raw = _integer(sample_rate, "sample_rate")
    output = _integer(output_sample_rate, "output_sample_rate")
    if raw % output or output > raw:
        raise ValueError("output sample rate must be an integer divisor of input sample rate")
    if profile == "rate-aware":
        if (pass_edge_hz is None) != (stop_edge_hz is None):
            raise ValueError("explicit FIR pass/stop edges must be supplied together")
        passed = .4 * output if pass_edge_hz is None else _number(pass_edge_hz, "pass_edge_hz", positive=True)
        stopped = .49 * output if stop_edge_hz is None else _number(stop_edge_hz, "stop_edge_hz", positive=True)
        if not 0 < passed < stopped < output / 2:
            raise ValueError("FIR requires 0 < pass edge < stop edge < output Nyquist")
    else:
        raise ValueError("frontend profile must be rate-aware")
    return dict(profile=profile, input_sample_rate=raw, output_sample_rate=output,
                decimation=raw // output, pass_edge_hz=passed, stop_edge_hz=stopped,
                design_attenuation_db=96.)

def _data_fir_design(profile, sample_rate, output_sample_rate, pass_edge_hz, stop_edge_hz):
    plan = frontend_plan(profile, sample_rate=sample_rate, output_sample_rate=output_sample_rate,
                         pass_edge_hz=pass_edge_hz, stop_edge_hz=stop_edge_hz)
    passed, stopped = plan["pass_edge_hz"], plan["stop_edge_hz"]
    count, beta = signal.kaiserord(96., (stopped - passed) / (sample_rate / 2))
    count += 1 - count % 2
    if count > 1_000_001:
        raise ValueError("frontend FIR exceeds the explicit 1000001-tap design budget")
    return count, (passed + stopped) / 2, beta


def data_fir_tap_count(profile="rate-aware", *, sample_rate=18000, output_sample_rate=900,
                       pass_edge_hz=None, stop_edge_hz=None):
    """Return the exact design length without allocating FIR taps."""
    return _data_fir_design(profile, sample_rate, output_sample_rate, pass_edge_hz, stop_edge_hz)[0]


def data_fir_taps(profile="rate-aware", *, sample_rate=18000,
                  output_sample_rate=900, pass_edge_hz=None,
                  stop_edge_hz=None):
    """Public real causal lowpass, with complete finite impulse response.

    Kaiser design targets 96 dB, with margin over the required 80-dB stopband.
    No automatic frequency shift, energy normalization or observation tuning.
    """
    count, cutoff, beta = _data_fir_design(profile, sample_rate, output_sample_rate,
                                          pass_edge_hz, stop_edge_hz)
    return signal.firwin(count, cutoff, window=("kaiser", beta), fs=sample_rate)
