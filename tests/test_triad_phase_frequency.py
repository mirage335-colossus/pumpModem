#!/usr/bin/env python3
"""Optional independent finite-state likelihood checks; requires Torch/SciPy.

Run outside the ordinary Robust receiver regression:
  /tmp/triad-weak-env/bin/python tests/test_triad_phase_frequency.py --device cpu
"""
from __future__ import annotations
import argparse
from dataclasses import replace
import itertools
import json
import math
from pathlib import Path
import sys
import time

import numpy as np
from scipy.linalg import expm
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from triad_phase_frequency import PhaseFrequencyLikelihood, PhaseFrequencyModel


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def close(actual, expected, message, tolerance=2e-11):
    error = float(np.max(np.abs(np.asarray(actual)-np.asarray(expected))))
    check(error <= tolerance, f"{message}: maximum error {error}")


def independent_transition(model, frequencies, dt):
    """Tiny RW reference uses SciPy expm and explicit forward-state loops."""
    phases = model.phase_states; count = len(frequencies); step = 2*math.pi/phases
    qf = np.zeros((count, count))
    if count > 1 and model.frequency_model == "random_walk":
        rate = model.random_walk_hz_per_sqrt_second**2/(2*(frequencies[1]-frequencies[0])**2)
        for index in range(count-1):
            qf[index, index+1] = qf[index+1, index] = rate
        np.fill_diagonal(qf, -qf.sum(1))
    freq = expm(qf*dt)
    qp = np.zeros((phases, phases))
    rate = model.phase_diffusion_rad_per_sqrt_second**2/(2*step**2)
    for phase in range(phases):
        qp[phase, (phase+1) % phases] += rate
        qp[phase, (phase-1) % phases] += rate
    np.fill_diagonal(qp, -qp.sum(1))
    phase_diffusion = expm(qp*dt)

    def advected(phase, frequency):
        shift = frequency*dt*phases*.5
        lower = math.floor(shift); fraction = shift-lower
        return [((phase+lower) % phases, 1-fraction), ((phase+lower+1) % phases, fraction)]

    transition = np.zeros((count*phases, count*phases))
    for old_frequency, old_phase in itertools.product(range(count), range(phases)):
        for intermediate, p0 in advected(old_phase, frequencies[old_frequency]):
            for new_frequency in range(count):
                for final, p1 in advected(intermediate, frequencies[new_frequency]):
                    for new_phase in range(phases):
                        transition[old_frequency*phases+old_phase, new_frequency*phases+new_phase] += p0*freq[old_frequency, new_frequency]*p1*phase_diffusion[final, new_phase]
    return transition


def tiny_path_sum(device):
    model = PhaseFrequencyModel(phase_states=4, frequency_grid_hz=(-.5, 0., .5),
        frequency_model="random_walk", random_walk_hz_per_sqrt_second=.3,
        phase_diffusion_rad_per_sqrt_second=.2, amplitude_nodes=(1.,),
        amplitude_weights=(1.,), relative_phase_states=1, phase_kernel_tail_probability=0.)
    state = PhaseFrequencyLikelihood(1, 1, 1, model, device=device)
    frequencies = list(model.frequency_grid_hz); phases = np.arange(model.phase_states)*2*math.pi/model.phase_states
    observations = [.2+.15j, -.03+.2j, .25-.1j]; grams = [.4, .3, .2]; dts = [0., 1., .7]
    emissions = [np.tile(np.exp(2*(r*np.exp(-1j*phases)).real-g), len(frequencies)) for r, g in zip(observations, grams)]
    t1, t2 = [independent_transition(model, frequencies, dt) for dt in dts[1:]]
    size = len(frequencies)*model.phase_states
    direct = 0.
    for first, second, third in itertools.product(range(size), repeat=3):
        direct += emissions[0][first]*t1[first, second]*emissions[1][second]*t2[second, third]*emissions[2][third]/size
    for r, g, dt in zip(observations, grams, dts):
        state.update(torch.tensor([[[r]]], dtype=torch.complex128, device=device),
                     torch.tensor([[[g]]], dtype=torch.complex128, device=device), dt)
    actual = float(state.scores()[0, 0])
    close(actual, math.log(direct), "explicit three-block path sum")
    check(state.diagnostics()["maximum_phase_interpolation_variance_radians2_per_interval"] > 0,
          "fractional-phase transition did not report interpolation variance")
    return {"log_lr": actual, "direct_path_sum_log_lr": math.log(direct), "paths": size**3}


def direct_sample_density(device):
    model = PhaseFrequencyModel(phase_states=8, frequency_bound_hz=0., frequency_model="static",
        phase_diffusion_rad_per_sqrt_second=0., amplitude_nodes=(.7, 1.2),
        amplitude_weights=(.3, .7), relative_phase_states=3)
    state = PhaseFrequencyLikelihood(1, 1, 2, model, device=device)
    a = np.array([[1+.2j, .3-.1j], [.2+.3j, -.6+.4j], [.1-.7j, .8+.1j]], dtype=np.complex128)
    y = np.array([.1+.6j, -.2+.1j, .4-.3j]); variance = .7
    r, gram = a.conj().T@y/variance, a.conj().T@a/variance
    state.update(torch.tensor(r[None, None], device=device), torch.tensor(gram[None], device=device))
    likelihood = 0.
    for amplitude, weight in zip(model.amplitude_nodes, model.amplitude_weights):
        for relative in range(model.relative_phase_states):
            v = amplitude*np.array([math.sqrt(.5), math.sqrt(.5)*np.exp(2j*math.pi*relative/model.relative_phase_states)])
            for phase in range(model.phase_states):
                signal = a@(v*np.exp(2j*math.pi*phase/model.phase_states))
                likelihood += weight/model.relative_phase_states/model.phase_states*math.exp((np.vdot(y, y).real-np.vdot(y-signal, y-signal).real)/variance)
    close(float(state.scores()[0, 0]), math.log(likelihood), "raw complex Gaussian density mixture")
    return {"log_lr": float(state.scores()[0, 0]), "raw_density_log_lr": math.log(likelihood)}


def static_and_gap_limits(device):
    model = PhaseFrequencyModel(phase_states=16, frequency_bound_hz=0., frequency_model="static",
        phase_diffusion_rad_per_sqrt_second=0., amplitude_nodes=(.5, 1.5), amplitude_weights=(.4, .6))
    state = PhaseFrequencyLikelihood(1, 1, 1, model, device=device)
    r = torch.tensor([[[.12+.1j]], [[-.07+.2j]], [[.2-.05j]]], dtype=torch.complex128, device=device)
    g = torch.tensor([[[.1]], [[.2]], [[.15]]], dtype=torch.complex128, device=device)
    for index in range(3):
        state.update(r[index:index+1], g[index:index+1], 0. if index == 0 else 4.)
    combined = PhaseFrequencyLikelihood(1, 1, 1, model, device=device)
    combined.update(r.sum(0, keepdim=True), g.sum(0, keepdim=True))
    close(state.scores().cpu(), combined.scores().cpu(), "constant gain/phase equals whole-window mixture")
    before = state.scores().clone()
    zero_r, zero_g = torch.zeros((1, 1, 1), dtype=torch.complex128, device=device), torch.zeros((1, 1, 1), dtype=torch.complex128, device=device)
    for _ in range(10):
        state.update(zero_r, zero_g, 123.)
    check(torch.equal(before, state.scores()), "empty blocks added numerical evidence")
    dynamic_model = replace(model, frequency_grid_hz=(-.125, 0., .125), frequency_model="random_walk",
                            random_walk_hz_per_sqrt_second=.04, phase_diffusion_rad_per_sqrt_second=.02)
    dynamic = PhaseFrequencyLikelihood(1, 1, 1, dynamic_model, device=device)
    dynamic.update(r[:1], g[:1], 0.)
    posterior = dynamic.weights.clone(); evidence = dynamic.scores().clone()
    dynamic.update(zero_r, zero_g, 3.7)
    check(not torch.allclose(posterior, dynamic.weights), "gap froze oscillator uncertainty")
    check(torch.equal(evidence, dynamic.scores()), "dynamic gap added evidence")
    empty = PhaseFrequencyLikelihood(2, 2, 2, device=device)
    zeros = torch.zeros((2, 2, 2), dtype=torch.complex128, device=device)
    empty.update(zeros, torch.zeros((2, 2, 2), dtype=torch.complex128, device=device), 4.)
    check(torch.equal(empty.scores(), torch.zeros_like(empty.scores())), "zero observations did not give LR=1")
    check(empty.maximum_interpolation_variance_radians2 == 0., "aligned default grid added phase interpolation diffusion")
    return {"static_log_lr": float(before[0, 0]), "gap_score_unchanged": True, "aligned_interpolation_variance": empty.maximum_interpolation_variance_radians2}


def precision_and_persistence(device):
    model = PhaseFrequencyModel(phase_states=16, frequency_grid_hz=(-.03125, 0., .03125),
        frequency_model="ou", frequency_std_hz=.02, frequency_correlation_seconds=30.,
        phase_diffusion_rad_per_sqrt_second=.03, amplitude_nodes=(.5, 1.), amplitude_weights=(.5, .5), relative_phase_states=4)
    generator = torch.Generator().manual_seed(712)
    r = torch.complex(torch.randn((11, 3, 2, 2), generator=generator, dtype=torch.float64),
                      torch.randn((11, 3, 2, 2), generator=generator, dtype=torch.float64))*.05
    g = torch.tensor([[[.1, .02j], [-.02j, .12]], [[.09, .01], [.01, .08]]], dtype=torch.complex128)
    reference = PhaseFrequencyLikelihood(3, 2, 2, model, device=device)
    split = PhaseFrequencyLikelihood(3, 2, 2, model, device=device)
    fp32 = PhaseFrequencyLikelihood(3, 2, 2, model, device=device, dtype=torch.float32)
    for index in range(11):
        reference.update(r[index].to(device), g.to(device), 0. if index == 0 else 4.)
        fp32.update(r[index].to(device), g.to(device), 0. if index == 0 else 4.)
    for first, last in ((0, 3), (3, 7), (7, 11)):
        for index in range(first, last):
            split.update(r[index].to(device), g.to(device), 0. if index == 0 else 4.)
    close(reference.scores().cpu(), split.scores().cpu(), "persistent state across caller chunks", 0.)
    error = float((reference.scores()-fp32.scores()).abs().max())
    check(error < 2e-5, f"FP32/FP64 log evidence mismatch {error}")
    return {"maximum_fp32_fp64_log_lr_difference": error, "chunk_difference": 0.}


def frequency_chain_checks(device):
    model = PhaseFrequencyModel(phase_states=8, frequency_grid_hz=(-.004, -.002, 0., .002, .004),
        frequency_model="ou", frequency_std_hz=.002, frequency_correlation_seconds=100.,
        random_walk_hz_per_sqrt_second=0., amplitude_nodes=(1.,), amplitude_weights=(1.,))
    state = PhaseFrequencyLikelihood(1, 1, 1, model, device=device)
    frequency = state.frequency_grid_cpu
    stationary = torch.softmax(-frequency.square()/(2*model.frequency_std_hz**2), dim=0)
    flux = stationary[:, None]*state.frequency_generator
    close(flux, flux.transpose(0, 1), "OU detailed balance", 1e-17)
    close(stationary@state._transition(100.)["frequency"].cpu(), stationary,
          "OU stationary frequency distribution", 2e-15)
    tiny = replace(model, frequency_grid_hz=(-.001, 0., .001), frequency_model="random_walk",
                   random_walk_hz_per_sqrt_second=1e-6)
    walker = PhaseFrequencyLikelihood(1, 1, 1, tiny, device=device)
    transition = walker._transition(100.)["frequency"].cpu()
    variance = float(transition[1]@walker.frequency_grid_cpu.square())
    expected = 100*tiny.random_walk_hz_per_sqrt_second**2
    check(abs(variance/expected-1) < 1e-3,
          "sub-grid random-walk diffusion froze or lost its accumulated variance")
    return {"subgrid_rw_variance": variance, "continuous_small_step_variance": expected,
            "ou_detailed_balance_max_error": float((flux-flux.T).abs().max())}


def null_mean_diagnostic(device):
    count = 12000
    model = PhaseFrequencyModel(phase_states=8, frequency_grid_hz=(-.0625, 0., .0625),
        frequency_model="random_walk", random_walk_hz_per_sqrt_second=.03,
        phase_diffusion_rad_per_sqrt_second=.1, amplitude_nodes=(.7, 1.2), amplitude_weights=(.5, .5))
    state = PhaseFrequencyLikelihood(count, 1, 1, model, device=device, validate_inputs=False)
    generator = torch.Generator().manual_seed(11923)
    g = torch.tensor([[[.15]]], dtype=torch.complex128, device=device)
    for index in range(2):
        real = torch.randn((count, 1, 1), generator=generator, dtype=torch.float64)
        imag = torch.randn((count, 1, 1), generator=generator, dtype=torch.float64)
        r = torch.complex(real, imag)*math.sqrt(.15/2)
        state.update(r.to(device), g, 0. if index == 0 else 4.)
    lr = state.scores().exp()
    mean, se = float(lr.mean()), float(lr.std(unbiased=True))/math.sqrt(count)
    check(abs(mean-1) < 5*se+.002, f"null mean-one diagnostic failed: mean={mean}, SE={se}")
    return {"trials": count, "mean_lr": mean, "standard_error": se,
            "scope": "moderate-SNR mean diagnostic, not rare-tail or operational false-admission qualification"}


def validation_guards(device):
    failures = 0
    calls = [lambda: PhaseFrequencyLikelihood(1, model=PhaseFrequencyModel(frequency_bound_hz=1e100), device=device),
             lambda: PhaseFrequencyLikelihood(10000, device=device, memory_limit_bytes=1000),
             lambda: PhaseFrequencyLikelihood(1, model=PhaseFrequencyModel(amplitude_weights=(0., 0., 0.)), device=device),
             lambda: PhaseFrequencyLikelihood(1, model=PhaseFrequencyModel(frequency_grid_hz=(0., .1, .3)), device=device)]
    for call in calls:
        try:
            call()
        except ValueError:
            failures += 1
        else:
            raise AssertionError("invalid model accepted")
    state = PhaseFrequencyLikelihood(1, 1, 1, device=device)
    try:
        state.update(torch.zeros((1, 1, 1), dtype=torch.complex128, device=device),
                     torch.tensor([[[-1.]]], dtype=torch.complex128, device=device))
    except ValueError:
        failures += 1
    else:
        raise AssertionError("negative Gram accepted")
    try:
        state.update(torch.zeros((1, 1, 1), dtype=torch.complex128, device=device, requires_grad=True),
                     torch.ones((1, 1, 1), dtype=torch.complex128, device=device))
    except ValueError:
        failures += 1
    else:
        raise AssertionError("autograd graph could grow across streaming updates")
    return {"expected_rejections": failures}


def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--device", default="cpu")
    args = parser.parse_args(); torch.set_num_threads(2)
    torch.set_float32_matmul_precision("highest"); torch.backends.cuda.matmul.allow_tf32 = False
    started = time.perf_counter(); results = {}
    for function in (tiny_path_sum, direct_sample_density, static_and_gap_limits,
                     precision_and_persistence, frequency_chain_checks, null_mean_diagnostic, validation_guards):
        results[function.__name__] = function(args.device)
    print(json.dumps({"device": args.device, "torch": torch.__version__, "passed": len(results),
                      "wall_seconds": time.perf_counter()-started, "checks": results}, indent=2))


if __name__ == "__main__":
    main()
