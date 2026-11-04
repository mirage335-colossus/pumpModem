#!/usr/bin/env python3
"""Physical-input adapter for the maintained three-carrier FFT receiver.

This module reuses its acquisition and likelihood arithmetic, not its Gaussian
false-admission claim.  A frozen rank gate has a finite-sample null guarantee
only when its training and test null scores are exchangeable.  Device drift,
overlapping captures, changed preprocessing and signal/key mismatch are not
covered by that assumption.  No physical capture is represented as a native
Gaussian simulation export.
"""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import threading

import numpy as np
from scipy.stats import beta

from triad_fft_acquire import FFTWorkspace, score_workspace
from triad_group_acquire import AcquisitionConfig


def _finite_scalar(value, name, *, positive=False):
    if isinstance(value, (bool, np.bool_)):
        raise ValueError(f"{name} must be a finite number")
    try:
        result = float(value)
    except (TypeError, ValueError) as error:
        raise ValueError(f"{name} must be a finite number") from error
    if not math.isfinite(result) or (positive and result <= 0):
        raise ValueError(f"{name} must be finite" + (" and positive" if positive else ""))
    return result


def _complex_array(value, shape, name):
    value = np.asarray(value)
    if value.shape != shape or value.dtype.kind != "c" or not np.isfinite(value).all():
        raise ValueError(f"{name} must be a finite complex array with shape {shape}")
    return np.array(value, dtype=np.complex128, order="C", copy=True)


class _PhysicalSamples:
    """Owned public arrays; deliberately not a SampledWorkspace manifest."""
    schema = "triad-physical-receiver-input-v1"

    def __init__(self, basis, sample_rate, valid, key, frequencies):
        self.samples = basis.shape[1]
        self.sample_rate = sample_rate
        self.selected = [0, 1]
        self.key = key
        self.frequencies = frequencies
        self._arrays = {"basis.c128": basis, "validity.u8": valid,
                        "observation.c128": np.zeros(self.samples, np.complex128)}
        for array in self._arrays.values():
            array.flags.writeable = False
        self.input_bytes = sum(array.nbytes for array in self._arrays.values())

    def array(self, name):
        return self._arrays[name]

    def verify_unchanged(self):
        # Arrays are owned, read-only copies; no mutable caller mapping or file
        # backing is retained. Observations are replaced in the FFT workspace.
        if any(array.flags.writeable for array in self._arrays.values()):
            raise ValueError("physical public arrays were made mutable")


class EmpiricalReceiver:
    """Reusable complete-bank physical receiver with no analytic admission.

    ``basis`` is [two bits, samples, two complex channel columns].  The caller
    must supply explicit finite tails and enough capture extent for every lag;
    the maintained FFT support guard remains active.  No template clipping,
    padding, noise whitening, waveform normalization or candidate pruning is
    performed here. ``reference_variance`` and the channel prior must be frozen
    independently of payload outcomes. Calls are serialized on this instance.
    """
    def __init__(self, basis, sample_rate, lags, *, valid=None, key=None,
                 frequencies=None, reference_variance, stationary_power=(.5, .5),
                 frequency_offsets=(0.,), precision="fp32", device="cpu",
                 memory_limit_bytes=512 * 1024 * 1024,
                 preprocessing_identity="caller-unspecified"):
        source = np.asarray(basis)
        if source.ndim != 3 or source.shape[0] != 2 or source.shape[2] != 2 or source.shape[1] <= 4:
            raise ValueError("basis must have shape [2, N>4, 2]")
        basis = _complex_array(source, source.shape, "basis")
        sample_rate = _finite_scalar(sample_rate, "sample_rate", positive=True)
        n = basis.shape[1]
        if valid is None:
            valid = np.ones(n, dtype=np.uint8)
        valid = np.asarray(valid)
        if valid.shape != (n,) or valid.dtype.kind not in "bui" or np.any((valid != 0) & (valid != 1)):
            raise ValueError("valid must be a binary vector with N entries")
        valid = np.array(valid, dtype=np.uint8, copy=True)
        public_key = dict(arrival_seconds=0., frequency_hz=0., clock_ppm=0.,
                          epoch_offset_seconds=0, frequency_drift_hz_per_second=0.)
        if key is not None:
            if not isinstance(key, dict) or set(key) - set(public_key):
                raise ValueError("key contains unknown public synchronization fields")
            public_key.update(key)
        epoch = public_key["epoch_offset_seconds"]
        if isinstance(epoch, (bool, np.bool_)) or not isinstance(epoch, (int, np.integer)) or abs(int(epoch)) > 2**63 - 1:
            raise ValueError("epoch offset must be an exact signed integer")
        public_key["epoch_offset_seconds"] = int(epoch)
        for name in set(public_key) - {"epoch_offset_seconds"}:
            public_key[name] = _finite_scalar(public_key[name], name)
        if frequencies is None:
            frequencies = [public_key["frequency_hz"]] * 4
        frequencies = np.asarray(frequencies, dtype=np.float64)
        if frequencies.shape != (4,) or not np.isfinite(frequencies).all():
            raise ValueError("frequencies must contain four finite carrier-column values")
        frequencies = frequencies.copy()
        frequencies.flags.writeable = False
        if not isinstance(preprocessing_identity, str) or not preprocessing_identity:
            raise ValueError("preprocessing_identity must be a nonempty string")
        self._sampled = _PhysicalSamples(basis, sample_rate, valid, public_key, frequencies)
        self._config = AcquisitionConfig(
            doppler_width_hz=0., window_blocks=1, reblock=1,
            reference_variance=_finite_scalar(reference_variance, "reference_variance", positive=True),
            stationary_power=tuple(stationary_power), frequency_offsets_hz=tuple(frequency_offsets),
            precision=precision, device=device, memory_limit_bytes=memory_limit_bytes,
            group_tile=min(64, max(1, len(lags))))
        self._workspace = FFTWorkspace(self._sampled, lags, precision=precision,
                                       device=device, memory_limit_bytes=memory_limit_bytes)
        self._lock = threading.Lock()
        self.preprocessing_identity = preprocessing_identity
        # Bind the statistic, not one observation or a future ground-truth label.
        identity = hashlib.sha256()
        for array in (basis, valid, frequencies, self._workspace.lags):
            identity.update(str(array.shape).encode())
            identity.update(str(array.dtype).encode())
            identity.update(array.tobytes())
        configuration = dict(sample_rate=sample_rate, key=public_key,
                             receiver_config=self._config.__dict__,
                             preprocessing_identity=preprocessing_identity)
        identity.update(json.dumps(configuration, sort_keys=True, allow_nan=False).encode())
        for name in ("triad_live_receiver.py", "triad_fft_acquire.py", "triad_group_acquire.py",
                     "triad_gaussian_window.py", "triad_noise_likelihood.py", "triad_gaussian_state.py",
                     "triad_state_workspace.py"):
            identity.update(name.encode())
            identity.update((Path(__file__).parent / name).read_bytes())
        self.receiver_id = identity.hexdigest()

    @property
    def samples(self):
        return self._sampled.samples

    def score(self, observation, *, retain_group_scores=False):
        """Return the actual receiver's working statistic, without a decision."""
        observation = _complex_array(observation, (self.samples,), "observation")
        with self._lock:
            self._workspace.observation = observation
            self._workspace.energy = math.fsum(map(float, abs(observation[self._workspace.valid != 0])**2))
            raw = score_workspace(self._workspace, self._config,
                                  retain_group_scores=retain_group_scores)
        scores = raw["bit_log_likelihoods"]
        winner = int(np.argmax(scores))
        keep = ("bit_log_likelihoods", "group_bit_log_likelihoods",
                "best_scoring_group_for_winning_bit", "best_group_public_parameters",
                "best_group_scope", "geometry", "arithmetic", "numerical_refinement", "timing", "resources")
        result = {name: raw[name] for name in keep}
        result.update(format="triad-live-working-likelihood-v1", receiver_id=self.receiver_id,
                      bit=winner, score=float(scores[winner]),
                      opposite_bit_margin=float(scores[winner] - scores[1 - winner]),
                      preprocessing_identity=self.preprocessing_identity,
                      calibration=dict(
                          analytic_admission_validated=False,
                          scope="working Gaussian likelihood and residual normalizer only; physical noise law unqualified",
                          empirical_gate_required=True,
                          receiver_id_scope="public arrays, search, arithmetic, receiver source and declared preprocessing; excludes observation and truth",
                          normalizer_over_reference_range=raw["calibration"]["variance_upper_over_reference_range"],
                          observable_rank_histogram=raw["calibration"]["rank_histogram"]),
                      limits=[
                          "No native Gaussian simulation metadata or analytic admission threshold is used as a physical guarantee.",
                          "All declared timing and frequency alternatives remain in the likelihood mixture; epoch and clock are fixed.",
                          "A static two-column channel is not a general phase tracker or renewed fading model.",
                          "Public preprocessing, amplitude scale and noise condition must match frozen null calibration.",
                          "An empirical null gate does not establish correct-bit reception or wrong-key rejection."])
        return result

    def freeze_calibration(self, scores, *, alpha=.05):
        return freeze_calibration(scores, receiver_id=self.receiver_id, alpha=alpha)


def freeze_calibration(scores, *, receiver_id, alpha=.05):
    """Freeze an upper-tail rank test from independent null training scores.

    For a future null score T, p=(1+number(training >= T))/(n+1).  Ties are
    conservative. Under exchangeability, P(p <= alpha) <= alpha marginally over
    calibration and that future test; it is not a conditional future-rate bound
    for this frozen calibration, nor a continuous-monitoring family guarantee.
    """
    alpha = _finite_scalar(alpha, "alpha", positive=True)
    if alpha >= 1:
        raise ValueError("alpha must be below one")
    if not isinstance(receiver_id, str) or not receiver_id:
        raise ValueError("a nonempty receiver_id is required")
    values = np.asarray(scores, dtype=np.float64)
    if values.ndim != 1 or not len(values) or not np.isfinite(values).all():
        raise ValueError("calibration requires finite scalar null scores")
    if 1. / (len(values) + 1) > alpha:
        raise ValueError("too few null scores to resolve the requested alpha")
    return dict(format="triad-live-empirical-rank-gate-v1", receiver_id=receiver_id,
                alpha=alpha, null_count=len(values), sorted_null_scores=np.sort(values).tolist(),
                resolution=1. / (len(values) + 1),
                assumptions="null training and future null scores exchangeable; fixed receiver/preprocessing/noise condition; ties conservative",
                scope="per-capture marginal rank test, not operational daily false-admission certification or a conditional future-rate bound")


def empirical_decision(outcome, calibration):
    """Apply a frozen compatible gate; this function receives no true bit."""
    if calibration.get("format") != "triad-live-empirical-rank-gate-v1":
        raise ValueError("unsupported empirical gate")
    if outcome.get("receiver_id") != calibration.get("receiver_id"):
        raise ValueError("receiver identity differs from frozen calibration")
    score = _finite_scalar(outcome.get("score"), "score")
    bit = outcome.get("bit")
    if isinstance(bit, bool) or bit not in (0, 1):
        raise ValueError("outcome bit must be zero or one")
    scores = np.asarray(calibration.get("sorted_null_scores"), dtype=np.float64)
    rebuilt = freeze_calibration(scores, receiver_id=calibration["receiver_id"], alpha=calibration.get("alpha"))
    if calibration.get("null_count") != len(scores) or np.any(np.diff(scores) < 0):
        raise ValueError("inconsistent frozen calibration scores")
    p = (1 + int(np.count_nonzero(scores >= score))) / (len(scores) + 1)
    return dict(bit=int(bit), accepted=bool(p <= rebuilt["alpha"]), score=score,
                rank_p_value=p, alpha=rebuilt["alpha"], null_count=len(scores),
                opposite_bit_margin=_finite_scalar(outcome.get("opposite_bit_margin"), "opposite bit margin"),
                receiver_id=calibration["receiver_id"],
                admission_scope=calibration["scope"])


def evaluate(decision, truth):
    """Evaluation-only classification; None denotes a separately known null."""
    if truth is not None and (isinstance(truth, bool) or truth not in (0, 1)):
        raise ValueError("truth must be zero, one or None for a null")
    if truth is None:
        return "false_admission" if decision["accepted"] else "correct_rejection"
    if not decision["accepted"]:
        return "miss"
    return "correct_admission" if decision["bit"] == truth else "wrong_admission"


def binomial_interval(successes, trials, *, confidence=.95):
    """Two-sided exact Clopper-Pearson interval, for independent Bernoulli trials."""
    if (isinstance(successes, bool) or isinstance(trials, bool) or
            not isinstance(successes, (int, np.integer)) or not isinstance(trials, (int, np.integer)) or
            trials <= 0 or not 0 <= successes <= trials):
        raise ValueError("integer binomial counts require 0 <= successes <= trials and trials > 0")
    confidence = _finite_scalar(confidence, "confidence", positive=True)
    if confidence >= 1:
        raise ValueError("confidence must be below one")
    tail = (1 - confidence) / 2
    low = 0. if successes == 0 else float(beta.ppf(tail, successes, trials - successes + 1))
    high = 1. if successes == trials else float(beta.ppf(1 - tail, successes + 1, trials - successes))
    return dict(successes=int(successes), trials=int(trials), estimate=successes / trials,
                confidence=confidence, interval=[low, high],
                scope="pointwise exact binomial interval; independent Bernoulli trials assumed, not a simultaneous or sequential interval")
