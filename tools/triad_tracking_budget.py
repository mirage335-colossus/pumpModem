#!/usr/bin/env python3
"""Bounded work and workspace forecasts for isolated streaming timing tracking.

Work counts describe a declared receiver configuration; they are not CPU-time
predictions. A timing forecast is available only from a source-, hardware-, and
algorithm-regime-matched measurement whose stage work scales uniformly.
"""
from __future__ import annotations

import math
from statistics import median


_U64 = (1 << 64) - 1
_WORK = ("sample_rows", "segments", "basis_row_samples", "gaussian_updates",
         "transition_edges")


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
                          reason="no source-, hardware-, and regime-matched timing calibration supplied"))


def forecast_tracking_cost(work, calibration=None, identity=None, *,
                           cpu_ceiling_seconds=None, memory_ceiling_bytes=None,
                           ratio_tolerance=.05):
    """Attach a deliberately narrow, empirical work-scaled CPU forecast.

    A calibration has ``identity``, ``work`` (the five integer work counters),
    and ``cpu_seconds``. Its identity must exactly match the supplied source,
    hardware and algorithm regime. Every nonzero calibration work counter must
    be positive; all target/calibration work ratios must lie within the declared
    tolerance of their median. The resulting range is an engineering envelope,
    not a statistical confidence interval or universal runtime bound.
    """
    if not isinstance(work, dict) or work.get("schema") != "triad-tracking-work-v1":
        raise ValueError("a triad-tracking-work-v1 report is required")
    if not 0 <= _finite(ratio_tolerance, "ratio tolerance", nonnegative=True) <= .25:
        raise ValueError("ratio tolerance must be in [0,.25]")
    if cpu_ceiling_seconds is not None:
        cpu_ceiling_seconds = _finite(cpu_ceiling_seconds, "CPU ceiling", positive=True)
    if memory_ceiling_bytes is not None:
        memory_ceiling_bytes = _integer(memory_ceiling_bytes, "memory ceiling")

    memory_bytes = work["workspace"]["receiver_estimate_bytes"]
    memory = dict(available=memory_ceiling_bytes is not None,
                  ceiling_bytes=memory_ceiling_bytes,
                  estimated_bytes=memory_bytes,
                  status=("unknown" if memory_ceiling_bytes is None else
                          "exceeded" if memory_bytes > memory_ceiling_bytes else "within"),
                  confidence="receiver estimate only; excludes allocator/RSS and external workspaces")
    cpu = dict(available=False, cpu_seconds=None, interval_seconds=None,
               status="unknown", confidence="none",
               reason="no source-, hardware-, and regime-matched timing calibration supplied")
    if calibration is not None:
        if not isinstance(calibration, dict):
            raise ValueError("calibration must be an object")
        def meaningful(value):
            if value is None:
                return False
            if isinstance(value, str):
                return bool(value.strip())
            if isinstance(value, (dict, list, tuple, set)):
                return bool(value)
            return True
        def valid_identity(value):
            return (isinstance(value, dict) and
                    all(key in value and meaningful(value[key])
                        for key in ("source", "hardware", "regime")))
        if not valid_identity(identity) or not valid_identity(calibration.get("identity")):
            cpu["reason"] = "calibration requires meaningful source, hardware, and regime identity"
        elif calibration.get("identity") != identity:
            cpu["reason"] = "source/hardware/algorithm-regime identity mismatch"
        elif not isinstance(calibration.get("work"), dict):
            cpu["reason"] = "calibration lacks measured work counters"
        else:
            try:
                measured_cpu = _finite(calibration.get("cpu_seconds"),
                                       "calibration CPU seconds", positive=True)
                ratios = []
                for key in _WORK:
                    target = _integer(work["work"][key], f"target {key}", low=0)
                    measured = _integer(calibration["work"].get(key),
                                        f"calibration {key}", low=0)
                    if target == measured == 0:
                        continue
                    if target == 0 or measured == 0:
                        raise ValueError(f"{key} work regime differs")
                    ratios.append(target / measured)
                if not ratios:
                    raise ValueError("no measured work counters")
                center = median(ratios)
                low, high = min(ratios), max(ratios)
                if center <= 0 or (high-low) / center > ratio_tolerance:
                    raise ValueError("work-counter ratios are not uniformly scalable")
                estimate_low, estimate_high = measured_cpu*low, measured_cpu*high
                cpu.update(available=True, cpu_seconds=measured_cpu*center,
                    interval_seconds=[estimate_low, estimate_high], status="estimated",
                    confidence="unquantified engineering envelope; not a statistical confidence interval",
                    ratio_tolerance=ratio_tolerance,
                    work_scale_ratios={key: target / measured for key, target, measured in
                        ((key, work["work"][key], calibration["work"][key])
                         for key in _WORK if work["work"][key] or calibration["work"][key])},
                    calibration_scope=calibration.get("scope", "unspecified"))
            except ValueError as error:
                cpu["reason"] = str(error)
    if cpu_ceiling_seconds is not None:
        cpu["ceiling_seconds"] = cpu_ceiling_seconds
        cpu["ceiling_status"] = ("unknown" if not cpu["available"] else
            "exceeded" if cpu["interval_seconds"][0] > cpu_ceiling_seconds else
            "within" if cpu["interval_seconds"][1] <= cpu_ceiling_seconds else "uncertain")
    result = dict(work)
    result["cpu_forecast"] = cpu
    result["memory_forecast"] = memory
    return result
