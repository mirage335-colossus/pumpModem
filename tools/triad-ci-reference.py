#!/usr/bin/env python3
"""Select a native CI reference; allow only the declared initial port transition."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

APPROVED_PREPORT = "1c58507a5178c7700cd15d0fcdd05a0091c6dba9"
NATIVE_PROTOCOL = "datapump.triad.native-probe.v1"
PROBE_SOURCE = "tools/triad-receiver-probe.cpp"


def git(repository, *args):
    result = subprocess.run(["git", "-C", str(repository), *args], check=False,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode:
        raise ValueError(result.stderr.strip() or "Git could not resolve native CI inputs")
    return result.stdout.strip()


def protocol(repository, revision):
    # A missing file is distinct from a malformed or unsupported implementation.
    path = f"{revision}:{PROBE_SOURCE}"
    if not git(repository, "ls-tree", "--name-only", revision, "--", PROBE_SOURCE):
        return None
    source = git(repository, "show", path)
    markers = re.findall(r'constexpr\s+auto\s+schema\s*=\s*"([^"\n]+)"\s*;', source)
    if markers != [NATIVE_PROTOCOL]:
        raise ValueError(f"{revision} has a malformed or unsupported native probe protocol")
    return markers[0]


def select_reference(repository, *, reference_ref="", establish_baseline=False):
    repository = Path(repository).resolve()
    candidate = git(repository, "rev-parse", "--verify", "HEAD^{commit}")
    if protocol(repository, candidate) != NATIVE_PROTOCOL:
        raise ValueError("Candidate does not implement the native measurement protocol")
    if establish_baseline:
        if reference_ref:
            raise ValueError("Explicit baseline establishment cannot also select a reference")
        return {"mode": "manual_establishment", "candidate_sha": candidate,
                "reference_sha": None, "native_protocol": NATIVE_PROTOCOL,
                "relative_regression_pass": False}
    reference = git(repository, "rev-parse", "--verify", f"{reference_ref or 'HEAD^'}^{{commit}}")
    reference_protocol = protocol(repository, reference)
    if reference_protocol == NATIVE_PROTOCOL:
        if reference == candidate:
            raise ValueError("A native regression reference must differ from the candidate")
        mode = "compare"
    elif not reference_ref and reference == APPROVED_PREPORT:
        # The exact first-parent transition introduces the native protocol. This
        # does not turn a later missing implementation or failed build into a pass.
        mode = "initial_native_establishment"
    else:
        raise ValueError("Native regression baseline unavailable; historical Python timings are inapplicable")
    return {"mode": mode, "candidate_sha": candidate, "reference_sha": reference,
            "native_protocol": NATIVE_PROTOCOL, "relative_regression_pass": False}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument("--reference-ref", default="")
    parser.add_argument("--establish-baseline", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--github-output", type=Path)
    args = parser.parse_args(argv)
    try:
        result = select_reference(args.repository, reference_ref=args.reference_ref,
                                  establish_baseline=args.establish_baseline)
    except (OSError, ValueError) as error:
        result = {"mode": "error", "reason": str(error), "relative_regression_pass": False}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    if args.github_output and result["mode"] != "error":
        with args.github_output.open("a", encoding="utf-8") as stream:
            stream.write(f"mode={result['mode']}\nreference_sha={result['reference_sha'] or ''}\n")
    print(result.get("reason", result["mode"]))
    return 2 if result["mode"] == "error" else 0


if __name__ == "__main__":
    sys.exit(main())
