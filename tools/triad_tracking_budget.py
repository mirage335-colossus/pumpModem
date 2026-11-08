#!/usr/bin/env python3
"""Bounded work and workspace forecasts for isolated streaming timing tracking.

Work counts describe a declared receiver configuration; they are not CPU-time
predictions. Runtime forecasts belong to the hierarchical abbreviated harness.
"""
from __future__ import annotations

import math


_U64 = (1 << 64) - 1


def _integer(value, name, low=1, high=_U64):
    if isinstance(value, bool) or not isinstance(value, int) or not low <= value <= high:
        raise ValueError(f"{name} must be an integer in [{low},{high}]")
    return value


def _finite(value, name, *, positive=False, nonnegative=False):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{name} must be finite")
    value = float(value)
    if (not math.isfinite(value) or (positive and value <= 0) or
            (nonnegative and value < 0)):
        raise ValueError(f"{name} must be finite and in range")
    return value


def _block_count(rows, block_rows):
    # Match IncrementalGaussianReceiver: one short block when rows < B;
    # otherwise floor(rows/B), with a short final remainder merged backward.
    return 1 if rows < block_rows else rows // block_rows


def count_tracking_work(*, samples, sample_rate_hz, block_samples,
                        segment_samples, initial_states, maximum_states,
                        maximum_proposals, rate_index_bound, models, taps,
                        precision, projection_mode, workspace_bytes,
                        fine_symbol_rate_hz=0., data_if_hz=0.,
                        maximum_relative_rate_ppm=0.):
    """Return exact calendar counts and conservative configured work bounds.

    ``basis_row_samples`` counts two DATA alternatives times active timing
    states, channel taps, and every received row. ``gaussian_updates`` counts
    three hypotheses (noise plus both bits), models, states, and projected
    receiver blocks. The first segment starts with ``initial_states``; following
    segments can enter scoring with ``maximum_proposals`` states. Transition
    edge visits include both persistent-tracker stencil traversals, using the
    retained-state cap and finite public rate stencil. Bounds exclude backend
    kernels, template-cache preparation, Python dispatch and allocator overhead.
    Caller-supplied workspace is the receiver's own preflight estimate, not RSS.
    """
    samples = _integer(samples, "sample count")
    block_samples = _integer(block_samples, "block size")
    segment_samples = _integer(segment_samples, "segment size")
    initial_states = _integer(initial_states, "initial states")
    maximum_states = _integer(maximum_states, "maximum states")
    maximum_proposals = _integer(maximum_proposals, "maximum proposals")
    rate_index_bound = _integer(rate_index_bound, "rate index bound", low=0, high=2048)
    models = _integer(models, "model count", high=64)
    taps = _integer(taps, "tap count", high=16)
    workspace_bytes = _integer(workspace_bytes, "workspace bytes")
    if maximum_proposals < maximum_states or initial_states > maximum_proposals:
        raise ValueError("initial states and maximum states must fit the proposal cap")
    if precision not in ("fp32", "fp64"):
        raise ValueError("precision must be fp32 or fp64")
    if projection_mode not in ("dft", "valid-rows"):
        raise ValueError("unsupported projection mode")
    sample_rate_hz = _finite(sample_rate_hz, "sample rate", positive=True)
    fine_symbol_rate_hz = _finite(fine_symbol_rate_hz, "fine symbol rate", nonnegative=True)
    data_if_hz = _finite(data_if_hz, "DATA IF", nonnegative=True)
    maximum_relative_rate_ppm = _finite(maximum_relative_rate_ppm,
                                         "relative rate bound", nonnegative=True)
    if maximum_relative_rate_ppm >= 1_000_000:
        raise ValueError("relative rate bound must be below one million ppm")

    segments = (samples + segment_samples - 1) // segment_samples
    full_segments, last_rows = divmod(samples, segment_samples)
    full_segment_blocks = _block_count(segment_samples, block_samples)
    tail_blocks = _block_count(last_rows, block_samples) if last_rows else 0
    blocks = full_segments * full_segment_blocks + tail_blocks
    first_rows = min(samples, segment_samples)
    initial_uses = _block_count(first_rows, block_samples)
    later_blocks = blocks - initial_uses
    # Following a prediction, the retained lattice can grow to the proposal
    # cap before observation/pruning, and all proposals are scored at that
    # segment. Bound that actual peak even when it exceeds the post-prune cap.
    active_state_blocks = initial_states * initial_uses + maximum_proposals * later_blocks
    # Separate first-segment exposure so the exact count can be reproduced.
    first_cells = 2 * taps * first_rows * initial_states
    remaining_cells = 2 * taps * (samples - first_rows) * maximum_proposals
    basis_cells = first_cells + remaining_cells
    gaussian_updates = 3 * models * active_state_blocks
    transition_edges = (4 * (2 * rate_index_bound + 1) * maximum_states *
                        max(0, segments - 1))
    maximum_active_states = max(initial_states, maximum_proposals)
    duration_seconds = samples / sample_rate_hz
    if not math.isfinite(duration_seconds):
        raise ValueError("observation duration is not representable as a finite value")
    relative_rate = maximum_relative_rate_ppm * 1e-6
    timing_uncertainty = dict(
        maximum_relative_rate_ppm=maximum_relative_rate_ppm,
        maximum_audio_if_shift_hz=data_if_hz * relative_rate,
        maximum_fine_symbol_rate_shift_hz=fine_symbol_rate_hz * relative_rate,
        maximum_envelope_timing_drift_seconds=duration_seconds * relative_rate)
    if not all(math.isfinite(value) for value in timing_uncertainty.values()):
        raise ValueError("configured timing motion is not representable as a finite value")
    return dict(
        schema="triad-tracking-work-v1",
        observation=dict(samples=samples, sample_rate_hz=sample_rate_hz,
                         duration_seconds=duration_seconds,
                         fine_symbol_rate_hz=fine_symbol_rate_hz, data_if_hz=data_if_hz),
        receiver=dict(block_samples=block_samples, segment_samples=segment_samples,
                      segments=segments, likelihood_blocks=blocks,
                      initial_states=initial_states, maximum_states=maximum_states,
                      maximum_proposals=maximum_proposals, maximum_active_states=maximum_active_states,
                      models=models, taps=taps, precision=precision,
                      projection_mode=projection_mode, rate_index_bound=rate_index_bound),
        work=dict(sample_rows=samples, segments=segments, likelihood_blocks=blocks,
                  basis_row_samples=basis_cells, gaussian_updates=gaussian_updates,
                  transition_edges=transition_edges,
                  bound_scope="O(1) calendar calculation. First segment uses initial states; following segments use proposal-cap candidates before pruning. Transition edge visits count both tracker traversals. Excludes synthesis/cache, DSP kernel constants, Python overhead and concurrency scaling."),
        workspace=dict(receiver_estimate_bytes=workspace_bytes,
                        confidence="caller-supplied receiver estimate; allocator/RSS not covered"),
        timing_uncertainty=timing_uncertainty,
        cpu_forecast=dict(available=False, cpu_seconds=None, interval_seconds=None,
                          status="unmeasured", confidence="none",
                          reason="runtime forecasts require the hierarchical abbreviated harness"))
