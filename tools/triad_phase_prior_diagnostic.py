#!/usr/bin/env python3
"""Check finite phase/frequency priors before interpreting sensitivity tests.

The finite model is read from triad_phase_frequency.py. Its first phase
harmonic is propagated by a reduced frequency-space transfer matrix; several
short transitions are checked against the actual full phase/frequency state.
The independent continuum reference is a stationary Gaussian OU frequency
plus independent Wiener phase. If the finite OU generator includes a random
walk diffusion term, its continuum comparator has variance sigma_f^2+q^2*tau/2;
this is not an independent nonstationary random-walk frequency component.

For stationary OU frequency, Var[2*pi*integral_0^t f(s)ds] equals
8*pi^2*sigma_f^2*tau*(t-tau*(1-exp(-t/tau))). The phase correlation is the
exponential of minus half this variance and the independent Wiener variance.
The script reports finite-grid coherence revivals near 1/frequency_spacing.
Passing the declared finite set of moment/correlation tolerances is a prior
diagnostic only, not proof of continuous-model convergence or reception.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict
import hashlib
import json
import math
from pathlib import Path
import sys
import time

import torch

from triad_phase_frequency import PhaseFrequencyLikelihood, PhaseFrequencyModel


def _list(value, convert=float):
    result = tuple(convert(item.strip()) for item in value.split(","))
    if not result:
        raise argparse.ArgumentTypeError("list cannot be empty")
    return result


def transfer_matrix(state, dt):
    """Exact first-harmonic operator of the implemented finite transition."""
    if dt == 0:
        return torch.eye(state.frequency_states, dtype=torch.complex128, device=state.device)
    transition = state._transition(dt)
    # A positive shift of a distribution multiplies E(exp(j*phase)) by a
    # positive complex phase. The gather indices encode its negative shift.
    angle0 = -state.phase_step*transition["index0"][:, 0].to(torch.float64)
    angle1 = -state.phase_step*transition["index1"][:, 0].to(torch.float64)
    fraction = transition["fraction"].to(torch.float64)
    advection = (1-fraction)*torch.exp(1j*angle0)+fraction*torch.exp(1j*angle1)
    diffusion = sum(probability*complex(math.cos(offset*state.phase_step), math.sin(offset*state.phase_step))
                    for offset, probability in zip(transition["phase_offsets"], transition["phase_probabilities"]))
    frequency = transition["frequency"].to(torch.complex128)
    return advection[:, None]*frequency*advection[None, :]*diffusion


def continuous_correlation(lag, variance, tau, sigma_phase):
    if lag == 0:
        return 1.
    x = lag/tau
    # The series avoids cancellation in x+expm1(-x) for very short lags.
    integral = (x*x/2-x*x*x/6+x**4/24) if abs(x) < 1e-4 else x+math.expm1(-x)
    frequency_phase_variance = 8*math.pi**2*variance*tau*tau*integral
    return math.exp(-.5*(frequency_phase_variance+sigma_phase*sigma_phase*lag))


def _reduced_correlations(state, lags, dt):
    initial = state.weights[0, 0, 0].sum(-1).to(torch.complex128)
    operator = transfer_matrix(state, dt)
    maximum_steps = max(int(math.floor(lag/dt)) for lag in lags)
    powers = [operator]
    for _ in range(maximum_steps.bit_length()-1):
        powers.append(powers[-1]@powers[-1])
    result = {}
    for lag in lags:
        steps = int(math.floor(lag/dt)); remainder = lag-steps*dt
        if abs(remainder) < 32*sys.float_info.epsilon*max(1., lag):
            remainder = 0.
        value = initial.clone(); bit = 0; remaining = steps
        while remaining:
            if remaining & 1:
                value = value@powers[bit]
            remaining >>= 1; bit += 1
        if remainder:
            value = value@transfer_matrix(state, remainder)
        result[lag] = complex(value.sum())
    return result


def _full_state_check(state, dt, steps):
    frequency_prior = state.weights[0, 0, 0].sum(-1).clone()
    weights = torch.zeros_like(state.weights)
    weights[0, 0, 0, :, 0] = frequency_prior
    harmonic = torch.exp(1j*state.phases.to(torch.float64))
    reduced = frequency_prior.to(torch.complex128)
    operator = transfer_matrix(state, dt)
    maximum_error = 0.
    for _ in range(steps):
        weights = state._predict(weights, dt)
        reduced = reduced@operator
        full = complex((weights[0, 0, 0].sum(0)*harmonic).sum())
        maximum_error = max(maximum_error, abs(full-complex(reduced.sum())))
    if maximum_error > 2e-10:
        raise ValueError(f"reduced/full finite prior discrepancy {maximum_error}")
    return maximum_error


def self_checks(device):
    base = dict(phase_states=32, frequency_bound_hz=0., frequency_std_hz=0.,
                random_walk_hz_per_sqrt_second=0., amplitude_nodes=(1.,),
                amplitude_weights=(1.,), relative_phase_states=1,
                phase_kernel_tail_probability=0.)
    zero = PhaseFrequencyLikelihood(1, 1, 1, PhaseFrequencyModel(**base,
        phase_diffusion_rad_per_sqrt_second=0.), device=device)
    zero_values = _reduced_correlations(zero, (0., 4., 1024.), 4.)
    zero_error = max(abs(value-1) for value in zero_values.values())
    if zero_error > 1e-12:
        raise ValueError("zero oscillator limit failed")
    sigma = .03
    diffusion = PhaseFrequencyLikelihood(1, 1, 1, PhaseFrequencyModel(**base,
        phase_diffusion_rad_per_sqrt_second=sigma), device=device)
    actual = _reduced_correlations(diffusion, (40.,), 4.)[40.]
    expected = math.exp(-sigma*sigma*40*(1-math.cos(diffusion.phase_step))/diffusion.phase_step**2)
    diffusion_error = abs(actual-expected)
    if diffusion_error > 1e-12:
        raise ValueError("discrete pure phase-diffusion moment failed")
    negative = PhaseFrequencyLikelihood(1, 1, 1, PhaseFrequencyModel(**base,
        frequency_grid_hz=(-.03125,), frequency_model="static",
        phase_diffusion_rad_per_sqrt_second=0.), device=device)
    negative_error = abs(_reduced_correlations(negative, (8.,), 4.)[8.]+1j)
    if negative_error > 1e-12:
        raise ValueError("negative-frequency advection moment failed")
    full_error = _full_state_check(negative, 3.7, 4)
    return {"zero_oscillator_error": zero_error, "pure_diffusion_error": diffusion_error,
            "negative_frequency_error": negative_error, "fractional_negative_advection_full_state_error": full_error}


def run_case(args, phase_states):
    grid = None
    if args.frequency_step_hz is not None:
        half = math.ceil(args.frequency_bound_hz/args.frequency_step_hz)
        if 2*half+1 > args.maximum_frequency_states:
            raise ValueError("explicit frequency grid exceeds state cap before allocation")
        grid = tuple(index*args.frequency_step_hz for index in range(-half, half+1))
    model = PhaseFrequencyModel(phase_states=phase_states,
        frequency_bound_hz=args.frequency_bound_hz, nominal_dt_seconds=args.dt,
        frequency_grid_hz=grid, frequency_std_hz=args.frequency_std_hz,
        frequency_correlation_seconds=args.frequency_correlation_seconds,
        random_walk_hz_per_sqrt_second=args.random_walk_hz_per_sqrt_second,
        phase_diffusion_rad_per_sqrt_second=args.phase_diffusion,
        amplitude_nodes=(1.,), amplitude_weights=(1.,), relative_phase_states=1,
        maximum_frequency_states=args.maximum_frequency_states)
    state = PhaseFrequencyLikelihood(1, 1, 1, model, device=args.device,
        dtype=torch.float64, validate_inputs=False, memory_limit_bytes=int(args.memory_gib*1024**3))
    maximum_steps = int(math.floor(max(args.lags)/args.dt))
    diagnostic_workspace = state.working_bytes_bound + (maximum_steps.bit_length()+4)*state.frequency_states**2*16
    if diagnostic_workspace > int(args.memory_gib*1024**3):
        raise ValueError("prior diagnostic transfer powers exceed the declared memory limit")
    spacing = state.frequency_spacing_hz
    period = 1/spacing if spacing else None
    lags = set(args.lags)
    if period is not None:
        for multiple in (.5, 1., 2., 4.):
            if period*multiple <= max(args.lags):
                lags.add(period*multiple)
    lags = tuple(sorted(lags))
    correlations = _reduced_correlations(state, lags, args.dt)
    frequency = state.frequency_grid_cpu
    initial = state.weights[0, 0, 0].sum(-1).cpu().to(torch.float64)
    mean = float((initial*frequency).sum())
    centered = frequency-mean
    variance = float((initial*centered.square()).sum())
    continuum_variance = args.frequency_std_hz**2+.5*args.random_walk_hz_per_sqrt_second**2*args.frequency_correlation_seconds
    relative_variance_error = abs(variance/continuum_variance-1) if continuum_variance > 0 else None
    decay = -float((initial*centered*(state.frequency_generator@centered)).sum())/variance if variance > 0 else None
    relative_decay_error = abs(decay*args.frequency_correlation_seconds-1) if decay is not None and continuum_variance > 0 else None
    rows = []
    for lag in lags:
        continuous = continuous_correlation(lag, continuum_variance,
                                           args.frequency_correlation_seconds, args.phase_diffusion)
        finite = correlations[lag]
        rows.append({"lag_seconds": lag, "finite_correlation_real": finite.real,
                     "finite_correlation_imag": finite.imag, "continuous_ou_correlation": continuous,
                     "absolute_correlation_error": abs(finite-continuous)})
    maximum_error = max(row["absolute_correlation_error"] for row in rows)
    mean_tolerance = max(1e-12, .01*math.sqrt(continuum_variance))
    variance_ok = (relative_variance_error <= args.variance_relative_tolerance) if relative_variance_error is not None else variance <= 1e-20
    decay_ok = relative_decay_error is None or relative_decay_error <= args.decay_relative_tolerance
    mean_ok = abs(mean) <= mean_tolerance
    passed = mean_ok and variance_ok and decay_ok and maximum_error <= args.correlation_absolute_tolerance
    return {"phase_states": phase_states, "frequency_states": state.frequency_states,
            "diagnostic_working_bytes_bound": diagnostic_workspace,
            "model": asdict(model), "frequency_grid_hz": frequency.tolist(), "frequency_spacing_hz": spacing,
            "frequency_comb_period_seconds": period, "initial_frequency_mean_hz": mean,
            "initial_frequency_variance_hz2": variance, "continuous_stationary_frequency_variance_hz2": continuum_variance,
            "relative_frequency_variance_error": relative_variance_error,
            "frequency_initial_correlation_decay_per_second": decay,
            "relative_frequency_decay_error": relative_decay_error,
            "maximum_absolute_phase_correlation_error": maximum_error, "correlations": rows,
            "short_full_state_check_steps": args.full_check_steps,
            "short_full_state_maximum_error": _full_state_check(state, args.dt, args.full_check_steps),
            "criterion": {"mean_absolute_tolerance_hz": mean_tolerance,
                          "variance_relative_tolerance": args.variance_relative_tolerance,
                          "decay_relative_tolerance": args.decay_relative_tolerance,
                          "phase_correlation_absolute_tolerance": args.correlation_absolute_tolerance,
                          "passed_declared_grid_diagnostic": passed,
                          "scope": "only the specified stationary OU model, interval and reported lag set; not uniform convergence, receiver sensitivity or arbitrary colored-oscillator qualification"},
            "transition_diagnostics": state.diagnostics()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase-states", type=lambda value: _list(value, int), default=(64,128,256,512))
    parser.add_argument("--frequency-bound-hz", type=float, default=.02)
    parser.add_argument("--frequency-step-hz", type=float)
    parser.add_argument("--frequency-std-hz", type=float, default=.003)
    parser.add_argument("--frequency-correlation-seconds", type=float, default=1000.)
    parser.add_argument("--random-walk-hz-per-sqrt-second", type=float, default=0.)
    parser.add_argument("--phase-diffusion", type=float, default=.00872665)
    parser.add_argument("--dt", type=float, default=4.)
    parser.add_argument("--lags", type=lambda value: _list(value), default=(0.,4.,100.,256.,512.,1024.,4096.))
    parser.add_argument("--maximum-frequency-states", type=int, default=257)
    parser.add_argument("--full-check-steps", type=int, default=4)
    parser.add_argument("--variance-relative-tolerance", type=float, default=.01)
    parser.add_argument("--decay-relative-tolerance", type=float, default=.05)
    parser.add_argument("--correlation-absolute-tolerance", type=float, default=.001)
    parser.add_argument("--memory-gib", type=float, default=1.)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        positive = (args.dt, args.frequency_correlation_seconds, args.memory_gib,
                    args.variance_relative_tolerance, args.decay_relative_tolerance,
                    args.correlation_absolute_tolerance)
        nonnegative = (args.frequency_bound_hz, args.frequency_std_hz,
                       args.random_walk_hz_per_sqrt_second, args.phase_diffusion, *args.lags)
        if any(not math.isfinite(value) or value <= 0 for value in positive) or any(not math.isfinite(value) or value < 0 for value in nonnegative):
            raise ValueError("invalid positive/nonnegative finite diagnostic parameter")
        if args.frequency_step_hz is not None and (not math.isfinite(args.frequency_step_hz) or args.frequency_step_hz <= 0):
            raise ValueError("explicit frequency step must be positive and finite")
        if not args.phase_states or not args.lags or len(args.lags) > 256 or max(args.lags)/args.dt > 1e9 or not 0 <= args.full_check_steps <= 64:
            raise ValueError("diagnostic extent is outside explicit bounds")
        torch.set_num_threads(2)
        start = time.perf_counter()
        result = {"schema": "triad-phase-prior-diagnostic-v1", "torch_version": torch.__version__,
                  "device": args.device, "arithmetic": "FP64/complex128",
                  "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  "phase_module_sha256": hashlib.sha256(Path(__file__).with_name("triad_phase_frequency.py").read_bytes()).hexdigest(),
                  "command_argv": sys.argv, "self_checks": self_checks(args.device),
                  "cases": [run_case(args, phases) for phases in args.phase_states]}
        result["wall_seconds"] = time.perf_counter()-start
        encoded = json.dumps(result, indent=2, allow_nan=False)+"\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True); args.output.write_text(encoded)
        else:
            sys.stdout.write(encoded)
        return 0
    except (ValueError, RuntimeError, OSError, OverflowError) as error:
        print(f"triad-phase-prior-diagnostic: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
