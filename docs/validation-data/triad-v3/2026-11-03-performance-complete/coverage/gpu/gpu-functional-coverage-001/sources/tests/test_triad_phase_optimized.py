#!/usr/bin/env python3
"""Paired optional phase-backend checks; CUDA skips are reported explicitly."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from triad_phase_frequency import PhaseFrequencyLikelihood, PhaseFrequencyModel
from triad_phase_optimized import GraphPhaseFrequencyLikelihood, OptimizedPhaseFrequencyLikelihood


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def paired(device, dtype, *, fractional):
    model = PhaseFrequencyModel(phase_states=32,
        frequency_grid_hz=(-.03125, 0., .03125), nominal_dt_seconds=4.,
        frequency_std_hz=.02, frequency_correlation_seconds=30.,
        phase_diffusion_rad_per_sqrt_second=.03,
        amplitude_nodes=(.5, 1., 2.), amplitude_weights=(.25, .5, .25),
        relative_phase_states=4)
    kwargs = dict(device=device, dtype=dtype, validate_inputs=False)
    reference = PhaseFrequencyLikelihood(3, 2, 2, model, **kwargs)
    optimized = OptimizedPhaseFrequencyLikelihood(3, 2, 2, model, **kwargs)
    generator = torch.Generator().manual_seed(691032)
    r = torch.complex(torch.randn((19, 3, 2, 2), generator=generator, dtype=torch.float64),
                      torch.randn((19, 3, 2, 2), generator=generator, dtype=torch.float64))*.12
    gram = torch.tensor([[[.13, .02j], [-.02j, .12]],
                         [[.09, .01], [.01, .08]]], dtype=torch.complex128, device=device)
    worst_score, worst_weight = 0., 0.
    for i in range(len(r)):
        dt = 0. if i == 0 else (3.7 if fractional and i % 3 else 4.)
        observation = r[i].to(device)
        current_gram = gram
        if i in (7, 8):
            observation = torch.zeros_like(observation)
            current_gram = torch.zeros_like(gram)
        before = optimized.scores()
        reference.update(observation, current_gram, dt)
        optimized.update(observation, current_gram, dt)
        if i in (7, 8):
            check(torch.equal(before, optimized.scores()), "empty block added evidence")
        worst_score = max(worst_score, float((reference.scores()-optimized.scores()).abs().max()))
        worst_weight = max(worst_weight, float((reference.weights-optimized.weights).abs().max()))
    tolerance = 4e-5 if dtype == torch.float32 else 3e-11
    check(worst_score <= tolerance, f"paired score error {worst_score}")
    check(worst_weight <= tolerance, f"paired posterior error {worst_weight}")
    check(torch.equal(reference.scores().argmax(-1), optimized.scores().argmax(-1)), "paired ranking changed")
    for threshold in (-2., 0., 2., 17.175365):
        check(torch.equal(reference.scores() >= threshold, optimized.scores() >= threshold),
              "paired admission changed")
    check(torch.isfinite(optimized.weights).all(), "nonfinite posterior")
    diagnostic = optimized.diagnostics()
    check(diagnostic["optimized_stencil_calls"] == len(r)-1, "stencil call accounting")
    check(diagnostic["optimized_fractional_first_advections"] > 0 if fractional else
          diagnostic["optimized_fractional_first_advections"] == 0, "fractional branch accounting")
    keys = ("backend", "gpu_optimized_execution", "optimized_transition_preparations",
            "optimized_stencil_calls", "optimized_integral_first_advections",
            "optimized_fractional_first_advections", "maximum_phase_stencil_support")
    return dict(dtype=str(dtype), fractional=fractional, maximum_score_error=worst_score,
                maximum_posterior_error=worst_weight,
                diagnostics={key: diagnostic[key] for key in keys})


def direct_transition(device):
    """Independent explicit transition matrix checks the fused operation."""
    from test_triad_phase_frequency import independent_transition
    model = PhaseFrequencyModel(phase_states=8, frequency_grid_hz=(-.125, 0., .125),
        frequency_model="random_walk", random_walk_hz_per_sqrt_second=.04,
        phase_diffusion_rad_per_sqrt_second=.07, amplitude_nodes=(1.,),
        amplitude_weights=(1.,), relative_phase_states=1, phase_kernel_tail_probability=0.)
    state = OptimizedPhaseFrequencyLikelihood(2, 1, 1, model, device=device)
    generator = torch.Generator().manual_seed(7017)
    weights = torch.rand(state.weights.shape, generator=generator, dtype=torch.float64).to(device)
    weights /= weights.sum(dim=(-3, -2, -1), keepdim=True)
    errors = []
    for dt in (0., 4., 3.7, 23.):
        matrix = torch.as_tensor(independent_transition(model, model.frequency_grid_hz, dt),
                                 dtype=torch.float64, device=device)
        expected = (weights.reshape(2, -1) @ matrix).reshape_as(weights)
        expected /= expected.sum(dim=(-3, -2, -1), keepdim=True)
        actual = state._predict(weights, dt)
        error = float((actual-expected).abs().max())
        check(error < 5e-13, f"independent transition error {error}")
        errors.append(error)
    return dict(maximum_absolute_error=max(errors), intervals=[0., 4., 3.7, 23.])


def high_dynamic_range(device):
    model = PhaseFrequencyModel(phase_states=32, frequency_bound_hz=0., frequency_model="static",
        phase_diffusion_rad_per_sqrt_second=0., amplitude_nodes=(.5, 1., 4.),
        amplitude_weights=(.25, .5, .25), relative_phase_states=1)
    reference = PhaseFrequencyLikelihood(1, 2, 1, model, device=device)
    optimized = OptimizedPhaseFrequencyLikelihood(1, 2, 1, model, device=device)
    r = torch.tensor([[[1000.+30j], [-300.+12j]]], dtype=torch.complex128, device=device)
    g = torch.tensor([[[100.]], [[100.]]], dtype=torch.complex128, device=device)
    for i in range(12):
        reference.update(r, g, 0. if i == 0 else 4.)
        optimized.update(r, g, 0. if i == 0 else 4.)
    error = float((reference.scores()-optimized.scores()).abs().max())
    check(error < 1e-9, "high-dynamic-range score mismatch")
    check(torch.isfinite(optimized.scores()).all(), "high-dynamic-range nonfinite score")
    return dict(maximum_score_error=error)


def long_recurrence(device):
    """Many updates with absolute elapsed time; no sampled-channel claim."""
    model = PhaseFrequencyModel(phase_states=16, frequency_grid_hz=(-.001, 0., .001),
        frequency_std_hz=.001, frequency_correlation_seconds=1000.,
        phase_diffusion_rad_per_sqrt_second=.002, amplitude_nodes=(.5, 1.),
        amplitude_weights=(.5, .5), relative_phase_states=1)
    generator = torch.Generator().manual_seed(21727)
    correlations = torch.complex(
        torch.randn((1025, 2, 2, 1), generator=generator, dtype=torch.float64),
        torch.randn((1025, 2, 2, 1), generator=generator, dtype=torch.float64))*.003
    gram = torch.full((2, 1, 1), .0001, dtype=torch.complex128, device=device)
    outputs = []
    for dtype in (torch.float64, torch.float32):
        reference = PhaseFrequencyLikelihood(2, 2, 1, model, dtype=dtype, device=device,
                                             validate_inputs=False)
        optimized = OptimizedPhaseFrequencyLikelihood(2, 2, 1, model, dtype=dtype, device=device,
                                                      validate_inputs=False)
        for first, last in ((0, 257), (257, 513), (513, 769), (769, 1025)):
            for i in range(first, last):
                # 1024 nonzero intervals cover two days of continuous model
                # coordinates.  This is a state arithmetic test, not sampled
                # physical hour/day validation.
                dt = 0. if i == 0 else 168.75
                observation = correlations[i].to(device)
                reference.update(observation, gram, dt)
                optimized.update(observation, gram, dt)
        error = float((reference.scores()-optimized.scores()).abs().max())
        tolerance = 1e-8 if dtype == torch.float64 else 5e-4
        check(error < tolerance, f"long recurrence score error {error}")
        check(optimized.elapsed_seconds == 172800., "absolute elapsed model state was lost")
        outputs.append(dict(dtype=str(dtype), updates=1025, elapsed_center_seconds=172800.,
                            maximum_score_error=error))
    return outputs


def graph_checks(device):
    if device.type != "cuda":
        try:
            GraphPhaseFrequencyLikelihood(1, device="cpu")
        except RuntimeError as error:
            check("unsupported on CPU" in str(error), "CPU graph rejection was not explicit")
        else:
            raise AssertionError("CUDA graph backend silently used CPU")
        return dict(status="unexecuted", reason="CUDA graph unsupported on CPU; rejection checked")
    rows = []
    for dtype in (torch.float64, torch.float32):
        model = PhaseFrequencyModel(phase_states=16, frequency_grid_hz=(-.03125, 0., .03125),
            frequency_std_hz=.02, frequency_correlation_seconds=30.,
            phase_diffusion_rad_per_sqrt_second=.03, amplitude_nodes=(.5, 1.),
            amplitude_weights=(.5, .5), relative_phase_states=2, transition_cache_entries=2)
        kwargs = dict(device=device, dtype=dtype, validate_inputs=False)
        reference = OptimizedPhaseFrequencyLikelihood(2, 2, 2, model, **kwargs)
        graphed = GraphPhaseFrequencyLikelihood(2, 2, 2, model, **kwargs)
        generator = torch.Generator().manual_seed(327773)
        values = torch.complex(torch.randn((10, 2, 2, 2), generator=generator, dtype=torch.float64),
                               torch.randn((10, 2, 2, 2), generator=generator, dtype=torch.float64))*.07
        values = values.to(device)
        gram = torch.tensor([[[.13, .02j], [-.02j, .12]],
                             [[.09, .01], [.01, .08]]], dtype=torch.complex128, device=device)
        dts = [0., 4., 4., 3.7, 4., 3.7, 12., 4., 3.7, 4.]
        max_score = max_weight = 0.
        for index, dt in enumerate(dts):
            r, g = values[index], gram
            if index in (3, 6):
                r, g = torch.zeros_like(r), torch.zeros_like(g)
            before = graphed.scores()
            reference.update(r, g, dt)
            graphed.update(r, g, dt)
            if index in (3, 6):
                check(torch.equal(before, graphed.scores()), "graph empty block added evidence")
            max_score = max(max_score, float((reference.scores()-graphed.scores()).abs().max()))
            max_weight = max(max_weight, float((reference.weights-graphed.weights).abs().max()))
            check(graphed.updates == index+1, "graph warmup/capture changed public update count")
        tolerance = 1e-10 if dtype == torch.float64 else 4e-5
        check(max_score < tolerance and max_weight < tolerance, "graph paired state differs")
        expected_scores, expected_weights = graphed.scores(), graphed.weights.clone()
        graph_count = graphed.diagnostics()["graph_captures"]
        graphed.reset()
        check(torch.equal(graphed.scores(), torch.zeros_like(graphed.scores())), "graph reset did not clear scores")
        check(graphed.updates == 0 and graphed.elapsed_seconds == 0., "graph reset lost public counters")
        for index, dt in enumerate(dts):
            r, g = values[index], gram
            if index in (3, 6):
                r, g = torch.zeros_like(r), torch.zeros_like(g)
            graphed.update(r, g, dt)
        check(torch.equal(expected_scores, graphed.scores()), "graph explicit reset/replay changed scores")
        check(torch.equal(expected_weights, graphed.weights), "graph explicit reset/replay changed posterior")
        diagnostic = graphed.diagnostics()
        check(diagnostic["graph_cache_entries"] <= 2, "graph cache exceeded bound")
        check(diagnostic["graph_cache_evictions"] > 0, "graph LRU eviction path was not exercised")
        check(diagnostic["graph_replays"] == 2*len(dts), "graph replay accounting")
        check(diagnostic["graph_resets"] == 1, "graph reset accounting")
        # Switching streams would violate the declared persistent-buffer order.
        try:
            with torch.cuda.stream(torch.cuda.Stream(device=device)):
                graphed.update(values[0], gram, 4.)
        except RuntimeError as error:
            check("construction stream" in str(error), "stream mismatch did not fail explicitly")
        else:
            raise AssertionError("graph receiver accepted unordered stream change")
        rows.append(dict(dtype=str(dtype), maximum_score_error=max_score,
                         maximum_posterior_error=max_weight, first_pass_captures=graph_count,
                         diagnostics={key: diagnostic[key] for key in (
                             "graph_captures", "graph_replays", "graph_cache_entries",
                             "graph_cache_evictions", "graph_warmup_synchronized_wall_seconds",
                             "graph_capture_synchronized_wall_seconds", "graph_peak_working_bytes_estimate")}))
    return dict(status="passed", rows=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", default="cpu")
    args = parser.parse_args()
    torch.set_num_threads(2)
    torch.set_float32_matmul_precision("highest")
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    device = torch.device(args.device)
    if device.type == "cuda" and not torch.cuda.is_available():
        print(json.dumps(dict(status="skipped", device=str(device), reason="CUDA unavailable")))
        return
    results = dict(status="passed", device=str(device),
        paired=[paired(device, dtype, fractional=fractional)
                for dtype in (torch.float64, torch.float32) for fractional in (False, True)],
        direct_transition=direct_transition(device), high_dynamic_range=high_dynamic_range(device),
        long_recurrence=long_recurrence(device),
        cuda_graph=graph_checks(device),
        scope="numerical and independent finite-model checks; no sensitivity or operational tail qualification")
    if device.type == "cpu":
        results["cuda_check"] = dict(status="unexecuted", reason="CPU invocation; use --device cuda:0 on supported hardware")
    print(json.dumps(results, allow_nan=False))


if __name__ == "__main__":
    main()
