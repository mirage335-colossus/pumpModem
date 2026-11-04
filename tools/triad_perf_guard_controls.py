#!/usr/bin/env python3
"""Cheap model checks of the workflow-level performance guard.

These simulations test the actual guard aggregator on a retained subset and a
cross-checked vectorized equivalent on the complete Monte Carlo sample. They
measure behavior under named distributions, including nonnormal populations and
deliberate violations of the guard's independent-sign assumption. They are not radio or CPU
performance measurements and establish no distribution-free machine guarantee.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import time

import numpy as np
from scipy.stats import beta, norm

from triad_perf_guard_policy import (BlockObservation, GuardPolicy, PairObservation, _sign_tail,
                                    build_guard_plan, build_guard_report,
                                    confirmation_futility, confirm_case, screen_case)


SCHEMA = "triad-performance-guard-distribution-controls-v2"

# The confirmation design uses one independently randomized four-context block
# as its inferential unit.  These labels deliberately match the execution
# schedule: the two sources each occupy both sides of the temporal midpoint.
CROSSOVER_ORDERS = (("candidate", "reference", "reference", "candidate"),
                    ("reference", "candidate", "candidate", "reference"))


def _hash(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def binomial_interval(successes, trials, alpha=.05):
    if type(successes) is not int or type(trials) is not int or not 0 <= successes <= trials or trials < 1:
        raise ValueError("valid integer binomial counts required")
    return [0. if successes == 0 else float(beta.ppf(alpha/2, successes, trials-successes+1)),
            1. if successes == trials else float(beta.ppf(1-alpha/2, successes+1, trials-successes))]


def randomized_crossover_blocks(*, trials, blocks, seed, threshold, slowdown=1.,
                                control="fixed-slot", jitter=.002):
    """Simulate complete four-context confirmations with a persistent slot state.

    Slot delays are drawn ONCE and retained across all blocks. Each block gets a
    fresh fair assignment coin, and source labels are permuted over the same four
    execution positions. This preserves the nuisance that broke permanent
    source-per-worker comparisons while modeling the actual proposed reset and
    reassignment protocol. Returned rows contain the four raw arms plus the one
    aggregate timing contrast that the guard must test.

    ``linear-drift`` adds source-independent drift by chronological position;
    symmetric placement cancels its linear component. ``slot-period`` adds a
    fixed alternating context-quality effect, whose sign is randomized by the
    block coin. ``full-only`` and ``preparation-only`` distinguish target timer
    cost from work outside it. ``work-regime`` is an integrity/work-counter
    failure and must be rejected before statistical timing admission.
    """
    if type(trials) is not int or trials < 1 or type(blocks) is not int or blocks < 1:
        raise ValueError("positive trial and block counts required")
    if not math.isfinite(threshold) or threshold <= 0 or not math.isfinite(slowdown) or slowdown <= 0:
        raise ValueError("finite positive threshold and slowdown required")
    if control not in {"fixed-slot", "linear-drift", "slot-period", "general",
                       "full-only", "preparation-only", "work-regime"}:
        raise ValueError("unknown crossover control")
    rng = np.random.default_rng(seed)
    # Permanent execution-slot characteristics, shared by every block. Odd
    # and even slots are intentionally very different to reproduce the prior
    # 19/1 persistent-context counterexample rather than IID resampling it.
    # Two paired invocations reuse logical slots A/B, so chronological slot
    # quality is slow/fast/slow/fast. Each crossover order gives both sources
    # one visit to each persistent slot.
    slot_factor = np.array([19., 1., 19., 1.], dtype=np.float64)
    result = []
    for trial in range(trials):
        trial_rows = []
        for block in range(blocks):
            coin = int(rng.integers(0, 2))
            order = CROSSOVER_ORDERS[coin]
            # A common execution-slot nuisance stays in the same chronological
            # slot across blocks. A tiny fresh perturbation represents timer
            # noise without resetting the large latent context effect.
            base = slot_factor.copy()
            if control in ("linear-drift", "general"):
                base *= 1. + .04 * np.arange(4, dtype=np.float64)
            if control in ("slot-period", "general"):
                base *= np.array([1.4, .8, .8, 1.4], dtype=np.float64)
            raw = []
            for position, source in enumerate(order):
                elapsed = float(base[position] * math.exp(rng.normal(0., jitter)))
                target_elapsed = elapsed
                preparation_elapsed = .1 * elapsed
                work_valid = True
                if source == "candidate" and control not in ("preparation-only", "full-only"):
                    target_elapsed *= slowdown
                if source == "candidate" and control == "full-only":
                    # The stored target is the full target. The separate screen
                    # control is reported as neutral in the raw row.
                    target_elapsed *= slowdown
                if source == "candidate" and control == "preparation-only":
                    preparation_elapsed *= slowdown
                if source == "candidate" and control == "work-regime":
                    work_valid = False
                screen_elapsed = elapsed
                raw.append({"position": position, "source": source,
                    "context_id": f"control-{trial}-{block}-{position}",
                    "target_seconds": target_elapsed,
                    "screen_seconds": screen_elapsed,
                    "preparation_seconds": preparation_elapsed,
                    "work_valid": work_valid})
            c_total = sum(row["target_seconds"] for row in raw if row["source"] == "candidate")
            r_total = sum(row["target_seconds"] for row in raw if row["source"] == "reference")
            trial_rows.append({"block": block, "coin": coin, "order": list(order),
                "arms": raw, "candidate_total_seconds": c_total,
                "reference_total_seconds": r_total,
                "ratio": c_total / r_total,
                "positive": c_total > threshold * r_total,
                "work_valid": all(row["work_valid"] for row in raw)})
        result.append(trial_rows)
    return result


def crossover_summary(rows, *, threshold, alpha=.05, looks=(12,)):
    """Summarize randomized-block signs with an engineering-envelope interval.

    The interval is an exact binomial interval over simulated workflows. It
    quantifies this declared randomization/control model only. It is not a
    guarantee under arbitrary temporal dependence or uncontrolled carry-over.
    """
    if not rows or any(not row for row in rows):
        raise ValueError("nonempty workflow blocks required")
    if not looks or any(type(look) is not int or look < 1 for look in looks) or tuple(sorted(set(looks))) != tuple(looks):
        raise ValueError("looks must be a strictly increasing tuple of positive counts")
    if max(looks) > len(rows[0]) or any(len(workflow) < max(looks) for workflow in rows):
        raise ValueError("each workflow must include every declared look")
    adjusted = alpha / len(looks)
    from scipy.stats import binom
    blocked = 0
    for workflow in rows:
        cumulative = 0
        rejected = False
        for count, block in enumerate(workflow[:max(looks)], start=1):
            cumulative += int(block["positive"])
            if count in looks and binom.cdf(count-cumulative, count, .5) <= adjusted:
                rejected = True
        blocked += int(rejected)
    total = len(rows)
    valid = all(block["work_valid"] for workflow in rows for block in workflow)
    return {"workflows": total, "blocks_per_workflow": len(rows[0]),
        "blocked_workflows": blocked, "blocked_rate": blocked / total,
        "blocked_rate_interval": binomial_interval(blocked, total, alpha=alpha),
        "all_work_counters_valid": valid,
        "threshold": threshold,
        "looks": list(looks), "per_look_alpha": adjusted,
        "inferential_unit": "fair randomized four-context crossover block; two runtimes per source summed",
        "assumption_scope": "fresh contexts, no source carry-over, fair independent block coin; declared persistent slot model only",
        "universal_guarantee": False}


def _crossover_inputs(rng, trials, hypotheses, blocks, *, jitter=.002):
    # Retain a persistent logical-slot signature, then independently randomize
    # source assignment in every block. Arm jitter, unlike the slot state, is
    # newly sampled per fresh context.
    coins = rng.integers(0, 2, size=(trials,hypotheses,blocks), dtype=np.int8)
    noise = np.exp(rng.normal(0., jitter, size=(trials,hypotheses,blocks,4)))
    return coins, noise


def _crossover_totals(coins, noise, *, slowdown, control):
    slot_factor = np.array([19.,1.,19.,1.], dtype=np.float64)
    slot_factor = np.broadcast_to(slot_factor, noise.shape)
    if control in ("linear-drift", "general"):
        slot_factor = slot_factor*np.array([1.,1.04,1.08,1.12])
    if control in ("slot-period", "general"):
        slot_factor = slot_factor*np.array([1.4,.8,.8,1.4])
    # coin 0: C,R,R,C; coin 1: R,C,C,R. This schedules two opposite
    # source orders in every block. Both sources see distinct fresh contexts.
    pattern0 = np.array([1.,0.,0.,1.])
    pattern1 = 1.-pattern0
    candidate_mask = np.where(coins[...,None] == 0, pattern0, pattern1)
    reference_mask = 1.-candidate_mask
    base = slot_factor*noise
    target_slowdown = slowdown if control not in ("preparation-only",) else 1.
    candidate = np.sum(base*candidate_mask,axis=-1)*target_slowdown
    reference = np.sum(base*reference_mask,axis=-1)
    return candidate, reference, candidate_mask


def _block_observation_from_arrays(coins, noise, *, trial, hypothesis, block,
                                   slowdown, control):
    order = CROSSOVER_ORDERS[int(coins[trial,hypothesis,block])]
    slot = np.array([19.,1.,19.,1.],dtype=np.float64)
    if control in ("linear-drift", "general"):
        slot *= 1.+.04*np.arange(4)
    if control in ("slot-period", "general"):
        slot *= np.array([1.4,.8,.8,1.4])
    arms=[]
    for position,source in enumerate(order):
        elapsed=float(slot[position]*noise[trial,hypothesis,block,position])
        target=elapsed*(slowdown if source=="candidate" and control!="preparation-only" else 1.)
        prep=.1*elapsed*(slowdown if source=="candidate" and control=="preparation-only" else 1.)
        arms.append({"position":position,"source":source,"context_id":
            f"ctx-{trial}-{hypothesis}-{block}-{position}","target_seconds":target,
            "preparation_seconds":prep,"screen_seconds":elapsed,
            "work_valid":not (source=="candidate" and control=="work-regime")})
    pairs=[]; orders=[]; contexts=[]
    for period in range(2):
        pairarms=arms[period*2:period*2+2]
        by_source={arm["source"]:arm for arm in pairarms}
        pairs.append(PairObservation(f"period-{trial}-{hypothesis}-{block}-{period}",
            by_source["reference"]["target_seconds"],by_source["candidate"]["target_seconds"]))
        orders.append(tuple(arm["source"] for arm in pairarms))
        contexts.append((by_source["reference"]["context_id"],by_source["candidate"]["context_id"]))
    blockobs=BlockObservation(f"block-{trial}-{hypothesis}-{block}",tuple(pairs),tuple(orders),tuple(contexts))
    return blockobs,arms


def simulate_crossover_case(spec, *, trials, seed, parity_workflows=4):
    """Replay complete fixed-size workflows under the randomized-block protocol.

    This runs the actual policy aggregator on selected full workflows and checks
    its decisions against the vectorized complete Monte Carlo sample. The
    persistent 19/1 logical-slot signature is shared across all workflows; only
    fresh context noise and the predeclared assignment coins vary. This validates
    the declared experiment, not arbitrary carry-over, clock changes or future
    hardware.
    """
    h,jobs=spec["hypotheses_per_job"],spec["workflow_jobs"]
    policy=GuardPolicy(spec["profile"],suite_alpha=spec["suite_alpha"],workflow_job_count=jobs)
    plan=build_guard_plan([f"case-{i}" for i in range(h)],policy,
        metadata={"experiment":"randomized four-arm crossover control"})
    looks=tuple(plan["looks"]);maxblocks=looks[-1]
    rng=np.random.default_rng(seed)
    jitter=float(spec.get("arm_jitter",.002))
    if not math.isfinite(jitter) or jitter < 0.:
        raise ValueError("arm_jitter must be finite and nonnegative")
    coins,noise=_crossover_inputs(rng,trials,h*jobs,maxblocks,jitter=jitter)
    # The screen is a deliberately advisory, force-nominated sample; inferential
    # errors are counted over complete confirmations, with screen data excluded.
    ratios=np.full((trials,h*jobs),spec["ratio"],dtype=float)
    scope=spec.get("effect_scope","all")
    if scope=="single":
        ratios.fill(1.);ratios[:,0]=spec["ratio"]
    elif scope!="all":
        raise ValueError("effect_scope must be all or single")
    controls=spec.get("control","fixed-slot")
    block_candidate=np.empty((trials,h*jobs,maxblocks),dtype=np.float64)
    block_reference=np.empty_like(block_candidate)
    valid_work=np.ones_like(block_candidate,dtype=bool)
    for case in range(h*jobs):
        cc,rr,_=_crossover_totals(coins[:,case:case+1,:],noise[:,case:case+1,:,:],
            slowdown=1.,control=controls)
        # Keep per-hypothesis effect scope explicit. Context generation itself is
        # shared; only the candidate source multiplier differs.
        factor=np.ones(trials) if controls=="preparation-only" else ratios[:,case]
        block_candidate[:,case,:]=cc[:,0,:]*factor[:,None]
        block_reference[:,case,:]=rr[:,0,:]
        if controls=="work-regime": valid_work[:,case,:]=False
    positives=block_candidate > policy.threshold*block_reference
    rejected=np.zeros((trials,h*jobs),dtype=bool)
    futile=np.zeros_like(rejected)
    counts=positives.cumsum(axis=2)
    for count in range(1,maxblocks+1):
        if count in looks:
            tail=np.array([_sign_tail(k,count) for k in range(count+1)])
            active=~rejected & ~futile
            new=active & (tail[counts[:,:,count-1]] <= plan["per_confirmation_look_alpha"])
            rejected |= new
        active=~rejected & ~futile
        reach=np.zeros_like(active)
        for look in looks:
            if look>=count:
                maximum=counts[:,:,count-1]+look-count
                tail=np.array([_sign_tail(k,look) for k in range(look+1)])
                reach |= tail[maximum] <= plan["per_confirmation_look_alpha"]
        futile |= active & ~reach
    performance_reject=np.any(rejected.reshape(trials,jobs,h),axis=(1,2))
    functional_reject=np.any(~valid_work.reshape(trials,jobs,h,maxblocks)[:,:,:,0],axis=(1,2))
    expected=performance_reject | functional_reject
    parity=min(parity_workflows,trials)
    actual=np.zeros(parity,dtype=bool)
    for trial in range(parity):
        job_reports=[]
        for job in range(jobs):
            screens=[]; confirms=[]; futility=[]
            for local in range(h):
                case=job*h+local;name=f"case-{local}"
                screen_id=f"screen-{trial}-{job}-{local}"
                screen=screen_case(plan,name,PairObservation(screen_id,10.,10.),force_confirmation=True)
                screens.append(screen); observations=[]
                for count in range(maxblocks):
                    ob,_=_block_observation_from_arrays(coins,noise,trial=trial,hypothesis=case,
                        block=count,slowdown=float(ratios[trial,case]),control=controls)
                    observations.append(ob)
                    if count+1 in looks:
                        receipt=confirm_case(plan,name,observations,screen_run_ids=[screen_id])
                        confirms.append(receipt)
                        if receipt["confirmed_regression"]: break
                    stop=confirmation_futility(plan,name,observations,screen_run_ids=[screen_id])
                    if stop["futile"]:
                        futility.append(stop);break
            report=build_guard_report(plan,screens,confirms,
                functional_failures=(["changed work regime"] if controls=="work-regime" else []),
                futility_records=futility)
            job_reports.append(report["exit_code"]!=0)
        actual[trial]=any(job_reports)
    if not np.array_equal(actual,expected[:parity]):
        raise AssertionError("vectorized randomized-block decisions disagree with actual aggregator")
    blocked=int(np.sum(expected))
    performance_blocked=int(np.sum(performance_reject))
    functional_blocked=int(np.sum(functional_reject))
    work_invalid=int(np.sum(~valid_work[:,:,0]))
    affected=ratios>policy.threshold
    detected=np.any(rejected & affected,axis=1)
    null=not bool(np.any(affected))
    return {**spec,"protocol":"randomized-crossover-block-v1","trials":trials,"seed":seed,
        "plan":plan,"blocked_workflows":blocked,"blocked_rate":blocked/trials,
        "performance_rejected_workflows":performance_blocked,
        "functional_failure_workflows":functional_blocked,
        "out_of_target_preparation_slowdown":controls=="preparation-only" and spec["ratio"]>1.,
        "target_timer_scope":"per-block target runtimes; preparation and setup remain separately reported",
        "blocked_rate_95_percent_interval":binomial_interval(blocked,trials),
        "performance_rejection_rate":performance_blocked/trials,
        "performance_rejection_rate_95_percent_interval":binomial_interval(performance_blocked,trials),
        "median_sign_null":null,"false_block_rate":performance_blocked/trials if null else None,
        "missed_regression_workflows":trials-int(np.sum(detected)) if not null else None,
        "detection_rate":float(np.mean(detected)) if not null else None,
        "detection_rate_95_percent_interval":binomial_interval(int(np.sum(detected)),trials) if not null else None,
        "invalid_work_counter_hypothesis_blocks":work_invalid,
        "actual_aggregator_parity_workflows":parity,"actual_aggregator_parity":"all matched",
        "statistical_unit":"one fresh randomized four-context block with two complete pairs",
        "assumptions":"fair independent block assignment coin, fresh execution contexts, no source carry-over; persistent slot effects remain fixed across the workflow",
        "scope_limit":"declared synthetic workflow and timing model only; not a universal future false-block guarantee"}


def scenarios(ci_hypotheses=36, dev_hypotheses=39):
    result = []
    def add(profile, ratio, *, distribution="gaussian-contrast", rho=.8, jobs=1, alpha=None):
        policy = GuardPolicy(profile, suite_alpha=alpha, workflow_job_count=jobs)
        result.append({"profile": profile, "suite_alpha": policy.suite_alpha,
            "hypotheses_per_job": ci_hypotheses if profile == "ci" else dev_hypotheses,
            "workflow_jobs": jobs, "ratio": ratio, "distribution": distribution,
            "latent_cross_case_correlation": rho})
    for rho in (0., .8):
        for ratio in (1., 1.69, 1.70, 1.71, 2., 3.):
            add("ci", ratio, rho=rho)
    for jobs in (1, 2):
        for alpha in (.01, .05):
            for ratio in (1.7, 2.):
                add("ci", ratio, jobs=jobs, alpha=alpha)
    for ratio in (1., 1.29, 1.30, 1.31, 1.6):
        add("gpu-dev", ratio)
    for distribution in ("lognormal-scheduling", "heavy-lognormal-scheduling",
                         "rademacher-jitter", "rare-fast-bimodal", "serial-regime"):
        for rho in (0., .8):
            add("ci", 1.7, distribution=distribution, rho=rho)
    for distribution in ("rademacher-jitter", "rare-fast-bimodal", "serial-regime"):
        add("gpu-dev", 1.3, distribution=distribution)
    return result


def extension_scenarios(ci_hypotheses=36, dev_hypotheses=39):
    result = []
    for alpha in (.01,.05):
        for ratio in (1.7,1.71,2.,3.):
            result.append({"profile":"ci","suite_alpha":alpha,"hypotheses_per_job":ci_hypotheses,
                "workflow_jobs":1,"ratio":ratio,"distribution":"gaussian-contrast",
                "latent_cross_case_correlation":.8,"effect_scope":"single"})
    for ratio in (1.30,1.31,1.6,2.):
        result.append({"profile":"gpu-dev","suite_alpha":.05,"hypotheses_per_job":dev_hypotheses,
            "workflow_jobs":1,"ratio":ratio,"distribution":"gaussian-contrast",
            "latent_cross_case_correlation":.8,"effect_scope":"single"})
    for distribution in ("bimodal-scheduling","serial-bimodal-scheduling"):
        for rho in (0.,.8):
            result.append({"profile":"ci","suite_alpha":.01,"hypotheses_per_job":ci_hypotheses,
                "workflow_jobs":1,"ratio":1.,"distribution":distribution,
                "latent_cross_case_correlation":rho,"effect_scope":"all"})
    return result


def crossover_scenarios(ci_hypotheses=36, dev_hypotheses=39):
    """Complete-workflow adversarial controls for the randomized block protocol."""
    result=[]
    def add(profile,ratio,control,*,effect_scope="all",suite_alpha=None):
        result.append({"profile":profile,"suite_alpha":suite_alpha,
            "hypotheses_per_job":ci_hypotheses if profile=="ci" else dev_hypotheses,
            "workflow_jobs":1,"ratio":ratio,"effect_scope":effect_scope,
            "distribution":"persistent-context-crossover","control":control,
            "latent_cross_case_correlation":0.})
    for control in ("fixed-slot","linear-drift","slot-period","general"):
        add("ci",1.,control)
    add("ci",1.7,"fixed-slot")
    add("ci",1.71,"fixed-slot")
    result[-1]["arm_jitter"]=.05
    add("ci",2.,"fixed-slot")
    add("ci",2.,"full-only")
    add("ci",2.,"preparation-only")
    add("ci",1.,"work-regime")
    add("gpu-dev",1.,"general")
    add("gpu-dev",1.31,"fixed-slot")
    add("gpu-dev",1.6,"fixed-slot")
    return result


def _draw(rng, trials, count, n, *, ratio, threshold, distribution, rho):
    # The leading row is an independent screen except in the explicitly serial
    # adversarial model. Its stored IDs remain disjoint in every model.
    shared = rng.standard_normal((trials, 1, n))
    z = math.sqrt(rho)*shared+math.sqrt(1-rho)*rng.standard_normal((trials,count,n))
    reference = np.full((trials,count,n), 10., dtype=np.float64)
    if distribution == "gaussian-contrast":
        contrast = (ratio-threshold)*10.+.5*z
        candidate = threshold*reference+contrast
        assumptions = "independent stationary Gaussian contrasts; arbitrary declared cross-case correlation; no Gaussian assumption is needed by the exact sign test"
    elif distribution in ("lognormal-scheduling", "heavy-lognormal-scheduling",
                           "bimodal-scheduling", "serial-bimodal-scheduling"):
        sigma = .5 if distribution == "lognormal-scheduling" else 1.0
        za = math.sqrt(rho)*shared+math.sqrt(1-rho)*rng.standard_normal((trials,count,n))
        zb = math.sqrt(rho)*shared+math.sqrt(1-rho)*rng.standard_normal((trials,count,n))
        if distribution in ("bimodal-scheduling","serial-bimodal-scheduling"):
            if distribution == "serial-bimodal-scheduling":
                za,zb = za[:,:,:1],zb[:,:,:1]
            reference = np.where(za>=0.,19.,1.)+1e-6*rng.standard_normal(z.shape)
            candidate = ratio*np.where(zb>=0.,19.,1.)+1e-6*rng.standard_normal(z.shape)
            assumptions = ("reference and candidate have identical two-regime runtime laws at ratio1; "
                +("source-session regime persists across screen/confirmations, violating pair independence"
                  if distribution == "serial-bimodal-scheduling" else "independent paired draws; nonnormal contrasts"))
        else:
            reference = 10.*np.exp(sigma*za-sigma*sigma/2)
            candidate = ratio*10.*np.exp(sigma*zb-sigma*sigma/2)
            assumptions = "independent positive lognormal scheduling; nonnormal contrasts are permitted by the exact sign test"
    elif distribution == "rademacher-jitter":
        contrast = (ratio-threshold)*10.+np.where(z >= 0.,1.,-1.)+1e-6*rng.standard_normal(z.shape)
        candidate = threshold*reference+contrast
        assumptions = "independent symmetric near-Rademacher contrasts; this nonnormal median-null law is permitted by the exact sign test"
    elif distribution == "rare-fast-bimodal":
        # P(positive)=.9, contrast +1 or -9 gives exactly zero population
        # contrast mean at the threshold. Candidate runtime remains positive.
        from scipy.special import ndtr
        contrast = (ratio-threshold)*10.+np.where(ndtr(z) < .9,1.,-9.)+1e-6*rng.standard_normal(z.shape)
        candidate = threshold*reference+contrast
        assumptions = "independent asymmetric two-regime contrasts; arithmetic mean and median effects differ"
    elif distribution == "serial-regime":
        # A regime lasts across screen and every confirmation observation.
        regime = z[:,:,:1]
        contrast = (ratio-threshold)*10.+np.where(regime >= 0.,1.,-1.)+1e-6*rng.standard_normal(z.shape)
        candidate = threshold*reference+contrast
        assumptions = "regime persists through screen and confirmations; violates the independent-sign assumption"
    else:
        raise ValueError("unknown distribution")
    if not np.isfinite(reference).all() or not np.isfinite(candidate).all() or np.any(candidate <= 0):
        raise ValueError("simulation produced an invalid physical runtime")
    return reference, candidate, assumptions


def _replay_one(plan, reference, candidate, expected_reject, expected_pairs, *, trial, h, jobs):
    any_reject = False
    actual_pairs = 0
    for job in range(jobs):
        screens, confirmations, futile = [], [], []
        for local,name in enumerate(plan["hypothesis_ids"]):
            index = job*h+local
            screen_id = f"screen-{trial}-{job}-{local}"
            screen = screen_case(plan,name,PairObservation(screen_id,
                float(reference[index,0]),float(candidate[index,0])))
            screens.append(screen)
            if screen["nominate_confirmation"]:
                rows = []
                for count in range(1,plan["looks"][-1]+1):
                    rows.append(PairObservation(f"confirmation-{trial}-{job}-{local}-{count-1}",
                        float(reference[index,count]),float(candidate[index,count])))
                    actual_pairs += 1
                    if count in plan["looks"]:
                        receipt = confirm_case(plan,name,rows,screen_run_ids=[screen_id])
                        confirmations.append(receipt)
                        if receipt["confirmed_regression"]:
                            break
                    stop = confirmation_futility(plan,name,rows,screen_run_ids=[screen_id])
                    if stop["futile"]:
                        futile.append(stop)
                        break
        report = build_guard_report(plan,screens,confirmations,futility_records=futile)
        any_reject |= report["exit_code"] != 0
    if any_reject != bool(expected_reject):
        raise AssertionError("vectorized decision disagrees with actual aggregator")
    if actual_pairs != expected_pairs:
        raise AssertionError("vectorized futility pair count disagrees with actual aggregator")
    return any_reject


def population_sign_probability(spec, ratios, threshold):
    """Known probabilities for these synthetic populations, separate from their mean ratios."""
    distribution,rho = spec["distribution"],spec["latent_cross_case_correlation"]
    delta = (ratios-threshold)*10.
    if distribution == "gaussian-contrast":
        return norm.sf(-delta/.5)
    if distribution in ("lognormal-scheduling","heavy-lognormal-scheduling"):
        sigma = .5 if distribution == "lognormal-scheduling" else 1.
        return norm.sf(np.log(threshold/ratios)/(sigma*math.sqrt(2*(1-rho))))
    if distribution in ("rademacher-jitter","serial-regime"):
        return .5*norm.sf((-delta-1.)/1e-6)+.5*norm.sf((-delta+1.)/1e-6)
    if distribution == "rare-fast-bimodal":
        return .9*norm.sf((-delta-1.)/1e-6)+.1*norm.sf((-delta+9.)/1e-6)
    if distribution in ("bimodal-scheduling","serial-bimodal-scheduling"):
        # These extension cases have ratio1, far from any timing-mode tie at
        # the1.7 threshold. Only low-reference/high-candidate pairs exceed it.
        if not np.all(ratios == 1.) or threshold != 1.7:
            raise ValueError("bimodal scheduling probability only declared for unchanged CI control")
        return np.full_like(ratios,math.acos(rho)/(2*math.pi))
    raise ValueError("unknown synthetic population")


def simulate_case(spec, *, trials, seed, parity_workflows=4):
    if type(trials) is not int or trials < 1 or type(parity_workflows) is not int or parity_workflows < 0:
        raise ValueError("positive trials and nonnegative parity count required")
    h, jobs = spec["hypotheses_per_job"], spec["workflow_jobs"]
    policy = GuardPolicy(spec["profile"], suite_alpha=spec["suite_alpha"], workflow_job_count=jobs)
    plan = build_guard_plan([f"case-{i}" for i in range(h)],policy,
                            metadata={"experiment": "synthetic distribution control"},
                            measurement_protocol="legacy-pair-v2")
    rng = np.random.default_rng(seed)
    ratios = np.full((1,h*jobs,1),spec["ratio"])
    if spec.get("effect_scope","all") == "single":
        ratios.fill(1.);ratios[0,0,0]=spec["ratio"]
    elif spec.get("effect_scope","all") != "all":
        raise ValueError("effect_scope must be all or single")
    reference,candidate,assumptions = _draw(rng,trials,h*jobs,plan["looks"][-1]+1,
        ratio=ratios,threshold=policy.threshold,distribution=spec["distribution"],
        rho=spec["latent_cross_case_correlation"])
    nominate = candidate[:,:,0]/reference[:,:,0] > policy.screen_threshold
    rejected = np.zeros_like(nominate)
    futile = np.zeros_like(nominate)
    fresh_pairs = np.zeros_like(nominate,dtype=np.int64)
    positives = np.zeros_like(nominate,dtype=np.int64)
    per_look_counts = []
    tail_tables = {look:np.array([_sign_tail(k,look) for k in range(look+1)]) for look in plan["looks"]}
    for count in range(1,plan["looks"][-1]+1):
        active = nominate & ~rejected & ~futile
        fresh_pairs += active
        positives += active & (candidate[:,:,count] > policy.threshold*reference[:,:,count])
        if count in plan["looks"]:
            new = active & (tail_tables[count][positives] <= plan["per_confirmation_look_alpha"])
            rejected |= new
            per_look_counts.append({"look":count,"new_rejected_hypotheses":int(np.sum(new))})
        reachable = np.zeros_like(active)
        for look in plan["looks"]:
            if look >= count:
                maximum_positive = positives+look-count
                reachable |= tail_tables[look][maximum_positive] <= plan["per_confirmation_look_alpha"]
        futile |= active & ~rejected & ~reachable
    workflow = np.any(rejected,axis=1)
    for trial in range(min(parity_workflows,trials)):
        _replay_one(plan,reference[trial],candidate[trial],workflow[trial],
                    int(np.sum(fresh_pairs[trial])),trial=trial,h=h,jobs=jobs)
    blocked = int(np.sum(workflow))
    sign_probabilities = population_sign_probability(spec,ratios,policy.threshold)
    affected = sign_probabilities.reshape(-1)>.5
    null = not bool(np.any(affected))
    mean_null = not bool(np.any(ratios>policy.threshold))
    detected = np.any(rejected & affected.reshape(1,-1),axis=1)
    detections = int(np.sum(detected))
    total_pairs = h*jobs+np.sum(fresh_pairs,axis=1)
    return {**spec,"trials":trials,"seed":seed,"assumption_status":assumptions,
        "plan":plan,"blocked_workflows":blocked,"blocked_rate":blocked/trials,
        "arithmetic_mean_null":mean_null,"median_sign_null":null,
        "population_probability_above_threshold":sign_probabilities.reshape(-1).tolist(),
        "mean_null_block_rate_is_not_median_false_block_rate":mean_null and not null,
        "blocked_rate_95_percent_interval":binomial_interval(blocked,trials),
        "false_block_workflows":blocked if null else None,
        "missed_regression_workflows":trials-detections if not null else None,
        "false_block_rate":blocked/trials if null else None,
        "miss_rate":(trials-detections)/trials if not null else None,
        "truly_regressed_hypotheses":int(np.sum(affected)),
        "detected_regression_workflows":detections if not null else None,
        "detection_rate_95_percent_interval":binomial_interval(detections,trials) if not null else None,
        "mean_nominated_hypotheses":float(np.mean(np.sum(nominate,axis=1))),
        "mean_confirmation_pairs":float(np.mean(np.sum(fresh_pairs,axis=1))),
        "mean_total_pairs_including_screen":float(np.mean(total_pairs)),
        "pair_count_is_not_processor_wall_time":True,
        "per_look_rejections":per_look_counts,
        "actual_aggregator_parity_workflows":min(parity_workflows,trials),
        "actual_aggregator_parity":"all matched",
        "sample_distribution_scope":"declared synthetic law only; no universal machine-timing false-block guarantee"}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trials",type=int,default=5000)
    parser.add_argument("--parity-workflows",type=int,default=4)
    parser.add_argument("--ci-hypotheses",type=int,default=36)
    parser.add_argument("--dev-hypotheses",type=int,default=39)
    parser.add_argument("--seed",type=int,default=184231)
    parser.add_argument("--campaign",choices=("base","extensions","crossover"),default="base")
    parser.add_argument("--output",type=Path,required=True)
    args = parser.parse_args(argv)
    if args.trials < 1 or args.ci_hypotheses < 1 or args.dev_hypotheses < 1 or args.parity_workflows < 0:
        parser.error("positive counts and nonnegative parity count required")
    started = time.perf_counter()
    specs = (scenarios if args.campaign == "base" else
             extension_scenarios if args.campaign == "extensions" else
             crossover_scenarios)(args.ci_hypotheses,args.dev_hypotheses)
    simulator = simulate_crossover_case if args.campaign == "crossover" else simulate_case
    rows = [simulator(spec,trials=args.trials,seed=args.seed+i*1009,
                      parity_workflows=args.parity_workflows) for i,spec in enumerate(specs)]
    for row in rows:
        row["blocked_rate_simultaneous_95_percent_interval"] = binomial_interval(
            row["blocked_workflows"],row["trials"],alpha=.05/len(rows))
    result = {"schema":("triad-performance-guard-crossover-controls-v1"
                        if args.campaign=="crossover" else SCHEMA),
        "measurement_protocol":("randomized-crossover-block-v1"
                                 if args.campaign=="crossover" else "legacy-pair-v2"),
        "cases":rows,"scenario_count":len(rows),
        "source_sha256":{Path(__file__).name:_hash(__file__),
            "triad_perf_guard_policy.py":_hash(Path(__file__).with_name("triad_perf_guard_policy.py"))},
        "elapsed_seconds":time.perf_counter()-started,
        "interval_scope":"per-scenario exact binomial intervals; separate Bonferroni intervals cover all declared scenarios",
        "qualification_scope":"policy model checks, not measured hardware/receiver performance",
        "screen_scope":("force-nominated neutral screens; this campaign validates complete-workflow confirmatory inference, not nomination error or screen wall cost"
                        if args.campaign=="crossover" else "legacy paired-screen distribution model"),
        "existing_policy_changed":False}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with args.output.open("x") as stream:
        json.dump(result,stream,indent=2,allow_nan=False);stream.write("\n")
    print(json.dumps({"scenario_count":len(rows),"elapsed_seconds":result["elapsed_seconds"],
        "output":str(args.output),"actual_aggregator_workflows":sum(row["actual_aggregator_parity_workflows"] for row in rows)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
