#!/usr/bin/env python3
"""Check bounded profiler records; optional second trace checks resume pairing.

Run against root-produced traces from the SAME exact source/configuration and
capture. Complete traces include destructor records and a successful trace_end.
Attempt/preparation counts may differ after cancellation; paired result matrices
must match successful uninterrupted and resumed original-event completion.
"""
import argparse
import json
from pathlib import Path

EVENTS = {"scheduled_certificate_preparation", "scheduled_certificate_cold", "scheduled_certificate_pairs"}
COHORTS = {"fp32_exact", "fp32_proxy", "fp64_exact", "fp64_proxy", "unavailable"}
PAIR_FIELDS = ("completed", "raw_exceeds_upper", "raw_below_threshold", "guarded_below_threshold", "upper", "raw", "guarded", "upper_raw", "upper_guarded")

def read(path, finalization_log):
    records = []
    all_records = []
    for line in Path(path).read_bytes().splitlines():
        item = json.loads(line)
        all_records.append(item)
        if item.get("event") in EVENTS:
            assert len(line) < 16384, "oversized diagnostic record"
            assert item["cohort"] in COHORTS
            records.append(item)
    assert records, "no enabled certificate diagnostics records"
    assert all_records[0]["event"] == "trace_begin" and all_records[-1]["event"] == "trace_end", "trace lacks complete begin/end"
    begin, end = all_records[0], all_records[-1]
    assert begin["token"] == end["token"] and end["finalization_required"]
    assert end["dropped"] == 0 and not end["size_limited"] and not end["prior_io_failed"], "trace producer coverage incomplete"
    seals = []
    for line in Path(finalization_log).read_text().splitlines():
        try:
            item = json.loads(line)
        except json.JSONDecodeError:
            continue
        if item.get("event") == "trace_finalization" and item.get("token") == end["token"]:
            seals.append(item)
    assert len(seals) == 1, "missing or ambiguous matching external finalization seal"
    seal = seals[0]
    assert Path(seal["path"]).resolve() == Path(path).resolve()
    assert seal["result"] == "complete" and seal["flush_ok"] and seal["close_ok"]
    assert not seal["prior_io_failed"] and not seal["size_limited"] and seal["dropped"] == 0
    assert seal["attempted"] == end["attempted"], "finalized trace counters differ"
    pairs = [item for item in records if item["event"] == "scheduled_certificate_pairs"]
    assert pairs, "completed-pair records missing"
    for item in pairs:
        assert item["pair_identity_mismatches"] == 0, "diagnostic identity mismatch"
        assert not item["pending_pair_at_destruction"], "unfinished original event omitted from pairing"
        assert not item["saturated"] and not item["state_counter_saturated"], "saturated counters need separate interpretation"
        for field in ("upper", "raw", "guarded"):
            assert len(item[field]) == 12 and sum(item[field]) == item["completed"]
        for matrix, marginal in (("upper_raw", "raw"), ("upper_guarded", "guarded")):
            values = item[matrix]
            assert len(values) == 144 and sum(values) == item["completed"]
            assert [sum(values[row*12:(row+1)*12]) for row in range(12)] == item["upper"]
            assert [sum(values[row*12+column] for row in range(12)) for column in range(12)] == item[marginal]
    return pairs

def totals(pairs):
    result = {}
    for item in pairs:
        key = (item["epoch"], item["cohort"])
        target = result.setdefault(key, {name: ([0]*len(item[name]) if isinstance(item[name], list) else 0) for name in PAIR_FIELDS})
        for name in PAIR_FIELDS:
            if isinstance(item[name], list):
                target[name] = [a+b for a,b in zip(target[name], item[name])]
            else:
                target[name] += item[name]
    return result

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("trace")
    parser.add_argument("--finalization-log", required=True)
    parser.add_argument("--same-capture-resumed-trace")
    parser.add_argument("--same-capture-resumed-finalization-log")
    args = parser.parse_args()
    pairs = read(args.trace, args.finalization_log)
    if args.same_capture_resumed_trace:
        assert args.same_capture_resumed_finalization_log, "resumed trace needs its external finalization log"
        assert totals(pairs) == totals(read(args.same_capture_resumed_trace, args.same_capture_resumed_finalization_log)), "completed native/original pair results changed after resume"
    print("bounded certificate trace and completed-pair checks passed")

if __name__ == "__main__":
    main()
