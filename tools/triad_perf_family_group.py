"""Performance adapter for the maintained actual-observation grouped receiver.

Only the outer CFO loop may be shortened. Every call retains the entire sampled
capture and every synchronization group. Shortened calls are timing evidence,
not complete-family receiver decisions. The existing receiver owns all DSP.
"""
from __future__ import annotations

from dataclasses import asdict, replace
import math
from pathlib import Path
import time


SOURCE_FILES = (
    "triad_group_acquire.py", "triad_gaussian_window.py",
    "triad_noise_likelihood.py", "triad_state_workspace.py",
    "triad_gaussian_state.py",
)


def _one_fixture(fixture):
    if isinstance(fixture, tuple):
        if len(fixture) != 1:
            raise ValueError("grouped acquisition requires exactly one physical workspace")
        fixture = fixture[0]
    return Path(fixture)


def run_case(root: Path, fixture: Path | tuple[Path, ...], case: dict,
             device: str, precision: str, limit: int | None = None) -> dict:
    """Score a full CFO family, or an explicitly incomplete timing prefix.

    ``options`` contains AcquisitionConfig field names except device/precision,
    supplied separately. JSON lists are accepted for the two tuple fields.
    Training counts must divide calibration_segments * full CFO count so the
    public calibration allocation can remain unchanged with integer budgets.
    Zero CFO is a separate branch: use its own one-offset case.
    """
    from triad_perf_families import load_source

    started = time.perf_counter()
    receiver = load_source(Path(root), "triad_group_acquire.py")
    workspace = receiver.GroupedWorkspace(_one_fixture(fixture))
    loaded = time.perf_counter()

    if case.get("unit", "cfo") != "cfo":
        raise ValueError("grouped acquisition work unit must be cfo")
    options = dict(case.get("options", {}))
    if "device" in options or "precision" in options:
        raise ValueError("device and precision must be supplied to run_case")
    for name in ("stationary_power", "frequency_offsets_hz"):
        if name in options:
            options[name] = tuple(options[name])
    full_config = receiver.AcquisitionConfig(device=device, precision=precision, **options)
    offsets = full_config.frequency_offsets_hz
    full_count = len(offsets)
    if any(value == 0 for value in offsets) and full_count != 1:
        raise ValueError("zero and nonzero CFO work must use separate performance cases")
    if limit is not None and (isinstance(limit, bool) or not isinstance(limit, int)
                              or not 1 <= limit <= full_count):
        raise ValueError("CFO limit must be an integer in 1..full CFO count, or None")
    count = full_count if limit is None else limit
    numerator = full_config.calibration_segments * full_count
    if numerator % count:
        raise ValueError("CFO limit cannot preserve the integer full-family calibration allocation")
    # The native implementation retains count-sized result vectors. Reserve
    # their omitted bytes in the budget, keeping exactly the same allowance
    # for Gaussian preparation/scoring. No artificial allocation is timed.
    per_group_result_bytes = 96 + 8 * workspace.dimension
    omitted_result_bytes = (full_count - count) * workspace.groups * per_group_result_bytes
    if omitted_result_bytes >= 1024 * 1024:
        raise ValueError("abbreviation omits at least 1 MiB of retained result arrays; use the full run")
    config = replace(full_config, frequency_offsets_hz=offsets[:count],
                     calibration_segments=numerator // count,
                     memory_limit_bytes=full_config.memory_limit_bytes - omitted_result_bytes)
    full_delta = math.nextafter(full_config.budgets()[0].delta_per_segment /
                               (workspace.groups * full_count), 0.)
    selected_delta = math.nextafter(config.budgets()[0].delta_per_segment /
                                   (workspace.groups * count), 0.)
    # Integer reallocation can differ by a few roundoff ulps because each
    # budget applies conservative nextafter before division. Never hide it.
    if not math.isclose(selected_delta, full_delta, rel_tol=4e-16, abs_tol=0.):
        raise ValueError("CFO prefix changed the full-family residual-UCB delta")
    if workspace.groups * full_count > receiver.MAX_EFFECTIVE_GROUPS:
        raise ValueError("full timing/frequency bank exceeds native effective-group cap")
    receiver.frequency_reuse_bound(offsets,
        min(workspace.block_samples, workspace.sample_count) / workspace.sample_rate,
        full_config.maximum_block_phase_loss_db)
    torch = receiver.torch
    torch.set_float32_matmul_precision("highest")
    if device.startswith("cuda"):
        if not torch.cuda.is_available():
            raise ValueError("requested CUDA device is unavailable; no CPU fallback")
        torch.cuda.set_device(device)
        torch.backends.cuda.matmul.allow_tf32 = False
        torch.cuda.synchronize(device)
    prepared = time.perf_counter()

    # score_workspace interleaves residual calibration, factor preparation,
    # transfers, score calls and final mixing. Preserve this real API boundary.
    report = receiver.score_workspace(workspace, config)
    if device.startswith("cuda"):
        torch.cuda.synchronize(device)
    scored = time.perf_counter()

    if report["calibration"]["delta_per_group"] != selected_delta:
        raise AssertionError("native calibration differs from preflight")
    geometry = report["geometry"]
    resources = report["resources"]
    complete = count == full_count
    blocks = geometry["block_count"]
    tile = resources["group_tile"]
    window = geometry["window_blocks"]
    group_tiles = (workspace.groups + tile - 1) // tile
    windows = (blocks + window - 1) // window
    projection_tiles = (blocks + config.projection_tile_blocks - 1) // config.projection_tile_blocks
    # Regime contains only target configuration and per-CFO geometry. Variable
    # work, count-sized outputs and the budget reservation are reported in work.
    regime = {
        "adapter": "grouped-workspace-v1",
        "input_format": workspace.manifest["format"],
        "sample_count": workspace.sample_count,
        "sample_rate": workspace.sample_rate,
        "source_blocks": workspace.blocks,
        "source_block_samples": workspace.block_samples,
        "source_tail_samples": workspace.sample_count % workspace.block_samples,
        "source_shapes": {name: shape for name, (_, _, shape) in workspace._spec.items()},
        "source_dtypes": {name: dtype for name, (_, dtype, _) in workspace._spec.items()},
        "base_groups": workspace.groups,
        "group_dimension": workspace.dimension,
        "reblocked_blocks": blocks,
        "reblock_tail_source_blocks": workspace.blocks % config.reblock,
        "group_tile": tile,
        "tail_group_tile": workspace.groups % tile,
        "window_blocks": window,
        "tail_window_blocks": blocks % window,
        "projection_tile_blocks": config.projection_tile_blocks,
        "tail_projection_tile_blocks": blocks % config.projection_tile_blocks,
        "correlation_tile_shape": [blocks, tile, 2, 2],
        "gram_tile_shape": [blocks, tile, 2, 2, 2],
        "frequency_branch": "zero" if offsets == (0.,) else "nonzero",
        "full_cfo_count": full_count,
        "configuration": asdict(full_config),
        "full_delta_per_group": full_delta,
        "full_result_array_bytes": workspace.groups * full_count * per_group_result_bytes,
        "memory_policy": "reserve omitted result-vector bytes from the cap; unchanged native per-tile allowance",
        "abbreviation_result_omission_cap_bytes": 1024 * 1024,
        "abbreviation_cap_scope": "conservative adapter scope, not a measured cache or memory threshold",
        "arithmetic": report["arithmetic"],
        "timing_boundaries": {
            "load": "source-module import and GroupedWorkspace metadata validation; bulk mappings remain lazy",
            "prepare": "full-bank configuration, allocation preflight and arithmetic/device setup",
            "transfer_and_score": "combined native score_workspace: array reads, reblocking, residual UCB, factor preparation, transfers, scoring and receiver mixing",
            "finish": "adapter validation, counters and result/regime construction; final envelope and serialization excluded",
        },
    }
    result = {
        "complete": complete,
        "scope": "complete declared CFO family" if complete else "incomplete CFO prefix; timing only",
        "scores": report["group_bit_log_likelihoods"],
        "bit_log_likelihoods": report["bit_log_likelihoods"] if complete else None,
        "decisions": {gate: report[gate] for gate in ("strict", "relaxed")} if complete else None,
        "best_group_public_parameters": report["best_group_public_parameters"] if complete else None,
        "calibration": report["calibration"],
        "frequency_reuse": report["frequency_reuse"],
        "precision_diagnostics": {"arithmetic": report["arithmetic"],
                                  "window_numerics": report["window_numerics"],
                                  "fallback": False},
    }
    work = {
        "unit": "cfo", "count": count, "target_count": full_count, "full_count": full_count,
        "complete": complete, "base_groups_per_cfo": workspace.groups,
        "effective_groups": count * workspace.groups,
        "sample_count": workspace.sample_count, "source_blocks": workspace.blocks,
        "reblocked_blocks": blocks, "group_tiles": count * group_tiles,
        "gaussian_windows": count * group_tiles * windows,
        "projection_tiles": count * group_tiles * projection_tiles,
        "group_bit_scores": count * workspace.groups * 2,
        "frequency_offsets_hz": list(offsets[:count]),
        "delta_per_group": selected_delta, "full_delta_per_group": full_delta,
        "delta_exactly_equal": selected_delta == full_delta,
        "calibration_segments": config.calibration_segments,
        "omitted_result_budget_bytes": omitted_result_bytes,
        "resources": resources,
        "native_stage_seconds": report["timing"],
    }
    finished = time.perf_counter()
    return {
        "seconds": {"load": loaded - started, "prepare": prepared - loaded,
                    "transfer_and_score": scored - prepared, "finish": finished - scored},
        "work": work, "regime": regime, "result": result,
    }
