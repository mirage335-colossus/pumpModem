"""Performance wrapper for actual sampled Gaussian and phase-state likelihoods."""
from __future__ import annotations

from collections import Counter
import time
from types import SimpleNamespace

import numpy as np
import torch

from triad_perf_families import TRITON_PHASE, load_source


IMPLEMENTATIONS = {
    "gaussian-state": None,
    "phase-reference": "reference",
    "phase-optimized": "optimized",
    "phase-graph": "graph",
    "phase-reduction": "graph-fp64",
    "phase-tensor": "tensor",
}


def run_case(root, fixture, case, device, precision, limit=None):
    options = dict(case["options"])
    implementation = IMPLEMENTATIONS[case["family"]]
    method = "phase-frequency" if implementation else "gaussian"
    if not isinstance(options.get("taps", 2), int) or options.get("taps", 2) not in (1, 2):
        raise ValueError("state workload requires one or two taps")
    args = SimpleNamespace(taps=2, coherences=[16., 64., 256., 1024.],
                           phase_states=128, relative_phase_states=8,
                           frequency_bound=.02, frequency_step=None, frequency_std=.003,
                           frequency_coherence=1000., phase_diffusion=.00872665,
                           tensor_mode="strict-padded", tensor_block_columns=128,
                           tensor_fuse_advection=False, memory_limit_bytes=512*1024**2)
    unknown = options.keys() - vars(args).keys()
    if unknown:
        raise ValueError(f"unknown state workload options {sorted(unknown)}")
    vars(args).update(options)
    args.device = device
    args.precision = precision
    args.methods = [method]
    args.phase_implementation = implementation or "reference"
    if implementation in ("graph", "graph-fp64", "tensor") and not device.startswith("cuda"):
        raise ValueError("selected phase implementation requires CUDA")
    if implementation == "tensor" and precision != "fp32":
        raise ValueError("tensor phase inference requires explicit FP32 bulk state")

    started = time.perf_counter()
    study = load_source(root, "triad-state-study.py")
    workspace = study.BlockWorkspace(fixture, memory_limit_bytes=args.memory_limit_bytes)
    if not workspace.has_observation or not workspace.gaussian_projection_supported:
        raise ValueError("state performance requires actual sampled Gaussian-compatible projections")
    selected = [i for i, mapping in enumerate(workspace.column_mapping) if len(mapping) == args.taps]
    labels = np.asarray([workspace.candidates[i]["bit"] for i in selected])
    if set(labels) != {0, 1}:
        raise ValueError("the complete public candidate bank must contain both source bits")
    count = workspace.block_count if limit is None else limit
    if not isinstance(count, int) or isinstance(count, bool) or not 1 <= count <= workspace.block_count:
        raise ValueError("abbreviated state block count is outside the actual calendar")
    load = time.perf_counter() - started

    # Keep the real recurrence, candidate width and transition history. Never
    # splice disjoint blocks or score an abbreviated result as an admission.
    dts = Counter()
    valid_rows = 0
    empty_blocks = 0
    partial_blocks = 0

    def observed():
        nonlocal valid_rows, empty_blocks, partial_blocks
        # The abbreviation counts are whole input tiles. Use the same bounded
        # loader batch in the full run, including its actual terminal tile.
        for block in workspace.iter_observation(stop=count, tile_blocks=16):
            dts[block.dt_seconds] += 1
            valid_rows += block.valid_rows
            empty_blocks += block.valid_rows == 0
            partial_blocks += block.interval[1] - block.interval[0] != workspace.block_samples
            yield block

    started = time.perf_counter()
    values, diagnostics, update_elapsed = study.evaluate(workspace, observed(), args, selected, labels, 1)
    if device.startswith("cuda"):
        torch.cuda.synchronize(device)
    elapsed = time.perf_counter() - started
    diag = diagnostics[method]
    if method == "gaussian":
        updates = [state["blocks"] for state in diag]
        resolved = {
            "state_dtype": diag[0]["state_dtype"],
            "log_likelihood_dtype": diag[0]["log_likelihood_dtype"],
            "states": len(diag),
            "owned_state_bytes": sum(state["owned_state_bytes"] for state in diag),
        }
    else:
        updates = [diag["updates"]]
        resolved = {key: diag[key] for key in (
            "phase_states", "frequency_states", "quadrature_states", "frequency_model",
            "working_bytes_bound", "bulk_dtype", "log_evidence_dtype", "tf32_allowed")}
        if device.startswith("cuda") and case["family"] in TRITON_PHASE:
            if not diag["triton_version"] or not diag["gpu_optimized_execution"] or diag["fallback_executed"]:
                raise RuntimeError("requested CUDA phase backend did not execute its Triton implementation")
            if diag["optimized_stencil_calls"] <= 0:
                raise RuntimeError("CUDA phase workload did not execute any Triton phase stencils")
            if implementation in ("graph", "graph-fp64", "tensor") and (
                diag["graph_captures"] <= 0 or diag["graph_replays"] <= 0):
                raise RuntimeError("requested graph backend did not capture and replay CUDA work")
            resolved["triton_runtime_version"] = diag["triton_version"]
    if any(value != count for value in updates) or sum(dts.values()) != count:
        raise RuntimeError("state likelihood work differs from the declared block count")
    scores = np.asarray(values[method])
    if scores.shape != (1, 2) or not np.isfinite(scores).all():
        raise ValueError("nonfinite or incomplete state likelihood")
    return {
        "seconds": {
            "load": load,
            "initialize_update_transfer_and_finalize": elapsed,
            "update_iterator_and_transfer_nested": update_elapsed,
        },
        "work": {
            "unit": case["unit"], "count": updates[0], "target_count": workspace.block_count,
            "updates_per_state": updates, "valid_rows": valid_rows, "empty_blocks": empty_blocks,
            "partial_blocks": partial_blocks,
            "transition_intervals": [[dt, n] for dt, n in sorted(dts.items())],
            "counter_scope": "target from public sample calendar; observed row and transition events counted in the iterator; updates from receiver diagnostics",
        },
        "regime": {
            "family": case["family"], "device": device, "precision": precision,
            "sample_count": workspace.sample_count, "sample_rate": workspace.sample_rate,
            "base_block_samples": workspace.block_samples, "dimension": workspace.dimension,
            "candidates": len(selected), "taps": args.taps, "terminal_partial_block":
                workspace.sample_count % workspace.block_samples != 0,
            "loader_tile_blocks": min(16, workspace.max_tile_blocks),
            "memory_limit_bytes": args.memory_limit_bytes, "resolved": resolved,
            "timing_boundaries": {
                "load": "source and public workspace metadata loading",
                "initialize_update_transfer_and_finalize": "one synchronized evaluate call, including receiver initialization, sampled-statistic reads, device transfers, updates and result finalization",
                "update_iterator_and_transfer_nested": "subset of evaluate call; diagnostic timer, never added to enclosing stage",
            },
        },
        "result": {
            "scores": scores[0].tolist(), "winner": int(scores.argmax()),
            "known_noise_simulation_diagnostic": True, "diagnostics": diag,
            "scope": "complete actual sampled receiver" if limit is None else "timing-only state prefix",
        },
    }
