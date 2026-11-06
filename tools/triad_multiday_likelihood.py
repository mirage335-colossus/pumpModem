#!/usr/bin/env python3
"""Persistent, fixed-public-bank statistics for long triad observations.

The sampled source/channel remain external. A bank contains both encrypted
patterns and every public synchronization alternative before any observations.
This module accumulates their *joint* Gram, retaining noise dependence across
bits/candidates. Static Gaussian likelihoods reuse GaussianStateFilter rather
than resetting its channel prior at chunk boundaries.

Whole-Gram Monte Carlo is an exact linear-complex-Gaussian sufficient-statistic
experiment conditional on the supplied, numerically represented waveforms and
computed covariance. It is not a fresh sampled waveform/channel trial. Finite
coherence, adaptive timing/phase paths, estimated noise and physical ASRC noise
require their own model; they cannot be inferred from this whole-window Gram.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import math
import numbers
import time

import numpy as np
from scipy.special import logsumexp
from scipy.stats import beta
import torch

from triad_gaussian_state import GaussianPrior, GaussianStateFilter


def _integer(value, name, minimum=1, maximum=(1 << 64)-1):
    if isinstance(value, bool) or not isinstance(value, numbers.Integral) or not minimum <= int(value) <= maximum:
        raise ValueError(f'{name} must be an integer in [{minimum},{maximum}]')
    return int(value)


def _weights(values, count):
    if values is None:
        return np.full(count, 1/count, np.float64)
    result = np.asarray(values, dtype=np.float64)
    if result.shape != (count,) or not np.isfinite(result).all() or (result < 0).any() or not result.max() > 0:
        raise ValueError('public mixture weights must be finite, nonnegative and nonzero')
    result = result/result.max()
    return result/result.sum()


def mix_log_evidence(model_bit_scores, weights=None):
    """Fixed normalized model mixture; never choose a model from its score.

    First axis enumerates public models. Remaining dimensions must agree and
    end in the two bit alternatives. With equal two-model weights the mixture
    is at least either individual score minus log(2), pointwise for each bit.
    """
    values = np.asarray(model_bit_scores, dtype=np.float64)
    if values.ndim < 2 or values.shape[-1] != 2 or not len(values) or np.isnan(values).any() or np.isposinf(values).any():
        raise ValueError('model likelihoods must end in two bits and contain no NaN/+infinity')
    prior = _weights(weights, len(values))
    with np.errstate(divide='ignore'):
        log_prior = np.log(prior).reshape((-1,)+(1,)*(values.ndim-1))
    return logsumexp(values+log_prior, axis=0)


def binomial_interval(successes, trials, confidence=.95):
    """Two-sided exact Clopper-Pearson interval for a named conditional count."""
    trials = _integer(trials, 'trials')
    successes = _integer(successes, 'successes', 0, trials)
    if not math.isfinite(confidence) or not 0 < confidence < 1:
        raise ValueError('confidence must be in (0,1)')
    alpha = 1-confidence
    low = 0. if successes == 0 else float(beta.ppf(alpha/2, successes, trials-successes+1))
    high = 1. if successes == trials else float(beta.ppf(1-alpha/2, successes+1, trials-successes))
    return [low, high]


def classify_scores(bit_scores, log_threshold, *, true_bit=None, confidence=.95):
    """Report an endpoint decision; threshold/exposure accounting is external."""
    scores = np.asarray(bit_scores, dtype=np.float64)
    if scores.ndim != 2 or scores.shape[1] != 2 or not len(scores) or not np.isfinite(scores).all():
        raise ValueError('finite [trials,2] bit scores required')
    if not math.isfinite(log_threshold):
        raise ValueError('finite fixed log threshold required')
    if true_bit is not None and (isinstance(true_bit, bool) or true_bit not in (0, 1)):
        raise ValueError('true_bit is zero, one or None for null evaluation')
    log_e = logsumexp(scores, axis=-1)-math.log(2)
    selected = np.argmax(scores, axis=-1)
    passed = log_e >= log_threshold
    n = len(scores)
    counts = ({'false_admissions': int(passed.sum()), 'null_rejections': int((~passed).sum())}
              if true_bit is None else
              {'correct_admissions': int((passed & (selected == true_bit)).sum()),
               'wrong_admissions': int((passed & (selected != true_bit)).sum()),
               'misses': int((~passed).sum()),
               'raw_bit_errors': int((selected != true_bit).sum())})
    return dict(trials=n, true_bit=true_bit, counts=counts,
        probability_intervals={k: binomial_interval(v, n, confidence) for k, v in counts.items()},
        confidence=confidence, log_threshold=log_threshold,
        log_e_quantiles=np.quantile(log_e, [.01, .5, .99]).tolist(),
        scope='Fixed-endpoint conditional Gaussian/noise trials; not channel-population reliability, physical admission or repeated-look calibration')


@dataclass
class BankStatistics:
    """A source-bound complete observation; oracle columns are kept separate."""
    gram: np.ndarray
    candidates: int
    signal_count: int
    candidate_weights: np.ndarray
    observed_correlations: np.ndarray
    metadata: dict

    def prepare_covariance(self):
        """Use one visible PSD covariance for both noise draws and likelihoods.

        Finite-precision Gram roundoff is not a certified enclosure of the
        exact sampled Gram. The actual sampled-noise comparison must measure
        that approximation. The resulting conditional MC model is internally
        normalized rather than drawing noise from a silently different matrix.
        """
        if hasattr(self,'_noise_factor'):
            return
        started,cpu = time.perf_counter(),time.process_time()
        values,vectors = np.linalg.eigh(self.gram)
        scale = max(1.,float(values[-1]))
        minimum = float(values[0])
        tolerance = 3e-5 if self.metadata['precision'] == 'fp32' else 2e-10
        if minimum < -tolerance*scale:
            raise FloatingPointError('joint noise Gram is not positive semidefinite within the arithmetic tolerance')
        self._noise_factor = vectors*np.sqrt(np.maximum(values,0))[None,:]
        self._minimum_eigenvalue = minimum
        self._relative_eigenvalue_repair = max(0.,-minimum)/scale
        if minimum < 0:
            repaired = self._noise_factor@self._noise_factor.conj().T
            repaired = (repaired+repaired.conj().T)*.5
            self.metadata['maximum_gram_entry_repair'] = float(abs(repaired-self.gram).max())
            repaired.flags.writeable = False
            self.gram = repaired
        else:
            self.metadata['maximum_gram_entry_repair'] = 0.
        self.metadata.update(minimum_gram_eigenvalue=minimum,
            relative_negative_eigenvalue_repair=self._relative_eigenvalue_repair,
            covariance_factor_cpu_seconds=time.process_time()-cpu,
            covariance_factor_wall_seconds=time.perf_counter()-started)

    @property
    def bank_columns(self):
        return 2*self.candidates

    @property
    def reference_energies(self):
        return self.gram.diagonal().real[self.bank_columns:].copy()

    def static_scores(self, correlations, *, prior_power=1.):
        """Unknown static scalar channel, candidate prior mixed once.

        Only the practical bank columns enter this method. Reference/channel
        truth columns never become templates or select acquisition alternatives.
        """
        self.prepare_covariance()
        r = np.asarray(correlations, dtype=np.complex128)
        if r.shape[-2:] != (2, self.candidates) or not np.isfinite(r).all():
            raise ValueError('correlations must end in [2,candidates]')
        power = float(prior_power)
        if not math.isfinite(power) or power < 0:
            raise ValueError('finite nonnegative public scalar-channel power required')
        diagonal = self.gram.diagonal().real[:self.bank_columns].reshape(2, self.candidates)
        runtime = GaussianStateFilter(GaussianPrior((power,), math.inf), dtype=torch.complex128)
        runtime.update(torch.as_tensor(r[..., None].copy()),
            torch.as_tensor(diagonal[..., None, None].copy()).to(torch.complex128), 0.)
        scores = runtime.finish().detach().cpu().numpy()
        with np.errstate(divide='ignore'):
            return logsumexp(scores+np.log(self.candidate_weights), axis=-1)

    def observed_scores(self, *, prior_power=1.):
        return self.static_scores(self.observed_correlations[:, :self.bank_columns].reshape(-1, 2, self.candidates),
                                  prior_power=prior_power)

    def oracle_scores(self, oracle_correlations):
        """Known channel/phase upper reference, explicitly using supplied truth."""
        r = np.asarray(oracle_correlations, np.complex128)
        if self.signal_count != 2 or r.shape[-1] != 2 or not np.isfinite(r).all():
            raise ValueError('known-channel bit oracle requires exactly two reference signals')
        return 2*r.real-self.reference_energies

    def draw_correlations(self, *, trials, seed, signal_index=None):
        """Joint CN(mean,Gram) draws; all candidate noise remains correlated.

        A sampled-waveform validation must independently compare this law with
        actual noise projected through the same represented sampled templates.
        Small negative Gram eigenvalues are repaired visibly; large ones fail.
        """
        trials = _integer(trials, 'trials', 1, 10_000_000)
        seed = _integer(seed, 'seed', 0, (1 << 63)-1)
        if signal_index is not None:
            signal_index = _integer(signal_index, 'signal index', 0, self.signal_count-1)
        began, cpu = time.perf_counter(), time.process_time()
        width = len(self.gram)
        # Bound caller-visible MC storage rather than allocating trials*width
        # without regard to the explicitly declared budget.
        required = trials*width*16*4+width*width*16*3
        if required > self.metadata['memory_limit_bytes']:
            raise MemoryError('joint Monte Carlo arrays exceed declared memory budget; tile trials explicitly')
        self.prepare_covariance()
        factor = self._noise_factor
        rng = np.random.default_rng(seed)
        z = (rng.standard_normal((trials, width))+1j*rng.standard_normal((trials, width)))/math.sqrt(2)
        draws = z@factor.T
        if signal_index is not None:
            draws += self.gram[:, self.bank_columns+signal_index][None, :]
        diagnostic = dict(trials=trials, seed=seed, signal_index=signal_index,
            minimum_gram_eigenvalue=self._minimum_eigenvalue,
            relative_negative_eigenvalue_repair=self._relative_eigenvalue_repair,
            wall_seconds=time.perf_counter()-began, cpu_seconds=time.process_time()-cpu,
            array_bytes_bound=required,
            scope='Exact joint sufficient-statistic Gaussian experiment for the computed Gram; no fresh sampled channel or physical-noise qualification')
        return dict(bank_correlations=draws[:, :self.bank_columns].reshape(trials, 2, self.candidates),
            oracle_correlations=draws[:, self.bank_columns:], diagnostics=diagnostic)

    def identity(self):
        digest = hashlib.sha256()
        digest.update(np.ascontiguousarray(self.gram).tobytes())
        digest.update(np.ascontiguousarray(self.candidate_weights).tobytes())
        digest.update(json.dumps({k:self.metadata[k] for k in
            ('sample_rate','samples','noise_variance','precision','observed_rows')}, sort_keys=True).encode())
        return digest.hexdigest()


class StreamingBankStatistics:
    """Bounded streaming cross-Gram for both bits and a fixed public bank.

    push(first,basis[2,C,N],signals[S,N],observations[W,N],valid[N]) consumes
    contiguous rows. signals are evaluator/channel references; they are stored
    only as separate Gram columns for noise simulation and labeled oracle scores.
    They do not alter the practical bank or candidate prior.

    Bulk products/partial sums use explicit complex64 or complex128, with
    complex128 accumulation between reduction tiles. FP32 never permits TF32.
    GPU transfer and synchronized compute times are reported separately; the
    complete push timer includes validation, conversion, transfer and reduction.
    """
    def __init__(self, *, sample_rate, samples, candidates, noise_variance,
                 signal_count=2, observed_streams=0, candidate_weights=None,
                 precision='fp32', device='cpu', reduction_samples=1024,
                 maximum_chunk_samples=16384, memory_limit_bytes=256*1024**2):
        began = time.perf_counter()
        self.sample_rate = float(sample_rate)
        self.variance = float(noise_variance)
        if not math.isfinite(self.sample_rate) or self.sample_rate <= 0 or not math.isfinite(self.variance) or self.variance <= 0:
            raise ValueError('positive finite sample rate and known noise variance required')
        self.samples = _integer(samples, 'samples')
        self.candidates = _integer(candidates, 'candidates', 1, 4096)
        self.signal_count = _integer(signal_count, 'signal count', 1, 16)
        self.observed_streams = _integer(observed_streams, 'observed streams', 0, 128)
        self.maximum_chunk_samples = _integer(maximum_chunk_samples, 'chunk limit', 1, 1_048_576)
        self.reduction_samples = _integer(reduction_samples, 'reduction tile', 1, self.maximum_chunk_samples)
        self.memory_limit_bytes = _integer(memory_limit_bytes, 'memory limit')
        if precision not in ('fp32','fp64'):
            raise ValueError('bulk precision must be fp32 or fp64')
        self.precision = precision
        self.numpy_dtype = np.complex64 if precision == 'fp32' else np.complex128
        self.torch_dtype = torch.complex64 if precision == 'fp32' else torch.complex128
        self.device = torch.device(device)
        if self.device.type not in ('cpu','cuda'):
            raise ValueError('CPU or CUDA backend required')
        if self.device.type == 'cuda':
            if not torch.cuda.is_available():
                raise RuntimeError('CUDA requested but unavailable')
            if precision == 'fp32' and (torch.backends.cuda.matmul.allow_tf32 or torch.get_float32_matmul_precision() != 'highest'):
                raise ValueError('FP32 minimum requires TF32 disabled and highest matmul precision')
        self.weights = _weights(candidate_weights, self.candidates)
        self.columns = 2*self.candidates+self.signal_count
        item = np.dtype(self.numpy_dtype).itemsize
        self.owned_bytes_bound = (self.columns*self.columns*16*8+
            self.maximum_chunk_samples*(self.columns+self.observed_streams)*item*4+
            self.observed_streams*self.columns*16*4+self.maximum_chunk_samples*16)
        if self.owned_bytes_bound > self.memory_limit_bytes:
            raise MemoryError('cross-Gram/input/transfer workspace exceeds declared budget')
        self._gram = torch.zeros((self.columns,self.columns),dtype=torch.complex128,device=self.device)
        self._r = torch.zeros((self.observed_streams,self.columns),dtype=torch.complex128,device=self.device)
        self.next_sample = self.observed_rows = self.erased_rows = self.chunks = self.reduction_tiles = 0
        self.push_cpu_seconds = self.push_wall_seconds = self.transfer_wall_seconds = self.compute_wall_seconds = 0.
        self.bytes_transferred = 0
        self._failed = self._finished = False
        self.preparation_wall_seconds = time.perf_counter()-began

    def _sync(self):
        if self.device.type == 'cuda':
            torch.cuda.synchronize(self.device)

    def push(self, first, basis, signals, observations=None, valid=None):
        if self._failed or self._finished:
            raise RuntimeError('cannot push after failure or completed finish')
        first = _integer(first,'first sample',0)
        if first != self.next_sample:
            raise ValueError('sample stream must be contiguous from zero')
        basis = np.asarray(basis)
        if basis.ndim != 3 or basis.shape[:2] != (2,self.candidates):
            raise ValueError('basis must have [2,candidates,samples] shape')
        count = basis.shape[-1]
        if not 1 <= count <= self.maximum_chunk_samples or first+count > self.samples:
            raise ValueError('chunk exceeds declared sample/calendar bounds')
        signals = np.asarray(signals)
        if signals.shape != (self.signal_count,count):
            raise ValueError('signals must have [signal_count,samples] shape')
        if self.observed_streams:
            observations = np.asarray(observations)
            if observations.shape != (self.observed_streams,count):
                raise ValueError('observations must have [observed_streams,samples] shape')
        elif observations is not None:
            raise ValueError('observations supplied without declared observed_streams')
        if valid is None:
            mask = np.ones(count,dtype=bool)
        else:
            valid = np.asarray(valid)
            if valid.shape != (count,) or not np.isin(valid,(0,1,False,True)).all():
                raise ValueError('validity must be a matching binary vector')
            mask = valid.astype(bool)
        started,cpu = time.perf_counter(),time.process_time()
        try:
            # Validate/cast once per bounded chunk. No observation-sized array
            # survives this call; partial products reduce into fixed matrices.
            columns = np.concatenate((basis.reshape(2*self.candidates,count),signals)).astype(self.numpy_dtype)
            if not np.isfinite(columns).all() or (self.observed_streams and not np.isfinite(observations).all()):
                raise ValueError('nonfinite or overflowing source/observation samples')
            if self.observed_streams:
                observations = np.asarray(observations,dtype=self.numpy_dtype)
                if not np.isfinite(observations).all():
                    raise ValueError('observation overflows selected bulk precision')
            for lo in range(0,count,self.reduction_samples):
                hi = min(count,lo+self.reduction_samples)
                keep = mask[lo:hi]
                if not keep.any():
                    continue
                transfer = time.perf_counter()
                host = np.ascontiguousarray(columns[:,lo:hi][:,keep])
                x = torch.as_tensor(host,dtype=self.torch_dtype,device=self.device)
                y = (torch.as_tensor(np.ascontiguousarray(observations[:,lo:hi][:,keep]),dtype=self.torch_dtype,device=self.device)
                     if self.observed_streams else None)
                self._sync()
                self.transfer_wall_seconds += time.perf_counter()-transfer
                if self.device.type == 'cuda':
                    self.bytes_transferred += host.nbytes+(0 if y is None else y.numel()*y.element_size())
                compute = time.perf_counter()
                self._gram += (x.conj()@x.T).to(torch.complex128)/self.variance
                if y is not None:
                    self._r += (y@x.conj().T).to(torch.complex128)/self.variance
                self._sync()
                self.compute_wall_seconds += time.perf_counter()-compute
                self.reduction_tiles += 1
            self.next_sample += count
            self.observed_rows += int(mask.sum())
            self.erased_rows += count-int(mask.sum())
            self.chunks += 1
        except BaseException:
            self._failed = True
            raise
        finally:
            self.push_wall_seconds += time.perf_counter()-started
            self.push_cpu_seconds += time.process_time()-cpu

    def finish(self):
        if self._failed or self.next_sample != self.samples:
            raise RuntimeError('failed or incomplete observation cannot produce a qualified snapshot')
        if self._finished:
            raise RuntimeError('finish already consumed this observation')
        began = time.perf_counter()
        self._sync()
        gram = self._gram.detach().cpu().numpy().copy()
        r = self._r.detach().cpu().numpy().copy()
        if not np.isfinite(gram).all() or not np.isfinite(r).all():
            raise FloatingPointError('accumulated statistics are nonfinite')
        hermitian_error = float(np.max(np.abs(gram-gram.conj().T)))
        gram = (gram+gram.conj().T)*.5
        if (gram.diagonal().real < 0).any():
            raise FloatingPointError('accumulated template energy became negative')
        self._finished = True
        gram.flags.writeable = False
        metadata = dict(schema='triad-multiday-static-bank-v1',sample_rate=self.sample_rate,
            samples=self.samples,simulated_seconds=self.samples/self.sample_rate,noise_variance=self.variance,
            candidates=self.candidates,signal_count=self.signal_count,observed_streams=self.observed_streams,
            observed_rows=self.observed_rows,erased_rows=self.erased_rows,chunks=self.chunks,
            reduction_tiles=self.reduction_tiles,reduction_samples=self.reduction_samples,
            maximum_chunk_samples=self.maximum_chunk_samples,precision=self.precision,device=str(self.device),
            bulk_dtype=str(self.torch_dtype),partial_sum_dtype=str(self.torch_dtype),outer_sum_dtype='torch.complex128',
            tf32_allowed=False,memory_limit_bytes=self.memory_limit_bytes,working_bytes_bound=self.owned_bytes_bound,
            preparation_wall_seconds=self.preparation_wall_seconds,push_wall_seconds=self.push_wall_seconds,
            push_cpu_seconds=self.push_cpu_seconds,transfer_wall_seconds=self.transfer_wall_seconds,
            compute_wall_seconds=self.compute_wall_seconds,bytes_transferred=self.bytes_transferred,
            finalization_wall_seconds=time.perf_counter()-began,gram_hermitian_error=hermitian_error,
            likelihood_scope='Persistent proper-complex Gaussian scalar channel with fixed public candidates; conditional known white noise; no adaptive tracking or physical-noise qualification',
            mc_scope='Joint whole-observation Gram supports static likelihood MC only; finite-coherence/phase paths need blockwise statistics',
            timing_scope='Complete push includes validation/conversion/transfers/products/reduction; caller waveform generation, IO and MC are separate')
        result = BankStatistics(gram,self.candidates,self.signal_count,self.weights.copy(),r,metadata)
        result.prepare_covariance()
        metadata['finalization_wall_seconds'] = time.perf_counter()-began
        return result
