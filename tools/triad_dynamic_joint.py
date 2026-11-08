#!/usr/bin/env python3
"""Block-continuous joint DATA/pilot likelihood for the isolated triad study.

The source is the *complete* three-carrier complex pattern.  Linear block
correlations precede channel marginalization; no noisy carrier products are
formed.  The two physical rays share coefficients across DATA and both pilots.
An OU prior is an inference approximation, not a claim that the sampled channel
has that law.  Finite public timing/CFO/epoch/model alternatives are uniformly
mixed.  This module returns scores, not an operational admission decision.

Helper's unknown auxiliary is excluded with an orthonormal, disjoint-block DFT
projection.  This preserves white Gaussian noise exactly in retained modes,
but does NOT remove finite-window auxiliary leakage exactly.  Actual complete
Helper captures and independent metadata require empirical calibration.  The
public support guard rejects overlap with the excluded auxiliary band; that is
an adapter limitation, not a physical impossibility.  No-Helper uses no such
exclusion, selects every available independent raw row, and needs no pilot
observation aperture.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import itertools
import json
import math
from pathlib import Path
import time
from typing import Callable, Sequence

import numpy as np
import torch

from triad_gaussian_prepared import PreparedGaussianDesign
from triad_gaussian_state import GaussianPrior


def _finite(value, name, *, positive=False):
    value = float(value)
    if not math.isfinite(value) or (positive and value <= 0):
        raise ValueError(f"{name} must be finite" + (" and positive" if positive else ""))
    return value


def _bands(values, sample_rate):
    result = tuple(tuple(_finite(x, "band edge") for x in band) for band in values)
    if any(len(band) != 2 or not -sample_rate/2 < band[0] <= band[1] < sample_rate/2
           for band in result):
        raise ValueError("bands must be ordered and strictly inside raw Nyquist")
    return result


@dataclass(frozen=True)
class BlockProjection:
    """Rows of a unitary DFT, or exact independent valid raw samples."""
    samples: int
    sample_rate: float
    indices: np.ndarray
    kind: str = "dft"

    @classmethod
    def make(cls, samples, sample_rate, required_bands, excluded_bands=()):
        if not isinstance(samples, int) or samples < 1:
            raise ValueError("projection needs a positive integer sample count")
        fs = _finite(sample_rate, "sample rate", positive=True)
        required, excluded = _bands(required_bands, fs), _bands(excluded_bands, fs)
        if not required:
            raise ValueError("at least one required band is needed")
        for lo, hi in required:
            if any(max(lo, a) <= min(hi, b) for a, b in excluded):
                raise ValueError("required public support overlaps excluded auxiliary band")
        frequencies = np.fft.fftfreq(samples, 1/fs)
        half_bin = fs/(2*samples)
        keep = np.zeros(samples, bool)
        excluded_bins = np.zeros(samples, bool)
        for lo, hi in excluded:
            excluded_bins |= (frequencies >= lo-half_bin) & (frequencies <= hi+half_bin)
        for lo, hi in required:
            selected = (frequencies >= lo-half_bin) & (frequencies <= hi+half_bin)
            if not np.any(selected & ~excluded_bins):
                raise ValueError("block too short to resolve required and excluded bands")
            # Never silently trim a requested band at an auxiliary-bin boundary.
            if np.any(selected & excluded_bins):
                raise ValueError("DFT bin width overlaps required and excluded bands")
            keep |= selected
        indices = np.flatnonzero(keep)
        indices.flags.writeable = False
        return cls(samples, fs, indices)

    @classmethod
    def valid_rows(cls, sample_rate, valid):
        indices = np.flatnonzero(valid)
        indices.flags.writeable = False
        return cls(len(valid), sample_rate, indices, "valid-rows")

    def numpy(self, values):
        values = np.asarray(values)
        if values.shape[-1] != self.samples:
            raise ValueError("projection input length changed")
        if self.kind == "valid-rows":
            return values[..., self.indices]
        return np.fft.fft(values, axis=-1, norm="ortho")[..., self.indices]


class ProjectedJointReceiver:
    """Reusable raw block workspace, independent of received observations.

    ``basis(start, stop)`` returns [3, candidates, taps, samples], ordered
    H0, bit0, bit1.  All alternatives use exactly the same public projection.
    The valid-rows mode retains every available white raw sample. The DFT mode
    supports wholly observed or wholly erased blocks; partial blocks require a
    future covariance/QR adapter and are explicitly rejected. Neither mode
    replaces a missing10ms interval with an unnecessarily erased whole block.
    """

    def __init__(self, *, sample_rate, samples, block_samples,
                 basis: Callable[[int, int], np.ndarray], required_bands,
                 excluded_bands=(), noise_variance=1., stationary_power=(.5, .5),
                 coherence_seconds=(.5, math.inf), valid=None, device="cpu",
                 precision="fp32", memory_limit_bytes=512*1024**2,
                 maximum_candidates=4096, candidate_count=1, projection_mode="dft"):
        started = time.perf_counter()
        self.sample_rate = _finite(sample_rate, "sample rate", positive=True)
        if (not isinstance(samples, int) or samples < 1 or
                not isinstance(block_samples, int) or block_samples < 1):
            raise ValueError("sample and block counts must be positive integers")
        if precision not in ("fp32", "fp64"):
            raise ValueError("precision must be fp32 or fp64")
        self.samples, self.block_samples = samples, block_samples
        self.memory_limit_bytes = int(memory_limit_bytes)
        if self.memory_limit_bytes < 1:
            raise MemoryError("positive workspace memory budget required")
        self.variance = _finite(noise_variance, "raw noise variance", positive=True)
        self.device = torch.device(device)
        self.dtype = torch.complex64 if precision == "fp32" else torch.complex128
        if self.device.type == "cuda":
            if not torch.cuda.is_available():
                raise RuntimeError("requested CUDA device is unavailable")
            if self.device.index is None:
                self.device = torch.device("cuda", torch.cuda.current_device())
            if precision == "fp32" and (torch.backends.cuda.matmul.allow_tf32 or
                    torch.get_float32_matmul_precision() != "highest"):
                raise ValueError("FP32 minimum requires TF32 disabled and highest matmul precision")
        self.coherences = tuple(float(x) for x in coherence_seconds)
        if (not self.coherences or len(set(self.coherences)) != len(self.coherences) or
                any(not x > 0 or math.isnan(x) for x in self.coherences)):
            raise ValueError("coherences must be unique positive values or infinity")
        powers = tuple(float(x) for x in stationary_power)
        GaussianPrior(powers, self.coherences[0])  # Validate the inherited contract.
        if (not isinstance(candidate_count, int) or
                not 1 <= candidate_count <= maximum_candidates):
            raise ValueError("candidate_count must be a bounded positive integer")
        if projection_mode not in ("dft", "valid-rows"):
            raise ValueError("projection mode must be dft or valid-rows")
        if projection_mode == "valid-rows" and excluded_bands:
            raise ValueError("raw-row selection cannot exclude auxiliary bands")
        _bands(required_bands, self.sample_rate)
        self.projection_mode = projection_mode
        if 2*samples > self.memory_limit_bytes:
            raise MemoryError("raw validity workspace exceeds declared memory budget")
        valid = np.ones(samples, bool) if valid is None else np.asarray(valid)
        if valid.shape != (samples,) or not np.isin(valid, (0, 1)).all():
            raise ValueError("validity must be a binary raw-sample vector")
        self.valid = valid.astype(bool, copy=True)
        self.valid.flags.writeable = False
        self.blocks = []
        self.zero_basis_blocks = set()
        grams, centers = [], []
        self.erased_samples = int(samples-np.count_nonzero(self.valid))
        self.candidates = candidate_count
        self.taps = len(powers)
        layouts = []
        for start in range(0, samples, block_samples):
            stop = min(samples, start+block_samples)
            # Merge the short remainder: zero-padding a DFT observation would
            # instead introduce correlated noise between retained modes.
            if 0 < samples-stop < block_samples:
                stop = samples
            if layouts and start < layouts[-1][1]:
                continue
            valid_rows = self.valid[start:stop]
            erased = not bool(valid_rows.any())
            if projection_mode == "dft":
                if not erased and not bool(valid_rows.all()):
                    raise ValueError("partial erasures with auxiliary DFT projection require a covariance adapter")
                projection = BlockProjection.make(stop-start, self.sample_rate,
                                                  required_bands, excluded_bands)
            else:
                projection = BlockProjection.valid_rows(self.sample_rate, valid_rows)
            layouts.append((start, stop, projection, erased))
        self.projected_modes = sum(len(p.indices) for _, _, p, _ in layouts)
        scalar_count = 3*candidate_count
        gram_count = len(layouts)*scalar_count*self.taps*self.taps
        complex_bytes = 8 if precision == "fp32" else 16
        # Conservative explicit-array upper estimate, including separate CPU
        # Gram/posterior designs and their device runtime copies even where
        # CPU FP64 views can alias. Includes both Gram-list/stack copies during
        # construction, state metadata, projection indices and copied validity.
        base_bytes = self.projected_modes*scalar_count*self.taps*complex_bytes
        gram_bytes = gram_count*(32+len(self.coherences)*(32+2*complex_bytes))
        scalar_bytes = len(layouts)*len(self.coherences)*(scalar_count*16+32)
        resident = base_bytes+gram_bytes+scalar_bytes+self.projected_modes*16+samples
        largest_block = max(stop-start for start, stop, *_ in layouts)
        # Also allow the bounded interpolation/rotation scratch of the native
        # basis adapter. It is released before the projection FFT is formed.
        scratch = largest_block*(scalar_count*self.taps*48+256)
        # This precedes *every* potentially large source-basis callback.
        if resident+scratch > self.memory_limit_bytes:
            raise MemoryError("projected joint workspace exceeds declared memory budget before basis generation")
        self.owned_bytes_estimate = resident
        self.preparation_peak_bytes_estimate = resident+scratch
        for start, stop, projection, erased in layouts:
            raw_basis = np.asarray(basis(start, stop), dtype=np.complex128)
            if (raw_basis.ndim != 4 or raw_basis.shape[0] != 3 or
                    raw_basis.shape[2:] != (self.taps, stop-start) or
                    raw_basis.shape[1] != candidate_count or
                    not np.isfinite(raw_basis).all()):
                raise ValueError("basis must be finite [H0/bit0/bit1,candidates,taps,raw rows]")
            if erased or not np.any(raw_basis):
                # An exactly zero design has exactly zero sufficient
                # statistics. Keep this block and its physical time in the
                # state model; only its unnecessary FFT/Gram work disappears.
                projected = np.zeros((*raw_basis.shape[:-1], len(projection.indices)),
                                     np.complex128)
                gram = np.zeros((3, candidate_count, self.taps, self.taps), np.complex128)
                self.zero_basis_blocks.add(len(self.blocks))
            else:
                projected = projection.numpy(raw_basis)
                gram = np.einsum("hctn,hcsn->hcts", projected.conj(), projected,
                                 optimize=True)/self.variance
            grams.append(gram)
            centers.append((start+stop-1)/(2*self.sample_rate))
            tensor = torch.as_tensor(projected, dtype=self.dtype, device=self.device)
            indices = torch.as_tensor(projection.indices.copy(), device=self.device)
            self.blocks.append((start, stop, projection, tensor, indices, erased))
        self.grams = torch.from_numpy(np.stack(grams))
        self.elapsed = (0., *np.diff(centers).tolist())
        self.designs = [PreparedGaussianDesign(self.grams, self.elapsed,
            GaussianPrior(powers, coherence), memory_limit_bytes=memory_limit_bytes)
            for coherence in self.coherences]
        self.runtimes = [design.to(self.device, self.dtype, check_inputs=False)
                         for design in self.designs]
        self.preparation_seconds = time.perf_counter()-started

    def correlations(self, raw):
        """Calculate reusable [block,batch,H,candidate,tap] complex statistics.

        FFT, transfers and GEMM are timed here.  Noise injection and source
        generation belong to the caller's workload, not this receiver timer.
        """
        started = time.perf_counter()
        if isinstance(raw, torch.Tensor):
            data = raw
        else:
            data = np.asarray(raw)
            if data.ndim == 1:
                data = data[None, :]
        if data.ndim != 2 or data.shape[1] != self.samples or data.shape[0] < 1:
            raise ValueError("raw data needs [batch, fixed capture rows]")
        complex_bytes = 8 if self.dtype == torch.complex64 else 16
        statistics_bytes = len(self.blocks)*data.shape[0]*3*self.candidates*self.taps*complex_bytes
        largest_block = max(stop-start for start, stop, *_ in self.blocks)
        # Output plus the list awaiting stack, and transferred/FFT scratch.
        state_bytes = len(self.coherences)*data.shape[0]*3*self.candidates*(
            self.taps*complex_bytes*8+8*8)
        if (self.owned_bytes_estimate+2*statistics_bytes+state_bytes+
                4*largest_block*data.shape[0]*complex_bytes > self.memory_limit_bytes):
            raise MemoryError("observation batch exceeds joint workspace budget; reduce batch size")
        if isinstance(data, torch.Tensor):
            if not bool(torch.isfinite(data).all()):
                raise ValueError("raw data contains nonfinite samples")
        elif not np.isfinite(data).all():
            raise ValueError("raw data contains nonfinite samples")
        output = []
        for bi, (start, stop, projection, basis, indices, erased) in enumerate(self.blocks):
            if erased or bi in self.zero_basis_blocks:
                r = torch.zeros((data.shape[0], 3, self.candidates, self.taps),
                                dtype=self.dtype, device=self.device)
            else:
                piece = data[:, start:stop]
                if isinstance(piece, np.ndarray) and not piece.flags.writeable:
                    piece = piece.copy()  # A readonly memmap must not alias a mutable Torch tensor.
                y = torch.as_tensor(piece, dtype=self.dtype, device=self.device)
                if projection.kind == "dft":
                    y = torch.fft.fft(y, dim=-1, norm="ortho")
                y = y.index_select(-1, indices)
                r = (y @ basis.reshape(-1, basis.shape[-1]).conj().T/self.variance)
                r = r.reshape(data.shape[0], 3, self.candidates, self.taps)
            output.append(r)
        result = torch.stack(output)
        if self.device.type == "cuda":
            torch.cuda.synchronize(self.device)
        return result, time.perf_counter()-started

    def score_correlations(self, correlations):
        started = time.perf_counter()
        if correlations.shape[0] != len(self.blocks):
            raise ValueError("correlation block count changed")
        for runtime in self.runtimes:
            runtime.reset()
        for r in correlations:
            for runtime in self.runtimes:
                runtime.update(r)
        # [models,batch,H0/bit0/bit1,candidates].  Candidate and channel-model
        # priors are normalized under *both* hypotheses before the ratio.
        evidence = torch.stack([runtime.scores() for runtime in self.runtimes])
        candidate_evidence = torch.logsumexp(evidence, dim=-1)-math.log(self.candidates)
        mixed = torch.logsumexp(candidate_evidence, dim=0)-math.log(len(self.coherences))
        bit_scores = mixed[:, 1:]-mixed[:, :1]
        model_bit_scores = candidate_evidence[:, :, 1:]-candidate_evidence[:, :, :1]
        if not bool(torch.isfinite(bit_scores).all()):
            raise FloatingPointError("nonfinite joint likelihood")
        if self.device.type == "cuda":
            torch.cuda.synchronize(self.device)
        return dict(bit_scores=bit_scores.detach().cpu().numpy(),
                    model_bit_scores=model_bit_scores.detach().cpu().numpy(),
                    hypothesis_log_evidence=candidate_evidence.detach().cpu().numpy(),
                    state_seconds=time.perf_counter()-started)

    def score(self, raw):
        r, elapsed = self.correlations(raw)
        result = self.score_correlations(r)
        result.update(correlation_seconds=elapsed, preparation_seconds=self.preparation_seconds,
                      work=self.work())
        return result

    def work(self):
        return dict(raw_samples=self.samples, blocks=len(self.blocks), taps=self.taps,
                    candidates=self.candidates, models=len(self.coherences),
                    projected_modes=self.projected_modes, erased_samples=self.erased_samples,
                    zero_basis_blocks=len(self.zero_basis_blocks),
                    owned_bytes_estimate=self.owned_bytes_estimate,
                    preparation_peak_bytes_estimate=self.preparation_peak_bytes_estimate,
                    device=str(self.device), bulk_dtype=str(self.dtype),
                    preparation_dtype="complex128 CPU", score_dtype="float64",
                    covariance_model="block-constant proper complex OU physical taps",
                    projection=self.projection_mode,
                    admission="external frozen full-procedure empirical calibration required")


def _smooth(x):
    x = np.clip(x, 0., 1.)
    return x*x*x*(10+x*(-15+6*x))


def _sample_cubic(source, position):
    """Four-point source interpolation with explicit zero extension."""
    center = np.floor(position).astype(np.int64)
    u = position-center
    weights = (-u*(u-1)*(u-2)/6, (u+1)*(u-1)*(u-2)/2,
               -(u+1)*u*(u-2)/2, (u+1)*u*(u-1)/6)
    result = np.zeros(position.shape, np.complex128)
    for shift, weight in zip((-1, 0, 1, 2), weights):
        ix = center+shift
        valid = (ix >= 0) & (ix < len(source))
        result[valid] += weight[valid]*source[ix[valid]]
    return result


def _sample_cubic_rows(source, position):
    """The same four-point interpolation, sharing geometry across source rows."""
    center = np.floor(position).astype(np.int64)
    u = position-center
    weights = (-u*(u-1)*(u-2)/6, (u+1)*(u-1)*(u-2)/2,
               -(u+1)*u*(u-2)/2, (u+1)*u*(u-1)/6)
    result = np.zeros((source.shape[0], *position.shape), np.complex128)
    for shift, weight in zip((-1, 0, 1, 2), weights):
        ix = center+shift
        valid = (ix >= 0) & (ix < source.shape[-1])
        result[:, valid] += weight[valid]*source[:, ix[valid]]
    return result


def public_spectral_bands(manifest, *, pilot_use="both", cfo_bound_hz=0.,
                          clock_bound_ppm=0., auxiliary_half_width_hz=200.):
    """Declared engineering envelopes; stochastic tails remain noncompact."""
    ch, fmt, hp = manifest["channel"], manifest["format"], manifest["helper_plan"]
    helper = bool(manifest.get("helper_enabled", True))
    if pilot_use not in ("both", "low", "high", "none"):
        raise ValueError("pilot_use must be both, low, high or none")
    doppler = abs(float(ch["spread_doppler_hz"]))
    width = float(ch.get("random_doppler_width_hz", 0.))
    # These are total-width conventions in the native channel: +/-3B rays,
    # +/-10B transformed-Gaussian selective coefficients.  Fixed centers are
    # at0,D, not +/-D/2.  Symmetric coverage is deliberately conservative.
    # AM and ray evolution are independent selections. In native manifests
    # channel_evolution is an effective versioned law, not the CLI request.
    # Historical manifests without coefficient_law contain only sine AM.
    stochastic_am = ch.get("selective_coefficient_law", "sinusoidal-v1") == "gaussian-erf-v1"
    am = float(ch.get("selective_modulation_rate_hz", doppler))
    enabled_am = (float(ch.get("selective_slow_depth", 0.)) > 0 or
                  float(ch.get("selective_fast_depth", 0.)) > 0)
    excursion = doppler+3*width+(10*am if stochastic_am else 1.1*am) * enabled_am
    excursion += float(ch.get("delay_motion_doppler_bound_hz", 0.))
    cfo = abs(_finite(cfo_bound_hz, "CFO bound"))
    clock = abs(_finite(clock_bound_ppm, "clock bound"))*1e-6
    data_if = float(ch["nominal_conversion_if_hz"])
    data_half = float(fmt["occupied_bandwidth_hz"])*.5
    required = [(-data_half*(1+clock)-excursion-cfo,
                  data_half*(1+clock)+excursion+cfo)]
    excluded = []
    if helper:
        # Source gates have noncompact skirts. 200Hz is the maintained public
        # auxiliary allowance, not a theorem of zero out-of-band energy.
        gating = 200. if hp.get("pilot_dwell_gating", False) else 0.
        for name in ("low", "high"):
            center = float(hp[name+"_hz"])-data_if
            half = excursion+cfo+abs(center)*clock+gating
            band = (center-half, center+half)
            if pilot_use in ("both", name):
                required.append(band)
            else:
                excluded.append(band)
        center = float(hp["auxiliary_hz"])-data_if
        half = _finite(auxiliary_half_width_hz, "auxiliary half width", positive=True)
        half = half*(1+clock)+excursion+cfo+abs(center)*clock
        excluded.append((center-half, center+half))
    return tuple(required), tuple(excluded)


class DynamicJointReceiver(ProjectedJointReceiver):
    """Public native-export adapter, initially a small bounded acquisition bank.

    A singleton timing/CFO/epoch bank is a *conditioned* diagnostic, not blind
    acquisition.  Dynamic gains approximate unmodeled channel evolution but do
    not make omitted timing/clock hypotheses exhaustive. ``candidate_coordinates``
    optionally supplies correlated (epoch, arrival, CFO, clock ppm) hypotheses
    without expanding their Cartesian product. It overrides the three numeric
    banks; ``selected_epochs`` still restricts allowed epochs. Caller order is
    retained and bound in the procedure identity. Adaptive selection and its
    admission calibration belong to the outer procedure, not this scorer.
    """

    def __init__(self, manifest, raw_templates, epoch_offsets, *,
                 arrival_seconds=(0.,), frequencies_hz=(0.,), clock_ppm=(0.,),
                 selected_epochs=None, block_seconds=.25,
                 coherence_seconds=(.5, math.inf), pilot_use="both",
                 required_bands_hz=None, auxiliary_exclusion_hz=None,
                 auxiliary_half_width_hz=200., valid=None, device="cpu",
                 precision="fp32", memory_limit_bytes=512*1024**2,
                 candidate_coordinates=None):
        m = manifest
        if m.get("schema") == "triad-helper-whole-bit-public-v1" or m.get("whole_bit_calendar"):
            raise ValueError("whole-bit train needs an explicit calendar/template adapter")
        source = np.asarray(raw_templates)
        epochs = tuple(int(x) for x in epoch_offsets)
        if source.ndim != 3 or source.shape[:2] != (len(epochs), 2):
            raise ValueError("templates need [public epochs,2,source samples]")
        chosen = epochs if selected_epochs is None else tuple(int(x) for x in selected_epochs)
        if not chosen or any(x not in epochs for x in chosen) or len(set(chosen)) != len(chosen):
            raise ValueError("selected epochs must be a unique nonempty public subset")
        if candidate_coordinates is None:
            arrivals = tuple(_finite(x, "arrival") for x in arrival_seconds)
            cfo = tuple(_finite(x, "CFO") for x in frequencies_hz)
            clocks = tuple(_finite(x, "clock ppm") for x in clock_ppm)
            candidates = tuple(itertools.product(chosen, arrivals, cfo, clocks))
        else:
            # Bound iteration before allocating templates or numerical designs.
            # In particular, do not first materialize an unbounded generator.
            candidates_list, seen = [], set()
            try:
                coordinate_iterator = iter(candidate_coordinates)
            except TypeError as error:
                raise ValueError("candidate coordinates must be an iterable") from error
            for coordinate in coordinate_iterator:
                if len(candidates_list) == 4096:
                    raise ValueError("direct dynamic adapter limited to4096 public hypotheses")
                try:
                    coordinate = tuple(coordinate)
                except TypeError as error:
                    raise ValueError("each candidate needs epoch, arrival, CFO and clock ppm") from error
                if len(coordinate) != 4:
                    raise ValueError("each candidate needs epoch, arrival, CFO and clock ppm")
                epoch = coordinate[0]
                if (isinstance(epoch, (bool, np.bool_)) or
                        not isinstance(epoch, (int, np.integer)) or int(epoch) not in chosen):
                    raise ValueError("candidate epoch must be an integer in selected epochs")
                if any(isinstance(value, (bool, np.bool_)) for value in coordinate[1:]):
                    raise ValueError("candidate arrival, CFO and clock ppm must be finite numbers")
                try:
                    candidate = (int(epoch), *(_finite(value, name) for value, name in
                        zip(coordinate[1:], ("arrival", "CFO", "clock ppm"))))
                except (TypeError, ValueError, OverflowError) as error:
                    raise ValueError("candidate arrival, CFO and clock ppm must be finite numbers") from error
                if candidate in seen:
                    raise ValueError("candidate coordinates must be unique")
                seen.add(candidate)
                candidates_list.append(candidate)
            if not candidates_list:
                raise ValueError("candidate coordinates must be nonempty")
            candidates = tuple(candidates_list)
            # Plan the projection from the actual sparse bank, not unused
            # defaults or the superset from which a coarse screen selected it.
            chosen, arrivals, cfo, clocks = tuple(
                tuple(dict.fromkeys(candidate[axis] for candidate in candidates))
                for axis in range(4))
        if any(not values or len(set(values)) != len(values) for values in (arrivals, cfo, clocks)):
            raise ValueError("acquisition banks must be nonempty and unique")
        if any(1+q*1e-6 <= 0 for q in clocks):
            raise ValueError("clock scale must be positive")
        calendar, channel = m["calendar"], m["channel"]
        bounds = tuple(calendar["arrival_prior_seconds"])
        if min(arrivals) < bounds[0] or max(arrivals) > bounds[1]:
            raise ValueError("arrival bank leaves public prior")
        if len(candidates) > 4096:
            raise ValueError("direct dynamic adapter limited to4096 public hypotheses")
        fs = int(m["sample_rate_hz"])
        count = int(calendar["capture_samples"])
        block = max(1, round(_finite(block_seconds, "block seconds", positive=True)*fs))
        helper = bool(m.get("helper_enabled", True)) and pilot_use != "none"
        # Helper absence is an explicit physical mode, never inferred from its
        # failed detection.  Pilot-use none on Helper-present data is an
        # empirically calibrated DATA-only projection comparator.
        if required_bands_hz is None:
            required, excluded = public_spectral_bands(m, pilot_use=pilot_use,
                cfo_bound_hz=max(map(abs, cfo)), clock_bound_ppm=max(map(abs, clocks)),
                auxiliary_half_width_hz=auxiliary_half_width_hz)
        else:
            required = tuple(required_bands_hz)
            excluded = ()
        if auxiliary_exclusion_hz is not None:
            excluded = tuple(auxiliary_exclusion_hz)
        delay = float(channel["spread_delay_ms"])*.001
        doppler = float(channel["spread_doppler_hz"])
        two_rays = delay != 0 or doppler != 0
        powers = (.5, .5) if two_rays else (1.,)
        rays = ((0., 0.), (delay, doppler)) if two_rays else ((0., 0.),)
        hp = m["helper_plan"]
        lead = float(calendar["capture_lead_seconds"])
        offset = float(calendar["data_sample_offset"])/fs
        helper_start = float(calendar["helper_start_seconds"])
        helper_end = float(calendar["helper_end_seconds"])
        data_if = float(channel["nominal_conversion_if_hz"])
        helper_gain = float(hp["helper_gain"])*float(m["power"]["post_plan_composite_gain"])
        fractions = {"high": 1/(1+.501187233627272285+.125892541179416721),
                     "low": .501187233627272285/(1+.501187233627272285+.125892541179416721)}
        epoch0 = int(m["public_epoch_seconds"])

        # CFO does not change source coordinates. Preserve caller ordering,
        # while sharing interpolation and pilot construction for candidates
        # with the same epoch, arrival and sample-clock scale.
        grouped = {}
        for ci, (epoch, arrival, frequency, q) in enumerate(candidates):
            grouped.setdefault((epoch, arrival, q), []).append((ci, frequency))
        source_groups = tuple((epochs.index(epoch), epoch, arrival, 1+q*1e-6, members)
                              for (epoch, arrival, q), members in grouped.items())

        def basis(start, stop):
            time_rx = np.arange(start, stop, dtype=np.float64)/fs-lead
            result = np.zeros((3, len(candidates), len(rays), stop-start), np.complex128)
            if not len(time_rx):
                return result
            rotations, ray_rotations = {}, {}
            for ei, epoch, arrival, scale, members in source_groups:
                for ri, (ray_delay, ray_doppler) in enumerate(rays):
                    # Cubic interpolation has a finite, zero-extended source
                    # domain including its complete endpoint stencil. This
                    # is not a nominal DATA gate: tails in source remain part
                    # of the support and every delayed ray is checked.
                    first = ((time_rx[0]-arrival-ray_delay)*scale-offset)*fs
                    last = ((time_rx[-1]-arrival-ray_delay)*scale-offset)*fs
                    has_data = last >= -2 and first < source.shape[-1]+1
                    if not helper and not has_data:
                        continue
                    ts = (time_rx-arrival-ray_delay)*scale
                    p = np.zeros(stop-start, np.complex128)
                    if helper:
                        envelope = _smooth((ts-helper_start)/.004)*_smooth((helper_end-ts)/.004)
                        if hp.get("pilot_dwell_gating", False):
                            local = np.remainder((epoch0+epoch)%2+ts, .4)
                            envelope *= _smooth(np.minimum(local, .4-local)/.004)
                        for name in ("low", "high"):
                            if pilot_use in ("both", name):
                                tone = float(hp[name+"_hz"])-data_if
                                p += helper_gain*math.sqrt(fractions[name])*envelope*np.exp(
                                    2j*np.pi*np.remainder(tone*ts, 1.))
                    if has_data:
                        data = _sample_cubic_rows(source[ei], (ts-offset)*fs)
                    else:
                        data = np.zeros((2, stop-start), np.complex128)
                    if ri not in ray_rotations:
                        ray_rotations[ri] = np.exp(2j*np.pi*np.remainder(ray_doppler*time_rx, 1.))
                    for ci, frequency in members:
                        if frequency not in rotations:
                            rotations[frequency] = np.exp(2j*np.pi*np.remainder(frequency*time_rx, 1.))
                        common = rotations[frequency]*ray_rotations[ri]
                        result[0, ci, ri] = p*common
                        result[1:, ci, ri] = (data+p)*common
            return result

        self.manifest = m
        self.candidate_coordinates = candidates
        self.required_bands_hz, self.auxiliary_exclusion_hz = required, excluded
        self.pilot_use = pilot_use
        self.source_basis = basis
        super().__init__(sample_rate=fs, samples=count, block_samples=block,
            basis=basis, required_bands=required, excluded_bands=excluded,
            noise_variance=float(m["power"]["complex_noise_variance"]),
            stationary_power=powers, coherence_seconds=coherence_seconds, valid=valid,
            device=device, precision=precision, memory_limit_bytes=memory_limit_bytes,
            candidate_count=len(candidates),
            projection_mode="dft" if bool(m.get("helper_enabled", True)) else "valid-rows")
        self.procedure_id = hashlib.sha256(json.dumps(dict(
            channel=m["channel"], format=m["format"], calendar=calendar,
            helper_plan=hp, helper_enabled=bool(m.get("helper_enabled", True)),
            power=m["power"], candidates=candidates, block_samples=block,
            coherences=[str(x) for x in self.coherences], pilot_use=pilot_use,
            required=required, excluded=excluded, precision=precision,
            templates_sha256=hashlib.sha256(np.ascontiguousarray(source).view(np.uint8)).hexdigest(),
            valid_sha256=hashlib.sha256(self.valid.tobytes()).hexdigest(),
            implementation_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()),
            sort_keys=True).encode()).hexdigest()
