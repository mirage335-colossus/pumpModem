#!/usr/bin/env python3
"""Bounded batched correlations for the isolated Helper DATA experiment.

This reuses the maintained FFT workspace's public templates, exact masked Grams
and edge refinements. It caches the same observable projection and two-tap
Gaussian factors across CFOs. It supplies a working empirical statistic only.
"""
from __future__ import annotations

import math
import time

import numpy as np
from scipy import fft
from scipy.special import gammaincinv
import torch

from triad_gaussian_window import PreparedWindowedGaussian
from triad_live_receiver import EmpiricalReceiver


def _check_precision(device, precision):
    if str(device).startswith("cuda") and precision == "fp32":
        if torch.backends.cuda.matmul.allow_tf32 or torch.get_float32_matmul_precision() != "highest":
            raise ValueError("FP32 minimum requires TF32 disabled and highest matmul precision")


class BatchedFFTReceiver:
    """One fixed public epoch/clock/rate workspace, streamed over CFO batches."""

    def __init__(self, basis, sample_rate, lags, *, key, frequencies,
                 reference_variance, precision, device, memory_limit_bytes,
                 preprocessing_identity, frequency_batch_size=8, minimum_fft_size=0):
        _check_precision(device, precision)
        self.receiver = EmpiricalReceiver(
            basis, sample_rate, lags, key=key, frequencies=frequencies,
            reference_variance=reference_variance, frequency_offsets=(0.,),
            precision=precision, device=device, memory_limit_bytes=memory_limit_bytes,
            preprocessing_identity=preprocessing_identity)
        self.workspace = self.receiver._workspace
        self.config = self.receiver._config
        self.device = torch.device(device)
        self.precision = precision
        self.dtype = torch.complex64 if precision == "fp32" else torch.complex128
        self.reference = reference_variance
        if (isinstance(frequency_batch_size, bool) or
                not isinstance(frequency_batch_size, int) or frequency_batch_size < 1):
            raise ValueError("frequency_batch_size must be a positive integer")
        self.batch_size = frequency_batch_size
        w = self.workspace
        if isinstance(minimum_fft_size, bool) or not isinstance(minimum_fft_size, int) or minimum_fft_size < 0:
            raise ValueError("minimum_fft_size must be a nonnegative integer")
        target_nfft = fft.next_fast_len(max(w.nfft, minimum_fft_size))
        if target_nfft != w.nfft:
            extra = (target_nfft - w.nfft) * (320 + (192 if self.device.type == "cuda" else 0))
            if w.array_bound + extra >= memory_limit_bytes:
                raise MemoryError("aligned CFO FFT padding exceeds explicit memory limit")
            w.array_bound += extra
            w.nfft = target_nfft
            if self.device.type == "cpu":
                w.template_fft = fft.fft(np.asarray(w.columns, dtype=w.correlation_dtype),
                                        n=w.nfft, axis=-1, workers=1).conj()
            else:
                w.template_fft = torch.fft.fft(torch.as_tensor(w.columns, dtype=self.dtype, device=self.device),
                                               n=w.nfft, dim=-1).conj()
        # Charge resident factors and the largest FFT/product/phase/score batch.
        self.extra_array_bound = (len(lags) * 4096 + w.nfft * 64 + w.sampled.samples * 48 +
            self.batch_size * (w.nfft * 80 + w.sampled.samples * 48 + len(lags) * 2048))
        self.array_bound = w.array_bound + self.extra_array_bound
        if self.array_bound >= memory_limit_bytes:
            raise MemoryError("batched FFT/factor reserve exceeds explicit memory limit")
        self.memory_limit_bytes = memory_limit_bytes
        G = torch.from_numpy(np.array(w.gram / reference_variance))
        values, vectors = torch.linalg.eigh((G + G.mH) * .5)
        scale = G.abs().amax(dim=(-2, -1)).clamp_min(torch.finfo(torch.float64).tiny)
        if bool((values[:, 0] < -1e-10 * scale).any()):
            raise ValueError("whole-bank Gram is not positive semidefinite")
        keep = values > values[:, -1:].clamp_min(0.) * 1e-10
        self.rank = keep.sum(-1).numpy()
        self.dof = w.valid_count - self.rank
        if np.any(self.dof <= 0):
            raise ValueError("fixed group has no positive residual calibration degrees of freedom")
        kept = torch.where(keep, values, 0.)
        projected_G = ((vectors * kept[:, None, :]) @ vectors.mH).numpy()
        self.vectors = vectors.to(self.device)
        self.keep = keep.to(self.device)
        self.inverse_values = torch.where(keep, 1 / values.clamp_min(torch.finfo(torch.float64).tiny), 0.).to(self.device)
        mapping = np.asarray([[0, 1], [2, 3]])
        bit_grams = projected_G[:, mapping[:, :, None], mapping[:, None, :]].reshape(1, -1, 2, 2)
        plan = PreparedWindowedGaussian(
            bit_grams, np.asarray([w.sampled.samples / (2 * sample_rate)]),
            doppler_width_hz=0., window_blocks=1, stationary_power=(.5, .5),
            memory_limit_bytes=memory_limit_bytes)
        self.whitener = torch.as_tensor(plan._whitener[0].reshape(-1, 2, 2, 2),
                                       dtype=torch.complex128, device=self.device)
        window = plan._windows[0]
        self.modes = torch.as_tensor(window.transform.reshape(-1, 2, 2, 2),
                                    dtype=self.dtype, device=self.device)
        self.eigenvalues = torch.as_tensor(window.eigenvalues.reshape(-1, 2, 2),
                                          dtype=torch.float64, device=self.device)
        self.indices = torch.as_tensor(w.lags, dtype=torch.int64, device=self.device)
        self.fft_indices = torch.arange(w.nfft, device=self.device)
        self.times = torch.as_tensor(np.asarray(w.times, np.float64), device=self.device)
        self.template_fft = torch.as_tensor(w.template_fft, dtype=self.dtype, device=self.device)
        self.base_frequencies = np.asarray(w.sampled.frequencies, np.float64)

    def _correlations(self, observation, offsets, base_spectra):
        """Return [CFO,lag,four columns] on the selected device."""
        w = self.workspace
        offsets = np.asarray(offsets, np.float64)
        bins = offsets * (w.nfft / w.sampled.sample_rate)
        integer = np.rint(bins)
        aligned = np.abs(bins - integer) <= 1e-10
        correlations = torch.empty((len(offsets), len(w.lags), 4),
                                   dtype=torch.complex128, device=self.device)
        for base in dict.fromkeys(map(float, self.base_frequencies)):
            columns = np.flatnonzero(self.base_frequencies == base)
            transformed = torch.empty((len(offsets), w.nfft), dtype=self.dtype, device=self.device)
            if aligned.any():
                shifts = torch.as_tensor(integer[aligned], dtype=torch.int64, device=self.device)
                ids = (self.fft_indices[None] + shifts[:, None]) % w.nfft
                transformed[torch.as_tensor(np.flatnonzero(aligned), device=self.device)] = base_spectra[base][ids]
            if (~aligned).any():
                frequencies = torch.as_tensor(offsets[~aligned] + base, device=self.device)
                angles = -2 * math.pi * frequencies[:, None] * self.times[None]
                rotation = torch.polar(torch.ones_like(angles), angles).to(self.dtype)
                transformed[torch.as_tensor(np.flatnonzero(~aligned), device=self.device)] = torch.fft.fft(
                    observation[None] * rotation, n=w.nfft, dim=-1)
            column_ids = torch.as_tensor(columns, device=self.device)
            values = torch.fft.ifft(transformed[:, None] * self.template_fft[column_ids][None], dim=-1)
            correlations[:, :, column_ids] = values[:, :, self.indices].transpose(1, 2).to(torch.complex128)
        if w.zero_columns.any():
            mask = torch.as_tensor(w.zero_columns, device=self.device)
            correlations[:, mask] = 0.
        # Retain the maintained public near-edge FP64 raw-dot exception.
        for gi, rows in w.refined_rows.items():
            A = w._compact_basis(int(w.lags[gi]), rows)
            for fi, frequency in enumerate(offsets):
                from triad_fft_acquire import phase
                y = w.observation[rows] * phase(-frequency, w.times[rows])
                correlations[fi, gi] = torch.as_tensor(A.conj().T @ y, device=self.device)
        return correlations

    def score(self, observation, frequencies, *, family_groups=None):
        with self.receiver._lock:
            return self._score(observation, frequencies, family_groups=family_groups)

    def _score(self, observation, frequencies, *, family_groups=None):
        _check_precision(self.device, self.precision)
        started = time.perf_counter()
        w = self.workspace
        values = np.asarray(observation)
        if values.shape != (w.sampled.samples,) or values.dtype.kind != "c" or not np.isfinite(values).all():
            raise ValueError("finite complete observation required")
        frequencies = tuple(float(f) for f in frequencies)
        if not frequencies or not np.isfinite(frequencies).all() or len(set(frequencies)) != len(frequencies):
            raise ValueError("finite unique CFO bank required")
        count = len(frequencies) * len(w.lags)
        family = count if family_groups is None else int(family_groups)
        if family < count or family > 2**53:
            raise ValueError("family_groups must cover this bank and remain exactly representable")
        strict, _ = self.config.budgets()
        delta = math.nextafter(strict.delta_per_segment / family, 0.)
        quantile = gammaincinv(self.dof, delta)
        if not np.isfinite(quantile).all() or np.any(quantile <= 0):
            raise ValueError("variance confidence quantile is unresolved")
        quantile = torch.as_tensor(quantile, dtype=torch.float64, device=self.device)
        w.observation = np.array(values, np.complex128, copy=True)
        energy = math.fsum(map(float, abs(values[w.valid != 0])**2)) / self.reference
        observation_device = torch.as_tensor(values * w.valid, dtype=self.dtype, device=self.device)
        base_spectra = {}
        for base in dict.fromkeys(map(float, self.base_frequencies)):
            angles = -2 * math.pi * base * self.times
            base_spectra[base] = torch.fft.fft(
                observation_device * torch.polar(torch.ones_like(angles), angles).to(self.dtype), n=w.nfft)
        mixture = torch.full((2,), -torch.inf, dtype=torch.float64, device=self.device)
        best = torch.full((2,), -torch.inf, dtype=torch.float64, device=self.device)
        best_indices = torch.zeros(2, dtype=torch.int64, device=self.device)
        low, high = math.inf, 0.
        fft_seconds = score_seconds = 0.
        aligned_count = 0
        for first in range(0, len(frequencies), self.batch_size):
            batch = frequencies[first:first + self.batch_size]
            if self.device.type == "cuda":
                torch.cuda.synchronize(self.device)
            mark = time.perf_counter()
            raw = self._correlations(observation_device, batch, base_spectra) / self.reference
            if self.device.type == "cuda":
                torch.cuda.synchronize(self.device)
            fft_seconds += time.perf_counter() - mark
            mark = time.perf_counter()
            coefficients = (self.vectors.mH[None] @ raw[..., None]).squeeze(-1)
            kept = torch.where(self.keep[None], coefficients, 0.)
            explained = (kept.abs().square() * self.inverse_values[None]).sum(-1)
            residual = energy - explained
            roundoff = 1e-10 * torch.maximum(torch.full_like(explained, energy), explained).clamp_min(torch.finfo(torch.float64).tiny)
            if bool((residual < -roundoff).any()):
                raise ValueError("whole-bank projected energy exceeds actual received energy")
            if bool((residual <= roundoff).any()):
                raise ValueError("whole-bank residual subtraction is unresolved; use raw QR energy")
            alpha = residual / quantile[None]
            projected = (self.vectors[None] @ kept[..., None]).squeeze(-1).reshape(len(batch), len(w.lags), 2, 2)
            white = (self.whitener[None] @ projected[..., None]).squeeze(-1) / torch.sqrt(alpha)[..., None, None]
            z = (white.to(self.dtype)[..., None, :] @ self.modes[None].conj()).squeeze(-2)
            power = z.real.to(torch.float64).square() + z.imag.to(torch.float64).square()
            denominator = alpha[..., None, None] + self.eigenvalues[None]
            scores = (power * (self.eigenvalues[None] / denominator)).sum(-1)
            scores -= torch.log1p(self.eigenvalues[None] / alpha[..., None, None]).sum(-1)
            if not bool(torch.isfinite(scores).all()):
                raise ValueError("nonfinite batched likelihood")
            flat = scores.reshape(-1, 2)
            mixture = torch.logaddexp(mixture, torch.logsumexp(flat, dim=0))
            local, ids = torch.max(flat, dim=0)
            improve = local > best
            best_indices = torch.where(improve, ids + first * len(w.lags), best_indices)
            best = torch.maximum(best, local)
            low = min(low, float(alpha.min())); high = max(high, float(alpha.max()))
            bins = np.asarray(batch) * (w.nfft / w.sampled.sample_rate)
            aligned_count += int(np.count_nonzero(abs(bins - np.rint(bins)) <= 1e-10))
            if self.device.type == "cuda":
                torch.cuda.synchronize(self.device)
            score_seconds += time.perf_counter() - mark
        mixture = (mixture - math.log(count)).cpu().numpy()
        winner = int(np.argmax(mixture))
        selected = int(best_indices[winner])
        fi, li = divmod(selected, len(w.lags))
        key = dict(w.sampled.key)
        key["arrival_seconds"] += int(w.lags[li]) / w.sampled.sample_rate
        key["frequency_hz"] += frequencies[fi]
        return dict(bit=winner, bit_log_likelihoods=mixture.tolist(),
            best_group_public_parameters=key,
            arithmetic=dict(requested_correlation_precision=self.precision,
                observation_fft_device=str(self.device), observation_fft_and_products=str(self.dtype),
                preparation_and_finish="cached FP64 observable Gram/whitening/prior; FP64 residual and evidence on selected device",
                likelihood_bulk=self.precision, tf32_allowed=False, implicit_fallback=False,
                aligned_cfo_reuse="exact cyclic shifts of resident observation FFT; arbitrary CFOs use batched modulation/FFT"),
            calibration=dict(variance_upper_over_reference_range=[low, high],
                rank_histogram=np.bincount(self.rank, minlength=5).tolist(), delta_per_group=delta),
            timing=dict(total_receiver_seconds=time.perf_counter() - started,
                observation_fft_seconds=fft_seconds, residual_and_likelihood_seconds=score_seconds),
            resources=dict(explicit_array_bound_bytes=self.array_bound, memory_limit_bytes=self.memory_limit_bytes,
                frequency_batch_size=self.batch_size, aligned_cfo_count=aligned_count,
                retained_group_scores=False))
