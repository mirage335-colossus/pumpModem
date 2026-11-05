#!/usr/bin/env python3
"""Measure finite clock-grid mismatch in public two-ray DATA template subspaces.

The tool consumes only the keyed public source alternatives and public geometry.
It does not read observations, evaluator truth, or oscillator draws.  It evaluates
grid-cell centers plus outer corners against their independently nearest declared
clock/rate hypotheses, then permits one common arrival translation within half
the receiver lag step.  The reported minimum principal-energy retention is a
sampled, finite-grid diagnostic, not a continuous worst-case bound.

The propagation model matches the receiver's declared two-ray component model
(audio source warp, RX-clock Doppler scaling, delayed branch, continuous causal
FIR, and 900-Hz decimation). It does not reproduce random frequency-selective
AM phases; this measures clock/rate quantization alone.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import time

import numpy as np
from scipy import interpolate, optimize, signal

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))
from triad_helper_receiver import (DATA_SAMPLE_RATE, DECIMATION, RAW_SAMPLE_RATE,
                                   clock_warp, data_fir_taps, fractional_delay)


def _unit_subspace(columns: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Return a rank-aware orthonormal basis and inverse-square-root Gram."""
    gram = columns.conj().T @ columns
    values, vectors = np.linalg.eigh(gram)
    threshold = max(float(values[-1]), 1e-300) * 1e-10
    keep = values > threshold
    if not np.any(keep):
        raise ValueError("two-ray public template subspace has zero numerical rank")
    whitener = vectors[:, keep] / np.sqrt(values[keep])
    return columns @ whitener, whitener


def _fractional_xcorr(a: np.ndarray, b: np.ndarray, max_shift: float) -> tuple[np.ndarray, np.ndarray]:
    """Cross-correlate two short complex bases and interpolate at fractional lags."""
    lags = np.arange(-math.ceil(max_shift) - 2, math.ceil(max_shift) + 3, dtype=float)
    # scipy.correlate(x, y) conjugates y; placing the candidate first gives
    # <target, shifted-candidate>, which composes with its Gram whitener.
    corr = signal.correlate(b, a, mode="full", method="fft")
    integer_lags = signal.correlation_lags(len(b), len(a), mode="full")
    return integer_lags, interpolate.CubicSpline(integer_lags, corr)


def _best_retention(target: np.ndarray, candidate: np.ndarray, half_lag_samples: float,
                   timing_center_samples: float = 0.):
    """Largest minimum squared principal cosine over common arrival shifts."""
    q_target, _ = _unit_subspace(target)
    q_candidate, candidate_whitener = _unit_subspace(candidate)
    if q_candidate.shape[1] < q_target.shape[1]:
        return 0., timing_center_samples, 0., [0.] * q_target.shape[1], 0.
    support = abs(timing_center_samples) + half_lag_samples
    curves = [[_fractional_xcorr(q_target[:, i], candidate[:, j], support)
               for j in range(candidate.shape[1])] for i in range(q_target.shape[1])]
    lag_values = curves[0][0][0]
    raw_lag_values = lag_values[(lag_values >= timing_center_samples - half_lag_samples) &
                                (lag_values <= timing_center_samples + half_lag_samples)]

    def at(lag: float) -> float:
        matrix = np.empty((q_target.shape[1], candidate.shape[1]), dtype=np.complex128)
        for i in range(q_target.shape[1]):
            for j in range(candidate.shape[1]):
                _, curve = curves[i][j]
                matrix[i, j] = curve(lag)
        matrix = matrix @ candidate_whitener
        singular = np.linalg.svd(matrix, compute_uv=False)
        # Normalize candidate columns as well (whitener does so); normalize
        # target basis already orthonormal. Cross-correlation is divided by
        # neither signal length because the vectors have actual norms encoded
        # by orthogonalization above.
        return float(min(1.0, singular[-1] ** 2))

    vals = [(float(lag), at(float(lag))) for lag in raw_lag_values]
    best_lag, best = max(vals, key=lambda item: item[1])
    if len(raw_lag_values) >= 3:
        lo = max(timing_center_samples - half_lag_samples, best_lag - 1.)
        hi = min(timing_center_samples + half_lag_samples, best_lag + 1.)
        if hi > lo:
            result = optimize.minimize_scalar(lambda shift: -at(float(shift)), bounds=(lo, hi),
                                              method="bounded", options={"xatol": 1e-4})
            if -result.fun > best:
                best, best_lag = float(-result.fun), float(result.x)
    matrix = np.empty((q_target.shape[1], candidate.shape[1]), dtype=np.complex128)
    for i in range(q_target.shape[1]):
        for j in range(candidate.shape[1]):
            _, curve = curves[i][j]
            matrix[i, j] = curve(best_lag)
    best_singular = np.linalg.svd(matrix @ candidate_whitener, compute_uv=False)
    by_direction = (np.minimum(1., best_singular**2)).tolist()
    centered = at(timing_center_samples)
    return best, best_lag, centered, by_direction, best_lag - timing_center_samples


def _candidate_points(clocks: tuple[float, ...], rates: tuple[float, ...]) -> list[tuple[float, float, str]]:
    """Return all joint cell centers plus the four outer corners, deduplicated."""
    if len(clocks) < 2 or len(rates) < 2:
        raise ValueError("clock-loss grid requires at least two clock and two RX-rate nodes")
    if tuple(sorted(clocks)) != clocks or tuple(sorted(rates)) != rates:
        raise ValueError("clock and RX-rate grids must be strictly increasing")
    if len(set(clocks)) != len(clocks) or len(set(rates)) != len(rates):
        raise ValueError("clock and RX-rate grid nodes must be unique")
    points = [(float((a + b) / 2), float((c + d) / 2), "cell-midpoint")
              for a, b in zip(clocks, clocks[1:]) for c, d in zip(rates, rates[1:])]
    for q in (clocks[0], clocks[-1]):
        for r in (rates[0], rates[-1]):
            points.append((float(q), float(r), "outer-endpoint"))
    return points


class PublicClockGridLoss:
    def __init__(self, fixture: str | Path, clock_nodes_ppm, doppler_scale_nodes,
                 *, frontend_profile="wide", lag_step_samples=4,
                 epoch_indices=None, bits=(0, 1)):
        self.folder = Path(fixture)
        self.manifest = json.loads((self.folder / "manifest.json").read_text())
        m = self.manifest
        if m.get("schema") != "triad-helper-public-v1" or not m.get("templates", {}).get("enabled"):
            raise ValueError("fixture must contain public triad-helper-v1 DATA templates")
        if frontend_profile not in ("wide", "legacy"):
            raise ValueError("frontend_profile must be legacy or wide")
        self.profile = frontend_profile
        self.clocks = tuple(map(float, clock_nodes_ppm))
        self.rates = tuple(map(float, doppler_scale_nodes))
        self.points = _candidate_points(self.clocks, self.rates)
        self.lag_step = int(lag_step_samples)
        if self.lag_step <= 0:
            raise ValueError("lag step must be positive")
        shape = tuple(m["templates"]["shape"])
        path = self.folder / m["templates"]["file"]
        if path.stat().st_size != math.prod(shape) * 16:
            raise ValueError("public template file size does not match manifest")
        self.templates = np.memmap(path, dtype="<c16", mode="r", shape=shape)
        self.template_sha256 = self._sha256(path)
        self.epoch_indices = tuple(range(shape[0])) if epoch_indices is None else tuple(map(int, epoch_indices))
        self.bits = tuple(map(int, bits))
        if (not self.epoch_indices or len(set(self.epoch_indices)) != len(self.epoch_indices) or
                min(self.epoch_indices) < 0 or max(self.epoch_indices) >= shape[0]):
            raise ValueError("epoch indices must be a nonempty unique subset of public templates")
        if not self.bits or len(set(self.bits)) != len(self.bits) or any(bit not in (0, 1) for bit in self.bits):
            raise ValueError("bits must be a nonempty subset of 0 and 1")
        self.fs = int(m["sample_rate_hz"])
        if self.fs != RAW_SAMPLE_RATE:
            raise ValueError("clock-grid diagnostic currently requires the 18-kHz public receiver")
        self.taps = data_fir_taps(frontend_profile)
        self.fs_out = DATA_SAMPLE_RATE
        self.decimation = DECIMATION
        self.capture_lead = float(m["calendar"]["capture_lead_seconds"])
        self.source_offset = float(m["calendar"]["data_sample_offset"]) / self.fs
        self.arrival_low, self.arrival_high = map(float, m["calendar"]["arrival_prior_seconds"])
        self.delay = float(m["channel"]["spread_delay_ms"]) * .001
        self.doppler = float(m["channel"]["spread_doppler_hz"])
        self.lagstep_samples = self.lag_step
        self.clock_half_lag = self.lag_step / 2
        self._cache = {}

    @staticmethod
    def _sha256(path):
        digest = hashlib.sha256()
        with Path(path).open("rb") as src:
            for block in iter(lambda: src.read(1024 * 1024), b""):
                digest.update(block)
        return digest.hexdigest()

    def run(self) -> dict:
        started = time.perf_counter()
        rows = []
        epochs = self.manifest["templates"]["epoch_offsets_seconds"]
        for ei in self.epoch_indices:
            epoch = epochs[ei]
            for bit in self.bits:
                for clock, rate, kind in self.points:
                    nearest_clock = min(self.clocks, key=lambda value: abs(value - clock))
                    nearest_rate = min(self.rates, key=lambda value: abs(value - rate))
                    target, candidate = self._render_basis_pair(ei, bit, clock, rate,
                                                               nearest_clock, nearest_rate)
                    arrival_mid = (self.arrival_low + self.arrival_high) / 2
                    actual_q, grid_q = 1 + clock * 1e-6, 1 + nearest_clock * 1e-6
                    actual_center = ((self.capture_lead + arrival_mid) / rate +
                                     self.source_offset / actual_q +
                                     len(self.templates[ei, bit]) / (2 * self.fs * actual_q)) * self.fs_out
                    grid_center = ((self.capture_lead + arrival_mid) / nearest_rate +
                                   self.source_offset / grid_q +
                                   len(self.templates[ei, bit]) / (2 * self.fs * grid_q)) * self.fs_out
                    timing_center = grid_center - actual_center
                    energy, shift, centered_energy, by_direction, residual_shift = _best_retention(
                        target, candidate, self.clock_half_lag, timing_center)
                    rows.append(dict(case=kind, clock_ppm=clock, doppler_scale=rate,
                        nearest_clock_ppm=nearest_clock, nearest_doppler_scale=nearest_rate,
                        epoch_offset_seconds=epoch, bit=bit,
                        minimum_principal_energy_retention=energy,
                        principal_energy_retention_by_direction=by_direction,
                        loss_db=10 * math.log10(max(energy, 1e-300)),
                        best_common_timing_shift_samples=shift,
                        analytic_timing_center_samples=timing_center,
                        best_residual_timing_shift_samples=residual_shift,
                        centered_shift_principal_energy_retention=centered_energy,
                        true_subspace_rank=int(_unit_subspace(target)[0].shape[1])))
                # Bound retained basis workspaces to one public epoch/bit pair.
                self._cache.clear()
        return dict(schema="triad-helper-clock-grid-loss-v1",
            scope="public two-ray DATA template clock/rate quantization after the selected continuous FIR and 900-Hz decimation",
            identity=dict(manifest_sha256=self._sha256(self.folder / "manifest.json"),
                          templates_sha256=self.template_sha256,
                          diagnostic_source_sha256=self._sha256(__file__),
                          receiver_source_sha256=self._sha256(TOOLS / "triad_helper_receiver.py")),
            grid=dict(clock_ppm_nodes=self.clocks, doppler_scale_nodes=self.rates,
                      tested_points=len(self.points), arrival_prior_seconds=[self.arrival_low, self.arrival_high],
                      common_arrival_translation_half_width_samples=self.clock_half_lag,
                      frontend=self.profile, receiver_lag_step_samples=self.lag_step,
                      epoch_indices=self.epoch_indices, epoch_offsets_seconds=[epochs[i] for i in self.epoch_indices],
                      bits=self.bits),
            results=rows,
            summary=dict(worst_sampled_principal_energy_retention=min(r["minimum_principal_energy_retention"] for r in rows),
                         worst_sampled_loss_db=min(r["loss_db"] for r in rows),
                         median_sampled_loss_db=float(np.median([r["loss_db"] for r in rows])),
                         matrix_singular_values_are_directional=True,
                         samples=len(rows), wall_seconds=time.perf_counter() - started),
            limitations=["Sampled finite grid locations and two-ray template subspaces; not a continuous-grid worst-case bound.",
                "Four-point cubic source clock interpolation and fractional delay match the receiver approximation, not exact bandlimited truth.",
                "Frequency-selective modulation phases and stochastic channel variations are omitted; this isolates clock/rate grid quantization.",
                "Continuous common arrival retiming over +/- half the lag step is allowed; this excludes the receiver's actual four-sample timing-lattice quantization loss.",
                "The grid's likelihood weighting and candidate-bank multiplicity are not modeled; this is not a complete finite-bank worst-case bound and only minimum subspace energy retention is reported."])

    def _render_basis_pair(self, epoch_index, bit, clock_ppm, scale, grid_clock, grid_scale):
        """Render direct/delayed columns, with coordinate-zero preserving clocks."""
        def render(qppm, rate):
            key = (epoch_index, bit, qppm, rate)
            if key in self._cache:
                return self._cache[key]
            q = 1 + qppm * 1e-6
            arrival = (self.arrival_low + self.arrival_high) / 2
            history = (len(self.taps) - 1) / self.fs
            public_data_midpoint = self.capture_lead + self.source_offset + arrival
            crop_raw = max(0, math.floor((public_data_midpoint - history) * self.fs_out) * self.decimation)
            anchor = ((self.capture_lead + arrival) / rate + self.source_offset / q) * self.fs - crop_raw
            delay = self.delay / rate * self.fs
            src = clock_warp(self.templates[epoch_index, bit], qppm)
            columns = []
            for branch in (0, 1):
                offset = anchor + (delay if branch else 0.)
                wide = fractional_delay(src, offset)
                if branch:
                    times = (np.arange(len(wide), dtype=np.float64) / self.fs -
                             (self.capture_lead + arrival) / rate)
                    wide *= np.exp(2j * np.pi * self.doppler * rate * times)
                filtered = signal.upfirdn(self.taps, wide, down=self.decimation)
                columns.append(filtered)
            length = max(map(len, columns))
            output = np.zeros((length, 2), np.complex128)
            for col, value in enumerate(columns):
                output[:len(value), col] = value
            output.flags.writeable = False
            self._cache[key] = output
            return output
        a, b = render(clock_ppm, scale), render(grid_clock, grid_scale)
        length = max(len(a), len(b))
        target, candidate = np.zeros((length, 2), complex), np.zeros((length, 2), complex)
        target[:len(a)] = a
        candidate[:len(b)] = b
        return target, candidate


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", required=True, type=Path)
    parser.add_argument("--clock-ppm", required=True, help="comma-separated declared public clock grid nodes")
    parser.add_argument("--doppler-scale", required=True, help="comma-separated declared RX-rate grid nodes")
    parser.add_argument("--frontend", choices=("wide", "legacy"), default="wide")
    parser.add_argument("--lag-step", type=int, default=4)
    parser.add_argument("--epoch-index", type=int, action="append", help="public template index; repeat or omit for all")
    parser.add_argument("--bit", type=int, choices=(0, 1), action="append", help="repeat or omit for both")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    if args.output.exists():
        parser.error("output already exists")
    try:
        clocks = tuple(float(item) for item in args.clock_ppm.split(","))
        rates = tuple(float(item) for item in args.doppler_scale.split(","))
        report = PublicClockGridLoss(args.fixture, clocks, rates,
                                     frontend_profile=args.frontend,
                                     lag_step_samples=args.lag_step,
                                     epoch_indices=args.epoch_index,
                                     bits=args.bit or (0, 1)).run()
    except (ValueError, OSError) as exc:
        parser.error(str(exc))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(json.dumps(report["summary"]))


if __name__ == "__main__":
    main()
