#!/usr/bin/env python3
"""Actual-observation acquisition with fixed group-local noise calibration.

Every declared synchronization group contains both bit patterns. Its complete
local column union defines an orthogonal residual and a stationary noise UCB.
All groups are chosen before observing data. Calibration failure is allocated
across groups; normalized likelihood mixtures do not require independent groups.
The workspace has no inter-group Gram: independently resampling group noise is
unsupported. No simulator means, signal energies or injected variance enter the
receiver. This is an isolated fixed-window study, not an anytime receiver.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import sys
import time

import numpy as np
from scipy.special import logsumexp
import torch

from triad_gaussian_window import PreparedWindowedGaussian
from triad_noise_likelihood import (FalseAdmissionBudget, whole_bank_residual,
                                    variance_upper_confidence)
from triad_state_workspace import WorkspaceError

SYNC_FIELDS = ("arrival_seconds", "frequency_hz", "clock_ppm",
               "epoch_offset_seconds", "frequency_drift_hz_per_second")
INPUT_FILES = ("manifest.json", "group_gram.c128", "group_observation.c128",
               "received_energy.f64", "intervals.u64", "valid_rows.u64")


def _integer(value, name, minimum=1):
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum or value > 2**53:
        raise WorkspaceError(f"{name} must be an integer in {minimum}..2**53")
    return value


def _number(value, name, *, positive=False):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or (positive and value <= 0):
        raise WorkspaceError(f"{name} must be finite" + (" and positive" if positive else ""))
    return float(value)


def _sync_key(value):
    if not isinstance(value, dict):
        raise WorkspaceError("synchronization key must be an object")
    epoch = value.get("epoch_offset_seconds")
    if isinstance(epoch, bool) or not isinstance(epoch, int) or not -(2**63) <= epoch < 2**63:
        raise WorkspaceError("epoch offset must be an exact signed 64-bit integer")
    return tuple(epoch if name == "epoch_offset_seconds" else _number(value.get(name),name)
                 for name in SYNC_FIELDS)


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


class GroupedWorkspace:
    """Validate metadata without reading any simulator-only sample arrays."""
    def __init__(self, directory):
        self.directory = Path(directory)
        try:
            self.manifest = json.loads((self.directory / "manifest.json").read_text())
        except (OSError, ValueError) as error:
            raise WorkspaceError(f"cannot read grouped manifest: {error}") from error
        m = self.manifest
        if not isinstance(m, dict) or m.get("format") != "triad-group-block-stats-v1":
            raise WorkspaceError("unsupported grouped statistics format")
        if (m.get("layout"), m.get("byte_order"), m.get("complex_type"), m.get("real_type"), m.get("integer_type")) != (
                "block,group,row,column", "little", "complex128", "float64", "uint64"):
            raise WorkspaceError("unsupported grouped scalar representation or layout")
        if m.get("stats_noisy") is not True:
            raise WorkspaceError("actual-noisy grouped acquisition requires --stats-noisy")
        if m.get("gaussian_projection_supported") is not True:
            raise WorkspaceError("nonlinear/non-Gaussian frontend does not support this calibration")
        if m.get("cross_group_gram_saved") is not False or m.get("conditional_noise_resampling_supported") is not False:
            raise WorkspaceError("grouped covariance scope must explicitly prohibit independent group noise resampling")
        self.blocks = _integer(m.get("block_count"), "block_count")
        self.groups = _integer(m.get("group_count"), "group_count")
        self.dimension = _integer(m.get("dimension"), "dimension")
        self.sample_count = _integer(m.get("sample_count"), "sample_count")
        self.block_samples = _integer(m.get("block_samples"), "block_samples")
        self.sample_rate = _number(m.get("sample_rate"), "sample_rate", positive=True)
        if self.dimension > 64 or self.groups > 5000 or self.blocks != (self.sample_count + self.block_samples - 1)//self.block_samples:
            raise WorkspaceError("group dimension, count or block calendar exceeds supported bounds")
        candidates = m.get("candidates")
        groups = m.get("groups")
        if not isinstance(candidates, list) or len(candidates) != m.get("candidate_count") or not 1 <= len(candidates) <= 10000:
            raise WorkspaceError("candidate metadata count is inconsistent")
        if not isinstance(groups, list) or len(groups) != self.groups:
            raise WorkspaceError("group metadata count is inconsistent")
        self.mapping = []
        self.selected_indices = []
        self.sync_keys = []
        assigned = []
        key_set = set()
        for gi, group in enumerate(groups):
            if not isinstance(group, dict) or not isinstance(group.get("sync_key"), dict):
                raise WorkspaceError("each group needs its fixed public synchronization key")
            key = _sync_key(group["sync_key"])
            if key in key_set:
                raise WorkspaceError("duplicate synchronization group")
            key_set.add(key)
            ids, mapping, descriptors = group.get("candidate_indices"), group.get("column_mapping"), group.get("column_descriptors")
            if not isinstance(ids, list) or not isinstance(mapping, list) or not ids or len(ids) != len(mapping):
                raise WorkspaceError("group candidate mapping is malformed")
            if not isinstance(descriptors, list) or len(descriptors) != self.dimension:
                raise WorkspaceError("group column descriptors are malformed")
            used = set()
            selected = {}
            for ci, columns in zip(ids, mapping):
                _integer(ci, "candidate index", 0)
                if ci >= len(candidates) or ci in assigned:
                    raise WorkspaceError("candidate index is repeated or out of range")
                assigned.append(ci)
                c = candidates[ci]
                if not isinstance(c, dict) or isinstance(c.get("bit"), bool) or c.get("bit") not in (0, 1):
                    raise WorkspaceError("candidate must specify bit 0 or 1")
                if _sync_key(c) != key:
                    raise WorkspaceError("candidate synchronization differs from its fixed group")
                if not isinstance(columns, list) or not columns:
                    raise WorkspaceError("candidate columns must be nonempty")
                for col in columns:
                    _integer(col, "column index", 0)
                    if col >= self.dimension:
                        raise WorkspaceError("candidate column outside local group")
                    descriptor = descriptors[col]
                    if not isinstance(descriptor, dict) or descriptor.get("source_bit") != c["bit"]:
                        raise WorkspaceError("column descriptor bit differs from mapped candidate")
                    if descriptor.get("linear_gain", False):
                        raise WorkspaceError("linear-gain columns are not supported by this two-tap receiver")
                    if _sync_key(descriptor) != key:
                        raise WorkspaceError("column descriptor synchronization differs from its group")
                taps = c.get("taps")
                if not isinstance(taps, list) or c.get("tap_count") != len(columns) or len(taps) != len(columns):
                    raise WorkspaceError("candidate tap metadata disagrees with mapping")
                for col, tap in zip(columns, taps):
                    if not isinstance(tap, dict):
                        raise WorkspaceError("invalid tap metadata")
                    delay = _number(tap.get("delay_seconds"), "tap delay")
                    doppler = _number(tap.get("doppler_hz"), "tap Doppler")
                    if (_number(descriptors[col].get("tap_delay_seconds"), "descriptor delay"),
                        _number(descriptors[col].get("tap_doppler_hz"), "descriptor Doppler")) != (delay, doppler):
                        raise WorkspaceError("descriptor tap differs from mapped candidate")
                used.update(columns)
                if len(columns) == 2:
                    if c["bit"] in selected:
                        raise WorkspaceError("each group must have exactly one two-tap candidate per bit")
                    selected[c["bit"]] = (ci, columns)
            if used != set(range(self.dimension)) or set(selected) != {0, 1}:
                raise WorkspaceError("group must cover all local columns and both two-tap bit alternatives")
            for descriptor in descriptors:
                if descriptor.get("candidate_index") not in ids:
                    raise WorkspaceError("column descriptor candidate is outside its group")
            self.mapping.append([selected[bit][1] for bit in (0, 1)])
            self.selected_indices.append([selected[bit][0] for bit in (0, 1)])
            self.sync_keys.append(dict(zip(SYNC_FIELDS, key)))
        if sorted(assigned) != list(range(len(candidates))):
            raise WorkspaceError("fixed groups must partition the entire declared candidate bank")
        self.mapping = np.asarray(self.mapping, dtype=np.int64)
        self.selected_indices = np.asarray(self.selected_indices, dtype=np.int64)
        b, h, d = self.blocks, self.groups, self.dimension
        self._spec = {
            "group_gram.c128": ("gram", "<c16", [b,h,d,d]),
            "group_observation.c128": ("observation", "<c16", [b,h,d]),
            "received_energy.f64": ("received_energy", "<f8", [b]),
            "intervals.u64": ("intervals", "<u8", [b,2]),
            "valid_rows.u64": ("valid_rows", "<u8", [b]),
        }
        sizes = m.get("file_sizes")
        if not isinstance(sizes, dict):
            raise WorkspaceError("grouped file size table is missing")
        self._stats = {}
        for filename, (prefix, dtype, shape) in self._spec.items():
            if m.get(prefix + "_file") != filename or m.get(prefix + "_shape") != shape:
                raise WorkspaceError(f"{prefix} filename or shape differs from grouped dimensions")
            expected = math.prod(shape)*np.dtype(dtype).itemsize
            try:
                stat = (self.directory/filename).stat()
            except OSError as error:
                raise WorkspaceError(f"cannot stat {filename}") from error
            if sizes.get(filename) != expected or stat.st_size != expected:
                raise WorkspaceError(f"{filename} size differs from declared shape")
            self._stats[filename] = (stat.st_size, stat.st_mtime_ns, stat.st_ino)
        self._maps = {}

    def array(self, filename):
        if filename not in self._spec:
            raise WorkspaceError("receiver may only map public design and actual observation arrays")
        if filename not in self._maps:
            _, dtype, shape = self._spec[filename]
            self._maps[filename] = np.memmap(self.directory/filename, dtype=dtype, mode="r", shape=tuple(shape))
        return self._maps[filename]

    def verify_unchanged(self):
        for filename, expected in self._stats.items():
            stat = (self.directory/filename).stat()
            if (stat.st_size, stat.st_mtime_ns, stat.st_ino) != expected:
                raise WorkspaceError("workspace changed while scoring")

    def calendar(self, reblock):
        _integer(reblock, "reblock")
        interval = np.array(self.array("intervals.u64"))
        valid = np.array(self.array("valid_rows.u64"))
        energy = np.array(self.array("received_energy.f64"))
        start = np.arange(self.blocks, dtype=np.uint64)*self.block_samples
        stop = np.minimum(start+self.block_samples, self.sample_count)
        if not np.array_equal(interval[:,0], start) or not np.array_equal(interval[:,1], stop) or np.any(valid > stop-start):
            raise WorkspaceError("grouped actual calendar is inconsistent")
        if not np.isfinite(energy).all() or np.any(energy < 0) or np.any(energy[valid == 0] != 0):
            raise WorkspaceError("grouped received energy is invalid")
        first = np.arange(0, self.blocks, reblock)
        result = np.column_stack((interval[first,0], interval[np.minimum(first+reblock,self.blocks)-1,1]))
        return result, np.add.reduceat(valid, first), np.add.reduceat(energy, first)

    def read_group_tile(self, first, stop, reblock):
        if not 0 <= first < stop <= self.groups:
            raise ValueError("invalid fixed group tile")
        G = np.array(self.array("group_gram.c128")[:,first:stop])
        r = np.array(self.array("group_observation.c128")[:,first:stop])
        if not np.isfinite(G).all() or not np.isfinite(r).all():
            raise WorkspaceError("grouped actual statistics are nonfinite")
        if reblock != 1:
            indices = np.arange(0, self.blocks, reblock)
            G = np.add.reduceat(G, indices, axis=0)
            r = np.add.reduceat(r, indices, axis=0)
        return G, r


@dataclass(frozen=True)
class AcquisitionConfig:
    doppler_width_hz: float
    window_blocks: int = 16
    window_seconds: float | None = None
    reblock: int = 1
    reference_variance: float = 1.
    stationary_power: tuple[float, float] = (.5, .5)
    group_tile: int = 1
    projection_tile_blocks: int = 256
    memory_limit_bytes: int = 512*1024*1024
    device: str = "cpu"
    precision: str = "fp64"
    attempts_per_day: int = 1440
    duration_alternatives: int = 8
    model_alternatives: int = 16
    calibration_segments: int = 2
    known_noise_variance: float | None = None

    def __post_init__(self):
        _number(self.doppler_width_hz, "Doppler width")
        if self.doppler_width_hz < 0:
            raise ValueError("Doppler width must be nonnegative")
        for name in ("window_blocks", "reblock", "group_tile", "projection_tile_blocks", "memory_limit_bytes",
                     "attempts_per_day", "duration_alternatives", "model_alternatives", "calibration_segments"):
            _integer(getattr(self, name), name)
        _number(self.reference_variance, "reference variance", positive=True)
        if self.window_seconds is not None:
            _number(self.window_seconds, "window seconds", positive=True)
        if self.known_noise_variance is not None:
            _number(self.known_noise_variance, "known noise diagnostic variance", positive=True)
        if len(self.stationary_power) != 2 or any(not math.isfinite(p) or p < 0 for p in self.stationary_power):
            raise ValueError("two nonnegative finite tap powers are required")
        if self.precision not in ("fp32", "fp64") or (self.device != "cpu" and not self.device.startswith("cuda")):
            raise ValueError("unsupported bulk precision or device")

    def budgets(self):
        common = dict(duration_alternatives=self.duration_alternatives, model_alternatives=self.model_alternatives,
                      calibration_segments=self.calibration_segments, candidates=2)
        return (FalseAdmissionBudget(false_probability=1e-4, attempts=self.attempts_per_day, **common),
                FalseAdmissionBudget(false_probability=.01, attempts=(self.attempts_per_day+1)//2, **common))


def prepare_group_tile(G, r, energy, valid, mapping, *, reference, delta, projection_tile_blocks=256):
    """Project complete fixed groups; no candidate selection enters calibration."""
    b, h, d, _ = G.shape
    correlations = np.empty((b,h,2,2), np.complex128)
    grams = np.empty((b,h,2,2,2), np.complex128)
    residual = np.zeros(h)
    compensation = np.zeros(h)
    dof = np.zeros(h, np.int64)
    ranks = np.zeros((h,d+1), np.int64)
    for first in range(0,b,projection_tile_blocks):
        stop = min(first+projection_tile_blocks,b)
        for count in np.unique(valid[first:stop]):
            select = first+np.flatnonzero(valid[first:stop] == count)
            rr = r[select]/reference
            gg = G[select]/reference
            ee = np.broadcast_to(energy[select,None]/reference, (len(select),h)).copy()
            fit = whole_bank_residual(torch.from_numpy(rr), torch.from_numpy(gg), torch.from_numpy(ee), int(count))
            pr, pg = fit.projected_correlations.numpy(), fit.projected_gram.numpy()
            for j in range(h):
                correlations[select,j] = pr[:,j,mapping[j]]
                grams[select,j] = pg[:,j,mapping[j,:, :,None],mapping[j,:,None,:]]
                ranks[j] += np.bincount(fit.rank.numpy()[:,j], minlength=d+1)
            subtotal = np.asarray([math.fsum(map(float,fit.energy.numpy()[:,j])) for j in range(h)])
            adjusted = subtotal-compensation
            updated = residual+adjusted
            compensation = (updated-residual)-adjusted
            residual = updated
            dof += fit.complex_dof.numpy().sum(axis=0,dtype=np.int64)
    if np.any(dof <= 0):
        raise ValueError("fixed group has no positive residual calibration degrees of freedom")
    upper = variance_upper_confidence(residual, dof, delta).numpy()
    if not np.isfinite(upper).all() or np.any(upper <= 0):
        raise ValueError("group noise upper bound is unresolved")
    return correlations, grams, upper, residual, dof, ranks


def bit_mixture(candidate_scores):
    scores = np.asarray(candidate_scores, dtype=np.float64)
    if scores.ndim != 2 or scores.shape[1] != 2 or not len(scores) or not np.isfinite(scores).all():
        raise ValueError("one finite pair of bit scores per fixed group is required")
    return logsumexp(scores,axis=0)-math.log(len(scores))


def decision(scores, threshold):
    winner = int(np.argmax(scores))
    return {"bit":winner,"accepted":bool(scores[winner] >= threshold),
            "score":float(scores[winner]),"opposite_bit_margin":float(scores[winner]-scores[1-winner]),
            "log_threshold":float(threshold)}


def score_workspace(workspace, config):
    started = time.perf_counter()
    strict, relaxed = config.budgets()
    delta = math.nextafter(strict.delta_per_segment/workspace.groups,0.)
    if delta <= 0:
        raise ValueError("group calibration allocation underflow")
    b = (workspace.blocks+config.reblock-1)//config.reblock
    # Bound resident tile arrays and the largest eigensolve batch. The existing
    # Gaussian helper separately preflights all retained covariance factors.
    tile = min(config.group_tile,workspace.groups)
    d = workspace.dimension
    source_bytes = workspace.blocks*tile*(d*d+d)*16
    retained_bytes = b*tile*(d*d+d+2*2+2*2*2)*16
    projection_bytes = min(b,config.projection_tile_blocks)*tile*(24*d*d*16+32*d*16+256)
    calendar_bytes = (workspace.blocks+b)*64
    # These arrays survive all group tiles: two bit scores, UCB/residual/DOF,
    # rank histograms, selected local columns and selected candidate indices.
    # Their size depends on the entire declared bank, not the working tile.
    all_group_bytes = workspace.groups*(96+8*d)
    base_bound = source_bytes+retained_bytes+projection_bytes+calendar_bytes+all_group_bytes+65536
    if base_bound >= config.memory_limit_bytes:
        raise MemoryError("grouped acquisition arrays exceed explicit memory limit")
    intervals, valid, energy = workspace.calendar(config.reblock)
    centers = (intervals[:,0].astype(float)+intervals[:,1].astype(float))/(2*workspace.sample_rate)
    window_blocks = config.window_blocks
    if config.window_seconds is not None:
        exact = config.window_seconds*workspace.sample_rate/(workspace.block_samples*config.reblock)
        window_blocks = int(round(exact))
        if window_blocks < 1 or not math.isclose(exact,window_blocks,rel_tol=1e-10,abs_tol=1e-10):
            raise ValueError("physical window seconds must be an integer number of reblocked sample blocks")
    if window_blocks > 1024:
        raise ValueError("Gaussian window exceeds 1024 blocks")
    values = np.empty((workspace.groups,2))
    upper_all = np.empty(workspace.groups)
    residual_all = np.empty(workspace.groups)
    dof_all = np.empty(workspace.groups,np.int64)
    ranks_all = np.empty((workspace.groups,d+1),np.int64)
    elapsed = dict(read_reblock_seconds=0.,residual_calibration_seconds=0.,factor_preparation_seconds=0.,score_seconds=0.)
    maximum_factor_bytes = 0
    observable_rank_tolerance = None
    window_numerics = dict(whitening_variance_inflation=1.,
                           maximum_relative_discarded_gram_trace=0.,
                           maximum_whitening_covariance_error=0.,
                           maximum_mode_orthogonality_error=0.)
    maximum_array_bound = base_bound
    for first in range(0,workspace.groups,tile):
        stop = min(first+tile,workspace.groups)
        mark = time.perf_counter()
        G,r = workspace.read_group_tile(first,stop,config.reblock)
        elapsed["read_reblock_seconds"] += time.perf_counter()-mark
        mark = time.perf_counter()
        correlations,grams,upper,residual,dof,ranks = prepare_group_tile(
            G,r,energy,valid,workspace.mapping[first:stop],reference=config.reference_variance,
            delta=delta,projection_tile_blocks=config.projection_tile_blocks)
        elapsed["residual_calibration_seconds"] += time.perf_counter()-mark
        chosen = upper if config.known_noise_variance is None else np.full_like(upper,config.known_noise_variance/config.reference_variance)
        mark = time.perf_counter()
        plan = PreparedWindowedGaussian(grams.reshape(b,(stop-first)*2,2,2),centers,
            doppler_width_hz=config.doppler_width_hz,window_blocks=window_blocks,
            stationary_power=config.stationary_power,memory_limit_bytes=config.memory_limit_bytes-base_bound)
        elapsed["factor_preparation_seconds"] += time.perf_counter()-mark
        maximum_factor_bytes = max(maximum_factor_bytes,plan.owned_factor_bytes)
        observable_rank_tolerance = plan.relative_tolerance
        for name in window_numerics:
            window_numerics[name] = max(window_numerics[name],getattr(plan,name))
        maximum_array_bound = max(maximum_array_bound,base_bound+plan.preparation_array_bound_bytes)
        if config.device.startswith("cuda"):
            torch.cuda.synchronize(config.device)
        mark = time.perf_counter()
        scores = plan.score(correlations.reshape(b,(stop-first)*2,2),
            variance_scale=np.repeat(chosen,2)[None,:],device=config.device,precision=config.precision).detach().cpu().numpy()
        if config.device.startswith("cuda"):
            torch.cuda.synchronize(config.device)
        elapsed["score_seconds"] += time.perf_counter()-mark
        maximum_array_bound = max(maximum_array_bound,base_bound+plan.last_score_array_bound_bytes)
        values[first:stop] = scores.reshape(stop-first,2)
        upper_all[first:stop],residual_all[first:stop],dof_all[first:stop],ranks_all[first:stop] = upper,residual,dof,ranks
        del G,r,correlations,grams,plan
    workspace.verify_unchanged()
    mixed = bit_mixture(values)
    winner = int(np.argmax(mixed))
    best = int(np.argmax(values[:,winner]))
    elapsed["total_receiver_seconds"] = time.perf_counter()-started
    return {"format":"triad-group-acquisition-result-v1","input":str(workspace.directory),
        "mode":"actual-noisy", "method":"group-local-gaussian-window-ucb" if config.known_noise_variance is None else "group-local-gaussian-window-known-noise-diagnostic",
        "bit_log_likelihoods":mixed.tolist(),"group_bit_log_likelihoods":values.tolist(),
        "strict":decision(mixed,strict.log_threshold),"relaxed":decision(mixed,relaxed.log_threshold),
        "best_scoring_group_for_winning_bit":best,"best_group_public_parameters":workspace.sync_keys[best],
        "best_group_scope":"score diagnostic only; all fixed groups remain in the bit mixture and calibration",
        "calibration":{"scope":"stationary proper-complex white Gaussian noise; fixed group-local complete-column residuals; union allocation across all predeclared groups",
            "group_count":workspace.groups,"delta_per_group":delta,"strict":strict.diagnostics(),"relaxed":relaxed.diagnostics(),
            "relaxed_uses_strict_calibration":True,"reference_variance":config.reference_variance,
            "known_noise_variance_diagnostic":config.known_noise_variance,
            "variance_upper_over_reference":upper_all.tolist(),"pooled_residual_energy_over_reference":residual_all.tolist(),
            "pooled_complex_dof":dof_all.tolist(),"rank_histograms":ranks_all.tolist(),
            "inter_group_independence_assumed":False,"conditional_noise_resampling_supported":False},
        "geometry":{"group_count":workspace.groups,"group_dimension":d,"selected_candidates":workspace.selected_indices.tolist(),
            "sample_count":workspace.sample_count,"sample_rate":workspace.sample_rate,"base_block_samples":workspace.block_samples,
            "reblock":config.reblock,"block_samples":workspace.block_samples*config.reblock,"block_count":b,
            "window_blocks":window_blocks,"nominal_window_seconds":window_blocks*workspace.block_samples*config.reblock/workspace.sample_rate,
            "public_group_parameters":workspace.sync_keys,
            "mixture":"uniform over every fixed synchronization group, separately for each bit; no pruning"},
        "arithmetic":{"bulk":config.precision,"device":config.device,"preparation":"CPU complex128/float64 rank projection, observation whitening, prior roots and rectangular SVD",
            "window_whitening":"FP64 small block whitening/scaling on selected device before bounded bulk mode GEMM",
            "finish":"FP64 residual pooling, variance quantiles, likelihood weights/logdet/evidence and bit mixtures","tf32_allowed":False},
        "window_numerics":dict(window_numerics,
            observable_relative_rank_tolerance=observable_rank_tolerance,
            scope="fixed observation-mode projection; discarded Gram trace is information loss, not a noise variance floor; residual checks are numerical diagnostics, not an IEEE rare-event proof"),
        "resources":{"group_tile":tile,"explicit_array_bound_bytes":maximum_array_bound,"maximum_retained_factor_bytes":maximum_factor_bytes,
            "all_group_retained_array_bytes":all_group_bytes,
            "memory_limit_bytes":config.memory_limit_bytes,"scope":"explicit arrays; mapped files, parsed JSON/report objects, framework allocator and solver internals are not whole-process memory"},
        "timing":elapsed,"limits":["independent Gaussian windows and block-center tap coefficients are an alternative model, not a full sampled channel likelihood",
            "noise-only calibration does not guarantee rejection of wrong-key signals or wrong-bit admission under model mismatch",
            "fixed completed windows only; no adaptive stopping or uncharged selection of groups, blocks, widths or models"]}


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs",nargs="+",type=Path)
    parser.add_argument("--mode",choices=("actual-noisy",),default="actual-noisy")
    parser.add_argument("--doppler-width-hz",type=float,required=True)
    window=parser.add_mutually_exclusive_group(required=True)
    window.add_argument("--window-blocks",type=int)
    window.add_argument("--window-seconds",type=float)
    parser.add_argument("--reblock",type=int,default=1)
    parser.add_argument("--reference-variance",type=float,default=1.)
    parser.add_argument("--tap-powers",type=float,nargs=2,default=(.5,.5))
    parser.add_argument("--group-tile",type=int,default=1)
    parser.add_argument("--projection-tile-blocks",type=int,default=256)
    parser.add_argument("--memory-mib",type=float,default=512.)
    parser.add_argument("--device",default="cpu")
    parser.add_argument("--precision",choices=("fp32","fp64"),default="fp64")
    parser.add_argument("--threads",type=int,default=1)
    parser.add_argument("--attempts-per-day",type=int,default=1440)
    parser.add_argument("--duration-alternatives",type=int,default=8)
    parser.add_argument("--selection-methods",type=int,default=16)
    parser.add_argument("--calibration-segments",type=int,default=2)
    parser.add_argument("--known-noise-variance",type=float,help="explicit diagnostic only; never reads injected simulator noise")
    parser.add_argument("--evaluate-simulation",action="store_true",help="append truth/control outcome after receiver decisions")
    parser.add_argument("--output",type=Path)
    parser.add_argument("--scores-output",type=Path)
    args=parser.parse_args(argv)
    # A default equal to an explicitly supplied small integer is not counted
    # as selected by some argparse versions for a required exclusive group.
    # Apply the seconds-mode placeholder only after checking the explicit choice.
    if args.window_blocks is None:
        args.window_blocks=16
    return args


def main(argv=None):
    args = parse_args(argv)
    _integer(args.threads,"threads")
    if not math.isfinite(args.memory_mib) or args.memory_mib <= 0:
        raise ValueError("memory MiB must be positive finite")
    if len({str(p.resolve()) for p in args.inputs}) != len(args.inputs):
        raise ValueError("duplicate physical workspace")
    for output in (args.output,args.scores_output):
        if output is not None and output.exists():
            raise ValueError("output already exists; choose a fresh path")
    torch.set_num_threads(args.threads)
    torch.set_float32_matmul_precision("highest")
    if args.device.startswith("cuda"):
        if not torch.cuda.is_available():
            raise ValueError("CUDA unavailable")
        torch.cuda.set_device(args.device)
        torch.backends.cuda.matmul.allow_tf32=False
    config = AcquisitionConfig(args.doppler_width_hz, args.window_blocks,args.window_seconds,args.reblock,
        args.reference_variance,tuple(args.tap_powers),args.group_tile,args.projection_tile_blocks,
        int(args.memory_mib*1024*1024),args.device,args.precision,args.attempts_per_day,
        args.duration_alternatives,args.selection_methods,args.calibration_segments,args.known_noise_variance)
    results=[]
    arrays={}
    for index,path in enumerate(args.inputs):
        workspace=GroupedWorkspace(path)
        identity={name:sha256(path/name) for name in INPUT_FILES}
        report=score_workspace(workspace,config)
        report["input_sha256"]=identity
        if args.evaluate_simulation:
            control=workspace.manifest.get("control")
            truth=workspace.manifest.get("signal_bit_simulation_truth")
            if control not in ("signal","null","wrong-key") or truth not in (0,1):
                raise WorkspaceError("simulation evaluation metadata invalid")
            outcomes={}
            for gate in ("strict","relaxed"):
                accepted=report[gate]["accepted"]
                if control=="signal":
                    outcomes[gate]={"correct_admitted":int(accepted and report[gate]["bit"]==truth),
                        "wrong_admitted":int(accepted and report[gate]["bit"]!=truth),"missed":int(not accepted)}
                else:
                    outcomes[gate]={"false_admitted":int(accepted),"not_admitted":int(not accepted)}
            report["simulation_evaluation_only"]={"control":control,"signal_bit":truth,"outcomes":outcomes}
        arrays[f"group_scores_{index}"]=np.asarray(report["group_bit_log_likelihoods"])
        arrays[f"bit_scores_{index}"]=np.asarray(report["bit_log_likelihoods"])
        results.append(report)
    payload={"format":"triad-group-acquisition-campaign-v1","receiver_source_sha256":sha256(Path(__file__)),"captures":results}
    if args.scores_output:
        args.scores_output.parent.mkdir(parents=True,exist_ok=True)
        with args.scores_output.open("xb") as stream:
            np.savez_compressed(stream,**arrays)
        payload["scores_npz_sha256"]=sha256(args.scores_output)
    text=json.dumps(payload,indent=2,allow_nan=False)+"\n"
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        with args.output.open("x") as stream:
            stream.write(text)
    else:
        print(text,end="")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, MemoryError) as error:
        print(f"triad_group_acquire: {error}",file=sys.stderr)
        raise SystemExit(2)
