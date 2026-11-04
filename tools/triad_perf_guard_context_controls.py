#!/usr/bin/env python3
"""Small, source-bound controls for the fresh-context guard protocol.

This qualification-only tool runs real full FFT/family adapter targets with
identical A/A sources and records fresh worker identities. It adds a disclosed
logical worker-slot delay to the adapter's reported complete-target duration
to replay the old permanent source/worker association and the balanced
creation-order crossover on the same observed target times. The injected delay
is a test control, not a receiver or hardware slowdown. It cannot establish a
future-hardware false-block rate by itself.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import random
import sys
import time

from triad_perf_guard_policy import (BlockObservation, GuardPolicy, PairObservation,
                                     build_guard_plan, confirm_case, screen_case)


PROTOCOL = "fresh-source-context-pair-v1"
SLOT_DELAYS_SECONDS = {"slow": 0.190, "fast": 0.010}


def _write(path: Path, obj):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2, sort_keys=True, allow_nan=False) + "\n")


def _context_ids(raw):
    context = raw.get("details", {}).get("confirmation_context")
    if not isinstance(context, dict) or context.get("protocol") != PROTOCOL:
        raise ValueError("adapter omitted the frozen fresh-context protocol receipt")
    if context.get("target_boundary_preserved") is not True:
        raise ValueError("adapter changed the confirmed full-target boundary")
    if context.get("creation_order") != raw["details"].get("order"):
        raise ValueError("worker creation order differs from the measured source order")
    ids = context.get("context_ids")
    if not isinstance(ids, dict) or set(ids) != {"reference", "candidate"}:
        raise ValueError("adapter omitted both source context identities")
    if len(set(ids.values())) != 2 or any(not isinstance(v, str) or not v for v in ids.values()):
        raise ValueError("source contexts are not distinct")
    # Worker-backed adapters must expose actual PIDs. Fresh subprocess adapters
    # instead expose unique launch receipts in their raw isolation evidence.
    evidence = context.get("workers")
    if evidence is not None:
        if set(evidence) != {"reference", "candidate"}:
            raise ValueError("worker context evidence has missing source")
        pids = [evidence[s].get("pid") for s in ("reference", "candidate")]
        if any(type(pid) is not int or pid <= 0 for pid in pids) or len(set(pids)) != 2:
            raise ValueError("fresh workers need distinct positive process IDs")
    else:
        launches = context.get("isolation_evidence")
        if not isinstance(launches, dict) or set(launches) != {"reference", "candidate"}:
            raise ValueError("fresh process adapters omitted launch isolation receipts")
        if any(not row.get("raw_launch_receipt") for row in launches.values()):
            raise ValueError("fresh process launch receipt path is missing")
    return context


def inject_slot_delays(raw, *, mode: str, order, permanent_source_slot=None,
                       apply_sleep=False):
    """Apply transparent, bounded delay labels to complete-target adapter time.

    ``crossover`` maps the slower logical execution slot from creation order for
    every period. ``persistent`` pins that slot to the source that owned it on
    the first period, reproducing the old source/session association. Every raw
    elapsed value and injected amount is retained before block aggregation.
    """
    if mode not in ("crossover", "persistent"):
        raise ValueError("control mode must be crossover or persistent")
    context = _context_ids(raw)
    if mode == "persistent" and permanent_source_slot is None:
        permanent_source_slot = {order[0]: "slow", order[1]: "fast"}
    by_source = {}
    for slot, source in enumerate(order):
        by_source[source] = ("slow" if slot == 0 else "fast") if mode == "crossover" else permanent_source_slot[source]
    details = raw.setdefault("details", {})
    details["slot_delay_control"] = {"mode": mode, "seconds_by_slot": SLOT_DELAYS_SECONDS,
        "slot_by_source": by_source,
        "placement": "transparent addition to adapter complete-target duration after real target returns",
        "physical_slowdown_claim": False}
    for source in ("reference", "candidate"):
        key = source + "_seconds"
        base = raw[key]
        requested = SLOT_DELAYS_SECONDS[by_source[source]]
        injection_started = time.perf_counter()
        if apply_sleep and requested:
            time.sleep(requested)
        injected = time.perf_counter() - injection_started if apply_sleep else requested
        if not math.isfinite(base) or base <= 0:
            raise ValueError("adapter returned invalid complete-target duration")
        raw[key] = base + injected
        details["slot_delay_control"][source] = {"measured_base_seconds": base,
            "requested_seconds": requested, "injected_seconds": injected,
            "controlled_target_seconds": raw[key],
            "context_id": context["context_ids"][source]}
    return raw


def build_block(case_id, first, second, *, mode, permanent_source_slot=None,
                apply_sleep=False):
    if second["details"]["order"] != first["details"]["order"][::-1]:
        raise ValueError("four-arm block must contain opposite source orders")
    if first["run_id"] == second["run_id"]:
        raise ValueError("block periods must have distinct invocation IDs")
    first = inject_slot_delays(first, mode=mode, order=first["details"]["order"],
                               permanent_source_slot=permanent_source_slot,
                               apply_sleep=apply_sleep)
    second = inject_slot_delays(second, mode=mode, order=second["details"]["order"],
                                permanent_source_slot=permanent_source_slot,
                                apply_sleep=apply_sleep)
    ctx = (_context_ids(first), _context_ids(second))
    contexts = tuple(tuple(c["context_ids"][s] for s in ("reference", "candidate")) for c in ctx)
    block = BlockObservation("block:" + case_id + ":" + first["run_id"],
        (PairObservation(first["run_id"], first["reference_seconds"], first["candidate_seconds"]),
         PairObservation(second["run_id"], second["reference_seconds"], second["candidate_seconds"])),
        (tuple(first["details"]["order"]), tuple(second["details"]["order"])), contexts)
    return block, {"run_id": block.run_id, "pairs": [first, second],
                   "reference_seconds": block.reference_seconds,
                   "candidate_seconds": block.candidate_seconds,
                   "ratio": block.candidate_seconds / block.reference_seconds,
                   "orders": block.orders, "contexts": block.contexts,
                   "logical_slot_mode": mode}


def _make_adapter(kind, context):
    if kind == "fft":
        from triad_perf_guard_native_fft import NativeFFTWorkloads
        return NativeFFTWorkloads(context)
    if kind == "family":
        from triad_perf_guard_families import FamilyWorkloads
        return FamilyWorkloads(context)
    raise ValueError("case family must be fft or family")


def run_case(args, case_id, deadline):
    output = args.output / case_id.replace(":", "_")
    context = {"candidate_root": args.root, "reference_root": args.root,
        "binary": args.binary, "reference_binary": args.binary,
        "fixture_cache": args.fixture_cache, "output": output / "adapter",
        "device": args.device, "precision": args.precision, "threads": args.threads,
        "native_threads": args.native_threads, "deadline": deadline}
    kind = "fft" if case_id.startswith("fft:") else "family"
    adapter = _make_adapter(kind, context)
    try:
        descriptors = {row["id"]: row for row in adapter.descriptors()}
        if case_id not in descriptors or descriptors[case_id].get("available") is False:
            raise ValueError("requested source-bound full target is absent or unavailable: " + case_id)
        ids = [case_id]
        rng = random.Random(args.seed ^ sum(case_id.encode("utf-8")))
        block_coins = [rng.getrandbits(1) for _ in range(args.blocks)]
        base_orders = (("reference", "candidate"), ("candidate", "reference"))
        order_schedule = [(base_orders[c], base_orders[1-c]) for c in block_coins]
        plan = build_guard_plan(ids, GuardPolicy("ci", workflow_job_count=args.workflow_jobs),
            metadata={"control": "actual A/A adapter contexts plus disclosed target-time slot controls",
                      "source_identity": "same exact root and executable supplied for both source labels",
                      "case_id": case_id,
                      "balanced_block_assignment_coins": block_coins,
                      "block_period_order_schedule": [[list(a), list(b)] for a,b in order_schedule],
                      "schedule_frozen_before_measurement": True})
        screen_id = "screen:" + case_id
        screen_raw = adapter.measure_pair(case_id, screen_id, args.seed,
                                          order=("reference", "candidate"), phase="screen")
        screen = screen_case(plan, case_id,
            PairObservation(screen_id, screen_raw["reference_seconds"], screen_raw["candidate_seconds"]),
            force_confirmation=True)
        records = {"case_id": case_id, "descriptor": descriptors[case_id], "plan": plan,
                   "screen": screen_raw, "screen_decision": screen, "blocks": [], "elapsed_seconds": None}
        confirmations = []
        for index in range(args.blocks):
            if time.monotonic() >= deadline:
                records["budget_limited"] = True
                break
            orders = order_schedule[index]
            pairs = []
            for period, order in enumerate(orders):
                if time.monotonic() >= deadline:
                    raise TimeoutError("budget ended before a complete four-arm block")
                run_id = f"block-{index:03d}-period-{period}"
                seed = args.seed + 1_000_003 + index * 17 + period
                raw = adapter.measure_pair(case_id, run_id, seed, order=order, phase="confirmation")
                pairs.append(raw)
            permanent_slot = {"reference": "fast", "candidate": "slow"}
            for mode in ("persistent", "crossover"):
                block, retained = build_block(case_id, json.loads(json.dumps(pairs[0])),
                    json.loads(json.dumps(pairs[1])), mode=mode, permanent_source_slot=permanent_slot,
                    apply_sleep=True)
                # The old and new controls are replayed on the identical four
                # actual base target observations and context receipts.
                records["blocks"].append(retained)
                if mode == "crossover":
                    confirmations.append(block)
            if index + 1 in plan["looks"]:
                decision = confirm_case(plan, case_id, confirmations, screen_run_ids=[screen_id])
                records.setdefault("look_decisions", []).append(decision)
        records["elapsed_seconds"] = time.monotonic() - args.started
        records["elapsed_scope"] = "cumulative campaign wall through this case, before final adapter cleanup"
        records["adapter_lifecycle"] = adapter.summary() if hasattr(adapter, "summary") else None
        return records
    finally:
        adapter.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--fixture-cache", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--case", action="append", required=True,
                        help="one source-bound full target (fft:ID or family:ID)")
    parser.add_argument("--device", choices=("cpu", "cuda"), default="cpu")
    parser.add_argument("--precision", choices=("fp32", "fp64"), default="fp32")
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--native-threads", type=int, default=1)
    parser.add_argument("--blocks", type=int, default=4,
                        help="complete balanced blocks per case; 4 confirms mechanism, 12 can exercise a look")
    parser.add_argument("--workflow-jobs", type=int, default=33)
    parser.add_argument("--seed", type=int, default=719_991)
    parser.add_argument("--budget-seconds", type=float, default=900.)
    args = parser.parse_args(argv)
    args.root = args.root.resolve(strict=True)
    args.binary = args.binary.resolve(strict=True)
    args.fixture_cache = args.fixture_cache.resolve()
    args.output = args.output.resolve()
    if args.output.exists():
        raise ValueError("output directory must be new")
    if not args.case or len(args.case) != len(set(args.case)) or args.blocks < 1 or args.threads < 1 or args.native_threads < 1:
        raise ValueError("nonempty unique cases and positive block/thread counts required")
    if not 0 < args.budget_seconds <= 2400 or args.workflow_jobs < 1 or args.seed < 0:
        raise ValueError("budget, workflow multiplicity or seed is invalid")
    if args.device == "cuda" and args.precision != "fp32":
        raise ValueError("the bounded GPU profile uses explicit FP32; FP64 tensor family is unavailable")
    args.output.mkdir(parents=True)
    args.started = time.monotonic()
    deadline = args.started + args.budget_seconds
    summary = {"schema": "triad-context-protocol-controls-v1", "case_results": [],
        "scope": "actual same-source full targets; real wrapper sleeps added inside the guard control wrapper and reflected in its complete-target return; no physical-slowdown or future-hardware guarantee",
        "false_block_rate_claim": None, "budget_seconds": args.budget_seconds}
    _write(args.output / "summary.json", summary)
    try:
        for case_id in args.case:
            row = run_case(args, case_id, deadline)
            _write(args.output / (case_id.replace(":", "_") + ".json"), row)
            summary["case_results"].append({"case_id": case_id,
                "blocks_completed": len(row["blocks"]) // 2,
                "elapsed_seconds": row["elapsed_seconds"],
                "elapsed_scope": row["elapsed_scope"],
                "crossover_look_decisions": row.get("look_decisions", []),
                "block_ratios": [r["ratio"] for r in row["blocks"]]})
            _write(args.output / "summary.json", summary)
        summary["status"] = "completed"
        summary["elapsed_seconds"] = time.monotonic() - args.started
    except Exception as error:
        summary["status"] = "incomplete"
        summary["error"] = type(error).__name__ + ": " + str(error)
        summary["elapsed_seconds"] = time.monotonic() - args.started
        _write(args.output / "summary.json", summary)
        print(json.dumps({"status": summary["status"], "error": summary["error"]}))
        return 1
    _write(args.output / "summary.json", summary)
    print(json.dumps({"status": summary["status"], "elapsed_seconds": summary["elapsed_seconds"],
                      "case_count": len(summary["case_results"])}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
