#!/usr/bin/env python3
"""Compare two triad_receiver_probe JSON outputs for non-timing differences.

Usage: triad_receipt_compare.py A.json B.json [--scope all|slot] [--rtol X] [--show-timing]
                  [--show-info]

Every scalar is flattened to a path.  Phase lists are keyed by phase name
(/phases{native_slot}/work/...), other lists by index.  Each path falls in
exactly one class:

  compared      path does not match the strip regex and is not environment.
                Any difference FAILS (exit 1).
  stripped      some path component matches the strip regex
                (seconds|wall|timing|cpu|elapsed|_ms|_ns|rss|throughput|
                per_second|clock|utilization) but the value is NOT a measured
                duration (e.g. midpoint_timing/*, varying_timing_proposals/*,
                clock_ppm, bit_seconds, deadline_ms, cpu_fft_kernel).  These
                are checked separately; any difference FAILS (exit 1).
  measured      a measured wall/CPU/device duration: a *seconds leaf under
                /phases or /identity that is not a signal-domain quantity.
                Differences are informational only (--show-timing lists them).
  environment   RAM-sized limits (allocation/memory_limit_bytes,
                generation_preflight_reserved_bytes) and source/build identity
                (source_commit_at_configure, source_manifest_sha256,
                build_options_sha256).  Informational only (--show-info).

--rtol X tolerates numeric differences with relative error <= X in the two
failing classes; they are listed as "within rtol" and do not fail.  Default 0
(exact; use only for exactness class E2 checks).
--scope slot restricts the comparison to the native_slot phase work, the
result and observation/input_sha256 (useful across backends).
Exit status: 0 no failing differences, 1 failing differences, 2 usage error.
"""
import json
import math
import re
import sys

STRIP_RE = re.compile(
    r"seconds|wall|timing|cpu|elapsed|_ms|_ns|rss|throughput|per_second|"
    r"clock|utilization")
# Leaves ending in "seconds" that are signal-domain values, not durations.
SIGNAL_SECONDS = {
    "represented_seconds", "arrival_seconds", "epoch_offset_seconds",
    "slow_gain_coherence_seconds", "bit_seconds", "knot_seconds",
    "excess_delay_seconds",
}
ENVIRONMENT_LEAVES = {
    "memory_limit_bytes", "generation_preflight_reserved_bytes",
    "source_commit_at_configure", "source_manifest_sha256",
    "build_options_sha256",
}


def flatten(value, path, out):
    if isinstance(value, dict):
        for key, item in value.items():
            flatten(item, path + "/" + key, out)
    elif isinstance(value, list):
        named = all(isinstance(item, dict) and isinstance(item.get("name"), str)
                    for item in value) and value
        for index, item in enumerate(value):
            if named and path.endswith("/phases"):
                label = "{%s}" % item["name"]
                if any(other["name"] == item["name"] for other in value[:index]):
                    label = "{%s#%d}" % (item["name"], index)
                flatten(item, path + label, out)
            else:
                flatten(item, path + "[%d]" % index, out)
    else:
        out[path] = value
    return out


def components(path):
    return [re.sub(r"[\[{].*$", "", part) for part in path.split("/") if part]


def classify(path):
    parts = components(path)
    leaf = parts[-1] if parts else ""
    if leaf in ENVIRONMENT_LEAVES:
        return "environment"
    if not any(STRIP_RE.search(part) for part in parts):
        return "compared"
    if (parts and parts[0] in ("phases", "identity") and leaf.endswith("seconds")
            and leaf not in SIGNAL_SECONDS):
        return "measured"
    return "stripped"


def scoped(flat, scope):
    if scope == "all":
        return flat
    keep = {}
    for path, value in flat.items():
        if (path.startswith("/phases{native_slot}/work/") or
                path.startswith("/result/") or
                path == "/observation/input_sha256" or path == "/status"):
            keep[path] = value
    return keep


def numeric(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def relative(a, b):
    if a == b:
        return 0.0
    if any(isinstance(x, float) and not math.isfinite(x) for x in (a, b)):
        return math.inf
    return abs(a - b) / max(abs(a), abs(b), 1e-300)


def main(argv):
    args = []
    scope = "all"
    rtol = 0.0
    show_timing = False
    show_info = False
    index = 0
    while index < len(argv):
        arg = argv[index]
        if arg == "--scope":
            index += 1
            scope = argv[index]
        elif arg == "--rtol":
            index += 1
            rtol = float(argv[index])
        elif arg == "--show-timing":
            show_timing = True
        elif arg == "--show-info":
            show_info = True
        elif arg.startswith("--"):
            print("unknown option " + arg, file=sys.stderr)
            return 2
        else:
            args.append(arg)
        index += 1
    if len(args) != 2 or scope not in ("all", "slot"):
        print(__doc__, file=sys.stderr)
        return 2
    with open(args[0]) as handle:
        a = scoped(flatten(json.load(handle), "", {}), scope)
    with open(args[1]) as handle:
        b = scoped(flatten(json.load(handle), "", {}), scope)
    missing = object()
    buckets = {"compared": [], "stripped": [], "measured": [], "environment": []}
    tolerated = []
    for path in sorted(set(a) | set(b)):
        u, v = a.get(path, missing), b.get(path, missing)
        if u is not missing and v is not missing and type(u) == type(v) and u == v:
            continue
        if (u is not missing and v is not missing and numeric(u) and numeric(v)
                and isinstance(u, float) and isinstance(v, float)
                and math.isnan(u) and math.isnan(v)):
            continue
        kind = classify(path)
        if (kind in ("compared", "stripped") and rtol > 0 and u is not missing
                and v is not missing and numeric(u) and numeric(v)
                and relative(u, v) <= rtol):
            tolerated.append((path, u, v))
            continue
        buckets[kind].append((path, u, v))

    def show(rows):
        for path, u, v in rows:
            us = "<missing>" if u is missing else repr(u)
            vs = "<missing>" if v is missing else repr(v)
            extra = ""
            if u is not missing and v is not missing and numeric(u) and numeric(v):
                extra = "  rel=%.3e" % relative(u, v)
            print("  %s: %s -> %s%s" % (path, us, vs, extra))

    print("scope=%s rtol=%g  A=%s  B=%s" % (scope, rtol, args[0], args[1]))
    print("compared (non-stripped) differences: %d" % len(buckets["compared"]))
    show(buckets["compared"])
    print("stripped non-timing differences: %d" % len(buckets["stripped"]))
    show(buckets["stripped"])
    if rtol > 0:
        print("numeric differences within rtol (not failing): %d" % len(tolerated))
        show(tolerated)
    print("measured timing differences (informational): %d" % len(buckets["measured"]))
    if show_timing:
        show(buckets["measured"])
    print("environment/identity differences (informational): %d"
          % len(buckets["environment"]))
    if show_info:
        show(buckets["environment"])
    failing = len(buckets["compared"]) + len(buckets["stripped"])
    print("RESULT: %s (%d failing non-timing differences)"
          % ("DIFFERENT" if failing else "IDENTICAL", failing))
    return 1 if failing else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
