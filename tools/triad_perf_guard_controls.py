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

from triad_perf_guard_policy import (GuardPolicy, PairObservation, _sign_tail,
                                    build_guard_plan, build_guard_report,
                                    confirmation_futility, confirm_case, screen_case)


SCHEMA = "triad-performance-guard-distribution-controls-v2"


def _hash(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def binomial_interval(successes, trials, alpha=.05):
    if type(successes) is not int or type(trials) is not int or not 0 <= successes <= trials or trials < 1:
        raise ValueError("valid integer binomial counts required")
    return [0. if successes == 0 else float(beta.ppf(alpha/2, successes, trials-successes+1)),
            1. if successes == trials else float(beta.ppf(1-alpha/2, successes+1, trials-successes))]


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
                            metadata={"experiment": "synthetic distribution control"})
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
    parser.add_argument("--campaign",choices=("base","extensions"),default="base")
    parser.add_argument("--output",type=Path,required=True)
    args = parser.parse_args(argv)
    if args.trials < 1 or args.ci_hypotheses < 1 or args.dev_hypotheses < 1 or args.parity_workflows < 0:
        parser.error("positive counts and nonnegative parity count required")
    started = time.perf_counter()
    specs = (scenarios if args.campaign == "base" else extension_scenarios)(args.ci_hypotheses,args.dev_hypotheses)
    rows = [simulate_case(spec,trials=args.trials,seed=args.seed+i*1009,
                          parity_workflows=args.parity_workflows) for i,spec in enumerate(specs)]
    for row in rows:
        row["blocked_rate_simultaneous_95_percent_interval"] = binomial_interval(
            row["blocked_workflows"],row["trials"],alpha=.05/len(rows))
    result = {"schema":SCHEMA,"cases":rows,"scenario_count":len(rows),
        "source_sha256":{Path(__file__).name:_hash(__file__),
            "triad_perf_guard_policy.py":_hash(Path(__file__).with_name("triad_perf_guard_policy.py"))},
        "elapsed_seconds":time.perf_counter()-started,
        "interval_scope":"per-scenario exact binomial intervals; separate Bonferroni intervals cover all declared scenarios",
        "qualification_scope":"policy model checks, not measured hardware/receiver performance",
        "existing_policy_changed":False}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with args.output.open("x") as stream:
        json.dump(result,stream,indent=2,allow_nan=False);stream.write("\n")
    print(json.dumps({"scenario_count":len(rows),"elapsed_seconds":result["elapsed_seconds"],
        "output":str(args.output),"actual_aggregator_workflows":sum(row["actual_aggregator_parity_workflows"] for row in rows)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
