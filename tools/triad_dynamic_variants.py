#!/usr/bin/env python3
"""Canonical sparse refinement stage and its fixed public DATA projection.

The projection changes only the receiver's retained modes, never the simulated
channel, captured sidebands or transmitted waveform. Prepared affine scans use
the same Gaussian model and a guarded serial numerical fallback.
"""
from __future__ import annotations

import copy
import hashlib
import json
import math
from pathlib import Path
import time

import numpy as np
import torch

from triad_dynamic_joint import DynamicJointReceiver, public_spectral_bands
from triad_dynamic_scan import PreparedGaussianScan


def _identity(receiver, policy):
    digest = hashlib.sha256()
    for start, stop, projection, *_ in receiver.blocks:
        digest.update(np.asarray([start, stop], dtype="<i8").tobytes())
        digest.update(projection.kind.encode())
        digest.update(np.asarray(projection.indices, dtype="<i8").tobytes())
    directory = Path(__file__).parent
    sources = {name: hashlib.sha256((directory/name).read_bytes()).hexdigest()
               for name in ("triad_dynamic_variants.py", "triad_dynamic_joint.py",
                            "triad_dynamic_scan.py", "triad_gaussian_prepared.py",
                            "triad_gaussian_state.py")}
    receiver.variant_policy = dict(policy, resolved_projection_sha256=digest.hexdigest(),
        helper_component_control=receiver.manifest.get("helper_component_control", "full"),
        source_sha256=sources)
    receiver.procedure_id = hashlib.sha256(json.dumps(dict(
        base_procedure_id=receiver.procedure_id, variant=receiver.variant_policy),
        sort_keys=True).encode()).hexdigest()


def core_projection_bands(manifest, *, pilot_use="both", frequencies_hz=(0.,),
                          clock_ppm=(0.,), auxiliary_half_width_hz=200.):
    """Plan a fixed core selector without changing any simulated effect.

    Ray center/width, motion, CFO, clock and gate envelopes remain. Added-AM
    sideband allowance alone is omitted from projection planning; no energy
    renormalization or hidden-truth recentering is performed.
    """
    planning = copy.deepcopy(manifest)
    planning["channel"]["selective_slow_depth"] = 0.
    planning["channel"]["selective_fast_depth"] = 0.
    return public_spectral_bands(planning, pilot_use=pilot_use,
        cfo_bound_hz=max(abs(float(x)) for x in frequencies_hz),
        clock_bound_ppm=max(abs(float(x)) for x in clock_ppm),
        auxiliary_half_width_hz=auxiliary_half_width_hz)


class ScanJointReceiver(DynamicJointReceiver):
    """Sparse refinement using the prepared Gaussian likelihood."""

    def __init__(self, *args, scan_residual_limit=5e-7,
                 scan_transition_norm_limit=8., scan_serial_below_blocks=0, **kwargs):
        super().__init__(*args, **kwargs)
        started = time.perf_counter()
        item = 8 if self.dtype == torch.complex64 else 16
        estimates = []
        for design in self.designs:
            matrix_elements = design.blocks*math.prod(design.leading_shape)*design.taps**2
            owned = (design.estimated_owned_bytes+3*matrix_elements*item+
                     design.blocks*math.prod(design.leading_shape)*8+design.blocks*item)
            estimates.append((owned, 5*matrix_elements*16))
        # Count parent runtime storage conservatively even though these serial
        # objects are released below. Shared CPU design storage is also counted
        # twice: this is an explicit-array engineering bound, not allocator RSS.
        base_owned = self.owned_bytes_estimate
        resident = base_owned+sum(owned for owned, _ in estimates)
        peak = resident+max(scratch for _, scratch in estimates)
        if peak > self.memory_limit_bytes:
            raise MemoryError("joint affine-scan preparation exceeds combined memory budget")
        self.runtimes = []
        self.scans = []
        for design, (owned, _) in zip(self.designs, estimates):
            # Other already-resident public workspaces reserve their space.
            available = self.memory_limit_bytes-(resident-owned)
            self.scans.append(PreparedGaussianScan(design, self.device, self.dtype,
                memory_limit_bytes=available, residual_limit=scan_residual_limit,
                transition_norm_limit=scan_transition_norm_limit,
                serial_below_blocks=scan_serial_below_blocks))
        self.owned_bytes_estimate = resident
        self.preparation_peak_bytes_estimate = max(self.preparation_peak_bytes_estimate, peak)
        self.scan_preparation_seconds = time.perf_counter()-started
        self.preparation_seconds += self.scan_preparation_seconds
        _identity(self, dict(execution="affine-parallel-scan", projection="full-envelope",
            residual_limit=scan_residual_limit, transition_norm_limit=scan_transition_norm_limit,
            serial_below_blocks=scan_serial_below_blocks))

    def score_correlations(self, correlations):
        started = time.perf_counter()
        if correlations.shape[0] != len(self.blocks):
            raise ValueError("correlation block count changed")
        outcomes = [scan.score(correlations) for scan in self.scans]
        evidence = torch.stack([result["scores"] for result in outcomes])
        candidate_evidence = torch.logsumexp(evidence, dim=-1)-math.log(self.candidates)
        mixed = torch.logsumexp(candidate_evidence, dim=0)-math.log(len(self.coherences))
        bit_scores = mixed[:, 1:]-mixed[:, :1]
        model_bit_scores = candidate_evidence[:, :, 1:]-candidate_evidence[:, :, :1]
        if not bool(torch.isfinite(bit_scores).all()):
            raise FloatingPointError("nonfinite joint scan likelihood")
        if self.device.type == "cuda":
            torch.cuda.synchronize(self.device)
        result = dict(bit_scores=bit_scores.detach().cpu().numpy(),
            model_bit_scores=model_bit_scores.detach().cpu().numpy(),
            hypothesis_log_evidence=candidate_evidence.detach().cpu().numpy(),
            scan_backend=[{key: outcome[key] for key in
                ("execution", "fallback_reason", "score_seconds", "peak_owned_bytes_estimate")}
                for outcome in outcomes], state_seconds=time.perf_counter()-started)
        return result

    def work(self):
        result = super().work()
        result.update(execution="explicit affine scan; guarded serial fallback",
                      scan_preparation_seconds=self.scan_preparation_seconds,
                      scan=[scan.diagnostics() for scan in self.scans],
                      variant_policy=self.variant_policy)
        return result
