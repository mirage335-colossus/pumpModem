#!/usr/bin/env python3
"""Conditional achievable-model bounds for a fixed Gaussian likelihood bank.

Only public block Grams, calendars and candidate descriptors are read. The
receiver in this calculation maximizes individual proper Gaussian model LRs;
it is not the coherence/model mixture used by triad-state-study.py. The bounds
are mathematical inequalities for the declared Gaussian process, evaluated in
FP64, rather than a certified floating-point enclosure or physical-channel
guarantee. Full exported observations are evaluated; no prefix is declared a
complete transmitted pattern and no duration extrapolation is performed.
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
import torch

from triad_gaussian_state import GaussianPrior, GaussianStateFilter
from triad_state_workspace import BlockWorkspace

DEFAULT_CHERNOFF_S = (.001, .003, .01, .03, .1, .3, 1., 3., 10., 30., 100., 300., 1000.)


class _Sum:
    def __init__(self):
        self.value, self.error = 0., 0.

    def add(self, value):
        adjusted = value-self.error
        following = self.value+adjusted
        self.error = (following-self.value)-adjusted
        self.value = following
        if not math.isfinite(self.value):
            raise ValueError("Gaussian design moment overflow")


def _positive_real(value, name):
    value = complex(value)
    scale = max(1., abs(value.real))
    if not math.isfinite(value.real) or not math.isfinite(value.imag) or abs(value.imag) > 1e-9*scale or value.real < -1e-9*scale:
        raise ValueError(f"invalid nonnegative real {name}")
    return max(0., value.real)


class GaussianDesignMoments:
    """Streaming exact-model tr(C), logdet(I+C), and tr(C^2).

    C is the covariance of the normalized raw signal samples, with stationary
    tap covariance P and common scalar OU transition. The posterior covariance
    used for innovation determinants is distinct from stationary P in traces.
    """

    def __init__(self, prior: GaussianPrior, *, chernoff_s=()):
        self.prior = prior
        self.stationary = torch.diag(torch.tensor(prior.stationary_power, dtype=torch.complex128))
        self.past_gram = torch.zeros_like(self.stationary)
        self.filter = GaussianStateFilter(prior, check_inputs=False)
        self.chernoff_s = tuple(float(value) for value in chernoff_s)
        if any(not math.isfinite(value) or not 0 < value <= 1e6 for value in self.chernoff_s) or len(set(self.chernoff_s)) != len(self.chernoff_s) or len(self.chernoff_s) > 64:
            raise ValueError("Chernoff parameters require at most64 unique positive finite values <=1e6")
        self.scaled_filter = GaussianStateFilter(prior, check_inputs=False) if self.chernoff_s else None
        self.scales = torch.tensor(self.chernoff_s, dtype=torch.float64)[:, None, None]
        self.trace = _Sum()
        self.variance = _Sum()
        self.blocks = 0
        self.maximum_relative_gram_repair = 0.

    def update(self, gram, dt_seconds):
        if not isinstance(gram, torch.Tensor) or gram.requires_grad or gram.device.type != "cpu":
            raise ValueError("design Grams require CPU inference tensors without gradients")
        if gram.shape != self.stationary.shape:
            raise ValueError("design Gram extent differs from prior")
        dt = float(dt_seconds)
        if not math.isfinite(dt) or dt < 0:
            raise ValueError("design elapsed time must be finite and nonnegative")
        gram = gram.to(torch.complex128)
        if not bool(torch.isfinite(gram).all()):
            raise ValueError("nonfinite design Gram")
        scale = max(1., float(gram.abs().max()))
        if float((gram-gram.mH).abs().max()) > 1e-10*scale:
            raise ValueError("design Gram is not Hermitian")
        gram = (gram+gram.mH)*.5
        values, vectors = torch.linalg.eigh(gram)
        if float(values[0]) < -1e-10*scale:
            raise ValueError("design Gram is not positive semidefinite")
        self.maximum_relative_gram_repair = max(self.maximum_relative_gram_repair, max(0., -float(values[0]))/scale)
        if bool((values < 0).any()):
            gram = (vectors*values.clamp_min(0)[None, :])@vectors.mH
        decay_squared = 1. if math.isinf(self.prior.coherence_seconds) else math.exp(-2*dt/self.prior.coherence_seconds)
        self.past_gram *= decay_squared
        gp = gram@self.stationary
        self.trace.add(_positive_real(torch.trace(gp), "signal covariance trace"))
        diagonal = torch.trace(gp@gp)
        cross = 2*torch.trace(gp@self.past_gram@self.stationary)
        self.variance.add(_positive_real(diagonal+cross, "log-LR variance"))
        self.past_gram += gram
        self.filter.update(torch.zeros(len(self.prior.stationary_power), dtype=torch.complex128), gram, dt)
        if self.scaled_filter is not None:
            self.scaled_filter.update(torch.zeros((len(self.chernoff_s), len(self.prior.stationary_power)), dtype=torch.complex128),
                                      self.scales*gram[None], dt)
        self.blocks += 1

    def result(self):
        logdet = -float(self.filter.finish())
        mean = self.trace.value-logdet
        if mean < -1e-9*max(1., self.trace.value, logdet):
            raise ValueError("negative Gaussian KL divergence beyond roundoff")
        scaled_logdet = (-self.scaled_filter.finish()).tolist() if self.scaled_filter is not None else []
        return {"trace_signal_covariance": self.trace.value,
                "logdet_noise_plus_signal": logdet,
                "h1_log_lr_mean": max(0., mean),
                "h1_log_lr_variance": self.variance.value,
                "chernoff_s": list(self.chernoff_s),
                "scaled_logdet_noise_plus_signal": scaled_logdet,
                "blocks": self.blocks,
                "maximum_relative_gram_psd_repair": self.maximum_relative_gram_repair}


def cantelli_miss_upper(mean, variance, threshold):
    if not all(math.isfinite(value) for value in (mean, variance, threshold)) or variance < 0:
        raise ValueError("invalid Cantelli inputs")
    if mean <= threshold:
        return 1.
    margin = mean-threshold
    if not variance:
        return 0.
    deviation = math.sqrt(variance)
    if math.isfinite(margin):
        ratio = deviation/math.hypot(deviation, margin)
    else:
        scale = max(abs(mean), abs(threshold), deviation)
        ratio = (deviation/scale)/math.hypot(deviation/scale, mean/scale-threshold/scale)
    return min(1., math.nextafter(ratio*ratio, math.inf))


def chernoff_miss_upper(logdet, scales, scaled_logdets, threshold):
    """Lower tail of L=sum(lambda_i Exp(1))-logdet(I+C), under H1.

    Every s>0 gives a bound. Taking their minimum uses fixed public design
    calculations, not selection among observed receiver outcomes.
    """
    if len(scales) != len(scaled_logdets) or not all(math.isfinite(value) for value in (logdet, threshold)):
        raise ValueError("invalid Chernoff determinant data")
    if not scales:
        return 1., None, 0.
    candidates = []
    for scale, scaled_logdet in zip(scales, scaled_logdets):
        if not math.isfinite(scale) or scale <= 0 or not math.isfinite(scaled_logdet):
            raise ValueError("invalid Chernoff scale or determinant")
        candidates.append((min(0., scale*(threshold+logdet)-scaled_logdet), scale))
    selected_log, selected_s = min(candidates)
    return _union_upper([selected_log]), selected_s, selected_log


def gaussian_null_calibration(moments, markov_threshold):
    """A fixed-design common maximum-LR gate from Gaussian H0 moments.

    For 0<t<=1, log E0 exp(tL)=(1-t)f(1)-f(1-t), f(s)=logdet(I+sC).
    Each candidate may select its strongest public-design bound. The common
    gate is the largest resulting candidate gate, preserving every allocated
    null probability. t=1 is always included and reproduces the Markov gate.
    """
    if not moments or not math.isfinite(markov_threshold) or markov_threshold <= 0:
        raise ValueError("invalid Gaussian null calibration family")
    candidates = []
    for index, moment in enumerate(moments):
        base = moment["logdet_noise_plus_signal"]
        scales = moment.get("chernoff_s", [])
        determinants = moment.get("scaled_logdet_noise_plus_signal", [])
        if not math.isfinite(base) or len(scales) != len(determinants):
            raise ValueError("invalid null fractional-moment determinant data")
        best_threshold, best_t, best_log_moment = markov_threshold, 1., 0.
        evaluated = []
        for scale, determinant in zip(scales, determinants):
            if not math.isfinite(scale) or not math.isfinite(determinant):
                raise ValueError("nonfinite null fractional-moment data")
            if not 0 < scale < 1:
                continue
            t = 1-scale
            log_moment = math.fsum((scale*base, -determinant))
            if log_moment > 1e-9*max(1., abs(base), abs(determinant)):
                raise ValueError("fractional Gaussian moment violates concavity beyond roundoff")
            log_moment = min(0., log_moment)
            threshold = math.nextafter((markov_threshold+log_moment)/t, math.inf)
            evaluated.append({"t": t, "log_null_fractional_moment": log_moment,
                              "candidate_gate": threshold})
            if threshold < best_threshold:
                best_threshold, best_t, best_log_moment = threshold, t, log_moment
        candidates.append({"candidate": index, "candidate_gate": best_threshold,
                           "selected_t": best_t, "selected_log_null_fractional_moment": best_log_moment,
                           "evaluated_fractional_moments": evaluated})
    common = min(markov_threshold, max(row["candidate_gate"] for row in candidates))
    for row in candidates:
        row["log_null_tail_upper_at_common_gate"] = min(0., row["selected_log_null_fractional_moment"]-row["selected_t"]*common)
    return {"common_threshold": common, "markov_threshold": markov_threshold,
            "reduction_from_markov": markov_threshold-common,
            "candidates": candidates,
            "scope": "fixed-design Gaussian null moment bound for maximum individual LRs; not empirical calibration or a mixture-receiver gate"}


def log_affinity(logdet_left, logdet_right, logdet_average):
    value = .5*logdet_left+.5*logdet_right-logdet_average
    if not math.isfinite(value) or value > 1e-9*max(1., abs(logdet_average)):
        raise ValueError("Gaussian affinity exceeds one beyond roundoff")
    return min(0., value)


def _union_upper(log_probabilities):
    if not log_probabilities:
        return 0.
    largest = max(log_probabilities)
    total_log = largest+math.log(math.fsum(math.exp(value-largest) for value in log_probabilities))
    if total_log >= 0:
        return 1.
    # Do not turn a positive, extremely small probability into an exact zero.
    value = math.exp(max(total_log, math.log(math.nextafter(0., math.inf))))
    return min(1., math.nextafter(value, math.inf))


def reception_bounds(moments, pair_log_affinities, identities, threshold):
    """Bounds for individual-LR maximization, conditional on each true model."""
    results = []
    for index, moment in enumerate(moments):
        cantelli = cantelli_miss_upper(moment["h1_log_lr_mean"], moment["h1_log_lr_variance"], threshold)
        chernoff, selected_s, log_chernoff = chernoff_miss_upper(
            moment.get("logdet_noise_plus_signal", 0.), moment.get("chernoff_s", []),
            moment.get("scaled_logdet_noise_plus_signal", []), threshold)
        miss = min(cantelli, chernoff)
        wrong_bit, wrong_event = [], []
        for other in range(len(moments)):
            if other == index:
                continue
            pair = tuple(sorted((index, other)))
            if identities[index][0] != identities[other][0]:
                wrong_bit.append(pair_log_affinities[pair])
            if identities[index] != identities[other]:
                wrong_event.append(pair_log_affinities[pair])
        bit_upper, event_upper = _union_upper(wrong_bit), _union_upper(wrong_event)
        lower = lambda upper: max(0., math.nextafter(1-miss-upper, -math.inf))
        results.append({"candidate": index, **moment, "miss_cantelli_upper": cantelli,
                        "miss_chernoff_upper": chernoff, "miss_combined_upper": miss,
                        "selected_chernoff_s": selected_s, "selected_chernoff_log_bound": log_chernoff,
                        "wrong_bit_union_upper": bit_upper, "wrong_bit_or_epoch_union_upper": event_upper,
                        "correct_bit_admission_lower": lower(bit_upper),
                        "correct_bit_epoch_admission_lower": lower(event_upper)})
    return results


def evaluate_workspace(workspace, *, cn0_db_hz=-24., coherence_seconds=128., taps=2,
                       stationary_power=None, false_probability=1e-4, horizon_seconds=86400.,
                       attempt_spacing=60., duration_alternatives=1, model_alternatives=1,
                       target_correct=.99, maximum_candidates=16, chernoff_s=DEFAULT_CHERNOFF_S,
                       null_calibration="markov"):
    if not math.isfinite(cn0_db_hz) or not -300 < cn0_db_hz/10 < 300:
        raise ValueError("C/N0 is outside the representable range")
    if not 0 < false_probability < 1 or not 0 < target_correct < 1:
        raise ValueError("probability targets must lie in (0,1)")
    if not all(math.isfinite(v) and v > 0 for v in (horizon_seconds, attempt_spacing)):
        raise ValueError("invalid monitoring calendar")
    if not all(isinstance(v, int) and not isinstance(v, bool) and v > 0 for v in (duration_alternatives, model_alternatives, maximum_candidates)):
        raise ValueError("alternative/resource counts must be positive integers")
    if null_calibration not in ("markov", "gaussian-chernoff"):
        raise ValueError("null calibration must be markov or gaussian-chernoff")
    selected = [i for i, row in enumerate(workspace.column_mapping) if len(row) == taps]
    if not selected or len(selected) > maximum_candidates:
        raise ValueError("fixed candidate bank is empty or exceeds its explicit bound; no candidates were pruned")
    if any(not isinstance(workspace.candidates[index].get("epoch_offset_seconds"), int) for index in selected):
        raise ValueError("public epoch offsets are required for the bit/epoch reception bound")
    identities = [(workspace.candidates[index]["bit"], workspace.candidates[index]["epoch_offset_seconds"]) for index in selected]
    if {identity[0] for identity in identities} != {0, 1}:
        raise ValueError("the fixed bank must retain both bit hypotheses")
    powers = tuple([1/taps]*taps) if stationary_power is None else tuple(stationary_power)
    prior = GaussianPrior(powers, coherence_seconds)
    if len(powers) != taps:
        raise ValueError("coefficient powers must match selected taps")
    maps = [workspace.column_mapping[index] for index in selected]
    candidate_states = [GaussianDesignMoments(prior, chernoff_s=chernoff_s) for _ in selected]
    pairs = [(i, j) for i in range(len(selected)) for j in range(i+1, len(selected)) if identities[i] != identities[j]]
    augmented_prior = GaussianPrior(tuple(value*.5 for value in powers)*2, coherence_seconds)
    pair_states = {pair: GaussianDesignMoments(augmented_prior) for pair in pairs}
    attempts_float = horizon_seconds/attempt_spacing
    if not math.isfinite(attempts_float):
        raise ValueError("monitoring attempt count overflow")
    attempts = max(1, math.ceil(attempts_float))
    family = len(selected)*attempts*duration_alternatives*model_alternatives
    markov_threshold = math.log(family)-math.log(false_probability)
    variance = workspace.sample_rate/10**(cn0_db_hz/10)
    if not math.isfinite(variance) or variance <= 0:
        raise ValueError("invalid complex sample noise variance")
    previous_center = None
    observed_blocks = valid_rows = 0
    for tile in workspace.iter_public():
        for raw, interval, valid in zip(tile.gram, tile.intervals, tile.valid_rows):
            center = (int(interval[0])+int(interval[1]))/(2*workspace.sample_rate)
            elapsed = 0. if previous_center is None else center-previous_center
            previous_center = center
            full = torch.from_numpy(np.array(raw, dtype=np.complex128, copy=True))/variance
            for mapping, state in zip(maps, candidate_states):
                idx = torch.tensor(mapping, dtype=torch.int64)
                state.update(full[idx[:, None], idx[None, :]], elapsed)
            for (i, j), state in pair_states.items():
                idx = torch.tensor(maps[i]+maps[j], dtype=torch.int64)
                state.update(full[idx[:, None], idx[None, :]], elapsed)
            observed_blocks += 1
            valid_rows += int(valid)
    if observed_blocks != workspace.block_count:
        raise ValueError("public observation calendar was not completely processed")
    moments = [state.result() for state in candidate_states]
    null_gate = gaussian_null_calibration(moments, markov_threshold)
    threshold = markov_threshold if null_calibration == "markov" else null_gate["common_threshold"]
    affinities = {pair: log_affinity(moments[pair[0]]["logdet_noise_plus_signal"],
                                     moments[pair[1]]["logdet_noise_plus_signal"],
                                     state.result()["logdet_noise_plus_signal"])
                  for pair, state in pair_states.items()}
    conditional = reception_bounds(moments, affinities, identities, threshold)
    minimum_bit = min(row["correct_bit_admission_lower"] for row in conditional)
    minimum_event = min(row["correct_bit_epoch_admission_lower"] for row in conditional)
    return {"schema": "triad-gaussian-fixed-bank-bounds-v1", "cn0_db_hz": cn0_db_hz,
            "complex_sample_noise_variance": variance, "stationary_coefficient_powers": list(powers),
            "coherence_seconds": None if math.isinf(coherence_seconds) else coherence_seconds,
            "static_coefficients": math.isinf(coherence_seconds),
            "selected_candidate_indices": selected, "selected_candidates": [workspace.candidates[i] for i in selected],
            "candidate_count": len(selected), "pair_designs": len(pairs),
            "threshold": threshold, "false_probability_per_horizon": false_probability,
            "null_calibration": null_calibration, "markov_threshold": markov_threshold,
            "gaussian_chernoff_threshold": null_gate["common_threshold"],
            "gaussian_chernoff_gate_reduction": null_gate["reduction_from_markov"],
            "null_gate_diagnostics": null_gate,
            "monitoring_horizon_seconds": horizon_seconds, "scheduled_attempts": attempts,
            "attempt_spacing_seconds": attempt_spacing, "duration_alternatives": duration_alternatives,
            "model_alternatives": model_alternatives, "total_family_tests": family,
            "target_correct_probability": target_correct,
            "minimum_correct_bit_admission_lower": minimum_bit,
            "minimum_correct_bit_epoch_admission_lower": minimum_event,
            "conditional_model_target_met": minimum_event >= target_correct,
            "bound_below_target_is_not_an_impossibility_result": True,
            "conditional_candidates": conditional,
            "miss_bound_method": "minimum of Cantelli and predeclared scaled-covariance Chernoff bounds",
            "chernoff_s_grid": list(chernoff_s),
            "pair_log_affinities": [{"candidate_indices": list(pair), "log_affinity_upper": value} for pair, value in affinities.items()],
            "observation_blocks": observed_blocks, "observed_valid_rows": valid_rows,
            "full_exported_capture_seconds": workspace.sample_count/workspace.sample_rate,
            "duration_scope": "entire exported calendar only; no prefix completion, minimum-duration recommendation or extrapolation",
            "receiver_scope": "maximum individual Gaussian LRs; same-bit/same-epoch frequency/timing alternatives count as the same reception event; not a mixture receiver bound",
            "operational_scope": "correct-reception bound is conditional on this one fixed-bank full-window decision; other attempts/durations/models receive a noise-only family allocation, not a joint signal-present selection guarantee",
            "assumptions": ["known independent circular white sample noise", "fixed pre-observation candidate designs and stationary proper complex Gaussian tap priors", "common scalar OU coherence for candidate and affinity models", "fixed observation/erasure calendar independent of noise", "declared family covers all attempted durations, models and starts"],
            "numerical_scope": "FP64 evaluated analytic inequalities with roundoff-sized PSD repair; no directed-rounding enclosure or physical-channel certificate",
            "hidden_channel_means_or_signal_energies_read": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workspace", type=Path)
    parser.add_argument("--cn0", type=float, default=-24.)
    parser.add_argument("--coherence", type=float, default=128.)
    parser.add_argument("--taps", type=int, choices=(1, 2), default=2)
    parser.add_argument("--stationary-powers", type=lambda value: tuple(float(x) for x in value.split(",")))
    parser.add_argument("--correct", type=float, default=.99)
    parser.add_argument("--false", dest="false_probability", type=float, default=1e-4)
    parser.add_argument("--horizon", type=float, default=86400.)
    parser.add_argument("--attempt-spacing", type=float, default=60.)
    parser.add_argument("--duration-alternatives", type=int, default=1)
    parser.add_argument("--model-alternatives", type=int, default=1)
    parser.add_argument("--maximum-candidates", type=int, default=16)
    parser.add_argument("--chernoff-s", type=lambda value: tuple(float(x) for x in value.split(",")), default=DEFAULT_CHERNOFF_S)
    parser.add_argument("--no-chernoff", action="store_true")
    parser.add_argument("--null-calibration", choices=("markov", "gaussian-chernoff"), default="markov")
    parser.add_argument("--memory-mib", type=int, default=256)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        torch.set_num_threads(2)
        workspace = BlockWorkspace(args.workspace, memory_limit_bytes=args.memory_mib*1024*1024)
        started = time.perf_counter()
        result = evaluate_workspace(workspace, cn0_db_hz=args.cn0, coherence_seconds=args.coherence,
            taps=args.taps, stationary_power=args.stationary_powers, false_probability=args.false_probability,
            horizon_seconds=args.horizon, attempt_spacing=args.attempt_spacing,
            duration_alternatives=args.duration_alternatives, model_alternatives=args.model_alternatives,
            target_correct=args.correct, maximum_candidates=args.maximum_candidates,
            chernoff_s=() if args.no_chernoff else args.chernoff_s,
            null_calibration=args.null_calibration)
        result.update(wall_seconds=time.perf_counter()-started, torch_version=torch.__version__,
                      command_argv=sys.argv, source_manifest_sha256=hashlib.sha256(workspace.manifest_path.read_bytes()).hexdigest(),
                      script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                      gaussian_state_sha256=hashlib.sha256(Path(__file__).with_name("triad_gaussian_state.py").read_bytes()).hexdigest(),
                      workspace_module_sha256=hashlib.sha256(Path(__file__).with_name("triad_state_workspace.py").read_bytes()).hexdigest())
        encoded = json.dumps(result, indent=2, allow_nan=False)+"\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True); args.output.write_text(encoded)
        else:
            sys.stdout.write(encoded)
        return 0
    except (ValueError, RuntimeError, OSError, OverflowError) as error:
        print(f"triad-gaussian-bounds: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
