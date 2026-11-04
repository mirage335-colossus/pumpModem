#!/usr/bin/env python3
"""Reject-only performance guard, separate from prediction qualification.

Only fresh direct full-target pairs may establish a regression. Screening and
engineering warnings are advisory. The statistical family is frozen before any
measurements and spends one workflow-wide false-rejection budget across jobs,
hypotheses and predeclared confirmation looks.
"""
from __future__ import annotations

from dataclasses import asdict, dataclass
from functools import lru_cache
import hashlib
import json
import math
import statistics
from typing import Iterable, Mapping

from scipy.stats import binom, t as student_t


SCHEMA = "triad-performance-reject-only-guard-v3"
LEGACY_SCHEMA = "triad-performance-reject-only-guard-v2"
BLOCK_PROTOCOL = "randomized-crossover-block-v1"
LEGACY_PROTOCOL = "legacy-pair-v2"
_PROFILES = {
    "ci": {"suite_alpha": .01, "threshold": 1.70, "screen_threshold": 1.50, "looks": (12,)},
    "gpu-dev": {"suite_alpha": .05, "threshold": 1.30, "screen_threshold": 1.15, "looks": (12, 20)},
}
LEGACY_ASSUMPTIONS = (
    "confirmation-pair signs are independent, with probability at most one half of exceeding the threshold under the median null",
    "screen observations are excluded from every confirmation observation",
    "the hypothesis family, workflow job count and confirmation looks are frozen before measurement",
    "each confirmation measures its complete declared target without extrapolation",
)
ASSUMPTIONS = (
    "each confirmation block uses a fair predeclared source-assignment coin and two opposite-order complete pairs in fresh execution contexts",
    "the conditional probability of a positive block contrast, given prior blocks and current source-independent slot conditions, is at most one half under the randomized-block null",
    "source assignment has no unaccounted carry-over into another arm; setup and full-case warmup preserve the declared target boundary and are separately charged",
    "screen observations are excluded from confirmation; the complete family, jobs, block schedule and rejection looks are frozen before measurement",
    "every block measures two complete declared targets per source, with no extrapolation or omitted work",
)
LEGACY_ESTIMAND = "median of paired full-target candidate/reference runtime ratios"
LEGACY_NULL = "P(candidate_seconds > threshold * reference_seconds) <= 0.5"
BLOCKING_ESTIMAND = "median randomized balanced-block ratio of two full-target candidate runtimes to two reference runtimes"
BLOCKING_NULL = "P(sum(candidate_seconds) > threshold * sum(reference_seconds) | prior history and source-independent slot conditions) <= 0.5"
BLOCKING_TEST = "exact one-sided binomial sign test"


@dataclass(frozen=True)
class GuardPolicy:
    profile: str = "ci"
    suite_alpha: float | None = None
    workflow_job_count: int = 1

    def __post_init__(self):
        if self.profile not in _PROFILES:
            raise ValueError("guard profile must be ci or gpu-dev")
        value = _PROFILES[self.profile]["suite_alpha"] if self.suite_alpha is None else self.suite_alpha
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not 0 < value <= .05:
            raise ValueError("suite_alpha must be finite and in (0, .05]")
        if (type(self.workflow_job_count) is not int or self.workflow_job_count < 1):
            raise ValueError("workflow_job_count must be a positive integer")
        object.__setattr__(self, "suite_alpha", float(value))

    @property
    def threshold(self) -> float:
        return _PROFILES[self.profile]["threshold"]

    @property
    def screen_threshold(self) -> float:
        return _PROFILES[self.profile]["screen_threshold"]

    @property
    def looks(self) -> tuple[int, ...]:
        return _PROFILES[self.profile]["looks"]


@dataclass(frozen=True)
class PairObservation:
    run_id: str
    reference_seconds: float
    candidate_seconds: float

    def __post_init__(self):
        if not isinstance(self.run_id, str) or not self.run_id.strip():
            raise ValueError("run_id must be a nonempty string")
        for name in ("reference_seconds", "candidate_seconds"):
            value = getattr(self, name)
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
                raise ValueError(f"{name} must be finite and positive")


@dataclass(frozen=True)
class BlockObservation:
    """One statistical unit, retaining its four complete execution arms.

    Contexts are in (reference, candidate) order for each pair. The runner
    checks their actual isolation receipts; these identifiers alone do not
    authenticate worker creation or physical independence.
    """
    run_id: str
    pairs: tuple[PairObservation, PairObservation]
    orders: tuple[tuple[str, str], tuple[str, str]]
    contexts: tuple[tuple[str, str], tuple[str, str]]

    def __post_init__(self):
        if not isinstance(self.run_id, str) or not self.run_id.strip():
            raise ValueError("block run_id must be a nonempty string")
        pairs = tuple(_pair(row) for row in self.pairs)
        orders = tuple(tuple(row) for row in self.orders)
        contexts = tuple(tuple(row) for row in self.contexts)
        if len(pairs) != 2 or len(orders) != 2 or len(contexts) != 2:
            raise ValueError("a confirmation block requires exactly two complete pairs")
        if any(len(row) != 2 or sorted(row) != ["candidate", "reference"] for row in orders) or orders[1] != orders[0][::-1]:
            raise ValueError("confirmation block orders must be opposite source assignments")
        ids = [value for row in contexts for value in row]
        if any(len(row) != 2 for row in contexts) or any(not isinstance(value, str) or not value.strip() for value in ids) or len(set(ids)) != 4:
            raise ValueError("all four block contexts must be distinct nonempty identities")
        if len({self.run_id, pairs[0].run_id, pairs[1].run_id}) != 3:
            raise ValueError("block and period identities must be distinct")
        object.__setattr__(self, "pairs", pairs)
        object.__setattr__(self, "orders", orders)
        object.__setattr__(self, "contexts", contexts)
        for value in (self.reference_seconds, self.candidate_seconds):
            if not math.isfinite(value) or value <= 0:
                raise ValueError("block summed time must be finite and positive")

    @property
    def reference_seconds(self):
        return math.fsum(row.reference_seconds for row in self.pairs)

    @property
    def candidate_seconds(self):
        return math.fsum(row.candidate_seconds for row in self.pairs)


def _digest(value: Mapping) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()


def _seal(value: dict) -> dict:
    # Nested block tuples must have identical types before and after JSON
    # retention, so source-bound receipt replay does not depend on a caller's
    # in-memory tuple/list representation.
    result = json.loads(json.dumps(value, allow_nan=False))
    return {**result, "record_sha256": _digest(result)}


def _verify(value: Mapping, *, schema: str) -> dict:
    if not isinstance(value, Mapping) or value.get("schema") != schema:
        raise ValueError("unsupported guard record schema")
    result = dict(value)
    claimed = result.pop("record_sha256", None)
    if claimed != _digest(result):
        raise ValueError("guard record identity mismatch")
    return result


def build_guard_plan(hypothesis_ids: Iterable[str], policy: GuardPolicy | None = None,
                     *, metadata: Mapping | None = None,
                     measurement_protocol: str = BLOCK_PROTOCOL) -> dict:
    policy = GuardPolicy() if policy is None else policy
    if not isinstance(policy, GuardPolicy):
        raise ValueError("policy must be GuardPolicy")
    if measurement_protocol not in (BLOCK_PROTOCOL, LEGACY_PROTOCOL):
        raise ValueError("unknown guard measurement protocol")
    legacy = measurement_protocol == LEGACY_PROTOCOL
    if isinstance(hypothesis_ids, (str, bytes)):
        raise ValueError("hypothesis_ids must be a complete ordered family")
    ids = list(hypothesis_ids)
    if (not ids or any(not isinstance(name, str) or not name.strip() for name in ids)
            or len(ids) != len(set(ids))):
        raise ValueError("hypothesis IDs must be unique nonempty strings")
    # JSON copying rejects nonfinite metadata and prevents later caller mutation.
    metadata = json.loads(json.dumps(dict(metadata or {}), allow_nan=False))
    per_hypothesis = policy.suite_alpha / (policy.workflow_job_count * len(ids))
    per_look = per_hypothesis / len(policy.looks)
    if not 0 < per_look < 1:
        raise ValueError("confirmation error allocation is outside representable positive range")
    minimum = max(3, math.ceil(-math.log2(per_look)))
    while _sign_tail(minimum,minimum) > per_look:
        minimum += 1
    looks = ([max(12,minimum)] if policy.profile == "ci" else
             [max(12,minimum),max(20,minimum+8)])
    result = {
        "schema": (LEGACY_SCHEMA if legacy else SCHEMA)+".plan", "hypothesis_ids": ids,
        "policy": asdict(policy), "threshold": policy.threshold,
        "screen_threshold": policy.screen_threshold, "looks": looks,
        "minimum_sign_test_sample_count": minimum,
        "family_size": len(ids), "per_hypothesis_alpha": per_hypothesis,
        "per_confirmation_look_alpha": per_look,
        "workflow_false_rejection_budget": policy.suite_alpha,
        "job_false_rejection_budget": policy.suite_alpha / policy.workflow_job_count,
        "statistical_scope": "one declared CI workflow across its predeclared performance jobs; not lifetime control across commits",
        "cross_hypothesis_independence_required": False,
        "distribution_free_claim": False, "assumptions": list(LEGACY_ASSUMPTIONS if legacy else ASSUMPTIONS),
        "blocking_estimand": LEGACY_ESTIMAND if legacy else BLOCKING_ESTIMAND,
        "blocking_null": LEGACY_NULL if legacy else BLOCKING_NULL,
        "blocking_test": BLOCKING_TEST,
        "arithmetic_mean_qualification_changed": False,
        "metadata": metadata,
    }
    if not legacy:
        result.update(measurement_protocol=BLOCK_PROTOCOL, experimental_unit="one balanced four-arm block",
                      complete_targets_per_source_per_observation=2,
                      independence_required=False,
                      conditional_sign_bound_required=True,
                      randomization_scope="independent fair assignment coin per block; source/context reassignment and no unaccounted carry-over")
    return _seal(result)


def _plan(value: Mapping) -> dict:
    legacy = isinstance(value, Mapping) and value.get("schema") == LEGACY_SCHEMA+".plan"
    raw = _verify(value, schema=(LEGACY_SCHEMA if legacy else SCHEMA)+".plan")
    expected = build_guard_plan(raw["hypothesis_ids"], GuardPolicy(**raw["policy"]), metadata=raw["metadata"],
                                measurement_protocol=LEGACY_PROTOCOL if legacy else BLOCK_PROTOCOL)
    if value != expected:
        raise ValueError("guard plan arithmetic or immutable policy differs")
    return raw


def _record_schema(raw, suffix):
    return raw["schema"].removesuffix(".plan")+"."+suffix


def _block(value):
    if isinstance(value, BlockObservation):
        return value
    if not isinstance(value, Mapping) or set(value) != {"run_id", "pairs", "orders", "contexts"}:
        raise ValueError("confirmation requires a complete balanced-block observation")
    return BlockObservation(**value)


def _pair(value: PairObservation | Mapping) -> PairObservation:
    if isinstance(value, PairObservation):
        return value
    if not isinstance(value, Mapping) or set(value) != {"run_id", "reference_seconds", "candidate_seconds"}:
        raise ValueError("a pair requires run_id and reference/candidate seconds")
    return PairObservation(**value)


def _hypothesis(plan: dict, hypothesis_id: str):
    if hypothesis_id not in plan["hypothesis_ids"]:
        raise ValueError("hypothesis was not in the frozen complete family")


def screen_case(plan: Mapping, hypothesis_id: str, observation: PairObservation | Mapping,
                *, warnings: Iterable[str] = (), force_confirmation: bool = False) -> dict:
    raw = _plan(plan)
    _hypothesis(raw, hypothesis_id)
    observation = _pair(observation)
    if isinstance(warnings, (str, bytes)):
        raise ValueError("warnings must be a list of advisory reasons")
    reasons = list(warnings)
    if any(not isinstance(reason, str) or not reason.strip() for reason in reasons):
        raise ValueError("advisory reasons must be nonempty strings")
    if type(force_confirmation) is not bool:
        raise ValueError("force_confirmation must be boolean")
    ratio = observation.candidate_seconds / observation.reference_seconds
    return _seal({"schema": _record_schema(raw,"screen"), "plan_sha256": plan["record_sha256"],
        "hypothesis_id": hypothesis_id, "observation": asdict(observation),
        "point_ratio": ratio, "nominate_confirmation": ratio > raw["screen_threshold"] or force_confirmation,
        "force_confirmation": force_confirmation,
        "advisory_reasons": reasons, "blocking": False,
        "scope": "one advisory paired screen; no statistical non-regression or regression decision"})


@lru_cache(maxsize=128)
def _critical_value(n: int, alpha: float) -> float:
    return float(student_t.isf(alpha, n-1))


@lru_cache(maxsize=2048)
def _sign_tail(positive_count: int, n: int) -> float:
    return float(binom.sf(positive_count-1,n,.5))


def _confirmation_rows(raw,observations,screen_run_ids):
    block = raw.get("measurement_protocol") == BLOCK_PROTOCOL
    rows = [(_block if block else _pair)(row) for row in observations]
    n = len(rows)
    ids = [row.run_id for row in rows]
    if len(set(ids)) != n:
        raise ValueError("confirmation pairs require distinct outer run IDs")
    if isinstance(screen_run_ids, (str, bytes)):
        raise ValueError("screen_run_ids must list excluded screen observations")
    excluded = list(screen_run_ids)
    if (not excluded or any(not isinstance(value, str) or not value.strip() for value in excluded)
            or len(excluded) != len(set(excluded))):
        raise ValueError("at least one unique excluded screen ID is required")
    if set(excluded) & set(ids):
        raise ValueError("screen observations cannot be reused for confirmation")
    if block:
        period_ids = [pair.run_id for row in rows for pair in row.pairs]
        context_ids = [value for row in rows for period in row.contexts for value in period]
        if len(set(ids+period_ids)) != len(ids)+len(period_ids) or set(excluded) & set(period_ids):
            raise ValueError("block periods require globally fresh identities excluded from screens")
        if len(set(context_ids)) != len(context_ids):
            raise ValueError("confirmation execution contexts cannot be reused across blocks")
    return rows,excluded


def confirm_case(plan: Mapping, hypothesis_id: str, observations: Iterable[PairObservation | Mapping],
                 *, screen_run_ids: Iterable[str]) -> dict:
    raw = _plan(plan)
    _hypothesis(raw, hypothesis_id)
    rows,excluded = _confirmation_rows(raw,observations,screen_run_ids)
    n = len(rows)
    if n not in raw["looks"]:
        raise ValueError("confirmation count must equal a predeclared look")
    contrasts = [row.candidate_seconds-raw["threshold"]*row.reference_seconds for row in rows]
    if any(not math.isfinite(value) for value in contrasts):
        raise ValueError("paired runtime contrast is not finite")
    center = statistics.fmean(contrasts)
    spread = statistics.stdev(contrasts)
    se = spread / math.sqrt(n)
    critical = _critical_value(n, raw["per_confirmation_look_alpha"])
    lower = center-critical*se
    mean_reference = statistics.fmean(row.reference_seconds for row in rows)
    mean_candidate = statistics.fmean(row.candidate_seconds for row in rows)
    positive_count = sum(value > 0. for value in contrasts)
    p_value = _sign_tail(positive_count,n)
    reject = p_value <= raw["per_confirmation_look_alpha"]
    result = {"schema": _record_schema(raw,"confirmation"), "plan_sha256": plan["record_sha256"],
        "hypothesis_id": hypothesis_id, "observations": [asdict(row) for row in rows],
        "excluded_screen_run_ids": excluded, "sample_count": n,
        "mean_reference_seconds": mean_reference, "mean_candidate_seconds": mean_candidate,
        "arithmetic_mean_ratio": mean_candidate/mean_reference,
        "blocking_estimand": raw["blocking_estimand"], "blocking_null": raw["blocking_null"],
        "blocking_test": BLOCKING_TEST,
        "positive_pair_count": positive_count, "nonpositive_pair_count": n-positive_count,
        "exact_one_sided_p_value": p_value, "ties_count_as_nonpositive": True,
        "arithmetic_mean_contrast_diagnostic": {
            "definition": "candidate_seconds - threshold * reference_seconds",
            "mean_seconds": center, "standard_error_seconds": se,
            "gaussian_one_sided_lower_bound_seconds": lower, "gaussian_critical_value": critical,
            "would_reject_under_gaussian_assumptions": lower > 0.,
            "blocking": False, "scope": "diagnostic only; does not establish arithmetic-mean non-regression"},
        "one_sided_alpha": raw["per_confirmation_look_alpha"],
        "confirmed_regression": reject,
        "status": "confirmed_regression" if reject else "no_confirmed_regression",
        "scope": ("direct full-target median paired-ratio sign test, conditional on the frozen independence assumptions"
                  if "measurement_protocol" not in raw else "direct balanced full-target block sign test, conditional on the frozen randomized-slot and no-carryover assumptions"),
        "affirmative_non_regression_certification": False}
    if "measurement_protocol" in raw:
        result.update(block_count=n, complete_pair_count=2*n, experimental_unit="balanced four-arm block",
                      positive_block_count=positive_count, nonpositive_block_count=n-positive_count)
    return _seal(result)


def confirmation_futility(plan: Mapping, hypothesis_id: str,
                          observations: Iterable[PairObservation | Mapping],
                          *, screen_run_ids: Iterable[str]) -> dict:
    """Stop only when every remaining look is unable to reject, even with all future signs positive."""
    raw = _plan(plan)
    _hypothesis(raw,hypothesis_id)
    rows,excluded = _confirmation_rows(raw,observations,screen_run_ids)
    n = len(rows)
    if n > raw["looks"][-1]:
        raise ValueError("confirmation exceeds the frozen schedule")
    positive = sum(row.candidate_seconds > raw["threshold"]*row.reference_seconds for row in rows)
    possibilities = [{"look":look,"maximum_possible_positive_count":positive+look-n,
        "best_possible_p_value":_sign_tail(positive+look-n,look)}
        for look in raw["looks"] if look >= n]
    reachable = [row["look"] for row in possibilities
                 if row["best_possible_p_value"] <= raw["per_confirmation_look_alpha"]]
    return _seal({"schema":_record_schema(raw,"futility"),"plan_sha256":plan["record_sha256"],
        "hypothesis_id":hypothesis_id,"observations":[asdict(row) for row in rows],
        "excluded_screen_run_ids":excluded,"sample_count":n,"positive_pair_count":positive,
        "reachable_looks":reachable,"best_possible_future_results":possibilities,
        "futile":not reachable,"blocking":False,
        "scope":"valid early non-rejection: no permitted look can reject with any remaining outcomes"})


def build_guard_report(plan: Mapping, screens: Iterable[Mapping], confirmations: Iterable[Mapping],
                       *, functional_failures: Iterable[str] = (), advisory: Iterable[str] = (),
                       futility_records: Iterable[Mapping] = ()) -> dict:
    """The sole guard exit decision; engineering diagnostics cannot turn it red."""
    raw = _plan(plan)
    screens = list(screens)
    confirmations = list(confirmations)
    if isinstance(functional_failures, (str, bytes)) or isinstance(advisory, (str, bytes)):
        raise ValueError("failure and advisory messages must be lists")
    failures, messages = list(functional_failures), list(advisory)
    if any(not isinstance(message, str) or not message.strip() for message in (*failures, *messages)):
        raise ValueError("failure and advisory messages must be nonempty strings")
    by_id = {}
    for receipt in screens:
        screen = _verify(receipt, schema=_record_schema(raw,"screen"))
        if screen["hypothesis_id"] in by_id:
            raise ValueError("duplicate hypothesis screen")
        expected = screen_case(plan, screen["hypothesis_id"], screen["observation"],
                               warnings=screen["advisory_reasons"], force_confirmation=screen["force_confirmation"])
        if receipt != expected:
            raise ValueError("screen receipt does not replay")
        by_id[screen["hypothesis_id"]] = screen
        messages.extend(f"{screen['hypothesis_id']}: {reason}" for reason in screen["advisory_reasons"])
    seen = {}
    confirmed = set()
    block_registry, period_registry, context_registry = {}, {}, {}
    all_screen_ids = {value["observation"]["run_id"] for value in by_id.values()}
    def register_blocks(name, observations):
        if raw.get("measurement_protocol") != BLOCK_PROTOCOL:
            return
        for value in observations:
            block = _block(value)
            identity = (name, _digest(asdict(block)))
            if block.run_id in all_screen_ids or block.run_id in period_registry:
                raise ValueError("a screen or period cannot be reused as a confirmation block")
            prior = block_registry.setdefault(block.run_id, identity)
            if prior != identity:
                raise ValueError("confirmation block identity reused across hypotheses or changed")
            for pair in block.pairs:
                if pair.run_id in all_screen_ids or pair.run_id in block_registry:
                    raise ValueError("a screen or block cannot be reused as a confirmation period")
                if period_registry.setdefault(pair.run_id, identity) != identity:
                    raise ValueError("confirmation period reused across blocks or hypotheses")
            for period in block.contexts:
                for context in period:
                    if context_registry.setdefault(context, identity) != identity:
                        raise ValueError("execution context reused across blocks or hypotheses")
    def check_earlier_prefixes(name,observations,excluded):
        # An omitted look is not permission to continue after its rejection.
        # Every later receipt carries enough raw observations to replay it.
        for look in raw["looks"]:
            if look >= len(observations):
                break
            earlier = confirm_case(plan,name,observations[:look],screen_run_ids=excluded)
            if earlier["confirmed_regression"]:
                raise ValueError("a later receipt cannot erase an earlier fixed-look rejection")
    for receipt in confirmations:
        result = _verify(receipt, schema=_record_schema(raw,"confirmation"))
        name = result["hypothesis_id"]
        if name not in by_id or not by_id[name]["nominate_confirmation"]:
            raise ValueError("confirmation requires its nominated screen")
        excluded = [by_id[name]["observation"]["run_id"]]
        expected = confirm_case(plan, name, result["observations"], screen_run_ids=excluded)
        if receipt != expected:
            raise ValueError("confirmation receipt does not replay")
        register_blocks(name,result["observations"])
        check_earlier_prefixes(name,result["observations"],excluded)
        history = seen.setdefault(name, [])
        if history:
            prior = history[-1]
            if (result["sample_count"] <= prior["sample_count"] or
                    result["observations"][:prior["sample_count"]] != prior["observations"]):
                raise ValueError("confirmation looks must extend the same frozen sequence")
            if prior["confirmed_regression"]:
                raise ValueError("cannot retry a previously confirmed regression")
        history.append(result)
        if result["confirmed_regression"]:
            confirmed.add(name)
    futile = {}
    for receipt in futility_records:
        result = _verify(receipt,schema=_record_schema(raw,"futility"))
        name = result["hypothesis_id"]
        if name in futile or name in confirmed or name not in by_id or not by_id[name]["nominate_confirmation"]:
            raise ValueError("futility requires a distinct nominated, unconfirmed hypothesis")
        expected = confirmation_futility(plan,name,result["observations"],
            screen_run_ids=[by_id[name]["observation"]["run_id"]])
        if receipt != expected or not result["futile"]:
            raise ValueError("futility receipt does not replay")
        register_blocks(name,result["observations"])
        check_earlier_prefixes(name,result["observations"],expected["excluded_screen_run_ids"])
        if name in seen:
            prior = seen[name][-1]
            if result["sample_count"] < prior["sample_count"] or result["observations"][:prior["sample_count"]] != prior["observations"]:
                raise ValueError("futility stop must extend the evaluated confirmation sequence")
        # Keep the validated sealed proof in new reports. _verify returns a
        # body for lookup/checking; retaining that body would silently discard
        # its identity and make a serialized report impossible to replay.
        # Historical v2 serialization remains frozen for explicit legacy replay.
        futile[name] = (result if raw["schema"] == LEGACY_SCHEMA+".plan"
                        else dict(receipt))
    missing = [name for name in raw["hypothesis_ids"] if name not in by_id]
    unconfirmed = [name for name, result in by_id.items() if result["nominate_confirmation"] and name not in seen and name not in futile]
    early = [name for name, history in seen.items()
             if history[-1]["sample_count"] < raw["looks"][-1] and name not in confirmed and name not in futile]
    if missing:
        messages.append("screening unavailable or incomplete for: "+", ".join(missing))
    if unconfirmed:
        messages.append("confirmation unavailable or budget-limited for: "+", ".join(unconfirmed))
    if early:
        messages.append("confirmation stopped before the final predeclared look for: "+", ".join(early))
    blocking = bool(failures or confirmed)
    return _seal({"schema": _record_schema(raw,"report"), "plan_sha256": plan["record_sha256"],
        "status": "blocked" if blocking else "no_confirmed_regression",
        "exit_code": 2 if blocking else 0,
        "functional_status": "failed" if failures else "no_functional_failure_reported",
        "functional_failures": failures, "confirmed_regressions": sorted(confirmed),
        "performance_status": "confirmed_regression" if confirmed else "no_confirmed_regression",
        "screened_hypotheses": list(by_id), "missing_screens": missing,
        "unconfirmed_nominations": unconfirmed, "confirmation_effort_limited": early, "advisory": messages,
        "screens": screens, "confirmations": confirmations,
        "futility_records":list(futile.values()),"futile_confirmations":list(futile),
        "qualification_status": "not_evaluated_by_fast_guard",
        "affirmative_non_regression_certification": False,
        "probability_scope": "conditional workflow-level statistical false rejection only; deterministic correctness and infrastructure failures are separate",
        "workflow_false_rejection_budget": raw["workflow_false_rejection_budget"],
        "blocking_estimand":raw["blocking_estimand"],"blocking_null":raw["blocking_null"],"blocking_test":BLOCKING_TEST,
        "statistical_assumptions":raw["assumptions"],
        "distribution_free_claim": False})
